/*
 * Copyright (c) 2015 ARM. All rights reserved.
 */
#include <unistd.h>
#include <pthread.h>
#include <signal.h> /* For SIGIGN and SIGINT */
#include "lwm2m-client/m2minterfacefactory.h"
#include "lwm2m-client/m2mdevice.h"
#include "lwm2m-client/m2minterfaceobserver.h"
#include "lwm2m-client/m2minterface.h"
#include "lwm2m-client/m2mobjectinstance.h"
#include "lwm2m-client/m2mresource.h"
#include "security.h"

// Select connection mode: Psk, Certificate or NoSecurity
M2MSecurity::SecurityModeType CONN_MODE = M2MSecurity::Psk;

// Enter your mbed Device Server's IPv4 address and Port number in
// mentioned format like coap://192.168.0.1
const String &M2M_SERVER_ADDRESS = "coap://<xxx.xxx.xxx.xxx>";
//If you use secure connection port is 5684, for non-secure port is 5683
const int &M2M_SERVER_PORT = 5683;

const String &ENDPOINT_NAME = "lwm2m-endpoint";

const String &MANUFACTURER = "manufacturer";
const String &TYPE = "type";
const String &MODEL_NUMBER = "2015";
const String &SERIAL_NUMBER = "12345";

const uint8_t value[] = "MyValue";
const uint8_t STATIC_VALUE[] = "Static value";

static void ctrl_c_handle_function(void);
typedef void (*signalhandler_t)(int); /* Function pointer type for ctrl-c */

class M2MLWClient: public M2MInterfaceObserver {
public:
    M2MLWClient(){
        _security = NULL;
        _interface = NULL;
        _device = NULL;
        _object = NULL;
        _error = false;
        _registered = false;
        _unregistered = false;
        _registration_updated = false;
        _value = 0;
    }

    ~M2MLWClient() {
        if(_security) {
            delete _security;
        }
        if( _register_security){
            delete _register_security;
        }
        if(_device) {
            M2MDevice::delete_instance();
        }
        if(_object) {
            delete _object;
        }
        if(_interface) {
            delete _interface;
        }
    }

    bool create_interface() {
        _interface = M2MInterfaceFactory::create_interface(*this,
                                                  ENDPOINT_NAME,
                                                  "test",
                                                  60,
                                                  M2M_SERVER_PORT,
                                                  "",
                                                  M2MInterface::UDP,
                                                  M2MInterface::LwIP_IPv4,
                                                  "");
        printf("Endpoint Name : linux-endpoint\n");
        return (_interface == NULL) ? false : true;
    }

    bool register_successful() {
        while(!_registered && !_error) {
            sleep(1);
        }
        return _registered;
    }

    bool unregister_successful() {
        while(!_unregistered && !_error) {
            sleep(1);
        }
        return _unregistered;
    }

    bool registration_update_successful() {
        while(!_registration_updated && !_error) {
        }
        return _registration_updated;
    }

    bool create_register_object() {
        bool success = false;
        _register_security = M2MInterfaceFactory::create_security(M2MSecurity::M2MServer);
        if(_register_security) {
            char buffer[6];
            sprintf(buffer,"%d",M2M_SERVER_PORT);
            m2m::String port(buffer);

            m2m::String addr = M2M_SERVER_ADDRESS;
            addr.append(":", 1);
            addr.append(port.c_str(), size_t(port.size()) );
            if(_register_security->set_resource_value(M2MSecurity::M2MServerUri, addr)) {
                if( CONN_MODE == M2MSecurity::Certificate ){
                    _register_security->set_resource_value(M2MSecurity::SecurityMode, M2MSecurity::Certificate);
                    _register_security->set_resource_value(M2MSecurity::ServerPublicKey,SERVER_CERT,sizeof(SERVER_CERT));
                    _register_security->set_resource_value(M2MSecurity::PublicKey,CERT,sizeof(CERT));
                    _register_security->set_resource_value(M2MSecurity::Secretkey,KEY,sizeof(KEY));
                }else if( CONN_MODE == M2MSecurity::Psk ){
                    _register_security->set_resource_value(M2MSecurity::SecurityMode, M2MSecurity::Psk);
                    _register_security->set_resource_value(M2MSecurity::ServerPublicKey,PSK_IDENTITY,sizeof(PSK_IDENTITY));
                    _register_security->set_resource_value(M2MSecurity::PublicKey,PSK_IDENTITY,sizeof(PSK_IDENTITY));
                    _register_security->set_resource_value(M2MSecurity::Secretkey,PSK,sizeof(PSK));
                }else{
                    _register_security->set_resource_value(M2MSecurity::SecurityMode, M2MSecurity::NoSecurity);
                }
                success = true;
            }
        }
        return success;
    }

    bool create_device_object() {
        bool success = false;
        _device = M2MInterfaceFactory::create_device();
        if(_device) {
            if(_device->create_resource(M2MDevice::Manufacturer,MANUFACTURER)     &&
               _device->create_resource(M2MDevice::DeviceType,TYPE)        &&
               _device->create_resource(M2MDevice::ModelNumber,MODEL_NUMBER)      &&
               _device->create_resource(M2MDevice::SerialNumber,SERIAL_NUMBER)) {
                success = true;
            }
        }
        return success;
    }

    void execute_function(void *argument) {
        if(argument) {
            char* arguments = (char*)argument;
            printf("Received %s!!\n", arguments);
        }
        printf("I am executed !!\n");
    }

    bool create_generic_object() {
        bool success = false;
        _object = M2MInterfaceFactory::create_object("Test");
        if(_object) {
            M2MObjectInstance* inst = _object->create_object_instance();
            if(inst) {
                M2MResource* res = inst->create_dynamic_resource("D","ResourceTest",true);
                char buffer[20];
                int size = sprintf(buffer,"%d",_value);
                  res->set_operation(M2MBase::GET_PUT_POST_ALLOWED);
                  res->set_value((const uint8_t*)buffer,
                                 (const uint32_t)size);
                  res->set_execute_function(execute_callback(this,&M2MLWClient::execute_function));
                _value++;
                inst->create_static_resource("S",
                                             "ResourceTest",
                                             STATIC_VALUE,
                                             sizeof(STATIC_VALUE)-1);
            }
        }
        return success;
    }

    void update_resource() {
        if(_object) {
            M2MObjectInstance* inst = _object->object_instance();
            if(inst) {
                M2MResource* res = inst->resource("D");
                printf(" Value sent %d\n", _value);
                char buffer[20];
                int size = sprintf(buffer,"%d",_value);
                res->set_value((const uint8_t*)buffer,
                               (const uint32_t)size,
                               true);
                _value++;
            }
        }
    }

    void test_register(){
        M2MObjectList object_list;
        object_list.push_back(_device);
        object_list.push_back(_object);
    if(_interface) {
            _interface->register_object(_register_security,object_list);
        } else {
        printf("Interface doesn't exist, exiting!!\n");
            exit(1);
    }
    }

    void test_update_register() {
        uint32_t updated_lifetime = 20;
        _registered = false;
        _unregistered = false;
        if(_interface) {
            _interface->update_registration(_register_security,updated_lifetime);
        }
    }

    void test_unregister() {
        if(_interface) {
            _interface->unregister_object(NULL);
        }
    }

    void bootstrap_done(M2MSecurity */*server_object*/){
    }

    void object_registered(M2MSecurity */*security_object*/, const M2MServer &/*server_object*/){
        _registered = true;
        printf("\nRegistered\n");
    }

    void object_unregistered(M2MSecurity */*server_object*/){
        _unregistered = true;
        printf("\nUnregistered\n");
    }

    void registration_updated(M2MSecurity */*security_object*/, const M2MServer & /*server_object*/){
        _registration_updated = true;
        printf("\nregistration updated\n");

    }

    void error(M2MInterface::Error error){
        _error = true;
        printf("\nError occured Error Code : %d\n", (int8_t)error);

    }

    void value_updated(M2MBase *base, M2MBase::BaseType type) {
        printf("\nValue updated of Object name %s and Type %d\n",
               base->name().c_str(), type);
    }

private:

    M2MInterface        *_interface;
    M2MSecurity         *_security;
    M2MSecurity         *_register_security;
    M2MDevice           *_device;
    M2MObject           *_object;
    bool                _error;
    bool                _registered;
    bool                _unregistered;
    bool                _registration_updated;
    int                 _value;
};

void* wait_for_unregister(void* arg) {
    M2MLWClient *client;
    client = (M2MLWClient*) arg;
    if(client->unregister_successful()) {
        printf("Unregistered done\n");
    }
    return NULL;
}

void* send_observation(void* arg) {
    M2MLWClient *client;
    client = (M2MLWClient*) arg;
    static uint8_t counter = 0;
    while(1) {
        sleep(1);
        if(counter >= 10 &&
           client->register_successful()) {
            printf("Sending observation\n");
            client->update_resource();
            counter = 0;
        }
        else
            counter++;
    }
    return NULL;
}

static M2MLWClient *m2mclient = NULL;

static void ctrl_c_handle_function(void)
{
    if(m2mclient && m2mclient->register_successful()) {
        printf("\nUnregistering endpoint and EXITING Program\n");
        m2mclient->test_unregister();
    exit(1);
    }
}

int main() {

    pthread_t unregister_thread;
    pthread_t observation_thread;
    M2MLWClient lwm2mclient;

    if(M2M_SERVER_ADDRESS.compare(7,17,"<xxx.xxx.xxx.xxx>") == 0) {
        printf("You don't have a valid Device Server Address, remember to update MBED_SERVER_ADDRESS !!!!!\n ");
        printf("EXITING program\n");
        exit(1);
    }

    m2mclient = &lwm2mclient;

    signal(SIGINT, (signalhandler_t)ctrl_c_handle_function);

    bool result = lwm2mclient.create_interface();
    if(true == result) {
        printf("\nInterface created\n");
    }
    result = lwm2mclient.create_register_object();
    if(true == result) {
        printf("register object created");
    }

    result = lwm2mclient.create_device_object();
    if(true == result){
        printf("\nDevice object created !!\n");
    }

    result = lwm2mclient.create_generic_object();

    if(true == result) {
        printf("\nGeneric object created\n");
    }

    printf("Registering endpoint\n");
    lwm2mclient.test_register();

    pthread_create(&observation_thread, NULL, &send_observation, (void*) &lwm2mclient);
    pthread_create(&unregister_thread, NULL, &wait_for_unregister, (void*) &lwm2mclient);

    pthread_join(unregister_thread, NULL);

    pthread_detach(unregister_thread);

    return 0;
}


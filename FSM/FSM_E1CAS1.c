#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include <FSM/FSMDevice/FSME1Cas.h>

struct FSME1CAS fsmcas[FSM_E1CasTreeSize];

void FSMCASHandler(char* Data, short len)
{

// struct FSM_SendAudioData* indat=(struct FSM_SendAudioData*) Data;
// struct FSME1CAS* casdata =(struct FSME1CAS*)FSM_AudioStreamData(indat->IDDevice);

}

int32_t FSMCASRegister(void)
{
    int i;
    struct FSM_AudioStream fsmas;

    for(i = 0; i < FSM_E1CasTreeSize; i++) {
        if(fsmcas[i].reg == 0) {
            fsmcas[i].reg = 1;
            fsmas.ToProcess = FSMCASHandler;
            fsmas.TransportDevice = 0;
            fsmas.TransportDeviceType = FSM_FifoID;
            fsmas.Data = &fsmcas[i];
            fsmcas[i].idstream = FSM_AudioStreamRegistr(fsmas);
            return i;
        }
    }
    return -1;
}

void FSMCASUnRegister(uint16_t idcon)
{
    fsmcas[idcon].reg = 0;
}

uint16_t FSM_CAS_GetStream(uint16_t idcon)
{
    return fsmcas[idcon].idstream;
}

static int __init FSME1CAS1Protocol_init(void)
{

    memset(&fsmcas, 0, sizeof(fsmcas));
    printk(KERN_INFO "FSME1CAS1 module loaded\n");
    return 0;
}

static void __exit FSME1CAS1Protocol_exit(void)
{
    printk(KERN_INFO "FSME1CAS1 module unloaded\n");
}

module_init(FSME1CAS1Protocol_init);
module_exit(FSME1CAS1Protocol_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM E1 Protocol Module");
MODULE_LICENSE("GPL");
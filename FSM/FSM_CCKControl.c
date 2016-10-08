#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct FSM_DeviceFunctionTree FSMCCK_dft;
struct CCKDeviceInfo FSMCCK_CCKDev[FSM_CCKTreeSize];

void FSM_CCKControlDeviceRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    // struct fsm_devices_config* fsmset;
    //   struct FSM_DeviceTree* fsdt;
    //  int i,j;
    // short hlen;
    //  unsigned short tmp;
    struct FSM_SendCmdTS* fscts = (struct FSM_SendCmdTS*)data;
    // printk( KERN_INFO "FSM SIOCTL,%u \n",fscts->opcode );

    switch(data[0]) {
    case RegDevice:
        FSM_Statstic_SetStatus(to_dt, "ok");
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(fscts->cmd) {
        }
        break;
    case PacketFromUserSpace: ///< Отправка команды серверу
        switch(fscts->cmd) {
        case FSM_CCKGetInfo:
            memcpy(fscts->Data, &FSMCCK_CCKDev, sizeof(FSMCCK_CCKDev));
            // printk( KERN_INFO "FSM CCK RC\n" );
            break;
        }
        break;
    }
}

void FSMCCK_AddDeviceInfo(struct CCKDeviceInfo* CCK)
{
    int i;
    for(i = 0; i < FSM_CCKTreeSize; i++) {
        if((FSMCCK_CCKDev[i].id == CCK->id) && (FSMCCK_CCKDev[i].reg == 1)) {
            FSMCCK_CCKDev[i].ip[0] = CCK->ip[0];
            FSMCCK_CCKDev[i].ip[1] = CCK->ip[1];
            FSMCCK_CCKDev[i].ip[2] = CCK->ip[2];
            FSMCCK_CCKDev[i].ip[3] = CCK->ip[3];
            FSMCCK_CCKDev[i].Position = CCK->Position;
            FSMCCK_CCKDev[i].type = CCK->type;
            FSMCCK_CCKDev[i].crc32 = CCK->crc32;
            FSMCCK_CCKDev[i].ramstate = CCK->ramstate;
            FSMCCK_CCKDev[i].dstlen = CCK->dstlen;
            return;
        }
    }
    for(i = 0; i < FSM_CCKTreeSize; i++) {
        if(FSMCCK_CCKDev[i].reg == 0) {
            FSMCCK_CCKDev[i].reg = 1;
            FSMCCK_CCKDev[i].n = i;
            FSMCCK_CCKDev[i].id = CCK->id;
            FSMCCK_CCKDev[i].ip[0] = CCK->ip[0];
            FSMCCK_CCKDev[i].ip[1] = CCK->ip[1];
            FSMCCK_CCKDev[i].ip[2] = CCK->ip[2];
            FSMCCK_CCKDev[i].ip[3] = CCK->ip[3];
            FSMCCK_CCKDev[i].Position = CCK->Position;
            FSMCCK_CCKDev[i].type = CCK->type;
            FSMCCK_CCKDev[i].crc32 = CCK->crc32;
            FSMCCK_CCKDev[i].ramstate = CCK->ramstate;
            FSMCCK_CCKDev[i].dstlen = CCK->dstlen;
            // printk( KERN_INFO "FSM CCK Device Added\n" );
            return;
        }
    }
}
EXPORT_SYMBOL(FSMCCK_AddDeviceInfo);

static int __init FSMCCKControlDevice_init(void)
{
    struct FSM_DeviceRegistr regp;
    FSMCCK_dft.aplayp = 0;
    FSMCCK_dft.type = (unsigned char)AudioDevice;
    FSMCCK_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMCCK_dft.PodVidDevice = (unsigned char)ControlCCK;
    FSMCCK_dft.KodDevice = (unsigned char)ControlCCKServer;
    FSMCCK_dft.Proc = FSM_CCKControlDeviceRecive;
    FSMCCK_dft.config_len = 0;
    FSM_DeviceClassRegister(FSMCCK_dft);

    regp.IDDevice = FSM_CCKControlID;
    regp.VidDevice = FSMCCK_dft.VidDevice;
    regp.PodVidDevice = FSMCCK_dft.PodVidDevice;
    regp.KodDevice = FSMCCK_dft.KodDevice;
    regp.type = FSMCCK_dft.type;
    regp.opcode = RegDevice;
    regp.CRC = 0;
    FSM_DeviceRegister(regp);
    printk(KERN_INFO "FSM CCK ControlDevice loaded\n");
    return 0;
}
static void __exit FSMCCKControlDevice_exit(void)
{
    FSM_ClassDeRegister(FSMCCK_dft);
    printk(KERN_INFO "FSM CCK ControlDevice module unloaded\n");
}

module_init(FSMCCKControlDevice_init);
module_exit(FSMCCKControlDevice_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM CCK ControlDevice Module");
MODULE_LICENSE("GPL");
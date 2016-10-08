/*!
\file
\brief Модуль взаимодествия с пультом ПО-06
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>

#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct CCKDeviceInfo FSMPO06_CCKDevE;
struct FSM_DeviceFunctionTree FSMPO06_dft;
struct FSM_PO06Device FSMPO06Dev[FSM_PO06DeviceTreeSize];
struct FSM_SendCmd FSMPO06_sendcmd;
struct FSM_AudioStream FSMPO06_fsmas;


void FSM_PO06SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;



    memset(&FSMPO06_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSMPO06_sendcmd.opcode = SendCmdToDevice;
    FSMPO06_sendcmd.IDDevice = from_dt->IDDevice;
    FSMPO06_sendcmd.cmd = FSMPO06SendStream;
    FSMPO06_sendcmd.countparam = 1;
    ((unsigned short*)FSMPO06_sendcmd.Data)[0] = id;
    if(to_dt->debug)
        printk(KERN_INFO "FSM Send %u ,%u \n", FSMPO06_sendcmd.Data[0], FSMPO06_sendcmd.Data[1]);
    FSMPO06_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSMPO06_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSMPO06_sendcmd, plen, to_dt, from_dt);
}

void FSM_PO06Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;

    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;
// char datas[2];

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_PO06DeviceTreeSize; i++) {
            if(FSMPO06Dev[i].iddev == to_dt->IDDevice) {
                FSM_PO06SendStreaminfo(FSMPO06Dev[i].idstream, from_dt, to_dt);
                return;
            }
        }
        for(i = 0; i < FSM_PO06DeviceTreeSize; i++) {
            if(FSMPO06Dev[i].reg == 0) {
                FSMPO06Dev[i].reg = 1;
                FSMPO06Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                FSMPO06_fsmas.iddev = to_dt->IDDevice;
                // fsmas.ToProcess=FSM_PO06RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                FSMPO06_fsmas.TransportDevice = FSMPO06Dev[i].ethdev->numdev;
                FSMPO06_fsmas.TransportDeviceType = FSM_EthernetID2;
                FSMPO06_fsmas.Data = &FSMPO06Dev[i];
                FSMPO06Dev[i].idstream = FSM_AudioStreamRegistr(FSMPO06_fsmas);
                FSMPO06Dev[i].iddev = to_dt->IDDevice;
                to_dt->data = &FSMPO06Dev[i];
                to_dt->config = &FSMPO06Dev[i].po06set;
                FSM_PO06SendStreaminfo(FSMPO06Dev[i].idstream, from_dt, to_dt);
                printk(KERN_INFO "FSMPO06 Device Added %u \n", to_dt->IDDevice);

                FSM_P2P_Connect(FSMPO06Dev[i].idstream, 2);

                // datas[0]=0xd0;
                // datas[1]=0xd1;
                // FSM_AudioStreamToUser(0,datas,2);
                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_PO06DeviceTreeSize; i++) {
            if((FSMPO06Dev[i].reg == 1) && (FSMPO06Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSMPO06Dev[i].idstream);
                FSMPO06Dev[i].reg = 0;
                if(to_dt->debug)
                    printk(KERN_INFO "FSMPO06 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSMPO06ConnectToDevE1:
            // ((struct FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idcon=FSM_P2P_Connect(((struct
            // FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idstream, ((struct
            // FSM_E1Device*)(FSM_FindDevice(((struct FSMPO06CommCons*)scmd->Data)->id)->data))->streams_id[((struct
            // FSMPO06CommCons*)scmd->Data)->channel]);
            break;
        case FSMPO06DisConnectToDevE1:
            FSM_P2P_Disconnect(((struct FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idcon);
            break;
        case AnsGetSettingClientPo06:
            if(to_dt->debug)
                printk(KERN_INFO "FSM_Set Recv %i\n", scmd->IDDevice);
            memcpy(&((struct fsm_po06_setting*)(FSM_FindDevice(scmd->IDDevice)->config))->fsm_p006_su_s,
                   scmd->Data,
                   FSM_FindDevice(scmd->IDDevice)->dt->config_len);
            break;
        case FSMPo06SendIP:
            FSMPO06_CCKDevE.id = scmd->IDDevice;
            FSMPO06_CCKDevE.ip[0] = scmd->Data[0];
            FSMPO06_CCKDevE.ip[1] = scmd->Data[1];
            FSMPO06_CCKDevE.ip[2] = scmd->Data[2];
            FSMPO06_CCKDevE.ip[3] = scmd->Data[3];
            FSMPO06_CCKDevE.type = PO06;
            FSMPO06_CCKDevE.Position = scmd->Data[4];
            FSMCCK_AddDeviceInfo(&FSMPO06_CCKDevE);
            if(to_dt->debug)
                printk(KERN_INFO "FSM PO06 ID%i Asterisk IP %i.%i.%i.%i\n ",
                       scmd->IDDevice,
                       scmd->Data[0],
                       scmd->Data[1],
                       scmd->Data[2],
                       scmd->Data[3]);
            break;
        }

        break;
    case SendTxtMassage: ///< Отправка текстового сообщения
        break;
    case Alern: ///<Тревога
        break;
    case Warning: ///<Предупреждение
        break;
    case Trouble: ///<Сбой
        break;
    case Beep: ///<Звук
        break;
    default:
        break;
    }

    if(to_dt->debug)
        printk(KERN_INFO "RPack %u \n", len);
}
EXPORT_SYMBOL(FSM_PO06Recive);

void ApplaySettingPO06(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{


    memset(&FSMPO06_sendcmd, 0, sizeof(FSMPO06_sendcmd));
    if(to_dt->debug)
        printk(KERN_INFO "FSM_Set\n");
    FSMPO06_sendcmd.cmd = SetSettingClientPo06;
    FSMPO06_sendcmd.countparam = 1;
    FSMPO06_sendcmd.IDDevice = to_dt->IDDevice;
    FSMPO06_sendcmd.CRC = 0;
    FSMPO06_sendcmd.opcode = SendCmdToDevice;
    memcpy(&FSMPO06_sendcmd.Data,
           &(((struct FSM_PO06Device*)to_dt->data)->po06set.fsm_p006_su_s),
           sizeof(struct fsm_po06_subscriber));
    from_dt->dt->Proc((char*)&FSMPO06_sendcmd,
                      sizeof(struct FSM_SendCmd) - sizeof(FSMPO06_sendcmd.Data) + sizeof(struct fsm_po06_subscriber),
                      from_dt,
                      to_dt);
}

static int __init FSM_PO06_init(void)
{
    FSMPO06_dft.aplayp = ApplaySettingPO06;
    FSMPO06_dft.type = (unsigned char)AudioDevice;
    FSMPO06_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMPO06_dft.PodVidDevice = (unsigned char)CCK;
    FSMPO06_dft.KodDevice = (unsigned char)PO06;
    FSMPO06_dft.Proc = FSM_PO06Recive;
    FSMPO06_dft.config_len = sizeof(struct fsm_po06_setting);
    FSM_DeviceClassRegister(FSMPO06_dft);
    printk(KERN_INFO "FSM PO06 Module loaded\n");
    return 0;
}

static void __exit FSM_PO06_exit(void)
{

    FSM_ClassDeRegister(FSMPO06_dft);
    printk(KERN_INFO "FSM PO06 Module unloaded\n");
}

module_init(FSM_PO06_init);
module_exit(FSM_PO06_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM PO06 Module");
MODULE_LICENSE("GPL");
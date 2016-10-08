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

struct CCKDeviceInfo FSMPO08_CCKDevE;
struct FSM_DeviceFunctionTree FSMPO08_dft;
struct FSM_PO08Device FSMPO08Dev[FSM_PO08DeviceTreeSize];
struct FSM_SendCmd FSMPO08_sendcmd;
struct FSM_AudioStream FSMPO08_fsmas;

void FSM_PO08SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;

    memset(&FSMPO08_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSMPO08_sendcmd.opcode = SendCmdToDevice;
    FSMPO08_sendcmd.IDDevice = from_dt->IDDevice;
    FSMPO08_sendcmd.cmd = FSMPO08SendStream;
    FSMPO08_sendcmd.countparam = 1;
    ((unsigned short*)FSMPO08_sendcmd.Data)[0] = id;
    if(to_dt->debug)
        printk(KERN_INFO "FSM Send %u ,%u \n", FSMPO08_sendcmd.Data[0], FSMPO08_sendcmd.Data[1]);
    FSMPO08_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSMPO08_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSMPO08_sendcmd, plen, to_dt, from_dt);

}

void FSM_PO08Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;

    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;
// char datas[2];

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_PO08DeviceTreeSize; i++) {
            if(FSMPO08Dev[i].iddev == to_dt->IDDevice) {
                FSM_PO08SendStreaminfo(FSMPO08Dev[i].idstream, from_dt, to_dt);
                return;
            }
        }
        for(i = 0; i < FSM_PO08DeviceTreeSize; i++) {
            if(FSMPO08Dev[i].reg == 0) {
                FSMPO08Dev[i].reg = 1;
                FSMPO08Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                FSMPO08_fsmas.iddev = to_dt->IDDevice;
                // fsmas.ToProcess=FSM_PO06RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                FSMPO08_fsmas.TransportDevice = FSMPO08Dev[i].ethdev->numdev;
                FSMPO08_fsmas.TransportDeviceType = FSM_EthernetID2;
                FSMPO08_fsmas.Data = &FSMPO08Dev[i];
                FSMPO08Dev[i].idstream = FSM_AudioStreamRegistr(FSMPO08_fsmas);
                FSMPO08Dev[i].iddev = to_dt->IDDevice;
                to_dt->data = &FSMPO08Dev[i];
                to_dt->config = &FSMPO08Dev[i].po08set;
                FSM_PO08SendStreaminfo(FSMPO08Dev[i].idstream, from_dt, to_dt);
                if(to_dt->debug)
                    printk(KERN_INFO "FSM PO08 Device Added %u \n", to_dt->IDDevice);
                ;

                // datas[0]=0xd0;
                // datas[1]=0xd1;
                // FSM_AudioStreamToUser(0,datas,2);
                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_PO08DeviceTreeSize; i++) {
            if((FSMPO08Dev[i].reg == 1) && (FSMPO08Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSMPO08Dev[i].idstream);
                FSMPO08Dev[i].reg = 0;
                if(to_dt->debug)
                    printk(KERN_INFO "FSM PO08 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSMPO08ConnectToDevE1:
            // ((struct FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idcon=FSM_P2P_Connect(((struct
            // FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idstream, ((struct
            // FSM_E1Device*)(FSM_FindDevice(((struct FSMPO06CommCons*)scmd->Data)->id)->data))->streams_id[((struct
            // FSMPO06CommCons*)scmd->Data)->channel]);
            break;
        case FSMPO08DisConnectToDevE1:
            FSM_P2P_Disconnect(((struct FSM_PO06Device*)(to_dt->data))->idcon);
            break;
        case AnsGetSettingClientPO08:
            if(to_dt->debug)
                printk(KERN_INFO "FSM_Set Recv %i\n", scmd->IDDevice);
            memcpy(&((struct fsm_po08_setting*)(to_dt->config))->fsm_p008_su_s, scmd->Data, to_dt->dt->config_len);
            break;
        case FSMPo08SendIP:
            FSMPO08_CCKDevE.id = scmd->IDDevice;
            FSMPO08_CCKDevE.ip[0] = scmd->Data[0];
            FSMPO08_CCKDevE.ip[1] = scmd->Data[1];
            FSMPO08_CCKDevE.ip[2] = scmd->Data[2];
            FSMPO08_CCKDevE.ip[3] = scmd->Data[3];
            FSMPO08_CCKDevE.type = PO08;
            FSMPO08_CCKDevE.Position = scmd->Data[4];
            FSMCCK_AddDeviceInfo(&FSMPO08_CCKDevE);
            if(to_dt->debug)
                printk(KERN_INFO "FSM PO08 ID%i Asterisk IP %i.%i.%i.%i\n ",
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
    case PacketFromUserSpace:
        switch(scmd->cmd) {
        case FSMPo08AudioRun:
        case FSMPo08Reset:
        case FSMPo08Reregister:
            scmd->IDDevice = to_dt->IDDevice;
            scmd->opcode = SendCmdToDevice;
            to_dt->TrDev->dt->Proc((char*)scmd, FSMH_Header_Size_SendCmd, to_dt->TrDev, to_dt);
            break;
        case FSMPo08GetCRC:

            break;
        }
        break;
    case Alern: ///<Тревога
        switch(((struct FSM_AlernSignal*)data)->ID) {
        }
        break;
    case Warning: ///<Предупреждение
        switch(((struct FSM_WarningSignal*)data)->ID) {
        case FSM_CCK_Server_Connect_Error:
            printk(KERN_WARNING "PO08 %u: Server Not Connect \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Trouble: ///<Сбой
        switch(((struct FSM_TroubleSignal*)data)->ID) {
        case FSM_CCK_Memory_Test_Filed:
            printk(KERN_ERR "PO08 %u: Memory Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Beep: ///<Звук
        break;
    default:
        break;
    }


    if(to_dt->debug)
        printk(KERN_INFO "RPack %u \n", len);
}
EXPORT_SYMBOL(FSM_PO08Recive);
void ApplaySettingPO08(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{

    memset(&FSMPO08_sendcmd, 0, sizeof(FSMPO08_sendcmd));
    if(to_dt->debug)
        printk(KERN_INFO "FSM_Set\n");
    FSMPO08_sendcmd.cmd = SetSettingClientPO08;
    FSMPO08_sendcmd.countparam = 1;
    FSMPO08_sendcmd.IDDevice = to_dt->IDDevice;
    FSMPO08_sendcmd.CRC = 0;
    FSMPO08_sendcmd.opcode = SendCmdToDevice;
    memcpy(&FSMPO08_sendcmd.Data,
           &(((struct FSM_PO08Device*)to_dt->data)->po08set.fsm_p008_su_s),
           sizeof(struct fsm_po08_subscriber));
    from_dt->dt->Proc((char*)&FSMPO08_sendcmd,
                      sizeof(struct FSM_SendCmd) - sizeof(FSMPO08_sendcmd.Data) + sizeof(struct fsm_po08_subscriber),
                      from_dt,
                      to_dt);
}

static int __init FSM_PO08_init(void)
{

    FSMPO08_dft.aplayp = ApplaySettingPO08;
    FSMPO08_dft.type = (unsigned char)AudioDevice;
    FSMPO08_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMPO08_dft.PodVidDevice = (unsigned char)CCK;
    FSMPO08_dft.KodDevice = (unsigned char)PO08;
    FSMPO08_dft.Proc = FSM_PO08Recive;
    FSMPO08_dft.config_len = sizeof(struct fsm_po08_setting);
    FSM_DeviceClassRegister(FSMPO08_dft);
    printk(KERN_INFO "FSM PO08 Module loaded\n");
    FSM_SendEventToAllDev(FSM_CCK_MN845_Started);

    return 0;
}

static void __exit FSM_PO08_exit(void)
{

    FSM_ClassDeRegister(FSMPO08_dft);
    printk(KERN_INFO "FSM PO08 Module unloaded\n");
}

module_init(FSM_PO08_init);
module_exit(FSM_PO08_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM PO08 Module");
MODULE_LICENSE("GPL");
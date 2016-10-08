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

struct CCKDeviceInfo FSMPO07_CCKDevE;
struct FSM_DeviceFunctionTree FSMPO07_dft;
struct FSM_PO07Device FSMPO07Dev[FSM_PO07DeviceTreeSize];
struct FSM_SendCmd FSMPO07_sendcmd;
struct FSM_AudioStream FSMPO07_fsmas;

void FSM_PO07SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;

    memset(&FSMPO07_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSMPO07_sendcmd.opcode = SendCmdToDevice;
    FSMPO07_sendcmd.IDDevice = from_dt->IDDevice;
    FSMPO07_sendcmd.cmd = FSMPO07SendStream;
    FSMPO07_sendcmd.countparam = 1;
    ((unsigned short*)FSMPO07_sendcmd.Data)[0] = id;
    if(to_dt->debug)
        printk(KERN_INFO "FSM Send %u ,%u \n", FSMPO07_sendcmd.Data[0], FSMPO07_sendcmd.Data[1]);
    FSMPO07_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSMPO07_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSMPO07_sendcmd, plen, to_dt, from_dt);
}

void FSM_PO07Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;

    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;
// struct FSM_SendMessage* sctt=(struct FSM_SendMessage*)data;
// char datas[2];

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_PO07DeviceTreeSize; i++) {
            if(FSMPO07Dev[i].iddev == to_dt->IDDevice) {
                FSM_PO07SendStreaminfo(FSMPO07Dev[i].idstream, from_dt, to_dt);
                return;
            }
        }
        for(i = 0; i < FSM_PO07DeviceTreeSize; i++) {
            if(FSMPO07Dev[i].reg == 0) {
                FSMPO07Dev[i].reg = 1;
                FSMPO07Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                FSMPO07_fsmas.iddev = to_dt->IDDevice;
                // fsmas.ToProcess=FSM_PO06RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                FSMPO07_fsmas.TransportDevice = FSMPO07Dev[i].ethdev->numdev;
                FSMPO07_fsmas.TransportDeviceType = FSM_EthernetID2;
                FSMPO07_fsmas.Data = &FSMPO07Dev[i];
                FSMPO07Dev[i].idstream = FSM_AudioStreamRegistr(FSMPO07_fsmas);
                FSMPO07Dev[i].iddev = to_dt->IDDevice;
                to_dt->data = &FSMPO07Dev[i];
                to_dt->config = &FSMPO07Dev[i].po07set;
                FSM_PO07SendStreaminfo(FSMPO07Dev[i].idstream, from_dt, to_dt);
                if(to_dt->debug)
                    printk(KERN_INFO "FSM PO07 Device Added %u \n", to_dt->IDDevice);
                ;

                // datas[0]=0xd0;
                // datas[1]=0xd1;
                // FSM_AudioStreamToUser(0,datas,2);
                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_PO07DeviceTreeSize; i++) {
            if((FSMPO07Dev[i].reg == 1) && (FSMPO07Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSMPO07Dev[i].idstream);
                FSMPO07Dev[i].reg = 0;
                if(to_dt->debug)
                    printk(KERN_INFO "FSM PO07 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSMPO07ConnectToDevE1:
            // ((struct FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idcon=FSM_P2P_Connect(((struct
            // FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idstream, ((struct
            // FSM_E1Device*)(FSM_FindDevice(((struct FSMPO06CommCons*)scmd->Data)->id)->data))->streams_id[((struct
            // FSMPO06CommCons*)scmd->Data)->channel]);
            break;
        case FSMPO07DisConnectToDevE1:
            FSM_P2P_Disconnect(((struct FSM_PO06Device*)(to_dt->data))->idcon);
            break;
        case AnsGetSettingClientPO07:
            if(to_dt->debug)
                printk(KERN_INFO "FSM_Set Recv %i\n", scmd->IDDevice);
            memcpy(&((struct fsm_po07_setting*)(to_dt->config))->fsm_p007_su_s, scmd->Data, to_dt->dt->config_len);
            break;
        case FSMPo07SendIP:
            FSMPO07_CCKDevE.id = scmd->IDDevice;
            FSMPO07_CCKDevE.ip[0] = scmd->Data[0];
            FSMPO07_CCKDevE.ip[1] = scmd->Data[1];
            FSMPO07_CCKDevE.ip[2] = scmd->Data[2];
            FSMPO07_CCKDevE.ip[3] = scmd->Data[3];
            FSMPO07_CCKDevE.type = PO07;
            FSMPO07_CCKDevE.Position = scmd->Data[4];
            FSMCCK_AddDeviceInfo(&FSMPO07_CCKDevE);
            if(to_dt->debug)
                printk(KERN_INFO "FSM PO07 ID%i Asterisk IP %i.%i.%i.%i\n ",
                       scmd->IDDevice,
                       scmd->Data[0],
                       scmd->Data[1],
                       scmd->Data[2],
                       scmd->Data[3]);
            break;
        }

        break;
    case SendTxtMassage: ///< Отправка текстового сообщения
        // printk( KERN_INFO "%s",sctt->Data );

        break;
    case PacketFromUserSpace:
        switch(scmd->cmd) {
        case FSMPo07AudioRun:
        case FSMPo07Reset:
        case FSMPo07Reregister:
            scmd->IDDevice = to_dt->IDDevice;
            scmd->opcode = SendCmdToDevice;
            to_dt->TrDev->dt->Proc((char*)scmd, FSMH_Header_Size_SendCmd, to_dt->TrDev, to_dt);
            break;
        case FSMPo07GetCRC:

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
            printk(KERN_WARNING "PO07 %u: Server Not Connect \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Trouble: ///<Сбой
        switch(((struct FSM_TroubleSignal*)data)->ID) {
        case FSM_CCK_Memory_Test_Filed:
            printk(KERN_ERR "PO07 %u: Memory Error \n", ((struct FSM_Header*)(data))->IDDevice);
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
EXPORT_SYMBOL(FSM_PO07Recive);
void ApplaySettingPO07(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{


    memset(&FSMPO07_sendcmd, 0, sizeof(FSMPO07_sendcmd));
    printk(KERN_INFO "FSM_Set\n");
    FSMPO07_sendcmd.cmd = SetSettingClientPO07;
    FSMPO07_sendcmd.countparam = 1;
    FSMPO07_sendcmd.IDDevice = to_dt->IDDevice;
    FSMPO07_sendcmd.CRC = 0;
    FSMPO07_sendcmd.opcode = SendCmdToDevice;
    memcpy(&FSMPO07_sendcmd.Data,
           &(((struct FSM_PO07Device*)to_dt->data)->po07set.fsm_p007_su_s),
           sizeof(struct fsm_po07_subscriber));
    from_dt->dt->Proc((char*)&FSMPO07_sendcmd,
                      sizeof(struct FSM_SendCmd) - sizeof(FSMPO07_sendcmd.Data) + sizeof(struct fsm_po07_subscriber),
                      from_dt,
                      to_dt);

}

static int __init FSM_PO07_init(void)
{

    FSMPO07_dft.aplayp = ApplaySettingPO07;
    FSMPO07_dft.type = (unsigned char)AudioDevice;
    FSMPO07_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMPO07_dft.PodVidDevice = (unsigned char)CCK;
    FSMPO07_dft.KodDevice = (unsigned char)PO07;
    FSMPO07_dft.Proc = FSM_PO07Recive;
    FSMPO07_dft.config_len = sizeof(struct fsm_po07_setting);
    FSM_DeviceClassRegister(FSMPO07_dft);
    printk(KERN_INFO "FSM PO07 Module loaded\n");
    FSM_SendEventToAllDev(FSM_CCK_MN845_Started);
    return 0;
}

static void __exit FSM_PO07_exit(void)
{
    FSM_ClassDeRegister(FSMPO07_dft);
    printk(KERN_INFO "FSM PO07 Module unloaded\n");
}

module_init(FSM_PO07_init);
module_exit(FSM_PO07_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM PO07 Module");
MODULE_LICENSE("GPL");
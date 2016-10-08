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

struct CCKDeviceInfo FSMMN825_CCKDevE;
struct FSM_DeviceFunctionTree FSMMN825_dft;
struct FSM_MN825Device FSMMN825Dev[FSM_MN825DeviceTreeSize];
struct FSM_SendCmd FSMMN825_sendcmd;
struct FSM_AudioStream FSMMN825_fsmas;

void FSM_MN825SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;


    memset(&FSMMN825_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSMMN825_sendcmd.opcode = SendCmdToDevice;
    FSMMN825_sendcmd.IDDevice = from_dt->IDDevice;
    FSMMN825_sendcmd.cmd = FSMPO06SendStream;
    FSMMN825_sendcmd.countparam = 1;
    ((unsigned short*)FSMMN825_sendcmd.Data)[0] = id;
    if(to_dt->debug)
        printk(KERN_INFO "FSM Send %u ,%u \n", FSMMN825_sendcmd.Data[0], FSMMN825_sendcmd.Data[1]);
    FSMMN825_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSMMN825_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSMMN825_sendcmd, plen, to_dt, from_dt);
}

void FSM_MN825Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;

    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;
// char datas[2];

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_MN825DeviceTreeSize; i++) {
            if(FSMMN825Dev[i].iddev == to_dt->IDDevice) {
                FSM_MN825SendStreaminfo(FSMMN825Dev[i].idstream, from_dt, to_dt);
                return;
            }
        }
        for(i = 0; i < FSM_MN825DeviceTreeSize; i++) {
            if(FSMMN825Dev[i].reg == 0) {
                FSMMN825Dev[i].reg = 1;
                FSMMN825Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                FSMMN825_fsmas.iddev = to_dt->IDDevice;
                // fsmas.ToProcess=FSM_PO06RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                FSMMN825_fsmas.TransportDevice = FSMMN825Dev[i].ethdev->numdev;
                FSMMN825_fsmas.TransportDeviceType = FSM_EthernetID2;
                FSMMN825_fsmas.Data = &FSMMN825Dev[i];
                FSMMN825Dev[i].idstream = FSM_AudioStreamRegistr(FSMMN825_fsmas);
                FSMMN825Dev[i].iddev = to_dt->IDDevice;
                to_dt->data = &FSMMN825Dev[i];
                to_dt->config = &FSMMN825Dev[i].mn825set;
                FSM_MN825SendStreaminfo(FSMMN825Dev[i].idstream, from_dt, to_dt);
                if(to_dt->debug)
                    printk(KERN_INFO "FSM MN825 Device Added %u \n", to_dt->IDDevice);
                ;

                // datas[0]=0xd0;
                // datas[1]=0xd1;
                // FSM_AudioStreamToUser(0,datas,2);
                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_MN825DeviceTreeSize; i++) {
            if((FSMMN825Dev[i].reg == 1) && (FSMMN825Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSMMN825Dev[i].idstream);
                FSMMN825Dev[i].reg = 0;
                if(to_dt->debug)
                    printk(KERN_INFO "FSM MN825 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSMMN825ConnectToDevE1:
            // ((struct FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idcon=FSM_P2P_Connect(((struct
            // FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idstream, ((struct
            // FSM_E1Device*)(FSM_FindDevice(((struct FSMPO06CommCons*)scmd->Data)->id)->data))->streams_id[((struct
            // FSMPO06CommCons*)scmd->Data)->channel]);
            break;
        case FSMMN825DisConnectToDevE1:
            FSM_P2P_Disconnect(((struct FSM_PO06Device*)(to_dt->data))->idcon);
            break;
        case AnsGetSettingClientMN825:
            if(to_dt->debug)
                printk(KERN_INFO "FSM_Set Recv %i\n", scmd->IDDevice);
            memcpy(&((struct fsm_po06_setting*)(to_dt->config))->fsm_p006_su_s, scmd->Data, to_dt->dt->config_len);
            break;
        case FSMMN825SendIP:
            FSMMN825_CCKDevE.id = scmd->IDDevice;
            FSMMN825_CCKDevE.ip[0] = scmd->Data[0];
            FSMMN825_CCKDevE.ip[1] = scmd->Data[1];
            FSMMN825_CCKDevE.ip[2] = scmd->Data[2];
            FSMMN825_CCKDevE.ip[3] = scmd->Data[3];
            FSMMN825_CCKDevE.type = MN825;
            FSMMN825_CCKDevE.Position = scmd->Data[4];
            FSMCCK_AddDeviceInfo(&FSMMN825_CCKDevE);
            if(to_dt->debug)
                printk(KERN_INFO "FSM ID%i Asterisk IP %i.%i.%i.%i\n ",
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
        switch(((struct FSM_AlernSignal*)data)->ID) {
        }
        break;
    case Warning: ///<Предупреждение
        switch(((struct FSM_WarningSignal*)data)->ID) {
        case FSM_CCK_Server_Connect_Error:
            printk(KERN_WARNING "MN825 %u: Server Not Connect \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Trouble: ///<Сбой
        switch(((struct FSM_TroubleSignal*)data)->ID) {
        case FSM_CCK_Memory_Test_Filed:
            printk(KERN_ERR "MN825 %u: Memory Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Beep: ///<Звук
        break;
    case PacketFromUserSpace:
        switch(scmd->cmd) {
        case FSMMN825AudioRun:
        case FSMMN825Reset:
        case FSMMN825Reregister:
        case FSMMN825SetTangenta:
            scmd->IDDevice = to_dt->IDDevice;
            scmd->opcode = SendCmdToDevice;
            to_dt->TrDev->dt->Proc((char*)scmd, FSMH_Header_Size_SendCmd, to_dt->TrDev, to_dt);
            break;
        case FSMMN825GetCRC:

            break;
        }
        break;
    default:
        break;
    }

    if(to_dt->debug)
        printk(KERN_INFO "RPack %u \n", len);
}
EXPORT_SYMBOL(FSM_MN825Recive);
void ApplaySettingMN825(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{

    memset(&FSMMN825_sendcmd, 0, sizeof(FSMMN825_sendcmd));
    if(to_dt->debug)
        printk(KERN_INFO "FSM_Set\n");
    FSMMN825_sendcmd.cmd = SetSettingClientMN825;
    FSMMN825_sendcmd.countparam = 1;
    FSMMN825_sendcmd.IDDevice = to_dt->IDDevice;
    FSMMN825_sendcmd.CRC = 0;
    FSMMN825_sendcmd.opcode = SendCmdToDevice;
    memcpy(&FSMMN825_sendcmd.Data,
           &(((struct FSM_MN825Device*)to_dt->data)->mn825set.fsm_mn825_su_s),
           sizeof(struct fsm_mn825_subscriber));
    from_dt->dt->Proc((char*)&FSMMN825_sendcmd,
                      sizeof(struct FSM_SendCmd) - sizeof(FSMMN825_sendcmd.Data) + sizeof(struct fsm_mn825_subscriber),
                      from_dt,
                      to_dt);
}

static int __init FSM_MN825_init(void)
{


    FSMMN825_dft.aplayp = ApplaySettingMN825;
    FSMMN825_dft.type = (unsigned char)AudioDevice;
    FSMMN825_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMMN825_dft.PodVidDevice = (unsigned char)CCK;
    FSMMN825_dft.KodDevice = (unsigned char)MN825;
    FSMMN825_dft.Proc = FSM_MN825Recive;
    FSMMN825_dft.config_len = sizeof(struct fsm_mn825_setting);
    FSM_DeviceClassRegister(FSMMN825_dft);
    printk(KERN_INFO "FSM MN825 Module loaded\n");
    FSM_SendEventToAllDev(FSM_CCK_MN845_Started);

    return 0;
}

static void __exit FSM_MN825_exit(void)
{
    FSM_ClassDeRegister(FSMMN825_dft);
    printk(KERN_INFO "FSM MN825 Module unloaded\n");
}

module_init(FSM_MN825_init);
module_exit(FSM_MN825_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM MN825 Module");
MODULE_LICENSE("GPL");
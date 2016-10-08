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

struct CCKDeviceInfo FSMMN921_CCKDevE;
struct FSM_DeviceFunctionTree FSMMN921_dft;
struct FSM_MN921Device FSMMN921Dev[FSM_MN921DeviceTreeSize];
struct FSM_SendCmd FSMMN921_sendcmd;
struct FSM_AudioStream FSMMN921_fsmas;


void FSM_MN921SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;

    memset(&FSMMN921_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSMMN921_sendcmd.opcode = SendCmdToDevice;
    FSMMN921_sendcmd.IDDevice = from_dt->IDDevice;
    FSMMN921_sendcmd.cmd = FSMPO06SendStream;
    FSMMN921_sendcmd.countparam = 1;
    ((unsigned short*)FSMMN921_sendcmd.Data)[0] = id;
    if(to_dt->debug)
        printk(KERN_INFO "FSM Send %u ,%u \n", FSMMN921_sendcmd.Data[0], FSMMN921_sendcmd.Data[1]);
    FSMMN921_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSMMN921_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSMMN921_sendcmd, plen, to_dt, from_dt);
}

void FSM_MN921Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;
    unsigned char fsm_mn_crc32[4];
    unsigned char fsm_mn_ramst[2];
    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;
// char datas[2];

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        FSM_SetTreeAdd(to_dt);
        for(i = 0; i < FSM_MN921DeviceTreeSize; i++) {
            if(FSMMN921Dev[i].iddev == to_dt->IDDevice) {
                FSM_MN921SendStreaminfo(FSMMN921Dev[i].idstream, from_dt, to_dt);
                return;
            }
        }
        for(i = 0; i < FSM_MN921DeviceTreeSize; i++) {
            if(FSMMN921Dev[i].reg == 0) {
                FSMMN921Dev[i].reg = 1;
                FSMMN921Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                FSMMN921_fsmas.iddev = to_dt->IDDevice;
                // fsmas.ToProcess=FSM_PO06RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                FSMMN921_fsmas.TransportDevice = FSMMN921Dev[i].ethdev->numdev;
                FSMMN921_fsmas.TransportDeviceType = FSM_EthernetID2;
                FSMMN921_fsmas.Data = &FSMMN921Dev[i];
                FSMMN921Dev[i].idstream = FSM_AudioStreamRegistr(FSMMN921_fsmas);
                FSMMN921Dev[i].iddev = to_dt->IDDevice;
                to_dt->data = &FSMMN921Dev[i];
                to_dt->config = &FSMMN921Dev[i].mn921set;
                FSM_MN921SendStreaminfo(FSMMN921Dev[i].idstream, from_dt, to_dt);
                if(to_dt->debug)
                    printk(KERN_INFO "FSM MN921 Device Added %u \n", to_dt->IDDevice);
                ;

                // datas[0]=0xd0;
                // datas[1]=0xd1;
                // FSM_AudioStreamToUser(0,datas,2);
                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_MN921DeviceTreeSize; i++) {
            if((FSMMN921Dev[i].reg == 1) && (FSMMN921Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSMMN921Dev[i].idstream);
                FSMMN921Dev[i].reg = 0;
                if(to_dt->debug)
                    printk(KERN_INFO "FSM MN921 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSMMN921ConnectToDevE1:
            // ((struct FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idcon=FSM_P2P_Connect(((struct
            // FSM_PO06Device*)((FSM_FindDevice(scmd->IDDevice))->data))->idstream, ((struct
            // FSM_E1Device*)(FSM_FindDevice(((struct FSMPO06CommCons*)scmd->Data)->id)->data))->streams_id[((struct
            // FSMPO06CommCons*)scmd->Data)->channel]);
            break;
        case FSMMN921DisConnectToDevE1:
            FSM_P2P_Disconnect(((struct FSM_PO06Device*)(to_dt->data))->idcon);
            break;
        case AnsGetSettingClientMN921:
            if(to_dt->debug)
                printk(KERN_INFO "FSM_Set Recv %i\n", scmd->IDDevice);
            memcpy(&((struct fsm_po06_setting*)(to_dt->config))->fsm_p006_su_s, scmd->Data, to_dt->dt->config_len);
            break;
        case FSMMN921SendIP:
            FSMMN921_CCKDevE.id = scmd->IDDevice;
            FSMMN921_CCKDevE.ip[0] = scmd->Data[0];
            FSMMN921_CCKDevE.ip[1] = scmd->Data[1];
            FSMMN921_CCKDevE.ip[2] = scmd->Data[2];
            FSMMN921_CCKDevE.ip[3] = scmd->Data[3];
            FSMMN921_CCKDevE.type = MN921;
            FSMMN921_CCKDevE.Position = scmd->Data[4];
            fsm_mn_crc32[0] = scmd->Data[5];
            fsm_mn_crc32[1] = scmd->Data[6];
            fsm_mn_crc32[2] = scmd->Data[7];
            fsm_mn_crc32[3] = scmd->Data[8];
            FSMMN921_CCKDevE.crc32=((unsigned int*)fsm_mn_crc32)[0];
            fsm_mn_ramst[0] = scmd->Data[9];
            fsm_mn_ramst[1] = scmd->Data[10];
            FSMMN921_CCKDevE.dstlen=((unsigned short*)fsm_mn_ramst)[0];
            FSMMN921_CCKDevE.dstlen=scmd->Data[11];
            FSMCCK_AddDeviceInfo(&FSMMN921_CCKDevE);
            if(to_dt->debug)
                printk(KERN_INFO "FSM MN921 ID%i Asterisk IP %i.%i.%i.%i\n ",
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
        case FSMMN921AudioRun:
        case FSMMN921Reset:
        case FSMMN921Reregister:
            scmd->IDDevice = to_dt->IDDevice;
            scmd->opcode = SendCmdToDevice;
            to_dt->TrDev->dt->Proc((char*)scmd, FSMH_Header_Size_SendCmd, to_dt->TrDev, to_dt);
            break;
        case FSMMN921GetCRC:

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
            printk(KERN_WARNING "MN921 %u: Server Not Connect \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Trouble: ///<Сбой
        switch(((struct FSM_TroubleSignal*)data)->ID) {
        case FSM_CCK_Memory_Test_Filed:
            printk(KERN_ERR "MN921 %u: Memory Error \n", ((struct FSM_Header*)(data))->IDDevice);
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
EXPORT_SYMBOL(FSM_MN921Recive);
void ApplaySettingMN921(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    memset(&FSMMN921_sendcmd, 0, sizeof(FSMMN921_sendcmd));
    if(to_dt->debug)
        printk(KERN_INFO "FSM_Set\n");
    FSMMN921_sendcmd.cmd = SetSettingClientMN921;
    FSMMN921_sendcmd.countparam = 1;
    FSMMN921_sendcmd.IDDevice = to_dt->IDDevice;
    FSMMN921_sendcmd.CRC = 0;
    FSMMN921_sendcmd.opcode = SendCmdToDevice;
    memcpy(&FSMMN921_sendcmd.Data,
           &(((struct FSM_MN921Device*)to_dt->data)->mn921set.fsm_mn921_su_s),
           sizeof(struct fsm_mn921_subscriber));
    from_dt->dt->Proc((char*)&FSMMN921_sendcmd,
                      sizeof(struct FSM_SendCmd) - sizeof(FSMMN921_sendcmd.Data) + sizeof(struct fsm_mn921_subscriber),
                      from_dt,
                      to_dt);

}

static int __init FSM_MN921_init(void)
{

    FSMMN921_dft.aplayp = ApplaySettingMN921;
    FSMMN921_dft.type = (unsigned char)AudioDevice;
    FSMMN921_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMMN921_dft.PodVidDevice = (unsigned char)CCK;
    FSMMN921_dft.KodDevice = (unsigned char)MN921;
    FSMMN921_dft.Proc = FSM_MN921Recive;
    FSMMN921_dft.config_len = sizeof(struct fsm_mn921_setting);
    FSM_DeviceClassRegister(FSMMN921_dft);
    printk(KERN_INFO "FSM MN921 Module loaded\n");
    FSM_SendEventToAllDev(FSM_CCK_MN921_Started);
    
    return 0;
}

static void __exit FSM_MN921_exit(void)
{

    FSM_ClassDeRegister(FSMMN921_dft);
    printk(KERN_INFO "FSM MN921 Module unloaded\n");
}

module_init(FSM_MN921_init);
module_exit(FSM_MN921_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM MN921 Module");
MODULE_LICENSE("GPL");
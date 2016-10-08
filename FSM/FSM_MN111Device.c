/*!
\file
\brief Модуль взаимодествия с пультом MN111
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct FSM_DeviceFunctionTree FSMMN111_dft;
struct FSM_MN111Device FSMMN111Dev[FSM_MN111DeviceTreeSize];
struct FSM_SendCmd FSMMN111_sendcmd;
struct FSM_AudioStream FSMMN111_fsmas;

static struct timer_list FSM_MN111_timer;

void FSM_MN111SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;

    memset(&FSMMN111_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSMMN111_sendcmd.opcode = SendCmdToDevice;
    FSMMN111_sendcmd.IDDevice = from_dt->IDDevice;
    FSMMN111_sendcmd.cmd = FSMPO06SendStream;
    FSMMN111_sendcmd.countparam = 1;
    ((unsigned short*)FSMMN111_sendcmd.Data)[0] = id;
    if(to_dt->debug)
        printk(KERN_INFO "FSM Send %u ,%u \n", FSMMN111_sendcmd.Data[0], FSMMN111_sendcmd.Data[1]);
    FSMMN111_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSMMN111_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSMMN111_sendcmd, plen, to_dt, from_dt);
}

void FSM_TestVoltage(struct FSM_DeviceTree* to_dt, unsigned short cmd)
{
    FSMMN111_sendcmd.opcode = PacketToDevice;
    FSMMN111_sendcmd.IDDevice = to_dt->IDDevice;
    FSMMN111_sendcmd.cmd = cmd;
    to_dt->dt->Proc((char*)&FSMMN111_sendcmd, sizeof(struct FSM_SendCmd), to_dt, to_dt);
}
void FSM_Test_Callback(unsigned long data)
{
    int i;
    for(i = 0; i < FSM_MN111DeviceTreeSize; i++) {
        if(FSMMN111Dev[i].reg == 0)
            break;
        switch(FSMMN111Dev[i].vst.sel) {
        case 0:
            FSM_TestVoltage(FSMMN111Dev[i].fsms, FSM_Get_MN111_Power_5V);
            break;
        case 1:
            FSM_TestVoltage(FSMMN111Dev[i].fsms, FSM_Get_MN111_Power_n5V);
            break;
        case 2:
            FSM_TestVoltage(FSMMN111Dev[i].fsms, FSM_Get_MN111_Power_n60V);
            break;
        case 3:
            FSM_TestVoltage(FSMMN111Dev[i].fsms, FSM_Get_MN111_Power_90V);
            break;
        case 4:
            FSM_TestVoltage(FSMMN111Dev[i].fsms, FSM_Get_MN111_Power_220V);
            break;
        }
        FSMMN111Dev[i].vst.sel++;
        if(FSMMN111Dev[i].vst.sel >= 5)
            FSMMN111Dev[i].vst.sel = 0;
    }
    mod_timer(&FSM_MN111_timer, jiffies + msecs_to_jiffies(1000));
}
void FSM_MN111Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;

    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;
    struct FSM_MN111Device* mn111 = (struct FSM_MN111Device*)to_dt->data;
// char datas[2];

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_MN111DeviceTreeSize; i++) {
            if(FSMMN111Dev[i].iddev == to_dt->IDDevice) {
                FSM_MN111SendStreaminfo(FSMMN111Dev[i].idstream, from_dt, to_dt);
                // FSM_TestVoltage(to_dt);
                return;
            }
        }
        for(i = 0; i < FSM_MN111DeviceTreeSize; i++) {
            if(FSMMN111Dev[i].reg == 0) {
                FSMMN111Dev[i].reg = 1;
                FSMMN111Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                FSMMN111_fsmas.iddev = to_dt->IDDevice;
                // fsmas.ToProcess=FSM_PO06RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                FSMMN111_fsmas.TransportDevice = FSMMN111Dev[i].ethdev->numdev;
                FSMMN111_fsmas.TransportDeviceType = FSM_EthernetID2;
                FSMMN111_fsmas.Data = &FSMMN111Dev[i];
                FSMMN111Dev[i].idstream = FSM_AudioStreamRegistr(FSMMN111_fsmas);
                FSMMN111Dev[i].iddev = to_dt->IDDevice;
                to_dt->TrDev = from_dt;
                to_dt->data = &FSMMN111Dev[i];
                to_dt->config = &FSMMN111Dev[i].mn111set;
                FSM_MN111SendStreaminfo(FSMMN111Dev[i].idstream, from_dt, to_dt);
                if(to_dt->debug)
                    printk(KERN_INFO "FSM MN111 Device Added %u \n", to_dt->IDDevice);
                FSMMN111Dev[i].fsms = to_dt;
                // FSM_TestVoltage(to_dt);

                // datas[0]=0xd0;
                // datas[1]=0xd1;
                // FSM_AudioStreamToUser(0,datas,2);
                break;
            }
        }

        break;
    case DelLisr:
        for(i = 0; i < FSM_MN111DeviceTreeSize; i++) {
            if((FSMMN111Dev[i].reg == 1) && (FSMMN111Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSMMN111Dev[i].idstream);
                FSMMN111Dev[i].reg = 0;
                if(to_dt->debug)
                    printk(KERN_INFO "FSM MN111 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSM_Ans_Get_MN111_Power_5V:
            mn111->vst.MN111_Power_5V.value = ((unsigned short*)scmd->Data)[0];
            mn111->vst.MN111_Power_5V.newdata = 1;
            printk(KERN_WARNING "MN111 %u: 5V: %u \n", scmd->IDDevice, ((unsigned short*)scmd->Data)[0]);
            break;

        case FSM_Ans_Get_MN111_Power_n5V:
            mn111->vst.MN111_Power_n5V.value = ((unsigned short*)scmd->Data)[0];
            mn111->vst.MN111_Power_n5V.newdata = 1;
            printk(KERN_WARNING "MN111 %u: -5V: %u \n", scmd->IDDevice, ((unsigned short*)scmd->Data)[0]);
            break;
        case FSM_Ans_Get_MN111_Power_n60V:
            mn111->vst.MN111_Power_n60V.value = ((unsigned short*)scmd->Data)[0];
            mn111->vst.MN111_Power_n60V.newdata = 1;
            printk(KERN_WARNING "MN111 %u: -60V: %u \n", scmd->IDDevice, ((unsigned short*)scmd->Data)[0]);
            break;
        case FSM_Ans_Get_MN111_Power_90V:
            mn111->vst.MN111_Power_90V.value = ((unsigned short*)scmd->Data)[0];
            mn111->vst.MN111_Power_90V.newdata = 1;
            printk(KERN_WARNING "MN111 %u: 90V: %u \n", scmd->IDDevice, ((unsigned short*)scmd->Data)[0]);
            break;
        case FSM_Ans_Get_MN111_Power_220V:
            mn111->vst.MN111_Power_220V.value = ((unsigned short*)scmd->Data)[0];
            mn111->vst.MN111_Power_220V.newdata = 1;
            printk(KERN_WARNING "MN111 %u: 220V: %u \n", scmd->IDDevice, ((unsigned short*)scmd->Data)[0]);
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
            printk(KERN_WARNING "MN111 %u: Server Not Connect \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        case FSM_MN111_Power_5V_Error:
            printk(KERN_WARNING "MN111 %u: 5V Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        case FSM_MN111_Power_n5V_Error:
            printk(KERN_WARNING "MN111 %u: -5V Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        case FSM_MN111_Power_n60V_Error:
            printk(KERN_WARNING "MN111 %u: -60V Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        case FSM_MN111_Power_90V_Error:
            printk(KERN_WARNING "MN111 %u: 90V Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        case FSM_MN111_Power_220V_Error:
            printk(KERN_WARNING "MN111 %u: 220V Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Trouble: ///<Сбой
        switch(((struct FSM_TroubleSignal*)data)->ID) {
        case FSM_CCK_Memory_Test_Filed:
            printk(KERN_ERR "MN111 %u: Memory Error \n", ((struct FSM_Header*)(data))->IDDevice);
            break;
        }
        break;
    case Beep: ///<Звук
        break;
    case PacketFromUserSpace:
        switch(scmd->cmd) {

        case FSM_Get_MN111_Power_5V:
        case FSM_Get_MN111_Power_n5V:
        case FSM_Get_MN111_Power_n60V:
        case FSM_Get_MN111_Power_90V:
        case FSM_Get_MN111_Power_220V:
            scmd->IDDevice = to_dt->IDDevice;
            scmd->opcode = SendCmdToDevice;
            to_dt->TrDev->dt->Proc((char*)scmd, FSMH_Header_Size_SendCmd, to_dt->TrDev, to_dt);
            // printk( KERN_ERR "MN111 %u: Reqest\n",((struct FSM_Header*)(data))->IDDevice);
            break;
        case FSM_Read_MN111_Power_5V:
            ((unsigned short*)scmd->Data)[0] = mn111->vst.MN111_Power_5V.value;
            break;
        case FSM_Read_MN111_Power_n5V:
            ((unsigned short*)scmd->Data)[0] = mn111->vst.MN111_Power_n5V.value;
            break;
        case FSM_Read_MN111_Power_n60V:
            ((unsigned short*)scmd->Data)[0] = mn111->vst.MN111_Power_n60V.value;
            break;
        case FSM_Read_MN111_Power_90V:
            ((unsigned short*)scmd->Data)[0] = mn111->vst.MN111_Power_90V.value;
            break;
        case FSM_Read_MN111_Power_220V:
            ((unsigned short*)scmd->Data)[0] = mn111->vst.MN111_Power_220V.value;
            break;
        case FSM_Read_MN111_AutoReqest:
            mod_timer(&FSM_MN111_timer, jiffies + msecs_to_jiffies(1000));
            break;
        }
        break;
    case PacketToDevice:
        switch(scmd->cmd) {

        case FSM_Get_MN111_Power_5V:
        case FSM_Get_MN111_Power_n5V:
        case FSM_Get_MN111_Power_n60V:
        case FSM_Get_MN111_Power_90V:
        case FSM_Get_MN111_Power_220V:
            scmd->IDDevice = to_dt->IDDevice;
            scmd->opcode = SendCmdToDevice;
            to_dt->TrDev->dt->Proc((char*)scmd, FSMH_Header_Size_SendCmd, to_dt->TrDev, to_dt);
            break;
        }
        break;
    default:
        break;
    }
    if(to_dt->debug)
        printk(KERN_INFO "RPack %u \n", len);
}
EXPORT_SYMBOL(FSM_MN111Recive);
void ApplaySettingMN111(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    memset(&FSMMN111_sendcmd, 0, sizeof(FSMMN111_sendcmd));
    if(to_dt->debug)
        printk(KERN_INFO "FSM_Set\n");
    FSMMN111_sendcmd.cmd = SetSettingClientMN111;
    FSMMN111_sendcmd.countparam = 1;
    FSMMN111_sendcmd.IDDevice = to_dt->IDDevice;
    FSMMN111_sendcmd.CRC = 0;
    FSMMN111_sendcmd.opcode = SendCmdToDevice;
    memcpy(&FSMMN111_sendcmd.Data,
           &(((struct FSM_MN111Device*)to_dt->data)->mn111set.fsm_mn111_su_s),
           sizeof(struct fsm_mn111_subscriber));
    from_dt->dt->Proc((char*)&FSMMN111_sendcmd,
                      sizeof(struct FSM_SendCmd) - sizeof(FSMMN111_sendcmd.Data) + sizeof(struct fsm_mn111_subscriber),
                      from_dt,
                      to_dt);
}

static int __init FSM_MN111_init(void)
{


    FSMMN111_dft.aplayp = ApplaySettingMN111;
    FSMMN111_dft.type = (unsigned char)AudioDevice;
    FSMMN111_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSMMN111_dft.PodVidDevice = (unsigned char)CCK;
    FSMMN111_dft.KodDevice = (unsigned char)MN111;
    FSMMN111_dft.Proc = FSM_MN111Recive;
    FSMMN111_dft.config_len = sizeof(struct fsm_mn111_setting);
    FSM_DeviceClassRegister(FSMMN111_dft);
    printk(KERN_INFO "FSM MN111 Module loaded\n");
    FSM_SendEventToAllDev(FSM_CCK_MN111_Started);
    setup_timer(&FSM_MN111_timer, FSM_Test_Callback, 0);

    return 0;
}

static void __exit FSM_MN111_exit(void)
{
    FSM_ClassDeRegister(FSMMN111_dft);
    del_timer(&FSM_MN111_timer);

    printk(KERN_INFO "FSM MN111 Module unloaded\n");
}

module_init(FSM_MN111_init);
module_exit(FSM_MN111_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM MN111 Module");
MODULE_LICENSE("GPL");
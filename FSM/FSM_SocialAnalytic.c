/*!
\file
\brief Модуль анализа соц сетей
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include "FSM/FSM_SA/FSM_SA.h"

struct FSM_DeviceFunctionTree FSMSA_dft;
struct FSM_SADevice FSMSADev[FSM_SADeviceTreeSize];
struct FSM_SendCmd FSMSA_sendcmd;
//struct FSM_AudioStream fsmas;

int AnalizData(char* Data, unsigned short len)
{
    return 0;
}

void FSM_SASRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i;

    struct FSM_SendCmdTS* scmd = (struct FSM_SendCmdTS*)data;

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_PO08DeviceTreeSize; i++) {
            if(FSMSADev[i].iddev == to_dt->IDDevice) {
                return;
            }
        }
        for(i = 0; i < FSM_SADeviceTreeSize; i++) {
            if(FSMSADev[i].reg == 0) {
                FSMSADev[i].reg = 1;
                FSMSADev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                to_dt->data = &FSMSADev[i];
                to_dt->config = &FSMSADev[i].saset;
                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_SADeviceTreeSize; i++) {
            if((FSMSADev[i].reg == 1) && (FSMSADev[i].iddev == to_dt->IDDevice)) {

                FSMSADev[i].reg = 0;
                printk(KERN_INFO "FSM SA Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(scmd->cmd) {
        case FSMSA_IDK:

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
            break;
        }
        break;
    case Trouble: ///<Сбой
        switch(((struct FSM_TroubleSignal*)data)->ID) {
        case FSM_CCK_Memory_Test_Filed:
            break;
        }
        break;
    case Beep: ///<Звук
        break;
    default:
        break;
    }

    printk(KERN_INFO "RPack %u \n", len);
}
EXPORT_SYMBOL(FSM_SASRecive);

void ApplaySettingSA(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
}

static int __init FSM_SA_init(void)
{

    FSMSA_dft.aplayp = ApplaySettingSA;
    FSMSA_dft.type = (unsigned char)SocialAnalytica;
    FSMSA_dft.VidDevice = (unsigned char)FSMSA_Analiz;
    FSMSA_dft.PodVidDevice = (unsigned char)FSMSA_AnalizData;
    FSMSA_dft.KodDevice = (unsigned char)FSMSA_AnalizDataServer;
    FSMSA_dft.Proc = FSM_SASRecive;
    FSMSA_dft.config_len = sizeof(struct fsm_sa_setting);
    FSM_DeviceClassRegister(FSMSA_dft);
    printk(KERN_INFO "FSM SA Module loaded\n");


    return 0;
}

static void __exit FSM_SA_exit(void)
{

    FSM_ClassDeRegister(FSMSA_dft);
    printk(KERN_INFO "FSM SA Module unloaded\n");

}

module_init(FSM_SA_init);
module_exit(FSM_SA_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM SA Module");
MODULE_LICENSE("GPL");
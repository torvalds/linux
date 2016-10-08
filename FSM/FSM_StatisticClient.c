/*!
\file
\brief Модуль клиент статистики
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#define SUCCESS 0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>

#include "FSM/FSMAudio/FSM_AudioStream.h"
#include <FSM/FSMEthernet/FSMEthernetHeader.h>
#include "FSM/FSMDevice/fcmprotocol.h"
#include "FSM/FSMDevice/fcm_audiodeviceclass.h"
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include "FSM/FSMSetting/FSM_settings.h"
#include "FSM/FSMDevice/fsm_statusstruct.h"
#include "FSM/FSM_Client/FSM_client.h"
struct fsm_statusstruct fsm_ss;
struct FSMSSetconfigParam fsmspar;
struct FSM_SendCmdTS FSM_SC_regpcmdts;

int FSMStat_rcv(char* Data, short len, struct fsm_client_struct* dev)
{
    char dats = Data[0];
    struct FSM_SendCmd* fscts = (struct FSM_SendCmd*)Data;
    //     struct FSM_AnsDeviceRegistr* fscar= (struct FSM_AnsDeviceRegistr*)Data;
    switch(dats) {
    case RegDevice: ///< Регистрация устройства
        break;
    case AnsRegDevice:
        break;
    case AnsDelList: ///< Подтверждение удаления устройства из списка
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToDevice: ///< Отправка команды устройству
        switch(fscts->cmd) {
        case AnsGetStatistic:
            memcpy(&fsm_ss.statel[((struct fsm_status_element*)fscts->Data)
                                      ->row][((struct fsm_status_element*)fscts->Data)->column],
                   fscts->Data,
                   sizeof(struct fsm_status_element));
            break;
        case SendSettingFull:
            FSM_SendSignalToPipe("fsmstat", FSM_ServerStatisticChanged);
            break;
        }

        break;
        case SendCmdGlobalcmdToClient: ///< Отправка команды устройству
        switch(fscts->cmd) {
        case FSMNotRegistred:
            printk(KERN_INFO "FNR");
              FSM_RegisterDevice(FSM_StatisicID,
                       StatisticandConfig,
                       FSMDeviceStatistic,
                       ComputerStatistic,
                       PCx86,
                       (DeviceClientProcess)FSMStat_rcv);
            break;
        }

        break;
    case AnsSendCmdToDevice: ///< Подтверждение приёма команды устройством
        break;
    case RqToDevice: ///< Ответ на команду устройством
        break;
    case AnsRqToDevice: ///< Подтверждение приёма команды сервером
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        break;
    case SendTxtMassage: ///< Отправка текстового сообщения
        break;
    case AnsSendTxtMassage: ///< Подтверждение приёма текстового сообщения
        break;
    case SendTxtEncMassage: ///< Отправка зашифрованного текстового сообщения
        break;
    case AnsSendTxtEncMassage: ///< Подтверждение приёма зашифрованного текстового сообщения
        break;
    case SendAudio: ///< Передача аудио данных
        break;
    case SendVideo: ///< Передача видео данных
        break;
    case SendBinData: ///< Передача бинарных данных
        break;
    case AnsSendBinData: ///< Подтверждение приёма бинарных данных
        break;
    case SendSMS: ///< Отправить СМС
        break;
    case SendAnsSMS: ///< Подтверждение СМС
        break;
    case SendSMStoDev: ///< Передача СМС устройству
        break;
    case SendAnsSMStoDev: ///< Подтверждение СМС устройством
        break;
    case SendEncSMS: ///< Отправить зашифрованного СМС
        break;
    case SendAnsEncSMS: ///<Подтверждение зашифрованного СМС
        break;
    case SendEncSMStoDev: ///< Отправить зашифрованного СМС устройству
        break;
    case SendAnsEncSMStoDev: ///< Подтверждение зашифрованного СМС  устройства
        break;
    case SendEmail: ///< Отправка email
        break;
    case AnsEmail: ///<Подтверждение email
        break;
    case SendEmailtoDevice: ///<Передача email устройству
        break;
    case AnsSendEmailtoDevice: ///<Подтверждение email устройством
        break;
    case SendEncEmail: ///<Отправить зашифрованного email
        break;
    case AnsEncEmail: ///<Подтверждение зашифрованного email
        break;
    case SendEncEmailtoDev: ///< Отправить зашифрованного email устройству
        break;
    case AnsEncEmailtoDev: ///< Подтверждение зашифрованного email   устройства
        break;
    case SocSend: ///< Отправка сообщение в социальную сеть
        break;
    case AnsSocSend: ///< Подтверждение сообщения в социальную сеть
        break;
    case SocSendtoDev: ///< Передача сообщения в социальную сеть устройству
        break;
    case AnsSocSendtoDev: ///< Подтверждение   сообщения в социальную сеть устройством
        break;
    case SocEncSend: ///< Отправить зашифрованного сообщения в социальную сеть
        break;
    case AnsSocEncSend: ///< Подтверждение зашифрованного сообщения в социальную сеть
        break;
    case SocEncSendtoDev: ///<	Отправить зашифрованного сообщения в социальную сеть устройству
        break;
    case AnsSocEncSendtoDev: ///<	Подтверждение зашифрованного сообщения в социальную сеть   устройства
        break;
    case Alern: ///<Тревога
        break;
    case Warning: ///<Предупреждение
        break;
    case Trouble: ///<Сбой
        break;
    case Beep: ///<Звук
        break;
    }
    return 0;
};
void FSMStat_rcv_ioctl(char* Data, short len, struct fsm_ioctl_struct* ioctl)
{
    struct FSM_SendCmdUserspace* fsm_scus = (struct FSM_SendCmdUserspace*)Data;
    switch(fsm_scus->cmd) {
    case FSMIOCTLStat_Requst:
        memset(&fsm_ss, 0, sizeof(fsm_ss));
        FSM_SC_regpcmdts.opcode = SendCmdToServer;
        FSM_SC_regpcmdts.countparam = 1;
        FSM_SC_regpcmdts.CRC = 0;
        FSM_SC_regpcmdts.IDDevice = FSM_StatisicID;
        FSM_SC_regpcmdts.cmd = GetStatistic;
        FSM_Send_Ethernet_TS(&FSM_SC_regpcmdts, sizeof(struct FSM_SendCmdTS));
        printk(KERN_INFO "Request");
        break;
    case FSMIOCTLStat_Read:
        //printk(KERN_INFO "Read %u - %u", fsm_scus->Data[0], fsm_scus->Data[1]);
        memcpy(
            fsm_scus->Data, &(fsm_ss.statel[fsm_scus->Data[0]][fsm_scus->Data[1]]), sizeof(struct fsm_status_element));

        break;
    }
}

void FSM_StatEventLoaded(char* Data, short len, struct fsm_event_struct* cl_str)
{
    printk(KERN_INFO "Event");
    memset(&fsm_ss, 0, sizeof(fsm_ss));
    FSM_SC_regpcmdts.opcode = SendCmdToServer;
    FSM_SC_regpcmdts.countparam = 1;
    FSM_SC_regpcmdts.CRC = 0;
    FSM_SC_regpcmdts.IDDevice = FSM_StatisicID;
    FSM_SC_regpcmdts.cmd = GetStatistic;
    FSM_Send_Ethernet_TS(&FSM_SC_regpcmdts, sizeof(struct FSM_SendCmdTS));
}
void FSM_StStartEventLoaded(char* Data, short len, struct fsm_event_struct* cl_str)
{
    FSM_RegisterDevice(FSM_StatisicID,
                       StatisticandConfig,
                       FSMDeviceStatistic,
                       ComputerStatistic,
                       PCx86,
                       (DeviceClientProcess)FSMStat_rcv);
}

static int __init FSM_Client_Statistic_init(void)
{
    FSM_RegisterDevice(FSM_StatisicID,
                       StatisticandConfig,
                       FSMDeviceStatistic,
                       ComputerStatistic,
                       PCx86,
                       (DeviceClientProcess)FSMStat_rcv);
    FSM_RegisterEvent(FSM_ServerStatisticChanged, FSM_StatEventLoaded);
    FSM_RegisterIOCtl(FSM_StatistickIOCtlId, FSMStat_rcv_ioctl);
    FSM_RegisterEvent(FSM_StaticServerRun, FSM_StStartEventLoaded);
    printk(KERN_INFO "FSM Statistic module loaded\n");
    return 0;
}

static void __exit FSM_Client_Statistic_exit(void)
{
    FSM_DeleteDevice(FSM_StatisicID);
    FSM_DeleteEvent(FSM_ServerStatisticChanged);
    printk(KERN_INFO "FSM Statistic module unloaded\n");
}
module_init(FSM_Client_Statistic_init);
module_exit(FSM_Client_Statistic_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Statistic Module");
MODULE_LICENSE("GPL");
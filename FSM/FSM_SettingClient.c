/*!
\file
\brief Модуль клиент конфигурирования
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
struct fsm_devices_config fsm_ds;
struct FSMSSetconfigParam fsmspar;
unsigned short FSM_SC_setservid;

int FSMSet_rcv(char* Data, short len, struct fsm_client_struct* dev)
{
    char dats = Data[0];
    struct FSM_SendCmd* fscts = (struct FSM_SendCmd*)Data;
    // struct FSM_AnsDeviceRegistr* fscar= (struct FSM_AnsDeviceRegistr*)Data;
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
        case AnsGetSet:
            // printk( KERN_INFO "FSM Cmd %u\n",fscts->cmd);
            memcpy(&fsm_ds.setel[((struct fsm_device_config*)fscts->Data)->row][((struct fsm_device_config*)fscts->Data)
                                                                                    ->column],
                   fscts->Data,
                   sizeof(struct fsm_device_config));
            break;

        

        case SetSettingClient:
            printk(KERN_INFO "FSM_Setting_Applay\n");
            FSM_DeleteDevice(FSM_SettingID);
            FSM_SC_setservid = ((struct fsm_ClientSetting_Setting*)fscts->Data)->id;
            FSM_RegisterDevice(FSM_SettingID,
                               StatisticandConfig,
                               FSMDeviceConfig,
                               ComputerStatistic,
                               PCx86,
                               (DeviceClientProcess)FSMSet_rcv);
            break;
        }
        case SendCmdGlobalcmdToClient: ///< Отправка команды устройству
        switch(fscts->cmd) {
      case FSMNotRegistred:
            printk(KERN_INFO "Device FSR\n");
            FSM_RegisterDevice(FSM_SettingID,
                               StatisticandConfig,
                               FSMDeviceConfig,
                               ComputerStatistic,
                               PCx86,
                               (DeviceClientProcess)FSMSet_rcv);
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

static int __init FSM_Client_Setting_init(void)
{
    FSM_SC_setservid = 22;
    FSM_RegisterDevice(
        FSM_SettingID, StatisticandConfig, FSMDeviceConfig, ComputerStatistic, PCx86, (DeviceClientProcess)FSMSet_rcv);
    printk(KERN_INFO "FSM Setting module loaded\n");
    return 0;
}

static void __exit FSM_Client_Setting_exit(void)
{
    FSM_DeleteDevice(FSM_SettingID);
    printk(KERN_INFO "FSM Setting module unloaded\n");
}
module_init(FSM_Client_Setting_init);
module_exit(FSM_Client_Setting_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Setting Module");
MODULE_LICENSE("GPL");

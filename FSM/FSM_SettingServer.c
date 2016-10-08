/*!
\file
\brief Модуль сервер настроек
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include "FSM/FSM_Client/FSM_client.h"

struct FSM_SendCmd fsm_Setting_scmdt;
struct fsm_Setting_Setting fsm_Setting_fsmSSS;

void FSM_SettingRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    struct fsm_devices_config* fsmset;
    // struct FSM_DeviceTree* fsdt;
    int i, j;
    short hlen;
    //  unsigned short tmp;
    struct FSM_SendCmdTS* fscts = (struct FSM_SendCmdTS*)data;

    switch(data[0]) {
    case RegDevice:
        FSM_Statstic_SetStatus(to_dt, "ok");
        to_dt->config = &fsm_Setting_fsmSSS;
        fsm_Setting_fsmSSS.fsmcs.id = to_dt->IDDevice;
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        fsmset = FSM_GetSetting();
        switch(fscts->cmd) {
        // printk( KERN_INFO "FSM Cmd %u\n",fscts->cmd);
        case GetSet:
            hlen = sizeof(struct FSM_SendCmd) - sizeof(fsm_Setting_scmdt.Data) + sizeof(struct fsm_device_config);
            fsm_Setting_scmdt.cmd = AnsGetSet;
            fsm_Setting_scmdt.countparam = 1;
            fsm_Setting_scmdt.IDDevice = fscts->IDDevice;
            fsm_Setting_scmdt.CRC = 0;
            fsm_Setting_scmdt.opcode = SendCmdToDevice;

            for(i = 0; i < srow_cnt; i++) {
                for(j = 0; j < scolumn_cnt; j++) {
                    if(fsmset->setel[i][j].IDDevice != 0) {
                        memcpy(fsm_Setting_scmdt.Data, &fsmset->setel[i][j], sizeof(struct fsm_device_config));
                        from_dt->dt->Proc((char*)&fsm_Setting_scmdt, hlen, from_dt, to_dt);
                        // printk( KERN_INFO "FSM Send %u
                        // %s\n",fsmstate->statel[i][j].devid,fsmstate->statel[i][j].fsmdevcode);
                    }
                }
            }
            break;
        case SetSetting:
            //  printk( KERN_INFO "FSM_Setting_Applay\n" );
            FSM_Setting_Applay(FSM_FindDevice(((struct fsm_device_config*)fscts->Data)->IDDevice),
                               ((struct fsm_device_config*)fscts->Data)->config);
            break;
        }
        break;
    }
}
void ApplaySetting(struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{

    // printk( KERN_INFO "FSM_Set\n" );
    fsm_Setting_scmdt.cmd = SetSettingClient;
    fsm_Setting_scmdt.countparam = 1;
    fsm_Setting_scmdt.IDDevice = to_dt->IDDevice;
    fsm_Setting_scmdt.CRC = 0;
    fsm_Setting_scmdt.opcode = SendCmdToDevice;
    memcpy(&fsm_Setting_scmdt.Data, &fsm_Setting_fsmSSS.fsmcs, sizeof(struct fsm_Setting_Setting));
    from_dt->dt->Proc((char*)&fsm_Setting_scmdt, sizeof(struct FSM_SendCmd), from_dt, to_dt);
}
struct FSM_DeviceFunctionTree fsm_Setting_dft;
static int __init FSM_Setting_Server_init(void)
{
    fsm_Setting_dft.type = (unsigned char)StatisticandConfig;
    fsm_Setting_dft.VidDevice = (unsigned char)FSMDeviceConfig;
    fsm_Setting_dft.PodVidDevice = (unsigned char)ComputerStatistic;
    fsm_Setting_dft.KodDevice = (unsigned char)PCx86;
    fsm_Setting_dft.Proc = FSM_SettingRecive;
    fsm_Setting_dft.config_len = sizeof(struct fsm_Setting_Setting);
    fsm_Setting_dft.aplayp = ApplaySetting;
    FSM_DeviceClassRegister(fsm_Setting_dft);
    printk(KERN_INFO "FSM Setting Server loaded\n");
    return 0;
}

static void __exit FSM_Setting_Server_exit(void)
{
    printk(KERN_INFO "FSM  Setting Server unloaded\n");
}

module_init(FSM_Setting_Server_init);
module_exit(FSM_Setting_Server_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Setting Server Module");
MODULE_LICENSE("GPL");
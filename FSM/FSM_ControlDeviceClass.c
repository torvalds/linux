#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct FSM_DeviceFunctionTree FSMControlDevice_dft;

void FSM_ControlDeviceRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
}
static int __init FSMControlDevice_init(void)
{
    FSMControlDevice_dft.aplayp = 0;
    FSMControlDevice_dft.type = (unsigned char)ControlMachine;
    FSMControlDevice_dft.VidDevice = (unsigned char)Computer;
    FSMControlDevice_dft.PodVidDevice = (unsigned char)PC;
    FSMControlDevice_dft.KodDevice = (unsigned char)ARM;
    FSMControlDevice_dft.Proc = FSM_ControlDeviceRecive;
    FSMControlDevice_dft.config_len = 0;
    FSM_DeviceClassRegister(FSMControlDevice_dft);
    printk(KERN_INFO "FSM ControlDevice loaded\n");
    FSM_SendEventToAllDev(FSM_ControlDeviceRun);
    return 0;
}
static void __exit FSMControlDevice_exit(void)
{
    FSM_ClassDeRegister(FSMControlDevice_dft);
    printk(KERN_INFO "FSM ControlDevice module unloaded\n");
}

module_init(FSMControlDevice_init);
module_exit(FSMControlDevice_exit);
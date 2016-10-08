#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include "FSM/FSM_Client/FSM_client.h"

static int __init FSMControlDevice_init(void)
{
    FSM_RegisterServer(27, ControlMachine, Computer, PC, ARM);
    printk(KERN_INFO "FSM ControlDevice loaded\n");

    return 0;
}
static void __exit FSMControlDevice_exit(void)
{
    FSM_DeregisterServer();
    printk(KERN_INFO "FSM ControlDevice module unloaded\n");
}

module_init(FSMControlDevice_init);
module_exit(FSMControlDevice_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Control Device Module");
MODULE_LICENSE("GPL");
/*!
\file
\brief Модуль командного управления
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#define SUCCESS 0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>

#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct FSM_SendCmdUserspace FSM_ServerIO_fsmdat;

static dev_t FSM_ServerIO_first;       // Global variable for the first device number
static struct cdev FSM_ServerIO_c_dev; // Global variable for the character device structure
static struct class* FSM_ServerIO_cl;  // Global variable for the device class

static int device_open(struct inode* inode, struct file* file)
{
    // printk( KERN_INFO "FSM SIOCTLOpen\n" );
    return SUCCESS;
}

static int device_release(struct inode* inode, struct file* file)
{
    // printk( KERN_INFO "FSM SIOCTLClose\n" );
    return SUCCESS;
}

long device_ioctl(struct file* f, unsigned int cmd, unsigned long arg)
{
    struct FSM_DeviceTree* dftv;

    switch(cmd) {
    case FSMIOCTL_SendData:
        if(copy_from_user(&FSM_ServerIO_fsmdat, (void*)arg, sizeof(struct FSM_SendCmdUserspace)))
            return -EFAULT;
        dftv = FSM_FindDevice(FSM_ServerIO_fsmdat.IDDevice);
        if(dftv != 0) {
            dftv->dt->Proc((char*)&FSM_ServerIO_fsmdat, sizeof(struct FSM_SendCmdUserspace), dftv, 0);
            if(dftv->debug)
                printk(KERN_INFO "FSM SIOCTL, \n");
        }
        FSM_ServerIO_fsmdat.opcode = PacketToUserSpace;
        if(copy_to_user((void*)arg, &FSM_ServerIO_fsmdat, sizeof(struct FSM_SendCmdUserspace)))
            return -EFAULT;

        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

static struct file_operations fops = { .open = device_open, .release = device_release, .unlocked_ioctl = device_ioctl };

static int __init FSM_SIOCTLModule_init(void)
{
    if(alloc_chrdev_region(&FSM_ServerIO_first, 0, 1, "fsmr") < 0)
        return -1;
    if((FSM_ServerIO_cl = class_create(THIS_MODULE, "fsm")) == NULL) {
        unregister_chrdev_region(FSM_ServerIO_first, 1);
        return -1;
    }
    if(device_create(FSM_ServerIO_cl, NULL, FSM_ServerIO_first, NULL, "fsmio") == NULL) {
        class_destroy(FSM_ServerIO_cl);
        unregister_chrdev_region(FSM_ServerIO_first, 1);
        return -1;
    }
    cdev_init(&FSM_ServerIO_c_dev, &fops);
    if(cdev_add(&FSM_ServerIO_c_dev, FSM_ServerIO_first, 1) == -1) {
        device_destroy(FSM_ServerIO_cl, FSM_ServerIO_first);
        class_destroy(FSM_ServerIO_cl);
        unregister_chrdev_region(FSM_ServerIO_first, 1);
        return -1;
    }
    printk(KERN_INFO "FSM SIOCTL module loaded\n");
    return 0;
}

static void __exit FSM_SIOCTLModule_exit(void)
{
    cdev_del(&FSM_ServerIO_c_dev);
    device_destroy(FSM_ServerIO_cl, FSM_ServerIO_first);
    class_destroy(FSM_ServerIO_cl);
    unregister_chrdev_region(FSM_ServerIO_first, 1);

    printk(KERN_INFO "FSM SIOCTL module unloaded\n");
}
module_init(FSM_SIOCTLModule_init);
module_exit(FSM_SIOCTLModule_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM SIOCTL Module");
MODULE_LICENSE("GPL");
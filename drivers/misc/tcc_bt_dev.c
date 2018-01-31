/* SPDX-License-Identifier: GPL-2.0 */

/*
 *  Copyright (C) 2010-2011  RDA Micro <anli@rdamicro.com>
 *  This file belong to RDA micro
 * File:        drivers/char/tcc_bt_dev.c
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <asm/mach-types.h>
#include <mach/iomux.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/wakelock.h>
#include <linux/rfkill-rk.h>
#include <linux/platform_device.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>


#ifndef HOST_WAKE_DELAY
#define HOST_WAKE_DELAY
#endif

#define DEV_NAME "tcc_bt_dev"

#define BT_DEV_MAJOR_NUM		    234
#define BT_DEV_MINOR_NUM		    0

#define IOCTL_BT_DEV_POWER   	    _IO(BT_DEV_MAJOR_NUM, 100)
#define IOCTL_BT_SET_EINT        _IO(BT_DEV_MAJOR_NUM, 101)

static wait_queue_head_t        eint_wait;
static struct work_struct       rda_bt_event_work;
//static struct workqueue_struct *rda_bt_workqueue;

static int irq_num  = -1;
static int eint_gen;
static int eint_mask;
static struct class *bt_dev_class;
static struct mutex  sem;
static struct tcc_bt_platform_data *tcc_bt_pdata = NULL;
static int irq_flag = 0;


static void rda_bt_enable_irq(void)
{
    if((irq_num != -1) && (irq_flag == 0))
    {
        enable_irq(irq_num);
        irq_flag = 1;
    }
}

static void rda_bt_disable_irq(void)
{
    if((irq_num != -1) && (irq_flag == 1))
    {	
        disable_irq_nosync(irq_num);
        irq_flag = 0;
    }       
}

#if 0
static void rda_bt_work_fun(struct work_struct *work)
{
    struct hci_dev *hdev = NULL;

    /* BlueZ stack, hci_uart driver */
    hdev = hci_dev_get(0);
    
    if(hdev == NULL)
    {
        /* Avoid the early interrupt before hci0 registered */
        //printk(KERN_ALERT "hdev is NULL\n ");
    }
    else
    {
        printk(KERN_ALERT "Send host wakeup command.\n");
        hci_send_cmd(hdev, 0xC0FC, 0, NULL);
    }
    
    rda_bt_enable_irq();
}
#endif

static int tcc_bt_dev_open(struct inode *inode, struct file *file)
{
    printk("[## BT ##] tcc_bt_dev_open.\n");
    eint_gen  = 0;
    eint_mask = 0;
    return 0;
}

static int tcc_bt_dev_release(struct inode *inode, struct file *file)
{
    printk("[## BT ##] tcc_bt_dev_release.\n");
    eint_gen  = 0;
    eint_mask = 0;
    return 0;
}

/*****************************************************************************
 *  tcc_bt_dev_poll
*****************************************************************************/

static unsigned int tcc_bt_dev_poll(struct file *file, poll_table *wait)
{
    uint32_t mask = 0;
    
    printk("[## BT ##] tcc_bt_poll eint_gen %d, eint_mask %d ++\n", eint_gen, eint_mask);

    wait_event_interruptible(eint_wait, (eint_gen == 1 || eint_mask == 1));
    
    printk("[## BT ##] tcc_bt_poll eint_gen %d, eint_mask %d --\n", eint_gen, eint_mask);
    
    if(eint_gen == 1)
    {
        mask = POLLIN|POLLRDNORM;
        eint_gen = 0;
    }
    else if (eint_mask == 1)
    {
        mask = POLLERR;
        eint_mask = 0;
    }
    
    return mask;
}

static int tcc_bt_power_control(int on_off)
{    
    printk("[## BT ##] tcc_bt_power_control input[%d].\n", on_off);
	
    if(on_off)
    {	    
     	 gpio_direction_output(tcc_bt_pdata->power_gpio.io, tcc_bt_pdata->power_gpio.enable);
    	 msleep(500);
         rda_bt_enable_irq();
    }
    else
    {
      	gpio_direction_output(tcc_bt_pdata->power_gpio.io, !tcc_bt_pdata->power_gpio.enable);
    	rda_bt_disable_irq();
        msleep(500);
    }

    return 0;
}


static long  tcc_bt_dev_ioctl(struct file *file, unsigned int cmd,unsigned long arg)
{
    void __user *argp = (void __user *)arg;
    int  ret   = -1;
    int  rate  = 0;

    switch(cmd)
    {
	case IOCTL_BT_DEV_POWER:
 	{
       	    printk("[## BT ##] IOCTL_BT_DEV_POWER cmd[%d] parm1[%d].\n", cmd, rate);
            if (copy_from_user(&rate, argp, sizeof(rate)))
    		return -EFAULT;

            mutex_lock(&sem);  
            ret = tcc_bt_power_control(rate);
            mutex_unlock(&sem);			
            break;
	}		
		
        case IOCTL_BT_SET_EINT:
        {
	    if (copy_from_user(&rate, argp, sizeof(rate)))
                return -EFAULT;
            printk("[## BT ##] IOCTL_BT_SET_EINT cmd[%d].\n", cmd);
            mutex_lock(&sem); 

            if(rate)
            {
                rda_bt_enable_irq();
            }
            else
            {
                rda_bt_disable_irq();
                eint_mask = 1;
                wake_up_interruptible(&eint_wait);
            }             
            mutex_unlock(&sem); 
            ret = 0;
            break;
        }
        
        default :
        {
	         printk("[## BT ##] tcc_bt_dev_ioctl cmd[%d].\n", cmd);
	         break;
	    }
    }

    return ret;
}


struct file_operations tcc_bt_dev_ops = 
{
    .owner           = THIS_MODULE,
    .unlocked_ioctl  = tcc_bt_dev_ioctl,
    .open            = tcc_bt_dev_open,
    .release         = tcc_bt_dev_release,
    .poll            = tcc_bt_dev_poll,
};

#ifdef HOST_WAKE_DELAY
struct wake_lock rda_bt_wakelock;
#endif

static irqreturn_t rda_bt_host_wake_irq(int irq, void *dev)
{
    printk("rda_bt_host_wake_irq.\n");
    rda_bt_disable_irq();	

#ifdef HOST_WAKE_DELAY
    wake_lock_timeout(&rda_bt_wakelock, 3 * HZ); 
#endif

#if 0 //CONFIG_BT_HCIUART
    if(rda_bt_workqueue)
        queue_work(rda_bt_workqueue, &rda_bt_event_work);
#else
    /* Maybe handle the interrupt in user space? */
    eint_gen = 1;
    wake_up_interruptible(&eint_wait);
    /* Send host wakeup command in user space, enable irq then */
#endif
    
    return IRQ_HANDLED;
}

static int tcc_bt_probe(struct platform_device *pdev)
{
    int ret;

    struct tcc_bt_platform_data *pdata = pdev->dev.platform_data;
    
    printk("tcc_bt_probe.\n");
    if(pdata == NULL) {
    	printk("tcc_bt_probe failed.\n");
    	return -1;
    }
    tcc_bt_pdata = pdata;
    
    if(pdata->power_gpio.io != INVALID_GPIO) {
	if (gpio_request(pdata->power_gpio.io, "ldoonpin")){
	    printk("tcc bt ldoonpin is busy!\n");
	    return -1;
	}
    }
    gpio_direction_output(pdata->power_gpio.io, !pdata->power_gpio.enable);//GPIO_LOW
    
#ifdef HOST_WAKE_DELAY
    wake_lock_init(&rda_bt_wakelock, WAKE_LOCK_SUSPEND, "rda_bt_wake");
#endif

    if(pdata->wake_host_gpio.io != INVALID_GPIO) {
	if (gpio_request(pdata->wake_host_gpio.io, "tcc_bt_wake")){
	    printk("tcc bt wakeis busy!\n");
	    gpio_free(pdata->wake_host_gpio.io);
	    return -1;
	}
    }	

    gpio_direction_input(pdata->wake_host_gpio.io);
    irq_num = gpio_to_irq(pdata->wake_host_gpio.io);
    ret = request_irq(irq_num, rda_bt_host_wake_irq, pdata->wake_host_gpio.enable, "tcc_bt_host_wake",NULL);	   
    if(ret < 0)
    {
        printk("bt_host_wake irq request fail.\n");
    	  irq_num = -1;
        goto error;
    }

    enable_irq_wake(irq_num);
    irq_flag = 1;
    rda_bt_disable_irq();

    mutex_init(&sem);	
    printk("[## BT ##] init_module\n");
	
    ret = register_chrdev(BT_DEV_MAJOR_NUM, DEV_NAME, &tcc_bt_dev_ops);
    if(ret < 0)
    {
        printk("[## BT ##] [%d]fail to register the character device.\n", ret);
        goto error;
    }
    
    bt_dev_class = class_create(THIS_MODULE, DEV_NAME);
    if (IS_ERR(bt_dev_class)) 
    {
        printk("BT RDA class_create failed\n");
        goto error;
    }
	
    device_create(bt_dev_class, NULL, MKDEV(BT_DEV_MAJOR_NUM, BT_DEV_MINOR_NUM), NULL, DEV_NAME);

    init_waitqueue_head(&eint_wait);
   
    /*
    INIT_WORK(&rda_bt_event_work, rda_bt_work_fun);
    rda_bt_workqueue = create_singlethread_workqueue("rda_bt");
    if (!rda_bt_workqueue)
    {
        printk("create_singlethread_workqueue failed.\n");
        ret = -ESRCH;
        goto error;
    } 
    */
 
    return 0;
    
error:
    gpio_free(pdata->power_gpio.io); 
    gpio_free(pdata->wake_host_gpio.io);
    return ret;    
}

static int tcc_bt_remove(struct platform_device *pdev)
{
    printk("[## BT ##] cleanup_module.\n");
    free_irq(irq_num, NULL);
    unregister_chrdev(BT_DEV_MAJOR_NUM, DEV_NAME);
    if(tcc_bt_pdata)
        tcc_bt_pdata = NULL;

    cancel_work_sync(&rda_bt_event_work);
    //destroy_workqueue(rda_bt_workqueue);    
    
#ifdef HOST_WAKE_DELAY    
    wake_lock_destroy(&rda_bt_wakelock);
#endif    

    return 0;
}

static struct platform_driver tcc_bt_driver = {
        .probe = tcc_bt_probe,
        .remove = tcc_bt_remove,
        .driver = {
                .name = "tcc_bt_dev",
                .owner = THIS_MODULE,
        },
};

static int __init tcc_bt_init_module(void)
{
    printk("Enter %s\n", __func__);
    return platform_driver_register(&tcc_bt_driver);
}

static void __exit tcc_bt_cleanup_module(void)
{
    printk("Enter %s\n", __func__);
    platform_driver_unregister(&tcc_bt_driver);
}

module_init(tcc_bt_init_module);
module_exit(tcc_bt_cleanup_module);


MODULE_AUTHOR("Telechips Inc. linux@telechips.com");
MODULE_DESCRIPTION("TCC_BT_DEV");
MODULE_LICENSE("GPL");



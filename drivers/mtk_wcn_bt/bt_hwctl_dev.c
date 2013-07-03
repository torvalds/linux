/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 * 
 * MediaTek Inc. (C) 2010. All rights reserved.
 * 
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <linux/gpio.h>
#include <mach/gpio.h>

#include "bt_hwctl.h"


#define BT_HWCTL_DEBUG_EN     0

#define BT_HWCTL_ALERT(f, s...) \
    printk(KERN_ALERT "BTHWCTL " f, ## s)

#if BT_HWCTL_DEBUG_EN
#define BT_HWCTL_DEBUG(f, s...) \
    printk(KERN_INFO "BTHWCTL " f, ## s)
#else
#define BT_HWCTL_DEBUG(f, s...) \
    ((void)0)
#endif

/**************************************************************************
 *                        D E F I N I T I O N S                           *
***************************************************************************/

#define BTHWCTL_NAME                 "bthwctl"
#define BTHWCTL_DEV_NAME             "/dev/bthwctl"
#define BTHWCTL_IOC_MAGIC            0xf6
#define BTHWCTL_IOCTL_SET_POWER      _IOWR(BTHWCTL_IOC_MAGIC, 0, uint32_t)
#define BTHWCTL_IOCTL_SET_EINT       _IOWR(BTHWCTL_IOC_MAGIC, 1, uint32_t)

wait_queue_head_t eint_wait;
int eint_gen;
int eint_mask;
int eint_handle_method = 0; // 0: for 4.1; 1: for 4.2 
struct wake_lock mt6622_irq_wakelock;
int mt6622_suspend_flag;

struct bt_hwctl {
    bool powerup;
    dev_t dev_t;
    struct class *cls;
    struct device *dev;
    struct cdev cdev;
    struct mutex sem;
};
static struct bt_hwctl *bh = NULL;

static struct mt6622_platform_data *mt6622_pdata;

/*****************************************************************************
 *  bt_hwctl_open
*****************************************************************************/
static int bt_hwctl_open(struct inode *inode, struct file *file)
{
    BT_HWCTL_DEBUG("bt_hwctl_open\n");
    eint_gen = 0;
    eint_mask = 0;
    return 0;
}

/*****************************************************************************
 *  bt_hwctl_ioctl
*****************************************************************************/
static long bt_hwctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
     
    BT_HWCTL_DEBUG("bt_hwctl_ioctl\n");
    
    if(!bh) {
        BT_HWCTL_ALERT("bt_hwctl struct not initialized\n");
        return -EFAULT;
    }
    
    switch(cmd)
    {
        case BTHWCTL_IOCTL_SET_POWER:
        {
            unsigned long pwr = 0;
            if (copy_from_user(&pwr, (void*)arg, sizeof(unsigned long)))
                return -EFAULT;
                
            BT_HWCTL_DEBUG("BTHWCTL_IOCTL_SET_POWER: %d\n", (int)pwr);
            
            mutex_lock(&bh->sem);
            if (pwr){
                ret = mt_bt_power_on();
            }
            else{
                mt_bt_power_off();
            }
            mutex_unlock(&bh->sem);
            
            break;
        }
        case BTHWCTL_IOCTL_SET_EINT:
        {
            unsigned long eint = 0;
            if (copy_from_user(&eint, (void*)arg, sizeof(unsigned long)))
                return -EFAULT;
                
            BT_HWCTL_DEBUG("BTHWCTL_IOCTL_SET_EINT: %d\n", (int)eint);
            
            mutex_lock(&bh->sem);
            if (eint){
                /* Enable irq from user space */ 
                BT_HWCTL_DEBUG("Set BT EINT enable\n");
                mt_bt_enable_irq();
            }
            else{
                /* Disable irq from user space, maybe time to close driver */
                BT_HWCTL_DEBUG("Set BT EINT disable\n");
                mt_bt_disable_irq();
                eint_mask = 1;
                wake_up_interruptible(&eint_wait);
            }
            mutex_unlock(&bh->sem);
            
            break;
        }    
        default:
            BT_HWCTL_ALERT("BTHWCTL_IOCTL not support\n");
            return -EPERM;
    }
    
    return ret;
}

/*****************************************************************************
 *  bt_hwctl_release
*****************************************************************************/
static int bt_hwctl_release(struct inode *inode, struct file *file)
{
    BT_HWCTL_DEBUG("bt_hwctl_release\n");
    eint_gen = 0;
    eint_mask = 0;
    return 0;
}

/*****************************************************************************
 *  bt_hwctl_poll
*****************************************************************************/
static unsigned int bt_hwctl_poll(struct file *file, poll_table *wait)
{
    uint32_t mask = 0;
   
    eint_handle_method = 1;
	 
    BT_HWCTL_DEBUG("bt_hwctl_poll eint_gen %d, eint_mask %d ++\n", eint_gen, eint_mask);
    //poll_wait(file, &eint_wait, wait);
    wait_event_interruptible(eint_wait, (eint_gen == 1 || eint_mask == 1));
    BT_HWCTL_DEBUG("bt_hwctl_poll eint_gen %d, eint_mask %d --\n", eint_gen, eint_mask);
    
    if(mt6622_suspend_flag == 1) {
    	printk("mt6622 wake lock 5000ms\n");
        mt6622_suspend_flag = 0;
        wake_lock_timeout(&mt6622_irq_wakelock, msecs_to_jiffies(5000));
    }
    
    if(eint_gen == 1){
        mask = POLLIN|POLLRDNORM;
        eint_gen = 0;
    }
    else if (eint_mask == 1){
        mask = POLLERR;
        eint_mask = 0;
    }
    
    return mask;
}

static void mtk_wcn_bt_work_fun(struct work_struct *work)
{
    struct hci_dev *hdev = NULL;

    /* BlueZ stack, hci_uart driver */
    hdev = hci_dev_get(0);
    if(hdev == NULL){
        /* Avoid the early interrupt before hci0 registered */
        //printk(KERN_ALERT "hdev is NULL\n ");
    }else{
        //printk(KERN_ALERT "Send host wakeup command\n");
        hci_send_cmd(hdev, 0xFCC1, 0, NULL);
    }
    
    mt_bt_enable_irq();
}

static int mt6622_probe(struct platform_device *pdev)
{
    struct mt6622_platform_data *pdata = pdev->dev.platform_data;
    
    printk("mt6622_probe.\n");
    
    mt6622_pdata = pdata;
    if(pdata == NULL) {
    	printk("mt6622_probe failed.\n");
    	return -1;
    }
    
		if(pdata->power_gpio.io != INVALID_GPIO) {
			if (gpio_request(pdata->power_gpio.io, "BT_PWR_EN")){
				printk("mt6622 power_gpio is busy!\n");
				//return -1;
			}
		}
		
		if(pdata->reset_gpio.io != INVALID_GPIO) {
			if (gpio_request(pdata->reset_gpio.io, "BT_RESET")){
				printk("mt6622 reset_gpio is busy!\n");
				gpio_free(pdata->power_gpio.io);
				//return -1;
			}
		}
		
		if(pdata->irq_gpio.io != INVALID_GPIO) {
			if (gpio_request(pdata->irq_gpio.io, "BT_EINT")){
				printk("mt6622 irq_gpio is busy!\n");
				gpio_free(pdata->power_gpio.io);
				gpio_free(pdata->reset_gpio.io);
				//return -1;
			}
		}
		
		mt_bt_power_init();
		
		return 0;
}

static int mt6622_remove(struct platform_device *pdev)
{
	struct mt6622_platform_data *pdata = pdev->dev.platform_data;
	
	printk("mt6622_remove.\n");
	
	if(pdata) {
	  if(pdata->power_gpio.io != INVALID_GPIO)
	  	gpio_free(pdata->power_gpio.io);
	  if(pdata->reset_gpio.io != INVALID_GPIO)
	  	gpio_free(pdata->reset_gpio.io);
	  if(pdata->irq_gpio.io != INVALID_GPIO)
	  	gpio_free(pdata->irq_gpio.io);
  }
	
	return 0;
}

void *mt_bt_get_platform_data(void)
{
	return (void *)mt6622_pdata;
}

/**************************************************************************
 *                K E R N E L   I N T E R F A C E S                       *
***************************************************************************/
static struct file_operations bt_hwctl_fops = {
    .owner      = THIS_MODULE,
//    .ioctl      = bt_hwctl_ioctl,
    .unlocked_ioctl = bt_hwctl_ioctl,
    .open       = bt_hwctl_open,
    .release    = bt_hwctl_release,
    .poll       = bt_hwctl_poll,
};

static struct platform_driver mt6622_driver = {
    .probe = mt6622_probe,
    .remove = mt6622_remove,
    .suspend = mt6622_suspend,
    .resume = mt6622_resume,
    .driver = {
        .name = "mt6622",
        .owner = THIS_MODULE,
    },
};

/*****************************************************************************/
static int __init bt_hwctl_init(void)
{
    int ret = -1, err = -1;
    
    BT_HWCTL_DEBUG("bt_hwctl_init\n");
    
    platform_driver_register(&mt6622_driver);
    
    if (!(bh = kzalloc(sizeof(struct bt_hwctl), GFP_KERNEL)))
    {
        BT_HWCTL_ALERT("bt_hwctl_init allocate dev struct failed\n");
        err = -ENOMEM;
        goto ERR_EXIT;
    }
    
    ret = alloc_chrdev_region(&bh->dev_t, 0, 1, BTHWCTL_NAME);
    if (ret) {
        BT_HWCTL_ALERT("alloc chrdev region failed\n");
        goto ERR_EXIT;
    }
    
    BT_HWCTL_DEBUG("alloc %s:%d:%d\n", BTHWCTL_NAME, MAJOR(bh->dev_t), MINOR(bh->dev_t));
    
    cdev_init(&bh->cdev, &bt_hwctl_fops);
    
    bh->cdev.owner = THIS_MODULE;
    bh->cdev.ops = &bt_hwctl_fops;
    
    err = cdev_add(&bh->cdev, bh->dev_t, 1);
    if (err) {
        BT_HWCTL_ALERT("add chrdev failed\n");
        goto ERR_EXIT;
    }
    
    bh->cls = class_create(THIS_MODULE, BTHWCTL_NAME);
    if (IS_ERR(bh->cls)) {
        err = PTR_ERR(bh->cls);
        BT_HWCTL_ALERT("class_create failed, errno:%d\n", err);
        goto ERR_EXIT;
    }
    
    bh->dev = device_create(bh->cls, NULL, bh->dev_t, NULL, BTHWCTL_NAME);
    mutex_init(&bh->sem);
    
    init_waitqueue_head(&eint_wait);
    
    wake_lock_init(&mt6622_irq_wakelock, WAKE_LOCK_SUSPEND, "mt6622_irq_wakelock");
    
    /*INIT_WORK(&mtk_wcn_bt_event_work, mtk_wcn_bt_work_fun);
    mtk_wcn_bt_workqueue = create_singlethread_workqueue("mtk_wcn_bt");
    if (!mtk_wcn_bt_workqueue) {
        printk("create_singlethread_workqueue failed.\n");
        err = -ESRCH;
        goto ERR_EXIT;
    }*/    
    
    /* request gpio used by BT */
    //mt_bt_gpio_init();
    
    BT_HWCTL_DEBUG("bt_hwctl_init ok\n");
    
    return 0;
    
ERR_EXIT:
    if (err == 0)
        cdev_del(&bh->cdev);
    if (ret == 0)
        unregister_chrdev_region(bh->dev_t, 1);
        
    if (bh){
        kfree(bh);
        bh = NULL;
    }     
    return -1;
}

/*****************************************************************************/
static void __exit bt_hwctl_exit(void)
{
    BT_HWCTL_DEBUG("bt_hwctl_exit\n");
    
    wake_lock_destroy(&mt6622_irq_wakelock);
    
    platform_driver_unregister(&mt6622_driver);
    
    if (bh){
        cdev_del(&bh->cdev);
        
        unregister_chrdev_region(bh->dev_t, 1);
        device_destroy(bh->cls, bh->dev_t);
        
        class_destroy(bh->cls);
        mutex_destroy(&bh->sem);
        
        kfree(bh);
        bh = NULL;
    }
    
    /* release gpio used by BT */
    //mt_bt_gpio_release();
}

EXPORT_SYMBOL(mt_bt_get_platform_data);
EXPORT_SYMBOL(eint_wait);
EXPORT_SYMBOL(eint_gen);
//EXPORT_SYMBOL(mtk_wcn_bt_event_work);
//EXPORT_SYMBOL(mtk_wcn_bt_workqueue);

module_init(bt_hwctl_init);
module_exit(bt_hwctl_exit);
MODULE_AUTHOR("Tingting Lei <tingting.lei@mediatek.com>");
MODULE_DESCRIPTION("Bluetooth hardware control driver");
MODULE_LICENSE("GPL");

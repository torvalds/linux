/* drivers/misc/bp/chips/c66a.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/earlysuspend.h>

#include <linux/bp-auto.h>
	 
	 
#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif


/****************operate according to bp chip:start************/
static int bp_active(struct bp_private_data *bp, int enable)
{	
	int result = 0;
	if(enable)
	{		
		printk("<-----c66a power on-------->\n");
		gpio_set_value(bp->ops->bp_power, GPIO_HIGH);
	}
	else
	{
		printk("<-----c66a power on-------->\n");
		gpio_set_value(bp->ops->bp_power, GPIO_LOW);
		msleep(500);
	}
	
	return result;
}

static void  ap_wake_bp_work(struct work_struct *work)
{
	return;
}
static int bp_wake_ap(struct bp_private_data *bp)
{
	int result = 0;
	
	printk("<-----c66a bp_wake_ap-------->\n");
	bp->suspend_status = 0;		
	wake_lock_timeout(&bp->bp_wakelock, 10 * HZ);
	
	return result;
}
static int bp_init(struct bp_private_data *bp)
{
	int result = 0;	
	gpio_direction_input(bp->ops->bp_wakeup_ap);
	gpio_direction_output(bp->ops->bp_power,GPIO_LOW);
	gpio_direction_output(bp->ops->ap_wakeup_bp,GPIO_HIGH);
	INIT_DELAYED_WORK(&bp->wakeup_work, ap_wake_bp_work);
	return result;
}

static int bp_reset(struct bp_private_data *bp)
{
	printk("ioctrl c66a reset !!! \n");
	gpio_set_value(bp->ops->bp_power, GPIO_LOW);
	msleep(2000);
	gpio_set_value(bp->ops->bp_power, GPIO_HIGH);	
	return 0;
}
static int bp_shutdown(struct bp_private_data *bp)
{
	int result = 0;
	
	if(bp->ops->active)
		bp->ops->active(bp, 0);
	
	cancel_delayed_work_sync(&bp->wakeup_work);	
		
	return result;
}
static int bp_suspend(struct bp_private_data *bp)
{	
	int result = 0;
	bp->suspend_status = 1;
	gpio_set_value(bp->ops->ap_wakeup_bp, GPIO_LOW);		
	
	return result;
}
static int bp_resume(struct bp_private_data *bp)
{
	
	printk("<-----c66a bp_resume-------->\n");
	bp->suspend_status = 0;	
	gpio_set_value(bp->ops->ap_wakeup_bp, GPIO_HIGH);	
	
	
	return 0;
}


struct bp_operate bp_c66a_ops = {
#if defined(CONFIG_ARCH_RK2928)
	.name			= "c66a",
	.bp_id			= BP_ID_C66A,
	.bp_bus			= BP_BUS_TYPE_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= BP_UNKNOW_DATA,//RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= BP_UNKNOW_DATA,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp		= BP_UNKNOW_DATA,//RK2928_PIN3_PC4,
	.bp_wakeup_ap		= BP_UNKNOW_DATA,//RK2928_PIN3_PC3,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.bp_assert		= BP_UNKNOW_DATA,//RK2928_PIN3_PC5,
	.trig				= IRQF_TRIGGER_RISING,

	.active			= bp_active,
	.init				= bp_init,
	.reset			= bp_reset,
	.ap_wake_bp		= NULL,
	.bp_wake_ap		= bp_wake_ap,
	.shutdown		= bp_shutdown,
	.read_status		= NULL,
	.write_status		= NULL,
	.suspend 		= bp_suspend,
	.resume			= bp_resume,
	.misc_name		= NULL,
	.private_miscdev	= NULL,
#elif defined(CONFIG_ARCH_RK30)
	.name			= "c66a",
	.bp_id			= BP_ID_C66A,
	.bp_bus			= BP_BUS_TYPE_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= BP_UNKNOW_DATA,//RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= BP_UNKNOW_DATA,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp		= BP_UNKNOW_DATA,//RK2928_PIN3_PC4,
	.bp_wakeup_ap		= BP_UNKNOW_DATA,//RK2928_PIN3_PC3,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.bp_assert		= BP_UNKNOW_DATA,//RK2928_PIN3_PC5,
	.trig				= IRQF_TRIGGER_RISING,

	.active			= bp_active,
	.init				= bp_init,
	.reset			= bp_reset,
	.ap_wake_bp		= NULL,
	.bp_wake_ap		= bp_wake_ap,
	.shutdown		= bp_shutdown,
	.read_status		= NULL,
	.write_status		= NULL,
	.suspend 		= bp_suspend,
	.resume			= bp_resume,
	.misc_name		= NULL,
	.private_miscdev	= NULL,
#else
	.name			= "c66a",
	.bp_id			= BP_ID_C66A,
	.bp_bus			= BP_BUS_TYPE_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= BP_UNKNOW_DATA,//RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= BP_UNKNOW_DATA,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp		= BP_UNKNOW_DATA,//RK2928_PIN3_PC4,
	.bp_wakeup_ap		= BP_UNKNOW_DATA,//RK2928_PIN3_PC3,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.bp_assert		= BP_UNKNOW_DATA,//RK2928_PIN3_PC5,
	.trig				= IRQF_TRIGGER_RISING,

	.active			= bp_active,
	.init				= bp_init,
	.reset			= bp_reset,
	.ap_wake_bp		= NULL,
	.bp_wake_ap		= bp_wake_ap,
	.shutdown		= bp_shutdown,
	.read_status		= NULL,
	.write_status		= NULL,
	.suspend 		= bp_suspend,
	.resume			= bp_resume,
	.misc_name		= NULL,
	.private_miscdev	= NULL,
#endif


};

/****************operate according to bp chip:end************/

//function name should not be changed
static struct bp_operate *bp_get_ops(void)
{
	return &bp_c66a_ops;
}

static int __init bp_c66a_init(void)
{
	struct bp_operate *ops = bp_get_ops();
	int result = 0;
	result = bp_register_slave(NULL, NULL, bp_get_ops);
	if(result)
	{	
		return result;
	}
	
	if(ops->private_miscdev)
	{
		result = misc_register(ops->private_miscdev);
		if (result < 0) {
			printk("%s:misc_register err\n",__func__);
			return result;
		}
	}
	
	DBG("%s\n",__func__);
	return result;
}

static void __exit bp_c66a_exit(void)
{
	//struct bp_operate *ops = bp_get_ops();
	bp_unregister_slave(NULL, NULL, bp_get_ops);
}


subsys_initcall(bp_c66a_init);
module_exit(bp_c66a_exit);


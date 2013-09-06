/* drivers/misc/bp/chips/E1230S.c
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
	  
/****************operate according to bp chip:start************/
static int bp_active(struct bp_private_data *bp, int enable)
{		
	return 0;
}

static int ap_wake_bp(struct bp_private_data *bp, int wake)
{
	printk("<-----E1230S ap_wake_bp-------->\n");
	gpio_set_value(bp->ops->ap_wakeup_bp, wake);  
	
	return 0;

}

static void  ap_wake_bp_work(struct work_struct *work)
{
	struct delayed_work *wakeup_work = container_of(work, struct delayed_work, work);
	struct bp_private_data *bp = container_of(wakeup_work, struct bp_private_data, wakeup_work);
	if(bp->suspend_status)
	{
		bp->suspend_status = 0;
		if(bp->ops->ap_wake_bp)
		bp->ops->ap_wake_bp(bp, 0);
	}
}


static int bp_init(struct bp_private_data *bp)
{
	printk("<-----E1230S bp_init-------->\n");
	gpio_direction_output(bp->ops->bp_power, GPIO_HIGH);
	gpio_set_value(bp->ops->bp_power, GPIO_HIGH);
	msleep(500);
	bp->suspend_status = 0;
	gpio_direction_input(bp->ops->bp_wakeup_ap);
	gpio_pull_updown(bp->ops->bp_wakeup_ap, 1);	
	gpio_direction_output(bp->ops->bp_en, GPIO_HIGH);
	gpio_direction_output(bp->ops->ap_wakeup_bp, GPIO_LOW);
	INIT_DELAYED_WORK(&bp->wakeup_work, ap_wake_bp_work);
	return 0;
}

static int bp_reset(struct bp_private_data *bp)
{
	gpio_set_value(bp->ops->bp_en, GPIO_LOW);
	gpio_set_value(bp->ops->bp_power, GPIO_LOW);
	msleep(3000);
	gpio_set_value(bp->ops->bp_power, GPIO_HIGH);
	gpio_set_value(bp->ops->bp_en, GPIO_HIGH);	

	return 0;
}

static int bp_wake_ap(struct bp_private_data *bp)
{
	printk("<-----E1230S bp_wake_ap-------->\n");
	
	bp->suspend_status = 1;
	schedule_delayed_work(&bp->wakeup_work, 2*HZ);
	wake_lock_timeout(&bp->bp_wakelock, 10* HZ);	
	
	return 0;
}

static int bp_shutdown(struct bp_private_data *bp)
{
	int result = 0;
	
	if(bp->ops->active)
		bp->ops->active(bp, 0);
	gpio_set_value(bp->ops->bp_power, GPIO_LOW);
	cancel_delayed_work_sync(&bp->wakeup_work);	

	return result;
}

static int bp_suspend(struct bp_private_data *bp)
{	

	gpio_set_value(bp->ops->ap_wakeup_bp, GPIO_HIGH);	
	
	return 0;
}

static int bp_resume(struct bp_private_data *bp)
{	
	if(!bp->suspend_status){
	gpio_set_value(bp->ops->ap_wakeup_bp, GPIO_LOW);	
	}
	return 0;
}


struct bp_operate bp_E1230S_ops = {
#if defined(CONFIG_ARCH_RK2928)
	.name			= "E1230S",
	.bp_id			= BP_ID_E1230S,
	.bp_bus			= BP_BUS_TYPE_USB_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= RK2928_PIN1_PA3,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp		= RK2928_PIN3_PC4,
	.bp_wakeup_ap		= RK2928_PIN3_PC3,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.trig			= IRQF_TRIGGER_FALLING,

	.active			= bp_active,
	.init			= bp_init,
	.reset			= bp_reset,
	.ap_wake_bp		= ap_wake_bp,
	.bp_wake_ap		= bp_wake_ap,
	.shutdown		= bp_shutdown,
	.read_status		= NULL,
	.write_status		= NULL,
	.suspend 		= bp_suspend,
	.resume			= bp_resume,
	.misc_name		= NULL,
	.private_miscdev	= NULL,
#elif (defined(CONFIG_SOC_RK3168) || defined(CONFIG_SOC_RK3188))
	.name			= "E1230S",
	.bp_id			= BP_ID_E1230S,
	.bp_bus			= BP_BUS_TYPE_USB_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= RK30_PIN0_PC6,// 3g_power
	.bp_en			= RK30_PIN2_PD5,// 3g_en
	.bp_reset			= RK30_PIN2_PD4,//BP_UNKNOW_DATA,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp		= RK30_PIN0_PC4,
	.bp_wakeup_ap		= RK30_PIN0_PC5,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.trig			= IRQF_TRIGGER_FALLING,

	.active			= bp_active,
	.init			= bp_init,
	.reset			= bp_reset,
	.ap_wake_bp		= ap_wake_bp,
	.bp_wake_ap		= bp_wake_ap,
	.shutdown		= bp_shutdown,
	.read_status		= NULL,
	.write_status		= NULL,
	.suspend 		= bp_suspend,
	.resume			= bp_resume,
	.misc_name		= NULL,
	.private_miscdev	= NULL,
#else
	.name			= "E1230S",
	.bp_id			= BP_ID_E1230S,
	.bp_bus			= BP_BUS_TYPE_USB_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= BP_UNKNOW_DATA,//RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= BP_UNKNOW_DATA,//RK2928_PIN1_PA3,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp		= BP_UNKNOW_DATA,//RK2928_PIN3_PC4,
	.bp_wakeup_ap		= BP_UNKNOW_DATA,//RK2928_PIN3_PC3,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.trig			= IRQF_TRIGGER_FALLING,

	.active			= bp_active,
	.init			= bp_init,
	.reset			= bp_reset,
	.ap_wake_bp		= ap_wake_bp,
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
	return &bp_E1230S_ops;
}

static int __init bp_E1230S_init(void)
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
	return result;
}

static void __exit bp_E1230S_exit(void)
{
	//struct bp_operate *ops = bp_get_ops();
	bp_unregister_slave(NULL, NULL, bp_get_ops);
}


subsys_initcall(bp_E1230S_init);
module_exit(bp_E1230S_exit);


/* drivers/misc/bp/chips/aw706.c
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
	switch(enable){
		case 0:
			printk("<-----aw706 power off-------->\n");
			//result = gpio_get_value(bp->ops->bp_assert);
			//if(!result){ 
			//printk("<-----aw706 is poweroff-------->\n");
			//return result;}
			gpio_set_value(bp->ops->bp_en, GPIO_LOW);
			msleep(2500);
			gpio_set_value(bp->ops->bp_en, GPIO_HIGH);		
			gpio_set_value(bp->ops->bp_power, GPIO_LOW);	
			break;
		case 1:
			printk("<-----aw706 power on-------->\n");	
			//result = gpio_get_value(bp->ops->bp_assert);
			//if(result){
			//	printk("<-----aw706 power bp_assert is high no need to power on again-------->\n");
			//	return result;
			//}
        	gpio_set_value(bp->ops->bp_power, GPIO_HIGH);
			mdelay(100);
			gpio_set_value(bp->ops->bp_en,GPIO_LOW);
			gpio_set_value(bp->ops->ap_wakeup_bp,GPIO_HIGH);	
        	mdelay(1000);		
			gpio_set_value(bp->ops->ap_wakeup_bp,GPIO_LOW);
			gpio_set_value(bp->ops->bp_en,GPIO_HIGH);
			break;
		case 2:
			printk("<-----aw706 udate power_en low-------->\n");
			gpio_set_value(bp->ops->bp_power, GPIO_HIGH);
			mdelay(50);
			gpio_set_value(bp->ops->bp_en,GPIO_LOW);
			gpio_set_value(bp->ops->ap_wakeup_bp,GPIO_HIGH);
			break;
		case 3:
			printk("<-----aw706 udate power_en high-------->\n");
			gpio_set_value(bp->ops->bp_en,GPIO_HIGH);
			gpio_set_value(bp->ops->ap_wakeup_bp,GPIO_LOW);
			break;
		default:
			break;
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
	
	if(bp->suspend_status)
	{
		//iomux_set(UART1_RTSN);
		//gpio_direction_output(RK30_PIN1_PA6, GPIO_LOW);
		printk("bp_wake_ap aw706 done!!! \n");	
		
		bp->suspend_status = 0;		
		wake_lock_timeout(&bp->bp_wakelock, 10 * HZ);
	}
	
	return result;
}
static int bp_init(struct bp_private_data *bp)
{
	int result = 0;
	
	gpio_direction_output(bp->ops->bp_power, GPIO_LOW);
	//gpio_direction_output(bp->ops->bp_reset, GPIO_LOW);
	gpio_direction_output(bp->ops->bp_en, GPIO_HIGH);
	//gpio_direction_output(bp->ops->ap_ready, GPIO_LOW);
	gpio_direction_output(bp->ops->ap_wakeup_bp, GPIO_LOW);   
	//gpio_direction_input(bp->ops->bp_ready);	
	gpio_direction_input(bp->ops->bp_wakeup_ap);
	gpio_direction_input(bp->ops->bp_assert);
	gpio_pull_updown(bp->ops->bp_wakeup_ap, 1);	
	
	//if(bp->ops->active)
		//bp->ops->active(bp, 1);	
	INIT_DELAYED_WORK(&bp->wakeup_work, ap_wake_bp_work);
	return result;
}

static int bp_reset(struct bp_private_data *bp)
{
	printk("ioctrl aw706 reset !!! \n");	
	return 0;
	gpio_set_value(bp->ops->bp_en,GPIO_LOW);
	mdelay(6000);
	gpio_set_value(bp->ops->bp_en,GPIO_HIGH);
	
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
	int ret = 0;
	printk("aw706 %s !!! \n",__func__);
	bp->suspend_status = 1;
	//gpio_set_value(bp->ops->ap_ready, GPIO_HIGH);
	gpio_set_value(bp->ops->ap_wakeup_bp, GPIO_HIGH);
	/*
	iomux_set(GPIO1_A7);
	ret = gpio_request(RK30_PIN1_PA7,"bp_auto");
	if(ret < 0){
		printk("%s rk30_pin1_pa7 request failed\n",__func__);
		return result;
	}	
	gpio_direction_output(RK30_PIN1_PA7, GPIO_HIGH);
	*/
	return result;
}
static int bp_resume(struct bp_private_data *bp)
{
	
	bp->suspend_status = 0;	
		
	//iomux_set(UART1_RTSN);		
	//gpio_direction_output(RK30_PIN1_PA7, GPIO_LOW);
	gpio_set_value(bp->ops->ap_wakeup_bp, GPIO_LOW);
	return 0;
}


struct bp_operate bp_aw706_ops = {
#if defined(CONFIG_ARCH_RK2928)
	.name			= "aw706",
	.bp_id			= BP_ID_AW706,
	.bp_bus			= BP_BUS_TYPE_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= RK2928_PIN3_PC5,//BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= RK2928_PIN0_PB6,
	.ap_ready		= RK2928_PIN0_PD0,	//
	.bp_ready		= RK2928_PIN0_PD6,
	.ap_wakeup_bp	= RK2928_PIN3_PC4,
	.bp_wakeup_ap	= RK2928_PIN3_PC3,	//
	.bp_assert		= BP_UNKNOW_DATA,
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.trig				= IRQF_TRIGGER_FALLING,

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
#elif defined(CONFIG_SOC_RK3028)
	.name			= "aw706",
	.bp_id			= BP_ID_AW706,
	.bp_bus			= BP_BUS_TYPE_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= RK30_PIN0_PC2,//BP_UNKNOW_DATA,// 3g_power
#if defined(CONFIG_MACH_RK3028_PHONEPAD_780)
	.bp_en			= RK30_PIN0_PC5,	// RK2928_PIN3_PC5,//BP_UNKNOW_DATA,	// 3g_en
#else
	.bp_en			= RK30_PIN3_PD0,	// RK2928_PIN3_PC5,//BP_UNKNOW_DATA,	// 3g_en
#endif
	.bp_reset		= BP_UNKNOW_DATA,//BP_UNKNOW_DATA,	// RK2928_PIN0_PB6,
	.ap_ready		= BP_UNKNOW_DATA,//RK30_PIN1_PB5,//BP_UNKNOW_DATA,	// RK2928_PIN0_PD0,	//
	.bp_ready		= BP_UNKNOW_DATA,//RK30_PIN3_PD0,//BP_UNKNOW_DATA,	// RK2928_PIN0_PD6,
	.ap_wakeup_bp	= RK30_PIN3_PC6,//BP_UNKNOW_DATA,	// RK2928_PIN3_PC4,
	.bp_wakeup_ap	= RK30_PIN0_PC1,//BP_UNKNOW_DATA,	// RK2928_PIN3_PC3,	//
	.bp_assert		= RK30_PIN0_PC0,
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.trig				= IRQF_TRIGGER_FALLING,

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
	.name			= "aw706",
	.bp_id			= BP_ID_AW706,
	.bp_bus			= BP_BUS_TYPE_UART,		
	.bp_pid			= 0,	
	.bp_vid			= 0,	
	.bp_power		= BP_UNKNOW_DATA,	// RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// RK2928_PIN3_PC5,//BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= BP_UNKNOW_DATA,	// RK2928_PIN0_PB6,
	.ap_ready		= BP_UNKNOW_DATA,	// RK2928_PIN0_PD0,	//
	.bp_ready		= BP_UNKNOW_DATA,	// RK2928_PIN0_PD6,
	.ap_wakeup_bp	= BP_UNKNOW_DATA,	// RK2928_PIN3_PC4,
	.bp_wakeup_ap	= BP_UNKNOW_DATA,	// RK2928_PIN3_PC3,	//
	.bp_assert		= BP_UNKNOW_DATA,
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.trig				= IRQF_TRIGGER_FALLING,

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
	return &bp_aw706_ops;
}

static int __init bp_aw706_init(void)
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

static void __exit bp_aw706_exit(void)
{
	//struct bp_operate *ops = bp_get_ops();
	bp_unregister_slave(NULL, NULL, bp_get_ops);
}


subsys_initcall(bp_aw706_init);
module_exit(bp_aw706_exit);


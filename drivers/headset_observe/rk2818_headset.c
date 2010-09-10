/* arch/arm/mach-rockchip/rk28_headset.c
 *
 * Copyright (C) 2009 Rockchip Corporation.
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
#include <linux/sysdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/switch.h>
#include <linux/input.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <asm/gpio.h>
#include <asm/atomic.h>
#include <asm/mach-types.h>
#include "rk2818_headset.h"

/* Debug */
#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

/* 耳机状态 */
#define BIT_HEADSET             (1 << 0)//带MIC的耳机
#define BIT_HEADSET_NO_MIC      (1 << 1)//不带MIC的耳机

struct rk2818_headset_dev{
	struct switch_dev sdev;
	int cur_headset_status; 
	int pre_headset_status; 
	struct mutex mutex_lock;
};

static struct rk2818_headset_dev Headset_dev;
static struct work_struct g_headsetobserve_work;
static struct rk2818_headset_data *prk2818_headset_info;
static irqreturn_t headset_interrupt(int irq, void *dev_id);
unsigned int headset_irq_type;

static void headsetobserve_work(void)
{
	if(gpio_get_value(prk2818_headset_info->irq)){
		if(prk2818_headset_info->headset_in_type)
			{DBG("headset in-----cjq------/n");Headset_dev.cur_headset_status = BIT_HEADSET;}
		else
			Headset_dev.cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);

		if(headset_irq_type != (IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING))
			headset_irq_type = IRQF_TRIGGER_FALLING;
	}
	else{
		if(prk2818_headset_info->headset_in_type)
			{DBG("headset out-----cjq------/n");Headset_dev.cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);}
		else
			Headset_dev.cur_headset_status = BIT_HEADSET;

		if(headset_irq_type != (IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING))
			headset_irq_type = IRQF_TRIGGER_RISING;	
	}

	if(Headset_dev.cur_headset_status != Headset_dev.pre_headset_status)
	{
		Headset_dev.pre_headset_status = Headset_dev.cur_headset_status;
		mutex_lock(&Headset_dev.mutex_lock);
		switch_set_state(&Headset_dev.sdev, Headset_dev.cur_headset_status);
		mutex_unlock(&Headset_dev.mutex_lock);
		DBG("---------------cur_headset_status = [0x%x]\n", Headset_dev.cur_headset_status);
	}

	if(headset_irq_type == (IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING))return;

	free_irq(prk2818_headset_info->irq,NULL);
	if (request_irq(prk2818_headset_info->irq, headset_interrupt, headset_irq_type, NULL, NULL))
		DBG("headsetobserve: request irq failed\n");
}

static irqreturn_t headset_interrupt(int irq, void *dev_id)
{
	schedule_work(&g_headsetobserve_work);
	DBG("---------------headset_interrupt---------------\n");
	
	return IRQ_HANDLED;
}

static ssize_t h2w_print_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "Headset\n");
}

static int rockchip_headsetobserve_probe(struct platform_device *pdev)
{
	int ret;
	
	DBG("RockChip headset observe driver\n");
	prk2818_headset_info = pdev->dev.platform_data;
	
	Headset_dev.cur_headset_status = 0;
	Headset_dev.pre_headset_status = 0;
	Headset_dev.sdev.name = "h2w";
	Headset_dev.sdev.print_name = h2w_print_name;
	mutex_init(&Headset_dev.mutex_lock);

	ret = switch_dev_register(&Headset_dev.sdev);
	if (ret < 0)
		return 0;

	INIT_WORK(&g_headsetobserve_work, headsetobserve_work);
    
	ret = gpio_request(prk2818_headset_info->irq, "headset_det");
	if (ret) {
		DBG( "headsetobserve: failed to request FPGA_PIO0_00\n");
		return ret;
	}

	gpio_direction_input(prk2818_headset_info->irq);

	prk2818_headset_info->irq = gpio_to_irq(prk2818_headset_info->irq);
	headset_irq_type = prk2818_headset_info->irq_type;
	ret = request_irq(prk2818_headset_info->irq, headset_interrupt, headset_irq_type, NULL, NULL);
	if (ret ) {
		DBG("headsetobserve: request irq failed\n");
        	return ret;
	}
	headsetobserve_work();

	return 0;	
}

static struct platform_driver rockchip_headsetobserve_driver = {
	.probe	= rockchip_headsetobserve_probe,
	.driver	= {
		.name	= "rk2818_headsetdet",
		.owner	= THIS_MODULE,
	},
};


static int __init rockchip_headsetobserve_init(void)
{
	platform_driver_register(&rockchip_headsetobserve_driver);
	return 0;
}
module_init(rockchip_headsetobserve_init);
MODULE_DESCRIPTION("Rockchip Headset Driver");
MODULE_LICENSE("GPL");

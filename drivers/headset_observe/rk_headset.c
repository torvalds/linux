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
#include "rk_headset.h"
#include <linux/earlysuspend.h>

/* Debug */
#if 1
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif

#define BIT_HEADSET             (1 << 0)
#define BIT_HEADSET_NO_MIC      (1 << 1)

struct rk2818_headset_dev{
	struct switch_dev sdev;
	int cur_headset_status; 
	int pre_headset_status; 
	struct mutex mutex_lock;
};

static struct rk2818_headset_dev Headset_dev;
static struct delayed_work g_headsetobserve_work;
static struct rk2818_headset_data *prk2818_headset_info;
#if defined(CONFIG_MACH_BENGO_V2) || defined(CONFIG_MACH_BENGO) || defined(CONFIG_MACH_Z5) || defined(CONFIG_MACH_Z5_V2) || defined(CONFIG_MACH_A22)
extern void detect_HSMic(void);
#endif

int headset_status(void)
{	    
	if(Headset_dev.cur_headset_status & BIT_HEADSET)		        
		return 1;
	else
		return 0;
}

EXPORT_SYMBOL_GPL(headset_status);

static irqreturn_t headset_interrupt(int irq, void *dev_id)
{
//	DBG("---headset_interrupt---\n");	
	schedule_delayed_work(&g_headsetobserve_work, msecs_to_jiffies(20));
	return IRQ_HANDLED;
}

static int headset_change_irqtype(unsigned int irq_type)
{
	int ret = 0;
//	DBG("--------%s----------\n",__FUNCTION__);
	free_irq(prk2818_headset_info->irq,NULL);
	
	ret = request_irq(prk2818_headset_info->irq, headset_interrupt, irq_type, NULL, NULL);
	if (ret) 
	{
		DBG("headsetobserve: request irq failed\n");
        return ret;
	}
	return ret;
}

static void headsetobserve_work(struct work_struct *work)
{
	int i,level = 0;

//	DBG("---headsetobserve_work---\n");
	mutex_lock(&Headset_dev.mutex_lock);

	for(i=0; i<3; i++)
	{
		level = gpio_get_value(prk2818_headset_info->gpio);
		if(level < 0)
		{
			printk("%s:get pin level again,pin=%d,i=%d\n",__FUNCTION__,prk2818_headset_info->gpio,i);
			msleep(1);
			continue;
		}
		else
		break;
	}
	if(level < 0)
	{
		printk("%s:get pin level  err!\n",__FUNCTION__);
		return;
	}
	
	switch(prk2818_headset_info->headset_in_type)
	{
		case HEADSET_IN_HIGH:
			if(level > 0)
			{//in--High level
				DBG("--- HEADSET_IN_HIGH headset in---\n");
				Headset_dev.cur_headset_status = BIT_HEADSET;
				headset_change_irqtype(IRQF_TRIGGER_FALLING);//
			}
			else if(level == 0)
			{//out--Low level
				DBG("---HEADSET_IN_HIGH headset out---\n");		
				Headset_dev.cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);
				headset_change_irqtype(IRQF_TRIGGER_RISING);//
			}
			break;
		case HEADSET_IN_LOW:
			if(level == 0)
			{//in--High level
				DBG("---HEADSET_IN_LOW headset in---\n");
				Headset_dev.cur_headset_status = BIT_HEADSET;
				headset_change_irqtype(IRQF_TRIGGER_RISING);//
			}
			else if(level > 0)
			{//out--High level
				DBG("---HEADSET_IN_LOW headset out---\n");		
				Headset_dev.cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);
				headset_change_irqtype(IRQF_TRIGGER_FALLING);//
			}
			break;			
		default:
			DBG("---- ERROR: on headset headset_in_type error -----\n");
			break;			
	}

	if(Headset_dev.cur_headset_status != Headset_dev.pre_headset_status)
	{
		Headset_dev.pre_headset_status = Headset_dev.cur_headset_status;					
		switch_set_state(&Headset_dev.sdev, Headset_dev.cur_headset_status);	
		DBG("Headset_dev.cur_headset_status = %d\n",Headset_dev.cur_headset_status);
#if defined(CONFIG_MACH_BENGO_V2) || defined(CONFIG_MACH_BENGO) || defined(CONFIG_MACH_Z5) || defined(CONFIG_MACH_Z5_V2) || defined(CONFIG_MACH_A22)
		detect_HSMic();
#endif
	}
	mutex_unlock(&Headset_dev.mutex_lock);	
}

static ssize_t h2w_print_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "Headset\n");
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void headset_early_resume(struct early_suspend *h)
{
	schedule_delayed_work(&g_headsetobserve_work, msecs_to_jiffies(10));
	//DBG(">>>>>headset_early_resume\n");
}

static struct early_suspend hs_early_suspend;
#endif

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
		return ret;

	INIT_DELAYED_WORK(&g_headsetobserve_work, headsetobserve_work);
    
	ret = gpio_request(prk2818_headset_info->gpio, "headset_det");
	if (ret) 
	{
		DBG("headsetobserve: request gpio_request failed\n");
		return ret;
	}
	gpio_pull_updown(prk2818_headset_info->gpio, GPIONormal);
	gpio_direction_input(prk2818_headset_info->gpio);
	prk2818_headset_info->irq = gpio_to_irq(prk2818_headset_info->gpio);

	if(prk2818_headset_info->headset_in_type == HEADSET_IN_HIGH)
		prk2818_headset_info->irq_type = IRQF_TRIGGER_RISING;
	else
		prk2818_headset_info->irq_type = IRQF_TRIGGER_FALLING;
	ret = request_irq(prk2818_headset_info->irq, headset_interrupt, prk2818_headset_info->irq_type, NULL, NULL);
	if (ret) 
	{
		DBG("headsetobserve: request irq failed\n");
        return ret;
	}

	schedule_delayed_work(&g_headsetobserve_work, msecs_to_jiffies(500));
	
#ifdef CONFIG_HAS_EARLYSUSPEND
    hs_early_suspend.suspend = NULL;
    hs_early_suspend.resume = headset_early_resume;
    hs_early_suspend.level = ~0x0;
    register_early_suspend(&hs_early_suspend);
#endif

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

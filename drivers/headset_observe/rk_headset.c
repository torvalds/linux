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
#include <linux/gpio.h>
#include <mach/board.h>
#include <linux/slab.h>

/* Debug */
#if 0
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif

#define BIT_HEADSET             (1 << 0)
#define BIT_HEADSET_NO_MIC      (1 << 1)

#define HEADSET 0
#define HOOK 1

#define HEADSET_IN 1
#define HEADSET_OUT 0
#define HOOK_DOWN 0
#define HOOK_UP 1
#define enable 1
#define disable 0

#ifdef CONFIG_SND_RK_SOC_RK2928
extern void rk2928_codec_set_spk(bool on);
#endif
#ifdef CONFIG_SND_SOC_WM8994
extern int wm8994_set_status(void);
#endif

/* headset private data */
struct headset_priv {
	struct input_dev *input_dev;
	struct rk_headset_pdata *pdata;
	unsigned int headset_status:1;
	unsigned int hook_status:1;
	unsigned int isMic:1;
	unsigned int isHook_irq:1;
	int cur_headset_status; 
	
	unsigned int irq[2];
	unsigned int irq_type[2];
	struct delayed_work h_delayed_work[2];
	struct switch_dev sdev;
	struct mutex mutex_lock[2];	
	struct timer_list headset_timer;
	unsigned char *keycodes;
};
static struct headset_priv *headset_info;

int Headset_isMic(void)
{
	return headset_info->isMic;
}
EXPORT_SYMBOL_GPL(Headset_isMic);

int Headset_status(void)
{
	if(headset_info->cur_headset_status == BIT_HEADSET_NO_MIC ||
		headset_info->cur_headset_status == BIT_HEADSET )
		return HEADSET_IN;
	else
		return HEADSET_OUT;
}
EXPORT_SYMBOL_GPL(Headset_status);

static irqreturn_t headset_interrupt(int irq, void *dev_id)
{
	DBG("---headset_interrupt---\n");	
	schedule_delayed_work(&headset_info->h_delayed_work[HEADSET], msecs_to_jiffies(50));
	return IRQ_HANDLED;
}

static irqreturn_t Hook_interrupt(int irq, void *dev_id)
{
	DBG("---Hook_interrupt---\n");	
//	disable_irq_nosync(headset_info->irq[HOOK]);
	schedule_delayed_work(&headset_info->h_delayed_work[HOOK], msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static int headset_change_irqtype(int type,unsigned int irq_type)
{
	int ret = 0;
	DBG("--------%s----------\n",__FUNCTION__);
	free_irq(headset_info->irq[type],NULL);
	
	switch(type)
	{
	case HOOK:
		ret = request_irq(headset_info->irq[type], Hook_interrupt, irq_type, "headset_input", NULL);
		break;
	case HEADSET:
		ret = request_irq(headset_info->irq[type], headset_interrupt, irq_type, "headset_hook", NULL);
		break;
	default:
		ret = -1;
		break;
	}

	if (ret<0) 
	{
		DBG("headset_change_irqtype: request irq failed\n");
        return ret;
	}
	return ret;
}

static void headsetobserve_work(struct work_struct *work)
{
	int i,level = 0;
	struct rk_headset_pdata *pdata = headset_info->pdata;
	static unsigned int old_status = 0;
	DBG("---headsetobserve_work---\n");
	mutex_lock(&headset_info->mutex_lock[HEADSET]);

	for(i=0; i<3; i++)
	{
		level = gpio_get_value(pdata->Headset_gpio);
		if(level < 0)
		{
			printk("%s:get pin level again,pin=%d,i=%d\n",__FUNCTION__,pdata->Headset_gpio,i);
			msleep(1);
			continue;
		}
		else
		break;
	}
	if(level < 0)
	{
		printk("%s:get pin level  err!\n",__FUNCTION__);
		goto RE_ERROR;
	}

	old_status = headset_info->headset_status;
	switch(pdata->headset_in_type)
	{
	case HEADSET_IN_HIGH:
		if(level > 0)
			headset_info->headset_status = HEADSET_IN;
		else if(level == 0)
			headset_info->headset_status = HEADSET_OUT;	
		break;
	case HEADSET_IN_LOW:
		if(level == 0)
			headset_info->headset_status = HEADSET_IN;
		else if(level > 0)
			headset_info->headset_status = HEADSET_OUT;		
		break;			
	default:
		DBG("---- ERROR: on headset headset_in_type error -----\n");
		break;			
	}
	if(old_status == headset_info->headset_status)
	{
		DBG("old_status == headset_info->headset_status\n");
		goto RE_ERROR;
	}

	switch(pdata->headset_in_type)
	{
	case HEADSET_IN_HIGH:
		if(level > 0)
		{//in--High level
			DBG("--- HEADSET_IN_HIGH headset in HIGH---\n");
			headset_info->cur_headset_status = BIT_HEADSET_NO_MIC;
			headset_change_irqtype(HEADSET,IRQF_TRIGGER_FALLING);//
			if (pdata->Hook_gpio) {
				del_timer(&headset_info->headset_timer);//Start the timer, wait for switch to the headphone channel
			//	headset_info->headset_timer.expires = jiffies + 500;
				headset_info->headset_timer.expires = jiffies + 10;
				add_timer(&headset_info->headset_timer);
			}
		}
		else if(level == 0)
		{//out--Low level
			DBG("---HEADSET_IN_HIGH headset out HIGH---\n");	
			if(headset_info->isHook_irq == enable)
			{
				DBG("disable headset_hook irq\n");
				headset_info->isHook_irq = disable;
				disable_irq(headset_info->irq[HOOK]);		
			}	
			headset_info->cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);
	#ifdef CONFIG_SND_RK_SOC_RK2928
	rk2928_codec_set_spk(HEADSET_OUT);
	#endif						
			headset_change_irqtype(HEADSET,IRQF_TRIGGER_RISING);//
			rk28_send_wakeup_key();
			switch_set_state(&headset_info->sdev, headset_info->cur_headset_status);	
			DBG("headset_info->cur_headset_status = %d\n",headset_info->cur_headset_status);			
		}
		break;
	case HEADSET_IN_LOW:
		if(level == 0)
		{//in--High level
			DBG("---HEADSET_IN_LOW headset in LOW ---\n");
			headset_info->cur_headset_status = BIT_HEADSET_NO_MIC;
			headset_change_irqtype(HEADSET,IRQF_TRIGGER_RISING);//
			if (pdata->Hook_gpio) {			
				del_timer(&headset_info->headset_timer);//Start the timer, wait for switch to the headphone channel
			//	headset_info->headset_timer.expires = jiffies + 500;
				headset_info->headset_timer.expires = jiffies + 10;
				add_timer(&headset_info->headset_timer);
			}
		}
		else if(level > 0)
		{//out--High level
			DBG("---HEADSET_IN_LOW headset out LOW ---\n");
			if(headset_info->isHook_irq == enable)
			{
				DBG("disable headset_hook irq\n");
				headset_info->isHook_irq = disable;
				disable_irq(headset_info->irq[HOOK]);		
			}
	#ifdef CONFIG_SND_RK_SOC_RK2928
	rk2928_codec_set_spk(HEADSET_OUT);
	#endif		
			headset_info->cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);
			headset_change_irqtype(HEADSET,IRQF_TRIGGER_FALLING);//
			rk28_send_wakeup_key();
			switch_set_state(&headset_info->sdev, headset_info->cur_headset_status);	
			DBG("headset_info->cur_headset_status = %d\n",headset_info->cur_headset_status);			
		}
		break;			
	default:
		DBG("---- ERROR: on headset headset_in_type error -----\n");
		break;			
	}
	

RE_ERROR:
	mutex_unlock(&headset_info->mutex_lock[HEADSET]);	
}

static void Hook_work(struct work_struct *work)
{
	int i,level = 0;
	struct rk_headset_pdata *pdata = headset_info->pdata;
	static unsigned int old_status = HOOK_UP;

	DBG("---Hook_work---\n");
	mutex_lock(&headset_info->mutex_lock[HOOK]);
	if(headset_info->headset_status == HEADSET_OUT)
	{
		DBG("Headset is out\n");
		goto RE_ERROR;
	}	
	#ifdef CONFIG_SND_SOC_WM8994
	if(wm8994_set_status() != 0)
	{
		DBG("wm8994 is not set on heatset channel or suspend\n");
		goto RE_ERROR;
	}
	#endif
	for(i=0; i<3; i++)
	{
		level = gpio_get_value(pdata->Hook_gpio);
		if(level < 0)
		{
			printk("%s:get pin level again,pin=%d,i=%d\n",__FUNCTION__,pdata->Hook_gpio,i);
			msleep(1);
			continue;
		}
		else
		break;
	}
	if(level < 0)
	{
		printk("%s:get pin level  err!\n",__FUNCTION__);
		goto RE_ERROR;
	}
	
	old_status = headset_info->hook_status;
	if(level == 0)
		headset_info->hook_status = HOOK_UP;
	else if(level > 0)	
		headset_info->hook_status = HOOK_DOWN;
	if(old_status == headset_info->hook_status)
	{
		DBG("old_status == headset_info->hook_status\n");
		goto RE_ERROR;
	}	
	
	if(level == 0)
	{
		DBG("---HOOK Down ---\n");
		headset_change_irqtype(HOOK,IRQF_TRIGGER_RISING);//
		input_report_key(headset_info->input_dev,pdata->hook_key_code,headset_info->hook_status);
		input_sync(headset_info->input_dev);
	}
	else if(level > 0)
	{
		DBG("---HOOK Up ---\n");		
		headset_change_irqtype(HOOK,IRQF_TRIGGER_FALLING);//
		input_report_key(headset_info->input_dev,pdata->hook_key_code,headset_info->hook_status);
		input_sync(headset_info->input_dev);
	}
RE_ERROR:
	mutex_unlock(&headset_info->mutex_lock[HOOK]);
}

static void headset_timer_callback(unsigned long arg)
{
	struct headset_priv *headset = (struct headset_priv *)(arg);
	struct rk_headset_pdata *pdata = headset->pdata;
	int i,level = 0;
	
//	printk("headset_timer_callback,headset->headset_status=%d\n",headset->headset_status);	

	if(headset->headset_status == HEADSET_OUT)
	{
		printk("Headset is out\n");
		goto out;
	}
	#ifdef CONFIG_SND_SOC_WM8994
	if(wm8994_set_status() != 0)
	{
	//	printk("wait wm8994 set the MICB2\n");
	//	headset_info->headset_timer.expires = jiffies + 500;
		headset_info->headset_timer.expires = jiffies + 10;
		add_timer(&headset_info->headset_timer);	
		goto out;
	}
	#endif
	for(i=0; i<3; i++)
	{
		level = gpio_get_value(pdata->Hook_gpio);
		if(level < 0)
		{
			printk("%s:get pin level again,pin=%d,i=%d\n",__FUNCTION__,pdata->Hook_gpio,i);
			msleep(1);
			continue;
		}
		else
		break;
	}
	if(level < 0)
	{
		printk("%s:get pin level  err!\n",__FUNCTION__);
		goto out;
	}

	if(level == 0)
	{
		headset->isMic= 0;//No microphone
		headset_info->cur_headset_status = BIT_HEADSET_NO_MIC;
		printk("headset->isMic = %d\n",headset->isMic);		
	}	
	else if(level > 0)	
	{	
		headset->isMic = 1;//have mic
		DBG("enable headset_hook irq\n");
		enable_irq(headset_info->irq[HOOK]);
		headset->isHook_irq = enable;
		headset_info->cur_headset_status = BIT_HEADSET;	
		printk("headset->isMic = %d\n",headset->isMic);		
	}
	#ifdef CONFIG_SND_RK_SOC_RK2928
	rk2928_codec_set_spk(HEADSET_IN);
	#endif
	rk28_send_wakeup_key();
	switch_set_state(&headset_info->sdev, headset_info->cur_headset_status);	
	DBG("headset_info->cur_headset_status = %d\n",headset_info->cur_headset_status);	

out:
	return;
}

static ssize_t h2w_print_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "Headset\n");
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void headset_early_resume(struct early_suspend *h)
{
	schedule_delayed_work(&headset_info->h_delayed_work[HEADSET], msecs_to_jiffies(10));
	//DBG(">>>>>headset_early_resume\n");
}

static struct early_suspend hs_early_suspend;
#endif

static int rk_Hskey_open(struct input_dev *dev)
{
	//struct rk28_adckey *adckey = input_get_drvdata(dev);
//	DBG("===========rk_Hskey_open===========\n");
	return 0;
}

static void rk_Hskey_close(struct input_dev *dev)
{
//	DBG("===========rk_Hskey_close===========\n");
//	struct rk28_adckey *adckey = input_get_drvdata(dev);

}

static int rockchip_headsetobserve_probe(struct platform_device *pdev)
{
	int ret;
	struct headset_priv *headset;
	struct rk_headset_pdata *pdata;
	
	headset = kzalloc(sizeof(struct headset_priv), GFP_KERNEL);
	if (headset == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}	
	headset->pdata = pdev->dev.platform_data;
	pdata = headset->pdata;
	headset->headset_status = HEADSET_OUT;
	headset->hook_status = HOOK_UP;
	headset->isHook_irq = disable;
	headset->cur_headset_status = 0;
	headset->sdev.name = "h2w";
	headset->sdev.print_name = h2w_print_name;
	ret = switch_dev_register(&headset->sdev);
	if (ret < 0)
		goto failed_free;
	
	mutex_init(&headset->mutex_lock[HEADSET]);
	mutex_init(&headset->mutex_lock[HOOK]);
	
	INIT_DELAYED_WORK(&headset->h_delayed_work[HEADSET], headsetobserve_work);
	INIT_DELAYED_WORK(&headset->h_delayed_work[HOOK], Hook_work);

//	init_timer(&headset->headset_timer);
//	headset->headset_timer.function = headset_timer_callback;
//	headset->headset_timer.data = (unsigned long)headset;
//	headset->headset_timer.expires = jiffies + 3000;
	headset->isMic = 0;
	setup_timer(&headset->headset_timer, headset_timer_callback, (unsigned long)headset);
//	headset->headset_timer.expires = jiffies + 1000;
//	add_timer(&headset->headset_timer);	
	
	// Create and register the input driver. 
	headset->input_dev = input_allocate_device();
	if (!headset->input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto failed_free;
	}	
	headset->input_dev->name = pdev->name;
	headset->input_dev->open = rk_Hskey_open;
	headset->input_dev->close = rk_Hskey_close;
	headset->input_dev->dev.parent = &pdev->dev;
	//input_dev->phys = KEY_PHYS_NAME;
	headset->input_dev->id.vendor = 0x0001;
	headset->input_dev->id.product = 0x0001;
	headset->input_dev->id.version = 0x0100;
	// Register the input device 
	ret = input_register_device(headset->input_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed_free_dev;
	}


//	headset->input_dev->keycode = headset->keycodes;
//	headset->input_dev->keycodesize = sizeof(unsigned char);
//	headset->input_dev->keycodemax = 2;
	
//	set_bit(KEY_MEDIA, headset->input_dev->keybit);
//	clear_bit(0, headset->input_dev->keybit);
	input_set_capability(headset->input_dev, EV_KEY, pdata->hook_key_code);
//	input_set_capability(headset->input_dev, EV_SW, SW_HEADPHONE_INSERT);
//	input_set_capability(headset->input_dev, EV_KEY, KEY_END);

//	headset->input_dev->evbit[0] = BIT_MASK(EV_KEY);
	
	headset_info = headset;
	schedule_delayed_work(&headset->h_delayed_work[HEADSET], msecs_to_jiffies(500));	

#ifdef CONFIG_HAS_EARLYSUSPEND
	hs_early_suspend.suspend = NULL;
	hs_early_suspend.resume = headset_early_resume;
	hs_early_suspend.level = ~0x0;
	register_early_suspend(&hs_early_suspend);
#endif

	//------------------------------------------------------------------
	if (pdata->Headset_gpio) {
		ret = pdata->headset_io_init(pdata->Headset_gpio, pdata->headset_gpio_info.iomux_name, pdata->headset_gpio_info.iomux_mode);
		if (ret) 
			goto failed_free;	

		headset->irq[HEADSET] = gpio_to_irq(pdata->Headset_gpio);

		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			headset->irq_type[HEADSET] = IRQF_TRIGGER_RISING;
		else
			headset->irq_type[HEADSET] = IRQF_TRIGGER_FALLING;
		ret = request_irq(headset->irq[HEADSET], headset_interrupt, headset->irq_type[HEADSET], "headset_input", NULL);
		if (ret) 
			goto failed_free_dev;
		enable_irq_wake(headset->irq[HEADSET]);
	}
	else
		goto failed_free_dev;
//------------------------------------------------------------------
	if (pdata->Hook_gpio) {
		ret = pdata->hook_io_init(pdata->Hook_gpio, pdata->hook_gpio_info.iomux_name, pdata->hook_gpio_info.iomux_mode);
		if (ret) 
			goto failed_free;
		headset->irq[HOOK] = gpio_to_irq(pdata->Hook_gpio);
		headset->irq_type[HOOK] = IRQF_TRIGGER_FALLING;
	
		ret = request_irq(headset->irq[HOOK], Hook_interrupt, headset->irq_type[HOOK] , "headset_hook", NULL);
		if (ret) 
			goto failed_free_dev;
		disable_irq(headset->irq[HOOK]);
	}
//------------------------------------------------------------------	

	return 0;	
	
failed_free_dev:
	platform_set_drvdata(pdev, NULL);
	input_free_device(headset->input_dev);
failed_free:
	dev_err(&pdev->dev, "failed to headset probe\n");
	kfree(headset);
	return ret;
}

static int rockchip_headsetobserve_suspend(struct platform_device *pdev, pm_message_t state)
{
	DBG("%s----%d\n",__FUNCTION__,__LINE__);
	disable_irq(headset_info->irq[HEADSET]);
	disable_irq(headset_info->irq[HOOK]);

	return 0;
}

static int rockchip_headsetobserve_resume(struct platform_device *pdev)
{
	DBG("%s----%d\n",__FUNCTION__,__LINE__);	
	enable_irq(headset_info->irq[HEADSET]);
	enable_irq(headset_info->irq[HOOK]);
	
	return 0;
}

static struct platform_driver rockchip_headsetobserve_driver = {
	.probe	= rockchip_headsetobserve_probe,
//	.resume = 	rockchip_headsetobserve_resume,	
//	.suspend = 	rockchip_headsetobserve_suspend,	
	.driver	= {
		.name	= "rk_headsetdet",
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

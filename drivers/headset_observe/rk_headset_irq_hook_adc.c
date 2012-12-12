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
#include <linux/adc.h>
#include <linux/wakelock.h>

/* Debug */
#if 1
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif

#define HOOK_ADC_SAMPLE_TIME	50
#define HOOK_LEVEL_HIGH  		410		//1V*1024/2.5
#define HOOK_LEVEL_LOW  		204		//0.5V*1024/2.5
#define HOOK_DEFAULT_VAL  		1024	

#define BIT_HEADSET             (1 << 0)
#define BIT_HEADSET_NO_MIC      (1 << 1)

#define HEADSET 0
#define HOOK 1

#define HEADSET_IN 1
#define HEADSET_OUT 0
#define HOOK_DOWN 1
#define HOOK_UP 0
#define enable 1
#define disable 0

#define HEADSET_TIMER 1
#define HOOK_TIMER 2

#define WAIT 2
#define BUSY 1
#define IDLE 0

#ifdef CONFIG_SND_SOC_WM8994
extern int wm8994_headset_mic_detect(bool headset_status);
#endif
#ifdef CONFIG_SND_SOC_RT5631_PHONE
extern int rt5631_headset_mic_detect(bool headset_status);
#endif
#if defined (CONFIG_SND_SOC_RT3261) || defined (CONFIG_SND_SOC_RT3224)
extern int rt3261_headset_mic_detect(int jack_insert);
#endif

/* headset private data */
struct headset_priv {
	struct input_dev *input_dev;
	struct rk_headset_pdata *pdata;
	unsigned int headset_status:1;
	unsigned int hook_status:1;
	int isMic;
	unsigned int heatset_irq_working;// headset interrupt working will not check hook key	
	int cur_headset_status; 
	
	unsigned int irq[2];
	unsigned int irq_type[2];
	struct delayed_work h_delayed_work[2];
	struct switch_dev sdev;
	struct mutex mutex_lock[2];	
	struct timer_list headset_timer;
	unsigned char *keycodes;
	struct adc_client *client;
	struct timer_list hook_timer;
	unsigned int hook_time;//ms
	struct wake_lock headset_on_wake;
};
static struct headset_priv *headset_info;

int Headset_isMic(void)
{
	return headset_info->isMic;
}
EXPORT_SYMBOL_GPL(Headset_isMic);

//1
static irqreturn_t headset_interrupt(int irq, void *dev_id)
{
	struct rk_headset_pdata *pdata = headset_info->pdata;
	static unsigned int old_status = 0;
	int i,level = 0;	
	int adc_value = 0;
	wake_lock(&headset_info->headset_on_wake);
	if(headset_info->heatset_irq_working == BUSY || headset_info->heatset_irq_working == WAIT)
		return IRQ_HANDLED;
	DBG("In the headset_interrupt for read headset level  wake_lock headset_on_wake\n");		
	headset_info->heatset_irq_working = BUSY;
	msleep(150);
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
		goto out;
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
		DBG("Read Headset IO level old status == now status\n");
		goto out;
	}

	DBG("(headset in is %s)headset status is %s\n",
		pdata->headset_in_type?"high level":"low level",
		headset_info->headset_status?"in":"out");
	if(headset_info->headset_status == HEADSET_IN)
	{
		#if 0
		while(1)
		{
			if(adc_sync_read(headset_info->client) > HOOK_DEFAULT_VAL
			 || adc_sync_read(headset_info->client) < 0)
			{
				printk("headset is showly inside\n");
			}
			else
				break;
			msleep(50);
			
			if(pdata->headset_in_type == HEADSET_IN_HIGH)
				old_status = headset_info->headset_status = gpio_get_value(pdata->Headset_gpio)?HEADSET_IN:HEADSET_OUT;
			else
				old_status = headset_info->headset_status = gpio_get_value(pdata->Headset_gpio)?HEADSET_OUT:HEADSET_IN;
			if(headset_info->headset_status == HEADSET_OUT)
				goto out1;
			msleep(5);	
		}
		#endif
		if(pdata->Hook_adc_chn>=0 && 3>=pdata->Hook_adc_chn)
		{
		// wait for find Hook key
			//#ifdef CONFIG_SND_SOC_RT5625
			CHECK_AGAIN:
			//headset_info->isMic = rt5625_headset_mic_detect(true);
			#ifdef CONFIG_SND_SOC_WM8994
			wm8994_headset_mic_detect(true);
			#endif
			#if defined (CONFIG_SND_SOC_RT3261) || defined (CONFIG_SND_SOC_RT3224)
			rt3261_headset_mic_detect(true);
			#endif
			#ifdef CONFIG_SND_SOC_RT5631_PHONE
			rt5631_headset_mic_detect(true);
			#endif			
			//mdelay(400);
			adc_value = adc_sync_read(headset_info->client);
			if(adc_value >= 0 && adc_value < HOOK_LEVEL_LOW)
			{
				headset_info->isMic= 0;//No microphone
				#ifdef CONFIG_SND_SOC_WM8994
				wm8994_headset_mic_detect(false);
				#endif
				#if defined (CONFIG_SND_SOC_RT3261) || defined (CONFIG_SND_SOC_RT3224)
				rt3261_headset_mic_detect(false);
				#endif	
				#ifdef CONFIG_SND_SOC_RT5631_PHONE
				rt5631_headset_mic_detect(false);
				#endif					
			}	
			else if(adc_value >= HOOK_LEVEL_HIGH)
				headset_info->isMic = 1;//have mic

			if(headset_info->isMic < 0)	
			{
				printk("codec is error\n");
				headset_info->heatset_irq_working = WAIT;
				if(pdata->headset_in_type == HEADSET_IN_HIGH)
					irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_LOW|IRQF_ONESHOT);
				else
					irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_HIGH|IRQF_ONESHOT);
				schedule_delayed_work(&headset_info->h_delayed_work[HEADSET], msecs_to_jiffies(0));	
				wake_unlock(&headset_info->headset_on_wake);
				return IRQ_HANDLED;
			}
			//adc_value = adc_sync_read(headset_info->client);
			printk("headset adc value = %d\n",adc_value);
			if(headset_info->isMic) {
				if(adc_value > HOOK_DEFAULT_VAL || adc_value < HOOK_LEVEL_HIGH)
					goto CHECK_AGAIN;
				mod_timer(&headset_info->hook_timer, jiffies + msecs_to_jiffies(1000));
			}	
			//#endif		
			headset_info->cur_headset_status = headset_info->isMic ? BIT_HEADSET:BIT_HEADSET_NO_MIC;
		}
		else
		{
			headset_info->isMic= 0;//No microphone
			headset_info->cur_headset_status = BIT_HEADSET_NO_MIC;
		}
		printk("headset->isMic = %d\n",headset_info->isMic);		
		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_FALLING);
		else
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_RISING);
	}
	else if(headset_info->headset_status == HEADSET_OUT)
	{
		headset_info->cur_headset_status = ~(BIT_HEADSET|BIT_HEADSET_NO_MIC);
		del_timer(&headset_info->hook_timer);
		if(headset_info->isMic)
		{
			headset_info->hook_status = HOOK_UP;
			#ifdef CONFIG_SND_SOC_WM8994
			//rt5625_headset_mic_detect(false);
			wm8994_headset_mic_detect(false);
			#endif
			#if defined (CONFIG_SND_SOC_RT3261) || defined (CONFIG_SND_SOC_RT3224)
			rt3261_headset_mic_detect(false);
			#endif
			#ifdef CONFIG_SND_SOC_RT5631_PHONE
			rt5631_headset_mic_detect(false);
			#endif				
		}	
		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_RISING);
		else
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_FALLING);			
	}	

	rk28_send_wakeup_key();			
	switch_set_state(&headset_info->sdev, headset_info->cur_headset_status);	
	DBG("headset notice android headset status = %d\n",headset_info->cur_headset_status);	

//	schedule_delayed_work(&headset_info->h_delayed_work[HEADSET], msecs_to_jiffies(0));
out:
	headset_info->heatset_irq_working = IDLE;
	wake_unlock(&headset_info->headset_on_wake);
	return IRQ_HANDLED;
}

static int headset_change_irqtype(int type,unsigned int irq_type)
{
	int ret = 0;
	free_irq(headset_info->irq[type],NULL);

	DBG("%s: type is %s irqtype is %s\n",__FUNCTION__,	type?"hook":"headset",(irq_type == IRQF_TRIGGER_RISING)?"RISING":"FALLING");
//	DBG("%s: type is %s irqtype is %s\n",__FUNCTION__,	type?"hook":"headset",(irq_type == IRQF_TRIGGER_LOW)?"LOW":"HIGH");
	switch(type)
	{
	case HEADSET:
		ret = request_threaded_irq(headset_info->irq[type],NULL, headset_interrupt, irq_type, "headset_input", NULL);
		if (ret<0) 
			DBG("headset_change_irqtype: request irq failed\n");		
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}
//2
static void headsetobserve_work(struct work_struct *work)
{
	struct rk_headset_pdata *pdata = headset_info->pdata;
	DBG("In the headsetobserve_work headset_status is %s\n",headset_info->headset_status?"in":"out");

	if(headset_info->heatset_irq_working == WAIT && headset_info->headset_status == HEADSET_IN)
	{
		printk("wait for codec\n");
		headset_info->heatset_irq_working = IDLE;
		headset_info->headset_status = HEADSET_OUT;	
		
		free_irq(headset_info->irq[HEADSET],NULL);	
		msleep(100);
		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			headset_info->irq_type[HEADSET] = IRQF_TRIGGER_HIGH|IRQF_ONESHOT;
		else
			headset_info->irq_type[HEADSET] = IRQF_TRIGGER_LOW|IRQF_ONESHOT;
		if(request_threaded_irq(headset_info->irq[HEADSET], NULL,headset_interrupt, headset_info->irq_type[HEADSET]|IRQF_NO_SUSPEND, "headset_input", NULL) < 0)
			printk("headset request_threaded_irq error\n");
		return;	
	}
/*	
	if(pdata->headset_in_type == HEADSET_IN_HIGH && headset_info->headset_status == HEADSET_IN)
		headset_change_irqtype(HEADSET,IRQF_TRIGGER_FALLING);
	else if(pdata->headset_in_type == HEADSET_IN_LOW && headset_info->headset_status == HEADSET_IN)
		headset_change_irqtype(HEADSET,IRQF_TRIGGER_RISING);

	if(pdata->headset_in_type == HEADSET_IN_HIGH && headset_info->headset_status == HEADSET_OUT)
		headset_change_irqtype(HEADSET,IRQF_TRIGGER_RISING);
	else if(pdata->headset_in_type == HEADSET_IN_LOW && headset_info->headset_status == HEADSET_OUT)
		headset_change_irqtype(HEADSET,IRQF_TRIGGER_FALLING);
*/		
}
//4
static void hook_adc_callback(struct adc_client *client, void *client_param, int result)
{
	int level = result;
	struct headset_priv *headset = (struct headset_priv *)client_param;
	struct rk_headset_pdata *pdata = headset->pdata;
	static unsigned int old_status = HOOK_UP;

//	DBG("hook_adc_callback read adc value: %d\n",level);

	if(level < 0)
	{
		printk("%s:get adc level err = %d!\n",__FUNCTION__,level);
		return;
	}

	if(headset->headset_status == HEADSET_OUT
		|| headset->heatset_irq_working == BUSY
		|| headset->heatset_irq_working == WAIT
		|| pdata->headset_in_type?gpio_get_value(pdata->Headset_gpio) == 0:gpio_get_value(pdata->Headset_gpio) > 0)
	{
		DBG("Headset is out or waiting for headset is in or out,after same time check HOOK key\n");
		return;
	}	
	
	old_status = headset->hook_status;
	if(level < HOOK_LEVEL_LOW && level >= 0)	
		headset->hook_status = HOOK_DOWN;
	else if(level > HOOK_LEVEL_HIGH && level < HOOK_DEFAULT_VAL)
		headset->hook_status = HOOK_UP;
	else{
	//	DBG("hook_adc_callback read adc value.........outside showly....: %d\n",level);
		del_timer(&headset->hook_timer);
		mod_timer(&headset->hook_timer, jiffies + msecs_to_jiffies(50));
		return;
	}
	
	if(old_status == headset->hook_status)
	{
	//	DBG("Hook adc read old_status == headset->hook_status hook_time = %d\n",headset->hook_time);
		return;
	}	
	
	DBG("HOOK status is %s , adc value = %d hook_time = %d\n",headset->hook_status?"down":"up",level,headset->hook_time);	
	if(headset->headset_status == HEADSET_OUT
		|| headset->heatset_irq_working == BUSY
		|| headset->heatset_irq_working == WAIT
		|| (pdata->headset_in_type?gpio_get_value(pdata->Headset_gpio) == 0:gpio_get_value(pdata->Headset_gpio) > 0))
		DBG("headset is out,HOOK status must discard\n");
	else
	{
		input_report_key(headset->input_dev,pdata->hook_key_code,headset->hook_status);
		input_sync(headset->input_dev);
	}	
}
//3
static void hook_timer_callback(unsigned long arg)
{
	struct headset_priv *headset = (struct headset_priv *)(arg);
//	DBG("hook_timer_callback\n");
	if(headset->headset_status == HEADSET_OUT
		|| headset->heatset_irq_working == BUSY
		|| headset->heatset_irq_working == WAIT)
		return;
	adc_async_read(headset->client);
	mod_timer(&headset->hook_timer, jiffies + msecs_to_jiffies(headset->hook_time));
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
	headset_info = headset;
	headset->pdata = pdev->dev.platform_data;
	pdata = headset->pdata;
	headset->headset_status = HEADSET_OUT;
	headset->heatset_irq_working = IDLE;
	headset->hook_status = HOOK_UP;
	headset->hook_time = HOOK_ADC_SAMPLE_TIME;
	headset->cur_headset_status = 0;
	headset->sdev.name = "h2w";
	headset->sdev.print_name = h2w_print_name;
	ret = switch_dev_register(&headset->sdev);
	if (ret < 0)
		goto failed_free;
	
//	mutex_init(&headset->mutex_lock[HEADSET]);
//	mutex_init(&headset->mutex_lock[HOOK]);
	wake_lock_init(&headset->headset_on_wake, WAKE_LOCK_SUSPEND, "headset_on_wake");
	INIT_DELAYED_WORK(&headset->h_delayed_work[HEADSET], headsetobserve_work);

	headset->isMic = 0;
//------------------------------------------------------------------		
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

	input_set_capability(headset->input_dev, EV_KEY, pdata->hook_key_code);
//------------------------------------------------------------------
	if (pdata->Headset_gpio) {
		ret = pdata->headset_io_init(pdata->Headset_gpio, pdata->headset_gpio_info.iomux_name, pdata->headset_gpio_info.iomux_mode);
		if (ret) 
			goto failed_free;

		headset->irq[HEADSET] = gpio_to_irq(pdata->Headset_gpio);

		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			headset->irq_type[HEADSET] = IRQF_TRIGGER_HIGH|IRQF_ONESHOT;
		else
			headset->irq_type[HEADSET] = IRQF_TRIGGER_LOW|IRQF_ONESHOT;
		ret = request_threaded_irq(headset->irq[HEADSET], NULL,headset_interrupt, headset->irq_type[HEADSET]|IRQF_NO_SUSPEND, "headset_input", NULL);
		if (ret) 
			goto failed_free;
		enable_irq_wake(headset->irq[HEADSET]);
	}
	else
		goto failed_free;
//------------------------------------------------------------------
	if(pdata->Hook_adc_chn>=0 && 3>=pdata->Hook_adc_chn)
	{
		headset->client = adc_register(pdata->Hook_adc_chn, hook_adc_callback, (void *)headset);
		if(!headset->client) {
			printk("hook adc register error\n");
			ret = -EINVAL;
			goto failed_free;
		}
		setup_timer(&headset->hook_timer,hook_timer_callback, (unsigned long)headset);	
		printk("headset adc default value = %d\n",adc_sync_read(headset->client));
	}
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	hs_early_suspend.suspend = NULL;
	hs_early_suspend.resume = headset_early_resume;
	hs_early_suspend.level = ~0x0;
	register_early_suspend(&hs_early_suspend);
#endif

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
//	disable_irq(headset_info->irq[HEADSET]);
	del_timer(&headset_info->hook_timer);
	return 0;
}

static int rockchip_headsetobserve_resume(struct platform_device *pdev)
{
	DBG("%s----%d\n",__FUNCTION__,__LINE__);	
//	enable_irq(headset_info->irq[HEADSET]);
	if(headset_info->isMic)
		mod_timer(&headset_info->hook_timer, jiffies + msecs_to_jiffies(1500));	
	return 0;
}

static struct platform_driver rockchip_headsetobserve_driver = {
	.probe	= rockchip_headsetobserve_probe,
	.resume = 	rockchip_headsetobserve_resume,	
	.suspend = 	rockchip_headsetobserve_suspend,	
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
late_initcall(rockchip_headsetobserve_init);
MODULE_DESCRIPTION("Rockchip Headset Driver");
MODULE_LICENSE("GPL");


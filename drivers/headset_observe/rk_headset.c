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
#define HOOK_DOWN 1
#define HOOK_UP 0
#define enable 1
#define disable 0

#if defined(CONFIG_SND_RK_SOC_RK2928) || defined(CONFIG_SND_RK29_SOC_RK610)
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

#ifdef CONFIG_MODEM_MIC_SWITCH
#define HP_MIC 0
#define MAIN_MIC 1
void Modem_Mic_switch(int value)
{
	if(value == HP_MIC)
		gpio_set_value(headset_info->pdata->Sw_mic_gpio, headset_info->pdata->Hp_mic_io_value);
	else if(value == MAIN_MIC)
		gpio_set_value(headset_info->pdata->Sw_mic_gpio,headset_info->pdata->Main_mic_io_value);
}
void Modem_Mic_release(void)
{
	if(headset_info->cur_headset_status == 1)
		gpio_set_value(headset_info->pdata->Sw_mic_gpio, headset_info->pdata->Hp_mic_io_value);
	else
		gpio_set_value(headset_info->pdata->Sw_mic_gpio,headset_info->pdata->Main_mic_io_value);
}
#endif
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

static int read_gpio(int gpio)
{
	int i,level;
	for(i=0; i<3; i++)
	{
		level = gpio_get_value(gpio);
		if(level < 0)
		{
			printk("%s:get pin level again,pin=%d,i=%d\n",__FUNCTION__,gpio,i);
			msleep(1);
			continue;
		}
		else
		break;
	}
	if(level < 0)
		printk("%s:get pin level  err!\n",__FUNCTION__);

	return level;
}

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

static void headsetobserve_work(struct work_struct *work)
{
	int level = 0;
	int level2 = 0;
	struct rk_headset_pdata *pdata = headset_info->pdata;
	static unsigned int old_status = 0;
	DBG("---headsetobserve_work---\n");
	mutex_lock(&headset_info->mutex_lock[HEADSET]);

	level = read_gpio(pdata->Headset_gpio);
	if(level < 0)
		goto out;
	msleep(100);	
	level2 = read_gpio(pdata->Headset_gpio);
	if(level2 < 0)
		goto out;
	if(level2 != level)
		goto out;
	old_status = headset_info->headset_status;
	if(pdata->headset_in_type == HEADSET_IN_HIGH)
		headset_info->headset_status = level?HEADSET_IN:HEADSET_OUT;
	else
		headset_info->headset_status = level?HEADSET_OUT:HEADSET_IN;

	if(old_status == headset_info->headset_status)	{
		DBG("old_status == headset_info->headset_status\n");
		goto out;
	}
	
	DBG("(headset in is %s)headset status is %s\n",
		pdata->headset_in_type?"high level":"low level",
		headset_info->headset_status?"in":"out");
		
	if(headset_info->headset_status == HEADSET_IN)
	{
		headset_info->cur_headset_status = BIT_HEADSET_NO_MIC;
		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_FALLING);
		else
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_RISING);
		if (pdata->Hook_gpio) {
			del_timer(&headset_info->headset_timer);//Start the timer, wait for switch to the headphone channel
			headset_info->headset_timer.expires = jiffies + 100;
			add_timer(&headset_info->headset_timer);
			goto out;
		}
	}
	else if(headset_info->headset_status == HEADSET_OUT)
	{	
		headset_info->hook_status = HOOK_UP;
		if(headset_info->isHook_irq == enable)
		{
			DBG("disable headset_hook irq\n");
			headset_info->isHook_irq = disable;
			disable_irq(headset_info->irq[HOOK]);		
		}	
		headset_info->cur_headset_status = 0;//~(BIT_HEADSET|BIT_HEADSET_NO_MIC);
		//#if defined(CONFIG_SND_RK_SOC_RK2928) || defined(CONFIG_SOC_RK3028)
		//rk2928_codec_set_spk(HEADSET_OUT);
		//#endif						
		if(pdata->headset_in_type == HEADSET_IN_HIGH)
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_RISING);
		else
			irq_set_irq_type(headset_info->irq[HEADSET],IRQF_TRIGGER_FALLING);
	}
	rk28_send_wakeup_key();
	switch_set_state(&headset_info->sdev, headset_info->cur_headset_status);	
	#if defined(CONFIG_SND_RK_SOC_RK2928) || defined(CONFIG_SND_RK29_SOC_RK610)
	if (headset_info->headset_status == HEADSET_OUT)
	{
		mdelay(200);
		rk2928_codec_set_spk(HEADSET_OUT);
		gpio_set_value(pdata->Sw_mic_gpio, headset_info->pdata->Main_mic_io_value);
	}
	#endif
	DBG("headset_info->cur_headset_status = %d\n",headset_info->cur_headset_status);

out:
	mutex_unlock(&headset_info->mutex_lock[HEADSET]);	
}

static void Hook_work(struct work_struct *work)
{
	int level = 0;
	struct rk_headset_pdata *pdata = headset_info->pdata;
	static unsigned int old_status = HOOK_UP;

	mutex_lock(&headset_info->mutex_lock[HOOK]);
	
	if(headset_info->headset_status == HEADSET_OUT){
		DBG("Headset is out\n");
		goto RE_ERROR;
	}
	
	#ifdef CONFIG_SND_SOC_WM8994
	if(wm8994_set_status() != 0)	{
		DBG("wm8994 is not set on heatset channel or suspend\n");
		goto RE_ERROR;
	}
	#endif
	
	level = read_gpio(pdata->Hook_gpio);
	if(level < 0)
		goto RE_ERROR;
	
	old_status = headset_info->hook_status;
	DBG("Hook_work -- level = %d\n",level);
	
	if(level == 0)
		headset_info->hook_status = pdata->Hook_down_type == HOOK_DOWN_HIGH?HOOK_UP:HOOK_DOWN;
	else if(level > 0)	
		headset_info->hook_status = pdata->Hook_down_type == HOOK_DOWN_HIGH?HOOK_DOWN:HOOK_UP;
		
	if(old_status == headset_info->hook_status)
	{
		DBG("old_status == headset_info->hook_status\n");
		goto RE_ERROR;
	}	
	DBG("Hook_work -- level = %d  hook status is %s\n",level,headset_info->hook_status?"key down":"key up");	
	if(headset_info->hook_status == HOOK_DOWN)
	{
		if(pdata->Hook_down_type == HOOK_DOWN_HIGH)
			irq_set_irq_type(headset_info->irq[HOOK],IRQF_TRIGGER_FALLING);
		else
			irq_set_irq_type(headset_info->irq[HOOK],IRQF_TRIGGER_RISING);		
	}
	else
	{
		if(pdata->Hook_down_type == HOOK_DOWN_HIGH)
			irq_set_irq_type(headset_info->irq[HOOK],IRQF_TRIGGER_RISING);
		else
			irq_set_irq_type(headset_info->irq[HOOK],IRQF_TRIGGER_FALLING);
	}
	input_report_key(headset_info->input_dev,pdata->hook_key_code,headset_info->hook_status);
	input_sync(headset_info->input_dev);	
RE_ERROR:
	mutex_unlock(&headset_info->mutex_lock[HOOK]);
}

static void headset_timer_callback(unsigned long arg)
{
	struct headset_priv *headset = (struct headset_priv *)(arg);
	struct rk_headset_pdata *pdata = headset->pdata;
	int level = 0;
	
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
	level = read_gpio(pdata->Hook_gpio);
	if(level < 0)
		goto out;
	if((level > 0 && pdata->Hook_down_type == HOOK_DOWN_LOW)
		|| (level == 0 && pdata->Hook_down_type == HOOK_DOWN_HIGH))
	{
		headset->isMic = 1;//have mic
		DBG("enable headset_hook irq\n");
		enable_irq(headset_info->irq[HOOK]);
		headset->isHook_irq = enable;
		headset_info->hook_status = HOOK_UP;
                if(pdata->Hook_down_type == HOOK_DOWN_HIGH)
                        irq_set_irq_type(headset_info->irq[HOOK],IRQF_TRIGGER_RISING);
                else
                        irq_set_irq_type(headset_info->irq[HOOK],IRQF_TRIGGER_FALLING);

	}	
	else	
		headset->isMic= 0;//No microphone
		
	printk("headset->isMic = %d\n",headset->isMic);	
	headset_info->cur_headset_status = headset_info->isMic ? 1:2;//BIT_HEADSET:BIT_HEADSET_NO_MIC;//
	#if defined(CONFIG_SND_RK_SOC_RK2928) || defined(CONFIG_SND_RK29_SOC_RK610)
	rk2928_codec_set_spk(HEADSET_IN);
	if(headset_info->cur_headset_status == 1)
		gpio_set_value(pdata->Sw_mic_gpio, pdata->Hp_mic_io_value);
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

	headset->isMic = 0;
	setup_timer(&headset->headset_timer, headset_timer_callback, (unsigned long)headset);
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

#ifdef CONFIG_HAS_EARLYSUSPEND
	hs_early_suspend.suspend = NULL;
	hs_early_suspend.resume = headset_early_resume;
	hs_early_suspend.level = ~0x0;
	register_early_suspend(&hs_early_suspend);
#endif
	//------------------------------------------------------------------
	if (pdata->Headset_gpio) {
		if(pdata->Headset_gpio == NULL){
			dev_err(&pdev->dev,"failed init headset,please full hook_io_init function in board\n");
			goto failed_free_dev;
		}	
		ret = pdata->headset_io_init(pdata->Headset_gpio);
		if (ret) 
			goto failed_free_dev;	

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
		if(pdata->hook_io_init == NULL){
			dev_err(&pdev->dev,"failed init hook,please full hook_io_init function in board\n");
			goto failed_free_dev;
		}	
		ret = pdata->hook_io_init(pdata->Hook_gpio);
		if (ret) 
			goto failed_free_dev;
		headset->irq[HOOK] = gpio_to_irq(pdata->Hook_gpio);
		headset->irq_type[HOOK] = pdata->Hook_down_type == HOOK_DOWN_HIGH ? IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;
			
		ret = request_irq(headset->irq[HOOK], Hook_interrupt, headset->irq_type[HOOK] , "headset_hook", NULL);
		if (ret) 
			goto failed_free_dev;
		disable_irq(headset->irq[HOOK]);
	}
//------------------------------------------------------------------	
	headset_info = headset;
	schedule_delayed_work(&headset->h_delayed_work[HEADSET], msecs_to_jiffies(500));		
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
late_initcall(rockchip_headsetobserve_init);
MODULE_DESCRIPTION("Rockchip Headset Driver");
MODULE_LICENSE("GPL");

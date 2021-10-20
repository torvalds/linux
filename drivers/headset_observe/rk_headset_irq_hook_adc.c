/*
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
#include <linux/extcon-provider.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/iio/consumer.h>
#include <linux/wakelock.h>
#include <linux/gpio.h>

#include <asm/atomic.h>

#include "rk_headset.h"

/* Debug */
#if 0
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif

#define HOOK_ADC_SAMPLE_TIME	100

#define HOOK_LEVEL_HIGH		410	//1V*1024/2.5
#define HOOK_LEVEL_LOW		204	//0.5V*1024/2.5
#define HOOK_DEFAULT_VAL	1024

#define BIT_HEADSET             BIT(0)
#define BIT_HEADSET_NO_MIC      BIT(1)

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
#if defined(CONFIG_SND_SOC_RT3261) || defined(CONFIG_SND_SOC_RT3224)
extern int rt3261_headset_mic_detect(int jack_insert);
#endif


/* headset private data */
struct headset_priv {
	struct input_dev *input_dev;
	struct rk_headset_pdata *pdata;
	unsigned int headset_status : 1;
	unsigned int hook_status : 1;
	int isMic;
	struct iio_channel *chan;
	/* headset interrupt working will not check hook key  */
	unsigned int heatset_irq_working;
	int cur_headset_status;
	unsigned int irq[2];
	struct delayed_work h_delayed_work[2];
	struct extcon_dev *edev;
	struct mutex mutex_lock[2];
	unsigned char *keycodes;
	struct delayed_work hook_work;
	/* ms */
	unsigned int hook_time;
};

static struct headset_priv *headset_info;

static irqreturn_t headset_interrupt(int irq, void *dev_id)
{
	struct rk_headset_pdata *pdata = headset_info->pdata;
	static unsigned int old_status = 0;
	int i, level = 0;

	disable_irq_nosync(headset_info->irq[HEADSET]);
	if (headset_info->heatset_irq_working == BUSY ||
	    headset_info->heatset_irq_working == WAIT)
		return IRQ_HANDLED;
	DBG("In the headset_interrupt\n");
	headset_info->heatset_irq_working = BUSY;
	msleep(150);
	for (i = 0; i < 3; i++) {
		level = gpio_get_value(pdata->headset_gpio);
		if (level < 0) {
			pr_err("%s:get pin level again,pin=%d,i=%d\n",
			       __func__, pdata->headset_gpio, i);
			msleep(1);
			continue;
		}
		break;
	}
	if (level < 0) {
		pr_err("%s:get pin level  err!\n", __func__);
		goto out;
	} else {
		pr_err("%s:get pin level again, pin=%d,i=%d\n",
		       __func__, pdata->headset_gpio, i);
	}

	old_status = headset_info->headset_status;
	switch (pdata->headset_insert_type) {
	case HEADSET_IN_HIGH:
		if (level > 0)
			headset_info->headset_status = HEADSET_IN;
		else if (level == 0)
			headset_info->headset_status = HEADSET_OUT;
		break;
	case HEADSET_IN_LOW:
		if (level == 0)
			headset_info->headset_status = HEADSET_IN;
		else if (level > 0)
			headset_info->headset_status = HEADSET_OUT;
		break;
	default:
		DBG("---- ERROR: on headset headset_insert_type error -----\n");
		break;
	}
	if (old_status == headset_info->headset_status) {
		DBG("Read Headset IO level old status == now status =%d\n",
		    headset_info->headset_status);
		goto out;
	}

	pr_info("(headset in is %s)headset status is %s\n",
		pdata->headset_insert_type ? "high level" : "low level",
		headset_info->headset_status ? "in" : "out");

	if (headset_info->headset_status == HEADSET_IN) {
		if (pdata->chan != 0) {
			/* detect Hook key */
			schedule_delayed_work(
				&headset_info->h_delayed_work[HOOK],
				msecs_to_jiffies(200));
		} else {
			headset_info->isMic = 0;
			headset_info->cur_headset_status = BIT_HEADSET_NO_MIC;
			extcon_set_state_sync(headset_info->edev,
					      EXTCON_JACK_HEADPHONE, true);
			DBG("headset notice android headset status = %d\n",
			    headset_info->cur_headset_status);
		}
		if (pdata->headset_insert_type == HEADSET_IN_HIGH)
			irq_set_irq_type(headset_info->irq[HEADSET],
					 IRQF_TRIGGER_FALLING);
		else
			irq_set_irq_type(headset_info->irq[HEADSET],
					 IRQF_TRIGGER_RISING);
	} else if (headset_info->headset_status == HEADSET_OUT) {
		headset_info->cur_headset_status = HEADSET_OUT;
		cancel_delayed_work(&headset_info->hook_work);
		if (headset_info->isMic) {
			if (headset_info->hook_status == HOOK_DOWN) {
				headset_info->hook_status = HOOK_UP;
				input_report_key(headset_info->input_dev, HOOK_KEY_CODE, headset_info->hook_status);
				input_sync(headset_info->input_dev);
			}
#ifdef CONFIG_SND_SOC_WM8994
			//rt5625_headset_mic_detect(false);
			wm8994_headset_mic_detect(false);
#endif
#if defined(CONFIG_SND_SOC_RT3261) || defined(CONFIG_SND_SOC_RT3224)
			rt3261_headset_mic_detect(false);
#endif
#ifdef CONFIG_SND_SOC_RT5631_PHONE
			rt5631_headset_mic_detect(false);
#endif
			headset_info->isMic = 0;
		}

		if (pdata->headset_insert_type == HEADSET_IN_HIGH)
			irq_set_irq_type(headset_info->irq[HEADSET],
					 IRQF_TRIGGER_RISING);
		else
			irq_set_irq_type(headset_info->irq[HEADSET],
					 IRQF_TRIGGER_FALLING);
		extcon_set_state_sync(headset_info->edev, EXTCON_JACK_HEADPHONE,
				      false);
		extcon_set_state_sync(headset_info->edev,
				      EXTCON_JACK_MICROPHONE, false);
		DBG("headset notice android headset status = %d\n",
		    headset_info->cur_headset_status);
	}
	/*rk_send_wakeup_key();  */
out:
	headset_info->heatset_irq_working = IDLE;
	enable_irq(headset_info->irq[HEADSET]);
	return IRQ_HANDLED;
}

#if 0
static int headset_change_irqtype(int type, unsigned int irq_type)
{
	int ret = 0;

	free_irq(headset_info->irq[type], NULL);

	DBG("%s: type is %s irqtype is %s\n", __func__,
	    type ? "hook" : "headset",
	    (irq_type == IRQF_TRIGGER_RISING) ? "RISING" : "FALLING");
//      DBG("%s: type is %s irqtype is %s\n",__func__,      type?"hook":"headset",(irq_type == IRQF_TRIGGER_LOW)?"LOW":"HIGH");
	switch (type) {
	case HEADSET:
		ret =
		    request_threaded_irq(headset_info->irq[type], NULL,
					 headset_interrupt, irq_type,
					 "headset_input", NULL);
		if (ret < 0)
			DBG("headset_change_irqtype: request irq failed\n");
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}
#endif

static void hook_once_work(struct work_struct *work)
{
	int ret, val;

#ifdef CONFIG_SND_SOC_WM8994
	wm8994_headset_mic_detect(true);
#endif

#if defined(CONFIG_SND_SOC_RT3261) || defined(CONFIG_SND_SOC_RT3224)
	rt3261_headset_mic_detect(true);
#endif

#ifdef CONFIG_SND_SOC_RT5631_PHONE
	rt5631_headset_mic_detect(true);
#endif
	ret = iio_read_channel_raw(headset_info->chan, &val);
	if (ret < 0)
		pr_err("read hook_once_work adc channel() error: %d\n", ret);
	else
		DBG("hook_once_work read adc value: %d\n", val);

	if (val >= 0 && val < HOOK_LEVEL_LOW) {
		headset_info->isMic = 0;
#ifdef CONFIG_SND_SOC_WM8994
		wm8994_headset_mic_detect(false);
#endif

#if defined (CONFIG_SND_SOC_RT3261) || defined (CONFIG_SND_SOC_RT3224)
		rt3261_headset_mic_detect(false);
#endif

#ifdef CONFIG_SND_SOC_RT5631_PHONE
		rt5631_headset_mic_detect(false);
#endif
	} else if (val >= HOOK_LEVEL_HIGH) {
		headset_info->isMic = 1;
		schedule_delayed_work(&headset_info->hook_work,
				      msecs_to_jiffies(100));
	}
	headset_info->cur_headset_status =
		headset_info->isMic ? BIT_HEADSET : BIT_HEADSET_NO_MIC;
	if (headset_info->cur_headset_status) {
		if (headset_info->isMic) {
			extcon_set_state_sync(headset_info->edev,
					      EXTCON_JACK_MICROPHONE, true);
		} else {
			extcon_set_state_sync(headset_info->edev,
					      EXTCON_JACK_HEADPHONE, true);
		}
	}
	DBG("%s notice android headset status = %d\n", __func__,
	    headset_info->cur_headset_status);
}

static void hook_work_callback(struct work_struct *work)
{
	int ret, val;
	struct headset_priv *headset = headset_info;
	struct rk_headset_pdata *pdata = headset->pdata;
	static unsigned int old_status = HOOK_UP;

	ret = iio_read_channel_raw(headset->chan, &val);
	if (ret < 0) {
		pr_err("read hook adc channel() error: %d\n", ret);
		goto out;
	} else {
		DBG("hook_work_callback read adc value=%d\n", val);
	}

	if (headset->headset_status == HEADSET_OUT ||
	    headset->heatset_irq_working == BUSY ||
	    headset->heatset_irq_working == WAIT ||
	    (pdata->headset_insert_type ?
		    gpio_get_value(pdata->headset_gpio) == 0 :
		    gpio_get_value(pdata->headset_gpio) > 0)) {
		DBG("Headset is out or waiting for headset is in or out, after same time check HOOK key\n");
		goto out;
	}
	old_status = headset->hook_status;
	if (val < HOOK_LEVEL_LOW && val >= 0)
		headset->hook_status = HOOK_DOWN;
	else if (val > HOOK_LEVEL_HIGH && val < HOOK_DEFAULT_VAL)
		headset->hook_status = HOOK_UP;
	DBG("HOOK status is %s , adc value = %d hook_time = %d\n",
	    headset->hook_status ? "down" : "up", val, headset->hook_time);
	if (old_status == headset->hook_status) {
		DBG("Hook adc read old_status == headset->hook_status=%d hook_time = %d\n",
		    headset->hook_status, headset->hook_time);
		goto status_error;
	}
	if (headset->headset_status == HEADSET_OUT ||
	    headset->heatset_irq_working == BUSY ||
	    headset->heatset_irq_working == WAIT ||
	    (pdata->headset_insert_type ?
		     gpio_get_value(pdata->headset_gpio) == 0 :
		     gpio_get_value(pdata->headset_gpio) > 0)) {
		printk("headset is out, HOOK status must discard\n");
		goto out;
	} else {
		input_report_key(headset->input_dev,
				 HOOK_KEY_CODE, headset->hook_status);
		input_sync(headset->input_dev);
	}
status_error:
	schedule_delayed_work(&headset_info->hook_work, msecs_to_jiffies(100));
out:;
}

static int rk_hskey_open(struct input_dev *dev)
{
	return 0;
}

static void rk_hskey_close(struct input_dev *dev)
{
}

static const unsigned int headset_cable[] = {
	EXTCON_JACK_MICROPHONE,
	EXTCON_JACK_HEADPHONE,
	EXTCON_NONE,
};

int rk_headset_adc_probe(struct platform_device *pdev,
			 struct rk_headset_pdata *pdata)
{
	int ret;
	struct headset_priv *headset;

	headset = devm_kzalloc(&pdev->dev, sizeof(*headset), GFP_KERNEL);
	if (!headset) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto failed;
	}
	headset_info = headset;
	headset->pdata = pdata;
	headset->headset_status = HEADSET_OUT;
	headset->heatset_irq_working = IDLE;
	headset->hook_status = HOOK_UP;
	headset->hook_time = HOOK_ADC_SAMPLE_TIME;
	headset->cur_headset_status = 0;
	headset->edev = devm_extcon_dev_allocate(&pdev->dev, headset_cable);
	if (IS_ERR(headset->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		ret = -ENOMEM;
		goto failed;
	}
	ret = devm_extcon_dev_register(&pdev->dev, headset->edev);
	if (ret < 0) {
		dev_err(&pdev->dev, "extcon_dev_register() failed: %d\n", ret);
		goto failed;
	}
	INIT_DELAYED_WORK(&headset->h_delayed_work[HOOK], hook_once_work);
	headset->isMic = 0;
	//------------------------------------------------------------------
	// Create and register the input driver
	headset->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!headset->input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto failed;
	}
	headset->input_dev->name = pdev->name;
	headset->input_dev->open = rk_hskey_open;
	headset->input_dev->close = rk_hskey_close;
	headset->input_dev->dev.parent = &pdev->dev;
	//input_dev->phys = KEY_PHYS_NAME;
	headset->input_dev->id.vendor = 0x0001;
	headset->input_dev->id.product = 0x0001;
	headset->input_dev->id.version = 0x0100;
	// Register the input device
	ret = input_register_device(headset->input_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed;
	}
	input_set_capability(headset->input_dev, EV_KEY, HOOK_KEY_CODE);
	if (pdata->headset_gpio) {
		unsigned long irq_type;

		headset->irq[HEADSET] = gpio_to_irq(pdata->headset_gpio);
		if (pdata->headset_insert_type == HEADSET_IN_HIGH)
			irq_type = IRQF_TRIGGER_HIGH;
		else
			irq_type = IRQF_TRIGGER_LOW;
		irq_type |= IRQF_NO_SUSPEND | IRQF_ONESHOT;
		ret =
		    devm_request_threaded_irq(&pdev->dev, headset->irq[HEADSET],
					      NULL, headset_interrupt,
					      irq_type, "headset_input",
					      NULL);
		if (ret)
			goto failed;
		if (pdata->headset_wakeup)
			enable_irq_wake(headset->irq[HEADSET]);
	} else {
		dev_err(&pdev->dev, "failed init headset,please full hook_io_init function in board\n");
		ret = -EEXIST;
		goto failed;
	}
	if (pdata->chan) {
		headset->chan = pdata->chan;
		INIT_DELAYED_WORK(&headset->hook_work, hook_work_callback);
	}
	return 0;
failed:
	dev_err(&pdev->dev, "failed headset adc probe ret=%d\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(rk_headset_adc_probe);

int rk_headset_adc_suspend(struct platform_device *pdev, pm_message_t state)
{
	DBG("%s----%d\n", __func__, __LINE__);
//	disable_irq(headset_info->irq[HEADSET]);
//	del_timer(&headset_info->hook_timer);
	return 0;
}
EXPORT_SYMBOL_GPL(rk_headset_adc_suspend);

int rk_headset_adc_resume(struct platform_device *pdev)
{
	DBG("%s----%d\n", __func__, __LINE__);
//	enable_irq(headset_info->irq[HEADSET]);
//	if(headset_info->isMic)
//		mod_timer(&headset_info->hook_timer, jiffies + msecs_to_jiffies(1500));
	return 0;
}
EXPORT_SYMBOL_GPL(rk_headset_adc_resume);

MODULE_LICENSE("GPL");

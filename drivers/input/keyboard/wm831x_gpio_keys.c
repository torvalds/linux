/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>

#include <asm/gpio.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>

#define CONFIG_WM831X_GPIO_KEY_DEBUG	  0

#if (CONFIG_WM831X_GPIO_KEY_DEBUG)
#define WM831X_GPIO_KEY_DG(format, ...)      printk(format, ## __VA_ARGS__)
#else
#define WM831X_GPIO_KEY_DG(format, ...)
#endif
bool isHSKeyMIC = false;
int pre_state = 0;
struct wm831x_gpio_keys_button *media_button;

extern bool wm8994_set_status(void);
extern int headset_status(void);

struct wm831x_gpio_button_data {
	struct wm831x_gpio_keys_button *button;
	struct input_dev *input;
	struct timer_list timer;
	struct work_struct work;
};

struct wm831x_gpio_keys_drvdata {
	struct input_dev *input;
	struct wm831x_gpio_button_data data[0];
};

bool isHSKey_MIC(void)
{	    
	return isHSKeyMIC;
}
EXPORT_SYMBOL_GPL(isHSKey_MIC);

void detect_HSMic(void)
{
	int state;
	struct wm831x_gpio_keys_button *button = media_button;
	WM831X_GPIO_KEY_DG("detect_HSMic\n");
	if(!headset_status())
	{
		isHSKeyMIC = false;
		return ;
	}
	else
	{
		mdelay(500);
	}
	
	state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low;

	WM831X_GPIO_KEY_DG("detect_HSMic: code=%d,gpio=%d\n",button->gpio,button->code);
	if(state){
		WM831X_GPIO_KEY_DG("detect_HSMic:---headset without MIC and HSKey---\n");
		isHSKeyMIC = false;
	}else{
		WM831X_GPIO_KEY_DG("detect_HSMic:---headset with MIC---\n");
		isHSKeyMIC = true;
	}

	return;
}
EXPORT_SYMBOL_GPL(detect_HSMic);

static int HSKeyDetect(int state)
{
	WM831X_GPIO_KEY_DG("HSKeyDetect\n");

	if(headset_status()){
		WM831X_GPIO_KEY_DG("headset_status()  == true !\n");
		if(pre_state != state && !wm8994_set_status()){
			WM831X_GPIO_KEY_DG("wm8994_set_status()  == true !\n");
			pre_state = state;
			
			if(!isHSKeyMIC){
				state = -1;
			}
		}
		else{
			WM831X_GPIO_KEY_DG("wm8994_set_status()  == false !\n");
			state = -1;
		}
	}
	else{
		WM831X_GPIO_KEY_DG("headset_status()  == false !\n");
		isHSKeyMIC = false;
		state = -1;
	}

	return state;
}

static void wm831x_gpio_keys_report_event(struct work_struct *work)
{
	struct wm831x_gpio_button_data *bdata =
		container_of(work, struct wm831x_gpio_button_data, work);
	struct wm831x_gpio_keys_button *button = bdata->button;
	struct input_dev *input = bdata->input;
	unsigned int type = button->type ?: EV_KEY;
	
	int state = (gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low;
	if(button->code == KEY_MEDIA)
	{
		state = HSKeyDetect(state);
		
		if(state == -1)
		{
			WM831X_GPIO_KEY_DG("wm831x_gpio_keys_report_event:HSKeyDetect=-1\n");
			goto out;
		}
	}
	printk("wm831x_gpio_keys_report_event:state=%d,code=%d \n",state,button->code);
	
	input_event(input, type, button->code, state);
	input_sync(input);
out:	
	enable_irq(gpio_to_irq(button->gpio));
	return;
}

static void wm831x_gpio_keys_timer(unsigned long _data)
{
	struct wm831x_gpio_button_data *data = (struct wm831x_gpio_button_data *)_data;

	WM831X_GPIO_KEY_DG("wm831x_gpio_keys_timer\n");
	schedule_work(&data->work);
}

static irqreturn_t wm831x_gpio_keys_isr(int irq, void *dev_id)
{
	struct wm831x_gpio_button_data *bdata = dev_id;
	struct wm831x_gpio_keys_button *button = bdata->button;
	
	//printk("wm831x_gpio_keys_isr:irq=%d,%d \n",irq,button->debounce_interval);
	
	BUG_ON(irq != gpio_to_irq(button->gpio));
	disable_irq_nosync(gpio_to_irq(button->gpio));
	if (button->debounce_interval)
		mod_timer(&bdata->timer,
			jiffies + msecs_to_jiffies(button->debounce_interval));
	else
		schedule_work(&bdata->work);
		
	return IRQ_HANDLED;
}

static int __devinit wm831x_gpio_keys_probe(struct platform_device *pdev)
{

	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct wm831x_gpio_keys_pdata *gpio_keys;
	struct wm831x_gpio_keys_drvdata *ddata;
	struct input_dev *input;
	int i, error;
	//int wakeup = 0;

	printk("wm831x_gpio_keys_probe\n");
	
	if (pdata == NULL || pdata->gpio_keys == NULL)
		return -ENODEV;

	gpio_keys = pdata->gpio_keys;
	
	ddata = kzalloc(sizeof(struct wm831x_gpio_keys_drvdata) +
			gpio_keys->nbuttons * sizeof(struct wm831x_gpio_button_data),
			GFP_KERNEL);
	input = input_allocate_device();
	if (!ddata || !input) {
		error = -ENOMEM;
		goto fail1;
	}
	
	platform_set_drvdata(pdev, ddata);

	input->name = pdev->name;
	input->phys = "wm831x_gpio-keys/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	/* Enable auto repeat feature of Linux input subsystem */
	if (gpio_keys->rep)
		__set_bit(EV_REP, input->evbit);

	ddata->input = input;
	
	for (i = 0; i < gpio_keys->nbuttons; i++) {
		struct wm831x_gpio_keys_button *button = &gpio_keys->buttons[i];
		struct wm831x_gpio_button_data *bdata = &ddata->data[i];
		int irq = 0;
		unsigned int type = button->type ?: EV_KEY;

		bdata->input = input;
		bdata->button = button;
		
		if(button->code == KEY_MEDIA)
		{
			media_button = button;
		}
		if (button->debounce_interval)
			setup_timer(&bdata->timer,
			   	 	wm831x_gpio_keys_timer, (unsigned long)bdata);
		//else
		INIT_WORK(&bdata->work, wm831x_gpio_keys_report_event);

		error = gpio_request(button->gpio, button->desc ?: "wm831x_gpio_keys");
		if (error < 0) {
			pr_err("wm831x_gpio-keys: failed to request GPIO %d,"
				" error %d\n", button->gpio, error);
			goto fail2;
		}

		if(button->gpio >= WM831X_P01 && button->gpio <= WM831X_P12)
		{
			error = gpio_pull_updown(button->gpio,GPIOPullUp);
			if (error < 0) {
				pr_err("wm831x_gpio-keys: failed to pull up"
					" for GPIO %d, error %d\n",
					button->gpio, error);
				gpio_free(button->gpio);
				goto fail2;
			}
		}
		
		error = gpio_direction_input(button->gpio);
		if (error < 0) {
			pr_err("wm831x_gpio-keys: failed to configure input"
				" direction for GPIO %d, error %d\n",
				button->gpio, error);
			gpio_free(button->gpio);
			goto fail2;
		}

		irq = gpio_to_irq(button->gpio);
		if (irq < 0) {
			error = irq;
			pr_err("wm831x_gpio-keys: Unable to get irq number"
				" for GPIO %d, error %d\n",
				button->gpio, error);
			gpio_free(button->gpio);
			goto fail2;
		}
		printk("wm831x_gpio_keys_probe:i=%d,gpio=%d,irq=%d \n",i,button->gpio,irq);
		enable_irq_wake(irq);	
		if(button->gpio >= WM831X_P01 && button->gpio <= WM831X_P12)
		{
			error = request_threaded_irq(irq, NULL,wm831x_gpio_keys_isr,
					    IRQF_SHARED |
					    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					    button->desc ? button->desc : "wm831x_gpio_keys",
					    bdata);
		}
		else if(button->gpio >= TCA6424_P00 && button->gpio <= TCA6424_P27)
		{
			error = request_irq(irq, wm831x_gpio_keys_isr,
				    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				    button->desc ? button->desc : "tca6424_gpio_keys",
				    bdata);
		}
		
		if (error) {
			pr_err("wm831x_gpio-keys: Unable to claim irq %d; error %d\n",
				irq, error);
			gpio_free(button->gpio);
			goto fail2;
		}

		//if (button->wakeup)
		//	wakeup = 1;

		input_set_capability(input, type, button->code);
	}

	error = input_register_device(input);
	if (error) {
		pr_err("wm831x_gpio-keys: Unable to register input device, "
			"error: %d\n", error);
		goto fail2;
	}

	//device_init_wakeup(&pdev->dev, wakeup);

	return 0;

 fail2:
	while (--i >= 0) {
		free_irq(gpio_to_irq(gpio_keys->buttons[i].gpio), &ddata->data[i]);
		if (gpio_keys->buttons[i].debounce_interval)
			del_timer_sync(&ddata->data[i].timer);
		//else
			cancel_work_sync(&ddata->data[i].work);
		gpio_free(gpio_keys->buttons[i].gpio);
	}

	platform_set_drvdata(pdev, NULL);
 fail1:
	input_free_device(input);
	kfree(ddata);

	return error;
}

static int __devexit wm831x_gpio_keys_remove(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct wm831x_gpio_keys_pdata *gpio_keys;
	struct wm831x_gpio_keys_drvdata *ddata = platform_get_drvdata(pdev);
	struct input_dev *input = ddata->input;
	int i;
	
	if (pdata == NULL || pdata->gpio_keys == NULL)
		return -ENODEV;
	
	gpio_keys = pdata->gpio_keys;
	
	//device_init_wakeup(&pdev->dev, 0);

	for (i = 0; i < gpio_keys->nbuttons; i++) {
		int irq = gpio_to_irq(gpio_keys->buttons[i].gpio);
		free_irq(irq, &ddata->data[i]);
		if (gpio_keys->buttons[i].debounce_interval)
			del_timer_sync(&ddata->data[i].timer);
		//else
			cancel_work_sync(&ddata->data[i].work);
		gpio_free(gpio_keys->buttons[i].gpio);
	}

	input_unregister_device(input);

	return 0;
}

#ifdef CONFIG_PM
static int wm831x_gpio_keys_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct wm831x_gpio_keys_pdata *gpio_keys;
	int i,irq;

	if (pdata == NULL || pdata->gpio_keys == NULL)
	{
		printk("wm831x_gpio_keys_suspend fail\n");
		return -ENODEV;
	}	

	//printk("wm831x_gpio_keys_suspend\n");
	
	gpio_keys = pdata->gpio_keys;
	
	//if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < gpio_keys->nbuttons; i++) {
			struct wm831x_gpio_keys_button *button = &gpio_keys->buttons[i];
			if (button->wakeup) {
				irq = gpio_to_irq(button->gpio);
				enable_irq_wake(irq);			
			}
			else
			{
				irq = gpio_to_irq(button->gpio);
				disable_irq_wake(irq);
			}
		}
	//}

	return 0;
}

static int wm831x_gpio_keys_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *pdata = wm831x->dev->platform_data;
	struct wm831x_gpio_keys_pdata *gpio_keys;
	int i,irq;

	if (pdata == NULL || pdata->gpio_keys == NULL)
	{
		printk("wm831x_gpio_keys_resume fail\n");
		return -ENODEV;
	}
	
	//printk("wm831x_gpio_keys_resume\n");
	
	gpio_keys = pdata->gpio_keys;
	
	//if (device_may_wakeup(&pdev->dev)) {
		for (i = 0; i < gpio_keys->nbuttons; i++) {
			struct wm831x_gpio_keys_button *button = &gpio_keys->buttons[i];
			//if (button->wakeup) {		
				irq = gpio_to_irq(button->gpio);
				enable_irq_wake(irq);
			//}
		}
	//}

	return 0;
}

static const struct dev_pm_ops wm831x_gpio_keys_pm_ops = {
	.suspend	= wm831x_gpio_keys_suspend,
	.resume		= wm831x_gpio_keys_resume,
};		
#endif

static struct platform_driver wm831x_gpio_keys_device_driver = {
	.probe		= wm831x_gpio_keys_probe,
	.remove		= __devexit_p(wm831x_gpio_keys_remove),
	.driver		= {
		.name	= "wm831x_gpio-keys",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &wm831x_gpio_keys_pm_ops,
#endif
	}
};

static int __init wm831x_gpio_keys_init(void)
{
	return platform_driver_register(&wm831x_gpio_keys_device_driver);
}

static void __exit wm831x_gpio_keys_exit(void)
{
	platform_driver_unregister(&wm831x_gpio_keys_device_driver);
}

subsys_initcall(wm831x_gpio_keys_init);
module_exit(wm831x_gpio_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SRT <srt@rock-chip.com>");
MODULE_DESCRIPTION("Keyboard driver for WM831x GPIOs");
MODULE_ALIAS("platform:wm831x_gpio-keys");

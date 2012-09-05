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
#include <linux/adc.h>

#include <asm/gpio.h>
#include <mach/board.h>
#include <plat/key.h>

#define EMPTY_ADVALUE					950
#define DRIFT_ADVALUE					70
#define INVALID_ADVALUE 				-1
#define EV_MENU					KEY_F1


#if 0
#define key_dbg(bdata, format, arg...)		\
	dev_printk(KERN_INFO , &bdata->input->dev , format , ## arg)
#else
#define key_dbg(bdata, format, arg...)	
#endif

struct rk29_button_data {
	int state;
	int long_press_count;
	struct rk29_keys_button *button;
	struct input_dev *input;
	struct timer_list timer;
};

struct rk29_keys_drvdata {
	int nbuttons;
	int result;
	bool in_suspend;	/* Flag to indicate if we're suspending/resuming */
	struct input_dev *input;
	struct adc_client *client;
	struct timer_list timer;
	struct rk29_button_data data[0];
};

static struct input_dev *input_dev;
struct rk29_keys_Arrary {
	char keyArrary[20];
};

static ssize_t rk29key_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct rk29_keys_platform_data *pdata = dev_get_platdata(dev);
	int i,j,start,end;
	char rk29keyArrary[400];
	struct rk29_keys_Arrary Arrary[]={
                {
                        .keyArrary = {"menu"},
                },
                {
                        .keyArrary = {"home"},
                },
                {
                        .keyArrary = {"esc"},
                },
                {
                        .keyArrary = {"sensor"},
                },
                {
                        .keyArrary = {"play"},
                },
                {
                        .keyArrary = {"vol+"},
                },
                {
                        .keyArrary = {"vol-"},
                },
        }; 
	char *p;
	  
	for(i=0;i<7;i++)
	{
		
		p = strstr(buf,Arrary[i].keyArrary);
		
		start = strcspn(p,":");
		
		if(i<6)
			end = strcspn(p,",");
		else
			end = strcspn(p,"}");
	
		memset(rk29keyArrary,0,sizeof(rk29keyArrary));
		
		strncpy(rk29keyArrary,p+start+1,end-start-1);
							 		
		for(j=0;j<7;j++)
		{		
			if(strcmp(pdata->buttons[j].desc,Arrary[i].keyArrary)==0)
			{
				if(strcmp(rk29keyArrary,"MENU")==0)
					pdata->buttons[j].code = EV_MENU;
				else if(strcmp(rk29keyArrary,"HOME")==0)
					pdata->buttons[j].code = KEY_HOME;
				else if(strcmp(rk29keyArrary,"ESC")==0)
					pdata->buttons[j].code = KEY_BACK;
				else if(strcmp(rk29keyArrary,"sensor")==0)
					pdata->buttons[j].code = KEY_CAMERA;
				else if(strcmp(rk29keyArrary,"PLAY")==0)
					pdata->buttons[j].code = KEY_POWER;
				else if(strcmp(rk29keyArrary,"VOLUP")==0)
					pdata->buttons[j].code = KEY_VOLUMEUP;
				else if(strcmp(rk29keyArrary,"VOLDOWN")==0)
					pdata->buttons[j].code = KEY_VOLUMEDOWN;
				else
				     continue;
		 	}

		}
			
   	}

	for(i=0;i<7;i++)
		dev_dbg(dev, "desc=%s, code=%d\n",pdata->buttons[i].desc,pdata->buttons[i].code);
	return 0; 

}

static DEVICE_ATTR(rk29key,0660, NULL, rk29key_set);

void rk29_send_power_key(int state)
{
	if (!input_dev)
		return;
	if(state)
	{
		input_report_key(input_dev, KEY_POWER, 1);
		input_sync(input_dev);
	}
	else
	{
		input_report_key(input_dev, KEY_POWER, 0);
		input_sync(input_dev);
	}
}

void rk28_send_wakeup_key(void)
{
	if (!input_dev)
		return;

	input_report_key(input_dev, KEY_WAKEUP, 1);
	input_sync(input_dev);
	input_report_key(input_dev, KEY_WAKEUP, 0);
	input_sync(input_dev);
}

static void keys_long_press_timer(unsigned long _data)
{
	int state;
	struct rk29_button_data *bdata = (struct rk29_button_data *)_data;
	struct rk29_keys_button *button = bdata->button;
	struct input_dev *input = bdata->input;
	unsigned int type = EV_KEY;
	if(button->gpio != INVALID_GPIO )
		state = !!((gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low);
	else
		state = !!button->adc_state;
	if(state) {
		if(bdata->long_press_count != 0) {
			if(bdata->long_press_count % (LONG_PRESS_COUNT+ONE_SEC_COUNT) == 0){
				key_dbg(bdata, "%skey[%s]: report ev[%d] state[0]\n", 
					(button->gpio == INVALID_GPIO)?"ad":"io", button->desc, button->code_long_press);
				input_event(input, type, button->code_long_press, 0);
				input_sync(input);
			}
			else if(bdata->long_press_count%LONG_PRESS_COUNT == 0) {
				key_dbg(bdata, "%skey[%s]: report ev[%d] state[1]\n", 
					(button->gpio == INVALID_GPIO)?"ad":"io", button->desc, button->code_long_press);
				input_event(input, type, button->code_long_press, 1);
				input_sync(input);
			}
		}
		bdata->long_press_count++;
		mod_timer(&bdata->timer,
				jiffies + msecs_to_jiffies(DEFAULT_DEBOUNCE_INTERVAL));
	}
	else {
		if(bdata->long_press_count <= LONG_PRESS_COUNT) {
			bdata->long_press_count = 0;
			key_dbg(bdata, "%skey[%s]: report ev[%d] state[1], report ev[%d] state[0]\n", 
					(button->gpio == INVALID_GPIO)?"ad":"io", button->desc, button->code, button->code);
			input_event(input, type, button->code, 1);
			input_sync(input);
			input_event(input, type, button->code, 0);
			input_sync(input);
		}
		else if(bdata->state != state) {
			key_dbg(bdata, "%skey[%s]: report ev[%d] state[0]\n", 
			(button->gpio == INVALID_GPIO)?"ad":"io", button->desc, button->code_long_press);
			input_event(input, type, button->code_long_press, 0);
			input_sync(input);
		}
	}
	bdata->state = state;
}
static void keys_timer(unsigned long _data)
{
	int state;
	struct rk29_button_data *bdata = (struct rk29_button_data *)_data;
	struct rk29_keys_button *button = bdata->button;
	struct input_dev *input = bdata->input;
	unsigned int type = EV_KEY;
	
	if(button->gpio != INVALID_GPIO)
		state = !!((gpio_get_value(button->gpio) ? 1 : 0) ^ button->active_low);
	else
		state = !!button->adc_state;
	if(bdata->state != state) {
		bdata->state = state;
		key_dbg(bdata, "%skey[%s]: report ev[%d] state[%d]\n", 
			(button->gpio == INVALID_GPIO)?"ad":"io", button->desc, button->code, bdata->state);
		input_event(input, type, button->code, bdata->state);
		input_sync(input);
	}
	if(state)
		mod_timer(&bdata->timer,
			jiffies + msecs_to_jiffies(DEFAULT_DEBOUNCE_INTERVAL));
}

static irqreturn_t keys_isr(int irq, void *dev_id)
{
	struct rk29_button_data *bdata = dev_id;
	struct rk29_keys_button *button = bdata->button;
	BUG_ON(irq != gpio_to_irq(button->gpio));
	bdata->long_press_count = 0;
	mod_timer(&bdata->timer,
				jiffies + msecs_to_jiffies(DEFAULT_DEBOUNCE_INTERVAL));
	return IRQ_HANDLED;
}
static void callback(struct adc_client *client, void *client_param, int result)
{
	struct rk29_keys_drvdata *ddata = (struct rk29_keys_drvdata *)client_param;
	int i;
	if(result > INVALID_ADVALUE && result < EMPTY_ADVALUE)
		ddata->result = result;
	for (i = 0; i < ddata->nbuttons; i++) {
		struct rk29_button_data *bdata = &ddata->data[i];
		struct rk29_keys_button *button = bdata->button;
		if(!button->adc_value)
			continue;
		if(result < button->adc_value + DRIFT_ADVALUE &&
			result > button->adc_value - DRIFT_ADVALUE)
			button->adc_state = 1;
		else
			button->adc_state = 0;
		if(bdata->state != button->adc_state)
			mod_timer(&bdata->timer,
				jiffies + msecs_to_jiffies(DEFAULT_DEBOUNCE_INTERVAL));
	}
	return;
}
static void adc_timer(unsigned long _data)
{
	struct rk29_keys_drvdata *ddata = (struct rk29_keys_drvdata *)_data;

	if (!ddata->in_suspend)
		adc_async_read(ddata->client);
	mod_timer(&ddata->timer, jiffies + msecs_to_jiffies(ADC_SAMPLE_TIME));
}
static ssize_t adc_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rk29_keys_drvdata *ddata = dev_get_drvdata(dev);
	
	return sprintf(buf, "adc_value: %d\n", ddata->result);
}

static DEVICE_ATTR(get_adc_value, S_IRUGO | S_IWUSR, adc_value_show, NULL);

static int __devinit keys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk29_keys_platform_data *pdata = dev_get_platdata(dev);
	struct rk29_keys_drvdata *ddata;
	struct input_dev *input;
	int i, error = 0;
	int wakeup = 0;

	if(!pdata) 
		return -EINVAL;
	
	ddata = kzalloc(sizeof(struct rk29_keys_drvdata) +
			pdata->nbuttons * sizeof(struct rk29_button_data),
			GFP_KERNEL);
	input = input_allocate_device();
	if (!ddata || !input) {
		error = -ENOMEM;
		goto fail0;
	}

	platform_set_drvdata(pdev, ddata);

	input->name = pdev->name;
	input->phys = "gpio-keys/input0";
	input->dev.parent = dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);
	ddata->nbuttons = pdata->nbuttons;
	ddata->input = input;
	if(pdata->chn >= 0) {
		ddata->client = adc_register(pdata->chn, callback, (void *)ddata);
		if(!ddata->client) {
			error = -EINVAL;
			goto fail1;
		}
		setup_timer(&ddata->timer,
			    	adc_timer, (unsigned long)ddata);
		mod_timer(&ddata->timer, jiffies + msecs_to_jiffies(100));
	}
	for (i = 0; i < pdata->nbuttons; i++) {
		struct rk29_keys_button *button = &pdata->buttons[i];
		struct rk29_button_data *bdata = &ddata->data[i];
		int irq;
		unsigned int type = EV_KEY;

		bdata->input = input;
		bdata->button = button;
		if(button->code_long_press)
			setup_timer(&bdata->timer,
			    	keys_long_press_timer, (unsigned long)bdata);
		else if(button->code)
			setup_timer(&bdata->timer,
			    	keys_timer, (unsigned long)bdata);
		if(button->gpio != INVALID_GPIO) {
			error = gpio_request(button->gpio, button->desc ?: "keys");
			if (error < 0) {
				pr_err("gpio-keys: failed to request GPIO %d,"
					" error %d\n", button->gpio, error);
				goto fail2;
			}

			error = gpio_direction_input(button->gpio);
			if (error < 0) {
				pr_err("gpio-keys: failed to configure input"
					" direction for GPIO %d, error %d\n",
					button->gpio, error);
				gpio_free(button->gpio);
				goto fail2;
			}

			irq = gpio_to_irq(button->gpio);
			if (irq < 0) {
				error = irq;
				pr_err("gpio-keys: Unable to get irq number"
					" for GPIO %d, error %d\n",
					button->gpio, error);
				gpio_free(button->gpio);
				goto fail2;
			}

			error = request_irq(irq, keys_isr,
					    (button->active_low)?IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING,
					    button->desc ? button->desc : "keys",
					    bdata);
			if (error) {
				pr_err("gpio-keys: Unable to claim irq %d; error %d\n",
					irq, error);
				gpio_free(button->gpio);
				goto fail2;
			}
		}
		if (button->wakeup)
			wakeup = 1;

		input_set_capability(input, type, button->code);
	}

	input_set_capability(input, EV_KEY, KEY_WAKEUP);

	error = input_register_device(input);
	if (error) {
		pr_err("gpio-keys: Unable to register input device, "
			"error: %d\n", error);
		goto fail2;
	}

	device_init_wakeup(dev, wakeup);
	error = device_create_file(dev, &dev_attr_get_adc_value);

	error = device_create_file(dev, &dev_attr_rk29key);
	if(error )
	{
		pr_err("failed to create key file error: %d\n", error);
	}


	input_dev = input;
	return error;

 fail2:
	while (--i >= 0) {
		free_irq(gpio_to_irq(pdata->buttons[i].gpio), &ddata->data[i]);
		del_timer_sync(&ddata->data[i].timer);
		gpio_free(pdata->buttons[i].gpio);
	}
	if(pdata->chn >= 0 && ddata->client);
		adc_unregister(ddata->client);
	if(pdata->chn >= 0)
	        del_timer_sync(&ddata->timer);
 fail1:
 	platform_set_drvdata(pdev, NULL);
 fail0:
	input_free_device(input);
	kfree(ddata);

	return error;
}

static int __devexit keys_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk29_keys_platform_data *pdata = dev_get_platdata(dev);
	struct rk29_keys_drvdata *ddata = dev_get_drvdata(dev);
	struct input_dev *input = ddata->input;
	int i;

	input_dev = NULL;
	device_init_wakeup(dev, 0);

	for (i = 0; i < pdata->nbuttons; i++) {
		int irq = gpio_to_irq(pdata->buttons[i].gpio);
		free_irq(irq, &ddata->data[i]);
		del_timer_sync(&ddata->data[i].timer);
		gpio_free(pdata->buttons[i].gpio);
	}
	if(pdata->chn >= 0 && ddata->client);
		adc_unregister(ddata->client);
	input_unregister_device(input);

	return 0;
}


#ifdef CONFIG_PM
static int keys_suspend(struct device *dev)
{
	struct rk29_keys_platform_data *pdata = dev_get_platdata(dev);
	struct rk29_keys_drvdata *ddata = dev_get_drvdata(dev);
	int i;

	ddata->in_suspend = true;

	if (device_may_wakeup(dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct rk29_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq = gpio_to_irq(button->gpio);
				enable_irq_wake(irq);
			}
		}
	}

	return 0;
}

static int keys_resume(struct device *dev)
{
	struct rk29_keys_platform_data *pdata = dev_get_platdata(dev);
	struct rk29_keys_drvdata *ddata = dev_get_drvdata(dev);
	int i;

	if (device_may_wakeup(dev)) {
		for (i = 0; i < pdata->nbuttons; i++) {
			struct rk29_keys_button *button = &pdata->buttons[i];
			if (button->wakeup) {
				int irq = gpio_to_irq(button->gpio);
				disable_irq_wake(irq);
			}
		}
	}

	ddata->in_suspend = false;

	return 0;
}

static const struct dev_pm_ops keys_pm_ops = {
	.suspend	= keys_suspend,
	.resume		= keys_resume,
};
#endif

static struct platform_driver keys_device_driver = {
	.probe		= keys_probe,
	.remove		= __devexit_p(keys_remove),
	.driver		= {
		.name	= "rk29-keypad",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &keys_pm_ops,
#endif
	}
};

static int __init keys_init(void)
{
	return platform_driver_register(&keys_device_driver);
}

static void __exit keys_exit(void)
{
	platform_driver_unregister(&keys_device_driver);
}

module_init(keys_init);
module_exit(keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Phil Blundell <pb@handhelds.org>");
MODULE_DESCRIPTION("Keyboard driver for CPU GPIOs");
MODULE_ALIAS("platform:gpio-keys");

/*
 * linux/drivers/input/keyboard/rk28_adckey.c
 *
 * This driver program support to AD key which use for rk28 chip
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/gpio.h>
#include <mach/adc.h>
#include <mach/board.h>

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define KEY_PHYS_NAME	"rk2818_adckey/input0"

volatile int gADSampleTimes = 0;
volatile int gStatePlaykey = 0;
volatile unsigned int gCodeCount = 0;
volatile unsigned int gThisCode = 0;
volatile unsigned int gLastCode = 0;
volatile unsigned int gFlagShortPlay = 0;
volatile unsigned int gFlagLongPlay = 0;
volatile unsigned int gPlayCount = 0;

//key code tab
struct rk28_adckey 
{
	struct semaphore	lock;
	struct rk28_adc_client	*client;
	struct input_dev *input_dev;
	struct timer_list timer;
	unsigned char * keycodes;
	void __iomem *mmio_base;
};

struct rk28_adckey *pRk28AdcKey;

unsigned int rk28_get_keycode(unsigned int advalue,pADC_keyst ptab,struct adc_key_data *rk2818_adckey_data)
{	
	while(ptab->adc_value != 0)
	{
		if((advalue > ptab->adc_value - rk2818_adckey_data->adc_drift) && (advalue < ptab->adc_value + rk2818_adckey_data->adc_drift))
		    return ptab->adc_keycode;
		ptab++;
	}

	return 0;
}

static irqreturn_t rk28_playkey_irq(int irq, void *handle)
{ 
	
	//gFlagPlay = 1;	
	//DBG("Enter::%s,LINE=%d,KEY_PLAY_SHORT_PRESS=%d\n",__FUNCTION__,__LINE__,KEY_PLAY_SHORT_PRESS);
	
	return IRQ_HANDLED;
}

void rk28_send_wakeup_key( void ) 
{
    input_report_key(pRk28AdcKey->input_dev,KEY_WAKEUP,1);
    input_sync(pRk28AdcKey->input_dev);
    input_report_key(pRk28AdcKey->input_dev,KEY_WAKEUP,0);
    input_sync(pRk28AdcKey->input_dev);
	DBG("Wake up system\n");
}

static int rk28_adckey_open(struct input_dev *dev)
{
	//struct rk28_adckey *adckey = input_get_drvdata(dev);

	return 0;
}

static void rk28_adckey_close(struct input_dev *dev)
{
	//struct rk28_adckey *adckey = input_get_drvdata(dev);
//
}

#ifdef CONFIG_PM
static int rk28_adckey_suspend(struct platform_device *pdev, pm_message_t state)
{
	//struct rk28_adckey *adckey = platform_get_drvdata(pdev);

	return 0;
}

static int rk28_adckey_resume(struct platform_device *pdev)
{
	//struct rk28_adckey *adckey = platform_get_drvdata(pdev);
	//struct input_dev *input_dev = adckey->input_dev;
#if 0
	mutex_lock(&input_dev->mutex);

	mutex_unlock(&input_dev->mutex);
#endif
	return 0;
}
#else
#define rk28_adckey_suspend	NULL
#define rk28_adckey_resume	NULL
#endif
static void rk28_adkeyscan_timer(unsigned long data)
{
	unsigned int adcvalue = -1, code;
	struct adc_key_data *rk2818_adckey_data = (struct adc_key_data *)data;
	
	pRk28AdcKey->timer.expires  = jiffies + msecs_to_jiffies(10);
	add_timer(&pRk28AdcKey->timer);

	/*handle long press of play key*/
	if(gpio_get_value(rk2818_adckey_data->pin_playon) == rk2818_adckey_data->playon_level)
	{
		if(++gPlayCount > 20000)
			gPlayCount = 101;
		if((1 == gPlayCount) && (0 == gFlagShortPlay))
		{
			gFlagShortPlay = 1;			
		}
		else if((100 == gPlayCount) && (0 == gFlagLongPlay))
		{
			gFlagLongPlay = 1;
			gFlagShortPlay = 0;	
			input_report_key(pRk28AdcKey->input_dev,KEY_PLAY_LONG_PRESS,1);
			input_sync(pRk28AdcKey->input_dev);
			DBG("Enter::%s,LINE=%d,KEY_PLAY_LONG_PRESS=%d,1\n",__FUNCTION__,__LINE__,KEY_PLAY_LONG_PRESS);
		}
	}
	else
	{
		if (1 == gFlagShortPlay) 
		{
			input_report_key(pRk28AdcKey->input_dev,ENDCALL,1);
		    input_sync(pRk28AdcKey->input_dev);
		    input_report_key(pRk28AdcKey->input_dev,ENDCALL,0);
		    input_sync(pRk28AdcKey->input_dev);
			DBG("Wake up system,ENDCALL=%d\n",ENDCALL);
			
			input_report_key(pRk28AdcKey->input_dev,KEY_PLAY_SHORT_PRESS,1);
			input_sync(pRk28AdcKey->input_dev);
			DBG("Enter::%s,LINE=%d,KEY_PLAY_SHORT_PRESS=%d,1\n",__FUNCTION__,__LINE__,KEY_PLAY_SHORT_PRESS);
			input_report_key(pRk28AdcKey->input_dev,KEY_PLAY_SHORT_PRESS,0);
			input_sync(pRk28AdcKey->input_dev);
			DBG("Enter::%s,LINE=%d,KEY_PLAY_SHORT_PRESS=%d,0\n",__FUNCTION__,__LINE__,KEY_PLAY_SHORT_PRESS);
		}	
		else if(1 == gFlagLongPlay)
		{
			input_report_key(pRk28AdcKey->input_dev,KEY_PLAY_LONG_PRESS,0);
			input_sync(pRk28AdcKey->input_dev);
			DBG("Enter::%s,LINE=%d,KEY_PLAY_LONG_PRESS=%d,0\n",__FUNCTION__,__LINE__,KEY_PLAY_LONG_PRESS);
		}
		
		gFlagShortPlay = 0;	
		gFlagLongPlay = 0;
		gPlayCount = 0;
	}

	/*handle long press of adc key*/
	if (gADSampleTimes < 4)
	{
		gADSampleTimes ++;
		return;
	}
	
	gADSampleTimes = 0;

	//rk28_read_adc(pRk28AdcKey);	
	adcvalue = gAdcValue[rk2818_adckey_data->adc_chn];
	//printk("=========== adcvalue=0x%x ===========\n",adcvalue);

	if((adcvalue > rk2818_adckey_data->adc_empty) || (adcvalue < rk2818_adckey_data->adc_invalid))
	{
	    //DBG("adcvalue invalid !!!\n");
		if(gLastCode == 0) {
			return;
		}
		else
		{
			if(gLastCode == KEYMENU)
			{
				if(gCodeCount > 31)
				{
					input_report_key(pRk28AdcKey->input_dev,ENDCALL,0);
					input_sync(pRk28AdcKey->input_dev);
					DBG("Enter::%s,LINE=%d,code=%d,ENDCALL,0\n",__FUNCTION__,__LINE__,gLastCode);
				}	
				input_report_key(pRk28AdcKey->input_dev,gLastCode,1);
				input_sync(pRk28AdcKey->input_dev);
				DBG("Enter::%s,LINE=%d,code=%d,1\n",__FUNCTION__,__LINE__,gLastCode);
			}
			
			input_report_key(pRk28AdcKey->input_dev,gLastCode,0);
			input_sync(pRk28AdcKey->input_dev);
			DBG("Enter::%s,LINE=%d,code=%d,0\n",__FUNCTION__,__LINE__,gLastCode);
			gLastCode = 0;
			gCodeCount = 0;
			return;
		}
	}
	
	//DBG("adcvalue=0x%x\n",adcvalue);
	
	code=rk28_get_keycode(adcvalue,rk2818_adckey_data->adc_key_table,rk2818_adckey_data);
	if(code)
	{
		if(code == KEYMENU)
		{
			gLastCode = code;
			if(++gCodeCount == 31)//40ms * 30 =1.2s 
			{
				input_report_key(pRk28AdcKey->input_dev,ENDCALL,1);
	    		input_sync(pRk28AdcKey->input_dev);
				DBG("Enter::%s,LINE=%d,code=%d,ENDCALL,1\n",__FUNCTION__,__LINE__,code);
			}
		}
		else
		{
			gLastCode = code;
			if(++gCodeCount == 2)//only one event once one touch 
			{
				input_report_key(pRk28AdcKey->input_dev,code,1);
				input_sync(pRk28AdcKey->input_dev);
				DBG("Enter::%s,LINE=%d,code=%d,1\n",__FUNCTION__,__LINE__,code);
			}
		}
		
	}

}


static int __devinit rk28_adckey_probe(struct platform_device *pdev)
{
	struct rk28_adckey *adckey;
	struct input_dev *input_dev;
	int error,i,irq_num;
	struct rk2818_adckey_platform_data *pdata = pdev->dev.platform_data;

	if (!(pdata->adc_key))
		return -1;
	
	adckey = kzalloc(sizeof(struct rk28_adckey), GFP_KERNEL);
	if (adckey == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	
	//memcpy(adckey->keycodes, gInitKeyCode, sizeof(adckey->keycodes));
	adckey->keycodes = pdata->adc_key->initKeyCode;
	
	/* Create and register the input driver. */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		error = -ENOMEM;
		goto failed_free;
	}

	input_dev->name = pdata->name ? pdata->name : pdev->name;
	//input_dev->id.bustype = BUS_HOST;
	input_dev->open = rk28_adckey_open;
	input_dev->close = rk28_adckey_close;
	input_dev->dev.parent = &pdev->dev;
	input_dev->phys = KEY_PHYS_NAME;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->keycode = adckey->keycodes;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = pdata->adc_key->adc_key_cnt;
	for (i = 0; i < pdata->adc_key->adc_key_cnt; i++)
		set_bit(pdata->adc_key->initKeyCode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	adckey->input_dev = input_dev;
	input_set_drvdata(input_dev, adckey);

	input_dev->evbit[0] = BIT_MASK(EV_KEY);

//	rk28_adckey_build_keycode(adckey);
	platform_set_drvdata(pdev, adckey);

	pRk28AdcKey = adckey;

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed_free_dev;
	}

	error = gpio_request(pdata->adc_key->pin_playon, "play key gpio");
	if (error) {
		dev_err(&pdev->dev, "failed to request play key gpio\n");
		goto free_gpio;
	}
	
	irq_num = gpio_to_irq(pdata->adc_key->pin_playon);

    if(pdata->adc_key->playon_level)
    {
    	gpio_pull_updown(pdata->adc_key->pin_playon,GPIOPullDown);		
    	error = request_irq(irq_num,rk28_playkey_irq,IRQF_TRIGGER_RISING,NULL,NULL);
    	if(error)
    	{
    		printk("unable to request play key irq\n");
    		goto free_gpio_irq;
    	}
    }
    else
    {
    	gpio_pull_updown(pdata->adc_key->pin_playon,GPIOPullUp);		
    	error = request_irq(irq_num,rk28_playkey_irq,IRQF_TRIGGER_FALLING,NULL,NULL);  
    	if(error)
    	{
    		printk("unable to request play key irq\n");
    		goto free_gpio_irq;
    	}
    }
	
	enable_irq_wake(irq_num); // so play/wakeup key can wake up system

	setup_timer(&adckey->timer, rk28_adkeyscan_timer, (unsigned long)(pdata->adc_key));
	adckey->timer.expires  = jiffies+50;
	add_timer(&adckey->timer);
	printk(KERN_INFO "rk2818_adckey: driver initialized\n");
	return 0;
	
free_gpio_irq:
	free_irq(irq_num,NULL);
free_gpio:	
	gpio_free(pdata->adc_key->pin_playon);
failed_free_dev:
	platform_set_drvdata(pdev, NULL);
	input_free_device(input_dev);
failed_free:
	kfree(adckey);
	return error;
}

static int __devexit rk28_adckey_remove(struct platform_device *pdev)
{
	struct rk28_adckey *adckey = platform_get_drvdata(pdev);
	struct rk2818_adckey_platform_data *pdata = pdev->dev.platform_data;
	
	input_unregister_device(adckey->input_dev);
	input_free_device(adckey->input_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(adckey);
	free_irq(gpio_to_irq(pdata->adc_key->pin_playon), NULL);
	gpio_free(pdata->adc_key->pin_playon);
	return 0;
}

static struct platform_driver rk28_adckey_driver = 
{
	.probe		= rk28_adckey_probe,
	.remove 	= __devexit_p(rk28_adckey_remove),
	.suspend	= rk28_adckey_suspend,
	.resume 	= rk28_adckey_resume,
	.driver 	= {
		.name	= "rk2818-adckey",
		.owner	= THIS_MODULE,
	},
};

 int __init rk28_adckey_init(void)
{
	return platform_driver_register(&rk28_adckey_driver);
}

static void __exit rk28_adckey_exit(void)
{
	platform_driver_unregister(&rk28_adckey_driver);
}

module_init(rk28_adckey_init);
module_exit(rk28_adckey_exit);

MODULE_DESCRIPTION("rk2818 adc Key Controller Driver");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");


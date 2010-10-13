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

#if 1
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

//#define KEY_PHYS_NAME	"rk2818_adckey/input0"

static unsigned int gLastKeyCode = 0;
static int gSampleTimes = 0;
static int gADValue = 0;
static struct rk28_hskey *phsKey;

//key code tab
struct rk28_hskey 
{
	struct input_dev *input_dev;
	struct timer_list timer;
	unsigned char * keycodes;
};

extern int headset_status(void);
extern unsigned int rk28_get_keycode(unsigned int advalue,pADC_keyst ptab,struct adc_key_data *rk2818_adckey_data);

#if 0
unsigned int rk28_get_keycode(unsigned int advalue,pADC_keyst ptab,struct adc_key_data *rk2818_adckey_data)
{	
	while(ptab->adc_keycode != 0)
	{
	    if((ptab->adc_value == 0)&&(advalue >= 0 && advalue <= 5))
	        return ptab->adc_keycode;
		if((advalue > ptab->adc_value - rk2818_adckey_data->adc_drift) && (advalue < ptab->adc_value + rk2818_adckey_data->adc_drift))
		    return ptab->adc_keycode;
		ptab++;
	}

	return 0;
}

void rk28_send_wakeup_key( void ) 
{
    input_report_key(phsKey->input_dev,KEY_WAKEUP,1);
    input_sync(phsKey->input_dev);
    input_report_key(phsKey->input_dev,KEY_WAKEUP,0);
    input_sync(phsKey->input_dev);
	DBG("Wake up system\n");
}
#endif

static int rk28_Hskey_open(struct input_dev *dev)
{
	//struct rk28_adckey *adckey = input_get_drvdata(dev);
    DBG("===========rk28_Hskey_open===========\n");
	return 0;
}

static void rk28_Hskey_close(struct input_dev *dev)
{
    DBG("===========rk28_Hskey_close===========\n");
	//struct rk28_adckey *adckey = input_get_drvdata(dev);
//
}

#ifdef CONFIG_PM
static int rk28_hskey_suspend(struct platform_device *pdev, pm_message_t state)
{
	//struct rk28_adckey *adckey = platform_get_drvdata(pdev);

	return 0;
}

static int rk28_hskey_resume(struct platform_device *pdev)
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
#define rk28_hskey_suspend	NULL
#define rk28_hskey_resume	NULL
#endif
static void rk28_hskeyscan_timer(unsigned long data)
{
	unsigned int adcvalue = 0x3FF,code = 0;
	struct adc_key_data *rk2818_adckey_data = (struct adc_key_data *)data;
	
	phsKey->timer.expires  = jiffies + msecs_to_jiffies(10);
	add_timer(&phsKey->timer);

	if(headset_status())
	{
	    if(gSampleTimes<4)
	    {
            gADValue += gAdcValue[rk2818_adckey_data->adc_chn];
            gSampleTimes++;
	    }
        else
        {
            gADValue = gADValue/4;
            code=rk28_get_keycode(gADValue,rk2818_adckey_data->adc_key_table,rk2818_adckey_data);
            gADValue = 0;
            gSampleTimes = 0;
    	    
        	if(code && !gLastKeyCode)
        	{
        	    gLastKeyCode = code;
    			input_report_key(phsKey->input_dev,gLastKeyCode,1);
    			input_sync(phsKey->input_dev);
    			DBG("===========headset key press code=%d ===========\n",code);
        	}
        	else if(!code && gLastKeyCode)
        	{
        	    input_report_key(phsKey->input_dev,gLastKeyCode,0);
    			input_sync(phsKey->input_dev);
    			gLastKeyCode = 0;
    			DBG("===========headset key release code=%d ===========\n",code);
        	}
    	}
    }
    else
    {
        if(gLastKeyCode)
        {
            input_report_key(phsKey->input_dev,code,0);
			input_sync(phsKey->input_dev);
			gLastKeyCode = 0;
			DBG("===========headset key release code=%d ===========\n",code);
        }
        gADValue = 0;
	    gSampleTimes = 0;
    }
}

static int __devinit rk28_adckey_probe(struct platform_device *pdev)
{
	struct rk28_hskey *hskey;
	struct input_dev *input_dev;
	int error,i,irq_num;
	struct rk2818_adckey_platform_data *pdata = pdev->dev.platform_data;

	if (!(pdata->adc_key))
		return -1;
	
	hskey = kzalloc(sizeof(struct rk28_hskey), GFP_KERNEL);
	if (hskey == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	
	hskey->keycodes = pdata->adc_key->initKeyCode;
	
	/* Create and register the input driver. */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		error = -ENOMEM;
		goto failed_free;
	}

	input_dev->name = pdev->name;
	input_dev->open = rk28_Hskey_open;
	input_dev->close = rk28_Hskey_close;
	input_dev->dev.parent = &pdev->dev;
	//input_dev->phys = KEY_PHYS_NAME;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->keycode = hskey->keycodes;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = pdata->adc_key->adc_key_cnt;
	for (i = 0; i < pdata->adc_key->adc_key_cnt; i++)
		set_bit(pdata->adc_key->initKeyCode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	hskey->input_dev = input_dev;
	input_set_drvdata(input_dev, hskey);

	input_dev->evbit[0] = BIT_MASK(EV_KEY);

	platform_set_drvdata(pdev, hskey);

    phsKey = hskey;
    
	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed_free_dev;
	}
		
	setup_timer(&hskey->timer, rk28_hskeyscan_timer, (unsigned long)(pdata->adc_key));
	hskey->timer.expires  = jiffies+50;
	add_timer(&hskey->timer);
	printk(KERN_INFO "rk2818_hskey: driver initialized\n");
	return 0;
	
free_gpio_irq:
	free_irq(irq_num,NULL);
free_gpio:	
	gpio_free(pdata->adc_key->pin_playon);
failed_free_dev:
	platform_set_drvdata(pdev, NULL);
	input_free_device(input_dev);
failed_free:
	kfree(hskey);
	return error;
}

static int __devexit rk28_adckey_remove(struct platform_device *pdev)
{
	struct rk28_hskey *adckey = platform_get_drvdata(pdev);
	struct rk2818_adckey_platform_data *pdata = pdev->dev.platform_data;
	
	input_unregister_device(adckey->input_dev);
	input_free_device(adckey->input_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(adckey);
	return 0;
}

static struct platform_driver rk28_hskey_driver = 
{
	.probe		= rk28_adckey_probe,
	.remove 	= __devexit_p(rk28_adckey_remove),
	.suspend	= rk28_hskey_suspend,
	.resume 	= rk28_hskey_resume,
	.driver 	= {
		.name	= "rk2818-hskey",
		.owner	= THIS_MODULE,
	},
};

int __init rk28_hskey_init(void)
{
	return platform_driver_register(&rk28_hskey_driver);
}

static void __exit rk28_hskey_exit(void)
{
	platform_driver_unregister(&rk28_hskey_driver);
}

module_init(rk28_hskey_init);
module_exit(rk28_hskey_exit);

MODULE_DESCRIPTION("rk2818 headset Key Controller Driver");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");


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

#include <mach/adc.h>

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

//ROCKCHIP AD KEY CODE ,for demo board
//      key		--->	EV	
#define AD2KEY1			114   ///VOLUME_DOWN 
#define AD2KEY2 		115   ///VOLUME_UP
#define AD2KEY3 		59    ///MENU
#define AD2KEY4 		102   ///HOME
#define AD2KEY5 		158   ///BACK
#define AD2KEY6 		61    ///CALL

#define KEYMENU			AD2KEY6
#define ENDCALL			62

#define Valuedrift		50
#define ADEmpty			1000
#define ADInvalid		20
#define ADKEYNUM		6

#define ADKEYCH			1	//ADÍ¨µÀ

#define KEY_PHYS_NAME	"rk2818_adckey/input0"

volatile int gADSampleTimes = 0;
volatile int gAdcChanel = 0;
volatile int gAdcValue[4]={0, 0, 0, 0};	//0->ch0 1->ch1 2->ch2 3->ch3

volatile unsigned int gCodeCount = 0;
volatile unsigned int gThisCode = 0;
volatile unsigned int gLastCode = 0;

//ADC Registers
typedef  struct tagADC_keyst
{
	unsigned int adc_value;
	unsigned int adc_keycode;
}ADC_keyst,*pADC_keyst;

//	adc	 ---> key	
static  ADC_keyst gAdcValueTab[] = 
{
	{95,  AD2KEY1},
	{249, AD2KEY2},
	{406, AD2KEY3},
	{561, AD2KEY4},
	{726, AD2KEY5},
	{899, AD2KEY6},
	{ADEmpty,0}
};

//key code tab
static unsigned char gInitKeyCode[ADKEYNUM] = 
{
	AD2KEY1,AD2KEY2,AD2KEY3,AD2KEY4,AD2KEY5,AD2KEY6	
};


struct rk28_adckey 
{
	struct semaphore	lock;
	struct rk28_adc_client	*client;
	struct input_dev *input_dev;
	struct timer_list timer;
	unsigned char keycodes[ADKEYNUM];
	struct clk *clk;
	void __iomem *mmio_base;
};

struct rk28_adckey *pRk28AdcKey;

unsigned int rk28_get_keycode(unsigned int advalue,pADC_keyst ptab)
{	
	while(ptab->adc_value != ADEmpty)
	{
		if((advalue > ptab->adc_value - Valuedrift) && (advalue < ptab->adc_value + Valuedrift))
		return ptab->adc_keycode;
		ptab++;
	}

	return 0;
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
	struct rk28_adckey *adckey = platform_get_drvdata(pdev);

	clk_disable(adckey->clk);
	return 0;
}

static int rk28_adckey_resume(struct platform_device *pdev)
{
	struct rk28_adckey *adckey = platform_get_drvdata(pdev);
	struct input_dev *input_dev = adckey->input_dev;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users) {
		/* Enable unit clock */
		clk_enable(adckey->clk);
	}

	mutex_unlock(&input_dev->mutex);

	return 0;
}
#else
#define rk28_adckey_suspend	NULL
#define rk28_adckey_resume	NULL
#endif

//read four ADC chanel
static int rk28_read_adc(struct rk28_adckey *adckey)
{
	int ret;

	ret = down_interruptible(&adckey->lock);
	if (ret < 0)
		return ret;	
	if(gAdcChanel > 3)
		gAdcChanel = 0;
	gAdcValue[gAdcChanel] = rk28_adc_read(adckey->client, gAdcChanel);
	//DBG("Enter::%s,LINE=%d,gAdcValue[%d]=%d\n",__FUNCTION__,__LINE__,gAdcChanel,gAdcValue[gAdcChanel]);
	gAdcChanel++;
	up(&adckey->lock);
	return ret;
}

static void rk28_adkeyscan_timer(unsigned long data)
{
	unsigned int adcvalue = -1, code;

	pRk28AdcKey->timer.expires  = jiffies + msecs_to_jiffies(10);
	add_timer(&pRk28AdcKey->timer);
	
	if (gADSampleTimes < 4)
	{
		gADSampleTimes ++;
		return;
	}
	
	gADSampleTimes = 0;

	rk28_read_adc(pRk28AdcKey);	
	adcvalue = gAdcValue[ADKEYCH];
	if((adcvalue > ADEmpty) || (adcvalue < ADInvalid))
	{
		if(gLastCode == 0)
		return;
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
	
	DBG("adcvalue=0x%x\n",adcvalue);
	
	code=rk28_get_keycode(adcvalue,gAdcValueTab);
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
	int error,i;

	adckey = kzalloc(sizeof(struct rk28_adckey), GFP_KERNEL);
	if (adckey == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	
	memcpy(adckey->keycodes, gInitKeyCode, sizeof(adckey->keycodes));
	
	/* Create and register the input driver. */
	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		error = -ENOMEM;
		goto failed_put_clk;
	}

	input_dev->name = pdev->name;
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
	input_dev->keycodemax = ARRAY_SIZE(gInitKeyCode);
	for (i = 0; i < ARRAY_SIZE(gInitKeyCode); i++)
		set_bit(gInitKeyCode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	adckey->input_dev = input_dev;
	input_set_drvdata(input_dev, adckey);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) ;

//	rk28_adckey_build_keycode(adckey);
	platform_set_drvdata(pdev, adckey);

	init_MUTEX(&adckey->lock);

	/* Register with the core ADC driver. */
	adckey->client = rk28_adc_register(pdev, NULL, NULL, 0);
	if (IS_ERR(adckey->client)) {
		dev_err(&pdev->dev, "cannot register adc\n");
		error = PTR_ERR(adckey->client);
		goto failed_free;
	}

	pRk28AdcKey = adckey;

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto failed_free_dev;
	}

	setup_timer(&adckey->timer, rk28_adkeyscan_timer, (unsigned long)adckey);
	adckey->timer.expires  = jiffies+50;
	add_timer(&adckey->timer);
	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
	return 0;

failed_free_dev:
	platform_set_drvdata(pdev, NULL);
	input_free_device(input_dev);
failed_put_clk:
	//clk_put(adckey->clk);
failed_free:
	kfree(adckey);
	return error;
}

static int __devexit rk28_adckey_remove(struct platform_device *pdev)
{
	struct rk28_adckey *adckey = platform_get_drvdata(pdev);

	input_unregister_device(adckey->input_dev);
	input_free_device(adckey->input_dev);
	platform_set_drvdata(pdev, NULL);
	rk28_adc_release(adckey->client);
	kfree(adckey);
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
	DBG("Enter::%s,LINE=%d\n",__FUNCTION__,__LINE__);
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


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

#if 0 
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

//ROCKCHIP AD KEY CODE ,for demo board
//      key		--->	EV	
#define AD2KEY1                 114   ///VOLUME_DOWN
#define AD2KEY2                 115   ///VOLUME_UP
#define AD2KEY3                 59    ///MENU
#define AD2KEY4                 102   ///HOME
#define AD2KEY5                 158   ///BACK
#define AD2KEY6                 61    ///CALL

#define ENDCALL					62
#define	KEYSTART				28			//ENTER
#define KEYMENU					AD2KEY6		///CALL
#define KEY_PLAYON_PIN			RK2818_PIN_PE1
#define	KEY_PLAY_SHORT_PRESS	KEYSTART	//code for short press the play key
#define	KEY_PLAY_LONG_PRESS		ENDCALL		//code for long press the play key


#define Valuedrift		50
#ifndef CONFIG_MACH_RK2818PHONE
#define ADEmpty			1000
#else
#define ADEmpty			900
#endif
#define ADInvalid		20
#define ADKEYNUM		10

#define ADKEYCH			1	//AD通道
#define KEYPLAY_ON		0	//按键接通时的电平值
#define KEY_PHYS_NAME	"rk2818_adckey/input0"

volatile int gADSampleTimes = 0;
volatile int gStatePlaykey = 0;

volatile unsigned int gCodeCount = 0;
volatile unsigned int gThisCode = 0;
volatile unsigned int gLastCode = 0;
volatile unsigned int gFlagShortPlay = 0;
volatile unsigned int gFlagLongPlay = 0;
volatile unsigned int gPlayCount = 0;
//ADC Registers
typedef  struct tagADC_keyst
{
	unsigned int adc_value;
	unsigned int adc_keycode;
}ADC_keyst,*pADC_keyst;

#ifndef CONFIG_MACH_RK2818PHONE
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
#else
static  ADC_keyst gAdcValueTab[] = 
{
	{95,  AD2KEY1},
	{192, AD2KEY2},
	{280, AD2KEY3},
	{376, AD2KEY4},
	{467, AD2KEY5},
	{560, AD2KEY6},
	{ADEmpty,0}
};
#endif

//key code tab
static unsigned char gInitKeyCode[ADKEYNUM] = 
{
	AD2KEY1,AD2KEY2,AD2KEY3,AD2KEY4,AD2KEY5,AD2KEY6,	
	ENDCALL,KEYSTART,KEY_WAKEUP,
};


struct rk28_adckey 
{
	struct semaphore	lock;
	struct rk28_adc_client	*client;
	struct input_dev *input_dev;
	struct timer_list timer;
	unsigned char keycodes[ADKEYNUM];
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

#if 1
static irqreturn_t rk28_playkey_irq(int irq, void *handle)
{ 
	
	//gFlagPlay = 1;	
	//DBG("Enter::%s,LINE=%d,KEY_PLAY_SHORT_PRESS=%d\n",__FUNCTION__,__LINE__,KEY_PLAY_SHORT_PRESS);
	
	return IRQ_HANDLED;
}

#endif
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

	pRk28AdcKey->timer.expires  = jiffies + msecs_to_jiffies(10);
	add_timer(&pRk28AdcKey->timer);

	/*handle long press of play key*/
	if(gpio_get_value(KEY_PLAYON_PIN) == KEYPLAY_ON) 
	{
		if(++gPlayCount > 20000)
			gPlayCount = 101;
		if((2 == gPlayCount) && (0 == gFlagShortPlay))
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
	adcvalue = gAdcValue[ADKEYCH];
	DBG("=========== adcvalue=0x%x ===========\n",adcvalue);

	if((adcvalue > ADEmpty) || (adcvalue < ADInvalid))
	{
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
		goto failed_free;
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

	error = gpio_request(KEY_PLAYON_PIN, "play key gpio");
	if (error) {
		dev_err(&pdev->dev, "failed to request play key gpio\n");
		goto free_gpio;
	}
#if KEYPLAY_ON	
	gpio_pull_updown(KEY_PLAYON_PIN,GPIOPullDown);
	error = request_irq(gpio_to_irq(KEY_PLAYON_PIN),rk28_playkey_irq,IRQF_TRIGGER_RISING,NULL,NULL);
	if(error)
	{
		printk("unable to request play key irq\n");
		goto free_gpio_irq;
	}	
#else
	gpio_pull_updown(KEY_PLAYON_PIN,GPIOPullUp);
	error = request_irq(gpio_to_irq(KEY_PLAYON_PIN),rk28_playkey_irq,IRQF_TRIGGER_FALLING,NULL,NULL);  
	if(error)
	{
		printk("unable to request play key irq\n");
		goto free_gpio_irq;
	}
#endif

	enable_irq_wake(gpio_to_irq(KEY_PLAYON_PIN)); // so play/wakeup key can wake up system

#if 0
	error = gpio_direction_input(KEY_PLAYON_PIN);
	if (error) 
	{
		printk("failed to set gpio KEY_PLAYON_PIN input\n");
		goto free_gpio_irq;
	}
#endif
	setup_timer(&adckey->timer, rk28_adkeyscan_timer, (unsigned long)adckey);
	adckey->timer.expires  = jiffies+50;
	add_timer(&adckey->timer);
	printk(KERN_INFO "rk2818_adckey: driver initialized\n");
	return 0;
	
free_gpio_irq:
	free_irq(gpio_to_irq(KEY_PLAYON_PIN),NULL);
free_gpio:	
	gpio_free(KEY_PLAYON_PIN);
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

	input_unregister_device(adckey->input_dev);
	input_free_device(adckey->input_dev);
	platform_set_drvdata(pdev, NULL);
	kfree(adckey);
	free_irq(gpio_to_irq(KEY_PLAYON_PIN),NULL);
	gpio_free(KEY_PLAYON_PIN);
	
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


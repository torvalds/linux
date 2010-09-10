/*
 * linux/drivers/input/keyboard/rk28_jogball.c
 *
 * Driver for the rk28 matrix keyboard controller.
 *
 * Created: 2009-11-28
 * Author:	TY <ty@rockchip.com>
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
#include <linux/time.h>

#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/irq.h>


#ifdef CONFIG_ANDROID_POWER
#include <linux/android_power.h>

static android_early_suspend_t jogball_early_suspend;
#endif

/* Debug */
#define JB_DEBUG 1

#ifdef JB_DEBUG
#define DBG	printk
#else
#define DBG(x...)
#endif

#define JB_KEY_UP           103
#define JB_KEY_DOWN         108
#define JB_KEY_LEFT         105
#define JB_KEY_RIGHT        106

#define JOGBALL_PHYS_NAME	"rk28_jogball/input0"

#define JOGBALL_KEY_UP_IO       TCA6424_P06
#define JOGBALL_KEY_DOWN_IO     TCA6424_P07
#define JOGBALL_KEY_LEFT_IO     TCA6424_P10
#define JOGBALL_KEY_RIGHT_IO    TCA6424_P11

#define JOGBALL_MAX_CNT 2

static volatile int jogball_cnt_up = 0;
static volatile int jogball_cnt_down = 0;
static volatile int jogball_cnt_left = 0;
static volatile int jogball_cnt_right = 0;

//key code tab
static unsigned char initkey_code[ ] = 
{
	JB_KEY_UP, JB_KEY_DOWN, JB_KEY_LEFT, JB_KEY_RIGHT
};

struct rk28_jogball 
{
	struct input_dev *input_dev;
	unsigned char keycodes[5];
};

struct rk28_jogball *prockjogball;

//static void rk28_jogball_up_ISR(void)

static int rk28_jogball_disable_irq(void )
{
	//DBG("IN jogball suspend !!\n");
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_UP_IO));
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_DOWN_IO));
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_LEFT_IO));
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_RIGHT_IO));
	
	return 0;
}

static int rk28_jogball_enable_irq(void)
{
	//DBG("IN jogball resume !!\n");
	enable_irq (gpio_to_irq(JOGBALL_KEY_UP_IO));
	enable_irq (gpio_to_irq(JOGBALL_KEY_DOWN_IO));
	enable_irq (gpio_to_irq(JOGBALL_KEY_LEFT_IO));
	enable_irq (gpio_to_irq(JOGBALL_KEY_RIGHT_IO));

    return 0;
}
static irqreturn_t rk28_jogball_up_ISR(int irq, void *dev_id)
{	
	
	 rk28_jogball_disable_irq( );
	jogball_cnt_up++;

	if (jogball_cnt_up > JOGBALL_MAX_CNT){
		//printk("jogball: up\n");
		
		input_report_key(prockjogball->input_dev, JB_KEY_UP, 1);
		input_sync(prockjogball->input_dev);
		input_report_key(prockjogball->input_dev, JB_KEY_UP, 0);
		input_sync(prockjogball->input_dev);

		jogball_cnt_up = 0;
		jogball_cnt_down = 0;
		jogball_cnt_left = 0;
		jogball_cnt_right = 0;
	}
	
	//gpio_irq_enable(JOGBALL_KEY_UP_IO);
	//enable_irq (gpio_to_irq(JOGBALL_KEY_UP_IO));
	 rk28_jogball_enable_irq( );
	return IRQ_HANDLED;
}

//static void rk28_jogball_down_ISR(void)
static irqreturn_t rk28_jogball_down_ISR(int irq, void *dev_id)
{
	 rk28_jogball_disable_irq( );
	 //disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_DOWN_IO));
	jogball_cnt_down++;
	
	if (jogball_cnt_down > JOGBALL_MAX_CNT){
		//printk("jogball: down\n");
		
		input_report_key(prockjogball->input_dev, JB_KEY_DOWN, 1);
		input_sync(prockjogball->input_dev);
		input_report_key(prockjogball->input_dev, JB_KEY_DOWN, 0);
		input_sync(prockjogball->input_dev);
		
		jogball_cnt_up = 0;
		jogball_cnt_down = 0;
		jogball_cnt_left = 0;
		jogball_cnt_right = 0;
	}
	
	//gpio_irq_enable(JOGBALL_KEY_DOWN_IO);
	//enable_irq (gpio_to_irq(JOGBALL_KEY_DOWN_IO));
	 rk28_jogball_enable_irq( );
	return IRQ_HANDLED;
}

//static void rk28_jogball_left_ISR(void)
static irqreturn_t rk28_jogball_left_ISR(int irq, void *dev_id)
{
	 rk28_jogball_disable_irq( );
	//disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_LEFT_IO));
	jogball_cnt_left++;

	if (jogball_cnt_left > JOGBALL_MAX_CNT){
		//printk("jogball: left\n");
		
		input_report_key(prockjogball->input_dev, JB_KEY_LEFT, 1);
		input_sync(prockjogball->input_dev);
		input_report_key(prockjogball->input_dev, JB_KEY_LEFT, 0);
		input_sync(prockjogball->input_dev);

		jogball_cnt_up = 0;
		jogball_cnt_down = 0;
		jogball_cnt_left = 0;
		jogball_cnt_right = 0;
	}
	
	//gpio_irq_enable(JOGBALL_KEY_LEFT_IO);
	//enable_irq (gpio_to_irq(JOGBALL_KEY_LEFT_IO));
	 rk28_jogball_enable_irq( );
	return IRQ_HANDLED;
}

//static void rk28_jogball_right_ISR(void)
static irqreturn_t rk28_jogball_right_ISR(int irq, void *dev_id)
{
	 rk28_jogball_disable_irq( );
	//disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_RIGHT_IO));
	jogball_cnt_right++;

	if (jogball_cnt_right > JOGBALL_MAX_CNT){
		//printk("jogball: right\n");
		
		input_report_key(prockjogball->input_dev, JB_KEY_RIGHT, 1);
		input_sync(prockjogball->input_dev);
		input_report_key(prockjogball->input_dev, JB_KEY_RIGHT, 0);
		input_sync(prockjogball->input_dev);

		jogball_cnt_up = 0;
		jogball_cnt_down = 0;
		jogball_cnt_left = 0;
		jogball_cnt_right = 0;
	}
	
	//gpio_irq_enable(JOGBALL_KEY_RIGHT_IO);
	//enable_irq (gpio_to_irq(JOGBALL_KEY_RIGHT_IO));
	 rk28_jogball_enable_irq( );
	return IRQ_HANDLED;
}



#ifdef CONFIG_ANDROID_POWER

void rk28_jogball_early_suspend(android_early_suspend_t *h)
{
    DBG("IN jogball early suspend !!\n\n\n");
}

void rk28_jogball_early_resume(android_early_suspend_t *h)
{
    DBG("IN jogball early resume !!\n\n\n");
}

#endif

void rk28_jogball_shutdown(struct platform_device *dev)
{
    DBG("IN jogball early shutdown !!\n\n\n");
}

static int rk28_jogball_probe(struct platform_device *pdev)
{
  #if 1
      int    error, i;
	struct rk28_jogball *jogball = NULL;
	struct input_dev *input_dev = NULL;

	printk("***************rk28_jogball_probe...\n");
	
	jogball = kzalloc(sizeof(struct rk28_jogball), GFP_KERNEL);
	if (jogball == NULL)
	{
	    printk("Alloc memory for rk28_jogball failed.\n");
	    return -ENOMEM;
	}
	
	/* Create and register the input driver. */
	input_dev = input_allocate_device();
	if (!input_dev || !jogball) 
	{
		printk("failed to allocate input device.\n");
		error = -ENOMEM;
		goto failed1;
	}
	
	memcpy(jogball->keycodes, initkey_code, sizeof(jogball->keycodes));
	input_dev->name = "jogball";
	input_dev->dev.parent = &pdev->dev;
	input_dev->phys = JOGBALL_PHYS_NAME;
	input_dev->keycode = jogball->keycodes;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(initkey_code);
	for (i = 0; i < ARRAY_SIZE(initkey_code); i++)
		set_bit(initkey_code[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);
    input_dev->evbit[0] = BIT_MASK(EV_KEY);
    
	jogball->input_dev = input_dev;
	input_set_drvdata(input_dev, jogball);

	platform_set_drvdata(pdev, jogball);

	prockjogball = jogball;

	/* Register the input device */
	error = input_register_device(input_dev);
	if (error) 
	{
		printk("failed to register input device.\n");
		goto failed2;
	}
	printk(" irq register for input device.\n");
	#if 1	//ÉêÇëEXTERN GPIO INTERRUPT
	//JOG_UP_PORT
	

	error = gpio_request(JOGBALL_KEY_UP_IO,"Jog up");
	if(error)
	{
		printk("unable to request JOG_UP_PORT IRQ err=%d\n", error);
		goto failed3;
	}
	gpio_direction_input(JOGBALL_KEY_UP_IO);	
	error = request_irq(gpio_to_irq(JOGBALL_KEY_UP_IO),rk28_jogball_up_ISR,IRQ_TYPE_EDGE_RISING,NULL,jogball);
	if(error)
	{
		printk("unable to request JOG_UP_PORT irq\n");
		goto failed4;
	}	
	//JOG_DOWN_PORT
	error = gpio_request(JOGBALL_KEY_DOWN_IO,"jog down");
	if(error)
	{
		printk("unable to request JOG_DOWN_PORT IRQ err=%d\n", error);
		goto failed5;
	}
	gpio_direction_input(JOGBALL_KEY_DOWN_IO);
	//gpio_pull_updown(JOG_DOWN_PORT,GPIOPullUp);
	error = request_irq(gpio_to_irq(JOGBALL_KEY_DOWN_IO),rk28_jogball_down_ISR,IRQ_TYPE_EDGE_RISING,NULL,jogball);
	if(error)
	{
		printk("unable to request JOG_DOWN_PORT irq\n");
		goto failed6;
	}	
	//JOG_LEFT_PORT
	error = gpio_request(JOGBALL_KEY_LEFT_IO,"jog left");
	if(error)
	{
		printk("unable to request JOG_LEFT_PORT IRQ err=%d\n", error);
		goto failed7;
	}
	gpio_direction_input(JOGBALL_KEY_LEFT_IO);
	//gpio_pull_updown(JOG_LEFT_PORT,GPIOPullUp);
	error = request_irq(gpio_to_irq(JOGBALL_KEY_LEFT_IO),rk28_jogball_left_ISR,IRQ_TYPE_EDGE_RISING,NULL,jogball);
	if(error)
	{
		printk("unable to request JOG_LEFT_PORT irq\n");
		goto failed8;
	}	
	//JOG_RIGHT_PORT
	error = gpio_request(JOGBALL_KEY_RIGHT_IO,NULL);
	if(error)
	{
		printk("unable to request JOG_RIGHT_PORT IRQ err=%d\n", error);
		goto failed9;
	}
	gpio_direction_input(JOGBALL_KEY_RIGHT_IO);
	//gpio_pull_updown(JOG_RIGHT_PORT,GPIOPullUp);
	error = request_irq(gpio_to_irq(JOGBALL_KEY_RIGHT_IO),rk28_jogball_right_ISR,IRQ_TYPE_EDGE_RISING,NULL,jogball);
	if(error)
	{
		printk("unable to request JOG_RIGHT_PORT irq\n");
		goto failed10;
	}			
#endif

#ifdef CONFIG_ANDROID_POWER
    jogball_early_suspend.suspend = rk28_jogball_early_suspend;
    jogball_early_suspend.resume = rk28_jogball_early_resume;
    jogball_early_suspend.level = 0x2;
    android_register_early_suspend(&jogball_early_suspend);
#endif
#endif
	printk("******************rk28_jogball_probe end\n");
	return 0;
#if 1
failed10:
	free_irq(gpio_to_irq(JOGBALL_KEY_RIGHT_IO),NULL);
failed9:
	gpio_free(JOGBALL_KEY_RIGHT_IO);
failed8:
	free_irq(gpio_to_irq(JOGBALL_KEY_LEFT_IO),NULL);
failed7:
	gpio_free(JOGBALL_KEY_LEFT_IO);
failed6:		
	free_irq(gpio_to_irq(JOGBALL_KEY_DOWN_IO),NULL);
failed5:
	gpio_free(JOGBALL_KEY_DOWN_IO);
failed4:
	free_irq(gpio_to_irq(JOGBALL_KEY_UP_IO),NULL);
failed3:
	gpio_free(JOGBALL_KEY_UP_IO);
	
	input_unregister_device(jogball->input_dev);

failed2:
    platform_set_drvdata(pdev, NULL);
    input_free_device(input_dev);

failed1:
	kfree(jogball);
	
	return error;
#endif
}

static int __devexit rk28_jogball_remove(struct platform_device *pdev)
{
	struct rk28_jogball *jogball = platform_get_drvdata(pdev);

    platform_set_drvdata(pdev, NULL);
    
	input_unregister_device(jogball->input_dev);
	input_free_device(jogball->input_dev);
	kfree(jogball);
	
	return 0;
}

#ifdef CONFIG_PM

static int rk28_jogball_suspend(struct platform_device *pdev, pm_message_t state)
{
	DBG("IN jogball suspend !!\n");
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_UP_IO));
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_DOWN_IO));
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_LEFT_IO));
	disable_irq_nosync (gpio_to_irq(JOGBALL_KEY_RIGHT_IO));
	
	return 0;
}

static int rk28_jogball_resume(struct platform_device *pdev)
{
	DBG("IN jogball resume !!\n");
	enable_irq (gpio_to_irq(JOGBALL_KEY_UP_IO));
	enable_irq (gpio_to_irq(JOGBALL_KEY_DOWN_IO));
	enable_irq (gpio_to_irq(JOGBALL_KEY_LEFT_IO));
	enable_irq (gpio_to_irq(JOGBALL_KEY_RIGHT_IO));

    return 0;
}


#endif

static struct platform_driver rk28_jogball_driver = 
{
	.probe		= rk28_jogball_probe,
	.remove 	= __devexit_p(rk28_jogball_remove),
	.driver 	= {
		.name	= "rk2818_jogball",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk28_jogball_shutdown,
#ifdef CONFIG_PM
   .suspend    = rk28_jogball_suspend,
   .resume     = rk28_jogball_resume,
#endif
};

 int __init rk28_jogball_init(void)
{
	int ret;

	printk("****************JOGBALL inital\n");
	ret = platform_driver_register(&rk28_jogball_driver);
	if (ret < 0){
		printk("register rk28_jogball_driver failed!!\n");
	}
	printk("********************JOGBALL inital end\n");
	return ret;
}

static void __exit rk28_jogball_exit(void)
{
	platform_driver_unregister(&rk28_jogball_driver);
}

late_initcall(rk28_jogball_init);
module_exit(rk28_jogball_exit);

MODULE_DESCRIPTION("rk28 jogball Controller Driver");
MODULE_AUTHOR("Yi Tang && Yongle Lai");
MODULE_LICENSE("GPL");



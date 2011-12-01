/*
 * Backlight driver for Wolfson Microelectronics WM831x PMICs
 *
 * Copyright 2009 Wolfson Microelectonics plc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/slab.h>
#include <mach/gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/delay.h>
#include <linux/ktime.h>
#include "aw9364_bl.h"

/*
 * Debug
 */
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#define BL_SET   255
#define BL_INIT_VALUE 102
struct aw9364_backlight_data {
	int pin_en;
	int current_brightness;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct 	early_suspend early_suspend;
	struct delayed_work work;
	int suspend_flag;
	int shutdown_flag;
#endif

	spinlock_t bl_lock;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct backlight_device *g_aw9364_bl;
static struct aw9364_backlight_data *g_aw9364_data;
#endif

static int aw9364_backlight_set(struct backlight_device *bl, int brightness)
{
	struct aw9364_backlight_data *data = bl_get_data(bl);
	int i,num_clk, num_clk_to, num_clk_from;
	unsigned long flags;
		
	brightness = brightness & 0xff; //0-256

	num_clk_from = 16 -(data->current_brightness>>4);	
	num_clk_to = 16 -(brightness>>4);
	num_clk = (16 + num_clk_to - num_clk_from)%16;
	
	
	if(brightness < 16)
	{
		gpio_direction_output(data->pin_en, GPIO_LOW);
		mdelay(3);
	}
	else {
		spin_lock_irqsave(&data->bl_lock, flags);
		for(i=0; i<num_clk; i++)	//the wave should not be intterupted
		{
			gpio_set_value(data->pin_en, GPIO_LOW);	
			gpio_set_value(data->pin_en, GPIO_HIGH);
			if(i==0)
			udelay(30);	
		}
		spin_unlock_irqrestore(&data->bl_lock, flags);
	}
	
	DBG("%s:current_bl=%d,bl=%d,num_clk_to=%d,num_clk_from=%d,num_clk=%d\n",__FUNCTION__,
		data->current_brightness,brightness,num_clk_to,num_clk_from,num_clk);

	if((num_clk) || (brightness < 16))
	data->current_brightness = brightness;
	
	return 0;
	
}


static int aw9364_backlight_update_status(struct backlight_device *bl)
{

	int brightness = bl->props.brightness;
	
	if(g_aw9364_data->suspend_flag == 1)
		brightness = 0;
	
	if (g_aw9364_data->shutdown_flag == 1)
		brightness = 0;
		
	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.state & BL_CORE_SUSPENDED)
		brightness = 0;

	DBG("backlight brightness=%d\n", brightness);

	return aw9364_backlight_set(bl, brightness);
}

static int aw9364_backlight_get_brightness(struct backlight_device *bl)
{
	struct aw9364_backlight_data *data = bl_get_data(bl);
	return data->current_brightness;
}

static struct backlight_ops aw9364_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status	= aw9364_backlight_update_status,
	.get_brightness	= aw9364_backlight_get_brightness,
};
#ifdef CONFIG_HAS_EARLYSUSPEND
static void aw9364_bl_work(struct work_struct *work)
{
	//struct aw9364_backlight_data *aw9364_data = container_of(work, struct aw9364_backlight_data,
						   //work.work);
	backlight_update_status(g_aw9364_bl);
}

static void aw9364_bl_suspend(struct early_suspend *h)
{
	struct aw9364_backlight_data *aw9364_data;
	aw9364_data = container_of(h, struct aw9364_backlight_data, early_suspend);
	aw9364_data->suspend_flag = 1;

	schedule_delayed_work(&aw9364_data->work, msecs_to_jiffies(100));		
}


static void aw9364_bl_resume(struct early_suspend *h)
{
	struct aw9364_backlight_data *aw9364_data;
	aw9364_data = container_of(h, struct aw9364_backlight_data, early_suspend);
	aw9364_data->suspend_flag = 0;

	schedule_delayed_work(&aw9364_data->work, msecs_to_jiffies(100));
	
}

#endif


int rk29_backlight_ctrl(int open)
{
	if(open)
		g_aw9364_data->suspend_flag = 0;
	else
		g_aw9364_data->suspend_flag = 1;

	backlight_update_status(g_aw9364_bl);
	return 0;
}

static int aw9364_backlight_probe(struct platform_device *pdev)
{
	struct aw9364_backlight_data *data;
	struct aw9364_platform_data *pdata = pdev->dev.platform_data;
	struct backlight_device *bl;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->current_brightness = 0;
	data->pin_en = pdata->pin_en;


	bl = backlight_device_register("wm831x", &pdev->dev, data,
				       &aw9364_backlight_ops, NULL);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		kfree(data);
		return PTR_ERR(bl);
	}

	bl->props.brightness = BL_INIT_VALUE;
	bl->props.max_brightness= BL_SET;

	if(data && data->pin_en)
	gpio_request(data->pin_en, NULL);
	else
	return -1;

	spin_lock_init(&data->bl_lock);	

	platform_set_drvdata(pdev, bl);

#ifdef CONFIG_HAS_EARLYSUSPEND	
	data->early_suspend.level = ~0x0;
	data->early_suspend.suspend = aw9364_bl_suspend;
	data->early_suspend.resume = aw9364_bl_resume;
	register_early_suspend(&data->early_suspend);
	INIT_DELAYED_WORK(&data->work, aw9364_bl_work);
	g_aw9364_bl = bl;
	g_aw9364_data = data;
#endif

	gpio_direction_output(data->pin_en, GPIO_LOW);
	mdelay(3);

	backlight_update_status(bl);
	schedule_delayed_work(&data->work, msecs_to_jiffies(100));

	printk("%s\n",__FUNCTION__);

	return 0;
}

static int aw9364_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct aw9364_backlight_data *data = bl_get_data(bl);

	backlight_device_unregister(bl);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif 
	kfree(data);
	return 0;
}

static void aw9364_backlight_shutdown(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct aw9364_backlight_data *data = bl_get_data(bl);
	
	printk("%s\n",__FUNCTION__);
	data->shutdown_flag = 1;
	aw9364_backlight_update_status(bl);
	return;
}

static struct platform_driver aw9364_backlight_driver = {
	.driver		= {
		.name	= "aw9364_backlight",
		.owner	= THIS_MODULE,
	},
	.probe		= aw9364_backlight_probe,
	.remove		= aw9364_backlight_remove,
	.shutdown	= aw9364_backlight_shutdown,
};

static int __init aw9364_backlight_init(void)
{
	return platform_driver_register(&aw9364_backlight_driver);
}
module_init(aw9364_backlight_init);

static void __exit aw9364_backlight_exit(void)
{
	platform_driver_unregister(&aw9364_backlight_driver);
}
module_exit(aw9364_backlight_exit);

MODULE_DESCRIPTION("Backlight Driver for AW9364");
MODULE_AUTHOR("luo wei <lw@rock-chips.com");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:aw9364-backlight");

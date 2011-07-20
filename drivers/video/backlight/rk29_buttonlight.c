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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>

#include <linux/earlysuspend.h>
#include <asm/io.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>


/*
 * Debug
 */
#if 0 
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

static int rk29_button_light_value = 0;
struct backlight_device * rk29_button_light_device;

static s32 rk29_set_button_light(struct backlight_device *bl)
{
    struct rk29_button_light_info *button_light_info = bl->dev.parent->platform_data;

    DBG(">>>>>>> rk29_set_button_light\n");
    if(bl->props.brightness)
        {
    	gpio_set_value(button_light_info->led_on_pin, button_light_info->led_on_level);
    	rk29_button_light_value = 255;
        }
    else
        {
        gpio_set_value(button_light_info->led_on_pin, button_light_info->led_on_level?0:1);
    	rk29_button_light_value = 0;
        }
    return 0;
}

static s32 rk29_get_button_light(struct backlight_device *bl)
{
    DBG(">>>>>>> rk29_get_button_light\n");
    return rk29_button_light_value;
}

static struct backlight_ops rk29_button_light_ops = {
	.update_status = rk29_set_button_light,
	.get_brightness = rk29_get_button_light,
};

static int rk29_button_light_probe(struct platform_device *pdev)
{		 
    struct rk29_button_light_info *button_light_info = pdev->dev.platform_data;
    
	rk29_button_light_device = backlight_device_register("rk28_button_light", &pdev->dev, NULL, &rk29_button_light_ops);
	if (!rk29_button_light_device) {
        DBG("rk29_button_light_probe error\n"); 
		return -ENODEV;		
	}
   
	rk29_button_light_device->props.power = FB_BLANK_UNBLANK;
	rk29_button_light_device->props.fb_blank = FB_BLANK_UNBLANK;
	rk29_button_light_device->props.max_brightness = 255;
	rk29_button_light_device->props.brightness = 255;

    gpio_request(button_light_info->led_on_pin, NULL); 	
    gpio_direction_output(button_light_info->led_on_pin, button_light_info->led_on_level?0:1);
    
    if (button_light_info && button_light_info->io_init)
        button_light_info->io_init();
    
    return 0;
}

static int rk29_button_light_remove(struct platform_device *pdev)
{		
   
	if (rk29_button_light_device) {
		backlight_device_unregister(rk29_button_light_device);
        return 0;
    } else {
        DBG("rk29_button_light_remove error\n"); 
        return -ENODEV;      
    }
}

static struct platform_driver rk29_button_light_driver = {
	.probe	= rk29_button_light_probe,
	.remove = rk29_button_light_remove,
	.driver	= {
		.name	= "rk29_button_light",
		.owner	= THIS_MODULE,
	},
};


static int __init rk29_button_light_init(void)
{
	platform_driver_register(&rk29_button_light_driver);
	return 0;
}

static int __init rk29_button_light_exit(void)
{
	platform_driver_unregister(&rk29_button_light_driver);
	return 0;
}

late_initcall(rk29_button_light_init);
module_exit(rk29_button_light_exit);


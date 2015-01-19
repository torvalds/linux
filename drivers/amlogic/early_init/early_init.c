/*
 * early_init device driver.
 *
 *
 * Copyright (c) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
  * the Free Software Foundation; version 2 of the License.
  *
  */

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <mach/am_regs.h>
#include <plat/io.h>


#include <linux/of.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of_gpio.h>



static int early_init_dt_probe(struct platform_device *pdev)
{
	const char *str;
	int ret;
	int pin;
	printk("-------%s:%d----------\n",__func__,__LINE__);
	
	/**     *  Meson6 socket board ONLY     *  Do *NOT* merge for other BSP     */    
	ret = of_property_read_string(pdev->dev.of_node,"gpio-1",&str);
	if(ret){
		printk("---%s----can not get gpio-1\n",__func__);
		return -1;
	}	
	pin = amlogic_gpio_name_map_num(str);
	ret = amlogic_gpio_request(pin,"early_init");
	if(ret){
		printk("---%s----can not request pin %d\n",__func__,pin);
		return -1;
	}	
	ret = amlogic_gpio_direction_output(pin,0,"early_init");
	if(ret){
		printk("---%s----can not set output pin %d\n",__func__,pin);
		amlogic_gpio_free(pin,"early_init");
		return -1;
	}

	ret = of_property_read_string(pdev->dev.of_node,"gpio-2",&str);
	if(ret){
		printk("---%s----can not get gpio-1\n",__func__);
		return -1;
	}	
	pin = amlogic_gpio_name_map_num(str);
	ret = amlogic_gpio_request(pin,"early_init");
	if(ret){
		printk("---%s----can not request pin %d\n",__func__,pin);
		return -1;
	}	
	ret = amlogic_gpio_direction_output(pin,1,"early_init");
	if(ret){
		printk("---%s----can not set output pin %d\n",__func__,pin);
		amlogic_gpio_free(pin,"early_init");
		return -1;
	}
	
	 return 0;
}

static int early_init_dt_remove(struct platform_device *pdev)
{
	printk("-------%s:%d----------\n",__func__,__LINE__);
	
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id amlogic_early_init_dt_match[]={
	{	.compatible = "amlogic,early_init",
	},
	{},
};
#else
#define amlogic_early_init_dt_match NULL
#endif

static struct platform_driver early_init_dt_driver = {
	.probe = early_init_dt_probe,
	.remove = early_init_dt_remove,
	.driver = {
		.name = "early_init",
		.of_match_table = amlogic_early_init_dt_match,
	.owner = THIS_MODULE,
	},
};

static int __init early_init_dt_init(void)
{
	int ret = -1;
	ret = platform_driver_register(&early_init_dt_driver);	
	if (ret != 0) {
		printk("failed to register early_init driver, error %d\n", ret);
		return -ENODEV;
	}
	printk("-------%s:%d----------\n",__func__,__LINE__);

	return ret;
}

static void __exit early_init_dt_exit(void)
{
	platform_driver_unregister(&early_init_dt_driver);
}

arch_initcall(early_init_dt_init);
module_exit(early_init_dt_exit);

MODULE_DESCRIPTION("AMLOGIC early_init dt driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("yun.cai <yun.cai@amlogic.com>");

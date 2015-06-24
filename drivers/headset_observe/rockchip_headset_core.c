/* 
 * Copyright (C) 2014 Rockchip Corporation.
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
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include "rk_headset.h"
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/iio/consumer.h>

/* Debug */
#if 0
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif

struct rk_headset_pdata *pdata_info;

static int rockchip_headset_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct rk_headset_pdata *pdata;
	int ret;
	enum of_gpio_flags flags;

	pdata = kzalloc(sizeof(struct rk_headset_pdata), GFP_KERNEL);
	if (pdata == NULL) {
		printk("%s failed to allocate driver data\n",__FUNCTION__);
		return -ENOMEM;
	}
	memset(pdata,0,sizeof(struct rk_headset_pdata));
	pdata_info = pdata;

	//headset
	ret = of_get_named_gpio_flags(node, "headset_gpio", 0, &flags);
	if (ret < 0) {
		printk("%s() Can not read property headset_gpio\n", __FUNCTION__);
		goto err;
	} else {
		pdata->headset_gpio = ret;
		ret = devm_gpio_request(&pdev->dev, pdata->headset_gpio, "headset_gpio");
		if(ret < 0){
			printk("%s() devm_gpio_request headset_gpio request ERROR\n", __FUNCTION__);
			goto err;
		}

		ret = gpio_direction_input(pdata->headset_gpio); 
		if(ret < 0){
			printk("%s() gpio_direction_input headset_gpio set ERROR\n", __FUNCTION__);
			goto err;
		}

		pdata->headset_insert_type = (flags & OF_GPIO_ACTIVE_LOW) ? HEADSET_IN_LOW : HEADSET_IN_HIGH;
	}

	//hook
	ret = of_get_named_gpio_flags(node, "hook_gpio", 0, &pdata->hook_gpio);
	if (ret < 0) {
		DBG("%s() Can not read property hook_gpio\n", __FUNCTION__);
		pdata->hook_gpio = 0;
		//adc mode
		pdata->chan = iio_channel_get(&pdev->dev, NULL);
   		if (IS_ERR(pdata->chan))
 	       	{
			pdata->chan = NULL;
			printk("%s() have not set adc chan\n", __FUNCTION__);
		}
	} else {
		ret = of_property_read_u32(node, "hook_down_type", &pdata->hook_down_type);
		if (ret < 0) {
			DBG("%s() have not set hook_down_type,set >hook< insert type low level default\n", __FUNCTION__);
			pdata->hook_down_type = 0;
		}
		ret = devm_gpio_request(&pdev->dev, pdata->hook_gpio, "hook_gpio");
		if(ret < 0){
			printk("%s() devm_gpio_request hook_gpio request ERROR\n", __FUNCTION__);
			goto err;
		}
		ret = gpio_direction_input(pdata->hook_gpio); 
		if(ret < 0){
			printk("%s() gpio_direction_input hook_gpio set ERROR\n", __FUNCTION__);
			goto err;
		}
	}

	#ifdef CONFIG_MODEM_MIC_SWITCH
	//mic
	ret = of_get_named_gpio_flags(node, "mic_switch_gpio", 0, &flags);
	if (ret < 0) {
		DBG("%s() Can not read property mic_switch_gpio\n", __FUNCTION__);
	} else {
		pdata->headset_gpio = ret;
		ret = of_property_read_u32(node, "hp_mic_io_value", &pdata->hp_mic_io_value);
		if (ret < 0) {
			DBG("%s() have not set hp_mic_io_value ,so default set pull down low level\n", __FUNCTION__);
			pdata->hp_mic_io_value = 0;
		}
		ret = of_property_read_u32(node, "main_mic_io_value", &pdata->main_mic_io_value);
		if (ret < 0) {
			DBG("%s() have not set main_mic_io_value ,so default set pull down low level\n", __FUNCTION__);
			pdata->main_mic_io_value = 1;
		}
	}
	#endif

	ret = of_property_read_u32(node, "rockchip,headset_wakeup", &pdata->headset_wakeup);
	if (ret < 0)
		pdata->headset_wakeup = 1;

	if(pdata->chan != NULL)
	{//hook adc mode
		printk("%s() headset have hook adc mode\n",__FUNCTION__);
		ret = rk_headset_adc_probe(pdev,pdata);
		if(ret < 0)
		{
			goto err;
		}	

	}
	else
	{//hook interrupt mode and not hook
		printk("%s() headset have %s mode\n",__FUNCTION__,pdata->hook_gpio?"interrupt hook":"no hook");
		ret = rk_headset_probe(pdev,pdata);
		if(ret < 0)
		{
			goto err;
		}
	}

	return 0;
err:
	kfree(pdata);
	return ret;
}

static int rockchip_headset_remove(struct platform_device *pdev)
{
	if(pdata_info)
		kfree(pdata_info);
	return 0;
}

static int rockchip_headset_suspend(struct platform_device *pdev, pm_message_t state)
{
	if(pdata_info->chan != 0)
	{
		return rk_headset_adc_suspend(pdev,state);
	}
	return 0;
}

static int rockchip_headset_resume(struct platform_device *pdev)
{
	if(pdata_info->chan != 0)
	{
		return rk_headset_adc_resume(pdev);
	}	
	return 0;
}

static const struct of_device_id rockchip_headset_of_match[] = {
        { .compatible = "rockchip_headset", },
        {},
};
MODULE_DEVICE_TABLE(of, rockchip_headset_of_match);

static struct platform_driver rockchip_headset_driver = {
	.probe	= rockchip_headset_probe,
	.remove = rockchip_headset_remove,
	.resume = 	rockchip_headset_resume,	
	.suspend = 	rockchip_headset_suspend,	
	.driver	= {
		.name	= "rockchip_headset",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rockchip_headset_of_match),		
	},
};

static int __init rockchip_headset_init(void)
{
	platform_driver_register(&rockchip_headset_driver);
	return 0;
}

static void __exit rockchip_headset_exit(void)
{
	platform_driver_unregister(&rockchip_headset_driver);
}
late_initcall(rockchip_headset_init);
MODULE_DESCRIPTION("Rockchip Headset Core Driver");
MODULE_LICENSE("GPL");

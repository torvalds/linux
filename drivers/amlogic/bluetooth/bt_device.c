/*
 *
 * arch/arm/mach-meson/bcm-bt.c
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Platform machine definition.
 */
 
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/rfkill.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <mach/am_regs.h>
#include <plat/bt_device.h>
#ifdef CONFIG_AM_WIFI_SD_MMC
#include <linux/amlogic/wifi_dt.h>
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend bt_early_suspend;
#endif

#define BT_RFKILL "bt_rfkill"

struct bt_dev_runtime_data {
    struct rfkill *bt_rfk;
    struct bt_dev_data *pdata;
};

static void bt_device_init(struct bt_dev_data *pdata)
{
    if(pdata->gpio_reset > 0 ) {
        amlogic_gpio_request(pdata->gpio_reset, BT_RFKILL);
    }    
        
    if(pdata->gpio_en > 0 ) {
        amlogic_gpio_request(pdata->gpio_en, BT_RFKILL);
    }
        
    if(pdata->gpio_wake > 0 ) {
        amlogic_gpio_request(pdata->gpio_wake, BT_RFKILL);
        amlogic_gpio_direction_output(pdata->gpio_wake, 1, BT_RFKILL);
    }

}

static void bt_device_deinit(struct bt_dev_data *pdata)
{
      if(pdata->gpio_reset > 0 )
        amlogic_gpio_free(pdata->gpio_reset, BT_RFKILL);
        
    if(pdata->gpio_en > 0 )
        amlogic_gpio_free(pdata->gpio_en, BT_RFKILL);
        
    if(pdata->gpio_wake > 0 ) 
        amlogic_gpio_free(pdata->gpio_wake, BT_RFKILL);     
}

static void bt_device_on(struct bt_dev_data *pdata)
{	
	if(pdata->gpio_reset > 0 )
	    amlogic_gpio_direction_output(pdata->gpio_reset, 0, BT_RFKILL);
	if(pdata->gpio_en > 0 )
	    amlogic_gpio_direction_output(pdata->gpio_en, 0, BT_RFKILL);	
	msleep(20);	
	if(pdata->gpio_reset > 0 )
	    amlogic_gpio_direction_output(pdata->gpio_reset, 1, BT_RFKILL);
	if(pdata->gpio_en > 0 )
	    amlogic_gpio_direction_output(pdata->gpio_en, 1, BT_RFKILL);	
	msleep(20);
}

static void bt_device_off(struct bt_dev_data *pdata)
{
    if(pdata->gpio_reset > 0 )
	    amlogic_gpio_direction_output(pdata->gpio_reset, 0, BT_RFKILL);
	if(pdata->gpio_en > 0 )
	    amlogic_gpio_direction_output(pdata->gpio_en, 0, BT_RFKILL);
	msleep(20);	
}

static int bt_set_block(void *data, bool blocked)
{
    struct bt_dev_data *pdata = data;
    pr_info("BT_RADIO going: %s\n", blocked ? "off" : "on");

	if (!blocked) {
		pr_info("BCM_BT: going ON\n");
		bt_device_on(pdata);
	} else {
		pr_info("BCM_BT: going OFF\n");
        bt_device_off(pdata);
	}
    return 0;
}

static const struct rfkill_ops bt_rfkill_ops = {
	.set_block = bt_set_block,
};
#ifdef CONFIG_HAS_EARLYSUSPEND 
static void bt_earlysuspend(struct early_suspend *h)                                               
{

    return;
}

static void bt_lateresume(struct early_suspend *h)
{

    return;
}
#endif

static int bt_suspend(struct platform_device *pdev, pm_message_t state)                                               
{

    return 0;
}

static int bt_resume(struct platform_device *pdev)
{

    return 0;
}

static int bt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rfkill *bt_rfk;
	struct bt_dev_data *pdata = NULL;
	struct bt_dev_runtime_data *prdata;

#ifdef CONFIG_OF
	//plat = aml_get_driver_data(pdev);
	if (pdev->dev.of_node) {
	    const char *str;
	    
	    printk(KERN_DEBUG "enter bt_probe of_node\n");
	    pdata = kzalloc(sizeof(struct bt_dev_data), GFP_KERNEL);
		ret = of_property_read_string(pdev->dev.of_node,"gpio_reset",&str);
		if(ret){
			printk(KERN_WARNING "not get gpio_reset\n");
			pdata->gpio_reset = 0;
		} else {
		    pdata->gpio_reset = amlogic_gpio_name_map_num(str);
		}
		
        ret = of_property_read_string(pdev->dev.of_node,"gpio_en",&str);
		if(ret){
			printk(KERN_WARNING "not get gpio_en\n");
			pdata->gpio_en = 0;
		} else {
		    pdata->gpio_en = amlogic_gpio_name_map_num(str);
		}
		
		ret = of_property_read_string(pdev->dev.of_node,"gpio_wake",&str);
		if(ret){
			printk(KERN_WARNING "not get gpio_wake\n");
			pdata->gpio_wake = 0;
		} else {
		    pdata->gpio_wake = amlogic_gpio_name_map_num(str);
		}
	}
#else
    pdata = (struct bt_dev_data *)(pdev->dev.platform_data);
#endif
    
    bt_device_init(pdata);
    /* default to bluetooth off */
    //rfkill_switch_all(RFKILL_TYPE_BLUETOOTH, 1);
    bt_device_off(pdata);
    
	bt_rfk = rfkill_alloc("bt-dev", &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			&bt_rfkill_ops, pdata);
						   
	if (!bt_rfk) {
        printk("rfk alloc fail\n");
		ret = -ENOMEM;
		goto err_rfk_alloc;
	}
	/* if not set false, the bt_set_block will call when rfkill class resume */
    rfkill_init_sw_state(bt_rfk, false);      //we want to reset bt when system resume
	ret = rfkill_register(bt_rfk);
	if (ret){
        printk(KERN_ERR "rfkill_register fail\n");
		goto err_rfkill;
    }
    prdata = kmalloc(sizeof(struct bt_dev_runtime_data), GFP_KERNEL);
    if (!prdata) {
        printk(KERN_ERR  "bt_dev_runtime_data alloc fail\n");
		goto err_rfkill;
    }
#ifdef CONFIG_AM_WIFI_SD_MMC
    //setup 32k clock
    wifi_request_32k_clk(1, BT_RFKILL);
    msleep(100);
#endif
    
    prdata->bt_rfk = bt_rfk;
    prdata->pdata = pdata;
	platform_set_drvdata(pdev, prdata);
#ifdef CONFIG_HAS_EARLYSUSPEND                                                                                        
        bt_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
        bt_early_suspend.suspend = bt_earlysuspend;
        bt_early_suspend.resume = bt_lateresume;
        bt_early_suspend.param = pdev;
        register_early_suspend(&bt_early_suspend);
#endif

	return 0;	
	
err_rfkill:
	rfkill_destroy(bt_rfk);
err_rfk_alloc:
    bt_device_deinit(pdata);
	return ret;
	
}

static int bt_remove(struct platform_device *pdev)
{
    struct bt_dev_runtime_data *prdata = platform_get_drvdata(pdev);
	struct rfkill *rfk = NULL;
	struct bt_dev_data *pdata = NULL;

	platform_set_drvdata(pdev, NULL);
#ifdef CONFIG_AM_WIFI_SD_MMC
    wifi_request_32k_clk(0, BT_RFKILL);
#endif
	if(prdata) {
	    rfk = prdata->bt_rfk;
	    pdata = prdata->pdata;
	}

	if(pdata) {
	    bt_device_deinit(pdata);
	    kfree(pdata);
	}

	if (rfk) {
		rfkill_unregister(rfk);
		rfkill_destroy(rfk);
	}
	rfk = NULL;

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id bt_dev_dt_match[]={
	{	.compatible = "amlogic,bt-dev",
	},
	{},
};
#else
#define bt_dev_dt_match NULL
#endif

static struct platform_driver bt_driver = {
	.driver		= {
		.name	= "bt-dev",
		.of_match_table = bt_dev_dt_match,
	},
	.probe		= bt_probe,
	.remove		= bt_remove,
	.suspend    = bt_suspend,
    .resume     = bt_resume,
};

static int __init bt_init(void)
{
    printk("amlogic rfkill init\n");

	return platform_driver_register(&bt_driver);
}
static void __exit bt_exit(void)
{
	platform_driver_unregister(&bt_driver);
}

module_init(bt_init);
module_exit(bt_exit);
MODULE_DESCRIPTION("bt rfkill");
MODULE_AUTHOR("");
MODULE_LICENSE("GPL");

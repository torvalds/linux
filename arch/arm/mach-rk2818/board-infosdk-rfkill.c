/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * Author: roger_chen <cz@rock-chips.com>
 *
 * This program is the bluetooth device bcm4329's driver,
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/rfkill.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/slab.h>
//#include <asm/gpio.h>
//#include <asm/arch/gpio.h>
//#include <asm/arch/iomux.h>
//#include <asm/arch/gpio.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <mach/spi_fpga.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>

#if 0
#define DBG(x...)   printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#ifdef CONFIG_MACH_RK2818INFO_IT50
#define INFO_BT_GPIO_POWER_N   TCA6424_P01
#define INFO_BT_GPIO_RESET_N   TCA6424_P14
#else
#define INFO_BT_GPIO_POWER_N   TCA6424_P25
#define INFO_BT_GPIO_RESET_N   TCA6424_P22
#endif
static struct rfkill *bt_rfk;
static const char bt_name[] = "bcm4329";
extern int info_bt_power_state;
extern int info_wifi_power_state;
  
static int bcm4329_set_block(void *data, bool blocked)
{
    	DBG("%s---blocked :%d\n", __FUNCTION__, blocked);

    	if (false == blocked) {          
		gpio_set_value(INFO_BT_GPIO_POWER_N, GPIO_HIGH);  /* bt power on */
		gpio_set_value(INFO_BT_GPIO_RESET_N, GPIO_HIGH);  /* bt reset deactive*/
		mdelay(20);
        	pr_info("bt turn on power\n");
    	}
    	else {
		if (!info_wifi_power_state) {
			gpio_set_value(INFO_BT_GPIO_POWER_N, GPIO_LOW);  /* bt power off */
        		mdelay(20);	
        		pr_info("bt shut off power\n");
		}else {
			pr_info("bt shouldn't shut off power, wifi is using it!\n");
		}

		gpio_set_value(INFO_BT_GPIO_RESET_N, GPIO_LOW);  /* bt reset active*/
		mdelay(20);
    	}

	info_bt_power_state = !blocked;
    	return 0;
}


static const struct rfkill_ops bcm4329_rfk_ops = {
	.set_block = bcm4329_set_block,
};

static int __init bcm4329_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	
	/* default to bluetooth off */
//	rfkill_switch_all(RFKILL_TYPE_BLUETOOTH, true);
    
    	bt_rfk = rfkill_alloc(bt_name, 
                    NULL, 
                    RFKILL_TYPE_BLUETOOTH, 
                    &bcm4329_rfk_ops, 
                    NULL);

	if (!bt_rfk)
	{
		printk("fail to rfkill_allocate************\n");
		return -ENOMEM;
	}

	rc = rfkill_register(bt_rfk);
	if (rc)
		rfkill_destroy(bt_rfk);

    	printk("rc=0x%x\n", rc);
    
	return rc;
}


static int __devexit bcm4329_rfkill_remove(struct platform_device *pdev)
{
	if (bt_rfk)
		rfkill_unregister(bt_rfk);
	bt_rfk = NULL;

	platform_set_drvdata(pdev, NULL);
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static struct platform_driver bcm4329_rfkill_driver = {
	.probe = bcm4329_rfkill_probe,
	.remove = __devexit_p(bcm4329_rfkill_remove),
	.driver = {
		.name = "info_rfkill", 
		.owner = THIS_MODULE,
	},
};

/*
 * Module initialization
 */
static int __init bcm4329_mod_init(void)
{
	int ret;
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	ret = platform_driver_register(&bcm4329_rfkill_driver);
    	printk("ret=0x%x\n", ret);
	return ret;
}

static void __exit bcm4329_mod_exit(void)
{
	platform_driver_unregister(&bcm4329_rfkill_driver);
}

module_init(bcm4329_mod_init);
module_exit(bcm4329_mod_exit);
MODULE_DESCRIPTION("bcm4329 Bluetooth driver");
MODULE_AUTHOR("roger_chen cz@rock-chips.com");
MODULE_LICENSE("GPL");


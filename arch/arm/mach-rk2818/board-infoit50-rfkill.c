/* linux/arch/arm/mach-rk2818/board-infoit50-rfkill.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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
#include <linux/rfkill.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>

#define INFOIT50_BT_GPIO_POWER_N   TCA6424_P01
#define INFOIT50_BT_GPIO_RESET_N   TCA6424_P14

static struct rfkill *bt_rfk;
static const char bt_name[] = "bcm4329";

static int bcm4329_set_block(void *data, bool blocked)
{
    	pr_info("%s---blocked :%d\n", __func__, blocked);

	if (false == blocked) {
		gpio_direction_output(INFOIT50_BT_GPIO_POWER_N, GPIO_HIGH);  /* bt power on */
		gpio_direction_output(INFOIT50_BT_GPIO_RESET_N, GPIO_HIGH);  /* bt reset deactive */
		mdelay(20);
		pr_info("bt turn on power\n");
	}else{   
		gpio_direction_output(INFOIT50_BT_GPIO_POWER_N, GPIO_LOW);  /* bt power off */
		gpio_direction_output(INFOIT50_BT_GPIO_RESET_N, GPIO_LOW); /* bt reset active */
        	mdelay(20);	
		pr_info("bt shut off power\n");
	}

	return 0;
}


static const struct rfkill_ops bcm4329_rfk_ops = {
	.set_block = bcm4329_set_block,
};

static int bcm4329_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	bool default_state = true;
	
	pr_info("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	
	rc = gpio_request(INFOIT50_BT_GPIO_POWER_N, "bt_shutdown");
	if (rc)
		return rc;	
	rc = gpio_request(INFOIT50_BT_GPIO_RESET_N, "bt_reset");
 	if (rc){
	 	gpio_free(INFOIT50_BT_GPIO_POWER_N);	
		return rc;
	}
	
	/* default to bluetooth off */
 	bcm4329_set_block(NULL, default_state); /* blocked -> bt off */
	 
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
	
	rfkill_set_states(bt_rfk, default_state, false);

	rc = rfkill_register(bt_rfk);
	if (rc)
		rfkill_destroy(bt_rfk);

    	printk("rc=0x%x\n", rc);
    
	return rc;
}


static int bcm4329_rfkill_remove(struct platform_device *pdev)
{
	if (bt_rfk)
		rfkill_unregister(bt_rfk);
	rfkill_destroy(bt_rfk);
	bt_rfk = NULL;
	gpio_free(INFOIT50_BT_GPIO_POWER_N);
 	gpio_free(INFOIT50_BT_GPIO_RESET_N);

	platform_set_drvdata(pdev, NULL);
	pr_info("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static struct platform_driver bcm4329_rfkill_driver = {
	.probe = bcm4329_rfkill_probe,
	.remove = bcm4329_rfkill_remove,
	.driver = {
		.name = "infoit50_rfkill", 
		.owner = THIS_MODULE,
	},
};

/*
 * Module initialization
 */
static int __init bcm4329_mod_init(void)
{
	int ret;
	pr_info("Enter::%s,line=%d\n",__func__,__LINE__);
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


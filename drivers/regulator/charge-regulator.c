/* drivers/regulator/charge-regulator.c
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
/*******************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.			  */
/*******************************************************************
FILE		:	    	charge-regulator.c
DESC		:	charge current change driver
AUTHOR		:	hxy
DATE		:	2010-09-02
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
********************************************************************/


#include <linux/bug.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/charge-regulator.h>
#include <linux/gpio.h>


#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif


const static int charge_current_map[] = {
	475, 1200,
};

static int charge_current_is_enabled(struct regulator_dev *dev)
{
	return 0;
}

static int charge_current_enable(struct regulator_dev *dev)
{
	return 0;
}

static int charge_current_disable(struct regulator_dev *dev)
{
	return 0;
}

static int charge_get_current(struct regulator_dev *dev)
{
	const int *current_map = charge_current_map;

       struct charge_platform_data *pdata = rdev_get_drvdata(dev);

       gpio_direction_input(pdata->gpio_charge);
	   
	return gpio_get_value(pdata->gpio_charge) ? current_map[0] *1000:current_map[1]*1000;
}

static int charge_set_current(struct regulator_dev *dev,
				  int min_uA, int max_uA)
{
       DBG("enter charge_set_current , max_uA = %d\n",max_uA);
	struct charge_platform_data *pdata = rdev_get_drvdata(dev);
       const int *current_map = charge_current_map;
       int max_mA = max_uA / 1000;
	if ( max_mA == current_map[0] )
	     gpio_direction_output(pdata->gpio_charge, GPIO_HIGH);
	else 
	     gpio_direction_output(pdata->gpio_charge, GPIO_LOW);

       return 0;

}

static struct regulator_ops charge_current_ops = {
	.is_enabled = charge_current_is_enabled,
	.enable = charge_current_enable,
	.disable = charge_current_disable,
	.get_current_limit = charge_get_current,
	.set_current_limit = charge_set_current,
};

static struct regulator_desc chargeregulator= {
		.name = "charge-regulator",
		.ops = &charge_current_ops,
		.type = REGULATOR_CURRENT,
};



static int __devinit charge_regulator_probe(struct platform_device *pdev)
{

	struct charge_platform_data *pdata = pdev->dev.platform_data;
	struct regulator_dev *rdev;
	int ret ;

	rdev = regulator_register(&chargeregulator, &pdev->dev,
				pdata->init_data, pdata);
	if (IS_ERR(rdev)) {
		dev_dbg(&pdev->dev, "couldn't register regulator\n");
		return PTR_ERR(rdev);
	}
	
	ret = gpio_request(pdata->gpio_charge, "charge_current");

	if (ret) {
			dev_err(&pdev->dev,"failed to request charge gpio\n");
			goto err_gpio;
		}

	platform_set_drvdata(pdev, rdev);
printk(KERN_INFO "charge_regulator: driver initialized\n");
	return 0;
	
err_gpio:
	gpio_free(pdata->gpio_charge);

	return ret;

}
static int __devexit charge_regulator_remove(struct platform_device *pdev)
{
	struct charge_platform_data *pdata = pdev->dev.platform_data;
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	gpio_free(pdata->gpio_charge);

	return 0;
}

static struct platform_driver charge_regulator_driver = {
	.driver = {
		.name = "charge-regulator",
	},
	.remove = __devexit_p(charge_regulator_remove),
};


static int __init charge_regulator_module_init(void)
{
	return platform_driver_probe(&charge_regulator_driver, charge_regulator_probe);
}

static void __exit charge_regulator_module_exit(void)
{
	platform_driver_unregister(&charge_regulator_driver);
}


module_init(charge_regulator_module_init);

module_exit(charge_regulator_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hxy <hxy@rock-chips.com>");
MODULE_DESCRIPTION("charge current change driver");


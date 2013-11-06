/*
 *  drivers/mfd/rt5025-misc.c
 *  Driver foo Richtek RT5025 PMIC Misc Part
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/mfd/rt5025.h>
#include <linux/mfd/rt5025-misc.h>

struct rt5025_misc_info {
	struct i2c_client *i2c;
};

static struct i2c_client *g_shdn;
void rt5025_power_off(void)
{
	rt5025_reg_write(g_shdn, RT5025_CHENH_REG, 0x00);
	rt5025_reg_write(g_shdn, RT5025_CHENL_REG, 0x80);
	rt5025_set_bits(g_shdn, RT5025_SHDNCTRL_REG, RT5025_SHDNCTRL_MASK);
}
EXPORT_SYMBOL(rt5025_power_off);

int rt5025_cable_exist(void)
{
	int ret = 0;
	ret = rt5025_reg_read(g_shdn, 0x01);
	if (ret < 0)
		return 0;
	else
	{
		if (ret&0x3)
			return 1;
		return 0;
	}
}
EXPORT_SYMBOL(rt5025_cable_exist);

static int __devinit rt5025_misc_reg_init(struct i2c_client *client, struct rt5025_misc_data *md)
{
	int ret = 0;
	
	rt5025_reg_write(client, RT5025_RESETCTRL_REG, md->RSTCtrl.val);
	rt5025_assign_bits(client, RT5025_VSYSULVO_REG, RT5025_VSYSOFF_MASK, md->VSYSCtrl.val);
	rt5025_reg_write(client, RT5025_PWRONCTRL_REG, md->PwrOnCfg.val);
	rt5025_reg_write(client, RT5025_SHDNCTRL_REG, md->SHDNCtrl.val);
	rt5025_reg_write(client, RT5025_PWROFFEN_REG, md->PwrOffCond.val);

	return ret;
}

static int __devinit rt5025_misc_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = chip->dev->platform_data;
	struct rt5025_misc_info *mi;

	mi = kzalloc(sizeof(*mi), GFP_KERNEL);
	if (!mi)
		return -ENOMEM;

	mi->i2c = chip->i2c;
	rt5025_misc_reg_init(mi->i2c, pdata->misc_data);

	//for shutdown control
	g_shdn = chip->i2c;

	platform_set_drvdata(pdev, mi);
	return 0;
}

static int __devexit rt5025_misc_remove(struct platform_device *pdev)
{
	struct rt5025_misc_info *mi = platform_get_drvdata(pdev);

	kfree(mi);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver rt5025_misc_driver = 
{
	.driver = {
		.name = RT5025_DEVICE_NAME "-misc",
		.owner = THIS_MODULE,
	},
	.probe = rt5025_misc_probe,
	.remove = __devexit_p(rt5025_misc_remove),
};

static int __init rt5025_misc_init(void)
{
	return platform_driver_register(&rt5025_misc_driver);
}
module_init(rt5025_misc_init);

static void __exit rt5025_misc_exit(void)
{
	platform_driver_unregister(&rt5025_misc_driver);
}
module_exit(rt5025_misc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Misc driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-misc");
MODULE_VERSION(RT5025_DRV_VER);

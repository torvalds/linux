/*
 *  drivers/mfd/rt5025-misc.c
 *  Driver foo Richtek RT5025 PMIC Misc Part
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/power_supply.h>

#include <linux/mfd/rt5025.h>
#include <linux/mfd/rt5025-misc.h>

static unsigned char misc_init_regval[] = {
	0x2A,   /* reg 0x15 */
	0x00,	/* reg 0x16 */
	0x60,   /* reg 0x17 */
	0x16,   /* reg 0x19 */
	0x60,   /* reg 0x1A */
	0x0C,   /* reg 0x1B */
	0xF3,	/* reg 0x36 */
	0x90,	/* reg 0x38 */
};

static struct i2c_client *g_shdn;
static bool rt_pm_off;
static void rt5025_power_off(void)
{
	rt5025_reg_write(g_shdn, RT5025_REG_CHANNELH, 0x00);
	rt5025_reg_write(g_shdn, RT5025_REG_CHANNELL, 0x80);
	rt5025_set_bits(g_shdn, RT5025_REG_MISC3, RT5025_SHDNCTRL_MASK);
}
EXPORT_SYMBOL(rt5025_power_off);

int rt5025_cable_exist(void)
{
	int ret = 0;
	#ifdef CONFIG_CHARGER_RT5025
	struct power_supply *psy = power_supply_get_by_name("rt-charger");
	union power_supply_propval pval;

	if (!psy) {
		pr_err(" couldn't get charger power supply\n");
	} else {
		ret = psy->get_property(psy,
			POWER_SUPPLY_PROP_CHARGE_NOW, &pval);
		if (ret < 0) {
			ret = 0;
		} else {
			if (pval.intval > POWER_SUPPLY_TYPE_BATTERY)
				ret = 1;
			else
				ret = 0;
		}
	}
	#else
	ret = rt5025_reg_read(g_shdn, RT5025_REG_CHGCTL1);
	if (ret < 0) {
		pr_err("couldn't get cable status\n");
		ret = 0;
	} else {
		if (ret & RT5025_CABLEIN_MASK)
			ret = 1;
		else
			ret = 0;
	}
	#endif /* #ifdef CONFIG_CHARGER_RT5025 */
	return ret;
}
EXPORT_SYMBOL(rt5025_cable_exist);

static void rt5025_general_irq_handler(void *info, int eventno)
{
	struct rt5025_misc_info *mi = info;

	RTINFO("eventno=%02d\n", eventno);

	switch (eventno) {
	case MISCEVENT_RESETB:
		dev_warn(mi->dev, "RESETB event trigger\n");
		break;
	case MISCEVENT_KPSHDN:
		dev_warn(mi->dev, "PwrKey force shdn\n");
		break;
	case MISCEVENT_SYSLV:
		dev_warn(mi->dev, "syslv event trigger\n");
		break;
	case MISCEVENT_DCDC4LVHV:
		dev_warn(mi->dev, "DCDC4LVHV event trigger\n");
		break;
	case MISCEVENT_DCDC3LV:
		dev_warn(mi->dev, "DCDC3LV event trigger\n");
		break;
	case MISCEVENT_DCDC2LV:
		dev_warn(mi->dev, "DCDC2LV event trigger\n");
		break;
	case MISCEVENT_DCDC1LV:
		dev_warn(mi->dev, "DCDC2LV event trigger\n");
		break;
	case MISCEVENT_OT:
		dev_warn(mi->dev, "Over temperature event trigger\n");
		break;
	default:
		break;
	}
}

static rt_irq_handler rt_miscirq_handler[MISCEVENT_MAX] = {
	[MISCEVENT_GPIO0_IE] = rt5025_general_irq_handler,
	[MISCEVENT_GPIO1_IE] = rt5025_general_irq_handler,
	[MISCEVENT_GPIO2_IE] = rt5025_general_irq_handler,
	[MISCEVENT_RESETB] = rt5025_general_irq_handler,
	[MISCEVENT_PWRONF] = rt5025_general_irq_handler,
	[MISCEVENT_PWRONR] = rt5025_general_irq_handler,
	[MISCEVENT_KPSHDN] = rt5025_general_irq_handler,
	[MISCEVENT_SYSLV] = rt5025_general_irq_handler,
	[MISCEVENT_DCDC4LVHV] = rt5025_general_irq_handler,
	[MISCEVENT_PWRONLP_IRQ] = rt5025_general_irq_handler,
	[MISCEVENT_PWRONSP_IRQ] = rt5025_general_irq_handler,
	[MISCEVENT_DCDC3LV] = rt5025_general_irq_handler,
	[MISCEVENT_DCDC2LV] = rt5025_general_irq_handler,
	[MISCEVENT_DCDC1LV] = rt5025_general_irq_handler,
	[MISCEVENT_OT] = rt5025_general_irq_handler,
};

void rt5025_misc_irq_handler(struct rt5025_misc_info *ci, unsigned int irqevent)
{
	int i;
	unsigned int enable_irq_event = (misc_init_regval[6] << 8) |
		misc_init_regval[7];
	unsigned int final_irq_event = irqevent&enable_irq_event;

	for (i = 0; i < MISCEVENT_MAX; i++) {
		if ((final_irq_event&(1 << i)) && rt_miscirq_handler[i])
			rt_miscirq_handler[i](ci, i);
	}
}
EXPORT_SYMBOL(rt5025_misc_irq_handler);

static int  rt5025_misc_reginit(struct i2c_client *client)
{
	rt5025_reg_write(client, RT5025_REG_MISC1, misc_init_regval[0]);
	rt5025_reg_write(client, RT5025_REG_ONEVENT, misc_init_regval[1]);
	rt5025_assign_bits(client, RT5025_REG_DCDCONOFF,
		RT5025_VSYSOFF_MASK, misc_init_regval[2]);
	rt5025_reg_write(client, RT5025_REG_MISC2, misc_init_regval[3]);
	rt5025_reg_write(client, RT5025_REG_MISC3, misc_init_regval[4]);
	rt5025_reg_write(client, RT5025_REG_MISC4, misc_init_regval[5]);
	/*set all to be masked*/
	rt5025_reg_write(client, RT5025_REG_IRQEN4, 0x00);
	rt5025_reg_write(client, RT5025_REG_IRQEN5, 0x00);
	/*clear the old irq status*/
	rt5025_reg_read(client, RT5025_REG_IRQSTAT4);
	rt5025_reg_read(client, RT5025_REG_IRQSTAT5);
	/*set enable irqs as we want*/
	rt5025_reg_write(client, RT5025_REG_IRQEN4, misc_init_regval[6]);
	rt5025_reg_write(client, RT5025_REG_IRQEN5, misc_init_regval[7]);
	return 0;
}

static int rt_parse_dt(struct rt5025_misc_info *mi, struct device *dev)
{
	int rc;
	#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	unsigned int val;

	rc = of_property_read_u32(np, "rt,vsyslv", &val);
	if (rc < 0) {
		dev_info(dev, "no system lv value, use default value\n");
	} else {
		if (val > RT5025_VOFF_MAX)
			val = RT5025_VOFF_MAX;
		misc_init_regval[2] &= ~RT5025_VSYSOFF_MASK;
		misc_init_regval[2] |= val << RT5025_VSYSOFF_SHFT;
	}

	rc = of_property_read_u32(np, "rt,shdnlpress_time", &val);
	if (rc < 0) {
		dev_info(dev, "no shdnlpress time, use default value\n");
	} else {
		if (val > RT5025_SHDNPRESS_MAX)
			val = RT5025_SHDNPRESS_MAX;
		misc_init_regval[3] &= ~RT5025_SHDNLPRESS_MASK;
		misc_init_regval[3] |= val << RT5025_SHDNLPRESS_SHFT;
	}

	rc = of_property_read_u32(np, "rt,startlpress_time", &val);
	if (rc < 0) {
		dev_err(dev, "no start_lpress, use default value\n");
	} else {
		if (val > RT5025_STARTIME_MAX)
			val = RT5025_STARTIME_MAX;
		misc_init_regval[3] &= ~RT5025_STARTLPRESS_MASK;
		misc_init_regval[3] |= val << RT5025_STARTLPRESS_SHFT;
	}

	if (of_property_read_bool(np, "rt,vsyslv_enshdn"))
		misc_init_regval[5] |= RT5025_VSYSLVSHDN_MASK;
	else
		misc_init_regval[5] &= ~RT5025_VSYSLVSHDN_MASK;
	#endif

	rt_pm_off = of_property_read_bool(np, "rt,system-power-controller");
	/* #ifdef CONFIG_OF */
	rc = rt5025_misc_reginit(mi->i2c);
	return rc;
}

static int rt_parse_pdata(struct rt5025_misc_info *mi, struct device *dev)
{
	struct rt5025_misc_data  *pdata = dev->platform_data;
	int rc = 0;

	/*system low voltage*/
	misc_init_regval[2] &= ~RT5025_VSYSOFF_MASK;
	misc_init_regval[2] |= (pdata->vsyslv << RT5025_VSYSOFF_SHFT);
	/*shutdown long press time*/
	misc_init_regval[3] &= ~RT5025_SHDNLPRESS_MASK;
	misc_init_regval[3] |= (pdata->shdnlpress_time <<
		RT5025_SHDNLPRESS_SHFT);
	/*start long press time*/
	misc_init_regval[3] &= ~RT5025_STARTLPRESS_MASK;
	misc_init_regval[3] |= (pdata->startlpress_time <<
		RT5025_STARTLPRESS_SHFT);
	/*systemlv enable shutdown*/
	misc_init_regval[5] &= ~RT5025_VSYSLVSHDN_MASK;
	misc_init_regval[5] |= (pdata->vsyslv_enshdn <<
		RT5025_VSYSLVSHDN_SHFT);
	rc = rt5025_misc_reginit(mi->i2c);
	return rc;
}

static int rt5025_misc_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5025_misc_info *mi;
	bool use_dt = pdev->dev.of_node;
	int ret = 0;

	mi = devm_kzalloc(&pdev->dev, sizeof(*mi), GFP_KERNEL);
	if (!mi)
		return -ENOMEM;

	mi->i2c = chip->i2c;
	mi->dev = &pdev->dev;
	if (use_dt) {
		rt_parse_dt(mi, &pdev->dev);
	} else {
		if (!pdata) {
			dev_err(&pdev->dev, "no initial platform data\n");
			ret = -EINVAL;
			goto err_init;
		}
		pdev->dev.platform_data = pdata->misc_pdata;
		rt_parse_pdata(mi, &pdev->dev);
	}
	/*for shutdown control*/
	g_shdn = chip->i2c;

	if (rt_pm_off && !pm_power_off)
		pm_power_off = rt5025_power_off;

	platform_set_drvdata(pdev, mi);
	chip->misc_info = mi;
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
err_init:
	return ret;
}

static int rt5025_misc_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);
	return 0;
}

static struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025-misc",},
	{},
};

static struct platform_driver rt5025_misc_driver = {
	.driver = {
		.name = RT5025_DEV_NAME "-misc",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt5025_misc_probe,
	.remove = rt5025_misc_remove,
};

static int rt5025_misc_init(void)
{
	return platform_driver_register(&rt5025_misc_driver);
}
subsys_initcall(rt5025_misc_init);

static void rt5025_misc_exit(void)
{
	platform_driver_unregister(&rt5025_misc_driver);
}
module_exit(rt5025_misc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Misc driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEV_NAME "-misc");
MODULE_VERSION(RT5025_DRV_VER);

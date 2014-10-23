/*
 *  drivers/mfd/rt5036-misc.c
 *  Driver for Richtek RT5036 PMIC misc option
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
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#ifdef CONFIG_MISC_RT5036_PWRKEY
#include <linux/input.h>
#endif /* #ifdef CONFIG_MISC_RT5036_PWRKEY */

#include <linux/mfd/rt5036/rt5036.h>
#include <linux/mfd/rt5036/rt5036-misc.h>
#include <asm/system_misc.h>

static struct i2c_client *g_shdn;

static unsigned char misc_init_regval[] = {
	0xA8,			/*REG 0x51*/
	0x96,			/*REG 0x52*/
	0x48,			/*REG 0x53*/
	0x00,			/*REG 0x54*/
	0x06,			/*REG 0x55*/
	0xA0,			/*REG 0x65*/
	0xFF,			/*REG 0x5A*/
	0xE0,			/*REG 0x5B*/
#ifdef CONFIG_MISC_RT5036_PWRKEY
	0x18,			/*REG 0x5C*/
#else
	0x78,			/*REG 0x5C*/
#endif /* #ifdef CONFIG_RT5036_PWRKEY */
};

int rt5036_vin_exist(void)
{
	int ret = 0;
#ifdef CONFIG_CHARGER_RT5036
	union power_supply_propval pval;
	struct power_supply *psy = power_supply_get_by_name("rt-charger");

	if (psy) {
		ret = psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &pval);
		if (ret < 0)
			ret = 0;
		else
			ret = pval.intval;
	} else {
		pr_err("couldn't get rt-charger psy\n");
	}
	return ret;
#else
	if (g_shdn)
		ret = rt5036_reg_read(g_shdn, RT5036_REG_CHGSTAT2);
	return ret < 0 ? 0 : ret & RT5036_PWRRDY_MASK;
#endif /* #ifdef CONFIG_CHARGER_RT5036 */
}
EXPORT_SYMBOL(rt5036_vin_exist);
static bool rt_pm_off;
void rt5036_chip_shutdown(void)
{
	pr_info("%s\n", __func__);
	if (rt5036_vin_exist()) {
		arm_pm_restart('h', "charge");
	} else {
		if (g_shdn) {
			pr_info("chip enter shutdown process\n");
			rt5036_set_bits(g_shdn, RT5036_REG_MISC3,
					RT5036_CHIPSHDN_MASK);
			rt5036_clr_bits(g_shdn, RT5036_REG_MISC3,
					RT5036_CHIPSHDN_MASK);
		}
	}
}
EXPORT_SYMBOL(rt5036_chip_shutdown);

static void rt5036_general_irq_handler(void *info, int eventno)
{
	struct rt5036_misc_info *mi = info;

	dev_info(mi->dev, "eventno=%02d\n", eventno);
#ifdef CONFIG_MISC_RT5036_PWRKEY
	switch (eventno) {
	case MISCEVENT_PWRONF:
		if (!mi->pwr_key_pressed) {
			input_report_key(mi->pwr_key, KEY_POWER, 1);
			input_sync(mi->pwr_key);
			mi->pwr_key_pressed = 1;
		}
		break;
	case MISCEVENT_PWRONR:
		if (mi->pwr_key_pressed) {
			input_report_key(mi->pwr_key, KEY_POWER, 0);
			input_sync(mi->pwr_key);
			mi->pwr_key_pressed = 0;
		}
		break;
	default:
		break;
	}
#endif /* #ifdef CONFIG_MISC_RT5036_PWRKEY */
}

static rt_irq_handler rt_miscirq_handler[MISCEVENT_MAX] = {
	[MISCEVENT_PWRONLP] = rt5036_general_irq_handler,
	[MISCEVENT_PWRONSP] = rt5036_general_irq_handler,
	[MISCEVENT_PWRONF] = rt5036_general_irq_handler,
	[MISCEVENT_PWRONR] = rt5036_general_irq_handler,
	[MISCEVENT_KPSHDN] = rt5036_general_irq_handler,
	[MISCEVENT_VDDALV] = rt5036_general_irq_handler,
	[MISCEVNET_OTM] = rt5036_general_irq_handler,
	[MISCEVENT_PMICSYSLV] = rt5036_general_irq_handler,
	[MISCEVENT_LSW2LV] = rt5036_general_irq_handler,
	[MISCEVENT_LSW1LV] = rt5036_general_irq_handler,
	[MISCEVENT_LDO4LV] = rt5036_general_irq_handler,
	[MISCEVENT_LDO3LV] = rt5036_general_irq_handler,
	[MISCEVENT_LDO2LV] = rt5036_general_irq_handler,
	[MISCEVENT_LDO1LV] = rt5036_general_irq_handler,
	[MISCEVENT_BUCK4LV] = rt5036_general_irq_handler,
	[MISCEVENT_BUCK3LV] = rt5036_general_irq_handler,
	[MISCEVENT_BUCK2LV] = rt5036_general_irq_handler,
	[MISCEVENT_BUCK1LV] = rt5036_general_irq_handler,
};

void rt5036_misc_irq_handler(struct rt5036_misc_info *mi, unsigned int irqevent)
{
	int i;
	unsigned int masked_irq_event =
	    (misc_init_regval[6] << 16) | (misc_init_regval[7] << 8) |
	    misc_init_regval[8];
	unsigned int final_irq_event = irqevent & (~masked_irq_event);

	for (i = 0; i < MISCEVENT_MAX; i++) {
		if ((final_irq_event & (1 << i)) && rt_miscirq_handler[i])
			rt_miscirq_handler[i] (mi, i);
	}
}
EXPORT_SYMBOL(rt5036_misc_irq_handler);

static int rt5036_misc_reginit(struct i2c_client *i2c)
{
	rt5036_reg_write(i2c, RT5036_REG_MISC6, misc_init_regval[5]);
	rt5036_reg_block_write(i2c, RT5036_REG_MISC1, 5, misc_init_regval);
	rt5036_reg_block_write(i2c, RT5036_REG_BUCKLDOIRQMASK,
			       3, &misc_init_regval[6]);
	/*always clear at the first time*/
	rt5036_reg_read(i2c, RT5036_REG_BUCKLDOIRQ);
	rt5036_reg_read(i2c, RT5036_REG_LSWBASEIRQ);
	rt5036_reg_read(i2c, RT5036_REG_PWRKEYIRQ);
	return 0;
}

static int rt_parse_dt(struct rt5036_misc_info *mi,
				 struct device *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	u32 val;

	if (of_property_read_u32(np, "rt,shdn_press", &val)) {
		dev_info(dev,
			 "no shut_lpress property, use the default value\n");
	} else {
		if (val > RT5036_SHDNPRESS_MASK)
			val = RT5036_SHDNPRESS_MAX;
		misc_init_regval[1] &= (~RT5036_SHDNPRESS_MASK);
		misc_init_regval[1] |= (val << RT5036_SHDNPRESS_SHIFT);
	}

	if (of_property_read_u32(np, "rt,stb_en", &val)) {
		dev_info(dev, "no stb_en prpperty , use the default value\n");
	} else {
		if (val > RT5036_STB_MAX)
			val = RT5036_STB_MAX;
		misc_init_regval[2] &= (~RT5036_STBEN_MASK);
		misc_init_regval[2] |= (val << RT5036_STBEN_SHIFT);
	}

	if (of_property_read_bool(np, "rt,lp_enshdn"))
		misc_init_regval[4] |= RT5036_LPSHDNEN_MASK;
	else
		misc_init_regval[4] &= (~RT5036_LPSHDNEN_MASK);

	if (of_property_read_u32(np, "rt,vsysuvlo", &val)) {
		dev_info(dev, "no vsysuvlo prpperty , use the default value\n");
	} else {
		if (val > RT5036_SYSLV_MAX)
			val = RT5036_SYSLV_MAX;
		misc_init_regval[5] &= (~RT5036_SYSUVLO_MASK);
		misc_init_regval[5] |= (val << RT5036_SYSUVLO_SHIFT);
	}

	if (of_property_read_bool(np, "rt,syslv_enshdn"))
		misc_init_regval[4] |= RT5036_SYSLVENSHDN_MASK;
	else
		misc_init_regval[4] &= (~RT5036_SYSLVENSHDN_MASK);

	rt_pm_off = of_property_read_bool(np, "rt,system-power-controller");
#endif /* #ifdef CONFIG_OF */
	rt5036_misc_reginit(mi->i2c);
	RTINFO("\n");
	return 0;
}

static int rt_parse_pdata(struct rt5036_misc_info *mi,
				    struct device *dev)
{
	struct rt5036_misc_data *misc_pdata = dev->platform_data;
	/*SHDN_PRESS_TIME property*/
	misc_init_regval[1] &= (~RT5036_SHDNPRESS_MASK);
	misc_init_regval[1] |=
	    (misc_pdata->shdn_press << RT5036_SHDNPRESS_SHIFT);
	/*STB_EN property*/
	misc_init_regval[2] &= (~RT5036_STBEN_MASK);
	misc_init_regval[2] |= (misc_pdata->stb_en << RT5036_STBEN_SHIFT);
	/*LP_ENSHEN property*/
	if (misc_pdata->lp_enshdn)
		misc_init_regval[4] |= RT5036_LPSHDNEN_MASK;
	else
		misc_init_regval[4] &= (~RT5036_LPSHDNEN_MASK);

	misc_init_regval[5] &= (~RT5036_SYSUVLO_MASK);
	misc_init_regval[5] |= (misc_pdata->vsysuvlo << RT5036_SYSUVLO_SHIFT);

	if (misc_pdata->syslv_enshdn)
		misc_init_regval[4] |= RT5036_SYSLVENSHDN_MASK;
	else
		misc_init_regval[4] &= (~RT5036_SYSLVENSHDN_MASK);

	rt5036_misc_reginit(mi->i2c);
	RTINFO("\n");
	return 0;
}

static int rt5036_misc_probe(struct platform_device *pdev)
{
	struct rt5036_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5036_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5036_misc_info *mi;
	bool use_dt = pdev->dev.of_node;

	mi = devm_kzalloc(&pdev->dev, sizeof(*mi), GFP_KERNEL);
	if (!mi)
		return -ENOMEM;

	mi->i2c = chip->i2c;
	if (use_dt) {
		rt_parse_dt(mi, &pdev->dev);
	} else {
		if (!pdata)
			goto out_dev;
		pdev->dev.platform_data = pdata->misc_pdata;
		rt_parse_pdata(mi, &pdev->dev);
	}
#ifdef CONFIG_MISC_RT5036_PWRKEY
	mi->pwr_key = input_allocate_device();
	if (!mi->pwr_key) {
		dev_err(&pdev->dev, "Allocate pwr_key input fail\n");
		goto out_dev;
	}
	input_set_capability(mi->pwr_key, EV_KEY, KEY_POWER);
	mi->pwr_key->name = "rt5036_pwr_key";
	mi->pwr_key->phys = "rt5036_pwr_key/input0";
	mi->pwr_key->dev.parent = &pdev->dev;
	if (input_register_device(mi->pwr_key)) {
		dev_err(&pdev->dev, "register pwr key fail\n");
		goto free_input;
	}
#endif /* #ifdef CONFIG_MISC_RT5036_PWRKEY */
	mi->dev = &pdev->dev;
	g_shdn = mi->i2c;
	chip->misc_info = mi;
	platform_set_drvdata(pdev, mi);

	if (rt_pm_off && !pm_power_off)
		pm_power_off = rt5036_chip_shutdown;

	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
#ifdef CONFIG_MISC_RT5036_PWRKEY
free_input:
	input_free_device(mi->pwr_key);
#endif /* #ifdef CONFIG_MISC_RT5036_PWRKEY */
out_dev:
	return -EINVAL;
}

static int rt5036_misc_remove(struct platform_device *pdev)
{
#ifdef CONFIG_MISC_RT5036_PWRKEY
	struct rt5036_misc_info *mi = platform_get_drvdata(pdev);

	input_unregister_device(mi->pwr_key);
	input_free_device(mi->pwr_key);
#endif /* #ifdef CONFIG_MISC_RT5036_PWRKEY */
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt5036-misc",},
	{},
};

static struct platform_driver rt5036_misc_driver = {
	.driver = {
		   .name = RT5036_DEV_NAME "-misc",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt5036_misc_probe,
	.remove = rt5036_misc_remove,
};

static int __init rt5036_misc_init(void)
{
	return platform_driver_register(&rt5036_misc_driver);
}
subsys_initcall(rt5036_misc_init);

static void __exit rt5036_misc_exit(void)
{
	platform_driver_unregister(&rt5036_misc_driver);
}
module_exit(rt5036_misc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Misc driver for RT5036");
MODULE_ALIAS("platform:" RT5036_DEV_NAME "-misc");
MODULE_VERSION(RT5036_DRV_VER);

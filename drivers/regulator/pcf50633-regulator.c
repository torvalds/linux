/* NXP PCF50633 PMIC Driver
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * Author: Balaji Rao <balajirrao@openmoko.org>
 * All rights reserved.
 *
 * Broken down from monstrous PCF50633 driver mainly by
 * Harald Welte and Andy Green and Werner Almesberger
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <linux/mfd/pcf50633/core.h>
#include <linux/mfd/pcf50633/pmic.h>

#define PCF50633_REGULATOR(_name, _id, _n)			\
	{							\
		.name = _name,					\
		.id = PCF50633_REGULATOR_##_id,			\
		.ops = &pcf50633_regulator_ops,			\
		.n_voltages = _n,				\
		.type = REGULATOR_VOLTAGE,			\
		.owner = THIS_MODULE,				\
		.vsel_reg = PCF50633_REG_##_id##OUT,		\
		.vsel_mask = 0xff,				\
		.enable_reg = PCF50633_REG_##_id##OUT + 1,	\
		.enable_mask = PCF50633_REGULATOR_ON,		\
	}

/* Bits from voltage value */
static u8 auto_voltage_bits(unsigned int millivolts)
{
	if (millivolts < 1800)
		return 0x2f;
	if (millivolts > 3800)
		return 0xff;

	millivolts -= 625;

	return millivolts / 25;
}

static u8 down_voltage_bits(unsigned int millivolts)
{
	if (millivolts < 625)
		return 0;
	else if (millivolts > 3000)
		return 0xff;

	millivolts -= 625;

	return millivolts / 25;
}

static u8 ldo_voltage_bits(unsigned int millivolts)
{
	if (millivolts < 900)
		return 0;
	else if (millivolts > 3600)
		return 0x1f;

	millivolts -= 900;
	return millivolts / 100;
}

/* Obtain voltage value from bits */
static unsigned int auto_voltage_value(u8 bits)
{
	/* AUTOOUT: 00000000 to 00101110 are reserved.
	 * Return 0 for bits in reserved range, which means this selector code
	 * can't be used on this system */
	if (bits < 0x2f)
		return 0;

	return 625 + (bits * 25);
}


static unsigned int down_voltage_value(u8 bits)
{
	return 625 + (bits * 25);
}


static unsigned int ldo_voltage_value(u8 bits)
{
	bits &= 0x1f;

	return 900 + (bits * 100);
}

static int pcf50633_regulator_map_voltage(struct regulator_dev *rdev,
					  int min_uV, int max_uV)
{
	struct pcf50633 *pcf;
	int regulator_id, millivolts;
	u8 volt_bits;

	pcf = rdev_get_drvdata(rdev);

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= PCF50633_NUM_REGULATORS)
		return -EINVAL;

	millivolts = min_uV / 1000;

	switch (regulator_id) {
	case PCF50633_REGULATOR_AUTO:
		volt_bits = auto_voltage_bits(millivolts);
		break;
	case PCF50633_REGULATOR_DOWN1:
	case PCF50633_REGULATOR_DOWN2:
		volt_bits = down_voltage_bits(millivolts);
		break;
	case PCF50633_REGULATOR_LDO1:
	case PCF50633_REGULATOR_LDO2:
	case PCF50633_REGULATOR_LDO3:
	case PCF50633_REGULATOR_LDO4:
	case PCF50633_REGULATOR_LDO5:
	case PCF50633_REGULATOR_LDO6:
	case PCF50633_REGULATOR_HCLDO:
	case PCF50633_REGULATOR_MEMLDO:
		volt_bits = ldo_voltage_bits(millivolts);
		break;
	default:
		return -EINVAL;
	}

	return volt_bits;
}

static int pcf50633_regulator_list_voltage(struct regulator_dev *rdev,
						unsigned int index)
{
	int regulator_id = rdev_get_id(rdev);

	int millivolts;

	switch (regulator_id) {
	case PCF50633_REGULATOR_AUTO:
		millivolts = auto_voltage_value(index);
		break;
	case PCF50633_REGULATOR_DOWN1:
	case PCF50633_REGULATOR_DOWN2:
		millivolts = down_voltage_value(index);
		break;
	case PCF50633_REGULATOR_LDO1:
	case PCF50633_REGULATOR_LDO2:
	case PCF50633_REGULATOR_LDO3:
	case PCF50633_REGULATOR_LDO4:
	case PCF50633_REGULATOR_LDO5:
	case PCF50633_REGULATOR_LDO6:
	case PCF50633_REGULATOR_HCLDO:
	case PCF50633_REGULATOR_MEMLDO:
		millivolts = ldo_voltage_value(index);
		break;
	default:
		return -EINVAL;
	}

	return millivolts * 1000;
}

static struct regulator_ops pcf50633_regulator_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = pcf50633_regulator_list_voltage,
	.map_voltage = pcf50633_regulator_map_voltage,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_desc regulators[] = {
	[PCF50633_REGULATOR_AUTO] = PCF50633_REGULATOR("auto", AUTO, 128),
	[PCF50633_REGULATOR_DOWN1] = PCF50633_REGULATOR("down1", DOWN1, 96),
	[PCF50633_REGULATOR_DOWN2] = PCF50633_REGULATOR("down2", DOWN2, 96),
	[PCF50633_REGULATOR_LDO1] = PCF50633_REGULATOR("ldo1", LDO1, 28),
	[PCF50633_REGULATOR_LDO2] = PCF50633_REGULATOR("ldo2", LDO2, 28),
	[PCF50633_REGULATOR_LDO3] = PCF50633_REGULATOR("ldo3", LDO3, 28),
	[PCF50633_REGULATOR_LDO4] = PCF50633_REGULATOR("ldo4", LDO4, 28),
	[PCF50633_REGULATOR_LDO5] = PCF50633_REGULATOR("ldo5", LDO5, 28),
	[PCF50633_REGULATOR_LDO6] = PCF50633_REGULATOR("ldo6", LDO6, 28),
	[PCF50633_REGULATOR_HCLDO] = PCF50633_REGULATOR("hcldo", HCLDO, 28),
	[PCF50633_REGULATOR_MEMLDO] = PCF50633_REGULATOR("memldo", MEMLDO, 28),
};

static int __devinit pcf50633_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct pcf50633 *pcf;
	struct regulator_config config = { };

	/* Already set by core driver */
	pcf = dev_to_pcf50633(pdev->dev.parent);

	config.dev = &pdev->dev;
	config.init_data = pdev->dev.platform_data;
	config.driver_data = pcf;
	config.regmap = pcf->regmap;

	rdev = regulator_register(&regulators[pdev->id], &config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	platform_set_drvdata(pdev, rdev);

	if (pcf->pdata->regulator_registered)
		pcf->pdata->regulator_registered(pcf, pdev->id);

	return 0;
}

static int __devexit pcf50633_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	regulator_unregister(rdev);

	return 0;
}

static struct platform_driver pcf50633_regulator_driver = {
	.driver = {
		.name = "pcf50633-regltr",
	},
	.probe = pcf50633_regulator_probe,
	.remove = __devexit_p(pcf50633_regulator_remove),
};

static int __init pcf50633_regulator_init(void)
{
	return platform_driver_register(&pcf50633_regulator_driver);
}
subsys_initcall(pcf50633_regulator_init);

static void __exit pcf50633_regulator_exit(void)
{
	platform_driver_unregister(&pcf50633_regulator_driver);
}
module_exit(pcf50633_regulator_exit);

MODULE_AUTHOR("Balaji Rao <balajirrao@openmoko.org>");
MODULE_DESCRIPTION("PCF50633 regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pcf50633-regulator");

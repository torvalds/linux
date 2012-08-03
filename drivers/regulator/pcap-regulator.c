/*
 * PCAP2 Regulator Driver
 *
 * Copyright (c) 2009 Daniel Ribeiro <drwyrm@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/ezx-pcap.h>

static const unsigned int V1_table[] = {
	2775000, 1275000, 1600000, 1725000, 1825000, 1925000, 2075000, 2275000,
};

static const unsigned int V2_table[] = {
	2500000, 2775000,
};

static const unsigned int V3_table[] = {
	1075000, 1275000, 1550000, 1725000, 1876000, 1950000, 2075000, 2275000,
};

static const unsigned int V4_table[] = {
	1275000, 1550000, 1725000, 1875000, 1950000, 2075000, 2275000, 2775000,
};

static const unsigned int V5_table[] = {
	1875000, 2275000, 2475000, 2775000,
};

static const unsigned int V6_table[] = {
	2475000, 2775000,
};

static const unsigned int V7_table[] = {
	1875000, 2775000,
};

#define V8_table V4_table

static const unsigned int V9_table[] = {
	1575000, 1875000, 2475000, 2775000,
};

static const unsigned int V10_table[] = {
	5000000,
};

static const unsigned int VAUX1_table[] = {
	1875000, 2475000, 2775000, 3000000,
};

#define VAUX2_table VAUX1_table

static const unsigned int VAUX3_table[] = {
	1200000, 1200000, 1200000, 1200000, 1400000, 1600000, 1800000, 2000000,
	2200000, 2400000, 2600000, 2800000, 3000000, 3200000, 3400000, 3600000,
};

static const unsigned int VAUX4_table[] = {
	1800000, 1800000, 3000000, 5000000,
};

static const unsigned int VSIM_table[] = {
	1875000, 3000000,
};

static const unsigned int VSIM2_table[] = {
	1875000,
};

static const unsigned int VVIB_table[] = {
	1300000, 1800000, 2000000, 3000000,
};

static const unsigned int SW1_table[] = {
	 900000,  950000, 1000000, 1050000, 1100000, 1150000, 1200000, 1250000,
	1300000, 1350000, 1400000, 1450000, 1500000, 1600000, 1875000, 2250000,
};

#define SW2_table SW1_table

static const unsigned int SW3_table[] = {
	4000000, 4500000, 5000000, 5500000,
};

struct pcap_regulator {
	const u8 reg;
	const u8 en;
	const u8 index;
	const u8 stby;
	const u8 lowpwr;
};

#define NA 0xff

#define VREG_INFO(_vreg, _reg, _en, _index, _stby, _lowpwr)		\
	[_vreg]	= {							\
		.reg		= _reg,					\
		.en		= _en,					\
		.index		= _index,				\
		.stby		= _stby,				\
		.lowpwr		= _lowpwr,				\
	}

static struct pcap_regulator vreg_table[] = {
	VREG_INFO(V1,    PCAP_REG_VREG1,   1,  2,  18, 0),
	VREG_INFO(V2,    PCAP_REG_VREG1,   5,  6,  19, 22),
	VREG_INFO(V3,    PCAP_REG_VREG1,   7,  8,  20, 23),
	VREG_INFO(V4,    PCAP_REG_VREG1,   11, 12, 21, 24),
	/* V5 STBY and LOWPWR are on PCAP_REG_VREG2 */
	VREG_INFO(V5,    PCAP_REG_VREG1,   15, 16, 12, 19),

	VREG_INFO(V6,    PCAP_REG_VREG2,   1,  2,  14, 20),
	VREG_INFO(V7,    PCAP_REG_VREG2,   3,  4,  15, 21),
	VREG_INFO(V8,    PCAP_REG_VREG2,   5,  6,  16, 22),
	VREG_INFO(V9,    PCAP_REG_VREG2,   9,  10, 17, 23),
	VREG_INFO(V10,   PCAP_REG_VREG2,   10, NA, 18, 24),

	VREG_INFO(VAUX1, PCAP_REG_AUXVREG, 1,  2,  22, 23),
	/* VAUX2 ... VSIM2 STBY and LOWPWR are on PCAP_REG_LOWPWR */
	VREG_INFO(VAUX2, PCAP_REG_AUXVREG, 4,  5,  0,  1),
	VREG_INFO(VAUX3, PCAP_REG_AUXVREG, 7,  8,  2,  3),
	VREG_INFO(VAUX4, PCAP_REG_AUXVREG, 12, 13, 4,  5),
	VREG_INFO(VSIM,  PCAP_REG_AUXVREG, 17, 18, NA, 6),
	VREG_INFO(VSIM2, PCAP_REG_AUXVREG, 16, NA, NA, 7),
	VREG_INFO(VVIB,  PCAP_REG_AUXVREG, 19, 20, NA, NA),

	VREG_INFO(SW1,   PCAP_REG_SWCTRL,  1,  2,  NA, NA),
	VREG_INFO(SW2,   PCAP_REG_SWCTRL,  6,  7,  NA, NA),
	/* SW3 STBY is on PCAP_REG_AUXVREG */
	VREG_INFO(SW3,   PCAP_REG_SWCTRL,  11, 12, 24, NA),

	/* SWxS used to control SWx voltage on standby */
/*	VREG_INFO(SW1S,  PCAP_REG_LOWPWR,  NA, 12, NA, NA),
	VREG_INFO(SW2S,  PCAP_REG_LOWPWR,  NA, 20, NA, NA), */
};

static int pcap_regulator_set_voltage_sel(struct regulator_dev *rdev,
					  unsigned selector)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);

	/* the regulator doesn't support voltage switching */
	if (rdev->desc->n_voltages == 1)
		return -EINVAL;

	return ezx_pcap_set_bits(pcap, vreg->reg,
				 (rdev->desc->n_voltages - 1) << vreg->index,
				 selector << vreg->index);
}

static int pcap_regulator_get_voltage_sel(struct regulator_dev *rdev)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);
	u32 tmp;

	if (rdev->desc->n_voltages == 1)
		return 0;

	ezx_pcap_read(pcap, vreg->reg, &tmp);
	tmp = ((tmp >> vreg->index) & (rdev->desc->n_voltages - 1));
	return tmp;
}

static int pcap_regulator_enable(struct regulator_dev *rdev)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);

	if (vreg->en == NA)
		return -EINVAL;

	return ezx_pcap_set_bits(pcap, vreg->reg, 1 << vreg->en, 1 << vreg->en);
}

static int pcap_regulator_disable(struct regulator_dev *rdev)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);

	if (vreg->en == NA)
		return -EINVAL;

	return ezx_pcap_set_bits(pcap, vreg->reg, 1 << vreg->en, 0);
}

static int pcap_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);
	u32 tmp;

	if (vreg->en == NA)
		return -EINVAL;

	ezx_pcap_read(pcap, vreg->reg, &tmp);
	return (tmp >> vreg->en) & 1;
}

static struct regulator_ops pcap_regulator_ops = {
	.list_voltage	= regulator_list_voltage_table,
	.set_voltage_sel = pcap_regulator_set_voltage_sel,
	.get_voltage_sel = pcap_regulator_get_voltage_sel,
	.enable		= pcap_regulator_enable,
	.disable	= pcap_regulator_disable,
	.is_enabled	= pcap_regulator_is_enabled,
};

#define VREG(_vreg)						\
	[_vreg]	= {						\
		.name		= #_vreg,			\
		.id		= _vreg,			\
		.n_voltages	= ARRAY_SIZE(_vreg##_table),	\
		.volt_table	= _vreg##_table,		\
		.ops		= &pcap_regulator_ops,		\
		.type		= REGULATOR_VOLTAGE,		\
		.owner		= THIS_MODULE,			\
	}

static const struct regulator_desc pcap_regulators[] = {
	VREG(V1), VREG(V2), VREG(V3), VREG(V4), VREG(V5), VREG(V6), VREG(V7),
	VREG(V8), VREG(V9), VREG(V10), VREG(VAUX1), VREG(VAUX2), VREG(VAUX3),
	VREG(VAUX4), VREG(VSIM), VREG(VSIM2), VREG(VVIB), VREG(SW1), VREG(SW2),
};

static int __devinit pcap_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	void *pcap = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };

	config.dev = &pdev->dev;
	config.init_data = pdev->dev.platform_data;
	config.driver_data = pcap;

	rdev = regulator_register(&pcap_regulators[pdev->id], &config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static int __devexit pcap_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver pcap_regulator_driver = {
	.driver = {
		.name	= "pcap-regulator",
		.owner	= THIS_MODULE,
	},
	.probe	= pcap_regulator_probe,
	.remove	= __devexit_p(pcap_regulator_remove),
};

static int __init pcap_regulator_init(void)
{
	return platform_driver_register(&pcap_regulator_driver);
}

static void __exit pcap_regulator_exit(void)
{
	platform_driver_unregister(&pcap_regulator_driver);
}

subsys_initcall(pcap_regulator_init);
module_exit(pcap_regulator_exit);

MODULE_AUTHOR("Daniel Ribeiro <drwyrm@gmail.com>");
MODULE_DESCRIPTION("PCAP2 Regulator Driver");
MODULE_LICENSE("GPL");

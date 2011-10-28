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

static const u16 V1_table[] = {
	2775, 1275, 1600, 1725, 1825, 1925, 2075, 2275,
};

static const u16 V2_table[] = {
	2500, 2775,
};

static const u16 V3_table[] = {
	1075, 1275, 1550, 1725, 1876, 1950, 2075, 2275,
};

static const u16 V4_table[] = {
	1275, 1550, 1725, 1875, 1950, 2075, 2275, 2775,
};

static const u16 V5_table[] = {
	1875, 2275, 2475, 2775,
};

static const u16 V6_table[] = {
	2475, 2775,
};

static const u16 V7_table[] = {
	1875, 2775,
};

#define V8_table V4_table

static const u16 V9_table[] = {
	1575, 1875, 2475, 2775,
};

static const u16 V10_table[] = {
	5000,
};

static const u16 VAUX1_table[] = {
	1875, 2475, 2775, 3000,
};

#define VAUX2_table VAUX1_table

static const u16 VAUX3_table[] = {
	1200, 1200, 1200, 1200, 1400, 1600, 1800, 2000,
	2200, 2400, 2600, 2800, 3000, 3200, 3400, 3600,
};

static const u16 VAUX4_table[] = {
	1800, 1800, 3000, 5000,
};

static const u16 VSIM_table[] = {
	1875, 3000,
};

static const u16 VSIM2_table[] = {
	1875,
};

static const u16 VVIB_table[] = {
	1300, 1800, 2000, 3000,
};

static const u16 SW1_table[] = {
	900, 950, 1000, 1050, 1100, 1150, 1200, 1250,
	1300, 1350, 1400, 1450, 1500, 1600, 1875, 2250,
};

#define SW2_table SW1_table

static const u16 SW3_table[] = {
	4000, 4500, 5000, 5500,
};

struct pcap_regulator {
	const u8 reg;
	const u8 en;
	const u8 index;
	const u8 stby;
	const u8 lowpwr;
	const u8 n_voltages;
	const u16 *voltage_table;
};

#define NA 0xff

#define VREG_INFO(_vreg, _reg, _en, _index, _stby, _lowpwr)		\
	[_vreg]	= {							\
		.reg		= _reg,					\
		.en		= _en,					\
		.index		= _index,				\
		.stby		= _stby,				\
		.lowpwr		= _lowpwr,				\
		.n_voltages	= ARRAY_SIZE(_vreg##_table),		\
		.voltage_table	= _vreg##_table,			\
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

static int pcap_regulator_set_voltage(struct regulator_dev *rdev,
				      int min_uV, int max_uV,
				      unsigned *selector)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);
	int uV;
	u8 i;

	/* the regulator doesn't support voltage switching */
	if (vreg->n_voltages == 1)
		return -EINVAL;

	for (i = 0; i < vreg->n_voltages; i++) {
		/* For V1 the first is not the best match */
		if (i == 0 && rdev_get_id(rdev) == V1)
			i = 1;
		else if (i + 1 == vreg->n_voltages && rdev_get_id(rdev) == V1)
			i = 0;

		uV = vreg->voltage_table[i] * 1000;
		if (min_uV <= uV && uV <= max_uV) {
			*selector = i;
			return ezx_pcap_set_bits(pcap, vreg->reg,
					(vreg->n_voltages - 1) << vreg->index,
					i << vreg->index);
		}

		if (i == 0 && rdev_get_id(rdev) == V1)
			i = vreg->n_voltages - 1;
	}

	/* the requested voltage range is not supported by this regulator */
	return -EINVAL;
}

static int pcap_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];
	void *pcap = rdev_get_drvdata(rdev);
	u32 tmp;
	int mV;

	if (vreg->n_voltages == 1)
		return vreg->voltage_table[0] * 1000;

	ezx_pcap_read(pcap, vreg->reg, &tmp);
	tmp = ((tmp >> vreg->index) & (vreg->n_voltages - 1));
	mV = vreg->voltage_table[tmp];

	return mV * 1000;
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

static int pcap_regulator_list_voltage(struct regulator_dev *rdev,
							unsigned int index)
{
	struct pcap_regulator *vreg = &vreg_table[rdev_get_id(rdev)];

	return vreg->voltage_table[index] * 1000;
}

static struct regulator_ops pcap_regulator_ops = {
	.list_voltage	= pcap_regulator_list_voltage,
	.set_voltage	= pcap_regulator_set_voltage,
	.get_voltage	= pcap_regulator_get_voltage,
	.enable		= pcap_regulator_enable,
	.disable	= pcap_regulator_disable,
	.is_enabled	= pcap_regulator_is_enabled,
};

#define VREG(_vreg)						\
	[_vreg]	= {						\
		.name		= #_vreg,			\
		.id		= _vreg,			\
		.n_voltages	= ARRAY_SIZE(_vreg##_table),	\
		.ops		= &pcap_regulator_ops,		\
		.type		= REGULATOR_VOLTAGE,		\
		.owner		= THIS_MODULE,			\
	}

static struct regulator_desc pcap_regulators[] = {
	VREG(V1), VREG(V2), VREG(V3), VREG(V4), VREG(V5), VREG(V6), VREG(V7),
	VREG(V8), VREG(V9), VREG(V10), VREG(VAUX1), VREG(VAUX2), VREG(VAUX3),
	VREG(VAUX4), VREG(VSIM), VREG(VSIM2), VREG(VVIB), VREG(SW1), VREG(SW2),
};

static int __devinit pcap_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	void *pcap = dev_get_drvdata(pdev->dev.parent);

	rdev = regulator_register(&pcap_regulators[pdev->id], &pdev->dev,
				pdev->dev.platform_data, pcap);
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

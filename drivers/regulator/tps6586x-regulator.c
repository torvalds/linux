/*
 * Regulator driver for TI TPS6586x
 *
 * Copyright (C) 2010 Compulab Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on da903x
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Copyright (C) 2008 Compulab Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps6586x.h>

/* supply control and voltage setting  */
#define TPS6586X_SUPPLYENA	0x10
#define TPS6586X_SUPPLYENB	0x11
#define TPS6586X_SUPPLYENC	0x12
#define TPS6586X_SUPPLYEND	0x13
#define TPS6586X_SUPPLYENE	0x14
#define TPS6586X_VCC1		0x20
#define TPS6586X_VCC2		0x21
#define TPS6586X_SM1V1		0x23
#define TPS6586X_SM1V2		0x24
#define TPS6586X_SM1SL		0x25
#define TPS6586X_SM0V1		0x26
#define TPS6586X_SM0V2		0x27
#define TPS6586X_SM0SL		0x28
#define TPS6586X_LDO2AV1	0x29
#define TPS6586X_LDO2AV2	0x2A
#define TPS6586X_LDO2BV1	0x2F
#define TPS6586X_LDO2BV2	0x30
#define TPS6586X_LDO4V1		0x32
#define TPS6586X_LDO4V2		0x33

/* converter settings  */
#define TPS6586X_SUPPLYV1	0x41
#define TPS6586X_SUPPLYV2	0x42
#define TPS6586X_SUPPLYV3	0x43
#define TPS6586X_SUPPLYV4	0x44
#define TPS6586X_SUPPLYV5	0x45
#define TPS6586X_SUPPLYV6	0x46
#define TPS6586X_SMODE1		0x47
#define TPS6586X_SMODE2		0x48

struct tps6586x_regulator {
	struct regulator_desc desc;

	int volt_reg;
	int volt_shift;
	int volt_nbits;
	int enable_bit[2];
	int enable_reg[2];

	int *voltages;

	/* for DVM regulators */
	int go_reg;
	int go_bit;
};

static inline struct device *to_tps6586x_dev(struct regulator_dev *rdev)
{
	return rdev_get_dev(rdev)->parent->parent;
}

static int tps6586x_ldo_list_voltage(struct regulator_dev *rdev,
				     unsigned selector)
{
	struct tps6586x_regulator *info = rdev_get_drvdata(rdev);

	return info->voltages[selector] * 1000;
}


static int __tps6586x_ldo_set_voltage(struct device *parent,
				      struct tps6586x_regulator *ri,
				      int min_uV, int max_uV,
				      unsigned *selector)
{
	int val, uV;
	uint8_t mask;

	for (val = 0; val < ri->desc.n_voltages; val++) {
		uV = ri->voltages[val] * 1000;

		/* LDO0 has minimal voltage 1.2 rather than 1.25 */
		if (ri->desc.id == TPS6586X_ID_LDO_0 && val == 0)
			uV -= 50 * 1000;

		/* use the first in-range value */
		if (min_uV <= uV && uV <= max_uV) {

			*selector = val;

			val <<= ri->volt_shift;
			mask = ((1 << ri->volt_nbits) - 1) << ri->volt_shift;

			return tps6586x_update(parent, ri->volt_reg, val, mask);
		}
	}

	return -EINVAL;
}

static int tps6586x_ldo_set_voltage(struct regulator_dev *rdev,
				    int min_uV, int max_uV, unsigned *selector)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);

	return __tps6586x_ldo_set_voltage(parent, ri, min_uV, max_uV,
					  selector);
}

static int tps6586x_ldo_get_voltage(struct regulator_dev *rdev)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);
	uint8_t val, mask;
	int ret;

	ret = tps6586x_read(parent, ri->volt_reg, &val);
	if (ret)
		return ret;

	mask = ((1 << ri->volt_nbits) - 1) << ri->volt_shift;
	val = (val & mask) >> ri->volt_shift;

	if (val >= ri->desc.n_voltages)
		BUG();

	return ri->voltages[val] * 1000;
}

static int tps6586x_dvm_set_voltage(struct regulator_dev *rdev,
				    int min_uV, int max_uV, unsigned *selector)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);
	int ret;

	ret = __tps6586x_ldo_set_voltage(parent, ri, min_uV, max_uV,
					 selector);
	if (ret)
		return ret;

	return tps6586x_set_bits(parent, ri->go_reg, 1 << ri->go_bit);
}

static int tps6586x_regulator_enable(struct regulator_dev *rdev)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);

	return tps6586x_set_bits(parent, ri->enable_reg[0],
				 1 << ri->enable_bit[0]);
}

static int tps6586x_regulator_disable(struct regulator_dev *rdev)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);

	return tps6586x_clr_bits(parent, ri->enable_reg[0],
				 1 << ri->enable_bit[0]);
}

static int tps6586x_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);
	uint8_t reg_val;
	int ret;

	ret = tps6586x_read(parent, ri->enable_reg[0], &reg_val);
	if (ret)
		return ret;

	return !!(reg_val & (1 << ri->enable_bit[0]));
}

static struct regulator_ops tps6586x_regulator_ldo_ops = {
	.list_voltage = tps6586x_ldo_list_voltage,
	.get_voltage = tps6586x_ldo_get_voltage,
	.set_voltage = tps6586x_ldo_set_voltage,

	.is_enabled = tps6586x_regulator_is_enabled,
	.enable = tps6586x_regulator_enable,
	.disable = tps6586x_regulator_disable,
};

static struct regulator_ops tps6586x_regulator_dvm_ops = {
	.list_voltage = tps6586x_ldo_list_voltage,
	.get_voltage = tps6586x_ldo_get_voltage,
	.set_voltage = tps6586x_dvm_set_voltage,

	.is_enabled = tps6586x_regulator_is_enabled,
	.enable = tps6586x_regulator_enable,
	.disable = tps6586x_regulator_disable,
};

static int tps6586x_ldo_voltages[] = {
	1250, 1500, 1800, 2500, 2700, 2850, 3100, 3300,
};

static int tps6586x_ldo4_voltages[] = {
	1700, 1725, 1750, 1775, 1800, 1825, 1850, 1875,
	1900, 1925, 1950, 1975, 2000, 2025, 2050, 2075,
	2100, 2125, 2150, 2175, 2200, 2225, 2250, 2275,
	2300, 2325, 2350, 2375, 2400, 2425, 2450, 2475,
};

static int tps6586x_sm2_voltages[] = {
	3000, 3050, 3100, 3150, 3200, 3250, 3300, 3350,
	3400, 3450, 3500, 3550, 3600, 3650, 3700, 3750,
	3800, 3850, 3900, 3950, 4000, 4050, 4100, 4150,
	4200, 4250, 4300, 4350, 4400, 4450, 4500, 4550,
};

static int tps6586x_dvm_voltages[] = {
	 725,  750,  775,  800,  825,  850,  875,  900,
	 925,  950,  975, 1000, 1025, 1050, 1075, 1100,
	1125, 1150, 1175, 1200, 1225, 1250, 1275, 1300,
	1325, 1350, 1375, 1400, 1425, 1450, 1475, 1500,
};

#define TPS6586X_REGULATOR(_id, vdata, _ops, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1)			\
	.desc	= {							\
		.name	= "REG-" #_id,					\
		.ops	= &tps6586x_regulator_##_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= TPS6586X_ID_##_id,				\
		.n_voltages = ARRAY_SIZE(tps6586x_##vdata##_voltages),	\
		.owner	= THIS_MODULE,					\
	},								\
	.volt_reg	= TPS6586X_##vreg,				\
	.volt_shift	= (shift),					\
	.volt_nbits	= (nbits),					\
	.enable_reg[0]	= TPS6586X_SUPPLY##ereg0,			\
	.enable_bit[0]	= (ebit0),					\
	.enable_reg[1]	= TPS6586X_SUPPLY##ereg1,			\
	.enable_bit[1]	= (ebit1),					\
	.voltages	= tps6586x_##vdata##_voltages,

#define TPS6586X_REGULATOR_DVM_GOREG(goreg, gobit)			\
	.go_reg = TPS6586X_##goreg,					\
	.go_bit = (gobit),

#define TPS6586X_LDO(_id, vdata, vreg, shift, nbits,			\
		     ereg0, ebit0, ereg1, ebit1)			\
{									\
	TPS6586X_REGULATOR(_id, vdata, ldo_ops, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1)			\
}

#define TPS6586X_DVM(_id, vdata, vreg, shift, nbits,			\
		     ereg0, ebit0, ereg1, ebit1, goreg, gobit)		\
{									\
	TPS6586X_REGULATOR(_id, vdata, dvm_ops, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1)			\
	TPS6586X_REGULATOR_DVM_GOREG(goreg, gobit)			\
}

static struct tps6586x_regulator tps6586x_regulator[] = {
	TPS6586X_LDO(LDO_0, ldo, SUPPLYV1, 5, 3, ENC, 0, END, 0),
	TPS6586X_LDO(LDO_3, ldo, SUPPLYV4, 0, 3, ENC, 2, END, 2),
	TPS6586X_LDO(LDO_5, ldo, SUPPLYV6, 0, 3, ENE, 6, ENE, 6),
	TPS6586X_LDO(LDO_6, ldo, SUPPLYV3, 0, 3, ENC, 4, END, 4),
	TPS6586X_LDO(LDO_7, ldo, SUPPLYV3, 3, 3, ENC, 5, END, 5),
	TPS6586X_LDO(LDO_8, ldo, SUPPLYV2, 5, 3, ENC, 6, END, 6),
	TPS6586X_LDO(LDO_9, ldo, SUPPLYV6, 3, 3, ENE, 7, ENE, 7),
	TPS6586X_LDO(LDO_RTC, ldo, SUPPLYV4, 3, 3, V4, 7, V4, 7),
	TPS6586X_LDO(LDO_1, dvm, SUPPLYV1, 0, 5, ENC, 1, END, 1),
	TPS6586X_LDO(SM_2, sm2, SUPPLYV2, 0, 5, ENC, 7, END, 7),

	TPS6586X_DVM(LDO_2, dvm, LDO2BV1, 0, 5, ENA, 3, ENB, 3, VCC2, 6),
	TPS6586X_DVM(LDO_4, ldo4, LDO4V1, 0, 5, ENC, 3, END, 3, VCC1, 6),
	TPS6586X_DVM(SM_0, dvm, SM0V1, 0, 5, ENA, 1, ENB, 1, VCC1, 2),
	TPS6586X_DVM(SM_1, dvm, SM1V1, 0, 5, ENA, 0, ENB, 0, VCC1, 0),
};

/*
 * TPS6586X has 2 enable bits that are OR'ed to determine the actual
 * regulator state. Clearing one of this bits allows switching
 * regulator on and of with single register write.
 */
static inline int tps6586x_regulator_preinit(struct device *parent,
					     struct tps6586x_regulator *ri)
{
	uint8_t val1, val2;
	int ret;

	if (ri->enable_reg[0] == ri->enable_reg[1] &&
	    ri->enable_bit[0] == ri->enable_bit[1])
			return 0;

	ret = tps6586x_read(parent, ri->enable_reg[0], &val1);
	if (ret)
		return ret;

	ret = tps6586x_read(parent, ri->enable_reg[1], &val2);
	if (ret)
		return ret;

	if (!(val2 & (1 << ri->enable_bit[1])))
		return 0;

	/*
	 * The regulator is on, but it's enabled with the bit we don't
	 * want to use, so we switch the enable bits
	 */
	if (!(val1 & (1 << ri->enable_bit[0]))) {
		ret = tps6586x_set_bits(parent, ri->enable_reg[0],
					1 << ri->enable_bit[0]);
		if (ret)
			return ret;
	}

	return tps6586x_clr_bits(parent, ri->enable_reg[1],
				 1 << ri->enable_bit[1]);
}

static inline struct tps6586x_regulator *find_regulator_info(int id)
{
	struct tps6586x_regulator *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(tps6586x_regulator); i++) {
		ri = &tps6586x_regulator[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

static int __devinit tps6586x_regulator_probe(struct platform_device *pdev)
{
	struct tps6586x_regulator *ri = NULL;
	struct regulator_dev *rdev;
	int id = pdev->id;
	int err;

	dev_dbg(&pdev->dev, "Probing reulator %d\n", id);

	ri = find_regulator_info(id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	err = tps6586x_regulator_preinit(pdev->dev.parent, ri);
	if (err)
		return err;

	rdev = regulator_register(&ri->desc, &pdev->dev,
				  pdev->dev.platform_data, ri);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static int __devexit tps6586x_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	return 0;
}

static struct platform_driver tps6586x_regulator_driver = {
	.driver	= {
		.name	= "tps6586x-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= tps6586x_regulator_probe,
	.remove		= __devexit_p(tps6586x_regulator_remove),
};

static int __init tps6586x_regulator_init(void)
{
	return platform_driver_register(&tps6586x_regulator_driver);
}
subsys_initcall(tps6586x_regulator_init);

static void __exit tps6586x_regulator_exit(void)
{
	platform_driver_unregister(&tps6586x_regulator_driver);
}
module_exit(tps6586x_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("Regulator Driver for TI TPS6586X PMIC");
MODULE_ALIAS("platform:tps6586x-regulator");

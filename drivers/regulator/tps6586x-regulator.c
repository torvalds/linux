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
#include <linux/module.h>
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

	int enable_bit[2];
	int enable_reg[2];

	/* for DVM regulators */
	int go_reg;
	int go_bit;
};

static inline struct device *to_tps6586x_dev(struct regulator_dev *rdev)
{
	return rdev_get_dev(rdev)->parent;
}

static int tps6586x_set_voltage_sel(struct regulator_dev *rdev,
				    unsigned selector)
{
	struct tps6586x_regulator *ri = rdev_get_drvdata(rdev);
	struct device *parent = to_tps6586x_dev(rdev);
	int ret, val, rid = rdev_get_id(rdev);
	uint8_t mask;

	val = selector << (ffs(rdev->desc->vsel_mask) - 1);
	mask = rdev->desc->vsel_mask;

	ret = tps6586x_update(parent, rdev->desc->vsel_reg, val, mask);
	if (ret)
		return ret;

	/* Update go bit for DVM regulators */
	switch (rid) {
	case TPS6586X_ID_LDO_2:
	case TPS6586X_ID_LDO_4:
	case TPS6586X_ID_SM_0:
	case TPS6586X_ID_SM_1:
		ret = tps6586x_set_bits(parent, ri->go_reg, 1 << ri->go_bit);
		break;
	}
	return ret;
}

static struct regulator_ops tps6586x_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = tps6586x_set_voltage_sel,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static struct regulator_ops tps6586x_sys_regulator_ops = {
};

static const unsigned int tps6586x_ldo0_voltages[] = {
	1200000, 1500000, 1800000, 2500000, 2700000, 2850000, 3100000, 3300000,
};

static const unsigned int tps6586x_ldo4_voltages[] = {
	1700000, 1725000, 1750000, 1775000, 1800000, 1825000, 1850000, 1875000,
	1900000, 1925000, 1950000, 1975000, 2000000, 2025000, 2050000, 2075000,
	2100000, 2125000, 2150000, 2175000, 2200000, 2225000, 2250000, 2275000,
	2300000, 2325000, 2350000, 2375000, 2400000, 2425000, 2450000, 2475000,
};

static const unsigned int tps6586x_ldo_voltages[] = {
	1250000, 1500000, 1800000, 2500000, 2700000, 2850000, 3100000, 3300000,
};

static const unsigned int tps6586x_sm2_voltages[] = {
	3000000, 3050000, 3100000, 3150000, 3200000, 3250000, 3300000, 3350000,
	3400000, 3450000, 3500000, 3550000, 3600000, 3650000, 3700000, 3750000,
	3800000, 3850000, 3900000, 3950000, 4000000, 4050000, 4100000, 4150000,
	4200000, 4250000, 4300000, 4350000, 4400000, 4450000, 4500000, 4550000,
};

static const unsigned int tps6586x_dvm_voltages[] = {
	 725000,  750000,  775000,  800000,  825000,  850000,  875000,  900000,
	 925000,  950000,  975000, 1000000, 1025000, 1050000, 1075000, 1100000,
	1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000,
	1325000, 1350000, 1375000, 1400000, 1425000, 1450000, 1475000, 1500000,
};

#define TPS6586X_REGULATOR(_id, _pin_name, vdata, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1)			\
	.desc	= {							\
		.supply_name = _pin_name,				\
		.name	= "REG-" #_id,					\
		.ops	= &tps6586x_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= TPS6586X_ID_##_id,				\
		.n_voltages = ARRAY_SIZE(tps6586x_##vdata##_voltages),	\
		.volt_table = tps6586x_##vdata##_voltages,		\
		.owner	= THIS_MODULE,					\
		.enable_reg = TPS6586X_SUPPLY##ereg0,			\
		.enable_mask = 1 << (ebit0),				\
		.vsel_reg = TPS6586X_##vreg,				\
		.vsel_mask = ((1 << (nbits)) - 1) << (shift),		\
	},								\
	.enable_reg[0]	= TPS6586X_SUPPLY##ereg0,			\
	.enable_bit[0]	= (ebit0),					\
	.enable_reg[1]	= TPS6586X_SUPPLY##ereg1,			\
	.enable_bit[1]	= (ebit1),

#define TPS6586X_REGULATOR_DVM_GOREG(goreg, gobit)			\
	.go_reg = TPS6586X_##goreg,					\
	.go_bit = (gobit),

#define TPS6586X_LDO(_id, _pname, vdata, vreg, shift, nbits,		\
		     ereg0, ebit0, ereg1, ebit1)			\
{									\
	TPS6586X_REGULATOR(_id, _pname, vdata, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1)			\
}

#define TPS6586X_DVM(_id, _pname, vdata, vreg, shift, nbits,		\
		     ereg0, ebit0, ereg1, ebit1, goreg, gobit)		\
{									\
	TPS6586X_REGULATOR(_id, _pname, vdata, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1)			\
	TPS6586X_REGULATOR_DVM_GOREG(goreg, gobit)			\
}

#define TPS6586X_SYS_REGULATOR()					\
{									\
	.desc	= {							\
		.supply_name = "sys",					\
		.name	= "REG-SYS",					\
		.ops	= &tps6586x_sys_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= TPS6586X_ID_SYS,				\
		.owner	= THIS_MODULE,					\
	},								\
}

static struct tps6586x_regulator tps6586x_regulator[] = {
	TPS6586X_SYS_REGULATOR(),
	TPS6586X_LDO(LDO_0, "vinldo01", ldo0, SUPPLYV1, 5, 3, ENC, 0, END, 0),
	TPS6586X_LDO(LDO_3, "vinldo23", ldo, SUPPLYV4, 0, 3, ENC, 2, END, 2),
	TPS6586X_LDO(LDO_5, "REG-SYS", ldo, SUPPLYV6, 0, 3, ENE, 6, ENE, 6),
	TPS6586X_LDO(LDO_6, "vinldo678", ldo, SUPPLYV3, 0, 3, ENC, 4, END, 4),
	TPS6586X_LDO(LDO_7, "vinldo678", ldo, SUPPLYV3, 3, 3, ENC, 5, END, 5),
	TPS6586X_LDO(LDO_8, "vinldo678", ldo, SUPPLYV2, 5, 3, ENC, 6, END, 6),
	TPS6586X_LDO(LDO_9, "vinldo9", ldo, SUPPLYV6, 3, 3, ENE, 7, ENE, 7),
	TPS6586X_LDO(LDO_RTC, "REG-SYS", ldo, SUPPLYV4, 3, 3, V4, 7, V4, 7),
	TPS6586X_LDO(LDO_1, "vinldo01", dvm, SUPPLYV1, 0, 5, ENC, 1, END, 1),
	TPS6586X_LDO(SM_2, "vin-sm2", sm2, SUPPLYV2, 0, 5, ENC, 7, END, 7),

	TPS6586X_DVM(LDO_2, "vinldo23", dvm, LDO2BV1, 0, 5, ENA, 3,
					ENB, 3, VCC2, 6),
	TPS6586X_DVM(LDO_4, "vinldo4", ldo4, LDO4V1, 0, 5, ENC, 3,
					END, 3, VCC1, 6),
	TPS6586X_DVM(SM_0, "vin-sm0", dvm, SM0V1, 0, 5, ENA, 1,
					ENB, 1, VCC1, 2),
	TPS6586X_DVM(SM_1, "vin-sm1", dvm, SM1V1, 0, 5, ENA, 0,
					ENB, 0, VCC1, 0),
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

static int tps6586x_regulator_set_slew_rate(struct platform_device *pdev)
{
	struct device *parent = pdev->dev.parent;
	struct regulator_init_data *p = pdev->dev.platform_data;
	struct tps6586x_settings *setting = p->driver_data;
	uint8_t reg;

	if (setting == NULL)
		return 0;

	if (!(setting->slew_rate & TPS6586X_SLEW_RATE_SET))
		return 0;

	/* only SM0 and SM1 can have the slew rate settings */
	switch (pdev->id) {
	case TPS6586X_ID_SM_0:
		reg = TPS6586X_SM0SL;
		break;
	case TPS6586X_ID_SM_1:
		reg = TPS6586X_SM1SL;
		break;
	default:
		dev_warn(&pdev->dev, "Only SM0/SM1 can set slew rate\n");
		return -EINVAL;
	}

	return tps6586x_write(parent, reg,
			setting->slew_rate & TPS6586X_SLEW_RATE_MASK);
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
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int id = pdev->id;
	int err;

	dev_dbg(&pdev->dev, "Probing regulator %d\n", id);

	ri = find_regulator_info(id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	err = tps6586x_regulator_preinit(pdev->dev.parent, ri);
	if (err)
		return err;

	config.dev = pdev->dev.parent;
	config.of_node = pdev->dev.of_node;
	config.init_data = pdev->dev.platform_data;
	config.driver_data = ri;

	rdev = regulator_register(&ri->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);

	return tps6586x_regulator_set_slew_rate(pdev);
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for TI TPS6586x
 *
 * Copyright (C) 2010 Compulab Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on da903x
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Copyright (C) 2008 Compulab Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
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
};

static const struct regulator_ops tps6586x_rw_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static const struct regulator_ops tps6586x_rw_linear_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static const struct regulator_ops tps6586x_ro_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,

	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static const struct regulator_ops tps6586x_sys_regulator_ops = {
};

static const unsigned int tps6586x_ldo0_voltages[] = {
	1200000, 1500000, 1800000, 2500000, 2700000, 2850000, 3100000, 3300000,
};

static const unsigned int tps6586x_ldo_voltages[] = {
	1250000, 1500000, 1800000, 2500000, 2700000, 2850000, 3100000, 3300000,
};

static const unsigned int tps658640_rtc_voltages[] = {
	2500000, 2850000, 3100000, 3300000,
};

#define TPS6586X_REGULATOR(_id, _ops, _pin_name, vdata, vreg, shift, nbits, \
			   ereg0, ebit0, ereg1, ebit1, goreg, gobit)	\
	.desc	= {							\
		.supply_name = _pin_name,				\
		.name	= "REG-" #_id,					\
		.ops	= &tps6586x_## _ops ## _regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= TPS6586X_ID_##_id,				\
		.n_voltages = ARRAY_SIZE(vdata##_voltages),		\
		.volt_table = vdata##_voltages,				\
		.owner	= THIS_MODULE,					\
		.enable_reg = TPS6586X_SUPPLY##ereg0,			\
		.enable_mask = 1 << (ebit0),				\
		.vsel_reg = TPS6586X_##vreg,				\
		.vsel_mask = ((1 << (nbits)) - 1) << (shift),		\
		.apply_reg = (goreg),				\
		.apply_bit = (gobit),				\
	},								\
	.enable_reg[0]	= TPS6586X_SUPPLY##ereg0,			\
	.enable_bit[0]	= (ebit0),					\
	.enable_reg[1]	= TPS6586X_SUPPLY##ereg1,			\
	.enable_bit[1]	= (ebit1),

#define TPS6586X_REGULATOR_LINEAR(_id, _ops, _pin_name, n_volt, min_uv,	\
				  uv_step, vreg, shift, nbits, ereg0,	\
				  ebit0, ereg1, ebit1, goreg, gobit)	\
	.desc	= {							\
		.supply_name = _pin_name,				\
		.name	= "REG-" #_id,					\
		.ops	= &tps6586x_## _ops ## _regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= TPS6586X_ID_##_id,				\
		.n_voltages = n_volt,					\
		.min_uV = min_uv,					\
		.uV_step = uv_step,					\
		.owner	= THIS_MODULE,					\
		.enable_reg = TPS6586X_SUPPLY##ereg0,			\
		.enable_mask = 1 << (ebit0),				\
		.vsel_reg = TPS6586X_##vreg,				\
		.vsel_mask = ((1 << (nbits)) - 1) << (shift),		\
		.apply_reg = (goreg),				\
		.apply_bit = (gobit),				\
	},								\
	.enable_reg[0]	= TPS6586X_SUPPLY##ereg0,			\
	.enable_bit[0]	= (ebit0),					\
	.enable_reg[1]	= TPS6586X_SUPPLY##ereg1,			\
	.enable_bit[1]	= (ebit1),

#define TPS6586X_LDO(_id, _pname, vdata, vreg, shift, nbits,		\
		     ereg0, ebit0, ereg1, ebit1)			\
{									\
	TPS6586X_REGULATOR(_id, rw, _pname, vdata, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1, 0, 0)		\
}

#define TPS6586X_LDO_LINEAR(_id, _pname, n_volt, min_uv, uv_step, vreg,	\
			    shift, nbits, ereg0, ebit0, ereg1, ebit1)	\
{									\
	TPS6586X_REGULATOR_LINEAR(_id, rw_linear, _pname, n_volt,	\
				  min_uv, uv_step, vreg, shift, nbits,	\
				  ereg0, ebit0, ereg1, ebit1, 0, 0)	\
}

#define TPS6586X_FIXED_LDO(_id, _pname, vdata, vreg, shift, nbits,	\
			  ereg0, ebit0, ereg1, ebit1)			\
{									\
	TPS6586X_REGULATOR(_id, ro, _pname, vdata, vreg, shift, nbits,	\
			   ereg0, ebit0, ereg1, ebit1, 0, 0)		\
}

#define TPS6586X_DVM(_id, _pname, n_volt, min_uv, uv_step, vreg, shift,	\
		     nbits, ereg0, ebit0, ereg1, ebit1, goreg, gobit)	\
{									\
	TPS6586X_REGULATOR_LINEAR(_id, rw_linear, _pname, n_volt,	\
				  min_uv, uv_step, vreg, shift, nbits,	\
				  ereg0, ebit0, ereg1, ebit1, goreg,	\
				  gobit)				\
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
	TPS6586X_LDO(LDO_0, "vinldo01", tps6586x_ldo0, SUPPLYV1, 5, 3, ENC, 0,
					END, 0),
	TPS6586X_LDO(LDO_3, "vinldo23", tps6586x_ldo, SUPPLYV4, 0, 3, ENC, 2,
					END, 2),
	TPS6586X_LDO(LDO_5, "REG-SYS", tps6586x_ldo, SUPPLYV6, 0, 3, ENE, 6,
					ENE, 6),
	TPS6586X_LDO(LDO_6, "vinldo678", tps6586x_ldo, SUPPLYV3, 0, 3, ENC, 4,
					END, 4),
	TPS6586X_LDO(LDO_7, "vinldo678", tps6586x_ldo, SUPPLYV3, 3, 3, ENC, 5,
					END, 5),
	TPS6586X_LDO(LDO_8, "vinldo678", tps6586x_ldo, SUPPLYV2, 5, 3, ENC, 6,
					END, 6),
	TPS6586X_LDO(LDO_9, "vinldo9", tps6586x_ldo, SUPPLYV6, 3, 3, ENE, 7,
					ENE, 7),
	TPS6586X_LDO(LDO_RTC, "REG-SYS", tps6586x_ldo, SUPPLYV4, 3, 3, V4, 7,
					V4, 7),
	TPS6586X_LDO_LINEAR(LDO_1, "vinldo01", 32, 725000, 25000, SUPPLYV1,
			    0, 5, ENC, 1, END, 1),
	TPS6586X_LDO_LINEAR(SM_2, "vin-sm2", 32, 3000000, 50000, SUPPLYV2,
			    0, 5, ENC, 7, END, 7),
	TPS6586X_DVM(LDO_2, "vinldo23", 32, 725000, 25000, LDO2BV1, 0, 5,
		     ENA, 3, ENB, 3, TPS6586X_VCC2, BIT(6)),
	TPS6586X_DVM(LDO_4, "vinldo4", 32, 1700000, 25000, LDO4V1, 0, 5,
		     ENC, 3, END, 3, TPS6586X_VCC1, BIT(6)),
	TPS6586X_DVM(SM_0, "vin-sm0", 32, 725000, 25000, SM0V1, 0, 5,
		     ENA, 1, ENB, 1, TPS6586X_VCC1, BIT(2)),
	TPS6586X_DVM(SM_1, "vin-sm1", 32, 725000, 25000, SM1V1, 0, 5,
		     ENA, 0, ENB, 0, TPS6586X_VCC1, BIT(0)),
};

static struct tps6586x_regulator tps658623_regulator[] = {
	TPS6586X_LDO_LINEAR(SM_2, "vin-sm2", 32, 1700000, 25000, SUPPLYV2,
			    0, 5, ENC, 7, END, 7),
};

static struct tps6586x_regulator tps658640_regulator[] = {
	TPS6586X_LDO(LDO_3, "vinldo23", tps6586x_ldo0, SUPPLYV4, 0, 3,
					ENC, 2, END, 2),
	TPS6586X_LDO(LDO_5, "REG-SYS", tps6586x_ldo0, SUPPLYV6, 0, 3,
					ENE, 6, ENE, 6),
	TPS6586X_LDO(LDO_6, "vinldo678", tps6586x_ldo0, SUPPLYV3, 0, 3,
					ENC, 4, END, 4),
	TPS6586X_LDO(LDO_7, "vinldo678", tps6586x_ldo0, SUPPLYV3, 3, 3,
					ENC, 5, END, 5),
	TPS6586X_LDO(LDO_8, "vinldo678", tps6586x_ldo0, SUPPLYV2, 5, 3,
					ENC, 6, END, 6),
	TPS6586X_LDO(LDO_9, "vinldo9", tps6586x_ldo0, SUPPLYV6, 3, 3,
					ENE, 7, ENE, 7),
	TPS6586X_LDO_LINEAR(SM_2, "vin-sm2", 32, 2150000, 50000, SUPPLYV2,
			    0, 5, ENC, 7, END, 7),

	TPS6586X_FIXED_LDO(LDO_RTC, "REG-SYS", tps658640_rtc, SUPPLYV4, 3, 2,
					V4, 7, V4, 7),
};

static struct tps6586x_regulator tps658643_regulator[] = {
	TPS6586X_LDO_LINEAR(SM_2, "vin-sm2", 32, 1025000, 25000, SUPPLYV2,
			    0, 5, ENC, 7, END, 7),
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

static int tps6586x_regulator_set_slew_rate(struct platform_device *pdev,
			int id, struct regulator_init_data *p)
{
	struct device *parent = pdev->dev.parent;
	struct tps6586x_settings *setting = p->driver_data;
	uint8_t reg;

	if (setting == NULL)
		return 0;

	if (!(setting->slew_rate & TPS6586X_SLEW_RATE_SET))
		return 0;

	/* only SM0 and SM1 can have the slew rate settings */
	switch (id) {
	case TPS6586X_ID_SM_0:
		reg = TPS6586X_SM0SL;
		break;
	case TPS6586X_ID_SM_1:
		reg = TPS6586X_SM1SL;
		break;
	default:
		dev_err(&pdev->dev, "Only SM0/SM1 can set slew rate\n");
		return -EINVAL;
	}

	return tps6586x_write(parent, reg,
			setting->slew_rate & TPS6586X_SLEW_RATE_MASK);
}

static struct tps6586x_regulator *find_regulator_info(int id, int version)
{
	struct tps6586x_regulator *ri;
	struct tps6586x_regulator *table = NULL;
	int num;
	int i;

	switch (version) {
	case TPS658623:
	case TPS658624:
		table = tps658623_regulator;
		num = ARRAY_SIZE(tps658623_regulator);
		break;
	case TPS658640:
	case TPS658640v2:
		table = tps658640_regulator;
		num = ARRAY_SIZE(tps658640_regulator);
		break;
	case TPS658643:
		table = tps658643_regulator;
		num = ARRAY_SIZE(tps658643_regulator);
		break;
	}

	/* Search version specific table first */
	if (table) {
		for (i = 0; i < num; i++) {
			ri = &table[i];
			if (ri->desc.id == id)
				return ri;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tps6586x_regulator); i++) {
		ri = &tps6586x_regulator[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

#ifdef CONFIG_OF
static struct of_regulator_match tps6586x_matches[] = {
	{ .name = "sys",     .driver_data = (void *)TPS6586X_ID_SYS     },
	{ .name = "sm0",     .driver_data = (void *)TPS6586X_ID_SM_0    },
	{ .name = "sm1",     .driver_data = (void *)TPS6586X_ID_SM_1    },
	{ .name = "sm2",     .driver_data = (void *)TPS6586X_ID_SM_2    },
	{ .name = "ldo0",    .driver_data = (void *)TPS6586X_ID_LDO_0   },
	{ .name = "ldo1",    .driver_data = (void *)TPS6586X_ID_LDO_1   },
	{ .name = "ldo2",    .driver_data = (void *)TPS6586X_ID_LDO_2   },
	{ .name = "ldo3",    .driver_data = (void *)TPS6586X_ID_LDO_3   },
	{ .name = "ldo4",    .driver_data = (void *)TPS6586X_ID_LDO_4   },
	{ .name = "ldo5",    .driver_data = (void *)TPS6586X_ID_LDO_5   },
	{ .name = "ldo6",    .driver_data = (void *)TPS6586X_ID_LDO_6   },
	{ .name = "ldo7",    .driver_data = (void *)TPS6586X_ID_LDO_7   },
	{ .name = "ldo8",    .driver_data = (void *)TPS6586X_ID_LDO_8   },
	{ .name = "ldo9",    .driver_data = (void *)TPS6586X_ID_LDO_9   },
	{ .name = "ldo_rtc", .driver_data = (void *)TPS6586X_ID_LDO_RTC },
};

static struct tps6586x_platform_data *tps6586x_parse_regulator_dt(
		struct platform_device *pdev,
		struct of_regulator_match **tps6586x_reg_matches)
{
	const unsigned int num = ARRAY_SIZE(tps6586x_matches);
	struct device_node *np = pdev->dev.parent->of_node;
	struct device_node *regs;
	const char *sys_rail = NULL;
	unsigned int i;
	struct tps6586x_platform_data *pdata;
	int err;

	regs = of_get_child_by_name(np, "regulators");
	if (!regs) {
		dev_err(&pdev->dev, "regulator node not found\n");
		return NULL;
	}

	err = of_regulator_match(&pdev->dev, regs, tps6586x_matches, num);
	of_node_put(regs);
	if (err < 0) {
		dev_err(&pdev->dev, "Regulator match failed, e %d\n", err);
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	for (i = 0; i < num; i++) {
		uintptr_t id;
		if (!tps6586x_matches[i].init_data)
			continue;

		pdata->reg_init_data[i] = tps6586x_matches[i].init_data;
		id = (uintptr_t)tps6586x_matches[i].driver_data;
		if (id == TPS6586X_ID_SYS)
			sys_rail = pdata->reg_init_data[i]->constraints.name;

		if ((id == TPS6586X_ID_LDO_5) || (id == TPS6586X_ID_LDO_RTC))
			pdata->reg_init_data[i]->supply_regulator = sys_rail;
	}
	*tps6586x_reg_matches = tps6586x_matches;
	return pdata;
}
#else
static struct tps6586x_platform_data *tps6586x_parse_regulator_dt(
		struct platform_device *pdev,
		struct of_regulator_match **tps6586x_reg_matches)
{
	*tps6586x_reg_matches = NULL;
	return NULL;
}
#endif

static int tps6586x_regulator_probe(struct platform_device *pdev)
{
	struct tps6586x_regulator *ri = NULL;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regulator_init_data *reg_data;
	struct tps6586x_platform_data *pdata;
	struct of_regulator_match *tps6586x_reg_matches = NULL;
	int version;
	int id;
	int err;

	dev_dbg(&pdev->dev, "Probing regulator\n");

	pdata = dev_get_platdata(pdev->dev.parent);
	if ((!pdata) && (pdev->dev.parent->of_node))
		pdata = tps6586x_parse_regulator_dt(pdev,
					&tps6586x_reg_matches);

	if (!pdata) {
		dev_err(&pdev->dev, "Platform data not available, exiting\n");
		return -ENODEV;
	}

	version = tps6586x_get_version(pdev->dev.parent);

	for (id = 0; id < TPS6586X_ID_MAX_REGULATOR; ++id) {
		reg_data = pdata->reg_init_data[id];

		ri = find_regulator_info(id, version);

		if (!ri) {
			dev_err(&pdev->dev, "invalid regulator ID specified\n");
			return -EINVAL;
		}

		err = tps6586x_regulator_preinit(pdev->dev.parent, ri);
		if (err) {
			dev_err(&pdev->dev,
				"regulator %d preinit failed, e %d\n", id, err);
			return err;
		}

		config.dev = pdev->dev.parent;
		config.init_data = reg_data;
		config.driver_data = ri;

		if (tps6586x_reg_matches)
			config.of_node = tps6586x_reg_matches[id].of_node;

		rdev = devm_regulator_register(&pdev->dev, &ri->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register regulator %s\n",
					ri->desc.name);
			return PTR_ERR(rdev);
		}

		if (reg_data) {
			err = tps6586x_regulator_set_slew_rate(pdev, id,
					reg_data);
			if (err < 0) {
				dev_err(&pdev->dev,
					"Slew rate config failed, e %d\n", err);
				return err;
			}
		}
	}

	platform_set_drvdata(pdev, rdev);
	return 0;
}

static struct platform_driver tps6586x_regulator_driver = {
	.driver	= {
		.name	= "tps6586x-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe		= tps6586x_regulator_probe,
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

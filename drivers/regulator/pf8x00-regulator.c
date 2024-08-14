// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 NXP
 * Copyright (C) 2019 Boundary Devices
 * Copyright (C) 2020 Amarula Solutions(India)
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

/* registers */
#define PF8X00_DEVICEID			0x00
#define PF8X00_REVID			0x01
#define PF8X00_EMREV			0x02
#define PF8X00_PROGID			0x03
#define PF8X00_IMS_INT			0x04
#define PF8X00_IMS_THERM		0x07
#define PF8X00_SW_MODE_INT		0x0a
#define PF8X00_SW_MODE_MASK		0x0b
#define PF8X00_IMS_SW_ILIM		0x12
#define PF8X00_IMS_LDO_ILIM		0x15
#define PF8X00_IMS_SW_UV		0x18
#define PF8X00_IMS_SW_OV		0x1b
#define PF8X00_IMS_LDO_UV		0x1e
#define PF8X00_IMS_LDO_OV		0x21
#define PF8X00_IMS_PWRON		0x24
#define PF8X00_SYS_INT			0x27
#define PF8X00_HARD_FAULT		0x29
#define PF8X00_FSOB_FLAGS		0x2a
#define PF8X00_FSOB_SELECT		0x2b
#define PF8X00_ABIST_OV1		0x2c
#define PF8X00_ABIST_OV2		0x2d
#define PF8X00_ABIST_UV1		0x2e
#define PF8X00_ABIST_UV2		0x2f
#define PF8X00_TEST_FLAGS		0x30
#define PF8X00_ABIST_RUN		0x31
#define PF8X00_RANDOM_GEN		0x33
#define PF8X00_RANDOM_CHK		0x34
#define PF8X00_VMONEN1			0x35
#define PF8X00_VMONEN2			0x36
#define PF8X00_CTRL1			0x37
#define PF8X00_CTRL2			0x38
#define PF8X00_CTRL3			0x39
#define PF8X00_PWRUP_CTRL		0x3a
#define PF8X00_RESETBMCU		0x3c
#define PF8X00_PGOOD			0x3d
#define PF8X00_PWRDN_DLY1		0x3e
#define PF8X00_PWRDN_DLY2		0x3f
#define PF8X00_FREQ_CTRL		0x40
#define PF8X00_COINCELL_CTRL		0x41
#define PF8X00_PWRON			0x42
#define PF8X00_WD_CONFIG		0x43
#define PF8X00_WD_CLEAR			0x44
#define PF8X00_WD_EXPIRE		0x45
#define PF8X00_WD_COUNTER		0x46
#define PF8X00_FAULT_COUNTER		0x47
#define PF8X00_FSAFE_COUNTER		0x48
#define PF8X00_FAULT_TIMER		0x49
#define PF8X00_AMUX			0x4a
#define PF8X00_SW1_CONFIG1		0x4d
#define PF8X00_LDO1_CONFIG1		0x85
#define PF8X00_VSNVS_CONFIG1		0x9d
#define PF8X00_PAGE_SELECT		0x9f

/* regulators */
enum pf8x00_regulators {
	PF8X00_LDO1,
	PF8X00_LDO2,
	PF8X00_LDO3,
	PF8X00_LDO4,
	PF8X00_BUCK1,
	PF8X00_BUCK2,
	PF8X00_BUCK3,
	PF8X00_BUCK4,
	PF8X00_BUCK5,
	PF8X00_BUCK6,
	PF8X00_BUCK7,
	PF8X00_VSNVS,

	PF8X00_MAX_REGULATORS,
};

enum pf8x00_buck_states {
	SW_CONFIG1,
	SW_CONFIG2,
	SW_PWRUP,
	SW_MODE1,
	SW_RUN_VOLT,
	SW_STBY_VOLT,
};
#define PF8X00_SW_BASE(i)		(8 * (i - PF8X00_BUCK1) + PF8X00_SW1_CONFIG1)

enum pf8x00_ldo_states {
	LDO_CONFIG1,
	LDO_CONFIG2,
	LDO_PWRUP,
	LDO_RUN_VOLT,
	LDO_STBY_VOLT,
};
#define PF8X00_LDO_BASE(i)		(6 * (i - PF8X00_LDO1) + PF8X00_LDO1_CONFIG1)

enum swxilim_bits {
	SWXILIM_2100_MA,
	SWXILIM_2600_MA,
	SWXILIM_3000_MA,
	SWXILIM_4500_MA,
};
#define PF8X00_SWXILIM_SHIFT		3
#define PF8X00_SWXILIM_MASK		GENMASK(4, 3)
#define PF8X00_SWXPHASE_MASK		GENMASK(2, 0)
#define PF8X00_SWXPHASE_SHIFT		7

enum pf8x00_devid {
	PF8100			= 0x0,
	PF8121A			= BIT(1),
	PF8200			= BIT(3),
};
#define PF8X00_FAM			BIT(6)
#define PF8X00_DEVICE_FAM_MASK		GENMASK(7, 4)
#define PF8X00_DEVICE_ID_MASK		GENMASK(3, 0)

struct pf8x00_regulator_data {
	struct regulator_desc desc;
	unsigned int suspend_enable_reg;
	unsigned int suspend_enable_mask;
	unsigned int suspend_voltage_reg;
	unsigned int suspend_voltage_cache;
};

struct pf8x00_chip {
	struct regmap *regmap;
	struct device *dev;
};

static const struct regmap_config pf8x00_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PF8X00_PAGE_SELECT,
	.cache_type = REGCACHE_MAPLE,
};

/* VLDOx output: 1.5V to 5.0V */
static const int pf8x00_ldo_voltages[] = {
	1500000, 1600000, 1800000, 1850000, 2150000, 2500000, 2800000, 3000000,
	3100000, 3150000, 3200000, 3300000, 3350000, 1650000, 1700000, 5000000,
};

/* Output: 2.1A to 4.5A */
static const unsigned int pf8x00_sw_current_table[] = {
	2100000, 2600000, 3000000, 4500000,
};

/* Output: 0.4V to 1.8V */
#define PF8XOO_SW1_6_VOLTAGE_NUM 0xB2
static const struct linear_range pf8x00_sw1_to_6_voltages[] = {
	REGULATOR_LINEAR_RANGE(400000, 0x00, 0xB0, 6250),
	REGULATOR_LINEAR_RANGE(1800000, 0xB1, 0xB1, 0),
};

/* Output: 1.0V to 4.1V */
static const int pf8x00_sw7_voltages[] = {
	1000000, 1100000, 1200000, 1250000, 1300000, 1350000, 1500000, 1600000,
	1800000, 1850000, 2000000, 2100000, 2150000, 2250000, 2300000, 2400000,
	2500000, 2800000, 3150000, 3200000, 3250000, 3300000, 3350000, 3400000,
	3500000, 3800000, 4000000, 4100000, 4100000, 4100000, 4100000, 4100000,
};

/* Output: 1.8V, 3.0V, or 3.3V */
static const int pf8x00_vsnvs_voltages[] = {
	0, 1800000, 3000000, 3300000,
};

static void swxilim_select(struct pf8x00_chip *chip, int id, int ilim)
{
	u8 ilim_sel;
	u8 reg = PF8X00_SW_BASE(id) + SW_CONFIG2;

	switch (ilim) {
	case 2100:
		ilim_sel = SWXILIM_2100_MA;
		break;
	case 2600:
		ilim_sel = SWXILIM_2600_MA;
		break;
	case 3000:
		ilim_sel = SWXILIM_3000_MA;
		break;
	case 4500:
		ilim_sel = SWXILIM_4500_MA;
		break;
	default:
		ilim_sel = SWXILIM_2100_MA;
		break;
	}

	regmap_update_bits(chip->regmap, reg,
					PF8X00_SWXILIM_MASK,
					ilim_sel << PF8X00_SWXILIM_SHIFT);
}

static void handle_ilim_property(struct device_node *np,
			      const struct regulator_desc *desc,
			      struct regulator_config *config)
{
	struct pf8x00_chip *chip = config->driver_data;
	int ret;
	int val;

	if ((desc->id >= PF8X00_BUCK1) && (desc->id <= PF8X00_BUCK7)) {
		ret = of_property_read_u32(np, "nxp,ilim-ma", &val);
		if (ret) {
			dev_dbg(chip->dev, "unspecified ilim for BUCK%d, use value stored in OTP\n",
				desc->id - PF8X00_LDO4);
			return;
		}

		dev_warn(chip->dev, "nxp,ilim-ma is deprecated, please use regulator-max-microamp\n");
		swxilim_select(chip, desc->id, val);

	} else
		dev_warn(chip->dev, "nxp,ilim-ma used with incorrect regulator (%d)\n", desc->id);
}

static void handle_shift_property(struct device_node *np,
			      const struct regulator_desc *desc,
			      struct regulator_config *config)
{
	unsigned char id = desc->id - PF8X00_LDO4;
	unsigned char reg = PF8X00_SW_BASE(id) + SW_CONFIG2;
	struct pf8x00_chip *chip = config->driver_data;

	int phase;
	int val;
	int ret;
	if ((desc->id >= PF8X00_BUCK1) && (desc->id <= PF8X00_BUCK7)) {
		ret = of_property_read_u32(np, "nxp,phase-shift", &val);
		if (ret) {
			dev_dbg(chip->dev,
				"unspecified phase-shift for BUCK%d, using OTP configuration\n",
				id);
			return;
		}

		if (val < 0 || val > 315 || val % 45 != 0) {
			dev_warn(config->dev,
				"invalid phase_shift %d for BUCK%d, using OTP configuration\n",
				val, id);
			return;
		}

		phase = val / 45;

		if (phase >= 1)
			phase -= 1;
		else
			phase = PF8X00_SWXPHASE_SHIFT;

		regmap_update_bits(chip->regmap, reg,
				PF8X00_SWXPHASE_MASK,
				phase);
	} else
		dev_warn(chip->dev, "nxp,phase-shift used with incorrect regulator (%d)\n", id);

}

static int pf8x00_of_parse_cb(struct device_node *np,
			      const struct regulator_desc *desc,
			      struct regulator_config *config)
{

	handle_ilim_property(np, desc, config);
	handle_shift_property(np, desc, config);

	return 0;
}

static int pf8x00_suspend_enable(struct regulator_dev *rdev)
{
	struct pf8x00_regulator_data *regl = rdev_get_drvdata(rdev);
	struct regmap *rmap = rdev_get_regmap(rdev);

	return regmap_update_bits(rmap, regl->suspend_enable_reg,
				  regl->suspend_enable_mask,
				  regl->suspend_enable_mask);
}

static int pf8x00_suspend_disable(struct regulator_dev *rdev)
{
	struct pf8x00_regulator_data *regl = rdev_get_drvdata(rdev);
	struct regmap *rmap = rdev_get_regmap(rdev);

	return regmap_update_bits(rmap, regl->suspend_enable_reg,
				  regl->suspend_enable_mask, 0);
}

static int pf8x00_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct pf8x00_regulator_data *regl = rdev_get_drvdata(rdev);
	int ret;

	if (regl->suspend_voltage_cache == uV)
		return 0;

	ret = regulator_map_voltage_iterate(rdev, uV, uV);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev), "failed to map %i uV\n", uV);
		return ret;
	}

	dev_dbg(rdev_get_dev(rdev), "uV: %i, reg: 0x%x, msk: 0x%x, val: 0x%x\n",
		uV, regl->suspend_voltage_reg, regl->desc.vsel_mask, ret);
	ret = regmap_update_bits(rdev->regmap, regl->suspend_voltage_reg,
				 regl->desc.vsel_mask, ret);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev), "failed to set %i uV\n", uV);
		return ret;
	}

	regl->suspend_voltage_cache = uV;

	return 0;
}

static const struct regulator_ops pf8x00_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_suspend_enable = pf8x00_suspend_enable,
	.set_suspend_disable = pf8x00_suspend_disable,
	.set_suspend_voltage = pf8x00_set_suspend_voltage,
};


static const struct regulator_ops pf8x00_buck1_6_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
	.set_suspend_enable = pf8x00_suspend_enable,
	.set_suspend_disable = pf8x00_suspend_disable,
	.set_suspend_voltage = pf8x00_set_suspend_voltage,
};

static const struct regulator_ops pf8x00_buck7_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
	.set_suspend_enable = pf8x00_suspend_enable,
	.set_suspend_disable = pf8x00_suspend_disable,
};

static const struct regulator_ops pf8x00_vsnvs_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

#define PF8X00LDO(_id, _name, base, voltages)			\
	[PF8X00_LDO ## _id] = {					\
		.desc = {					\
			.name = _name,				\
			.of_match = _name,			\
			.regulators_node = "regulators",	\
			.n_voltages = ARRAY_SIZE(voltages),	\
			.ops = &pf8x00_ldo_ops,			\
			.type = REGULATOR_VOLTAGE,		\
			.id = PF8X00_LDO ## _id,		\
			.owner = THIS_MODULE,			\
			.volt_table = voltages,			\
			.vsel_reg = (base) + LDO_RUN_VOLT,	\
			.vsel_mask = 0xff,			\
			.enable_reg = (base) + LDO_CONFIG2,	\
			.enable_val = 0x2,			\
			.disable_val = 0x0,			\
			.enable_mask = 2,			\
		},						\
		.suspend_enable_reg = (base) + LDO_CONFIG2,	\
		.suspend_enable_mask = 1,			\
		.suspend_voltage_reg = (base) + LDO_STBY_VOLT,	\
	}

#define PF8X00BUCK(_id, _name, base, voltages)			\
	[PF8X00_BUCK ## _id] = {				\
		.desc = {					\
			.name = _name,				\
			.of_match = _name,			\
			.regulators_node = "regulators",	\
			.of_parse_cb = pf8x00_of_parse_cb,	\
			.n_voltages = PF8XOO_SW1_6_VOLTAGE_NUM,	\
			.ops = &pf8x00_buck1_6_ops,		\
			.type = REGULATOR_VOLTAGE,		\
			.id = PF8X00_BUCK ## _id,		\
			.owner = THIS_MODULE,			\
			.ramp_delay = 19000,			\
			.linear_ranges = pf8x00_sw1_to_6_voltages, \
			.n_linear_ranges = \
				ARRAY_SIZE(pf8x00_sw1_to_6_voltages), \
			.vsel_reg = (base) + SW_RUN_VOLT,	\
			.vsel_mask = 0xff,			\
			.curr_table = pf8x00_sw_current_table, \
			.n_current_limits = \
				ARRAY_SIZE(pf8x00_sw_current_table), \
			.csel_reg = (base) + SW_CONFIG2,	\
			.csel_mask = PF8X00_SWXILIM_MASK,	\
			.enable_reg = (base) + SW_MODE1,	\
			.enable_val = 0x3,			\
			.disable_val = 0x0,			\
			.enable_mask = 0x3,			\
			.enable_time = 500,			\
		},						\
		.suspend_enable_reg = (base) + SW_MODE1,	\
		.suspend_enable_mask = 0xc,			\
		.suspend_voltage_reg = (base) + SW_STBY_VOLT,	\
	}

#define PF8X00BUCK7(_name, base, voltages)			\
	[PF8X00_BUCK7] = {				\
		.desc = {					\
			.name = _name,				\
			.of_match = _name,			\
			.regulators_node = "regulators",	\
			.of_parse_cb = pf8x00_of_parse_cb,	\
			.n_voltages = ARRAY_SIZE(voltages),	\
			.ops = &pf8x00_buck7_ops,		\
			.type = REGULATOR_VOLTAGE,		\
			.id = PF8X00_BUCK7,		\
			.owner = THIS_MODULE,			\
			.ramp_delay = 19000,			\
			.volt_table = voltages,			\
			.vsel_reg = (base) + SW_RUN_VOLT,	\
			.vsel_mask = 0xff,			\
			.curr_table = pf8x00_sw_current_table, \
			.n_current_limits = \
				ARRAY_SIZE(pf8x00_sw_current_table), \
			.csel_reg = (base) + SW_CONFIG2,	\
			.csel_mask = PF8X00_SWXILIM_MASK,	\
			.enable_reg = (base) + SW_MODE1,	\
			.enable_val = 0x3,			\
			.disable_val = 0x0,			\
			.enable_mask = 0x3,			\
			.enable_time = 500,			\
		},						\
	}


#define PF8X00VSNVS(_name, base, voltages)			\
	[PF8X00_VSNVS] = {					\
		.desc = {					\
			.name = _name,				\
			.of_match = _name,			\
			.regulators_node = "regulators",	\
			.n_voltages = ARRAY_SIZE(voltages),	\
			.ops = &pf8x00_vsnvs_ops,		\
			.type = REGULATOR_VOLTAGE,		\
			.id = PF8X00_VSNVS,			\
			.owner = THIS_MODULE,			\
			.volt_table = voltages,			\
			.vsel_reg = (base),			\
			.vsel_mask = 0x3,			\
		},						\
	}

static struct pf8x00_regulator_data pf8x00_regs_data[PF8X00_MAX_REGULATORS] = {
	PF8X00LDO(1, "ldo1", PF8X00_LDO_BASE(PF8X00_LDO1), pf8x00_ldo_voltages),
	PF8X00LDO(2, "ldo2", PF8X00_LDO_BASE(PF8X00_LDO2), pf8x00_ldo_voltages),
	PF8X00LDO(3, "ldo3", PF8X00_LDO_BASE(PF8X00_LDO3), pf8x00_ldo_voltages),
	PF8X00LDO(4, "ldo4", PF8X00_LDO_BASE(PF8X00_LDO4), pf8x00_ldo_voltages),
	PF8X00BUCK(1, "buck1", PF8X00_SW_BASE(PF8X00_BUCK1), pf8x00_sw1_to_6_voltages),
	PF8X00BUCK(2, "buck2", PF8X00_SW_BASE(PF8X00_BUCK2), pf8x00_sw1_to_6_voltages),
	PF8X00BUCK(3, "buck3", PF8X00_SW_BASE(PF8X00_BUCK3), pf8x00_sw1_to_6_voltages),
	PF8X00BUCK(4, "buck4", PF8X00_SW_BASE(PF8X00_BUCK4), pf8x00_sw1_to_6_voltages),
	PF8X00BUCK(5, "buck5", PF8X00_SW_BASE(PF8X00_BUCK5), pf8x00_sw1_to_6_voltages),
	PF8X00BUCK(6, "buck6", PF8X00_SW_BASE(PF8X00_BUCK6), pf8x00_sw1_to_6_voltages),
	PF8X00BUCK7("buck7", PF8X00_SW_BASE(PF8X00_BUCK7), pf8x00_sw7_voltages),
	PF8X00VSNVS("vsnvs", PF8X00_VSNVS_CONFIG1, pf8x00_vsnvs_voltages),
};

static int pf8x00_identify(struct pf8x00_chip *chip)
{
	unsigned int value;
	u8 dev_fam, dev_id;
	const char *name = NULL;
	int ret;

	ret = regmap_read(chip->regmap, PF8X00_DEVICEID, &value);
	if (ret) {
		dev_err(chip->dev, "failed to read chip family\n");
		return ret;
	}

	dev_fam = value & PF8X00_DEVICE_FAM_MASK;
	switch (dev_fam) {
	case PF8X00_FAM:
		break;
	default:
		dev_err(chip->dev,
			"Chip 0x%x is not from PF8X00 family\n", dev_fam);
		return ret;
	}

	dev_id = value & PF8X00_DEVICE_ID_MASK;
	switch (dev_id) {
	case PF8100:
		name = "PF8100";
		break;
	case PF8121A:
		name = "PF8121A";
		break;
	case PF8200:
		name = "PF8200";
		break;
	default:
		dev_err(chip->dev, "Unknown pf8x00 device id 0x%x\n", dev_id);
		return -ENODEV;
	}

	dev_info(chip->dev, "%s PMIC found.\n", name);

	return 0;
}

static int pf8x00_i2c_probe(struct i2c_client *client)
{
	struct regulator_config config = { NULL, };
	struct pf8x00_chip *chip;
	int id;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->dev = &client->dev;

	chip->regmap = devm_regmap_init_i2c(client, &pf8x00_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev,
			"regmap allocation failed with err %d\n", ret);
		return ret;
	}

	ret = pf8x00_identify(chip);
	if (ret)
		return ret;

	for (id = 0; id < ARRAY_SIZE(pf8x00_regs_data); id++) {
		struct pf8x00_regulator_data *data = &pf8x00_regs_data[id];
		struct regulator_dev *rdev;

		config.dev = chip->dev;
		config.driver_data = data;
		config.regmap = chip->regmap;

		rdev = devm_regulator_register(&client->dev, &data->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&client->dev,
				"failed to register %s regulator\n", data->desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct of_device_id pf8x00_dt_ids[] = {
	{ .compatible = "nxp,pf8100",},
	{ .compatible = "nxp,pf8121a",},
	{ .compatible = "nxp,pf8200",},
	{ }
};
MODULE_DEVICE_TABLE(of, pf8x00_dt_ids);

static const struct i2c_device_id pf8x00_i2c_id[] = {
	{ "pf8100" },
	{ "pf8121a" },
	{ "pf8200" },
	{}
};
MODULE_DEVICE_TABLE(i2c, pf8x00_i2c_id);

static struct i2c_driver pf8x00_regulator_driver = {
	.id_table = pf8x00_i2c_id,
	.driver = {
		.name = "pf8x00",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = pf8x00_dt_ids,
	},
	.probe = pf8x00_i2c_probe,
};
module_i2c_driver(pf8x00_regulator_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_AUTHOR("Troy Kisky <troy.kisky@boundarydevices.com>");
MODULE_DESCRIPTION("Regulator Driver for NXP's PF8100/PF8121A/PF8200 PMIC");
MODULE_LICENSE("GPL v2");

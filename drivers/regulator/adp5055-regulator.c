// SPDX-License-Identifier: GPL-2.0
//
// Regulator driver for Analog Devices ADP5055
//
// Copyright (C) 2025 Analog Devices, Inc.

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

// ADP5055 Register Map.

#define ADP5055_CTRL123         0xD1
#define ADP5055_CTRL_MODE1      0xD3
#define ADP5055_CTRL_MODE2      0xD4
#define ADP5055_DLY0            0xD5
#define ADP5055_DLY1            0xD6
#define ADP5055_DLY2            0xD7
#define ADP5055_VID0            0xD8
#define ADP5055_VID1            0xD9
#define ADP5055_VID2            0xDA
#define ADP5055_DVS_LIM0        0xDC
#define ADP5055_DVS_LIM1        0xDD
#define ADP5055_DVS_LIM2        0xDE
#define ADP5055_FT_CFG          0xDF
#define ADP5055_PG_CFG          0xE0

// ADP5055 Field Masks.

#define	ADP5055_MASK_EN_MODE		BIT(0)
#define	ADP5055_MASK_OCP_BLANKING	BIT(7)
#define	ADP5055_MASK_PSM		BIT(4)
#define	ADP5055_MASK_DIS2		BIT(2)
#define	ADP5055_MASK_DIS1		BIT(1)
#define	ADP5055_MASK_DIS0		BIT(0)
#define	ADP5055_MASK_DIS_DLY		GENMASK(6, 4)
#define	ADP5055_MASK_EN_DLY		GENMASK(2, 0)
#define	ADP5055_MASK_DVS_LIM_UPPER	GENMASK(7, 4)
#define	ADP5055_MASK_DVS_LIM_LOWER	GENMASK(3, 0)
#define	ADP5055_MASK_FAST_TRANSIENT2	GENMASK(5, 4)
#define	ADP5055_MASK_FAST_TRANSIENT1	GENMASK(3, 2)
#define	ADP5055_MASK_FAST_TRANSIENT0	GENMASK(1, 0)
#define	ADP5055_MASK_DLY_PWRGD		BIT(4)
#define	ADP5055_MASK_PWRGD2		BIT(2)
#define	ADP5055_MASK_PWRGD1		BIT(1)
#define	ADP5055_MASK_PWRGD0		BIT(0)

#define	ADP5055_MIN_VOUT		408000
#define ADP5055_NUM_CH			3

struct adp5055 {
	struct device *dev;
	struct regmap *regmap;
	u32 tset;
	struct gpio_desc *en_gpiod[ADP5055_NUM_CH];
	bool en_mode_software;
	int dvs_limit_upper[ADP5055_NUM_CH];
	int dvs_limit_lower[ADP5055_NUM_CH];
	u32 fast_transient[ADP5055_NUM_CH];
	bool mask_power_good[ADP5055_NUM_CH];
};

static const unsigned int adp5055_tset_vals[] = {
	2600,
	20800,
};

static const unsigned int adp5055_enable_delay_vals_2_6[] = {
	0,
	2600,
	5200,
	7800,
	10400,
	13000,
	15600,
	18200,
};

static const unsigned int adp5055_enable_delay_vals_20_8[] = {
	0,
	20800,
	41600,
	62400,
	83200,
	104000,
	124800,
	145600,
};

static const char * const adp5055_fast_transient_vals[] = {
	"none",
	"3G_1.5%",
	"5G_1.5%",
	"5G_2.5%",
};

static int adp5055_get_prop_index(const u32 *table, size_t table_size,
				  u32 value)
{
	int i;

	for (i = 0; i < table_size; i++)
		if (table[i] == value)
			return i;

	return -EINVAL;
}

static const struct regmap_range adp5055_reg_ranges[] = {
	regmap_reg_range(0xD1, 0xE0),
};

static const struct regmap_access_table adp5055_access_ranges_table = {
	.yes_ranges	= adp5055_reg_ranges,
	.n_yes_ranges	= ARRAY_SIZE(adp5055_reg_ranges),
};

static const struct regmap_config adp5055_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xE0,
	.wr_table = &adp5055_access_ranges_table,
	.rd_table = &adp5055_access_ranges_table,
};

static const struct linear_range adp5055_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(ADP5055_MIN_VOUT, 0, 255, 1500),
};

static int adp5055_parse_fw(struct device *dev, struct  adp5055 *adp5055)
{
	int i, ret;
	struct regmap *regmap = adp5055->regmap;
	int val;
	bool ocp_blanking;
	bool delay_power_good;

	ret = device_property_read_u32(dev, "adi,tset-us", &adp5055->tset);
	if (!ret) {
		ret = adp5055_get_prop_index(adp5055_tset_vals,
					ARRAY_SIZE(adp5055_tset_vals), adp5055->tset);
		if (ret < 0)
			return dev_err_probe(dev, ret,
				"Failed to initialize tset.");
		adp5055->tset = adp5055_tset_vals[ret];
	}

	ocp_blanking = device_property_read_bool(dev, "adi,ocp-blanking");

	delay_power_good = device_property_read_bool(dev,
				    "adi,delay-power-good");

	for (i = 0; i < ADP5055_NUM_CH; i++) {
		val = FIELD_PREP(ADP5055_MASK_DVS_LIM_UPPER,
				DIV_ROUND_CLOSEST_ULL(192000 - adp5055->dvs_limit_upper[i], 12000));
		val |= FIELD_PREP(ADP5055_MASK_DVS_LIM_LOWER,
				DIV_ROUND_CLOSEST_ULL(adp5055->dvs_limit_lower[i] + 190500, 12000));
		ret = regmap_write(regmap, ADP5055_DVS_LIM0 + i, val);
		if (ret)
			return ret;
	}

	val = FIELD_PREP(ADP5055_MASK_EN_MODE, adp5055->en_mode_software);
	ret = regmap_write(regmap, ADP5055_CTRL_MODE1, val);
	if (ret)
		return ret;

	val = FIELD_PREP(ADP5055_MASK_OCP_BLANKING, ocp_blanking);
	ret = regmap_update_bits(regmap, ADP5055_CTRL_MODE2,
				ADP5055_MASK_OCP_BLANKING, val);
	if (ret)
		return ret;

	val = FIELD_PREP(ADP5055_MASK_FAST_TRANSIENT2, adp5055->fast_transient[2]);
	val |= FIELD_PREP(ADP5055_MASK_FAST_TRANSIENT1, adp5055->fast_transient[1]);
	val |= FIELD_PREP(ADP5055_MASK_FAST_TRANSIENT0, adp5055->fast_transient[0]);
	ret = regmap_write(regmap, ADP5055_FT_CFG, val);
	if (ret)
		return ret;

	val = FIELD_PREP(ADP5055_MASK_DLY_PWRGD, delay_power_good);
	val |= FIELD_PREP(ADP5055_MASK_PWRGD2, adp5055->mask_power_good[2]);
	val |= FIELD_PREP(ADP5055_MASK_PWRGD1, adp5055->mask_power_good[1]);
	val |= FIELD_PREP(ADP5055_MASK_PWRGD0, adp5055->mask_power_good[0]);
	ret = regmap_write(regmap, ADP5055_PG_CFG, val);
	if (ret)
		return ret;

	return 0;
}

static int adp5055_of_parse_cb(struct device_node *np,
			      const struct regulator_desc *desc,
			      struct regulator_config *config)
{
	struct adp5055 *adp5055 = config->driver_data;
	int id, ret, pval, i;

	id = desc->id;

	if (of_property_read_bool(np, "enable-gpios")) {
		adp5055->en_gpiod[id] = devm_fwnode_gpiod_get(config->dev,
						of_fwnode_handle(np), "enable",
						GPIOD_OUT_LOW, "enable");
		if (IS_ERR(adp5055->en_gpiod[id]))
			return dev_err_probe(config->dev, PTR_ERR(adp5055->en_gpiod[id]),
					"Failed to get enable GPIO\n");

		config->ena_gpiod = adp5055->en_gpiod[id];
	} else {
		adp5055->en_mode_software = true;
	}

	ret = of_property_read_u32(np, "adi,dvs-limit-upper-microvolt", &pval);
	if (ret)
		adp5055->dvs_limit_upper[id] = 192000;
	else
		adp5055->dvs_limit_upper[id] = pval;

	if (adp5055->dvs_limit_upper[id] > 192000 || adp5055->dvs_limit_upper[id] < 12000)
		return dev_err_probe(config->dev, adp5055->dvs_limit_upper[id],
			"Out of range - dvs-limit-upper-microvolt value.");

	ret = of_property_read_u32(np, "adi,dvs-limit-lower-microvolt", &pval);
	if (ret)
		adp5055->dvs_limit_lower[id] = -190500;
	else
		adp5055->dvs_limit_lower[id] = pval;

	if (adp5055->dvs_limit_lower[id] > -10500 || adp5055->dvs_limit_lower[id] < -190500)
		return dev_err_probe(config->dev, adp5055->dvs_limit_lower[id],
			"Out of range - dvs-limit-lower-microvolt value.");

	for (i = 0; i < 4; i++) {
		ret = of_property_match_string(np, "adi,fast-transient",
					adp5055_fast_transient_vals[i]);
		if (!ret)
			break;
	}

	if (ret < 0)
		adp5055->fast_transient[id] = 3;
	else
		adp5055->fast_transient[id] = i;

	adp5055->mask_power_good[id] = of_property_read_bool(np, "adi,mask-power-good");

	return 0;
}

static int adp5055_set_mode(struct regulator_dev *rdev, u32 mode)
{
	struct adp5055 *adp5055 = rdev_get_drvdata(rdev);
	int id, ret;

	id = rdev_get_id(rdev);

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret = regmap_update_bits(adp5055->regmap, ADP5055_CTRL_MODE2,
					ADP5055_MASK_PSM << id, 0);
		break;
	case REGULATOR_MODE_IDLE:
		ret = regmap_update_bits(adp5055->regmap, ADP5055_CTRL_MODE2,
					ADP5055_MASK_PSM << id, ADP5055_MASK_PSM << id);
		break;
	default:
		return dev_err_probe(&rdev->dev, -EINVAL,
				"Unsupported mode: %d\n", mode);
	}

	return ret;
}

static unsigned int adp5055_get_mode(struct regulator_dev *rdev)
{
	struct adp5055 *adp5055 = rdev_get_drvdata(rdev);
	int id, ret, regval;

	id = rdev_get_id(rdev);

	ret = regmap_read(adp5055->regmap, ADP5055_CTRL_MODE2, &regval);
	if (ret)
		return ret;

	if (regval & (ADP5055_MASK_PSM << id))
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops adp5055_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = adp5055_set_mode,
	.get_mode = adp5055_get_mode,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};

#define ADP5055_REG_(_name, _id, _ch, _ops) \
	[_id] = { \
		.name = _name, \
		.of_match = of_match_ptr(_name), \
		.of_parse_cb = adp5055_of_parse_cb, \
		.id = _id, \
		.ops = _ops, \
		.linear_ranges = adp5055_voltage_ranges, \
		.n_linear_ranges = ARRAY_SIZE(adp5055_voltage_ranges), \
		.vsel_reg = ADP5055_VID##_ch, \
		.vsel_mask = GENMASK(7, 0), \
		.enable_reg = ADP5055_CTRL123, \
		.enable_mask = BIT(_ch), \
		.active_discharge_on = ADP5055_MASK_DIS##_id, \
		.active_discharge_off = 0, \
		.active_discharge_mask = ADP5055_MASK_DIS##_id, \
		.active_discharge_reg = ADP5055_CTRL_MODE2, \
		.ramp_reg = ADP5055_DLY##_ch, \
		.ramp_mask = ADP5055_MASK_EN_DLY, \
		.n_ramp_values = ARRAY_SIZE(adp5055_enable_delay_vals_2_6), \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}

#define ADP5055_REG(_name, _id, _ch) \
	ADP5055_REG_(_name, _id, _ch, &adp5055_ops)

static struct regulator_desc adp5055_regulators[] = {
	ADP5055_REG("buck0", 0, 0),
	ADP5055_REG("buck1", 1, 1),
	ADP5055_REG("buck2", 2, 2),
};

static int adp5055_probe(struct i2c_client *client)
{
	struct regulator_init_data *init_data;
	struct device *dev = &client->dev;
	struct adp5055 *adp5055;
	int i, ret;

	init_data = of_get_regulator_init_data(dev, client->dev.of_node,
					       &adp5055_regulators[0]);
	if (!init_data)
		return -EINVAL;

	adp5055 = devm_kzalloc(dev, sizeof(struct adp5055), GFP_KERNEL);
	if (!adp5055)
		return -ENOMEM;

	adp5055->tset = 2600;
	adp5055->en_mode_software = false;

	adp5055->regmap = devm_regmap_init_i2c(client, &adp5055_regmap_config);
	if (IS_ERR(adp5055->regmap))
		return dev_err_probe(dev, PTR_ERR(adp5055->regmap), "Failed to allocate reg map");

	for (i = 0; i < ADP5055_NUM_CH; i++) {
		const struct regulator_desc *desc;
		struct regulator_config config = { };
		struct regulator_dev *rdev;

		if (adp5055->tset == 2600)
			adp5055_regulators[i].ramp_delay_table = adp5055_enable_delay_vals_2_6;
		else
			adp5055_regulators[i].ramp_delay_table = adp5055_enable_delay_vals_20_8;

		desc = &adp5055_regulators[i];

		config.dev = dev;
		config.driver_data = adp5055;
		config.regmap = adp5055->regmap;
		config.init_data = init_data;

		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev)) {
			return dev_err_probe(dev, PTR_ERR(rdev),
					"Failed to register %s\n", desc->name);
		}
	}

	ret = adp5055_parse_fw(dev, adp5055);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct of_device_id adp5055_of_match[] = {
	{ .compatible = "adi,adp5055", },
	{ }
};
MODULE_DEVICE_TABLE(of, adp5055_of_match);

static const struct i2c_device_id adp5055_ids[] = {
	{ .name = "adp5055"},
	{ },
};
MODULE_DEVICE_TABLE(i2c, adp5055_ids);

static struct i2c_driver adp5055_driver = {
	.driver	= {
		.name	= "adp5055",
		.of_match_table = adp5055_of_match,
	},
	.probe		= adp5055_probe,
	.id_table	= adp5055_ids,
};
module_i2c_driver(adp5055_driver);

MODULE_DESCRIPTION("ADP5055 Voltage Regulator Driver");
MODULE_AUTHOR("Alexis Czezar Torreno <alexisczezar.torreno@analog.com>");
MODULE_LICENSE("GPL");

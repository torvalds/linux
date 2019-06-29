// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ads7828.c - driver for TI ADS7828 8-channel A/D converter and compatibles
 * (C) 2007 EADS Astrium
 *
 * This driver is based on the lm75 and other lm_sensors/hwmon drivers
 *
 * Written by Steve Hardy <shardy@redhat.com>
 *
 * ADS7830 support, by Guillaume Roguez <guillaume.roguez@savoirfairelinux.com>
 *
 * For further information, see the Documentation/hwmon/ads7828.rst file.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_data/ads7828.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>

/* The ADS7828 registers */
#define ADS7828_CMD_SD_SE	0x80	/* Single ended inputs */
#define ADS7828_CMD_PD1		0x04	/* Internal vref OFF && A/D ON */
#define ADS7828_CMD_PD3		0x0C	/* Internal vref ON && A/D ON */
#define ADS7828_INT_VREF_MV	2500	/* Internal vref is 2.5V, 2500mV */
#define ADS7828_EXT_VREF_MV_MIN	50	/* External vref min value 0.05V */
#define ADS7828_EXT_VREF_MV_MAX	5250	/* External vref max value 5.25V */

/* List of supported devices */
enum ads7828_chips { ads7828, ads7830 };

/* Client specific data */
struct ads7828_data {
	struct regmap *regmap;
	u8 cmd_byte;			/* Command byte without channel bits */
	unsigned int lsb_resol;		/* Resolution of the ADC sample LSB */
};

/* Command byte C2,C1,C0 - see datasheet */
static inline u8 ads7828_cmd_byte(u8 cmd, int ch)
{
	return cmd | (((ch >> 1) | (ch & 0x01) << 2) << 4);
}

/* sysfs callback function */
static ssize_t ads7828_in_show(struct device *dev,
			       struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ads7828_data *data = dev_get_drvdata(dev);
	u8 cmd = ads7828_cmd_byte(data->cmd_byte, attr->index);
	unsigned int regval;
	int err;

	err = regmap_read(data->regmap, cmd, &regval);
	if (err < 0)
		return err;

	return sprintf(buf, "%d\n",
		       DIV_ROUND_CLOSEST(regval * data->lsb_resol, 1000));
}

static SENSOR_DEVICE_ATTR_RO(in0_input, ads7828_in, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, ads7828_in, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, ads7828_in, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, ads7828_in, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, ads7828_in, 4);
static SENSOR_DEVICE_ATTR_RO(in5_input, ads7828_in, 5);
static SENSOR_DEVICE_ATTR_RO(in6_input, ads7828_in, 6);
static SENSOR_DEVICE_ATTR_RO(in7_input, ads7828_in, 7);

static struct attribute *ads7828_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(ads7828);

static const struct regmap_config ads2828_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
};

static const struct regmap_config ads2830_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ads7828_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ads7828_platform_data *pdata = dev_get_platdata(dev);
	struct ads7828_data *data;
	struct device *hwmon_dev;
	unsigned int vref_mv = ADS7828_INT_VREF_MV;
	unsigned int vref_uv;
	bool diff_input = false;
	bool ext_vref = false;
	unsigned int regval;
	enum ads7828_chips chip;
	struct regulator *reg;

	data = devm_kzalloc(dev, sizeof(struct ads7828_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (pdata) {
		diff_input = pdata->diff_input;
		ext_vref = pdata->ext_vref;
		if (ext_vref && pdata->vref_mv)
			vref_mv = pdata->vref_mv;
	} else if (dev->of_node) {
		diff_input = of_property_read_bool(dev->of_node,
						   "ti,differential-input");
		reg = devm_regulator_get_optional(dev, "vref");
		if (!IS_ERR(reg)) {
			vref_uv = regulator_get_voltage(reg);
			vref_mv = DIV_ROUND_CLOSEST(vref_uv, 1000);
			if (vref_mv < ADS7828_EXT_VREF_MV_MIN ||
			    vref_mv > ADS7828_EXT_VREF_MV_MAX)
				return -EINVAL;
			ext_vref = true;
		}
	}

	if (client->dev.of_node)
		chip = (enum ads7828_chips)
			of_device_get_match_data(&client->dev);
	else
		chip = id->driver_data;

	/* Bound Vref with min/max values */
	vref_mv = clamp_val(vref_mv, ADS7828_EXT_VREF_MV_MIN,
			    ADS7828_EXT_VREF_MV_MAX);

	/* ADS7828 uses 12-bit samples, while ADS7830 is 8-bit */
	if (chip == ads7828) {
		data->lsb_resol = DIV_ROUND_CLOSEST(vref_mv * 1000, 4096);
		data->regmap = devm_regmap_init_i2c(client,
						    &ads2828_regmap_config);
	} else {
		data->lsb_resol = DIV_ROUND_CLOSEST(vref_mv * 1000, 256);
		data->regmap = devm_regmap_init_i2c(client,
						    &ads2830_regmap_config);
	}

	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	data->cmd_byte = ext_vref ? ADS7828_CMD_PD1 : ADS7828_CMD_PD3;
	if (!diff_input)
		data->cmd_byte |= ADS7828_CMD_SD_SE;

	/*
	 * Datasheet specifies internal reference voltage is disabled by
	 * default. The internal reference voltage needs to be enabled and
	 * voltage needs to settle before getting valid ADC data. So perform a
	 * dummy read to enable the internal reference voltage.
	 */
	if (!ext_vref)
		regmap_read(data->regmap, data->cmd_byte, &regval);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   ads7828_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ads7828_device_ids[] = {
	{ "ads7828", ads7828 },
	{ "ads7830", ads7830 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads7828_device_ids);

static const struct of_device_id __maybe_unused ads7828_of_match[] = {
	{
		.compatible = "ti,ads7828",
		.data = (void *)ads7828
	},
	{
		.compatible = "ti,ads7830",
		.data = (void *)ads7830
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ads7828_of_match);

static struct i2c_driver ads7828_driver = {
	.driver = {
		.name = "ads7828",
		.of_match_table = of_match_ptr(ads7828_of_match),
	},

	.id_table = ads7828_device_ids,
	.probe = ads7828_probe,
};

module_i2c_driver(ads7828_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steve Hardy <shardy@redhat.com>");
MODULE_DESCRIPTION("Driver for TI ADS7828 A/D converter and compatibles");

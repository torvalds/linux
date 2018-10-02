/*
 * INA3221 Triple Current/Voltage Monitor
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define INA3221_DRIVER_NAME		"ina3221"

#define INA3221_CONFIG			0x00
#define INA3221_SHUNT1			0x01
#define INA3221_BUS1			0x02
#define INA3221_SHUNT2			0x03
#define INA3221_BUS2			0x04
#define INA3221_SHUNT3			0x05
#define INA3221_BUS3			0x06
#define INA3221_CRIT1			0x07
#define INA3221_WARN1			0x08
#define INA3221_CRIT2			0x09
#define INA3221_WARN2			0x0a
#define INA3221_CRIT3			0x0b
#define INA3221_WARN3			0x0c
#define INA3221_MASK_ENABLE		0x0f

#define INA3221_CONFIG_MODE_MASK	GENMASK(2, 0)
#define INA3221_CONFIG_MODE_POWERDOWN	0
#define INA3221_CONFIG_MODE_SHUNT	BIT(0)
#define INA3221_CONFIG_MODE_BUS		BIT(1)
#define INA3221_CONFIG_MODE_CONTINUOUS	BIT(2)
#define INA3221_CONFIG_CHx_EN(x)	BIT(14 - (x))

#define INA3221_RSHUNT_DEFAULT		10000

enum ina3221_fields {
	/* Configuration */
	F_RST,

	/* Alert Flags */
	F_WF3, F_WF2, F_WF1,
	F_CF3, F_CF2, F_CF1,

	/* sentinel */
	F_MAX_FIELDS
};

static const struct reg_field ina3221_reg_fields[] = {
	[F_RST] = REG_FIELD(INA3221_CONFIG, 15, 15),

	[F_WF3] = REG_FIELD(INA3221_MASK_ENABLE, 3, 3),
	[F_WF2] = REG_FIELD(INA3221_MASK_ENABLE, 4, 4),
	[F_WF1] = REG_FIELD(INA3221_MASK_ENABLE, 5, 5),
	[F_CF3] = REG_FIELD(INA3221_MASK_ENABLE, 7, 7),
	[F_CF2] = REG_FIELD(INA3221_MASK_ENABLE, 8, 8),
	[F_CF1] = REG_FIELD(INA3221_MASK_ENABLE, 9, 9),
};

enum ina3221_channels {
	INA3221_CHANNEL1,
	INA3221_CHANNEL2,
	INA3221_CHANNEL3,
	INA3221_NUM_CHANNELS
};

static const unsigned int register_channel[] = {
	[INA3221_BUS1] = INA3221_CHANNEL1,
	[INA3221_BUS2] = INA3221_CHANNEL2,
	[INA3221_BUS3] = INA3221_CHANNEL3,
	[INA3221_SHUNT1] = INA3221_CHANNEL1,
	[INA3221_SHUNT2] = INA3221_CHANNEL2,
	[INA3221_SHUNT3] = INA3221_CHANNEL3,
	[INA3221_CRIT1] = INA3221_CHANNEL1,
	[INA3221_CRIT2] = INA3221_CHANNEL2,
	[INA3221_CRIT3] = INA3221_CHANNEL3,
	[INA3221_WARN1] = INA3221_CHANNEL1,
	[INA3221_WARN2] = INA3221_CHANNEL2,
	[INA3221_WARN3] = INA3221_CHANNEL3,
};

/**
 * struct ina3221_input - channel input source specific information
 * @label: label of channel input source
 * @shunt_resistor: shunt resistor value of channel input source
 * @disconnected: connection status of channel input source
 */
struct ina3221_input {
	const char *label;
	int shunt_resistor;
	bool disconnected;
};

/**
 * struct ina3221_data - device specific information
 * @regmap: Register map of the device
 * @fields: Register fields of the device
 * @inputs: Array of channel input source specific structures
 * @reg_config: Register value of INA3221_CONFIG
 */
struct ina3221_data {
	struct regmap *regmap;
	struct regmap_field *fields[F_MAX_FIELDS];
	struct ina3221_input inputs[INA3221_NUM_CHANNELS];
	u32 reg_config;
};

static inline bool ina3221_is_enabled(struct ina3221_data *ina, int channel)
{
	return ina->reg_config & INA3221_CONFIG_CHx_EN(channel);
}

static ssize_t ina3221_show_label(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;
	struct ina3221_input *input = &ina->inputs[channel];

	return snprintf(buf, PAGE_SIZE, "%s\n", input->label);
}

static ssize_t ina3221_show_enable(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			ina3221_is_enabled(ina, channel));
}

static ssize_t ina3221_set_enable(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;
	u16 config, mask = INA3221_CONFIG_CHx_EN(channel);
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	config = enable ? mask : 0;

	/* Enable or disable the channel */
	ret = regmap_update_bits(ina->regmap, INA3221_CONFIG, mask, config);
	if (ret)
		return ret;

	/* Cache the latest config register value */
	ret = regmap_read(ina->regmap, INA3221_CONFIG, &ina->reg_config);
	if (ret)
		return ret;

	return count;
}

static int ina3221_read_value(struct ina3221_data *ina, unsigned int reg,
			      int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(ina->regmap, reg, &regval);
	if (ret)
		return ret;

	*val = sign_extend32(regval >> 3, 12);

	return 0;
}

static ssize_t ina3221_show_bus_voltage(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int reg = sd_attr->index;
	unsigned int channel = register_channel[reg];
	int val, voltage_mv, ret;

	/* No data for read-only attribute if channel is disabled */
	if (!attr->store && !ina3221_is_enabled(ina, channel))
		return -ENODATA;

	ret = ina3221_read_value(ina, reg, &val);
	if (ret)
		return ret;

	voltage_mv = val * 8;

	return snprintf(buf, PAGE_SIZE, "%d\n", voltage_mv);
}

static ssize_t ina3221_show_shunt_voltage(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int reg = sd_attr->index;
	unsigned int channel = register_channel[reg];
	int val, voltage_uv, ret;

	/* No data for read-only attribute if channel is disabled */
	if (!attr->store && !ina3221_is_enabled(ina, channel))
		return -ENODATA;

	ret = ina3221_read_value(ina, reg, &val);
	if (ret)
		return ret;
	voltage_uv = val * 40;

	return snprintf(buf, PAGE_SIZE, "%d\n", voltage_uv);
}

static ssize_t ina3221_show_current(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int reg = sd_attr->index;
	unsigned int channel = register_channel[reg];
	struct ina3221_input *input = &ina->inputs[channel];
	int resistance_uo = input->shunt_resistor;
	int val, current_ma, voltage_nv, ret;

	/* No data for read-only attribute if channel is disabled */
	if (!attr->store && !ina3221_is_enabled(ina, channel))
		return -ENODATA;

	ret = ina3221_read_value(ina, reg, &val);
	if (ret)
		return ret;
	voltage_nv = val * 40000;

	current_ma = DIV_ROUND_CLOSEST(voltage_nv, resistance_uo);

	return snprintf(buf, PAGE_SIZE, "%d\n", current_ma);
}

static ssize_t ina3221_set_current(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int reg = sd_attr->index;
	unsigned int channel = register_channel[reg];
	struct ina3221_input *input = &ina->inputs[channel];
	int resistance_uo = input->shunt_resistor;
	int val, current_ma, voltage_uv, ret;

	ret = kstrtoint(buf, 0, &current_ma);
	if (ret)
		return ret;

	/* clamp current */
	current_ma = clamp_val(current_ma,
			       INT_MIN / resistance_uo,
			       INT_MAX / resistance_uo);

	voltage_uv = DIV_ROUND_CLOSEST(current_ma * resistance_uo, 1000);

	/* clamp voltage */
	voltage_uv = clamp_val(voltage_uv, -163800, 163800);

	/* 1 / 40uV(scale) << 3(register shift) = 5 */
	val = DIV_ROUND_CLOSEST(voltage_uv, 5) & 0xfff8;

	ret = regmap_write(ina->regmap, reg, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t ina3221_show_shunt(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;
	struct ina3221_input *input = &ina->inputs[channel];

	return snprintf(buf, PAGE_SIZE, "%d\n", input->shunt_resistor);
}

static ssize_t ina3221_set_shunt(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;
	struct ina3221_input *input = &ina->inputs[channel];
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	val = clamp_val(val, 1, INT_MAX);

	input->shunt_resistor = val;

	return count;
}

static ssize_t ina3221_show_alert(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int field = sd_attr->index;
	unsigned int regval;
	int ret;

	ret = regmap_field_read(ina->fields[field], &regval);
	if (ret)
		return ret;

	return snprintf(buf, PAGE_SIZE, "%d\n", regval);
}

/* input channel label */
static SENSOR_DEVICE_ATTR(in1_label, 0444,
		ina3221_show_label, NULL, INA3221_CHANNEL1);
static SENSOR_DEVICE_ATTR(in2_label, 0444,
		ina3221_show_label, NULL, INA3221_CHANNEL2);
static SENSOR_DEVICE_ATTR(in3_label, 0444,
		ina3221_show_label, NULL, INA3221_CHANNEL3);

/* voltage channel enable */
static SENSOR_DEVICE_ATTR(in1_enable, 0644,
		ina3221_show_enable, ina3221_set_enable, INA3221_CHANNEL1);
static SENSOR_DEVICE_ATTR(in2_enable, 0644,
		ina3221_show_enable, ina3221_set_enable, INA3221_CHANNEL2);
static SENSOR_DEVICE_ATTR(in3_enable, 0644,
		ina3221_show_enable, ina3221_set_enable, INA3221_CHANNEL3);

/* bus voltage */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO,
		ina3221_show_bus_voltage, NULL, INA3221_BUS1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO,
		ina3221_show_bus_voltage, NULL, INA3221_BUS2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO,
		ina3221_show_bus_voltage, NULL, INA3221_BUS3);

/* calculated current */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO,
		ina3221_show_current, NULL, INA3221_SHUNT1);
static SENSOR_DEVICE_ATTR(curr2_input, S_IRUGO,
		ina3221_show_current, NULL, INA3221_SHUNT2);
static SENSOR_DEVICE_ATTR(curr3_input, S_IRUGO,
		ina3221_show_current, NULL, INA3221_SHUNT3);

/* shunt resistance */
static SENSOR_DEVICE_ATTR(shunt1_resistor, S_IRUGO | S_IWUSR,
		ina3221_show_shunt, ina3221_set_shunt, INA3221_CHANNEL1);
static SENSOR_DEVICE_ATTR(shunt2_resistor, S_IRUGO | S_IWUSR,
		ina3221_show_shunt, ina3221_set_shunt, INA3221_CHANNEL2);
static SENSOR_DEVICE_ATTR(shunt3_resistor, S_IRUGO | S_IWUSR,
		ina3221_show_shunt, ina3221_set_shunt, INA3221_CHANNEL3);

/* critical current */
static SENSOR_DEVICE_ATTR(curr1_crit, S_IRUGO | S_IWUSR,
		ina3221_show_current, ina3221_set_current, INA3221_CRIT1);
static SENSOR_DEVICE_ATTR(curr2_crit, S_IRUGO | S_IWUSR,
		ina3221_show_current, ina3221_set_current, INA3221_CRIT2);
static SENSOR_DEVICE_ATTR(curr3_crit, S_IRUGO | S_IWUSR,
		ina3221_show_current, ina3221_set_current, INA3221_CRIT3);

/* critical current alert */
static SENSOR_DEVICE_ATTR(curr1_crit_alarm, S_IRUGO,
		ina3221_show_alert, NULL, F_CF1);
static SENSOR_DEVICE_ATTR(curr2_crit_alarm, S_IRUGO,
		ina3221_show_alert, NULL, F_CF2);
static SENSOR_DEVICE_ATTR(curr3_crit_alarm, S_IRUGO,
		ina3221_show_alert, NULL, F_CF3);

/* warning current */
static SENSOR_DEVICE_ATTR(curr1_max, S_IRUGO | S_IWUSR,
		ina3221_show_current, ina3221_set_current, INA3221_WARN1);
static SENSOR_DEVICE_ATTR(curr2_max, S_IRUGO | S_IWUSR,
		ina3221_show_current, ina3221_set_current, INA3221_WARN2);
static SENSOR_DEVICE_ATTR(curr3_max, S_IRUGO | S_IWUSR,
		ina3221_show_current, ina3221_set_current, INA3221_WARN3);

/* warning current alert */
static SENSOR_DEVICE_ATTR(curr1_max_alarm, S_IRUGO,
		ina3221_show_alert, NULL, F_WF1);
static SENSOR_DEVICE_ATTR(curr2_max_alarm, S_IRUGO,
		ina3221_show_alert, NULL, F_WF2);
static SENSOR_DEVICE_ATTR(curr3_max_alarm, S_IRUGO,
		ina3221_show_alert, NULL, F_WF3);

/* shunt voltage */
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO,
		ina3221_show_shunt_voltage, NULL, INA3221_SHUNT1);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO,
		ina3221_show_shunt_voltage, NULL, INA3221_SHUNT2);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO,
		ina3221_show_shunt_voltage, NULL, INA3221_SHUNT3);

static struct attribute *ina3221_attrs[] = {
	/* channel 1 -- make sure label at first */
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_in1_enable.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_shunt1_resistor.dev_attr.attr,
	&sensor_dev_attr_curr1_crit.dev_attr.attr,
	&sensor_dev_attr_curr1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_curr1_max.dev_attr.attr,
	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,

	/* channel 2 -- make sure label at first */
	&sensor_dev_attr_in2_label.dev_attr.attr,
	&sensor_dev_attr_in2_enable.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_curr2_input.dev_attr.attr,
	&sensor_dev_attr_shunt2_resistor.dev_attr.attr,
	&sensor_dev_attr_curr2_crit.dev_attr.attr,
	&sensor_dev_attr_curr2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_curr2_max.dev_attr.attr,
	&sensor_dev_attr_curr2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,

	/* channel 3 -- make sure label at first */
	&sensor_dev_attr_in3_label.dev_attr.attr,
	&sensor_dev_attr_in3_enable.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_curr3_input.dev_attr.attr,
	&sensor_dev_attr_shunt3_resistor.dev_attr.attr,
	&sensor_dev_attr_curr3_crit.dev_attr.attr,
	&sensor_dev_attr_curr3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_curr3_max.dev_attr.attr,
	&sensor_dev_attr_curr3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,

	NULL,
};

static umode_t ina3221_attr_is_visible(struct kobject *kobj,
				       struct attribute *attr, int n)
{
	const int max_attrs = ARRAY_SIZE(ina3221_attrs) - 1;
	const int num_attrs = max_attrs / INA3221_NUM_CHANNELS;
	struct device *dev = kobj_to_dev(kobj);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	enum ina3221_channels channel = n / num_attrs;
	struct ina3221_input *input = &ina->inputs[channel];
	int index = n % num_attrs;

	/* Hide label node if label is not provided */
	if (index == 0 && !input->label)
		return 0;

	return attr->mode;
}

static const struct attribute_group ina3221_group = {
	.is_visible = ina3221_attr_is_visible,
	.attrs = ina3221_attrs,
};
__ATTRIBUTE_GROUPS(ina3221);

static const struct regmap_range ina3221_yes_ranges[] = {
	regmap_reg_range(INA3221_CONFIG, INA3221_BUS3),
	regmap_reg_range(INA3221_MASK_ENABLE, INA3221_MASK_ENABLE),
};

static const struct regmap_access_table ina3221_volatile_table = {
	.yes_ranges = ina3221_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(ina3221_yes_ranges),
};

static const struct regmap_config ina3221_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &ina3221_volatile_table,
};

static int ina3221_probe_child_from_dt(struct device *dev,
				       struct device_node *child,
				       struct ina3221_data *ina)
{
	struct ina3221_input *input;
	u32 val;
	int ret;

	ret = of_property_read_u32(child, "reg", &val);
	if (ret) {
		dev_err(dev, "missing reg property of %s\n", child->name);
		return ret;
	} else if (val > INA3221_CHANNEL3) {
		dev_err(dev, "invalid reg %d of %s\n", val, child->name);
		return ret;
	}

	input = &ina->inputs[val];

	/* Log the disconnected channel input */
	if (!of_device_is_available(child)) {
		input->disconnected = true;
		return 0;
	}

	/* Save the connected input label if available */
	of_property_read_string(child, "label", &input->label);

	/* Overwrite default shunt resistor value optionally */
	if (!of_property_read_u32(child, "shunt-resistor-micro-ohms", &val))
		input->shunt_resistor = val;

	return 0;
}

static int ina3221_probe_from_dt(struct device *dev, struct ina3221_data *ina)
{
	const struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret;

	/* Compatible with non-DT platforms */
	if (!np)
		return 0;

	for_each_child_of_node(np, child) {
		ret = ina3221_probe_child_from_dt(dev, child, ina);
		if (ret)
			return ret;
	}

	return 0;
}

static int ina3221_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ina3221_data *ina;
	struct device *hwmon_dev;
	int i, ret;

	ina = devm_kzalloc(dev, sizeof(*ina), GFP_KERNEL);
	if (!ina)
		return -ENOMEM;

	ina->regmap = devm_regmap_init_i2c(client, &ina3221_regmap_config);
	if (IS_ERR(ina->regmap)) {
		dev_err(dev, "Unable to allocate register map\n");
		return PTR_ERR(ina->regmap);
	}

	for (i = 0; i < F_MAX_FIELDS; i++) {
		ina->fields[i] = devm_regmap_field_alloc(dev,
							 ina->regmap,
							 ina3221_reg_fields[i]);
		if (IS_ERR(ina->fields[i])) {
			dev_err(dev, "Unable to allocate regmap fields\n");
			return PTR_ERR(ina->fields[i]);
		}
	}

	for (i = 0; i < INA3221_NUM_CHANNELS; i++)
		ina->inputs[i].shunt_resistor = INA3221_RSHUNT_DEFAULT;

	ret = ina3221_probe_from_dt(dev, ina);
	if (ret) {
		dev_err(dev, "Unable to probe from device tree\n");
		return ret;
	}

	ret = regmap_field_write(ina->fields[F_RST], true);
	if (ret) {
		dev_err(dev, "Unable to reset device\n");
		return ret;
	}

	/* Sync config register after reset */
	ret = regmap_read(ina->regmap, INA3221_CONFIG, &ina->reg_config);
	if (ret)
		return ret;

	/* Disable channels if their inputs are disconnected */
	for (i = 0; i < INA3221_NUM_CHANNELS; i++) {
		if (ina->inputs[i].disconnected)
			ina->reg_config &= ~INA3221_CONFIG_CHx_EN(i);
	}
	ret = regmap_write(ina->regmap, INA3221_CONFIG, ina->reg_config);
	if (ret)
		return ret;

	dev_set_drvdata(dev, ina);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name,
							   ina, ina3221_groups);
	if (IS_ERR(hwmon_dev)) {
		dev_err(dev, "Unable to register hwmon device\n");
		return PTR_ERR(hwmon_dev);
	}

	return 0;
}

static int __maybe_unused ina3221_suspend(struct device *dev)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret;

	/* Save config register value and enable cache-only */
	ret = regmap_read(ina->regmap, INA3221_CONFIG, &ina->reg_config);
	if (ret)
		return ret;

	/* Set to power-down mode for power saving */
	ret = regmap_update_bits(ina->regmap, INA3221_CONFIG,
				 INA3221_CONFIG_MODE_MASK,
				 INA3221_CONFIG_MODE_POWERDOWN);
	if (ret)
		return ret;

	regcache_cache_only(ina->regmap, true);
	regcache_mark_dirty(ina->regmap);

	return 0;
}

static int __maybe_unused ina3221_resume(struct device *dev)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(ina->regmap, false);

	/* Software reset the chip */
	ret = regmap_field_write(ina->fields[F_RST], true);
	if (ret) {
		dev_err(dev, "Unable to reset device\n");
		return ret;
	}

	/* Restore cached register values to hardware */
	ret = regcache_sync(ina->regmap);
	if (ret)
		return ret;

	/* Restore config register value to hardware */
	ret = regmap_write(ina->regmap, INA3221_CONFIG, ina->reg_config);
	if (ret)
		return ret;

	return 0;
}

static const struct dev_pm_ops ina3221_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(ina3221_suspend, ina3221_resume)
};

static const struct of_device_id ina3221_of_match_table[] = {
	{ .compatible = "ti,ina3221", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ina3221_of_match_table);

static const struct i2c_device_id ina3221_ids[] = {
	{ "ina3221", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ina3221_ids);

static struct i2c_driver ina3221_i2c_driver = {
	.probe = ina3221_probe,
	.driver = {
		.name = INA3221_DRIVER_NAME,
		.of_match_table = ina3221_of_match_table,
		.pm = &ina3221_pm,
	},
	.id_table = ina3221_ids,
};
module_i2c_driver(ina3221_i2c_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("Texas Instruments INA3221 HWMon Driver");
MODULE_LICENSE("GPL v2");

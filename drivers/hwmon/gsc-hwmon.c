// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Gateworks System Controller Hardware Monitor module
 *
 * Copyright (C) 2020 Gateworks Corporation
 */
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/mfd/gsc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/platform_data/gsc_hwmon.h>

#define GSC_HWMON_MAX_TEMP_CH	16
#define GSC_HWMON_MAX_IN_CH	16
#define GSC_HWMON_MAX_FAN_CH	16

#define GSC_HWMON_RESOLUTION	12
#define GSC_HWMON_VREF		2500

struct gsc_hwmon_data {
	struct gsc_dev *gsc;
	struct gsc_hwmon_platform_data *pdata;
	struct regmap *regmap;
	const struct gsc_hwmon_channel *temp_ch[GSC_HWMON_MAX_TEMP_CH];
	const struct gsc_hwmon_channel *in_ch[GSC_HWMON_MAX_IN_CH];
	const struct gsc_hwmon_channel *fan_ch[GSC_HWMON_MAX_FAN_CH];
	u32 temp_config[GSC_HWMON_MAX_TEMP_CH + 1];
	u32 in_config[GSC_HWMON_MAX_IN_CH + 1];
	u32 fan_config[GSC_HWMON_MAX_FAN_CH + 1];
	struct hwmon_channel_info temp_info;
	struct hwmon_channel_info in_info;
	struct hwmon_channel_info fan_info;
	const struct hwmon_channel_info *info[4];
	struct hwmon_chip_info chip;
};

static struct regmap_bus gsc_hwmon_regmap_bus = {
	.reg_read = gsc_read,
	.reg_write = gsc_write,
};

static const struct regmap_config gsc_hwmon_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};

static ssize_t pwm_auto_point_temp_show(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct gsc_hwmon_data *hwmon = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	u8 reg = hwmon->pdata->fan_base + (2 * attr->index);
	u8 regs[2];
	int ret;

	ret = regmap_bulk_read(hwmon->regmap, reg, regs, 2);
	if (ret)
		return ret;

	ret = regs[0] | regs[1] << 8;
	return sprintf(buf, "%d\n", ret * 10);
}

static ssize_t pwm_auto_point_temp_store(struct device *dev,
					 struct device_attribute *devattr,
					 const char *buf, size_t count)
{
	struct gsc_hwmon_data *hwmon = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	u8 reg = hwmon->pdata->fan_base + (2 * attr->index);
	u8 regs[2];
	long temp;
	int err;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 10000);
	temp = DIV_ROUND_CLOSEST(temp, 10);

	regs[0] = temp & 0xff;
	regs[1] = (temp >> 8) & 0xff;
	err = regmap_bulk_write(hwmon->regmap, reg, regs, 2);
	if (err)
		return err;

	return count;
}

static ssize_t pwm_auto_point_pwm_show(struct device *dev,
				       struct device_attribute *devattr,
				       char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%d\n", 255 * (50 + (attr->index * 10)) / 100);
}

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point1_pwm, pwm_auto_point_pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point1_temp, pwm_auto_point_temp, 0);

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point2_pwm, pwm_auto_point_pwm, 1);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point2_temp, pwm_auto_point_temp, 1);

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point3_pwm, pwm_auto_point_pwm, 2);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point3_temp, pwm_auto_point_temp, 2);

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point4_pwm, pwm_auto_point_pwm, 3);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point4_temp, pwm_auto_point_temp, 3);

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point5_pwm, pwm_auto_point_pwm, 4);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point5_temp, pwm_auto_point_temp, 4);

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point6_pwm, pwm_auto_point_pwm, 5);
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point6_temp, pwm_auto_point_temp, 5);

static struct attribute *gsc_hwmon_attributes[] = {
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point5_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point5_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point6_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point6_temp.dev_attr.attr,
	NULL
};

static const struct attribute_group gsc_hwmon_group = {
	.attrs = gsc_hwmon_attributes,
};
__ATTRIBUTE_GROUPS(gsc_hwmon);

static int
gsc_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	       int channel, long *val)
{
	struct gsc_hwmon_data *hwmon = dev_get_drvdata(dev);
	const struct gsc_hwmon_channel *ch;
	int sz, ret;
	long tmp;
	u8 buf[3];

	switch (type) {
	case hwmon_in:
		ch = hwmon->in_ch[channel];
		break;
	case hwmon_temp:
		ch = hwmon->temp_ch[channel];
		break;
	case hwmon_fan:
		ch = hwmon->fan_ch[channel];
		break;
	default:
		return -EOPNOTSUPP;
	}

	sz = (ch->mode == mode_voltage_24bit) ? 3 : 2;
	ret = regmap_bulk_read(hwmon->regmap, ch->reg, buf, sz);
	if (ret)
		return ret;

	tmp = 0;
	while (sz-- > 0)
		tmp |= (buf[sz] << (8 * sz));

	switch (ch->mode) {
	case mode_temperature:
		if (tmp > 0x8000)
			tmp -= 0xffff;
		tmp *= 100; /* convert to millidegrees celsius */
		break;
	case mode_voltage_raw:
		tmp = clamp_val(tmp, 0, BIT(GSC_HWMON_RESOLUTION));
		/* scale based on ref voltage and ADC resolution */
		tmp *= GSC_HWMON_VREF;
		tmp >>= GSC_HWMON_RESOLUTION;
		/* scale based on optional voltage divider */
		if (ch->vdiv[0] && ch->vdiv[1]) {
			tmp *= (ch->vdiv[0] + ch->vdiv[1]);
			tmp /= ch->vdiv[1];
		}
		/* adjust by uV offset */
		tmp += ch->mvoffset;
		break;
	case mode_fan:
		tmp *= 30; /* convert to revolutions per minute */
		break;
	case mode_voltage_24bit:
	case mode_voltage_16bit:
		/* no adjustment needed */
		break;
	}

	*val = tmp;

	return 0;
}

static int
gsc_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, const char **buf)
{
	struct gsc_hwmon_data *hwmon = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_in:
		*buf = hwmon->in_ch[channel]->name;
		break;
	case hwmon_temp:
		*buf = hwmon->temp_ch[channel]->name;
		break;
	case hwmon_fan:
		*buf = hwmon->fan_ch[channel]->name;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static umode_t
gsc_hwmon_is_visible(const void *_data, enum hwmon_sensor_types type, u32 attr,
		     int ch)
{
	return 0444;
}

static const struct hwmon_ops gsc_hwmon_ops = {
	.is_visible = gsc_hwmon_is_visible,
	.read = gsc_hwmon_read,
	.read_string = gsc_hwmon_read_string,
};

static struct gsc_hwmon_platform_data *
gsc_hwmon_get_devtree_pdata(struct device *dev)
{
	struct gsc_hwmon_platform_data *pdata;
	struct gsc_hwmon_channel *ch;
	struct fwnode_handle *child;
	struct device_node *fan;
	int nchannels;

	nchannels = device_get_child_node_count(dev);
	if (nchannels == 0)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev,
			     sizeof(*pdata) + nchannels * sizeof(*ch),
			     GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);
	ch = (struct gsc_hwmon_channel *)(pdata + 1);
	pdata->channels = ch;
	pdata->nchannels = nchannels;

	/* fan controller base address */
	of_node_get(dev->parent->of_node);
	fan = of_find_compatible_node(dev->parent->of_node, NULL, "gw,gsc-fan");
	if (fan && of_property_read_u32(fan, "reg", &pdata->fan_base)) {
		of_node_put(fan);
		dev_err(dev, "fan node without base\n");
		return ERR_PTR(-EINVAL);
	}

	of_node_put(fan);

	/* allocate structures for channels and count instances of each type */
	device_for_each_child_node(dev, child) {
		if (fwnode_property_read_string(child, "label", &ch->name)) {
			dev_err(dev, "channel without label\n");
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}
		if (fwnode_property_read_u32(child, "reg", &ch->reg)) {
			dev_err(dev, "channel without reg\n");
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}
		if (fwnode_property_read_u32(child, "gw,mode", &ch->mode)) {
			dev_err(dev, "channel without mode\n");
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}
		if (ch->mode > mode_max) {
			dev_err(dev, "invalid channel mode\n");
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}

		if (!fwnode_property_read_u32(child,
					      "gw,voltage-offset-microvolt",
					      &ch->mvoffset))
			ch->mvoffset /= 1000;
		fwnode_property_read_u32_array(child,
					       "gw,voltage-divider-ohms",
					       ch->vdiv, ARRAY_SIZE(ch->vdiv));
		ch++;
	}

	return pdata;
}

static int gsc_hwmon_probe(struct platform_device *pdev)
{
	struct gsc_dev *gsc = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	struct gsc_hwmon_platform_data *pdata = dev_get_platdata(dev);
	struct gsc_hwmon_data *hwmon;
	const struct attribute_group **groups;
	int i, i_in, i_temp, i_fan;

	if (!pdata) {
		pdata = gsc_hwmon_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;
	hwmon->gsc = gsc;
	hwmon->pdata = pdata;

	hwmon->regmap = devm_regmap_init(dev, &gsc_hwmon_regmap_bus,
					 gsc->i2c_hwmon,
					 &gsc_hwmon_regmap_config);
	if (IS_ERR(hwmon->regmap))
		return PTR_ERR(hwmon->regmap);

	for (i = 0, i_in = 0, i_temp = 0, i_fan = 0; i < hwmon->pdata->nchannels; i++) {
		const struct gsc_hwmon_channel *ch = &pdata->channels[i];

		switch (ch->mode) {
		case mode_temperature:
			if (i_temp == GSC_HWMON_MAX_TEMP_CH) {
				dev_err(gsc->dev, "too many temp channels\n");
				return -EINVAL;
			}
			hwmon->temp_ch[i_temp] = ch;
			hwmon->temp_config[i_temp] = HWMON_T_INPUT |
						     HWMON_T_LABEL;
			i_temp++;
			break;
		case mode_fan:
			if (i_fan == GSC_HWMON_MAX_FAN_CH) {
				dev_err(gsc->dev, "too many fan channels\n");
				return -EINVAL;
			}
			hwmon->fan_ch[i_fan] = ch;
			hwmon->fan_config[i_fan] = HWMON_F_INPUT |
						   HWMON_F_LABEL;
			i_fan++;
			break;
		case mode_voltage_24bit:
		case mode_voltage_16bit:
		case mode_voltage_raw:
			if (i_in == GSC_HWMON_MAX_IN_CH) {
				dev_err(gsc->dev, "too many input channels\n");
				return -EINVAL;
			}
			hwmon->in_ch[i_in] = ch;
			hwmon->in_config[i_in] =
				HWMON_I_INPUT | HWMON_I_LABEL;
			i_in++;
			break;
		default:
			dev_err(gsc->dev, "invalid mode: %d\n", ch->mode);
			return -EINVAL;
		}
	}

	/* setup config structures */
	hwmon->chip.ops = &gsc_hwmon_ops;
	hwmon->chip.info = hwmon->info;
	hwmon->info[0] = &hwmon->temp_info;
	hwmon->info[1] = &hwmon->in_info;
	hwmon->info[2] = &hwmon->fan_info;
	hwmon->temp_info.type = hwmon_temp;
	hwmon->temp_info.config = hwmon->temp_config;
	hwmon->in_info.type = hwmon_in;
	hwmon->in_info.config = hwmon->in_config;
	hwmon->fan_info.type = hwmon_fan;
	hwmon->fan_info.config = hwmon->fan_config;

	groups = pdata->fan_base ? gsc_hwmon_groups : NULL;
	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 KBUILD_MODNAME, hwmon,
							 &hwmon->chip, groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id gsc_hwmon_of_match[] = {
	{ .compatible = "gw,gsc-adc", },
	{}
};

static struct platform_driver gsc_hwmon_driver = {
	.driver = {
		.name = "gsc-hwmon",
		.of_match_table = gsc_hwmon_of_match,
	},
	.probe = gsc_hwmon_probe,
};

module_platform_driver(gsc_hwmon_driver);

MODULE_AUTHOR("Tim Harvey <tharvey@gateworks.com>");
MODULE_DESCRIPTION("GSC hardware monitor driver");
MODULE_LICENSE("GPL v2");

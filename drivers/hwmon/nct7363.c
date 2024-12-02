// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023 Nuvoton Technology corporation.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define NCT7363_REG_FUNC_CFG_BASE(x)	(0x20 + (x))
#define NCT7363_REG_LSRS(x)		(0x34 + ((x) / 8))
#define NCT7363_REG_PWMEN_BASE(x)	(0x38 + (x))
#define NCT7363_REG_FANINEN_BASE(x)	(0x41 + (x))
#define NCT7363_REG_FANINX_HVAL(x)	(0x48 + ((x) * 2))
#define NCT7363_REG_FANINX_LVAL(x)	(0x49 + ((x) * 2))
#define NCT7363_REG_FANINX_HL(x)	(0x6C + ((x) * 2))
#define NCT7363_REG_FANINX_LL(x)	(0x6D + ((x) * 2))
#define NCT7363_REG_FSCPXDUTY(x)	(0x90 + ((x) * 2))
#define NCT7363_REG_FSCPXDIV(x)		(0x91 + ((x) * 2))

#define PWM_SEL(x)			(BIT(0) << ((x) * 2))
#define FANIN_SEL(_x)			({typeof(_x) (x) = (_x); \
					 BIT(1) << (((x) < 8) ? \
					 (((x) + 8) * 2) : \
					 (((x) % 8) * 2)); })
#define ALARM_SEL(x, y)			((x) & (BIT((y) % 8)))
#define VALUE_TO_REG(x, y)		(((x) >> ((y) * 8)) & 0xFF)

#define NCT7363_FANINX_LVAL_MASK	GENMASK(4, 0)
#define NCT7363_FANIN_MASK		GENMASK(12, 0)

#define NCT7363_PWM_COUNT		16

static inline unsigned int fan_from_reg(u16 val)
{
	if (val == NCT7363_FANIN_MASK || val == 0)
		return 0;

	return (1350000UL / val);
}

static const struct of_device_id nct7363_of_match[] = {
	{ .compatible = "nuvoton,nct7363", },
	{ .compatible = "nuvoton,nct7362", },
	{ }
};
MODULE_DEVICE_TABLE(of, nct7363_of_match);

struct nct7363_data {
	struct regmap		*regmap;

	u16			fanin_mask;
	u16			pwm_mask;
};

static int nct7363_read_fan(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct nct7363_data *data = dev_get_drvdata(dev);
	unsigned int reg;
	u8 regval[2];
	int ret;
	u16 cnt;

	switch (attr) {
	case hwmon_fan_input:
		/*
		 * High-byte register should be read first to latch
		 * synchronous low-byte value
		 */
		ret = regmap_bulk_read(data->regmap,
				       NCT7363_REG_FANINX_HVAL(channel),
				       &regval, 2);
		if (ret)
			return ret;

		cnt = (regval[0] << 5) | (regval[1] & NCT7363_FANINX_LVAL_MASK);
		*val = fan_from_reg(cnt);
		return 0;
	case hwmon_fan_min:
		ret = regmap_bulk_read(data->regmap,
				       NCT7363_REG_FANINX_HL(channel),
				       &regval, 2);
		if (ret)
			return ret;

		cnt = (regval[0] << 5) | (regval[1] & NCT7363_FANINX_LVAL_MASK);
		*val = fan_from_reg(cnt);
		return 0;
	case hwmon_fan_alarm:
		ret = regmap_read(data->regmap,
				  NCT7363_REG_LSRS(channel), &reg);
		if (ret)
			return ret;

		*val = (long)ALARM_SEL(reg, channel) > 0 ? 1 : 0;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7363_write_fan(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct nct7363_data *data = dev_get_drvdata(dev);
	u8 regval[2];
	int ret;

	if (val <= 0)
		return -EINVAL;

	switch (attr) {
	case hwmon_fan_min:
		val = clamp_val(DIV_ROUND_CLOSEST(1350000, val),
				1, NCT7363_FANIN_MASK);
		regval[0] = val >> 5;
		regval[1] = val & NCT7363_FANINX_LVAL_MASK;

		ret = regmap_bulk_write(data->regmap,
					NCT7363_REG_FANINX_HL(channel),
					regval, 2);
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7363_fan_is_visible(const void *_data, u32 attr, int channel)
{
	const struct nct7363_data *data = _data;

	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_alarm:
		if (data->fanin_mask & BIT(channel))
			return 0444;
		break;
	case hwmon_fan_min:
		if (data->fanin_mask & BIT(channel))
			return 0644;
		break;
	default:
		break;
	}

	return 0;
}

static int nct7363_read_pwm(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct nct7363_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int ret;

	switch (attr) {
	case hwmon_pwm_input:
		ret = regmap_read(data->regmap,
				  NCT7363_REG_FSCPXDUTY(channel), &regval);
		if (ret)
			return ret;

		*val = (long)regval;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7363_write_pwm(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct nct7363_data *data = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;

		ret = regmap_write(data->regmap,
				   NCT7363_REG_FSCPXDUTY(channel), val);

		return ret;

	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7363_pwm_is_visible(const void *_data, u32 attr, int channel)
{
	const struct nct7363_data *data = _data;

	switch (attr) {
	case hwmon_pwm_input:
		if (data->pwm_mask & BIT(channel))
			return 0644;
		break;
	default:
		break;
	}

	return 0;
}

static int nct7363_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return nct7363_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return nct7363_read_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7363_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_fan:
		return nct7363_write_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return nct7363_write_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7363_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return nct7363_fan_is_visible(data, attr, channel);
	case hwmon_pwm:
		return nct7363_pwm_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const struct hwmon_channel_info *nct7363_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_ops nct7363_hwmon_ops = {
	.is_visible = nct7363_is_visible,
	.read = nct7363_read,
	.write = nct7363_write,
};

static const struct hwmon_chip_info nct7363_chip_info = {
	.ops = &nct7363_hwmon_ops,
	.info = nct7363_info,
};

static int nct7363_init_chip(struct nct7363_data *data)
{
	u32 func_config = 0;
	int i, ret;

	/* Pin Function Configuration */
	for (i = 0; i < NCT7363_PWM_COUNT; i++) {
		if (data->pwm_mask & BIT(i))
			func_config |= PWM_SEL(i);
		if (data->fanin_mask & BIT(i))
			func_config |= FANIN_SEL(i);
	}

	for (i = 0; i < 4; i++) {
		ret = regmap_write(data->regmap, NCT7363_REG_FUNC_CFG_BASE(i),
				   VALUE_TO_REG(func_config, i));
		if (ret < 0)
			return ret;
	}

	/* PWM and FANIN Monitoring Enable */
	for (i = 0; i < 2; i++) {
		ret = regmap_write(data->regmap, NCT7363_REG_PWMEN_BASE(i),
				   VALUE_TO_REG(data->pwm_mask, i));
		if (ret < 0)
			return ret;

		ret = regmap_write(data->regmap, NCT7363_REG_FANINEN_BASE(i),
				   VALUE_TO_REG(data->fanin_mask, i));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int nct7363_present_pwm_fanin(struct device *dev,
				     struct device_node *child,
				     struct nct7363_data *data)
{
	u8 fanin_ch[NCT7363_PWM_COUNT];
	struct of_phandle_args args;
	int ret, fanin_cnt;
	u8 ch, index;

	ret = of_parse_phandle_with_args(child, "pwms", "#pwm-cells",
					 0, &args);
	if (ret)
		return ret;

	if (args.args[0] >= NCT7363_PWM_COUNT)
		return -EINVAL;
	data->pwm_mask |= BIT(args.args[0]);

	fanin_cnt = of_property_count_u8_elems(child, "tach-ch");
	if (fanin_cnt < 1 || fanin_cnt > NCT7363_PWM_COUNT)
		return -EINVAL;

	ret = of_property_read_u8_array(child, "tach-ch", fanin_ch, fanin_cnt);
	if (ret)
		return ret;

	for (ch = 0; ch < fanin_cnt; ch++) {
		index = fanin_ch[ch];
		if (index >= NCT7363_PWM_COUNT)
			return -EINVAL;
		data->fanin_mask |= BIT(index);
	}

	return 0;
}

static bool nct7363_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case NCT7363_REG_LSRS(0) ... NCT7363_REG_LSRS(15):
	case NCT7363_REG_FANINX_HVAL(0) ... NCT7363_REG_FANINX_LVAL(15):
	case NCT7363_REG_FANINX_HL(0) ... NCT7363_REG_FANINX_LL(15):
	case NCT7363_REG_FSCPXDUTY(0) ... NCT7363_REG_FSCPXDIV(15):
		return true;
	default:
		return false;
	}
}

static const struct regmap_config nct7363_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.use_single_read = true,
	.use_single_write = true,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = nct7363_regmap_is_volatile,
};

static int nct7363_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct nct7363_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &nct7363_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	for_each_child_of_node(dev->of_node, child) {
		ret = nct7363_present_pwm_fanin(dev, child, data);
		if (ret) {
			of_node_put(child);
			return ret;
		}
	}

	/* Initialize the chip */
	ret = nct7363_init_chip(data);
	if (ret)
		return ret;

	hwmon_dev =
		devm_hwmon_device_register_with_info(dev, client->name, data,
						     &nct7363_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct i2c_driver nct7363_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "nct7363",
		.of_match_table = nct7363_of_match,
	},
	.probe = nct7363_probe,
};

module_i2c_driver(nct7363_driver);

MODULE_AUTHOR("CW Ho <cwho@nuvoton.com>");
MODULE_AUTHOR("Ban Feng <kcfeng0@nuvoton.com>");
MODULE_DESCRIPTION("NCT7363 Hardware Monitoring Driver");
MODULE_LICENSE("GPL");

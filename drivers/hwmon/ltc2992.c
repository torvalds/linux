// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * LTC2992 - Dual Wide Range Power Monitor
 *
 * Copyright 2020 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define LTC2992_CTRLB			0x01
#define LTC2992_FAULT1			0x03
#define LTC2992_POWER1			0x05
#define LTC2992_POWER1_MAX		0x08
#define LTC2992_POWER1_MIN		0x0B
#define LTC2992_POWER1_MAX_THRESH	0x0E
#define LTC2992_POWER1_MIN_THRESH	0x11
#define LTC2992_DSENSE1			0x14
#define LTC2992_DSENSE1_MAX		0x16
#define LTC2992_DSENSE1_MIN		0x18
#define LTC2992_DSENSE1_MAX_THRESH	0x1A
#define LTC2992_DSENSE1_MIN_THRESH	0x1C
#define LTC2992_SENSE1			0x1E
#define LTC2992_SENSE1_MAX		0x20
#define LTC2992_SENSE1_MIN		0x22
#define LTC2992_SENSE1_MAX_THRESH	0x24
#define LTC2992_SENSE1_MIN_THRESH	0x26
#define LTC2992_G1			0x28
#define LTC2992_G1_MAX			0x2A
#define LTC2992_G1_MIN			0x2C
#define LTC2992_G1_MAX_THRESH		0x2E
#define LTC2992_G1_MIN_THRESH		0x30
#define LTC2992_FAULT2			0x35
#define LTC2992_G2			0x5A
#define LTC2992_G2_MAX			0x5C
#define LTC2992_G2_MIN			0x5E
#define LTC2992_G2_MAX_THRESH		0x60
#define LTC2992_G2_MIN_THRESH		0x62
#define LTC2992_G3			0x64
#define LTC2992_G3_MAX			0x66
#define LTC2992_G3_MIN			0x68
#define LTC2992_G3_MAX_THRESH		0x6A
#define LTC2992_G3_MIN_THRESH		0x6C
#define LTC2992_G4			0x6E
#define LTC2992_G4_MAX			0x70
#define LTC2992_G4_MIN			0x72
#define LTC2992_G4_MAX_THRESH		0x74
#define LTC2992_G4_MIN_THRESH		0x76
#define LTC2992_FAULT3			0x92
#define LTC2992_GPIO_STATUS		0x95
#define LTC2992_GPIO_IO_CTRL		0x96
#define LTC2992_GPIO_CTRL		0x97

#define LTC2992_POWER(x)		(LTC2992_POWER1 + ((x) * 0x32))
#define LTC2992_POWER_MAX(x)		(LTC2992_POWER1_MAX + ((x) * 0x32))
#define LTC2992_POWER_MIN(x)		(LTC2992_POWER1_MIN + ((x) * 0x32))
#define LTC2992_POWER_MAX_THRESH(x)	(LTC2992_POWER1_MAX_THRESH + ((x) * 0x32))
#define LTC2992_POWER_MIN_THRESH(x)	(LTC2992_POWER1_MIN_THRESH + ((x) * 0x32))
#define LTC2992_DSENSE(x)		(LTC2992_DSENSE1 + ((x) * 0x32))
#define LTC2992_DSENSE_MAX(x)		(LTC2992_DSENSE1_MAX + ((x) * 0x32))
#define LTC2992_DSENSE_MIN(x)		(LTC2992_DSENSE1_MIN + ((x) * 0x32))
#define LTC2992_DSENSE_MAX_THRESH(x)	(LTC2992_DSENSE1_MAX_THRESH + ((x) * 0x32))
#define LTC2992_DSENSE_MIN_THRESH(x)	(LTC2992_DSENSE1_MIN_THRESH + ((x) * 0x32))
#define LTC2992_SENSE(x)		(LTC2992_SENSE1 + ((x) * 0x32))
#define LTC2992_SENSE_MAX(x)		(LTC2992_SENSE1_MAX + ((x) * 0x32))
#define LTC2992_SENSE_MIN(x)		(LTC2992_SENSE1_MIN + ((x) * 0x32))
#define LTC2992_SENSE_MAX_THRESH(x)	(LTC2992_SENSE1_MAX_THRESH + ((x) * 0x32))
#define LTC2992_SENSE_MIN_THRESH(x)	(LTC2992_SENSE1_MIN_THRESH + ((x) * 0x32))
#define LTC2992_POWER_FAULT(x)		(LTC2992_FAULT1 + ((x) * 0x32))
#define LTC2992_SENSE_FAULT(x)		(LTC2992_FAULT1 + ((x) * 0x32))
#define LTC2992_DSENSE_FAULT(x)		(LTC2992_FAULT1 + ((x) * 0x32))

/* CTRLB register bitfields */
#define LTC2992_RESET_HISTORY		BIT(3)

/* FAULT1 FAULT2 registers common bitfields */
#define LTC2992_POWER_FAULT_MSK(x)	(BIT(6) << (x))
#define LTC2992_DSENSE_FAULT_MSK(x)	(BIT(4) << (x))
#define LTC2992_SENSE_FAULT_MSK(x)	(BIT(2) << (x))

/* FAULT1 bitfields */
#define LTC2992_GPIO1_FAULT_MSK(x)	(BIT(0) << (x))

/* FAULT2 bitfields */
#define LTC2992_GPIO2_FAULT_MSK(x)	(BIT(0) << (x))

/* FAULT3 bitfields */
#define LTC2992_GPIO3_FAULT_MSK(x)	(BIT(6) << (x))
#define LTC2992_GPIO4_FAULT_MSK(x)	(BIT(4) << (x))

#define LTC2992_IADC_NANOV_LSB		12500
#define LTC2992_VADC_UV_LSB		25000
#define LTC2992_VADC_GPIO_UV_LSB	500

#define LTC2992_GPIO_NR		4
#define LTC2992_GPIO1_BIT	7
#define LTC2992_GPIO2_BIT	6
#define LTC2992_GPIO3_BIT	0
#define LTC2992_GPIO4_BIT	6
#define LTC2992_GPIO_BIT(x)	(LTC2992_GPIO_NR - (x) - 1)

struct ltc2992_state {
	struct i2c_client		*client;
	struct gpio_chip		gc;
	struct mutex			gpio_mutex; /* lock for gpio access */
	const char			*gpio_names[LTC2992_GPIO_NR];
	struct regmap			*regmap;
	u32				r_sense_uohm[2];
};

struct ltc2992_gpio_regs {
	u8	data;
	u8	max;
	u8	min;
	u8	max_thresh;
	u8	min_thresh;
	u8	alarm;
	u8	min_alarm_msk;
	u8	max_alarm_msk;
	u8	ctrl;
	u8	ctrl_bit;
};

static const struct ltc2992_gpio_regs ltc2992_gpio_addr_map[] = {
	{
		.data = LTC2992_G1,
		.max = LTC2992_G1_MAX,
		.min = LTC2992_G1_MIN,
		.max_thresh = LTC2992_G1_MAX_THRESH,
		.min_thresh = LTC2992_G1_MIN_THRESH,
		.alarm = LTC2992_FAULT1,
		.min_alarm_msk = LTC2992_GPIO1_FAULT_MSK(0),
		.max_alarm_msk = LTC2992_GPIO1_FAULT_MSK(1),
		.ctrl = LTC2992_GPIO_IO_CTRL,
		.ctrl_bit = LTC2992_GPIO1_BIT,
	},
	{
		.data = LTC2992_G2,
		.max = LTC2992_G2_MAX,
		.min = LTC2992_G2_MIN,
		.max_thresh = LTC2992_G2_MAX_THRESH,
		.min_thresh = LTC2992_G2_MIN_THRESH,
		.alarm = LTC2992_FAULT2,
		.min_alarm_msk = LTC2992_GPIO2_FAULT_MSK(0),
		.max_alarm_msk = LTC2992_GPIO2_FAULT_MSK(1),
		.ctrl = LTC2992_GPIO_IO_CTRL,
		.ctrl_bit = LTC2992_GPIO2_BIT,
	},
	{
		.data = LTC2992_G3,
		.max = LTC2992_G3_MAX,
		.min = LTC2992_G3_MIN,
		.max_thresh = LTC2992_G3_MAX_THRESH,
		.min_thresh = LTC2992_G3_MIN_THRESH,
		.alarm = LTC2992_FAULT3,
		.min_alarm_msk = LTC2992_GPIO3_FAULT_MSK(0),
		.max_alarm_msk = LTC2992_GPIO3_FAULT_MSK(1),
		.ctrl = LTC2992_GPIO_IO_CTRL,
		.ctrl_bit = LTC2992_GPIO3_BIT,
	},
	{
		.data = LTC2992_G4,
		.max = LTC2992_G4_MAX,
		.min = LTC2992_G4_MIN,
		.max_thresh = LTC2992_G4_MAX_THRESH,
		.min_thresh = LTC2992_G4_MIN_THRESH,
		.alarm = LTC2992_FAULT3,
		.min_alarm_msk = LTC2992_GPIO4_FAULT_MSK(0),
		.max_alarm_msk = LTC2992_GPIO4_FAULT_MSK(1),
		.ctrl = LTC2992_GPIO_CTRL,
		.ctrl_bit = LTC2992_GPIO4_BIT,
	},
};

static const char *ltc2992_gpio_names[LTC2992_GPIO_NR] = {
	"GPIO1", "GPIO2", "GPIO3", "GPIO4",
};

static int ltc2992_read_reg(struct ltc2992_state *st, u8 addr, const u8 reg_len)
{
	u8 regvals[4];
	int val;
	int ret;
	int i;

	ret = regmap_bulk_read(st->regmap, addr, regvals, reg_len);
	if (ret < 0)
		return ret;

	val = 0;
	for (i = 0; i < reg_len; i++)
		val |= regvals[reg_len - i - 1] << (i * 8);

	return val;
}

static int ltc2992_write_reg(struct ltc2992_state *st, u8 addr, const u8 reg_len, u32 val)
{
	u8 regvals[4];
	int i;

	for (i = 0; i < reg_len; i++)
		regvals[reg_len - i - 1] = (val >> (i * 8)) & 0xFF;

	return regmap_bulk_write(st->regmap, addr, regvals, reg_len);
}

static int ltc2992_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ltc2992_state *st = gpiochip_get_data(chip);
	unsigned long gpio_status;
	int reg;

	mutex_lock(&st->gpio_mutex);
	reg = ltc2992_read_reg(st, LTC2992_GPIO_STATUS, 1);
	mutex_unlock(&st->gpio_mutex);

	if (reg < 0)
		return reg;

	gpio_status = reg;

	return !test_bit(LTC2992_GPIO_BIT(offset), &gpio_status);
}

static int ltc2992_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				     unsigned long *bits)
{
	struct ltc2992_state *st = gpiochip_get_data(chip);
	unsigned long gpio_status;
	unsigned int gpio_nr;
	int reg;

	mutex_lock(&st->gpio_mutex);
	reg = ltc2992_read_reg(st, LTC2992_GPIO_STATUS, 1);
	mutex_unlock(&st->gpio_mutex);

	if (reg < 0)
		return reg;

	gpio_status = reg;

	for_each_set_bit(gpio_nr, mask, LTC2992_GPIO_NR) {
		if (test_bit(LTC2992_GPIO_BIT(gpio_nr), &gpio_status))
			set_bit(gpio_nr, bits);
	}

	return 0;
}

static void ltc2992_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct ltc2992_state *st = gpiochip_get_data(chip);
	unsigned long gpio_ctrl;
	int reg;

	mutex_lock(&st->gpio_mutex);
	reg = ltc2992_read_reg(st, ltc2992_gpio_addr_map[offset].ctrl, 1);
	if (reg < 0) {
		mutex_unlock(&st->gpio_mutex);
		return;
	}

	gpio_ctrl = reg;
	assign_bit(ltc2992_gpio_addr_map[offset].ctrl_bit, &gpio_ctrl, value);

	ltc2992_write_reg(st, ltc2992_gpio_addr_map[offset].ctrl, 1, gpio_ctrl);
	mutex_unlock(&st->gpio_mutex);
}

static void ltc2992_gpio_set_multiple(struct gpio_chip *chip, unsigned long *mask,
				      unsigned long *bits)
{
	struct ltc2992_state *st = gpiochip_get_data(chip);
	unsigned long gpio_ctrl_io = 0;
	unsigned long gpio_ctrl = 0;
	unsigned int gpio_nr;

	for_each_set_bit(gpio_nr, mask, LTC2992_GPIO_NR) {
		if (gpio_nr < 3)
			assign_bit(ltc2992_gpio_addr_map[gpio_nr].ctrl_bit, &gpio_ctrl_io, true);

		if (gpio_nr == 3)
			assign_bit(ltc2992_gpio_addr_map[gpio_nr].ctrl_bit, &gpio_ctrl, true);
	}

	mutex_lock(&st->gpio_mutex);
	ltc2992_write_reg(st, LTC2992_GPIO_IO_CTRL, 1, gpio_ctrl_io);
	ltc2992_write_reg(st, LTC2992_GPIO_CTRL, 1, gpio_ctrl);
	mutex_unlock(&st->gpio_mutex);
}

static int ltc2992_config_gpio(struct ltc2992_state *st)
{
	const char *name = dev_name(&st->client->dev);
	char *gpio_name;
	int ret;
	int i;

	ret = ltc2992_write_reg(st, LTC2992_GPIO_IO_CTRL, 1, 0);
	if (ret < 0)
		return ret;

	mutex_init(&st->gpio_mutex);

	for (i = 0; i < ARRAY_SIZE(st->gpio_names); i++) {
		gpio_name = devm_kasprintf(&st->client->dev, GFP_KERNEL, "ltc2992-%x-%s",
					   st->client->addr, ltc2992_gpio_names[i]);
		if (!gpio_name)
			return -ENOMEM;

		st->gpio_names[i] = gpio_name;
	}

	st->gc.label = name;
	st->gc.parent = &st->client->dev;
	st->gc.owner = THIS_MODULE;
	st->gc.base = -1;
	st->gc.names = st->gpio_names;
	st->gc.ngpio = ARRAY_SIZE(st->gpio_names);
	st->gc.get = ltc2992_gpio_get;
	st->gc.get_multiple = ltc2992_gpio_get_multiple;
	st->gc.set = ltc2992_gpio_set;
	st->gc.set_multiple = ltc2992_gpio_set_multiple;

	ret = devm_gpiochip_add_data(&st->client->dev, &st->gc, st);
	if (ret)
		dev_err(&st->client->dev, "GPIO registering failed (%d)\n", ret);

	return ret;
}

static umode_t ltc2992_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr,
				  int channel)
{
	const struct ltc2992_state *st = data;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_in_reset_history:
			return 0200;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_lowest:
		case hwmon_in_highest:
		case hwmon_in_min_alarm:
		case hwmon_in_max_alarm:
			return 0444;
		case hwmon_in_min:
		case hwmon_in_max:
			return 0644;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_lowest:
		case hwmon_curr_highest:
		case hwmon_curr_min_alarm:
		case hwmon_curr_max_alarm:
			if (st->r_sense_uohm[channel])
				return 0444;
			break;
		case hwmon_curr_min:
		case hwmon_curr_max:
			if (st->r_sense_uohm[channel])
				return 0644;
			break;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
		case hwmon_power_input_lowest:
		case hwmon_power_input_highest:
		case hwmon_power_min_alarm:
		case hwmon_power_max_alarm:
			if (st->r_sense_uohm[channel])
				return 0444;
			break;
		case hwmon_power_min:
		case hwmon_power_max:
			if (st->r_sense_uohm[channel])
				return 0644;
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int ltc2992_get_voltage(struct ltc2992_state *st, u32 reg, u32 scale, long *val)
{
	int reg_val;

	reg_val = ltc2992_read_reg(st, reg, 2);
	if (reg_val < 0)
		return reg_val;

	reg_val = reg_val >> 4;
	*val = DIV_ROUND_CLOSEST(reg_val * scale, 1000);

	return 0;
}

static int ltc2992_set_voltage(struct ltc2992_state *st, u32 reg, u32 scale, long val)
{
	val = DIV_ROUND_CLOSEST(val * 1000, scale);
	val = val << 4;

	return ltc2992_write_reg(st, reg, 2, val);
}

static int ltc2992_read_gpio_alarm(struct ltc2992_state *st, int nr_gpio, u32 attr, long *val)
{
	int reg_val;
	u32 mask;

	if (attr == hwmon_in_max_alarm)
		mask = ltc2992_gpio_addr_map[nr_gpio].max_alarm_msk;
	else
		mask = ltc2992_gpio_addr_map[nr_gpio].min_alarm_msk;

	reg_val = ltc2992_read_reg(st, ltc2992_gpio_addr_map[nr_gpio].alarm, 1);
	if (reg_val < 0)
		return reg_val;

	*val = !!(reg_val & mask);
	reg_val &= ~mask;

	return ltc2992_write_reg(st, ltc2992_gpio_addr_map[nr_gpio].alarm, 1, reg_val);
}

static int ltc2992_read_gpios_in(struct device *dev, u32 attr, int nr_gpio, long *val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_in_input:
		reg = ltc2992_gpio_addr_map[nr_gpio].data;
		break;
	case hwmon_in_lowest:
		reg = ltc2992_gpio_addr_map[nr_gpio].min;
		break;
	case hwmon_in_highest:
		reg = ltc2992_gpio_addr_map[nr_gpio].max;
		break;
	case hwmon_in_min:
		reg = ltc2992_gpio_addr_map[nr_gpio].min_thresh;
		break;
	case hwmon_in_max:
		reg = ltc2992_gpio_addr_map[nr_gpio].max_thresh;
		break;
	case hwmon_in_min_alarm:
	case hwmon_in_max_alarm:
		return ltc2992_read_gpio_alarm(st, nr_gpio, attr, val);
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_get_voltage(st, reg, LTC2992_VADC_GPIO_UV_LSB, val);
}

static int ltc2992_read_in_alarm(struct ltc2992_state *st, int channel, long *val, u32 attr)
{
	int reg_val;
	u32 mask;

	if (attr == hwmon_in_max_alarm)
		mask = LTC2992_SENSE_FAULT_MSK(1);
	else
		mask = LTC2992_SENSE_FAULT_MSK(0);

	reg_val = ltc2992_read_reg(st, LTC2992_SENSE_FAULT(channel), 1);
	if (reg_val < 0)
		return reg_val;

	*val = !!(reg_val & mask);
	reg_val &= ~mask;

	return ltc2992_write_reg(st, LTC2992_SENSE_FAULT(channel), 1, reg_val);
}

static int ltc2992_read_in(struct device *dev, u32 attr, int channel, long *val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	if (channel > 1)
		return ltc2992_read_gpios_in(dev, attr, channel - 2, val);

	switch (attr) {
	case hwmon_in_input:
		reg = LTC2992_SENSE(channel);
		break;
	case hwmon_in_lowest:
		reg = LTC2992_SENSE_MIN(channel);
		break;
	case hwmon_in_highest:
		reg = LTC2992_SENSE_MAX(channel);
		break;
	case hwmon_in_min:
		reg = LTC2992_SENSE_MIN_THRESH(channel);
		break;
	case hwmon_in_max:
		reg = LTC2992_SENSE_MAX_THRESH(channel);
		break;
	case hwmon_in_min_alarm:
	case hwmon_in_max_alarm:
		return ltc2992_read_in_alarm(st, channel, val, attr);
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_get_voltage(st, reg, LTC2992_VADC_UV_LSB, val);
}

static int ltc2992_get_current(struct ltc2992_state *st, u32 reg, u32 channel, long *val)
{
	int reg_val;

	reg_val = ltc2992_read_reg(st, reg, 2);
	if (reg_val < 0)
		return reg_val;

	reg_val = reg_val >> 4;
	*val = DIV_ROUND_CLOSEST(reg_val * LTC2992_IADC_NANOV_LSB, st->r_sense_uohm[channel]);

	return 0;
}

static int ltc2992_set_current(struct ltc2992_state *st, u32 reg, u32 channel, long val)
{
	u32 reg_val;

	reg_val = DIV_ROUND_CLOSEST(val * st->r_sense_uohm[channel], LTC2992_IADC_NANOV_LSB);
	reg_val = reg_val << 4;

	return ltc2992_write_reg(st, reg, 2, reg_val);
}

static int ltc2992_read_curr_alarm(struct ltc2992_state *st, int channel, long *val, u32 attr)
{
	int reg_val;
	u32 mask;

	if (attr == hwmon_curr_max_alarm)
		mask = LTC2992_DSENSE_FAULT_MSK(1);
	else
		mask = LTC2992_DSENSE_FAULT_MSK(0);

	reg_val = ltc2992_read_reg(st, LTC2992_DSENSE_FAULT(channel), 1);
	if (reg_val < 0)
		return reg_val;

	*val = !!(reg_val & mask);

	reg_val &= ~mask;
	return ltc2992_write_reg(st, LTC2992_DSENSE_FAULT(channel), 1, reg_val);
}

static int ltc2992_read_curr(struct device *dev, u32 attr, int channel, long *val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_curr_input:
		reg = LTC2992_DSENSE(channel);
		break;
	case hwmon_curr_lowest:
		reg = LTC2992_DSENSE_MIN(channel);
		break;
	case hwmon_curr_highest:
		reg = LTC2992_DSENSE_MAX(channel);
		break;
	case hwmon_curr_min:
		reg = LTC2992_DSENSE_MIN_THRESH(channel);
		break;
	case hwmon_curr_max:
		reg = LTC2992_DSENSE_MAX_THRESH(channel);
		break;
	case hwmon_curr_min_alarm:
	case hwmon_curr_max_alarm:
		return ltc2992_read_curr_alarm(st, channel, val, attr);
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_get_current(st, reg, channel, val);
}

static int ltc2992_get_power(struct ltc2992_state *st, u32 reg, u32 channel, long *val)
{
	int reg_val;

	reg_val = ltc2992_read_reg(st, reg, 3);
	if (reg_val < 0)
		return reg_val;

	*val = mul_u64_u32_div(reg_val, LTC2992_VADC_UV_LSB * LTC2992_IADC_NANOV_LSB,
			       st->r_sense_uohm[channel] * 1000);

	return 0;
}

static int ltc2992_set_power(struct ltc2992_state *st, u32 reg, u32 channel, long val)
{
	u32 reg_val;

	reg_val = mul_u64_u32_div(val, st->r_sense_uohm[channel] * 1000,
				  LTC2992_VADC_UV_LSB * LTC2992_IADC_NANOV_LSB);

	return ltc2992_write_reg(st, reg, 3, reg_val);
}

static int ltc2992_read_power_alarm(struct ltc2992_state *st, int channel, long *val, u32 attr)
{
	int reg_val;
	u32 mask;

	if (attr == hwmon_power_max_alarm)
		mask = LTC2992_POWER_FAULT_MSK(1);
	else
		mask = LTC2992_POWER_FAULT_MSK(0);

	reg_val = ltc2992_read_reg(st, LTC2992_POWER_FAULT(channel), 1);
	if (reg_val < 0)
		return reg_val;

	*val = !!(reg_val & mask);
	reg_val &= ~mask;

	return ltc2992_write_reg(st, LTC2992_POWER_FAULT(channel), 1, reg_val);
}

static int ltc2992_read_power(struct device *dev, u32 attr, int channel, long *val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_power_input:
		reg = LTC2992_POWER(channel);
		break;
	case hwmon_power_input_lowest:
		reg = LTC2992_POWER_MIN(channel);
		break;
	case hwmon_power_input_highest:
		reg = LTC2992_POWER_MAX(channel);
		break;
	case hwmon_power_min:
		reg = LTC2992_POWER_MIN_THRESH(channel);
		break;
	case hwmon_power_max:
		reg = LTC2992_POWER_MAX_THRESH(channel);
		break;
	case hwmon_power_min_alarm:
	case hwmon_power_max_alarm:
		return ltc2992_read_power_alarm(st, channel, val, attr);
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_get_power(st, reg, channel, val);
}

static int ltc2992_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			long *val)
{
	switch (type) {
	case hwmon_in:
		return ltc2992_read_in(dev, attr, channel, val);
	case hwmon_curr:
		return ltc2992_read_curr(dev, attr, channel, val);
	case hwmon_power:
		return ltc2992_read_power(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc2992_write_curr(struct device *dev, u32 attr, int channel, long val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_curr_min:
		reg = LTC2992_DSENSE_MIN_THRESH(channel);
		break;
	case hwmon_curr_max:
		reg = LTC2992_DSENSE_MAX_THRESH(channel);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_set_current(st, reg, channel, val);
}

static int ltc2992_write_gpios_in(struct device *dev, u32 attr, int nr_gpio, long val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_in_min:
		reg = ltc2992_gpio_addr_map[nr_gpio].min_thresh;
		break;
	case hwmon_in_max:
		reg = ltc2992_gpio_addr_map[nr_gpio].max_thresh;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_set_voltage(st, reg, LTC2992_VADC_GPIO_UV_LSB, val);
}

static int ltc2992_write_in(struct device *dev, u32 attr, int channel, long val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	if (channel > 1)
		return ltc2992_write_gpios_in(dev, attr, channel - 2, val);

	switch (attr) {
	case hwmon_in_min:
		reg = LTC2992_SENSE_MIN_THRESH(channel);
		break;
	case hwmon_in_max:
		reg = LTC2992_SENSE_MAX_THRESH(channel);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_set_voltage(st, reg, LTC2992_VADC_UV_LSB, val);
}

static int ltc2992_write_power(struct device *dev, u32 attr, int channel, long val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);
	u32 reg;

	switch (attr) {
	case hwmon_power_min:
		reg = LTC2992_POWER_MIN_THRESH(channel);
		break;
	case hwmon_power_max:
		reg = LTC2992_POWER_MAX_THRESH(channel);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ltc2992_set_power(st, reg, channel, val);
}

static int ltc2992_write_chip(struct device *dev, u32 attr, int channel, long val)
{
	struct ltc2992_state *st = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_chip_in_reset_history:
		return regmap_update_bits(st->regmap, LTC2992_CTRLB, LTC2992_RESET_HISTORY,
					  LTC2992_RESET_HISTORY);
	default:
		return -EOPNOTSUPP;
	}
}

static int ltc2992_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			 long val)
{
	switch (type) {
	case hwmon_chip:
		return ltc2992_write_chip(dev, attr, channel, val);
	case hwmon_in:
		return ltc2992_write_in(dev, attr, channel, val);
	case hwmon_curr:
		return ltc2992_write_curr(dev, attr, channel, val);
	case hwmon_power:
		return ltc2992_write_power(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops ltc2992_hwmon_ops = {
	.is_visible = ltc2992_is_visible,
	.read = ltc2992_read,
	.write = ltc2992_write,
};

static const struct hwmon_channel_info *ltc2992_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_IN_RESET_HISTORY),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST | HWMON_I_MIN |
			   HWMON_I_MAX | HWMON_I_MIN_ALARM | HWMON_I_MAX_ALARM,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST | HWMON_I_MIN |
			   HWMON_I_MAX | HWMON_I_MIN_ALARM | HWMON_I_MAX_ALARM,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST | HWMON_I_MIN |
			   HWMON_I_MAX | HWMON_I_MIN_ALARM | HWMON_I_MAX_ALARM,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST | HWMON_I_MIN |
			   HWMON_I_MAX | HWMON_I_MIN_ALARM | HWMON_I_MAX_ALARM,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST | HWMON_I_MIN |
			   HWMON_I_MAX | HWMON_I_MIN_ALARM | HWMON_I_MAX_ALARM,
			   HWMON_I_INPUT | HWMON_I_LOWEST | HWMON_I_HIGHEST | HWMON_I_MIN |
			   HWMON_I_MAX | HWMON_I_MIN_ALARM | HWMON_I_MAX_ALARM),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LOWEST | HWMON_C_HIGHEST | HWMON_C_MIN |
			   HWMON_C_MAX | HWMON_C_MIN_ALARM | HWMON_C_MAX_ALARM,
			   HWMON_C_INPUT | HWMON_C_LOWEST | HWMON_C_HIGHEST | HWMON_C_MIN |
			   HWMON_C_MAX | HWMON_C_MIN_ALARM | HWMON_C_MAX_ALARM),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_INPUT_LOWEST | HWMON_P_INPUT_HIGHEST |
			   HWMON_P_MIN | HWMON_P_MAX | HWMON_P_MIN_ALARM | HWMON_P_MAX_ALARM,
			   HWMON_P_INPUT | HWMON_P_INPUT_LOWEST | HWMON_P_INPUT_HIGHEST |
			   HWMON_P_MIN | HWMON_P_MAX | HWMON_P_MIN_ALARM | HWMON_P_MAX_ALARM),
	NULL
};

static const struct hwmon_chip_info ltc2992_chip_info = {
	.ops = &ltc2992_hwmon_ops,
	.info = ltc2992_info,
};

static const struct regmap_config ltc2992_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xE8,
};

static int ltc2992_parse_dt(struct ltc2992_state *st)
{
	struct fwnode_handle *fwnode;
	struct fwnode_handle *child;
	u32 addr;
	u32 val;
	int ret;

	fwnode = dev_fwnode(&st->client->dev);

	fwnode_for_each_available_child_node(fwnode, child) {
		ret = fwnode_property_read_u32(child, "reg", &addr);
		if (ret < 0) {
			fwnode_handle_put(child);
			return ret;
		}

		if (addr > 1) {
			fwnode_handle_put(child);
			return -EINVAL;
		}

		ret = fwnode_property_read_u32(child, "shunt-resistor-micro-ohms", &val);
		if (!ret)
			st->r_sense_uohm[addr] = val;
	}

	return 0;
}

static int ltc2992_i2c_probe(struct i2c_client *client)
{
	struct device *hwmon_dev;
	struct ltc2992_state *st;
	int ret;

	st = devm_kzalloc(&client->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->client = client;
	st->regmap = devm_regmap_init_i2c(client, &ltc2992_regmap_config);
	if (IS_ERR(st->regmap))
		return PTR_ERR(st->regmap);

	ret = ltc2992_parse_dt(st);
	if (ret < 0)
		return ret;

	ret = ltc2992_config_gpio(st);
	if (ret < 0)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(&client->dev, client->name, st,
							 &ltc2992_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id ltc2992_of_match[] = {
	{ .compatible = "adi,ltc2992" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2992_of_match);

static const struct i2c_device_id ltc2992_i2c_id[] = {
	{"ltc2992", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2992_i2c_id);

static struct i2c_driver ltc2992_i2c_driver = {
	.driver = {
		.name = "ltc2992",
		.of_match_table = ltc2992_of_match,
	},
	.probe_new = ltc2992_i2c_probe,
	.id_table = ltc2992_i2c_id,
};

module_i2c_driver(ltc2992_i2c_driver);

MODULE_AUTHOR("Alexandru Tachici <alexandru.tachici@analog.com>");
MODULE_DESCRIPTION("Hwmon driver for Linear Technology 2992");
MODULE_LICENSE("Dual BSD/GPL");

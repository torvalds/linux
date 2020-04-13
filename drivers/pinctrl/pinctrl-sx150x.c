// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, BayLibre, SAS. All rights reserved.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * Driver for Semtech SX150X I2C GPIO Expanders
 * The handling of the 4-bit chips (SX1501/SX1504/SX1507) is untested.
 *
 * Author: Gregory Bean <gbean@codeaurora.org>
 */

#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

/* The chip models of sx150x */
enum {
	SX150X_123 = 0,
	SX150X_456,
	SX150X_789,
};
enum {
	SX150X_789_REG_MISC_AUTOCLEAR_OFF = 1 << 0,
	SX150X_MAX_REGISTER = 0xad,
	SX150X_IRQ_TYPE_EDGE_RISING = 0x1,
	SX150X_IRQ_TYPE_EDGE_FALLING = 0x2,
	SX150X_789_RESET_KEY1 = 0x12,
	SX150X_789_RESET_KEY2 = 0x34,
};

struct sx150x_123_pri {
	u8 reg_pld_mode;
	u8 reg_pld_table0;
	u8 reg_pld_table1;
	u8 reg_pld_table2;
	u8 reg_pld_table3;
	u8 reg_pld_table4;
	u8 reg_advanced;
};

struct sx150x_456_pri {
	u8 reg_pld_mode;
	u8 reg_pld_table0;
	u8 reg_pld_table1;
	u8 reg_pld_table2;
	u8 reg_pld_table3;
	u8 reg_pld_table4;
	u8 reg_advanced;
};

struct sx150x_789_pri {
	u8 reg_drain;
	u8 reg_polarity;
	u8 reg_clock;
	u8 reg_misc;
	u8 reg_reset;
	u8 ngpios;
};

struct sx150x_device_data {
	u8 model;
	u8 reg_pullup;
	u8 reg_pulldn;
	u8 reg_dir;
	u8 reg_data;
	u8 reg_irq_mask;
	u8 reg_irq_src;
	u8 reg_sense;
	u8 ngpios;
	union {
		struct sx150x_123_pri x123;
		struct sx150x_456_pri x456;
		struct sx150x_789_pri x789;
	} pri;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
};

struct sx150x_pinctrl {
	struct device *dev;
	struct i2c_client *client;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc pinctrl_desc;
	struct gpio_chip gpio;
	struct irq_chip irq_chip;
	struct regmap *regmap;
	struct {
		u32 sense;
		u32 masked;
	} irq;
	struct mutex lock;
	const struct sx150x_device_data *data;
};

static const struct pinctrl_pin_desc sx150x_4_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "oscio"),
};

static const struct pinctrl_pin_desc sx150x_8_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "oscio"),
};

static const struct pinctrl_pin_desc sx150x_16_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "oscio"),
};

static const struct sx150x_device_data sx1501q_device_data = {
	.model = SX150X_123,
	.reg_pullup	= 0x02,
	.reg_pulldn	= 0x03,
	.reg_dir	= 0x01,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x05,
	.reg_irq_src	= 0x08,
	.reg_sense	= 0x07,
	.pri.x123 = {
		.reg_pld_mode	= 0x10,
		.reg_pld_table0	= 0x11,
		.reg_pld_table2	= 0x13,
		.reg_advanced	= 0xad,
	},
	.ngpios	= 4,
	.pins = sx150x_4_pins,
	.npins = 4, /* oscio not available */
};

static const struct sx150x_device_data sx1502q_device_data = {
	.model = SX150X_123,
	.reg_pullup	= 0x02,
	.reg_pulldn	= 0x03,
	.reg_dir	= 0x01,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x05,
	.reg_irq_src	= 0x08,
	.reg_sense	= 0x06,
	.pri.x123 = {
		.reg_pld_mode	= 0x10,
		.reg_pld_table0	= 0x11,
		.reg_pld_table1	= 0x12,
		.reg_pld_table2	= 0x13,
		.reg_pld_table3	= 0x14,
		.reg_pld_table4	= 0x15,
		.reg_advanced	= 0xad,
	},
	.ngpios	= 8,
	.pins = sx150x_8_pins,
	.npins = 8, /* oscio not available */
};

static const struct sx150x_device_data sx1503q_device_data = {
	.model = SX150X_123,
	.reg_pullup	= 0x04,
	.reg_pulldn	= 0x06,
	.reg_dir	= 0x02,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x08,
	.reg_irq_src	= 0x0e,
	.reg_sense	= 0x0a,
	.pri.x123 = {
		.reg_pld_mode	= 0x20,
		.reg_pld_table0	= 0x22,
		.reg_pld_table1	= 0x24,
		.reg_pld_table2	= 0x26,
		.reg_pld_table3	= 0x28,
		.reg_pld_table4	= 0x2a,
		.reg_advanced	= 0xad,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins  = 16, /* oscio not available */
};

static const struct sx150x_device_data sx1504q_device_data = {
	.model = SX150X_456,
	.reg_pullup	= 0x02,
	.reg_pulldn	= 0x03,
	.reg_dir	= 0x01,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x05,
	.reg_irq_src	= 0x08,
	.reg_sense	= 0x07,
	.pri.x456 = {
		.reg_pld_mode	= 0x10,
		.reg_pld_table0	= 0x11,
		.reg_pld_table2	= 0x13,
	},
	.ngpios	= 4,
	.pins = sx150x_4_pins,
	.npins = 4, /* oscio not available */
};

static const struct sx150x_device_data sx1505q_device_data = {
	.model = SX150X_456,
	.reg_pullup	= 0x02,
	.reg_pulldn	= 0x03,
	.reg_dir	= 0x01,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x05,
	.reg_irq_src	= 0x08,
	.reg_sense	= 0x06,
	.pri.x456 = {
		.reg_pld_mode	= 0x10,
		.reg_pld_table0	= 0x11,
		.reg_pld_table1	= 0x12,
		.reg_pld_table2	= 0x13,
		.reg_pld_table3	= 0x14,
		.reg_pld_table4	= 0x15,
	},
	.ngpios	= 8,
	.pins = sx150x_8_pins,
	.npins = 8, /* oscio not available */
};

static const struct sx150x_device_data sx1506q_device_data = {
	.model = SX150X_456,
	.reg_pullup	= 0x04,
	.reg_pulldn	= 0x06,
	.reg_dir	= 0x02,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x08,
	.reg_irq_src	= 0x0e,
	.reg_sense	= 0x0a,
	.pri.x456 = {
		.reg_pld_mode	= 0x20,
		.reg_pld_table0	= 0x22,
		.reg_pld_table1	= 0x24,
		.reg_pld_table2	= 0x26,
		.reg_pld_table3	= 0x28,
		.reg_pld_table4	= 0x2a,
		.reg_advanced	= 0xad,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins = 16, /* oscio not available */
};

static const struct sx150x_device_data sx1507q_device_data = {
	.model = SX150X_789,
	.reg_pullup	= 0x03,
	.reg_pulldn	= 0x04,
	.reg_dir	= 0x07,
	.reg_data	= 0x08,
	.reg_irq_mask	= 0x09,
	.reg_irq_src	= 0x0b,
	.reg_sense	= 0x0a,
	.pri.x789 = {
		.reg_drain	= 0x05,
		.reg_polarity	= 0x06,
		.reg_clock	= 0x0d,
		.reg_misc	= 0x0e,
		.reg_reset	= 0x7d,
	},
	.ngpios = 4,
	.pins = sx150x_4_pins,
	.npins = ARRAY_SIZE(sx150x_4_pins),
};

static const struct sx150x_device_data sx1508q_device_data = {
	.model = SX150X_789,
	.reg_pullup	= 0x03,
	.reg_pulldn	= 0x04,
	.reg_dir	= 0x07,
	.reg_data	= 0x08,
	.reg_irq_mask	= 0x09,
	.reg_irq_src	= 0x0c,
	.reg_sense	= 0x0a,
	.pri.x789 = {
		.reg_drain	= 0x05,
		.reg_polarity	= 0x06,
		.reg_clock	= 0x0f,
		.reg_misc	= 0x10,
		.reg_reset	= 0x7d,
	},
	.ngpios = 8,
	.pins = sx150x_8_pins,
	.npins = ARRAY_SIZE(sx150x_8_pins),
};

static const struct sx150x_device_data sx1509q_device_data = {
	.model = SX150X_789,
	.reg_pullup	= 0x06,
	.reg_pulldn	= 0x08,
	.reg_dir	= 0x0e,
	.reg_data	= 0x10,
	.reg_irq_mask	= 0x12,
	.reg_irq_src	= 0x18,
	.reg_sense	= 0x14,
	.pri.x789 = {
		.reg_drain	= 0x0a,
		.reg_polarity	= 0x0c,
		.reg_clock	= 0x1e,
		.reg_misc	= 0x1f,
		.reg_reset	= 0x7d,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins = ARRAY_SIZE(sx150x_16_pins),
};

static int sx150x_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *sx150x_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int group)
{
	return NULL;
}

static int sx150x_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int group,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	return -ENOTSUPP;
}

static const struct pinctrl_ops sx150x_pinctrl_ops = {
	.get_groups_count = sx150x_pinctrl_get_groups_count,
	.get_group_name = sx150x_pinctrl_get_group_name,
	.get_group_pins = sx150x_pinctrl_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
#endif
};

static bool sx150x_pin_is_oscio(struct sx150x_pinctrl *pctl, unsigned int pin)
{
	if (pin >= pctl->data->npins)
		return false;

	/* OSCIO pin is only present in 789 devices */
	if (pctl->data->model != SX150X_789)
		return false;

	return !strcmp(pctl->data->pins[pin].name, "oscio");
}

static int sx150x_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	unsigned int value;
	int ret;

	if (sx150x_pin_is_oscio(pctl, offset))
		return GPIO_LINE_DIRECTION_OUT;

	ret = regmap_read(pctl->regmap, pctl->data->reg_dir, &value);
	if (ret < 0)
		return ret;

	if (value & BIT(offset))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int sx150x_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	unsigned int value;
	int ret;

	if (sx150x_pin_is_oscio(pctl, offset))
		return -EINVAL;

	ret = regmap_read(pctl->regmap, pctl->data->reg_data, &value);
	if (ret < 0)
		return ret;

	return !!(value & BIT(offset));
}

static int __sx150x_gpio_set(struct sx150x_pinctrl *pctl, unsigned int offset,
			     int value)
{
	return regmap_write_bits(pctl->regmap, pctl->data->reg_data,
				 BIT(offset), value ? BIT(offset) : 0);
}

static int sx150x_gpio_oscio_set(struct sx150x_pinctrl *pctl,
				 int value)
{
	return regmap_write(pctl->regmap,
			    pctl->data->pri.x789.reg_clock,
			    (value ? 0x1f : 0x10));
}

static void sx150x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);

	if (sx150x_pin_is_oscio(pctl, offset))
		sx150x_gpio_oscio_set(pctl, value);
	else
		__sx150x_gpio_set(pctl, offset, value);

}

static void sx150x_gpio_set_multiple(struct gpio_chip *chip,
				     unsigned long *mask,
				     unsigned long *bits)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);

	regmap_write_bits(pctl->regmap, pctl->data->reg_data, *mask, *bits);
}

static int sx150x_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);

	if (sx150x_pin_is_oscio(pctl, offset))
		return -EINVAL;

	return regmap_write_bits(pctl->regmap,
				 pctl->data->reg_dir,
				 BIT(offset), BIT(offset));
}

static int sx150x_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int ret;

	if (sx150x_pin_is_oscio(pctl, offset))
		return sx150x_gpio_oscio_set(pctl, value);

	ret = __sx150x_gpio_set(pctl, offset, value);
	if (ret < 0)
		return ret;

	return regmap_write_bits(pctl->regmap,
				 pctl->data->reg_dir,
				 BIT(offset), 0);
}

static void sx150x_irq_mask(struct irq_data *d)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int n = d->hwirq;

	pctl->irq.masked |= BIT(n);
}

static void sx150x_irq_unmask(struct irq_data *d)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int n = d->hwirq;

	pctl->irq.masked &= ~BIT(n);
}

static void sx150x_irq_set_sense(struct sx150x_pinctrl *pctl,
				 unsigned int line, unsigned int sense)
{
	/*
	 * Every interrupt line is represented by two bits shifted
	 * proportionally to the line number
	 */
	const unsigned int n = line * 2;
	const unsigned int mask = ~((SX150X_IRQ_TYPE_EDGE_RISING |
				     SX150X_IRQ_TYPE_EDGE_FALLING) << n);

	pctl->irq.sense &= mask;
	pctl->irq.sense |= sense << n;
}

static int sx150x_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int n, val = 0;

	if (flow_type & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		return -EINVAL;

	n = d->hwirq;

	if (flow_type & IRQ_TYPE_EDGE_RISING)
		val |= SX150X_IRQ_TYPE_EDGE_RISING;
	if (flow_type & IRQ_TYPE_EDGE_FALLING)
		val |= SX150X_IRQ_TYPE_EDGE_FALLING;

	sx150x_irq_set_sense(pctl, n, val);
	return 0;
}

static irqreturn_t sx150x_irq_thread_fn(int irq, void *dev_id)
{
	struct sx150x_pinctrl *pctl = (struct sx150x_pinctrl *)dev_id;
	unsigned long n, status;
	unsigned int val;
	int err;

	err = regmap_read(pctl->regmap, pctl->data->reg_irq_src, &val);
	if (err < 0)
		return IRQ_NONE;

	err = regmap_write(pctl->regmap, pctl->data->reg_irq_src, val);
	if (err < 0)
		return IRQ_NONE;

	status = val;
	for_each_set_bit(n, &status, pctl->data->ngpios)
		handle_nested_irq(irq_find_mapping(pctl->gpio.irq.domain, n));

	return IRQ_HANDLED;
}

static void sx150x_irq_bus_lock(struct irq_data *d)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));

	mutex_lock(&pctl->lock);
}

static void sx150x_irq_bus_sync_unlock(struct irq_data *d)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));

	regmap_write(pctl->regmap, pctl->data->reg_irq_mask, pctl->irq.masked);
	regmap_write(pctl->regmap, pctl->data->reg_sense, pctl->irq.sense);
	mutex_unlock(&pctl->lock);
}

static int sx150x_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	int ret;
	u32 arg;
	unsigned int data;

	if (sx150x_pin_is_oscio(pctl, pin)) {
		switch (param) {
		case PIN_CONFIG_DRIVE_PUSH_PULL:
		case PIN_CONFIG_OUTPUT:
			ret = regmap_read(pctl->regmap,
					  pctl->data->pri.x789.reg_clock,
					  &data);
			if (ret < 0)
				return ret;

			if (param == PIN_CONFIG_DRIVE_PUSH_PULL)
				arg = (data & 0x1f) ? 1 : 0;
			else {
				if ((data & 0x1f) == 0x1f)
					arg = 1;
				else if ((data & 0x1f) == 0x10)
					arg = 0;
				else
					return -EINVAL;
			}

			break;
		default:
			return -ENOTSUPP;
		}

		goto out;
	}

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = regmap_read(pctl->regmap,
				  pctl->data->reg_pulldn,
				  &data);
		data &= BIT(pin);

		if (ret < 0)
			return ret;

		if (!ret)
			return -EINVAL;

		arg = 1;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		ret = regmap_read(pctl->regmap,
				  pctl->data->reg_pullup,
				  &data);
		data &= BIT(pin);

		if (ret < 0)
			return ret;

		if (!ret)
			return -EINVAL;

		arg = 1;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (pctl->data->model != SX150X_789)
			return -ENOTSUPP;

		ret = regmap_read(pctl->regmap,
				  pctl->data->pri.x789.reg_drain,
				  &data);
		data &= BIT(pin);

		if (ret < 0)
			return ret;

		if (!data)
			return -EINVAL;

		arg = 1;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (pctl->data->model != SX150X_789)
			arg = true;
		else {
			ret = regmap_read(pctl->regmap,
					  pctl->data->pri.x789.reg_drain,
					  &data);
			data &= BIT(pin);

			if (ret < 0)
				return ret;

			if (data)
				return -EINVAL;

			arg = 1;
		}
		break;

	case PIN_CONFIG_OUTPUT:
		ret = sx150x_gpio_get_direction(&pctl->gpio, pin);
		if (ret < 0)
			return ret;

		if (ret == GPIO_LINE_DIRECTION_IN)
			return -EINVAL;

		ret = sx150x_gpio_get(&pctl->gpio, pin);
		if (ret < 0)
			return ret;

		arg = ret;
		break;

	default:
		return -ENOTSUPP;
	}

out:
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int sx150x_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 arg;
	int i;
	int ret;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		if (sx150x_pin_is_oscio(pctl, pin)) {
			if (param == PIN_CONFIG_OUTPUT) {
				ret = sx150x_gpio_direction_output(&pctl->gpio,
								   pin, arg);
				if (ret < 0)
					return ret;

				continue;
			} else
				return -ENOTSUPP;
		}

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		case PIN_CONFIG_BIAS_DISABLE:
			ret = regmap_write_bits(pctl->regmap,
						pctl->data->reg_pulldn,
						BIT(pin), 0);
			if (ret < 0)
				return ret;

			ret = regmap_write_bits(pctl->regmap,
						pctl->data->reg_pullup,
						BIT(pin), 0);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = regmap_write_bits(pctl->regmap,
						pctl->data->reg_pullup,
						BIT(pin), BIT(pin));
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = regmap_write_bits(pctl->regmap,
						pctl->data->reg_pulldn,
						BIT(pin), BIT(pin));
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			if (pctl->data->model != SX150X_789 ||
			    sx150x_pin_is_oscio(pctl, pin))
				return -ENOTSUPP;

			ret = regmap_write_bits(pctl->regmap,
						pctl->data->pri.x789.reg_drain,
						BIT(pin), BIT(pin));
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			if (pctl->data->model != SX150X_789 ||
			    sx150x_pin_is_oscio(pctl, pin))
				return 0;

			ret = regmap_write_bits(pctl->regmap,
						pctl->data->pri.x789.reg_drain,
						BIT(pin), 0);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_OUTPUT:
			ret = sx150x_gpio_direction_output(&pctl->gpio,
							   pin, arg);
			if (ret < 0)
				return ret;

			break;

		default:
			return -ENOTSUPP;
		}
	} /* for each config */

	return 0;
}

static const struct pinconf_ops sx150x_pinconf_ops = {
	.pin_config_get = sx150x_pinconf_get,
	.pin_config_set = sx150x_pinconf_set,
	.is_generic = true,
};

static const struct i2c_device_id sx150x_id[] = {
	{"sx1501q", (kernel_ulong_t) &sx1501q_device_data },
	{"sx1502q", (kernel_ulong_t) &sx1502q_device_data },
	{"sx1503q", (kernel_ulong_t) &sx1503q_device_data },
	{"sx1504q", (kernel_ulong_t) &sx1504q_device_data },
	{"sx1505q", (kernel_ulong_t) &sx1505q_device_data },
	{"sx1506q", (kernel_ulong_t) &sx1506q_device_data },
	{"sx1507q", (kernel_ulong_t) &sx1507q_device_data },
	{"sx1508q", (kernel_ulong_t) &sx1508q_device_data },
	{"sx1509q", (kernel_ulong_t) &sx1509q_device_data },
	{}
};

static const struct of_device_id sx150x_of_match[] = {
	{ .compatible = "semtech,sx1501q", .data = &sx1501q_device_data },
	{ .compatible = "semtech,sx1502q", .data = &sx1502q_device_data },
	{ .compatible = "semtech,sx1503q", .data = &sx1503q_device_data },
	{ .compatible = "semtech,sx1504q", .data = &sx1504q_device_data },
	{ .compatible = "semtech,sx1505q", .data = &sx1505q_device_data },
	{ .compatible = "semtech,sx1506q", .data = &sx1506q_device_data },
	{ .compatible = "semtech,sx1507q", .data = &sx1507q_device_data },
	{ .compatible = "semtech,sx1508q", .data = &sx1508q_device_data },
	{ .compatible = "semtech,sx1509q", .data = &sx1509q_device_data },
	{},
};

static int sx150x_reset(struct sx150x_pinctrl *pctl)
{
	int err;

	err = i2c_smbus_write_byte_data(pctl->client,
					pctl->data->pri.x789.reg_reset,
					SX150X_789_RESET_KEY1);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(pctl->client,
					pctl->data->pri.x789.reg_reset,
					SX150X_789_RESET_KEY2);
	return err;
}

static int sx150x_init_misc(struct sx150x_pinctrl *pctl)
{
	u8 reg, value;

	switch (pctl->data->model) {
	case SX150X_789:
		reg   = pctl->data->pri.x789.reg_misc;
		value = SX150X_789_REG_MISC_AUTOCLEAR_OFF;
		break;
	case SX150X_456:
		reg   = pctl->data->pri.x456.reg_advanced;
		value = 0x00;

		/*
		 * Only SX1506 has RegAdvanced, SX1504/5 are expected
		 * to initialize this offset to zero
		 */
		if (!reg)
			return 0;
		break;
	case SX150X_123:
		reg   = pctl->data->pri.x123.reg_advanced;
		value = 0x00;
		break;
	default:
		WARN(1, "Unknown chip model %d\n", pctl->data->model);
		return -EINVAL;
	}

	return regmap_write(pctl->regmap, reg, value);
}

static int sx150x_init_hw(struct sx150x_pinctrl *pctl)
{
	const u8 reg[] = {
		[SX150X_789] = pctl->data->pri.x789.reg_polarity,
		[SX150X_456] = pctl->data->pri.x456.reg_pld_mode,
		[SX150X_123] = pctl->data->pri.x123.reg_pld_mode,
	};
	int err;

	if (pctl->data->model == SX150X_789 &&
	    of_property_read_bool(pctl->dev->of_node, "semtech,probe-reset")) {
		err = sx150x_reset(pctl);
		if (err < 0)
			return err;
	}

	err = sx150x_init_misc(pctl);
	if (err < 0)
		return err;

	/* Set all pins to work in normal mode */
	return regmap_write(pctl->regmap, reg[pctl->data->model], 0);
}

static int sx150x_regmap_reg_width(struct sx150x_pinctrl *pctl,
				   unsigned int reg)
{
	const struct sx150x_device_data *data = pctl->data;

	if (reg == data->reg_sense) {
		/*
		 * RegSense packs two bits of configuration per GPIO,
		 * so we'd need to read twice as many bits as there
		 * are GPIO in our chip
		 */
		return 2 * data->ngpios;
	} else if ((data->model == SX150X_789 &&
		    (reg == data->pri.x789.reg_misc ||
		     reg == data->pri.x789.reg_clock ||
		     reg == data->pri.x789.reg_reset))
		   ||
		   (data->model == SX150X_123 &&
		    reg == data->pri.x123.reg_advanced)
		   ||
		   (data->model == SX150X_456 &&
		    data->pri.x456.reg_advanced &&
		    reg == data->pri.x456.reg_advanced)) {
		return 8;
	} else {
		return data->ngpios;
	}
}

static unsigned int sx150x_maybe_swizzle(struct sx150x_pinctrl *pctl,
					 unsigned int reg, unsigned int val)
{
	unsigned int a, b;
	const struct sx150x_device_data *data = pctl->data;

	/*
	 * Whereas SX1509 presents RegSense in a simple layout as such:
	 *	reg     [ f f e e d d c c ]
	 *	reg + 1 [ b b a a 9 9 8 8 ]
	 *	reg + 2 [ 7 7 6 6 5 5 4 4 ]
	 *	reg + 3 [ 3 3 2 2 1 1 0 0 ]
	 *
	 * SX1503 and SX1506 deviate from that data layout, instead storing
	 * their contents as follows:
	 *
	 *	reg     [ f f e e d d c c ]
	 *	reg + 1 [ 7 7 6 6 5 5 4 4 ]
	 *	reg + 2 [ b b a a 9 9 8 8 ]
	 *	reg + 3 [ 3 3 2 2 1 1 0 0 ]
	 *
	 * so, taking that into account, we swap two
	 * inner bytes of a 4-byte result
	 */

	if (reg == data->reg_sense &&
	    data->ngpios == 16 &&
	    (data->model == SX150X_123 ||
	     data->model == SX150X_456)) {
		a = val & 0x00ff0000;
		b = val & 0x0000ff00;

		val &= 0xff0000ff;
		val |= b << 8;
		val |= a >> 8;
	}

	return val;
}

/*
 * In order to mask the differences between 16 and 8 bit expander
 * devices we set up a sligthly ficticious regmap that pretends to be
 * a set of 32-bit (to accomodate RegSenseLow/RegSenseHigh
 * pair/quartet) registers and transparently reconstructs those
 * registers via multiple I2C/SMBus reads
 *
 * This way the rest of the driver code, interfacing with the chip via
 * regmap API, can work assuming that each GPIO pin is represented by
 * a group of bits at an offset proportional to GPIO number within a
 * given register.
 */
static int sx150x_regmap_reg_read(void *context, unsigned int reg,
				  unsigned int *result)
{
	int ret, n;
	struct sx150x_pinctrl *pctl = context;
	struct i2c_client *i2c = pctl->client;
	const int width = sx150x_regmap_reg_width(pctl, reg);
	unsigned int idx, val;

	/*
	 * There are four potential cases covered by this function:
	 *
	 * 1) 8-pin chip, single configuration bit register
	 *
	 *	This is trivial the code below just needs to read:
	 *		reg  [ 7 6 5 4 3 2 1 0 ]
	 *
	 * 2) 8-pin chip, double configuration bit register (RegSense)
	 *
	 *	The read will be done as follows:
	 *		reg      [ 7 7 6 6 5 5 4 4 ]
	 *		reg + 1  [ 3 3 2 2 1 1 0 0 ]
	 *
	 * 3) 16-pin chip, single configuration bit register
	 *
	 *	The read will be done as follows:
	 *		reg     [ f e d c b a 9 8 ]
	 *		reg + 1 [ 7 6 5 4 3 2 1 0 ]
	 *
	 * 4) 16-pin chip, double configuration bit register (RegSense)
	 *
	 *	The read will be done as follows:
	 *		reg     [ f f e e d d c c ]
	 *		reg + 1 [ b b a a 9 9 8 8 ]
	 *		reg + 2 [ 7 7 6 6 5 5 4 4 ]
	 *		reg + 3 [ 3 3 2 2 1 1 0 0 ]
	 */

	for (n = width, val = 0, idx = reg; n > 0; n -= 8, idx++) {
		val <<= 8;

		ret = i2c_smbus_read_byte_data(i2c, idx);
		if (ret < 0)
			return ret;

		val |= ret;
	}

	*result = sx150x_maybe_swizzle(pctl, reg, val);

	return 0;
}

static int sx150x_regmap_reg_write(void *context, unsigned int reg,
				   unsigned int val)
{
	int ret, n;
	struct sx150x_pinctrl *pctl = context;
	struct i2c_client *i2c = pctl->client;
	const int width = sx150x_regmap_reg_width(pctl, reg);

	val = sx150x_maybe_swizzle(pctl, reg, val);

	n = (width - 1) & ~7;
	do {
		const u8 byte = (val >> n) & 0xff;

		ret = i2c_smbus_write_byte_data(i2c, reg, byte);
		if (ret < 0)
			return ret;

		reg++;
		n -= 8;
	} while (n >= 0);

	return 0;
}

static bool sx150x_reg_volatile(struct device *dev, unsigned int reg)
{
	struct sx150x_pinctrl *pctl = i2c_get_clientdata(to_i2c_client(dev));

	return reg == pctl->data->reg_irq_src || reg == pctl->data->reg_data;
}

static const struct regmap_config sx150x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,

	.cache_type = REGCACHE_RBTREE,

	.reg_read = sx150x_regmap_reg_read,
	.reg_write = sx150x_regmap_reg_write,

	.max_register = SX150X_MAX_REGISTER,
	.volatile_reg = sx150x_reg_volatile,
};

static int sx150x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	static const u32 i2c_funcs = I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WRITE_WORD_DATA;
	struct device *dev = &client->dev;
	struct sx150x_pinctrl *pctl;
	int ret;

	if (!i2c_check_functionality(client->adapter, i2c_funcs))
		return -ENOSYS;

	pctl = devm_kzalloc(dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	i2c_set_clientdata(client, pctl);

	pctl->dev = dev;
	pctl->client = client;

	if (dev->of_node)
		pctl->data = of_device_get_match_data(dev);
	else
		pctl->data = (struct sx150x_device_data *)id->driver_data;

	if (!pctl->data)
		return -EINVAL;

	pctl->regmap = devm_regmap_init(dev, NULL, pctl,
					&sx150x_regmap_config);
	if (IS_ERR(pctl->regmap)) {
		ret = PTR_ERR(pctl->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	mutex_init(&pctl->lock);

	ret = sx150x_init_hw(pctl);
	if (ret)
		return ret;

	/* Pinctrl_desc */
	pctl->pinctrl_desc.name = "sx150x-pinctrl";
	pctl->pinctrl_desc.pctlops = &sx150x_pinctrl_ops;
	pctl->pinctrl_desc.confops = &sx150x_pinconf_ops;
	pctl->pinctrl_desc.pins = pctl->data->pins;
	pctl->pinctrl_desc.npins = pctl->data->npins;
	pctl->pinctrl_desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(dev, &pctl->pinctrl_desc,
					     pctl, &pctl->pctldev);
	if (ret) {
		dev_err(dev, "Failed to register pinctrl device\n");
		return ret;
	}

	ret = pinctrl_enable(pctl->pctldev);
	if (ret) {
		dev_err(dev, "Failed to enable pinctrl device\n");
		return ret;
	}

	/* Register GPIO controller */
	pctl->gpio.base = -1;
	pctl->gpio.ngpio = pctl->data->npins;
	pctl->gpio.get_direction = sx150x_gpio_get_direction;
	pctl->gpio.direction_input = sx150x_gpio_direction_input;
	pctl->gpio.direction_output = sx150x_gpio_direction_output;
	pctl->gpio.get = sx150x_gpio_get;
	pctl->gpio.set = sx150x_gpio_set;
	pctl->gpio.set_config = gpiochip_generic_config;
	pctl->gpio.parent = dev;
#ifdef CONFIG_OF_GPIO
	pctl->gpio.of_node = dev->of_node;
#endif
	pctl->gpio.can_sleep = true;
	pctl->gpio.label = devm_kstrdup(dev, client->name, GFP_KERNEL);
	if (!pctl->gpio.label)
		return -ENOMEM;

	/*
	 * Setting multiple pins is not safe when all pins are not
	 * handled by the same regmap register. The oscio pin (present
	 * on the SX150X_789 chips) lives in its own register, so
	 * would require locking that is not in place at this time.
	 */
	if (pctl->data->model != SX150X_789)
		pctl->gpio.set_multiple = sx150x_gpio_set_multiple;

	ret = devm_gpiochip_add_data(dev, &pctl->gpio, pctl);
	if (ret)
		return ret;

	ret = gpiochip_add_pin_range(&pctl->gpio, dev_name(dev),
				     0, 0, pctl->data->npins);
	if (ret)
		return ret;

	/* Add Interrupt support if an irq is specified */
	if (client->irq > 0) {
		pctl->irq_chip.irq_mask = sx150x_irq_mask;
		pctl->irq_chip.irq_unmask = sx150x_irq_unmask;
		pctl->irq_chip.irq_set_type = sx150x_irq_set_type;
		pctl->irq_chip.irq_bus_lock = sx150x_irq_bus_lock;
		pctl->irq_chip.irq_bus_sync_unlock = sx150x_irq_bus_sync_unlock;
		pctl->irq_chip.name = devm_kstrdup(dev, client->name,
						   GFP_KERNEL);
		if (!pctl->irq_chip.name)
			return -ENOMEM;

		pctl->irq.masked = ~0;
		pctl->irq.sense = 0;

		/*
		 * Because sx150x_irq_threaded_fn invokes all of the
		 * nested interrrupt handlers via handle_nested_irq,
		 * any "handler" passed to gpiochip_irqchip_add()
		 * below is going to be ignored, so the choice of the
		 * function does not matter that much.
		 *
		 * We set it to handle_bad_irq to avoid confusion,
		 * plus it will be instantly noticeable if it is ever
		 * called (should not happen)
		 */
		ret = gpiochip_irqchip_add_nested(&pctl->gpio,
					&pctl->irq_chip, 0,
					handle_bad_irq, IRQ_TYPE_NONE);
		if (ret) {
			dev_err(dev, "could not connect irqchip to gpiochip\n");
			return ret;
		}

		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						sx150x_irq_thread_fn,
						IRQF_ONESHOT | IRQF_SHARED |
						IRQF_TRIGGER_FALLING,
						pctl->irq_chip.name, pctl);
		if (ret < 0)
			return ret;

		gpiochip_set_nested_irqchip(&pctl->gpio,
					    &pctl->irq_chip,
					    client->irq);
	}

	return 0;
}

static struct i2c_driver sx150x_driver = {
	.driver = {
		.name = "sx150x-pinctrl",
		.of_match_table = of_match_ptr(sx150x_of_match),
	},
	.probe    = sx150x_probe,
	.id_table = sx150x_id,
};

static int __init sx150x_init(void)
{
	return i2c_add_driver(&sx150x_driver);
}
subsys_initcall(sx150x_init);

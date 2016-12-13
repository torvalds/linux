/*
 * Copyright (c) 2016, BayLibre, SAS. All rights reserved.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * Driver for Semtech SX150X I2C GPIO Expanders
 *
 * Author: Gregory Bean <gbean@codeaurora.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/pinctrl/machine.h>
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

struct sx150x_123_pri {
	u8 reg_pld_mode;
	u8 reg_pld_table0;
	u8 reg_pld_table1;
	u8 reg_pld_table2;
	u8 reg_pld_table3;
	u8 reg_pld_table4;
	u8 reg_advance;
};

struct sx150x_456_pri {
	u8 reg_pld_mode;
	u8 reg_pld_table0;
	u8 reg_pld_table1;
	u8 reg_pld_table2;
	u8 reg_pld_table3;
	u8 reg_pld_table4;
	u8 reg_advance;
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
	struct {
		int update;
		u32 sense;
		u32 masked;
		u32 dev_sense;
		u32 dev_masked;
	} irq;
	struct mutex lock;
	const struct sx150x_device_data *data;
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

static const struct sx150x_device_data sx1508q_device_data = {
	.model = SX150X_789,
	.reg_pullup	= 0x03,
	.reg_pulldn	= 0x04,
	.reg_dir	= 0x07,
	.reg_data	= 0x08,
	.reg_irq_mask	= 0x09,
	.reg_irq_src	= 0x0c,
	.reg_sense	= 0x0b,
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
	.reg_pullup	= 0x07,
	.reg_pulldn	= 0x09,
	.reg_dir	= 0x0f,
	.reg_data	= 0x11,
	.reg_irq_mask	= 0x13,
	.reg_irq_src	= 0x19,
	.reg_sense	= 0x17,
	.pri.x789 = {
		.reg_drain	= 0x0b,
		.reg_polarity	= 0x0d,
		.reg_clock	= 0x1e,
		.reg_misc	= 0x1f,
		.reg_reset	= 0x7d,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins = ARRAY_SIZE(sx150x_16_pins),
};

static const struct sx150x_device_data sx1506q_device_data = {
	.model = SX150X_456,
	.reg_pullup	= 0x05,
	.reg_pulldn	= 0x07,
	.reg_dir	= 0x03,
	.reg_data	= 0x01,
	.reg_irq_mask	= 0x09,
	.reg_irq_src	= 0x0f,
	.reg_sense	= 0x0d,
	.pri.x456 = {
		.reg_pld_mode	= 0x21,
		.reg_pld_table0	= 0x23,
		.reg_pld_table1	= 0x25,
		.reg_pld_table2	= 0x27,
		.reg_pld_table3	= 0x29,
		.reg_pld_table4	= 0x2b,
		.reg_advance	= 0xad,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins = 16, /* oscio not available */
};

static const struct sx150x_device_data sx1502q_device_data = {
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
		.reg_pld_table1	= 0x12,
		.reg_pld_table2	= 0x13,
		.reg_pld_table3	= 0x14,
		.reg_pld_table4	= 0x15,
		.reg_advance	= 0xad,
	},
	.ngpios	= 8,
	.pins = sx150x_8_pins,
	.npins = 8, /* oscio not available */
};

static s32 sx150x_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	s32 err = i2c_smbus_write_byte_data(client, reg, val);

	if (err < 0)
		dev_warn(&client->dev,
			"i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, err);
	return err;
}

static s32 sx150x_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 err = i2c_smbus_read_byte_data(client, reg);

	if (err >= 0)
		*val = err;
	else
		dev_warn(&client->dev,
			"i2c read fail: can't read from %02x: %d\n",
			reg, err);
	return err;
}

/*
 * These utility functions solve the common problem of locating and setting
 * configuration bits.  Configuration bits are grouped into registers
 * whose indexes increase downwards.  For example, with eight-bit registers,
 * sixteen gpios would have their config bits grouped in the following order:
 * REGISTER N-1 [ f e d c b a 9 8 ]
 *          N   [ 7 6 5 4 3 2 1 0 ]
 *
 * For multi-bit configurations, the pattern gets wider:
 * REGISTER N-3 [ f f e e d d c c ]
 *          N-2 [ b b a a 9 9 8 8 ]
 *          N-1 [ 7 7 6 6 5 5 4 4 ]
 *          N   [ 3 3 2 2 1 1 0 0 ]
 *
 * Given the address of the starting register 'N', the index of the gpio
 * whose configuration we seek to change, and the width in bits of that
 * configuration, these functions allow us to locate the correct
 * register and mask the correct bits.
 */
static inline void sx150x_find_cfg(u8 offset, u8 width,
				   u8 *reg, u8 *mask, u8 *shift)
{
	*reg   -= offset * width / 8;
	*mask   = (1 << width) - 1;
	*shift  = (offset * width) % 8;
	*mask <<= *shift;
}

static int sx150x_write_cfg(struct i2c_client *client,
			    u8 offset, u8 width, u8 reg, u8 val)
{
	u8  mask;
	u8  data;
	u8  shift;
	int err;

	sx150x_find_cfg(offset, width, &reg, &mask, &shift);
	err = sx150x_i2c_read(client, reg, &data);
	if (err < 0)
		return err;

	data &= ~mask;
	data |= (val << shift) & mask;
	return sx150x_i2c_write(client, reg, data);
}

static int sx150x_read_cfg(struct i2c_client *client,
			   u8 offset, u8 width, u8 reg)
{
	u8  mask;
	u8  data;
	u8  shift;
	int err;

	sx150x_find_cfg(offset, width, &reg, &mask, &shift);
	err = sx150x_i2c_read(client, reg, &data);
	if (err < 0)
		return err;

	return (data & mask);
}

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
	int status;

	if (sx150x_pin_is_oscio(pctl, offset))
		return false;

	status = sx150x_read_cfg(pctl->client, offset, 1, pctl->data->reg_dir);
	if (status >= 0)
		status = !!status;

	return status;
}

static int sx150x_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int status;

	if (sx150x_pin_is_oscio(pctl, offset))
		return -EINVAL;

	status = sx150x_read_cfg(pctl->client, offset, 1, pctl->data->reg_data);
	if (status >= 0)
		status = !!status;

	return status;
}

static int sx150x_gpio_set_single_ended(struct gpio_chip *chip,
					unsigned int offset,
					enum single_ended_mode mode)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int ret;

	switch (mode) {
	case LINE_MODE_PUSH_PULL:
		if (pctl->data->model != SX150X_789 ||
		    sx150x_pin_is_oscio(pctl, offset))
			return 0;

		mutex_lock(&pctl->lock);
		ret = sx150x_write_cfg(pctl->client, offset, 1,
				       pctl->data->pri.x789.reg_drain,
				       0);
		mutex_unlock(&pctl->lock);
		if (ret < 0)
			return ret;
		break;

	case LINE_MODE_OPEN_DRAIN:
		if (pctl->data->model != SX150X_789 ||
		    sx150x_pin_is_oscio(pctl, offset))
			return -ENOTSUPP;

		mutex_lock(&pctl->lock);
		ret = sx150x_write_cfg(pctl->client, offset, 1,
				       pctl->data->pri.x789.reg_drain,
				       1);
		mutex_unlock(&pctl->lock);
		if (ret < 0)
			return ret;
		break;

	default:
		return -ENOTSUPP;
	}

	return 0;
}

static void sx150x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			       int value)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);

	if (sx150x_pin_is_oscio(pctl, offset)) {

		mutex_lock(&pctl->lock);
		sx150x_i2c_write(pctl->client,
				       pctl->data->pri.x789.reg_clock,
				       (value ? 0x1f : 0x10));
		mutex_unlock(&pctl->lock);
	} else {
		mutex_lock(&pctl->lock);
		sx150x_write_cfg(pctl->client, offset, 1,
				       pctl->data->reg_data,
				       (value ? 1 : 0));
		mutex_unlock(&pctl->lock);
	}
}

static int sx150x_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int ret;

	if (sx150x_pin_is_oscio(pctl, offset))
		return -EINVAL;

	mutex_lock(&pctl->lock);
	ret = sx150x_write_cfg(pctl->client, offset, 1,
				pctl->data->reg_dir, 1);
	mutex_unlock(&pctl->lock);

	return ret;
}

static int sx150x_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int status;

	if (sx150x_pin_is_oscio(pctl, offset)) {
		sx150x_gpio_set(chip, offset, value);
		return 0;
	}

	mutex_lock(&pctl->lock);
	status = sx150x_write_cfg(pctl->client, offset, 1,
				  pctl->data->reg_data,
				  (value ? 1 : 0));
	if (status >= 0)
		status = sx150x_write_cfg(pctl->client, offset, 1,
					  pctl->data->reg_dir, 0);
	mutex_unlock(&pctl->lock);

	return status;
}

static void sx150x_irq_mask(struct irq_data *d)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int n = d->hwirq;

	pctl->irq.masked |= (1 << n);
	pctl->irq.update = n;
}

static void sx150x_irq_unmask(struct irq_data *d)
{
	struct sx150x_pinctrl *pctl =
			gpiochip_get_data(irq_data_get_irq_chip_data(d));
	unsigned int n = d->hwirq;

	pctl->irq.masked &= ~(1 << n);
	pctl->irq.update = n;
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
		val |= 0x1;
	if (flow_type & IRQ_TYPE_EDGE_FALLING)
		val |= 0x2;

	pctl->irq.sense &= ~(3UL << (n * 2));
	pctl->irq.sense |= val << (n * 2);
	pctl->irq.update = n;
	return 0;
}

static irqreturn_t sx150x_irq_thread_fn(int irq, void *dev_id)
{
	struct sx150x_pinctrl *pctl = (struct sx150x_pinctrl *)dev_id;
	unsigned int nhandled = 0;
	unsigned int sub_irq;
	unsigned int n;
	s32 err;
	u8 val;
	int i;

	for (i = (pctl->data->ngpios / 8) - 1; i >= 0; --i) {
		err = sx150x_i2c_read(pctl->client,
				      pctl->data->reg_irq_src - i,
				      &val);
		if (err < 0)
			continue;

		err = sx150x_i2c_write(pctl->client,
				       pctl->data->reg_irq_src - i,
				       val);
		if (err < 0)
			continue;

		for (n = 0; n < 8; ++n) {
			if (val & (1 << n)) {
				sub_irq = irq_find_mapping(
						pctl->gpio.irqdomain,
						(i * 8) + n);
				handle_nested_irq(sub_irq);
				++nhandled;
			}
		}
	}

	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
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
	unsigned int n;

	if (pctl->irq.update < 0)
		goto out;

	n = pctl->irq.update;
	pctl->irq.update = -1;

	/* Avoid updates if nothing changed */
	if (pctl->irq.dev_sense == pctl->irq.sense &&
	    pctl->irq.dev_masked == pctl->irq.masked)
		goto out;

	pctl->irq.dev_sense = pctl->irq.sense;
	pctl->irq.dev_masked = pctl->irq.masked;

	if (pctl->irq.masked & (1 << n)) {
		sx150x_write_cfg(pctl->client, n, 1,
				 pctl->data->reg_irq_mask, 1);
		sx150x_write_cfg(pctl->client, n, 2,
				 pctl->data->reg_sense, 0);
	} else {
		sx150x_write_cfg(pctl->client, n, 1,
				 pctl->data->reg_irq_mask, 0);
		sx150x_write_cfg(pctl->client, n, 2,
				 pctl->data->reg_sense,
				 pctl->irq.sense >> (n * 2));
	}
out:
	mutex_unlock(&pctl->lock);
}

static int sx150x_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	int ret;
	u32 arg;

	if (sx150x_pin_is_oscio(pctl, pin)) {
		u8 data;

		switch (param) {
		case PIN_CONFIG_DRIVE_PUSH_PULL:
		case PIN_CONFIG_OUTPUT:
			mutex_lock(&pctl->lock);
			ret = sx150x_i2c_read(pctl->client,
					pctl->data->pri.x789.reg_clock,
					&data);
			mutex_unlock(&pctl->lock);

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
		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->reg_pulldn);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		if (!ret)
			return -EINVAL;

		arg = 1;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->reg_pullup);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		if (!ret)
			return -EINVAL;

		arg = 1;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (pctl->data->model != SX150X_789)
			return -ENOTSUPP;

		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->pri.x789.reg_drain);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		if (!ret)
			return -EINVAL;

		arg = 1;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (pctl->data->model != SX150X_789)
			arg = true;
		else {
			mutex_lock(&pctl->lock);
			ret = sx150x_read_cfg(pctl->client, pin, 1,
					      pctl->data->pri.x789.reg_drain);
			mutex_unlock(&pctl->lock);

			if (ret < 0)
				return ret;

			if (ret)
				return -EINVAL;

			arg = 1;
		}
		break;

	case PIN_CONFIG_OUTPUT:
		ret = sx150x_gpio_get_direction(&pctl->gpio, pin);
		if (ret < 0)
			return ret;

		if (ret)
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
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pulldn, 0);
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pullup, 0);
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pullup,
					       1);
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pulldn,
					       1);
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			ret = sx150x_gpio_set_single_ended(&pctl->gpio,
						pin, LINE_MODE_OPEN_DRAIN);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			ret = sx150x_gpio_set_single_ended(&pctl->gpio,
						pin, LINE_MODE_PUSH_PULL);
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
	{"sx1508q", (kernel_ulong_t) &sx1508q_device_data },
	{"sx1509q", (kernel_ulong_t) &sx1509q_device_data },
	{"sx1506q", (kernel_ulong_t) &sx1506q_device_data },
	{"sx1502q", (kernel_ulong_t) &sx1502q_device_data },
	{}
};

static const struct of_device_id sx150x_of_match[] = {
	{ .compatible = "semtech,sx1508q" },
	{ .compatible = "semtech,sx1509q" },
	{ .compatible = "semtech,sx1506q" },
	{ .compatible = "semtech,sx1502q" },
	{},
};

static int sx150x_init_io(struct sx150x_pinctrl *pctl, u8 base, u16 cfg)
{
	int err = 0;
	unsigned int n;

	for (n = 0; err >= 0 && n < (pctl->data->ngpios / 8); ++n)
		err = sx150x_i2c_write(pctl->client, base - n, cfg >> (n * 8));
	return err;
}

static int sx150x_reset(struct sx150x_pinctrl *pctl)
{
	int err;

	err = i2c_smbus_write_byte_data(pctl->client,
					pctl->data->pri.x789.reg_reset,
					0x12);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(pctl->client,
					pctl->data->pri.x789.reg_reset,
					0x34);
	return err;
}

static int sx150x_init_hw(struct sx150x_pinctrl *pctl)
{
	int err;

	if (pctl->data->model == SX150X_789 &&
	    of_property_read_bool(pctl->dev->of_node, "semtech,probe-reset")) {
		err = sx150x_reset(pctl);
		if (err < 0)
			return err;
	}

	if (pctl->data->model == SX150X_789)
		err = sx150x_i2c_write(pctl->client,
				pctl->data->pri.x789.reg_misc,
				0x01);
	else if (pctl->data->model == SX150X_456)
		err = sx150x_i2c_write(pctl->client,
				pctl->data->pri.x456.reg_advance,
				0x04);
	else
		err = sx150x_i2c_write(pctl->client,
				pctl->data->pri.x123.reg_advance,
				0x00);
	if (err < 0)
		return err;

	/* Set all pins to work in normal mode */
	if (pctl->data->model == SX150X_789) {
		err = sx150x_init_io(pctl,
				pctl->data->pri.x789.reg_polarity,
				0);
		if (err < 0)
			return err;
	} else if (pctl->data->model == SX150X_456) {
		/* Set all pins to work in normal mode */
		err = sx150x_init_io(pctl,
				pctl->data->pri.x456.reg_pld_mode,
				0);
		if (err < 0)
			return err;
	} else {
		/* Set all pins to work in normal mode */
		err = sx150x_init_io(pctl,
				pctl->data->pri.x123.reg_pld_mode,
				0);
		if (err < 0)
			return err;
	}

	return 0;
}

static int sx150x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	static const u32 i2c_funcs = I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WRITE_WORD_DATA;
	struct device *dev = &client->dev;
	struct sx150x_pinctrl *pctl;
	int ret;

	if (!id->driver_data)
		return -EINVAL;

	if (!i2c_check_functionality(client->adapter, i2c_funcs))
		return -ENOSYS;

	pctl = devm_kzalloc(dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	pctl->dev = dev;
	pctl->client = client;
	pctl->data = (void *)id->driver_data;

	mutex_init(&pctl->lock);

	ret = sx150x_init_hw(pctl);
	if (ret)
		return ret;

	/* Register GPIO controller */
	pctl->gpio.label = devm_kstrdup(dev, client->name, GFP_KERNEL);
	pctl->gpio.base = -1;
	pctl->gpio.ngpio = pctl->data->npins;
	pctl->gpio.get_direction = sx150x_gpio_get_direction;
	pctl->gpio.direction_input = sx150x_gpio_direction_input;
	pctl->gpio.direction_output = sx150x_gpio_direction_output;
	pctl->gpio.get = sx150x_gpio_get;
	pctl->gpio.set = sx150x_gpio_set;
	pctl->gpio.set_single_ended = sx150x_gpio_set_single_ended;
	pctl->gpio.parent = dev;
#ifdef CONFIG_OF_GPIO
	pctl->gpio.of_node = dev->of_node;
#endif
	pctl->gpio.can_sleep = true;

	ret = devm_gpiochip_add_data(dev, &pctl->gpio, pctl);
	if (ret)
		return ret;

	/* Add Interrupt support if an irq is specified */
	if (client->irq > 0) {
		pctl->irq_chip.name = devm_kstrdup(dev, client->name,
						   GFP_KERNEL);
		pctl->irq_chip.irq_mask = sx150x_irq_mask;
		pctl->irq_chip.irq_unmask = sx150x_irq_unmask;
		pctl->irq_chip.irq_set_type = sx150x_irq_set_type;
		pctl->irq_chip.irq_bus_lock = sx150x_irq_bus_lock;
		pctl->irq_chip.irq_bus_sync_unlock = sx150x_irq_bus_sync_unlock;

		pctl->irq.masked = ~0;
		pctl->irq.sense = 0;
		pctl->irq.dev_masked = ~0;
		pctl->irq.dev_sense = 0;
		pctl->irq.update = -1;

		ret = gpiochip_irqchip_add(&pctl->gpio,
					   &pctl->irq_chip, 0,
					   handle_edge_irq, IRQ_TYPE_NONE);
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
	}

	/* Pinctrl_desc */
	pctl->pinctrl_desc.name = "sx150x-pinctrl";
	pctl->pinctrl_desc.pctlops = &sx150x_pinctrl_ops;
	pctl->pinctrl_desc.confops = &sx150x_pinconf_ops;
	pctl->pinctrl_desc.pins = pctl->data->pins;
	pctl->pinctrl_desc.npins = pctl->data->npins;
	pctl->pinctrl_desc.owner = THIS_MODULE;

	pctl->pctldev = pinctrl_register(&pctl->pinctrl_desc, dev, pctl);
	if (IS_ERR(pctl->pctldev)) {
		dev_err(dev, "Failed to register pinctrl device\n");
		return PTR_ERR(pctl->pctldev);
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

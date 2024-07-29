// SPDX-License-Identifier: GPL-2.0
/*
 * MAX1600 PCMCIA power switch library
 *
 * Copyright (C) 2016 Russell King
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include "max1600.h"

static const char *max1600_gpio_name[2][MAX1600_GPIO_MAX] = {
	{ "a0vcc", "a1vcc", "a0vpp", "a1vpp" },
	{ "b0vcc", "b1vcc", "b0vpp", "b1vpp" },
};

int max1600_init(struct device *dev, struct max1600 **ptr,
	unsigned int channel, unsigned int code)
{
	struct max1600 *m;
	int chan;
	int i;

	switch (channel) {
	case MAX1600_CHAN_A:
		chan = 0;
		break;
	case MAX1600_CHAN_B:
		chan = 1;
		break;
	default:
		return -EINVAL;
	}

	if (code != MAX1600_CODE_LOW && code != MAX1600_CODE_HIGH)
		return -EINVAL;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->dev = dev;
	m->code = code;

	for (i = 0; i < MAX1600_GPIO_MAX; i++) {
		const char *name;

		name = max1600_gpio_name[chan][i];
		if (i != MAX1600_GPIO_0VPP) {
			m->gpio[i] = devm_gpiod_get(dev, name, GPIOD_OUT_LOW);
		} else {
			m->gpio[i] = devm_gpiod_get_optional(dev, name,
							     GPIOD_OUT_LOW);
			if (!m->gpio[i])
				break;
		}
		if (IS_ERR(m->gpio[i]))
			return PTR_ERR(m->gpio[i]);
	}

	*ptr = m;

	return 0;
}
EXPORT_SYMBOL_GPL(max1600_init);

int max1600_configure(struct max1600 *m, unsigned int vcc, unsigned int vpp)
{
	DECLARE_BITMAP(values, MAX1600_GPIO_MAX) = { 0, };
	int n = MAX1600_GPIO_0VPP;

	if (m->gpio[MAX1600_GPIO_0VPP]) {
		if (vpp == 0) {
			__assign_bit(MAX1600_GPIO_0VPP, values, 0);
			__assign_bit(MAX1600_GPIO_1VPP, values, 0);
		} else if (vpp == 120) {
			__assign_bit(MAX1600_GPIO_0VPP, values, 0);
			__assign_bit(MAX1600_GPIO_1VPP, values, 1);
		} else if (vpp == vcc) {
			__assign_bit(MAX1600_GPIO_0VPP, values, 1);
			__assign_bit(MAX1600_GPIO_1VPP, values, 0);
		} else {
			dev_err(m->dev, "unrecognised Vpp %u.%uV\n",
				vpp / 10, vpp % 10);
			return -EINVAL;
		}
		n = MAX1600_GPIO_MAX;
	} else if (vpp != vcc && vpp != 0) {
		dev_err(m->dev, "no VPP control\n");
		return -EINVAL;
	}

	if (vcc == 0) {
		__assign_bit(MAX1600_GPIO_0VCC, values, 0);
		__assign_bit(MAX1600_GPIO_1VCC, values, 0);
	} else if (vcc == 33) { /* VY */
		__assign_bit(MAX1600_GPIO_0VCC, values, 1);
		__assign_bit(MAX1600_GPIO_1VCC, values, 0);
	} else if (vcc == 50) { /* VX */
		__assign_bit(MAX1600_GPIO_0VCC, values, 0);
		__assign_bit(MAX1600_GPIO_1VCC, values, 1);
	} else {
		dev_err(m->dev, "unrecognised Vcc %u.%uV\n",
			vcc / 10, vcc % 10);
		return -EINVAL;
	}

	if (m->code == MAX1600_CODE_HIGH) {
		/*
		 * Cirrus mode appears to be the same as Intel mode,
		 * except the VCC pins are inverted.
		 */
		__change_bit(MAX1600_GPIO_0VCC, values);
		__change_bit(MAX1600_GPIO_1VCC, values);
	}

	return gpiod_set_array_value_cansleep(n, m->gpio, NULL, values);
}
EXPORT_SYMBOL_GPL(max1600_configure);

MODULE_DESCRIPTION("MAX1600 PCMCIA power switch library");
MODULE_LICENSE("GPL v2");

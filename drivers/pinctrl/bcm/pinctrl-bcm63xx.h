// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Álvaro Fernández Rojas <noltari@gmail.com>
 * Copyright (C) 2016 Jonas Gorski <jonas.gorski@gmail.com>
 */

#ifndef __PINCTRL_BCM63XX_H__
#define __PINCTRL_BCM63XX_H__

#include <linux/pinctrl/pinctrl.h>

#define BCM63XX_BANK_GPIOS 32

struct bcm63xx_pinctrl_soc {
	const struct pinctrl_ops *pctl_ops;
	const struct pinmux_ops *pmx_ops;

	const struct pinctrl_pin_desc *pins;
	unsigned npins;

	unsigned int ngpios;
};

#define BCM_PIN_GROUP(n)	PINCTRL_PINGROUP(#n, n##_pins, ARRAY_SIZE(n##_pins))

struct bcm63xx_pinctrl {
	struct device *dev;
	struct regmap *regs;

	struct pinctrl_desc pctl_desc;
	struct pinctrl_dev *pctl_dev;

	void *driver_data;
};

static inline unsigned int bcm63xx_bank_pin(unsigned int pin)
{
	return pin % BCM63XX_BANK_GPIOS;
}

int bcm63xx_pinctrl_probe(struct platform_device *pdev,
			  const struct bcm63xx_pinctrl_soc *soc,
			  void *driver_data);

#endif /* __PINCTRL_BCM63XX_H__ */

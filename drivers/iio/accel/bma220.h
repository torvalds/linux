/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Forward declarations needed by the bma220 sources.
 *
 * Copyright 2025 Petre Rodan <petre.rodan@subdimension.ro>
 */

#ifndef _BMA220_H
#define _BMA220_H

#include <linux/pm.h>
#include <linux/regmap.h>

#define BMA220_REG_WDT				0x17
#define BMA220_WDT_MASK				GENMASK(2, 1)
#define BMA220_WDT_OFF				0x0
#define BMA220_WDT_1MS				0x2
#define BMA220_WDT_10MS				0x3

struct device;

extern const struct regmap_config bma220_i2c_regmap_config;
extern const struct regmap_config bma220_spi_regmap_config;
extern const struct dev_pm_ops bma220_pm_ops;

int bma220_common_probe(struct device *dev, struct regmap *regmap, int irq);

#endif

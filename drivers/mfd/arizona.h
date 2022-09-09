/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm5102.h  --  WM5102 MFD internals
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef _WM5102_H
#define _WM5102_H

#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/pm.h>

extern const struct regmap_config wm5102_i2c_regmap;
extern const struct regmap_config wm5102_spi_regmap;

extern const struct regmap_config wm5110_i2c_regmap;
extern const struct regmap_config wm5110_spi_regmap;

extern const struct regmap_config cs47l24_spi_regmap;

extern const struct regmap_config wm8997_i2c_regmap;

extern const struct regmap_config wm8998_i2c_regmap;

extern const struct dev_pm_ops arizona_pm_ops;

extern const struct regmap_irq_chip wm5102_aod;
extern const struct regmap_irq_chip wm5102_irq;

extern const struct regmap_irq_chip wm5110_aod;
extern const struct regmap_irq_chip wm5110_irq;
extern const struct regmap_irq_chip wm5110_revd_irq;

extern const struct regmap_irq_chip cs47l24_irq;

extern const struct regmap_irq_chip wm8997_aod;
extern const struct regmap_irq_chip wm8997_irq;

extern struct regmap_irq_chip wm8998_aod;
extern struct regmap_irq_chip wm8998_irq;

int arizona_dev_init(struct arizona *arizona);
int arizona_dev_exit(struct arizona *arizona);
int arizona_irq_init(struct arizona *arizona);
int arizona_irq_exit(struct arizona *arizona);

#endif

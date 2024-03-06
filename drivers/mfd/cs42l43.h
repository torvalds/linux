/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CS42L43 core driver internal data
 *
 * Copyright (C) 2022-2023 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/mfd/cs42l43.h>
#include <linux/pm.h>
#include <linux/regmap.h>

#ifndef CS42L43_CORE_INT_H
#define CS42L43_CORE_INT_H

#define CS42L43_N_DEFAULTS 176

extern const struct dev_pm_ops cs42l43_pm_ops;
extern const struct reg_default cs42l43_reg_default[CS42L43_N_DEFAULTS];

bool cs42l43_readable_register(struct device *dev, unsigned int reg);
bool cs42l43_precious_register(struct device *dev, unsigned int reg);
bool cs42l43_volatile_register(struct device *dev, unsigned int reg);

int cs42l43_dev_probe(struct cs42l43 *cs42l43);
void cs42l43_dev_remove(struct cs42l43 *cs42l43);

#endif /* CS42L43_CORE_INT_H */

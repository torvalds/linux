/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8994.h -- WM8994 MFD internals
 *
 * Copyright 2011 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef __MFD_WM8994_H__
#define __MFD_WM8994_H__

#include <linux/regmap.h>

extern struct regmap_config wm1811_regmap_config;
extern struct regmap_config wm8994_regmap_config;
extern struct regmap_config wm8958_regmap_config;
extern struct regmap_config wm8994_base_regmap_config;

#endif

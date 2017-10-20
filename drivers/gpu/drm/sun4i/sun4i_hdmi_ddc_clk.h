/*
 * Copyright (C) 2018 Olliver Schinagl
 *
 * Olliver Schinagl <oliver@schinagl.nl>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */

#ifndef _SUN4I_HDMI_DDC_CLK_H_
#define _SUN4I_HDMI_DDC_CLK_H_

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include "sun4i_hdmi_i2c_drv.h"

struct clk *sun4i_ddc_create(struct device *dev, struct regmap *regmap,
			     const struct sun4i_hdmi_i2c_variant *variant,
			     const struct clk *parent);

#endif

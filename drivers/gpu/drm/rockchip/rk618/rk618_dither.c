/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rk618_output.h"

#define RK618_FRC_REG			0x0054
#define FRC_DEN_INV			HIWORD_UPDATE(1, 6, 6)
#define FRC_SYNC_INV			HIWORD_UPDATE(1, 5, 5)
#define FRC_DCLK_INV			HIWORD_UPDATE(1, 4, 4)
#define FRC_OUT_ZERO			HIWORD_UPDATE(1, 3, 3)
#define FRC_OUT_MODE_RGB666		HIWORD_UPDATE(1, 2, 2)
#define FRC_OUT_MODE_RGB888		HIWORD_UPDATE(0, 2, 2)
#define FRC_DITHER_MODE_HI_FRC		HIWORD_UPDATE(1, 1, 1)
#define FRC_DITHER_MODE_FRC		HIWORD_UPDATE(0, 1, 1)
#define FRC_DITHER_ENABLE		HIWORD_UPDATE(1, 0, 0)
#define FRC_DITHER_DISABLE		HIWORD_UPDATE(0, 0, 0)

void rk618_dither_disable(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_FRC_REG, FRC_DITHER_DISABLE);
}
EXPORT_SYMBOL_GPL(rk618_dither_disable);

void rk618_dither_enable(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_FRC_REG, FRC_DITHER_ENABLE);
}
EXPORT_SYMBOL_GPL(rk618_dither_enable);

void rk618_dither_frc_dclk_invert(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_FRC_REG, FRC_DCLK_INV);
}
EXPORT_SYMBOL_GPL(rk618_dither_frc_dclk_invert);

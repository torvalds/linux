/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#ifndef _RK618_DITHER_H_
#define _RK618_DITHER_H_

#include <uapi/linux/media-bus-format.h>
#include <linux/mfd/rk618.h>

void rk618_frc_dither_disable(struct rk618 *rk618);
void rk618_frc_dither_enable(struct rk618 *rk618, u32 bus_format);
void rk618_frc_dclk_invert(struct rk618 *rk618);

#endif

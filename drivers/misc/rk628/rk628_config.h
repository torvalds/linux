/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "rk628.h"

struct rk628_display_mode *rk628_display_get_src_mode(struct rk628 *rk628);
struct rk628_display_mode *rk628_display_get_dst_mode(struct rk628 *rk628);
void rk628_mode_copy(struct rk628_display_mode *to, struct rk628_display_mode *from);


void rk628_set_input_bus_format(struct rk628 *rk628, enum bus_format format);
enum bus_format rk628_get_input_bus_format(struct rk628 *rk628);
void rk628_set_output_bus_format(struct rk628 *rk628, enum bus_format format);
enum bus_format rk628_get_output_bus_format(struct rk628 *rk628);

#endif


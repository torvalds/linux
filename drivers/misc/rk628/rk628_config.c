// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include "rk628_config.h"

struct rk628_display_mode *rk628_display_get_src_mode(struct rk628 *rk628)
{
	return &rk628->src_mode;
}

struct rk628_display_mode *rk628_display_get_dst_mode(struct rk628 *rk628)
{
	return &rk628->dst_mode;
}

void rk628_mode_copy(struct rk628_display_mode *to, struct rk628_display_mode *from)
{
	to->clock = from->clock;
	to->hdisplay = from->hdisplay;
	to->hsync_start = from->hsync_start;
	to->hsync_end = from->hsync_end;
	to->htotal = from->htotal;
	to->vdisplay = from->vdisplay;
	to->vsync_start = from->vsync_start;
	to->vsync_end = from->vsync_end;
	to->vtotal = from->vtotal;
	to->flags = from->flags;
}

void rk628_set_input_bus_format(struct rk628 *rk628, enum bus_format format)
{
	rk628->input_fmt = format;
}

enum bus_format rk628_get_input_bus_format(struct rk628 *rk628)
{
	return rk628->input_fmt;
}

void rk628_set_output_bus_format(struct rk628 *rk628, enum bus_format format)
{
	rk628->output_fmt = format;
}

enum bus_format rk628_get_output_bus_format(struct rk628 *rk628)
{
	return rk628->output_fmt;
}

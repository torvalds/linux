// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define IS_VERSION			0x0000
#define IS_H_OFFS			0x0008
#define IS_V_OFFS			0x000c
#define IS_H_SIZE			0x0010
#define IS_V_SIZE			0x0014
#define IS_H_OFFS_SHD			0x0024
#define IS_V_OFFS_SHD			0x0028
#define IS_H_SIZE_SHD			0x002c
#define IS_V_SIZE_SHD			0x0030

static int rppx1_is_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, IS_VERSION) != 1)
		return -EINVAL;

	return 0;
}

static int rppx1_is_start(struct rpp_module *mod,
			  const struct v4l2_mbus_framefmt *fmt)
{
	rpp_module_write(mod, IS_H_OFFS, 0);
	rpp_module_write(mod, IS_V_OFFS, 0);
	rpp_module_write(mod, IS_H_SIZE, fmt->width);
	rpp_module_write(mod, IS_V_SIZE, fmt->height);

	return 0;
}

const struct rpp_module_ops rppx1_is_ops = {
	.probe = rppx1_is_probe,
	.start = rppx1_is_start,
};

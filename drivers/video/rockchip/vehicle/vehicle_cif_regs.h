/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle Driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _VEHICLE_RKCIF_REGS_H
#define _VEHICLE_RKCIF_REGS_H
#include "../../../media/platform/rockchip/cif/regs.h"

struct vehicle_cif_reg {
	u32 offset;
	char *name;
};

#define CIF_REG_NAME(_offset, _name)	{ .offset = (_offset), .name = (_name), }

#endif

/*
 * This file is part of wlcore
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WLCORE_HW_OPS_H__
#define __WLCORE_HW_OPS_H__

#include "wlcore.h"

static inline u32
wlcore_hw_calc_tx_blocks(struct wl1271 *wl, u32 len, u32 spare_blks)
{
	if (!wl->ops->calc_tx_blocks)
		BUG_ON(1);

	return wl->ops->calc_tx_blocks(wl, len, spare_blks);
}

#endif

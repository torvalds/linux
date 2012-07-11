/*
 * This file is part of wl12xx
 *
 * Copyright (C) 1998-2009, 2011 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2010 Nokia Corporation
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

#ifndef __WL12XX_ACX_H__
#define __WL12XX_ACX_H__

#include "../wlcore/wlcore.h"

struct wl1271_acx_host_config_bitmap {
	struct acx_header header;

	__le32 host_cfg_bitmap;
} __packed;

int wl1271_acx_host_if_cfg_bitmap(struct wl1271 *wl, u32 host_cfg_bitmap);

#endif /* __WL12XX_ACX_H__ */

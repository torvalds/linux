// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 * Copyright (C) 2011 Texas Instruments Inc.
 */

#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"
#include "../wlcore/acx.h"

#include "acx.h"

int wl1271_acx_host_if_cfg_bitmap(struct wl1271 *wl, u32 host_cfg_bitmap)
{
	struct wl1271_acx_host_config_bitmap *bitmap_conf;
	int ret;

	bitmap_conf = kzalloc(sizeof(*bitmap_conf), GFP_KERNEL);
	if (!bitmap_conf) {
		ret = -ENOMEM;
		goto out;
	}

	bitmap_conf->host_cfg_bitmap = cpu_to_le32(host_cfg_bitmap);

	ret = wl1271_cmd_configure(wl, ACX_HOST_IF_CFG_BITMAP,
				   bitmap_conf, sizeof(*bitmap_conf));
	if (ret < 0) {
		wl1271_warning("wl1271 bitmap config opt failed: %d", ret);
		goto out;
	}

out:
	kfree(bitmap_conf);

	return ret;
}

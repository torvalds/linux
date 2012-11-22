/*
 * This file is part of wl18xx
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

#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"
#include "../wlcore/acx.h"

#include "acx.h"

int wl18xx_acx_host_if_cfg_bitmap(struct wl1271 *wl, u32 host_cfg_bitmap,
				  u32 sdio_blk_size, u32 extra_mem_blks,
				  u32 len_field_size)
{
	struct wl18xx_acx_host_config_bitmap *bitmap_conf;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx cfg bitmap %d blk %d spare %d field %d",
		     host_cfg_bitmap, sdio_blk_size, extra_mem_blks,
		     len_field_size);

	bitmap_conf = kzalloc(sizeof(*bitmap_conf), GFP_KERNEL);
	if (!bitmap_conf) {
		ret = -ENOMEM;
		goto out;
	}

	bitmap_conf->host_cfg_bitmap = cpu_to_le32(host_cfg_bitmap);
	bitmap_conf->host_sdio_block_size = cpu_to_le32(sdio_blk_size);
	bitmap_conf->extra_mem_blocks = cpu_to_le32(extra_mem_blks);
	bitmap_conf->length_field_size = cpu_to_le32(len_field_size);

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

int wl18xx_acx_set_checksum_state(struct wl1271 *wl)
{
	struct wl18xx_acx_checksum_state *acx;
	int ret;

	wl1271_debug(DEBUG_ACX, "acx checksum state");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->checksum_state = CHECKSUM_OFFLOAD_ENABLED;

	ret = wl1271_cmd_configure(wl, ACX_CSUM_CONFIG, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to set Tx checksum state: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl18xx_acx_clear_statistics(struct wl1271 *wl)
{
	struct wl18xx_acx_clear_statistics *acx;
	int ret = 0;

	wl1271_debug(DEBUG_ACX, "acx clear statistics");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl1271_cmd_configure(wl, ACX_CLEAR_STATISTICS, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_warning("failed to clear firmware statistics: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

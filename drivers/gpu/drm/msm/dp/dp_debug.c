// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)"[drm-dp] %s: " fmt, __func__

#include <linux/debugfs.h>
#include <drm/drm_connector.h>

#include "dp_parser.h"
#include "dp_catalog.h"
#include "dp_aux.h"
#include "dp_ctrl.h"
#include "dp_debug.h"
#include "dp_display.h"

#define DEBUG_NAME "drm_dp"

struct dp_debug_private {
	struct dentry *root;

	struct dp_usbpd *usbpd;
	struct dp_link *link;
	struct dp_panel *panel;
	struct drm_connector **connector;
	struct device *dev;

	struct dp_debug dp_debug;
};

static int dp_debug_check_buffer_overflow(int rc, int *max_size, int *len)
{
	if (rc >= *max_size) {
		DRM_ERROR("buffer overflow\n");
		return -EINVAL;
	}
	*len += rc;
	*max_size = SZ_4K - *len;

	return 0;
}

static ssize_t dp_debug_read_info(struct file *file, char __user *user_buff,
		size_t count, loff_t *ppos)
{
	struct dp_debug_private *debug = file->private_data;
	char *buf;
	u32 len = 0, rc = 0;
	u64 lclk = 0;
	u32 max_size = SZ_4K;
	u32 link_params_rate;
	struct drm_display_mode *drm_mode;

	if (!debug)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	drm_mode = &debug->panel->dp_mode.drm_mode;

	rc = snprintf(buf + len, max_size, "\tname = %s\n", DEBUG_NAME);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\tdp_panel\n\t\tmax_pclk_khz = %d\n",
			debug->panel->max_pclk_khz);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\tdrm_dp_link\n\t\trate = %u\n",
			debug->panel->link_info.rate);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			 "\t\tnum_lanes = %u\n",
			debug->panel->link_info.num_lanes);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tcapabilities = %lu\n",
			debug->panel->link_info.capabilities);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\tdp_panel_info:\n\t\tactive = %dx%d\n",
			drm_mode->hdisplay,
			drm_mode->vdisplay);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tback_porch = %dx%d\n",
			drm_mode->htotal - drm_mode->hsync_end,
			drm_mode->vtotal - drm_mode->vsync_end);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tfront_porch = %dx%d\n",
			drm_mode->hsync_start - drm_mode->hdisplay,
			drm_mode->vsync_start - drm_mode->vdisplay);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tsync_width = %dx%d\n",
			drm_mode->hsync_end - drm_mode->hsync_start,
			drm_mode->vsync_end - drm_mode->vsync_start);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tactive_low = %dx%d\n",
			debug->panel->dp_mode.h_active_low,
			debug->panel->dp_mode.v_active_low);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\th_skew = %d\n",
			drm_mode->hskew);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\trefresh rate = %d\n",
			drm_mode_vrefresh(drm_mode));
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tpixel clock khz = %d\n",
			drm_mode->clock);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tbpp = %d\n",
			debug->panel->dp_mode.bpp);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	/* Link Information */
	rc = snprintf(buf + len, max_size,
			"\tdp_link:\n\t\ttest_requested = %d\n",
			debug->link->sink_request);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tnum_lanes = %d\n",
			debug->link->link_params.num_lanes);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	link_params_rate = debug->link->link_params.rate;
	rc = snprintf(buf + len, max_size,
			"\t\tbw_code = %d\n",
			drm_dp_link_rate_to_bw_code(link_params_rate));
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	lclk = debug->link->link_params.rate * 1000;
	rc = snprintf(buf + len, max_size,
			"\t\tlclk = %lld\n", lclk);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tv_level = %d\n",
			debug->link->phy_params.v_level);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = snprintf(buf + len, max_size,
			"\t\tp_level = %d\n",
			debug->link->phy_params.p_level);
	if (dp_debug_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	if (copy_to_user(user_buff, buf, len))
		goto error;

	*ppos += len;

	kfree(buf);
	return len;
 error:
	kfree(buf);
	return -EINVAL;
}

static const struct file_operations dp_debug_fops = {
	.open = simple_open,
	.read = dp_debug_read_info,
};

static int dp_debug_init(struct dp_debug *dp_debug)
{
	int rc = 0;
	struct dp_debug_private *debug = container_of(dp_debug,
			struct dp_debug_private, dp_debug);
	struct dentry *dir, *file;

	dir = debugfs_create_dir(DEBUG_NAME, NULL);
	if (IS_ERR_OR_NULL(dir)) {
		rc = PTR_ERR(dir);
		DRM_ERROR("[%s] debugfs create dir failed, rc = %d\n",
				  DEBUG_NAME, rc);
		goto error;
	}

	file = debugfs_create_file("dp_debug", 0444, dir,
			debug, &dp_debug_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DRM_ERROR("[%s] debugfs create file failed, rc=%d\n",
				  DEBUG_NAME, rc);
		goto error_remove_dir;
	}

	debug->root = dir;
	return rc;
 error_remove_dir:
	debugfs_remove(dir);
 error:
	return rc;
}

struct dp_debug *dp_debug_get(struct device *dev, struct dp_panel *panel,
		struct dp_usbpd *usbpd, struct dp_link *link,
		struct drm_connector **connector)
{
	int rc = 0;
	struct dp_debug_private *debug;
	struct dp_debug *dp_debug;

	if (!dev || !panel || !usbpd || !link) {
		DRM_ERROR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	debug = devm_kzalloc(dev, sizeof(*debug), GFP_KERNEL);
	if (!debug) {
		rc = -ENOMEM;
		goto error;
	}

	debug->dp_debug.debug_en = false;
	debug->usbpd = usbpd;
	debug->link = link;
	debug->panel = panel;
	debug->dev = dev;
	debug->connector = connector;

	dp_debug = &debug->dp_debug;
	dp_debug->vdisplay = 0;
	dp_debug->hdisplay = 0;
	dp_debug->vrefresh = 0;

	rc = dp_debug_init(dp_debug);
	if (rc) {
		devm_kfree(dev, debug);
		goto error;
	}

	return dp_debug;
 error:
	return ERR_PTR(rc);
}

static int dp_debug_deinit(struct dp_debug *dp_debug)
{
	struct dp_debug_private *debug;

	if (!dp_debug)
		return -EINVAL;

	debug = container_of(dp_debug, struct dp_debug_private, dp_debug);

	debugfs_remove_recursive(debug->root);

	return 0;
}

void dp_debug_put(struct dp_debug *dp_debug)
{
	struct dp_debug_private *debug;

	if (!dp_debug)
		return;

	debug = container_of(dp_debug, struct dp_debug_private, dp_debug);

	dp_debug_deinit(dp_debug);

	devm_kfree(debug->dev, debug);
}

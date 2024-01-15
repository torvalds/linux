// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)"[drm-dp] %s: " fmt, __func__

#include <linux/debugfs.h>
#include <drm/drm_connector.h>
#include <drm/drm_file.h>

#include "dp_parser.h"
#include "dp_catalog.h"
#include "dp_aux.h"
#include "dp_ctrl.h"
#include "dp_debug.h"
#include "dp_display.h"

#define DEBUG_NAME "msm_dp"

struct dp_debug_private {
	struct dp_link *link;
	struct dp_panel *panel;
	struct drm_connector *connector;

	struct dp_debug dp_debug;
};

static int dp_debug_show(struct seq_file *seq, void *p)
{
	struct dp_debug_private *debug = seq->private;
	u64 lclk = 0;
	u32 link_params_rate;
	const struct drm_display_mode *drm_mode;

	if (!debug)
		return -ENODEV;

	drm_mode = &debug->panel->dp_mode.drm_mode;

	seq_printf(seq, "\tname = %s\n", DEBUG_NAME);
	seq_printf(seq, "\tdrm_dp_link\n\t\trate = %u\n",
			debug->panel->link_info.rate);
	seq_printf(seq, "\t\tnum_lanes = %u\n",
			debug->panel->link_info.num_lanes);
	seq_printf(seq, "\t\tcapabilities = %lu\n",
			debug->panel->link_info.capabilities);
	seq_printf(seq, "\tdp_panel_info:\n\t\tactive = %dx%d\n",
			drm_mode->hdisplay,
			drm_mode->vdisplay);
	seq_printf(seq, "\t\tback_porch = %dx%d\n",
			drm_mode->htotal - drm_mode->hsync_end,
			drm_mode->vtotal - drm_mode->vsync_end);
	seq_printf(seq, "\t\tfront_porch = %dx%d\n",
			drm_mode->hsync_start - drm_mode->hdisplay,
			drm_mode->vsync_start - drm_mode->vdisplay);
	seq_printf(seq, "\t\tsync_width = %dx%d\n",
			drm_mode->hsync_end - drm_mode->hsync_start,
			drm_mode->vsync_end - drm_mode->vsync_start);
	seq_printf(seq, "\t\tactive_low = %dx%d\n",
			debug->panel->dp_mode.h_active_low,
			debug->panel->dp_mode.v_active_low);
	seq_printf(seq, "\t\th_skew = %d\n",
			drm_mode->hskew);
	seq_printf(seq, "\t\trefresh rate = %d\n",
			drm_mode_vrefresh(drm_mode));
	seq_printf(seq, "\t\tpixel clock khz = %d\n",
			drm_mode->clock);
	seq_printf(seq, "\t\tbpp = %d\n",
			debug->panel->dp_mode.bpp);

	/* Link Information */
	seq_printf(seq, "\tdp_link:\n\t\ttest_requested = %d\n",
			debug->link->sink_request);
	seq_printf(seq, "\t\tnum_lanes = %d\n",
			debug->link->link_params.num_lanes);
	link_params_rate = debug->link->link_params.rate;
	seq_printf(seq, "\t\tbw_code = %d\n",
			drm_dp_link_rate_to_bw_code(link_params_rate));
	lclk = debug->link->link_params.rate * 1000;
	seq_printf(seq, "\t\tlclk = %lld\n", lclk);
	seq_printf(seq, "\t\tv_level = %d\n",
			debug->link->phy_params.v_level);
	seq_printf(seq, "\t\tp_level = %d\n",
			debug->link->phy_params.p_level);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dp_debug);

static int dp_test_data_show(struct seq_file *m, void *data)
{
	const struct dp_debug_private *debug = m->private;
	const struct drm_connector *connector = debug->connector;
	u32 bpc;

	if (connector->status == connector_status_connected) {
		bpc = debug->link->test_video.test_bit_depth;
		seq_printf(m, "hdisplay: %d\n",
				debug->link->test_video.test_h_width);
		seq_printf(m, "vdisplay: %d\n",
				debug->link->test_video.test_v_height);
		seq_printf(m, "bpc: %u\n",
				dp_link_bit_depth_to_bpc(bpc));
	} else {
		seq_puts(m, "0");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dp_test_data);

static int dp_test_type_show(struct seq_file *m, void *data)
{
	const struct dp_debug_private *debug = m->private;
	const struct drm_connector *connector = debug->connector;

	if (connector->status == connector_status_connected)
		seq_printf(m, "%02x", DP_TEST_LINK_VIDEO_PATTERN);
	else
		seq_puts(m, "0");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dp_test_type);

static ssize_t dp_test_active_write(struct file *file,
		const char __user *ubuf,
		size_t len, loff_t *offp)
{
	char *input_buffer;
	int status = 0;
	const struct dp_debug_private *debug;
	const struct drm_connector *connector;
	int val = 0;

	debug = ((struct seq_file *)file->private_data)->private;
	connector = debug->connector;

	if (len == 0)
		return 0;

	input_buffer = memdup_user_nul(ubuf, len);
	if (IS_ERR(input_buffer))
		return PTR_ERR(input_buffer);

	DRM_DEBUG_DRIVER("Copied %d bytes from user\n", (unsigned int)len);

	if (connector->status == connector_status_connected) {
		status = kstrtoint(input_buffer, 10, &val);
		if (status < 0) {
			kfree(input_buffer);
			return status;
		}
		DRM_DEBUG_DRIVER("Got %d for test active\n", val);
		/* To prevent erroneous activation of the compliance
		 * testing code, only accept an actual value of 1 here
		 */
		if (val == 1)
			debug->panel->video_test = true;
		else
			debug->panel->video_test = false;
	}
	kfree(input_buffer);

	*offp += len;
	return len;
}

static int dp_test_active_show(struct seq_file *m, void *data)
{
	struct dp_debug_private *debug = m->private;
	struct drm_connector *connector = debug->connector;

	if (connector->status == connector_status_connected) {
		if (debug->panel->video_test)
			seq_puts(m, "1");
		else
			seq_puts(m, "0");
	} else {
		seq_puts(m, "0");
	}

	return 0;
}

static int dp_test_active_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, dp_test_active_show,
			inode->i_private);
}

static const struct file_operations test_active_fops = {
	.owner = THIS_MODULE,
	.open = dp_test_active_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = dp_test_active_write
};

static void dp_debug_init(struct dp_debug *dp_debug, struct dentry *root, bool is_edp)
{
	struct dp_debug_private *debug = container_of(dp_debug,
			struct dp_debug_private, dp_debug);

	debugfs_create_file("dp_debug", 0444, root,
			debug, &dp_debug_fops);

	if (!is_edp) {
		debugfs_create_file("msm_dp_test_active", 0444,
				    root,
				    debug, &test_active_fops);

		debugfs_create_file("msm_dp_test_data", 0444,
				    root,
				    debug, &dp_test_data_fops);

		debugfs_create_file("msm_dp_test_type", 0444,
				    root,
				    debug, &dp_test_type_fops);
	}
}

struct dp_debug *dp_debug_get(struct device *dev, struct dp_panel *panel,
		struct dp_link *link,
		struct drm_connector *connector,
		struct dentry *root, bool is_edp)
{
	struct dp_debug_private *debug;
	struct dp_debug *dp_debug;
	int rc;

	if (!dev || !panel || !link) {
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
	debug->link = link;
	debug->panel = panel;

	dp_debug = &debug->dp_debug;
	dp_debug->vdisplay = 0;
	dp_debug->hdisplay = 0;
	dp_debug->vrefresh = 0;

	dp_debug_init(dp_debug, root, is_edp);

	return dp_debug;
 error:
	return ERR_PTR(rc);
}

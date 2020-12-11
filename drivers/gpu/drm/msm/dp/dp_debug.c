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
	struct dentry *root;

	struct dp_usbpd *usbpd;
	struct dp_link *link;
	struct dp_panel *panel;
	struct drm_connector **connector;
	struct device *dev;
	struct drm_device *drm_dev;

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

static int dp_test_data_show(struct seq_file *m, void *data)
{
	struct drm_device *dev;
	struct dp_debug_private *debug;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	u32 bpc;

	debug = m->private;
	dev = debug->drm_dev;
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {

		if (connector->connector_type !=
			DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected) {
			bpc = debug->link->test_video.test_bit_depth;
			seq_printf(m, "hdisplay: %d\n",
					debug->link->test_video.test_h_width);
			seq_printf(m, "vdisplay: %d\n",
					debug->link->test_video.test_v_height);
					seq_printf(m, "bpc: %u\n",
					dp_link_bit_depth_to_bpc(bpc));
		} else
			seq_puts(m, "0");
	}

	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dp_test_data);

static int dp_test_type_show(struct seq_file *m, void *data)
{
	struct dp_debug_private *debug = m->private;
	struct drm_device *dev = debug->drm_dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {

		if (connector->connector_type !=
			DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected)
			seq_printf(m, "%02x", DP_TEST_LINK_VIDEO_PATTERN);
		else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dp_test_type);

static ssize_t dp_test_active_write(struct file *file,
		const char __user *ubuf,
		size_t len, loff_t *offp)
{
	char *input_buffer;
	int status = 0;
	struct dp_debug_private *debug;
	struct drm_device *dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int val = 0;

	debug = ((struct seq_file *)file->private_data)->private;
	dev = debug->drm_dev;

	if (len == 0)
		return 0;

	input_buffer = memdup_user_nul(ubuf, len);
	if (IS_ERR(input_buffer))
		return PTR_ERR(input_buffer);

	DRM_DEBUG_DRIVER("Copied %d bytes from user\n", (unsigned int)len);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type !=
			DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected) {
			status = kstrtoint(input_buffer, 10, &val);
			if (status < 0)
				break;
			DRM_DEBUG_DRIVER("Got %d for test active\n", val);
			/* To prevent erroneous activation of the compliance
			 * testing code, only accept an actual value of 1 here
			 */
			if (val == 1)
				debug->panel->video_test = true;
			else
				debug->panel->video_test = false;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	kfree(input_buffer);
	if (status < 0)
		return status;

	*offp += len;
	return len;
}

static int dp_test_active_show(struct seq_file *m, void *data)
{
	struct dp_debug_private *debug = m->private;
	struct drm_device *dev = debug->drm_dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type !=
			DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected) {
			if (debug->panel->video_test)
				seq_puts(m, "1");
			else
				seq_puts(m, "0");
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static int dp_test_active_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, dp_test_active_show,
			inode->i_private);
}

static const struct file_operations dp_debug_fops = {
	.open = simple_open,
	.read = dp_debug_read_info,
};

static const struct file_operations test_active_fops = {
	.owner = THIS_MODULE,
	.open = dp_test_active_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = dp_test_active_write
};

static int dp_debug_init(struct dp_debug *dp_debug, struct drm_minor *minor)
{
	int rc = 0;
	struct dp_debug_private *debug = container_of(dp_debug,
			struct dp_debug_private, dp_debug);
	struct dentry *file;
	struct dentry *test_active;
	struct dentry *test_data, *test_type;

	file = debugfs_create_file("dp_debug", 0444, minor->debugfs_root,
			debug, &dp_debug_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DRM_ERROR("[%s] debugfs create file failed, rc=%d\n",
				  DEBUG_NAME, rc);
	}

	test_active = debugfs_create_file("msm_dp_test_active", 0444,
			minor->debugfs_root,
			debug, &test_active_fops);
	if (IS_ERR_OR_NULL(test_active)) {
		rc = PTR_ERR(test_active);
		DRM_ERROR("[%s] debugfs test_active failed, rc=%d\n",
				  DEBUG_NAME, rc);
	}

	test_data = debugfs_create_file("msm_dp_test_data", 0444,
			minor->debugfs_root,
			debug, &dp_test_data_fops);
	if (IS_ERR_OR_NULL(test_data)) {
		rc = PTR_ERR(test_data);
		DRM_ERROR("[%s] debugfs test_data failed, rc=%d\n",
				  DEBUG_NAME, rc);
	}

	test_type = debugfs_create_file("msm_dp_test_type", 0444,
			minor->debugfs_root,
			debug, &dp_test_type_fops);
	if (IS_ERR_OR_NULL(test_type)) {
		rc = PTR_ERR(test_type);
		DRM_ERROR("[%s] debugfs test_type failed, rc=%d\n",
				  DEBUG_NAME, rc);
	}

	debug->root = minor->debugfs_root;

	return rc;
}

struct dp_debug *dp_debug_get(struct device *dev, struct dp_panel *panel,
		struct dp_usbpd *usbpd, struct dp_link *link,
		struct drm_connector **connector, struct drm_minor *minor)
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
	debug->drm_dev = minor->dev;
	debug->connector = connector;

	dp_debug = &debug->dp_debug;
	dp_debug->vdisplay = 0;
	dp_debug->hdisplay = 0;
	dp_debug->vrefresh = 0;

	rc = dp_debug_init(dp_debug, minor);
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

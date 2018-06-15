/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/debugfs.h>

#include "dc.h"

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_debugfs.h"

/* function description
 * get/ set DP configuration: lane_count, link_rate, spread_spectrum
 *
 * valid lane count value: 1, 2, 4
 * valid link rate value:
 * 06h = 1.62Gbps per lane
 * 0Ah = 2.7Gbps per lane
 * 0Ch = 3.24Gbps per lane
 * 14h = 5.4Gbps per lane
 * 1Eh = 8.1Gbps per lane
 *
 * debugfs is located at /sys/kernel/debug/dri/0/DP-x/link_settings
 *
 * --- to get dp configuration
 *
 * xxd -l 300 phy_settings
 *
 * It will list current, verified, reported, preferred dp configuration.
 * current -- for current video mode
 * verified --- maximum configuration which pass link training
 * reported --- DP rx report caps (DPCD register offset 0, 1 2)
 * preferred --- user force settings
 *
 * --- set (or force) dp configuration
 *
 * echo <lane_count>  <link_rate>
 *
 * for example, to force to  2 lane, 2.7GHz,
 * echo 4 0xa > link_settings
 *
 * spread_spectrum could not be changed dynamically.
 *
 * in case invalid lane count, link rate are force, no hw programming will be
 * done. please check link settings after force operation to see if HW get
 * programming.
 *
 * xxd -l 300 link_settings
 *
 * check current and preferred settings.
 *
 */
static ssize_t dp_link_settings_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	uint32_t rd_buf_size = 320;
	int bytes_to_user;
	uint8_t str_len = 0;
	int r;

	if (size == 0)
		return 0;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);
	if (!rd_buf)
		return 0;

	rd_buf_ptr = rd_buf;

	str_len = strlen("Current:  %d  %d  %d  ");
	snprintf(rd_buf_ptr, str_len, "Current:  %d  %d  %d  ",
			link->cur_link_settings.lane_count,
			link->cur_link_settings.link_rate,
			link->cur_link_settings.link_spread);
	rd_buf_ptr = rd_buf_ptr + str_len;

	str_len = strlen("Verified:  %d  %d  %d  ");
	snprintf(rd_buf_ptr, str_len, "Verified:  %d  %d  %d  ",
			link->verified_link_cap.lane_count,
			link->verified_link_cap.link_rate,
			link->verified_link_cap.link_spread);
	rd_buf_ptr = rd_buf_ptr + str_len;

	str_len = strlen("Reported:  %d  %d  %d  ");
	snprintf(rd_buf_ptr, str_len, "Reported:  %d  %d  %d  ",
			link->reported_link_cap.lane_count,
			link->reported_link_cap.link_rate,
			link->reported_link_cap.link_spread);
	rd_buf_ptr = rd_buf_ptr + str_len;

	str_len = strlen("Preferred:  %d  %d  %d  ");
	snprintf(rd_buf_ptr, str_len, "Preferred:  %d  %d  %d  ",
			link->preferred_link_setting.lane_count,
			link->preferred_link_setting.link_rate,
			link->preferred_link_setting.link_spread);

	r = copy_to_user(buf, rd_buf, rd_buf_size);

	bytes_to_user = rd_buf_size - r;

	if (r > rd_buf_size) {
		bytes_to_user = 0;
		DRM_DEBUG_DRIVER("data not copy to user");
	}

	kfree(rd_buf);
	return bytes_to_user;
}

static ssize_t dp_link_settings_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	struct dc *dc = (struct dc *)link->dc;
	struct dc_link_settings prefer_link_settings;
	char *wr_buf = NULL;
	char *wr_buf_ptr = NULL;
	uint32_t wr_buf_size = 40;
	int r;
	int bytes_from_user;
	char *sub_str;
	/* 0: lane_count; 1: link_rate */
	uint8_t param_index = 0;
	long param[2];
	const char delimiter[3] = {' ', '\n', '\0'};
	bool valid_input = false;

	if (size == 0)
		return 0;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);
	if (!wr_buf)
		return 0;
	wr_buf_ptr = wr_buf;

	r = copy_from_user(wr_buf_ptr, buf, wr_buf_size);

	/* r is bytes not be copied */
	if (r >= wr_buf_size) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("user data not read\n");
		return 0;
	}

	bytes_from_user = wr_buf_size - r;

	while (isspace(*wr_buf_ptr))
		wr_buf_ptr++;

	while ((*wr_buf_ptr != '\0') && (param_index < 2)) {

		sub_str = strsep(&wr_buf_ptr, delimiter);

		r = kstrtol(sub_str, 16, &param[param_index]);

		if (r)
			DRM_DEBUG_DRIVER(" -EINVAL convert error happens!\n");

		param_index++;
		while (isspace(*wr_buf_ptr))
			wr_buf_ptr++;
	}

	DRM_DEBUG_DRIVER("Lane_count:  %lx\n", param[0]);
	DRM_DEBUG_DRIVER("link_rate:  %lx\n", param[1]);

	switch (param[0]) {
	case LANE_COUNT_ONE:
	case LANE_COUNT_TWO:
	case LANE_COUNT_FOUR:
		valid_input = true;
		break;
	default:
		break;
	}

	switch (param[1]) {
	case LINK_RATE_LOW:
	case LINK_RATE_HIGH:
	case LINK_RATE_RBR2:
	case LINK_RATE_HIGH2:
	case LINK_RATE_HIGH3:
		valid_input = true;
		break;
	default:
		break;
	}

	if (!valid_input) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("Invalid Input value  exceed  No HW will be programmed\n");
		return bytes_from_user;
	}

	/* save user force lane_count, link_rate to preferred settings
	 * spread spectrum will not be changed
	 */
	prefer_link_settings.link_spread = link->cur_link_settings.link_spread;
	prefer_link_settings.lane_count = param[0];
	prefer_link_settings.link_rate = param[1];

	dc_link_set_preferred_link_settings(dc, &prefer_link_settings, link);

	kfree(wr_buf);

	return bytes_from_user;
}

static ssize_t dp_voltage_swing_debugfs_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to read voltage swing */
	return 1;
}

static ssize_t dp_voltage_swing_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to write voltage swing */
	return 1;
}

static ssize_t dp_pre_emphasis_debugfs_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to read pre-emphasis */
	return 1;
}

static ssize_t dp_pre_emphasis_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to write pre-emphasis */
	return 1;
}

static ssize_t dp_phy_test_pattern_debugfs_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to read PHY test pattern */
	return 1;
}

static ssize_t dp_phy_test_pattern_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to write PHY test pattern */
	return 1;
}

static const struct file_operations dp_link_settings_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_link_settings_read,
	.write = dp_link_settings_write,
	.llseek = default_llseek
};

static const struct file_operations dp_voltage_swing_fops = {
	.owner = THIS_MODULE,
	.read = dp_voltage_swing_debugfs_read,
	.write = dp_voltage_swing_debugfs_write,
	.llseek = default_llseek
};

static const struct file_operations dp_pre_emphasis_fops = {
	.owner = THIS_MODULE,
	.read = dp_pre_emphasis_debugfs_read,
	.write = dp_pre_emphasis_debugfs_write,
	.llseek = default_llseek
};

static const struct file_operations dp_phy_test_pattern_fops = {
	.owner = THIS_MODULE,
	.read = dp_phy_test_pattern_debugfs_read,
	.write = dp_phy_test_pattern_debugfs_write,
	.llseek = default_llseek
};

static const struct {
	char *name;
	const struct file_operations *fops;
} dp_debugfs_entries[] = {
		{"link_settings", &dp_link_settings_debugfs_fops},
		{"voltage_swing", &dp_voltage_swing_fops},
		{"pre_emphasis", &dp_pre_emphasis_fops},
		{"phy_test_pattern", &dp_phy_test_pattern_fops}
};

int connector_debugfs_init(struct amdgpu_dm_connector *connector)
{
	int i;
	struct dentry *ent, *dir = connector->base.debugfs_entry;

	if (connector->base.connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
		for (i = 0; i < ARRAY_SIZE(dp_debugfs_entries); i++) {
			ent = debugfs_create_file(dp_debugfs_entries[i].name,
						  0644,
						  dir,
						  connector,
						  dp_debugfs_entries[i].fops);
			if (IS_ERR(ent))
				return PTR_ERR(ent);
		}
	}

	return 0;
}


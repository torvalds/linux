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
#include "dc_link.h"

#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_debugfs.h"

static ssize_t dp_link_rate_debugfs_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to read link rate */
	return 1;
}

static ssize_t dp_link_rate_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to write link rate */
	return 1;
}

static ssize_t dp_lane_count_debugfs_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to read lane count */
	return 1;
}

static ssize_t dp_lane_count_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	/* TODO: create method to write lane count */
	return 1;
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

static const struct file_operations dp_link_rate_fops = {
	.owner = THIS_MODULE,
	.read = dp_link_rate_debugfs_read,
	.write = dp_link_rate_debugfs_write,
	.llseek = default_llseek
};

static const struct file_operations dp_lane_count_fops = {
	.owner = THIS_MODULE,
	.read = dp_lane_count_debugfs_read,
	.write = dp_lane_count_debugfs_write,
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
		{"link_rate", &dp_link_rate_fops},
		{"lane_count", &dp_lane_count_fops},
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


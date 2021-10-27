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

#include <linux/uaccess.h>

#include "dc.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_debugfs.h"
#include "dm_helpers.h"
#include "dmub/dmub_srv.h"
#include "resource.h"
#include "dsc.h"
#include "dc_link_dp.h"
#include "link_hwss.h"
#include "dc/dc_dmub_srv.h"

struct dmub_debugfs_trace_header {
	uint32_t entry_count;
	uint32_t reserved[3];
};

struct dmub_debugfs_trace_entry {
	uint32_t trace_code;
	uint32_t tick_count;
	uint32_t param0;
	uint32_t param1;
};

static inline const char *yesno(bool v)
{
	return v ? "yes" : "no";
}

/* parse_write_buffer_into_params - Helper function to parse debugfs write buffer into an array
 *
 * Function takes in attributes passed to debugfs write entry
 * and writes into param array.
 * The user passes max_param_num to identify maximum number of
 * parameters that could be parsed.
 *
 */
static int parse_write_buffer_into_params(char *wr_buf, uint32_t wr_buf_size,
					  long *param, const char __user *buf,
					  int max_param_num,
					  uint8_t *param_nums)
{
	char *wr_buf_ptr = NULL;
	uint32_t wr_buf_count = 0;
	int r;
	char *sub_str = NULL;
	const char delimiter[3] = {' ', '\n', '\0'};
	uint8_t param_index = 0;

	*param_nums = 0;

	wr_buf_ptr = wr_buf;

	r = copy_from_user(wr_buf_ptr, buf, wr_buf_size);

		/* r is bytes not be copied */
	if (r >= wr_buf_size) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		return -EINVAL;
	}

	/* check number of parameters. isspace could not differ space and \n */
	while ((*wr_buf_ptr != 0xa) && (wr_buf_count < wr_buf_size)) {
		/* skip space*/
		while (isspace(*wr_buf_ptr) && (wr_buf_count < wr_buf_size)) {
			wr_buf_ptr++;
			wr_buf_count++;
			}

		if (wr_buf_count == wr_buf_size)
			break;

		/* skip non-space*/
		while ((!isspace(*wr_buf_ptr)) && (wr_buf_count < wr_buf_size)) {
			wr_buf_ptr++;
			wr_buf_count++;
		}

		(*param_nums)++;

		if (wr_buf_count == wr_buf_size)
			break;
	}

	if (*param_nums > max_param_num)
		*param_nums = max_param_num;

	wr_buf_ptr = wr_buf; /* reset buf pointer */
	wr_buf_count = 0; /* number of char already checked */

	while (isspace(*wr_buf_ptr) && (wr_buf_count < wr_buf_size)) {
		wr_buf_ptr++;
		wr_buf_count++;
	}

	while (param_index < *param_nums) {
		/* after strsep, wr_buf_ptr will be moved to after space */
		sub_str = strsep(&wr_buf_ptr, delimiter);

		r = kstrtol(sub_str, 16, &(param[param_index]));

		if (r)
			DRM_DEBUG_DRIVER("string to int convert error code: %d\n", r);

		param_index++;
	}

	return 0;
}

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
 * cat /sys/kernel/debug/dri/0/DP-x/link_settings
 *
 * It will list current, verified, reported, preferred dp configuration.
 * current -- for current video mode
 * verified --- maximum configuration which pass link training
 * reported --- DP rx report caps (DPCD register offset 0, 1 2)
 * preferred --- user force settings
 *
 * --- set (or force) dp configuration
 *
 * echo <lane_count>  <link_rate> > link_settings
 *
 * for example, to force to  2 lane, 2.7GHz,
 * echo 4 0xa > /sys/kernel/debug/dri/0/DP-x/link_settings
 *
 * spread_spectrum could not be changed dynamically.
 *
 * in case invalid lane count, link rate are force, no hw programming will be
 * done. please check link settings after force operation to see if HW get
 * programming.
 *
 * cat /sys/kernel/debug/dri/0/DP-x/link_settings
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
	const uint32_t rd_buf_size = 100;
	uint32_t result = 0;
	uint8_t str_len = 0;
	int r;

	if (*pos & 3 || size & 3)
		return -EINVAL;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);
	if (!rd_buf)
		return 0;

	rd_buf_ptr = rd_buf;

	str_len = strlen("Current:  %d  0x%x  %d  ");
	snprintf(rd_buf_ptr, str_len, "Current:  %d  0x%x  %d  ",
			link->cur_link_settings.lane_count,
			link->cur_link_settings.link_rate,
			link->cur_link_settings.link_spread);
	rd_buf_ptr += str_len;

	str_len = strlen("Verified:  %d  0x%x  %d  ");
	snprintf(rd_buf_ptr, str_len, "Verified:  %d  0x%x  %d  ",
			link->verified_link_cap.lane_count,
			link->verified_link_cap.link_rate,
			link->verified_link_cap.link_spread);
	rd_buf_ptr += str_len;

	str_len = strlen("Reported:  %d  0x%x  %d  ");
	snprintf(rd_buf_ptr, str_len, "Reported:  %d  0x%x  %d  ",
			link->reported_link_cap.lane_count,
			link->reported_link_cap.link_rate,
			link->reported_link_cap.link_spread);
	rd_buf_ptr += str_len;

	str_len = strlen("Preferred:  %d  0x%x  %d  ");
	snprintf(rd_buf_ptr, str_len, "Preferred:  %d  0x%x  %d\n",
			link->preferred_link_setting.lane_count,
			link->preferred_link_setting.link_rate,
			link->preferred_link_setting.link_spread);

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

static ssize_t dp_link_settings_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	struct dc_link_settings prefer_link_settings;
	char *wr_buf = NULL;
	const uint32_t wr_buf_size = 40;
	/* 0: lane_count; 1: link_rate */
	int max_param_num = 2;
	uint8_t param_nums = 0;
	long param[2];
	bool valid_input = true;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);
	if (!wr_buf)
		return -ENOSPC;

	if (parse_write_buffer_into_params(wr_buf, wr_buf_size,
					   (long *)param, buf,
					   max_param_num,
					   &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("user data not be read\n");
		return -EINVAL;
	}

	switch (param[0]) {
	case LANE_COUNT_ONE:
	case LANE_COUNT_TWO:
	case LANE_COUNT_FOUR:
		break;
	default:
		valid_input = false;
		break;
	}

	switch (param[1]) {
	case LINK_RATE_LOW:
	case LINK_RATE_HIGH:
	case LINK_RATE_RBR2:
	case LINK_RATE_HIGH2:
	case LINK_RATE_HIGH3:
		break;
	default:
		valid_input = false;
		break;
	}

	if (!valid_input) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("Invalid Input value No HW will be programmed\n");
		return size;
	}

	/* save user force lane_count, link_rate to preferred settings
	 * spread spectrum will not be changed
	 */
	prefer_link_settings.link_spread = link->cur_link_settings.link_spread;
	prefer_link_settings.use_link_rate_set = false;
	prefer_link_settings.lane_count = param[0];
	prefer_link_settings.link_rate = param[1];

	dp_retrain_link_dp_test(link, &prefer_link_settings, false);

	kfree(wr_buf);
	return size;
}

/* function: get current DP PHY settings: voltage swing, pre-emphasis,
 * post-cursor2 (defined by VESA DP specification)
 *
 * valid values
 * voltage swing: 0,1,2,3
 * pre-emphasis : 0,1,2,3
 * post cursor2 : 0,1,2,3
 *
 *
 * how to use this debugfs
 *
 * debugfs is located at /sys/kernel/debug/dri/0/DP-x
 *
 * there will be directories, like DP-1, DP-2,DP-3, etc. for DP display
 *
 * To figure out which DP-x is the display for DP to be check,
 * cd DP-x
 * ls -ll
 * There should be debugfs file, like link_settings, phy_settings.
 * cat link_settings
 * from lane_count, link_rate to figure which DP-x is for display to be worked
 * on
 *
 * To get current DP PHY settings,
 * cat phy_settings
 *
 * To change DP PHY settings,
 * echo <voltage_swing> <pre-emphasis> <post_cursor2> > phy_settings
 * for examle, to change voltage swing to 2, pre-emphasis to 3, post_cursor2 to
 * 0,
 * echo 2 3 0 > phy_settings
 *
 * To check if change be applied, get current phy settings by
 * cat phy_settings
 *
 * In case invalid values are set by user, like
 * echo 1 4 0 > phy_settings
 *
 * HW will NOT be programmed by these settings.
 * cat phy_settings will show the previous valid settings.
 */
static ssize_t dp_phy_settings_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	char *rd_buf = NULL;
	const uint32_t rd_buf_size = 20;
	uint32_t result = 0;
	int r;

	if (*pos & 3 || size & 3)
		return -EINVAL;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);
	if (!rd_buf)
		return -EINVAL;

	snprintf(rd_buf, rd_buf_size, "  %d  %d  %d\n",
			link->cur_lane_setting.VOLTAGE_SWING,
			link->cur_lane_setting.PRE_EMPHASIS,
			link->cur_lane_setting.POST_CURSOR2);

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user((*(rd_buf + result)), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

static int dp_lttpr_status_show(struct seq_file *m, void *d)
{
	char *data;
	struct amdgpu_dm_connector *connector = file_inode(m->file)->i_private;
	struct dc_link *link = connector->dc_link;
	uint32_t read_size = 1;
	uint8_t repeater_count = 0;

	data = kzalloc(read_size, GFP_KERNEL);
	if (!data)
		return 0;

	dm_helpers_dp_read_dpcd(link->ctx, link, 0xF0002, data, read_size);

	switch ((uint8_t)*data) {
	case 0x80:
		repeater_count = 1;
		break;
	case 0x40:
		repeater_count = 2;
		break;
	case 0x20:
		repeater_count = 3;
		break;
	case 0x10:
		repeater_count = 4;
		break;
	case 0x8:
		repeater_count = 5;
		break;
	case 0x4:
		repeater_count = 6;
		break;
	case 0x2:
		repeater_count = 7;
		break;
	case 0x1:
		repeater_count = 8;
		break;
	case 0x0:
		repeater_count = 0;
		break;
	default:
		repeater_count = (uint8_t)*data;
		break;
	}

	seq_printf(m, "phy repeater count: %d\n", repeater_count);

	dm_helpers_dp_read_dpcd(link->ctx, link, 0xF0003, data, read_size);

	if ((uint8_t)*data == 0x55)
		seq_printf(m, "phy repeater mode: transparent\n");
	else if ((uint8_t)*data == 0xAA)
		seq_printf(m, "phy repeater mode: non-transparent\n");
	else if ((uint8_t)*data == 0x00)
		seq_printf(m, "phy repeater mode: non lttpr\n");
	else
		seq_printf(m, "phy repeater mode: read error\n");

	kfree(data);
	return 0;
}

static ssize_t dp_phy_settings_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	struct dc *dc = (struct dc *)link->dc;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 40;
	long param[3];
	bool use_prefer_link_setting;
	struct link_training_settings link_lane_settings;
	int max_param_num = 3;
	uint8_t param_nums = 0;
	int r = 0;


	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);
	if (!wr_buf)
		return -ENOSPC;

	if (parse_write_buffer_into_params(wr_buf, size,
					   (long *)param, buf,
					   max_param_num,
					   &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("user data not be read\n");
		return -EINVAL;
	}

	if ((param[0] > VOLTAGE_SWING_MAX_LEVEL) ||
			(param[1] > PRE_EMPHASIS_MAX_LEVEL) ||
			(param[2] > POST_CURSOR2_MAX_LEVEL)) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("Invalid Input No HW will be programmed\n");
		return size;
	}

	/* get link settings: lane count, link rate */
	use_prefer_link_setting =
		((link->preferred_link_setting.link_rate != LINK_RATE_UNKNOWN) &&
		(link->test_pattern_enabled));

	memset(&link_lane_settings, 0, sizeof(link_lane_settings));

	if (use_prefer_link_setting) {
		link_lane_settings.link_settings.lane_count =
				link->preferred_link_setting.lane_count;
		link_lane_settings.link_settings.link_rate =
				link->preferred_link_setting.link_rate;
		link_lane_settings.link_settings.link_spread =
				link->preferred_link_setting.link_spread;
	} else {
		link_lane_settings.link_settings.lane_count =
				link->cur_link_settings.lane_count;
		link_lane_settings.link_settings.link_rate =
				link->cur_link_settings.link_rate;
		link_lane_settings.link_settings.link_spread =
				link->cur_link_settings.link_spread;
	}

	/* apply phy settings from user */
	for (r = 0; r < link_lane_settings.link_settings.lane_count; r++) {
		link_lane_settings.lane_settings[r].VOLTAGE_SWING =
				(enum dc_voltage_swing) (param[0]);
		link_lane_settings.lane_settings[r].PRE_EMPHASIS =
				(enum dc_pre_emphasis) (param[1]);
		link_lane_settings.lane_settings[r].POST_CURSOR2 =
				(enum dc_post_cursor2) (param[2]);
	}

	/* program ASIC registers and DPCD registers */
	dc_link_set_drive_settings(dc, &link_lane_settings, link);

	kfree(wr_buf);
	return size;
}

/* function description
 *
 * set PHY layer or Link layer test pattern
 * PHY test pattern is used for PHY SI check.
 * Link layer test will not affect PHY SI.
 *
 * Reset Test Pattern:
 * 0 = DP_TEST_PATTERN_VIDEO_MODE
 *
 * PHY test pattern supported:
 * 1 = DP_TEST_PATTERN_D102
 * 2 = DP_TEST_PATTERN_SYMBOL_ERROR
 * 3 = DP_TEST_PATTERN_PRBS7
 * 4 = DP_TEST_PATTERN_80BIT_CUSTOM
 * 5 = DP_TEST_PATTERN_CP2520_1
 * 6 = DP_TEST_PATTERN_CP2520_2 = DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE
 * 7 = DP_TEST_PATTERN_CP2520_3
 *
 * DP PHY Link Training Patterns
 * 8 = DP_TEST_PATTERN_TRAINING_PATTERN1
 * 9 = DP_TEST_PATTERN_TRAINING_PATTERN2
 * a = DP_TEST_PATTERN_TRAINING_PATTERN3
 * b = DP_TEST_PATTERN_TRAINING_PATTERN4
 *
 * DP Link Layer Test pattern
 * c = DP_TEST_PATTERN_COLOR_SQUARES
 * d = DP_TEST_PATTERN_COLOR_SQUARES_CEA
 * e = DP_TEST_PATTERN_VERTICAL_BARS
 * f = DP_TEST_PATTERN_HORIZONTAL_BARS
 * 10= DP_TEST_PATTERN_COLOR_RAMP
 *
 * debugfs phy_test_pattern is located at /syskernel/debug/dri/0/DP-x
 *
 * --- set test pattern
 * echo <test pattern #> > test_pattern
 *
 * If test pattern # is not supported, NO HW programming will be done.
 * for DP_TEST_PATTERN_80BIT_CUSTOM, it needs extra 10 bytes of data
 * for the user pattern. input 10 bytes data are separated by space
 *
 * echo 0x4 0x11 0x22 0x33 0x44 0x55 0x66 0x77 0x88 0x99 0xaa > test_pattern
 *
 * --- reset test pattern
 * echo 0 > test_pattern
 *
 * --- HPD detection is disabled when set PHY test pattern
 *
 * when PHY test pattern (pattern # within [1,7]) is set, HPD pin of HW ASIC
 * is disable. User could unplug DP display from DP connected and plug scope to
 * check test pattern PHY SI.
 * If there is need unplug scope and plug DP display back, do steps below:
 * echo 0 > phy_test_pattern
 * unplug scope
 * plug DP display.
 *
 * "echo 0 > phy_test_pattern" will re-enable HPD pin again so that video sw
 * driver could detect "unplug scope" and "plug DP display"
 */
static ssize_t dp_phy_test_pattern_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 100;
	long param[11] = {0x0};
	int max_param_num = 11;
	enum dp_test_pattern test_pattern = DP_TEST_PATTERN_UNSUPPORTED;
	bool disable_hpd = false;
	bool valid_test_pattern = false;
	uint8_t param_nums = 0;
	/* init with default 80bit custom pattern */
	uint8_t custom_pattern[10] = {
			0x1f, 0x7c, 0xf0, 0xc1, 0x07,
			0x1f, 0x7c, 0xf0, 0xc1, 0x07
			};
	struct dc_link_settings prefer_link_settings = {LANE_COUNT_UNKNOWN,
			LINK_RATE_UNKNOWN, LINK_SPREAD_DISABLED};
	struct dc_link_settings cur_link_settings = {LANE_COUNT_UNKNOWN,
			LINK_RATE_UNKNOWN, LINK_SPREAD_DISABLED};
	struct link_training_settings link_training_settings;
	int i;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);
	if (!wr_buf)
		return -ENOSPC;

	if (parse_write_buffer_into_params(wr_buf, size,
					   (long *)param, buf,
					   max_param_num,
					   &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("user data not be read\n");
		return -EINVAL;
	}


	test_pattern = param[0];

	switch (test_pattern) {
	case DP_TEST_PATTERN_VIDEO_MODE:
	case DP_TEST_PATTERN_COLOR_SQUARES:
	case DP_TEST_PATTERN_COLOR_SQUARES_CEA:
	case DP_TEST_PATTERN_VERTICAL_BARS:
	case DP_TEST_PATTERN_HORIZONTAL_BARS:
	case DP_TEST_PATTERN_COLOR_RAMP:
		valid_test_pattern = true;
		break;

	case DP_TEST_PATTERN_D102:
	case DP_TEST_PATTERN_SYMBOL_ERROR:
	case DP_TEST_PATTERN_PRBS7:
	case DP_TEST_PATTERN_80BIT_CUSTOM:
	case DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
	case DP_TEST_PATTERN_TRAINING_PATTERN4:
		disable_hpd = true;
		valid_test_pattern = true;
		break;

	default:
		valid_test_pattern = false;
		test_pattern = DP_TEST_PATTERN_UNSUPPORTED;
		break;
	}

	if (!valid_test_pattern) {
		kfree(wr_buf);
		DRM_DEBUG_DRIVER("Invalid Test Pattern Parameters\n");
		return size;
	}

	if (test_pattern == DP_TEST_PATTERN_80BIT_CUSTOM) {
		for (i = 0; i < 10; i++) {
			if ((uint8_t) param[i + 1] != 0x0)
				break;
		}

		if (i < 10) {
			/* not use default value */
			for (i = 0; i < 10; i++)
				custom_pattern[i] = (uint8_t) param[i + 1];
		}
	}

	/* Usage: set DP physical test pattern using debugfs with normal DP
	 * panel. Then plug out DP panel and connect a scope to measure
	 * For normal video mode and test pattern generated from CRCT,
	 * they are visibile to user. So do not disable HPD.
	 * Video Mode is also set to clear the test pattern, so enable HPD
	 * because it might have been disabled after a test pattern was set.
	 * AUX depends on HPD * sequence dependent, do not move!
	 */
	if (!disable_hpd)
		dc_link_enable_hpd(link);

	prefer_link_settings.lane_count = link->verified_link_cap.lane_count;
	prefer_link_settings.link_rate = link->verified_link_cap.link_rate;
	prefer_link_settings.link_spread = link->verified_link_cap.link_spread;

	cur_link_settings.lane_count = link->cur_link_settings.lane_count;
	cur_link_settings.link_rate = link->cur_link_settings.link_rate;
	cur_link_settings.link_spread = link->cur_link_settings.link_spread;

	link_training_settings.link_settings = cur_link_settings;


	if (test_pattern != DP_TEST_PATTERN_VIDEO_MODE) {
		if (prefer_link_settings.lane_count != LANE_COUNT_UNKNOWN &&
			prefer_link_settings.link_rate !=  LINK_RATE_UNKNOWN &&
			(prefer_link_settings.lane_count != cur_link_settings.lane_count ||
			prefer_link_settings.link_rate != cur_link_settings.link_rate))
			link_training_settings.link_settings = prefer_link_settings;
	}

	for (i = 0; i < (unsigned int)(link_training_settings.link_settings.lane_count); i++)
		link_training_settings.lane_settings[i] = link->cur_lane_setting;

	dc_link_set_test_pattern(
		link,
		test_pattern,
		DP_TEST_PATTERN_COLOR_SPACE_RGB,
		&link_training_settings,
		custom_pattern,
		10);

	/* Usage: Set DP physical test pattern using AMDDP with normal DP panel
	 * Then plug out DP panel and connect a scope to measure DP PHY signal.
	 * Need disable interrupt to avoid SW driver disable DP output. This is
	 * done after the test pattern is set.
	 */
	if (valid_test_pattern && disable_hpd)
		dc_link_disable_hpd(link);

	kfree(wr_buf);

	return size;
}

/*
 * Returns the DMCUB tracebuffer contents.
 * Example usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_dmub_tracebuffer
 */
static int dmub_tracebuffer_show(struct seq_file *m, void *data)
{
	struct amdgpu_device *adev = m->private;
	struct dmub_srv_fb_info *fb_info = adev->dm.dmub_fb_info;
	struct dmub_debugfs_trace_entry *entries;
	uint8_t *tbuf_base;
	uint32_t tbuf_size, max_entries, num_entries, i;

	if (!fb_info)
		return 0;

	tbuf_base = (uint8_t *)fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].cpu_addr;
	if (!tbuf_base)
		return 0;

	tbuf_size = fb_info->fb[DMUB_WINDOW_5_TRACEBUFF].size;
	max_entries = (tbuf_size - sizeof(struct dmub_debugfs_trace_header)) /
		      sizeof(struct dmub_debugfs_trace_entry);

	num_entries =
		((struct dmub_debugfs_trace_header *)tbuf_base)->entry_count;

	num_entries = min(num_entries, max_entries);

	entries = (struct dmub_debugfs_trace_entry
			   *)(tbuf_base +
			      sizeof(struct dmub_debugfs_trace_header));

	for (i = 0; i < num_entries; ++i) {
		struct dmub_debugfs_trace_entry *entry = &entries[i];

		seq_printf(m,
			   "trace_code=%u tick_count=%u param0=%u param1=%u\n",
			   entry->trace_code, entry->tick_count, entry->param0,
			   entry->param1);
	}

	return 0;
}

/*
 * Returns the DMCUB firmware state contents.
 * Example usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_dmub_fw_state
 */
static int dmub_fw_state_show(struct seq_file *m, void *data)
{
	struct amdgpu_device *adev = m->private;
	struct dmub_srv_fb_info *fb_info = adev->dm.dmub_fb_info;
	uint8_t *state_base;
	uint32_t state_size;

	if (!fb_info)
		return 0;

	state_base = (uint8_t *)fb_info->fb[DMUB_WINDOW_6_FW_STATE].cpu_addr;
	if (!state_base)
		return 0;

	state_size = fb_info->fb[DMUB_WINDOW_6_FW_STATE].size;

	return seq_write(m, state_base, state_size);
}

/*
 * Returns the current and maximum output bpc for the connector.
 * Example usage: cat /sys/kernel/debug/dri/0/DP-1/output_bpc
 */
static int output_bpc_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc = NULL;
	struct dm_crtc_state *dm_crtc_state = NULL;
	int res = -ENODEV;
	unsigned int bpc;

	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	crtc = connector->state->crtc;
	if (crtc == NULL)
		goto unlock;

	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state == NULL)
		goto unlock;

	dm_crtc_state = to_dm_crtc_state(crtc->state);
	if (dm_crtc_state->stream == NULL)
		goto unlock;

	switch (dm_crtc_state->stream->timing.display_color_depth) {
	case COLOR_DEPTH_666:
		bpc = 6;
		break;
	case COLOR_DEPTH_888:
		bpc = 8;
		break;
	case COLOR_DEPTH_101010:
		bpc = 10;
		break;
	case COLOR_DEPTH_121212:
		bpc = 12;
		break;
	case COLOR_DEPTH_161616:
		bpc = 16;
		break;
	default:
		goto unlock;
	}

	seq_printf(m, "Current: %u\n", bpc);
	seq_printf(m, "Maximum: %u\n", connector->display_info.bpc);
	res = 0;

unlock:
	if (crtc)
		drm_modeset_unlock(&crtc->mutex);

	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);

	return res;
}

/*
 * Example usage:
 * Disable dsc passthrough, i.e.,: have dsc decoding at converver, not external RX
 *   echo 1 /sys/kernel/debug/dri/0/DP-1/dsc_disable_passthrough
 * Enable dsc passthrough, i.e.,: have dsc passthrough to external RX
 *   echo 0 /sys/kernel/debug/dri/0/DP-1/dsc_disable_passthrough
 */
static ssize_t dp_dsc_passthrough_set(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	long param;
	uint8_t param_nums = 0;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
					   &param, buf,
					   max_param_num,
					   &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	aconnector->dsc_settings.dsc_force_disable_passthrough = param;

	kfree(wr_buf);
	return 0;
}

#ifdef CONFIG_DRM_AMD_DC_HDCP
/*
 * Returns the HDCP capability of the Display (1.4 for now).
 *
 * NOTE* Not all HDMI displays report their HDCP caps even when they are capable.
 * Since its rare for a display to not be HDCP 1.4 capable, we set HDMI as always capable.
 *
 * Example usage: cat /sys/kernel/debug/dri/0/DP-1/hdcp_sink_capability
 *		or cat /sys/kernel/debug/dri/0/HDMI-A-1/hdcp_sink_capability
 */
static int hdcp_sink_capability_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	bool hdcp_cap, hdcp2_cap;

	if (connector->status != connector_status_connected)
		return -ENODEV;

	seq_printf(m, "%s:%d HDCP version: ", connector->name, connector->base.id);

	hdcp_cap = dc_link_is_hdcp14(aconnector->dc_link, aconnector->dc_sink->sink_signal);
	hdcp2_cap = dc_link_is_hdcp22(aconnector->dc_link, aconnector->dc_sink->sink_signal);


	if (hdcp_cap)
		seq_printf(m, "%s ", "HDCP1.4");
	if (hdcp2_cap)
		seq_printf(m, "%s ", "HDCP2.2");

	if (!hdcp_cap && !hdcp2_cap)
		seq_printf(m, "%s ", "None");

	seq_puts(m, "\n");

	return 0;
}
#endif

/*
 * Returns whether the connected display is internal and not hotpluggable.
 * Example usage: cat /sys/kernel/debug/dri/0/DP-1/internal_display
 */
static int internal_display_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	struct dc_link *link = aconnector->dc_link;

	seq_printf(m, "Internal: %u\n", link->is_internal_display);

	return 0;
}

/* function description
 *
 * generic SDP message access for testing
 *
 * debugfs sdp_message is located at /syskernel/debug/dri/0/DP-x
 *
 * SDP header
 * Hb0 : Secondary-Data Packet ID
 * Hb1 : Secondary-Data Packet type
 * Hb2 : Secondary-Data-packet-specific header, Byte 0
 * Hb3 : Secondary-Data-packet-specific header, Byte 1
 *
 * for using custom sdp message: input 4 bytes SDP header and 32 bytes raw data
 */
static ssize_t dp_sdp_message_debugfs_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	int r;
	uint8_t data[36];
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dm_crtc_state *acrtc_state;
	uint32_t write_size = 36;

	if (connector->base.status != connector_status_connected)
		return -ENODEV;

	if (size == 0)
		return 0;

	acrtc_state = to_dm_crtc_state(connector->base.state->crtc->state);

	r = copy_from_user(data, buf, write_size);

	write_size -= r;

	dc_stream_send_dp_sdp(acrtc_state->stream, data, write_size);

	return write_size;
}

static ssize_t dp_dpcd_address_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	int r;
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;

	if (size < sizeof(connector->debugfs_dpcd_address))
		return -EINVAL;

	r = copy_from_user(&connector->debugfs_dpcd_address,
			buf, sizeof(connector->debugfs_dpcd_address));

	return size - r;
}

static ssize_t dp_dpcd_size_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	int r;
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;

	if (size < sizeof(connector->debugfs_dpcd_size))
		return -EINVAL;

	r = copy_from_user(&connector->debugfs_dpcd_size,
			buf, sizeof(connector->debugfs_dpcd_size));

	if (connector->debugfs_dpcd_size > 256)
		connector->debugfs_dpcd_size = 0;

	return size - r;
}

static ssize_t dp_dpcd_data_write(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos)
{
	int r;
	char *data;
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	uint32_t write_size = connector->debugfs_dpcd_size;

	if (!write_size || size < write_size)
		return -EINVAL;

	data = kzalloc(write_size, GFP_KERNEL);
	if (!data)
		return 0;

	r = copy_from_user(data, buf, write_size);

	dm_helpers_dp_write_dpcd(link->ctx, link,
			connector->debugfs_dpcd_address, data, write_size - r);
	kfree(data);
	return write_size - r;
}

static ssize_t dp_dpcd_data_read(struct file *f, char __user *buf,
				 size_t size, loff_t *pos)
{
	int r;
	char *data;
	struct amdgpu_dm_connector *connector = file_inode(f)->i_private;
	struct dc_link *link = connector->dc_link;
	uint32_t read_size = connector->debugfs_dpcd_size;

	if (!read_size || size < read_size)
		return 0;

	data = kzalloc(read_size, GFP_KERNEL);
	if (!data)
		return 0;

	dm_helpers_dp_read_dpcd(link->ctx, link,
			connector->debugfs_dpcd_address, data, read_size);

	r = copy_to_user(buf, data, read_size);

	kfree(data);
	return read_size - r;
}

/* function: Read link's DSC & FEC capabilities
 *
 *
 * Access it with the following command (you need to specify
 * connector like DP-1):
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dp_dsc_fec_support
 *
 */
static int dp_dsc_fec_support_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_device *dev = connector->dev;
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(connector);
	int ret = 0;
	bool try_again = false;
	bool is_fec_supported = false;
	bool is_dsc_supported = false;
	struct dpcd_caps dpcd_caps;

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);
	do {
		try_again = false;
		ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
		if (ret) {
			if (ret == -EDEADLK) {
				ret = drm_modeset_backoff(&ctx);
				if (!ret) {
					try_again = true;
					continue;
				}
			}
			break;
		}
		if (connector->status != connector_status_connected) {
			ret = -ENODEV;
			break;
		}
		dpcd_caps = aconnector->dc_link->dpcd_caps;
		if (aconnector->port) {
			/* aconnector sets dsc_aux during get_modes call
			 * if MST connector has it means it can either
			 * enable DSC on the sink device or on MST branch
			 * its connected to.
			 */
			if (aconnector->dsc_aux) {
				is_fec_supported = true;
				is_dsc_supported = true;
			}
		} else {
			is_fec_supported = dpcd_caps.fec_cap.raw & 0x1;
			is_dsc_supported = dpcd_caps.dsc_caps.dsc_basic_caps.raw[0] & 0x1;
		}
	} while (try_again);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	seq_printf(m, "FEC_Sink_Support: %s\n", yesno(is_fec_supported));
	seq_printf(m, "DSC_Sink_Support: %s\n", yesno(is_dsc_supported));

	return ret;
}

/* function: Trigger virtual HPD redetection on connector
 *
 * This function will perform link rediscovery, link disable
 * and enable, and dm connector state update.
 *
 * Retrigger HPD on an existing connector by echoing 1 into
 * its respectful "trigger_hotplug" debugfs entry:
 *
 *	echo 1 > /sys/kernel/debug/dri/0/DP-X/trigger_hotplug
 *
 * This function can perform HPD unplug:
 *
 *	echo 0 > /sys/kernel/debug/dri/0/DP-X/trigger_hotplug
 *
 */
static ssize_t trigger_hotplug(struct file *f, const char __user *buf,
							size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct drm_connector *connector = &aconnector->base;
	struct dc_link *link = NULL;
	struct drm_device *dev = connector->dev;
	enum dc_connection_type new_connection_type = dc_connection_none;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	long param[1] = {0};
	uint8_t param_nums = 0;

	if (!aconnector || !aconnector->dc_link)
		return -EINVAL;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
						(long *)param, buf,
						max_param_num,
						&param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param[0] == 1) {
		mutex_lock(&aconnector->hpd_lock);

		if (!dc_link_detect_sink(aconnector->dc_link, &new_connection_type) &&
			new_connection_type != dc_connection_none)
			goto unlock;

		if (!dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD))
			goto unlock;

		amdgpu_dm_update_connector_after_detect(aconnector);

		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		drm_kms_helper_hotplug_event(dev);
	} else if (param[0] == 0) {
		if (!aconnector->dc_link)
			goto unlock;

		link = aconnector->dc_link;

		if (link->local_sink) {
			dc_sink_release(link->local_sink);
			link->local_sink = NULL;
		}

		link->dpcd_sink_count = 0;
		link->type = dc_connection_none;
		link->dongle_max_pix_clk = 0;

		amdgpu_dm_update_connector_after_detect(aconnector);

		drm_modeset_lock_all(dev);
		dm_restore_drm_connector_state(dev, connector);
		drm_modeset_unlock_all(dev);

		drm_kms_helper_hotplug_event(dev);
	}

unlock:
	mutex_unlock(&aconnector->hpd_lock);

	kfree(wr_buf);
	return size;
}

/* function: read DSC status on the connector
 *
 * The read function: dp_dsc_clock_en_read
 * returns current status of DSC clock on the connector.
 * The return is a boolean flag: 1 or 0.
 *
 * Access it with the following command (you need to specify
 * connector like DP-1):
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_clock_en
 *
 * Expected output:
 * 1 - means that DSC is currently enabled
 * 0 - means that DSC is disabled
 */
static ssize_t dp_dsc_clock_en_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 10;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_clock_en);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

/* function: write force DSC on the connector
 *
 * The write function: dp_dsc_clock_en_write
 * enables to force DSC on the connector.
 * User can write to either force enable or force disable DSC
 * on the next modeset or set it to driver default
 *
 * Accepted inputs:
 * 0 - default DSC enablement policy
 * 1 - force enable DSC on the connector
 * 2 - force disable DSC on the connector (might cause fail in atomic_check)
 *
 * Writing DSC settings is done with the following command:
 * - To force enable DSC (you need to specify
 * connector like DP-1):
 *
 *	echo 0x1 > /sys/kernel/debug/dri/0/DP-X/dsc_clock_en
 *
 * - To return to default state set the flag to zero and
 * let driver deal with DSC automatically
 * (you need to specify connector like DP-1):
 *
 *	echo 0x0 > /sys/kernel/debug/dri/0/DP-X/dsc_clock_en
 *
 */
static ssize_t dp_dsc_clock_en_write(struct file *f, const char __user *buf,
				     size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc = NULL;
	struct dm_crtc_state *dm_crtc_state = NULL;
	struct pipe_ctx *pipe_ctx;
	int i;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	long param[1] = {0};
	uint8_t param_nums = 0;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
					    (long *)param, buf,
					    max_param_num,
					    &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx || !pipe_ctx->stream)
		goto done;

	// Get CRTC state
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	crtc = connector->state->crtc;
	if (crtc == NULL)
		goto unlock;

	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state == NULL)
		goto unlock;

	dm_crtc_state = to_dm_crtc_state(crtc->state);
	if (dm_crtc_state->stream == NULL)
		goto unlock;

	if (param[0] == 1)
		aconnector->dsc_settings.dsc_force_enable = DSC_CLK_FORCE_ENABLE;
	else if (param[0] == 2)
		aconnector->dsc_settings.dsc_force_enable = DSC_CLK_FORCE_DISABLE;
	else
		aconnector->dsc_settings.dsc_force_enable = DSC_CLK_FORCE_DEFAULT;

	dm_crtc_state->dsc_force_changed = true;

unlock:
	if (crtc)
		drm_modeset_unlock(&crtc->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);

done:
	kfree(wr_buf);
	return size;
}

/* function: read DSC slice width parameter on the connector
 *
 * The read function: dp_dsc_slice_width_read
 * returns dsc slice width used in the current configuration
 * The return is an integer: 0 or other positive number
 *
 * Access the status with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_slice_width
 *
 * 0 - means that DSC is disabled
 *
 * Any other number more than zero represents the
 * slice width currently used by DSC in pixels
 *
 */
static ssize_t dp_dsc_slice_width_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_slice_width);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

/* function: write DSC slice width parameter
 *
 * The write function: dp_dsc_slice_width_write
 * overwrites automatically generated DSC configuration
 * of slice width.
 *
 * The user has to write the slice width divisible by the
 * picture width.
 *
 * Also the user has to write width in hexidecimal
 * rather than in decimal.
 *
 * Writing DSC settings is done with the following command:
 * - To force overwrite slice width: (example sets to 1920 pixels)
 *
 *	echo 0x780 > /sys/kernel/debug/dri/0/DP-X/dsc_slice_width
 *
 *  - To stop overwriting and let driver find the optimal size,
 * set the width to zero:
 *
 *	echo 0x0 > /sys/kernel/debug/dri/0/DP-X/dsc_slice_width
 *
 */
static ssize_t dp_dsc_slice_width_write(struct file *f, const char __user *buf,
				     size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct pipe_ctx *pipe_ctx;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc = NULL;
	struct dm_crtc_state *dm_crtc_state = NULL;
	int i;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	long param[1] = {0};
	uint8_t param_nums = 0;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
					    (long *)param, buf,
					    max_param_num,
					    &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx || !pipe_ctx->stream)
		goto done;

	// Safely get CRTC state
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	crtc = connector->state->crtc;
	if (crtc == NULL)
		goto unlock;

	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state == NULL)
		goto unlock;

	dm_crtc_state = to_dm_crtc_state(crtc->state);
	if (dm_crtc_state->stream == NULL)
		goto unlock;

	if (param[0] > 0)
		aconnector->dsc_settings.dsc_num_slices_h = DIV_ROUND_UP(
					pipe_ctx->stream->timing.h_addressable,
					param[0]);
	else
		aconnector->dsc_settings.dsc_num_slices_h = 0;

	dm_crtc_state->dsc_force_changed = true;

unlock:
	if (crtc)
		drm_modeset_unlock(&crtc->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);

done:
	kfree(wr_buf);
	return size;
}

/* function: read DSC slice height parameter on the connector
 *
 * The read function: dp_dsc_slice_height_read
 * returns dsc slice height used in the current configuration
 * The return is an integer: 0 or other positive number
 *
 * Access the status with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_slice_height
 *
 * 0 - means that DSC is disabled
 *
 * Any other number more than zero represents the
 * slice height currently used by DSC in pixels
 *
 */
static ssize_t dp_dsc_slice_height_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_slice_height);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

/* function: write DSC slice height parameter
 *
 * The write function: dp_dsc_slice_height_write
 * overwrites automatically generated DSC configuration
 * of slice height.
 *
 * The user has to write the slice height divisible by the
 * picture height.
 *
 * Also the user has to write height in hexidecimal
 * rather than in decimal.
 *
 * Writing DSC settings is done with the following command:
 * - To force overwrite slice height (example sets to 128 pixels):
 *
 *	echo 0x80 > /sys/kernel/debug/dri/0/DP-X/dsc_slice_height
 *
 *  - To stop overwriting and let driver find the optimal size,
 * set the height to zero:
 *
 *	echo 0x0 > /sys/kernel/debug/dri/0/DP-X/dsc_slice_height
 *
 */
static ssize_t dp_dsc_slice_height_write(struct file *f, const char __user *buf,
				     size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc = NULL;
	struct dm_crtc_state *dm_crtc_state = NULL;
	struct pipe_ctx *pipe_ctx;
	int i;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	uint8_t param_nums = 0;
	long param[1] = {0};

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
					    (long *)param, buf,
					    max_param_num,
					    &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx || !pipe_ctx->stream)
		goto done;

	// Get CRTC state
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	crtc = connector->state->crtc;
	if (crtc == NULL)
		goto unlock;

	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state == NULL)
		goto unlock;

	dm_crtc_state = to_dm_crtc_state(crtc->state);
	if (dm_crtc_state->stream == NULL)
		goto unlock;

	if (param[0] > 0)
		aconnector->dsc_settings.dsc_num_slices_v = DIV_ROUND_UP(
					pipe_ctx->stream->timing.v_addressable,
					param[0]);
	else
		aconnector->dsc_settings.dsc_num_slices_v = 0;

	dm_crtc_state->dsc_force_changed = true;

unlock:
	if (crtc)
		drm_modeset_unlock(&crtc->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);

done:
	kfree(wr_buf);
	return size;
}

/* function: read DSC target rate on the connector in bits per pixel
 *
 * The read function: dp_dsc_bits_per_pixel_read
 * returns target rate of compression in bits per pixel
 * The return is an integer: 0 or other positive integer
 *
 * Access it with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_bits_per_pixel
 *
 *  0 - means that DSC is disabled
 */
static ssize_t dp_dsc_bits_per_pixel_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_bits_per_pixel);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

/* function: write DSC target rate in bits per pixel
 *
 * The write function: dp_dsc_bits_per_pixel_write
 * overwrites automatically generated DSC configuration
 * of DSC target bit rate.
 *
 * Also the user has to write bpp in hexidecimal
 * rather than in decimal.
 *
 * Writing DSC settings is done with the following command:
 * - To force overwrite rate (example sets to 256 bpp x 1/16):
 *
 *	echo 0x100 > /sys/kernel/debug/dri/0/DP-X/dsc_bits_per_pixel
 *
 *  - To stop overwriting and let driver find the optimal rate,
 * set the rate to zero:
 *
 *	echo 0x0 > /sys/kernel/debug/dri/0/DP-X/dsc_bits_per_pixel
 *
 */
static ssize_t dp_dsc_bits_per_pixel_write(struct file *f, const char __user *buf,
				     size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct drm_crtc *crtc = NULL;
	struct dm_crtc_state *dm_crtc_state = NULL;
	struct pipe_ctx *pipe_ctx;
	int i;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	uint8_t param_nums = 0;
	long param[1] = {0};

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
					    (long *)param, buf,
					    max_param_num,
					    &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx || !pipe_ctx->stream)
		goto done;

	// Get CRTC state
	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	crtc = connector->state->crtc;
	if (crtc == NULL)
		goto unlock;

	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state == NULL)
		goto unlock;

	dm_crtc_state = to_dm_crtc_state(crtc->state);
	if (dm_crtc_state->stream == NULL)
		goto unlock;

	aconnector->dsc_settings.dsc_bits_per_pixel = param[0];

	dm_crtc_state->dsc_force_changed = true;

unlock:
	if (crtc)
		drm_modeset_unlock(&crtc->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);

done:
	kfree(wr_buf);
	return size;
}

/* function: read DSC picture width parameter on the connector
 *
 * The read function: dp_dsc_pic_width_read
 * returns dsc picture width used in the current configuration
 * It is the same as h_addressable of the current
 * display's timing
 * The return is an integer: 0 or other positive integer
 * If 0 then DSC is disabled.
 *
 * Access it with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_pic_width
 *
 * 0 - means that DSC is disabled
 */
static ssize_t dp_dsc_pic_width_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_pic_width);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

static ssize_t dp_dsc_pic_height_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_pic_height);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

/* function: read DSC chunk size parameter on the connector
 *
 * The read function: dp_dsc_chunk_size_read
 * returns dsc chunk size set in the current configuration
 * The value is calculated automatically by DSC code
 * and depends on slice parameters and bpp target rate
 * The return is an integer: 0 or other positive integer
 * If 0 then DSC is disabled.
 *
 * Access it with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_chunk_size
 *
 * 0 - means that DSC is disabled
 */
static ssize_t dp_dsc_chunk_size_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_chunk_size);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

/* function: read DSC slice bpg offset on the connector
 *
 * The read function: dp_dsc_slice_bpg_offset_read
 * returns dsc bpg slice offset set in the current configuration
 * The value is calculated automatically by DSC code
 * and depends on slice parameters and bpp target rate
 * The return is an integer: 0 or other positive integer
 * If 0 then DSC is disabled.
 *
 * Access it with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/dsc_slice_bpg_offset
 *
 * 0 - means that DSC is disabled
 */
static ssize_t dp_dsc_slice_bpg_offset_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct display_stream_compressor *dsc;
	struct dcn_dsc_state dsc_state = {0};
	const uint32_t rd_buf_size = 100;
	struct pipe_ctx *pipe_ctx;
	ssize_t result = 0;
	int i, r, str_len = 30;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	rd_buf_ptr = rd_buf;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_ctx = &aconnector->dc_link->dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx && pipe_ctx->stream &&
			    pipe_ctx->stream->link == aconnector->dc_link)
				break;
	}

	if (!pipe_ctx)
		return -ENXIO;

	dsc = pipe_ctx->stream_res.dsc;
	if (dsc)
		dsc->funcs->dsc_read_state(dsc, &dsc_state);

	snprintf(rd_buf_ptr, str_len,
		"%d\n",
		dsc_state.dsc_slice_bpg_offset);
	rd_buf_ptr += str_len;

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */

		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}


/*
 * function description: Read max_requested_bpc property from the connector
 *
 * Access it with the following command:
 *
 *	cat /sys/kernel/debug/dri/0/DP-X/max_bpc
 *
 */
static ssize_t dp_max_bpc_read(struct file *f, char __user *buf,
		size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct drm_connector *connector = &aconnector->base;
	struct drm_device *dev = connector->dev;
	struct dm_connector_state *state;
	ssize_t result = 0;
	char *rd_buf = NULL;
	char *rd_buf_ptr = NULL;
	const uint32_t rd_buf_size = 10;
	int r;

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);

	if (!rd_buf)
		return -ENOMEM;

	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	state = to_dm_connector_state(connector->state);

	rd_buf_ptr = rd_buf;
	snprintf(rd_buf_ptr, rd_buf_size,
		"%u\n",
		state->base.max_requested_bpc);

	while (size) {
		if (*pos >= rd_buf_size)
			break;

		r = put_user(*(rd_buf + result), buf);
		if (r) {
			result = r; /* r = -EFAULT */
			goto unlock;
		}
		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}
unlock:
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);
	kfree(rd_buf);
	return result;
}


/*
 * function description: Set max_requested_bpc property on the connector
 *
 * This function will not force the input BPC on connector, it will only
 * change the max value. This is equivalent to setting max_bpc through
 * xrandr.
 *
 * The BPC value written must be >= 6 and <= 16. Values outside of this
 * range will result in errors.
 *
 * BPC values:
 *	0x6 - 6 BPC
 *	0x8 - 8 BPC
 *	0xa - 10 BPC
 *	0xc - 12 BPC
 *	0x10 - 16 BPC
 *
 * Write the max_bpc in the following way:
 *
 * echo 0x6 > /sys/kernel/debug/dri/0/DP-X/max_bpc
 *
 */
static ssize_t dp_max_bpc_write(struct file *f, const char __user *buf,
				     size_t size, loff_t *pos)
{
	struct amdgpu_dm_connector *aconnector = file_inode(f)->i_private;
	struct drm_connector *connector = &aconnector->base;
	struct dm_connector_state *state;
	struct drm_device *dev = connector->dev;
	char *wr_buf = NULL;
	uint32_t wr_buf_size = 42;
	int max_param_num = 1;
	long param[1] = {0};
	uint8_t param_nums = 0;

	if (size == 0)
		return -EINVAL;

	wr_buf = kcalloc(wr_buf_size, sizeof(char), GFP_KERNEL);

	if (!wr_buf) {
		DRM_DEBUG_DRIVER("no memory to allocate write buffer\n");
		return -ENOSPC;
	}

	if (parse_write_buffer_into_params(wr_buf, size,
					   (long *)param, buf,
					   max_param_num,
					   &param_nums)) {
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param_nums <= 0) {
		DRM_DEBUG_DRIVER("user data not be read\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	if (param[0] < 6 || param[0] > 16) {
		DRM_DEBUG_DRIVER("bad max_bpc value\n");
		kfree(wr_buf);
		return -EINVAL;
	}

	mutex_lock(&dev->mode_config.mutex);
	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	if (connector->state == NULL)
		goto unlock;

	state = to_dm_connector_state(connector->state);
	state->base.max_requested_bpc = param[0];
unlock:
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
	mutex_unlock(&dev->mode_config.mutex);

	kfree(wr_buf);
	return size;
}

/*
 * Backlight at this moment.  Read only.
 * As written to display, taking ABM and backlight lut into account.
 * Ranges from 0x0 to 0x10000 (= 100% PWM)
 *
 * Example usage: cat /sys/kernel/debug/dri/0/eDP-1/current_backlight
 */
static int current_backlight_show(struct seq_file *m, void *unused)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(m->private);
	struct dc_link *link = aconnector->dc_link;
	unsigned int backlight;

	backlight = dc_link_get_backlight_level(link);
	seq_printf(m, "0x%x\n", backlight);

	return 0;
}

/*
 * Backlight value that is being approached.  Read only.
 * As written to display, taking ABM and backlight lut into account.
 * Ranges from 0x0 to 0x10000 (= 100% PWM)
 *
 * Example usage: cat /sys/kernel/debug/dri/0/eDP-1/target_backlight
 */
static int target_backlight_show(struct seq_file *m, void *unused)
{
	struct amdgpu_dm_connector *aconnector = to_amdgpu_dm_connector(m->private);
	struct dc_link *link = aconnector->dc_link;
	unsigned int backlight;

	backlight = dc_link_get_target_backlight_pwm(link);
	seq_printf(m, "0x%x\n", backlight);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dp_dsc_fec_support);
DEFINE_SHOW_ATTRIBUTE(dmub_fw_state);
DEFINE_SHOW_ATTRIBUTE(dmub_tracebuffer);
DEFINE_SHOW_ATTRIBUTE(output_bpc);
DEFINE_SHOW_ATTRIBUTE(dp_lttpr_status);
#ifdef CONFIG_DRM_AMD_DC_HDCP
DEFINE_SHOW_ATTRIBUTE(hdcp_sink_capability);
#endif
DEFINE_SHOW_ATTRIBUTE(internal_display);

static const struct file_operations dp_dsc_clock_en_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_clock_en_read,
	.write = dp_dsc_clock_en_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_slice_width_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_slice_width_read,
	.write = dp_dsc_slice_width_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_slice_height_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_slice_height_read,
	.write = dp_dsc_slice_height_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_bits_per_pixel_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_bits_per_pixel_read,
	.write = dp_dsc_bits_per_pixel_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_pic_width_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_pic_width_read,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_pic_height_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_pic_height_read,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_chunk_size_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_chunk_size_read,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_slice_bpg_offset_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dsc_slice_bpg_offset_read,
	.llseek = default_llseek
};

static const struct file_operations trigger_hotplug_debugfs_fops = {
	.owner = THIS_MODULE,
	.write = trigger_hotplug,
	.llseek = default_llseek
};

static const struct file_operations dp_link_settings_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_link_settings_read,
	.write = dp_link_settings_write,
	.llseek = default_llseek
};

static const struct file_operations dp_phy_settings_debugfs_fop = {
	.owner = THIS_MODULE,
	.read = dp_phy_settings_read,
	.write = dp_phy_settings_write,
	.llseek = default_llseek
};

static const struct file_operations dp_phy_test_pattern_fops = {
	.owner = THIS_MODULE,
	.write = dp_phy_test_pattern_debugfs_write,
	.llseek = default_llseek
};

static const struct file_operations sdp_message_fops = {
	.owner = THIS_MODULE,
	.write = dp_sdp_message_debugfs_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dpcd_address_debugfs_fops = {
	.owner = THIS_MODULE,
	.write = dp_dpcd_address_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dpcd_size_debugfs_fops = {
	.owner = THIS_MODULE,
	.write = dp_dpcd_size_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dpcd_data_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_dpcd_data_read,
	.write = dp_dpcd_data_write,
	.llseek = default_llseek
};

static const struct file_operations dp_max_bpc_debugfs_fops = {
	.owner = THIS_MODULE,
	.read = dp_max_bpc_read,
	.write = dp_max_bpc_write,
	.llseek = default_llseek
};

static const struct file_operations dp_dsc_disable_passthrough_debugfs_fops = {
	.owner = THIS_MODULE,
	.write = dp_dsc_passthrough_set,
	.llseek = default_llseek
};

static const struct {
	char *name;
	const struct file_operations *fops;
} dp_debugfs_entries[] = {
		{"link_settings", &dp_link_settings_debugfs_fops},
		{"phy_settings", &dp_phy_settings_debugfs_fop},
		{"lttpr_status", &dp_lttpr_status_fops},
		{"test_pattern", &dp_phy_test_pattern_fops},
#ifdef CONFIG_DRM_AMD_DC_HDCP
		{"hdcp_sink_capability", &hdcp_sink_capability_fops},
#endif
		{"sdp_message", &sdp_message_fops},
		{"aux_dpcd_address", &dp_dpcd_address_debugfs_fops},
		{"aux_dpcd_size", &dp_dpcd_size_debugfs_fops},
		{"aux_dpcd_data", &dp_dpcd_data_debugfs_fops},
		{"dsc_clock_en", &dp_dsc_clock_en_debugfs_fops},
		{"dsc_slice_width", &dp_dsc_slice_width_debugfs_fops},
		{"dsc_slice_height", &dp_dsc_slice_height_debugfs_fops},
		{"dsc_bits_per_pixel", &dp_dsc_bits_per_pixel_debugfs_fops},
		{"dsc_pic_width", &dp_dsc_pic_width_debugfs_fops},
		{"dsc_pic_height", &dp_dsc_pic_height_debugfs_fops},
		{"dsc_chunk_size", &dp_dsc_chunk_size_debugfs_fops},
		{"dsc_slice_bpg", &dp_dsc_slice_bpg_offset_debugfs_fops},
		{"dp_dsc_fec_support", &dp_dsc_fec_support_fops},
		{"max_bpc", &dp_max_bpc_debugfs_fops},
		{"dsc_disable_passthrough", &dp_dsc_disable_passthrough_debugfs_fops},
};

#ifdef CONFIG_DRM_AMD_DC_HDCP
static const struct {
	char *name;
	const struct file_operations *fops;
} hdmi_debugfs_entries[] = {
		{"hdcp_sink_capability", &hdcp_sink_capability_fops}
};
#endif
/*
 * Force YUV420 output if available from the given mode
 */
static int force_yuv420_output_set(void *data, u64 val)
{
	struct amdgpu_dm_connector *connector = data;

	connector->force_yuv420_output = (bool)val;

	return 0;
}

/*
 * Check if YUV420 is forced when available from the given mode
 */
static int force_yuv420_output_get(void *data, u64 *val)
{
	struct amdgpu_dm_connector *connector = data;

	*val = connector->force_yuv420_output;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(force_yuv420_output_fops, force_yuv420_output_get,
			 force_yuv420_output_set, "%llu\n");

/*
 *  Read PSR state
 */
static int psr_get(void *data, u64 *val)
{
	struct amdgpu_dm_connector *connector = data;
	struct dc_link *link = connector->dc_link;
	enum dc_psr_state state = PSR_STATE0;

	dc_link_get_psr_state(link, &state);

	*val = state;

	return 0;
}

/*
 * Set dmcub trace event IRQ enable or disable.
 * Usage to enable dmcub trace event IRQ: echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_dmcub_trace_event_en
 * Usage to disable dmcub trace event IRQ: echo 0 > /sys/kernel/debug/dri/0/amdgpu_dm_dmcub_trace_event_en
 */
static int dmcub_trace_event_state_set(void *data, u64 val)
{
	struct amdgpu_device *adev = data;

	if (val == 1 || val == 0) {
		dc_dmub_trace_event_control(adev->dm.dc, val);
		adev->dm.dmcub_trace_event_en = (bool)val;
	} else
		return 0;

	return 0;
}

/*
 * The interface doesn't need get function, so it will return the
 * value of zero
 * Usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_dmcub_trace_event_en
 */
static int dmcub_trace_event_state_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = data;

	*val = adev->dm.dmcub_trace_event_en;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(dmcub_trace_event_state_fops, dmcub_trace_event_state_get,
			 dmcub_trace_event_state_set, "%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(psr_fops, psr_get, NULL, "%llu\n");

DEFINE_SHOW_ATTRIBUTE(current_backlight);
DEFINE_SHOW_ATTRIBUTE(target_backlight);

static const struct {
	char *name;
	const struct file_operations *fops;
} connector_debugfs_entries[] = {
		{"force_yuv420_output", &force_yuv420_output_fops},
		{"output_bpc", &output_bpc_fops},
		{"trigger_hotplug", &trigger_hotplug_debugfs_fops},
		{"internal_display", &internal_display_fops}
};

void connector_debugfs_init(struct amdgpu_dm_connector *connector)
{
	int i;
	struct dentry *dir = connector->base.debugfs_entry;

	if (connector->base.connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector->base.connector_type == DRM_MODE_CONNECTOR_eDP) {
		for (i = 0; i < ARRAY_SIZE(dp_debugfs_entries); i++) {
			debugfs_create_file(dp_debugfs_entries[i].name,
					    0644, dir, connector,
					    dp_debugfs_entries[i].fops);
		}
	}
	if (connector->base.connector_type == DRM_MODE_CONNECTOR_eDP) {
		debugfs_create_file_unsafe("psr_state", 0444, dir, connector, &psr_fops);
		debugfs_create_file("amdgpu_current_backlight_pwm", 0444, dir, connector,
				    &current_backlight_fops);
		debugfs_create_file("amdgpu_target_backlight_pwm", 0444, dir, connector,
				    &target_backlight_fops);
	}

	for (i = 0; i < ARRAY_SIZE(connector_debugfs_entries); i++) {
		debugfs_create_file(connector_debugfs_entries[i].name,
				    0644, dir, connector,
				    connector_debugfs_entries[i].fops);
	}

	connector->debugfs_dpcd_address = 0;
	connector->debugfs_dpcd_size = 0;

#ifdef CONFIG_DRM_AMD_DC_HDCP
	if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		for (i = 0; i < ARRAY_SIZE(hdmi_debugfs_entries); i++) {
			debugfs_create_file(hdmi_debugfs_entries[i].name,
					    0644, dir, connector,
					    hdmi_debugfs_entries[i].fops);
		}
	}
#endif
}

#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
/*
 * Set crc window coordinate x start
 */
static int crc_win_x_start_set(void *data, u64 val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	acrtc->dm_irq_params.crc_window.x_start = (uint16_t) val;
	acrtc->dm_irq_params.crc_window.update_win = false;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

/*
 * Get crc window coordinate x start
 */
static int crc_win_x_start_get(void *data, u64 *val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	*val = acrtc->dm_irq_params.crc_window.x_start;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(crc_win_x_start_fops, crc_win_x_start_get,
			 crc_win_x_start_set, "%llu\n");


/*
 * Set crc window coordinate y start
 */
static int crc_win_y_start_set(void *data, u64 val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	acrtc->dm_irq_params.crc_window.y_start = (uint16_t) val;
	acrtc->dm_irq_params.crc_window.update_win = false;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

/*
 * Get crc window coordinate y start
 */
static int crc_win_y_start_get(void *data, u64 *val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	*val = acrtc->dm_irq_params.crc_window.y_start;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(crc_win_y_start_fops, crc_win_y_start_get,
			 crc_win_y_start_set, "%llu\n");

/*
 * Set crc window coordinate x end
 */
static int crc_win_x_end_set(void *data, u64 val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	acrtc->dm_irq_params.crc_window.x_end = (uint16_t) val;
	acrtc->dm_irq_params.crc_window.update_win = false;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

/*
 * Get crc window coordinate x end
 */
static int crc_win_x_end_get(void *data, u64 *val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	*val = acrtc->dm_irq_params.crc_window.x_end;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(crc_win_x_end_fops, crc_win_x_end_get,
			 crc_win_x_end_set, "%llu\n");

/*
 * Set crc window coordinate y end
 */
static int crc_win_y_end_set(void *data, u64 val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	acrtc->dm_irq_params.crc_window.y_end = (uint16_t) val;
	acrtc->dm_irq_params.crc_window.update_win = false;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

/*
 * Get crc window coordinate y end
 */
static int crc_win_y_end_get(void *data, u64 *val)
{
	struct drm_crtc *crtc = data;
	struct drm_device *drm_dev = crtc->dev;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

	spin_lock_irq(&drm_dev->event_lock);
	*val = acrtc->dm_irq_params.crc_window.y_end;
	spin_unlock_irq(&drm_dev->event_lock);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(crc_win_y_end_fops, crc_win_y_end_get,
			 crc_win_y_end_set, "%llu\n");
/*
 * Trigger to commit crc window
 */
static int crc_win_update_set(void *data, u64 val)
{
	struct drm_crtc *new_crtc = data;
	struct drm_crtc *old_crtc = NULL;
	struct amdgpu_crtc *new_acrtc, *old_acrtc;
	struct amdgpu_device *adev = drm_to_adev(new_crtc->dev);
	struct crc_rd_work *crc_rd_wrk = adev->dm.crc_rd_wrk;

	if (val) {
		spin_lock_irq(&adev_to_drm(adev)->event_lock);
		spin_lock_irq(&crc_rd_wrk->crc_rd_work_lock);
		if (crc_rd_wrk && crc_rd_wrk->crtc) {
			old_crtc = crc_rd_wrk->crtc;
			old_acrtc = to_amdgpu_crtc(old_crtc);
		}
		new_acrtc = to_amdgpu_crtc(new_crtc);

		if (old_crtc && old_crtc != new_crtc) {
			old_acrtc->dm_irq_params.crc_window.activated = false;
			old_acrtc->dm_irq_params.crc_window.update_win = false;
			old_acrtc->dm_irq_params.crc_window.skip_frame_cnt = 0;

			new_acrtc->dm_irq_params.crc_window.activated = true;
			new_acrtc->dm_irq_params.crc_window.update_win = true;
			new_acrtc->dm_irq_params.crc_window.skip_frame_cnt = 0;
			crc_rd_wrk->crtc = new_crtc;
		} else {
			new_acrtc->dm_irq_params.crc_window.activated = true;
			new_acrtc->dm_irq_params.crc_window.update_win = true;
			new_acrtc->dm_irq_params.crc_window.skip_frame_cnt = 0;
			crc_rd_wrk->crtc = new_crtc;
		}
		spin_unlock_irq(&crc_rd_wrk->crc_rd_work_lock);
		spin_unlock_irq(&adev_to_drm(adev)->event_lock);
	}

	return 0;
}

/*
 * Get crc window update flag
 */
static int crc_win_update_get(void *data, u64 *val)
{
	*val = 0;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(crc_win_update_fops, crc_win_update_get,
			 crc_win_update_set, "%llu\n");

void crtc_debugfs_init(struct drm_crtc *crtc)
{
	struct dentry *dir = debugfs_lookup("crc", crtc->debugfs_entry);

	if (!dir)
		return;

	debugfs_create_file_unsafe("crc_win_x_start", 0644, dir, crtc,
				   &crc_win_x_start_fops);
	debugfs_create_file_unsafe("crc_win_y_start", 0644, dir, crtc,
				   &crc_win_y_start_fops);
	debugfs_create_file_unsafe("crc_win_x_end", 0644, dir, crtc,
				   &crc_win_x_end_fops);
	debugfs_create_file_unsafe("crc_win_y_end", 0644, dir, crtc,
				   &crc_win_y_end_fops);
	debugfs_create_file_unsafe("crc_win_update", 0644, dir, crtc,
				   &crc_win_update_fops);

}
#endif
/*
 * Writes DTN log state to the user supplied buffer.
 * Example usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_dtn_log
 */
static ssize_t dtn_log_read(
	struct file *f,
	char __user *buf,
	size_t size,
	loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	struct dc *dc = adev->dm.dc;
	struct dc_log_buffer_ctx log_ctx = { 0 };
	ssize_t result = 0;

	if (!buf || !size)
		return -EINVAL;

	if (!dc->hwss.log_hw_state)
		return 0;

	dc->hwss.log_hw_state(dc, &log_ctx);

	if (*pos < log_ctx.pos) {
		size_t to_copy = log_ctx.pos - *pos;

		to_copy = min(to_copy, size);

		if (!copy_to_user(buf, log_ctx.buf + *pos, to_copy)) {
			*pos += to_copy;
			result = to_copy;
		}
	}

	kfree(log_ctx.buf);

	return result;
}

/*
 * Writes DTN log state to dmesg when triggered via a write.
 * Example usage: echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_dtn_log
 */
static ssize_t dtn_log_write(
	struct file *f,
	const char __user *buf,
	size_t size,
	loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	struct dc *dc = adev->dm.dc;

	/* Write triggers log output via dmesg. */
	if (size == 0)
		return 0;

	if (dc->hwss.log_hw_state)
		dc->hwss.log_hw_state(dc, NULL);

	return size;
}

static int mst_topo_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct amdgpu_dm_connector *aconnector;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		aconnector = to_amdgpu_dm_connector(connector);

		/* Ensure we're only dumping the topology of a root mst node */
		if (!aconnector->mst_mgr.mst_state)
			continue;

		seq_printf(m, "\nMST topology for connector %d\n", aconnector->connector_id);
		drm_dp_mst_dump_topology(m, &aconnector->mst_mgr);
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

/*
 * Sets trigger hpd for MST topologies.
 * All connected connectors will be rediscovered and re started as needed if val of 1 is sent.
 * All topologies will be disconnected if val of 0 is set .
 * Usage to enable topologies: echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_trigger_hpd_mst
 * Usage to disable topologies: echo 0 > /sys/kernel/debug/dri/0/amdgpu_dm_trigger_hpd_mst
 */
static int trigger_hpd_mst_set(void *data, u64 val)
{
	struct amdgpu_device *adev = data;
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_connector_list_iter iter;
	struct amdgpu_dm_connector *aconnector;
	struct drm_connector *connector;
	struct dc_link *link = NULL;

	if (val == 1) {
		drm_connector_list_iter_begin(dev, &iter);
		drm_for_each_connector_iter(connector, &iter) {
			aconnector = to_amdgpu_dm_connector(connector);
			if (aconnector->dc_link->type == dc_connection_mst_branch &&
			    aconnector->mst_mgr.aux) {
				dc_link_detect(aconnector->dc_link, DETECT_REASON_HPD);
				drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
			}
		}
	} else if (val == 0) {
		drm_connector_list_iter_begin(dev, &iter);
		drm_for_each_connector_iter(connector, &iter) {
			aconnector = to_amdgpu_dm_connector(connector);
			if (!aconnector->dc_link)
				continue;

			if (!aconnector->mst_port)
				continue;

			link = aconnector->dc_link;
			dp_receiver_power_ctrl(link, false);
			drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_port->mst_mgr, false);
			link->mst_stream_alloc_table.stream_count = 0;
			memset(link->mst_stream_alloc_table.stream_allocations, 0,
					sizeof(link->mst_stream_alloc_table.stream_allocations));
		}
	} else {
		return 0;
	}
	drm_kms_helper_hotplug_event(dev);

	return 0;
}

/*
 * The interface doesn't need get function, so it will return the
 * value of zero
 * Usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_trigger_hpd_mst
 */
static int trigger_hpd_mst_get(void *data, u64 *val)
{
	*val = 0;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(trigger_hpd_mst_ops, trigger_hpd_mst_get,
			 trigger_hpd_mst_set, "%llu\n");


/*
 * Sets the force_timing_sync debug option from the given string.
 * All connected displays will be force synchronized immediately.
 * Usage: echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_force_timing_sync
 */
static int force_timing_sync_set(void *data, u64 val)
{
	struct amdgpu_device *adev = data;

	adev->dm.force_timing_sync = (bool)val;

	amdgpu_dm_trigger_timing_sync(adev_to_drm(adev));

	return 0;
}

/*
 * Gets the force_timing_sync debug option value into the given buffer.
 * Usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_force_timing_sync
 */
static int force_timing_sync_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = data;

	*val = adev->dm.force_timing_sync;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(force_timing_sync_ops, force_timing_sync_get,
			 force_timing_sync_set, "%llu\n");


/*
 * Disables all HPD and HPD RX interrupt handling in the
 * driver when set to 1. Default is 0.
 */
static int disable_hpd_set(void *data, u64 val)
{
	struct amdgpu_device *adev = data;

	adev->dm.disable_hpd_irq = (bool)val;

	return 0;
}


/*
 * Returns 1 if HPD and HPRX interrupt handling is disabled,
 * 0 otherwise.
 */
static int disable_hpd_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = data;

	*val = adev->dm.disable_hpd_irq;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(disable_hpd_ops, disable_hpd_get,
			 disable_hpd_set, "%llu\n");

/*
 * Sets the DC visual confirm debug option from the given string.
 * Example usage: echo 1 > /sys/kernel/debug/dri/0/amdgpu_visual_confirm
 */
static int visual_confirm_set(void *data, u64 val)
{
	struct amdgpu_device *adev = data;

	adev->dm.dc->debug.visual_confirm = (enum visual_confirm)val;

	return 0;
}

/*
 * Reads the DC visual confirm debug option value into the given buffer.
 * Example usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_visual_confirm
 */
static int visual_confirm_get(void *data, u64 *val)
{
	struct amdgpu_device *adev = data;

	*val = adev->dm.dc->debug.visual_confirm;

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(mst_topo);
DEFINE_DEBUGFS_ATTRIBUTE(visual_confirm_fops, visual_confirm_get,
			 visual_confirm_set, "%llu\n");

/*
 * Dumps the DCC_EN bit for each pipe.
 * Example usage: cat /sys/kernel/debug/dri/0/amdgpu_dm_dcc_en
 */
static ssize_t dcc_en_bits_read(
	struct file *f,
	char __user *buf,
	size_t size,
	loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	struct dc *dc = adev->dm.dc;
	char *rd_buf = NULL;
	const uint32_t rd_buf_size = 32;
	uint32_t result = 0;
	int offset = 0;
	int num_pipes = dc->res_pool->pipe_count;
	int *dcc_en_bits;
	int i, r;

	dcc_en_bits = kcalloc(num_pipes, sizeof(int), GFP_KERNEL);
	if (!dcc_en_bits)
		return -ENOMEM;

	if (!dc->hwss.get_dcc_en_bits) {
		kfree(dcc_en_bits);
		return 0;
	}

	dc->hwss.get_dcc_en_bits(dc, dcc_en_bits);

	rd_buf = kcalloc(rd_buf_size, sizeof(char), GFP_KERNEL);
	if (!rd_buf)
		return -ENOMEM;

	for (i = 0; i < num_pipes; i++)
		offset += snprintf(rd_buf + offset, rd_buf_size - offset,
				   "%d  ", dcc_en_bits[i]);
	rd_buf[strlen(rd_buf)] = '\n';

	kfree(dcc_en_bits);

	while (size) {
		if (*pos >= rd_buf_size)
			break;
		r = put_user(*(rd_buf + result), buf);
		if (r)
			return r; /* r = -EFAULT */
		buf += 1;
		size -= 1;
		*pos += 1;
		result += 1;
	}

	kfree(rd_buf);
	return result;
}

void dtn_debugfs_init(struct amdgpu_device *adev)
{
	static const struct file_operations dtn_log_fops = {
		.owner = THIS_MODULE,
		.read = dtn_log_read,
		.write = dtn_log_write,
		.llseek = default_llseek
	};
	static const struct file_operations dcc_en_bits_fops = {
		.owner = THIS_MODULE,
		.read = dcc_en_bits_read,
		.llseek = default_llseek
	};

	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;

	debugfs_create_file("amdgpu_mst_topology", 0444, root,
			    adev, &mst_topo_fops);
	debugfs_create_file("amdgpu_dm_dtn_log", 0644, root, adev,
			    &dtn_log_fops);

	debugfs_create_file_unsafe("amdgpu_dm_visual_confirm", 0644, root, adev,
				   &visual_confirm_fops);

	debugfs_create_file_unsafe("amdgpu_dm_dmub_tracebuffer", 0644, root,
				   adev, &dmub_tracebuffer_fops);

	debugfs_create_file_unsafe("amdgpu_dm_dmub_fw_state", 0644, root,
				   adev, &dmub_fw_state_fops);

	debugfs_create_file_unsafe("amdgpu_dm_force_timing_sync", 0644, root,
				   adev, &force_timing_sync_ops);

	debugfs_create_file_unsafe("amdgpu_dm_dmcub_trace_event_en", 0644, root,
				   adev, &dmcub_trace_event_state_fops);

	debugfs_create_file_unsafe("amdgpu_dm_trigger_hpd_mst", 0644, root,
				   adev, &trigger_hpd_mst_ops);

	debugfs_create_file_unsafe("amdgpu_dm_dcc_en", 0644, root, adev,
				   &dcc_en_bits_fops);

	debugfs_create_file_unsafe("amdgpu_dm_disable_hpd", 0644, root, adev,
				   &disable_hpd_ops);

}

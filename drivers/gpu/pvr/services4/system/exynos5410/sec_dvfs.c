/* /drivers/gpu/pvr/services4/system/exynos5410/sec_dvfs.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX DVFS driver
 *
 * This software is proprietary of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed, transmitted,
 * transcribed, stored in a retrieval system or translated into any human or computer language in any form by any means,
 * electronic, mechanical, manual or otherwise, or disclosed
 * to third parties without the express written permission of Samsung Electronics.
 *
 * Alternatively, this program is free software in case of Linux Kernel;
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <plat/cpu.h>
#include <mach/asv-exynos.h>

#include "services_headers.h"
#include "sysinfo.h"
#include "sec_dvfs.h"
#include "sec_control_pwr_clk.h"
#include "sec_clock.h"

#define MAX_DVFS_LEVEL			10
#define BASE_START_LEVEL		0
#define BASE_UP_STEP_LEVEL		1
#define BASE_DWON_STEP_LEVEL	1
#define BASE_QUICK_UP_LEVEL		2
#define BASE_QUICK_DOWN_LEVEL	2
#define BASE_WAKE_UP_LEVEL		3
#define DOWN_REQUIREMENT_THRESHOLD	3
#ifdef USING_640MHZ
#define GPU_DVFS_MAX_LEVEL		6
#else
#define GPU_DVFS_MAX_LEVEL		4
#endif

/* boost mode need more test */
/* #define USING_BOOST_UP_MODE */
/* #define USING_BOOST_DOWN_MODE */

#define setmask(a, b) (((1 < a) < 24)|b)
#define getclockmask(a) ((a | 0xFF000000) > 24)
#define getlevelmask(a) (a | 0xFFFFFF)

/* start define DVFS info */
static GPU_DVFS_DATA default_dvfs_data[] = {
/* level, clock, voltage, src clk, min, max, min2, max2, stay, mask, etc */
#ifdef USING_640MHZ
	{ 0,    640, 1200000,     640, 180, 256,   170, 256, 0, 0, 0 },
	{ 1,    532, 1150000,     532, 170, 100,   160, 250, 0, 0, 0 },
	{ 2,    480, 1100000,     480, 160, 190,   150, 250, 0, 0, 0 },
	{ 3,    350,  925000,     350, 150, 200,   140, 250, 0, 0, 0 },
	{ 4,    266,  900000,     266, 140, 200,   130, 220, 0, 0, 0 },
	{ 5,    177,  900000,     177,   0, 200,     0, 220, 0, 0, 0 },
#else
	{ 0,    480, 1100000,     480, 170, 256,   160, 256, 0, 0, 0 },
	{ 1,    350,  925000,     350, 160, 190,   150, 210, 0, 0, 0 },
	{ 2,    266,  900000,     266, 150, 200,   140, 250, 0, 0, 0 },
	{ 3,    177,  900000,     177,   0, 200,     0, 220, 0, 0, 0 },
#endif

};

/* end define DVFS info */
GPU_DVFS_DATA g_gpu_dvfs_data[MAX_DVFS_LEVEL];

int sgx_dvfs_level = -1;
/* this value is dvfs mode- 0: auto, others: custom lock */
int sgx_dvfs_custom_clock;
int sgx_dvfs_min_lock;
int sgx_dvfs_max_lock;
int sgx_dvfs_down_requirement;
int custom_min_lock_level;
int custom_max_lock_level;
int custom_threshold_change;
int custom_threshold[MAX_DVFS_LEVEL*4];
int sgx_dvfs_custom_threshold_size;
int custom_threshold_change;
char sgx_dvfs_table_string[256] = {0};
char *sgx_dvfs_table;
/* set sys parameters */
module_param(sgx_dvfs_level, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(sgx_dvfs_level, "SGX DVFS status");
module_param(sgx_dvfs_custom_clock, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(sgx_dvfs_custom_clock, "SGX custom threshold array value");
module_param_array(custom_threshold, int, &sgx_dvfs_custom_threshold_size, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(custom_threshold, "SGX custom threshold array value");
module_param(custom_threshold_change, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(custom_threshold_change, "SGX DVFS custom threshold set (0: do nothing, 1: change, others: reset)");
module_param(sgx_dvfs_table, charp , S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(sgx_dvfs_table, "SGX DVFS frequency array (Mhz)");

#ifdef CONFIG_ASV_MARGIN_TEST
static int set_g3d_freq;
static int __init get_g3d_freq(char *str)
{
	get_option(&str, &set_g3d_freq);
	return 0;
}
early_param("g3dfreq", get_g3d_freq);
#endif
/* end sys parameters */

static int sec_gpu_lock_control_proc(int bmax, long value, size_t count)
{
	int lock_level = sec_gpu_dvfs_level_from_clk_get(value);
	int retval = -EINVAL;

	sgx_dvfs_level = sec_gpu_dvfs_level_from_clk_get(gpu_clock_get());
	if (lock_level < 0) { /* unlock something */
		if (bmax)
			sgx_dvfs_max_lock = custom_max_lock_level = 0;
		else
			sgx_dvfs_min_lock = custom_min_lock_level = 0;

		if (sgx_dvfs_min_lock && (sgx_dvfs_level > custom_min_lock_level)) /* min lock only - likely */
			sec_gpu_vol_clk_change(g_gpu_dvfs_data[custom_min_lock_level].clock, g_gpu_dvfs_data[custom_min_lock_level].voltage);
		else if (sgx_dvfs_max_lock && (sgx_dvfs_level < custom_max_lock_level)) /* max lock only - unlikely */
			sec_gpu_vol_clk_change(g_gpu_dvfs_data[custom_max_lock_level].clock, g_gpu_dvfs_data[custom_max_lock_level].voltage);

		if (value == 0)
			retval = count;
	} else{ /* lock something */
		if (bmax) {
			sgx_dvfs_max_lock = value;
			custom_max_lock_level = lock_level;
		} else {
			sgx_dvfs_min_lock = value;
			custom_min_lock_level = lock_level;
		}

		if ((sgx_dvfs_max_lock) && (sgx_dvfs_min_lock) && (sgx_dvfs_max_lock < sgx_dvfs_min_lock)) { /* abnormal status */
			if (sgx_dvfs_max_lock) /* max lock */
				sec_gpu_vol_clk_change(g_gpu_dvfs_data[custom_max_lock_level].clock, g_gpu_dvfs_data[custom_max_lock_level].voltage);
		} else { /* normal status */
			if ((bmax) && sgx_dvfs_max_lock && (sgx_dvfs_level < custom_max_lock_level)) /* max lock */
				sec_gpu_vol_clk_change(g_gpu_dvfs_data[custom_max_lock_level].clock, g_gpu_dvfs_data[custom_max_lock_level].voltage);
			if ((!bmax) && sgx_dvfs_min_lock && (sgx_dvfs_level > custom_min_lock_level)) /* min lock */
				sec_gpu_vol_clk_change(g_gpu_dvfs_data[custom_min_lock_level].clock, g_gpu_dvfs_data[custom_min_lock_level].voltage);
		}
		retval = count;
	}
	return retval;
}

static ssize_t get_dvfs_table(struct device *d, struct device_attribute *a, char *buf)
{
	return snprintf(buf, sizeof(sgx_dvfs_table_string), "%s\n", sgx_dvfs_table);
}
static DEVICE_ATTR(sgx_dvfs_table, S_IRUGO | S_IRGRP | S_IROTH, get_dvfs_table, 0);

static ssize_t get_min_clock(struct device *d, struct device_attribute *a, char *buf)
{
	PVR_LOG(("get_min_clock: %d MHz", sgx_dvfs_min_lock));
	return snprintf(buf, sizeof(sgx_dvfs_min_lock), "%d\n", sgx_dvfs_min_lock);
}

static ssize_t set_min_clock(struct device *d, struct device_attribute *a, const char *buf, size_t count)
{
	long value;
	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;
	return sec_gpu_lock_control_proc(0, value, count);
}
static DEVICE_ATTR(sgx_dvfs_min_lock, S_IRUGO | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH, get_min_clock, set_min_clock);

static ssize_t get_max_clock(struct device *d, struct device_attribute *a, char *buf)
{
	PVR_LOG(("get_max_clock: %d MHz", sgx_dvfs_max_lock));
	return snprintf(buf, sizeof(sgx_dvfs_max_lock), "%d\n", sgx_dvfs_max_lock);
}

static ssize_t set_max_clock(struct device *d, struct device_attribute *a, const char *buf, size_t count)
{
	long value;
	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;
	return sec_gpu_lock_control_proc(1, value, count);
}
static DEVICE_ATTR(sgx_dvfs_max_lock, S_IRUGO | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH, get_max_clock, set_max_clock);


static ssize_t get_cur_clock(struct device *d, struct device_attribute *a, char *buf)
{
	PVR_DPF((PVR_DBG_MESSAGE,"get_cur_clock: %d MHz", g_gpu_dvfs_data[sgx_dvfs_level].clock));
	return sprintf(buf, "%d\n", g_gpu_dvfs_data[sgx_dvfs_level].clock);
}
static DEVICE_ATTR(sgx_dvfs_cur_clk, S_IRUGO | S_IRGRP | S_IROTH, get_cur_clock, NULL);


void sec_gpu_dvfs_init(void)
{
	struct platform_device *pdev;
	int i = 0;
	ssize_t total = 0, offset = 0;
	memset(g_gpu_dvfs_data, 0x00, sizeof(GPU_DVFS_DATA)*MAX_DVFS_LEVEL);
	for (i = 0; i < GPU_DVFS_MAX_LEVEL; i++) {
		g_gpu_dvfs_data[i].level = default_dvfs_data[i].level;
		g_gpu_dvfs_data[i].clock = default_dvfs_data[i].clock;
		g_gpu_dvfs_data[i].voltage = get_match_volt(ID_G3D, default_dvfs_data[i].clock * 1000);
		g_gpu_dvfs_data[i].clock_source = default_dvfs_data[i].clock_source;
		g_gpu_dvfs_data[i].min_threadhold = default_dvfs_data[i].min_threadhold;
		g_gpu_dvfs_data[i].max_threadhold = default_dvfs_data[i].max_threadhold;
		g_gpu_dvfs_data[i].quick_down_threadhold = default_dvfs_data[i].quick_down_threadhold;
		g_gpu_dvfs_data[i].quick_up_threadhold = default_dvfs_data[i].quick_up_threadhold;
		g_gpu_dvfs_data[i].stay_total_count = default_dvfs_data[i].stay_total_count;
		g_gpu_dvfs_data[i].mask  = setmask(default_dvfs_data[i].level, default_dvfs_data[i].clock);
#ifdef CONFIG_ODROIDXU_DEBUG_MESSAGES
		PVR_LOG(("G3D DVFS Info: Level:%d, Clock:%d MHz, Voltage:%d uV", g_gpu_dvfs_data[i].level, g_gpu_dvfs_data[i].clock, g_gpu_dvfs_data[i].voltage));
#endif
	}
	/* default dvfs level depend on default clock setting */
	sgx_dvfs_level = sec_gpu_dvfs_level_from_clk_get(gpu_clock_get());
	custom_threshold_change = 0;
	sgx_dvfs_down_requirement = DOWN_REQUIREMENT_THRESHOLD;

	pdev = gpsPVRLDMDev;

	/* Required name attribute */
	if (device_create_file(&pdev->dev, &dev_attr_sgx_dvfs_min_lock) < 0)
		PVR_LOG(("device_create_file: dev_attr_sgx_dvfs_min_lock fail"));
	if (device_create_file(&pdev->dev, &dev_attr_sgx_dvfs_max_lock) < 0)
		PVR_LOG(("device_create_file: dev_attr_sgx_dvfs_max_lock fail"));
	if (device_create_file(&pdev->dev, &dev_attr_sgx_dvfs_cur_clk) < 0)
		PVR_LOG(("device_create_file: dev_attr_sgx_dvfs_max_lock fail"));

	 /* Generate DVFS table list*/
	for (i = 0; i < sizeof(default_dvfs_data) / sizeof(GPU_DVFS_DATA); i++) {
		offset = snprintf(sgx_dvfs_table_string+total, sizeof(sgx_dvfs_table_string), "%d ", default_dvfs_data[i].clock);
		total += offset;
	}
	sgx_dvfs_table = sgx_dvfs_table_string;
	if (device_create_file(&pdev->dev, &dev_attr_sgx_dvfs_table) < 0)
		PVR_LOG(("device_create_file: dev_attr_sgx_dvfs_table fail"));
}

int sec_gpu_dvfs_level_from_clk_get(int clock)
{
	int i = 0;

	for (i = 0; i < GPU_DVFS_MAX_LEVEL; i++) {
		/* This is necessary because the intent
		 * is the difference of kernel clock value
		 * and sgx clock table value to calibrate it */
		if ((g_gpu_dvfs_data[i].clock / 10) == (clock / 10))
			return i;
	}
	return -1;
}

void sec_gpu_dvfs_down_requirement_reset()
{
	int level;

	level = sec_gpu_dvfs_level_from_clk_get(gpu_clock_get());
	if (level >= 0)
		sgx_dvfs_down_requirement = g_gpu_dvfs_data[level].stay_total_count;
	else
		sgx_dvfs_down_requirement = DOWN_REQUIREMENT_THRESHOLD;
}

extern unsigned int *g_debug_CCB_Info_RO;
extern unsigned int *g_debug_CCB_Info_WO;
extern int g_debug_CCB_Info_WCNT;
static int g_debug_CCB_Info_Flag;
//static int g_debug_CCB_count = 1;
int sec_clock_change_up(int level, int step)
{
	level -= step;

	if (level < 0)
		level = 0;

	if (sgx_dvfs_max_lock) {
		if (level < custom_max_lock_level)
			level = custom_max_lock_level;
	}

	sgx_dvfs_down_requirement = g_gpu_dvfs_data[level].stay_total_count;
	sec_gpu_vol_clk_change(g_gpu_dvfs_data[level].clock, g_gpu_dvfs_data[level].voltage);

//	if ((g_debug_CCB_Info_Flag % g_debug_CCB_count) == 0)
//		PVR_LOG(("SGX CCB RO : %d, WO : %d, Total : %d", *g_debug_CCB_Info_RO, *g_debug_CCB_Info_WO, g_debug_CCB_Info_WCNT));

	g_debug_CCB_Info_WCNT = 0;
	g_debug_CCB_Info_Flag++;

	return level;
}

int sec_clock_change_down(int level, int step)
{
	sgx_dvfs_down_requirement--;
	if (sgx_dvfs_down_requirement > 0)
		return level;

	level += step;

	if (level > GPU_DVFS_MAX_LEVEL - 1)
		level = GPU_DVFS_MAX_LEVEL - 1;

	if (sgx_dvfs_min_lock) {
		if (level > custom_min_lock_level)
			level = custom_min_lock_level;
	}

	sgx_dvfs_down_requirement = g_gpu_dvfs_data[level].stay_total_count;
	sec_gpu_vol_clk_change(g_gpu_dvfs_data[level].clock, g_gpu_dvfs_data[level].voltage);

//	if ((g_debug_CCB_Info_Flag % g_debug_CCB_count) == 0)
//		PVR_LOG(("SGX CCB RO : %d, WO : %d, Total : %d", *g_debug_CCB_Info_RO, *g_debug_CCB_Info_WO, g_debug_CCB_Info_WCNT));

	g_debug_CCB_Info_WCNT = 0;
	g_debug_CCB_Info_Flag++;

	return level;
}

int sec_custom_threshold_set()
{
	int i;
	if ((16 > sgx_dvfs_custom_threshold_size) && (custom_threshold_change == 1)) {
		PVR_LOG(("Error, custom_threshold element not enough[%d]!!", sgx_dvfs_custom_threshold_size));
		custom_threshold_change = 0;
		return -1;
	}

	for (i = 0; i < GPU_DVFS_MAX_LEVEL; i++) {
		if (custom_threshold_change == 1) {
			g_gpu_dvfs_data[i].min_threadhold = custom_threshold[i * 4];
			g_gpu_dvfs_data[i].max_threadhold = custom_threshold[i * 4 + 1];
			g_gpu_dvfs_data[i].quick_down_threadhold = custom_threshold[i * 4 + 2];
			g_gpu_dvfs_data[i].quick_up_threadhold = custom_threshold[i * 4 + 3];
			PVR_LOG(("set custom_threshold level[%d] min[%d],max[%d],q_min[%d],q_max[%d]", i,
					g_gpu_dvfs_data[i].min_threadhold, g_gpu_dvfs_data[i].max_threadhold,
					g_gpu_dvfs_data[i].quick_down_threadhold, g_gpu_dvfs_data[i].quick_up_threadhold));
		} else {
			g_gpu_dvfs_data[i].min_threadhold = default_dvfs_data[i].min_threadhold;
			g_gpu_dvfs_data[i].max_threadhold = default_dvfs_data[i].max_threadhold;
			g_gpu_dvfs_data[i].quick_down_threadhold = default_dvfs_data[i].quick_down_threadhold;
			g_gpu_dvfs_data[i].quick_up_threadhold = default_dvfs_data[i].quick_up_threadhold;
			PVR_LOG(("set threshold value restore level[%d] min[%d],max[%d],q_min[%d],q_max[%d]", i,
					g_gpu_dvfs_data[i].min_threadhold, g_gpu_dvfs_data[i].max_threadhold,
					g_gpu_dvfs_data[i].quick_down_threadhold, g_gpu_dvfs_data[i].quick_up_threadhold));
		}
	}
	custom_threshold_change = 0;
	return 1;
}

unsigned int g_g3dfreq;
void sec_gpu_dvfs_handler(int utilization_value)
{
	if (custom_threshold_change)
		sec_custom_threshold_set();

	/*utilization_value is zero mean is gpu going to idle*/
	if (utilization_value == 0)
		return;

#ifdef CONFIG_ASV_MARGIN_TEST
	sgx_dvfs_custom_clock = set_g3d_freq;
#endif
	/* this check for custom dvfs setting - 0:auto, others: custom lock clock*/
	if (sgx_dvfs_custom_clock) {
		if (sgx_dvfs_custom_clock != gpu_clock_get()) {
			sgx_dvfs_level = sec_gpu_dvfs_level_from_clk_get(sgx_dvfs_custom_clock);
			/* this check for current clock must be find in dvfs table */
			if (sgx_dvfs_level < 0) {
				PVR_LOG(("WARN: custom clock: %d MHz not found in DVFS table", sgx_dvfs_custom_clock));
				return;
			}

			if (sgx_dvfs_level < MAX_DVFS_LEVEL && sgx_dvfs_level >= 0) {

				sec_gpu_vol_clk_change(g_gpu_dvfs_data[sgx_dvfs_level].clock, g_gpu_dvfs_data[sgx_dvfs_level].voltage);

				PVR_LOG(("INFO: CUSTOM DVFS [%d MHz] (%d, %d), utilization [%d] -(%d MHz)",
						gpu_clock_get(),
						g_gpu_dvfs_data[sgx_dvfs_level].min_threadhold,
						g_gpu_dvfs_data[sgx_dvfs_level].max_threadhold,
						utilization_value,
						sgx_dvfs_custom_clock
						));
			} else {
				 PVR_LOG(("INFO: CUSTOM DVFS [%d MHz] invalid clock - restore auto mode", sgx_dvfs_custom_clock));
				 sgx_dvfs_custom_clock = 0;
			}
		}
	} else {
		sgx_dvfs_level = sec_gpu_dvfs_level_from_clk_get(gpu_clock_get());
		/* this check for current clock must be find in dvfs table */
		if (sgx_dvfs_level < 0) {
			PVR_LOG(("WARN: current clock: %d MHz not found in DVFS table. so set to max clock", gpu_clock_get()));
			sec_gpu_vol_clk_change(g_gpu_dvfs_data[BASE_START_LEVEL].clock, g_gpu_dvfs_data[BASE_START_LEVEL].voltage);
			return;
		}

		PVR_DPF((PVR_DBG_MESSAGE, "INFO: AUTO DVFS [%d MHz] <%d, %d>, utilization [%d]",
				gpu_clock_get(),
				g_gpu_dvfs_data[sgx_dvfs_level].min_threadhold,
				g_gpu_dvfs_data[sgx_dvfs_level].max_threadhold, utilization_value));

		/* check current level's threadhold value */
		if (g_gpu_dvfs_data[sgx_dvfs_level].min_threadhold > utilization_value) {
#if defined(USING_BOOST_DOWN_MODE)
			/* check need Quick up/down change */
			if (g_gpu_dvfs_data[sgx_dvfs_level].quick_down_threadhold >= utilization_value)
				sgx_dvfs_level = sec_clock_change_down(sgx_dvfs_level, BASE_QUICK_DOWN_LEVEL);
			else
#endif
				/* need to down current clock */
				sgx_dvfs_level = sec_clock_change_down(sgx_dvfs_level, BASE_DWON_STEP_LEVEL);

		} else if (g_gpu_dvfs_data[sgx_dvfs_level].max_threadhold < utilization_value) {
#if defined(USING_BOOST_UP_MODE)
			if (g_gpu_dvfs_data[sgx_dvfs_level].quick_up_threadhold <= utilization_value)
				sgx_dvfs_level = sec_clock_change_up(sgx_dvfs_level, BASE_QUICK_UP_LEVEL);
			else
#endif
				/* need to up current clock */
				sgx_dvfs_level = sec_clock_change_up(sgx_dvfs_level, BASE_UP_STEP_LEVEL);
		} else
			sgx_dvfs_down_requirement = g_gpu_dvfs_data[sgx_dvfs_level].stay_total_count;
		}
	g_g3dfreq = g_gpu_dvfs_data[sgx_dvfs_level].clock;
}

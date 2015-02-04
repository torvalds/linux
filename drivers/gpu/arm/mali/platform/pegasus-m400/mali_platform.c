/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include <linux/version.h>
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#ifdef USING_MALI_PMM
#include "mali_pmm.h"
#endif

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>

#include <asm/io.h>

#define CLK_DIV_STAT_G3D 	0x1003C62C
#define CLK_DESC 		"clk-divider-status"

typedef struct mali_runtime_resumeTag {
	int clk;
	int vol;
} mali_runtime_resume_table;

mali_runtime_resume_table mali_runtime_resume = {400, 1100000};

static struct clk *sclk_g3d_clock = NULL;

/* Please take special care when lowering the voltage value, since it can *
 * cause system stability problems (random oops, etc.)                    */
unsigned int mali_gpu_vol = 1125000; /* 1.1125 V */

#ifdef CONFIG_MALI_DVFS
#define MALI_DVFS_DEFAULT_STEP 0
#endif

static int bPoweroff;

#ifdef CONFIG_REGULATOR
struct regulator {
	struct device *dev;
	struct list_head list;
	unsigned int always_on:1;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
	struct dentry *debugfs;
};
struct regulator *g3d_regulator = NULL;
#endif


mali_io_address clk_register_map = 0;
_mali_osk_mutex_t *mali_dvfs_lock = 0;

void mali_set_runtime_resume_params(int clk, int volt)
{
	mali_runtime_resume.clk = clk;
	mali_runtime_resume.vol = volt;
}

#ifdef CONFIG_REGULATOR
static unsigned int mali_regulator_get_usecount(void)
{
	struct regulator_dev *rdev;

	if ( IS_ERR_OR_NULL(g3d_regulator) ) {
		MALI_PRINT_ERROR(("Mali platform: getting regulator use count failed\n"));
		return 0;
	}
	rdev = g3d_regulator->rdev;
	return rdev->use_count;
}

static void mali_regulator_set_voltage(int min_uV, int max_uV)
{
	int voltage;
#ifndef CONFIG_MALI_DVFS
	min_uV = mali_gpu_vol;
	max_uV = mali_gpu_vol;
#endif

	_mali_osk_mutex_wait(mali_dvfs_lock);

	if( IS_ERR_OR_NULL(g3d_regulator) ) {
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_set_voltage : g3d_regulator is null\n"));
		return;
	}
	MALI_DEBUG_PRINT(2, ("= regulator_set_voltage: %d, %d \n", min_uV, max_uV));
	regulator_set_voltage(g3d_regulator, min_uV, max_uV);
	voltage = regulator_get_voltage(g3d_regulator);
	mali_gpu_vol = voltage;
	MALI_DEBUG_PRINT(1, ("= regulator_get_voltage: %d \n", mali_gpu_vol));

	_mali_osk_mutex_signal(mali_dvfs_lock);
}
#endif

static int mali_platform_clk_enable(void)
{
	struct device *dev = &mali_platform_device->dev;
	unsigned long rate;

	sclk_g3d_clock = clk_get(dev, "sclk_g3d");
	if (IS_ERR(sclk_g3d_clock)) {
		MALI_PRINT_ERROR(("Mali platform: failed to get source g3d clock\n"));
		return 1;
	}

	_mali_osk_mutex_wait(mali_dvfs_lock);

	if (clk_prepare_enable(sclk_g3d_clock) < 0) {
		MALI_PRINT_ERROR(("Mali platform: failed to enable source g3d clock\n"));
		return 1;
	}

	rate = clk_get_rate(sclk_g3d_clock);

	MALI_PRINT(("Mali platform: g3d clock rate = %u MHz\n", rate / 1000000));

	_mali_osk_mutex_signal(mali_dvfs_lock);

	return 0;
}

static int mali_platform_init_clk(void)
{
	static int initialized = 0;

	if (initialized) return 1;

	mali_dvfs_lock = _mali_osk_mutex_init(0, 0);
	if (mali_dvfs_lock == NULL) return 1;

	if (mali_platform_clk_enable()) return 1;

#ifdef CONFIG_REGULATOR
#ifdef USING_MALI_PMM
	g3d_regulator = regulator_get(&mali_platform_device.dev, "vdd_g3d");
#else
	g3d_regulator = regulator_get(NULL, "vdd_g3d");
#endif

	if (IS_ERR(g3d_regulator)) {
		MALI_PRINT_ERROR(("Mali platform: failed to get g3d regulator\n"));
		goto err_regulator;
	}

	regulator_enable(g3d_regulator);
	MALI_DEBUG_PRINT(3, ("Mali platform: g3d regulator enabled (use count = %u)\n", mali_regulator_get_usecount()));
	mali_regulator_set_voltage(mali_gpu_vol, mali_gpu_vol);
#endif

	initialized = 1;
	return 0;

#ifdef CONFIG_REGULATOR
err_regulator:
	regulator_put(g3d_regulator);
#endif
	return 1;
}

_mali_osk_errcode_t mali_platform_init()
{
	MALI_CHECK(mali_platform_init_clk() == 0, _MALI_OSK_ERR_FAULT);
#ifdef CONFIG_MALI_DVFS
	if (!clk_register_map)
		clk_register_map = _mali_osk_mem_mapioregion(CLK_DIV_STAT_G3D, 0x20, CLK_DESC);

	if (!init_mali_dvfs_status(MALI_DVFS_DEFAULT_STEP))
		MALI_DEBUG_PRINT(1, ("mali_platform_init failed\n"));
#endif

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit()
{
	if (sclk_g3d_clock) {
		clk_disable_unprepare(sclk_g3d_clock);
		clk_put(sclk_g3d_clock);
		sclk_g3d_clock = NULL;
	}

#ifdef CONFIG_REGULATOR
	if (g3d_regulator) {
		regulator_put(g3d_regulator);
		g3d_regulator = NULL;
	}
#endif

#ifdef CONFIG_MALI_DVFS
	deinit_mali_dvfs_status();
	if (clk_register_map) {
		_mali_osk_mem_unmapioregion(CLK_DIV_STAT_G3D, 0x20, clk_register_map);
		clk_register_map = 0;
	}
#endif

	MALI_SUCCESS;
}

void mali_gpu_utilization_handler(u32 utilization)
{
	if (bPoweroff == 0) {
#ifdef CONFIG_MALI_DVFS
		if (!mali_dvfs_handler(utilization))
			MALI_DEBUG_PRINT(1,( "error on mali dvfs status in utilization\n"));
#endif
	}
}

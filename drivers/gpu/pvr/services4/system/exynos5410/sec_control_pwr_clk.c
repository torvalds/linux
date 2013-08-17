/* /drivers/gpu/pvr/services4/system/exynos5410/sec_contorl_power_clock.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX power clock control driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */
#include <linux/module.h>
#include <linux/pm_qos.h>
#include <plat/cpu.h>
#include <linux/mutex.h>

#include "services_headers.h"
#include "sysinfo.h"
#include "sec_control_pwr_clk.h"
#include "sec_clock.h"
#include "sec_power.h"


#define SGX_DEFAULT_CLOCK   350
#define SGX_DEFAULT_VOLTAGE 925000
#define WAKEUP_LOCK_CLOCK   350
#define WAKEUP_LOCK_VOLTAGE 925000

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
static struct pm_qos_request exynos5_g3d_cpu_qos;
struct pm_qos_request exynos5_g3d_mif_qos;
struct pm_qos_request exynos5_g3d_int_qos;
#define MIF_THRESHHOLD_VALUE_CLK 350
#endif

static int sec_gpu_top_clock;
static int gpu_voltage_marin;
int sec_wakeup_lock_state;
bool sec_gpu_power_on;

module_param(sec_wakeup_lock_state, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(sec_wakeup_lock_state, "SGX wakeup lock setting");

int sec_gpu_setting_clock   = SGX_DEFAULT_CLOCK;
int sec_gpu_setting_voltage = SGX_DEFAULT_VOLTAGE;

static DEFINE_MUTEX(lock);

/* gpu power clock init */
int sec_gpu_pwr_clk_init(void)
{
	int ret = -1;
	gpu_voltage_marin = 0;
	sec_gpu_top_clock = 480;
	gpu_regulator_enable();
	ret = gpu_clks_get();
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "gpu_clks_get error[%d]", ret));
		return ret;
	}
	gpu_power_init();
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	pm_qos_add_request(&exynos5_g3d_cpu_qos, PM_QOS_CPU_FREQ_MIN, 0);
	pm_qos_add_request(&exynos5_g3d_int_qos, PM_QOS_DEVICE_THROUGHPUT, 0);
	pm_qos_add_request(&exynos5_g3d_mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
#endif
	return ret;
}

/* gpu power clock deinit */
int sec_gpu_pwr_clk_deinit(void)
{
	int ret = -1;
	gpu_clks_put();
	ret = gpu_regulator_disable();
	if (ret)
		PVR_DPF((PVR_DBG_ERROR, "gpu_regulator_disable error[%d]", ret));
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	pm_qos_remove_request(&exynos5_g3d_cpu_qos);
	pm_qos_remove_request(&exynos5_g3d_int_qos);
	pm_qos_remove_request(&exynos5_g3d_mif_qos);
#endif
	return ret;
}

/* gpu clock setting*/
void sec_gpu_vol_clk_change(int sgx_clock, int sgx_voltage)
{
	int cur_sgx_clock;
	mutex_lock(&lock);
	cur_sgx_clock = gpu_clock_get();
	sgx_voltage += gpu_voltage_marin;
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	if (sec_gpu_power_on) {
		if (sgx_clock >= sec_gpu_top_clock) {
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
			pm_qos_update_request(&exynos5_g3d_cpu_qos, 600000);
#else
			pm_qos_update_request(&exynos5_g3d_cpu_qos, 800000);
#endif
		}

		if (sec_gpu_setting_clock < MIF_THRESHHOLD_VALUE_CLK)
			pm_qos_update_request(&exynos5_g3d_mif_qos, 267000);
		else
			pm_qos_update_request(&exynos5_g3d_mif_qos, 800000);
	} else {
		pm_qos_update_request(&exynos5_g3d_cpu_qos, 0);
		pm_qos_update_request(&exynos5_g3d_int_qos, 0);
		pm_qos_update_request(&exynos5_g3d_mif_qos, 0);
	}
#endif
	if (cur_sgx_clock > sgx_clock) {
		gpu_clock_set(sgx_clock);
		gpu_voltage_set(sgx_voltage);
	} else if (cur_sgx_clock < sgx_clock) {
		gpu_voltage_set(sgx_voltage);
		gpu_clock_set(sgx_clock);
	}
	sec_gpu_setting_clock = gpu_clock_get();
	sec_gpu_setting_voltage = gpu_voltage_get();
	mutex_unlock(&lock);
}

static int sec_gpu_clock_disable(void)
{
	gpu_clock_disable();
	return 0;
}

static int sec_gpu_clock_enable(void)
{
	/* adonis must be set parent function after runtime pm resume */
	gpu_clock_set_parent();
	/*
	 * if setting wakeup lock clock, resume clock using that
	 * if different with current clock and default cleck, need to set clock
	 */
	if (sec_wakeup_lock_state) {
		if (gpu_clock_get() != WAKEUP_LOCK_CLOCK)
			gpu_clock_set(WAKEUP_LOCK_CLOCK);

		if (gpu_voltage_get() != WAKEUP_LOCK_VOLTAGE)
			gpu_voltage_set(WAKEUP_LOCK_VOLTAGE);
	} else {
		if (gpu_clock_get() != sec_gpu_setting_clock)
			gpu_clock_set(sec_gpu_setting_clock);

		if (gpu_voltage_get() != sec_gpu_setting_voltage)
			gpu_voltage_set(sec_gpu_setting_voltage);
	}
	gpu_clock_enable();

	/* wait for more than 10 clocks to proper reset SGX core */
	OSWaitus(1);
	return 0;
}

/* gpu gpu setmode clock and power*/
int sec_gpu_pwr_clk_state_set(sec_gpu_state state)
{
	mutex_lock(&lock);
	switch (state) {
	case GPU_PWR_CLK_STATE_ON:
	{
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
		pm_qos_update_request(&exynos5_g3d_int_qos, 200000);
		if (sec_gpu_setting_clock < MIF_THRESHHOLD_VALUE_CLK)
			pm_qos_update_request(&exynos5_g3d_mif_qos, 267000);
		else
			pm_qos_update_request(&exynos5_g3d_mif_qos, 800000);

		if (sec_gpu_setting_clock >= sec_gpu_top_clock) {
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
			pm_qos_update_request(&exynos5_g3d_cpu_qos, 600000);
#else
			pm_qos_update_request(&exynos5_g3d_cpu_qos, 800000);
#endif
		}
#endif
		gpu_power_enable();
		sec_gpu_clock_enable();
		sec_gpu_power_on = true;
	}
	break;
	case GPU_PWR_CLK_STATE_OFF:
	{
		sec_gpu_power_on = false;
		sec_gpu_clock_disable();
		gpu_power_disable();
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
		pm_qos_update_request(&exynos5_g3d_cpu_qos, 0);
		pm_qos_update_request(&exynos5_g3d_int_qos, 0);
		pm_qos_update_request(&exynos5_g3d_mif_qos, 0);
#endif
	}
	break;
	default:
		PVR_DPF((PVR_DBG_ERROR, "Error setting sec_gpu_state_set: %d", state));
	break;
	}

	mutex_unlock(&lock);
	return 0;
}

int sec_gpu_pwr_clk_margin_set(unsigned int margin_offset)
{
	mutex_lock(&lock);
	if (margin_offset) {
		/* set or reset voltage margin - margin_offset */
		if (margin_offset != gpu_voltage_marin) {
			gpu_voltage_set(sec_gpu_setting_voltage - gpu_voltage_marin + margin_offset);
			gpu_voltage_marin = margin_offset;
		}
		/* else case already setting as same margin value - do nothing */
	} else {
		/* this case restore voltage */
		if (gpu_voltage_marin) {
			gpu_voltage_set(sec_gpu_setting_voltage - gpu_voltage_marin);
			gpu_voltage_marin = 0;
		}
		/* else case margin value is zero - do nothing */
	}
	sec_gpu_setting_voltage = gpu_voltage_get();
	mutex_unlock(&lock);
	return 0;
}

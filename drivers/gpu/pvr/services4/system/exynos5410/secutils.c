/* /drivers/gpu/pvr/services4/system/exynos5410/secutils.c
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
#include <linux/module.h>
#include <linux/pm_qos.h>

#include <mach/regs-clock.h>

#include "services_headers.h"
#include "sysinfo.h"
#include "secutils.h"

#include <osfunc.h>
#include <sgxinfokm.h>

/* clz func */
#include <linux/bitops.h>

#define GPU_UTILIZATION_TIME 100 /* 100 msec */

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
extern struct pm_qos_request exynos5_g3d_mif_qos;
extern struct pm_qos_request exynos5_g3d_int_qos;
#endif
static int sgx_gpu_utilization;
extern int sgx_dvfs_level;
module_param(sgx_gpu_utilization, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(sgx_gpu_utilization, "SGX gpu utilization");

struct mutex time_data_lock;
struct mutex timer_lock;
int hw_running_state;

static u64 period_start_time = 0;
static u64 hw_start_time = 0;
static u64 total_work_time = 0;

static bool timer_running = false;

/* global variable */
PVRSRV_SGXDEV_INFO	psDevInfo;
IMG_BOOL bEnableMIFMornitering = true;

/* extern func define */
extern void sec_gpu_dvfs_handler(int utilization_value);

/*this function is SGX H/W state callback function*/
IMG_VOID SysSGXIdleTransition(IMG_BOOL bSGXIdle)
{
	#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	if (bEnableMIFMornitering) {
	#endif
		if (bSGXIdle)
			sgx_hw_end(); /* wakeup state */
		else
			sgx_hw_start(); /* idle state */
	#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	} else	{
		if (bSGXIdle) {
			sgx_hw_end(); /* wakeup state */
			{
				pm_qos_update_request(&exynos5_g3d_int_qos, 0);
				pm_qos_update_request(&exynos5_g3d_mif_qos, 0);
			}
			{
				void __iomem *status;
				status = EXYNOS_CLKREG(0x2051c);
				__raw_writel(0x3F, status);
			}
		} else {
			{
				void __iomem *status;
				status = EXYNOS_CLKREG(0x2051c);
				__raw_writel(0x00, status);
			}
			{
				pm_qos_update_request(&exynos5_g3d_int_qos, 200000);
				if (sgx_dvfs_level > 2)
					pm_qos_update_request(&exynos5_g3d_mif_qos, 267000);
				else
					pm_qos_update_request(&exynos5_g3d_mif_qos, 800000);
			}
			sgx_hw_start(); /* idle state */
		}
	}
	#endif
}

u64 get_time_ns(void)
{
	struct timespec tsval;
	getnstimeofday(&tsval);
	return (u64)timespec_to_ns(&tsval);
}

void sgx_hw_start(void)
{
	hw_running_state++;
	if (hw_running_state == 1) {
		mutex_lock(&time_data_lock);
		hw_start_time = get_time_ns();

		if (timer_running != true) { /* first time setting */
			timer_running = true;
			period_start_time = hw_start_time;
		}
		mutex_unlock(&time_data_lock);
	}
}

void sgx_hw_end(void)
{
	if (hw_running_state > 0) {
		hw_running_state--;
		if (hw_running_state == 0) {
			u64 time_now = get_time_ns();
			mutex_lock(&time_data_lock);

			total_work_time += (time_now - hw_start_time);
			hw_start_time = 0;

			mutex_unlock(&time_data_lock);
		}
	}
}

/* convert the struct timeval to int */
static u32 normalize_time(struct timeval time)
{
	return time.tv_sec * 1000000 + time.tv_usec;
}

static void sec_gpu_utilization_handler(void *arg)
{
	u64 time_now = get_time_ns();

	struct timeval working_time;
	struct timeval period_time;

	mutex_lock(&time_data_lock);

	if (total_work_time == 0) {
		if (!hw_start_time) {

			mutex_unlock(&time_data_lock);

			sec_gpu_dvfs_handler(0);
			return;
		}
	}

	if (hw_start_time != 0) {
		total_work_time += (time_now - hw_start_time);
		hw_start_time = time_now;
	}

	/* changed way to compute the utilization */
	working_time = (struct timeval)ns_to_timeval(total_work_time);
	period_time = (struct timeval)ns_to_timeval(time_now - period_start_time);
	sgx_gpu_utilization = (normalize_time(working_time) * 256) / normalize_time(period_time);


	total_work_time = 0;
	period_start_time = time_now;

	mutex_unlock(&time_data_lock);

	sec_gpu_dvfs_handler(sgx_gpu_utilization);
}

void sec_gpu_utilization_init(void)
{
	/* mutex init */
	mutex_init(&time_data_lock);
	mutex_init(&timer_lock);

	/* utilization handler init */
	utilization_init();
}

void sec_gpu_utilization_pause()
{
	mutex_lock(&timer_lock);
	OSDisableTimer(psDevInfo.hTimer);
	mutex_unlock(&timer_lock);
}

void sec_gpu_utilization_resume()
{
	mutex_lock(&timer_lock);
	OSEnableTimer(psDevInfo.hTimer);
	mutex_unlock(&timer_lock);
}

void utilization_init()
{
	IMG_HANDLE hTimer;

	/* register timer */
	hTimer = OSAddTimer(sec_gpu_utilization_handler, &psDevInfo, GPU_UTILIZATION_TIME);
	if (hTimer == IMG_NULL) {
		PVR_DPF((PVR_DBG_ERROR, "utilization_init: fail"));
		return ;
	}

	psDevInfo.hTimer = hTimer;

	/* start timer */
	OSEnableTimer(psDevInfo.hTimer);
}

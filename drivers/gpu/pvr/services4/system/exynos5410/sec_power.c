/* gpu/drivers/gpu/pvr/services4/system/exynos5410/sec_power.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX power driver
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
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include "services_headers.h"
#include "sysinfo.h"
#include "sec_power.h"
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif /* CONFIG_PM_RUNTIME */

static struct regulator	 *g3d_pd_regulator = NULL;

/* set sys parameters */
static int sgx_gpu_vol;
#ifdef CONFIG_PM_RUNTIME
static int sgx_gpu_power_state = 0;
#endif

module_param(sgx_gpu_vol, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(sgx_gpu_vol, "SGX voltage current value");

#ifdef CONFIG_PM_RUNTIME
module_param(sgx_gpu_power_state, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(sgx_gpu_power_state, "SGX power current status");
#endif
/* end sys parameters */

void gpu_voltage_set(int sgx_vol)
{
//	PVR_LOG(("SGX change voltage [%d] -> [%d] mV", sgx_gpu_vol, sgx_vol));
	regulator_set_voltage(g3d_pd_regulator, sgx_vol, sgx_vol);
	sgx_gpu_vol = regulator_get_voltage(g3d_pd_regulator);
}

int gpu_regulator_enable(void)
{
	if (!g3d_pd_regulator)
		g3d_pd_regulator = regulator_get(&gpsPVRLDMDev->dev, "vdd_g3d");
	regulator_enable(g3d_pd_regulator);

	return 0;
}

int gpu_regulator_disable(void)
{
	if (g3d_pd_regulator)
		regulator_disable(g3d_pd_regulator);

	return 0;
}

/* this is for power init */
void gpu_power_init(void)
{
#ifdef CONFIG_PM_RUNTIME
	pm_suspend_ignore_children(&gpsPVRLDMDev->dev, true);
	pm_runtime_enable(&gpsPVRLDMDev->dev);
#endif
}

/* this is for power gating */
int gpu_power_enable(void)
{
#ifdef CONFIG_PM_RUNTIME
	int err;
	int try_count = 50;
	err = pm_runtime_get_sync(&gpsPVRLDMDev->dev);
	if (err && pm_runtime_suspended(&gpsPVRLDMDev->dev)) {
		PVR_DPF((PVR_DBG_ERROR, "Error in pm_runtime_get_sync"));
		return err;
	}

	do { /* wait for gpu power turned on */
		if (!pm_runtime_suspended(&gpsPVRLDMDev->dev))
			break;

		if (try_count == 0)
			PVR_LOG(("enable_gpu_power on fail with pm_runtime_suspended"));
		schedule();
	} while (try_count--);

	/*this is debug for runtimepm power gating state*/
	{
		void __iomem *status;
		status = EXYNOS_PMUREG(0x4080);
		sgx_gpu_power_state = __raw_readl(status);
#ifdef PM_RUNTIME_DEBUG
		PVR_LOG(("enable_gpu_power: read register: 0x%x", sgx_gpu_power_state));
#endif
	}
#endif
	return 0;
}

/* this is for power gating */
int gpu_power_disable(void)
{
#ifdef CONFIG_PM_RUNTIME
	int err;
	int try_count = 50;

	err = pm_runtime_put_sync(&gpsPVRLDMDev->dev);
	if (err)
		PVR_DPF((PVR_DBG_MESSAGE, "Error in pm_runtime_put_sync"));

	do { /* wait for gpu power turned off */
		if (pm_runtime_suspended(&gpsPVRLDMDev->dev))
			break;

		if (try_count == 0)
			PVR_LOG(("enable_gpu_power off fail with pm_runtime_suspended"));
		schedule();
	} while (try_count--);

	/*this is debug for runtimepm power gating state*/
	{
		void __iomem *status;
		status = EXYNOS_PMUREG(0x4080);
		sgx_gpu_power_state = __raw_readl(status);
#ifdef PM_RUNTIME_DEBUG
		PVR_LOG(("disable_gpu_power: read register: 0x%x", sgx_gpu_power_state));
#endif
	}
#endif
	return 0;
}

int gpu_voltage_get(void)
{
	return sgx_gpu_vol;
}

/* gpu/drivers/gpu/pvr/services4/system/exynos5410/sec_clock.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX clock driver
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

#include "services_headers.h"
#include "sysinfo.h"

#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <linux/pm_qos.h>

#include "sec_clock.h"

#if defined(CONFIG_EXYNOS5410_BTS)
void __iomem *sgx_bts_base;
unsigned int sgx_clk_status;
#endif

//#define DEBUG_BW

/* clock control */
static struct clk	*sgx_core;
static struct clk	*sgx_hyd;
static struct clk	*g3d_clock_core_sub;
static struct clk	*g3d_clock_hydra_sub;
static struct clk	*g3d_clock_core;
static struct clk	*g3d_clock_hydra;
static struct clk	*mout_g3d;
static struct clk	*vpll_clock;
static struct clk	*vpll_src;
static struct clk	*fout_vpll_clock;

/* set sys parameters */
static int sgx_gpu_clk;
static int sgx_gpu_src_clk;

module_param(sgx_gpu_clk, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(sgx_gpu_clk, "SGX clock current value");

module_param(sgx_gpu_src_clk, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(sgx_gpu_src_clk, "SGX source clock current value");

/* end sys parameters */

int gpu_clks_get(void)
{
	struct platform_device *pdev;
	pdev = gpsPVRLDMDev;

	if (vpll_clock == NULL) {
		vpll_clock = clk_get(&pdev->dev, "mout_vpll");
		if (IS_ERR(vpll_clock)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find vpll clock"));
			return -1;
		}
	}

	if (vpll_src == NULL) {
		vpll_src = clk_get(&pdev->dev, "vpll_src");
		if (IS_ERR(vpll_src)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find vpll_src"));
			return -1;
		}
	}

	if (fout_vpll_clock == NULL) {
		fout_vpll_clock = clk_get(&pdev->dev, "fout_vpll");
		if (IS_ERR(fout_vpll_clock)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find vpll clock"));
			return -1;
		}
	}

	if (sgx_core == NULL) {
		sgx_core = clk_get(&pdev->dev, "sgx_core");
		if (IS_ERR(sgx_core)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find sgx_core clock"));
			return -1;
		}
	}

	if (sgx_hyd == NULL) {
		sgx_hyd = clk_get(&pdev->dev, "sgx_hyd");
		if (IS_ERR(sgx_hyd)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find sgx_hyd clock"));
			return -1;
		}
	}

	if (mout_g3d == NULL) {
		mout_g3d = clk_get(&pdev->dev, "mout_g3d");
		if (IS_ERR(mout_g3d)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find mout_g3d clock"));
			return -1;
		}
	}

	if (g3d_clock_core_sub == NULL) {
		g3d_clock_core_sub = clk_get(&pdev->dev, "sclk_g3d_core_sub");
		if (IS_ERR(g3d_clock_core_sub)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find g3d core sub clock"));
			return -1;
		}
	}

	if (g3d_clock_hydra_sub == NULL) {
		g3d_clock_hydra_sub = clk_get(&pdev->dev, "sclk_g3d_hydra_sub");
		if (IS_ERR(g3d_clock_hydra_sub)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find g3d hydra sub clock"));
			return -1;
		}
	}

	if (g3d_clock_core == NULL) {
		g3d_clock_core = clk_get(&pdev->dev, "sclk_g3d_core");
		if (IS_ERR(g3d_clock_core)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find g3d core clock"));
			return -1;
		}
	}

	if (g3d_clock_hydra == NULL) {
		g3d_clock_hydra = clk_get(&pdev->dev, "sclk_g3d_hydra");
		if (IS_ERR(g3d_clock_hydra)) {
			PVR_DPF((PVR_DBG_ERROR, "failed to find g3d hydra clock"));
			return -1;
		}
	}

#if defined(CONFIG_EXYNOS5410_BTS)
	sgx_bts_base = ioremap(EXYNOS5_PA_BTS_G3D0, PAGE_SIZE);
#endif

	return 0;
}

void gpu_clks_put(void)
{

	if (vpll_clock) {
		clk_put(vpll_clock);
		vpll_clock = 0;
	}

	if (vpll_src) {
		clk_put(vpll_src);
		vpll_src = 0;
	}

	if (fout_vpll_clock) {
		clk_put(fout_vpll_clock);
		fout_vpll_clock = 0;
	}

	if (mout_g3d) {
		clk_put(mout_g3d);
		mout_g3d = 0;
	}

	if (sgx_core) {
		clk_put(sgx_core);
		sgx_core = 0;
	}

	if (sgx_hyd) {
		clk_put(sgx_hyd);
		sgx_hyd = 0;
	}

	if (g3d_clock_core_sub) {
		clk_put(g3d_clock_core_sub);
		g3d_clock_core_sub = 0;
	}

	if (g3d_clock_hydra_sub) {
		clk_put(g3d_clock_hydra_sub);
		g3d_clock_hydra_sub = 0;
	}

	if (g3d_clock_core) {
		clk_put(g3d_clock_core);
		g3d_clock_core = 0;
	}

	if (g3d_clock_hydra) {
		clk_put(g3d_clock_hydra);
		g3d_clock_hydra = 0;
	}
#if defined(CONFIG_EXYNOS5410_BTS)
	iounmap(sgx_bts_base);
#endif
}

int gpu_clock_set_parent()
{
	int err = 0;
	err = clk_set_parent(mout_g3d, vpll_clock);
	if (err) {
		PVR_LOG(("SGX mout_g3d clk_set_parent fail!"));
		return err;
	}
	err = clk_set_parent(g3d_clock_core_sub, g3d_clock_core);
	if (err) {
		PVR_LOG(("SGX g3d_clock_core_sub clk_set_parent fail!"));
		return err;
	}
	err = clk_set_parent(g3d_clock_hydra_sub, g3d_clock_hydra);
	if (err) {
		PVR_LOG(("SGX g3d_clock_hydra_sub clk_set_parent fail!"));
		return err;
	}
	return err;
}

int gpu_clock_enable()
{
	int err = 0;
	err = clk_enable(sgx_core);
	if (err) {
		PVR_LOG(("SGX sgx_core clock enable fail!"));
		return err;
	}
	err = clk_enable(sgx_hyd);
	if (err) {
		PVR_LOG(("SGX sgx_hyd clock enable fail!"));
		return err;
	}
#if defined(CONFIG_EXYNOS5410_BTS)
	sgx_clk_status = 1;
#endif
	return err;
}

void gpu_clock_disable()
{
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	clk_disable(sgx_core);
	clk_disable(sgx_hyd);
#if defined(CONFIG_EXYNOS5410_BTS)
	sgx_clk_status = 0;
#endif
#endif
}

/*this function using for DVFS*/
void gpu_clock_set(int sgx_clk)
{
#ifdef DEBUG_BW
	int old_clk = clk_get_rate(g3d_clock_core)/MHZ;
#endif
	if (clk_get_rate(fout_vpll_clock)/MHZ != sgx_clk)
		sgx_gpu_src_clk = clk_set_rate(fout_vpll_clock, sgx_clk * MHZ);

	if (clk_get_rate(g3d_clock_core)/MHZ != sgx_clk)
		clk_set_rate(g3d_clock_core, sgx_clk * MHZ);

	if (clk_get_rate(g3d_clock_hydra)/MHZ != sgx_clk)
		clk_set_rate(g3d_clock_hydra, sgx_clk * MHZ);

	sgx_gpu_clk = clk_get_rate(g3d_clock_core)/MHZ;

#ifdef DEBUG_BW
	{
		unsigned int mif_sdiv;

		mif_sdiv = __raw_readl(EXYNOS5_BPLL_CON0);
		mif_sdiv &= 0x7;

#if defined(CONFIG_EXYNOS5410_BTS)
		{
			unsigned int bts = 0;
			if (sgx_clk_status && __raw_readl(sgx_bts_base+0))
				bts = __raw_readl(sgx_bts_base+0xc);
			else
				bts = 0;

			PVR_LOG(("SGX change clock [%d] Mhz -> [%d] MHz req [%d] MHz / M[%d] / B[%d]", old_clk, sgx_gpu_clk, sgx_clk, (800 / mif_sdiv), bts));
		}
#else
		PVR_LOG(("SGX change clock [%d] Mhz -> [%d] MHz req [%d] MHz / M[%d]", old_clk, sgx_gpu_clk, sgx_clk, (800 / mif_sdiv)));
#endif
	}
#endif
}

int gpu_clock_get(void)
{
	return sgx_gpu_clk;
}


/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS5410 - PLL support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/err.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <media/exynos_camera.h>
#include "board-odroidxu.h"

static int exynos5_gsc_clock_init(void)
{
	struct clk *clk_child;
	struct clk *clk_parent;
	struct clk *clk_isp_sensor;
	char sensor_name[20];
	int i;

	clk_child = clk_get(NULL, "aclk_300_gscl");
	if (IS_ERR(clk_child)) {
		pr_err("failed to get %s clock\n", "aclk_300_gscl");
		return PTR_ERR(clk_child);
	}
	clk_parent = clk_get(NULL, "dout_aclk_300_gscl");
	if (IS_ERR(clk_parent)) {
		clk_put(clk_child);
		pr_err("failed to get %s clock\n", "dout_aclk_300_gscl");
		return PTR_ERR(clk_child);
	}
	if (clk_set_parent(clk_child, clk_parent)) {
		clk_put(clk_child);
		clk_put(clk_parent);
		pr_err("Unable to set parent %s of clock %s.\n",
			"dout_aclk_300_gscl", "aclk_300_gscl");
		return PTR_ERR(clk_child);
	}
	clk_set_rate(clk_parent, 300000000);

	clk_put(clk_child);
	clk_put(clk_parent);

	/* Set MIPI-CSI source clock */
	clk_child = clk_get(NULL, "aclk_333_432_gscl");
	if (IS_ERR(clk_child)) {
		pr_err("failed to get %s clock\n", "aclk_333_432_gscl");
		return PTR_ERR(clk_child);
	}

	clk_parent = clk_get(NULL, "dout_aclk_333_432_gscl");
	if (IS_ERR(clk_parent)) {
		clk_put(clk_child);
		pr_err("failed to get %s clock\n", "dout_aclk_333_432_gscl");
		return PTR_ERR(clk_child);
	}

	if (clk_set_parent(clk_child, clk_parent)) {
		clk_put(clk_child);
		clk_put(clk_parent);
		pr_err("Unable to set parent %s of clock %s.\n",
			"dout_aclk_333_432_gscl", "aclk_333_432_gscl");
		return PTR_ERR(clk_child);
	}

	clk_put(clk_child);
	clk_put(clk_parent);

	/* Set Camera sensor source clock */
	for (i = 0; i < MAX_CAM_NUM; i++) {
		snprintf(sensor_name, sizeof(sensor_name), "sclk_isp_sensor%d", i);
		clk_isp_sensor = clk_get(NULL, sensor_name);
		if (IS_ERR(clk_isp_sensor)) {
			pr_err("failed to get sclk_isp_sensor%d clock\n", i);
			return PTR_ERR(clk_child);
		}
		clk_set_rate(clk_isp_sensor, 24000000);
		clk_put(clk_isp_sensor);
	}

	return 0;
}

static int exynos5_aclk_300_disp1_init(void)
{
	struct clk *aclk_300_disp1 = NULL;
	struct clk *dout_disp1 = NULL;
	struct clk *mout_dpll = NULL;
	int ret;

	aclk_300_disp1 = clk_get(NULL, "aclk_300_disp1");
	if (IS_ERR(aclk_300_disp1)) {
		pr_err("failed to get aclk for disp1\n");
		goto err_clk1;
	}

	dout_disp1 = clk_get(NULL, "dout_aclk_300_disp1");

	if (IS_ERR(dout_disp1)) {
		pr_err("failed to get dout_disp1 for disp1\n");
		goto err_clk2;
	}

	ret = clk_set_parent(aclk_300_disp1, dout_disp1);
	if (ret < 0) {
		pr_err("failed to clk_set_parent for disp1\n");
		goto err_clk2;
	}

	mout_dpll = clk_get(NULL, "mout_dpll");

	if (IS_ERR(mout_dpll)) {
		pr_err("failed to get mout_dpll for disp1\n");
		goto err_clk2;
	}

	ret = clk_set_parent(dout_disp1, mout_dpll);
	if (ret < 0) {
		pr_err("failed to clk_set_parent for disp1\n");
		goto err_clk2;
	}

	ret = clk_set_rate(dout_disp1, 300*1000*1000);
	if (ret < 0) {
		pr_err("failed to clk_set_rate of aclk_300_disp1 for disp1\n");
		goto err_clk2;
	}

	clk_put(dout_disp1);
	clk_put(mout_dpll);
	clk_put(aclk_300_disp1);
	return 0;

 err_clk2:
	clk_put(mout_dpll);
 err_clk1:
	clk_put(aclk_300_disp1);

	return -EINVAL;
}

static int exynos5_mfc_clock_init(void)
{
	struct clk *clk_child;
	struct clk *clk_parent;

	clk_child = clk_get(NULL, "aclk_333_pre");
	if (IS_ERR(clk_child)) {
		pr_err("failed to get %s clock\n", "aclk_333_pre");
		return PTR_ERR(clk_child);
	}

	clk_parent = clk_get(NULL, "mout_cpll");
	if (IS_ERR(clk_parent)) {
		clk_put(clk_child);
		pr_err("failed to get %s clock\n", "mout_cpll");
		return PTR_ERR(clk_child);
	}

	if (clk_set_parent(clk_child, clk_parent)) {
		clk_put(clk_child);
		clk_put(clk_parent);
		pr_err("Unable to set parent %s of clock %s.\n",
			"mout_cpll", "aclk_333_pre");
		return PTR_ERR(clk_child);
	}

	clk_put(clk_child);
	clk_put(clk_parent);

	clk_child = clk_get(NULL, "aclk_333");
	if (IS_ERR(clk_child)) {
		pr_err("failed to get %s clock\n", "aclk_333");
		return PTR_ERR(clk_child);
	}

	clk_parent = clk_get(NULL, "aclk_333_pre");
	if (IS_ERR(clk_parent)) {
		clk_put(clk_child);
		pr_err("failed to get %s clock\n", "aclk_333_pre");
		return PTR_ERR(clk_child);
	}

	if (clk_set_parent(clk_child, clk_parent)) {
		clk_put(clk_child);
		clk_put(clk_parent);
		pr_err("Unable to set parent %s of clock %s.\n",
			"aclk_333_pre", "aclk_333");
		return PTR_ERR(clk_child);
	}

	clk_set_rate(clk_parent, 333000000);

	clk_put(clk_child);
	clk_put(clk_parent);

	/* FIXME: W/A for MFC clock source setting */
	clk_child = clk_get(NULL, "aclk_333");
	if (IS_ERR(clk_child)) {
		pr_err("failed to get %s clock\n", "aclk_333");
		return PTR_ERR(clk_child);
	}

	clk_enable(clk_child);
	clk_disable(clk_child);

	clk_put(clk_child);

	return 0;
}

static int exynos5_aclk_200_disp1_init(void)
{
	struct clk *aclk_200_disp1;
	struct clk *aclk_200;

	aclk_200_disp1 = clk_get(NULL, "aclk_200_disp1");
	if (IS_ERR(aclk_200_disp1)) {
		pr_err("failed to get %s clock\n", "aclk_200_disp1");
		return PTR_ERR(aclk_200_disp1);
	}

	aclk_200 = clk_get(NULL, "aclk_200");
	if (IS_ERR(aclk_200)) {
		clk_put(aclk_200_disp1);
		pr_err("failed to get %s clock\n", "aclk_200");
		return PTR_ERR(aclk_200);
	}
	if (clk_set_parent(aclk_200_disp1, aclk_200)) {
		clk_put(aclk_200_disp1);
		clk_put(aclk_200);
		pr_err("Unable to set parent %s of clock %s.\n",
			"aclk_200_disp1", "aclk_200");
		return PTR_ERR(aclk_200_disp1);
	}

	clk_set_rate(aclk_200, 200*1000*1000);

	clk_put(aclk_200_disp1);
	clk_put(aclk_200);

	return 0;
}

static int exynos5_pcm_clock_init(void)
{
	struct clk *sclk_pcm0, *sclk_pcm1, *sclk_pcm2;

	sclk_pcm0 = clk_get(NULL, "sclk_pcm0");
	if (IS_ERR(sclk_pcm0)) {
		pr_err("failed to get %s clock\n", "sclk_pcm0");
		return PTR_ERR(sclk_pcm0);
	}

	sclk_pcm1 = clk_get(NULL, "sclk_pcm1");
	if (IS_ERR(sclk_pcm1)) {
		pr_err("failed to get %s clock\n", "sclk_pcm1");
		return PTR_ERR(sclk_pcm1);
	}

	sclk_pcm2 = clk_get(NULL, "sclk_pcm2");
	if (IS_ERR(sclk_pcm2)) {
		pr_err("failed to get %s clock\n", "sclk_pcm2");
		return PTR_ERR(sclk_pcm2);
	}

	clk_set_rate(sclk_pcm0, 4*1000*1000);
	clk_set_rate(sclk_pcm1, 4*1000*1000);
	clk_set_rate(sclk_pcm2, 4*1000*1000);

	clk_put(sclk_pcm0);
	clk_put(sclk_pcm1);
	clk_put(sclk_pcm2);

	return 0;
}

static int exynos5_spi_clock_init(void)
{
	struct clk *child_clk = NULL;
	struct clk *parent_clk = NULL;
	char clk_name[16];
	int i;

	for (i = 0; i < 3; i++) {
		snprintf(clk_name, sizeof(clk_name), "dout_spi%d", i);

		child_clk = clk_get(NULL, clk_name);
		if (IS_ERR(child_clk)) {
			pr_err("Failed to get %s clk\n", clk_name);
			return PTR_ERR(child_clk);
		}

		parent_clk = clk_get(NULL, "mout_cpll");
		if (IS_ERR(parent_clk)) {
			clk_put(child_clk);
			pr_err("Failed to get mout_cpll clk\n");
			return PTR_ERR(parent_clk);
		}

		if (clk_set_parent(child_clk, parent_clk)) {
			clk_put(child_clk);
			clk_put(parent_clk);
			pr_err("Unable to set parent %s of clock %s\n",
					parent_clk->name, child_clk->name);
			return PTR_ERR(child_clk);
		}

		clk_set_rate(child_clk, 80 * 1000 * 1000);

		clk_put(parent_clk);
		clk_put(child_clk);
	}

	return 0;
}

static int exynos5_mmc_clock_init(void)
{
	struct clk *sclk_mmc0, *sclk_mmc1, *sclk_mmc2, *mout_cpll;
	struct clk *dw_mmc0, *dw_mmc1, *dw_mmc2;

	sclk_mmc0 = clk_get(NULL, "sclk_mmc0");
	sclk_mmc1 = clk_get(NULL, "sclk_mmc1");
	sclk_mmc2 = clk_get(NULL, "sclk_mmc2");
#if defined(CONFIG_SDMMC_CLOCK_CPLL)
	mout_cpll = clk_get(NULL, "mout_cpll");
#else
	mout_cpll = clk_get(NULL, "mout_epll");
#endif	
	dw_mmc0 = clk_get_sys("dw_mmc.0", "sclk_dwmci");
	dw_mmc1 = clk_get_sys("dw_mmc.1", "sclk_dwmci");
	dw_mmc2 = clk_get_sys("dw_mmc.2", "sclk_dwmci");

	if (clk_set_parent(sclk_mmc0, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, sclk_mmc0->name);
	if (clk_set_parent(sclk_mmc1, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, sclk_mmc1->name);
	if (clk_set_parent(sclk_mmc2, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, sclk_mmc2->name);

#if defined(CONFIG_SDMMC_CLOCK_CPLL)
	clk_set_rate(sclk_mmc0, 640 * MHZ);
	clk_set_rate(sclk_mmc1, 640 * MHZ);
	clk_set_rate(sclk_mmc2, 640 * MHZ);
	clk_set_rate(dw_mmc0, 640 * MHZ);
	clk_set_rate(dw_mmc1, 640 * MHZ);
	clk_set_rate(dw_mmc2, 640 * MHZ);
#else
	clk_set_rate(sclk_mmc0, 400 * MHZ);
	clk_set_rate(sclk_mmc1, 400 * MHZ);
	clk_set_rate(sclk_mmc2, 400 * MHZ);
	clk_set_rate(dw_mmc0, 400 * MHZ);
	clk_set_rate(dw_mmc1, 400 * MHZ);
	clk_set_rate(dw_mmc2, 400 * MHZ);
#endif	

	clk_put(sclk_mmc0);
	clk_put(sclk_mmc1);
	clk_put(sclk_mmc2);
	clk_put(mout_cpll);
	clk_put(dw_mmc0);
	clk_put(dw_mmc1);
	clk_put(dw_mmc2);

	return 0;
}

static int exynos5_mpll_bpll_clock_init(void)
{
	struct clk *mout_mpll_bpll, *mout_mpll_user;

	mout_mpll_bpll = clk_get(NULL, "mout_mpll_bpll");
	mout_mpll_user = clk_get(NULL, "mout_mpll_user");

	if (clk_set_parent(mout_mpll_bpll, mout_mpll_user))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_mpll_user->name, mout_mpll_bpll->name);

	clk_put(mout_mpll_bpll);
	clk_put(mout_mpll_user);

	return 0;
}

static int exynos5_mipi_clock_init(void)
{
	struct clk *mipihsi_txbase;

	mipihsi_txbase = clk_get(NULL, "exynos5_clk_mipihsi");
	clk_set_rate(mipihsi_txbase, 100 * MHZ);
	clk_put(mipihsi_txbase);

	return 0;
}

static int exynos5_acp_clock_init(void)
{
	struct clk *aclk_acp, *pclk_acp;

	aclk_acp = clk_get(NULL, "aclk_acp");
	pclk_acp = clk_get(NULL, "pclk_acp");

	clk_set_rate(aclk_acp, 267000000);
	clk_set_rate(pclk_acp, 134000000);

	clk_put(aclk_acp);
	clk_put(pclk_acp);

	return 0;
}

static int exynos5_uart_clock_init(void)
{
	struct clk *uart0, *uart1, *uart2, *uart3;
	struct clk *mout_cpll;

	uart0 = clk_get_sys("s5pv210-uart.0", "uclk1");
	uart1 = clk_get_sys("s5pv210-uart.1", "uclk1");
	uart2 = clk_get_sys("s5pv210-uart.2", "uclk1");
	uart3 = clk_get_sys("s5pv210-uart.3", "uclk1");
	mout_cpll = clk_get(NULL, "mout_cpll");

	if (clk_set_parent(uart0, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, uart0->name);
	else
		clk_set_rate(uart0, 107 * MHZ);

	if (clk_set_parent(uart1, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, uart1->name);
	else
		clk_set_rate(uart1, 107 * MHZ);

	if (clk_set_parent(uart2, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, uart2->name);
	else
		clk_set_rate(uart2, 107 * MHZ);

	if (clk_set_parent(uart3, mout_cpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_cpll->name, uart3->name);
	else
		clk_set_rate(uart3, 107 * MHZ);

	clk_put(uart0);
	clk_put(uart1);
	clk_put(uart2);
	clk_put(uart3);

	return 0;
}

#if defined(CONFIG_USB_HSIC_USB3503)
static int odroidxu_clkout_init(void)
{
	struct clk *clkout = NULL;
	struct clk *xusbxti = NULL;
	struct clk *usb_clkout = NULL;
	unsigned long clkout_clk=0;

	xusbxti = clk_get(NULL, "xusbxti");
	if (IS_ERR(xusbxti)) {
		clk_put(xusbxti);
		pr_err("failed to get %s clock\n", "xxti");
		return PTR_ERR(xusbxti);
	}

	clkout = clk_get(NULL, "clkout");
	if (IS_ERR(clkout)) {
		pr_err("failed to get %s clock\n", "clkout");
		return PTR_ERR(clkout);
	}
	clk_set_parent(clkout, xusbxti);
	clk_put(clkout);
	clk_put(xusbxti);

	usb_clkout = clk_get(NULL, "clkout");
	if (!usb_clkout) {
		pr_err("%s: cannot get get clkout (clkout)\n", __func__);
		return PTR_ERR(usb_clkout);
	}
	clk_enable(usb_clkout);

	clkout_clk = clk_get_rate(usb_clkout);
	printk("%s [%d] : clkout_clk = %ld \n\n",__func__,__LINE__, clkout_clk);

	return 0;
}
#endif

void __init exynos5_odroidxu_clock_init(void)
{
	if (exynos5_uart_clock_init())
		pr_err("failed to init uart clock init\n");

	if (exynos5_mipi_clock_init())
		pr_err("failed to init mipi clock init\n");

	if (exynos5_mpll_bpll_clock_init())
		pr_err("failed to init mpll_bpll clock init\n");

	if (exynos5_mmc_clock_init())
		pr_err("failed to init emmc clock init\n");

	if (exynos5_gsc_clock_init())
		pr_err("failed to gscaler clock init\n");

	if (exynos5_mfc_clock_init())
		pr_err("failed to MFC clock init\n");

	if (exynos5_aclk_300_disp1_init())
		pr_err("failed to init aclk_300_disp1\n");

	if (exynos5_aclk_200_disp1_init())
		pr_err("failed to init aclk_200_disp1\n");

	if (exynos5_pcm_clock_init())
		pr_err("failed to init pcm clock init\n");

	if (exynos5_spi_clock_init())
		pr_err("failed to init spi clock init\n");

	if (exynos5_acp_clock_init())
		pr_err("failed to init acp clock init\n");

#if defined(CONFIG_USB_HSIC_USB3503)
	if (odroidxu_clkout_init())
		pr_err("failed to init clkout init\n");
#endif
}

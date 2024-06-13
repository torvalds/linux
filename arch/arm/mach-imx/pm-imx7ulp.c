// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *   Author: Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "common.h"

#define SMC_PMCTRL		0x10
#define BP_PMCTRL_PSTOPO        16
#define PSTOPO_PSTOP3		0x3
#define PSTOPO_PSTOP2		0x2
#define PSTOPO_PSTOP1		0x1
#define BP_PMCTRL_RUNM		8
#define RUNM_RUN		0
#define BP_PMCTRL_STOPM		0
#define STOPM_STOP		0

#define BM_PMCTRL_PSTOPO	(3 << BP_PMCTRL_PSTOPO)
#define BM_PMCTRL_RUNM		(3 << BP_PMCTRL_RUNM)
#define BM_PMCTRL_STOPM		(7 << BP_PMCTRL_STOPM)

static void __iomem *smc1_base;

int imx7ulp_set_lpm(enum ulp_cpu_pwr_mode mode)
{
	u32 val = readl_relaxed(smc1_base + SMC_PMCTRL);

	/* clear all */
	val &= ~(BM_PMCTRL_RUNM | BM_PMCTRL_STOPM | BM_PMCTRL_PSTOPO);

	switch (mode) {
	case ULP_PM_RUN:
		/* system/bus clock enabled */
		val |= PSTOPO_PSTOP3 << BP_PMCTRL_PSTOPO;
		break;
	case ULP_PM_WAIT:
		/* system clock disabled, bus clock enabled */
		val |= PSTOPO_PSTOP2 << BP_PMCTRL_PSTOPO;
		break;
	case ULP_PM_STOP:
		/* system/bus clock disabled */
		val |= PSTOPO_PSTOP1 << BP_PMCTRL_PSTOPO;
		break;
	default:
		return -EINVAL;
	}

	writel_relaxed(val, smc1_base + SMC_PMCTRL);

	return 0;
}

void __init imx7ulp_pm_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx7ulp-smc1");
	smc1_base = of_iomap(np, 0);
	of_node_put(np);
	WARN_ON(!smc1_base);

	imx7ulp_set_lpm(ULP_PM_RUN);
}

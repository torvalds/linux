// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *   Author: Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define SMC_PMCTRL		0x10
#define BP_PMCTRL_PSTOPO        16
#define PSTOPO_PSTOP3		0x3

void __init imx7ulp_pm_init(void)
{
	struct device_node *np;
	void __iomem *smc1_base;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx7ulp-smc1");
	smc1_base = of_iomap(np, 0);
	WARN_ON(!smc1_base);

	/* Partial Stop mode 3 with system/bus clock enabled */
	writel_relaxed(PSTOPO_PSTOP3 << BP_PMCTRL_PSTOPO,
		       smc1_base + SMC_PMCTRL);
	iounmap(smc1_base);
}

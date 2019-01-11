// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *   Author: Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>

#include "common.h"
#include "hardware.h"

static void __init imx7ulp_init_machine(void)
{
	imx7ulp_pm_init();

	mxc_set_cpu_type(MXC_CPU_IMX7ULP);
	of_platform_default_populate(NULL, NULL, imx_soc_device_init());
}

static const char *const imx7ulp_dt_compat[] __initconst = {
	"fsl,imx7ulp",
	NULL,
};

DT_MACHINE_START(IMX7ulp, "Freescale i.MX7ULP (Device Tree)")
	.init_machine	= imx7ulp_init_machine,
	.dt_compat	= imx7ulp_dt_compat,
MACHINE_END

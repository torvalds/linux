// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *   Author: Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/irqchip.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <asm/mach/arch.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define SIM_JTAG_ID_REG		0x8c

static void __init imx7ulp_set_revision(void)
{
	struct regmap *sim;
	u32 revision;

	sim = syscon_regmap_lookup_by_compatible("fsl,imx7ulp-sim");
	if (IS_ERR(sim)) {
		pr_warn("failed to find fsl,imx7ulp-sim regmap!\n");
		return;
	}

	if (regmap_read(sim, SIM_JTAG_ID_REG, &revision)) {
		pr_warn("failed to read sim regmap!\n");
		return;
	}

	/*
	 * bit[31:28] of JTAG_ID register defines revision as below from B0:
	 * 0001        B0
	 * 0010        B1
	 */
	switch (revision >> 28) {
	case 1:
		imx_set_soc_revision(IMX_CHIP_REVISION_2_0);
		break;
	case 2:
		imx_set_soc_revision(IMX_CHIP_REVISION_2_1);
		break;
	default:
		imx_set_soc_revision(IMX_CHIP_REVISION_1_0);
		break;
	}
}

static void __init imx7ulp_init_machine(void)
{
	imx7ulp_pm_init();

	mxc_set_cpu_type(MXC_CPU_IMX7ULP);
	imx7ulp_set_revision();
	of_platform_default_populate(NULL, NULL, NULL);
}

static const char *const imx7ulp_dt_compat[] __initconst = {
	"fsl,imx7ulp",
	NULL,
};

static void __init imx7ulp_init_late(void)
{
	if (IS_ENABLED(CONFIG_ARM_IMX_CPUFREQ_DT))
		platform_device_register_simple("imx-cpufreq-dt", -1, NULL, 0);

	imx7ulp_cpuidle_init();
}

DT_MACHINE_START(IMX7ulp, "Freescale i.MX7ULP (Device Tree)")
	.init_machine	= imx7ulp_init_machine,
	.dt_compat	= imx7ulp_dt_compat,
	.init_late	= imx7ulp_init_late,
MACHINE_END

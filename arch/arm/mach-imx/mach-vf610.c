// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 */

#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>

#include <linux/irqchip.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

#include "common.h"
#include "hardware.h"

#define MSCM_CPxCOUNT		0x00c
#define MSCM_CPxCFG1		0x014

static void __init vf610_detect_cpu(void)
{
	struct device_node *np;
	u32 cpxcount, cpxcfg1;
	unsigned int cpu_type;
	void __iomem *mscm;

	np = of_find_compatible_node(NULL, NULL, "fsl,vf610-mscm-cpucfg");
	if (WARN_ON(!np))
		return;

	mscm = of_iomap(np, 0);
	of_node_put(np);

	if (WARN_ON(!mscm))
		return;

	cpxcount = readl_relaxed(mscm + MSCM_CPxCOUNT);
	cpxcfg1  = readl_relaxed(mscm + MSCM_CPxCFG1);

	iounmap(mscm);

	cpu_type = cpxcount ? MXC_CPU_VF600 : MXC_CPU_VF500;

	if (cpxcfg1)
		cpu_type |= MXC_CPU_VFx10;

	mxc_set_cpu_type(cpu_type);
}

static void __init vf610_init_machine(void)
{
	vf610_detect_cpu();

	of_platform_default_populate(NULL, NULL, NULL);
}

static const char * const vf610_dt_compat[] __initconst = {
	"fsl,vf500",
	"fsl,vf510",
	"fsl,vf600",
	"fsl,vf610",
	"fsl,vf610m4",
	NULL,
};

DT_MACHINE_START(VYBRID_VF610, "Freescale Vybrid VF5xx/VF6xx (Device Tree)")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.init_machine   = vf610_init_machine,
	.dt_compat	= vf610_dt_compat,
MACHINE_END

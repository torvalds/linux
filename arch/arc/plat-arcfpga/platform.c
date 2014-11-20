/*
 * ARC FPGA Platform support code
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <asm/mach_desc.h>
#include <plat/smp.h>

/*----------------------- Machine Descriptions ------------------------------
 *
 * Machine description is simply a set of platform/board specific callbacks
 * This is not directly related to DeviceTree based dynamic device creation,
 * however as part of early device tree scan, we also select the right
 * callback set, by matching the DT compatible name.
 */

static const char *legacy_fpga_compat[] __initconst = {
	"snps,arc-angel4",
	"snps,arc-ml509",
	NULL,
};

MACHINE_START(LEGACY_FPGA, "legacy_fpga")
	.dt_compat	= legacy_fpga_compat,
#ifdef CONFIG_ISS_SMP_EXTN
	.init_early	= iss_model_init_early_smp,
	.init_smp	= iss_model_init_smp,
#endif
MACHINE_END

static const char *simulation_compat[] __initconst = {
	"snps,nsim",
	"snps,nsimosci",
	NULL,
};

MACHINE_START(SIMULATION, "simulation")
	.dt_compat	= simulation_compat,
MACHINE_END

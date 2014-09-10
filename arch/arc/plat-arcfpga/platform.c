/*
 * ARC FPGA Platform support code
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/console.h>
#include <asm/setup.h>
#include <asm/clk.h>
#include <asm/mach_desc.h>
#include <plat/memmap.h>
#include <plat/smp.h>
#include <plat/irq.h>

static void __init plat_fpga_early_init(void)
{
	pr_info("[plat-arcfpga]: registering early dev resources\n");

#ifdef CONFIG_ISS_SMP_EXTN
	iss_model_init_early_smp();
#endif
}

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
	.init_early	= plat_fpga_early_init,
#ifdef CONFIG_ISS_SMP_EXTN
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

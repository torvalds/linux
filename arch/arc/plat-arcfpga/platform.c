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
#include <linux/of_platform.h>
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

static void __init plat_fpga_populate_dev(void)
{
	/*
	 * Traverses flattened DeviceTree - registering platform devices
	 * (if any) complete with their resources
	 */
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

/*----------------------- Machine Descriptions ------------------------------
 *
 * Machine description is simply a set of platform/board specific callbacks
 * This is not directly related to DeviceTree based dynamic device creation,
 * however as part of early device tree scan, we also select the right
 * callback set, by matching the DT compatible name.
 */

static const char *aa4_compat[] __initconst = {
	"snps,arc-angel4",
	NULL,
};

MACHINE_START(ANGEL4, "angel4")
	.dt_compat	= aa4_compat,
	.init_early	= plat_fpga_early_init,
	.init_machine	= plat_fpga_populate_dev,
	.init_irq	= plat_fpga_init_IRQ,
#ifdef CONFIG_ISS_SMP_EXTN
	.init_smp	= iss_model_init_smp,
#endif
MACHINE_END

static const char *ml509_compat[] __initconst = {
	"snps,arc-ml509",
	NULL,
};

MACHINE_START(ML509, "ml509")
	.dt_compat	= ml509_compat,
	.init_early	= plat_fpga_early_init,
	.init_machine	= plat_fpga_populate_dev,
	.init_irq	= plat_fpga_init_IRQ,
#ifdef CONFIG_SMP
	.init_smp	= iss_model_init_smp,
#endif
MACHINE_END

static const char *nsimosci_compat[] __initconst = {
	"snps,nsimosci",
	NULL,
};

MACHINE_START(NSIMOSCI, "nsimosci")
	.dt_compat	= nsimosci_compat,
	.init_early	= NULL,
	.init_machine	= plat_fpga_populate_dev,
	.init_irq	= NULL,
MACHINE_END

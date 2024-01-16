// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC simulation Platform support code
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/init.h>
#include <asm/mach_desc.h>

/*----------------------- Machine Descriptions ------------------------------
 *
 * Machine description is simply a set of platform/board specific callbacks
 * This is not directly related to DeviceTree based dynamic device creation,
 * however as part of early device tree scan, we also select the right
 * callback set, by matching the DT compatible name.
 */

static const char *simulation_compat[] __initconst = {
#ifdef CONFIG_ISA_ARCOMPACT
	"snps,nsim",
	"snps,nsimosci",
#else
	"snps,nsimosci_hs",
	"snps,zebu_hs",
#endif
	NULL,
};

MACHINE_START(SIMULATION, "simulation")
	.dt_compat	= simulation_compat,
MACHINE_END

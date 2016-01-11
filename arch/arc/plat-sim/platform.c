/*
 * ARC simulation Platform support code
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
	"snps,nsim",
	"snps,nsim_hs",
	"snps,nsimosci",
	"snps,nsimosci_hs",
	NULL,
};

MACHINE_START(SIMULATION, "simulation")
	.dt_compat	= simulation_compat,
MACHINE_END

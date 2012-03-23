/*
 *  linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <mach/motherboard.h>
#define V2M_PA_CS7 0x10000000

#include "core.h"

extern void versatile_secondary_startup(void);

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	ct_desc->init_cpu_map();
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	ct_desc->smp_enable(max_cpus);

	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The boot monitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 */
	writel(~0, MMIO_P2V(V2M_SYS_FLAGSCLR));
	writel(virt_to_phys(versatile_secondary_startup),
		MMIO_P2V(V2M_SYS_FLAGSSET));
}

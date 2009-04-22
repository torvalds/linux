/*
 * Author: Xianghua Xiao <x.xiao@freescale.com>
 *         Zhang Wei <wei.zhang@freescale.com>
 *
 * Copyright 2006 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/code-patching.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pci-bridge.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>

#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"

extern void __secondary_start_mpc86xx(void);
extern unsigned long __secondary_hold_acknowledge;

#define MCM_PORT_CONFIG_OFFSET	0x10

/* Offset from CCSRBAR */
#define MPC86xx_MCM_OFFSET      (0x1000)
#define MPC86xx_MCM_SIZE        (0x1000)

static void __init
smp_86xx_release_core(int nr)
{
	__be32 __iomem *mcm_vaddr;
	unsigned long pcr;

	if (nr < 0 || nr >= NR_CPUS)
		return;

	/*
	 * Startup Core #nr.
	 */
	mcm_vaddr = ioremap(get_immrbase() + MPC86xx_MCM_OFFSET,
			    MPC86xx_MCM_SIZE);
	pcr = in_be32(mcm_vaddr + (MCM_PORT_CONFIG_OFFSET >> 2));
	pcr |= 1 << (nr + 24);
	out_be32(mcm_vaddr + (MCM_PORT_CONFIG_OFFSET >> 2), pcr);

	iounmap(mcm_vaddr);
}


static void __init
smp_86xx_kick_cpu(int nr)
{
	unsigned int save_vector;
	unsigned long target, flags;
	int n = 0;
	unsigned int *vector = (unsigned int *)(KERNELBASE + 0x100);

	if (nr < 0 || nr >= NR_CPUS)
		return;

	pr_debug("smp_86xx_kick_cpu: kick CPU #%d\n", nr);

	local_irq_save(flags);

	/* Save reset vector */
	save_vector = *vector;

	/* Setup fake reset vector to call __secondary_start_mpc86xx. */
	target = (unsigned long) __secondary_start_mpc86xx;
	patch_branch(vector, target, BRANCH_SET_LINK);

	/* Kick that CPU */
	smp_86xx_release_core(nr);

	/* Wait a bit for the CPU to take the exception. */
	while ((__secondary_hold_acknowledge != nr) && (n++, n < 1000))
		mdelay(1);

	/* Restore the exception vector */
	*vector = save_vector;
	flush_icache_range((unsigned long) vector, (unsigned long) vector + 4);

	local_irq_restore(flags);

	pr_debug("wait CPU #%d for %d msecs.\n", nr, n);
}


static void __init
smp_86xx_setup_cpu(int cpu_nr)
{
	mpic_setup_this_cpu();
}


struct smp_ops_t smp_86xx_ops = {
	.message_pass = smp_mpic_message_pass,
	.probe = smp_mpic_probe,
	.kick_cpu = smp_86xx_kick_cpu,
	.setup_cpu = smp_86xx_setup_cpu,
	.take_timebase = smp_generic_take_timebase,
	.give_timebase = smp_generic_give_timebase,
};


void __init
mpc86xx_smp_init(void)
{
	smp_ops = &smp_86xx_ops;
}

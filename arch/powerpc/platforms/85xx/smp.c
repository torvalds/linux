/*
 * Author: Andy Fleming <afleming@freescale.com>
 * 	   Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2006-2008 Freescale Semiconductor Inc.
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
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mpic.h>
#include <asm/cacheflush.h>
#include <asm/dbell.h>

#include <sysdev/fsl_soc.h>

extern volatile unsigned long __secondary_hold_acknowledge;
extern void __early_start(void);

#define BOOT_ENTRY_ADDR_UPPER	0
#define BOOT_ENTRY_ADDR_LOWER	1
#define BOOT_ENTRY_R3_UPPER	2
#define BOOT_ENTRY_R3_LOWER	3
#define BOOT_ENTRY_RESV		4
#define BOOT_ENTRY_PIR		5
#define BOOT_ENTRY_R6_UPPER	6
#define BOOT_ENTRY_R6_LOWER	7
#define NUM_BOOT_ENTRY		8
#define SIZE_BOOT_ENTRY		(NUM_BOOT_ENTRY * sizeof(u32))

static void __init
smp_85xx_kick_cpu(int nr)
{
	unsigned long flags;
	const u64 *cpu_rel_addr;
	__iomem u32 *bptr_vaddr;
	struct device_node *np;
	int n = 0;

	WARN_ON (nr < 0 || nr >= NR_CPUS);

	pr_debug("smp_85xx_kick_cpu: kick CPU #%d\n", nr);

	np = of_get_cpu_node(nr, NULL);
	cpu_rel_addr = of_get_property(np, "cpu-release-addr", NULL);

	if (cpu_rel_addr == NULL) {
		printk(KERN_ERR "No cpu-release-addr for cpu %d\n", nr);
		return;
	}

	/* Map the spin table */
	bptr_vaddr = ioremap(*cpu_rel_addr, SIZE_BOOT_ENTRY);

	local_irq_save(flags);

	out_be32(bptr_vaddr + BOOT_ENTRY_PIR, nr);
	out_be32(bptr_vaddr + BOOT_ENTRY_ADDR_LOWER, __pa(__early_start));

	/* Wait a bit for the CPU to ack. */
	while ((__secondary_hold_acknowledge != nr) && (++n < 1000))
		mdelay(1);

	local_irq_restore(flags);

	iounmap(bptr_vaddr);

	pr_debug("waited %d msecs for CPU #%d.\n", n, nr);
}

static void __init
smp_85xx_basic_setup(int cpu_nr)
{
	/* Clear any pending timer interrupts */
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);

	/* Enable decrementer interrupt */
	mtspr(SPRN_TCR, TCR_DIE);
}

static void __init
smp_85xx_setup_cpu(int cpu_nr)
{
	mpic_setup_this_cpu();

	smp_85xx_basic_setup(cpu_nr);
}

struct smp_ops_t smp_85xx_ops = {
	.kick_cpu = smp_85xx_kick_cpu,
};

static int __init smp_dummy_probe(void)
{
	return NR_CPUS;
}

void __init mpc85xx_smp_init(void)
{
	struct device_node *np;

	smp_85xx_ops.message_pass = NULL;

	np = of_find_node_by_type(NULL, "open-pic");
	if (np) {
		smp_85xx_ops.probe = smp_mpic_probe;
		smp_85xx_ops.setup_cpu = smp_85xx_setup_cpu;
		smp_85xx_ops.message_pass = smp_mpic_message_pass;
	} else {
		smp_85xx_ops.probe = smp_dummy_probe;
		smp_85xx_ops.setup_cpu = smp_85xx_basic_setup;
	}

	if (cpu_has_feature(CPU_FTR_DBELL))
		smp_85xx_ops.message_pass = smp_dbell_message_pass;

	BUG_ON(!smp_85xx_ops.message_pass);

	smp_ops = &smp_85xx_ops;
}

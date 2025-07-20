// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * SMP support functions for Microwatt
 * Copyright 2025 Paul Mackerras <paulus@ozlabs.org>
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <asm/early_ioremap.h>
#include <asm/ppc-opcode.h>
#include <asm/reg.h>
#include <asm/smp.h>
#include <asm/xics.h>

#include "microwatt.h"

static void __init microwatt_smp_probe(void)
{
	xics_smp_probe();
}

static void microwatt_smp_setup_cpu(int cpu)
{
	if (cpu != 0)
		xics_setup_cpu();
}

static struct smp_ops_t microwatt_smp_ops = {
	.probe		= microwatt_smp_probe,
	.message_pass	= NULL,		/* Use smp_muxed_ipi_message_pass */
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= microwatt_smp_setup_cpu,
};

/* XXX get from device tree */
#define SYSCON_BASE	0xc0000000
#define SYSCON_LENGTH	0x100

#define SYSCON_CPU_CTRL	0x58

void __init microwatt_init_smp(void)
{
	volatile unsigned char __iomem *syscon;
	int ncpus;
	int timeout;

	syscon = early_ioremap(SYSCON_BASE, SYSCON_LENGTH);
	if (syscon == NULL) {
		pr_err("Failed to map SYSCON\n");
		return;
	}
	ncpus = (readl(syscon + SYSCON_CPU_CTRL) >> 8) & 0xff;
	if (ncpus < 2)
		goto out;

	smp_ops = &microwatt_smp_ops;

	/*
	 * Write two instructions at location 0:
	 * mfspr r3, PIR
	 * b __secondary_hold
	 */
	*(unsigned int *)KERNELBASE = PPC_RAW_MFSPR(3, SPRN_PIR);
	*(unsigned int *)(KERNELBASE+4) = PPC_RAW_BRANCH(&__secondary_hold - (char *)(KERNELBASE+4));

	/* enable the other CPUs, they start at location 0 */
	writel((1ul << ncpus) - 1, syscon + SYSCON_CPU_CTRL);

	timeout = 10000;
	while (!__secondary_hold_acknowledge) {
		if (--timeout == 0)
			break;
		barrier();
	}

 out:
	early_iounmap((void *)syscon, SYSCON_LENGTH);
}

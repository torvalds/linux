// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Lubomir Rintel <lkundrak@v3.sk>
 */
#include <linux/io.h>
#include <asm/smp_scu.h>
#include <asm/smp.h>
#include "addr-map.h"

#define SW_BRANCH_VIRT_ADDR	CIU_REG(0x24)

static int mmp3_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	/*
	 * Apparently, the boot ROM on the second core spins on this
	 * register becoming non-zero and then jumps to the address written
	 * there. No IPIs involved.
	 */
	__raw_writel(__pa_symbol(secondary_startup), SW_BRANCH_VIRT_ADDR);
	return 0;
}

static void mmp3_smp_prepare_cpus(unsigned int max_cpus)
{
	scu_enable(SCU_VIRT_BASE);
}

static const struct smp_operations mmp3_smp_ops __initconst = {
	.smp_prepare_cpus	= mmp3_smp_prepare_cpus,
	.smp_boot_secondary	= mmp3_boot_secondary,
};
CPU_METHOD_OF_DECLARE(mmp3_smp, "marvell,mmp3-smp", &mmp3_smp_ops);

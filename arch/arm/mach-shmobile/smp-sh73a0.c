// SPDX-License-Identifier: GPL-2.0
/*
 * SMP support for R-Mobile / SH-Mobile - sh73a0 portion
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <asm/smp_plat.h>

#include "common.h"
#include "sh73a0.h"

#define CPG_BASE2	0xe6151000
#define WUPCR		0x10	/* System-CPU Wake Up Control Register */
#define SRESCR		0x18	/* System-CPU Software Reset Control Register */
#define PSTR		0x40	/* System-CPU Power Status Register */

#define SYSC_BASE	0xe6180000
#define SBAR		0x20	/* SYS Boot Address Register */

#define AP_BASE		0xe6f10000
#define APARMBAREA	0x20	/* Address Translation Area Register */

#define SH73A0_SCU_BASE 0xf0000000

static int sh73a0_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned int lcpu = cpu_logical_map(cpu);
	void __iomem *cpg2 = ioremap(CPG_BASE2, PAGE_SIZE);

	if (((readl(cpg2 + PSTR) >> (4 * lcpu)) & 3) == 3)
		writel(1 << lcpu, cpg2 + WUPCR);	/* wake up */
	else
		writel(1 << lcpu, cpg2 + SRESCR);	/* reset */
	iounmap(cpg2);
	return 0;
}

static void __init sh73a0_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *ap, *sysc;

	if (!request_mem_region(0, SZ_4K, "Boot Area")) {
		pr_err("Failed to request boot area\n");
		return;
	}

	/* Map the reset vector (in headsmp.S) */
	ap = ioremap(AP_BASE, PAGE_SIZE);
	sysc = ioremap(SYSC_BASE, PAGE_SIZE);
	writel(0, ap + APARMBAREA);      /* 4k */
	writel(__pa(shmobile_boot_vector), sysc + SBAR);
	iounmap(sysc);
	iounmap(ap);

	/* setup sh73a0 specific SCU bits */
	shmobile_smp_scu_prepare_cpus(SH73A0_SCU_BASE, max_cpus);
}

const struct smp_operations sh73a0_smp_ops __initconst = {
	.smp_prepare_cpus	= sh73a0_smp_prepare_cpus,
	.smp_boot_secondary	= sh73a0_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_can_disable	= shmobile_smp_cpu_can_disable,
	.cpu_die		= shmobile_smp_scu_cpu_die,
	.cpu_kill		= shmobile_smp_scu_cpu_kill,
#endif
};

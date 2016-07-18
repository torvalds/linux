/*
 * SMP support for the Nintendo 3DS
 *
 * Copyright (C) 2016 Sergi Granell
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>

#include <asm/smp_scu.h>

#include <mach/platsmp.h>

/*
 * CPU1 waits for IPI Interrupt 0x1,
 * 0x1FFFFFDC is where it expects the entrypoint.
 */

#define SECONDARY_STARTUP_ADDR	0x1FFFFFDC
#define SCU_BASE_ADDR		0x17E00000

extern void smp_cross_call(const struct cpumask *target, unsigned int ipinr);

static int nintendo3ds_smp_boot_secondary(unsigned int cpu,
				    struct task_struct *idle)
{
	//arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	smp_cross_call(cpumask_of(cpu), cpu);

	return 0;
}

static void nintendo3ds_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *scu_base;
	void __iomem *boot_addr;

	scu_base = ioremap(SCU_BASE_ADDR, SZ_256);
	scu_enable(scu_base);
	iounmap(scu_base);

	boot_addr = ioremap((phys_addr_t)SECONDARY_STARTUP_ADDR,
			       sizeof(phys_addr_t));

	/* Set CPU boot address */
	writel(virt_to_phys(nintendo3ds_secondary_startup),
		boot_addr);

	iounmap(boot_addr);
}

static const struct smp_operations nintendo3ds_smp_ops __initconst = {
	.smp_prepare_cpus	= nintendo3ds_smp_prepare_cpus,
	.smp_boot_secondary	= nintendo3ds_smp_boot_secondary,
};
CPU_METHOD_OF_DECLARE(nintendo3ds_smp, "nintendo3ds,smp", &nintendo3ds_smp_ops);

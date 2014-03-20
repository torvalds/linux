/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/mach-types.h>
#include <asm/smp_plat.h>

#include "scm-boot.h"
#include "common.h"

#define VDD_SC1_ARRAY_CLAMP_GFS_CTL 0x15A0
#define SCSS_CPU1CORE_RESET 0xD80
#define SCSS_DBG_STATUS_CORE_PWRDUP 0xE64

extern void msm_secondary_startup(void);

static DEFINE_SPINLOCK(boot_lock);

static inline int get_core_count(void)
{
	/* 1 + the PART[1:0] field of MIDR */
	return ((read_cpuid_id() >> 4) & 3) + 1;
}

static void msm_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	pen_release = -1;
	smp_wmb();

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static void prepare_cold_cpu(unsigned int cpu)
{
	int ret;
	ret = scm_set_boot_addr(virt_to_phys(msm_secondary_startup),
				SCM_FLAG_COLDBOOT_CPU1);
	if (ret == 0) {
		void __iomem *sc1_base_ptr;
		sc1_base_ptr = ioremap_nocache(0x00902000, SZ_4K*2);
		if (sc1_base_ptr) {
			writel(0, sc1_base_ptr + VDD_SC1_ARRAY_CLAMP_GFS_CTL);
			writel(0, sc1_base_ptr + SCSS_CPU1CORE_RESET);
			writel(3, sc1_base_ptr + SCSS_DBG_STATUS_CORE_PWRDUP);
			iounmap(sc1_base_ptr);
		}
	} else
		printk(KERN_DEBUG "Failed to set secondary core boot "
				  "address\n");
}

static int msm_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	static int cold_boot_done;

	/* Only need to bring cpu out of reset this way once */
	if (cold_boot_done == false) {
		prepare_cold_cpu(cpu);
		cold_boot_done = true;
	}

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	pen_release = cpu_logical_map(cpu);
	sync_cache_w(&pen_release);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;

		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system. The msm8x60
 * does not support the ARM SCU, so just set the possible cpu mask to
 * NR_CPUS.
 */
static void __init msm_smp_init_cpus(void)
{
	unsigned int i, ncores = get_core_count();

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

static void __init msm_smp_prepare_cpus(unsigned int max_cpus)
{
}

struct smp_operations msm_smp_ops __initdata = {
	.smp_init_cpus		= msm_smp_init_cpus,
	.smp_prepare_cpus	= msm_smp_prepare_cpus,
	.smp_secondary_init	= msm_secondary_init,
	.smp_boot_secondary	= msm_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= msm_cpu_die,
#endif
};

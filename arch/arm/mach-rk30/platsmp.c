/*
 * RK30 SMP source file. It contains platform specific fucntions
 * needed for the linux smp kernel.
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 * All Rights Reserved
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/version.h>

#include <asm/cacheflush.h>
#include <asm/hardware/gic.h>
#include <asm/smp_scu.h>

#include <mach/pmu.h>

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);
}

extern void rk30_sram_secondary_startup(void);

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	static bool copied;

	if (!copied) {
		unsigned long sz = 0x100;

		memcpy(RK30_SCU_BASE + sz - 4, (void *)rk30_sram_secondary_startup + sz - 4, 4);
		memcpy(RK30_IMEM_BASE, rk30_sram_secondary_startup, sz);
		flush_icache_range((unsigned long)RK30_IMEM_BASE, (unsigned long)RK30_IMEM_BASE + sz);
		copied = true;
	}

	dsb_sev();
	pmu_set_power_domain(PD_A9_1, true);

	return 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	unsigned int i, ncores = scu_get_core_count(RK30_SCU_BASE);

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0))
	int i;

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);
#endif

	scu_enable(RK30_SCU_BASE);
}

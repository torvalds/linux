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
#include <asm/fiq_glue.h>
#include <asm/hardware/gic.h>
#include <asm/smp_scu.h>

#include <mach/pmu.h>

#define SCU_CTRL             0x00
#define   SCU_STANDBY_EN     (1 << 5)

#ifdef CONFIG_FIQ
static void gic_raise_softirq_non_secure(const struct cpumask *mask, unsigned int irq)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0))
	unsigned long map = *cpus_addr(*mask);
#else
	int cpu;
	unsigned long map = 0;

	/* Convert our logical CPU mask into a physical one. */
	for_each_cpu(cpu, mask)
		map |= 1 << cpu_logical_map(cpu);
#endif

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb();

	/* this always happens on GIC0 */
	writel_relaxed(map << 16 | irq | 0x8000, RK30_GICD_BASE + GIC_DIST_SOFTINT);
}

static void gic_secondary_init_non_secure(void)
{
#define GIC_DIST_SECURITY	0x080
	writel_relaxed(0xffffffff, RK30_GICD_BASE + GIC_DIST_SECURITY);
	writel_relaxed(0xf, RK30_GICC_BASE + GIC_CPU_CTRL);
	dsb();
}
#endif

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init(0);

#ifdef CONFIG_FIQ
	gic_secondary_init_non_secure();
	fiq_glue_resume();
#endif
}

extern void rk30_sram_secondary_startup(void);

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	static bool first = true;

	if (first) {
		unsigned long sz = 0x10;

		pmu_set_power_domain(PD_A9_1, false);

		memcpy(RK30_IMEM_BASE, rk30_sram_secondary_startup, sz);
		flush_icache_range((unsigned long)RK30_IMEM_BASE, (unsigned long)RK30_IMEM_BASE + sz);
		outer_clean_range(0, sz);

		first = false;
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

#ifdef CONFIG_FIQ
	set_smp_cross_call(gic_raise_softirq_non_secure);
#else
	set_smp_cross_call(gic_raise_softirq);
#endif
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

	writel_relaxed(readl_relaxed(RK30_SCU_BASE + SCU_CTRL) | SCU_STANDBY_EN, RK30_SCU_BASE + SCU_CTRL);

	scu_enable(RK30_SCU_BASE);
}

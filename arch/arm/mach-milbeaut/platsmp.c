// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright:	(C) 2018 Socionext Inc.
 * Copyright:	(C) 2015 Linaro Ltd.
 */

#include <linux/cpu_pm.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of_address.h>
#include <linux/suspend.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/idmap.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

#define M10V_MAX_CPU	4
#define KERNEL_UNBOOT_FLAG	0x12345678

static void __iomem *m10v_smp_base;

static int m10v_boot_secondary(unsigned int l_cpu, struct task_struct *idle)
{
	unsigned int mpidr, cpu, cluster;

	if (!m10v_smp_base)
		return -ENXIO;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	if (cpu >= M10V_MAX_CPU)
		return -EINVAL;

	pr_info("%s: cpu %u l_cpu %u cluster %u\n",
			__func__, cpu, l_cpu, cluster);

	writel(__pa_symbol(secondary_startup), m10v_smp_base + cpu * 4);
	arch_send_wakeup_ipi_mask(cpumask_of(l_cpu));

	return 0;
}

static void m10v_smp_init(unsigned int max_cpus)
{
	unsigned int mpidr, cpu, cluster;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "socionext,milbeaut-smp-sram");
	if (!np)
		return;

	m10v_smp_base = of_iomap(np, 0);
	if (!m10v_smp_base)
		return;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pr_info("MCPM boot on cpu_%u cluster_%u\n", cpu, cluster);

	for (cpu = 0; cpu < M10V_MAX_CPU; cpu++)
		writel(KERNEL_UNBOOT_FLAG, m10v_smp_base + cpu * 4);
}

static void m10v_cpu_die(unsigned int l_cpu)
{
	gic_cpu_if_down(0);
	v7_exit_coherency_flush(louis);
	wfi();
}

static int m10v_cpu_kill(unsigned int l_cpu)
{
	unsigned int mpidr, cpu;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);

	writel(KERNEL_UNBOOT_FLAG, m10v_smp_base + cpu * 4);

	return 1;
}

static struct smp_operations m10v_smp_ops __initdata = {
	.smp_prepare_cpus	= m10v_smp_init,
	.smp_boot_secondary	= m10v_boot_secondary,
	.cpu_die		= m10v_cpu_die,
	.cpu_kill		= m10v_cpu_kill,
};
CPU_METHOD_OF_DECLARE(m10v_smp, "socionext,milbeaut-m10v-smp", &m10v_smp_ops);

static int m10v_pm_valid(suspend_state_t state)
{
	return (state == PM_SUSPEND_STANDBY) || (state == PM_SUSPEND_MEM);
}

typedef void (*phys_reset_t)(unsigned long);
static phys_reset_t phys_reset;

static int m10v_die(unsigned long arg)
{
	setup_mm_for_reboot();
	asm("wfi");
	/* Boot just like a secondary */
	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(cpu_resume));

	return 0;
}

static int m10v_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		asm("wfi");
		break;
	case PM_SUSPEND_MEM:
		cpu_pm_enter();
		cpu_suspend(0, m10v_die);
		cpu_pm_exit();
		break;
	}
	return 0;
}

static const struct platform_suspend_ops m10v_pm_ops = {
	.valid		= m10v_pm_valid,
	.enter		= m10v_pm_enter,
};

struct clk *m10v_clclk_register(struct device *cpu_dev);

static int __init m10v_pm_init(void)
{
	if (of_machine_is_compatible("socionext,milbeaut-evb"))
		suspend_set_ops(&m10v_pm_ops);

	return 0;
}
late_initcall(m10v_pm_init);

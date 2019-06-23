// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/fncpy.h>
#include <asm/proc-fns.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>

#include "core.h"

#define AON_SYS_CTRL_RESERVED1		0xa8

#define BUS_MATRIX_REMAP_CONFIG		0x00

#define PCU_CPU0_CTRL			0x00
#define PCU_CPU1_CTRL			0x04
#define PCU_CPU1_ST			0x0c
#define PCU_GLOBAL_CTRL			0x14
#define PCU_EXPEND_CONTROL		0x34

#define ZX_IRAM_BASE			0x00200000

static void __iomem *pcu_base;
static void __iomem *matrix_base;
static void __iomem *scu_base;

void __init zx_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	unsigned long base = 0;
	void __iomem *aonsysctrl_base;
	void __iomem *sys_iram;

	base = scu_a9_get_base();
	scu_base = ioremap(base, SZ_256);
	if (!scu_base) {
		pr_err("%s: failed to map scu\n", __func__);
		return;
	}

	scu_enable(scu_base);

	np = of_find_compatible_node(NULL, NULL, "zte,sysctrl");
	if (!np) {
		pr_err("%s: failed to find sysctrl node\n", __func__);
		return;
	}

	aonsysctrl_base = of_iomap(np, 0);
	if (!aonsysctrl_base) {
		pr_err("%s: failed to map aonsysctrl\n", __func__);
		of_node_put(np);
		return;
	}

	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The BootMonitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 */
	__raw_writel(__pa_symbol(zx_secondary_startup),
		     aonsysctrl_base + AON_SYS_CTRL_RESERVED1);

	iounmap(aonsysctrl_base);
	of_node_put(np);

	np = of_find_compatible_node(NULL, NULL, "zte,zx296702-pcu");
	pcu_base = of_iomap(np, 0);
	of_node_put(np);
	WARN_ON(!pcu_base);

	np = of_find_compatible_node(NULL, NULL, "zte,zx-bus-matrix");
	matrix_base = of_iomap(np, 0);
	of_node_put(np);
	WARN_ON(!matrix_base);

	/* Map the first 4 KB IRAM for suspend usage */
	sys_iram = __arm_ioremap_exec(ZX_IRAM_BASE, PAGE_SIZE, false);
	zx_secondary_startup_pa = __pa_symbol(zx_secondary_startup);
	fncpy(sys_iram, &zx_resume_jump, zx_suspend_iram_sz);
}

static int zx_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	static bool first_boot = true;

	if (first_boot) {
		arch_send_wakeup_ipi_mask(cpumask_of(cpu));
		first_boot = false;
		return 0;
	}

	/* Swap the base address mapping between IRAM and IROM */
	writel_relaxed(0x1, matrix_base + BUS_MATRIX_REMAP_CONFIG);

	/* Power on CPU1 */
	writel_relaxed(0x0, pcu_base + PCU_CPU1_CTRL);

	/* Wait for power on ack */
	while (readl_relaxed(pcu_base + PCU_CPU1_ST) & 0x4)
		cpu_relax();

	/* Swap back the mapping of IRAM and IROM */
	writel_relaxed(0x0, matrix_base + BUS_MATRIX_REMAP_CONFIG);

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static inline void cpu_enter_lowpower(void)
{
	unsigned int v;

	asm volatile(
		"mcr	p15, 0, %1, c7, c5, 0\n"
	"	mcr	p15, 0, %1, c7, c10, 4\n"
	/*
	 * Turn off coherency
	 */
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, %3\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, %2\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0), "Ir" (CR_C), "Ir" (0x40)
	  : "cc");
}

static int zx_cpu_kill(unsigned int cpu)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(2000);

	writel_relaxed(0x2, pcu_base + PCU_CPU1_CTRL);

	while ((readl_relaxed(pcu_base + PCU_CPU1_ST) & 0x3) != 0x0) {
		if (time_after(jiffies, timeout)) {
			pr_err("*** cpu1 poweroff timeout\n");
			break;
		}
	}
	return 1;
}

static void zx_cpu_die(unsigned int cpu)
{
	scu_power_mode(scu_base, SCU_PM_POWEROFF);
	cpu_enter_lowpower();

	while (1)
		cpu_do_idle();
}
#endif

static void zx_secondary_init(unsigned int cpu)
{
	scu_power_mode(scu_base, SCU_PM_NORMAL);
}

static const struct smp_operations zx_smp_ops __initconst = {
	.smp_prepare_cpus	= zx_smp_prepare_cpus,
	.smp_secondary_init	= zx_secondary_init,
	.smp_boot_secondary	= zx_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= zx_cpu_kill,
	.cpu_die		= zx_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(zx_smp, "zte,zx296702-smp", &zx_smp_ops);

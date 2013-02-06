/* linux/arch/arm/mach-exynos/platsmp.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Cloned from linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
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
#include <asm/hardware/gic.h>
#include <asm/smp_scu.h>
#include <asm/unified.h>

#include <mach/hardware.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/smc.h>

#include <plat/cpu.h>
#include <plat/exynos4.h>
#ifdef CONFIG_SEC_WATCHDOG_RESET
#include <plat/regs-watchdog.h>
#endif

extern void exynos_secondary_startup(void);
extern unsigned int gic_bank_offset;

struct _cpu_boot_info {
	void __iomem *power_base;
	void __iomem *boot_base;
};

struct _cpu_boot_info cpu_boot_info[NR_CPUS];

/*
 * control for which core is the next to come out of the secondary
 * boot "holding pen"
 */
volatile int pen_release = -1;

/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

static void __iomem *scu_base_addr(void)
{
	if (soc_is_exynos5210() || soc_is_exynos5250())
		return 0;

	return (void __iomem *)(S5P_VA_SCU);
}

static DEFINE_SPINLOCK(boot_lock);

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	void __iomem *dist_base = S5P_VA_GIC_DIST +
				 (gic_bank_offset * cpu);
	void __iomem *cpu_base = S5P_VA_GIC_CPU +
				(gic_bank_offset * cpu);

	/* Enable the full line of zero */
	if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
		enable_cache_foz();

	/*
	 * if any interrupts are already enabled for the primary
	 * core (e.g. timer irq), then they will not have been enabled
	 * for us: do so
	 */
	gic_secondary_init_base(0, dist_base, cpu_base);

	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int exynos_power_up_cpu(unsigned int cpu)
{
	unsigned int timeout;
	unsigned int val;
	void __iomem *power_base = cpu_boot_info[cpu].power_base;

	val = __raw_readl(power_base);
	if (!(val & S5P_CORE_LOCAL_PWR_EN)) {
		__raw_writel(S5P_CORE_LOCAL_PWR_EN, power_base);

		/* wait max 10 ms until cpu is on */
		timeout = 10;
		while (timeout) {
			val = __raw_readl(power_base + 0x4);

			if ((val & S5P_CORE_LOCAL_PWR_EN) == S5P_CORE_LOCAL_PWR_EN)
				break;

			mdelay(1);
			timeout--;
		}

		if (timeout == 0) {
			printk(KERN_ERR "cpu%d power up failed", cpu);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	int ret;
#ifdef CONFIG_SEC_WATCHDOG_RESET
	unsigned int tmp_wtcon;
#endif

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

#ifdef CONFIG_SEC_WATCHDOG_RESET
	tmp_wtcon = __raw_readl(S3C2410_WTCON);
#endif

	ret = exynos_power_up_cpu(cpu);
	if (ret) {
		spin_unlock(&boot_lock);
		return ret;
	}

	/*
	 * The secondary processor is waiting to be released from
	 * the holding pen - release it, then wait for it to flag
	 * that it has been released by resetting pen_release.
	 *
	 * Note that "pen_release" is the hardware CPU ID, whereas
	 * "cpu" is Linux's internal ID.
	 */
	write_pen_release(cpu);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();

		__raw_writel(BSYM(virt_to_phys(exynos_secondary_startup)),
			cpu_boot_info[cpu].boot_base);

#ifdef CONFIG_ARM_TRUSTZONE
		if (soc_is_exynos4412())
			exynos_smc(SMC_CMD_CPU1BOOT, cpu, 0, 0);
		else
			exynos_smc(SMC_CMD_CPU1BOOT, 0, 0, 0);
#endif
		smp_send_reschedule(cpu);

		if (pen_release == -1)
			break;

		udelay(10);
	}

#ifdef CONFIG_SEC_WATCHDOG_RESET
	__raw_writel(tmp_wtcon, S3C2410_WTCON);
#endif

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

static inline unsigned long exynos5_get_core_count(void)
{
	u32 val;

	/* Read L2 control register */
	asm volatile("mrc p15, 1, %0, c9, c0, 2" : "=r"(val));

	/* core count : [25:24] of L2 control register + 1 */
	val = ((val >> 24) & 3) + 1;

	return val;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	unsigned int i, ncores;

	void __iomem *scu_base = scu_base_addr();

	ncores = scu_base ? scu_get_core_count(scu_base) :
			    exynos5_get_core_count();

	/* sanity check */
	if (ncores > NR_CPUS) {
		printk(KERN_WARNING
		       "EXYNOS: no. of cores (%d) greater than configured "
		       "maximum of %d - clipping\n",
		       ncores, NR_CPUS);
		ncores = NR_CPUS;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(gic_raise_softirq);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	if (scu_base_addr())
		scu_enable(scu_base_addr());
	else
		flush_cache_all();

	/* Set up secondary boot base and core power cofiguration base address */
	for (i = 1; i < max_cpus; i++) {
#ifdef CONFIG_ARM_TRUSTZONE
		cpu_boot_info[i].boot_base = S5P_VA_SYSRAM_NS + 0x1C;
#else
		if (soc_is_exynos4210() && (samsung_rev() >= EXYNOS4210_REV_1_1))
			cpu_boot_info[i].boot_base = S5P_INFORM5;
		else
			cpu_boot_info[i].boot_base = S5P_VA_SYSRAM;
#endif
		if (soc_is_exynos4412())
			cpu_boot_info[i].boot_base += (0x4 * i);
		cpu_boot_info[i].power_base = S5P_ARM_CORE_CONFIGURATION(i);
	}
}

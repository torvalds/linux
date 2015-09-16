/*
 * Copyright (C) 2012 Samsung Electronics.
 * Kyungmin Park <kyungmin.park@samsung.com>
 * Tomasz Figa <t.figa@samsung.com>
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/firmware.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/suspend.h>

#include <mach/map.h>

#include "common.h"
#include "smc.h"

#define EXYNOS_BOOT_ADDR	0x8
#define EXYNOS_BOOT_FLAG	0xc

static void exynos_save_cp15(void)
{
	/* Save Power control and Diagnostic registers */
	asm ("mrc p15, 0, %0, c15, c0, 0\n"
	     "mrc p15, 0, %1, c15, c0, 1\n"
	     : "=r" (cp15_save_power), "=r" (cp15_save_diag)
	     : : "cc");
}

static int exynos_do_idle(unsigned long mode)
{
	switch (mode) {
	case FW_DO_IDLE_AFTR:
		if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
			exynos_save_cp15();
		__raw_writel(virt_to_phys(exynos_cpu_resume_ns),
			     sysram_ns_base_addr + 0x24);
		__raw_writel(EXYNOS_AFTR_MAGIC, sysram_ns_base_addr + 0x20);
		if (soc_is_exynos3250()) {
			flush_cache_all();
			exynos_smc(SMC_CMD_SAVE, OP_TYPE_CORE,
				   SMC_POWERSTATE_IDLE, 0);
			exynos_smc(SMC_CMD_SHUTDOWN, OP_TYPE_CLUSTER,
				   SMC_POWERSTATE_IDLE, 0);
		} else
			exynos_smc(SMC_CMD_CPU0AFTR, 0, 0, 0);
		break;
	case FW_DO_IDLE_SLEEP:
		exynos_smc(SMC_CMD_SLEEP, 0, 0, 0);
	}
	return 0;
}

static int exynos_cpu_boot(int cpu)
{
	/*
	 * Exynos3250 doesn't need to send smc command for secondary CPU boot
	 * because Exynos3250 removes WFE in secure mode.
	 */
	if (soc_is_exynos3250())
		return 0;

	/*
	 * The second parameter of SMC_CMD_CPU1BOOT command means CPU id.
	 * But, Exynos4212 has only one secondary CPU so second parameter
	 * isn't used for informing secure firmware about CPU id.
	 */
	if (soc_is_exynos4212())
		cpu = 0;

	exynos_smc(SMC_CMD_CPU1BOOT, cpu, 0, 0);
	return 0;
}

static int exynos_set_cpu_boot_addr(int cpu, unsigned long boot_addr)
{
	void __iomem *boot_reg;

	if (!sysram_ns_base_addr)
		return -ENODEV;

	boot_reg = sysram_ns_base_addr + 0x1c;

	/*
	 * Almost all Exynos-series of SoCs that run in secure mode don't need
	 * additional offset for every CPU, with Exynos4412 being the only
	 * exception.
	 */
	if (soc_is_exynos4412())
		boot_reg += 4 * cpu;

	__raw_writel(boot_addr, boot_reg);
	return 0;
}

static int exynos_get_cpu_boot_addr(int cpu, unsigned long *boot_addr)
{
	void __iomem *boot_reg;

	if (!sysram_ns_base_addr)
		return -ENODEV;

	boot_reg = sysram_ns_base_addr + 0x1c;

	if (soc_is_exynos4412())
		boot_reg += 4 * cpu;

	*boot_addr = __raw_readl(boot_reg);
	return 0;
}

static int exynos_cpu_suspend(unsigned long arg)
{
	flush_cache_all();
	outer_flush_all();

	exynos_smc(SMC_CMD_SLEEP, 0, 0, 0);

	pr_info("Failed to suspend the system\n");
	writel(0, sysram_ns_base_addr + EXYNOS_BOOT_FLAG);
	return 1;
}

static int exynos_suspend(void)
{
	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
		exynos_save_cp15();

	writel(EXYNOS_SLEEP_MAGIC, sysram_ns_base_addr + EXYNOS_BOOT_FLAG);
	writel(virt_to_phys(exynos_cpu_resume_ns),
		sysram_ns_base_addr + EXYNOS_BOOT_ADDR);

	return cpu_suspend(0, exynos_cpu_suspend);
}

static int exynos_resume(void)
{
	writel(0, sysram_ns_base_addr + EXYNOS_BOOT_FLAG);

	return 0;
}

static const struct firmware_ops exynos_firmware_ops = {
	.do_idle		= IS_ENABLED(CONFIG_EXYNOS_CPU_SUSPEND) ? exynos_do_idle : NULL,
	.set_cpu_boot_addr	= exynos_set_cpu_boot_addr,
	.get_cpu_boot_addr	= exynos_get_cpu_boot_addr,
	.cpu_boot		= exynos_cpu_boot,
	.suspend		= IS_ENABLED(CONFIG_PM_SLEEP) ? exynos_suspend : NULL,
	.resume			= IS_ENABLED(CONFIG_EXYNOS_CPU_SUSPEND) ? exynos_resume : NULL,
};

static void exynos_l2_write_sec(unsigned long val, unsigned reg)
{
	static int l2cache_enabled;

	switch (reg) {
	case L2X0_CTRL:
		if (val & L2X0_CTRL_EN) {
			/*
			 * Before the cache can be enabled, due to firmware
			 * design, SMC_CMD_L2X0INVALL must be called.
			 */
			if (!l2cache_enabled) {
				exynos_smc(SMC_CMD_L2X0INVALL, 0, 0, 0);
				l2cache_enabled = 1;
			}
		} else {
			l2cache_enabled = 0;
		}
		exynos_smc(SMC_CMD_L2X0CTRL, val, 0, 0);
		break;

	case L2X0_DEBUG_CTRL:
		exynos_smc(SMC_CMD_L2X0DEBUG, val, 0, 0);
		break;

	default:
		WARN_ONCE(1, "%s: ignoring write to reg 0x%x\n", __func__, reg);
	}
}

static void exynos_l2_configure(const struct l2x0_regs *regs)
{
	exynos_smc(SMC_CMD_L2X0SETUP1, regs->tag_latency, regs->data_latency,
		   regs->prefetch_ctrl);
	exynos_smc(SMC_CMD_L2X0SETUP2, regs->pwr_ctrl, regs->aux_ctrl, 0);
}

void __init exynos_firmware_init(void)
{
	struct device_node *nd;
	const __be32 *addr;

	nd = of_find_compatible_node(NULL, NULL,
					"samsung,secure-firmware");
	if (!nd)
		return;

	addr = of_get_address(nd, 0, NULL, NULL);
	if (!addr) {
		pr_err("%s: No address specified.\n", __func__);
		return;
	}

	pr_info("Running under secure firmware.\n");

	register_firmware_ops(&exynos_firmware_ops);

	/*
	 * Exynos 4 SoCs (based on Cortex A9 and equipped with L2C-310),
	 * running under secure firmware, require certain registers of L2
	 * cache controller to be written in secure mode. Here .write_sec
	 * callback is provided to perform necessary SMC calls.
	 */
	if (IS_ENABLED(CONFIG_CACHE_L2X0) &&
	    read_cpuid_part() == ARM_CPU_PART_CORTEX_A9) {
		outer_cache.write_sec = exynos_l2_write_sec;
		outer_cache.configure = exynos_l2_configure;
	}
}

#define REG_CPU_STATE_ADDR	(sysram_ns_base_addr + 0x28)
#define BOOT_MODE_MASK		0x1f

void exynos_set_boot_flag(unsigned int cpu, unsigned int mode)
{
	unsigned int tmp;

	tmp = __raw_readl(REG_CPU_STATE_ADDR + cpu * 4);

	if (mode & BOOT_MODE_MASK)
		tmp &= ~BOOT_MODE_MASK;

	tmp |= mode;
	__raw_writel(tmp, REG_CPU_STATE_ADDR + cpu * 4);
}

void exynos_clear_boot_flag(unsigned int cpu, unsigned int mode)
{
	unsigned int tmp;

	tmp = __raw_readl(REG_CPU_STATE_ADDR + cpu * 4);
	tmp &= ~mode;
	__raw_writel(tmp, REG_CPU_STATE_ADDR + cpu * 4);
}

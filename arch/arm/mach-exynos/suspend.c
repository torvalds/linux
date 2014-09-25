/*
 * Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - Suspend support
 *
 * Based on arch/arm/mach-s3c2410/pm.c
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/err.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/firmware.h>
#include <asm/smp_scu.h>
#include <asm/suspend.h>

#include <plat/pm-common.h>
#include <plat/regs-srom.h>

#include "common.h"
#include "regs-pmu.h"
#include "regs-sys.h"

#define S5P_CHECK_SLEEP 0x00000BAD

#define REG_TABLE_END (-1U)

/**
 * struct exynos_wkup_irq - Exynos GIC to PMU IRQ mapping
 * @hwirq: Hardware IRQ signal of the GIC
 * @mask: Mask in PMU wake-up mask register
 */
struct exynos_wkup_irq {
	unsigned int hwirq;
	u32 mask;
};

static struct sleep_save exynos5_sys_save[] = {
	SAVE_ITEM(EXYNOS5_SYS_I2C_CFG),
};

static struct sleep_save exynos_core_save[] = {
	/* SROM side */
	SAVE_ITEM(S5P_SROM_BW),
	SAVE_ITEM(S5P_SROM_BC0),
	SAVE_ITEM(S5P_SROM_BC1),
	SAVE_ITEM(S5P_SROM_BC2),
	SAVE_ITEM(S5P_SROM_BC3),
};

struct exynos_pm_data {
	const struct exynos_wkup_irq *wkup_irq;
	struct sleep_save *extra_save;
	int num_extra_save;
	unsigned int wake_disable_mask;
	unsigned int *release_ret_regs;

	void (*pm_prepare)(void);
	void (*pm_resume)(void);
	int (*pm_suspend)(void);
	int (*cpu_suspend)(unsigned long);
};

struct exynos_pm_data *pm_data;

/*
 * GIC wake-up support
 */

static u32 exynos_irqwake_intmask = 0xffffffff;

static const struct exynos_wkup_irq exynos4_wkup_irq[] = {
	{ 76, BIT(1) }, /* RTC alarm */
	{ 77, BIT(2) }, /* RTC tick */
	{ /* sentinel */ },
};

static const struct exynos_wkup_irq exynos5250_wkup_irq[] = {
	{ 75, BIT(1) }, /* RTC alarm */
	{ 76, BIT(2) }, /* RTC tick */
	{ /* sentinel */ },
};

unsigned int exynos_release_ret_regs[] = {
	S5P_PAD_RET_MAUDIO_OPTION,
	S5P_PAD_RET_GPIO_OPTION,
	S5P_PAD_RET_UART_OPTION,
	S5P_PAD_RET_MMCA_OPTION,
	S5P_PAD_RET_MMCB_OPTION,
	S5P_PAD_RET_EBIA_OPTION,
	S5P_PAD_RET_EBIB_OPTION,
	REG_TABLE_END,
};

static int exynos_irq_set_wake(struct irq_data *data, unsigned int state)
{
	const struct exynos_wkup_irq *wkup_irq;

	if (!pm_data->wkup_irq)
		return -ENOENT;
	wkup_irq = pm_data->wkup_irq;

	while (wkup_irq->mask) {
		if (wkup_irq->hwirq == data->hwirq) {
			if (!state)
				exynos_irqwake_intmask |= wkup_irq->mask;
			else
				exynos_irqwake_intmask &= ~wkup_irq->mask;
			return 0;
		}
		++wkup_irq;
	}

	return -ENOENT;
}

static int exynos_cpu_do_idle(void)
{
	/* issue the standby signal into the pm unit. */
	cpu_do_idle();

	pr_info("Failed to suspend the system\n");
	return 1; /* Aborting suspend */
}

static int exynos_cpu_suspend(unsigned long arg)
{
	flush_cache_all();
	outer_flush_all();
	return exynos_cpu_do_idle();
}

static void exynos_pm_set_wakeup_mask(void)
{
	/* Set wake-up mask registers */
	pmu_raw_writel(exynos_get_eint_wake_mask(), S5P_EINT_WAKEUP_MASK);
	pmu_raw_writel(exynos_irqwake_intmask & ~(1 << 31), S5P_WAKEUP_MASK);
}

static void exynos_pm_enter_sleep_mode(void)
{
	/* Set value of power down register for sleep mode */
	exynos_sys_powerdown_conf(SYS_SLEEP);
	pmu_raw_writel(S5P_CHECK_SLEEP, S5P_INFORM1);

	/* ensure at least INFORM0 has the resume address */
	pmu_raw_writel(virt_to_phys(exynos_cpu_resume), S5P_INFORM0);
}

static void exynos_pm_prepare(void)
{
	/* Set wake-up mask registers */
	exynos_pm_set_wakeup_mask();

	s3c_pm_do_save(exynos_core_save, ARRAY_SIZE(exynos_core_save));

	 if (pm_data->extra_save)
		s3c_pm_do_save(pm_data->extra_save,
				pm_data->num_extra_save);

	exynos_pm_enter_sleep_mode();
}

static int exynos_pm_suspend(void)
{
	exynos_pm_central_suspend();

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9)
		exynos_cpu_save_register();

	return 0;
}

static void exynos_pm_release_retention(void)
{
	unsigned int i;

	for (i = 0; (pm_data->release_ret_regs[i] != REG_TABLE_END); i++)
		pmu_raw_writel(EXYNOS_WAKEUP_FROM_LOWPWR,
				pm_data->release_ret_regs[i]);
}

static void exynos_pm_resume(void)
{
	u32 cpuid = read_cpuid_part();

	if (exynos_pm_central_resume())
		goto early_wakeup;

	/* For release retention */
	exynos_pm_release_retention();

	if (pm_data->extra_save)
		s3c_pm_do_restore_core(pm_data->extra_save,
					pm_data->num_extra_save);

	s3c_pm_do_restore_core(exynos_core_save, ARRAY_SIZE(exynos_core_save));

	if (cpuid == ARM_CPU_PART_CORTEX_A9)
		scu_enable(S5P_VA_SCU);

	if (call_firmware_op(resume) == -ENOSYS
	    && cpuid == ARM_CPU_PART_CORTEX_A9)
		exynos_cpu_restore_register();

early_wakeup:

	/* Clear SLEEP mode set in INFORM1 */
	pmu_raw_writel(0x0, S5P_INFORM1);
}

/*
 * Suspend Ops
 */

static int exynos_suspend_enter(suspend_state_t state)
{
	int ret;

	s3c_pm_debug_init();

	S3C_PMDBG("%s: suspending the system...\n", __func__);

	S3C_PMDBG("%s: wakeup masks: %08x,%08x\n", __func__,
			exynos_irqwake_intmask, exynos_get_eint_wake_mask());

	if (exynos_irqwake_intmask == -1U
	    && exynos_get_eint_wake_mask() == -1U) {
		pr_err("%s: No wake-up sources!\n", __func__);
		pr_err("%s: Aborting sleep\n", __func__);
		return -EINVAL;
	}

	s3c_pm_save_uarts();
	if (pm_data->pm_prepare)
		pm_data->pm_prepare();
	flush_cache_all();
	s3c_pm_check_store();

	ret = call_firmware_op(suspend);
	if (ret == -ENOSYS)
		ret = cpu_suspend(0, pm_data->cpu_suspend);
	if (ret)
		return ret;

	s3c_pm_restore_uarts();

	S3C_PMDBG("%s: wakeup stat: %08x\n", __func__,
			pmu_raw_readl(S5P_WAKEUP_STAT));

	s3c_pm_check_restore();

	S3C_PMDBG("%s: resuming the system...\n", __func__);

	return 0;
}

static int exynos_suspend_prepare(void)
{
	s3c_pm_check_prepare();

	return 0;
}

static void exynos_suspend_finish(void)
{
	s3c_pm_check_cleanup();
}

static const struct platform_suspend_ops exynos_suspend_ops = {
	.enter		= exynos_suspend_enter,
	.prepare	= exynos_suspend_prepare,
	.finish		= exynos_suspend_finish,
	.valid		= suspend_valid_only_mem,
};

static const struct exynos_pm_data exynos4_pm_data = {
	.wkup_irq	= exynos4_wkup_irq,
	.wake_disable_mask = ((0xFF << 8) | (0x1F << 1)),
	.release_ret_regs = exynos_release_ret_regs,
	.pm_suspend	= exynos_pm_suspend,
	.pm_resume	= exynos_pm_resume,
	.pm_prepare	= exynos_pm_prepare,
	.cpu_suspend	= exynos_cpu_suspend,
};

static const struct exynos_pm_data exynos5250_pm_data = {
	.wkup_irq	= exynos5250_wkup_irq,
	.wake_disable_mask = ((0xFF << 8) | (0x1F << 1)),
	.release_ret_regs = exynos_release_ret_regs,
	.extra_save	= exynos5_sys_save,
	.num_extra_save	= ARRAY_SIZE(exynos5_sys_save),
	.pm_suspend	= exynos_pm_suspend,
	.pm_resume	= exynos_pm_resume,
	.pm_prepare	= exynos_pm_prepare,
	.cpu_suspend	= exynos_cpu_suspend,
};

static struct of_device_id exynos_pmu_of_device_ids[] = {
	{
		.compatible = "samsung,exynos4210-pmu",
		.data = &exynos4_pm_data,
	}, {
		.compatible = "samsung,exynos4212-pmu",
		.data = &exynos4_pm_data,
	}, {
		.compatible = "samsung,exynos4412-pmu",
		.data = &exynos4_pm_data,
	}, {
		.compatible = "samsung,exynos5250-pmu",
		.data = &exynos5250_pm_data,
	},
	{ /*sentinel*/ },
};

static struct syscore_ops exynos_pm_syscore_ops;

void __init exynos_pm_init(void)
{
	const struct of_device_id *match;
	u32 tmp;

	of_find_matching_node_and_match(NULL, exynos_pmu_of_device_ids, &match);
	if (!match) {
		pr_err("Failed to find PMU node\n");
		return;
	}
	pm_data = (struct exynos_pm_data *) match->data;

	/* Platform-specific GIC callback */
	gic_arch_extn.irq_set_wake = exynos_irq_set_wake;

	/* All wakeup disable */
	tmp = pmu_raw_readl(S5P_WAKEUP_MASK);
	tmp |= pm_data->wake_disable_mask;
	pmu_raw_writel(tmp, S5P_WAKEUP_MASK);

	exynos_pm_syscore_ops.suspend	= pm_data->pm_suspend;
	exynos_pm_syscore_ops.resume	= pm_data->pm_resume;

	register_syscore_ops(&exynos_pm_syscore_ops);
	suspend_set_ops(&exynos_suspend_ops);
}

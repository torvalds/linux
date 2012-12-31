/* linux/arch/arm/mach-exynos/pm-exynos4.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - Power Management support
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
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/cpu.h>
#include <plat/pm.h>

#include <mach/regs-irq.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/pm-core.h>
#include <mach/pmu.h>
#include <mach/smc.h>

#ifdef CONFIG_ARM_TRUSTZONE
#define REG_INFORM0            (S5P_VA_SYSRAM_NS + 0x8)
#define REG_INFORM1            (S5P_VA_SYSRAM_NS + 0xC)
#else
#define REG_INFORM0            (S5P_INFORM0)
#define REG_INFORM1            (S5P_INFORM1)
#endif

static struct sleep_save exynos4_set_clksrc[] = {
	{ .reg = EXYNOS4_CLKSRC_MASK_TOP			, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_CAM			, .val = 0x11111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_TV				, .val = 0x00000111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD0			, .val = 0x00001111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_MAUDIO			, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_FSYS			, .val = 0x01011111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL0			, .val = 0x01111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL1			, .val = 0x01110111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_DMC			, .val = 0x00010000, },
};

static struct sleep_save exynos4210_set_clksrc[] = {
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD1			, .val = 0x00001111, },
};

static struct sleep_save exynos4_core_save[] = {
	/* GIC side */
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x000),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x004),
	SAVE_ITEM(S5P_VA_GIC_CPU + 0x008),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x000),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x004),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x100),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x104),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x108),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x300),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x304),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x308),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x400),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x404),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x408),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x40C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x410),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x414),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x418),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x41C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x420),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x424),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x428),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x42C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x430),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x434),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x438),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x43C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x440),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x444),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x448),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x44C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x450),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x454),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x458),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x45C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x460),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x464),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x468),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x46C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x470),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x474),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x478),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x47C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x480),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x484),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x488),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x48C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x490),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x494),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x498),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x49C),

	SAVE_ITEM(S5P_VA_GIC_DIST + 0x800),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x804),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x808),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x80C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x810),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x814),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x818),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x81C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x820),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x824),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x828),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x82C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x830),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x834),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x838),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x83C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x840),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x844),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x848),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x84C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x850),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x854),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x858),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x85C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x860),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x864),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x868),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x86C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x870),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x874),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x878),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x87C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x880),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x884),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x888),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x88C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x890),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x894),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x898),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0x89C),

	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC00),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC04),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC08),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC0C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC10),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC14),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC18),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC1C),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC20),
	SAVE_ITEM(S5P_VA_GIC_DIST + 0xC24),

	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x000),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x010),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x020),
	SAVE_ITEM(S5P_VA_COMBINER_BASE + 0x030),
};

static struct sleep_save exynos4_l2cc_save[] = {
	SAVE_ITEM(S5P_VA_L2CC + L2X0_TAG_LATENCY_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_DATA_LATENCY_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_PREFETCH_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_POWER_CTRL),
	SAVE_ITEM(S5P_VA_L2CC + L2X0_AUX_CTRL),
};

void exynos4_cpu_suspend(void)
{
	unsigned int tmp;

	if ((!soc_is_exynos4210()) && (exynos4_is_c2c_use())) {
		/* Gating CLK_IEM_APC & Enable CLK_SSS */
		tmp = __raw_readl(EXYNOS4_CLKGATE_IP_DMC);
		tmp &= ~(0x1 << 17);
		tmp |= (0x1 << 4);
		__raw_writel(tmp, EXYNOS4_CLKGATE_IP_DMC);

		/* Set MAX divider for PWI */
		tmp = __raw_readl(EXYNOS4_CLKDIV_DMC1);
		tmp |= (0xF << 8);
		__raw_writel(tmp, EXYNOS4_CLKDIV_DMC1);

		/* Set clock source for PWI */
		tmp = __raw_readl(EXYNOS4_CLKSRC_DMC);
		tmp &= ~EXYNOS4_CLKSRC_DMC_MASK;
		tmp |= ((0x6 << 16)|(0x1 << 12));
		__raw_writel(tmp, EXYNOS4_CLKSRC_DMC);
	}

	outer_flush_all();

#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc(SMC_CMD_SLEEP, 0, 0, 0);
#else
	/* issue the standby signal into the pm unit. */
	cpu_do_idle();
#endif
}

static void exynos4_pm_prepare(void)
{
	/* Set value of power down register for sleep mode */

	exynos4_sys_powerdown_conf(SYS_SLEEP);
	__raw_writel(S5P_CHECK_SLEEP, REG_INFORM1);

	/* ensure at least INFORM0 has the resume address */

	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_INFORM0);

	/* Before enter central sequence mode, clock src register have to set */

	s3c_pm_do_restore_core(exynos4_set_clksrc, ARRAY_SIZE(exynos4_set_clksrc));

	if (soc_is_exynos4210())
		s3c_pm_do_restore_core(exynos4210_set_clksrc, ARRAY_SIZE(exynos4210_set_clksrc));
}

static int exynos4_pm_add(struct sys_device *sysdev)
{
	pm_cpu_prep = exynos4_pm_prepare;
	pm_cpu_sleep = exynos4_cpu_suspend;

	return 0;
}

/* This function copy from linux/arch/arm/kernel/smp_scu.c */

void exynos4_scu_enable(void __iomem *scu_base)
{
	u32 scu_ctrl;

	scu_ctrl = __raw_readl(scu_base);
	/* already enabled? */
	if (scu_ctrl & 1)
		return;

	scu_ctrl |= 1;
	__raw_writel(scu_ctrl, scu_base);

	/*
	 * Ensure that the data accessed by CPU0 before the SCU was
	 * initialised is visible to the other CPUs.
	 */
	flush_cache_all();
}

static struct sysdev_driver exynos4_pm_driver = {
	.add		= exynos4_pm_add,
};

static __init int exynos4_pm_drvinit(void)
{
	unsigned int tmp;

	s3c_pm_init();

	/* All wakeup disable */

	tmp = __raw_readl(S5P_WAKEUP_MASK);
	tmp |= ((0xFF << 8) | (0x1F << 1));
	__raw_writel(tmp, S5P_WAKEUP_MASK);

	return sysdev_driver_register(&exynos4_sysclass, &exynos4_pm_driver);
}
arch_initcall(exynos4_pm_drvinit);

static int exynos4_pm_suspend(void)
{
	unsigned long tmp;

	if (!exynos4_is_c2c_use())
		s3c_pm_do_save(exynos4_core_save, ARRAY_SIZE(exynos4_core_save));

	s3c_pm_do_save(exynos4_l2cc_save, ARRAY_SIZE(exynos4_l2cc_save));

	/* Setting Central Sequence Register for power down mode */

	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~(S5P_CENTRAL_LOWPWR_CFG);
	__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

	/* When enter sleep mode, USE_DELAYED_RESET_ASSERTION have to disable */
	if (!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(0);

	if (!soc_is_exynos4210()) {
		tmp = S5P_USE_STANDBY_WFI0 | S5P_USE_STANDBY_WFE0;
		__raw_writel(tmp, S5P_CENTRAL_SEQ_OPTION);

		if (exynos4_is_c2c_use()) {
			tmp = __raw_readl(S5P_WAKEUP_MASK_COREBLK);
			tmp &= ~(1 << 20);
			__raw_writel(tmp, S5P_WAKEUP_MASK_COREBLK);
			tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION_COREBLK);
			tmp &= ~S5P_CENTRAL_SEQ_COREBLK_CONF;
			__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION_COREBLK);
		}
	}

	return 0;
}

static void exynos4_pm_resume(void)
{
	unsigned long tmp;

	/* If PMU failed while entering sleep mode, WFI will be
	 * ignored by PMU and then exiting cpu_do_idle().
	 * S5P_CENTRAL_LOWPWR_CFG bit will not be set automatically
	 * in this situation.
	 */
	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	if (!(tmp & S5P_CENTRAL_LOWPWR_CFG)) {
		tmp |= S5P_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);
		/* No need to perform below restore code */
		goto early_wakeup;
	}

	/* For release retention */

	__raw_writel((1 << 28), S5P_PAD_RET_MAUDIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_GPIO_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_UART_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_MMCB_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIA_OPTION);
	__raw_writel((1 << 28), S5P_PAD_RET_EBIB_OPTION);

	if (!exynos4_is_c2c_use())
		s3c_pm_do_restore_core(exynos4_core_save, ARRAY_SIZE(exynos4_core_save));
	else {
		if (!soc_is_exynos4210()) {
			/* Gating CLK_SSS */
			tmp = __raw_readl(EXYNOS4_CLKGATE_IP_DMC);
			tmp &= ~(0x1 << 4);
			__raw_writel(tmp, EXYNOS4_CLKGATE_IP_DMC);
		}
	}

	exynos4_scu_enable(S5P_VA_SCU);

#ifdef CONFIG_CACHE_L2X0
#ifdef CONFIG_ARM_TRUSTZONE
	/*
	 * Restore for Outer cache
	 */
	exynos_smc(SMC_CMD_L2X0SETUP1, exynos4_l2cc_save[0].val,
				       exynos4_l2cc_save[1].val,
				       exynos4_l2cc_save[2].val);

	exynos_smc(SMC_CMD_L2X0SETUP2,
			L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN,
			0x7C470001, 0xC200FFFF);

	exynos_smc(SMC_CMD_L2X0INVALL, 0, 0, 0);
	exynos_smc(SMC_CMD_L2X0CTRL, 1, 0, 0);
#else
	s3c_pm_do_restore_core(exynos4_l2cc_save, ARRAY_SIZE(exynos4_l2cc_save));
	outer_inv_all();
	/* enable L2X0*/
	writel_relaxed(1, S5P_VA_L2CC + L2X0_CTRL);
#endif
#endif

early_wakeup:
	if (!soc_is_exynos4210())
		exynos4_reset_assert_ctrl(1);

	/* Clear Check mode */
	__raw_writel(0x0, REG_INFORM1);

	return;
}

static struct syscore_ops exynos4_pm_syscore_ops = {
	.suspend	= exynos4_pm_suspend,
	.resume		= exynos4_pm_resume,
};

static __init int exynos4_pm_syscore_init(void)
{
	register_syscore_ops(&exynos4_pm_syscore_ops);
	return 0;
}
arch_initcall(exynos4_pm_syscore_init);

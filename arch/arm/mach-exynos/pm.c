/* linux/arch/arm/mach-exynos/pm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - Power Management support
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
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/smp_scu.h>
#include <asm/cputype.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/pll.h>
#include <plat/regs-srom.h>
#include <plat/bts.h>

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
#define REG_INFORM0            (EXYNOS_INFORM0)
#define REG_INFORM1            (EXYNOS_INFORM1)
#endif

#define EXYNOS_I2C_CFG		(S3C_VA_SYS + 0x234)

#define EXYNOS_WAKEUP_STAT_EINT		(1 << 0)
#define EXYNOS_WAKEUP_STAT_RTC_ALARM	(1 << 1)

static struct sleep_save exynos4_set_clksrc[] = {
	{ .reg = EXYNOS4_CLKSRC_MASK_TOP		, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_CAM		, .val = 0x11111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_TV			, .val = 0x00000111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_LCD0		, .val = 0x00001111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_MAUDIO		, .val = 0x00000001, },
	{ .reg = EXYNOS4_CLKSRC_MASK_FSYS		, .val = 0x01011111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL0		, .val = 0x01111111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_PERIL1		, .val = 0x01110111, },
	{ .reg = EXYNOS4_CLKSRC_MASK_DMC		, .val = 0x00010000, },
};

static struct sleep_save exynos4210_set_clksrc[] = {
	{ .reg = EXYNOS4210_CLKSRC_MASK_LCD1		, .val = 0x00001111, },
};

static struct sleep_save exynos4_epll_save[] = {
	SAVE_ITEM(EXYNOS4_EPLL_CON0),
	SAVE_ITEM(EXYNOS4_EPLL_CON1),
};

static struct sleep_save exynos4_vpll_save[] = {
	SAVE_ITEM(EXYNOS4_VPLL_CON0),
	SAVE_ITEM(EXYNOS4_VPLL_CON1),
};

static struct sleep_save exynos5_set_clksrc[] = {
	{ .reg = EXYNOS5_CLKSRC_MASK_TOP,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_GSCL,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_DISP1_0,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_MAUDIO,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_FSYS,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_PERIC0,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_PERIC1,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_ISP,		.val = 0xffffffff, },
};

static struct sleep_save exynos5410_set_clksrc[] = {
	{ .reg = EXYNOS5410_CLKSRC_MASK_CPERI,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_DISP0_0,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_DISP0_1,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_DISP1_1,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKSRC_MASK_FSYS,		.val = 0xffffffff, },
	{ .reg = EXYNOS5_CLKGATE_BUS_DISP1,		.val = 0xffffffff, },
};

static struct sleep_save exynos_core_save[] = {
	/* SROM side */
	SAVE_ITEM(S5P_SROM_BW),
	SAVE_ITEM(S5P_SROM_BC0),
	SAVE_ITEM(S5P_SROM_BC1),
	SAVE_ITEM(S5P_SROM_BC2),
	SAVE_ITEM(S5P_SROM_BC3),

	/* I2C CFG */
	SAVE_ITEM(EXYNOS_I2C_CFG),
};

/* For Cortex-A9 Diagnostic and Power control register */
static unsigned int save_arm_register[2];

static void exynos_clkgate_ctrl(bool on)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS5_CLKGATE_IP_GSCL0);
	tmp = on ? (tmp | EXYNOS5410_CLKGATE_GSCALER0_3) :
			(tmp & ~EXYNOS5410_CLKGATE_GSCALER0_3);

	__raw_writel(tmp, EXYNOS5_CLKGATE_IP_GSCL0);
}

static int exynos_cpu_suspend(unsigned long arg)
{
#ifdef CONFIG_CACHE_L2X0
	outer_flush_all();
#endif
	/* flush cache back to ram */
	flush_cache_all();

	if (soc_is_exynos5410())
		exynos_lpi_mask_ctrl(true);
	else
		exynos_reset_assert_ctrl(false);

#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc(SMC_CMD_SLEEP, 0, 0, 0);
#else
	/* issue the standby signal into the pm unit. */
	cpu_do_idle();
#endif
	pr_info("sleep resumed to originator?");

	if (soc_is_exynos5410()) {
		__raw_writel(EXYNOS5410_USE_STANDBY_WFI_ALL,
			EXYNOS_CENTRAL_SEQ_OPTION);
		exynos_lpi_mask_ctrl(false);
	}
	return 1; /* abort suspend */
}

static void exynos_pm_prepare(void)
{
	unsigned int tmp;

	if (soc_is_exynos5250()) {
		/* Decides whether to use retention capability */
		tmp = __raw_readl(EXYNOS5_ARM_L2_OPTION);
		tmp &= ~EXYNOS5_USE_RETENTION;
		__raw_writel(tmp, EXYNOS5_ARM_L2_OPTION);
	}

	if (!(soc_is_exynos5250() || soc_is_exynos5410())) {
		s3c_pm_do_save(exynos4_epll_save, ARRAY_SIZE(exynos4_epll_save));
		s3c_pm_do_save(exynos4_vpll_save, ARRAY_SIZE(exynos4_vpll_save));
	}

	/* Set value of power down register for sleep mode */
	exynos_sys_powerdown_conf(SYS_SLEEP);
	__raw_writel(EXYNOS_CHECK_SLEEP, REG_INFORM1);

	/* ensure at least INFORM0 has the resume address */
	__raw_writel(virt_to_phys(s3c_cpu_resume), REG_INFORM0);

	/*
	 * Before enter central sequence mode,
	 * clock src register have to set.
	 */
	if (!(soc_is_exynos5250() || soc_is_exynos5410()))
		s3c_pm_do_restore_core(exynos4_set_clksrc,
				ARRAY_SIZE(exynos4_set_clksrc));

	if (soc_is_exynos4210())
		s3c_pm_do_restore_core(exynos4210_set_clksrc, ARRAY_SIZE(exynos4210_set_clksrc));

	if (soc_is_exynos5250() || soc_is_exynos5410())
		s3c_pm_do_restore_core(exynos5_set_clksrc, ARRAY_SIZE(exynos5_set_clksrc));

	if (soc_is_exynos5410()) {
		s3c_pm_do_restore_core(exynos5410_set_clksrc, ARRAY_SIZE(exynos5410_set_clksrc));
		exynos_clkgate_ctrl(true);
	}
}

static int exynos_pm_add(struct device *dev, struct subsys_interface *sif)
{
	pm_cpu_prep = exynos_pm_prepare;
	pm_cpu_sleep = exynos_cpu_suspend;

	return 0;
}

static unsigned long pll_base_rate;

static void exynos4_restore_pll(void)
{
	unsigned long pll_con, locktime, lockcnt;
	unsigned long pll_in_rate;
	unsigned int p_div, epll_wait = 0, vpll_wait = 0;

	if (pll_base_rate == 0)
		return;

	pll_in_rate = pll_base_rate;

	/* EPLL */
	pll_con = exynos4_epll_save[0].val;

	if (pll_con & (1 << 31)) {
		pll_con &= (PLL46XX_PDIV_MASK << PLL46XX_PDIV_SHIFT);
		p_div = (pll_con >> PLL46XX_PDIV_SHIFT);

		pll_in_rate /= 1000000;

		locktime = (3000 / pll_in_rate) * p_div;
		lockcnt = locktime * 10000 / (10000 / pll_in_rate);

		__raw_writel(lockcnt, EXYNOS4_EPLL_LOCK);

		s3c_pm_do_restore_core(exynos4_epll_save,
					ARRAY_SIZE(exynos4_epll_save));
		epll_wait = 1;
	}

	pll_in_rate = pll_base_rate;

	/* VPLL */
	pll_con = exynos4_vpll_save[0].val;

	if (pll_con & (1 << 31)) {
		pll_in_rate /= 1000000;
		/* 750us */
		locktime = 750;
		lockcnt = locktime * 10000 / (10000 / pll_in_rate);

		__raw_writel(lockcnt, EXYNOS4_VPLL_LOCK);

		s3c_pm_do_restore_core(exynos4_vpll_save,
					ARRAY_SIZE(exynos4_vpll_save));
		vpll_wait = 1;
	}

	/* Wait PLL locking */

	do {
		if (epll_wait) {
			pll_con = __raw_readl(EXYNOS4_EPLL_CON0);
			if (pll_con & (1 << EXYNOS4_EPLLCON0_LOCKED_SHIFT))
				epll_wait = 0;
		}

		if (vpll_wait) {
			pll_con = __raw_readl(EXYNOS4_VPLL_CON0);
			if (pll_con & (1 << EXYNOS4_VPLLCON0_LOCKED_SHIFT))
				vpll_wait = 0;
		}
	} while (epll_wait || vpll_wait);
}

void exynos4_scu_enable(void __iomem *scu_base)
{
	u32 scu_ctrl;

#ifdef CONFIG_ARM_ERRATA_764369
	/* Cortex-A9 only */
	if ((read_cpuid(CPUID_ID) & 0xff0ffff0) == 0x410fc090) {
		scu_ctrl = __raw_readl(scu_base + 0x30);
		if (!(scu_ctrl & 1))
			__raw_writel(scu_ctrl | 0x1, scu_base + 0x30);
	}
#endif

	scu_ctrl = __raw_readl(scu_base);
	/* already enabled? */
	if (scu_ctrl & 1)
		return;

	if (soc_is_exynos4412() && (samsung_rev() >= EXYNOS4412_REV_1_0))
		scu_ctrl |= (1<<3);

	scu_ctrl |= 1;
	__raw_writel(scu_ctrl, scu_base);

	/*
	 * Ensure that the data accessed by CPU0 before the SCU was
	 * initialised is visible to the other CPUs.
	 */
	flush_cache_all();
}

static struct subsys_interface exynos4_pm_interface = {
	.name		= "exynos_pm",
	.subsys		= &exynos4_subsys,
	.add_dev	= exynos_pm_add,
};

static struct subsys_interface exynos5_pm_interface = {
	.name		= "exynos_pm",
	.subsys		= &exynos5_subsys,
	.add_dev	= exynos_pm_add,
};

static __init int exynos_pm_drvinit(void)
{
	struct clk *pll_base;

	s3c_pm_init();

	if (!(soc_is_exynos5250() || soc_is_exynos5410())) {
		pll_base = clk_get(NULL, "xtal");

		if (!IS_ERR(pll_base)) {
			pll_base_rate = clk_get_rate(pll_base);
			clk_put(pll_base);
		}
	}
	if (soc_is_exynos5250() || soc_is_exynos5410())
		return subsys_interface_register(&exynos5_pm_interface);
	else
		return subsys_interface_register(&exynos4_pm_interface);
}
arch_initcall(exynos_pm_drvinit);

static void exynos_show_wakeup_reason_eint(void)
{
	int bit;
	int reg_eintstart;
	long unsigned int ext_int_pend;
	unsigned long eint_wakeup_mask;
	bool found = 0;
	extern void __iomem *exynos_eint_base;

	eint_wakeup_mask = __raw_readl(EXYNOS_EINT_WAKEUP_MASK);

	for (reg_eintstart = 0; reg_eintstart <= 31; reg_eintstart += 8) {
		ext_int_pend =
			__raw_readl(EINT_PEND(exynos_eint_base,
					      IRQ_EINT(reg_eintstart)));

		for_each_set_bit(bit, &ext_int_pend, 8) {
			int irq = IRQ_EINT(reg_eintstart) + bit;
			struct irq_desc *desc = irq_to_desc(irq);

			if (eint_wakeup_mask & (1 << (reg_eintstart + bit)))
				continue;

			if (desc && desc->action && desc->action->name)
				pr_info("Resume caused by IRQ %d, %s\n", irq,
					desc->action->name);
			else
				pr_info("Resume caused by IRQ %d\n", irq);

			found = 1;
		}
	}

	if (!found)
		pr_info("Resume caused by unknown EINT\n");
}

static void exynos_show_wakeup_reason(void)
{
	unsigned long wakeup_stat;

	wakeup_stat = __raw_readl(EXYNOS_WAKEUP_STAT);

	if (wakeup_stat & EXYNOS_WAKEUP_STAT_RTC_ALARM)
		pr_info("Resume caused by RTC alarm\n");
	else if (wakeup_stat & EXYNOS_WAKEUP_STAT_EINT)
		exynos_show_wakeup_reason_eint();
	else
		pr_info("Resume caused by wakeup_stat=0x%08lx\n",
			wakeup_stat);
}


static int exynos_pm_suspend(void)
{
	unsigned long tmp;
	unsigned int cluster_id;

	s3c_pm_do_save(exynos_core_save, ARRAY_SIZE(exynos_core_save));

	if (!(soc_is_exynos4210() || soc_is_exynos5410()))
		exynos_reset_assert_ctrl(false);
#ifdef CONFIG_CPU_IDLE
	if (soc_is_exynos5410())
		exynos_disable_idle_clock_down(KFC);
#endif
	if (soc_is_exynos5410()) {
		cluster_id = read_cpuid(CPUID_MPIDR) >> 8 & 0xf;
		if(!cluster_id)
			__raw_writel(0x000F00F0, EXYNOS_CENTRAL_SEQ_OPTION);
		else
			__raw_writel(0x00F00F00, EXYNOS_CENTRAL_SEQ_OPTION);

	} else if (!soc_is_exynos5250()) {
		tmp = (EXYNOS4_USE_STANDBY_WFI0 | EXYNOS4_USE_STANDBY_WFE0);
		__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_OPTION);

		/* Save Power control register */
		asm ("mrc p15, 0, %0, c15, c0, 0"
		     : "=r" (tmp) : : "cc");
		save_arm_register[0] = tmp;

		/* Save Diagnostic register */
		asm ("mrc p15, 0, %0, c15, c0, 1"
		     : "=r" (tmp) : : "cc");
		save_arm_register[1] = tmp;
	}

	/* Setting Central Sequence Register for power down mode */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~EXYNOS_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);

	return 0;
}

static void exynos_pm_resume(void)
{
	unsigned long tmp;
#ifdef CONFIG_ARM_TRUSTZONE
	unsigned long p_reg, d_reg;
#endif
	unsigned int cluster_id = !((read_cpuid_mpidr() >> 8) & 0xf);

	if (soc_is_exynos5410())
		__raw_writel(EXYNOS5410_USE_STANDBY_WFI_ALL,
			EXYNOS_CENTRAL_SEQ_OPTION);
	else
		exynos_reset_assert_ctrl(true);

	/*
	 * If PMU failed while entering sleep mode, WFI will be
	 * ignored by PMU and then exiting cpu_do_idle().
	 * S5P_CENTRAL_LOWPWR_CFG bit will not be set automatically
	 * in this situation.
	 */
	tmp = __raw_readl(EXYNOS_CENTRAL_SEQ_CONFIGURATION);
	if (!(tmp & EXYNOS_CENTRAL_LOWPWR_CFG)) {
		tmp |= EXYNOS_CENTRAL_LOWPWR_CFG;
		__raw_writel(tmp, EXYNOS_CENTRAL_SEQ_CONFIGURATION);
		/* No need to perform below restore code */
		goto early_wakeup;
	}
	if (!(soc_is_exynos5250() || soc_is_exynos5410())) {
#ifdef CONFIG_ARM_TRUSTZONE
		/* Restore Power control register */
		p_reg = save_arm_register[0];
		/* Restore Diagnostic register */
		d_reg = save_arm_register[1];
		exynos_smc(SMC_CMD_C15RESUME, p_reg, d_reg, 0);
#else
		/* Restore Power control register */
		tmp = save_arm_register[0];
		asm volatile ("mcr p15, 0, %0, c15, c0, 0"
			      : : "r" (tmp)
			      : "cc");

		/* Restore Diagnostic register */
		tmp = save_arm_register[1];
		asm volatile ("mcr p15, 0, %0, c15, c0, 1"
			      : : "r" (tmp)
			      : "cc");
#endif
	}

	/* For release retention */
	if (!soc_is_exynos5410()) {
		__raw_writel((1 << 28), EXYNOS_PAD_RET_MAUDIO_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_GPIO_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_UART_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_MMCA_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_MMCB_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIA_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIB_OPTION);
		__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_SPI_OPTION);
		__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_GPIO_SYSMEM_OPTION);
	} else {
		__raw_writel((1 << 28), EXYNOS_PAD_RET_DRAM_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_MAUDIO_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_JTAG_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_GPIO_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_UART_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_MMCA_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_MMCB_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_MMCC_OPTION);
		__raw_writel((1 << 28), EXYNOS5410_PAD_RET_HSI_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIA_OPTION);
		__raw_writel((1 << 28), EXYNOS_PAD_RET_EBIB_OPTION);
		__raw_writel((1 << 28), EXYNOS5_PAD_RETENTION_SPI_OPTION);
	}

	s3c_pm_do_restore_core(exynos_core_save, ARRAY_SIZE(exynos_core_save));

	bts_initialize(NULL, true);

	if (!(soc_is_exynos5250() || soc_is_exynos5410())) {
		exynos4_restore_pll();
#ifdef CONFIG_SMP
	if (soc_is_exynos5250())
		scu_enable(S5P_VA_SCU);
	else
		exynos4_scu_enable(S5P_VA_SCU);
#endif
	}

#ifdef CONFIG_EXYNOS5_CLUSTER_POWER_CONTROL
	if (soc_is_exynos5410() && cluster_id == KFC) {
		__raw_writel(0x3, EXYNOS_COMMON_CONFIGURATION(0));
		/* wait till cluster power control is applied */
		do {
			if ((__raw_readl(EXYNOS_COMMON_STATUS(0)) &
						__raw_readl(EXYNOS_L2_STATUS(0)) & 0x3) == 0x3)
				break;
		} while (1);

		__raw_writel(0x0, EXYNOS_COMMON_CONFIGURATION(0));
		/* wait till cluster power control is applied */
		do {
			if ((__raw_readl(EXYNOS_COMMON_STATUS(0)) &
						__raw_readl(EXYNOS_L2_STATUS(0)) & 0x3) == 0x0)
				break;
		} while (1);
	}
#endif
early_wakeup:
	if (!soc_is_exynos5410()) {
		exynos_reset_assert_ctrl(true);
		__raw_writel(0x0, REG_INFORM1);
		exynos_show_wakeup_reason();
#ifdef CONFIG_CPU_IDLE
	} else {
		exynos_enable_idle_clock_down(ARM);
		exynos_enable_idle_clock_down(KFC);
#endif
	}
	return;
}

static struct syscore_ops exynos_pm_syscore_ops = {
	.suspend	= exynos_pm_suspend,
	.resume		= exynos_pm_resume,
};

static __init int exynos_pm_syscore_init(void)
{
	register_syscore_ops(&exynos_pm_syscore_ops);
	return 0;
}
arch_initcall(exynos_pm_syscore_init);

/* linux/arch/arm/mach-s5p64x0/pm.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 Power Management Support
 *
 * Based on arch/arm/mach-s3c64xx/pm.c by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/regs-timer.h>
#include <plat/wakeup-mask.h>

#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>

static struct sleep_save s5p64x0_core_save[] = {
	SAVE_ITEM(S5P64X0_APLL_CON),
	SAVE_ITEM(S5P64X0_MPLL_CON),
	SAVE_ITEM(S5P64X0_EPLL_CON),
	SAVE_ITEM(S5P64X0_EPLL_CON_K),
	SAVE_ITEM(S5P64X0_CLK_SRC0),
	SAVE_ITEM(S5P64X0_CLK_SRC1),
	SAVE_ITEM(S5P64X0_CLK_DIV0),
	SAVE_ITEM(S5P64X0_CLK_DIV1),
	SAVE_ITEM(S5P64X0_CLK_DIV2),
	SAVE_ITEM(S5P64X0_CLK_DIV3),
	SAVE_ITEM(S5P64X0_CLK_GATE_MEM0),
	SAVE_ITEM(S5P64X0_CLK_GATE_HCLK1),
	SAVE_ITEM(S5P64X0_CLK_GATE_SCLK1),
};

static struct sleep_save s5p64x0_misc_save[] = {
	SAVE_ITEM(S5P64X0_AHB_CON0),
	SAVE_ITEM(S5P64X0_SPCON0),
	SAVE_ITEM(S5P64X0_SPCON1),
	SAVE_ITEM(S5P64X0_MEM0CONSLP0),
	SAVE_ITEM(S5P64X0_MEM0CONSLP1),
	SAVE_ITEM(S5P64X0_MEM0DRVCON),
	SAVE_ITEM(S5P64X0_MEM1DRVCON),

	SAVE_ITEM(S3C64XX_TINT_CSTAT),
};

/* DPLL is present only in S5P6450 */
static struct sleep_save s5p6450_core_save[] = {
	SAVE_ITEM(S5P6450_DPLL_CON),
	SAVE_ITEM(S5P6450_DPLL_CON_K),
};

void s3c_pm_configure_extint(void)
{
	__raw_writel(s3c_irqwake_eintmask, S5P64X0_EINT_WAKEUP_MASK);
}

void s3c_pm_restore_core(void)
{
	__raw_writel(0, S5P64X0_EINT_WAKEUP_MASK);

	s3c_pm_do_restore_core(s5p64x0_core_save,
				ARRAY_SIZE(s5p64x0_core_save));

	if (soc_is_s5p6450())
		s3c_pm_do_restore_core(s5p6450_core_save,
				ARRAY_SIZE(s5p6450_core_save));

	s3c_pm_do_restore(s5p64x0_misc_save, ARRAY_SIZE(s5p64x0_misc_save));
}

void s3c_pm_save_core(void)
{
	s3c_pm_do_save(s5p64x0_misc_save, ARRAY_SIZE(s5p64x0_misc_save));

	if (soc_is_s5p6450())
		s3c_pm_do_save(s5p6450_core_save,
				ARRAY_SIZE(s5p6450_core_save));

	s3c_pm_do_save(s5p64x0_core_save, ARRAY_SIZE(s5p64x0_core_save));
}

static int s5p64x0_cpu_suspend(unsigned long arg)
{
	unsigned long tmp = 0;

	/*
	 * Issue the standby signal into the pm unit. Note, we
	 * issue a write-buffer drain just in case.
	 */
	asm("b 1f\n\t"
	    ".align 5\n\t"
	    "1:\n\t"
	    "mcr p15, 0, %0, c7, c10, 5\n\t"
	    "mcr p15, 0, %0, c7, c10, 4\n\t"
	    "mcr p15, 0, %0, c7, c0, 4" : : "r" (tmp));

	pr_info("Failed to suspend the system\n");
	return 1; /* Aborting suspend */
}

/* mapping of interrupts to parts of the wakeup mask */
static struct samsung_wakeup_mask s5p64x0_wake_irqs[] = {
	{ .irq = IRQ_RTC_ALARM,	.bit = S5P64X0_PWR_CFG_RTC_ALRM_DISABLE, },
	{ .irq = IRQ_RTC_TIC,	.bit = S5P64X0_PWR_CFG_RTC_TICK_DISABLE, },
	{ .irq = IRQ_HSMMC0,	.bit = S5P64X0_PWR_CFG_MMC0_DISABLE, },
	{ .irq = IRQ_HSMMC1,	.bit = S5P64X0_PWR_CFG_MMC1_DISABLE, },
};

static void s5p64x0_pm_prepare(void)
{
	u32 tmp;

	samsung_sync_wakemask(S5P64X0_PWR_CFG,
			s5p64x0_wake_irqs, ARRAY_SIZE(s5p64x0_wake_irqs));

	/* store the resume address in INFORM0 register */
	__raw_writel(virt_to_phys(s3c_cpu_resume), S5P64X0_INFORM0);

	/* setup clock gating for FIMGVG block */
	__raw_writel((__raw_readl(S5P64X0_CLK_GATE_HCLK1) | \
		(S5P64X0_CLK_GATE_HCLK1_FIMGVG)), S5P64X0_CLK_GATE_HCLK1);
	__raw_writel((__raw_readl(S5P64X0_CLK_GATE_SCLK1) | \
		(S5P64X0_CLK_GATE_SCLK1_FIMGVG)), S5P64X0_CLK_GATE_SCLK1);

	/* Configure the stabilization counter with wait time required */
	__raw_writel(S5P64X0_PWR_STABLE_PWR_CNT_VAL4, S5P64X0_PWR_STABLE);

	/* set WFI to SLEEP mode configuration */
	tmp = __raw_readl(S5P64X0_SLEEP_CFG);
	tmp &= ~(S5P64X0_SLEEP_CFG_OSC_EN);
	__raw_writel(tmp, S5P64X0_SLEEP_CFG);

	tmp = __raw_readl(S5P64X0_PWR_CFG);
	tmp &= ~(S5P64X0_PWR_CFG_WFI_MASK);
	tmp |= S5P64X0_PWR_CFG_WFI_SLEEP;
	__raw_writel(tmp, S5P64X0_PWR_CFG);

	/*
	 * set OTHERS register to disable interrupt before going to
	 * sleep. This bit is present only in S5P6450, it is reserved
	 * in S5P6440.
	 */
	if (soc_is_s5p6450()) {
		tmp = __raw_readl(S5P64X0_OTHERS);
		tmp |= S5P6450_OTHERS_DISABLE_INT;
		__raw_writel(tmp, S5P64X0_OTHERS);
	}

	/* ensure previous wakeup state is cleared before sleeping */
	__raw_writel(__raw_readl(S5P64X0_WAKEUP_STAT), S5P64X0_WAKEUP_STAT);

}

static int s5p64x0_pm_add(struct device *dev, struct subsys_interface *sif)
{
	pm_cpu_prep = s5p64x0_pm_prepare;
	pm_cpu_sleep = s5p64x0_cpu_suspend;
	pm_uart_udivslot = 1;

	return 0;
}

static struct subsys_interface s5p64x0_pm_interface = {
	.name		= "s5p64x0_pm",
	.subsys		= &s5p64x0_subsys,
	.add_dev	= s5p64x0_pm_add,
};

static __init int s5p64x0_pm_drvinit(void)
{
	s3c_pm_init();

	return subsys_interface_register(&s5p64x0_pm_interface);
}
arch_initcall(s5p64x0_pm_drvinit);

static void s5p64x0_pm_resume(void)
{
	u32 tmp;

	tmp = __raw_readl(S5P64X0_OTHERS);
	tmp |= (S5P64X0_OTHERS_RET_MMC0 | S5P64X0_OTHERS_RET_MMC1 | \
			S5P64X0_OTHERS_RET_UART);
	__raw_writel(tmp , S5P64X0_OTHERS);
}

static struct syscore_ops s5p64x0_pm_syscore_ops = {
	.resume		= s5p64x0_pm_resume,
};

static __init int s5p64x0_pm_syscore_init(void)
{
	register_syscore_ops(&s5p64x0_pm_syscore_ops);

	return 0;
}
arch_initcall(s5p64x0_pm_syscore_init);

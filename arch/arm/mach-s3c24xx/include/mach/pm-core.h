/* linux/arch/arm/mach-s3c2410/include/pm-core.h
 *
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C24xx - PM core support for arch/arm/plat-s3c/pm.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

static inline void s3c_pm_debug_init_uart(void)
{
	unsigned long tmp = __raw_readl(S3C2410_CLKCON);

	/* re-start uart clocks */
	tmp |= S3C2410_CLKCON_UART0;
	tmp |= S3C2410_CLKCON_UART1;
	tmp |= S3C2410_CLKCON_UART2;

	__raw_writel(tmp, S3C2410_CLKCON);
	udelay(10);
}

static inline void s3c_pm_arch_prepare_irqs(void)
{
	__raw_writel(s3c_irqwake_intmask, S3C2410_INTMSK);
	__raw_writel(s3c_irqwake_eintmask, S3C2410_EINTMASK);

	/* ack any outstanding external interrupts before we go to sleep */

	__raw_writel(__raw_readl(S3C2410_EINTPEND), S3C2410_EINTPEND);
	__raw_writel(__raw_readl(S3C2410_INTPND), S3C2410_INTPND);
	__raw_writel(__raw_readl(S3C2410_SRCPND), S3C2410_SRCPND);

}

static inline void s3c_pm_arch_stop_clocks(void)
{
	__raw_writel(0x00, S3C2410_CLKCON);  /* turn off clocks over sleep */
}

static void s3c_pm_show_resume_irqs(int start, unsigned long which,
				    unsigned long mask);

static inline void s3c_pm_arch_show_resume_irqs(void)
{
	S3C_PMDBG("post sleep: IRQs 0x%08x, 0x%08x\n",
		  __raw_readl(S3C2410_SRCPND),
		  __raw_readl(S3C2410_EINTPEND));

	s3c_pm_show_resume_irqs(IRQ_EINT0, __raw_readl(S3C2410_SRCPND),
				s3c_irqwake_intmask);

	s3c_pm_show_resume_irqs(IRQ_EINT4-4, __raw_readl(S3C2410_EINTPEND),
				s3c_irqwake_eintmask);
}

static inline void s3c_pm_arch_update_uart(void __iomem *regs,
					   struct pm_uart_save *save)
{
}

static inline void s3c_pm_restored_gpios(void) { }
static inline void samsung_pm_saved_gpios(void) { }

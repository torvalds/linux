/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C24xx - PM core support for arch/arm/plat-s3c/pm.c
 */

#include <linux/delay.h>
#include <linux/io.h>

#include "regs-clock.h"
#include "regs-irq-s3c24xx.h"
#include "irqs.h"

static inline void s3c_pm_debug_init_uart(void)
{
#ifdef CONFIG_SAMSUNG_PM_DEBUG
	unsigned long tmp = __raw_readl(S3C2410_CLKCON);

	/* re-start uart clocks */
	tmp |= S3C2410_CLKCON_UART0;
	tmp |= S3C2410_CLKCON_UART1;
	tmp |= S3C2410_CLKCON_UART2;

	__raw_writel(tmp, S3C2410_CLKCON);
	udelay(10);
#endif
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

/* s3c2410_pm_show_resume_irqs
 *
 * print any IRQs asserted at resume time (ie, we woke from)
*/
static inline void s3c_pm_show_resume_irqs(int start, unsigned long which,
					   unsigned long mask)
{
	int i;

	which &= ~mask;

	for (i = 0; i <= 31; i++) {
		if (which & (1L<<i)) {
			S3C_PMDBG("IRQ %d asserted at resume\n", start+i);
		}
	}
}

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

static inline void s3c_pm_restored_gpios(void) { }
static inline void samsung_pm_saved_gpios(void) { }

/* state for IRQs over sleep */

/* default is to allow for EINT0..EINT15, and IRQ_RTC as wakeup sources
 *
 * set bit to 1 in allow bitfield to enable the wakeup settings on it
*/
#ifdef CONFIG_PM_SLEEP
#define s3c_irqwake_intallow	(1L << 30 | 0xfL)
#define s3c_irqwake_eintallow	(0x0000fff0L)
#else
#define s3c_irqwake_eintallow 0
#define s3c_irqwake_intallow  0
#endif

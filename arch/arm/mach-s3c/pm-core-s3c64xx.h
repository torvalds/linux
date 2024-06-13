/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - PM core support for arch/arm/plat-s3c/pm.c
 */

#ifndef __MACH_S3C64XX_PM_CORE_H
#define __MACH_S3C64XX_PM_CORE_H __FILE__

#include <linux/serial_s3c.h>
#include <linux/delay.h>

#include "regs-gpio.h"
#include "regs-clock.h"
#include "map.h"

static inline void s3c_pm_debug_init_uart(void)
{
#ifdef CONFIG_SAMSUNG_PM_DEBUG
	u32 tmp = __raw_readl(S3C_PCLK_GATE);

	/* As a note, since the S3C64XX UARTs generally have multiple
	 * clock sources, we simply enable PCLK at the moment and hope
	 * that the resume settings for the UART are suitable for the
	 * use with PCLK.
	 */

	tmp |= S3C_CLKCON_PCLK_UART0;
	tmp |= S3C_CLKCON_PCLK_UART1;
	tmp |= S3C_CLKCON_PCLK_UART2;
	tmp |= S3C_CLKCON_PCLK_UART3;

	__raw_writel(tmp, S3C_PCLK_GATE);
	udelay(10);
#endif
}

static inline void s3c_pm_arch_prepare_irqs(void)
{
	/* VIC should have already been taken care of */

	/* clear any pending EINT0 interrupts */
	__raw_writel(__raw_readl(S3C64XX_EINT0PEND), S3C64XX_EINT0PEND);
}

static inline void s3c_pm_arch_stop_clocks(void)
{
}

static inline void s3c_pm_arch_show_resume_irqs(void)
{
}

/* make these defines, we currently do not have any need to change
 * the IRQ wake controls depending on the CPU we are running on */
#ifdef CONFIG_PM_SLEEP
#define s3c_irqwake_eintallow	((1 << 28) - 1)
#define s3c_irqwake_intallow	(~0)
#else
#define s3c_irqwake_eintallow 0
#define s3c_irqwake_intallow  0
#endif

static inline void s3c_pm_restored_gpios(void)
{
	/* ensure sleep mode has been cleared from the system */

	__raw_writel(0, S3C64XX_SLPEN);
}

static inline void samsung_pm_saved_gpios(void)
{
	/* turn on the sleep mode and keep it there, as it seems that during
	 * suspend the xCON registers get re-set and thus you can end up with
	 * problems between going to sleep and resuming.
	 */

	__raw_writel(S3C64XX_SLPEN_USE_xSLP, S3C64XX_SLPEN);
}
#endif /* __MACH_S3C64XX_PM_CORE_H */

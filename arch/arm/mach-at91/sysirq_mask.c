/*
 * sysirq_mask.c - System-interrupt masking
 *
 * Copyright (C) 2013 Johan Hovold <jhovold@gmail.com>
 *
 * Functions to disable system interrupts from backup-powered peripherals.
 *
 * The RTC and RTT-peripherals are generally powered by backup power (VDDBU)
 * and are not reset on wake-up, user, watchdog or software reset. This means
 * that their interrupts may be enabled during early boot (e.g. after a user
 * reset).
 *
 * As the RTC and RTT share the system-interrupt line with the PIT, an
 * interrupt occurring before a handler has been installed would lead to the
 * system interrupt being disabled and prevent the system from booting.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <mach/at91_rtt.h>

#include "generic.h"

#define AT91_RTC_IDR		0x24	/* Interrupt Disable Register */
#define AT91_RTC_IMR		0x28	/* Interrupt Mask Register */
#define AT91_RTC_IRQ_MASK	0x1f	/* Available IRQs mask */

void __init at91_sysirq_mask_rtc(u32 rtc_base)
{
	void __iomem *base;

	base = ioremap(rtc_base, 64);
	if (!base)
		return;

	/*
	 * sam9x5 SoCs have the following errata:
	 * "RTC: Interrupt Mask Register cannot be used
	 *  Interrupt Mask Register read always returns 0."
	 *
	 * Hence we're not relying on IMR values to disable
	 * interrupts.
	 */
	writel_relaxed(AT91_RTC_IRQ_MASK, base + AT91_RTC_IDR);
	(void)readl_relaxed(base + AT91_RTC_IMR);	/* flush */

	iounmap(base);
}

void __init at91_sysirq_mask_rtt(u32 rtt_base)
{
	void __iomem *base;
	void __iomem *reg;
	u32 mode;

	base = ioremap(rtt_base, 16);
	if (!base)
		return;

	reg = base + AT91_RTT_MR;

	mode = readl_relaxed(reg);
	if (mode & (AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN)) {
		pr_info("AT91: Disabling rtt irq\n");
		mode &= ~(AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN);
		writel_relaxed(mode, reg);
		(void)readl_relaxed(reg);			/* flush */
	}

	iounmap(base);
}

/*
 * System functions for Telechips TCCxxxx SoCs
 *
 * Copyright (C) Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GPL v2.
 *
 */

#include <linux/io.h>

#include <mach/tcc8k-regs.h>

/* System reboot */
void plat_tcc_reboot(void)
{
	/* Make sure clocks are on */
	__raw_writel(0xffffffff, CKC_BASE + BCLKCTR0_OFFS);

	/* Enable watchdog reset */
	__raw_writel(0x49, TIMER_BASE + TWDCFG_OFFS);
	/* Wait for reset */
	while(1)
		;
}

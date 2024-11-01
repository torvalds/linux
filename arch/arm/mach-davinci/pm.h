/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI DaVinci platform support for power management.
 *
 * Copyright (C) 2009 Texas Instruments, Inc. https://www.ti.com/
 */
#ifndef _MACH_DAVINCI_PM_H
#define _MACH_DAVINCI_PM_H

/*
 * Caution: Assembly code in sleep.S makes assumtion on the order
 * of the members of this structure.
 */
struct davinci_pm_config {
	void __iomem *ddr2_ctlr_base;
	void __iomem *ddrpsc_reg_base;
	int ddrpsc_num;
	void __iomem *ddrpll_reg_base;
	void __iomem *deepsleep_reg;
	void __iomem *cpupll_reg_base;
	/*
	 * Note on SLEEPCOUNT:
	 * The SLEEPCOUNT feature is mainly intended for cases in which
	 * the internal oscillator is used. The internal oscillator is
	 * fully disabled in deep sleep mode.  When you exist deep sleep
	 * mode, the oscillator will be turned on and will generate very
	 * small oscillations which will not be detected by the deep sleep
	 * counter.  Eventually those oscillations will grow to an amplitude
	 * large enough to start incrementing the deep sleep counter.
	 * In this case recommendation from hardware engineers is that the
	 * SLEEPCOUNT be set to 4096.  This means that 4096 valid clock cycles
	 * must be detected before the clock is passed to the rest of the
	 * system.
	 * In the case that the internal oscillator is not used and the
	 * clock is generated externally, the SLEEPCOUNT value can be very
	 * small since the clock input is assumed to be stable before SoC
	 * is taken out of deepsleep mode.  A value of 128 would be more than
	 * adequate.
	 */
	int sleepcount;
};

extern unsigned int davinci_cpu_suspend_sz;
extern void davinci_cpu_suspend(struct davinci_pm_config *);

#endif

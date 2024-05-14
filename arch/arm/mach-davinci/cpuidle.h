/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI DaVinci cpuidle platform support
 *
 * 2009 (C) Texas Instruments, Inc. https://www.ti.com/
 */
#ifndef _MACH_DAVINCI_CPUIDLE_H
#define _MACH_DAVINCI_CPUIDLE_H

struct davinci_cpuidle_config {
	u32 ddr2_pdown;
	void __iomem *ddr2_ctlr_base;
};

#endif

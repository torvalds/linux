/*
 * TI DaVinci cpuidle platform support
 *
 * 2009 (C) Texas Instruments, Inc. https://www.ti.com/
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */
#ifndef _MACH_DAVINCI_CPUIDLE_H
#define _MACH_DAVINCI_CPUIDLE_H

struct davinci_cpuidle_config {
	u32 ddr2_pdown;
	void __iomem *ddr2_ctlr_base;
};

#endif

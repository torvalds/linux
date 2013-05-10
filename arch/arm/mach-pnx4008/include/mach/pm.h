/*
 * arch/arm/mach-pnx4008/include/mach/pm.h
 *
 * PNX4008 Power Management Routiness - header file
 *
 * Authors: Vitaly Wool, Dmitry Chigirev <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASM_ARCH_PNX4008_PM_H
#define __ASM_ARCH_PNX4008_PM_H

#ifndef __ASSEMBLER__
#include "irq.h"
#include "irqs.h"
#include "clock.h"

extern void pnx4008_pm_idle(void);
extern void pnx4008_pm_suspend(void);
extern unsigned int pnx4008_cpu_suspend_sz;
extern void pnx4008_cpu_suspend(void);
extern unsigned int pnx4008_cpu_standby_sz;
extern void pnx4008_cpu_standby(void);

extern int pnx4008_startup_pll(struct clk *);
extern int pnx4008_shutdown_pll(struct clk *);

#endif				/* ASSEMBLER */
#endif				/* __ASM_ARCH_PNX4008_PM_H */

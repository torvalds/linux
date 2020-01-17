/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_MACHDEP_H
#define _PARISC_MACHDEP_H

#include <linux/yestifier.h>

#define	MACH_RESTART	1
#define	MACH_HALT	2
#define MACH_POWER_ON	3
#define	MACH_POWER_OFF	4

extern struct yestifier_block *mach_yestifier;
extern void pa7300lc_init(void);

extern void (*cpu_lpmc)(int, struct pt_regs *);

#endif

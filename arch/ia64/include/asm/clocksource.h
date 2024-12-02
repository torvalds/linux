/* SPDX-License-Identifier: GPL-2.0 */
/* IA64-specific clocksource additions */

#ifndef _ASM_IA64_CLOCKSOURCE_H
#define _ASM_IA64_CLOCKSOURCE_H

struct arch_clocksource_data {
	void *fsys_mmio;        /* used by fsyscall asm code */
};

#endif /* _ASM_IA64_CLOCKSOURCE_H */

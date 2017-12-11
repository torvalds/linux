/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CLOCKSOURCE_H
#define _ASM_CLOCKSOURCE_H

struct arch_clocksource_data {
	bool vdso_direct;	/* Usable for direct VDSO access? */
};

#endif

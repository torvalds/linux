/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSOCLOCKSOURCE_H
#define __ASM_VDSOCLOCKSOURCE_H

struct arch_clocksource_data {
	bool vdso_direct;	/* Usable for direct VDSO access? */
};

#endif

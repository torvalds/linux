/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_VDSOCLOCKSOURCE_H
#define __ASM_VDSOCLOCKSOURCE_H

#include <linux/types.h>

/* VDSO clocksources. */
#define VDSO_CLOCK_NONE		0	/* No suitable clocksource. */
#define VDSO_CLOCK_R4K		1	/* Use the coprocessor 0 count. */
#define VDSO_CLOCK_GIC		2	/* Use the GIC. */

/**
 * struct arch_clocksource_data - Architecture-specific clocksource information.
 * @vdso_clock_mode: Method the VDSO should use to access the clocksource.
 */
struct arch_clocksource_data {
	u8 vdso_clock_mode;
};

#endif /* __ASM_VDSOCLOCKSOURCE_H */

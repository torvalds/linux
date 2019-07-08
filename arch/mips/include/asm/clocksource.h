/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 */

#ifndef __ASM_CLOCKSOURCE_H
#define __ASM_CLOCKSOURCE_H

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

#endif /* __ASM_CLOCKSOURCE_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CLOCK_INLINED_H
#define _ASM_X86_CLOCK_INLINED_H

#include <asm/tsc.h>

struct clocksource;

static __always_inline u64 arch_inlined_clocksource_read(struct clocksource *cs)
{
	return (u64)rdtsc_ordered();
}

#endif

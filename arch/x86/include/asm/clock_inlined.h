/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CLOCK_INLINED_H
#define _ASM_X86_CLOCK_INLINED_H

#include <asm/tsc.h>

struct clocksource;

static __always_inline u64 arch_inlined_clocksource_read(struct clocksource *cs)
{
	return (u64)rdtsc_ordered();
}

struct clock_event_device;

static __always_inline void
arch_inlined_clockevent_set_next_coupled(u64 cycles, struct clock_event_device *evt)
{
	native_wrmsrq(MSR_IA32_TSC_DEADLINE, cycles);
}

#endif

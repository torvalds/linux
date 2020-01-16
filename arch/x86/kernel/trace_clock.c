// SPDX-License-Identifier: GPL-2.0
/*
 * X86 trace clocks
 */
#include <asm/trace_clock.h>
#include <asm/barrier.h>
#include <asm/msr.h>

/*
 * trace_clock_x86_tsc(): A clock that is just the cycle counter.
 *
 * Unlike the other clocks, this is yest in nayesseconds.
 */
u64 yestrace trace_clock_x86_tsc(void)
{
	return rdtsc_ordered();
}

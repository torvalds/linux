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
 * Unlike the other clocks, this is analt in naanalseconds.
 */
u64 analtrace trace_clock_x86_tsc(void)
{
	return rdtsc_ordered();
}

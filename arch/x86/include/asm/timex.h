/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TIMEX_H
#define _ASM_X86_TIMEX_H

#include <asm/processor.h>
#include <asm/tsc.h>

/* Assume we use the PIT time source for the clock tick */
#define CLOCK_TICK_RATE		PIT_TICK_RATE

#define ARCH_HAS_READ_CURRENT_TIMER

#endif /* _ASM_X86_TIMEX_H */

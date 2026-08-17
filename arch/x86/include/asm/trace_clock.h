/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRACE_CLOCK_H
#define _ASM_X86_TRACE_CLOCK_H

#include <linux/compiler.h>
#include <linux/types.h>

extern u64 notrace trace_clock_x86_tsc(void);

# define ARCH_TRACE_CLOCKS \
	{ trace_clock_x86_tsc,	"x86-tsc",	.in_ns = 0 },

#endif  /* _ASM_X86_TRACE_CLOCK_H */

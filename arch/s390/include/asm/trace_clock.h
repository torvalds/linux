/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_TRACE_CLOCK_H
#define _ASM_S390_TRACE_CLOCK_H

#include <linux/compiler.h>
#include <linux/types.h>

u64 notrace trace_clock_s390_tod(void);

#define ARCH_TRACE_CLOCKS \
	{ trace_clock_s390_tod, "s390-tod", .in_ns = 0 },

#endif /* _ASM_S390_TRACE_CLOCK_H */

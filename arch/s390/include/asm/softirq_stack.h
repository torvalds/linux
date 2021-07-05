/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_S390_SOFTIRQ_STACK_H
#define __ASM_S390_SOFTIRQ_STACK_H

#include <asm/lowcore.h>
#include <asm/stacktrace.h>

static inline void do_softirq_own_stack(void)
{
	CALL_ON_STACK(__do_softirq, S390_lowcore.async_stack, 0);
}

#endif /* __ASM_S390_SOFTIRQ_STACK_H */

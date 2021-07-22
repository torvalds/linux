/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_S390_SOFTIRQ_STACK_H
#define __ASM_S390_SOFTIRQ_STACK_H

#include <asm/lowcore.h>
#include <asm/stacktrace.h>

static inline void do_softirq_own_stack(void)
{
	call_on_stack(0, S390_lowcore.async_stack, void, __do_softirq);
}

#endif /* __ASM_S390_SOFTIRQ_STACK_H */

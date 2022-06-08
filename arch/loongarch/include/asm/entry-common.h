/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_LOONGARCH_ENTRY_COMMON_H
#define ARCH_LOONGARCH_ENTRY_COMMON_H

#include <linux/sched.h>
#include <linux/processor.h>

static inline bool on_thread_stack(void)
{
	return !(((unsigned long)(current->stack) ^ current_stack_pointer) & ~(THREAD_SIZE - 1));
}

#endif

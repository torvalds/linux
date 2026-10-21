/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_STACKTRACE_H
#define _ASM_ALPHA_STACKTRACE_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#include <asm/current.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

static __always_inline bool on_thread_stack(void)
{
	unsigned long base = (unsigned long)current->stack;
	unsigned long sp = (unsigned long)current_stack_pointer;

	return !((base ^ sp) & ~(THREAD_SIZE - 1));
}

#endif /* _ASM_ALPHA_STACKTRACE_H */

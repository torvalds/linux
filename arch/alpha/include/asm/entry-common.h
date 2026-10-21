/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_ALPHA_ENTRY_COMMON_H
#define ARCH_ALPHA_ENTRY_COMMON_H

#include <asm/stacktrace.h> /* For on_thread_stack() */
#include <asm/syscall.h>

#define arch_exit_to_user_mode_work arch_exit_to_user_mode_work

static __always_inline void arch_exit_to_user_mode_work(struct pt_regs *regs,
				unsigned long ti_work)
{
}
#endif

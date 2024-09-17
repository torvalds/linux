/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_SIGNAL_H
#define __ASM_SIGNAL_H

#include <uapi/asm/signal.h>
#include <uapi/asm/ptrace.h>

asmlinkage __visible
void do_work_pending(struct pt_regs *regs, unsigned long thread_info_flags);

#endif

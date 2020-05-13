/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SIGNAL_H
#define _ASM_POWERPC_SIGNAL_H

#define __ARCH_HAS_SA_RESTORER
#include <uapi/asm/signal.h>
#include <uapi/asm/ptrace.h>

struct pt_regs;
void do_notify_resume(struct pt_regs *regs, unsigned long thread_info_flags);

#endif /* _ASM_POWERPC_SIGNAL_H */

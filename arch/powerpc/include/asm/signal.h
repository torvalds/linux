/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SIGNAL_H
#define _ASM_POWERPC_SIGNAL_H

#define __ARCH_HAS_SA_RESTORER
#include <uapi/asm/signal.h>
#include <uapi/asm/ptrace.h>

struct pt_regs;
void do_notify_resume(struct pt_regs *regs, unsigned long thread_info_flags);

unsigned long get_min_sigframe_size_32(void);
unsigned long get_min_sigframe_size_64(void);
unsigned long get_min_sigframe_size(void);
unsigned long get_min_sigframe_size_compat(void);

#endif /* _ASM_POWERPC_SIGNAL_H */

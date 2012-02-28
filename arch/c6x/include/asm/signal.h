#ifndef _ASM_C6X_SIGNAL_H
#define _ASM_C6X_SIGNAL_H

#include <asm-generic/signal.h>

#ifndef __ASSEMBLY__
#include <linux/linkage.h>

struct pt_regs;

extern asmlinkage int do_rt_sigreturn(struct pt_regs *regs);
extern asmlinkage void do_notify_resume(struct pt_regs *regs,
					u32 thread_info_flags,
					int syscall);
#endif

#endif /* _ASM_C6X_SIGNAL_H */

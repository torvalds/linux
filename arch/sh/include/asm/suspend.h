#ifndef _ASM_SH_SUSPEND_H
#define _ASM_SH_SUSPEND_H

static inline int arch_prepare_suspend(void) { return 0; }

#include <asm/ptrace.h>

struct swsusp_arch_regs {
	struct pt_regs user_regs;
	unsigned long bank1_regs[8];
};

#endif /* _ASM_SH_SUSPEND_H */

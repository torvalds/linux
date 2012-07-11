#ifndef _ASM_X86_RCU_H
#define _ASM_X86_RCU_H

#include <linux/rcupdate.h>
#include <asm/ptrace.h>

static inline void exception_enter(struct pt_regs *regs)
{
	rcu_user_exit();
}

static inline void exception_exit(struct pt_regs *regs)
{
#ifdef CONFIG_RCU_USER_QS
	if (user_mode(regs))
		rcu_user_enter();
#endif
}

#endif

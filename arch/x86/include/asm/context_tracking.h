#ifndef _ASM_X86_CONTEXT_TRACKING_H
#define _ASM_X86_CONTEXT_TRACKING_H

#ifndef __ASSEMBLY__
#include <linux/context_tracking.h>
#include <asm/ptrace.h>

static inline void exception_enter(struct pt_regs *regs)
{
	user_exit();
}

static inline void exception_exit(struct pt_regs *regs)
{
#ifdef CONFIG_CONTEXT_TRACKING
	if (user_mode(regs))
		user_enter();
#endif
}

#else /* __ASSEMBLY__ */

#ifdef CONFIG_CONTEXT_TRACKING
# define SCHEDULE_USER call schedule_user
#else
# define SCHEDULE_USER call schedule
#endif

#endif /* !__ASSEMBLY__ */

#endif

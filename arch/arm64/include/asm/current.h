#ifndef __ASM_CURRENT_H
#define __ASM_CURRENT_H

#include <linux/compiler.h>

#include <asm/sysreg.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_THREAD_INFO_IN_TASK
struct task_struct;

/*
 * We don't use read_sysreg() as we want the compiler to cache the value where
 * possible.
 */
static __always_inline struct task_struct *get_current(void)
{
	unsigned long sp_el0;

	asm ("mrs %0, sp_el0" : "=r" (sp_el0));

	return (struct task_struct *)sp_el0;
}
#define current get_current()
#else
#include <linux/thread_info.h>
#define get_current() (current_thread_info()->task)
#define current get_current()
#endif

#endif /* __ASSEMBLY__ */

#endif /* __ASM_CURRENT_H */


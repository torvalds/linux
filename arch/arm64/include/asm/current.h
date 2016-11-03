#ifndef __ASM_CURRENT_H
#define __ASM_CURRENT_H

#include <linux/compiler.h>

#include <asm/sysreg.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_THREAD_INFO_IN_TASK
struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	return (struct task_struct *)read_sysreg(sp_el0);
}
#define current get_current()
#else
#include <linux/thread_info.h>
#define get_current() (current_thread_info()->task)
#define current get_current()
#endif

#endif /* __ASSEMBLY__ */

#endif /* __ASM_CURRENT_H */


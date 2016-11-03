#ifndef __ASM_CURRENT_H
#define __ASM_CURRENT_H

#include <linux/compiler.h>

#include <asm/sysreg.h>

#ifndef __ASSEMBLY__

struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	return (struct task_struct *)read_sysreg(sp_el0);
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* __ASM_CURRENT_H */


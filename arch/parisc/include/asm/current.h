/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_CURRENT_H
#define _ASM_PARISC_CURRENT_H

#include <asm/special_insns.h>

#ifndef __ASSEMBLY__
struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	return (struct task_struct *) mfctl(30);
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* _ASM_PARISC_CURRENT_H */

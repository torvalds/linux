/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_CURRENT_H
#define _ASM_PARISC_CURRENT_H

#ifndef __ASSEMBLY__
struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	struct task_struct *ts;

	/* do not use mfctl() macro as it is marked volatile */
	asm( "mfctl %%cr30,%0" : "=r" (ts) );
	return ts;
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* _ASM_PARISC_CURRENT_H */

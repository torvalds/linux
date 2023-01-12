/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_X86_GSSEG_H
#define _ASM_X86_GSSEG_H

#include <linux/types.h>

#include <asm/asm.h>
#include <asm/cpufeature.h>
#include <asm/alternative.h>
#include <asm/processor.h>
#include <asm/nops.h>

#ifdef CONFIG_X86_64

extern asmlinkage void asm_load_gs_index(u16 selector);

static inline void native_load_gs_index(unsigned int selector)
{
	unsigned long flags;

	local_irq_save(flags);
	asm_load_gs_index(selector);
	local_irq_restore(flags);
}

#endif /* CONFIG_X86_64 */

#ifndef CONFIG_PARAVIRT_XXL

static inline void load_gs_index(unsigned int selector)
{
#ifdef CONFIG_X86_64
	native_load_gs_index(selector);
#else
	loadsegment(gs, selector);
#endif
}

#endif /* CONFIG_PARAVIRT_XXL */

#endif /* _ASM_X86_GSSEG_H */

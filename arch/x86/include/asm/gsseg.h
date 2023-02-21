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

/* Replace with "lkgs %di" once binutils support LKGS instruction */
#define LKGS_DI _ASM_BYTES(0xf2,0x0f,0x00,0xf7)

static inline void native_lkgs(unsigned int selector)
{
	u16 sel = selector;
	asm_inline volatile("1: " LKGS_DI
			    _ASM_EXTABLE_TYPE_REG(1b, 1b, EX_TYPE_ZERO_REG, %k[sel])
			    : [sel] "+D" (sel));
}

static inline void native_load_gs_index(unsigned int selector)
{
	if (cpu_feature_enabled(X86_FEATURE_LKGS)) {
		native_lkgs(selector);
	} else {
		unsigned long flags;

		local_irq_save(flags);
		asm_load_gs_index(selector);
		local_irq_restore(flags);
	}
}

#endif /* CONFIG_X86_64 */

static inline void __init lkgs_init(void)
{
#ifdef CONFIG_PARAVIRT_XXL
#ifdef CONFIG_X86_64
	if (cpu_feature_enabled(X86_FEATURE_LKGS))
		pv_ops.cpu.load_gs_index = native_lkgs;
#endif
#endif
}

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

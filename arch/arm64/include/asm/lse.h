/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LSE_H
#define __ASM_LSE_H

#include <asm/atomic_ll_sc.h>

#ifdef CONFIG_ARM64_LSE_ATOMICS

#define __LSE_PREAMBLE	".arch_extension lse\n"

#include <linux/compiler_types.h>
#include <linux/export.h>
#include <linux/jump_label.h>
#include <linux/stringify.h>
#include <asm/alternative.h>
#include <asm/alternative-macros.h>
#include <asm/atomic_lse.h>
#include <asm/cpucaps.h>

static __always_inline bool system_uses_lse_atomics(void)
{
	return alternative_has_feature_likely(ARM64_HAS_LSE_ATOMICS);
}

#define __lse_ll_sc_body(op, ...)					\
({									\
	system_uses_lse_atomics() ?					\
		__lse_##op(__VA_ARGS__) :				\
		__ll_sc_##op(__VA_ARGS__);				\
})

/* In-line patching at runtime */
#define ARM64_LSE_ATOMIC_INSN(llsc, lse)				\
	ALTERNATIVE(llsc, __LSE_PREAMBLE lse, ARM64_HAS_LSE_ATOMICS)

#else	/* CONFIG_ARM64_LSE_ATOMICS */

static inline bool system_uses_lse_atomics(void) { return false; }

#define __lse_ll_sc_body(op, ...)		__ll_sc_##op(__VA_ARGS__)

#define ARM64_LSE_ATOMIC_INSN(llsc, lse)	llsc

#endif	/* CONFIG_ARM64_LSE_ATOMICS */
#endif	/* __ASM_LSE_H */

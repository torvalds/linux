/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LSE_H
#define __ASM_LSE_H

#include <asm/atomic_ll_sc.h>

#if defined(CONFIG_AS_LSE) && defined(CONFIG_ARM64_LSE_ATOMICS)

#include <linux/compiler_types.h>
#include <linux/export.h>
#include <linux/jump_label.h>
#include <linux/stringify.h>
#include <asm/alternative.h>
#include <asm/atomic_lse.h>
#include <asm/cpucaps.h>

__asm__(".arch_extension	lse");

extern struct static_key_false cpu_hwcap_keys[ARM64_NCAPS];
extern struct static_key_false arm64_const_caps_ready;

static inline bool system_uses_lse_atomics(void)
{
	return (static_branch_likely(&arm64_const_caps_ready)) &&
		static_branch_likely(&cpu_hwcap_keys[ARM64_HAS_LSE_ATOMICS]);
}

#define __lse_ll_sc_body(op, ...)					\
({									\
	system_uses_lse_atomics() ?					\
		__lse_##op(__VA_ARGS__) :				\
		__ll_sc_##op(__VA_ARGS__);				\
})

/* In-line patching at runtime */
#define ARM64_LSE_ATOMIC_INSN(llsc, lse)				\
	ALTERNATIVE(llsc, lse, ARM64_HAS_LSE_ATOMICS)

#else	/* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */

static inline bool system_uses_lse_atomics(void) { return false; }

#define __lse_ll_sc_body(op, ...)		__ll_sc_##op(__VA_ARGS__)

#define ARM64_LSE_ATOMIC_INSN(llsc, lse)	llsc

#endif	/* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */
#endif	/* __ASM_LSE_H */

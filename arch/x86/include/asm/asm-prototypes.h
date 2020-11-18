/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/ftrace.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/string.h>
#include <asm/page.h>
#include <asm/checksum.h>

#include <asm-generic/asm-prototypes.h>

#include <asm/special_insns.h>
#include <asm/preempt.h>
#include <asm/asm.h>

#ifndef CONFIG_X86_CMPXCHG64
extern void cmpxchg8b_emu(void);
#endif

#ifdef CONFIG_RETPOLINE

#define DECL_INDIRECT_THUNK(reg) \
	extern asmlinkage void __x86_indirect_thunk_ ## reg (void);

#define DECL_RETPOLINE(reg) \
	extern asmlinkage void __x86_retpoline_ ## reg (void);

#undef GEN
#define GEN(reg) DECL_INDIRECT_THUNK(reg)
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) DECL_RETPOLINE(reg)
#include <asm/GEN-for-each-reg.h>

#endif /* CONFIG_RETPOLINE */

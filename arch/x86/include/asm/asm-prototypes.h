/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/ftrace.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/string.h>
#include <asm/page.h>
#include <asm/checksum.h>
#include <asm/mce.h>

#include <asm-generic/asm-prototypes.h>

#include <asm/special_insns.h>
#include <asm/preempt.h>
#include <asm/asm.h>

#ifndef CONFIG_X86_CMPXCHG64
extern void cmpxchg8b_emu(void);
#endif

#ifdef CONFIG_RETPOLINE

#undef GEN
#define GEN(reg) \
	extern asmlinkage void __x86_indirect_thunk_ ## reg (void);
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) \
	extern asmlinkage void __x86_indirect_alt_call_ ## reg (void);
#include <asm/GEN-for-each-reg.h>

#undef GEN
#define GEN(reg) \
	extern asmlinkage void __x86_indirect_alt_jmp_ ## reg (void);
#include <asm/GEN-for-each-reg.h>

#endif /* CONFIG_RETPOLINE */

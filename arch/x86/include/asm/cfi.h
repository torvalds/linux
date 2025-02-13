/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CFI_H
#define _ASM_X86_CFI_H

/*
 * Clang Control Flow Integrity (CFI) support.
 *
 * Copyright (C) 2022 Google LLC
 */
#include <linux/bug.h>
#include <asm/ibt.h>

/*
 * An overview of the various calling conventions...
 *
 * Traditional:
 *
 * foo:
 *   ... code here ...
 *   ret
 *
 * direct caller:
 *   call foo
 *
 * indirect caller:
 *   lea foo(%rip), %r11
 *   ...
 *   call *%r11
 *
 *
 * IBT:
 *
 * foo:
 *   endbr64
 *   ... code here ...
 *   ret
 *
 * direct caller:
 *   call foo / call foo+4
 *
 * indirect caller:
 *   lea foo(%rip), %r11
 *   ...
 *   call *%r11
 *
 *
 * kCFI:
 *
 * __cfi_foo:
 *   movl $0x12345678, %eax
 *				# 11 nops when CONFIG_CALL_PADDING
 * foo:
 *   endbr64			# when IBT
 *   ... code here ...
 *   ret
 *
 * direct call:
 *   call foo			# / call foo+4 when IBT
 *
 * indirect call:
 *   lea foo(%rip), %r11
 *   ...
 *   movl $(-0x12345678), %r10d
 *   addl -4(%r11), %r10d	# -15 when CONFIG_CALL_PADDING
 *   jz   1f
 *   ud2
 * 1:call *%r11
 *
 *
 * FineIBT (builds as kCFI + CALL_PADDING + IBT + RETPOLINE and runtime patches into):
 *
 * __cfi_foo:
 *   endbr64
 *   subl 0x12345678, %r10d
 *   jz   foo
 *   ud2
 *   nop
 * foo:
 *   osp nop3			# was endbr64
 *   ... code here ...
 *   ret
 *
 * direct caller:
 *   call foo / call foo+4
 *
 * indirect caller:
 *   lea foo(%rip), %r11
 *   ...
 *   movl $0x12345678, %r10d
 *   subl $16, %r11
 *   nop4
 *   call *%r11
 *
 */
enum cfi_mode {
	CFI_AUTO,	/* FineIBT if hardware has IBT, otherwise kCFI */
	CFI_OFF,	/* Taditional / IBT depending on .config */
	CFI_KCFI,	/* Optionally CALL_PADDING, IBT, RETPOLINE */
	CFI_FINEIBT,	/* see arch/x86/kernel/alternative.c */
};

extern enum cfi_mode cfi_mode;

struct pt_regs;

#ifdef CONFIG_CFI_CLANG
enum bug_trap_type handle_cfi_failure(struct pt_regs *regs);
#define __bpfcall
extern u32 cfi_bpf_hash;
extern u32 cfi_bpf_subprog_hash;

static inline int cfi_get_offset(void)
{
	switch (cfi_mode) {
	case CFI_FINEIBT:
		return 16;
	case CFI_KCFI:
		if (IS_ENABLED(CONFIG_CALL_PADDING))
			return 16;
		return 5;
	default:
		return 0;
	}
}
#define cfi_get_offset cfi_get_offset

extern u32 cfi_get_func_hash(void *func);

#ifdef CONFIG_FINEIBT
extern bool decode_fineibt_insn(struct pt_regs *regs, unsigned long *target, u32 *type);
#else
static inline bool
decode_fineibt_insn(struct pt_regs *regs, unsigned long *target, u32 *type)
{
	return false;
}

#endif

#else
static inline enum bug_trap_type handle_cfi_failure(struct pt_regs *regs)
{
	return BUG_TRAP_TYPE_NONE;
}
#define cfi_bpf_hash 0U
#define cfi_bpf_subprog_hash 0U
static inline u32 cfi_get_func_hash(void *func)
{
	return 0;
}
#endif /* CONFIG_CFI_CLANG */

#if HAS_KERNEL_IBT == 1
#define CFI_NOSEAL(x)	asm(IBT_NOSEAL(__stringify(x)))
#endif

#endif /* _ASM_X86_CFI_H */

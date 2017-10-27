#ifndef _ASM_X86_INSN_EVAL_H
#define _ASM_X86_INSN_EVAL_H
/*
 * A collection of utility functions for x86 instruction analysis to be
 * used in a kernel context. Useful when, for instance, making sense
 * of the registers indicated by operands.
 */

#include <linux/compiler.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <asm/ptrace.h>

void __user *insn_get_addr_ref(struct insn *insn, struct pt_regs *regs);
int insn_get_modrm_rm_off(struct insn *insn, struct pt_regs *regs);

#endif /* _ASM_X86_INSN_EVAL_H */

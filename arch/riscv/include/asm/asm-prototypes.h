/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_PROTOTYPES_H
#define _ASM_RISCV_PROTOTYPES_H

#include <linux/ftrace.h>
#include <asm-generic/asm-prototypes.h>

long long __lshrti3(long long a, int b);
long long __ashrti3(long long a, int b);
long long __ashlti3(long long a, int b);


#define DECLARE_DO_ERROR_INFO(name)	asmlinkage void name(struct pt_regs *regs)

DECLARE_DO_ERROR_INFO(do_trap_unknown);
DECLARE_DO_ERROR_INFO(do_trap_insn_misaligned);
DECLARE_DO_ERROR_INFO(do_trap_insn_fault);
DECLARE_DO_ERROR_INFO(do_trap_insn_illegal);
DECLARE_DO_ERROR_INFO(do_trap_load_fault);
DECLARE_DO_ERROR_INFO(do_trap_load_misaligned);
DECLARE_DO_ERROR_INFO(do_trap_store_misaligned);
DECLARE_DO_ERROR_INFO(do_trap_store_fault);
DECLARE_DO_ERROR_INFO(do_trap_ecall_u);
DECLARE_DO_ERROR_INFO(do_trap_ecall_s);
DECLARE_DO_ERROR_INFO(do_trap_ecall_m);
DECLARE_DO_ERROR_INFO(do_trap_break);

asmlinkage unsigned long get_overflow_stack(void);
asmlinkage void handle_bad_stack(struct pt_regs *regs);
asmlinkage void do_page_fault(struct pt_regs *regs);
asmlinkage void do_irq(struct pt_regs *regs);

#endif /* _ASM_RISCV_PROTOTYPES_H */

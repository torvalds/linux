/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_PROTOTYPES_H
#define _ASM_RISCV_PROTOTYPES_H

#include <linux/ftrace.h>
#include <asm-generic/asm-prototypes.h>

long long __lshrti3(long long a, int b);
long long __ashrti3(long long a, int b);
long long __ashlti3(long long a, int b);

#ifdef CONFIG_RISCV_ISA_V

#ifdef CONFIG_MMU
asmlinkage int enter_vector_usercopy(void *dst, void *src, size_t n, bool enable_sum);
#endif /* CONFIG_MMU  */

void xor_regs_2_(unsigned long bytes, unsigned long *__restrict p1,
		 const unsigned long *__restrict p2);
void xor_regs_3_(unsigned long bytes, unsigned long *__restrict p1,
		 const unsigned long *__restrict p2,
		 const unsigned long *__restrict p3);
void xor_regs_4_(unsigned long bytes, unsigned long *__restrict p1,
		 const unsigned long *__restrict p2,
		 const unsigned long *__restrict p3,
		 const unsigned long *__restrict p4);
void xor_regs_5_(unsigned long bytes, unsigned long *__restrict p1,
		 const unsigned long *__restrict p2,
		 const unsigned long *__restrict p3,
		 const unsigned long *__restrict p4,
		 const unsigned long *__restrict p5);

#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
asmlinkage void riscv_v_context_nesting_start(struct pt_regs *regs);
asmlinkage void riscv_v_context_nesting_end(struct pt_regs *regs);
#endif /* CONFIG_RISCV_ISA_V_PREEMPTIVE */

#endif /* CONFIG_RISCV_ISA_V */

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

asmlinkage void ret_from_fork_kernel(void *fn_arg, int (*fn)(void *), struct pt_regs *regs);
asmlinkage void ret_from_fork_user(struct pt_regs *regs);
asmlinkage void handle_bad_stack(struct pt_regs *regs);
asmlinkage void do_page_fault(struct pt_regs *regs);
asmlinkage void do_irq(struct pt_regs *regs);

#endif /* _ASM_RISCV_PROTOTYPES_H */

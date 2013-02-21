/*
 * linux/include/asm-m32r/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * M32R version:
 *   Copyright (C) 2001-2002, 2004  Hirokazu Takata <takata at linux-m32r.org>
 */
#ifndef _ASM_M32R_PTRACE_H
#define _ASM_M32R_PTRACE_H


#include <asm/m32r.h>		/* M32R_PSW_BSM, M32R_PSW_BPM */
#include <uapi/asm/ptrace.h>

#define arch_has_single_step() (1)

struct task_struct;
extern void init_debug_traps(struct task_struct *);
#define arch_ptrace_attach(child) \
	init_debug_traps(child)

#if defined(CONFIG_ISA_M32R2) || defined(CONFIG_CHIP_VDEC2)
#define user_mode(regs) ((M32R_PSW_BPM & (regs)->psw) != 0)
#elif defined(CONFIG_ISA_M32R)
#define user_mode(regs) ((M32R_PSW_BSM & (regs)->psw) != 0)
#else
#error unknown isa configuration
#endif

#define instruction_pointer(regs) ((regs)->bpc)
#define profile_pc(regs) instruction_pointer(regs)
#define user_stack_pointer(regs) ((regs)->spu)

extern void withdraw_debug_trap(struct pt_regs *regs);

#define task_pt_regs(task) \
        ((struct pt_regs *)(task_stack_page(task) + THREAD_SIZE) - 1)
#define current_pt_regs() ((struct pt_regs *) \
	((unsigned long)current_thread_info() + THREAD_SIZE) - 1)

#endif /* _ASM_M32R_PTRACE_H */

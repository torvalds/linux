/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#ifndef _ASM_RISCV_FTRACE_H
#define _ASM_RISCV_FTRACE_H

/*
 * The graph frame test is not possible if CONFIG_FRAME_POINTER is not enabled.
 * Check arch/riscv/kernel/mcount.S for detail.
 */
#if defined(CONFIG_FUNCTION_GRAPH_TRACER) && defined(CONFIG_FRAME_POINTER)
#define HAVE_FUNCTION_GRAPH_FP_TEST
#endif
#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR

/*
 * Clang prior to 13 had "mcount" instead of "_mcount":
 * https://reviews.llvm.org/D98881
 */
#if defined(CONFIG_CC_IS_GCC) || CONFIG_CLANG_VERSION >= 130000
#define MCOUNT_NAME _mcount
#else
#define MCOUNT_NAME mcount
#endif

#define ARCH_SUPPORTS_FTRACE_OPS 1
#ifndef __ASSEMBLY__
void MCOUNT_NAME(void);
static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct dyn_arch_ftrace {
};
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * A general call in RISC-V is a pair of insts:
 * 1) auipc: setting high-20 pc-related bits to ra register
 * 2) jalr: setting low-12 offset to ra, jump to ra, and set ra to
 *          return address (original pc + 4)
 *
 * Dynamic ftrace generates probes to call sites, so we must deal with
 * both auipc and jalr at the same time.
 */

#define MCOUNT_ADDR		((unsigned long)MCOUNT_NAME)
#define JALR_SIGN_MASK		(0x00000800)
#define JALR_OFFSET_MASK	(0x00000fff)
#define AUIPC_OFFSET_MASK	(0xfffff000)
#define AUIPC_PAD		(0x00001000)
#define JALR_SHIFT		20
#define JALR_BASIC		(0x000080e7)
#define AUIPC_BASIC		(0x00000097)
#define NOP4			(0x00000013)

#define make_call(caller, callee, call)					\
do {									\
	call[0] = to_auipc_insn((unsigned int)((unsigned long)callee -	\
				(unsigned long)caller));		\
	call[1] = to_jalr_insn((unsigned int)((unsigned long)callee -	\
			       (unsigned long)caller));			\
} while (0)

#define to_jalr_insn(offset)						\
	(((offset & JALR_OFFSET_MASK) << JALR_SHIFT) | JALR_BASIC)

#define to_auipc_insn(offset)						\
	((offset & JALR_SIGN_MASK) ?					\
	(((offset & AUIPC_OFFSET_MASK) + AUIPC_PAD) | AUIPC_BASIC) :	\
	((offset & AUIPC_OFFSET_MASK) | AUIPC_BASIC))

/*
 * Let auipc+jalr be the basic *mcount unit*, so we make it 8 bytes here.
 */
#define MCOUNT_INSN_SIZE 8

#ifndef __ASSEMBLY__
struct dyn_ftrace;
int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec);
#define ftrace_init_nop ftrace_init_nop
#endif

#endif /* CONFIG_DYNAMIC_FTRACE */

#endif /* _ASM_RISCV_FTRACE_H */

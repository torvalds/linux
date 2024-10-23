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

extern void *return_address(unsigned int level);

#define ftrace_return_address(n) return_address(n)

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
 *<ftrace enable>:
 * 0: auipc  t0/ra, 0x?
 * 4: jalr   t0/ra, ?(t0/ra)
 *
 *<ftrace disable>:
 * 0: nop
 * 4: nop
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
#define JALR_RA			(0x000080e7)
#define AUIPC_RA		(0x00000097)
#define JALR_T0			(0x000282e7)
#define AUIPC_T0		(0x00000297)
#define NOP4			(0x00000013)

#define to_jalr_t0(offset)						\
	(((offset & JALR_OFFSET_MASK) << JALR_SHIFT) | JALR_T0)

#define to_auipc_t0(offset)						\
	((offset & JALR_SIGN_MASK) ?					\
	(((offset & AUIPC_OFFSET_MASK) + AUIPC_PAD) | AUIPC_T0) :	\
	((offset & AUIPC_OFFSET_MASK) | AUIPC_T0))

#define make_call_t0(caller, callee, call)				\
do {									\
	unsigned int offset =						\
		(unsigned long) callee - (unsigned long) caller;	\
	call[0] = to_auipc_t0(offset);					\
	call[1] = to_jalr_t0(offset);					\
} while (0)

#define to_jalr_ra(offset)						\
	(((offset & JALR_OFFSET_MASK) << JALR_SHIFT) | JALR_RA)

#define to_auipc_ra(offset)						\
	((offset & JALR_SIGN_MASK) ?					\
	(((offset & AUIPC_OFFSET_MASK) + AUIPC_PAD) | AUIPC_RA) :	\
	((offset & AUIPC_OFFSET_MASK) | AUIPC_RA))

#define make_call_ra(caller, callee, call)				\
do {									\
	unsigned int offset =						\
		(unsigned long) callee - (unsigned long) caller;	\
	call[0] = to_auipc_ra(offset);					\
	call[1] = to_jalr_ra(offset);					\
} while (0)

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

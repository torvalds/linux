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

#define ARCH_SUPPORTS_FTRACE_OPS 1
#ifndef __ASSEMBLY__

extern void *return_address(unsigned int level);

#define ftrace_return_address(n) return_address(n)

void _mcount(void);
static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

/*
 * Let's do like x86/arm64 and ignore the compat syscalls.
 */
#define ARCH_TRACE_IGNORE_COMPAT_SYSCALLS
static inline bool arch_trace_is_compat_syscall(struct pt_regs *regs)
{
	return is_compat_task();
}

#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym,
					       const char *name)
{
	/*
	 * Since all syscall functions have __riscv_ prefix, we must skip it.
	 * However, as we described above, we decided to ignore compat
	 * syscalls, so we don't care about __riscv_compat_ prefix here.
	 */
	return !strcmp(sym + 8, name);
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

#define MCOUNT_ADDR		((unsigned long)_mcount)
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

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
struct ftrace_ops;
struct ftrace_regs;
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs);
#define ftrace_graph_func ftrace_graph_func

static inline void __arch_ftrace_set_direct_caller(struct pt_regs *regs, unsigned long addr)
{
		regs->t1 = addr;
}
#define arch_ftrace_set_direct_caller(fregs, addr) \
	__arch_ftrace_set_direct_caller(&(fregs)->regs, addr)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_DYNAMIC_FTRACE */

#ifndef __ASSEMBLY__
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
struct fgraph_ret_regs {
	unsigned long a1;
	unsigned long a0;
	unsigned long s0;
	unsigned long ra;
};

static inline unsigned long fgraph_ret_regs_return_value(struct fgraph_ret_regs *ret_regs)
{
	return ret_regs->a0;
}

static inline unsigned long fgraph_ret_regs_frame_pointer(struct fgraph_ret_regs *ret_regs)
{
	return ret_regs->s0;
}
#endif /* ifdef CONFIG_FUNCTION_GRAPH_TRACER */
#endif

#endif /* _ASM_RISCV_FTRACE_H */

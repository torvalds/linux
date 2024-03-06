/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_EXTABLE_H
#define _ASM_RISCV_EXTABLE_H

/*
 * The exception table consists of pairs of relative offsets: the first
 * is the relative offset to an instruction that is allowed to fault,
 * and the second is the relative offset at which the program should
 * continue. No registers are modified, so it is entirely up to the
 * continuation code to figure out what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry {
	int insn, fixup;
	short type, data;
};

#define ARCH_HAS_RELATIVE_EXTABLE

#define swap_ex_entry_fixup(a, b, tmp, delta)		\
do {							\
	(a)->fixup = (b)->fixup + (delta);		\
	(b)->fixup = (tmp).fixup - (delta);		\
	(a)->type = (b)->type;				\
	(b)->type = (tmp).type;				\
	(a)->data = (b)->data;				\
	(b)->data = (tmp).data;				\
} while (0)

#ifdef CONFIG_MMU
bool fixup_exception(struct pt_regs *regs);
#else
static inline bool fixup_exception(struct pt_regs *regs) { return false; }
#endif

#if defined(CONFIG_BPF_JIT) && defined(CONFIG_ARCH_RV64I)
bool ex_handler_bpf(const struct exception_table_entry *ex, struct pt_regs *regs);
#else
static inline bool
ex_handler_bpf(const struct exception_table_entry *ex,
	       struct pt_regs *regs)
{
	return false;
}
#endif

#endif

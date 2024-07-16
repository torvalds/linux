/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_EXTABLE_H
#define _ASM_X86_EXTABLE_H

#include <asm/extable_fixup_types.h>

/*
 * The exception table consists of two addresses relative to the
 * exception table entry itself and a type selector field.
 *
 * The first address is of an instruction that is allowed to fault, the
 * second is the target at which the program should continue.
 *
 * The type entry is used by fixup_exception() to select the handler to
 * deal with the fault caused by the instruction in the first field.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry {
	int insn, fixup, data;
};
struct pt_regs;

#define ARCH_HAS_RELATIVE_EXTABLE

#define swap_ex_entry_fixup(a, b, tmp, delta)			\
	do {							\
		(a)->fixup = (b)->fixup + (delta);		\
		(b)->fixup = (tmp).fixup - (delta);		\
		(a)->data = (b)->data;				\
		(b)->data = (tmp).data;				\
	} while (0)

extern int fixup_exception(struct pt_regs *regs, int trapnr,
			   unsigned long error_code, unsigned long fault_addr);
extern int fixup_bug(struct pt_regs *regs, int trapnr);
extern int ex_get_fixup_type(unsigned long ip);
extern void early_fixup_exception(struct pt_regs *regs, int trapnr);

#ifdef CONFIG_X86_MCE
extern void __noreturn ex_handler_msr_mce(struct pt_regs *regs, bool wrmsr);
#else
static inline void __noreturn ex_handler_msr_mce(struct pt_regs *regs, bool wrmsr)
{
	for (;;)
		cpu_relax();
}
#endif

#if defined(CONFIG_BPF_JIT) && defined(CONFIG_X86_64)
bool ex_handler_bpf(const struct exception_table_entry *x, struct pt_regs *regs);
#else
static inline bool ex_handler_bpf(const struct exception_table_entry *x,
				  struct pt_regs *regs) { return false; }
#endif

#endif

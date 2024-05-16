/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PARISC_EXTABLE_H
#define __PARISC_EXTABLE_H

#include <asm/ptrace.h>
#include <linux/compiler.h>

/*
 * The exception table consists of three addresses:
 *
 * - A relative address to the instruction that is allowed to fault.
 * - A relative address at which the program should continue (fixup routine)
 * - An asm statement which specifies which CPU register will
 *   receive -EFAULT when an exception happens if the lowest bit in
 *   the fixup address is set.
 *
 * Note: The register specified in the err_opcode instruction will be
 * modified at runtime if a fault happens. Register %r0 will be ignored.
 *
 * Since relative addresses are used, 32bit values are sufficient even on
 * 64bit kernel.
 */

struct pt_regs;
int fixup_exception(struct pt_regs *regs);

#define ARCH_HAS_RELATIVE_EXTABLE
struct exception_table_entry {
	int insn;	/* relative address of insn that is allowed to fault. */
	int fixup;	/* relative address of fixup routine */
	int err_opcode; /* sample opcode with register which holds error code */
};

#define ASM_EXCEPTIONTABLE_ENTRY( fault_addr, except_addr, opcode )\
	".section __ex_table,\"aw\"\n"			   \
	".align 4\n"					   \
	".word (" #fault_addr " - .), (" #except_addr " - .)\n" \
	opcode "\n"					   \
	".previous\n"

/*
 * ASM_EXCEPTIONTABLE_ENTRY_EFAULT() creates a special exception table entry
 * (with lowest bit set) for which the fault handler in fixup_exception() will
 * load -EFAULT on fault into the register specified by the err_opcode instruction,
 * and zeroes the target register in case of a read fault in get_user().
 */
#define ASM_EXCEPTIONTABLE_VAR(__err_var)		\
	int __err_var = 0
#define ASM_EXCEPTIONTABLE_ENTRY_EFAULT( fault_addr, except_addr, register )\
	ASM_EXCEPTIONTABLE_ENTRY( fault_addr, except_addr + 1, "or %%r0,%%r0," register)

static inline void swap_ex_entry_fixup(struct exception_table_entry *a,
				       struct exception_table_entry *b,
				       struct exception_table_entry tmp,
				       int delta)
{
	a->fixup = b->fixup + delta;
	b->fixup = tmp.fixup - delta;
	a->err_opcode = b->err_opcode;
	b->err_opcode = tmp.err_opcode;
}
#define swap_ex_entry_fixup swap_ex_entry_fixup

#endif

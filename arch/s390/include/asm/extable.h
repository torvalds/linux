/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_EXTABLE_H
#define __S390_EXTABLE_H

#include <asm/ptrace.h>
#include <linux/compiler.h>

/*
 * The exception table consists of three addresses:
 *
 * - Address of an instruction that is allowed to fault.
 * - Address at which the program should continue.
 * - Optional address of handler that takes pt_regs * argument and runs in
 *   interrupt context.
 *
 * No registers are modified, so it is entirely up to the continuation code
 * to figure out what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	int insn, fixup;
	long handler;
};

extern struct exception_table_entry *__start_dma_ex_table;
extern struct exception_table_entry *__stop_dma_ex_table;

const struct exception_table_entry *s390_search_extables(unsigned long addr);

static inline unsigned long extable_fixup(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}

typedef bool (*ex_handler_t)(const struct exception_table_entry *,
			     struct pt_regs *);

static inline ex_handler_t
ex_fixup_handler(const struct exception_table_entry *x)
{
	if (likely(!x->handler))
		return NULL;
	return (ex_handler_t)((unsigned long)&x->handler + x->handler);
}

static inline bool ex_handle(const struct exception_table_entry *x,
			     struct pt_regs *regs)
{
	ex_handler_t handler = ex_fixup_handler(x);

	if (unlikely(handler))
		return handler(x, regs);
	regs->psw.addr = extable_fixup(x);
	return true;
}

#define ARCH_HAS_RELATIVE_EXTABLE

static inline void swap_ex_entry_fixup(struct exception_table_entry *a,
				       struct exception_table_entry *b,
				       struct exception_table_entry tmp,
				       int delta)
{
	a->fixup = b->fixup + delta;
	b->fixup = tmp.fixup - delta;
	a->handler = b->handler + delta;
	b->handler = tmp.handler - delta;
}

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_POWERPC_EXTABLE_H
#define _ARCH_POWERPC_EXTABLE_H

/*
 * The exception table consists of pairs of relative addresses: the first is
 * the address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out what
 * to do.
 *
 * All the routines below use bits of fixup code that are out of line with the
 * main instruction path.  This means when everything is well, we don't even
 * have to jump over them.  Further, they do not intrude on our cache or tlb
 * entries.
 */

#define ARCH_HAS_RELATIVE_EXTABLE

struct exception_table_entry {
	int insn;
	int fixup;
};

static inline unsigned long extable_fixup(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}

#endif

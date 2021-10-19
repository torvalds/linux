/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_EXTABLE_H
#define __ASM_ASM_EXTABLE_H

#ifdef __ASSEMBLY__

#define __ASM_EXTABLE_RAW(insn, fixup)		\
	.pushsection	__ex_table, "a";	\
	.align		3;			\
	.long		((insn) - .);		\
	.long		((fixup) - .);		\
	.popsection;

/*
 * Create an exception table entry for `insn`, which will branch to `fixup`
 * when an unhandled fault is taken.
 */
	.macro		_asm_extable, insn, fixup
	__ASM_EXTABLE_RAW(\insn, \fixup)
	.endm

/*
 * Create an exception table entry for `insn` if `fixup` is provided. Otherwise
 * do nothing.
 */
	.macro		_cond_extable, insn, fixup
	.ifnc		\fixup,
	_asm_extable	\insn, \fixup
	.endif
	.endm

#else /* __ASSEMBLY__ */

#include <linux/stringify.h>

#define __ASM_EXTABLE_RAW(insn, fixup)		\
	".pushsection	__ex_table, \"a\"\n"	\
	".align		3\n"			\
	".long		((" insn ") - .)\n"	\
	".long		((" fixup ") - .)\n"	\
	".popsection\n"

#define _ASM_EXTABLE(insn, fixup) \
	__ASM_EXTABLE_RAW(#insn, #fixup)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ASM_EXTABLE_H */

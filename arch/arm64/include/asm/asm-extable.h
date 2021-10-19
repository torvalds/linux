/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_EXTABLE_H
#define __ASM_ASM_EXTABLE_H

#define EX_TYPE_NONE			0
#define EX_TYPE_FIXUP			1
#define EX_TYPE_BPF			2

#ifdef __ASSEMBLY__

#define __ASM_EXTABLE_RAW(insn, fixup, type, data)	\
	.pushsection	__ex_table, "a";		\
	.align		2;				\
	.long		((insn) - .);			\
	.long		((fixup) - .);			\
	.short		(type);				\
	.short		(data);				\
	.popsection;

/*
 * Create an exception table entry for `insn`, which will branch to `fixup`
 * when an unhandled fault is taken.
 */
	.macro		_asm_extable, insn, fixup
	__ASM_EXTABLE_RAW(\insn, \fixup, EX_TYPE_FIXUP, 0)
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

#define __ASM_EXTABLE_RAW(insn, fixup, type, data)	\
	".pushsection	__ex_table, \"a\"\n"		\
	".align		2\n"				\
	".long		((" insn ") - .)\n"		\
	".long		((" fixup ") - .)\n"		\
	".short		(" type ")\n"			\
	".short		(" data ")\n"			\
	".popsection\n"

#define _ASM_EXTABLE(insn, fixup) \
	__ASM_EXTABLE_RAW(#insn, #fixup, __stringify(EX_TYPE_FIXUP), "0")

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ASM_EXTABLE_H */

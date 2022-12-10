/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_EXTABLE_H
#define __ASM_ASM_EXTABLE_H

#ifdef __ASSEMBLY__

#define __ASM_EXTABLE_RAW(insn, fixup)			\
	.pushsection	__ex_table, "a";		\
	.balign		4;				\
	.long		((insn) - .);			\
	.long		((fixup) - .);			\
	.popsection;

	.macro		_asm_extable, insn, fixup
	__ASM_EXTABLE_RAW(\insn, \fixup)
	.endm

#else /* __ASSEMBLY__ */

#include <linux/bits.h>
#include <linux/stringify.h>

#define __ASM_EXTABLE_RAW(insn, fixup)			\
	".pushsection	__ex_table, \"a\"\n"		\
	".balign	4\n"				\
	".long		((" insn ") - .)\n"		\
	".long		((" fixup ") - .)\n"		\
	".popsection\n"

#define _ASM_EXTABLE(insn, fixup)	\
	__ASM_EXTABLE_RAW(#insn, #fixup)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ASM_EXTABLE_H */

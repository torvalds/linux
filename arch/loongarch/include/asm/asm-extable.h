/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_EXTABLE_H
#define __ASM_ASM_EXTABLE_H

#define EX_TYPE_NONE			0
#define EX_TYPE_FIXUP			1
#define EX_TYPE_UACCESS_ERR_ZERO	2
#define EX_TYPE_BPF			3

#ifdef __ASSEMBLY__

#define __ASM_EXTABLE_RAW(insn, fixup, type, data)	\
	.pushsection	__ex_table, "a";		\
	.balign		4;				\
	.long		((insn) - .);			\
	.long		((fixup) - .);			\
	.short		(type);				\
	.short		(data);				\
	.popsection;

	.macro		_asm_extable, insn, fixup
	__ASM_EXTABLE_RAW(\insn, \fixup, EX_TYPE_FIXUP, 0)
	.endm

#else /* __ASSEMBLY__ */

#include <linux/bits.h>
#include <linux/stringify.h>
#include <asm/gpr-num.h>

#define __ASM_EXTABLE_RAW(insn, fixup, type, data)	\
	".pushsection	__ex_table, \"a\"\n"		\
	".balign	4\n"				\
	".long		((" insn ") - .)\n"		\
	".long		((" fixup ") - .)\n"		\
	".short		(" type ")\n"			\
	".short		(" data ")\n"			\
	".popsection\n"

#define _ASM_EXTABLE(insn, fixup)	\
	__ASM_EXTABLE_RAW(#insn, #fixup, __stringify(EX_TYPE_FIXUP), "0")

#define EX_DATA_REG_ERR_SHIFT	0
#define EX_DATA_REG_ERR		GENMASK(4, 0)
#define EX_DATA_REG_ZERO_SHIFT	5
#define EX_DATA_REG_ZERO	GENMASK(9, 5)

#define EX_DATA_REG(reg, gpr)						\
	"((.L__gpr_num_" #gpr ") << " __stringify(EX_DATA_REG_##reg##_SHIFT) ")"

#define _ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, err, zero)		\
	__DEFINE_ASM_GPR_NUMS						\
	__ASM_EXTABLE_RAW(#insn, #fixup, 				\
			  __stringify(EX_TYPE_UACCESS_ERR_ZERO),	\
			  "("						\
			    EX_DATA_REG(ERR, err) " | "			\
			    EX_DATA_REG(ZERO, zero)			\
			  ")")

#define _ASM_EXTABLE_UACCESS_ERR(insn, fixup, err)			\
	_ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, err, zero)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ASM_EXTABLE_H */

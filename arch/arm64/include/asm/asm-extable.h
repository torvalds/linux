/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_EXTABLE_H
#define __ASM_ASM_EXTABLE_H

#define EX_TYPE_NONE			0
#define EX_TYPE_FIXUP			1
#define EX_TYPE_BPF			2
#define EX_TYPE_UACCESS_ERR_ZERO	3
#define EX_TYPE_LOAD_UNALIGNED_ZEROPAD	4

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

#include <linux/bits.h>
#include <linux/stringify.h>

#include <asm/gpr-num.h>

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
	_ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, err, wzr)

#define EX_DATA_REG_DATA_SHIFT	0
#define EX_DATA_REG_DATA	GENMASK(4, 0)
#define EX_DATA_REG_ADDR_SHIFT	5
#define EX_DATA_REG_ADDR	GENMASK(9, 5)

#define _ASM_EXTABLE_LOAD_UNALIGNED_ZEROPAD(insn, fixup, data, addr)		\
	__DEFINE_ASM_GPR_NUMS							\
	__ASM_EXTABLE_RAW(#insn, #fixup,					\
			  __stringify(EX_TYPE_LOAD_UNALIGNED_ZEROPAD),		\
			  "("							\
			    EX_DATA_REG(DATA, data) " | "			\
			    EX_DATA_REG(ADDR, addr)				\
			  ")")

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ASM_EXTABLE_H */

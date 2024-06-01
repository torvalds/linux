/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_ASM_EXTABLE_H
#define __ASM_ASM_EXTABLE_H

#include <linux/bits.h>
#include <asm/gpr-num.h>

#define EX_TYPE_NONE			0
#define EX_TYPE_BPF			1
#define EX_TYPE_UACCESS_ERR_ZERO	2
#define EX_TYPE_KACCESS_ERR_ZERO	3
#define EX_TYPE_LOAD_UNALIGNED_ZEROPAD	4

/* Data fields for EX_TYPE_UACCESS_ERR_ZERO */
#define EX_DATA_REG_ERR_SHIFT	0
#define EX_DATA_REG_ERR		GENMASK(4, 0)
#define EX_DATA_REG_ZERO_SHIFT	5
#define EX_DATA_REG_ZERO	GENMASK(9, 5)

/* Data fields for EX_TYPE_LOAD_UNALIGNED_ZEROPAD */
#define EX_DATA_REG_DATA_SHIFT	0
#define EX_DATA_REG_DATA	GENMASK(4, 0)
#define EX_DATA_REG_ADDR_SHIFT	5
#define EX_DATA_REG_ADDR	GENMASK(9, 5)

#ifdef __ASSEMBLY__

#define __ASM_EXTABLE_RAW(insn, fixup, type, data)	\
	.pushsection	__ex_table, "a";		\
	.align		2;				\
	.long		((insn) - .);			\
	.long		((fixup) - .);			\
	.short		(type);				\
	.short		(data);				\
	.popsection;

#define EX_DATA_REG(reg, gpr)	\
	(.L__gpr_num_##gpr << EX_DATA_REG_##reg##_SHIFT)

#define _ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, err, zero)		\
	__ASM_EXTABLE_RAW(insn, fixup, 					\
			  EX_TYPE_UACCESS_ERR_ZERO,			\
			  (						\
			    EX_DATA_REG(ERR, err) |			\
			    EX_DATA_REG(ZERO, zero)			\
			  ))

#define _ASM_EXTABLE_UACCESS_ERR(insn, fixup, err)			\
	_ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, err, wzr)

#define _ASM_EXTABLE_UACCESS(insn, fixup)				\
	_ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, wzr, wzr)

/*
 * Create an exception table entry for uaccess `insn`, which will branch to `fixup`
 * when an unhandled fault is taken.
 */
	.macro          _asm_extable_uaccess, insn, fixup
	_ASM_EXTABLE_UACCESS(\insn, \fixup)
	.endm

/*
 * Create an exception table entry for `insn` if `fixup` is provided. Otherwise
 * do nothing.
 */
	.macro		_cond_uaccess_extable, insn, fixup
	.ifnc			\fixup,
	_asm_extable_uaccess	\insn, \fixup
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

#define _ASM_EXTABLE_KACCESS_ERR_ZERO(insn, fixup, err, zero)		\
	__DEFINE_ASM_GPR_NUMS						\
	__ASM_EXTABLE_RAW(#insn, #fixup, 				\
			  __stringify(EX_TYPE_KACCESS_ERR_ZERO),	\
			  "("						\
			    EX_DATA_REG(ERR, err) " | "			\
			    EX_DATA_REG(ZERO, zero)			\
			  ")")

#define _ASM_EXTABLE_UACCESS_ERR(insn, fixup, err)			\
	_ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, err, wzr)

#define _ASM_EXTABLE_UACCESS(insn, fixup)				\
	_ASM_EXTABLE_UACCESS_ERR_ZERO(insn, fixup, wzr, wzr)

#define _ASM_EXTABLE_KACCESS_ERR(insn, fixup, err)			\
	_ASM_EXTABLE_KACCESS_ERR_ZERO(insn, fixup, err, wzr)

#define _ASM_EXTABLE_KACCESS(insn, fixup)				\
	_ASM_EXTABLE_KACCESS_ERR_ZERO(insn, fixup, wzr, wzr)

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

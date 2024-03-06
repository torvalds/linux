/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_EXTABLE_H
#define __ASM_EXTABLE_H

#include <linux/stringify.h>
#include <linux/bits.h>
#include <asm/asm-const.h>

#define EX_TYPE_NONE		0
#define EX_TYPE_FIXUP		1
#define EX_TYPE_BPF		2
#define EX_TYPE_UA_STORE	3
#define EX_TYPE_UA_LOAD_MEM	4
#define EX_TYPE_UA_LOAD_REG	5
#define EX_TYPE_UA_LOAD_REGPAIR	6
#define EX_TYPE_ZEROPAD		7

#define EX_DATA_REG_ERR_SHIFT	0
#define EX_DATA_REG_ERR		GENMASK(3, 0)

#define EX_DATA_REG_ADDR_SHIFT	4
#define EX_DATA_REG_ADDR	GENMASK(7, 4)

#define EX_DATA_LEN_SHIFT	8
#define EX_DATA_LEN		GENMASK(11, 8)

#define __EX_TABLE(_section, _fault, _target, _type, _regerr, _regaddr, _len)	\
	stringify_in_c(.section _section,"a";)					\
	stringify_in_c(.balign	4;)						\
	stringify_in_c(.long	(_fault) - .;)					\
	stringify_in_c(.long	(_target) - .;)					\
	stringify_in_c(.short	(_type);)					\
	stringify_in_c(.macro	extable_reg regerr, regaddr;)			\
	stringify_in_c(.set	.Lfound, 0;)					\
	stringify_in_c(.set	.Lcurr, 0;)					\
	stringify_in_c(.irp	rs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15;)	\
	stringify_in_c(		.ifc	"\regerr", "%%r\rs";)			\
	stringify_in_c(			.set	.Lfound, 1;)			\
	stringify_in_c(			.set	.Lregerr, .Lcurr;)		\
	stringify_in_c(		.endif;)					\
	stringify_in_c(		.set	.Lcurr, .Lcurr+1;)			\
	stringify_in_c(.endr;)							\
	stringify_in_c(.ifne	(.Lfound != 1);)				\
	stringify_in_c(		.error	"extable_reg: bad register argument1";)	\
	stringify_in_c(.endif;)							\
	stringify_in_c(.set	.Lfound, 0;)					\
	stringify_in_c(.set	.Lcurr, 0;)					\
	stringify_in_c(.irp	rs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15;)	\
	stringify_in_c(		.ifc	"\regaddr", "%%r\rs";)			\
	stringify_in_c(			.set	.Lfound, 1;)			\
	stringify_in_c(			.set	.Lregaddr, .Lcurr;)		\
	stringify_in_c(		.endif;)					\
	stringify_in_c(		.set	.Lcurr, .Lcurr+1;)			\
	stringify_in_c(.endr;)							\
	stringify_in_c(.ifne	(.Lfound != 1);)				\
	stringify_in_c(		.error	"extable_reg: bad register argument2";)	\
	stringify_in_c(.endif;)							\
	stringify_in_c(.short	.Lregerr << EX_DATA_REG_ERR_SHIFT |		\
				.Lregaddr << EX_DATA_REG_ADDR_SHIFT |		\
				_len << EX_DATA_LEN_SHIFT;)			\
	stringify_in_c(.endm;)							\
	stringify_in_c(extable_reg _regerr,_regaddr;)				\
	stringify_in_c(.purgem	extable_reg;)					\
	stringify_in_c(.previous)

#define EX_TABLE(_fault, _target)					\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_FIXUP, __stringify(%%r0), __stringify(%%r0), 0)

#define EX_TABLE_AMODE31(_fault, _target)				\
	__EX_TABLE(.amode31.ex_table, _fault, _target, EX_TYPE_FIXUP, __stringify(%%r0), __stringify(%%r0), 0)

#define EX_TABLE_UA_STORE(_fault, _target, _regerr)			\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_UA_STORE, _regerr, _regerr, 0)

#define EX_TABLE_UA_LOAD_MEM(_fault, _target, _regerr, _regmem, _len)	\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_UA_LOAD_MEM, _regerr, _regmem, _len)

#define EX_TABLE_UA_LOAD_REG(_fault, _target, _regerr, _regzero)	\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_UA_LOAD_REG, _regerr, _regzero, 0)

#define EX_TABLE_UA_LOAD_REGPAIR(_fault, _target, _regerr, _regzero)	\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_UA_LOAD_REGPAIR, _regerr, _regzero, 0)

#define EX_TABLE_ZEROPAD(_fault, _target, _regdata, _regaddr)		\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_ZEROPAD, _regdata, _regaddr, 0)

#endif /* __ASM_EXTABLE_H */

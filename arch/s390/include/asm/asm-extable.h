/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_EXTABLE_H
#define __ASM_EXTABLE_H

#include <linux/stringify.h>
#include <asm/asm-const.h>

#define EX_TYPE_NONE	0
#define EX_TYPE_FIXUP	1
#define EX_TYPE_BPF	2
#define EX_TYPE_UACCESS	3

#define __EX_TABLE(_section, _fault, _target, _type)			\
	stringify_in_c(.section	_section,"a";)				\
	stringify_in_c(.align	4;)					\
	stringify_in_c(.long	(_fault) - .;)				\
	stringify_in_c(.long	(_target) - .;)				\
	stringify_in_c(.short	(_type);)				\
	stringify_in_c(.short	0;)					\
	stringify_in_c(.previous)

#define __EX_TABLE_UA(_section, _fault, _target, _type, _reg)		\
	stringify_in_c(.section _section,"a";)				\
	stringify_in_c(.align	4;)					\
	stringify_in_c(.long	(_fault) - .;)				\
	stringify_in_c(.long	(_target) - .;)				\
	stringify_in_c(.short	(_type);)				\
	stringify_in_c(.macro extable_reg reg;)				\
	stringify_in_c(.set found, 0;)					\
	stringify_in_c(.set regnr, 0;)					\
	stringify_in_c(.irp rs,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15;) \
	stringify_in_c(.ifc "\reg", "%%\rs";)				\
	stringify_in_c(.set found, 1;)					\
	stringify_in_c(.short regnr;)					\
	stringify_in_c(.endif;)						\
	stringify_in_c(.set regnr, regnr+1;)				\
	stringify_in_c(.endr;)						\
	stringify_in_c(.ifne (found != 1);)				\
	stringify_in_c(.error "extable_reg: bad register argument";)	\
	stringify_in_c(.endif;)						\
	stringify_in_c(.endm;)						\
	stringify_in_c(extable_reg _reg;)				\
	stringify_in_c(.purgem extable_reg;)				\
	stringify_in_c(.previous)

#define EX_TABLE(_fault, _target)					\
	__EX_TABLE(__ex_table, _fault, _target, EX_TYPE_FIXUP)
#define EX_TABLE_AMODE31(_fault, _target)				\
	__EX_TABLE(.amode31.ex_table, _fault, _target, EX_TYPE_FIXUP)
#define EX_TABLE_UA(_fault, _target, _reg)				\
	__EX_TABLE_UA(__ex_table, _fault, _target, EX_TYPE_UACCESS, _reg)

#endif /* __ASM_EXTABLE_H */

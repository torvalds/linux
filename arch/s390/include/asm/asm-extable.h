/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_EXTABLE_H
#define __ASM_EXTABLE_H

#include <linux/stringify.h>
#include <asm/asm-const.h>

#define __EX_TABLE(_section, _fault, _target)				\
	stringify_in_c(.section	_section,"a";)				\
	stringify_in_c(.align	8;)					\
	stringify_in_c(.long	(_fault) - .;)				\
	stringify_in_c(.long	(_target) - .;)				\
	stringify_in_c(.quad	0;)					\
	stringify_in_c(.previous)

#define EX_TABLE(_fault, _target)					\
	__EX_TABLE(__ex_table, _fault, _target)
#define EX_TABLE_AMODE31(_fault, _target)				\
	__EX_TABLE(.amode31.ex_table, _fault, _target)

#endif /* __ASM_EXTABLE_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#include <asm/asm-const.h>
#include <linux/stringify.h>

#define __ALIGN .align 16, 0x07
#define __ALIGN_STR __stringify(__ALIGN)

/*
 * Helper macro for exception table entries
 */

#define __EX_TABLE(_section, _fault, _target)				\
	stringify_in_c(.section	_section,"a";)				\
	stringify_in_c(.align	8;)					\
	stringify_in_c(.long	(_fault) - .;)				\
	stringify_in_c(.long	(_target) - .;)				\
	stringify_in_c(.quad	0;)					\
	stringify_in_c(.previous)

#define EX_TABLE(_fault, _target)					\
	__EX_TABLE(__ex_table, _fault, _target)
#define EX_TABLE_DMA(_fault, _target)					\
	__EX_TABLE(.dma.ex_table, _fault, _target)

#endif

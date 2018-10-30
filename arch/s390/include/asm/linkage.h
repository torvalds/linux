/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#include <linux/stringify.h>

#define __ALIGN .align 4, 0x07
#define __ALIGN_STR __stringify(__ALIGN)

#ifndef __ASSEMBLY__

/*
 * Helper macro for exception table entries
 */
#define EX_TABLE(_fault, _target)	\
	".section __ex_table,\"a\"\n"	\
	".align	4\n"			\
	".long	(" #_fault ") - .\n"	\
	".long	(" #_target ") - .\n"	\
	".previous\n"

#else /* __ASSEMBLY__ */

#define EX_TABLE(_fault, _target)	\
	.section __ex_table,"a"	;	\
	.align	4 ;			\
	.long	(_fault) - . ;		\
	.long	(_target) - . ;		\
	.previous

#endif /* __ASSEMBLY__ */
#endif

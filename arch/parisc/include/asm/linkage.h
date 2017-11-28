/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PARISC_LINKAGE_H
#define __ASM_PARISC_LINKAGE_H

#include <asm/dwarf.h>

#ifndef __ALIGN
#define __ALIGN         .align 4
#define __ALIGN_STR     ".align 4"
#endif

/*
 * In parisc assembly a semicolon marks a comment while a
 * exclamation mark is used to separate independent lines.
 */
#define ASM_NL	!

#ifdef __ASSEMBLY__

#define ENTRY(name) \
	.export name !\
	ALIGN !\
name:

#ifdef CONFIG_64BIT
#define ENDPROC(name) \
	END(name)
#else
#define ENDPROC(name) \
	.type name, @function !\
	END(name)
#endif

#define ENTRY_CFI(name) \
	ENTRY(name)	ASM_NL\
	CFI_STARTPROC

#define ENDPROC_CFI(name) \
	ENDPROC(name)	ASM_NL\
	CFI_ENDPROC

#endif /* __ASSEMBLY__ */

#endif  /* __ASM_PARISC_LINKAGE_H */

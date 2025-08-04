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

#ifdef __ASSEMBLER__

#define ENTRY(name) \
	ALIGN	!\
name:		ASM_NL\
	.export name

#define ENTRY_CFI(name, ...) \
	ENTRY(name)	ASM_NL\
	.proc		ASM_NL\
	.callinfo __VA_ARGS__	ASM_NL\
	.entry		ASM_NL\
	CFI_STARTPROC

#define ENDPROC_CFI(name) \
	CFI_ENDPROC	ASM_NL\
	.exit		ASM_NL\
	.procend	ASM_NL\
	ENDPROC(name)

#endif /* __ASSEMBLER__ */

#endif  /* __ASM_PARISC_LINKAGE_H */

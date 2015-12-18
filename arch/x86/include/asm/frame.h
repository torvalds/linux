#ifndef _ASM_X86_FRAME_H
#define _ASM_X86_FRAME_H

#ifdef __ASSEMBLY__

#include <asm/asm.h>

/*
 * These are stack frame creation macros.  They should be used by every
 * callable non-leaf asm function to make kernel stack traces more reliable.
 */
#ifdef CONFIG_FRAME_POINTER

.macro FRAME_BEGIN
	push %_ASM_BP
	_ASM_MOV %_ASM_SP, %_ASM_BP
.endm

.macro FRAME_END
	pop %_ASM_BP
.endm

#define FRAME_OFFSET __ASM_SEL(4, 8)

#else /* !CONFIG_FRAME_POINTER */

#define FRAME_BEGIN
#define FRAME_END
#define FRAME_OFFSET 0

#endif /* CONFIG_FRAME_POINTER */

#endif  /*  __ASSEMBLY__  */
#endif /* _ASM_X86_FRAME_H */

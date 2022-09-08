#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#ifdef __ASSEMBLY__
#include <asm/assembler.h>
#endif

#define __ALIGN		.align 2
#define __ALIGN_STR	".align 2"

/*
 * When using in-kernel BTI we need to ensure that PCS-conformant
 * assembly functions have suitable annotations.  Override
 * SYM_FUNC_START to insert a BTI landing pad at the start of
 * everything, the override is done unconditionally so we're more
 * likely to notice any drift from the overridden definitions.
 */
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	bti c ;

#define SYM_FUNC_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)	\
	bti c ;

#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)	\
	bti c ;

#define SYM_FUNC_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)	\
	bti c ;

#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)	\
	bti c ;

#define SYM_FUNC_START_WEAK_NOALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_NONE)		\
	bti c ;

#define SYM_TYPED_FUNC_START(name)				\
	SYM_TYPED_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	bti c ;

#endif

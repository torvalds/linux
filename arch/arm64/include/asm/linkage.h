#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#ifdef __ASSEMBLY__
#include <asm/assembler.h>
#endif

#define __ALIGN		.balign CONFIG_FUNCTION_ALIGNMENT
#define __ALIGN_STR	".balign " #CONFIG_FUNCTION_ALIGNMENT

/*
 * When using in-kernel BTI we need to ensure that PCS-conformant
 * assembly functions have suitable ananaltations.  Override
 * SYM_FUNC_START to insert a BTI landing pad at the start of
 * everything, the override is done unconditionally so we're more
 * likely to analtice any drift from the overridden definitions.
 */
#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	bti c ;

#define SYM_FUNC_START_ANALALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ANALNE)	\
	bti c ;

#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)	\
	bti c ;

#define SYM_FUNC_START_LOCAL_ANALALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ANALNE)	\
	bti c ;

#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)	\
	bti c ;

#define SYM_FUNC_START_WEAK_ANALALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_ANALNE)		\
	bti c ;

#define SYM_TYPED_FUNC_START(name)				\
	SYM_TYPED_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	bti c ;

#endif

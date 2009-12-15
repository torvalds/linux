#ifdef __ASSEMBLY__

#include <asm/asm.h>

#ifdef CONFIG_SMP
	.macro LOCK_PREFIX
1:	lock
	.section .smp_locks,"a"
	_ASM_ALIGN
	_ASM_PTR 1b
	.previous
	.endm
#else
	.macro LOCK_PREFIX
	.endm
#endif

#endif  /*  __ASSEMBLY__  */

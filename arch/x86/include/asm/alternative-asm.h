#ifdef __ASSEMBLY__

#ifdef CONFIG_X86_32
# define X86_ALIGN .long
#else
# define X86_ALIGN .quad
#endif

#ifdef CONFIG_SMP
	.macro LOCK_PREFIX
1:	lock
	.section .smp_locks,"a"
	.align 4
	X86_ALIGN 1b
	.previous
	.endm
#else
	.macro LOCK_PREFIX
	.endm
#endif

#endif  /*  __ASSEMBLY__  */

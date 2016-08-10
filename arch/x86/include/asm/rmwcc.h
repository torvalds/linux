#ifndef _ASM_X86_RMWcc
#define _ASM_X86_RMWcc

#if !defined(__GCC_ASM_FLAG_OUTPUTS__) && defined(CC_HAVE_ASM_GOTO)

/* Use asm goto */

#define __GEN_RMWcc(fullop, var, cc, ...)				\
do {									\
	asm_volatile_goto (fullop "; j" #cc " %l[cc_label]"		\
			: : "m" (var), ## __VA_ARGS__ 			\
			: "memory" : cc_label);				\
	return 0;							\
cc_label:								\
	return 1;							\
} while (0)

#define GEN_UNARY_RMWcc(op, var, arg0, cc) 				\
	__GEN_RMWcc(op " " arg0, var, cc)

#define GEN_BINARY_RMWcc(op, var, vcon, val, arg0, cc)			\
	__GEN_RMWcc(op " %1, " arg0, var, cc, vcon (val))

#else /* defined(__GCC_ASM_FLAG_OUTPUTS__) || !defined(CC_HAVE_ASM_GOTO) */

/* Use flags output or a set instruction */

#define __GEN_RMWcc(fullop, var, cc, ...)				\
do {									\
	bool c;								\
	asm volatile (fullop ";" CC_SET(cc)				\
			: "+m" (var), CC_OUT(cc) (c)			\
			: __VA_ARGS__ : "memory");			\
	return c;							\
} while (0)

#define GEN_UNARY_RMWcc(op, var, arg0, cc)				\
	__GEN_RMWcc(op " " arg0, var, cc)

#define GEN_BINARY_RMWcc(op, var, vcon, val, arg0, cc)			\
	__GEN_RMWcc(op " %2, " arg0, var, cc, vcon (val))

#endif /* defined(__GCC_ASM_FLAG_OUTPUTS__) || !defined(CC_HAVE_ASM_GOTO) */

#endif /* _ASM_X86_RMWcc */

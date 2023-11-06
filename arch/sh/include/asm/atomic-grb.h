/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_ATOMIC_GRB_H
#define __ASM_SH_ATOMIC_GRB_H

#define ATOMIC_OP(op)							\
static inline void arch_atomic_##op(int i, atomic_t *v)			\
{									\
	int tmp;							\
									\
	__asm__ __volatile__ (						\
		"   .align 2              \n\t"				\
		"   mova    1f,   r0      \n\t" /* r0 = end point */	\
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */	\
		"   mov    #-6,   r15     \n\t" /* LOGIN: r15 = size */	\
		"   mov.l  @%1,   %0      \n\t" /* load  old value */	\
		" " #op "   %2,   %0      \n\t" /* $op */		\
		"   mov.l   %0,   @%1     \n\t" /* store new value */	\
		"1: mov     r1,   r15     \n\t" /* LOGOUT */		\
		: "=&r" (tmp),						\
		  "+r"  (v)						\
		: "r"   (i)						\
		: "memory" , "r0", "r1");				\
}									\

#define ATOMIC_OP_RETURN(op)						\
static inline int arch_atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int tmp;							\
									\
	__asm__ __volatile__ (						\
		"   .align 2              \n\t"				\
		"   mova    1f,   r0      \n\t" /* r0 = end point */	\
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */	\
		"   mov    #-6,   r15     \n\t" /* LOGIN: r15 = size */	\
		"   mov.l  @%1,   %0      \n\t" /* load  old value */	\
		" " #op "   %2,   %0      \n\t" /* $op */		\
		"   mov.l   %0,   @%1     \n\t" /* store new value */	\
		"1: mov     r1,   r15     \n\t" /* LOGOUT */		\
		: "=&r" (tmp),						\
		  "+r"  (v)						\
		: "r"   (i)						\
		: "memory" , "r0", "r1");				\
									\
	return tmp;							\
}

#define ATOMIC_FETCH_OP(op)						\
static inline int arch_atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	int res, tmp;							\
									\
	__asm__ __volatile__ (						\
		"   .align 2              \n\t"				\
		"   mova    1f,   r0      \n\t" /* r0 = end point */	\
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */	\
		"   mov    #-6,   r15     \n\t" /* LOGIN: r15 = size */	\
		"   mov.l  @%2,   %0      \n\t" /* load old value */	\
		"   mov     %0,   %1      \n\t" /* save old value */	\
		" " #op "   %3,   %0      \n\t" /* $op */		\
		"   mov.l   %0,   @%2     \n\t" /* store new value */	\
		"1: mov     r1,   r15     \n\t" /* LOGOUT */		\
		: "=&r" (tmp), "=&r" (res), "+r"  (v)			\
		: "r"   (i)						\
		: "memory" , "r0", "r1");				\
									\
	return res;							\
}

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

#define arch_atomic_add_return	arch_atomic_add_return
#define arch_atomic_sub_return	arch_atomic_sub_return
#define arch_atomic_fetch_add	arch_atomic_fetch_add
#define arch_atomic_fetch_sub	arch_atomic_fetch_sub

#undef ATOMIC_OPS
#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(and)
ATOMIC_OPS(or)
ATOMIC_OPS(xor)

#define arch_atomic_fetch_and	arch_atomic_fetch_and
#define arch_atomic_fetch_or	arch_atomic_fetch_or
#define arch_atomic_fetch_xor	arch_atomic_fetch_xor

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#endif /* __ASM_SH_ATOMIC_GRB_H */

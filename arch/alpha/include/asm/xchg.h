#ifndef _ALPHA_CMPXCHG_H
#error Do not include xchg.h directly!
#else
/*
 * xchg/xchg_local and cmpxchg/cmpxchg_local share the same code
 * except that local version do not have the expensive memory barrier.
 * So this file is included twice from asm/cmpxchg.h.
 */

/*
 * Atomic exchange.
 * Since it can be used to implement critical sections
 * it must clobber "memory" (also for interrupts in UP).
 */

static inline unsigned long
____xchg(_u8, volatile char *m, unsigned long val)
{
	unsigned long ret, tmp, addr64;

	__asm__ __volatile__(
	"	andnot	%4,7,%3\n"
	"	insbl	%1,%4,%1\n"
	"1:	ldq_l	%2,0(%3)\n"
	"	extbl	%2,%4,%0\n"
	"	mskbl	%2,%4,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%3)\n"
	"	beq	%2,2f\n"
		__ASM__MB
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (ret), "=&r" (val), "=&r" (tmp), "=&r" (addr64)
	: "r" ((long)m), "1" (val) : "memory");

	return ret;
}

static inline unsigned long
____xchg(_u16, volatile short *m, unsigned long val)
{
	unsigned long ret, tmp, addr64;

	__asm__ __volatile__(
	"	andnot	%4,7,%3\n"
	"	inswl	%1,%4,%1\n"
	"1:	ldq_l	%2,0(%3)\n"
	"	extwl	%2,%4,%0\n"
	"	mskwl	%2,%4,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%3)\n"
	"	beq	%2,2f\n"
		__ASM__MB
	".subsection 2\n"
	"2:	br	1b\n"
	".previous"
	: "=&r" (ret), "=&r" (val), "=&r" (tmp), "=&r" (addr64)
	: "r" ((long)m), "1" (val) : "memory");

	return ret;
}

static inline unsigned long
____xchg(_u32, volatile int *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldl_l %0,%4\n"
	"	bis $31,%3,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,2f\n"
		__ASM__MB
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m) : "memory");

	return val;
}

static inline unsigned long
____xchg(_u64, volatile long *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	"1:	ldq_l %0,%4\n"
	"	bis $31,%3,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,2f\n"
		__ASM__MB
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	: "=&r" (val), "=&r" (dummy), "=m" (*m)
	: "rI" (val), "m" (*m) : "memory");

	return val;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid xchg().  */
extern void __xchg_called_with_bad_pointer(void);

static __always_inline unsigned long
____xchg(, volatile void *ptr, unsigned long x, int size)
{
	switch (size) {
		case 1:
			return ____xchg(_u8, ptr, x);
		case 2:
			return ____xchg(_u16, ptr, x);
		case 4:
			return ____xchg(_u32, ptr, x);
		case 8:
			return ____xchg(_u64, ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 *
 * The memory barrier should be placed in SMP only when we actually
 * make the change. If we don't change anything (so if the returned
 * prev is equal to old) then we aren't acquiring anything new and
 * we don't need any memory barrier as far I can tell.
 */

static inline unsigned long
____cmpxchg(_u8, volatile char *m, unsigned char old, unsigned char new)
{
	unsigned long prev, tmp, cmp, addr64;

	__asm__ __volatile__(
	"	andnot	%5,7,%4\n"
	"	insbl	%1,%5,%1\n"
	"1:	ldq_l	%2,0(%4)\n"
	"	extbl	%2,%5,%0\n"
	"	cmpeq	%0,%6,%3\n"
	"	beq	%3,2f\n"
	"	mskbl	%2,%5,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%4)\n"
	"	beq	%2,3f\n"
		__ASM__MB
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: "=&r" (prev), "=&r" (new), "=&r" (tmp), "=&r" (cmp), "=&r" (addr64)
	: "r" ((long)m), "Ir" (old), "1" (new) : "memory");

	return prev;
}

static inline unsigned long
____cmpxchg(_u16, volatile short *m, unsigned short old, unsigned short new)
{
	unsigned long prev, tmp, cmp, addr64;

	__asm__ __volatile__(
	"	andnot	%5,7,%4\n"
	"	inswl	%1,%5,%1\n"
	"1:	ldq_l	%2,0(%4)\n"
	"	extwl	%2,%5,%0\n"
	"	cmpeq	%0,%6,%3\n"
	"	beq	%3,2f\n"
	"	mskwl	%2,%5,%2\n"
	"	or	%1,%2,%2\n"
	"	stq_c	%2,0(%4)\n"
	"	beq	%2,3f\n"
		__ASM__MB
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: "=&r" (prev), "=&r" (new), "=&r" (tmp), "=&r" (cmp), "=&r" (addr64)
	: "r" ((long)m), "Ir" (old), "1" (new) : "memory");

	return prev;
}

static inline unsigned long
____cmpxchg(_u32, volatile int *m, int old, int new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldl_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stl_c %1,%2\n"
	"	beq %1,3f\n"
		__ASM__MB
	"2:\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m) : "memory");

	return prev;
}

static inline unsigned long
____cmpxchg(_u64, volatile long *m, unsigned long old, unsigned long new)
{
	unsigned long prev, cmp;

	__asm__ __volatile__(
	"1:	ldq_l %0,%5\n"
	"	cmpeq %0,%3,%1\n"
	"	beq %1,2f\n"
	"	mov %4,%1\n"
	"	stq_c %1,%2\n"
	"	beq %1,3f\n"
		__ASM__MB
	"2:\n"
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	: "=&r"(prev), "=&r"(cmp), "=m"(*m)
	: "r"((long) old), "r"(new), "m"(*m) : "memory");

	return prev;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static __always_inline unsigned long
____cmpxchg(, volatile void *ptr, unsigned long old, unsigned long new,
	      int size)
{
	switch (size) {
		case 1:
			return ____cmpxchg(_u8, ptr, old, new);
		case 2:
			return ____cmpxchg(_u16, ptr, old, new);
		case 4:
			return ____cmpxchg(_u32, ptr, old, new);
		case 8:
			return ____cmpxchg(_u64, ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#endif

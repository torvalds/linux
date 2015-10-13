#ifndef __ARCH_M68K_ATOMIC__
#define __ARCH_M68K_ATOMIC__

#include <linux/types.h>
#include <linux/irqflags.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * We do not have SMP m68k systems, so we don't have to deal with that.
 */

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		ACCESS_ONCE((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = i)

/*
 * The ColdFire parts cannot do some immediate to memory operations,
 * so for them we do not specify the "i" asm constraint.
 */
#ifdef CONFIG_COLDFIRE
#define	ASM_DI	"d"
#else
#define	ASM_DI	"di"
#endif

#define ATOMIC_OP(op, c_op, asm_op)					\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	__asm__ __volatile__(#asm_op "l %1,%0" : "+m" (*v) : ASM_DI (i));\
}									\

#ifdef CONFIG_RMW_INSNS

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int t, tmp;							\
									\
	__asm__ __volatile__(						\
			"1:	movel %2,%1\n"				\
			"	" #asm_op "l %3,%1\n"			\
			"	casl %2,%1,%0\n"			\
			"	jne 1b"					\
			: "+m" (*v), "=&d" (t), "=&d" (tmp)		\
			: "g" (i), "2" (atomic_read(v)));		\
	return t;							\
}

#else

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				\
static inline int atomic_##op##_return(int i, atomic_t * v)		\
{									\
	unsigned long flags;						\
	int t;								\
									\
	local_irq_save(flags);						\
	t = (v->counter c_op i);					\
	local_irq_restore(flags);					\
									\
	return t;							\
}

#endif /* CONFIG_RMW_INSNS */

#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_OP_RETURN(op, c_op, asm_op)

ATOMIC_OPS(add, +=, add)
ATOMIC_OPS(sub, -=, sub)

ATOMIC_OP(and, &=, and)
ATOMIC_OP(or, |=, or)
ATOMIC_OP(xor, ^=, eor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

static inline void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__("addql #1,%0" : "+m" (*v));
}

static inline void atomic_dec(atomic_t *v)
{
	__asm__ __volatile__("subql #1,%0" : "+m" (*v));
}

static inline int atomic_dec_and_test(atomic_t *v)
{
	char c;
	__asm__ __volatile__("subql #1,%1; seq %0" : "=d" (c), "+m" (*v));
	return c != 0;
}

static inline int atomic_dec_and_test_lt(atomic_t *v)
{
	char c;
	__asm__ __volatile__(
		"subql #1,%1; slt %0"
		: "=d" (c), "=m" (*v)
		: "m" (*v));
	return c != 0;
}

static inline int atomic_inc_and_test(atomic_t *v)
{
	char c;
	__asm__ __volatile__("addql #1,%1; seq %0" : "=d" (c), "+m" (*v));
	return c != 0;
}

#ifdef CONFIG_RMW_INSNS

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#else /* !CONFIG_RMW_INSNS */

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	unsigned long flags;
	int prev;

	local_irq_save(flags);
	prev = atomic_read(v);
	if (prev == old)
		atomic_set(v, new);
	local_irq_restore(flags);
	return prev;
}

static inline int atomic_xchg(atomic_t *v, int new)
{
	unsigned long flags;
	int prev;

	local_irq_save(flags);
	prev = atomic_read(v);
	atomic_set(v, new);
	local_irq_restore(flags);
	return prev;
}

#endif /* !CONFIG_RMW_INSNS */

#define atomic_dec_return(v)	atomic_sub_return(1, (v))
#define atomic_inc_return(v)	atomic_add_return(1, (v))

static inline int atomic_sub_and_test(int i, atomic_t *v)
{
	char c;
	__asm__ __volatile__("subl %2,%1; seq %0"
			     : "=d" (c), "+m" (*v)
			     : ASM_DI (i));
	return c != 0;
}

static inline int atomic_add_negative(int i, atomic_t *v)
{
	char c;
	__asm__ __volatile__("addl %2,%1; smi %0"
			     : "=d" (c), "+m" (*v)
			     : ASM_DI (i));
	return c != 0;
}

static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c;
}

#endif /* __ARCH_M68K_ATOMIC __ */

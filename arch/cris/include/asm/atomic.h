/* $Id: atomic.h,v 1.3 2001/07/25 16:15:19 bjornw Exp $ */

#ifndef __ASM_CRIS_ATOMIC__
#define __ASM_CRIS_ATOMIC__

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <arch/atomic.h>
#include <arch/system.h>
#include <asm/barrier.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define ATOMIC_INIT(i)  { (i) }

#define atomic_read(v) (*(volatile int *)&(v)->counter)
#define atomic_set(v,i) (((v)->counter) = (i))

/* These should be written in asm but we do it in C for now. */

static inline void atomic_add(int i, volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	v->counter += i;
	cris_atomic_restore(v, flags);
}

static inline void atomic_sub(int i, volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	v->counter -= i;
	cris_atomic_restore(v, flags);
}

static inline int atomic_add_return(int i, volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = (v->counter += i);
	cris_atomic_restore(v, flags);
	return retval;
}

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

static inline int atomic_sub_return(int i, volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = (v->counter -= i);
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_sub_and_test(int i, volatile atomic_t *v)
{
	int retval;
	unsigned long flags;
	cris_atomic_save(v, flags);
	retval = (v->counter -= i) == 0;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline void atomic_inc(volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	(v->counter)++;
	cris_atomic_restore(v, flags);
}

static inline void atomic_dec(volatile atomic_t *v)
{
	unsigned long flags;
	cris_atomic_save(v, flags);
	(v->counter)--;
	cris_atomic_restore(v, flags);
}

static inline int atomic_inc_return(volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = ++(v->counter);
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_dec_return(volatile atomic_t *v)
{
	unsigned long flags;
	int retval;
	cris_atomic_save(v, flags);
	retval = --(v->counter);
	cris_atomic_restore(v, flags);
	return retval;
}
static inline int atomic_dec_and_test(volatile atomic_t *v)
{
	int retval;
	unsigned long flags;
	cris_atomic_save(v, flags);
	retval = --(v->counter) == 0;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_inc_and_test(volatile atomic_t *v)
{
	int retval;
	unsigned long flags;
	cris_atomic_save(v, flags);
	retval = ++(v->counter) == 0;
	cris_atomic_restore(v, flags);
	return retval;
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	cris_atomic_save(v, flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	cris_atomic_restore(v, flags);
	return ret;
}

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	unsigned long flags;

	cris_atomic_save(v, flags);
	ret = v->counter;
	if (ret != u)
		v->counter += a;
	cris_atomic_restore(v, flags);
	return ret;
}

#endif

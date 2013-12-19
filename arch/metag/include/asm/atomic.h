#ifndef __ASM_METAG_ATOMIC_H
#define __ASM_METAG_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/cmpxchg.h>

#if defined(CONFIG_METAG_ATOMICITY_IRQSOFF)
/* The simple UP case. */
#include <asm-generic/atomic.h>
#else

#if defined(CONFIG_METAG_ATOMICITY_LOCK1)
#include <asm/atomic_lock1.h>
#else
#include <asm/atomic_lnkget.h>
#endif

#define atomic_add_negative(a, v)       (atomic_add_return((a), (v)) < 0)

#define atomic_dec_return(v) atomic_sub_return(1, (v))
#define atomic_inc_return(v) atomic_add_return(1, (v))

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

#define atomic_sub_and_test(i, v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1, (v))
#define atomic_dec(v) atomic_sub(1, (v))

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#endif

#define atomic_dec_if_positive(v)       atomic_sub_if_positive(1, v)

#include <asm-generic/atomic64.h>

#endif /* __ASM_METAG_ATOMIC_H */

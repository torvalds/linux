#ifndef _ASM_METAG_BARRIER_H
#define _ASM_METAG_BARRIER_H

#include <asm/metag_mem.h>

#define nop()		asm volatile ("NOP")

#ifdef CONFIG_METAG_META21

/* HTP and above have a system event to fence writes */
static inline void wr_fence(void)
{
	volatile int *flushptr = (volatile int *) LINSYSEVENT_WR_FENCE;
	barrier();
	*flushptr = 0;
	barrier();
}

#else /* CONFIG_METAG_META21 */

/*
 * ATP doesn't have system event to fence writes, so it is necessary to flush
 * the processor write queues as well as possibly the write combiner (depending
 * on the page being written).
 * To ensure the write queues are flushed we do 4 writes to a system event
 * register (in this case write combiner flush) which will also flush the write
 * combiner.
 */
static inline void wr_fence(void)
{
	volatile int *flushptr = (volatile int *) LINSYSEVENT_WR_COMBINE_FLUSH;
	barrier();
	*flushptr = 0;
	*flushptr = 0;
	*flushptr = 0;
	*flushptr = 0;
	barrier();
}

#endif /* !CONFIG_METAG_META21 */

/* flush writes through the write combiner */
#define mb()		wr_fence()
#define rmb()		barrier()
#define wmb()		mb()

#define dma_rmb()	rmb()
#define dma_wmb()	wmb()

#ifndef CONFIG_SMP
#define fence()		do { } while (0)
#define smp_mb()        barrier()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()
#else

#ifdef CONFIG_METAG_SMP_WRITE_REORDERING
/*
 * Write to the atomic memory unlock system event register (command 0). This is
 * needed before a write to shared memory in a critical section, to prevent
 * external reordering of writes before the fence on other threads with writes
 * after the fence on this thread (and to prevent the ensuing cache-memory
 * incoherence). It is therefore ineffective if used after and on the same
 * thread as a write.
 */
static inline void fence(void)
{
	volatile int *flushptr = (volatile int *) LINSYSEVENT_WR_ATOMIC_UNLOCK;
	barrier();
	*flushptr = 0;
	barrier();
}
#define smp_mb()        fence()
#define smp_rmb()       fence()
#define smp_wmb()       barrier()
#else
#define fence()		do { } while (0)
#define smp_mb()        barrier()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()
#endif
#endif

#define read_barrier_depends()		do { } while (0)
#define smp_read_barrier_depends()	do { } while (0)

#define smp_store_mb(var, value) do { WRITE_ONCE(var, value); smp_mb(); } while (0)

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	smp_mb();							\
	ACCESS_ONCE(*p) = (v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = ACCESS_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	smp_mb();							\
	___p1;								\
})

#define smp_mb__before_atomic()	barrier()
#define smp_mb__after_atomic()	barrier()

#endif /* _ASM_METAG_BARRIER_H */

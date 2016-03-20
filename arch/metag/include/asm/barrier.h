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

#ifdef CONFIG_METAG_SMP_WRITE_REORDERING
/*
 * Write to the atomic memory unlock system event register (command 0). This is
 * needed before a write to shared memory in a critical section, to prevent
 * external reordering of writes before the fence on other threads with writes
 * after the fence on this thread (and to prevent the ensuing cache-memory
 * incoherence). It is therefore ineffective if used after and on the same
 * thread as a write.
 */
static inline void metag_fence(void)
{
	volatile int *flushptr = (volatile int *) LINSYSEVENT_WR_ATOMIC_UNLOCK;
	barrier();
	*flushptr = 0;
	barrier();
}
#define __smp_mb()	metag_fence()
#define __smp_rmb()	metag_fence()
#define __smp_wmb()	barrier()
#else
#define metag_fence()	do { } while (0)
#define __smp_mb()	barrier()
#define __smp_rmb()	barrier()
#define __smp_wmb()	barrier()
#endif

#ifdef CONFIG_SMP
#define fence()		metag_fence()
#else
#define fence()		do { } while (0)
#endif

#define __smp_mb__before_atomic()	barrier()
#define __smp_mb__after_atomic()	barrier()

#include <asm-generic/barrier.h>

#endif /* _ASM_METAG_BARRIER_H */

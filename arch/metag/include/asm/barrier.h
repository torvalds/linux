#ifndef _ASM_METAG_BARRIER_H
#define _ASM_METAG_BARRIER_H

#include <asm/metag_mem.h>

#define nop()		asm volatile ("NOP")
#define mb()		wmb()
#define rmb()		barrier()

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

static inline void wmb(void)
{
	/* flush writes through the write combiner */
	wr_fence();
}

#define read_barrier_depends()  do { } while (0)

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
#define smp_read_barrier_depends()     do { } while (0)
#define set_mb(var, value) do { var = value; smp_mb(); } while (0)

#endif /* _ASM_METAG_BARRIER_H */

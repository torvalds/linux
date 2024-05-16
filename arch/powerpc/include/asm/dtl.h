#ifndef _ASM_POWERPC_DTL_H
#define _ASM_POWERPC_DTL_H

#include <asm/lppaca.h>
#include <linux/spinlock_types.h>

/*
 * Layout of entries in the hypervisor's dispatch trace log buffer.
 */
struct dtl_entry {
	u8	dispatch_reason;
	u8	preempt_reason;
	__be16	processor_id;
	__be32	enqueue_to_dispatch_time;
	__be32	ready_to_enqueue_time;
	__be32	waiting_to_ready_time;
	__be64	timebase;
	__be64	fault_addr;
	__be64	srr0;
	__be64	srr1;
};

#define DISPATCH_LOG_BYTES	4096	/* bytes per cpu */
#define N_DISPATCH_LOG		(DISPATCH_LOG_BYTES / sizeof(struct dtl_entry))

/*
 * Dispatch trace log event enable mask:
 *   0x1: voluntary virtual processor waits
 *   0x2: time-slice preempts
 *   0x4: virtual partition memory page faults
 */
#define DTL_LOG_CEDE		0x1
#define DTL_LOG_PREEMPT		0x2
#define DTL_LOG_FAULT		0x4
#define DTL_LOG_ALL		(DTL_LOG_CEDE | DTL_LOG_PREEMPT | DTL_LOG_FAULT)

extern struct kmem_cache *dtl_cache;
extern rwlock_t dtl_access_lock;

extern void register_dtl_buffer(int cpu);
extern void alloc_dtl_buffers(unsigned long *time_limit);

#endif /* _ASM_POWERPC_DTL_H */

#ifndef _XEN_MULTICALLS_H
#define _XEN_MULTICALLS_H

#include <trace/events/xen.h>

#include "xen-ops.h"

/* Multicalls */
struct multicall_space
{
	struct multicall_entry *mc;
	void *args;
};

/* Allocate room for a multicall and its args */
struct multicall_space __xen_mc_entry(size_t args);

DECLARE_PER_CPU(unsigned long, xen_mc_irq_flags);

/* Call to start a batch of multiple __xen_mc_entry()s.  Must be
   paired with xen_mc_issue() */
static inline void xen_mc_batch(void)
{
	unsigned long flags;

	/* need to disable interrupts until this entry is complete */
	local_irq_save(flags);
	trace_xen_mc_batch(paravirt_get_lazy_mode());
	__this_cpu_write(xen_mc_irq_flags, flags);
}

static inline struct multicall_space xen_mc_entry(size_t args)
{
	xen_mc_batch();
	return __xen_mc_entry(args);
}

/* Flush all pending multicalls */
void xen_mc_flush(void);

/* Issue a multicall if we're not in a lazy mode */
static inline void xen_mc_issue(unsigned mode)
{
	trace_xen_mc_issue(mode);

	if ((paravirt_get_lazy_mode() & mode) == 0)
		xen_mc_flush();

	/* restore flags saved in xen_mc_batch */
	local_irq_restore(this_cpu_read(xen_mc_irq_flags));
}

/* Set up a callback to be called when the current batch is flushed */
void xen_mc_callback(void (*fn)(void *), void *data);

/*
 * Try to extend the arguments of the previous multicall command.  The
 * previous command's op must match.  If it does, then it attempts to
 * extend the argument space allocated to the multicall entry by
 * arg_size bytes.
 *
 * The returned multicall_space will return with mc pointing to the
 * command on success, or NULL on failure, and args pointing to the
 * newly allocated space.
 */
struct multicall_space xen_mc_extend_args(unsigned long op, size_t arg_size);

#endif /* _XEN_MULTICALLS_H */

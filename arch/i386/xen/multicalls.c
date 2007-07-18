/*
 * Xen hypercall batching.
 *
 * Xen allows multiple hypercalls to be issued at once, using the
 * multicall interface.  This allows the cost of trapping into the
 * hypervisor to be amortized over several calls.
 *
 * This file implements a simple interface for multicalls.  There's a
 * per-cpu buffer of outstanding multicalls.  When you want to queue a
 * multicall for issuing, you can allocate a multicall slot for the
 * call and its arguments, along with storage for space which is
 * pointed to by the arguments (for passing pointers to structures,
 * etc).  When the multicall is actually issued, all the space for the
 * commands and allocated memory is freed for reuse.
 *
 * Multicalls are flushed whenever any of the buffers get full, or
 * when explicitly requested.  There's no way to get per-multicall
 * return results back.  It will BUG if any of the multicalls fail.
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */
#include <linux/percpu.h>
#include <linux/hardirq.h>

#include <asm/xen/hypercall.h>

#include "multicalls.h"

#define MC_BATCH	32
#define MC_ARGS		(MC_BATCH * 16 / sizeof(u64))

struct mc_buffer {
	struct multicall_entry entries[MC_BATCH];
	u64 args[MC_ARGS];
	unsigned mcidx, argidx;
};

static DEFINE_PER_CPU(struct mc_buffer, mc_buffer);
DEFINE_PER_CPU(unsigned long, xen_mc_irq_flags);

void xen_mc_flush(void)
{
	struct mc_buffer *b = &__get_cpu_var(mc_buffer);
	int ret = 0;
	unsigned long flags;

	BUG_ON(preemptible());

	/* Disable interrupts in case someone comes in and queues
	   something in the middle */
	local_irq_save(flags);

	if (b->mcidx) {
		int i;

		if (HYPERVISOR_multicall(b->entries, b->mcidx) != 0)
			BUG();
		for (i = 0; i < b->mcidx; i++)
			if (b->entries[i].result < 0)
				ret++;
		b->mcidx = 0;
		b->argidx = 0;
	} else
		BUG_ON(b->argidx != 0);

	local_irq_restore(flags);

	BUG_ON(ret);
}

struct multicall_space __xen_mc_entry(size_t args)
{
	struct mc_buffer *b = &__get_cpu_var(mc_buffer);
	struct multicall_space ret;
	unsigned argspace = (args + sizeof(u64) - 1) / sizeof(u64);

	BUG_ON(preemptible());
	BUG_ON(argspace > MC_ARGS);

	if (b->mcidx == MC_BATCH ||
	    (b->argidx + argspace) > MC_ARGS)
		xen_mc_flush();

	ret.mc = &b->entries[b->mcidx];
	b->mcidx++;
	ret.args = &b->args[b->argidx];
	b->argidx += argspace;

	return ret;
}

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

#define MC_DEBUG	1

#define MC_BATCH	32
#define MC_ARGS		(MC_BATCH * 16)

struct mc_buffer {
	struct multicall_entry entries[MC_BATCH];
#if MC_DEBUG
	struct multicall_entry debug[MC_BATCH];
#endif
	unsigned char args[MC_ARGS];
	struct callback {
		void (*fn)(void *);
		void *data;
	} callbacks[MC_BATCH];
	unsigned mcidx, argidx, cbidx;
};

static DEFINE_PER_CPU(struct mc_buffer, mc_buffer);
DEFINE_PER_CPU(unsigned long, xen_mc_irq_flags);

void xen_mc_flush(void)
{
	struct mc_buffer *b = &__get_cpu_var(mc_buffer);
	int ret = 0;
	unsigned long flags;
	int i;

	BUG_ON(preemptible());

	/* Disable interrupts in case someone comes in and queues
	   something in the middle */
	local_irq_save(flags);

	if (b->mcidx) {
#if MC_DEBUG
		memcpy(b->debug, b->entries,
		       b->mcidx * sizeof(struct multicall_entry));
#endif

		if (HYPERVISOR_multicall(b->entries, b->mcidx) != 0)
			BUG();
		for (i = 0; i < b->mcidx; i++)
			if (b->entries[i].result < 0)
				ret++;

#if MC_DEBUG
		if (ret) {
			printk(KERN_ERR "%d multicall(s) failed: cpu %d\n",
			       ret, smp_processor_id());
			dump_stack();
			for (i = 0; i < b->mcidx; i++) {
				printk("  call %2d/%d: op=%lu arg=[%lx] result=%ld\n",
				       i+1, b->mcidx,
				       b->debug[i].op,
				       b->debug[i].args[0],
				       b->entries[i].result);
			}
		}
#endif

		b->mcidx = 0;
		b->argidx = 0;
	} else
		BUG_ON(b->argidx != 0);

	local_irq_restore(flags);

	for (i = 0; i < b->cbidx; i++) {
		struct callback *cb = &b->callbacks[i];

		(*cb->fn)(cb->data);
	}
	b->cbidx = 0;

	BUG_ON(ret);
}

struct multicall_space __xen_mc_entry(size_t args)
{
	struct mc_buffer *b = &__get_cpu_var(mc_buffer);
	struct multicall_space ret;
	unsigned argidx = roundup(b->argidx, sizeof(u64));

	BUG_ON(preemptible());
	BUG_ON(b->argidx > MC_ARGS);

	if (b->mcidx == MC_BATCH ||
	    (argidx + args) > MC_ARGS) {
		xen_mc_flush();
		argidx = roundup(b->argidx, sizeof(u64));
	}

	ret.mc = &b->entries[b->mcidx];
	b->mcidx++;
	ret.args = &b->args[argidx];
	b->argidx = argidx + args;

	BUG_ON(b->argidx > MC_ARGS);
	return ret;
}

struct multicall_space xen_mc_extend_args(unsigned long op, size_t size)
{
	struct mc_buffer *b = &__get_cpu_var(mc_buffer);
	struct multicall_space ret = { NULL, NULL };

	BUG_ON(preemptible());
	BUG_ON(b->argidx > MC_ARGS);

	if (b->mcidx == 0)
		return ret;

	if (b->entries[b->mcidx - 1].op != op)
		return ret;

	if ((b->argidx + size) > MC_ARGS)
		return ret;

	ret.mc = &b->entries[b->mcidx - 1];
	ret.args = &b->args[b->argidx];
	b->argidx += size;

	BUG_ON(b->argidx > MC_ARGS);
	return ret;
}

void xen_mc_callback(void (*fn)(void *), void *data)
{
	struct mc_buffer *b = &__get_cpu_var(mc_buffer);
	struct callback *cb;

	if (b->cbidx == MC_BATCH)
		xen_mc_flush();

	cb = &b->callbacks[b->cbidx++];
	cb->fn = fn;
	cb->data = data;
}

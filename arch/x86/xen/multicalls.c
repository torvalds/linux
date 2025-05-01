// SPDX-License-Identifier: GPL-2.0
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
#include <linux/debugfs.h>
#include <linux/jump_label.h>
#include <linux/printk.h>

#include <asm/xen/hypercall.h>

#include "xen-ops.h"

#define MC_BATCH	32

#define MC_ARGS		(MC_BATCH * 16)


struct mc_buffer {
	unsigned mcidx, argidx, cbidx;
	struct multicall_entry entries[MC_BATCH];
	unsigned char args[MC_ARGS];
	struct callback {
		void (*fn)(void *);
		void *data;
	} callbacks[MC_BATCH];
};

struct mc_debug_data {
	struct multicall_entry entries[MC_BATCH];
	void *caller[MC_BATCH];
	size_t argsz[MC_BATCH];
	unsigned long *args[MC_BATCH];
};

static DEFINE_PER_CPU(struct mc_buffer, mc_buffer);
static struct mc_debug_data mc_debug_data_early __initdata;
static struct mc_debug_data __percpu *mc_debug_data_ptr;
DEFINE_PER_CPU(unsigned long, xen_mc_irq_flags);

static struct static_key mc_debug __ro_after_init;
static bool mc_debug_enabled __initdata;

static struct mc_debug_data * __ref get_mc_debug(void)
{
	if (!mc_debug_data_ptr)
		return &mc_debug_data_early;

	return this_cpu_ptr(mc_debug_data_ptr);
}

static int __init xen_parse_mc_debug(char *arg)
{
	mc_debug_enabled = true;
	static_key_slow_inc(&mc_debug);

	return 0;
}
early_param("xen_mc_debug", xen_parse_mc_debug);

static int __init mc_debug_enable(void)
{
	unsigned long flags;
	struct mc_debug_data __percpu *mcdb;

	if (!mc_debug_enabled)
		return 0;

	mcdb = alloc_percpu(struct mc_debug_data);
	if (!mcdb) {
		pr_err("xen_mc_debug inactive\n");
		static_key_slow_dec(&mc_debug);
		return -ENOMEM;
	}

	/* Be careful when switching to percpu debug data. */
	local_irq_save(flags);
	xen_mc_flush();
	mc_debug_data_ptr = mcdb;
	local_irq_restore(flags);

	pr_info("xen_mc_debug active\n");

	return 0;
}
early_initcall(mc_debug_enable);

/* Number of parameters of hypercalls used via multicalls. */
static const uint8_t hpcpars[] = {
	[__HYPERVISOR_mmu_update] = 4,
	[__HYPERVISOR_stack_switch] = 2,
	[__HYPERVISOR_fpu_taskswitch] = 1,
	[__HYPERVISOR_update_descriptor] = 2,
	[__HYPERVISOR_update_va_mapping] = 3,
	[__HYPERVISOR_mmuext_op] = 4,
};

static void print_debug_data(struct mc_buffer *b, struct mc_debug_data *mcdb,
			     int idx)
{
	unsigned int arg;
	unsigned int opidx = mcdb->entries[idx].op & 0xff;
	unsigned int pars = 0;

	pr_err("  call %2d: op=%lu result=%ld caller=%pS ", idx + 1,
	       mcdb->entries[idx].op, b->entries[idx].result,
	       mcdb->caller[idx]);
	if (opidx < ARRAY_SIZE(hpcpars))
		pars = hpcpars[opidx];
	if (pars) {
		pr_cont("pars=");
		for (arg = 0; arg < pars; arg++)
			pr_cont("%lx ", mcdb->entries[idx].args[arg]);
	}
	if (mcdb->argsz[idx]) {
		pr_cont("args=");
		for (arg = 0; arg < mcdb->argsz[idx] / 8; arg++)
			pr_cont("%lx ", mcdb->args[idx][arg]);
	}
	pr_cont("\n");
}

void xen_mc_flush(void)
{
	struct mc_buffer *b = this_cpu_ptr(&mc_buffer);
	struct multicall_entry *mc;
	struct mc_debug_data *mcdb = NULL;
	int ret = 0;
	unsigned long flags;
	int i;

	BUG_ON(preemptible());

	/* Disable interrupts in case someone comes in and queues
	   something in the middle */
	local_irq_save(flags);

	trace_xen_mc_flush(b->mcidx, b->argidx, b->cbidx);

	if (static_key_false(&mc_debug)) {
		mcdb = get_mc_debug();
		memcpy(mcdb->entries, b->entries,
		       b->mcidx * sizeof(struct multicall_entry));
	}

	switch (b->mcidx) {
	case 0:
		/* no-op */
		BUG_ON(b->argidx != 0);
		break;

	case 1:
		/* Singleton multicall - bypass multicall machinery
		   and just do the call directly. */
		mc = &b->entries[0];

		mc->result = xen_single_call(mc->op, mc->args[0], mc->args[1],
					     mc->args[2], mc->args[3],
					     mc->args[4]);
		ret = mc->result < 0;
		break;

	default:
		if (HYPERVISOR_multicall(b->entries, b->mcidx) != 0)
			BUG();
		for (i = 0; i < b->mcidx; i++)
			if (b->entries[i].result < 0)
				ret++;
	}

	if (WARN_ON(ret)) {
		pr_err("%d of %d multicall(s) failed: cpu %d\n",
		       ret, b->mcidx, smp_processor_id());
		for (i = 0; i < b->mcidx; i++) {
			if (static_key_false(&mc_debug)) {
				print_debug_data(b, mcdb, i);
			} else if (b->entries[i].result < 0) {
				pr_err("  call %2d: op=%lu arg=[%lx] result=%ld\n",
				       i + 1,
				       b->entries[i].op,
				       b->entries[i].args[0],
				       b->entries[i].result);
			}
		}
	}

	b->mcidx = 0;
	b->argidx = 0;

	for (i = 0; i < b->cbidx; i++) {
		struct callback *cb = &b->callbacks[i];

		(*cb->fn)(cb->data);
	}
	b->cbidx = 0;

	local_irq_restore(flags);
}

struct multicall_space __xen_mc_entry(size_t args)
{
	struct mc_buffer *b = this_cpu_ptr(&mc_buffer);
	struct multicall_space ret;
	unsigned argidx = roundup(b->argidx, sizeof(u64));

	trace_xen_mc_entry_alloc(args);

	BUG_ON(preemptible());
	BUG_ON(b->argidx >= MC_ARGS);

	if (unlikely(b->mcidx == MC_BATCH ||
		     (argidx + args) >= MC_ARGS)) {
		trace_xen_mc_flush_reason((b->mcidx == MC_BATCH) ?
					  XEN_MC_FL_BATCH : XEN_MC_FL_ARGS);
		xen_mc_flush();
		argidx = roundup(b->argidx, sizeof(u64));
	}

	ret.mc = &b->entries[b->mcidx];
	if (static_key_false(&mc_debug)) {
		struct mc_debug_data *mcdb = get_mc_debug();

		mcdb->caller[b->mcidx] = __builtin_return_address(0);
		mcdb->argsz[b->mcidx] = args;
		mcdb->args[b->mcidx] = (unsigned long *)(&b->args[argidx]);
	}
	b->mcidx++;
	ret.args = &b->args[argidx];
	b->argidx = argidx + args;

	BUG_ON(b->argidx >= MC_ARGS);
	return ret;
}

struct multicall_space xen_mc_extend_args(unsigned long op, size_t size)
{
	struct mc_buffer *b = this_cpu_ptr(&mc_buffer);
	struct multicall_space ret = { NULL, NULL };

	BUG_ON(preemptible());
	BUG_ON(b->argidx >= MC_ARGS);

	if (unlikely(b->mcidx == 0 ||
		     b->entries[b->mcidx - 1].op != op)) {
		trace_xen_mc_extend_args(op, size, XEN_MC_XE_BAD_OP);
		goto out;
	}

	if (unlikely((b->argidx + size) >= MC_ARGS)) {
		trace_xen_mc_extend_args(op, size, XEN_MC_XE_NO_SPACE);
		goto out;
	}

	ret.mc = &b->entries[b->mcidx - 1];
	ret.args = &b->args[b->argidx];
	b->argidx += size;

	BUG_ON(b->argidx >= MC_ARGS);

	trace_xen_mc_extend_args(op, size, XEN_MC_XE_OK);
out:
	return ret;
}

void xen_mc_callback(void (*fn)(void *), void *data)
{
	struct mc_buffer *b = this_cpu_ptr(&mc_buffer);
	struct callback *cb;

	if (b->cbidx == MC_BATCH) {
		trace_xen_mc_flush_reason(XEN_MC_FL_CALLBACK);
		xen_mc_flush();
	}

	trace_xen_mc_callback(fn, data);

	cb = &b->callbacks[b->cbidx++];
	cb->fn = fn;
	cb->data = data;
}

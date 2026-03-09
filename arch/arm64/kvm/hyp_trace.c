// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Vincent Donnefort <vdonnefort@google.com>
 */

#include <linux/cpumask.h>
#include <linux/trace_remote.h>
#include <linux/tracefs.h>
#include <linux/simple_ring_buffer.h>

#include <asm/arch_timer.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyptrace.h>
#include <asm/kvm_mmu.h>

#include "hyp_trace.h"

/* Same 10min used by clocksource when width is more than 32-bits */
#define CLOCK_MAX_CONVERSION_S	600
/*
 * Time to give for the clock init. Long enough to get a good mult/shift
 * estimation. Short enough to not delay the tracing start too much.
 */
#define CLOCK_INIT_MS		100
/*
 * Time between clock checks. Must be small enough to catch clock deviation when
 * it is still tiny.
 */
#define CLOCK_UPDATE_MS		500

static struct hyp_trace_clock {
	u64			cycles;
	u64			cyc_overflow64;
	u64			boot;
	u32			mult;
	u32			shift;
	struct delayed_work	work;
	struct completion	ready;
	struct mutex		lock;
	bool			running;
} hyp_clock;

static void __hyp_clock_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct hyp_trace_clock *hyp_clock;
	struct system_time_snapshot snap;
	u64 rate, delta_cycles;
	u64 boot, delta_boot;

	hyp_clock = container_of(dwork, struct hyp_trace_clock, work);

	ktime_get_snapshot(&snap);
	boot = ktime_to_ns(snap.boot);

	delta_boot = boot - hyp_clock->boot;
	delta_cycles = snap.cycles - hyp_clock->cycles;

	/* Compare hyp clock with the kernel boot clock */
	if (hyp_clock->mult) {
		u64 err, cur = delta_cycles;

		if (WARN_ON_ONCE(cur >= hyp_clock->cyc_overflow64)) {
			__uint128_t tmp = (__uint128_t)cur * hyp_clock->mult;

			cur = tmp >> hyp_clock->shift;
		} else {
			cur *= hyp_clock->mult;
			cur >>= hyp_clock->shift;
		}
		cur += hyp_clock->boot;

		err = abs_diff(cur, boot);
		/* No deviation, only update epoch if necessary */
		if (!err) {
			if (delta_cycles >= (hyp_clock->cyc_overflow64 >> 1))
				goto fast_forward;

			goto resched;
		}

		/* Warn if the error is above tracing precision (1us) */
		if (err > NSEC_PER_USEC)
			pr_warn_ratelimited("hyp trace clock off by %lluus\n",
					    err / NSEC_PER_USEC);
	}

	rate = div64_u64(delta_cycles * NSEC_PER_SEC, delta_boot);

	clocks_calc_mult_shift(&hyp_clock->mult, &hyp_clock->shift,
			       rate, NSEC_PER_SEC, CLOCK_MAX_CONVERSION_S);

	/* Add a comfortable 50% margin */
	hyp_clock->cyc_overflow64 = (U64_MAX / hyp_clock->mult) >> 1;

fast_forward:
	hyp_clock->cycles = snap.cycles;
	hyp_clock->boot = boot;
	kvm_call_hyp_nvhe(__tracing_update_clock, hyp_clock->mult,
			  hyp_clock->shift, hyp_clock->boot, hyp_clock->cycles);
	complete(&hyp_clock->ready);

resched:
	schedule_delayed_work(&hyp_clock->work,
			      msecs_to_jiffies(CLOCK_UPDATE_MS));
}

static void hyp_trace_clock_enable(struct hyp_trace_clock *hyp_clock, bool enable)
{
	struct system_time_snapshot snap;

	if (hyp_clock->running == enable)
		return;

	if (!enable) {
		cancel_delayed_work_sync(&hyp_clock->work);
		hyp_clock->running = false;
	}

	ktime_get_snapshot(&snap);

	hyp_clock->boot = ktime_to_ns(snap.boot);
	hyp_clock->cycles = snap.cycles;
	hyp_clock->mult = 0;

	init_completion(&hyp_clock->ready);
	INIT_DELAYED_WORK(&hyp_clock->work, __hyp_clock_work);
	schedule_delayed_work(&hyp_clock->work, msecs_to_jiffies(CLOCK_INIT_MS));
	wait_for_completion(&hyp_clock->ready);
	hyp_clock->running = true;
}

/* Access to this struct within the trace_remote_callbacks are protected by the trace_remote lock */
static struct hyp_trace_buffer {
	struct hyp_trace_desc	*desc;
	size_t			desc_size;
} trace_buffer;

static int __map_hyp(void *start, size_t size)
{
	if (is_protected_kvm_enabled())
		return 0;

	return create_hyp_mappings(start, start + size, PAGE_HYP);
}

static int __share_page(unsigned long va)
{
	return kvm_share_hyp((void *)va, (void *)va + 1);
}

static void __unshare_page(unsigned long va)
{
	kvm_unshare_hyp((void *)va, (void *)va + 1);
}

static int hyp_trace_buffer_alloc_bpages_backing(struct hyp_trace_buffer *trace_buffer, size_t size)
{
	int nr_bpages = (PAGE_ALIGN(size) / PAGE_SIZE) + 1;
	size_t backing_size;
	void *start;

	backing_size = PAGE_ALIGN(sizeof(struct simple_buffer_page) * nr_bpages *
				  num_possible_cpus());

	start = alloc_pages_exact(backing_size, GFP_KERNEL_ACCOUNT);
	if (!start)
		return -ENOMEM;

	trace_buffer->desc->bpages_backing_start = (unsigned long)start;
	trace_buffer->desc->bpages_backing_size = backing_size;

	return __map_hyp(start, backing_size);
}

static void hyp_trace_buffer_free_bpages_backing(struct hyp_trace_buffer *trace_buffer)
{
	free_pages_exact((void *)trace_buffer->desc->bpages_backing_start,
			 trace_buffer->desc->bpages_backing_size);
}

static void hyp_trace_buffer_unshare_hyp(struct hyp_trace_buffer *trace_buffer, int last_cpu)
{
	struct ring_buffer_desc *rb_desc;
	int cpu, p;

	for_each_ring_buffer_desc(rb_desc, cpu, &trace_buffer->desc->trace_buffer_desc) {
		if (cpu > last_cpu)
			break;

		__share_page(rb_desc->meta_va);
		for (p = 0; p < rb_desc->nr_page_va; p++)
			__unshare_page(rb_desc->page_va[p]);
	}
}

static int hyp_trace_buffer_share_hyp(struct hyp_trace_buffer *trace_buffer)
{
	struct ring_buffer_desc *rb_desc;
	int cpu, p, ret = 0;

	for_each_ring_buffer_desc(rb_desc, cpu, &trace_buffer->desc->trace_buffer_desc) {
		ret = __share_page(rb_desc->meta_va);
		if (ret)
			break;

		for (p = 0; p < rb_desc->nr_page_va; p++) {
			ret = __share_page(rb_desc->page_va[p]);
			if (ret)
				break;
		}

		if (ret) {
			for (p--; p >= 0; p--)
				__unshare_page(rb_desc->page_va[p]);
			break;
		}
	}

	if (ret)
		hyp_trace_buffer_unshare_hyp(trace_buffer, cpu--);

	return ret;
}

static struct trace_buffer_desc *hyp_trace_load(unsigned long size, void *priv)
{
	struct hyp_trace_buffer *trace_buffer = priv;
	struct hyp_trace_desc *desc;
	size_t desc_size;
	int ret;

	if (WARN_ON(trace_buffer->desc))
		return ERR_PTR(-EINVAL);

	desc_size = trace_buffer_desc_size(size, num_possible_cpus());
	if (desc_size == SIZE_MAX)
		return ERR_PTR(-E2BIG);

	desc_size = PAGE_ALIGN(desc_size);
	desc = (struct hyp_trace_desc *)alloc_pages_exact(desc_size, GFP_KERNEL);
	if (!desc)
		return ERR_PTR(-ENOMEM);

	ret = __map_hyp(desc, desc_size);
	if (ret)
		goto err_free_desc;

	trace_buffer->desc = desc;

	ret = hyp_trace_buffer_alloc_bpages_backing(trace_buffer, size);
	if (ret)
		goto err_free_desc;

	ret = trace_remote_alloc_buffer(&desc->trace_buffer_desc, desc_size, size,
					cpu_possible_mask);
	if (ret)
		goto err_free_backing;

	ret = hyp_trace_buffer_share_hyp(trace_buffer);
	if (ret)
		goto err_free_buffer;

	ret = kvm_call_hyp_nvhe(__tracing_load, (unsigned long)desc, desc_size);
	if (ret)
		goto err_unload_pages;

	return &desc->trace_buffer_desc;

err_unload_pages:
	hyp_trace_buffer_unshare_hyp(trace_buffer, INT_MAX);

err_free_buffer:
	trace_remote_free_buffer(&desc->trace_buffer_desc);

err_free_backing:
	hyp_trace_buffer_free_bpages_backing(trace_buffer);

err_free_desc:
	free_pages_exact(desc, desc_size);
	trace_buffer->desc = NULL;

	return ERR_PTR(ret);
}

static void hyp_trace_unload(struct trace_buffer_desc *desc, void *priv)
{
	struct hyp_trace_buffer *trace_buffer = priv;

	if (WARN_ON(desc != &trace_buffer->desc->trace_buffer_desc))
		return;

	kvm_call_hyp_nvhe(__tracing_unload);
	hyp_trace_buffer_unshare_hyp(trace_buffer, INT_MAX);
	trace_remote_free_buffer(desc);
	hyp_trace_buffer_free_bpages_backing(trace_buffer);
	free_pages_exact(trace_buffer->desc, trace_buffer->desc_size);
	trace_buffer->desc = NULL;
}

static int hyp_trace_enable_tracing(bool enable, void *priv)
{
	hyp_trace_clock_enable(&hyp_clock, enable);

	return kvm_call_hyp_nvhe(__tracing_enable, enable);
}

static int hyp_trace_swap_reader_page(unsigned int cpu, void *priv)
{
	return kvm_call_hyp_nvhe(__tracing_swap_reader, cpu);
}

static int hyp_trace_reset(unsigned int cpu, void *priv)
{
	return kvm_call_hyp_nvhe(__tracing_reset, cpu);
}

static int hyp_trace_enable_event(unsigned short id, bool enable, void *priv)
{
	return 0;
}

static int hyp_trace_clock_show(struct seq_file *m, void *v)
{
	seq_puts(m, "[boot]\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hyp_trace_clock);

static int hyp_trace_init_tracefs(struct dentry *d, void *priv)
{
	return tracefs_create_file("trace_clock", 0440, d, NULL, &hyp_trace_clock_fops) ?
		0 : -ENOMEM;
}

static struct trace_remote_callbacks trace_remote_callbacks = {
	.init			= hyp_trace_init_tracefs,
	.load_trace_buffer	= hyp_trace_load,
	.unload_trace_buffer	= hyp_trace_unload,
	.enable_tracing		= hyp_trace_enable_tracing,
	.swap_reader_page	= hyp_trace_swap_reader_page,
	.reset			= hyp_trace_reset,
	.enable_event		= hyp_trace_enable_event,
};

int __init kvm_hyp_trace_init(void)
{
	int cpu;

	if (is_kernel_in_hyp_mode())
		return 0;

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
	for_each_possible_cpu(cpu) {
		const struct arch_timer_erratum_workaround *wa =
			per_cpu(timer_unstable_counter_workaround, cpu);

		if (wa && wa->read_cntvct_el0) {
			pr_warn("hyp trace can't handle CNTVCT workaround '%s'\n", wa->desc);
			return -EOPNOTSUPP;
		}
	}
#endif

	return trace_remote_register("hypervisor", &trace_remote_callbacks, &trace_buffer, NULL, 0);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Vincent Donnefort <vdonnefort@google.com>
 */

#include <nvhe/clock.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/trace.h>

#include <asm/percpu.h>
#include <asm/kvm_mmu.h>
#include <asm/local.h>

#include "simple_ring_buffer.c"

static DEFINE_PER_CPU(struct simple_rb_per_cpu, __simple_rbs);

static struct hyp_trace_buffer {
	struct simple_rb_per_cpu __percpu	*simple_rbs;
	void					*bpages_backing_start;
	size_t					bpages_backing_size;
	hyp_spinlock_t				lock;
} trace_buffer = {
	.simple_rbs = &__simple_rbs,
	.lock = __HYP_SPIN_LOCK_UNLOCKED,
};

static bool hyp_trace_buffer_loaded(struct hyp_trace_buffer *trace_buffer)
{
	return trace_buffer->bpages_backing_size > 0;
}

void *tracing_reserve_entry(unsigned long length)
{
	return simple_ring_buffer_reserve(this_cpu_ptr(trace_buffer.simple_rbs), length,
					  trace_clock());
}

void tracing_commit_entry(void)
{
	simple_ring_buffer_commit(this_cpu_ptr(trace_buffer.simple_rbs));
}

static int __admit_host_mem(void *start, u64 size)
{
	if (!PAGE_ALIGNED(start) || !PAGE_ALIGNED(size) || !size)
		return -EINVAL;

	if (!is_protected_kvm_enabled())
		return 0;

	return __pkvm_host_donate_hyp(hyp_virt_to_pfn(start), size >> PAGE_SHIFT);
}

static void __release_host_mem(void *start, u64 size)
{
	if (!is_protected_kvm_enabled())
		return;

	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(start), size >> PAGE_SHIFT));
}

static int hyp_trace_buffer_load_bpage_backing(struct hyp_trace_buffer *trace_buffer,
					       struct hyp_trace_desc *desc)
{
	void *start = (void *)kern_hyp_va(desc->bpages_backing_start);
	size_t size = desc->bpages_backing_size;
	int ret;

	ret = __admit_host_mem(start, size);
	if (ret)
		return ret;

	memset(start, 0, size);

	trace_buffer->bpages_backing_start = start;
	trace_buffer->bpages_backing_size = size;

	return 0;
}

static void hyp_trace_buffer_unload_bpage_backing(struct hyp_trace_buffer *trace_buffer)
{
	void *start = trace_buffer->bpages_backing_start;
	size_t size = trace_buffer->bpages_backing_size;

	if (!size)
		return;

	memset(start, 0, size);

	__release_host_mem(start, size);

	trace_buffer->bpages_backing_start = 0;
	trace_buffer->bpages_backing_size = 0;
}

static void *__pin_shared_page(unsigned long kern_va)
{
	void *va = kern_hyp_va((void *)kern_va);

	if (!is_protected_kvm_enabled())
		return va;

	return hyp_pin_shared_mem(va, va + PAGE_SIZE) ? NULL : va;
}

static void __unpin_shared_page(void *va)
{
	if (!is_protected_kvm_enabled())
		return;

	hyp_unpin_shared_mem(va, va + PAGE_SIZE);
}

static void hyp_trace_buffer_unload(struct hyp_trace_buffer *trace_buffer)
{
	int cpu;

	hyp_assert_lock_held(&trace_buffer->lock);

	if (!hyp_trace_buffer_loaded(trace_buffer))
		return;

	for (cpu = 0; cpu < hyp_nr_cpus; cpu++)
		simple_ring_buffer_unload_mm(per_cpu_ptr(trace_buffer->simple_rbs, cpu),
					     __unpin_shared_page);

	hyp_trace_buffer_unload_bpage_backing(trace_buffer);
}

static int hyp_trace_buffer_load(struct hyp_trace_buffer *trace_buffer,
				 struct hyp_trace_desc *desc)
{
	struct simple_buffer_page *bpages;
	struct ring_buffer_desc *rb_desc;
	int ret, cpu;

	hyp_assert_lock_held(&trace_buffer->lock);

	if (hyp_trace_buffer_loaded(trace_buffer))
		return -EINVAL;

	ret = hyp_trace_buffer_load_bpage_backing(trace_buffer, desc);
	if (ret)
		return ret;

	bpages = trace_buffer->bpages_backing_start;
	for_each_ring_buffer_desc(rb_desc, cpu, &desc->trace_buffer_desc) {
		ret = simple_ring_buffer_init_mm(per_cpu_ptr(trace_buffer->simple_rbs, cpu),
						 bpages, rb_desc, __pin_shared_page,
						__unpin_shared_page);
		if (ret)
			break;

		bpages += rb_desc->nr_page_va;
	}

	if (ret)
		hyp_trace_buffer_unload(trace_buffer);

	return ret;
}

static bool hyp_trace_desc_validate(struct hyp_trace_desc *desc, size_t desc_size)
{
	struct ring_buffer_desc *rb_desc;
	unsigned int cpu;
	size_t nr_bpages;
	void *desc_end;

	/*
	 * Both desc_size and bpages_backing_size are untrusted host-provided
	 * values. We rely on __pkvm_host_donate_hyp() to enforce their validity.
	 */
	desc_end = (void *)desc + desc_size;
	nr_bpages = desc->bpages_backing_size / sizeof(struct simple_buffer_page);

	for_each_ring_buffer_desc(rb_desc, cpu, &desc->trace_buffer_desc) {
		/* Can we read nr_page_va? */
		if ((void *)rb_desc + struct_size(rb_desc, page_va, 0) > desc_end)
			return false;

		/* Overflow desc? */
		if ((void *)rb_desc + struct_size(rb_desc, page_va, rb_desc->nr_page_va) > desc_end)
			return false;

		/* Overflow bpages backing memory? */
		if (nr_bpages < rb_desc->nr_page_va)
			return false;

		if (cpu >= hyp_nr_cpus)
			return false;

		if (cpu != rb_desc->cpu)
			return false;

		nr_bpages -= rb_desc->nr_page_va;
	}

	return true;
}

int __tracing_load(unsigned long desc_hva, size_t desc_size)
{
	struct hyp_trace_desc *desc = (struct hyp_trace_desc *)kern_hyp_va(desc_hva);
	int ret;

	ret = __admit_host_mem(desc, desc_size);
	if (ret)
		return ret;

	if (!hyp_trace_desc_validate(desc, desc_size))
		goto err_release_desc;

	hyp_spin_lock(&trace_buffer.lock);

	ret = hyp_trace_buffer_load(&trace_buffer, desc);

	hyp_spin_unlock(&trace_buffer.lock);

err_release_desc:
	__release_host_mem(desc, desc_size);
	return ret;
}

void __tracing_unload(void)
{
	hyp_spin_lock(&trace_buffer.lock);
	hyp_trace_buffer_unload(&trace_buffer);
	hyp_spin_unlock(&trace_buffer.lock);
}

int __tracing_enable(bool enable)
{
	int cpu, ret = enable ? -EINVAL : 0;

	hyp_spin_lock(&trace_buffer.lock);

	if (!hyp_trace_buffer_loaded(&trace_buffer))
		goto unlock;

	for (cpu = 0; cpu < hyp_nr_cpus; cpu++)
		simple_ring_buffer_enable_tracing(per_cpu_ptr(trace_buffer.simple_rbs, cpu),
						  enable);

	ret = 0;

unlock:
	hyp_spin_unlock(&trace_buffer.lock);

	return ret;
}

int __tracing_swap_reader(unsigned int cpu)
{
	int ret = -ENODEV;

	if (cpu >= hyp_nr_cpus)
		return -EINVAL;

	hyp_spin_lock(&trace_buffer.lock);

	if (hyp_trace_buffer_loaded(&trace_buffer))
		ret = simple_ring_buffer_swap_reader_page(
				per_cpu_ptr(trace_buffer.simple_rbs, cpu));

	hyp_spin_unlock(&trace_buffer.lock);

	return ret;
}

void __tracing_update_clock(u32 mult, u32 shift, u64 epoch_ns, u64 epoch_cyc)
{
	int cpu;

	/* After this loop, all CPUs are observing the new bank... */
	for (cpu = 0; cpu < hyp_nr_cpus; cpu++) {
		struct simple_rb_per_cpu *simple_rb = per_cpu_ptr(trace_buffer.simple_rbs, cpu);

		while (READ_ONCE(simple_rb->status) == SIMPLE_RB_WRITING)
			;
	}

	/* ...we can now override the old one and swap. */
	trace_clock_update(mult, shift, epoch_ns, epoch_cyc);
}

int __tracing_reset(unsigned int cpu)
{
	int ret = -ENODEV;

	if (cpu >= hyp_nr_cpus)
		return -EINVAL;

	hyp_spin_lock(&trace_buffer.lock);

	if (hyp_trace_buffer_loaded(&trace_buffer))
		ret = simple_ring_buffer_reset(per_cpu_ptr(trace_buffer.simple_rbs, cpu));

	hyp_spin_unlock(&trace_buffer.lock);

	return ret;
}

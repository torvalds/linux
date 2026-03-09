// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 * Author: Vincent Donnefort <vdonnefort@google.com>
 */

#include <linux/trace_remote.h>
#include <linux/simple_ring_buffer.h>

#include <asm/kvm_host.h>
#include <asm/kvm_hyptrace.h>
#include <asm/kvm_mmu.h>

#include "hyp_trace.h"

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
	return kvm_call_hyp_nvhe(__tracing_enable, enable);
}

static int hyp_trace_swap_reader_page(unsigned int cpu, void *priv)
{
	return kvm_call_hyp_nvhe(__tracing_swap_reader, cpu);
}

static int hyp_trace_reset(unsigned int cpu, void *priv)
{
	return 0;
}

static int hyp_trace_enable_event(unsigned short id, bool enable, void *priv)
{
	return 0;
}

static struct trace_remote_callbacks trace_remote_callbacks = {
	.load_trace_buffer	= hyp_trace_load,
	.unload_trace_buffer	= hyp_trace_unload,
	.enable_tracing		= hyp_trace_enable_tracing,
	.swap_reader_page	= hyp_trace_swap_reader_page,
	.reset			= hyp_trace_reset,
	.enable_event		= hyp_trace_enable_event,
};

int __init kvm_hyp_trace_init(void)
{
	if (is_kernel_in_hyp_mode())
		return 0;

	return trace_remote_register("hypervisor", &trace_remote_callbacks, &trace_buffer, NULL, 0);
}

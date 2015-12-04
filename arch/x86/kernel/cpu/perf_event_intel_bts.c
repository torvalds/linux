/*
 * BTS PMU driver for perf
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#undef DEBUG

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/coredump.h>

#include <asm-generic/sizes.h>
#include <asm/perf_event.h>

#include "perf_event.h"

struct bts_ctx {
	struct perf_output_handle	handle;
	struct debug_store		ds_back;
	int				started;
};

static DEFINE_PER_CPU(struct bts_ctx, bts_ctx);

#define BTS_RECORD_SIZE		24
#define BTS_SAFETY_MARGIN	4080

struct bts_phys {
	struct page	*page;
	unsigned long	size;
	unsigned long	offset;
	unsigned long	displacement;
};

struct bts_buffer {
	size_t		real_size;	/* multiple of BTS_RECORD_SIZE */
	unsigned int	nr_pages;
	unsigned int	nr_bufs;
	unsigned int	cur_buf;
	bool		snapshot;
	local_t		data_size;
	local_t		lost;
	local_t		head;
	unsigned long	end;
	void		**data_pages;
	struct bts_phys	buf[0];
};

struct pmu bts_pmu;

static size_t buf_size(struct page *page)
{
	return 1 << (PAGE_SHIFT + page_private(page));
}

static void *
bts_buffer_setup_aux(int cpu, void **pages, int nr_pages, bool overwrite)
{
	struct bts_buffer *buf;
	struct page *page;
	int node = (cpu == -1) ? cpu : cpu_to_node(cpu);
	unsigned long offset;
	size_t size = nr_pages << PAGE_SHIFT;
	int pg, nbuf, pad;

	/* count all the high order buffers */
	for (pg = 0, nbuf = 0; pg < nr_pages;) {
		page = virt_to_page(pages[pg]);
		if (WARN_ON_ONCE(!PagePrivate(page) && nr_pages > 1))
			return NULL;
		pg += 1 << page_private(page);
		nbuf++;
	}

	/*
	 * to avoid interrupts in overwrite mode, only allow one physical
	 */
	if (overwrite && nbuf > 1)
		return NULL;

	buf = kzalloc_node(offsetof(struct bts_buffer, buf[nbuf]), GFP_KERNEL, node);
	if (!buf)
		return NULL;

	buf->nr_pages = nr_pages;
	buf->nr_bufs = nbuf;
	buf->snapshot = overwrite;
	buf->data_pages = pages;
	buf->real_size = size - size % BTS_RECORD_SIZE;

	for (pg = 0, nbuf = 0, offset = 0, pad = 0; nbuf < buf->nr_bufs; nbuf++) {
		unsigned int __nr_pages;

		page = virt_to_page(pages[pg]);
		__nr_pages = PagePrivate(page) ? 1 << page_private(page) : 1;
		buf->buf[nbuf].page = page;
		buf->buf[nbuf].offset = offset;
		buf->buf[nbuf].displacement = (pad ? BTS_RECORD_SIZE - pad : 0);
		buf->buf[nbuf].size = buf_size(page) - buf->buf[nbuf].displacement;
		pad = buf->buf[nbuf].size % BTS_RECORD_SIZE;
		buf->buf[nbuf].size -= pad;

		pg += __nr_pages;
		offset += __nr_pages << PAGE_SHIFT;
	}

	return buf;
}

static void bts_buffer_free_aux(void *data)
{
	kfree(data);
}

static unsigned long bts_buffer_offset(struct bts_buffer *buf, unsigned int idx)
{
	return buf->buf[idx].offset + buf->buf[idx].displacement;
}

static void
bts_config_buffer(struct bts_buffer *buf)
{
	int cpu = raw_smp_processor_id();
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;
	struct bts_phys *phys = &buf->buf[buf->cur_buf];
	unsigned long index, thresh = 0, end = phys->size;
	struct page *page = phys->page;

	index = local_read(&buf->head);

	if (!buf->snapshot) {
		if (buf->end < phys->offset + buf_size(page))
			end = buf->end - phys->offset - phys->displacement;

		index -= phys->offset + phys->displacement;

		if (end - index > BTS_SAFETY_MARGIN)
			thresh = end - BTS_SAFETY_MARGIN;
		else if (end - index > BTS_RECORD_SIZE)
			thresh = end - BTS_RECORD_SIZE;
		else
			thresh = end;
	}

	ds->bts_buffer_base = (u64)(long)page_address(page) + phys->displacement;
	ds->bts_index = ds->bts_buffer_base + index;
	ds->bts_absolute_maximum = ds->bts_buffer_base + end;
	ds->bts_interrupt_threshold = !buf->snapshot
		? ds->bts_buffer_base + thresh
		: ds->bts_absolute_maximum + BTS_RECORD_SIZE;
}

static void bts_buffer_pad_out(struct bts_phys *phys, unsigned long head)
{
	unsigned long index = head - phys->offset;

	memset(page_address(phys->page) + index, 0, phys->size - index);
}

static bool bts_buffer_is_full(struct bts_buffer *buf, struct bts_ctx *bts)
{
	if (buf->snapshot)
		return false;

	if (local_read(&buf->data_size) >= bts->handle.size ||
	    bts->handle.size - local_read(&buf->data_size) < BTS_RECORD_SIZE)
		return true;

	return false;
}

static void bts_update(struct bts_ctx *bts)
{
	int cpu = raw_smp_processor_id();
	struct debug_store *ds = per_cpu(cpu_hw_events, cpu).ds;
	struct bts_buffer *buf = perf_get_aux(&bts->handle);
	unsigned long index = ds->bts_index - ds->bts_buffer_base, old, head;

	if (!buf)
		return;

	head = index + bts_buffer_offset(buf, buf->cur_buf);
	old = local_xchg(&buf->head, head);

	if (!buf->snapshot) {
		if (old == head)
			return;

		if (ds->bts_index >= ds->bts_absolute_maximum)
			local_inc(&buf->lost);

		/*
		 * old and head are always in the same physical buffer, so we
		 * can subtract them to get the data size.
		 */
		local_add(head - old, &buf->data_size);
	} else {
		local_set(&buf->data_size, head);
	}
}

static void __bts_event_start(struct perf_event *event)
{
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);
	struct bts_buffer *buf = perf_get_aux(&bts->handle);
	u64 config = 0;

	if (!buf || bts_buffer_is_full(buf, bts))
		return;

	event->hw.itrace_started = 1;
	event->hw.state = 0;

	if (!buf->snapshot)
		config |= ARCH_PERFMON_EVENTSEL_INT;
	if (!event->attr.exclude_kernel)
		config |= ARCH_PERFMON_EVENTSEL_OS;
	if (!event->attr.exclude_user)
		config |= ARCH_PERFMON_EVENTSEL_USR;

	bts_config_buffer(buf);

	/*
	 * local barrier to make sure that ds configuration made it
	 * before we enable BTS
	 */
	wmb();

	intel_pmu_enable_bts(config);
}

static void bts_event_start(struct perf_event *event, int flags)
{
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);

	__bts_event_start(event);

	/* PMI handler: this counter is running and likely generating PMIs */
	ACCESS_ONCE(bts->started) = 1;
}

static void __bts_event_stop(struct perf_event *event)
{
	/*
	 * No extra synchronization is mandated by the documentation to have
	 * BTS data stores globally visible.
	 */
	intel_pmu_disable_bts();

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	ACCESS_ONCE(event->hw.state) |= PERF_HES_STOPPED;
}

static void bts_event_stop(struct perf_event *event, int flags)
{
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);

	/* PMI handler: don't restart this counter */
	ACCESS_ONCE(bts->started) = 0;

	__bts_event_stop(event);

	if (flags & PERF_EF_UPDATE)
		bts_update(bts);
}

void intel_bts_enable_local(void)
{
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);

	if (bts->handle.event && bts->started)
		__bts_event_start(bts->handle.event);
}

void intel_bts_disable_local(void)
{
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);

	if (bts->handle.event)
		__bts_event_stop(bts->handle.event);
}

static int
bts_buffer_reset(struct bts_buffer *buf, struct perf_output_handle *handle)
{
	unsigned long head, space, next_space, pad, gap, skip, wakeup;
	unsigned int next_buf;
	struct bts_phys *phys, *next_phys;
	int ret;

	if (buf->snapshot)
		return 0;

	head = handle->head & ((buf->nr_pages << PAGE_SHIFT) - 1);
	if (WARN_ON_ONCE(head != local_read(&buf->head)))
		return -EINVAL;

	phys = &buf->buf[buf->cur_buf];
	space = phys->offset + phys->displacement + phys->size - head;
	pad = space;
	if (space > handle->size) {
		space = handle->size;
		space -= space % BTS_RECORD_SIZE;
	}
	if (space <= BTS_SAFETY_MARGIN) {
		/* See if next phys buffer has more space */
		next_buf = buf->cur_buf + 1;
		if (next_buf >= buf->nr_bufs)
			next_buf = 0;
		next_phys = &buf->buf[next_buf];
		gap = buf_size(phys->page) - phys->displacement - phys->size +
		      next_phys->displacement;
		skip = pad + gap;
		if (handle->size >= skip) {
			next_space = next_phys->size;
			if (next_space + skip > handle->size) {
				next_space = handle->size - skip;
				next_space -= next_space % BTS_RECORD_SIZE;
			}
			if (next_space > space || !space) {
				if (pad)
					bts_buffer_pad_out(phys, head);
				ret = perf_aux_output_skip(handle, skip);
				if (ret)
					return ret;
				/* Advance to next phys buffer */
				phys = next_phys;
				space = next_space;
				head = phys->offset + phys->displacement;
				/*
				 * After this, cur_buf and head won't match ds
				 * anymore, so we must not be racing with
				 * bts_update().
				 */
				buf->cur_buf = next_buf;
				local_set(&buf->head, head);
			}
		}
	}

	/* Don't go far beyond wakeup watermark */
	wakeup = BTS_SAFETY_MARGIN + BTS_RECORD_SIZE + handle->wakeup -
		 handle->head;
	if (space > wakeup) {
		space = wakeup;
		space -= space % BTS_RECORD_SIZE;
	}

	buf->end = head + space;

	/*
	 * If we have no space, the lost notification would have been sent when
	 * we hit absolute_maximum - see bts_update()
	 */
	if (!space)
		return -ENOSPC;

	return 0;
}

int intel_bts_interrupt(void)
{
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);
	struct perf_event *event = bts->handle.event;
	struct bts_buffer *buf;
	s64 old_head;
	int err;

	if (!event || !bts->started)
		return 0;

	buf = perf_get_aux(&bts->handle);
	/*
	 * Skip snapshot counters: they don't use the interrupt, but
	 * there's no other way of telling, because the pointer will
	 * keep moving
	 */
	if (!buf || buf->snapshot)
		return 0;

	old_head = local_read(&buf->head);
	bts_update(bts);

	/* no new data */
	if (old_head == local_read(&buf->head))
		return 0;

	perf_aux_output_end(&bts->handle, local_xchg(&buf->data_size, 0),
			    !!local_xchg(&buf->lost, 0));

	buf = perf_aux_output_begin(&bts->handle, event);
	if (!buf)
		return 1;

	err = bts_buffer_reset(buf, &bts->handle);
	if (err)
		perf_aux_output_end(&bts->handle, 0, false);

	return 1;
}

static void bts_event_del(struct perf_event *event, int mode)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);
	struct bts_buffer *buf = perf_get_aux(&bts->handle);

	bts_event_stop(event, PERF_EF_UPDATE);

	if (buf) {
		if (buf->snapshot)
			bts->handle.head =
				local_xchg(&buf->data_size,
					   buf->nr_pages << PAGE_SHIFT);
		perf_aux_output_end(&bts->handle, local_xchg(&buf->data_size, 0),
				    !!local_xchg(&buf->lost, 0));
	}

	cpuc->ds->bts_index = bts->ds_back.bts_buffer_base;
	cpuc->ds->bts_buffer_base = bts->ds_back.bts_buffer_base;
	cpuc->ds->bts_absolute_maximum = bts->ds_back.bts_absolute_maximum;
	cpuc->ds->bts_interrupt_threshold = bts->ds_back.bts_interrupt_threshold;
}

static int bts_event_add(struct perf_event *event, int mode)
{
	struct bts_buffer *buf;
	struct bts_ctx *bts = this_cpu_ptr(&bts_ctx);
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int ret = -EBUSY;

	event->hw.state = PERF_HES_STOPPED;

	if (test_bit(INTEL_PMC_IDX_FIXED_BTS, cpuc->active_mask))
		return -EBUSY;

	if (bts->handle.event)
		return -EBUSY;

	buf = perf_aux_output_begin(&bts->handle, event);
	if (!buf)
		return -EINVAL;

	ret = bts_buffer_reset(buf, &bts->handle);
	if (ret) {
		perf_aux_output_end(&bts->handle, 0, false);
		return ret;
	}

	bts->ds_back.bts_buffer_base = cpuc->ds->bts_buffer_base;
	bts->ds_back.bts_absolute_maximum = cpuc->ds->bts_absolute_maximum;
	bts->ds_back.bts_interrupt_threshold = cpuc->ds->bts_interrupt_threshold;

	if (mode & PERF_EF_START) {
		bts_event_start(event, 0);
		if (hwc->state & PERF_HES_STOPPED) {
			bts_event_del(event, 0);
			return -EBUSY;
		}
	}

	return 0;
}

static void bts_event_destroy(struct perf_event *event)
{
	x86_release_hardware();
	x86_del_exclusive(x86_lbr_exclusive_bts);
}

static int bts_event_init(struct perf_event *event)
{
	int ret;

	if (event->attr.type != bts_pmu.type)
		return -ENOENT;

	if (x86_add_exclusive(x86_lbr_exclusive_bts))
		return -EBUSY;

	/*
	 * BTS leaks kernel addresses even when CPL0 tracing is
	 * disabled, so disallow intel_bts driver for unprivileged
	 * users on paranoid systems since it provides trace data
	 * to the user in a zero-copy fashion.
	 *
	 * Note that the default paranoia setting permits unprivileged
	 * users to profile the kernel.
	 */
	if (event->attr.exclude_kernel && perf_paranoid_kernel() &&
	    !capable(CAP_SYS_ADMIN))
		return -EACCES;

	ret = x86_reserve_hardware();
	if (ret) {
		x86_del_exclusive(x86_lbr_exclusive_bts);
		return ret;
	}

	event->destroy = bts_event_destroy;

	return 0;
}

static void bts_event_read(struct perf_event *event)
{
}

static __init int bts_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_DTES64) || !x86_pmu.bts)
		return -ENODEV;

	bts_pmu.capabilities	= PERF_PMU_CAP_AUX_NO_SG | PERF_PMU_CAP_ITRACE;
	bts_pmu.task_ctx_nr	= perf_sw_context;
	bts_pmu.event_init	= bts_event_init;
	bts_pmu.add		= bts_event_add;
	bts_pmu.del		= bts_event_del;
	bts_pmu.start		= bts_event_start;
	bts_pmu.stop		= bts_event_stop;
	bts_pmu.read		= bts_event_read;
	bts_pmu.setup_aux	= bts_buffer_setup_aux;
	bts_pmu.free_aux	= bts_buffer_free_aux;

	return perf_pmu_register(&bts_pmu, "intel_bts", -1);
}
arch_initcall(bts_init);

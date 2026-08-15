// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "drm/drm_file.h"
#include "drm/msm_drm.h"

#include "linux/anon_inodes.h"
#include "linux/gfp_types.h"
#include "linux/poll.h"
#include "linux/slab.h"

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_perfcntr.h"

#include "adreno/adreno_gpu.h"

/* space used: */
#define fifo_count(stream) \
	(CIRC_CNT((stream)->fifo.head, (stream)->fifo.tail, (stream)->fifo_size))
#define fifo_count_to_end(stream) \
	(CIRC_CNT_TO_END(smp_load_acquire(&(stream)->fifo.head), (stream)->fifo.tail, (stream)->fifo_size))
/* space available: */
#define fifo_space(stream) \
	(CIRC_SPACE((stream)->fifo.head, (stream)->fifo.tail, (stream)->fifo_size))

static int
msm_perfcntr_resume_locked(struct msm_perfcntr_stream *stream)
{
	if (!stream)
		return 0;

	/* Reprogram SEL regs on highest priority rb: */
	struct msm_ringbuffer *ring = stream->gpu->rb[0];

	queue_work(ring->sched.submit_wq, &stream->sel_work);

	hrtimer_start(&stream->sample_timer,
		      ns_to_ktime(stream->sample_period_ns),
		      HRTIMER_MODE_REL_PINNED);

	return 0;
}

int
msm_perfcntr_resume(struct msm_gpu *gpu)
{
	if (!gpu->perfcntrs)
		return 0;
	guard(mutex)(&gpu->perfcntr_lock);
	return msm_perfcntr_resume_locked(gpu->perfcntrs->stream);
}

static void
msm_perfcntr_suspend_locked(struct msm_perfcntr_stream *stream)
{
	if (!stream)
		return;

	hrtimer_cancel(&stream->sample_timer);
	kthread_cancel_work_sync(&stream->sample_work);

	/*
	 * We can't use cancel_work_sync() here, since sel_work acquires
	 * gpu->lock which (a) in suspend path can already be held, or
	 * (b) in release path would invert the order of gpu->lock and
	 * gpu->perfcntr_lock.  Either would cause deadlock.
	 */
	cancel_work(&stream->sel_work);

	stream->sel_fence = ++stream->gpu->perfcntrs->sel_seqno;
	stream->seqno = 0;
}

void
msm_perfcntr_suspend(struct msm_gpu *gpu)
{
	if (!gpu->perfcntrs)
		return;
	guard(mutex)(&gpu->perfcntr_lock);
	msm_perfcntr_suspend_locked(gpu->perfcntrs->stream);
}

static int
msm_perfcntrs_stream_release(struct inode *inode, struct file *file)
{
	struct msm_perfcntr_stream *stream = file->private_data;
	struct msm_gpu *gpu = stream->gpu;

	scoped_guard (mutex, &gpu->perfcntr_lock) {
		struct msm_perfcntr_state *perfcntrs = gpu->perfcntrs;

		msm_perfcntr_suspend_locked(stream);
		perfcntrs->stream = NULL;

		/* release previously allocated counters: */
		for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++)
			perfcntrs->groups[i]->allocated_counters = 0;
	}

	/*
	 * In the suspend path we use async cancel_work(), to avoid blocking
	 * on sel_work, which acquires gpu->lock (which could deadlock since
	 * other paths acquire gpu->lock before perfcntr_lock) or already
	 * hold gpu->lock.
	 *
	 * But since we are freeing the stream, after dropping perfcntr_lock
	 * we need to block until sel_work is done:
	 */
	cancel_work_sync(&stream->sel_work);

	kfree(stream->group_idx);
	kfree(stream->fifo.buf);
	kfree(stream);

	return 0;
}

static __poll_t
msm_perfcntrs_stream_poll(struct file *file, poll_table *wait)
{
	struct msm_perfcntr_stream *stream = file->private_data;
	__poll_t events = 0;

	poll_wait(file, &stream->poll_wq, wait);

	/* Are there samples to read? */
	if (fifo_count(stream) > 0)
		events |= EPOLLIN;

	return events;
}

static ssize_t
msm_perfcntrs_stream_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct msm_perfcntr_stream *stream = file->private_data;
	int ret;

	if (!(file->f_flags & O_NONBLOCK)) {
		ret = wait_event_interruptible(stream->poll_wq,
					       fifo_count(stream) > 0);
		if (ret)
			return ret;
	}

	guard(mutex)(&stream->read_lock);

	struct circ_buf *fifo = &stream->fifo;
	const char *fptr = &fifo->buf[fifo->tail];

	count = min_t(size_t, count, fifo_count_to_end(stream));
	if (!count)
		return -EAGAIN;
	if (copy_to_user(buf, fptr, count))
		return -EFAULT;

	smp_store_release(&fifo->tail, (fifo->tail + count) & (stream->fifo_size - 1));
	*ppos += count;

	return count;
}

static const struct file_operations stream_fops = {
	.owner		= THIS_MODULE,
	.release	= msm_perfcntrs_stream_release,
	.poll		= msm_perfcntrs_stream_poll,
	.read		= msm_perfcntrs_stream_read,
};

static void
sel_worker(struct work_struct *w)
{
	struct msm_perfcntr_stream *stream =
		container_of(w, typeof(*stream), sel_work);
	struct msm_gpu *gpu = stream->gpu;
	/* Reprogram SEL regs on highest priority rb: */
	struct msm_ringbuffer *ring = stream->gpu->rb[0];

	/*
	 * If in the process of resuming, wait for that.  Otherwise sel_worker
	 * which is enqueued in the resume path can be scheduled before the
	 * resume completes.
	 */
	pm_runtime_barrier(&gpu->pdev->dev);

	/*
	 * sel_work could end up scheduled before suspend, but running
	 * after.  See msm_perfcntr_suspend_locked()
	 *
	 * So if we end up running sel_work after the GPU is already
	 * suspended, just bail.  It will be scheduled again after
	 * the GPU is resumed.
	 */
	if (!pm_runtime_get_if_active(&gpu->pdev->dev))
		return;

	scoped_guard (mutex, &gpu->lock) {
		guard(mutex)(&gpu->perfcntr_lock);

		if (stream == gpu->perfcntrs->stream) {
			msm_gpu_hw_init(gpu);
			gpu->funcs->perfcntr_configure(gpu, ring, stream);
		}
	}

	pm_runtime_put_autosuspend(&gpu->pdev->dev);
}

static void
sample_write(struct msm_perfcntr_stream *stream, int *head, const void *buf, size_t sz)
{
	/*
	 * FIFO size is power-of-two, and guaranteed to have enough space to
	 * fit what we are writing.  So we should not hit the wrap-around
	 * point writing things that are power-of-two sized
	 */
	WARN_ON(CIRC_SPACE_TO_END(*head, stream->fifo.tail, stream->fifo_size) < sz);

	memcpy(&stream->fifo.buf[*head], buf, sz);

	/* Advance head, wrapping around if necessary: */
	*head = (*head + sz) & (stream->fifo_size - 1);
}

static void
sample_write_u32(struct msm_perfcntr_stream *stream, int *head, uint32_t val)
{
	sample_write(stream, head, &val, sizeof(val));
}

static void
sample_write_u64(struct msm_perfcntr_stream *stream, int *head, uint64_t val)
{
	sample_write(stream, head, &val, sizeof(val));
}

static void
sample_worker(struct kthread_work *work)
{
	struct msm_perfcntr_stream *stream =
		container_of(work, typeof(*stream), sample_work);
	struct msm_gpu *gpu = stream->gpu;
	struct msm_rbmemptrs *memptrs = gpu->rb[0]->memptrs;

	if (memptrs->perfcntr_fence != stream->sel_fence)
		return;

	/*
	 * Ensure we have enough space to capture a sample period's
	 * worth of data:
	 */
	if (stream->period_size > fifo_space(stream)) {
		stream->seqno = 0;
		return;
	}

	/* Inhibit IFPC while accessing registers: */
	if (gpu->funcs->sysprof_setup)
		gpu->funcs->sysprof_setup(gpu, true);

	if (gpu->funcs->perfcntr_flush)
		gpu->funcs->perfcntr_flush(gpu);

	/* Keep local copy of head to avoid updating fifo until the end: */
	int head = stream->fifo.head;

	/*
	 * We expect the GPU to be powered at this point, as the timer
	 * and kthread work are canceled/flushed in the suspend path:
	 */
	sample_write_u64(stream, &head,
			 to_adreno_gpu(gpu)->funcs->get_timestamp(gpu));
	sample_write_u32(stream, &head, stream->seqno++);
	sample_write_u32(stream, &head, 0);

	for (unsigned i = 0; i < stream->nr_groups; i++) {
		unsigned group_idx = msm_perfcntr_group_idx(stream, i);
		unsigned base = msm_perfcntr_counter_base(stream, group_idx);

		const struct msm_perfcntr_group *group =
			&gpu->perfcntr_groups[group_idx];

		struct msm_perfcntr_group_state *group_state =
			gpu->perfcntrs->groups[group_idx];

		unsigned nr = group_state->allocated_counters;
		for (unsigned j = 0; j < nr; j++) {
			const struct msm_perfcntr_counter *counter =
				&group->counters[j + base];
			uint64_t val = gpu_read64(gpu, counter->counter_reg_lo);
			sample_write_u64(stream, &head, val);
		}
	}

	/* Re-enable IFPC: */
	if (gpu->funcs->sysprof_setup)
		gpu->funcs->sysprof_setup(gpu, false);

	smp_store_release(&stream->fifo.head, head);
	wake_up_all(&stream->poll_wq);
}

static enum hrtimer_restart
sample_timer(struct hrtimer *hrtimer)
{
	struct msm_perfcntr_stream *stream =
		container_of(hrtimer, typeof(*stream), sample_timer);

	kthread_queue_work(stream->gpu->worker, &stream->sample_work);

	hrtimer_forward_now(hrtimer, ns_to_ktime(stream->sample_period_ns));

	return HRTIMER_RESTART;
}

static int
get_group_idx(struct msm_gpu *gpu, const char *name, size_t len)
{
	for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++) {
		const struct msm_perfcntr_group *group =
			&gpu->perfcntr_groups[i];
		if (!strncmp(group->name, name, len))
			return i;
	}

	return -1;
}

static int
get_available_counters(struct msm_gpu *gpu, int group_idx, uint32_t flags)
{
	struct msm_perfcntr_state *perfcntrs = gpu->perfcntrs;

	/*
	 * For local counter reservation, anything that is not used by
	 * global perfcntr stream is available:
	 */
	if (!(flags & MSM_PERFCNTR_STREAM)) {
		return gpu->perfcntr_groups[group_idx].num_counters -
			perfcntrs->groups[group_idx]->allocated_counters;
	}

	/*
	 * For global counter collection, anything that is not reserved by
	 * one or more contexts is available:
	 */
	guard(mutex)(&gpu->dev->filelist_mutex);

	unsigned reserved_counters = 0;
	struct drm_file *file;

	list_for_each_entry (file, &gpu->dev->filelist, lhead) {
		struct msm_context *ctx = file->driver_priv;

		if (!ctx || !ctx->perfctx)
			continue;

		unsigned n = ctx->perfctx->reserved_counters[group_idx];
		reserved_counters = max(reserved_counters, n);
	}

	return gpu->perfcntr_groups[group_idx].num_counters - reserved_counters;
}

int
msm_ioctl_perfcntr_config(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct msm_drm_private *priv = dev->dev_private;
	const struct drm_msm_perfcntr_config *args = data;
	struct msm_context *ctx = file->driver_priv;
	struct msm_gpu *gpu = priv->gpu;
	int stream_fd = 0;

	if (!gpu || !gpu->num_perfcntr_groups)
		return -ENXIO;

	struct msm_perfcntr_state *perfcntrs = gpu->perfcntrs;

	/*
	 * Validate args that don't require locks/power first:
	 */

	if (args->flags & ~MSM_PERFCNTR_FLAGS)
		return UERR(EINVAL, dev, "invalid flags");

	if (args->nr_groups && !args->group_stride)
		return UERR(EINVAL, dev, "invalid group_stride");

	if (args->nr_groups > gpu->num_perfcntr_groups)
		return UERR(EINVAL, dev, "too many groups");

	if (args->nr_groups && !args->groups)
		return UERR(EINVAL, dev, "no groups");

	if (args->flags & MSM_PERFCNTR_STREAM) {
		if (!perfmon_capable())
			return UERR(EPERM, dev, "invalid permissions");
		if (!args->nr_groups)
			return UERR(EINVAL, dev, "invalid nr_groups");
		if (!args->period)
			return UERR(EINVAL, dev, "invalid sampling period");
		if (args->bufsz_shift > const_ilog2(SZ_128M))
			return UERR(EINVAL, dev, "buffer size too big (>128M)");
	} else {
		if (args->period)
			return UERR(EINVAL, dev, "sampling period not allowed");
		if (args->bufsz_shift)
			return UERR(EINVAL, dev, "sample buf size not allowed");
	}

	/*
	 * To avoid iterating over the groups multiple times, allocate and setup
	 * both a ctx and global stream object.  Only one of the two will be
	 * kept in the end.
	 */

	struct msm_perfcntr_context_state *perfctx __free(kfree) = kzalloc(
		struct_size(perfctx, reserved_counters, gpu->num_perfcntr_groups),
		GFP_KERNEL);
	if (!perfctx)
		return -ENOMEM;

	struct msm_perfcntr_stream *stream __free(kfree) = kzalloc_obj(*stream);
	if (!stream)
		return -ENOMEM;

	uint8_t *nr_counters __free(kfree) = kzalloc_objs(uint8_t, gpu->num_perfcntr_groups);
	if (!nr_counters)
		return -ENOMEM;

	uint32_t *group_idx __free(kfree) = kzalloc_objs(uint32_t, args->nr_groups);
	if (!group_idx)
		return -ENOMEM;

	stream->gpu = gpu;
	stream->sample_period_ns = args->period;
	stream->nr_groups = args->nr_groups;
	stream->fifo_size = 1ull << args->bufsz_shift;

	mutex_init(&stream->read_lock);

	guard(mutex)(&gpu->perfcntr_lock);

	if (args->flags & MSM_PERFCNTR_STREAM) {
		if (perfcntrs->stream)
			return UERR(EBUSY, dev, "perfcntr stream already open");
	}

	size_t bufsz = 16;  /* header size includes seqno and 64b timestamp: */
	int ret = 0;

	for (unsigned i = 0; i < args->nr_groups; i++) {
		struct drm_msm_perfcntr_group g = {0};
		size_t sz = min_t(size_t, args->group_stride, sizeof(g));
		void __user *userptr =
			u64_to_user_ptr(args->groups + (i * args->group_stride));

		if (copy_from_user(&g, userptr, sz))
			return -EFAULT;

		if (g.pad)
			return UERR(EINVAL, dev, "groups[%d]: invalid pad", i);

		int idx = get_group_idx(gpu, g.group_name, sizeof(g.group_name));

		if (idx < 0)
			return UERR(EINVAL, dev, "groups[%d]: unknown group", i);

		if (nr_counters[idx])
			return UERR(EINVAL, dev, "groups[%d]: duplicate group", i);

		if (g.nr_countables > gpu->perfcntr_groups[idx].num_counters)
			return UERR(EINVAL, dev, "groups[%d]: too many counters", i);

		if (args->flags & MSM_PERFCNTR_STREAM) {
			if (g.nr_countables && !g.countables)
				return UERR(EINVAL, dev, "groups[%d]: no countables", i);
		} else {
			if (g.countables)
				return UERR(EINVAL, dev, "groups[%d]: countables should be NULL", i);
		}

		int avail_counters = get_available_counters(gpu, idx, args->flags);
		if (g.nr_countables > avail_counters) {
			/*
			 * Defer error return until we process all groups, in
			 * case there are other E2BIG groups:
			 */
			ret = UERR(E2BIG, dev, "groups[%d]: too few counters available", i);

			if (args->flags & MSM_PERFCNTR_UPDATE) {
				/* Let userspace know how many counters are actually avail: */
				g.nr_countables = avail_counters;
				if (copy_to_user(userptr, &g, sz))
					return -EFAULT;
			}
		}

		group_idx[i] = idx;
		perfctx->reserved_counters[idx] = g.nr_countables;

		/* +1 to catch duplicate zero sized groups: */
		nr_counters[idx] = g.nr_countables + 1;

		if (args->flags & MSM_PERFCNTR_STREAM) {
			size_t sz = sizeof(uint32_t) * g.nr_countables;
			void __user *userptr = u64_to_user_ptr(g.countables);

			if (copy_from_user(perfcntrs->groups[idx]->countables, userptr, sz))
				return -EFAULT;

			/* Samples are 64b per countable: */
			bufsz += 2 * sz;
		}
	}

	if (ret)
		return ret;

	if (args->flags & MSM_PERFCNTR_STREAM) {
		/*
		 * Validate requested buffer size is large enough for at least
		 * a single sample period.
		 *
		 * Note the circ_buf implementation needs to be 1 byte larger
		 * than max it can hold (see CIRC_SPACE()).
		 */
		if (stream->fifo_size <= bufsz)
			return UERR(EINVAL, dev, "required buffer size: %zu", bufsz);

		/* There aren't enough counters to hit this limit: */
		WARN_ON(bufsz > SZ_128M);

		stream->period_size = bufsz;

		void *buf __free(kfree) = kmalloc(stream->fifo_size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		FD_PREPARE(fdf, O_CLOEXEC,
			   anon_inode_getfile("[msm_perfcntrs]", &stream_fops, stream, 0));
		if (fdf.err)
			return fdf.err;

		INIT_WORK(&stream->sel_work, sel_worker);
		kthread_init_work(&stream->sample_work, sample_worker);
		init_waitqueue_head(&stream->poll_wq);
		hrtimer_setup(&stream->sample_timer, sample_timer,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);

		stream->sel_fence = ++perfcntrs->sel_seqno;
		stream->group_idx = no_free_ptr(group_idx);
		stream->fifo.buf = no_free_ptr(buf);

		/* commit the allocated counters, subtracting off original +1: */
		for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++)
			perfcntrs->groups[i]->allocated_counters = nr_counters[i] - 1;

		perfcntrs->stream = no_free_ptr(stream);

		msm_perfcntr_resume_locked(perfcntrs->stream);

		stream_fd = fd_publish(fdf);
	} else {
		kfree(ctx->perfctx);
		ctx->perfctx = no_free_ptr(perfctx);
	}

	return stream_fd;
}

/**
 * msm_perfcntr_group_idx - map idx of perfcntr group to group_idx
 * @stream: The global perfcntr stream
 * @n: The requested group_idx
 *
 * The PERFCNTR_CONFIG ioctl requested N counters/countables per perfcntr
 * group, but the order of groups is not required to match the order they
 * are defined in the perfcntr tables (which is not stable/UABI, only the
 * group names are UABI).
 *
 * But the order samples are returned in the stream should match the
 * order they are requested in the PERFCNTR_CONFIG ioctl.  This helper
 * handles the order remapping.
 *
 * Returns an index into gpu->perfcntr_groups[] and perfcntrs->groups[].
 */
uint32_t
msm_perfcntr_group_idx(const struct msm_perfcntr_stream *stream, uint32_t n)
{
	WARN_ON_ONCE(n >= stream->nr_groups);
	return stream->group_idx[n];
}

/**
 * msm_perfcntr_counter_base - get idx of the first counter in group
 * @stream: The global perfcntr stream
 * @group_idx: the index of the counter group
 *
 * For global counter collection, counters are allocated from the end
 * (last counter) while UMD allocates them from the start (0..N-1).
 * Since UMD always allocated them from the start this also minimizes
 * the chance of conflict when using old UMD which predates
 * PERFCNTR_CONFIG ioctl.
 *
 * Returns the index of first counter to use.  An index into
 * msm_perfcntr_group::counters[].
 */
uint32_t
msm_perfcntr_counter_base(const struct msm_perfcntr_stream *stream, uint32_t group_idx)
{
	struct msm_gpu *gpu = stream->gpu;
	struct msm_perfcntr_state *perfcntrs = gpu->perfcntrs;
	unsigned num_counters = gpu->perfcntr_groups[group_idx].num_counters;
	unsigned allocated_counters = perfcntrs->groups[group_idx]->allocated_counters;

	return num_counters - allocated_counters;
}

static void
__msm_perfcntr_cleanup(struct msm_gpu *gpu, struct msm_perfcntr_state *perfcntrs)
{
	struct device *dev = &gpu->pdev->dev;

	for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++)
		devm_kfree(dev, perfcntrs->groups[i]);

	devm_kfree(dev, perfcntrs);
}

void
msm_perfcntr_cleanup(struct msm_gpu *gpu)
{
	if (!gpu->perfcntrs)
		return;

	__msm_perfcntr_cleanup(gpu, gpu->perfcntrs);
	gpu->perfcntrs = NULL;
}

struct msm_perfcntr_state *
msm_perfcntr_init(struct msm_gpu *gpu)
{
	struct msm_perfcntr_state *perfcntrs;
	struct device *dev = &gpu->pdev->dev;
	size_t sz;

	sz = struct_size(perfcntrs, groups, gpu->num_perfcntr_groups);
	perfcntrs = devm_kzalloc(dev, sz, GFP_KERNEL);
	if (!perfcntrs)
		return ERR_PTR(-ENOMEM);

	for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++) {
		const struct msm_perfcntr_group *group =
			&gpu->perfcntr_groups[i];

		sz = struct_size(perfcntrs->groups[i], countables, group->num_counters);
		perfcntrs->groups[i] = devm_kzalloc(dev, sz, GFP_KERNEL);
		if (!perfcntrs->groups[i]) {
			__msm_perfcntr_cleanup(gpu, perfcntrs);
			return ERR_PTR(-ENOMEM);
		}
	}

	return perfcntrs;
}

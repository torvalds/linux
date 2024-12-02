// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2018 Broadcom */

/**
 * DOC: Broadcom V3D scheduling
 *
 * The shared DRM GPU scheduler is used to coordinate submitting jobs
 * to the hardware.  Each DRM fd (roughly a client process) gets its
 * own scheduler entity, which will process jobs in order.  The GPU
 * scheduler will round-robin between clients to submit the next job.
 *
 * For simplicity, and in order to keep latency low for interactive
 * jobs when bulk background jobs are queued up, we submit a new job
 * to the HW only when it has completed the last one, instead of
 * filling up the CT[01]Q FIFOs with jobs.  Similarly, we use
 * drm_sched_job_add_dependency() to manage the dependency between bin and
 * render, instead of having the clients submit jobs using the HW's
 * semaphores to interlock between them.
 */

#include <linux/sched/clock.h>
#include <linux/kthread.h>

#include <drm/drm_syncobj.h>

#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

#define V3D_CSD_CFG012_WG_COUNT_SHIFT 16

static struct v3d_job *
to_v3d_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_job, base);
}

static struct v3d_bin_job *
to_bin_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_bin_job, base.base);
}

static struct v3d_render_job *
to_render_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_render_job, base.base);
}

static struct v3d_tfu_job *
to_tfu_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_tfu_job, base.base);
}

static struct v3d_csd_job *
to_csd_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_csd_job, base.base);
}

static struct v3d_cpu_job *
to_cpu_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_cpu_job, base.base);
}

static void
v3d_sched_job_free(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);

	v3d_job_cleanup(job);
}

void
v3d_timestamp_query_info_free(struct v3d_timestamp_query_info *query_info,
			      unsigned int count)
{
	if (query_info->queries) {
		unsigned int i;

		for (i = 0; i < count; i++)
			drm_syncobj_put(query_info->queries[i].syncobj);

		kvfree(query_info->queries);
	}
}

void
v3d_performance_query_info_free(struct v3d_performance_query_info *query_info,
				unsigned int count)
{
	if (query_info->queries) {
		unsigned int i;

		for (i = 0; i < count; i++) {
			drm_syncobj_put(query_info->queries[i].syncobj);
			kvfree(query_info->queries[i].kperfmon_ids);
		}

		kvfree(query_info->queries);
	}
}

static void
v3d_cpu_job_free(struct drm_sched_job *sched_job)
{
	struct v3d_cpu_job *job = to_cpu_job(sched_job);

	v3d_timestamp_query_info_free(&job->timestamp_query,
				      job->timestamp_query.count);

	v3d_performance_query_info_free(&job->performance_query,
					job->performance_query.count);

	v3d_job_cleanup(&job->base);
}

static void
v3d_switch_perfmon(struct v3d_dev *v3d, struct v3d_job *job)
{
	struct v3d_perfmon *perfmon = v3d->global_perfmon;

	if (!perfmon)
		perfmon = job->perfmon;

	if (perfmon == v3d->active_perfmon)
		return;

	if (perfmon != v3d->active_perfmon)
		v3d_perfmon_stop(v3d, v3d->active_perfmon, true);

	if (perfmon && v3d->active_perfmon != perfmon)
		v3d_perfmon_start(v3d, perfmon);
}

static void
v3d_job_start_stats(struct v3d_job *job, enum v3d_queue queue)
{
	struct v3d_dev *v3d = job->v3d;
	struct v3d_file_priv *file = job->file->driver_priv;
	struct v3d_stats *global_stats = &v3d->queue[queue].stats;
	struct v3d_stats *local_stats = &file->stats[queue];
	u64 now = local_clock();
	unsigned long flags;

	/*
	 * We only need to disable local interrupts to appease lockdep who
	 * otherwise would think v3d_job_start_stats vs v3d_stats_update has an
	 * unsafe in-irq vs no-irq-off usage problem. This is a false positive
	 * because all the locks are per queue and stats type, and all jobs are
	 * completely one at a time serialised. More specifically:
	 *
	 * 1. Locks for GPU queues are updated from interrupt handlers under a
	 *    spin lock and started here with preemption disabled.
	 *
	 * 2. Locks for CPU queues are updated from the worker with preemption
	 *    disabled and equally started here with preemption disabled.
	 *
	 * Therefore both are consistent.
	 *
	 * 3. Because next job can only be queued after the previous one has
	 *    been signaled, and locks are per queue, there is also no scope for
	 *    the start part to race with the update part.
	 */
	if (IS_ENABLED(CONFIG_LOCKDEP))
		local_irq_save(flags);
	else
		preempt_disable();

	write_seqcount_begin(&local_stats->lock);
	local_stats->start_ns = now;
	write_seqcount_end(&local_stats->lock);

	write_seqcount_begin(&global_stats->lock);
	global_stats->start_ns = now;
	write_seqcount_end(&global_stats->lock);

	if (IS_ENABLED(CONFIG_LOCKDEP))
		local_irq_restore(flags);
	else
		preempt_enable();
}

static void
v3d_stats_update(struct v3d_stats *stats, u64 now)
{
	write_seqcount_begin(&stats->lock);
	stats->enabled_ns += now - stats->start_ns;
	stats->jobs_completed++;
	stats->start_ns = 0;
	write_seqcount_end(&stats->lock);
}

void
v3d_job_update_stats(struct v3d_job *job, enum v3d_queue queue)
{
	struct v3d_dev *v3d = job->v3d;
	struct v3d_file_priv *file = job->file->driver_priv;
	struct v3d_stats *global_stats = &v3d->queue[queue].stats;
	struct v3d_stats *local_stats = &file->stats[queue];
	u64 now = local_clock();
	unsigned long flags;

	/* See comment in v3d_job_start_stats() */
	if (IS_ENABLED(CONFIG_LOCKDEP))
		local_irq_save(flags);
	else
		preempt_disable();

	v3d_stats_update(local_stats, now);
	v3d_stats_update(global_stats, now);

	if (IS_ENABLED(CONFIG_LOCKDEP))
		local_irq_restore(flags);
	else
		preempt_enable();
}

static struct dma_fence *v3d_bin_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_bin_job *job = to_bin_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;
	unsigned long irqflags;

	if (unlikely(job->base.base.s_fence->finished.error))
		return NULL;

	/* Lock required around bin_job update vs
	 * v3d_overflow_mem_work().
	 */
	spin_lock_irqsave(&v3d->job_lock, irqflags);
	v3d->bin_job = job;
	/* Clear out the overflow allocation, so we don't
	 * reuse the overflow attached to a previous job.
	 */
	V3D_CORE_WRITE(0, V3D_PTB_BPOS, 0);
	spin_unlock_irqrestore(&v3d->job_lock, irqflags);

	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, V3D_BIN);
	if (IS_ERR(fence))
		return NULL;

	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_cl(dev, false, to_v3d_fence(fence)->seqno,
			    job->start, job->end);

	v3d_job_start_stats(&job->base, V3D_BIN);
	v3d_switch_perfmon(v3d, &job->base);

	/* Set the current and end address of the control list.
	 * Writing the end register is what starts the job.
	 */
	if (job->qma) {
		V3D_CORE_WRITE(0, V3D_CLE_CT0QMA, job->qma);
		V3D_CORE_WRITE(0, V3D_CLE_CT0QMS, job->qms);
	}
	if (job->qts) {
		V3D_CORE_WRITE(0, V3D_CLE_CT0QTS,
			       V3D_CLE_CT0QTS_ENABLE |
			       job->qts);
	}
	V3D_CORE_WRITE(0, V3D_CLE_CT0QBA, job->start);
	V3D_CORE_WRITE(0, V3D_CLE_CT0QEA, job->end);

	return fence;
}

static struct dma_fence *v3d_render_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_render_job *job = to_render_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;

	if (unlikely(job->base.base.s_fence->finished.error))
		return NULL;

	v3d->render_job = job;

	/* Can we avoid this flush?  We need to be careful of
	 * scheduling, though -- imagine job0 rendering to texture and
	 * job1 reading, and them being executed as bin0, bin1,
	 * render0, render1, so that render1's flush at bin time
	 * wasn't enough.
	 */
	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, V3D_RENDER);
	if (IS_ERR(fence))
		return NULL;

	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_cl(dev, true, to_v3d_fence(fence)->seqno,
			    job->start, job->end);

	v3d_job_start_stats(&job->base, V3D_RENDER);
	v3d_switch_perfmon(v3d, &job->base);

	/* XXX: Set the QCFG */

	/* Set the current and end address of the control list.
	 * Writing the end register is what starts the job.
	 */
	V3D_CORE_WRITE(0, V3D_CLE_CT1QBA, job->start);
	V3D_CORE_WRITE(0, V3D_CLE_CT1QEA, job->end);

	return fence;
}

static struct dma_fence *
v3d_tfu_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_tfu_job *job = to_tfu_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;

	fence = v3d_fence_create(v3d, V3D_TFU);
	if (IS_ERR(fence))
		return NULL;

	v3d->tfu_job = job;
	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_tfu(dev, to_v3d_fence(fence)->seqno);

	v3d_job_start_stats(&job->base, V3D_TFU);

	V3D_WRITE(V3D_TFU_IIA(v3d->ver), job->args.iia);
	V3D_WRITE(V3D_TFU_IIS(v3d->ver), job->args.iis);
	V3D_WRITE(V3D_TFU_ICA(v3d->ver), job->args.ica);
	V3D_WRITE(V3D_TFU_IUA(v3d->ver), job->args.iua);
	V3D_WRITE(V3D_TFU_IOA(v3d->ver), job->args.ioa);
	if (v3d->ver >= 71)
		V3D_WRITE(V3D_V7_TFU_IOC, job->args.v71.ioc);
	V3D_WRITE(V3D_TFU_IOS(v3d->ver), job->args.ios);
	V3D_WRITE(V3D_TFU_COEF0(v3d->ver), job->args.coef[0]);
	if (v3d->ver >= 71 || (job->args.coef[0] & V3D_TFU_COEF0_USECOEF)) {
		V3D_WRITE(V3D_TFU_COEF1(v3d->ver), job->args.coef[1]);
		V3D_WRITE(V3D_TFU_COEF2(v3d->ver), job->args.coef[2]);
		V3D_WRITE(V3D_TFU_COEF3(v3d->ver), job->args.coef[3]);
	}
	/* ICFG kicks off the job. */
	V3D_WRITE(V3D_TFU_ICFG(v3d->ver), job->args.icfg | V3D_TFU_ICFG_IOC);

	return fence;
}

static struct dma_fence *
v3d_csd_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_csd_job *job = to_csd_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;
	int i, csd_cfg0_reg;

	v3d->csd_job = job;

	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, V3D_CSD);
	if (IS_ERR(fence))
		return NULL;

	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_csd(dev, to_v3d_fence(fence)->seqno);

	v3d_job_start_stats(&job->base, V3D_CSD);
	v3d_switch_perfmon(v3d, &job->base);

	csd_cfg0_reg = V3D_CSD_QUEUED_CFG0(v3d->ver);
	for (i = 1; i <= 6; i++)
		V3D_CORE_WRITE(0, csd_cfg0_reg + 4 * i, job->args.cfg[i]);

	/* Although V3D 7.1 has an eighth configuration register, we are not
	 * using it. Therefore, make sure it remains unused.
	 *
	 * XXX: Set the CFG7 register
	 */
	if (v3d->ver >= 71)
		V3D_CORE_WRITE(0, V3D_V7_CSD_QUEUED_CFG7, 0);

	/* CFG0 write kicks off the job. */
	V3D_CORE_WRITE(0, csd_cfg0_reg, job->args.cfg[0]);

	return fence;
}

static void
v3d_rewrite_csd_job_wg_counts_from_indirect(struct v3d_cpu_job *job)
{
	struct v3d_indirect_csd_info *indirect_csd = &job->indirect_csd;
	struct v3d_bo *bo = to_v3d_bo(job->base.bo[0]);
	struct v3d_bo *indirect = to_v3d_bo(indirect_csd->indirect);
	struct drm_v3d_submit_csd *args = &indirect_csd->job->args;
	u32 *wg_counts;

	v3d_get_bo_vaddr(bo);
	v3d_get_bo_vaddr(indirect);

	wg_counts = (uint32_t *)(bo->vaddr + indirect_csd->offset);

	if (wg_counts[0] == 0 || wg_counts[1] == 0 || wg_counts[2] == 0)
		return;

	args->cfg[0] = wg_counts[0] << V3D_CSD_CFG012_WG_COUNT_SHIFT;
	args->cfg[1] = wg_counts[1] << V3D_CSD_CFG012_WG_COUNT_SHIFT;
	args->cfg[2] = wg_counts[2] << V3D_CSD_CFG012_WG_COUNT_SHIFT;
	args->cfg[4] = DIV_ROUND_UP(indirect_csd->wg_size, 16) *
		       (wg_counts[0] * wg_counts[1] * wg_counts[2]) - 1;

	for (int i = 0; i < 3; i++) {
		/* 0xffffffff indicates that the uniform rewrite is not needed */
		if (indirect_csd->wg_uniform_offsets[i] != 0xffffffff) {
			u32 uniform_idx = indirect_csd->wg_uniform_offsets[i];
			((uint32_t *)indirect->vaddr)[uniform_idx] = wg_counts[i];
		}
	}

	v3d_put_bo_vaddr(indirect);
	v3d_put_bo_vaddr(bo);
}

static void
v3d_timestamp_query(struct v3d_cpu_job *job)
{
	struct v3d_timestamp_query_info *timestamp_query = &job->timestamp_query;
	struct v3d_bo *bo = to_v3d_bo(job->base.bo[0]);
	u8 *value_addr;

	v3d_get_bo_vaddr(bo);

	for (int i = 0; i < timestamp_query->count; i++) {
		value_addr = ((u8 *)bo->vaddr) + timestamp_query->queries[i].offset;
		*((u64 *)value_addr) = i == 0 ? ktime_get_ns() : 0ull;

		drm_syncobj_replace_fence(timestamp_query->queries[i].syncobj,
					  job->base.done_fence);
	}

	v3d_put_bo_vaddr(bo);
}

static void
v3d_reset_timestamp_queries(struct v3d_cpu_job *job)
{
	struct v3d_timestamp_query_info *timestamp_query = &job->timestamp_query;
	struct v3d_timestamp_query *queries = timestamp_query->queries;
	struct v3d_bo *bo = to_v3d_bo(job->base.bo[0]);
	u8 *value_addr;

	v3d_get_bo_vaddr(bo);

	for (int i = 0; i < timestamp_query->count; i++) {
		value_addr = ((u8 *)bo->vaddr) + queries[i].offset;
		*((u64 *)value_addr) = 0;

		drm_syncobj_replace_fence(queries[i].syncobj, NULL);
	}

	v3d_put_bo_vaddr(bo);
}

static void write_to_buffer_32(u32 *dst, unsigned int idx, u32 value)
{
	dst[idx] = value;
}

static void write_to_buffer_64(u64 *dst, unsigned int idx, u64 value)
{
	dst[idx] = value;
}

static void
write_to_buffer(void *dst, unsigned int idx, bool do_64bit, u64 value)
{
	if (do_64bit)
		write_to_buffer_64(dst, idx, value);
	else
		write_to_buffer_32(dst, idx, value);
}

static void
v3d_copy_query_results(struct v3d_cpu_job *job)
{
	struct v3d_timestamp_query_info *timestamp_query = &job->timestamp_query;
	struct v3d_timestamp_query *queries = timestamp_query->queries;
	struct v3d_bo *bo = to_v3d_bo(job->base.bo[0]);
	struct v3d_bo *timestamp = to_v3d_bo(job->base.bo[1]);
	struct v3d_copy_query_results_info *copy = &job->copy;
	struct dma_fence *fence;
	u8 *query_addr;
	bool available, write_result;
	u8 *data;
	int i;

	v3d_get_bo_vaddr(bo);
	v3d_get_bo_vaddr(timestamp);

	data = ((u8 *)bo->vaddr) + copy->offset;

	for (i = 0; i < timestamp_query->count; i++) {
		fence = drm_syncobj_fence_get(queries[i].syncobj);
		available = fence ? dma_fence_is_signaled(fence) : false;

		write_result = available || copy->do_partial;
		if (write_result) {
			query_addr = ((u8 *)timestamp->vaddr) + queries[i].offset;
			write_to_buffer(data, 0, copy->do_64bit, *((u64 *)query_addr));
		}

		if (copy->availability_bit)
			write_to_buffer(data, 1, copy->do_64bit, available ? 1u : 0u);

		data += copy->stride;

		dma_fence_put(fence);
	}

	v3d_put_bo_vaddr(timestamp);
	v3d_put_bo_vaddr(bo);
}

static void
v3d_reset_performance_queries(struct v3d_cpu_job *job)
{
	struct v3d_performance_query_info *performance_query = &job->performance_query;
	struct v3d_file_priv *v3d_priv = job->base.file->driver_priv;
	struct v3d_dev *v3d = job->base.v3d;
	struct v3d_perfmon *perfmon;

	for (int i = 0; i < performance_query->count; i++) {
		for (int j = 0; j < performance_query->nperfmons; j++) {
			perfmon = v3d_perfmon_find(v3d_priv,
						   performance_query->queries[i].kperfmon_ids[j]);
			if (!perfmon) {
				DRM_DEBUG("Failed to find perfmon.");
				continue;
			}

			v3d_perfmon_stop(v3d, perfmon, false);

			memset(perfmon->values, 0, perfmon->ncounters * sizeof(u64));

			v3d_perfmon_put(perfmon);
		}

		drm_syncobj_replace_fence(performance_query->queries[i].syncobj, NULL);
	}
}

static void
v3d_write_performance_query_result(struct v3d_cpu_job *job, void *data,
				   unsigned int query)
{
	struct v3d_performance_query_info *performance_query =
						&job->performance_query;
	struct v3d_file_priv *v3d_priv = job->base.file->driver_priv;
	struct v3d_performance_query *perf_query =
			&performance_query->queries[query];
	struct v3d_dev *v3d = job->base.v3d;
	unsigned int i, j, offset;

	for (i = 0, offset = 0;
	     i < performance_query->nperfmons;
	     i++, offset += DRM_V3D_MAX_PERF_COUNTERS) {
		struct v3d_perfmon *perfmon;

		perfmon = v3d_perfmon_find(v3d_priv,
					   perf_query->kperfmon_ids[i]);
		if (!perfmon) {
			DRM_DEBUG("Failed to find perfmon.");
			continue;
		}

		v3d_perfmon_stop(v3d, perfmon, true);

		if (job->copy.do_64bit) {
			for (j = 0; j < perfmon->ncounters; j++)
				write_to_buffer_64(data, offset + j,
						   perfmon->values[j]);
		} else {
			for (j = 0; j < perfmon->ncounters; j++)
				write_to_buffer_32(data, offset + j,
						   perfmon->values[j]);
		}

		v3d_perfmon_put(perfmon);
	}
}

static void
v3d_copy_performance_query(struct v3d_cpu_job *job)
{
	struct v3d_performance_query_info *performance_query = &job->performance_query;
	struct v3d_copy_query_results_info *copy = &job->copy;
	struct v3d_bo *bo = to_v3d_bo(job->base.bo[0]);
	struct dma_fence *fence;
	bool available, write_result;
	u8 *data;

	v3d_get_bo_vaddr(bo);

	data = ((u8 *)bo->vaddr) + copy->offset;

	for (int i = 0; i < performance_query->count; i++) {
		fence = drm_syncobj_fence_get(performance_query->queries[i].syncobj);
		available = fence ? dma_fence_is_signaled(fence) : false;

		write_result = available || copy->do_partial;
		if (write_result)
			v3d_write_performance_query_result(job, data, i);

		if (copy->availability_bit)
			write_to_buffer(data, performance_query->ncounters,
					copy->do_64bit, available ? 1u : 0u);

		data += copy->stride;

		dma_fence_put(fence);
	}

	v3d_put_bo_vaddr(bo);
}

static const v3d_cpu_job_fn cpu_job_function[] = {
	[V3D_CPU_JOB_TYPE_INDIRECT_CSD] = v3d_rewrite_csd_job_wg_counts_from_indirect,
	[V3D_CPU_JOB_TYPE_TIMESTAMP_QUERY] = v3d_timestamp_query,
	[V3D_CPU_JOB_TYPE_RESET_TIMESTAMP_QUERY] = v3d_reset_timestamp_queries,
	[V3D_CPU_JOB_TYPE_COPY_TIMESTAMP_QUERY] = v3d_copy_query_results,
	[V3D_CPU_JOB_TYPE_RESET_PERFORMANCE_QUERY] = v3d_reset_performance_queries,
	[V3D_CPU_JOB_TYPE_COPY_PERFORMANCE_QUERY] = v3d_copy_performance_query,
};

static struct dma_fence *
v3d_cpu_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_cpu_job *job = to_cpu_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;

	v3d->cpu_job = job;

	if (job->job_type >= ARRAY_SIZE(cpu_job_function)) {
		DRM_DEBUG_DRIVER("Unknown CPU job: %d\n", job->job_type);
		return NULL;
	}

	v3d_job_start_stats(&job->base, V3D_CPU);
	trace_v3d_cpu_job_begin(&v3d->drm, job->job_type);

	cpu_job_function[job->job_type](job);

	trace_v3d_cpu_job_end(&v3d->drm, job->job_type);
	v3d_job_update_stats(&job->base, V3D_CPU);

	return NULL;
}

static struct dma_fence *
v3d_cache_clean_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_dev *v3d = job->v3d;

	v3d_job_start_stats(job, V3D_CACHE_CLEAN);

	v3d_clean_caches(v3d);

	v3d_job_update_stats(job, V3D_CACHE_CLEAN);

	return NULL;
}

static enum drm_gpu_sched_stat
v3d_gpu_reset_for_timeout(struct v3d_dev *v3d, struct drm_sched_job *sched_job)
{
	enum v3d_queue q;

	mutex_lock(&v3d->reset_lock);

	/* block scheduler */
	for (q = 0; q < V3D_MAX_QUEUES; q++)
		drm_sched_stop(&v3d->queue[q].sched, sched_job);

	if (sched_job)
		drm_sched_increase_karma(sched_job);

	/* get the GPU back into the init state */
	v3d_reset(v3d);

	for (q = 0; q < V3D_MAX_QUEUES; q++)
		drm_sched_resubmit_jobs(&v3d->queue[q].sched);

	/* Unblock schedulers and restart their jobs. */
	for (q = 0; q < V3D_MAX_QUEUES; q++) {
		drm_sched_start(&v3d->queue[q].sched, 0);
	}

	mutex_unlock(&v3d->reset_lock);

	return DRM_GPU_SCHED_STAT_NOMINAL;
}

/* If the current address or return address have changed, then the GPU
 * has probably made progress and we should delay the reset.  This
 * could fail if the GPU got in an infinite loop in the CL, but that
 * is pretty unlikely outside of an i-g-t testcase.
 */
static enum drm_gpu_sched_stat
v3d_cl_job_timedout(struct drm_sched_job *sched_job, enum v3d_queue q,
		    u32 *timedout_ctca, u32 *timedout_ctra)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_dev *v3d = job->v3d;
	u32 ctca = V3D_CORE_READ(0, V3D_CLE_CTNCA(q));
	u32 ctra = V3D_CORE_READ(0, V3D_CLE_CTNRA(q));

	if (*timedout_ctca != ctca || *timedout_ctra != ctra) {
		*timedout_ctca = ctca;
		*timedout_ctra = ctra;
		return DRM_GPU_SCHED_STAT_NOMINAL;
	}

	return v3d_gpu_reset_for_timeout(v3d, sched_job);
}

static enum drm_gpu_sched_stat
v3d_bin_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_bin_job *job = to_bin_job(sched_job);

	return v3d_cl_job_timedout(sched_job, V3D_BIN,
				   &job->timedout_ctca, &job->timedout_ctra);
}

static enum drm_gpu_sched_stat
v3d_render_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_render_job *job = to_render_job(sched_job);

	return v3d_cl_job_timedout(sched_job, V3D_RENDER,
				   &job->timedout_ctca, &job->timedout_ctra);
}

static enum drm_gpu_sched_stat
v3d_generic_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);

	return v3d_gpu_reset_for_timeout(job->v3d, sched_job);
}

static enum drm_gpu_sched_stat
v3d_csd_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_csd_job *job = to_csd_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	u32 batches = V3D_CORE_READ(0, V3D_CSD_CURRENT_CFG4(v3d->ver));

	/* If we've made progress, skip reset and let the timer get
	 * rearmed.
	 */
	if (job->timedout_batches != batches) {
		job->timedout_batches = batches;
		return DRM_GPU_SCHED_STAT_NOMINAL;
	}

	return v3d_gpu_reset_for_timeout(v3d, sched_job);
}

static const struct drm_sched_backend_ops v3d_bin_sched_ops = {
	.run_job = v3d_bin_job_run,
	.timedout_job = v3d_bin_job_timedout,
	.free_job = v3d_sched_job_free,
};

static const struct drm_sched_backend_ops v3d_render_sched_ops = {
	.run_job = v3d_render_job_run,
	.timedout_job = v3d_render_job_timedout,
	.free_job = v3d_sched_job_free,
};

static const struct drm_sched_backend_ops v3d_tfu_sched_ops = {
	.run_job = v3d_tfu_job_run,
	.timedout_job = v3d_generic_job_timedout,
	.free_job = v3d_sched_job_free,
};

static const struct drm_sched_backend_ops v3d_csd_sched_ops = {
	.run_job = v3d_csd_job_run,
	.timedout_job = v3d_csd_job_timedout,
	.free_job = v3d_sched_job_free
};

static const struct drm_sched_backend_ops v3d_cache_clean_sched_ops = {
	.run_job = v3d_cache_clean_job_run,
	.timedout_job = v3d_generic_job_timedout,
	.free_job = v3d_sched_job_free
};

static const struct drm_sched_backend_ops v3d_cpu_sched_ops = {
	.run_job = v3d_cpu_job_run,
	.timedout_job = v3d_generic_job_timedout,
	.free_job = v3d_cpu_job_free
};

int
v3d_sched_init(struct v3d_dev *v3d)
{
	int hw_jobs_limit = 1;
	int job_hang_limit = 0;
	int hang_limit_ms = 500;
	int ret;

	ret = drm_sched_init(&v3d->queue[V3D_BIN].sched,
			     &v3d_bin_sched_ops, NULL,
			     DRM_SCHED_PRIORITY_COUNT,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms), NULL,
			     NULL, "v3d_bin", v3d->drm.dev);
	if (ret)
		return ret;

	ret = drm_sched_init(&v3d->queue[V3D_RENDER].sched,
			     &v3d_render_sched_ops, NULL,
			     DRM_SCHED_PRIORITY_COUNT,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms), NULL,
			     NULL, "v3d_render", v3d->drm.dev);
	if (ret)
		goto fail;

	ret = drm_sched_init(&v3d->queue[V3D_TFU].sched,
			     &v3d_tfu_sched_ops, NULL,
			     DRM_SCHED_PRIORITY_COUNT,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms), NULL,
			     NULL, "v3d_tfu", v3d->drm.dev);
	if (ret)
		goto fail;

	if (v3d_has_csd(v3d)) {
		ret = drm_sched_init(&v3d->queue[V3D_CSD].sched,
				     &v3d_csd_sched_ops, NULL,
				     DRM_SCHED_PRIORITY_COUNT,
				     hw_jobs_limit, job_hang_limit,
				     msecs_to_jiffies(hang_limit_ms), NULL,
				     NULL, "v3d_csd", v3d->drm.dev);
		if (ret)
			goto fail;

		ret = drm_sched_init(&v3d->queue[V3D_CACHE_CLEAN].sched,
				     &v3d_cache_clean_sched_ops, NULL,
				     DRM_SCHED_PRIORITY_COUNT,
				     hw_jobs_limit, job_hang_limit,
				     msecs_to_jiffies(hang_limit_ms), NULL,
				     NULL, "v3d_cache_clean", v3d->drm.dev);
		if (ret)
			goto fail;
	}

	ret = drm_sched_init(&v3d->queue[V3D_CPU].sched,
			     &v3d_cpu_sched_ops, NULL,
			     DRM_SCHED_PRIORITY_COUNT,
			     1, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms), NULL,
			     NULL, "v3d_cpu", v3d->drm.dev);
	if (ret)
		goto fail;

	return 0;

fail:
	v3d_sched_fini(v3d);
	return ret;
}

void
v3d_sched_fini(struct v3d_dev *v3d)
{
	enum v3d_queue q;

	for (q = 0; q < V3D_MAX_QUEUES; q++) {
		if (v3d->queue[q].sched.ready)
			drm_sched_fini(&v3d->queue[q].sched);
	}
}

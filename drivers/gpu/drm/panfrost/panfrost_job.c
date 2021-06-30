// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-resv.h>
#include <drm/gpu_scheduler.h>
#include <drm/panfrost_drm.h>

#include "panfrost_device.h"
#include "panfrost_devfreq.h"
#include "panfrost_job.h"
#include "panfrost_features.h"
#include "panfrost_issues.h"
#include "panfrost_gem.h"
#include "panfrost_regs.h"
#include "panfrost_gpu.h"
#include "panfrost_mmu.h"

#define JOB_TIMEOUT_MS 500

#define job_write(dev, reg, data) writel(data, dev->iomem + (reg))
#define job_read(dev, reg) readl(dev->iomem + (reg))

struct panfrost_queue_state {
	struct drm_gpu_scheduler sched;
	u64 fence_context;
	u64 emit_seqno;
};

struct panfrost_job_slot {
	struct panfrost_queue_state queue[NUM_JOB_SLOTS];
	spinlock_t job_lock;
	int irq;
};

static struct panfrost_job *
to_panfrost_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct panfrost_job, base);
}

struct panfrost_fence {
	struct dma_fence base;
	struct drm_device *dev;
	/* panfrost seqno for signaled() test */
	u64 seqno;
	int queue;
};

static inline struct panfrost_fence *
to_panfrost_fence(struct dma_fence *fence)
{
	return (struct panfrost_fence *)fence;
}

static const char *panfrost_fence_get_driver_name(struct dma_fence *fence)
{
	return "panfrost";
}

static const char *panfrost_fence_get_timeline_name(struct dma_fence *fence)
{
	struct panfrost_fence *f = to_panfrost_fence(fence);

	switch (f->queue) {
	case 0:
		return "panfrost-js-0";
	case 1:
		return "panfrost-js-1";
	case 2:
		return "panfrost-js-2";
	default:
		return NULL;
	}
}

static const struct dma_fence_ops panfrost_fence_ops = {
	.get_driver_name = panfrost_fence_get_driver_name,
	.get_timeline_name = panfrost_fence_get_timeline_name,
};

static struct dma_fence *panfrost_fence_create(struct panfrost_device *pfdev, int js_num)
{
	struct panfrost_fence *fence;
	struct panfrost_job_slot *js = pfdev->js;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->dev = pfdev->ddev;
	fence->queue = js_num;
	fence->seqno = ++js->queue[js_num].emit_seqno;
	dma_fence_init(&fence->base, &panfrost_fence_ops, &js->job_lock,
		       js->queue[js_num].fence_context, fence->seqno);

	return &fence->base;
}

static int panfrost_job_get_slot(struct panfrost_job *job)
{
	/* JS0: fragment jobs.
	 * JS1: vertex/tiler jobs
	 * JS2: compute jobs
	 */
	if (job->requirements & PANFROST_JD_REQ_FS)
		return 0;

/* Not exposed to userspace yet */
#if 0
	if (job->requirements & PANFROST_JD_REQ_ONLY_COMPUTE) {
		if ((job->requirements & PANFROST_JD_REQ_CORE_GRP_MASK) &&
		    (job->pfdev->features.nr_core_groups == 2))
			return 2;
		if (panfrost_has_hw_issue(job->pfdev, HW_ISSUE_8987))
			return 2;
	}
#endif
	return 1;
}

static void panfrost_job_write_affinity(struct panfrost_device *pfdev,
					u32 requirements,
					int js)
{
	u64 affinity;

	/*
	 * Use all cores for now.
	 * Eventually we may need to support tiler only jobs and h/w with
	 * multiple (2) coherent core groups
	 */
	affinity = pfdev->features.shader_present;

	job_write(pfdev, JS_AFFINITY_NEXT_LO(js), affinity & 0xFFFFFFFF);
	job_write(pfdev, JS_AFFINITY_NEXT_HI(js), affinity >> 32);
}

static void panfrost_job_hw_submit(struct panfrost_job *job, int js)
{
	struct panfrost_device *pfdev = job->pfdev;
	u32 cfg;
	u64 jc_head = job->jc;
	int ret;

	panfrost_devfreq_record_busy(&pfdev->pfdevfreq);

	ret = pm_runtime_get_sync(pfdev->dev);
	if (ret < 0)
		return;

	if (WARN_ON(job_read(pfdev, JS_COMMAND_NEXT(js)))) {
		return;
	}

	cfg = panfrost_mmu_as_get(pfdev, job->file_priv->mmu);

	job_write(pfdev, JS_HEAD_NEXT_LO(js), jc_head & 0xFFFFFFFF);
	job_write(pfdev, JS_HEAD_NEXT_HI(js), jc_head >> 32);

	panfrost_job_write_affinity(pfdev, job->requirements, js);

	/* start MMU, medium priority, cache clean/flush on end, clean/flush on
	 * start */
	cfg |= JS_CONFIG_THREAD_PRI(8) |
		JS_CONFIG_START_FLUSH_CLEAN_INVALIDATE |
		JS_CONFIG_END_FLUSH_CLEAN_INVALIDATE;

	if (panfrost_has_hw_feature(pfdev, HW_FEATURE_FLUSH_REDUCTION))
		cfg |= JS_CONFIG_ENABLE_FLUSH_REDUCTION;

	if (panfrost_has_hw_issue(pfdev, HW_ISSUE_10649))
		cfg |= JS_CONFIG_START_MMU;

	job_write(pfdev, JS_CONFIG_NEXT(js), cfg);

	if (panfrost_has_hw_feature(pfdev, HW_FEATURE_FLUSH_REDUCTION))
		job_write(pfdev, JS_FLUSH_ID_NEXT(js), job->flush_id);

	/* GO ! */
	dev_dbg(pfdev->dev, "JS: Submitting atom %p to js[%d] with head=0x%llx",
				job, js, jc_head);

	job_write(pfdev, JS_COMMAND_NEXT(js), JS_COMMAND_START);
}

static int panfrost_acquire_object_fences(struct drm_gem_object **bos,
					  int bo_count,
					  struct xarray *deps)
{
	int i, ret;

	for (i = 0; i < bo_count; i++) {
		/* panfrost always uses write mode in its current uapi */
		ret = drm_gem_fence_array_add_implicit(deps, bos[i], true);
		if (ret)
			return ret;
	}

	return 0;
}

static void panfrost_attach_object_fences(struct drm_gem_object **bos,
					  int bo_count,
					  struct dma_fence *fence)
{
	int i;

	for (i = 0; i < bo_count; i++)
		dma_resv_add_excl_fence(bos[i]->resv, fence);
}

int panfrost_job_push(struct panfrost_job *job)
{
	struct panfrost_device *pfdev = job->pfdev;
	int slot = panfrost_job_get_slot(job);
	struct drm_sched_entity *entity = &job->file_priv->sched_entity[slot];
	struct ww_acquire_ctx acquire_ctx;
	int ret = 0;


	ret = drm_gem_lock_reservations(job->bos, job->bo_count,
					    &acquire_ctx);
	if (ret)
		return ret;

	mutex_lock(&pfdev->sched_lock);

	ret = drm_sched_job_init(&job->base, entity, NULL);
	if (ret) {
		mutex_unlock(&pfdev->sched_lock);
		goto unlock;
	}

	job->render_done_fence = dma_fence_get(&job->base.s_fence->finished);

	ret = panfrost_acquire_object_fences(job->bos, job->bo_count,
					     &job->deps);
	if (ret) {
		mutex_unlock(&pfdev->sched_lock);
		goto unlock;
	}

	kref_get(&job->refcount); /* put by scheduler job completion */

	drm_sched_entity_push_job(&job->base, entity);

	mutex_unlock(&pfdev->sched_lock);

	panfrost_attach_object_fences(job->bos, job->bo_count,
				      job->render_done_fence);

unlock:
	drm_gem_unlock_reservations(job->bos, job->bo_count, &acquire_ctx);

	return ret;
}

static void panfrost_job_cleanup(struct kref *ref)
{
	struct panfrost_job *job = container_of(ref, struct panfrost_job,
						refcount);
	struct dma_fence *fence;
	unsigned long index;
	unsigned int i;

	xa_for_each(&job->deps, index, fence) {
		dma_fence_put(fence);
	}
	xa_destroy(&job->deps);

	dma_fence_put(job->done_fence);
	dma_fence_put(job->render_done_fence);

	if (job->mappings) {
		for (i = 0; i < job->bo_count; i++) {
			if (!job->mappings[i])
				break;

			atomic_dec(&job->mappings[i]->obj->gpu_usecount);
			panfrost_gem_mapping_put(job->mappings[i]);
		}
		kvfree(job->mappings);
	}

	if (job->bos) {
		for (i = 0; i < job->bo_count; i++)
			drm_gem_object_put(job->bos[i]);

		kvfree(job->bos);
	}

	kfree(job);
}

void panfrost_job_put(struct panfrost_job *job)
{
	kref_put(&job->refcount, panfrost_job_cleanup);
}

static void panfrost_job_free(struct drm_sched_job *sched_job)
{
	struct panfrost_job *job = to_panfrost_job(sched_job);

	drm_sched_job_cleanup(sched_job);

	panfrost_job_put(job);
}

static struct dma_fence *panfrost_job_dependency(struct drm_sched_job *sched_job,
						 struct drm_sched_entity *s_entity)
{
	struct panfrost_job *job = to_panfrost_job(sched_job);

	if (!xa_empty(&job->deps))
		return xa_erase(&job->deps, job->last_dep++);

	return NULL;
}

static struct dma_fence *panfrost_job_run(struct drm_sched_job *sched_job)
{
	struct panfrost_job *job = to_panfrost_job(sched_job);
	struct panfrost_device *pfdev = job->pfdev;
	int slot = panfrost_job_get_slot(job);
	struct dma_fence *fence = NULL;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	pfdev->jobs[slot] = job;

	fence = panfrost_fence_create(pfdev, slot);
	if (IS_ERR(fence))
		return fence;

	if (job->done_fence)
		dma_fence_put(job->done_fence);
	job->done_fence = dma_fence_get(fence);

	panfrost_job_hw_submit(job, slot);

	return fence;
}

void panfrost_job_enable_interrupts(struct panfrost_device *pfdev)
{
	int j;
	u32 irq_mask = 0;

	for (j = 0; j < NUM_JOB_SLOTS; j++) {
		irq_mask |= MK_JS_MASK(j);
	}

	job_write(pfdev, JOB_INT_CLEAR, irq_mask);
	job_write(pfdev, JOB_INT_MASK, irq_mask);
}

static void panfrost_reset(struct panfrost_device *pfdev,
			   struct drm_sched_job *bad)
{
	unsigned int i;
	bool cookie;

	if (!atomic_read(&pfdev->reset.pending))
		return;

	/* Stop the schedulers.
	 *
	 * FIXME: We temporarily get out of the dma_fence_signalling section
	 * because the cleanup path generate lockdep splats when taking locks
	 * to release job resources. We should rework the code to follow this
	 * pattern:
	 *
	 *	try_lock
	 *	if (locked)
	 *		release
	 *	else
	 *		schedule_work_to_release_later
	 */
	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_stop(&pfdev->js->queue[i].sched, bad);

	cookie = dma_fence_begin_signalling();

	if (bad)
		drm_sched_increase_karma(bad);

	/* Mask job interrupts and synchronize to make sure we won't be
	 * interrupted during our reset.
	 */
	job_write(pfdev, JOB_INT_MASK, 0);
	synchronize_irq(pfdev->js->irq);

	/* Schedulers are stopped and interrupts are masked+flushed, we don't
	 * need to protect the 'evict unfinished jobs' lock with the job_lock.
	 */
	spin_lock(&pfdev->js->job_lock);
	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		if (pfdev->jobs[i]) {
			pm_runtime_put_noidle(pfdev->dev);
			panfrost_devfreq_record_idle(&pfdev->pfdevfreq);
			pfdev->jobs[i] = NULL;
		}
	}
	spin_unlock(&pfdev->js->job_lock);

	panfrost_device_reset(pfdev);

	/* GPU has been reset, we can clear the reset pending bit. */
	atomic_set(&pfdev->reset.pending, 0);

	/* Now resubmit jobs that were previously queued but didn't have a
	 * chance to finish.
	 * FIXME: We temporarily get out of the DMA fence signalling section
	 * while resubmitting jobs because the job submission logic will
	 * allocate memory with the GFP_KERNEL flag which can trigger memory
	 * reclaim and exposes a lock ordering issue.
	 */
	dma_fence_end_signalling(cookie);
	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_resubmit_jobs(&pfdev->js->queue[i].sched);
	cookie = dma_fence_begin_signalling();

	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_start(&pfdev->js->queue[i].sched, true);

	dma_fence_end_signalling(cookie);
}

static enum drm_gpu_sched_stat panfrost_job_timedout(struct drm_sched_job
						     *sched_job)
{
	struct panfrost_job *job = to_panfrost_job(sched_job);
	struct panfrost_device *pfdev = job->pfdev;
	int js = panfrost_job_get_slot(job);

	/*
	 * If the GPU managed to complete this jobs fence, the timeout is
	 * spurious. Bail out.
	 */
	if (dma_fence_is_signaled(job->done_fence))
		return DRM_GPU_SCHED_STAT_NOMINAL;

	dev_err(pfdev->dev, "gpu sched timeout, js=%d, config=0x%x, status=0x%x, head=0x%x, tail=0x%x, sched_job=%p",
		js,
		job_read(pfdev, JS_CONFIG(js)),
		job_read(pfdev, JS_STATUS(js)),
		job_read(pfdev, JS_HEAD_LO(js)),
		job_read(pfdev, JS_TAIL_LO(js)),
		sched_job);

	atomic_set(&pfdev->reset.pending, 1);
	panfrost_reset(pfdev, sched_job);

	return DRM_GPU_SCHED_STAT_NOMINAL;
}

static const struct drm_sched_backend_ops panfrost_sched_ops = {
	.dependency = panfrost_job_dependency,
	.run_job = panfrost_job_run,
	.timedout_job = panfrost_job_timedout,
	.free_job = panfrost_job_free
};

static void panfrost_job_handle_irq(struct panfrost_device *pfdev, u32 status)
{
	int j;

	dev_dbg(pfdev->dev, "jobslot irq status=%x\n", status);

	for (j = 0; status; j++) {
		u32 mask = MK_JS_MASK(j);

		if (!(status & mask))
			continue;

		job_write(pfdev, JOB_INT_CLEAR, mask);

		if (status & JOB_INT_MASK_ERR(j)) {
			u32 js_status = job_read(pfdev, JS_STATUS(j));
			const char *exception_name = panfrost_exception_name(js_status);

			job_write(pfdev, JS_COMMAND_NEXT(j), JS_COMMAND_NOP);

			if (!panfrost_exception_is_fault(js_status)) {
				dev_dbg(pfdev->dev, "js interrupt, js=%d, status=%s, head=0x%x, tail=0x%x",
					j, exception_name,
					job_read(pfdev, JS_HEAD_LO(j)),
					job_read(pfdev, JS_TAIL_LO(j)));
			} else {
				dev_err(pfdev->dev, "js fault, js=%d, status=%s, head=0x%x, tail=0x%x",
					j, exception_name,
					job_read(pfdev, JS_HEAD_LO(j)),
					job_read(pfdev, JS_TAIL_LO(j)));
			}

			/* If we need a reset, signal it to the timeout
			 * handler, otherwise, update the fence error field and
			 * signal the job fence.
			 */
			if (panfrost_exception_needs_reset(pfdev, js_status)) {
				drm_sched_fault(&pfdev->js->queue[j].sched);
			} else {
				int error = 0;

				if (js_status == DRM_PANFROST_EXCEPTION_TERMINATED)
					error = -ECANCELED;
				else if (panfrost_exception_is_fault(js_status))
					error = -EINVAL;

				if (error)
					dma_fence_set_error(pfdev->jobs[j]->done_fence, error);

				status |= JOB_INT_MASK_DONE(j);
			}
		}

		if (status & JOB_INT_MASK_DONE(j)) {
			struct panfrost_job *job;

			job = pfdev->jobs[j];
			/* The only reason this job could be NULL is if the
			 * job IRQ handler is called just after the
			 * in-flight job eviction in the reset path, and
			 * this shouldn't happen because the job IRQ has
			 * been masked and synchronized when this eviction
			 * happens.
			 */
			WARN_ON(!job);
			if (job) {
				pfdev->jobs[j] = NULL;

				panfrost_mmu_as_put(pfdev, job->file_priv->mmu);
				panfrost_devfreq_record_idle(&pfdev->pfdevfreq);

				dma_fence_signal_locked(job->done_fence);
				pm_runtime_put_autosuspend(pfdev->dev);
			}
		}

		status &= ~mask;
	}
}

static irqreturn_t panfrost_job_irq_handler_thread(int irq, void *data)
{
	struct panfrost_device *pfdev = data;
	u32 status = job_read(pfdev, JOB_INT_RAWSTAT);

	while (status) {
		pm_runtime_mark_last_busy(pfdev->dev);

		spin_lock(&pfdev->js->job_lock);
		panfrost_job_handle_irq(pfdev, status);
		spin_unlock(&pfdev->js->job_lock);
		status = job_read(pfdev, JOB_INT_RAWSTAT);
	}

	job_write(pfdev, JOB_INT_MASK,
		  GENMASK(16 + NUM_JOB_SLOTS - 1, 16) |
		  GENMASK(NUM_JOB_SLOTS - 1, 0));
	return IRQ_HANDLED;
}

static irqreturn_t panfrost_job_irq_handler(int irq, void *data)
{
	struct panfrost_device *pfdev = data;
	u32 status = job_read(pfdev, JOB_INT_STAT);

	if (!status)
		return IRQ_NONE;

	job_write(pfdev, JOB_INT_MASK, 0);
	return IRQ_WAKE_THREAD;
}

static void panfrost_reset_work(struct work_struct *work)
{
	struct panfrost_device *pfdev = container_of(work,
						     struct panfrost_device,
						     reset.work);

	panfrost_reset(pfdev, NULL);
}

int panfrost_job_init(struct panfrost_device *pfdev)
{
	struct panfrost_job_slot *js;
	int ret, j;

	INIT_WORK(&pfdev->reset.work, panfrost_reset_work);

	pfdev->js = js = devm_kzalloc(pfdev->dev, sizeof(*js), GFP_KERNEL);
	if (!js)
		return -ENOMEM;

	spin_lock_init(&js->job_lock);

	js->irq = platform_get_irq_byname(to_platform_device(pfdev->dev), "job");
	if (js->irq <= 0)
		return -ENODEV;

	ret = devm_request_threaded_irq(pfdev->dev, js->irq,
					panfrost_job_irq_handler,
					panfrost_job_irq_handler_thread,
					IRQF_SHARED, KBUILD_MODNAME "-job",
					pfdev);
	if (ret) {
		dev_err(pfdev->dev, "failed to request job irq");
		return ret;
	}

	pfdev->reset.wq = alloc_ordered_workqueue("panfrost-reset", 0);
	if (!pfdev->reset.wq)
		return -ENOMEM;

	for (j = 0; j < NUM_JOB_SLOTS; j++) {
		js->queue[j].fence_context = dma_fence_context_alloc(1);

		ret = drm_sched_init(&js->queue[j].sched,
				     &panfrost_sched_ops,
				     1, 0,
				     msecs_to_jiffies(JOB_TIMEOUT_MS),
				     pfdev->reset.wq,
				     NULL, "pan_js");
		if (ret) {
			dev_err(pfdev->dev, "Failed to create scheduler: %d.", ret);
			goto err_sched;
		}
	}

	panfrost_job_enable_interrupts(pfdev);

	return 0;

err_sched:
	for (j--; j >= 0; j--)
		drm_sched_fini(&js->queue[j].sched);

	destroy_workqueue(pfdev->reset.wq);
	return ret;
}

void panfrost_job_fini(struct panfrost_device *pfdev)
{
	struct panfrost_job_slot *js = pfdev->js;
	int j;

	job_write(pfdev, JOB_INT_MASK, 0);

	for (j = 0; j < NUM_JOB_SLOTS; j++) {
		drm_sched_fini(&js->queue[j].sched);
	}

	cancel_work_sync(&pfdev->reset.work);
	destroy_workqueue(pfdev->reset.wq);
}

int panfrost_job_open(struct panfrost_file_priv *panfrost_priv)
{
	struct panfrost_device *pfdev = panfrost_priv->pfdev;
	struct panfrost_job_slot *js = pfdev->js;
	struct drm_gpu_scheduler *sched;
	int ret, i;

	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		sched = &js->queue[i].sched;
		ret = drm_sched_entity_init(&panfrost_priv->sched_entity[i],
					    DRM_SCHED_PRIORITY_NORMAL, &sched,
					    1, NULL);
		if (WARN_ON(ret))
			return ret;
	}
	return 0;
}

void panfrost_job_close(struct panfrost_file_priv *panfrost_priv)
{
	struct panfrost_device *pfdev = panfrost_priv->pfdev;
	int i;

	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_entity_destroy(&panfrost_priv->sched_entity[i]);

	/* Kill in-flight jobs */
	spin_lock(&pfdev->js->job_lock);
	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		struct drm_sched_entity *entity = &panfrost_priv->sched_entity[i];
		struct panfrost_job *job = pfdev->jobs[i];

		if (!job || job->base.entity != entity)
			continue;

		job_write(pfdev, JS_COMMAND(i), JS_COMMAND_HARD_STOP);
	}
	spin_unlock(&pfdev->js->job_lock);
}

int panfrost_job_is_idle(struct panfrost_device *pfdev)
{
	struct panfrost_job_slot *js = pfdev->js;
	int i;

	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		/* If there are any jobs in the HW queue, we're not idle */
		if (atomic_read(&js->queue[i].sched.hw_rq_count))
			return false;
	}

	return true;
}

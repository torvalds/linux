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

	cfg = panfrost_mmu_as_get(pfdev, &job->file_priv->mmu);

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

static void panfrost_acquire_object_fences(struct drm_gem_object **bos,
					   int bo_count,
					   struct dma_fence **implicit_fences)
{
	int i;

	for (i = 0; i < bo_count; i++)
		implicit_fences[i] = dma_resv_get_excl_rcu(bos[i]->resv);
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

	mutex_lock(&pfdev->sched_lock);

	ret = drm_gem_lock_reservations(job->bos, job->bo_count,
					    &acquire_ctx);
	if (ret) {
		mutex_unlock(&pfdev->sched_lock);
		return ret;
	}

	ret = drm_sched_job_init(&job->base, entity, NULL);
	if (ret) {
		mutex_unlock(&pfdev->sched_lock);
		goto unlock;
	}

	job->render_done_fence = dma_fence_get(&job->base.s_fence->finished);

	kref_get(&job->refcount); /* put by scheduler job completion */

	panfrost_acquire_object_fences(job->bos, job->bo_count,
				       job->implicit_fences);

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
	unsigned int i;

	if (job->in_fences) {
		for (i = 0; i < job->in_fence_count; i++)
			dma_fence_put(job->in_fences[i]);
		kvfree(job->in_fences);
	}
	if (job->implicit_fences) {
		for (i = 0; i < job->bo_count; i++)
			dma_fence_put(job->implicit_fences[i]);
		kvfree(job->implicit_fences);
	}
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
	struct dma_fence *fence;
	unsigned int i;

	/* Explicit fences */
	for (i = 0; i < job->in_fence_count; i++) {
		if (job->in_fences[i]) {
			fence = job->in_fences[i];
			job->in_fences[i] = NULL;
			return fence;
		}
	}

	/* Implicit fences, max. one per BO */
	for (i = 0; i < job->bo_count; i++) {
		if (job->implicit_fences[i]) {
			fence = job->implicit_fences[i];
			job->implicit_fences[i] = NULL;
			return fence;
		}
	}

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
		return NULL;

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

static void panfrost_job_timedout(struct drm_sched_job *sched_job)
{
	struct panfrost_job *job = to_panfrost_job(sched_job);
	struct panfrost_device *pfdev = job->pfdev;
	int js = panfrost_job_get_slot(job);
	unsigned long flags;
	int i;

	/*
	 * If the GPU managed to complete this jobs fence, the timeout is
	 * spurious. Bail out.
	 */
	if (dma_fence_is_signaled(job->done_fence))
		return;

	dev_err(pfdev->dev, "gpu sched timeout, js=%d, config=0x%x, status=0x%x, head=0x%x, tail=0x%x, sched_job=%p",
		js,
		job_read(pfdev, JS_CONFIG(js)),
		job_read(pfdev, JS_STATUS(js)),
		job_read(pfdev, JS_HEAD_LO(js)),
		job_read(pfdev, JS_TAIL_LO(js)),
		sched_job);

	if (!mutex_trylock(&pfdev->reset_lock))
		return;

	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		struct drm_gpu_scheduler *sched = &pfdev->js->queue[i].sched;

		drm_sched_stop(sched, sched_job);
		if (js != i)
			/* Ensure any timeouts on other slots have finished */
			cancel_delayed_work_sync(&sched->work_tdr);
	}

	drm_sched_increase_karma(sched_job);

	spin_lock_irqsave(&pfdev->js->job_lock, flags);
	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		if (pfdev->jobs[i]) {
			pm_runtime_put_noidle(pfdev->dev);
			panfrost_devfreq_record_idle(&pfdev->pfdevfreq);
			pfdev->jobs[i] = NULL;
		}
	}
	spin_unlock_irqrestore(&pfdev->js->job_lock, flags);

	panfrost_device_reset(pfdev);

	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_resubmit_jobs(&pfdev->js->queue[i].sched);

	/* restart scheduler after GPU is usable again */
	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_start(&pfdev->js->queue[i].sched, true);

	mutex_unlock(&pfdev->reset_lock);
}

static const struct drm_sched_backend_ops panfrost_sched_ops = {
	.dependency = panfrost_job_dependency,
	.run_job = panfrost_job_run,
	.timedout_job = panfrost_job_timedout,
	.free_job = panfrost_job_free
};

static irqreturn_t panfrost_job_irq_handler(int irq, void *data)
{
	struct panfrost_device *pfdev = data;
	u32 status = job_read(pfdev, JOB_INT_STAT);
	int j;

	dev_dbg(pfdev->dev, "jobslot irq status=%x\n", status);

	if (!status)
		return IRQ_NONE;

	pm_runtime_mark_last_busy(pfdev->dev);

	for (j = 0; status; j++) {
		u32 mask = MK_JS_MASK(j);

		if (!(status & mask))
			continue;

		job_write(pfdev, JOB_INT_CLEAR, mask);

		if (status & JOB_INT_MASK_ERR(j)) {
			job_write(pfdev, JS_COMMAND_NEXT(j), JS_COMMAND_NOP);

			dev_err(pfdev->dev, "js fault, js=%d, status=%s, head=0x%x, tail=0x%x",
				j,
				panfrost_exception_name(pfdev, job_read(pfdev, JS_STATUS(j))),
				job_read(pfdev, JS_HEAD_LO(j)),
				job_read(pfdev, JS_TAIL_LO(j)));

			drm_sched_fault(&pfdev->js->queue[j].sched);
		}

		if (status & JOB_INT_MASK_DONE(j)) {
			struct panfrost_job *job;

			spin_lock(&pfdev->js->job_lock);
			job = pfdev->jobs[j];
			/* Only NULL if job timeout occurred */
			if (job) {
				pfdev->jobs[j] = NULL;

				panfrost_mmu_as_put(pfdev, &job->file_priv->mmu);
				panfrost_devfreq_record_idle(&pfdev->pfdevfreq);

				dma_fence_signal_locked(job->done_fence);
				pm_runtime_put_autosuspend(pfdev->dev);
			}
			spin_unlock(&pfdev->js->job_lock);
		}

		status &= ~mask;
	}

	return IRQ_HANDLED;
}

int panfrost_job_init(struct panfrost_device *pfdev)
{
	struct panfrost_job_slot *js;
	int ret, j, irq;

	pfdev->js = js = devm_kzalloc(pfdev->dev, sizeof(*js), GFP_KERNEL);
	if (!js)
		return -ENOMEM;

	spin_lock_init(&js->job_lock);

	irq = platform_get_irq_byname(to_platform_device(pfdev->dev), "job");
	if (irq <= 0)
		return -ENODEV;

	ret = devm_request_irq(pfdev->dev, irq, panfrost_job_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME "-job", pfdev);
	if (ret) {
		dev_err(pfdev->dev, "failed to request job irq");
		return ret;
	}

	for (j = 0; j < NUM_JOB_SLOTS; j++) {
		js->queue[j].fence_context = dma_fence_context_alloc(1);

		ret = drm_sched_init(&js->queue[j].sched,
				     &panfrost_sched_ops,
				     1, 0, msecs_to_jiffies(500),
				     "pan_js");
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

	return ret;
}

void panfrost_job_fini(struct panfrost_device *pfdev)
{
	struct panfrost_job_slot *js = pfdev->js;
	int j;

	job_write(pfdev, JOB_INT_MASK, 0);

	for (j = 0; j < NUM_JOB_SLOTS; j++)
		drm_sched_fini(&js->queue[j].sched);

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
	int i;

	for (i = 0; i < NUM_JOB_SLOTS; i++)
		drm_sched_entity_destroy(&panfrost_priv->sched_entity[i]);
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

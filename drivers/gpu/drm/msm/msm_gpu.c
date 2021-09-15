// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_fence.h"
#include "msm_gpu_trace.h"
#include "adreno/adreno_gpu.h"

#include <generated/utsrelease.h>
#include <linux/string_helpers.h>
#include <linux/devcoredump.h>
#include <linux/sched/task.h>

/*
 * Power Management:
 */

static int enable_pwrrail(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	int ret = 0;

	if (gpu->gpu_reg) {
		ret = regulator_enable(gpu->gpu_reg);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to enable 'gpu_reg': %d\n", ret);
			return ret;
		}
	}

	if (gpu->gpu_cx) {
		ret = regulator_enable(gpu->gpu_cx);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to enable 'gpu_cx': %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int disable_pwrrail(struct msm_gpu *gpu)
{
	if (gpu->gpu_cx)
		regulator_disable(gpu->gpu_cx);
	if (gpu->gpu_reg)
		regulator_disable(gpu->gpu_reg);
	return 0;
}

static int enable_clk(struct msm_gpu *gpu)
{
	if (gpu->core_clk && gpu->fast_rate)
		clk_set_rate(gpu->core_clk, gpu->fast_rate);

	/* Set the RBBM timer rate to 19.2Mhz */
	if (gpu->rbbmtimer_clk)
		clk_set_rate(gpu->rbbmtimer_clk, 19200000);

	return clk_bulk_prepare_enable(gpu->nr_clocks, gpu->grp_clks);
}

static int disable_clk(struct msm_gpu *gpu)
{
	clk_bulk_disable_unprepare(gpu->nr_clocks, gpu->grp_clks);

	/*
	 * Set the clock to a deliberately low rate. On older targets the clock
	 * speed had to be non zero to avoid problems. On newer targets this
	 * will be rounded down to zero anyway so it all works out.
	 */
	if (gpu->core_clk)
		clk_set_rate(gpu->core_clk, 27000000);

	if (gpu->rbbmtimer_clk)
		clk_set_rate(gpu->rbbmtimer_clk, 0);

	return 0;
}

static int enable_axi(struct msm_gpu *gpu)
{
	return clk_prepare_enable(gpu->ebi1_clk);
}

static int disable_axi(struct msm_gpu *gpu)
{
	clk_disable_unprepare(gpu->ebi1_clk);
	return 0;
}

int msm_gpu_pm_resume(struct msm_gpu *gpu)
{
	int ret;

	DBG("%s", gpu->name);
	trace_msm_gpu_resume(0);

	ret = enable_pwrrail(gpu);
	if (ret)
		return ret;

	ret = enable_clk(gpu);
	if (ret)
		return ret;

	ret = enable_axi(gpu);
	if (ret)
		return ret;

	msm_devfreq_resume(gpu);

	gpu->needs_hw_init = true;

	return 0;
}

int msm_gpu_pm_suspend(struct msm_gpu *gpu)
{
	int ret;

	DBG("%s", gpu->name);
	trace_msm_gpu_suspend(0);

	msm_devfreq_suspend(gpu);

	ret = disable_axi(gpu);
	if (ret)
		return ret;

	ret = disable_clk(gpu);
	if (ret)
		return ret;

	ret = disable_pwrrail(gpu);
	if (ret)
		return ret;

	gpu->suspend_count++;

	return 0;
}

int msm_gpu_hw_init(struct msm_gpu *gpu)
{
	int ret;

	WARN_ON(!mutex_is_locked(&gpu->dev->struct_mutex));

	if (!gpu->needs_hw_init)
		return 0;

	disable_irq(gpu->irq);
	ret = gpu->funcs->hw_init(gpu);
	if (!ret)
		gpu->needs_hw_init = false;
	enable_irq(gpu->irq);

	return ret;
}

static void update_fences(struct msm_gpu *gpu, struct msm_ringbuffer *ring,
		uint32_t fence)
{
	struct msm_gem_submit *submit;
	unsigned long flags;

	spin_lock_irqsave(&ring->submit_lock, flags);
	list_for_each_entry(submit, &ring->submits, node) {
		if (submit->seqno > fence)
			break;

		msm_update_fence(submit->ring->fctx,
			submit->hw_fence->seqno);
		dma_fence_signal(submit->hw_fence);
	}
	spin_unlock_irqrestore(&ring->submit_lock, flags);
}

#ifdef CONFIG_DEV_COREDUMP
static ssize_t msm_gpu_devcoredump_read(char *buffer, loff_t offset,
		size_t count, void *data, size_t datalen)
{
	struct msm_gpu *gpu = data;
	struct drm_print_iterator iter;
	struct drm_printer p;
	struct msm_gpu_state *state;

	state = msm_gpu_crashstate_get(gpu);
	if (!state)
		return 0;

	iter.data = buffer;
	iter.offset = 0;
	iter.start = offset;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	drm_printf(&p, "---\n");
	drm_printf(&p, "kernel: " UTS_RELEASE "\n");
	drm_printf(&p, "module: " KBUILD_MODNAME "\n");
	drm_printf(&p, "time: %lld.%09ld\n",
		state->time.tv_sec, state->time.tv_nsec);
	if (state->comm)
		drm_printf(&p, "comm: %s\n", state->comm);
	if (state->cmd)
		drm_printf(&p, "cmdline: %s\n", state->cmd);

	gpu->funcs->show(gpu, state, &p);

	msm_gpu_crashstate_put(gpu);

	return count - iter.remain;
}

static void msm_gpu_devcoredump_free(void *data)
{
	struct msm_gpu *gpu = data;

	msm_gpu_crashstate_put(gpu);
}

static void msm_gpu_crashstate_get_bo(struct msm_gpu_state *state,
		struct msm_gem_object *obj, u64 iova, u32 flags)
{
	struct msm_gpu_state_bo *state_bo = &state->bos[state->nr_bos];

	/* Don't record write only objects */
	state_bo->size = obj->base.size;
	state_bo->iova = iova;

	/* Only store data for non imported buffer objects marked for read */
	if ((flags & MSM_SUBMIT_BO_READ) && !obj->base.import_attach) {
		void *ptr;

		state_bo->data = kvmalloc(obj->base.size, GFP_KERNEL);
		if (!state_bo->data)
			goto out;

		msm_gem_lock(&obj->base);
		ptr = msm_gem_get_vaddr_active(&obj->base);
		msm_gem_unlock(&obj->base);
		if (IS_ERR(ptr)) {
			kvfree(state_bo->data);
			state_bo->data = NULL;
			goto out;
		}

		memcpy(state_bo->data, ptr, obj->base.size);
		msm_gem_put_vaddr(&obj->base);
	}
out:
	state->nr_bos++;
}

static void msm_gpu_crashstate_capture(struct msm_gpu *gpu,
		struct msm_gem_submit *submit, char *comm, char *cmd)
{
	struct msm_gpu_state *state;

	/* Check if the target supports capturing crash state */
	if (!gpu->funcs->gpu_state_get)
		return;

	/* Only save one crash state at a time */
	if (gpu->crashstate)
		return;

	state = gpu->funcs->gpu_state_get(gpu);
	if (IS_ERR_OR_NULL(state))
		return;

	/* Fill in the additional crash state information */
	state->comm = kstrdup(comm, GFP_KERNEL);
	state->cmd = kstrdup(cmd, GFP_KERNEL);
	state->fault_info = gpu->fault_info;

	if (submit) {
		int i, nr = 0;

		/* count # of buffers to dump: */
		for (i = 0; i < submit->nr_bos; i++)
			if (should_dump(submit, i))
				nr++;
		/* always dump cmd bo's, but don't double count them: */
		for (i = 0; i < submit->nr_cmds; i++)
			if (!should_dump(submit, submit->cmd[i].idx))
				nr++;

		state->bos = kcalloc(nr,
			sizeof(struct msm_gpu_state_bo), GFP_KERNEL);

		for (i = 0; i < submit->nr_bos; i++) {
			if (should_dump(submit, i)) {
				msm_gpu_crashstate_get_bo(state, submit->bos[i].obj,
					submit->bos[i].iova, submit->bos[i].flags);
			}
		}

		for (i = 0; state->bos && i < submit->nr_cmds; i++) {
			int idx = submit->cmd[i].idx;

			if (!should_dump(submit, submit->cmd[i].idx)) {
				msm_gpu_crashstate_get_bo(state, submit->bos[idx].obj,
					submit->bos[idx].iova, submit->bos[idx].flags);
			}
		}
	}

	/* Set the active crash state to be dumped on failure */
	gpu->crashstate = state;

	/* FIXME: Release the crashstate if this errors out? */
	dev_coredumpm(gpu->dev->dev, THIS_MODULE, gpu, 0, GFP_KERNEL,
		msm_gpu_devcoredump_read, msm_gpu_devcoredump_free);
}
#else
static void msm_gpu_crashstate_capture(struct msm_gpu *gpu,
		struct msm_gem_submit *submit, char *comm, char *cmd)
{
}
#endif

/*
 * Hangcheck detection for locked gpu:
 */

static struct msm_gem_submit *
find_submit(struct msm_ringbuffer *ring, uint32_t fence)
{
	struct msm_gem_submit *submit;
	unsigned long flags;

	spin_lock_irqsave(&ring->submit_lock, flags);
	list_for_each_entry(submit, &ring->submits, node) {
		if (submit->seqno == fence) {
			spin_unlock_irqrestore(&ring->submit_lock, flags);
			return submit;
		}
	}
	spin_unlock_irqrestore(&ring->submit_lock, flags);

	return NULL;
}

static void retire_submits(struct msm_gpu *gpu);

static void recover_worker(struct kthread_work *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, recover_work);
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_submit *submit;
	struct msm_ringbuffer *cur_ring = gpu->funcs->active_ring(gpu);
	char *comm = NULL, *cmd = NULL;
	int i;

	mutex_lock(&dev->struct_mutex);

	DRM_DEV_ERROR(dev->dev, "%s: hangcheck recover!\n", gpu->name);

	submit = find_submit(cur_ring, cur_ring->memptrs->fence + 1);
	if (submit) {
		struct task_struct *task;

		/* Increment the fault counts */
		gpu->global_faults++;
		submit->queue->faults++;

		task = get_pid_task(submit->pid, PIDTYPE_PID);
		if (task) {
			comm = kstrdup(task->comm, GFP_KERNEL);
			cmd = kstrdup_quotable_cmdline(task, GFP_KERNEL);
			put_task_struct(task);
		}

		if (comm && cmd) {
			DRM_DEV_ERROR(dev->dev, "%s: offending task: %s (%s)\n",
				gpu->name, comm, cmd);

			msm_rd_dump_submit(priv->hangrd, submit,
				"offending task: %s (%s)", comm, cmd);
		} else {
			msm_rd_dump_submit(priv->hangrd, submit, NULL);
		}
	}

	/* Record the crash state */
	pm_runtime_get_sync(&gpu->pdev->dev);
	msm_gpu_crashstate_capture(gpu, submit, comm, cmd);
	pm_runtime_put_sync(&gpu->pdev->dev);

	kfree(cmd);
	kfree(comm);

	/*
	 * Update all the rings with the latest and greatest fence.. this
	 * needs to happen after msm_rd_dump_submit() to ensure that the
	 * bo's referenced by the offending submit are still around.
	 */
	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		uint32_t fence = ring->memptrs->fence;

		/*
		 * For the current (faulting?) ring/submit advance the fence by
		 * one more to clear the faulting submit
		 */
		if (ring == cur_ring)
			fence++;

		update_fences(gpu, ring, fence);
	}

	if (msm_gpu_active(gpu)) {
		/* retire completed submits, plus the one that hung: */
		retire_submits(gpu);

		pm_runtime_get_sync(&gpu->pdev->dev);
		gpu->funcs->recover(gpu);
		pm_runtime_put_sync(&gpu->pdev->dev);

		/*
		 * Replay all remaining submits starting with highest priority
		 * ring
		 */
		for (i = 0; i < gpu->nr_rings; i++) {
			struct msm_ringbuffer *ring = gpu->rb[i];
			unsigned long flags;

			spin_lock_irqsave(&ring->submit_lock, flags);
			list_for_each_entry(submit, &ring->submits, node)
				gpu->funcs->submit(gpu, submit);
			spin_unlock_irqrestore(&ring->submit_lock, flags);
		}
	}

	mutex_unlock(&dev->struct_mutex);

	msm_gpu_retire(gpu);
}

static void fault_worker(struct kthread_work *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, fault_work);
	struct drm_device *dev = gpu->dev;
	struct msm_gem_submit *submit;
	struct msm_ringbuffer *cur_ring = gpu->funcs->active_ring(gpu);
	char *comm = NULL, *cmd = NULL;

	mutex_lock(&dev->struct_mutex);

	submit = find_submit(cur_ring, cur_ring->memptrs->fence + 1);
	if (submit && submit->fault_dumped)
		goto resume_smmu;

	if (submit) {
		struct task_struct *task;

		task = get_pid_task(submit->pid, PIDTYPE_PID);
		if (task) {
			comm = kstrdup(task->comm, GFP_KERNEL);
			cmd = kstrdup_quotable_cmdline(task, GFP_KERNEL);
			put_task_struct(task);
		}

		/*
		 * When we get GPU iova faults, we can get 1000s of them,
		 * but we really only want to log the first one.
		 */
		submit->fault_dumped = true;
	}

	/* Record the crash state */
	pm_runtime_get_sync(&gpu->pdev->dev);
	msm_gpu_crashstate_capture(gpu, submit, comm, cmd);
	pm_runtime_put_sync(&gpu->pdev->dev);

	kfree(cmd);
	kfree(comm);

resume_smmu:
	memset(&gpu->fault_info, 0, sizeof(gpu->fault_info));
	gpu->aspace->mmu->funcs->resume_translation(gpu->aspace->mmu);

	mutex_unlock(&dev->struct_mutex);
}

static void hangcheck_timer_reset(struct msm_gpu *gpu)
{
	struct msm_drm_private *priv = gpu->dev->dev_private;
	mod_timer(&gpu->hangcheck_timer,
			round_jiffies_up(jiffies + msecs_to_jiffies(priv->hangcheck_period)));
}

static void hangcheck_handler(struct timer_list *t)
{
	struct msm_gpu *gpu = from_timer(gpu, t, hangcheck_timer);
	struct drm_device *dev = gpu->dev;
	struct msm_ringbuffer *ring = gpu->funcs->active_ring(gpu);
	uint32_t fence = ring->memptrs->fence;

	if (fence != ring->hangcheck_fence) {
		/* some progress has been made.. ya! */
		ring->hangcheck_fence = fence;
	} else if (fence < ring->seqno) {
		/* no progress and not done.. hung! */
		ring->hangcheck_fence = fence;
		DRM_DEV_ERROR(dev->dev, "%s: hangcheck detected gpu lockup rb %d!\n",
				gpu->name, ring->id);
		DRM_DEV_ERROR(dev->dev, "%s:     completed fence: %u\n",
				gpu->name, fence);
		DRM_DEV_ERROR(dev->dev, "%s:     submitted fence: %u\n",
				gpu->name, ring->seqno);

		kthread_queue_work(gpu->worker, &gpu->recover_work);
	}

	/* if still more pending work, reset the hangcheck timer: */
	if (ring->seqno > ring->hangcheck_fence)
		hangcheck_timer_reset(gpu);

	/* workaround for missing irq: */
	msm_gpu_retire(gpu);
}

/*
 * Performance Counters:
 */

/* called under perf_lock */
static int update_hw_cntrs(struct msm_gpu *gpu, uint32_t ncntrs, uint32_t *cntrs)
{
	uint32_t current_cntrs[ARRAY_SIZE(gpu->last_cntrs)];
	int i, n = min(ncntrs, gpu->num_perfcntrs);

	/* read current values: */
	for (i = 0; i < gpu->num_perfcntrs; i++)
		current_cntrs[i] = gpu_read(gpu, gpu->perfcntrs[i].sample_reg);

	/* update cntrs: */
	for (i = 0; i < n; i++)
		cntrs[i] = current_cntrs[i] - gpu->last_cntrs[i];

	/* save current values: */
	for (i = 0; i < gpu->num_perfcntrs; i++)
		gpu->last_cntrs[i] = current_cntrs[i];

	return n;
}

static void update_sw_cntrs(struct msm_gpu *gpu)
{
	ktime_t time;
	uint32_t elapsed;
	unsigned long flags;

	spin_lock_irqsave(&gpu->perf_lock, flags);
	if (!gpu->perfcntr_active)
		goto out;

	time = ktime_get();
	elapsed = ktime_to_us(ktime_sub(time, gpu->last_sample.time));

	gpu->totaltime += elapsed;
	if (gpu->last_sample.active)
		gpu->activetime += elapsed;

	gpu->last_sample.active = msm_gpu_active(gpu);
	gpu->last_sample.time = time;

out:
	spin_unlock_irqrestore(&gpu->perf_lock, flags);
}

void msm_gpu_perfcntr_start(struct msm_gpu *gpu)
{
	unsigned long flags;

	pm_runtime_get_sync(&gpu->pdev->dev);

	spin_lock_irqsave(&gpu->perf_lock, flags);
	/* we could dynamically enable/disable perfcntr registers too.. */
	gpu->last_sample.active = msm_gpu_active(gpu);
	gpu->last_sample.time = ktime_get();
	gpu->activetime = gpu->totaltime = 0;
	gpu->perfcntr_active = true;
	update_hw_cntrs(gpu, 0, NULL);
	spin_unlock_irqrestore(&gpu->perf_lock, flags);
}

void msm_gpu_perfcntr_stop(struct msm_gpu *gpu)
{
	gpu->perfcntr_active = false;
	pm_runtime_put_sync(&gpu->pdev->dev);
}

/* returns -errno or # of cntrs sampled */
int msm_gpu_perfcntr_sample(struct msm_gpu *gpu, uint32_t *activetime,
		uint32_t *totaltime, uint32_t ncntrs, uint32_t *cntrs)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&gpu->perf_lock, flags);

	if (!gpu->perfcntr_active) {
		ret = -EINVAL;
		goto out;
	}

	*activetime = gpu->activetime;
	*totaltime = gpu->totaltime;

	gpu->activetime = gpu->totaltime = 0;

	ret = update_hw_cntrs(gpu, ncntrs, cntrs);

out:
	spin_unlock_irqrestore(&gpu->perf_lock, flags);

	return ret;
}

/*
 * Cmdstream submission/retirement:
 */

static void retire_submit(struct msm_gpu *gpu, struct msm_ringbuffer *ring,
		struct msm_gem_submit *submit)
{
	int index = submit->seqno % MSM_GPU_SUBMIT_STATS_COUNT;
	volatile struct msm_gpu_submit_stats *stats;
	u64 elapsed, clock = 0;
	unsigned long flags;

	stats = &ring->memptrs->stats[index];
	/* Convert 19.2Mhz alwayson ticks to nanoseconds for elapsed time */
	elapsed = (stats->alwayson_end - stats->alwayson_start) * 10000;
	do_div(elapsed, 192);

	/* Calculate the clock frequency from the number of CP cycles */
	if (elapsed) {
		clock = (stats->cpcycles_end - stats->cpcycles_start) * 1000;
		do_div(clock, elapsed);
	}

	trace_msm_gpu_submit_retired(submit, elapsed, clock,
		stats->alwayson_start, stats->alwayson_end);

	msm_submit_retire(submit);

	pm_runtime_mark_last_busy(&gpu->pdev->dev);
	pm_runtime_put_autosuspend(&gpu->pdev->dev);

	spin_lock_irqsave(&ring->submit_lock, flags);
	list_del(&submit->node);
	spin_unlock_irqrestore(&ring->submit_lock, flags);

	/* Update devfreq on transition from active->idle: */
	mutex_lock(&gpu->active_lock);
	gpu->active_submits--;
	WARN_ON(gpu->active_submits < 0);
	if (!gpu->active_submits)
		msm_devfreq_idle(gpu);
	mutex_unlock(&gpu->active_lock);

	msm_gem_submit_put(submit);
}

static void retire_submits(struct msm_gpu *gpu)
{
	int i;

	/* Retire the commits starting with highest priority */
	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		while (true) {
			struct msm_gem_submit *submit = NULL;
			unsigned long flags;

			spin_lock_irqsave(&ring->submit_lock, flags);
			submit = list_first_entry_or_null(&ring->submits,
					struct msm_gem_submit, node);
			spin_unlock_irqrestore(&ring->submit_lock, flags);

			/*
			 * If no submit, we are done.  If submit->fence hasn't
			 * been signalled, then later submits are not signalled
			 * either, so we are also done.
			 */
			if (submit && dma_fence_is_signaled(submit->hw_fence)) {
				retire_submit(gpu, ring, submit);
			} else {
				break;
			}
		}
	}
}

static void retire_worker(struct kthread_work *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, retire_work);

	retire_submits(gpu);
}

/* call from irq handler to schedule work to retire bo's */
void msm_gpu_retire(struct msm_gpu *gpu)
{
	int i;

	for (i = 0; i < gpu->nr_rings; i++)
		update_fences(gpu, gpu->rb[i], gpu->rb[i]->memptrs->fence);

	kthread_queue_work(gpu->worker, &gpu->retire_work);
	update_sw_cntrs(gpu);
}

/* add bo's to gpu's ring, and kick gpu: */
void msm_gpu_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_ringbuffer *ring = submit->ring;
	unsigned long flags;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	pm_runtime_get_sync(&gpu->pdev->dev);

	msm_gpu_hw_init(gpu);

	submit->seqno = ++ring->seqno;

	msm_rd_dump_submit(priv->rd, submit, NULL);

	update_sw_cntrs(gpu);

	/*
	 * ring->submits holds a ref to the submit, to deal with the case
	 * that a submit completes before msm_ioctl_gem_submit() returns.
	 */
	msm_gem_submit_get(submit);

	spin_lock_irqsave(&ring->submit_lock, flags);
	list_add_tail(&submit->node, &ring->submits);
	spin_unlock_irqrestore(&ring->submit_lock, flags);

	/* Update devfreq on transition from idle->active: */
	mutex_lock(&gpu->active_lock);
	if (!gpu->active_submits)
		msm_devfreq_active(gpu);
	gpu->active_submits++;
	mutex_unlock(&gpu->active_lock);

	gpu->funcs->submit(gpu, submit);
	priv->lastctx = submit->queue->ctx;

	hangcheck_timer_reset(gpu);
}

/*
 * Init/Cleanup:
 */

static irqreturn_t irq_handler(int irq, void *data)
{
	struct msm_gpu *gpu = data;
	return gpu->funcs->irq(gpu);
}

static int get_clocks(struct platform_device *pdev, struct msm_gpu *gpu)
{
	int ret = devm_clk_bulk_get_all(&pdev->dev, &gpu->grp_clks);

	if (ret < 1) {
		gpu->nr_clocks = 0;
		return ret;
	}

	gpu->nr_clocks = ret;

	gpu->core_clk = msm_clk_bulk_get_clock(gpu->grp_clks,
		gpu->nr_clocks, "core");

	gpu->rbbmtimer_clk = msm_clk_bulk_get_clock(gpu->grp_clks,
		gpu->nr_clocks, "rbbmtimer");

	return 0;
}

/* Return a new address space for a msm_drm_private instance */
struct msm_gem_address_space *
msm_gpu_create_private_address_space(struct msm_gpu *gpu, struct task_struct *task)
{
	struct msm_gem_address_space *aspace = NULL;
	if (!gpu)
		return NULL;

	/*
	 * If the target doesn't support private address spaces then return
	 * the global one
	 */
	if (gpu->funcs->create_private_address_space) {
		aspace = gpu->funcs->create_private_address_space(gpu);
		if (!IS_ERR(aspace))
			aspace->pid = get_pid(task_pid(task));
	}

	if (IS_ERR_OR_NULL(aspace))
		aspace = msm_gem_address_space_get(gpu->aspace);

	return aspace;
}

int msm_gpu_init(struct drm_device *drm, struct platform_device *pdev,
		struct msm_gpu *gpu, const struct msm_gpu_funcs *funcs,
		const char *name, struct msm_gpu_config *config)
{
	int i, ret, nr_rings = config->nr_rings;
	void *memptrs;
	uint64_t memptrs_iova;

	if (WARN_ON(gpu->num_perfcntrs > ARRAY_SIZE(gpu->last_cntrs)))
		gpu->num_perfcntrs = ARRAY_SIZE(gpu->last_cntrs);

	gpu->dev = drm;
	gpu->funcs = funcs;
	gpu->name = name;

	gpu->worker = kthread_create_worker(0, "%s-worker", gpu->name);
	if (IS_ERR(gpu->worker)) {
		ret = PTR_ERR(gpu->worker);
		gpu->worker = NULL;
		goto fail;
	}

	sched_set_fifo_low(gpu->worker->task);

	INIT_LIST_HEAD(&gpu->active_list);
	mutex_init(&gpu->active_lock);
	kthread_init_work(&gpu->retire_work, retire_worker);
	kthread_init_work(&gpu->recover_work, recover_worker);
	kthread_init_work(&gpu->fault_work, fault_worker);

	timer_setup(&gpu->hangcheck_timer, hangcheck_handler, 0);

	spin_lock_init(&gpu->perf_lock);


	/* Map registers: */
	gpu->mmio = msm_ioremap(pdev, config->ioname, name);
	if (IS_ERR(gpu->mmio)) {
		ret = PTR_ERR(gpu->mmio);
		goto fail;
	}

	/* Get Interrupt: */
	gpu->irq = platform_get_irq(pdev, 0);
	if (gpu->irq < 0) {
		ret = gpu->irq;
		DRM_DEV_ERROR(drm->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, gpu->irq, irq_handler,
			IRQF_TRIGGER_HIGH, gpu->name, gpu);
	if (ret) {
		DRM_DEV_ERROR(drm->dev, "failed to request IRQ%u: %d\n", gpu->irq, ret);
		goto fail;
	}

	ret = get_clocks(pdev, gpu);
	if (ret)
		goto fail;

	gpu->ebi1_clk = msm_clk_get(pdev, "bus");
	DBG("ebi1_clk: %p", gpu->ebi1_clk);
	if (IS_ERR(gpu->ebi1_clk))
		gpu->ebi1_clk = NULL;

	/* Acquire regulators: */
	gpu->gpu_reg = devm_regulator_get(&pdev->dev, "vdd");
	DBG("gpu_reg: %p", gpu->gpu_reg);
	if (IS_ERR(gpu->gpu_reg))
		gpu->gpu_reg = NULL;

	gpu->gpu_cx = devm_regulator_get(&pdev->dev, "vddcx");
	DBG("gpu_cx: %p", gpu->gpu_cx);
	if (IS_ERR(gpu->gpu_cx))
		gpu->gpu_cx = NULL;

	gpu->pdev = pdev;
	platform_set_drvdata(pdev, &gpu->adreno_smmu);

	msm_devfreq_init(gpu);


	gpu->aspace = gpu->funcs->create_address_space(gpu, pdev);

	if (gpu->aspace == NULL)
		DRM_DEV_INFO(drm->dev, "%s: no IOMMU, fallback to VRAM carveout!\n", name);
	else if (IS_ERR(gpu->aspace)) {
		ret = PTR_ERR(gpu->aspace);
		goto fail;
	}

	memptrs = msm_gem_kernel_new(drm,
		sizeof(struct msm_rbmemptrs) * nr_rings,
		check_apriv(gpu, MSM_BO_UNCACHED), gpu->aspace, &gpu->memptrs_bo,
		&memptrs_iova);

	if (IS_ERR(memptrs)) {
		ret = PTR_ERR(memptrs);
		DRM_DEV_ERROR(drm->dev, "could not allocate memptrs: %d\n", ret);
		goto fail;
	}

	msm_gem_object_set_name(gpu->memptrs_bo, "memptrs");

	if (nr_rings > ARRAY_SIZE(gpu->rb)) {
		DRM_DEV_INFO_ONCE(drm->dev, "Only creating %zu ringbuffers\n",
			ARRAY_SIZE(gpu->rb));
		nr_rings = ARRAY_SIZE(gpu->rb);
	}

	/* Create ringbuffer(s): */
	for (i = 0; i < nr_rings; i++) {
		gpu->rb[i] = msm_ringbuffer_new(gpu, i, memptrs, memptrs_iova);

		if (IS_ERR(gpu->rb[i])) {
			ret = PTR_ERR(gpu->rb[i]);
			DRM_DEV_ERROR(drm->dev,
				"could not create ringbuffer %d: %d\n", i, ret);
			goto fail;
		}

		memptrs += sizeof(struct msm_rbmemptrs);
		memptrs_iova += sizeof(struct msm_rbmemptrs);
	}

	gpu->nr_rings = nr_rings;

	return 0;

fail:
	for (i = 0; i < ARRAY_SIZE(gpu->rb); i++)  {
		msm_ringbuffer_destroy(gpu->rb[i]);
		gpu->rb[i] = NULL;
	}

	msm_gem_kernel_put(gpu->memptrs_bo, gpu->aspace);

	platform_set_drvdata(pdev, NULL);
	return ret;
}

void msm_gpu_cleanup(struct msm_gpu *gpu)
{
	int i;

	DBG("%s", gpu->name);

	WARN_ON(!list_empty(&gpu->active_list));

	for (i = 0; i < ARRAY_SIZE(gpu->rb); i++) {
		msm_ringbuffer_destroy(gpu->rb[i]);
		gpu->rb[i] = NULL;
	}

	msm_gem_kernel_put(gpu->memptrs_bo, gpu->aspace);

	if (!IS_ERR_OR_NULL(gpu->aspace)) {
		gpu->aspace->mmu->funcs->detach(gpu->aspace->mmu);
		msm_gem_address_space_put(gpu->aspace);
	}

	if (gpu->worker) {
		kthread_destroy_worker(gpu->worker);
	}

	msm_devfreq_cleanup(gpu);
}

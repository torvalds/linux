/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "msm_gpu.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_fence.h"

#include <linux/string_helpers.h>
#include <linux/pm_opp.h>
#include <linux/devfreq.h>


/*
 * Power Management:
 */

static int msm_devfreq_target(struct device *dev, unsigned long *freq,
		u32 flags)
{
	struct msm_gpu *gpu = platform_get_drvdata(to_platform_device(dev));
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dev, freq, flags);

	if (IS_ERR(opp))
		return PTR_ERR(opp);

	clk_set_rate(gpu->core_clk, *freq);
	dev_pm_opp_put(opp);

	return 0;
}

static int msm_devfreq_get_dev_status(struct device *dev,
		struct devfreq_dev_status *status)
{
	struct msm_gpu *gpu = platform_get_drvdata(to_platform_device(dev));
	u64 cycles;
	u32 freq = ((u32) status->current_frequency) / 1000000;
	ktime_t time;

	status->current_frequency = (unsigned long) clk_get_rate(gpu->core_clk);
	gpu->funcs->gpu_busy(gpu, &cycles);

	status->busy_time = ((u32) (cycles - gpu->devfreq.busy_cycles)) / freq;

	gpu->devfreq.busy_cycles = cycles;

	time = ktime_get();
	status->total_time = ktime_us_delta(time, gpu->devfreq.time);
	gpu->devfreq.time = time;

	return 0;
}

static int msm_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct msm_gpu *gpu = platform_get_drvdata(to_platform_device(dev));

	*freq = (unsigned long) clk_get_rate(gpu->core_clk);

	return 0;
}

static struct devfreq_dev_profile msm_devfreq_profile = {
	.polling_ms = 10,
	.target = msm_devfreq_target,
	.get_dev_status = msm_devfreq_get_dev_status,
	.get_cur_freq = msm_devfreq_get_cur_freq,
};

static void msm_devfreq_init(struct msm_gpu *gpu)
{
	/* We need target support to do devfreq */
	if (!gpu->funcs->gpu_busy)
		return;

	msm_devfreq_profile.initial_freq = gpu->fast_rate;

	/*
	 * Don't set the freq_table or max_state and let devfreq build the table
	 * from OPP
	 */

	gpu->devfreq.devfreq = devm_devfreq_add_device(&gpu->pdev->dev,
			&msm_devfreq_profile, "simple_ondemand", NULL);

	if (IS_ERR(gpu->devfreq.devfreq)) {
		dev_err(&gpu->pdev->dev, "Couldn't initialize GPU devfreq\n");
		gpu->devfreq.devfreq = NULL;
	}
}

static int enable_pwrrail(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	int ret = 0;

	if (gpu->gpu_reg) {
		ret = regulator_enable(gpu->gpu_reg);
		if (ret) {
			dev_err(dev->dev, "failed to enable 'gpu_reg': %d\n", ret);
			return ret;
		}
	}

	if (gpu->gpu_cx) {
		ret = regulator_enable(gpu->gpu_cx);
		if (ret) {
			dev_err(dev->dev, "failed to enable 'gpu_cx': %d\n", ret);
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
	int i;

	if (gpu->core_clk && gpu->fast_rate)
		clk_set_rate(gpu->core_clk, gpu->fast_rate);

	/* Set the RBBM timer rate to 19.2Mhz */
	if (gpu->rbbmtimer_clk)
		clk_set_rate(gpu->rbbmtimer_clk, 19200000);

	for (i = gpu->nr_clocks - 1; i >= 0; i--)
		if (gpu->grp_clks[i])
			clk_prepare(gpu->grp_clks[i]);

	for (i = gpu->nr_clocks - 1; i >= 0; i--)
		if (gpu->grp_clks[i])
			clk_enable(gpu->grp_clks[i]);

	return 0;
}

static int disable_clk(struct msm_gpu *gpu)
{
	int i;

	for (i = gpu->nr_clocks - 1; i >= 0; i--)
		if (gpu->grp_clks[i])
			clk_disable(gpu->grp_clks[i]);

	for (i = gpu->nr_clocks - 1; i >= 0; i--)
		if (gpu->grp_clks[i])
			clk_unprepare(gpu->grp_clks[i]);

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
	if (gpu->ebi1_clk)
		clk_prepare_enable(gpu->ebi1_clk);
	return 0;
}

static int disable_axi(struct msm_gpu *gpu)
{
	if (gpu->ebi1_clk)
		clk_disable_unprepare(gpu->ebi1_clk);
	return 0;
}

int msm_gpu_pm_resume(struct msm_gpu *gpu)
{
	int ret;

	DBG("%s", gpu->name);

	ret = enable_pwrrail(gpu);
	if (ret)
		return ret;

	ret = enable_clk(gpu);
	if (ret)
		return ret;

	ret = enable_axi(gpu);
	if (ret)
		return ret;

	if (gpu->devfreq.devfreq) {
		gpu->devfreq.busy_cycles = 0;
		gpu->devfreq.time = ktime_get();

		devfreq_resume_device(gpu->devfreq.devfreq);
	}

	gpu->needs_hw_init = true;

	return 0;
}

int msm_gpu_pm_suspend(struct msm_gpu *gpu)
{
	int ret;

	DBG("%s", gpu->name);

	if (gpu->devfreq.devfreq)
		devfreq_suspend_device(gpu->devfreq.devfreq);

	ret = disable_axi(gpu);
	if (ret)
		return ret;

	ret = disable_clk(gpu);
	if (ret)
		return ret;

	ret = disable_pwrrail(gpu);
	if (ret)
		return ret;

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

/*
 * Hangcheck detection for locked gpu:
 */

static void update_fences(struct msm_gpu *gpu, struct msm_ringbuffer *ring,
		uint32_t fence)
{
	struct msm_gem_submit *submit;

	list_for_each_entry(submit, &ring->submits, node) {
		if (submit->seqno > fence)
			break;

		msm_update_fence(submit->ring->fctx,
			submit->fence->seqno);
	}
}

static struct msm_gem_submit *
find_submit(struct msm_ringbuffer *ring, uint32_t fence)
{
	struct msm_gem_submit *submit;

	WARN_ON(!mutex_is_locked(&ring->gpu->dev->struct_mutex));

	list_for_each_entry(submit, &ring->submits, node)
		if (submit->seqno == fence)
			return submit;

	return NULL;
}

static void retire_submits(struct msm_gpu *gpu);

static void recover_worker(struct work_struct *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, recover_work);
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_submit *submit;
	struct msm_ringbuffer *cur_ring = gpu->funcs->active_ring(gpu);
	int i;

	mutex_lock(&dev->struct_mutex);

	dev_err(dev->dev, "%s: hangcheck recover!\n", gpu->name);

	submit = find_submit(cur_ring, cur_ring->memptrs->fence + 1);
	if (submit) {
		struct task_struct *task;

		rcu_read_lock();
		task = pid_task(submit->pid, PIDTYPE_PID);
		if (task) {
			char *cmd;

			/*
			 * So slightly annoying, in other paths like
			 * mmap'ing gem buffers, mmap_sem is acquired
			 * before struct_mutex, which means we can't
			 * hold struct_mutex across the call to
			 * get_cmdline().  But submits are retired
			 * from the same in-order workqueue, so we can
			 * safely drop the lock here without worrying
			 * about the submit going away.
			 */
			mutex_unlock(&dev->struct_mutex);
			cmd = kstrdup_quotable_cmdline(task, GFP_KERNEL);
			mutex_lock(&dev->struct_mutex);

			dev_err(dev->dev, "%s: offending task: %s (%s)\n",
				gpu->name, task->comm, cmd);

			msm_rd_dump_submit(priv->hangrd, submit,
				"offending task: %s (%s)", task->comm, cmd);

			kfree(cmd);
		} else {
			msm_rd_dump_submit(priv->hangrd, submit, NULL);
		}
		rcu_read_unlock();
	}


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

			list_for_each_entry(submit, &ring->submits, node)
				gpu->funcs->submit(gpu, submit, NULL);
		}
	}

	mutex_unlock(&dev->struct_mutex);

	msm_gpu_retire(gpu);
}

static void hangcheck_timer_reset(struct msm_gpu *gpu)
{
	DBG("%s", gpu->name);
	mod_timer(&gpu->hangcheck_timer,
			round_jiffies_up(jiffies + DRM_MSM_HANGCHECK_JIFFIES));
}

static void hangcheck_handler(struct timer_list *t)
{
	struct msm_gpu *gpu = from_timer(gpu, t, hangcheck_timer);
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_ringbuffer *ring = gpu->funcs->active_ring(gpu);
	uint32_t fence = ring->memptrs->fence;

	if (fence != ring->hangcheck_fence) {
		/* some progress has been made.. ya! */
		ring->hangcheck_fence = fence;
	} else if (fence < ring->seqno) {
		/* no progress and not done.. hung! */
		ring->hangcheck_fence = fence;
		dev_err(dev->dev, "%s: hangcheck detected gpu lockup rb %d!\n",
				gpu->name, ring->id);
		dev_err(dev->dev, "%s:     completed fence: %u\n",
				gpu->name, fence);
		dev_err(dev->dev, "%s:     submitted fence: %u\n",
				gpu->name, ring->seqno);

		queue_work(priv->wq, &gpu->recover_work);
	}

	/* if still more pending work, reset the hangcheck timer: */
	if (ring->seqno > ring->hangcheck_fence)
		hangcheck_timer_reset(gpu);

	/* workaround for missing irq: */
	queue_work(priv->wq, &gpu->retire_work);
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

static void retire_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	int i;

	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;
		/* move to inactive: */
		msm_gem_move_to_inactive(&msm_obj->base);
		msm_gem_put_iova(&msm_obj->base, gpu->aspace);
		drm_gem_object_unreference(&msm_obj->base);
	}

	pm_runtime_mark_last_busy(&gpu->pdev->dev);
	pm_runtime_put_autosuspend(&gpu->pdev->dev);
	msm_gem_submit_free(submit);
}

static void retire_submits(struct msm_gpu *gpu)
{
	struct drm_device *dev = gpu->dev;
	struct msm_gem_submit *submit, *tmp;
	int i;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	/* Retire the commits starting with highest priority */
	for (i = 0; i < gpu->nr_rings; i++) {
		struct msm_ringbuffer *ring = gpu->rb[i];

		list_for_each_entry_safe(submit, tmp, &ring->submits, node) {
			if (dma_fence_is_signaled(submit->fence))
				retire_submit(gpu, submit);
		}
	}
}

static void retire_worker(struct work_struct *work)
{
	struct msm_gpu *gpu = container_of(work, struct msm_gpu, retire_work);
	struct drm_device *dev = gpu->dev;
	int i;

	for (i = 0; i < gpu->nr_rings; i++)
		update_fences(gpu, gpu->rb[i], gpu->rb[i]->memptrs->fence);

	mutex_lock(&dev->struct_mutex);
	retire_submits(gpu);
	mutex_unlock(&dev->struct_mutex);
}

/* call from irq handler to schedule work to retire bo's */
void msm_gpu_retire(struct msm_gpu *gpu)
{
	struct msm_drm_private *priv = gpu->dev->dev_private;
	queue_work(priv->wq, &gpu->retire_work);
	update_sw_cntrs(gpu);
}

/* add bo's to gpu's ring, and kick gpu: */
void msm_gpu_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit,
		struct msm_file_private *ctx)
{
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_ringbuffer *ring = submit->ring;
	int i;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	pm_runtime_get_sync(&gpu->pdev->dev);

	msm_gpu_hw_init(gpu);

	submit->seqno = ++ring->seqno;

	list_add_tail(&submit->node, &ring->submits);

	msm_rd_dump_submit(priv->rd, submit, NULL);

	update_sw_cntrs(gpu);

	for (i = 0; i < submit->nr_bos; i++) {
		struct msm_gem_object *msm_obj = submit->bos[i].obj;
		uint64_t iova;

		/* can't happen yet.. but when we add 2d support we'll have
		 * to deal w/ cross-ring synchronization:
		 */
		WARN_ON(is_active(msm_obj) && (msm_obj->gpu != gpu));

		/* submit takes a reference to the bo and iova until retired: */
		drm_gem_object_reference(&msm_obj->base);
		msm_gem_get_iova(&msm_obj->base,
				submit->gpu->aspace, &iova);

		if (submit->bos[i].flags & MSM_SUBMIT_BO_WRITE)
			msm_gem_move_to_active(&msm_obj->base, gpu, true, submit->fence);
		else if (submit->bos[i].flags & MSM_SUBMIT_BO_READ)
			msm_gem_move_to_active(&msm_obj->base, gpu, false, submit->fence);
	}

	gpu->funcs->submit(gpu, submit, ctx);
	priv->lastctx = ctx;

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

static struct clk *get_clock(struct device *dev, const char *name)
{
	struct clk *clk = devm_clk_get(dev, name);

	return IS_ERR(clk) ? NULL : clk;
}

static int get_clocks(struct platform_device *pdev, struct msm_gpu *gpu)
{
	struct device *dev = &pdev->dev;
	struct property *prop;
	const char *name;
	int i = 0;

	gpu->nr_clocks = of_property_count_strings(dev->of_node, "clock-names");
	if (gpu->nr_clocks < 1) {
		gpu->nr_clocks = 0;
		return 0;
	}

	gpu->grp_clks = devm_kcalloc(dev, sizeof(struct clk *), gpu->nr_clocks,
		GFP_KERNEL);
	if (!gpu->grp_clks)
		return -ENOMEM;

	of_property_for_each_string(dev->of_node, "clock-names", prop, name) {
		gpu->grp_clks[i] = get_clock(dev, name);

		/* Remember the key clocks that we need to control later */
		if (!strcmp(name, "core") || !strcmp(name, "core_clk"))
			gpu->core_clk = gpu->grp_clks[i];
		else if (!strcmp(name, "rbbmtimer") || !strcmp(name, "rbbmtimer_clk"))
			gpu->rbbmtimer_clk = gpu->grp_clks[i];

		++i;
	}

	return 0;
}

static struct msm_gem_address_space *
msm_gpu_create_address_space(struct msm_gpu *gpu, struct platform_device *pdev,
		uint64_t va_start, uint64_t va_end)
{
	struct iommu_domain *iommu;
	struct msm_gem_address_space *aspace;
	int ret;

	/*
	 * Setup IOMMU.. eventually we will (I think) do this once per context
	 * and have separate page tables per context.  For now, to keep things
	 * simple and to get something working, just use a single address space:
	 */
	iommu = iommu_domain_alloc(&platform_bus_type);
	if (!iommu)
		return NULL;

	iommu->geometry.aperture_start = va_start;
	iommu->geometry.aperture_end = va_end;

	dev_info(gpu->dev->dev, "%s: using IOMMU\n", gpu->name);

	aspace = msm_gem_address_space_create(&pdev->dev, iommu, "gpu");
	if (IS_ERR(aspace)) {
		dev_err(gpu->dev->dev, "failed to init iommu: %ld\n",
			PTR_ERR(aspace));
		iommu_domain_free(iommu);
		return ERR_CAST(aspace);
	}

	ret = aspace->mmu->funcs->attach(aspace->mmu, NULL, 0);
	if (ret) {
		msm_gem_address_space_put(aspace);
		return ERR_PTR(ret);
	}

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

	INIT_LIST_HEAD(&gpu->active_list);
	INIT_WORK(&gpu->retire_work, retire_worker);
	INIT_WORK(&gpu->recover_work, recover_worker);


	timer_setup(&gpu->hangcheck_timer, hangcheck_handler, 0);

	spin_lock_init(&gpu->perf_lock);


	/* Map registers: */
	gpu->mmio = msm_ioremap(pdev, config->ioname, name);
	if (IS_ERR(gpu->mmio)) {
		ret = PTR_ERR(gpu->mmio);
		goto fail;
	}

	/* Get Interrupt: */
	gpu->irq = platform_get_irq_byname(pdev, config->irqname);
	if (gpu->irq < 0) {
		ret = gpu->irq;
		dev_err(drm->dev, "failed to get irq: %d\n", ret);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, gpu->irq, irq_handler,
			IRQF_TRIGGER_HIGH, gpu->name, gpu);
	if (ret) {
		dev_err(drm->dev, "failed to request IRQ%u: %d\n", gpu->irq, ret);
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
	platform_set_drvdata(pdev, gpu);

	msm_devfreq_init(gpu);

	gpu->aspace = msm_gpu_create_address_space(gpu, pdev,
		config->va_start, config->va_end);

	if (gpu->aspace == NULL)
		dev_info(drm->dev, "%s: no IOMMU, fallback to VRAM carveout!\n", name);
	else if (IS_ERR(gpu->aspace)) {
		ret = PTR_ERR(gpu->aspace);
		goto fail;
	}

	memptrs = msm_gem_kernel_new(drm, sizeof(*gpu->memptrs_bo),
		MSM_BO_UNCACHED, gpu->aspace, &gpu->memptrs_bo,
		&memptrs_iova);

	if (IS_ERR(memptrs)) {
		ret = PTR_ERR(memptrs);
		dev_err(drm->dev, "could not allocate memptrs: %d\n", ret);
		goto fail;
	}

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
			dev_err(drm->dev,
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

	if (gpu->memptrs_bo) {
		msm_gem_put_vaddr(gpu->memptrs_bo);
		msm_gem_put_iova(gpu->memptrs_bo, gpu->aspace);
		drm_gem_object_unreference_unlocked(gpu->memptrs_bo);
	}

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

	if (gpu->memptrs_bo) {
		msm_gem_put_vaddr(gpu->memptrs_bo);
		msm_gem_put_iova(gpu->memptrs_bo, gpu->aspace);
		drm_gem_object_unreference_unlocked(gpu->memptrs_bo);
	}

	if (!IS_ERR_OR_NULL(gpu->aspace)) {
		gpu->aspace->mmu->funcs->detach(gpu->aspace->mmu,
			NULL, 0);
		msm_gem_address_space_put(gpu->aspace);
	}
}

// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */
/* Copyright 2025 Arm, Ltd. */

#include <linux/bitfield.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>
#include <drm/ethosu_accel.h>

#include "ethosu_device.h"
#include "ethosu_drv.h"
#include "ethosu_gem.h"
#include "ethosu_job.h"

#define JOB_TIMEOUT_MS 500

static struct ethosu_job *to_ethosu_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct ethosu_job, base);
}

static const char *ethosu_fence_get_driver_name(struct dma_fence *fence)
{
	return "ethosu";
}

static const char *ethosu_fence_get_timeline_name(struct dma_fence *fence)
{
	return "ethosu-npu";
}

static const struct dma_fence_ops ethosu_fence_ops = {
	.get_driver_name = ethosu_fence_get_driver_name,
	.get_timeline_name = ethosu_fence_get_timeline_name,
};

static void ethosu_job_hw_submit(struct ethosu_device *dev, struct ethosu_job *job)
{
	struct drm_gem_dma_object *cmd_bo = to_drm_gem_dma_obj(job->cmd_bo);
	struct ethosu_validated_cmdstream_info *cmd_info = to_ethosu_bo(job->cmd_bo)->info;

	for (int i = 0; i < job->region_cnt; i++) {
		struct drm_gem_dma_object *bo;
		int region = job->region_bo_num[i];

		bo = to_drm_gem_dma_obj(job->region_bo[i]);
		writel_relaxed(lower_32_bits(bo->dma_addr), dev->regs + NPU_REG_BASEP(region));
		writel_relaxed(upper_32_bits(bo->dma_addr), dev->regs + NPU_REG_BASEP_HI(region));
		dev_dbg(dev->base.dev, "Region %d base addr = %pad\n", region, &bo->dma_addr);
	}

	if (job->sram_size) {
		writel_relaxed(lower_32_bits(dev->sramphys),
			       dev->regs + NPU_REG_BASEP(ETHOSU_SRAM_REGION));
		writel_relaxed(upper_32_bits(dev->sramphys),
			       dev->regs + NPU_REG_BASEP_HI(ETHOSU_SRAM_REGION));
		dev_dbg(dev->base.dev, "Region %d base addr = %pad (SRAM)\n",
			ETHOSU_SRAM_REGION, &dev->sramphys);
	}

	writel_relaxed(lower_32_bits(cmd_bo->dma_addr), dev->regs + NPU_REG_QBASE);
	writel_relaxed(upper_32_bits(cmd_bo->dma_addr), dev->regs + NPU_REG_QBASE_HI);
	writel_relaxed(cmd_info->cmd_size, dev->regs + NPU_REG_QSIZE);

	writel(CMD_TRANSITION_TO_RUN, dev->regs + NPU_REG_CMD);

	dev_dbg(dev->base.dev,
		"Submitted cmd at %pad to core\n", &cmd_bo->dma_addr);
}

static int ethosu_acquire_object_fences(struct ethosu_job *job)
{
	int i, ret;
	struct drm_gem_object **bos = job->region_bo;
	struct ethosu_validated_cmdstream_info *info = to_ethosu_bo(job->cmd_bo)->info;

	for (i = 0; i < job->region_cnt; i++) {
		bool is_write;

		if (!bos[i])
			break;

		ret = dma_resv_reserve_fences(bos[i]->resv, 1);
		if (ret)
			return ret;

		is_write = info->output_region[job->region_bo_num[i]];
		ret = drm_sched_job_add_implicit_dependencies(&job->base, bos[i],
							      is_write);
		if (ret)
			return ret;
	}

	return 0;
}

static void ethosu_attach_object_fences(struct ethosu_job *job)
{
	int i;
	struct dma_fence *fence = job->inference_done_fence;
	struct drm_gem_object **bos = job->region_bo;
	struct ethosu_validated_cmdstream_info *info = to_ethosu_bo(job->cmd_bo)->info;

	for (i = 0; i < job->region_cnt; i++)
		if (info->output_region[job->region_bo_num[i]])
			dma_resv_add_fence(bos[i]->resv, fence, DMA_RESV_USAGE_WRITE);
}

static int ethosu_job_push(struct ethosu_job *job)
{
	struct ww_acquire_ctx acquire_ctx;
	int ret;

	ret = drm_gem_lock_reservations(job->region_bo, job->region_cnt, &acquire_ctx);
	if (ret)
		return ret;

	ret = ethosu_acquire_object_fences(job);
	if (ret)
		goto out;

	ret = pm_runtime_resume_and_get(job->dev->base.dev);
	if (!ret) {
		guard(mutex)(&job->dev->sched_lock);

		drm_sched_job_arm(&job->base);
		job->inference_done_fence = dma_fence_get(&job->base.s_fence->finished);
		kref_get(&job->refcount); /* put by scheduler job completion */
		drm_sched_entity_push_job(&job->base);
		ethosu_attach_object_fences(job);
	}

out:
	drm_gem_unlock_reservations(job->region_bo, job->region_cnt, &acquire_ctx);
	return ret;
}

static void ethosu_job_cleanup(struct kref *ref)
{
	struct ethosu_job *job = container_of(ref, struct ethosu_job,
						refcount);
	unsigned int i;

	pm_runtime_put_autosuspend(job->dev->base.dev);

	dma_fence_put(job->done_fence);
	dma_fence_put(job->inference_done_fence);

	for (i = 0; i < job->region_cnt; i++)
		drm_gem_object_put(job->region_bo[i]);

	drm_gem_object_put(job->cmd_bo);

	kfree(job);
}

static void ethosu_job_put(struct ethosu_job *job)
{
	kref_put(&job->refcount, ethosu_job_cleanup);
}

static void ethosu_job_free(struct drm_sched_job *sched_job)
{
	struct ethosu_job *job = to_ethosu_job(sched_job);

	drm_sched_job_cleanup(sched_job);
	ethosu_job_put(job);
}

static struct dma_fence *ethosu_job_run(struct drm_sched_job *sched_job)
{
	struct ethosu_job *job = to_ethosu_job(sched_job);
	struct ethosu_device *dev = job->dev;
	struct dma_fence *fence = job->done_fence;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	dma_fence_init(fence, &ethosu_fence_ops, &dev->fence_lock,
		       dev->fence_context, ++dev->emit_seqno);
	dma_fence_get(fence);

	scoped_guard(mutex, &dev->job_lock) {
		dev->in_flight_job = job;
		ethosu_job_hw_submit(dev, job);
	}

	return fence;
}

static void ethosu_job_handle_irq(struct ethosu_device *dev)
{
	u32 status = readl_relaxed(dev->regs + NPU_REG_STATUS);

	if (status & (STATUS_BUS_STATUS | STATUS_CMD_PARSE_ERR)) {
		dev_err(dev->base.dev, "Error IRQ - %x\n", status);
		drm_sched_fault(&dev->sched);
		return;
	}

	scoped_guard(mutex, &dev->job_lock) {
		if (dev->in_flight_job) {
			dma_fence_signal(dev->in_flight_job->done_fence);
			dev->in_flight_job = NULL;
		}
	}
}

static irqreturn_t ethosu_job_irq_handler_thread(int irq, void *data)
{
	struct ethosu_device *dev = data;

	ethosu_job_handle_irq(dev);

	return IRQ_HANDLED;
}

static irqreturn_t ethosu_job_irq_handler(int irq, void *data)
{
	struct ethosu_device *dev = data;
	u32 status = readl_relaxed(dev->regs + NPU_REG_STATUS);

	if (!(status & STATUS_IRQ_RAISED))
		return IRQ_NONE;

	writel_relaxed(CMD_CLEAR_IRQ, dev->regs + NPU_REG_CMD);
	return IRQ_WAKE_THREAD;
}

static enum drm_gpu_sched_stat ethosu_job_timedout(struct drm_sched_job *bad)
{
	struct ethosu_job *job = to_ethosu_job(bad);
	struct ethosu_device *dev = job->dev;
	bool running;
	u32 *bocmds = to_drm_gem_dma_obj(job->cmd_bo)->vaddr;
	u32 cmdaddr;

	cmdaddr = readl_relaxed(dev->regs + NPU_REG_QREAD);
	running = FIELD_GET(STATUS_STATE_RUNNING, readl_relaxed(dev->regs + NPU_REG_STATUS));

	if (running) {
		int ret;
		u32 reg;

		ret = readl_relaxed_poll_timeout(dev->regs + NPU_REG_QREAD,
						 reg,
						 reg != cmdaddr,
						 USEC_PER_MSEC, 100 * USEC_PER_MSEC);

		/* If still running and progress is being made, just return */
		if (!ret)
			return DRM_GPU_SCHED_STAT_NO_HANG;
	}

	dev_err(dev->base.dev, "NPU sched timed out: NPU %s, cmdstream offset 0x%x: 0x%x\n",
		running ? "running" : "stopped",
		cmdaddr, bocmds[cmdaddr / 4]);

	drm_sched_stop(&dev->sched, bad);

	scoped_guard(mutex, &dev->job_lock)
		dev->in_flight_job = NULL;

	/* Proceed with reset now. */
	pm_runtime_force_suspend(dev->base.dev);
	pm_runtime_force_resume(dev->base.dev);

	/* Restart the scheduler */
	drm_sched_start(&dev->sched, 0);

	return DRM_GPU_SCHED_STAT_RESET;
}

static const struct drm_sched_backend_ops ethosu_sched_ops = {
	.run_job = ethosu_job_run,
	.timedout_job = ethosu_job_timedout,
	.free_job = ethosu_job_free
};

int ethosu_job_init(struct ethosu_device *edev)
{
	struct device *dev = edev->base.dev;
	struct drm_sched_init_args args = {
		.ops = &ethosu_sched_ops,
		.num_rqs = DRM_SCHED_PRIORITY_COUNT,
		.credit_limit = 1,
		.timeout = msecs_to_jiffies(JOB_TIMEOUT_MS),
		.name = dev_name(dev),
		.dev = dev,
	};
	int ret;

	spin_lock_init(&edev->fence_lock);
	ret = devm_mutex_init(dev, &edev->job_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(dev, &edev->sched_lock);
	if (ret)
		return ret;

	edev->irq = platform_get_irq(to_platform_device(dev), 0);
	if (edev->irq < 0)
		return edev->irq;

	ret = devm_request_threaded_irq(dev, edev->irq,
					ethosu_job_irq_handler,
					ethosu_job_irq_handler_thread,
					IRQF_SHARED, KBUILD_MODNAME,
					edev);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	edev->fence_context = dma_fence_context_alloc(1);

	ret = drm_sched_init(&edev->sched, &args);
	if (ret) {
		dev_err(dev, "Failed to create scheduler: %d\n", ret);
		goto err_sched;
	}

	return 0;

err_sched:
	drm_sched_fini(&edev->sched);
	return ret;
}

void ethosu_job_fini(struct ethosu_device *dev)
{
	drm_sched_fini(&dev->sched);
}

int ethosu_job_open(struct ethosu_file_priv *ethosu_priv)
{
	struct ethosu_device *dev = ethosu_priv->edev;
	struct drm_gpu_scheduler *sched = &dev->sched;
	int ret;

	ret = drm_sched_entity_init(&ethosu_priv->sched_entity,
				    DRM_SCHED_PRIORITY_NORMAL,
				    &sched, 1, NULL);
	return WARN_ON(ret);
}

void ethosu_job_close(struct ethosu_file_priv *ethosu_priv)
{
	struct drm_sched_entity *entity = &ethosu_priv->sched_entity;

	drm_sched_entity_destroy(entity);
}

static int ethosu_ioctl_submit_job(struct drm_device *dev, struct drm_file *file,
				   struct drm_ethosu_job *job)
{
	struct ethosu_device *edev = to_ethosu_device(dev);
	struct ethosu_file_priv *file_priv = file->driver_priv;
	struct ethosu_job *ejob = NULL;
	struct ethosu_validated_cmdstream_info *cmd_info;
	int ret = 0;

	/* BO region 2 is reserved if SRAM is used */
	if (job->region_bo_handles[ETHOSU_SRAM_REGION] && job->sram_size)
		return -EINVAL;

	if (edev->npu_info.sram_size < job->sram_size)
		return -EINVAL;

	ejob = kzalloc(sizeof(*ejob), GFP_KERNEL);
	if (!ejob)
		return -ENOMEM;

	kref_init(&ejob->refcount);

	ejob->dev = edev;
	ejob->sram_size = job->sram_size;

	ejob->done_fence = kzalloc(sizeof(*ejob->done_fence), GFP_KERNEL);
	if (!ejob->done_fence) {
		ret = -ENOMEM;
		goto out_cleanup_job;
	}

	ret = drm_sched_job_init(&ejob->base,
				 &file_priv->sched_entity,
				 1, NULL, file->client_id);
	if (ret)
		goto out_put_job;

	ejob->cmd_bo = drm_gem_object_lookup(file, job->cmd_bo);
	if (!ejob->cmd_bo) {
		ret = -ENOENT;
		goto out_cleanup_job;
	}
	cmd_info = to_ethosu_bo(ejob->cmd_bo)->info;
	if (!cmd_info) {
		ret = -EINVAL;
		goto out_cleanup_job;
	}

	for (int i = 0; i < NPU_BASEP_REGION_MAX; i++) {
		struct drm_gem_object *gem;

		/* Can only omit a BO handle if the region is not used or used for SRAM */
		if (!job->region_bo_handles[i] &&
		    (!cmd_info->region_size[i] || (i == ETHOSU_SRAM_REGION && job->sram_size)))
			continue;

		if (job->region_bo_handles[i] && !cmd_info->region_size[i]) {
			dev_err(dev->dev,
				"Cmdstream BO handle %d set for unused region %d\n",
				job->region_bo_handles[i], i);
			ret = -EINVAL;
			goto out_cleanup_job;
		}

		gem = drm_gem_object_lookup(file, job->region_bo_handles[i]);
		if (!gem) {
			dev_err(dev->dev,
				"Invalid BO handle %d for region %d\n",
				job->region_bo_handles[i], i);
			ret = -ENOENT;
			goto out_cleanup_job;
		}

		ejob->region_bo[ejob->region_cnt] = gem;
		ejob->region_bo_num[ejob->region_cnt] = i;
		ejob->region_cnt++;

		if (to_ethosu_bo(gem)->info) {
			dev_err(dev->dev,
				"Cmdstream BO handle %d used for region %d\n",
				job->region_bo_handles[i], i);
			ret = -EINVAL;
			goto out_cleanup_job;
		}

		/* Verify the command stream doesn't have accesses outside the BO */
		if (cmd_info->region_size[i] > gem->size) {
			dev_err(dev->dev,
				"cmd stream region %d size greater than BO size (%llu > %zu)\n",
				i, cmd_info->region_size[i], gem->size);
			ret = -EOVERFLOW;
			goto out_cleanup_job;
		}
	}
	ret = ethosu_job_push(ejob);

out_cleanup_job:
	if (ret)
		drm_sched_job_cleanup(&ejob->base);
out_put_job:
	ethosu_job_put(ejob);

	return ret;
}

int ethosu_ioctl_submit(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_ethosu_submit *args = data;
	int ret = 0;
	unsigned int i = 0;

	if (args->pad) {
		drm_dbg(dev, "Reserved field in drm_ethosu_submit struct should be 0.\n");
		return -EINVAL;
	}

	struct drm_ethosu_job __free(kvfree) *jobs =
		kvmalloc_array(args->job_count, sizeof(*jobs), GFP_KERNEL);
	if (!jobs)
		return -ENOMEM;

	if (copy_from_user(jobs,
			   (void __user *)(uintptr_t)args->jobs,
			   args->job_count * sizeof(*jobs))) {
		drm_dbg(dev, "Failed to copy incoming job array\n");
		return -EFAULT;
	}

	for (i = 0; i < args->job_count; i++) {
		ret = ethosu_ioctl_submit_job(dev, file, &jobs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

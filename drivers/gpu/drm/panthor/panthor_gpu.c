// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd., Rob Herring <robh@kernel.org> */
/* Copyright 2019 Collabora ltd. */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>

#include "panthor_device.h"
#include "panthor_gpu.h"
#include "panthor_regs.h"

/**
 * struct panthor_gpu - GPU block management data.
 */
struct panthor_gpu {
	/** @irq: GPU irq. */
	struct panthor_irq irq;

	/** @reqs_lock: Lock protecting access to pending_reqs. */
	spinlock_t reqs_lock;

	/** @pending_reqs: Pending GPU requests. */
	u32 pending_reqs;

	/** @reqs_acked: GPU request wait queue. */
	wait_queue_head_t reqs_acked;

	/** @cache_flush_lock: Lock to serialize cache flushes */
	struct mutex cache_flush_lock;
};

#define GPU_INTERRUPTS_MASK	\
	(GPU_IRQ_FAULT | \
	 GPU_IRQ_PROTM_FAULT | \
	 GPU_IRQ_RESET_COMPLETED | \
	 GPU_IRQ_CLEAN_CACHES_COMPLETED)

static void panthor_gpu_coherency_set(struct panthor_device *ptdev)
{
	gpu_write(ptdev, GPU_COHERENCY_PROTOCOL,
		ptdev->coherent ? GPU_COHERENCY_PROT_BIT(ACE_LITE) : GPU_COHERENCY_NONE);
}

static void panthor_gpu_irq_handler(struct panthor_device *ptdev, u32 status)
{
	gpu_write(ptdev, GPU_INT_CLEAR, status);

	if (status & GPU_IRQ_FAULT) {
		u32 fault_status = gpu_read(ptdev, GPU_FAULT_STATUS);
		u64 address = gpu_read64(ptdev, GPU_FAULT_ADDR);

		drm_warn(&ptdev->base, "GPU Fault 0x%08x (%s) at 0x%016llx\n",
			 fault_status, panthor_exception_name(ptdev, fault_status & 0xFF),
			 address);
	}
	if (status & GPU_IRQ_PROTM_FAULT)
		drm_warn(&ptdev->base, "GPU Fault in protected mode\n");

	spin_lock(&ptdev->gpu->reqs_lock);
	if (status & ptdev->gpu->pending_reqs) {
		ptdev->gpu->pending_reqs &= ~status;
		wake_up_all(&ptdev->gpu->reqs_acked);
	}
	spin_unlock(&ptdev->gpu->reqs_lock);
}
PANTHOR_IRQ_HANDLER(gpu, GPU, panthor_gpu_irq_handler);

/**
 * panthor_gpu_unplug() - Called when the GPU is unplugged.
 * @ptdev: Device to unplug.
 */
void panthor_gpu_unplug(struct panthor_device *ptdev)
{
	unsigned long flags;

	/* Make sure the IRQ handler is not running after that point. */
	if (!IS_ENABLED(CONFIG_PM) || pm_runtime_active(ptdev->base.dev))
		panthor_gpu_irq_suspend(&ptdev->gpu->irq);

	/* Wake-up all waiters. */
	spin_lock_irqsave(&ptdev->gpu->reqs_lock, flags);
	ptdev->gpu->pending_reqs = 0;
	wake_up_all(&ptdev->gpu->reqs_acked);
	spin_unlock_irqrestore(&ptdev->gpu->reqs_lock, flags);
}

/**
 * panthor_gpu_init() - Initialize the GPU block
 * @ptdev: Device.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_gpu_init(struct panthor_device *ptdev)
{
	struct panthor_gpu *gpu;
	u32 pa_bits;
	int ret, irq;

	gpu = drmm_kzalloc(&ptdev->base, sizeof(*gpu), GFP_KERNEL);
	if (!gpu)
		return -ENOMEM;

	spin_lock_init(&gpu->reqs_lock);
	init_waitqueue_head(&gpu->reqs_acked);
	mutex_init(&gpu->cache_flush_lock);
	ptdev->gpu = gpu;

	dma_set_max_seg_size(ptdev->base.dev, UINT_MAX);
	pa_bits = GPU_MMU_FEATURES_PA_BITS(ptdev->gpu_info.mmu_features);
	ret = dma_set_mask_and_coherent(ptdev->base.dev, DMA_BIT_MASK(pa_bits));
	if (ret)
		return ret;

	irq = platform_get_irq_byname(to_platform_device(ptdev->base.dev), "gpu");
	if (irq < 0)
		return irq;

	ret = panthor_request_gpu_irq(ptdev, &ptdev->gpu->irq, irq, GPU_INTERRUPTS_MASK);
	if (ret)
		return ret;

	return 0;
}

/**
 * panthor_gpu_block_power_off() - Power-off a specific block of the GPU
 * @ptdev: Device.
 * @blk_name: Block name.
 * @pwroff_reg: Power-off register for this block.
 * @pwrtrans_reg: Power transition register for this block.
 * @mask: Sub-elements to power-off.
 * @timeout_us: Timeout in microseconds.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_gpu_block_power_off(struct panthor_device *ptdev,
				const char *blk_name,
				u32 pwroff_reg, u32 pwrtrans_reg,
				u64 mask, u32 timeout_us)
{
	u32 val;
	int ret;

	ret = gpu_read64_relaxed_poll_timeout(ptdev, pwrtrans_reg, val,
					      !(mask & val), 100, timeout_us);
	if (ret) {
		drm_err(&ptdev->base,
			"timeout waiting on %s:%llx power transition", blk_name,
			mask);
		return ret;
	}

	gpu_write64(ptdev, pwroff_reg, mask);

	ret = gpu_read64_relaxed_poll_timeout(ptdev, pwrtrans_reg, val,
					      !(mask & val), 100, timeout_us);
	if (ret) {
		drm_err(&ptdev->base,
			"timeout waiting on %s:%llx power transition", blk_name,
			mask);
		return ret;
	}

	return 0;
}

/**
 * panthor_gpu_block_power_on() - Power-on a specific block of the GPU
 * @ptdev: Device.
 * @blk_name: Block name.
 * @pwron_reg: Power-on register for this block.
 * @pwrtrans_reg: Power transition register for this block.
 * @rdy_reg: Power transition ready register.
 * @mask: Sub-elements to power-on.
 * @timeout_us: Timeout in microseconds.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_gpu_block_power_on(struct panthor_device *ptdev,
			       const char *blk_name,
			       u32 pwron_reg, u32 pwrtrans_reg,
			       u32 rdy_reg, u64 mask, u32 timeout_us)
{
	u32 val;
	int ret;

	ret = gpu_read64_relaxed_poll_timeout(ptdev, pwrtrans_reg, val,
					      !(mask & val), 100, timeout_us);
	if (ret) {
		drm_err(&ptdev->base,
			"timeout waiting on %s:%llx power transition", blk_name,
			mask);
		return ret;
	}

	gpu_write64(ptdev, pwron_reg, mask);

	ret = gpu_read64_relaxed_poll_timeout(ptdev, rdy_reg, val,
					      (mask & val) == val,
					      100, timeout_us);
	if (ret) {
		drm_err(&ptdev->base, "timeout waiting on %s:%llx readiness",
			blk_name, mask);
		return ret;
	}

	return 0;
}

/**
 * panthor_gpu_l2_power_on() - Power-on the L2-cache
 * @ptdev: Device.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_gpu_l2_power_on(struct panthor_device *ptdev)
{
	if (ptdev->gpu_info.l2_present != 1) {
		/*
		 * Only support one core group now.
		 * ~(l2_present - 1) unsets all bits in l2_present except
		 * the bottom bit. (l2_present - 2) has all the bits in
		 * the first core group set. AND them together to generate
		 * a mask of cores in the first core group.
		 */
		u64 core_mask = ~(ptdev->gpu_info.l2_present - 1) &
				(ptdev->gpu_info.l2_present - 2);
		drm_info_once(&ptdev->base, "using only 1st core group (%lu cores from %lu)\n",
			      hweight64(core_mask),
			      hweight64(ptdev->gpu_info.shader_present));
	}

	/* Set the desired coherency mode before the power up of L2 */
	panthor_gpu_coherency_set(ptdev);

	return panthor_gpu_power_on(ptdev, L2, 1, 20000);
}

/**
 * panthor_gpu_flush_caches() - Flush caches
 * @ptdev: Device.
 * @l2: L2 flush type.
 * @lsc: LSC flush type.
 * @other: Other flush type.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_gpu_flush_caches(struct panthor_device *ptdev,
			     u32 l2, u32 lsc, u32 other)
{
	bool timedout = false;
	unsigned long flags;

	/* Serialize cache flush operations. */
	guard(mutex)(&ptdev->gpu->cache_flush_lock);

	spin_lock_irqsave(&ptdev->gpu->reqs_lock, flags);
	if (!drm_WARN_ON(&ptdev->base,
			 ptdev->gpu->pending_reqs & GPU_IRQ_CLEAN_CACHES_COMPLETED)) {
		ptdev->gpu->pending_reqs |= GPU_IRQ_CLEAN_CACHES_COMPLETED;
		gpu_write(ptdev, GPU_CMD, GPU_FLUSH_CACHES(l2, lsc, other));
	}
	spin_unlock_irqrestore(&ptdev->gpu->reqs_lock, flags);

	if (!wait_event_timeout(ptdev->gpu->reqs_acked,
				!(ptdev->gpu->pending_reqs & GPU_IRQ_CLEAN_CACHES_COMPLETED),
				msecs_to_jiffies(100))) {
		spin_lock_irqsave(&ptdev->gpu->reqs_lock, flags);
		if ((ptdev->gpu->pending_reqs & GPU_IRQ_CLEAN_CACHES_COMPLETED) != 0 &&
		    !(gpu_read(ptdev, GPU_INT_RAWSTAT) & GPU_IRQ_CLEAN_CACHES_COMPLETED))
			timedout = true;
		else
			ptdev->gpu->pending_reqs &= ~GPU_IRQ_CLEAN_CACHES_COMPLETED;
		spin_unlock_irqrestore(&ptdev->gpu->reqs_lock, flags);
	}

	if (timedout) {
		drm_err(&ptdev->base, "Flush caches timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * panthor_gpu_soft_reset() - Issue a soft-reset
 * @ptdev: Device.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_gpu_soft_reset(struct panthor_device *ptdev)
{
	bool timedout = false;
	unsigned long flags;

	spin_lock_irqsave(&ptdev->gpu->reqs_lock, flags);
	if (!drm_WARN_ON(&ptdev->base,
			 ptdev->gpu->pending_reqs & GPU_IRQ_RESET_COMPLETED)) {
		ptdev->gpu->pending_reqs |= GPU_IRQ_RESET_COMPLETED;
		gpu_write(ptdev, GPU_INT_CLEAR, GPU_IRQ_RESET_COMPLETED);
		gpu_write(ptdev, GPU_CMD, GPU_SOFT_RESET);
	}
	spin_unlock_irqrestore(&ptdev->gpu->reqs_lock, flags);

	if (!wait_event_timeout(ptdev->gpu->reqs_acked,
				!(ptdev->gpu->pending_reqs & GPU_IRQ_RESET_COMPLETED),
				msecs_to_jiffies(100))) {
		spin_lock_irqsave(&ptdev->gpu->reqs_lock, flags);
		if ((ptdev->gpu->pending_reqs & GPU_IRQ_RESET_COMPLETED) != 0 &&
		    !(gpu_read(ptdev, GPU_INT_RAWSTAT) & GPU_IRQ_RESET_COMPLETED))
			timedout = true;
		else
			ptdev->gpu->pending_reqs &= ~GPU_IRQ_RESET_COMPLETED;
		spin_unlock_irqrestore(&ptdev->gpu->reqs_lock, flags);
	}

	if (timedout) {
		drm_err(&ptdev->base, "Soft reset timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * panthor_gpu_suspend() - Suspend the GPU block.
 * @ptdev: Device.
 *
 * Suspend the GPU irq. This should be called last in the suspend procedure,
 * after all other blocks have been suspented.
 */
void panthor_gpu_suspend(struct panthor_device *ptdev)
{
	/* On a fast reset, simply power down the L2. */
	if (!ptdev->reset.fast)
		panthor_gpu_soft_reset(ptdev);
	else
		panthor_gpu_power_off(ptdev, L2, 1, 20000);

	panthor_gpu_irq_suspend(&ptdev->gpu->irq);
}

/**
 * panthor_gpu_resume() - Resume the GPU block.
 * @ptdev: Device.
 *
 * Resume the IRQ handler and power-on the L2-cache.
 * The FW takes care of powering the other blocks.
 */
void panthor_gpu_resume(struct panthor_device *ptdev)
{
	panthor_gpu_irq_resume(&ptdev->gpu->irq, GPU_INTERRUPTS_MASK);
	panthor_gpu_l2_power_on(ptdev);
}


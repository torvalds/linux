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
};

/**
 * struct panthor_model - GPU model description
 */
struct panthor_model {
	/** @name: Model name. */
	const char *name;

	/** @arch_major: Major version number of architecture. */
	u8 arch_major;

	/** @product_major: Major version number of product. */
	u8 product_major;
};

/**
 * GPU_MODEL() - Define a GPU model. A GPU product can be uniquely identified
 * by a combination of the major architecture version and the major product
 * version.
 * @_name: Name for the GPU model.
 * @_arch_major: Architecture major.
 * @_product_major: Product major.
 */
#define GPU_MODEL(_name, _arch_major, _product_major) \
{\
	.name = __stringify(_name),				\
	.arch_major = _arch_major,				\
	.product_major = _product_major,			\
}

static const struct panthor_model gpu_models[] = {
	GPU_MODEL(g610, 10, 7),
	{},
};

#define GPU_INTERRUPTS_MASK	\
	(GPU_IRQ_FAULT | \
	 GPU_IRQ_PROTM_FAULT | \
	 GPU_IRQ_RESET_COMPLETED | \
	 GPU_IRQ_CLEAN_CACHES_COMPLETED)

static void panthor_gpu_init_info(struct panthor_device *ptdev)
{
	const struct panthor_model *model;
	u32 arch_major, product_major;
	u32 major, minor, status;
	unsigned int i;

	ptdev->gpu_info.gpu_id = gpu_read(ptdev, GPU_ID);
	ptdev->gpu_info.csf_id = gpu_read(ptdev, GPU_CSF_ID);
	ptdev->gpu_info.gpu_rev = gpu_read(ptdev, GPU_REVID);
	ptdev->gpu_info.core_features = gpu_read(ptdev, GPU_CORE_FEATURES);
	ptdev->gpu_info.l2_features = gpu_read(ptdev, GPU_L2_FEATURES);
	ptdev->gpu_info.tiler_features = gpu_read(ptdev, GPU_TILER_FEATURES);
	ptdev->gpu_info.mem_features = gpu_read(ptdev, GPU_MEM_FEATURES);
	ptdev->gpu_info.mmu_features = gpu_read(ptdev, GPU_MMU_FEATURES);
	ptdev->gpu_info.thread_features = gpu_read(ptdev, GPU_THREAD_FEATURES);
	ptdev->gpu_info.max_threads = gpu_read(ptdev, GPU_THREAD_MAX_THREADS);
	ptdev->gpu_info.thread_max_workgroup_size = gpu_read(ptdev, GPU_THREAD_MAX_WORKGROUP_SIZE);
	ptdev->gpu_info.thread_max_barrier_size = gpu_read(ptdev, GPU_THREAD_MAX_BARRIER_SIZE);
	ptdev->gpu_info.coherency_features = gpu_read(ptdev, GPU_COHERENCY_FEATURES);
	for (i = 0; i < 4; i++)
		ptdev->gpu_info.texture_features[i] = gpu_read(ptdev, GPU_TEXTURE_FEATURES(i));

	ptdev->gpu_info.as_present = gpu_read(ptdev, GPU_AS_PRESENT);

	ptdev->gpu_info.shader_present = gpu_read(ptdev, GPU_SHADER_PRESENT_LO);
	ptdev->gpu_info.shader_present |= (u64)gpu_read(ptdev, GPU_SHADER_PRESENT_HI) << 32;

	ptdev->gpu_info.tiler_present = gpu_read(ptdev, GPU_TILER_PRESENT_LO);
	ptdev->gpu_info.tiler_present |= (u64)gpu_read(ptdev, GPU_TILER_PRESENT_HI) << 32;

	ptdev->gpu_info.l2_present = gpu_read(ptdev, GPU_L2_PRESENT_LO);
	ptdev->gpu_info.l2_present |= (u64)gpu_read(ptdev, GPU_L2_PRESENT_HI) << 32;

	arch_major = GPU_ARCH_MAJOR(ptdev->gpu_info.gpu_id);
	product_major = GPU_PROD_MAJOR(ptdev->gpu_info.gpu_id);
	major = GPU_VER_MAJOR(ptdev->gpu_info.gpu_id);
	minor = GPU_VER_MINOR(ptdev->gpu_info.gpu_id);
	status = GPU_VER_STATUS(ptdev->gpu_info.gpu_id);

	for (model = gpu_models; model->name; model++) {
		if (model->arch_major == arch_major &&
		    model->product_major == product_major)
			break;
	}

	drm_info(&ptdev->base,
		 "mali-%s id 0x%x major 0x%x minor 0x%x status 0x%x",
		 model->name ?: "unknown", ptdev->gpu_info.gpu_id >> 16,
		 major, minor, status);

	drm_info(&ptdev->base,
		 "Features: L2:%#x Tiler:%#x Mem:%#x MMU:%#x AS:%#x",
		 ptdev->gpu_info.l2_features,
		 ptdev->gpu_info.tiler_features,
		 ptdev->gpu_info.mem_features,
		 ptdev->gpu_info.mmu_features,
		 ptdev->gpu_info.as_present);

	drm_info(&ptdev->base,
		 "shader_present=0x%0llx l2_present=0x%0llx tiler_present=0x%0llx",
		 ptdev->gpu_info.shader_present, ptdev->gpu_info.l2_present,
		 ptdev->gpu_info.tiler_present);
}

static void panthor_gpu_irq_handler(struct panthor_device *ptdev, u32 status)
{
	if (status & GPU_IRQ_FAULT) {
		u32 fault_status = gpu_read(ptdev, GPU_FAULT_STATUS);
		u64 address = ((u64)gpu_read(ptdev, GPU_FAULT_ADDR_HI) << 32) |
			      gpu_read(ptdev, GPU_FAULT_ADDR_LO);

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
	ptdev->gpu = gpu;
	panthor_gpu_init_info(ptdev);

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
	u32 val, i;
	int ret;

	for (i = 0; i < 2; i++) {
		u32 mask32 = mask >> (i * 32);

		if (!mask32)
			continue;

		ret = readl_relaxed_poll_timeout(ptdev->iomem + pwrtrans_reg + (i * 4),
						 val, !(mask32 & val),
						 100, timeout_us);
		if (ret) {
			drm_err(&ptdev->base, "timeout waiting on %s:%llx power transition",
				blk_name, mask);
			return ret;
		}
	}

	if (mask & GENMASK(31, 0))
		gpu_write(ptdev, pwroff_reg, mask);

	if (mask >> 32)
		gpu_write(ptdev, pwroff_reg + 4, mask >> 32);

	for (i = 0; i < 2; i++) {
		u32 mask32 = mask >> (i * 32);

		if (!mask32)
			continue;

		ret = readl_relaxed_poll_timeout(ptdev->iomem + pwrtrans_reg + (i * 4),
						 val, !(mask32 & val),
						 100, timeout_us);
		if (ret) {
			drm_err(&ptdev->base, "timeout waiting on %s:%llx power transition",
				blk_name, mask);
			return ret;
		}
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
	u32 val, i;
	int ret;

	for (i = 0; i < 2; i++) {
		u32 mask32 = mask >> (i * 32);

		if (!mask32)
			continue;

		ret = readl_relaxed_poll_timeout(ptdev->iomem + pwrtrans_reg + (i * 4),
						 val, !(mask32 & val),
						 100, timeout_us);
		if (ret) {
			drm_err(&ptdev->base, "timeout waiting on %s:%llx power transition",
				blk_name, mask);
			return ret;
		}
	}

	if (mask & GENMASK(31, 0))
		gpu_write(ptdev, pwron_reg, mask);

	if (mask >> 32)
		gpu_write(ptdev, pwron_reg + 4, mask >> 32);

	for (i = 0; i < 2; i++) {
		u32 mask32 = mask >> (i * 32);

		if (!mask32)
			continue;

		ret = readl_relaxed_poll_timeout(ptdev->iomem + rdy_reg + (i * 4),
						 val, (mask32 & val) == mask32,
						 100, timeout_us);
		if (ret) {
			drm_err(&ptdev->base, "timeout waiting on %s:%llx readiness",
				blk_name, mask);
			return ret;
		}
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
	/*
	 * It may be preferable to simply power down the L2, but for now just
	 * soft-reset which will leave the L2 powered down.
	 */
	panthor_gpu_soft_reset(ptdev);
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

/**
 * panthor_gpu_read_64bit_counter() - Read a 64-bit counter at a given offset.
 * @ptdev: Device.
 * @reg: The offset of the register to read.
 *
 * Return: The counter value.
 */
static u64
panthor_gpu_read_64bit_counter(struct panthor_device *ptdev, u32 reg)
{
	u32 hi, lo;

	do {
		hi = gpu_read(ptdev, reg + 0x4);
		lo = gpu_read(ptdev, reg);
	} while (hi != gpu_read(ptdev, reg + 0x4));

	return ((u64)hi << 32) | lo;
}

/**
 * panthor_gpu_read_timestamp() - Read the timestamp register.
 * @ptdev: Device.
 *
 * Return: The GPU timestamp value.
 */
u64 panthor_gpu_read_timestamp(struct panthor_device *ptdev)
{
	return panthor_gpu_read_64bit_counter(ptdev, GPU_TIMESTAMP_LO);
}

/**
 * panthor_gpu_read_timestamp_offset() - Read the timestamp offset register.
 * @ptdev: Device.
 *
 * Return: The GPU timestamp offset value.
 */
u64 panthor_gpu_read_timestamp_offset(struct panthor_device *ptdev)
{
	u32 hi, lo;

	hi = gpu_read(ptdev, GPU_TIMESTAMP_OFFSET_HI);
	lo = gpu_read(ptdev, GPU_TIMESTAMP_OFFSET_LO);

	return ((u64)hi << 32) | lo;
}

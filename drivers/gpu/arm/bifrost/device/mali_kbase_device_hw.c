// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2016, 2018-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>

#if !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
void kbase_reg_write(struct kbase_device *kbdev, u32 offset, u32 value)
{
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);

	writel(value, kbdev->reg + offset);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				value, 1);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "w: reg %08x val %08x", offset, value);
}

KBASE_EXPORT_TEST_API(kbase_reg_write);

u32 kbase_reg_read(struct kbase_device *kbdev, u32 offset)
{
	u32 val;

	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);

	val = readl(kbdev->reg + offset);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				val, 0);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "r: reg %08x val %08x", offset, val);

	return val;
}

KBASE_EXPORT_TEST_API(kbase_reg_read);

bool kbase_is_gpu_removed(struct kbase_device *kbdev)
{
	u32 val;

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));

	return val == 0;
}
#endif /* !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */

void kbase_gpu_start_cache_clean_nolock(struct kbase_device *kbdev)
{
	u32 irq_mask;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->cache_clean_in_progress) {
		/* If this is called while another clean is in progress, we
		 * can't rely on the current one to flush any new changes in
		 * the cache. Instead, trigger another cache clean immediately
		 * after this one finishes.
		 */
		kbdev->cache_clean_queued = true;
		return;
	}

	/* Enable interrupt */
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK));
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask | CLEAN_CACHES_COMPLETED);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_CLEAN_INV_CACHES);

	kbdev->cache_clean_in_progress = true;
}

void kbase_gpu_start_cache_clean(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_gpu_start_cache_clean_nolock(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_gpu_cache_clean_wait_complete(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->cache_clean_queued = false;
	kbdev->cache_clean_in_progress = false;
	wake_up(&kbdev->cache_clean_wait);
}

void kbase_clean_caches_done(struct kbase_device *kbdev)
{
	u32 irq_mask;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->cache_clean_queued) {
		kbdev->cache_clean_queued = false;

		KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, 0);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
				GPU_COMMAND_CLEAN_INV_CACHES);
	} else {
		/* Disable interrupt */
		irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK));
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask & ~CLEAN_CACHES_COMPLETED);

		kbase_gpu_cache_clean_wait_complete(kbdev);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

static inline bool get_cache_clean_flag(struct kbase_device *kbdev)
{
	bool cache_clean_in_progress;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	cache_clean_in_progress = kbdev->cache_clean_in_progress;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return cache_clean_in_progress;
}

void kbase_gpu_wait_cache_clean(struct kbase_device *kbdev)
{
	while (get_cache_clean_flag(kbdev)) {
		wait_event_interruptible(kbdev->cache_clean_wait,
				!kbdev->cache_clean_in_progress);
	}
}

int kbase_gpu_wait_cache_clean_timeout(struct kbase_device *kbdev,
				unsigned int wait_timeout_ms)
{
	long remaining = msecs_to_jiffies(wait_timeout_ms);

	while (remaining && get_cache_clean_flag(kbdev)) {
		remaining = wait_event_timeout(kbdev->cache_clean_wait,
					!kbdev->cache_clean_in_progress,
					remaining);
	}

	return (remaining ? 0 : -ETIMEDOUT);
}

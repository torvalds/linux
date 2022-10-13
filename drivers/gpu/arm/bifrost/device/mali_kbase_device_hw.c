// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2016, 2018-2022 ARM Limited. All rights reserved.
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
bool kbase_is_gpu_removed(struct kbase_device *kbdev)
{
	u32 val;

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));

	return val == 0;
}
#endif /* !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */

static int busy_wait_on_irq(struct kbase_device *kbdev, u32 irq_bit)
{
	char *irq_flag_name;
	/* Previously MMU-AS command was used for L2 cache flush on page-table update.
	 * And we're using the same max-loops count for GPU command, because amount of
	 * L2 cache flush overhead are same between them.
	 */
	unsigned int max_loops = KBASE_AS_INACTIVE_MAX_LOOPS;

	/* Wait for the GPU cache clean operation to complete */
	while (--max_loops &&
	       !(kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT)) & irq_bit)) {
		;
	}

	/* reset gpu if time-out occurred */
	if (max_loops == 0) {
		switch (irq_bit) {
		case CLEAN_CACHES_COMPLETED:
			irq_flag_name = "CLEAN_CACHES_COMPLETED";
			break;
		case FLUSH_PA_RANGE_COMPLETED:
			irq_flag_name = "FLUSH_PA_RANGE_COMPLETED";
			break;
		default:
			irq_flag_name = "UNKNOWN";
			break;
		}

		dev_err(kbdev->dev,
			"Stuck waiting on %s bit, might be caused by slow/unstable GPU clock or possible faulty FPGA connector\n",
			irq_flag_name);

		if (kbase_prepare_to_reset_gpu_locked(kbdev, RESET_FLAGS_NONE))
			kbase_reset_gpu_locked(kbdev);
		return -EBUSY;
	}

	/* Clear the interrupt bit. */
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, irq_bit);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), irq_bit);

	return 0;
}

#if MALI_USE_CSF
#define U64_LO_MASK ((1ULL << 32) - 1)
#define U64_HI_MASK (~U64_LO_MASK)

int kbase_gpu_cache_flush_pa_range_and_busy_wait(struct kbase_device *kbdev, phys_addr_t phys,
						 size_t nr_bytes, u32 flush_op)
{
	u64 start_pa, end_pa;
	int ret = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* 1. Clear the interrupt FLUSH_PA_RANGE_COMPLETED bit. */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), FLUSH_PA_RANGE_COMPLETED);

	/* 2. Issue GPU_CONTROL.COMMAND.FLUSH_PA_RANGE operation. */
	start_pa = phys;
	end_pa = start_pa + nr_bytes - 1;

	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND_ARG0_LO), start_pa & U64_LO_MASK);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND_ARG0_HI),
			(start_pa & U64_HI_MASK) >> 32);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND_ARG1_LO), end_pa & U64_LO_MASK);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND_ARG1_HI), (end_pa & U64_HI_MASK) >> 32);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), flush_op);

	/* 3. Busy-wait irq status to be enabled. */
	ret = busy_wait_on_irq(kbdev, (u32)FLUSH_PA_RANGE_COMPLETED);

	return ret;
}
#endif /* MALI_USE_CSF */

int kbase_gpu_cache_flush_and_busy_wait(struct kbase_device *kbdev,
					u32 flush_op)
{
	int need_to_wake_up = 0;
	int ret = 0;

	/* hwaccess_lock must be held to avoid any sync issue with
	 * kbase_gpu_start_cache_clean() / kbase_clean_caches_done()
	 */
	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* 1. Check if kbdev->cache_clean_in_progress is set.
	 *    If it is set, it means there are threads waiting for
	 *    CLEAN_CACHES_COMPLETED irq to be raised and that the
	 *    corresponding irq mask bit is set.
	 *    We'll clear the irq mask bit and busy-wait for the cache
	 *    clean operation to complete before submitting the cache
	 *    clean command required after the GPU page table update.
	 *    Pended flush commands will be merged to requested command.
	 */
	if (kbdev->cache_clean_in_progress) {
		/* disable irq first */
		u32 irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK));
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask & ~CLEAN_CACHES_COMPLETED);

		/* busy wait irq status to be enabled */
		ret = busy_wait_on_irq(kbdev, (u32)CLEAN_CACHES_COMPLETED);
		if (ret)
			return ret;

		/* merge pended command if there's any */
		flush_op = GPU_COMMAND_FLUSH_CACHE_MERGE(
			kbdev->cache_clean_queued, flush_op);

		/* enable wake up notify flag */
		need_to_wake_up = 1;
	} else {
		/* Clear the interrupt CLEAN_CACHES_COMPLETED bit. */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR),
				CLEAN_CACHES_COMPLETED);
	}

	/* 2. Issue GPU_CONTROL.COMMAND.FLUSH_CACHE operation. */
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, flush_op);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), flush_op);

	/* 3. Busy-wait irq status to be enabled. */
	ret = busy_wait_on_irq(kbdev, (u32)CLEAN_CACHES_COMPLETED);
	if (ret)
		return ret;

	/* 4. Wake-up blocked threads when there is any. */
	if (need_to_wake_up)
		kbase_gpu_cache_clean_wait_complete(kbdev);

	return ret;
}

void kbase_gpu_start_cache_clean_nolock(struct kbase_device *kbdev,
					u32 flush_op)
{
	u32 irq_mask;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->cache_clean_in_progress) {
		/* If this is called while another clean is in progress, we
		 * can't rely on the current one to flush any new changes in
		 * the cache. Instead, accumulate all cache clean operations
		 * and trigger that immediately after this one finishes.
		 */
		kbdev->cache_clean_queued = GPU_COMMAND_FLUSH_CACHE_MERGE(
			kbdev->cache_clean_queued, flush_op);
		return;
	}

	/* Enable interrupt */
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK));
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask | CLEAN_CACHES_COMPLETED);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, flush_op);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), flush_op);

	kbdev->cache_clean_in_progress = true;
}

void kbase_gpu_start_cache_clean(struct kbase_device *kbdev, u32 flush_op)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_gpu_start_cache_clean_nolock(kbdev, flush_op);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_gpu_cache_clean_wait_complete(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbdev->cache_clean_queued = 0;
	kbdev->cache_clean_in_progress = false;
	wake_up(&kbdev->cache_clean_wait);
}

void kbase_clean_caches_done(struct kbase_device *kbdev)
{
	u32 irq_mask;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->cache_clean_in_progress) {
		/* Clear the interrupt CLEAN_CACHES_COMPLETED bit if set.
		 * It might have already been done by kbase_gpu_cache_flush_and_busy_wait.
		 */
		KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, CLEAN_CACHES_COMPLETED);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), CLEAN_CACHES_COMPLETED);

		if (kbdev->cache_clean_queued) {
			u32 pended_flush_op = kbdev->cache_clean_queued;

			kbdev->cache_clean_queued = 0;

			KBASE_KTRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, pended_flush_op);
			kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), pended_flush_op);
		} else {
			/* Disable interrupt */
			irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK));
			kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
					irq_mask & ~CLEAN_CACHES_COMPLETED);

			kbase_gpu_cache_clean_wait_complete(kbdev);
		}
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

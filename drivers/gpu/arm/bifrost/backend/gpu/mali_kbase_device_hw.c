/*
 *
 * (C) COPYRIGHT 2014-2016, 2018-2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */


/*
 *
 */
#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <backend/gpu/mali_kbase_mmu_hw_direct.h>
#include <mali_kbase_reset_gpu.h>

#if !defined(CONFIG_MALI_BIFROST_NO_MALI)


#ifdef CONFIG_DEBUG_FS

int kbase_io_history_resize(struct kbase_io_history *h, u16 new_size)
{
	struct kbase_io_access *old_buf;
	struct kbase_io_access *new_buf;
	unsigned long flags;

	if (!new_size)
		goto out_err; /* The new size must not be 0 */

	new_buf = vmalloc(new_size * sizeof(*h->buf));
	if (!new_buf)
		goto out_err;

	spin_lock_irqsave(&h->lock, flags);

	old_buf = h->buf;

	/* Note: we won't bother with copying the old data over. The dumping
	 * logic wouldn't work properly as it relies on 'count' both as a
	 * counter and as an index to the buffer which would have changed with
	 * the new array. This is a corner case that we don't need to support.
	 */
	h->count = 0;
	h->size = new_size;
	h->buf = new_buf;

	spin_unlock_irqrestore(&h->lock, flags);

	vfree(old_buf);

	return 0;

out_err:
	return -1;
}


int kbase_io_history_init(struct kbase_io_history *h, u16 n)
{
	h->enabled = false;
	spin_lock_init(&h->lock);
	h->count = 0;
	h->size = 0;
	h->buf = NULL;
	if (kbase_io_history_resize(h, n))
		return -1;

	return 0;
}


void kbase_io_history_term(struct kbase_io_history *h)
{
	vfree(h->buf);
	h->buf = NULL;
}


/* kbase_io_history_add - add new entry to the register access history
 *
 * @h: Pointer to the history data structure
 * @addr: Register address
 * @value: The value that is either read from or written to the register
 * @write: 1 if it's a register write, 0 if it's a read
 */
static void kbase_io_history_add(struct kbase_io_history *h,
		void __iomem const *addr, u32 value, u8 write)
{
	struct kbase_io_access *io;
	unsigned long flags;

	spin_lock_irqsave(&h->lock, flags);

	io = &h->buf[h->count % h->size];
	io->addr = (uintptr_t)addr | write;
	io->value = value;
	++h->count;
	/* If count overflows, move the index by the buffer size so the entire
	 * buffer will still be dumped later */
	if (unlikely(!h->count))
		h->count = h->size;

	spin_unlock_irqrestore(&h->lock, flags);
}


void kbase_io_history_dump(struct kbase_device *kbdev)
{
	struct kbase_io_history *const h = &kbdev->io_history;
	u16 i;
	size_t iters;
	unsigned long flags;

	if (!unlikely(h->enabled))
		return;

	spin_lock_irqsave(&h->lock, flags);

	dev_err(kbdev->dev, "Register IO History:");
	iters = (h->size > h->count) ? h->count : h->size;
	dev_err(kbdev->dev, "Last %zu register accesses of %zu total:\n", iters,
			h->count);
	for (i = 0; i < iters; ++i) {
		struct kbase_io_access *io =
			&h->buf[(h->count - iters + i) % h->size];
		char const access = (io->addr & 1) ? 'w' : 'r';

		dev_err(kbdev->dev, "%6i: %c: reg 0x%p val %08x\n", i, access,
				(void *)(io->addr & ~0x1), io->value);
	}

	spin_unlock_irqrestore(&h->lock, flags);
}


#endif /* CONFIG_DEBUG_FS */


void kbase_reg_write(struct kbase_device *kbdev, u32 offset, u32 value)
{
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);

	writel(value, kbdev->reg + offset);

#ifdef CONFIG_DEBUG_FS
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

#ifdef CONFIG_DEBUG_FS
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				val, 0);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "r: reg %08x val %08x", offset, val);

	return val;
}

KBASE_EXPORT_TEST_API(kbase_reg_read);
#endif /* !defined(CONFIG_MALI_BIFROST_NO_MALI) */

/**
 * kbase_report_gpu_fault - Report a GPU fault.
 * @kbdev:    Kbase device pointer
 * @multiple: Zero if only GPU_FAULT was raised, non-zero if MULTIPLE_GPU_FAULTS
 *            was also set
 *
 * This function is called from the interrupt handler when a GPU fault occurs.
 * It reports the details of the fault using dev_warn().
 */
static void kbase_report_gpu_fault(struct kbase_device *kbdev, int multiple)
{
	u32 gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
	u32 status = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(GPU_FAULTSTATUS));
	u64 address = (u64) kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_HI)) << 32;

	address |= kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_LO));

	if ((gpu_id & GPU_ID2_PRODUCT_MODEL) != GPU_ID2_PRODUCT_TULX) {
		dev_warn(kbdev->dev, "GPU Fault 0x%08x (%s) at 0x%016llx",
			status,
			kbase_exception_name(kbdev, status & 0xFF),
			address);
		if (multiple)
			dev_warn(kbdev->dev, "There were multiple GPU faults - some have not been reported\n");
	}
}

static bool kbase_gpu_fault_interrupt(struct kbase_device *kbdev, int multiple)
{
	kbase_report_gpu_fault(kbdev, multiple);
	return false;
}

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

	KBASE_TRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, NULL, 0u, 0);
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

static void kbase_clean_caches_done(struct kbase_device *kbdev)
{
	u32 irq_mask;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->cache_clean_queued) {
		kbdev->cache_clean_queued = false;

		KBASE_TRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, NULL, 0u, 0);
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

void kbase_gpu_wait_cache_clean(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	while (kbdev->cache_clean_in_progress) {
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		wait_event_interruptible(kbdev->cache_clean_wait,
				!kbdev->cache_clean_in_progress);
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	bool clear_gpu_fault = false;

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, NULL, 0u, val);
	if (val & GPU_FAULT)
		clear_gpu_fault = kbase_gpu_fault_interrupt(kbdev,
					val & MULTIPLE_GPU_FAULTS);

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	if (val & PRFCNT_SAMPLE_COMPLETED)
		kbase_instr_hwcnt_sample_done(kbdev);

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, NULL, 0u, val);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val);

	/* kbase_pm_check_transitions (called by kbase_pm_power_changed) must
	 * be called after the IRQ has been cleared. This is because it might
	 * trigger further power transitions and we don't want to miss the
	 * interrupt raised to notify us that these further transitions have
	 * finished. The same applies to kbase_clean_caches_done() - if another
	 * clean was queued, it might trigger another clean, which might
	 * generate another interrupt which shouldn't be missed.
	 */

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	if (val & POWER_CHANGED_ALL) {
		kbase_pm_power_changed(kbdev);
	} else if (val & CLEAN_CACHES_COMPLETED) {
		/* When 'platform_power_down_only' is enabled, the L2 cache is
		 * not powered down, but flushed before the GPU power down
		 * (which is done by the platform code). So the L2 state machine
		 * requests a cache flush. And when that flush completes, the L2
		 * state machine needs to be re-invoked to proceed with the GPU
		 * power down.
		 * If cache line evict messages can be lost when shader cores
		 * power down then we need to flush the L2 cache before powering
		 * down cores. When the flush completes, the shaders' state
		 * machine needs to be re-invoked to proceed with powering down
		 * cores.
		 */
		if (platform_power_down_only ||
				kbdev->pm.backend.l2_always_on ||
				kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TTRX_921))
			kbase_pm_power_changed(kbdev);
	}


	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, NULL, 0u, val);
}

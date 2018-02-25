/*
 *
 * (C) COPYRIGHT 2014-2016 ARM Limited. All rights reserved.
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


void kbase_reg_write(struct kbase_device *kbdev, u16 offset, u32 value,
						struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kctx == NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);

	writel(value, kbdev->reg + offset);

#ifdef CONFIG_DEBUG_FS
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				value, 1);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "w: reg %04x val %08x", offset, value);

	if (kctx && kctx->jctx.tb)
		kbase_device_trace_register_access(kctx, REG_WRITE, offset,
									value);
}

KBASE_EXPORT_TEST_API(kbase_reg_write);

u32 kbase_reg_read(struct kbase_device *kbdev, u16 offset,
						struct kbase_context *kctx)
{
	u32 val;
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kctx == NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);

	val = readl(kbdev->reg + offset);

#ifdef CONFIG_DEBUG_FS
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				val, 0);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "r: reg %04x val %08x", offset, val);

	if (kctx && kctx->jctx.tb)
		kbase_device_trace_register_access(kctx, REG_READ, offset, val);
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
	u32 status;
	u64 address;

	status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS), NULL);
	address = (u64) kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_HI), NULL) << 32;
	address |= kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_LO), NULL);

	dev_warn(kbdev->dev, "GPU Fault 0x%08x (%s) at 0x%016llx",
			status & 0xFF,
			kbase_exception_name(kbdev, status),
			address);
	if (multiple)
		dev_warn(kbdev->dev, "There were multiple GPU faults - some have not been reported\n");
}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, NULL, 0u, val);
	if (val & GPU_FAULT)
		kbase_report_gpu_fault(kbdev, val & MULTIPLE_GPU_FAULTS);

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	if (val & PRFCNT_SAMPLE_COMPLETED)
		kbase_instr_hwcnt_sample_done(kbdev);

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, NULL, 0u, val);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val, NULL);

	/* kbase_pm_check_transitions must be called after the IRQ has been
	 * cleared. This is because it might trigger further power transitions
	 * and we don't want to miss the interrupt raised to notify us that
	 * these further transitions have finished.
	 */
	if (val & POWER_CHANGED_ALL)
		kbase_pm_power_changed(kbdev);

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, NULL, 0u, val);
}

/*
 *
 * (C) COPYRIGHT 2014-2019 ARM Limited. All rights reserved.
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

#include <linux/bitops.h>
#include <mali_kbase.h>
#include <mali_kbase_mem.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <tl/mali_kbase_tracepoints.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <mali_kbase_as_fault_debugfs.h>

/**
 * lock_region() - Generate lockaddr to lock memory region in MMU
 * @pfn:       Starting page frame number of the region to lock
 * @num_pages: Number of pages to lock. It must be greater than 0.
 * @lockaddr:  Address and size of memory region to lock
 *
 * The lockaddr value is a combination of the starting address and
 * the size of the region that encompasses all the memory pages to lock.
 *
 * The size is expressed as a logarithm: it is represented in a way
 * that is compatible with the HW specification and it also determines
 * how many of the lowest bits of the address are cleared.
 *
 * Return: 0 if success, or an error code on failure.
 */
static int lock_region(u64 pfn, u32 num_pages, u64 *lockaddr)
{
	const u64 lockaddr_base = pfn << PAGE_SHIFT;
	u64 lockaddr_size_log2, region_frame_number_start,
		region_frame_number_end;

	if (num_pages == 0)
		return -EINVAL;

	/* The size is expressed as a logarithm and should take into account
	 * the possibility that some pages might spill into the next region.
	 */
	lockaddr_size_log2 = fls(num_pages) + PAGE_SHIFT - 1;

	/* Round up if the number of pages is not a power of 2. */
	if (num_pages != ((u32)1 << (lockaddr_size_log2 - PAGE_SHIFT)))
		lockaddr_size_log2 += 1;

	/* Round up if some memory pages spill into the next region. */
	region_frame_number_start = pfn >> (lockaddr_size_log2 - PAGE_SHIFT);
	region_frame_number_end =
	    (pfn + num_pages - 1) >> (lockaddr_size_log2 - PAGE_SHIFT);

	if (region_frame_number_start < region_frame_number_end)
		lockaddr_size_log2 += 1;

	/* Represent the size according to the HW specification. */
	lockaddr_size_log2 = MAX(lockaddr_size_log2,
		KBASE_LOCK_REGION_MIN_SIZE_LOG2);

	if (lockaddr_size_log2 > KBASE_LOCK_REGION_MAX_SIZE_LOG2)
		return -EINVAL;

	/* The lowest bits are cleared and then set to size - 1 to represent
	 * the size in a way that is compatible with the HW specification.
	 */
	*lockaddr = lockaddr_base & ~((1ull << lockaddr_size_log2) - 1);
	*lockaddr |= lockaddr_size_log2 - 1;

	return 0;
}

static int wait_ready(struct kbase_device *kbdev,
		unsigned int as_nr)
{
	unsigned int max_loops = KBASE_AS_INACTIVE_MAX_LOOPS;
	u32 val = kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS));

	/* Wait for the MMU status to indicate there is no active command, in
	 * case one is pending. Do not log remaining register accesses.
	 */
	while (--max_loops && (val & AS_STATUS_AS_ACTIVE))
		val = kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS));

	if (max_loops == 0) {
		dev_err(kbdev->dev, "AS_ACTIVE bit stuck, might be caused by slow/unstable GPU clock or possible faulty FPGA connector\n");
		return -1;
	}

	/* If waiting in loop was performed, log last read value. */
	if (KBASE_AS_INACTIVE_MAX_LOOPS - 1 > max_loops)
		kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS));

	return 0;
}

static int write_cmd(struct kbase_device *kbdev, int as_nr, u32 cmd)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(kbdev, as_nr);
	if (status == 0)
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_COMMAND), cmd);

	return status;
}

void kbase_mmu_hw_configure(struct kbase_device *kbdev, struct kbase_as *as)
{
	struct kbase_mmu_setup *current_setup = &as->current_setup;
	u64 transcfg = 0;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU)) {
		transcfg = current_setup->transcfg;

		/* Set flag AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK
		 * Clear PTW_MEMATTR bits
		 */
		transcfg &= ~AS_TRANSCFG_PTW_MEMATTR_MASK;
		/* Enable correct PTW_MEMATTR bits */
		transcfg |= AS_TRANSCFG_PTW_MEMATTR_WRITE_BACK;
		/* Ensure page-tables reads use read-allocate cache-policy in
		 * the L2
		 */
		transcfg |= AS_TRANSCFG_R_ALLOCATE;

		if (kbdev->system_coherency == COHERENCY_ACE) {
			/* Set flag AS_TRANSCFG_PTW_SH_OS (outer shareable)
			 * Clear PTW_SH bits
			 */
			transcfg = (transcfg & ~AS_TRANSCFG_PTW_SH_MASK);
			/* Enable correct PTW_SH bits */
			transcfg = (transcfg | AS_TRANSCFG_PTW_SH_OS);
		}

		kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSCFG_LO),
				transcfg);
		kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSCFG_HI),
				(transcfg >> 32) & 0xFFFFFFFFUL);
	} else {
		if (kbdev->system_coherency == COHERENCY_ACE)
			current_setup->transtab |= AS_TRANSTAB_LPAE_SHARE_OUTER;
	}

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_LO),
			current_setup->transtab & 0xFFFFFFFFUL);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_HI),
			(current_setup->transtab >> 32) & 0xFFFFFFFFUL);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_LO),
			current_setup->memattr & 0xFFFFFFFFUL);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_HI),
			(current_setup->memattr >> 32) & 0xFFFFFFFFUL);

	KBASE_TLSTREAM_TL_ATTRIB_AS_CONFIG(kbdev, as,
			current_setup->transtab,
			current_setup->memattr,
			transcfg);

	write_cmd(kbdev, as->number, AS_COMMAND_UPDATE);
}

int kbase_mmu_hw_do_operation(struct kbase_device *kbdev, struct kbase_as *as,
		u64 vpfn, u32 nr, u32 op,
		unsigned int handling_irq)
{
	int ret;

	lockdep_assert_held(&kbdev->mmu_hw_mutex);

	if (op == AS_COMMAND_UNLOCK) {
		/* Unlock doesn't require a lock first */
		ret = write_cmd(kbdev, as->number, AS_COMMAND_UNLOCK);
	} else {
		u64 lock_addr;

		ret = lock_region(vpfn, nr, &lock_addr);

		if (!ret) {
			/* Lock the region that needs to be updated */
			kbase_reg_write(kbdev,
				MMU_AS_REG(as->number, AS_LOCKADDR_LO),
				lock_addr & 0xFFFFFFFFUL);
			kbase_reg_write(kbdev,
				MMU_AS_REG(as->number, AS_LOCKADDR_HI),
				(lock_addr >> 32) & 0xFFFFFFFFUL);
			write_cmd(kbdev, as->number, AS_COMMAND_LOCK);

			/* Run the MMU operation */
			write_cmd(kbdev, as->number, op);

			/* Wait for the flush to complete */
			ret = wait_ready(kbdev, as->number);
		}
	}

	return ret;
}

void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 pf_bf_mask;

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	/* Clear the page (and bus fault IRQ as well in case one occurred) */
	pf_bf_mask = MMU_PAGE_FAULT(as->number);
	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		pf_bf_mask |= MMU_BUS_ERROR(as->number);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), pf_bf_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 irq_mask;

	/* Enable the page fault IRQ
	 * (and bus fault IRQ as well in case one occurred)
	 */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	irq_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK)) |
			MMU_PAGE_FAULT(as->number);

	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		irq_mask |= MMU_BUS_ERROR(as->number);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), irq_mask);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

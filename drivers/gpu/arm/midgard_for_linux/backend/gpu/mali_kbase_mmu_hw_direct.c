/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/bitops.h>

#include <mali_kbase.h>
#include <mali_kbase_mem.h>
#include <mali_kbase_mmu_hw.h>
#if defined(CONFIG_MALI_MIPE_ENABLED)
#include <mali_kbase_tlstream.h>
#endif
#include <backend/gpu/mali_kbase_mmu_hw_direct.h>
#include <backend/gpu/mali_kbase_device_internal.h>

static inline u64 lock_region(struct kbase_device *kbdev, u64 pfn,
		u32 num_pages)
{
	u64 region;

	/* can't lock a zero sized range */
	KBASE_DEBUG_ASSERT(num_pages);

	region = pfn << PAGE_SHIFT;
	/*
	 * fls returns (given the ASSERT above):
	 * 1 .. 32
	 *
	 * 10 + fls(num_pages)
	 * results in the range (11 .. 42)
	 */

	/* gracefully handle num_pages being zero */
	if (0 == num_pages) {
		region |= 11;
	} else {
		u8 region_width;

		region_width = 10 + fls(num_pages);
		if (num_pages != (1ul << (region_width - 11))) {
			/* not pow2, so must go up to the next pow2 */
			region_width += 1;
		}
		KBASE_DEBUG_ASSERT(region_width <= KBASE_LOCK_REGION_MAX_SIZE);
		KBASE_DEBUG_ASSERT(region_width >= KBASE_LOCK_REGION_MIN_SIZE);
		region |= region_width;
	}

	return region;
}

static int wait_ready(struct kbase_device *kbdev,
		unsigned int as_nr, struct kbase_context *kctx)
{
	unsigned int max_loops = KBASE_AS_INACTIVE_MAX_LOOPS;
	u32 val = kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS), kctx);

	/* Wait for the MMU status to indicate there is no active command, in
	 * case one is pending. Do not log remaining register accesses. */
	while (--max_loops && (val & AS_STATUS_AS_ACTIVE))
		val = kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS), NULL);

	if (max_loops == 0) {
		dev_err(kbdev->dev, "AS_ACTIVE bit stuck\n");
		return -1;
	}

	/* If waiting in loop was performed, log last read value. */
	if (KBASE_AS_INACTIVE_MAX_LOOPS - 1 > max_loops)
		kbase_reg_read(kbdev, MMU_AS_REG(as_nr, AS_STATUS), kctx);

	return 0;
}

static int write_cmd(struct kbase_device *kbdev, int as_nr, u32 cmd,
		struct kbase_context *kctx)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(kbdev, as_nr, kctx);
	if (status == 0)
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_COMMAND), cmd,
									kctx);

	return status;
}

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat)
{
	const int num_as = 16;
	const int busfault_shift = MMU_PAGE_FAULT_FLAGS;
	const int pf_shift = 0;
	const unsigned long as_bit_mask = (1UL << num_as) - 1;
	unsigned long flags;
	u32 new_mask;
	u32 tmp;

	/* bus faults */
	u32 bf_bits = (irq_stat >> busfault_shift) & as_bit_mask;
	/* page faults (note: Ignore ASes with both pf and bf) */
	u32 pf_bits = ((irq_stat >> pf_shift) & as_bit_mask) & ~bf_bits;

	KBASE_DEBUG_ASSERT(NULL != kbdev);

	/* remember current mask */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	new_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), NULL);
	/* mask interrupts for now */
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), 0, NULL);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	while (bf_bits | pf_bits) {
		struct kbase_as *as;
		int as_no;
		struct kbase_context *kctx;

		/*
		 * the while logic ensures we have a bit set, no need to check
		 * for not-found here
		 */
		as_no = ffs(bf_bits | pf_bits) - 1;
		as = &kbdev->as[as_no];

		/*
		 * Refcount the kctx ASAP - it shouldn't disappear anyway, since
		 * Bus/Page faults _should_ only occur whilst jobs are running,
		 * and a job causing the Bus/Page fault shouldn't complete until
		 * the MMU is updated
		 */
		kctx = kbasep_js_runpool_lookup_ctx(kbdev, as_no);

		/* find faulting address */
		as->fault_addr = kbase_reg_read(kbdev,
						MMU_AS_REG(as_no,
							AS_FAULTADDRESS_HI),
						kctx);
		as->fault_addr <<= 32;
		as->fault_addr |= kbase_reg_read(kbdev,
						MMU_AS_REG(as_no,
							AS_FAULTADDRESS_LO),
						kctx);

		/* record the fault status */
		as->fault_status = kbase_reg_read(kbdev,
						  MMU_AS_REG(as_no,
							AS_FAULTSTATUS),
						  kctx);

		/* find the fault type */
		as->fault_type = (bf_bits & (1 << as_no)) ?
				KBASE_MMU_FAULT_TYPE_BUS :
				KBASE_MMU_FAULT_TYPE_PAGE;


		if (kbase_as_has_bus_fault(as)) {
			/* Mark bus fault as handled.
			 * Note that a bus fault is processed first in case
			 * where both a bus fault and page fault occur.
			 */
			bf_bits &= ~(1UL << as_no);

			/* remove the queued BF (and PF) from the mask */
			new_mask &= ~(MMU_BUS_ERROR(as_no) |
					MMU_PAGE_FAULT(as_no));
		} else {
			/* Mark page fault as handled */
			pf_bits &= ~(1UL << as_no);

			/* remove the queued PF from the mask */
			new_mask &= ~MMU_PAGE_FAULT(as_no);
		}

		/* Process the interrupt for this address space */
		spin_lock_irqsave(&kbdev->js_data.runpool_irq.lock, flags);
		kbase_mmu_interrupt_process(kbdev, kctx, as);
		spin_unlock_irqrestore(&kbdev->js_data.runpool_irq.lock,
				flags);
	}

	/* reenable interrupts */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	tmp = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), NULL);
	new_mask |= tmp;
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), new_mask, NULL);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

void kbase_mmu_hw_configure(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx)
{
	struct kbase_mmu_setup *current_setup = &as->current_setup;
#ifdef CONFIG_MALI_MIPE_ENABLED
	u32 transcfg = 0;
#endif


	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_LO),
			current_setup->transtab & 0xFFFFFFFFUL, kctx);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_HI),
			(current_setup->transtab >> 32) & 0xFFFFFFFFUL, kctx);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_LO),
			current_setup->memattr & 0xFFFFFFFFUL, kctx);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_HI),
			(current_setup->memattr >> 32) & 0xFFFFFFFFUL, kctx);

#if defined(CONFIG_MALI_MIPE_ENABLED)
	kbase_tlstream_tl_attrib_as_config(as,
			current_setup->transtab,
			current_setup->memattr,
			transcfg);
#endif

	write_cmd(kbdev, as->number, AS_COMMAND_UPDATE, kctx);
}

int kbase_mmu_hw_do_operation(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx, u64 vpfn, u32 nr, u32 op,
		unsigned int handling_irq)
{
	int ret;

	if (op == AS_COMMAND_UNLOCK) {
		/* Unlock doesn't require a lock first */
		ret = write_cmd(kbdev, as->number, AS_COMMAND_UNLOCK, kctx);
	} else {
		u64 lock_addr = lock_region(kbdev, vpfn, nr);

		/* Lock the region that needs to be updated */
		kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_LOCKADDR_LO),
				lock_addr & 0xFFFFFFFFUL, kctx);
		kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_LOCKADDR_HI),
				(lock_addr >> 32) & 0xFFFFFFFFUL, kctx);
		write_cmd(kbdev, as->number, AS_COMMAND_LOCK, kctx);

		/* Run the MMU operation */
		write_cmd(kbdev, as->number, op, kctx);

		/* Wait for the flush to complete */
		ret = wait_ready(kbdev, as->number, kctx);

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9630)) {
			/* Issue an UNLOCK command to ensure that valid page
			   tables are re-read by the GPU after an update.
			   Note that, the FLUSH command should perform all the
			   actions necessary, however the bus logs show that if
			   multiple page faults occur within an 8 page region
			   the MMU does not always re-read the updated page
			   table entries for later faults or is only partially
			   read, it subsequently raises the page fault IRQ for
			   the same addresses, the unlock ensures that the MMU
			   cache is flushed, so updates can be re-read.  As the
			   region is now unlocked we need to issue 2 UNLOCK
			   commands in order to flush the MMU/uTLB,
			   see PRLAM-8812.
			 */
			write_cmd(kbdev, as->number, AS_COMMAND_UNLOCK, kctx);
			write_cmd(kbdev, as->number, AS_COMMAND_UNLOCK, kctx);
		}
	}

	return ret;
}

void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx, enum kbase_mmu_fault_type type)
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

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), pf_bf_mask, kctx);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx, enum kbase_mmu_fault_type type)
{
	unsigned long flags;
	u32 irq_mask;

	/* Enable the page fault IRQ (and bus fault IRQ as well in case one
	 * occurred) */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);

	/*
	 * A reset is in-flight and we're flushing the IRQ + bottom half
	 * so don't update anything as it could race with the reset code.
	 */
	if (kbdev->irq_reset_flush)
		goto unlock;

	irq_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), kctx) |
			MMU_PAGE_FAULT(as->number);

	if (type == KBASE_MMU_FAULT_TYPE_BUS ||
			type == KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED)
		irq_mask |= MMU_BUS_ERROR(as->number);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), irq_mask, kctx);

unlock:
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}

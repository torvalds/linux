/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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
#include <mali_kbase_mmu_hw_direct.h>

#if KBASE_MMU_HW_BACKEND

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

	/* Wait for the MMU status to indicate there is no active command. */
	while (--max_loops && kbase_reg_read(kbdev,
			MMU_AS_REG(as_nr, AS_STATUS),
			kctx) & AS_STATUS_AS_ACTIVE) {
		;
	}

	if (max_loops == 0) {
		dev_err(kbdev->dev, "AS_ACTIVE bit stuck\n");
		return -1;
	}

	return 0;
}

static int write_cmd(struct kbase_device *kbdev, int as_nr, u32 cmd,
		struct kbase_context *kctx)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(kbdev, as_nr, kctx);
	if (status == 0)
		kbase_reg_write(kbdev, MMU_AS_REG(as_nr, AS_COMMAND), cmd, kctx);

	return status;
}

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat)
{
	const int num_as = 16;
	const int busfault_shift = MMU_REGS_PAGE_FAULT_FLAGS;
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
						MMU_AS_REG(as_no, AS_FAULTADDRESS_HI),
						kctx);
		as->fault_addr <<= 32;
		as->fault_addr |= kbase_reg_read(kbdev,
						MMU_AS_REG(as_no, AS_FAULTADDRESS_LO),
						kctx);

		/* record the fault status */
		as->fault_status = kbase_reg_read(kbdev,
						  MMU_AS_REG(as_no, AS_FAULTSTATUS),
						  kctx);

		/* find the fault type */
		as->fault_type = (bf_bits & (1 << as_no)) ?
				  KBASE_MMU_FAULT_TYPE_BUS : KBASE_MMU_FAULT_TYPE_PAGE;


		if (kbase_as_has_bus_fault(as)) {
			/*
			 * Clear the internal JM mask first before clearing the
			 * internal MMU mask
			 *
			 * Note:
			 * Always clear the page fault just in case there was
			 * one at the same time as the bus error (bus errors are
			 * always processed in preference to pagefaults should
			 * both happen at the same time).
			 */
			kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR),
					(1UL << MMU_REGS_BUS_ERROR_FLAG(as_no)) |
					(1UL << MMU_REGS_PAGE_FAULT_FLAG(as_no)), kctx);

			/* mark as handled (note: bf_bits is already shifted) */
			bf_bits &= ~(1UL << (as_no));

			/* remove the queued BFs (and PFs) from the mask */
			new_mask &= ~((1UL << MMU_REGS_BUS_ERROR_FLAG(as_no)) |
				      (1UL << MMU_REGS_PAGE_FAULT_FLAG(as_no)));
		} else {
			/*
			 * Clear the internal JM mask first before clearing the
			 * internal MMU mask
			 */
			kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR),
					1UL << MMU_REGS_PAGE_FAULT_FLAG(as_no),
					kctx);

			/* mark as handled */
			pf_bits &= ~(1UL << as_no);

			/* remove the queued PFs from the mask */
			new_mask &= ~(1UL << MMU_REGS_PAGE_FAULT_FLAG(as_no));
		}

		/* Process the interrupt for this address space */
		kbase_mmu_interrupt_process(kbdev, kctx, as);
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

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_LO),
			current_setup->transtab & 0xFFFFFFFFUL, kctx);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_TRANSTAB_HI),
			(current_setup->transtab >> 32) & 0xFFFFFFFFUL, kctx);

	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_LO),
			current_setup->memattr & 0xFFFFFFFFUL, kctx);
	kbase_reg_write(kbdev, MMU_AS_REG(as->number, AS_MEMATTR_HI),
			(current_setup->memattr >> 32) & 0xFFFFFFFFUL, kctx);
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

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_T76X_3285) &&
				handling_irq) {
			kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR),
					(1UL << as->number), NULL);
			write_cmd(kbdev, as->number, AS_COMMAND_LOCK, kctx);
		}

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
	u32 mask;

	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), kctx);

	mask |= (1UL << MMU_REGS_PAGE_FAULT_FLAG(as->number));
	if (type == KBASE_MMU_FAULT_TYPE_BUS)
		mask |= (1UL << MMU_REGS_BUS_ERROR_FLAG(as->number));

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), mask, kctx);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);
}
#endif

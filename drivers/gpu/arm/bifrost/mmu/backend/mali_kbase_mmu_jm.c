/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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

/**
 * Base kernel MMU management specific for Job Manager GPU.
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <mali_kbase_hwaccess_jm.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <mali_kbase_as_fault_debugfs.h>
#include "../mali_kbase_mmu_internal.h"
#include "mali_kbase_device_internal.h"

void kbase_mmu_get_as_setup(struct kbase_mmu_table *mmut,
		struct kbase_mmu_setup * const setup)
{
	/* Set up the required caching policies at the correct indices
	 * in the memattr register.
	 */
	setup->memattr =
		(AS_MEMATTR_IMPL_DEF_CACHE_POLICY <<
			(AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY * 8)) |
		(AS_MEMATTR_FORCE_TO_CACHE_ALL    <<
			(AS_MEMATTR_INDEX_FORCE_TO_CACHE_ALL * 8)) |
		(AS_MEMATTR_WRITE_ALLOC           <<
			(AS_MEMATTR_INDEX_WRITE_ALLOC * 8)) |
		(AS_MEMATTR_AARCH64_OUTER_IMPL_DEF   <<
			(AS_MEMATTR_INDEX_OUTER_IMPL_DEF * 8)) |
		(AS_MEMATTR_AARCH64_OUTER_WA         <<
			(AS_MEMATTR_INDEX_OUTER_WA * 8)) |
		(AS_MEMATTR_AARCH64_NON_CACHEABLE    <<
			(AS_MEMATTR_INDEX_NON_CACHEABLE * 8));

	setup->transtab = (u64)mmut->pgd & AS_TRANSTAB_BASE_MASK;
	setup->transcfg = AS_TRANSCFG_ADRMODE_AARCH64_4K;
}

void kbase_gpu_report_bus_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, struct kbase_fault *fault)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	u32 const status = fault->status;
	u32 const exception_type = (status & 0xFF);
	u32 const exception_data = (status >> 8) & 0xFFFFFF;
	int const as_no = as->number;
	unsigned long flags;

	/* terminal fault, print info about the fault */
	dev_err(kbdev->dev,
		"GPU bus fault in AS%d at VA 0x%016llX\n"
		"raw fault status: 0x%X\n"
		"exception type 0x%X: %s\n"
		"exception data 0x%X\n"
		"pid: %d\n",
		as_no, fault->addr,
		status,
		exception_type, kbase_gpu_exception_name(exception_type),
		exception_data,
		kctx->pid);

	/* switch to UNMAPPED mode, will abort all jobs and stop any hw counter
	 * dumping AS transaction begin
	 */
	mutex_lock(&kbdev->mmu_hw_mutex);

	/* Set the MMU into unmapped mode */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);
	/* AS transaction end */

	kbase_mmu_hw_clear_fault(kbdev, as,
				 KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
	kbase_mmu_hw_enable_fault(kbdev, as,
				 KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
}

/**
 * The caller must ensure it's retained the ctx to prevent it from being
 * scheduled out whilst it's being worked on.
 */
void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, const char *reason_str,
		struct kbase_fault *fault)
{
	unsigned long flags;
	u32 exception_type;
	u32 access_type;
	u32 source_id;
	int as_no;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;

	as_no = as->number;
	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;

	/* Make sure the context was active */
	if (WARN_ON(atomic_read(&kctx->refcount) <= 0))
		return;

	/* decode the fault status */
	exception_type = fault->status & 0xFF;
	access_type = (fault->status >> 8) & 0x3;
	source_id = (fault->status >> 16);

	/* terminal fault, print info about the fault */
	dev_err(kbdev->dev,
		"Unhandled Page fault in AS%d at VA 0x%016llX\n"
		"Reason: %s\n"
		"raw fault status: 0x%X\n"
		"exception type 0x%X: %s\n"
		"access type 0x%X: %s\n"
		"source id 0x%X\n"
		"pid: %d\n",
		as_no, fault->addr,
		reason_str,
		fault->status,
		exception_type, kbase_gpu_exception_name(exception_type),
		access_type, kbase_gpu_access_type_name(fault->status),
		source_id,
		kctx->pid);

	/* hardware counters dump fault handling */
	if ((kbdev->hwcnt.kctx) && (kbdev->hwcnt.kctx->as_nr == as_no) &&
			(kbdev->hwcnt.backend.state ==
						KBASE_INSTR_STATE_DUMPING)) {
		if ((fault->addr >= kbdev->hwcnt.addr) &&
				(fault->addr < (kbdev->hwcnt.addr +
					kbdev->hwcnt.addr_bytes)))
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_FAULT;
	}

	/* Stop the kctx from submitting more jobs and cause it to be scheduled
	 * out/rescheduled - this will occur on releasing the context's refcount
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbasep_js_clear_submit_allowed(js_devdata, kctx);

	/* Kill any running jobs from the context. Submit is disallowed, so no
	 * more jobs from this context can appear in the job slots from this
	 * point on
	 */
	kbase_backend_jm_kill_running_jobs_from_kctx(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);

	/* switch to UNMAPPED mode, will abort all jobs and stop
	 * any hw counter dumping
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);

	/* AS transaction end */
	/* Clear down the fault */
	kbase_mmu_hw_clear_fault(kbdev, as,
			KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
	kbase_mmu_hw_enable_fault(kbdev, as,
			KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
}

void kbase_mmu_interrupt_process(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_as *as,
		struct kbase_fault *fault)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	dev_dbg(kbdev->dev,
		"Entering %s kctx %p, as %p\n",
		__func__, (void *)kctx, (void *)as);

	if (!kctx) {
		dev_warn(kbdev->dev, "%s in AS%d at 0x%016llx with no context present! Spurious IRQ or SW Design Error?\n",
				kbase_as_has_bus_fault(as, fault) ?
						"Bus error" : "Page fault",
				as->number, fault->addr);

		/* Since no ctx was found, the MMU must be disabled. */
		WARN_ON(as->current_setup.transtab);

		if (kbase_as_has_bus_fault(as, fault)) {
			kbase_mmu_hw_clear_fault(kbdev, as,
					KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as,
					KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED);
		} else if (kbase_as_has_page_fault(as, fault)) {
			kbase_mmu_hw_clear_fault(kbdev, as,
					KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as,
					KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
		}

		return;
	}

	if (kbase_as_has_bus_fault(as, fault)) {
		struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

		/*
		 * hw counters dumping in progress, signal the
		 * other thread that it failed
		 */
		if ((kbdev->hwcnt.kctx == kctx) &&
		    (kbdev->hwcnt.backend.state ==
					KBASE_INSTR_STATE_DUMPING))
			kbdev->hwcnt.backend.state =
						KBASE_INSTR_STATE_FAULT;

		/*
		 * Stop the kctx from submitting more jobs and cause it
		 * to be scheduled out/rescheduled when all references
		 * to it are released
		 */
		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
			dev_warn(kbdev->dev,
					"Bus error in AS%d at VA=0x%016llx, IPA=0x%016llx\n",
					as->number, fault->addr,
					fault->extra_addr);
		else
			dev_warn(kbdev->dev, "Bus error in AS%d at 0x%016llx\n",
					as->number, fault->addr);

		/*
		 * We need to switch to UNMAPPED mode - but we do this in a
		 * worker so that we can sleep
		 */
		WARN_ON(!queue_work(as->pf_wq, &as->work_busfault));
		atomic_inc(&kbdev->faults_pending);
	} else {
		WARN_ON(!queue_work(as->pf_wq, &as->work_pagefault));
		atomic_inc(&kbdev->faults_pending);
	}

	dev_dbg(kbdev->dev,
		"Leaving %s kctx %p, as %p\n",
		__func__, (void *)kctx, (void *)as);
}

static void validate_protected_page_fault(struct kbase_device *kbdev)
{
	/* GPUs which support (native) protected mode shall not report page
	 * fault addresses unless it has protected debug mode and protected
	 * debug mode is turned on
	 */
	u32 protected_debug_mode = 0;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_DEBUG_MODE)) {
		protected_debug_mode = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(GPU_STATUS)) & GPU_DBGEN;
	}

	if (!protected_debug_mode) {
		/* fault_addr should never be reported in protected mode.
		 * However, we just continue by printing an error message
		 */
		dev_err(kbdev->dev, "Fault address reported in protected mode\n");
	}
}

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat)
{
	const int num_as = 16;
	const int busfault_shift = MMU_PAGE_FAULT_FLAGS;
	const int pf_shift = 0;
	const unsigned long as_bit_mask = (1UL << num_as) - 1;
	unsigned long flags;
	u32 new_mask;
	u32 tmp, bf_bits, pf_bits;
	bool gpu_lost = false;

	dev_dbg(kbdev->dev, "Entering %s irq_stat %u\n",
		__func__, irq_stat);
	/* bus faults */
	bf_bits = (irq_stat >> busfault_shift) & as_bit_mask;
	/* page faults (note: Ignore ASes with both pf and bf) */
	pf_bits = ((irq_stat >> pf_shift) & as_bit_mask) & ~bf_bits;

	if (WARN_ON(kbdev == NULL))
		return;

	/* remember current mask */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	new_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK));
	/* mask interrupts for now */
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), 0);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	while (bf_bits | pf_bits) {
		struct kbase_as *as;
		int as_no;
		struct kbase_context *kctx;
		struct kbase_fault *fault;

		/*
		 * the while logic ensures we have a bit set, no need to check
		 * for not-found here
		 */
		as_no = ffs(bf_bits | pf_bits) - 1;
		as = &kbdev->as[as_no];

		/* find the fault type */
		if (bf_bits & (1 << as_no))
			fault = &as->bf_data;
		else
			fault = &as->pf_data;

		/*
		 * Refcount the kctx ASAP - it shouldn't disappear anyway, since
		 * Bus/Page faults _should_ only occur whilst jobs are running,
		 * and a job causing the Bus/Page fault shouldn't complete until
		 * the MMU is updated
		 */
		kctx = kbase_ctx_sched_as_to_ctx_refcount(kbdev, as_no);

		/* find faulting address */
		fault->addr = kbase_reg_read(kbdev, MMU_AS_REG(as_no,
				AS_FAULTADDRESS_HI));
		fault->addr <<= 32;
		fault->addr |= kbase_reg_read(kbdev, MMU_AS_REG(as_no,
				AS_FAULTADDRESS_LO));
		/* Mark the fault protected or not */
		fault->protected_mode = kbdev->protected_mode;

		if (kbdev->protected_mode && fault->addr) {
			/* check if address reporting is allowed */
			validate_protected_page_fault(kbdev);
		}

		/* report the fault to debugfs */
		kbase_as_fault_debugfs_new(kbdev, as_no);

		/* record the fault status */
		fault->status = kbase_reg_read(kbdev, MMU_AS_REG(as_no,
				AS_FAULTSTATUS));

		if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU)) {
			fault->extra_addr = kbase_reg_read(kbdev,
					MMU_AS_REG(as_no, AS_FAULTEXTRA_HI));
			fault->extra_addr <<= 32;
			fault->extra_addr |= kbase_reg_read(kbdev,
					MMU_AS_REG(as_no, AS_FAULTEXTRA_LO));
		}

		/* check if we still have GPU */
		gpu_lost = kbase_is_gpu_lost(kbdev);
		if (gpu_lost) {
			if (kctx)
				kbasep_js_runpool_release_ctx(kbdev, kctx);
			return;
		}

		if (kbase_as_has_bus_fault(as, fault)) {
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
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_mmu_interrupt_process(kbdev, kctx, as, fault);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}

	/* reenable interrupts */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	tmp = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK));
	new_mask |= tmp;
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), new_mask);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	dev_dbg(kbdev->dev, "Leaving %s irq_stat %u\n",
		__func__, irq_stat);
}

int kbase_mmu_switch_to_ir(struct kbase_context *const kctx,
	struct kbase_va_region *const reg)
{
	dev_dbg(kctx->kbdev->dev,
		"Switching to incremental rendering for region %p\n",
		(void *)reg);
	return kbase_job_slot_softstop_start_rp(kctx, reg);
}

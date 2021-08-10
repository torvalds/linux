// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

/**
 * DOC: Base kernel MMU management specific for CSF GPU.
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_reset_gpu.h>
#include <mali_kbase_as_fault_debugfs.h>
#include <mmu/mali_kbase_mmu_internal.h>

void kbase_mmu_get_as_setup(struct kbase_mmu_table *mmut,
		struct kbase_mmu_setup * const setup)
{
	/* Set up the required caching policies at the correct indices
	 * in the memattr register.
	 */
	setup->memattr =
		(AS_MEMATTR_IMPL_DEF_CACHE_POLICY <<
			(AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY * 8)) |
		(AS_MEMATTR_FORCE_TO_CACHE_ALL <<
			(AS_MEMATTR_INDEX_FORCE_TO_CACHE_ALL * 8)) |
		(AS_MEMATTR_WRITE_ALLOC <<
			(AS_MEMATTR_INDEX_WRITE_ALLOC * 8)) |
		(AS_MEMATTR_AARCH64_OUTER_IMPL_DEF   <<
			(AS_MEMATTR_INDEX_OUTER_IMPL_DEF * 8)) |
		(AS_MEMATTR_AARCH64_OUTER_WA <<
			(AS_MEMATTR_INDEX_OUTER_WA * 8)) |
		(AS_MEMATTR_AARCH64_NON_CACHEABLE <<
			(AS_MEMATTR_INDEX_NON_CACHEABLE * 8)) |
		(AS_MEMATTR_AARCH64_SHARED <<
			(AS_MEMATTR_INDEX_SHARED * 8));

	setup->transtab = (u64)mmut->pgd & AS_TRANSTAB_BASE_MASK;
	setup->transcfg = AS_TRANSCFG_ADRMODE_AARCH64_4K;
}

/**
 * submit_work_pagefault() - Submit a work for MMU page fault.
 *
 * @kbdev:    Kbase device pointer
 * @as_nr:    Faulty address space
 * @fault:    Data relating to the fault
 *
 * This function submits a work for reporting the details of MMU fault.
 */
static void submit_work_pagefault(struct kbase_device *kbdev, u32 as_nr,
		struct kbase_fault *fault)
{
	unsigned long flags;
	struct kbase_as *const as = &kbdev->as[as_nr];
	struct kbase_context *kctx;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kctx = kbase_ctx_sched_as_to_ctx_nolock(kbdev, as_nr);

	if (kctx) {
		kbase_ctx_sched_retain_ctx_refcount(kctx);

		as->pf_data = (struct kbase_fault) {
			.status = fault->status,
			.addr = fault->addr,
		};

		/*
		 * A page fault work item could already be pending for the
		 * context's address space, when the page fault occurs for
		 * MCU's address space.
		 */
		if (!queue_work(as->pf_wq, &as->work_pagefault))
			kbase_ctx_sched_release_ctx(kctx);
		else {
			dev_dbg(kbdev->dev,
				"Page fault is already pending for as %u\n",
				as_nr);
			atomic_inc(&kbdev->faults_pending);
		}
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_mmu_report_mcu_as_fault_and_reset(struct kbase_device *kbdev,
		struct kbase_fault *fault)
{
	/* decode the fault status */
	u32 exception_type = fault->status & 0xFF;
	u32 access_type = (fault->status >> 8) & 0x3;
	u32 source_id = (fault->status >> 16);
	int as_no;

	/* terminal fault, print info about the fault */
	dev_err(kbdev->dev,
		"Unexpected Page fault in firmware address space at VA 0x%016llX\n"
		"raw fault status: 0x%X\n"
		"exception type 0x%X: %s\n"
		"access type 0x%X: %s\n"
		"source id 0x%X\n",
		fault->addr,
		fault->status,
		exception_type, kbase_gpu_exception_name(exception_type),
		access_type, kbase_gpu_access_type_name(fault->status),
		source_id);

	/* Report MMU fault for all address spaces (except MCU_AS_NR) */
	for (as_no = 1; as_no < kbdev->nr_hw_address_spaces; as_no++)
		submit_work_pagefault(kbdev, as_no, fault);

	/* GPU reset is required to recover */
	if (kbase_prepare_to_reset_gpu(kbdev,
				       RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
		kbase_reset_gpu(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_mmu_report_mcu_as_fault_and_reset);

void kbase_gpu_report_bus_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, struct kbase_fault *fault)
{
	struct kbase_device *kbdev = kctx->kbdev;
	u32 const status = fault->status;
	int exception_type = (status & GPU_FAULTSTATUS_EXCEPTION_TYPE_MASK) >>
				GPU_FAULTSTATUS_EXCEPTION_TYPE_SHIFT;
	int access_type = (status & GPU_FAULTSTATUS_ACCESS_TYPE_MASK) >>
				GPU_FAULTSTATUS_ACCESS_TYPE_SHIFT;
	int source_id = (status & GPU_FAULTSTATUS_SOURCE_ID_MASK) >>
				GPU_FAULTSTATUS_SOURCE_ID_SHIFT;
	const char *addr_valid = (status & GPU_FAULTSTATUS_ADDR_VALID_FLAG) ?
					"true" : "false";
	int as_no = as->number;
	unsigned long flags;

	/* terminal fault, print info about the fault */
	dev_err(kbdev->dev,
		"GPU bus fault in AS%d at VA 0x%016llX\n"
		"VA_VALID: %s\n"
		"raw fault status: 0x%X\n"
		"exception type 0x%X: %s\n"
		"access type 0x%X: %s\n"
		"source id 0x%X\n"
		"pid: %d\n",
		as_no, fault->addr,
		addr_valid,
		status,
		exception_type, kbase_gpu_exception_name(exception_type),
		access_type, kbase_gpu_access_type_name(access_type),
		source_id,
		kctx->pid);

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	kbase_ctx_flag_set(kctx, KCTX_AS_DISABLED_ON_FAULT);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	/* Switching to UNMAPPED mode above would have enabled the firmware to
	 * recover from the fault (if the memory access was made by firmware)
	 * and it can then respond to CSG termination requests to be sent now.
	 * All GPU command queue groups associated with the context would be
	 * affected as they use the same GPU address space.
	 */
	kbase_csf_ctx_handle_fault(kctx, fault);

	/* Now clear the GPU fault */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
			GPU_COMMAND_CLEAR_FAULT);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

/*
 * The caller must ensure it's retained the ctx to prevent it from being
 * scheduled out whilst it's being worked on.
 */
void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, const char *reason_str,
		struct kbase_fault *fault)
{
	unsigned long flags;
	unsigned int exception_type;
	unsigned int access_type;
	unsigned int source_id;
	int as_no;
	struct kbase_device *kbdev;
	const u32 status = fault->status;

	as_no = as->number;
	kbdev = kctx->kbdev;

	/* Make sure the context was active */
	if (WARN_ON(atomic_read(&kctx->refcount) <= 0))
		return;

	/* decode the fault status */
	exception_type = AS_FAULTSTATUS_EXCEPTION_TYPE_GET(status);
	access_type = AS_FAULTSTATUS_ACCESS_TYPE_GET(status);
	source_id = AS_FAULTSTATUS_SOURCE_ID_GET(status);

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
		status,
		exception_type, kbase_gpu_exception_name(exception_type),
		access_type, kbase_gpu_access_type_name(status),
		source_id,
		kctx->pid);

	/* AS transaction begin */
	mutex_lock(&kbdev->mmu_hw_mutex);

	/* switch to UNMAPPED mode,
	 * will abort all jobs and stop any hw counter dumping
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_disable(kctx);
	kbase_ctx_flag_set(kctx, KCTX_AS_DISABLED_ON_FAULT);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&kbdev->mmu_hw_mutex);
	/* AS transaction end */

	/* Switching to UNMAPPED mode above would have enabled the firmware to
	 * recover from the fault (if the memory access was made by firmware)
	 * and it can then respond to CSG termination requests to be sent now.
	 * All GPU command queue groups associated with the context would be
	 * affected as they use the same GPU address space.
	 */
	kbase_csf_ctx_handle_fault(kctx, fault);

	/* Clear down the fault */
	kbase_mmu_hw_clear_fault(kbdev, as,
			KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
	kbase_mmu_hw_enable_fault(kbdev, as,
			KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
}

/**
 * kbase_mmu_interrupt_process() - Process a bus or page fault.
 * @kbdev:	The kbase_device the fault happened on
 * @kctx:	The kbase_context for the faulting address space if one was
 *		found.
 * @as:		The address space that has the fault
 * @fault:	Data relating to the fault
 *
 * This function will process a fault on a specific address space
 */
static void kbase_mmu_interrupt_process(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_as *as,
		struct kbase_fault *fault)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!kctx) {
		dev_warn(kbdev->dev, "%s in AS%d at 0x%016llx with no context present! Spurious IRQ or SW Design Error?\n",
				kbase_as_has_bus_fault(as, fault) ?
						"Bus error" : "Page fault",
				as->number, fault->addr);

		/* Since no ctx was found, the MMU must be disabled. */
		WARN_ON(as->current_setup.transtab);

		if (kbase_as_has_bus_fault(as, fault))
			kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
				GPU_COMMAND_CLEAR_FAULT);
		else if (kbase_as_has_page_fault(as, fault)) {
			kbase_mmu_hw_clear_fault(kbdev, as,
					KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
			kbase_mmu_hw_enable_fault(kbdev, as,
					KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED);
		}

		return;
	}

	if (kbase_as_has_bus_fault(as, fault)) {
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
}

int kbase_mmu_bus_fault_interrupt(struct kbase_device *kbdev,
		u32 status, u32 as_nr)
{
	struct kbase_context *kctx;
	unsigned long flags;
	struct kbase_as *as;
	struct kbase_fault *fault;

	if (WARN_ON(as_nr == MCU_AS_NR))
		return -EINVAL;

	if (WARN_ON(as_nr >= BASE_MAX_NR_AS))
		return -EINVAL;

	as = &kbdev->as[as_nr];
	fault = &as->bf_data;
	fault->status = status;
	fault->addr = (u64) kbase_reg_read(kbdev,
		GPU_CONTROL_REG(GPU_FAULTADDRESS_HI)) << 32;
	fault->addr |= kbase_reg_read(kbdev,
		GPU_CONTROL_REG(GPU_FAULTADDRESS_LO));
	fault->protected_mode = false;

	/* report the fault to debugfs */
	kbase_as_fault_debugfs_new(kbdev, as_nr);

	kctx = kbase_ctx_sched_as_to_ctx_refcount(kbdev, as_nr);

	/* Process the bus fault interrupt for this address space */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_mmu_interrupt_process(kbdev, kctx, as, fault);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return 0;
}

void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat)
{
	const int num_as = 16;
	const int pf_shift = 0;
	const unsigned long as_bit_mask = (1UL << num_as) - 1;
	unsigned long flags;
	u32 new_mask;
	u32 tmp;
	u32 pf_bits = ((irq_stat >> pf_shift) & as_bit_mask);

	/* remember current mask */
	spin_lock_irqsave(&kbdev->mmu_mask_change, flags);
	new_mask = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK));
	/* mask interrupts for now */
	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_MASK), 0);
	spin_unlock_irqrestore(&kbdev->mmu_mask_change, flags);

	while (pf_bits) {
		struct kbase_context *kctx;
		int as_no = ffs(pf_bits) - 1;
		struct kbase_as *as = &kbdev->as[as_no];
		struct kbase_fault *fault = &as->pf_data;

		/* find faulting address */
		fault->addr = kbase_reg_read(kbdev, MMU_AS_REG(as_no,
				AS_FAULTADDRESS_HI));
		fault->addr <<= 32;
		fault->addr |= kbase_reg_read(kbdev, MMU_AS_REG(as_no,
				AS_FAULTADDRESS_LO));

		/* Mark the fault protected or not */
		fault->protected_mode = false;

		/* report the fault to debugfs */
		kbase_as_fault_debugfs_new(kbdev, as_no);

		/* record the fault status */
		fault->status = kbase_reg_read(kbdev, MMU_AS_REG(as_no,
				AS_FAULTSTATUS));

		fault->extra_addr = kbase_reg_read(kbdev,
					MMU_AS_REG(as_no, AS_FAULTEXTRA_HI));
		fault->extra_addr <<= 32;
		fault->extra_addr |= kbase_reg_read(kbdev,
					MMU_AS_REG(as_no, AS_FAULTEXTRA_LO));

		/* Mark page fault as handled */
		pf_bits &= ~(1UL << as_no);

		/* remove the queued PF from the mask */
		new_mask &= ~MMU_PAGE_FAULT(as_no);

		if (as_no == MCU_AS_NR) {
			kbase_mmu_report_mcu_as_fault_and_reset(kbdev, fault);
			/* Pointless to handle remaining faults */
			break;
		}

		/*
		 * Refcount the kctx - it shouldn't disappear anyway, since
		 * Page faults _should_ only occur whilst GPU commands are
		 * executing, and a command causing the Page fault shouldn't
		 * complete until the MMU is updated.
		 * Reference is released at the end of bottom half of page
		 * fault handling.
		 */
		kctx = kbase_ctx_sched_as_to_ctx_refcount(kbdev, as_no);

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
}

int kbase_mmu_switch_to_ir(struct kbase_context *const kctx,
	struct kbase_va_region *const reg)
{
	/* Can't soft-stop the provoking job */
	return -EPERM;
}

/**
 * kbase_mmu_gpu_fault_worker() - Process a GPU fault for the device.
 *
 * @data:  work_struct passed by queue_work()
 *
 * Report a GPU fatal error for all GPU command queue groups that are
 * using the address space and terminate them.
 */
static void kbase_mmu_gpu_fault_worker(struct work_struct *data)
{
	struct kbase_as *const faulting_as = container_of(data, struct kbase_as,
			work_gpufault);
	const u32 as_nr = faulting_as->number;
	struct kbase_device *const kbdev = container_of(faulting_as, struct
			kbase_device, as[as_nr]);
	struct kbase_fault *fault;
	struct kbase_context *kctx;
	u32 status;
	u64 address;
	u32 as_valid;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	fault = &faulting_as->gf_data;
	status = fault->status;
	as_valid = status & GPU_FAULTSTATUS_JASID_VALID_FLAG;
	address = fault->addr;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_warn(kbdev->dev,
		 "GPU Fault 0x%08x (%s) in AS%u at 0x%016llx\n"
		 "ASID_VALID: %s,  ADDRESS_VALID: %s\n",
		 status,
		 kbase_gpu_exception_name(
			GPU_FAULTSTATUS_EXCEPTION_TYPE_GET(status)),
		 as_nr, address,
		 as_valid ? "true" : "false",
		 status & GPU_FAULTSTATUS_ADDR_VALID_FLAG ? "true" : "false");

	kctx = kbase_ctx_sched_as_to_ctx(kbdev, as_nr);
	kbase_csf_ctx_handle_fault(kctx, fault);
	kbase_ctx_sched_release_ctx_lock(kctx);

	atomic_dec(&kbdev->faults_pending);

	/* A work for GPU fault is complete.
	 * Till reaching here, no further GPU fault will be reported.
	 * Now clear the GPU fault to allow next GPU fault interrupt report.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
			GPU_COMMAND_CLEAR_FAULT);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

/**
 * submit_work_gpufault() - Submit a work for GPU fault.
 *
 * @kbdev:    Kbase device pointer
 * @status:   GPU fault status
 * @as_nr:    Faulty address space
 * @address:  GPU fault address
 *
 * This function submits a work for reporting the details of GPU fault.
 */
static void submit_work_gpufault(struct kbase_device *kbdev, u32 status,
		u32 as_nr, u64 address)
{
	unsigned long flags;
	struct kbase_as *const as = &kbdev->as[as_nr];
	struct kbase_context *kctx;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kctx = kbase_ctx_sched_as_to_ctx_nolock(kbdev, as_nr);

	if (kctx) {
		kbase_ctx_sched_retain_ctx_refcount(kctx);

		as->gf_data = (struct kbase_fault) {
			.status = status,
			.addr = address,
		};

		if (WARN_ON(!queue_work(as->pf_wq, &as->work_gpufault)))
			kbase_ctx_sched_release_ctx(kctx);
		else
			atomic_inc(&kbdev->faults_pending);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

void kbase_mmu_gpu_fault_interrupt(struct kbase_device *kbdev, u32 status,
		u32 as_nr, u64 address, bool as_valid)
{
	if (!as_valid || (as_nr == MCU_AS_NR)) {
		int as;

		/* Report GPU fault for all contexts (except MCU_AS_NR) in case either
		 * the address space is invalid or it's MCU address space.
		 */
		for (as = 1; as < kbdev->nr_hw_address_spaces; as++)
			submit_work_gpufault(kbdev, status, as, address);
	} else
		submit_work_gpufault(kbdev, status, as_nr, address);
}
KBASE_EXPORT_TEST_API(kbase_mmu_gpu_fault_interrupt);

int kbase_mmu_as_init(struct kbase_device *kbdev, int i)
{
	kbdev->as[i].number = i;
	kbdev->as[i].bf_data.addr = 0ULL;
	kbdev->as[i].pf_data.addr = 0ULL;
	kbdev->as[i].gf_data.addr = 0ULL;

	kbdev->as[i].pf_wq = alloc_workqueue("mali_mmu%d", 0, 1, i);
	if (!kbdev->as[i].pf_wq)
		return -ENOMEM;

	INIT_WORK(&kbdev->as[i].work_pagefault, kbase_mmu_page_fault_worker);
	INIT_WORK(&kbdev->as[i].work_busfault, kbase_mmu_bus_fault_worker);
	INIT_WORK(&kbdev->as[i].work_gpufault, kbase_mmu_gpu_fault_worker);

	return 0;
}

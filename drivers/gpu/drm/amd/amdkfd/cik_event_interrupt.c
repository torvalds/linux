/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "kfd_priv.h"
#include "kfd_events.h"
#include "cik_int.h"
#include "amdgpu_amdkfd.h"
#include "kfd_smi_events.h"

static bool cik_event_interrupt_isr(struct kfd_node *dev,
					const uint32_t *ih_ring_entry,
					uint32_t *patched_ihre,
					bool *patched_flag)
{
	const struct cik_ih_ring_entry *ihre =
			(const struct cik_ih_ring_entry *)ih_ring_entry;
	const struct kfd2kgd_calls *f2g = dev->kfd2kgd;
	unsigned int vmid;
	uint16_t pasid;
	bool ret;

	/* This workaround is due to HW/FW limitation on Hawaii that
	 * VMID and PASID are not written into ih_ring_entry
	 */
	if ((ihre->source_id == CIK_INTSRC_GFX_PAGE_INV_FAULT ||
		ihre->source_id == CIK_INTSRC_GFX_MEM_PROT_FAULT) &&
		dev->adev->asic_type == CHIP_HAWAII) {
		struct cik_ih_ring_entry *tmp_ihre =
			(struct cik_ih_ring_entry *)patched_ihre;

		*patched_flag = true;
		*tmp_ihre = *ihre;

		vmid = f2g->read_vmid_from_vmfault_reg(dev->adev);
		ret = f2g->get_atc_vmid_pasid_mapping_info(dev->adev, vmid, &pasid);

		tmp_ihre->ring_id &= 0x000000ff;
		tmp_ihre->ring_id |= vmid << 8;
		tmp_ihre->ring_id |= pasid << 16;

		return ret && (pasid != 0) &&
			vmid >= dev->vm_info.first_vmid_kfd &&
			vmid <= dev->vm_info.last_vmid_kfd;
	}

	/* Only handle interrupts from KFD VMIDs */
	vmid  = (ihre->ring_id & 0x0000ff00) >> 8;
	if (vmid < dev->vm_info.first_vmid_kfd ||
	    vmid > dev->vm_info.last_vmid_kfd)
		return false;

	/* If there is no valid PASID, it's likely a firmware bug */
	pasid = (ihre->ring_id & 0xffff0000) >> 16;
	if (WARN_ONCE(pasid == 0, "FW bug: No PASID in KFD interrupt"))
		return false;

	/* Interrupt types we care about: various signals and faults.
	 * They will be forwarded to a work queue (see below).
	 */
	return ihre->source_id == CIK_INTSRC_CP_END_OF_PIPE ||
		ihre->source_id == CIK_INTSRC_SDMA_TRAP ||
		ihre->source_id == CIK_INTSRC_SQ_INTERRUPT_MSG ||
		ihre->source_id == CIK_INTSRC_CP_BAD_OPCODE ||
		((ihre->source_id == CIK_INTSRC_GFX_PAGE_INV_FAULT ||
		ihre->source_id == CIK_INTSRC_GFX_MEM_PROT_FAULT) &&
		!amdgpu_no_queue_eviction_on_vm_fault);
}

static void cik_event_interrupt_wq(struct kfd_node *dev,
					const uint32_t *ih_ring_entry)
{
	const struct cik_ih_ring_entry *ihre =
			(const struct cik_ih_ring_entry *)ih_ring_entry;
	uint32_t context_id = ihre->data & 0xfffffff;
	u32 pasid = (ihre->ring_id & 0xffff0000) >> 16;

	if (pasid == 0)
		return;

	if (ihre->source_id == CIK_INTSRC_CP_END_OF_PIPE)
		kfd_signal_event_interrupt(pasid, context_id, 28);
	else if (ihre->source_id == CIK_INTSRC_SDMA_TRAP)
		kfd_signal_event_interrupt(pasid, context_id, 28);
	else if (ihre->source_id == CIK_INTSRC_SQ_INTERRUPT_MSG)
		kfd_signal_event_interrupt(pasid, context_id & 0xff, 8);
	else if (ihre->source_id == CIK_INTSRC_CP_BAD_OPCODE)
		kfd_signal_hw_exception_event(pasid);
	else if (ihre->source_id == CIK_INTSRC_GFX_PAGE_INV_FAULT ||
		ihre->source_id == CIK_INTSRC_GFX_MEM_PROT_FAULT) {
		struct kfd_process_device *pdd = NULL;
		struct kfd_vm_fault_info info;
		struct kfd_process *p;

		kfd_smi_event_update_vmfault(dev, pasid);
		p = kfd_lookup_process_by_pasid(pasid, &pdd);
		if (!pdd)
			return;

		kfd_evict_process_device(pdd);

		memset(&info, 0, sizeof(info));
		amdgpu_amdkfd_gpuvm_get_vm_fault_info(dev->adev, &info);
		if (!info.page_addr && !info.status) {
			kfd_unref_process(p);
			return;
		}

		kfd_signal_vm_fault_event(pdd, &info, NULL);
		kfd_unref_process(p);
	}
}

const struct kfd_event_interrupt_class event_interrupt_class_cik = {
	.interrupt_isr = cik_event_interrupt_isr,
	.interrupt_wq = cik_event_interrupt_wq,
};

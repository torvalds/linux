/*
 * Copyright 2016-2018 Advanced Micro Devices, Inc.
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
#include "soc15_int.h"
#include "kfd_device_queue_manager.h"

static bool event_interrupt_isr_v9(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry,
					uint32_t *patched_ihre,
					bool *patched_flag)
{
	uint16_t source_id, client_id, pasid, vmid;
	const uint32_t *data = ih_ring_entry;

	/* Only handle interrupts from KFD VMIDs */
	vmid = SOC15_VMID_FROM_IH_ENTRY(ih_ring_entry);
	if (vmid < dev->vm_info.first_vmid_kfd ||
	    vmid > dev->vm_info.last_vmid_kfd)
		return 0;

	source_id = SOC15_SOURCE_ID_FROM_IH_ENTRY(ih_ring_entry);
	client_id = SOC15_CLIENT_ID_FROM_IH_ENTRY(ih_ring_entry);
	pasid = SOC15_PASID_FROM_IH_ENTRY(ih_ring_entry);

	/* This is a known issue for gfx9. Under non HWS, pasid is not set
	 * in the interrupt payload, so we need to find out the pasid on our
	 * own.
	 */
	if (!pasid && dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		const uint32_t pasid_mask = 0xffff;

		*patched_flag = true;
		memcpy(patched_ihre, ih_ring_entry,
				dev->device_info->ih_ring_entry_size);

		pasid = dev->kfd2kgd->get_atc_vmid_pasid_mapping_pasid(
				dev->kgd, vmid);

		/* Patch the pasid field */
		patched_ihre[3] = cpu_to_le32((le32_to_cpu(patched_ihre[3])
					& ~pasid_mask) | pasid);
	}

	pr_debug("client id 0x%x, source id %d, vmid %d, pasid 0x%x. raw data:\n",
		 client_id, source_id, vmid, pasid);
	pr_debug("%8X, %8X, %8X, %8X, %8X, %8X, %8X, %8X.\n",
		 data[0], data[1], data[2], data[3],
		 data[4], data[5], data[6], data[7]);

	/* If there is no valid PASID, it's likely a bug */
	if (WARN_ONCE(pasid == 0, "Bug: No PASID in KFD interrupt"))
		return 0;

	/* Interrupt types we care about: various signals and faults.
	 * They will be forwarded to a work queue (see below).
	 */
	return source_id == SOC15_INTSRC_CP_END_OF_PIPE ||
		source_id == SOC15_INTSRC_SDMA_TRAP ||
		source_id == SOC15_INTSRC_SQ_INTERRUPT_MSG ||
		source_id == SOC15_INTSRC_CP_BAD_OPCODE ||
		client_id == SOC15_IH_CLIENTID_VMC ||
		client_id == SOC15_IH_CLIENTID_UTCL2;
}

static void event_interrupt_wq_v9(struct kfd_dev *dev,
					const uint32_t *ih_ring_entry)
{
	uint16_t source_id, client_id, pasid, vmid;
	uint32_t context_id;

	source_id = SOC15_SOURCE_ID_FROM_IH_ENTRY(ih_ring_entry);
	client_id = SOC15_CLIENT_ID_FROM_IH_ENTRY(ih_ring_entry);
	pasid = SOC15_PASID_FROM_IH_ENTRY(ih_ring_entry);
	vmid = SOC15_VMID_FROM_IH_ENTRY(ih_ring_entry);
	context_id = SOC15_CONTEXT_ID0_FROM_IH_ENTRY(ih_ring_entry);

	if (source_id == SOC15_INTSRC_CP_END_OF_PIPE)
		kfd_signal_event_interrupt(pasid, context_id, 32);
	else if (source_id == SOC15_INTSRC_SDMA_TRAP)
		kfd_signal_event_interrupt(pasid, context_id & 0xfffffff, 28);
	else if (source_id == SOC15_INTSRC_SQ_INTERRUPT_MSG)
		kfd_signal_event_interrupt(pasid, context_id & 0xffffff, 24);
	else if (source_id == SOC15_INTSRC_CP_BAD_OPCODE)
		kfd_signal_hw_exception_event(pasid);
	else if (client_id == SOC15_IH_CLIENTID_VMC ||
		 client_id == SOC15_IH_CLIENTID_UTCL2) {
		struct kfd_vm_fault_info info = {0};
		uint16_t ring_id = SOC15_RING_ID_FROM_IH_ENTRY(ih_ring_entry);

		info.vmid = vmid;
		info.mc_id = client_id;
		info.page_addr = ih_ring_entry[4] |
			(uint64_t)(ih_ring_entry[5] & 0xf) << 32;
		info.prot_valid = ring_id & 0x08;
		info.prot_read  = ring_id & 0x10;
		info.prot_write = ring_id & 0x20;

		kfd_process_vm_fault(dev->dqm, pasid);
		kfd_signal_vm_fault_event(dev, pasid, &info);
	}
}

const struct kfd_event_interrupt_class event_interrupt_class_v9 = {
	.interrupt_isr = event_interrupt_isr_v9,
	.interrupt_wq = event_interrupt_wq_v9,
};

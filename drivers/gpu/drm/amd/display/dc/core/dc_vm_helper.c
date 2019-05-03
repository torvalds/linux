/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 *
 * Authors: AMD
 *
 */

#include "vm_helper.h"

static void mark_vmid_used(struct vm_helper *vm_helper, unsigned int pos, uint8_t hubp_idx)
{
	struct vmid_usage vmids = vm_helper->hubp_vmid_usage[hubp_idx];

	vmids.vmid_usage[0] = vmids.vmid_usage[1];
	vmids.vmid_usage[1] = 1 << pos;
}

static void add_ptb_to_table(struct vm_helper *vm_helper, unsigned int vmid, uint64_t ptb)
{
	vm_helper->ptb_assigned_to_vmid[vmid] = ptb;
	vm_helper->num_vmids_available--;
}

static void clear_entry_from_vmid_table(struct vm_helper *vm_helper, unsigned int vmid)
{
	vm_helper->ptb_assigned_to_vmid[vmid] = 0;
	vm_helper->num_vmids_available++;
}

static void evict_vmids(struct vm_helper *vm_helper)
{
	int i;
	uint16_t ord = 0;

	for (i = 0; i < vm_helper->num_vmid; i++)
		ord |= vm_helper->hubp_vmid_usage[i].vmid_usage[0] | vm_helper->hubp_vmid_usage[i].vmid_usage[1];

	// At this point any positions with value 0 are unused vmids, evict them
	for (i = 1; i < vm_helper->num_vmid; i++) {
		if (ord & (1u << i))
			clear_entry_from_vmid_table(vm_helper, i);
	}
}

// Return value of -1 indicates vmid table unitialized or ptb dne in the table
static int get_existing_vmid_for_ptb(struct vm_helper *vm_helper, uint64_t ptb)
{
	int i;

	for (i = 0; i < vm_helper->num_vmid; i++) {
		if (vm_helper->ptb_assigned_to_vmid[i] == ptb)
			return i;
	}

	return -1;
}

// Expected to be called only when there's an available vmid
static int get_next_available_vmid(struct vm_helper *vm_helper)
{
	int i;

	for (i = 1; i < vm_helper->num_vmid; i++) {
		if (vm_helper->ptb_assigned_to_vmid[i] == 0)
			return i;
	}

	return -1;
}

uint8_t get_vmid_for_ptb(struct vm_helper *vm_helper, int64_t ptb, uint8_t hubp_idx)
{
	unsigned int vmid = 0;
	int vmid_exists = -1;

	// Physical address gets vmid 0
	if (ptb == 0)
		return 0;

	vmid_exists = get_existing_vmid_for_ptb(vm_helper, ptb);

	if (vmid_exists != -1) {
		mark_vmid_used(vm_helper, vmid_exists, hubp_idx);
		vmid = vmid_exists;
	} else {
		if (vm_helper->num_vmids_available == 0)
			evict_vmids(vm_helper);

		vmid = get_next_available_vmid(vm_helper);
		mark_vmid_used(vm_helper, vmid, hubp_idx);
		add_ptb_to_table(vm_helper, vmid, ptb);
	}

	return vmid;
}

void init_vm_helper(struct vm_helper *vm_helper, unsigned int num_vmid, unsigned int num_hubp)
{
	vm_helper->num_vmid = num_vmid;
	vm_helper->num_hubp = num_hubp;
	vm_helper->num_vmids_available = num_vmid - 1;

	memset(vm_helper->hubp_vmid_usage, 0, sizeof(vm_helper->hubp_vmid_usage[0]) * MAX_HUBP);
	memset(vm_helper->ptb_assigned_to_vmid, 0, sizeof(vm_helper->ptb_assigned_to_vmid[0]) * MAX_VMID);
}

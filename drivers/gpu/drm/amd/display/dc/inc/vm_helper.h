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

#ifndef DC_INC_VM_HELPER_H_
#define DC_INC_VM_HELPER_H_

#include "dc_types.h"

#define MAX_VMID 16
#define MAX_HUBP 6

struct vmid_usage {
	uint16_t vmid_usage[2];
};

struct vm_helper {
	unsigned int num_vmid;
	unsigned int num_hubp;
	unsigned int num_vmids_available;
	uint64_t ptb_assigned_to_vmid[MAX_VMID];
	struct vmid_usage hubp_vmid_usage[MAX_HUBP];
};

uint8_t get_vmid_for_ptb(
		struct vm_helper *vm_helper,
		int64_t ptb,
		uint8_t pipe_idx);

void init_vm_helper(
	struct vm_helper *vm_helper,
	unsigned int num_vmid,
	unsigned int num_hubp);

#endif /* DC_INC_VM_HELPER_H_ */

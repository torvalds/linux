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
#include "dc.h"

void vm_helper_mark_vmid_used(struct vm_helper *vm_helper, unsigned int pos, uint8_t hubp_idx)
{
	struct vmid_usage vmids = vm_helper->hubp_vmid_usage[hubp_idx];

	vmids.vmid_usage[0] = vmids.vmid_usage[1];
	vmids.vmid_usage[1] = 1 << pos;
}

int dc_setup_system_context(struct dc *dc, struct dc_phy_addr_space_config *pa_config)
{
	int num_vmids = 0;

	/* Call HWSS to setup HUBBUB for address config */
	if (dc->hwss.init_sys_ctx) {
		num_vmids = dc->hwss.init_sys_ctx(dc->hwseq, dc, pa_config);

		/* Pre-init system aperture start/end for all HUBP instances (if not gating?)
		 * or cache system aperture if using power gating
		 */
		memcpy(&dc->vm_pa_config, pa_config, sizeof(struct dc_phy_addr_space_config));
		dc->vm_pa_config.valid = true;
	}

	return num_vmids;
}

void dc_setup_vm_context(struct dc *dc, struct dc_virtual_addr_space_config *va_config, int vmid)
{
	dc->hwss.init_vm_ctx(dc->hwseq, dc, va_config, vmid);
}

int dc_get_vmid_use_vector(struct dc *dc)
{
	int i;
	int in_use = 0;

	for (i = 0; i < dc->vm_helper->num_vmid; i++)
		in_use |= dc->vm_helper->hubp_vmid_usage[i].vmid_usage[0]
			| dc->vm_helper->hubp_vmid_usage[i].vmid_usage[1];
	return in_use;
}

void vm_helper_init(struct vm_helper *vm_helper, unsigned int num_vmid)
{
	vm_helper->num_vmid = num_vmid;

	memset(vm_helper->hubp_vmid_usage, 0, sizeof(vm_helper->hubp_vmid_usage[0]) * MAX_HUBP);
}

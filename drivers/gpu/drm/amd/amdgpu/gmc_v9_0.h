/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 */

#ifndef __GMC_V9_0_H__
#define __GMC_V9_0_H__

	/*
	 * The latest engine allocation on gfx9 is:
	 * Engine 2, 3: firmware
	 * Engine 0, 1, 4~16: amdgpu ring,
	 *                    subject to change when ring number changes
	 * Engine 17: Gart flushes
	 */
#define GFXHUB_FREE_VM_INV_ENGS_BITMAP		0x1FFF3
#define MMHUB_FREE_VM_INV_ENGS_BITMAP		0x1FFF3

extern const struct amd_ip_funcs gmc_v9_0_ip_funcs;
extern const struct amdgpu_ip_block_version gmc_v9_0_ip_block;

/* amdgpu_amdkfd*.c */
void gfxhub_v1_0_setup_vm_pt_regs(struct amdgpu_device *adev, uint32_t vmid,
				uint64_t value);
void mmhub_v1_0_setup_vm_pt_regs(struct amdgpu_device *adev, uint32_t vmid,
				uint64_t value);
void mmhub_v9_4_setup_vm_pt_regs(struct amdgpu_device *adev, int hubid,
				uint32_t vmid, uint64_t value);
#endif

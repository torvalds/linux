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
 * Author: Huang Rui
 *
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v10_0.h"

#include "mp/mp_10_0_offset.h"
#include "gc/gc_9_1_offset.h"
#include "sdma0/sdma0_4_1_offset.h"

MODULE_FIRMWARE("amdgpu/raven_asd.bin");
MODULE_FIRMWARE("amdgpu/picasso_asd.bin");
MODULE_FIRMWARE("amdgpu/raven2_asd.bin");
MODULE_FIRMWARE("amdgpu/picasso_ta.bin");
MODULE_FIRMWARE("amdgpu/raven2_ta.bin");
MODULE_FIRMWARE("amdgpu/raven_ta.bin");

static int psp_v10_0_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	char ucode_prefix[30];
	int err = 0;
	DRM_DEBUG("\n");

	amdgpu_ucode_ip_version_decode(adev, MP0_HWIP, ucode_prefix, sizeof(ucode_prefix));

	err = psp_init_asd_microcode(psp, ucode_prefix);
	if (err)
		return err;

	err = psp_init_ta_microcode(psp, ucode_prefix);
	if ((amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 1, 0)) &&
	    (adev->pdev->revision == 0xa1) &&
	    (psp->securedisplay_context.context.bin_desc.fw_version >=
	     0x27000008)) {
		adev->psp.securedisplay_context.context.bin_desc.size_bytes = 0;
	}
	return err;
}

static int psp_v10_0_ring_create(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	/* Write low address of the ring to C2PMSG_69 */
	psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, psp_ring_reg);
	/* Write high address of the ring to C2PMSG_70 */
	psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, psp_ring_reg);
	/* Write size of ring to C2PMSG_71 */
	psp_ring_reg = ring->ring_size;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_71, psp_ring_reg);
	/* Write the ring initialization command to C2PMSG_64 */
	psp_ring_reg = ring_type;
	psp_ring_reg = psp_ring_reg << 16;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, psp_ring_reg);

	/* There might be handshake issue with hardware which needs delay */
	mdelay(20);

	/* Wait for response flag (bit 31) in C2PMSG_64 */
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
			   MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);

	return ret;
}

static int psp_v10_0_ring_stop(struct psp_context *psp,
			       enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Write the ring destroy command to C2PMSG_64 */
	psp_ring_reg = 3 << 16;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, psp_ring_reg);

	/* There might be handshake issue with hardware which needs delay */
	mdelay(20);

	/* Wait for response flag (bit 31) in C2PMSG_64 */
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
			   MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, 0);

	return ret;
}

static int psp_v10_0_ring_destroy(struct psp_context *psp,
				  enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v10_0_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static int psp_v10_0_mode1_reset(struct psp_context *psp)
{
	DRM_INFO("psp mode 1 reset not supported now! \n");
	return -EINVAL;
}

static uint32_t psp_v10_0_ring_get_wptr(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	return RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67);
}

static void psp_v10_0_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67, value);
}

static const struct psp_funcs psp_v10_0_funcs = {
	.init_microcode = psp_v10_0_init_microcode,
	.ring_create = psp_v10_0_ring_create,
	.ring_stop = psp_v10_0_ring_stop,
	.ring_destroy = psp_v10_0_ring_destroy,
	.mode1_reset = psp_v10_0_mode1_reset,
	.ring_get_wptr = psp_v10_0_ring_get_wptr,
	.ring_set_wptr = psp_v10_0_ring_set_wptr,
};

void psp_v10_0_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v10_0_funcs;
}

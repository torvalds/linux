/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v11_0_8.h"

#include "mp/mp_11_0_8_offset.h"

static int psp_v11_0_8_ring_init(struct psp_context *psp,
			      enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring;
	struct amdgpu_device *adev = psp->adev;

	ring = &psp->km_ring;

	ring->ring_type = ring_type;

	/* allocate 4k Page of Local Frame Buffer memory for ring */
	ring->ring_size = 0x1000;
	ret = amdgpu_bo_create_kernel(adev, ring->ring_size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->firmware.rbuf,
				      &ring->ring_mem_mc_addr,
				      (void **)&ring->ring_mem);
	if (ret) {
		ring->ring_size = 0;
		return ret;
	}

	return 0;
}

static int psp_v11_0_8_ring_stop(struct psp_context *psp,
			       enum psp_ring_type ring_type)
{
	int ret = 0;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		/* Write the ring destroy command*/
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
			     GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING);
		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);
		/* Wait for response flag (bit 31) */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
				   0x80000000, 0x80000000, false);
	} else {
		/* Write the ring destroy command*/
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64,
			     GFX_CTRL_CMD_ID_DESTROY_RINGS);
		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);
		/* Wait for response flag (bit 31) */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);
	}

	return ret;
}

static int psp_v11_0_8_ring_create(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		ret = psp_v11_0_8_ring_stop(psp, ring_type);
		if (ret) {
			DRM_ERROR("psp_v11_0_8_ring_stop_sriov failed!\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_102 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_103 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_103, psp_ring_reg);

		/* Write the ring initialization command to C2PMSG_101 */
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
			     GFX_CTRL_CMD_ID_INIT_GPCOM_RING);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
				   0x80000000, 0x8000FFFF, false);

	} else {
		/* Wait for sOS ready for ring creation */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);
		if (ret) {
			DRM_ERROR("Failed to wait for trust OS ready for ring creation\n");
			return ret;
		}

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

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x8000FFFF, false);
	}

	return ret;
}

static int psp_v11_0_8_ring_destroy(struct psp_context *psp,
				  enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v11_0_8_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static uint32_t psp_v11_0_8_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev))
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102);
	else
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67);

	return data;
}

static void psp_v11_0_8_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, value);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
			     GFX_CTRL_CMD_ID_CONSUME_CMD);
	} else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67, value);
}

static const struct psp_funcs psp_v11_0_8_funcs = {
	.ring_init = psp_v11_0_8_ring_init,
	.ring_create = psp_v11_0_8_ring_create,
	.ring_stop = psp_v11_0_8_ring_stop,
	.ring_destroy = psp_v11_0_8_ring_destroy,
	.ring_get_wptr = psp_v11_0_8_ring_get_wptr,
	.ring_set_wptr = psp_v11_0_8_ring_set_wptr,
};

void psp_v11_0_8_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v11_0_8_funcs;
}

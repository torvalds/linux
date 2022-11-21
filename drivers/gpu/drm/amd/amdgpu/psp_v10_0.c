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
	const char *chip_name;
	char fw_name[30];
	int err = 0;
	const struct ta_firmware_header_v1_0 *ta_hdr;
	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
			chip_name = "raven2";
		else if (adev->apu_flags & AMD_APU_IS_PICASSO)
			chip_name = "picasso";
		else
			chip_name = "raven";
		break;
	default: BUG();
	}

	err = psp_init_asd_microcode(psp, chip_name);
	if (err)
		goto out;

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_ta.bin", chip_name);
	err = request_firmware(&adev->psp.ta_fw, fw_name, adev->dev);
	if (err) {
		release_firmware(adev->psp.ta_fw);
		adev->psp.ta_fw = NULL;
		dev_info(adev->dev,
			 "psp v10.0: Failed to load firmware \"%s\"\n",
			 fw_name);
	} else {
		err = amdgpu_ucode_validate(adev->psp.ta_fw);
		if (err)
			goto out2;

		ta_hdr = (const struct ta_firmware_header_v1_0 *)
				 adev->psp.ta_fw->data;
		adev->psp.hdcp_context.context.bin_desc.fw_version =
			le32_to_cpu(ta_hdr->hdcp.fw_version);
		adev->psp.hdcp_context.context.bin_desc.size_bytes =
			le32_to_cpu(ta_hdr->hdcp.size_bytes);
		adev->psp.hdcp_context.context.bin_desc.start_addr =
			(uint8_t *)ta_hdr +
			le32_to_cpu(ta_hdr->header.ucode_array_offset_bytes);

		adev->psp.dtm_context.context.bin_desc.fw_version =
			le32_to_cpu(ta_hdr->dtm.fw_version);
		adev->psp.dtm_context.context.bin_desc.size_bytes =
			le32_to_cpu(ta_hdr->dtm.size_bytes);
		adev->psp.dtm_context.context.bin_desc.start_addr =
			(uint8_t *)adev->psp.hdcp_context.context.bin_desc.start_addr +
			le32_to_cpu(ta_hdr->dtm.offset_bytes);

		adev->psp.securedisplay_context.context.bin_desc.fw_version =
			le32_to_cpu(ta_hdr->securedisplay.fw_version);
		adev->psp.securedisplay_context.context.bin_desc.size_bytes =
			le32_to_cpu(ta_hdr->securedisplay.size_bytes);
		adev->psp.securedisplay_context.context.bin_desc.start_addr =
			(uint8_t *)adev->psp.hdcp_context.context.bin_desc.start_addr +
			le32_to_cpu(ta_hdr->securedisplay.offset_bytes);

		adev->psp.ta_fw_version = le32_to_cpu(ta_hdr->header.ucode_version);
	}

	return 0;

out2:
	release_firmware(adev->psp.ta_fw);
	adev->psp.ta_fw = NULL;
out:
	if (err) {
		dev_err(adev->dev,
			"psp v10.0: Failed to load firmware \"%s\"\n",
			fw_name);
	}

	return err;
}

static int psp_v10_0_ring_init(struct psp_context *psp,
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
			   0x80000000, 0x8000FFFF, false);

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
			   0x80000000, 0x80000000, false);

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
	.ring_init = psp_v10_0_ring_init,
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

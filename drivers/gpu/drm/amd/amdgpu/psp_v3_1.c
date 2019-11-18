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
#include "psp_v3_1.h"

#include "mp/mp_9_0_offset.h"
#include "mp/mp_9_0_sh_mask.h"
#include "gc/gc_9_0_offset.h"
#include "sdma0/sdma0_4_0_offset.h"
#include "nbio/nbio_6_1_offset.h"

#include "oss/osssys_4_0_offset.h"
#include "oss/osssys_4_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/vega10_sos.bin");
MODULE_FIRMWARE("amdgpu/vega10_asd.bin");
MODULE_FIRMWARE("amdgpu/vega12_sos.bin");
MODULE_FIRMWARE("amdgpu/vega12_asd.bin");


#define smnMP1_FIRMWARE_FLAGS 0x3010028

static uint32_t sos_old_versions[] = {1517616, 1510592, 1448594, 1446554};

static bool psp_v3_1_support_vmr_ring(struct psp_context *psp);
static int psp_v3_1_ring_stop(struct psp_context *psp,
			      enum psp_ring_type ring_type);

static int psp_v3_1_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	const char *chip_name;
	char fw_name[30];
	int err = 0;
	const struct psp_firmware_header_v1_0 *hdr;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		chip_name = "vega10";
		break;
	case CHIP_VEGA12:
		chip_name = "vega12";
		break;
	default: BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sos.bin", chip_name);
	err = request_firmware(&adev->psp.sos_fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->psp.sos_fw);
	if (err)
		goto out;

	hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.sos_fw->data;
	adev->psp.sos_fw_version = le32_to_cpu(hdr->header.ucode_version);
	adev->psp.sos_feature_version = le32_to_cpu(hdr->ucode_feature_version);
	adev->psp.sos_bin_size = le32_to_cpu(hdr->sos_size_bytes);
	adev->psp.sys_bin_size = le32_to_cpu(hdr->header.ucode_size_bytes) -
					le32_to_cpu(hdr->sos_size_bytes);
	adev->psp.sys_start_addr = (uint8_t *)hdr +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes);
	adev->psp.sos_start_addr = (uint8_t *)adev->psp.sys_start_addr +
				le32_to_cpu(hdr->sos_offset_bytes);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_asd.bin", chip_name);
	err = request_firmware(&adev->psp.asd_fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = amdgpu_ucode_validate(adev->psp.asd_fw);
	if (err)
		goto out;

	hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.asd_fw->data;
	adev->psp.asd_fw_version = le32_to_cpu(hdr->header.ucode_version);
	adev->psp.asd_feature_version = le32_to_cpu(hdr->ucode_feature_version);
	adev->psp.asd_ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes);
	adev->psp.asd_start_addr = (uint8_t *)hdr +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes);

	return 0;
out:
	if (err) {
		dev_err(adev->dev,
			"psp v3.1: Failed to load firmware \"%s\"\n",
			fw_name);
		release_firmware(adev->psp.sos_fw);
		adev->psp.sos_fw = NULL;
		release_firmware(adev->psp.asd_fw);
		adev->psp.asd_fw = NULL;
	}

	return err;
}

static int psp_v3_1_bootloader_load_sysdrv(struct psp_context *psp)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;
	uint32_t sol_reg;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
	if (sol_reg)
		return 0;

	/* Wait for bootloader to signify that is ready having bit 31 of C2PMSG_35 set to 1 */
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			   0x80000000, 0x80000000, false);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy PSP System Driver binary to memory */
	memcpy(psp->fw_pri_buf, psp->sys_start_addr, psp->sys_bin_size);

	/* Provide the sys driver to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SYSDRV;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);

	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			   0x80000000, 0x80000000, false);

	return ret;
}

static bool psp_v3_1_match_version(struct amdgpu_device *adev, uint32_t ver)
{
	int i;

	if (ver == adev->psp.sos_fw_version)
		return true;

	/*
	 * Double check if the latest four legacy versions.
	 * If yes, it is still the right version.
	 */
	for (i = 0; i < sizeof(sos_old_versions) / sizeof(uint32_t); i++) {
		if (sos_old_versions[i] == adev->psp.sos_fw_version)
			return true;
	}

	return false;
}

static int psp_v3_1_bootloader_load_sos(struct psp_context *psp)
{
	int ret;
	unsigned int psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;
	uint32_t sol_reg, ver;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
	if (sol_reg) {
		psp->sos_fw_version = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_58);
		printk("sos fw version = 0x%x.\n", psp->sos_fw_version);
		return 0;
	}

	/* Wait for bootloader to signify that is ready having bit 31 of C2PMSG_35 set to 1 */
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			   0x80000000, 0x80000000, false);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy Secure OS binary to PSP memory */
	memcpy(psp->fw_pri_buf, psp->sos_start_addr, psp->sos_bin_size);

	/* Provide the PSP secure OS to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SOSDRV;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_81),
			   RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81),
			   0, true);

	ver = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_58);
	if (!psp_v3_1_match_version(adev, ver))
		DRM_WARN("SOS version doesn't match\n");

	return ret;
}

static int psp_v3_1_ring_init(struct psp_context *psp,
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

static void psp_v3_1_reroute_ih(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t tmp;

	/* Change IH ring for VMC */
	tmp = REG_SET_FIELD(0, IH_CLIENT_CFG_DATA, CREDIT_RETURN_ADDR, 0x1244b);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, CLIENT_TYPE, 1);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, RING_ID, 1);

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, 3);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, tmp);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, GFX_CTRL_CMD_ID_GBR_IH_SET);

	mdelay(20);
	psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
		     0x80000000, 0x8000FFFF, false);

	/* Change IH ring for UMC */
	tmp = REG_SET_FIELD(0, IH_CLIENT_CFG_DATA, CREDIT_RETURN_ADDR, 0x1216b);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, RING_ID, 1);

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, 4);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, tmp);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, GFX_CTRL_CMD_ID_GBR_IH_SET);

	mdelay(20);
	psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
		     0x80000000, 0x8000FFFF, false);
}

static int psp_v3_1_ring_create(struct psp_context *psp,
				enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	psp_v3_1_reroute_ih(psp);

	if (psp_v3_1_support_vmr_ring(psp)) {
		ret = psp_v3_1_ring_stop(psp, ring_type);
		if (ret) {
			DRM_ERROR("psp_v3_1_ring_stop_sriov failed!\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_102 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_103 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_103, psp_ring_reg);
		/* No size initialization for sriov  */
		/* Write the ring initialization command to C2PMSG_101 */
		psp_ring_reg = ring_type;
		psp_ring_reg = psp_ring_reg << 16;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101, psp_ring_reg);

		/* there might be hardware handshake issue which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0,
					mmMP0_SMN_C2PMSG_101), 0x80000000,
					0x8000FFFF, false);
	} else {

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

		/* there might be hardware handshake issue which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0,
					mmMP0_SMN_C2PMSG_64), 0x80000000,
					0x8000FFFF, false);

	}
	return ret;
}

static int psp_v3_1_ring_stop(struct psp_context *psp,
			      enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	if (psp_v3_1_support_vmr_ring(psp)) {
		/* Write the Destroy GPCOM ring command to C2PMSG_101 */
		psp_ring_reg = GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101, psp_ring_reg);

		/* there might be handshake issue which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(psp,
				SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
				0x80000000, 0x80000000, false);
	} else {
		/* Write the ring destroy command to C2PMSG_64 */
		psp_ring_reg = 3 << 16;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, psp_ring_reg);

		/* there might be handshake issue which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(psp,
				SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				0x80000000, 0x80000000, false);
	}

	return ret;
}

static int psp_v3_1_ring_destroy(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v3_1_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static int
psp_v3_1_sram_map(struct amdgpu_device *adev,
		  unsigned int *sram_offset, unsigned int *sram_addr_reg_offset,
		  unsigned int *sram_data_reg_offset,
		  enum AMDGPU_UCODE_ID ucode_id)
{
	int ret = 0;

	switch(ucode_id) {
/* TODO: needs to confirm */
#if 0
	case AMDGPU_UCODE_ID_SMC:
		*sram_offset = 0;
		*sram_addr_reg_offset = 0;
		*sram_data_reg_offset = 0;
		break;
#endif

	case AMDGPU_UCODE_ID_CP_CE:
		*sram_offset = 0x0;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_CE_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_CE_UCODE_DATA);
		break;

	case AMDGPU_UCODE_ID_CP_PFP:
		*sram_offset = 0x0;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_PFP_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_PFP_UCODE_DATA);
		break;

	case AMDGPU_UCODE_ID_CP_ME:
		*sram_offset = 0x0;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_HYP_ME_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_HYP_ME_UCODE_DATA);
		break;

	case AMDGPU_UCODE_ID_CP_MEC1:
		*sram_offset = 0x10000;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_MEC_ME1_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_MEC_ME1_UCODE_DATA);
		break;

	case AMDGPU_UCODE_ID_CP_MEC2:
		*sram_offset = 0x10000;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_HYP_MEC2_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_HYP_MEC2_UCODE_DATA);
		break;

	case AMDGPU_UCODE_ID_RLC_G:
		*sram_offset = 0x2000;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_UCODE_DATA);
		break;

	case AMDGPU_UCODE_ID_SDMA0:
		*sram_offset = 0x0;
		*sram_addr_reg_offset = SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_UCODE_ADDR);
		*sram_data_reg_offset = SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_UCODE_DATA);
		break;

/* TODO: needs to confirm */
#if 0
	case AMDGPU_UCODE_ID_SDMA1:
		*sram_offset = ;
		*sram_addr_reg_offset = ;
		break;

	case AMDGPU_UCODE_ID_UVD:
		*sram_offset = ;
		*sram_addr_reg_offset = ;
		break;

	case AMDGPU_UCODE_ID_VCE:
		*sram_offset = ;
		*sram_addr_reg_offset = ;
		break;
#endif

	case AMDGPU_UCODE_ID_MAXIMUM:
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static bool psp_v3_1_compare_sram_data(struct psp_context *psp,
				       struct amdgpu_firmware_info *ucode,
				       enum AMDGPU_UCODE_ID ucode_type)
{
	int err = 0;
	unsigned int fw_sram_reg_val = 0;
	unsigned int fw_sram_addr_reg_offset = 0;
	unsigned int fw_sram_data_reg_offset = 0;
	unsigned int ucode_size;
	uint32_t *ucode_mem = NULL;
	struct amdgpu_device *adev = psp->adev;

	err = psp_v3_1_sram_map(adev, &fw_sram_reg_val, &fw_sram_addr_reg_offset,
				&fw_sram_data_reg_offset, ucode_type);
	if (err)
		return false;

	WREG32(fw_sram_addr_reg_offset, fw_sram_reg_val);

	ucode_size = ucode->ucode_size;
	ucode_mem = (uint32_t *)ucode->kaddr;
	while (ucode_size) {
		fw_sram_reg_val = RREG32(fw_sram_data_reg_offset);

		if (*ucode_mem != fw_sram_reg_val)
			return false;

		ucode_mem++;
		/* 4 bytes */
		ucode_size -= 4;
	}

	return true;
}

static bool psp_v3_1_smu_reload_quirk(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t reg;

	reg = RREG32_PCIE(smnMP1_FIRMWARE_FLAGS | 0x03b00000);
	return (reg & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) ? true : false;
}

static int psp_v3_1_mode1_reset(struct psp_context *psp)
{
	int ret;
	uint32_t offset;
	struct amdgpu_device *adev = psp->adev;

	offset = SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64);

	ret = psp_wait_for(psp, offset, 0x80000000, 0x8000FFFF, false);

	if (ret) {
		DRM_INFO("psp is not working correctly before mode1 reset!\n");
		return -EINVAL;
	}

	/*send the mode 1 reset command*/
	WREG32(offset, GFX_CTRL_CMD_ID_MODE1_RST);

	msleep(500);

	offset = SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_33);

	ret = psp_wait_for(psp, offset, 0x80000000, 0x80000000, false);

	if (ret) {
		DRM_INFO("psp mode 1 reset failed!\n");
		return -EINVAL;
	}

	DRM_INFO("psp mode1 reset succeed \n");

	return 0;
}

static bool psp_v3_1_support_vmr_ring(struct psp_context *psp)
{
	if (amdgpu_sriov_vf(psp->adev))
		return true;

	return false;
}

static uint32_t psp_v3_1_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (psp_v3_1_support_vmr_ring(psp))
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102);
	else
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67);
	return data;
}

static void psp_v3_1_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (psp_v3_1_support_vmr_ring(psp)) {
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, value);
		/* send interrupt to PSP for SRIOV ring write pointer update */
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
			GFX_CTRL_CMD_ID_CONSUME_CMD);
	} else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67, value);
}

static const struct psp_funcs psp_v3_1_funcs = {
	.init_microcode = psp_v3_1_init_microcode,
	.bootloader_load_sysdrv = psp_v3_1_bootloader_load_sysdrv,
	.bootloader_load_sos = psp_v3_1_bootloader_load_sos,
	.ring_init = psp_v3_1_ring_init,
	.ring_create = psp_v3_1_ring_create,
	.ring_stop = psp_v3_1_ring_stop,
	.ring_destroy = psp_v3_1_ring_destroy,
	.compare_sram_data = psp_v3_1_compare_sram_data,
	.smu_reload_quirk = psp_v3_1_smu_reload_quirk,
	.mode1_reset = psp_v3_1_mode1_reset,
	.support_vmr_ring = psp_v3_1_support_vmr_ring,
	.ring_get_wptr = psp_v3_1_ring_get_wptr,
	.ring_set_wptr = psp_v3_1_ring_set_wptr,
};

void psp_v3_1_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v3_1_funcs;
}

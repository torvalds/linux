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

static int psp_v3_1_ring_stop(struct psp_context *psp,
			      enum psp_ring_type ring_type);

static int psp_v3_1_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	const char *chip_name;
	int err = 0;

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

	err = psp_init_sos_microcode(psp, chip_name);
	if (err)
		return err;

	err = psp_init_asd_microcode(psp, chip_name);
	if (err)
		return err;

	return 0;
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

	/* Copy PSP System Driver binary to memory */
	psp_copy_fw(psp, psp->sys_start_addr, psp->sys_bin_size);

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

static int psp_v3_1_bootloader_load_sos(struct psp_context *psp)
{
	int ret;
	unsigned int psp_gfxdrv_command_reg = 0;
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

	/* Copy Secure OS binary to PSP memory */
	psp_copy_fw(psp, psp->sos_start_addr, psp->sos_bin_size);

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

	if (amdgpu_sriov_vf(adev)) {
		ring->ring_wptr = 0;
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
	struct amdgpu_device *adev = psp->adev;

	/* Write the ring destroy command*/
	if (amdgpu_sriov_vf(adev))
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
				     GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING);
	else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64,
				     GFX_CTRL_CMD_ID_DESTROY_RINGS);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);

	/* Wait for response flag (bit 31) */
	if (amdgpu_sriov_vf(adev))
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
				   0x80000000, 0x80000000, false);
	else
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);

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

static uint32_t psp_v3_1_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev))
		data = psp->km_ring.ring_wptr;
	else
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67);
	return data;
}

static void psp_v3_1_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, value);
		/* send interrupt to PSP for SRIOV ring write pointer update */
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
			GFX_CTRL_CMD_ID_CONSUME_CMD);
		psp->km_ring.ring_wptr = value;
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
	.smu_reload_quirk = psp_v3_1_smu_reload_quirk,
	.mode1_reset = psp_v3_1_mode1_reset,
	.ring_get_wptr = psp_v3_1_ring_get_wptr,
	.ring_set_wptr = psp_v3_1_ring_set_wptr,
};

void psp_v3_1_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v3_1_funcs;
}

/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_ucode.h"
#include "amdgpu_psp.h"
#include "amdgpu_smu.h"
#include "atom.h"
#include "amd_pcie.h"

#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "hdp/hdp_5_0_0_offset.h"
#include "hdp/hdp_5_0_0_sh_mask.h"
#include "smuio/smuio_11_0_0_offset.h"
#include "mp/mp_11_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "gmc_v10_0.h"
#include "gfxhub_v2_0.h"
#include "mmhub_v2_0.h"
#include "nbio_v2_3.h"
#include "nv.h"
#include "navi10_ih.h"
#include "gfx_v10_0.h"
#include "sdma_v5_0.h"
#include "sdma_v5_2.h"
#include "vcn_v2_0.h"
#include "jpeg_v2_0.h"
#include "vcn_v3_0.h"
#include "jpeg_v3_0.h"
#include "dce_virtual.h"
#include "mes_v10_1.h"
#include "mxgpu_nv.h"

static const struct amd_ip_funcs nv_common_ip_funcs;

/*
 * Indirect registers accessor
 */
static u32 nv_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long address, data;
	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	return amdgpu_device_indirect_rreg(adev, address, data, reg);
}

static void nv_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg(adev, address, data, reg, v);
}

static u64 nv_pcie_rreg64(struct amdgpu_device *adev, u32 reg)
{
	unsigned long address, data;
	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	return amdgpu_device_indirect_rreg64(adev, address, data, reg);
}

static void nv_pcie_wreg64(struct amdgpu_device *adev, u32 reg, u64 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg64(adev, address, data, reg, v);
}

static u32 nv_didt_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;

	address = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_INDEX);
	data = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_DATA);

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(address, (reg));
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
	return r;
}

static void nv_didt_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_INDEX);
	data = SOC15_REG_OFFSET(GC, 0, mmDIDT_IND_DATA);

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(address, (reg));
	WREG32(data, (v));
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
}

static u32 nv_get_config_memsize(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_memsize(adev);
}

static u32 nv_get_xclk(struct amdgpu_device *adev)
{
	return adev->clock.spll.reference_freq;
}


void nv_grbm_select(struct amdgpu_device *adev,
		     u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 grbm_gfx_cntl = 0;
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, PIPEID, pipe);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, MEID, me);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, VMID, vmid);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, QUEUEID, queue);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmGRBM_GFX_CNTL), grbm_gfx_cntl);
}

static void nv_vga_set_state(struct amdgpu_device *adev, bool state)
{
	/* todo */
}

static bool nv_read_disabled_bios(struct amdgpu_device *adev)
{
	/* todo */
	return false;
}

static bool nv_read_bios_from_rom(struct amdgpu_device *adev,
				  u8 *bios, u32 length_bytes)
{
	u32 *dw_ptr;
	u32 i, length_dw;

	if (bios == NULL)
		return false;
	if (length_bytes == 0)
		return false;
	/* APU vbios image is part of sbios image */
	if (adev->flags & AMD_IS_APU)
		return false;

	dw_ptr = (u32 *)bios;
	length_dw = ALIGN(length_bytes, 4) / 4;

	/* set rom index to 0 */
	WREG32(SOC15_REG_OFFSET(SMUIO, 0, mmROM_INDEX), 0);
	/* read out the rom data */
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(SOC15_REG_OFFSET(SMUIO, 0, mmROM_DATA));

	return true;
}

static struct soc15_allowed_register_entry nv_allowed_read_registers[] = {
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS2)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE0)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE1)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE2)},
	{ SOC15_REG_ENTRY(GC, 0, mmGRBM_STATUS_SE3)},
	{ SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_STATUS_REG)},
	{ SOC15_REG_ENTRY(SDMA1, 0, mmSDMA1_STATUS_REG)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STALLED_STAT2)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_STALLED_STAT3)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPF_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPF_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPF_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPC_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPC_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, mmCP_CPC_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, mmGB_ADDR_CONFIG)},
};

static uint32_t nv_read_indexed_register(struct amdgpu_device *adev, u32 se_num,
					 u32 sh_num, u32 reg_offset)
{
	uint32_t val;

	mutex_lock(&adev->grbm_idx_mutex);
	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, se_num, sh_num, 0xffffffff);

	val = RREG32(reg_offset);

	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
	return val;
}

static uint32_t nv_get_register_value(struct amdgpu_device *adev,
				      bool indexed, u32 se_num,
				      u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		return nv_read_indexed_register(adev, se_num, sh_num, reg_offset);
	} else {
		if (reg_offset == SOC15_REG_OFFSET(GC, 0, mmGB_ADDR_CONFIG))
			return adev->gfx.config.gb_addr_config;
		return RREG32(reg_offset);
	}
}

static int nv_read_register(struct amdgpu_device *adev, u32 se_num,
			    u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;
	struct soc15_allowed_register_entry  *en;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(nv_allowed_read_registers); i++) {
		en = &nv_allowed_read_registers[i];
		if ((i == 7 && (adev->sdma.num_instances == 1)) || /* some asics don't have SDMA1 */
		    reg_offset !=
		    (adev->reg_offset[en->hwip][en->inst][en->seg] + en->reg_offset))
			continue;

		*value = nv_get_register_value(adev,
					       nv_allowed_read_registers[i].grbm_indexed,
					       se_num, sh_num, reg_offset);
		return 0;
	}
	return -EINVAL;
}

static int nv_asic_mode1_reset(struct amdgpu_device *adev)
{
	u32 i;
	int ret = 0;

	amdgpu_atombios_scratch_regs_engine_hung(adev, true);

	/* disable BM */
	pci_clear_master(adev->pdev);

	amdgpu_device_cache_pci_state(adev->pdev);

	if (amdgpu_dpm_is_mode1_reset_supported(adev)) {
		dev_info(adev->dev, "GPU smu mode1 reset\n");
		ret = amdgpu_dpm_mode1_reset(adev);
	} else {
		dev_info(adev->dev, "GPU psp mode1 reset\n");
		ret = psp_gpu_reset(adev);
	}

	if (ret)
		dev_err(adev->dev, "GPU mode1 reset failed\n");
	amdgpu_device_load_pci_state(adev->pdev);

	/* wait for asic to come out of reset */
	for (i = 0; i < adev->usec_timeout; i++) {
		u32 memsize = adev->nbio.funcs->get_memsize(adev);

		if (memsize != 0xffffffff)
			break;
		udelay(1);
	}

	amdgpu_atombios_scratch_regs_engine_hung(adev, false);

	return ret;
}

static bool nv_asic_supports_baco(struct amdgpu_device *adev)
{
	struct smu_context *smu = &adev->smu;

	if (smu_baco_is_support(smu))
		return true;
	else
		return false;
}

static enum amd_reset_method
nv_asic_reset_method(struct amdgpu_device *adev)
{
	struct smu_context *smu = &adev->smu;

	if (amdgpu_reset_method == AMD_RESET_METHOD_MODE1 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_BACO)
		return amdgpu_reset_method;

	if (amdgpu_reset_method != -1)
		dev_warn(adev->dev, "Specified reset method:%d isn't supported, using AUTO instead.\n",
				  amdgpu_reset_method);

	switch (adev->asic_type) {
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
		return AMD_RESET_METHOD_MODE1;
	default:
		if (smu_baco_is_support(smu))
			return AMD_RESET_METHOD_BACO;
		else
			return AMD_RESET_METHOD_MODE1;
	}
}

static int nv_asic_reset(struct amdgpu_device *adev)
{
	int ret = 0;
	struct smu_context *smu = &adev->smu;

	if (nv_asic_reset_method(adev) == AMD_RESET_METHOD_BACO) {
		dev_info(adev->dev, "BACO reset\n");

		ret = smu_baco_enter(smu);
		if (ret)
			return ret;
		ret = smu_baco_exit(smu);
		if (ret)
			return ret;
	} else {
		dev_info(adev->dev, "MODE1 reset\n");
		ret = nv_asic_mode1_reset(adev);
	}

	return ret;
}

static int nv_set_uvd_clocks(struct amdgpu_device *adev, u32 vclk, u32 dclk)
{
	/* todo */
	return 0;
}

static int nv_set_vce_clocks(struct amdgpu_device *adev, u32 evclk, u32 ecclk)
{
	/* todo */
	return 0;
}

static void nv_pcie_gen3_enable(struct amdgpu_device *adev)
{
	if (pci_is_root_bus(adev->pdev->bus))
		return;

	if (amdgpu_pcie_gen2 == 0)
		return;

	if (!(adev->pm.pcie_gen_mask & (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2 |
					CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)))
		return;

	/* todo */
}

static void nv_program_aspm(struct amdgpu_device *adev)
{

	if (amdgpu_aspm == 0)
		return;

	/* todo */
}

static void nv_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable)
{
	adev->nbio.funcs->enable_doorbell_aperture(adev, enable);
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, enable);
}

static const struct amdgpu_ip_block_version nv_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &nv_common_ip_funcs,
};

static int nv_reg_base_init(struct amdgpu_device *adev)
{
	int r;

	if (amdgpu_discovery) {
		r = amdgpu_discovery_reg_base_init(adev);
		if (r) {
			DRM_WARN("failed to init reg base from ip discovery table, "
					"fallback to legacy init method\n");
			goto legacy_init;
		}

		return 0;
	}

legacy_init:
	switch (adev->asic_type) {
	case CHIP_NAVI10:
		navi10_reg_base_init(adev);
		break;
	case CHIP_NAVI14:
		navi14_reg_base_init(adev);
		break;
	case CHIP_NAVI12:
		navi12_reg_base_init(adev);
		break;
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
		sienna_cichlid_reg_base_init(adev);
		break;
	case CHIP_VANGOGH:
		vangogh_reg_base_init(adev);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void nv_set_virt_ops(struct amdgpu_device *adev)
{
	adev->virt.ops = &xgpu_nv_virt_ops;
}

int nv_set_ip_blocks(struct amdgpu_device *adev)
{
	int r;

	adev->nbio.funcs = &nbio_v2_3_funcs;
	adev->nbio.hdp_flush_reg = &nbio_v2_3_hdp_flush_reg;

	if (adev->asic_type == CHIP_SIENNA_CICHLID)
		adev->gmc.xgmi.supported = true;

	/* Set IP register base before any HW register access */
	r = nv_reg_base_init(adev);
	if (r)
		return r;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP &&
		    !amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT &&
		    !amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v2_0_ip_block);
		amdgpu_device_ip_block_add(adev, &jpeg_v2_0_ip_block);
		if (adev->enable_mes)
			amdgpu_device_ip_block_add(adev, &mes_v10_1_ip_block);
		break;
	case CHIP_NAVI12:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT &&
		    !amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v2_0_ip_block);
		if (!amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &jpeg_v2_0_ip_block);
		break;
	case CHIP_SIENNA_CICHLID:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP &&
		    is_support_sw_smu(adev) && !amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
		if (!amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &jpeg_v3_0_ip_block);

		if (adev->enable_mes)
			amdgpu_device_ip_block_add(adev, &mes_v10_1_ip_block);
		break;
	case CHIP_NAVY_FLOUNDER:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP &&
		    is_support_sw_smu(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &jpeg_v3_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT &&
		    is_support_sw_smu(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		break;
	case CHIP_VANGOGH:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static uint32_t nv_get_rev_id(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_rev_id(adev);
}

static void nv_flush_hdp(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	adev->nbio.funcs->hdp_flush(adev, ring);
}

static void nv_invalidate_hdp(struct amdgpu_device *adev,
				struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg) {
		WREG32_SOC15_NO_KIQ(HDP, 0, mmHDP_READ_CACHE_INVALIDATE, 1);
	} else {
		amdgpu_ring_emit_wreg(ring, SOC15_REG_OFFSET(
					HDP, 0, mmHDP_READ_CACHE_INVALIDATE), 1);
	}
}

static bool nv_need_full_reset(struct amdgpu_device *adev)
{
	return true;
}

static bool nv_need_reset_on_init(struct amdgpu_device *adev)
{
	u32 sol_reg;

	if (adev->flags & AMD_IS_APU)
		return false;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);
	if (sol_reg)
		return true;

	return false;
}

static uint64_t nv_get_pcie_replay_count(struct amdgpu_device *adev)
{

	/* TODO
	 * dummy implement for pcie_replay_count sysfs interface
	 * */

	return 0;
}

static void nv_init_doorbell_index(struct amdgpu_device *adev)
{
	adev->doorbell_index.kiq = AMDGPU_NAVI10_DOORBELL_KIQ;
	adev->doorbell_index.mec_ring0 = AMDGPU_NAVI10_DOORBELL_MEC_RING0;
	adev->doorbell_index.mec_ring1 = AMDGPU_NAVI10_DOORBELL_MEC_RING1;
	adev->doorbell_index.mec_ring2 = AMDGPU_NAVI10_DOORBELL_MEC_RING2;
	adev->doorbell_index.mec_ring3 = AMDGPU_NAVI10_DOORBELL_MEC_RING3;
	adev->doorbell_index.mec_ring4 = AMDGPU_NAVI10_DOORBELL_MEC_RING4;
	adev->doorbell_index.mec_ring5 = AMDGPU_NAVI10_DOORBELL_MEC_RING5;
	adev->doorbell_index.mec_ring6 = AMDGPU_NAVI10_DOORBELL_MEC_RING6;
	adev->doorbell_index.mec_ring7 = AMDGPU_NAVI10_DOORBELL_MEC_RING7;
	adev->doorbell_index.userqueue_start = AMDGPU_NAVI10_DOORBELL_USERQUEUE_START;
	adev->doorbell_index.userqueue_end = AMDGPU_NAVI10_DOORBELL_USERQUEUE_END;
	adev->doorbell_index.gfx_ring0 = AMDGPU_NAVI10_DOORBELL_GFX_RING0;
	adev->doorbell_index.gfx_ring1 = AMDGPU_NAVI10_DOORBELL_GFX_RING1;
	adev->doorbell_index.mes_ring = AMDGPU_NAVI10_DOORBELL_MES_RING;
	adev->doorbell_index.sdma_engine[0] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0;
	adev->doorbell_index.sdma_engine[1] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE1;
	adev->doorbell_index.sdma_engine[2] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE2;
	adev->doorbell_index.sdma_engine[3] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE3;
	adev->doorbell_index.ih = AMDGPU_NAVI10_DOORBELL_IH;
	adev->doorbell_index.vcn.vcn_ring0_1 = AMDGPU_NAVI10_DOORBELL64_VCN0_1;
	adev->doorbell_index.vcn.vcn_ring2_3 = AMDGPU_NAVI10_DOORBELL64_VCN2_3;
	adev->doorbell_index.vcn.vcn_ring4_5 = AMDGPU_NAVI10_DOORBELL64_VCN4_5;
	adev->doorbell_index.vcn.vcn_ring6_7 = AMDGPU_NAVI10_DOORBELL64_VCN6_7;
	adev->doorbell_index.first_non_cp = AMDGPU_NAVI10_DOORBELL64_FIRST_NON_CP;
	adev->doorbell_index.last_non_cp = AMDGPU_NAVI10_DOORBELL64_LAST_NON_CP;

	adev->doorbell_index.max_assignment = AMDGPU_NAVI10_DOORBELL_MAX_ASSIGNMENT << 1;
	adev->doorbell_index.sdma_doorbell_range = 20;
}

static void nv_pre_asic_init(struct amdgpu_device *adev)
{
}

static const struct amdgpu_asic_funcs nv_asic_funcs =
{
	.read_disabled_bios = &nv_read_disabled_bios,
	.read_bios_from_rom = &nv_read_bios_from_rom,
	.read_register = &nv_read_register,
	.reset = &nv_asic_reset,
	.reset_method = &nv_asic_reset_method,
	.set_vga_state = &nv_vga_set_state,
	.get_xclk = &nv_get_xclk,
	.set_uvd_clocks = &nv_set_uvd_clocks,
	.set_vce_clocks = &nv_set_vce_clocks,
	.get_config_memsize = &nv_get_config_memsize,
	.flush_hdp = &nv_flush_hdp,
	.invalidate_hdp = &nv_invalidate_hdp,
	.init_doorbell_index = &nv_init_doorbell_index,
	.need_full_reset = &nv_need_full_reset,
	.need_reset_on_init = &nv_need_reset_on_init,
	.get_pcie_replay_count = &nv_get_pcie_replay_count,
	.supports_baco = &nv_asic_supports_baco,
	.pre_asic_init = &nv_pre_asic_init,
};

static int nv_common_early_init(void *handle)
{
#define MMIO_REG_HOLE_OFFSET (0x80000 - PAGE_SIZE)
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->rmmio_remap.reg_offset = MMIO_REG_HOLE_OFFSET;
	adev->rmmio_remap.bus_addr = adev->rmmio_base + MMIO_REG_HOLE_OFFSET;
	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &nv_pcie_rreg;
	adev->pcie_wreg = &nv_pcie_wreg;
	adev->pcie_rreg64 = &nv_pcie_rreg64;
	adev->pcie_wreg64 = &nv_pcie_wreg64;

	/* TODO: will add them during VCN v2 implementation */
	adev->uvd_ctx_rreg = NULL;
	adev->uvd_ctx_wreg = NULL;

	adev->didt_rreg = &nv_didt_rreg;
	adev->didt_wreg = &nv_didt_wreg;

	adev->asic_funcs = &nv_asic_funcs;

	adev->rev_id = nv_get_rev_id(adev);
	adev->external_rev_id = 0xff;
	switch (adev->asic_type) {
	case CHIP_NAVI10:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_ATHUB;
		adev->external_rev_id = adev->rev_id + 0x1;
		break;
	case CHIP_NAVI14:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_VCN_DPG;
		adev->external_rev_id = adev->rev_id + 20;
		break;
	case CHIP_NAVI12:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_ATHUB;
		/* guest vm gets 0xffffffff when reading RCC_DEV0_EPF0_STRAP0,
		 * as a consequence, the rev_id and external_rev_id are wrong.
		 * workaround it by hardcoding rev_id to 0 (default value).
		 */
		if (amdgpu_sriov_vf(adev))
			adev->rev_id = 0;
		adev->external_rev_id = adev->rev_id + 0xa;
		break;
	case CHIP_SIENNA_CICHLID:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_MC_LS;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_ATHUB |
			AMD_PG_SUPPORT_MMHUB;
		if (amdgpu_sriov_vf(adev)) {
			/* hypervisor control CG and PG enablement */
			adev->cg_flags = 0;
			adev->pg_flags = 0;
		}
		adev->external_rev_id = adev->rev_id + 0x28;
		break;
	case CHIP_NAVY_FLOUNDER:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_IH_CG;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_ATHUB |
			AMD_PG_SUPPORT_MMHUB;
		adev->external_rev_id = adev->rev_id + 0x32;
		break;

	case CHIP_VANGOGH:
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x01;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_init_setting(adev);
		xgpu_nv_mailbox_set_irq_funcs(adev);
	}

	return 0;
}

static int nv_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_get_irq(adev);

	return 0;
}

static int nv_common_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_add_irq_id(adev);

	return 0;
}

static int nv_common_sw_fini(void *handle)
{
	return 0;
}

static int nv_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* enable pcie gen2/3 link */
	nv_pcie_gen3_enable(adev);
	/* enable aspm */
	nv_program_aspm(adev);
	/* setup nbio registers */
	adev->nbio.funcs->init_registers(adev);
	/* remap HDP registers to a hole in mmio space,
	 * for the purpose of expose those registers
	 * to process space
	 */
	if (adev->nbio.funcs->remap_hdp_registers)
		adev->nbio.funcs->remap_hdp_registers(adev);
	/* enable the doorbell aperture */
	nv_enable_doorbell_aperture(adev, true);

	return 0;
}

static int nv_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* disable the doorbell aperture */
	nv_enable_doorbell_aperture(adev, false);

	return 0;
}

static int nv_common_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return nv_common_hw_fini(adev);
}

static int nv_common_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return nv_common_hw_init(adev);
}

static bool nv_common_is_idle(void *handle)
{
	return true;
}

static int nv_common_wait_for_idle(void *handle)
{
	return 0;
}

static int nv_common_soft_reset(void *handle)
{
	return 0;
}

static void nv_update_hdp_mem_power_gating(struct amdgpu_device *adev,
					   bool enable)
{
	uint32_t hdp_clk_cntl, hdp_clk_cntl1;
	uint32_t hdp_mem_pwr_cntl;

	if (!(adev->cg_flags & (AMD_CG_SUPPORT_HDP_LS |
				AMD_CG_SUPPORT_HDP_DS |
				AMD_CG_SUPPORT_HDP_SD)))
		return;

	hdp_clk_cntl = hdp_clk_cntl1 = RREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL);
	hdp_mem_pwr_cntl = RREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL);

	/* Before doing clock/power mode switch,
	 * forced on IPH & RC clock */
	hdp_clk_cntl = REG_SET_FIELD(hdp_clk_cntl, HDP_CLK_CNTL,
				     IPH_MEM_CLK_SOFT_OVERRIDE, 1);
	hdp_clk_cntl = REG_SET_FIELD(hdp_clk_cntl, HDP_CLK_CNTL,
				     RC_MEM_CLK_SOFT_OVERRIDE, 1);
	WREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL, hdp_clk_cntl);

	/* HDP 5.0 doesn't support dynamic power mode switch,
	 * disable clock and power gating before any changing */
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_CTRL_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_LS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_DS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 IPH_MEM_POWER_SD_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_CTRL_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_LS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_DS_EN, 0);
	hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
					 RC_MEM_POWER_SD_EN, 0);
	WREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL, hdp_mem_pwr_cntl);

	/* only one clock gating mode (LS/DS/SD) can be enabled */
	if (adev->cg_flags & AMD_CG_SUPPORT_HDP_LS) {
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
						 HDP_MEM_POWER_CTRL,
						 IPH_MEM_POWER_LS_EN, enable);
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
						 HDP_MEM_POWER_CTRL,
						 RC_MEM_POWER_LS_EN, enable);
	} else if (adev->cg_flags & AMD_CG_SUPPORT_HDP_DS) {
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
						 HDP_MEM_POWER_CTRL,
						 IPH_MEM_POWER_DS_EN, enable);
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
						 HDP_MEM_POWER_CTRL,
						 RC_MEM_POWER_DS_EN, enable);
	} else if (adev->cg_flags & AMD_CG_SUPPORT_HDP_SD) {
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
						 HDP_MEM_POWER_CTRL,
						 IPH_MEM_POWER_SD_EN, enable);
		/* RC should not use shut down mode, fallback to ds */
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl,
						 HDP_MEM_POWER_CTRL,
						 RC_MEM_POWER_DS_EN, enable);
	}

	/* confirmed that IPH_MEM_POWER_CTRL_EN and RC_MEM_POWER_CTRL_EN have to
	 * be set for SRAM LS/DS/SD */
	if (adev->cg_flags & (AMD_CG_SUPPORT_HDP_LS | AMD_CG_SUPPORT_HDP_DS |
							AMD_CG_SUPPORT_HDP_SD)) {
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
						IPH_MEM_POWER_CTRL_EN, 1);
		hdp_mem_pwr_cntl = REG_SET_FIELD(hdp_mem_pwr_cntl, HDP_MEM_POWER_CTRL,
						RC_MEM_POWER_CTRL_EN, 1);
	}

	WREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL, hdp_mem_pwr_cntl);

	/* restore IPH & RC clock override after clock/power mode changing */
	WREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL, hdp_clk_cntl1);
}

static void nv_update_hdp_clock_gating(struct amdgpu_device *adev,
				       bool enable)
{
	uint32_t hdp_clk_cntl;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_HDP_MGCG))
		return;

	hdp_clk_cntl = RREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL);

	if (enable) {
		hdp_clk_cntl &=
			~(uint32_t)
			  (HDP_CLK_CNTL__IPH_MEM_CLK_SOFT_OVERRIDE_MASK |
			   HDP_CLK_CNTL__RC_MEM_CLK_SOFT_OVERRIDE_MASK |
			   HDP_CLK_CNTL__DBUS_CLK_SOFT_OVERRIDE_MASK |
			   HDP_CLK_CNTL__DYN_CLK_SOFT_OVERRIDE_MASK |
			   HDP_CLK_CNTL__XDP_REG_CLK_SOFT_OVERRIDE_MASK |
			   HDP_CLK_CNTL__HDP_REG_CLK_SOFT_OVERRIDE_MASK);
	} else {
		hdp_clk_cntl |= HDP_CLK_CNTL__IPH_MEM_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__RC_MEM_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__DBUS_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__DYN_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__XDP_REG_CLK_SOFT_OVERRIDE_MASK |
			HDP_CLK_CNTL__HDP_REG_CLK_SOFT_OVERRIDE_MASK;
	}

	WREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL, hdp_clk_cntl);
}

static int nv_common_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
		adev->nbio.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->nbio.funcs->update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		nv_update_hdp_mem_power_gating(adev,
				   state == AMD_CG_STATE_GATE);
		nv_update_hdp_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}
	return 0;
}

static int nv_common_set_powergating_state(void *handle,
					   enum amd_powergating_state state)
{
	/* TODO */
	return 0;
}

static void nv_common_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	uint32_t tmp;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	adev->nbio.funcs->get_clockgating_state(adev, flags);

	/* AMD_CG_SUPPORT_HDP_MGCG */
	tmp = RREG32_SOC15(HDP, 0, mmHDP_CLK_CNTL);
	if (!(tmp & (HDP_CLK_CNTL__IPH_MEM_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__RC_MEM_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__DBUS_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__DYN_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__XDP_REG_CLK_SOFT_OVERRIDE_MASK |
		     HDP_CLK_CNTL__HDP_REG_CLK_SOFT_OVERRIDE_MASK)))
		*flags |= AMD_CG_SUPPORT_HDP_MGCG;

	/* AMD_CG_SUPPORT_HDP_LS/DS/SD */
	tmp = RREG32_SOC15(HDP, 0, mmHDP_MEM_POWER_CTRL);
	if (tmp & HDP_MEM_POWER_CTRL__IPH_MEM_POWER_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_LS;
	else if (tmp & HDP_MEM_POWER_CTRL__IPH_MEM_POWER_DS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_DS;
	else if (tmp & HDP_MEM_POWER_CTRL__IPH_MEM_POWER_SD_EN_MASK)
		*flags |= AMD_CG_SUPPORT_HDP_SD;

	return;
}

static const struct amd_ip_funcs nv_common_ip_funcs = {
	.name = "nv_common",
	.early_init = nv_common_early_init,
	.late_init = nv_common_late_init,
	.sw_init = nv_common_sw_init,
	.sw_fini = nv_common_sw_fini,
	.hw_init = nv_common_hw_init,
	.hw_fini = nv_common_hw_fini,
	.suspend = nv_common_suspend,
	.resume = nv_common_resume,
	.is_idle = nv_common_is_idle,
	.wait_for_idle = nv_common_wait_for_idle,
	.soft_reset = nv_common_soft_reset,
	.set_clockgating_state = nv_common_set_clockgating_state,
	.set_powergating_state = nv_common_set_powergating_state,
	.get_clockgating_state = nv_common_get_clockgating_state,
};

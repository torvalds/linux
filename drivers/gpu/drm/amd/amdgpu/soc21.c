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

#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"
#include "mp/mp_13_0_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "soc21.h"
#include "mxgpu_nv.h"

static const struct amd_ip_funcs soc21_common_ip_funcs;

/* SOC21 */
static const struct amdgpu_video_codec_info vcn_4_0_0_video_codecs_encode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 2304, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 4096, 2304, 0)},
};

static const struct amdgpu_video_codecs vcn_4_0_0_video_codecs_encode =
{
	.codec_count = ARRAY_SIZE(vcn_4_0_0_video_codecs_encode_array),
	.codec_array = vcn_4_0_0_video_codecs_encode_array,
};

static const struct amdgpu_video_codec_info vcn_4_0_0_video_codecs_decode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs vcn_4_0_0_video_codecs_decode =
{
	.codec_count = ARRAY_SIZE(vcn_4_0_0_video_codecs_decode_array),
	.codec_array = vcn_4_0_0_video_codecs_decode_array,
};

static int soc21_query_video_codecs(struct amdgpu_device *adev, bool encode,
				 const struct amdgpu_video_codecs **codecs)
{
	switch (adev->ip_versions[UVD_HWIP][0]) {

	case IP_VERSION(4, 0, 0):
	case IP_VERSION(4, 0, 2):
		if (encode)
			*codecs = &vcn_4_0_0_video_codecs_encode;
		else
			*codecs = &vcn_4_0_0_video_codecs_decode;
		return 0;
	default:
		return -EINVAL;
	}
}
/*
 * Indirect registers accessor
 */
static u32 soc21_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long address, data;
	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	return amdgpu_device_indirect_rreg(adev, address, data, reg);
}

static void soc21_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg(adev, address, data, reg, v);
}

static u64 soc21_pcie_rreg64(struct amdgpu_device *adev, u32 reg)
{
	unsigned long address, data;
	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	return amdgpu_device_indirect_rreg64(adev, address, data, reg);
}

static void soc21_pcie_wreg64(struct amdgpu_device *adev, u32 reg, u64 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg64(adev, address, data, reg, v);
}

static u32 soc21_didt_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;

	address = SOC15_REG_OFFSET(GC, 0, regDIDT_IND_INDEX);
	data = SOC15_REG_OFFSET(GC, 0, regDIDT_IND_DATA);

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(address, (reg));
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
	return r;
}

static void soc21_didt_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = SOC15_REG_OFFSET(GC, 0, regDIDT_IND_INDEX);
	data = SOC15_REG_OFFSET(GC, 0, regDIDT_IND_DATA);

	spin_lock_irqsave(&adev->didt_idx_lock, flags);
	WREG32(address, (reg));
	WREG32(data, (v));
	spin_unlock_irqrestore(&adev->didt_idx_lock, flags);
}

static u32 soc21_get_config_memsize(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_memsize(adev);
}

static u32 soc21_get_xclk(struct amdgpu_device *adev)
{
	return adev->clock.spll.reference_freq;
}


void soc21_grbm_select(struct amdgpu_device *adev,
		     u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 grbm_gfx_cntl = 0;
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, PIPEID, pipe);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, MEID, me);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, VMID, vmid);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, QUEUEID, queue);

	WREG32_SOC15(GC, 0, regGRBM_GFX_CNTL, grbm_gfx_cntl);
}

static void soc21_vga_set_state(struct amdgpu_device *adev, bool state)
{
	/* todo */
}

static bool soc21_read_disabled_bios(struct amdgpu_device *adev)
{
	/* todo */
	return false;
}

static struct soc15_allowed_register_entry soc21_allowed_read_registers[] = {
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS2)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE0)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE1)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE2)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE3)},
	{ SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_STATUS_REG)},
	{ SOC15_REG_ENTRY(SDMA1, 0, regSDMA1_STATUS_REG)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT2)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT3)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, regGB_ADDR_CONFIG)},
};

static uint32_t soc21_read_indexed_register(struct amdgpu_device *adev, u32 se_num,
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

static uint32_t soc21_get_register_value(struct amdgpu_device *adev,
				      bool indexed, u32 se_num,
				      u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		return soc21_read_indexed_register(adev, se_num, sh_num, reg_offset);
	} else {
		if (reg_offset == SOC15_REG_OFFSET(GC, 0, regGB_ADDR_CONFIG) && adev->gfx.config.gb_addr_config)
			return adev->gfx.config.gb_addr_config;
		return RREG32(reg_offset);
	}
}

static int soc21_read_register(struct amdgpu_device *adev, u32 se_num,
			    u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;
	struct soc15_allowed_register_entry  *en;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(soc21_allowed_read_registers); i++) {
		en = &soc21_allowed_read_registers[i];
		if (adev->reg_offset[en->hwip][en->inst] &&
		    reg_offset != (adev->reg_offset[en->hwip][en->inst][en->seg]
				   + en->reg_offset))
			continue;

		*value = soc21_get_register_value(adev,
					       soc21_allowed_read_registers[i].grbm_indexed,
					       se_num, sh_num, reg_offset);
		return 0;
	}
	return -EINVAL;
}

#if 0
static int soc21_asic_mode1_reset(struct amdgpu_device *adev)
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
#endif

static enum amd_reset_method
soc21_asic_reset_method(struct amdgpu_device *adev)
{
	if (amdgpu_reset_method == AMD_RESET_METHOD_MODE1 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_MODE2 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_BACO)
		return amdgpu_reset_method;

	if (amdgpu_reset_method != -1)
		dev_warn(adev->dev, "Specified reset method:%d isn't supported, using AUTO instead.\n",
				  amdgpu_reset_method);

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 7):
	case IP_VERSION(13, 0, 10):
		return AMD_RESET_METHOD_MODE1;
	case IP_VERSION(13, 0, 4):
	case IP_VERSION(13, 0, 11):
		return AMD_RESET_METHOD_MODE2;
	default:
		if (amdgpu_dpm_is_baco_supported(adev))
			return AMD_RESET_METHOD_BACO;
		else
			return AMD_RESET_METHOD_MODE1;
	}
}

static int soc21_asic_reset(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (soc21_asic_reset_method(adev)) {
	case AMD_RESET_METHOD_PCI:
		dev_info(adev->dev, "PCI reset\n");
		ret = amdgpu_device_pci_reset(adev);
		break;
	case AMD_RESET_METHOD_BACO:
		dev_info(adev->dev, "BACO reset\n");
		ret = amdgpu_dpm_baco_reset(adev);
		break;
	case AMD_RESET_METHOD_MODE2:
		dev_info(adev->dev, "MODE2 reset\n");
		ret = amdgpu_dpm_mode2_reset(adev);
		break;
	default:
		dev_info(adev->dev, "MODE1 reset\n");
		ret = amdgpu_device_mode1_reset(adev);
		break;
	}

	return ret;
}

static int soc21_set_uvd_clocks(struct amdgpu_device *adev, u32 vclk, u32 dclk)
{
	/* todo */
	return 0;
}

static int soc21_set_vce_clocks(struct amdgpu_device *adev, u32 evclk, u32 ecclk)
{
	/* todo */
	return 0;
}

static void soc21_pcie_gen3_enable(struct amdgpu_device *adev)
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

static void soc21_program_aspm(struct amdgpu_device *adev)
{
	if (!amdgpu_device_should_use_aspm(adev))
		return;

	if (!(adev->flags & AMD_IS_APU) &&
	    (adev->nbio.funcs->program_aspm))
		adev->nbio.funcs->program_aspm(adev);
}

static void soc21_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable)
{
	adev->nbio.funcs->enable_doorbell_aperture(adev, enable);
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, enable);
}

const struct amdgpu_ip_block_version soc21_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &soc21_common_ip_funcs,
};

static uint32_t soc21_get_rev_id(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_rev_id(adev);
}

static bool soc21_need_full_reset(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(11, 0, 0):
		return amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__UMC);
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
		return false;
	default:
		return true;
	}
}

static bool soc21_need_reset_on_init(struct amdgpu_device *adev)
{
	u32 sol_reg;

	if (adev->flags & AMD_IS_APU)
		return false;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, regMP0_SMN_C2PMSG_81);
	if (sol_reg)
		return true;

	return false;
}

static uint64_t soc21_get_pcie_replay_count(struct amdgpu_device *adev)
{

	/* TODO
	 * dummy implement for pcie_replay_count sysfs interface
	 * */

	return 0;
}

static void soc21_init_doorbell_index(struct amdgpu_device *adev)
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
	adev->doorbell_index.gfx_userqueue_start =
		AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_START;
	adev->doorbell_index.gfx_userqueue_end =
		AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_END;
	adev->doorbell_index.mes_ring0 = AMDGPU_NAVI10_DOORBELL_MES_RING0;
	adev->doorbell_index.mes_ring1 = AMDGPU_NAVI10_DOORBELL_MES_RING1;
	adev->doorbell_index.sdma_engine[0] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0;
	adev->doorbell_index.sdma_engine[1] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE1;
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

static void soc21_pre_asic_init(struct amdgpu_device *adev)
{
}

static int soc21_update_umd_stable_pstate(struct amdgpu_device *adev,
					  bool enter)
{
	if (enter)
		amdgpu_gfx_rlc_enter_safe_mode(adev);
	else
		amdgpu_gfx_rlc_exit_safe_mode(adev);

	if (adev->gfx.funcs->update_perfmon_mgcg)
		adev->gfx.funcs->update_perfmon_mgcg(adev, !enter);

	return 0;
}

static const struct amdgpu_asic_funcs soc21_asic_funcs =
{
	.read_disabled_bios = &soc21_read_disabled_bios,
	.read_bios_from_rom = &amdgpu_soc15_read_bios_from_rom,
	.read_register = &soc21_read_register,
	.reset = &soc21_asic_reset,
	.reset_method = &soc21_asic_reset_method,
	.set_vga_state = &soc21_vga_set_state,
	.get_xclk = &soc21_get_xclk,
	.set_uvd_clocks = &soc21_set_uvd_clocks,
	.set_vce_clocks = &soc21_set_vce_clocks,
	.get_config_memsize = &soc21_get_config_memsize,
	.init_doorbell_index = &soc21_init_doorbell_index,
	.need_full_reset = &soc21_need_full_reset,
	.need_reset_on_init = &soc21_need_reset_on_init,
	.get_pcie_replay_count = &soc21_get_pcie_replay_count,
	.supports_baco = &amdgpu_dpm_is_baco_supported,
	.pre_asic_init = &soc21_pre_asic_init,
	.query_video_codecs = &soc21_query_video_codecs,
	.update_umd_stable_pstate = &soc21_update_umd_stable_pstate,
};

static int soc21_common_early_init(void *handle)
{
#define MMIO_REG_HOLE_OFFSET (0x80000 - PAGE_SIZE)
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->rmmio_remap.reg_offset = MMIO_REG_HOLE_OFFSET;
	adev->rmmio_remap.bus_addr = adev->rmmio_base + MMIO_REG_HOLE_OFFSET;
	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &soc21_pcie_rreg;
	adev->pcie_wreg = &soc21_pcie_wreg;
	adev->pcie_rreg64 = &soc21_pcie_rreg64;
	adev->pcie_wreg64 = &soc21_pcie_wreg64;
	adev->pciep_rreg = amdgpu_device_pcie_port_rreg;
	adev->pciep_wreg = amdgpu_device_pcie_port_wreg;

	/* TODO: will add them during VCN v2 implementation */
	adev->uvd_ctx_rreg = NULL;
	adev->uvd_ctx_wreg = NULL;

	adev->didt_rreg = &soc21_didt_rreg;
	adev->didt_wreg = &soc21_didt_wreg;

	adev->asic_funcs = &soc21_asic_funcs;

	adev->rev_id = soc21_get_rev_id(adev);
	adev->external_rev_id = 0xff;
	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(11, 0, 0):
		adev->cg_flags = AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
#if 0
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
#endif
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_GFX_PERF_CLK |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_HDP_SD;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_ATHUB |
			AMD_PG_SUPPORT_MMHUB;
		adev->external_rev_id = adev->rev_id + 0x1; // TODO: need update
		break;
	case IP_VERSION(11, 0, 2):
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_HDP_SD;
		adev->pg_flags =
			AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_ATHUB |
			AMD_PG_SUPPORT_MMHUB;
		adev->external_rev_id = adev->rev_id + 0x10;
		break;
	case IP_VERSION(11, 0, 1):
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_GFX_PERF_CLK |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG;
		adev->pg_flags =
			AMD_PG_SUPPORT_GFX_PG |
			AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG;
		adev->external_rev_id = adev->rev_id + 0x1;
		break;
	case IP_VERSION(11, 0, 3):
		adev->cg_flags = AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_GFX_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG;
		adev->external_rev_id = adev->rev_id + 0x20;
		break;
	case IP_VERSION(11, 0, 4):
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_GFX_PERF_CLK |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_BIF_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_GFX_PG |
			AMD_PG_SUPPORT_JPEG;
		adev->external_rev_id = adev->rev_id + 0x1;
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

static int soc21_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_get_irq(adev);

	return 0;
}

static int soc21_common_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_add_irq_id(adev);

	return 0;
}

static int soc21_common_sw_fini(void *handle)
{
	return 0;
}

static int soc21_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* enable pcie gen2/3 link */
	soc21_pcie_gen3_enable(adev);
	/* enable aspm */
	soc21_program_aspm(adev);
	/* setup nbio registers */
	adev->nbio.funcs->init_registers(adev);
	/* remap HDP registers to a hole in mmio space,
	 * for the purpose of expose those registers
	 * to process space
	 */
	if (adev->nbio.funcs->remap_hdp_registers)
		adev->nbio.funcs->remap_hdp_registers(adev);
	/* enable the doorbell aperture */
	soc21_enable_doorbell_aperture(adev, true);

	return 0;
}

static int soc21_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* disable the doorbell aperture */
	soc21_enable_doorbell_aperture(adev, false);

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_put_irq(adev);

	return 0;
}

static int soc21_common_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return soc21_common_hw_fini(adev);
}

static int soc21_common_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return soc21_common_hw_init(adev);
}

static bool soc21_common_is_idle(void *handle)
{
	return true;
}

static int soc21_common_wait_for_idle(void *handle)
{
	return 0;
}

static int soc21_common_soft_reset(void *handle)
{
	return 0;
}

static int soc21_common_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->ip_versions[NBIO_HWIP][0]) {
	case IP_VERSION(4, 3, 0):
	case IP_VERSION(4, 3, 1):
	case IP_VERSION(7, 7, 0):
		adev->nbio.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->nbio.funcs->update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		adev->hdp.funcs->update_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}
	return 0;
}

static int soc21_common_set_powergating_state(void *handle,
					   enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->ip_versions[LSDMA_HWIP][0]) {
	case IP_VERSION(6, 0, 0):
	case IP_VERSION(6, 0, 2):
		adev->lsdma.funcs->update_memory_power_gating(adev,
				state == AMD_PG_STATE_GATE);
		break;
	default:
		break;
	}

	return 0;
}

static void soc21_common_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->nbio.funcs->get_clockgating_state(adev, flags);

	adev->hdp.funcs->get_clock_gating_state(adev, flags);

	return;
}

static const struct amd_ip_funcs soc21_common_ip_funcs = {
	.name = "soc21_common",
	.early_init = soc21_common_early_init,
	.late_init = soc21_common_late_init,
	.sw_init = soc21_common_sw_init,
	.sw_fini = soc21_common_sw_fini,
	.hw_init = soc21_common_hw_init,
	.hw_fini = soc21_common_hw_fini,
	.suspend = soc21_common_suspend,
	.resume = soc21_common_resume,
	.is_idle = soc21_common_is_idle,
	.wait_for_idle = soc21_common_wait_for_idle,
	.soft_reset = soc21_common_soft_reset,
	.set_clockgating_state = soc21_common_set_clockgating_state,
	.set_powergating_state = soc21_common_set_powergating_state,
	.get_clockgating_state = soc21_common_get_clockgating_state,
};

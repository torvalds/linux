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

#include <drm/amdgpu_drm.h>

#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_ucode.h"
#include "amdgpu_psp.h"
#include "atom.h"
#include "amd_pcie.h"

#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "mp/mp_11_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "gmc_v10_0.h"
#include "gfxhub_v2_0.h"
#include "mmhub_v2_0.h"
#include "nbio_v2_3.h"
#include "nbio_v7_2.h"
#include "hdp_v5_0.h"
#include "nv.h"
#include "navi10_ih.h"
#include "gfx_v10_0.h"
#include "sdma_v5_0.h"
#include "sdma_v5_2.h"
#include "vcn_v2_0.h"
#include "jpeg_v2_0.h"
#include "vcn_v3_0.h"
#include "jpeg_v3_0.h"
#include "amdgpu_vkms.h"
#include "mes_v10_1.h"
#include "mxgpu_nv.h"
#include "smuio_v11_0.h"
#include "smuio_v11_0_6.h"

static const struct amd_ip_funcs nv_common_ip_funcs;

/* Navi */
static const struct amdgpu_video_codec_info nv_video_codecs_encode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 2304, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 4096, 2304, 0)},
};

static const struct amdgpu_video_codecs nv_video_codecs_encode =
{
	.codec_count = ARRAY_SIZE(nv_video_codecs_encode_array),
	.codec_array = nv_video_codecs_encode_array,
};

/* Navi1x */
static const struct amdgpu_video_codec_info nv_video_codecs_decode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4906, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4906, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4906, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4906, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs nv_video_codecs_decode =
{
	.codec_count = ARRAY_SIZE(nv_video_codecs_decode_array),
	.codec_array = nv_video_codecs_decode_array,
};

/* Sienna Cichlid */
static const struct amdgpu_video_codec_info sc_video_codecs_decode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4906, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4906, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4906, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4906, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs sc_video_codecs_decode =
{
	.codec_count = ARRAY_SIZE(sc_video_codecs_decode_array),
	.codec_array = sc_video_codecs_decode_array,
};

/* SRIOV Sienna Cichlid, not const since data is controlled by host */
static struct amdgpu_video_codec_info sriov_sc_video_codecs_encode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 2304, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 4096, 2304, 0)},
};

static struct amdgpu_video_codec_info sriov_sc_video_codecs_decode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4906, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4906, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4906, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4906, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static struct amdgpu_video_codecs sriov_sc_video_codecs_encode =
{
	.codec_count = ARRAY_SIZE(sriov_sc_video_codecs_encode_array),
	.codec_array = sriov_sc_video_codecs_encode_array,
};

static struct amdgpu_video_codecs sriov_sc_video_codecs_decode =
{
	.codec_count = ARRAY_SIZE(sriov_sc_video_codecs_decode_array),
	.codec_array = sriov_sc_video_codecs_decode_array,
};

/* Beige Goby*/
static const struct amdgpu_video_codec_info bg_video_codecs_decode_array[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4906, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs bg_video_codecs_decode = {
	.codec_count = ARRAY_SIZE(bg_video_codecs_decode_array),
	.codec_array = bg_video_codecs_decode_array,
};

static const struct amdgpu_video_codecs bg_video_codecs_encode = {
	.codec_count = 0,
	.codec_array = NULL,
};

/* Yellow Carp*/
static const struct amdgpu_video_codec_info yc_video_codecs_decode_array[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4906, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs yc_video_codecs_decode = {
	.codec_count = ARRAY_SIZE(yc_video_codecs_decode_array),
	.codec_array = yc_video_codecs_decode_array,
};

static int nv_query_video_codecs(struct amdgpu_device *adev, bool encode,
				 const struct amdgpu_video_codecs **codecs)
{
	switch (adev->asic_type) {
	case CHIP_SIENNA_CICHLID:
		if (amdgpu_sriov_vf(adev)) {
			if (encode)
				*codecs = &sriov_sc_video_codecs_encode;
			else
				*codecs = &sriov_sc_video_codecs_decode;
		} else {
			if (encode)
				*codecs = &nv_video_codecs_encode;
			else
				*codecs = &sc_video_codecs_decode;
		}
		return 0;
	case CHIP_NAVY_FLOUNDER:
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_VANGOGH:
		if (encode)
			*codecs = &nv_video_codecs_encode;
		else
			*codecs = &sc_video_codecs_decode;
		return 0;
	case CHIP_YELLOW_CARP:
		if (encode)
			*codecs = &nv_video_codecs_encode;
		else
			*codecs = &yc_video_codecs_decode;
		return 0;
	case CHIP_BEIGE_GOBY:
		if (encode)
			*codecs = &bg_video_codecs_encode;
		else
			*codecs = &bg_video_codecs_decode;
		return 0;
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		if (encode)
			*codecs = &nv_video_codecs_encode;
		else
			*codecs = &nv_video_codecs_decode;
		return 0;
	default:
		return -EINVAL;
	}
}

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

static u32 nv_pcie_port_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;
	address = adev->nbio.funcs->get_pcie_port_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_port_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, reg * 4);
	(void)RREG32(address);
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static void nv_pcie_wreg64(struct amdgpu_device *adev, u32 reg, u64 v)
{
	unsigned long address, data;

	address = adev->nbio.funcs->get_pcie_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_data_offset(adev);

	amdgpu_device_indirect_wreg64(adev, address, data, reg, v);
}

static void nv_pcie_port_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = adev->nbio.funcs->get_pcie_port_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_port_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, reg * 4);
	(void)RREG32(address);
	WREG32(data, v);
	(void)RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
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

	WREG32_SOC15(GC, 0, mmGRBM_GFX_CNTL, grbm_gfx_cntl);
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
	u32 rom_index_offset, rom_data_offset;

	if (bios == NULL)
		return false;
	if (length_bytes == 0)
		return false;
	/* APU vbios image is part of sbios image */
	if (adev->flags & AMD_IS_APU)
		return false;

	dw_ptr = (u32 *)bios;
	length_dw = ALIGN(length_bytes, 4) / 4;

	rom_index_offset =
		adev->smuio.funcs->get_rom_index_offset(adev);
	rom_data_offset =
		adev->smuio.funcs->get_rom_data_offset(adev);

	/* set rom index to 0 */
	WREG32(rom_index_offset, 0);
	/* read out the rom data */
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(rom_data_offset);

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

static int nv_asic_mode2_reset(struct amdgpu_device *adev)
{
	u32 i;
	int ret = 0;

	amdgpu_atombios_scratch_regs_engine_hung(adev, true);

	/* disable BM */
	pci_clear_master(adev->pdev);

	amdgpu_device_cache_pci_state(adev->pdev);

	ret = amdgpu_dpm_mode2_reset(adev);
	if (ret)
		dev_err(adev->dev, "GPU mode2 reset failed\n");

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

static enum amd_reset_method
nv_asic_reset_method(struct amdgpu_device *adev)
{
	if (amdgpu_reset_method == AMD_RESET_METHOD_MODE1 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_MODE2 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_BACO ||
	    amdgpu_reset_method == AMD_RESET_METHOD_PCI)
		return amdgpu_reset_method;

	if (amdgpu_reset_method != -1)
		dev_warn(adev->dev, "Specified reset method:%d isn't supported, using AUTO instead.\n",
				  amdgpu_reset_method);

	switch (adev->asic_type) {
	case CHIP_VANGOGH:
	case CHIP_YELLOW_CARP:
		return AMD_RESET_METHOD_MODE2;
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_BEIGE_GOBY:
		return AMD_RESET_METHOD_MODE1;
	default:
		if (amdgpu_dpm_is_baco_supported(adev))
			return AMD_RESET_METHOD_BACO;
		else
			return AMD_RESET_METHOD_MODE1;
	}
}

static int nv_asic_reset(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (nv_asic_reset_method(adev)) {
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
		ret = nv_asic_mode2_reset(adev);
		break;
	default:
		dev_info(adev->dev, "MODE1 reset\n");
		ret = amdgpu_device_mode1_reset(adev);
		break;
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
	if (!amdgpu_device_should_use_aspm(adev))
		return;

	if (!(adev->flags & AMD_IS_APU) &&
	    (adev->nbio.funcs->program_aspm))
		adev->nbio.funcs->program_aspm(adev);

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

static bool nv_is_headless_sku(struct pci_dev *pdev)
{
	if ((pdev->device == 0x731E &&
	    (pdev->revision == 0xC6 || pdev->revision == 0xC7)) ||
	    (pdev->device == 0x7340 && pdev->revision == 0xC9)  ||
	    (pdev->device == 0x7360 && pdev->revision == 0xC7))
		return true;
	return false;
}

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

		amdgpu_discovery_harvest_ip(adev);
		if (nv_is_headless_sku(adev->pdev)) {
			adev->harvest_ip_mask |= AMD_HARVEST_IP_VCN_MASK;
			adev->harvest_ip_mask |= AMD_HARVEST_IP_JPEG_MASK;
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
	case CHIP_DIMGREY_CAVEFISH:
		dimgrey_cavefish_reg_base_init(adev);
		break;
	case CHIP_BEIGE_GOBY:
		beige_goby_reg_base_init(adev);
		break;
	case CHIP_YELLOW_CARP:
		yellow_carp_reg_base_init(adev);
		break;
	case CHIP_CYAN_SKILLFISH:
		cyan_skillfish_reg_base_init(adev);
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

	if (adev->asic_type == CHIP_CYAN_SKILLFISH) {
		adev->nbio.funcs = &nbio_v2_3_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v2_3_hdp_flush_reg;
	} else if (adev->flags & AMD_IS_APU) {
		adev->nbio.funcs = &nbio_v7_2_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v7_2_hdp_flush_reg;
	} else {
		adev->nbio.funcs = &nbio_v2_3_funcs;
		adev->nbio.hdp_flush_reg = &nbio_v2_3_hdp_flush_reg;
	}
	adev->hdp.funcs = &hdp_v5_0_funcs;

	if (adev->asic_type >= CHIP_SIENNA_CICHLID)
		adev->smuio.funcs = &smuio_v11_0_6_funcs;
	else
		adev->smuio.funcs = &smuio_v11_0_funcs;

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
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
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
		if (!amdgpu_sriov_vf(adev)) {
			amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		} else {
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
			amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		}
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
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
		if (!amdgpu_sriov_vf(adev)) {
			amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
				amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		} else {
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
				amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
			amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		}
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP &&
		    is_support_sw_smu(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
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
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
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
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &jpeg_v3_0_ip_block);
		break;
	case CHIP_DIMGREY_CAVEFISH:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP &&
		    is_support_sw_smu(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
                else if (amdgpu_device_has_dc_support(adev))
                        amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &jpeg_v3_0_ip_block);
		break;
	case CHIP_BEIGE_GOBY:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v11_0_ip_block);
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP &&
		    is_support_sw_smu(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT &&
		    is_support_sw_smu(adev))
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
		break;
	case CHIP_YELLOW_CARP:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
			amdgpu_device_ip_block_add(adev, &psp_v13_0_ip_block);
		amdgpu_device_ip_block_add(adev, &smu_v13_0_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_2_ip_block);
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
#if defined(CONFIG_DRM_AMD_DC)
		else if (amdgpu_device_has_dc_support(adev))
			amdgpu_device_ip_block_add(adev, &dm_ip_block);
#endif
		amdgpu_device_ip_block_add(adev, &vcn_v3_0_ip_block);
		amdgpu_device_ip_block_add(adev, &jpeg_v3_0_ip_block);
		break;
	case CHIP_CYAN_SKILLFISH:
		amdgpu_device_ip_block_add(adev, &nv_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &navi10_ih_ip_block);
		if (adev->apu_flags & AMD_APU_IS_CYAN_SKILLFISH2) {
			if (likely(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
				amdgpu_device_ip_block_add(adev, &psp_v11_0_8_ip_block);
			amdgpu_device_ip_block_add(adev, &smu_v11_0_ip_block);
		}
		if (adev->enable_virtual_display || amdgpu_sriov_vf(adev))
			amdgpu_device_ip_block_add(adev, &amdgpu_vkms_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v10_0_ip_block);
		amdgpu_device_ip_block_add(adev, &sdma_v5_0_ip_block);
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

static int nv_update_umd_stable_pstate(struct amdgpu_device *adev,
				       bool enter)
{
	if (enter)
		amdgpu_gfx_rlc_enter_safe_mode(adev);
	else
		amdgpu_gfx_rlc_exit_safe_mode(adev);

	if (adev->gfx.funcs->update_perfmon_mgcg)
		adev->gfx.funcs->update_perfmon_mgcg(adev, !enter);

	if (!(adev->flags & AMD_IS_APU) &&
	    (adev->nbio.funcs->enable_aspm))
		adev->nbio.funcs->enable_aspm(adev, !enter);

	return 0;
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
	.init_doorbell_index = &nv_init_doorbell_index,
	.need_full_reset = &nv_need_full_reset,
	.need_reset_on_init = &nv_need_reset_on_init,
	.get_pcie_replay_count = &nv_get_pcie_replay_count,
	.supports_baco = &amdgpu_dpm_is_baco_supported,
	.pre_asic_init = &nv_pre_asic_init,
	.update_umd_stable_pstate = &nv_update_umd_stable_pstate,
	.query_video_codecs = &nv_query_video_codecs,
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
	adev->pciep_rreg = &nv_pcie_port_rreg;
	adev->pciep_wreg = &nv_pcie_port_wreg;

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
			AMD_CG_SUPPORT_GFX_CGLS |
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
			AMD_CG_SUPPORT_GFX_CGLS |
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
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_JPEG_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_GFX_PG |
			AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG;
		if (adev->apu_flags & AMD_APU_IS_VANGOGH)
			adev->external_rev_id = adev->rev_id + 0x01;
		break;
	case CHIP_DIMGREY_CAVEFISH:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
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
		adev->external_rev_id = adev->rev_id + 0x3c;
		break;
	case CHIP_BEIGE_GOBY:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_VCN_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_ATHUB |
			AMD_PG_SUPPORT_MMHUB;
		adev->external_rev_id = adev->rev_id + 0x46;
		break;
	case CHIP_YELLOW_CARP:
		adev->cg_flags = AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_HDP_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_IH_CG |
			AMD_CG_SUPPORT_VCN_MGCG |
			AMD_CG_SUPPORT_JPEG_MGCG;
		adev->pg_flags = AMD_PG_SUPPORT_GFX_PG |
			AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG;
		if (adev->pdev->device == 0x1681)
			adev->external_rev_id = 0x20;
		else
			adev->external_rev_id = adev->rev_id + 0x01;
		break;
	case CHIP_CYAN_SKILLFISH:
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x82;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (adev->harvest_ip_mask & AMD_HARVEST_IP_VCN_MASK)
		adev->pg_flags &= ~(AMD_PG_SUPPORT_VCN |
				    AMD_PG_SUPPORT_VCN_DPG |
				    AMD_PG_SUPPORT_JPEG);

	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_init_setting(adev);
		xgpu_nv_mailbox_set_irq_funcs(adev);
	}

	return 0;
}

static int nv_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev)) {
		xgpu_nv_mailbox_get_irq(adev);
		amdgpu_virt_update_sriov_video_codec(adev,
				sriov_sc_video_codecs_encode_array, ARRAY_SIZE(sriov_sc_video_codecs_encode_array),
				sriov_sc_video_codecs_decode_array, ARRAY_SIZE(sriov_sc_video_codecs_decode_array));
	}

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

	if (adev->nbio.funcs->apply_lc_spc_mode_wa)
		adev->nbio.funcs->apply_lc_spc_mode_wa(adev);

	if (adev->nbio.funcs->apply_l1_link_width_reconfig_wa)
		adev->nbio.funcs->apply_l1_link_width_reconfig_wa(adev);

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
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_BEIGE_GOBY:
		adev->nbio.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->nbio.funcs->update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		adev->hdp.funcs->update_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->smuio.funcs->update_rom_clock_gating(adev,
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

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	adev->nbio.funcs->get_clockgating_state(adev, flags);

	adev->hdp.funcs->get_clock_gating_state(adev, flags);

	adev->smuio.funcs->get_clock_gating_state(adev, flags);

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

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
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4096, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4096, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4096, 4)},
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
static const struct amdgpu_video_codec_info sc_video_codecs_encode_array[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 2160, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 7680, 4352, 0)},
};

static const struct amdgpu_video_codecs sc_video_codecs_encode = {
	.codec_count = ARRAY_SIZE(sc_video_codecs_encode_array),
	.codec_array = sc_video_codecs_encode_array,
};

static const struct amdgpu_video_codec_info sc_video_codecs_decode_array_vcn0[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4096, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4096, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4096, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codec_info sc_video_codecs_decode_array_vcn1[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4096, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4096, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4096, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs sc_video_codecs_decode_vcn0 =
{
	.codec_count = ARRAY_SIZE(sc_video_codecs_decode_array_vcn0),
	.codec_array = sc_video_codecs_decode_array_vcn0,
};

static const struct amdgpu_video_codecs sc_video_codecs_decode_vcn1 =
{
	.codec_count = ARRAY_SIZE(sc_video_codecs_decode_array_vcn1),
	.codec_array = sc_video_codecs_decode_array_vcn1,
};

/* SRIOV Sienna Cichlid, not const since data is controlled by host */
static struct amdgpu_video_codec_info sriov_sc_video_codecs_encode_array[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 2160, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 7680, 4352, 0)},
};

static struct amdgpu_video_codec_info sriov_sc_video_codecs_decode_array_vcn0[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4096, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4096, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4096, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static struct amdgpu_video_codec_info sriov_sc_video_codecs_decode_array_vcn1[] =
{
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG2, 4096, 4096, 3)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4, 4096, 4096, 5)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VC1, 4096, 4096, 4)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
};

static struct amdgpu_video_codecs sriov_sc_video_codecs_encode =
{
	.codec_count = ARRAY_SIZE(sriov_sc_video_codecs_encode_array),
	.codec_array = sriov_sc_video_codecs_encode_array,
};

static struct amdgpu_video_codecs sriov_sc_video_codecs_decode_vcn0 =
{
	.codec_count = ARRAY_SIZE(sriov_sc_video_codecs_decode_array_vcn0),
	.codec_array = sriov_sc_video_codecs_decode_array_vcn0,
};

static struct amdgpu_video_codecs sriov_sc_video_codecs_decode_vcn1 =
{
	.codec_count = ARRAY_SIZE(sriov_sc_video_codecs_decode_array_vcn1),
	.codec_array = sriov_sc_video_codecs_decode_array_vcn1,
};

/* Beige Goby*/
static const struct amdgpu_video_codec_info bg_video_codecs_decode_array[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
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
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
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
	if (adev->vcn.num_vcn_inst == hweight8(adev->vcn.harvest_config))
		return -EINVAL;

	switch (adev->ip_versions[UVD_HWIP][0]) {
	case IP_VERSION(3, 0, 0):
	case IP_VERSION(3, 0, 64):
	case IP_VERSION(3, 0, 192):
		if (amdgpu_sriov_vf(adev)) {
			if (adev->vcn.harvest_config & AMDGPU_VCN_HARVEST_VCN0) {
				if (encode)
					*codecs = &sriov_sc_video_codecs_encode;
				else
					*codecs = &sriov_sc_video_codecs_decode_vcn1;
			} else {
				if (encode)
					*codecs = &sriov_sc_video_codecs_encode;
				else
					*codecs = &sriov_sc_video_codecs_decode_vcn0;
			}
		} else {
			if (adev->vcn.harvest_config & AMDGPU_VCN_HARVEST_VCN0) {
				if (encode)
					*codecs = &sc_video_codecs_encode;
				else
					*codecs = &sc_video_codecs_decode_vcn1;
			} else {
				if (encode)
					*codecs = &sc_video_codecs_encode;
				else
					*codecs = &sc_video_codecs_decode_vcn0;
			}
		}
		return 0;
	case IP_VERSION(3, 0, 16):
	case IP_VERSION(3, 0, 2):
		if (encode)
			*codecs = &sc_video_codecs_encode;
		else
			*codecs = &sc_video_codecs_decode_vcn0;
		return 0;
	case IP_VERSION(3, 1, 1):
	case IP_VERSION(3, 1, 2):
		if (encode)
			*codecs = &sc_video_codecs_encode;
		else
			*codecs = &yc_video_codecs_decode;
		return 0;
	case IP_VERSION(3, 0, 33):
		if (encode)
			*codecs = &bg_video_codecs_encode;
		else
			*codecs = &bg_video_codecs_decode;
		return 0;
	case IP_VERSION(2, 0, 0):
	case IP_VERSION(2, 0, 2):
		if (encode)
			*codecs = &nv_video_codecs_encode;
		else
			*codecs = &nv_video_codecs_decode;
		return 0;
	default:
		return -EINVAL;
	}
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

static bool nv_read_disabled_bios(struct amdgpu_device *adev)
{
	/* todo */
	return false;
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
		amdgpu_gfx_select_se_sh(adev, se_num, sh_num, 0xffffffff, 0);

	val = RREG32(reg_offset);

	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
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
		if (!adev->reg_offset[en->hwip][en->inst])
			continue;
		else if (reg_offset != (adev->reg_offset[en->hwip][en->inst][en->seg]
					+ en->reg_offset))
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

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 3):
	case IP_VERSION(13, 0, 5):
	case IP_VERSION(13, 0, 8):
		return AMD_RESET_METHOD_MODE2;
	case IP_VERSION(11, 0, 7):
	case IP_VERSION(11, 0, 11):
	case IP_VERSION(11, 0, 12):
	case IP_VERSION(11, 0, 13):
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

static void nv_program_aspm(struct amdgpu_device *adev)
{
	if (!amdgpu_device_should_use_aspm(adev) || !amdgpu_device_aspm_support_quirk())
		return;

	if (!(adev->flags & AMD_IS_APU) &&
	    (adev->nbio.funcs->program_aspm))
		adev->nbio.funcs->program_aspm(adev);

}

const struct amdgpu_ip_block_version nv_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &nv_common_ip_funcs,
};

void nv_set_virt_ops(struct amdgpu_device *adev)
{
	adev->virt.ops = &xgpu_nv_virt_ops;
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
	adev->doorbell_index.gfx_userqueue_start =
		AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_START;
	adev->doorbell_index.gfx_userqueue_end =
		AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_END;
	adev->doorbell_index.mes_ring0 = AMDGPU_NAVI10_DOORBELL_MES_RING0;
	adev->doorbell_index.mes_ring1 = AMDGPU_NAVI10_DOORBELL_MES_RING1;
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
		amdgpu_gfx_rlc_enter_safe_mode(adev, 0);
	else
		amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	if (adev->gfx.funcs->update_perfmon_mgcg)
		adev->gfx.funcs->update_perfmon_mgcg(adev, !enter);

	if (!(adev->flags & AMD_IS_APU) &&
	    (adev->nbio.funcs->enable_aspm) &&
	     amdgpu_device_should_use_aspm(adev))
		adev->nbio.funcs->enable_aspm(adev, !enter);

	return 0;
}

static const struct amdgpu_asic_funcs nv_asic_funcs =
{
	.read_disabled_bios = &nv_read_disabled_bios,
	.read_bios_from_rom = &amdgpu_soc15_read_bios_from_rom,
	.read_register = &nv_read_register,
	.reset = &nv_asic_reset,
	.reset_method = &nv_asic_reset_method,
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

	if (!amdgpu_sriov_vf(adev)) {
		adev->rmmio_remap.reg_offset = MMIO_REG_HOLE_OFFSET;
		adev->rmmio_remap.bus_addr = adev->rmmio_base + MMIO_REG_HOLE_OFFSET;
	}
	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &amdgpu_device_indirect_rreg;
	adev->pcie_wreg = &amdgpu_device_indirect_wreg;
	adev->pcie_rreg64 = &amdgpu_device_indirect_rreg64;
	adev->pcie_wreg64 = &amdgpu_device_indirect_wreg64;
	adev->pciep_rreg = amdgpu_device_pcie_port_rreg;
	adev->pciep_wreg = amdgpu_device_pcie_port_wreg;

	/* TODO: will add them during VCN v2 implementation */
	adev->uvd_ctx_rreg = NULL;
	adev->uvd_ctx_wreg = NULL;

	adev->didt_rreg = &nv_didt_rreg;
	adev->didt_wreg = &nv_didt_wreg;

	adev->asic_funcs = &nv_asic_funcs;

	adev->rev_id = amdgpu_device_get_rev_id(adev);
	adev->external_rev_id = 0xff;
	/* TODO: split the GC and PG flags based on the relevant IP version for which
	 * they are relevant.
	 */
	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(10, 1, 10):
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
	case IP_VERSION(10, 1, 1):
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
	case IP_VERSION(10, 1, 2):
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
	case IP_VERSION(10, 3, 0):
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
	case IP_VERSION(10, 3, 2):
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
	case IP_VERSION(10, 3, 1):
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
	case IP_VERSION(10, 3, 4):
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
	case IP_VERSION(10, 3, 5):
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
	case IP_VERSION(10, 3, 3):
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
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x82;
		break;
	case IP_VERSION(10, 3, 6):
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
		adev->external_rev_id = adev->rev_id + 0x01;
		break;
	case IP_VERSION(10, 3, 7):
		adev->cg_flags =  AMD_CG_SUPPORT_GFX_MGCG |
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
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_VCN_DPG |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_GFX_PG;
		adev->external_rev_id = adev->rev_id + 0x01;
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
		if (adev->vcn.harvest_config & AMDGPU_VCN_HARVEST_VCN0) {
			amdgpu_virt_update_sriov_video_codec(adev,
							     sriov_sc_video_codecs_encode_array,
							     ARRAY_SIZE(sriov_sc_video_codecs_encode_array),
							     sriov_sc_video_codecs_decode_array_vcn1,
							     ARRAY_SIZE(sriov_sc_video_codecs_decode_array_vcn1));
		} else {
			amdgpu_virt_update_sriov_video_codec(adev,
							     sriov_sc_video_codecs_encode_array,
							     ARRAY_SIZE(sriov_sc_video_codecs_encode_array),
							     sriov_sc_video_codecs_decode_array_vcn0,
							     ARRAY_SIZE(sriov_sc_video_codecs_decode_array_vcn0));
		}
	}

	/* Enable selfring doorbell aperture late because doorbell BAR
	 * aperture will change if resize BAR successfully in gmc sw_init.
	 */
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, true);

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

	/* enable aspm */
	nv_program_aspm(adev);
	/* setup nbio registers */
	adev->nbio.funcs->init_registers(adev);
	/* remap HDP registers to a hole in mmio space,
	 * for the purpose of expose those registers
	 * to process space
	 */
	if (adev->nbio.funcs->remap_hdp_registers && !amdgpu_sriov_vf(adev))
		adev->nbio.funcs->remap_hdp_registers(adev);
	/* enable the doorbell aperture */
	adev->nbio.funcs->enable_doorbell_aperture(adev, true);

	return 0;
}

static int nv_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* Disable the doorbell aperture and selfring doorbell aperture
	 * separately in hw_fini because nv_enable_doorbell_aperture
	 * has been removed and there is no need to delay disabling
	 * selfring doorbell.
	 */
	adev->nbio.funcs->enable_doorbell_aperture(adev, false);
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, false);

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

	switch (adev->ip_versions[NBIO_HWIP][0]) {
	case IP_VERSION(2, 3, 0):
	case IP_VERSION(2, 3, 1):
	case IP_VERSION(2, 3, 2):
	case IP_VERSION(3, 3, 0):
	case IP_VERSION(3, 3, 1):
	case IP_VERSION(3, 3, 2):
	case IP_VERSION(3, 3, 3):
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

static void nv_common_get_clockgating_state(void *handle, u64 *flags)
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

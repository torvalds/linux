/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "amdgpu_discovery.h"
#include "soc15.h"
#include "soc15_common.h"
#include "soc_v1_0.h"
#include "amdgpu_ip.h"
#include "amdgpu_imu.h"
#include "gfxhub_v12_1.h"
#include "sdma_v7_1.h"
#include "gfx_v12_1.h"
#include "amdgpu_video_codecs.h"
#include "amdgpu_reset.h"

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "mp/mp_15_0_8_offset.h"

#define XCC_REG_RANGE_0_LOW  0x1260     /* XCC gfxdec0 lower Bound */
#define XCC_REG_RANGE_0_HIGH 0x3C00     /* XCC gfxdec0 upper Bound */
#define XCC_REG_RANGE_1_LOW  0xA000     /* XCC gfxdec1 lower Bound */
#define XCC_REG_RANGE_1_HIGH 0x10000    /* XCC gfxdec1 upper Bound */
#define NORMALIZE_XCC_REG_OFFSET(offset) \
	(offset & 0xFFFF)

#define MID1_REG_RANGE_0_LOW  0x40000
#define MID1_REG_RANGE_0_HIGH 0x80000
#define NORMALIZE_MID_REG_OFFSET(offset) \
		(offset & 0x3FFFF)

static const struct amdgpu_video_codecs vcn_5_0_2_video_codecs_encode_vcn0 = {
	.codec_count = 0,
	.codec_array = NULL,
};

static const struct amdgpu_video_codec_info vcn_5_0_2_video_codecs_decode_array_vcn0[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 16384, 16384, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs vcn_5_0_2_video_codecs_decode_vcn0 = {
	.codec_count = ARRAY_SIZE(vcn_5_0_2_video_codecs_decode_array_vcn0),
	.codec_array = vcn_5_0_2_video_codecs_decode_array_vcn0,
};

static int soc_v1_0_query_video_codecs(struct amdgpu_device *adev, bool encode,
					const struct amdgpu_video_codecs **codecs)
{
	switch (amdgpu_ip_version(adev, UVD_HWIP, 0)) {
	case IP_VERSION(5, 0, 2):
		if (encode)
			*codecs = &vcn_5_0_2_video_codecs_encode_vcn0;
		else
			*codecs = &vcn_5_0_2_video_codecs_decode_vcn0;
		return 0;
	default:
		return -EINVAL;
	}
}

/* Initialized doorbells for amdgpu including multimedia
 * KFD can use all the rest in 2M doorbell bar */
static void soc_v1_0_doorbell_index_init(struct amdgpu_device *adev)
{
	int i;

	adev->doorbell_index.kiq = AMDGPU_SOC_V1_0_DOORBELL_KIQ_START;

	adev->doorbell_index.mec_ring0 = AMDGPU_SOC_V1_0_DOORBELL_MEC_RING_START;
	adev->doorbell_index.mes_ring0 = AMDGPU_SOC_V1_0_DOORBELL_MES_RING0;
	adev->doorbell_index.mes_ring1 = AMDGPU_SOC_V1_0_DOORBELL_MES_RING1;

	adev->doorbell_index.userqueue_start = AMDGPU_SOC_V1_0_DOORBELL_USERQUEUE_START;
	adev->doorbell_index.userqueue_end = AMDGPU_SOC_V1_0_DOORBELL_USERQUEUE_END;
	adev->doorbell_index.xcc_doorbell_range = AMDGPU_SOC_V1_0_DOORBELL_XCC_RANGE;

	adev->doorbell_index.sdma_doorbell_range = 14;
	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->doorbell_index.sdma_engine[i] =
			AMDGPU_SOC_V1_0_DOORBELL_sDMA_ENGINE_START +
			i * (adev->doorbell_index.sdma_doorbell_range >> 1);

	adev->doorbell_index.ih = AMDGPU_SOC_V1_0_DOORBELL_IH;
	adev->doorbell_index.vcn.vcn_ring0_1 = AMDGPU_SOC_V1_0_DOORBELL_VCN_START;

	adev->doorbell_index.first_non_cp = AMDGPU_SOC_V1_0_DOORBELL_FIRST_NON_CP;
	adev->doorbell_index.last_non_cp = AMDGPU_SOC_V1_0_DOORBELL_LAST_NON_CP;

	adev->doorbell_index.max_assignment = AMDGPU_SOC_V1_0_DOORBELL_MAX_ASSIGNMENT << 1;
}

/* Fixed pattern for upper 32bits smn addressing.
 *   bit[47:40]: Socket ID
 *   bit[39:34]: Die ID
 *   bit[32]: local or remote die in same socket
 * The ext_id is comprised of socket_id and die_id.
 *   ext_id = (socket_id << 6) | (die_id)
*/
u64 soc_v1_0_encode_ext_smn_addressing(int ext_id)
{
	u64 ext_offset;
	int socket_id, die_id;

	/* local die routing for MID0 on local socket */
	if (ext_id == 0)
		return 0;

	die_id = ext_id & 0x3;
	socket_id = (ext_id >> 6) & 0xff;

	/* Initiated from host, accessing to non-MID0 is cross-die traffic */
	if (socket_id == 0)
		ext_offset = ((u64)die_id << 34) | (1ULL << 32);
	else if (socket_id != 0 && die_id != 0)
		ext_offset = ((u64)socket_id << 40) | ((u64)die_id << 34) |
				(3ULL << 32);
	else
		ext_offset = ((u64)socket_id << 40) | (1ULL << 33);

	return ext_offset;
}

static u32 soc_v1_0_get_config_memsize(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_memsize(adev);
}

static u32 soc_v1_0_get_xclk(struct amdgpu_device *adev)
{
	/* 100MHz is the default */
	if (!adev->bios)
		return 10000;
	else
		return adev->clock.spll.reference_freq;
}

void soc_v1_0_grbm_select(struct amdgpu_device *adev,
			  u32 me, u32 pipe,
			  u32 queue, u32 vmid,
			  int xcc_id)
{
	u32 grbm_gfx_cntl = 0;
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, PIPEID, pipe);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, MEID, me);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, VMID, vmid);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, QUEUEID, queue);

	WREG32_SOC15_RLC_SHADOW(GC, xcc_id, regGRBM_GFX_CNTL, grbm_gfx_cntl);
}

static struct soc15_allowed_register_entry soc_v1_0_allowed_read_registers[] = {
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS) },
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS2) },
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS3) },
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE0) },
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE1) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_STAT) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT1) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT2) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT3) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_BUSY_STAT) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_STALLED_STAT1) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_STATUS) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_BUSY_STAT) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_STALLED_STAT1) },
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_STATUS) },
	{ SOC15_REG_ENTRY(GC, 0, regGB_ADDR_CONFIG_1) },
};


static uint32_t soc_v1_0_get_register_value(struct amdgpu_device *adev,
					    bool indexed, u32 se_num,
					    u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		return amdgpu_read_indexed_register(adev, se_num, sh_num, reg_offset);
	} else {
		if (reg_offset == SOC15_REG_OFFSET(GC, 0, regGB_ADDR_CONFIG_1) &&
		    adev->gfx.config.gb_addr_config)
			return adev->gfx.config.gb_addr_config;
		return RREG32(reg_offset);
	}
}

static int soc_v1_0_read_register(struct amdgpu_device *adev,
				  u32 se_num, u32 sh_num,
				  u32 reg_offset, u32 *value)
{
	uint32_t i;
	struct soc15_allowed_register_entry  *en;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(soc_v1_0_allowed_read_registers); i++) {
		en = &soc_v1_0_allowed_read_registers[i];
		if (!adev->reg_offset[en->hwip][en->inst])
			continue;
		else if (reg_offset != (adev->reg_offset[en->hwip][en->inst][en->seg]
					+ en->reg_offset))
			continue;

		*value = soc_v1_0_get_register_value(adev,
				soc_v1_0_allowed_read_registers[i].grbm_indexed,
				se_num, sh_num, reg_offset);
		return 0;
	}
	return -EINVAL;
}

static bool soc_v1_0_need_reset_on_init(struct amdgpu_device *adev)
{

	return false;
}

static enum amd_reset_method
soc_v1_0_asic_reset_method(struct amdgpu_device *adev)
{
	if ((adev->gmc.xgmi.supported && adev->gmc.xgmi.connected_to_cpu) ||
	    (amdgpu_ip_version(adev, MP1_HWIP, 0) == IP_VERSION(15, 0, 8))) {
		if (amdgpu_reset_method != -1)
			dev_warn_once(adev->dev, "Reset override isn't supported, using Mode2 instead.\n");

		return AMD_RESET_METHOD_MODE2;
	}

	return amdgpu_reset_method;
}

static int soc_v1_0_asic_reset(struct amdgpu_device *adev)
{
	switch (soc_v1_0_asic_reset_method(adev)) {
	case AMD_RESET_METHOD_MODE2:
		dev_info(adev->dev, "MODE2 reset\n");
		return amdgpu_dpm_mode2_reset(adev);
	default:
		dev_info(adev->dev, "Invalid reset method Not supported\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * Function returns a pair of (size, offset_within_vram) to
 * tell caller to skip the *reserve_size bytes starting at *offset inside VRAM.
 */
static void soc_v1_0_get_fw_reserved_info(struct amdgpu_device *adev,
					  u64 *reserve_size,
					  u64 *offset)
{
	struct mem_reserved_info umf_info, vram_info;
	u64 vram_base;

	if (amdgpu_discovery_get_mem_reserved_region_by_id(adev,
							   MASTER_DIE_UMF_REGION_ID, &umf_info)) {
		dev_warn(adev->dev,
			 "MASTER_DIE_UMF region not found in discovery\n");
		return;
	}
	/*
	 * If SPECIFIC_PURPOSE_REGION exists and non-zero, use it as the VRAM base.
	 * In multi-die layouts, VRAM may be placed at non-default bases.
	 * Prefer to query SPECIFIC_PURPOSE_REGION to get the vram_base.
	 */
	if (!amdgpu_discovery_get_mem_reserved_region_by_id(adev,
			SPECIFIC_PURPOSE_REGION_ID, &vram_info) &&
	    vram_info.reserved_region_size)
		vram_base = vram_info.reserved_region_start;
	else
		vram_base = adev->gmc.vram_start;
	dev_dbg(adev->dev, "%s: vram_base=0x%llx\n", __func__, vram_base);

	if (umf_info.reserved_region_size == 0 ||
	    umf_info.reserved_region_start < vram_base ||
	    umf_info.reserved_region_start + umf_info.reserved_region_size >
			vram_base + adev->gmc.real_vram_size) {
		dev_warn(adev->dev,
			 "MASTER_DIE_UMF region out of VRAM: start=0x%llx size=0x%llx vram=[0x%llx,+0x%llx)\n",
			 umf_info.reserved_region_start, umf_info.reserved_region_size,
			 vram_base, adev->gmc.real_vram_size);
		return;
	}
	/*
	 * *offset is the distance from the start of VRAM to the start of the
	 * UMF carveout.
	 */
	*reserve_size = umf_info.reserved_region_size;
	*offset = umf_info.reserved_region_start - vram_base;
}

static const struct amdgpu_asic_funcs soc_v1_0_asic_funcs = {
	.read_bios_from_rom = &amdgpu_soc15_read_bios_from_rom,
	.read_register = &soc_v1_0_read_register,
	.get_config_memsize = &soc_v1_0_get_config_memsize,
	.get_xclk = &soc_v1_0_get_xclk,
	.init_doorbell_index = &soc_v1_0_doorbell_index_init,
	.need_reset_on_init = &soc_v1_0_need_reset_on_init,
	.encode_ext_smn_addressing = &soc_v1_0_encode_ext_smn_addressing,
	.reset = soc_v1_0_asic_reset,
	.reset_method = &soc_v1_0_asic_reset_method,
	.query_video_codecs = &soc_v1_0_query_video_codecs,
	.get_fw_reserved_info = &soc_v1_0_get_fw_reserved_info,
};

enum soc_v1_0_external_rev_id {
	SOC_V1_0_MID_A0_AID_A0_XCD_A0 = 0x1,
	SOC_V1_0_MID_A0_AID_A0_XCD_B0 = 0x2,
	SOC_V1_0_MID_A0_AID_A1_XCD_A0 = 0x3,
	SOC_V1_0_MID_A0_AID_A1_XCD_B0 = 0x4,
};

static int soc_v1_0_set_rev_id(struct amdgpu_device *adev)
{
	u16 die_rev_id = 0, xcd_rev_id;
	int r;

	xcd_rev_id = amdgpu_device_get_rev_id(adev);
	r = amdgpu_discovery_get_die_rev_id(adev, &die_rev_id);
	if (r) {
		die_rev_id = 0;
		dev_warn(adev->dev,
			"missing die rev id from ip discovery, assuming 0\n");
	}

	/*
	 * FIXME: XCD revision is not yet populated in die_info[0].die_id by
	 * ASP firmware. Use the PCI revision ID register as a temporary
	 * fallback until firmware support is available.
	 */
	die_rev_id |= xcd_rev_id << SOC_V1_0_DIE_REV_XCD__SHIFT;
	adev->rev_id = die_rev_id;

	/* SOC_V1_0_DIE_REV(mid_rev, aid_rev, xcd_rev) */
	switch (die_rev_id) {
	case SOC_V1_0_DIE_REV(0, 0, 0):
		adev->external_rev_id = SOC_V1_0_MID_A0_AID_A0_XCD_A0;
		break;
	case SOC_V1_0_DIE_REV(0, 0, 1):
		adev->external_rev_id = SOC_V1_0_MID_A0_AID_A0_XCD_B0;
		break;
	case SOC_V1_0_DIE_REV(0, 1, 0):
		adev->external_rev_id = SOC_V1_0_MID_A0_AID_A1_XCD_A0;
		break;
	case SOC_V1_0_DIE_REV(0, 1, 1):
		adev->external_rev_id = SOC_V1_0_MID_A0_AID_A1_XCD_B0;
		break;
	default:
		dev_warn(adev->dev, "unknown die rev id 0x%x\n", die_rev_id);
		break;
	}

	return 0;
}

static int soc_v1_0_common_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	adev->reg.pcie.rreg = &amdgpu_device_indirect_rreg;
	adev->reg.pcie.wreg = &amdgpu_device_indirect_wreg;
	adev->reg.pcie.rreg_ext = &amdgpu_device_indirect_rreg_ext;
	adev->reg.pcie.wreg_ext = &amdgpu_device_indirect_wreg_ext;
	adev->reg.pcie.rreg64 = &amdgpu_device_indirect_rreg64;
	adev->reg.pcie.wreg64 = &amdgpu_device_indirect_wreg64;
	adev->reg.pcie.port_rreg = &amdgpu_device_pcie_port_rreg;
	adev->reg.pcie.port_wreg = &amdgpu_device_pcie_port_wreg;
	adev->reg.pcie.rreg64_ext = &amdgpu_device_indirect_rreg64_ext;
	adev->reg.pcie.wreg64_ext = &amdgpu_device_indirect_wreg64_ext;

	adev->asic_funcs = &soc_v1_0_asic_funcs;

	r = soc_v1_0_set_rev_id(adev);
	if (r)
		return r;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 1, 0):
		adev->cg_flags = 0;
		adev->pg_flags = AMD_PG_SUPPORT_VCN_DPG;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	adev->nbio.funcs->init_registers(adev);

	return 0;
}

static int soc_v1_0_common_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	/* Enable selfring doorbell aperture late because doorbell BAR
	 * aperture will change if resize BAR successfully in gmc sw_init.
	 */
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, true);

	return 0;
}

static int soc_v1_0_common_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	/* enable the doorbell aperture */
	adev->nbio.funcs->enable_doorbell_aperture(adev, true);

	return 0;
}

static int soc_v1_0_common_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->nbio.funcs->enable_doorbell_aperture(adev, false);
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, false);

	return 0;
}

static int soc_v1_0_common_suspend(struct amdgpu_ip_block *ip_block)
{
	return soc_v1_0_common_hw_fini(ip_block);
}

static int soc_v1_0_common_resume(struct amdgpu_ip_block *ip_block)
{
	return soc_v1_0_common_hw_init(ip_block);
}

static int soc_v1_0_common_set_clockgating_state(struct amdgpu_ip_block *ip_block,
						 enum amd_clockgating_state state)
{
	return 0;
}

static int soc_v1_0_common_set_powergating_state(struct amdgpu_ip_block *ip_block,
						 enum amd_powergating_state state)
{
	return 0;
}

static void soc_v1_0_common_get_clockgating_state(struct amdgpu_ip_block *ip_block,
						  u64 *flags)
{
	return;
}

static const struct amd_ip_funcs soc_v1_0_common_ip_funcs = {
	.name = "soc_v1_0_common",
	.early_init = soc_v1_0_common_early_init,
	.late_init = soc_v1_0_common_late_init,
	.hw_init = soc_v1_0_common_hw_init,
	.hw_fini = soc_v1_0_common_hw_fini,
	.suspend = soc_v1_0_common_suspend,
	.resume = soc_v1_0_common_resume,
	.set_clockgating_state = soc_v1_0_common_set_clockgating_state,
	.set_powergating_state = soc_v1_0_common_set_powergating_state,
	.get_clockgating_state = soc_v1_0_common_get_clockgating_state,
};

const struct amdgpu_ip_block_version soc_v1_0_common_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &soc_v1_0_common_ip_funcs,
};

static struct amdgpu_reset_handler *
soc_v1_0_get_reset_handler(struct amdgpu_reset_control *reset_ctl,
			   struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	struct amdgpu_reset_handler *handler;
	enum amd_reset_method method;
	int i;

	method = (reset_context->method == AMD_RESET_METHOD_NONE) ?
		amdgpu_asic_reset_method(adev) :  reset_context->method;
	for_each_handler(i, handler, reset_ctl) {
		if (handler->reset_method == method)
			return handler;
	}

	return NULL;
}

static inline u32 soc_v1_0_get_ip_block_mask(struct amdgpu_device *adev)
{
	u32 ip_block_mask = BIT(AMD_IP_BLOCK_TYPE_GFX) |
				 BIT(AMD_IP_BLOCK_TYPE_MES) |
				 BIT(AMD_IP_BLOCK_TYPE_SDMA) |
				 BIT(AMD_IP_BLOCK_TYPE_IH);

	return ip_block_mask;
}

static int soc_v1_0_mode2_suspend_ip(struct amdgpu_device *adev)
{
	u32 ip_block_mask = soc_v1_0_get_ip_block_mask(adev);
	u32 ip_block;
	int r, i;

	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_UNGATE);

	/* SDMA suspend not required */
	ip_block_mask &= ~BIT(AMD_IP_BLOCK_TYPE_SDMA);
	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		ip_block = BIT(adev->ip_blocks[i].version->type);
		if (!(ip_block_mask & ip_block))
			continue;

		r = amdgpu_ip_block_suspend(&adev->ip_blocks[i]);
		if (r)
			return r;
	}

	return 0;
}

static int
soc_v1_0_mode2_prepare_hwcontext(struct amdgpu_reset_control *reset_ctl,
				 struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	return soc_v1_0_mode2_suspend_ip(adev);
}

static int soc_v1_0_mode2_reset(struct amdgpu_device *adev)
{
	adev->asic_reset_res = amdgpu_dpm_mode2_reset(adev);
	return adev->asic_reset_res;
}

static void soc_v1_0_async_reset(struct work_struct *work)
{
	struct amdgpu_reset_handler *handler;
	struct amdgpu_reset_control *reset_ctl =
		container_of(work, struct amdgpu_reset_control, reset_work);
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	int i;

	for_each_handler(i, handler, reset_ctl) {
		if (handler->reset_method == reset_ctl->active_reset) {
			dev_dbg(adev->dev, "Resetting device\n");
			handler->do_reset(adev);
			break;
		}
	}
}

static int
soc_v1_0_mode2_perform_reset(struct amdgpu_reset_control *reset_ctl,
			     struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	struct list_head *reset_device_list = reset_context->reset_device_list;
	struct amdgpu_device *tmp_adev = NULL;
	int r = 0;

	dev_dbg(adev->dev, "soc_v1_0 perform hw reset\n");

	if (!reset_device_list)
		return -EINVAL;

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		mutex_lock(&tmp_adev->reset_cntl->reset_lock);
		tmp_adev->reset_cntl->active_reset = AMD_RESET_METHOD_MODE2;
	}

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		r = soc_v1_0_mode2_reset(tmp_adev);
		if (r) {
			dev_err(tmp_adev->dev,
				"ASIC reset failed with error, %d for drm dev, %s",
				r, adev_to_drm(tmp_adev)->unique);
			break;
		}
	}

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		mutex_unlock(&tmp_adev->reset_cntl->reset_lock);
		tmp_adev->reset_cntl->active_reset = AMD_RESET_METHOD_NONE;
	}

	return r;
}

static int soc_v1_0_mode2_restore_ip(struct amdgpu_device *adev)
{
	u32 ip_block_mask = soc_v1_0_get_ip_block_mask(adev);
	struct amdgpu_ip_block *ih_block;
	u32 ip_block;
	int i, r;

	if (ip_block_mask & BIT(AMD_IP_BLOCK_TYPE_IH)) {
		ih_block = amdgpu_device_ip_get_ip_block(adev,
							 AMD_IP_BLOCK_TYPE_IH);
		if (unlikely(!ih_block)) {
			dev_err(adev->dev, "Failed to get IH handle\n");
			return -EINVAL;
		}
		r = amdgpu_ip_block_resume(ih_block);
		if (r)
			return r;
	}

	/* IH Block resume completed can be removed from ip_block_mask */
	ip_block_mask &= ~BIT(AMD_IP_BLOCK_TYPE_IH);

	/* Reinit GFXHUB */
	adev->gfxhub.funcs->init(adev);
	r = adev->gfxhub.funcs->gart_enable(adev);
	if (r) {
		dev_err(adev->dev, "GFXHUB gart reenable failed after reset\n");
		return r;
	}

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		ip_block = BIT(adev->ip_blocks[i].version->type);
		if (!(ip_block_mask & ip_block))
			continue;
		r = amdgpu_ip_block_resume(&adev->ip_blocks[i]);
		if (r)
			return r;
	}

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		ip_block = BIT(adev->ip_blocks[i].version->type);
		if (!(ip_block_mask & ip_block))
			continue;

		if (adev->ip_blocks[i].version->funcs->late_init) {
			r = adev->ip_blocks[i].version->funcs->late_init(&adev->ip_blocks[i]);
			if (r) {
				dev_err(adev->dev,
					"late_init of IP block <%s> failed %d after reset\n",
					adev->ip_blocks[i].version->funcs->name,
					r);
				return r;
			}
		}
		adev->ip_blocks[i].status.late_initialized = true;
	}

	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_GATE);

	return r;
}

static int
soc_v1_0_mode2_restore_hwcontext(struct amdgpu_reset_control *reset_ctl,
				 struct amdgpu_reset_context *reset_context)
{
	struct list_head *reset_device_list = reset_context->reset_device_list;
	struct amdgpu_device *tmp_adev = NULL;
	int r;

	if (!reset_device_list)
		return -EINVAL;

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		amdgpu_set_init_level(tmp_adev,
				      AMDGPU_INIT_LEVEL_RESET_RECOVERY);
		dev_info(tmp_adev->dev,
			 "GPU reset succeeded, trying to resume\n");
		amdgpu_ras_clear_err_state(tmp_adev);
		r = soc_v1_0_mode2_restore_ip(tmp_adev);
		if (r)
			goto end;

		/*
		 * Add this ASIC as tracked as reset was already
		 * complete successfully.
		 */
		amdgpu_register_gpu_instance(tmp_adev);

		amdgpu_ras_resume(tmp_adev);

		if (!r) {
			amdgpu_set_init_level(tmp_adev,
					      AMDGPU_INIT_LEVEL_DEFAULT);
			amdgpu_irq_gpu_reset_resume_helper(tmp_adev);

			r = amdgpu_ib_ring_tests(tmp_adev);
			if (r) {
				dev_err(tmp_adev->dev,
					"ib ring test failed (%d).\n", r);
				r = -EAGAIN;
				tmp_adev->asic_reset_res = r;
				goto end;
			}
		}
	}

end:
	return r;
}

static struct amdgpu_reset_handler soc_v1_0_mode2_handler = {
	.reset_method		= AMD_RESET_METHOD_MODE2,
	.prepare_env		= NULL,
	.prepare_hwcontext	= soc_v1_0_mode2_prepare_hwcontext,
	.perform_reset		= soc_v1_0_mode2_perform_reset,
	.restore_hwcontext	= soc_v1_0_mode2_restore_hwcontext,
	.restore_env		= NULL,
	.do_reset		= soc_v1_0_mode2_reset,
};

static struct amdgpu_reset_handler
	*soc_v1_0_rst_handlers[AMDGPU_RESET_MAX_HANDLERS] = {
		&soc_v1_0_mode2_handler,
	};

int soc_v1_0_reset_init(struct amdgpu_device *adev)
{
	struct amdgpu_reset_control *reset_ctl;

	reset_ctl = kzalloc_obj(*reset_ctl, GFP_KERNEL);
	if (!reset_ctl)
		return -ENOMEM;

	reset_ctl->handle = adev;
	reset_ctl->async_reset = soc_v1_0_async_reset;
	reset_ctl->active_reset = AMD_RESET_METHOD_NONE;
	reset_ctl->get_reset_handler = soc_v1_0_get_reset_handler;

	INIT_WORK(&reset_ctl->reset_work, reset_ctl->async_reset);
	/* Only mode2 is handled through reset control now */
	reset_ctl->reset_handlers = &soc_v1_0_rst_handlers;

	adev->reset_cntl = reset_ctl;

	return 0;
}

int soc_v1_0_reset_fini(struct amdgpu_device *adev)
{
	kfree(adev->reset_cntl);
	adev->reset_cntl = NULL;
	return 0;
}

static enum amdgpu_gfx_partition __soc_v1_0_calc_xcp_mode(struct amdgpu_xcp_mgr *xcp_mgr)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_xcc, num_xcc_per_xcp = 0, mode = 0;

	num_xcc = NUM_XCC(xcp_mgr->adev->gfx.xcc_mask);
	if (adev->gfx.funcs &&
	    adev->gfx.funcs->get_xccs_per_xcp)
		num_xcc_per_xcp = adev->gfx.funcs->get_xccs_per_xcp(adev);
	if ((num_xcc_per_xcp) && (num_xcc % num_xcc_per_xcp == 0))
		mode = num_xcc / num_xcc_per_xcp;

	if (num_xcc_per_xcp == 1)
		return AMDGPU_CPX_PARTITION_MODE;

	switch (mode) {
	case 1:
		return AMDGPU_SPX_PARTITION_MODE;
	case 2:
		return AMDGPU_DPX_PARTITION_MODE;
	case 3:
		return AMDGPU_TPX_PARTITION_MODE;
	case 4:
		return AMDGPU_QPX_PARTITION_MODE;
	default:
		return AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE;
	}

	return AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE;
}

static int soc_v1_0_query_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr)
{
	enum amdgpu_gfx_partition derv_mode, mode;
	struct amdgpu_device *adev = xcp_mgr->adev;

	mode = AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE;
	derv_mode = __soc_v1_0_calc_xcp_mode(xcp_mgr);

	if (amdgpu_sriov_vf(adev) || !adev->psp.funcs)
		return derv_mode;

	if (adev->nbio.funcs &&
	    adev->nbio.funcs->get_compute_partition_mode) {
		mode = adev->nbio.funcs->get_compute_partition_mode(adev);
		if (mode != derv_mode)
			dev_warn(adev->dev,
				 "Mismatch in compute partition mode - reported : %d derived : %d",
				 mode, derv_mode);
	}

	return mode;
}

static int __soc_v1_0_get_xcc_per_xcp(struct amdgpu_xcp_mgr *xcp_mgr, int mode)
{
	int num_xcc, num_xcc_per_xcp = 0;

	num_xcc = NUM_XCC(xcp_mgr->adev->gfx.xcc_mask);

	switch (mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc;
		break;
	case AMDGPU_DPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc / 2;
		break;
	case AMDGPU_TPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc / 3;
		break;
	case AMDGPU_QPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc / 4;
		break;
	case AMDGPU_CPX_PARTITION_MODE:
		num_xcc_per_xcp = 1;
		break;
	}

	return num_xcc_per_xcp;
}

static int __soc_v1_0_get_xcp_ip_info(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
				      enum AMDGPU_XCP_IP_BLOCK ip_id,
				      struct amdgpu_xcp_ip *ip)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_sdma, num_vcn, num_shared_vcn, num_xcp;
	int num_xcc_xcp, num_sdma_xcp, num_vcn_xcp;

	num_sdma = adev->sdma.num_instances;
	num_vcn = adev->vcn.num_vcn_inst;
	num_shared_vcn = 1;

	num_xcc_xcp = adev->gfx.num_xcc_per_xcp;
	num_xcp = NUM_XCC(adev->gfx.xcc_mask) / num_xcc_xcp;

	switch (xcp_mgr->mode) {
	case AMDGPU_SPX_PARTITION_MODE:
	case AMDGPU_DPX_PARTITION_MODE:
	case AMDGPU_TPX_PARTITION_MODE:
	case AMDGPU_QPX_PARTITION_MODE:
	case AMDGPU_CPX_PARTITION_MODE:
		num_sdma_xcp = DIV_ROUND_UP(num_sdma, num_xcp);
		num_vcn_xcp = DIV_ROUND_UP(num_vcn, num_xcp);
		break;
	default:
		return -EINVAL;
	}

	if (num_vcn && num_xcp > num_vcn)
		num_shared_vcn = num_xcp / num_vcn;

	switch (ip_id) {
	case AMDGPU_XCP_GFXHUB:
		ip->inst_mask = XCP_INST_MASK(num_xcc_xcp, xcp_id);
		ip->ip_funcs = &gfxhub_v12_1_xcp_funcs;
		break;
	case AMDGPU_XCP_GFX:
		ip->inst_mask = XCP_INST_MASK(num_xcc_xcp, xcp_id);
		ip->ip_funcs = &gfx_v12_1_xcp_funcs;
		break;
	case AMDGPU_XCP_SDMA:
		ip->inst_mask = XCP_INST_MASK(num_sdma_xcp, xcp_id);
		ip->ip_funcs = &sdma_v7_1_xcp_funcs;
		break;
	case AMDGPU_XCP_VCN:
		ip->inst_mask =
			XCP_INST_MASK(num_vcn_xcp, xcp_id / num_shared_vcn);
		/* TODO : Assign IP funcs */
		break;
	default:
		return -EINVAL;
	}

	ip->ip_id = ip_id;

	return 0;
}

static int soc_v1_0_get_xcp_res_info(struct amdgpu_xcp_mgr *xcp_mgr,
				     int mode,
				     struct amdgpu_xcp_cfg *xcp_cfg)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int max_res[AMDGPU_XCP_RES_MAX] = {};
	bool res_lt_xcp;
	int num_xcp, i;
	u16 nps_modes;

	if (!(xcp_mgr->supp_xcp_modes & BIT(mode)))
		return -EINVAL;

	max_res[AMDGPU_XCP_RES_XCC] = NUM_XCC(adev->gfx.xcc_mask);
	max_res[AMDGPU_XCP_RES_DMA] = adev->sdma.num_instances;
	max_res[AMDGPU_XCP_RES_DEC] = adev->vcn.num_vcn_inst;
	max_res[AMDGPU_XCP_RES_JPEG] = adev->jpeg.num_jpeg_inst;

	switch (mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		num_xcp = 1;
		nps_modes = BIT(AMDGPU_NPS1_PARTITION_MODE);
		break;
	case AMDGPU_DPX_PARTITION_MODE:
		num_xcp = 2;
		nps_modes = BIT(AMDGPU_NPS1_PARTITION_MODE) |
			    BIT(AMDGPU_NPS2_PARTITION_MODE);
		break;
	case AMDGPU_TPX_PARTITION_MODE:
		num_xcp = 3;
		nps_modes = BIT(AMDGPU_NPS1_PARTITION_MODE) |
			    BIT(AMDGPU_NPS4_PARTITION_MODE);
		break;
	case AMDGPU_QPX_PARTITION_MODE:
		num_xcp = 4;
		nps_modes = BIT(AMDGPU_NPS1_PARTITION_MODE) |
			    BIT(AMDGPU_NPS4_PARTITION_MODE);
		break;
	case AMDGPU_CPX_PARTITION_MODE:
		num_xcp = NUM_XCC(adev->gfx.xcc_mask);
		nps_modes = BIT(AMDGPU_NPS1_PARTITION_MODE) |
			    BIT(AMDGPU_NPS4_PARTITION_MODE);
		break;
	default:
		return -EINVAL;
	}

	xcp_cfg->compatible_nps_modes =
		(adev->gmc.supported_nps_modes & nps_modes);
	xcp_cfg->num_res = ARRAY_SIZE(max_res);

	for (i = 0; i < xcp_cfg->num_res; i++) {
		xcp_cfg->xcp_res[i].id = i;
		if (!max_res[i])
			continue;
		res_lt_xcp = max_res[i] < num_xcp;
		xcp_cfg->xcp_res[i].num_inst =
			res_lt_xcp ? 1 : max_res[i] / num_xcp;
		xcp_cfg->xcp_res[i].num_inst =
			i == AMDGPU_XCP_RES_JPEG ?
			xcp_cfg->xcp_res[i].num_inst *
			adev->jpeg.num_jpeg_rings : xcp_cfg->xcp_res[i].num_inst;
		xcp_cfg->xcp_res[i].num_shared =
			res_lt_xcp ? num_xcp / max_res[i] : 1;
	}

	return 0;
}

static enum amdgpu_gfx_partition __soc_v1_0_get_auto_mode(struct amdgpu_xcp_mgr *xcp_mgr)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_xcc;

	num_xcc = NUM_XCC(xcp_mgr->adev->gfx.xcc_mask);

	if (adev->gmc.num_mem_partitions == 1)
		return AMDGPU_SPX_PARTITION_MODE;

	if (adev->gmc.num_mem_partitions == num_xcc)
		return AMDGPU_CPX_PARTITION_MODE;

	if (adev->gmc.num_mem_partitions == 2)
		return AMDGPU_DPX_PARTITION_MODE;

	return AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE;
}

static bool __soc_v1_0_is_valid_mode(struct amdgpu_xcp_mgr *xcp_mgr,
				     enum amdgpu_gfx_partition mode)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_xcc, num_xccs_per_xcp;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		return adev->gmc.num_mem_partitions == 1 && num_xcc > 0;
	case AMDGPU_DPX_PARTITION_MODE:
		return adev->gmc.num_mem_partitions <= 2 && (num_xcc % 2) == 0;
	case AMDGPU_TPX_PARTITION_MODE:
		return (adev->gmc.num_mem_partitions == 1 ||
			adev->gmc.num_mem_partitions == 3) &&
		       ((num_xcc % 3) == 0);
	case AMDGPU_QPX_PARTITION_MODE:
		num_xccs_per_xcp = num_xcc / 4;
		return (adev->gmc.num_mem_partitions == 1 ||
			adev->gmc.num_mem_partitions == 4) &&
		       (num_xccs_per_xcp >= 2);
	case AMDGPU_CPX_PARTITION_MODE:
		/* (num_xcc > 1) because 1 XCC is considered SPX, not CPX.
		 * (num_xcc % adev->gmc.num_mem_partitions) == 0 because
		 * num_compute_partitions can't be less than num_mem_partitions
		 */
		return (adev->gmc.num_mem_partitions == 1 ||
			adev->gmc.num_mem_partitions == 4) &&
		       (num_xcc > 1) &&
		       (num_xcc % adev->gmc.num_mem_partitions) == 0;
	default:
		return false;
	}

	return false;
}

static void __soc_v1_0_update_available_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr)
{
	int mode;

	xcp_mgr->avail_xcp_modes = 0;

	for_each_inst(mode, xcp_mgr->supp_xcp_modes) {
		if (__soc_v1_0_is_valid_mode(xcp_mgr, mode))
			xcp_mgr->avail_xcp_modes |= BIT(mode);
	}
}

static int soc_v1_0_switch_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr,
					  int mode, int *num_xcps)
{
	int num_xcc_per_xcp, num_xcc, ret;
	struct amdgpu_device *adev;
	u32 flags = 0;

	adev = xcp_mgr->adev;
	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	if (mode == AMDGPU_AUTO_COMPUTE_PARTITION_MODE) {
		mode = __soc_v1_0_get_auto_mode(xcp_mgr);
		if (mode == AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE) {
			dev_err(adev->dev,
				"Invalid config, no compatible compute partition mode found, available memory partitions: %d",
				adev->gmc.num_mem_partitions);
			return -EINVAL;
		}
	} else if (!__soc_v1_0_is_valid_mode(xcp_mgr, mode)) {
		dev_err(adev->dev,
			"Invalid compute partition mode requested, requested: %s, available memory partitions: %d",
			amdgpu_gfx_compute_mode_desc(mode), adev->gmc.num_mem_partitions);
		return -EINVAL;
	}

	if (adev->kfd.init_complete && !amdgpu_in_reset(adev))
		flags |= AMDGPU_XCP_OPS_KFD;

	ret = amdgpu_xcp_pre_partition_switch(xcp_mgr, flags);
	if (ret)
		goto out;

	num_xcc_per_xcp = __soc_v1_0_get_xcc_per_xcp(xcp_mgr, mode);
	if (adev->gfx.imu.funcs &&
	    adev->gfx.imu.funcs->switch_compute_partition) {
		ret = adev->gfx.imu.funcs->switch_compute_partition(xcp_mgr->adev, num_xcc_per_xcp, mode);
		if (ret)
			goto out;
	}
	if (adev->gfx.imu.funcs &&
	    adev->gfx.imu.funcs->init_mcm_addr_lut &&
	    amdgpu_emu_mode)
		adev->gfx.imu.funcs->init_mcm_addr_lut(adev);

	/* Init info about new xcps */
	*num_xcps = num_xcc / num_xcc_per_xcp;
	amdgpu_xcp_init(xcp_mgr, *num_xcps, mode);

	ret = amdgpu_xcp_post_partition_switch(xcp_mgr, flags);
	if (!ret)
		__soc_v1_0_update_available_partition_mode(xcp_mgr);
out:
	return ret;
}

#ifdef HAVE_ACPI_DEV_GET_FIRST_MATCH_DEV
static int __soc_v1_0_get_xcp_mem_id(struct amdgpu_device *adev,
				     int xcc_id, uint8_t *mem_id)
{
	/* memory/spatial modes validation check is already done */
	*mem_id = xcc_id / adev->gfx.num_xcc_per_xcp;
	*mem_id /= adev->xcp_mgr->num_xcp_per_mem_partition;

	return 0;
}

static int soc_v1_0_get_xcp_mem_id(struct amdgpu_xcp_mgr *xcp_mgr,
				   struct amdgpu_xcp *xcp, uint8_t *mem_id)
{
	struct amdgpu_numa_info numa_info;
	struct amdgpu_device *adev;
	uint32_t xcc_mask;
	int r, i, xcc_id;

	adev = xcp_mgr->adev;
	/* TODO: BIOS is not returning the right info now
	 * Check on this later
	 */
	/*
	if (adev->gmc.gmc_funcs->query_mem_partition_mode)
		mode = adev->gmc.gmc_funcs->query_mem_partition_mode(adev);
	*/
	if (adev->gmc.num_mem_partitions == 1) {
		/* Only one range */
		*mem_id = 0;
		return 0;
	}

	r = amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_GFX, &xcc_mask);
	if (r || !xcc_mask)
		return -EINVAL;

	xcc_id = ffs(xcc_mask) - 1;
	if (!adev->gmc.is_app_apu)
		return __soc_v1_0_get_xcp_mem_id(adev, xcc_id, mem_id);

	r = amdgpu_acpi_get_mem_info(adev, xcc_id, &numa_info);

	if (r)
		return r;

	r = -EINVAL;
	for (i = 0; i < adev->gmc.num_mem_partitions; ++i) {
		if (adev->gmc.mem_partitions[i].numa.node == numa_info.nid) {
			*mem_id = i;
			r = 0;
			break;
		}
	}

	return r;
}
#endif

static int soc_v1_0_get_xcp_ip_details(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
				       enum AMDGPU_XCP_IP_BLOCK ip_id,
				       struct amdgpu_xcp_ip *ip)
{
	if (!ip)
		return -EINVAL;

	return __soc_v1_0_get_xcp_ip_info(xcp_mgr, xcp_id, ip_id, ip);
}

struct amdgpu_xcp_mgr_funcs soc_v1_0_xcp_funcs = {
	.switch_partition_mode = &soc_v1_0_switch_partition_mode,
	.query_partition_mode = &soc_v1_0_query_partition_mode,
	.get_ip_details = &soc_v1_0_get_xcp_ip_details,
	.get_xcp_res_info = &soc_v1_0_get_xcp_res_info,
#ifdef HAVE_ACPI_DEV_GET_FIRST_MATCH_DEV
	.get_xcp_mem_id = &soc_v1_0_get_xcp_mem_id,
#endif
};

static int soc_v1_0_xcp_mgr_init(struct amdgpu_device *adev)
{
	int ret;

	if (amdgpu_sriov_vf(adev))
		soc_v1_0_xcp_funcs.switch_partition_mode = NULL;

	ret = amdgpu_xcp_mgr_init(adev, AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE,
				  1, &soc_v1_0_xcp_funcs);
	if (ret)
		return ret;

	amdgpu_xcp_update_supported_modes(adev->xcp_mgr);
	/* TODO: Default memory node affinity init */

	return ret;
}

uint32_t soc_v1_0_get_aid_mask(uint16_t xcc_mask)
{
	int xcc_inst_per_aid = 4;
	uint32_t aid_mask = 0;
	int i;

	for (i = 0; xcc_mask; xcc_mask >>= xcc_inst_per_aid, i++) {
		if (xcc_mask & GENMASK(xcc_inst_per_aid - 1, 0))
			aid_mask |= BIT(i);
	}

	return aid_mask;
}

int soc_v1_0_init_soc_config(struct amdgpu_device *adev)
{
	int ret, i;
	uint16_t sdma_mask = 0;

	adev->aid_mask = soc_v1_0_get_aid_mask(adev->gfx.xcc_mask);

	adev->sdma.num_inst_per_xcc = 2;
	for_each_inst(i, adev->gfx.xcc_mask)
		sdma_mask |=
			GENMASK(adev->sdma.num_inst_per_xcc - 1, 0) <<
			(i * adev->sdma.num_inst_per_xcc);
	adev->sdma.sdma_mask = sdma_mask;
	adev->sdma.num_instances = NUM_XCC(adev->sdma.sdma_mask);

	adev->vcn.harvest_config = 0;
	adev->vcn.num_inst_per_aid = 2;
	adev->vcn.num_vcn_inst = hweight32(adev->vcn.inst_mask);

	adev->jpeg.harvest_config = 0;
	adev->jpeg.num_inst_per_aid = 2;
	adev->jpeg.num_jpeg_inst = hweight32(adev->jpeg.inst_mask);

	ret = soc_v1_0_xcp_mgr_init(adev);
	if (ret)
		return ret;

	amdgpu_ip_map_init(adev);

	return 0;
}

bool soc_v1_0_normalize_xcc_reg_range(uint32_t reg)
{
	if (((reg >= XCC_REG_RANGE_0_LOW) && (reg < XCC_REG_RANGE_0_HIGH)) ||
	    ((reg >= XCC_REG_RANGE_1_LOW) && (reg < XCC_REG_RANGE_1_HIGH)))
		return true;
	else
		return false;
}

uint32_t soc_v1_0_normalize_xcc_reg_offset(uint32_t reg)
{
	uint32_t normalized_reg = NORMALIZE_XCC_REG_OFFSET(reg);

	/* If it is an XCC reg, normalize the reg to keep
	 * lower 16 bits in local xcc */

	if (soc_v1_0_normalize_xcc_reg_range(normalized_reg))
		return normalized_reg;
	else
		return reg;
}

bool soc_v1_0_mid1_reg_range(uint32_t reg)
{
	uint32_t normalized_reg = soc_v1_0_normalize_xcc_reg_offset(reg);

	if (soc_v1_0_normalize_xcc_reg_range(normalized_reg))
		return false;

	if ((reg >= MID1_REG_RANGE_0_LOW) && (reg < MID1_REG_RANGE_0_HIGH))
		return true;
	else
		return false;
}

uint32_t soc_v1_0_normalize_reg_offset(uint32_t reg)
{
	uint32_t normalized_reg = soc_v1_0_normalize_xcc_reg_offset(reg);

	if (soc_v1_0_normalize_xcc_reg_range(normalized_reg))
		return soc_v1_0_normalize_xcc_reg_offset(reg);

	/* check if the reg offset is inside MID1. */
	if (soc_v1_0_mid1_reg_range(reg))
		return NORMALIZE_MID_REG_OFFSET(reg);

	return reg;
}


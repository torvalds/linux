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
#include "soc15.h"
#include "soc15_common.h"
#include "soc_v1_0.h"
#include "amdgpu_ip.h"
#include "amdgpu_imu.h"
#include "gfxhub_v12_1.h"
#include "sdma_v7_1.h"
#include "gfx_v12_1.h"

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "mp/mp_15_0_8_offset.h"

#define XCC_REG_RANGE_0_LOW  0x1260     /* XCC gfxdec0 lower Bound */
#define XCC_REG_RANGE_0_HIGH 0x3C00     /* XCC gfxdec0 upper Bound */
#define XCC_REG_RANGE_1_LOW  0xA000     /* XCC gfxdec1 lower Bound */
#define XCC_REG_RANGE_1_HIGH 0x10000    /* XCC gfxdec1 upper Bound */
#define NORMALIZE_XCC_REG_OFFSET(offset) \
	(offset & 0xFFFF)

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

	adev->doorbell_index.sdma_doorbell_range = 20;
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

static uint32_t soc_v1_0_read_indexed_register(struct amdgpu_device *adev,
					       u32 se_num,
					       u32 sh_num,
					       u32 reg_offset)
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

static uint32_t soc_v1_0_get_register_value(struct amdgpu_device *adev,
					    bool indexed, u32 se_num,
					    u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		return soc_v1_0_read_indexed_register(adev, se_num, sh_num, reg_offset);
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

static bool soc_v1_0_need_full_reset(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 1, 0):
	default:
		return true;
	}
}

static bool soc_v1_0_need_reset_on_init(struct amdgpu_device *adev)
{
	u32 sol_reg;

	if (adev->flags & AMD_IS_APU)
		return false;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_81);
	if (sol_reg)
		return true;

	return false;
}

static int soc_v1_0_asic_reset(struct amdgpu_device *adev)
{
	return 0;
}

static const struct amdgpu_asic_funcs soc_v1_0_asic_funcs = {
	.read_bios_from_rom = &amdgpu_soc15_read_bios_from_rom,
	.read_register = &soc_v1_0_read_register,
	.get_config_memsize = &soc_v1_0_get_config_memsize,
	.get_xclk = &soc_v1_0_get_xclk,
	.need_full_reset = &soc_v1_0_need_full_reset,
	.init_doorbell_index = &soc_v1_0_doorbell_index_init,
	.need_reset_on_init = &soc_v1_0_need_reset_on_init,
	.encode_ext_smn_addressing = &soc_v1_0_encode_ext_smn_addressing,
	.reset = soc_v1_0_asic_reset,
};

static int soc_v1_0_common_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &amdgpu_device_indirect_rreg;
	adev->pcie_wreg = &amdgpu_device_indirect_wreg;
	adev->pcie_rreg_ext = &amdgpu_device_indirect_rreg_ext;
	adev->pcie_wreg_ext = &amdgpu_device_indirect_wreg_ext;
	adev->pcie_rreg64 = &amdgpu_device_indirect_rreg64;
	adev->pcie_wreg64 = &amdgpu_device_indirect_wreg64;
	adev->pciep_rreg = amdgpu_device_pcie_port_rreg;
	adev->pciep_wreg = amdgpu_device_pcie_port_wreg;
	adev->pcie_rreg64_ext = &amdgpu_device_indirect_rreg64_ext;
	adev->pcie_wreg64_ext = &amdgpu_device_indirect_wreg64_ext;
	adev->uvd_ctx_rreg = NULL;
	adev->uvd_ctx_wreg = NULL;
	adev->didt_rreg = NULL;
	adev->didt_wreg = NULL;

	adev->asic_funcs = &soc_v1_0_asic_funcs;

	adev->rev_id = amdgpu_device_get_rev_id(adev);
	adev->external_rev_id = 0xff;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 1, 0):
		adev->cg_flags = 0;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 0x50;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

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

static int soc_v1_0_common_sw_init(struct amdgpu_ip_block *ip_block)
{
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

static bool soc_v1_0_common_is_idle(struct amdgpu_ip_block *ip_block)
{
	return true;
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
	.sw_init = soc_v1_0_common_sw_init,
	.hw_init = soc_v1_0_common_hw_init,
	.hw_fini = soc_v1_0_common_hw_fini,
	.suspend = soc_v1_0_common_suspend,
	.resume = soc_v1_0_common_resume,
	.is_idle = soc_v1_0_common_is_idle,
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
		nps_modes = BIT(AMDGPU_NPS1_PARTITION_MODE);
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
		res_lt_xcp = max_res[i] < num_xcp;
		xcp_cfg->xcp_res[i].id = i;
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
		return adev->gmc.num_mem_partitions <= 2 && (num_xcc % 4) == 0;
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
		return ((num_xcc > 1) &&
		       (num_xcc % adev->gmc.num_mem_partitions) == 0);
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

	if (flags & AMDGPU_XCP_OPS_KFD) {
		ret = amdgpu_amdkfd_check_and_lock_kfd(adev);
		if (ret)
			goto out;
	}

	ret = amdgpu_xcp_pre_partition_switch(xcp_mgr, flags);
	if (ret)
		goto unlock;

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
unlock:
	if (flags & AMDGPU_XCP_OPS_KFD)
		amdgpu_amdkfd_unlock_kfd(adev);
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

int soc_v1_0_init_soc_config(struct amdgpu_device *adev)
{
	int ret, i;
	int xcc_inst_per_aid = 4;
	uint16_t xcc_mask;

	xcc_mask = adev->gfx.xcc_mask;
	adev->aid_mask = 0;
	for (i = 0; xcc_mask; xcc_mask >>= xcc_inst_per_aid, i++) {
		if (xcc_mask & ((1U << xcc_inst_per_aid) - 1))
			adev->aid_mask |= (1 << i);
	}

	adev->sdma.num_inst_per_xcc = 2;
	adev->sdma.num_instances =
		NUM_XCC(adev->gfx.xcc_mask) * adev->sdma.num_inst_per_xcc;
	adev->sdma.sdma_mask =
		GENMASK(adev->sdma.num_instances - 1, 0);

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

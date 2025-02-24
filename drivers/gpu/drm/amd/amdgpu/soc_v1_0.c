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

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "mp/mp_15_0_8_offset.h"

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

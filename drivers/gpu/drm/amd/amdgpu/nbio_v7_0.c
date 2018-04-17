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
 */
#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "nbio_v7_0.h"

#include "vega10/soc15ip.h"
#include "raven1/NBIO/nbio_7_0_default.h"
#include "raven1/NBIO/nbio_7_0_offset.h"
#include "raven1/NBIO/nbio_7_0_sh_mask.h"
#include "vega10/vega10_enum.h"

#define smnNBIF_MGCG_CTRL_LCLK	0x1013a05c

u32 nbio_v7_0_get_rev_id(struct amdgpu_device *adev)
{
        u32 tmp = RREG32_SOC15(NBIO, 0, mmRCC_DEV0_EPF0_STRAP0);

	tmp &= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

u32 nbio_v7_0_get_atombios_scratch_regs(struct amdgpu_device *adev,
					uint32_t idx)
{
	return RREG32_SOC15_OFFSET(NBIO, 0, mmBIOS_SCRATCH_0, idx);
}

void nbio_v7_0_set_atombios_scratch_regs(struct amdgpu_device *adev,
					 uint32_t idx, uint32_t val)
{
	WREG32_SOC15_OFFSET(NBIO, 0, mmBIOS_SCRATCH_0, idx, val);
}

void nbio_v7_0_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN,
			BIF_FB_EN__FB_READ_EN_MASK | BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN, 0);
}

void nbio_v7_0_hdp_flush(struct amdgpu_device *adev)
{
	WREG32_SOC15_NO_KIQ(NBIO, 0, mmHDP_MEM_COHERENCY_FLUSH_CNTL, 0);
}

u32 nbio_v7_0_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, mmRCC_CONFIG_MEMSIZE);
}

static const u32 nbio_sdma_doorbell_range_reg[] =
{
	SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA0_DOORBELL_RANGE),
	SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA1_DOORBELL_RANGE)
};

void nbio_v7_0_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
				  bool use_doorbell, int doorbell_index)
{
	u32 doorbell_range = RREG32(nbio_sdma_doorbell_range_reg[instance]);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, OFFSET, doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, SIZE, 2);
	} else
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, SIZE, 0);

	WREG32(nbio_sdma_doorbell_range_reg[instance], doorbell_range);
}

void nbio_v7_0_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable)
{
	WREG32_FIELD15(NBIO, 0, RCC_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

void nbio_v7_0_ih_doorbell_range(struct amdgpu_device *adev,
				bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_SOC15(NBIO, 0 , mmBIF_IH_DOORBELL_RANGE);

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, OFFSET, doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 2);
	} else
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 0);

	WREG32_SOC15(NBIO, 0, mmBIF_IH_DOORBELL_RANGE, ih_doorbell_range);
}

static uint32_t nbio_7_0_read_syshub_ind_mmr(struct amdgpu_device *adev, uint32_t offset)
{
	uint32_t data;

	WREG32_SOC15(NBIO, 0, mmSYSHUB_INDEX, offset);
	data = RREG32_SOC15(NBIO, 0, mmSYSHUB_DATA);

	return data;
}

static void nbio_7_0_write_syshub_ind_mmr(struct amdgpu_device *adev, uint32_t offset,
				       uint32_t data)
{
	WREG32_SOC15(NBIO, 0, mmSYSHUB_INDEX, offset);
	WREG32_SOC15(NBIO, 0, mmSYSHUB_DATA, data);
}

void nbio_v7_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t def, data;

	/* NBIF_MGCG_CTRL_LCLK */
	def = data = RREG32_PCIE(smnNBIF_MGCG_CTRL_LCLK);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG))
		data |= NBIF_MGCG_CTRL_LCLK__NBIF_MGCG_EN_LCLK_MASK;
	else
		data &= ~NBIF_MGCG_CTRL_LCLK__NBIF_MGCG_EN_LCLK_MASK;

	if (def != data)
		WREG32_PCIE(smnNBIF_MGCG_CTRL_LCLK, data);

	/* SYSHUB_MGCG_CTRL_SOCCLK */
	def = data = nbio_7_0_read_syshub_ind_mmr(adev, ixSYSHUB_MMREG_IND_SYSHUB_MGCG_CTRL_SOCCLK);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG))
		data |= SYSHUB_MMREG_DIRECT_SYSHUB_MGCG_CTRL_SOCCLK__SYSHUB_MGCG_EN_SOCCLK_MASK;
	else
		data &= ~SYSHUB_MMREG_DIRECT_SYSHUB_MGCG_CTRL_SOCCLK__SYSHUB_MGCG_EN_SOCCLK_MASK;

	if (def != data)
		nbio_7_0_write_syshub_ind_mmr(adev, ixSYSHUB_MMREG_IND_SYSHUB_MGCG_CTRL_SOCCLK, data);

	/* SYSHUB_MGCG_CTRL_SHUBCLK */
	def = data = nbio_7_0_read_syshub_ind_mmr(adev, ixSYSHUB_MMREG_IND_SYSHUB_MGCG_CTRL_SHUBCLK);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG))
		data |= SYSHUB_MMREG_DIRECT_SYSHUB_MGCG_CTRL_SHUBCLK__SYSHUB_MGCG_EN_SHUBCLK_MASK;
	else
		data &= ~SYSHUB_MMREG_DIRECT_SYSHUB_MGCG_CTRL_SHUBCLK__SYSHUB_MGCG_EN_SHUBCLK_MASK;

	if (def != data)
		nbio_7_0_write_syshub_ind_mmr(adev, ixSYSHUB_MMREG_IND_SYSHUB_MGCG_CTRL_SHUBCLK, data);
}

void nbio_v7_0_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL2, adev->dummy_page.addr >> 8);
	interrupt_cntl = RREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL);
	/* INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_DUMMY_RD_OVERRIDE, 0);
	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_REQ_NONSNOOP_EN, 0);
	WREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL, interrupt_cntl);
}

const struct nbio_hdp_flush_reg nbio_v7_0_hdp_flush_reg = {
	.hdp_flush_req_offset = SOC15_REG_OFFSET(NBIO, 0, mmGPU_HDP_FLUSH_REQ),
	.hdp_flush_done_offset = SOC15_REG_OFFSET(NBIO, 0, mmGPU_HDP_FLUSH_DONE),
	.ref_and_mask_cp0 = GPU_HDP_FLUSH_DONE__CP0_MASK,
	.ref_and_mask_cp1 = GPU_HDP_FLUSH_DONE__CP1_MASK,
	.ref_and_mask_cp2 = GPU_HDP_FLUSH_DONE__CP2_MASK,
	.ref_and_mask_cp3 = GPU_HDP_FLUSH_DONE__CP3_MASK,
	.ref_and_mask_cp4 = GPU_HDP_FLUSH_DONE__CP4_MASK,
	.ref_and_mask_cp5 = GPU_HDP_FLUSH_DONE__CP5_MASK,
	.ref_and_mask_cp6 = GPU_HDP_FLUSH_DONE__CP6_MASK,
	.ref_and_mask_cp7 = GPU_HDP_FLUSH_DONE__CP7_MASK,
	.ref_and_mask_cp8 = GPU_HDP_FLUSH_DONE__CP8_MASK,
	.ref_and_mask_cp9 = GPU_HDP_FLUSH_DONE__CP9_MASK,
	.ref_and_mask_sdma0 = GPU_HDP_FLUSH_DONE__SDMA0_MASK,
	.ref_and_mask_sdma1 = GPU_HDP_FLUSH_DONE__SDMA1_MASK,
};

const struct nbio_pcie_index_data nbio_v7_0_pcie_index_data = {
	.index_offset = SOC15_REG_OFFSET(NBIO, 0, mmPCIE_INDEX2),
	.data_offset = SOC15_REG_OFFSET(NBIO, 0, mmPCIE_DATA2)
};

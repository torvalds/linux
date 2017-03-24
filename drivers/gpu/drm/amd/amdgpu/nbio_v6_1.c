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
#include "nbio_v6_1.h"

#include "vega10/soc15ip.h"
#include "vega10/NBIO/nbio_6_1_default.h"
#include "vega10/NBIO/nbio_6_1_offset.h"
#include "vega10/NBIO/nbio_6_1_sh_mask.h"
#include "vega10/vega10_enum.h"

#define smnCPM_CONTROL                                                                                  0x11180460
#define smnPCIE_CNTL2                                                                                   0x11180070

u32 nbio_v6_1_get_rev_id(struct amdgpu_device *adev)
{
        u32 tmp = RREG32(SOC15_REG_OFFSET(NBIO, 0, mmRCC_DEV0_EPF0_STRAP0));

	tmp &= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

u32 nbio_v6_1_get_atombios_scratch_regs(struct amdgpu_device *adev,
					uint32_t idx)
{
	return RREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIOS_SCRATCH_0) + idx);
}

void nbio_v6_1_set_atombios_scratch_regs(struct amdgpu_device *adev,
					 uint32_t idx, uint32_t val)
{
	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIOS_SCRATCH_0) + idx, val);
}

void nbio_v6_1_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_FB_EN),
			BIF_FB_EN__FB_READ_EN_MASK | BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_FB_EN), 0);
}

void nbio_v6_1_hdp_flush(struct amdgpu_device *adev)
{
	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_HDP_MEM_COHERENCY_FLUSH_CNTL), 0);
}

u32 nbio_v6_1_get_memsize(struct amdgpu_device *adev)
{
	return RREG32(SOC15_REG_OFFSET(NBIO, 0, mmRCC_PF_0_0_RCC_CONFIG_MEMSIZE));
}

static const u32 nbio_sdma_doorbell_range_reg[] =
{
	SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA0_DOORBELL_RANGE),
	SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA1_DOORBELL_RANGE)
};

void nbio_v6_1_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
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

void nbio_v6_1_enable_doorbell_aperture(struct amdgpu_device *adev,
					bool enable)
{
	u32 tmp;

	tmp = RREG32(SOC15_REG_OFFSET(NBIO, 0, mmRCC_PF_0_0_RCC_DOORBELL_APER_EN));
	if (enable)
		tmp = REG_SET_FIELD(tmp, RCC_PF_0_0_RCC_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, 1);
	else
		tmp = REG_SET_FIELD(tmp, RCC_PF_0_0_RCC_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, 0);

	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmRCC_PF_0_0_RCC_DOORBELL_APER_EN), tmp);
}

void nbio_v6_1_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
					bool enable)
{
	u32 tmp = 0;

	if (enable) {
		tmp = REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_EN, 1) |
			REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_MODE, 1) |
			REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_SIZE, 0);

		WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_LOW),
				       lower_32_bits(adev->doorbell.base));
		WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_HIGH),
				       upper_32_bits(adev->doorbell.base));
	}

	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL), tmp);
}


void nbio_v6_1_ih_doorbell_range(struct amdgpu_device *adev,
				bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32(SOC15_REG_OFFSET(NBIO, 0 , mmBIF_IH_DOORBELL_RANGE));

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, OFFSET, doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 2);
	} else
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 0);

	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmBIF_IH_DOORBELL_RANGE), ih_doorbell_range);
}

void nbio_v6_1_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmINTERRUPT_CNTL2), adev->dummy_page.addr >> 8);
	interrupt_cntl = RREG32(SOC15_REG_OFFSET(NBIO, 0, mmINTERRUPT_CNTL));
	/* INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_DUMMY_RD_OVERRIDE, 0);
	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_REQ_NONSNOOP_EN, 0);
	WREG32(SOC15_REG_OFFSET(NBIO, 0, mmINTERRUPT_CNTL), interrupt_cntl);
}

void nbio_v6_1_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t def, data;

	def = data = RREG32_PCIE(smnCPM_CONTROL);
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG)) {
		data |= (CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_DYN_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_PERM_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_LCNT_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_REGS_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_PRBS_GATE_ENABLE_MASK |
			 CPM_CONTROL__REFCLK_REGS_GATE_ENABLE_MASK);
	} else {
		data &= ~(CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_DYN_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_PERM_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_LCNT_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_REGS_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_PRBS_GATE_ENABLE_MASK |
			  CPM_CONTROL__REFCLK_REGS_GATE_ENABLE_MASK);
	}

	if (def != data)
		WREG32_PCIE(smnCPM_CONTROL, data);
}

void nbio_v6_1_update_medium_grain_light_sleep(struct amdgpu_device *adev,
					       bool enable)
{
	uint32_t def, data;

	def = data = RREG32_PCIE(smnPCIE_CNTL2);
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_LS)) {
		data |= (PCIE_CNTL2__SLV_MEM_LS_EN_MASK |
			 PCIE_CNTL2__MST_MEM_LS_EN_MASK |
			 PCIE_CNTL2__REPLAY_MEM_LS_EN_MASK);
	} else {
		data &= ~(PCIE_CNTL2__SLV_MEM_LS_EN_MASK |
			  PCIE_CNTL2__MST_MEM_LS_EN_MASK |
			  PCIE_CNTL2__REPLAY_MEM_LS_EN_MASK);
	}

	if (def != data)
		WREG32_PCIE(smnPCIE_CNTL2, data);
}

void nbio_v6_1_get_clockgating_state(struct amdgpu_device *adev, u32 *flags)
{
	int data;

	/* AMD_CG_SUPPORT_BIF_MGCG */
	data = RREG32_PCIE(smnCPM_CONTROL);
	if (data & CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_MGCG;

	/* AMD_CG_SUPPORT_BIF_LS */
	data = RREG32_PCIE(smnPCIE_CNTL2);
	if (data & PCIE_CNTL2__SLV_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_LS;
}

struct nbio_hdp_flush_reg nbio_v6_1_hdp_flush_reg;
struct nbio_pcie_index_data nbio_v6_1_pcie_index_data;

int nbio_v6_1_init(struct amdgpu_device *adev)
{
	nbio_v6_1_hdp_flush_reg.hdp_flush_req_offset = SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_GPU_HDP_FLUSH_REQ);
	nbio_v6_1_hdp_flush_reg.hdp_flush_done_offset = SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_GPU_HDP_FLUSH_DONE);
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp0 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP0_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp1 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP1_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp2 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP2_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp3 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP3_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp4 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP4_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp5 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP5_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp6 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP6_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp7 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP7_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp8 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP8_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_cp9 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP9_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_sdma0 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__SDMA0_MASK;
	nbio_v6_1_hdp_flush_reg.ref_and_mask_sdma1 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__SDMA1_MASK;

	nbio_v6_1_pcie_index_data.index_offset = SOC15_REG_OFFSET(NBIO, 0, mmPCIE_INDEX);
	nbio_v6_1_pcie_index_data.data_offset = SOC15_REG_OFFSET(NBIO, 0, mmPCIE_DATA);

	return 0;
}

void nbio_v6_1_detect_hw_virt(struct amdgpu_device *adev)
{
	uint32_t reg;

	reg = RREG32(SOC15_REG_OFFSET(NBIO, 0,
				      mmRCC_PF_0_0_RCC_IOV_FUNC_IDENTIFIER));
	if (reg & 1)
		adev->virt.caps |= AMDGPU_SRIOV_CAPS_IS_VF;

	if (reg & 0x80000000)
		adev->virt.caps |= AMDGPU_SRIOV_CAPS_ENABLE_IOV;

	if (!reg) {
		if (is_virtual_machine())	/* passthrough mode exclus sriov mod */
			adev->virt.caps |= AMDGPU_PASSTHROUGH_MODE;
	}
}

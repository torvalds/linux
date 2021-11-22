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
#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "nbio_v2_3.h"

#include "nbio/nbio_2_3_default.h"
#include "nbio/nbio_2_3_offset.h"
#include "nbio/nbio_2_3_sh_mask.h"
#include <uapi/linux/kfd_ioctl.h>
#include <linux/pci.h>

#define smnPCIE_CONFIG_CNTL	0x11180044
#define smnCPM_CONTROL		0x11180460
#define smnPCIE_CNTL2		0x11180070
#define smnPCIE_LC_CNTL		0x11140280
#define smnPCIE_LC_CNTL3	0x111402d4
#define smnPCIE_LC_CNTL6	0x111402ec
#define smnPCIE_LC_CNTL7	0x111402f0
#define smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2	0x1014008c
#define smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL	0x10123538
#define smnBIF_CFG_DEV0_EPF0_PCIE_LTR_CAP	0x10140324
#define smnPSWUSP0_PCIE_LC_CNTL2		0x111402c4
#define smnNBIF_MGCG_CTRL_LCLK			0x1013a21c

#define mmBIF_SDMA2_DOORBELL_RANGE		0x01d6
#define mmBIF_SDMA2_DOORBELL_RANGE_BASE_IDX	2
#define mmBIF_SDMA3_DOORBELL_RANGE		0x01d7
#define mmBIF_SDMA3_DOORBELL_RANGE_BASE_IDX	2

#define mmBIF_MMSCH1_DOORBELL_RANGE		0x01d8
#define mmBIF_MMSCH1_DOORBELL_RANGE_BASE_IDX	2

#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288

#define GPU_HDP_FLUSH_DONE__RSVD_ENG0_MASK	0x00001000L /* Don't use.  Firmware uses this bit internally */
#define GPU_HDP_FLUSH_DONE__RSVD_ENG1_MASK	0x00002000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG2_MASK	0x00004000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG3_MASK	0x00008000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG4_MASK	0x00010000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG5_MASK	0x00020000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG6_MASK	0x00040000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG7_MASK	0x00080000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG8_MASK	0x00100000L

static void nbio_v2_3_remap_hdp_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, mmREMAP_HDP_MEM_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
	WREG32_SOC15(NBIO, 0, mmREMAP_HDP_REG_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v2_3_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

	/*
	 * guest vm gets 0xffffffff when reading RCC_DEV0_EPF0_STRAP0,
	 * therefore we force rev_id to 0 (which is the default value)
	 */
	if (amdgpu_sriov_vf(adev)) {
		return 0;
	}

	tmp = RREG32_SOC15(NBIO, 0, mmRCC_DEV0_EPF0_STRAP0);
	tmp &= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v2_3_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN,
			     BIF_FB_EN__FB_READ_EN_MASK |
			     BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN, 0);
}

static u32 nbio_v2_3_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, mmRCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v2_3_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
					  bool use_doorbell, int doorbell_index,
					  int doorbell_size)
{
	u32 reg = instance == 0 ? SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA0_DOORBELL_RANGE) :
			instance == 1 ? SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA1_DOORBELL_RANGE) :
			instance == 2 ? SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA2_DOORBELL_RANGE) :
			SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA3_DOORBELL_RANGE);

	u32 doorbell_range = RREG32(reg);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       BIF_SDMA0_DOORBELL_RANGE, OFFSET,
					       doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       BIF_SDMA0_DOORBELL_RANGE, SIZE,
					       doorbell_size);
	} else
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       BIF_SDMA0_DOORBELL_RANGE, SIZE,
					       0);

	WREG32(reg, doorbell_range);
}

static void nbio_v2_3_vcn_doorbell_range(struct amdgpu_device *adev, bool use_doorbell,
					 int doorbell_index, int instance)
{
	u32 reg = instance ? SOC15_REG_OFFSET(NBIO, 0, mmBIF_MMSCH1_DOORBELL_RANGE) :
		SOC15_REG_OFFSET(NBIO, 0, mmBIF_MMSCH0_DOORBELL_RANGE);

	u32 doorbell_range = RREG32(reg);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       BIF_MMSCH0_DOORBELL_RANGE, OFFSET,
					       doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       BIF_MMSCH0_DOORBELL_RANGE, SIZE, 8);
	} else
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       BIF_MMSCH0_DOORBELL_RANGE, SIZE, 0);

	WREG32(reg, doorbell_range);
}

static void nbio_v2_3_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	WREG32_FIELD15(NBIO, 0, RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN,
		       enable ? 1 : 0);
}

static void nbio_v2_3_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
							bool enable)
{
	u32 tmp = 0;

	if (enable) {
		tmp = REG_SET_FIELD(tmp, BIF_BX_PF_DOORBELL_SELFRING_GPA_APER_CNTL,
				    DOORBELL_SELFRING_GPA_APER_EN, 1) |
		      REG_SET_FIELD(tmp, BIF_BX_PF_DOORBELL_SELFRING_GPA_APER_CNTL,
				    DOORBELL_SELFRING_GPA_APER_MODE, 1) |
		      REG_SET_FIELD(tmp, BIF_BX_PF_DOORBELL_SELFRING_GPA_APER_CNTL,
				    DOORBELL_SELFRING_GPA_APER_SIZE, 0);

		WREG32_SOC15(NBIO, 0, mmBIF_BX_PF_DOORBELL_SELFRING_GPA_APER_BASE_LOW,
			     lower_32_bits(adev->doorbell.base));
		WREG32_SOC15(NBIO, 0, mmBIF_BX_PF_DOORBELL_SELFRING_GPA_APER_BASE_HIGH,
			     upper_32_bits(adev->doorbell.base));
	}

	WREG32_SOC15(NBIO, 0, mmBIF_BX_PF_DOORBELL_SELFRING_GPA_APER_CNTL,
		     tmp);
}


static void nbio_v2_3_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_SOC15(NBIO, 0, mmBIF_IH_DOORBELL_RANGE);

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  BIF_IH_DOORBELL_RANGE, OFFSET,
						  doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  BIF_IH_DOORBELL_RANGE, SIZE,
						  2);
	} else
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  BIF_IH_DOORBELL_RANGE, SIZE,
						  0);

	WREG32_SOC15(NBIO, 0, mmBIF_IH_DOORBELL_RANGE, ih_doorbell_range);
}

static void nbio_v2_3_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL2, adev->dummy_page_addr >> 8);

	interrupt_cntl = RREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL);
	/*
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL,
				       IH_DUMMY_RD_OVERRIDE, 0);

	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL,
				       IH_REQ_NONSNOOP_EN, 0);

	WREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL, interrupt_cntl);
}

static void nbio_v2_3_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG))
		return;

	def = data = RREG32_PCIE(smnCPM_CONTROL);
	if (enable) {
		data |= (CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_DYN_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_LCNT_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_REGS_GATE_ENABLE_MASK |
			 CPM_CONTROL__TXCLK_PRBS_GATE_ENABLE_MASK |
			 CPM_CONTROL__REFCLK_REGS_GATE_ENABLE_MASK);
	} else {
		data &= ~(CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_DYN_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_LCNT_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_REGS_GATE_ENABLE_MASK |
			  CPM_CONTROL__TXCLK_PRBS_GATE_ENABLE_MASK |
			  CPM_CONTROL__REFCLK_REGS_GATE_ENABLE_MASK);
	}

	if (def != data)
		WREG32_PCIE(smnCPM_CONTROL, data);
}

static void nbio_v2_3_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_BIF_LS))
		return;

	def = data = RREG32_PCIE(smnPCIE_CNTL2);
	if (enable) {
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

static void nbio_v2_3_get_clockgating_state(struct amdgpu_device *adev,
					    u32 *flags)
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

static u32 nbio_v2_3_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF_GPU_HDP_FLUSH_REQ);
}

static u32 nbio_v2_3_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF_GPU_HDP_FLUSH_DONE);
}

static u32 nbio_v2_3_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmPCIE_INDEX2);
}

static u32 nbio_v2_3_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmPCIE_DATA2);
}

const struct nbio_hdp_flush_reg nbio_v2_3_hdp_flush_reg = {
	.ref_and_mask_cp0 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP0_MASK,
	.ref_and_mask_cp1 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP1_MASK,
	.ref_and_mask_cp2 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP2_MASK,
	.ref_and_mask_cp3 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP3_MASK,
	.ref_and_mask_cp4 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP4_MASK,
	.ref_and_mask_cp5 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP5_MASK,
	.ref_and_mask_cp6 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP6_MASK,
	.ref_and_mask_cp7 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP7_MASK,
	.ref_and_mask_cp8 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP8_MASK,
	.ref_and_mask_cp9 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP9_MASK,
	.ref_and_mask_sdma0 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__SDMA0_MASK,
	.ref_and_mask_sdma1 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__SDMA1_MASK,
};

const struct nbio_hdp_flush_reg nbio_v2_3_hdp_flush_reg_sc = {
	.ref_and_mask_cp0 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP0_MASK,
	.ref_and_mask_cp1 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP1_MASK,
	.ref_and_mask_cp2 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP2_MASK,
	.ref_and_mask_cp3 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP3_MASK,
	.ref_and_mask_cp4 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP4_MASK,
	.ref_and_mask_cp5 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP5_MASK,
	.ref_and_mask_cp6 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP6_MASK,
	.ref_and_mask_cp7 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP7_MASK,
	.ref_and_mask_cp8 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP8_MASK,
	.ref_and_mask_cp9 = BIF_BX_PF_GPU_HDP_FLUSH_DONE__CP9_MASK,
	.ref_and_mask_sdma0 = GPU_HDP_FLUSH_DONE__RSVD_ENG1_MASK,
	.ref_and_mask_sdma1 = GPU_HDP_FLUSH_DONE__RSVD_ENG2_MASK,
	.ref_and_mask_sdma2 = GPU_HDP_FLUSH_DONE__RSVD_ENG3_MASK,
	.ref_and_mask_sdma3 = GPU_HDP_FLUSH_DONE__RSVD_ENG4_MASK,
	.ref_and_mask_sdma4 = GPU_HDP_FLUSH_DONE__RSVD_ENG5_MASK,
	.ref_and_mask_sdma5 = GPU_HDP_FLUSH_DONE__RSVD_ENG6_MASK,
	.ref_and_mask_sdma6 = GPU_HDP_FLUSH_DONE__RSVD_ENG7_MASK,
	.ref_and_mask_sdma7 = GPU_HDP_FLUSH_DONE__RSVD_ENG8_MASK,
};

static void nbio_v2_3_init_registers(struct amdgpu_device *adev)
{
	uint32_t def, data;

	def = data = RREG32_PCIE(smnPCIE_CONFIG_CNTL);
	data = REG_SET_FIELD(data, PCIE_CONFIG_CNTL, CI_SWUS_MAX_READ_REQUEST_SIZE_MODE, 1);
	data = REG_SET_FIELD(data, PCIE_CONFIG_CNTL, CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV, 1);

	if (def != data)
		WREG32_PCIE(smnPCIE_CONFIG_CNTL, data);
}

#define NAVI10_PCIE__LC_L0S_INACTIVITY_DEFAULT		0x00000000 // off by default, no gains over L1
#define NAVI10_PCIE__LC_L1_INACTIVITY_DEFAULT		0x00000009 // 1=1us, 9=1ms
#define NAVI10_PCIE__LC_L1_INACTIVITY_TBT_DEFAULT	0x0000000E // 4ms

static void nbio_v2_3_enable_aspm(struct amdgpu_device *adev,
				  bool enable)
{
	uint32_t def, data;

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL);

	if (enable) {
		/* Disable ASPM L0s/L1 first */
		data &= ~(PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK | PCIE_LC_CNTL__LC_L1_INACTIVITY_MASK);

		data |= NAVI10_PCIE__LC_L0S_INACTIVITY_DEFAULT << PCIE_LC_CNTL__LC_L0S_INACTIVITY__SHIFT;

		if (pci_is_thunderbolt_attached(adev->pdev))
			data |= NAVI10_PCIE__LC_L1_INACTIVITY_TBT_DEFAULT  << PCIE_LC_CNTL__LC_L1_INACTIVITY__SHIFT;
		else
			data |= NAVI10_PCIE__LC_L1_INACTIVITY_DEFAULT << PCIE_LC_CNTL__LC_L1_INACTIVITY__SHIFT;

		data &= ~PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	} else {
		/* Disbale ASPM L1 */
		data &= ~PCIE_LC_CNTL__LC_L1_INACTIVITY_MASK;
		/* Disable ASPM TxL0s */
		data &= ~PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK;
		/* Disable ACPI L1 */
		data |= PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	}

	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL, data);
}

static void nbio_v2_3_program_ltr(struct amdgpu_device *adev)
{
	uint32_t def, data;

	WREG32_PCIE(smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL, 0x75EB);

	def = data = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP2);
	data &= ~RCC_BIF_STRAP2__STRAP_LTR_IN_ASPML1_DIS_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP2, data);

	def = data = RREG32_PCIE(smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL);
	data &= ~EP_PCIE_TX_LTR_CNTL__LTR_PRIV_MSG_DIS_IN_PM_NON_D0_MASK;
	if (def != data)
		WREG32_PCIE(smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL, data);

	def = data = RREG32_PCIE(smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2);
	data |= BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK;
	if (def != data)
		WREG32_PCIE(smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2, data);
}

static void nbio_v2_3_program_aspm(struct amdgpu_device *adev)
{
	uint32_t def, data;

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL);
	data &= ~PCIE_LC_CNTL__LC_L1_INACTIVITY_MASK;
	data &= ~PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK;
	data |= PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL, data);

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL7);
	data |= PCIE_LC_CNTL7__LC_NBIF_ASPM_INPUT_EN_MASK;
	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL7, data);

	def = data = RREG32_PCIE(smnNBIF_MGCG_CTRL_LCLK);
	data |= NBIF_MGCG_CTRL_LCLK__NBIF_MGCG_REG_DIS_LCLK_MASK;
	if (def != data)
		WREG32_PCIE(smnNBIF_MGCG_CTRL_LCLK, data);

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL3);
	data |= PCIE_LC_CNTL3__LC_DSC_DONT_ENTER_L23_AFTER_PME_ACK_MASK;
	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL3, data);

	def = data = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP3);
	data &= ~RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER_MASK;
	data &= ~RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP3, data);

	def = data = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP5);
	data &= ~RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP5, data);

	def = data = RREG32_PCIE(smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2);
	data &= ~BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK;
	if (def != data)
		WREG32_PCIE(smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2, data);

	WREG32_PCIE(smnBIF_CFG_DEV0_EPF0_PCIE_LTR_CAP, 0x10011001);

	def = data = RREG32_PCIE(smnPSWUSP0_PCIE_LC_CNTL2);
	data |= PSWUSP0_PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L1_MASK |
		PSWUSP0_PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L23_MASK;
	data &= ~PSWUSP0_PCIE_LC_CNTL2__LC_RCV_L0_TO_RCV_L0S_DIS_MASK;
	if (def != data)
		WREG32_PCIE(smnPSWUSP0_PCIE_LC_CNTL2, data);

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL6);
	data |= PCIE_LC_CNTL6__LC_L1_POWERDOWN_MASK |
		PCIE_LC_CNTL6__LC_RX_L0S_STANDBY_EN_MASK;
	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL6, data);

	nbio_v2_3_program_ltr(adev);

	def = data = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP3);
	data |= 0x5DE0 << RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER__SHIFT;
	data |= 0x0010 << RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER__SHIFT;
	if (def != data)
		WREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP3, data);

	def = data = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP5);
	data |= 0x0010 << RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER__SHIFT;
	if (def != data)
		WREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP5, data);

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL);
	data &= ~PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK;
	data |= 0x9 << PCIE_LC_CNTL__LC_L1_INACTIVITY__SHIFT;
	data |= 0x1 << PCIE_LC_CNTL__LC_PMI_TO_L1_DIS__SHIFT;
	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL, data);

	def = data = RREG32_PCIE(smnPCIE_LC_CNTL3);
	data &= ~PCIE_LC_CNTL3__LC_DSC_DONT_ENTER_L23_AFTER_PME_ACK_MASK;
	if (def != data)
		WREG32_PCIE(smnPCIE_LC_CNTL3, data);
}

static void nbio_v2_3_apply_lc_spc_mode_wa(struct amdgpu_device *adev)
{
	uint32_t reg_data = 0;
	uint32_t link_width = 0;

	if (!((adev->asic_type >= CHIP_NAVI10) &&
	     (adev->asic_type <= CHIP_NAVI12)))
		return;

	reg_data = RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL);
	link_width = (reg_data & PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
		>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;

	/*
	 * Program PCIE_LC_CNTL6.LC_SPC_MODE_8GT to 0x2 (4 symbols per clock data)
	 * if link_width is 0x3 (x4)
	 */
	if (0x3 == link_width) {
		reg_data = RREG32_PCIE(smnPCIE_LC_CNTL6);
		reg_data &= ~PCIE_LC_CNTL6__LC_SPC_MODE_8GT_MASK;
		reg_data |= (0x2 << PCIE_LC_CNTL6__LC_SPC_MODE_8GT__SHIFT);
		WREG32_PCIE(smnPCIE_LC_CNTL6, reg_data);
	}
}

static void nbio_v2_3_apply_l1_link_width_reconfig_wa(struct amdgpu_device *adev)
{
	uint32_t reg_data = 0;

	if (adev->asic_type != CHIP_NAVI10)
		return;

	reg_data = RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL);
	reg_data |= PCIE_LC_LINK_WIDTH_CNTL__LC_L1_RECONFIG_EN_MASK;
	WREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL, reg_data);
}

static void nbio_v2_3_clear_doorbell_interrupt(struct amdgpu_device *adev)
{
	uint32_t reg, reg_data;

	if (adev->asic_type != CHIP_SIENNA_CICHLID)
		return;

	reg = RREG32_SOC15(NBIO, 0, mmBIF_RB_CNTL);

	/* Clear Interrupt Status
	 */
	if ((reg & BIF_RB_CNTL__RB_ENABLE_MASK) == 0) {
		reg = RREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL);
		if (reg & BIF_DOORBELL_INT_CNTL__DOORBELL_INTERRUPT_STATUS_MASK) {
			reg_data = 1 << BIF_DOORBELL_INT_CNTL__DOORBELL_INTERRUPT_CLEAR__SHIFT;
			WREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL, reg_data);
		}
	}
}

const struct amdgpu_nbio_funcs nbio_v2_3_funcs = {
	.get_hdp_flush_req_offset = nbio_v2_3_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v2_3_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v2_3_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v2_3_get_pcie_data_offset,
	.get_rev_id = nbio_v2_3_get_rev_id,
	.mc_access_enable = nbio_v2_3_mc_access_enable,
	.get_memsize = nbio_v2_3_get_memsize,
	.sdma_doorbell_range = nbio_v2_3_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v2_3_vcn_doorbell_range,
	.enable_doorbell_aperture = nbio_v2_3_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v2_3_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v2_3_ih_doorbell_range,
	.update_medium_grain_clock_gating = nbio_v2_3_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v2_3_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v2_3_get_clockgating_state,
	.ih_control = nbio_v2_3_ih_control,
	.init_registers = nbio_v2_3_init_registers,
	.remap_hdp_registers = nbio_v2_3_remap_hdp_registers,
	.enable_aspm = nbio_v2_3_enable_aspm,
	.program_aspm =  nbio_v2_3_program_aspm,
	.apply_lc_spc_mode_wa = nbio_v2_3_apply_lc_spc_mode_wa,
	.apply_l1_link_width_reconfig_wa = nbio_v2_3_apply_l1_link_width_reconfig_wa,
	.clear_doorbell_interrupt = nbio_v2_3_clear_doorbell_interrupt,
};

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

#include "nbio/nbio_6_1_default.h"
#include "nbio/nbio_6_1_offset.h"
#include "nbio/nbio_6_1_sh_mask.h"
#include "nbio/nbio_6_1_smn.h"
#include "vega10_enum.h"
#include <uapi/linux/kfd_ioctl.h>

#define smnPCIE_LC_CNTL		0x11140280
#define smnPCIE_LC_CNTL3	0x111402d4
#define smnPCIE_LC_CNTL6	0x111402ec
#define smnPCIE_LC_CNTL7	0x111402f0
#define smnNBIF_MGCG_CTRL_LCLK	0x1013a05c
#define NBIF_MGCG_CTRL_LCLK__NBIF_MGCG_REG_DIS_LCLK_MASK	0x00001000L
#define RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER_MASK	0x0000FFFFL
#define RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER_MASK	0xFFFF0000L
#define smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL	0x10123530
#define smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2	0x1014008c
#define smnBIF_CFG_DEV0_EPF0_PCIE_LTR_CAP	0x10140324
#define smnPSWUSP0_PCIE_LC_CNTL2		0x111402c4
#define smnRCC_BIF_STRAP2	0x10123488
#define smnRCC_BIF_STRAP3	0x1012348c
#define smnRCC_BIF_STRAP5	0x10123494
#define BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK			0x0400L
#define RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER_MASK	0x0000FFFFL
#define RCC_BIF_STRAP2__STRAP_LTR_IN_ASPML1_DIS_MASK	0x00004000L
#define RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER__SHIFT	0x0
#define RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER__SHIFT	0x10
#define RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER__SHIFT	0x0

static void nbio_v6_1_remap_hdp_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, mmREMAP_HDP_MEM_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
	WREG32_SOC15(NBIO, 0, mmREMAP_HDP_REG_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v6_1_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SOC15(NBIO, 0, mmRCC_DEV0_EPF0_STRAP0);

	tmp &= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v6_1_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN,
			     BIF_FB_EN__FB_READ_EN_MASK |
			     BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN, 0);
}

static u32 nbio_v6_1_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, mmRCC_PF_0_0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v6_1_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
			bool use_doorbell, int doorbell_index, int doorbell_size)
{
	u32 reg = instance == 0 ? SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA0_DOORBELL_RANGE) :
			SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA1_DOORBELL_RANGE);

	u32 doorbell_range = RREG32(reg);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, OFFSET, doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, SIZE, doorbell_size);
	} else
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, SIZE, 0);

	WREG32(reg, doorbell_range);

}

static void nbio_v6_1_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	WREG32_FIELD15(NBIO, 0, RCC_PF_0_0_RCC_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

static void nbio_v6_1_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
							bool enable)
{
	u32 tmp = 0;

	if (enable) {
		tmp = REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_EN, 1) |
		      REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_MODE, 1) |
		      REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_SIZE, 0);

		WREG32_SOC15(NBIO, 0, mmBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_LOW,
			     lower_32_bits(adev->doorbell.base));
		WREG32_SOC15(NBIO, 0, mmBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_HIGH,
			     upper_32_bits(adev->doorbell.base));
	}

	WREG32_SOC15(NBIO, 0, mmBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, tmp);
}


static void nbio_v6_1_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_SOC15(NBIO, 0, mmBIF_IH_DOORBELL_RANGE);

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, OFFSET, doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  BIF_IH_DOORBELL_RANGE, SIZE, 6);
	} else
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 0);

	WREG32_SOC15(NBIO, 0, mmBIF_IH_DOORBELL_RANGE, ih_doorbell_range);
}

static void nbio_v6_1_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL2, adev->dummy_page_addr >> 8);
	interrupt_cntl = RREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL);
	/* INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_DUMMY_RD_OVERRIDE, 0);
	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_REQ_NONSNOOP_EN, 0);
	WREG32_SOC15(NBIO, 0, mmINTERRUPT_CNTL, interrupt_cntl);
}

static void nbio_v6_1_update_medium_grain_clock_gating(struct amdgpu_device *adev,
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

static void nbio_v6_1_update_medium_grain_light_sleep(struct amdgpu_device *adev,
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

static void nbio_v6_1_get_clockgating_state(struct amdgpu_device *adev,
					    u64 *flags)
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

static u32 nbio_v6_1_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_GPU_HDP_FLUSH_REQ);
}

static u32 nbio_v6_1_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmBIF_BX_PF0_GPU_HDP_FLUSH_DONE);
}

static u32 nbio_v6_1_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmPCIE_INDEX2);
}

static u32 nbio_v6_1_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmPCIE_DATA2);
}

const struct nbio_hdp_flush_reg nbio_v6_1_hdp_flush_reg = {
	.ref_and_mask_cp0 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP0_MASK,
	.ref_and_mask_cp1 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP1_MASK,
	.ref_and_mask_cp2 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP2_MASK,
	.ref_and_mask_cp3 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP3_MASK,
	.ref_and_mask_cp4 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP4_MASK,
	.ref_and_mask_cp5 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP5_MASK,
	.ref_and_mask_cp6 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP6_MASK,
	.ref_and_mask_cp7 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP7_MASK,
	.ref_and_mask_cp8 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP8_MASK,
	.ref_and_mask_cp9 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__CP9_MASK,
	.ref_and_mask_sdma0 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__SDMA0_MASK,
	.ref_and_mask_sdma1 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__SDMA1_MASK
};

static void nbio_v6_1_init_registers(struct amdgpu_device *adev)
{
	uint32_t def, data;

	def = data = RREG32_PCIE(smnPCIE_CONFIG_CNTL);
	data = REG_SET_FIELD(data, PCIE_CONFIG_CNTL, CI_SWUS_MAX_READ_REQUEST_SIZE_MODE, 1);
	data = REG_SET_FIELD(data, PCIE_CONFIG_CNTL, CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV, 1);

	if (def != data)
		WREG32_PCIE(smnPCIE_CONFIG_CNTL, data);

	def = data = RREG32_PCIE(smnPCIE_CI_CNTL);
	data = REG_SET_FIELD(data, PCIE_CI_CNTL, CI_SLV_ORDERING_DIS, 1);

	if (def != data)
		WREG32_PCIE(smnPCIE_CI_CNTL, data);

	if (amdgpu_sriov_vf(adev))
		adev->rmmio_remap.reg_offset = SOC15_REG_OFFSET(NBIO, 0,
			mmBIF_BX_DEV0_EPF0_VF0_HDP_MEM_COHERENCY_FLUSH_CNTL) << 2;
}

#ifdef CONFIG_PCIEASPM
static void nbio_v6_1_program_ltr(struct amdgpu_device *adev)
{
	uint32_t def, data;

	WREG32_PCIE(smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL, 0x75EB);

	def = data = RREG32_PCIE(smnRCC_BIF_STRAP2);
	data &= ~RCC_BIF_STRAP2__STRAP_LTR_IN_ASPML1_DIS_MASK;
	if (def != data)
		WREG32_PCIE(smnRCC_BIF_STRAP2, data);

	def = data = RREG32_PCIE(smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL);
	data &= ~EP_PCIE_TX_LTR_CNTL__LTR_PRIV_MSG_DIS_IN_PM_NON_D0_MASK;
	if (def != data)
		WREG32_PCIE(smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL, data);

	def = data = RREG32_PCIE(smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2);
	data |= BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK;
	if (def != data)
		WREG32_PCIE(smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2, data);
}
#endif

static void nbio_v6_1_program_aspm(struct amdgpu_device *adev)
{
#ifdef CONFIG_PCIEASPM
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

	def = data = RREG32_PCIE(smnRCC_BIF_STRAP3);
	data &= ~RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER_MASK;
	data &= ~RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER_MASK;
	if (def != data)
		WREG32_PCIE(smnRCC_BIF_STRAP3, data);

	def = data = RREG32_PCIE(smnRCC_BIF_STRAP5);
	data &= ~RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER_MASK;
	if (def != data)
		WREG32_PCIE(smnRCC_BIF_STRAP5, data);

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

	/* Don't bother about LTR if LTR is not enabled
	 * in the path */
	if (adev->pdev->ltr_path)
		nbio_v6_1_program_ltr(adev);

	def = data = RREG32_PCIE(smnRCC_BIF_STRAP3);
	data |= 0x5DE0 << RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER__SHIFT;
	data |= 0x0010 << RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER__SHIFT;
	if (def != data)
		WREG32_PCIE(smnRCC_BIF_STRAP3, data);

	def = data = RREG32_PCIE(smnRCC_BIF_STRAP5);
	data |= 0x0010 << RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER__SHIFT;
	if (def != data)
		WREG32_PCIE(smnRCC_BIF_STRAP5, data);

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
#endif
}

const struct amdgpu_nbio_funcs nbio_v6_1_funcs = {
	.get_hdp_flush_req_offset = nbio_v6_1_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v6_1_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v6_1_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v6_1_get_pcie_data_offset,
	.get_rev_id = nbio_v6_1_get_rev_id,
	.mc_access_enable = nbio_v6_1_mc_access_enable,
	.get_memsize = nbio_v6_1_get_memsize,
	.sdma_doorbell_range = nbio_v6_1_sdma_doorbell_range,
	.enable_doorbell_aperture = nbio_v6_1_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v6_1_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v6_1_ih_doorbell_range,
	.update_medium_grain_clock_gating = nbio_v6_1_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v6_1_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v6_1_get_clockgating_state,
	.ih_control = nbio_v6_1_ih_control,
	.init_registers = nbio_v6_1_init_registers,
	.remap_hdp_registers = nbio_v6_1_remap_hdp_registers,
	.program_aspm =  nbio_v6_1_program_aspm,
};

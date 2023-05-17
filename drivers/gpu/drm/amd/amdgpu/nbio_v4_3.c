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
#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "nbio_v4_3.h"

#include "nbio/nbio_4_3_0_offset.h"
#include "nbio/nbio_4_3_0_sh_mask.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"
#include <uapi/linux/kfd_ioctl.h>

static void nbio_v4_3_remap_hdp_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regBIF_BX0_REMAP_HDP_MEM_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
	WREG32_SOC15(NBIO, 0, regBIF_BX0_REMAP_HDP_REG_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v4_3_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0);

	tmp &= RCC_STRAP0_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_STRAP0_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v4_3_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN,
			     BIF_BX0_BIF_FB_EN__FB_READ_EN_MASK |
			     BIF_BX0_BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN, 0);
}

static u32 nbio_v4_3_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v4_3_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
					  bool use_doorbell, int doorbell_index,
					  int doorbell_size)
{
	if (instance == 0) {
		u32 doorbell_range = RREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_2_CTRL);

		if (use_doorbell) {
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_ENABLE,
						       0x1);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_AWID,
						       0xe);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_RANGE_OFFSET,
						       doorbell_index);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_RANGE_SIZE,
						       doorbell_size);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_AWADDR_31_28_VALUE,
						       0x3);
		} else
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_RANGE_SIZE,
						       0);

		WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_2_CTRL, doorbell_range);
	}
}

static void nbio_v4_3_vcn_doorbell_range(struct amdgpu_device *adev, bool use_doorbell,
					 int doorbell_index, int instance)
{
	u32 doorbell_range;

	if (instance)
		doorbell_range = RREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_5_CTRL);
	else
		doorbell_range = RREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_4_CTRL);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       S2A_DOORBELL_ENTRY_4_CTRL,
					       S2A_DOORBELL_PORT4_ENABLE,
					       0x1);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       S2A_DOORBELL_ENTRY_4_CTRL,
					       S2A_DOORBELL_PORT4_AWID,
					       instance ? 0x7 : 0x4);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       S2A_DOORBELL_ENTRY_4_CTRL,
					       S2A_DOORBELL_PORT4_RANGE_OFFSET,
					       doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       S2A_DOORBELL_ENTRY_4_CTRL,
					       S2A_DOORBELL_PORT4_RANGE_SIZE,
					       8);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       S2A_DOORBELL_ENTRY_4_CTRL,
					       S2A_DOORBELL_PORT4_AWADDR_31_28_VALUE,
					       instance ? 0x7 : 0x4);
	} else
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       S2A_DOORBELL_ENTRY_4_CTRL,
					       S2A_DOORBELL_PORT4_RANGE_SIZE,
					       0);

	if (instance)
		WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_5_CTRL, doorbell_range);
	else
		WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_4_CTRL, doorbell_range);
}

static void nbio_v4_3_gc_doorbell_init(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_0_CTRL, 0x30000007);
	WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_3_CTRL, 0x3000000d);
}

static void nbio_v4_3_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	WREG32_FIELD15_PREREG(NBIO, 0, RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN,
			BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

static void nbio_v4_3_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
							bool enable)
{
	u32 tmp = 0;

	if (enable) {
		tmp = REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL,
				    DOORBELL_SELFRING_GPA_APER_EN, 1) |
		      REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL,
				    DOORBELL_SELFRING_GPA_APER_MODE, 1) |
		      REG_SET_FIELD(tmp, BIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL,
				    DOORBELL_SELFRING_GPA_APER_SIZE, 0);

		WREG32_SOC15(NBIO, 0, regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_LOW,
			     lower_32_bits(adev->doorbell.base));
		WREG32_SOC15(NBIO, 0, regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_HIGH,
			     upper_32_bits(adev->doorbell.base));
	}

	WREG32_SOC15(NBIO, 0, regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL,
		     tmp);
}

static void nbio_v4_3_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_1_CTRL);

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_ENABLE,
						  0x1);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_AWID,
						  0x0);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_RANGE_OFFSET,
						  doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_RANGE_SIZE,
						  2);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
						  0x0);
	} else
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_RANGE_SIZE,
						  0);

	WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_1_CTRL, ih_doorbell_range);
}

static void nbio_v4_3_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL2, adev->dummy_page_addr >> 8);

	interrupt_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL);
	/*
	 * BIF_BX0_INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * BIF_BX0_INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX0_INTERRUPT_CNTL,
				       IH_DUMMY_RD_OVERRIDE, 0);

	/* BIF_BX0_INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX0_INTERRUPT_CNTL,
				       IH_REQ_NONSNOOP_EN, 0);

	WREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL, interrupt_cntl);
}

static void nbio_v4_3_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t def, data;

	if (enable && !(adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG))
		return;

	def = data = RREG32_SOC15(NBIO, 0, regCPM_CONTROL);
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
		WREG32_SOC15(NBIO, 0, regCPM_CONTROL, data);
}

static void nbio_v4_3_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t def, data;

	if (enable && !(adev->cg_flags & AMD_CG_SUPPORT_BIF_LS))
		return;

	/* TODO: need update in future */
	def = data = RREG32_SOC15(NBIO, 0, regPCIE_CNTL2);
	if (enable) {
		data |= PCIE_CNTL2__SLV_MEM_LS_EN_MASK;
	} else {
		data &= ~PCIE_CNTL2__SLV_MEM_LS_EN_MASK;
	}

	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_CNTL2, data);
}

static void nbio_v4_3_get_clockgating_state(struct amdgpu_device *adev,
					    u64 *flags)
{
	int data;

	/* AMD_CG_SUPPORT_BIF_MGCG */
	data = RREG32_SOC15(NBIO, 0, regCPM_CONTROL);
	if (data & CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_MGCG;

	/* AMD_CG_SUPPORT_BIF_LS */
	data = RREG32_SOC15(NBIO, 0, regPCIE_CNTL2);
	if (data & PCIE_CNTL2__SLV_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_LS;
}

static u32 nbio_v4_3_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_REQ);
}

static u32 nbio_v4_3_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_DONE);
}

static u32 nbio_v4_3_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_RSMU_INDEX);
}

static u32 nbio_v4_3_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_RSMU_DATA);
}

const struct nbio_hdp_flush_reg nbio_v4_3_hdp_flush_reg = {
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

static void nbio_v4_3_init_registers(struct amdgpu_device *adev)
{
	if (adev->ip_versions[NBIO_HWIP][0] == IP_VERSION(4, 3, 0)) {
		uint32_t data;

		data = RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF2_STRAP2);
		data &= ~RCC_DEV0_EPF2_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F2_MASK;
		WREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF2_STRAP2, data);
	}
}

static u32 nbio_v4_3_get_rom_offset(struct amdgpu_device *adev)
{
	u32 data, rom_offset;

	data = RREG32_SOC15(NBIO, 0, regREGS_ROM_OFFSET_CTRL);
	rom_offset = REG_GET_FIELD(data, REGS_ROM_OFFSET_CTRL, ROM_OFFSET);

	return rom_offset;
}

#ifdef CONFIG_PCIEASPM
static void nbio_v4_3_program_ltr(struct amdgpu_device *adev)
{
	uint32_t def, data;

	def = RREG32_SOC15(NBIO, 0, regRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL);
	data = 0x35EB;
	data &= ~EP_PCIE_TX_LTR_CNTL__LTR_PRIV_MSG_DIS_IN_PM_NON_D0_MASK;
	data &= ~EP_PCIE_TX_LTR_CNTL__LTR_PRIV_RST_LTR_IN_DL_DOWN_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL, data);

	def = data = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP2);
	data &= ~RCC_BIF_STRAP2__STRAP_LTR_IN_ASPML1_DIS_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP2, data);

	def = data = RREG32_SOC15(NBIO, 0, regBIF_CFG_DEV0_EPF0_DEVICE_CNTL2);
	if (adev->pdev->ltr_path)
		data |= BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK;
	else
		data &= ~BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regBIF_CFG_DEV0_EPF0_DEVICE_CNTL2, data);
}
#endif

static void nbio_v4_3_program_aspm(struct amdgpu_device *adev)
{
#ifdef CONFIG_PCIEASPM
	uint32_t def, data;

	if (!(adev->ip_versions[PCIE_HWIP][0] == IP_VERSION(7, 4, 0)) &&
	      !(adev->ip_versions[PCIE_HWIP][0] == IP_VERSION(7, 6, 0)))
		return;

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL);
	data &= ~PCIE_LC_CNTL__LC_L1_INACTIVITY_MASK;
	data &= ~PCIE_LC_CNTL__LC_L0S_INACTIVITY_MASK;
	data |= PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL, data);

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL7);
	data |= PCIE_LC_CNTL7__LC_NBIF_ASPM_INPUT_EN_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL7, data);

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL3);
	data |= PCIE_LC_CNTL3__LC_DSC_DONT_ENTER_L23_AFTER_PME_ACK_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL3, data);

	def = data = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP3);
	data &= ~RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER_MASK;
	data &= ~RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP3, data);

	def = data = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP5);
	data &= ~RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP5, data);

	def = data = RREG32_SOC15(NBIO, 0, regBIF_CFG_DEV0_EPF0_DEVICE_CNTL2);
	data &= ~BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regBIF_CFG_DEV0_EPF0_DEVICE_CNTL2, data);

	WREG32_SOC15(NBIO, 0, regBIF_CFG_DEV0_EPF0_PCIE_LTR_CAP, 0x10011001);

	def = data = RREG32_SOC15(NBIO, 0, regPSWUSP0_PCIE_LC_CNTL2);
	data |= PSWUSP0_PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L1_MASK |
		PSWUSP0_PCIE_LC_CNTL2__LC_ALLOW_PDWN_IN_L23_MASK;
	data &= ~PSWUSP0_PCIE_LC_CNTL2__LC_RCV_L0_TO_RCV_L0S_DIS_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPSWUSP0_PCIE_LC_CNTL2, data);

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL4);
	data |= PCIE_LC_CNTL4__LC_L1_POWERDOWN_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL4, data);

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_RXRECOVER_RXSTANDBY_CNTL);
	data |= PCIE_LC_RXRECOVER_RXSTANDBY_CNTL__LC_RX_L0S_STANDBY_EN_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_RXRECOVER_RXSTANDBY_CNTL, data);

	nbio_v4_3_program_ltr(adev);

	def = data = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP3);
	data |= 0x5DE0 << RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER__SHIFT;
	data |= 0x0010 << RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER__SHIFT;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP3, data);

	def = data = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP5);
	data |= 0x0010 << RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER__SHIFT;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_BIF_STRAP5, data);

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL);
	data |= 0x0 << PCIE_LC_CNTL__LC_L0S_INACTIVITY__SHIFT;
	data |= 0x9 << PCIE_LC_CNTL__LC_L1_INACTIVITY__SHIFT;
	data &= ~PCIE_LC_CNTL__LC_PMI_TO_L1_DIS_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL, data);

	def = data = RREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL3);
	data &= ~PCIE_LC_CNTL3__LC_DSC_DONT_ENTER_L23_AFTER_PME_ACK_MASK;
	if (def != data)
		WREG32_SOC15(NBIO, 0, regPCIE_LC_CNTL3, data);
#endif
}

const struct amdgpu_nbio_funcs nbio_v4_3_funcs = {
	.get_hdp_flush_req_offset = nbio_v4_3_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v4_3_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v4_3_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v4_3_get_pcie_data_offset,
	.get_rev_id = nbio_v4_3_get_rev_id,
	.mc_access_enable = nbio_v4_3_mc_access_enable,
	.get_memsize = nbio_v4_3_get_memsize,
	.sdma_doorbell_range = nbio_v4_3_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v4_3_vcn_doorbell_range,
	.gc_doorbell_init = nbio_v4_3_gc_doorbell_init,
	.enable_doorbell_aperture = nbio_v4_3_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v4_3_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v4_3_ih_doorbell_range,
	.update_medium_grain_clock_gating = nbio_v4_3_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v4_3_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v4_3_get_clockgating_state,
	.ih_control = nbio_v4_3_ih_control,
	.init_registers = nbio_v4_3_init_registers,
	.remap_hdp_registers = nbio_v4_3_remap_hdp_registers,
	.get_rom_offset = nbio_v4_3_get_rom_offset,
	.program_aspm = nbio_v4_3_program_aspm,
};


static void nbio_v4_3_sriov_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
}

static void nbio_v4_3_sriov_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
					  bool use_doorbell, int doorbell_index,
					  int doorbell_size)
{
}

static void nbio_v4_3_sriov_vcn_doorbell_range(struct amdgpu_device *adev, bool use_doorbell,
					 int doorbell_index, int instance)
{
}

static void nbio_v4_3_sriov_gc_doorbell_init(struct amdgpu_device *adev)
{
}

const struct amdgpu_nbio_funcs nbio_v4_3_sriov_funcs = {
	.get_hdp_flush_req_offset = nbio_v4_3_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v4_3_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v4_3_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v4_3_get_pcie_data_offset,
	.get_rev_id = nbio_v4_3_get_rev_id,
	.mc_access_enable = nbio_v4_3_mc_access_enable,
	.get_memsize = nbio_v4_3_get_memsize,
	.sdma_doorbell_range = nbio_v4_3_sriov_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v4_3_sriov_vcn_doorbell_range,
	.gc_doorbell_init = nbio_v4_3_sriov_gc_doorbell_init,
	.enable_doorbell_aperture = nbio_v4_3_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v4_3_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v4_3_sriov_ih_doorbell_range,
	.update_medium_grain_clock_gating = nbio_v4_3_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v4_3_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v4_3_get_clockgating_state,
	.ih_control = nbio_v4_3_ih_control,
	.init_registers = nbio_v4_3_init_registers,
	.remap_hdp_registers = nbio_v4_3_remap_hdp_registers,
	.get_rom_offset = nbio_v4_3_get_rom_offset,
};

static int nbio_v4_3_set_ras_err_event_athub_irq_state(struct amdgpu_device *adev,
						       struct amdgpu_irq_src *src,
						       unsigned type,
						       enum amdgpu_interrupt_state state)
{
	/* The ras_controller_irq enablement should be done in psp bl when it
	 * tries to enable ras feature. Driver only need to set the correct interrupt
	 * vector for bare-metal and sriov use case respectively
	 */
	uint32_t bif_doorbell_int_cntl;

	bif_doorbell_int_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL);
	bif_doorbell_int_cntl = REG_SET_FIELD(bif_doorbell_int_cntl,
					      BIF_BX0_BIF_DOORBELL_INT_CNTL,
					      RAS_ATHUB_ERR_EVENT_INTERRUPT_DISABLE,
					      (state == AMDGPU_IRQ_STATE_ENABLE) ? 0 : 1);
	WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL, bif_doorbell_int_cntl);

	return 0;
}

static int nbio_v4_3_process_err_event_athub_irq(struct amdgpu_device *adev,
						 struct amdgpu_irq_src *source,
						 struct amdgpu_iv_entry *entry)
{
	/* By design, the ih cookie for err_event_athub_irq should be written
	 * to bif ring. since bif ring is not enabled, just leave process callback
	 * as a dummy one.
	 */
	return 0;
}

static const struct amdgpu_irq_src_funcs nbio_v4_3_ras_err_event_athub_irq_funcs = {
	.set = nbio_v4_3_set_ras_err_event_athub_irq_state,
	.process = nbio_v4_3_process_err_event_athub_irq,
};

static void nbio_v4_3_handle_ras_err_event_athub_intr_no_bifring(struct amdgpu_device *adev)
{
	uint32_t bif_doorbell_int_cntl;

	bif_doorbell_int_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL);
	if (REG_GET_FIELD(bif_doorbell_int_cntl,
			  BIF_DOORBELL_INT_CNTL,
			  RAS_ATHUB_ERR_EVENT_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_int_cntl = REG_SET_FIELD(bif_doorbell_int_cntl,
						BIF_DOORBELL_INT_CNTL,
						RAS_ATHUB_ERR_EVENT_INTERRUPT_CLEAR, 1);
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL, bif_doorbell_int_cntl);
		amdgpu_ras_global_ras_isr(adev);
	}
}

static int nbio_v4_3_init_ras_err_event_athub_interrupt(struct amdgpu_device *adev)
{

	int r;

	/* init the irq funcs */
	adev->nbio.ras_err_event_athub_irq.funcs =
		&nbio_v4_3_ras_err_event_athub_irq_funcs;
	adev->nbio.ras_err_event_athub_irq.num_types = 1;

	/* register ras err event athub interrupt
	 * nbio v4_3 uses the same irq source as nbio v7_4 */
	r = amdgpu_irq_add_id(adev, SOC21_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__ERREVENT_ATHUB_INTERRUPT,
			      &adev->nbio.ras_err_event_athub_irq);

	return r;
}

struct amdgpu_nbio_ras nbio_v4_3_ras = {
	.handle_ras_err_event_athub_intr_no_bifring = nbio_v4_3_handle_ras_err_event_athub_intr_no_bifring,
	.init_ras_err_event_athub_interrupt = nbio_v4_3_init_ras_err_event_athub_interrupt,
};

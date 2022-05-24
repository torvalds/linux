/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "nbio_v7_2.h"

#include "nbio/nbio_7_2_0_offset.h"
#include "nbio/nbio_7_2_0_sh_mask.h"
#include <uapi/linux/kfd_ioctl.h>

#define regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0_YC				0x0015
#define regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0_YC_BASE_IDX		2
#define regBIF_BX0_BIF_FB_EN_YC								0x0100
#define regBIF_BX0_BIF_FB_EN_YC_BASE_IDX					2
#define regBIF1_PCIE_MST_CTRL_3								0x4601c6
#define regBIF1_PCIE_MST_CTRL_3_BASE_IDX					5
#define BIF1_PCIE_MST_CTRL_3__CI_SWUS_MAX_READ_REQUEST_SIZE_MODE__SHIFT \
			0x1b
#define BIF1_PCIE_MST_CTRL_3__CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV__SHIFT \
			0x1c
#define BIF1_PCIE_MST_CTRL_3__CI_SWUS_MAX_READ_REQUEST_SIZE_MODE_MASK \
			0x08000000L
#define BIF1_PCIE_MST_CTRL_3__CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV_MASK \
			0x30000000L
#define regBIF1_PCIE_TX_POWER_CTRL_1						0x460187
#define regBIF1_PCIE_TX_POWER_CTRL_1_BASE_IDX				5
#define BIF1_PCIE_TX_POWER_CTRL_1__MST_MEM_LS_EN_MASK		0x00000001L
#define BIF1_PCIE_TX_POWER_CTRL_1__REPLAY_MEM_LS_EN_MASK	0x00000008L

static void nbio_v7_2_remap_hdp_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regBIF_BX0_REMAP_HDP_MEM_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
	WREG32_SOC15(NBIO, 0, regBIF_BX0_REMAP_HDP_REG_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v7_2_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

	switch (adev->ip_versions[NBIO_HWIP][0]) {
	case IP_VERSION(7, 2, 1):
	case IP_VERSION(7, 3, 0):
	case IP_VERSION(7, 5, 0):
		tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0_YC);
		break;
	default:
		tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0);
		break;
	}

	tmp &= RCC_STRAP0_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_STRAP0_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v7_2_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	switch (adev->ip_versions[NBIO_HWIP][0]) {
	case IP_VERSION(7, 2, 1):
	case IP_VERSION(7, 3, 0):
	case IP_VERSION(7, 5, 0):
		if (enable)
			WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN_YC,
				BIF_BX0_BIF_FB_EN__FB_READ_EN_MASK |
				BIF_BX0_BIF_FB_EN__FB_WRITE_EN_MASK);
		else
			WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN_YC, 0);
	break;
	default:
		if (enable)
			WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN,
				BIF_BX0_BIF_FB_EN__FB_READ_EN_MASK |
				BIF_BX0_BIF_FB_EN__FB_WRITE_EN_MASK);
		else
			WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN, 0);
		break;
	}
}

static u32 nbio_v7_2_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v7_2_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
					  bool use_doorbell, int doorbell_index,
					  int doorbell_size)
{
	u32 reg = SOC15_REG_OFFSET(NBIO, 0, regGDC0_BIF_SDMA0_DOORBELL_RANGE);
	u32 doorbell_range = RREG32_PCIE_PORT(reg);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC0_BIF_SDMA0_DOORBELL_RANGE,
					       OFFSET, doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC0_BIF_SDMA0_DOORBELL_RANGE,
					       SIZE, doorbell_size);
	} else {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC0_BIF_SDMA0_DOORBELL_RANGE,
					       SIZE, 0);
	}

	WREG32_PCIE_PORT(reg, doorbell_range);
}

static void nbio_v7_2_vcn_doorbell_range(struct amdgpu_device *adev, bool use_doorbell,
					 int doorbell_index, int instance)
{
	u32 reg = SOC15_REG_OFFSET(NBIO, 0, regGDC0_BIF_VCN0_DOORBELL_RANGE);
	u32 doorbell_range = RREG32_PCIE_PORT(reg);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
							GDC0_BIF_VCN0_DOORBELL_RANGE, OFFSET,
							doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
							GDC0_BIF_VCN0_DOORBELL_RANGE, SIZE, 8);
	} else {
		doorbell_range = REG_SET_FIELD(doorbell_range,
							GDC0_BIF_VCN0_DOORBELL_RANGE, SIZE, 0);
	}

	WREG32_PCIE_PORT(reg, doorbell_range);
}

static void nbio_v7_2_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	u32 reg;

	reg = RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN);
	reg = REG_SET_FIELD(reg, RCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN,
			    BIF_DOORBELL_APER_EN, enable ? 1 : 0);

	WREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN, reg);
}

static void nbio_v7_2_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
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

		WREG32_SOC15(NBIO, 0,
			regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_LOW,
			lower_32_bits(adev->doorbell.base));
		WREG32_SOC15(NBIO, 0,
			regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_BASE_HIGH,
			upper_32_bits(adev->doorbell.base));
	}

	WREG32_SOC15(NBIO, 0, regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL,
		tmp);
}


static void nbio_v7_2_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regGDC0_BIF_IH_DOORBELL_RANGE));

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC0_BIF_IH_DOORBELL_RANGE, OFFSET,
						  doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC0_BIF_IH_DOORBELL_RANGE, SIZE,
						  2);
	} else {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC0_BIF_IH_DOORBELL_RANGE, SIZE,
						  0);
	}

	WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regGDC0_BIF_IH_DOORBELL_RANGE),
			 ih_doorbell_range);
}

static void nbio_v7_2_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL2,
		     adev->dummy_page_addr >> 8);

	interrupt_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL);
	/*
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX0_INTERRUPT_CNTL,
				       IH_DUMMY_RD_OVERRIDE, 0);

	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX0_INTERRUPT_CNTL,
				       IH_REQ_NONSNOOP_EN, 0);

	WREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL, interrupt_cntl);
}

static void nbio_v7_2_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t def, data;

	def = data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regCPM_CONTROL));
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_MGCG)) {
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
		WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regCPM_CONTROL), data);
}

static void nbio_v7_2_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t def, data;

	switch (adev->ip_versions[NBIO_HWIP][0]) {
	case IP_VERSION(7, 2, 1):
	case IP_VERSION(7, 3, 0):
	case IP_VERSION(7, 5, 0):
		def = data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CNTL2));
		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_LS))
			data |= PCIE_CNTL2__SLV_MEM_LS_EN_MASK;
		else
			data &= ~PCIE_CNTL2__SLV_MEM_LS_EN_MASK;

		if (def != data)
			WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CNTL2), data);

		def = data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0,
			regBIF1_PCIE_TX_POWER_CTRL_1));
		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_LS))
			data |= (BIF1_PCIE_TX_POWER_CTRL_1__MST_MEM_LS_EN_MASK |
				BIF1_PCIE_TX_POWER_CTRL_1__REPLAY_MEM_LS_EN_MASK);
		else
			data &= ~(BIF1_PCIE_TX_POWER_CTRL_1__MST_MEM_LS_EN_MASK |
				BIF1_PCIE_TX_POWER_CTRL_1__REPLAY_MEM_LS_EN_MASK);

		if (def != data)
			WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regBIF1_PCIE_TX_POWER_CTRL_1),
				data);
		break;
	default:
		def = data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CNTL2));
		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_BIF_LS))
			data |= (PCIE_CNTL2__SLV_MEM_LS_EN_MASK |
				 PCIE_CNTL2__MST_MEM_LS_EN_MASK |
				 PCIE_CNTL2__REPLAY_MEM_LS_EN_MASK);
		else
			data &= ~(PCIE_CNTL2__SLV_MEM_LS_EN_MASK |
				  PCIE_CNTL2__MST_MEM_LS_EN_MASK |
				  PCIE_CNTL2__REPLAY_MEM_LS_EN_MASK);

		if (def != data)
			WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CNTL2), data);
		break;
	}
}

static void nbio_v7_2_get_clockgating_state(struct amdgpu_device *adev,
					    u32 *flags)
{
	int data;

	/* AMD_CG_SUPPORT_BIF_MGCG */
	data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regCPM_CONTROL));
	if (data & CPM_CONTROL__LCLK_DYN_GATE_ENABLE_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_MGCG;

	/* AMD_CG_SUPPORT_BIF_LS */
	data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CNTL2));
	if (data & PCIE_CNTL2__SLV_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_BIF_LS;
}

static u32 nbio_v7_2_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_REQ);
}

static u32 nbio_v7_2_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_DONE);
}

static u32 nbio_v7_2_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_INDEX2);
}

static u32 nbio_v7_2_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_DATA2);
}

static u32 nbio_v7_2_get_pcie_port_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_RSMU_INDEX);
}

static u32 nbio_v7_2_get_pcie_port_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_RSMU_DATA);
}

const struct nbio_hdp_flush_reg nbio_v7_2_hdp_flush_reg = {
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
	.ref_and_mask_sdma1 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__SDMA1_MASK,
};

static void nbio_v7_2_init_registers(struct amdgpu_device *adev)
{
	uint32_t def, data;
	switch (adev->ip_versions[NBIO_HWIP][0]) {
	case IP_VERSION(7, 2, 1):
	case IP_VERSION(7, 3, 0):
	case IP_VERSION(7, 5, 0):
		def = data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regBIF1_PCIE_MST_CTRL_3));
		data = REG_SET_FIELD(data, BIF1_PCIE_MST_CTRL_3,
			CI_SWUS_MAX_READ_REQUEST_SIZE_MODE, 1);
		data = REG_SET_FIELD(data, BIF1_PCIE_MST_CTRL_3,
			CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV, 1);

		if (def != data)
			WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regBIF1_PCIE_MST_CTRL_3), data);
		break;
	default:
		def = data = RREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CONFIG_CNTL));
		data = REG_SET_FIELD(data, PCIE_CONFIG_CNTL,
			CI_SWUS_MAX_READ_REQUEST_SIZE_MODE, 1);
		data = REG_SET_FIELD(data, PCIE_CONFIG_CNTL,
			CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV, 1);

		if (def != data)
			WREG32_PCIE_PORT(SOC15_REG_OFFSET(NBIO, 0, regPCIE_CONFIG_CNTL), data);
		break;
	}

	if (amdgpu_sriov_vf(adev))
		adev->rmmio_remap.reg_offset = SOC15_REG_OFFSET(NBIO, 0,
			regBIF_BX_PF0_HDP_MEM_COHERENCY_FLUSH_CNTL) << 2;
}

const struct amdgpu_nbio_funcs nbio_v7_2_funcs = {
	.get_hdp_flush_req_offset = nbio_v7_2_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v7_2_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v7_2_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v7_2_get_pcie_data_offset,
	.get_pcie_port_index_offset = nbio_v7_2_get_pcie_port_index_offset,
	.get_pcie_port_data_offset = nbio_v7_2_get_pcie_port_data_offset,
	.get_rev_id = nbio_v7_2_get_rev_id,
	.mc_access_enable = nbio_v7_2_mc_access_enable,
	.get_memsize = nbio_v7_2_get_memsize,
	.sdma_doorbell_range = nbio_v7_2_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v7_2_vcn_doorbell_range,
	.enable_doorbell_aperture = nbio_v7_2_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v7_2_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v7_2_ih_doorbell_range,
	.update_medium_grain_clock_gating = nbio_v7_2_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v7_2_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v7_2_get_clockgating_state,
	.ih_control = nbio_v7_2_ih_control,
	.init_registers = nbio_v7_2_init_registers,
	.remap_hdp_registers = nbio_v7_2_remap_hdp_registers,
};

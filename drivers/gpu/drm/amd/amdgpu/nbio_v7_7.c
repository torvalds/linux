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
#include "nbio_v7_7.h"

#include "nbio/nbio_7_7_0_offset.h"
#include "nbio/nbio_7_7_0_sh_mask.h"
#include <uapi/linux/kfd_ioctl.h>

static u32 nbio_v7_7_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0);
	tmp &= RCC_STRAP0_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_STRAP0_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v7_7_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, regBIF_BX1_BIF_FB_EN,
			BIF_BX1_BIF_FB_EN__FB_READ_EN_MASK |
			BIF_BX1_BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, regBIF_BX1_BIF_FB_EN, 0);
}

static u32 nbio_v7_7_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v7_7_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
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

static void nbio_v7_7_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	u32 reg;

	reg = RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN);
	reg = REG_SET_FIELD(reg, RCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN,
			    BIF_DOORBELL_APER_EN, enable ? 1 : 0);

	WREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN, reg);
}

static void nbio_v7_7_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
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


static void nbio_v7_7_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_SOC15(NBIO, 0,
								regGDC0_BIF_IH_DOORBELL_RANGE);

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

	WREG32_SOC15(NBIO, 0, regGDC0_BIF_IH_DOORBELL_RANGE,
			 ih_doorbell_range);
}

static void nbio_v7_7_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, regBIF_BX1_INTERRUPT_CNTL2,
		     adev->dummy_page_addr >> 8);

	interrupt_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX1_INTERRUPT_CNTL);
	/*
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX1_INTERRUPT_CNTL,
				       IH_DUMMY_RD_OVERRIDE, 0);

	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX1_INTERRUPT_CNTL,
				       IH_REQ_NONSNOOP_EN, 0);

	WREG32_SOC15(NBIO, 0, regBIF_BX1_INTERRUPT_CNTL, interrupt_cntl);
}

static u32 nbio_v7_7_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_REQ);
}

static u32 nbio_v7_7_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_DONE);
}

static u32 nbio_v7_7_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_INDEX2);
}

static u32 nbio_v7_7_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_DATA2);
}

static u32 nbio_v7_7_get_pcie_port_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_RSMU_INDEX);
}

static u32 nbio_v7_7_get_pcie_port_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_RSMU_DATA);
}

const struct nbio_hdp_flush_reg nbio_v7_7_hdp_flush_reg = {
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

static void nbio_v7_7_init_registers(struct amdgpu_device *adev)
{
	uint32_t def, data;

	def = data = RREG32_SOC15(NBIO, 0, regBIF0_PCIE_MST_CTRL_3);
	data = REG_SET_FIELD(data, BIF0_PCIE_MST_CTRL_3,
			     CI_SWUS_MAX_READ_REQUEST_SIZE_MODE, 1);
	data = REG_SET_FIELD(data, BIF0_PCIE_MST_CTRL_3,
			     CI_SWUS_MAX_READ_REQUEST_SIZE_PRIV, 1);

	if (def != data)
		WREG32_SOC15(NBIO, 0, regBIF0_PCIE_MST_CTRL_3, data);

}

const struct amdgpu_nbio_funcs nbio_v7_7_funcs = {
	.get_hdp_flush_req_offset = nbio_v7_7_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v7_7_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v7_7_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v7_7_get_pcie_data_offset,
	.get_pcie_port_index_offset = nbio_v7_7_get_pcie_port_index_offset,
	.get_pcie_port_data_offset = nbio_v7_7_get_pcie_port_data_offset,
	.get_rev_id = nbio_v7_7_get_rev_id,
	.mc_access_enable = nbio_v7_7_mc_access_enable,
	.get_memsize = nbio_v7_7_get_memsize,
	.sdma_doorbell_range = nbio_v7_7_sdma_doorbell_range,
	.enable_doorbell_aperture = nbio_v7_7_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v7_7_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v7_7_ih_doorbell_range,
	.ih_control = nbio_v7_7_ih_control,
	.init_registers = nbio_v7_7_init_registers,
};

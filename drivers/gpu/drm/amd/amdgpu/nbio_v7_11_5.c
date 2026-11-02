/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
#include "nbio_v7_11_5.h"

#include "nbio/nbio_7_11_5_offset.h"
#include "nbio/nbio_7_11_5_sh_mask.h"
#include <uapi/linux/kfd_ioctl.h>

static void nbio_v7_11_5_remap_hdp_registers(struct amdgpu_device *adev)
{
		WREG32_SOC15(NBIO, 0, regBIF_BX1_REMAP_HDP_MEM_FLUSH_CNTL,
			adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
		WREG32_SOC15(NBIO, 0, regBIF_BX1_REMAP_HDP_REG_FLUSH_CNTL,
			adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v7_11_5_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

		tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP1_RCC_DEV0_EPF0_STRAP0);

	tmp &= RCC_STRAP1_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_STRAP1_RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v7_11_5_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, regBIF_BX1_BIF_FB_EN,
			BIF_BX1_BIF_FB_EN__FB_READ_EN_MASK |
			BIF_BX1_BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, regBIF_BX1_BIF_FB_EN, 0);
}

static u32 nbio_v7_11_5_get_memsize(struct amdgpu_device *adev)
{
	u32 memsize;

	memsize = RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_0_RCC_CONFIG_MEMSIZE);

	return memsize;
}

static void nbio_v7_11_5_sdma_doorbell_range(struct amdgpu_device *adev,
					    int instance, bool use_doorbell,
					    int doorbell_index,
					    int doorbell_size)
{
	u32 doorbell_range;

	if (instance == 0) {
		doorbell_range = RREG32_SOC15(NBIO, 0,
					     regGDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL);

		if (use_doorbell) {
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_ENABLE,
						       0x1);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_AWID,
						       0xe);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_RANGE_OFFSET,
						       doorbell_index);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_RANGE_SIZE,
						       doorbell_size);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_AWADDR_31_28_VALUE,
						       0x3);
		} else {
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
						       S2A_DOORBELL_PORT2_RANGE_SIZE,
						       0);
		}

		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL, doorbell_range);
	}
}

static void nbio_v7_11_5_vpe_doorbell_range(struct amdgpu_device *adev,
							int instance, bool use_doorbell,
							int doorbell_index,
							int doorbell_size)
{
	u32 doorbell_range;

	if (instance)
		return;

	doorbell_range = RREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL,
					       S2A_DOORBELL_PORT5_ENABLE,
					       0x1);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL,
					       S2A_DOORBELL_PORT5_AWID,
					       0xf);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL,
					       S2A_DOORBELL_PORT5_RANGE_OFFSET,
					       doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL,
					       S2A_DOORBELL_PORT5_RANGE_SIZE,
					       doorbell_size);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL,
					       S2A_DOORBELL_PORT5_AWADDR_31_28_VALUE,
					       0xf);
	} else {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL,
					       S2A_DOORBELL_PORT5_RANGE_SIZE,
					       0);
	}

	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL, doorbell_range);
}

static void nbio_v7_11_5_gc_doorbell_init(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_0_CTRL, 0x30000007);
	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_3_CTRL, 0x3000000d);
}

static void nbio_v7_11_5_enable_doorbell_aperture(struct amdgpu_device *adev,
						bool enable)
{
	WREG32_FIELD15_PREREG(NBIO, 0, RCC_DEV0_EPF0_0_RCC_DOORBELL_APER_EN,
			BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

static void nbio_v7_11_5_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
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

	WREG32_SOC15(NBIO, 0, regBIF_BX_PF0_DOORBELL_SELFRING_GPA_APER_CNTL, tmp);
}

static void nbio_v7_11_5_ih_doorbell_range(struct amdgpu_device *adev,
					 bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range;

	ih_doorbell_range = RREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL);

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_ENABLE,
						  0x1);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_AWID,
						  0x0);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_RANGE_OFFSET,
						  doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_RANGE_SIZE,
						  2);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
						  0x0);
	} else {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_RANGE_SIZE,
						  0);
	}

	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL, ih_doorbell_range);
}

static void nbio_v7_11_5_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
		WREG32_SOC15(NBIO, 0, regBIF_BX1_INTERRUPT_CNTL2,
				adev->dummy_page_addr >> 8);

	interrupt_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX1_INTERRUPT_CNTL);
	/*
	 * BIF_BX1_INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * BIF_BX1_INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX1_INTERRUPT_CNTL,
				       IH_DUMMY_RD_OVERRIDE, 0);

	/* BIF_BX1_INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, BIF_BX1_INTERRUPT_CNTL,
				       IH_REQ_NONSNOOP_EN, 0);

		WREG32_SOC15(NBIO, 0, regBIF_BX1_INTERRUPT_CNTL, interrupt_cntl);
}

static void
nbio_v7_11_5_get_clockgating_state(struct amdgpu_device *adev,
				  u64 *flags)
{
}

static u32 nbio_v7_11_5_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	u32 offset;

	offset = SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF1_GPU_HDP_FLUSH_REQ);

	return offset;
}

static u32 nbio_v7_11_5_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	u32 offset;

	offset = SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF1_GPU_HDP_FLUSH_DONE);

	return offset;
}

static u32 nbio_v7_11_5_get_pcie_index_offset(struct amdgpu_device *adev)
{
	u32 offset;

	offset = SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF1_RSMU_INDEX);

	return offset;
}

static u32 nbio_v7_11_5_get_pcie_data_offset(struct amdgpu_device *adev)
{
	u32 offset;

	offset = SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF1_RSMU_DATA);

	return offset;
}

const struct nbio_hdp_flush_reg nbio_v7_11_5_hdp_flush_reg = {
	.ref_and_mask_cp0 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP0_MASK,
	.ref_and_mask_cp1 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP1_MASK,
	.ref_and_mask_cp2 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP2_MASK,
	.ref_and_mask_cp3 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP3_MASK,
	.ref_and_mask_cp4 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP4_MASK,
	.ref_and_mask_cp5 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP5_MASK,
	.ref_and_mask_cp6 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP6_MASK,
	.ref_and_mask_cp7 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP7_MASK,
	.ref_and_mask_cp8 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP8_MASK,
	.ref_and_mask_cp9 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__CP9_MASK,
	.ref_and_mask_sdma0 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__SDMA0_MASK,
	.ref_and_mask_sdma1 = BIF_BX_PF1_GPU_HDP_FLUSH_DONE__SDMA1_MASK,
};

static void nbio_v7_11_5_init_registers(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF2_STRAP2);
	data &= ~RCC_DEV0_EPF2_STRAP2__STRAP_NO_SOFT_RESET_DEV0_F2_MASK;
	WREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF2_STRAP2, data);
}

#define MMIO_REG_HOLE_OFFSET (0x80000 - PAGE_SIZE)

static void nbio_v7_11_5_set_reg_remap(struct amdgpu_device *adev)
{
	if (!amdgpu_sriov_vf(adev) && (PAGE_SIZE <= 4096)) {
		adev->rmmio_remap.reg_offset = MMIO_REG_HOLE_OFFSET;
		adev->rmmio_remap.bus_addr = adev->rmmio_base + MMIO_REG_HOLE_OFFSET;
	} else {
		adev->rmmio_remap.reg_offset = SOC15_REG_OFFSET(NBIO, 0,
			regBIF_BX_PF1_HDP_MEM_COHERENCY_FLUSH_CNTL) << 2;
		adev->rmmio_remap.bus_addr = 0;
	}
}

const struct amdgpu_nbio_funcs nbio_v7_11_5_funcs = {
	.get_hdp_flush_req_offset = nbio_v7_11_5_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v7_11_5_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v7_11_5_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v7_11_5_get_pcie_data_offset,
	.get_rev_id = nbio_v7_11_5_get_rev_id,
	.mc_access_enable = nbio_v7_11_5_mc_access_enable,
	.get_memsize = nbio_v7_11_5_get_memsize,
	.sdma_doorbell_range = nbio_v7_11_5_sdma_doorbell_range,
	.vpe_doorbell_range = nbio_v7_11_5_vpe_doorbell_range,
	.gc_doorbell_init = nbio_v7_11_5_gc_doorbell_init,
	.enable_doorbell_aperture = nbio_v7_11_5_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v7_11_5_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v7_11_5_ih_doorbell_range,
	.get_clockgating_state = nbio_v7_11_5_get_clockgating_state,
	.ih_control = nbio_v7_11_5_ih_control,
	.init_registers = nbio_v7_11_5_init_registers,
	.remap_hdp_registers = nbio_v7_11_5_remap_hdp_registers,
	.set_reg_remap = nbio_v7_11_5_set_reg_remap,
};

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
#include "nbio/nbio_6_3_2_offset.h"
#include "nbio/nbio_6_3_2_sh_mask.h"

#include "amdgpu.h"
#include "nbio_v6_3_2.h"

static u32 nbio_v6_3_2_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_INDEX2);
}

static u32 nbio_v6_3_2_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_DATA2);
}

static u32 nbio_v6_3_2_get_pcie_index_hi_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_INDEX2_HI);
}

static u32 nbio_v6_3_2_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

	/* TODO: RCC_STRAP0_RCC_DEV0_EPF0_STRAP0 is not accessible from
	 * guest side. It requires bootloader to update specific fields
	 * in ip discovery table to identify soc revision id.
	 * Return 0 when the function is called from guest side until
	 * bootloader change is available.
	 */
	if (amdgpu_sriov_vf(adev))
		return 0;

	tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0);
	tmp = REG_GET_FIELD(tmp, RCC_STRAP0_RCC_DEV0_EPF0_STRAP0,
			    STRAP_ATI_REV_ID_DEV0_F0);

	return tmp;
}

static void nbio_v6_3_2_mc_access_enable(struct amdgpu_device *adev,
					 bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN,
			BIF_BX0_BIF_FB_EN__FB_READ_EN_MASK | BIF_BX0_BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN, 0);
}

static void nbio_v6_3_2_init_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regXCD_DOORBELL_FENCE_1,
		(0xff & ~(adev->gfx.xcc_mask)) <<
		XCD_DOORBELL_FENCE_1__XCD_0_DOORBELL_DISABLE__SHIFT);
}

static u32 nbio_v6_3_2_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v6_3_2_enable_doorbell_aperture(struct amdgpu_device *adev,
						 bool enable)
{
	/* Enable to allow doorbell pass thru on pre-silicon bare-metal */
	WREG32_SOC15(NBIO, 0, regGDC0_DOORBELL_ACCESS_EN_PF, 0xfffff);
	WREG32_FIELD15_PREREG(NBIO, 0, RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN,
			BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

static void nbio_v6_3_2_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
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

static void nbio_v6_3_2_enable_doorbell_interrupt(struct amdgpu_device *adev,
						  bool enable)
{
	WREG32_FIELD15_PREREG(NBIO, 0, BIF_BX0_BIF_DOORBELL_INT_CNTL,
			      DOORBELL_INTERRUPT_DISABLE, enable ? 0 : 1);
}

static void nbio_v6_3_2_ih_control(struct amdgpu_device *adev)
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

static void nbio_v6_3_2_ih_doorbell_range(struct amdgpu_device *adev,
					  bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = 0;
	u32 ih_doorbell_range1 = 0;

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
						  8);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
						  GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL,
						  S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
						  0x0);
		ih_doorbell_range1 = REG_SET_FIELD(ih_doorbell_range1,
						   GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL1,
						   S2A_DOORBELL_PORT1_TARGET_PORT_TYPE,
						   0x3);
		ih_doorbell_range1 = REG_SET_FIELD(ih_doorbell_range1,
						   GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL1,
						   S2A_DOORBELL_PORT1_TARGET_DIEID,
						   0x0);
		ih_doorbell_range1 = REG_SET_FIELD(ih_doorbell_range1,
						   GDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL1,
						   S2A_DOORBELL_PORT1_TARGET_PORT_ID,
						   0x0);
	}

	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL, ih_doorbell_range);
	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_1_CTRL1, ih_doorbell_range1);
}

static void nbio_v6_3_2_gc_doorbell_init(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_0_CTRL, 0x30000007);
	WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_0_CTRL1, 0x3);
}

static void nbio_v6_3_2_sdma_doorbell_range(struct amdgpu_device *adev,
					    int instance, bool use_doorbell,
					    int doorbell_index,
					    int doorbell_size)
{
	if (instance == 0) {
		u32 doorbell_range = 0;
		u32 doorbell_range1 = 0;

		if (use_doorbell) {
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL,
						       S2A_DOORBELL_PORT6_ENABLE,
						       0x1);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL,
						       S2A_DOORBELL_PORT6_AWID,
						       0xe);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL,
						       S2A_DOORBELL_PORT6_RANGE_OFFSET,
						       doorbell_index);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL,
						       S2A_DOORBELL_PORT6_RANGE_SIZE,
						       doorbell_size);
			doorbell_range = REG_SET_FIELD(doorbell_range,
						       GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL,
						       S2A_DOORBELL_PORT6_AWADDR_31_28_VALUE,
						       0xe);
			doorbell_range1 = REG_SET_FIELD(doorbell_range1,
						        GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL1,
						        S2A_DOORBELL_PORT6_TARGET_PORT_TYPE,
						        0x3);
			doorbell_range1 = REG_SET_FIELD(doorbell_range1,
						        GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL1,
						        S2A_DOORBELL_PORT6_TARGET_DIEID,
						        0x0);
			doorbell_range1 = REG_SET_FIELD(doorbell_range1,
						        GDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL1,
						        S2A_DOORBELL_PORT6_TARGET_PORT_ID,
						        0x0);
		}

		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL, doorbell_range);
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_6_CTRL1, doorbell_range1);
	}
}

static void nbio_v6_3_2_vcn_doorbell_range(struct amdgpu_device *adev,
					   bool use_doorbell, int doorbell_index,
					   int instance)
{
	u32 doorbell_range = 0;
	u32 doorbell_range1 = 0;

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
					       S2A_DOORBELL_PORT2_ENABLE,
					       0x1);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
					       S2A_DOORBELL_PORT2_AWID,
					       (instance % adev->vcn.num_inst_per_aid) ? 0x7 : 0x4);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
					       S2A_DOORBELL_PORT2_RANGE_OFFSET,
					       doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
					       S2A_DOORBELL_PORT2_RANGE_SIZE,
					       8);
		doorbell_range = REG_SET_FIELD(doorbell_range,
					       GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL,
					       S2A_DOORBELL_PORT2_AWADDR_31_28_VALUE,
					       (instance % adev->vcn.num_inst_per_aid) ? 0x7 : 0x4);
		doorbell_range1 = REG_SET_FIELD(doorbell_range1,
						GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL1,
					        S2A_DOORBELL_PORT2_TARGET_PORT_TYPE,
					        0x3);
		doorbell_range1 = REG_SET_FIELD(doorbell_range1,
						GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL1,
					        S2A_DOORBELL_PORT2_TARGET_DIEID,
					        (instance / adev->vcn.num_inst_per_aid) ? 0x3 : 0x0);
		doorbell_range1 = REG_SET_FIELD(doorbell_range1,
						GDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL1,
					        S2A_DOORBELL_PORT2_TARGET_PORT_ID,
					        0x0);
	}

	switch (instance) {
	case 0:
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL, doorbell_range);
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_2_CTRL1, doorbell_range1);
		break;
	case 1:
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_3_CTRL, doorbell_range);
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_3_CTRL1, doorbell_range1);
		break;
	case 2:
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_4_CTRL, doorbell_range);
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_4_CTRL1, doorbell_range1);
		break;
	case 3:
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL, doorbell_range);
		WREG32_SOC15(NBIO, 0, regGDC_S2A0_S2A_DOORBELL_ENTRY_5_CTRL1, doorbell_range1);
		break;
	default:
		dev_err(adev->dev,
			"amdgpu: invalid vcn instance set when program doorbell range\n");
		break;
	}
}

static int nbio_v6_3_2_get_compute_partition_mode(struct amdgpu_device *adev)
{
	u32 tmp, px;

	tmp = RREG32_SOC15(NBIO, 0, regBIF_BX_PF0_PARTITION_COMPUTE_STATUS);
	px = REG_GET_FIELD(tmp, BIF_BX_PF0_PARTITION_COMPUTE_STATUS,
			   PARTITION_MODE);

	return px;
}

static bool nbio_v6_3_2_is_nps_switch_requested(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32_SOC15(NBIO, 0, regBIF_BX_PF0_PARTITION_MEM_STATUS);
	tmp = REG_GET_FIELD(tmp, BIF_BX_PF0_PARTITION_MEM_STATUS,
			    CHANGE_STATUS);

	/* 0x8 - NPS switch requested */
	return (tmp == 0x8);
}
static u32 nbio_v6_3_2_get_memory_partition_mode(struct amdgpu_device *adev,
						 u32 *supp_modes)
{
	u32 tmp;

	tmp = RREG32_SOC15(NBIO, 0, regBIF_BX_PF0_PARTITION_MEM_STATUS);
	tmp = REG_GET_FIELD(tmp, BIF_BX_PF0_PARTITION_MEM_STATUS, NPS_MODE);

	if (supp_modes) {
		*supp_modes =
			RREG32_SOC15(NBIO, 0, regBIF_BX_PF0_PARTITION_MEM_CAP);
	}

	return ffs(tmp);
}

const struct amdgpu_nbio_funcs nbio_v6_3_2_funcs = {
	.get_pcie_index_offset = nbio_v6_3_2_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v6_3_2_get_pcie_data_offset,
	.get_pcie_index_hi_offset = nbio_v6_3_2_get_pcie_index_hi_offset,
	.get_rev_id = nbio_v6_3_2_get_rev_id,
	.mc_access_enable = nbio_v6_3_2_mc_access_enable,
	.get_memsize = nbio_v6_3_2_get_memsize,
	.enable_doorbell_aperture = nbio_v6_3_2_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v6_3_2_enable_doorbell_selfring_aperture,
	.enable_doorbell_interrupt = nbio_v6_3_2_enable_doorbell_interrupt,
	.get_compute_partition_mode = nbio_v6_3_2_get_compute_partition_mode,
	.get_memory_partition_mode = nbio_v6_3_2_get_memory_partition_mode,
	.is_nps_switch_requested = nbio_v6_3_2_is_nps_switch_requested,
	.ih_control = nbio_v6_3_2_ih_control,
	.ih_doorbell_range = nbio_v6_3_2_ih_doorbell_range,
	.gc_doorbell_init = nbio_v6_3_2_gc_doorbell_init,
	.sdma_doorbell_range = nbio_v6_3_2_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v6_3_2_vcn_doorbell_range,
	.init_registers = nbio_v6_3_2_init_registers,
};

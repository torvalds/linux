/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "nbio_v7_9.h"
#include "amdgpu_ras.h"

#include "nbio/nbio_7_9_0_offset.h"
#include "nbio/nbio_7_9_0_sh_mask.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"
#include <uapi/linux/kfd_ioctl.h>

#define NPS_MODE_MASK 0x000000FFL

/* Core 0 Port 0 counter */
#define smnPCIEP_NAK_COUNTER 0x1A340218

#define smnPCIE_PERF_CNTL_TXCLK3		0x1A38021c
#define smnPCIE_PERF_CNTL_TXCLK7		0x1A380888
#define smnPCIE_PERF_COUNT_CNTL			0x1A380200
#define smnPCIE_PERF_COUNT0_TXCLK3		0x1A380220
#define smnPCIE_PERF_COUNT0_TXCLK7		0x1A38088C
#define smnPCIE_PERF_COUNT0_UPVAL_TXCLK3	0x1A3808F8
#define smnPCIE_PERF_COUNT0_UPVAL_TXCLK7	0x1A380918


static void nbio_v7_9_remap_hdp_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, regBIF_BX0_REMAP_HDP_MEM_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
	WREG32_SOC15(NBIO, 0, regBIF_BX0_REMAP_HDP_REG_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v7_9_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = IP_VERSION_SUBREV(amdgpu_ip_version_full(adev, NBIO_HWIP, 0));
	/* If it is VF or subrevision holds a non-zero value, that should be used */
	if (tmp || amdgpu_sriov_vf(adev))
		return tmp;

	/* If discovery subrev is not updated, use register version */
	tmp = RREG32_SOC15(NBIO, 0, regRCC_STRAP0_RCC_DEV0_EPF0_STRAP0);
	tmp = REG_GET_FIELD(tmp, RCC_STRAP0_RCC_DEV0_EPF0_STRAP0,
			    STRAP_ATI_REV_ID_DEV0_F0);

	return tmp;
}

static void nbio_v7_9_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN,
			BIF_BX0_BIF_FB_EN__FB_READ_EN_MASK | BIF_BX0_BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_FB_EN, 0);
}

static u32 nbio_v7_9_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, regRCC_DEV0_EPF0_RCC_CONFIG_MEMSIZE);
}

static void nbio_v7_9_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
			bool use_doorbell, int doorbell_index, int doorbell_size)
{
	u32 doorbell_range = 0, doorbell_ctrl = 0;
	int aid_id, dev_inst;

	dev_inst = GET_INST(SDMA0, instance);
	aid_id = adev->sdma.instance[instance].aid_id;

	if (use_doorbell == false)
		return;

	doorbell_range =
		REG_SET_FIELD(doorbell_range, DOORBELL0_CTRL_ENTRY_0,
			BIF_DOORBELL0_RANGE_OFFSET_ENTRY, doorbell_index);
	doorbell_range =
		REG_SET_FIELD(doorbell_range, DOORBELL0_CTRL_ENTRY_0,
			BIF_DOORBELL0_RANGE_SIZE_ENTRY, doorbell_size);
	doorbell_ctrl =
		REG_SET_FIELD(doorbell_ctrl, S2A_DOORBELL_ENTRY_1_CTRL,
			S2A_DOORBELL_PORT1_ENABLE, 1);
	doorbell_ctrl =
		REG_SET_FIELD(doorbell_ctrl, S2A_DOORBELL_ENTRY_1_CTRL,
			S2A_DOORBELL_PORT1_RANGE_SIZE, doorbell_size);

	switch (dev_inst % adev->sdma.num_inst_per_aid) {
	case 0:
		WREG32_SOC15_OFFSET(NBIO, 0, regDOORBELL0_CTRL_ENTRY_1,
			4 * aid_id, doorbell_range);

		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWID, 0xe);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_RANGE_OFFSET, 0xe);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
					0x1);
		WREG32_SOC15_EXT(NBIO, aid_id, regS2A_DOORBELL_ENTRY_1_CTRL,
			aid_id, doorbell_ctrl);
		break;
	case 1:
		WREG32_SOC15_OFFSET(NBIO, 0, regDOORBELL0_CTRL_ENTRY_2,
			4 * aid_id, doorbell_range);

		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWID, 0x8);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_RANGE_OFFSET, 0x8);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
					0x2);
		WREG32_SOC15_EXT(NBIO, aid_id, regS2A_DOORBELL_ENTRY_2_CTRL,
			aid_id, doorbell_ctrl);
		break;
	case 2:
		WREG32_SOC15_OFFSET(NBIO, 0, regDOORBELL0_CTRL_ENTRY_3,
			4 * aid_id, doorbell_range);

		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWID, 0x9);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_RANGE_OFFSET, 0x9);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
					0x8);
		WREG32_SOC15_EXT(NBIO, aid_id, regS2A_DOORBELL_ENTRY_5_CTRL,
			aid_id, doorbell_ctrl);
		break;
	case 3:
		WREG32_SOC15_OFFSET(NBIO, 0, regDOORBELL0_CTRL_ENTRY_4,
			4 * aid_id, doorbell_range);

		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWID, 0xa);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_RANGE_OFFSET, 0xa);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
					S2A_DOORBELL_ENTRY_1_CTRL,
					S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE,
					0x9);
		WREG32_SOC15_EXT(NBIO, aid_id, regS2A_DOORBELL_ENTRY_6_CTRL,
			aid_id, doorbell_ctrl);
		break;
	default:
		break;
	}
}

static void nbio_v7_9_vcn_doorbell_range(struct amdgpu_device *adev, bool use_doorbell,
					 int doorbell_index, int instance)
{
	u32 doorbell_range = 0, doorbell_ctrl = 0;
	u32 aid_id = instance;

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range,
				DOORBELL0_CTRL_ENTRY_0,
				BIF_DOORBELL0_RANGE_OFFSET_ENTRY,
				doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range,
				DOORBELL0_CTRL_ENTRY_0,
				BIF_DOORBELL0_RANGE_SIZE_ENTRY,
				0x9);
		if (aid_id)
			doorbell_range = REG_SET_FIELD(doorbell_range,
					DOORBELL0_CTRL_ENTRY_0,
					DOORBELL0_FENCE_ENABLE_ENTRY,
					0x4);

		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_ENABLE, 1);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_AWID, 0x4);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_RANGE_OFFSET, 0x4);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_RANGE_SIZE, 0x9);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE, 0x4);

		WREG32_SOC15_OFFSET(NBIO, 0, regDOORBELL0_CTRL_ENTRY_17,
					aid_id, doorbell_range);
		WREG32_SOC15_EXT(NBIO, aid_id, regS2A_DOORBELL_ENTRY_4_CTRL,
				aid_id, doorbell_ctrl);
	} else {
		doorbell_range = REG_SET_FIELD(doorbell_range,
				DOORBELL0_CTRL_ENTRY_0,
				BIF_DOORBELL0_RANGE_SIZE_ENTRY, 0);
		doorbell_ctrl = REG_SET_FIELD(doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_RANGE_SIZE, 0);

		WREG32_SOC15_OFFSET(NBIO, 0, regDOORBELL0_CTRL_ENTRY_17,
					aid_id, doorbell_range);
		WREG32_SOC15_EXT(NBIO, aid_id, regS2A_DOORBELL_ENTRY_4_CTRL,
				aid_id, doorbell_ctrl);
	}
}

static void nbio_v7_9_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	/* Enable to allow doorbell pass thru on pre-silicon bare-metal */
	WREG32_SOC15(NBIO, 0, regBIFC_DOORBELL_ACCESS_EN_PF, 0xfffff);
	WREG32_FIELD15_PREREG(NBIO, 0, RCC_DEV0_EPF0_RCC_DOORBELL_APER_EN,
			BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

static void nbio_v7_9_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
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

static void nbio_v7_9_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = 0, ih_doorbell_ctrl = 0;

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
				DOORBELL0_CTRL_ENTRY_0,
				BIF_DOORBELL0_RANGE_OFFSET_ENTRY,
				doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
				DOORBELL0_CTRL_ENTRY_0,
				BIF_DOORBELL0_RANGE_SIZE_ENTRY,
				0x8);

		ih_doorbell_ctrl = REG_SET_FIELD(ih_doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_ENABLE, 1);
		ih_doorbell_ctrl = REG_SET_FIELD(ih_doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_AWID, 0);
		ih_doorbell_ctrl = REG_SET_FIELD(ih_doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_RANGE_OFFSET, 0);
		ih_doorbell_ctrl = REG_SET_FIELD(ih_doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_RANGE_SIZE, 0x8);
		ih_doorbell_ctrl = REG_SET_FIELD(ih_doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_AWADDR_31_28_VALUE, 0);
	} else {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range,
				DOORBELL0_CTRL_ENTRY_0,
				BIF_DOORBELL0_RANGE_SIZE_ENTRY, 0);
		ih_doorbell_ctrl = REG_SET_FIELD(ih_doorbell_ctrl,
				S2A_DOORBELL_ENTRY_1_CTRL,
				S2A_DOORBELL_PORT1_RANGE_SIZE, 0);
	}

	WREG32_SOC15(NBIO, 0, regDOORBELL0_CTRL_ENTRY_0, ih_doorbell_range);
	WREG32_SOC15(NBIO, 0, regS2A_DOORBELL_ENTRY_3_CTRL, ih_doorbell_ctrl);
}


static void nbio_v7_9_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
}

static void nbio_v7_9_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						      bool enable)
{
}

static void nbio_v7_9_get_clockgating_state(struct amdgpu_device *adev,
					    u64 *flags)
{
}

static void nbio_v7_9_ih_control(struct amdgpu_device *adev)
{
	u32 interrupt_cntl;

	/* setup interrupt control */
	WREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL2, adev->dummy_page_addr >> 8);
	interrupt_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL);
	/* INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl =
		REG_SET_FIELD(interrupt_cntl, BIF_BX0_INTERRUPT_CNTL, IH_DUMMY_RD_OVERRIDE, 0);
	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl =
		REG_SET_FIELD(interrupt_cntl, BIF_BX0_INTERRUPT_CNTL, IH_REQ_NONSNOOP_EN, 0);
	WREG32_SOC15(NBIO, 0, regBIF_BX0_INTERRUPT_CNTL, interrupt_cntl);
}

static u32 nbio_v7_9_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_REQ);
}

static u32 nbio_v7_9_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX_PF0_GPU_HDP_FLUSH_DONE);
}

static u32 nbio_v7_9_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_INDEX2);
}

static u32 nbio_v7_9_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_DATA2);
}

static u32 nbio_v7_9_get_pcie_index_hi_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, regBIF_BX0_PCIE_INDEX2_HI);
}

const struct nbio_hdp_flush_reg nbio_v7_9_hdp_flush_reg = {
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
	.ref_and_mask_sdma2 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__RSVD_ENG0_MASK,
	.ref_and_mask_sdma3 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__RSVD_ENG1_MASK,
	.ref_and_mask_sdma4 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__RSVD_ENG2_MASK,
	.ref_and_mask_sdma5 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__RSVD_ENG3_MASK,
	.ref_and_mask_sdma6 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__RSVD_ENG4_MASK,
	.ref_and_mask_sdma7 = BIF_BX_PF0_GPU_HDP_FLUSH_DONE__RSVD_ENG5_MASK,
};

static void nbio_v7_9_enable_doorbell_interrupt(struct amdgpu_device *adev,
						bool enable)
{
	WREG32_FIELD15_PREREG(NBIO, 0, BIF_BX0_BIF_DOORBELL_INT_CNTL,
			      DOORBELL_INTERRUPT_DISABLE, enable ? 0 : 1);
}

static int nbio_v7_9_get_compute_partition_mode(struct amdgpu_device *adev)
{
	u32 tmp, px;

	tmp = RREG32_SOC15(NBIO, 0, regBIF_BX_PF0_PARTITION_COMPUTE_STATUS);
	px = REG_GET_FIELD(tmp, BIF_BX_PF0_PARTITION_COMPUTE_STATUS,
			   PARTITION_MODE);

	return px;
}

static u32 nbio_v7_9_get_memory_partition_mode(struct amdgpu_device *adev,
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

static void nbio_v7_9_init_registers(struct amdgpu_device *adev)
{
	u32 inst_mask;
	int i;

	if (amdgpu_sriov_vf(adev))
		adev->rmmio_remap.reg_offset =
			SOC15_REG_OFFSET(
				NBIO, 0,
				regBIF_BX_DEV0_EPF0_VF0_HDP_MEM_COHERENCY_FLUSH_CNTL)
			<< 2;
	WREG32_SOC15(NBIO, 0, regXCC_DOORBELL_FENCE,
		0xff & ~(adev->gfx.xcc_mask));

	WREG32_SOC15(NBIO, 0, regBIFC_GFX_INT_MONITOR_MASK, 0x7ff);

	inst_mask = adev->aid_mask & ~1U;
	for_each_inst(i, inst_mask) {
		WREG32_SOC15_EXT(NBIO, i, regXCC_DOORBELL_FENCE, i,
			XCC_DOORBELL_FENCE__SHUB_SLV_MODE_MASK);

	}

	if (!amdgpu_sriov_vf(adev)) {
		u32 baco_cntl;
		for_each_inst(i, adev->aid_mask) {
			baco_cntl = RREG32_SOC15(NBIO, i, regBIF_BX0_BACO_CNTL);
			if (baco_cntl & (BIF_BX0_BACO_CNTL__BACO_DUMMY_EN_MASK |
					 BIF_BX0_BACO_CNTL__BACO_EN_MASK)) {
				baco_cntl &= ~(
					BIF_BX0_BACO_CNTL__BACO_DUMMY_EN_MASK |
					BIF_BX0_BACO_CNTL__BACO_EN_MASK);
				dev_dbg(adev->dev,
					"Unsetting baco dummy mode %x",
					baco_cntl);
				WREG32_SOC15(NBIO, i, regBIF_BX0_BACO_CNTL,
					     baco_cntl);
			}
		}
	}
}

static u64 nbio_v7_9_get_pcie_replay_count(struct amdgpu_device *adev)
{
	u32 val, nak_r, nak_g;

	if (adev->flags & AMD_IS_APU)
		return 0;

	/* Get the number of NAKs received and generated */
	val = RREG32_PCIE(smnPCIEP_NAK_COUNTER);
	nak_r = val & 0xFFFF;
	nak_g = val >> 16;

	/* Add the total number of NAKs, i.e the number of replays */
	return (nak_r + nak_g);
}

static void nbio_v7_9_get_pcie_usage(struct amdgpu_device *adev, uint64_t *count0,
				     uint64_t *count1)
{
	uint32_t perfctrrx = 0;
	uint32_t perfctrtx = 0;

	/* This reports 0 on APUs, so return to avoid writing/reading registers
	 * that may or may not be different from their GPU counterparts
	 */
	if (adev->flags & AMD_IS_APU)
		return;

	/* Use TXCLK3 counter group for rx event */
	/* Use TXCLK7 counter group for tx event */
	/* Set the 2 events that we wish to watch, defined above */
	/* 40 is event# for received msgs */
	/* 2 is event# of posted requests sent */
	perfctrrx = REG_SET_FIELD(perfctrrx, PCIE_PERF_CNTL_TXCLK3, EVENT0_SEL, 40);
	perfctrtx = REG_SET_FIELD(perfctrtx, PCIE_PERF_CNTL_TXCLK7, EVENT0_SEL, 2);

	/* Write to enable desired perf counters */
	WREG32_PCIE(smnPCIE_PERF_CNTL_TXCLK3, perfctrrx);
	WREG32_PCIE(smnPCIE_PERF_CNTL_TXCLK7, perfctrtx);

	/* Zero out and enable SHADOW_WR
	 * Write 0x6:
	 * Bit 1 = Global Shadow wr(1)
	 * Bit 2 = Global counter reset enable(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000006);

	/* Enable Gloabl Counter
	 * Write 0x1:
	 * Bit 0 = Global Counter Enable(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000001);

	msleep(1000);

	/* Disable Global Counter, Reset and enable SHADOW_WR
	 * Write 0x6:
	 * Bit 1 = Global Shadow wr(1)
	 * Bit 2 = Global counter reset enable(1)
	 */
	WREG32_PCIE(smnPCIE_PERF_COUNT_CNTL, 0x00000006);

	/* Get the upper and lower count  */
	*count0 = RREG32_PCIE(smnPCIE_PERF_COUNT0_TXCLK3) |
		  ((uint64_t)RREG32_PCIE(smnPCIE_PERF_COUNT0_UPVAL_TXCLK3) << 32);
	*count1 = RREG32_PCIE(smnPCIE_PERF_COUNT0_TXCLK7) |
		  ((uint64_t)RREG32_PCIE(smnPCIE_PERF_COUNT0_UPVAL_TXCLK7) << 32);
}

const struct amdgpu_nbio_funcs nbio_v7_9_funcs = {
	.get_hdp_flush_req_offset = nbio_v7_9_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v7_9_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v7_9_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v7_9_get_pcie_data_offset,
	.get_pcie_index_hi_offset = nbio_v7_9_get_pcie_index_hi_offset,
	.get_rev_id = nbio_v7_9_get_rev_id,
	.mc_access_enable = nbio_v7_9_mc_access_enable,
	.get_memsize = nbio_v7_9_get_memsize,
	.sdma_doorbell_range = nbio_v7_9_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v7_9_vcn_doorbell_range,
	.enable_doorbell_aperture = nbio_v7_9_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v7_9_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v7_9_ih_doorbell_range,
	.enable_doorbell_interrupt = nbio_v7_9_enable_doorbell_interrupt,
	.update_medium_grain_clock_gating = nbio_v7_9_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v7_9_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v7_9_get_clockgating_state,
	.ih_control = nbio_v7_9_ih_control,
	.remap_hdp_registers = nbio_v7_9_remap_hdp_registers,
	.get_compute_partition_mode = nbio_v7_9_get_compute_partition_mode,
	.get_memory_partition_mode = nbio_v7_9_get_memory_partition_mode,
	.init_registers = nbio_v7_9_init_registers,
	.get_pcie_replay_count = nbio_v7_9_get_pcie_replay_count,
	.get_pcie_usage = nbio_v7_9_get_pcie_usage,
};

static void nbio_v7_9_query_ras_error_count(struct amdgpu_device *adev,
					void *ras_error_status)
{
}

static void nbio_v7_9_handle_ras_controller_intr_no_bifring(struct amdgpu_device *adev)
{
	uint32_t bif_doorbell_intr_cntl;
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, adev->nbio.ras_if);
	struct ras_err_data err_data;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (amdgpu_ras_error_data_init(&err_data))
		return;

	bif_doorbell_intr_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL);

	if (REG_GET_FIELD(bif_doorbell_intr_cntl,
		BIF_BX0_BIF_DOORBELL_INT_CNTL, RAS_CNTLR_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_BX0_BIF_DOORBELL_INT_CNTL,
						RAS_CNTLR_INTERRUPT_CLEAR, 1);
		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		if (!ras->disable_ras_err_cnt_harvest) {
			/*
			 * clear error status after ras_controller_intr
			 * according to hw team and count ue number
			 * for query
			 */
			nbio_v7_9_query_ras_error_count(adev, &err_data);

			/* logging on error cnt and printing for awareness */
			obj->err_data.ue_count += err_data.ue_count;
			obj->err_data.ce_count += err_data.ce_count;

			if (err_data.ce_count)
				dev_info(adev->dev, "%ld correctable hardware "
						"errors detected in %s block\n",
						obj->err_data.ce_count,
						get_ras_block_str(adev->nbio.ras_if));

			if (err_data.ue_count)
				dev_info(adev->dev, "%ld uncorrectable hardware "
						"errors detected in %s block\n",
						obj->err_data.ue_count,
						get_ras_block_str(adev->nbio.ras_if));
		}

		dev_info(adev->dev, "RAS controller interrupt triggered "
					"by NBIF error\n");
	}

	amdgpu_ras_error_data_fini(&err_data);
}

static void nbio_v7_9_handle_ras_err_event_athub_intr_no_bifring(struct amdgpu_device *adev)
{
	uint32_t bif_doorbell_intr_cntl;

	bif_doorbell_intr_cntl = RREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL);

	if (REG_GET_FIELD(bif_doorbell_intr_cntl,
		BIF_BX0_BIF_DOORBELL_INT_CNTL, RAS_ATHUB_ERR_EVENT_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_BX0_BIF_DOORBELL_INT_CNTL,
						RAS_ATHUB_ERR_EVENT_INTERRUPT_CLEAR, 1);

		WREG32_SOC15(NBIO, 0, regBIF_BX0_BIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		amdgpu_ras_global_ras_isr(adev);
	}
}

static int nbio_v7_9_set_ras_controller_irq_state(struct amdgpu_device *adev,
						  struct amdgpu_irq_src *src,
						  unsigned type,
						  enum amdgpu_interrupt_state state)
{
	/* Dummy function, there is no initialization operation in driver */

	return 0;
}

static int nbio_v7_9_process_ras_controller_irq(struct amdgpu_device *adev,
						struct amdgpu_irq_src *source,
						struct amdgpu_iv_entry *entry)
{
	/* By design, the ih cookie for ras_controller_irq should be written
	 * to BIFring instead of general iv ring. However, due to known bif ring
	 * hw bug, it has to be disabled. There is no chance the process function
	 * will be involked. Just left it as a dummy one.
	 */
	return 0;
}

static int nbio_v7_9_set_ras_err_event_athub_irq_state(struct amdgpu_device *adev,
						       struct amdgpu_irq_src *src,
						       unsigned type,
						       enum amdgpu_interrupt_state state)
{
	/* Dummy function, there is no initialization operation in driver */

	return 0;
}

static int nbio_v7_9_process_err_event_athub_irq(struct amdgpu_device *adev,
						 struct amdgpu_irq_src *source,
						 struct amdgpu_iv_entry *entry)
{
	/* By design, the ih cookie for err_event_athub_irq should be written
	 * to BIFring instead of general iv ring. However, due to known bif ring
	 * hw bug, it has to be disabled. There is no chance the process function
	 * will be involked. Just left it as a dummy one.
	 */
	return 0;
}

static const struct amdgpu_irq_src_funcs nbio_v7_9_ras_controller_irq_funcs = {
	.set = nbio_v7_9_set_ras_controller_irq_state,
	.process = nbio_v7_9_process_ras_controller_irq,
};

static const struct amdgpu_irq_src_funcs nbio_v7_9_ras_err_event_athub_irq_funcs = {
	.set = nbio_v7_9_set_ras_err_event_athub_irq_state,
	.process = nbio_v7_9_process_err_event_athub_irq,
};

static int nbio_v7_9_init_ras_controller_interrupt (struct amdgpu_device *adev)
{
	int r;

	/* init the irq funcs */
	adev->nbio.ras_controller_irq.funcs =
		&nbio_v7_9_ras_controller_irq_funcs;
	adev->nbio.ras_controller_irq.num_types = 1;

	/* register ras controller interrupt */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__RAS_CONTROLLER_INTERRUPT,
			      &adev->nbio.ras_controller_irq);

	return r;
}

static int nbio_v7_9_init_ras_err_event_athub_interrupt (struct amdgpu_device *adev)
{

	int r;

	/* init the irq funcs */
	adev->nbio.ras_err_event_athub_irq.funcs =
		&nbio_v7_9_ras_err_event_athub_irq_funcs;
	adev->nbio.ras_err_event_athub_irq.num_types = 1;

	/* register ras err event athub interrupt */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__ERREVENT_ATHUB_INTERRUPT,
			      &adev->nbio.ras_err_event_athub_irq);

	return r;
}

const struct amdgpu_ras_block_hw_ops nbio_v7_9_ras_hw_ops = {
	.query_ras_error_count = nbio_v7_9_query_ras_error_count,
};

struct amdgpu_nbio_ras nbio_v7_9_ras = {
	.ras_block = {
		.ras_comm = {
			.name = "pcie_bif",
			.block = AMDGPU_RAS_BLOCK__PCIE_BIF,
			.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
		},
		.hw_ops = &nbio_v7_9_ras_hw_ops,
		.ras_late_init = amdgpu_nbio_ras_late_init,
	},
	.handle_ras_controller_intr_no_bifring = nbio_v7_9_handle_ras_controller_intr_no_bifring,
	.handle_ras_err_event_athub_intr_no_bifring = nbio_v7_9_handle_ras_err_event_athub_intr_no_bifring,
	.init_ras_controller_interrupt = nbio_v7_9_init_ras_controller_interrupt,
	.init_ras_err_event_athub_interrupt = nbio_v7_9_init_ras_err_event_athub_interrupt,
};

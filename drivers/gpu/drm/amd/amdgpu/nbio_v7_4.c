/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#include "nbio_v7_4.h"
#include "amdgpu_ras.h"

#include "nbio/nbio_7_4_offset.h"
#include "nbio/nbio_7_4_sh_mask.h"
#include "nbio/nbio_7_4_0_smn.h"
#include "ivsrcid/nbio/irqsrcs_nbif_7_4.h"
#include <uapi/linux/kfd_ioctl.h>

#define smnPCIE_LC_CNTL		0x11140280
#define smnPCIE_LC_CNTL3	0x111402d4
#define smnPCIE_LC_CNTL6	0x111402ec
#define smnPCIE_LC_CNTL7	0x111402f0
#define smnNBIF_MGCG_CTRL_LCLK	0x1013a21c
#define smnRCC_BIF_STRAP3	0x1012348c
#define RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER_MASK	0x0000FFFFL
#define RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER_MASK	0xFFFF0000L
#define smnRCC_BIF_STRAP5	0x10123494
#define RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER_MASK	0x0000FFFFL
#define smnBIF_CFG_DEV0_EPF0_DEVICE_CNTL2	0x1014008c
#define BIF_CFG_DEV0_EPF0_DEVICE_CNTL2__LTR_EN_MASK			0x0400L
#define smnBIF_CFG_DEV0_EPF0_PCIE_LTR_CAP	0x10140324
#define smnPSWUSP0_PCIE_LC_CNTL2		0x111402c4
#define smnRCC_EP_DEV0_0_EP_PCIE_TX_LTR_CNTL	0x10123538
#define smnRCC_BIF_STRAP2	0x10123488
#define RCC_BIF_STRAP2__STRAP_LTR_IN_ASPML1_DIS_MASK	0x00004000L
#define RCC_BIF_STRAP3__STRAP_VLINK_ASPM_IDLE_TIMER__SHIFT	0x0
#define RCC_BIF_STRAP3__STRAP_VLINK_PM_L1_ENTRY_TIMER__SHIFT	0x10
#define RCC_BIF_STRAP5__STRAP_VLINK_LDN_ENTRY_TIMER__SHIFT	0x0

/*
 * These are nbio v7_4_1 registers mask. Temporarily define these here since
 * nbio v7_4_1 header is incomplete.
 */
#define GPU_HDP_FLUSH_DONE__RSVD_ENG0_MASK	0x00001000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG1_MASK	0x00002000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG2_MASK	0x00004000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG3_MASK	0x00008000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG4_MASK	0x00010000L
#define GPU_HDP_FLUSH_DONE__RSVD_ENG5_MASK	0x00020000L

#define mmBIF_MMSCH1_DOORBELL_RANGE                     0x01dc
#define mmBIF_MMSCH1_DOORBELL_RANGE_BASE_IDX            2
//BIF_MMSCH1_DOORBELL_RANGE
#define BIF_MMSCH1_DOORBELL_RANGE__OFFSET__SHIFT        0x2
#define BIF_MMSCH1_DOORBELL_RANGE__SIZE__SHIFT          0x10
#define BIF_MMSCH1_DOORBELL_RANGE__OFFSET_MASK          0x00000FFCL
#define BIF_MMSCH1_DOORBELL_RANGE__SIZE_MASK            0x001F0000L

#define BIF_MMSCH1_DOORBELL_RANGE__OFFSET_MASK          0x00000FFCL
#define BIF_MMSCH1_DOORBELL_RANGE__SIZE_MASK            0x001F0000L

#define mmBIF_MMSCH1_DOORBELL_RANGE_ALDE                0x01d8
#define mmBIF_MMSCH1_DOORBELL_RANGE_ALDE_BASE_IDX       2
//BIF_MMSCH1_DOORBELL_ALDE_RANGE
#define BIF_MMSCH1_DOORBELL_RANGE_ALDE__OFFSET__SHIFT   0x2
#define BIF_MMSCH1_DOORBELL_RANGE_ALDE__SIZE__SHIFT     0x10
#define BIF_MMSCH1_DOORBELL_RANGE_ALDE__OFFSET_MASK     0x00000FFCL
#define BIF_MMSCH1_DOORBELL_RANGE_ALDE__SIZE_MASK       0x001F0000L

#define mmRCC_DEV0_EPF0_STRAP0_ALDE			0x0015
#define mmRCC_DEV0_EPF0_STRAP0_ALDE_BASE_IDX		2

static void nbio_v7_4_query_ras_error_count(struct amdgpu_device *adev,
					void *ras_error_status);

static void nbio_v7_4_remap_hdp_registers(struct amdgpu_device *adev)
{
	WREG32_SOC15(NBIO, 0, mmREMAP_HDP_MEM_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL);
	WREG32_SOC15(NBIO, 0, mmREMAP_HDP_REG_FLUSH_CNTL,
		adev->rmmio_remap.reg_offset + KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL);
}

static u32 nbio_v7_4_get_rev_id(struct amdgpu_device *adev)
{
	u32 tmp;

	if (adev->asic_type == CHIP_ALDEBARAN)
		tmp = RREG32_SOC15(NBIO, 0, mmRCC_DEV0_EPF0_STRAP0_ALDE);
	else
		tmp = RREG32_SOC15(NBIO, 0, mmRCC_DEV0_EPF0_STRAP0);

	tmp &= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0_MASK;
	tmp >>= RCC_DEV0_EPF0_STRAP0__STRAP_ATI_REV_ID_DEV0_F0__SHIFT;

	return tmp;
}

static void nbio_v7_4_mc_access_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable)
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN,
			BIF_FB_EN__FB_READ_EN_MASK | BIF_FB_EN__FB_WRITE_EN_MASK);
	else
		WREG32_SOC15(NBIO, 0, mmBIF_FB_EN, 0);
}

static u32 nbio_v7_4_get_memsize(struct amdgpu_device *adev)
{
	return RREG32_SOC15(NBIO, 0, mmRCC_CONFIG_MEMSIZE);
}

static void nbio_v7_4_sdma_doorbell_range(struct amdgpu_device *adev, int instance,
			bool use_doorbell, int doorbell_index, int doorbell_size)
{
	u32 reg, doorbell_range;

	if (instance < 2) {
		reg = instance +
			SOC15_REG_OFFSET(NBIO, 0, mmBIF_SDMA0_DOORBELL_RANGE);
	} else {
		/*
		 * These registers address of SDMA2~7 is not consecutive
		 * from SDMA0~1. Need plus 4 dwords offset.
		 *
		 *   BIF_SDMA0_DOORBELL_RANGE:  0x3bc0
		 *   BIF_SDMA1_DOORBELL_RANGE:  0x3bc4
		 *   BIF_SDMA2_DOORBELL_RANGE:  0x3bd8
+		 *   BIF_SDMA4_DOORBELL_RANGE:
+		 *     ARCTURUS:  0x3be0
+		 *     ALDEBARAN: 0x3be4
		 */
		if (adev->asic_type == CHIP_ALDEBARAN && instance == 4)
			reg = instance + 0x4 + 0x1 +
				SOC15_REG_OFFSET(NBIO, 0,
						 mmBIF_SDMA0_DOORBELL_RANGE);
		else
			reg = instance + 0x4 +
				SOC15_REG_OFFSET(NBIO, 0,
						 mmBIF_SDMA0_DOORBELL_RANGE);
	}

	doorbell_range = RREG32(reg);

	if (use_doorbell) {
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, OFFSET, doorbell_index);
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, SIZE, doorbell_size);
	} else
		doorbell_range = REG_SET_FIELD(doorbell_range, BIF_SDMA0_DOORBELL_RANGE, SIZE, 0);

	WREG32(reg, doorbell_range);
}

static void nbio_v7_4_vcn_doorbell_range(struct amdgpu_device *adev, bool use_doorbell,
					 int doorbell_index, int instance)
{
	u32 reg;
	u32 doorbell_range;

	if (instance) {
		if (adev->asic_type == CHIP_ALDEBARAN)
			reg = SOC15_REG_OFFSET(NBIO, 0, mmBIF_MMSCH1_DOORBELL_RANGE_ALDE);
		else
			reg = SOC15_REG_OFFSET(NBIO, 0, mmBIF_MMSCH1_DOORBELL_RANGE);
	} else
		reg = SOC15_REG_OFFSET(NBIO, 0, mmBIF_MMSCH0_DOORBELL_RANGE);

	doorbell_range = RREG32(reg);

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

static void nbio_v7_4_enable_doorbell_aperture(struct amdgpu_device *adev,
					       bool enable)
{
	WREG32_FIELD15(NBIO, 0, RCC_DOORBELL_APER_EN, BIF_DOORBELL_APER_EN, enable ? 1 : 0);
}

static void nbio_v7_4_enable_doorbell_selfring_aperture(struct amdgpu_device *adev,
							bool enable)
{
	u32 tmp = 0;

	if (enable) {
		tmp = REG_SET_FIELD(tmp, DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_EN, 1) |
		      REG_SET_FIELD(tmp, DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_MODE, 1) |
		      REG_SET_FIELD(tmp, DOORBELL_SELFRING_GPA_APER_CNTL, DOORBELL_SELFRING_GPA_APER_SIZE, 0);

		WREG32_SOC15(NBIO, 0, mmDOORBELL_SELFRING_GPA_APER_BASE_LOW,
			     lower_32_bits(adev->doorbell.base));
		WREG32_SOC15(NBIO, 0, mmDOORBELL_SELFRING_GPA_APER_BASE_HIGH,
			     upper_32_bits(adev->doorbell.base));
	}

	WREG32_SOC15(NBIO, 0, mmDOORBELL_SELFRING_GPA_APER_CNTL, tmp);
}

static void nbio_v7_4_ih_doorbell_range(struct amdgpu_device *adev,
					bool use_doorbell, int doorbell_index)
{
	u32 ih_doorbell_range = RREG32_SOC15(NBIO, 0 , mmBIF_IH_DOORBELL_RANGE);

	if (use_doorbell) {
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, OFFSET, doorbell_index);
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 4);
	} else
		ih_doorbell_range = REG_SET_FIELD(ih_doorbell_range, BIF_IH_DOORBELL_RANGE, SIZE, 0);

	WREG32_SOC15(NBIO, 0, mmBIF_IH_DOORBELL_RANGE, ih_doorbell_range);
}


static void nbio_v7_4_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	//TODO: Add support for v7.4
}

static void nbio_v7_4_update_medium_grain_light_sleep(struct amdgpu_device *adev,
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

static void nbio_v7_4_get_clockgating_state(struct amdgpu_device *adev,
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

static void nbio_v7_4_ih_control(struct amdgpu_device *adev)
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

static u32 nbio_v7_4_get_hdp_flush_req_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmGPU_HDP_FLUSH_REQ);
}

static u32 nbio_v7_4_get_hdp_flush_done_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmGPU_HDP_FLUSH_DONE);
}

static u32 nbio_v7_4_get_pcie_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmPCIE_INDEX2);
}

static u32 nbio_v7_4_get_pcie_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(NBIO, 0, mmPCIE_DATA2);
}

const struct nbio_hdp_flush_reg nbio_v7_4_hdp_flush_reg = {
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
	.ref_and_mask_sdma2 = GPU_HDP_FLUSH_DONE__RSVD_ENG0_MASK,
	.ref_and_mask_sdma3 = GPU_HDP_FLUSH_DONE__RSVD_ENG1_MASK,
	.ref_and_mask_sdma4 = GPU_HDP_FLUSH_DONE__RSVD_ENG2_MASK,
	.ref_and_mask_sdma5 = GPU_HDP_FLUSH_DONE__RSVD_ENG3_MASK,
	.ref_and_mask_sdma6 = GPU_HDP_FLUSH_DONE__RSVD_ENG4_MASK,
	.ref_and_mask_sdma7 = GPU_HDP_FLUSH_DONE__RSVD_ENG5_MASK,
};

static void nbio_v7_4_init_registers(struct amdgpu_device *adev)
{

}

static void nbio_v7_4_handle_ras_controller_intr_no_bifring(struct amdgpu_device *adev)
{
	uint32_t bif_doorbell_intr_cntl;
	struct ras_manager *obj = amdgpu_ras_find_obj(adev, adev->nbio.ras_if);
	struct ras_err_data err_data = {0, 0, 0, NULL};
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	bif_doorbell_intr_cntl = RREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL);
	if (REG_GET_FIELD(bif_doorbell_intr_cntl,
		BIF_DOORBELL_INT_CNTL, RAS_CNTLR_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_DOORBELL_INT_CNTL,
						RAS_CNTLR_INTERRUPT_CLEAR, 1);
		WREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		if (!ras->disable_ras_err_cnt_harvest) {
			/*
			 * clear error status after ras_controller_intr
			 * according to hw team and count ue number
			 * for query
			 */
			nbio_v7_4_query_ras_error_count(adev, &err_data);

			/* logging on error cnt and printing for awareness */
			obj->err_data.ue_count += err_data.ue_count;
			obj->err_data.ce_count += err_data.ce_count;

			if (err_data.ce_count)
				dev_info(adev->dev, "%ld correctable hardware "
						"errors detected in %s block, "
						"no user action is needed.\n",
						obj->err_data.ce_count,
						adev->nbio.ras_if->name);

			if (err_data.ue_count)
				dev_info(adev->dev, "%ld uncorrectable hardware "
						"errors detected in %s block\n",
						obj->err_data.ue_count,
						adev->nbio.ras_if->name);
		}

		dev_info(adev->dev, "RAS controller interrupt triggered "
					"by NBIF error\n");

		/* ras_controller_int is dedicated for nbif ras error,
		 * not the global interrupt for sync flood
		 */
		amdgpu_ras_reset_gpu(adev);
	}
}

static void nbio_v7_4_handle_ras_err_event_athub_intr_no_bifring(struct amdgpu_device *adev)
{
	uint32_t bif_doorbell_intr_cntl;

	bif_doorbell_intr_cntl = RREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL);
	if (REG_GET_FIELD(bif_doorbell_intr_cntl,
		BIF_DOORBELL_INT_CNTL, RAS_ATHUB_ERR_EVENT_INTERRUPT_STATUS)) {
		/* driver has to clear the interrupt status when bif ring is disabled */
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_DOORBELL_INT_CNTL,
						RAS_ATHUB_ERR_EVENT_INTERRUPT_CLEAR, 1);
		WREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		amdgpu_ras_global_ras_isr(adev);
	}
}


static int nbio_v7_4_set_ras_controller_irq_state(struct amdgpu_device *adev,
						  struct amdgpu_irq_src *src,
						  unsigned type,
						  enum amdgpu_interrupt_state state)
{
	/* The ras_controller_irq enablement should be done in psp bl when it
	 * tries to enable ras feature. Driver only need to set the correct interrupt
	 * vector for bare-metal and sriov use case respectively
	 */
	uint32_t bif_intr_cntl;

	bif_intr_cntl = RREG32_SOC15(NBIO, 0, mmBIF_INTR_CNTL);
	if (state == AMDGPU_IRQ_STATE_ENABLE) {
		/* set interrupt vector select bit to 0 to select
		 * vetcor 1 for bare metal case */
		bif_intr_cntl = REG_SET_FIELD(bif_intr_cntl,
					      BIF_INTR_CNTL,
					      RAS_INTR_VEC_SEL, 0);
		WREG32_SOC15(NBIO, 0, mmBIF_INTR_CNTL, bif_intr_cntl);
	}

	return 0;
}

static int nbio_v7_4_process_ras_controller_irq(struct amdgpu_device *adev,
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

static int nbio_v7_4_set_ras_err_event_athub_irq_state(struct amdgpu_device *adev,
						       struct amdgpu_irq_src *src,
						       unsigned type,
						       enum amdgpu_interrupt_state state)
{
	/* The ras_controller_irq enablement should be done in psp bl when it
	 * tries to enable ras feature. Driver only need to set the correct interrupt
	 * vector for bare-metal and sriov use case respectively
	 */
	uint32_t bif_intr_cntl;

	bif_intr_cntl = RREG32_SOC15(NBIO, 0, mmBIF_INTR_CNTL);
	if (state == AMDGPU_IRQ_STATE_ENABLE) {
		/* set interrupt vector select bit to 0 to select
		 * vetcor 1 for bare metal case */
		bif_intr_cntl = REG_SET_FIELD(bif_intr_cntl,
					      BIF_INTR_CNTL,
					      RAS_INTR_VEC_SEL, 0);
		WREG32_SOC15(NBIO, 0, mmBIF_INTR_CNTL, bif_intr_cntl);
	}

	return 0;
}

static int nbio_v7_4_process_err_event_athub_irq(struct amdgpu_device *adev,
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

static const struct amdgpu_irq_src_funcs nbio_v7_4_ras_controller_irq_funcs = {
	.set = nbio_v7_4_set_ras_controller_irq_state,
	.process = nbio_v7_4_process_ras_controller_irq,
};

static const struct amdgpu_irq_src_funcs nbio_v7_4_ras_err_event_athub_irq_funcs = {
	.set = nbio_v7_4_set_ras_err_event_athub_irq_state,
	.process = nbio_v7_4_process_err_event_athub_irq,
};

static int nbio_v7_4_init_ras_controller_interrupt (struct amdgpu_device *adev)
{
	int r;

	/* init the irq funcs */
	adev->nbio.ras_controller_irq.funcs =
		&nbio_v7_4_ras_controller_irq_funcs;
	adev->nbio.ras_controller_irq.num_types = 1;

	/* register ras controller interrupt */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__RAS_CONTROLLER_INTERRUPT,
			      &adev->nbio.ras_controller_irq);

	return r;
}

static int nbio_v7_4_init_ras_err_event_athub_interrupt (struct amdgpu_device *adev)
{

	int r;

	/* init the irq funcs */
	adev->nbio.ras_err_event_athub_irq.funcs =
		&nbio_v7_4_ras_err_event_athub_irq_funcs;
	adev->nbio.ras_err_event_athub_irq.num_types = 1;

	/* register ras err event athub interrupt */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_BIF,
			      NBIF_7_4__SRCID__ERREVENT_ATHUB_INTERRUPT,
			      &adev->nbio.ras_err_event_athub_irq);

	return r;
}

#define smnPARITY_ERROR_STATUS_UNCORR_GRP2	0x13a20030

static void nbio_v7_4_query_ras_error_count(struct amdgpu_device *adev,
					void *ras_error_status)
{
	uint32_t global_sts, central_sts, int_eoi, parity_sts;
	uint32_t corr, fatal, non_fatal;
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;

	global_sts = RREG32_PCIE(smnRAS_GLOBAL_STATUS_LO);
	corr = REG_GET_FIELD(global_sts, RAS_GLOBAL_STATUS_LO, ParityErrCorr);
	fatal = REG_GET_FIELD(global_sts, RAS_GLOBAL_STATUS_LO, ParityErrFatal);
	non_fatal = REG_GET_FIELD(global_sts, RAS_GLOBAL_STATUS_LO,
				ParityErrNonFatal);
	parity_sts = RREG32_PCIE(smnPARITY_ERROR_STATUS_UNCORR_GRP2);

	if (corr)
		err_data->ce_count++;
	if (fatal)
		err_data->ue_count++;

	if (corr || fatal || non_fatal) {
		central_sts = RREG32_PCIE(smnBIFL_RAS_CENTRAL_STATUS);
		/* clear error status register */
		WREG32_PCIE(smnRAS_GLOBAL_STATUS_LO, global_sts);

		if (fatal)
			/* clear parity fatal error indication field */
			WREG32_PCIE(smnPARITY_ERROR_STATUS_UNCORR_GRP2,
				    parity_sts);

		if (REG_GET_FIELD(central_sts, BIFL_RAS_CENTRAL_STATUS,
				BIFL_RasContller_Intr_Recv)) {
			/* clear interrupt status register */
			WREG32_PCIE(smnBIFL_RAS_CENTRAL_STATUS, central_sts);
			int_eoi = RREG32_PCIE(smnIOHC_INTERRUPT_EOI);
			int_eoi = REG_SET_FIELD(int_eoi,
					IOHC_INTERRUPT_EOI, SMI_EOI, 1);
			WREG32_PCIE(smnIOHC_INTERRUPT_EOI, int_eoi);
		}
	}
}

static void nbio_v7_4_enable_doorbell_interrupt(struct amdgpu_device *adev,
						bool enable)
{
	WREG32_FIELD15(NBIO, 0, BIF_DOORBELL_INT_CNTL,
		       DOORBELL_INTERRUPT_DISABLE, enable ? 0 : 1);
}

const struct amdgpu_nbio_ras_funcs nbio_v7_4_ras_funcs = {
	.handle_ras_controller_intr_no_bifring = nbio_v7_4_handle_ras_controller_intr_no_bifring,
	.handle_ras_err_event_athub_intr_no_bifring = nbio_v7_4_handle_ras_err_event_athub_intr_no_bifring,
	.init_ras_controller_interrupt = nbio_v7_4_init_ras_controller_interrupt,
	.init_ras_err_event_athub_interrupt = nbio_v7_4_init_ras_err_event_athub_interrupt,
	.query_ras_error_count = nbio_v7_4_query_ras_error_count,
	.ras_late_init = amdgpu_nbio_ras_late_init,
	.ras_fini = amdgpu_nbio_ras_fini,
};

static void nbio_v7_4_program_ltr(struct amdgpu_device *adev)
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

static void nbio_v7_4_program_aspm(struct amdgpu_device *adev)
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

	nbio_v7_4_program_ltr(adev);

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
}

const struct amdgpu_nbio_funcs nbio_v7_4_funcs = {
	.get_hdp_flush_req_offset = nbio_v7_4_get_hdp_flush_req_offset,
	.get_hdp_flush_done_offset = nbio_v7_4_get_hdp_flush_done_offset,
	.get_pcie_index_offset = nbio_v7_4_get_pcie_index_offset,
	.get_pcie_data_offset = nbio_v7_4_get_pcie_data_offset,
	.get_rev_id = nbio_v7_4_get_rev_id,
	.mc_access_enable = nbio_v7_4_mc_access_enable,
	.get_memsize = nbio_v7_4_get_memsize,
	.sdma_doorbell_range = nbio_v7_4_sdma_doorbell_range,
	.vcn_doorbell_range = nbio_v7_4_vcn_doorbell_range,
	.enable_doorbell_aperture = nbio_v7_4_enable_doorbell_aperture,
	.enable_doorbell_selfring_aperture = nbio_v7_4_enable_doorbell_selfring_aperture,
	.ih_doorbell_range = nbio_v7_4_ih_doorbell_range,
	.enable_doorbell_interrupt = nbio_v7_4_enable_doorbell_interrupt,
	.update_medium_grain_clock_gating = nbio_v7_4_update_medium_grain_clock_gating,
	.update_medium_grain_light_sleep = nbio_v7_4_update_medium_grain_light_sleep,
	.get_clockgating_state = nbio_v7_4_get_clockgating_state,
	.ih_control = nbio_v7_4_ih_control,
	.init_registers = nbio_v7_4_init_registers,
	.remap_hdp_registers = nbio_v7_4_remap_hdp_registers,
	.program_aspm =  nbio_v7_4_program_aspm,
};

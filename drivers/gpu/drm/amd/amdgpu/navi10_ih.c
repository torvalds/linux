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

#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_ih.h"

#include "oss/osssys_5_0_0_offset.h"
#include "oss/osssys_5_0_0_sh_mask.h"

#include "soc15_common.h"
#include "navi10_ih.h"

#define MAX_REARM_RETRY 10

#define mmIH_CHICKEN_Sienna_Cichlid                 0x018d
#define mmIH_CHICKEN_Sienna_Cichlid_BASE_IDX        0

static void navi10_ih_set_interrupt_funcs(struct amdgpu_device *adev);

/**
 * force_update_wptr_for_self_int - Force update the wptr for self interrupt
 *
 * @adev: amdgpu_device pointer
 * @threshold: threshold to trigger the wptr reporting
 * @timeout: timeout to trigger the wptr reporting
 * @enabled: Enable/disable timeout flush mechanism
 *
 * threshold input range: 0 ~ 15, default 0,
 * real_threshold = 2^threshold
 * timeout input range: 0 ~ 20, default 8,
 * real_timeout = (2^timeout) * 1024 / (socclk_freq)
 *
 * Force update wptr for self interrupt ( >= SIENNA_CICHLID).
 */
static void
force_update_wptr_for_self_int(struct amdgpu_device *adev,
			       u32 threshold, u32 timeout, bool enabled)
{
	u32 ih_cntl, ih_rb_cntl;

	if (adev->asic_type < CHIP_SIENNA_CICHLID)
		return;

	ih_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_CNTL2);
	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1);

	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL2,
				SELF_IV_FORCE_WPTR_UPDATE_TIMEOUT, timeout);
	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL2,
				SELF_IV_FORCE_WPTR_UPDATE_ENABLE, enabled);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING1,
				   RB_USED_INT_THRESHOLD, threshold);

	WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1, ih_rb_cntl);
	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING2,
				   RB_USED_INT_THRESHOLD, threshold);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2, ih_rb_cntl);
	WREG32_SOC15(OSSSYS, 0, mmIH_CNTL2, ih_cntl);
}

/**
 * navi10_ih_enable_interrupts - Enable the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 *
 * Enable the interrupt ring buffer (NAVI10).
 */
static void navi10_ih_enable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, ENABLE_INTR, 1);
	if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
		if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL, ih_rb_cntl)) {
			DRM_ERROR("PSP program IH_RB_CNTL failed!\n");
			return;
		}
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL, ih_rb_cntl);
	}

	adev->irq.ih.enabled = true;

	if (adev->irq.ih1.ring_size) {
		ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING1,
					   RB_ENABLE, 1);
		if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
			if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING1,
						ih_rb_cntl)) {
				DRM_ERROR("program IH_RB_CNTL_RING1 failed!\n");
				return;
			}
		} else {
			WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1, ih_rb_cntl);
		}
		adev->irq.ih1.enabled = true;
	}

	if (adev->irq.ih2.ring_size) {
		ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING2,
					   RB_ENABLE, 1);
		if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
			if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING2,
						ih_rb_cntl)) {
				DRM_ERROR("program IH_RB_CNTL_RING2 failed!\n");
				return;
			}
		} else {
			WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2, ih_rb_cntl);
		}
		adev->irq.ih2.enabled = true;
	}
}

/**
 * navi10_ih_disable_interrupts - Disable the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 *
 * Disable the interrupt ring buffer (NAVI10).
 */
static void navi10_ih_disable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_ENABLE, 0);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, ENABLE_INTR, 0);
	if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
		if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL, ih_rb_cntl)) {
			DRM_ERROR("PSP program IH_RB_CNTL failed!\n");
			return;
		}
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL, ih_rb_cntl);
	}

	/* set rptr, wptr to 0 */
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR, 0);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR, 0);
	adev->irq.ih.enabled = false;
	adev->irq.ih.rptr = 0;

	if (adev->irq.ih1.ring_size) {
		ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING1,
					   RB_ENABLE, 0);
		if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
			if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING1,
						ih_rb_cntl)) {
				DRM_ERROR("program IH_RB_CNTL_RING1 failed!\n");
				return;
			}
		} else {
			WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1, ih_rb_cntl);
		}
		/* set rptr, wptr to 0 */
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR_RING1, 0);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_RING1, 0);
		adev->irq.ih1.enabled = false;
		adev->irq.ih1.rptr = 0;
	}

	if (adev->irq.ih2.ring_size) {
		ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING2,
					   RB_ENABLE, 0);
		if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
			if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING2,
						ih_rb_cntl)) {
				DRM_ERROR("program IH_RB_CNTL_RING2 failed!\n");
				return;
			}
		} else {
			WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2, ih_rb_cntl);
		}
		/* set rptr, wptr to 0 */
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR_RING2, 0);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_RING2, 0);
		adev->irq.ih2.enabled = false;
		adev->irq.ih2.rptr = 0;
	}

}

static uint32_t navi10_ih_rb_cntl(struct amdgpu_ih_ring *ih, uint32_t ih_rb_cntl)
{
	int rb_bufsz = order_base_2(ih->ring_size / 4);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
				   MC_SPACE, ih->use_bus_addr ? 1 : 4);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
				   WPTR_OVERFLOW_CLEAR, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
				   WPTR_OVERFLOW_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_SIZE, rb_bufsz);
	/* Ring Buffer write pointer writeback. If enabled, IH_RB_WPTR register
	 * value is written to memory
	 */
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
				   WPTR_WRITEBACK_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_SNOOP, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_RO, 0);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_VMID, 0);

	return ih_rb_cntl;
}

static uint32_t navi10_ih_doorbell_rptr(struct amdgpu_ih_ring *ih)
{
	u32 ih_doorbell_rtpr = 0;

	if (ih->use_doorbell) {
		ih_doorbell_rtpr = REG_SET_FIELD(ih_doorbell_rtpr,
						 IH_DOORBELL_RPTR, OFFSET,
						 ih->doorbell_index);
		ih_doorbell_rtpr = REG_SET_FIELD(ih_doorbell_rtpr,
						 IH_DOORBELL_RPTR,
						 ENABLE, 1);
	} else {
		ih_doorbell_rtpr = REG_SET_FIELD(ih_doorbell_rtpr,
						 IH_DOORBELL_RPTR,
						 ENABLE, 0);
	}
	return ih_doorbell_rtpr;
}

static void navi10_ih_reroute_ih(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Reroute to IH ring 1 for VMC */
	WREG32_SOC15(OSSSYS, 0, mmIH_CLIENT_CFG_INDEX, 0x12);
	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_CLIENT_CFG_DATA);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, CLIENT_TYPE, 1);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, RING_ID, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_CLIENT_CFG_DATA, tmp);

	/* Reroute IH ring 1 for UMC */
	WREG32_SOC15(OSSSYS, 0, mmIH_CLIENT_CFG_INDEX, 0x1B);
	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_CLIENT_CFG_DATA);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, RING_ID, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_CLIENT_CFG_DATA, tmp);
}

/**
 * navi10_ih_irq_init - init and enable the interrupt ring
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate a ring buffer for the interrupt controller,
 * enable the RLC, disable interrupts, enable the IH
 * ring buffer and enable it (NAVI).
 * Called at device load and reume.
 * Returns 0 for success, errors for failure.
 */
static int navi10_ih_irq_init(struct amdgpu_device *adev)
{
	struct amdgpu_ih_ring *ih = &adev->irq.ih;
	u32 ih_rb_cntl, ih_chicken;
	u32 tmp;

	/* disable irqs */
	navi10_ih_disable_interrupts(adev);

	adev->nbio.funcs->ih_control(adev);

	/* Ring Buffer base. [39:8] of 40-bit address of the beginning of the ring buffer*/
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE, ih->gpu_addr >> 8);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_HI, (ih->gpu_addr >> 40) & 0xff);

	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL);
	ih_rb_cntl = navi10_ih_rb_cntl(ih, ih_rb_cntl);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RPTR_REARM,
				   !!adev->irq.msi_enabled);
	if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
		if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL, ih_rb_cntl)) {
			DRM_ERROR("PSP program IH_RB_CNTL failed!\n");
			return -ETIMEDOUT;
		}
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL, ih_rb_cntl);
	}
	if (adev->irq.ih1.ring_size)
		navi10_ih_reroute_ih(adev);

	if (unlikely(adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT)) {
		if (ih->use_bus_addr) {
			switch (adev->asic_type) {
			case CHIP_SIENNA_CICHLID:
			case CHIP_NAVY_FLOUNDER:
				ih_chicken = RREG32_SOC15(OSSSYS, 0, mmIH_CHICKEN_Sienna_Cichlid);
				ih_chicken = REG_SET_FIELD(ih_chicken,
						IH_CHICKEN, MC_SPACE_GPA_ENABLE, 1);
				WREG32_SOC15(OSSSYS, 0, mmIH_CHICKEN_Sienna_Cichlid, ih_chicken);
				break;
			default:
				ih_chicken = RREG32_SOC15(OSSSYS, 0, mmIH_CHICKEN);
				ih_chicken = REG_SET_FIELD(ih_chicken,
						IH_CHICKEN, MC_SPACE_GPA_ENABLE, 1);
				WREG32_SOC15(OSSSYS, 0, mmIH_CHICKEN, ih_chicken);
				break;
			}
		}
	}

	/* set the writeback address whether it's enabled or not */
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_ADDR_LO,
		     lower_32_bits(ih->wptr_addr));
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_ADDR_HI,
		     upper_32_bits(ih->wptr_addr) & 0xFFFF);

	/* set rptr, wptr to 0 */
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR, 0);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR, 0);

	WREG32_SOC15(OSSSYS, 0, mmIH_DOORBELL_RPTR,
			navi10_ih_doorbell_rptr(ih));

	adev->nbio.funcs->ih_doorbell_range(adev, ih->use_doorbell,
					    ih->doorbell_index);

	ih = &adev->irq.ih1;
	if (ih->ring_size) {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_RING1, ih->gpu_addr >> 8);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_HI_RING1,
			     (ih->gpu_addr >> 40) & 0xff);

		ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1);
		ih_rb_cntl = navi10_ih_rb_cntl(ih, ih_rb_cntl);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
					   WPTR_OVERFLOW_ENABLE, 0);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
					   RB_FULL_DRAIN_ENABLE, 1);
		if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
			if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING1,
						ih_rb_cntl)) {
				DRM_ERROR("program IH_RB_CNTL_RING1 failed!\n");
				return -ETIMEDOUT;
			}
		} else {
			WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1, ih_rb_cntl);
		}
		/* set rptr, wptr to 0 */
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_RING1, 0);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR_RING1, 0);

		WREG32_SOC15(OSSSYS, 0, mmIH_DOORBELL_RPTR_RING1,
				navi10_ih_doorbell_rptr(ih));
	}

	ih = &adev->irq.ih2;
	if (ih->ring_size) {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_RING2, ih->gpu_addr >> 8);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_HI_RING2,
			     (ih->gpu_addr >> 40) & 0xff);

		ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2);
		ih_rb_cntl = navi10_ih_rb_cntl(ih, ih_rb_cntl);

		if (amdgpu_sriov_vf(adev) && adev->asic_type < CHIP_NAVI10) {
			if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING2,
						ih_rb_cntl)) {
				DRM_ERROR("program IH_RB_CNTL_RING2 failed!\n");
				return -ETIMEDOUT;
			}
		} else {
			WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2, ih_rb_cntl);
		}
		/* set rptr, wptr to 0 */
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_RING2, 0);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR_RING2, 0);

		WREG32_SOC15(OSSSYS, 0, mmIH_DOORBELL_RPTR_RING2,
			     navi10_ih_doorbell_rptr(ih));
	}


	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_STORM_CLIENT_LIST_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_STORM_CLIENT_LIST_CNTL,
			    CLIENT18_IS_STORM_CLIENT, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_STORM_CLIENT_LIST_CNTL, tmp);

	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_INT_FLOOD_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_INT_FLOOD_CNTL, FLOOD_CNTL_ENABLE, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_INT_FLOOD_CNTL, tmp);

	pci_set_master(adev->pdev);

	/* enable interrupts */
	navi10_ih_enable_interrupts(adev);
	/* enable wptr force update for self int */
	force_update_wptr_for_self_int(adev, 0, 8, true);

	return 0;
}

/**
 * navi10_ih_irq_disable - disable interrupts
 *
 * @adev: amdgpu_device pointer
 *
 * Disable interrupts on the hw (NAVI10).
 */
static void navi10_ih_irq_disable(struct amdgpu_device *adev)
{
	force_update_wptr_for_self_int(adev, 0, 8, false);
	navi10_ih_disable_interrupts(adev);

	/* Wait and acknowledge irq */
	mdelay(1);
}

/**
 * navi10_ih_get_wptr - get the IH ring buffer wptr
 *
 * @adev: amdgpu_device pointer
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer (NAVI10).  Also check for
 * ring buffer overflow and deal with it.
 * Returns the value of the wptr.
 */
static u32 navi10_ih_get_wptr(struct amdgpu_device *adev,
			      struct amdgpu_ih_ring *ih)
{
	u32 wptr, reg, tmp;

	wptr = le32_to_cpu(*ih->wptr_cpu);

	if (!REG_GET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW))
		goto out;

	if (ih == &adev->irq.ih)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR);
	else if (ih == &adev->irq.ih1)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR_RING1);
	else if (ih == &adev->irq.ih2)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR_RING2);
	else
		BUG();

	wptr = RREG32_NO_KIQ(reg);
	if (!REG_GET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW))
		goto out;
	wptr = REG_SET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW, 0);

	/* When a ring buffer overflow happen start parsing interrupt
	 * from the last not overwritten vector (wptr + 32). Hopefully
	 * this should allow us to catch up.
	 */
	tmp = (wptr + 32) & ih->ptr_mask;
	dev_warn(adev->dev, "IH ring buffer overflow "
		 "(0x%08X, 0x%08X, 0x%08X)\n",
		 wptr, ih->rptr, tmp);
	ih->rptr = tmp;

	if (ih == &adev->irq.ih)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL);
	else if (ih == &adev->irq.ih1)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL_RING1);
	else if (ih == &adev->irq.ih2)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL_RING2);
	else
		BUG();

	tmp = RREG32_NO_KIQ(reg);
	tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 1);
	WREG32_NO_KIQ(reg, tmp);
out:
	return (wptr & ih->ptr_mask);
}

/**
 * navi10_ih_decode_iv - decode an interrupt vector
 *
 * @adev: amdgpu_device pointer
 *
 * Decodes the interrupt vector at the current rptr
 * position and also advance the position.
 */
static void navi10_ih_decode_iv(struct amdgpu_device *adev,
				struct amdgpu_ih_ring *ih,
				struct amdgpu_iv_entry *entry)
{
	/* wptr/rptr are in bytes! */
	u32 ring_index = ih->rptr >> 2;
	uint32_t dw[8];

	dw[0] = le32_to_cpu(ih->ring[ring_index + 0]);
	dw[1] = le32_to_cpu(ih->ring[ring_index + 1]);
	dw[2] = le32_to_cpu(ih->ring[ring_index + 2]);
	dw[3] = le32_to_cpu(ih->ring[ring_index + 3]);
	dw[4] = le32_to_cpu(ih->ring[ring_index + 4]);
	dw[5] = le32_to_cpu(ih->ring[ring_index + 5]);
	dw[6] = le32_to_cpu(ih->ring[ring_index + 6]);
	dw[7] = le32_to_cpu(ih->ring[ring_index + 7]);

	entry->client_id = dw[0] & 0xff;
	entry->src_id = (dw[0] >> 8) & 0xff;
	entry->ring_id = (dw[0] >> 16) & 0xff;
	entry->vmid = (dw[0] >> 24) & 0xf;
	entry->vmid_src = (dw[0] >> 31);
	entry->timestamp = dw[1] | ((u64)(dw[2] & 0xffff) << 32);
	entry->timestamp_src = dw[2] >> 31;
	entry->pasid = dw[3] & 0xffff;
	entry->pasid_src = dw[3] >> 31;
	entry->src_data[0] = dw[4];
	entry->src_data[1] = dw[5];
	entry->src_data[2] = dw[6];
	entry->src_data[3] = dw[7];

	/* wptr/rptr are in bytes! */
	ih->rptr += 32;
}

/**
 * navi10_ih_irq_rearm - rearm IRQ if lost
 *
 * @adev: amdgpu_device pointer
 *
 */
static void navi10_ih_irq_rearm(struct amdgpu_device *adev,
			       struct amdgpu_ih_ring *ih)
{
	uint32_t reg_rptr = 0;
	uint32_t v = 0;
	uint32_t i = 0;

	if (ih == &adev->irq.ih)
		reg_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_RPTR);
	else if (ih == &adev->irq.ih1)
		reg_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_RPTR_RING1);
	else if (ih == &adev->irq.ih2)
		reg_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_RPTR_RING2);
	else
		return;

	/* Rearm IRQ / re-write doorbell if doorbell write is lost */
	for (i = 0; i < MAX_REARM_RETRY; i++) {
		v = RREG32_NO_KIQ(reg_rptr);
		if ((v < ih->ring_size) && (v != ih->rptr))
			WDOORBELL32(ih->doorbell_index, ih->rptr);
		else
			break;
	}
}

/**
 * navi10_ih_set_rptr - set the IH ring buffer rptr
 *
 * @adev: amdgpu_device pointer
 *
 * Set the IH ring buffer rptr.
 */
static void navi10_ih_set_rptr(struct amdgpu_device *adev,
			       struct amdgpu_ih_ring *ih)
{
	if (ih->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		*ih->rptr_cpu = ih->rptr;
		WDOORBELL32(ih->doorbell_index, ih->rptr);

		if (amdgpu_sriov_vf(adev))
			navi10_ih_irq_rearm(adev, ih);
	} else if (ih == &adev->irq.ih) {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR, ih->rptr);
	} else if (ih == &adev->irq.ih1) {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR_RING1, ih->rptr);
	} else if (ih == &adev->irq.ih2) {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR_RING2, ih->rptr);
	}
}

/**
 * navi10_ih_self_irq - dispatch work for ring 1 and 2
 *
 * @adev: amdgpu_device pointer
 * @source: irq source
 * @entry: IV with WPTR update
 *
 * Update the WPTR from the IV and schedule work to handle the entries.
 */
static int navi10_ih_self_irq(struct amdgpu_device *adev,
			      struct amdgpu_irq_src *source,
			      struct amdgpu_iv_entry *entry)
{
	uint32_t wptr = cpu_to_le32(entry->src_data[0]);

	switch (entry->ring_id) {
	case 1:
		*adev->irq.ih1.wptr_cpu = wptr;
		schedule_work(&adev->irq.ih1_work);
		break;
	case 2:
		*adev->irq.ih2.wptr_cpu = wptr;
		schedule_work(&adev->irq.ih2_work);
		break;
	default: break;
	}
	return 0;
}

static const struct amdgpu_irq_src_funcs navi10_ih_self_irq_funcs = {
	.process = navi10_ih_self_irq,
};

static void navi10_ih_set_self_irq_funcs(struct amdgpu_device *adev)
{
	adev->irq.self_irq.num_types = 0;
	adev->irq.self_irq.funcs = &navi10_ih_self_irq_funcs;
}

static int navi10_ih_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	navi10_ih_set_interrupt_funcs(adev);
	navi10_ih_set_self_irq_funcs(adev);
	return 0;
}

static int navi10_ih_sw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool use_bus_addr;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_IH, 0,
				&adev->irq.self_irq);

	if (r)
		return r;

	/* use gpu virtual address for ih ring
	 * until ih_checken is programmed to allow
	 * use bus address for ih ring by psp bl */
	use_bus_addr =
		(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) ? false : true;
	r = amdgpu_ih_ring_init(adev, &adev->irq.ih, 256 * 1024, use_bus_addr);
	if (r)
		return r;

	adev->irq.ih.use_doorbell = true;
	adev->irq.ih.doorbell_index = adev->doorbell_index.ih << 1;

	adev->irq.ih1.ring_size = 0;
	adev->irq.ih2.ring_size = 0;

	if (adev->asic_type < CHIP_NAVI10) {
		r = amdgpu_ih_ring_init(adev, &adev->irq.ih1, PAGE_SIZE, true);
		if (r)
			return r;

		adev->irq.ih1.use_doorbell = true;
		adev->irq.ih1.doorbell_index =
					(adev->doorbell_index.ih + 1) << 1;

		r = amdgpu_ih_ring_init(adev, &adev->irq.ih2, PAGE_SIZE, true);
		if (r)
			return r;

		adev->irq.ih2.use_doorbell = true;
		adev->irq.ih2.doorbell_index =
					(adev->doorbell_index.ih + 2) << 1;
	}

	r = amdgpu_irq_init(adev);

	return r;
}

static int navi10_ih_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_fini(adev);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih2);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih1);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih);

	return 0;
}

static int navi10_ih_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = navi10_ih_irq_init(adev);
	if (r)
		return r;

	return 0;
}

static int navi10_ih_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	navi10_ih_irq_disable(adev);

	return 0;
}

static int navi10_ih_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return navi10_ih_hw_fini(adev);
}

static int navi10_ih_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return navi10_ih_hw_init(adev);
}

static bool navi10_ih_is_idle(void *handle)
{
	/* todo */
	return true;
}

static int navi10_ih_wait_for_idle(void *handle)
{
	/* todo */
	return -ETIMEDOUT;
}

static int navi10_ih_soft_reset(void *handle)
{
	/* todo */
	return 0;
}

static void navi10_ih_update_clockgating_state(struct amdgpu_device *adev,
					       bool enable)
{
	uint32_t data, def, field_val;

	if (adev->cg_flags & AMD_CG_SUPPORT_IH_CG) {
		def = data = RREG32_SOC15(OSSSYS, 0, mmIH_CLK_CTRL);
		field_val = enable ? 0 : 1;
		data = REG_SET_FIELD(data, IH_CLK_CTRL,
				     DBUS_MUX_CLK_SOFT_OVERRIDE, field_val);
		data = REG_SET_FIELD(data, IH_CLK_CTRL,
				     OSSSYS_SHARE_CLK_SOFT_OVERRIDE, field_val);
		data = REG_SET_FIELD(data, IH_CLK_CTRL,
				     LIMIT_SMN_CLK_SOFT_OVERRIDE, field_val);
		data = REG_SET_FIELD(data, IH_CLK_CTRL,
				     DYN_CLK_SOFT_OVERRIDE, field_val);
		data = REG_SET_FIELD(data, IH_CLK_CTRL,
				     REG_CLK_SOFT_OVERRIDE, field_val);
		if (def != data)
			WREG32_SOC15(OSSSYS, 0, mmIH_CLK_CTRL, data);
	}

	return;
}

static int navi10_ih_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	navi10_ih_update_clockgating_state(adev,
				state == AMD_CG_STATE_GATE);
	return 0;
}

static int navi10_ih_set_powergating_state(void *handle,
					   enum amd_powergating_state state)
{
	return 0;
}

static void navi10_ih_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!RREG32_SOC15(OSSSYS, 0, mmIH_CLK_CTRL))
		*flags |= AMD_CG_SUPPORT_IH_CG;

	return;
}

static const struct amd_ip_funcs navi10_ih_ip_funcs = {
	.name = "navi10_ih",
	.early_init = navi10_ih_early_init,
	.late_init = NULL,
	.sw_init = navi10_ih_sw_init,
	.sw_fini = navi10_ih_sw_fini,
	.hw_init = navi10_ih_hw_init,
	.hw_fini = navi10_ih_hw_fini,
	.suspend = navi10_ih_suspend,
	.resume = navi10_ih_resume,
	.is_idle = navi10_ih_is_idle,
	.wait_for_idle = navi10_ih_wait_for_idle,
	.soft_reset = navi10_ih_soft_reset,
	.set_clockgating_state = navi10_ih_set_clockgating_state,
	.set_powergating_state = navi10_ih_set_powergating_state,
	.get_clockgating_state = navi10_ih_get_clockgating_state,
};

static const struct amdgpu_ih_funcs navi10_ih_funcs = {
	.get_wptr = navi10_ih_get_wptr,
	.decode_iv = navi10_ih_decode_iv,
	.set_rptr = navi10_ih_set_rptr
};

static void navi10_ih_set_interrupt_funcs(struct amdgpu_device *adev)
{
	if (adev->irq.ih_funcs == NULL)
		adev->irq.ih_funcs = &navi10_ih_funcs;
}

const struct amdgpu_ip_block_version navi10_ih_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_IH,
	.major = 5,
	.minor = 0,
	.rev = 0,
	.funcs = &navi10_ih_ip_funcs,
};

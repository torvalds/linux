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
 * navi10_ih_init_register_offset - Initialize register offset for ih rings
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize register offset ih rings (NAVI10).
 */
static void navi10_ih_init_register_offset(struct amdgpu_device *adev)
{
	struct amdgpu_ih_regs *ih_regs;

	if (adev->irq.ih.ring_size) {
		ih_regs = &adev->irq.ih.ih_regs;
		ih_regs->ih_rb_base = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_BASE);
		ih_regs->ih_rb_base_hi = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_BASE_HI);
		ih_regs->ih_rb_cntl = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL);
		ih_regs->ih_rb_wptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR);
		ih_regs->ih_rb_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_RPTR);
		ih_regs->ih_doorbell_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_DOORBELL_RPTR);
		ih_regs->ih_rb_wptr_addr_lo = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR_ADDR_LO);
		ih_regs->ih_rb_wptr_addr_hi = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR_ADDR_HI);
		ih_regs->psp_reg_id = PSP_REG_IH_RB_CNTL;
	}

	if (adev->irq.ih1.ring_size) {
		ih_regs = &adev->irq.ih1.ih_regs;
		ih_regs->ih_rb_base = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_BASE_RING1);
		ih_regs->ih_rb_base_hi = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_BASE_HI_RING1);
		ih_regs->ih_rb_cntl = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL_RING1);
		ih_regs->ih_rb_wptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR_RING1);
		ih_regs->ih_rb_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_RPTR_RING1);
		ih_regs->ih_doorbell_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_DOORBELL_RPTR_RING1);
		ih_regs->psp_reg_id = PSP_REG_IH_RB_CNTL_RING1;
	}

	if (adev->irq.ih2.ring_size) {
		ih_regs = &adev->irq.ih2.ih_regs;
		ih_regs->ih_rb_base = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_BASE_RING2);
		ih_regs->ih_rb_base_hi = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_BASE_HI_RING2);
		ih_regs->ih_rb_cntl = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL_RING2);
		ih_regs->ih_rb_wptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_WPTR_RING2);
		ih_regs->ih_rb_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_RPTR_RING2);
		ih_regs->ih_doorbell_rptr = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_DOORBELL_RPTR_RING2);
		ih_regs->psp_reg_id = PSP_REG_IH_RB_CNTL_RING2;
	}
}

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

	if (adev->ip_versions[OSSSYS_HWIP][0] < IP_VERSION(5, 0, 3))
		return;

	ih_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_CNTL2);
	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1);

	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL2,
				SELF_IV_FORCE_WPTR_UPDATE_TIMEOUT, timeout);
	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL2,
				SELF_IV_FORCE_WPTR_UPDATE_ENABLE, enabled);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING1,
				   RB_USED_INT_THRESHOLD, threshold);

	if (amdgpu_sriov_vf(adev) && amdgpu_sriov_reg_indirect_ih(adev)) {
		if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING1, ih_rb_cntl))
			return;
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING1, ih_rb_cntl);
	}

	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL_RING2,
				   RB_USED_INT_THRESHOLD, threshold);
	if (amdgpu_sriov_vf(adev) && amdgpu_sriov_reg_indirect_ih(adev)) {
		if (psp_reg_program(&adev->psp, PSP_REG_IH_RB_CNTL_RING2, ih_rb_cntl))
			return;
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL_RING2, ih_rb_cntl);
	}

	WREG32_SOC15(OSSSYS, 0, mmIH_CNTL2, ih_cntl);
}

/**
 * navi10_ih_toggle_ring_interrupts - toggle the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointet
 * @enable: true - enable the interrupts, false - disable the interrupts
 *
 * Toggle the interrupt ring buffer (NAVI10)
 */
static int navi10_ih_toggle_ring_interrupts(struct amdgpu_device *adev,
					    struct amdgpu_ih_ring *ih,
					    bool enable)
{
	struct amdgpu_ih_regs *ih_regs;
	uint32_t tmp;

	ih_regs = &ih->ih_regs;

	tmp = RREG32(ih_regs->ih_rb_cntl);
	tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, RB_ENABLE, (enable ? 1 : 0));
	/* enable_intr field is only valid in ring0 */
	if (ih == &adev->irq.ih)
		tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, ENABLE_INTR, (enable ? 1 : 0));

	if (amdgpu_sriov_vf(adev) && amdgpu_sriov_reg_indirect_ih(adev)) {
		if (psp_reg_program(&adev->psp, ih_regs->psp_reg_id, tmp))
			return -ETIMEDOUT;
	} else {
		WREG32(ih_regs->ih_rb_cntl, tmp);
	}

	if (enable) {
		ih->enabled = true;
	} else {
		/* set rptr, wptr to 0 */
		WREG32(ih_regs->ih_rb_rptr, 0);
		WREG32(ih_regs->ih_rb_wptr, 0);
		ih->enabled = false;
		ih->rptr = 0;
	}

	return 0;
}

/**
 * navi10_ih_toggle_interrupts - Toggle all the available interrupt ring buffers
 *
 * @adev: amdgpu_device pointer
 * @enable: enable or disable interrupt ring buffers
 *
 * Toggle all the available interrupt ring buffers (NAVI10).
 */
static int navi10_ih_toggle_interrupts(struct amdgpu_device *adev, bool enable)
{
	struct amdgpu_ih_ring *ih[] = {&adev->irq.ih, &adev->irq.ih1, &adev->irq.ih2};
	int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(ih); i++) {
		if (ih[i]->ring_size) {
			r = navi10_ih_toggle_ring_interrupts(adev, ih[i], enable);
			if (r)
				return r;
		}
	}

	return 0;
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

/**
 * navi10_ih_enable_ring - enable an ih ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointer
 *
 * Enable an ih ring buffer (NAVI10)
 */
static int navi10_ih_enable_ring(struct amdgpu_device *adev,
				 struct amdgpu_ih_ring *ih)
{
	struct amdgpu_ih_regs *ih_regs;
	uint32_t tmp;

	ih_regs = &ih->ih_regs;

	/* Ring Buffer base. [39:8] of 40-bit address of the beginning of the ring buffer*/
	WREG32(ih_regs->ih_rb_base, ih->gpu_addr >> 8);
	WREG32(ih_regs->ih_rb_base_hi, (ih->gpu_addr >> 40) & 0xff);

	tmp = RREG32(ih_regs->ih_rb_cntl);
	tmp = navi10_ih_rb_cntl(ih, tmp);
	if (ih == &adev->irq.ih)
		tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, RPTR_REARM, !!adev->irq.msi_enabled);
	if (ih == &adev->irq.ih1) {
		tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, WPTR_OVERFLOW_ENABLE, 0);
		tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, RB_FULL_DRAIN_ENABLE, 1);
	}

	if (amdgpu_sriov_vf(adev) && amdgpu_sriov_reg_indirect_ih(adev)) {
		if (psp_reg_program(&adev->psp, ih_regs->psp_reg_id, tmp)) {
			DRM_ERROR("PSP program IH_RB_CNTL failed!\n");
			return -ETIMEDOUT;
		}
	} else {
		WREG32(ih_regs->ih_rb_cntl, tmp);
	}

	if (ih == &adev->irq.ih) {
		/* set the ih ring 0 writeback address whether it's enabled or not */
		WREG32(ih_regs->ih_rb_wptr_addr_lo, lower_32_bits(ih->wptr_addr));
		WREG32(ih_regs->ih_rb_wptr_addr_hi, upper_32_bits(ih->wptr_addr) & 0xFFFF);
	}

	/* set rptr, wptr to 0 */
	WREG32(ih_regs->ih_rb_wptr, 0);
	WREG32(ih_regs->ih_rb_rptr, 0);

	WREG32(ih_regs->ih_doorbell_rptr, navi10_ih_doorbell_rptr(ih));

	return 0;
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
	struct amdgpu_ih_ring *ih[] = {&adev->irq.ih, &adev->irq.ih1, &adev->irq.ih2};
	u32 ih_chicken;
	u32 tmp;
	int ret;
	int i;

	/* disable irqs */
	ret = navi10_ih_toggle_interrupts(adev, false);
	if (ret)
		return ret;

	adev->nbio.funcs->ih_control(adev);

	if (unlikely(adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT)) {
		if (ih[0]->use_bus_addr) {
			switch (adev->ip_versions[OSSSYS_HWIP][0]) {
			case IP_VERSION(5, 0, 3):
			case IP_VERSION(5, 2, 0):
			case IP_VERSION(5, 2, 1):
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

	for (i = 0; i < ARRAY_SIZE(ih); i++) {
		if (ih[i]->ring_size) {
			ret = navi10_ih_enable_ring(adev, ih[i]);
			if (ret)
				return ret;
		}
	}

	/* update doorbell range for ih ring 0*/
	adev->nbio.funcs->ih_doorbell_range(adev, ih[0]->use_doorbell,
					    ih[0]->doorbell_index);

	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_STORM_CLIENT_LIST_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_STORM_CLIENT_LIST_CNTL,
			    CLIENT18_IS_STORM_CLIENT, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_STORM_CLIENT_LIST_CNTL, tmp);

	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_INT_FLOOD_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_INT_FLOOD_CNTL, FLOOD_CNTL_ENABLE, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_INT_FLOOD_CNTL, tmp);

	pci_set_master(adev->pdev);

	/* enable interrupts */
	ret = navi10_ih_toggle_interrupts(adev, true);
	if (ret)
		return ret;
	/* enable wptr force update for self int */
	force_update_wptr_for_self_int(adev, 0, 8, true);

	if (adev->irq.ih_soft.ring_size)
		adev->irq.ih_soft.enabled = true;

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
	navi10_ih_toggle_interrupts(adev, false);

	/* Wait and acknowledge irq */
	mdelay(1);
}

/**
 * navi10_ih_get_wptr - get the IH ring buffer wptr
 *
 * @adev: amdgpu_device pointer
 * @ih: IH ring buffer to fetch wptr
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer (NAVI10).  Also check for
 * ring buffer overflow and deal with it.
 * Returns the value of the wptr.
 */
static u32 navi10_ih_get_wptr(struct amdgpu_device *adev,
			      struct amdgpu_ih_ring *ih)
{
	u32 wptr, tmp;
	struct amdgpu_ih_regs *ih_regs;

	wptr = le32_to_cpu(*ih->wptr_cpu);
	ih_regs = &ih->ih_regs;

	if (!REG_GET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW))
		goto out;

	wptr = RREG32_NO_KIQ(ih_regs->ih_rb_wptr);
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

	tmp = RREG32_NO_KIQ(ih_regs->ih_rb_cntl);
	tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 1);
	WREG32_NO_KIQ(ih_regs->ih_rb_cntl, tmp);
out:
	return (wptr & ih->ptr_mask);
}

/**
 * navi10_ih_irq_rearm - rearm IRQ if lost
 *
 * @adev: amdgpu_device pointer
 * @ih: IH ring to match
 *
 */
static void navi10_ih_irq_rearm(struct amdgpu_device *adev,
			       struct amdgpu_ih_ring *ih)
{
	uint32_t v = 0;
	uint32_t i = 0;
	struct amdgpu_ih_regs *ih_regs;

	ih_regs = &ih->ih_regs;

	/* Rearm IRQ / re-write doorbell if doorbell write is lost */
	for (i = 0; i < MAX_REARM_RETRY; i++) {
		v = RREG32_NO_KIQ(ih_regs->ih_rb_rptr);
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
 * @ih: IH ring buffer to set rptr
 * Set the IH ring buffer rptr.
 */
static void navi10_ih_set_rptr(struct amdgpu_device *adev,
			       struct amdgpu_ih_ring *ih)
{
	struct amdgpu_ih_regs *ih_regs;

	if (ih->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		*ih->rptr_cpu = ih->rptr;
		WDOORBELL32(ih->doorbell_index, ih->rptr);

		if (amdgpu_sriov_vf(adev))
			navi10_ih_irq_rearm(adev, ih);
	} else {
		ih_regs = &ih->ih_regs;
		WREG32(ih_regs->ih_rb_rptr, ih->rptr);
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
	if ((adev->flags & AMD_IS_APU) ||
	    (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP))
		use_bus_addr = false;
	else
		use_bus_addr = true;
	r = amdgpu_ih_ring_init(adev, &adev->irq.ih, 256 * 1024, use_bus_addr);
	if (r)
		return r;

	adev->irq.ih.use_doorbell = true;
	adev->irq.ih.doorbell_index = adev->doorbell_index.ih << 1;

	adev->irq.ih1.ring_size = 0;
	adev->irq.ih2.ring_size = 0;

	/* initialize ih control registers offset */
	navi10_ih_init_register_offset(adev);

	r = amdgpu_ih_ring_init(adev, &adev->irq.ih_soft, PAGE_SIZE, true);
	if (r)
		return r;

	r = amdgpu_irq_init(adev);

	return r;
}

static int navi10_ih_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_fini_sw(adev);

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
	.decode_iv = amdgpu_ih_decode_iv_helper,
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

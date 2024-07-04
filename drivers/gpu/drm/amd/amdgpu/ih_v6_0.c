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

#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_ih.h"

#include "oss/osssys_6_0_0_offset.h"
#include "oss/osssys_6_0_0_sh_mask.h"

#include "soc15_common.h"
#include "ih_v6_0.h"

#define MAX_REARM_RETRY 10

static void ih_v6_0_set_interrupt_funcs(struct amdgpu_device *adev);

/**
 * ih_v6_0_init_register_offset - Initialize register offset for ih rings
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize register offset ih rings (IH_V6_0).
 */
static void ih_v6_0_init_register_offset(struct amdgpu_device *adev)
{
	struct amdgpu_ih_regs *ih_regs;

	/* ih ring 2 is removed
	 * ih ring and ih ring 1 are available */
	if (adev->irq.ih.ring_size) {
		ih_regs = &adev->irq.ih.ih_regs;
		ih_regs->ih_rb_base = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_BASE);
		ih_regs->ih_rb_base_hi = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_BASE_HI);
		ih_regs->ih_rb_cntl = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_CNTL);
		ih_regs->ih_rb_wptr = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_WPTR);
		ih_regs->ih_rb_rptr = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_RPTR);
		ih_regs->ih_doorbell_rptr = SOC15_REG_OFFSET(OSSSYS, 0, regIH_DOORBELL_RPTR);
		ih_regs->ih_rb_wptr_addr_lo = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_WPTR_ADDR_LO);
		ih_regs->ih_rb_wptr_addr_hi = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_WPTR_ADDR_HI);
		ih_regs->psp_reg_id = PSP_REG_IH_RB_CNTL;
	}

	if (adev->irq.ih1.ring_size) {
		ih_regs = &adev->irq.ih1.ih_regs;
		ih_regs->ih_rb_base = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_BASE_RING1);
		ih_regs->ih_rb_base_hi = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_BASE_HI_RING1);
		ih_regs->ih_rb_cntl = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_CNTL_RING1);
		ih_regs->ih_rb_wptr = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_WPTR_RING1);
		ih_regs->ih_rb_rptr = SOC15_REG_OFFSET(OSSSYS, 0, regIH_RB_RPTR_RING1);
		ih_regs->ih_doorbell_rptr = SOC15_REG_OFFSET(OSSSYS, 0, regIH_DOORBELL_RPTR_RING1);
		ih_regs->psp_reg_id = PSP_REG_IH_RB_CNTL_RING1;
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

	ih_cntl = RREG32_SOC15(OSSSYS, 0, regIH_CNTL2);
	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, regIH_RB_CNTL_RING1);

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
		WREG32_SOC15(OSSSYS, 0, regIH_RB_CNTL_RING1, ih_rb_cntl);
	}

	WREG32_SOC15(OSSSYS, 0, regIH_CNTL2, ih_cntl);
}

/**
 * ih_v6_0_toggle_ring_interrupts - toggle the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointer
 * @enable: true - enable the interrupts, false - disable the interrupts
 *
 * Toggle the interrupt ring buffer (IH_V6_0)
 */
static int ih_v6_0_toggle_ring_interrupts(struct amdgpu_device *adev,
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
 * ih_v6_0_toggle_interrupts - Toggle all the available interrupt ring buffers
 *
 * @adev: amdgpu_device pointer
 * @enable: enable or disable interrupt ring buffers
 *
 * Toggle all the available interrupt ring buffers (IH_V6_0).
 */
static int ih_v6_0_toggle_interrupts(struct amdgpu_device *adev, bool enable)
{
	struct amdgpu_ih_ring *ih[] = {&adev->irq.ih, &adev->irq.ih1};
	int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(ih); i++) {
		if (ih[i]->ring_size) {
			r = ih_v6_0_toggle_ring_interrupts(adev, ih[i], enable);
			if (r)
				return r;
		}
	}

	return 0;
}

static uint32_t ih_v6_0_rb_cntl(struct amdgpu_ih_ring *ih, uint32_t ih_rb_cntl)
{
	int rb_bufsz = order_base_2(ih->ring_size / 4);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL,
				   MC_SPACE, ih->use_bus_addr ? 2 : 4);
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

static uint32_t ih_v6_0_doorbell_rptr(struct amdgpu_ih_ring *ih)
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
 * ih_v6_0_enable_ring - enable an ih ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointer
 *
 * Enable an ih ring buffer (IH_V6_0)
 */
static int ih_v6_0_enable_ring(struct amdgpu_device *adev,
				      struct amdgpu_ih_ring *ih)
{
	struct amdgpu_ih_regs *ih_regs;
	uint32_t tmp;

	ih_regs = &ih->ih_regs;

	/* Ring Buffer base. [39:8] of 40-bit address of the beginning of the ring buffer*/
	WREG32(ih_regs->ih_rb_base, ih->gpu_addr >> 8);
	WREG32(ih_regs->ih_rb_base_hi, (ih->gpu_addr >> 40) & 0xff);

	tmp = RREG32(ih_regs->ih_rb_cntl);
	tmp = ih_v6_0_rb_cntl(ih, tmp);
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

	WREG32(ih_regs->ih_doorbell_rptr, ih_v6_0_doorbell_rptr(ih));

	return 0;
}

/**
 * ih_v6_0_irq_init - init and enable the interrupt ring
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate a ring buffer for the interrupt controller,
 * enable the RLC, disable interrupts, enable the IH
 * ring buffer and enable it.
 * Called at device load and reume.
 * Returns 0 for success, errors for failure.
 */
static int ih_v6_0_irq_init(struct amdgpu_device *adev)
{
	struct amdgpu_ih_ring *ih[] = {&adev->irq.ih, &adev->irq.ih1};
	u32 ih_chicken;
	u32 tmp;
	int ret;
	int i;

	/* disable irqs */
	ret = ih_v6_0_toggle_interrupts(adev, false);
	if (ret)
		return ret;

	adev->nbio.funcs->ih_control(adev);

	if (unlikely((adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) ||
		     (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO))) {
		if (ih[0]->use_bus_addr) {
			ih_chicken = RREG32_SOC15(OSSSYS, 0, regIH_CHICKEN);
			ih_chicken = REG_SET_FIELD(ih_chicken,
					IH_CHICKEN, MC_SPACE_GPA_ENABLE, 1);
			WREG32_SOC15(OSSSYS, 0, regIH_CHICKEN, ih_chicken);
		}
	}

	for (i = 0; i < ARRAY_SIZE(ih); i++) {
		if (ih[i]->ring_size) {
			ret = ih_v6_0_enable_ring(adev, ih[i]);
			if (ret)
				return ret;
		}
	}

	/* update doorbell range for ih ring 0 */
	adev->nbio.funcs->ih_doorbell_range(adev, ih[0]->use_doorbell,
					    ih[0]->doorbell_index);

	tmp = RREG32_SOC15(OSSSYS, 0, regIH_STORM_CLIENT_LIST_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_STORM_CLIENT_LIST_CNTL,
			    CLIENT18_IS_STORM_CLIENT, 1);
	WREG32_SOC15(OSSSYS, 0, regIH_STORM_CLIENT_LIST_CNTL, tmp);

	tmp = RREG32_SOC15(OSSSYS, 0, regIH_INT_FLOOD_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_INT_FLOOD_CNTL, FLOOD_CNTL_ENABLE, 1);
	WREG32_SOC15(OSSSYS, 0, regIH_INT_FLOOD_CNTL, tmp);

	/* GC/MMHUB UTCL2 page fault interrupts are configured as
	 * MSI storm capable interrupts by deafult. The delay is
	 * used to avoid ISR being called too frequently
	 * when page fault happens on several continuous page
	 * and thus avoid MSI storm */
	tmp = RREG32_SOC15(OSSSYS, 0, regIH_MSI_STORM_CTRL);
	tmp = REG_SET_FIELD(tmp, IH_MSI_STORM_CTRL,
			    DELAY, 3);
	WREG32_SOC15(OSSSYS, 0, regIH_MSI_STORM_CTRL, tmp);

	/* Redirect the interrupts to IH RB1 for dGPU */
	if (adev->irq.ih1.ring_size) {
		tmp = RREG32_SOC15(OSSSYS, 0, regIH_RING1_CLIENT_CFG_INDEX);
		tmp = REG_SET_FIELD(tmp, IH_RING1_CLIENT_CFG_INDEX, INDEX, 0);
		WREG32_SOC15(OSSSYS, 0, regIH_RING1_CLIENT_CFG_INDEX, tmp);

		tmp = RREG32_SOC15(OSSSYS, 0, regIH_RING1_CLIENT_CFG_DATA);
		tmp = REG_SET_FIELD(tmp, IH_RING1_CLIENT_CFG_DATA, CLIENT_ID, 0xa);
		tmp = REG_SET_FIELD(tmp, IH_RING1_CLIENT_CFG_DATA, SOURCE_ID, 0x0);
		tmp = REG_SET_FIELD(tmp, IH_RING1_CLIENT_CFG_DATA,
				    SOURCE_ID_MATCH_ENABLE, 0x1);

		WREG32_SOC15(OSSSYS, 0, regIH_RING1_CLIENT_CFG_DATA, tmp);
	}

	pci_set_master(adev->pdev);

	/* enable interrupts */
	ret = ih_v6_0_toggle_interrupts(adev, true);
	if (ret)
		return ret;
	/* enable wptr force update for self int */
	force_update_wptr_for_self_int(adev, 0, 8, true);

	if (adev->irq.ih_soft.ring_size)
		adev->irq.ih_soft.enabled = true;

	return 0;
}

/**
 * ih_v6_0_irq_disable - disable interrupts
 *
 * @adev: amdgpu_device pointer
 *
 * Disable interrupts on the hw.
 */
static void ih_v6_0_irq_disable(struct amdgpu_device *adev)
{
	force_update_wptr_for_self_int(adev, 0, 8, false);
	ih_v6_0_toggle_interrupts(adev, false);

	/* Wait and acknowledge irq */
	mdelay(1);
}

/**
 * ih_v6_0_get_wptr - get the IH ring buffer wptr
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointer
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer.  Also check for
 * ring buffer overflow and deal with it.
 * Returns the value of the wptr.
 */
static u32 ih_v6_0_get_wptr(struct amdgpu_device *adev,
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

	/* Unset the CLEAR_OVERFLOW bit immediately so new overflows
	 * can be detected.
	 */
	tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 0);
	WREG32_NO_KIQ(ih_regs->ih_rb_cntl, tmp);
out:
	return (wptr & ih->ptr_mask);
}

/**
 * ih_v6_0_irq_rearm - rearm IRQ if lost
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointer
 *
 */
static void ih_v6_0_irq_rearm(struct amdgpu_device *adev,
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
 * ih_v6_0_set_rptr - set the IH ring buffer rptr
 *
 * @adev: amdgpu_device pointer
 * @ih: amdgpu_ih_ring pointer
 *
 * Set the IH ring buffer rptr.
 */
static void ih_v6_0_set_rptr(struct amdgpu_device *adev,
			       struct amdgpu_ih_ring *ih)
{
	struct amdgpu_ih_regs *ih_regs;

	if (ih->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		*ih->rptr_cpu = ih->rptr;
		WDOORBELL32(ih->doorbell_index, ih->rptr);

		if (amdgpu_sriov_vf(adev))
			ih_v6_0_irq_rearm(adev, ih);
	} else {
		ih_regs = &ih->ih_regs;
		WREG32(ih_regs->ih_rb_rptr, ih->rptr);
	}
}

/**
 * ih_v6_0_self_irq - dispatch work for ring 1
 *
 * @adev: amdgpu_device pointer
 * @source: irq source
 * @entry: IV with WPTR update
 *
 * Update the WPTR from the IV and schedule work to handle the entries.
 */
static int ih_v6_0_self_irq(struct amdgpu_device *adev,
			      struct amdgpu_irq_src *source,
			      struct amdgpu_iv_entry *entry)
{
	uint32_t wptr = cpu_to_le32(entry->src_data[0]);

	switch (entry->ring_id) {
	case 1:
		*adev->irq.ih1.wptr_cpu = wptr;
		schedule_work(&adev->irq.ih1_work);
		break;
	default:
		break;
	}
	return 0;
}

static const struct amdgpu_irq_src_funcs ih_v6_0_self_irq_funcs = {
	.process = ih_v6_0_self_irq,
};

static void ih_v6_0_set_self_irq_funcs(struct amdgpu_device *adev)
{
	adev->irq.self_irq.num_types = 0;
	adev->irq.self_irq.funcs = &ih_v6_0_self_irq_funcs;
}

static int ih_v6_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ih_v6_0_set_interrupt_funcs(adev);
	ih_v6_0_set_self_irq_funcs(adev);
	return 0;
}

static int ih_v6_0_sw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool use_bus_addr;

	r = amdgpu_irq_add_id(adev, SOC21_IH_CLIENTID_IH, 0,
			      &adev->irq.self_irq);

	if (r)
		return r;

	/* use gpu virtual address for ih ring
	 * until ih_checken is programmed to allow
	 * use bus address for ih ring by psp bl */
	use_bus_addr =
		(adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) ? false : true;
	r = amdgpu_ih_ring_init(adev, &adev->irq.ih, IH_RING_SIZE, use_bus_addr);
	if (r)
		return r;

	adev->irq.ih.use_doorbell = true;
	adev->irq.ih.doorbell_index = adev->doorbell_index.ih << 1;

	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_ih_ring_init(adev, &adev->irq.ih1, IH_RING_SIZE,
					use_bus_addr);
		if (r)
			return r;

		adev->irq.ih1.use_doorbell = true;
		adev->irq.ih1.doorbell_index = (adev->doorbell_index.ih + 1) << 1;
	}

	/* initialize ih control register offset */
	ih_v6_0_init_register_offset(adev);

	r = amdgpu_ih_ring_init(adev, &adev->irq.ih_soft, IH_SW_RING_SIZE, true);
	if (r)
		return r;

	r = amdgpu_irq_init(adev);

	return r;
}

static int ih_v6_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_fini_sw(adev);

	return 0;
}

static int ih_v6_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = ih_v6_0_irq_init(adev);
	if (r)
		return r;

	return 0;
}

static int ih_v6_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ih_v6_0_irq_disable(adev);

	return 0;
}

static int ih_v6_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return ih_v6_0_hw_fini(adev);
}

static int ih_v6_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return ih_v6_0_hw_init(adev);
}

static bool ih_v6_0_is_idle(void *handle)
{
	/* todo */
	return true;
}

static int ih_v6_0_wait_for_idle(void *handle)
{
	/* todo */
	return -ETIMEDOUT;
}

static int ih_v6_0_soft_reset(void *handle)
{
	/* todo */
	return 0;
}

static void ih_v6_0_update_clockgating_state(struct amdgpu_device *adev,
					       bool enable)
{
	uint32_t data, def, field_val;

	if (adev->cg_flags & AMD_CG_SUPPORT_IH_CG) {
		def = data = RREG32_SOC15(OSSSYS, 0, regIH_CLK_CTRL);
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
			WREG32_SOC15(OSSSYS, 0, regIH_CLK_CTRL, data);
	}
}

static int ih_v6_0_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ih_v6_0_update_clockgating_state(adev,
				state == AMD_CG_STATE_GATE);
	return 0;
}

static void ih_v6_0_update_ih_mem_power_gating(struct amdgpu_device *adev,
					       bool enable)
{
	uint32_t ih_mem_pwr_cntl;

	/* Disable ih sram power cntl before switch powergating mode */
	ih_mem_pwr_cntl = RREG32_SOC15(OSSSYS, 0, regIH_MEM_POWER_CTRL);
	ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
					IH_BUFFER_MEM_POWER_CTRL_EN, 0);
	WREG32_SOC15(OSSSYS, 0, regIH_MEM_POWER_CTRL, ih_mem_pwr_cntl);

	/* It is recommended to set mem powergating mode to DS mode */
	if (enable) {
		/* mem power mode */
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_LS_EN, 0);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_DS_EN, 1);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_SD_EN, 0);
		/* cam mem power mode */
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_RETRY_INT_CAM_MEM_POWER_LS_EN, 0);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_RETRY_INT_CAM_MEM_POWER_DS_EN, 1);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_RETRY_INT_CAM_MEM_POWER_SD_EN, 0);
		/* re-enable power cntl */
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_CTRL_EN, 1);
	} else {
		/* mem power mode */
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_LS_EN, 0);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_DS_EN, 0);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_SD_EN, 0);
		/* cam mem power mode */
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_RETRY_INT_CAM_MEM_POWER_LS_EN, 0);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_RETRY_INT_CAM_MEM_POWER_DS_EN, 0);
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_RETRY_INT_CAM_MEM_POWER_SD_EN, 0);
		/* re-enable power cntl*/
		ih_mem_pwr_cntl = REG_SET_FIELD(ih_mem_pwr_cntl, IH_MEM_POWER_CTRL,
						IH_BUFFER_MEM_POWER_CTRL_EN, 1);
	}

	WREG32_SOC15(OSSSYS, 0, regIH_MEM_POWER_CTRL, ih_mem_pwr_cntl);
}

static int ih_v6_0_set_powergating_state(void *handle,
					 enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_PG_STATE_GATE);

	if (adev->pg_flags & AMD_PG_SUPPORT_IH_SRAM_PG)
		ih_v6_0_update_ih_mem_power_gating(adev, enable);

	return 0;
}

static void ih_v6_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!RREG32_SOC15(OSSSYS, 0, regIH_CLK_CTRL))
		*flags |= AMD_CG_SUPPORT_IH_CG;
}

static const struct amd_ip_funcs ih_v6_0_ip_funcs = {
	.name = "ih_v6_0",
	.early_init = ih_v6_0_early_init,
	.late_init = NULL,
	.sw_init = ih_v6_0_sw_init,
	.sw_fini = ih_v6_0_sw_fini,
	.hw_init = ih_v6_0_hw_init,
	.hw_fini = ih_v6_0_hw_fini,
	.suspend = ih_v6_0_suspend,
	.resume = ih_v6_0_resume,
	.is_idle = ih_v6_0_is_idle,
	.wait_for_idle = ih_v6_0_wait_for_idle,
	.soft_reset = ih_v6_0_soft_reset,
	.set_clockgating_state = ih_v6_0_set_clockgating_state,
	.set_powergating_state = ih_v6_0_set_powergating_state,
	.get_clockgating_state = ih_v6_0_get_clockgating_state,
	.dump_ip_state = NULL,
	.print_ip_state = NULL,
};

static const struct amdgpu_ih_funcs ih_v6_0_funcs = {
	.get_wptr = ih_v6_0_get_wptr,
	.decode_iv = amdgpu_ih_decode_iv_helper,
	.decode_iv_ts = amdgpu_ih_decode_iv_ts_helper,
	.set_rptr = ih_v6_0_set_rptr
};

static void ih_v6_0_set_interrupt_funcs(struct amdgpu_device *adev)
{
	adev->irq.ih_funcs = &ih_v6_0_funcs;
}

const struct amdgpu_ip_block_version ih_v6_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_IH,
	.major = 6,
	.minor = 0,
	.rev = 0,
	.funcs = &ih_v6_0_ip_funcs,
};

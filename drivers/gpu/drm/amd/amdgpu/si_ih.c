/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "sid.h"
#include "si_ih.h"
#include "oss/oss_1_0_d.h"
#include "oss/oss_1_0_sh_mask.h"

static void si_ih_set_interrupt_funcs(struct amdgpu_device *adev);

static void si_ih_enable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_cntl = RREG32(IH_CNTL);
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);

	ih_cntl |= ENABLE_INTR;
	ih_rb_cntl |= IH_RB_ENABLE;
	WREG32(IH_CNTL, ih_cntl);
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	adev->irq.ih.enabled = true;
}

static void si_ih_disable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);
	u32 ih_cntl = RREG32(IH_CNTL);

	ih_rb_cntl &= ~IH_RB_ENABLE;
	ih_cntl &= ~ENABLE_INTR;
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	WREG32(IH_CNTL, ih_cntl);
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);
	adev->irq.ih.enabled = false;
	adev->irq.ih.rptr = 0;
}

static int si_ih_irq_init(struct amdgpu_device *adev)
{
	struct amdgpu_ih_ring *ih = &adev->irq.ih;
	int rb_bufsz;
	u32 interrupt_cntl, ih_cntl, ih_rb_cntl;

	si_ih_disable_interrupts(adev);
	/* set dummy read address to dummy page address */
	WREG32(INTERRUPT_CNTL2, adev->dummy_page_addr >> 8);
	interrupt_cntl = RREG32(INTERRUPT_CNTL);
	interrupt_cntl &= ~IH_DUMMY_RD_OVERRIDE;
	interrupt_cntl &= ~IH_REQ_NONSNOOP_EN;
	WREG32(INTERRUPT_CNTL, interrupt_cntl);

	WREG32(IH_RB_BASE, adev->irq.ih.gpu_addr >> 8);
	rb_bufsz = order_base_2(adev->irq.ih.ring_size / 4);

	ih_rb_cntl = IH_WPTR_OVERFLOW_ENABLE |
		     IH_WPTR_OVERFLOW_CLEAR |
		     (rb_bufsz << 1) |
		     IH_WPTR_WRITEBACK_ENABLE;

	WREG32(IH_RB_WPTR_ADDR_LO, lower_32_bits(ih->wptr_addr));
	WREG32(IH_RB_WPTR_ADDR_HI, upper_32_bits(ih->wptr_addr) & 0xFF);
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);

	ih_cntl = MC_WRREQ_CREDIT(0x10) | MC_WR_CLEAN_CNT(0x10) | MC_VMID(0);
	if (adev->irq.msi_enabled)
		ih_cntl |= RPTR_REARM;
	WREG32(IH_CNTL, ih_cntl);

	pci_set_master(adev->pdev);
	si_ih_enable_interrupts(adev);

	return 0;
}

static void si_ih_irq_disable(struct amdgpu_device *adev)
{
	si_ih_disable_interrupts(adev);
	mdelay(1);
}

static u32 si_ih_get_wptr(struct amdgpu_device *adev,
			  struct amdgpu_ih_ring *ih)
{
	u32 wptr, tmp;

	wptr = le32_to_cpu(*ih->wptr_cpu);

	if (wptr & IH_RB_WPTR__RB_OVERFLOW_MASK) {
		wptr &= ~IH_RB_WPTR__RB_OVERFLOW_MASK;
		dev_warn(adev->dev, "IH ring buffer overflow (0x%08X, 0x%08X, 0x%08X)\n",
			wptr, ih->rptr, (wptr + 16) & ih->ptr_mask);
		ih->rptr = (wptr + 16) & ih->ptr_mask;
		tmp = RREG32(IH_RB_CNTL);
		tmp |= IH_RB_CNTL__WPTR_OVERFLOW_CLEAR_MASK;
		WREG32(IH_RB_CNTL, tmp);

		/* Unset the CLEAR_OVERFLOW bit immediately so new overflows
		 * can be detected.
		 */
		tmp &= ~IH_RB_CNTL__WPTR_OVERFLOW_CLEAR_MASK;
		WREG32(IH_RB_CNTL, tmp);
	}
	return (wptr & ih->ptr_mask);
}

static void si_ih_decode_iv(struct amdgpu_device *adev,
			    struct amdgpu_ih_ring *ih,
			    struct amdgpu_iv_entry *entry)
{
	u32 ring_index = ih->rptr >> 2;
	uint32_t dw[4];

	dw[0] = le32_to_cpu(ih->ring[ring_index + 0]);
	dw[1] = le32_to_cpu(ih->ring[ring_index + 1]);
	dw[2] = le32_to_cpu(ih->ring[ring_index + 2]);
	dw[3] = le32_to_cpu(ih->ring[ring_index + 3]);

	entry->client_id = AMDGPU_IRQ_CLIENTID_LEGACY;
	entry->src_id = dw[0] & 0xff;
	entry->src_data[0] = dw[1] & 0xfffffff;
	entry->ring_id = dw[2] & 0xff;
	entry->vmid = (dw[2] >> 8) & 0xff;

	ih->rptr += 16;
}

static void si_ih_set_rptr(struct amdgpu_device *adev,
			   struct amdgpu_ih_ring *ih)
{
	WREG32(IH_RB_RPTR, ih->rptr);
}

static int si_ih_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	si_ih_set_interrupt_funcs(adev);

	return 0;
}

static int si_ih_sw_init(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	r = amdgpu_ih_ring_init(adev, &adev->irq.ih, 64 * 1024, false);
	if (r)
		return r;

	return amdgpu_irq_init(adev);
}

static int si_ih_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	amdgpu_irq_fini_sw(adev);

	return 0;
}

static int si_ih_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	return si_ih_irq_init(adev);
}

static int si_ih_hw_fini(struct amdgpu_ip_block *ip_block)
{
	si_ih_irq_disable(ip_block->adev);

	return 0;
}

static int si_ih_suspend(struct amdgpu_ip_block *ip_block)
{
	return si_ih_hw_fini(ip_block);
}

static int si_ih_resume(struct amdgpu_ip_block *ip_block)
{
	return si_ih_hw_init(ip_block);
}

static bool si_ih_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(SRBM_STATUS);

	if (tmp & SRBM_STATUS__IH_BUSY_MASK)
		return false;

	return true;
}

static int si_ih_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i;
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (si_ih_is_idle(adev))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int si_ih_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	u32 srbm_soft_reset = 0;
	u32 tmp = RREG32(SRBM_STATUS);

	if (tmp & SRBM_STATUS__IH_BUSY_MASK)
		srbm_soft_reset |= SRBM_SOFT_RESET__SOFT_RESET_IH_MASK;

	if (srbm_soft_reset) {
		tmp = RREG32(SRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);

		udelay(50);
	}

	return 0;
}

static int si_ih_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int si_ih_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs si_ih_ip_funcs = {
	.name = "si_ih",
	.early_init = si_ih_early_init,
	.sw_init = si_ih_sw_init,
	.sw_fini = si_ih_sw_fini,
	.hw_init = si_ih_hw_init,
	.hw_fini = si_ih_hw_fini,
	.suspend = si_ih_suspend,
	.resume = si_ih_resume,
	.is_idle = si_ih_is_idle,
	.wait_for_idle = si_ih_wait_for_idle,
	.soft_reset = si_ih_soft_reset,
	.set_clockgating_state = si_ih_set_clockgating_state,
	.set_powergating_state = si_ih_set_powergating_state,
};

static const struct amdgpu_ih_funcs si_ih_funcs = {
	.get_wptr = si_ih_get_wptr,
	.decode_iv = si_ih_decode_iv,
	.set_rptr = si_ih_set_rptr
};

static void si_ih_set_interrupt_funcs(struct amdgpu_device *adev)
{
	adev->irq.ih_funcs = &si_ih_funcs;
}

const struct amdgpu_ip_block_version si_ih_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_IH,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &si_ih_ip_funcs,
};

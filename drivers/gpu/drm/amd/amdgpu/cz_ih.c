/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#include "drmP.h"
#include "amdgpu.h"
#include "amdgpu_ih.h"
#include "vid.h"

#include "oss/oss_3_0_1_d.h"
#include "oss/oss_3_0_1_sh_mask.h"

#include "bif/bif_5_1_d.h"
#include "bif/bif_5_1_sh_mask.h"

/*
 * Interrupts
 * Starting with r6xx, interrupts are handled via a ring buffer.
 * Ring buffers are areas of GPU accessible memory that the GPU
 * writes interrupt vectors into and the host reads vectors out of.
 * There is a rptr (read pointer) that determines where the
 * host is currently reading, and a wptr (write pointer)
 * which determines where the GPU has written.  When the
 * pointers are equal, the ring is idle.  When the GPU
 * writes vectors to the ring buffer, it increments the
 * wptr.  When there is an interrupt, the host then starts
 * fetching commands and processing them until the pointers are
 * equal again at which point it updates the rptr.
 */

static void cz_ih_set_interrupt_funcs(struct amdgpu_device *adev);

/**
 * cz_ih_enable_interrupts - Enable the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 *
 * Enable the interrupt ring buffer (VI).
 */
static void cz_ih_enable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_cntl = RREG32(mmIH_CNTL);
	u32 ih_rb_cntl = RREG32(mmIH_RB_CNTL);

	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL, ENABLE_INTR, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_ENABLE, 1);
	WREG32(mmIH_CNTL, ih_cntl);
	WREG32(mmIH_RB_CNTL, ih_rb_cntl);
	adev->irq.ih.enabled = true;
}

/**
 * cz_ih_disable_interrupts - Disable the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 *
 * Disable the interrupt ring buffer (VI).
 */
static void cz_ih_disable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_rb_cntl = RREG32(mmIH_RB_CNTL);
	u32 ih_cntl = RREG32(mmIH_CNTL);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_ENABLE, 0);
	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL, ENABLE_INTR, 0);
	WREG32(mmIH_RB_CNTL, ih_rb_cntl);
	WREG32(mmIH_CNTL, ih_cntl);
	/* set rptr, wptr to 0 */
	WREG32(mmIH_RB_RPTR, 0);
	WREG32(mmIH_RB_WPTR, 0);
	adev->irq.ih.enabled = false;
	adev->irq.ih.rptr = 0;
}

/**
 * cz_ih_irq_init - init and enable the interrupt ring
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate a ring buffer for the interrupt controller,
 * enable the RLC, disable interrupts, enable the IH
 * ring buffer and enable it (VI).
 * Called at device load and reume.
 * Returns 0 for success, errors for failure.
 */
static int cz_ih_irq_init(struct amdgpu_device *adev)
{
	int ret = 0;
	int rb_bufsz;
	u32 interrupt_cntl, ih_cntl, ih_rb_cntl;
	u64 wptr_off;

	/* disable irqs */
	cz_ih_disable_interrupts(adev);

	/* setup interrupt control */
	WREG32(mmINTERRUPT_CNTL2, adev->dummy_page.addr >> 8);
	interrupt_cntl = RREG32(mmINTERRUPT_CNTL);
	/* INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=0 - dummy read disabled with msi, enabled without msi
	 * INTERRUPT_CNTL__IH_DUMMY_RD_OVERRIDE_MASK=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_DUMMY_RD_OVERRIDE, 0);
	/* INTERRUPT_CNTL__IH_REQ_NONSNOOP_EN_MASK=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl = REG_SET_FIELD(interrupt_cntl, INTERRUPT_CNTL, IH_REQ_NONSNOOP_EN, 0);
	WREG32(mmINTERRUPT_CNTL, interrupt_cntl);

	/* Ring Buffer base. [39:8] of 40-bit address of the beginning of the ring buffer*/
	WREG32(mmIH_RB_BASE, adev->irq.ih.gpu_addr >> 8);

	rb_bufsz = order_base_2(adev->irq.ih.ring_size / 4);
	ih_rb_cntl = REG_SET_FIELD(0, IH_RB_CNTL, WPTR_OVERFLOW_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_SIZE, rb_bufsz);

	/* Ring Buffer write pointer writeback. If enabled, IH_RB_WPTR register value is written to memory */
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, WPTR_WRITEBACK_ENABLE, 1);

	/* set the writeback address whether it's enabled or not */
	wptr_off = adev->wb.gpu_addr + (adev->irq.ih.wptr_offs * 4);
	WREG32(mmIH_RB_WPTR_ADDR_LO, lower_32_bits(wptr_off));
	WREG32(mmIH_RB_WPTR_ADDR_HI, upper_32_bits(wptr_off) & 0xFF);

	WREG32(mmIH_RB_CNTL, ih_rb_cntl);

	/* set rptr, wptr to 0 */
	WREG32(mmIH_RB_RPTR, 0);
	WREG32(mmIH_RB_WPTR, 0);

	/* Default settings for IH_CNTL (disabled at first) */
	ih_cntl = RREG32(mmIH_CNTL);
	ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL, MC_VMID, 0);

	if (adev->irq.msi_enabled)
		ih_cntl = REG_SET_FIELD(ih_cntl, IH_CNTL, RPTR_REARM, 1);
	WREG32(mmIH_CNTL, ih_cntl);

	pci_set_master(adev->pdev);

	/* enable interrupts */
	cz_ih_enable_interrupts(adev);

	return ret;
}

/**
 * cz_ih_irq_disable - disable interrupts
 *
 * @adev: amdgpu_device pointer
 *
 * Disable interrupts on the hw (VI).
 */
static void cz_ih_irq_disable(struct amdgpu_device *adev)
{
	cz_ih_disable_interrupts(adev);

	/* Wait and acknowledge irq */
	mdelay(1);
}

/**
 * cz_ih_get_wptr - get the IH ring buffer wptr
 *
 * @adev: amdgpu_device pointer
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer (VI).  Also check for
 * ring buffer overflow and deal with it.
 * Used by cz_irq_process(VI).
 * Returns the value of the wptr.
 */
static u32 cz_ih_get_wptr(struct amdgpu_device *adev)
{
	u32 wptr, tmp;

	wptr = le32_to_cpu(adev->wb.wb[adev->irq.ih.wptr_offs]);

	if (REG_GET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW)) {
		wptr = REG_SET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW, 0);
		/* When a ring buffer overflow happen start parsing interrupt
		 * from the last not overwritten vector (wptr + 16). Hopefully
		 * this should allow us to catchup.
		 */
		dev_warn(adev->dev, "IH ring buffer overflow (0x%08X, 0x%08X, 0x%08X)\n",
			wptr, adev->irq.ih.rptr, (wptr + 16) & adev->irq.ih.ptr_mask);
		adev->irq.ih.rptr = (wptr + 16) & adev->irq.ih.ptr_mask;
		tmp = RREG32(mmIH_RB_CNTL);
		tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 1);
		WREG32(mmIH_RB_CNTL, tmp);
	}
	return (wptr & adev->irq.ih.ptr_mask);
}

/**
 * cz_ih_decode_iv - decode an interrupt vector
 *
 * @adev: amdgpu_device pointer
 *
 * Decodes the interrupt vector at the current rptr
 * position and also advance the position.
 */
static void cz_ih_decode_iv(struct amdgpu_device *adev,
				 struct amdgpu_iv_entry *entry)
{
	/* wptr/rptr are in bytes! */
	u32 ring_index = adev->irq.ih.rptr >> 2;
	uint32_t dw[4];
	
	dw[0] = le32_to_cpu(adev->irq.ih.ring[ring_index + 0]);
	dw[1] = le32_to_cpu(adev->irq.ih.ring[ring_index + 1]);
	dw[2] = le32_to_cpu(adev->irq.ih.ring[ring_index + 2]);
	dw[3] = le32_to_cpu(adev->irq.ih.ring[ring_index + 3]);

	entry->src_id = dw[0] & 0xff;
	entry->src_data = dw[1] & 0xfffffff;
	entry->ring_id = dw[2] & 0xff;
	entry->vm_id = (dw[2] >> 8) & 0xff;
	entry->pas_id = (dw[2] >> 16) & 0xffff;

	/* wptr/rptr are in bytes! */
	adev->irq.ih.rptr += 16;
}

/**
 * cz_ih_set_rptr - set the IH ring buffer rptr
 *
 * @adev: amdgpu_device pointer
 *
 * Set the IH ring buffer rptr.
 */
static void cz_ih_set_rptr(struct amdgpu_device *adev)
{
	WREG32(mmIH_RB_RPTR, adev->irq.ih.rptr);
}

static int cz_ih_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	cz_ih_set_interrupt_funcs(adev);
	return 0;
}

static int cz_ih_sw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_ih_ring_init(adev, 64 * 1024, false);
	if (r)
		return r;

	r = amdgpu_irq_init(adev);

	return r;
}

static int cz_ih_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_fini(adev);
	amdgpu_ih_ring_fini(adev);

	return 0;
}

static int cz_ih_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = cz_ih_irq_init(adev);
	if (r)
		return r;

	return 0;
}

static int cz_ih_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	cz_ih_irq_disable(adev);

	return 0;
}

static int cz_ih_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return cz_ih_hw_fini(adev);
}

static int cz_ih_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return cz_ih_hw_init(adev);
}

static bool cz_ih_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (REG_GET_FIELD(tmp, SRBM_STATUS, IH_BUSY))
		return false;

	return true;
}

static int cz_ih_wait_for_idle(void *handle)
{
	unsigned i;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(mmSRBM_STATUS);
		if (!REG_GET_FIELD(tmp, SRBM_STATUS, IH_BUSY))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static void cz_ih_print_status(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_info(adev->dev, "CZ IH registers\n");
	dev_info(adev->dev, "  SRBM_STATUS=0x%08X\n",
		RREG32(mmSRBM_STATUS));
	dev_info(adev->dev, "  SRBM_STATUS2=0x%08X\n",
		RREG32(mmSRBM_STATUS2));
	dev_info(adev->dev, "  INTERRUPT_CNTL=0x%08X\n",
		 RREG32(mmINTERRUPT_CNTL));
	dev_info(adev->dev, "  INTERRUPT_CNTL2=0x%08X\n",
		 RREG32(mmINTERRUPT_CNTL2));
	dev_info(adev->dev, "  IH_CNTL=0x%08X\n",
		 RREG32(mmIH_CNTL));
	dev_info(adev->dev, "  IH_RB_CNTL=0x%08X\n",
		 RREG32(mmIH_RB_CNTL));
	dev_info(adev->dev, "  IH_RB_BASE=0x%08X\n",
		 RREG32(mmIH_RB_BASE));
	dev_info(adev->dev, "  IH_RB_WPTR_ADDR_LO=0x%08X\n",
		 RREG32(mmIH_RB_WPTR_ADDR_LO));
	dev_info(adev->dev, "  IH_RB_WPTR_ADDR_HI=0x%08X\n",
		 RREG32(mmIH_RB_WPTR_ADDR_HI));
	dev_info(adev->dev, "  IH_RB_RPTR=0x%08X\n",
		 RREG32(mmIH_RB_RPTR));
	dev_info(adev->dev, "  IH_RB_WPTR=0x%08X\n",
		 RREG32(mmIH_RB_WPTR));
}

static int cz_ih_soft_reset(void *handle)
{
	u32 srbm_soft_reset = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (tmp & SRBM_STATUS__IH_BUSY_MASK)
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET,
						SOFT_RESET_IH, 1);

	if (srbm_soft_reset) {
		cz_ih_print_status((void *)adev);

		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);

		cz_ih_print_status((void *)adev);
	}

	return 0;
}

static int cz_ih_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	// TODO
	return 0;
}

static int cz_ih_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	// TODO
	return 0;
}

const struct amd_ip_funcs cz_ih_ip_funcs = {
	.early_init = cz_ih_early_init,
	.late_init = NULL,
	.sw_init = cz_ih_sw_init,
	.sw_fini = cz_ih_sw_fini,
	.hw_init = cz_ih_hw_init,
	.hw_fini = cz_ih_hw_fini,
	.suspend = cz_ih_suspend,
	.resume = cz_ih_resume,
	.is_idle = cz_ih_is_idle,
	.wait_for_idle = cz_ih_wait_for_idle,
	.soft_reset = cz_ih_soft_reset,
	.print_status = cz_ih_print_status,
	.set_clockgating_state = cz_ih_set_clockgating_state,
	.set_powergating_state = cz_ih_set_powergating_state,
};

static const struct amdgpu_ih_funcs cz_ih_funcs = {
	.get_wptr = cz_ih_get_wptr,
	.decode_iv = cz_ih_decode_iv,
	.set_rptr = cz_ih_set_rptr
};

static void cz_ih_set_interrupt_funcs(struct amdgpu_device *adev)
{
	if (adev->irq.ih_funcs == NULL)
		adev->irq.ih_funcs = &cz_ih_funcs;
}


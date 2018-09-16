/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_ih.h"
#include "soc15.h"

#include "oss/osssys_4_0_offset.h"
#include "oss/osssys_4_0_sh_mask.h"

#include "soc15_common.h"
#include "vega10_ih.h"



static void vega10_ih_set_interrupt_funcs(struct amdgpu_device *adev);

/**
 * vega10_ih_enable_interrupts - Enable the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 *
 * Enable the interrupt ring buffer (VEGA10).
 */
static void vega10_ih_enable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, ENABLE_INTR, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL, ih_rb_cntl);
	adev->irq.ih.enabled = true;
}

/**
 * vega10_ih_disable_interrupts - Disable the interrupt ring buffer
 *
 * @adev: amdgpu_device pointer
 *
 * Disable the interrupt ring buffer (VEGA10).
 */
static void vega10_ih_disable_interrupts(struct amdgpu_device *adev)
{
	u32 ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL);

	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_ENABLE, 0);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, ENABLE_INTR, 0);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL, ih_rb_cntl);
	/* set rptr, wptr to 0 */
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR, 0);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR, 0);
	adev->irq.ih.enabled = false;
	adev->irq.ih.rptr = 0;
}

/**
 * vega10_ih_irq_init - init and enable the interrupt ring
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate a ring buffer for the interrupt controller,
 * enable the RLC, disable interrupts, enable the IH
 * ring buffer and enable it (VI).
 * Called at device load and reume.
 * Returns 0 for success, errors for failure.
 */
static int vega10_ih_irq_init(struct amdgpu_device *adev)
{
	int ret = 0;
	int rb_bufsz;
	u32 ih_rb_cntl, ih_doorbell_rtpr;
	u32 tmp;
	u64 wptr_off;

	/* disable irqs */
	vega10_ih_disable_interrupts(adev);

	adev->nbio_funcs->ih_control(adev);

	ih_rb_cntl = RREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL);
	/* Ring Buffer base. [39:8] of 40-bit address of the beginning of the ring buffer*/
	if (adev->irq.ih.use_bus_addr) {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE, adev->irq.ih.rb_dma_addr >> 8);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_HI, ((u64)adev->irq.ih.rb_dma_addr >> 40) & 0xff);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_SPACE, 1);
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE, adev->irq.ih.gpu_addr >> 8);
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_BASE_HI, (adev->irq.ih.gpu_addr >> 40) & 0xff);
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_SPACE, 4);
	}
	rb_bufsz = order_base_2(adev->irq.ih.ring_size / 4);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, WPTR_OVERFLOW_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RB_SIZE, rb_bufsz);
	/* Ring Buffer write pointer writeback. If enabled, IH_RB_WPTR register value is written to memory */
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, WPTR_WRITEBACK_ENABLE, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_SNOOP, 1);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_RO, 0);
	ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, MC_VMID, 0);

	if (adev->irq.msi_enabled)
		ih_rb_cntl = REG_SET_FIELD(ih_rb_cntl, IH_RB_CNTL, RPTR_REARM, 1);

	WREG32_SOC15(OSSSYS, 0, mmIH_RB_CNTL, ih_rb_cntl);

	/* set the writeback address whether it's enabled or not */
	if (adev->irq.ih.use_bus_addr)
		wptr_off = adev->irq.ih.rb_dma_addr + (adev->irq.ih.wptr_offs * 4);
	else
		wptr_off = adev->wb.gpu_addr + (adev->irq.ih.wptr_offs * 4);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_ADDR_LO, lower_32_bits(wptr_off));
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR_ADDR_HI, upper_32_bits(wptr_off) & 0xFF);

	/* set rptr, wptr to 0 */
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR, 0);
	WREG32_SOC15(OSSSYS, 0, mmIH_RB_WPTR, 0);

	ih_doorbell_rtpr = RREG32_SOC15(OSSSYS, 0, mmIH_DOORBELL_RPTR);
	if (adev->irq.ih.use_doorbell) {
		ih_doorbell_rtpr = REG_SET_FIELD(ih_doorbell_rtpr, IH_DOORBELL_RPTR,
						 OFFSET, adev->irq.ih.doorbell_index);
		ih_doorbell_rtpr = REG_SET_FIELD(ih_doorbell_rtpr, IH_DOORBELL_RPTR,
						 ENABLE, 1);
	} else {
		ih_doorbell_rtpr = REG_SET_FIELD(ih_doorbell_rtpr, IH_DOORBELL_RPTR,
						 ENABLE, 0);
	}
	WREG32_SOC15(OSSSYS, 0, mmIH_DOORBELL_RPTR, ih_doorbell_rtpr);
	adev->nbio_funcs->ih_doorbell_range(adev, adev->irq.ih.use_doorbell,
					    adev->irq.ih.doorbell_index);

	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_STORM_CLIENT_LIST_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_STORM_CLIENT_LIST_CNTL,
			    CLIENT18_IS_STORM_CLIENT, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_STORM_CLIENT_LIST_CNTL, tmp);

	tmp = RREG32_SOC15(OSSSYS, 0, mmIH_INT_FLOOD_CNTL);
	tmp = REG_SET_FIELD(tmp, IH_INT_FLOOD_CNTL, FLOOD_CNTL_ENABLE, 1);
	WREG32_SOC15(OSSSYS, 0, mmIH_INT_FLOOD_CNTL, tmp);

	pci_set_master(adev->pdev);

	/* enable interrupts */
	vega10_ih_enable_interrupts(adev);

	return ret;
}

/**
 * vega10_ih_irq_disable - disable interrupts
 *
 * @adev: amdgpu_device pointer
 *
 * Disable interrupts on the hw (VEGA10).
 */
static void vega10_ih_irq_disable(struct amdgpu_device *adev)
{
	vega10_ih_disable_interrupts(adev);

	/* Wait and acknowledge irq */
	mdelay(1);
}

/**
 * vega10_ih_get_wptr - get the IH ring buffer wptr
 *
 * @adev: amdgpu_device pointer
 *
 * Get the IH ring buffer wptr from either the register
 * or the writeback memory buffer (VEGA10).  Also check for
 * ring buffer overflow and deal with it.
 * Returns the value of the wptr.
 */
static u32 vega10_ih_get_wptr(struct amdgpu_device *adev)
{
	u32 wptr, tmp;

	if (adev->irq.ih.use_bus_addr)
		wptr = le32_to_cpu(adev->irq.ih.ring[adev->irq.ih.wptr_offs]);
	else
		wptr = le32_to_cpu(adev->wb.wb[adev->irq.ih.wptr_offs]);

	if (REG_GET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW)) {
		wptr = REG_SET_FIELD(wptr, IH_RB_WPTR, RB_OVERFLOW, 0);

		/* When a ring buffer overflow happen start parsing interrupt
		 * from the last not overwritten vector (wptr + 32). Hopefully
		 * this should allow us to catchup.
		 */
		tmp = (wptr + 32) & adev->irq.ih.ptr_mask;
		dev_warn(adev->dev, "IH ring buffer overflow (0x%08X, 0x%08X, 0x%08X)\n",
			wptr, adev->irq.ih.rptr, tmp);
		adev->irq.ih.rptr = tmp;

		tmp = RREG32_NO_KIQ(SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL));
		tmp = REG_SET_FIELD(tmp, IH_RB_CNTL, WPTR_OVERFLOW_CLEAR, 1);
		WREG32_NO_KIQ(SOC15_REG_OFFSET(OSSSYS, 0, mmIH_RB_CNTL), tmp);
	}
	return (wptr & adev->irq.ih.ptr_mask);
}

/**
 * vega10_ih_prescreen_iv - prescreen an interrupt vector
 *
 * @adev: amdgpu_device pointer
 *
 * Returns true if the interrupt vector should be further processed.
 */
static bool vega10_ih_prescreen_iv(struct amdgpu_device *adev)
{
	u32 ring_index = adev->irq.ih.rptr >> 2;
	u32 dw0, dw3, dw4, dw5;
	u16 pasid;
	u64 addr, key;
	struct amdgpu_vm *vm;
	int r;

	dw0 = le32_to_cpu(adev->irq.ih.ring[ring_index + 0]);
	dw3 = le32_to_cpu(adev->irq.ih.ring[ring_index + 3]);
	dw4 = le32_to_cpu(adev->irq.ih.ring[ring_index + 4]);
	dw5 = le32_to_cpu(adev->irq.ih.ring[ring_index + 5]);

	/* Filter retry page faults, let only the first one pass. If
	 * there are too many outstanding faults, ignore them until
	 * some faults get cleared.
	 */
	switch (dw0 & 0xff) {
	case SOC15_IH_CLIENTID_VMC:
	case SOC15_IH_CLIENTID_UTCL2:
		break;
	default:
		/* Not a VM fault */
		return true;
	}

	pasid = dw3 & 0xffff;
	/* No PASID, can't identify faulting process */
	if (!pasid)
		return true;

	/* Not a retry fault, check fault credit */
	if (!(dw5 & 0x80)) {
		if (!amdgpu_vm_pasid_fault_credit(adev, pasid))
			goto ignore_iv;
		return true;
	}

	/* Track retry faults in per-VM fault FIFO. */
	spin_lock(&adev->vm_manager.pasid_lock);
	vm = idr_find(&adev->vm_manager.pasid_idr, pasid);
	addr = ((u64)(dw5 & 0xf) << 44) | ((u64)dw4 << 12);
	key = AMDGPU_VM_FAULT(pasid, addr);
	if (!vm) {
		/* VM not found, process it normally */
		spin_unlock(&adev->vm_manager.pasid_lock);
		return true;
	} else {
		r = amdgpu_vm_add_fault(vm->fault_hash, key);

		/* Hash table is full or the fault is already being processed,
		 * ignore further page faults
		 */
		if (r != 0) {
			spin_unlock(&adev->vm_manager.pasid_lock);
			goto ignore_iv;
		}
	}
	/* No locking required with single writer and single reader */
	r = kfifo_put(&vm->faults, key);
	if (!r) {
		/* FIFO is full. Ignore it until there is space */
		amdgpu_vm_clear_fault(vm->fault_hash, key);
		spin_unlock(&adev->vm_manager.pasid_lock);
		goto ignore_iv;
	}

	spin_unlock(&adev->vm_manager.pasid_lock);
	/* It's the first fault for this address, process it normally */
	return true;

ignore_iv:
	adev->irq.ih.rptr += 32;
	return false;
}

/**
 * vega10_ih_decode_iv - decode an interrupt vector
 *
 * @adev: amdgpu_device pointer
 *
 * Decodes the interrupt vector at the current rptr
 * position and also advance the position.
 */
static void vega10_ih_decode_iv(struct amdgpu_device *adev,
				 struct amdgpu_iv_entry *entry)
{
	/* wptr/rptr are in bytes! */
	u32 ring_index = adev->irq.ih.rptr >> 2;
	uint32_t dw[8];

	dw[0] = le32_to_cpu(adev->irq.ih.ring[ring_index + 0]);
	dw[1] = le32_to_cpu(adev->irq.ih.ring[ring_index + 1]);
	dw[2] = le32_to_cpu(adev->irq.ih.ring[ring_index + 2]);
	dw[3] = le32_to_cpu(adev->irq.ih.ring[ring_index + 3]);
	dw[4] = le32_to_cpu(adev->irq.ih.ring[ring_index + 4]);
	dw[5] = le32_to_cpu(adev->irq.ih.ring[ring_index + 5]);
	dw[6] = le32_to_cpu(adev->irq.ih.ring[ring_index + 6]);
	dw[7] = le32_to_cpu(adev->irq.ih.ring[ring_index + 7]);

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
	adev->irq.ih.rptr += 32;
}

/**
 * vega10_ih_set_rptr - set the IH ring buffer rptr
 *
 * @adev: amdgpu_device pointer
 *
 * Set the IH ring buffer rptr.
 */
static void vega10_ih_set_rptr(struct amdgpu_device *adev)
{
	if (adev->irq.ih.use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		if (adev->irq.ih.use_bus_addr)
			adev->irq.ih.ring[adev->irq.ih.rptr_offs] = adev->irq.ih.rptr;
		else
			adev->wb.wb[adev->irq.ih.rptr_offs] = adev->irq.ih.rptr;
		WDOORBELL32(adev->irq.ih.doorbell_index, adev->irq.ih.rptr);
	} else {
		WREG32_SOC15(OSSSYS, 0, mmIH_RB_RPTR, adev->irq.ih.rptr);
	}
}

static int vega10_ih_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	vega10_ih_set_interrupt_funcs(adev);
	return 0;
}

static int vega10_ih_sw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_ih_ring_init(adev, &adev->irq.ih, 256 * 1024, true);
	if (r)
		return r;

	adev->irq.ih.use_doorbell = true;
	adev->irq.ih.doorbell_index = AMDGPU_DOORBELL64_IH << 1;

	r = amdgpu_irq_init(adev);

	return r;
}

static int vega10_ih_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_fini(adev);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih);

	return 0;
}

static int vega10_ih_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vega10_ih_irq_init(adev);
	if (r)
		return r;

	return 0;
}

static int vega10_ih_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	vega10_ih_irq_disable(adev);

	return 0;
}

static int vega10_ih_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return vega10_ih_hw_fini(adev);
}

static int vega10_ih_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return vega10_ih_hw_init(adev);
}

static bool vega10_ih_is_idle(void *handle)
{
	/* todo */
	return true;
}

static int vega10_ih_wait_for_idle(void *handle)
{
	/* todo */
	return -ETIMEDOUT;
}

static int vega10_ih_soft_reset(void *handle)
{
	/* todo */

	return 0;
}

static int vega10_ih_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int vega10_ih_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs vega10_ih_ip_funcs = {
	.name = "vega10_ih",
	.early_init = vega10_ih_early_init,
	.late_init = NULL,
	.sw_init = vega10_ih_sw_init,
	.sw_fini = vega10_ih_sw_fini,
	.hw_init = vega10_ih_hw_init,
	.hw_fini = vega10_ih_hw_fini,
	.suspend = vega10_ih_suspend,
	.resume = vega10_ih_resume,
	.is_idle = vega10_ih_is_idle,
	.wait_for_idle = vega10_ih_wait_for_idle,
	.soft_reset = vega10_ih_soft_reset,
	.set_clockgating_state = vega10_ih_set_clockgating_state,
	.set_powergating_state = vega10_ih_set_powergating_state,
};

static const struct amdgpu_ih_funcs vega10_ih_funcs = {
	.get_wptr = vega10_ih_get_wptr,
	.prescreen_iv = vega10_ih_prescreen_iv,
	.decode_iv = vega10_ih_decode_iv,
	.set_rptr = vega10_ih_set_rptr
};

static void vega10_ih_set_interrupt_funcs(struct amdgpu_device *adev)
{
	adev->irq.ih_funcs = &vega10_ih_funcs;
}

const struct amdgpu_ip_block_version vega10_ih_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_IH,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &vega10_ih_ip_funcs,
};

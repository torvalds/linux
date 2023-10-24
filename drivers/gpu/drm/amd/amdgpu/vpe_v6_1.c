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
 */

#include <linux/firmware.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_vpe.h"
#include "vpe_v6_1.h"
#include "soc15_common.h"
#include "ivsrcid/vpe/irqsrcs_vpe_6_1.h"
#include "vpe/vpe_6_1_0_offset.h"
#include "vpe/vpe_6_1_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/vpe_6_1_0.bin");

#define VPE_THREAD1_UCODE_OFFSET	0x8000

static uint32_t vpe_v6_1_get_reg_offset(struct amdgpu_vpe *vpe, uint32_t inst, uint32_t offset)
{
	uint32_t base;

	base = vpe->ring.adev->reg_offset[VPE_HWIP][0][0];

	return base + offset;
}

static void vpe_v6_1_halt(struct amdgpu_vpe *vpe, bool halt)
{
	struct amdgpu_device *adev = vpe->ring.adev;
	uint32_t f32_cntl;

	f32_cntl = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_F32_CNTL));
	f32_cntl = REG_SET_FIELD(f32_cntl, VPEC_F32_CNTL, HALT, halt ? 1 : 0);
	f32_cntl = REG_SET_FIELD(f32_cntl, VPEC_F32_CNTL, TH1_RESET, halt ? 1 : 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_F32_CNTL), f32_cntl);
}

static int vpe_v6_1_irq_init(struct amdgpu_vpe *vpe)
{
	struct amdgpu_device *adev = container_of(vpe, struct amdgpu_device, vpe);
	int ret;

	ret = amdgpu_irq_add_id(adev, SOC21_IH_CLIENTID_VPE,
				VPE_6_1_SRCID__VPE_TRAP,
				&adev->vpe.trap_irq);
	if (ret)
		return ret;

	return 0;
}

static int vpe_v6_1_load_microcode(struct amdgpu_vpe *vpe)
{
	struct amdgpu_device *adev = vpe->ring.adev;
	const struct vpe_firmware_header_v1_0 *vpe_hdr;
	const __le32 *data;
	uint32_t ucode_offset[2], ucode_size[2];
	uint32_t i, size_dw;
	uint32_t ret;

	// disable UMSCH_INT_ENABLE
	ret = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_CNTL));
	ret = REG_SET_FIELD(ret, VPEC_CNTL, UMSCH_INT_ENABLE, 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_CNTL), ret);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		uint32_t f32_offset, f32_cntl;

		f32_offset = vpe_get_reg_offset(vpe, 0, regVPEC_F32_CNTL);
		f32_cntl = RREG32(f32_offset);
		f32_cntl = REG_SET_FIELD(f32_cntl, VPEC_F32_CNTL, HALT, 0);
		f32_cntl = REG_SET_FIELD(f32_cntl, VPEC_F32_CNTL, TH1_RESET, 0);

		adev->vpe.cmdbuf_cpu_addr[0] = f32_offset;
		adev->vpe.cmdbuf_cpu_addr[1] = f32_cntl;

		amdgpu_vpe_psp_update_sram(adev);
		return 0;
	}

	vpe_hdr = (const struct vpe_firmware_header_v1_0 *)adev->vpe.fw->data;

	/* Thread 0(command thread) ucode offset/size */
	ucode_offset[0] = le32_to_cpu(vpe_hdr->header.ucode_array_offset_bytes);
	ucode_size[0] = le32_to_cpu(vpe_hdr->ctx_ucode_size_bytes);
	/* Thread 1(control thread) ucode offset/size */
	ucode_offset[1] = le32_to_cpu(vpe_hdr->ctl_ucode_offset);
	ucode_size[1] = le32_to_cpu(vpe_hdr->ctl_ucode_size_bytes);

	vpe_v6_1_halt(vpe, true);

	for (i = 0; i < 2; i++) {
		if (i > 0)
			WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_UCODE_ADDR), VPE_THREAD1_UCODE_OFFSET);
		else
			WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_UCODE_ADDR), 0);

		data = (const __le32 *)(adev->vpe.fw->data + ucode_offset[i]);
		size_dw = ucode_size[i] / sizeof(__le32);

		while (size_dw--) {
			if (amdgpu_emu_mode && size_dw % 500 == 0)
				msleep(1);
			WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_UCODE_DATA), le32_to_cpup(data++));
		}

	}

	vpe_v6_1_halt(vpe, false);

	return 0;
}

static int vpe_v6_1_ring_start(struct amdgpu_vpe *vpe)
{
	struct amdgpu_ring *ring = &vpe->ring;
	struct amdgpu_device *adev = ring->adev;
	uint32_t rb_bufsz, rb_cntl;
	uint32_t ib_cntl;
	uint32_t doorbell, doorbell_offset;
	int ret;

	rb_bufsz = order_base_2(ring->ring_size / 4);
	rb_cntl = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_CNTL));
	rb_cntl = REG_SET_FIELD(rb_cntl, VPEC_QUEUE0_RB_CNTL, RB_SIZE, rb_bufsz);
	rb_cntl = REG_SET_FIELD(rb_cntl, VPEC_QUEUE0_RB_CNTL, RB_PRIV, 1);
	rb_cntl = REG_SET_FIELD(rb_cntl, VPEC_QUEUE0_RB_CNTL, RB_VMID, 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_CNTL), rb_cntl);

	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_RPTR), 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_RPTR_HI), 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_WPTR), 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_WPTR_HI), 0);

	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_RPTR_ADDR_LO),
	       lower_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFC);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_RPTR_ADDR_HI),
	       upper_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFF);

	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_BASE), ring->gpu_addr >> 8);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_BASE_HI), ring->gpu_addr >> 40);

	ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_MINOR_PTR_UPDATE), 1);

	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_WPTR), lower_32_bits(ring->wptr) << 2);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_WPTR_HI), upper_32_bits(ring->wptr) << 2);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_MINOR_PTR_UPDATE), 0);

	doorbell = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_DOORBELL));
	doorbell_offset = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_DOORBELL_OFFSET));

	doorbell = REG_SET_FIELD(doorbell, VPEC_QUEUE0_DOORBELL, ENABLE, ring->use_doorbell ? 1 : 0);
	doorbell_offset = REG_SET_FIELD(doorbell_offset, VPEC_QUEUE0_DOORBELL_OFFSET, OFFSET, ring->doorbell_index);

	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_DOORBELL), doorbell);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_DOORBELL_OFFSET), doorbell_offset);

	adev->nbio.funcs->vpe_doorbell_range(adev, 0, ring->use_doorbell, ring->doorbell_index, 2);

	rb_cntl = REG_SET_FIELD(rb_cntl, VPEC_QUEUE0_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);
	rb_cntl = REG_SET_FIELD(rb_cntl, VPEC_QUEUE0_RB_CNTL, RB_ENABLE, 1);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_CNTL), rb_cntl);

	ib_cntl = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_IB_CNTL));
	ib_cntl = REG_SET_FIELD(ib_cntl, VPEC_QUEUE0_IB_CNTL, IB_ENABLE, 1);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_IB_CNTL), ib_cntl);

	ring->sched.ready = true;

	ret = amdgpu_ring_test_helper(ring);
	if (ret) {
		ring->sched.ready = false;
		return ret;
	}

	return 0;
}

static int vpe_v_6_1_ring_stop(struct amdgpu_vpe *vpe)
{
	struct amdgpu_device *adev = vpe->ring.adev;
	uint32_t rb_cntl, ib_cntl;

	rb_cntl = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_CNTL));
	rb_cntl = REG_SET_FIELD(rb_cntl, VPEC_QUEUE0_RB_CNTL, RB_ENABLE, 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_RB_CNTL), rb_cntl);

	ib_cntl = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_IB_CNTL));
	ib_cntl = REG_SET_FIELD(ib_cntl, VPEC_QUEUE0_IB_CNTL, IB_ENABLE, 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_QUEUE0_IB_CNTL), ib_cntl);

	vpe->ring.sched.ready = false;

	return 0;
}

static int vpe_v6_1_set_trap_irq_state(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *source,
				       unsigned int type,
				       enum amdgpu_interrupt_state state)
{
	struct amdgpu_vpe *vpe = &adev->vpe;
	uint32_t vpe_cntl;

	vpe_cntl = RREG32(vpe_get_reg_offset(vpe, 0, regVPEC_CNTL));
	vpe_cntl = REG_SET_FIELD(vpe_cntl, VPEC_CNTL, TRAP_ENABLE,
				 state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32(vpe_get_reg_offset(vpe, 0, regVPEC_CNTL), vpe_cntl);

	return 0;
}

static int vpe_v6_1_process_trap_irq(struct amdgpu_device *adev,
				     struct amdgpu_irq_src *source,
				     struct amdgpu_iv_entry *entry)
{

	dev_dbg(adev->dev, "IH: VPE trap\n");

	switch (entry->client_id) {
	case SOC21_IH_CLIENTID_VPE:
		amdgpu_fence_process(&adev->vpe.ring);
		break;
	default:
		break;
	}

	return 0;
}

static int vpe_v6_1_set_regs(struct amdgpu_vpe *vpe)
{
	vpe->regs.queue0_rb_rptr_lo = regVPEC_QUEUE0_RB_RPTR;
	vpe->regs.queue0_rb_rptr_hi = regVPEC_QUEUE0_RB_RPTR_HI;
	vpe->regs.queue0_rb_wptr_lo = regVPEC_QUEUE0_RB_WPTR;
	vpe->regs.queue0_rb_wptr_hi = regVPEC_QUEUE0_RB_WPTR_HI;
	vpe->regs.queue0_preempt = regVPEC_QUEUE0_PREEMPT;

	return 0;
}

static const struct vpe_funcs vpe_v6_1_funcs = {
	.get_reg_offset = vpe_v6_1_get_reg_offset,
	.set_regs = vpe_v6_1_set_regs,
	.irq_init = vpe_v6_1_irq_init,
	.init_microcode = amdgpu_vpe_init_microcode,
	.load_microcode = vpe_v6_1_load_microcode,
	.ring_init = amdgpu_vpe_ring_init,
	.ring_start = vpe_v6_1_ring_start,
	.ring_stop = vpe_v_6_1_ring_stop,
	.ring_fini = amdgpu_vpe_ring_fini,
};

static const struct amdgpu_irq_src_funcs vpe_v6_1_trap_irq_funcs = {
	.set = vpe_v6_1_set_trap_irq_state,
	.process = vpe_v6_1_process_trap_irq,
};

void vpe_v6_1_set_funcs(struct amdgpu_vpe *vpe)
{
	vpe->funcs = &vpe_v6_1_funcs;
	vpe->trap_irq.funcs = &vpe_v6_1_trap_irq_funcs;
}

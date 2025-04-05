/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * Authors: Christian König <christian.koenig@amd.com>
 */

#include <linux/firmware.h>

#include "amdgpu.h"
#include "amdgpu_vce.h"
#include "cikd.h"
#include "vce/vce_2_0_d.h"
#include "vce/vce_2_0_sh_mask.h"
#include "smu/smu_7_0_1_d.h"
#include "smu/smu_7_0_1_sh_mask.h"
#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"

#define VCE_V2_0_FW_SIZE	(256 * 1024)
#define VCE_V2_0_STACK_SIZE	(64 * 1024)
#define VCE_V2_0_DATA_SIZE	(23552 * AMDGPU_MAX_VCE_HANDLES)
#define VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK	0x02

static void vce_v2_0_set_ring_funcs(struct amdgpu_device *adev);
static void vce_v2_0_set_irq_funcs(struct amdgpu_device *adev);

/**
 * vce_v2_0_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vce_v2_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		return RREG32(mmVCE_RB_RPTR);
	else
		return RREG32(mmVCE_RB_RPTR2);
}

/**
 * vce_v2_0_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vce_v2_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		return RREG32(mmVCE_RB_WPTR);
	else
		return RREG32(mmVCE_RB_WPTR2);
}

/**
 * vce_v2_0_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vce_v2_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		WREG32(mmVCE_RB_WPTR, lower_32_bits(ring->wptr));
	else
		WREG32(mmVCE_RB_WPTR2, lower_32_bits(ring->wptr));
}

static int vce_v2_0_lmi_clean(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			uint32_t status = RREG32(mmVCE_LMI_STATUS);

			if (status & 0x337f)
				return 0;
			mdelay(10);
		}
	}

	return -ETIMEDOUT;
}

static int vce_v2_0_firmware_loaded(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			uint32_t status = RREG32(mmVCE_STATUS);

			if (status & VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK)
				return 0;
			mdelay(10);
		}

		DRM_ERROR("VCE not responding, trying to reset the ECPU!!!\n");
		WREG32_P(mmVCE_SOFT_RESET,
			VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK,
			~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
		mdelay(10);
		WREG32_P(mmVCE_SOFT_RESET, 0,
			~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
		mdelay(10);
	}

	return -ETIMEDOUT;
}

static void vce_v2_0_disable_cg(struct amdgpu_device *adev)
{
	WREG32(mmVCE_CGTT_CLK_OVERRIDE, 7);
}

static void vce_v2_0_init_cg(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32(mmVCE_CLOCK_GATING_A);
	tmp &= ~0xfff;
	tmp |= ((0 << 0) | (4 << 4));
	tmp |= 0x40000;
	WREG32(mmVCE_CLOCK_GATING_A, tmp);

	tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
	tmp &= ~0xfff;
	tmp |= ((0 << 0) | (4 << 4));
	WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

	tmp = RREG32(mmVCE_CLOCK_GATING_B);
	tmp |= 0x10;
	tmp &= ~0x100000;
	WREG32(mmVCE_CLOCK_GATING_B, tmp);
}

static void vce_v2_0_mc_resume(struct amdgpu_device *adev)
{
	uint32_t size, offset;

	WREG32_P(mmVCE_CLOCK_GATING_A, 0, ~(1 << 16));
	WREG32_P(mmVCE_UENC_CLOCK_GATING, 0x1FF000, ~0xFF9FF000);
	WREG32_P(mmVCE_UENC_REG_CLOCK_GATING, 0x3F, ~0x3F);
	WREG32(mmVCE_CLOCK_GATING_B, 0xf7);

	WREG32(mmVCE_LMI_CTRL, 0x00398000);
	WREG32_P(mmVCE_LMI_CACHE_CTRL, 0x0, ~0x1);
	WREG32(mmVCE_LMI_SWAP_CNTL, 0);
	WREG32(mmVCE_LMI_SWAP_CNTL1, 0);
	WREG32(mmVCE_LMI_VM_CTRL, 0);

	WREG32(mmVCE_LMI_VCPU_CACHE_40BIT_BAR, (adev->vce.gpu_addr >> 8));

	offset = AMDGPU_VCE_FIRMWARE_OFFSET;
	size = VCE_V2_0_FW_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET0, offset & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE0, size);

	offset += size;
	size = VCE_V2_0_STACK_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET1, offset & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE1, size);

	offset += size;
	size = VCE_V2_0_DATA_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET2, offset & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE2, size);

	WREG32_P(mmVCE_LMI_CTRL2, 0x0, ~0x100);
	WREG32_FIELD(VCE_SYS_INT_EN, VCE_SYS_INT_TRAP_INTERRUPT_EN, 1);
}

static bool vce_v2_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return !(RREG32(mmSRBM_STATUS2) & SRBM_STATUS2__VCE_BUSY_MASK);
}

static int vce_v2_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	unsigned i;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (vce_v2_0_is_idle(adev))
			return 0;
	}
	return -ETIMEDOUT;
}

/**
 * vce_v2_0_start - start VCE block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the VCE block
 */
static int vce_v2_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	/* set BUSY flag */
	WREG32_P(mmVCE_STATUS, 1, ~1);

	vce_v2_0_init_cg(adev);
	vce_v2_0_disable_cg(adev);

	vce_v2_0_mc_resume(adev);

	ring = &adev->vce.ring[0];
	WREG32(mmVCE_RB_RPTR, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_WPTR, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_BASE_LO, ring->gpu_addr);
	WREG32(mmVCE_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_SIZE, ring->ring_size / 4);

	ring = &adev->vce.ring[1];
	WREG32(mmVCE_RB_RPTR2, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_WPTR2, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_BASE_LO2, ring->gpu_addr);
	WREG32(mmVCE_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_SIZE2, ring->ring_size / 4);

	WREG32_FIELD(VCE_VCPU_CNTL, CLK_EN, 1);
	WREG32_FIELD(VCE_SOFT_RESET, ECPU_SOFT_RESET, 1);
	mdelay(100);
	WREG32_FIELD(VCE_SOFT_RESET, ECPU_SOFT_RESET, 0);

	r = vce_v2_0_firmware_loaded(adev);

	/* clear BUSY flag */
	WREG32_P(mmVCE_STATUS, 0, ~1);

	if (r) {
		DRM_ERROR("VCE not responding, giving up!!!\n");
		return r;
	}

	return 0;
}

static int vce_v2_0_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ip_block *ip_block;
	int i;
	int status;


	if (vce_v2_0_lmi_clean(adev)) {
		DRM_INFO("vce is not idle \n");
		return 0;
	}

	ip_block = amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_VCN);
	if (!ip_block)
		return -EINVAL;

	if (vce_v2_0_wait_for_idle(ip_block)) {
		DRM_INFO("VCE is busy, Can't set clock gating");
		return 0;
	}

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(mmVCE_LMI_CTRL2, 1 << 8, ~(1 << 8));

	for (i = 0; i < 100; ++i) {
		status = RREG32(mmVCE_LMI_STATUS);
		if (status & 0x240)
			break;
		mdelay(1);
	}

	WREG32_P(mmVCE_VCPU_CNTL, 0, ~0x80001);

	/* put LMI, VCPU, RBC etc... into reset */
	WREG32_P(mmVCE_SOFT_RESET, 1, ~0x1);

	WREG32(mmVCE_STATUS, 0);

	return 0;
}

static void vce_v2_0_set_sw_cg(struct amdgpu_device *adev, bool gated)
{
	u32 tmp;

	if (gated) {
		tmp = RREG32(mmVCE_CLOCK_GATING_B);
		tmp |= 0xe70000;
		WREG32(mmVCE_CLOCK_GATING_B, tmp);

		tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
		tmp |= 0xff000000;
		WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

		tmp = RREG32(mmVCE_UENC_REG_CLOCK_GATING);
		tmp &= ~0x3fc;
		WREG32(mmVCE_UENC_REG_CLOCK_GATING, tmp);

		WREG32(mmVCE_CGTT_CLK_OVERRIDE, 0);
	} else {
		tmp = RREG32(mmVCE_CLOCK_GATING_B);
		tmp |= 0xe7;
		tmp &= ~0xe70000;
		WREG32(mmVCE_CLOCK_GATING_B, tmp);

		tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
		tmp |= 0x1fe000;
		tmp &= ~0xff000000;
		WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

		tmp = RREG32(mmVCE_UENC_REG_CLOCK_GATING);
		tmp |= 0x3fc;
		WREG32(mmVCE_UENC_REG_CLOCK_GATING, tmp);
	}
}

static void vce_v2_0_set_dyn_cg(struct amdgpu_device *adev, bool gated)
{
	u32 orig, tmp;

/* LMI_MC/LMI_UMC always set in dynamic,
 * set {CGC_*_GATE_MODE, CGC_*_SW_GATE} = {0, 0}
 */
	tmp = RREG32(mmVCE_CLOCK_GATING_B);
	tmp &= ~0x00060006;

/* Exception for ECPU, IH, SEM, SYS blocks needs to be turned on/off by SW */
	if (gated) {
		tmp |= 0xe10000;
		WREG32(mmVCE_CLOCK_GATING_B, tmp);
	} else {
		tmp |= 0xe1;
		tmp &= ~0xe10000;
		WREG32(mmVCE_CLOCK_GATING_B, tmp);
	}

	orig = tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
	tmp &= ~0x1fe000;
	tmp &= ~0xff000000;
	if (tmp != orig)
		WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

	orig = tmp = RREG32(mmVCE_UENC_REG_CLOCK_GATING);
	tmp &= ~0x3fc;
	if (tmp != orig)
		WREG32(mmVCE_UENC_REG_CLOCK_GATING, tmp);

	/* set VCE_UENC_REG_CLOCK_GATING always in dynamic mode */
	WREG32(mmVCE_UENC_REG_CLOCK_GATING, 0x00);

	if(gated)
		WREG32(mmVCE_CGTT_CLK_OVERRIDE, 0);
}

static void vce_v2_0_enable_mgcg(struct amdgpu_device *adev, bool enable,
								bool sw_cg)
{
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_VCE_MGCG)) {
		if (sw_cg)
			vce_v2_0_set_sw_cg(adev, true);
		else
			vce_v2_0_set_dyn_cg(adev, true);
	} else {
		vce_v2_0_disable_cg(adev);

		if (sw_cg)
			vce_v2_0_set_sw_cg(adev, false);
		else
			vce_v2_0_set_dyn_cg(adev, false);
	}
}

static int vce_v2_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->vce.num_rings = 2;

	vce_v2_0_set_ring_funcs(adev);
	vce_v2_0_set_irq_funcs(adev);

	return 0;
}

static int vce_v2_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;

	/* VCE */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 167, &adev->vce.irq);
	if (r)
		return r;

	r = amdgpu_vce_sw_init(adev, VCE_V2_0_FW_SIZE +
		VCE_V2_0_STACK_SIZE + VCE_V2_0_DATA_SIZE);
	if (r)
		return r;

	r = amdgpu_vce_resume(adev);
	if (r)
		return r;

	for (i = 0; i < adev->vce.num_rings; i++) {
		enum amdgpu_ring_priority_level hw_prio = amdgpu_vce_get_ring_prio(i);

		ring = &adev->vce.ring[i];
		sprintf(ring->name, "vce%d", i);
		r = amdgpu_ring_init(adev, ring, 512, &adev->vce.irq, 0,
				     hw_prio, NULL);
		if (r)
			return r;
	}

	return r;
}

static int vce_v2_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	r = amdgpu_vce_suspend(adev);
	if (r)
		return r;

	return amdgpu_vce_sw_fini(adev);
}

static int vce_v2_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;

	amdgpu_asic_set_vce_clocks(adev, 10000, 10000);
	vce_v2_0_enable_mgcg(adev, true, false);

	for (i = 0; i < adev->vce.num_rings; i++) {
		r = amdgpu_ring_test_helper(&adev->vce.ring[i]);
		if (r)
			return r;
	}

	DRM_INFO("VCE initialized successfully.\n");

	return 0;
}

static int vce_v2_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	cancel_delayed_work_sync(&ip_block->adev->vce.idle_work);

	return 0;
}

static int vce_v2_0_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;


	/*
	 * Proper cleanups before halting the HW engine:
	 *   - cancel the delayed idle work
	 *   - enable powergating
	 *   - enable clockgating
	 *   - disable dpm
	 *
	 * TODO: to align with the VCN implementation, move the
	 * jobs for clockgating/powergating/dpm setting to
	 * ->set_powergating_state().
	 */
	cancel_delayed_work_sync(&adev->vce.idle_work);

	if (adev->pm.dpm_enabled) {
		amdgpu_dpm_enable_vce(adev, false);
	} else {
		amdgpu_asic_set_vce_clocks(adev, 0, 0);
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_PG_STATE_GATE);
		amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_CG_STATE_GATE);
	}

	r = vce_v2_0_hw_fini(ip_block);
	if (r)
		return r;

	return amdgpu_vce_suspend(adev);
}

static int vce_v2_0_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = amdgpu_vce_resume(ip_block->adev);
	if (r)
		return r;

	return vce_v2_0_hw_init(ip_block);
}

static int vce_v2_0_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	WREG32_FIELD(SRBM_SOFT_RESET, SOFT_RESET_VCE, 1);
	mdelay(5);

	return vce_v2_0_start(adev);
}

static int vce_v2_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	uint32_t val = 0;

	if (state == AMDGPU_IRQ_STATE_ENABLE)
		val |= VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK;

	WREG32_P(mmVCE_SYS_INT_EN, val, ~VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK);
	return 0;
}

static int vce_v2_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: VCE\n");
	switch (entry->src_data[0]) {
	case 0:
	case 1:
		amdgpu_fence_process(&adev->vce.ring[entry->src_data[0]]);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static int vce_v2_0_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	bool gate = false;
	bool sw_cg = false;

	struct amdgpu_device *adev = ip_block->adev;

	if (state == AMD_CG_STATE_GATE) {
		gate = true;
		sw_cg = true;
	}

	vce_v2_0_enable_mgcg(adev, gate, sw_cg);

	return 0;
}

static int vce_v2_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	/* This doesn't actually powergate the VCE block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	struct amdgpu_device *adev = ip_block->adev;

	if (state == AMD_PG_STATE_GATE)
		return vce_v2_0_stop(adev);
	else
		return vce_v2_0_start(adev);
}

static const struct amd_ip_funcs vce_v2_0_ip_funcs = {
	.name = "vce_v2_0",
	.early_init = vce_v2_0_early_init,
	.sw_init = vce_v2_0_sw_init,
	.sw_fini = vce_v2_0_sw_fini,
	.hw_init = vce_v2_0_hw_init,
	.hw_fini = vce_v2_0_hw_fini,
	.suspend = vce_v2_0_suspend,
	.resume = vce_v2_0_resume,
	.is_idle = vce_v2_0_is_idle,
	.wait_for_idle = vce_v2_0_wait_for_idle,
	.soft_reset = vce_v2_0_soft_reset,
	.set_clockgating_state = vce_v2_0_set_clockgating_state,
	.set_powergating_state = vce_v2_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs vce_v2_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_VCE,
	.align_mask = 0xf,
	.nop = VCE_CMD_NO_OP,
	.support_64bit_ptrs = false,
	.no_user_fence = true,
	.get_rptr = vce_v2_0_ring_get_rptr,
	.get_wptr = vce_v2_0_ring_get_wptr,
	.set_wptr = vce_v2_0_ring_set_wptr,
	.parse_cs = amdgpu_vce_ring_parse_cs,
	.emit_frame_size = 6, /* amdgpu_vce_ring_emit_fence  x1 no user fence */
	.emit_ib_size = 4, /* amdgpu_vce_ring_emit_ib */
	.emit_ib = amdgpu_vce_ring_emit_ib,
	.emit_fence = amdgpu_vce_ring_emit_fence,
	.test_ring = amdgpu_vce_ring_test_ring,
	.test_ib = amdgpu_vce_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vce_ring_begin_use,
	.end_use = amdgpu_vce_ring_end_use,
};

static void vce_v2_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vce.num_rings; i++) {
		adev->vce.ring[i].funcs = &vce_v2_0_ring_funcs;
		adev->vce.ring[i].me = i;
	}
}

static const struct amdgpu_irq_src_funcs vce_v2_0_irq_funcs = {
	.set = vce_v2_0_set_interrupt_state,
	.process = vce_v2_0_process_interrupt,
};

static void vce_v2_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->vce.irq.num_types = 1;
	adev->vce.irq.funcs = &vce_v2_0_irq_funcs;
};

const struct amdgpu_ip_block_version vce_v2_0_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_VCE,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &vce_v2_0_ip_funcs,
};

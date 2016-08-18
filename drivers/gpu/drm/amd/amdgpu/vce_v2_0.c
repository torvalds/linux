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
#include <drm/drmP.h>
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

static void vce_v2_0_mc_resume(struct amdgpu_device *adev);
static void vce_v2_0_set_ring_funcs(struct amdgpu_device *adev);
static void vce_v2_0_set_irq_funcs(struct amdgpu_device *adev);
static int vce_v2_0_wait_for_idle(void *handle);
/**
 * vce_v2_0_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint32_t vce_v2_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vce.ring[0])
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
static uint32_t vce_v2_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vce.ring[0])
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

	if (ring == &adev->vce.ring[0])
		WREG32(mmVCE_RB_WPTR, ring->wptr);
	else
		WREG32(mmVCE_RB_WPTR2, ring->wptr);
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

	vce_v2_0_mc_resume(adev);

	/* set BUSY flag */
	WREG32_P(mmVCE_STATUS, 1, ~1);

	ring = &adev->vce.ring[0];
	WREG32(mmVCE_RB_RPTR, ring->wptr);
	WREG32(mmVCE_RB_WPTR, ring->wptr);
	WREG32(mmVCE_RB_BASE_LO, ring->gpu_addr);
	WREG32(mmVCE_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_SIZE, ring->ring_size / 4);

	ring = &adev->vce.ring[1];
	WREG32(mmVCE_RB_RPTR2, ring->wptr);
	WREG32(mmVCE_RB_WPTR2, ring->wptr);
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

static int vce_v2_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->vce.num_rings = 2;

	vce_v2_0_set_ring_funcs(adev);
	vce_v2_0_set_irq_funcs(adev);

	return 0;
}

static int vce_v2_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* VCE */
	r = amdgpu_irq_add_id(adev, 167, &adev->vce.irq);
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
		ring = &adev->vce.ring[i];
		sprintf(ring->name, "vce%d", i);
		r = amdgpu_ring_init(adev, ring, 512, VCE_CMD_NO_OP, 0xf,
				     &adev->vce.irq, 0, AMDGPU_RING_TYPE_VCE);
		if (r)
			return r;
	}

	return r;
}

static int vce_v2_0_sw_fini(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vce_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vce_sw_fini(adev);
	if (r)
		return r;

	return r;
}

static int vce_v2_0_hw_init(void *handle)
{
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vce_v2_0_start(adev);
	/* this error mean vcpu not in running state, so just skip ring test, not stop driver initialize */
	if (r)
		return 0;

	for (i = 0; i < adev->vce.num_rings; i++)
		adev->vce.ring[i].ready = false;

	for (i = 0; i < adev->vce.num_rings; i++) {
		r = amdgpu_ring_test_ring(&adev->vce.ring[i]);
		if (r)
			return r;
		else
			adev->vce.ring[i].ready = true;
	}

	DRM_INFO("VCE initialized successfully.\n");

	return 0;
}

static int vce_v2_0_hw_fini(void *handle)
{
	return 0;
}

static int vce_v2_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vce_v2_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vce_suspend(adev);
	if (r)
		return r;

	return r;
}

static int vce_v2_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vce_resume(adev);
	if (r)
		return r;

	r = vce_v2_0_hw_init(adev);
	if (r)
		return r;

	return r;
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
	if (vce_v2_0_wait_for_idle(adev)) {
		DRM_INFO("VCE is busy, Can't set clock gateing");
		return;
	}

	WREG32_P(mmVCE_LMI_CTRL2, 0x100, ~0x100);

	if (vce_v2_0_lmi_clean(adev)) {
		DRM_INFO("LMI is busy, Can't set clock gateing");
		return;
	}

	WREG32_P(mmVCE_VCPU_CNTL, 0, ~VCE_VCPU_CNTL__CLK_EN_MASK);
	WREG32_P(mmVCE_SOFT_RESET,
		 VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK,
		 ~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
	WREG32(mmVCE_STATUS, 0);

	if (gated)
		WREG32(mmVCE_CGTT_CLK_OVERRIDE, 0);
	/* LMI_MC/LMI_UMC always set in dynamic, set {CGC_*_GATE_MODE, CGC_*_SW_GATE} = {0, 0} */
	if (gated) {
		/* Force CLOCK OFF , set {CGC_*_GATE_MODE, CGC_*_SW_GATE} = {*, 1} */
		WREG32(mmVCE_CLOCK_GATING_B, 0xe90010);
	} else {
		/* Force CLOCK ON, set {CGC_*_GATE_MODE, CGC_*_SW_GATE} = {1, 0} */
		WREG32(mmVCE_CLOCK_GATING_B, 0x800f1);
	}

	/* Set VCE_UENC_CLOCK_GATING always in dynamic mode {*_FORCE_ON, *_FORCE_OFF} = {0, 0}*/;
	WREG32(mmVCE_UENC_CLOCK_GATING, 0x40);

	/* set VCE_UENC_REG_CLOCK_GATING always in dynamic mode */
	WREG32(mmVCE_UENC_REG_CLOCK_GATING, 0x00);

	WREG32_P(mmVCE_LMI_CTRL2, 0, ~0x100);
	if(!gated) {
		WREG32_P(mmVCE_VCPU_CNTL, VCE_VCPU_CNTL__CLK_EN_MASK, ~VCE_VCPU_CNTL__CLK_EN_MASK);
		mdelay(100);
		WREG32_P(mmVCE_SOFT_RESET, 0, ~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);

		vce_v2_0_firmware_loaded(adev);
		WREG32_P(mmVCE_STATUS, 0, ~VCE_STATUS__JOB_BUSY_MASK);
	}
}

static void vce_v2_0_disable_cg(struct amdgpu_device *adev)
{
	WREG32(mmVCE_CGTT_CLK_OVERRIDE, 7);
}

static void vce_v2_0_enable_mgcg(struct amdgpu_device *adev, bool enable)
{
	bool sw_cg = false;

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
	uint64_t addr = adev->vce.gpu_addr;
	uint32_t size;

	WREG32_P(mmVCE_CLOCK_GATING_A, 0, ~(1 << 16));
	WREG32_P(mmVCE_UENC_CLOCK_GATING, 0x1FF000, ~0xFF9FF000);
	WREG32_P(mmVCE_UENC_REG_CLOCK_GATING, 0x3F, ~0x3F);
	WREG32(mmVCE_CLOCK_GATING_B, 0xf7);

	WREG32(mmVCE_LMI_CTRL, 0x00398000);
	WREG32_P(mmVCE_LMI_CACHE_CTRL, 0x0, ~0x1);
	WREG32(mmVCE_LMI_SWAP_CNTL, 0);
	WREG32(mmVCE_LMI_SWAP_CNTL1, 0);
	WREG32(mmVCE_LMI_VM_CTRL, 0);

	addr += AMDGPU_VCE_FIRMWARE_OFFSET;
	size = VCE_V2_0_FW_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET0, addr & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE0, size);

	addr += size;
	size = VCE_V2_0_STACK_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET1, addr & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE1, size);

	addr += size;
	size = VCE_V2_0_DATA_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET2, addr & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE2, size);

	WREG32_P(mmVCE_LMI_CTRL2, 0x0, ~0x100);
	WREG32_FIELD(VCE_SYS_INT_EN, VCE_SYS_INT_TRAP_INTERRUPT_EN, 1);

	vce_v2_0_init_cg(adev);
}

static bool vce_v2_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return !(RREG32(mmSRBM_STATUS2) & SRBM_STATUS2__VCE_BUSY_MASK);
}

static int vce_v2_0_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	unsigned i;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (vce_v2_0_is_idle(handle))
			return 0;
	}
	return -ETIMEDOUT;
}

static int vce_v2_0_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

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
	switch (entry->src_data) {
	case 0:
	case 1:
		amdgpu_fence_process(&adev->vce.ring[entry->src_data]);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data);
		break;
	}

	return 0;
}

static void vce_v2_0_set_bypass_mode(struct amdgpu_device *adev, bool enable)
{
	u32 tmp = RREG32_SMC(ixGCK_DFS_BYPASS_CNTL);

	if (enable)
		tmp |= GCK_DFS_BYPASS_CNTL__BYPASSECLK_MASK;
	else
		tmp &= ~GCK_DFS_BYPASS_CNTL__BYPASSECLK_MASK;

	WREG32_SMC(ixGCK_DFS_BYPASS_CNTL, tmp);
}


static int vce_v2_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	bool gate = false;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;


	vce_v2_0_set_bypass_mode(adev, enable);

	if (state == AMD_CG_STATE_GATE)
		gate = true;

	vce_v2_0_enable_mgcg(adev, gate);

	return 0;
}

static int vce_v2_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	/* This doesn't actually powergate the VCE block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!(adev->pg_flags & AMD_PG_SUPPORT_VCE))
		return 0;

	if (state == AMD_PG_STATE_GATE)
		/* XXX do we need a vce_v2_0_stop()? */
		return 0;
	else
		return vce_v2_0_start(adev);
}

const struct amd_ip_funcs vce_v2_0_ip_funcs = {
	.name = "vce_v2_0",
	.early_init = vce_v2_0_early_init,
	.late_init = NULL,
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
	.get_rptr = vce_v2_0_ring_get_rptr,
	.get_wptr = vce_v2_0_ring_get_wptr,
	.set_wptr = vce_v2_0_ring_set_wptr,
	.parse_cs = amdgpu_vce_ring_parse_cs,
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

	for (i = 0; i < adev->vce.num_rings; i++)
		adev->vce.ring[i].funcs = &vce_v2_0_ring_funcs;
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

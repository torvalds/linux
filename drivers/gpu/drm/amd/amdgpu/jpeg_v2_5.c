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

#include "amdgpu.h"
#include "amdgpu_jpeg.h"
#include "soc15.h"
#include "soc15d.h"
#include "jpeg_v2_0.h"

#include "vcn/vcn_2_5_offset.h"
#include "vcn/vcn_2_5_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

#define mmUVD_JPEG_PITCH_INTERNAL_OFFSET			0x401f

#define JPEG25_MAX_HW_INSTANCES_ARCTURUS			2

static void jpeg_v2_5_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v2_5_set_irq_funcs(struct amdgpu_device *adev);
static int jpeg_v2_5_set_powergating_state(void *handle,
				enum amd_powergating_state state);

static int amdgpu_ih_clientid_jpeg[] = {
	SOC15_IH_CLIENTID_VCN,
	SOC15_IH_CLIENTID_VCN1
};

/**
 * jpeg_v2_5_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int jpeg_v2_5_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 harvest;
	int i;

	adev->jpeg.num_jpeg_inst = JPEG25_MAX_HW_INSTANCES_ARCTURUS;
	for (i = 0; i < adev->jpeg.num_jpeg_inst; i++) {
		harvest = RREG32_SOC15(JPEG, i, mmCC_UVD_HARVESTING);
		if (harvest & CC_UVD_HARVESTING__UVD_DISABLE_MASK)
			adev->jpeg.harvest_config |= 1 << i;
	}
	if (adev->jpeg.harvest_config == (AMDGPU_JPEG_HARVEST_JPEG0 |
					 AMDGPU_JPEG_HARVEST_JPEG1))
		return -ENOENT;

	jpeg_v2_5_set_dec_ring_funcs(adev);
	jpeg_v2_5_set_irq_funcs(adev);

	return 0;
}

/**
 * jpeg_v2_5_sw_init - sw init for JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int jpeg_v2_5_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int i, r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		/* JPEG TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_jpeg[i],
				VCN_2_0__SRCID__JPEG_DECODE, &adev->jpeg.inst[i].irq);
		if (r)
			return r;
	}

	r = amdgpu_jpeg_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_jpeg_resume(adev);
	if (r)
		return r;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		ring = &adev->jpeg.inst[i].ring_dec;
		ring->use_doorbell = true;
		ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 1 + 8 * i;
		sprintf(ring->name, "jpeg_dec_%d", i);
		r = amdgpu_ring_init(adev, ring, 512, &adev->jpeg.inst[i].irq,
				     0, AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;

		adev->jpeg.internal.jpeg_pitch = mmUVD_JPEG_PITCH_INTERNAL_OFFSET;
		adev->jpeg.inst[i].external.jpeg_pitch = SOC15_REG_OFFSET(JPEG, i, mmUVD_JPEG_PITCH);
	}

	return 0;
}

/**
 * jpeg_v2_5_sw_fini - sw fini for JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 * JPEG suspend and free up sw allocation
 */
static int jpeg_v2_5_sw_fini(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_jpeg_suspend(adev);
	if (r)
		return r;

	r = amdgpu_jpeg_sw_fini(adev);

	return r;
}

/**
 * jpeg_v2_5_hw_init - start and test JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 */
static int jpeg_v2_5_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i, r;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		ring = &adev->jpeg.inst[i].ring_dec;
		adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
			(adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 8 * i, i);

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;
	}

	DRM_INFO("JPEG decode initialized successfully.\n");

	return 0;
}

/**
 * jpeg_v2_5_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the JPEG block, mark ring as not ready any more
 */
static int jpeg_v2_5_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		ring = &adev->jpeg.inst[i].ring_dec;
		if (adev->jpeg.cur_state != AMD_PG_STATE_GATE &&
		      RREG32_SOC15(JPEG, i, mmUVD_JRBC_STATUS))
			jpeg_v2_5_set_powergating_state(adev, AMD_PG_STATE_GATE);

		ring->sched.ready = false;
	}

	return 0;
}

/**
 * jpeg_v2_5_suspend - suspend JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend JPEG block
 */
static int jpeg_v2_5_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = jpeg_v2_5_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_jpeg_suspend(adev);

	return r;
}

/**
 * jpeg_v2_5_resume - resume JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init JPEG block
 */
static int jpeg_v2_5_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_jpeg_resume(adev);
	if (r)
		return r;

	r = jpeg_v2_5_hw_init(adev);

	return r;
}

static void jpeg_v2_5_disable_clock_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	data = RREG32_SOC15(JPEG, inst, mmJPEG_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_JPEG_MGCG)
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data &= ~JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(JPEG, inst, mmJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(JPEG, inst, mmJPEG_CGC_GATE);
	data &= ~(JPEG_CGC_GATE__JPEG_DEC_MASK
		| JPEG_CGC_GATE__JPEG2_DEC_MASK
		| JPEG_CGC_GATE__JMCIF_MASK
		| JPEG_CGC_GATE__JRBBM_MASK);
	WREG32_SOC15(JPEG, inst, mmJPEG_CGC_GATE, data);

	data = RREG32_SOC15(JPEG, inst, mmJPEG_CGC_CTRL);
	data &= ~(JPEG_CGC_CTRL__JPEG_DEC_MODE_MASK
		| JPEG_CGC_CTRL__JPEG2_DEC_MODE_MASK
		| JPEG_CGC_CTRL__JMCIF_MODE_MASK
		| JPEG_CGC_CTRL__JRBBM_MODE_MASK);
	WREG32_SOC15(JPEG, inst, mmJPEG_CGC_CTRL, data);
}

static void jpeg_v2_5_enable_clock_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	data = RREG32_SOC15(JPEG, inst, mmJPEG_CGC_GATE);
	data |= (JPEG_CGC_GATE__JPEG_DEC_MASK
		|JPEG_CGC_GATE__JPEG2_DEC_MASK
		|JPEG_CGC_GATE__JPEG_ENC_MASK
		|JPEG_CGC_GATE__JMCIF_MASK
		|JPEG_CGC_GATE__JRBBM_MASK);
	WREG32_SOC15(JPEG, inst, mmJPEG_CGC_GATE, data);
}

/**
 * jpeg_v2_5_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
static int jpeg_v2_5_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		ring = &adev->jpeg.inst[i].ring_dec;
		/* disable anti hang mechanism */
		WREG32_P(SOC15_REG_OFFSET(JPEG, i, mmUVD_JPEG_POWER_STATUS), 0,
			~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

		/* JPEG disable CGC */
		jpeg_v2_5_disable_clock_gating(adev, i);

		/* MJPEG global tiling registers */
		WREG32_SOC15(JPEG, i, mmJPEG_DEC_GFX8_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);
		WREG32_SOC15(JPEG, i, mmJPEG_DEC_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);

		/* enable JMI channel */
		WREG32_P(SOC15_REG_OFFSET(JPEG, i, mmUVD_JMI_CNTL), 0,
			~UVD_JMI_CNTL__SOFT_RESET_MASK);

		/* enable System Interrupt for JRBC */
		WREG32_P(SOC15_REG_OFFSET(JPEG, i, mmJPEG_SYS_INT_EN),
			JPEG_SYS_INT_EN__DJRBC_MASK,
			~JPEG_SYS_INT_EN__DJRBC_MASK);

		WREG32_SOC15(JPEG, i, mmUVD_LMI_JRBC_RB_VMID, 0);
		WREG32_SOC15(JPEG, i, mmUVD_JRBC_RB_CNTL, (0x00000001L | 0x00000002L));
		WREG32_SOC15(JPEG, i, mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW,
			lower_32_bits(ring->gpu_addr));
		WREG32_SOC15(JPEG, i, mmUVD_LMI_JRBC_RB_64BIT_BAR_HIGH,
			upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(JPEG, i, mmUVD_JRBC_RB_RPTR, 0);
		WREG32_SOC15(JPEG, i, mmUVD_JRBC_RB_WPTR, 0);
		WREG32_SOC15(JPEG, i, mmUVD_JRBC_RB_CNTL, 0x00000002L);
		WREG32_SOC15(JPEG, i, mmUVD_JRBC_RB_SIZE, ring->ring_size / 4);
		ring->wptr = RREG32_SOC15(JPEG, i, mmUVD_JRBC_RB_WPTR);
	}

	return 0;
}

/**
 * jpeg_v2_5_stop - stop JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the JPEG block
 */
static int jpeg_v2_5_stop(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		/* reset JMI */
		WREG32_P(SOC15_REG_OFFSET(JPEG, i, mmUVD_JMI_CNTL),
			UVD_JMI_CNTL__SOFT_RESET_MASK,
			~UVD_JMI_CNTL__SOFT_RESET_MASK);

		jpeg_v2_5_enable_clock_gating(adev, i);

		/* enable anti hang mechanism */
		WREG32_P(SOC15_REG_OFFSET(JPEG, i, mmUVD_JPEG_POWER_STATUS),
			UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK,
			~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);
	}

	return 0;
}

/**
 * jpeg_v2_5_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v2_5_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(JPEG, ring->me, mmUVD_JRBC_RB_RPTR);
}

/**
 * jpeg_v2_5_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v2_5_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];
	else
		return RREG32_SOC15(JPEG, ring->me, mmUVD_JRBC_RB_WPTR);
}

/**
 * jpeg_v2_5_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v2_5_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(JPEG, ring->me, mmUVD_JRBC_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static bool jpeg_v2_5_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 1;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		ret &= (((RREG32_SOC15(JPEG, i, mmUVD_JRBC_STATUS) &
			UVD_JRBC_STATUS__RB_JOB_DONE_MASK) ==
			UVD_JRBC_STATUS__RB_JOB_DONE_MASK));
	}

	return ret;
}

static int jpeg_v2_5_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		ret = SOC15_WAIT_ON_RREG(JPEG, i, mmUVD_JRBC_STATUS,
			UVD_JRBC_STATUS__RB_JOB_DONE_MASK,
			UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
		if (ret)
			return ret;
	}

	return 0;
}

static int jpeg_v2_5_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE);
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		if (enable) {
			if (!jpeg_v2_5_is_idle(handle))
				return -EBUSY;
			jpeg_v2_5_enable_clock_gating(adev, i);
		} else {
			jpeg_v2_5_disable_clock_gating(adev, i);
		}
	}

	return 0;
}

static int jpeg_v2_5_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	if(state == adev->jpeg.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = jpeg_v2_5_stop(adev);
	else
		ret = jpeg_v2_5_start(adev);

	if(!ret)
		adev->jpeg.cur_state = state;

	return ret;
}

static int jpeg_v2_5_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v2_5_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t ip_instance;

	switch (entry->client_id) {
	case SOC15_IH_CLIENTID_VCN:
		ip_instance = 0;
		break;
	case SOC15_IH_CLIENTID_VCN1:
		ip_instance = 1;
		break;
	default:
		DRM_ERROR("Unhandled client id: %d\n", entry->client_id);
		return 0;
	}

	DRM_DEBUG("IH: JPEG TRAP\n");

	switch (entry->src_id) {
	case VCN_2_0__SRCID__JPEG_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[ip_instance].ring_dec);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amd_ip_funcs jpeg_v2_5_ip_funcs = {
	.name = "jpeg_v2_5",
	.early_init = jpeg_v2_5_early_init,
	.late_init = NULL,
	.sw_init = jpeg_v2_5_sw_init,
	.sw_fini = jpeg_v2_5_sw_fini,
	.hw_init = jpeg_v2_5_hw_init,
	.hw_fini = jpeg_v2_5_hw_fini,
	.suspend = jpeg_v2_5_suspend,
	.resume = jpeg_v2_5_resume,
	.is_idle = jpeg_v2_5_is_idle,
	.wait_for_idle = jpeg_v2_5_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = jpeg_v2_5_set_clockgating_state,
	.set_powergating_state = jpeg_v2_5_set_powergating_state,
};

static const struct amd_ip_funcs jpeg_v2_6_ip_funcs = {
	.name = "jpeg_v2_6",
	.early_init = jpeg_v2_5_early_init,
	.late_init = NULL,
	.sw_init = jpeg_v2_5_sw_init,
	.sw_fini = jpeg_v2_5_sw_fini,
	.hw_init = jpeg_v2_5_hw_init,
	.hw_fini = jpeg_v2_5_hw_fini,
	.suspend = jpeg_v2_5_suspend,
	.resume = jpeg_v2_5_resume,
	.is_idle = jpeg_v2_5_is_idle,
	.wait_for_idle = jpeg_v2_5_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = jpeg_v2_5_set_clockgating_state,
	.set_powergating_state = jpeg_v2_5_set_powergating_state,
};

static const struct amdgpu_ring_funcs jpeg_v2_5_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.vmhub = AMDGPU_MMHUB_1,
	.get_rptr = jpeg_v2_5_dec_ring_get_rptr,
	.get_wptr = jpeg_v2_5_dec_ring_get_wptr,
	.set_wptr = jpeg_v2_5_dec_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v2_5_dec_ring_emit_vm_flush */
		18 + 18 + /* jpeg_v2_5_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 22, /* jpeg_v2_5_dec_ring_emit_ib */
	.emit_ib = jpeg_v2_0_dec_ring_emit_ib,
	.emit_fence = jpeg_v2_0_dec_ring_emit_fence,
	.emit_vm_flush = jpeg_v2_0_dec_ring_emit_vm_flush,
	.test_ring = amdgpu_jpeg_dec_ring_test_ring,
	.test_ib = amdgpu_jpeg_dec_ring_test_ib,
	.insert_nop = jpeg_v2_0_dec_ring_nop,
	.insert_start = jpeg_v2_0_dec_ring_insert_start,
	.insert_end = jpeg_v2_0_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_jpeg_ring_begin_use,
	.end_use = amdgpu_jpeg_ring_end_use,
	.emit_wreg = jpeg_v2_0_dec_ring_emit_wreg,
	.emit_reg_wait = jpeg_v2_0_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static const struct amdgpu_ring_funcs jpeg_v2_6_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = jpeg_v2_5_dec_ring_get_rptr,
	.get_wptr = jpeg_v2_5_dec_ring_get_wptr,
	.set_wptr = jpeg_v2_5_dec_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v2_5_dec_ring_emit_vm_flush */
		18 + 18 + /* jpeg_v2_5_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 22, /* jpeg_v2_5_dec_ring_emit_ib */
	.emit_ib = jpeg_v2_0_dec_ring_emit_ib,
	.emit_fence = jpeg_v2_0_dec_ring_emit_fence,
	.emit_vm_flush = jpeg_v2_0_dec_ring_emit_vm_flush,
	.test_ring = amdgpu_jpeg_dec_ring_test_ring,
	.test_ib = amdgpu_jpeg_dec_ring_test_ib,
	.insert_nop = jpeg_v2_0_dec_ring_nop,
	.insert_start = jpeg_v2_0_dec_ring_insert_start,
	.insert_end = jpeg_v2_0_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_jpeg_ring_begin_use,
	.end_use = amdgpu_jpeg_ring_end_use,
	.emit_wreg = jpeg_v2_0_dec_ring_emit_wreg,
	.emit_reg_wait = jpeg_v2_0_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void jpeg_v2_5_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;
		if (adev->asic_type == CHIP_ARCTURUS)
			adev->jpeg.inst[i].ring_dec.funcs = &jpeg_v2_5_dec_ring_vm_funcs;
		else  /* CHIP_ALDEBARAN */
			adev->jpeg.inst[i].ring_dec.funcs = &jpeg_v2_6_dec_ring_vm_funcs;
		adev->jpeg.inst[i].ring_dec.me = i;
		DRM_INFO("JPEG(%d) JPEG decode is enabled in VM mode\n", i);
	}
}

static const struct amdgpu_irq_src_funcs jpeg_v2_5_irq_funcs = {
	.set = jpeg_v2_5_set_interrupt_state,
	.process = jpeg_v2_5_process_interrupt,
};

static void jpeg_v2_5_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (adev->jpeg.harvest_config & (1 << i))
			continue;

		adev->jpeg.inst[i].irq.num_types = 1;
		adev->jpeg.inst[i].irq.funcs = &jpeg_v2_5_irq_funcs;
	}
}

const struct amdgpu_ip_block_version jpeg_v2_5_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_JPEG,
		.major = 2,
		.minor = 5,
		.rev = 0,
		.funcs = &jpeg_v2_5_ip_funcs,
};

const struct amdgpu_ip_block_version jpeg_v2_6_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_JPEG,
		.major = 2,
		.minor = 6,
		.rev = 0,
		.funcs = &jpeg_v2_6_ip_funcs,
};

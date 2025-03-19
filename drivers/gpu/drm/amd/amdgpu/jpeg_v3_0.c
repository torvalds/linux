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
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "jpeg_v2_0.h"

#include "vcn/vcn_3_0_0_offset.h"
#include "vcn/vcn_3_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

#define mmUVD_JPEG_PITCH_INTERNAL_OFFSET	0x401f

static const struct amdgpu_hwip_reg_entry jpeg_reg_list_3_0[] = {
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JPEG_POWER_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JPEG_INT_STAT),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JRBC_RB_CNTL),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JRBC_RB_SIZE),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmJPEG_DEC_ADDR_MODE),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmJPEG_DEC_GFX10_ADDR_CONFIG),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmJPEG_DEC_Y_GFX10_TILING_SURFACE),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmJPEG_DEC_UV_GFX10_TILING_SURFACE),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JPEG_PITCH),
	SOC15_REG_ENTRY_STR(JPEG, 0, mmUVD_JPEG_UV_PITCH),
};

static void jpeg_v3_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v3_0_set_irq_funcs(struct amdgpu_device *adev);
static int jpeg_v3_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
				enum amd_powergating_state state);

/**
 * jpeg_v3_0_early_init - set function pointers
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 */
static int jpeg_v3_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	u32 harvest;

	switch (amdgpu_ip_version(adev, UVD_HWIP, 0)) {
	case IP_VERSION(3, 1, 1):
	case IP_VERSION(3, 1, 2):
		break;
	default:
		harvest = RREG32_SOC15(JPEG, 0, mmCC_UVD_HARVESTING);
		if (harvest & CC_UVD_HARVESTING__UVD_DISABLE_MASK)
			return -ENOENT;
		break;
	}

	adev->jpeg.num_jpeg_inst = 1;
	adev->jpeg.num_jpeg_rings = 1;

	jpeg_v3_0_set_dec_ring_funcs(adev);
	jpeg_v3_0_set_irq_funcs(adev);

	return 0;
}

/**
 * jpeg_v3_0_sw_init - sw init for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int jpeg_v3_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int r;

	/* JPEG TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
		VCN_2_0__SRCID__JPEG_DECODE, &adev->jpeg.inst->irq);
	if (r)
		return r;

	r = amdgpu_jpeg_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_jpeg_resume(adev);
	if (r)
		return r;

	ring = adev->jpeg.inst->ring_dec;
	ring->use_doorbell = true;
	ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 1;
	ring->vm_hub = AMDGPU_MMHUB0(0);
	sprintf(ring->name, "jpeg_dec");
	r = amdgpu_ring_init(adev, ring, 512, &adev->jpeg.inst->irq, 0,
			     AMDGPU_RING_PRIO_DEFAULT, NULL);
	if (r)
		return r;

	adev->jpeg.internal.jpeg_pitch[0] = mmUVD_JPEG_PITCH_INTERNAL_OFFSET;
	adev->jpeg.inst->external.jpeg_pitch[0] = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_PITCH);

	r = amdgpu_jpeg_reg_dump_init(adev, jpeg_reg_list_3_0, ARRAY_SIZE(jpeg_reg_list_3_0));
	if (r)
		return r;

	adev->jpeg.supported_reset = AMDGPU_RESET_TYPE_PER_QUEUE;
	r = amdgpu_jpeg_sysfs_reset_mask_init(adev);

	return r;
}

/**
 * jpeg_v3_0_sw_fini - sw fini for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * JPEG suspend and free up sw allocation
 */
static int jpeg_v3_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_jpeg_suspend(adev);
	if (r)
		return r;

	amdgpu_jpeg_sysfs_reset_mask_fini(adev);

	r = amdgpu_jpeg_sw_fini(adev);

	return r;
}

/**
 * jpeg_v3_0_hw_init - start and test JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int jpeg_v3_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring = adev->jpeg.inst->ring_dec;

	adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
		(adev->doorbell_index.vcn.vcn_ring0_1 << 1), 0);

	return amdgpu_ring_test_helper(ring);
}

/**
 * jpeg_v3_0_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the JPEG block, mark ring as not ready any more
 */
static int jpeg_v3_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	if (adev->jpeg.cur_state != AMD_PG_STATE_GATE &&
	      RREG32_SOC15(JPEG, 0, mmUVD_JRBC_STATUS))
		jpeg_v3_0_set_powergating_state(ip_block, AMD_PG_STATE_GATE);

	return 0;
}

/**
 * jpeg_v3_0_suspend - suspend JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend JPEG block
 */
static int jpeg_v3_0_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = jpeg_v3_0_hw_fini(ip_block);
	if (r)
		return r;

	r = amdgpu_jpeg_suspend(ip_block->adev);

	return r;
}

/**
 * jpeg_v3_0_resume - resume JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init JPEG block
 */
static int jpeg_v3_0_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = amdgpu_jpeg_resume(ip_block->adev);
	if (r)
		return r;

	r = jpeg_v3_0_hw_init(ip_block);

	return r;
}

static void jpeg_v3_0_disable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	data = RREG32_SOC15(JPEG, 0, mmJPEG_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_JPEG_MGCG)
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data &= ~JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(JPEG, 0, mmJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(JPEG, 0, mmJPEG_CGC_GATE);
	data &= ~(JPEG_CGC_GATE__JPEG_DEC_MASK
		| JPEG_CGC_GATE__JPEG2_DEC_MASK
		| JPEG_CGC_GATE__JPEG_ENC_MASK
		| JPEG_CGC_GATE__JMCIF_MASK
		| JPEG_CGC_GATE__JRBBM_MASK);
	WREG32_SOC15(JPEG, 0, mmJPEG_CGC_GATE, data);

	data = RREG32_SOC15(JPEG, 0, mmJPEG_CGC_CTRL);
	data &= ~(JPEG_CGC_CTRL__JPEG_DEC_MODE_MASK
		| JPEG_CGC_CTRL__JPEG2_DEC_MODE_MASK
		| JPEG_CGC_CTRL__JMCIF_MODE_MASK
		| JPEG_CGC_CTRL__JRBBM_MODE_MASK);
	WREG32_SOC15(JPEG, 0, mmJPEG_CGC_CTRL, data);
}

static void jpeg_v3_0_enable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	data = RREG32_SOC15(JPEG, 0, mmJPEG_CGC_GATE);
	data |= (JPEG_CGC_GATE__JPEG_DEC_MASK
		|JPEG_CGC_GATE__JPEG2_DEC_MASK
		|JPEG_CGC_GATE__JPEG_ENC_MASK
		|JPEG_CGC_GATE__JMCIF_MASK
		|JPEG_CGC_GATE__JRBBM_MASK);
	WREG32_SOC15(JPEG, 0, mmJPEG_CGC_GATE, data);
}

static int jpeg_v3_0_disable_static_power_gating(struct amdgpu_device *adev)
{
	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG) {
		uint32_t data = 0;
		int r = 0;

		data = 1 << UVD_PGFSM_CONFIG__UVDJ_PWR_CONFIG__SHIFT;
		WREG32(SOC15_REG_OFFSET(JPEG, 0, mmUVD_PGFSM_CONFIG), data);

		r = SOC15_WAIT_ON_RREG(JPEG, 0,
			mmUVD_PGFSM_STATUS, UVD_PGFSM_STATUS_UVDJ_PWR_ON,
			UVD_PGFSM_STATUS__UVDJ_PWR_STATUS_MASK);

		if (r) {
			DRM_ERROR("amdgpu: JPEG disable power gating failed\n");
			return r;
		}
	}

	/* disable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS), 0,
		~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

	/* keep the JPEG in static PG mode */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS), 0,
		~UVD_JPEG_POWER_STATUS__JPEG_PG_MODE_MASK);

	return 0;
}

static int jpeg_v3_0_enable_static_power_gating(struct amdgpu_device *adev)
{
	/* enable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS),
		UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK,
		~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG) {
		uint32_t data = 0;
		int r = 0;

		data = 2 << UVD_PGFSM_CONFIG__UVDJ_PWR_CONFIG__SHIFT;
		WREG32(SOC15_REG_OFFSET(JPEG, 0, mmUVD_PGFSM_CONFIG), data);

		r = SOC15_WAIT_ON_RREG(JPEG, 0, mmUVD_PGFSM_STATUS,
			(2 << UVD_PGFSM_STATUS__UVDJ_PWR_STATUS__SHIFT),
			UVD_PGFSM_STATUS__UVDJ_PWR_STATUS_MASK);

		if (r) {
			DRM_ERROR("amdgpu: JPEG enable power gating failed\n");
			return r;
		}
	}

	return 0;
}

/**
 * jpeg_v3_0_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
static int jpeg_v3_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = adev->jpeg.inst->ring_dec;
	int r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_jpeg(adev, true);

	/* disable power gating */
	r = jpeg_v3_0_disable_static_power_gating(adev);
	if (r)
		return r;

	/* JPEG disable CGC */
	jpeg_v3_0_disable_clock_gating(adev);

	/* MJPEG global tiling registers */
	WREG32_SOC15(JPEG, 0, mmJPEG_DEC_GFX10_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);
	WREG32_SOC15(JPEG, 0, mmJPEG_ENC_GFX10_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);

	/* enable JMI channel */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JMI_CNTL), 0,
		~UVD_JMI_CNTL__SOFT_RESET_MASK);

	/* enable System Interrupt for JRBC */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmJPEG_SYS_INT_EN),
		JPEG_SYS_INT_EN__DJRBC_MASK,
		~JPEG_SYS_INT_EN__DJRBC_MASK);

	WREG32_SOC15(JPEG, 0, mmUVD_LMI_JRBC_RB_VMID, 0);
	WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_CNTL, (0x00000001L | 0x00000002L));
	WREG32_SOC15(JPEG, 0, mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(JPEG, 0, mmUVD_LMI_JRBC_RB_64BIT_BAR_HIGH,
		upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_RPTR, 0);
	WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR, 0);
	WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_CNTL, 0x00000002L);
	WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_SIZE, ring->ring_size / 4);
	ring->wptr = RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR);

	return 0;
}

/**
 * jpeg_v3_0_stop - stop JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the JPEG block
 */
static int jpeg_v3_0_stop(struct amdgpu_device *adev)
{
	int r;

	/* reset JMI */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JMI_CNTL),
		UVD_JMI_CNTL__SOFT_RESET_MASK,
		~UVD_JMI_CNTL__SOFT_RESET_MASK);

	jpeg_v3_0_enable_clock_gating(adev);

	/* enable power gating */
	r = jpeg_v3_0_enable_static_power_gating(adev);
	if (r)
		return r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_jpeg(adev, false);

	return 0;
}

/**
 * jpeg_v3_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v3_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_RPTR);
}

/**
 * jpeg_v3_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v3_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR);
}

/**
 * jpeg_v3_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v3_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static bool jpeg_v3_0_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret = 1;

	ret &= (((RREG32_SOC15(JPEG, 0, mmUVD_JRBC_STATUS) &
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK) ==
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK));

	return ret;
}

static int jpeg_v3_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	return SOC15_WAIT_ON_RREG(JPEG, 0, mmUVD_JRBC_STATUS,
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK,
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
}

static int jpeg_v3_0_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = state == AMD_CG_STATE_GATE;

	if (enable) {
		if (!jpeg_v3_0_is_idle(ip_block))
			return -EBUSY;
		jpeg_v3_0_enable_clock_gating(adev);
	} else {
		jpeg_v3_0_disable_clock_gating(adev);
	}

	return 0;
}

static int jpeg_v3_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret;

	if(state == adev->jpeg.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = jpeg_v3_0_stop(adev);
	else
		ret = jpeg_v3_0_start(adev);

	if(!ret)
		adev->jpeg.cur_state = state;

	return ret;
}

static int jpeg_v3_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v3_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: JPEG TRAP\n");

	switch (entry->src_id) {
	case VCN_2_0__SRCID__JPEG_DECODE:
		amdgpu_fence_process(adev->jpeg.inst->ring_dec);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static int jpeg_v3_0_ring_reset(struct amdgpu_ring *ring, unsigned int vmid)
{
	jpeg_v3_0_stop(ring->adev);
	jpeg_v3_0_start(ring->adev);
	return amdgpu_ring_test_helper(ring);
}

static const struct amd_ip_funcs jpeg_v3_0_ip_funcs = {
	.name = "jpeg_v3_0",
	.early_init = jpeg_v3_0_early_init,
	.sw_init = jpeg_v3_0_sw_init,
	.sw_fini = jpeg_v3_0_sw_fini,
	.hw_init = jpeg_v3_0_hw_init,
	.hw_fini = jpeg_v3_0_hw_fini,
	.suspend = jpeg_v3_0_suspend,
	.resume = jpeg_v3_0_resume,
	.is_idle = jpeg_v3_0_is_idle,
	.wait_for_idle = jpeg_v3_0_wait_for_idle,
	.set_clockgating_state = jpeg_v3_0_set_clockgating_state,
	.set_powergating_state = jpeg_v3_0_set_powergating_state,
	.dump_ip_state = amdgpu_jpeg_dump_ip_state,
	.print_ip_state = amdgpu_jpeg_print_ip_state,
};

static const struct amdgpu_ring_funcs jpeg_v3_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.get_rptr = jpeg_v3_0_dec_ring_get_rptr,
	.get_wptr = jpeg_v3_0_dec_ring_get_wptr,
	.set_wptr = jpeg_v3_0_dec_ring_set_wptr,
	.parse_cs = jpeg_v2_dec_ring_parse_cs,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v3_0_dec_ring_emit_vm_flush */
		18 + 18 + /* jpeg_v3_0_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 22, /* jpeg_v3_0_dec_ring_emit_ib */
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
	.reset = jpeg_v3_0_ring_reset,
};

static void jpeg_v3_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->ring_dec->funcs = &jpeg_v3_0_dec_ring_vm_funcs;
}

static const struct amdgpu_irq_src_funcs jpeg_v3_0_irq_funcs = {
	.set = jpeg_v3_0_set_interrupt_state,
	.process = jpeg_v3_0_process_interrupt,
};

static void jpeg_v3_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->irq.num_types = 1;
	adev->jpeg.inst->irq.funcs = &jpeg_v3_0_irq_funcs;
}

const struct amdgpu_ip_block_version jpeg_v3_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_JPEG,
	.major = 3,
	.minor = 0,
	.rev = 0,
	.funcs = &jpeg_v3_0_ip_funcs,
};

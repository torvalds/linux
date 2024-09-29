/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "jpeg_v4_0_3.h"

#include "vcn/vcn_5_0_0_offset.h"
#include "vcn/vcn_5_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_4_0.h"
#include "jpeg_v5_0_0.h"

static void jpeg_v5_0_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v5_0_0_set_irq_funcs(struct amdgpu_device *adev);
static int jpeg_v5_0_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
				enum amd_powergating_state state);

/**
 * jpeg_v5_0_0_early_init - set function pointers
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 */
static int jpeg_v5_0_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->jpeg.num_jpeg_inst = 1;
	adev->jpeg.num_jpeg_rings = 1;

	jpeg_v5_0_0_set_dec_ring_funcs(adev);
	jpeg_v5_0_0_set_irq_funcs(adev);

	return 0;
}

/**
 * jpeg_v5_0_0_sw_init - sw init for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int jpeg_v5_0_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int r;

	/* JPEG TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
		VCN_4_0__SRCID__JPEG_DECODE, &adev->jpeg.inst->irq);
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

	adev->jpeg.internal.jpeg_pitch[0] = regUVD_JPEG_PITCH_INTERNAL_OFFSET;
	adev->jpeg.inst->external.jpeg_pitch[0] = SOC15_REG_OFFSET(JPEG, 0, regUVD_JPEG_PITCH);

	/* TODO: Add queue reset mask when FW fully supports it */
	adev->jpeg.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->jpeg.inst[0].ring_dec[0]);
	r = amdgpu_jpeg_sysfs_reset_mask_init(adev);
	if (r)
		return r;
	return 0;
}

/**
 * jpeg_v5_0_0_sw_fini - sw fini for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * JPEG suspend and free up sw allocation
 */
static int jpeg_v5_0_0_sw_fini(struct amdgpu_ip_block *ip_block)
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
 * jpeg_v5_0_0_hw_init - start and test JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int jpeg_v5_0_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring = adev->jpeg.inst->ring_dec;
	int r;

	adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
			(adev->doorbell_index.vcn.vcn_ring0_1 << 1), 0);

	/* Skip ring test because pause DPG is not implemented. */
	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG_DPG)
		return 0;

	r = amdgpu_ring_test_helper(ring);
	if (r)
		return r;

	return 0;
}

/**
 * jpeg_v5_0_0_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the JPEG block, mark ring as not ready any more
 */
static int jpeg_v5_0_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	if (adev->jpeg.cur_state != AMD_PG_STATE_GATE &&
	      RREG32_SOC15(JPEG, 0, regUVD_JRBC_STATUS))
		jpeg_v5_0_0_set_powergating_state(ip_block, AMD_PG_STATE_GATE);

	return 0;
}

/**
 * jpeg_v5_0_0_suspend - suspend JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend JPEG block
 */
static int jpeg_v5_0_0_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = jpeg_v5_0_0_hw_fini(ip_block);
	if (r)
		return r;

	r = amdgpu_jpeg_suspend(ip_block->adev);

	return r;
}

/**
 * jpeg_v5_0_0_resume - resume JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init JPEG block
 */
static int jpeg_v5_0_0_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = amdgpu_jpeg_resume(ip_block->adev);
	if (r)
		return r;

	r = jpeg_v5_0_0_hw_init(ip_block);

	return r;
}

static void jpeg_v5_0_0_disable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	WREG32_SOC15(JPEG, 0, regJPEG_CGC_GATE, data);

	data = RREG32_SOC15(JPEG, 0, regJPEG_CGC_CTRL);
	data &= ~(JPEG_CGC_CTRL__JPEG0_DEC_MODE_MASK
		| JPEG_CGC_CTRL__JPEG_ENC_MODE_MASK);
	WREG32_SOC15(JPEG, 0, regJPEG_CGC_CTRL, data);
}

static void jpeg_v5_0_0_enable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	data = RREG32_SOC15(JPEG, 0, regJPEG_CGC_CTRL);

	data |= 1 << JPEG_CGC_CTRL__JPEG0_DEC_MODE__SHIFT;
	WREG32_SOC15(JPEG, 0, regJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(JPEG, 0, regJPEG_CGC_GATE);
	data |= (JPEG_CGC_GATE__JPEG0_DEC_MASK
		|JPEG_CGC_GATE__JPEG_ENC_MASK
		|JPEG_CGC_GATE__JMCIF_MASK
		|JPEG_CGC_GATE__JRBBM_MASK);
	WREG32_SOC15(JPEG, 0, regJPEG_CGC_GATE, data);
}

static int jpeg_v5_0_0_disable_power_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	data = 1 << UVD_IPX_DLDO_CONFIG__ONO1_PWR_CONFIG__SHIFT;
	WREG32_SOC15(JPEG, 0, regUVD_IPX_DLDO_CONFIG, data);
	SOC15_WAIT_ON_RREG(JPEG, 0, regUVD_IPX_DLDO_STATUS, 0,
			UVD_IPX_DLDO_STATUS__ONO1_PWR_STATUS_MASK);

	/* disable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, regUVD_JPEG_POWER_STATUS), 0,
		~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

	return 0;
}

static int jpeg_v5_0_0_enable_power_gating(struct amdgpu_device *adev)
{
	/* enable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, regUVD_JPEG_POWER_STATUS),
		UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK,
		~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG) {
		WREG32(SOC15_REG_OFFSET(JPEG, 0, regUVD_IPX_DLDO_CONFIG),
			2 << UVD_IPX_DLDO_CONFIG__ONO1_PWR_CONFIG__SHIFT);
		SOC15_WAIT_ON_RREG(JPEG, 0, regUVD_IPX_DLDO_STATUS,
			1 << UVD_IPX_DLDO_STATUS__ONO1_PWR_STATUS__SHIFT,
			UVD_IPX_DLDO_STATUS__ONO1_PWR_STATUS_MASK);
	}

	return 0;
}

static void jpeg_engine_5_0_0_dpg_clock_gating_mode(struct amdgpu_device *adev,
	       int inst_idx, uint8_t indirect)
{
	uint32_t data = 0;

	// JPEG disable CGC
	if (adev->cg_flags & AMD_CG_SUPPORT_JPEG_MGCG)
		data = 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data = 0 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;

	if (indirect) {
		ADD_SOC24_JPEG_TO_DPG_SRAM(inst_idx, vcnipJPEG_CGC_CTRL, data, indirect);

		// Turn on All JPEG clocks
		data = 0;
		ADD_SOC24_JPEG_TO_DPG_SRAM(inst_idx, vcnipJPEG_CGC_GATE, data, indirect);
	} else {
		WREG32_SOC24_JPEG_DPG_MODE(inst_idx, vcnipJPEG_CGC_CTRL, data, indirect);

		// Turn on All JPEG clocks
		data = 0;
		WREG32_SOC24_JPEG_DPG_MODE(inst_idx, vcnipJPEG_CGC_GATE, data, indirect);
	}
}

/**
 * jpeg_v5_0_0_start_dpg_mode - Jpeg start with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Start JPEG block with dpg mode
 */
static int jpeg_v5_0_0_start_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	struct amdgpu_ring *ring = adev->jpeg.inst[inst_idx].ring_dec;
	uint32_t reg_data = 0;

	jpeg_v5_0_0_enable_power_gating(adev);

	// enable dynamic power gating mode
	reg_data = RREG32_SOC15(JPEG, inst_idx, regUVD_JPEG_POWER_STATUS);
	reg_data |= UVD_JPEG_POWER_STATUS__JPEG_PG_MODE_MASK;
	WREG32_SOC15(JPEG, inst_idx, regUVD_JPEG_POWER_STATUS, reg_data);

	if (indirect)
		adev->jpeg.inst[inst_idx].dpg_sram_curr_addr =
			(uint32_t *)adev->jpeg.inst[inst_idx].dpg_sram_cpu_addr;

	jpeg_engine_5_0_0_dpg_clock_gating_mode(adev, inst_idx, indirect);

	/* MJPEG global tiling registers */
	if (indirect)
		ADD_SOC24_JPEG_TO_DPG_SRAM(inst_idx, vcnipJPEG_DEC_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config, indirect);
	else
		WREG32_SOC24_JPEG_DPG_MODE(inst_idx, vcnipJPEG_DEC_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config, 1);

	/* enable System Interrupt for JRBC */
	if (indirect)
		ADD_SOC24_JPEG_TO_DPG_SRAM(inst_idx, vcnipJPEG_SYS_INT_EN,
			JPEG_SYS_INT_EN__DJRBC0_MASK, indirect);
	else
		WREG32_SOC24_JPEG_DPG_MODE(inst_idx, vcnipJPEG_SYS_INT_EN,
			JPEG_SYS_INT_EN__DJRBC0_MASK, 1);

	if (indirect) {
		/* add nop to workaround PSP size check */
		ADD_SOC24_JPEG_TO_DPG_SRAM(inst_idx, vcnipUVD_NO_OP, 0, indirect);

		amdgpu_jpeg_psp_update_sram(adev, inst_idx, 0);
	}

	WREG32_SOC15(VCN, 0, regVCN_JPEG_DB_CTRL,
		ring->doorbell_index << VCN_JPEG_DB_CTRL__OFFSET__SHIFT |
		VCN_JPEG_DB_CTRL__EN_MASK);

	WREG32_SOC15(JPEG, inst_idx, regUVD_LMI_JRBC_RB_VMID, 0);
	WREG32_SOC15(JPEG, inst_idx, regUVD_JRBC_RB_CNTL, (0x00000001L | 0x00000002L));
	WREG32_SOC15(JPEG, inst_idx, regUVD_LMI_JRBC_RB_64BIT_BAR_LOW,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(JPEG, inst_idx, regUVD_LMI_JRBC_RB_64BIT_BAR_HIGH,
		upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(JPEG, inst_idx, regUVD_JRBC_RB_RPTR, 0);
	WREG32_SOC15(JPEG, inst_idx, regUVD_JRBC_RB_WPTR, 0);
	WREG32_SOC15(JPEG, inst_idx, regUVD_JRBC_RB_CNTL, 0x00000002L);
	WREG32_SOC15(JPEG, inst_idx, regUVD_JRBC_RB_SIZE, ring->ring_size / 4);
	ring->wptr = RREG32_SOC15(JPEG, inst_idx, regUVD_JRBC_RB_WPTR);

	return 0;
}

/**
 * jpeg_v5_0_0_stop_dpg_mode - Jpeg stop with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 *
 * Stop JPEG block with dpg mode
 */
static void jpeg_v5_0_0_stop_dpg_mode(struct amdgpu_device *adev, int inst_idx)
{
	uint32_t reg_data = 0;

	reg_data = RREG32_SOC15(JPEG, inst_idx, regUVD_JPEG_POWER_STATUS);
	reg_data &= ~UVD_JPEG_POWER_STATUS__JPEG_PG_MODE_MASK;
	WREG32_SOC15(JPEG, inst_idx, regUVD_JPEG_POWER_STATUS, reg_data);
}

/**
 * jpeg_v5_0_0_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
static int jpeg_v5_0_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = adev->jpeg.inst->ring_dec;
	int r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_jpeg(adev, true);

	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG_DPG) {
		r = jpeg_v5_0_0_start_dpg_mode(adev, 0, adev->jpeg.indirect_sram);
		return r;
	}

	/* disable power gating */
	r = jpeg_v5_0_0_disable_power_gating(adev);
	if (r)
		return r;

	/* JPEG disable CGC */
	jpeg_v5_0_0_disable_clock_gating(adev);

	/* MJPEG global tiling registers */
	WREG32_SOC15(JPEG, 0, regJPEG_DEC_GFX10_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);

	/* enable JMI channel */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, regUVD_JMI_CNTL), 0,
		~UVD_JMI_CNTL__SOFT_RESET_MASK);

	/* enable System Interrupt for JRBC */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, regJPEG_SYS_INT_EN),
		JPEG_SYS_INT_EN__DJRBC0_MASK,
		~JPEG_SYS_INT_EN__DJRBC0_MASK);

	WREG32_SOC15(VCN, 0, regVCN_JPEG_DB_CTRL,
		ring->doorbell_index << VCN_JPEG_DB_CTRL__OFFSET__SHIFT |
		VCN_JPEG_DB_CTRL__EN_MASK);

	WREG32_SOC15(JPEG, 0, regUVD_LMI_JRBC_RB_VMID, 0);
	WREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_CNTL, (0x00000001L | 0x00000002L));
	WREG32_SOC15(JPEG, 0, regUVD_LMI_JRBC_RB_64BIT_BAR_LOW,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(JPEG, 0, regUVD_LMI_JRBC_RB_64BIT_BAR_HIGH,
		upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_RPTR, 0);
	WREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_WPTR, 0);
	WREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_CNTL, 0x00000002L);
	WREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_SIZE, ring->ring_size / 4);
	ring->wptr = RREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_WPTR);

	return 0;
}

/**
 * jpeg_v5_0_0_stop - stop JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the JPEG block
 */
static int jpeg_v5_0_0_stop(struct amdgpu_device *adev)
{
	int r;

	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG_DPG) {
		jpeg_v5_0_0_stop_dpg_mode(adev, 0);
	} else {

		/* reset JMI */
		WREG32_P(SOC15_REG_OFFSET(JPEG, 0, regUVD_JMI_CNTL),
			UVD_JMI_CNTL__SOFT_RESET_MASK,
			~UVD_JMI_CNTL__SOFT_RESET_MASK);

		jpeg_v5_0_0_enable_clock_gating(adev);

		/* enable power gating */
		r = jpeg_v5_0_0_enable_power_gating(adev);
		if (r)
			return r;
	}

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_jpeg(adev, false);

	return 0;
}

/**
 * jpeg_v5_0_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v5_0_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_RPTR);
}

/**
 * jpeg_v5_0_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v5_0_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_WPTR);
}

/**
 * jpeg_v5_0_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v5_0_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(JPEG, 0, regUVD_JRBC_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static bool jpeg_v5_0_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret = 1;

	ret &= (((RREG32_SOC15(JPEG, 0, regUVD_JRBC_STATUS) &
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK) ==
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK));

	return ret;
}

static int jpeg_v5_0_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	return SOC15_WAIT_ON_RREG(JPEG, 0, regUVD_JRBC_STATUS,
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK,
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
}

static int jpeg_v5_0_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;

	if (enable) {
		if (!jpeg_v5_0_0_is_idle(handle))
			return -EBUSY;
		jpeg_v5_0_0_enable_clock_gating(adev);
	} else {
		jpeg_v5_0_0_disable_clock_gating(adev);
	}

	return 0;
}

static int jpeg_v5_0_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret;

	if (state == adev->jpeg.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = jpeg_v5_0_0_stop(adev);
	else
		ret = jpeg_v5_0_0_start(adev);

	if (!ret)
		adev->jpeg.cur_state = state;

	return ret;
}

static int jpeg_v5_0_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v5_0_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: JPEG TRAP\n");

	switch (entry->src_id) {
	case VCN_4_0__SRCID__JPEG_DECODE:
		amdgpu_fence_process(adev->jpeg.inst->ring_dec);
		break;
	default:
		DRM_DEV_ERROR(adev->dev, "Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amd_ip_funcs jpeg_v5_0_0_ip_funcs = {
	.name = "jpeg_v5_0_0",
	.early_init = jpeg_v5_0_0_early_init,
	.sw_init = jpeg_v5_0_0_sw_init,
	.sw_fini = jpeg_v5_0_0_sw_fini,
	.hw_init = jpeg_v5_0_0_hw_init,
	.hw_fini = jpeg_v5_0_0_hw_fini,
	.suspend = jpeg_v5_0_0_suspend,
	.resume = jpeg_v5_0_0_resume,
	.is_idle = jpeg_v5_0_0_is_idle,
	.wait_for_idle = jpeg_v5_0_0_wait_for_idle,
	.set_clockgating_state = jpeg_v5_0_0_set_clockgating_state,
	.set_powergating_state = jpeg_v5_0_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs jpeg_v5_0_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.get_rptr = jpeg_v5_0_0_dec_ring_get_rptr,
	.get_wptr = jpeg_v5_0_0_dec_ring_get_wptr,
	.set_wptr = jpeg_v5_0_0_dec_ring_set_wptr,
	.parse_cs = jpeg_v2_dec_ring_parse_cs,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v5_0_0_dec_ring_emit_vm_flush */
		22 + 22 + /* jpeg_v5_0_0_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 22, /* jpeg_v5_0_0_dec_ring_emit_ib */
	.emit_ib = jpeg_v4_0_3_dec_ring_emit_ib,
	.emit_fence = jpeg_v4_0_3_dec_ring_emit_fence,
	.emit_vm_flush = jpeg_v4_0_3_dec_ring_emit_vm_flush,
	.test_ring = amdgpu_jpeg_dec_ring_test_ring,
	.test_ib = amdgpu_jpeg_dec_ring_test_ib,
	.insert_nop = jpeg_v4_0_3_dec_ring_nop,
	.insert_start = jpeg_v4_0_3_dec_ring_insert_start,
	.insert_end = jpeg_v4_0_3_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_jpeg_ring_begin_use,
	.end_use = amdgpu_jpeg_ring_end_use,
	.emit_wreg = jpeg_v4_0_3_dec_ring_emit_wreg,
	.emit_reg_wait = jpeg_v4_0_3_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void jpeg_v5_0_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->ring_dec->funcs = &jpeg_v5_0_0_dec_ring_vm_funcs;
}

static const struct amdgpu_irq_src_funcs jpeg_v5_0_0_irq_funcs = {
	.set = jpeg_v5_0_0_set_interrupt_state,
	.process = jpeg_v5_0_0_process_interrupt,
};

static void jpeg_v5_0_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->irq.num_types = 1;
	adev->jpeg.inst->irq.funcs = &jpeg_v5_0_0_irq_funcs;
}

const struct amdgpu_ip_block_version jpeg_v5_0_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_JPEG,
	.major = 5,
	.minor = 0,
	.rev = 0,
	.funcs = &jpeg_v5_0_0_ip_funcs,
};

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
#include "amdgpu_cs.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "jpeg_v2_0.h"

#include "vcn/vcn_2_0_0_offset.h"
#include "vcn/vcn_2_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

static const struct amdgpu_hwip_reg_entry jpeg_reg_list_2_0[] = {
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

static void jpeg_v2_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v2_0_set_irq_funcs(struct amdgpu_device *adev);
static int jpeg_v2_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
				enum amd_powergating_state state);

/**
 * jpeg_v2_0_early_init - set function pointers
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 */
static int jpeg_v2_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->jpeg.num_jpeg_inst = 1;
	adev->jpeg.num_jpeg_rings = 1;

	jpeg_v2_0_set_dec_ring_funcs(adev);
	jpeg_v2_0_set_irq_funcs(adev);

	return 0;
}

/**
 * jpeg_v2_0_sw_init - sw init for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int jpeg_v2_0_sw_init(struct amdgpu_ip_block *ip_block)
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
	r = amdgpu_ring_init(adev, ring, 512, &adev->jpeg.inst->irq,
			     0, AMDGPU_RING_PRIO_DEFAULT, NULL);
	if (r)
		return r;

	adev->jpeg.internal.jpeg_pitch[0] = mmUVD_JPEG_PITCH_INTERNAL_OFFSET;
	adev->jpeg.inst->external.jpeg_pitch[0] = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_PITCH);

	r = amdgpu_jpeg_reg_dump_init(adev, jpeg_reg_list_2_0, ARRAY_SIZE(jpeg_reg_list_2_0));
	if (r)
		return r;

	adev->jpeg.supported_reset = AMDGPU_RESET_TYPE_PER_QUEUE;
	r = amdgpu_jpeg_sysfs_reset_mask_init(adev);

	return r;
}

/**
 * jpeg_v2_0_sw_fini - sw fini for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * JPEG suspend and free up sw allocation
 */
static int jpeg_v2_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	r = amdgpu_jpeg_suspend(adev);
	if (r)
		return r;

	amdgpu_jpeg_sysfs_reset_mask_fini(adev);

	r = amdgpu_jpeg_sw_fini(adev);

	return r;
}

/**
 * jpeg_v2_0_hw_init - start and test JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int jpeg_v2_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring = adev->jpeg.inst->ring_dec;

	adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
		(adev->doorbell_index.vcn.vcn_ring0_1 << 1), 0);

	return amdgpu_ring_test_helper(ring);
}

/**
 * jpeg_v2_0_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the JPEG block, mark ring as not ready any more
 */
static int jpeg_v2_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	if (adev->jpeg.cur_state != AMD_PG_STATE_GATE &&
	      RREG32_SOC15(JPEG, 0, mmUVD_JRBC_STATUS))
		jpeg_v2_0_set_powergating_state(ip_block, AMD_PG_STATE_GATE);

	return 0;
}

/**
 * jpeg_v2_0_suspend - suspend JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend JPEG block
 */
static int jpeg_v2_0_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = jpeg_v2_0_hw_fini(ip_block);
	if (r)
		return r;

	r = amdgpu_jpeg_suspend(ip_block->adev);

	return r;
}

/**
 * jpeg_v2_0_resume - resume JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init JPEG block
 */
static int jpeg_v2_0_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = amdgpu_jpeg_resume(ip_block->adev);
	if (r)
		return r;

	r = jpeg_v2_0_hw_init(ip_block);

	return r;
}

static int jpeg_v2_0_disable_power_gating(struct amdgpu_device *adev)
{
	uint32_t data;
	int r = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG) {
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

	/* Removing the anti hang mechanism to indicate the UVDJ tile is ON */
	data = RREG32(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS)) & ~0x1;
	WREG32(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS), data);

	return 0;
}

static int jpeg_v2_0_enable_power_gating(struct amdgpu_device *adev)
{
	if (adev->pg_flags & AMD_PG_SUPPORT_JPEG) {
		uint32_t data;
		int r = 0;

		data = RREG32(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS));
		data &= ~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK;
		data |=  0x1; //UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_TILES_OFF;
		WREG32(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_POWER_STATUS), data);

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

static void jpeg_v2_0_disable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data;

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
}

static void jpeg_v2_0_enable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RREG32_SOC15(JPEG, 0, mmJPEG_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_JPEG_MGCG)
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data |= 0 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(JPEG, 0, mmJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(JPEG, 0, mmJPEG_CGC_GATE);
	data |= (JPEG_CGC_GATE__JPEG_DEC_MASK
		|JPEG_CGC_GATE__JPEG2_DEC_MASK
		|JPEG_CGC_GATE__JPEG_ENC_MASK
		|JPEG_CGC_GATE__JMCIF_MASK
		|JPEG_CGC_GATE__JRBBM_MASK);
	WREG32_SOC15(JPEG, 0, mmJPEG_CGC_GATE, data);
}

/**
 * jpeg_v2_0_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
static int jpeg_v2_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = adev->jpeg.inst->ring_dec;
	int r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_jpeg(adev, true);

	/* disable power gating */
	r = jpeg_v2_0_disable_power_gating(adev);
	if (r)
		return r;

	/* JPEG disable CGC */
	jpeg_v2_0_disable_clock_gating(adev);

	WREG32_SOC15(JPEG, 0, mmJPEG_DEC_GFX10_ADDR_CONFIG, adev->gfx.config.gb_addr_config);

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
 * jpeg_v2_0_stop - stop JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the JPEG block
 */
static int jpeg_v2_0_stop(struct amdgpu_device *adev)
{
	int r;

	/* reset JMI */
	WREG32_P(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JMI_CNTL),
		UVD_JMI_CNTL__SOFT_RESET_MASK,
		~UVD_JMI_CNTL__SOFT_RESET_MASK);

	/* enable JPEG CGC */
	jpeg_v2_0_enable_clock_gating(adev);

	/* enable power gating */
	r = jpeg_v2_0_enable_power_gating(adev);
	if (r)
		return r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_jpeg(adev, false);

	return 0;
}

/**
 * jpeg_v2_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v2_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_RPTR);
}

/**
 * jpeg_v2_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v2_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR);
}

/**
 * jpeg_v2_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v2_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

/**
 * jpeg_v2_0_dec_ring_insert_start - insert a start command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a start command to the ring.
 */
void jpeg_v2_0_dec_ring_insert_start(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKETJ(mmUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x68e04);

	amdgpu_ring_write(ring, PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x80010000);
}

/**
 * jpeg_v2_0_dec_ring_insert_end - insert a end command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a end command to the ring.
 */
void jpeg_v2_0_dec_ring_insert_end(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKETJ(mmUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x68e04);

	amdgpu_ring_write(ring, PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x00010000);
}

/**
 * jpeg_v2_0_dec_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Write a fence and a trap command to the ring.
 */
void jpeg_v2_0_dec_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, PACKETJ(mmUVD_JPEG_GPCOM_DATA0_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JPEG_GPCOM_DATA1_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(addr));

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(addr));

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JPEG_GPCOM_CMD_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JPEG_GPCOM_CMD_INTERNAL_OFFSET,
		0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE4));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x3fbc);

	amdgpu_ring_write(ring, PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x1);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE7));
	amdgpu_ring_write(ring, 0);
}

/**
 * jpeg_v2_0_dec_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @job: job to retrieve vmid from
 * @ib: indirect buffer to execute
 * @flags: unused
 *
 * Write ring commands to execute the indirect buffer.
 */
void jpeg_v2_0_dec_ring_emit_ib(struct amdgpu_ring *ring,
				struct amdgpu_job *job,
				struct amdgpu_ib *ib,
				uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JPEG_IH_CTRL_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid << JPEG_IH_CTRL__IH_VMID__SHIFT));

	amdgpu_ring_write(ring, PACKETJ(mmUVD_LMI_JRBC_IB_VMID_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));

	if (ring->funcs->parse_cs)
		amdgpu_ring_write(ring, 0);
	else
		amdgpu_ring_write(ring, (vmid | (vmid << 4) | (vmid << 8)));

	amdgpu_ring_write(ring, PACKETJ(mmUVD_LMI_JPEG_VMID_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid | (vmid << 4) | (vmid << 8)));

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_LMI_JRBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_LMI_JRBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JRBC_IB_SIZE_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, ib->length_dw);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(0, 0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE2));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JRBC_RB_COND_RD_TIMER_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring, PACKETJ(mmUVD_JRBC_RB_REF_DATA_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x2);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JRBC_STATUS_INTERNAL_OFFSET,
		0, PACKETJ_CONDITION_CHECK3, PACKETJ_TYPE3));
	amdgpu_ring_write(ring, 0x2);
}

void jpeg_v2_0_dec_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
				uint32_t val, uint32_t mask)
{
	uint32_t reg_offset = (reg << 2);

	amdgpu_ring_write(ring, PACKETJ(mmUVD_JRBC_RB_COND_RD_TIMER_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JRBC_RB_REF_DATA_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, val);

	amdgpu_ring_write(ring, PACKETJ(mmUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	if (reg_offset >= 0x10000 && reg_offset <= 0x105ff) {
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring,
			PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE3));
	} else {
		amdgpu_ring_write(ring, reg_offset);
		amdgpu_ring_write(ring,	PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR,
			0, 0, PACKETJ_TYPE3));
	}
	amdgpu_ring_write(ring, mask);
}

void jpeg_v2_0_dec_ring_emit_vm_flush(struct amdgpu_ring *ring,
				unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->vm_hub];
	uint32_t data0, data1, mask;

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for register write */
	data0 = hub->ctx0_ptb_addr_lo32 + vmid * hub->ctx_addr_distance;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	jpeg_v2_0_dec_ring_emit_reg_wait(ring, data0, data1, mask);
}

void jpeg_v2_0_dec_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg, uint32_t val)
{
	uint32_t reg_offset = (reg << 2);

	amdgpu_ring_write(ring,	PACKETJ(mmUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	if (reg_offset >= 0x10000 && reg_offset <= 0x105ff) {
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring,
			PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE0));
	} else {
		amdgpu_ring_write(ring, reg_offset);
		amdgpu_ring_write(ring,	PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR,
			0, 0, PACKETJ_TYPE0));
	}
	amdgpu_ring_write(ring, val);
}

void jpeg_v2_0_dec_ring_nop(struct amdgpu_ring *ring, uint32_t count)
{
	int i;

	WARN_ON(ring->wptr % 2 || count % 2);

	for (i = 0; i < count / 2; i++) {
		amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE6));
		amdgpu_ring_write(ring, 0);
	}
}

static bool jpeg_v2_0_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	return ((RREG32_SOC15(JPEG, 0, mmUVD_JRBC_STATUS) &
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK) ==
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
}

static int jpeg_v2_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret;

	ret = SOC15_WAIT_ON_RREG(JPEG, 0, mmUVD_JRBC_STATUS, UVD_JRBC_STATUS__RB_JOB_DONE_MASK,
		UVD_JRBC_STATUS__RB_JOB_DONE_MASK);

	return ret;
}

static int jpeg_v2_0_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = (state == AMD_CG_STATE_GATE);

	if (enable) {
		if (!jpeg_v2_0_is_idle(ip_block))
			return -EBUSY;
		jpeg_v2_0_enable_clock_gating(adev);
	} else {
		jpeg_v2_0_disable_clock_gating(adev);
	}

	return 0;
}

static int jpeg_v2_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret;

	if (state == adev->jpeg.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = jpeg_v2_0_stop(adev);
	else
		ret = jpeg_v2_0_start(adev);

	if (!ret)
		adev->jpeg.cur_state = state;

	return ret;
}

static int jpeg_v2_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v2_0_process_interrupt(struct amdgpu_device *adev,
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

static int jpeg_v2_0_ring_reset(struct amdgpu_ring *ring, unsigned int vmid)
{
	jpeg_v2_0_stop(ring->adev);
	jpeg_v2_0_start(ring->adev);
	return amdgpu_ring_test_helper(ring);
}

static const struct amd_ip_funcs jpeg_v2_0_ip_funcs = {
	.name = "jpeg_v2_0",
	.early_init = jpeg_v2_0_early_init,
	.sw_init = jpeg_v2_0_sw_init,
	.sw_fini = jpeg_v2_0_sw_fini,
	.hw_init = jpeg_v2_0_hw_init,
	.hw_fini = jpeg_v2_0_hw_fini,
	.suspend = jpeg_v2_0_suspend,
	.resume = jpeg_v2_0_resume,
	.is_idle = jpeg_v2_0_is_idle,
	.wait_for_idle = jpeg_v2_0_wait_for_idle,
	.set_clockgating_state = jpeg_v2_0_set_clockgating_state,
	.set_powergating_state = jpeg_v2_0_set_powergating_state,
	.dump_ip_state = amdgpu_jpeg_dump_ip_state,
	.print_ip_state = amdgpu_jpeg_print_ip_state,
};

static const struct amdgpu_ring_funcs jpeg_v2_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.get_rptr = jpeg_v2_0_dec_ring_get_rptr,
	.get_wptr = jpeg_v2_0_dec_ring_get_wptr,
	.set_wptr = jpeg_v2_0_dec_ring_set_wptr,
	.parse_cs = jpeg_v2_dec_ring_parse_cs,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v2_0_dec_ring_emit_vm_flush */
		18 + 18 + /* jpeg_v2_0_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 24, /* jpeg_v2_0_dec_ring_emit_ib */
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
	.reset = jpeg_v2_0_ring_reset,
};

static void jpeg_v2_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->ring_dec->funcs = &jpeg_v2_0_dec_ring_vm_funcs;
}

static const struct amdgpu_irq_src_funcs jpeg_v2_0_irq_funcs = {
	.set = jpeg_v2_0_set_interrupt_state,
	.process = jpeg_v2_0_process_interrupt,
};

static void jpeg_v2_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->irq.num_types = 1;
	adev->jpeg.inst->irq.funcs = &jpeg_v2_0_irq_funcs;
}

const struct amdgpu_ip_block_version jpeg_v2_0_ip_block = {
		.type = AMD_IP_BLOCK_TYPE_JPEG,
		.major = 2,
		.minor = 0,
		.rev = 0,
		.funcs = &jpeg_v2_0_ip_funcs,
};

/**
 * jpeg_v2_dec_ring_parse_cs - command submission parser
 *
 * @parser: Command submission parser context
 * @job: the job to parse
 * @ib: the IB to parse
 *
 * Parse the command stream, return -EINVAL for invalid packet,
 * 0 otherwise
 */
int jpeg_v2_dec_ring_parse_cs(struct amdgpu_cs_parser *parser,
			      struct amdgpu_job *job,
			      struct amdgpu_ib *ib)
{
	u32 i, reg, res, cond, type;
	struct amdgpu_device *adev = parser->adev;

	for (i = 0; i < ib->length_dw ; i += 2) {
		reg  = CP_PACKETJ_GET_REG(ib->ptr[i]);
		res  = CP_PACKETJ_GET_RES(ib->ptr[i]);
		cond = CP_PACKETJ_GET_COND(ib->ptr[i]);
		type = CP_PACKETJ_GET_TYPE(ib->ptr[i]);

		if (res) /* only support 0 at the moment */
			return -EINVAL;

		switch (type) {
		case PACKETJ_TYPE0:
			if (cond != PACKETJ_CONDITION_CHECK0 || reg < JPEG_REG_RANGE_START ||
			    reg > JPEG_REG_RANGE_END) {
				dev_err(adev->dev, "Invalid packet [0x%08x]!\n", ib->ptr[i]);
				return -EINVAL;
			}
			break;
		case PACKETJ_TYPE3:
			if (cond != PACKETJ_CONDITION_CHECK3 || reg < JPEG_REG_RANGE_START ||
			    reg > JPEG_REG_RANGE_END) {
				dev_err(adev->dev, "Invalid packet [0x%08x]!\n", ib->ptr[i]);
				return -EINVAL;
			}
			break;
		case PACKETJ_TYPE6:
			if (ib->ptr[i] == CP_PACKETJ_NOP)
				continue;
			dev_err(adev->dev, "Invalid packet [0x%08x]!\n", ib->ptr[i]);
			return -EINVAL;
		default:
			dev_err(adev->dev, "Unknown packet type %d !\n", type);
			return -EINVAL;
		}
	}

	return 0;
}

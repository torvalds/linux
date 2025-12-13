// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2024 Advanced Micro Devices, Inc. All rights reserved.
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

#include "amdgpu.h"
#include "amdgpu_jpeg.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "jpeg_v4_0_3.h"
#include "jpeg_v5_0_1.h"
#include "mmsch_v5_0.h"

#include "vcn/vcn_5_0_0_offset.h"
#include "vcn/vcn_5_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_5_0.h"

static int jpeg_v5_0_1_start_sriov(struct amdgpu_device *adev);
static void jpeg_v5_0_1_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v5_0_1_set_irq_funcs(struct amdgpu_device *adev);
static int jpeg_v5_0_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
					     enum amd_powergating_state state);
static void jpeg_v5_0_1_set_ras_funcs(struct amdgpu_device *adev);
static void jpeg_v5_0_1_dec_ring_set_wptr(struct amdgpu_ring *ring);

static int amdgpu_ih_srcid_jpeg[] = {
	VCN_5_0__SRCID__JPEG_DECODE,
	VCN_5_0__SRCID__JPEG1_DECODE,
	VCN_5_0__SRCID__JPEG2_DECODE,
	VCN_5_0__SRCID__JPEG3_DECODE,
	VCN_5_0__SRCID__JPEG4_DECODE,
	VCN_5_0__SRCID__JPEG5_DECODE,
	VCN_5_0__SRCID__JPEG6_DECODE,
	VCN_5_0__SRCID__JPEG7_DECODE,
	VCN_5_0__SRCID__JPEG8_DECODE,
	VCN_5_0__SRCID__JPEG9_DECODE,
};

static const struct amdgpu_hwip_reg_entry jpeg_reg_list_5_0_1[] = {
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JPEG_POWER_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JPEG_INT_STAT),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC0_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC0_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC0_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regJPEG_DEC_ADDR_MODE),
	SOC15_REG_ENTRY_STR(JPEG, 0, regJPEG_DEC_GFX10_ADDR_CONFIG),
	SOC15_REG_ENTRY_STR(JPEG, 0, regJPEG_DEC_Y_GFX10_TILING_SURFACE),
	SOC15_REG_ENTRY_STR(JPEG, 0, regJPEG_DEC_UV_GFX10_TILING_SURFACE),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JPEG_PITCH),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JPEG_UV_PITCH),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC1_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC1_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC1_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC2_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC2_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC2_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC3_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC3_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC3_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC4_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC4_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC4_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC5_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC5_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC5_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC6_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC6_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC6_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC7_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC7_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC7_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC8_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC8_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC8_UVD_JRBC_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC9_UVD_JRBC_RB_RPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC9_UVD_JRBC_RB_WPTR),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JRBC9_UVD_JRBC_STATUS),
};

static int jpeg_v5_0_1_core_reg_offset(u32 pipe)
{
	if (pipe <= AMDGPU_MAX_JPEG_RINGS_4_0_3)
		return ((0x40 * pipe) - 0xc80);
	else
		return ((0x40 * pipe) - 0x440);
}

/**
 * jpeg_v5_0_1_early_init - set function pointers
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 */
static int jpeg_v5_0_1_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (!adev->jpeg.num_jpeg_inst || adev->jpeg.num_jpeg_inst > AMDGPU_MAX_JPEG_INSTANCES)
		return -ENOENT;

	adev->jpeg.num_jpeg_rings = AMDGPU_MAX_JPEG_RINGS;
	jpeg_v5_0_1_set_dec_ring_funcs(adev);
	jpeg_v5_0_1_set_irq_funcs(adev);
	jpeg_v5_0_1_set_ras_funcs(adev);

	return 0;
}

/**
 * jpeg_v5_0_1_sw_init - sw init for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int jpeg_v5_0_1_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, j, r, jpeg_inst;

	for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
		/* JPEG TRAP */
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
				      amdgpu_ih_srcid_jpeg[j], &adev->jpeg.inst->irq);
		if (r)
			return r;
	}
	/* JPEG DJPEG POISON EVENT */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
			VCN_5_0__SRCID_DJPEG0_POISON, &adev->jpeg.inst->ras_poison_irq);
	if (r)
		return r;

	/* JPEG EJPEG POISON EVENT */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
			VCN_5_0__SRCID_EJPEG0_POISON, &adev->jpeg.inst->ras_poison_irq);
	if (r)
		return r;

	r = amdgpu_jpeg_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_jpeg_resume(adev);
	if (r)
		return r;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		jpeg_inst = GET_INST(JPEG, i);

		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			ring->use_doorbell = true;
			ring->vm_hub = AMDGPU_MMHUB0(adev->jpeg.inst[i].aid_id);
			if (!amdgpu_sriov_vf(adev)) {
				ring->doorbell_index =
					(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
					1 + j + 11 * jpeg_inst;
			} else {
				ring->doorbell_index =
					(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
					2 + j + 32 * jpeg_inst;
			}
			sprintf(ring->name, "jpeg_dec_%d.%d", adev->jpeg.inst[i].aid_id, j);
			r = amdgpu_ring_init(adev, ring, 512, &adev->jpeg.inst->irq, 0,
					     AMDGPU_RING_PRIO_DEFAULT, NULL);
			if (r)
				return r;

			adev->jpeg.internal.jpeg_pitch[j] =
				regUVD_JRBC0_UVD_JRBC_SCRATCH0_INTERNAL_OFFSET;
			adev->jpeg.inst[i].external.jpeg_pitch[j] =
				SOC15_REG_OFFSET1(JPEG, jpeg_inst, regUVD_JRBC_SCRATCH0,
						  (j ? jpeg_v5_0_1_core_reg_offset(j) : 0));
		}
	}

	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__JPEG)) {
		r = amdgpu_jpeg_ras_sw_init(adev);
		if (r) {
			dev_err(adev->dev, "Failed to initialize jpeg ras block!\n");
			return r;
		}
	}

	r = amdgpu_jpeg_reg_dump_init(adev, jpeg_reg_list_5_0_1, ARRAY_SIZE(jpeg_reg_list_5_0_1));
	if (r)
		return r;

	adev->jpeg.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->jpeg.inst[0].ring_dec[0]);
	if (!amdgpu_sriov_vf(adev))
		adev->jpeg.supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;
	r = amdgpu_jpeg_sysfs_reset_mask_init(adev);

	return r;
}

/**
 * jpeg_v5_0_1_sw_fini - sw fini for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * JPEG suspend and free up sw allocation
 */
static int jpeg_v5_0_1_sw_fini(struct amdgpu_ip_block *ip_block)
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
 * jpeg_v5_0_1_hw_init - start and test JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int jpeg_v5_0_1_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, j, r, jpeg_inst;

	if (amdgpu_sriov_vf(adev)) {
		r = jpeg_v5_0_1_start_sriov(adev);
		if (r)
			return r;

		for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
			for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
				ring = &adev->jpeg.inst[i].ring_dec[j];
				ring->wptr = 0;
				ring->wptr_old = 0;
				jpeg_v5_0_1_dec_ring_set_wptr(ring);
				ring->sched.ready = true;
			}
		}
		return 0;
	}
	if (RREG32_SOC15(VCN, GET_INST(VCN, 0), regVCN_RRMT_CNTL) & 0x100)
		adev->jpeg.caps |= AMDGPU_JPEG_CAPS(RRMT_ENABLED);

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		jpeg_inst = GET_INST(JPEG, i);
		ring = adev->jpeg.inst[i].ring_dec;
		if (ring->use_doorbell)
			adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
				 (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 11 * jpeg_inst,
				 adev->jpeg.inst[i].aid_id);

		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			if (ring->use_doorbell)
				WREG32_SOC15_OFFSET(VCN, GET_INST(VCN, i), regVCN_JPEG_DB_CTRL,
						    ring->pipe,
						    ring->doorbell_index <<
						    VCN_JPEG_DB_CTRL__OFFSET__SHIFT |
						    VCN_JPEG_DB_CTRL__EN_MASK);
			r = amdgpu_ring_test_helper(ring);
			if (r)
				return r;
		}
	}

	return 0;
}

/**
 * jpeg_v5_0_1_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the JPEG block, mark ring as not ready any more
 */
static int jpeg_v5_0_1_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret = 0;

	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	if (!amdgpu_sriov_vf(adev)) {
		if (adev->jpeg.cur_state != AMD_PG_STATE_GATE)
			ret = jpeg_v5_0_1_set_powergating_state(ip_block, AMD_PG_STATE_GATE);
	}

	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__JPEG) && !amdgpu_sriov_vf(adev))
		amdgpu_irq_put(adev, &adev->jpeg.inst->ras_poison_irq, 0);

	return ret;
}

/**
 * jpeg_v5_0_1_suspend - suspend JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend JPEG block
 */
static int jpeg_v5_0_1_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = jpeg_v5_0_1_hw_fini(ip_block);
	if (r)
		return r;

	r = amdgpu_jpeg_suspend(adev);

	return r;
}

/**
 * jpeg_v5_0_1_resume - resume JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init JPEG block
 */
static int jpeg_v5_0_1_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_jpeg_resume(adev);
	if (r)
		return r;

	r = jpeg_v5_0_1_hw_init(ip_block);

	return r;
}

static void jpeg_v5_0_1_init_inst(struct amdgpu_device *adev, int i)
{
	int jpeg_inst = GET_INST(JPEG, i);

	/* disable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JPEG_POWER_STATUS), 0,
		 ~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

	/* keep the JPEG in static PG mode */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JPEG_POWER_STATUS), 0,
		 ~UVD_JPEG_POWER_STATUS__JPEG_PG_MODE_MASK);

	/* MJPEG global tiling registers */
	WREG32_SOC15(JPEG, 0, regJPEG_DEC_GFX10_ADDR_CONFIG,
		     adev->gfx.config.gb_addr_config);

	/* enable JMI channel */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JMI_CNTL), 0,
		 ~UVD_JMI_CNTL__SOFT_RESET_MASK);
}

static void jpeg_v5_0_1_deinit_inst(struct amdgpu_device *adev, int i)
{
	int jpeg_inst = GET_INST(JPEG, i);
	/* reset JMI */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JMI_CNTL),
		 UVD_JMI_CNTL__SOFT_RESET_MASK,
		 ~UVD_JMI_CNTL__SOFT_RESET_MASK);

	/* enable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JPEG_POWER_STATUS),
		 UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK,
		 ~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);
}

static void jpeg_v5_0_1_init_jrbc(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 reg, data, mask;
	int jpeg_inst = GET_INST(JPEG, ring->me);
	int reg_offset = ring->pipe ? jpeg_v5_0_1_core_reg_offset(ring->pipe) : 0;

	/* enable System Interrupt for JRBC */
	reg = SOC15_REG_OFFSET(JPEG, jpeg_inst, regJPEG_SYS_INT_EN);
	if (ring->pipe < AMDGPU_MAX_JPEG_RINGS_4_0_3) {
		data = JPEG_SYS_INT_EN__DJRBC0_MASK << ring->pipe;
		mask = ~(JPEG_SYS_INT_EN__DJRBC0_MASK << ring->pipe);
		WREG32_P(reg, data, mask);
	} else {
		data = JPEG_SYS_INT_EN__DJRBC0_MASK << (ring->pipe+12);
		mask = ~(JPEG_SYS_INT_EN__DJRBC0_MASK << (ring->pipe+12));
		WREG32_P(reg, data, mask);
	}

	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_LMI_JRBC_RB_VMID,
			    reg_offset, 0);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC_RB_CNTL,
			    reg_offset,
			    (0x00000001L | 0x00000002L));
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_LMI_JRBC_RB_64BIT_BAR_LOW,
			    reg_offset, lower_32_bits(ring->gpu_addr));
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_LMI_JRBC_RB_64BIT_BAR_HIGH,
			    reg_offset, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC_RB_RPTR,
			    reg_offset, 0);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC_RB_WPTR,
			    reg_offset, 0);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC_RB_CNTL,
			    reg_offset, 0x00000002L);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC_RB_SIZE,
			    reg_offset, ring->ring_size / 4);
	ring->wptr = RREG32_SOC15_OFFSET(JPEG, jpeg_inst, regUVD_JRBC_RB_WPTR,
					 reg_offset);
}

static int jpeg_v5_0_1_start_sriov(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	uint64_t ctx_addr;
	uint32_t param, resp, expected;
	uint32_t tmp, timeout;

	struct amdgpu_mm_table *table = &adev->virt.mm_table;
	uint32_t *table_loc;
	uint32_t table_size;
	uint32_t size, size_dw, item_offset;
	uint32_t init_status;
	int i, j, jpeg_inst;

	struct mmsch_v5_0_cmd_direct_write
		direct_wt = { {0} };
	struct mmsch_v5_0_cmd_end end = { {0} };
	struct mmsch_v5_0_init_header header;

	direct_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_WRITE;
	end.cmd_header.command_type =
		MMSCH_COMMAND__END;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; i++) {
		jpeg_inst = GET_INST(JPEG, i);

		memset(&header, 0, sizeof(struct mmsch_v5_0_init_header));
		header.version = MMSCH_VERSION;
		header.total_size = sizeof(struct mmsch_v5_0_init_header) >> 2;

		table_loc = (uint32_t *)table->cpu_addr;
		table_loc += header.total_size;

		item_offset = header.total_size;

		for (j = 0; j < adev->jpeg.num_jpeg_rings; j++) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			table_size = 0;

			tmp = SOC15_REG_OFFSET(JPEG, 0, regUVD_LMI_JRBC_RB_64BIT_BAR_LOW);
			MMSCH_V5_0_INSERT_DIRECT_WT(tmp, lower_32_bits(ring->gpu_addr));
			tmp = SOC15_REG_OFFSET(JPEG, 0, regUVD_LMI_JRBC_RB_64BIT_BAR_HIGH);
			MMSCH_V5_0_INSERT_DIRECT_WT(tmp, upper_32_bits(ring->gpu_addr));
			tmp = SOC15_REG_OFFSET(JPEG, 0, regUVD_JRBC_RB_SIZE);
			MMSCH_V5_0_INSERT_DIRECT_WT(tmp, ring->ring_size / 4);

			if (j < 5) {
				header.mjpegdec0[j].table_offset = item_offset;
				header.mjpegdec0[j].init_status = 0;
				header.mjpegdec0[j].table_size = table_size;
			} else {
				header.mjpegdec1[j - 5].table_offset = item_offset;
				header.mjpegdec1[j - 5].init_status = 0;
				header.mjpegdec1[j - 5].table_size = table_size;
			}
			header.total_size += table_size;
			item_offset += table_size;
		}

		MMSCH_V5_0_INSERT_END();

		/* send init table to MMSCH */
		size = sizeof(struct mmsch_v5_0_init_header);
		table_loc = (uint32_t *)table->cpu_addr;
		memcpy((void *)table_loc, &header, size);

		ctx_addr = table->gpu_addr;
		WREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_CTX_ADDR_LO, lower_32_bits(ctx_addr));
		WREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_CTX_ADDR_HI, upper_32_bits(ctx_addr));

		tmp = RREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_VMID);
		tmp &= ~MMSCH_VF_VMID__VF_CTX_VMID_MASK;
		tmp |= (0 << MMSCH_VF_VMID__VF_CTX_VMID__SHIFT);
		WREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_VMID, tmp);

		size = header.total_size;
		WREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_CTX_SIZE, size);

		WREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_MAILBOX_RESP, 0);

		param = 0x00000001;
		WREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_MAILBOX_HOST, param);
		tmp = 0;
		timeout = 1000;
		resp = 0;
		expected = MMSCH_VF_MAILBOX_RESP__OK;
		init_status =
			((struct mmsch_v5_0_init_header *)(table_loc))->mjpegdec0[i].init_status;
		while (resp != expected) {
			resp = RREG32_SOC15(VCN, jpeg_inst, regMMSCH_VF_MAILBOX_RESP);

			if (resp != 0)
				break;
			udelay(10);
			tmp = tmp + 10;
			if (tmp >= timeout) {
				DRM_ERROR("failed to init MMSCH. TIME-OUT after %d usec"\
					" waiting for regMMSCH_VF_MAILBOX_RESP "\
					"(expected=0x%08x, readback=0x%08x)\n",
					tmp, expected, resp);
				return -EBUSY;
			}
		}
		if (resp != expected && resp != MMSCH_VF_MAILBOX_RESP__INCOMPLETE &&
				init_status != MMSCH_VF_ENGINE_STATUS__PASS)
			DRM_ERROR("MMSCH init status is incorrect! readback=0x%08x, header init status for jpeg: %x\n",
					resp, init_status);

	}
	return 0;
}

/**
 * jpeg_v5_0_1_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
static int jpeg_v5_0_1_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		jpeg_v5_0_1_init_inst(adev, i);
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			jpeg_v5_0_1_init_jrbc(ring);
		}
	}

	return 0;
}

/**
 * jpeg_v5_0_1_stop - stop JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the JPEG block
 */
static int jpeg_v5_0_1_stop(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i)
		jpeg_v5_0_1_deinit_inst(adev, i);

	return 0;
}

/**
 * jpeg_v5_0_1_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v5_0_1_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, ring->me), regUVD_JRBC_RB_RPTR,
				   ring->pipe ? jpeg_v5_0_1_core_reg_offset(ring->pipe) : 0);
}

/**
 * jpeg_v5_0_1_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v5_0_1_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];

	return RREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, ring->me), regUVD_JRBC_RB_WPTR,
				   ring->pipe ? jpeg_v5_0_1_core_reg_offset(ring->pipe) : 0);
}

/**
 * jpeg_v5_0_1_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v5_0_1_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, ring->me),
				    regUVD_JRBC_RB_WPTR,
				    (ring->pipe ? jpeg_v5_0_1_core_reg_offset(ring->pipe) : 0),
				    lower_32_bits(ring->wptr));
	}
}

static bool jpeg_v5_0_1_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool ret = false;
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			int reg_offset = (j ? jpeg_v5_0_1_core_reg_offset(j) : 0);

			ret &= ((RREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, i),
				regUVD_JRBC_STATUS, reg_offset) &
				UVD_JRBC_STATUS__RB_JOB_DONE_MASK) ==
				UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
		}
	}

	return ret;
}

static int jpeg_v5_0_1_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret = 0;
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			int reg_offset = (j ? jpeg_v5_0_1_core_reg_offset(j) : 0);

			ret &= SOC15_WAIT_ON_RREG_OFFSET(JPEG, GET_INST(JPEG, i),
							 regUVD_JRBC_STATUS, reg_offset,
							 UVD_JRBC_STATUS__RB_JOB_DONE_MASK,
							 UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
		}
	}
	return ret;
}

static int jpeg_v5_0_1_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					     enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = state == AMD_CG_STATE_GATE;

	int i;

	if (!enable)
		return 0;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (!jpeg_v5_0_1_is_idle(ip_block))
			return -EBUSY;
	}

	return 0;
}

static int jpeg_v5_0_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
					     enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret;

	if (amdgpu_sriov_vf(adev)) {
		adev->jpeg.cur_state = AMD_PG_STATE_UNGATE;
		return 0;
	}

	if (state == adev->jpeg.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = jpeg_v5_0_1_stop(adev);
	else
		ret = jpeg_v5_0_1_start(adev);

	if (!ret)
		adev->jpeg.cur_state = state;

	return ret;
}

static int jpeg_v5_0_1_set_interrupt_state(struct amdgpu_device *adev,
					   struct amdgpu_irq_src *source,
					   unsigned int type,
					   enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v5_0_1_set_ras_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}



static int jpeg_v5_0_1_process_interrupt(struct amdgpu_device *adev,
					 struct amdgpu_irq_src *source,
					 struct amdgpu_iv_entry *entry)
{
	u32 i, inst;

	i = node_id_to_phys_map[entry->node_id];
	DRM_DEV_DEBUG(adev->dev, "IH: JPEG TRAP\n");

	for (inst = 0; inst < adev->jpeg.num_jpeg_inst; ++inst)
		if (adev->jpeg.inst[inst].aid_id == i)
			break;

	if (inst >= adev->jpeg.num_jpeg_inst) {
		dev_WARN_ONCE(adev->dev, 1,
			      "Interrupt received for unknown JPEG instance %d",
			      entry->node_id);
		return 0;
	}

	switch (entry->src_id) {
	case VCN_5_0__SRCID__JPEG_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[0]);
		break;
	case VCN_5_0__SRCID__JPEG1_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[1]);
		break;
	case VCN_5_0__SRCID__JPEG2_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[2]);
		break;
	case VCN_5_0__SRCID__JPEG3_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[3]);
		break;
	case VCN_5_0__SRCID__JPEG4_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[4]);
		break;
	case VCN_5_0__SRCID__JPEG5_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[5]);
		break;
	case VCN_5_0__SRCID__JPEG6_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[6]);
		break;
	case VCN_5_0__SRCID__JPEG7_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[7]);
		break;
	case VCN_5_0__SRCID__JPEG8_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[8]);
		break;
	case VCN_5_0__SRCID__JPEG9_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[9]);
		break;
	default:
		DRM_DEV_ERROR(adev->dev, "Unhandled interrupt: %d %d\n",
			      entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static void jpeg_v5_0_1_core_stall_reset(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int jpeg_inst = GET_INST(JPEG, ring->me);
	int reg_offset = ring->pipe ? jpeg_v5_0_1_core_reg_offset(ring->pipe) : 0;

	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_UVD_JMI_CLIENT_STALL,
			    reg_offset, 0x1F);
	SOC15_WAIT_ON_RREG_OFFSET(JPEG, jpeg_inst,
				  regUVD_JMI0_UVD_JMI_CLIENT_CLEAN_STATUS,
				  reg_offset, 0x1F, 0x1F);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_JPEG_LMI_DROP,
			    reg_offset, 0x1F);
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_CORE_RST_CTRL, 1 << ring->pipe);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_UVD_JMI_CLIENT_STALL,
			    reg_offset, 0x00);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_JPEG_LMI_DROP,
			    reg_offset, 0x00);
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_CORE_RST_CTRL, 0x00);
}

static int jpeg_v5_0_1_ring_reset(struct amdgpu_ring *ring,
				  unsigned int vmid,
				  struct amdgpu_fence *timedout_fence)
{
	amdgpu_ring_reset_helper_begin(ring, timedout_fence);
	jpeg_v5_0_1_core_stall_reset(ring);
	jpeg_v5_0_1_init_jrbc(ring);
	return amdgpu_ring_reset_helper_end(ring, timedout_fence);
}

static const struct amd_ip_funcs jpeg_v5_0_1_ip_funcs = {
	.name = "jpeg_v5_0_1",
	.early_init = jpeg_v5_0_1_early_init,
	.late_init = NULL,
	.sw_init = jpeg_v5_0_1_sw_init,
	.sw_fini = jpeg_v5_0_1_sw_fini,
	.hw_init = jpeg_v5_0_1_hw_init,
	.hw_fini = jpeg_v5_0_1_hw_fini,
	.suspend = jpeg_v5_0_1_suspend,
	.resume = jpeg_v5_0_1_resume,
	.is_idle = jpeg_v5_0_1_is_idle,
	.wait_for_idle = jpeg_v5_0_1_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = jpeg_v5_0_1_set_clockgating_state,
	.set_powergating_state = jpeg_v5_0_1_set_powergating_state,
	.dump_ip_state = amdgpu_jpeg_dump_ip_state,
	.print_ip_state = amdgpu_jpeg_print_ip_state,
};

static const struct amdgpu_ring_funcs jpeg_v5_0_1_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.get_rptr = jpeg_v5_0_1_dec_ring_get_rptr,
	.get_wptr = jpeg_v5_0_1_dec_ring_get_wptr,
	.set_wptr = jpeg_v5_0_1_dec_ring_set_wptr,
	.parse_cs = amdgpu_jpeg_dec_parse_cs,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v5_0_1_dec_ring_emit_vm_flush */
		22 + 22 + /* jpeg_v5_0_1_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 22, /* jpeg_v5_0_1_dec_ring_emit_ib */
	.emit_ib = jpeg_v4_0_3_dec_ring_emit_ib,
	.emit_fence = jpeg_v4_0_3_dec_ring_emit_fence,
	.emit_vm_flush = jpeg_v4_0_3_dec_ring_emit_vm_flush,
	.emit_hdp_flush = jpeg_v4_0_3_ring_emit_hdp_flush,
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
	.reset = jpeg_v5_0_1_ring_reset,
};

static void jpeg_v5_0_1_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	int i, j, jpeg_inst;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			adev->jpeg.inst[i].ring_dec[j].funcs = &jpeg_v5_0_1_dec_ring_vm_funcs;
			adev->jpeg.inst[i].ring_dec[j].me = i;
			adev->jpeg.inst[i].ring_dec[j].pipe = j;
		}
		jpeg_inst = GET_INST(JPEG, i);
		adev->jpeg.inst[i].aid_id =
			jpeg_inst / adev->jpeg.num_inst_per_aid;
	}
}

static const struct amdgpu_irq_src_funcs jpeg_v5_0_1_irq_funcs = {
	.set = jpeg_v5_0_1_set_interrupt_state,
	.process = jpeg_v5_0_1_process_interrupt,
};

static const struct amdgpu_irq_src_funcs jpeg_v5_0_1_ras_irq_funcs = {
	.set = jpeg_v5_0_1_set_ras_interrupt_state,
	.process = amdgpu_jpeg_process_poison_irq,
};

static void jpeg_v5_0_1_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i)
		adev->jpeg.inst->irq.num_types += adev->jpeg.num_jpeg_rings;

	adev->jpeg.inst->irq.funcs = &jpeg_v5_0_1_irq_funcs;

	adev->jpeg.inst->ras_poison_irq.num_types = 1;
	adev->jpeg.inst->ras_poison_irq.funcs = &jpeg_v5_0_1_ras_irq_funcs;

}

const struct amdgpu_ip_block_version jpeg_v5_0_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_JPEG,
	.major = 5,
	.minor = 0,
	.rev = 1,
	.funcs = &jpeg_v5_0_1_ip_funcs,
};

static uint32_t jpeg_v5_0_1_query_poison_by_instance(struct amdgpu_device *adev,
		uint32_t instance, uint32_t sub_block)
{
	uint32_t poison_stat = 0, reg_value = 0;

	switch (sub_block) {
	case AMDGPU_JPEG_V5_0_1_JPEG0:
		reg_value = RREG32_SOC15(JPEG, instance, regUVD_RAS_JPEG0_STATUS);
		poison_stat = REG_GET_FIELD(reg_value, UVD_RAS_JPEG0_STATUS, POISONED_PF);
		break;
	case AMDGPU_JPEG_V5_0_1_JPEG1:
		reg_value = RREG32_SOC15(JPEG, instance, regUVD_RAS_JPEG1_STATUS);
		poison_stat = REG_GET_FIELD(reg_value, UVD_RAS_JPEG1_STATUS, POISONED_PF);
		break;
	default:
		break;
	}

	if (poison_stat)
		dev_info(adev->dev, "Poison detected in JPEG%d sub_block%d\n",
			instance, sub_block);

	return poison_stat;
}

static bool jpeg_v5_0_1_query_ras_poison_status(struct amdgpu_device *adev)
{
	uint32_t inst = 0, sub = 0, poison_stat = 0;

	for (inst = 0; inst < adev->jpeg.num_jpeg_inst; inst++)
		for (sub = 0; sub < AMDGPU_JPEG_V5_0_1_MAX_SUB_BLOCK; sub++)
			poison_stat +=
			jpeg_v5_0_1_query_poison_by_instance(adev, inst, sub);

	return !!poison_stat;
}

static const struct amdgpu_ras_block_hw_ops jpeg_v5_0_1_ras_hw_ops = {
	.query_poison_status = jpeg_v5_0_1_query_ras_poison_status,
};

static int jpeg_v5_0_1_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
				      enum aca_smu_type type, void *data)
{
	struct aca_bank_info info;
	u64 misc0;
	int ret;

	ret = aca_bank_info_decode(bank, &info);
	if (ret)
		return ret;

	misc0 = bank->regs[ACA_REG_IDX_MISC0];
	switch (type) {
	case ACA_SMU_TYPE_UE:
		bank->aca_err_type = ACA_ERROR_TYPE_UE;
		ret = aca_error_cache_log_bank_error(handle, &info, ACA_ERROR_TYPE_UE,
						     1ULL);
		break;
	case ACA_SMU_TYPE_CE:
		bank->aca_err_type = ACA_ERROR_TYPE_CE;
		ret = aca_error_cache_log_bank_error(handle, &info, bank->aca_err_type,
						     ACA_REG__MISC0__ERRCNT(misc0));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/* reference to smu driver if header file */
static int jpeg_v5_0_1_err_codes[] = {
	16, 17, 18, 19, 20, 21, 22, 23, /* JPEG[0-9][S|D] */
	24, 25, 26, 27, 28, 29, 30, 31,
	48, 49, 50, 51,
};

static bool jpeg_v5_0_1_aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank,
					 enum aca_smu_type type, void *data)
{
	u32 instlo;

	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);

	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	if (aca_bank_check_error_codes(handle->adev, bank,
				       jpeg_v5_0_1_err_codes,
				       ARRAY_SIZE(jpeg_v5_0_1_err_codes)))
		return false;

	return true;
}

static const struct aca_bank_ops jpeg_v5_0_1_aca_bank_ops = {
	.aca_bank_parser = jpeg_v5_0_1_aca_bank_parser,
	.aca_bank_is_valid = jpeg_v5_0_1_aca_bank_is_valid,
};

static const struct aca_info jpeg_v5_0_1_aca_info = {
	.hwip = ACA_HWIP_TYPE_SMU,
	.mask = ACA_ERROR_UE_MASK,
	.bank_ops = &jpeg_v5_0_1_aca_bank_ops,
};

static int jpeg_v5_0_1_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	r = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__JPEG,
				&jpeg_v5_0_1_aca_info, NULL);
	if (r)
		goto late_fini;

	if (amdgpu_ras_is_supported(adev, ras_block->block) &&
		adev->jpeg.inst->ras_poison_irq.funcs) {
		r = amdgpu_irq_get(adev, &adev->jpeg.inst->ras_poison_irq, 0);
		if (r)
			goto late_fini;
	}

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);

	return r;
}

static struct amdgpu_jpeg_ras jpeg_v5_0_1_ras = {
	.ras_block = {
		.hw_ops = &jpeg_v5_0_1_ras_hw_ops,
		.ras_late_init = jpeg_v5_0_1_ras_late_init,
	},
};

static void jpeg_v5_0_1_set_ras_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.ras = &jpeg_v5_0_1_ras;
}

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
 *
 */

#include "amdgpu.h"
#include "amdgpu_jpeg.h"
#include "soc15.h"
#include "soc15d.h"
#include "jpeg_v2_0.h"
#include "jpeg_v4_0_3.h"
#include "mmsch_v4_0_3.h"

#include "vcn/vcn_4_0_3_offset.h"
#include "vcn/vcn_4_0_3_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_4_0.h"

#define NORMALIZE_JPEG_REG_OFFSET(offset) \
		(offset & 0x1FFFF)

enum jpeg_engin_status {
	UVD_PGFSM_STATUS__UVDJ_PWR_ON  = 0,
	UVD_PGFSM_STATUS__UVDJ_PWR_OFF = 2,
};

static void jpeg_v4_0_3_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v4_0_3_set_irq_funcs(struct amdgpu_device *adev);
static int jpeg_v4_0_3_set_powergating_state(struct amdgpu_ip_block *ip_block,
				enum amd_powergating_state state);
static void jpeg_v4_0_3_set_ras_funcs(struct amdgpu_device *adev);
static void jpeg_v4_0_3_dec_ring_set_wptr(struct amdgpu_ring *ring);

static int amdgpu_ih_srcid_jpeg[] = {
	VCN_4_0__SRCID__JPEG_DECODE,
	VCN_4_0__SRCID__JPEG1_DECODE,
	VCN_4_0__SRCID__JPEG2_DECODE,
	VCN_4_0__SRCID__JPEG3_DECODE,
	VCN_4_0__SRCID__JPEG4_DECODE,
	VCN_4_0__SRCID__JPEG5_DECODE,
	VCN_4_0__SRCID__JPEG6_DECODE,
	VCN_4_0__SRCID__JPEG7_DECODE
};

static const struct amdgpu_hwip_reg_entry jpeg_reg_list_4_0_3[] = {
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JPEG_POWER_STATUS),
	SOC15_REG_ENTRY_STR(JPEG, 0, regUVD_JPEG_INT_STAT),
	SOC15_REG_ENTRY_STR(JPEG, 0, regJPEG_SYS_INT_STATUS),
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
};

static inline bool jpeg_v4_0_3_normalizn_reqd(struct amdgpu_device *adev)
{
	return (adev->jpeg.caps & AMDGPU_JPEG_CAPS(RRMT_ENABLED)) == 0;
}

static inline int jpeg_v4_0_3_core_reg_offset(u32 pipe)
{
	if (pipe)
		return ((0x40 * pipe) - 0xc80);
	else
		return 0;
}

/**
 * jpeg_v4_0_3_early_init - set function pointers
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 */
static int jpeg_v4_0_3_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->jpeg.num_jpeg_rings = AMDGPU_MAX_JPEG_RINGS_4_0_3;

	jpeg_v4_0_3_set_dec_ring_funcs(adev);
	jpeg_v4_0_3_set_irq_funcs(adev);
	jpeg_v4_0_3_set_ras_funcs(adev);

	return 0;
}

/**
 * jpeg_v4_0_3_sw_init - sw init for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int jpeg_v4_0_3_sw_init(struct amdgpu_ip_block *ip_block)
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
					1 + j + 9 * jpeg_inst;
			} else {
				if (j < 4)
					ring->doorbell_index =
						(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
						4 + j + 32 * jpeg_inst;
				else
					ring->doorbell_index =
						(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
						8 + j + 32 * jpeg_inst;
			}
			sprintf(ring->name, "jpeg_dec_%d.%d", adev->jpeg.inst[i].aid_id, j);
			r = amdgpu_ring_init(adev, ring, 512, &adev->jpeg.inst->irq, 0,
						AMDGPU_RING_PRIO_DEFAULT, NULL);
			if (r)
				return r;

			adev->jpeg.internal.jpeg_pitch[j] =
				regUVD_JRBC0_UVD_JRBC_SCRATCH0_INTERNAL_OFFSET;
			adev->jpeg.inst[i].external.jpeg_pitch[j] =
				SOC15_REG_OFFSET1(JPEG, jpeg_inst, regUVD_JRBC0_UVD_JRBC_SCRATCH0,
						  jpeg_v4_0_3_core_reg_offset(j));
		}
	}

	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__JPEG)) {
		r = amdgpu_jpeg_ras_sw_init(adev);
		if (r) {
			dev_err(adev->dev, "Failed to initialize jpeg ras block!\n");
			return r;
		}
	}

	r = amdgpu_jpeg_reg_dump_init(adev, jpeg_reg_list_4_0_3, ARRAY_SIZE(jpeg_reg_list_4_0_3));
	if (r)
		return r;

	if (!amdgpu_sriov_vf(adev)) {
		adev->jpeg.supported_reset = AMDGPU_RESET_TYPE_PER_QUEUE;
		r = amdgpu_jpeg_sysfs_reset_mask_init(adev);
		if (r)
			return r;
	}

	return 0;
}

/**
 * jpeg_v4_0_3_sw_fini - sw fini for JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * JPEG suspend and free up sw allocation
 */
static int jpeg_v4_0_3_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_jpeg_suspend(adev);
	if (r)
		return r;

	if (!amdgpu_sriov_vf(adev))
		amdgpu_jpeg_sysfs_reset_mask_fini(adev);

	r = amdgpu_jpeg_sw_fini(adev);

	return r;
}

static int jpeg_v4_0_3_start_sriov(struct amdgpu_device *adev)
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

	struct mmsch_v4_0_cmd_direct_write
		direct_wt = { {0} };
	struct mmsch_v4_0_cmd_end end = { {0} };
	struct mmsch_v4_0_3_init_header header;

	direct_wt.cmd_header.command_type =
		MMSCH_COMMAND__DIRECT_REG_WRITE;
	end.cmd_header.command_type =
		MMSCH_COMMAND__END;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; i++) {
		jpeg_inst = GET_INST(JPEG, i);

		memset(&header, 0, sizeof(struct mmsch_v4_0_3_init_header));
		header.version = MMSCH_VERSION;
		header.total_size = sizeof(struct mmsch_v4_0_3_init_header) >> 2;

		table_loc = (uint32_t *)table->cpu_addr;
		table_loc += header.total_size;

		item_offset = header.total_size;

		for (j = 0; j < adev->jpeg.num_jpeg_rings; j++) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			table_size = 0;

			tmp = SOC15_REG_OFFSET(JPEG, 0, regUVD_JMI0_UVD_LMI_JRBC_RB_64BIT_BAR_LOW);
			MMSCH_V4_0_INSERT_DIRECT_WT(tmp, lower_32_bits(ring->gpu_addr));
			tmp = SOC15_REG_OFFSET(JPEG, 0, regUVD_JMI0_UVD_LMI_JRBC_RB_64BIT_BAR_HIGH);
			MMSCH_V4_0_INSERT_DIRECT_WT(tmp, upper_32_bits(ring->gpu_addr));
			tmp = SOC15_REG_OFFSET(JPEG, 0, regUVD_JRBC0_UVD_JRBC_RB_SIZE);
			MMSCH_V4_0_INSERT_DIRECT_WT(tmp, ring->ring_size / 4);

			if (j <= 3) {
				header.mjpegdec0[j].table_offset = item_offset;
				header.mjpegdec0[j].init_status = 0;
				header.mjpegdec0[j].table_size = table_size;
			} else {
				header.mjpegdec1[j - 4].table_offset = item_offset;
				header.mjpegdec1[j - 4].init_status = 0;
				header.mjpegdec1[j - 4].table_size = table_size;
			}
			header.total_size += table_size;
			item_offset += table_size;
		}

		MMSCH_V4_0_INSERT_END();

		/* send init table to MMSCH */
		size = sizeof(struct mmsch_v4_0_3_init_header);
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
			((struct mmsch_v4_0_3_init_header *)(table_loc))->mjpegdec0[i].init_status;
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
 * jpeg_v4_0_3_hw_init - start and test JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int jpeg_v4_0_3_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, j, r, jpeg_inst;

	if (amdgpu_sriov_vf(adev)) {
		r = jpeg_v4_0_3_start_sriov(adev);
		if (r)
			return r;

		for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
			for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
				ring = &adev->jpeg.inst[i].ring_dec[j];
				ring->wptr = 0;
				ring->wptr_old = 0;
				jpeg_v4_0_3_dec_ring_set_wptr(ring);
				ring->sched.ready = true;
			}
		}
	} else {
		/* This flag is not set for VF, assumed to be disabled always */
		if (RREG32_SOC15(VCN, GET_INST(VCN, 0), regVCN_RRMT_CNTL) &
		    0x100)
			adev->jpeg.caps |= AMDGPU_JPEG_CAPS(RRMT_ENABLED);

		for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
			jpeg_inst = GET_INST(JPEG, i);

			ring = adev->jpeg.inst[i].ring_dec;

			if (ring->use_doorbell)
				adev->nbio.funcs->vcn_doorbell_range(
					adev, ring->use_doorbell,
					(adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
						9 * jpeg_inst,
					adev->jpeg.inst[i].aid_id);

			for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
				ring = &adev->jpeg.inst[i].ring_dec[j];
				if (ring->use_doorbell)
					WREG32_SOC15_OFFSET(
						VCN, GET_INST(VCN, i),
						regVCN_JPEG_DB_CTRL,
						(ring->pipe ? (ring->pipe - 0x15) : 0),
						ring->doorbell_index
							<< VCN_JPEG_DB_CTRL__OFFSET__SHIFT |
							VCN_JPEG_DB_CTRL__EN_MASK);
				r = amdgpu_ring_test_helper(ring);
				if (r)
					return r;
			}
		}
	}

	return 0;
}

/**
 * jpeg_v4_0_3_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the JPEG block, mark ring as not ready any more
 */
static int jpeg_v4_0_3_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret = 0;

	cancel_delayed_work_sync(&adev->jpeg.idle_work);

	if (!amdgpu_sriov_vf(adev)) {
		if (adev->jpeg.cur_state != AMD_PG_STATE_GATE)
			ret = jpeg_v4_0_3_set_powergating_state(ip_block, AMD_PG_STATE_GATE);
	}

	return ret;
}

/**
 * jpeg_v4_0_3_suspend - suspend JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend JPEG block
 */
static int jpeg_v4_0_3_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = jpeg_v4_0_3_hw_fini(ip_block);
	if (r)
		return r;

	r = amdgpu_jpeg_suspend(ip_block->adev);

	return r;
}

/**
 * jpeg_v4_0_3_resume - resume JPEG block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init JPEG block
 */
static int jpeg_v4_0_3_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = amdgpu_jpeg_resume(ip_block->adev);
	if (r)
		return r;

	r = jpeg_v4_0_3_hw_init(ip_block);

	return r;
}

static void jpeg_v4_0_3_disable_clock_gating(struct amdgpu_device *adev, int inst_idx)
{
	int i, jpeg_inst;
	uint32_t data;

	jpeg_inst = GET_INST(JPEG, inst_idx);
	data = RREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_JPEG_MGCG) {
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
		data &= (~(JPEG_CGC_CTRL__JPEG0_DEC_MODE_MASK << 1));
	} else {
		data &= ~JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	}

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_GATE);
	data &= ~(JPEG_CGC_GATE__JMCIF_MASK | JPEG_CGC_GATE__JRBBM_MASK);
	for (i = 0; i < adev->jpeg.num_jpeg_rings; ++i)
		data &= ~(JPEG_CGC_GATE__JPEG0_DEC_MASK << i);
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_GATE, data);
}

static void jpeg_v4_0_3_enable_clock_gating(struct amdgpu_device *adev, int inst_idx)
{
	int i, jpeg_inst;
	uint32_t data;

	jpeg_inst = GET_INST(JPEG, inst_idx);
	data = RREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_JPEG_MGCG) {
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
		data |= (JPEG_CGC_CTRL__JPEG0_DEC_MODE_MASK << 1);
	} else {
		data &= ~JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	}

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_GATE);
	data |= (JPEG_CGC_GATE__JMCIF_MASK | JPEG_CGC_GATE__JRBBM_MASK);
	for (i = 0; i < adev->jpeg.num_jpeg_rings; ++i)
		data |= (JPEG_CGC_GATE__JPEG0_DEC_MASK << i);
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_CGC_GATE, data);
}

static void jpeg_v4_0_3_start_inst(struct amdgpu_device *adev, int inst)
{
	int jpeg_inst = GET_INST(JPEG, inst);

	WREG32_SOC15(JPEG, jpeg_inst, regUVD_PGFSM_CONFIG,
		     1 << UVD_PGFSM_CONFIG__UVDJ_PWR_CONFIG__SHIFT);
	SOC15_WAIT_ON_RREG(JPEG, jpeg_inst, regUVD_PGFSM_STATUS,
			   UVD_PGFSM_STATUS__UVDJ_PWR_ON <<
			   UVD_PGFSM_STATUS__UVDJ_PWR_STATUS__SHIFT,
			   UVD_PGFSM_STATUS__UVDJ_PWR_STATUS_MASK);

	/* disable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JPEG_POWER_STATUS),
		 0, ~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

	/* JPEG disable CGC */
	jpeg_v4_0_3_disable_clock_gating(adev, inst);

	/* MJPEG global tiling registers */
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_DEC_GFX8_ADDR_CONFIG,
		     adev->gfx.config.gb_addr_config);
	WREG32_SOC15(JPEG, jpeg_inst, regJPEG_DEC_GFX10_ADDR_CONFIG,
		     adev->gfx.config.gb_addr_config);

	/* enable JMI channel */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JMI_CNTL), 0,
		 ~UVD_JMI_CNTL__SOFT_RESET_MASK);
}

static void jpeg_v4_0_3_start_jrbc(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int jpeg_inst = GET_INST(JPEG, ring->me);
	int reg_offset = jpeg_v4_0_3_core_reg_offset(ring->pipe);

	/* enable System Interrupt for JRBC */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regJPEG_SYS_INT_EN),
		 JPEG_SYS_INT_EN__DJRBC0_MASK << ring->pipe,
		 ~(JPEG_SYS_INT_EN__DJRBC0_MASK << ring->pipe));

	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_UVD_LMI_JRBC_RB_VMID,
			    reg_offset, 0);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC0_UVD_JRBC_RB_CNTL,
			    reg_offset,
			    (0x00000001L | 0x00000002L));
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_UVD_LMI_JRBC_RB_64BIT_BAR_LOW,
			    reg_offset, lower_32_bits(ring->gpu_addr));
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JMI0_UVD_LMI_JRBC_RB_64BIT_BAR_HIGH,
			    reg_offset, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC0_UVD_JRBC_RB_RPTR,
			    reg_offset, 0);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC0_UVD_JRBC_RB_WPTR,
			    reg_offset, 0);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC0_UVD_JRBC_RB_CNTL,
			    reg_offset, 0x00000002L);
	WREG32_SOC15_OFFSET(JPEG, jpeg_inst,
			    regUVD_JRBC0_UVD_JRBC_RB_SIZE,
			    reg_offset, ring->ring_size / 4);
	ring->wptr = RREG32_SOC15_OFFSET(JPEG, jpeg_inst, regUVD_JRBC0_UVD_JRBC_RB_WPTR,
					 reg_offset);
}

/**
 * jpeg_v4_0_3_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
static int jpeg_v4_0_3_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		jpeg_v4_0_3_start_inst(adev, i);
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ring = &adev->jpeg.inst[i].ring_dec[j];
			jpeg_v4_0_3_start_jrbc(ring);
		}
	}

	return 0;
}

static void jpeg_v4_0_3_stop_inst(struct amdgpu_device *adev, int inst)
{
	int jpeg_inst = GET_INST(JPEG, inst);
	/* reset JMI */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JMI_CNTL),
		 UVD_JMI_CNTL__SOFT_RESET_MASK,
		 ~UVD_JMI_CNTL__SOFT_RESET_MASK);

	jpeg_v4_0_3_enable_clock_gating(adev, inst);

	/* enable anti hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(JPEG, jpeg_inst, regUVD_JPEG_POWER_STATUS),
		 UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK,
		 ~UVD_JPEG_POWER_STATUS__JPEG_POWER_STATUS_MASK);

}

/**
 * jpeg_v4_0_3_stop - stop JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the JPEG block
 */
static int jpeg_v4_0_3_stop(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i)
		jpeg_v4_0_3_stop_inst(adev, i);

	return 0;
}

/**
 * jpeg_v4_0_3_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v4_0_3_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, ring->me), regUVD_JRBC0_UVD_JRBC_RB_RPTR,
				   jpeg_v4_0_3_core_reg_offset(ring->pipe));
}

/**
 * jpeg_v4_0_3_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v4_0_3_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];

	return RREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, ring->me), regUVD_JRBC0_UVD_JRBC_RB_WPTR,
				   jpeg_v4_0_3_core_reg_offset(ring->pipe));
}

void jpeg_v4_0_3_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	/* JPEG engine access for HDP flush doesn't work when RRMT is enabled.
	 * This is a workaround to avoid any HDP flush through JPEG ring.
	 */
}

/**
 * jpeg_v4_0_3_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v4_0_3_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, ring->me), regUVD_JRBC0_UVD_JRBC_RB_WPTR,
				    jpeg_v4_0_3_core_reg_offset(ring->pipe),
				    lower_32_bits(ring->wptr));
	}
}

/**
 * jpeg_v4_0_3_dec_ring_insert_start - insert a start command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a start command to the ring.
 */
void jpeg_v4_0_3_dec_ring_insert_start(struct amdgpu_ring *ring)
{
	if (!amdgpu_sriov_vf(ring->adev)) {
		amdgpu_ring_write(ring, PACKETJ(regUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
			0, 0, PACKETJ_TYPE0));
		amdgpu_ring_write(ring, 0x62a04); /* PCTL0_MMHUB_DEEPSLEEP_IB */

		amdgpu_ring_write(ring,
				  PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR, 0,
					  0, PACKETJ_TYPE0));
		amdgpu_ring_write(ring, 0x80004000);
	}
}

/**
 * jpeg_v4_0_3_dec_ring_insert_end - insert a end command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a end command to the ring.
 */
void jpeg_v4_0_3_dec_ring_insert_end(struct amdgpu_ring *ring)
{
	if (!amdgpu_sriov_vf(ring->adev)) {
		amdgpu_ring_write(ring, PACKETJ(regUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
			0, 0, PACKETJ_TYPE0));
		amdgpu_ring_write(ring, 0x62a04);

		amdgpu_ring_write(ring,
				  PACKETJ(JRBC_DEC_EXTERNAL_REG_WRITE_ADDR, 0,
					  0, PACKETJ_TYPE0));
		amdgpu_ring_write(ring, 0x00004000);
	}
}

/**
 * jpeg_v4_0_3_dec_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Write a fence and a trap command to the ring.
 */
void jpeg_v4_0_3_dec_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				unsigned int flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, PACKETJ(regUVD_JPEG_GPCOM_DATA0_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JPEG_GPCOM_DATA1_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(addr));

	amdgpu_ring_write(ring,	PACKETJ(regUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(addr));

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JPEG_GPCOM_CMD_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JPEG_GPCOM_CMD_INTERNAL_OFFSET,
		0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE4));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE6));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE6));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE7));
	amdgpu_ring_write(ring, 0);
}

/**
 * jpeg_v4_0_3_dec_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @job: job to retrieve vmid from
 * @ib: indirect buffer to execute
 * @flags: unused
 *
 * Write ring commands to execute the indirect buffer.
 */
void jpeg_v4_0_3_dec_ring_emit_ib(struct amdgpu_ring *ring,
				struct amdgpu_job *job,
				struct amdgpu_ib *ib,
				uint32_t flags)
{
	unsigned int vmid = AMDGPU_JOB_GET_VMID(job);

	amdgpu_ring_write(ring, PACKETJ(regUVD_LMI_JRBC_IB_VMID_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));

	if (ring->funcs->parse_cs)
		amdgpu_ring_write(ring, 0);
	else
		amdgpu_ring_write(ring, (vmid | (vmid << 4) | (vmid << 8)));

	amdgpu_ring_write(ring, PACKETJ(regUVD_LMI_JPEG_VMID_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid | (vmid << 4) | (vmid << 8)));

	amdgpu_ring_write(ring,	PACKETJ(regUVD_LMI_JRBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(regUVD_LMI_JRBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JRBC_IB_SIZE_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, ib->length_dw);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(regUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,	PACKETJ(0, 0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE2));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JRBC_RB_COND_RD_TIMER_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring, PACKETJ(regUVD_JRBC_RB_REF_DATA_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x2);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JRBC_STATUS_INTERNAL_OFFSET,
		0, PACKETJ_CONDITION_CHECK3, PACKETJ_TYPE3));
	amdgpu_ring_write(ring, 0x2);
}

void jpeg_v4_0_3_dec_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
				uint32_t val, uint32_t mask)
{
	uint32_t reg_offset;

	/* Use normalized offsets if required */
	if (jpeg_v4_0_3_normalizn_reqd(ring->adev))
		reg = NORMALIZE_JPEG_REG_OFFSET(reg);

	reg_offset = (reg << 2);

	amdgpu_ring_write(ring, PACKETJ(regUVD_JRBC_RB_COND_RD_TIMER_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JRBC_RB_REF_DATA_INTERNAL_OFFSET,
		0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, val);

	amdgpu_ring_write(ring, PACKETJ(regUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
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

void jpeg_v4_0_3_dec_ring_emit_vm_flush(struct amdgpu_ring *ring,
				unsigned int vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->vm_hub];
	uint32_t data0, data1, mask;

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for register write */
	data0 = hub->ctx0_ptb_addr_lo32 + vmid * hub->ctx_addr_distance;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	jpeg_v4_0_3_dec_ring_emit_reg_wait(ring, data0, data1, mask);
}

void jpeg_v4_0_3_dec_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg, uint32_t val)
{
	uint32_t reg_offset;

	/* Use normalized offsets if required */
	if (jpeg_v4_0_3_normalizn_reqd(ring->adev))
		reg = NORMALIZE_JPEG_REG_OFFSET(reg);

	reg_offset = (reg << 2);

	amdgpu_ring_write(ring,	PACKETJ(regUVD_JRBC_EXTERNAL_REG_INTERNAL_OFFSET,
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

void jpeg_v4_0_3_dec_ring_nop(struct amdgpu_ring *ring, uint32_t count)
{
	int i;

	WARN_ON(ring->wptr % 2 || count % 2);

	for (i = 0; i < count / 2; i++) {
		amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE6));
		amdgpu_ring_write(ring, 0);
	}
}

static bool jpeg_v4_0_3_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool ret = false;
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ret &= ((RREG32_SOC15_OFFSET(JPEG, GET_INST(JPEG, i),
				regUVD_JRBC0_UVD_JRBC_STATUS, jpeg_v4_0_3_core_reg_offset(j)) &
				UVD_JRBC0_UVD_JRBC_STATUS__RB_JOB_DONE_MASK) ==
				UVD_JRBC0_UVD_JRBC_STATUS__RB_JOB_DONE_MASK);
		}
	}

	return ret;
}

static int jpeg_v4_0_3_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret = 0;
	int i, j;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			ret &= (SOC15_WAIT_ON_RREG_OFFSET(JPEG, GET_INST(JPEG, i),
				regUVD_JRBC0_UVD_JRBC_STATUS, jpeg_v4_0_3_core_reg_offset(j),
				UVD_JRBC0_UVD_JRBC_STATUS__RB_JOB_DONE_MASK,
				UVD_JRBC0_UVD_JRBC_STATUS__RB_JOB_DONE_MASK));
		}
	}
	return ret;
}

static int jpeg_v4_0_3_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = state == AMD_CG_STATE_GATE;
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		if (enable) {
			if (!jpeg_v4_0_3_is_idle(ip_block))
				return -EBUSY;
			jpeg_v4_0_3_enable_clock_gating(adev, i);
		} else {
			jpeg_v4_0_3_disable_clock_gating(adev, i);
		}
	}
	return 0;
}

static int jpeg_v4_0_3_set_powergating_state(struct amdgpu_ip_block *ip_block,
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
		ret = jpeg_v4_0_3_stop(adev);
	else
		ret = jpeg_v4_0_3_start(adev);

	if (!ret)
		adev->jpeg.cur_state = state;

	return ret;
}

static int jpeg_v4_0_3_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v4_0_3_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t i, inst;

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
	case VCN_4_0__SRCID__JPEG_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[0]);
		break;
	case VCN_4_0__SRCID__JPEG1_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[1]);
		break;
	case VCN_4_0__SRCID__JPEG2_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[2]);
		break;
	case VCN_4_0__SRCID__JPEG3_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[3]);
		break;
	case VCN_4_0__SRCID__JPEG4_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[4]);
		break;
	case VCN_4_0__SRCID__JPEG5_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[5]);
		break;
	case VCN_4_0__SRCID__JPEG6_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[6]);
		break;
	case VCN_4_0__SRCID__JPEG7_DECODE:
		amdgpu_fence_process(&adev->jpeg.inst[inst].ring_dec[7]);
		break;
	default:
		DRM_DEV_ERROR(adev->dev, "Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static void jpeg_v4_0_3_core_stall_reset(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int jpeg_inst = GET_INST(JPEG, ring->me);
	int reg_offset = jpeg_v4_0_3_core_reg_offset(ring->pipe);

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

static int jpeg_v4_0_3_ring_reset(struct amdgpu_ring *ring, unsigned int vmid)
{
	if (amdgpu_sriov_vf(ring->adev))
		return -EOPNOTSUPP;

	jpeg_v4_0_3_core_stall_reset(ring);
	jpeg_v4_0_3_start_jrbc(ring);
	return amdgpu_ring_test_helper(ring);
}

static const struct amd_ip_funcs jpeg_v4_0_3_ip_funcs = {
	.name = "jpeg_v4_0_3",
	.early_init = jpeg_v4_0_3_early_init,
	.sw_init = jpeg_v4_0_3_sw_init,
	.sw_fini = jpeg_v4_0_3_sw_fini,
	.hw_init = jpeg_v4_0_3_hw_init,
	.hw_fini = jpeg_v4_0_3_hw_fini,
	.suspend = jpeg_v4_0_3_suspend,
	.resume = jpeg_v4_0_3_resume,
	.is_idle = jpeg_v4_0_3_is_idle,
	.wait_for_idle = jpeg_v4_0_3_wait_for_idle,
	.set_clockgating_state = jpeg_v4_0_3_set_clockgating_state,
	.set_powergating_state = jpeg_v4_0_3_set_powergating_state,
	.dump_ip_state = amdgpu_jpeg_dump_ip_state,
	.print_ip_state = amdgpu_jpeg_print_ip_state,
};

static const struct amdgpu_ring_funcs jpeg_v4_0_3_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.get_rptr = jpeg_v4_0_3_dec_ring_get_rptr,
	.get_wptr = jpeg_v4_0_3_dec_ring_get_wptr,
	.set_wptr = jpeg_v4_0_3_dec_ring_set_wptr,
	.parse_cs = jpeg_v2_dec_ring_parse_cs,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v4_0_3_dec_ring_emit_vm_flush */
		18 + 18 + /* jpeg_v4_0_3_dec_ring_emit_fence x2 vm fence */
		8 + 16,
	.emit_ib_size = 22, /* jpeg_v4_0_3_dec_ring_emit_ib */
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
	.reset = jpeg_v4_0_3_ring_reset,
};

static void jpeg_v4_0_3_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	int i, j, jpeg_inst;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		for (j = 0; j < adev->jpeg.num_jpeg_rings; ++j) {
			adev->jpeg.inst[i].ring_dec[j].funcs = &jpeg_v4_0_3_dec_ring_vm_funcs;
			adev->jpeg.inst[i].ring_dec[j].me = i;
			adev->jpeg.inst[i].ring_dec[j].pipe = j;
		}
		jpeg_inst = GET_INST(JPEG, i);
		adev->jpeg.inst[i].aid_id =
			jpeg_inst / adev->jpeg.num_inst_per_aid;
	}
}

static const struct amdgpu_irq_src_funcs jpeg_v4_0_3_irq_funcs = {
	.set = jpeg_v4_0_3_set_interrupt_state,
	.process = jpeg_v4_0_3_process_interrupt,
};

static void jpeg_v4_0_3_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->jpeg.num_jpeg_inst; ++i) {
		adev->jpeg.inst->irq.num_types += adev->jpeg.num_jpeg_rings;
	}
	adev->jpeg.inst->irq.funcs = &jpeg_v4_0_3_irq_funcs;
}

const struct amdgpu_ip_block_version jpeg_v4_0_3_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_JPEG,
	.major = 4,
	.minor = 0,
	.rev = 3,
	.funcs = &jpeg_v4_0_3_ip_funcs,
};

static const struct amdgpu_ras_err_status_reg_entry jpeg_v4_0_3_ue_reg_list[] = {
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG0S, regVCN_UE_ERR_STATUS_HI_JPEG0S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG0S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG0D, regVCN_UE_ERR_STATUS_HI_JPEG0D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG0D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG1S, regVCN_UE_ERR_STATUS_HI_JPEG1S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG1S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG1D, regVCN_UE_ERR_STATUS_HI_JPEG1D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG1D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG2S, regVCN_UE_ERR_STATUS_HI_JPEG2S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG2S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG2D, regVCN_UE_ERR_STATUS_HI_JPEG2D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG2D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG3S, regVCN_UE_ERR_STATUS_HI_JPEG3S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG3S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG3D, regVCN_UE_ERR_STATUS_HI_JPEG3D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG3D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG4S, regVCN_UE_ERR_STATUS_HI_JPEG4S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG4S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG4D, regVCN_UE_ERR_STATUS_HI_JPEG4D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG4D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG5S, regVCN_UE_ERR_STATUS_HI_JPEG5S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG5S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG5D, regVCN_UE_ERR_STATUS_HI_JPEG5D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG5D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG6S, regVCN_UE_ERR_STATUS_HI_JPEG6S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG6S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG6D, regVCN_UE_ERR_STATUS_HI_JPEG6D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG6D"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG7S, regVCN_UE_ERR_STATUS_HI_JPEG7S),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG7S"},
	{AMDGPU_RAS_REG_ENTRY(JPEG, 0, regVCN_UE_ERR_STATUS_LO_JPEG7D, regVCN_UE_ERR_STATUS_HI_JPEG7D),
	1, (AMDGPU_RAS_ERR_INFO_VALID | AMDGPU_RAS_ERR_STATUS_VALID), "JPEG7D"},
};

static void jpeg_v4_0_3_inst_query_ras_error_count(struct amdgpu_device *adev,
						   uint32_t jpeg_inst,
						   void *ras_err_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_err_status;

	/* jpeg v4_0_3 only support uncorrectable errors */
	amdgpu_ras_inst_query_ras_error_count(adev,
			jpeg_v4_0_3_ue_reg_list,
			ARRAY_SIZE(jpeg_v4_0_3_ue_reg_list),
			NULL, 0, GET_INST(VCN, jpeg_inst),
			AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
			&err_data->ue_count);
}

static void jpeg_v4_0_3_query_ras_error_count(struct amdgpu_device *adev,
					      void *ras_err_status)
{
	uint32_t i;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__JPEG)) {
		dev_warn(adev->dev, "JPEG RAS is not supported\n");
		return;
	}

	for (i = 0; i < adev->jpeg.num_jpeg_inst; i++)
		jpeg_v4_0_3_inst_query_ras_error_count(adev, i, ras_err_status);
}

static void jpeg_v4_0_3_inst_reset_ras_error_count(struct amdgpu_device *adev,
						   uint32_t jpeg_inst)
{
	amdgpu_ras_inst_reset_ras_error_count(adev,
			jpeg_v4_0_3_ue_reg_list,
			ARRAY_SIZE(jpeg_v4_0_3_ue_reg_list),
			GET_INST(VCN, jpeg_inst));
}

static void jpeg_v4_0_3_reset_ras_error_count(struct amdgpu_device *adev)
{
	uint32_t i;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__JPEG)) {
		dev_warn(adev->dev, "JPEG RAS is not supported\n");
		return;
	}

	for (i = 0; i < adev->jpeg.num_jpeg_inst; i++)
		jpeg_v4_0_3_inst_reset_ras_error_count(adev, i);
}

static const struct amdgpu_ras_block_hw_ops jpeg_v4_0_3_ras_hw_ops = {
	.query_ras_error_count = jpeg_v4_0_3_query_ras_error_count,
	.reset_ras_error_count = jpeg_v4_0_3_reset_ras_error_count,
};

static int jpeg_v4_0_3_aca_bank_parser(struct aca_handle *handle, struct aca_bank *bank,
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
		bank->aca_err_type = ACA_BANK_ERR_CE_DE_DECODE(bank);
		ret = aca_error_cache_log_bank_error(handle, &info, bank->aca_err_type,
						     ACA_REG__MISC0__ERRCNT(misc0));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/* reference to smu driver if header file */
static int jpeg_v4_0_3_err_codes[] = {
	16, 17, 18, 19, 20, 21, 22, 23, /* JPEG[0-7][S|D] */
	24, 25, 26, 27, 28, 29, 30, 31
};

static bool jpeg_v4_0_3_aca_bank_is_valid(struct aca_handle *handle, struct aca_bank *bank,
					 enum aca_smu_type type, void *data)
{
	u32 instlo;

	instlo = ACA_REG__IPID__INSTANCEIDLO(bank->regs[ACA_REG_IDX_IPID]);
	instlo &= GENMASK(31, 1);

	if (instlo != mmSMNAID_AID0_MCA_SMU)
		return false;

	if (aca_bank_check_error_codes(handle->adev, bank,
				       jpeg_v4_0_3_err_codes,
				       ARRAY_SIZE(jpeg_v4_0_3_err_codes)))
		return false;

	return true;
}

static const struct aca_bank_ops jpeg_v4_0_3_aca_bank_ops = {
	.aca_bank_parser = jpeg_v4_0_3_aca_bank_parser,
	.aca_bank_is_valid = jpeg_v4_0_3_aca_bank_is_valid,
};

static const struct aca_info jpeg_v4_0_3_aca_info = {
	.hwip = ACA_HWIP_TYPE_SMU,
	.mask = ACA_ERROR_UE_MASK,
	.bank_ops = &jpeg_v4_0_3_aca_bank_ops,
};

static int jpeg_v4_0_3_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	r = amdgpu_ras_bind_aca(adev, AMDGPU_RAS_BLOCK__JPEG,
				&jpeg_v4_0_3_aca_info, NULL);
	if (r)
		goto late_fini;

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);

	return r;
}

static struct amdgpu_jpeg_ras jpeg_v4_0_3_ras = {
	.ras_block = {
		.hw_ops = &jpeg_v4_0_3_ras_hw_ops,
		.ras_late_init = jpeg_v4_0_3_ras_late_init,
	},
};

static void jpeg_v4_0_3_set_ras_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.ras = &jpeg_v4_0_3_ras;
}

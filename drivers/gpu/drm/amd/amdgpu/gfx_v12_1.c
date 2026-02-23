/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_psp.h"
#include "amdgpu_smu.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_userq_fence.h"
#include "imu_v12_1.h"
#include "soc_v1_0.h"
#include "gfx_v12_1_pkt.h"

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "soc24_enum.h"
#include "ivsrcid/gfx/irqsrcs_gfx_12_1_0.h"

#include "soc15.h"
#include "clearstate_gfx12.h"
#include "v12_structs.h"
#include "gfx_v12_1.h"
#include "mes_v12_1.h"

#define GFX12_MEC_HPD_SIZE	2048
#define NUM_SIMD_PER_CU_GFX12_1	4

#define RLCG_UCODE_LOADING_START_ADDRESS	0x00002000L

#define regCP_HQD_EOP_CONTROL_DEFAULT                                             0x00000000
#define regCP_HQD_PQ_DOORBELL_CONTROL_DEFAULT                                     0x00000000
#define regCP_MQD_CONTROL_DEFAULT                                                 0x00000100
#define regCP_HQD_PQ_CONTROL_DEFAULT                                              0x00308509
#define regCP_HQD_PQ_RPTR_DEFAULT                                                 0x00000000
#define regCP_HQD_PERSISTENT_STATE_DEFAULT                                        0x0ae06301
#define regCP_HQD_IB_CONTROL_DEFAULT                                              0x00100000

MODULE_FIRMWARE("amdgpu/gc_12_1_0_mec.bin");
MODULE_FIRMWARE("amdgpu/gc_12_1_0_rlc.bin");

#define SH_MEM_ALIGNMENT_MODE_UNALIGNED_GFX12_1_0	0x00000001
#define DEFAULT_SH_MEM_CONFIG \
	((SH_MEM_ADDRESS_MODE_64 << SH_MEM_CONFIG__ADDRESS_MODE__SHIFT) | \
	 (SH_MEM_ALIGNMENT_MODE_UNALIGNED_GFX12_1_0 << SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT) | \
	 (3 << SH_MEM_CONFIG__INITIAL_INST_PREFETCH__SHIFT))

static void gfx_v12_1_xcc_disable_gpa_mode(struct amdgpu_device *adev, int xcc_id);
static void gfx_v12_1_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v12_1_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v12_1_set_rlc_funcs(struct amdgpu_device *adev);
static void gfx_v12_1_set_mqd_funcs(struct amdgpu_device *adev);
static void gfx_v12_1_set_imu_funcs(struct amdgpu_device *adev);
static int gfx_v12_1_get_cu_info(struct amdgpu_device *adev,
				 struct amdgpu_cu_info *cu_info);
static uint64_t gfx_v12_1_get_gpu_clock_counter(struct amdgpu_device *adev);
static void gfx_v12_1_xcc_select_se_sh(struct amdgpu_device *adev, u32 se_num,
				       u32 sh_num, u32 instance, int xcc_id);
static void gfx_v12_1_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				     uint32_t val);
static int gfx_v12_1_wait_for_rlc_autoload_complete(struct amdgpu_device *adev);
static void gfx_v12_1_ring_invalidate_tlbs(struct amdgpu_ring *ring,
					   uint16_t pasid, uint32_t flush_type,
					   bool all_hub, uint8_t dst_sel);
static void gfx_v12_1_xcc_set_safe_mode(struct amdgpu_device *adev, int xcc_id);
static void gfx_v12_1_xcc_unset_safe_mode(struct amdgpu_device *adev, int xcc_id);
static void gfx_v12_1_update_perf_clk(struct amdgpu_device *adev,
				      bool enable);
static void gfx_v12_1_xcc_update_perf_clk(struct amdgpu_device *adev,
					 bool enable, int xcc_id);
static int gfx_v12_1_init_cp_compute_microcode_bo(struct amdgpu_device *adev);

static void gfx_v12_1_kiq_set_resources(struct amdgpu_ring *kiq_ring,
					uint64_t queue_mask)
{
	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_SET_RESOURCES, 6));
	amdgpu_ring_write(kiq_ring, PACKET3_SET_RESOURCES_VMID_MASK(0) |
			  PACKET3_SET_RESOURCES_QUEUE_TYPE(0));	/* vmid_mask:0 queue_type:0 (KIQ) */
	amdgpu_ring_write(kiq_ring, lower_32_bits(queue_mask));	/* queue mask lo */
	amdgpu_ring_write(kiq_ring, upper_32_bits(queue_mask));	/* queue mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask lo */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* oac mask */
	amdgpu_ring_write(kiq_ring, 0);
}

static void gfx_v12_1_kiq_map_queues(struct amdgpu_ring *kiq_ring,
				     struct amdgpu_ring *ring)
{
	uint64_t mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
	uint64_t wptr_addr = ring->wptr_gpu_addr;
	uint32_t me = 0, eng_sel = 0;

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_COMPUTE:
		me = 1;
		eng_sel = 0;
		break;
	case AMDGPU_RING_TYPE_MES:
		me = 2;
		eng_sel = 5;
		break;
	default:
		WARN_ON(1);
	}

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
	/* Q_sel:0, vmid:0, vidmem: 1, engine:0, num_Q:1*/
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_MAP_QUEUES_QUEUE_SEL(0) | /* Queue_Sel */
			  PACKET3_MAP_QUEUES_VMID(0) | /* VMID */
			  PACKET3_MAP_QUEUES_QUEUE(ring->queue) |
			  PACKET3_MAP_QUEUES_PIPE(ring->pipe) |
			  PACKET3_MAP_QUEUES_ME((me)) |
			  PACKET3_MAP_QUEUES_QUEUE_TYPE(0) | /*queue_type: normal compute queue */
			  PACKET3_MAP_QUEUES_ALLOC_FORMAT(0) | /* alloc format: all_on_one_pipe */
			  PACKET3_MAP_QUEUES_ENGINE_SEL(eng_sel) |
			  PACKET3_MAP_QUEUES_NUM_QUEUES(1)); /* num_queues: must be 1 */
	amdgpu_ring_write(kiq_ring, PACKET3_MAP_QUEUES_DOORBELL_OFFSET(ring->doorbell_index));
	amdgpu_ring_write(kiq_ring, lower_32_bits(mqd_addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(mqd_addr));
	amdgpu_ring_write(kiq_ring, lower_32_bits(wptr_addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(wptr_addr));
}

static void gfx_v12_1_kiq_unmap_queues(struct amdgpu_ring *kiq_ring,
				       struct amdgpu_ring *ring,
				       enum amdgpu_unmap_queues_action action,
				       u64 gpu_addr, u64 seq)
{
	struct amdgpu_device *adev = kiq_ring->adev;
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	if (adev->enable_mes && !adev->gfx.kiq[0].ring.sched.ready) {
		amdgpu_mes_unmap_legacy_queue(adev, ring, action, gpu_addr,
					      seq, kiq_ring->xcc_id);
		return;
	}

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_UNMAP_QUEUES, 4));
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_UNMAP_QUEUES_ACTION(action) |
			  PACKET3_UNMAP_QUEUES_QUEUE_SEL(0) |
			  PACKET3_UNMAP_QUEUES_ENGINE_SEL(eng_sel) |
			  PACKET3_UNMAP_QUEUES_NUM_QUEUES(1));
	amdgpu_ring_write(kiq_ring,
		  PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET0(ring->doorbell_index));

	if (action == PREEMPT_QUEUES_NO_UNMAP) {
		amdgpu_ring_write(kiq_ring, lower_32_bits(gpu_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(gpu_addr));
		amdgpu_ring_write(kiq_ring, seq);
	} else {
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
	}
}

static void gfx_v12_1_kiq_query_status(struct amdgpu_ring *kiq_ring,
				       struct amdgpu_ring *ring,
				       u64 addr, u64 seq)
{
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_QUERY_STATUS, 5));
	amdgpu_ring_write(kiq_ring,
			  PACKET3_QUERY_STATUS_CONTEXT_ID(0) |
			  PACKET3_QUERY_STATUS_INTERRUPT_SEL(0) |
			  PACKET3_QUERY_STATUS_COMMAND(2));
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_QUERY_STATUS_DOORBELL_OFFSET(ring->doorbell_index) |
			  PACKET3_QUERY_STATUS_ENG_SEL(eng_sel));
	amdgpu_ring_write(kiq_ring, lower_32_bits(addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(addr));
	amdgpu_ring_write(kiq_ring, lower_32_bits(seq));
	amdgpu_ring_write(kiq_ring, upper_32_bits(seq));
}

static void gfx_v12_1_kiq_invalidate_tlbs(struct amdgpu_ring *kiq_ring,
					  uint16_t pasid,
					  uint32_t flush_type,
					  bool all_hub)
{
	gfx_v12_1_ring_invalidate_tlbs(kiq_ring, pasid, flush_type, all_hub, 1);
}

static const struct kiq_pm4_funcs gfx_v12_1_kiq_pm4_funcs = {
	.kiq_set_resources = gfx_v12_1_kiq_set_resources,
	.kiq_map_queues = gfx_v12_1_kiq_map_queues,
	.kiq_unmap_queues = gfx_v12_1_kiq_unmap_queues,
	.kiq_query_status = gfx_v12_1_kiq_query_status,
	.kiq_invalidate_tlbs = gfx_v12_1_kiq_invalidate_tlbs,
	.set_resources_size = 8,
	.map_queues_size = 7,
	.unmap_queues_size = 6,
	.query_status_size = 7,
	.invalidate_tlbs_size = 2,
};

static void gfx_v12_1_set_kiq_pm4_funcs(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i =0; i < num_xcc; i++)
		adev->gfx.kiq[i].pmf = &gfx_v12_1_kiq_pm4_funcs;
}

static void gfx_v12_1_wait_reg_mem(struct amdgpu_ring *ring, int eng_sel,
				   int mem_space, int opt, uint32_t addr0,
				   uint32_t addr1, uint32_t ref,
				   uint32_t mask, uint32_t inv)
{
	if (mem_space == 0) {
		addr0 = soc_v1_0_normalize_xcc_reg_offset(addr0);
		addr1 = soc_v1_0_normalize_xcc_reg_offset(addr1);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring,
			  /* memory (1) or register (0) */
			  (WAIT_REG_MEM_MEM_SPACE(mem_space) |
			   WAIT_REG_MEM_OPERATION(opt) | /* wait */
			   WAIT_REG_MEM_FUNCTION(3) |  /* equal */
			   WAIT_REG_MEM_ENGINE(eng_sel)));

	if (mem_space)
		BUG_ON(addr0 & 0x3); /* Dword align */
	amdgpu_ring_write(ring, addr0);
	amdgpu_ring_write(ring, addr1);
	amdgpu_ring_write(ring, ref);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, inv); /* poll interval */
}

static int gfx_v12_1_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t scratch_reg0_offset, xcc_offset;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	/* Use register offset which is local to XCC in the packet */
	xcc_offset = SOC15_REG_OFFSET(GC, 0, regSCRATCH_REG0);
	scratch_reg0_offset = SOC15_REG_OFFSET(GC, GET_INST(GC, ring->xcc_id), regSCRATCH_REG0);
	WREG32(scratch_reg0_offset, 0xCAFEDEAD);
	tmp = RREG32(scratch_reg0_offset);

	r = amdgpu_ring_alloc(ring, 5);
	if (r) {
		dev_err(adev->dev,
			"amdgpu: cp failed to lock ring %d (%d).\n",
			ring->idx, r);
		return r;
	}

	if (ring->funcs->type == AMDGPU_RING_TYPE_KIQ) {
		gfx_v12_1_ring_emit_wreg(ring, xcc_offset, 0xDEADBEEF);
	} else {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
		amdgpu_ring_write(ring, xcc_offset -
				  PACKET3_SET_UCONFIG_REG_START);
		amdgpu_ring_write(ring, 0xDEADBEEF);
	}
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch_reg0_offset);
		if (tmp == 0xDEADBEEF)
			break;
		if (amdgpu_emu_mode == 1)
			msleep(1);
		else
			udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;
	return r;
}

static int gfx_v12_1_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	uint64_t gpu_addr;
	volatile uint32_t *cpu_ptr;
	long r;

	/* MES KIQ fw hasn't indirect buffer support for now */
	if (adev->enable_mes_kiq &&
	    ring->funcs->type == AMDGPU_RING_TYPE_KIQ)
		return 0;

	memset(&ib, 0, sizeof(ib));

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	adev->wb.wb[index] = cpu_to_le32(0xCAFEDEAD);
	cpu_ptr = &adev->wb.wb[index];

	r = amdgpu_ib_get(adev, NULL, 16, AMDGPU_IB_POOL_DIRECT, &ib);
	if (r) {
		dev_err(adev->dev, "amdgpu: failed to get ib (%ld).\n", r);
		goto err1;
	}

	ib.ptr[0] = PACKET3(PACKET3_WRITE_DATA, 3);
	ib.ptr[1] = WRITE_DATA_DST_SEL(5) | WR_CONFIRM;
	ib.ptr[2] = lower_32_bits(gpu_addr);
	ib.ptr[3] = upper_32_bits(gpu_addr);
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err2;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		r = -ETIMEDOUT;
		goto err2;
	} else if (r < 0) {
		goto err2;
	}

	if (le32_to_cpu(*cpu_ptr) == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;
err2:
	amdgpu_ib_free(&ib, NULL);
	dma_fence_put(f);
err1:
	amdgpu_device_wb_free(adev, index);
	return r;
}

static void gfx_v12_1_free_microcode(struct amdgpu_device *adev)
{
	amdgpu_ucode_release(&adev->gfx.rlc_fw);
	amdgpu_ucode_release(&adev->gfx.mec_fw);

	kfree(adev->gfx.rlc.register_list_format);
}

static int gfx_v12_1_init_toc_microcode(struct amdgpu_device *adev, const char *ucode_prefix)
{
	const struct psp_firmware_header_v1_0 *toc_hdr;
	int err = 0;

	err = amdgpu_ucode_request(adev, &adev->psp.toc_fw,
				   AMDGPU_UCODE_REQUIRED,
				   "amdgpu/%s_toc.bin", ucode_prefix);
	if (err)
		goto out;

	toc_hdr = (const struct psp_firmware_header_v1_0 *)adev->psp.toc_fw->data;
	adev->psp.toc.fw_version = le32_to_cpu(toc_hdr->header.ucode_version);
	adev->psp.toc.feature_version = le32_to_cpu(toc_hdr->sos.fw_version);
	adev->psp.toc.size_bytes = le32_to_cpu(toc_hdr->header.ucode_size_bytes);
	adev->psp.toc.start_addr = (uint8_t *)toc_hdr +
			le32_to_cpu(toc_hdr->header.ucode_array_offset_bytes);
	return 0;
out:
	amdgpu_ucode_release(&adev->psp.toc_fw);
	return err;
}

static int gfx_v12_1_init_microcode(struct amdgpu_device *adev)
{
	char ucode_prefix[15];
	int err;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	uint16_t version_major;
	uint16_t version_minor;

	DRM_DEBUG("\n");

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix, sizeof(ucode_prefix));

	if (!amdgpu_sriov_vf(adev)) {
		err = amdgpu_ucode_request(adev, &adev->gfx.rlc_fw,
					   AMDGPU_UCODE_REQUIRED,
					   "amdgpu/%s_rlc.bin", ucode_prefix);
		if (err)
			goto out;
		rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
		version_major = le16_to_cpu(rlc_hdr->header.header_version_major);
		version_minor = le16_to_cpu(rlc_hdr->header.header_version_minor);
		err = amdgpu_gfx_rlc_init_microcode(adev, version_major, version_minor);
		if (err)
			goto out;
	}

	err = amdgpu_ucode_request(adev, &adev->gfx.mec_fw,
				   AMDGPU_UCODE_REQUIRED,
				   "amdgpu/%s_mec.bin", ucode_prefix);
	if (err)
		goto out;
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_RS64_MEC);
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK);
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK);
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK);
	amdgpu_gfx_cp_init_microcode(adev, AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO)
		err = gfx_v12_1_init_toc_microcode(adev, ucode_prefix);

	/* only one MEC for gfx 12 */
	adev->gfx.mec2_fw = NULL;

	if (adev->gfx.imu.funcs) {
		if (adev->gfx.imu.funcs->init_microcode) {
			err = adev->gfx.imu.funcs->init_microcode(adev);
			if (err)
				dev_err(adev->dev, "Failed to load imu firmware!\n");
		}
	}

out:
	if (err) {
		amdgpu_ucode_release(&adev->gfx.rlc_fw);
		amdgpu_ucode_release(&adev->gfx.mec_fw);
	}

	return err;
}

static u32 gfx_v12_1_get_csb_size(struct amdgpu_device *adev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	count += 1;

	for (sect = gfx12_cs_data; sect->section != NULL; ++sect) {
		if (sect->id == SECT_CONTEXT) {
			for (ext = sect->section; ext->extent != NULL; ++ext)
				count += 2 + ext->reg_count;
		} else
			return 0;
	}

	return count;
}

static void gfx_v12_1_get_csb_buffer(struct amdgpu_device *adev, u32 *buffer)
{
	u32 count = 0, clustercount = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (adev->gfx.rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	count += 1;

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		if (sect->id == SECT_CONTEXT) {
			for (ext = sect->section; ext->extent != NULL; ++ext) {
				clustercount++;
				buffer[count++] = ext->reg_count;
				buffer[count++] = ext->reg_index;

				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			}
		} else
			return;
	}

	buffer[0] = clustercount;
}

static void gfx_v12_1_rlc_fini(struct amdgpu_device *adev)
{
	/* clear state block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.clear_state_obj,
			&adev->gfx.rlc.clear_state_gpu_addr,
			(void **)&adev->gfx.rlc.cs_ptr);

	/* jump table block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.cp_table_obj,
			&adev->gfx.rlc.cp_table_gpu_addr,
			(void **)&adev->gfx.rlc.cp_table_ptr);
}

static void gfx_v12_1_init_rlcg_reg_access_ctrl(struct amdgpu_device *adev)
{
	int xcc_id, num_xcc;
	struct amdgpu_rlcg_reg_access_ctrl *reg_access_ctrl;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		reg_access_ctrl = &adev->gfx.rlc.reg_access_ctrl[GET_INST(GC, xcc_id)];

		reg_access_ctrl->grbm_cntl =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regGRBM_GFX_CNTL);
		reg_access_ctrl->grbm_idx =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regGRBM_GFX_INDEX);

		reg_access_ctrl->vfi_cmd =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_VFI_CMD);
		reg_access_ctrl->vfi_stat =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_VFI_STAT);
		reg_access_ctrl->vfi_addr =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_VFI_ADDR);
		reg_access_ctrl->vfi_data =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_VFI_DATA);
		reg_access_ctrl->vfi_grbm_cntl =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_VFI_GRBM_GFX_CNTL);
		reg_access_ctrl->vfi_grbm_idx =
			SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_VFI_GRBM_GFX_INDEX);
		reg_access_ctrl->vfi_grbm_cntl_data = 0;
		reg_access_ctrl->vfi_grbm_idx_data = 0;
	}
	adev->gfx.rlc.rlcg_reg_access_supported = true;
}

static int gfx_v12_1_rlc_init(struct amdgpu_device *adev)
{
	const struct cs_section_def *cs_data;
	int r, i, num_xcc;

	adev->gfx.rlc.cs_data = gfx12_cs_data;

	cs_data = adev->gfx.rlc.cs_data;

	if (cs_data) {
		/* init clear state block */
		r = amdgpu_gfx_rlc_init_csb(adev);
		if (r)
			return r;
	}

	/* init spm vmid with 0xf */
	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		if (adev->gfx.rlc.funcs->update_spm_vmid)
			adev->gfx.rlc.funcs->update_spm_vmid(adev, i, NULL, 0xf);
	}

	return 0;
}

static void gfx_v12_1_mec_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.mec.hpd_eop_obj, NULL, NULL);
	amdgpu_bo_free_kernel(&adev->gfx.mec.mec_fw_obj, NULL, NULL);
	amdgpu_bo_free_kernel(&adev->gfx.mec.mec_fw_data_obj, NULL, NULL);
}

static int gfx_v12_1_mec_init(struct amdgpu_device *adev)
{
	int r, i, num_xcc;
	u32 *hpd;
	size_t mec_hpd_size;

	bitmap_zero(adev->gfx.mec_bitmap[0].queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		bitmap_zero(adev->gfx.mec_bitmap[i].queue_bitmap,
			    AMDGPU_MAX_COMPUTE_QUEUES);

	/* take ownership of the relevant compute queues */
	amdgpu_gfx_compute_queue_acquire(adev);
	mec_hpd_size = adev->gfx.num_compute_rings *
		       GFX12_MEC_HPD_SIZE * num_xcc;

	if (mec_hpd_size) {
		r = amdgpu_bo_create_reserved(adev, mec_hpd_size, PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_GTT,
					      &adev->gfx.mec.hpd_eop_obj,
					      &adev->gfx.mec.hpd_eop_gpu_addr,
					      (void **)&hpd);
		if (r) {
			dev_warn(adev->dev, "(%d) create HDP EOP bo failed\n", r);
			gfx_v12_1_mec_fini(adev);
			return r;
		}

		memset(hpd, 0, mec_hpd_size);

		amdgpu_bo_kunmap(adev->gfx.mec.hpd_eop_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);
	}

	return 0;
}

static uint32_t wave_read_ind(struct amdgpu_device *adev,
			      uint32_t xcc_id, uint32_t wave,
			      uint32_t address)
{
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT));
	return RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_IND_DATA);
}

static void wave_read_regs(struct amdgpu_device *adev,
			   uint32_t xcc_id, uint32_t wave,
			   uint32_t thread, uint32_t regno,
			   uint32_t num, uint32_t *out)
{
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(regno << SQ_IND_INDEX__INDEX__SHIFT) |
		(thread << SQ_IND_INDEX__WORKITEM_ID__SHIFT) |
		(SQ_IND_INDEX__AUTO_INCR_MASK));
	while (num--)
		*(out++) = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_IND_DATA);
}

static void gfx_v12_1_read_wave_data(struct amdgpu_device *adev,
				     uint32_t xcc_id,
				     uint32_t simd, uint32_t wave,
				     uint32_t *dst, int *no_fields)
{
	/* in gfx12 the SIMD_ID is specified as part of the INSTANCE
	 * field when performing a select_se_sh so it should be
	 * zero here */
	WARN_ON(simd != 0);

	/* type 4 wave data */
	dst[(*no_fields)++] = 4;
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_STATUS);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_PC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_PC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_EXEC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_EXEC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_HW_ID1);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_HW_ID2);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_GPR_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_LDS_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_IB_STS);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_IB_STS2);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_IB_DBG1);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_M0);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_MODE);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_STATE_PRIV);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_EXCP_FLAG_PRIV);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_EXCP_FLAG_USER);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_TRAP_CTRL);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_ACTIVE);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_VALID_AND_IDLE);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_DVGPR_ALLOC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_DVGPR_ALLOC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, xcc_id, wave, ixSQ_WAVE_SCHED_MODE);
}

static void gfx_v12_1_read_wave_sgprs(struct amdgpu_device *adev,
				      uint32_t xcc_id, uint32_t simd,
				      uint32_t wave, uint32_t start,
				      uint32_t size, uint32_t *dst)
{
	WARN_ON(simd != 0);

	wave_read_regs(adev, xcc_id, wave, 0,
		       start + SQIND_WAVE_SGPRS_OFFSET,
		       size, dst);
}

static void gfx_v12_1_read_wave_vgprs(struct amdgpu_device *adev,
				      uint32_t xcc_id, uint32_t simd,
				      uint32_t wave, uint32_t thread,
				      uint32_t start, uint32_t size,
				      uint32_t *dst)
{
	wave_read_regs(adev, xcc_id, wave, thread,
		       start + SQIND_WAVE_VGPRS_OFFSET,
		       size, dst);
}

static void gfx_v12_1_select_me_pipe_q(struct amdgpu_device *adev,
				       u32 me, u32 pipe, u32 q, u32 vm, u32 xcc_id)
{
	soc_v1_0_grbm_select(adev, me, pipe, q, vm, GET_INST(GC, xcc_id));
}

static int gfx_v12_1_get_xccs_per_xcp(struct amdgpu_device *adev)
{
	/* Fill this in when the interface is ready */
	return 1;
}

static int gfx_v12_1_ih_to_xcc_inst(struct amdgpu_device *adev, int ih_node)
{
	int logic_xcc;
	int xcc = (ih_node & 0x7) - 2 + (ih_node >> 3) * 4;

	for (logic_xcc = 0; logic_xcc < NUM_XCC(adev->gfx.xcc_mask); logic_xcc++) {
		if (xcc == GET_INST(GC, logic_xcc))
			return logic_xcc;
	}

	dev_err(adev->dev, "Couldn't find xcc mapping from IH node");
	return -EINVAL;
}

static const struct amdgpu_gfx_funcs gfx_v12_1_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v12_1_get_gpu_clock_counter,
	.select_se_sh = &gfx_v12_1_xcc_select_se_sh,
	.read_wave_data = &gfx_v12_1_read_wave_data,
	.read_wave_sgprs = &gfx_v12_1_read_wave_sgprs,
	.read_wave_vgprs = &gfx_v12_1_read_wave_vgprs,
	.select_me_pipe_q = &gfx_v12_1_select_me_pipe_q,
	.update_perfmon_mgcg = &gfx_v12_1_update_perf_clk,
	.get_xccs_per_xcp = &gfx_v12_1_get_xccs_per_xcp,
	.ih_node_to_logical_xcc = &gfx_v12_1_ih_to_xcc_inst,
};

static int gfx_v12_1_gpu_early_init(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 1, 0):
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		break;
	default:
		BUG();
		break;
	}

	return 0;
}

static int gfx_v12_1_compute_ring_init(struct amdgpu_device *adev, int ring_id,
				       int xcc_id, int mec, int pipe, int queue)
{
	int r;
	unsigned irq_type;
	struct amdgpu_ring *ring;
	unsigned int hw_prio;
	uint32_t xcc_doorbell_start;

	ring = &adev->gfx.compute_ring[xcc_id * adev->gfx.num_compute_rings +
				       ring_id];

	/* mec0 is me1 */
	ring->xcc_id = xcc_id;
	ring->me = mec + 1;
	ring->pipe = pipe;
	ring->queue = queue;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	xcc_doorbell_start = adev->doorbell_index.mec_ring0 +
			     xcc_id * adev->doorbell_index.xcc_doorbell_range;
	ring->doorbell_index = (xcc_doorbell_start + ring_id) << 1;
	ring->eop_gpu_addr = adev->gfx.mec.hpd_eop_gpu_addr +
			     (ring_id + xcc_id * adev->gfx.num_compute_rings) *
			     GFX12_MEC_HPD_SIZE;
	ring->vm_hub = AMDGPU_GFXHUB(xcc_id);
	sprintf(ring->name, "comp_%d.%d.%d.%d",
			ring->xcc_id, ring->me, ring->pipe, ring->queue);

	irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP
		+ ((ring->me - 1) * adev->gfx.mec.num_pipe_per_mec)
		+ ring->pipe;
	hw_prio = amdgpu_gfx_is_high_priority_compute_queue(adev, ring) ?
			AMDGPU_GFX_PIPE_PRIO_HIGH : AMDGPU_GFX_PIPE_PRIO_NORMAL;
	/* type-2 packets are deprecated on MEC, use type-3 instead */
	r = amdgpu_ring_init(adev, ring, 1024, &adev->gfx.eop_irq, irq_type,
			     hw_prio, NULL);
	if (r)
		return r;

	return 0;
}

static struct {
	SOC24_FIRMWARE_ID	id;
	unsigned int		offset;
	unsigned int		size;
	unsigned int		size_x16;
	unsigned int		num_inst;
} rlc_autoload_info[SOC24_FIRMWARE_ID_MAX];

#define RLC_TOC_OFFSET_DWUNIT   8
#define RLC_SIZE_MULTIPLE       1024
#define RLC_TOC_UMF_SIZE_inM	23ULL
#define RLC_TOC_FORMAT_API	165ULL

#define RLC_NUM_INS_CODE0   1
#define RLC_NUM_INS_CODE1   8
#define RLC_NUM_INS_CODE2   2
#define RLC_NUM_INS_CODE3   16

static void gfx_v12_1_parse_rlc_toc(struct amdgpu_device *adev, void *rlc_toc)
{
	RLC_TABLE_OF_CONTENT_V2 *ucode = rlc_toc;

	while (ucode && (ucode->id > SOC24_FIRMWARE_ID_INVALID)) {
		rlc_autoload_info[ucode->id].id = ucode->id;
		rlc_autoload_info[ucode->id].offset =
			ucode->offset * RLC_TOC_OFFSET_DWUNIT * 4;
		rlc_autoload_info[ucode->id].size =
			ucode->size_x16 ? ucode->size * RLC_SIZE_MULTIPLE * 4 :
					  ucode->size * 4;
		switch (ucode->vfflr_image_code) {
		case 0:
			rlc_autoload_info[ucode->id].num_inst =
				RLC_NUM_INS_CODE0;
			break;
		case 1:
			rlc_autoload_info[ucode->id].num_inst =
				RLC_NUM_INS_CODE1;
			break;
		case 2:
			rlc_autoload_info[ucode->id].num_inst =
				RLC_NUM_INS_CODE2;
			break;
		case 3:
			rlc_autoload_info[ucode->id].num_inst =
				RLC_NUM_INS_CODE3;
			break;
		default:
			dev_err(adev->dev,
				"Invalid Instance number detected\n");
			break;
		}
		ucode++;
	}
}

static uint32_t gfx_v12_1_calc_toc_total_size(struct amdgpu_device *adev)
{
	uint32_t total_size = 0;
	SOC24_FIRMWARE_ID id;

	gfx_v12_1_parse_rlc_toc(adev, adev->psp.toc.start_addr);

	for (id = SOC24_FIRMWARE_ID_RLC_G_UCODE; id < SOC24_FIRMWARE_ID_MAX; id++)
		total_size += rlc_autoload_info[id].size;

	/* In case the offset in rlc toc ucode is aligned */
	if (total_size < rlc_autoload_info[SOC24_FIRMWARE_ID_MAX-1].offset)
		total_size = rlc_autoload_info[SOC24_FIRMWARE_ID_MAX-1].offset +
			rlc_autoload_info[SOC24_FIRMWARE_ID_MAX-1].size;
	if (total_size < (RLC_TOC_UMF_SIZE_inM << 20))
		total_size = RLC_TOC_UMF_SIZE_inM << 20;

	return total_size;
}

static int gfx_v12_1_rlc_autoload_buffer_init(struct amdgpu_device *adev)
{
	int r;
	uint32_t total_size;

	total_size = gfx_v12_1_calc_toc_total_size(adev);

	r = amdgpu_bo_create_reserved(adev, total_size, 64 * 1024,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->gfx.rlc.rlc_autoload_bo,
				      &adev->gfx.rlc.rlc_autoload_gpu_addr,
				      (void **)&adev->gfx.rlc.rlc_autoload_ptr);

	if (r) {
		dev_err(adev->dev, "(%d) failed to create fw autoload bo\n", r);
		return r;
	}

	return 0;
}

static void gfx_v12_1_rlc_backdoor_autoload_copy_ucode(struct amdgpu_device *adev,
						       SOC24_FIRMWARE_ID id,
						       const void *fw_data,
						       uint32_t fw_size)
{
	uint32_t toc_offset;
	uint32_t toc_fw_size, toc_fw_inst_size;
	char *ptr = adev->gfx.rlc.rlc_autoload_ptr;
	int i, num_inst;

	if (id <= SOC24_FIRMWARE_ID_INVALID || id >= SOC24_FIRMWARE_ID_MAX)
		return;

	toc_offset = rlc_autoload_info[id].offset;
	toc_fw_size = rlc_autoload_info[id].size;
	num_inst = rlc_autoload_info[id].num_inst;
	toc_fw_inst_size = toc_fw_size / num_inst;

	if (fw_size == 0)
		fw_size = toc_fw_inst_size;

	if (fw_size > toc_fw_inst_size)
		fw_size = toc_fw_inst_size;

	for (i = 0; i < num_inst; i++) {
		if ((num_inst == RLC_NUM_INS_CODE0) ||
		    ((1 << (i / 2)) & adev->gfx.xcc_mask)) {
			memcpy(ptr + toc_offset + i * toc_fw_inst_size, fw_data, fw_size);

			if (fw_size < toc_fw_inst_size)
				memset(ptr + toc_offset + fw_size + i * toc_fw_inst_size,
				       0, toc_fw_inst_size - fw_size);
		}
	}
}

static void
gfx_v12_1_rlc_backdoor_autoload_copy_toc_ucode(struct amdgpu_device *adev)
{
	void *data;
	uint32_t size;
	uint32_t *toc_ptr;

	data = adev->psp.toc.start_addr;
	size = rlc_autoload_info[SOC24_FIRMWARE_ID_RLC_TOC].size;

	toc_ptr = (uint32_t *)data + size / 4 - 2;
	*toc_ptr = (RLC_TOC_FORMAT_API << 24) | 0x1;

	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RLC_TOC,
						   data, size);
}

static void
gfx_v12_1_rlc_backdoor_autoload_copy_gfx_ucode(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	uint32_t fw_size;
	const struct gfx_firmware_header_v2_0 *cpv2_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	const struct rlc_firmware_header_v2_1 *rlcv21_hdr;
	const struct rlc_firmware_header_v2_2 *rlcv22_hdr;
	uint16_t version_major, version_minor;

	/* mec ucode */
	cpv2_hdr = (const struct gfx_firmware_header_v2_0 *)
		adev->gfx.mec_fw->data;
	/* instruction */
	fw_data = (const __le32 *) (adev->gfx.mec_fw->data +
		le32_to_cpu(cpv2_hdr->ucode_offset_bytes));
	fw_size = le32_to_cpu(cpv2_hdr->ucode_size_bytes);
	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RS64_MEC,
						   fw_data, fw_size);
	/* data */
	fw_data = (const __le32 *) (adev->gfx.mec_fw->data +
		le32_to_cpu(cpv2_hdr->data_offset_bytes));
	fw_size = le32_to_cpu(cpv2_hdr->data_size_bytes);
	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RS64_MEC_P0_STACK,
						   fw_data, fw_size);
	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RS64_MEC_P1_STACK,
						   fw_data, fw_size);
	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RS64_MEC_P2_STACK,
						   fw_data, fw_size);
	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RS64_MEC_P3_STACK,
						   fw_data, fw_size);

	/* rlc ucode */
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)
		adev->gfx.rlc_fw->data;
	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			le32_to_cpu(rlc_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(rlc_hdr->header.ucode_size_bytes);
	gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RLC_G_UCODE,
						   fw_data, fw_size);

	version_major = le16_to_cpu(rlc_hdr->header.header_version_major);
	version_minor = le16_to_cpu(rlc_hdr->header.header_version_minor);
	if (version_major == 2) {
		if (version_minor >= 1) {
			rlcv21_hdr = (const struct rlc_firmware_header_v2_1 *)adev->gfx.rlc_fw->data;

			fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
					le32_to_cpu(rlcv21_hdr->save_restore_list_gpm_offset_bytes));
			fw_size = le32_to_cpu(rlcv21_hdr->save_restore_list_gpm_size_bytes);
			gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RLCG_SCRATCH,
						   fw_data, fw_size);

			fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
					le32_to_cpu(rlcv21_hdr->save_restore_list_srm_offset_bytes));
			fw_size = le32_to_cpu(rlcv21_hdr->save_restore_list_srm_size_bytes);
			gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RLC_SRM_ARAM,
						   fw_data, fw_size);
		}
		if (version_minor >= 2) {
			rlcv22_hdr = (const struct rlc_firmware_header_v2_2 *)adev->gfx.rlc_fw->data;

			fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
					le32_to_cpu(rlcv22_hdr->rlc_iram_ucode_offset_bytes));
			fw_size = le32_to_cpu(rlcv22_hdr->rlc_iram_ucode_size_bytes);
			gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RLX6_UCODE,
						   fw_data, fw_size);

			fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
					le32_to_cpu(rlcv22_hdr->rlc_dram_ucode_offset_bytes));
			fw_size = le32_to_cpu(rlcv22_hdr->rlc_dram_ucode_size_bytes);
			gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_RLX6_DRAM_BOOT,
						   fw_data, fw_size);
		}
	}
}

static void
gfx_v12_1_rlc_backdoor_autoload_copy_sdma_ucode(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	uint32_t fw_size;
	const struct sdma_firmware_header_v3_0 *sdma_hdr;

	if (adev->sdma.instance[0].fw) {
		sdma_hdr = (const struct sdma_firmware_header_v3_0 *)
			adev->sdma.instance[0].fw->data;
		fw_data = (const __le32 *) (adev->sdma.instance[0].fw->data +
				le32_to_cpu(sdma_hdr->ucode_offset_bytes));
		fw_size = le32_to_cpu(sdma_hdr->ucode_size_bytes);

		gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, SOC24_FIRMWARE_ID_SDMA_UCODE_TH0,
							   fw_data, fw_size);
	}
}

static void
gfx_v12_1_rlc_backdoor_autoload_copy_mes_ucode(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	unsigned fw_size;
	const struct mes_firmware_header_v1_0 *mes_hdr;
	int pipe, ucode_id, data_id;

	for (pipe = 0; pipe < 2; pipe++) {
		if (pipe == 0) {
			ucode_id = SOC24_FIRMWARE_ID_RS64_MES_P0;
			data_id  = SOC24_FIRMWARE_ID_RS64_MES_P0_STACK;
		} else {
			ucode_id = SOC24_FIRMWARE_ID_RS64_MES_P1;
			data_id  = SOC24_FIRMWARE_ID_RS64_MES_P1_STACK;
		}

		mes_hdr = (const struct mes_firmware_header_v1_0 *)
			adev->mes.fw[pipe]->data;

		fw_data = (const __le32 *)(adev->mes.fw[pipe]->data +
				le32_to_cpu(mes_hdr->mes_ucode_offset_bytes));
		fw_size = le32_to_cpu(mes_hdr->mes_ucode_size_bytes);

		gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, ucode_id, fw_data, fw_size);

		fw_data = (const __le32 *)(adev->mes.fw[pipe]->data +
				le32_to_cpu(mes_hdr->mes_ucode_data_offset_bytes));
		fw_size = le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes);

		gfx_v12_1_rlc_backdoor_autoload_copy_ucode(adev, data_id, fw_data, fw_size);
	}
}

static int gfx_v12_1_rlc_backdoor_autoload_enable(struct amdgpu_device *adev)
{
	uint32_t rlc_g_offset, rlc_g_size;
	uint64_t gpu_addr;
	uint32_t data;
	int i, num_xcc;

	/* RLC autoload sequence 2: copy ucode */
	gfx_v12_1_rlc_backdoor_autoload_copy_sdma_ucode(adev);
	gfx_v12_1_rlc_backdoor_autoload_copy_gfx_ucode(adev);
	gfx_v12_1_rlc_backdoor_autoload_copy_mes_ucode(adev);
	gfx_v12_1_rlc_backdoor_autoload_copy_toc_ucode(adev);

	rlc_g_offset = rlc_autoload_info[SOC24_FIRMWARE_ID_RLC_G_UCODE].offset;
	rlc_g_size = rlc_autoload_info[SOC24_FIRMWARE_ID_RLC_G_UCODE].size;
	gpu_addr = adev->gfx.rlc.rlc_autoload_gpu_addr + rlc_g_offset - adev->gmc.vram_start;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGFX_IMU_RLC_BOOTLOADER_ADDR_HI,
			     upper_32_bits(gpu_addr));
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGFX_IMU_RLC_BOOTLOADER_ADDR_LO,
			     lower_32_bits(gpu_addr));
		WREG32_SOC15(GC, GET_INST(GC, i),
			     regGFX_IMU_RLC_BOOTLOADER_SIZE,
			     rlc_g_size);
	}

	if (adev->gfx.imu.funcs) {
		/* RLC autoload sequence 3: load IMU fw */
		if (adev->gfx.imu.funcs->load_microcode)
			adev->gfx.imu.funcs->load_microcode(adev);
	}

	/* unhalt rlc to start autoload */
	for (i = 0; i < num_xcc; i++) {
		data = RREG32_SOC15(GC, GET_INST(GC, i), regRLC_GPM_THREAD_ENABLE);
		data = REG_SET_FIELD(data, RLC_GPM_THREAD_ENABLE, THREAD0_ENABLE, 1);
		data = REG_SET_FIELD(data, RLC_GPM_THREAD_ENABLE, THREAD1_ENABLE, 1);
		WREG32_SOC15(GC, GET_INST(GC, i), regRLC_GPM_THREAD_ENABLE, data);
		WREG32_SOC15(GC, GET_INST(GC, i), regRLC_CNTL, RLC_CNTL__RLC_ENABLE_F32_MASK);
	}

	return 0;
}

static int gfx_v12_1_sw_init(struct amdgpu_ip_block *ip_block)
{
	int i, j, k, r, ring_id = 0;
	unsigned num_compute_rings;
	int xcc_id, num_xcc;
	struct amdgpu_device *adev = ip_block->adev;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 1, 0):
		adev->gfx.mec.num_mec = 1;
		adev->gfx.mec.num_pipe_per_mec = 4;
		adev->gfx.mec.num_queue_per_pipe = 8;
		break;
	default:
		adev->gfx.mec.num_mec = 2;
		adev->gfx.mec.num_pipe_per_mec = 2;
		adev->gfx.mec.num_queue_per_pipe = 4;
		break;
	}

	/* recalculate compute rings to use based on hardware configuration */
	num_compute_rings = (adev->gfx.mec.num_pipe_per_mec *
			     adev->gfx.mec.num_queue_per_pipe) / 2;
	adev->gfx.num_compute_rings = min(adev->gfx.num_compute_rings,
					  num_compute_rings);

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	/* EOP Event */
	r = amdgpu_irq_add_id(adev, SOC_V1_0_IH_CLIENTID_GRBM_CP,
			      GFX_12_1_0__SRCID__CP_EOP_INTERRUPT,
			      &adev->gfx.eop_irq);
	if (r)
		return r;

	/* Privileged reg */
	r = amdgpu_irq_add_id(adev, SOC_V1_0_IH_CLIENTID_GRBM_CP,
			      GFX_12_1_0__SRCID__CP_PRIV_REG_FAULT,
			      &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	/* Privileged inst */
	r = amdgpu_irq_add_id(adev, SOC_V1_0_IH_CLIENTID_GRBM_CP,
			      GFX_12_1_0__SRCID__CP_PRIV_INSTR_FAULT,
			      &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	adev->gfx.gfx_current_status = AMDGPU_GFX_NORMAL_MODE;

	r = gfx_v12_1_rlc_init(adev);
	if (r) {
		dev_err(adev->dev, "Failed to init rlc BOs!\n");
		return r;
	}

	r = gfx_v12_1_mec_init(adev);
	if (r) {
		dev_err(adev->dev, "Failed to init MEC BOs!\n");
		return r;
	}

	/* set up the compute queues - allocate horizontally across pipes */
	for (xcc_id = 0; xcc_id < num_xcc; xcc_id++) {
		ring_id = 0;
		for (i = 0; i < adev->gfx.mec.num_mec; ++i) {
			for (j = 0; j < adev->gfx.mec.num_queue_per_pipe; j++) {
				for (k = 0; k < adev->gfx.mec.num_pipe_per_mec; k++) {
					if (!amdgpu_gfx_is_mec_queue_enabled(adev,
								xcc_id, i, k, j))
						continue;

					r = gfx_v12_1_compute_ring_init(adev, ring_id,
								xcc_id, i, k, j);
					if (r)
						return r;

					ring_id++;
				}
			}
		}

		if (!adev->enable_mes_kiq) {
			r = amdgpu_gfx_kiq_init(adev, GFX12_MEC_HPD_SIZE, xcc_id);
			if (r) {
				dev_err(adev->dev, "Failed to init KIQ BOs!\n");
				return r;
			}

			r = amdgpu_gfx_kiq_init_ring(adev, xcc_id);
			if (r)
				return r;
		}

		r = amdgpu_gfx_mqd_sw_init(adev, sizeof(struct v12_1_compute_mqd), xcc_id);
		if (r)
			return r;
	}

	/* allocate visible FB for rlc auto-loading fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
		r = gfx_v12_1_rlc_autoload_buffer_init(adev);
		if (r)
			return r;
	} else if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		r = gfx_v12_1_init_cp_compute_microcode_bo(adev);
		if (r)
			return r;
	}

	r = gfx_v12_1_gpu_early_init(adev);
	if (r)
		return r;

	r = amdgpu_gfx_sysfs_init(adev);
	if (r)
		return r;

	return 0;
}

static void gfx_v12_1_rlc_autoload_buffer_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.rlc.rlc_autoload_bo,
			&adev->gfx.rlc.rlc_autoload_gpu_addr,
			(void **)&adev->gfx.rlc.rlc_autoload_ptr);
}

static int gfx_v12_1_sw_fini(struct amdgpu_ip_block *ip_block)
{
	int i, num_xcc;
	struct amdgpu_device *adev = ip_block->adev;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < adev->gfx.num_compute_rings * num_xcc; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	for (i = 0; i < num_xcc; i++) {
		amdgpu_gfx_mqd_sw_fini(adev, i);

		if (!adev->enable_mes_kiq) {
			amdgpu_gfx_kiq_free_ring(&adev->gfx.kiq[i].ring);
			amdgpu_gfx_kiq_fini(adev, i);
		}
	}

	gfx_v12_1_rlc_fini(adev);
	gfx_v12_1_mec_fini(adev);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO)
		gfx_v12_1_rlc_autoload_buffer_fini(adev);

	gfx_v12_1_free_microcode(adev);
	amdgpu_gfx_sysfs_fini(adev);

	return 0;
}

static void gfx_v12_1_xcc_select_se_sh(struct amdgpu_device *adev, u32 se_num,
				       u32 sh_num, u32 instance, int xcc_id)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_INDEX,
				     instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES,
				     1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SA_BROADCAST_WRITES,
				     1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SA_INDEX, sh_num);

	WREG32_SOC15_RLC_SHADOW_EX(reg, GC, GET_INST(GC, xcc_id), regGRBM_GFX_INDEX, data);
}

static u32 gfx_v12_1_get_sa_active_bitmap(struct amdgpu_device *adev,
					  int xcc_id)
{
	u32 gc_disabled_sa_mask, gc_user_disabled_sa_mask, sa_mask;

	gc_disabled_sa_mask = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCC_GC_SA_UNIT_DISABLE);
	gc_disabled_sa_mask = REG_GET_FIELD(gc_disabled_sa_mask,
					    CC_GC_SA_UNIT_DISABLE,
					    SA_DISABLE);
	gc_user_disabled_sa_mask = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regGC_USER_SA_UNIT_DISABLE);
	gc_user_disabled_sa_mask = REG_GET_FIELD(gc_user_disabled_sa_mask,
						 GC_USER_SA_UNIT_DISABLE,
						 SA_DISABLE);
	sa_mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_sh_per_se *
					    adev->gfx.config.max_shader_engines);

	return sa_mask & (~(gc_disabled_sa_mask | gc_user_disabled_sa_mask));
}

static u32 gfx_v12_1_get_rb_active_bitmap(struct amdgpu_device *adev,
					  int xcc_id)
{
	u32 gc_disabled_rb_mask, gc_user_disabled_rb_mask;
	u32 rb_mask;

	gc_disabled_rb_mask = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
					   regCC_RB_BACKEND_DISABLE);
	gc_disabled_rb_mask = REG_GET_FIELD(gc_disabled_rb_mask,
					    CC_RB_BACKEND_DISABLE,
					    BACKEND_DISABLE);
	gc_user_disabled_rb_mask = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
						regGC_USER_RB_BACKEND_DISABLE);
	gc_user_disabled_rb_mask = REG_GET_FIELD(gc_user_disabled_rb_mask,
						 GC_USER_RB_BACKEND_DISABLE,
						 BACKEND_DISABLE);
	rb_mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_backends_per_se *
					    adev->gfx.config.max_shader_engines);

	return rb_mask & (~(gc_disabled_rb_mask | gc_user_disabled_rb_mask));
}

static void gfx_v12_1_setup_rb(struct amdgpu_device *adev)
{
	u32 rb_bitmap_width_per_sa;
	u32 max_sa;
	u32 active_sa_bitmap;
	u32 global_active_rb_bitmap;
	u32 active_rb_bitmap = 0;
	u32 i;
	int xcc_id;

	for (xcc_id = 0; xcc_id < NUM_XCC(adev->gfx.xcc_mask); xcc_id++) {
		/* query sa bitmap from SA_UNIT_DISABLE registers */
		active_sa_bitmap = gfx_v12_1_get_sa_active_bitmap(adev, xcc_id);
		/* query rb bitmap from RB_BACKEND_DISABLE registers */
		global_active_rb_bitmap = gfx_v12_1_get_rb_active_bitmap(adev, xcc_id);

		/* generate active rb bitmap according to active sa bitmap */
		max_sa = adev->gfx.config.max_shader_engines *
			 adev->gfx.config.max_sh_per_se;
		rb_bitmap_width_per_sa = adev->gfx.config.max_backends_per_se /
					 adev->gfx.config.max_sh_per_se;
		for (i = 0; i < max_sa; i++) {
			if (active_sa_bitmap & (1 << i))
				active_rb_bitmap |= (0x3 << (i * rb_bitmap_width_per_sa));
		}

		active_rb_bitmap |= global_active_rb_bitmap;
	}

	adev->gfx.config.backend_enable_mask = active_rb_bitmap;
	adev->gfx.config.num_rbs = hweight32(active_rb_bitmap);
}

static void gfx_v12_1_xcc_init_compute_vmid(struct amdgpu_device *adev,
					    int xcc_id)
{
	int i;
	uint32_t sh_mem_bases;
	uint32_t data;

	/*
	 * Configure apertures:
	 * LDS:         0x20000000'00000000 - 0x20000001'00000000 (4GB)
	 * Scratch:     0x10000000'00000000 - 0x10000001'00000000 (4GB)
	 */
	sh_mem_bases = REG_SET_FIELD(0, SH_MEM_BASES, PRIVATE_BASE,
				     (adev->gmc.private_aperture_start >> 58));
	sh_mem_bases = REG_SET_FIELD(sh_mem_bases, SH_MEM_BASES, SHARED_BASE,
				     (adev->gmc.shared_aperture_start >> 48));

	mutex_lock(&adev->srbm_mutex);
	for (i = adev->vm_manager.first_kfd_vmid; i < AMDGPU_NUM_VMID; i++) {
		soc_v1_0_grbm_select(adev, 0, 0, 0, i, GET_INST(GC, xcc_id));
		/* CP and shaders */
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSH_MEM_CONFIG, DEFAULT_SH_MEM_CONFIG);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSH_MEM_BASES, sh_mem_bases);

		/* Enable trap for each kfd vmid. */
		data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSPI_GDBG_PER_VMID_CNTL);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSPI_GDBG_PER_VMID_CNTL, data);

		/* Disable VGPR deallocation instruction for each KFD vmid. */
		data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_DEBUG);
		data = REG_SET_FIELD(data, SQ_DEBUG, DISABLE_VGPR_DEALLOC, 1);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSQ_DEBUG, data);
	}
	soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
	mutex_unlock(&adev->srbm_mutex);
}

static void gfx_v12_1_tcp_harvest(struct amdgpu_device *adev)
{
	/* TODO: harvest feature to be added later. */
}

static void gfx_v12_1_get_tcc_info(struct amdgpu_device *adev)
{
}

static void gfx_v12_1_xcc_constants_init(struct amdgpu_device *adev,
					 int xcc_id)
{
	u32 tmp;
	int i;

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < adev->vm_manager.id_mgr[AMDGPU_GFXHUB(0)].num_ids; i++) {
		soc_v1_0_grbm_select(adev, 0, 0, 0, i, GET_INST(GC, xcc_id));
		/* CP and shaders */
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regSH_MEM_CONFIG, DEFAULT_SH_MEM_CONFIG);
		if (i != 0) {
			tmp = REG_SET_FIELD(0, SH_MEM_BASES, PRIVATE_BASE,
				(adev->gmc.private_aperture_start >> 58));
			tmp = REG_SET_FIELD(tmp, SH_MEM_BASES, SHARED_BASE,
				(adev->gmc.shared_aperture_start >> 48));
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regSH_MEM_BASES, tmp);
		}
	}
	soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));

	mutex_unlock(&adev->srbm_mutex);

	gfx_v12_1_xcc_init_compute_vmid(adev, xcc_id);
}

static void gfx_v12_1_constants_init(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	gfx_v12_1_setup_rb(adev);
	gfx_v12_1_get_cu_info(adev, &adev->gfx.cu_info);
	gfx_v12_1_get_tcc_info(adev);
	adev->gfx.config.pa_sc_tile_steering_override = 0;

	for (i = 0; i < num_xcc; i++)
		gfx_v12_1_xcc_constants_init(adev, i);
}

static void gfx_v12_1_xcc_enable_gui_idle_interrupt(struct amdgpu_device *adev,
						    bool enable, int xcc_id)
{
	u32 tmp;

	if (amdgpu_sriov_vf(adev))
		return;

	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE,
			    enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE,
			    enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE,
			    enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, GFX_IDLE_INT_ENABLE,
			    enable ? 1 : 0);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_INT_CNTL_RING0, tmp);
}

static int gfx_v12_1_xcc_init_csb(struct amdgpu_device *adev,
				  int xcc_id)
{
	adev->gfx.rlc.funcs->get_csb_buffer(adev, adev->gfx.rlc.cs_ptr);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CSIB_ADDR_HI,
			adev->gfx.rlc.clear_state_gpu_addr >> 32);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CSIB_ADDR_LO,
			adev->gfx.rlc.clear_state_gpu_addr & 0xfffffffc);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regRLC_CSIB_LENGTH, adev->gfx.rlc.clear_state_size);

	return 0;
}

static void gfx_v12_1_xcc_rlc_stop(struct amdgpu_device *adev,
				   int xcc_id)
{
	u32 tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CNTL);

	tmp = REG_SET_FIELD(tmp, RLC_CNTL, RLC_ENABLE_F32, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CNTL, tmp);
}

static void gfx_v12_1_rlc_stop(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		gfx_v12_1_xcc_rlc_stop(adev, i);
}

static void gfx_v12_1_xcc_rlc_reset(struct amdgpu_device *adev,
				    int xcc_id)
{
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id),
			      GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);
	udelay(50);
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id),
			      GRBM_SOFT_RESET, SOFT_RESET_RLC, 0);
	udelay(50);
}

static void gfx_v12_1_rlc_reset(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		gfx_v12_1_xcc_rlc_reset(adev, i);
}

static void gfx_v12_1_xcc_rlc_smu_handshake_cntl(struct amdgpu_device *adev,
						 bool enable, int xcc_id)
{
	uint32_t rlc_pg_cntl;

	rlc_pg_cntl = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_PG_CNTL);

	if (!enable) {
		/* RLC_PG_CNTL[23] = 0 (default)
		 * RLC will wait for handshake acks with SMU
		 * GFXOFF will be enabled
		 * RLC_PG_CNTL[23] = 1
		 * RLC will not issue any message to SMU
		 * hence no handshake between SMU & RLC
		 * GFXOFF will be disabled
		 */
		rlc_pg_cntl |= RLC_PG_CNTL__SMU_HANDSHAKE_DISABLE_MASK;
	} else
		rlc_pg_cntl &= ~RLC_PG_CNTL__SMU_HANDSHAKE_DISABLE_MASK;
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_PG_CNTL, rlc_pg_cntl);
}

static void gfx_v12_1_xcc_rlc_start(struct amdgpu_device *adev,
				    int xcc_id)
{
	/* TODO: enable rlc & smu handshake until smu
	 * and gfxoff feature works as expected */
	if (!(amdgpu_pp_feature_mask & PP_GFXOFF_MASK))
		gfx_v12_1_xcc_rlc_smu_handshake_cntl(adev, false, xcc_id);

	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), RLC_CNTL, RLC_ENABLE_F32, 1);
	udelay(50);
}

static void gfx_v12_1_rlc_start(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		gfx_v12_1_xcc_rlc_start(adev, i);
	}
}

static void gfx_v12_1_xcc_rlc_enable_srm(struct amdgpu_device *adev,
					 int xcc_id)
{
	uint32_t tmp;

	/* enable Save Restore Machine */
	tmp = RREG32(SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_SRM_CNTL));
	tmp |= RLC_SRM_CNTL__AUTO_INCR_ADDR_MASK;
	tmp |= RLC_SRM_CNTL__SRM_ENABLE_MASK;
	WREG32(SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_SRM_CNTL), tmp);
}

static void gfx_v12_1_xcc_load_rlcg_microcode(struct amdgpu_device *adev,
					      int xcc_id)
{
	const struct rlc_firmware_header_v2_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			   le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_GPM_UCODE_ADDR,
		     RLCG_UCODE_LOADING_START_ADDRESS);

	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regRLC_GPM_UCODE_DATA,
			     le32_to_cpup(fw_data++));

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regRLC_GPM_UCODE_ADDR,
		     adev->gfx.rlc_fw_version);
}

static void gfx_v12_1_xcc_load_rlc_iram_dram_microcode(struct amdgpu_device *adev,
						       int xcc_id)
{
	const struct rlc_firmware_header_v2_2 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;
	u32 tmp;

	hdr = (const struct rlc_firmware_header_v2_2 *)adev->gfx.rlc_fw->data;

	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			le32_to_cpu(hdr->rlc_iram_ucode_offset_bytes));
	fw_size = le32_to_cpu(hdr->rlc_iram_ucode_size_bytes) / 4;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_LX6_IRAM_ADDR, 0);

	for (i = 0; i < fw_size; i++) {
		if ((amdgpu_emu_mode == 1) && (i % 100 == 99))
			msleep(1);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regRLC_LX6_IRAM_DATA,
			     le32_to_cpup(fw_data++));
	}

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regRLC_LX6_IRAM_ADDR, adev->gfx.rlc_fw_version);

	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			le32_to_cpu(hdr->rlc_dram_ucode_offset_bytes));
	fw_size = le32_to_cpu(hdr->rlc_dram_ucode_size_bytes) / 4;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regRLC_LX6_DRAM_ADDR, 0);
	for (i = 0; i < fw_size; i++) {
		if ((amdgpu_emu_mode == 1) && (i % 100 == 99))
			msleep(1);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regRLC_LX6_DRAM_DATA,
			     le32_to_cpup(fw_data++));
	}

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regRLC_LX6_IRAM_ADDR, adev->gfx.rlc_fw_version);

	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_LX6_CNTL);
	tmp = REG_SET_FIELD(tmp, RLC_LX6_CNTL, PDEBUG_ENABLE, 1);
	tmp = REG_SET_FIELD(tmp, RLC_LX6_CNTL, BRESET, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_LX6_CNTL, tmp);
}

static int gfx_v12_1_xcc_rlc_load_microcode(struct amdgpu_device *adev,
					    int xcc_id)
{
	const struct rlc_firmware_header_v2_0 *hdr;
	uint16_t version_major;
	uint16_t version_minor;

	if (!adev->gfx.rlc_fw)
		return -EINVAL;

	hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	amdgpu_ucode_print_rlc_hdr(&hdr->header);

	version_major = le16_to_cpu(hdr->header.header_version_major);
	version_minor = le16_to_cpu(hdr->header.header_version_minor);

	if (version_major == 2) {
		gfx_v12_1_xcc_load_rlcg_microcode(adev, xcc_id);
		if (amdgpu_dpm == 1) {
			if (version_minor >= 2)
				gfx_v12_1_xcc_load_rlc_iram_dram_microcode(adev, xcc_id);
		}

		return 0;
	}

	return -EINVAL;
}

static int gfx_v12_1_xcc_rlc_resume(struct amdgpu_device *adev,
				    int xcc_id)
{
	int r;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		gfx_v12_1_xcc_init_csb(adev, xcc_id);

		if (!amdgpu_sriov_vf(adev)) /* enable RLC SRM */
			gfx_v12_1_xcc_rlc_enable_srm(adev, xcc_id);
	} else {
		if (amdgpu_sriov_vf(adev)) {
			gfx_v12_1_xcc_init_csb(adev, xcc_id);
			return 0;
		}

		gfx_v12_1_xcc_rlc_stop(adev, xcc_id);

		/* disable CG */
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL, 0);

		/* disable PG */
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_PG_CNTL, 0);

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
			/* legacy rlc firmware loading */
			r = gfx_v12_1_xcc_rlc_load_microcode(adev, xcc_id);
			if (r)
				return r;
		}

		gfx_v12_1_xcc_init_csb(adev, xcc_id);

		gfx_v12_1_xcc_rlc_start(adev, xcc_id);
	}

	return 0;
}

static int gfx_v12_1_rlc_resume(struct amdgpu_device *adev)
{
	int r, i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		r = gfx_v12_1_xcc_rlc_resume(adev, i);
		if (r)
			return r;
	}

	return 0;
}

static void gfx_v12_1_xcc_config_gfx_rs64(struct amdgpu_device *adev,
					  int xcc_id)
{
	const struct gfx_firmware_header_v2_0 *mec_hdr;
	uint32_t pipe_id, tmp;

	mec_hdr = (const struct gfx_firmware_header_v2_0 *)
		adev->gfx.mec_fw->data;

	/* config mec program start addr */
	for (pipe_id = 0; pipe_id < 4; pipe_id++) {
		soc_v1_0_grbm_select(adev, 1, pipe_id, 0, 0, GET_INST(GC, xcc_id));
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_PRGRM_CNTR_START,
					mec_hdr->ucode_start_addr_lo >> 2 |
					mec_hdr->ucode_start_addr_hi << 30);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_PRGRM_CNTR_START_HI,
					mec_hdr->ucode_start_addr_hi >> 2);
	}
	soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));

	/* reset mec pipe */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE0_RESET, 1);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE1_RESET, 1);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE2_RESET, 1);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE3_RESET, 1);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_CNTL, tmp);

	/* clear mec pipe reset */
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE0_RESET, 0);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE1_RESET, 0);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE2_RESET, 0);
	tmp = REG_SET_FIELD(tmp, CP_MEC_RS64_CNTL, MEC_PIPE3_RESET, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_CNTL, tmp);
}

static void gfx_v12_1_config_gfx_rs64(struct amdgpu_device *adev)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	for (i = 0; i < num_xcc; i++)
		gfx_v12_1_xcc_config_gfx_rs64(adev, i);
}

static void gfx_v12_1_xcc_set_mec_ucode_start_addr(struct amdgpu_device *adev,
						   int xcc_id)
{
	const struct gfx_firmware_header_v2_0 *cp_hdr;
	unsigned pipe_id;

	cp_hdr = (const struct gfx_firmware_header_v2_0 *)
		adev->gfx.mec_fw->data;
	mutex_lock(&adev->srbm_mutex);
	for (pipe_id = 0; pipe_id < adev->gfx.mec.num_pipe_per_mec; pipe_id++) {
		soc_v1_0_grbm_select(adev, 1, pipe_id, 0, 0, GET_INST(GC, xcc_id));
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_PRGRM_CNTR_START,
			     cp_hdr->ucode_start_addr_lo >> 2 |
			     cp_hdr->ucode_start_addr_hi << 30);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_PRGRM_CNTR_START_HI,
			     cp_hdr->ucode_start_addr_hi >> 2);
	}
	soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
	mutex_unlock(&adev->srbm_mutex);
}

static int gfx_v12_1_xcc_wait_for_rlc_autoload_complete(struct amdgpu_device *adev,
							int xcc_id)
{
	uint32_t cp_status;
	uint32_t bootload_status;
	int i;

	for (i = 0; i < adev->usec_timeout; i++) {
		cp_status = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_STAT);
		bootload_status = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
					       regRLC_RLCS_BOOTLOAD_STATUS);

		if ((cp_status == 0) &&
		    (REG_GET_FIELD(bootload_status,
			RLC_RLCS_BOOTLOAD_STATUS, BOOTLOAD_COMPLETE) == 1)) {
			break;
		}
		udelay(1);
		if (amdgpu_emu_mode)
			msleep(10);
	}

	if (i >= adev->usec_timeout) {
		dev_err(adev->dev,
			"rlc autoload: xcc%d gc ucode autoload timeout\n", xcc_id);
		return -ETIMEDOUT;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
		gfx_v12_1_xcc_set_mec_ucode_start_addr(adev, xcc_id);
	}

	return 0;
}

static int gfx_v12_1_wait_for_rlc_autoload_complete(struct amdgpu_device *adev)
{
	int xcc_id;

	for (xcc_id = 0; xcc_id < NUM_XCC(adev->gfx.xcc_mask); xcc_id++)
		gfx_v12_1_xcc_wait_for_rlc_autoload_complete(adev, xcc_id);

	return 0;
}

static void gfx_v12_1_xcc_cp_compute_enable(struct amdgpu_device *adev,
					    bool enable, int xcc_id)
{
	u32 data;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_CNTL);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_INVALIDATE_ICACHE,
						 enable ? 0 : 1);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE0_RESET,
						 enable ? 0 : 1);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE1_RESET,
						 enable ? 0 : 1);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE2_RESET,
						 enable ? 0 : 1);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE3_RESET,
						 enable ? 0 : 1);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE0_ACTIVE,
						 enable ? 1 : 0);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE1_ACTIVE,
			                         enable ? 1 : 0);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE2_ACTIVE,
						 enable ? 1 : 0);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_PIPE3_ACTIVE,
						 enable ? 1 : 0);
	data = REG_SET_FIELD(data, CP_MEC_RS64_CNTL, MEC_HALT,
						 enable ? 0 : 1);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_RS64_CNTL, data);

	adev->gfx.kiq[xcc_id].ring.sched.ready = enable;

	udelay(50);
}

static int gfx_v12_1_init_cp_compute_microcode_bo(struct amdgpu_device *adev)
{
	const struct gfx_firmware_header_v2_0 *mec_hdr;
	const __le32 *fw_ucode, *fw_data;
	u32 fw_ucode_size, fw_data_size;
	u32 *fw_ucode_ptr, *fw_data_ptr;
	int i, r, xcc_id;

	if (!adev->gfx.mec_fw)
		return -EINVAL;

	mec_hdr = (const struct gfx_firmware_header_v2_0 *)adev->gfx.mec_fw->data;
	amdgpu_ucode_print_gfx_hdr(&mec_hdr->header);

	fw_ucode = (const __le32 *) (adev->gfx.mec_fw->data +
				le32_to_cpu(mec_hdr->ucode_offset_bytes));
	fw_ucode_size = le32_to_cpu(mec_hdr->ucode_size_bytes);

	fw_data = (const __le32 *) (adev->gfx.mec_fw->data +
				le32_to_cpu(mec_hdr->data_offset_bytes));
	fw_data_size = le32_to_cpu(mec_hdr->data_size_bytes);

	if (adev->gfx.mec.mec_fw_obj == NULL) {
		r = amdgpu_bo_create_reserved(adev, fw_ucode_size,
					      64 * 1024, AMDGPU_GEM_DOMAIN_VRAM,
					      &adev->gfx.mec.mec_fw_obj,
					      &adev->gfx.mec.mec_fw_gpu_addr,
					      (void **)&fw_ucode_ptr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to create mec fw ucode bo\n", r);
			gfx_v12_1_mec_fini(adev);
			return r;
		}

		memcpy(fw_ucode_ptr, fw_ucode, fw_ucode_size);

		amdgpu_bo_kunmap(adev->gfx.mec.mec_fw_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.mec_fw_obj);
	}

	if (adev->gfx.mec.mec_fw_data_obj == NULL) {
		r = amdgpu_bo_create_reserved(adev,
					      ALIGN(fw_data_size, 64 * 1024) *
					      adev->gfx.mec.num_pipe_per_mec * NUM_XCC(adev->gfx.xcc_mask),
					      64 * 1024, AMDGPU_GEM_DOMAIN_VRAM,
					      &adev->gfx.mec.mec_fw_data_obj,
					      &adev->gfx.mec.mec_fw_data_gpu_addr,
					      (void **)&fw_data_ptr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to create mec fw data bo\n", r);
			gfx_v12_1_mec_fini(adev);
			return r;
		}

		for (xcc_id = 0; xcc_id < NUM_XCC(adev->gfx.xcc_mask); xcc_id++) {
			for (i = 0; i < adev->gfx.mec.num_pipe_per_mec; i++) {
				u32 offset = (xcc_id * adev->gfx.mec.num_pipe_per_mec + i) *
					     ALIGN(fw_data_size, 64 * 1024) / 4;
				memcpy(fw_data_ptr + offset, fw_data, fw_data_size);
			}
		}

		amdgpu_bo_kunmap(adev->gfx.mec.mec_fw_data_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.mec_fw_data_obj);
	}

	return 0;
}

static int gfx_v12_1_xcc_cp_compute_load_microcode_rs64(struct amdgpu_device *adev,
							int xcc_id)
{
	const struct gfx_firmware_header_v2_0 *mec_hdr;
	u32 fw_data_size;
	u32 tmp, i, usec_timeout = 50000; /* Wait for 50 ms */

	if (!adev->gfx.mec_fw)
		return -EINVAL;

	mec_hdr = (const struct gfx_firmware_header_v2_0 *)adev->gfx.mec_fw->data;
	fw_data_size = le32_to_cpu(mec_hdr->data_size_bytes);

	gfx_v12_1_xcc_cp_compute_enable(adev, false, xcc_id);

	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, EXE_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, CACHE_POLICY, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_CNTL, tmp);

	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_MEC_DC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_MEC_DC_BASE_CNTL, CACHE_POLICY, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DC_BASE_CNTL, tmp);

	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < adev->gfx.mec.num_pipe_per_mec; i++) {
		soc_v1_0_grbm_select(adev, 1, i, 0, 0, GET_INST(GC, xcc_id));

		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_MDBASE_LO,
			     lower_32_bits(adev->gfx.mec.mec_fw_data_gpu_addr +
					   (xcc_id * adev->gfx.mec.num_pipe_per_mec + i) *
					   ALIGN(fw_data_size, 64 * 1024)));
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_MDBASE_HI,
			     upper_32_bits(adev->gfx.mec.mec_fw_data_gpu_addr +
					   (xcc_id * adev->gfx.mec.num_pipe_per_mec + i) *
					   ALIGN(fw_data_size, 64 * 1024)));

		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_LO,
				lower_32_bits(adev->gfx.mec.mec_fw_gpu_addr));
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_BASE_HI,
				upper_32_bits(adev->gfx.mec.mec_fw_gpu_addr));
	}
	mutex_unlock(&adev->srbm_mutex);
	soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_MEC_DC_OP_CNTL, INVALIDATE_DCACHE, 1);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_MEC_DC_OP_CNTL,
				       INVALIDATE_DCACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate data cache\n");
		return -EINVAL;
	}

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_CPC_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_CPC_IC_OP_CNTL,
				       INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	gfx_v12_1_xcc_set_mec_ucode_start_addr(adev, xcc_id);

	return 0;
}

static void gfx_v12_1_xcc_kiq_setting(struct amdgpu_ring *ring,
				      int xcc_id)
{
	uint32_t tmp;
	struct amdgpu_device *adev = ring->adev;

	/* tell RLC which is KIQ queue */
	tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS);
	tmp &= 0xffffff00;
	tmp |= (ring->me << 5) | (ring->pipe << 3) | (ring->queue);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS, tmp);
	tmp |= 0x80;
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS, tmp);
}

static void gfx_v12_1_xcc_cp_set_doorbell_range(struct amdgpu_device *adev,
						int xcc_id)
{
	/* disable gfx engine doorbell range */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_RB_DOORBELL_RANGE_LOWER, 0);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_RB_DOORBELL_RANGE_UPPER, 0);

	/* set compute engine doorbell range */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DOORBELL_RANGE_LOWER,
		     ((adev->doorbell_index.kiq +
		       xcc_id * adev->doorbell_index.xcc_doorbell_range) *
		      2) << 2);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MEC_DOORBELL_RANGE_UPPER,
		     ((adev->doorbell_index.userqueue_end +
		       xcc_id * adev->doorbell_index.xcc_doorbell_range) *
		      2) << 2);
}

static int gfx_v12_1_compute_mqd_init(struct amdgpu_device *adev, void *m,
				      struct amdgpu_mqd_prop *prop)
{
	struct v12_1_compute_mqd *mqd = m;
	uint64_t hqd_gpu_addr, wb_gpu_addr, eop_base_addr;
	uint32_t tmp;

	mqd->header = 0xC0310800;
	mqd->compute_pipelinestat_enable = 0x00000001;
	mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
	mqd->compute_misc_reserved = 0x00000007;

	eop_base_addr = prop->eop_gpu_addr >> 8;
	mqd->cp_hqd_eop_base_addr_lo = eop_base_addr;
	mqd->cp_hqd_eop_base_addr_hi = upper_32_bits(eop_base_addr);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = regCP_HQD_EOP_CONTROL_DEFAULT;
	tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
			(order_base_2(GFX12_MEC_HPD_SIZE / 4) - 1));

	mqd->cp_hqd_eop_control = tmp;

	/* enable doorbell? */
	tmp = regCP_HQD_PQ_DOORBELL_CONTROL_DEFAULT;

	if (prop->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, prop->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* disable the queue if it's active */
	mqd->cp_hqd_dequeue_request = 0;
	mqd->cp_hqd_pq_rptr = 0;
	mqd->cp_hqd_pq_wptr_lo = 0;
	mqd->cp_hqd_pq_wptr_hi = 0;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr_lo = prop->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(prop->mqd_gpu_addr);

	/* set MQD vmid to 0 */
	tmp = regCP_MQD_CONTROL_DEFAULT;
	tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
	mqd->cp_mqd_control = tmp;

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	hqd_gpu_addr = prop->hqd_base_gpu_addr >> 8;
	mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
	mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	tmp = regCP_HQD_PQ_CONTROL_DEFAULT;
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, QUEUE_SIZE,
			    (order_base_2(prop->queue_size / 4) - 1));
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, RPTR_BLOCK_SIZE,
			    (order_base_2(AMDGPU_GPU_PAGE_SIZE / 4) - 1));
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, UNORD_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, TUNNEL_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, PRIV_STATE, 1);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, KMD_QUEUE, 1);
	mqd->cp_hqd_pq_control = tmp;

	/* set the wb address whether it's enabled or not */
	wb_gpu_addr = prop->rptr_gpu_addr;
	mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_rptr_report_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	wb_gpu_addr = prop->wptr_gpu_addr;
	mqd->cp_hqd_pq_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	tmp = 0;
	/* enable the doorbell if requested */
	if (prop->use_doorbell) {
		tmp = regCP_HQD_PQ_DOORBELL_CONTROL_DEFAULT;
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				DOORBELL_OFFSET, prop->doorbell_index);

		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	mqd->cp_hqd_pq_rptr = regCP_HQD_PQ_RPTR_DEFAULT;

	/* set the vmid for the queue */
	mqd->cp_hqd_vmid = 0;

	tmp = regCP_HQD_PERSISTENT_STATE_DEFAULT;
	tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x63);
	mqd->cp_hqd_persistent_state = tmp;

	/* set MIN_IB_AVAIL_SIZE */
	tmp = regCP_HQD_IB_CONTROL_DEFAULT;
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MIN_IB_AVAIL_SIZE, 1);
	mqd->cp_hqd_ib_control = tmp;

	/* set static priority for a compute queue/ring */
	mqd->cp_hqd_pipe_priority = prop->hqd_pipe_priority;
	mqd->cp_hqd_queue_priority = prop->hqd_queue_priority;

	mqd->cp_mqd_stride_size = prop->mqd_stride_size ? prop->mqd_stride_size :
		AMDGPU_MQD_SIZE_ALIGN(adev->mqds[AMDGPU_HW_IP_COMPUTE].mqd_size);

	mqd->cp_hqd_active = prop->hqd_active;

	return 0;
}

static int gfx_v12_1_xcc_kiq_init_register(struct amdgpu_ring *ring,
					   int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	struct v12_1_compute_mqd *mqd = ring->mqd_ptr;
	int j;

	/* inactivate the queue */
	if (amdgpu_sriov_vf(adev))
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE, 0);

	/* disable wptr polling */
	WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), CP_PQ_WPTR_POLL_CNTL, EN, 0);

	/* write the EOP addr */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_BASE_ADDR,
	       mqd->cp_hqd_eop_base_addr_lo);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_BASE_ADDR_HI,
	       mqd->cp_hqd_eop_base_addr_hi);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_EOP_CONTROL,
	       mqd->cp_hqd_eop_control);

	/* enable doorbell? */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* disable the queue if it's active */
	if (RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1) {
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST, 1);
		for (j = 0; j < adev->usec_timeout; j++) {
			if (!(RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_DEQUEUE_REQUEST,
		       mqd->cp_hqd_dequeue_request);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR,
		       mqd->cp_hqd_pq_rptr);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_LO,
		       mqd->cp_hqd_pq_wptr_lo);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_HI,
		       mqd->cp_hqd_pq_wptr_hi);
	}

	/* set the pointer to the MQD */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MQD_BASE_ADDR,
	       mqd->cp_mqd_base_addr_lo);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MQD_BASE_ADDR_HI,
	       mqd->cp_mqd_base_addr_hi);

	/* set MQD vmid to 0 */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_MQD_CONTROL,
	       mqd->cp_mqd_control);

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_BASE,
	       mqd->cp_hqd_pq_base_lo);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_BASE_HI,
	       mqd->cp_hqd_pq_base_hi);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_CONTROL,
	       mqd->cp_hqd_pq_control);

	/* set the wb address whether it's enabled or not */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR_REPORT_ADDR,
		mqd->cp_hqd_pq_rptr_report_addr_lo);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_RPTR_REPORT_ADDR_HI,
		mqd->cp_hqd_pq_rptr_report_addr_hi);

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_POLL_ADDR,
	       mqd->cp_hqd_pq_wptr_poll_addr_lo);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_POLL_ADDR_HI,
	       mqd->cp_hqd_pq_wptr_poll_addr_hi);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_LO,
	       mqd->cp_hqd_pq_wptr_lo);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PQ_WPTR_HI,
	       mqd->cp_hqd_pq_wptr_hi);

	/* set the vmid for the queue */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_VMID, mqd->cp_hqd_vmid);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_PERSISTENT_STATE,
	       mqd->cp_hqd_persistent_state);

	/* activate the queue */
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_HQD_ACTIVE,
	       mqd->cp_hqd_active);

	if (ring->use_doorbell)
		WREG32_FIELD15_PREREG(GC, GET_INST(GC, xcc_id), CP_PQ_STATUS, DOORBELL_ENABLE, 1);

	return 0;
}

static int gfx_v12_1_xcc_kiq_init_queue(struct amdgpu_ring *ring,
					int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	struct v12_1_compute_mqd *mqd = ring->mqd_ptr;

	gfx_v12_1_xcc_kiq_setting(ring, xcc_id);

	if (amdgpu_in_reset(adev)) { /* for GPU_RESET case */
		/* reset MQD to a clean status */
		if (adev->gfx.kiq[xcc_id].mqd_backup)
			memcpy(mqd, adev->gfx.kiq[xcc_id].mqd_backup, sizeof(*mqd));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);

		mutex_lock(&adev->srbm_mutex);
		soc_v1_0_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0, GET_INST(GC, xcc_id));
		gfx_v12_1_xcc_kiq_init_register(ring, xcc_id);
		soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);
	} else {
		memset((void *)mqd, 0, sizeof(*mqd));
		if (amdgpu_sriov_vf(adev) && adev->in_suspend)
			amdgpu_ring_clear_ring(ring);
		mutex_lock(&adev->srbm_mutex);
		soc_v1_0_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0, GET_INST(GC, xcc_id));
		amdgpu_ring_init_mqd(ring);
		gfx_v12_1_xcc_kiq_init_register(ring, xcc_id);
		soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.kiq[xcc_id].mqd_backup)
			memcpy(adev->gfx.kiq[xcc_id].mqd_backup, mqd, sizeof(*mqd));
	}

	return 0;
}

static int gfx_v12_1_xcc_kcq_init_queue(struct amdgpu_ring *ring,
					int xcc_id)
{
	struct amdgpu_device *adev = ring->adev;
	struct v12_1_compute_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = ring - &adev->gfx.compute_ring[0];

	if (!amdgpu_in_reset(adev) && !adev->in_suspend) {
		memset((void *)mqd, 0, sizeof(*mqd));
		mutex_lock(&adev->srbm_mutex);
		soc_v1_0_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0, GET_INST(GC, xcc_id));
		amdgpu_ring_init_mqd(ring);
		soc_v1_0_grbm_select(adev, 0, 0, 0, 0, GET_INST(GC, xcc_id));
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy_fromio(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(*mqd));
	} else {
		/* restore MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy_toio(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(*mqd));
		/* reset ring buffer */
		ring->wptr = 0;
		atomic64_set((atomic64_t *)ring->wptr_cpu_addr, 0);
		amdgpu_ring_clear_ring(ring);
	}

	return 0;
}

static int gfx_v12_1_xcc_kiq_resume(struct amdgpu_device *adev,
				    int xcc_id)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->gfx.kiq[xcc_id].ring;

	r = amdgpu_bo_reserve(ring->mqd_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
	if (unlikely(r != 0)) {
		amdgpu_bo_unreserve(ring->mqd_obj);
		return r;
	}

	gfx_v12_1_xcc_kiq_init_queue(ring, xcc_id);
	amdgpu_bo_kunmap(ring->mqd_obj);
	ring->mqd_ptr = NULL;
	amdgpu_bo_unreserve(ring->mqd_obj);
	ring->sched.ready = true;
	return 0;
}

static int gfx_v12_1_xcc_kcq_resume(struct amdgpu_device *adev,
				    int xcc_id)
{
	struct amdgpu_ring *ring = NULL;
	int r = 0, i;

	if (!amdgpu_async_gfx_ring)
		gfx_v12_1_xcc_cp_compute_enable(adev, true, xcc_id);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i + xcc_id * adev->gfx.num_compute_rings];

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0))
			goto done;
		r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
		if (!r) {
			r = gfx_v12_1_xcc_kcq_init_queue(ring, xcc_id);
			amdgpu_bo_kunmap(ring->mqd_obj);
			ring->mqd_ptr = NULL;
		}
		amdgpu_bo_unreserve(ring->mqd_obj);
		if (r)
			goto done;
	}

	r = amdgpu_gfx_enable_kcq(adev, xcc_id);
done:
	return r;
}

static int gfx_v12_1_xcc_cp_resume(struct amdgpu_device *adev, uint16_t xcc_mask)
{
	int r, i, xcc_id;
	struct amdgpu_ring *ring;

	for_each_inst(xcc_id, xcc_mask) {
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
			/* legacy firmware loading */
			r = gfx_v12_1_xcc_cp_compute_load_microcode_rs64(adev, xcc_id);
			if (r)
				return r;
		}

		/* GFX CGCG and LS is set by default */
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)
			gfx_v12_1_xcc_enable_gui_idle_interrupt(adev, true, xcc_id);

		gfx_v12_1_xcc_cp_set_doorbell_range(adev, xcc_id);

		gfx_v12_1_xcc_cp_compute_enable(adev, true, xcc_id);

		if (adev->enable_mes_kiq && adev->mes.kiq_hw_init)
			r = amdgpu_mes_kiq_hw_init(adev, xcc_id);
		else
			r = gfx_v12_1_xcc_kiq_resume(adev, xcc_id);
		if (r)
			return r;

		r = gfx_v12_1_xcc_kcq_resume(adev, xcc_id);
		if (r)
			return r;

		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i + xcc_id * adev->gfx.num_compute_rings];
			r = amdgpu_ring_test_helper(ring);
			if (r)
				return r;
		}
	}

	return 0;
}

static int gfx_v12_1_cp_resume(struct amdgpu_device *adev)
{
	int num_xcc, num_xcp, num_xcc_per_xcp;
	uint16_t xcc_mask;
	int r = 0;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	if (amdgpu_sriov_vf(adev)) {
		enum amdgpu_gfx_partition mode;

		mode = amdgpu_xcp_query_partition_mode(adev->xcp_mgr,
						       AMDGPU_XCP_FL_NONE);
		if (mode == AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE)
			return -EINVAL;
		if (adev->gfx.funcs &&
		    adev->gfx.funcs->get_xccs_per_xcp) {
			num_xcc_per_xcp = adev->gfx.funcs->get_xccs_per_xcp(adev);
			adev->gfx.num_xcc_per_xcp = num_xcc_per_xcp;
			num_xcp = num_xcc / num_xcc_per_xcp;
		} else {
			return -EINVAL;
		}
		r = amdgpu_xcp_init(adev->xcp_mgr, num_xcp, mode);

	} else {
		if (amdgpu_xcp_query_partition_mode(adev->xcp_mgr,
						    AMDGPU_XCP_FL_NONE) ==
		    AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE)
			r = amdgpu_xcp_switch_partition_mode(adev->xcp_mgr,
							     amdgpu_user_partt_mode);
	}

	if (r)
		return r;

	xcc_mask = GENMASK(NUM_XCC(adev->gfx.xcc_mask) - 1, 0);

	return gfx_v12_1_xcc_cp_resume(adev, xcc_mask);
}

static int gfx_v12_1_gfxhub_enable(struct amdgpu_device *adev)
{
	int r, i;
	bool value;

	r = adev->gfxhub.funcs->gart_enable(adev);
	if (r)
		return r;

	value = (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS) ?
		false : true;

	adev->gfxhub.funcs->set_fault_enable_default(adev, value);
	/* TODO investigate why TLB flush is needed,
	 * are we missing a flush somewhere else? */
	for_each_set_bit(i, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
		if (AMDGPU_IS_GFXHUB(i))
			adev->gmc.gmc_funcs->flush_gpu_tlb(adev, 0, AMDGPU_GFXHUB(i), 0);
	}

	return 0;
}

static int get_gb_addr_config(struct amdgpu_device *adev)
{
	u32 gb_addr_config;

	gb_addr_config = RREG32_SOC15(GC, GET_INST(GC, 0), regGB_ADDR_CONFIG_READ);
	if (gb_addr_config == 0)
		return -EINVAL;

	adev->gfx.config.gb_addr_config_fields.num_pkrs =
		1 << REG_GET_FIELD(gb_addr_config, GB_ADDR_CONFIG_READ, NUM_PKRS);

	adev->gfx.config.gb_addr_config = gb_addr_config;

	adev->gfx.config.gb_addr_config_fields.num_pipes = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG_READ, NUM_PIPES);

	adev->gfx.config.max_tile_pipes =
		adev->gfx.config.gb_addr_config_fields.num_pipes;

	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG_READ, MAX_COMPRESSED_FRAGS);
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG_READ, NUM_RB_PER_SE);
	adev->gfx.config.gb_addr_config_fields.num_se = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG_READ, NUM_SHADER_ENGINES);
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 1 << (8 +
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG_READ, PIPE_INTERLEAVE_SIZE));

	return 0;
}

static void gfx_v12_1_xcc_disable_gpa_mode(struct amdgpu_device *adev,
					   int xcc_id)
{
	uint32_t data;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCPC_PSP_DEBUG);
	data |= CPC_PSP_DEBUG__GPA_OVERRIDE_MASK;
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCPC_PSP_DEBUG, data);

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCPG_PSP_DEBUG);
	data |= CPG_PSP_DEBUG__GPA_OVERRIDE_MASK;
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCPG_PSP_DEBUG, data);
}

static void gfx_v12_1_xcc_setup_tcp_thrashing_ctrl(struct amdgpu_device *adev,
					 int xcc_id)
{
	uint32_t val;

	/* Set the TCP UTCL0 register to enable atomics */
	val = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
					regTCP_UTCL0_THRASHING_CTRL);
	val = REG_SET_FIELD(val, TCP_UTCL0_THRASHING_CTRL, THRASHING_EN, 0x2);
	val = REG_SET_FIELD(val, TCP_UTCL0_THRASHING_CTRL,
					RETRY_FRAGMENT_THRESHOLD_UP_EN, 0x1);
	val = REG_SET_FIELD(val, TCP_UTCL0_THRASHING_CTRL,
					RETRY_FRAGMENT_THRESHOLD_DOWN_EN, 0x1);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
					regTCP_UTCL0_THRASHING_CTRL, val);
}

static void gfx_v12_1_xcc_enable_atomics(struct amdgpu_device *adev,
					 int xcc_id)
{
	uint32_t data;

	/* Set the TCP UTCL0 register to enable atomics */
	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regTCP_UTCL0_CNTL1);
	data = REG_SET_FIELD(data, TCP_UTCL0_CNTL1, ATOMIC_REQUESTER_EN, 0x1);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regTCP_UTCL0_CNTL1, data);
}

static void gfx_v12_1_xcc_disable_burst(struct amdgpu_device *adev,
					int xcc_id)
{
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regGL1_DRAM_BURST_CTRL, 0xf);
	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regGLARB_DRAM_BURST_CTRL, 0xf);
}

static void gfx_v12_1_xcc_disable_early_write_ack(struct amdgpu_device *adev,
					int xcc_id)
{
	uint32_t data;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regTCP_CNTL3);
	data = REG_SET_FIELD(data, TCP_CNTL3, DISABLE_EARLY_WRITE_ACK, 0x1);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regTCP_CNTL3, data);
}

static void gfx_v12_1_xcc_disable_tcp_spill_cache(struct amdgpu_device *adev,
					int xcc_id)
{
	uint32_t data;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regTCP_CNTL);
	data = REG_SET_FIELD(data, TCP_CNTL, TCP_SPILL_CACHE_DISABLE, 0x1);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regTCP_CNTL, data);
}

static void gfx_v12_1_init_golden_registers(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < NUM_XCC(adev->gfx.xcc_mask); i++) {
		gfx_v12_1_xcc_disable_burst(adev, i);
		gfx_v12_1_xcc_enable_atomics(adev, i);
		gfx_v12_1_xcc_setup_tcp_thrashing_ctrl(adev, i);
		gfx_v12_1_xcc_disable_early_write_ack(adev, i);
		gfx_v12_1_xcc_disable_tcp_spill_cache(adev, i);
	}
}

static int gfx_v12_1_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r, i, num_xcc;
	struct amdgpu_device *adev = ip_block->adev;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
		/* rlc autoload firmware */
		r = gfx_v12_1_rlc_backdoor_autoload_enable(adev);
		if (r)
			return r;
	} else {
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
			num_xcc = NUM_XCC(adev->gfx.xcc_mask);

			if (adev->gfx.imu.funcs) {
				if (adev->gfx.imu.funcs->load_microcode)
					adev->gfx.imu.funcs->load_microcode(adev);
			}

			for (i = 0; i < num_xcc; i++) {
				/* disable gpa mode in backdoor loading */
				gfx_v12_1_xcc_disable_gpa_mode(adev, i);
			}
		}
	}

	if ((adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) ||
	    (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)) {
		r = gfx_v12_1_wait_for_rlc_autoload_complete(adev);
		if (r) {
			dev_err(adev->dev, "(%d) failed to wait rlc autoload complete\n", r);
			return r;
		}
	}

	adev->gfx.is_poweron = true;

	if (get_gb_addr_config(adev))
		DRM_WARN("Invalid gb_addr_config !\n");

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP)
		gfx_v12_1_config_gfx_rs64(adev);

	r = gfx_v12_1_gfxhub_enable(adev);
	if (r)
		return r;

	gfx_v12_1_init_golden_registers(adev);

	gfx_v12_1_constants_init(adev);

	if (adev->nbio.funcs->gc_doorbell_init)
		adev->nbio.funcs->gc_doorbell_init(adev);

	r = gfx_v12_1_rlc_resume(adev);
	if (r)
		return r;

	/*
	 * init golden registers and rlc resume may override some registers,
	 * reconfig them here
	 */
	gfx_v12_1_tcp_harvest(adev);

	r = gfx_v12_1_cp_resume(adev);
	if (r)
		return r;

	return r;
}

static void gfx_v12_1_xcc_fini(struct amdgpu_device *adev,
			      int xcc_id)
{
	uint32_t tmp;

	if (!adev->no_hw_access) {
		if (amdgpu_gfx_disable_kcq(adev, xcc_id))
			DRM_ERROR("KCQ disable failed\n");

		amdgpu_mes_kiq_hw_fini(adev, xcc_id);
	}

	if (amdgpu_sriov_vf(adev)) {
		/* Program KIQ position of RLC_CP_SCHEDULERS during destroy */
		tmp = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS);
		tmp &= 0xffffff00;
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CP_SCHEDULERS, tmp);
	}
	gfx_v12_1_xcc_cp_compute_enable(adev, false, xcc_id);
	gfx_v12_1_xcc_enable_gui_idle_interrupt(adev, false, xcc_id);
}

static int gfx_v12_1_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, num_xcc;

	amdgpu_irq_put(adev, &adev->gfx.priv_reg_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_inst_irq, 0);

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		gfx_v12_1_xcc_fini(adev, i);
	}

	adev->gfxhub.funcs->gart_disable(adev);

	adev->gfx.is_poweron = false;

	return 0;
}

static int gfx_v12_1_suspend(struct amdgpu_ip_block *ip_block)
{
	return gfx_v12_1_hw_fini(ip_block);
}

static int gfx_v12_1_resume(struct amdgpu_ip_block *ip_block)
{
	return gfx_v12_1_hw_init(ip_block);
}

static bool gfx_v12_1_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		if (REG_GET_FIELD(RREG32_SOC15(GC, GET_INST(GC, i),
				regGRBM_STATUS), GRBM_STATUS, GUI_ACTIVE))
			return false;
	}
	return true;
}

static int gfx_v12_1_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i;
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gfx_v12_1_is_idle(ip_block))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static uint64_t gfx_v12_1_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock = 0;

	if (adev->smuio.funcs &&
	    adev->smuio.funcs->get_gpu_clock_counter)
		clock = adev->smuio.funcs->get_gpu_clock_counter(adev);
	else
		dev_warn(adev->dev, "query gpu clock counter is not supported\n");

	return clock;
}

static int gfx_v12_1_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->gfx.funcs = &gfx_v12_1_gfx_funcs;

	adev->gfx.num_compute_rings = min(amdgpu_gfx_get_num_kcq(adev),
					  AMDGPU_MAX_COMPUTE_RINGS);

	gfx_v12_1_set_kiq_pm4_funcs(adev);
	gfx_v12_1_set_ring_funcs(adev);
	gfx_v12_1_set_irq_funcs(adev);
	gfx_v12_1_set_rlc_funcs(adev);
	gfx_v12_1_set_mqd_funcs(adev);
	gfx_v12_1_set_imu_funcs(adev);

	gfx_v12_1_init_rlcg_reg_access_ctrl(adev);

	return gfx_v12_1_init_microcode(adev);
}

static int gfx_v12_1_late_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_reg_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_inst_irq, 0);
	if (r)
		return r;

	return 0;
}

static bool gfx_v12_1_is_rlc_enabled(struct amdgpu_device *adev)
{
	uint32_t rlc_cntl;

	/* if RLC is not enabled, do nothing */
	rlc_cntl = RREG32_SOC15(GC, GET_INST(GC, 0), regRLC_CNTL);
	return (REG_GET_FIELD(rlc_cntl, RLC_CNTL, RLC_ENABLE_F32)) ? true : false;
}

static void gfx_v12_1_xcc_set_safe_mode(struct amdgpu_device *adev,
					int xcc_id)
{
	uint32_t data;
	unsigned i;

	data = RLC_SAFE_MODE__CMD_MASK;
	data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SAFE_MODE, data);

	/* wait for RLC_SAFE_MODE */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32_SOC15(GC, GET_INST(GC, xcc_id),
						regRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v12_1_xcc_unset_safe_mode(struct amdgpu_device *adev,
					  int xcc_id)
{
	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regRLC_SAFE_MODE, RLC_SAFE_MODE__CMD_MASK);
}

static void gfx_v12_1_update_perf_clk(struct amdgpu_device *adev,
				      bool enable)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++)
		gfx_v12_1_xcc_update_perf_clk(adev, enable, i);
}

static void gfx_v12_1_update_spm_vmid(struct amdgpu_device *adev,
				      int xcc_id,
				      struct amdgpu_ring *ring,
				      unsigned vmid)
{
	u32 reg, data;

	reg = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_SPM_MC_CNTL);
	if (amdgpu_sriov_is_pp_one_vf(adev))
		data = RREG32_NO_KIQ(reg);
	else
		data = RREG32(reg);

	data &= ~RLC_SPM_MC_CNTL__RLC_SPM_VMID_MASK;
	data |= (vmid & RLC_SPM_MC_CNTL__RLC_SPM_VMID_MASK) << RLC_SPM_MC_CNTL__RLC_SPM_VMID__SHIFT;

	if (amdgpu_sriov_is_pp_one_vf(adev))
		WREG32_SOC15_NO_KIQ(GC, GET_INST(GC, xcc_id), regRLC_SPM_MC_CNTL, data);
	else
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_SPM_MC_CNTL, data);

	if (ring
	    && amdgpu_sriov_is_pp_one_vf(adev)
	    && ((ring->funcs->type == AMDGPU_RING_TYPE_GFX)
		|| (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE))) {
		uint32_t reg = SOC15_REG_OFFSET(GC, GET_INST(GC, xcc_id), regRLC_SPM_MC_CNTL);
		amdgpu_ring_emit_wreg(ring, reg, data);
	}
}

static const struct amdgpu_rlc_funcs gfx_v12_1_rlc_funcs = {
	.is_rlc_enabled = gfx_v12_1_is_rlc_enabled,
	.set_safe_mode = gfx_v12_1_xcc_set_safe_mode,
	.unset_safe_mode = gfx_v12_1_xcc_unset_safe_mode,
	.init = gfx_v12_1_rlc_init,
	.get_csb_size = gfx_v12_1_get_csb_size,
	.get_csb_buffer = gfx_v12_1_get_csb_buffer,
	.resume = gfx_v12_1_rlc_resume,
	.stop = gfx_v12_1_rlc_stop,
	.reset = gfx_v12_1_rlc_reset,
	.start = gfx_v12_1_rlc_start,
	.update_spm_vmid = gfx_v12_1_update_spm_vmid,
};

#if 0
static void gfx_v12_cntl_power_gating(struct amdgpu_device *adev, bool enable)
{
	/* TODO */
}

static void gfx_v12_cntl_pg(struct amdgpu_device *adev, bool enable)
{
	/* TODO */
}
#endif

static int gfx_v12_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
					   enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = (state == AMD_PG_STATE_GATE);

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 1, 0):
		amdgpu_gfx_off_ctrl(adev, enable);
		break;
	default:
		break;
	}

	return 0;
}

static void gfx_v12_1_xcc_update_coarse_grain_clock_gating(struct amdgpu_device *adev,
							   bool enable, int xcc_id)
{
	uint32_t def, data;

	if (!(adev->cg_flags &
	      (AMD_CG_SUPPORT_GFX_CGCG |
	      AMD_CG_SUPPORT_GFX_CGLS |
	      AMD_CG_SUPPORT_GFX_3D_CGCG |
	      AMD_CG_SUPPORT_GFX_3D_CGLS)))
		return;

	if (enable) {
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id),
					  regRLC_CGTT_MGCG_OVERRIDE);

		/* unset CGCG override */
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGCG_OVERRIDE_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGCG ||
		    adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGLS)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_GFX3D_CG_OVERRIDE_MASK;

		/* update CGCG override bits */
		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id),
				     regRLC_CGTT_MGCG_OVERRIDE, data);

		/* enable cgcg FSM(0x0000363F) */
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL);

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG) {
			data &= ~RLC_CGCG_CGLS_CTRL__CGCG_GFX_IDLE_THRESHOLD_MASK;
			data |= (0x36 << RLC_CGCG_CGLS_CTRL__CGCG_GFX_IDLE_THRESHOLD__SHIFT) |
				 RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;
		}

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS) {
			data &= ~RLC_CGCG_CGLS_CTRL__CGLS_REP_COMPANSAT_DELAY_MASK;
			data |= (0x000F << RLC_CGCG_CGLS_CTRL__CGLS_REP_COMPANSAT_DELAY__SHIFT) |
				 RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;
		}

		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id),
				     regRLC_CGCG_CGLS_CTRL, data);

		/* set IDLE_POLL_COUNT(0x00900100) */
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_RB_WPTR_POLL_CNTL);

		data &= ~CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY_MASK;
		data &= ~CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK;
		data |= (0x0100 << CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT) |
			(0x0090 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);

		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_RB_WPTR_POLL_CNTL, data);

		data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_INT_CNTL);
		data = REG_SET_FIELD(data, CP_INT_CNTL, CNTX_BUSY_INT_ENABLE, 1);
		data = REG_SET_FIELD(data, CP_INT_CNTL, CNTX_EMPTY_INT_ENABLE, 1);
		data = REG_SET_FIELD(data, CP_INT_CNTL, CMP_BUSY_INT_ENABLE, 1);
		data = REG_SET_FIELD(data, CP_INT_CNTL, GFX_IDLE_INT_ENABLE, 1);
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regCP_INT_CNTL, data);
	} else {
		/* Program RLC_CGCG_CGLS_CTRL */
		def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL);

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)
			data &= ~RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data &= ~RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;

		if (def != data)
			WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGCG_CGLS_CTRL, data);
	}
}

static void gfx_v12_1_xcc_update_medium_grain_clock_gating(struct amdgpu_device *adev,
							   bool enable, int xcc_id)
{
	uint32_t data, def;
	if (!(adev->cg_flags & (AMD_CG_SUPPORT_GFX_MGCG | AMD_CG_SUPPORT_GFX_MGLS)))
		return;

	/* It is disabled by HW by default */
	if (enable) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG) {
			/* 1 - RLC_CGTT_MGCG_OVERRIDE */
			def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

			data &= ~(RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK);

			if (def != data)
				WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);
		}
	} else {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG) {
			def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

			data |= (RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK |
				 RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
				 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK);

			if (def != data)
				WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);
		}
	}
}

static void gfx_v12_1_xcc_update_repeater_fgcg(struct amdgpu_device *adev,
					       bool enable, int xcc_id)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_REPEATER_FGCG))
		return;

	def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

	if (enable)
		data &= ~(RLC_CGTT_MGCG_OVERRIDE__GFXIP_REPEATER_FGCG_OVERRIDE_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__RLC_REPEATER_FGCG_OVERRIDE_MASK);
	else
		data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_REPEATER_FGCG_OVERRIDE_MASK |
				RLC_CGTT_MGCG_OVERRIDE__RLC_REPEATER_FGCG_OVERRIDE_MASK;

	if (def != data)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);
}

static void gfx_v12_1_xcc_update_sram_fgcg(struct amdgpu_device *adev,
					   bool enable, int xcc_id)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_GFX_FGCG))
		return;

	def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

	if (enable)
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_FGCG_OVERRIDE_MASK;
	else
		data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_FGCG_OVERRIDE_MASK;

	if (def != data)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);
}

static void gfx_v12_1_xcc_update_perf_clk(struct amdgpu_device *adev,
					  bool enable, int xcc_id)
{
	uint32_t def, data;

	if (!(adev->cg_flags & AMD_CG_SUPPORT_GFX_PERF_CLK))
		return;

	def = data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE);

	if (enable)
		data &= ~RLC_CGTT_MGCG_OVERRIDE__PERFMON_CLOCK_STATE_MASK;
	else
		data |= RLC_CGTT_MGCG_OVERRIDE__PERFMON_CLOCK_STATE_MASK;

	if (def != data)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id), regRLC_CGTT_MGCG_OVERRIDE, data);
}

static int gfx_v12_1_xcc_update_gfx_clock_gating(struct amdgpu_device *adev,
					     bool enable, int xcc_id)
{
	amdgpu_gfx_rlc_enter_safe_mode(adev, xcc_id);

	gfx_v12_1_xcc_update_coarse_grain_clock_gating(adev, enable, xcc_id);

	gfx_v12_1_xcc_update_medium_grain_clock_gating(adev, enable, xcc_id);

	gfx_v12_1_xcc_update_repeater_fgcg(adev, enable, xcc_id);

	gfx_v12_1_xcc_update_sram_fgcg(adev, enable, xcc_id);

	gfx_v12_1_xcc_update_perf_clk(adev, enable, xcc_id);

	if (adev->cg_flags &
	    (AMD_CG_SUPPORT_GFX_MGCG |
	     AMD_CG_SUPPORT_GFX_CGLS |
	     AMD_CG_SUPPORT_GFX_CGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGLS))
		gfx_v12_1_xcc_enable_gui_idle_interrupt(adev, enable, xcc_id);

	amdgpu_gfx_rlc_exit_safe_mode(adev, xcc_id);

	return 0;
}

static int gfx_v12_1_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, num_xcc;

	if (amdgpu_sriov_vf(adev))
		return 0;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(12, 1, 0):
		for (i = 0; i < num_xcc; i++)
			gfx_v12_1_xcc_update_gfx_clock_gating(adev,
				  state == AMD_CG_STATE_GATE, i);
		break;
	default:
		break;
	}

	return 0;
}

static void gfx_v12_1_get_clockgating_state(struct amdgpu_ip_block *ip_block, u64 *flags)
{
	struct amdgpu_device *adev = ip_block->adev;
	int data;

	/* AMD_CG_SUPPORT_GFX_MGCG */
	data = RREG32_SOC15(GC, GET_INST(GC, 0), regRLC_CGTT_MGCG_OVERRIDE);
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_MGCG;

	/* AMD_CG_SUPPORT_REPEATER_FGCG */
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__GFXIP_REPEATER_FGCG_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_REPEATER_FGCG;

	/* AMD_CG_SUPPORT_GFX_FGCG */
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__GFXIP_FGCG_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_FGCG;

	/* AMD_CG_SUPPORT_GFX_PERF_CLK */
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__PERFMON_CLOCK_STATE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_PERF_CLK;

	/* AMD_CG_SUPPORT_GFX_CGCG */
	data = RREG32_SOC15(GC, GET_INST(GC, 0), regRLC_CGCG_CGLS_CTRL);
	if (data & RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGCG;

	/* AMD_CG_SUPPORT_GFX_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGLS;
}

static u64 gfx_v12_1_ring_get_rptr_compute(struct amdgpu_ring *ring)
{
	/* gfx12 hardware is 32bit rptr */
	return *(uint32_t *)ring->rptr_cpu_addr;
}

static u64 gfx_v12_1_ring_get_wptr_compute(struct amdgpu_ring *ring)
{
	u64 wptr;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell)
		wptr = atomic64_read((atomic64_t *)ring->wptr_cpu_addr);
	else
		BUG();
	return wptr;
}

static void gfx_v12_1_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell) {
		atomic64_set((atomic64_t *)ring->wptr_cpu_addr,
			     ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else {
		BUG(); /* only DOORBELL method supported on gfx12 now */
	}
}

static void gfx_v12_1_ring_emit_ib_compute(struct amdgpu_ring *ring,
					   struct amdgpu_job *job,
					   struct amdgpu_ib *ib,
					   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 control = INDIRECT_BUFFER_VALID | ib->length_dw | (vmid << 24);

	/* Currently, there is a high possibility to get wave ID mismatch
	 * between ME and GDS, leading to a hw deadlock, because ME generates
	 * different wave IDs than the GDS expects. This situation happens
	 * randomly when at least 5 compute pipes use GDS ordered append.
	 * The wave IDs generated by ME are also wrong after suspend/resume.
	 * Those are probably bugs somewhere else in the kernel driver.
	 *
	 * Writing GDS_COMPUTE_MAX_WAVE_ID resets wave ID counters in ME and
	 * GDS to 0 for this ring (me/pipe).
	 */
	if (ib->flags & AMDGPU_IB_FLAG_RESET_GDS_MAX_WAVE_ID) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		amdgpu_ring_write(ring, regGDS_COMPUTE_MAX_WAVE_ID);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	BUG_ON(ib->gpu_addr & 0x3); /* Dword align */
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
				(2 << 0) |
#endif
				lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, control);
}

static void gfx_v12_1_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
				     u64 seq, unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;

	/* RELEASE_MEM - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 6));
	amdgpu_ring_write(ring, (PACKET3_RELEASE_MEM_GCR_SEQ(1) |
				 PACKET3_RELEASE_MEM_GCR_GLV_WB |
				 PACKET3_RELEASE_MEM_GCR_GL2_WB |
				 PACKET3_RELEASE_MEM_GCR_GL2_SCOPE(2) |
				 PACKET3_RELEASE_MEM_TEMPORAL(3) |
				 PACKET3_RELEASE_MEM_EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 PACKET3_RELEASE_MEM_EVENT_INDEX(5)));
	amdgpu_ring_write(ring, (PACKET3_RELEASE_MEM_DATA_SEL(write64bit ? 2 : 1) |
				 PACKET3_RELEASE_MEM_INT_SEL(int_sel ? 2 : 0)));

	/*
	 * the address should be Qword aligned if 64bit write, Dword
	 * aligned if only send 32bit data low (discard data high)
	 */
	if (write64bit)
		BUG_ON(addr & 0x7);
	else
		BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v12_1_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	gfx_v12_1_wait_reg_mem(ring, usepfp, 1, 0, lower_32_bits(addr),
			       upper_32_bits(addr), seq, 0xffffffff, 4);
}

static void gfx_v12_1_ring_invalidate_tlbs(struct amdgpu_ring *ring,
				   uint16_t pasid, uint32_t flush_type,
				   bool all_hub, uint8_t dst_sel)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_INVALIDATE_TLBS, 0));
	amdgpu_ring_write(ring,
			  PACKET3_INVALIDATE_TLBS_DST_SEL(dst_sel) |
			  PACKET3_INVALIDATE_TLBS_ALL_HUB(all_hub) |
			  PACKET3_INVALIDATE_TLBS_PASID(pasid) |
			  PACKET3_INVALIDATE_TLBS_FLUSH_TYPE(flush_type));
}

static void gfx_v12_1_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* compute doesn't have PFP */
	if (ring->funcs->type == AMDGPU_RING_TYPE_GFX) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		amdgpu_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		amdgpu_ring_write(ring, 0x0);
	}
}

static void gfx_v12_1_ring_emit_fence_kiq(struct amdgpu_ring *ring, u64 addr,
					  u64 seq, unsigned int flags)
{
	struct amdgpu_device *adev = ring->adev;

	/* we only allocate 32bit for each seq wb address */
	BUG_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	/* write fence seq to the "addr" */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(5) | WR_CONFIRM));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* set register to trigger INT */
		amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
		amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
					 WRITE_DATA_DST_SEL(0) | WR_CONFIRM));
		amdgpu_ring_write(ring, SOC15_REG_OFFSET(GC, GET_INST(GC, 0), regCPC_INT_STATUS));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, 0x20000000); /* src_id is 178 */
	}
}

static void gfx_v12_1_ring_emit_rreg(struct amdgpu_ring *ring, uint32_t reg,
				     uint32_t reg_val_offs)
{
	struct amdgpu_device *adev = ring->adev;

	reg = soc_v1_0_normalize_xcc_reg_offset(reg);

	amdgpu_ring_write(ring, PACKET3(PACKET3_COPY_DATA, 4));
	amdgpu_ring_write(ring, 0 |	/* src: register*/
				(5 << 8) |	/* dst: memory */
				(1 << 20));	/* write confirm */
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, lower_32_bits(adev->wb.gpu_addr +
				reg_val_offs * 4));
	amdgpu_ring_write(ring, upper_32_bits(adev->wb.gpu_addr +
				reg_val_offs * 4));
}

static void gfx_v12_1_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg,
				     uint32_t val)
{
	uint32_t cmd = 0;

	reg = soc_v1_0_normalize_xcc_reg_offset(reg);

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_KIQ:
		cmd = (1 << 16); /* no inc addr */
		break;
	default:
		cmd = WR_CONFIRM;
		break;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, cmd);
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v12_1_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					uint32_t val, uint32_t mask)
{
	gfx_v12_1_wait_reg_mem(ring, 0, 0, 0, reg, 0, val, mask, 0x20);
}

static void gfx_v12_1_ring_emit_reg_write_reg_wait(struct amdgpu_ring *ring,
						   uint32_t reg0, uint32_t reg1,
						   uint32_t ref, uint32_t mask)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);

	gfx_v12_1_wait_reg_mem(ring, usepfp, 0, 1, reg0, reg1,
			       ref, mask, 0x20);
}

static void gfx_v12_1_xcc_set_compute_eop_interrupt_state(struct amdgpu_device *adev,
							int me, int pipe,
							enum amdgpu_interrupt_state state,
							int xcc_id)
{
	u32 mec_int_cntl, mec_int_cntl_reg;

	/*
	 * amdgpu controls only the first MEC. That's why this function only
	 * handles the setting of interrupts for this specific MEC. All other
	 * pipes' interrupts are set by amdkfd.
	 */

	if (me == 1) {
		switch (pipe) {
		case 0:
			mec_int_cntl_reg = SOC15_REG_OFFSET(
					GC, GET_INST(GC, xcc_id),
					regCP_ME1_PIPE0_INT_CNTL);
			break;
		case 1:
			mec_int_cntl_reg = SOC15_REG_OFFSET(
					GC, GET_INST(GC, xcc_id),
					regCP_ME1_PIPE1_INT_CNTL);
			break;
		case 2:
			mec_int_cntl_reg = SOC15_REG_OFFSET(
					GC, GET_INST(GC, xcc_id),
					regCP_ME1_PIPE2_INT_CNTL);
			break;
		case 3:
			mec_int_cntl_reg = SOC15_REG_OFFSET(
					GC, GET_INST(GC, xcc_id),
					regCP_ME1_PIPE3_INT_CNTL);
			break;
		default:
			DRM_DEBUG("invalid pipe %d\n", pipe);
			return;
		}
	} else {
		DRM_DEBUG("invalid me %d\n", me);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		mec_int_cntl = RREG32_XCC(mec_int_cntl_reg, xcc_id);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 0);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     GENERIC0_INT_ENABLE, 0);
		WREG32_XCC(mec_int_cntl_reg, mec_int_cntl, xcc_id);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		mec_int_cntl = RREG32_XCC(mec_int_cntl_reg, xcc_id);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 1);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     GENERIC0_INT_ENABLE, 1);
		WREG32_XCC(mec_int_cntl_reg, mec_int_cntl, xcc_id);
		break;
	default:
		break;
	}
}

static int gfx_v12_1_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		switch (type) {
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
			gfx_v12_1_xcc_set_compute_eop_interrupt_state(
					adev, 1, 0, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
			gfx_v12_1_xcc_set_compute_eop_interrupt_state(
					adev, 1, 1, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP:
			gfx_v12_1_xcc_set_compute_eop_interrupt_state(
					adev, 1, 2, state, i);
			break;
		case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP:
			gfx_v12_1_xcc_set_compute_eop_interrupt_state(
					adev, 1, 3, state, i);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int gfx_v12_1_eop_irq(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *source,
			     struct amdgpu_iv_entry *entry)
{
	u32 doorbell_offset = entry->src_data[0];
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;
	int i, xcc_id;

	DRM_DEBUG("IH: CP EOP\n");

	if (adev->enable_mes && doorbell_offset) {
		struct amdgpu_userq_fence_driver *fence_drv = NULL;
		struct xarray *xa = &adev->userq_xa;
		unsigned long flags;

		xa_lock_irqsave(xa, flags);
		fence_drv = xa_load(xa, doorbell_offset);
		if (fence_drv)
			amdgpu_userq_fence_driver_process(fence_drv);
		xa_unlock_irqrestore(xa, flags);
	} else {
		me_id = (entry->ring_id & 0x0c) >> 2;
		pipe_id = (entry->ring_id & 0x03) >> 0;
		queue_id = (entry->ring_id & 0x70) >> 4;
		xcc_id = gfx_v12_1_ih_to_xcc_inst(adev, entry->node_id);

		if (xcc_id == -EINVAL)
			return -EINVAL;

		switch (me_id) {
		case 0:
			if (pipe_id == 0)
				amdgpu_fence_process(&adev->gfx.gfx_ring[0]);
			else
				amdgpu_fence_process(&adev->gfx.gfx_ring[1]);
			break;
		case 1:
		case 2:
			for (i = 0; i < adev->gfx.num_compute_rings; i++) {
				ring = &adev->gfx.compute_ring
						[i +
						 xcc_id * adev->gfx.num_compute_rings];
				/* Per-queue interrupt is supported for MEC starting from VI.
				 * The interrupt can only be enabled/disabled per pipe instead
				 * of per queue.
				 */
				if ((ring->me == me_id) &&
				    (ring->pipe == pipe_id) &&
				    (ring->queue == queue_id))
					amdgpu_fence_process(ring);
			}
			break;
		}
	}

	return 0;
}

static int gfx_v12_1_set_priv_reg_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		for (i = 0; i < num_xcc; i++)
			WREG32_FIELD15_PREREG(GC, GET_INST(GC, i), CP_INT_CNTL_RING0,
					      PRIV_REG_INT_ENABLE,
					      state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v12_1_set_priv_inst_fault_state(struct amdgpu_device *adev,
					       struct amdgpu_irq_src *source,
					       unsigned type,
					       enum amdgpu_interrupt_state state)
{
	int i, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		for (i = 0; i < num_xcc; i++)
			WREG32_FIELD15_PREREG(GC, GET_INST(GC, i), CP_INT_CNTL_RING0,
				       PRIV_INSTR_INT_ENABLE,
				       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		break;
	default:
		break;
	}

	return 0;
}

static void gfx_v12_1_handle_priv_fault(struct amdgpu_device *adev,
					struct amdgpu_iv_entry *entry)
{
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;
	int i, xcc_id;

	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;
	xcc_id = gfx_v12_1_ih_to_xcc_inst(adev, entry->node_id);

	if (xcc_id == -EINVAL)
		return;

	switch (me_id) {
	case 0:
		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			ring = &adev->gfx.gfx_ring[i];
			/* we only enabled 1 gfx queue per pipe for now */
			if (ring->me == me_id && ring->pipe == pipe_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring
					[i +
					 xcc_id * adev->gfx.num_compute_rings];
			if (ring->me == me_id && ring->pipe == pipe_id &&
			    ring->queue == queue_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	default:
		BUG();
		break;
	}
}

static int gfx_v12_1_priv_reg_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	gfx_v12_1_handle_priv_fault(adev, entry);
	return 0;
}

static int gfx_v12_1_priv_inst_irq(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	gfx_v12_1_handle_priv_fault(adev, entry);
	return 0;
}

static void gfx_v12_1_emit_mem_sync(struct amdgpu_ring *ring)
{
	const unsigned int gcr_cntl =
			PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_INV(1) |
			PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_WB(1) |
			PACKET3_ACQUIRE_MEM_GCR_CNTL_GLV_INV(1) |
			PACKET3_ACQUIRE_MEM_GCR_CNTL_GLK_INV(1) |
			PACKET3_ACQUIRE_MEM_GCR_CNTL_GLI_INV(1) |
			PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_SCOPE(2);

	/* ACQUIRE_MEM - make one or more surfaces valid for use by the subsequent operations */
	amdgpu_ring_write(ring, PACKET3(PACKET3_ACQUIRE_MEM, 6));
	amdgpu_ring_write(ring, 0); /* CP_COHER_CNTL */
	amdgpu_ring_write(ring, 0xffffffff);  /* CP_COHER_SIZE */
	amdgpu_ring_write(ring, 0xffffff);  /* CP_COHER_SIZE_HI */
	amdgpu_ring_write(ring, 0); /* CP_COHER_BASE */
	amdgpu_ring_write(ring, 0);  /* CP_COHER_BASE_HI */
	amdgpu_ring_write(ring, 0x0000000A); /* POLL_INTERVAL */
	amdgpu_ring_write(ring, gcr_cntl); /* GCR_CNTL */
}

static const struct amd_ip_funcs gfx_v12_1_ip_funcs = {
	.name = "gfx_v12_1",
	.early_init = gfx_v12_1_early_init,
	.late_init = gfx_v12_1_late_init,
	.sw_init = gfx_v12_1_sw_init,
	.sw_fini = gfx_v12_1_sw_fini,
	.hw_init = gfx_v12_1_hw_init,
	.hw_fini = gfx_v12_1_hw_fini,
	.suspend = gfx_v12_1_suspend,
	.resume = gfx_v12_1_resume,
	.is_idle = gfx_v12_1_is_idle,
	.wait_for_idle = gfx_v12_1_wait_for_idle,
	.set_clockgating_state = gfx_v12_1_set_clockgating_state,
	.set_powergating_state = gfx_v12_1_set_powergating_state,
	.get_clockgating_state = gfx_v12_1_get_clockgating_state,
};

static const struct amdgpu_ring_funcs gfx_v12_1_ring_funcs_compute = {
	.type = AMDGPU_RING_TYPE_COMPUTE,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.get_rptr = gfx_v12_1_ring_get_rptr_compute,
	.get_wptr = gfx_v12_1_ring_get_wptr_compute,
	.set_wptr = gfx_v12_1_ring_set_wptr_compute,
	.emit_frame_size =
		7 + /* gfx_v12_1_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v12_1_ring_emit_vm_flush */
		8 + 8 + 8 + /* gfx_v12_1_ring_emit_fence x3 for user fence, vm fence */
		8, /* gfx_v12_1_emit_mem_sync */
	.emit_ib_size =	7, /* gfx_v12_1_ring_emit_ib_compute */
	.emit_ib = gfx_v12_1_ring_emit_ib_compute,
	.emit_fence = gfx_v12_1_ring_emit_fence,
	.emit_pipeline_sync = gfx_v12_1_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v12_1_ring_emit_vm_flush,
	.test_ring = gfx_v12_1_ring_test_ring,
	.test_ib = gfx_v12_1_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_wreg = gfx_v12_1_ring_emit_wreg,
	.emit_reg_wait = gfx_v12_1_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v12_1_ring_emit_reg_write_reg_wait,
	.emit_mem_sync = gfx_v12_1_emit_mem_sync,
};

static const struct amdgpu_ring_funcs gfx_v12_1_ring_funcs_kiq = {
	.type = AMDGPU_RING_TYPE_KIQ,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.get_rptr = gfx_v12_1_ring_get_rptr_compute,
	.get_wptr = gfx_v12_1_ring_get_wptr_compute,
	.set_wptr = gfx_v12_1_ring_set_wptr_compute,
	.emit_frame_size =
		7 + /* gfx_v12_1_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v12_1_ring_emit_vm_flush */
		8 + 8 + 8, /* gfx_v12_1_ring_emit_fence_kiq x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v12_1_ring_emit_ib_compute */
	.emit_ib = gfx_v12_1_ring_emit_ib_compute,
	.emit_fence = gfx_v12_1_ring_emit_fence_kiq,
	.test_ring = gfx_v12_1_ring_test_ring,
	.test_ib = gfx_v12_1_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_rreg = gfx_v12_1_ring_emit_rreg,
	.emit_wreg = gfx_v12_1_ring_emit_wreg,
	.emit_reg_wait = gfx_v12_1_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v12_1_ring_emit_reg_write_reg_wait,
};

static void gfx_v12_1_set_ring_funcs(struct amdgpu_device *adev)
{
	int i, j, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		adev->gfx.kiq[i].ring.funcs = &gfx_v12_1_ring_funcs_kiq;

		for (j = 0; j < adev->gfx.num_compute_rings; j++)
			adev->gfx.compute_ring[j + i * adev->gfx.num_compute_rings].funcs =
						&gfx_v12_1_ring_funcs_compute;
	}
}

static const struct amdgpu_irq_src_funcs gfx_v12_1_eop_irq_funcs = {
	.set = gfx_v12_1_set_eop_interrupt_state,
	.process = gfx_v12_1_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v12_1_priv_reg_irq_funcs = {
	.set = gfx_v12_1_set_priv_reg_fault_state,
	.process = gfx_v12_1_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v12_1_priv_inst_irq_funcs = {
	.set = gfx_v12_1_set_priv_inst_fault_state,
	.process = gfx_v12_1_priv_inst_irq,
};

static void gfx_v12_1_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v12_1_eop_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v12_1_priv_reg_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v12_1_priv_inst_irq_funcs;
}

static void gfx_v12_1_set_imu_funcs(struct amdgpu_device *adev)
{
	if (adev->flags & AMD_IS_APU)
		adev->gfx.imu.mode = MISSION_MODE;
	else
		adev->gfx.imu.mode = DEBUG_MODE;
	if (!amdgpu_sriov_vf(adev))
		adev->gfx.imu.funcs = &gfx_v12_1_imu_funcs;
}

static void gfx_v12_1_set_rlc_funcs(struct amdgpu_device *adev)
{
	adev->gfx.rlc.funcs = &gfx_v12_1_rlc_funcs;
}

static void gfx_v12_1_set_mqd_funcs(struct amdgpu_device *adev)
{
	/* set compute eng mqd */
	adev->mqds[AMDGPU_HW_IP_COMPUTE].mqd_size =
		sizeof(struct v12_1_compute_mqd);
	adev->mqds[AMDGPU_HW_IP_COMPUTE].init_mqd =
		gfx_v12_1_compute_mqd_init;
}

static void gfx_v12_1_set_user_cu_inactive_bitmap_per_sh(struct amdgpu_device *adev,
							  u32 bitmap, int xcc_id)
{
	u32 data;

	if (!bitmap)
		return;

	data = bitmap << GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_WGPS__SHIFT;
	data &= GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_WGPS_MASK;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regGC_USER_SHADER_ARRAY_CONFIG, data);
}

static u32 gfx_v12_1_get_cu_active_bitmap_per_sh(struct amdgpu_device *adev,
						 int xcc_id)
{
	u32 data, mask;

	data = RREG32_SOC15(GC, GET_INST(GC, xcc_id), regCC_GC_SHADER_ARRAY_CONFIG);
	data |= RREG32_SOC15(GC, GET_INST(GC, xcc_id), regGC_USER_SHADER_ARRAY_CONFIG);

	data &= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_WGPS_MASK;
	data >>= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_WGPS__SHIFT;

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_cu_per_sh);

	return (~data) & mask;
}

static int gfx_v12_1_get_cu_info(struct amdgpu_device *adev,
				 struct amdgpu_cu_info *cu_info)
{
	int i, j, k, counter, xcc_id, active_cu_number = 0;
	u32 mask, bitmap;
	unsigned int disable_masks[2 * 2];

	if (!adev || !cu_info)
		return -EINVAL;

	if (adev->gfx.config.max_shader_engines > 2 ||
	    adev->gfx.config.max_sh_per_se > 2) {
		dev_err(adev->dev,
			"Max SE (%d) and Max SA per SE (%d) is greater than expected\n",
			adev->gfx.config.max_shader_engines,
			adev->gfx.config.max_sh_per_se);
		return -EINVAL;
	}

	amdgpu_gfx_parse_disable_cu(adev, disable_masks,
				    adev->gfx.config.max_shader_engines,
				    adev->gfx.config.max_sh_per_se);

	mutex_lock(&adev->grbm_idx_mutex);
	for (xcc_id = 0; xcc_id < NUM_XCC(adev->gfx.xcc_mask); xcc_id++) {
		for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
			for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
				bitmap = i * adev->gfx.config.max_sh_per_se + j;
				if (!((gfx_v12_1_get_sa_active_bitmap(adev, xcc_id) >> bitmap) & 1))
					continue;
				mask = 1;
				counter = 0;
				gfx_v12_1_xcc_select_se_sh(adev, i, j, 0xffffffff, xcc_id);
				gfx_v12_1_set_user_cu_inactive_bitmap_per_sh(
					adev,
					disable_masks[i * adev->gfx.config.max_sh_per_se + j],
					xcc_id);
				bitmap = gfx_v12_1_get_cu_active_bitmap_per_sh(adev, xcc_id);

				cu_info->bitmap[xcc_id][i][j] = bitmap;

				for (k = 0; k < adev->gfx.config.max_cu_per_sh; k++) {
					if (bitmap & mask)
						counter++;

					mask <<= 1;
				}
				active_cu_number += counter;
			}
		}
		gfx_v12_1_xcc_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, xcc_id);
	}
	mutex_unlock(&adev->grbm_idx_mutex);

	cu_info->number = active_cu_number;
	cu_info->simd_per_cu = NUM_SIMD_PER_CU_GFX12_1;
	cu_info->lds_size = 320;

	return 0;
}

const struct amdgpu_ip_block_version gfx_v12_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 12,
	.minor = 1,
	.rev = 0,
	.funcs = &gfx_v12_1_ip_funcs,
};

static int gfx_v12_1_xcp_resume(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	uint32_t tmp_mask;
	int i, r;

	/* TODO : Initialize golden regs */
	/* gfx_v12_1_init_golden_registers(adev); */

	tmp_mask = inst_mask;
	for_each_inst(i, tmp_mask)
		gfx_v12_1_xcc_constants_init(adev, i);

	if (!amdgpu_sriov_vf(adev)) {
		tmp_mask = inst_mask;
		for_each_inst(i, tmp_mask) {
			r = gfx_v12_1_xcc_rlc_resume(adev, i);
			if (r)
				return r;
		}
	}

	r = gfx_v12_1_xcc_cp_resume(adev, inst_mask);

	return r;
}

static int gfx_v12_1_xcp_suspend(void *handle, uint32_t inst_mask)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for_each_inst(i, inst_mask)
		gfx_v12_1_xcc_fini(adev, i);

	return 0;
}

struct amdgpu_xcp_ip_funcs gfx_v12_1_xcp_funcs = {
	.suspend = &gfx_v12_1_xcp_suspend,
	.resume = &gfx_v12_1_xcp_resume
};

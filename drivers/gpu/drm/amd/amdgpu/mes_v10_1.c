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

#include <linux/firmware.h>
#include <linux/module.h>
#include "amdgpu.h"
#include "soc15_common.h"
#include "nv.h"
#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "v10_structs.h"

MODULE_FIRMWARE("amdgpu/navi10_mes.bin");

#define MES_EOP_SIZE   2048

static void mes_v10_1_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		atomic64_set((atomic64_t*)&adev->wb.wb[ring->wptr_offs],
			     ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else {
		BUG();
	}
}

static u64 mes_v10_1_ring_get_rptr(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs];
}

static u64 mes_v10_1_ring_get_wptr(struct amdgpu_ring *ring)
{
	u64 wptr;

	if (ring->use_doorbell)
		wptr = atomic64_read((atomic64_t *)
				     &ring->adev->wb.wb[ring->wptr_offs]);
	else
		BUG();
	return wptr;
}

static const struct amdgpu_ring_funcs mes_v10_1_ring_funcs = {
	.type = AMDGPU_RING_TYPE_MES,
	.align_mask = 1,
	.nop = 0,
	.support_64bit_ptrs = true,
	.get_rptr = mes_v10_1_ring_get_rptr,
	.get_wptr = mes_v10_1_ring_get_wptr,
	.set_wptr = mes_v10_1_ring_set_wptr,
	.insert_nop = amdgpu_ring_insert_nop,
};

static int mes_v10_1_add_hw_queue(struct amdgpu_mes *mes,
				  struct mes_add_queue_input *input)
{
	return 0;
}

static int mes_v10_1_remove_hw_queue(struct amdgpu_mes *mes,
				     struct mes_remove_queue_input *input)
{
	return 0;
}

static int mes_v10_1_suspend_gang(struct amdgpu_mes *mes,
				  struct mes_suspend_gang_input *input)
{
	return 0;
}

static int mes_v10_1_resume_gang(struct amdgpu_mes *mes,
				 struct mes_resume_gang_input *input)
{
	return 0;
}

static const struct amdgpu_mes_funcs mes_v10_1_funcs = {
	.add_hw_queue = mes_v10_1_add_hw_queue,
	.remove_hw_queue = mes_v10_1_remove_hw_queue,
	.suspend_gang = mes_v10_1_suspend_gang,
	.resume_gang = mes_v10_1_resume_gang,
};

static int mes_v10_1_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	const struct mes_firmware_header_v1_0 *mes_hdr;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
		chip_name = "navi10";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes.bin", chip_name);
	err = request_firmware(&adev->mes.fw, fw_name, adev->dev);
	if (err)
		return err;

	err = amdgpu_ucode_validate(adev->mes.fw);
	if (err) {
		release_firmware(adev->mes.fw);
		adev->mes.fw = NULL;
		return err;
	}

	mes_hdr = (const struct mes_firmware_header_v1_0 *)adev->mes.fw->data;
	adev->mes.ucode_fw_version = le32_to_cpu(mes_hdr->mes_ucode_version);
	adev->mes.ucode_fw_version =
		le32_to_cpu(mes_hdr->mes_ucode_data_version);
	adev->mes.uc_start_addr =
		le32_to_cpu(mes_hdr->mes_uc_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_uc_start_addr_hi)) << 32);
	adev->mes.data_start_addr =
		le32_to_cpu(mes_hdr->mes_data_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_data_start_addr_hi)) << 32);

	return 0;
}

static void mes_v10_1_free_microcode(struct amdgpu_device *adev)
{
	release_firmware(adev->mes.fw);
	adev->mes.fw = NULL;
}

static int mes_v10_1_allocate_ucode_buffer(struct amdgpu_device *adev)
{
	int r;
	const struct mes_firmware_header_v1_0 *mes_hdr;
	const __le32 *fw_data;
	unsigned fw_size;

	mes_hdr = (const struct mes_firmware_header_v1_0 *)
		adev->mes.fw->data;

	fw_data = (const __le32 *)(adev->mes.fw->data +
		   le32_to_cpu(mes_hdr->mes_ucode_offset_bytes));
	fw_size = le32_to_cpu(mes_hdr->mes_ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->mes.ucode_fw_obj,
				      &adev->mes.ucode_fw_gpu_addr,
				      (void **)&adev->mes.ucode_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create mes fw bo\n", r);
		return r;
	}

	memcpy(adev->mes.ucode_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->mes.ucode_fw_obj);
	amdgpu_bo_unreserve(adev->mes.ucode_fw_obj);

	return 0;
}

static int mes_v10_1_allocate_ucode_data_buffer(struct amdgpu_device *adev)
{
	int r;
	const struct mes_firmware_header_v1_0 *mes_hdr;
	const __le32 *fw_data;
	unsigned fw_size;

	mes_hdr = (const struct mes_firmware_header_v1_0 *)
		adev->mes.fw->data;

	fw_data = (const __le32 *)(adev->mes.fw->data +
		   le32_to_cpu(mes_hdr->mes_ucode_data_offset_bytes));
	fw_size = le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      64 * 1024, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->mes.data_fw_obj,
				      &adev->mes.data_fw_gpu_addr,
				      (void **)&adev->mes.data_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create mes data fw bo\n", r);
		return r;
	}

	memcpy(adev->mes.data_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->mes.data_fw_obj);
	amdgpu_bo_unreserve(adev->mes.data_fw_obj);

	return 0;
}

static void mes_v10_1_free_ucode_buffers(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->mes.data_fw_obj,
			      &adev->mes.data_fw_gpu_addr,
			      (void **)&adev->mes.data_fw_ptr);

	amdgpu_bo_free_kernel(&adev->mes.ucode_fw_obj,
			      &adev->mes.ucode_fw_gpu_addr,
			      (void **)&adev->mes.ucode_fw_ptr);
}

static void mes_v10_1_enable(struct amdgpu_device *adev, bool enable)
{
	uint32_t data = 0;

	if (enable) {
		data = RREG32_SOC15(GC, 0, mmCP_MES_CNTL);
		data = REG_SET_FIELD(data, CP_MES_CNTL, MES_PIPE0_RESET, 1);
		WREG32_SOC15(GC, 0, mmCP_MES_CNTL, data);

		/* set ucode start address */
		WREG32_SOC15(GC, 0, mmCP_MES_PRGRM_CNTR_START,
			     (uint32_t)(adev->mes.uc_start_addr) >> 2);

		/* clear BYPASS_UNCACHED to avoid hangs after interrupt. */
		data = RREG32_SOC15(GC, 0, mmCP_MES_DC_OP_CNTL);
		data = REG_SET_FIELD(data, CP_MES_DC_OP_CNTL,
				     BYPASS_UNCACHED, 0);
		WREG32_SOC15(GC, 0, mmCP_MES_DC_OP_CNTL, data);

		/* unhalt MES and activate pipe0 */
		data = REG_SET_FIELD(0, CP_MES_CNTL, MES_PIPE0_ACTIVE, 1);
		WREG32_SOC15(GC, 0, mmCP_MES_CNTL, data);
	} else {
		data = RREG32_SOC15(GC, 0, mmCP_MES_CNTL);
		data = REG_SET_FIELD(data, CP_MES_CNTL, MES_PIPE0_ACTIVE, 0);
		data = REG_SET_FIELD(data, CP_MES_CNTL,
				     MES_INVALIDATE_ICACHE, 1);
		data = REG_SET_FIELD(data, CP_MES_CNTL, MES_PIPE0_RESET, 1);
		data = REG_SET_FIELD(data, CP_MES_CNTL, MES_HALT, 1);
		WREG32_SOC15(GC, 0, mmCP_MES_CNTL, data);
	}
}

/* This function is for backdoor MES firmware */
static int mes_v10_1_load_microcode(struct amdgpu_device *adev)
{
	int r;
	uint32_t data;

	if (!adev->mes.fw)
		return -EINVAL;

	r = mes_v10_1_allocate_ucode_buffer(adev);
	if (r)
		return r;

	r = mes_v10_1_allocate_ucode_data_buffer(adev);
	if (r) {
		mes_v10_1_free_ucode_buffers(adev);
		return r;
	}

	mes_v10_1_enable(adev, false);

	WREG32_SOC15(GC, 0, mmCP_MES_IC_BASE_CNTL, 0);

	mutex_lock(&adev->srbm_mutex);
	/* me=3, pipe=0, queue=0 */
	nv_grbm_select(adev, 3, 0, 0, 0);

	/* set ucode start address */
	WREG32_SOC15(GC, 0, mmCP_MES_PRGRM_CNTR_START,
		     (uint32_t)(adev->mes.uc_start_addr) >> 2);

	/* set ucode fimrware address */
	WREG32_SOC15(GC, 0, mmCP_MES_IC_BASE_LO,
		     lower_32_bits(adev->mes.ucode_fw_gpu_addr));
	WREG32_SOC15(GC, 0, mmCP_MES_IC_BASE_HI,
		     upper_32_bits(adev->mes.ucode_fw_gpu_addr));

	/* set ucode instruction cache boundary to 2M-1 */
	WREG32_SOC15(GC, 0, mmCP_MES_MIBOUND_LO, 0x1FFFFF);

	/* set ucode data firmware address */
	WREG32_SOC15(GC, 0, mmCP_MES_MDBASE_LO,
		     lower_32_bits(adev->mes.data_fw_gpu_addr));
	WREG32_SOC15(GC, 0, mmCP_MES_MDBASE_HI,
		     upper_32_bits(adev->mes.data_fw_gpu_addr));

	/* Set 0x3FFFF (256K-1) to CP_MES_MDBOUND_LO */
	WREG32_SOC15(GC, 0, mmCP_MES_MDBOUND_LO, 0x3FFFF);

	/* invalidate ICACHE */
	data = RREG32_SOC15(GC, 0, mmCP_MES_IC_OP_CNTL);
	data = REG_SET_FIELD(data, CP_MES_IC_OP_CNTL, PRIME_ICACHE, 0);
	data = REG_SET_FIELD(data, CP_MES_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_MES_IC_OP_CNTL, data);

	/* prime the ICACHE. */
	data = RREG32_SOC15(GC, 0, mmCP_MES_IC_OP_CNTL);
	data = REG_SET_FIELD(data, CP_MES_IC_OP_CNTL, PRIME_ICACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_MES_IC_OP_CNTL, data);

	nv_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	return 0;
}

static int mes_v10_1_allocate_eop_buf(struct amdgpu_device *adev)
{
	int r;
	u32 *eop;

	r = amdgpu_bo_create_reserved(adev, MES_EOP_SIZE, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_GTT,
				      &adev->mes.eop_gpu_obj,
				      &adev->mes.eop_gpu_addr,
				      (void **)&eop);
	if (r) {
		dev_warn(adev->dev, "(%d) create EOP bo failed\n", r);
		return r;
	}

	memset(eop, 0, adev->mes.eop_gpu_obj->tbo.mem.size);

	amdgpu_bo_kunmap(adev->mes.eop_gpu_obj);
	amdgpu_bo_unreserve(adev->mes.eop_gpu_obj);

	return 0;
}

static int mes_v10_1_mqd_init(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_compute_mqd *mqd = ring->mqd_ptr;
	uint64_t hqd_gpu_addr, wb_gpu_addr, eop_base_addr;
	uint32_t tmp;

	mqd->header = 0xC0310800;
	mqd->compute_pipelinestat_enable = 0x00000001;
	mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
	mqd->compute_misc_reserved = 0x00000003;

	eop_base_addr = ring->eop_gpu_addr >> 8;
	mqd->cp_hqd_eop_base_addr_lo = eop_base_addr;
	mqd->cp_hqd_eop_base_addr_hi = upper_32_bits(eop_base_addr);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_EOP_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
			(order_base_2(MES_EOP_SIZE / 4) - 1));

	mqd->cp_hqd_eop_control = tmp;

	/* enable doorbell? */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL);

	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	}
	else
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 0);

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* disable the queue if it's active */
	ring->wptr = 0;
	mqd->cp_hqd_dequeue_request = 0;
	mqd->cp_hqd_pq_rptr = 0;
	mqd->cp_hqd_pq_wptr_lo = 0;
	mqd->cp_hqd_pq_wptr_hi = 0;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr_lo = ring->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(ring->mqd_gpu_addr);

	/* set MQD vmid to 0 */
	tmp = RREG32_SOC15(GC, 0, mmCP_MQD_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
	mqd->cp_mqd_control = tmp;

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	hqd_gpu_addr = ring->gpu_addr >> 8;
	mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
	mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, QUEUE_SIZE,
			    (order_base_2(ring->ring_size / 4) - 1));
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, RPTR_BLOCK_SIZE,
			    ((order_base_2(AMDGPU_GPU_PAGE_SIZE / 4) - 1) << 8));
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ENDIAN_SWAP, 1);
#endif
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, UNORD_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, TUNNEL_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, PRIV_STATE, 1);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, KMD_QUEUE, 1);
	mqd->cp_hqd_pq_control = tmp;

	/* set the wb address whether it's enabled or not */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_rptr_report_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	mqd->cp_hqd_pq_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffff8;
	mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	tmp = 0;
	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				DOORBELL_OFFSET, ring->doorbell_index);

		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	ring->wptr = 0;
	mqd->cp_hqd_pq_rptr = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_RPTR);

	/* set the vmid for the queue */
	mqd->cp_hqd_vmid = 0;

	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PERSISTENT_STATE);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x53);
	mqd->cp_hqd_persistent_state = tmp;

	/* set MIN_IB_AVAIL_SIZE */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_IB_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MIN_IB_AVAIL_SIZE, 3);
	mqd->cp_hqd_ib_control = tmp;

	/* activate the queue */
	mqd->cp_hqd_active = 1;
	return 0;
}

static int mes_v10_1_ring_init(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->mes.ring;

	ring->funcs = &mes_v10_1_ring_funcs;

	ring->me = 3;
	ring->pipe = 0;
	ring->queue = 0;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->doorbell_index = adev->doorbell_index.mes_ring << 1;
	ring->eop_gpu_addr = adev->mes.eop_gpu_addr;
	sprintf(ring->name, "mes_%d.%d.%d", ring->me, ring->pipe, ring->queue);

	r = amdgpu_ring_init(adev, ring, 1024, NULL, 0, AMDGPU_RING_PRIO_DEFAULT);
	if (r)
		return r;

	return 0;
}

static int mes_v10_1_mqd_sw_init(struct amdgpu_device *adev)
{
	int r, mqd_size = sizeof(struct v10_compute_mqd);
	struct amdgpu_ring *ring = &adev->mes.ring;

	if (ring->mqd_obj)
		return 0;

	r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT, &ring->mqd_obj,
				    &ring->mqd_gpu_addr, &ring->mqd_ptr);
	if (r) {
		dev_warn(adev->dev, "failed to create ring mqd bo (%d)", r);
		return r;
	}

	/* prepare MQD backup */
	adev->mes.mqd_backup = kmalloc(mqd_size, GFP_KERNEL);
	if (!adev->mes.mqd_backup)
		dev_warn(adev->dev,
			 "no memory to create MQD backup for ring %s\n",
			 ring->name);

	return 0;
}

static int mes_v10_1_sw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->mes.adev = adev;

	r = mes_v10_1_init_microcode(adev);
	if (r)
		return r;

	r = mes_v10_1_allocate_eop_buf(adev);
	if (r)
		return r;

	r = mes_v10_1_mqd_sw_init(adev);
	if (r)
		return r;

	return 0;
}

static int mes_v10_1_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	kfree(adev->mes.mqd_backup);

	amdgpu_bo_free_kernel(&adev->mes.ring.mqd_obj,
			      &adev->mes.ring.mqd_gpu_addr,
			      &adev->mes.ring.mqd_ptr);

	amdgpu_bo_free_kernel(&adev->mes.eop_gpu_obj,
			      &adev->mes.eop_gpu_addr,
			      NULL);

	mes_v10_1_free_microcode(adev);

	return 0;
}

static int mes_v10_1_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		r = mes_v10_1_load_microcode(adev);
		if (r) {
			DRM_ERROR("failed to MES fw, r=%d\n", r);
			return r;
		}
	} else {
		DRM_ERROR("only support direct fw loading on MES\n");
		return -EINVAL;
	}

	mes_v10_1_enable(adev, true);

	return 0;
}

static int mes_v10_1_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	mes_v10_1_enable(adev, false);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT)
		mes_v10_1_free_ucode_buffers(adev);

	return 0;
}

static int mes_v10_1_suspend(void *handle)
{
	return 0;
}

static int mes_v10_1_resume(void *handle)
{
	return 0;
}

static const struct amd_ip_funcs mes_v10_1_ip_funcs = {
	.name = "mes_v10_1",
	.sw_init = mes_v10_1_sw_init,
	.sw_fini = mes_v10_1_sw_fini,
	.hw_init = mes_v10_1_hw_init,
	.hw_fini = mes_v10_1_hw_fini,
	.suspend = mes_v10_1_suspend,
	.resume = mes_v10_1_resume,
};

const struct amdgpu_ip_block_version mes_v10_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_MES,
	.major = 10,
	.minor = 1,
	.rev = 0,
	.funcs = &mes_v10_1_ip_funcs,
};

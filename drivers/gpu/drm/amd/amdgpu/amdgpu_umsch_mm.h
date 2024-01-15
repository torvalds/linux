/* SPDX-License-Identifier: MIT */
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

#ifndef __AMDGPU_UMSCH_MM_H__
#define __AMDGPU_UMSCH_MM_H__

enum UMSCH_SWIP_ENGINE_TYPE {
	UMSCH_SWIP_ENGINE_TYPE_VCN0 = 0,
	UMSCH_SWIP_ENGINE_TYPE_VCN1 = 1,
	UMSCH_SWIP_ENGINE_TYPE_VCN = 2,
	UMSCH_SWIP_ENGINE_TYPE_VPE = 3,
	UMSCH_SWIP_ENGINE_TYPE_MAX
};

enum UMSCH_SWIP_AFFINITY_TYPE {
	UMSCH_SWIP_AFFINITY_TYPE_ANY = 0,
	UMSCH_SWIP_AFFINITY_TYPE_VCN0 = 1,
	UMSCH_SWIP_AFFINITY_TYPE_VCN1 = 2,
	UMSCH_SWIP_AFFINITY_TYPE_MAX
};

enum UMSCH_CONTEXT_PRIORITY_LEVEL {
	CONTEXT_PRIORITY_LEVEL_IDLE = 0,
	CONTEXT_PRIORITY_LEVEL_NORMAL = 1,
	CONTEXT_PRIORITY_LEVEL_FOCUS = 2,
	CONTEXT_PRIORITY_LEVEL_REALTIME = 3,
	CONTEXT_PRIORITY_NUM_LEVELS
};

struct umsch_mm_set_resource_input {
	uint32_t vmid_mask_mm_vcn;
	uint32_t vmid_mask_mm_vpe;
	uint32_t logging_vmid;
	uint32_t engine_mask;
	union {
		struct {
			uint32_t disable_reset : 1;
			uint32_t disable_umsch_mm_log : 1;
			uint32_t reserved : 30;
		};
		uint32_t uint32_all;
	};
};

struct umsch_mm_add_queue_input {
	uint32_t process_id;
	uint64_t page_table_base_addr;
	uint64_t process_va_start;
	uint64_t process_va_end;
	uint64_t process_quantum;
	uint64_t process_csa_addr;
	uint64_t context_quantum;
	uint64_t context_csa_addr;
	uint32_t inprocess_context_priority;
	enum UMSCH_CONTEXT_PRIORITY_LEVEL context_global_priority_level;
	uint32_t doorbell_offset_0;
	uint32_t doorbell_offset_1;
	enum UMSCH_SWIP_ENGINE_TYPE engine_type;
	uint32_t affinity;
	enum UMSCH_SWIP_AFFINITY_TYPE affinity_type;
	uint64_t mqd_addr;
	uint64_t h_context;
	uint64_t h_queue;
	uint32_t vm_context_cntl;

	struct {
		uint32_t is_context_suspended : 1;
		uint32_t reserved : 31;
	};
};

struct umsch_mm_remove_queue_input {
	uint32_t doorbell_offset_0;
	uint32_t doorbell_offset_1;
	uint64_t context_csa_addr;
};

struct MQD_INFO {
	uint32_t rb_base_hi;
	uint32_t rb_base_lo;
	uint32_t rb_size;
	uint32_t wptr_val;
	uint32_t rptr_val;
	uint32_t unmapped;
};

struct amdgpu_umsch_mm;

struct umsch_mm_funcs {
	int (*set_hw_resources)(struct amdgpu_umsch_mm *umsch);
	int (*add_queue)(struct amdgpu_umsch_mm *umsch,
			 struct umsch_mm_add_queue_input *input);
	int (*remove_queue)(struct amdgpu_umsch_mm *umsch,
			    struct umsch_mm_remove_queue_input *input);
	int (*set_regs)(struct amdgpu_umsch_mm *umsch);
	int (*init_microcode)(struct amdgpu_umsch_mm *umsch);
	int (*load_microcode)(struct amdgpu_umsch_mm *umsch);
	int (*ring_init)(struct amdgpu_umsch_mm *umsch);
	int (*ring_start)(struct amdgpu_umsch_mm *umsch);
	int (*ring_stop)(struct amdgpu_umsch_mm *umsch);
	int (*ring_fini)(struct amdgpu_umsch_mm *umsch);
};

struct amdgpu_umsch_mm {
	struct amdgpu_ring		ring;

	uint32_t			rb_wptr;
	uint32_t			rb_rptr;

	const struct umsch_mm_funcs	*funcs;

	const struct firmware		*fw;
	uint32_t			fw_version;
	uint32_t			feature_version;

	struct amdgpu_bo		*ucode_fw_obj;
	uint64_t			ucode_fw_gpu_addr;
	uint32_t			*ucode_fw_ptr;
	uint64_t			irq_start_addr;
	uint64_t			uc_start_addr;
	uint32_t			ucode_size;

	struct amdgpu_bo		*data_fw_obj;
	uint64_t			data_fw_gpu_addr;
	uint32_t			*data_fw_ptr;
	uint64_t			data_start_addr;
	uint32_t			data_size;

	struct amdgpu_bo		*cmd_buf_obj;
	uint64_t			cmd_buf_gpu_addr;
	uint32_t			*cmd_buf_ptr;
	uint32_t			*cmd_buf_curr_ptr;

	uint32_t			wb_index;
	uint64_t			sch_ctx_gpu_addr;
	uint32_t			*sch_ctx_cpu_addr;

	uint32_t			vmid_mask_mm_vcn;
	uint32_t			vmid_mask_mm_vpe;
	uint32_t			engine_mask;
	uint32_t			vcn0_hqd_mask;
	uint32_t			vcn1_hqd_mask;
	uint32_t			vcn_hqd_mask[2];
	uint32_t			vpe_hqd_mask;
	uint32_t			agdb_index[CONTEXT_PRIORITY_NUM_LEVELS];

	struct mutex			mutex_hidden;
};

int amdgpu_umsch_mm_submit_pkt(struct amdgpu_umsch_mm *umsch, void *pkt, int ndws);
int amdgpu_umsch_mm_query_fence(struct amdgpu_umsch_mm *umsch);

int amdgpu_umsch_mm_init_microcode(struct amdgpu_umsch_mm *umsch);
int amdgpu_umsch_mm_allocate_ucode_buffer(struct amdgpu_umsch_mm *umsch);
int amdgpu_umsch_mm_allocate_ucode_data_buffer(struct amdgpu_umsch_mm *umsch);

int amdgpu_umsch_mm_psp_execute_cmd_buf(struct amdgpu_umsch_mm *umsch);

int amdgpu_umsch_mm_ring_init(struct amdgpu_umsch_mm *umsch);

#define WREG32_SOC15_UMSCH(reg, value)								\
	do {											\
		uint32_t reg_offset = adev->reg_offset[VCN_HWIP][0][reg##_BASE_IDX] + reg;	\
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {				\
			*adev->umsch_mm.cmd_buf_curr_ptr++ = (reg_offset << 2);			\
			*adev->umsch_mm.cmd_buf_curr_ptr++ = value;				\
		} else	{									\
			WREG32(reg_offset, value);						\
		}										\
	} while (0)

#define umsch_mm_set_hw_resources(umsch) \
	((umsch)->funcs->set_hw_resources ? (umsch)->funcs->set_hw_resources((umsch)) : 0)
#define umsch_mm_add_queue(umsch, input) \
	((umsch)->funcs->add_queue ? (umsch)->funcs->add_queue((umsch), (input)) : 0)
#define umsch_mm_remove_queue(umsch, input) \
	((umsch)->funcs->remove_queue ? (umsch)->funcs->remove_queue((umsch), (input)) : 0)

#define umsch_mm_set_regs(umsch) \
	((umsch)->funcs->set_regs ? (umsch)->funcs->set_regs((umsch)) : 0)
#define umsch_mm_init_microcode(umsch) \
	((umsch)->funcs->init_microcode ? (umsch)->funcs->init_microcode((umsch)) : 0)
#define umsch_mm_load_microcode(umsch) \
	((umsch)->funcs->load_microcode ? (umsch)->funcs->load_microcode((umsch)) : 0)

#define umsch_mm_ring_init(umsch) \
	((umsch)->funcs->ring_init ? (umsch)->funcs->ring_init((umsch)) : 0)
#define umsch_mm_ring_start(umsch) \
	((umsch)->funcs->ring_start ? (umsch)->funcs->ring_start((umsch)) : 0)
#define umsch_mm_ring_stop(umsch) \
	((umsch)->funcs->ring_stop ? (umsch)->funcs->ring_stop((umsch)) : 0)
#define umsch_mm_ring_fini(umsch) \
	((umsch)->funcs->ring_fini ? (umsch)->funcs->ring_fini((umsch)) : 0)

static inline void amdgpu_umsch_mm_lock(struct amdgpu_umsch_mm *umsch)
{
	mutex_lock(&umsch->mutex_hidden);
}

static inline void amdgpu_umsch_mm_unlock(struct amdgpu_umsch_mm *umsch)
{
	mutex_unlock(&umsch->mutex_hidden);
}

extern const struct amdgpu_ip_block_version umsch_mm_v4_0_ip_block;

#endif

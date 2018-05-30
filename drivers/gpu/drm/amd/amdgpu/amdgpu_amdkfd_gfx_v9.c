/*
 * Copyright 2014-2018 Advanced Micro Devices, Inc.
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

#define pr_fmt(fmt) "kfd2kgd: " fmt

#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_ucode.h"
#include "soc15_hw_ip.h"
#include "gc/gc_9_0_offset.h"
#include "gc/gc_9_0_sh_mask.h"
#include "vega10_enum.h"
#include "sdma0/sdma0_4_0_offset.h"
#include "sdma0/sdma0_4_0_sh_mask.h"
#include "sdma1/sdma1_4_0_offset.h"
#include "sdma1/sdma1_4_0_sh_mask.h"
#include "athub/athub_1_0_offset.h"
#include "athub/athub_1_0_sh_mask.h"
#include "oss/osssys_4_0_offset.h"
#include "oss/osssys_4_0_sh_mask.h"
#include "soc15_common.h"
#include "v9_structs.h"
#include "soc15.h"
#include "soc15d.h"

/* HACK: MMHUB and GC both have VM-related register with the same
 * names but different offsets. Define the MMHUB register we need here
 * with a prefix. A proper solution would be to move the functions
 * programming these registers into gfx_v9_0.c and mmhub_v1_0.c
 * respectively.
 */
#define mmMMHUB_VM_INVALIDATE_ENG16_REQ				0x06f3
#define mmMMHUB_VM_INVALIDATE_ENG16_REQ_BASE_IDX		0

#define mmMMHUB_VM_INVALIDATE_ENG16_ACK				0x0705
#define mmMMHUB_VM_INVALIDATE_ENG16_ACK_BASE_IDX		0

#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32		0x072b
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32_BASE_IDX	0
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32		0x072c
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32_BASE_IDX	0

#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32		0x074b
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32_BASE_IDX	0
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32		0x074c
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32_BASE_IDX	0

#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32		0x076b
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32_BASE_IDX	0
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32		0x076c
#define mmMMHUB_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32_BASE_IDX	0

#define mmMMHUB_VM_INVALIDATE_ENG16_ADDR_RANGE_LO32		0x0727
#define mmMMHUB_VM_INVALIDATE_ENG16_ADDR_RANGE_LO32_BASE_IDX	0
#define mmMMHUB_VM_INVALIDATE_ENG16_ADDR_RANGE_HI32		0x0728
#define mmMMHUB_VM_INVALIDATE_ENG16_ADDR_RANGE_HI32_BASE_IDX	0

#define V9_PIPE_PER_MEC		(4)
#define V9_QUEUES_PER_PIPE_MEC	(8)

enum hqd_dequeue_request_type {
	NO_ACTION = 0,
	DRAIN_PIPE,
	RESET_WAVES
};

/*
 * Register access functions
 */

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t sh_mem_config,
		uint32_t sh_mem_ape1_base, uint32_t sh_mem_ape1_limit,
		uint32_t sh_mem_bases);
static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
		unsigned int vmid);
static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id);
static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm);
static int kgd_hqd_dump(struct kgd_dev *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs);
static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm);
static int kgd_hqd_sdma_dump(struct kgd_dev *kgd,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs);
static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
		uint32_t pipe_id, uint32_t queue_id);
static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd);
static int kgd_hqd_destroy(struct kgd_dev *kgd, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id);
static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int utimeout);
static int kgd_address_watch_disable(struct kgd_dev *kgd);
static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo);
static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset);

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd,
		uint8_t vmid);
static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
		uint8_t vmid);
static void set_vm_context_page_table_base(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t page_table_base);
static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type);
static void set_scratch_backing_va(struct kgd_dev *kgd,
					uint64_t va, uint32_t vmid);
static int invalidate_tlbs(struct kgd_dev *kgd, uint16_t pasid);
static int invalidate_tlbs_vmid(struct kgd_dev *kgd, uint16_t vmid);

/* Because of REG_GET_FIELD() being used, we put this function in the
 * asic specific file.
 */
static int amdgpu_amdkfd_get_tile_config(struct kgd_dev *kgd,
		struct tile_config *config)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;

	config->gb_addr_config = adev->gfx.config.gb_addr_config;

	config->tile_config_ptr = adev->gfx.config.tile_mode_array;
	config->num_tile_configs =
			ARRAY_SIZE(adev->gfx.config.tile_mode_array);
	config->macro_tile_config_ptr =
			adev->gfx.config.macrotile_mode_array;
	config->num_macro_tile_configs =
			ARRAY_SIZE(adev->gfx.config.macrotile_mode_array);

	return 0;
}

static const struct kfd2kgd_calls kfd2kgd = {
	.init_gtt_mem_allocation = alloc_gtt_mem,
	.free_gtt_mem = free_gtt_mem,
	.get_local_mem_info = get_local_mem_info,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.alloc_pasid = amdgpu_pasid_alloc,
	.free_pasid = amdgpu_pasid_free,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_dump = kgd_hqd_dump,
	.hqd_sdma_dump = kgd_hqd_sdma_dump,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.address_watch_disable = kgd_address_watch_disable,
	.address_watch_execute = kgd_address_watch_execute,
	.wave_control_execute = kgd_wave_control_execute,
	.address_watch_get_offset = kgd_address_watch_get_offset,
	.get_atc_vmid_pasid_mapping_pasid =
			get_atc_vmid_pasid_mapping_pasid,
	.get_atc_vmid_pasid_mapping_valid =
			get_atc_vmid_pasid_mapping_valid,
	.get_fw_version = get_fw_version,
	.set_scratch_backing_va = set_scratch_backing_va,
	.get_tile_config = amdgpu_amdkfd_get_tile_config,
	.get_cu_info = get_cu_info,
	.get_vram_usage = amdgpu_amdkfd_get_vram_usage,
	.create_process_vm = amdgpu_amdkfd_gpuvm_create_process_vm,
	.acquire_process_vm = amdgpu_amdkfd_gpuvm_acquire_process_vm,
	.destroy_process_vm = amdgpu_amdkfd_gpuvm_destroy_process_vm,
	.get_process_page_dir = amdgpu_amdkfd_gpuvm_get_process_page_dir,
	.set_vm_context_page_table_base = set_vm_context_page_table_base,
	.alloc_memory_of_gpu = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu,
	.free_memory_of_gpu = amdgpu_amdkfd_gpuvm_free_memory_of_gpu,
	.map_memory_to_gpu = amdgpu_amdkfd_gpuvm_map_memory_to_gpu,
	.unmap_memory_to_gpu = amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu,
	.sync_memory = amdgpu_amdkfd_gpuvm_sync_memory,
	.map_gtt_bo_to_kernel = amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel,
	.restore_process_bos = amdgpu_amdkfd_gpuvm_restore_process_bos,
	.invalidate_tlbs = invalidate_tlbs,
	.invalidate_tlbs_vmid = invalidate_tlbs_vmid,
	.submit_ib = amdgpu_amdkfd_submit_ib,
};

struct kfd2kgd_calls *amdgpu_amdkfd_gfx_9_0_get_functions(void)
{
	return (struct kfd2kgd_calls *)&kfd2kgd;
}

static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

static void lock_srbm(struct kgd_dev *kgd, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	mutex_lock(&adev->srbm_mutex);
	soc15_grbm_select(adev, mec, pipe, queue, vmid);
}

static void unlock_srbm(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	soc15_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t queue_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	uint32_t mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	uint32_t pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(kgd, mec, pipe, queue_id, 0);
}

static uint32_t get_queue_mask(struct amdgpu_device *adev,
			       uint32_t pipe_id, uint32_t queue_id)
{
	unsigned int bit = (pipe_id * adev->gfx.mec.num_queue_per_pipe +
			    queue_id) & 31;

	return ((uint32_t)1) << bit;
}

static void release_queue(struct kgd_dev *kgd)
{
	unlock_srbm(kgd);
}

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
					uint32_t sh_mem_config,
					uint32_t sh_mem_ape1_base,
					uint32_t sh_mem_ape1_limit,
					uint32_t sh_mem_bases)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	lock_srbm(kgd, 0, 0, 0, vmid);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmSH_MEM_CONFIG), sh_mem_config);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSH_MEM_BASES), sh_mem_bases);
	/* APE1 no longer exists on GFX9 */

	unlock_srbm(kgd);
}

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because
	 * a mapping is in progress or because a mapping finished
	 * and the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
			ATC_VMID0_PASID_MAPPING__VALID_MASK;

	/*
	 * need to do this twice, once for gfx and once for mmhub
	 * for ATC add 16 to VMID for mmhub, for IH different registers.
	 * ATC_VMID0..15 registers are separate from ATC_VMID16..31.
	 */

	WREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING) + vmid,
	       pasid_mapping);

	while (!(RREG32(SOC15_REG_OFFSET(
				ATHUB, 0,
				mmATC_VMID_PASID_MAPPING_UPDATE_STATUS)) &
		 (1U << vmid)))
		cpu_relax();

	WREG32(SOC15_REG_OFFSET(ATHUB, 0,
				mmATC_VMID_PASID_MAPPING_UPDATE_STATUS),
	       1U << vmid);

	/* Mapping vmid to pasid also for IH block */
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT) + vmid,
	       pasid_mapping);

	WREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID16_PASID_MAPPING) + vmid,
	       pasid_mapping);

	while (!(RREG32(SOC15_REG_OFFSET(
				ATHUB, 0,
				mmATC_VMID_PASID_MAPPING_UPDATE_STATUS)) &
		 (1U << (vmid + 16))))
		cpu_relax();

	WREG32(SOC15_REG_OFFSET(ATHUB, 0,
				mmATC_VMID_PASID_MAPPING_UPDATE_STATUS),
	       1U << (vmid + 16));

	/* Mapping vmid to pasid also for IH block */
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT_MM) + vmid,
	       pasid_mapping);
	return 0;
}

/* TODO - RING0 form of field is obsolete, seems to date back to SI
 * but still works
 */

static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(kgd, mec, pipe, 0, 0);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmCPC_INT_CNTL),
		CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
		CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(kgd);

	return 0;
}

static uint32_t get_sdma_base_addr(struct amdgpu_device *adev,
				unsigned int engine_id,
				unsigned int queue_id)
{
	uint32_t base[2] = {
		SOC15_REG_OFFSET(SDMA0, 0,
				 mmSDMA0_RLC0_RB_CNTL) - mmSDMA0_RLC0_RB_CNTL,
		SOC15_REG_OFFSET(SDMA1, 0,
				 mmSDMA1_RLC0_RB_CNTL) - mmSDMA1_RLC0_RB_CNTL
	};
	uint32_t retval;

	retval = base[engine_id] + queue_id * (mmSDMA0_RLC1_RB_CNTL -
					       mmSDMA0_RLC0_RB_CNTL);

	pr_debug("sdma base address: 0x%x\n", retval);

	return retval;
}

static inline struct v9_mqd *get_mqd(void *mqd)
{
	return (struct v9_mqd *)mqd;
}

static inline struct v9_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v9_sdma_mqd *)mqd;
}

static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct v9_mqd *m;
	uint32_t *mqd_hqd;
	uint32_t reg, hqd_base, data;

	m = get_mqd(mqd);

	acquire_queue(kgd, pipe_id, queue_id);

	/* HIQ is set during driver init period with vmid set to 0*/
	if (m->cp_hqd_vmid == 0) {
		uint32_t value, mec, pipe;

		mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
		pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

		pr_debug("kfd: set HIQ, mec:%d, pipe:%d, queue:%d.\n",
			mec, pipe, queue_id);
		value = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_CP_SCHEDULERS));
		value = REG_SET_FIELD(value, RLC_CP_SCHEDULERS, scheduler1,
			((mec << 5) | (pipe << 3) | queue_id | 0x80));
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_CP_SCHEDULERS), value);
	}

	/* HQD registers extend from CP_MQD_BASE_ADDR to CP_HQD_EOP_WPTR_MEM. */
	mqd_hqd = &m->cp_mqd_base_addr_lo;
	hqd_base = SOC15_REG_OFFSET(GC, 0, mmCP_MQD_BASE_ADDR);

	for (reg = hqd_base;
	     reg <= SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_HI); reg++)
		WREG32(reg, mqd_hqd[reg - hqd_base]);


	/* Activate doorbell logic before triggering WPTR poll. */
	data = REG_SET_FIELD(m->cp_hqd_pq_doorbell_control,
			     CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL), data);

	if (wptr) {
		/* Don't read wptr with get_user because the user
		 * context may not be accessible (if this function
		 * runs in a work queue). Instead trigger a one-shot
		 * polling read from memory in the CP. This assumes
		 * that wptr is GPU-accessible in the queue's VMID via
		 * ATC or SVM. WPTR==RPTR before starting the poll so
		 * the CP starts fetching new commands from the right
		 * place.
		 *
		 * Guessing a 64-bit WPTR from a 32-bit RPTR is a bit
		 * tricky. Assume that the queue didn't overflow. The
		 * number of valid bits in the 32-bit RPTR depends on
		 * the queue size. The remaining bits are taken from
		 * the saved 64-bit WPTR. If the WPTR wrapped, add the
		 * queue size.
		 */
		uint32_t queue_size =
			2 << REG_GET_FIELD(m->cp_hqd_pq_control,
					   CP_HQD_PQ_CONTROL, QUEUE_SIZE);
		uint64_t guessed_wptr = m->cp_hqd_pq_rptr & (queue_size - 1);

		if ((m->cp_hqd_pq_wptr_lo & (queue_size - 1)) < guessed_wptr)
			guessed_wptr += queue_size;
		guessed_wptr += m->cp_hqd_pq_wptr_lo & ~(queue_size - 1);
		guessed_wptr += (uint64_t)m->cp_hqd_pq_wptr_hi << 32;

		WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_LO),
		       lower_32_bits(guessed_wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_HI),
		       upper_32_bits(guessed_wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR),
		       lower_32_bits((uintptr_t)wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR_HI),
		       upper_32_bits((uintptr_t)wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_PQ_WPTR_POLL_CNTL1),
		       get_queue_mask(adev, pipe_id, queue_id));
	}

	/* Start the EOP fetcher */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_EOP_RPTR),
	       REG_SET_FIELD(m->cp_hqd_eop_rptr,
			     CP_HQD_EOP_RPTR, INIT_FETCHER, 1));

	data = REG_SET_FIELD(m->cp_hqd_active, CP_HQD_ACTIVE, ACTIVE, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_ACTIVE), data);

	release_queue(kgd);

	return 0;
}

static int kgd_hqd_dump(struct kgd_dev *kgd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t i = 0, reg;
#define HQD_N_REGS 56
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))	\
			break;				\
		(*dump)[i][0] = (addr) << 2;		\
		(*dump)[i++][1] = RREG32(addr);		\
	} while (0)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	acquire_queue(kgd, pipe_id, queue_id);

	for (reg = SOC15_REG_OFFSET(GC, 0, mmCP_MQD_BASE_ADDR);
	     reg <= SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_HI); reg++)
		DUMP_REG(reg);

	release_queue(kgd);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct v9_sdma_mqd *m;
	uint32_t sdma_base_addr, sdmax_gfx_context_cntl;
	unsigned long end_jiffies;
	uint32_t data;
	uint64_t data64;
	uint64_t __user *wptr64 = (uint64_t __user *)wptr;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(adev, m->sdma_engine_id,
					    m->sdma_queue_id);
	sdmax_gfx_context_cntl = m->sdma_engine_id ?
		SOC15_REG_OFFSET(SDMA1, 0, mmSDMA1_GFX_CONTEXT_CNTL) :
		SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_GFX_CONTEXT_CNTL);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
		m->sdmax_rlcx_rb_cntl & (~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK));

	end_jiffies = msecs_to_jiffies(2000) + jiffies;
	while (true) {
		data = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (data & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies))
			return -ETIME;
		usleep_range(500, 1000);
	}
	data = RREG32(sdmax_gfx_context_cntl);
	data = REG_SET_FIELD(data, SDMA0_GFX_CONTEXT_CNTL,
			     RESUME_CTX, 0);
	WREG32(sdmax_gfx_context_cntl, data);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL_OFFSET,
	       m->sdmax_rlcx_doorbell_offset);

	data = REG_SET_FIELD(m->sdmax_rlcx_doorbell, SDMA0_RLC0_DOORBELL,
			     ENABLE, 1);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, data);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR, m->sdmax_rlcx_rb_rptr);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_HI,
				m->sdmax_rlcx_rb_rptr_hi);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_MINOR_PTR_UPDATE, 1);
	if (read_user_wptr(mm, wptr64, data64)) {
		WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR,
		       lower_32_bits(data64));
		WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR_HI,
		       upper_32_bits(data64));
	} else {
		WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR,
		       m->sdmax_rlcx_rb_rptr);
		WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR_HI,
		       m->sdmax_rlcx_rb_rptr_hi);
	}
	WREG32(sdma_base_addr + mmSDMA0_RLC0_MINOR_PTR_UPDATE, 0);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE, m->sdmax_rlcx_rb_base);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE_HI,
			m->sdmax_rlcx_rb_base_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdmax_rlcx_rb_rptr_addr_lo);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdmax_rlcx_rb_rptr_addr_hi);

	data = REG_SET_FIELD(m->sdmax_rlcx_rb_cntl, SDMA0_RLC0_RB_CNTL,
			     RB_ENABLE, 1);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, data);

	return 0;
}

static int kgd_hqd_sdma_dump(struct kgd_dev *kgd,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t sdma_base_addr = get_sdma_base_addr(adev, engine_id, queue_id);
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (19+6+7+10)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = mmSDMA0_RLC0_RB_CNTL; reg <= mmSDMA0_RLC0_DOORBELL; reg++)
		DUMP_REG(sdma_base_addr + reg);
	for (reg = mmSDMA0_RLC0_STATUS; reg <= mmSDMA0_RLC0_CSA_ADDR_HI; reg++)
		DUMP_REG(sdma_base_addr + reg);
	for (reg = mmSDMA0_RLC0_IB_SUB_REMAIN;
	     reg <= mmSDMA0_RLC0_MINOR_PTR_UPDATE; reg++)
		DUMP_REG(sdma_base_addr + reg);
	for (reg = mmSDMA0_RLC0_MIDCMD_DATA0;
	     reg <= mmSDMA0_RLC0_MIDCMD_CNTL; reg++)
		DUMP_REG(sdma_base_addr + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t act;
	bool retval = false;
	uint32_t low, high;

	acquire_queue(kgd, pipe_id, queue_id);
	act = RREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_ACTIVE));
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == RREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_BASE)) &&
		   high == RREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_BASE_HI)))
			retval = true;
	}
	release_queue(kgd);
	return retval;
}

static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct v9_sdma_mqd *m;
	uint32_t sdma_base_addr;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	sdma_rlc_rb_cntl = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

static int kgd_hqd_destroy(struct kgd_dev *kgd, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	enum hqd_dequeue_request_type type;
	unsigned long end_jiffies;
	uint32_t temp;
	struct v9_mqd *m = get_mqd(mqd);

	acquire_queue(kgd, pipe_id, queue_id);

	if (m->cp_hqd_vmid == 0)
		WREG32_FIELD15(GC, 0, RLC_CP_SCHEDULERS, scheduler1, 0);

	switch (reset_type) {
	case KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN:
		type = DRAIN_PIPE;
		break;
	case KFD_PREEMPT_TYPE_WAVEFRONT_RESET:
		type = RESET_WAVES;
		break;
	default:
		type = DRAIN_PIPE;
		break;
	}

	WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_DEQUEUE_REQUEST), type);

	end_jiffies = (utimeout * HZ / 1000) + jiffies;
	while (true) {
		temp = RREG32(SOC15_REG_OFFSET(GC, 0, mmCP_HQD_ACTIVE));
		if (!(temp & CP_HQD_ACTIVE__ACTIVE_MASK))
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("cp queue preemption time out.\n");
			release_queue(kgd);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	release_queue(kgd);
	return 0;
}

static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int utimeout)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct v9_sdma_mqd *m;
	uint32_t sdma_base_addr;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies))
			return -ETIME;
		usleep_range(500, 1000);
	}

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
		RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL) |
		SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK);

	m->sdmax_rlcx_rb_rptr = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR);
	m->sdmax_rlcx_rb_rptr_hi =
		RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_HI);

	return 0;
}

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd,
							uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING)
		     + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__VALID_MASK;
}

static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
								uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING)
		     + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__PASID_MASK;
}

static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	uint32_t req = (1 << vmid) |
		(0 << VM_INVALIDATE_ENG16_REQ__FLUSH_TYPE__SHIFT) | /* legacy */
		VM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PTES_MASK |
		VM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE0_MASK |
		VM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE1_MASK |
		VM_INVALIDATE_ENG16_REQ__INVALIDATE_L2_PDE2_MASK |
		VM_INVALIDATE_ENG16_REQ__INVALIDATE_L1_PTES_MASK;

	mutex_lock(&adev->srbm_mutex);

	/* Use legacy mode tlb invalidation.
	 *
	 * Currently on Raven the code below is broken for anything but
	 * legacy mode due to a MMHUB power gating problem. A workaround
	 * is for MMHUB to wait until the condition PER_VMID_INVALIDATE_REQ
	 * == PER_VMID_INVALIDATE_ACK instead of simply waiting for the ack
	 * bit.
	 *
	 * TODO 1: agree on the right set of invalidation registers for
	 * KFD use. Use the last one for now. Invalidate both GC and
	 * MMHUB.
	 *
	 * TODO 2: support range-based invalidation, requires kfg2kgd
	 * interface change
	 */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_INVALIDATE_ENG16_ADDR_RANGE_LO32),
				0xffffffff);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_INVALIDATE_ENG16_ADDR_RANGE_HI32),
				0x0000001f);

	WREG32(SOC15_REG_OFFSET(MMHUB, 0,
				mmMMHUB_VM_INVALIDATE_ENG16_ADDR_RANGE_LO32),
				0xffffffff);
	WREG32(SOC15_REG_OFFSET(MMHUB, 0,
				mmMMHUB_VM_INVALIDATE_ENG16_ADDR_RANGE_HI32),
				0x0000001f);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_INVALIDATE_ENG16_REQ), req);

	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_INVALIDATE_ENG16_REQ),
				req);

	while (!(RREG32(SOC15_REG_OFFSET(GC, 0, mmVM_INVALIDATE_ENG16_ACK)) &
					(1 << vmid)))
		cpu_relax();

	while (!(RREG32(SOC15_REG_OFFSET(MMHUB, 0,
					mmMMHUB_VM_INVALIDATE_ENG16_ACK)) &
					(1 << vmid)))
		cpu_relax();

	mutex_unlock(&adev->srbm_mutex);

}

static int invalidate_tlbs_with_kiq(struct amdgpu_device *adev, uint16_t pasid)
{
	signed long r;
	uint32_t seq;
	struct amdgpu_ring *ring = &adev->gfx.kiq.ring;

	spin_lock(&adev->gfx.kiq.ring_lock);
	amdgpu_ring_alloc(ring, 12); /* fence + invalidate_tlbs package*/
	amdgpu_ring_write(ring, PACKET3(PACKET3_INVALIDATE_TLBS, 0));
	amdgpu_ring_write(ring,
			PACKET3_INVALIDATE_TLBS_DST_SEL(1) |
			PACKET3_INVALIDATE_TLBS_ALL_HUB(1) |
			PACKET3_INVALIDATE_TLBS_PASID(pasid) |
			PACKET3_INVALIDATE_TLBS_FLUSH_TYPE(0)); /* legacy */
	amdgpu_fence_emit_polling(ring, &seq);
	amdgpu_ring_commit(ring);
	spin_unlock(&adev->gfx.kiq.ring_lock);

	r = amdgpu_fence_wait_polling(ring, seq, adev->usec_timeout);
	if (r < 1) {
		DRM_ERROR("wait for kiq fence error: %ld.\n", r);
		return -ETIME;
	}

	return 0;
}

static int invalidate_tlbs(struct kgd_dev *kgd, uint16_t pasid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	int vmid;
	struct amdgpu_ring *ring = &adev->gfx.kiq.ring;

	if (ring->ready)
		return invalidate_tlbs_with_kiq(adev, pasid);

	for (vmid = 0; vmid < 16; vmid++) {
		if (!amdgpu_amdkfd_is_kfd_vmid(adev, vmid))
			continue;
		if (get_atc_vmid_pasid_mapping_valid(kgd, vmid)) {
			if (get_atc_vmid_pasid_mapping_pasid(kgd, vmid)
				== pasid) {
				write_vmid_invalidate_request(kgd, vmid);
				break;
			}
		}
	}

	return 0;
}

static int invalidate_tlbs_vmid(struct kgd_dev *kgd, uint16_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	if (!amdgpu_amdkfd_is_kfd_vmid(adev, vmid)) {
		pr_err("non kfd vmid %d\n", vmid);
		return 0;
	}

	write_vmid_invalidate_request(kgd, vmid);
	return 0;
}

static int kgd_address_watch_disable(struct kgd_dev *kgd)
{
	return 0;
}

static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo)
{
	return 0;
}

static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data = 0;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmGRBM_GFX_INDEX), gfx_index_val);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_CMD), sq_cmd);

	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		INSTANCE_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SH_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SE_BROADCAST_WRITES, 1);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmGRBM_GFX_INDEX), data);
	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset)
{
	return 0;
}

static void set_scratch_backing_va(struct kgd_dev *kgd,
					uint64_t va, uint32_t vmid)
{
	/* No longer needed on GFXv9. The scratch base address is
	 * passed to the shader by the CP. It's the user mode driver's
	 * responsibility.
	 */
}

/* FIXME: Does this need to be ASIC-specific code? */
static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	const union amdgpu_firmware_header *hdr;

	switch (type) {
	case KGD_ENGINE_PFP:
		hdr = (const union amdgpu_firmware_header *)adev->gfx.pfp_fw->data;
		break;

	case KGD_ENGINE_ME:
		hdr = (const union amdgpu_firmware_header *)adev->gfx.me_fw->data;
		break;

	case KGD_ENGINE_CE:
		hdr = (const union amdgpu_firmware_header *)adev->gfx.ce_fw->data;
		break;

	case KGD_ENGINE_MEC1:
		hdr = (const union amdgpu_firmware_header *)adev->gfx.mec_fw->data;
		break;

	case KGD_ENGINE_MEC2:
		hdr = (const union amdgpu_firmware_header *)adev->gfx.mec2_fw->data;
		break;

	case KGD_ENGINE_RLC:
		hdr = (const union amdgpu_firmware_header *)adev->gfx.rlc_fw->data;
		break;

	case KGD_ENGINE_SDMA1:
		hdr = (const union amdgpu_firmware_header *)adev->sdma.instance[0].fw->data;
		break;

	case KGD_ENGINE_SDMA2:
		hdr = (const union amdgpu_firmware_header *)adev->sdma.instance[1].fw->data;
		break;

	default:
		return 0;
	}

	if (hdr == NULL)
		return 0;

	/* Only 12 bit in use*/
	return hdr->common.ucode_version;
}

static void set_vm_context_page_table_base(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t page_table_base)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint64_t base = (uint64_t)page_table_base << PAGE_SHIFT |
		AMDGPU_PTE_VALID;

	if (!amdgpu_amdkfd_is_kfd_vmid(adev, vmid)) {
		pr_err("trying to set page table base for wrong VMID %u\n",
		       vmid);
		return;
	}

	/* TODO: take advantage of per-process address space size. For
	 * now, all processes share the same address space size, like
	 * on GFX8 and older.
	 */
	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32) + (vmid*2), 0);
	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32) + (vmid*2), 0);

	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32) + (vmid*2),
			lower_32_bits(adev->vm_manager.max_pfn - 1));
	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32) + (vmid*2),
			upper_32_bits(adev->vm_manager.max_pfn - 1));

	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32) + (vmid*2), lower_32_bits(base));
	WREG32(SOC15_REG_OFFSET(MMHUB, 0, mmMMHUB_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32) + (vmid*2), upper_32_bits(base));

	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32) + (vmid*2), 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32) + (vmid*2), 0);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32) + (vmid*2),
			lower_32_bits(adev->vm_manager.max_pfn - 1));
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32) + (vmid*2),
			upper_32_bits(adev->vm_manager.max_pfn - 1));

	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32) + (vmid*2), lower_32_bits(base));
	WREG32(SOC15_REG_OFFSET(GC, 0, mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32) + (vmid*2), upper_32_bits(base));
}

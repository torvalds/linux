/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "cikd.h"
#include "cik_sdma.h"
#include "amdgpu_ucode.h"
#include "gfx_v7_0.h"
#include "gca/gfx_7_2_d.h"
#include "gca/gfx_7_2_enum.h"
#include "gca/gfx_7_2_sh_mask.h"
#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"
#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"
#include "cik_structs.h"

enum hqd_dequeue_request_type {
	NO_ACTION = 0,
	DRAIN_PIPE,
	RESET_WAVES
};

enum {
	MAX_TRAPID = 8,		/* 3 bits in the bitfield. */
	MAX_WATCH_ADDRESSES = 4
};

enum {
	ADDRESS_WATCH_REG_ADDR_HI = 0,
	ADDRESS_WATCH_REG_ADDR_LO,
	ADDRESS_WATCH_REG_CNTL,
	ADDRESS_WATCH_REG_MAX
};

/*  not defined in the CI/KV reg file  */
enum {
	ADDRESS_WATCH_REG_CNTL_ATC_BIT = 0x10000000UL,
	ADDRESS_WATCH_REG_CNTL_DEFAULT_MASK = 0x00FFFFFF,
	ADDRESS_WATCH_REG_ADDLOW_MASK_EXTENSION = 0x03000000,
	/* extend the mask to 26 bits to match the low address field */
	ADDRESS_WATCH_REG_ADDLOW_SHIFT = 6,
	ADDRESS_WATCH_REG_ADDHIGH_MASK = 0xFFFF
};

static const uint32_t watchRegs[MAX_WATCH_ADDRESSES * ADDRESS_WATCH_REG_MAX] = {
	mmTCP_WATCH0_ADDR_H, mmTCP_WATCH0_ADDR_L, mmTCP_WATCH0_CNTL,
	mmTCP_WATCH1_ADDR_H, mmTCP_WATCH1_ADDR_L, mmTCP_WATCH1_CNTL,
	mmTCP_WATCH2_ADDR_H, mmTCP_WATCH2_ADDR_L, mmTCP_WATCH2_CNTL,
	mmTCP_WATCH3_ADDR_H, mmTCP_WATCH3_ADDR_L, mmTCP_WATCH3_CNTL
};

union TCP_WATCH_CNTL_BITS {
	struct {
		uint32_t mask:24;
		uint32_t vmid:4;
		uint32_t atc:1;
		uint32_t mode:2;
		uint32_t valid:1;
	} bitfields, bits;
	uint32_t u32All;
	signed int i32All;
	float f32All;
};

/*
 * Register access functions
 */

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t sh_mem_config,	uint32_t sh_mem_ape1_base,
		uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid);

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr);
static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id);
static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm);
static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd);
static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id);

static int kgd_hqd_destroy(struct kgd_dev *kgd, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id);
static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd);
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

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd, uint8_t vmid);
static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
							uint8_t vmid);
static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid);

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type);
static void set_scratch_backing_va(struct kgd_dev *kgd,
					uint64_t va, uint32_t vmid);

/* Because of REG_GET_FIELD() being used, we put this function in the
 * asic specific file.
 */
static int get_tile_config(struct kgd_dev *kgd,
		struct tile_config *config)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;

	config->gb_addr_config = adev->gfx.config.gb_addr_config;
	config->num_banks = REG_GET_FIELD(adev->gfx.config.mc_arb_ramcfg,
				MC_ARB_RAMCFG, NOOFBANK);
	config->num_ranks = REG_GET_FIELD(adev->gfx.config.mc_arb_ramcfg,
				MC_ARB_RAMCFG, NOOFRANKS);

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
	.get_vmem_size = get_vmem_size,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_pipeline = kgd_init_pipeline,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.address_watch_disable = kgd_address_watch_disable,
	.address_watch_execute = kgd_address_watch_execute,
	.wave_control_execute = kgd_wave_control_execute,
	.address_watch_get_offset = kgd_address_watch_get_offset,
	.get_atc_vmid_pasid_mapping_pasid = get_atc_vmid_pasid_mapping_pasid,
	.get_atc_vmid_pasid_mapping_valid = get_atc_vmid_pasid_mapping_valid,
	.write_vmid_invalidate_request = write_vmid_invalidate_request,
	.get_fw_version = get_fw_version,
	.set_scratch_backing_va = set_scratch_backing_va,
	.get_tile_config = get_tile_config,
};

struct kfd2kgd_calls *amdgpu_amdkfd_gfx_7_get_functions(void)
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
	uint32_t value = PIPEID(pipe) | MEID(mec) | VMID(vmid) | QUEUEID(queue);

	mutex_lock(&adev->srbm_mutex);
	WREG32(mmSRBM_GFX_CNTL, value);
}

static void unlock_srbm(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	WREG32(mmSRBM_GFX_CNTL, 0);
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

	WREG32(mmSH_MEM_CONFIG, sh_mem_config);
	WREG32(mmSH_MEM_APE1_BASE, sh_mem_ape1_base);
	WREG32(mmSH_MEM_APE1_LIMIT, sh_mem_ape1_limit);
	WREG32(mmSH_MEM_BASES, sh_mem_bases);

	unlock_srbm(kgd);
}

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);

	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because
	 * a mapping is in progress or because a mapping finished and the
	 * SW cleared it. So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
			ATC_VMID0_PASID_MAPPING__VALID_MASK;

	WREG32(mmATC_VMID0_PASID_MAPPING + vmid, pasid_mapping);

	while (!(RREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS) & (1U << vmid)))
		cpu_relax();
	WREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	/* Mapping vmid to pasid also for IH block */
	WREG32(mmIH_VMID_0_LUT + vmid, pasid_mapping);

	return 0;
}

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr)
{
	/* amdgpu owns the per-pipe state */
	return 0;
}

static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(kgd, mec, pipe, 0, 0);

	WREG32(mmCPC_INT_CNTL, CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
			CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(kgd);

	return 0;
}

static inline uint32_t get_sdma_base_addr(struct cik_sdma_rlc_registers *m)
{
	uint32_t retval;

	retval = m->sdma_engine_id * SDMA1_REGISTER_OFFSET +
			m->sdma_queue_id * KFD_CIK_SDMA_QUEUE_OFFSET;

	pr_debug("kfd: sdma base address: 0x%x\n", retval);

	return retval;
}

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
}

static inline struct cik_sdma_rlc_registers *get_sdma_mqd(void *mqd)
{
	return (struct cik_sdma_rlc_registers *)mqd;
}

static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct cik_mqd *m;
	uint32_t *mqd_hqd;
	uint32_t reg, wptr_val, data;

	m = get_mqd(mqd);

	acquire_queue(kgd, pipe_id, queue_id);

	/* HQD registers extend from CP_MQD_BASE_ADDR to CP_MQD_CONTROL. */
	mqd_hqd = &m->cp_mqd_base_addr_lo;

	for (reg = mmCP_MQD_BASE_ADDR; reg <= mmCP_MQD_CONTROL; reg++)
		WREG32(reg, mqd_hqd[reg - mmCP_MQD_BASE_ADDR]);

	/* Copy userspace write pointer value to register.
	 * Activate doorbell logic to monitor subsequent changes.
	 */
	data = REG_SET_FIELD(m->cp_hqd_pq_doorbell_control,
			     CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, data);

	if (read_user_wptr(mm, wptr, wptr_val))
		WREG32(mmCP_HQD_PQ_WPTR, (wptr_val << wptr_shift) & wptr_mask);

	data = REG_SET_FIELD(m->cp_hqd_active, CP_HQD_ACTIVE, ACTIVE, 1);
	WREG32(mmCP_HQD_ACTIVE, data);

	release_queue(kgd);

	return 0;
}

static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct cik_sdma_rlc_registers *m;
	unsigned long end_jiffies;
	uint32_t sdma_base_addr;
	uint32_t data;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
		m->sdma_rlc_rb_cntl & (~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK));

	end_jiffies = msecs_to_jiffies(2000) + jiffies;
	while (true) {
		data = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (data & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies))
			return -ETIME;
		usleep_range(500, 1000);
	}
	if (m->sdma_engine_id) {
		data = RREG32(mmSDMA1_GFX_CONTEXT_CNTL);
		data = REG_SET_FIELD(data, SDMA1_GFX_CONTEXT_CNTL,
				RESUME_CTX, 0);
		WREG32(mmSDMA1_GFX_CONTEXT_CNTL, data);
	} else {
		data = RREG32(mmSDMA0_GFX_CONTEXT_CNTL);
		data = REG_SET_FIELD(data, SDMA0_GFX_CONTEXT_CNTL,
				RESUME_CTX, 0);
		WREG32(mmSDMA0_GFX_CONTEXT_CNTL, data);
	}

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL,
				m->sdma_rlc_doorbell);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_WPTR, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_VIRTUAL_ADDR,
				m->sdma_rlc_virtual_addr);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE, m->sdma_rlc_rb_base);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_BASE_HI,
			m->sdma_rlc_rb_base_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdma_rlc_rb_rptr_addr_lo);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdma_rlc_rb_rptr_addr_hi);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
			m->sdma_rlc_rb_cntl);

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
	act = RREG32(mmCP_HQD_ACTIVE);
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == RREG32(mmCP_HQD_PQ_BASE) &&
				high == RREG32(mmCP_HQD_PQ_BASE_HI))
			retval = true;
	}
	release_queue(kgd);
	return retval;
}

static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

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
	uint32_t temp;
	enum hqd_dequeue_request_type type;
	unsigned long flags, end_jiffies;
	int retry;

	acquire_queue(kgd, pipe_id, queue_id);
	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, 0);

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

	/* Workaround: If IQ timer is active and the wait time is close to or
	 * equal to 0, dequeueing is not safe. Wait until either the wait time
	 * is larger or timer is cleared. Also, ensure that IQ_REQ_PEND is
	 * cleared before continuing. Also, ensure wait times are set to at
	 * least 0x3.
	 */
	local_irq_save(flags);
	preempt_disable();
	retry = 5000; /* wait for 500 usecs at maximum */
	while (true) {
		temp = RREG32(mmCP_HQD_IQ_TIMER);
		if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, PROCESSING_IQ)) {
			pr_debug("HW is processing IQ\n");
			goto loop;
		}
		if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, ACTIVE)) {
			if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, RETRY_TYPE)
					== 3) /* SEM-rearm is safe */
				break;
			/* Wait time 3 is safe for CP, but our MMIO read/write
			 * time is close to 1 microsecond, so check for 10 to
			 * leave more buffer room
			 */
			if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, WAIT_TIME)
					>= 10)
				break;
			pr_debug("IQ timer is active\n");
		} else
			break;
loop:
		if (!retry) {
			pr_err("CP HQD IQ timer status time out\n");
			break;
		}
		ndelay(100);
		--retry;
	}
	retry = 1000;
	while (true) {
		temp = RREG32(mmCP_HQD_DEQUEUE_REQUEST);
		if (!(temp & CP_HQD_DEQUEUE_REQUEST__IQ_REQ_PEND_MASK))
			break;
		pr_debug("Dequeue request is pending\n");

		if (!retry) {
			pr_err("CP HQD dequeue request time out\n");
			break;
		}
		ndelay(100);
		--retry;
	}
	local_irq_restore(flags);
	preempt_enable();

	WREG32(mmCP_HQD_DEQUEUE_REQUEST, type);

	end_jiffies = (utimeout * HZ / 1000) + jiffies;
	while (true) {
		temp = RREG32(mmCP_HQD_ACTIVE);
		if (!(temp & CP_HQD_ACTIVE__ACTIVE_MASK))
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("cp queue preemption time out\n");
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
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t temp;
	int timeout = utimeout;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_base_addr + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (timeout <= 0)
			return -ETIME;
		msleep(20);
		timeout -= 20;
	}

	WREG32(sdma_base_addr + mmSDMA0_RLC0_DOORBELL, 0);
	WREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL,
		RREG32(sdma_base_addr + mmSDMA0_RLC0_RB_CNTL) |
		SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK);

	return 0;
}

static int kgd_address_watch_disable(struct kgd_dev *kgd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	union TCP_WATCH_CNTL_BITS cntl;
	unsigned int i;

	cntl.u32All = 0;

	cntl.bitfields.valid = 0;
	cntl.bitfields.mask = ADDRESS_WATCH_REG_CNTL_DEFAULT_MASK;
	cntl.bitfields.atc = 1;

	/* Turning off this address until we set all the registers */
	for (i = 0; i < MAX_WATCH_ADDRESSES; i++)
		WREG32(watchRegs[i * ADDRESS_WATCH_REG_MAX +
			ADDRESS_WATCH_REG_CNTL], cntl.u32All);

	return 0;
}

static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	union TCP_WATCH_CNTL_BITS cntl;

	cntl.u32All = cntl_val;

	/* Turning off this watch point until we set all the registers */
	cntl.bitfields.valid = 0;
	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
		ADDRESS_WATCH_REG_CNTL], cntl.u32All);

	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
		ADDRESS_WATCH_REG_ADDR_HI], addr_hi);

	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
		ADDRESS_WATCH_REG_ADDR_LO], addr_lo);

	/* Enable the watch point */
	cntl.bitfields.valid = 1;

	WREG32(watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
		ADDRESS_WATCH_REG_CNTL], cntl.u32All);

	return 0;
}

static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	uint32_t data;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(mmGRBM_GFX_INDEX, gfx_index_val);
	WREG32(mmSQ_CMD, sq_cmd);

	/*  Restore the GRBM_GFX_INDEX register  */

	data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK |
		GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK |
		GRBM_GFX_INDEX__SE_BROADCAST_WRITES_MASK;

	WREG32(mmGRBM_GFX_INDEX, data);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset)
{
	return watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + reg_offset];
}

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd,
							uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__VALID_MASK;
}

static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
								uint8_t vmid)
{
	uint32_t reg;
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	reg = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	return reg & ATC_VMID0_PASID_MAPPING__VALID_MASK;
}

static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	WREG32(mmVM_INVALIDATE_REQUEST, 1 << vmid);
}

static void set_scratch_backing_va(struct kgd_dev *kgd,
					uint64_t va, uint32_t vmid)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;

	lock_srbm(kgd, 0, 0, 0, vmid);
	WREG32(mmSH_HIDDEN_PRIVATE_BASE_VMID, va);
	unlock_srbm(kgd);
}

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type)
{
	struct amdgpu_device *adev = (struct amdgpu_device *) kgd;
	const union amdgpu_firmware_header *hdr;

	BUG_ON(kgd == NULL);

	switch (type) {
	case KGD_ENGINE_PFP:
		hdr = (const union amdgpu_firmware_header *)
						adev->gfx.pfp_fw->data;
		break;

	case KGD_ENGINE_ME:
		hdr = (const union amdgpu_firmware_header *)
						adev->gfx.me_fw->data;
		break;

	case KGD_ENGINE_CE:
		hdr = (const union amdgpu_firmware_header *)
						adev->gfx.ce_fw->data;
		break;

	case KGD_ENGINE_MEC1:
		hdr = (const union amdgpu_firmware_header *)
						adev->gfx.mec_fw->data;
		break;

	case KGD_ENGINE_MEC2:
		hdr = (const union amdgpu_firmware_header *)
						adev->gfx.mec2_fw->data;
		break;

	case KGD_ENGINE_RLC:
		hdr = (const union amdgpu_firmware_header *)
						adev->gfx.rlc_fw->data;
		break;

	case KGD_ENGINE_SDMA1:
		hdr = (const union amdgpu_firmware_header *)
						adev->sdma.instance[0].fw->data;
		break;

	case KGD_ENGINE_SDMA2:
		hdr = (const union amdgpu_firmware_header *)
						adev->sdma.instance[1].fw->data;
		break;

	default:
		return 0;
	}

	if (hdr == NULL)
		return 0;

	/* Only 12 bit in use*/
	return hdr->common.ucode_version;
}


/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include <linux/mmu_context.h>
#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"
#include "oss/osssys_6_0_0_offset.h"
#include "oss/osssys_6_0_0_sh_mask.h"
#include "soc15_common.h"
#include "soc15d.h"
#include "v11_structs.h"
#include "soc21.h"
#include <uapi/linux/kfd_ioctl.h>

enum hqd_dequeue_request_type {
	NO_ACTION = 0,
	DRAIN_PIPE,
	RESET_WAVES,
	SAVE_WAVES
};

static void lock_srbm(struct amdgpu_device *adev, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	mutex_lock(&adev->srbm_mutex);
	soc21_grbm_select(adev, mec, pipe, queue, vmid);
}

static void unlock_srbm(struct amdgpu_device *adev)
{
	soc21_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	uint32_t pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, queue_id, 0);
}

static uint64_t get_queue_mask(struct amdgpu_device *adev,
			       uint32_t pipe_id, uint32_t queue_id)
{
	unsigned int bit = pipe_id * adev->gfx.mec.num_queue_per_pipe +
			queue_id;

	return 1ull << bit;
}

static void release_queue(struct amdgpu_device *adev)
{
	unlock_srbm(adev);
}

static void program_sh_mem_settings_v11(struct amdgpu_device *adev, uint32_t vmid,
					uint32_t sh_mem_config,
					uint32_t sh_mem_ape1_base,
					uint32_t sh_mem_ape1_limit,
					uint32_t sh_mem_bases, uint32_t inst)
{
	lock_srbm(adev, 0, 0, 0, vmid);

	WREG32(SOC15_REG_OFFSET(GC, 0, regSH_MEM_CONFIG), sh_mem_config);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSH_MEM_BASES), sh_mem_bases);

	unlock_srbm(adev);
}

static int set_pasid_vmid_mapping_v11(struct amdgpu_device *adev, unsigned int pasid,
					unsigned int vmid, uint32_t inst)
{
	uint32_t value = pasid << IH_VMID_0_LUT__PASID__SHIFT;

	/* Mapping vmid to pasid also for IH block */
	pr_debug("mapping vmid %d -> pasid %d in IH block for GFX client\n",
			vmid, pasid);
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT) + vmid, value);

	return 0;
}

static int init_interrupts_v11(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t inst)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, 0, 0);

	WREG32_SOC15(GC, 0, regCPC_INT_CNTL,
		CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
		CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(adev);

	return 0;
}

static uint32_t get_sdma_rlc_reg_offset(struct amdgpu_device *adev,
				unsigned int engine_id,
				unsigned int queue_id)
{
	uint32_t sdma_engine_reg_base = 0;
	uint32_t sdma_rlc_reg_offset;

	switch (engine_id) {
	case 0:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA0, 0,
				regSDMA0_QUEUE0_RB_CNTL) - regSDMA0_QUEUE0_RB_CNTL;
		break;
	case 1:
		sdma_engine_reg_base = SOC15_REG_OFFSET(SDMA1, 0,
				regSDMA1_QUEUE0_RB_CNTL) - regSDMA0_QUEUE0_RB_CNTL;
		break;
	default:
		BUG();
	}

	sdma_rlc_reg_offset = sdma_engine_reg_base
		+ queue_id * (regSDMA0_QUEUE1_RB_CNTL - regSDMA0_QUEUE0_RB_CNTL);

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n", engine_id,
			queue_id, sdma_rlc_reg_offset);

	return sdma_rlc_reg_offset;
}

static inline struct v11_compute_mqd *get_mqd(void *mqd)
{
	return (struct v11_compute_mqd *)mqd;
}

static inline struct v11_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v11_sdma_mqd *)mqd;
}

static int hqd_load_v11(struct amdgpu_device *adev, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm, uint32_t inst)
{
	struct v11_compute_mqd *m;
	uint32_t *mqd_hqd;
	uint32_t reg, hqd_base, data;

	m = get_mqd(mqd);

	pr_debug("Load hqd of pipe %d queue %d\n", pipe_id, queue_id);
	acquire_queue(adev, pipe_id, queue_id);

	/* HIQ is set during driver init period with vmid set to 0*/
	if (m->cp_hqd_vmid == 0) {
		uint32_t value, mec, pipe;

		mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
		pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

		pr_debug("kfd: set HIQ, mec:%d, pipe:%d, queue:%d.\n",
			mec, pipe, queue_id);
		value = RREG32(SOC15_REG_OFFSET(GC, 0, regRLC_CP_SCHEDULERS));
		value = REG_SET_FIELD(value, RLC_CP_SCHEDULERS, scheduler1,
			((mec << 5) | (pipe << 3) | queue_id | 0x80));
		WREG32(SOC15_REG_OFFSET(GC, 0, regRLC_CP_SCHEDULERS), value);
	}

	/* HQD registers extend from CP_MQD_BASE_ADDR to CP_HQD_EOP_WPTR_MEM. */
	mqd_hqd = &m->cp_mqd_base_addr_lo;
	hqd_base = SOC15_REG_OFFSET(GC, 0, regCP_MQD_BASE_ADDR);

	for (reg = hqd_base;
	     reg <= SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_HI); reg++)
		WREG32(reg, mqd_hqd[reg - hqd_base]);


	/* Activate doorbell logic before triggering WPTR poll. */
	data = REG_SET_FIELD(m->cp_hqd_pq_doorbell_control,
			     CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_DOORBELL_CONTROL), data);

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

		WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_LO),
		       lower_32_bits(guessed_wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_HI),
		       upper_32_bits(guessed_wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_POLL_ADDR),
		       lower_32_bits((uint64_t)wptr));
		WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_POLL_ADDR_HI),
		       upper_32_bits((uint64_t)wptr));
		pr_debug("%s setting CP_PQ_WPTR_POLL_CNTL1 to %x\n", __func__,
			 (uint32_t)get_queue_mask(adev, pipe_id, queue_id));
		WREG32(SOC15_REG_OFFSET(GC, 0, regCP_PQ_WPTR_POLL_CNTL1),
		       (uint32_t)get_queue_mask(adev, pipe_id, queue_id));
	}

	/* Start the EOP fetcher */
	WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_EOP_RPTR),
	       REG_SET_FIELD(m->cp_hqd_eop_rptr,
			     CP_HQD_EOP_RPTR, INIT_FETCHER, 1));

	data = REG_SET_FIELD(m->cp_hqd_active, CP_HQD_ACTIVE, ACTIVE, 1);
	WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_ACTIVE), data);

	release_queue(adev);

	return 0;
}

static int hiq_mqd_load_v11(struct amdgpu_device *adev, void *mqd,
			      uint32_t pipe_id, uint32_t queue_id,
			      uint32_t doorbell_off, uint32_t inst)
{
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq[0].ring;
	struct v11_compute_mqd *m;
	uint32_t mec, pipe;
	int r;

	m = get_mqd(mqd);

	acquire_queue(adev, pipe_id, queue_id);

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	pr_debug("kfd: set HIQ, mec:%d, pipe:%d, queue:%d.\n",
		 mec, pipe, queue_id);

	spin_lock(&adev->gfx.kiq[0].ring_lock);
	r = amdgpu_ring_alloc(kiq_ring, 7);
	if (r) {
		pr_err("Failed to alloc KIQ (%d).\n", r);
		goto out_unlock;
	}

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
	amdgpu_ring_write(kiq_ring,
			  PACKET3_MAP_QUEUES_QUEUE_SEL(0) | /* Queue_Sel */
			  PACKET3_MAP_QUEUES_VMID(m->cp_hqd_vmid) | /* VMID */
			  PACKET3_MAP_QUEUES_QUEUE(queue_id) |
			  PACKET3_MAP_QUEUES_PIPE(pipe) |
			  PACKET3_MAP_QUEUES_ME((mec - 1)) |
			  PACKET3_MAP_QUEUES_QUEUE_TYPE(0) | /*queue_type: normal compute queue */
			  PACKET3_MAP_QUEUES_ALLOC_FORMAT(0) | /* alloc format: all_on_one_pipe */
			  PACKET3_MAP_QUEUES_ENGINE_SEL(1) | /* engine_sel: hiq */
			  PACKET3_MAP_QUEUES_NUM_QUEUES(1)); /* num_queues: must be 1 */
	amdgpu_ring_write(kiq_ring,
			PACKET3_MAP_QUEUES_DOORBELL_OFFSET(doorbell_off));
	amdgpu_ring_write(kiq_ring, m->cp_mqd_base_addr_lo);
	amdgpu_ring_write(kiq_ring, m->cp_mqd_base_addr_hi);
	amdgpu_ring_write(kiq_ring, m->cp_hqd_pq_wptr_poll_addr_lo);
	amdgpu_ring_write(kiq_ring, m->cp_hqd_pq_wptr_poll_addr_hi);
	amdgpu_ring_commit(kiq_ring);

out_unlock:
	spin_unlock(&adev->gfx.kiq[0].ring_lock);
	release_queue(adev);

	return r;
}

static int hqd_dump_v11(struct amdgpu_device *adev,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs, uint32_t inst)
{
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

	acquire_queue(adev, pipe_id, queue_id);

	for (reg = SOC15_REG_OFFSET(GC, 0, regCP_MQD_BASE_ADDR);
	     reg <= SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_WPTR_HI); reg++)
		DUMP_REG(reg);

	release_queue(adev);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int hqd_sdma_load_v11(struct amdgpu_device *adev, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm)
{
	struct v11_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	unsigned long end_jiffies;
	uint32_t data;
	uint64_t data64;
	uint64_t __user *wptr64 = (uint64_t __user *)wptr;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL,
		m->sdmax_rlcx_rb_cntl & (~SDMA0_QUEUE0_RB_CNTL__RB_ENABLE_MASK));

	end_jiffies = msecs_to_jiffies(2000) + jiffies;
	while (true) {
		data = RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_CONTEXT_STATUS);
		if (data & SDMA0_QUEUE0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_DOORBELL_OFFSET,
	       m->sdmax_rlcx_doorbell_offset);

	data = REG_SET_FIELD(m->sdmax_rlcx_doorbell, SDMA0_QUEUE0_DOORBELL,
			     ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_DOORBELL, data);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_RPTR,
				m->sdmax_rlcx_rb_rptr);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_RPTR_HI,
				m->sdmax_rlcx_rb_rptr_hi);

	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_MINOR_PTR_UPDATE, 1);
	if (read_user_wptr(mm, wptr64, data64)) {
		WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_WPTR,
		       lower_32_bits(data64));
		WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_WPTR_HI,
		       upper_32_bits(data64));
	} else {
		WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_WPTR,
		       m->sdmax_rlcx_rb_rptr);
		WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_WPTR_HI,
		       m->sdmax_rlcx_rb_rptr_hi);
	}
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_MINOR_PTR_UPDATE, 0);

	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_BASE, m->sdmax_rlcx_rb_base);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_BASE_HI,
			m->sdmax_rlcx_rb_base_hi);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_RPTR_ADDR_LO,
			m->sdmax_rlcx_rb_rptr_addr_lo);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_RPTR_ADDR_HI,
			m->sdmax_rlcx_rb_rptr_addr_hi);

	data = REG_SET_FIELD(m->sdmax_rlcx_rb_cntl, SDMA0_QUEUE0_RB_CNTL,
			     RB_ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL, data);

	return 0;
}

static int hqd_sdma_dump_v11(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev,
			engine_id, queue_id);
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (7+11+1+12+12)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = regSDMA0_QUEUE0_RB_CNTL;
	     reg <= regSDMA0_QUEUE0_RB_WPTR_HI; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA0_QUEUE0_RB_RPTR_ADDR_HI;
	     reg <= regSDMA0_QUEUE0_DOORBELL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA0_QUEUE0_DOORBELL_LOG;
	     reg <= regSDMA0_QUEUE0_DOORBELL_LOG; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA0_QUEUE0_DOORBELL_OFFSET;
	     reg <= regSDMA0_QUEUE0_RB_PREEMPT; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = regSDMA0_QUEUE0_MIDCMD_DATA0;
	     reg <= regSDMA0_QUEUE0_MIDCMD_CNTL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static bool hqd_is_occupied_v11(struct amdgpu_device *adev, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id, uint32_t inst)
{
	uint32_t act;
	bool retval = false;
	uint32_t low, high;

	acquire_queue(adev, pipe_id, queue_id);
	act = RREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_ACTIVE));
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == RREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_BASE)) &&
		   high == RREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_PQ_BASE_HI)))
			retval = true;
	}
	release_queue(adev);
	return retval;
}

static bool hqd_sdma_is_occupied_v11(struct amdgpu_device *adev, void *mqd)
{
	struct v11_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	sdma_rlc_rb_cntl = RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA0_QUEUE0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

static int hqd_destroy_v11(struct amdgpu_device *adev, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id, uint32_t inst)
{
	enum hqd_dequeue_request_type type;
	unsigned long end_jiffies;
	uint32_t temp;
	struct v11_compute_mqd *m = get_mqd(mqd);

	acquire_queue(adev, pipe_id, queue_id);

	if (m->cp_hqd_vmid == 0)
		WREG32_FIELD15_PREREG(GC, 0, RLC_CP_SCHEDULERS, scheduler1, 0);

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

	WREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_DEQUEUE_REQUEST), type);

	end_jiffies = (utimeout * HZ / 1000) + jiffies;
	while (true) {
		temp = RREG32(SOC15_REG_OFFSET(GC, 0, regCP_HQD_ACTIVE));
		if (!(temp & CP_HQD_ACTIVE__ACTIVE_MASK))
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("cp queue pipe %d queue %d preemption failed\n",
					pipe_id, queue_id);
			release_queue(adev);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	release_queue(adev);
	return 0;
}

static int hqd_sdma_destroy_v11(struct amdgpu_device *adev, void *mqd,
				unsigned int utimeout)
{
	struct v11_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	temp = RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL);
	temp = temp & ~SDMA0_QUEUE0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_CONTEXT_STATUS);
		if (temp & SDMA0_QUEUE0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_DOORBELL, 0);
	WREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL,
		RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_CNTL) |
		SDMA0_QUEUE0_RB_CNTL__RB_ENABLE_MASK);

	m->sdmax_rlcx_rb_rptr = RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_RPTR);
	m->sdmax_rlcx_rb_rptr_hi =
		RREG32(sdma_rlc_reg_offset + regSDMA0_QUEUE0_RB_RPTR_HI);

	return 0;
}

static int wave_control_execute_v11(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd, uint32_t inst)
{
	uint32_t data = 0;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(SOC15_REG_OFFSET(GC, 0, regGRBM_GFX_INDEX), gfx_index_val);
	WREG32(SOC15_REG_OFFSET(GC, 0, regSQ_CMD), sq_cmd);

	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		INSTANCE_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SA_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SE_BROADCAST_WRITES, 1);

	WREG32(SOC15_REG_OFFSET(GC, 0, regGRBM_GFX_INDEX), data);
	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static void set_vm_context_page_table_base_v11(struct amdgpu_device *adev,
		uint32_t vmid, uint64_t page_table_base)
{
	if (!amdgpu_amdkfd_is_kfd_vmid(adev, vmid)) {
		pr_err("trying to set page table base for wrong VMID %u\n",
		       vmid);
		return;
	}

	/* SDMA is on gfxhub as well for gfx11 adapters */
	adev->gfxhub.funcs->setup_vm_pt_regs(adev, vmid, page_table_base);
}

/*
 * Returns TRAP_EN, EXCP_EN and EXCP_REPLACE.
 *
 * restore_dbg_registers is ignored here but is a general interface requirement
 * for devices that support GFXOFF and where the RLC save/restore list
 * does not support hw registers for debugging i.e. the driver has to manually
 * initialize the debug mode registers after it has disabled GFX off during the
 * debug session.
 */
static uint32_t kgd_gfx_v11_enable_debug_trap(struct amdgpu_device *adev,
					    bool restore_dbg_registers,
					    uint32_t vmid)
{
	uint32_t data = 0;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);

	return data;
}

/* Returns TRAP_EN, EXCP_EN and EXCP_REPLACE. */
static uint32_t kgd_gfx_v11_disable_debug_trap(struct amdgpu_device *adev,
						bool keep_trap_enabled,
						uint32_t vmid)
{
	uint32_t data = 0;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, 0);

	return data;
}

static int kgd_gfx_v11_validate_trap_override_request(struct amdgpu_device *adev,
							uint32_t trap_override,
							uint32_t *trap_mask_supported)
{
	*trap_mask_supported &= KFD_DBG_TRAP_MASK_FP_INVALID |
				KFD_DBG_TRAP_MASK_FP_INPUT_DENORMAL |
				KFD_DBG_TRAP_MASK_FP_DIVIDE_BY_ZERO |
				KFD_DBG_TRAP_MASK_FP_OVERFLOW |
				KFD_DBG_TRAP_MASK_FP_UNDERFLOW |
				KFD_DBG_TRAP_MASK_FP_INEXACT |
				KFD_DBG_TRAP_MASK_INT_DIVIDE_BY_ZERO |
				KFD_DBG_TRAP_MASK_DBG_ADDRESS_WATCH |
				KFD_DBG_TRAP_MASK_DBG_MEMORY_VIOLATION;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 4))
		*trap_mask_supported |= KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START |
					KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END;

	if (trap_override != KFD_DBG_TRAP_OVERRIDE_OR &&
			trap_override != KFD_DBG_TRAP_OVERRIDE_REPLACE)
		return -EPERM;

	return 0;
}

static uint32_t trap_mask_map_sw_to_hw(uint32_t mask)
{
	uint32_t trap_on_start = (mask & KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START) ? 1 : 0;
	uint32_t trap_on_end = (mask & KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END) ? 1 : 0;
	uint32_t excp_en = mask & (KFD_DBG_TRAP_MASK_FP_INVALID |
			KFD_DBG_TRAP_MASK_FP_INPUT_DENORMAL |
			KFD_DBG_TRAP_MASK_FP_DIVIDE_BY_ZERO |
			KFD_DBG_TRAP_MASK_FP_OVERFLOW |
			KFD_DBG_TRAP_MASK_FP_UNDERFLOW |
			KFD_DBG_TRAP_MASK_FP_INEXACT |
			KFD_DBG_TRAP_MASK_INT_DIVIDE_BY_ZERO |
			KFD_DBG_TRAP_MASK_DBG_ADDRESS_WATCH |
			KFD_DBG_TRAP_MASK_DBG_MEMORY_VIOLATION);
	uint32_t ret;

	ret = REG_SET_FIELD(0, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, excp_en);
	ret = REG_SET_FIELD(ret, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_START, trap_on_start);
	ret = REG_SET_FIELD(ret, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_END, trap_on_end);

	return ret;
}

static uint32_t trap_mask_map_hw_to_sw(uint32_t mask)
{
	uint32_t ret = REG_GET_FIELD(mask, SPI_GDBG_PER_VMID_CNTL, EXCP_EN);

	if (REG_GET_FIELD(mask, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_START))
		ret |= KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START;

	if (REG_GET_FIELD(mask, SPI_GDBG_PER_VMID_CNTL, TRAP_ON_END))
		ret |= KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END;

	return ret;
}

/* Returns TRAP_EN, EXCP_EN and EXCP_REPLACE. */
static uint32_t kgd_gfx_v11_set_wave_launch_trap_override(struct amdgpu_device *adev,
					uint32_t vmid,
					uint32_t trap_override,
					uint32_t trap_mask_bits,
					uint32_t trap_mask_request,
					uint32_t *trap_mask_prev,
					uint32_t kfd_dbg_trap_cntl_prev)
{
	uint32_t data = 0;

	*trap_mask_prev = trap_mask_map_hw_to_sw(kfd_dbg_trap_cntl_prev);

	data = (trap_mask_bits & trap_mask_request) | (*trap_mask_prev & ~trap_mask_request);
	data = trap_mask_map_sw_to_hw(data);

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE, trap_override);

	return data;
}

static uint32_t kgd_gfx_v11_set_wave_launch_mode(struct amdgpu_device *adev,
					uint8_t wave_launch_mode,
					uint32_t vmid)
{
	uint32_t data = 0;

	data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, LAUNCH_MODE, wave_launch_mode);

	return data;
}

#define TCP_WATCH_STRIDE (regTCP_WATCH1_ADDR_H - regTCP_WATCH0_ADDR_H)
static uint32_t kgd_gfx_v11_set_address_watch(struct amdgpu_device *adev,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t watch_id,
					uint32_t watch_mode,
					uint32_t debug_vmid,
					uint32_t inst)
{
	uint32_t watch_address_high;
	uint32_t watch_address_low;
	uint32_t watch_address_cntl;

	watch_address_cntl = 0;
	watch_address_low = lower_32_bits(watch_address);
	watch_address_high = upper_32_bits(watch_address) & 0xffff;

	watch_address_cntl = REG_SET_FIELD(watch_address_cntl,
			TCP_WATCH0_CNTL,
			MODE,
			watch_mode);

	watch_address_cntl = REG_SET_FIELD(watch_address_cntl,
			TCP_WATCH0_CNTL,
			MASK,
			watch_address_mask >> 7);

	watch_address_cntl = REG_SET_FIELD(watch_address_cntl,
			TCP_WATCH0_CNTL,
			VALID,
			1);

	WREG32_RLC((SOC15_REG_OFFSET(GC, 0, regTCP_WATCH0_ADDR_H) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_high);

	WREG32_RLC((SOC15_REG_OFFSET(GC, 0, regTCP_WATCH0_ADDR_L) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_low);

	return watch_address_cntl;
}

static uint32_t kgd_gfx_v11_clear_address_watch(struct amdgpu_device *adev,
						uint32_t watch_id)
{
	return 0;
}

static uint64_t kgd_gfx_v11_hqd_get_pq_addr(struct amdgpu_device *adev,
					    uint32_t pipe_id, uint32_t queue_id,
					    uint32_t inst)
{
	return 0;
}

static uint64_t kgd_gfx_v11_hqd_reset(struct amdgpu_device *adev,
				      uint32_t pipe_id, uint32_t queue_id,
				      uint32_t inst, unsigned int utimeout)
{
	return 0;
}

static uint32_t kgd_gfx_v11_hqd_sdma_get_doorbell(struct amdgpu_device *adev,
						  int engine, int queue)
{
	return 0;
}

const struct kfd2kgd_calls gfx_v11_kfd2kgd = {
	.program_sh_mem_settings = program_sh_mem_settings_v11,
	.set_pasid_vmid_mapping = set_pasid_vmid_mapping_v11,
	.init_interrupts = init_interrupts_v11,
	.hqd_load = hqd_load_v11,
	.hiq_mqd_load = hiq_mqd_load_v11,
	.hqd_sdma_load = hqd_sdma_load_v11,
	.hqd_dump = hqd_dump_v11,
	.hqd_sdma_dump = hqd_sdma_dump_v11,
	.hqd_is_occupied = hqd_is_occupied_v11,
	.hqd_sdma_is_occupied = hqd_sdma_is_occupied_v11,
	.hqd_destroy = hqd_destroy_v11,
	.hqd_sdma_destroy = hqd_sdma_destroy_v11,
	.wave_control_execute = wave_control_execute_v11,
	.get_atc_vmid_pasid_mapping_info = NULL,
	.set_vm_context_page_table_base = set_vm_context_page_table_base_v11,
	.enable_debug_trap = kgd_gfx_v11_enable_debug_trap,
	.disable_debug_trap = kgd_gfx_v11_disable_debug_trap,
	.validate_trap_override_request = kgd_gfx_v11_validate_trap_override_request,
	.set_wave_launch_trap_override = kgd_gfx_v11_set_wave_launch_trap_override,
	.set_wave_launch_mode = kgd_gfx_v11_set_wave_launch_mode,
	.set_address_watch = kgd_gfx_v11_set_address_watch,
	.clear_address_watch = kgd_gfx_v11_clear_address_watch,
	.hqd_get_pq_addr = kgd_gfx_v11_hqd_get_pq_addr,
	.hqd_reset = kgd_gfx_v11_hqd_reset,
	.hqd_sdma_get_doorbell = kgd_gfx_v11_hqd_sdma_get_doorbell
};

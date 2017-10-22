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
 *
 */

#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/mm_types.h>

#include "kfd_priv.h"
#include "kfd_mqd_manager.h"
#include "cik_regs.h"
#include "cik_structs.h"
#include "oss/oss_2_4_sh_mask.h"

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
}

static int init_mqd(struct mqd_manager *mm, void **mqd,
		struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
		struct queue_properties *q)
{
	uint64_t addr;
	struct cik_mqd *m;
	int retval;

	retval = kfd_gtt_sa_allocate(mm->dev, sizeof(struct cik_mqd),
					mqd_mem_obj);

	if (retval != 0)
		return -ENOMEM;

	m = (struct cik_mqd *) (*mqd_mem_obj)->cpu_ptr;
	addr = (*mqd_mem_obj)->gpu_addr;

	memset(m, 0, ALIGN(sizeof(struct cik_mqd), 256));

	m->header = 0xC0310800;
	m->compute_pipelinestat_enable = 1;
	m->compute_static_thread_mgmt_se0 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se1 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se2 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se3 = 0xFFFFFFFF;

	/*
	 * Make sure to use the last queue state saved on mqd when the cp
	 * reassigns the queue, so when queue is switched on/off (e.g over
	 * subscription or quantum timeout) the context will be consistent
	 */
	m->cp_hqd_persistent_state =
				DEFAULT_CP_HQD_PERSISTENT_STATE | PRELOAD_REQ;

	m->cp_mqd_control             = MQD_CONTROL_PRIV_STATE_EN;
	m->cp_mqd_base_addr_lo        = lower_32_bits(addr);
	m->cp_mqd_base_addr_hi        = upper_32_bits(addr);

	m->cp_hqd_ib_control = DEFAULT_MIN_IB_AVAIL_SIZE | IB_ATC_EN;
	/* Although WinKFD writes this, I suspect it should not be necessary */
	m->cp_hqd_ib_control = IB_ATC_EN | DEFAULT_MIN_IB_AVAIL_SIZE;

	m->cp_hqd_quantum = QUANTUM_EN | QUANTUM_SCALE_1MS |
				QUANTUM_DURATION(10);

	/*
	 * Pipe Priority
	 * Identifies the pipe relative priority when this queue is connected
	 * to the pipeline. The pipe priority is against the GFX pipe and HP3D.
	 * In KFD we are using a fixed pipe priority set to CS_MEDIUM.
	 * 0 = CS_LOW (typically below GFX)
	 * 1 = CS_MEDIUM (typically between HP3D and GFX
	 * 2 = CS_HIGH (typically above HP3D)
	 */
	m->cp_hqd_pipe_priority = 1;
	m->cp_hqd_queue_priority = 15;

	if (q->format == KFD_QUEUE_FORMAT_AQL)
		m->cp_hqd_iq_rptr = AQL_ENABLE;

	*mqd = m;
	if (gart_addr)
		*gart_addr = addr;
	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static int init_mqd_sdma(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	int retval;
	struct cik_sdma_rlc_registers *m;

	retval = kfd_gtt_sa_allocate(mm->dev,
					sizeof(struct cik_sdma_rlc_registers),
					mqd_mem_obj);

	if (retval != 0)
		return -ENOMEM;

	m = (struct cik_sdma_rlc_registers *) (*mqd_mem_obj)->cpu_ptr;

	memset(m, 0, sizeof(struct cik_sdma_rlc_registers));

	*mqd = m;
	if (gart_addr)
		*gart_addr = (*mqd_mem_obj)->gpu_addr;

	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static void uninit_mqd(struct mqd_manager *mm, void *mqd,
			struct kfd_mem_obj *mqd_mem_obj)
{
	kfd_gtt_sa_free(mm->dev, mqd_mem_obj);
}

static void uninit_mqd_sdma(struct mqd_manager *mm, void *mqd,
				struct kfd_mem_obj *mqd_mem_obj)
{
	kfd_gtt_sa_free(mm->dev, mqd_mem_obj);
}

static int load_mqd(struct mqd_manager *mm, void *mqd, uint32_t pipe_id,
		    uint32_t queue_id, struct queue_properties *p,
		    struct mm_struct *mms)
{
	/* AQL write pointer counts in 64B packets, PM4/CP counts in dwords. */
	uint32_t wptr_shift = (p->format == KFD_QUEUE_FORMAT_AQL ? 4 : 0);
	uint32_t wptr_mask = (uint32_t)((p->queue_size / sizeof(uint32_t)) - 1);

	return mm->dev->kfd2kgd->hqd_load(mm->dev->kgd, mqd, pipe_id, queue_id,
					  (uint32_t __user *)p->write_ptr,
					  wptr_shift, wptr_mask, mms);
}

static int load_mqd_sdma(struct mqd_manager *mm, void *mqd,
			 uint32_t pipe_id, uint32_t queue_id,
			 struct queue_properties *p, struct mm_struct *mms)
{
	return mm->dev->kfd2kgd->hqd_sdma_load(mm->dev->kgd, mqd);
}

static int update_mqd(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q)
{
	struct cik_mqd *m;

	m = get_mqd(mqd);
	m->cp_hqd_pq_control = DEFAULT_RPTR_BLOCK_SIZE |
				DEFAULT_MIN_AVAIL_SIZE | PQ_ATC_EN;

	/*
	 * Calculating queue size which is log base 2 of actual queue size -1
	 * dwords and another -1 for ffs
	 */
	m->cp_hqd_pq_control |= ffs(q->queue_size / sizeof(unsigned int))
								- 1 - 1;
	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_doorbell_control = DOORBELL_OFFSET(q->doorbell_off);

	m->cp_hqd_vmid = q->vmid;

	if (q->format == KFD_QUEUE_FORMAT_AQL)
		m->cp_hqd_pq_control |= NO_UPDATE_RPTR;

	q->is_active = false;
	if (q->queue_size > 0 &&
			q->queue_address != 0 &&
			q->queue_percent > 0) {
		q->is_active = true;
	}

	return 0;
}

static int update_mqd_sdma(struct mqd_manager *mm, void *mqd,
				struct queue_properties *q)
{
	struct cik_sdma_rlc_registers *m;

	m = get_sdma_mqd(mqd);
	m->sdma_rlc_rb_cntl = ffs(q->queue_size / sizeof(unsigned int)) <<
			SDMA0_RLC0_RB_CNTL__RB_SIZE__SHIFT |
			q->vmid << SDMA0_RLC0_RB_CNTL__RB_VMID__SHIFT |
			1 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT |
			6 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT;

	m->sdma_rlc_rb_base = lower_32_bits(q->queue_address >> 8);
	m->sdma_rlc_rb_base_hi = upper_32_bits(q->queue_address >> 8);
	m->sdma_rlc_rb_rptr_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->sdma_rlc_rb_rptr_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->sdma_rlc_doorbell = q->doorbell_off <<
			SDMA0_RLC0_DOORBELL__OFFSET__SHIFT |
			1 << SDMA0_RLC0_DOORBELL__ENABLE__SHIFT;

	m->sdma_rlc_virtual_addr = q->sdma_vm_addr;

	m->sdma_engine_id = q->sdma_engine_id;
	m->sdma_queue_id = q->sdma_queue_id;

	q->is_active = false;
	if (q->queue_size > 0 &&
			q->queue_address != 0 &&
			q->queue_percent > 0) {
		m->sdma_rlc_rb_cntl |=
				1 << SDMA0_RLC0_RB_CNTL__RB_ENABLE__SHIFT;

		q->is_active = true;
	}

	return 0;
}

static int destroy_mqd(struct mqd_manager *mm, void *mqd,
			enum kfd_preempt_type type,
			unsigned int timeout, uint32_t pipe_id,
			uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_destroy(mm->dev->kgd, mqd, type, timeout,
					pipe_id, queue_id);
}

/*
 * preempt type here is ignored because there is only one way
 * to preempt sdma queue
 */
static int destroy_mqd_sdma(struct mqd_manager *mm, void *mqd,
				enum kfd_preempt_type type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_sdma_destroy(mm->dev->kgd, mqd, timeout);
}

static bool is_occupied(struct mqd_manager *mm, void *mqd,
			uint64_t queue_address,	uint32_t pipe_id,
			uint32_t queue_id)
{

	return mm->dev->kfd2kgd->hqd_is_occupied(mm->dev->kgd, queue_address,
					pipe_id, queue_id);

}

static bool is_occupied_sdma(struct mqd_manager *mm, void *mqd,
			uint64_t queue_address,	uint32_t pipe_id,
			uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_sdma_is_occupied(mm->dev->kgd, mqd);
}

/*
 * HIQ MQD Implementation, concrete implementation for HIQ MQD implementation.
 * The HIQ queue in Kaveri is using the same MQD structure as all the user mode
 * queues but with different initial values.
 */

static int init_mqd_hiq(struct mqd_manager *mm, void **mqd,
		struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
		struct queue_properties *q)
{
	uint64_t addr;
	struct cik_mqd *m;
	int retval;

	retval = kfd_gtt_sa_allocate(mm->dev, sizeof(struct cik_mqd),
					mqd_mem_obj);

	if (retval != 0)
		return -ENOMEM;

	m = (struct cik_mqd *) (*mqd_mem_obj)->cpu_ptr;
	addr = (*mqd_mem_obj)->gpu_addr;

	memset(m, 0, ALIGN(sizeof(struct cik_mqd), 256));

	m->header = 0xC0310800;
	m->compute_pipelinestat_enable = 1;
	m->compute_static_thread_mgmt_se0 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se1 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se2 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se3 = 0xFFFFFFFF;

	m->cp_hqd_persistent_state = DEFAULT_CP_HQD_PERSISTENT_STATE |
					PRELOAD_REQ;
	m->cp_hqd_quantum = QUANTUM_EN | QUANTUM_SCALE_1MS |
				QUANTUM_DURATION(10);

	m->cp_mqd_control             = MQD_CONTROL_PRIV_STATE_EN;
	m->cp_mqd_base_addr_lo        = lower_32_bits(addr);
	m->cp_mqd_base_addr_hi        = upper_32_bits(addr);

	m->cp_hqd_ib_control = DEFAULT_MIN_IB_AVAIL_SIZE;

	/*
	 * Pipe Priority
	 * Identifies the pipe relative priority when this queue is connected
	 * to the pipeline. The pipe priority is against the GFX pipe and HP3D.
	 * In KFD we are using a fixed pipe priority set to CS_MEDIUM.
	 * 0 = CS_LOW (typically below GFX)
	 * 1 = CS_MEDIUM (typically between HP3D and GFX
	 * 2 = CS_HIGH (typically above HP3D)
	 */
	m->cp_hqd_pipe_priority = 1;
	m->cp_hqd_queue_priority = 15;

	*mqd = m;
	if (gart_addr)
		*gart_addr = addr;
	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static int update_mqd_hiq(struct mqd_manager *mm, void *mqd,
				struct queue_properties *q)
{
	struct cik_mqd *m;

	m = get_mqd(mqd);
	m->cp_hqd_pq_control = DEFAULT_RPTR_BLOCK_SIZE |
				DEFAULT_MIN_AVAIL_SIZE |
				PRIV_STATE |
				KMD_QUEUE;

	/*
	 * Calculating queue size which is log base 2 of actual queue
	 * size -1 dwords
	 */
	m->cp_hqd_pq_control |= ffs(q->queue_size / sizeof(unsigned int))
								- 1 - 1;
	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_doorbell_control = DOORBELL_EN |
					DOORBELL_OFFSET(q->doorbell_off);

	m->cp_hqd_vmid = q->vmid;

	m->cp_hqd_active = 0;
	q->is_active = false;
	if (q->queue_size > 0 &&
			q->queue_address != 0 &&
			q->queue_percent > 0) {
		m->cp_hqd_active = 1;
		q->is_active = true;
	}

	return 0;
}

struct cik_sdma_rlc_registers *get_sdma_mqd(void *mqd)
{
	struct cik_sdma_rlc_registers *m;

	m = (struct cik_sdma_rlc_registers *)mqd;

	return m;
}

struct mqd_manager *mqd_manager_init_cik(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev)
{
	struct mqd_manager *mqd;

	if (WARN_ON(type >= KFD_MQD_TYPE_MAX))
		return NULL;

	mqd = kzalloc(sizeof(*mqd), GFP_KERNEL);
	if (!mqd)
		return NULL;

	mqd->dev = dev;

	switch (type) {
	case KFD_MQD_TYPE_CP:
	case KFD_MQD_TYPE_COMPUTE:
		mqd->init_mqd = init_mqd;
		mqd->uninit_mqd = uninit_mqd;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd;
		mqd->destroy_mqd = destroy_mqd;
		mqd->is_occupied = is_occupied;
		break;
	case KFD_MQD_TYPE_HIQ:
		mqd->init_mqd = init_mqd_hiq;
		mqd->uninit_mqd = uninit_mqd;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd_hiq;
		mqd->destroy_mqd = destroy_mqd;
		mqd->is_occupied = is_occupied;
		break;
	case KFD_MQD_TYPE_SDMA:
		mqd->init_mqd = init_mqd_sdma;
		mqd->uninit_mqd = uninit_mqd_sdma;
		mqd->load_mqd = load_mqd_sdma;
		mqd->update_mqd = update_mqd_sdma;
		mqd->destroy_mqd = destroy_mqd_sdma;
		mqd->is_occupied = is_occupied_sdma;
		break;
	default:
		kfree(mqd);
		return NULL;
	}

	return mqd;
}


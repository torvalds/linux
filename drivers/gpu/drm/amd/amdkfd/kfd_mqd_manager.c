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
#include "kfd_priv.h"
#include "kfd_mqd_manager.h"
#include "cik_regs.h"
#include "../../radeon/cik_reg.h"

inline void busy_wait(unsigned long ms)
{
	while (time_before(jiffies, ms))
		cpu_relax();
}

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

	BUG_ON(!mm || !q || !mqd);

	pr_debug("kfd: In func %s\n", __func__);

	retval = kfd2kgd->allocate_mem(mm->dev->kgd,
					sizeof(struct cik_mqd),
					256,
					KFD_MEMPOOL_SYSTEM_WRITECOMBINE,
					(struct kgd_mem **) mqd_mem_obj);

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

	*mqd = m;
	if (gart_addr != NULL)
		*gart_addr = addr;
	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static void uninit_mqd(struct mqd_manager *mm, void *mqd,
			struct kfd_mem_obj *mqd_mem_obj)
{
	BUG_ON(!mm || !mqd);
	kfd2kgd->free_mem(mm->dev->kgd, (struct kgd_mem *) mqd_mem_obj);
}

static int load_mqd(struct mqd_manager *mm, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr)
{
	return kfd2kgd->hqd_load(mm->dev->kgd, mqd, pipe_id, queue_id, wptr);

}

static int update_mqd(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q)
{
	struct cik_mqd *m;

	BUG_ON(!mm || !q || !mqd);

	pr_debug("kfd: In func %s\n", __func__);

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
	m->cp_hqd_pq_doorbell_control = DOORBELL_EN |
					DOORBELL_OFFSET(q->doorbell_off);

	m->cp_hqd_vmid = q->vmid;

	if (q->format == KFD_QUEUE_FORMAT_AQL) {
		m->cp_hqd_iq_rptr = AQL_ENABLE;
		m->cp_hqd_pq_control |= NO_UPDATE_RPTR;
	}

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

static int destroy_mqd(struct mqd_manager *mm, void *mqd,
			enum kfd_preempt_type type,
			unsigned int timeout, uint32_t pipe_id,
			uint32_t queue_id)
{
	return kfd2kgd->hqd_destroy(mm->dev->kgd, type, timeout,
					pipe_id, queue_id);
}

static bool is_occupied(struct mqd_manager *mm, void *mqd,
			uint64_t queue_address,	uint32_t pipe_id,
			uint32_t queue_id)
{

	return kfd2kgd->hqd_is_occupied(mm->dev->kgd, queue_address,
					pipe_id, queue_id);

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

	BUG_ON(!mm || !q || !mqd || !mqd_mem_obj);

	pr_debug("kfd: In func %s\n", __func__);

	retval = kfd2kgd->allocate_mem(mm->dev->kgd,
					sizeof(struct cik_mqd),
					256,
					KFD_MEMPOOL_SYSTEM_WRITECOMBINE,
					(struct kgd_mem **) mqd_mem_obj);

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

	BUG_ON(!mm || !q || !mqd);

	pr_debug("kfd: In func %s\n", __func__);

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

struct mqd_manager *mqd_manager_init(enum KFD_MQD_TYPE type,
					struct kfd_dev *dev)
{
	struct mqd_manager *mqd;

	BUG_ON(!dev);
	BUG_ON(type >= KFD_MQD_TYPE_MAX);

	pr_debug("kfd: In func %s\n", __func__);

	mqd = kzalloc(sizeof(struct mqd_manager), GFP_KERNEL);
	if (!mqd)
		return NULL;

	mqd->dev = dev;

	switch (type) {
	case KFD_MQD_TYPE_CIK_CP:
	case KFD_MQD_TYPE_CIK_COMPUTE:
		mqd->init_mqd = init_mqd;
		mqd->uninit_mqd = uninit_mqd;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd;
		mqd->destroy_mqd = destroy_mqd;
		mqd->is_occupied = is_occupied;
		break;
	case KFD_MQD_TYPE_CIK_HIQ:
		mqd->init_mqd = init_mqd_hiq;
		mqd->uninit_mqd = uninit_mqd;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd_hiq;
		mqd->destroy_mqd = destroy_mqd;
		mqd->is_occupied = is_occupied;
		break;
	default:
		kfree(mqd);
		return NULL;
	}

	return mqd;
}

/* SDMA queues should be implemented here when the cp will supports them */

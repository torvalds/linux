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
#include "vi_structs.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "gca/gfx_8_0_enum.h"

#define CP_MQD_CONTROL__PRIV_STATE__SHIFT 0x8

static inline struct vi_mqd *get_mqd(void *mqd)
{
	return (struct vi_mqd *)mqd;
}

static int init_mqd(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	int retval;
	uint64_t addr;
	struct vi_mqd *m;

	retval = kfd_gtt_sa_allocate(mm->dev, sizeof(struct vi_mqd),
			mqd_mem_obj);
	if (retval != 0)
		return -ENOMEM;

	m = (struct vi_mqd *) (*mqd_mem_obj)->cpu_ptr;
	addr = (*mqd_mem_obj)->gpu_addr;

	memset(m, 0, sizeof(struct vi_mqd));

	m->header = 0xC0310800;
	m->compute_pipelinestat_enable = 1;
	m->compute_static_thread_mgmt_se0 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se1 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se2 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se3 = 0xFFFFFFFF;

	m->cp_hqd_persistent_state = CP_HQD_PERSISTENT_STATE__PRELOAD_REQ_MASK |
			0x53 << CP_HQD_PERSISTENT_STATE__PRELOAD_SIZE__SHIFT;

	m->cp_mqd_control = 1 << CP_MQD_CONTROL__PRIV_STATE__SHIFT |
			MTYPE_UC << CP_MQD_CONTROL__MTYPE__SHIFT;

	m->cp_mqd_base_addr_lo        = lower_32_bits(addr);
	m->cp_mqd_base_addr_hi        = upper_32_bits(addr);

	m->cp_hqd_quantum = 1 << CP_HQD_QUANTUM__QUANTUM_EN__SHIFT |
			1 << CP_HQD_QUANTUM__QUANTUM_SCALE__SHIFT |
			10 << CP_HQD_QUANTUM__QUANTUM_DURATION__SHIFT;

	m->cp_hqd_pipe_priority = 1;
	m->cp_hqd_queue_priority = 15;

	m->cp_hqd_eop_rptr = 1 << CP_HQD_EOP_RPTR__INIT_FETCHER__SHIFT;

	if (q->format == KFD_QUEUE_FORMAT_AQL)
		m->cp_hqd_iq_rptr = 1;

	*mqd = m;
	if (gart_addr != NULL)
		*gart_addr = addr;
	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static int load_mqd(struct mqd_manager *mm, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t __user *wptr)
{
	return mm->dev->kfd2kgd->hqd_load
		(mm->dev->kgd, mqd, pipe_id, queue_id, wptr);
}

static int __update_mqd(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q, unsigned int mtype,
			unsigned int atc_bit)
{
	struct vi_mqd *m;

	BUG_ON(!mm || !q || !mqd);

	pr_debug("kfd: In func %s\n", __func__);

	m = get_mqd(mqd);

	m->cp_hqd_pq_control = 5 << CP_HQD_PQ_CONTROL__RPTR_BLOCK_SIZE__SHIFT |
			atc_bit << CP_HQD_PQ_CONTROL__PQ_ATC__SHIFT |
			mtype << CP_HQD_PQ_CONTROL__MTYPE__SHIFT;
	m->cp_hqd_pq_control |=
			ffs(q->queue_size / sizeof(unsigned int)) - 1 - 1;
	pr_debug("kfd: cp_hqd_pq_control 0x%x\n", m->cp_hqd_pq_control);

	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);

	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);

	m->cp_hqd_pq_doorbell_control =
		1 << CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_EN__SHIFT |
		q->doorbell_off <<
			CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_OFFSET__SHIFT;
	pr_debug("kfd: cp_hqd_pq_doorbell_control 0x%x\n",
			m->cp_hqd_pq_doorbell_control);

	m->cp_hqd_eop_control = atc_bit << CP_HQD_EOP_CONTROL__EOP_ATC__SHIFT |
			mtype << CP_HQD_EOP_CONTROL__MTYPE__SHIFT;

	m->cp_hqd_ib_control = atc_bit << CP_HQD_IB_CONTROL__IB_ATC__SHIFT |
			3 << CP_HQD_IB_CONTROL__MIN_IB_AVAIL_SIZE__SHIFT |
			mtype << CP_HQD_IB_CONTROL__MTYPE__SHIFT;

	m->cp_hqd_eop_control |=
		ffs(q->eop_ring_buffer_size / sizeof(unsigned int)) - 1 - 1;
	m->cp_hqd_eop_base_addr_lo =
			lower_32_bits(q->eop_ring_buffer_address >> 8);
	m->cp_hqd_eop_base_addr_hi =
			upper_32_bits(q->eop_ring_buffer_address >> 8);

	m->cp_hqd_iq_timer = atc_bit << CP_HQD_IQ_TIMER__IQ_ATC__SHIFT |
			mtype << CP_HQD_IQ_TIMER__MTYPE__SHIFT;

	m->cp_hqd_vmid = q->vmid;

	if (q->format == KFD_QUEUE_FORMAT_AQL) {
		m->cp_hqd_pq_control |= CP_HQD_PQ_CONTROL__NO_UPDATE_RPTR_MASK |
				2 << CP_HQD_PQ_CONTROL__SLOT_BASED_WPTR__SHIFT;
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


static int update_mqd(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q)
{
	return __update_mqd(mm, mqd, q, MTYPE_CC, 1);
}

static int destroy_mqd(struct mqd_manager *mm, void *mqd,
			enum kfd_preempt_type type,
			unsigned int timeout, uint32_t pipe_id,
			uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_destroy
		(mm->dev->kgd, type, timeout,
		pipe_id, queue_id);
}

static void uninit_mqd(struct mqd_manager *mm, void *mqd,
			struct kfd_mem_obj *mqd_mem_obj)
{
	BUG_ON(!mm || !mqd);
	kfd_gtt_sa_free(mm->dev, mqd_mem_obj);
}

static bool is_occupied(struct mqd_manager *mm, void *mqd,
			uint64_t queue_address,	uint32_t pipe_id,
			uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_is_occupied(
		mm->dev->kgd, queue_address,
		pipe_id, queue_id);
}

static int init_mqd_hiq(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	struct vi_mqd *m;
	int retval = init_mqd(mm, mqd, mqd_mem_obj, gart_addr, q);

	if (retval != 0)
		return retval;

	m = get_mqd(*mqd);

	m->cp_hqd_pq_control |= 1 << CP_HQD_PQ_CONTROL__PRIV_STATE__SHIFT |
			1 << CP_HQD_PQ_CONTROL__KMD_QUEUE__SHIFT;

	return retval;
}

static int update_mqd_hiq(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q)
{
	struct vi_mqd *m;
	int retval = __update_mqd(mm, mqd, q, MTYPE_UC, 0);

	if (retval != 0)
		return retval;

	m = get_mqd(mqd);
	m->cp_hqd_vmid = q->vmid;
	return retval;
}

struct mqd_manager *mqd_manager_init_vi(enum KFD_MQD_TYPE type,
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
		break;
	default:
		kfree(mqd);
		return NULL;
	}

	return mqd;
}

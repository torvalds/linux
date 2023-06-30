// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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

#include <linux/ratelimit.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_mqd_manager.h"
#include "cik_regs.h"
#include "kfd_kernel_queue.h"
#include "amdgpu_amdkfd.h"
#include "mes_api_def.h"
#include "kfd_debug.h"

/* Size of the per-pipe EOP queue */
#define CIK_HPD_EOP_BYTES_LOG2 11
#define CIK_HPD_EOP_BYTES (1U << CIK_HPD_EOP_BYTES_LOG2)

static int set_pasid_vmid_mapping(struct device_queue_manager *dqm,
				  u32 pasid, unsigned int vmid);

static int execute_queues_cpsch(struct device_queue_manager *dqm,
				enum kfd_unmap_queues_filter filter,
				uint32_t filter_param,
				uint32_t grace_period);
static int unmap_queues_cpsch(struct device_queue_manager *dqm,
				enum kfd_unmap_queues_filter filter,
				uint32_t filter_param,
				uint32_t grace_period,
				bool reset);

static int map_queues_cpsch(struct device_queue_manager *dqm);

static void deallocate_sdma_queue(struct device_queue_manager *dqm,
				struct queue *q);

static inline void deallocate_hqd(struct device_queue_manager *dqm,
				struct queue *q);
static int allocate_hqd(struct device_queue_manager *dqm, struct queue *q);
static int allocate_sdma_queue(struct device_queue_manager *dqm,
				struct queue *q, const uint32_t *restore_sdma_id);
static void kfd_process_hw_exception(struct work_struct *work);

static inline
enum KFD_MQD_TYPE get_mqd_type_from_queue_type(enum kfd_queue_type type)
{
	if (type == KFD_QUEUE_TYPE_SDMA || type == KFD_QUEUE_TYPE_SDMA_XGMI)
		return KFD_MQD_TYPE_SDMA;
	return KFD_MQD_TYPE_CP;
}

static bool is_pipe_enabled(struct device_queue_manager *dqm, int mec, int pipe)
{
	int i;
	int pipe_offset = (mec * dqm->dev->kfd->shared_resources.num_pipe_per_mec
		+ pipe) * dqm->dev->kfd->shared_resources.num_queue_per_pipe;

	/* queue is available for KFD usage if bit is 1 */
	for (i = 0; i <  dqm->dev->kfd->shared_resources.num_queue_per_pipe; ++i)
		if (test_bit(pipe_offset + i,
			      dqm->dev->kfd->shared_resources.cp_queue_bitmap))
			return true;
	return false;
}

unsigned int get_cp_queues_num(struct device_queue_manager *dqm)
{
	return bitmap_weight(dqm->dev->kfd->shared_resources.cp_queue_bitmap,
				KGD_MAX_QUEUES);
}

unsigned int get_queues_per_pipe(struct device_queue_manager *dqm)
{
	return dqm->dev->kfd->shared_resources.num_queue_per_pipe;
}

unsigned int get_pipes_per_mec(struct device_queue_manager *dqm)
{
	return dqm->dev->kfd->shared_resources.num_pipe_per_mec;
}

static unsigned int get_num_all_sdma_engines(struct device_queue_manager *dqm)
{
	return kfd_get_num_sdma_engines(dqm->dev) +
		kfd_get_num_xgmi_sdma_engines(dqm->dev);
}

unsigned int get_num_sdma_queues(struct device_queue_manager *dqm)
{
	return kfd_get_num_sdma_engines(dqm->dev) *
		dqm->dev->kfd->device_info.num_sdma_queues_per_engine;
}

unsigned int get_num_xgmi_sdma_queues(struct device_queue_manager *dqm)
{
	return kfd_get_num_xgmi_sdma_engines(dqm->dev) *
		dqm->dev->kfd->device_info.num_sdma_queues_per_engine;
}

static void init_sdma_bitmaps(struct device_queue_manager *dqm)
{
	bitmap_zero(dqm->sdma_bitmap, KFD_MAX_SDMA_QUEUES);
	bitmap_set(dqm->sdma_bitmap, 0, get_num_sdma_queues(dqm));

	bitmap_zero(dqm->xgmi_sdma_bitmap, KFD_MAX_SDMA_QUEUES);
	bitmap_set(dqm->xgmi_sdma_bitmap, 0, get_num_xgmi_sdma_queues(dqm));

	/* Mask out the reserved queues */
	bitmap_andnot(dqm->sdma_bitmap, dqm->sdma_bitmap,
		      dqm->dev->kfd->device_info.reserved_sdma_queues_bitmap,
		      KFD_MAX_SDMA_QUEUES);
}

void program_sh_mem_settings(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd)
{
	uint32_t xcc_mask = dqm->dev->xcc_mask;
	int xcc_id;

	for_each_inst(xcc_id, xcc_mask)
		dqm->dev->kfd2kgd->program_sh_mem_settings(
			dqm->dev->adev, qpd->vmid, qpd->sh_mem_config,
			qpd->sh_mem_ape1_base, qpd->sh_mem_ape1_limit,
			qpd->sh_mem_bases, xcc_id);
}

static void kfd_hws_hang(struct device_queue_manager *dqm)
{
	/*
	 * Issue a GPU reset if HWS is unresponsive
	 */
	dqm->is_hws_hang = true;

	/* It's possible we're detecting a HWS hang in the
	 * middle of a GPU reset. No need to schedule another
	 * reset in this case.
	 */
	if (!dqm->is_resetting)
		schedule_work(&dqm->hw_exception_work);
}

static int convert_to_mes_queue_type(int queue_type)
{
	int mes_queue_type;

	switch (queue_type) {
	case KFD_QUEUE_TYPE_COMPUTE:
		mes_queue_type = MES_QUEUE_TYPE_COMPUTE;
		break;
	case KFD_QUEUE_TYPE_SDMA:
		mes_queue_type = MES_QUEUE_TYPE_SDMA;
		break;
	default:
		WARN(1, "Invalid queue type %d", queue_type);
		mes_queue_type = -EINVAL;
		break;
	}

	return mes_queue_type;
}

static int add_queue_mes(struct device_queue_manager *dqm, struct queue *q,
			 struct qcm_process_device *qpd)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)dqm->dev->adev;
	struct kfd_process_device *pdd = qpd_to_pdd(qpd);
	struct mes_add_queue_input queue_input;
	int r, queue_type;
	uint64_t wptr_addr_off;

	if (dqm->is_hws_hang)
		return -EIO;

	memset(&queue_input, 0x0, sizeof(struct mes_add_queue_input));
	queue_input.process_id = qpd->pqm->process->pasid;
	queue_input.page_table_base_addr =  qpd->page_table_base;
	queue_input.process_va_start = 0;
	queue_input.process_va_end = adev->vm_manager.max_pfn - 1;
	/* MES unit for quantum is 100ns */
	queue_input.process_quantum = KFD_MES_PROCESS_QUANTUM;  /* Equivalent to 10ms. */
	queue_input.process_context_addr = pdd->proc_ctx_gpu_addr;
	queue_input.gang_quantum = KFD_MES_GANG_QUANTUM; /* Equivalent to 1ms */
	queue_input.gang_context_addr = q->gang_ctx_gpu_addr;
	queue_input.inprocess_gang_priority = q->properties.priority;
	queue_input.gang_global_priority_level =
					AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	queue_input.doorbell_offset = q->properties.doorbell_off;
	queue_input.mqd_addr = q->gart_mqd_addr;
	queue_input.wptr_addr = (uint64_t)q->properties.write_ptr;

	if (q->wptr_bo) {
		wptr_addr_off = (uint64_t)q->properties.write_ptr & (PAGE_SIZE - 1);
		queue_input.wptr_mc_addr = ((uint64_t)q->wptr_bo->tbo.resource->start << PAGE_SHIFT) + wptr_addr_off;
	}

	queue_input.is_kfd_process = 1;
	queue_input.is_aql_queue = (q->properties.format == KFD_QUEUE_FORMAT_AQL);
	queue_input.queue_size = q->properties.queue_size >> 2;

	queue_input.paging = false;
	queue_input.tba_addr = qpd->tba_addr;
	queue_input.tma_addr = qpd->tma_addr;
	queue_input.trap_en = KFD_GC_VERSION(q->device) < IP_VERSION(11, 0, 0) ||
			      KFD_GC_VERSION(q->device) > IP_VERSION(11, 0, 3);
	queue_input.skip_process_ctx_clear = qpd->pqm->process->debug_trap_enabled;

	queue_type = convert_to_mes_queue_type(q->properties.type);
	if (queue_type < 0) {
		pr_err("Queue type not supported with MES, queue:%d\n",
				q->properties.type);
		return -EINVAL;
	}
	queue_input.queue_type = (uint32_t)queue_type;

	if (q->gws) {
		queue_input.gws_base = 0;
		queue_input.gws_size = qpd->num_gws;
	}

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->add_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r) {
		pr_err("failed to add hardware queue to MES, doorbell=0x%x\n",
			q->properties.doorbell_off);
		pr_err("MES might be in unrecoverable state, issue a GPU reset\n");
		kfd_hws_hang(dqm);
}

	return r;
}

static int remove_queue_mes(struct device_queue_manager *dqm, struct queue *q,
			struct qcm_process_device *qpd)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)dqm->dev->adev;
	int r;
	struct mes_remove_queue_input queue_input;

	if (dqm->is_hws_hang)
		return -EIO;

	memset(&queue_input, 0x0, sizeof(struct mes_remove_queue_input));
	queue_input.doorbell_offset = q->properties.doorbell_off;
	queue_input.gang_context_addr = q->gang_ctx_gpu_addr;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->remove_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);

	if (r) {
		pr_err("failed to remove hardware queue from MES, doorbell=0x%x\n",
			q->properties.doorbell_off);
		pr_err("MES might be in unrecoverable state, issue a GPU reset\n");
		kfd_hws_hang(dqm);
	}

	return r;
}

static int remove_all_queues_mes(struct device_queue_manager *dqm)
{
	struct device_process_node *cur;
	struct qcm_process_device *qpd;
	struct queue *q;
	int retval = 0;

	list_for_each_entry(cur, &dqm->queues, list) {
		qpd = cur->qpd;
		list_for_each_entry(q, &qpd->queues_list, list) {
			if (q->properties.is_active) {
				retval = remove_queue_mes(dqm, q, qpd);
				if (retval) {
					pr_err("%s: Failed to remove queue %d for dev %d",
						__func__,
						q->properties.queue_id,
						dqm->dev->id);
					return retval;
				}
			}
		}
	}

	return retval;
}

static void increment_queue_count(struct device_queue_manager *dqm,
				  struct qcm_process_device *qpd,
				  struct queue *q)
{
	dqm->active_queue_count++;
	if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE ||
	    q->properties.type == KFD_QUEUE_TYPE_DIQ)
		dqm->active_cp_queue_count++;

	if (q->properties.is_gws) {
		dqm->gws_queue_count++;
		qpd->mapped_gws_queue = true;
	}
}

static void decrement_queue_count(struct device_queue_manager *dqm,
				  struct qcm_process_device *qpd,
				  struct queue *q)
{
	dqm->active_queue_count--;
	if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE ||
	    q->properties.type == KFD_QUEUE_TYPE_DIQ)
		dqm->active_cp_queue_count--;

	if (q->properties.is_gws) {
		dqm->gws_queue_count--;
		qpd->mapped_gws_queue = false;
	}
}

/*
 * Allocate a doorbell ID to this queue.
 * If doorbell_id is passed in, make sure requested ID is valid then allocate it.
 */
static int allocate_doorbell(struct qcm_process_device *qpd,
			     struct queue *q,
			     uint32_t const *restore_id)
{
	struct kfd_node *dev = qpd->dqm->dev;

	if (!KFD_IS_SOC15(dev)) {
		/* On pre-SOC15 chips we need to use the queue ID to
		 * preserve the user mode ABI.
		 */

		if (restore_id && *restore_id != q->properties.queue_id)
			return -EINVAL;

		q->doorbell_id = q->properties.queue_id;
	} else if (q->properties.type == KFD_QUEUE_TYPE_SDMA ||
			q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
		/* For SDMA queues on SOC15 with 8-byte doorbell, use static
		 * doorbell assignments based on the engine and queue id.
		 * The doobell index distance between RLC (2*i) and (2*i+1)
		 * for a SDMA engine is 512.
		 */

		uint32_t *idx_offset = dev->kfd->shared_resources.sdma_doorbell_idx;

		/*
		 * q->properties.sdma_engine_id corresponds to the virtual
		 * sdma engine number. However, for doorbell allocation,
		 * we need the physical sdma engine id in order to get the
		 * correct doorbell offset.
		 */
		uint32_t valid_id = idx_offset[qpd->dqm->dev->node_id *
					       get_num_all_sdma_engines(qpd->dqm) +
					       q->properties.sdma_engine_id]
						+ (q->properties.sdma_queue_id & 1)
						* KFD_QUEUE_DOORBELL_MIRROR_OFFSET
						+ (q->properties.sdma_queue_id >> 1);

		if (restore_id && *restore_id != valid_id)
			return -EINVAL;
		q->doorbell_id = valid_id;
	} else {
		/* For CP queues on SOC15 */
		if (restore_id) {
			/* make sure that ID is free  */
			if (__test_and_set_bit(*restore_id, qpd->doorbell_bitmap))
				return -EINVAL;

			q->doorbell_id = *restore_id;
		} else {
			/* or reserve a free doorbell ID */
			unsigned int found;

			found = find_first_zero_bit(qpd->doorbell_bitmap,
						KFD_MAX_NUM_OF_QUEUES_PER_PROCESS);
			if (found >= KFD_MAX_NUM_OF_QUEUES_PER_PROCESS) {
				pr_debug("No doorbells available");
				return -EBUSY;
			}
			set_bit(found, qpd->doorbell_bitmap);
			q->doorbell_id = found;
		}
	}

	q->properties.doorbell_off =
		kfd_get_doorbell_dw_offset_in_bar(dev->kfd, qpd_to_pdd(qpd),
					  q->doorbell_id);
	return 0;
}

static void deallocate_doorbell(struct qcm_process_device *qpd,
				struct queue *q)
{
	unsigned int old;
	struct kfd_node *dev = qpd->dqm->dev;

	if (!KFD_IS_SOC15(dev) ||
	    q->properties.type == KFD_QUEUE_TYPE_SDMA ||
	    q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)
		return;

	old = test_and_clear_bit(q->doorbell_id, qpd->doorbell_bitmap);
	WARN_ON(!old);
}

static void program_trap_handler_settings(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd)
{
	uint32_t xcc_mask = dqm->dev->xcc_mask;
	int xcc_id;

	if (dqm->dev->kfd2kgd->program_trap_handler_settings)
		for_each_inst(xcc_id, xcc_mask)
			dqm->dev->kfd2kgd->program_trap_handler_settings(
				dqm->dev->adev, qpd->vmid, qpd->tba_addr,
				qpd->tma_addr, xcc_id);
}

static int allocate_vmid(struct device_queue_manager *dqm,
			struct qcm_process_device *qpd,
			struct queue *q)
{
	int allocated_vmid = -1, i;

	for (i = dqm->dev->vm_info.first_vmid_kfd;
			i <= dqm->dev->vm_info.last_vmid_kfd; i++) {
		if (!dqm->vmid_pasid[i]) {
			allocated_vmid = i;
			break;
		}
	}

	if (allocated_vmid < 0) {
		pr_err("no more vmid to allocate\n");
		return -ENOSPC;
	}

	pr_debug("vmid allocated: %d\n", allocated_vmid);

	dqm->vmid_pasid[allocated_vmid] = q->process->pasid;

	set_pasid_vmid_mapping(dqm, q->process->pasid, allocated_vmid);

	qpd->vmid = allocated_vmid;
	q->properties.vmid = allocated_vmid;

	program_sh_mem_settings(dqm, qpd);

	if (KFD_IS_SOC15(dqm->dev) && dqm->dev->kfd->cwsr_enabled)
		program_trap_handler_settings(dqm, qpd);

	/* qpd->page_table_base is set earlier when register_process()
	 * is called, i.e. when the first queue is created.
	 */
	dqm->dev->kfd2kgd->set_vm_context_page_table_base(dqm->dev->adev,
			qpd->vmid,
			qpd->page_table_base);
	/* invalidate the VM context after pasid and vmid mapping is set up */
	kfd_flush_tlb(qpd_to_pdd(qpd), TLB_FLUSH_LEGACY);

	if (dqm->dev->kfd2kgd->set_scratch_backing_va)
		dqm->dev->kfd2kgd->set_scratch_backing_va(dqm->dev->adev,
				qpd->sh_hidden_private_base, qpd->vmid);

	return 0;
}

static int flush_texture_cache_nocpsch(struct kfd_node *kdev,
				struct qcm_process_device *qpd)
{
	const struct packet_manager_funcs *pmf = qpd->dqm->packet_mgr.pmf;
	int ret;

	if (!qpd->ib_kaddr)
		return -ENOMEM;

	ret = pmf->release_mem(qpd->ib_base, (uint32_t *)qpd->ib_kaddr);
	if (ret)
		return ret;

	return amdgpu_amdkfd_submit_ib(kdev->adev, KGD_ENGINE_MEC1, qpd->vmid,
				qpd->ib_base, (uint32_t *)qpd->ib_kaddr,
				pmf->release_mem_size / sizeof(uint32_t));
}

static void deallocate_vmid(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd,
				struct queue *q)
{
	/* On GFX v7, CP doesn't flush TC at dequeue */
	if (q->device->adev->asic_type == CHIP_HAWAII)
		if (flush_texture_cache_nocpsch(q->device, qpd))
			pr_err("Failed to flush TC\n");

	kfd_flush_tlb(qpd_to_pdd(qpd), TLB_FLUSH_LEGACY);

	/* Release the vmid mapping */
	set_pasid_vmid_mapping(dqm, 0, qpd->vmid);
	dqm->vmid_pasid[qpd->vmid] = 0;

	qpd->vmid = 0;
	q->properties.vmid = 0;
}

static int create_queue_nocpsch(struct device_queue_manager *dqm,
				struct queue *q,
				struct qcm_process_device *qpd,
				const struct kfd_criu_queue_priv_data *qd,
				const void *restore_mqd, const void *restore_ctl_stack)
{
	struct mqd_manager *mqd_mgr;
	int retval;

	dqm_lock(dqm);

	if (dqm->total_queue_count >= max_num_of_queues_per_device) {
		pr_warn("Can't create new usermode queue because %d queues were already created\n",
				dqm->total_queue_count);
		retval = -EPERM;
		goto out_unlock;
	}

	if (list_empty(&qpd->queues_list)) {
		retval = allocate_vmid(dqm, qpd, q);
		if (retval)
			goto out_unlock;
	}
	q->properties.vmid = qpd->vmid;
	/*
	 * Eviction state logic: mark all queues as evicted, even ones
	 * not currently active. Restoring inactive queues later only
	 * updates the is_evicted flag but is a no-op otherwise.
	 */
	q->properties.is_evicted = !!qpd->evicted;

	q->properties.tba_addr = qpd->tba_addr;
	q->properties.tma_addr = qpd->tma_addr;

	mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
			q->properties.type)];
	if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE) {
		retval = allocate_hqd(dqm, q);
		if (retval)
			goto deallocate_vmid;
		pr_debug("Loading mqd to hqd on pipe %d, queue %d\n",
			q->pipe, q->queue);
	} else if (q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
		retval = allocate_sdma_queue(dqm, q, qd ? &qd->sdma_id : NULL);
		if (retval)
			goto deallocate_vmid;
		dqm->asic_ops.init_sdma_vm(dqm, q, qpd);
	}

	retval = allocate_doorbell(qpd, q, qd ? &qd->doorbell_id : NULL);
	if (retval)
		goto out_deallocate_hqd;

	/* Temporarily release dqm lock to avoid a circular lock dependency */
	dqm_unlock(dqm);
	q->mqd_mem_obj = mqd_mgr->allocate_mqd(mqd_mgr->dev, &q->properties);
	dqm_lock(dqm);

	if (!q->mqd_mem_obj) {
		retval = -ENOMEM;
		goto out_deallocate_doorbell;
	}

	if (qd)
		mqd_mgr->restore_mqd(mqd_mgr, &q->mqd, q->mqd_mem_obj, &q->gart_mqd_addr,
				     &q->properties, restore_mqd, restore_ctl_stack,
				     qd->ctl_stack_size);
	else
		mqd_mgr->init_mqd(mqd_mgr, &q->mqd, q->mqd_mem_obj,
					&q->gart_mqd_addr, &q->properties);

	if (q->properties.is_active) {
		if (!dqm->sched_running) {
			WARN_ONCE(1, "Load non-HWS mqd while stopped\n");
			goto add_queue_to_list;
		}

		if (WARN(q->process->mm != current->mm,
					"should only run in user thread"))
			retval = -EFAULT;
		else
			retval = mqd_mgr->load_mqd(mqd_mgr, q->mqd, q->pipe,
					q->queue, &q->properties, current->mm);
		if (retval)
			goto out_free_mqd;
	}

add_queue_to_list:
	list_add(&q->list, &qpd->queues_list);
	qpd->queue_count++;
	if (q->properties.is_active)
		increment_queue_count(dqm, qpd, q);

	/*
	 * Unconditionally increment this counter, regardless of the queue's
	 * type or whether the queue is active.
	 */
	dqm->total_queue_count++;
	pr_debug("Total of %d queues are accountable so far\n",
			dqm->total_queue_count);
	goto out_unlock;

out_free_mqd:
	mqd_mgr->free_mqd(mqd_mgr, q->mqd, q->mqd_mem_obj);
out_deallocate_doorbell:
	deallocate_doorbell(qpd, q);
out_deallocate_hqd:
	if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE)
		deallocate_hqd(dqm, q);
	else if (q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)
		deallocate_sdma_queue(dqm, q);
deallocate_vmid:
	if (list_empty(&qpd->queues_list))
		deallocate_vmid(dqm, qpd, q);
out_unlock:
	dqm_unlock(dqm);
	return retval;
}

static int allocate_hqd(struct device_queue_manager *dqm, struct queue *q)
{
	bool set;
	int pipe, bit, i;

	set = false;

	for (pipe = dqm->next_pipe_to_allocate, i = 0;
			i < get_pipes_per_mec(dqm);
			pipe = ((pipe + 1) % get_pipes_per_mec(dqm)), ++i) {

		if (!is_pipe_enabled(dqm, 0, pipe))
			continue;

		if (dqm->allocated_queues[pipe] != 0) {
			bit = ffs(dqm->allocated_queues[pipe]) - 1;
			dqm->allocated_queues[pipe] &= ~(1 << bit);
			q->pipe = pipe;
			q->queue = bit;
			set = true;
			break;
		}
	}

	if (!set)
		return -EBUSY;

	pr_debug("hqd slot - pipe %d, queue %d\n", q->pipe, q->queue);
	/* horizontal hqd allocation */
	dqm->next_pipe_to_allocate = (pipe + 1) % get_pipes_per_mec(dqm);

	return 0;
}

static inline void deallocate_hqd(struct device_queue_manager *dqm,
				struct queue *q)
{
	dqm->allocated_queues[q->pipe] |= (1 << q->queue);
}

#define SQ_IND_CMD_CMD_KILL		0x00000003
#define SQ_IND_CMD_MODE_BROADCAST	0x00000001

static int dbgdev_wave_reset_wavefronts(struct kfd_node *dev, struct kfd_process *p)
{
	int status = 0;
	unsigned int vmid;
	uint16_t queried_pasid;
	union SQ_CMD_BITS reg_sq_cmd;
	union GRBM_GFX_INDEX_BITS reg_gfx_index;
	struct kfd_process_device *pdd;
	int first_vmid_to_scan = dev->vm_info.first_vmid_kfd;
	int last_vmid_to_scan = dev->vm_info.last_vmid_kfd;
	uint32_t xcc_mask = dev->xcc_mask;
	int xcc_id;

	reg_sq_cmd.u32All = 0;
	reg_gfx_index.u32All = 0;

	pr_debug("Killing all process wavefronts\n");

	if (!dev->kfd2kgd->get_atc_vmid_pasid_mapping_info) {
		pr_err("no vmid pasid mapping supported \n");
		return -EOPNOTSUPP;
	}

	/* Scan all registers in the range ATC_VMID8_PASID_MAPPING ..
	 * ATC_VMID15_PASID_MAPPING
	 * to check which VMID the current process is mapped to.
	 */

	for (vmid = first_vmid_to_scan; vmid <= last_vmid_to_scan; vmid++) {
		status = dev->kfd2kgd->get_atc_vmid_pasid_mapping_info
				(dev->adev, vmid, &queried_pasid);

		if (status && queried_pasid == p->pasid) {
			pr_debug("Killing wave fronts of vmid %d and pasid 0x%x\n",
					vmid, p->pasid);
			break;
		}
	}

	if (vmid > last_vmid_to_scan) {
		pr_err("Didn't find vmid for pasid 0x%x\n", p->pasid);
		return -EFAULT;
	}

	/* taking the VMID for that process on the safe way using PDD */
	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd)
		return -EFAULT;

	reg_gfx_index.bits.sh_broadcast_writes = 1;
	reg_gfx_index.bits.se_broadcast_writes = 1;
	reg_gfx_index.bits.instance_broadcast_writes = 1;
	reg_sq_cmd.bits.mode = SQ_IND_CMD_MODE_BROADCAST;
	reg_sq_cmd.bits.cmd = SQ_IND_CMD_CMD_KILL;
	reg_sq_cmd.bits.vm_id = vmid;

	for_each_inst(xcc_id, xcc_mask)
		dev->kfd2kgd->wave_control_execute(
			dev->adev, reg_gfx_index.u32All,
			reg_sq_cmd.u32All, xcc_id);

	return 0;
}

/* Access to DQM has to be locked before calling destroy_queue_nocpsch_locked
 * to avoid asynchronized access
 */
static int destroy_queue_nocpsch_locked(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd,
				struct queue *q)
{
	int retval;
	struct mqd_manager *mqd_mgr;

	mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
			q->properties.type)];

	if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE)
		deallocate_hqd(dqm, q);
	else if (q->properties.type == KFD_QUEUE_TYPE_SDMA)
		deallocate_sdma_queue(dqm, q);
	else if (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)
		deallocate_sdma_queue(dqm, q);
	else {
		pr_debug("q->properties.type %d is invalid\n",
				q->properties.type);
		return -EINVAL;
	}
	dqm->total_queue_count--;

	deallocate_doorbell(qpd, q);

	if (!dqm->sched_running) {
		WARN_ONCE(1, "Destroy non-HWS queue while stopped\n");
		return 0;
	}

	retval = mqd_mgr->destroy_mqd(mqd_mgr, q->mqd,
				KFD_PREEMPT_TYPE_WAVEFRONT_RESET,
				KFD_UNMAP_LATENCY_MS,
				q->pipe, q->queue);
	if (retval == -ETIME)
		qpd->reset_wavefronts = true;

	list_del(&q->list);
	if (list_empty(&qpd->queues_list)) {
		if (qpd->reset_wavefronts) {
			pr_warn("Resetting wave fronts (nocpsch) on dev %p\n",
					dqm->dev);
			/* dbgdev_wave_reset_wavefronts has to be called before
			 * deallocate_vmid(), i.e. when vmid is still in use.
			 */
			dbgdev_wave_reset_wavefronts(dqm->dev,
					qpd->pqm->process);
			qpd->reset_wavefronts = false;
		}

		deallocate_vmid(dqm, qpd, q);
	}
	qpd->queue_count--;
	if (q->properties.is_active)
		decrement_queue_count(dqm, qpd, q);

	return retval;
}

static int destroy_queue_nocpsch(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd,
				struct queue *q)
{
	int retval;
	uint64_t sdma_val = 0;
	struct kfd_process_device *pdd = qpd_to_pdd(qpd);
	struct mqd_manager *mqd_mgr =
		dqm->mqd_mgrs[get_mqd_type_from_queue_type(q->properties.type)];

	/* Get the SDMA queue stats */
	if ((q->properties.type == KFD_QUEUE_TYPE_SDMA) ||
	    (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)) {
		retval = read_sdma_queue_counter((uint64_t __user *)q->properties.read_ptr,
							&sdma_val);
		if (retval)
			pr_err("Failed to read SDMA queue counter for queue: %d\n",
				q->properties.queue_id);
	}

	dqm_lock(dqm);
	retval = destroy_queue_nocpsch_locked(dqm, qpd, q);
	if (!retval)
		pdd->sdma_past_activity_counter += sdma_val;
	dqm_unlock(dqm);

	mqd_mgr->free_mqd(mqd_mgr, q->mqd, q->mqd_mem_obj);

	return retval;
}

static int update_queue(struct device_queue_manager *dqm, struct queue *q,
			struct mqd_update_info *minfo)
{
	int retval = 0;
	struct mqd_manager *mqd_mgr;
	struct kfd_process_device *pdd;
	bool prev_active = false;

	dqm_lock(dqm);
	pdd = kfd_get_process_device_data(q->device, q->process);
	if (!pdd) {
		retval = -ENODEV;
		goto out_unlock;
	}
	mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
			q->properties.type)];

	/* Save previous activity state for counters */
	prev_active = q->properties.is_active;

	/* Make sure the queue is unmapped before updating the MQD */
	if (dqm->sched_policy != KFD_SCHED_POLICY_NO_HWS) {
		if (!dqm->dev->kfd->shared_resources.enable_mes)
			retval = unmap_queues_cpsch(dqm,
						    KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0, USE_DEFAULT_GRACE_PERIOD, false);
		else if (prev_active)
			retval = remove_queue_mes(dqm, q, &pdd->qpd);

		if (retval) {
			pr_err("unmap queue failed\n");
			goto out_unlock;
		}
	} else if (prev_active &&
		   (q->properties.type == KFD_QUEUE_TYPE_COMPUTE ||
		    q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		    q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)) {

		if (!dqm->sched_running) {
			WARN_ONCE(1, "Update non-HWS queue while stopped\n");
			goto out_unlock;
		}

		retval = mqd_mgr->destroy_mqd(mqd_mgr, q->mqd,
				(dqm->dev->kfd->cwsr_enabled ?
				 KFD_PREEMPT_TYPE_WAVEFRONT_SAVE :
				 KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN),
				KFD_UNMAP_LATENCY_MS, q->pipe, q->queue);
		if (retval) {
			pr_err("destroy mqd failed\n");
			goto out_unlock;
		}
	}

	mqd_mgr->update_mqd(mqd_mgr, q->mqd, &q->properties, minfo);

	/*
	 * check active state vs. the previous state and modify
	 * counter accordingly. map_queues_cpsch uses the
	 * dqm->active_queue_count to determine whether a new runlist must be
	 * uploaded.
	 */
	if (q->properties.is_active && !prev_active) {
		increment_queue_count(dqm, &pdd->qpd, q);
	} else if (!q->properties.is_active && prev_active) {
		decrement_queue_count(dqm, &pdd->qpd, q);
	} else if (q->gws && !q->properties.is_gws) {
		if (q->properties.is_active) {
			dqm->gws_queue_count++;
			pdd->qpd.mapped_gws_queue = true;
		}
		q->properties.is_gws = true;
	} else if (!q->gws && q->properties.is_gws) {
		if (q->properties.is_active) {
			dqm->gws_queue_count--;
			pdd->qpd.mapped_gws_queue = false;
		}
		q->properties.is_gws = false;
	}

	if (dqm->sched_policy != KFD_SCHED_POLICY_NO_HWS) {
		if (!dqm->dev->kfd->shared_resources.enable_mes)
			retval = map_queues_cpsch(dqm);
		else if (q->properties.is_active)
			retval = add_queue_mes(dqm, q, &pdd->qpd);
	} else if (q->properties.is_active &&
		 (q->properties.type == KFD_QUEUE_TYPE_COMPUTE ||
		  q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		  q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)) {
		if (WARN(q->process->mm != current->mm,
			 "should only run in user thread"))
			retval = -EFAULT;
		else
			retval = mqd_mgr->load_mqd(mqd_mgr, q->mqd,
						   q->pipe, q->queue,
						   &q->properties, current->mm);
	}

out_unlock:
	dqm_unlock(dqm);
	return retval;
}

/* suspend_single_queue does not lock the dqm like the
 * evict_process_queues_cpsch or evict_process_queues_nocpsch. You should
 * lock the dqm before calling, and unlock after calling.
 *
 * The reason we don't lock the dqm is because this function may be
 * called on multiple queues in a loop, so rather than locking/unlocking
 * multiple times, we will just keep the dqm locked for all of the calls.
 */
static int suspend_single_queue(struct device_queue_manager *dqm,
				      struct kfd_process_device *pdd,
				      struct queue *q)
{
	bool is_new;

	if (q->properties.is_suspended)
		return 0;

	pr_debug("Suspending PASID %u queue [%i]\n",
			pdd->process->pasid,
			q->properties.queue_id);

	is_new = q->properties.exception_status & KFD_EC_MASK(EC_QUEUE_NEW);

	if (is_new || q->properties.is_being_destroyed) {
		pr_debug("Suspend: skip %s queue id %i\n",
				is_new ? "new" : "destroyed",
				q->properties.queue_id);
		return -EBUSY;
	}

	q->properties.is_suspended = true;
	if (q->properties.is_active) {
		if (dqm->dev->kfd->shared_resources.enable_mes) {
			int r = remove_queue_mes(dqm, q, &pdd->qpd);

			if (r)
				return r;
		}

		decrement_queue_count(dqm, &pdd->qpd, q);
		q->properties.is_active = false;
	}

	return 0;
}

/* resume_single_queue does not lock the dqm like the functions
 * restore_process_queues_cpsch or restore_process_queues_nocpsch. You should
 * lock the dqm before calling, and unlock after calling.
 *
 * The reason we don't lock the dqm is because this function may be
 * called on multiple queues in a loop, so rather than locking/unlocking
 * multiple times, we will just keep the dqm locked for all of the calls.
 */
static int resume_single_queue(struct device_queue_manager *dqm,
				      struct qcm_process_device *qpd,
				      struct queue *q)
{
	struct kfd_process_device *pdd;

	if (!q->properties.is_suspended)
		return 0;

	pdd = qpd_to_pdd(qpd);

	pr_debug("Restoring from suspend PASID %u queue [%i]\n",
			    pdd->process->pasid,
			    q->properties.queue_id);

	q->properties.is_suspended = false;

	if (QUEUE_IS_ACTIVE(q->properties)) {
		if (dqm->dev->kfd->shared_resources.enable_mes) {
			int r = add_queue_mes(dqm, q, &pdd->qpd);

			if (r)
				return r;
		}

		q->properties.is_active = true;
		increment_queue_count(dqm, qpd, q);
	}

	return 0;
}

static int evict_process_queues_nocpsch(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd)
{
	struct queue *q;
	struct mqd_manager *mqd_mgr;
	struct kfd_process_device *pdd;
	int retval, ret = 0;

	dqm_lock(dqm);
	if (qpd->evicted++ > 0) /* already evicted, do nothing */
		goto out;

	pdd = qpd_to_pdd(qpd);
	pr_debug_ratelimited("Evicting PASID 0x%x queues\n",
			    pdd->process->pasid);

	pdd->last_evict_timestamp = get_jiffies_64();
	/* Mark all queues as evicted. Deactivate all active queues on
	 * the qpd.
	 */
	list_for_each_entry(q, &qpd->queues_list, list) {
		q->properties.is_evicted = true;
		if (!q->properties.is_active)
			continue;

		mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
				q->properties.type)];
		q->properties.is_active = false;
		decrement_queue_count(dqm, qpd, q);

		if (WARN_ONCE(!dqm->sched_running, "Evict when stopped\n"))
			continue;

		retval = mqd_mgr->destroy_mqd(mqd_mgr, q->mqd,
				(dqm->dev->kfd->cwsr_enabled ?
				 KFD_PREEMPT_TYPE_WAVEFRONT_SAVE :
				 KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN),
				KFD_UNMAP_LATENCY_MS, q->pipe, q->queue);
		if (retval && !ret)
			/* Return the first error, but keep going to
			 * maintain a consistent eviction state
			 */
			ret = retval;
	}

out:
	dqm_unlock(dqm);
	return ret;
}

static int evict_process_queues_cpsch(struct device_queue_manager *dqm,
				      struct qcm_process_device *qpd)
{
	struct queue *q;
	struct kfd_process_device *pdd;
	int retval = 0;

	dqm_lock(dqm);
	if (qpd->evicted++ > 0) /* already evicted, do nothing */
		goto out;

	pdd = qpd_to_pdd(qpd);

	/* The debugger creates processes that temporarily have not acquired
	 * all VMs for all devices and has no VMs itself.
	 * Skip queue eviction on process eviction.
	 */
	if (!pdd->drm_priv)
		goto out;

	pr_debug_ratelimited("Evicting PASID 0x%x queues\n",
			    pdd->process->pasid);

	/* Mark all queues as evicted. Deactivate all active queues on
	 * the qpd.
	 */
	list_for_each_entry(q, &qpd->queues_list, list) {
		q->properties.is_evicted = true;
		if (!q->properties.is_active)
			continue;

		q->properties.is_active = false;
		decrement_queue_count(dqm, qpd, q);

		if (dqm->dev->kfd->shared_resources.enable_mes) {
			retval = remove_queue_mes(dqm, q, qpd);
			if (retval) {
				pr_err("Failed to evict queue %d\n",
					q->properties.queue_id);
				goto out;
			}
		}
	}
	pdd->last_evict_timestamp = get_jiffies_64();
	if (!dqm->dev->kfd->shared_resources.enable_mes)
		retval = execute_queues_cpsch(dqm,
					      qpd->is_debug ?
					      KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES :
					      KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0,
					      USE_DEFAULT_GRACE_PERIOD);

out:
	dqm_unlock(dqm);
	return retval;
}

static int restore_process_queues_nocpsch(struct device_queue_manager *dqm,
					  struct qcm_process_device *qpd)
{
	struct mm_struct *mm = NULL;
	struct queue *q;
	struct mqd_manager *mqd_mgr;
	struct kfd_process_device *pdd;
	uint64_t pd_base;
	uint64_t eviction_duration;
	int retval, ret = 0;

	pdd = qpd_to_pdd(qpd);
	/* Retrieve PD base */
	pd_base = amdgpu_amdkfd_gpuvm_get_process_page_dir(pdd->drm_priv);

	dqm_lock(dqm);
	if (WARN_ON_ONCE(!qpd->evicted)) /* already restored, do nothing */
		goto out;
	if (qpd->evicted > 1) { /* ref count still > 0, decrement & quit */
		qpd->evicted--;
		goto out;
	}

	pr_debug_ratelimited("Restoring PASID 0x%x queues\n",
			    pdd->process->pasid);

	/* Update PD Base in QPD */
	qpd->page_table_base = pd_base;
	pr_debug("Updated PD address to 0x%llx\n", pd_base);

	if (!list_empty(&qpd->queues_list)) {
		dqm->dev->kfd2kgd->set_vm_context_page_table_base(
				dqm->dev->adev,
				qpd->vmid,
				qpd->page_table_base);
		kfd_flush_tlb(pdd, TLB_FLUSH_LEGACY);
	}

	/* Take a safe reference to the mm_struct, which may otherwise
	 * disappear even while the kfd_process is still referenced.
	 */
	mm = get_task_mm(pdd->process->lead_thread);
	if (!mm) {
		ret = -EFAULT;
		goto out;
	}

	/* Remove the eviction flags. Activate queues that are not
	 * inactive for other reasons.
	 */
	list_for_each_entry(q, &qpd->queues_list, list) {
		q->properties.is_evicted = false;
		if (!QUEUE_IS_ACTIVE(q->properties))
			continue;

		mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
				q->properties.type)];
		q->properties.is_active = true;
		increment_queue_count(dqm, qpd, q);

		if (WARN_ONCE(!dqm->sched_running, "Restore when stopped\n"))
			continue;

		retval = mqd_mgr->load_mqd(mqd_mgr, q->mqd, q->pipe,
				       q->queue, &q->properties, mm);
		if (retval && !ret)
			/* Return the first error, but keep going to
			 * maintain a consistent eviction state
			 */
			ret = retval;
	}
	qpd->evicted = 0;
	eviction_duration = get_jiffies_64() - pdd->last_evict_timestamp;
	atomic64_add(eviction_duration, &pdd->evict_duration_counter);
out:
	if (mm)
		mmput(mm);
	dqm_unlock(dqm);
	return ret;
}

static int restore_process_queues_cpsch(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd)
{
	struct queue *q;
	struct kfd_process_device *pdd;
	uint64_t eviction_duration;
	int retval = 0;

	pdd = qpd_to_pdd(qpd);

	dqm_lock(dqm);
	if (WARN_ON_ONCE(!qpd->evicted)) /* already restored, do nothing */
		goto out;
	if (qpd->evicted > 1) { /* ref count still > 0, decrement & quit */
		qpd->evicted--;
		goto out;
	}

	/* The debugger creates processes that temporarily have not acquired
	 * all VMs for all devices and has no VMs itself.
	 * Skip queue restore on process restore.
	 */
	if (!pdd->drm_priv)
		goto vm_not_acquired;

	pr_debug_ratelimited("Restoring PASID 0x%x queues\n",
			    pdd->process->pasid);

	/* Update PD Base in QPD */
	qpd->page_table_base = amdgpu_amdkfd_gpuvm_get_process_page_dir(pdd->drm_priv);
	pr_debug("Updated PD address to 0x%llx\n", qpd->page_table_base);

	/* activate all active queues on the qpd */
	list_for_each_entry(q, &qpd->queues_list, list) {
		q->properties.is_evicted = false;
		if (!QUEUE_IS_ACTIVE(q->properties))
			continue;

		q->properties.is_active = true;
		increment_queue_count(dqm, &pdd->qpd, q);

		if (dqm->dev->kfd->shared_resources.enable_mes) {
			retval = add_queue_mes(dqm, q, qpd);
			if (retval) {
				pr_err("Failed to restore queue %d\n",
					q->properties.queue_id);
				goto out;
			}
		}
	}
	if (!dqm->dev->kfd->shared_resources.enable_mes)
		retval = execute_queues_cpsch(dqm,
					      KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0, USE_DEFAULT_GRACE_PERIOD);
	eviction_duration = get_jiffies_64() - pdd->last_evict_timestamp;
	atomic64_add(eviction_duration, &pdd->evict_duration_counter);
vm_not_acquired:
	qpd->evicted = 0;
out:
	dqm_unlock(dqm);
	return retval;
}

static int register_process(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd)
{
	struct device_process_node *n;
	struct kfd_process_device *pdd;
	uint64_t pd_base;
	int retval;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	n->qpd = qpd;

	pdd = qpd_to_pdd(qpd);
	/* Retrieve PD base */
	pd_base = amdgpu_amdkfd_gpuvm_get_process_page_dir(pdd->drm_priv);

	dqm_lock(dqm);
	list_add(&n->list, &dqm->queues);

	/* Update PD Base in QPD */
	qpd->page_table_base = pd_base;
	pr_debug("Updated PD address to 0x%llx\n", pd_base);

	retval = dqm->asic_ops.update_qpd(dqm, qpd);

	dqm->processes_count++;

	dqm_unlock(dqm);

	/* Outside the DQM lock because under the DQM lock we can't do
	 * reclaim or take other locks that others hold while reclaiming.
	 */
	kfd_inc_compute_active(dqm->dev);

	return retval;
}

static int unregister_process(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd)
{
	int retval;
	struct device_process_node *cur, *next;

	pr_debug("qpd->queues_list is %s\n",
			list_empty(&qpd->queues_list) ? "empty" : "not empty");

	retval = 0;
	dqm_lock(dqm);

	list_for_each_entry_safe(cur, next, &dqm->queues, list) {
		if (qpd == cur->qpd) {
			list_del(&cur->list);
			kfree(cur);
			dqm->processes_count--;
			goto out;
		}
	}
	/* qpd not found in dqm list */
	retval = 1;
out:
	dqm_unlock(dqm);

	/* Outside the DQM lock because under the DQM lock we can't do
	 * reclaim or take other locks that others hold while reclaiming.
	 */
	if (!retval)
		kfd_dec_compute_active(dqm->dev);

	return retval;
}

static int
set_pasid_vmid_mapping(struct device_queue_manager *dqm, u32 pasid,
			unsigned int vmid)
{
	uint32_t xcc_mask = dqm->dev->xcc_mask;
	int xcc_id, ret;

	for_each_inst(xcc_id, xcc_mask) {
		ret = dqm->dev->kfd2kgd->set_pasid_vmid_mapping(
			dqm->dev->adev, pasid, vmid, xcc_id);
		if (ret)
			break;
	}

	return ret;
}

static void init_interrupts(struct device_queue_manager *dqm)
{
	uint32_t xcc_mask = dqm->dev->xcc_mask;
	unsigned int i, xcc_id;

	for_each_inst(xcc_id, xcc_mask) {
		for (i = 0 ; i < get_pipes_per_mec(dqm) ; i++) {
			if (is_pipe_enabled(dqm, 0, i)) {
				dqm->dev->kfd2kgd->init_interrupts(
					dqm->dev->adev, i, xcc_id);
			}
		}
	}
}

static int initialize_nocpsch(struct device_queue_manager *dqm)
{
	int pipe, queue;

	pr_debug("num of pipes: %d\n", get_pipes_per_mec(dqm));

	dqm->allocated_queues = kcalloc(get_pipes_per_mec(dqm),
					sizeof(unsigned int), GFP_KERNEL);
	if (!dqm->allocated_queues)
		return -ENOMEM;

	mutex_init(&dqm->lock_hidden);
	INIT_LIST_HEAD(&dqm->queues);
	dqm->active_queue_count = dqm->next_pipe_to_allocate = 0;
	dqm->active_cp_queue_count = 0;
	dqm->gws_queue_count = 0;

	for (pipe = 0; pipe < get_pipes_per_mec(dqm); pipe++) {
		int pipe_offset = pipe * get_queues_per_pipe(dqm);

		for (queue = 0; queue < get_queues_per_pipe(dqm); queue++)
			if (test_bit(pipe_offset + queue,
				     dqm->dev->kfd->shared_resources.cp_queue_bitmap))
				dqm->allocated_queues[pipe] |= 1 << queue;
	}

	memset(dqm->vmid_pasid, 0, sizeof(dqm->vmid_pasid));

	init_sdma_bitmaps(dqm);

	return 0;
}

static void uninitialize(struct device_queue_manager *dqm)
{
	int i;

	WARN_ON(dqm->active_queue_count > 0 || dqm->processes_count > 0);

	kfree(dqm->allocated_queues);
	for (i = 0 ; i < KFD_MQD_TYPE_MAX ; i++)
		kfree(dqm->mqd_mgrs[i]);
	mutex_destroy(&dqm->lock_hidden);
}

static int start_nocpsch(struct device_queue_manager *dqm)
{
	int r = 0;

	pr_info("SW scheduler is used");
	init_interrupts(dqm);

	if (dqm->dev->adev->asic_type == CHIP_HAWAII)
		r = pm_init(&dqm->packet_mgr, dqm);
	if (!r)
		dqm->sched_running = true;

	return r;
}

static int stop_nocpsch(struct device_queue_manager *dqm)
{
	dqm_lock(dqm);
	if (!dqm->sched_running) {
		dqm_unlock(dqm);
		return 0;
	}

	if (dqm->dev->adev->asic_type == CHIP_HAWAII)
		pm_uninit(&dqm->packet_mgr, false);
	dqm->sched_running = false;
	dqm_unlock(dqm);

	return 0;
}

static void pre_reset(struct device_queue_manager *dqm)
{
	dqm_lock(dqm);
	dqm->is_resetting = true;
	dqm_unlock(dqm);
}

static int allocate_sdma_queue(struct device_queue_manager *dqm,
				struct queue *q, const uint32_t *restore_sdma_id)
{
	int bit;

	if (q->properties.type == KFD_QUEUE_TYPE_SDMA) {
		if (bitmap_empty(dqm->sdma_bitmap, KFD_MAX_SDMA_QUEUES)) {
			pr_err("No more SDMA queue to allocate\n");
			return -ENOMEM;
		}

		if (restore_sdma_id) {
			/* Re-use existing sdma_id */
			if (!test_bit(*restore_sdma_id, dqm->sdma_bitmap)) {
				pr_err("SDMA queue already in use\n");
				return -EBUSY;
			}
			clear_bit(*restore_sdma_id, dqm->sdma_bitmap);
			q->sdma_id = *restore_sdma_id;
		} else {
			/* Find first available sdma_id */
			bit = find_first_bit(dqm->sdma_bitmap,
					     get_num_sdma_queues(dqm));
			clear_bit(bit, dqm->sdma_bitmap);
			q->sdma_id = bit;
		}

		q->properties.sdma_engine_id =
			q->sdma_id % kfd_get_num_sdma_engines(dqm->dev);
		q->properties.sdma_queue_id = q->sdma_id /
				kfd_get_num_sdma_engines(dqm->dev);
	} else if (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
		if (bitmap_empty(dqm->xgmi_sdma_bitmap, KFD_MAX_SDMA_QUEUES)) {
			pr_err("No more XGMI SDMA queue to allocate\n");
			return -ENOMEM;
		}
		if (restore_sdma_id) {
			/* Re-use existing sdma_id */
			if (!test_bit(*restore_sdma_id, dqm->xgmi_sdma_bitmap)) {
				pr_err("SDMA queue already in use\n");
				return -EBUSY;
			}
			clear_bit(*restore_sdma_id, dqm->xgmi_sdma_bitmap);
			q->sdma_id = *restore_sdma_id;
		} else {
			bit = find_first_bit(dqm->xgmi_sdma_bitmap,
					     get_num_xgmi_sdma_queues(dqm));
			clear_bit(bit, dqm->xgmi_sdma_bitmap);
			q->sdma_id = bit;
		}
		/* sdma_engine_id is sdma id including
		 * both PCIe-optimized SDMAs and XGMI-
		 * optimized SDMAs. The calculation below
		 * assumes the first N engines are always
		 * PCIe-optimized ones
		 */
		q->properties.sdma_engine_id =
			kfd_get_num_sdma_engines(dqm->dev) +
			q->sdma_id % kfd_get_num_xgmi_sdma_engines(dqm->dev);
		q->properties.sdma_queue_id = q->sdma_id /
			kfd_get_num_xgmi_sdma_engines(dqm->dev);
	}

	pr_debug("SDMA engine id: %d\n", q->properties.sdma_engine_id);
	pr_debug("SDMA queue id: %d\n", q->properties.sdma_queue_id);

	return 0;
}

static void deallocate_sdma_queue(struct device_queue_manager *dqm,
				struct queue *q)
{
	if (q->properties.type == KFD_QUEUE_TYPE_SDMA) {
		if (q->sdma_id >= get_num_sdma_queues(dqm))
			return;
		set_bit(q->sdma_id, dqm->sdma_bitmap);
	} else if (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
		if (q->sdma_id >= get_num_xgmi_sdma_queues(dqm))
			return;
		set_bit(q->sdma_id, dqm->xgmi_sdma_bitmap);
	}
}

/*
 * Device Queue Manager implementation for cp scheduler
 */

static int set_sched_resources(struct device_queue_manager *dqm)
{
	int i, mec;
	struct scheduling_resources res;

	res.vmid_mask = dqm->dev->compute_vmid_bitmap;

	res.queue_mask = 0;
	for (i = 0; i < KGD_MAX_QUEUES; ++i) {
		mec = (i / dqm->dev->kfd->shared_resources.num_queue_per_pipe)
			/ dqm->dev->kfd->shared_resources.num_pipe_per_mec;

		if (!test_bit(i, dqm->dev->kfd->shared_resources.cp_queue_bitmap))
			continue;

		/* only acquire queues from the first MEC */
		if (mec > 0)
			continue;

		/* This situation may be hit in the future if a new HW
		 * generation exposes more than 64 queues. If so, the
		 * definition of res.queue_mask needs updating
		 */
		if (WARN_ON(i >= (sizeof(res.queue_mask)*8))) {
			pr_err("Invalid queue enabled by amdgpu: %d\n", i);
			break;
		}

		res.queue_mask |= 1ull
			<< amdgpu_queue_mask_bit_to_set_resource_bit(
				dqm->dev->adev, i);
	}
	res.gws_mask = ~0ull;
	res.oac_mask = res.gds_heap_base = res.gds_heap_size = 0;

	pr_debug("Scheduling resources:\n"
			"vmid mask: 0x%8X\n"
			"queue mask: 0x%8llX\n",
			res.vmid_mask, res.queue_mask);

	return pm_send_set_resources(&dqm->packet_mgr, &res);
}

static int initialize_cpsch(struct device_queue_manager *dqm)
{
	pr_debug("num of pipes: %d\n", get_pipes_per_mec(dqm));

	mutex_init(&dqm->lock_hidden);
	INIT_LIST_HEAD(&dqm->queues);
	dqm->active_queue_count = dqm->processes_count = 0;
	dqm->active_cp_queue_count = 0;
	dqm->gws_queue_count = 0;
	dqm->active_runlist = false;
	INIT_WORK(&dqm->hw_exception_work, kfd_process_hw_exception);
	dqm->trap_debug_vmid = 0;

	init_sdma_bitmaps(dqm);

	if (dqm->dev->kfd2kgd->get_iq_wait_times)
		dqm->dev->kfd2kgd->get_iq_wait_times(dqm->dev->adev,
					&dqm->wait_times);
	return 0;
}

static int start_cpsch(struct device_queue_manager *dqm)
{
	int retval;

	retval = 0;

	dqm_lock(dqm);

	if (!dqm->dev->kfd->shared_resources.enable_mes) {
		retval = pm_init(&dqm->packet_mgr, dqm);
		if (retval)
			goto fail_packet_manager_init;

		retval = set_sched_resources(dqm);
		if (retval)
			goto fail_set_sched_resources;
	}
	pr_debug("Allocating fence memory\n");

	/* allocate fence memory on the gart */
	retval = kfd_gtt_sa_allocate(dqm->dev, sizeof(*dqm->fence_addr),
					&dqm->fence_mem);

	if (retval)
		goto fail_allocate_vidmem;

	dqm->fence_addr = (uint64_t *)dqm->fence_mem->cpu_ptr;
	dqm->fence_gpu_addr = dqm->fence_mem->gpu_addr;

	init_interrupts(dqm);

	/* clear hang status when driver try to start the hw scheduler */
	dqm->is_hws_hang = false;
	dqm->is_resetting = false;
	dqm->sched_running = true;

	if (!dqm->dev->kfd->shared_resources.enable_mes)
		execute_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0, USE_DEFAULT_GRACE_PERIOD);
	dqm_unlock(dqm);

	return 0;
fail_allocate_vidmem:
fail_set_sched_resources:
	if (!dqm->dev->kfd->shared_resources.enable_mes)
		pm_uninit(&dqm->packet_mgr, false);
fail_packet_manager_init:
	dqm_unlock(dqm);
	return retval;
}

static int stop_cpsch(struct device_queue_manager *dqm)
{
	bool hanging;

	dqm_lock(dqm);
	if (!dqm->sched_running) {
		dqm_unlock(dqm);
		return 0;
	}

	if (!dqm->is_hws_hang) {
		if (!dqm->dev->kfd->shared_resources.enable_mes)
			unmap_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES, 0, USE_DEFAULT_GRACE_PERIOD, false);
		else
			remove_all_queues_mes(dqm);
	}

	hanging = dqm->is_hws_hang || dqm->is_resetting;
	dqm->sched_running = false;

	if (!dqm->dev->kfd->shared_resources.enable_mes)
		pm_release_ib(&dqm->packet_mgr);

	kfd_gtt_sa_free(dqm->dev, dqm->fence_mem);
	if (!dqm->dev->kfd->shared_resources.enable_mes)
		pm_uninit(&dqm->packet_mgr, hanging);
	dqm_unlock(dqm);

	return 0;
}

static int create_kernel_queue_cpsch(struct device_queue_manager *dqm,
					struct kernel_queue *kq,
					struct qcm_process_device *qpd)
{
	dqm_lock(dqm);
	if (dqm->total_queue_count >= max_num_of_queues_per_device) {
		pr_warn("Can't create new kernel queue because %d queues were already created\n",
				dqm->total_queue_count);
		dqm_unlock(dqm);
		return -EPERM;
	}

	/*
	 * Unconditionally increment this counter, regardless of the queue's
	 * type or whether the queue is active.
	 */
	dqm->total_queue_count++;
	pr_debug("Total of %d queues are accountable so far\n",
			dqm->total_queue_count);

	list_add(&kq->list, &qpd->priv_queue_list);
	increment_queue_count(dqm, qpd, kq->queue);
	qpd->is_debug = true;
	execute_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0,
			USE_DEFAULT_GRACE_PERIOD);
	dqm_unlock(dqm);

	return 0;
}

static void destroy_kernel_queue_cpsch(struct device_queue_manager *dqm,
					struct kernel_queue *kq,
					struct qcm_process_device *qpd)
{
	dqm_lock(dqm);
	list_del(&kq->list);
	decrement_queue_count(dqm, qpd, kq->queue);
	qpd->is_debug = false;
	execute_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES, 0,
			USE_DEFAULT_GRACE_PERIOD);
	/*
	 * Unconditionally decrement this counter, regardless of the queue's
	 * type.
	 */
	dqm->total_queue_count--;
	pr_debug("Total of %d queues are accountable so far\n",
			dqm->total_queue_count);
	dqm_unlock(dqm);
}

static int create_queue_cpsch(struct device_queue_manager *dqm, struct queue *q,
			struct qcm_process_device *qpd,
			const struct kfd_criu_queue_priv_data *qd,
			const void *restore_mqd, const void *restore_ctl_stack)
{
	int retval;
	struct mqd_manager *mqd_mgr;

	if (dqm->total_queue_count >= max_num_of_queues_per_device) {
		pr_warn("Can't create new usermode queue because %d queues were already created\n",
				dqm->total_queue_count);
		retval = -EPERM;
		goto out;
	}

	if (q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
		dqm_lock(dqm);
		retval = allocate_sdma_queue(dqm, q, qd ? &qd->sdma_id : NULL);
		dqm_unlock(dqm);
		if (retval)
			goto out;
	}

	retval = allocate_doorbell(qpd, q, qd ? &qd->doorbell_id : NULL);
	if (retval)
		goto out_deallocate_sdma_queue;

	mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
			q->properties.type)];

	if (q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)
		dqm->asic_ops.init_sdma_vm(dqm, q, qpd);
	q->properties.tba_addr = qpd->tba_addr;
	q->properties.tma_addr = qpd->tma_addr;
	q->mqd_mem_obj = mqd_mgr->allocate_mqd(mqd_mgr->dev, &q->properties);
	if (!q->mqd_mem_obj) {
		retval = -ENOMEM;
		goto out_deallocate_doorbell;
	}

	dqm_lock(dqm);
	/*
	 * Eviction state logic: mark all queues as evicted, even ones
	 * not currently active. Restoring inactive queues later only
	 * updates the is_evicted flag but is a no-op otherwise.
	 */
	q->properties.is_evicted = !!qpd->evicted;
	q->properties.is_dbg_wa = qpd->pqm->process->debug_trap_enabled &&
			KFD_GC_VERSION(q->device) >= IP_VERSION(11, 0, 0) &&
			KFD_GC_VERSION(q->device) <= IP_VERSION(11, 0, 3);

	if (qd)
		mqd_mgr->restore_mqd(mqd_mgr, &q->mqd, q->mqd_mem_obj, &q->gart_mqd_addr,
				     &q->properties, restore_mqd, restore_ctl_stack,
				     qd->ctl_stack_size);
	else
		mqd_mgr->init_mqd(mqd_mgr, &q->mqd, q->mqd_mem_obj,
					&q->gart_mqd_addr, &q->properties);

	list_add(&q->list, &qpd->queues_list);
	qpd->queue_count++;

	if (q->properties.is_active) {
		increment_queue_count(dqm, qpd, q);

		if (!dqm->dev->kfd->shared_resources.enable_mes)
			retval = execute_queues_cpsch(dqm,
					KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0, USE_DEFAULT_GRACE_PERIOD);
		else
			retval = add_queue_mes(dqm, q, qpd);
		if (retval)
			goto cleanup_queue;
	}

	/*
	 * Unconditionally increment this counter, regardless of the queue's
	 * type or whether the queue is active.
	 */
	dqm->total_queue_count++;

	pr_debug("Total of %d queues are accountable so far\n",
			dqm->total_queue_count);

	dqm_unlock(dqm);
	return retval;

cleanup_queue:
	qpd->queue_count--;
	list_del(&q->list);
	if (q->properties.is_active)
		decrement_queue_count(dqm, qpd, q);
	mqd_mgr->free_mqd(mqd_mgr, q->mqd, q->mqd_mem_obj);
	dqm_unlock(dqm);
out_deallocate_doorbell:
	deallocate_doorbell(qpd, q);
out_deallocate_sdma_queue:
	if (q->properties.type == KFD_QUEUE_TYPE_SDMA ||
		q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
		dqm_lock(dqm);
		deallocate_sdma_queue(dqm, q);
		dqm_unlock(dqm);
	}
out:
	return retval;
}

int amdkfd_fence_wait_timeout(uint64_t *fence_addr,
				uint64_t fence_value,
				unsigned int timeout_ms)
{
	unsigned long end_jiffies = msecs_to_jiffies(timeout_ms) + jiffies;

	while (*fence_addr != fence_value) {
		if (time_after(jiffies, end_jiffies)) {
			pr_err("qcm fence wait loop timeout expired\n");
			/* In HWS case, this is used to halt the driver thread
			 * in order not to mess up CP states before doing
			 * scandumps for FW debugging.
			 */
			while (halt_if_hws_hang)
				schedule();

			return -ETIME;
		}
		schedule();
	}

	return 0;
}

/* dqm->lock mutex has to be locked before calling this function */
static int map_queues_cpsch(struct device_queue_manager *dqm)
{
	int retval;

	if (!dqm->sched_running)
		return 0;
	if (dqm->active_queue_count <= 0 || dqm->processes_count <= 0)
		return 0;
	if (dqm->active_runlist)
		return 0;

	retval = pm_send_runlist(&dqm->packet_mgr, &dqm->queues);
	pr_debug("%s sent runlist\n", __func__);
	if (retval) {
		pr_err("failed to execute runlist\n");
		return retval;
	}
	dqm->active_runlist = true;

	return retval;
}

/* dqm->lock mutex has to be locked before calling this function */
static int unmap_queues_cpsch(struct device_queue_manager *dqm,
				enum kfd_unmap_queues_filter filter,
				uint32_t filter_param,
				uint32_t grace_period,
				bool reset)
{
	int retval = 0;
	struct mqd_manager *mqd_mgr;

	if (!dqm->sched_running)
		return 0;
	if (dqm->is_hws_hang || dqm->is_resetting)
		return -EIO;
	if (!dqm->active_runlist)
		return retval;

	if (grace_period != USE_DEFAULT_GRACE_PERIOD) {
		retval = pm_update_grace_period(&dqm->packet_mgr, grace_period);
		if (retval)
			return retval;
	}

	retval = pm_send_unmap_queue(&dqm->packet_mgr, filter, filter_param, reset);
	if (retval)
		return retval;

	*dqm->fence_addr = KFD_FENCE_INIT;
	pm_send_query_status(&dqm->packet_mgr, dqm->fence_gpu_addr,
				KFD_FENCE_COMPLETED);
	/* should be timed out */
	retval = amdkfd_fence_wait_timeout(dqm->fence_addr, KFD_FENCE_COMPLETED,
				queue_preemption_timeout_ms);
	if (retval) {
		pr_err("The cp might be in an unrecoverable state due to an unsuccessful queues preemption\n");
		kfd_hws_hang(dqm);
		return retval;
	}

	/* In the current MEC firmware implementation, if compute queue
	 * doesn't response to the preemption request in time, HIQ will
	 * abandon the unmap request without returning any timeout error
	 * to driver. Instead, MEC firmware will log the doorbell of the
	 * unresponding compute queue to HIQ.MQD.queue_doorbell_id fields.
	 * To make sure the queue unmap was successful, driver need to
	 * check those fields
	 */
	mqd_mgr = dqm->mqd_mgrs[KFD_MQD_TYPE_HIQ];
	if (mqd_mgr->read_doorbell_id(dqm->packet_mgr.priv_queue->queue->mqd)) {
		pr_err("HIQ MQD's queue_doorbell_id0 is not 0, Queue preemption time out\n");
		while (halt_if_hws_hang)
			schedule();
		return -ETIME;
	}

	/* We need to reset the grace period value for this device */
	if (grace_period != USE_DEFAULT_GRACE_PERIOD) {
		if (pm_update_grace_period(&dqm->packet_mgr,
					USE_DEFAULT_GRACE_PERIOD))
			pr_err("Failed to reset grace period\n");
	}

	pm_release_ib(&dqm->packet_mgr);
	dqm->active_runlist = false;

	return retval;
}

/* only for compute queue */
static int reset_queues_cpsch(struct device_queue_manager *dqm,
			uint16_t pasid)
{
	int retval;

	dqm_lock(dqm);

	retval = unmap_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_BY_PASID,
			pasid, USE_DEFAULT_GRACE_PERIOD, true);

	dqm_unlock(dqm);
	return retval;
}

/* dqm->lock mutex has to be locked before calling this function */
static int execute_queues_cpsch(struct device_queue_manager *dqm,
				enum kfd_unmap_queues_filter filter,
				uint32_t filter_param,
				uint32_t grace_period)
{
	int retval;

	if (dqm->is_hws_hang)
		return -EIO;
	retval = unmap_queues_cpsch(dqm, filter, filter_param, grace_period, false);
	if (retval)
		return retval;

	return map_queues_cpsch(dqm);
}

static int wait_on_destroy_queue(struct device_queue_manager *dqm,
				 struct queue *q)
{
	struct kfd_process_device *pdd = kfd_get_process_device_data(q->device,
								q->process);
	int ret = 0;

	if (pdd->qpd.is_debug)
		return ret;

	q->properties.is_being_destroyed = true;

	if (pdd->process->debug_trap_enabled && q->properties.is_suspended) {
		dqm_unlock(dqm);
		mutex_unlock(&q->process->mutex);
		ret = wait_event_interruptible(dqm->destroy_wait,
						!q->properties.is_suspended);

		mutex_lock(&q->process->mutex);
		dqm_lock(dqm);
	}

	return ret;
}

static int destroy_queue_cpsch(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd,
				struct queue *q)
{
	int retval;
	struct mqd_manager *mqd_mgr;
	uint64_t sdma_val = 0;
	struct kfd_process_device *pdd = qpd_to_pdd(qpd);

	/* Get the SDMA queue stats */
	if ((q->properties.type == KFD_QUEUE_TYPE_SDMA) ||
	    (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)) {
		retval = read_sdma_queue_counter((uint64_t __user *)q->properties.read_ptr,
							&sdma_val);
		if (retval)
			pr_err("Failed to read SDMA queue counter for queue: %d\n",
				q->properties.queue_id);
	}

	/* remove queue from list to prevent rescheduling after preemption */
	dqm_lock(dqm);

	retval = wait_on_destroy_queue(dqm, q);

	if (retval) {
		dqm_unlock(dqm);
		return retval;
	}

	if (qpd->is_debug) {
		/*
		 * error, currently we do not allow to destroy a queue
		 * of a currently debugged process
		 */
		retval = -EBUSY;
		goto failed_try_destroy_debugged_queue;

	}

	mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
			q->properties.type)];

	deallocate_doorbell(qpd, q);

	if ((q->properties.type == KFD_QUEUE_TYPE_SDMA) ||
	    (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)) {
		deallocate_sdma_queue(dqm, q);
		pdd->sdma_past_activity_counter += sdma_val;
	}

	list_del(&q->list);
	qpd->queue_count--;
	if (q->properties.is_active) {
		decrement_queue_count(dqm, qpd, q);
		if (!dqm->dev->kfd->shared_resources.enable_mes) {
			retval = execute_queues_cpsch(dqm,
						      KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0,
						      USE_DEFAULT_GRACE_PERIOD);
			if (retval == -ETIME)
				qpd->reset_wavefronts = true;
		} else {
			retval = remove_queue_mes(dqm, q, qpd);
		}
	}

	/*
	 * Unconditionally decrement this counter, regardless of the queue's
	 * type
	 */
	dqm->total_queue_count--;
	pr_debug("Total of %d queues are accountable so far\n",
			dqm->total_queue_count);

	dqm_unlock(dqm);

	/*
	 * Do free_mqd and raise delete event after dqm_unlock(dqm) to avoid
	 * circular locking
	 */
	kfd_dbg_ev_raise(KFD_EC_MASK(EC_DEVICE_QUEUE_DELETE),
				qpd->pqm->process, q->device,
				-1, false, NULL, 0);

	mqd_mgr->free_mqd(mqd_mgr, q->mqd, q->mqd_mem_obj);

	return retval;

failed_try_destroy_debugged_queue:

	dqm_unlock(dqm);
	return retval;
}

/*
 * Low bits must be 0000/FFFF as required by HW, high bits must be 0 to
 * stay in user mode.
 */
#define APE1_FIXED_BITS_MASK 0xFFFF80000000FFFFULL
/* APE1 limit is inclusive and 64K aligned. */
#define APE1_LIMIT_ALIGNMENT 0xFFFF

static bool set_cache_memory_policy(struct device_queue_manager *dqm,
				   struct qcm_process_device *qpd,
				   enum cache_policy default_policy,
				   enum cache_policy alternate_policy,
				   void __user *alternate_aperture_base,
				   uint64_t alternate_aperture_size)
{
	bool retval = true;

	if (!dqm->asic_ops.set_cache_memory_policy)
		return retval;

	dqm_lock(dqm);

	if (alternate_aperture_size == 0) {
		/* base > limit disables APE1 */
		qpd->sh_mem_ape1_base = 1;
		qpd->sh_mem_ape1_limit = 0;
	} else {
		/*
		 * In FSA64, APE1_Base[63:0] = { 16{SH_MEM_APE1_BASE[31]},
		 *			SH_MEM_APE1_BASE[31:0], 0x0000 }
		 * APE1_Limit[63:0] = { 16{SH_MEM_APE1_LIMIT[31]},
		 *			SH_MEM_APE1_LIMIT[31:0], 0xFFFF }
		 * Verify that the base and size parameters can be
		 * represented in this format and convert them.
		 * Additionally restrict APE1 to user-mode addresses.
		 */

		uint64_t base = (uintptr_t)alternate_aperture_base;
		uint64_t limit = base + alternate_aperture_size - 1;

		if (limit <= base || (base & APE1_FIXED_BITS_MASK) != 0 ||
		   (limit & APE1_FIXED_BITS_MASK) != APE1_LIMIT_ALIGNMENT) {
			retval = false;
			goto out;
		}

		qpd->sh_mem_ape1_base = base >> 16;
		qpd->sh_mem_ape1_limit = limit >> 16;
	}

	retval = dqm->asic_ops.set_cache_memory_policy(
			dqm,
			qpd,
			default_policy,
			alternate_policy,
			alternate_aperture_base,
			alternate_aperture_size);

	if ((dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) && (qpd->vmid != 0))
		program_sh_mem_settings(dqm, qpd);

	pr_debug("sh_mem_config: 0x%x, ape1_base: 0x%x, ape1_limit: 0x%x\n",
		qpd->sh_mem_config, qpd->sh_mem_ape1_base,
		qpd->sh_mem_ape1_limit);

out:
	dqm_unlock(dqm);
	return retval;
}

static int process_termination_nocpsch(struct device_queue_manager *dqm,
		struct qcm_process_device *qpd)
{
	struct queue *q;
	struct device_process_node *cur, *next_dpn;
	int retval = 0;
	bool found = false;

	dqm_lock(dqm);

	/* Clear all user mode queues */
	while (!list_empty(&qpd->queues_list)) {
		struct mqd_manager *mqd_mgr;
		int ret;

		q = list_first_entry(&qpd->queues_list, struct queue, list);
		mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
				q->properties.type)];
		ret = destroy_queue_nocpsch_locked(dqm, qpd, q);
		if (ret)
			retval = ret;
		dqm_unlock(dqm);
		mqd_mgr->free_mqd(mqd_mgr, q->mqd, q->mqd_mem_obj);
		dqm_lock(dqm);
	}

	/* Unregister process */
	list_for_each_entry_safe(cur, next_dpn, &dqm->queues, list) {
		if (qpd == cur->qpd) {
			list_del(&cur->list);
			kfree(cur);
			dqm->processes_count--;
			found = true;
			break;
		}
	}

	dqm_unlock(dqm);

	/* Outside the DQM lock because under the DQM lock we can't do
	 * reclaim or take other locks that others hold while reclaiming.
	 */
	if (found)
		kfd_dec_compute_active(dqm->dev);

	return retval;
}

static int get_wave_state(struct device_queue_manager *dqm,
			  struct queue *q,
			  void __user *ctl_stack,
			  u32 *ctl_stack_used_size,
			  u32 *save_area_used_size)
{
	struct mqd_manager *mqd_mgr;

	dqm_lock(dqm);

	mqd_mgr = dqm->mqd_mgrs[KFD_MQD_TYPE_CP];

	if (q->properties.type != KFD_QUEUE_TYPE_COMPUTE ||
	    q->properties.is_active || !q->device->kfd->cwsr_enabled ||
	    !mqd_mgr->get_wave_state) {
		dqm_unlock(dqm);
		return -EINVAL;
	}

	dqm_unlock(dqm);

	/*
	 * get_wave_state is outside the dqm lock to prevent circular locking
	 * and the queue should be protected against destruction by the process
	 * lock.
	 */
	return mqd_mgr->get_wave_state(mqd_mgr, q->mqd, &q->properties,
			ctl_stack, ctl_stack_used_size, save_area_used_size);
}

static void get_queue_checkpoint_info(struct device_queue_manager *dqm,
			const struct queue *q,
			u32 *mqd_size,
			u32 *ctl_stack_size)
{
	struct mqd_manager *mqd_mgr;
	enum KFD_MQD_TYPE mqd_type =
			get_mqd_type_from_queue_type(q->properties.type);

	dqm_lock(dqm);
	mqd_mgr = dqm->mqd_mgrs[mqd_type];
	*mqd_size = mqd_mgr->mqd_size;
	*ctl_stack_size = 0;

	if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE && mqd_mgr->get_checkpoint_info)
		mqd_mgr->get_checkpoint_info(mqd_mgr, q->mqd, ctl_stack_size);

	dqm_unlock(dqm);
}

static int checkpoint_mqd(struct device_queue_manager *dqm,
			  const struct queue *q,
			  void *mqd,
			  void *ctl_stack)
{
	struct mqd_manager *mqd_mgr;
	int r = 0;
	enum KFD_MQD_TYPE mqd_type =
			get_mqd_type_from_queue_type(q->properties.type);

	dqm_lock(dqm);

	if (q->properties.is_active || !q->device->kfd->cwsr_enabled) {
		r = -EINVAL;
		goto dqm_unlock;
	}

	mqd_mgr = dqm->mqd_mgrs[mqd_type];
	if (!mqd_mgr->checkpoint_mqd) {
		r = -EOPNOTSUPP;
		goto dqm_unlock;
	}

	mqd_mgr->checkpoint_mqd(mqd_mgr, q->mqd, mqd, ctl_stack);

dqm_unlock:
	dqm_unlock(dqm);
	return r;
}

static int process_termination_cpsch(struct device_queue_manager *dqm,
		struct qcm_process_device *qpd)
{
	int retval;
	struct queue *q;
	struct kernel_queue *kq, *kq_next;
	struct mqd_manager *mqd_mgr;
	struct device_process_node *cur, *next_dpn;
	enum kfd_unmap_queues_filter filter =
		KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES;
	bool found = false;

	retval = 0;

	dqm_lock(dqm);

	/* Clean all kernel queues */
	list_for_each_entry_safe(kq, kq_next, &qpd->priv_queue_list, list) {
		list_del(&kq->list);
		decrement_queue_count(dqm, qpd, kq->queue);
		qpd->is_debug = false;
		dqm->total_queue_count--;
		filter = KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES;
	}

	/* Clear all user mode queues */
	list_for_each_entry(q, &qpd->queues_list, list) {
		if (q->properties.type == KFD_QUEUE_TYPE_SDMA)
			deallocate_sdma_queue(dqm, q);
		else if (q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI)
			deallocate_sdma_queue(dqm, q);

		if (q->properties.is_active) {
			decrement_queue_count(dqm, qpd, q);

			if (dqm->dev->kfd->shared_resources.enable_mes) {
				retval = remove_queue_mes(dqm, q, qpd);
				if (retval)
					pr_err("Failed to remove queue %d\n",
						q->properties.queue_id);
			}
		}

		dqm->total_queue_count--;
	}

	/* Unregister process */
	list_for_each_entry_safe(cur, next_dpn, &dqm->queues, list) {
		if (qpd == cur->qpd) {
			list_del(&cur->list);
			kfree(cur);
			dqm->processes_count--;
			found = true;
			break;
		}
	}

	if (!dqm->dev->kfd->shared_resources.enable_mes)
		retval = execute_queues_cpsch(dqm, filter, 0, USE_DEFAULT_GRACE_PERIOD);

	if ((!dqm->is_hws_hang) && (retval || qpd->reset_wavefronts)) {
		pr_warn("Resetting wave fronts (cpsch) on dev %p\n", dqm->dev);
		dbgdev_wave_reset_wavefronts(dqm->dev, qpd->pqm->process);
		qpd->reset_wavefronts = false;
	}

	/* Lastly, free mqd resources.
	 * Do free_mqd() after dqm_unlock to avoid circular locking.
	 */
	while (!list_empty(&qpd->queues_list)) {
		q = list_first_entry(&qpd->queues_list, struct queue, list);
		mqd_mgr = dqm->mqd_mgrs[get_mqd_type_from_queue_type(
				q->properties.type)];
		list_del(&q->list);
		qpd->queue_count--;
		dqm_unlock(dqm);
		mqd_mgr->free_mqd(mqd_mgr, q->mqd, q->mqd_mem_obj);
		dqm_lock(dqm);
	}
	dqm_unlock(dqm);

	/* Outside the DQM lock because under the DQM lock we can't do
	 * reclaim or take other locks that others hold while reclaiming.
	 */
	if (found)
		kfd_dec_compute_active(dqm->dev);

	return retval;
}

static int init_mqd_managers(struct device_queue_manager *dqm)
{
	int i, j;
	struct mqd_manager *mqd_mgr;

	for (i = 0; i < KFD_MQD_TYPE_MAX; i++) {
		mqd_mgr = dqm->asic_ops.mqd_manager_init(i, dqm->dev);
		if (!mqd_mgr) {
			pr_err("mqd manager [%d] initialization failed\n", i);
			goto out_free;
		}
		dqm->mqd_mgrs[i] = mqd_mgr;
	}

	return 0;

out_free:
	for (j = 0; j < i; j++) {
		kfree(dqm->mqd_mgrs[j]);
		dqm->mqd_mgrs[j] = NULL;
	}

	return -ENOMEM;
}

/* Allocate one hiq mqd (HWS) and all SDMA mqd in a continuous trunk*/
static int allocate_hiq_sdma_mqd(struct device_queue_manager *dqm)
{
	int retval;
	struct kfd_node *dev = dqm->dev;
	struct kfd_mem_obj *mem_obj = &dqm->hiq_sdma_mqd;
	uint32_t size = dqm->mqd_mgrs[KFD_MQD_TYPE_SDMA]->mqd_size *
		get_num_all_sdma_engines(dqm) *
		dev->kfd->device_info.num_sdma_queues_per_engine +
		(dqm->mqd_mgrs[KFD_MQD_TYPE_HIQ]->mqd_size *
		NUM_XCC(dqm->dev->xcc_mask));

	retval = amdgpu_amdkfd_alloc_gtt_mem(dev->adev, size,
		&(mem_obj->gtt_mem), &(mem_obj->gpu_addr),
		(void *)&(mem_obj->cpu_ptr), false);

	return retval;
}

struct device_queue_manager *device_queue_manager_init(struct kfd_node *dev)
{
	struct device_queue_manager *dqm;

	pr_debug("Loading device queue manager\n");

	dqm = kzalloc(sizeof(*dqm), GFP_KERNEL);
	if (!dqm)
		return NULL;

	switch (dev->adev->asic_type) {
	/* HWS is not available on Hawaii. */
	case CHIP_HAWAII:
	/* HWS depends on CWSR for timely dequeue. CWSR is not
	 * available on Tonga.
	 *
	 * FIXME: This argument also applies to Kaveri.
	 */
	case CHIP_TONGA:
		dqm->sched_policy = KFD_SCHED_POLICY_NO_HWS;
		break;
	default:
		dqm->sched_policy = sched_policy;
		break;
	}

	dqm->dev = dev;
	switch (dqm->sched_policy) {
	case KFD_SCHED_POLICY_HWS:
	case KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION:
		/* initialize dqm for cp scheduling */
		dqm->ops.create_queue = create_queue_cpsch;
		dqm->ops.initialize = initialize_cpsch;
		dqm->ops.start = start_cpsch;
		dqm->ops.stop = stop_cpsch;
		dqm->ops.pre_reset = pre_reset;
		dqm->ops.destroy_queue = destroy_queue_cpsch;
		dqm->ops.update_queue = update_queue;
		dqm->ops.register_process = register_process;
		dqm->ops.unregister_process = unregister_process;
		dqm->ops.uninitialize = uninitialize;
		dqm->ops.create_kernel_queue = create_kernel_queue_cpsch;
		dqm->ops.destroy_kernel_queue = destroy_kernel_queue_cpsch;
		dqm->ops.set_cache_memory_policy = set_cache_memory_policy;
		dqm->ops.process_termination = process_termination_cpsch;
		dqm->ops.evict_process_queues = evict_process_queues_cpsch;
		dqm->ops.restore_process_queues = restore_process_queues_cpsch;
		dqm->ops.get_wave_state = get_wave_state;
		dqm->ops.reset_queues = reset_queues_cpsch;
		dqm->ops.get_queue_checkpoint_info = get_queue_checkpoint_info;
		dqm->ops.checkpoint_mqd = checkpoint_mqd;
		break;
	case KFD_SCHED_POLICY_NO_HWS:
		/* initialize dqm for no cp scheduling */
		dqm->ops.start = start_nocpsch;
		dqm->ops.stop = stop_nocpsch;
		dqm->ops.pre_reset = pre_reset;
		dqm->ops.create_queue = create_queue_nocpsch;
		dqm->ops.destroy_queue = destroy_queue_nocpsch;
		dqm->ops.update_queue = update_queue;
		dqm->ops.register_process = register_process;
		dqm->ops.unregister_process = unregister_process;
		dqm->ops.initialize = initialize_nocpsch;
		dqm->ops.uninitialize = uninitialize;
		dqm->ops.set_cache_memory_policy = set_cache_memory_policy;
		dqm->ops.process_termination = process_termination_nocpsch;
		dqm->ops.evict_process_queues = evict_process_queues_nocpsch;
		dqm->ops.restore_process_queues =
			restore_process_queues_nocpsch;
		dqm->ops.get_wave_state = get_wave_state;
		dqm->ops.get_queue_checkpoint_info = get_queue_checkpoint_info;
		dqm->ops.checkpoint_mqd = checkpoint_mqd;
		break;
	default:
		pr_err("Invalid scheduling policy %d\n", dqm->sched_policy);
		goto out_free;
	}

	switch (dev->adev->asic_type) {
	case CHIP_CARRIZO:
		device_queue_manager_init_vi(&dqm->asic_ops);
		break;

	case CHIP_KAVERI:
		device_queue_manager_init_cik(&dqm->asic_ops);
		break;

	case CHIP_HAWAII:
		device_queue_manager_init_cik_hawaii(&dqm->asic_ops);
		break;

	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		device_queue_manager_init_vi_tonga(&dqm->asic_ops);
		break;

	default:
		if (KFD_GC_VERSION(dev) >= IP_VERSION(11, 0, 0))
			device_queue_manager_init_v11(&dqm->asic_ops);
		else if (KFD_GC_VERSION(dev) >= IP_VERSION(10, 1, 1))
			device_queue_manager_init_v10_navi10(&dqm->asic_ops);
		else if (KFD_GC_VERSION(dev) >= IP_VERSION(9, 0, 1))
			device_queue_manager_init_v9(&dqm->asic_ops);
		else {
			WARN(1, "Unexpected ASIC family %u",
			     dev->adev->asic_type);
			goto out_free;
		}
	}

	if (init_mqd_managers(dqm))
		goto out_free;

	if (!dev->kfd->shared_resources.enable_mes && allocate_hiq_sdma_mqd(dqm)) {
		pr_err("Failed to allocate hiq sdma mqd trunk buffer\n");
		goto out_free;
	}

	if (!dqm->ops.initialize(dqm)) {
		init_waitqueue_head(&dqm->destroy_wait);
		return dqm;
	}

out_free:
	kfree(dqm);
	return NULL;
}

static void deallocate_hiq_sdma_mqd(struct kfd_node *dev,
				    struct kfd_mem_obj *mqd)
{
	WARN(!mqd, "No hiq sdma mqd trunk to free");

	amdgpu_amdkfd_free_gtt_mem(dev->adev, mqd->gtt_mem);
}

void device_queue_manager_uninit(struct device_queue_manager *dqm)
{
	dqm->ops.stop(dqm);
	dqm->ops.uninitialize(dqm);
	if (!dqm->dev->kfd->shared_resources.enable_mes)
		deallocate_hiq_sdma_mqd(dqm->dev, &dqm->hiq_sdma_mqd);
	kfree(dqm);
}

int kfd_dqm_evict_pasid(struct device_queue_manager *dqm, u32 pasid)
{
	struct kfd_process_device *pdd;
	struct kfd_process *p = kfd_lookup_process_by_pasid(pasid);
	int ret = 0;

	if (!p)
		return -EINVAL;
	WARN(debug_evictions, "Evicting pid %d", p->lead_thread->pid);
	pdd = kfd_get_process_device_data(dqm->dev, p);
	if (pdd)
		ret = dqm->ops.evict_process_queues(dqm, &pdd->qpd);
	kfd_unref_process(p);

	return ret;
}

static void kfd_process_hw_exception(struct work_struct *work)
{
	struct device_queue_manager *dqm = container_of(work,
			struct device_queue_manager, hw_exception_work);
	amdgpu_amdkfd_gpu_reset(dqm->dev->adev);
}

int reserve_debug_trap_vmid(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd)
{
	int r;
	int updated_vmid_mask;

	if (dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		pr_err("Unsupported on sched_policy: %i\n", dqm->sched_policy);
		return -EINVAL;
	}

	dqm_lock(dqm);

	if (dqm->trap_debug_vmid != 0) {
		pr_err("Trap debug id already reserved\n");
		r = -EBUSY;
		goto out_unlock;
	}

	r = unmap_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES, 0,
			USE_DEFAULT_GRACE_PERIOD, false);
	if (r)
		goto out_unlock;

	updated_vmid_mask = dqm->dev->kfd->shared_resources.compute_vmid_bitmap;
	updated_vmid_mask &= ~(1 << dqm->dev->vm_info.last_vmid_kfd);

	dqm->dev->kfd->shared_resources.compute_vmid_bitmap = updated_vmid_mask;
	dqm->trap_debug_vmid = dqm->dev->vm_info.last_vmid_kfd;
	r = set_sched_resources(dqm);
	if (r)
		goto out_unlock;

	r = map_queues_cpsch(dqm);
	if (r)
		goto out_unlock;

	pr_debug("Reserved VMID for trap debug: %i\n", dqm->trap_debug_vmid);

out_unlock:
	dqm_unlock(dqm);
	return r;
}

/*
 * Releases vmid for the trap debugger
 */
int release_debug_trap_vmid(struct device_queue_manager *dqm,
			struct qcm_process_device *qpd)
{
	int r;
	int updated_vmid_mask;
	uint32_t trap_debug_vmid;

	if (dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		pr_err("Unsupported on sched_policy: %i\n", dqm->sched_policy);
		return -EINVAL;
	}

	dqm_lock(dqm);
	trap_debug_vmid = dqm->trap_debug_vmid;
	if (dqm->trap_debug_vmid == 0) {
		pr_err("Trap debug id is not reserved\n");
		r = -EINVAL;
		goto out_unlock;
	}

	r = unmap_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES, 0,
			USE_DEFAULT_GRACE_PERIOD, false);
	if (r)
		goto out_unlock;

	updated_vmid_mask = dqm->dev->kfd->shared_resources.compute_vmid_bitmap;
	updated_vmid_mask |= (1 << dqm->dev->vm_info.last_vmid_kfd);

	dqm->dev->kfd->shared_resources.compute_vmid_bitmap = updated_vmid_mask;
	dqm->trap_debug_vmid = 0;
	r = set_sched_resources(dqm);
	if (r)
		goto out_unlock;

	r = map_queues_cpsch(dqm);
	if (r)
		goto out_unlock;

	pr_debug("Released VMID for trap debug: %i\n", trap_debug_vmid);

out_unlock:
	dqm_unlock(dqm);
	return r;
}

#define QUEUE_NOT_FOUND		-1
/* invalidate queue operation in array */
static void q_array_invalidate(uint32_t num_queues, uint32_t *queue_ids)
{
	int i;

	for (i = 0; i < num_queues; i++)
		queue_ids[i] |= KFD_DBG_QUEUE_INVALID_MASK;
}

/* find queue index in array */
static int q_array_get_index(unsigned int queue_id,
		uint32_t num_queues,
		uint32_t *queue_ids)
{
	int i;

	for (i = 0; i < num_queues; i++)
		if (queue_id == (queue_ids[i] & ~KFD_DBG_QUEUE_INVALID_MASK))
			return i;

	return QUEUE_NOT_FOUND;
}

struct copy_context_work_handler_workarea {
	struct work_struct copy_context_work;
	struct kfd_process *p;
};

static void copy_context_work_handler (struct work_struct *work)
{
	struct copy_context_work_handler_workarea *workarea;
	struct mqd_manager *mqd_mgr;
	struct queue *q;
	struct mm_struct *mm;
	struct kfd_process *p;
	uint32_t tmp_ctl_stack_used_size, tmp_save_area_used_size;
	int i;

	workarea = container_of(work,
			struct copy_context_work_handler_workarea,
			copy_context_work);

	p = workarea->p;
	mm = get_task_mm(p->lead_thread);

	if (!mm)
		return;

	kthread_use_mm(mm);
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		struct device_queue_manager *dqm = pdd->dev->dqm;
		struct qcm_process_device *qpd = &pdd->qpd;

		list_for_each_entry(q, &qpd->queues_list, list) {
			mqd_mgr = dqm->mqd_mgrs[KFD_MQD_TYPE_CP];

			/* We ignore the return value from get_wave_state
			 * because
			 * i) right now, it always returns 0, and
			 * ii) if we hit an error, we would continue to the
			 *      next queue anyway.
			 */
			mqd_mgr->get_wave_state(mqd_mgr,
					q->mqd,
					&q->properties,
					(void __user *)	q->properties.ctx_save_restore_area_address,
					&tmp_ctl_stack_used_size,
					&tmp_save_area_used_size);
		}
	}
	kthread_unuse_mm(mm);
	mmput(mm);
}

static uint32_t *get_queue_ids(uint32_t num_queues, uint32_t *usr_queue_id_array)
{
	size_t array_size = num_queues * sizeof(uint32_t);
	uint32_t *queue_ids = NULL;

	if (!usr_queue_id_array)
		return NULL;

	queue_ids = kzalloc(array_size, GFP_KERNEL);
	if (!queue_ids)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(queue_ids, usr_queue_id_array, array_size))
		return ERR_PTR(-EFAULT);

	return queue_ids;
}

int resume_queues(struct kfd_process *p,
		uint32_t num_queues,
		uint32_t *usr_queue_id_array)
{
	uint32_t *queue_ids = NULL;
	int total_resumed = 0;
	int i;

	if (usr_queue_id_array) {
		queue_ids = get_queue_ids(num_queues, usr_queue_id_array);

		if (IS_ERR(queue_ids))
			return PTR_ERR(queue_ids);

		/* mask all queues as invalid.  unmask per successful request */
		q_array_invalidate(num_queues, queue_ids);
	}

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		struct device_queue_manager *dqm = pdd->dev->dqm;
		struct qcm_process_device *qpd = &pdd->qpd;
		struct queue *q;
		int r, per_device_resumed = 0;

		dqm_lock(dqm);

		/* unmask queues that resume or already resumed as valid */
		list_for_each_entry(q, &qpd->queues_list, list) {
			int q_idx = QUEUE_NOT_FOUND;

			if (queue_ids)
				q_idx = q_array_get_index(
						q->properties.queue_id,
						num_queues,
						queue_ids);

			if (!queue_ids || q_idx != QUEUE_NOT_FOUND) {
				int err = resume_single_queue(dqm, &pdd->qpd, q);

				if (queue_ids) {
					if (!err) {
						queue_ids[q_idx] &=
							~KFD_DBG_QUEUE_INVALID_MASK;
					} else {
						queue_ids[q_idx] |=
							KFD_DBG_QUEUE_ERROR_MASK;
						break;
					}
				}

				if (dqm->dev->kfd->shared_resources.enable_mes) {
					wake_up_all(&dqm->destroy_wait);
					if (!err)
						total_resumed++;
				} else {
					per_device_resumed++;
				}
			}
		}

		if (!per_device_resumed) {
			dqm_unlock(dqm);
			continue;
		}

		r = execute_queues_cpsch(dqm,
					KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES,
					0,
					USE_DEFAULT_GRACE_PERIOD);
		if (r) {
			pr_err("Failed to resume process queues\n");
			if (queue_ids) {
				list_for_each_entry(q, &qpd->queues_list, list) {
					int q_idx = q_array_get_index(
							q->properties.queue_id,
							num_queues,
							queue_ids);

					/* mask queue as error on resume fail */
					if (q_idx != QUEUE_NOT_FOUND)
						queue_ids[q_idx] |=
							KFD_DBG_QUEUE_ERROR_MASK;
				}
			}
		} else {
			wake_up_all(&dqm->destroy_wait);
			total_resumed += per_device_resumed;
		}

		dqm_unlock(dqm);
	}

	if (queue_ids) {
		if (copy_to_user((void __user *)usr_queue_id_array, queue_ids,
				num_queues * sizeof(uint32_t)))
			pr_err("copy_to_user failed on queue resume\n");

		kfree(queue_ids);
	}

	return total_resumed;
}

int suspend_queues(struct kfd_process *p,
			uint32_t num_queues,
			uint32_t grace_period,
			uint64_t exception_clear_mask,
			uint32_t *usr_queue_id_array)
{
	uint32_t *queue_ids = get_queue_ids(num_queues, usr_queue_id_array);
	int total_suspended = 0;
	int i;

	if (IS_ERR(queue_ids))
		return PTR_ERR(queue_ids);

	/* mask all queues as invalid.  umask on successful request */
	q_array_invalidate(num_queues, queue_ids);

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		struct device_queue_manager *dqm = pdd->dev->dqm;
		struct qcm_process_device *qpd = &pdd->qpd;
		struct queue *q;
		int r, per_device_suspended = 0;

		mutex_lock(&p->event_mutex);
		dqm_lock(dqm);

		/* unmask queues that suspend or already suspended */
		list_for_each_entry(q, &qpd->queues_list, list) {
			int q_idx = q_array_get_index(q->properties.queue_id,
							num_queues,
							queue_ids);

			if (q_idx != QUEUE_NOT_FOUND) {
				int err = suspend_single_queue(dqm, pdd, q);
				bool is_mes = dqm->dev->kfd->shared_resources.enable_mes;

				if (!err) {
					queue_ids[q_idx] &= ~KFD_DBG_QUEUE_INVALID_MASK;
					if (exception_clear_mask && is_mes)
						q->properties.exception_status &=
							~exception_clear_mask;

					if (is_mes)
						total_suspended++;
					else
						per_device_suspended++;
				} else if (err != -EBUSY) {
					r = err;
					queue_ids[q_idx] |= KFD_DBG_QUEUE_ERROR_MASK;
					break;
				}
			}
		}

		if (!per_device_suspended) {
			dqm_unlock(dqm);
			mutex_unlock(&p->event_mutex);
			if (total_suspended)
				amdgpu_amdkfd_debug_mem_fence(dqm->dev->adev);
			continue;
		}

		r = execute_queues_cpsch(dqm,
			KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES, 0,
			grace_period);

		if (r)
			pr_err("Failed to suspend process queues.\n");
		else
			total_suspended += per_device_suspended;

		list_for_each_entry(q, &qpd->queues_list, list) {
			int q_idx = q_array_get_index(q->properties.queue_id,
						num_queues, queue_ids);

			if (q_idx == QUEUE_NOT_FOUND)
				continue;

			/* mask queue as error on suspend fail */
			if (r)
				queue_ids[q_idx] |= KFD_DBG_QUEUE_ERROR_MASK;
			else if (exception_clear_mask)
				q->properties.exception_status &=
							~exception_clear_mask;
		}

		dqm_unlock(dqm);
		mutex_unlock(&p->event_mutex);
		amdgpu_device_flush_hdp(dqm->dev->adev, NULL);
	}

	if (total_suspended) {
		struct copy_context_work_handler_workarea copy_context_worker;

		INIT_WORK_ONSTACK(
				&copy_context_worker.copy_context_work,
				copy_context_work_handler);

		copy_context_worker.p = p;

		schedule_work(&copy_context_worker.copy_context_work);


		flush_work(&copy_context_worker.copy_context_work);
		destroy_work_on_stack(&copy_context_worker.copy_context_work);
	}

	if (copy_to_user((void __user *)usr_queue_id_array, queue_ids,
			num_queues * sizeof(uint32_t)))
		pr_err("copy_to_user failed on queue suspend\n");

	kfree(queue_ids);

	return total_suspended;
}

static uint32_t set_queue_type_for_user(struct queue_properties *q_props)
{
	switch (q_props->type) {
	case KFD_QUEUE_TYPE_COMPUTE:
		return q_props->format == KFD_QUEUE_FORMAT_PM4
					? KFD_IOC_QUEUE_TYPE_COMPUTE
					: KFD_IOC_QUEUE_TYPE_COMPUTE_AQL;
	case KFD_QUEUE_TYPE_SDMA:
		return KFD_IOC_QUEUE_TYPE_SDMA;
	case KFD_QUEUE_TYPE_SDMA_XGMI:
		return KFD_IOC_QUEUE_TYPE_SDMA_XGMI;
	default:
		WARN_ONCE(true, "queue type not recognized!");
		return 0xffffffff;
	};
}

void set_queue_snapshot_entry(struct queue *q,
			      uint64_t exception_clear_mask,
			      struct kfd_queue_snapshot_entry *qss_entry)
{
	qss_entry->ring_base_address = q->properties.queue_address;
	qss_entry->write_pointer_address = (uint64_t)q->properties.write_ptr;
	qss_entry->read_pointer_address = (uint64_t)q->properties.read_ptr;
	qss_entry->ctx_save_restore_address =
				q->properties.ctx_save_restore_area_address;
	qss_entry->ctx_save_restore_area_size =
				q->properties.ctx_save_restore_area_size;
	qss_entry->exception_status = q->properties.exception_status;
	qss_entry->queue_id = q->properties.queue_id;
	qss_entry->gpu_id = q->device->id;
	qss_entry->ring_size = (uint32_t)q->properties.queue_size;
	qss_entry->queue_type = set_queue_type_for_user(&q->properties);
	q->properties.exception_status &= ~exception_clear_mask;
}

int debug_lock_and_unmap(struct device_queue_manager *dqm)
{
	int r;

	if (dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		pr_err("Unsupported on sched_policy: %i\n", dqm->sched_policy);
		return -EINVAL;
	}

	if (!kfd_dbg_is_per_vmid_supported(dqm->dev))
		return 0;

	dqm_lock(dqm);

	r = unmap_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES, 0, 0, false);
	if (r)
		dqm_unlock(dqm);

	return r;
}

int debug_map_and_unlock(struct device_queue_manager *dqm)
{
	int r;

	if (dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		pr_err("Unsupported on sched_policy: %i\n", dqm->sched_policy);
		return -EINVAL;
	}

	if (!kfd_dbg_is_per_vmid_supported(dqm->dev))
		return 0;

	r = map_queues_cpsch(dqm);

	dqm_unlock(dqm);

	return r;
}

int debug_refresh_runlist(struct device_queue_manager *dqm)
{
	int r = debug_lock_and_unmap(dqm);

	if (r)
		return r;

	return debug_map_and_unlock(dqm);
}

#if defined(CONFIG_DEBUG_FS)

static void seq_reg_dump(struct seq_file *m,
			 uint32_t (*dump)[2], uint32_t n_regs)
{
	uint32_t i, count;

	for (i = 0, count = 0; i < n_regs; i++) {
		if (count == 0 ||
		    dump[i-1][0] + sizeof(uint32_t) != dump[i][0]) {
			seq_printf(m, "%s    %08x: %08x",
				   i ? "\n" : "",
				   dump[i][0], dump[i][1]);
			count = 7;
		} else {
			seq_printf(m, " %08x", dump[i][1]);
			count--;
		}
	}

	seq_puts(m, "\n");
}

int dqm_debugfs_hqds(struct seq_file *m, void *data)
{
	struct device_queue_manager *dqm = data;
	uint32_t xcc_mask = dqm->dev->xcc_mask;
	uint32_t (*dump)[2], n_regs;
	int pipe, queue;
	int r = 0, xcc_id;
	uint32_t sdma_engine_start;

	if (!dqm->sched_running) {
		seq_puts(m, " Device is stopped\n");
		return 0;
	}

	for_each_inst(xcc_id, xcc_mask) {
		r = dqm->dev->kfd2kgd->hqd_dump(dqm->dev->adev,
						KFD_CIK_HIQ_PIPE,
						KFD_CIK_HIQ_QUEUE, &dump,
						&n_regs, xcc_id);
		if (!r) {
			seq_printf(
				m,
				"   Inst %d, HIQ on MEC %d Pipe %d Queue %d\n",
				xcc_id,
				KFD_CIK_HIQ_PIPE / get_pipes_per_mec(dqm) + 1,
				KFD_CIK_HIQ_PIPE % get_pipes_per_mec(dqm),
				KFD_CIK_HIQ_QUEUE);
			seq_reg_dump(m, dump, n_regs);

			kfree(dump);
		}

		for (pipe = 0; pipe < get_pipes_per_mec(dqm); pipe++) {
			int pipe_offset = pipe * get_queues_per_pipe(dqm);

			for (queue = 0; queue < get_queues_per_pipe(dqm); queue++) {
				if (!test_bit(pipe_offset + queue,
				      dqm->dev->kfd->shared_resources.cp_queue_bitmap))
					continue;

				r = dqm->dev->kfd2kgd->hqd_dump(dqm->dev->adev,
								pipe, queue,
								&dump, &n_regs,
								xcc_id);
				if (r)
					break;

				seq_printf(m,
					   " Inst %d,  CP Pipe %d, Queue %d\n",
					   xcc_id, pipe, queue);
				seq_reg_dump(m, dump, n_regs);

				kfree(dump);
			}
		}
	}

	sdma_engine_start = dqm->dev->node_id * get_num_all_sdma_engines(dqm);
	for (pipe = sdma_engine_start;
	     pipe < (sdma_engine_start + get_num_all_sdma_engines(dqm));
	     pipe++) {
		for (queue = 0;
		     queue < dqm->dev->kfd->device_info.num_sdma_queues_per_engine;
		     queue++) {
			r = dqm->dev->kfd2kgd->hqd_sdma_dump(
				dqm->dev->adev, pipe, queue, &dump, &n_regs);
			if (r)
				break;

			seq_printf(m, "  SDMA Engine %d, RLC %d\n",
				  pipe, queue);
			seq_reg_dump(m, dump, n_regs);

			kfree(dump);
		}
	}

	return r;
}

int dqm_debugfs_hang_hws(struct device_queue_manager *dqm)
{
	int r = 0;

	dqm_lock(dqm);
	r = pm_debugfs_hang_hws(&dqm->packet_mgr);
	if (r) {
		dqm_unlock(dqm);
		return r;
	}
	dqm->active_runlist = true;
	r = execute_queues_cpsch(dqm, KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES,
				0, USE_DEFAULT_GRACE_PERIOD);
	dqm_unlock(dqm);

	return r;
}

#endif

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

#include <linux/slab.h>
#include <linux/list.h>
#include "kfd_device_queue_manager.h"
#include "kfd_priv.h"
#include "kfd_kernel_queue.h"
#include "amdgpu_amdkfd.h"

static inline struct process_queue_node *get_queue_by_qid(
			struct process_queue_manager *pqm, unsigned int qid)
{
	struct process_queue_node *pqn;

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if ((pqn->q && pqn->q->properties.queue_id == qid) ||
		    (pqn->kq && pqn->kq->queue->properties.queue_id == qid))
			return pqn;
	}

	return NULL;
}

static int assign_queue_slot_by_qid(struct process_queue_manager *pqm,
				    unsigned int qid)
{
	if (qid >= KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)
		return -EINVAL;

	if (__test_and_set_bit(qid, pqm->queue_slot_bitmap)) {
		pr_err("Cannot create new queue because requested qid(%u) is in use\n", qid);
		return -ENOSPC;
	}

	return 0;
}

static int find_available_queue_slot(struct process_queue_manager *pqm,
					unsigned int *qid)
{
	unsigned long found;

	found = find_first_zero_bit(pqm->queue_slot_bitmap,
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS);

	pr_debug("The new slot id %lu\n", found);

	if (found >= KFD_MAX_NUM_OF_QUEUES_PER_PROCESS) {
		pr_info("Cannot open more queues for process with pasid 0x%x\n",
				pqm->process->pasid);
		return -ENOMEM;
	}

	set_bit(found, pqm->queue_slot_bitmap);
	*qid = found;

	return 0;
}

void kfd_process_dequeue_from_device(struct kfd_process_device *pdd)
{
	struct kfd_node *dev = pdd->dev;

	if (pdd->already_dequeued)
		return;

	dev->dqm->ops.process_termination(dev->dqm, &pdd->qpd);
	pdd->already_dequeued = true;
}

int pqm_set_gws(struct process_queue_manager *pqm, unsigned int qid,
			void *gws)
{
	struct kfd_node *dev = NULL;
	struct process_queue_node *pqn;
	struct kfd_process_device *pdd;
	struct kgd_mem *mem = NULL;
	int ret;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_err("Queue id does not match any known queue\n");
		return -EINVAL;
	}

	if (pqn->q)
		dev = pqn->q->device;
	if (WARN_ON(!dev))
		return -ENODEV;

	pdd = kfd_get_process_device_data(dev, pqm->process);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return -EINVAL;
	}

	/* Only allow one queue per process can have GWS assigned */
	if (gws && pdd->qpd.num_gws)
		return -EBUSY;

	if (!gws && pdd->qpd.num_gws == 0)
		return -EINVAL;

	if (KFD_GC_VERSION(dev) != IP_VERSION(9, 4, 3) && !dev->kfd->shared_resources.enable_mes) {
		if (gws)
			ret = amdgpu_amdkfd_add_gws_to_process(pdd->process->kgd_process_info,
				gws, &mem);
		else
			ret = amdgpu_amdkfd_remove_gws_from_process(pdd->process->kgd_process_info,
				pqn->q->gws);
		if (unlikely(ret))
			return ret;
		pqn->q->gws = mem;
	} else {
		/*
		 * Intentionally set GWS to a non-NULL value
		 * for devices that do not use GWS for global wave
		 * synchronization but require the formality
		 * of setting GWS for cooperative groups.
		 */
		pqn->q->gws = gws ? ERR_PTR(-ENOMEM) : NULL;
	}

	pdd->qpd.num_gws = gws ? dev->adev->gds.gws_size : 0;

	return pqn->q->device->dqm->ops.update_queue(pqn->q->device->dqm,
							pqn->q, NULL);
}

void kfd_process_dequeue_from_all_devices(struct kfd_process *p)
{
	int i;

	for (i = 0; i < p->n_pdds; i++)
		kfd_process_dequeue_from_device(p->pdds[i]);
}

int pqm_init(struct process_queue_manager *pqm, struct kfd_process *p)
{
	INIT_LIST_HEAD(&pqm->queues);
	pqm->queue_slot_bitmap = bitmap_zalloc(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
					       GFP_KERNEL);
	if (!pqm->queue_slot_bitmap)
		return -ENOMEM;
	pqm->process = p;

	return 0;
}

void pqm_uninit(struct process_queue_manager *pqm)
{
	struct process_queue_node *pqn, *next;

	list_for_each_entry_safe(pqn, next, &pqm->queues, process_queue_list) {
		if (pqn->q && pqn->q->gws &&
		    KFD_GC_VERSION(pqn->q->device) != IP_VERSION(9, 4, 3) &&
		    !pqn->q->device->kfd->shared_resources.enable_mes)
			amdgpu_amdkfd_remove_gws_from_process(pqm->process->kgd_process_info,
				pqn->q->gws);
		kfd_procfs_del_queue(pqn->q);
		uninit_queue(pqn->q);
		list_del(&pqn->process_queue_list);
		kfree(pqn);
	}

	bitmap_free(pqm->queue_slot_bitmap);
	pqm->queue_slot_bitmap = NULL;
}

static int init_user_queue(struct process_queue_manager *pqm,
				struct kfd_node *dev, struct queue **q,
				struct queue_properties *q_properties,
				struct file *f, struct amdgpu_bo *wptr_bo,
				unsigned int qid)
{
	int retval;

	/* Doorbell initialized in user space*/
	q_properties->doorbell_ptr = NULL;
	q_properties->exception_status = KFD_EC_MASK(EC_QUEUE_NEW);

	/* let DQM handle it*/
	q_properties->vmid = 0;
	q_properties->queue_id = qid;

	retval = init_queue(q, q_properties);
	if (retval != 0)
		return retval;

	(*q)->device = dev;
	(*q)->process = pqm->process;

	if (dev->kfd->shared_resources.enable_mes) {
		retval = amdgpu_amdkfd_alloc_gtt_mem(dev->adev,
						AMDGPU_MES_GANG_CTX_SIZE,
						&(*q)->gang_ctx_bo,
						&(*q)->gang_ctx_gpu_addr,
						&(*q)->gang_ctx_cpu_ptr,
						false);
		if (retval) {
			pr_err("failed to allocate gang context bo\n");
			goto cleanup;
		}
		memset((*q)->gang_ctx_cpu_ptr, 0, AMDGPU_MES_GANG_CTX_SIZE);
		(*q)->wptr_bo = wptr_bo;
	}

	pr_debug("PQM After init queue");
	return 0;

cleanup:
	uninit_queue(*q);
	*q = NULL;
	return retval;
}

int pqm_create_queue(struct process_queue_manager *pqm,
			    struct kfd_node *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int *qid,
			    struct amdgpu_bo *wptr_bo,
			    const struct kfd_criu_queue_priv_data *q_data,
			    const void *restore_mqd,
			    const void *restore_ctl_stack,
			    uint32_t *p_doorbell_offset_in_process)
{
	int retval;
	struct kfd_process_device *pdd;
	struct queue *q;
	struct process_queue_node *pqn;
	struct kernel_queue *kq;
	enum kfd_queue_type type = properties->type;
	unsigned int max_queues = 127; /* HWS limit */

	/*
	 * On GFX 9.4.3, increase the number of queues that
	 * can be created to 255. No HWS limit on GFX 9.4.3.
	 */
	if (KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 3))
		max_queues = 255;

	q = NULL;
	kq = NULL;

	pdd = kfd_get_process_device_data(dev, pqm->process);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return -1;
	}

	/*
	 * for debug process, verify that it is within the static queues limit
	 * currently limit is set to half of the total avail HQD slots
	 * If we are just about to create DIQ, the is_debug flag is not set yet
	 * Hence we also check the type as well
	 */
	if ((pdd->qpd.is_debug) || (type == KFD_QUEUE_TYPE_DIQ))
		max_queues = dev->kfd->device_info.max_no_of_hqd/2;

	if (pdd->qpd.queue_count >= max_queues)
		return -ENOSPC;

	if (q_data) {
		retval = assign_queue_slot_by_qid(pqm, q_data->q_id);
		*qid = q_data->q_id;
	} else
		retval = find_available_queue_slot(pqm, qid);

	if (retval != 0)
		return retval;

	if (list_empty(&pdd->qpd.queues_list) &&
	    list_empty(&pdd->qpd.priv_queue_list))
		dev->dqm->ops.register_process(dev->dqm, &pdd->qpd);

	pqn = kzalloc(sizeof(*pqn), GFP_KERNEL);
	if (!pqn) {
		retval = -ENOMEM;
		goto err_allocate_pqn;
	}

	switch (type) {
	case KFD_QUEUE_TYPE_SDMA:
	case KFD_QUEUE_TYPE_SDMA_XGMI:
		/* SDMA queues are always allocated statically no matter
		 * which scheduler mode is used. We also do not need to
		 * check whether a SDMA queue can be allocated here, because
		 * allocate_sdma_queue() in create_queue() has the
		 * corresponding check logic.
		 */
		retval = init_user_queue(pqm, dev, &q, properties, f, wptr_bo, *qid);
		if (retval != 0)
			goto err_create_queue;
		pqn->q = q;
		pqn->kq = NULL;
		retval = dev->dqm->ops.create_queue(dev->dqm, q, &pdd->qpd, q_data,
						    restore_mqd, restore_ctl_stack);
		print_queue(q);
		break;

	case KFD_QUEUE_TYPE_COMPUTE:
		/* check if there is over subscription */
		if ((dev->dqm->sched_policy ==
		     KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION) &&
		((dev->dqm->processes_count >= dev->vm_info.vmid_num_kfd) ||
		(dev->dqm->active_queue_count >= get_cp_queues_num(dev->dqm)))) {
			pr_debug("Over-subscription is not allowed when amdkfd.sched_policy == 1\n");
			retval = -EPERM;
			goto err_create_queue;
		}

		retval = init_user_queue(pqm, dev, &q, properties, f, wptr_bo, *qid);
		if (retval != 0)
			goto err_create_queue;
		pqn->q = q;
		pqn->kq = NULL;
		retval = dev->dqm->ops.create_queue(dev->dqm, q, &pdd->qpd, q_data,
						    restore_mqd, restore_ctl_stack);
		print_queue(q);
		break;
	case KFD_QUEUE_TYPE_DIQ:
		kq = kernel_queue_init(dev, KFD_QUEUE_TYPE_DIQ);
		if (!kq) {
			retval = -ENOMEM;
			goto err_create_queue;
		}
		kq->queue->properties.queue_id = *qid;
		pqn->kq = kq;
		pqn->q = NULL;
		retval = kfd_process_drain_interrupts(pdd);
		if (retval)
			break;

		retval = dev->dqm->ops.create_kernel_queue(dev->dqm,
							kq, &pdd->qpd);
		break;
	default:
		WARN(1, "Invalid queue type %d", type);
		retval = -EINVAL;
	}

	if (retval != 0) {
		pr_err("Pasid 0x%x DQM create queue type %d failed. ret %d\n",
			pqm->process->pasid, type, retval);
		goto err_create_queue;
	}

	if (q && p_doorbell_offset_in_process) {
		/* Return the doorbell offset within the doorbell page
		 * to the caller so it can be passed up to user mode
		 * (in bytes).
		 * relative doorbell index = Absolute doorbell index -
		 * absolute index of first doorbell in the page.
		 */
		uint32_t first_db_index = amdgpu_doorbell_index_on_bar(pdd->dev->adev,
								       pdd->qpd.proc_doorbells,
								       0);

		*p_doorbell_offset_in_process = (q->properties.doorbell_off
						- first_db_index) * sizeof(uint32_t);
	}

	pr_debug("PQM After DQM create queue\n");

	list_add(&pqn->process_queue_list, &pqm->queues);

	if (q) {
		pr_debug("PQM done creating queue\n");
		kfd_procfs_add_queue(q);
		print_queue_properties(&q->properties);
	}

	return retval;

err_create_queue:
	uninit_queue(q);
	if (kq)
		kernel_queue_uninit(kq, false);
	kfree(pqn);
err_allocate_pqn:
	/* check if queues list is empty unregister process from device */
	clear_bit(*qid, pqm->queue_slot_bitmap);
	if (list_empty(&pdd->qpd.queues_list) &&
	    list_empty(&pdd->qpd.priv_queue_list))
		dev->dqm->ops.unregister_process(dev->dqm, &pdd->qpd);
	return retval;
}

int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid)
{
	struct process_queue_node *pqn;
	struct kfd_process_device *pdd;
	struct device_queue_manager *dqm;
	struct kfd_node *dev;
	int retval;

	dqm = NULL;

	retval = 0;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_err("Queue id does not match any known queue\n");
		return -EINVAL;
	}

	dev = NULL;
	if (pqn->kq)
		dev = pqn->kq->dev;
	if (pqn->q)
		dev = pqn->q->device;
	if (WARN_ON(!dev))
		return -ENODEV;

	pdd = kfd_get_process_device_data(dev, pqm->process);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return -1;
	}

	if (pqn->kq) {
		/* destroy kernel queue (DIQ) */
		dqm = pqn->kq->dev->dqm;
		dqm->ops.destroy_kernel_queue(dqm, pqn->kq, &pdd->qpd);
		kernel_queue_uninit(pqn->kq, false);
	}

	if (pqn->q) {
		kfd_procfs_del_queue(pqn->q);
		dqm = pqn->q->device->dqm;
		retval = dqm->ops.destroy_queue(dqm, &pdd->qpd, pqn->q);
		if (retval) {
			pr_err("Pasid 0x%x destroy queue %d failed, ret %d\n",
				pqm->process->pasid,
				pqn->q->properties.queue_id, retval);
			if (retval != -ETIME)
				goto err_destroy_queue;
		}

		if (pqn->q->gws) {
			if (KFD_GC_VERSION(pqn->q->device) != IP_VERSION(9, 4, 3) &&
			    !dev->kfd->shared_resources.enable_mes)
				amdgpu_amdkfd_remove_gws_from_process(
						pqm->process->kgd_process_info,
						pqn->q->gws);
			pdd->qpd.num_gws = 0;
		}

		if (dev->kfd->shared_resources.enable_mes) {
			amdgpu_amdkfd_free_gtt_mem(dev->adev,
						   pqn->q->gang_ctx_bo);
			if (pqn->q->wptr_bo)
				amdgpu_amdkfd_free_gtt_mem(dev->adev, pqn->q->wptr_bo);

		}
		uninit_queue(pqn->q);
	}

	list_del(&pqn->process_queue_list);
	kfree(pqn);
	clear_bit(qid, pqm->queue_slot_bitmap);

	if (list_empty(&pdd->qpd.queues_list) &&
	    list_empty(&pdd->qpd.priv_queue_list))
		dqm->ops.unregister_process(dqm, &pdd->qpd);

err_destroy_queue:
	return retval;
}

int pqm_update_queue_properties(struct process_queue_manager *pqm,
				unsigned int qid, struct queue_properties *p)
{
	int retval;
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("No queue %d exists for update operation\n", qid);
		return -EFAULT;
	}

	pqn->q->properties.queue_address = p->queue_address;
	pqn->q->properties.queue_size = p->queue_size;
	pqn->q->properties.queue_percent = p->queue_percent;
	pqn->q->properties.priority = p->priority;
	pqn->q->properties.pm4_target_xcc = p->pm4_target_xcc;

	retval = pqn->q->device->dqm->ops.update_queue(pqn->q->device->dqm,
							pqn->q, NULL);
	if (retval != 0)
		return retval;

	return 0;
}

int pqm_update_mqd(struct process_queue_manager *pqm,
				unsigned int qid, struct mqd_update_info *minfo)
{
	int retval;
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("No queue %d exists for update operation\n", qid);
		return -EFAULT;
	}

	/* CUs are masked for debugger requirements so deny user mask  */
	if (pqn->q->properties.is_dbg_wa && minfo && minfo->cu_mask.ptr)
		return -EBUSY;

	/* ASICs that have WGPs must enforce pairwise enabled mask checks. */
	if (minfo && minfo->cu_mask.ptr &&
			KFD_GC_VERSION(pqn->q->device) >= IP_VERSION(10, 0, 0)) {
		int i;

		for (i = 0; i < minfo->cu_mask.count; i += 2) {
			uint32_t cu_pair = (minfo->cu_mask.ptr[i / 32] >> (i % 32)) & 0x3;

			if (cu_pair && cu_pair != 0x3) {
				pr_debug("CUs must be adjacent pairwise enabled.\n");
				return -EINVAL;
			}
		}
	}

	retval = pqn->q->device->dqm->ops.update_queue(pqn->q->device->dqm,
							pqn->q, minfo);
	if (retval != 0)
		return retval;

	if (minfo && minfo->cu_mask.ptr)
		pqn->q->properties.is_user_cu_masked = true;

	return 0;
}

struct kernel_queue *pqm_get_kernel_queue(
					struct process_queue_manager *pqm,
					unsigned int qid)
{
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	if (pqn && pqn->kq)
		return pqn->kq;

	return NULL;
}

struct queue *pqm_get_user_queue(struct process_queue_manager *pqm,
					unsigned int qid)
{
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	return pqn ? pqn->q : NULL;
}

int pqm_get_wave_state(struct process_queue_manager *pqm,
		       unsigned int qid,
		       void __user *ctl_stack,
		       u32 *ctl_stack_used_size,
		       u32 *save_area_used_size)
{
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("amdkfd: No queue %d exists for operation\n",
			 qid);
		return -EFAULT;
	}

	return pqn->q->device->dqm->ops.get_wave_state(pqn->q->device->dqm,
						       pqn->q,
						       ctl_stack,
						       ctl_stack_used_size,
						       save_area_used_size);
}

int pqm_get_queue_snapshot(struct process_queue_manager *pqm,
			   uint64_t exception_clear_mask,
			   void __user *buf,
			   int *num_qss_entries,
			   uint32_t *entry_size)
{
	struct process_queue_node *pqn;
	struct kfd_queue_snapshot_entry src;
	uint32_t tmp_entry_size = *entry_size, tmp_qss_entries = *num_qss_entries;
	int r = 0;

	*num_qss_entries = 0;
	if (!(*entry_size))
		return -EINVAL;

	*entry_size = min_t(size_t, *entry_size, sizeof(struct kfd_queue_snapshot_entry));
	mutex_lock(&pqm->process->event_mutex);

	memset(&src, 0, sizeof(src));

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if (!pqn->q)
			continue;

		if (*num_qss_entries < tmp_qss_entries) {
			set_queue_snapshot_entry(pqn->q, exception_clear_mask, &src);

			if (copy_to_user(buf, &src, *entry_size)) {
				r = -EFAULT;
				break;
			}
			buf += tmp_entry_size;
		}
		*num_qss_entries += 1;
	}

	mutex_unlock(&pqm->process->event_mutex);
	return r;
}

static int get_queue_data_sizes(struct kfd_process_device *pdd,
				struct queue *q,
				uint32_t *mqd_size,
				uint32_t *ctl_stack_size)
{
	int ret;

	ret = pqm_get_queue_checkpoint_info(&pdd->process->pqm,
					    q->properties.queue_id,
					    mqd_size,
					    ctl_stack_size);
	if (ret)
		pr_err("Failed to get queue dump info (%d)\n", ret);

	return ret;
}

int kfd_process_get_queue_info(struct kfd_process *p,
			       uint32_t *num_queues,
			       uint64_t *priv_data_sizes)
{
	uint32_t extra_data_sizes = 0;
	struct queue *q;
	int i;
	int ret;

	*num_queues = 0;

	/* Run over all PDDs of the process */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		list_for_each_entry(q, &pdd->qpd.queues_list, list) {
			if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE ||
				q->properties.type == KFD_QUEUE_TYPE_SDMA ||
				q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {
				uint32_t mqd_size, ctl_stack_size;

				*num_queues = *num_queues + 1;

				ret = get_queue_data_sizes(pdd, q, &mqd_size, &ctl_stack_size);
				if (ret)
					return ret;

				extra_data_sizes += mqd_size + ctl_stack_size;
			} else {
				pr_err("Unsupported queue type (%d)\n", q->properties.type);
				return -EOPNOTSUPP;
			}
		}
	}
	*priv_data_sizes = extra_data_sizes +
				(*num_queues * sizeof(struct kfd_criu_queue_priv_data));

	return 0;
}

static int pqm_checkpoint_mqd(struct process_queue_manager *pqm,
			      unsigned int qid,
			      void *mqd,
			      void *ctl_stack)
{
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("amdkfd: No queue %d exists for operation\n", qid);
		return -EFAULT;
	}

	if (!pqn->q->device->dqm->ops.checkpoint_mqd) {
		pr_err("amdkfd: queue dumping not supported on this device\n");
		return -EOPNOTSUPP;
	}

	return pqn->q->device->dqm->ops.checkpoint_mqd(pqn->q->device->dqm,
						       pqn->q, mqd, ctl_stack);
}

static int criu_checkpoint_queue(struct kfd_process_device *pdd,
			   struct queue *q,
			   struct kfd_criu_queue_priv_data *q_data)
{
	uint8_t *mqd, *ctl_stack;
	int ret;

	mqd = (void *)(q_data + 1);
	ctl_stack = mqd + q_data->mqd_size;

	q_data->gpu_id = pdd->user_gpu_id;
	q_data->type = q->properties.type;
	q_data->format = q->properties.format;
	q_data->q_id =  q->properties.queue_id;
	q_data->q_address = q->properties.queue_address;
	q_data->q_size = q->properties.queue_size;
	q_data->priority = q->properties.priority;
	q_data->q_percent = q->properties.queue_percent;
	q_data->read_ptr_addr = (uint64_t)q->properties.read_ptr;
	q_data->write_ptr_addr = (uint64_t)q->properties.write_ptr;
	q_data->doorbell_id = q->doorbell_id;

	q_data->sdma_id = q->sdma_id;

	q_data->eop_ring_buffer_address =
		q->properties.eop_ring_buffer_address;

	q_data->eop_ring_buffer_size = q->properties.eop_ring_buffer_size;

	q_data->ctx_save_restore_area_address =
		q->properties.ctx_save_restore_area_address;

	q_data->ctx_save_restore_area_size =
		q->properties.ctx_save_restore_area_size;

	q_data->gws = !!q->gws;

	ret = pqm_checkpoint_mqd(&pdd->process->pqm, q->properties.queue_id, mqd, ctl_stack);
	if (ret) {
		pr_err("Failed checkpoint queue_mqd (%d)\n", ret);
		return ret;
	}

	pr_debug("Dumping Queue: gpu_id:%x queue_id:%u\n", q_data->gpu_id, q_data->q_id);
	return ret;
}

static int criu_checkpoint_queues_device(struct kfd_process_device *pdd,
				   uint8_t __user *user_priv,
				   unsigned int *q_index,
				   uint64_t *queues_priv_data_offset)
{
	unsigned int q_private_data_size = 0;
	uint8_t *q_private_data = NULL; /* Local buffer to store individual queue private data */
	struct queue *q;
	int ret = 0;

	list_for_each_entry(q, &pdd->qpd.queues_list, list) {
		struct kfd_criu_queue_priv_data *q_data;
		uint64_t q_data_size;
		uint32_t mqd_size;
		uint32_t ctl_stack_size;

		if (q->properties.type != KFD_QUEUE_TYPE_COMPUTE &&
			q->properties.type != KFD_QUEUE_TYPE_SDMA &&
			q->properties.type != KFD_QUEUE_TYPE_SDMA_XGMI) {

			pr_err("Unsupported queue type (%d)\n", q->properties.type);
			ret = -EOPNOTSUPP;
			break;
		}

		ret = get_queue_data_sizes(pdd, q, &mqd_size, &ctl_stack_size);
		if (ret)
			break;

		q_data_size = sizeof(*q_data) + mqd_size + ctl_stack_size;

		/* Increase local buffer space if needed */
		if (q_private_data_size < q_data_size) {
			kfree(q_private_data);

			q_private_data = kzalloc(q_data_size, GFP_KERNEL);
			if (!q_private_data) {
				ret = -ENOMEM;
				break;
			}
			q_private_data_size = q_data_size;
		}

		q_data = (struct kfd_criu_queue_priv_data *)q_private_data;

		/* data stored in this order: priv_data, mqd, ctl_stack */
		q_data->mqd_size = mqd_size;
		q_data->ctl_stack_size = ctl_stack_size;

		ret = criu_checkpoint_queue(pdd, q, q_data);
		if (ret)
			break;

		q_data->object_type = KFD_CRIU_OBJECT_TYPE_QUEUE;

		ret = copy_to_user(user_priv + *queues_priv_data_offset,
				q_data, q_data_size);
		if (ret) {
			ret = -EFAULT;
			break;
		}
		*queues_priv_data_offset += q_data_size;
		*q_index = *q_index + 1;
	}

	kfree(q_private_data);

	return ret;
}

int kfd_criu_checkpoint_queues(struct kfd_process *p,
			 uint8_t __user *user_priv_data,
			 uint64_t *priv_data_offset)
{
	int ret = 0, pdd_index, q_index = 0;

	for (pdd_index = 0; pdd_index < p->n_pdds; pdd_index++) {
		struct kfd_process_device *pdd = p->pdds[pdd_index];

		/*
		 * criu_checkpoint_queues_device will copy data to user and update q_index and
		 * queues_priv_data_offset
		 */
		ret = criu_checkpoint_queues_device(pdd, user_priv_data, &q_index,
					      priv_data_offset);

		if (ret)
			break;
	}

	return ret;
}

static void set_queue_properties_from_criu(struct queue_properties *qp,
					  struct kfd_criu_queue_priv_data *q_data)
{
	qp->is_interop = false;
	qp->queue_percent = q_data->q_percent;
	qp->priority = q_data->priority;
	qp->queue_address = q_data->q_address;
	qp->queue_size = q_data->q_size;
	qp->read_ptr = (uint32_t *) q_data->read_ptr_addr;
	qp->write_ptr = (uint32_t *) q_data->write_ptr_addr;
	qp->eop_ring_buffer_address = q_data->eop_ring_buffer_address;
	qp->eop_ring_buffer_size = q_data->eop_ring_buffer_size;
	qp->ctx_save_restore_area_address = q_data->ctx_save_restore_area_address;
	qp->ctx_save_restore_area_size = q_data->ctx_save_restore_area_size;
	qp->ctl_stack_size = q_data->ctl_stack_size;
	qp->type = q_data->type;
	qp->format = q_data->format;
}

int kfd_criu_restore_queue(struct kfd_process *p,
			   uint8_t __user *user_priv_ptr,
			   uint64_t *priv_data_offset,
			   uint64_t max_priv_data_size)
{
	uint8_t *mqd, *ctl_stack, *q_extra_data = NULL;
	struct kfd_criu_queue_priv_data *q_data;
	struct kfd_process_device *pdd;
	uint64_t q_extra_data_size;
	struct queue_properties qp;
	unsigned int queue_id;
	int ret = 0;

	if (*priv_data_offset + sizeof(*q_data) > max_priv_data_size)
		return -EINVAL;

	q_data = kmalloc(sizeof(*q_data), GFP_KERNEL);
	if (!q_data)
		return -ENOMEM;

	ret = copy_from_user(q_data, user_priv_ptr + *priv_data_offset, sizeof(*q_data));
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	*priv_data_offset += sizeof(*q_data);
	q_extra_data_size = (uint64_t)q_data->ctl_stack_size + q_data->mqd_size;

	if (*priv_data_offset + q_extra_data_size > max_priv_data_size) {
		ret = -EINVAL;
		goto exit;
	}

	q_extra_data = kmalloc(q_extra_data_size, GFP_KERNEL);
	if (!q_extra_data) {
		ret = -ENOMEM;
		goto exit;
	}

	ret = copy_from_user(q_extra_data, user_priv_ptr + *priv_data_offset, q_extra_data_size);
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	*priv_data_offset += q_extra_data_size;

	pdd = kfd_process_device_data_by_id(p, q_data->gpu_id);
	if (!pdd) {
		pr_err("Failed to get pdd\n");
		ret = -EINVAL;
		goto exit;
	}

	/* data stored in this order: mqd, ctl_stack */
	mqd = q_extra_data;
	ctl_stack = mqd + q_data->mqd_size;

	memset(&qp, 0, sizeof(qp));
	set_queue_properties_from_criu(&qp, q_data);

	print_queue_properties(&qp);

	ret = pqm_create_queue(&p->pqm, pdd->dev, NULL, &qp, &queue_id, NULL, q_data, mqd, ctl_stack,
				NULL);
	if (ret) {
		pr_err("Failed to create new queue err:%d\n", ret);
		goto exit;
	}

	if (q_data->gws)
		ret = pqm_set_gws(&p->pqm, q_data->q_id, pdd->dev->gws);

exit:
	if (ret)
		pr_err("Failed to restore queue (%d)\n", ret);
	else
		pr_debug("Queue id %d was restored successfully\n", queue_id);

	kfree(q_data);

	return ret;
}

int pqm_get_queue_checkpoint_info(struct process_queue_manager *pqm,
				  unsigned int qid,
				  uint32_t *mqd_size,
				  uint32_t *ctl_stack_size)
{
	struct process_queue_node *pqn;

	pqn = get_queue_by_qid(pqm, qid);
	if (!pqn) {
		pr_debug("amdkfd: No queue %d exists for operation\n", qid);
		return -EFAULT;
	}

	if (!pqn->q->device->dqm->ops.get_queue_checkpoint_info) {
		pr_err("amdkfd: queue dumping not supported on this device\n");
		return -EOPNOTSUPP;
	}

	pqn->q->device->dqm->ops.get_queue_checkpoint_info(pqn->q->device->dqm,
						       pqn->q, mqd_size,
						       ctl_stack_size);
	return 0;
}

#if defined(CONFIG_DEBUG_FS)

int pqm_debugfs_mqds(struct seq_file *m, void *data)
{
	struct process_queue_manager *pqm = data;
	struct process_queue_node *pqn;
	struct queue *q;
	enum KFD_MQD_TYPE mqd_type;
	struct mqd_manager *mqd_mgr;
	int r = 0, xcc, num_xccs = 1;
	void *mqd;
	uint64_t size = 0;

	list_for_each_entry(pqn, &pqm->queues, process_queue_list) {
		if (pqn->q) {
			q = pqn->q;
			switch (q->properties.type) {
			case KFD_QUEUE_TYPE_SDMA:
			case KFD_QUEUE_TYPE_SDMA_XGMI:
				seq_printf(m, "  SDMA queue on device %x\n",
					   q->device->id);
				mqd_type = KFD_MQD_TYPE_SDMA;
				break;
			case KFD_QUEUE_TYPE_COMPUTE:
				seq_printf(m, "  Compute queue on device %x\n",
					   q->device->id);
				mqd_type = KFD_MQD_TYPE_CP;
				num_xccs = NUM_XCC(q->device->xcc_mask);
				break;
			default:
				seq_printf(m,
				"  Bad user queue type %d on device %x\n",
					   q->properties.type, q->device->id);
				continue;
			}
			mqd_mgr = q->device->dqm->mqd_mgrs[mqd_type];
			size = mqd_mgr->mqd_stride(mqd_mgr,
							&q->properties);
		} else if (pqn->kq) {
			q = pqn->kq->queue;
			mqd_mgr = pqn->kq->mqd_mgr;
			switch (q->properties.type) {
			case KFD_QUEUE_TYPE_DIQ:
				seq_printf(m, "  DIQ on device %x\n",
					   pqn->kq->dev->id);
				break;
			default:
				seq_printf(m,
				"  Bad kernel queue type %d on device %x\n",
					   q->properties.type,
					   pqn->kq->dev->id);
				continue;
			}
		} else {
			seq_printf(m,
		"  Weird: Queue node with neither kernel nor user queue\n");
			continue;
		}

		for (xcc = 0; xcc < num_xccs; xcc++) {
			mqd = q->mqd + size * xcc;
			r = mqd_mgr->debugfs_show_mqd(m, mqd);
			if (r != 0)
				break;
		}
	}

	return r;
}

#endif

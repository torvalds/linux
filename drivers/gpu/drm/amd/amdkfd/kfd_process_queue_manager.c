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
	struct kfd_dev *dev = pdd->dev;

	if (pdd->already_dequeued)
		return;

	dev->dqm->ops.process_termination(dev->dqm, &pdd->qpd);
	pdd->already_dequeued = true;
}

int pqm_set_gws(struct process_queue_manager *pqm, unsigned int qid,
			void *gws)
{
	struct kfd_dev *dev = NULL;
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

	if (gws)
		ret = amdgpu_amdkfd_add_gws_to_process(pdd->process->kgd_process_info,
			gws, &mem);
	else
		ret = amdgpu_amdkfd_remove_gws_from_process(pdd->process->kgd_process_info,
			pqn->q->gws);
	if (unlikely(ret))
		return ret;

	pqn->q->gws = mem;
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
		if (pqn->q && pqn->q->gws)
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
				struct kfd_dev *dev, struct queue **q,
				struct queue_properties *q_properties,
				struct file *f, unsigned int qid)
{
	int retval;

	/* Doorbell initialized in user space*/
	q_properties->doorbell_ptr = NULL;

	/* let DQM handle it*/
	q_properties->vmid = 0;
	q_properties->queue_id = qid;

	retval = init_queue(q, q_properties);
	if (retval != 0)
		return retval;

	(*q)->device = dev;
	(*q)->process = pqm->process;

	pr_debug("PQM After init queue");

	return retval;
}

int pqm_create_queue(struct process_queue_manager *pqm,
			    struct kfd_dev *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int *qid,
			    uint32_t *p_doorbell_offset_in_process)
{
	int retval;
	struct kfd_process_device *pdd;
	struct queue *q;
	struct process_queue_node *pqn;
	struct kernel_queue *kq;
	enum kfd_queue_type type = properties->type;
	unsigned int max_queues = 127; /* HWS limit */

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
		max_queues = dev->device_info.max_no_of_hqd/2;

	if (pdd->qpd.queue_count >= max_queues)
		return -ENOSPC;

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
		retval = init_user_queue(pqm, dev, &q, properties, f, *qid);
		if (retval != 0)
			goto err_create_queue;
		pqn->q = q;
		pqn->kq = NULL;
		retval = dev->dqm->ops.create_queue(dev->dqm, q, &pdd->qpd);
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

		retval = init_user_queue(pqm, dev, &q, properties, f, *qid);
		if (retval != 0)
			goto err_create_queue;
		pqn->q = q;
		pqn->kq = NULL;
		retval = dev->dqm->ops.create_queue(dev->dqm, q, &pdd->qpd);
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

	if (q && p_doorbell_offset_in_process)
		/* Return the doorbell offset within the doorbell page
		 * to the caller so it can be passed up to user mode
		 * (in bytes).
		 * There are always 1024 doorbells per process, so in case
		 * of 8-byte doorbells, there are two doorbell pages per
		 * process.
		 */
		*p_doorbell_offset_in_process =
			(q->properties.doorbell_off * sizeof(uint32_t)) &
			(kfd_doorbell_process_slice(dev) - 1);

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
	struct kfd_dev *dev;
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
			amdgpu_amdkfd_remove_gws_from_process(pqm->process->kgd_process_info,
				pqn->q->gws);
			pdd->qpd.num_gws = 0;
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

	retval = pqn->q->device->dqm->ops.update_queue(pqn->q->device->dqm,
							pqn->q, minfo);
	if (retval != 0)
		return retval;

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

#if defined(CONFIG_DEBUG_FS)

int pqm_debugfs_mqds(struct seq_file *m, void *data)
{
	struct process_queue_manager *pqm = data;
	struct process_queue_node *pqn;
	struct queue *q;
	enum KFD_MQD_TYPE mqd_type;
	struct mqd_manager *mqd_mgr;
	int r = 0;

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
				break;
			default:
				seq_printf(m,
				"  Bad user queue type %d on device %x\n",
					   q->properties.type, q->device->id);
				continue;
			}
			mqd_mgr = q->device->dqm->mqd_mgrs[mqd_type];
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

		r = mqd_mgr->debugfs_show_mqd(m, q->mqd);
		if (r != 0)
			break;
	}

	return r;
}

#endif

/*
 * Copyright (c) 2017 Christoph Hellwig.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/moduleparam.h>
#include "nvme.h"

static bool multipath = true;
module_param(multipath, bool, 0444);
MODULE_PARM_DESC(multipath,
	"turn on native support for multiple controllers per subsystem");

void nvme_failover_req(struct request *req)
{
	struct nvme_ns *ns = req->q->queuedata;
	unsigned long flags;

	spin_lock_irqsave(&ns->head->requeue_lock, flags);
	blk_steal_bios(&ns->head->requeue_list, req);
	spin_unlock_irqrestore(&ns->head->requeue_lock, flags);
	blk_mq_end_request(req, 0);

	nvme_reset_ctrl(ns->ctrl);
	kblockd_schedule_work(&ns->head->requeue_work);
}

bool nvme_req_needs_failover(struct request *req, blk_status_t error)
{
	if (!(req->cmd_flags & REQ_NVME_MPATH))
		return false;
	return blk_path_error(error);
}

void nvme_kick_requeue_lists(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;

	down_read(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		if (ns->head->disk)
			kblockd_schedule_work(&ns->head->requeue_work);
	}
	up_read(&ctrl->namespaces_rwsem);
}

static struct nvme_ns *__nvme_find_path(struct nvme_ns_head *head)
{
	struct nvme_ns *ns;

	list_for_each_entry_rcu(ns, &head->list, siblings) {
		if (ns->ctrl->state == NVME_CTRL_LIVE) {
			rcu_assign_pointer(head->current_path, ns);
			return ns;
		}
	}

	return NULL;
}

inline struct nvme_ns *nvme_find_path(struct nvme_ns_head *head)
{
	struct nvme_ns *ns = srcu_dereference(head->current_path, &head->srcu);

	if (unlikely(!ns || ns->ctrl->state != NVME_CTRL_LIVE))
		ns = __nvme_find_path(head);
	return ns;
}

static blk_qc_t nvme_ns_head_make_request(struct request_queue *q,
		struct bio *bio)
{
	struct nvme_ns_head *head = q->queuedata;
	struct device *dev = disk_to_dev(head->disk);
	struct nvme_ns *ns;
	blk_qc_t ret = BLK_QC_T_NONE;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&head->srcu);
	ns = nvme_find_path(head);
	if (likely(ns)) {
		bio->bi_disk = ns->disk;
		bio->bi_opf |= REQ_NVME_MPATH;
		ret = direct_make_request(bio);
	} else if (!list_empty_careful(&head->list)) {
		dev_warn_ratelimited(dev, "no path available - requeuing I/O\n");

		spin_lock_irq(&head->requeue_lock);
		bio_list_add(&head->requeue_list, bio);
		spin_unlock_irq(&head->requeue_lock);
	} else {
		dev_warn_ratelimited(dev, "no path - failing I/O\n");

		bio->bi_status = BLK_STS_IOERR;
		bio_endio(bio);
	}

	srcu_read_unlock(&head->srcu, srcu_idx);
	return ret;
}

static bool nvme_ns_head_poll(struct request_queue *q, blk_qc_t qc)
{
	struct nvme_ns_head *head = q->queuedata;
	struct nvme_ns *ns;
	bool found = false;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&head->srcu);
	ns = srcu_dereference(head->current_path, &head->srcu);
	if (likely(ns && ns->ctrl->state == NVME_CTRL_LIVE))
		found = ns->queue->poll_fn(q, qc);
	srcu_read_unlock(&head->srcu, srcu_idx);
	return found;
}

static void nvme_requeue_work(struct work_struct *work)
{
	struct nvme_ns_head *head =
		container_of(work, struct nvme_ns_head, requeue_work);
	struct bio *bio, *next;

	spin_lock_irq(&head->requeue_lock);
	next = bio_list_get(&head->requeue_list);
	spin_unlock_irq(&head->requeue_lock);

	while ((bio = next) != NULL) {
		next = bio->bi_next;
		bio->bi_next = NULL;

		/*
		 * Reset disk to the mpath node and resubmit to select a new
		 * path.
		 */
		bio->bi_disk = head->disk;
		generic_make_request(bio);
	}
}

int nvme_mpath_alloc_disk(struct nvme_ctrl *ctrl, struct nvme_ns_head *head)
{
	struct request_queue *q;
	bool vwc = false;

	bio_list_init(&head->requeue_list);
	spin_lock_init(&head->requeue_lock);
	INIT_WORK(&head->requeue_work, nvme_requeue_work);

	/*
	 * Add a multipath node if the subsystems supports multiple controllers.
	 * We also do this for private namespaces as the namespace sharing data could
	 * change after a rescan.
	 */
	if (!(ctrl->subsys->cmic & (1 << 1)) || !multipath)
		return 0;

	q = blk_alloc_queue_node(GFP_KERNEL, NUMA_NO_NODE, NULL);
	if (!q)
		goto out;
	q->queuedata = head;
	blk_queue_make_request(q, nvme_ns_head_make_request);
	q->poll_fn = nvme_ns_head_poll;
	blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
	/* set to a default value for 512 until disk is validated */
	blk_queue_logical_block_size(q, 512);

	/* we need to propagate up the VMC settings */
	if (ctrl->vwc & NVME_CTRL_VWC_PRESENT)
		vwc = true;
	blk_queue_write_cache(q, vwc, vwc);

	head->disk = alloc_disk(0);
	if (!head->disk)
		goto out_cleanup_queue;
	head->disk->fops = &nvme_ns_head_ops;
	head->disk->private_data = head;
	head->disk->queue = q;
	head->disk->flags = GENHD_FL_EXT_DEVT;
	sprintf(head->disk->disk_name, "nvme%dn%d",
			ctrl->subsys->instance, head->instance);
	return 0;

out_cleanup_queue:
	blk_cleanup_queue(q);
out:
	return -ENOMEM;
}

void nvme_mpath_add_disk(struct nvme_ns_head *head)
{
	if (!head->disk)
		return;

	mutex_lock(&head->subsys->lock);
	if (!(head->disk->flags & GENHD_FL_UP)) {
		device_add_disk(&head->subsys->dev, head->disk);
		if (sysfs_create_group(&disk_to_dev(head->disk)->kobj,
				&nvme_ns_id_attr_group))
			pr_warn("%s: failed to create sysfs group for identification\n",
				head->disk->disk_name);
	}
	mutex_unlock(&head->subsys->lock);
}

void nvme_mpath_remove_disk(struct nvme_ns_head *head)
{
	if (!head->disk)
		return;
	sysfs_remove_group(&disk_to_dev(head->disk)->kobj,
			   &nvme_ns_id_attr_group);
	del_gendisk(head->disk);
	blk_set_queue_dying(head->disk->queue);
	/* make sure all pending bios are cleaned up */
	kblockd_schedule_work(&head->requeue_work);
	flush_work(&head->requeue_work);
	blk_cleanup_queue(head->disk->queue);
	put_disk(head->disk);
}

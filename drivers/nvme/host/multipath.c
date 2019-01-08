/*
 * Copyright (c) 2017-2018 Christoph Hellwig.
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
#include <trace/events/block.h>
#include "nvme.h"

static bool multipath = true;
module_param(multipath, bool, 0444);
MODULE_PARM_DESC(multipath,
	"turn on native support for multiple controllers per subsystem");

inline bool nvme_ctrl_use_ana(struct nvme_ctrl *ctrl)
{
	return multipath && ctrl->subsys && (ctrl->subsys->cmic & (1 << 3));
}

/*
 * If multipathing is enabled we need to always use the subsystem instance
 * number for numbering our devices to avoid conflicts between subsystems that
 * have multiple controllers and thus use the multipath-aware subsystem node
 * and those that have a single controller and use the controller node
 * directly.
 */
void nvme_set_disk_name(char *disk_name, struct nvme_ns *ns,
			struct nvme_ctrl *ctrl, int *flags)
{
	if (!multipath) {
		sprintf(disk_name, "nvme%dn%d", ctrl->instance, ns->head->instance);
	} else if (ns->head->disk) {
		sprintf(disk_name, "nvme%dc%dn%d", ctrl->subsys->instance,
				ctrl->cntlid, ns->head->instance);
		*flags = GENHD_FL_HIDDEN;
	} else {
		sprintf(disk_name, "nvme%dn%d", ctrl->subsys->instance,
				ns->head->instance);
	}
}

void nvme_failover_req(struct request *req)
{
	struct nvme_ns *ns = req->q->queuedata;
	u16 status = nvme_req(req)->status;
	unsigned long flags;

	spin_lock_irqsave(&ns->head->requeue_lock, flags);
	blk_steal_bios(&ns->head->requeue_list, req);
	spin_unlock_irqrestore(&ns->head->requeue_lock, flags);
	blk_mq_end_request(req, 0);

	switch (status & 0x7ff) {
	case NVME_SC_ANA_TRANSITION:
	case NVME_SC_ANA_INACCESSIBLE:
	case NVME_SC_ANA_PERSISTENT_LOSS:
		/*
		 * If we got back an ANA error we know the controller is alive,
		 * but not ready to serve this namespaces.  The spec suggests
		 * we should update our general state here, but due to the fact
		 * that the admin and I/O queues are not serialized that is
		 * fundamentally racy.  So instead just clear the current path,
		 * mark the the path as pending and kick of a re-read of the ANA
		 * log page ASAP.
		 */
		nvme_mpath_clear_current_path(ns);
		if (ns->ctrl->ana_log_buf) {
			set_bit(NVME_NS_ANA_PENDING, &ns->flags);
			queue_work(nvme_wq, &ns->ctrl->ana_work);
		}
		break;
	case NVME_SC_HOST_PATH_ERROR:
		/*
		 * Temporary transport disruption in talking to the controller.
		 * Try to send on a new path.
		 */
		nvme_mpath_clear_current_path(ns);
		break;
	default:
		/*
		 * Reset the controller for any non-ANA error as we don't know
		 * what caused the error.
		 */
		nvme_reset_ctrl(ns->ctrl);
		break;
	}

	kblockd_schedule_work(&ns->head->requeue_work);
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

static const char *nvme_ana_state_names[] = {
	[0]				= "invalid state",
	[NVME_ANA_OPTIMIZED]		= "optimized",
	[NVME_ANA_NONOPTIMIZED]		= "non-optimized",
	[NVME_ANA_INACCESSIBLE]		= "inaccessible",
	[NVME_ANA_PERSISTENT_LOSS]	= "persistent-loss",
	[NVME_ANA_CHANGE]		= "change",
};

void nvme_mpath_clear_current_path(struct nvme_ns *ns)
{
	struct nvme_ns_head *head = ns->head;
	int node;

	if (!head)
		return;

	for_each_node(node) {
		if (ns == rcu_access_pointer(head->current_path[node]))
			rcu_assign_pointer(head->current_path[node], NULL);
	}
}

static struct nvme_ns *__nvme_find_path(struct nvme_ns_head *head, int node)
{
	int found_distance = INT_MAX, fallback_distance = INT_MAX, distance;
	struct nvme_ns *found = NULL, *fallback = NULL, *ns;

	list_for_each_entry_rcu(ns, &head->list, siblings) {
		if (ns->ctrl->state != NVME_CTRL_LIVE ||
		    test_bit(NVME_NS_ANA_PENDING, &ns->flags))
			continue;

		distance = node_distance(node, ns->ctrl->numa_node);

		switch (ns->ana_state) {
		case NVME_ANA_OPTIMIZED:
			if (distance < found_distance) {
				found_distance = distance;
				found = ns;
			}
			break;
		case NVME_ANA_NONOPTIMIZED:
			if (distance < fallback_distance) {
				fallback_distance = distance;
				fallback = ns;
			}
			break;
		default:
			break;
		}
	}

	if (!found)
		found = fallback;
	if (found)
		rcu_assign_pointer(head->current_path[node], found);
	return found;
}

static inline bool nvme_path_is_optimized(struct nvme_ns *ns)
{
	return ns->ctrl->state == NVME_CTRL_LIVE &&
		ns->ana_state == NVME_ANA_OPTIMIZED;
}

inline struct nvme_ns *nvme_find_path(struct nvme_ns_head *head)
{
	int node = numa_node_id();
	struct nvme_ns *ns;

	ns = srcu_dereference(head->current_path[node], &head->srcu);
	if (unlikely(!ns || !nvme_path_is_optimized(ns)))
		ns = __nvme_find_path(head, node);
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
		trace_block_bio_remap(bio->bi_disk->queue, bio,
				      disk_devt(ns->head->disk),
				      bio->bi_iter.bi_sector);
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

	mutex_init(&head->lock);
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

	q = blk_alloc_queue_node(GFP_KERNEL, ctrl->numa_node);
	if (!q)
		goto out;
	q->queuedata = head;
	blk_queue_make_request(q, nvme_ns_head_make_request);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
	/* set to a default value for 512 until disk is validated */
	blk_queue_logical_block_size(q, 512);
	blk_set_stacking_limits(&q->limits);

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

static void nvme_mpath_set_live(struct nvme_ns *ns)
{
	struct nvme_ns_head *head = ns->head;

	lockdep_assert_held(&ns->head->lock);

	if (!head->disk)
		return;

	if (!(head->disk->flags & GENHD_FL_UP))
		device_add_disk(&head->subsys->dev, head->disk,
				nvme_ns_id_attr_groups);

	if (nvme_path_is_optimized(ns)) {
		int node, srcu_idx;

		srcu_idx = srcu_read_lock(&head->srcu);
		for_each_node(node)
			__nvme_find_path(head, node);
		srcu_read_unlock(&head->srcu, srcu_idx);
	}

	kblockd_schedule_work(&ns->head->requeue_work);
}

static int nvme_parse_ana_log(struct nvme_ctrl *ctrl, void *data,
		int (*cb)(struct nvme_ctrl *ctrl, struct nvme_ana_group_desc *,
			void *))
{
	void *base = ctrl->ana_log_buf;
	size_t offset = sizeof(struct nvme_ana_rsp_hdr);
	int error, i;

	lockdep_assert_held(&ctrl->ana_lock);

	for (i = 0; i < le16_to_cpu(ctrl->ana_log_buf->ngrps); i++) {
		struct nvme_ana_group_desc *desc = base + offset;
		u32 nr_nsids = le32_to_cpu(desc->nnsids);
		size_t nsid_buf_size = nr_nsids * sizeof(__le32);

		if (WARN_ON_ONCE(desc->grpid == 0))
			return -EINVAL;
		if (WARN_ON_ONCE(le32_to_cpu(desc->grpid) > ctrl->anagrpmax))
			return -EINVAL;
		if (WARN_ON_ONCE(desc->state == 0))
			return -EINVAL;
		if (WARN_ON_ONCE(desc->state > NVME_ANA_CHANGE))
			return -EINVAL;

		offset += sizeof(*desc);
		if (WARN_ON_ONCE(offset > ctrl->ana_log_size - nsid_buf_size))
			return -EINVAL;

		error = cb(ctrl, desc, data);
		if (error)
			return error;

		offset += nsid_buf_size;
		if (WARN_ON_ONCE(offset > ctrl->ana_log_size - sizeof(*desc)))
			return -EINVAL;
	}

	return 0;
}

static inline bool nvme_state_is_live(enum nvme_ana_state state)
{
	return state == NVME_ANA_OPTIMIZED || state == NVME_ANA_NONOPTIMIZED;
}

static void nvme_update_ns_ana_state(struct nvme_ana_group_desc *desc,
		struct nvme_ns *ns)
{
	enum nvme_ana_state old;

	mutex_lock(&ns->head->lock);
	old = ns->ana_state;
	ns->ana_grpid = le32_to_cpu(desc->grpid);
	ns->ana_state = desc->state;
	clear_bit(NVME_NS_ANA_PENDING, &ns->flags);

	if (nvme_state_is_live(ns->ana_state) && !nvme_state_is_live(old))
		nvme_mpath_set_live(ns);
	mutex_unlock(&ns->head->lock);
}

static int nvme_update_ana_state(struct nvme_ctrl *ctrl,
		struct nvme_ana_group_desc *desc, void *data)
{
	u32 nr_nsids = le32_to_cpu(desc->nnsids), n = 0;
	unsigned *nr_change_groups = data;
	struct nvme_ns *ns;

	dev_info(ctrl->device, "ANA group %d: %s.\n",
			le32_to_cpu(desc->grpid),
			nvme_ana_state_names[desc->state]);

	if (desc->state == NVME_ANA_CHANGE)
		(*nr_change_groups)++;

	if (!nr_nsids)
		return 0;

	down_write(&ctrl->namespaces_rwsem);
	list_for_each_entry(ns, &ctrl->namespaces, list) {
		if (ns->head->ns_id != le32_to_cpu(desc->nsids[n]))
			continue;
		nvme_update_ns_ana_state(desc, ns);
		if (++n == nr_nsids)
			break;
	}
	up_write(&ctrl->namespaces_rwsem);
	WARN_ON_ONCE(n < nr_nsids);
	return 0;
}

static int nvme_read_ana_log(struct nvme_ctrl *ctrl, bool groups_only)
{
	u32 nr_change_groups = 0;
	int error;

	mutex_lock(&ctrl->ana_lock);
	error = nvme_get_log(ctrl, NVME_NSID_ALL, NVME_LOG_ANA,
			groups_only ? NVME_ANA_LOG_RGO : 0,
			ctrl->ana_log_buf, ctrl->ana_log_size, 0);
	if (error) {
		dev_warn(ctrl->device, "Failed to get ANA log: %d\n", error);
		goto out_unlock;
	}

	error = nvme_parse_ana_log(ctrl, &nr_change_groups,
			nvme_update_ana_state);
	if (error)
		goto out_unlock;

	/*
	 * In theory we should have an ANATT timer per group as they might enter
	 * the change state at different times.  But that is a lot of overhead
	 * just to protect against a target that keeps entering new changes
	 * states while never finishing previous ones.  But we'll still
	 * eventually time out once all groups are in change state, so this
	 * isn't a big deal.
	 *
	 * We also double the ANATT value to provide some slack for transports
	 * or AEN processing overhead.
	 */
	if (nr_change_groups)
		mod_timer(&ctrl->anatt_timer, ctrl->anatt * HZ * 2 + jiffies);
	else
		del_timer_sync(&ctrl->anatt_timer);
out_unlock:
	mutex_unlock(&ctrl->ana_lock);
	return error;
}

static void nvme_ana_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl = container_of(work, struct nvme_ctrl, ana_work);

	nvme_read_ana_log(ctrl, false);
}

static void nvme_anatt_timeout(struct timer_list *t)
{
	struct nvme_ctrl *ctrl = from_timer(ctrl, t, anatt_timer);

	dev_info(ctrl->device, "ANATT timeout, resetting controller.\n");
	nvme_reset_ctrl(ctrl);
}

void nvme_mpath_stop(struct nvme_ctrl *ctrl)
{
	if (!nvme_ctrl_use_ana(ctrl))
		return;
	del_timer_sync(&ctrl->anatt_timer);
	cancel_work_sync(&ctrl->ana_work);
}

static ssize_t ana_grpid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", nvme_get_ns_from_dev(dev)->ana_grpid);
}
DEVICE_ATTR_RO(ana_grpid);

static ssize_t ana_state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);

	return sprintf(buf, "%s\n", nvme_ana_state_names[ns->ana_state]);
}
DEVICE_ATTR_RO(ana_state);

static int nvme_set_ns_ana_state(struct nvme_ctrl *ctrl,
		struct nvme_ana_group_desc *desc, void *data)
{
	struct nvme_ns *ns = data;

	if (ns->ana_grpid == le32_to_cpu(desc->grpid)) {
		nvme_update_ns_ana_state(desc, ns);
		return -ENXIO; /* just break out of the loop */
	}

	return 0;
}

void nvme_mpath_add_disk(struct nvme_ns *ns, struct nvme_id_ns *id)
{
	if (nvme_ctrl_use_ana(ns->ctrl)) {
		mutex_lock(&ns->ctrl->ana_lock);
		ns->ana_grpid = le32_to_cpu(id->anagrpid);
		nvme_parse_ana_log(ns->ctrl, ns, nvme_set_ns_ana_state);
		mutex_unlock(&ns->ctrl->ana_lock);
	} else {
		mutex_lock(&ns->head->lock);
		ns->ana_state = NVME_ANA_OPTIMIZED; 
		nvme_mpath_set_live(ns);
		mutex_unlock(&ns->head->lock);
	}
}

void nvme_mpath_remove_disk(struct nvme_ns_head *head)
{
	if (!head->disk)
		return;
	if (head->disk->flags & GENHD_FL_UP)
		del_gendisk(head->disk);
	blk_set_queue_dying(head->disk->queue);
	/* make sure all pending bios are cleaned up */
	kblockd_schedule_work(&head->requeue_work);
	flush_work(&head->requeue_work);
	blk_cleanup_queue(head->disk->queue);
	put_disk(head->disk);
}

int nvme_mpath_init(struct nvme_ctrl *ctrl, struct nvme_id_ctrl *id)
{
	int error;

	if (!nvme_ctrl_use_ana(ctrl))
		return 0;

	ctrl->anacap = id->anacap;
	ctrl->anatt = id->anatt;
	ctrl->nanagrpid = le32_to_cpu(id->nanagrpid);
	ctrl->anagrpmax = le32_to_cpu(id->anagrpmax);

	mutex_init(&ctrl->ana_lock);
	timer_setup(&ctrl->anatt_timer, nvme_anatt_timeout, 0);
	ctrl->ana_log_size = sizeof(struct nvme_ana_rsp_hdr) +
		ctrl->nanagrpid * sizeof(struct nvme_ana_group_desc);
	if (!(ctrl->anacap & (1 << 6)))
		ctrl->ana_log_size += ctrl->max_namespaces * sizeof(__le32);

	if (ctrl->ana_log_size > ctrl->max_hw_sectors << SECTOR_SHIFT) {
		dev_err(ctrl->device,
			"ANA log page size (%zd) larger than MDTS (%d).\n",
			ctrl->ana_log_size,
			ctrl->max_hw_sectors << SECTOR_SHIFT);
		dev_err(ctrl->device, "disabling ANA support.\n");
		return 0;
	}

	INIT_WORK(&ctrl->ana_work, nvme_ana_work);
	ctrl->ana_log_buf = kmalloc(ctrl->ana_log_size, GFP_KERNEL);
	if (!ctrl->ana_log_buf) {
		error = -ENOMEM;
		goto out;
	}

	error = nvme_read_ana_log(ctrl, true);
	if (error)
		goto out_free_ana_log_buf;
	return 0;
out_free_ana_log_buf:
	kfree(ctrl->ana_log_buf);
out:
	return error;
}

void nvme_mpath_uninit(struct nvme_ctrl *ctrl)
{
	kfree(ctrl->ana_log_buf);
}


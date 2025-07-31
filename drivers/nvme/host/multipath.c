// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2018 Christoph Hellwig.
 */

#include <linux/backing-dev.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <trace/events/block.h>
#include "nvme.h"

bool multipath = true;
static bool multipath_always_on;

static int multipath_param_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	bool *arg = kp->arg;

	ret = param_set_bool(val, kp);
	if (ret)
		return ret;

	if (multipath_always_on && !*arg) {
		pr_err("Can't disable multipath when multipath_always_on is configured.\n");
		*arg = true;
		return -EINVAL;
	}

	return 0;
}

static const struct kernel_param_ops multipath_param_ops = {
	.set = multipath_param_set,
	.get = param_get_bool,
};

module_param_cb(multipath, &multipath_param_ops, &multipath, 0444);
MODULE_PARM_DESC(multipath,
	"turn on native support for multiple controllers per subsystem");

static int multipath_always_on_set(const char *val,
		const struct kernel_param *kp)
{
	int ret;
	bool *arg = kp->arg;

	ret = param_set_bool(val, kp);
	if (ret < 0)
		return ret;

	if (*arg)
		multipath = true;

	return 0;
}

static const struct kernel_param_ops multipath_always_on_ops = {
	.set = multipath_always_on_set,
	.get = param_get_bool,
};

module_param_cb(multipath_always_on, &multipath_always_on_ops,
		&multipath_always_on, 0444);
MODULE_PARM_DESC(multipath_always_on,
	"create multipath node always except for private namespace with non-unique nsid; note that this also implicitly enables native multipath support");

static const char *nvme_iopolicy_names[] = {
	[NVME_IOPOLICY_NUMA]	= "numa",
	[NVME_IOPOLICY_RR]	= "round-robin",
	[NVME_IOPOLICY_QD]      = "queue-depth",
};

static int iopolicy = NVME_IOPOLICY_NUMA;

static int nvme_set_iopolicy(const char *val, const struct kernel_param *kp)
{
	if (!val)
		return -EINVAL;
	if (!strncmp(val, "numa", 4))
		iopolicy = NVME_IOPOLICY_NUMA;
	else if (!strncmp(val, "round-robin", 11))
		iopolicy = NVME_IOPOLICY_RR;
	else if (!strncmp(val, "queue-depth", 11))
		iopolicy = NVME_IOPOLICY_QD;
	else
		return -EINVAL;

	return 0;
}

static int nvme_get_iopolicy(char *buf, const struct kernel_param *kp)
{
	return sprintf(buf, "%s\n", nvme_iopolicy_names[iopolicy]);
}

module_param_call(iopolicy, nvme_set_iopolicy, nvme_get_iopolicy,
	&iopolicy, 0644);
MODULE_PARM_DESC(iopolicy,
	"Default multipath I/O policy; 'numa' (default), 'round-robin' or 'queue-depth'");

void nvme_mpath_default_iopolicy(struct nvme_subsystem *subsys)
{
	subsys->iopolicy = iopolicy;
}

void nvme_mpath_unfreeze(struct nvme_subsystem *subsys)
{
	struct nvme_ns_head *h;

	lockdep_assert_held(&subsys->lock);
	list_for_each_entry(h, &subsys->nsheads, entry)
		if (h->disk)
			blk_mq_unfreeze_queue_nomemrestore(h->disk->queue);
}

void nvme_mpath_wait_freeze(struct nvme_subsystem *subsys)
{
	struct nvme_ns_head *h;

	lockdep_assert_held(&subsys->lock);
	list_for_each_entry(h, &subsys->nsheads, entry)
		if (h->disk)
			blk_mq_freeze_queue_wait(h->disk->queue);
}

void nvme_mpath_start_freeze(struct nvme_subsystem *subsys)
{
	struct nvme_ns_head *h;

	lockdep_assert_held(&subsys->lock);
	list_for_each_entry(h, &subsys->nsheads, entry)
		if (h->disk)
			blk_freeze_queue_start(h->disk->queue);
}

void nvme_failover_req(struct request *req)
{
	struct nvme_ns *ns = req->q->queuedata;
	u16 status = nvme_req(req)->status & NVME_SCT_SC_MASK;
	unsigned long flags;
	struct bio *bio;

	nvme_mpath_clear_current_path(ns);

	/*
	 * If we got back an ANA error, we know the controller is alive but not
	 * ready to serve this namespace.  Kick of a re-read of the ANA
	 * information page, and just try any other available path for now.
	 */
	if (nvme_is_ana_error(status) && ns->ctrl->ana_log_buf) {
		set_bit(NVME_NS_ANA_PENDING, &ns->flags);
		queue_work(nvme_wq, &ns->ctrl->ana_work);
	}

	spin_lock_irqsave(&ns->head->requeue_lock, flags);
	for (bio = req->bio; bio; bio = bio->bi_next) {
		bio_set_dev(bio, ns->head->disk->part0);
		if (bio->bi_opf & REQ_POLLED) {
			bio->bi_opf &= ~REQ_POLLED;
			bio->bi_cookie = BLK_QC_T_NONE;
		}
		/*
		 * The alternate request queue that we may end up submitting
		 * the bio to may be frozen temporarily, in this case REQ_NOWAIT
		 * will fail the I/O immediately with EAGAIN to the issuer.
		 * We are not in the issuer context which cannot block. Clear
		 * the flag to avoid spurious EAGAIN I/O failures.
		 */
		bio->bi_opf &= ~REQ_NOWAIT;
	}
	blk_steal_bios(&ns->head->requeue_list, req);
	spin_unlock_irqrestore(&ns->head->requeue_lock, flags);

	nvme_req(req)->status = 0;
	nvme_end_req(req);
	kblockd_schedule_work(&ns->head->requeue_work);
}

void nvme_mpath_start_request(struct request *rq)
{
	struct nvme_ns *ns = rq->q->queuedata;
	struct gendisk *disk = ns->head->disk;

	if (READ_ONCE(ns->head->subsys->iopolicy) == NVME_IOPOLICY_QD) {
		atomic_inc(&ns->ctrl->nr_active);
		nvme_req(rq)->flags |= NVME_MPATH_CNT_ACTIVE;
	}

	if (!blk_queue_io_stat(disk->queue) || blk_rq_is_passthrough(rq))
		return;

	nvme_req(rq)->flags |= NVME_MPATH_IO_STATS;
	nvme_req(rq)->start_time = bdev_start_io_acct(disk->part0, req_op(rq),
						      jiffies);
}
EXPORT_SYMBOL_GPL(nvme_mpath_start_request);

void nvme_mpath_end_request(struct request *rq)
{
	struct nvme_ns *ns = rq->q->queuedata;

	if (nvme_req(rq)->flags & NVME_MPATH_CNT_ACTIVE)
		atomic_dec_if_positive(&ns->ctrl->nr_active);

	if (!(nvme_req(rq)->flags & NVME_MPATH_IO_STATS))
		return;
	bdev_end_io_acct(ns->head->disk->part0, req_op(rq),
			 blk_rq_bytes(rq) >> SECTOR_SHIFT,
			 nvme_req(rq)->start_time);
}

void nvme_kick_requeue_lists(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&ctrl->srcu);
	list_for_each_entry_srcu(ns, &ctrl->namespaces, list,
				 srcu_read_lock_held(&ctrl->srcu)) {
		if (!ns->head->disk)
			continue;
		kblockd_schedule_work(&ns->head->requeue_work);
		if (nvme_ctrl_state(ns->ctrl) == NVME_CTRL_LIVE)
			disk_uevent(ns->head->disk, KOBJ_CHANGE);
	}
	srcu_read_unlock(&ctrl->srcu, srcu_idx);
}

static const char *nvme_ana_state_names[] = {
	[0]				= "invalid state",
	[NVME_ANA_OPTIMIZED]		= "optimized",
	[NVME_ANA_NONOPTIMIZED]		= "non-optimized",
	[NVME_ANA_INACCESSIBLE]		= "inaccessible",
	[NVME_ANA_PERSISTENT_LOSS]	= "persistent-loss",
	[NVME_ANA_CHANGE]		= "change",
};

bool nvme_mpath_clear_current_path(struct nvme_ns *ns)
{
	struct nvme_ns_head *head = ns->head;
	bool changed = false;
	int node;

	if (!head)
		goto out;

	for_each_node(node) {
		if (ns == rcu_access_pointer(head->current_path[node])) {
			rcu_assign_pointer(head->current_path[node], NULL);
			changed = true;
		}
	}
out:
	return changed;
}

void nvme_mpath_clear_ctrl_paths(struct nvme_ctrl *ctrl)
{
	struct nvme_ns *ns;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&ctrl->srcu);
	list_for_each_entry_srcu(ns, &ctrl->namespaces, list,
				 srcu_read_lock_held(&ctrl->srcu)) {
		nvme_mpath_clear_current_path(ns);
		kblockd_schedule_work(&ns->head->requeue_work);
	}
	srcu_read_unlock(&ctrl->srcu, srcu_idx);
}

void nvme_mpath_revalidate_paths(struct nvme_ns *ns)
{
	struct nvme_ns_head *head = ns->head;
	sector_t capacity = get_capacity(head->disk);
	int node;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&head->srcu);
	list_for_each_entry_srcu(ns, &head->list, siblings,
				 srcu_read_lock_held(&head->srcu)) {
		if (capacity != get_capacity(ns->disk))
			clear_bit(NVME_NS_READY, &ns->flags);
	}
	srcu_read_unlock(&head->srcu, srcu_idx);

	for_each_node(node)
		rcu_assign_pointer(head->current_path[node], NULL);
	kblockd_schedule_work(&head->requeue_work);
}

static bool nvme_path_is_disabled(struct nvme_ns *ns)
{
	enum nvme_ctrl_state state = nvme_ctrl_state(ns->ctrl);

	/*
	 * We don't treat NVME_CTRL_DELETING as a disabled path as I/O should
	 * still be able to complete assuming that the controller is connected.
	 * Otherwise it will fail immediately and return to the requeue list.
	 */
	if (state != NVME_CTRL_LIVE && state != NVME_CTRL_DELETING)
		return true;
	if (test_bit(NVME_NS_ANA_PENDING, &ns->flags) ||
	    !test_bit(NVME_NS_READY, &ns->flags))
		return true;
	return false;
}

static struct nvme_ns *__nvme_find_path(struct nvme_ns_head *head, int node)
{
	int found_distance = INT_MAX, fallback_distance = INT_MAX, distance;
	struct nvme_ns *found = NULL, *fallback = NULL, *ns;

	list_for_each_entry_srcu(ns, &head->list, siblings,
				 srcu_read_lock_held(&head->srcu)) {
		if (nvme_path_is_disabled(ns))
			continue;

		if (ns->ctrl->numa_node != NUMA_NO_NODE &&
		    READ_ONCE(head->subsys->iopolicy) == NVME_IOPOLICY_NUMA)
			distance = node_distance(node, ns->ctrl->numa_node);
		else
			distance = LOCAL_DISTANCE;

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

static struct nvme_ns *nvme_next_ns(struct nvme_ns_head *head,
		struct nvme_ns *ns)
{
	ns = list_next_or_null_rcu(&head->list, &ns->siblings, struct nvme_ns,
			siblings);
	if (ns)
		return ns;
	return list_first_or_null_rcu(&head->list, struct nvme_ns, siblings);
}

static struct nvme_ns *nvme_round_robin_path(struct nvme_ns_head *head)
{
	struct nvme_ns *ns, *found = NULL;
	int node = numa_node_id();
	struct nvme_ns *old = srcu_dereference(head->current_path[node],
					       &head->srcu);

	if (unlikely(!old))
		return __nvme_find_path(head, node);

	if (list_is_singular(&head->list)) {
		if (nvme_path_is_disabled(old))
			return NULL;
		return old;
	}

	for (ns = nvme_next_ns(head, old);
	     ns && ns != old;
	     ns = nvme_next_ns(head, ns)) {
		if (nvme_path_is_disabled(ns))
			continue;

		if (ns->ana_state == NVME_ANA_OPTIMIZED) {
			found = ns;
			goto out;
		}
		if (ns->ana_state == NVME_ANA_NONOPTIMIZED)
			found = ns;
	}

	/*
	 * The loop above skips the current path for round-robin semantics.
	 * Fall back to the current path if either:
	 *  - no other optimized path found and current is optimized,
	 *  - no other usable path found and current is usable.
	 */
	if (!nvme_path_is_disabled(old) &&
	    (old->ana_state == NVME_ANA_OPTIMIZED ||
	     (!found && old->ana_state == NVME_ANA_NONOPTIMIZED)))
		return old;

	if (!found)
		return NULL;
out:
	rcu_assign_pointer(head->current_path[node], found);
	return found;
}

static struct nvme_ns *nvme_queue_depth_path(struct nvme_ns_head *head)
{
	struct nvme_ns *best_opt = NULL, *best_nonopt = NULL, *ns;
	unsigned int min_depth_opt = UINT_MAX, min_depth_nonopt = UINT_MAX;
	unsigned int depth;

	list_for_each_entry_srcu(ns, &head->list, siblings,
				 srcu_read_lock_held(&head->srcu)) {
		if (nvme_path_is_disabled(ns))
			continue;

		depth = atomic_read(&ns->ctrl->nr_active);

		switch (ns->ana_state) {
		case NVME_ANA_OPTIMIZED:
			if (depth < min_depth_opt) {
				min_depth_opt = depth;
				best_opt = ns;
			}
			break;
		case NVME_ANA_NONOPTIMIZED:
			if (depth < min_depth_nonopt) {
				min_depth_nonopt = depth;
				best_nonopt = ns;
			}
			break;
		default:
			break;
		}

		if (min_depth_opt == 0)
			return best_opt;
	}

	return best_opt ? best_opt : best_nonopt;
}

static inline bool nvme_path_is_optimized(struct nvme_ns *ns)
{
	return nvme_ctrl_state(ns->ctrl) == NVME_CTRL_LIVE &&
		ns->ana_state == NVME_ANA_OPTIMIZED;
}

static struct nvme_ns *nvme_numa_path(struct nvme_ns_head *head)
{
	int node = numa_node_id();
	struct nvme_ns *ns;

	ns = srcu_dereference(head->current_path[node], &head->srcu);
	if (unlikely(!ns))
		return __nvme_find_path(head, node);
	if (unlikely(!nvme_path_is_optimized(ns)))
		return __nvme_find_path(head, node);
	return ns;
}

inline struct nvme_ns *nvme_find_path(struct nvme_ns_head *head)
{
	switch (READ_ONCE(head->subsys->iopolicy)) {
	case NVME_IOPOLICY_QD:
		return nvme_queue_depth_path(head);
	case NVME_IOPOLICY_RR:
		return nvme_round_robin_path(head);
	default:
		return nvme_numa_path(head);
	}
}

static bool nvme_available_path(struct nvme_ns_head *head)
{
	struct nvme_ns *ns;

	if (!test_bit(NVME_NSHEAD_DISK_LIVE, &head->flags))
		return false;

	list_for_each_entry_srcu(ns, &head->list, siblings,
				 srcu_read_lock_held(&head->srcu)) {
		if (test_bit(NVME_CTRL_FAILFAST_EXPIRED, &ns->ctrl->flags))
			continue;
		switch (nvme_ctrl_state(ns->ctrl)) {
		case NVME_CTRL_LIVE:
		case NVME_CTRL_RESETTING:
		case NVME_CTRL_CONNECTING:
			return true;
		default:
			break;
		}
	}

	/*
	 * If "head->delayed_removal_secs" is configured (i.e., non-zero), do
	 * not immediately fail I/O. Instead, requeue the I/O for the configured
	 * duration, anticipating that if there's a transient link failure then
	 * it may recover within this time window. This parameter is exported to
	 * userspace via sysfs, and its default value is zero. It is internally
	 * mapped to NVME_NSHEAD_QUEUE_IF_NO_PATH. When delayed_removal_secs is
	 * non-zero, this flag is set to true. When zero, the flag is cleared.
	 */
	return nvme_mpath_queue_if_no_path(head);
}

static void nvme_ns_head_submit_bio(struct bio *bio)
{
	struct nvme_ns_head *head = bio->bi_bdev->bd_disk->private_data;
	struct device *dev = disk_to_dev(head->disk);
	struct nvme_ns *ns;
	int srcu_idx;

	/*
	 * The namespace might be going away and the bio might be moved to a
	 * different queue via blk_steal_bios(), so we need to use the bio_split
	 * pool from the original queue to allocate the bvecs from.
	 */
	bio = bio_split_to_limits(bio);
	if (!bio)
		return;

	srcu_idx = srcu_read_lock(&head->srcu);
	ns = nvme_find_path(head);
	if (likely(ns)) {
		bio_set_dev(bio, ns->disk->part0);
		bio->bi_opf |= REQ_NVME_MPATH;
		trace_block_bio_remap(bio, disk_devt(ns->head->disk),
				      bio->bi_iter.bi_sector);
		submit_bio_noacct(bio);
	} else if (nvme_available_path(head)) {
		dev_warn_ratelimited(dev, "no usable path - requeuing I/O\n");

		spin_lock_irq(&head->requeue_lock);
		bio_list_add(&head->requeue_list, bio);
		spin_unlock_irq(&head->requeue_lock);
	} else {
		dev_warn_ratelimited(dev, "no available path - failing I/O\n");

		bio_io_error(bio);
	}

	srcu_read_unlock(&head->srcu, srcu_idx);
}

static int nvme_ns_head_open(struct gendisk *disk, blk_mode_t mode)
{
	if (!nvme_tryget_ns_head(disk->private_data))
		return -ENXIO;
	return 0;
}

static void nvme_ns_head_release(struct gendisk *disk)
{
	nvme_put_ns_head(disk->private_data);
}

static int nvme_ns_head_get_unique_id(struct gendisk *disk, u8 id[16],
		enum blk_unique_id type)
{
	struct nvme_ns_head *head = disk->private_data;
	struct nvme_ns *ns;
	int srcu_idx, ret = -EWOULDBLOCK;

	srcu_idx = srcu_read_lock(&head->srcu);
	ns = nvme_find_path(head);
	if (ns)
		ret = nvme_ns_get_unique_id(ns, id, type);
	srcu_read_unlock(&head->srcu, srcu_idx);
	return ret;
}

#ifdef CONFIG_BLK_DEV_ZONED
static int nvme_ns_head_report_zones(struct gendisk *disk, sector_t sector,
		unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct nvme_ns_head *head = disk->private_data;
	struct nvme_ns *ns;
	int srcu_idx, ret = -EWOULDBLOCK;

	srcu_idx = srcu_read_lock(&head->srcu);
	ns = nvme_find_path(head);
	if (ns)
		ret = nvme_ns_report_zones(ns, sector, nr_zones, cb, data);
	srcu_read_unlock(&head->srcu, srcu_idx);
	return ret;
}
#else
#define nvme_ns_head_report_zones	NULL
#endif /* CONFIG_BLK_DEV_ZONED */

const struct block_device_operations nvme_ns_head_ops = {
	.owner		= THIS_MODULE,
	.submit_bio	= nvme_ns_head_submit_bio,
	.open		= nvme_ns_head_open,
	.release	= nvme_ns_head_release,
	.ioctl		= nvme_ns_head_ioctl,
	.compat_ioctl	= blkdev_compat_ptr_ioctl,
	.getgeo		= nvme_getgeo,
	.get_unique_id	= nvme_ns_head_get_unique_id,
	.report_zones	= nvme_ns_head_report_zones,
	.pr_ops		= &nvme_pr_ops,
};

static inline struct nvme_ns_head *cdev_to_ns_head(struct cdev *cdev)
{
	return container_of(cdev, struct nvme_ns_head, cdev);
}

static int nvme_ns_head_chr_open(struct inode *inode, struct file *file)
{
	if (!nvme_tryget_ns_head(cdev_to_ns_head(inode->i_cdev)))
		return -ENXIO;
	return 0;
}

static int nvme_ns_head_chr_release(struct inode *inode, struct file *file)
{
	nvme_put_ns_head(cdev_to_ns_head(inode->i_cdev));
	return 0;
}

static const struct file_operations nvme_ns_head_chr_fops = {
	.owner		= THIS_MODULE,
	.open		= nvme_ns_head_chr_open,
	.release	= nvme_ns_head_chr_release,
	.unlocked_ioctl	= nvme_ns_head_chr_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.uring_cmd	= nvme_ns_head_chr_uring_cmd,
	.uring_cmd_iopoll = nvme_ns_chr_uring_cmd_iopoll,
};

static int nvme_add_ns_head_cdev(struct nvme_ns_head *head)
{
	int ret;

	head->cdev_device.parent = &head->subsys->dev;
	ret = dev_set_name(&head->cdev_device, "ng%dn%d",
			   head->subsys->instance, head->instance);
	if (ret)
		return ret;
	ret = nvme_cdev_add(&head->cdev, &head->cdev_device,
			    &nvme_ns_head_chr_fops, THIS_MODULE);
	return ret;
}

static void nvme_partition_scan_work(struct work_struct *work)
{
	struct nvme_ns_head *head =
		container_of(work, struct nvme_ns_head, partition_scan_work);

	if (WARN_ON_ONCE(!test_and_clear_bit(GD_SUPPRESS_PART_SCAN,
					     &head->disk->state)))
		return;

	mutex_lock(&head->disk->open_mutex);
	bdev_disk_changed(head->disk, false);
	mutex_unlock(&head->disk->open_mutex);
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

		submit_bio_noacct(bio);
	}
}

static void nvme_remove_head(struct nvme_ns_head *head)
{
	if (test_and_clear_bit(NVME_NSHEAD_DISK_LIVE, &head->flags)) {
		/*
		 * requeue I/O after NVME_NSHEAD_DISK_LIVE has been cleared
		 * to allow multipath to fail all I/O.
		 */
		kblockd_schedule_work(&head->requeue_work);

		nvme_cdev_del(&head->cdev, &head->cdev_device);
		synchronize_srcu(&head->srcu);
		del_gendisk(head->disk);
		nvme_put_ns_head(head);
	}
}

static void nvme_remove_head_work(struct work_struct *work)
{
	struct nvme_ns_head *head = container_of(to_delayed_work(work),
			struct nvme_ns_head, remove_work);
	bool remove = false;

	mutex_lock(&head->subsys->lock);
	if (list_empty(&head->list)) {
		list_del_init(&head->entry);
		remove = true;
	}
	mutex_unlock(&head->subsys->lock);
	if (remove)
		nvme_remove_head(head);

	module_put(THIS_MODULE);
}

int nvme_mpath_alloc_disk(struct nvme_ctrl *ctrl, struct nvme_ns_head *head)
{
	struct queue_limits lim;

	mutex_init(&head->lock);
	bio_list_init(&head->requeue_list);
	spin_lock_init(&head->requeue_lock);
	INIT_WORK(&head->requeue_work, nvme_requeue_work);
	INIT_WORK(&head->partition_scan_work, nvme_partition_scan_work);
	INIT_DELAYED_WORK(&head->remove_work, nvme_remove_head_work);
	head->delayed_removal_secs = 0;

	/*
	 * If "multipath_always_on" is enabled, a multipath node is added
	 * regardless of whether the disk is single/multi ported, and whether
	 * the namespace is shared or private. If "multipath_always_on" is not
	 * enabled, a multipath node is added only if the subsystem supports
	 * multiple controllers and the "multipath" option is configured. In
	 * either case, for private namespaces, we ensure that the NSID is
	 * unique.
	 */
	if (!multipath_always_on) {
		if (!(ctrl->subsys->cmic & NVME_CTRL_CMIC_MULTI_CTRL) ||
				!multipath)
			return 0;
	}

	if (!nvme_is_unique_nsid(ctrl, head))
		return 0;

	blk_set_stacking_limits(&lim);
	lim.dma_alignment = 3;
	lim.features |= BLK_FEAT_IO_STAT | BLK_FEAT_NOWAIT |
		BLK_FEAT_POLL | BLK_FEAT_ATOMIC_WRITES;
	if (head->ids.csi == NVME_CSI_ZNS)
		lim.features |= BLK_FEAT_ZONED;

	head->disk = blk_alloc_disk(&lim, ctrl->numa_node);
	if (IS_ERR(head->disk))
		return PTR_ERR(head->disk);
	head->disk->fops = &nvme_ns_head_ops;
	head->disk->private_data = head;

	/*
	 * We need to suppress the partition scan from occuring within the
	 * controller's scan_work context. If a path error occurs here, the IO
	 * will wait until a path becomes available or all paths are torn down,
	 * but that action also occurs within scan_work, so it would deadlock.
	 * Defer the partition scan to a different context that does not block
	 * scan_work.
	 */
	set_bit(GD_SUPPRESS_PART_SCAN, &head->disk->state);
	sprintf(head->disk->disk_name, "nvme%dn%d",
			ctrl->subsys->instance, head->instance);
	nvme_tryget_ns_head(head);
	return 0;
}

static void nvme_mpath_set_live(struct nvme_ns *ns)
{
	struct nvme_ns_head *head = ns->head;
	int rc;

	if (!head->disk)
		return;

	/*
	 * test_and_set_bit() is used because it is protecting against two nvme
	 * paths simultaneously calling device_add_disk() on the same namespace
	 * head.
	 */
	if (!test_and_set_bit(NVME_NSHEAD_DISK_LIVE, &head->flags)) {
		rc = device_add_disk(&head->subsys->dev, head->disk,
				     nvme_ns_attr_groups);
		if (rc) {
			clear_bit(NVME_NSHEAD_DISK_LIVE, &head->flags);
			return;
		}
		nvme_add_ns_head_cdev(head);
		kblockd_schedule_work(&head->partition_scan_work);
	}

	nvme_mpath_add_sysfs_link(ns->head);

	mutex_lock(&head->lock);
	if (nvme_path_is_optimized(ns)) {
		int node, srcu_idx;

		srcu_idx = srcu_read_lock(&head->srcu);
		for_each_online_node(node)
			__nvme_find_path(head, node);
		srcu_read_unlock(&head->srcu, srcu_idx);
	}
	mutex_unlock(&head->lock);

	synchronize_srcu(&head->srcu);
	kblockd_schedule_work(&head->requeue_work);
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
		u32 nr_nsids;
		size_t nsid_buf_size;

		if (WARN_ON_ONCE(offset > ctrl->ana_log_size - sizeof(*desc)))
			return -EINVAL;

		nr_nsids = le32_to_cpu(desc->nnsids);
		nsid_buf_size = flex_array_size(desc, nsids, nr_nsids);

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
	ns->ana_grpid = le32_to_cpu(desc->grpid);
	ns->ana_state = desc->state;
	clear_bit(NVME_NS_ANA_PENDING, &ns->flags);
	/*
	 * nvme_mpath_set_live() will trigger I/O to the multipath path device
	 * and in turn to this path device.  However we cannot accept this I/O
	 * if the controller is not live.  This may deadlock if called from
	 * nvme_mpath_init_identify() and the ctrl will never complete
	 * initialization, preventing I/O from completing.  For this case we
	 * will reprocess the ANA log page in nvme_mpath_update() once the
	 * controller is ready.
	 */
	if (nvme_state_is_live(ns->ana_state) &&
	    nvme_ctrl_state(ns->ctrl) == NVME_CTRL_LIVE)
		nvme_mpath_set_live(ns);
	else {
		/*
		 * Add sysfs link from multipath head gendisk node to path
		 * device gendisk node.
		 * If path's ana state is live (i.e. state is either optimized
		 * or non-optimized) while we alloc the ns then sysfs link would
		 * be created from nvme_mpath_set_live(). In that case we would
		 * not fallthrough this code path. However for the path's ana
		 * state other than live, we call nvme_mpath_set_live() only
		 * after ana state transitioned to the live state. But we still
		 * want to create the sysfs link from head node to a path device
		 * irrespctive of the path's ana state.
		 * If we reach through here then it means that path's ana state
		 * is not live but still create the sysfs link to this path from
		 * head node if head node of the path has already come alive.
		 */
		if (test_bit(NVME_NSHEAD_DISK_LIVE, &ns->head->flags))
			nvme_mpath_add_sysfs_link(ns->head);
	}
}

static int nvme_update_ana_state(struct nvme_ctrl *ctrl,
		struct nvme_ana_group_desc *desc, void *data)
{
	u32 nr_nsids = le32_to_cpu(desc->nnsids), n = 0;
	unsigned *nr_change_groups = data;
	struct nvme_ns *ns;
	int srcu_idx;

	dev_dbg(ctrl->device, "ANA group %d: %s.\n",
			le32_to_cpu(desc->grpid),
			nvme_ana_state_names[desc->state]);

	if (desc->state == NVME_ANA_CHANGE)
		(*nr_change_groups)++;

	if (!nr_nsids)
		return 0;

	srcu_idx = srcu_read_lock(&ctrl->srcu);
	list_for_each_entry_srcu(ns, &ctrl->namespaces, list,
				 srcu_read_lock_held(&ctrl->srcu)) {
		unsigned nsid;
again:
		nsid = le32_to_cpu(desc->nsids[n]);
		if (ns->head->ns_id < nsid)
			continue;
		if (ns->head->ns_id == nsid)
			nvme_update_ns_ana_state(desc, ns);
		if (++n == nr_nsids)
			break;
		if (ns->head->ns_id > nsid)
			goto again;
	}
	srcu_read_unlock(&ctrl->srcu, srcu_idx);
	return 0;
}

static int nvme_read_ana_log(struct nvme_ctrl *ctrl)
{
	u32 nr_change_groups = 0;
	int error;

	mutex_lock(&ctrl->ana_lock);
	error = nvme_get_log(ctrl, NVME_NSID_ALL, NVME_LOG_ANA, 0, NVME_CSI_NVM,
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
		timer_delete_sync(&ctrl->anatt_timer);
out_unlock:
	mutex_unlock(&ctrl->ana_lock);
	return error;
}

static void nvme_ana_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl = container_of(work, struct nvme_ctrl, ana_work);

	if (nvme_ctrl_state(ctrl) != NVME_CTRL_LIVE)
		return;

	nvme_read_ana_log(ctrl);
}

void nvme_mpath_update(struct nvme_ctrl *ctrl)
{
	u32 nr_change_groups = 0;

	if (!ctrl->ana_log_buf)
		return;

	mutex_lock(&ctrl->ana_lock);
	nvme_parse_ana_log(ctrl, &nr_change_groups, nvme_update_ana_state);
	mutex_unlock(&ctrl->ana_lock);
}

static void nvme_anatt_timeout(struct timer_list *t)
{
	struct nvme_ctrl *ctrl = timer_container_of(ctrl, t, anatt_timer);

	dev_info(ctrl->device, "ANATT timeout, resetting controller.\n");
	nvme_reset_ctrl(ctrl);
}

void nvme_mpath_stop(struct nvme_ctrl *ctrl)
{
	if (!nvme_ctrl_use_ana(ctrl))
		return;
	timer_delete_sync(&ctrl->anatt_timer);
	cancel_work_sync(&ctrl->ana_work);
}

#define SUBSYS_ATTR_RW(_name, _mode, _show, _store)  \
	struct device_attribute subsys_attr_##_name =	\
		__ATTR(_name, _mode, _show, _store)

static ssize_t nvme_subsys_iopolicy_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_subsystem *subsys =
		container_of(dev, struct nvme_subsystem, dev);

	return sysfs_emit(buf, "%s\n",
			  nvme_iopolicy_names[READ_ONCE(subsys->iopolicy)]);
}

static void nvme_subsys_iopolicy_update(struct nvme_subsystem *subsys,
		int iopolicy)
{
	struct nvme_ctrl *ctrl;
	int old_iopolicy = READ_ONCE(subsys->iopolicy);

	if (old_iopolicy == iopolicy)
		return;

	WRITE_ONCE(subsys->iopolicy, iopolicy);

	/* iopolicy changes clear the mpath by design */
	mutex_lock(&nvme_subsystems_lock);
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry)
		nvme_mpath_clear_ctrl_paths(ctrl);
	mutex_unlock(&nvme_subsystems_lock);

	pr_notice("subsysnqn %s iopolicy changed from %s to %s\n",
			subsys->subnqn,
			nvme_iopolicy_names[old_iopolicy],
			nvme_iopolicy_names[iopolicy]);
}

static ssize_t nvme_subsys_iopolicy_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct nvme_subsystem *subsys =
		container_of(dev, struct nvme_subsystem, dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(nvme_iopolicy_names); i++) {
		if (sysfs_streq(buf, nvme_iopolicy_names[i])) {
			nvme_subsys_iopolicy_update(subsys, i);
			return count;
		}
	}

	return -EINVAL;
}
SUBSYS_ATTR_RW(iopolicy, S_IRUGO | S_IWUSR,
		      nvme_subsys_iopolicy_show, nvme_subsys_iopolicy_store);

static ssize_t ana_grpid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "%d\n", nvme_get_ns_from_dev(dev)->ana_grpid);
}
DEVICE_ATTR_RO(ana_grpid);

static ssize_t ana_state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);

	return sysfs_emit(buf, "%s\n", nvme_ana_state_names[ns->ana_state]);
}
DEVICE_ATTR_RO(ana_state);

static ssize_t queue_depth_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);

	if (ns->head->subsys->iopolicy != NVME_IOPOLICY_QD)
		return 0;

	return sysfs_emit(buf, "%d\n", atomic_read(&ns->ctrl->nr_active));
}
DEVICE_ATTR_RO(queue_depth);

static ssize_t numa_nodes_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int node, srcu_idx;
	nodemask_t numa_nodes;
	struct nvme_ns *current_ns;
	struct nvme_ns *ns = nvme_get_ns_from_dev(dev);
	struct nvme_ns_head *head = ns->head;

	if (head->subsys->iopolicy != NVME_IOPOLICY_NUMA)
		return 0;

	nodes_clear(numa_nodes);

	srcu_idx = srcu_read_lock(&head->srcu);
	for_each_node(node) {
		current_ns = srcu_dereference(head->current_path[node],
				&head->srcu);
		if (ns == current_ns)
			node_set(node, numa_nodes);
	}
	srcu_read_unlock(&head->srcu, srcu_idx);

	return sysfs_emit(buf, "%*pbl\n", nodemask_pr_args(&numa_nodes));
}
DEVICE_ATTR_RO(numa_nodes);

static ssize_t delayed_removal_secs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct nvme_ns_head *head = disk->private_data;
	int ret;

	mutex_lock(&head->subsys->lock);
	ret = sysfs_emit(buf, "%u\n", head->delayed_removal_secs);
	mutex_unlock(&head->subsys->lock);
	return ret;
}

static ssize_t delayed_removal_secs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct nvme_ns_head *head = disk->private_data;
	unsigned int sec;
	int ret;

	ret = kstrtouint(buf, 0, &sec);
	if (ret < 0)
		return ret;

	mutex_lock(&head->subsys->lock);
	head->delayed_removal_secs = sec;
	if (sec)
		set_bit(NVME_NSHEAD_QUEUE_IF_NO_PATH, &head->flags);
	else
		clear_bit(NVME_NSHEAD_QUEUE_IF_NO_PATH, &head->flags);
	mutex_unlock(&head->subsys->lock);
	/*
	 * Ensure that update to NVME_NSHEAD_QUEUE_IF_NO_PATH is seen
	 * by its reader.
	 */
	synchronize_srcu(&head->srcu);

	return count;
}

DEVICE_ATTR_RW(delayed_removal_secs);

static int nvme_lookup_ana_group_desc(struct nvme_ctrl *ctrl,
		struct nvme_ana_group_desc *desc, void *data)
{
	struct nvme_ana_group_desc *dst = data;

	if (desc->grpid != dst->grpid)
		return 0;

	*dst = *desc;
	return -ENXIO; /* just break out of the loop */
}

void nvme_mpath_add_sysfs_link(struct nvme_ns_head *head)
{
	struct device *target;
	int rc, srcu_idx;
	struct nvme_ns *ns;
	struct kobject *kobj;

	/*
	 * Ensure head disk node is already added otherwise we may get invalid
	 * kobj for head disk node
	 */
	if (!test_bit(GD_ADDED, &head->disk->state))
		return;

	kobj = &disk_to_dev(head->disk)->kobj;

	/*
	 * loop through each ns chained through the head->list and create the
	 * sysfs link from head node to the ns path node
	 */
	srcu_idx = srcu_read_lock(&head->srcu);

	list_for_each_entry_rcu(ns, &head->list, siblings) {
		/*
		 * Ensure that ns path disk node is already added otherwise we
		 * may get invalid kobj name for target
		 */
		if (!test_bit(GD_ADDED, &ns->disk->state))
			continue;

		/*
		 * Avoid creating link if it already exists for the given path.
		 * When path ana state transitions from optimized to non-
		 * optimized or vice-versa, the nvme_mpath_set_live() is
		 * invoked which in truns call this function. Now if the sysfs
		 * link already exists for the given path and we attempt to re-
		 * create the link then sysfs code would warn about it loudly.
		 * So we evaluate NVME_NS_SYSFS_ATTR_LINK flag here to ensure
		 * that we're not creating duplicate link.
		 * The test_and_set_bit() is used because it is protecting
		 * against multiple nvme paths being simultaneously added.
		 */
		if (test_and_set_bit(NVME_NS_SYSFS_ATTR_LINK, &ns->flags))
			continue;

		target = disk_to_dev(ns->disk);
		/*
		 * Create sysfs link from head gendisk kobject @kobj to the
		 * ns path gendisk kobject @target->kobj.
		 */
		rc = sysfs_add_link_to_group(kobj, nvme_ns_mpath_attr_group.name,
				&target->kobj, dev_name(target));
		if (unlikely(rc)) {
			dev_err(disk_to_dev(ns->head->disk),
					"failed to create link to %s\n",
					dev_name(target));
			clear_bit(NVME_NS_SYSFS_ATTR_LINK, &ns->flags);
		}
	}

	srcu_read_unlock(&head->srcu, srcu_idx);
}

void nvme_mpath_remove_sysfs_link(struct nvme_ns *ns)
{
	struct device *target;
	struct kobject *kobj;

	if (!test_bit(NVME_NS_SYSFS_ATTR_LINK, &ns->flags))
		return;

	target = disk_to_dev(ns->disk);
	kobj = &disk_to_dev(ns->head->disk)->kobj;
	sysfs_remove_link_from_group(kobj, nvme_ns_mpath_attr_group.name,
			dev_name(target));
	clear_bit(NVME_NS_SYSFS_ATTR_LINK, &ns->flags);
}

void nvme_mpath_add_disk(struct nvme_ns *ns, __le32 anagrpid)
{
	if (nvme_ctrl_use_ana(ns->ctrl)) {
		struct nvme_ana_group_desc desc = {
			.grpid = anagrpid,
			.state = 0,
		};

		mutex_lock(&ns->ctrl->ana_lock);
		ns->ana_grpid = le32_to_cpu(anagrpid);
		nvme_parse_ana_log(ns->ctrl, &desc, nvme_lookup_ana_group_desc);
		mutex_unlock(&ns->ctrl->ana_lock);
		if (desc.state) {
			/* found the group desc: update */
			nvme_update_ns_ana_state(&desc, ns);
		} else {
			/* group desc not found: trigger a re-read */
			set_bit(NVME_NS_ANA_PENDING, &ns->flags);
			queue_work(nvme_wq, &ns->ctrl->ana_work);
		}
	} else {
		ns->ana_state = NVME_ANA_OPTIMIZED;
		nvme_mpath_set_live(ns);
	}

#ifdef CONFIG_BLK_DEV_ZONED
	if (blk_queue_is_zoned(ns->queue) && ns->head->disk)
		ns->head->disk->nr_zones = ns->disk->nr_zones;
#endif
}

void nvme_mpath_remove_disk(struct nvme_ns_head *head)
{
	bool remove = false;

	mutex_lock(&head->subsys->lock);
	/*
	 * We are called when all paths have been removed, and at that point
	 * head->list is expected to be empty. However, nvme_remove_ns() and
	 * nvme_init_ns_head() can run concurrently and so if head->delayed_
	 * removal_secs is configured, it is possible that by the time we reach
	 * this point, head->list may no longer be empty. Therefore, we recheck
	 * head->list here. If it is no longer empty then we skip enqueuing the
	 * delayed head removal work.
	 */
	if (!list_empty(&head->list))
		goto out;

	if (head->delayed_removal_secs) {
		/*
		 * Ensure that no one could remove this module while the head
		 * remove work is pending.
		 */
		if (!try_module_get(THIS_MODULE))
			goto out;
		mod_delayed_work(nvme_wq, &head->remove_work,
				head->delayed_removal_secs * HZ);
	} else {
		list_del_init(&head->entry);
		remove = true;
	}
out:
	mutex_unlock(&head->subsys->lock);
	if (remove)
		nvme_remove_head(head);
}

void nvme_mpath_put_disk(struct nvme_ns_head *head)
{
	if (!head->disk)
		return;
	/* make sure all pending bios are cleaned up */
	kblockd_schedule_work(&head->requeue_work);
	flush_work(&head->requeue_work);
	flush_work(&head->partition_scan_work);
	put_disk(head->disk);
}

void nvme_mpath_init_ctrl(struct nvme_ctrl *ctrl)
{
	mutex_init(&ctrl->ana_lock);
	timer_setup(&ctrl->anatt_timer, nvme_anatt_timeout, 0);
	INIT_WORK(&ctrl->ana_work, nvme_ana_work);
}

int nvme_mpath_init_identify(struct nvme_ctrl *ctrl, struct nvme_id_ctrl *id)
{
	size_t max_transfer_size = ctrl->max_hw_sectors << SECTOR_SHIFT;
	size_t ana_log_size;
	int error = 0;

	/* check if multipath is enabled and we have the capability */
	if (!multipath || !ctrl->subsys ||
	    !(ctrl->subsys->cmic & NVME_CTRL_CMIC_ANA))
		return 0;

	/* initialize this in the identify path to cover controller resets */
	atomic_set(&ctrl->nr_active, 0);

	if (!ctrl->max_namespaces ||
	    ctrl->max_namespaces > le32_to_cpu(id->nn)) {
		dev_err(ctrl->device,
			"Invalid MNAN value %u\n", ctrl->max_namespaces);
		return -EINVAL;
	}

	ctrl->anacap = id->anacap;
	ctrl->anatt = id->anatt;
	ctrl->nanagrpid = le32_to_cpu(id->nanagrpid);
	ctrl->anagrpmax = le32_to_cpu(id->anagrpmax);

	ana_log_size = sizeof(struct nvme_ana_rsp_hdr) +
		ctrl->nanagrpid * sizeof(struct nvme_ana_group_desc) +
		ctrl->max_namespaces * sizeof(__le32);
	if (ana_log_size > max_transfer_size) {
		dev_err(ctrl->device,
			"ANA log page size (%zd) larger than MDTS (%zd).\n",
			ana_log_size, max_transfer_size);
		dev_err(ctrl->device, "disabling ANA support.\n");
		goto out_uninit;
	}
	if (ana_log_size > ctrl->ana_log_size) {
		nvme_mpath_stop(ctrl);
		nvme_mpath_uninit(ctrl);
		ctrl->ana_log_buf = kvmalloc(ana_log_size, GFP_KERNEL);
		if (!ctrl->ana_log_buf)
			return -ENOMEM;
	}
	ctrl->ana_log_size = ana_log_size;
	error = nvme_read_ana_log(ctrl);
	if (error)
		goto out_uninit;
	return 0;

out_uninit:
	nvme_mpath_uninit(ctrl);
	return error;
}

void nvme_mpath_uninit(struct nvme_ctrl *ctrl)
{
	kvfree(ctrl->ana_log_buf);
	ctrl->ana_log_buf = NULL;
	ctrl->ana_log_size = 0;
}

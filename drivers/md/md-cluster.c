/*
 * Copyright (C) 2015, SUSE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 */


#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/dlm.h>
#include <linux/sched.h>
#include <linux/raid/md_p.h>
#include "md.h"
#include "md-bitmap.h"
#include "md-cluster.h"

#define LVB_SIZE	64
#define NEW_DEV_TIMEOUT 5000

struct dlm_lock_resource {
	dlm_lockspace_t *ls;
	struct dlm_lksb lksb;
	char *name; /* lock name. */
	uint32_t flags; /* flags to pass to dlm_lock() */
	wait_queue_head_t sync_locking; /* wait queue for synchronized locking */
	bool sync_locking_done;
	void (*bast)(void *arg, int mode); /* blocking AST function pointer*/
	struct mddev *mddev; /* pointing back to mddev. */
	int mode;
};

struct suspend_info {
	int slot;
	sector_t lo;
	sector_t hi;
	struct list_head list;
};

struct resync_info {
	__le64 lo;
	__le64 hi;
};

/* md_cluster_info flags */
#define		MD_CLUSTER_WAITING_FOR_NEWDISK		1
#define		MD_CLUSTER_SUSPEND_READ_BALANCING	2
#define		MD_CLUSTER_BEGIN_JOIN_CLUSTER		3

/* Lock the send communication. This is done through
 * bit manipulation as opposed to a mutex in order to
 * accomodate lock and hold. See next comment.
 */
#define		MD_CLUSTER_SEND_LOCK			4
/* If cluster operations (such as adding a disk) must lock the
 * communication channel, so as to perform extra operations
 * (update metadata) and no other operation is allowed on the
 * MD. Token needs to be locked and held until the operation
 * completes witha md_update_sb(), which would eventually release
 * the lock.
 */
#define		MD_CLUSTER_SEND_LOCKED_ALREADY		5
/* We should receive message after node joined cluster and
 * set up all the related infos such as bitmap and personality */
#define		MD_CLUSTER_ALREADY_IN_CLUSTER		6
#define		MD_CLUSTER_PENDING_RECV_EVENT		7
#define 	MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD		8

struct md_cluster_info {
	struct mddev *mddev; /* the md device which md_cluster_info belongs to */
	/* dlm lock space and resources for clustered raid. */
	dlm_lockspace_t *lockspace;
	int slot_number;
	struct completion completion;
	struct mutex recv_mutex;
	struct dlm_lock_resource *bitmap_lockres;
	struct dlm_lock_resource **other_bitmap_lockres;
	struct dlm_lock_resource *resync_lockres;
	struct list_head suspend_list;
	spinlock_t suspend_lock;
	struct md_thread *recovery_thread;
	unsigned long recovery_map;
	/* communication loc resources */
	struct dlm_lock_resource *ack_lockres;
	struct dlm_lock_resource *message_lockres;
	struct dlm_lock_resource *token_lockres;
	struct dlm_lock_resource *no_new_dev_lockres;
	struct md_thread *recv_thread;
	struct completion newdisk_completion;
	wait_queue_head_t wait;
	unsigned long state;
	/* record the region in RESYNCING message */
	sector_t sync_low;
	sector_t sync_hi;
};

enum msg_type {
	METADATA_UPDATED = 0,
	RESYNCING,
	NEWDISK,
	REMOVE,
	RE_ADD,
	BITMAP_NEEDS_SYNC,
	CHANGE_CAPACITY,
};

struct cluster_msg {
	__le32 type;
	__le32 slot;
	/* TODO: Unionize this for smaller footprint */
	__le64 low;
	__le64 high;
	char uuid[16];
	__le32 raid_slot;
};

static void sync_ast(void *arg)
{
	struct dlm_lock_resource *res;

	res = arg;
	res->sync_locking_done = true;
	wake_up(&res->sync_locking);
}

static int dlm_lock_sync(struct dlm_lock_resource *res, int mode)
{
	int ret = 0;

	ret = dlm_lock(res->ls, mode, &res->lksb,
			res->flags, res->name, strlen(res->name),
			0, sync_ast, res, res->bast);
	if (ret)
		return ret;
	wait_event(res->sync_locking, res->sync_locking_done);
	res->sync_locking_done = false;
	if (res->lksb.sb_status == 0)
		res->mode = mode;
	return res->lksb.sb_status;
}

static int dlm_unlock_sync(struct dlm_lock_resource *res)
{
	return dlm_lock_sync(res, DLM_LOCK_NL);
}

/*
 * An variation of dlm_lock_sync, which make lock request could
 * be interrupted
 */
static int dlm_lock_sync_interruptible(struct dlm_lock_resource *res, int mode,
				       struct mddev *mddev)
{
	int ret = 0;

	ret = dlm_lock(res->ls, mode, &res->lksb,
			res->flags, res->name, strlen(res->name),
			0, sync_ast, res, res->bast);
	if (ret)
		return ret;

	wait_event(res->sync_locking, res->sync_locking_done
				      || kthread_should_stop()
				      || test_bit(MD_CLOSING, &mddev->flags));
	if (!res->sync_locking_done) {
		/*
		 * the convert queue contains the lock request when request is
		 * interrupted, and sync_ast could still be run, so need to
		 * cancel the request and reset completion
		 */
		ret = dlm_unlock(res->ls, res->lksb.sb_lkid, DLM_LKF_CANCEL,
			&res->lksb, res);
		res->sync_locking_done = false;
		if (unlikely(ret != 0))
			pr_info("failed to cancel previous lock request "
				 "%s return %d\n", res->name, ret);
		return -EPERM;
	} else
		res->sync_locking_done = false;
	if (res->lksb.sb_status == 0)
		res->mode = mode;
	return res->lksb.sb_status;
}

static struct dlm_lock_resource *lockres_init(struct mddev *mddev,
		char *name, void (*bastfn)(void *arg, int mode), int with_lvb)
{
	struct dlm_lock_resource *res = NULL;
	int ret, namelen;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	res = kzalloc(sizeof(struct dlm_lock_resource), GFP_KERNEL);
	if (!res)
		return NULL;
	init_waitqueue_head(&res->sync_locking);
	res->sync_locking_done = false;
	res->ls = cinfo->lockspace;
	res->mddev = mddev;
	res->mode = DLM_LOCK_IV;
	namelen = strlen(name);
	res->name = kzalloc(namelen + 1, GFP_KERNEL);
	if (!res->name) {
		pr_err("md-cluster: Unable to allocate resource name for resource %s\n", name);
		goto out_err;
	}
	strlcpy(res->name, name, namelen + 1);
	if (with_lvb) {
		res->lksb.sb_lvbptr = kzalloc(LVB_SIZE, GFP_KERNEL);
		if (!res->lksb.sb_lvbptr) {
			pr_err("md-cluster: Unable to allocate LVB for resource %s\n", name);
			goto out_err;
		}
		res->flags = DLM_LKF_VALBLK;
	}

	if (bastfn)
		res->bast = bastfn;

	res->flags |= DLM_LKF_EXPEDITE;

	ret = dlm_lock_sync(res, DLM_LOCK_NL);
	if (ret) {
		pr_err("md-cluster: Unable to lock NL on new lock resource %s\n", name);
		goto out_err;
	}
	res->flags &= ~DLM_LKF_EXPEDITE;
	res->flags |= DLM_LKF_CONVERT;

	return res;
out_err:
	kfree(res->lksb.sb_lvbptr);
	kfree(res->name);
	kfree(res);
	return NULL;
}

static void lockres_free(struct dlm_lock_resource *res)
{
	int ret = 0;

	if (!res)
		return;

	/*
	 * use FORCEUNLOCK flag, so we can unlock even the lock is on the
	 * waiting or convert queue
	 */
	ret = dlm_unlock(res->ls, res->lksb.sb_lkid, DLM_LKF_FORCEUNLOCK,
		&res->lksb, res);
	if (unlikely(ret != 0))
		pr_err("failed to unlock %s return %d\n", res->name, ret);
	else
		wait_event(res->sync_locking, res->sync_locking_done);

	kfree(res->name);
	kfree(res->lksb.sb_lvbptr);
	kfree(res);
}

static void add_resync_info(struct dlm_lock_resource *lockres,
			    sector_t lo, sector_t hi)
{
	struct resync_info *ri;

	ri = (struct resync_info *)lockres->lksb.sb_lvbptr;
	ri->lo = cpu_to_le64(lo);
	ri->hi = cpu_to_le64(hi);
}

static struct suspend_info *read_resync_info(struct mddev *mddev, struct dlm_lock_resource *lockres)
{
	struct resync_info ri;
	struct suspend_info *s = NULL;
	sector_t hi = 0;

	dlm_lock_sync(lockres, DLM_LOCK_CR);
	memcpy(&ri, lockres->lksb.sb_lvbptr, sizeof(struct resync_info));
	hi = le64_to_cpu(ri.hi);
	if (hi > 0) {
		s = kzalloc(sizeof(struct suspend_info), GFP_KERNEL);
		if (!s)
			goto out;
		s->hi = hi;
		s->lo = le64_to_cpu(ri.lo);
	}
	dlm_unlock_sync(lockres);
out:
	return s;
}

static void recover_bitmaps(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct dlm_lock_resource *bm_lockres;
	char str[64];
	int slot, ret;
	struct suspend_info *s, *tmp;
	sector_t lo, hi;

	while (cinfo->recovery_map) {
		slot = fls64((u64)cinfo->recovery_map) - 1;

		/* Clear suspend_area associated with the bitmap */
		spin_lock_irq(&cinfo->suspend_lock);
		list_for_each_entry_safe(s, tmp, &cinfo->suspend_list, list)
			if (slot == s->slot) {
				list_del(&s->list);
				kfree(s);
			}
		spin_unlock_irq(&cinfo->suspend_lock);

		snprintf(str, 64, "bitmap%04d", slot);
		bm_lockres = lockres_init(mddev, str, NULL, 1);
		if (!bm_lockres) {
			pr_err("md-cluster: Cannot initialize bitmaps\n");
			goto clear_bit;
		}

		ret = dlm_lock_sync_interruptible(bm_lockres, DLM_LOCK_PW, mddev);
		if (ret) {
			pr_err("md-cluster: Could not DLM lock %s: %d\n",
					str, ret);
			goto clear_bit;
		}
		ret = bitmap_copy_from_slot(mddev, slot, &lo, &hi, true);
		if (ret) {
			pr_err("md-cluster: Could not copy data from bitmap %d\n", slot);
			goto clear_bit;
		}
		if (hi > 0) {
			if (lo < mddev->recovery_cp)
				mddev->recovery_cp = lo;
			/* wake up thread to continue resync in case resync
			 * is not finished */
			if (mddev->recovery_cp != MaxSector) {
			    set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			    md_wakeup_thread(mddev->thread);
			}
		}
clear_bit:
		lockres_free(bm_lockres);
		clear_bit(slot, &cinfo->recovery_map);
	}
}

static void recover_prep(void *arg)
{
	struct mddev *mddev = arg;
	struct md_cluster_info *cinfo = mddev->cluster_info;
	set_bit(MD_CLUSTER_SUSPEND_READ_BALANCING, &cinfo->state);
}

static void __recover_slot(struct mddev *mddev, int slot)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	set_bit(slot, &cinfo->recovery_map);
	if (!cinfo->recovery_thread) {
		cinfo->recovery_thread = md_register_thread(recover_bitmaps,
				mddev, "recover");
		if (!cinfo->recovery_thread) {
			pr_warn("md-cluster: Could not create recovery thread\n");
			return;
		}
	}
	md_wakeup_thread(cinfo->recovery_thread);
}

static void recover_slot(void *arg, struct dlm_slot *slot)
{
	struct mddev *mddev = arg;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	pr_info("md-cluster: %s Node %d/%d down. My slot: %d. Initiating recovery.\n",
			mddev->bitmap_info.cluster_name,
			slot->nodeid, slot->slot,
			cinfo->slot_number);
	/* deduct one since dlm slot starts from one while the num of
	 * cluster-md begins with 0 */
	__recover_slot(mddev, slot->slot - 1);
}

static void recover_done(void *arg, struct dlm_slot *slots,
		int num_slots, int our_slot,
		uint32_t generation)
{
	struct mddev *mddev = arg;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	cinfo->slot_number = our_slot;
	/* completion is only need to be complete when node join cluster,
	 * it doesn't need to run during another node's failure */
	if (test_bit(MD_CLUSTER_BEGIN_JOIN_CLUSTER, &cinfo->state)) {
		complete(&cinfo->completion);
		clear_bit(MD_CLUSTER_BEGIN_JOIN_CLUSTER, &cinfo->state);
	}
	clear_bit(MD_CLUSTER_SUSPEND_READ_BALANCING, &cinfo->state);
}

/* the ops is called when node join the cluster, and do lock recovery
 * if node failure occurs */
static const struct dlm_lockspace_ops md_ls_ops = {
	.recover_prep = recover_prep,
	.recover_slot = recover_slot,
	.recover_done = recover_done,
};

/*
 * The BAST function for the ack lock resource
 * This function wakes up the receive thread in
 * order to receive and process the message.
 */
static void ack_bast(void *arg, int mode)
{
	struct dlm_lock_resource *res = arg;
	struct md_cluster_info *cinfo = res->mddev->cluster_info;

	if (mode == DLM_LOCK_EX) {
		if (test_bit(MD_CLUSTER_ALREADY_IN_CLUSTER, &cinfo->state))
			md_wakeup_thread(cinfo->recv_thread);
		else
			set_bit(MD_CLUSTER_PENDING_RECV_EVENT, &cinfo->state);
	}
}

static void __remove_suspend_info(struct md_cluster_info *cinfo, int slot)
{
	struct suspend_info *s, *tmp;

	list_for_each_entry_safe(s, tmp, &cinfo->suspend_list, list)
		if (slot == s->slot) {
			list_del(&s->list);
			kfree(s);
			break;
		}
}

static void remove_suspend_info(struct mddev *mddev, int slot)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	mddev->pers->quiesce(mddev, 1);
	spin_lock_irq(&cinfo->suspend_lock);
	__remove_suspend_info(cinfo, slot);
	spin_unlock_irq(&cinfo->suspend_lock);
	mddev->pers->quiesce(mddev, 0);
}


static void process_suspend_info(struct mddev *mddev,
		int slot, sector_t lo, sector_t hi)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct suspend_info *s;

	if (!hi) {
		remove_suspend_info(mddev, slot);
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
		return;
	}

	/*
	 * The bitmaps are not same for different nodes
	 * if RESYNCING is happening in one node, then
	 * the node which received the RESYNCING message
	 * probably will perform resync with the region
	 * [lo, hi] again, so we could reduce resync time
	 * a lot if we can ensure that the bitmaps among
	 * different nodes are match up well.
	 *
	 * sync_low/hi is used to record the region which
	 * arrived in the previous RESYNCING message,
	 *
	 * Call bitmap_sync_with_cluster to clear
	 * NEEDED_MASK and set RESYNC_MASK since
	 * resync thread is running in another node,
	 * so we don't need to do the resync again
	 * with the same section */
	bitmap_sync_with_cluster(mddev, cinfo->sync_low,
					cinfo->sync_hi,
					lo, hi);
	cinfo->sync_low = lo;
	cinfo->sync_hi = hi;

	s = kzalloc(sizeof(struct suspend_info), GFP_KERNEL);
	if (!s)
		return;
	s->slot = slot;
	s->lo = lo;
	s->hi = hi;
	mddev->pers->quiesce(mddev, 1);
	spin_lock_irq(&cinfo->suspend_lock);
	/* Remove existing entry (if exists) before adding */
	__remove_suspend_info(cinfo, slot);
	list_add(&s->list, &cinfo->suspend_list);
	spin_unlock_irq(&cinfo->suspend_lock);
	mddev->pers->quiesce(mddev, 0);
}

static void process_add_new_disk(struct mddev *mddev, struct cluster_msg *cmsg)
{
	char disk_uuid[64];
	struct md_cluster_info *cinfo = mddev->cluster_info;
	char event_name[] = "EVENT=ADD_DEVICE";
	char raid_slot[16];
	char *envp[] = {event_name, disk_uuid, raid_slot, NULL};
	int len;

	len = snprintf(disk_uuid, 64, "DEVICE_UUID=");
	sprintf(disk_uuid + len, "%pU", cmsg->uuid);
	snprintf(raid_slot, 16, "RAID_DISK=%d", le32_to_cpu(cmsg->raid_slot));
	pr_info("%s:%d Sending kobject change with %s and %s\n", __func__, __LINE__, disk_uuid, raid_slot);
	init_completion(&cinfo->newdisk_completion);
	set_bit(MD_CLUSTER_WAITING_FOR_NEWDISK, &cinfo->state);
	kobject_uevent_env(&disk_to_dev(mddev->gendisk)->kobj, KOBJ_CHANGE, envp);
	wait_for_completion_timeout(&cinfo->newdisk_completion,
			NEW_DEV_TIMEOUT);
	clear_bit(MD_CLUSTER_WAITING_FOR_NEWDISK, &cinfo->state);
}


static void process_metadata_update(struct mddev *mddev, struct cluster_msg *msg)
{
	int got_lock = 0;
	struct md_cluster_info *cinfo = mddev->cluster_info;
	mddev->good_device_nr = le32_to_cpu(msg->raid_slot);

	dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_CR);
	wait_event(mddev->thread->wqueue,
		   (got_lock = mddev_trylock(mddev)) ||
		    test_bit(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD, &cinfo->state));
	md_reload_sb(mddev, mddev->good_device_nr);
	if (got_lock)
		mddev_unlock(mddev);
}

static void process_remove_disk(struct mddev *mddev, struct cluster_msg *msg)
{
	struct md_rdev *rdev;

	rcu_read_lock();
	rdev = md_find_rdev_nr_rcu(mddev, le32_to_cpu(msg->raid_slot));
	if (rdev) {
		set_bit(ClusterRemove, &rdev->flags);
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
	}
	else
		pr_warn("%s: %d Could not find disk(%d) to REMOVE\n",
			__func__, __LINE__, le32_to_cpu(msg->raid_slot));
	rcu_read_unlock();
}

static void process_readd_disk(struct mddev *mddev, struct cluster_msg *msg)
{
	struct md_rdev *rdev;

	rcu_read_lock();
	rdev = md_find_rdev_nr_rcu(mddev, le32_to_cpu(msg->raid_slot));
	if (rdev && test_bit(Faulty, &rdev->flags))
		clear_bit(Faulty, &rdev->flags);
	else
		pr_warn("%s: %d Could not find disk(%d) which is faulty",
			__func__, __LINE__, le32_to_cpu(msg->raid_slot));
	rcu_read_unlock();
}

static int process_recvd_msg(struct mddev *mddev, struct cluster_msg *msg)
{
	int ret = 0;

	if (WARN(mddev->cluster_info->slot_number - 1 == le32_to_cpu(msg->slot),
		"node %d received it's own msg\n", le32_to_cpu(msg->slot)))
		return -1;
	switch (le32_to_cpu(msg->type)) {
	case METADATA_UPDATED:
		process_metadata_update(mddev, msg);
		break;
	case CHANGE_CAPACITY:
		set_capacity(mddev->gendisk, mddev->array_sectors);
		revalidate_disk(mddev->gendisk);
		break;
	case RESYNCING:
		process_suspend_info(mddev, le32_to_cpu(msg->slot),
				     le64_to_cpu(msg->low),
				     le64_to_cpu(msg->high));
		break;
	case NEWDISK:
		process_add_new_disk(mddev, msg);
		break;
	case REMOVE:
		process_remove_disk(mddev, msg);
		break;
	case RE_ADD:
		process_readd_disk(mddev, msg);
		break;
	case BITMAP_NEEDS_SYNC:
		__recover_slot(mddev, le32_to_cpu(msg->slot));
		break;
	default:
		ret = -1;
		pr_warn("%s:%d Received unknown message from %d\n",
			__func__, __LINE__, msg->slot);
	}
	return ret;
}

/*
 * thread for receiving message
 */
static void recv_daemon(struct md_thread *thread)
{
	struct md_cluster_info *cinfo = thread->mddev->cluster_info;
	struct dlm_lock_resource *ack_lockres = cinfo->ack_lockres;
	struct dlm_lock_resource *message_lockres = cinfo->message_lockres;
	struct cluster_msg msg;
	int ret;

	mutex_lock(&cinfo->recv_mutex);
	/*get CR on Message*/
	if (dlm_lock_sync(message_lockres, DLM_LOCK_CR)) {
		pr_err("md/raid1:failed to get CR on MESSAGE\n");
		mutex_unlock(&cinfo->recv_mutex);
		return;
	}

	/* read lvb and wake up thread to process this message_lockres */
	memcpy(&msg, message_lockres->lksb.sb_lvbptr, sizeof(struct cluster_msg));
	ret = process_recvd_msg(thread->mddev, &msg);
	if (ret)
		goto out;

	/*release CR on ack_lockres*/
	ret = dlm_unlock_sync(ack_lockres);
	if (unlikely(ret != 0))
		pr_info("unlock ack failed return %d\n", ret);
	/*up-convert to PR on message_lockres*/
	ret = dlm_lock_sync(message_lockres, DLM_LOCK_PR);
	if (unlikely(ret != 0))
		pr_info("lock PR on msg failed return %d\n", ret);
	/*get CR on ack_lockres again*/
	ret = dlm_lock_sync(ack_lockres, DLM_LOCK_CR);
	if (unlikely(ret != 0))
		pr_info("lock CR on ack failed return %d\n", ret);
out:
	/*release CR on message_lockres*/
	ret = dlm_unlock_sync(message_lockres);
	if (unlikely(ret != 0))
		pr_info("unlock msg failed return %d\n", ret);
	mutex_unlock(&cinfo->recv_mutex);
}

/* lock_token()
 * Takes the lock on the TOKEN lock resource so no other
 * node can communicate while the operation is underway.
 */
static int lock_token(struct md_cluster_info *cinfo, bool mddev_locked)
{
	int error, set_bit = 0;
	struct mddev *mddev = cinfo->mddev;

	/*
	 * If resync thread run after raid1d thread, then process_metadata_update
	 * could not continue if raid1d held reconfig_mutex (and raid1d is blocked
	 * since another node already got EX on Token and waitting the EX of Ack),
	 * so let resync wake up thread in case flag is set.
	 */
	if (mddev_locked && !test_bit(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD,
				      &cinfo->state)) {
		error = test_and_set_bit_lock(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD,
					      &cinfo->state);
		WARN_ON_ONCE(error);
		md_wakeup_thread(mddev->thread);
		set_bit = 1;
	}
	error = dlm_lock_sync(cinfo->token_lockres, DLM_LOCK_EX);
	if (set_bit)
		clear_bit_unlock(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD, &cinfo->state);

	if (error)
		pr_err("md-cluster(%s:%d): failed to get EX on TOKEN (%d)\n",
				__func__, __LINE__, error);

	/* Lock the receive sequence */
	mutex_lock(&cinfo->recv_mutex);
	return error;
}

/* lock_comm()
 * Sets the MD_CLUSTER_SEND_LOCK bit to lock the send channel.
 */
static int lock_comm(struct md_cluster_info *cinfo, bool mddev_locked)
{
	wait_event(cinfo->wait,
		   !test_and_set_bit(MD_CLUSTER_SEND_LOCK, &cinfo->state));

	return lock_token(cinfo, mddev_locked);
}

static void unlock_comm(struct md_cluster_info *cinfo)
{
	WARN_ON(cinfo->token_lockres->mode != DLM_LOCK_EX);
	mutex_unlock(&cinfo->recv_mutex);
	dlm_unlock_sync(cinfo->token_lockres);
	clear_bit(MD_CLUSTER_SEND_LOCK, &cinfo->state);
	wake_up(&cinfo->wait);
}

/* __sendmsg()
 * This function performs the actual sending of the message. This function is
 * usually called after performing the encompassing operation
 * The function:
 * 1. Grabs the message lockresource in EX mode
 * 2. Copies the message to the message LVB
 * 3. Downconverts message lockresource to CW
 * 4. Upconverts ack lock resource from CR to EX. This forces the BAST on other nodes
 *    and the other nodes read the message. The thread will wait here until all other
 *    nodes have released ack lock resource.
 * 5. Downconvert ack lockresource to CR
 */
static int __sendmsg(struct md_cluster_info *cinfo, struct cluster_msg *cmsg)
{
	int error;
	int slot = cinfo->slot_number - 1;

	cmsg->slot = cpu_to_le32(slot);
	/*get EX on Message*/
	error = dlm_lock_sync(cinfo->message_lockres, DLM_LOCK_EX);
	if (error) {
		pr_err("md-cluster: failed to get EX on MESSAGE (%d)\n", error);
		goto failed_message;
	}

	memcpy(cinfo->message_lockres->lksb.sb_lvbptr, (void *)cmsg,
			sizeof(struct cluster_msg));
	/*down-convert EX to CW on Message*/
	error = dlm_lock_sync(cinfo->message_lockres, DLM_LOCK_CW);
	if (error) {
		pr_err("md-cluster: failed to convert EX to CW on MESSAGE(%d)\n",
				error);
		goto failed_ack;
	}

	/*up-convert CR to EX on Ack*/
	error = dlm_lock_sync(cinfo->ack_lockres, DLM_LOCK_EX);
	if (error) {
		pr_err("md-cluster: failed to convert CR to EX on ACK(%d)\n",
				error);
		goto failed_ack;
	}

	/*down-convert EX to CR on Ack*/
	error = dlm_lock_sync(cinfo->ack_lockres, DLM_LOCK_CR);
	if (error) {
		pr_err("md-cluster: failed to convert EX to CR on ACK(%d)\n",
				error);
		goto failed_ack;
	}

failed_ack:
	error = dlm_unlock_sync(cinfo->message_lockres);
	if (unlikely(error != 0)) {
		pr_err("md-cluster: failed convert to NL on MESSAGE(%d)\n",
			error);
		/* in case the message can't be released due to some reason */
		goto failed_ack;
	}
failed_message:
	return error;
}

static int sendmsg(struct md_cluster_info *cinfo, struct cluster_msg *cmsg,
		   bool mddev_locked)
{
	int ret;

	lock_comm(cinfo, mddev_locked);
	ret = __sendmsg(cinfo, cmsg);
	unlock_comm(cinfo);
	return ret;
}

static int gather_all_resync_info(struct mddev *mddev, int total_slots)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	int i, ret = 0;
	struct dlm_lock_resource *bm_lockres;
	struct suspend_info *s;
	char str[64];
	sector_t lo, hi;


	for (i = 0; i < total_slots; i++) {
		memset(str, '\0', 64);
		snprintf(str, 64, "bitmap%04d", i);
		bm_lockres = lockres_init(mddev, str, NULL, 1);
		if (!bm_lockres)
			return -ENOMEM;
		if (i == (cinfo->slot_number - 1)) {
			lockres_free(bm_lockres);
			continue;
		}

		bm_lockres->flags |= DLM_LKF_NOQUEUE;
		ret = dlm_lock_sync(bm_lockres, DLM_LOCK_PW);
		if (ret == -EAGAIN) {
			s = read_resync_info(mddev, bm_lockres);
			if (s) {
				pr_info("%s:%d Resync[%llu..%llu] in progress on %d\n",
						__func__, __LINE__,
						(unsigned long long) s->lo,
						(unsigned long long) s->hi, i);
				spin_lock_irq(&cinfo->suspend_lock);
				s->slot = i;
				list_add(&s->list, &cinfo->suspend_list);
				spin_unlock_irq(&cinfo->suspend_lock);
			}
			ret = 0;
			lockres_free(bm_lockres);
			continue;
		}
		if (ret) {
			lockres_free(bm_lockres);
			goto out;
		}

		/* Read the disk bitmap sb and check if it needs recovery */
		ret = bitmap_copy_from_slot(mddev, i, &lo, &hi, false);
		if (ret) {
			pr_warn("md-cluster: Could not gather bitmaps from slot %d", i);
			lockres_free(bm_lockres);
			continue;
		}
		if ((hi > 0) && (lo < mddev->recovery_cp)) {
			set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			mddev->recovery_cp = lo;
			md_check_recovery(mddev);
		}

		lockres_free(bm_lockres);
	}
out:
	return ret;
}

static int join(struct mddev *mddev, int nodes)
{
	struct md_cluster_info *cinfo;
	int ret, ops_rv;
	char str[64];

	cinfo = kzalloc(sizeof(struct md_cluster_info), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	INIT_LIST_HEAD(&cinfo->suspend_list);
	spin_lock_init(&cinfo->suspend_lock);
	init_completion(&cinfo->completion);
	set_bit(MD_CLUSTER_BEGIN_JOIN_CLUSTER, &cinfo->state);
	init_waitqueue_head(&cinfo->wait);
	mutex_init(&cinfo->recv_mutex);

	mddev->cluster_info = cinfo;
	cinfo->mddev = mddev;

	memset(str, 0, 64);
	sprintf(str, "%pU", mddev->uuid);
	ret = dlm_new_lockspace(str, mddev->bitmap_info.cluster_name,
				DLM_LSFL_FS, LVB_SIZE,
				&md_ls_ops, mddev, &ops_rv, &cinfo->lockspace);
	if (ret)
		goto err;
	wait_for_completion(&cinfo->completion);
	if (nodes < cinfo->slot_number) {
		pr_err("md-cluster: Slot allotted(%d) is greater than available slots(%d).",
			cinfo->slot_number, nodes);
		ret = -ERANGE;
		goto err;
	}
	/* Initiate the communication resources */
	ret = -ENOMEM;
	cinfo->recv_thread = md_register_thread(recv_daemon, mddev, "cluster_recv");
	if (!cinfo->recv_thread) {
		pr_err("md-cluster: cannot allocate memory for recv_thread!\n");
		goto err;
	}
	cinfo->message_lockres = lockres_init(mddev, "message", NULL, 1);
	if (!cinfo->message_lockres)
		goto err;
	cinfo->token_lockres = lockres_init(mddev, "token", NULL, 0);
	if (!cinfo->token_lockres)
		goto err;
	cinfo->no_new_dev_lockres = lockres_init(mddev, "no-new-dev", NULL, 0);
	if (!cinfo->no_new_dev_lockres)
		goto err;

	ret = dlm_lock_sync(cinfo->token_lockres, DLM_LOCK_EX);
	if (ret) {
		ret = -EAGAIN;
		pr_err("md-cluster: can't join cluster to avoid lock issue\n");
		goto err;
	}
	cinfo->ack_lockres = lockres_init(mddev, "ack", ack_bast, 0);
	if (!cinfo->ack_lockres) {
		ret = -ENOMEM;
		goto err;
	}
	/* get sync CR lock on ACK. */
	if (dlm_lock_sync(cinfo->ack_lockres, DLM_LOCK_CR))
		pr_err("md-cluster: failed to get a sync CR lock on ACK!(%d)\n",
				ret);
	dlm_unlock_sync(cinfo->token_lockres);
	/* get sync CR lock on no-new-dev. */
	if (dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_CR))
		pr_err("md-cluster: failed to get a sync CR lock on no-new-dev!(%d)\n", ret);


	pr_info("md-cluster: Joined cluster %s slot %d\n", str, cinfo->slot_number);
	snprintf(str, 64, "bitmap%04d", cinfo->slot_number - 1);
	cinfo->bitmap_lockres = lockres_init(mddev, str, NULL, 1);
	if (!cinfo->bitmap_lockres) {
		ret = -ENOMEM;
		goto err;
	}
	if (dlm_lock_sync(cinfo->bitmap_lockres, DLM_LOCK_PW)) {
		pr_err("Failed to get bitmap lock\n");
		ret = -EINVAL;
		goto err;
	}

	cinfo->resync_lockres = lockres_init(mddev, "resync", NULL, 0);
	if (!cinfo->resync_lockres) {
		ret = -ENOMEM;
		goto err;
	}

	return 0;
err:
	set_bit(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD, &cinfo->state);
	md_unregister_thread(&cinfo->recovery_thread);
	md_unregister_thread(&cinfo->recv_thread);
	lockres_free(cinfo->message_lockres);
	lockres_free(cinfo->token_lockres);
	lockres_free(cinfo->ack_lockres);
	lockres_free(cinfo->no_new_dev_lockres);
	lockres_free(cinfo->resync_lockres);
	lockres_free(cinfo->bitmap_lockres);
	if (cinfo->lockspace)
		dlm_release_lockspace(cinfo->lockspace, 2);
	mddev->cluster_info = NULL;
	kfree(cinfo);
	return ret;
}

static void load_bitmaps(struct mddev *mddev, int total_slots)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	/* load all the node's bitmap info for resync */
	if (gather_all_resync_info(mddev, total_slots))
		pr_err("md-cluster: failed to gather all resyn infos\n");
	set_bit(MD_CLUSTER_ALREADY_IN_CLUSTER, &cinfo->state);
	/* wake up recv thread in case something need to be handled */
	if (test_and_clear_bit(MD_CLUSTER_PENDING_RECV_EVENT, &cinfo->state))
		md_wakeup_thread(cinfo->recv_thread);
}

static void resync_bitmap(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct cluster_msg cmsg = {0};
	int err;

	cmsg.type = cpu_to_le32(BITMAP_NEEDS_SYNC);
	err = sendmsg(cinfo, &cmsg, 1);
	if (err)
		pr_err("%s:%d: failed to send BITMAP_NEEDS_SYNC message (%d)\n",
			__func__, __LINE__, err);
}

static void unlock_all_bitmaps(struct mddev *mddev);
static int leave(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	if (!cinfo)
		return 0;

	/* BITMAP_NEEDS_SYNC message should be sent when node
	 * is leaving the cluster with dirty bitmap, also we
	 * can only deliver it when dlm connection is available */
	if (cinfo->slot_number > 0 && mddev->recovery_cp != MaxSector)
		resync_bitmap(mddev);

	set_bit(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD, &cinfo->state);
	md_unregister_thread(&cinfo->recovery_thread);
	md_unregister_thread(&cinfo->recv_thread);
	lockres_free(cinfo->message_lockres);
	lockres_free(cinfo->token_lockres);
	lockres_free(cinfo->ack_lockres);
	lockres_free(cinfo->no_new_dev_lockres);
	lockres_free(cinfo->resync_lockres);
	lockres_free(cinfo->bitmap_lockres);
	unlock_all_bitmaps(mddev);
	dlm_release_lockspace(cinfo->lockspace, 2);
	kfree(cinfo);
	return 0;
}

/* slot_number(): Returns the MD slot number to use
 * DLM starts the slot numbers from 1, wheras cluster-md
 * wants the number to be from zero, so we deduct one
 */
static int slot_number(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	return cinfo->slot_number - 1;
}

/*
 * Check if the communication is already locked, else lock the communication
 * channel.
 * If it is already locked, token is in EX mode, and hence lock_token()
 * should not be called.
 */
static int metadata_update_start(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	int ret;

	/*
	 * metadata_update_start is always called with the protection of
	 * reconfig_mutex, so set WAITING_FOR_TOKEN here.
	 */
	ret = test_and_set_bit_lock(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD,
				    &cinfo->state);
	WARN_ON_ONCE(ret);
	md_wakeup_thread(mddev->thread);

	wait_event(cinfo->wait,
		   !test_and_set_bit(MD_CLUSTER_SEND_LOCK, &cinfo->state) ||
		   test_and_clear_bit(MD_CLUSTER_SEND_LOCKED_ALREADY, &cinfo->state));

	/* If token is already locked, return 0 */
	if (cinfo->token_lockres->mode == DLM_LOCK_EX) {
		clear_bit_unlock(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD, &cinfo->state);
		return 0;
	}

	ret = lock_token(cinfo, 1);
	clear_bit_unlock(MD_CLUSTER_HOLDING_MUTEX_FOR_RECVD, &cinfo->state);
	return ret;
}

static int metadata_update_finish(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct cluster_msg cmsg;
	struct md_rdev *rdev;
	int ret = 0;
	int raid_slot = -1;

	memset(&cmsg, 0, sizeof(cmsg));
	cmsg.type = cpu_to_le32(METADATA_UPDATED);
	/* Pick up a good active device number to send.
	 */
	rdev_for_each(rdev, mddev)
		if (rdev->raid_disk > -1 && !test_bit(Faulty, &rdev->flags)) {
			raid_slot = rdev->desc_nr;
			break;
		}
	if (raid_slot >= 0) {
		cmsg.raid_slot = cpu_to_le32(raid_slot);
		ret = __sendmsg(cinfo, &cmsg);
	} else
		pr_warn("md-cluster: No good device id found to send\n");
	clear_bit(MD_CLUSTER_SEND_LOCKED_ALREADY, &cinfo->state);
	unlock_comm(cinfo);
	return ret;
}

static void metadata_update_cancel(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	clear_bit(MD_CLUSTER_SEND_LOCKED_ALREADY, &cinfo->state);
	unlock_comm(cinfo);
}

/*
 * return 0 if all the bitmaps have the same sync_size
 */
static int cluster_check_sync_size(struct mddev *mddev)
{
	int i, rv;
	bitmap_super_t *sb;
	unsigned long my_sync_size, sync_size = 0;
	int node_num = mddev->bitmap_info.nodes;
	int current_slot = md_cluster_ops->slot_number(mddev);
	struct bitmap *bitmap = mddev->bitmap;
	char str[64];
	struct dlm_lock_resource *bm_lockres;

	sb = kmap_atomic(bitmap->storage.sb_page);
	my_sync_size = sb->sync_size;
	kunmap_atomic(sb);

	for (i = 0; i < node_num; i++) {
		if (i == current_slot)
			continue;

		bitmap = get_bitmap_from_slot(mddev, i);
		if (IS_ERR(bitmap)) {
			pr_err("can't get bitmap from slot %d\n", i);
			return -1;
		}

		/*
		 * If we can hold the bitmap lock of one node then
		 * the slot is not occupied, update the sb.
		 */
		snprintf(str, 64, "bitmap%04d", i);
		bm_lockres = lockres_init(mddev, str, NULL, 1);
		if (!bm_lockres) {
			pr_err("md-cluster: Cannot initialize %s\n", str);
			bitmap_free(bitmap);
			return -1;
		}
		bm_lockres->flags |= DLM_LKF_NOQUEUE;
		rv = dlm_lock_sync(bm_lockres, DLM_LOCK_PW);
		if (!rv)
			bitmap_update_sb(bitmap);
		lockres_free(bm_lockres);

		sb = kmap_atomic(bitmap->storage.sb_page);
		if (sync_size == 0)
			sync_size = sb->sync_size;
		else if (sync_size != sb->sync_size) {
			kunmap_atomic(sb);
			bitmap_free(bitmap);
			return -1;
		}
		kunmap_atomic(sb);
		bitmap_free(bitmap);
	}

	return (my_sync_size == sync_size) ? 0 : -1;
}

/*
 * Update the size for cluster raid is a little more complex, we perform it
 * by the steps:
 * 1. hold token lock and update superblock in initiator node.
 * 2. send METADATA_UPDATED msg to other nodes.
 * 3. The initiator node continues to check each bitmap's sync_size, if all
 *    bitmaps have the same value of sync_size, then we can set capacity and
 *    let other nodes to perform it. If one node can't update sync_size
 *    accordingly, we need to revert to previous value.
 */
static void update_size(struct mddev *mddev, sector_t old_dev_sectors)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct cluster_msg cmsg;
	struct md_rdev *rdev;
	int ret = 0;
	int raid_slot = -1;

	md_update_sb(mddev, 1);
	lock_comm(cinfo, 1);

	memset(&cmsg, 0, sizeof(cmsg));
	cmsg.type = cpu_to_le32(METADATA_UPDATED);
	rdev_for_each(rdev, mddev)
		if (rdev->raid_disk >= 0 && !test_bit(Faulty, &rdev->flags)) {
			raid_slot = rdev->desc_nr;
			break;
		}
	if (raid_slot >= 0) {
		cmsg.raid_slot = cpu_to_le32(raid_slot);
		/*
		 * We can only change capiticy after all the nodes can do it,
		 * so need to wait after other nodes already received the msg
		 * and handled the change
		 */
		ret = __sendmsg(cinfo, &cmsg);
		if (ret) {
			pr_err("%s:%d: failed to send METADATA_UPDATED msg\n",
			       __func__, __LINE__);
			unlock_comm(cinfo);
			return;
		}
	} else {
		pr_err("md-cluster: No good device id found to send\n");
		unlock_comm(cinfo);
		return;
	}

	/*
	 * check the sync_size from other node's bitmap, if sync_size
	 * have already updated in other nodes as expected, send an
	 * empty metadata msg to permit the change of capacity
	 */
	if (cluster_check_sync_size(mddev) == 0) {
		memset(&cmsg, 0, sizeof(cmsg));
		cmsg.type = cpu_to_le32(CHANGE_CAPACITY);
		ret = __sendmsg(cinfo, &cmsg);
		if (ret)
			pr_err("%s:%d: failed to send CHANGE_CAPACITY msg\n",
			       __func__, __LINE__);
		set_capacity(mddev->gendisk, mddev->array_sectors);
		revalidate_disk(mddev->gendisk);
	} else {
		/* revert to previous sectors */
		ret = mddev->pers->resize(mddev, old_dev_sectors);
		if (!ret)
			revalidate_disk(mddev->gendisk);
		ret = __sendmsg(cinfo, &cmsg);
		if (ret)
			pr_err("%s:%d: failed to send METADATA_UPDATED msg\n",
			       __func__, __LINE__);
	}
	unlock_comm(cinfo);
}

static int resync_start(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	return dlm_lock_sync_interruptible(cinfo->resync_lockres, DLM_LOCK_EX, mddev);
}

static int resync_info_update(struct mddev *mddev, sector_t lo, sector_t hi)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct resync_info ri;
	struct cluster_msg cmsg = {0};

	/* do not send zero again, if we have sent before */
	if (hi == 0) {
		memcpy(&ri, cinfo->bitmap_lockres->lksb.sb_lvbptr, sizeof(struct resync_info));
		if (le64_to_cpu(ri.hi) == 0)
			return 0;
	}

	add_resync_info(cinfo->bitmap_lockres, lo, hi);
	/* Re-acquire the lock to refresh LVB */
	dlm_lock_sync(cinfo->bitmap_lockres, DLM_LOCK_PW);
	cmsg.type = cpu_to_le32(RESYNCING);
	cmsg.low = cpu_to_le64(lo);
	cmsg.high = cpu_to_le64(hi);

	/*
	 * mddev_lock is held if resync_info_update is called from
	 * resync_finish (md_reap_sync_thread -> resync_finish)
	 */
	if (lo == 0 && hi == 0)
		return sendmsg(cinfo, &cmsg, 1);
	else
		return sendmsg(cinfo, &cmsg, 0);
}

static int resync_finish(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	dlm_unlock_sync(cinfo->resync_lockres);
	return resync_info_update(mddev, 0, 0);
}

static int area_resyncing(struct mddev *mddev, int direction,
		sector_t lo, sector_t hi)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	int ret = 0;
	struct suspend_info *s;

	if ((direction == READ) &&
		test_bit(MD_CLUSTER_SUSPEND_READ_BALANCING, &cinfo->state))
		return 1;

	spin_lock_irq(&cinfo->suspend_lock);
	if (list_empty(&cinfo->suspend_list))
		goto out;
	list_for_each_entry(s, &cinfo->suspend_list, list)
		if (hi > s->lo && lo < s->hi) {
			ret = 1;
			break;
		}
out:
	spin_unlock_irq(&cinfo->suspend_lock);
	return ret;
}

/* add_new_disk() - initiates a disk add
 * However, if this fails before writing md_update_sb(),
 * add_new_disk_cancel() must be called to release token lock
 */
static int add_new_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct cluster_msg cmsg;
	int ret = 0;
	struct mdp_superblock_1 *sb = page_address(rdev->sb_page);
	char *uuid = sb->device_uuid;

	memset(&cmsg, 0, sizeof(cmsg));
	cmsg.type = cpu_to_le32(NEWDISK);
	memcpy(cmsg.uuid, uuid, 16);
	cmsg.raid_slot = cpu_to_le32(rdev->desc_nr);
	lock_comm(cinfo, 1);
	ret = __sendmsg(cinfo, &cmsg);
	if (ret) {
		unlock_comm(cinfo);
		return ret;
	}
	cinfo->no_new_dev_lockres->flags |= DLM_LKF_NOQUEUE;
	ret = dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_EX);
	cinfo->no_new_dev_lockres->flags &= ~DLM_LKF_NOQUEUE;
	/* Some node does not "see" the device */
	if (ret == -EAGAIN)
		ret = -ENOENT;
	if (ret)
		unlock_comm(cinfo);
	else {
		dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_CR);
		/* Since MD_CHANGE_DEVS will be set in add_bound_rdev which
		 * will run soon after add_new_disk, the below path will be
		 * invoked:
		 *   md_wakeup_thread(mddev->thread)
		 *	-> conf->thread (raid1d)
		 *	-> md_check_recovery -> md_update_sb
		 *	-> metadata_update_start/finish
		 * MD_CLUSTER_SEND_LOCKED_ALREADY will be cleared eventually.
		 *
		 * For other failure cases, metadata_update_cancel and
		 * add_new_disk_cancel also clear below bit as well.
		 * */
		set_bit(MD_CLUSTER_SEND_LOCKED_ALREADY, &cinfo->state);
		wake_up(&cinfo->wait);
	}
	return ret;
}

static void add_new_disk_cancel(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	clear_bit(MD_CLUSTER_SEND_LOCKED_ALREADY, &cinfo->state);
	unlock_comm(cinfo);
}

static int new_disk_ack(struct mddev *mddev, bool ack)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;

	if (!test_bit(MD_CLUSTER_WAITING_FOR_NEWDISK, &cinfo->state)) {
		pr_warn("md-cluster(%s): Spurious cluster confirmation\n", mdname(mddev));
		return -EINVAL;
	}

	if (ack)
		dlm_unlock_sync(cinfo->no_new_dev_lockres);
	complete(&cinfo->newdisk_completion);
	return 0;
}

static int remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct cluster_msg cmsg = {0};
	struct md_cluster_info *cinfo = mddev->cluster_info;
	cmsg.type = cpu_to_le32(REMOVE);
	cmsg.raid_slot = cpu_to_le32(rdev->desc_nr);
	return sendmsg(cinfo, &cmsg, 1);
}

static int lock_all_bitmaps(struct mddev *mddev)
{
	int slot, my_slot, ret, held = 1, i = 0;
	char str[64];
	struct md_cluster_info *cinfo = mddev->cluster_info;

	cinfo->other_bitmap_lockres = kzalloc((mddev->bitmap_info.nodes - 1) *
					     sizeof(struct dlm_lock_resource *),
					     GFP_KERNEL);
	if (!cinfo->other_bitmap_lockres) {
		pr_err("md: can't alloc mem for other bitmap locks\n");
		return 0;
	}

	my_slot = slot_number(mddev);
	for (slot = 0; slot < mddev->bitmap_info.nodes; slot++) {
		if (slot == my_slot)
			continue;

		memset(str, '\0', 64);
		snprintf(str, 64, "bitmap%04d", slot);
		cinfo->other_bitmap_lockres[i] = lockres_init(mddev, str, NULL, 1);
		if (!cinfo->other_bitmap_lockres[i])
			return -ENOMEM;

		cinfo->other_bitmap_lockres[i]->flags |= DLM_LKF_NOQUEUE;
		ret = dlm_lock_sync(cinfo->other_bitmap_lockres[i], DLM_LOCK_PW);
		if (ret)
			held = -1;
		i++;
	}

	return held;
}

static void unlock_all_bitmaps(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	int i;

	/* release other node's bitmap lock if they are existed */
	if (cinfo->other_bitmap_lockres) {
		for (i = 0; i < mddev->bitmap_info.nodes - 1; i++) {
			if (cinfo->other_bitmap_lockres[i]) {
				lockres_free(cinfo->other_bitmap_lockres[i]);
			}
		}
		kfree(cinfo->other_bitmap_lockres);
	}
}

static int gather_bitmaps(struct md_rdev *rdev)
{
	int sn, err;
	sector_t lo, hi;
	struct cluster_msg cmsg = {0};
	struct mddev *mddev = rdev->mddev;
	struct md_cluster_info *cinfo = mddev->cluster_info;

	cmsg.type = cpu_to_le32(RE_ADD);
	cmsg.raid_slot = cpu_to_le32(rdev->desc_nr);
	err = sendmsg(cinfo, &cmsg, 1);
	if (err)
		goto out;

	for (sn = 0; sn < mddev->bitmap_info.nodes; sn++) {
		if (sn == (cinfo->slot_number - 1))
			continue;
		err = bitmap_copy_from_slot(mddev, sn, &lo, &hi, false);
		if (err) {
			pr_warn("md-cluster: Could not gather bitmaps from slot %d", sn);
			goto out;
		}
		if ((hi > 0) && (lo < mddev->recovery_cp))
			mddev->recovery_cp = lo;
	}
out:
	return err;
}

static struct md_cluster_operations cluster_ops = {
	.join   = join,
	.leave  = leave,
	.slot_number = slot_number,
	.resync_start = resync_start,
	.resync_finish = resync_finish,
	.resync_info_update = resync_info_update,
	.metadata_update_start = metadata_update_start,
	.metadata_update_finish = metadata_update_finish,
	.metadata_update_cancel = metadata_update_cancel,
	.area_resyncing = area_resyncing,
	.add_new_disk = add_new_disk,
	.add_new_disk_cancel = add_new_disk_cancel,
	.new_disk_ack = new_disk_ack,
	.remove_disk = remove_disk,
	.load_bitmaps = load_bitmaps,
	.gather_bitmaps = gather_bitmaps,
	.lock_all_bitmaps = lock_all_bitmaps,
	.unlock_all_bitmaps = unlock_all_bitmaps,
	.update_size = update_size,
};

static int __init cluster_init(void)
{
	pr_warn("md-cluster: support raid1 and raid10 (limited support)\n");
	pr_info("Registering Cluster MD functions\n");
	register_md_cluster_operations(&cluster_ops, THIS_MODULE);
	return 0;
}

static void cluster_exit(void)
{
	unregister_md_cluster_operations();
}

module_init(cluster_init);
module_exit(cluster_exit);
MODULE_AUTHOR("SUSE");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clustering support for MD");

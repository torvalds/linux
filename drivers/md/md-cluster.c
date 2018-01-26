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
#include <linux/dlm.h>
#include <linux/sched.h>
#include <linux/raid/md_p.h>
#include "md.h"
#include "bitmap.h"
#include "md-cluster.h"

#define LVB_SIZE	64
#define NEW_DEV_TIMEOUT 5000

struct dlm_lock_resource {
	dlm_lockspace_t *ls;
	struct dlm_lksb lksb;
	char *name; /* lock name. */
	uint32_t flags; /* flags to pass to dlm_lock() */
	struct completion completion; /* completion for synchronized locking */
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


struct md_cluster_info {
	/* dlm lock space and resources for clustered raid. */
	dlm_lockspace_t *lockspace;
	int slot_number;
	struct completion completion;
	struct dlm_lock_resource *bitmap_lockres;
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
	unsigned long state;
};

enum msg_type {
	METADATA_UPDATED = 0,
	RESYNCING,
	NEWDISK,
	REMOVE,
	RE_ADD,
	BITMAP_NEEDS_SYNC,
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
	complete(&res->completion);
}

static int dlm_lock_sync(struct dlm_lock_resource *res, int mode)
{
	int ret = 0;

	ret = dlm_lock(res->ls, mode, &res->lksb,
			res->flags, res->name, strlen(res->name),
			0, sync_ast, res, res->bast);
	if (ret)
		return ret;
	wait_for_completion(&res->completion);
	if (res->lksb.sb_status == 0)
		res->mode = mode;
	return res->lksb.sb_status;
}

static int dlm_unlock_sync(struct dlm_lock_resource *res)
{
	return dlm_lock_sync(res, DLM_LOCK_NL);
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
	init_completion(&res->completion);
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
	int ret;

	if (!res)
		return;

	/* cancel a lock request or a conversion request that is blocked */
	res->flags |= DLM_LKF_CANCEL;
retry:
	ret = dlm_unlock(res->ls, res->lksb.sb_lkid, 0, &res->lksb, res);
	if (unlikely(ret != 0)) {
		pr_info("%s: failed to unlock %s return %d\n", __func__, res->name, ret);

		/* if a lock conversion is cancelled, then the lock is put
		 * back to grant queue, need to ensure it is unlocked */
		if (ret == -DLM_ECANCEL)
			goto retry;
	}
	res->flags &= ~DLM_LKF_CANCEL;
	wait_for_completion(&res->completion);

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

		ret = dlm_lock_sync(bm_lockres, DLM_LOCK_PW);
		if (ret) {
			pr_err("md-cluster: Could not DLM lock %s: %d\n",
					str, ret);
			goto clear_bit;
		}
		ret = bitmap_copy_from_slot(mddev, slot, &lo, &hi, true);
		if (ret) {
			pr_err("md-cluster: Could not copy data from bitmap %d\n", slot);
			goto dlm_unlock;
		}
		if (hi > 0) {
			/* TODO:Wait for current resync to get over */
			set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			if (lo < mddev->recovery_cp)
				mddev->recovery_cp = lo;
			md_check_recovery(mddev);
		}
dlm_unlock:
		dlm_unlock_sync(bm_lockres);
clear_bit:
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

	if (mode == DLM_LOCK_EX)
		md_wakeup_thread(cinfo->recv_thread);
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
	spin_lock_irq(&cinfo->suspend_lock);
	__remove_suspend_info(cinfo, slot);
	spin_unlock_irq(&cinfo->suspend_lock);
	mddev->pers->quiesce(mddev, 2);
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
	s = kzalloc(sizeof(struct suspend_info), GFP_KERNEL);
	if (!s)
		return;
	s->slot = slot;
	s->lo = lo;
	s->hi = hi;
	mddev->pers->quiesce(mddev, 1);
	mddev->pers->quiesce(mddev, 0);
	spin_lock_irq(&cinfo->suspend_lock);
	/* Remove existing entry (if exists) before adding */
	__remove_suspend_info(cinfo, slot);
	list_add(&s->list, &cinfo->suspend_list);
	spin_unlock_irq(&cinfo->suspend_lock);
	mddev->pers->quiesce(mddev, 2);
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
	struct md_cluster_info *cinfo = mddev->cluster_info;
	md_reload_sb(mddev, le32_to_cpu(msg->raid_slot));
	dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_CR);
}

static void process_remove_disk(struct mddev *mddev, struct cluster_msg *msg)
{
	struct md_rdev *rdev = md_find_rdev_nr_rcu(mddev,
						   le32_to_cpu(msg->raid_slot));

	if (rdev)
		md_kick_rdev_from_array(rdev);
	else
		pr_warn("%s: %d Could not find disk(%d) to REMOVE\n",
			__func__, __LINE__, le32_to_cpu(msg->raid_slot));
}

static void process_readd_disk(struct mddev *mddev, struct cluster_msg *msg)
{
	struct md_rdev *rdev = md_find_rdev_nr_rcu(mddev,
						   le32_to_cpu(msg->raid_slot));

	if (rdev && test_bit(Faulty, &rdev->flags))
		clear_bit(Faulty, &rdev->flags);
	else
		pr_warn("%s: %d Could not find disk(%d) which is faulty",
			__func__, __LINE__, le32_to_cpu(msg->raid_slot));
}

static void process_recvd_msg(struct mddev *mddev, struct cluster_msg *msg)
{
	if (WARN(mddev->cluster_info->slot_number - 1 == le32_to_cpu(msg->slot),
		"node %d received it's own msg\n", le32_to_cpu(msg->slot)))
		return;
	switch (le32_to_cpu(msg->type)) {
	case METADATA_UPDATED:
		process_metadata_update(mddev, msg);
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
		pr_warn("%s:%d Received unknown message from %d\n",
			__func__, __LINE__, msg->slot);
	}
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

	/*get CR on Message*/
	if (dlm_lock_sync(message_lockres, DLM_LOCK_CR)) {
		pr_err("md/raid1:failed to get CR on MESSAGE\n");
		return;
	}

	/* read lvb and wake up thread to process this message_lockres */
	memcpy(&msg, message_lockres->lksb.sb_lvbptr, sizeof(struct cluster_msg));
	process_recvd_msg(thread->mddev, &msg);

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
	/*release CR on message_lockres*/
	ret = dlm_unlock_sync(message_lockres);
	if (unlikely(ret != 0))
		pr_info("unlock msg failed return %d\n", ret);
}

/* lock_comm()
 * Takes the lock on the TOKEN lock resource so no other
 * node can communicate while the operation is underway.
 * If called again, and the TOKEN lock is alread in EX mode
 * return success. However, care must be taken that unlock_comm()
 * is called only once.
 */
static int lock_comm(struct md_cluster_info *cinfo)
{
	int error;

	if (cinfo->token_lockres->mode == DLM_LOCK_EX)
		return 0;

	error = dlm_lock_sync(cinfo->token_lockres, DLM_LOCK_EX);
	if (error)
		pr_err("md-cluster(%s:%d): failed to get EX on TOKEN (%d)\n",
				__func__, __LINE__, error);
	return error;
}

static void unlock_comm(struct md_cluster_info *cinfo)
{
	WARN_ON(cinfo->token_lockres->mode != DLM_LOCK_EX);
	dlm_unlock_sync(cinfo->token_lockres);
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

static int sendmsg(struct md_cluster_info *cinfo, struct cluster_msg *cmsg)
{
	int ret;

	lock_comm(cinfo);
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
		if (i == (cinfo->slot_number - 1))
			continue;

		bm_lockres->flags |= DLM_LKF_NOQUEUE;
		ret = dlm_lock_sync(bm_lockres, DLM_LOCK_PW);
		if (ret == -EAGAIN) {
			memset(bm_lockres->lksb.sb_lvbptr, '\0', LVB_SIZE);
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

		dlm_unlock_sync(bm_lockres);
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

	mddev->cluster_info = cinfo;

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
	cinfo->ack_lockres = lockres_init(mddev, "ack", ack_bast, 0);
	if (!cinfo->ack_lockres)
		goto err;
	cinfo->no_new_dev_lockres = lockres_init(mddev, "no-new-dev", NULL, 0);
	if (!cinfo->no_new_dev_lockres)
		goto err;

	/* get sync CR lock on ACK. */
	if (dlm_lock_sync(cinfo->ack_lockres, DLM_LOCK_CR))
		pr_err("md-cluster: failed to get a sync CR lock on ACK!(%d)\n",
				ret);
	/* get sync CR lock on no-new-dev. */
	if (dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_CR))
		pr_err("md-cluster: failed to get a sync CR lock on no-new-dev!(%d)\n", ret);


	pr_info("md-cluster: Joined cluster %s slot %d\n", str, cinfo->slot_number);
	snprintf(str, 64, "bitmap%04d", cinfo->slot_number - 1);
	cinfo->bitmap_lockres = lockres_init(mddev, str, NULL, 1);
	if (!cinfo->bitmap_lockres)
		goto err;
	if (dlm_lock_sync(cinfo->bitmap_lockres, DLM_LOCK_PW)) {
		pr_err("Failed to get bitmap lock\n");
		ret = -EINVAL;
		goto err;
	}

	cinfo->resync_lockres = lockres_init(mddev, "resync", NULL, 0);
	if (!cinfo->resync_lockres)
		goto err;

	ret = gather_all_resync_info(mddev, nodes);
	if (ret)
		goto err;

	return 0;
err:
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

static void resync_bitmap(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct cluster_msg cmsg = {0};
	int err;

	cmsg.type = cpu_to_le32(BITMAP_NEEDS_SYNC);
	err = sendmsg(cinfo, &cmsg);
	if (err)
		pr_err("%s:%d: failed to send BITMAP_NEEDS_SYNC message (%d)\n",
			__func__, __LINE__, err);
}

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

	md_unregister_thread(&cinfo->recovery_thread);
	md_unregister_thread(&cinfo->recv_thread);
	lockres_free(cinfo->message_lockres);
	lockres_free(cinfo->token_lockres);
	lockres_free(cinfo->ack_lockres);
	lockres_free(cinfo->no_new_dev_lockres);
	lockres_free(cinfo->bitmap_lockres);
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

static int metadata_update_start(struct mddev *mddev)
{
	return lock_comm(mddev->cluster_info);
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
	unlock_comm(cinfo);
	return ret;
}

static void metadata_update_cancel(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	unlock_comm(cinfo);
}

static int resync_start(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	cinfo->resync_lockres->flags |= DLM_LKF_NOQUEUE;
	return dlm_lock_sync(cinfo->resync_lockres, DLM_LOCK_EX);
}

static int resync_info_update(struct mddev *mddev, sector_t lo, sector_t hi)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	struct cluster_msg cmsg = {0};

	add_resync_info(cinfo->bitmap_lockres, lo, hi);
	/* Re-acquire the lock to refresh LVB */
	dlm_lock_sync(cinfo->bitmap_lockres, DLM_LOCK_PW);
	cmsg.type = cpu_to_le32(RESYNCING);
	cmsg.low = cpu_to_le64(lo);
	cmsg.high = cpu_to_le64(hi);

	return sendmsg(cinfo, &cmsg);
}

static int resync_finish(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
	cinfo->resync_lockres->flags &= ~DLM_LKF_NOQUEUE;
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
	lock_comm(cinfo);
	ret = __sendmsg(cinfo, &cmsg);
	if (ret)
		return ret;
	cinfo->no_new_dev_lockres->flags |= DLM_LKF_NOQUEUE;
	ret = dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_EX);
	cinfo->no_new_dev_lockres->flags &= ~DLM_LKF_NOQUEUE;
	/* Some node does not "see" the device */
	if (ret == -EAGAIN)
		ret = -ENOENT;
	if (ret)
		unlock_comm(cinfo);
	else
		dlm_lock_sync(cinfo->no_new_dev_lockres, DLM_LOCK_CR);
	return ret;
}

static void add_new_disk_cancel(struct mddev *mddev)
{
	struct md_cluster_info *cinfo = mddev->cluster_info;
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
	return __sendmsg(cinfo, &cmsg);
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
	err = sendmsg(cinfo, &cmsg);
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
	.gather_bitmaps = gather_bitmaps,
};

static int __init cluster_init(void)
{
	pr_warn("md-cluster: EXPERIMENTAL. Use with caution\n");
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

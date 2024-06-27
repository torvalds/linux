// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " L" __stringify(__LINE__) ": " fmt

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/scatterlist.h>
#include <linux/idr.h>

#include "rnbd-clt.h"

MODULE_DESCRIPTION("RDMA Network Block Device Client");
MODULE_LICENSE("GPL");

static int rnbd_client_major;
static DEFINE_IDA(index_ida);
static DEFINE_MUTEX(sess_lock);
static LIST_HEAD(sess_list);
static struct workqueue_struct *rnbd_clt_wq;

/*
 * Maximum number of partitions an instance can have.
 * 6 bits = 64 minors = 63 partitions (one minor is used for the device itself)
 */
#define RNBD_PART_BITS		6

static inline bool rnbd_clt_get_sess(struct rnbd_clt_session *sess)
{
	return refcount_inc_not_zero(&sess->refcount);
}

static void free_sess(struct rnbd_clt_session *sess);

static void rnbd_clt_put_sess(struct rnbd_clt_session *sess)
{
	might_sleep();

	if (refcount_dec_and_test(&sess->refcount))
		free_sess(sess);
}

static void rnbd_clt_put_dev(struct rnbd_clt_dev *dev)
{
	might_sleep();

	if (!refcount_dec_and_test(&dev->refcount))
		return;

	ida_free(&index_ida, dev->clt_device_id);
	kfree(dev->hw_queues);
	kfree(dev->pathname);
	rnbd_clt_put_sess(dev->sess);
	mutex_destroy(&dev->lock);
	kfree(dev);
}

static inline bool rnbd_clt_get_dev(struct rnbd_clt_dev *dev)
{
	return refcount_inc_not_zero(&dev->refcount);
}

static void rnbd_clt_change_capacity(struct rnbd_clt_dev *dev,
				    sector_t new_nsectors)
{
	if (get_capacity(dev->gd) == new_nsectors)
		return;

	/*
	 * If the size changed, we need to revalidate it
	 */
	rnbd_clt_info(dev, "Device size changed from %llu to %llu sectors\n",
		      get_capacity(dev->gd), new_nsectors);
	set_capacity_and_notify(dev->gd, new_nsectors);
}

static int process_msg_open_rsp(struct rnbd_clt_dev *dev,
				struct rnbd_msg_open_rsp *rsp)
{
	struct kobject *gd_kobj;
	int err = 0;

	mutex_lock(&dev->lock);
	if (dev->dev_state == DEV_STATE_UNMAPPED) {
		rnbd_clt_info(dev,
			       "Ignoring Open-Response message from server for  unmapped device\n");
		err = -ENOENT;
		goto out;
	}
	if (dev->dev_state == DEV_STATE_MAPPED_DISCONNECTED) {
		u64 nsectors = le64_to_cpu(rsp->nsectors);

		rnbd_clt_change_capacity(dev, nsectors);
		gd_kobj = &disk_to_dev(dev->gd)->kobj;
		kobject_uevent(gd_kobj, KOBJ_ONLINE);
		rnbd_clt_info(dev, "Device online, device remapped successfully\n");
	}
	if (!rsp->logical_block_size) {
		err = -EINVAL;
		goto out;
	}
	dev->device_id = le32_to_cpu(rsp->device_id);
	dev->dev_state = DEV_STATE_MAPPED;

out:
	mutex_unlock(&dev->lock);

	return err;
}

int rnbd_clt_resize_disk(struct rnbd_clt_dev *dev, sector_t newsize)
{
	int ret = 0;

	mutex_lock(&dev->lock);
	if (dev->dev_state != DEV_STATE_MAPPED) {
		pr_err("Failed to set new size of the device, device is not opened\n");
		ret = -ENOENT;
		goto out;
	}
	rnbd_clt_change_capacity(dev, newsize);

out:
	mutex_unlock(&dev->lock);

	return ret;
}

static inline void rnbd_clt_dev_requeue(struct rnbd_queue *q)
{
	if (WARN_ON(!q->hctx))
		return;

	/* We can come here from interrupt, thus async=true */
	blk_mq_run_hw_queue(q->hctx, true);
}

enum {
	RNBD_DELAY_IFBUSY = -1,
};

/**
 * rnbd_get_cpu_qlist() - finds a list with HW queues to be rerun
 * @sess:	Session to find a queue for
 * @cpu:	Cpu to start the search from
 *
 * Description:
 *     Each CPU has a list of HW queues, which needs to be rerun.  If a list
 *     is not empty - it is marked with a bit.  This function finds first
 *     set bit in a bitmap and returns corresponding CPU list.
 */
static struct rnbd_cpu_qlist *
rnbd_get_cpu_qlist(struct rnbd_clt_session *sess, int cpu)
{
	int bit;

	/* Search from cpu to nr_cpu_ids */
	bit = find_next_bit(sess->cpu_queues_bm, nr_cpu_ids, cpu);
	if (bit < nr_cpu_ids) {
		return per_cpu_ptr(sess->cpu_queues, bit);
	} else if (cpu != 0) {
		/* Search from 0 to cpu */
		bit = find_first_bit(sess->cpu_queues_bm, cpu);
		if (bit < cpu)
			return per_cpu_ptr(sess->cpu_queues, bit);
	}

	return NULL;
}

static inline int nxt_cpu(int cpu)
{
	return (cpu + 1) % nr_cpu_ids;
}

/**
 * rnbd_rerun_if_needed() - rerun next queue marked as stopped
 * @sess:	Session to rerun a queue on
 *
 * Description:
 *     Each CPU has it's own list of HW queues, which should be rerun.
 *     Function finds such list with HW queues, takes a list lock, picks up
 *     the first HW queue out of the list and requeues it.
 *
 * Return:
 *     True if the queue was requeued, false otherwise.
 *
 * Context:
 *     Does not matter.
 */
static bool rnbd_rerun_if_needed(struct rnbd_clt_session *sess)
{
	struct rnbd_queue *q = NULL;
	struct rnbd_cpu_qlist *cpu_q;
	unsigned long flags;
	int *cpup;

	/*
	 * To keep fairness and not to let other queues starve we always
	 * try to wake up someone else in round-robin manner.  That of course
	 * increases latency but queues always have a chance to be executed.
	 */
	cpup = get_cpu_ptr(sess->cpu_rr);
	for (cpu_q = rnbd_get_cpu_qlist(sess, nxt_cpu(*cpup)); cpu_q;
	     cpu_q = rnbd_get_cpu_qlist(sess, nxt_cpu(cpu_q->cpu))) {
		if (!spin_trylock_irqsave(&cpu_q->requeue_lock, flags))
			continue;
		if (!test_bit(cpu_q->cpu, sess->cpu_queues_bm))
			goto unlock;
		q = list_first_entry_or_null(&cpu_q->requeue_list,
					     typeof(*q), requeue_list);
		if (WARN_ON(!q))
			goto clear_bit;
		list_del_init(&q->requeue_list);
		clear_bit_unlock(0, &q->in_list);

		if (list_empty(&cpu_q->requeue_list)) {
			/* Clear bit if nothing is left */
clear_bit:
			clear_bit(cpu_q->cpu, sess->cpu_queues_bm);
		}
unlock:
		spin_unlock_irqrestore(&cpu_q->requeue_lock, flags);

		if (q)
			break;
	}

	/**
	 * Saves the CPU that is going to be requeued on the per-cpu var. Just
	 * incrementing it doesn't work because rnbd_get_cpu_qlist() will
	 * always return the first CPU with something on the queue list when the
	 * value stored on the var is greater than the last CPU with something
	 * on the list.
	 */
	if (cpu_q)
		*cpup = cpu_q->cpu;
	put_cpu_ptr(sess->cpu_rr);

	if (q)
		rnbd_clt_dev_requeue(q);

	return q;
}

/**
 * rnbd_rerun_all_if_idle() - rerun all queues left in the list if
 *				 session is idling (there are no requests
 *				 in-flight).
 * @sess:	Session to rerun the queues on
 *
 * Description:
 *     This function tries to rerun all stopped queues if there are no
 *     requests in-flight anymore.  This function tries to solve an obvious
 *     problem, when number of tags < than number of queues (hctx), which
 *     are stopped and put to sleep.  If last permit, which has been just put,
 *     does not wake up all left queues (hctxs), IO requests hang forever.
 *
 *     That can happen when all number of permits, say N, have been exhausted
 *     from one CPU, and we have many block devices per session, say M.
 *     Each block device has it's own queue (hctx) for each CPU, so eventually
 *     we can put that number of queues (hctxs) to sleep: M x nr_cpu_ids.
 *     If number of permits N < M x nr_cpu_ids finally we will get an IO hang.
 *
 *     To avoid this hang last caller of rnbd_put_permit() (last caller is the
 *     one who observes sess->busy == 0) must wake up all remaining queues.
 *
 * Context:
 *     Does not matter.
 */
static void rnbd_rerun_all_if_idle(struct rnbd_clt_session *sess)
{
	bool requeued;

	do {
		requeued = rnbd_rerun_if_needed(sess);
	} while (atomic_read(&sess->busy) == 0 && requeued);
}

static struct rtrs_permit *rnbd_get_permit(struct rnbd_clt_session *sess,
					     enum rtrs_clt_con_type con_type,
					     enum wait_type wait)
{
	struct rtrs_permit *permit;

	permit = rtrs_clt_get_permit(sess->rtrs, con_type, wait);
	if (permit)
		/* We have a subtle rare case here, when all permits can be
		 * consumed before busy counter increased.  This is safe,
		 * because loser will get NULL as a permit, observe 0 busy
		 * counter and immediately restart the queue himself.
		 */
		atomic_inc(&sess->busy);

	return permit;
}

static void rnbd_put_permit(struct rnbd_clt_session *sess,
			     struct rtrs_permit *permit)
{
	rtrs_clt_put_permit(sess->rtrs, permit);
	atomic_dec(&sess->busy);
	/* Paired with rnbd_clt_dev_add_to_requeue().  Decrement first
	 * and then check queue bits.
	 */
	smp_mb__after_atomic();
	rnbd_rerun_all_if_idle(sess);
}

static struct rnbd_iu *rnbd_get_iu(struct rnbd_clt_session *sess,
				     enum rtrs_clt_con_type con_type,
				     enum wait_type wait)
{
	struct rnbd_iu *iu;
	struct rtrs_permit *permit;

	iu = kzalloc(sizeof(*iu), GFP_KERNEL);
	if (!iu)
		return NULL;

	permit = rnbd_get_permit(sess, con_type, wait);
	if (!permit) {
		kfree(iu);
		return NULL;
	}

	iu->permit = permit;
	/*
	 * 1st reference is dropped after finishing sending a "user" message,
	 * 2nd reference is dropped after confirmation with the response is
	 * returned.
	 * 1st and 2nd can happen in any order, so the rnbd_iu should be
	 * released (rtrs_permit returned to rtrs) only after both
	 * are finished.
	 */
	atomic_set(&iu->refcount, 2);
	init_waitqueue_head(&iu->comp.wait);
	iu->comp.errno = INT_MAX;

	if (sg_alloc_table(&iu->sgt, 1, GFP_KERNEL)) {
		rnbd_put_permit(sess, permit);
		kfree(iu);
		return NULL;
	}

	return iu;
}

static void rnbd_put_iu(struct rnbd_clt_session *sess, struct rnbd_iu *iu)
{
	if (atomic_dec_and_test(&iu->refcount)) {
		sg_free_table(&iu->sgt);
		rnbd_put_permit(sess, iu->permit);
		kfree(iu);
	}
}

static void rnbd_softirq_done_fn(struct request *rq)
{
	struct rnbd_clt_dev *dev	= rq->q->disk->private_data;
	struct rnbd_clt_session *sess	= dev->sess;
	struct rnbd_iu *iu;

	iu = blk_mq_rq_to_pdu(rq);
	sg_free_table_chained(&iu->sgt, RNBD_INLINE_SG_CNT);
	rnbd_put_permit(sess, iu->permit);
	blk_mq_end_request(rq, errno_to_blk_status(iu->errno));
}

static void msg_io_conf(void *priv, int errno)
{
	struct rnbd_iu *iu = priv;
	struct rnbd_clt_dev *dev = iu->dev;
	struct request *rq = iu->rq;
	int rw = rq_data_dir(rq);

	iu->errno = errno;

	blk_mq_complete_request(rq);

	if (errno)
		rnbd_clt_info_rl(dev, "%s I/O failed with err: %d\n",
				 rw == READ ? "read" : "write", errno);
}

static void wake_up_iu_comp(struct rnbd_iu *iu, int errno)
{
	iu->comp.errno = errno;
	wake_up(&iu->comp.wait);
}

static void msg_conf(void *priv, int errno)
{
	struct rnbd_iu *iu = priv;

	iu->errno = errno;
	schedule_work(&iu->work);
}

static int send_usr_msg(struct rtrs_clt_sess *rtrs, int dir,
			struct rnbd_iu *iu, struct kvec *vec,
			size_t len, struct scatterlist *sg, unsigned int sg_len,
			void (*conf)(struct work_struct *work),
			int *errno, int wait)
{
	int err;
	struct rtrs_clt_req_ops req_ops;

	INIT_WORK(&iu->work, conf);
	req_ops = (struct rtrs_clt_req_ops) {
		.priv = iu,
		.conf_fn = msg_conf,
	};
	err = rtrs_clt_request(dir, &req_ops, rtrs, iu->permit,
				vec, 1, len, sg, sg_len);
	if (!err && wait) {
		wait_event(iu->comp.wait, iu->comp.errno != INT_MAX);
		*errno = iu->comp.errno;
	} else {
		*errno = 0;
	}

	return err;
}

static void msg_close_conf(struct work_struct *work)
{
	struct rnbd_iu *iu = container_of(work, struct rnbd_iu, work);
	struct rnbd_clt_dev *dev = iu->dev;

	wake_up_iu_comp(iu, iu->errno);
	rnbd_put_iu(dev->sess, iu);
	rnbd_clt_put_dev(dev);
}

static int send_msg_close(struct rnbd_clt_dev *dev, u32 device_id,
			  enum wait_type wait)
{
	struct rnbd_clt_session *sess = dev->sess;
	struct rnbd_msg_close msg;
	struct rnbd_iu *iu;
	struct kvec vec = {
		.iov_base = &msg,
		.iov_len  = sizeof(msg)
	};
	int err, errno;

	iu = rnbd_get_iu(sess, RTRS_ADMIN_CON, RTRS_PERMIT_WAIT);
	if (!iu)
		return -ENOMEM;

	iu->buf = NULL;
	iu->dev = dev;

	msg.hdr.type	= cpu_to_le16(RNBD_MSG_CLOSE);
	msg.device_id	= cpu_to_le32(device_id);

	WARN_ON(!rnbd_clt_get_dev(dev));
	err = send_usr_msg(sess->rtrs, WRITE, iu, &vec, 0, NULL, 0,
			   msg_close_conf, &errno, wait);
	if (err) {
		rnbd_clt_put_dev(dev);
		rnbd_put_iu(sess, iu);
	} else {
		err = errno;
	}

	rnbd_put_iu(sess, iu);
	return err;
}

static void msg_open_conf(struct work_struct *work)
{
	struct rnbd_iu *iu = container_of(work, struct rnbd_iu, work);
	struct rnbd_msg_open_rsp *rsp = iu->buf;
	struct rnbd_clt_dev *dev = iu->dev;
	int errno = iu->errno;
	bool from_map = false;

	/* INIT state is only triggered from rnbd_clt_map_device */
	if (dev->dev_state == DEV_STATE_INIT)
		from_map = true;

	if (errno) {
		rnbd_clt_err(dev,
			      "Opening failed, server responded: %d\n",
			      errno);
	} else {
		errno = process_msg_open_rsp(dev, rsp);
		if (errno) {
			u32 device_id = le32_to_cpu(rsp->device_id);
			/*
			 * If server thinks its fine, but we fail to process
			 * then be nice and send a close to server.
			 */
			send_msg_close(dev, device_id, RTRS_PERMIT_NOWAIT);
		}
	}
	/* We free rsp in rnbd_clt_map_device for map scenario */
	if (!from_map)
		kfree(rsp);
	wake_up_iu_comp(iu, errno);
	rnbd_put_iu(dev->sess, iu);
	rnbd_clt_put_dev(dev);
}

static void msg_sess_info_conf(struct work_struct *work)
{
	struct rnbd_iu *iu = container_of(work, struct rnbd_iu, work);
	struct rnbd_msg_sess_info_rsp *rsp = iu->buf;
	struct rnbd_clt_session *sess = iu->sess;

	if (!iu->errno)
		sess->ver = min_t(u8, rsp->ver, RNBD_PROTO_VER_MAJOR);

	kfree(rsp);
	wake_up_iu_comp(iu, iu->errno);
	rnbd_put_iu(sess, iu);
	rnbd_clt_put_sess(sess);
}

static int send_msg_open(struct rnbd_clt_dev *dev, enum wait_type wait)
{
	struct rnbd_clt_session *sess = dev->sess;
	struct rnbd_msg_open_rsp *rsp;
	struct rnbd_msg_open msg;
	struct rnbd_iu *iu;
	struct kvec vec = {
		.iov_base = &msg,
		.iov_len  = sizeof(msg)
	};
	int err, errno;

	rsp = kzalloc(sizeof(*rsp), GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	iu = rnbd_get_iu(sess, RTRS_ADMIN_CON, RTRS_PERMIT_WAIT);
	if (!iu) {
		kfree(rsp);
		return -ENOMEM;
	}

	iu->buf = rsp;
	iu->dev = dev;

	sg_init_one(iu->sgt.sgl, rsp, sizeof(*rsp));

	msg.hdr.type	= cpu_to_le16(RNBD_MSG_OPEN);
	msg.access_mode	= dev->access_mode;
	strscpy(msg.dev_name, dev->pathname, sizeof(msg.dev_name));

	WARN_ON(!rnbd_clt_get_dev(dev));
	err = send_usr_msg(sess->rtrs, READ, iu,
			   &vec, sizeof(*rsp), iu->sgt.sgl, 1,
			   msg_open_conf, &errno, wait);
	if (err) {
		rnbd_clt_put_dev(dev);
		rnbd_put_iu(sess, iu);
		kfree(rsp);
	} else {
		err = errno;
	}

	rnbd_put_iu(sess, iu);
	return err;
}

static int send_msg_sess_info(struct rnbd_clt_session *sess, enum wait_type wait)
{
	struct rnbd_msg_sess_info_rsp *rsp;
	struct rnbd_msg_sess_info msg;
	struct rnbd_iu *iu;
	struct kvec vec = {
		.iov_base = &msg,
		.iov_len  = sizeof(msg)
	};
	int err, errno;

	rsp = kzalloc(sizeof(*rsp), GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	iu = rnbd_get_iu(sess, RTRS_ADMIN_CON, RTRS_PERMIT_WAIT);
	if (!iu) {
		kfree(rsp);
		return -ENOMEM;
	}

	iu->buf = rsp;
	iu->sess = sess;
	sg_init_one(iu->sgt.sgl, rsp, sizeof(*rsp));

	msg.hdr.type = cpu_to_le16(RNBD_MSG_SESS_INFO);
	msg.ver      = RNBD_PROTO_VER_MAJOR;

	if (!rnbd_clt_get_sess(sess)) {
		/*
		 * That can happen only in one case, when RTRS has restablished
		 * the connection and link_ev() is called, but session is almost
		 * dead, last reference on session is put and caller is waiting
		 * for RTRS to close everything.
		 */
		err = -ENODEV;
		goto put_iu;
	}
	err = send_usr_msg(sess->rtrs, READ, iu,
			   &vec, sizeof(*rsp), iu->sgt.sgl, 1,
			   msg_sess_info_conf, &errno, wait);
	if (err) {
		rnbd_clt_put_sess(sess);
put_iu:
		rnbd_put_iu(sess, iu);
		kfree(rsp);
	} else {
		err = errno;
	}
	rnbd_put_iu(sess, iu);
	return err;
}

static void set_dev_states_to_disconnected(struct rnbd_clt_session *sess)
{
	struct rnbd_clt_dev *dev;
	struct kobject *gd_kobj;

	mutex_lock(&sess->lock);
	list_for_each_entry(dev, &sess->devs_list, list) {
		rnbd_clt_err(dev, "Device disconnected.\n");

		mutex_lock(&dev->lock);
		if (dev->dev_state == DEV_STATE_MAPPED) {
			dev->dev_state = DEV_STATE_MAPPED_DISCONNECTED;
			gd_kobj = &disk_to_dev(dev->gd)->kobj;
			kobject_uevent(gd_kobj, KOBJ_OFFLINE);
		}
		mutex_unlock(&dev->lock);
	}
	mutex_unlock(&sess->lock);
}

static void remap_devs(struct rnbd_clt_session *sess)
{
	struct rnbd_clt_dev *dev;
	struct rtrs_attrs attrs;
	int err;

	/*
	 * Careful here: we are called from RTRS link event directly,
	 * thus we can't send any RTRS request and wait for response
	 * or RTRS will not be able to complete request with failure
	 * if something goes wrong (failing of outstanding requests
	 * happens exactly from the context where we are blocking now).
	 *
	 * So to avoid deadlocks each usr message sent from here must
	 * be asynchronous.
	 */

	err = send_msg_sess_info(sess, RTRS_PERMIT_NOWAIT);
	if (err) {
		pr_err("send_msg_sess_info(\"%s\"): %d\n", sess->sessname, err);
		return;
	}

	err = rtrs_clt_query(sess->rtrs, &attrs);
	if (err) {
		pr_err("rtrs_clt_query(\"%s\"): %d\n", sess->sessname, err);
		return;
	}
	mutex_lock(&sess->lock);
	sess->max_io_size = attrs.max_io_size;

	list_for_each_entry(dev, &sess->devs_list, list) {
		bool skip;

		mutex_lock(&dev->lock);
		skip = (dev->dev_state == DEV_STATE_INIT);
		mutex_unlock(&dev->lock);
		if (skip)
			/*
			 * When device is establishing connection for the first
			 * time - do not remap, it will be closed soon.
			 */
			continue;

		rnbd_clt_info(dev, "session reconnected, remapping device\n");
		err = send_msg_open(dev, RTRS_PERMIT_NOWAIT);
		if (err) {
			rnbd_clt_err(dev, "send_msg_open(): %d\n", err);
			break;
		}
	}
	mutex_unlock(&sess->lock);
}

static void rnbd_clt_link_ev(void *priv, enum rtrs_clt_link_ev ev)
{
	struct rnbd_clt_session *sess = priv;

	switch (ev) {
	case RTRS_CLT_LINK_EV_DISCONNECTED:
		set_dev_states_to_disconnected(sess);
		break;
	case RTRS_CLT_LINK_EV_RECONNECTED:
		remap_devs(sess);
		break;
	default:
		pr_err("Unknown session event received (%d), session: %s\n",
		       ev, sess->sessname);
	}
}

static void rnbd_init_cpu_qlists(struct rnbd_cpu_qlist __percpu *cpu_queues)
{
	unsigned int cpu;
	struct rnbd_cpu_qlist *cpu_q;

	for_each_possible_cpu(cpu) {
		cpu_q = per_cpu_ptr(cpu_queues, cpu);

		cpu_q->cpu = cpu;
		INIT_LIST_HEAD(&cpu_q->requeue_list);
		spin_lock_init(&cpu_q->requeue_lock);
	}
}

static void destroy_mq_tags(struct rnbd_clt_session *sess)
{
	if (sess->tag_set.tags)
		blk_mq_free_tag_set(&sess->tag_set);
}

static inline void wake_up_rtrs_waiters(struct rnbd_clt_session *sess)
{
	sess->rtrs_ready = true;
	wake_up_all(&sess->rtrs_waitq);
}

static void close_rtrs(struct rnbd_clt_session *sess)
{
	might_sleep();

	if (!IS_ERR_OR_NULL(sess->rtrs)) {
		rtrs_clt_close(sess->rtrs);
		sess->rtrs = NULL;
		wake_up_rtrs_waiters(sess);
	}
}

static void free_sess(struct rnbd_clt_session *sess)
{
	WARN_ON(!list_empty(&sess->devs_list));

	might_sleep();

	close_rtrs(sess);
	destroy_mq_tags(sess);
	if (!list_empty(&sess->list)) {
		mutex_lock(&sess_lock);
		list_del(&sess->list);
		mutex_unlock(&sess_lock);
	}
	free_percpu(sess->cpu_queues);
	free_percpu(sess->cpu_rr);
	mutex_destroy(&sess->lock);
	kfree(sess);
}

static struct rnbd_clt_session *alloc_sess(const char *sessname)
{
	struct rnbd_clt_session *sess;
	int err, cpu;

	sess = kzalloc_node(sizeof(*sess), GFP_KERNEL, NUMA_NO_NODE);
	if (!sess)
		return ERR_PTR(-ENOMEM);
	strscpy(sess->sessname, sessname, sizeof(sess->sessname));
	atomic_set(&sess->busy, 0);
	mutex_init(&sess->lock);
	INIT_LIST_HEAD(&sess->devs_list);
	INIT_LIST_HEAD(&sess->list);
	bitmap_zero(sess->cpu_queues_bm, num_possible_cpus());
	init_waitqueue_head(&sess->rtrs_waitq);
	refcount_set(&sess->refcount, 1);

	sess->cpu_queues = alloc_percpu(struct rnbd_cpu_qlist);
	if (!sess->cpu_queues) {
		err = -ENOMEM;
		goto err;
	}
	rnbd_init_cpu_qlists(sess->cpu_queues);

	/*
	 * That is simple percpu variable which stores cpu indices, which are
	 * incremented on each access.  We need that for the sake of fairness
	 * to wake up queues in a round-robin manner.
	 */
	sess->cpu_rr = alloc_percpu(int);
	if (!sess->cpu_rr) {
		err = -ENOMEM;
		goto err;
	}
	for_each_possible_cpu(cpu)
		* per_cpu_ptr(sess->cpu_rr, cpu) = cpu;

	return sess;

err:
	free_sess(sess);

	return ERR_PTR(err);
}

static int wait_for_rtrs_connection(struct rnbd_clt_session *sess)
{
	wait_event(sess->rtrs_waitq, sess->rtrs_ready);
	if (IS_ERR_OR_NULL(sess->rtrs))
		return -ECONNRESET;

	return 0;
}

static void wait_for_rtrs_disconnection(struct rnbd_clt_session *sess)
	__releases(&sess_lock)
	__acquires(&sess_lock)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&sess->rtrs_waitq, &wait, TASK_UNINTERRUPTIBLE);
	if (IS_ERR_OR_NULL(sess->rtrs)) {
		finish_wait(&sess->rtrs_waitq, &wait);
		return;
	}
	mutex_unlock(&sess_lock);
	/* loop in caller, see __find_and_get_sess().
	 * You can't leave mutex locked and call schedule(), you will catch a
	 * deadlock with a caller of free_sess(), which has just put the last
	 * reference and is about to take the sess_lock in order to delete
	 * the session from the list.
	 */
	schedule();
	mutex_lock(&sess_lock);
}

static struct rnbd_clt_session *__find_and_get_sess(const char *sessname)
	__releases(&sess_lock)
	__acquires(&sess_lock)
{
	struct rnbd_clt_session *sess, *sn;
	int err;

again:
	list_for_each_entry_safe(sess, sn, &sess_list, list) {
		if (strcmp(sessname, sess->sessname))
			continue;

		if (sess->rtrs_ready && IS_ERR_OR_NULL(sess->rtrs))
			/*
			 * No RTRS connection, session is dying.
			 */
			continue;

		if (rnbd_clt_get_sess(sess)) {
			/*
			 * Alive session is found, wait for RTRS connection.
			 */
			mutex_unlock(&sess_lock);
			err = wait_for_rtrs_connection(sess);
			if (err)
				rnbd_clt_put_sess(sess);
			mutex_lock(&sess_lock);

			if (err)
				/* Session is dying, repeat the loop */
				goto again;

			return sess;
		}
		/*
		 * Ref is 0, session is dying, wait for RTRS disconnect
		 * in order to avoid session names clashes.
		 */
		wait_for_rtrs_disconnection(sess);
		/*
		 * RTRS is disconnected and soon session will be freed,
		 * so repeat a loop.
		 */
		goto again;
	}

	return NULL;
}

/* caller is responsible for initializing 'first' to false */
static struct
rnbd_clt_session *find_or_create_sess(const char *sessname, bool *first)
{
	struct rnbd_clt_session *sess = NULL;

	mutex_lock(&sess_lock);
	sess = __find_and_get_sess(sessname);
	if (!sess) {
		sess = alloc_sess(sessname);
		if (IS_ERR(sess)) {
			mutex_unlock(&sess_lock);
			return sess;
		}
		list_add(&sess->list, &sess_list);
		*first = true;
	}
	mutex_unlock(&sess_lock);

	return sess;
}

static int rnbd_client_open(struct gendisk *disk, blk_mode_t mode)
{
	struct rnbd_clt_dev *dev = disk->private_data;

	if (get_disk_ro(dev->gd) && (mode & BLK_OPEN_WRITE))
		return -EPERM;

	if (dev->dev_state == DEV_STATE_UNMAPPED ||
	    !rnbd_clt_get_dev(dev))
		return -EIO;

	return 0;
}

static void rnbd_client_release(struct gendisk *gen)
{
	struct rnbd_clt_dev *dev = gen->private_data;

	rnbd_clt_put_dev(dev);
}

static int rnbd_client_getgeo(struct block_device *block_device,
			      struct hd_geometry *geo)
{
	u64 size;
	struct rnbd_clt_dev *dev = block_device->bd_disk->private_data;
	struct queue_limits *limit = &dev->queue->limits;

	size = dev->size * (limit->logical_block_size / SECTOR_SIZE);
	geo->cylinders	= size >> 6;	/* size/64 */
	geo->heads	= 4;
	geo->sectors	= 16;
	geo->start	= 0;

	return 0;
}

static const struct block_device_operations rnbd_client_ops = {
	.owner		= THIS_MODULE,
	.open		= rnbd_client_open,
	.release	= rnbd_client_release,
	.getgeo		= rnbd_client_getgeo
};

/* The amount of data that belongs to an I/O and the amount of data that
 * should be read or written to the disk (bi_size) can differ.
 *
 * E.g. When WRITE_SAME is used, only a small amount of data is
 * transferred that is then written repeatedly over a lot of sectors.
 *
 * Get the size of data to be transferred via RTRS by summing up the size
 * of the scather-gather list entries.
 */
static size_t rnbd_clt_get_sg_size(struct scatterlist *sglist, u32 len)
{
	struct scatterlist *sg;
	size_t tsize = 0;
	int i;

	for_each_sg(sglist, sg, len, i)
		tsize += sg->length;
	return tsize;
}

static int rnbd_client_xfer_request(struct rnbd_clt_dev *dev,
				     struct request *rq,
				     struct rnbd_iu *iu)
{
	struct rtrs_clt_sess *rtrs = dev->sess->rtrs;
	struct rtrs_permit *permit = iu->permit;
	struct rnbd_msg_io msg;
	struct rtrs_clt_req_ops req_ops;
	unsigned int sg_cnt = 0;
	struct kvec vec;
	size_t size;
	int err;

	iu->rq		= rq;
	iu->dev		= dev;
	msg.sector	= cpu_to_le64(blk_rq_pos(rq));
	msg.bi_size	= cpu_to_le32(blk_rq_bytes(rq));
	msg.rw		= cpu_to_le32(rq_to_rnbd_flags(rq));
	msg.prio	= cpu_to_le16(req_get_ioprio(rq));

	/*
	 * We only support discards/WRITE_ZEROES with single segment for now.
	 * See queue limits.
	 */
	if ((req_op(rq) != REQ_OP_DISCARD) && (req_op(rq) != REQ_OP_WRITE_ZEROES))
		sg_cnt = blk_rq_map_sg(dev->queue, rq, iu->sgt.sgl);

	if (sg_cnt == 0)
		sg_mark_end(&iu->sgt.sgl[0]);

	msg.hdr.type	= cpu_to_le16(RNBD_MSG_IO);
	msg.device_id	= cpu_to_le32(dev->device_id);

	vec = (struct kvec) {
		.iov_base = &msg,
		.iov_len  = sizeof(msg)
	};
	size = rnbd_clt_get_sg_size(iu->sgt.sgl, sg_cnt);
	req_ops = (struct rtrs_clt_req_ops) {
		.priv = iu,
		.conf_fn = msg_io_conf,
	};
	err = rtrs_clt_request(rq_data_dir(rq), &req_ops, rtrs, permit,
			       &vec, 1, size, iu->sgt.sgl, sg_cnt);
	if (err) {
		rnbd_clt_err_rl(dev, "RTRS failed to transfer IO, err: %d\n",
				 err);
		return err;
	}

	return 0;
}

/**
 * rnbd_clt_dev_add_to_requeue() - add device to requeue if session is busy
 * @dev:	Device to be checked
 * @q:		Queue to be added to the requeue list if required
 *
 * Description:
 *     If session is busy, that means someone will requeue us when resources
 *     are freed.  If session is not doing anything - device is not added to
 *     the list and @false is returned.
 */
static bool rnbd_clt_dev_add_to_requeue(struct rnbd_clt_dev *dev,
						struct rnbd_queue *q)
{
	struct rnbd_clt_session *sess = dev->sess;
	struct rnbd_cpu_qlist *cpu_q;
	unsigned long flags;
	bool added = true;
	bool need_set;

	cpu_q = get_cpu_ptr(sess->cpu_queues);
	spin_lock_irqsave(&cpu_q->requeue_lock, flags);

	if (!test_and_set_bit_lock(0, &q->in_list)) {
		if (WARN_ON(!list_empty(&q->requeue_list)))
			goto unlock;

		need_set = !test_bit(cpu_q->cpu, sess->cpu_queues_bm);
		if (need_set) {
			set_bit(cpu_q->cpu, sess->cpu_queues_bm);
			/* Paired with rnbd_put_permit(). Set a bit first
			 * and then observe the busy counter.
			 */
			smp_mb__before_atomic();
		}
		if (atomic_read(&sess->busy)) {
			list_add_tail(&q->requeue_list, &cpu_q->requeue_list);
		} else {
			/* Very unlikely, but possible: busy counter was
			 * observed as zero.  Drop all bits and return
			 * false to restart the queue by ourselves.
			 */
			if (need_set)
				clear_bit(cpu_q->cpu, sess->cpu_queues_bm);
			clear_bit_unlock(0, &q->in_list);
			added = false;
		}
	}
unlock:
	spin_unlock_irqrestore(&cpu_q->requeue_lock, flags);
	put_cpu_ptr(sess->cpu_queues);

	return added;
}

static void rnbd_clt_dev_kick_mq_queue(struct rnbd_clt_dev *dev,
					struct blk_mq_hw_ctx *hctx,
					int delay)
{
	struct rnbd_queue *q = hctx->driver_data;

	if (delay != RNBD_DELAY_IFBUSY)
		blk_mq_delay_run_hw_queue(hctx, delay);
	else if (!rnbd_clt_dev_add_to_requeue(dev, q))
		/*
		 * If session is not busy we have to restart
		 * the queue ourselves.
		 */
		blk_mq_delay_run_hw_queue(hctx, 10/*ms*/);
}

static blk_status_t rnbd_queue_rq(struct blk_mq_hw_ctx *hctx,
				   const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct rnbd_clt_dev *dev = rq->q->disk->private_data;
	struct rnbd_iu *iu = blk_mq_rq_to_pdu(rq);
	int err;
	blk_status_t ret = BLK_STS_IOERR;

	if (dev->dev_state != DEV_STATE_MAPPED)
		return BLK_STS_IOERR;

	iu->permit = rnbd_get_permit(dev->sess, RTRS_IO_CON,
				      RTRS_PERMIT_NOWAIT);
	if (!iu->permit) {
		rnbd_clt_dev_kick_mq_queue(dev, hctx, RNBD_DELAY_IFBUSY);
		return BLK_STS_RESOURCE;
	}

	iu->sgt.sgl = iu->first_sgl;
	err = sg_alloc_table_chained(&iu->sgt,
				     /* Even-if the request has no segment,
				      * sglist must have one entry at least.
				      */
				     blk_rq_nr_phys_segments(rq) ? : 1,
				     iu->sgt.sgl,
				     RNBD_INLINE_SG_CNT);
	if (err) {
		rnbd_clt_err_rl(dev, "sg_alloc_table_chained ret=%d\n", err);
		rnbd_clt_dev_kick_mq_queue(dev, hctx, 10/*ms*/);
		rnbd_put_permit(dev->sess, iu->permit);
		return BLK_STS_RESOURCE;
	}

	blk_mq_start_request(rq);
	err = rnbd_client_xfer_request(dev, rq, iu);
	if (err == 0)
		return BLK_STS_OK;
	if (err == -EAGAIN || err == -ENOMEM) {
		rnbd_clt_dev_kick_mq_queue(dev, hctx, 10/*ms*/);
		ret = BLK_STS_RESOURCE;
	}
	sg_free_table_chained(&iu->sgt, RNBD_INLINE_SG_CNT);
	rnbd_put_permit(dev->sess, iu->permit);
	return ret;
}

static int rnbd_rdma_poll(struct blk_mq_hw_ctx *hctx, struct io_comp_batch *iob)
{
	struct rnbd_queue *q = hctx->driver_data;
	struct rnbd_clt_dev *dev = q->dev;

	return rtrs_clt_rdma_cq_direct(dev->sess->rtrs, hctx->queue_num);
}

static void rnbd_rdma_map_queues(struct blk_mq_tag_set *set)
{
	struct rnbd_clt_session *sess = set->driver_data;

	/* shared read/write queues */
	set->map[HCTX_TYPE_DEFAULT].nr_queues = num_online_cpus();
	set->map[HCTX_TYPE_DEFAULT].queue_offset = 0;
	set->map[HCTX_TYPE_READ].nr_queues = num_online_cpus();
	set->map[HCTX_TYPE_READ].queue_offset = 0;
	blk_mq_map_queues(&set->map[HCTX_TYPE_DEFAULT]);
	blk_mq_map_queues(&set->map[HCTX_TYPE_READ]);

	if (sess->nr_poll_queues) {
		/* dedicated queue for poll */
		set->map[HCTX_TYPE_POLL].nr_queues = sess->nr_poll_queues;
		set->map[HCTX_TYPE_POLL].queue_offset = set->map[HCTX_TYPE_READ].queue_offset +
			set->map[HCTX_TYPE_READ].nr_queues;
		blk_mq_map_queues(&set->map[HCTX_TYPE_POLL]);
		pr_info("[session=%s] mapped %d/%d/%d default/read/poll queues.\n",
			sess->sessname,
			set->map[HCTX_TYPE_DEFAULT].nr_queues,
			set->map[HCTX_TYPE_READ].nr_queues,
			set->map[HCTX_TYPE_POLL].nr_queues);
	} else {
		pr_info("[session=%s] mapped %d/%d default/read queues.\n",
			sess->sessname,
			set->map[HCTX_TYPE_DEFAULT].nr_queues,
			set->map[HCTX_TYPE_READ].nr_queues);
	}
}

static struct blk_mq_ops rnbd_mq_ops = {
	.queue_rq	= rnbd_queue_rq,
	.complete	= rnbd_softirq_done_fn,
	.map_queues     = rnbd_rdma_map_queues,
	.poll           = rnbd_rdma_poll,
};

static int setup_mq_tags(struct rnbd_clt_session *sess)
{
	struct blk_mq_tag_set *tag_set = &sess->tag_set;

	memset(tag_set, 0, sizeof(*tag_set));
	tag_set->ops		= &rnbd_mq_ops;
	tag_set->queue_depth	= sess->queue_depth;
	tag_set->numa_node		= NUMA_NO_NODE;
	tag_set->flags		= BLK_MQ_F_SHOULD_MERGE |
				  BLK_MQ_F_TAG_QUEUE_SHARED;
	tag_set->cmd_size	= sizeof(struct rnbd_iu) + RNBD_RDMA_SGL_SIZE;

	/* for HCTX_TYPE_DEFAULT, HCTX_TYPE_READ, HCTX_TYPE_POLL */
	tag_set->nr_maps        = sess->nr_poll_queues ? HCTX_MAX_TYPES : 2;
	/*
	 * HCTX_TYPE_DEFAULT and HCTX_TYPE_READ share one set of queues
	 * others are for HCTX_TYPE_POLL
	 */
	tag_set->nr_hw_queues	= num_online_cpus() + sess->nr_poll_queues;
	tag_set->driver_data    = sess;

	return blk_mq_alloc_tag_set(tag_set);
}

static struct rnbd_clt_session *
find_and_get_or_create_sess(const char *sessname,
			    const struct rtrs_addr *paths,
			    size_t path_cnt, u16 port_nr, u32 nr_poll_queues)
{
	struct rnbd_clt_session *sess;
	struct rtrs_attrs attrs;
	int err;
	bool first = false;
	struct rtrs_clt_ops rtrs_ops;

	sess = find_or_create_sess(sessname, &first);
	if (sess == ERR_PTR(-ENOMEM)) {
		return ERR_PTR(-ENOMEM);
	} else if ((nr_poll_queues && !first) ||  (!nr_poll_queues && sess->nr_poll_queues)) {
		/*
		 * A device MUST have its own session to use the polling-mode.
		 * It must fail to map new device with the same session.
		 */
		err = -EINVAL;
		goto put_sess;
	}

	if (!first)
		return sess;

	if (!path_cnt) {
		pr_err("Session %s not found, and path parameter not given", sessname);
		err = -ENXIO;
		goto put_sess;
	}

	rtrs_ops = (struct rtrs_clt_ops) {
		.priv = sess,
		.link_ev = rnbd_clt_link_ev,
	};
	/*
	 * Nothing was found, establish rtrs connection and proceed further.
	 */
	sess->rtrs = rtrs_clt_open(&rtrs_ops, sessname,
				   paths, path_cnt, port_nr,
				   0, /* Do not use pdu of rtrs */
				   RECONNECT_DELAY,
				   MAX_RECONNECTS, nr_poll_queues);
	if (IS_ERR(sess->rtrs)) {
		err = PTR_ERR(sess->rtrs);
		goto wake_up_and_put;
	}

	err = rtrs_clt_query(sess->rtrs, &attrs);
	if (err)
		goto close_rtrs;

	sess->max_io_size = attrs.max_io_size;
	sess->queue_depth = attrs.queue_depth;
	sess->nr_poll_queues = nr_poll_queues;
	sess->max_segments = attrs.max_segments;

	err = setup_mq_tags(sess);
	if (err)
		goto close_rtrs;

	err = send_msg_sess_info(sess, RTRS_PERMIT_WAIT);
	if (err)
		goto close_rtrs;

	wake_up_rtrs_waiters(sess);

	return sess;

close_rtrs:
	close_rtrs(sess);
put_sess:
	rnbd_clt_put_sess(sess);

	return ERR_PTR(err);

wake_up_and_put:
	wake_up_rtrs_waiters(sess);
	goto put_sess;
}

static inline void rnbd_init_hw_queue(struct rnbd_clt_dev *dev,
				       struct rnbd_queue *q,
				       struct blk_mq_hw_ctx *hctx)
{
	INIT_LIST_HEAD(&q->requeue_list);
	q->dev  = dev;
	q->hctx = hctx;
}

static void rnbd_init_mq_hw_queues(struct rnbd_clt_dev *dev)
{
	unsigned long i;
	struct blk_mq_hw_ctx *hctx;
	struct rnbd_queue *q;

	queue_for_each_hw_ctx(dev->queue, hctx, i) {
		q = &dev->hw_queues[i];
		rnbd_init_hw_queue(dev, q, hctx);
		hctx->driver_data = q;
	}
}

static int rnbd_clt_setup_gen_disk(struct rnbd_clt_dev *dev,
				   struct rnbd_msg_open_rsp *rsp, int idx)
{
	int err;

	dev->gd->major		= rnbd_client_major;
	dev->gd->first_minor	= idx << RNBD_PART_BITS;
	dev->gd->minors		= 1 << RNBD_PART_BITS;
	dev->gd->fops		= &rnbd_client_ops;
	dev->gd->queue		= dev->queue;
	dev->gd->private_data	= dev;
	snprintf(dev->gd->disk_name, sizeof(dev->gd->disk_name), "rnbd%d",
		 idx);
	pr_debug("disk_name=%s, capacity=%llu\n",
		 dev->gd->disk_name,
		 le64_to_cpu(rsp->nsectors) *
		 (le16_to_cpu(rsp->logical_block_size) / SECTOR_SIZE));

	set_capacity(dev->gd, le64_to_cpu(rsp->nsectors));

	if (dev->access_mode == RNBD_ACCESS_RO)
		set_disk_ro(dev->gd, true);

	err = add_disk(dev->gd);
	if (err)
		put_disk(dev->gd);

	return err;
}

static int rnbd_client_setup_device(struct rnbd_clt_dev *dev,
				    struct rnbd_msg_open_rsp *rsp)
{
	struct queue_limits lim = {
		.logical_block_size	= le16_to_cpu(rsp->logical_block_size),
		.physical_block_size	= le16_to_cpu(rsp->physical_block_size),
		.io_opt			= dev->sess->max_io_size,
		.max_hw_sectors		= dev->sess->max_io_size / SECTOR_SIZE,
		.max_hw_discard_sectors	= le32_to_cpu(rsp->max_discard_sectors),
		.discard_granularity	= le32_to_cpu(rsp->discard_granularity),
		.discard_alignment	= le32_to_cpu(rsp->discard_alignment),
		.max_segments		= dev->sess->max_segments,
		.virt_boundary_mask	= SZ_4K - 1,
		.max_write_zeroes_sectors =
			le32_to_cpu(rsp->max_write_zeroes_sectors),
	};
	int idx = dev->clt_device_id;

	dev->size = le64_to_cpu(rsp->nsectors) *
			le16_to_cpu(rsp->logical_block_size);

	if (rsp->secure_discard) {
		lim.max_secure_erase_sectors =
			le32_to_cpu(rsp->max_discard_sectors);
	}

	if (rsp->cache_policy & RNBD_WRITEBACK) {
		lim.features |= BLK_FEAT_WRITE_CACHE;
		if (rsp->cache_policy & RNBD_FUA)
			lim.features |= BLK_FEAT_FUA;
	}

	dev->gd = blk_mq_alloc_disk(&dev->sess->tag_set, &lim, dev);
	if (IS_ERR(dev->gd))
		return PTR_ERR(dev->gd);
	dev->queue = dev->gd->queue;
	rnbd_init_mq_hw_queues(dev);

	return rnbd_clt_setup_gen_disk(dev, rsp, idx);
}

static struct rnbd_clt_dev *init_dev(struct rnbd_clt_session *sess,
				      enum rnbd_access_mode access_mode,
				      const char *pathname,
				      u32 nr_poll_queues)
{
	struct rnbd_clt_dev *dev;
	int ret;

	dev = kzalloc_node(sizeof(*dev), GFP_KERNEL, NUMA_NO_NODE);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	/*
	 * nr_cpu_ids: the number of softirq queues
	 * nr_poll_queues: the number of polling queues
	 */
	dev->hw_queues = kcalloc(nr_cpu_ids + nr_poll_queues,
				 sizeof(*dev->hw_queues),
				 GFP_KERNEL);
	if (!dev->hw_queues) {
		ret = -ENOMEM;
		goto out_alloc;
	}

	ret = ida_alloc_max(&index_ida, (1 << (MINORBITS - RNBD_PART_BITS)) - 1,
			    GFP_KERNEL);
	if (ret < 0) {
		pr_err("Failed to initialize device '%s' from session %s, allocating idr failed, err: %d\n",
		       pathname, sess->sessname, ret);
		goto out_queues;
	}

	dev->pathname = kstrdup(pathname, GFP_KERNEL);
	if (!dev->pathname) {
		ret = -ENOMEM;
		goto out_queues;
	}

	dev->clt_device_id	= ret;
	dev->sess		= sess;
	dev->access_mode	= access_mode;
	dev->nr_poll_queues	= nr_poll_queues;
	mutex_init(&dev->lock);
	refcount_set(&dev->refcount, 1);
	dev->dev_state = DEV_STATE_INIT;

	/*
	 * Here we called from sysfs entry, thus clt-sysfs is
	 * responsible that session will not disappear.
	 */
	WARN_ON(!rnbd_clt_get_sess(sess));

	return dev;

out_queues:
	kfree(dev->hw_queues);
out_alloc:
	kfree(dev);
	return ERR_PTR(ret);
}

static bool __exists_dev(const char *pathname, const char *sessname)
{
	struct rnbd_clt_session *sess;
	struct rnbd_clt_dev *dev;
	bool found = false;

	list_for_each_entry(sess, &sess_list, list) {
		if (sessname && strncmp(sess->sessname, sessname,
					sizeof(sess->sessname)))
			continue;
		mutex_lock(&sess->lock);
		list_for_each_entry(dev, &sess->devs_list, list) {
			if (strlen(dev->pathname) == strlen(pathname) &&
			    !strcmp(dev->pathname, pathname)) {
				found = true;
				break;
			}
		}
		mutex_unlock(&sess->lock);
		if (found)
			break;
	}

	return found;
}

static bool exists_devpath(const char *pathname, const char *sessname)
{
	bool found;

	mutex_lock(&sess_lock);
	found = __exists_dev(pathname, sessname);
	mutex_unlock(&sess_lock);

	return found;
}

static bool insert_dev_if_not_exists_devpath(struct rnbd_clt_dev *dev)
{
	bool found;
	struct rnbd_clt_session *sess = dev->sess;

	mutex_lock(&sess_lock);
	found = __exists_dev(dev->pathname, sess->sessname);
	if (!found) {
		mutex_lock(&sess->lock);
		list_add_tail(&dev->list, &sess->devs_list);
		mutex_unlock(&sess->lock);
	}
	mutex_unlock(&sess_lock);

	return found;
}

static void delete_dev(struct rnbd_clt_dev *dev)
{
	struct rnbd_clt_session *sess = dev->sess;

	mutex_lock(&sess->lock);
	list_del(&dev->list);
	mutex_unlock(&sess->lock);
}

struct rnbd_clt_dev *rnbd_clt_map_device(const char *sessname,
					   struct rtrs_addr *paths,
					   size_t path_cnt, u16 port_nr,
					   const char *pathname,
					   enum rnbd_access_mode access_mode,
					   u32 nr_poll_queues)
{
	struct rnbd_clt_session *sess;
	struct rnbd_clt_dev *dev;
	int ret, errno;
	struct rnbd_msg_open_rsp *rsp;
	struct rnbd_msg_open msg;
	struct rnbd_iu *iu;
	struct kvec vec = {
		.iov_base = &msg,
		.iov_len  = sizeof(msg)
	};

	if (exists_devpath(pathname, sessname))
		return ERR_PTR(-EEXIST);

	sess = find_and_get_or_create_sess(sessname, paths, path_cnt, port_nr, nr_poll_queues);
	if (IS_ERR(sess))
		return ERR_CAST(sess);

	dev = init_dev(sess, access_mode, pathname, nr_poll_queues);
	if (IS_ERR(dev)) {
		pr_err("map_device: failed to map device '%s' from session %s, can't initialize device, err: %pe\n",
		       pathname, sess->sessname, dev);
		ret = PTR_ERR(dev);
		goto put_sess;
	}
	if (insert_dev_if_not_exists_devpath(dev)) {
		ret = -EEXIST;
		goto put_dev;
	}

	rsp = kzalloc(sizeof(*rsp), GFP_KERNEL);
	if (!rsp) {
		ret = -ENOMEM;
		goto del_dev;
	}

	iu = rnbd_get_iu(sess, RTRS_ADMIN_CON, RTRS_PERMIT_WAIT);
	if (!iu) {
		ret = -ENOMEM;
		kfree(rsp);
		goto del_dev;
	}
	iu->buf = rsp;
	iu->dev = dev;
	sg_init_one(iu->sgt.sgl, rsp, sizeof(*rsp));

	msg.hdr.type    = cpu_to_le16(RNBD_MSG_OPEN);
	msg.access_mode = dev->access_mode;
	strscpy(msg.dev_name, dev->pathname, sizeof(msg.dev_name));

	WARN_ON(!rnbd_clt_get_dev(dev));
	ret = send_usr_msg(sess->rtrs, READ, iu,
			   &vec, sizeof(*rsp), iu->sgt.sgl, 1,
			   msg_open_conf, &errno, RTRS_PERMIT_WAIT);
	if (ret) {
		rnbd_clt_put_dev(dev);
		rnbd_put_iu(sess, iu);
	} else {
		ret = errno;
	}
	if (ret) {
		rnbd_clt_err(dev,
			      "map_device: failed, can't open remote device, err: %d\n",
			      ret);
		goto put_iu;
	}
	mutex_lock(&dev->lock);
	pr_debug("Opened remote device: session=%s, path='%s'\n",
		 sess->sessname, pathname);
	ret = rnbd_client_setup_device(dev, rsp);
	if (ret) {
		rnbd_clt_err(dev,
			      "map_device: Failed to configure device, err: %d\n",
			      ret);
		mutex_unlock(&dev->lock);
		goto send_close;
	}

	rnbd_clt_info(dev,
		       "map_device: Device mapped as %s (nsectors: %llu, logical_block_size: %d, physical_block_size: %d, max_write_zeroes_sectors: %d, max_discard_sectors: %d, discard_granularity: %d, discard_alignment: %d, secure_discard: %d, max_segments: %d, max_hw_sectors: %d, wc: %d, fua: %d)\n",
		       dev->gd->disk_name, le64_to_cpu(rsp->nsectors),
		       le16_to_cpu(rsp->logical_block_size),
		       le16_to_cpu(rsp->physical_block_size),
		       le32_to_cpu(rsp->max_write_zeroes_sectors),
		       le32_to_cpu(rsp->max_discard_sectors),
		       le32_to_cpu(rsp->discard_granularity),
		       le32_to_cpu(rsp->discard_alignment),
		       le16_to_cpu(rsp->secure_discard),
		       sess->max_segments, sess->max_io_size / SECTOR_SIZE,
		       !!(rsp->cache_policy & RNBD_WRITEBACK),
		       !!(rsp->cache_policy & RNBD_FUA));

	mutex_unlock(&dev->lock);
	kfree(rsp);
	rnbd_put_iu(sess, iu);
	rnbd_clt_put_sess(sess);

	return dev;

send_close:
	send_msg_close(dev, dev->device_id, RTRS_PERMIT_WAIT);
put_iu:
	kfree(rsp);
	rnbd_put_iu(sess, iu);
del_dev:
	delete_dev(dev);
put_dev:
	rnbd_clt_put_dev(dev);
put_sess:
	rnbd_clt_put_sess(sess);

	return ERR_PTR(ret);
}

static void destroy_gen_disk(struct rnbd_clt_dev *dev)
{
	del_gendisk(dev->gd);
	put_disk(dev->gd);
}

static void destroy_sysfs(struct rnbd_clt_dev *dev,
			  const struct attribute *sysfs_self)
{
	rnbd_clt_remove_dev_symlink(dev);
	if (dev->kobj.state_initialized) {
		if (sysfs_self)
			/* To avoid deadlock firstly remove itself */
			sysfs_remove_file_self(&dev->kobj, sysfs_self);
		kobject_del(&dev->kobj);
		kobject_put(&dev->kobj);
	}
}

int rnbd_clt_unmap_device(struct rnbd_clt_dev *dev, bool force,
			   const struct attribute *sysfs_self)
{
	struct rnbd_clt_session *sess = dev->sess;
	int refcount, ret = 0;
	bool was_mapped;

	mutex_lock(&dev->lock);
	if (dev->dev_state == DEV_STATE_UNMAPPED) {
		rnbd_clt_info(dev, "Device is already being unmapped\n");
		ret = -EALREADY;
		goto err;
	}
	refcount = refcount_read(&dev->refcount);
	if (!force && refcount > 1) {
		rnbd_clt_err(dev,
			      "Closing device failed, device is in use, (%d device users)\n",
			      refcount - 1);
		ret = -EBUSY;
		goto err;
	}
	was_mapped = (dev->dev_state == DEV_STATE_MAPPED);
	dev->dev_state = DEV_STATE_UNMAPPED;
	mutex_unlock(&dev->lock);

	delete_dev(dev);
	destroy_sysfs(dev, sysfs_self);
	destroy_gen_disk(dev);
	if (was_mapped && sess->rtrs)
		send_msg_close(dev, dev->device_id, RTRS_PERMIT_WAIT);

	rnbd_clt_info(dev, "Device is unmapped\n");

	/* Likely last reference put */
	rnbd_clt_put_dev(dev);

	/*
	 * Here device and session can be vanished!
	 */

	return 0;
err:
	mutex_unlock(&dev->lock);

	return ret;
}

int rnbd_clt_remap_device(struct rnbd_clt_dev *dev)
{
	int err;

	mutex_lock(&dev->lock);
	if (dev->dev_state == DEV_STATE_MAPPED_DISCONNECTED)
		err = 0;
	else if (dev->dev_state == DEV_STATE_UNMAPPED)
		err = -ENODEV;
	else if (dev->dev_state == DEV_STATE_MAPPED)
		err = -EALREADY;
	else
		err = -EBUSY;
	mutex_unlock(&dev->lock);
	if (!err) {
		rnbd_clt_info(dev, "Remapping device.\n");
		err = send_msg_open(dev, RTRS_PERMIT_WAIT);
		if (err)
			rnbd_clt_err(dev, "remap_device: %d\n", err);
	}

	return err;
}

static void unmap_device_work(struct work_struct *work)
{
	struct rnbd_clt_dev *dev;

	dev = container_of(work, typeof(*dev), unmap_on_rmmod_work);
	rnbd_clt_unmap_device(dev, true, NULL);
}

static void rnbd_destroy_sessions(void)
{
	struct rnbd_clt_session *sess, *sn;
	struct rnbd_clt_dev *dev, *tn;

	/* Firstly forbid access through sysfs interface */
	rnbd_clt_destroy_sysfs_files();

	/*
	 * Here at this point there is no any concurrent access to sessions
	 * list and devices list:
	 *   1. New session or device can't be created - session sysfs files
	 *      are removed.
	 *   2. Device or session can't be removed - module reference is taken
	 *      into account in unmap device sysfs callback.
	 *   3. No IO requests inflight - each file open of block_dev increases
	 *      module reference in get_disk().
	 *
	 * But still there can be user requests inflights, which are sent by
	 * asynchronous send_msg_*() functions, thus before unmapping devices
	 * RTRS session must be explicitly closed.
	 */

	list_for_each_entry_safe(sess, sn, &sess_list, list) {
		if (!rnbd_clt_get_sess(sess))
			continue;
		close_rtrs(sess);
		list_for_each_entry_safe(dev, tn, &sess->devs_list, list) {
			/*
			 * Here unmap happens in parallel for only one reason:
			 * del_gendisk() takes around half a second, so
			 * on huge amount of devices the whole module unload
			 * procedure takes minutes.
			 */
			INIT_WORK(&dev->unmap_on_rmmod_work, unmap_device_work);
			queue_work(rnbd_clt_wq, &dev->unmap_on_rmmod_work);
		}
		rnbd_clt_put_sess(sess);
	}
	/* Wait for all scheduled unmap works */
	flush_workqueue(rnbd_clt_wq);
	WARN_ON(!list_empty(&sess_list));
}

static int __init rnbd_client_init(void)
{
	int err = 0;

	BUILD_BUG_ON(sizeof(struct rnbd_msg_hdr) != 4);
	BUILD_BUG_ON(sizeof(struct rnbd_msg_sess_info) != 36);
	BUILD_BUG_ON(sizeof(struct rnbd_msg_sess_info_rsp) != 36);
	BUILD_BUG_ON(sizeof(struct rnbd_msg_open) != 264);
	BUILD_BUG_ON(sizeof(struct rnbd_msg_close) != 8);
	BUILD_BUG_ON(sizeof(struct rnbd_msg_open_rsp) != 56);
	rnbd_client_major = register_blkdev(rnbd_client_major, "rnbd");
	if (rnbd_client_major <= 0) {
		pr_err("Failed to load module, block device registration failed\n");
		return -EBUSY;
	}

	err = rnbd_clt_create_sysfs_files();
	if (err) {
		pr_err("Failed to load module, creating sysfs device files failed, err: %d\n",
		       err);
		unregister_blkdev(rnbd_client_major, "rnbd");
		return err;
	}
	rnbd_clt_wq = alloc_workqueue("rnbd_clt_wq", 0, 0);
	if (!rnbd_clt_wq) {
		pr_err("Failed to load module, alloc_workqueue failed.\n");
		rnbd_clt_destroy_sysfs_files();
		unregister_blkdev(rnbd_client_major, "rnbd");
		err = -ENOMEM;
	}

	return err;
}

static void __exit rnbd_client_exit(void)
{
	rnbd_destroy_sessions();
	unregister_blkdev(rnbd_client_major, "rnbd");
	ida_destroy(&index_ida);
	destroy_workqueue(rnbd_clt_wq);
}

module_init(rnbd_client_init);
module_exit(rnbd_client_exit);

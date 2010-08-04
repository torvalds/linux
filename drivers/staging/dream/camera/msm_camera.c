/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */

/* FIXME: most allocations need not be GFP_ATOMIC */
/* FIXME: management of mutexes */
/* FIXME: msm_pmem_region_lookup return values */
/* FIXME: way too many copy to/from user */
/* FIXME: does region->active mean free */
/* FIXME: check limits on command lenghts passed from userspace */
/* FIXME: __msm_release: which queues should we flush when opencnt != 0 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <mach/board.h>

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/android_pmem.h>
#include <linux/poll.h>
#include <media/msm_camera.h>
#include <mach/camera.h>

#define MSM_MAX_CAMERA_SENSORS 5

#define ERR_USER_COPY(to) pr_err("%s(%d): copy %s user\n", \
				__func__, __LINE__, ((to) ? "to" : "from"))
#define ERR_COPY_FROM_USER() ERR_USER_COPY(0)
#define ERR_COPY_TO_USER() ERR_USER_COPY(1)

static struct class *msm_class;
static dev_t msm_devno;
static LIST_HEAD(msm_sensors);

#define __CONTAINS(r, v, l, field) ({				\
	typeof(r) __r = r;					\
	typeof(v) __v = v;					\
	typeof(v) __e = __v + l;				\
	int res = __v >= __r->field &&				\
		__e <= __r->field + __r->len;			\
	res;							\
})

#define CONTAINS(r1, r2, field) ({				\
	typeof(r2) __r2 = r2;					\
	__CONTAINS(r1, __r2->field, __r2->len, field);		\
})

#define IN_RANGE(r, v, field) ({				\
	typeof(r) __r = r;					\
	typeof(v) __vv = v;					\
	int res = ((__vv >= __r->field) &&			\
		(__vv < (__r->field + __r->len)));		\
	res;							\
})

#define OVERLAPS(r1, r2, field) ({				\
	typeof(r1) __r1 = r1;					\
	typeof(r2) __r2 = r2;					\
	typeof(__r2->field) __v = __r2->field;			\
	typeof(__v) __e = __v + __r2->len - 1;			\
	int res = (IN_RANGE(__r1, __v, field) ||		\
		   IN_RANGE(__r1, __e, field));                 \
	res;							\
})

#define MSM_DRAIN_QUEUE_NOSYNC(sync, name) do {			\
	struct msm_queue_cmd *qcmd = NULL;			\
	CDBG("%s: draining queue "#name"\n", __func__);		\
	while (!list_empty(&(sync)->name)) {			\
		qcmd = list_first_entry(&(sync)->name,		\
			struct msm_queue_cmd, list);		\
		list_del_init(&qcmd->list);			\
		kfree(qcmd);					\
	};							\
} while (0)

#define MSM_DRAIN_QUEUE(sync, name) do {			\
	unsigned long flags;					\
	spin_lock_irqsave(&(sync)->name##_lock, flags);		\
	MSM_DRAIN_QUEUE_NOSYNC(sync, name);			\
	spin_unlock_irqrestore(&(sync)->name##_lock, flags);	\
} while (0)

static int check_overlap(struct hlist_head *ptype,
			unsigned long paddr,
			unsigned long len)
{
	struct msm_pmem_region *region;
	struct msm_pmem_region t = { .paddr = paddr, .len = len };
	struct hlist_node *node;

	hlist_for_each_entry(region, node, ptype, list) {
		if (CONTAINS(region, &t, paddr) ||
				CONTAINS(&t, region, paddr) ||
				OVERLAPS(region, &t, paddr)) {
			printk(KERN_ERR
				" region (PHYS %p len %ld)"
				" clashes with registered region"
				" (paddr %p len %ld)\n",
				(void *)t.paddr, t.len,
				(void *)region->paddr, region->len);
			return -1;
		}
	}

	return 0;
}

static int msm_pmem_table_add(struct hlist_head *ptype,
	struct msm_pmem_info *info)
{
	struct file *file;
	unsigned long paddr;
	unsigned long vstart;
	unsigned long len;
	int rc;
	struct msm_pmem_region *region;

	rc = get_pmem_file(info->fd, &paddr, &vstart, &len, &file);
	if (rc < 0) {
		pr_err("msm_pmem_table_add: get_pmem_file fd %d error %d\n",
			info->fd, rc);
		return rc;
	}

	if (check_overlap(ptype, paddr, len) < 0)
		return -EINVAL;

	CDBG("%s: type = %d, paddr = 0x%lx, vaddr = 0x%lx\n",
		__func__,
		info->type, paddr, (unsigned long)info->vaddr);

	region = kmalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	INIT_HLIST_NODE(&region->list);

	region->type = info->type;
	region->vaddr = info->vaddr;
	region->paddr = paddr;
	region->len = len;
	region->file = file;
	region->y_off = info->y_off;
	region->cbcr_off = info->cbcr_off;
	region->fd = info->fd;
	region->active = info->active;

	hlist_add_head(&(region->list), ptype);

	return 0;
}

/* return of 0 means failure */
static uint8_t msm_pmem_region_lookup(struct hlist_head *ptype,
	int pmem_type, struct msm_pmem_region *reg, uint8_t maxcount)
{
	struct msm_pmem_region *region;
	struct msm_pmem_region *regptr;
	struct hlist_node *node, *n;

	uint8_t rc = 0;

	regptr = reg;

	hlist_for_each_entry_safe(region, node, n, ptype, list) {
		if (region->type == pmem_type && region->active) {
			*regptr = *region;
			rc += 1;
			if (rc >= maxcount)
				break;
			regptr++;
		}
	}

	return rc;
}

static unsigned long msm_pmem_frame_ptov_lookup(struct msm_sync *sync,
		unsigned long pyaddr,
		unsigned long pcbcraddr,
		uint32_t *yoff, uint32_t *cbcroff, int *fd)
{
	struct msm_pmem_region *region;
	struct hlist_node *node, *n;

	hlist_for_each_entry_safe(region, node, n, &sync->frame, list) {
		if (pyaddr == (region->paddr + region->y_off) &&
				pcbcraddr == (region->paddr +
						region->cbcr_off) &&
				region->active) {
			/* offset since we could pass vaddr inside
			 * a registerd pmem buffer
			 */
			*yoff = region->y_off;
			*cbcroff = region->cbcr_off;
			*fd = region->fd;
			region->active = 0;
			return (unsigned long)(region->vaddr);
		}
	}

	return 0;
}

static unsigned long msm_pmem_stats_ptov_lookup(struct msm_sync *sync,
		unsigned long addr, int *fd)
{
	struct msm_pmem_region *region;
	struct hlist_node *node, *n;

	hlist_for_each_entry_safe(region, node, n, &sync->stats, list) {
		if (addr == region->paddr && region->active) {
			/* offset since we could pass vaddr inside a
			 * registered pmem buffer */
			*fd = region->fd;
			region->active = 0;
			return (unsigned long)(region->vaddr);
		}
	}

	return 0;
}

static unsigned long msm_pmem_frame_vtop_lookup(struct msm_sync *sync,
		unsigned long buffer,
		uint32_t yoff, uint32_t cbcroff, int fd)
{
	struct msm_pmem_region *region;
	struct hlist_node *node, *n;

	hlist_for_each_entry_safe(region,
		node, n, &sync->frame, list) {
		if (((unsigned long)(region->vaddr) == buffer) &&
				(region->y_off == yoff) &&
				(region->cbcr_off == cbcroff) &&
				(region->fd == fd) &&
				(region->active == 0)) {

			region->active = 1;
			return region->paddr;
		}
	}

	return 0;
}

static unsigned long msm_pmem_stats_vtop_lookup(
		struct msm_sync *sync,
		unsigned long buffer,
		int fd)
{
	struct msm_pmem_region *region;
	struct hlist_node *node, *n;

	hlist_for_each_entry_safe(region, node, n, &sync->stats, list) {
		if (((unsigned long)(region->vaddr) == buffer) &&
				(region->fd == fd) && region->active == 0) {
			region->active = 1;
			return region->paddr;
		}
	}

	return 0;
}

static int __msm_pmem_table_del(struct msm_sync *sync,
		struct msm_pmem_info *pinfo)
{
	int rc = 0;
	struct msm_pmem_region *region;
	struct hlist_node *node, *n;

	switch (pinfo->type) {
	case MSM_PMEM_OUTPUT1:
	case MSM_PMEM_OUTPUT2:
	case MSM_PMEM_THUMBAIL:
	case MSM_PMEM_MAINIMG:
	case MSM_PMEM_RAW_MAINIMG:
		hlist_for_each_entry_safe(region, node, n,
			&sync->frame, list) {

			if (pinfo->type == region->type &&
					pinfo->vaddr == region->vaddr &&
					pinfo->fd == region->fd) {
				hlist_del(node);
				put_pmem_file(region->file);
				kfree(region);
			}
		}
		break;

	case MSM_PMEM_AEC_AWB:
	case MSM_PMEM_AF:
		hlist_for_each_entry_safe(region, node, n,
			&sync->stats, list) {

			if (pinfo->type == region->type &&
					pinfo->vaddr == region->vaddr &&
					pinfo->fd == region->fd) {
				hlist_del(node);
				put_pmem_file(region->file);
				kfree(region);
			}
		}
		break;

	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int msm_pmem_table_del(struct msm_sync *sync, void __user *arg)
{
	struct msm_pmem_info info;

	if (copy_from_user(&info, arg, sizeof(info))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	return __msm_pmem_table_del(sync, &info);
}

static int __msm_get_frame(struct msm_sync *sync,
		struct msm_frame *frame)
{
	unsigned long flags;
	int rc = 0;

	struct msm_queue_cmd *qcmd = NULL;
	struct msm_vfe_phy_info *pphy;

	spin_lock_irqsave(&sync->prev_frame_q_lock, flags);
	if (!list_empty(&sync->prev_frame_q)) {
		qcmd = list_first_entry(&sync->prev_frame_q,
			struct msm_queue_cmd, list);
		list_del_init(&qcmd->list);
	}
	spin_unlock_irqrestore(&sync->prev_frame_q_lock, flags);

	if (!qcmd) {
		pr_err("%s: no preview frame.\n", __func__);
		return -EAGAIN;
	}

	pphy = (struct msm_vfe_phy_info *)(qcmd->command);

	frame->buffer =
		msm_pmem_frame_ptov_lookup(sync,
			pphy->y_phy,
			pphy->cbcr_phy, &(frame->y_off),
			&(frame->cbcr_off), &(frame->fd));
	if (!frame->buffer) {
		pr_err("%s: cannot get frame, invalid lookup address "
			"y=%x cbcr=%x offset=%d\n",
			__func__,
			pphy->y_phy,
			pphy->cbcr_phy,
			frame->y_off);
		rc = -EINVAL;
	}

	CDBG("__msm_get_frame: y=0x%x, cbcr=0x%x, qcmd=0x%x, virt_addr=0x%x\n",
		pphy->y_phy, pphy->cbcr_phy, (int) qcmd, (int) frame->buffer);

	kfree(qcmd);
	return rc;
}

static int msm_get_frame(struct msm_sync *sync, void __user *arg)
{
	int rc = 0;
	struct msm_frame frame;

	if (copy_from_user(&frame,
				arg,
				sizeof(struct msm_frame))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	rc = __msm_get_frame(sync, &frame);
	if (rc < 0)
		return rc;

	if (sync->croplen) {
		if (frame.croplen > sync->croplen) {
			pr_err("msm_get_frame: invalid frame croplen %d\n",
				frame.croplen);
			return -EINVAL;
		}

		if (copy_to_user((void *)frame.cropinfo,
				sync->cropinfo,
				sync->croplen)) {
			ERR_COPY_TO_USER();
			return -EFAULT;
		}
	}

	if (copy_to_user((void *)arg,
				&frame, sizeof(struct msm_frame))) {
		ERR_COPY_TO_USER();
		rc = -EFAULT;
	}

	CDBG("Got frame!!!\n");

	return rc;
}

static int msm_enable_vfe(struct msm_sync *sync, void __user *arg)
{
	int rc = -EIO;
	struct camera_enable_cmd cfg;

	if (copy_from_user(&cfg,
			arg,
			sizeof(struct camera_enable_cmd))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	if (sync->vfefn.vfe_enable)
		rc = sync->vfefn.vfe_enable(&cfg);

	CDBG("msm_enable_vfe: returned rc = %d\n", rc);
	return rc;
}

static int msm_disable_vfe(struct msm_sync *sync, void __user *arg)
{
	int rc = -EIO;
	struct camera_enable_cmd cfg;

	if (copy_from_user(&cfg,
			arg,
			sizeof(struct camera_enable_cmd))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	if (sync->vfefn.vfe_disable)
		rc = sync->vfefn.vfe_disable(&cfg, NULL);

	CDBG("msm_disable_vfe: returned rc = %d\n", rc);
	return rc;
}

static struct msm_queue_cmd *__msm_control(struct msm_sync *sync,
		struct msm_control_device_queue *queue,
		struct msm_queue_cmd *qcmd,
		int timeout)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&sync->msg_event_q_lock, flags);
	list_add_tail(&qcmd->list, &sync->msg_event_q);
	/* wake up config thread */
	wake_up(&sync->msg_event_wait);
	spin_unlock_irqrestore(&sync->msg_event_q_lock, flags);

	if (!queue)
		return NULL;

	/* wait for config status */
	rc = wait_event_interruptible_timeout(
			queue->ctrl_status_wait,
			!list_empty_careful(&queue->ctrl_status_q),
			timeout);
	if (list_empty_careful(&queue->ctrl_status_q)) {
		if (!rc)
			rc = -ETIMEDOUT;
		if (rc < 0) {
			pr_err("msm_control: wait_event error %d\n", rc);
#if 0
			/* This is a bit scary.  If we time out too early, we
			 * will free qcmd at the end of this function, and the
			 * dsp may do the same when it does respond, so we
			 * remove the message from the source queue.
			 */
			pr_err("%s: error waiting for ctrl_status_q: %d\n",
				__func__, rc);
			spin_lock_irqsave(&sync->msg_event_q_lock, flags);
			list_del_init(&qcmd->list);
			spin_unlock_irqrestore(&sync->msg_event_q_lock, flags);
#endif
			return ERR_PTR(rc);
		}
	}

	/* control command status is ready */
	spin_lock_irqsave(&queue->ctrl_status_q_lock, flags);
	BUG_ON(list_empty(&queue->ctrl_status_q));
	qcmd = list_first_entry(&queue->ctrl_status_q,
			struct msm_queue_cmd, list);
	list_del_init(&qcmd->list);
	spin_unlock_irqrestore(&queue->ctrl_status_q_lock, flags);

	return qcmd;
}

static int msm_control(struct msm_control_device *ctrl_pmsm,
			int block,
			void __user *arg)
{
	int rc = 0;

	struct msm_sync *sync = ctrl_pmsm->pmsm->sync;
	struct msm_ctrl_cmd udata, *ctrlcmd;
	struct msm_queue_cmd *qcmd = NULL, *qcmd_temp;

	if (copy_from_user(&udata, arg, sizeof(struct msm_ctrl_cmd))) {
		ERR_COPY_FROM_USER();
		rc = -EFAULT;
		goto end;
	}

	qcmd = kmalloc(sizeof(struct msm_queue_cmd) +
				sizeof(struct msm_ctrl_cmd) + udata.length,
				GFP_KERNEL);
	if (!qcmd) {
		pr_err("msm_control: cannot allocate buffer\n");
		rc = -ENOMEM;
		goto end;
	}

	qcmd->type = MSM_CAM_Q_CTRL;
	qcmd->command = ctrlcmd = (struct msm_ctrl_cmd *)(qcmd + 1);
	*ctrlcmd = udata;
	ctrlcmd->value = ctrlcmd + 1;

	if (udata.length) {
		if (copy_from_user(ctrlcmd->value,
				udata.value, udata.length)) {
			ERR_COPY_FROM_USER();
			rc = -EFAULT;
			goto end;
		}
	}

	if (!block) {
		/* qcmd will be set to NULL */
		qcmd = __msm_control(sync, NULL, qcmd, 0);
		goto end;
	}

	qcmd_temp = __msm_control(sync,
				  &ctrl_pmsm->ctrl_q,
				  qcmd, MAX_SCHEDULE_TIMEOUT);

	if (IS_ERR(qcmd_temp)) {
		rc = PTR_ERR(qcmd_temp);
		goto end;
	}
	qcmd = qcmd_temp;

	if (qcmd->command) {
		void __user *to = udata.value;
		udata = *(struct msm_ctrl_cmd *)qcmd->command;
		if (udata.length > 0) {
			if (copy_to_user(to,
					 udata.value,
					 udata.length)) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
				goto end;
			}
		}
		udata.value = to;

		if (copy_to_user((void *)arg, &udata,
				sizeof(struct msm_ctrl_cmd))) {
			ERR_COPY_TO_USER();
			rc = -EFAULT;
			goto end;
		}
	}

end:
	/* Note: if we get here as a result of an error, we will free the
	 * qcmd that we kmalloc() in this function.  When we come here as
	 * a result of a successful completion, we are freeing the qcmd that
	 * we dequeued from queue->ctrl_status_q.
	 */
	kfree(qcmd);

	CDBG("msm_control: end rc = %d\n", rc);
	return rc;
}

static int msm_get_stats(struct msm_sync *sync, void __user *arg)
{
	unsigned long flags;
	int timeout;
	int rc = 0;

	struct msm_stats_event_ctrl se;

	struct msm_queue_cmd *qcmd = NULL;
	struct msm_ctrl_cmd  *ctrl = NULL;
	struct msm_vfe_resp  *data = NULL;
	struct msm_stats_buf stats;

	if (copy_from_user(&se, arg,
			sizeof(struct msm_stats_event_ctrl))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	timeout = (int)se.timeout_ms;

	CDBG("msm_get_stats timeout %d\n", timeout);
	rc = wait_event_interruptible_timeout(
			sync->msg_event_wait,
			!list_empty_careful(&sync->msg_event_q),
			msecs_to_jiffies(timeout));
	if (list_empty_careful(&sync->msg_event_q)) {
		if (rc == 0)
			rc = -ETIMEDOUT;
		if (rc < 0) {
			pr_err("msm_get_stats error %d\n", rc);
			return rc;
		}
	}
	CDBG("msm_get_stats returned from wait: %d\n", rc);

	spin_lock_irqsave(&sync->msg_event_q_lock, flags);
	BUG_ON(list_empty(&sync->msg_event_q));
	qcmd = list_first_entry(&sync->msg_event_q,
			struct msm_queue_cmd, list);
	list_del_init(&qcmd->list);
	spin_unlock_irqrestore(&sync->msg_event_q_lock, flags);

	CDBG("=== received from DSP === %d\n", qcmd->type);

	switch (qcmd->type) {
	case MSM_CAM_Q_VFE_EVT:
	case MSM_CAM_Q_VFE_MSG:
		data = (struct msm_vfe_resp *)(qcmd->command);

		/* adsp event and message */
		se.resptype = MSM_CAM_RESP_STAT_EVT_MSG;

		/* 0 - msg from aDSP, 1 - event from mARM */
		se.stats_event.type   = data->evt_msg.type;
		se.stats_event.msg_id = data->evt_msg.msg_id;
		se.stats_event.len    = data->evt_msg.len;

		CDBG("msm_get_stats, qcmd->type = %d\n", qcmd->type);
		CDBG("length = %d\n", se.stats_event.len);
		CDBG("msg_id = %d\n", se.stats_event.msg_id);

		if ((data->type == VFE_MSG_STATS_AF) ||
				(data->type == VFE_MSG_STATS_WE)) {

			stats.buffer =
			msm_pmem_stats_ptov_lookup(sync,
					data->phy.sbuf_phy,
					&(stats.fd));
			if (!stats.buffer) {
				pr_err("%s: msm_pmem_stats_ptov_lookup error\n",
					__func__);
				rc = -EINVAL;
				goto failure;
			}

			if (copy_to_user((void *)(se.stats_event.data),
					&stats,
					sizeof(struct msm_stats_buf))) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
				goto failure;
			}
		} else if ((data->evt_msg.len > 0) &&
				(data->type == VFE_MSG_GENERAL)) {
			if (copy_to_user((void *)(se.stats_event.data),
					data->evt_msg.data,
					data->evt_msg.len)) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
			}
		} else if (data->type == VFE_MSG_OUTPUT1 ||
			data->type == VFE_MSG_OUTPUT2) {
			if (copy_to_user((void *)(se.stats_event.data),
					data->extdata,
					data->extlen)) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
			}
		} else if (data->type == VFE_MSG_SNAPSHOT && sync->pict_pp) {
			struct msm_postproc buf;
			struct msm_pmem_region region;
			buf.fmnum = msm_pmem_region_lookup(&sync->frame,
					MSM_PMEM_MAINIMG,
					&region, 1);
			if (buf.fmnum == 1) {
				buf.fmain.buffer = (unsigned long)region.vaddr;
				buf.fmain.y_off  = region.y_off;
				buf.fmain.cbcr_off = region.cbcr_off;
				buf.fmain.fd = region.fd;
			} else {
				buf.fmnum = msm_pmem_region_lookup(&sync->frame,
						MSM_PMEM_RAW_MAINIMG,
						&region, 1);
				if (buf.fmnum == 1) {
					buf.fmain.path = MSM_FRAME_PREV_2;
					buf.fmain.buffer =
						(unsigned long)region.vaddr;
					buf.fmain.fd = region.fd;
				} else {
					pr_err("%s: pmem lookup failed\n",
						__func__);
					rc = -EINVAL;
				}
			}

			if (copy_to_user((void *)(se.stats_event.data), &buf,
					sizeof(buf))) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
				goto failure;
			}
			CDBG("snapshot copy_to_user!\n");
		}
		break;

	case MSM_CAM_Q_CTRL:
		/* control command from control thread */
		ctrl = (struct msm_ctrl_cmd *)(qcmd->command);

		CDBG("msm_get_stats, qcmd->type = %d\n", qcmd->type);
		CDBG("length = %d\n", ctrl->length);

		if (ctrl->length > 0) {
			if (copy_to_user((void *)(se.ctrl_cmd.value),
						ctrl->value,
						ctrl->length)) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
				goto failure;
			}
		}

		se.resptype = MSM_CAM_RESP_CTRL;

		/* what to control */
		se.ctrl_cmd.type = ctrl->type;
		se.ctrl_cmd.length = ctrl->length;
		se.ctrl_cmd.resp_fd = ctrl->resp_fd;
		break;

	case MSM_CAM_Q_V4L2_REQ:
		/* control command from v4l2 client */
		ctrl = (struct msm_ctrl_cmd *)(qcmd->command);

		CDBG("msm_get_stats, qcmd->type = %d\n", qcmd->type);
		CDBG("length = %d\n", ctrl->length);

		if (ctrl->length > 0) {
			if (copy_to_user((void *)(se.ctrl_cmd.value),
					ctrl->value, ctrl->length)) {
				ERR_COPY_TO_USER();
				rc = -EFAULT;
				goto failure;
			}
		}

		/* 2 tells config thread this is v4l2 request */
		se.resptype = MSM_CAM_RESP_V4L2;

		/* what to control */
		se.ctrl_cmd.type   = ctrl->type;
		se.ctrl_cmd.length = ctrl->length;
		break;

	default:
		rc = -EFAULT;
		goto failure;
	} /* switch qcmd->type */

	if (copy_to_user((void *)arg, &se, sizeof(se))) {
		ERR_COPY_TO_USER();
		rc = -EFAULT;
	}

failure:
	kfree(qcmd);

	CDBG("msm_get_stats: %d\n", rc);
	return rc;
}

static int msm_ctrl_cmd_done(struct msm_control_device *ctrl_pmsm,
		void __user *arg)
{
	unsigned long flags;
	int rc = 0;

	struct msm_ctrl_cmd udata, *ctrlcmd;
	struct msm_queue_cmd *qcmd = NULL;

	if (copy_from_user(&udata, arg, sizeof(struct msm_ctrl_cmd))) {
		ERR_COPY_FROM_USER();
		rc = -EFAULT;
		goto end;
	}

	qcmd = kmalloc(sizeof(struct msm_queue_cmd) +
			sizeof(struct msm_ctrl_cmd) + udata.length,
			GFP_KERNEL);
	if (!qcmd) {
		rc = -ENOMEM;
		goto end;
	}

	qcmd->command = ctrlcmd = (struct msm_ctrl_cmd *)(qcmd + 1);
	*ctrlcmd = udata;
	if (udata.length > 0) {
		ctrlcmd->value = ctrlcmd + 1;
		if (copy_from_user(ctrlcmd->value,
					(void *)udata.value,
					udata.length)) {
			ERR_COPY_FROM_USER();
			rc = -EFAULT;
			kfree(qcmd);
			goto end;
		}
	} else
		ctrlcmd->value = NULL;

end:
	CDBG("msm_ctrl_cmd_done: end rc = %d\n", rc);
	if (rc == 0) {
		/* wake up control thread */
		spin_lock_irqsave(&ctrl_pmsm->ctrl_q.ctrl_status_q_lock, flags);
		list_add_tail(&qcmd->list, &ctrl_pmsm->ctrl_q.ctrl_status_q);
		wake_up(&ctrl_pmsm->ctrl_q.ctrl_status_wait);
		spin_unlock_irqrestore(&ctrl_pmsm->ctrl_q.ctrl_status_q_lock, flags);
	}

	return rc;
}

static int msm_config_vfe(struct msm_sync *sync, void __user *arg)
{
	struct msm_vfe_cfg_cmd cfgcmd;
	struct msm_pmem_region region[8];
	struct axidata axi_data;
	void *data = NULL;
	int rc = -EIO;

	memset(&axi_data, 0, sizeof(axi_data));

	if (copy_from_user(&cfgcmd, arg, sizeof(cfgcmd))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	switch (cfgcmd.cmd_type) {
	case CMD_STATS_ENABLE:
		axi_data.bufnum1 =
			msm_pmem_region_lookup(&sync->stats,
					MSM_PMEM_AEC_AWB, &region[0],
					NUM_WB_EXP_STAT_OUTPUT_BUFFERS);
		if (!axi_data.bufnum1) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		axi_data.region = &region[0];
		data = &axi_data;
		break;
	case CMD_STATS_AF_ENABLE:
		axi_data.bufnum1 =
			msm_pmem_region_lookup(&sync->stats,
					MSM_PMEM_AF, &region[0],
					NUM_AF_STAT_OUTPUT_BUFFERS);
		if (!axi_data.bufnum1) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		axi_data.region = &region[0];
		data = &axi_data;
		break;
	case CMD_GENERAL:
	case CMD_STATS_DISABLE:
		break;
	default:
		pr_err("%s: unknown command type %d\n",
			__func__, cfgcmd.cmd_type);
		return -EINVAL;
	}


	if (sync->vfefn.vfe_config)
		rc = sync->vfefn.vfe_config(&cfgcmd, data);

	return rc;
}

static int msm_frame_axi_cfg(struct msm_sync *sync,
		struct msm_vfe_cfg_cmd *cfgcmd)
{
	int rc = -EIO;
	struct axidata axi_data;
	void *data = &axi_data;
	struct msm_pmem_region region[8];
	int pmem_type;

	memset(&axi_data, 0, sizeof(axi_data));

	switch (cfgcmd->cmd_type) {
	case CMD_AXI_CFG_OUT1:
		pmem_type = MSM_PMEM_OUTPUT1;
		axi_data.bufnum1 =
			msm_pmem_region_lookup(&sync->frame, pmem_type,
				&region[0], 8);
		if (!axi_data.bufnum1) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		break;

	case CMD_AXI_CFG_OUT2:
		pmem_type = MSM_PMEM_OUTPUT2;
		axi_data.bufnum2 =
			msm_pmem_region_lookup(&sync->frame, pmem_type,
				&region[0], 8);
		if (!axi_data.bufnum2) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		break;

	case CMD_AXI_CFG_SNAP_O1_AND_O2:
		pmem_type = MSM_PMEM_THUMBAIL;
		axi_data.bufnum1 =
			msm_pmem_region_lookup(&sync->frame, pmem_type,
				&region[0], 8);
		if (!axi_data.bufnum1) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}

		pmem_type = MSM_PMEM_MAINIMG;
		axi_data.bufnum2 =
			msm_pmem_region_lookup(&sync->frame, pmem_type,
				&region[axi_data.bufnum1], 8);
		if (!axi_data.bufnum2) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		break;

	case CMD_RAW_PICT_AXI_CFG:
		pmem_type = MSM_PMEM_RAW_MAINIMG;
		axi_data.bufnum2 =
			msm_pmem_region_lookup(&sync->frame, pmem_type,
				&region[0], 8);
		if (!axi_data.bufnum2) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		break;

	case CMD_GENERAL:
		data = NULL;
		break;

	default:
		pr_err("%s: unknown command type %d\n",
			__func__, cfgcmd->cmd_type);
		return -EINVAL;
	}

	axi_data.region = &region[0];

	/* send the AXI configuration command to driver */
	if (sync->vfefn.vfe_config)
		rc = sync->vfefn.vfe_config(cfgcmd, data);

	return rc;
}

static int msm_get_sensor_info(struct msm_sync *sync, void __user *arg)
{
	int rc = 0;
	struct msm_camsensor_info info;
	struct msm_camera_sensor_info *sdata;

	if (copy_from_user(&info,
			arg,
			sizeof(struct msm_camsensor_info))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	sdata = sync->pdev->dev.platform_data;
	CDBG("sensor_name %s\n", sdata->sensor_name);

	memcpy(&info.name[0],
		sdata->sensor_name,
		MAX_SENSOR_NAME);
	info.flash_enabled = sdata->flash_type != MSM_CAMERA_FLASH_NONE;

	/* copy back to user space */
	if (copy_to_user((void *)arg,
			&info,
			sizeof(struct msm_camsensor_info))) {
		ERR_COPY_TO_USER();
		rc = -EFAULT;
	}

	return rc;
}

static int __msm_put_frame_buf(struct msm_sync *sync,
		struct msm_frame *pb)
{
	unsigned long pphy;
	struct msm_vfe_cfg_cmd cfgcmd;

	int rc = -EIO;

	pphy = msm_pmem_frame_vtop_lookup(sync,
		pb->buffer,
		pb->y_off, pb->cbcr_off, pb->fd);

	if (pphy != 0) {
		CDBG("rel: vaddr = 0x%lx, paddr = 0x%lx\n",
			pb->buffer, pphy);
		cfgcmd.cmd_type = CMD_FRAME_BUF_RELEASE;
		cfgcmd.value    = (void *)pb;
		if (sync->vfefn.vfe_config)
			rc = sync->vfefn.vfe_config(&cfgcmd, &pphy);
	} else {
		pr_err("%s: msm_pmem_frame_vtop_lookup failed\n",
			__func__);
		rc = -EINVAL;
	}

	return rc;
}

static int msm_put_frame_buffer(struct msm_sync *sync, void __user *arg)
{
	struct msm_frame buf_t;

	if (copy_from_user(&buf_t,
				arg,
				sizeof(struct msm_frame))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	return __msm_put_frame_buf(sync, &buf_t);
}

static int __msm_register_pmem(struct msm_sync *sync,
		struct msm_pmem_info *pinfo)
{
	int rc = 0;

	switch (pinfo->type) {
	case MSM_PMEM_OUTPUT1:
	case MSM_PMEM_OUTPUT2:
	case MSM_PMEM_THUMBAIL:
	case MSM_PMEM_MAINIMG:
	case MSM_PMEM_RAW_MAINIMG:
		rc = msm_pmem_table_add(&sync->frame, pinfo);
		break;

	case MSM_PMEM_AEC_AWB:
	case MSM_PMEM_AF:
		rc = msm_pmem_table_add(&sync->stats, pinfo);
		break;

	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int msm_register_pmem(struct msm_sync *sync, void __user *arg)
{
	struct msm_pmem_info info;

	if (copy_from_user(&info, arg, sizeof(info))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	return __msm_register_pmem(sync, &info);
}

static int msm_stats_axi_cfg(struct msm_sync *sync,
		struct msm_vfe_cfg_cmd *cfgcmd)
{
	int rc = -EIO;
	struct axidata axi_data;
	void *data = &axi_data;

	struct msm_pmem_region region[3];
	int pmem_type = MSM_PMEM_MAX;

	memset(&axi_data, 0, sizeof(axi_data));

	switch (cfgcmd->cmd_type) {
	case CMD_STATS_AXI_CFG:
		pmem_type = MSM_PMEM_AEC_AWB;
		break;
	case CMD_STATS_AF_AXI_CFG:
		pmem_type = MSM_PMEM_AF;
		break;
	case CMD_GENERAL:
		data = NULL;
		break;
	default:
		pr_err("%s: unknown command type %d\n",
			__func__, cfgcmd->cmd_type);
		return -EINVAL;
	}

	if (cfgcmd->cmd_type != CMD_GENERAL) {
		axi_data.bufnum1 =
			msm_pmem_region_lookup(&sync->stats, pmem_type,
				&region[0], NUM_WB_EXP_STAT_OUTPUT_BUFFERS);
		if (!axi_data.bufnum1) {
			pr_err("%s: pmem region lookup error\n", __func__);
			return -EINVAL;
		}
		axi_data.region = &region[0];
	}

	/* send the AEC/AWB STATS configuration command to driver */
	if (sync->vfefn.vfe_config)
		rc = sync->vfefn.vfe_config(cfgcmd, &axi_data);

	return rc;
}

static int msm_put_stats_buffer(struct msm_sync *sync, void __user *arg)
{
	int rc = -EIO;

	struct msm_stats_buf buf;
	unsigned long pphy;
	struct msm_vfe_cfg_cmd cfgcmd;

	if (copy_from_user(&buf, arg,
				sizeof(struct msm_stats_buf))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	CDBG("msm_put_stats_buffer\n");
	pphy = msm_pmem_stats_vtop_lookup(sync, buf.buffer, buf.fd);

	if (pphy != 0) {
		if (buf.type == STAT_AEAW)
			cfgcmd.cmd_type = CMD_STATS_BUF_RELEASE;
		else if (buf.type == STAT_AF)
			cfgcmd.cmd_type = CMD_STATS_AF_BUF_RELEASE;
		else {
			pr_err("%s: invalid buf type %d\n",
				__func__,
				buf.type);
			rc = -EINVAL;
			goto put_done;
		}

		cfgcmd.value = (void *)&buf;

		if (sync->vfefn.vfe_config) {
			rc = sync->vfefn.vfe_config(&cfgcmd, &pphy);
			if (rc < 0)
				pr_err("msm_put_stats_buffer: "\
					"vfe_config err %d\n", rc);
		} else
			pr_err("msm_put_stats_buffer: vfe_config is NULL\n");
	} else {
		pr_err("msm_put_stats_buffer: NULL physical address\n");
		rc = -EINVAL;
	}

put_done:
	return rc;
}

static int msm_axi_config(struct msm_sync *sync, void __user *arg)
{
	struct msm_vfe_cfg_cmd cfgcmd;

	if (copy_from_user(&cfgcmd, arg, sizeof(cfgcmd))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	switch (cfgcmd.cmd_type) {
	case CMD_AXI_CFG_OUT1:
	case CMD_AXI_CFG_OUT2:
	case CMD_AXI_CFG_SNAP_O1_AND_O2:
	case CMD_RAW_PICT_AXI_CFG:
		return msm_frame_axi_cfg(sync, &cfgcmd);

	case CMD_STATS_AXI_CFG:
	case CMD_STATS_AF_AXI_CFG:
		return msm_stats_axi_cfg(sync, &cfgcmd);

	default:
		pr_err("%s: unknown command type %d\n",
			__func__,
			cfgcmd.cmd_type);
		return -EINVAL;
	}

	return 0;
}

static int __msm_get_pic(struct msm_sync *sync, struct msm_ctrl_cmd *ctrl)
{
	unsigned long flags;
	int rc = 0;
	int tm;

	struct msm_queue_cmd *qcmd = NULL;

	tm = (int)ctrl->timeout_ms;

	rc = wait_event_interruptible_timeout(
			sync->pict_frame_wait,
			!list_empty_careful(&sync->pict_frame_q),
			msecs_to_jiffies(tm));
	if (list_empty_careful(&sync->pict_frame_q)) {
		if (rc == 0)
			return -ETIMEDOUT;
		if (rc < 0) {
			pr_err("msm_camera_get_picture, rc = %d\n", rc);
			return rc;
		}
	}

	spin_lock_irqsave(&sync->pict_frame_q_lock, flags);
	BUG_ON(list_empty(&sync->pict_frame_q));
	qcmd = list_first_entry(&sync->pict_frame_q,
			struct msm_queue_cmd, list);
	list_del_init(&qcmd->list);
	spin_unlock_irqrestore(&sync->pict_frame_q_lock, flags);

	if (qcmd->command != NULL) {
		struct msm_ctrl_cmd *q =
			(struct msm_ctrl_cmd *)qcmd->command;
		ctrl->type = q->type;
		ctrl->status = q->status;
	} else {
		ctrl->type = -1;
		ctrl->status = -1;
	}

	kfree(qcmd);
	return rc;
}

static int msm_get_pic(struct msm_sync *sync, void __user *arg)
{
	struct msm_ctrl_cmd ctrlcmd_t;
	int rc;

	if (copy_from_user(&ctrlcmd_t,
				arg,
				sizeof(struct msm_ctrl_cmd))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	rc = __msm_get_pic(sync, &ctrlcmd_t);
	if (rc < 0)
		return rc;

	if (sync->croplen) {
		if (ctrlcmd_t.length < sync->croplen) {
			pr_err("msm_get_pic: invalid len %d\n",
				ctrlcmd_t.length);
			return -EINVAL;
		}
		if (copy_to_user(ctrlcmd_t.value,
				sync->cropinfo,
				sync->croplen)) {
			ERR_COPY_TO_USER();
			return -EFAULT;
		}
	}

	if (copy_to_user((void *)arg,
		&ctrlcmd_t,
		sizeof(struct msm_ctrl_cmd))) {
		ERR_COPY_TO_USER();
		return -EFAULT;
	}
	return 0;
}

static int msm_set_crop(struct msm_sync *sync, void __user *arg)
{
	struct crop_info crop;

	if (copy_from_user(&crop,
				arg,
				sizeof(struct crop_info))) {
		ERR_COPY_FROM_USER();
		return -EFAULT;
	}

	if (!sync->croplen) {
		sync->cropinfo = kmalloc(crop.len, GFP_KERNEL);
		if (!sync->cropinfo)
			return -ENOMEM;
	} else if (sync->croplen < crop.len)
		return -EINVAL;

	if (copy_from_user(sync->cropinfo,
				crop.info,
				crop.len)) {
		ERR_COPY_FROM_USER();
		kfree(sync->cropinfo);
		return -EFAULT;
	}

	sync->croplen = crop.len;

	return 0;
}

static int msm_pict_pp_done(struct msm_sync *sync, void __user *arg)
{
	struct msm_ctrl_cmd udata;
	struct msm_ctrl_cmd *ctrlcmd = NULL;
	struct msm_queue_cmd *qcmd = NULL;
	unsigned long flags;
	int rc = 0;

	if (!sync->pict_pp)
		return -EINVAL;

	if (copy_from_user(&udata, arg, sizeof(struct msm_ctrl_cmd))) {
		ERR_COPY_FROM_USER();
		rc = -EFAULT;
		goto pp_fail;
	}

	qcmd = kmalloc(sizeof(struct msm_queue_cmd) +
			sizeof(struct msm_ctrl_cmd),
			GFP_KERNEL);
	if (!qcmd) {
		rc = -ENOMEM;
		goto pp_fail;
	}

	qcmd->type = MSM_CAM_Q_VFE_MSG;
	qcmd->command = ctrlcmd = (struct msm_ctrl_cmd *)(qcmd + 1);
	memset(ctrlcmd, 0, sizeof(struct msm_ctrl_cmd));
	ctrlcmd->type = udata.type;
	ctrlcmd->status = udata.status;

	spin_lock_irqsave(&sync->pict_frame_q_lock, flags);
	list_add_tail(&qcmd->list, &sync->pict_frame_q);
	spin_unlock_irqrestore(&sync->pict_frame_q_lock, flags);
	wake_up(&sync->pict_frame_wait);

pp_fail:
	return rc;
}

static long msm_ioctl_common(struct msm_device *pmsm,
		unsigned int cmd,
		void __user *argp)
{
	CDBG("msm_ioctl_common\n");
	switch (cmd) {
	case MSM_CAM_IOCTL_REGISTER_PMEM:
		return msm_register_pmem(pmsm->sync, argp);
	case MSM_CAM_IOCTL_UNREGISTER_PMEM:
		return msm_pmem_table_del(pmsm->sync, argp);
	default:
		return -EINVAL;
	}
}

static long msm_ioctl_config(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	int rc = -EINVAL;
	void __user *argp = (void __user *)arg;
	struct msm_device *pmsm = filep->private_data;

	CDBG("msm_ioctl_config cmd = %d\n", _IOC_NR(cmd));

	switch (cmd) {
	case MSM_CAM_IOCTL_GET_SENSOR_INFO:
		rc = msm_get_sensor_info(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_CONFIG_VFE:
		/* Coming from config thread for update */
		rc = msm_config_vfe(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_GET_STATS:
		/* Coming from config thread wait
		 * for vfe statistics and control requests */
		rc = msm_get_stats(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_ENABLE_VFE:
		/* This request comes from control thread:
		 * enable either QCAMTASK or VFETASK */
		rc = msm_enable_vfe(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_DISABLE_VFE:
		/* This request comes from control thread:
		 * disable either QCAMTASK or VFETASK */
		rc = msm_disable_vfe(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_VFE_APPS_RESET:
		msm_camio_vfe_blk_reset();
		rc = 0;
		break;

	case MSM_CAM_IOCTL_RELEASE_STATS_BUFFER:
		rc = msm_put_stats_buffer(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_AXI_CONFIG:
		rc = msm_axi_config(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_SET_CROP:
		rc = msm_set_crop(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_PICT_PP: {
		uint8_t enable;
		if (copy_from_user(&enable, argp, sizeof(enable))) {
			ERR_COPY_FROM_USER();
			rc = -EFAULT;
		} else {
			pmsm->sync->pict_pp = enable;
			rc = 0;
		}
		break;
	}

	case MSM_CAM_IOCTL_PICT_PP_DONE:
		rc = msm_pict_pp_done(pmsm->sync, argp);
		break;

	case MSM_CAM_IOCTL_SENSOR_IO_CFG:
		rc = pmsm->sync->sctrl.s_config(argp);
		break;

	case MSM_CAM_IOCTL_FLASH_LED_CFG: {
		uint32_t led_state;
		if (copy_from_user(&led_state, argp, sizeof(led_state))) {
			ERR_COPY_FROM_USER();
			rc = -EFAULT;
		} else
			rc = msm_camera_flash_set_led_state(led_state);
		break;
	}

	default:
		rc = msm_ioctl_common(pmsm, cmd, argp);
		break;
	}

	CDBG("msm_ioctl_config cmd = %d DONE\n", _IOC_NR(cmd));
	return rc;
}

static int msm_unblock_poll_frame(struct msm_sync *);

static long msm_ioctl_frame(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	int rc = -EINVAL;
	void __user *argp = (void __user *)arg;
	struct msm_device *pmsm = filep->private_data;


	switch (cmd) {
	case MSM_CAM_IOCTL_GETFRAME:
		/* Coming from frame thread to get frame
		 * after SELECT is done */
		rc = msm_get_frame(pmsm->sync, argp);
		break;
	case MSM_CAM_IOCTL_RELEASE_FRAME_BUFFER:
		rc = msm_put_frame_buffer(pmsm->sync, argp);
		break;
	case MSM_CAM_IOCTL_UNBLOCK_POLL_FRAME:
		rc = msm_unblock_poll_frame(pmsm->sync);
		break;
	default:
		break;
	}

	return rc;
}


static long msm_ioctl_control(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	int rc = -EINVAL;
	void __user *argp = (void __user *)arg;
	struct msm_control_device *ctrl_pmsm = filep->private_data;
	struct msm_device *pmsm = ctrl_pmsm->pmsm;

	switch (cmd) {
	case MSM_CAM_IOCTL_CTRL_COMMAND:
		/* Coming from control thread, may need to wait for
		 * command status */
		rc = msm_control(ctrl_pmsm, 1, argp);
		break;
	case MSM_CAM_IOCTL_CTRL_COMMAND_2:
		/* Sends a message, returns immediately */
		rc = msm_control(ctrl_pmsm, 0, argp);
		break;
	case MSM_CAM_IOCTL_CTRL_CMD_DONE:
		/* Config thread calls the control thread to notify it
		 * of the result of a MSM_CAM_IOCTL_CTRL_COMMAND.
		 */
		rc = msm_ctrl_cmd_done(ctrl_pmsm, argp);
		break;
	case MSM_CAM_IOCTL_GET_PICTURE:
		rc = msm_get_pic(pmsm->sync, argp);
		break;
	default:
		rc = msm_ioctl_common(pmsm, cmd, argp);
		break;
	}

	return rc;
}

static int __msm_release(struct msm_sync *sync)
{
	struct msm_pmem_region *region;
	struct hlist_node *hnode;
	struct hlist_node *n;

	mutex_lock(&sync->lock);
	if (sync->opencnt)
		sync->opencnt--;

	if (!sync->opencnt) {
		/* need to clean up system resource */
		if (sync->vfefn.vfe_release)
			sync->vfefn.vfe_release(sync->pdev);

		if (sync->cropinfo) {
			kfree(sync->cropinfo);
			sync->cropinfo = NULL;
			sync->croplen = 0;
		}

		hlist_for_each_entry_safe(region, hnode, n,
				&sync->frame, list) {
			hlist_del(hnode);
			put_pmem_file(region->file);
			kfree(region);
		}

		hlist_for_each_entry_safe(region, hnode, n,
				&sync->stats, list) {
			hlist_del(hnode);
			put_pmem_file(region->file);
			kfree(region);
		}

		MSM_DRAIN_QUEUE(sync, msg_event_q);
		MSM_DRAIN_QUEUE(sync, prev_frame_q);
		MSM_DRAIN_QUEUE(sync, pict_frame_q);

		sync->sctrl.s_release();

		sync->apps_id = NULL;
		CDBG("msm_release completed!\n");
	}
	mutex_unlock(&sync->lock);

	return 0;
}

static int msm_release_config(struct inode *node, struct file *filep)
{
	int rc;
	struct msm_device *pmsm = filep->private_data;
	printk("msm_camera: RELEASE %s\n", filep->f_path.dentry->d_name.name);
	rc = __msm_release(pmsm->sync);
	atomic_set(&pmsm->opened, 0);
	return rc;
}

static int msm_release_control(struct inode *node, struct file *filep)
{
	int rc;
	struct msm_control_device *ctrl_pmsm = filep->private_data;
	struct msm_device *pmsm = ctrl_pmsm->pmsm;
	printk(KERN_INFO "msm_camera: RELEASE %s\n",
					filep->f_path.dentry->d_name.name);
	rc = __msm_release(pmsm->sync);
	if (!rc) {
		MSM_DRAIN_QUEUE(&ctrl_pmsm->ctrl_q, ctrl_status_q);
		MSM_DRAIN_QUEUE(pmsm->sync, pict_frame_q);
	}
	kfree(ctrl_pmsm);
	return rc;
}

static int msm_release_frame(struct inode *node, struct file *filep)
{
	int rc;
	struct msm_device *pmsm = filep->private_data;
	printk(KERN_INFO "msm_camera: RELEASE %s\n",
					filep->f_path.dentry->d_name.name);
	rc = __msm_release(pmsm->sync);
	if (!rc) {
		MSM_DRAIN_QUEUE(pmsm->sync, prev_frame_q);
		atomic_set(&pmsm->opened, 0);
	}
	return rc;
}

static int msm_unblock_poll_frame(struct msm_sync *sync)
{
	unsigned long flags;
	CDBG("msm_unblock_poll_frame\n");
	spin_lock_irqsave(&sync->prev_frame_q_lock, flags);
	sync->unblock_poll_frame = 1;
	wake_up(&sync->prev_frame_wait);
	spin_unlock_irqrestore(&sync->prev_frame_q_lock, flags);
	return 0;
}

static unsigned int __msm_poll_frame(struct msm_sync *sync,
		struct file *filep,
		struct poll_table_struct *pll_table)
{
	int rc = 0;
	unsigned long flags;

	poll_wait(filep, &sync->prev_frame_wait, pll_table);

	spin_lock_irqsave(&sync->prev_frame_q_lock, flags);
	if (!list_empty_careful(&sync->prev_frame_q))
		/* frame ready */
		rc = POLLIN | POLLRDNORM;
	if (sync->unblock_poll_frame) {
		CDBG("%s: sync->unblock_poll_frame is true\n", __func__);
		rc |= POLLPRI;
		sync->unblock_poll_frame = 0;
	}
	spin_unlock_irqrestore(&sync->prev_frame_q_lock, flags);

	return rc;
}

static unsigned int msm_poll_frame(struct file *filep,
	struct poll_table_struct *pll_table)
{
	struct msm_device *pmsm = filep->private_data;
	return __msm_poll_frame(pmsm->sync, filep, pll_table);
}

/*
 * This function executes in interrupt context.
 */

static void *msm_vfe_sync_alloc(int size,
			void *syncdata __attribute__((unused)))
{
	struct msm_queue_cmd *qcmd =
		kmalloc(sizeof(struct msm_queue_cmd) + size, GFP_ATOMIC);
	return qcmd ? qcmd + 1 : NULL;
}

/*
 * This function executes in interrupt context.
 */

static void msm_vfe_sync(struct msm_vfe_resp *vdata,
		enum msm_queue qtype, void *syncdata)
{
	struct msm_queue_cmd *qcmd = NULL;
	struct msm_queue_cmd *qcmd_frame = NULL;
	struct msm_vfe_phy_info *fphy;

	unsigned long flags;
	struct msm_sync *sync = (struct msm_sync *)syncdata;
	if (!sync) {
		pr_err("msm_camera: no context in dsp callback.\n");
		return;
	}

	qcmd = ((struct msm_queue_cmd *)vdata) - 1;
	qcmd->type = qtype;

	if (qtype == MSM_CAM_Q_VFE_MSG) {
		switch (vdata->type) {
		case VFE_MSG_OUTPUT1:
		case VFE_MSG_OUTPUT2:
			qcmd_frame =
				kmalloc(sizeof(struct msm_queue_cmd) +
					sizeof(struct msm_vfe_phy_info),
					GFP_ATOMIC);
			if (!qcmd_frame)
				goto mem_fail;
			fphy = (struct msm_vfe_phy_info *)(qcmd_frame + 1);
			*fphy = vdata->phy;

			qcmd_frame->type = MSM_CAM_Q_VFE_MSG;
			qcmd_frame->command = fphy;

			CDBG("qcmd_frame= 0x%x phy_y= 0x%x, phy_cbcr= 0x%x\n",
				(int) qcmd_frame, fphy->y_phy, fphy->cbcr_phy);

			spin_lock_irqsave(&sync->prev_frame_q_lock, flags);
			list_add_tail(&qcmd_frame->list, &sync->prev_frame_q);
			wake_up(&sync->prev_frame_wait);
			spin_unlock_irqrestore(&sync->prev_frame_q_lock, flags);
			CDBG("woke up frame thread\n");
			break;
		case VFE_MSG_SNAPSHOT:
			if (sync->pict_pp)
				break;

			CDBG("snapshot pp = %d\n", sync->pict_pp);
			qcmd_frame =
				kmalloc(sizeof(struct msm_queue_cmd),
					GFP_ATOMIC);
			if (!qcmd_frame)
				goto mem_fail;
			qcmd_frame->type = MSM_CAM_Q_VFE_MSG;
			qcmd_frame->command = NULL;
				spin_lock_irqsave(&sync->pict_frame_q_lock,
				flags);
			list_add_tail(&qcmd_frame->list, &sync->pict_frame_q);
			wake_up(&sync->pict_frame_wait);
			spin_unlock_irqrestore(&sync->pict_frame_q_lock, flags);
			CDBG("woke up picture thread\n");
			break;
		default:
			CDBG("%s: qtype = %d not handled\n",
				__func__, vdata->type);
			break;
		}
	}

	qcmd->command = (void *)vdata;
	CDBG("vdata->type = %d\n", vdata->type);

	spin_lock_irqsave(&sync->msg_event_q_lock, flags);
	list_add_tail(&qcmd->list, &sync->msg_event_q);
	wake_up(&sync->msg_event_wait);
	spin_unlock_irqrestore(&sync->msg_event_q_lock, flags);
	CDBG("woke up config thread\n");
	return;

mem_fail:
	kfree(qcmd);
}

static struct msm_vfe_callback msm_vfe_s = {
	.vfe_resp = msm_vfe_sync,
	.vfe_alloc = msm_vfe_sync_alloc,
};

static int __msm_open(struct msm_sync *sync, const char *const apps_id)
{
	int rc = 0;

	mutex_lock(&sync->lock);
	if (sync->apps_id && strcmp(sync->apps_id, apps_id)) {
		pr_err("msm_camera(%s): sensor %s is already opened for %s\n",
			apps_id,
			sync->sdata->sensor_name,
			sync->apps_id);
		rc = -EBUSY;
		goto msm_open_done;
	}

	sync->apps_id = apps_id;

	if (!sync->opencnt) {

		msm_camvfe_fn_init(&sync->vfefn, sync);
		if (sync->vfefn.vfe_init) {
			rc = sync->vfefn.vfe_init(&msm_vfe_s,
				sync->pdev);
			if (rc < 0) {
				pr_err("vfe_init failed at %d\n", rc);
				goto msm_open_done;
			}
			rc = sync->sctrl.s_init(sync->sdata);
			if (rc < 0) {
				pr_err("sensor init failed: %d\n", rc);
				goto msm_open_done;
			}
		} else {
			pr_err("no sensor init func\n");
			rc = -ENODEV;
			goto msm_open_done;
		}

		if (rc >= 0) {
			INIT_HLIST_HEAD(&sync->frame);
			INIT_HLIST_HEAD(&sync->stats);
			sync->unblock_poll_frame = 0;
		}
	}
	sync->opencnt++;

msm_open_done:
	mutex_unlock(&sync->lock);
	return rc;
}

static int msm_open_common(struct inode *inode, struct file *filep,
			   int once)
{
	int rc;
	struct msm_device *pmsm =
		container_of(inode->i_cdev, struct msm_device, cdev);

	CDBG("msm_camera: open %s\n", filep->f_path.dentry->d_name.name);

	if (atomic_cmpxchg(&pmsm->opened, 0, 1) && once) {
		pr_err("msm_camera: %s is already opened.\n",
			filep->f_path.dentry->d_name.name);
		return -EBUSY;
	}

	rc = nonseekable_open(inode, filep);
	if (rc < 0) {
		pr_err("msm_open: nonseekable_open error %d\n", rc);
		return rc;
	}

	rc = __msm_open(pmsm->sync, MSM_APPS_ID_PROP);
	if (rc < 0)
		return rc;

	filep->private_data = pmsm;

	CDBG("msm_open() open: rc = %d\n", rc);
	return rc;
}

static int msm_open(struct inode *inode, struct file *filep)
{
	return msm_open_common(inode, filep, 1);
}

static int msm_open_control(struct inode *inode, struct file *filep)
{
	int rc;

	struct msm_control_device *ctrl_pmsm =
		kmalloc(sizeof(struct msm_control_device), GFP_KERNEL);
	if (!ctrl_pmsm)
		return -ENOMEM;

	rc = msm_open_common(inode, filep, 0);
	if (rc < 0) {
		kfree(ctrl_pmsm);
		return rc;
	}

	ctrl_pmsm->pmsm = filep->private_data;
	filep->private_data = ctrl_pmsm;
	spin_lock_init(&ctrl_pmsm->ctrl_q.ctrl_status_q_lock);
	INIT_LIST_HEAD(&ctrl_pmsm->ctrl_q.ctrl_status_q);
	init_waitqueue_head(&ctrl_pmsm->ctrl_q.ctrl_status_wait);

	CDBG("msm_open() open: rc = %d\n", rc);
	return rc;
}

static int __msm_v4l2_control(struct msm_sync *sync,
		struct msm_ctrl_cmd *out)
{
	int rc = 0;

	struct msm_queue_cmd *qcmd = NULL, *rcmd = NULL;
	struct msm_ctrl_cmd *ctrl;
	struct msm_control_device_queue FIXME;

	/* wake up config thread, 4 is for V4L2 application */
	qcmd = kmalloc(sizeof(struct msm_queue_cmd), GFP_KERNEL);
	if (!qcmd) {
		pr_err("msm_control: cannot allocate buffer\n");
		rc = -ENOMEM;
		goto end;
	}
	qcmd->type = MSM_CAM_Q_V4L2_REQ;
	qcmd->command = out;

	rcmd = __msm_control(sync, &FIXME, qcmd, out->timeout_ms);
	if (IS_ERR(rcmd)) {
		rc = PTR_ERR(rcmd);
		goto end;
	}

	ctrl = (struct msm_ctrl_cmd *)(rcmd->command);
	/* FIXME: we should just set out->length = ctrl->length; */
	BUG_ON(out->length < ctrl->length);
	memcpy(out->value, ctrl->value, ctrl->length);

end:
	kfree(rcmd);
	CDBG("__msm_v4l2_control: end rc = %d\n", rc);
	return rc;
}

static const struct file_operations msm_fops_config = {
	.owner = THIS_MODULE,
	.open = msm_open,
	.unlocked_ioctl = msm_ioctl_config,
	.release = msm_release_config,
};

static const struct file_operations msm_fops_control = {
	.owner = THIS_MODULE,
	.open = msm_open_control,
	.unlocked_ioctl = msm_ioctl_control,
	.release = msm_release_control,
};

static const struct file_operations msm_fops_frame = {
	.owner = THIS_MODULE,
	.open = msm_open,
	.unlocked_ioctl = msm_ioctl_frame,
	.release = msm_release_frame,
	.poll = msm_poll_frame,
};

static int msm_setup_cdev(struct msm_device *msm,
			int node,
			dev_t devno,
			const char *suffix,
			const struct file_operations *fops)
{
	int rc = -ENODEV;

	struct device *device =
		device_create(msm_class, NULL,
			devno, NULL,
			"%s%d", suffix, node);

	if (IS_ERR(device)) {
		rc = PTR_ERR(device);
		pr_err("msm_camera: error creating device: %d\n", rc);
		return rc;
	}

	cdev_init(&msm->cdev, fops);
	msm->cdev.owner = THIS_MODULE;

	rc = cdev_add(&msm->cdev, devno, 1);
	if (rc < 0) {
		pr_err("msm_camera: error adding cdev: %d\n", rc);
		device_destroy(msm_class, devno);
		return rc;
	}

	return rc;
}

static int msm_tear_down_cdev(struct msm_device *msm, dev_t devno)
{
	cdev_del(&msm->cdev);
	device_destroy(msm_class, devno);
	return 0;
}

int msm_v4l2_register(struct msm_v4l2_driver *drv)
{
	/* FIXME: support multiple sensors */
	if (list_empty(&msm_sensors))
		return -ENODEV;

	drv->sync = list_first_entry(&msm_sensors, struct msm_sync, list);
	drv->open      = __msm_open;
	drv->release   = __msm_release;
	drv->ctrl      = __msm_v4l2_control;
	drv->reg_pmem  = __msm_register_pmem;
	drv->get_frame = __msm_get_frame;
	drv->put_frame = __msm_put_frame_buf;
	drv->get_pict  = __msm_get_pic;
	drv->drv_poll  = __msm_poll_frame;

	return 0;
}
EXPORT_SYMBOL(msm_v4l2_register);

int msm_v4l2_unregister(struct msm_v4l2_driver *drv)
{
	drv->sync = NULL;
	return 0;
}
EXPORT_SYMBOL(msm_v4l2_unregister);

static int msm_sync_init(struct msm_sync *sync,
		struct platform_device *pdev,
		int (*sensor_probe)(const struct msm_camera_sensor_info *,
				struct msm_sensor_ctrl *))
{
	int rc = 0;
	struct msm_sensor_ctrl sctrl;
	sync->sdata = pdev->dev.platform_data;

	spin_lock_init(&sync->msg_event_q_lock);
	INIT_LIST_HEAD(&sync->msg_event_q);
	init_waitqueue_head(&sync->msg_event_wait);

	spin_lock_init(&sync->prev_frame_q_lock);
	INIT_LIST_HEAD(&sync->prev_frame_q);
	init_waitqueue_head(&sync->prev_frame_wait);

	spin_lock_init(&sync->pict_frame_q_lock);
	INIT_LIST_HEAD(&sync->pict_frame_q);
	init_waitqueue_head(&sync->pict_frame_wait);

	rc = msm_camio_probe_on(pdev);
	if (rc < 0)
		return rc;
	rc = sensor_probe(sync->sdata, &sctrl);
	if (rc >= 0) {
		sync->pdev = pdev;
		sync->sctrl = sctrl;
	}
	msm_camio_probe_off(pdev);
	if (rc < 0) {
		pr_err("msm_camera: failed to initialize %s\n",
			sync->sdata->sensor_name);
		return rc;
	}

	sync->opencnt = 0;
	mutex_init(&sync->lock);
	CDBG("initialized %s\n", sync->sdata->sensor_name);
	return rc;
}

static int msm_sync_destroy(struct msm_sync *sync)
{
	return 0;
}

static int msm_device_init(struct msm_device *pmsm,
		struct msm_sync *sync,
		int node)
{
	int dev_num = 3 * node;
	int rc = msm_setup_cdev(pmsm, node,
		MKDEV(MAJOR(msm_devno), dev_num),
		"control", &msm_fops_control);
	if (rc < 0) {
		pr_err("error creating control node: %d\n", rc);
		return rc;
	}

	rc = msm_setup_cdev(pmsm + 1, node,
		MKDEV(MAJOR(msm_devno), dev_num + 1),
		"config", &msm_fops_config);
	if (rc < 0) {
		pr_err("error creating config node: %d\n", rc);
		msm_tear_down_cdev(pmsm, MKDEV(MAJOR(msm_devno),
				dev_num));
		return rc;
	}

	rc = msm_setup_cdev(pmsm + 2, node,
		MKDEV(MAJOR(msm_devno), dev_num + 2),
		"frame", &msm_fops_frame);
	if (rc < 0) {
		pr_err("error creating frame node: %d\n", rc);
		msm_tear_down_cdev(pmsm,
			MKDEV(MAJOR(msm_devno), dev_num));
		msm_tear_down_cdev(pmsm + 1,
			MKDEV(MAJOR(msm_devno), dev_num + 1));
		return rc;
	}

	atomic_set(&pmsm[0].opened, 0);
	atomic_set(&pmsm[1].opened, 0);
	atomic_set(&pmsm[2].opened, 0);

	pmsm[0].sync = sync;
	pmsm[1].sync = sync;
	pmsm[2].sync = sync;

	return rc;
}

int msm_camera_drv_start(struct platform_device *dev,
		int (*sensor_probe)(const struct msm_camera_sensor_info *,
			struct msm_sensor_ctrl *))
{
	struct msm_device *pmsm = NULL;
	struct msm_sync *sync;
	int rc = -ENODEV;
	static int camera_node;

	if (camera_node >= MSM_MAX_CAMERA_SENSORS) {
		pr_err("msm_camera: too many camera sensors\n");
		return rc;
	}

	if (!msm_class) {
		/* There are three device nodes per sensor */
		rc = alloc_chrdev_region(&msm_devno, 0,
				3 * MSM_MAX_CAMERA_SENSORS,
				"msm_camera");
		if (rc < 0) {
			pr_err("msm_camera: failed to allocate chrdev: %d\n",
				rc);
			return rc;
		}

		msm_class = class_create(THIS_MODULE, "msm_camera");
		if (IS_ERR(msm_class)) {
			rc = PTR_ERR(msm_class);
			pr_err("msm_camera: create device class failed: %d\n",
				rc);
			return rc;
		}
	}

	pmsm = kzalloc(sizeof(struct msm_device) * 3 +
			sizeof(struct msm_sync), GFP_ATOMIC);
	if (!pmsm)
		return -ENOMEM;
	sync = (struct msm_sync *)(pmsm + 3);

	rc = msm_sync_init(sync, dev, sensor_probe);
	if (rc < 0) {
		kfree(pmsm);
		return rc;
	}

	CDBG("setting camera node %d\n", camera_node);
	rc = msm_device_init(pmsm, sync, camera_node);
	if (rc < 0) {
		msm_sync_destroy(sync);
		kfree(pmsm);
		return rc;
	}

	camera_node++;
	list_add(&sync->list, &msm_sensors);
	return rc;
}
EXPORT_SYMBOL(msm_camera_drv_start);

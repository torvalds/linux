/*******************************************************************************
 * Vhost kernel TCM fabric driver for virtio SCSI initiators
 *
 * (C) Copyright 2010-2013 Datera, Inc.
 * (C) Copyright 2010-2012 IBM Corp.
 *
 * Licensed to the Linux Foundation under the General Public License (GPL) version 2.
 *
 * Authors: Nicholas A. Bellinger <nab@daterainc.com>
 *          Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 ****************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <generated/utsrelease.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/compat.h>
#include <linux/eventfd.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <asm/unaligned.h>
#include <scsi/scsi_common.h>
#include <scsi/scsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <linux/vhost.h>
#include <linux/virtio_scsi.h>
#include <linux/llist.h>
#include <linux/bitmap.h>

#include "vhost.h"

#define VHOST_SCSI_VERSION  "v0.1"
#define VHOST_SCSI_NAMELEN 256
#define VHOST_SCSI_MAX_CDB_SIZE 32
#define VHOST_SCSI_PREALLOC_SGLS 2048
#define VHOST_SCSI_PREALLOC_UPAGES 2048
#define VHOST_SCSI_PREALLOC_PROT_SGLS 2048

/* Max number of requests before requeueing the job.
 * Using this limit prevents one virtqueue from starving others with
 * request.
 */
#define VHOST_SCSI_WEIGHT 256

struct vhost_scsi_inflight {
	/* Wait for the flush operation to finish */
	struct completion comp;
	/* Refcount for the inflight reqs */
	struct kref kref;
};

struct vhost_scsi_cmd {
	/* Descriptor from vhost_get_vq_desc() for virt_queue segment */
	int tvc_vq_desc;
	/* virtio-scsi initiator task attribute */
	int tvc_task_attr;
	/* virtio-scsi response incoming iovecs */
	int tvc_in_iovs;
	/* virtio-scsi initiator data direction */
	enum dma_data_direction tvc_data_direction;
	/* Expected data transfer length from virtio-scsi header */
	u32 tvc_exp_data_len;
	/* The Tag from include/linux/virtio_scsi.h:struct virtio_scsi_cmd_req */
	u64 tvc_tag;
	/* The number of scatterlists associated with this cmd */
	u32 tvc_sgl_count;
	u32 tvc_prot_sgl_count;
	/* Saved unpacked SCSI LUN for vhost_scsi_submission_work() */
	u32 tvc_lun;
	/* Pointer to the SGL formatted memory from virtio-scsi */
	struct scatterlist *tvc_sgl;
	struct scatterlist *tvc_prot_sgl;
	struct page **tvc_upages;
	/* Pointer to response header iovec */
	struct iovec tvc_resp_iov;
	/* Pointer to vhost_scsi for our device */
	struct vhost_scsi *tvc_vhost;
	/* Pointer to vhost_virtqueue for the cmd */
	struct vhost_virtqueue *tvc_vq;
	/* Pointer to vhost nexus memory */
	struct vhost_scsi_nexus *tvc_nexus;
	/* The TCM I/O descriptor that is accessed via container_of() */
	struct se_cmd tvc_se_cmd;
	/* work item used for cmwq dispatch to vhost_scsi_submission_work() */
	struct work_struct work;
	/* Copy of the incoming SCSI command descriptor block (CDB) */
	unsigned char tvc_cdb[VHOST_SCSI_MAX_CDB_SIZE];
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char tvc_sense_buf[TRANSPORT_SENSE_BUFFER];
	/* Completed commands list, serviced from vhost worker thread */
	struct llist_node tvc_completion_list;
	/* Used to track inflight cmd */
	struct vhost_scsi_inflight *inflight;
};

struct vhost_scsi_nexus {
	/* Pointer to TCM session for I_T Nexus */
	struct se_session *tvn_se_sess;
};

struct vhost_scsi_tpg {
	/* Vhost port target portal group tag for TCM */
	u16 tport_tpgt;
	/* Used to track number of TPG Port/Lun Links wrt to explict I_T Nexus shutdown */
	int tv_tpg_port_count;
	/* Used for vhost_scsi device reference to tpg_nexus, protected by tv_tpg_mutex */
	int tv_tpg_vhost_count;
	/* Used for enabling T10-PI with legacy devices */
	int tv_fabric_prot_type;
	/* list for vhost_scsi_list */
	struct list_head tv_tpg_list;
	/* Used to protect access for tpg_nexus */
	struct mutex tv_tpg_mutex;
	/* Pointer to the TCM VHost I_T Nexus for this TPG endpoint */
	struct vhost_scsi_nexus *tpg_nexus;
	/* Pointer back to vhost_scsi_tport */
	struct vhost_scsi_tport *tport;
	/* Returned by vhost_scsi_make_tpg() */
	struct se_portal_group se_tpg;
	/* Pointer back to vhost_scsi, protected by tv_tpg_mutex */
	struct vhost_scsi *vhost_scsi;
	struct list_head tmf_queue;
};

struct vhost_scsi_tport {
	/* SCSI protocol the tport is providing */
	u8 tport_proto_id;
	/* Binary World Wide unique Port Name for Vhost Target port */
	u64 tport_wwpn;
	/* ASCII formatted WWPN for Vhost Target port */
	char tport_name[VHOST_SCSI_NAMELEN];
	/* Returned by vhost_scsi_make_tport() */
	struct se_wwn tport_wwn;
};

struct vhost_scsi_evt {
	/* event to be sent to guest */
	struct virtio_scsi_event event;
	/* event list, serviced from vhost worker thread */
	struct llist_node list;
};

enum {
	VHOST_SCSI_VQ_CTL = 0,
	VHOST_SCSI_VQ_EVT = 1,
	VHOST_SCSI_VQ_IO = 2,
};

/* Note: can't set VIRTIO_F_VERSION_1 yet, since that implies ANY_LAYOUT. */
enum {
	VHOST_SCSI_FEATURES = VHOST_FEATURES | (1ULL << VIRTIO_SCSI_F_HOTPLUG) |
					       (1ULL << VIRTIO_SCSI_F_T10_PI)
};

#define VHOST_SCSI_MAX_TARGET	256
#define VHOST_SCSI_MAX_VQ	128
#define VHOST_SCSI_MAX_EVENT	128

struct vhost_scsi_virtqueue {
	struct vhost_virtqueue vq;
	/*
	 * Reference counting for inflight reqs, used for flush operation. At
	 * each time, one reference tracks new commands submitted, while we
	 * wait for another one to reach 0.
	 */
	struct vhost_scsi_inflight inflights[2];
	/*
	 * Indicate current inflight in use, protected by vq->mutex.
	 * Writers must also take dev mutex and flush under it.
	 */
	int inflight_idx;
	struct vhost_scsi_cmd *scsi_cmds;
	struct sbitmap scsi_tags;
	int max_cmds;
};

struct vhost_scsi {
	/* Protected by vhost_scsi->dev.mutex */
	struct vhost_scsi_tpg **vs_tpg;
	char vs_vhost_wwpn[TRANSPORT_IQN_LEN];

	struct vhost_dev dev;
	struct vhost_scsi_virtqueue vqs[VHOST_SCSI_MAX_VQ];

	struct vhost_work vs_completion_work; /* cmd completion work item */
	struct llist_head vs_completion_list; /* cmd completion queue */

	struct vhost_work vs_event_work; /* evt injection work item */
	struct llist_head vs_event_list; /* evt injection queue */

	bool vs_events_missed; /* any missed events, protected by vq->mutex */
	int vs_events_nr; /* num of pending events, protected by vq->mutex */
};

struct vhost_scsi_tmf {
	struct vhost_work vwork;
	struct vhost_scsi_tpg *tpg;
	struct vhost_scsi *vhost;
	struct vhost_scsi_virtqueue *svq;
	struct list_head queue_entry;

	struct se_cmd se_cmd;
	struct vhost_scsi_inflight *inflight;
	struct iovec resp_iov;
	int in_iovs;
	int vq_desc;
};

/*
 * Context for processing request and control queue operations.
 */
struct vhost_scsi_ctx {
	int head;
	unsigned int out, in;
	size_t req_size, rsp_size;
	size_t out_size, in_size;
	u8 *target, *lunp;
	void *req;
	struct iov_iter out_iter;
};

static struct workqueue_struct *vhost_scsi_workqueue;

/* Global spinlock to protect vhost_scsi TPG list for vhost IOCTL access */
static DEFINE_MUTEX(vhost_scsi_mutex);
static LIST_HEAD(vhost_scsi_list);

static void vhost_scsi_done_inflight(struct kref *kref)
{
	struct vhost_scsi_inflight *inflight;

	inflight = container_of(kref, struct vhost_scsi_inflight, kref);
	complete(&inflight->comp);
}

static void vhost_scsi_init_inflight(struct vhost_scsi *vs,
				    struct vhost_scsi_inflight *old_inflight[])
{
	struct vhost_scsi_inflight *new_inflight;
	struct vhost_virtqueue *vq;
	int idx, i;

	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
		vq = &vs->vqs[i].vq;

		mutex_lock(&vq->mutex);

		/* store old infight */
		idx = vs->vqs[i].inflight_idx;
		if (old_inflight)
			old_inflight[i] = &vs->vqs[i].inflights[idx];

		/* setup new infight */
		vs->vqs[i].inflight_idx = idx ^ 1;
		new_inflight = &vs->vqs[i].inflights[idx ^ 1];
		kref_init(&new_inflight->kref);
		init_completion(&new_inflight->comp);

		mutex_unlock(&vq->mutex);
	}
}

static struct vhost_scsi_inflight *
vhost_scsi_get_inflight(struct vhost_virtqueue *vq)
{
	struct vhost_scsi_inflight *inflight;
	struct vhost_scsi_virtqueue *svq;

	svq = container_of(vq, struct vhost_scsi_virtqueue, vq);
	inflight = &svq->inflights[svq->inflight_idx];
	kref_get(&inflight->kref);

	return inflight;
}

static void vhost_scsi_put_inflight(struct vhost_scsi_inflight *inflight)
{
	kref_put(&inflight->kref, vhost_scsi_done_inflight);
}

static int vhost_scsi_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int vhost_scsi_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static char *vhost_scsi_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	struct vhost_scsi_tport *tport = tpg->tport;

	return &tport->tport_name[0];
}

static u16 vhost_scsi_get_tpgt(struct se_portal_group *se_tpg)
{
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	return tpg->tport_tpgt;
}

static int vhost_scsi_check_prot_fabric_only(struct se_portal_group *se_tpg)
{
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);

	return tpg->tv_fabric_prot_type;
}

static u32 vhost_scsi_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static void vhost_scsi_release_cmd_res(struct se_cmd *se_cmd)
{
	struct vhost_scsi_cmd *tv_cmd = container_of(se_cmd,
				struct vhost_scsi_cmd, tvc_se_cmd);
	struct vhost_scsi_virtqueue *svq = container_of(tv_cmd->tvc_vq,
				struct vhost_scsi_virtqueue, vq);
	struct vhost_scsi_inflight *inflight = tv_cmd->inflight;
	int i;

	if (tv_cmd->tvc_sgl_count) {
		for (i = 0; i < tv_cmd->tvc_sgl_count; i++)
			put_page(sg_page(&tv_cmd->tvc_sgl[i]));
	}
	if (tv_cmd->tvc_prot_sgl_count) {
		for (i = 0; i < tv_cmd->tvc_prot_sgl_count; i++)
			put_page(sg_page(&tv_cmd->tvc_prot_sgl[i]));
	}

	sbitmap_clear_bit(&svq->scsi_tags, se_cmd->map_tag);
	vhost_scsi_put_inflight(inflight);
}

static void vhost_scsi_release_tmf_res(struct vhost_scsi_tmf *tmf)
{
	struct vhost_scsi_tpg *tpg = tmf->tpg;
	struct vhost_scsi_inflight *inflight = tmf->inflight;

	mutex_lock(&tpg->tv_tpg_mutex);
	list_add_tail(&tpg->tmf_queue, &tmf->queue_entry);
	mutex_unlock(&tpg->tv_tpg_mutex);
	vhost_scsi_put_inflight(inflight);
}

static void vhost_scsi_release_cmd(struct se_cmd *se_cmd)
{
	if (se_cmd->se_cmd_flags & SCF_SCSI_TMR_CDB) {
		struct vhost_scsi_tmf *tmf = container_of(se_cmd,
					struct vhost_scsi_tmf, se_cmd);

		vhost_work_queue(&tmf->vhost->dev, &tmf->vwork);
	} else {
		struct vhost_scsi_cmd *cmd = container_of(se_cmd,
					struct vhost_scsi_cmd, tvc_se_cmd);
		struct vhost_scsi *vs = cmd->tvc_vhost;

		llist_add(&cmd->tvc_completion_list, &vs->vs_completion_list);
		vhost_work_queue(&vs->dev, &vs->vs_completion_work);
	}
}

static u32 vhost_scsi_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static int vhost_scsi_write_pending(struct se_cmd *se_cmd)
{
	/* Go ahead and process the write immediately */
	target_execute_cmd(se_cmd);
	return 0;
}

static void vhost_scsi_set_default_node_attrs(struct se_node_acl *nacl)
{
	return;
}

static int vhost_scsi_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static int vhost_scsi_queue_data_in(struct se_cmd *se_cmd)
{
	transport_generic_free_cmd(se_cmd, 0);
	return 0;
}

static int vhost_scsi_queue_status(struct se_cmd *se_cmd)
{
	transport_generic_free_cmd(se_cmd, 0);
	return 0;
}

static void vhost_scsi_queue_tm_rsp(struct se_cmd *se_cmd)
{
	struct vhost_scsi_tmf *tmf = container_of(se_cmd, struct vhost_scsi_tmf,
						  se_cmd);

	transport_generic_free_cmd(&tmf->se_cmd, 0);
}

static void vhost_scsi_aborted_task(struct se_cmd *se_cmd)
{
	return;
}

static void vhost_scsi_free_evt(struct vhost_scsi *vs, struct vhost_scsi_evt *evt)
{
	vs->vs_events_nr--;
	kfree(evt);
}

static struct vhost_scsi_evt *
vhost_scsi_allocate_evt(struct vhost_scsi *vs,
		       u32 event, u32 reason)
{
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
	struct vhost_scsi_evt *evt;

	if (vs->vs_events_nr > VHOST_SCSI_MAX_EVENT) {
		vs->vs_events_missed = true;
		return NULL;
	}

	evt = kzalloc(sizeof(*evt), GFP_KERNEL);
	if (!evt) {
		vq_err(vq, "Failed to allocate vhost_scsi_evt\n");
		vs->vs_events_missed = true;
		return NULL;
	}

	evt->event.event = cpu_to_vhost32(vq, event);
	evt->event.reason = cpu_to_vhost32(vq, reason);
	vs->vs_events_nr++;

	return evt;
}

static int vhost_scsi_check_stop_free(struct se_cmd *se_cmd)
{
	return target_put_sess_cmd(se_cmd);
}

static void
vhost_scsi_do_evt_work(struct vhost_scsi *vs, struct vhost_scsi_evt *evt)
{
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
	struct virtio_scsi_event *event = &evt->event;
	struct virtio_scsi_event __user *eventp;
	unsigned out, in;
	int head, ret;

	if (!vhost_vq_get_backend(vq)) {
		vs->vs_events_missed = true;
		return;
	}

again:
	vhost_disable_notify(&vs->dev, vq);
	head = vhost_get_vq_desc(vq, vq->iov,
			ARRAY_SIZE(vq->iov), &out, &in,
			NULL, NULL);
	if (head < 0) {
		vs->vs_events_missed = true;
		return;
	}
	if (head == vq->num) {
		if (vhost_enable_notify(&vs->dev, vq))
			goto again;
		vs->vs_events_missed = true;
		return;
	}

	if ((vq->iov[out].iov_len != sizeof(struct virtio_scsi_event))) {
		vq_err(vq, "Expecting virtio_scsi_event, got %zu bytes\n",
				vq->iov[out].iov_len);
		vs->vs_events_missed = true;
		return;
	}

	if (vs->vs_events_missed) {
		event->event |= cpu_to_vhost32(vq, VIRTIO_SCSI_T_EVENTS_MISSED);
		vs->vs_events_missed = false;
	}

	eventp = vq->iov[out].iov_base;
	ret = __copy_to_user(eventp, event, sizeof(*event));
	if (!ret)
		vhost_add_used_and_signal(&vs->dev, vq, head, 0);
	else
		vq_err(vq, "Faulted on vhost_scsi_send_event\n");
}

static void vhost_scsi_evt_work(struct vhost_work *work)
{
	struct vhost_scsi *vs = container_of(work, struct vhost_scsi,
					vs_event_work);
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
	struct vhost_scsi_evt *evt, *t;
	struct llist_node *llnode;

	mutex_lock(&vq->mutex);
	llnode = llist_del_all(&vs->vs_event_list);
	llist_for_each_entry_safe(evt, t, llnode, list) {
		vhost_scsi_do_evt_work(vs, evt);
		vhost_scsi_free_evt(vs, evt);
	}
	mutex_unlock(&vq->mutex);
}

/* Fill in status and signal that we are done processing this command
 *
 * This is scheduled in the vhost work queue so we are called with the owner
 * process mm and can access the vring.
 */
static void vhost_scsi_complete_cmd_work(struct vhost_work *work)
{
	struct vhost_scsi *vs = container_of(work, struct vhost_scsi,
					vs_completion_work);
	DECLARE_BITMAP(signal, VHOST_SCSI_MAX_VQ);
	struct virtio_scsi_cmd_resp v_rsp;
	struct vhost_scsi_cmd *cmd, *t;
	struct llist_node *llnode;
	struct se_cmd *se_cmd;
	struct iov_iter iov_iter;
	int ret, vq;

	bitmap_zero(signal, VHOST_SCSI_MAX_VQ);
	llnode = llist_del_all(&vs->vs_completion_list);
	llist_for_each_entry_safe(cmd, t, llnode, tvc_completion_list) {
		se_cmd = &cmd->tvc_se_cmd;

		pr_debug("%s tv_cmd %p resid %u status %#02x\n", __func__,
			cmd, se_cmd->residual_count, se_cmd->scsi_status);

		memset(&v_rsp, 0, sizeof(v_rsp));
		v_rsp.resid = cpu_to_vhost32(cmd->tvc_vq, se_cmd->residual_count);
		/* TODO is status_qualifier field needed? */
		v_rsp.status = se_cmd->scsi_status;
		v_rsp.sense_len = cpu_to_vhost32(cmd->tvc_vq,
						 se_cmd->scsi_sense_length);
		memcpy(v_rsp.sense, cmd->tvc_sense_buf,
		       se_cmd->scsi_sense_length);

		iov_iter_init(&iov_iter, READ, &cmd->tvc_resp_iov,
			      cmd->tvc_in_iovs, sizeof(v_rsp));
		ret = copy_to_iter(&v_rsp, sizeof(v_rsp), &iov_iter);
		if (likely(ret == sizeof(v_rsp))) {
			struct vhost_scsi_virtqueue *q;
			vhost_add_used(cmd->tvc_vq, cmd->tvc_vq_desc, 0);
			q = container_of(cmd->tvc_vq, struct vhost_scsi_virtqueue, vq);
			vq = q - vs->vqs;
			__set_bit(vq, signal);
		} else
			pr_err("Faulted on virtio_scsi_cmd_resp\n");

		vhost_scsi_release_cmd_res(se_cmd);
	}

	vq = -1;
	while ((vq = find_next_bit(signal, VHOST_SCSI_MAX_VQ, vq + 1))
		< VHOST_SCSI_MAX_VQ)
		vhost_signal(&vs->dev, &vs->vqs[vq].vq);
}

static struct vhost_scsi_cmd *
vhost_scsi_get_cmd(struct vhost_virtqueue *vq, struct vhost_scsi_tpg *tpg,
		   unsigned char *cdb, u64 scsi_tag, u16 lun, u8 task_attr,
		   u32 exp_data_len, int data_direction)
{
	struct vhost_scsi_virtqueue *svq = container_of(vq,
					struct vhost_scsi_virtqueue, vq);
	struct vhost_scsi_cmd *cmd;
	struct vhost_scsi_nexus *tv_nexus;
	struct scatterlist *sg, *prot_sg;
	struct page **pages;
	int tag;

	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		pr_err("Unable to locate active struct vhost_scsi_nexus\n");
		return ERR_PTR(-EIO);
	}

	tag = sbitmap_get(&svq->scsi_tags, 0, false);
	if (tag < 0) {
		pr_err("Unable to obtain tag for vhost_scsi_cmd\n");
		return ERR_PTR(-ENOMEM);
	}

	cmd = &svq->scsi_cmds[tag];
	sg = cmd->tvc_sgl;
	prot_sg = cmd->tvc_prot_sgl;
	pages = cmd->tvc_upages;
	memset(cmd, 0, sizeof(*cmd));
	cmd->tvc_sgl = sg;
	cmd->tvc_prot_sgl = prot_sg;
	cmd->tvc_upages = pages;
	cmd->tvc_se_cmd.map_tag = tag;
	cmd->tvc_tag = scsi_tag;
	cmd->tvc_lun = lun;
	cmd->tvc_task_attr = task_attr;
	cmd->tvc_exp_data_len = exp_data_len;
	cmd->tvc_data_direction = data_direction;
	cmd->tvc_nexus = tv_nexus;
	cmd->inflight = vhost_scsi_get_inflight(vq);

	memcpy(cmd->tvc_cdb, cdb, VHOST_SCSI_MAX_CDB_SIZE);

	return cmd;
}

/*
 * Map a user memory range into a scatterlist
 *
 * Returns the number of scatterlist entries used or -errno on error.
 */
static int
vhost_scsi_map_to_sgl(struct vhost_scsi_cmd *cmd,
		      struct iov_iter *iter,
		      struct scatterlist *sgl,
		      bool write)
{
	struct page **pages = cmd->tvc_upages;
	struct scatterlist *sg = sgl;
	ssize_t bytes;
	size_t offset;
	unsigned int npages = 0;

	bytes = iov_iter_get_pages(iter, pages, LONG_MAX,
				VHOST_SCSI_PREALLOC_UPAGES, &offset);
	/* No pages were pinned */
	if (bytes <= 0)
		return bytes < 0 ? bytes : -EFAULT;

	iov_iter_advance(iter, bytes);

	while (bytes) {
		unsigned n = min_t(unsigned, PAGE_SIZE - offset, bytes);
		sg_set_page(sg++, pages[npages++], n, offset);
		bytes -= n;
		offset = 0;
	}
	return npages;
}

static int
vhost_scsi_calc_sgls(struct iov_iter *iter, size_t bytes, int max_sgls)
{
	int sgl_count = 0;

	if (!iter || !iter->iov) {
		pr_err("%s: iter->iov is NULL, but expected bytes: %zu"
		       " present\n", __func__, bytes);
		return -EINVAL;
	}

	sgl_count = iov_iter_npages(iter, 0xffff);
	if (sgl_count > max_sgls) {
		pr_err("%s: requested sgl_count: %d exceeds pre-allocated"
		       " max_sgls: %d\n", __func__, sgl_count, max_sgls);
		return -EINVAL;
	}
	return sgl_count;
}

static int
vhost_scsi_iov_to_sgl(struct vhost_scsi_cmd *cmd, bool write,
		      struct iov_iter *iter,
		      struct scatterlist *sg, int sg_count)
{
	struct scatterlist *p = sg;
	int ret;

	while (iov_iter_count(iter)) {
		ret = vhost_scsi_map_to_sgl(cmd, iter, sg, write);
		if (ret < 0) {
			while (p < sg) {
				struct page *page = sg_page(p++);
				if (page)
					put_page(page);
			}
			return ret;
		}
		sg += ret;
	}
	return 0;
}

static int
vhost_scsi_mapal(struct vhost_scsi_cmd *cmd,
		 size_t prot_bytes, struct iov_iter *prot_iter,
		 size_t data_bytes, struct iov_iter *data_iter)
{
	int sgl_count, ret;
	bool write = (cmd->tvc_data_direction == DMA_FROM_DEVICE);

	if (prot_bytes) {
		sgl_count = vhost_scsi_calc_sgls(prot_iter, prot_bytes,
						 VHOST_SCSI_PREALLOC_PROT_SGLS);
		if (sgl_count < 0)
			return sgl_count;

		sg_init_table(cmd->tvc_prot_sgl, sgl_count);
		cmd->tvc_prot_sgl_count = sgl_count;
		pr_debug("%s prot_sg %p prot_sgl_count %u\n", __func__,
			 cmd->tvc_prot_sgl, cmd->tvc_prot_sgl_count);

		ret = vhost_scsi_iov_to_sgl(cmd, write, prot_iter,
					    cmd->tvc_prot_sgl,
					    cmd->tvc_prot_sgl_count);
		if (ret < 0) {
			cmd->tvc_prot_sgl_count = 0;
			return ret;
		}
	}
	sgl_count = vhost_scsi_calc_sgls(data_iter, data_bytes,
					 VHOST_SCSI_PREALLOC_SGLS);
	if (sgl_count < 0)
		return sgl_count;

	sg_init_table(cmd->tvc_sgl, sgl_count);
	cmd->tvc_sgl_count = sgl_count;
	pr_debug("%s data_sg %p data_sgl_count %u\n", __func__,
		  cmd->tvc_sgl, cmd->tvc_sgl_count);

	ret = vhost_scsi_iov_to_sgl(cmd, write, data_iter,
				    cmd->tvc_sgl, cmd->tvc_sgl_count);
	if (ret < 0) {
		cmd->tvc_sgl_count = 0;
		return ret;
	}
	return 0;
}

static int vhost_scsi_to_tcm_attr(int attr)
{
	switch (attr) {
	case VIRTIO_SCSI_S_SIMPLE:
		return TCM_SIMPLE_TAG;
	case VIRTIO_SCSI_S_ORDERED:
		return TCM_ORDERED_TAG;
	case VIRTIO_SCSI_S_HEAD:
		return TCM_HEAD_TAG;
	case VIRTIO_SCSI_S_ACA:
		return TCM_ACA_TAG;
	default:
		break;
	}
	return TCM_SIMPLE_TAG;
}

static void vhost_scsi_submission_work(struct work_struct *work)
{
	struct vhost_scsi_cmd *cmd =
		container_of(work, struct vhost_scsi_cmd, work);
	struct vhost_scsi_nexus *tv_nexus;
	struct se_cmd *se_cmd = &cmd->tvc_se_cmd;
	struct scatterlist *sg_ptr, *sg_prot_ptr = NULL;
	int rc;

	/* FIXME: BIDI operation */
	if (cmd->tvc_sgl_count) {
		sg_ptr = cmd->tvc_sgl;

		if (cmd->tvc_prot_sgl_count)
			sg_prot_ptr = cmd->tvc_prot_sgl;
		else
			se_cmd->prot_pto = true;
	} else {
		sg_ptr = NULL;
	}
	tv_nexus = cmd->tvc_nexus;

	se_cmd->tag = 0;
	rc = target_submit_cmd_map_sgls(se_cmd, tv_nexus->tvn_se_sess,
			cmd->tvc_cdb, &cmd->tvc_sense_buf[0],
			cmd->tvc_lun, cmd->tvc_exp_data_len,
			vhost_scsi_to_tcm_attr(cmd->tvc_task_attr),
			cmd->tvc_data_direction, TARGET_SCF_ACK_KREF,
			sg_ptr, cmd->tvc_sgl_count, NULL, 0, sg_prot_ptr,
			cmd->tvc_prot_sgl_count);
	if (rc < 0) {
		transport_send_check_condition_and_sense(se_cmd,
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE, 0);
		transport_generic_free_cmd(se_cmd, 0);
	}
}

static void
vhost_scsi_send_bad_target(struct vhost_scsi *vs,
			   struct vhost_virtqueue *vq,
			   int head, unsigned out)
{
	struct virtio_scsi_cmd_resp __user *resp;
	struct virtio_scsi_cmd_resp rsp;
	int ret;

	memset(&rsp, 0, sizeof(rsp));
	rsp.response = VIRTIO_SCSI_S_BAD_TARGET;
	resp = vq->iov[out].iov_base;
	ret = __copy_to_user(resp, &rsp, sizeof(rsp));
	if (!ret)
		vhost_add_used_and_signal(&vs->dev, vq, head, 0);
	else
		pr_err("Faulted on virtio_scsi_cmd_resp\n");
}

static int
vhost_scsi_get_desc(struct vhost_scsi *vs, struct vhost_virtqueue *vq,
		    struct vhost_scsi_ctx *vc)
{
	int ret = -ENXIO;

	vc->head = vhost_get_vq_desc(vq, vq->iov,
				     ARRAY_SIZE(vq->iov), &vc->out, &vc->in,
				     NULL, NULL);

	pr_debug("vhost_get_vq_desc: head: %d, out: %u in: %u\n",
		 vc->head, vc->out, vc->in);

	/* On error, stop handling until the next kick. */
	if (unlikely(vc->head < 0))
		goto done;

	/* Nothing new?  Wait for eventfd to tell us they refilled. */
	if (vc->head == vq->num) {
		if (unlikely(vhost_enable_notify(&vs->dev, vq))) {
			vhost_disable_notify(&vs->dev, vq);
			ret = -EAGAIN;
		}
		goto done;
	}

	/*
	 * Get the size of request and response buffers.
	 * FIXME: Not correct for BIDI operation
	 */
	vc->out_size = iov_length(vq->iov, vc->out);
	vc->in_size = iov_length(&vq->iov[vc->out], vc->in);

	/*
	 * Copy over the virtio-scsi request header, which for a
	 * ANY_LAYOUT enabled guest may span multiple iovecs, or a
	 * single iovec may contain both the header + outgoing
	 * WRITE payloads.
	 *
	 * copy_from_iter() will advance out_iter, so that it will
	 * point at the start of the outgoing WRITE payload, if
	 * DMA_TO_DEVICE is set.
	 */
	iov_iter_init(&vc->out_iter, WRITE, vq->iov, vc->out, vc->out_size);
	ret = 0;

done:
	return ret;
}

static int
vhost_scsi_chk_size(struct vhost_virtqueue *vq, struct vhost_scsi_ctx *vc)
{
	if (unlikely(vc->in_size < vc->rsp_size)) {
		vq_err(vq,
		       "Response buf too small, need min %zu bytes got %zu",
		       vc->rsp_size, vc->in_size);
		return -EINVAL;
	} else if (unlikely(vc->out_size < vc->req_size)) {
		vq_err(vq,
		       "Request buf too small, need min %zu bytes got %zu",
		       vc->req_size, vc->out_size);
		return -EIO;
	}

	return 0;
}

static int
vhost_scsi_get_req(struct vhost_virtqueue *vq, struct vhost_scsi_ctx *vc,
		   struct vhost_scsi_tpg **tpgp)
{
	int ret = -EIO;

	if (unlikely(!copy_from_iter_full(vc->req, vc->req_size,
					  &vc->out_iter))) {
		vq_err(vq, "Faulted on copy_from_iter_full\n");
	} else if (unlikely(*vc->lunp != 1)) {
		/* virtio-scsi spec requires byte 0 of the lun to be 1 */
		vq_err(vq, "Illegal virtio-scsi lun: %u\n", *vc->lunp);
	} else {
		struct vhost_scsi_tpg **vs_tpg, *tpg;

		vs_tpg = vhost_vq_get_backend(vq);	/* validated at handler entry */

		tpg = READ_ONCE(vs_tpg[*vc->target]);
		if (unlikely(!tpg)) {
			vq_err(vq, "Target 0x%x does not exist\n", *vc->target);
		} else {
			if (tpgp)
				*tpgp = tpg;
			ret = 0;
		}
	}

	return ret;
}

static u16 vhost_buf_to_lun(u8 *lun_buf)
{
	return ((lun_buf[2] << 8) | lun_buf[3]) & 0x3FFF;
}

static void
vhost_scsi_handle_vq(struct vhost_scsi *vs, struct vhost_virtqueue *vq)
{
	struct vhost_scsi_tpg **vs_tpg, *tpg;
	struct virtio_scsi_cmd_req v_req;
	struct virtio_scsi_cmd_req_pi v_req_pi;
	struct vhost_scsi_ctx vc;
	struct vhost_scsi_cmd *cmd;
	struct iov_iter in_iter, prot_iter, data_iter;
	u64 tag;
	u32 exp_data_len, data_direction;
	int ret, prot_bytes, c = 0;
	u16 lun;
	u8 task_attr;
	bool t10_pi = vhost_has_feature(vq, VIRTIO_SCSI_F_T10_PI);
	void *cdb;

	mutex_lock(&vq->mutex);
	/*
	 * We can handle the vq only after the endpoint is setup by calling the
	 * VHOST_SCSI_SET_ENDPOINT ioctl.
	 */
	vs_tpg = vhost_vq_get_backend(vq);
	if (!vs_tpg)
		goto out;

	memset(&vc, 0, sizeof(vc));
	vc.rsp_size = sizeof(struct virtio_scsi_cmd_resp);

	vhost_disable_notify(&vs->dev, vq);

	do {
		ret = vhost_scsi_get_desc(vs, vq, &vc);
		if (ret)
			goto err;

		/*
		 * Setup pointers and values based upon different virtio-scsi
		 * request header if T10_PI is enabled in KVM guest.
		 */
		if (t10_pi) {
			vc.req = &v_req_pi;
			vc.req_size = sizeof(v_req_pi);
			vc.lunp = &v_req_pi.lun[0];
			vc.target = &v_req_pi.lun[1];
		} else {
			vc.req = &v_req;
			vc.req_size = sizeof(v_req);
			vc.lunp = &v_req.lun[0];
			vc.target = &v_req.lun[1];
		}

		/*
		 * Validate the size of request and response buffers.
		 * Check for a sane response buffer so we can report
		 * early errors back to the guest.
		 */
		ret = vhost_scsi_chk_size(vq, &vc);
		if (ret)
			goto err;

		ret = vhost_scsi_get_req(vq, &vc, &tpg);
		if (ret)
			goto err;

		ret = -EIO;	/* bad target on any error from here on */

		/*
		 * Determine data_direction by calculating the total outgoing
		 * iovec sizes + incoming iovec sizes vs. virtio-scsi request +
		 * response headers respectively.
		 *
		 * For DMA_TO_DEVICE this is out_iter, which is already pointing
		 * to the right place.
		 *
		 * For DMA_FROM_DEVICE, the iovec will be just past the end
		 * of the virtio-scsi response header in either the same
		 * or immediately following iovec.
		 *
		 * Any associated T10_PI bytes for the outgoing / incoming
		 * payloads are included in calculation of exp_data_len here.
		 */
		prot_bytes = 0;

		if (vc.out_size > vc.req_size) {
			data_direction = DMA_TO_DEVICE;
			exp_data_len = vc.out_size - vc.req_size;
			data_iter = vc.out_iter;
		} else if (vc.in_size > vc.rsp_size) {
			data_direction = DMA_FROM_DEVICE;
			exp_data_len = vc.in_size - vc.rsp_size;

			iov_iter_init(&in_iter, READ, &vq->iov[vc.out], vc.in,
				      vc.rsp_size + exp_data_len);
			iov_iter_advance(&in_iter, vc.rsp_size);
			data_iter = in_iter;
		} else {
			data_direction = DMA_NONE;
			exp_data_len = 0;
		}
		/*
		 * If T10_PI header + payload is present, setup prot_iter values
		 * and recalculate data_iter for vhost_scsi_mapal() mapping to
		 * host scatterlists via get_user_pages_fast().
		 */
		if (t10_pi) {
			if (v_req_pi.pi_bytesout) {
				if (data_direction != DMA_TO_DEVICE) {
					vq_err(vq, "Received non zero pi_bytesout,"
						" but wrong data_direction\n");
					goto err;
				}
				prot_bytes = vhost32_to_cpu(vq, v_req_pi.pi_bytesout);
			} else if (v_req_pi.pi_bytesin) {
				if (data_direction != DMA_FROM_DEVICE) {
					vq_err(vq, "Received non zero pi_bytesin,"
						" but wrong data_direction\n");
					goto err;
				}
				prot_bytes = vhost32_to_cpu(vq, v_req_pi.pi_bytesin);
			}
			/*
			 * Set prot_iter to data_iter and truncate it to
			 * prot_bytes, and advance data_iter past any
			 * preceeding prot_bytes that may be present.
			 *
			 * Also fix up the exp_data_len to reflect only the
			 * actual data payload length.
			 */
			if (prot_bytes) {
				exp_data_len -= prot_bytes;
				prot_iter = data_iter;
				iov_iter_truncate(&prot_iter, prot_bytes);
				iov_iter_advance(&data_iter, prot_bytes);
			}
			tag = vhost64_to_cpu(vq, v_req_pi.tag);
			task_attr = v_req_pi.task_attr;
			cdb = &v_req_pi.cdb[0];
			lun = vhost_buf_to_lun(v_req_pi.lun);
		} else {
			tag = vhost64_to_cpu(vq, v_req.tag);
			task_attr = v_req.task_attr;
			cdb = &v_req.cdb[0];
			lun = vhost_buf_to_lun(v_req.lun);
		}
		/*
		 * Check that the received CDB size does not exceeded our
		 * hardcoded max for vhost-scsi, then get a pre-allocated
		 * cmd descriptor for the new virtio-scsi tag.
		 *
		 * TODO what if cdb was too small for varlen cdb header?
		 */
		if (unlikely(scsi_command_size(cdb) > VHOST_SCSI_MAX_CDB_SIZE)) {
			vq_err(vq, "Received SCSI CDB with command_size: %d that"
				" exceeds SCSI_MAX_VARLEN_CDB_SIZE: %d\n",
				scsi_command_size(cdb), VHOST_SCSI_MAX_CDB_SIZE);
				goto err;
		}
		cmd = vhost_scsi_get_cmd(vq, tpg, cdb, tag, lun, task_attr,
					 exp_data_len + prot_bytes,
					 data_direction);
		if (IS_ERR(cmd)) {
			vq_err(vq, "vhost_scsi_get_cmd failed %ld\n",
			       PTR_ERR(cmd));
			goto err;
		}
		cmd->tvc_vhost = vs;
		cmd->tvc_vq = vq;
		cmd->tvc_resp_iov = vq->iov[vc.out];
		cmd->tvc_in_iovs = vc.in;

		pr_debug("vhost_scsi got command opcode: %#02x, lun: %d\n",
			 cmd->tvc_cdb[0], cmd->tvc_lun);
		pr_debug("cmd: %p exp_data_len: %d, prot_bytes: %d data_direction:"
			 " %d\n", cmd, exp_data_len, prot_bytes, data_direction);

		if (data_direction != DMA_NONE) {
			if (unlikely(vhost_scsi_mapal(cmd, prot_bytes,
						      &prot_iter, exp_data_len,
						      &data_iter))) {
				vq_err(vq, "Failed to map iov to sgl\n");
				vhost_scsi_release_cmd_res(&cmd->tvc_se_cmd);
				goto err;
			}
		}
		/*
		 * Save the descriptor from vhost_get_vq_desc() to be used to
		 * complete the virtio-scsi request in TCM callback context via
		 * vhost_scsi_queue_data_in() and vhost_scsi_queue_status()
		 */
		cmd->tvc_vq_desc = vc.head;
		/*
		 * Dispatch cmd descriptor for cmwq execution in process
		 * context provided by vhost_scsi_workqueue.  This also ensures
		 * cmd is executed on the same kworker CPU as this vhost
		 * thread to gain positive L2 cache locality effects.
		 */
		INIT_WORK(&cmd->work, vhost_scsi_submission_work);
		queue_work(vhost_scsi_workqueue, &cmd->work);
		ret = 0;
err:
		/*
		 * ENXIO:  No more requests, or read error, wait for next kick
		 * EINVAL: Invalid response buffer, drop the request
		 * EIO:    Respond with bad target
		 * EAGAIN: Pending request
		 */
		if (ret == -ENXIO)
			break;
		else if (ret == -EIO)
			vhost_scsi_send_bad_target(vs, vq, vc.head, vc.out);
	} while (likely(!vhost_exceeds_weight(vq, ++c, 0)));
out:
	mutex_unlock(&vq->mutex);
}

static void
vhost_scsi_send_tmf_resp(struct vhost_scsi *vs, struct vhost_virtqueue *vq,
			 int in_iovs, int vq_desc, struct iovec *resp_iov,
			 int tmf_resp_code)
{
	struct virtio_scsi_ctrl_tmf_resp rsp;
	struct iov_iter iov_iter;
	int ret;

	pr_debug("%s\n", __func__);
	memset(&rsp, 0, sizeof(rsp));
	rsp.response = tmf_resp_code;

	iov_iter_init(&iov_iter, READ, resp_iov, in_iovs, sizeof(rsp));

	ret = copy_to_iter(&rsp, sizeof(rsp), &iov_iter);
	if (likely(ret == sizeof(rsp)))
		vhost_add_used_and_signal(&vs->dev, vq, vq_desc, 0);
	else
		pr_err("Faulted on virtio_scsi_ctrl_tmf_resp\n");
}

static void vhost_scsi_tmf_resp_work(struct vhost_work *work)
{
	struct vhost_scsi_tmf *tmf = container_of(work, struct vhost_scsi_tmf,
						  vwork);
	int resp_code;

	if (tmf->se_cmd.se_tmr_req->response == TMR_FUNCTION_COMPLETE)
		resp_code = VIRTIO_SCSI_S_FUNCTION_SUCCEEDED;
	else
		resp_code = VIRTIO_SCSI_S_FUNCTION_REJECTED;

	vhost_scsi_send_tmf_resp(tmf->vhost, &tmf->svq->vq, tmf->in_iovs,
				 tmf->vq_desc, &tmf->resp_iov, resp_code);
	vhost_scsi_release_tmf_res(tmf);
}

static void
vhost_scsi_handle_tmf(struct vhost_scsi *vs, struct vhost_scsi_tpg *tpg,
		      struct vhost_virtqueue *vq,
		      struct virtio_scsi_ctrl_tmf_req *vtmf,
		      struct vhost_scsi_ctx *vc)
{
	struct vhost_scsi_virtqueue *svq = container_of(vq,
					struct vhost_scsi_virtqueue, vq);
	struct vhost_scsi_tmf *tmf;

	if (vhost32_to_cpu(vq, vtmf->subtype) !=
	    VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET)
		goto send_reject;

	if (!tpg->tpg_nexus || !tpg->tpg_nexus->tvn_se_sess) {
		pr_err("Unable to locate active struct vhost_scsi_nexus for LUN RESET.\n");
		goto send_reject;
	}

	mutex_lock(&tpg->tv_tpg_mutex);
	if (list_empty(&tpg->tmf_queue)) {
		pr_err("Missing reserve TMF. Could not handle LUN RESET.\n");
		mutex_unlock(&tpg->tv_tpg_mutex);
		goto send_reject;
	}

	tmf = list_first_entry(&tpg->tmf_queue, struct vhost_scsi_tmf,
			       queue_entry);
	list_del_init(&tmf->queue_entry);
	mutex_unlock(&tpg->tv_tpg_mutex);

	tmf->tpg = tpg;
	tmf->vhost = vs;
	tmf->svq = svq;
	tmf->resp_iov = vq->iov[vc->out];
	tmf->vq_desc = vc->head;
	tmf->in_iovs = vc->in;
	tmf->inflight = vhost_scsi_get_inflight(vq);

	if (target_submit_tmr(&tmf->se_cmd, tpg->tpg_nexus->tvn_se_sess, NULL,
			      vhost_buf_to_lun(vtmf->lun), NULL,
			      TMR_LUN_RESET, GFP_KERNEL, 0,
			      TARGET_SCF_ACK_KREF) < 0) {
		vhost_scsi_release_tmf_res(tmf);
		goto send_reject;
	}

	return;

send_reject:
	vhost_scsi_send_tmf_resp(vs, vq, vc->in, vc->head, &vq->iov[vc->out],
				 VIRTIO_SCSI_S_FUNCTION_REJECTED);
}

static void
vhost_scsi_send_an_resp(struct vhost_scsi *vs,
			struct vhost_virtqueue *vq,
			struct vhost_scsi_ctx *vc)
{
	struct virtio_scsi_ctrl_an_resp rsp;
	struct iov_iter iov_iter;
	int ret;

	pr_debug("%s\n", __func__);
	memset(&rsp, 0, sizeof(rsp));	/* event_actual = 0 */
	rsp.response = VIRTIO_SCSI_S_OK;

	iov_iter_init(&iov_iter, READ, &vq->iov[vc->out], vc->in, sizeof(rsp));

	ret = copy_to_iter(&rsp, sizeof(rsp), &iov_iter);
	if (likely(ret == sizeof(rsp)))
		vhost_add_used_and_signal(&vs->dev, vq, vc->head, 0);
	else
		pr_err("Faulted on virtio_scsi_ctrl_an_resp\n");
}

static void
vhost_scsi_ctl_handle_vq(struct vhost_scsi *vs, struct vhost_virtqueue *vq)
{
	struct vhost_scsi_tpg *tpg;
	union {
		__virtio32 type;
		struct virtio_scsi_ctrl_an_req an;
		struct virtio_scsi_ctrl_tmf_req tmf;
	} v_req;
	struct vhost_scsi_ctx vc;
	size_t typ_size;
	int ret, c = 0;

	mutex_lock(&vq->mutex);
	/*
	 * We can handle the vq only after the endpoint is setup by calling the
	 * VHOST_SCSI_SET_ENDPOINT ioctl.
	 */
	if (!vhost_vq_get_backend(vq))
		goto out;

	memset(&vc, 0, sizeof(vc));

	vhost_disable_notify(&vs->dev, vq);

	do {
		ret = vhost_scsi_get_desc(vs, vq, &vc);
		if (ret)
			goto err;

		/*
		 * Get the request type first in order to setup
		 * other parameters dependent on the type.
		 */
		vc.req = &v_req.type;
		typ_size = sizeof(v_req.type);

		if (unlikely(!copy_from_iter_full(vc.req, typ_size,
						  &vc.out_iter))) {
			vq_err(vq, "Faulted on copy_from_iter tmf type\n");
			/*
			 * The size of the response buffer depends on the
			 * request type and must be validated against it.
			 * Since the request type is not known, don't send
			 * a response.
			 */
			continue;
		}

		switch (vhost32_to_cpu(vq, v_req.type)) {
		case VIRTIO_SCSI_T_TMF:
			vc.req = &v_req.tmf;
			vc.req_size = sizeof(struct virtio_scsi_ctrl_tmf_req);
			vc.rsp_size = sizeof(struct virtio_scsi_ctrl_tmf_resp);
			vc.lunp = &v_req.tmf.lun[0];
			vc.target = &v_req.tmf.lun[1];
			break;
		case VIRTIO_SCSI_T_AN_QUERY:
		case VIRTIO_SCSI_T_AN_SUBSCRIBE:
			vc.req = &v_req.an;
			vc.req_size = sizeof(struct virtio_scsi_ctrl_an_req);
			vc.rsp_size = sizeof(struct virtio_scsi_ctrl_an_resp);
			vc.lunp = &v_req.an.lun[0];
			vc.target = NULL;
			break;
		default:
			vq_err(vq, "Unknown control request %d", v_req.type);
			continue;
		}

		/*
		 * Validate the size of request and response buffers.
		 * Check for a sane response buffer so we can report
		 * early errors back to the guest.
		 */
		ret = vhost_scsi_chk_size(vq, &vc);
		if (ret)
			goto err;

		/*
		 * Get the rest of the request now that its size is known.
		 */
		vc.req += typ_size;
		vc.req_size -= typ_size;

		ret = vhost_scsi_get_req(vq, &vc, &tpg);
		if (ret)
			goto err;

		if (v_req.type == VIRTIO_SCSI_T_TMF)
			vhost_scsi_handle_tmf(vs, tpg, vq, &v_req.tmf, &vc);
		else
			vhost_scsi_send_an_resp(vs, vq, &vc);
err:
		/*
		 * ENXIO:  No more requests, or read error, wait for next kick
		 * EINVAL: Invalid response buffer, drop the request
		 * EIO:    Respond with bad target
		 * EAGAIN: Pending request
		 */
		if (ret == -ENXIO)
			break;
		else if (ret == -EIO)
			vhost_scsi_send_bad_target(vs, vq, vc.head, vc.out);
	} while (likely(!vhost_exceeds_weight(vq, ++c, 0)));
out:
	mutex_unlock(&vq->mutex);
}

static void vhost_scsi_ctl_handle_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						poll.work);
	struct vhost_scsi *vs = container_of(vq->dev, struct vhost_scsi, dev);

	pr_debug("%s: The handling func for control queue.\n", __func__);
	vhost_scsi_ctl_handle_vq(vs, vq);
}

static void
vhost_scsi_send_evt(struct vhost_scsi *vs,
		   struct vhost_scsi_tpg *tpg,
		   struct se_lun *lun,
		   u32 event,
		   u32 reason)
{
	struct vhost_scsi_evt *evt;

	evt = vhost_scsi_allocate_evt(vs, event, reason);
	if (!evt)
		return;

	if (tpg && lun) {
		/* TODO: share lun setup code with virtio-scsi.ko */
		/*
		 * Note: evt->event is zeroed when we allocate it and
		 * lun[4-7] need to be zero according to virtio-scsi spec.
		 */
		evt->event.lun[0] = 0x01;
		evt->event.lun[1] = tpg->tport_tpgt;
		if (lun->unpacked_lun >= 256)
			evt->event.lun[2] = lun->unpacked_lun >> 8 | 0x40 ;
		evt->event.lun[3] = lun->unpacked_lun & 0xFF;
	}

	llist_add(&evt->list, &vs->vs_event_list);
	vhost_work_queue(&vs->dev, &vs->vs_event_work);
}

static void vhost_scsi_evt_handle_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						poll.work);
	struct vhost_scsi *vs = container_of(vq->dev, struct vhost_scsi, dev);

	mutex_lock(&vq->mutex);
	if (!vhost_vq_get_backend(vq))
		goto out;

	if (vs->vs_events_missed)
		vhost_scsi_send_evt(vs, NULL, NULL, VIRTIO_SCSI_T_NO_EVENT, 0);
out:
	mutex_unlock(&vq->mutex);
}

static void vhost_scsi_handle_kick(struct vhost_work *work)
{
	struct vhost_virtqueue *vq = container_of(work, struct vhost_virtqueue,
						poll.work);
	struct vhost_scsi *vs = container_of(vq->dev, struct vhost_scsi, dev);

	vhost_scsi_handle_vq(vs, vq);
}

static void vhost_scsi_flush_vq(struct vhost_scsi *vs, int index)
{
	vhost_poll_flush(&vs->vqs[index].vq.poll);
}

/* Callers must hold dev mutex */
static void vhost_scsi_flush(struct vhost_scsi *vs)
{
	struct vhost_scsi_inflight *old_inflight[VHOST_SCSI_MAX_VQ];
	int i;

	/* Init new inflight and remember the old inflight */
	vhost_scsi_init_inflight(vs, old_inflight);

	/*
	 * The inflight->kref was initialized to 1. We decrement it here to
	 * indicate the start of the flush operation so that it will reach 0
	 * when all the reqs are finished.
	 */
	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		kref_put(&old_inflight[i]->kref, vhost_scsi_done_inflight);

	/* Flush both the vhost poll and vhost work */
	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		vhost_scsi_flush_vq(vs, i);
	vhost_work_flush(&vs->dev, &vs->vs_completion_work);
	vhost_work_flush(&vs->dev, &vs->vs_event_work);

	/* Wait for all reqs issued before the flush to be finished */
	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		wait_for_completion(&old_inflight[i]->comp);
}

static void vhost_scsi_destroy_vq_cmds(struct vhost_virtqueue *vq)
{
	struct vhost_scsi_virtqueue *svq = container_of(vq,
					struct vhost_scsi_virtqueue, vq);
	struct vhost_scsi_cmd *tv_cmd;
	unsigned int i;

	if (!svq->scsi_cmds)
		return;

	for (i = 0; i < svq->max_cmds; i++) {
		tv_cmd = &svq->scsi_cmds[i];

		kfree(tv_cmd->tvc_sgl);
		kfree(tv_cmd->tvc_prot_sgl);
		kfree(tv_cmd->tvc_upages);
	}

	sbitmap_free(&svq->scsi_tags);
	kfree(svq->scsi_cmds);
	svq->scsi_cmds = NULL;
}

static int vhost_scsi_setup_vq_cmds(struct vhost_virtqueue *vq, int max_cmds)
{
	struct vhost_scsi_virtqueue *svq = container_of(vq,
					struct vhost_scsi_virtqueue, vq);
	struct vhost_scsi_cmd *tv_cmd;
	unsigned int i;

	if (svq->scsi_cmds)
		return 0;

	if (sbitmap_init_node(&svq->scsi_tags, max_cmds, -1, GFP_KERNEL,
			      NUMA_NO_NODE))
		return -ENOMEM;
	svq->max_cmds = max_cmds;

	svq->scsi_cmds = kcalloc(max_cmds, sizeof(*tv_cmd), GFP_KERNEL);
	if (!svq->scsi_cmds) {
		sbitmap_free(&svq->scsi_tags);
		return -ENOMEM;
	}

	for (i = 0; i < max_cmds; i++) {
		tv_cmd = &svq->scsi_cmds[i];

		tv_cmd->tvc_sgl = kcalloc(VHOST_SCSI_PREALLOC_SGLS,
					  sizeof(struct scatterlist),
					  GFP_KERNEL);
		if (!tv_cmd->tvc_sgl) {
			pr_err("Unable to allocate tv_cmd->tvc_sgl\n");
			goto out;
		}

		tv_cmd->tvc_upages = kcalloc(VHOST_SCSI_PREALLOC_UPAGES,
					     sizeof(struct page *),
					     GFP_KERNEL);
		if (!tv_cmd->tvc_upages) {
			pr_err("Unable to allocate tv_cmd->tvc_upages\n");
			goto out;
		}

		tv_cmd->tvc_prot_sgl = kcalloc(VHOST_SCSI_PREALLOC_PROT_SGLS,
					       sizeof(struct scatterlist),
					       GFP_KERNEL);
		if (!tv_cmd->tvc_prot_sgl) {
			pr_err("Unable to allocate tv_cmd->tvc_prot_sgl\n");
			goto out;
		}
	}
	return 0;
out:
	vhost_scsi_destroy_vq_cmds(vq);
	return -ENOMEM;
}

/*
 * Called from vhost_scsi_ioctl() context to walk the list of available
 * vhost_scsi_tpg with an active struct vhost_scsi_nexus
 *
 *  The lock nesting rule is:
 *    vhost_scsi_mutex -> vs->dev.mutex -> tpg->tv_tpg_mutex -> vq->mutex
 */
static int
vhost_scsi_set_endpoint(struct vhost_scsi *vs,
			struct vhost_scsi_target *t)
{
	struct se_portal_group *se_tpg;
	struct vhost_scsi_tport *tv_tport;
	struct vhost_scsi_tpg *tpg;
	struct vhost_scsi_tpg **vs_tpg;
	struct vhost_virtqueue *vq;
	int index, ret, i, len;
	bool match = false;

	mutex_lock(&vhost_scsi_mutex);
	mutex_lock(&vs->dev.mutex);

	/* Verify that ring has been setup correctly. */
	for (index = 0; index < vs->dev.nvqs; ++index) {
		/* Verify that ring has been setup correctly. */
		if (!vhost_vq_access_ok(&vs->vqs[index].vq)) {
			ret = -EFAULT;
			goto out;
		}
	}

	len = sizeof(vs_tpg[0]) * VHOST_SCSI_MAX_TARGET;
	vs_tpg = kzalloc(len, GFP_KERNEL);
	if (!vs_tpg) {
		ret = -ENOMEM;
		goto out;
	}
	if (vs->vs_tpg)
		memcpy(vs_tpg, vs->vs_tpg, len);

	list_for_each_entry(tpg, &vhost_scsi_list, tv_tpg_list) {
		mutex_lock(&tpg->tv_tpg_mutex);
		if (!tpg->tpg_nexus) {
			mutex_unlock(&tpg->tv_tpg_mutex);
			continue;
		}
		if (tpg->tv_tpg_vhost_count != 0) {
			mutex_unlock(&tpg->tv_tpg_mutex);
			continue;
		}
		tv_tport = tpg->tport;

		if (!strcmp(tv_tport->tport_name, t->vhost_wwpn)) {
			if (vs->vs_tpg && vs->vs_tpg[tpg->tport_tpgt]) {
				mutex_unlock(&tpg->tv_tpg_mutex);
				ret = -EEXIST;
				goto undepend;
			}
			/*
			 * In order to ensure individual vhost-scsi configfs
			 * groups cannot be removed while in use by vhost ioctl,
			 * go ahead and take an explicit se_tpg->tpg_group.cg_item
			 * dependency now.
			 */
			se_tpg = &tpg->se_tpg;
			ret = target_depend_item(&se_tpg->tpg_group.cg_item);
			if (ret) {
				pr_warn("target_depend_item() failed: %d\n", ret);
				mutex_unlock(&tpg->tv_tpg_mutex);
				goto undepend;
			}
			tpg->tv_tpg_vhost_count++;
			tpg->vhost_scsi = vs;
			vs_tpg[tpg->tport_tpgt] = tpg;
			match = true;
		}
		mutex_unlock(&tpg->tv_tpg_mutex);
	}

	if (match) {
		memcpy(vs->vs_vhost_wwpn, t->vhost_wwpn,
		       sizeof(vs->vs_vhost_wwpn));

		for (i = VHOST_SCSI_VQ_IO; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i].vq;
			if (!vhost_vq_is_setup(vq))
				continue;

			if (vhost_scsi_setup_vq_cmds(vq, vq->num))
				goto destroy_vq_cmds;
		}

		for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i].vq;
			mutex_lock(&vq->mutex);
			vhost_vq_set_backend(vq, vs_tpg);
			vhost_vq_init_access(vq);
			mutex_unlock(&vq->mutex);
		}
		ret = 0;
	} else {
		ret = -EEXIST;
	}

	/*
	 * Act as synchronize_rcu to make sure access to
	 * old vs->vs_tpg is finished.
	 */
	vhost_scsi_flush(vs);
	kfree(vs->vs_tpg);
	vs->vs_tpg = vs_tpg;
	goto out;

destroy_vq_cmds:
	for (i--; i >= VHOST_SCSI_VQ_IO; i--) {
		if (!vhost_vq_get_backend(&vs->vqs[i].vq))
			vhost_scsi_destroy_vq_cmds(&vs->vqs[i].vq);
	}
undepend:
	for (i = 0; i < VHOST_SCSI_MAX_TARGET; i++) {
		tpg = vs_tpg[i];
		if (tpg) {
			tpg->tv_tpg_vhost_count--;
			target_undepend_item(&tpg->se_tpg.tpg_group.cg_item);
		}
	}
	kfree(vs_tpg);
out:
	mutex_unlock(&vs->dev.mutex);
	mutex_unlock(&vhost_scsi_mutex);
	return ret;
}

static int
vhost_scsi_clear_endpoint(struct vhost_scsi *vs,
			  struct vhost_scsi_target *t)
{
	struct se_portal_group *se_tpg;
	struct vhost_scsi_tport *tv_tport;
	struct vhost_scsi_tpg *tpg;
	struct vhost_virtqueue *vq;
	bool match = false;
	int index, ret, i;
	u8 target;

	mutex_lock(&vhost_scsi_mutex);
	mutex_lock(&vs->dev.mutex);
	/* Verify that ring has been setup correctly. */
	for (index = 0; index < vs->dev.nvqs; ++index) {
		if (!vhost_vq_access_ok(&vs->vqs[index].vq)) {
			ret = -EFAULT;
			goto err_dev;
		}
	}

	if (!vs->vs_tpg) {
		ret = 0;
		goto err_dev;
	}

	for (i = 0; i < VHOST_SCSI_MAX_TARGET; i++) {
		target = i;
		tpg = vs->vs_tpg[target];
		if (!tpg)
			continue;

		mutex_lock(&tpg->tv_tpg_mutex);
		tv_tport = tpg->tport;
		if (!tv_tport) {
			ret = -ENODEV;
			goto err_tpg;
		}

		if (strcmp(tv_tport->tport_name, t->vhost_wwpn)) {
			pr_warn("tv_tport->tport_name: %s, tpg->tport_tpgt: %hu"
				" does not match t->vhost_wwpn: %s, t->vhost_tpgt: %hu\n",
				tv_tport->tport_name, tpg->tport_tpgt,
				t->vhost_wwpn, t->vhost_tpgt);
			ret = -EINVAL;
			goto err_tpg;
		}
		tpg->tv_tpg_vhost_count--;
		tpg->vhost_scsi = NULL;
		vs->vs_tpg[target] = NULL;
		match = true;
		mutex_unlock(&tpg->tv_tpg_mutex);
		/*
		 * Release se_tpg->tpg_group.cg_item configfs dependency now
		 * to allow vhost-scsi WWPN se_tpg->tpg_group shutdown to occur.
		 */
		se_tpg = &tpg->se_tpg;
		target_undepend_item(&se_tpg->tpg_group.cg_item);
	}
	if (match) {
		for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i].vq;
			mutex_lock(&vq->mutex);
			vhost_vq_set_backend(vq, NULL);
			mutex_unlock(&vq->mutex);
			/*
			 * Make sure cmds are not running before tearing them
			 * down.
			 */
			vhost_scsi_flush(vs);
			vhost_scsi_destroy_vq_cmds(vq);
		}
	}
	/*
	 * Act as synchronize_rcu to make sure access to
	 * old vs->vs_tpg is finished.
	 */
	vhost_scsi_flush(vs);
	kfree(vs->vs_tpg);
	vs->vs_tpg = NULL;
	WARN_ON(vs->vs_events_nr);
	mutex_unlock(&vs->dev.mutex);
	mutex_unlock(&vhost_scsi_mutex);
	return 0;

err_tpg:
	mutex_unlock(&tpg->tv_tpg_mutex);
err_dev:
	mutex_unlock(&vs->dev.mutex);
	mutex_unlock(&vhost_scsi_mutex);
	return ret;
}

static int vhost_scsi_set_features(struct vhost_scsi *vs, u64 features)
{
	struct vhost_virtqueue *vq;
	int i;

	if (features & ~VHOST_SCSI_FEATURES)
		return -EOPNOTSUPP;

	mutex_lock(&vs->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&vs->dev)) {
		mutex_unlock(&vs->dev.mutex);
		return -EFAULT;
	}

	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
		vq = &vs->vqs[i].vq;
		mutex_lock(&vq->mutex);
		vq->acked_features = features;
		mutex_unlock(&vq->mutex);
	}
	mutex_unlock(&vs->dev.mutex);
	return 0;
}

static int vhost_scsi_open(struct inode *inode, struct file *f)
{
	struct vhost_scsi *vs;
	struct vhost_virtqueue **vqs;
	int r = -ENOMEM, i;

	vs = kzalloc(sizeof(*vs), GFP_KERNEL | __GFP_NOWARN | __GFP_RETRY_MAYFAIL);
	if (!vs) {
		vs = vzalloc(sizeof(*vs));
		if (!vs)
			goto err_vs;
	}

	vqs = kmalloc_array(VHOST_SCSI_MAX_VQ, sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vqs;

	vhost_work_init(&vs->vs_completion_work, vhost_scsi_complete_cmd_work);
	vhost_work_init(&vs->vs_event_work, vhost_scsi_evt_work);

	vs->vs_events_nr = 0;
	vs->vs_events_missed = false;

	vqs[VHOST_SCSI_VQ_CTL] = &vs->vqs[VHOST_SCSI_VQ_CTL].vq;
	vqs[VHOST_SCSI_VQ_EVT] = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
	vs->vqs[VHOST_SCSI_VQ_CTL].vq.handle_kick = vhost_scsi_ctl_handle_kick;
	vs->vqs[VHOST_SCSI_VQ_EVT].vq.handle_kick = vhost_scsi_evt_handle_kick;
	for (i = VHOST_SCSI_VQ_IO; i < VHOST_SCSI_MAX_VQ; i++) {
		vqs[i] = &vs->vqs[i].vq;
		vs->vqs[i].vq.handle_kick = vhost_scsi_handle_kick;
	}
	vhost_dev_init(&vs->dev, vqs, VHOST_SCSI_MAX_VQ, UIO_MAXIOV,
		       VHOST_SCSI_WEIGHT, 0, true, NULL);

	vhost_scsi_init_inflight(vs, NULL);

	f->private_data = vs;
	return 0;

err_vqs:
	kvfree(vs);
err_vs:
	return r;
}

static int vhost_scsi_release(struct inode *inode, struct file *f)
{
	struct vhost_scsi *vs = f->private_data;
	struct vhost_scsi_target t;

	mutex_lock(&vs->dev.mutex);
	memcpy(t.vhost_wwpn, vs->vs_vhost_wwpn, sizeof(t.vhost_wwpn));
	mutex_unlock(&vs->dev.mutex);
	vhost_scsi_clear_endpoint(vs, &t);
	vhost_dev_stop(&vs->dev);
	vhost_dev_cleanup(&vs->dev);
	/* Jobs can re-queue themselves in evt kick handler. Do extra flush. */
	vhost_scsi_flush(vs);
	kfree(vs->dev.vqs);
	kvfree(vs);
	return 0;
}

static long
vhost_scsi_ioctl(struct file *f,
		 unsigned int ioctl,
		 unsigned long arg)
{
	struct vhost_scsi *vs = f->private_data;
	struct vhost_scsi_target backend;
	void __user *argp = (void __user *)arg;
	u64 __user *featurep = argp;
	u32 __user *eventsp = argp;
	u32 events_missed;
	u64 features;
	int r, abi_version = VHOST_SCSI_ABI_VERSION;
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;

	switch (ioctl) {
	case VHOST_SCSI_SET_ENDPOINT:
		if (copy_from_user(&backend, argp, sizeof backend))
			return -EFAULT;
		if (backend.reserved != 0)
			return -EOPNOTSUPP;

		return vhost_scsi_set_endpoint(vs, &backend);
	case VHOST_SCSI_CLEAR_ENDPOINT:
		if (copy_from_user(&backend, argp, sizeof backend))
			return -EFAULT;
		if (backend.reserved != 0)
			return -EOPNOTSUPP;

		return vhost_scsi_clear_endpoint(vs, &backend);
	case VHOST_SCSI_GET_ABI_VERSION:
		if (copy_to_user(argp, &abi_version, sizeof abi_version))
			return -EFAULT;
		return 0;
	case VHOST_SCSI_SET_EVENTS_MISSED:
		if (get_user(events_missed, eventsp))
			return -EFAULT;
		mutex_lock(&vq->mutex);
		vs->vs_events_missed = events_missed;
		mutex_unlock(&vq->mutex);
		return 0;
	case VHOST_SCSI_GET_EVENTS_MISSED:
		mutex_lock(&vq->mutex);
		events_missed = vs->vs_events_missed;
		mutex_unlock(&vq->mutex);
		if (put_user(events_missed, eventsp))
			return -EFAULT;
		return 0;
	case VHOST_GET_FEATURES:
		features = VHOST_SCSI_FEATURES;
		if (copy_to_user(featurep, &features, sizeof features))
			return -EFAULT;
		return 0;
	case VHOST_SET_FEATURES:
		if (copy_from_user(&features, featurep, sizeof features))
			return -EFAULT;
		return vhost_scsi_set_features(vs, features);
	default:
		mutex_lock(&vs->dev.mutex);
		r = vhost_dev_ioctl(&vs->dev, ioctl, argp);
		/* TODO: flush backend after dev ioctl. */
		if (r == -ENOIOCTLCMD)
			r = vhost_vring_ioctl(&vs->dev, ioctl, argp);
		mutex_unlock(&vs->dev.mutex);
		return r;
	}
}

static const struct file_operations vhost_scsi_fops = {
	.owner          = THIS_MODULE,
	.release        = vhost_scsi_release,
	.unlocked_ioctl = vhost_scsi_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open           = vhost_scsi_open,
	.llseek		= noop_llseek,
};

static struct miscdevice vhost_scsi_misc = {
	MISC_DYNAMIC_MINOR,
	"vhost-scsi",
	&vhost_scsi_fops,
};

static int __init vhost_scsi_register(void)
{
	return misc_register(&vhost_scsi_misc);
}

static void vhost_scsi_deregister(void)
{
	misc_deregister(&vhost_scsi_misc);
}

static char *vhost_scsi_dump_proto_id(struct vhost_scsi_tport *tport)
{
	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return "SAS";
	case SCSI_PROTOCOL_FCP:
		return "FCP";
	case SCSI_PROTOCOL_ISCSI:
		return "iSCSI";
	default:
		break;
	}

	return "Unknown";
}

static void
vhost_scsi_do_plug(struct vhost_scsi_tpg *tpg,
		  struct se_lun *lun, bool plug)
{

	struct vhost_scsi *vs = tpg->vhost_scsi;
	struct vhost_virtqueue *vq;
	u32 reason;

	if (!vs)
		return;

	mutex_lock(&vs->dev.mutex);

	if (plug)
		reason = VIRTIO_SCSI_EVT_RESET_RESCAN;
	else
		reason = VIRTIO_SCSI_EVT_RESET_REMOVED;

	vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
	mutex_lock(&vq->mutex);
	if (vhost_has_feature(vq, VIRTIO_SCSI_F_HOTPLUG))
		vhost_scsi_send_evt(vs, tpg, lun,
				   VIRTIO_SCSI_T_TRANSPORT_RESET, reason);
	mutex_unlock(&vq->mutex);
	mutex_unlock(&vs->dev.mutex);
}

static void vhost_scsi_hotplug(struct vhost_scsi_tpg *tpg, struct se_lun *lun)
{
	vhost_scsi_do_plug(tpg, lun, true);
}

static void vhost_scsi_hotunplug(struct vhost_scsi_tpg *tpg, struct se_lun *lun)
{
	vhost_scsi_do_plug(tpg, lun, false);
}

static int vhost_scsi_port_link(struct se_portal_group *se_tpg,
			       struct se_lun *lun)
{
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	struct vhost_scsi_tmf *tmf;

	tmf = kzalloc(sizeof(*tmf), GFP_KERNEL);
	if (!tmf)
		return -ENOMEM;
	INIT_LIST_HEAD(&tmf->queue_entry);
	vhost_work_init(&tmf->vwork, vhost_scsi_tmf_resp_work);

	mutex_lock(&vhost_scsi_mutex);

	mutex_lock(&tpg->tv_tpg_mutex);
	tpg->tv_tpg_port_count++;
	list_add_tail(&tmf->queue_entry, &tpg->tmf_queue);
	mutex_unlock(&tpg->tv_tpg_mutex);

	vhost_scsi_hotplug(tpg, lun);

	mutex_unlock(&vhost_scsi_mutex);

	return 0;
}

static void vhost_scsi_port_unlink(struct se_portal_group *se_tpg,
				  struct se_lun *lun)
{
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	struct vhost_scsi_tmf *tmf;

	mutex_lock(&vhost_scsi_mutex);

	mutex_lock(&tpg->tv_tpg_mutex);
	tpg->tv_tpg_port_count--;
	tmf = list_first_entry(&tpg->tmf_queue, struct vhost_scsi_tmf,
			       queue_entry);
	list_del(&tmf->queue_entry);
	kfree(tmf);
	mutex_unlock(&tpg->tv_tpg_mutex);

	vhost_scsi_hotunplug(tpg, lun);

	mutex_unlock(&vhost_scsi_mutex);
}

static ssize_t vhost_scsi_tpg_attrib_fabric_prot_type_store(
		struct config_item *item, const char *page, size_t count)
{
	struct se_portal_group *se_tpg = attrib_to_tpg(item);
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	unsigned long val;
	int ret = kstrtoul(page, 0, &val);

	if (ret) {
		pr_err("kstrtoul() returned %d for fabric_prot_type\n", ret);
		return ret;
	}
	if (val != 0 && val != 1 && val != 3) {
		pr_err("Invalid vhost_scsi fabric_prot_type: %lu\n", val);
		return -EINVAL;
	}
	tpg->tv_fabric_prot_type = val;

	return count;
}

static ssize_t vhost_scsi_tpg_attrib_fabric_prot_type_show(
		struct config_item *item, char *page)
{
	struct se_portal_group *se_tpg = attrib_to_tpg(item);
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);

	return sprintf(page, "%d\n", tpg->tv_fabric_prot_type);
}

CONFIGFS_ATTR(vhost_scsi_tpg_attrib_, fabric_prot_type);

static struct configfs_attribute *vhost_scsi_tpg_attrib_attrs[] = {
	&vhost_scsi_tpg_attrib_attr_fabric_prot_type,
	NULL,
};

static int vhost_scsi_make_nexus(struct vhost_scsi_tpg *tpg,
				const char *name)
{
	struct vhost_scsi_nexus *tv_nexus;

	mutex_lock(&tpg->tv_tpg_mutex);
	if (tpg->tpg_nexus) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_debug("tpg->tpg_nexus already exists\n");
		return -EEXIST;
	}

	tv_nexus = kzalloc(sizeof(*tv_nexus), GFP_KERNEL);
	if (!tv_nexus) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_err("Unable to allocate struct vhost_scsi_nexus\n");
		return -ENOMEM;
	}
	/*
	 * Since we are running in 'demo mode' this call with generate a
	 * struct se_node_acl for the vhost_scsi struct se_portal_group with
	 * the SCSI Initiator port name of the passed configfs group 'name'.
	 */
	tv_nexus->tvn_se_sess = target_setup_session(&tpg->se_tpg, 0, 0,
					TARGET_PROT_DIN_PASS | TARGET_PROT_DOUT_PASS,
					(unsigned char *)name, tv_nexus, NULL);
	if (IS_ERR(tv_nexus->tvn_se_sess)) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		kfree(tv_nexus);
		return -ENOMEM;
	}
	tpg->tpg_nexus = tv_nexus;

	mutex_unlock(&tpg->tv_tpg_mutex);
	return 0;
}

static int vhost_scsi_drop_nexus(struct vhost_scsi_tpg *tpg)
{
	struct se_session *se_sess;
	struct vhost_scsi_nexus *tv_nexus;

	mutex_lock(&tpg->tv_tpg_mutex);
	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		return -ENODEV;
	}

	se_sess = tv_nexus->tvn_se_sess;
	if (!se_sess) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		return -ENODEV;
	}

	if (tpg->tv_tpg_port_count != 0) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_err("Unable to remove TCM_vhost I_T Nexus with"
			" active TPG port count: %d\n",
			tpg->tv_tpg_port_count);
		return -EBUSY;
	}

	if (tpg->tv_tpg_vhost_count != 0) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_err("Unable to remove TCM_vhost I_T Nexus with"
			" active TPG vhost count: %d\n",
			tpg->tv_tpg_vhost_count);
		return -EBUSY;
	}

	pr_debug("TCM_vhost_ConfigFS: Removing I_T Nexus to emulated"
		" %s Initiator Port: %s\n", vhost_scsi_dump_proto_id(tpg->tport),
		tv_nexus->tvn_se_sess->se_node_acl->initiatorname);

	/*
	 * Release the SCSI I_T Nexus to the emulated vhost Target Port
	 */
	target_remove_session(se_sess);
	tpg->tpg_nexus = NULL;
	mutex_unlock(&tpg->tv_tpg_mutex);

	kfree(tv_nexus);
	return 0;
}

static ssize_t vhost_scsi_tpg_nexus_show(struct config_item *item, char *page)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	struct vhost_scsi_nexus *tv_nexus;
	ssize_t ret;

	mutex_lock(&tpg->tv_tpg_mutex);
	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		return -ENODEV;
	}
	ret = snprintf(page, PAGE_SIZE, "%s\n",
			tv_nexus->tvn_se_sess->se_node_acl->initiatorname);
	mutex_unlock(&tpg->tv_tpg_mutex);

	return ret;
}

static ssize_t vhost_scsi_tpg_nexus_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);
	struct vhost_scsi_tport *tport_wwn = tpg->tport;
	unsigned char i_port[VHOST_SCSI_NAMELEN], *ptr, *port_ptr;
	int ret;
	/*
	 * Shutdown the active I_T nexus if 'NULL' is passed..
	 */
	if (!strncmp(page, "NULL", 4)) {
		ret = vhost_scsi_drop_nexus(tpg);
		return (!ret) ? count : ret;
	}
	/*
	 * Otherwise make sure the passed virtual Initiator port WWN matches
	 * the fabric protocol_id set in vhost_scsi_make_tport(), and call
	 * vhost_scsi_make_nexus().
	 */
	if (strlen(page) >= VHOST_SCSI_NAMELEN) {
		pr_err("Emulated NAA Sas Address: %s, exceeds"
				" max: %d\n", page, VHOST_SCSI_NAMELEN);
		return -EINVAL;
	}
	snprintf(&i_port[0], VHOST_SCSI_NAMELEN, "%s", page);

	ptr = strstr(i_port, "naa.");
	if (ptr) {
		if (tport_wwn->tport_proto_id != SCSI_PROTOCOL_SAS) {
			pr_err("Passed SAS Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				vhost_scsi_dump_proto_id(tport_wwn));
			return -EINVAL;
		}
		port_ptr = &i_port[0];
		goto check_newline;
	}
	ptr = strstr(i_port, "fc.");
	if (ptr) {
		if (tport_wwn->tport_proto_id != SCSI_PROTOCOL_FCP) {
			pr_err("Passed FCP Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				vhost_scsi_dump_proto_id(tport_wwn));
			return -EINVAL;
		}
		port_ptr = &i_port[3]; /* Skip over "fc." */
		goto check_newline;
	}
	ptr = strstr(i_port, "iqn.");
	if (ptr) {
		if (tport_wwn->tport_proto_id != SCSI_PROTOCOL_ISCSI) {
			pr_err("Passed iSCSI Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				vhost_scsi_dump_proto_id(tport_wwn));
			return -EINVAL;
		}
		port_ptr = &i_port[0];
		goto check_newline;
	}
	pr_err("Unable to locate prefix for emulated Initiator Port:"
			" %s\n", i_port);
	return -EINVAL;
	/*
	 * Clear any trailing newline for the NAA WWN
	 */
check_newline:
	if (i_port[strlen(i_port)-1] == '\n')
		i_port[strlen(i_port)-1] = '\0';

	ret = vhost_scsi_make_nexus(tpg, port_ptr);
	if (ret < 0)
		return ret;

	return count;
}

CONFIGFS_ATTR(vhost_scsi_tpg_, nexus);

static struct configfs_attribute *vhost_scsi_tpg_attrs[] = {
	&vhost_scsi_tpg_attr_nexus,
	NULL,
};

static struct se_portal_group *
vhost_scsi_make_tpg(struct se_wwn *wwn, const char *name)
{
	struct vhost_scsi_tport *tport = container_of(wwn,
			struct vhost_scsi_tport, tport_wwn);

	struct vhost_scsi_tpg *tpg;
	u16 tpgt;
	int ret;

	if (strstr(name, "tpgt_") != name)
		return ERR_PTR(-EINVAL);
	if (kstrtou16(name + 5, 10, &tpgt) || tpgt >= VHOST_SCSI_MAX_TARGET)
		return ERR_PTR(-EINVAL);

	tpg = kzalloc(sizeof(*tpg), GFP_KERNEL);
	if (!tpg) {
		pr_err("Unable to allocate struct vhost_scsi_tpg");
		return ERR_PTR(-ENOMEM);
	}
	mutex_init(&tpg->tv_tpg_mutex);
	INIT_LIST_HEAD(&tpg->tv_tpg_list);
	INIT_LIST_HEAD(&tpg->tmf_queue);
	tpg->tport = tport;
	tpg->tport_tpgt = tpgt;

	ret = core_tpg_register(wwn, &tpg->se_tpg, tport->tport_proto_id);
	if (ret < 0) {
		kfree(tpg);
		return NULL;
	}
	mutex_lock(&vhost_scsi_mutex);
	list_add_tail(&tpg->tv_tpg_list, &vhost_scsi_list);
	mutex_unlock(&vhost_scsi_mutex);

	return &tpg->se_tpg;
}

static void vhost_scsi_drop_tpg(struct se_portal_group *se_tpg)
{
	struct vhost_scsi_tpg *tpg = container_of(se_tpg,
				struct vhost_scsi_tpg, se_tpg);

	mutex_lock(&vhost_scsi_mutex);
	list_del(&tpg->tv_tpg_list);
	mutex_unlock(&vhost_scsi_mutex);
	/*
	 * Release the virtual I_T Nexus for this vhost TPG
	 */
	vhost_scsi_drop_nexus(tpg);
	/*
	 * Deregister the se_tpg from TCM..
	 */
	core_tpg_deregister(se_tpg);
	kfree(tpg);
}

static struct se_wwn *
vhost_scsi_make_tport(struct target_fabric_configfs *tf,
		     struct config_group *group,
		     const char *name)
{
	struct vhost_scsi_tport *tport;
	char *ptr;
	u64 wwpn = 0;
	int off = 0;

	/* if (vhost_scsi_parse_wwn(name, &wwpn, 1) < 0)
		return ERR_PTR(-EINVAL); */

	tport = kzalloc(sizeof(*tport), GFP_KERNEL);
	if (!tport) {
		pr_err("Unable to allocate struct vhost_scsi_tport");
		return ERR_PTR(-ENOMEM);
	}
	tport->tport_wwpn = wwpn;
	/*
	 * Determine the emulated Protocol Identifier and Target Port Name
	 * based on the incoming configfs directory name.
	 */
	ptr = strstr(name, "naa.");
	if (ptr) {
		tport->tport_proto_id = SCSI_PROTOCOL_SAS;
		goto check_len;
	}
	ptr = strstr(name, "fc.");
	if (ptr) {
		tport->tport_proto_id = SCSI_PROTOCOL_FCP;
		off = 3; /* Skip over "fc." */
		goto check_len;
	}
	ptr = strstr(name, "iqn.");
	if (ptr) {
		tport->tport_proto_id = SCSI_PROTOCOL_ISCSI;
		goto check_len;
	}

	pr_err("Unable to locate prefix for emulated Target Port:"
			" %s\n", name);
	kfree(tport);
	return ERR_PTR(-EINVAL);

check_len:
	if (strlen(name) >= VHOST_SCSI_NAMELEN) {
		pr_err("Emulated %s Address: %s, exceeds"
			" max: %d\n", name, vhost_scsi_dump_proto_id(tport),
			VHOST_SCSI_NAMELEN);
		kfree(tport);
		return ERR_PTR(-EINVAL);
	}
	snprintf(&tport->tport_name[0], VHOST_SCSI_NAMELEN, "%s", &name[off]);

	pr_debug("TCM_VHost_ConfigFS: Allocated emulated Target"
		" %s Address: %s\n", vhost_scsi_dump_proto_id(tport), name);

	return &tport->tport_wwn;
}

static void vhost_scsi_drop_tport(struct se_wwn *wwn)
{
	struct vhost_scsi_tport *tport = container_of(wwn,
				struct vhost_scsi_tport, tport_wwn);

	pr_debug("TCM_VHost_ConfigFS: Deallocating emulated Target"
		" %s Address: %s\n", vhost_scsi_dump_proto_id(tport),
		tport->tport_name);

	kfree(tport);
}

static ssize_t
vhost_scsi_wwn_version_show(struct config_item *item, char *page)
{
	return sprintf(page, "TCM_VHOST fabric module %s on %s/%s"
		"on "UTS_RELEASE"\n", VHOST_SCSI_VERSION, utsname()->sysname,
		utsname()->machine);
}

CONFIGFS_ATTR_RO(vhost_scsi_wwn_, version);

static struct configfs_attribute *vhost_scsi_wwn_attrs[] = {
	&vhost_scsi_wwn_attr_version,
	NULL,
};

static const struct target_core_fabric_ops vhost_scsi_ops = {
	.module				= THIS_MODULE,
	.fabric_name			= "vhost",
	.max_data_sg_nents		= VHOST_SCSI_PREALLOC_SGLS,
	.tpg_get_wwn			= vhost_scsi_get_fabric_wwn,
	.tpg_get_tag			= vhost_scsi_get_tpgt,
	.tpg_check_demo_mode		= vhost_scsi_check_true,
	.tpg_check_demo_mode_cache	= vhost_scsi_check_true,
	.tpg_check_demo_mode_write_protect = vhost_scsi_check_false,
	.tpg_check_prod_mode_write_protect = vhost_scsi_check_false,
	.tpg_check_prot_fabric_only	= vhost_scsi_check_prot_fabric_only,
	.tpg_get_inst_index		= vhost_scsi_tpg_get_inst_index,
	.release_cmd			= vhost_scsi_release_cmd,
	.check_stop_free		= vhost_scsi_check_stop_free,
	.sess_get_index			= vhost_scsi_sess_get_index,
	.sess_get_initiator_sid		= NULL,
	.write_pending			= vhost_scsi_write_pending,
	.set_default_node_attributes	= vhost_scsi_set_default_node_attrs,
	.get_cmd_state			= vhost_scsi_get_cmd_state,
	.queue_data_in			= vhost_scsi_queue_data_in,
	.queue_status			= vhost_scsi_queue_status,
	.queue_tm_rsp			= vhost_scsi_queue_tm_rsp,
	.aborted_task			= vhost_scsi_aborted_task,
	/*
	 * Setup callers for generic logic in target_core_fabric_configfs.c
	 */
	.fabric_make_wwn		= vhost_scsi_make_tport,
	.fabric_drop_wwn		= vhost_scsi_drop_tport,
	.fabric_make_tpg		= vhost_scsi_make_tpg,
	.fabric_drop_tpg		= vhost_scsi_drop_tpg,
	.fabric_post_link		= vhost_scsi_port_link,
	.fabric_pre_unlink		= vhost_scsi_port_unlink,

	.tfc_wwn_attrs			= vhost_scsi_wwn_attrs,
	.tfc_tpg_base_attrs		= vhost_scsi_tpg_attrs,
	.tfc_tpg_attrib_attrs		= vhost_scsi_tpg_attrib_attrs,
};

static int __init vhost_scsi_init(void)
{
	int ret = -ENOMEM;

	pr_debug("TCM_VHOST fabric module %s on %s/%s"
		" on "UTS_RELEASE"\n", VHOST_SCSI_VERSION, utsname()->sysname,
		utsname()->machine);

	/*
	 * Use our own dedicated workqueue for submitting I/O into
	 * target core to avoid contention within system_wq.
	 */
	vhost_scsi_workqueue = alloc_workqueue("vhost_scsi", 0, 0);
	if (!vhost_scsi_workqueue)
		goto out;

	ret = vhost_scsi_register();
	if (ret < 0)
		goto out_destroy_workqueue;

	ret = target_register_template(&vhost_scsi_ops);
	if (ret < 0)
		goto out_vhost_scsi_deregister;

	return 0;

out_vhost_scsi_deregister:
	vhost_scsi_deregister();
out_destroy_workqueue:
	destroy_workqueue(vhost_scsi_workqueue);
out:
	return ret;
};

static void vhost_scsi_exit(void)
{
	target_unregister_template(&vhost_scsi_ops);
	vhost_scsi_deregister();
	destroy_workqueue(vhost_scsi_workqueue);
};

MODULE_DESCRIPTION("VHOST_SCSI series fabric driver");
MODULE_ALIAS("tcm_vhost");
MODULE_LICENSE("GPL");
module_init(vhost_scsi_init);
module_exit(vhost_scsi_exit);

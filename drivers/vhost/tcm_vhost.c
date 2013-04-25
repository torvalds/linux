/*******************************************************************************
 * Vhost kernel TCM fabric driver for virtio SCSI initiators
 *
 * (C) Copyright 2010-2012 RisingTide Systems LLC.
 * (C) Copyright 2010-2012 IBM Corp.
 *
 * Licensed to the Linux Foundation under the General Public License (GPL) version 2.
 *
 * Authors: Nicholas A. Bellinger <nab@risingtidesystems.com>
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
#include <linux/miscdevice.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_fabric_configfs.h>
#include <target/target_core_configfs.h>
#include <target/configfs_macros.h>
#include <linux/vhost.h>
#include <linux/virtio_net.h> /* TODO vhost.h currently depends on this */
#include <linux/virtio_scsi.h>
#include <linux/llist.h>
#include <linux/bitmap.h>

#include "vhost.c"
#include "vhost.h"
#include "tcm_vhost.h"

enum {
	VHOST_SCSI_VQ_CTL = 0,
	VHOST_SCSI_VQ_EVT = 1,
	VHOST_SCSI_VQ_IO = 2,
};

/*
 * VIRTIO_RING_F_EVENT_IDX seems broken. Not sure the bug is in
 * kernel but disabling it helps.
 * TODO: debug and remove the workaround.
 */
enum {
	VHOST_SCSI_FEATURES = VHOST_FEATURES & (~VIRTIO_RING_F_EVENT_IDX)
};

#define VHOST_SCSI_MAX_TARGET	256
#define VHOST_SCSI_MAX_VQ	128
#define VHOST_SCSI_MAX_EVENT	128

struct vhost_scsi {
	/* Protected by vhost_scsi->dev.mutex */
	struct tcm_vhost_tpg **vs_tpg;
	char vs_vhost_wwpn[TRANSPORT_IQN_LEN];

	struct vhost_dev dev;
	struct vhost_virtqueue vqs[VHOST_SCSI_MAX_VQ];

	struct vhost_work vs_completion_work; /* cmd completion work item */
	struct llist_head vs_completion_list; /* cmd completion queue */

	struct vhost_work vs_event_work; /* evt injection work item */
	struct llist_head vs_event_list; /* evt injection queue */

	bool vs_events_missed; /* any missed events, protected by vq->mutex */
	int vs_events_nr; /* num of pending events, protected by vq->mutex */
};

/* Local pointer to allocated TCM configfs fabric module */
static struct target_fabric_configfs *tcm_vhost_fabric_configfs;

static struct workqueue_struct *tcm_vhost_workqueue;

/* Global spinlock to protect tcm_vhost TPG list for vhost IOCTL access */
static DEFINE_MUTEX(tcm_vhost_mutex);
static LIST_HEAD(tcm_vhost_list);

static int iov_num_pages(struct iovec *iov)
{
	return (PAGE_ALIGN((unsigned long)iov->iov_base + iov->iov_len) -
	       ((unsigned long)iov->iov_base & PAGE_MASK)) >> PAGE_SHIFT;
}

static int tcm_vhost_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int tcm_vhost_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static char *tcm_vhost_get_fabric_name(void)
{
	return "vhost";
}

static u8 tcm_vhost_get_fabric_proto_ident(struct se_portal_group *se_tpg)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport = tpg->tport;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return sas_get_fabric_proto_ident(se_tpg);
	case SCSI_PROTOCOL_FCP:
		return fc_get_fabric_proto_ident(se_tpg);
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_get_fabric_proto_ident(se_tpg);
	default:
		pr_err("Unknown tport_proto_id: 0x%02x, using"
			" SAS emulation\n", tport->tport_proto_id);
		break;
	}

	return sas_get_fabric_proto_ident(se_tpg);
}

static char *tcm_vhost_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport = tpg->tport;

	return &tport->tport_name[0];
}

static u16 tcm_vhost_get_tag(struct se_portal_group *se_tpg)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	return tpg->tport_tpgt;
}

static u32 tcm_vhost_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static u32 tcm_vhost_get_pr_transport_id(struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code,
	unsigned char *buf)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport = tpg->tport;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return sas_get_pr_transport_id(se_tpg, se_nacl, pr_reg,
					format_code, buf);
	case SCSI_PROTOCOL_FCP:
		return fc_get_pr_transport_id(se_tpg, se_nacl, pr_reg,
					format_code, buf);
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_get_pr_transport_id(se_tpg, se_nacl, pr_reg,
					format_code, buf);
	default:
		pr_err("Unknown tport_proto_id: 0x%02x, using"
			" SAS emulation\n", tport->tport_proto_id);
		break;
	}

	return sas_get_pr_transport_id(se_tpg, se_nacl, pr_reg,
			format_code, buf);
}

static u32 tcm_vhost_get_pr_transport_id_len(struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int *format_code)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport = tpg->tport;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return sas_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,
					format_code);
	case SCSI_PROTOCOL_FCP:
		return fc_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,
					format_code);
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,
					format_code);
	default:
		pr_err("Unknown tport_proto_id: 0x%02x, using"
			" SAS emulation\n", tport->tport_proto_id);
		break;
	}

	return sas_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,
			format_code);
}

static char *tcm_vhost_parse_pr_out_transport_id(struct se_portal_group *se_tpg,
	const char *buf,
	u32 *out_tid_len,
	char **port_nexus_ptr)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport = tpg->tport;

	switch (tport->tport_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return sas_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,
					port_nexus_ptr);
	case SCSI_PROTOCOL_FCP:
		return fc_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,
					port_nexus_ptr);
	case SCSI_PROTOCOL_ISCSI:
		return iscsi_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,
					port_nexus_ptr);
	default:
		pr_err("Unknown tport_proto_id: 0x%02x, using"
			" SAS emulation\n", tport->tport_proto_id);
		break;
	}

	return sas_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,
			port_nexus_ptr);
}

static struct se_node_acl *tcm_vhost_alloc_fabric_acl(
	struct se_portal_group *se_tpg)
{
	struct tcm_vhost_nacl *nacl;

	nacl = kzalloc(sizeof(struct tcm_vhost_nacl), GFP_KERNEL);
	if (!nacl) {
		pr_err("Unable to allocate struct tcm_vhost_nacl\n");
		return NULL;
	}

	return &nacl->se_node_acl;
}

static void tcm_vhost_release_fabric_acl(struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl)
{
	struct tcm_vhost_nacl *nacl = container_of(se_nacl,
			struct tcm_vhost_nacl, se_node_acl);
	kfree(nacl);
}

static u32 tcm_vhost_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static void tcm_vhost_release_cmd(struct se_cmd *se_cmd)
{
	return;
}

static int tcm_vhost_shutdown_session(struct se_session *se_sess)
{
	return 0;
}

static void tcm_vhost_close_session(struct se_session *se_sess)
{
	return;
}

static u32 tcm_vhost_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static int tcm_vhost_write_pending(struct se_cmd *se_cmd)
{
	/* Go ahead and process the write immediately */
	target_execute_cmd(se_cmd);
	return 0;
}

static int tcm_vhost_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static void tcm_vhost_set_default_node_attrs(struct se_node_acl *nacl)
{
	return;
}

static u32 tcm_vhost_get_task_tag(struct se_cmd *se_cmd)
{
	return 0;
}

static int tcm_vhost_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static void vhost_scsi_complete_cmd(struct tcm_vhost_cmd *tv_cmd)
{
	struct vhost_scsi *vs = tv_cmd->tvc_vhost;

	llist_add(&tv_cmd->tvc_completion_list, &vs->vs_completion_list);

	vhost_work_queue(&vs->dev, &vs->vs_completion_work);
}

static int tcm_vhost_queue_data_in(struct se_cmd *se_cmd)
{
	struct tcm_vhost_cmd *tv_cmd = container_of(se_cmd,
				struct tcm_vhost_cmd, tvc_se_cmd);
	vhost_scsi_complete_cmd(tv_cmd);
	return 0;
}

static int tcm_vhost_queue_status(struct se_cmd *se_cmd)
{
	struct tcm_vhost_cmd *tv_cmd = container_of(se_cmd,
				struct tcm_vhost_cmd, tvc_se_cmd);
	vhost_scsi_complete_cmd(tv_cmd);
	return 0;
}

static int tcm_vhost_queue_tm_rsp(struct se_cmd *se_cmd)
{
	return 0;
}

static void tcm_vhost_free_evt(struct vhost_scsi *vs, struct tcm_vhost_evt *evt)
{
	vs->vs_events_nr--;
	kfree(evt);
}

static struct tcm_vhost_evt *tcm_vhost_allocate_evt(struct vhost_scsi *vs,
	u32 event, u32 reason)
{
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT];
	struct tcm_vhost_evt *evt;

	if (vs->vs_events_nr > VHOST_SCSI_MAX_EVENT) {
		vs->vs_events_missed = true;
		return NULL;
	}

	evt = kzalloc(sizeof(*evt), GFP_KERNEL);
	if (!evt) {
		vq_err(vq, "Failed to allocate tcm_vhost_evt\n");
		vs->vs_events_missed = true;
		return NULL;
	}

	evt->event.event = event;
	evt->event.reason = reason;
	vs->vs_events_nr++;

	return evt;
}

static void vhost_scsi_free_cmd(struct tcm_vhost_cmd *tv_cmd)
{
	struct se_cmd *se_cmd = &tv_cmd->tvc_se_cmd;

	/* TODO locking against target/backend threads? */
	transport_generic_free_cmd(se_cmd, 1);

	if (tv_cmd->tvc_sgl_count) {
		u32 i;
		for (i = 0; i < tv_cmd->tvc_sgl_count; i++)
			put_page(sg_page(&tv_cmd->tvc_sgl[i]));

		kfree(tv_cmd->tvc_sgl);
	}

	kfree(tv_cmd);
}

static void tcm_vhost_do_evt_work(struct vhost_scsi *vs,
	struct tcm_vhost_evt *evt)
{
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT];
	struct virtio_scsi_event *event = &evt->event;
	struct virtio_scsi_event __user *eventp;
	unsigned out, in;
	int head, ret;

	if (!vq->private_data) {
		vs->vs_events_missed = true;
		return;
	}

again:
	vhost_disable_notify(&vs->dev, vq);
	head = vhost_get_vq_desc(&vs->dev, vq, vq->iov,
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
		event->event |= VIRTIO_SCSI_T_EVENTS_MISSED;
		vs->vs_events_missed = false;
	}

	eventp = vq->iov[out].iov_base;
	ret = __copy_to_user(eventp, event, sizeof(*event));
	if (!ret)
		vhost_add_used_and_signal(&vs->dev, vq, head, 0);
	else
		vq_err(vq, "Faulted on tcm_vhost_send_event\n");
}

static void tcm_vhost_evt_work(struct vhost_work *work)
{
	struct vhost_scsi *vs = container_of(work, struct vhost_scsi,
					vs_event_work);
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT];
	struct tcm_vhost_evt *evt;
	struct llist_node *llnode;

	mutex_lock(&vq->mutex);
	llnode = llist_del_all(&vs->vs_event_list);
	while (llnode) {
		evt = llist_entry(llnode, struct tcm_vhost_evt, list);
		llnode = llist_next(llnode);
		tcm_vhost_do_evt_work(vs, evt);
		tcm_vhost_free_evt(vs, evt);
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
	struct tcm_vhost_cmd *tv_cmd;
	struct llist_node *llnode;
	struct se_cmd *se_cmd;
	int ret, vq;

	bitmap_zero(signal, VHOST_SCSI_MAX_VQ);
	llnode = llist_del_all(&vs->vs_completion_list);
	while (llnode) {
		tv_cmd = llist_entry(llnode, struct tcm_vhost_cmd,
				     tvc_completion_list);
		llnode = llist_next(llnode);
		se_cmd = &tv_cmd->tvc_se_cmd;

		pr_debug("%s tv_cmd %p resid %u status %#02x\n", __func__,
			tv_cmd, se_cmd->residual_count, se_cmd->scsi_status);

		memset(&v_rsp, 0, sizeof(v_rsp));
		v_rsp.resid = se_cmd->residual_count;
		/* TODO is status_qualifier field needed? */
		v_rsp.status = se_cmd->scsi_status;
		v_rsp.sense_len = se_cmd->scsi_sense_length;
		memcpy(v_rsp.sense, tv_cmd->tvc_sense_buf,
		       v_rsp.sense_len);
		ret = copy_to_user(tv_cmd->tvc_resp, &v_rsp, sizeof(v_rsp));
		if (likely(ret == 0)) {
			vhost_add_used(tv_cmd->tvc_vq, tv_cmd->tvc_vq_desc, 0);
			vq = tv_cmd->tvc_vq - vs->vqs;
			__set_bit(vq, signal);
		} else
			pr_err("Faulted on virtio_scsi_cmd_resp\n");

		vhost_scsi_free_cmd(tv_cmd);
	}

	vq = -1;
	while ((vq = find_next_bit(signal, VHOST_SCSI_MAX_VQ, vq + 1))
		< VHOST_SCSI_MAX_VQ)
		vhost_signal(&vs->dev, &vs->vqs[vq]);
}

static struct tcm_vhost_cmd *vhost_scsi_allocate_cmd(
	struct tcm_vhost_tpg *tv_tpg,
	struct virtio_scsi_cmd_req *v_req,
	u32 exp_data_len,
	int data_direction)
{
	struct tcm_vhost_cmd *tv_cmd;
	struct tcm_vhost_nexus *tv_nexus;

	tv_nexus = tv_tpg->tpg_nexus;
	if (!tv_nexus) {
		pr_err("Unable to locate active struct tcm_vhost_nexus\n");
		return ERR_PTR(-EIO);
	}

	tv_cmd = kzalloc(sizeof(struct tcm_vhost_cmd), GFP_ATOMIC);
	if (!tv_cmd) {
		pr_err("Unable to allocate struct tcm_vhost_cmd\n");
		return ERR_PTR(-ENOMEM);
	}
	tv_cmd->tvc_tag = v_req->tag;
	tv_cmd->tvc_task_attr = v_req->task_attr;
	tv_cmd->tvc_exp_data_len = exp_data_len;
	tv_cmd->tvc_data_direction = data_direction;
	tv_cmd->tvc_nexus = tv_nexus;

	return tv_cmd;
}

/*
 * Map a user memory range into a scatterlist
 *
 * Returns the number of scatterlist entries used or -errno on error.
 */
static int vhost_scsi_map_to_sgl(struct scatterlist *sgl,
	unsigned int sgl_count, struct iovec *iov, int write)
{
	unsigned int npages = 0, pages_nr, offset, nbytes;
	struct scatterlist *sg = sgl;
	void __user *ptr = iov->iov_base;
	size_t len = iov->iov_len;
	struct page **pages;
	int ret, i;

	pages_nr = iov_num_pages(iov);
	if (pages_nr > sgl_count)
		return -ENOBUFS;

	pages = kmalloc(pages_nr * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = get_user_pages_fast((unsigned long)ptr, pages_nr, write, pages);
	/* No pages were pinned */
	if (ret < 0)
		goto out;
	/* Less pages pinned than wanted */
	if (ret != pages_nr) {
		for (i = 0; i < ret; i++)
			put_page(pages[i]);
		ret = -EFAULT;
		goto out;
	}

	while (len > 0) {
		offset = (uintptr_t)ptr & ~PAGE_MASK;
		nbytes = min_t(unsigned int, PAGE_SIZE - offset, len);
		sg_set_page(sg, pages[npages], nbytes, offset);
		ptr += nbytes;
		len -= nbytes;
		sg++;
		npages++;
	}

out:
	kfree(pages);
	return ret;
}

static int vhost_scsi_map_iov_to_sgl(struct tcm_vhost_cmd *tv_cmd,
	struct iovec *iov, unsigned int niov, int write)
{
	int ret;
	unsigned int i;
	u32 sgl_count;
	struct scatterlist *sg;

	/*
	 * Find out how long sglist needs to be
	 */
	sgl_count = 0;
	for (i = 0; i < niov; i++)
		sgl_count += iov_num_pages(&iov[i]);

	/* TODO overflow checking */

	sg = kmalloc(sizeof(tv_cmd->tvc_sgl[0]) * sgl_count, GFP_ATOMIC);
	if (!sg)
		return -ENOMEM;
	pr_debug("%s sg %p sgl_count %u is_err %d\n", __func__,
	       sg, sgl_count, !sg);
	sg_init_table(sg, sgl_count);

	tv_cmd->tvc_sgl = sg;
	tv_cmd->tvc_sgl_count = sgl_count;

	pr_debug("Mapping %u iovecs for %u pages\n", niov, sgl_count);
	for (i = 0; i < niov; i++) {
		ret = vhost_scsi_map_to_sgl(sg, sgl_count, &iov[i], write);
		if (ret < 0) {
			for (i = 0; i < tv_cmd->tvc_sgl_count; i++)
				put_page(sg_page(&tv_cmd->tvc_sgl[i]));
			kfree(tv_cmd->tvc_sgl);
			tv_cmd->tvc_sgl = NULL;
			tv_cmd->tvc_sgl_count = 0;
			return ret;
		}

		sg += ret;
		sgl_count -= ret;
	}
	return 0;
}

static void tcm_vhost_submission_work(struct work_struct *work)
{
	struct tcm_vhost_cmd *tv_cmd =
		container_of(work, struct tcm_vhost_cmd, work);
	struct tcm_vhost_nexus *tv_nexus;
	struct se_cmd *se_cmd = &tv_cmd->tvc_se_cmd;
	struct scatterlist *sg_ptr, *sg_bidi_ptr = NULL;
	int rc, sg_no_bidi = 0;

	if (tv_cmd->tvc_sgl_count) {
		sg_ptr = tv_cmd->tvc_sgl;
/* FIXME: Fix BIDI operation in tcm_vhost_submission_work() */
#if 0
		if (se_cmd->se_cmd_flags & SCF_BIDI) {
			sg_bidi_ptr = NULL;
			sg_no_bidi = 0;
		}
#endif
	} else {
		sg_ptr = NULL;
	}
	tv_nexus = tv_cmd->tvc_nexus;

	rc = target_submit_cmd_map_sgls(se_cmd, tv_nexus->tvn_se_sess,
			tv_cmd->tvc_cdb, &tv_cmd->tvc_sense_buf[0],
			tv_cmd->tvc_lun, tv_cmd->tvc_exp_data_len,
			tv_cmd->tvc_task_attr, tv_cmd->tvc_data_direction,
			0, sg_ptr, tv_cmd->tvc_sgl_count,
			sg_bidi_ptr, sg_no_bidi);
	if (rc < 0) {
		transport_send_check_condition_and_sense(se_cmd,
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE, 0);
		transport_generic_free_cmd(se_cmd, 0);
	}
}

static void vhost_scsi_send_bad_target(struct vhost_scsi *vs,
	struct vhost_virtqueue *vq, int head, unsigned out)
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

static void vhost_scsi_handle_vq(struct vhost_scsi *vs,
	struct vhost_virtqueue *vq)
{
	struct tcm_vhost_tpg **vs_tpg;
	struct virtio_scsi_cmd_req v_req;
	struct tcm_vhost_tpg *tv_tpg;
	struct tcm_vhost_cmd *tv_cmd;
	u32 exp_data_len, data_first, data_num, data_direction;
	unsigned out, in, i;
	int head, ret;
	u8 target;

	/*
	 * We can handle the vq only after the endpoint is setup by calling the
	 * VHOST_SCSI_SET_ENDPOINT ioctl.
	 *
	 * TODO: Check that we are running from vhost_worker which acts
	 * as read-side critical section for vhost kind of RCU.
	 * See the comments in struct vhost_virtqueue in drivers/vhost/vhost.h
	 */
	vs_tpg = rcu_dereference_check(vq->private_data, 1);
	if (!vs_tpg)
		return;

	mutex_lock(&vq->mutex);
	vhost_disable_notify(&vs->dev, vq);

	for (;;) {
		head = vhost_get_vq_desc(&vs->dev, vq, vq->iov,
					ARRAY_SIZE(vq->iov), &out, &in,
					NULL, NULL);
		pr_debug("vhost_get_vq_desc: head: %d, out: %u in: %u\n",
					head, out, in);
		/* On error, stop handling until the next kick. */
		if (unlikely(head < 0))
			break;
		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (head == vq->num) {
			if (unlikely(vhost_enable_notify(&vs->dev, vq))) {
				vhost_disable_notify(&vs->dev, vq);
				continue;
			}
			break;
		}

/* FIXME: BIDI operation */
		if (out == 1 && in == 1) {
			data_direction = DMA_NONE;
			data_first = 0;
			data_num = 0;
		} else if (out == 1 && in > 1) {
			data_direction = DMA_FROM_DEVICE;
			data_first = out + 1;
			data_num = in - 1;
		} else if (out > 1 && in == 1) {
			data_direction = DMA_TO_DEVICE;
			data_first = 1;
			data_num = out - 1;
		} else {
			vq_err(vq, "Invalid buffer layout out: %u in: %u\n",
					out, in);
			break;
		}

		/*
		 * Check for a sane resp buffer so we can report errors to
		 * the guest.
		 */
		if (unlikely(vq->iov[out].iov_len !=
					sizeof(struct virtio_scsi_cmd_resp))) {
			vq_err(vq, "Expecting virtio_scsi_cmd_resp, got %zu"
				" bytes\n", vq->iov[out].iov_len);
			break;
		}

		if (unlikely(vq->iov[0].iov_len != sizeof(v_req))) {
			vq_err(vq, "Expecting virtio_scsi_cmd_req, got %zu"
				" bytes\n", vq->iov[0].iov_len);
			break;
		}
		pr_debug("Calling __copy_from_user: vq->iov[0].iov_base: %p,"
			" len: %zu\n", vq->iov[0].iov_base, sizeof(v_req));
		ret = __copy_from_user(&v_req, vq->iov[0].iov_base,
				sizeof(v_req));
		if (unlikely(ret)) {
			vq_err(vq, "Faulted on virtio_scsi_cmd_req\n");
			break;
		}

		/* Extract the tpgt */
		target = v_req.lun[1];
		tv_tpg = ACCESS_ONCE(vs_tpg[target]);

		/* Target does not exist, fail the request */
		if (unlikely(!tv_tpg)) {
			vhost_scsi_send_bad_target(vs, vq, head, out);
			continue;
		}

		exp_data_len = 0;
		for (i = 0; i < data_num; i++)
			exp_data_len += vq->iov[data_first + i].iov_len;

		tv_cmd = vhost_scsi_allocate_cmd(tv_tpg, &v_req,
					exp_data_len, data_direction);
		if (IS_ERR(tv_cmd)) {
			vq_err(vq, "vhost_scsi_allocate_cmd failed %ld\n",
					PTR_ERR(tv_cmd));
			goto err_cmd;
		}
		pr_debug("Allocated tv_cmd: %p exp_data_len: %d, data_direction"
			": %d\n", tv_cmd, exp_data_len, data_direction);

		tv_cmd->tvc_vhost = vs;
		tv_cmd->tvc_vq = vq;
		tv_cmd->tvc_resp = vq->iov[out].iov_base;

		/*
		 * Copy in the recieved CDB descriptor into tv_cmd->tvc_cdb
		 * that will be used by tcm_vhost_new_cmd_map() and down into
		 * target_setup_cmd_from_cdb()
		 */
		memcpy(tv_cmd->tvc_cdb, v_req.cdb, TCM_VHOST_MAX_CDB_SIZE);
		/*
		 * Check that the recieved CDB size does not exceeded our
		 * hardcoded max for tcm_vhost
		 */
		/* TODO what if cdb was too small for varlen cdb header? */
		if (unlikely(scsi_command_size(tv_cmd->tvc_cdb) >
					TCM_VHOST_MAX_CDB_SIZE)) {
			vq_err(vq, "Received SCSI CDB with command_size: %d that"
				" exceeds SCSI_MAX_VARLEN_CDB_SIZE: %d\n",
				scsi_command_size(tv_cmd->tvc_cdb),
				TCM_VHOST_MAX_CDB_SIZE);
			goto err_free;
		}
		tv_cmd->tvc_lun = ((v_req.lun[2] << 8) | v_req.lun[3]) & 0x3FFF;

		pr_debug("vhost_scsi got command opcode: %#02x, lun: %d\n",
			tv_cmd->tvc_cdb[0], tv_cmd->tvc_lun);

		if (data_direction != DMA_NONE) {
			ret = vhost_scsi_map_iov_to_sgl(tv_cmd,
					&vq->iov[data_first], data_num,
					data_direction == DMA_TO_DEVICE);
			if (unlikely(ret)) {
				vq_err(vq, "Failed to map iov to sgl\n");
				goto err_free;
			}
		}

		/*
		 * Save the descriptor from vhost_get_vq_desc() to be used to
		 * complete the virtio-scsi request in TCM callback context via
		 * tcm_vhost_queue_data_in() and tcm_vhost_queue_status()
		 */
		tv_cmd->tvc_vq_desc = head;
		/*
		 * Dispatch tv_cmd descriptor for cmwq execution in process
		 * context provided by tcm_vhost_workqueue.  This also ensures
		 * tv_cmd is executed on the same kworker CPU as this vhost
		 * thread to gain positive L2 cache locality effects..
		 */
		INIT_WORK(&tv_cmd->work, tcm_vhost_submission_work);
		queue_work(tcm_vhost_workqueue, &tv_cmd->work);
	}

	mutex_unlock(&vq->mutex);
	return;

err_free:
	vhost_scsi_free_cmd(tv_cmd);
err_cmd:
	vhost_scsi_send_bad_target(vs, vq, head, out);
	mutex_unlock(&vq->mutex);
}

static void vhost_scsi_ctl_handle_kick(struct vhost_work *work)
{
	pr_debug("%s: The handling func for control queue.\n", __func__);
}

static void tcm_vhost_send_evt(struct vhost_scsi *vs, struct tcm_vhost_tpg *tpg,
	struct se_lun *lun, u32 event, u32 reason)
{
	struct tcm_vhost_evt *evt;

	evt = tcm_vhost_allocate_evt(vs, event, reason);
	if (!evt)
		return;

	if (tpg && lun) {
		/* TODO: share lun setup code with virtio-scsi.ko */
		/*
		 * Note: evt->event is zeroed when we allocate it and
		 * lun[4-7] need to be zero according to virtio-scsi spec.
		 */
		evt->event.lun[0] = 0x01;
		evt->event.lun[1] = tpg->tport_tpgt & 0xFF;
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
	if (!vq->private_data)
		goto out;

	if (vs->vs_events_missed)
		tcm_vhost_send_evt(vs, NULL, NULL, VIRTIO_SCSI_T_NO_EVENT, 0);
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
	vhost_poll_flush(&vs->dev.vqs[index].poll);
}

static void vhost_scsi_flush(struct vhost_scsi *vs)
{
	int i;

	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		vhost_scsi_flush_vq(vs, i);
	vhost_work_flush(&vs->dev, &vs->vs_completion_work);
	vhost_work_flush(&vs->dev, &vs->vs_event_work);
}

/*
 * Called from vhost_scsi_ioctl() context to walk the list of available
 * tcm_vhost_tpg with an active struct tcm_vhost_nexus
 *
 *  The lock nesting rule is:
 *    tcm_vhost_mutex -> vs->dev.mutex -> tpg->tv_tpg_mutex -> vq->mutex
 */
static int vhost_scsi_set_endpoint(
	struct vhost_scsi *vs,
	struct vhost_scsi_target *t)
{
	struct tcm_vhost_tport *tv_tport;
	struct tcm_vhost_tpg *tv_tpg;
	struct tcm_vhost_tpg **vs_tpg;
	struct vhost_virtqueue *vq;
	int index, ret, i, len;
	bool match = false;

	mutex_lock(&tcm_vhost_mutex);
	mutex_lock(&vs->dev.mutex);

	/* Verify that ring has been setup correctly. */
	for (index = 0; index < vs->dev.nvqs; ++index) {
		/* Verify that ring has been setup correctly. */
		if (!vhost_vq_access_ok(&vs->vqs[index])) {
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

	list_for_each_entry(tv_tpg, &tcm_vhost_list, tv_tpg_list) {
		mutex_lock(&tv_tpg->tv_tpg_mutex);
		if (!tv_tpg->tpg_nexus) {
			mutex_unlock(&tv_tpg->tv_tpg_mutex);
			continue;
		}
		if (tv_tpg->tv_tpg_vhost_count != 0) {
			mutex_unlock(&tv_tpg->tv_tpg_mutex);
			continue;
		}
		tv_tport = tv_tpg->tport;

		if (!strcmp(tv_tport->tport_name, t->vhost_wwpn)) {
			if (vs->vs_tpg && vs->vs_tpg[tv_tpg->tport_tpgt]) {
				kfree(vs_tpg);
				mutex_unlock(&tv_tpg->tv_tpg_mutex);
				ret = -EEXIST;
				goto out;
			}
			tv_tpg->tv_tpg_vhost_count++;
			tv_tpg->vhost_scsi = vs;
			vs_tpg[tv_tpg->tport_tpgt] = tv_tpg;
			smp_mb__after_atomic_inc();
			match = true;
		}
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
	}

	if (match) {
		memcpy(vs->vs_vhost_wwpn, t->vhost_wwpn,
		       sizeof(vs->vs_vhost_wwpn));
		for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i];
			/* Flushing the vhost_work acts as synchronize_rcu */
			mutex_lock(&vq->mutex);
			rcu_assign_pointer(vq->private_data, vs_tpg);
			vhost_init_used(vq);
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

out:
	mutex_unlock(&vs->dev.mutex);
	mutex_unlock(&tcm_vhost_mutex);
	return ret;
}

static int vhost_scsi_clear_endpoint(
	struct vhost_scsi *vs,
	struct vhost_scsi_target *t)
{
	struct tcm_vhost_tport *tv_tport;
	struct tcm_vhost_tpg *tv_tpg;
	struct vhost_virtqueue *vq;
	bool match = false;
	int index, ret, i;
	u8 target;

	mutex_lock(&tcm_vhost_mutex);
	mutex_lock(&vs->dev.mutex);
	/* Verify that ring has been setup correctly. */
	for (index = 0; index < vs->dev.nvqs; ++index) {
		if (!vhost_vq_access_ok(&vs->vqs[index])) {
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
		tv_tpg = vs->vs_tpg[target];
		if (!tv_tpg)
			continue;

		mutex_lock(&tv_tpg->tv_tpg_mutex);
		tv_tport = tv_tpg->tport;
		if (!tv_tport) {
			ret = -ENODEV;
			goto err_tpg;
		}

		if (strcmp(tv_tport->tport_name, t->vhost_wwpn)) {
			pr_warn("tv_tport->tport_name: %s, tv_tpg->tport_tpgt: %hu"
				" does not match t->vhost_wwpn: %s, t->vhost_tpgt: %hu\n",
				tv_tport->tport_name, tv_tpg->tport_tpgt,
				t->vhost_wwpn, t->vhost_tpgt);
			ret = -EINVAL;
			goto err_tpg;
		}
		tv_tpg->tv_tpg_vhost_count--;
		tv_tpg->vhost_scsi = NULL;
		vs->vs_tpg[target] = NULL;
		match = true;
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
	}
	if (match) {
		for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i];
			/* Flushing the vhost_work acts as synchronize_rcu */
			mutex_lock(&vq->mutex);
			rcu_assign_pointer(vq->private_data, NULL);
			mutex_unlock(&vq->mutex);
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
	mutex_unlock(&tcm_vhost_mutex);
	return 0;

err_tpg:
	mutex_unlock(&tv_tpg->tv_tpg_mutex);
err_dev:
	mutex_unlock(&vs->dev.mutex);
	mutex_unlock(&tcm_vhost_mutex);
	return ret;
}

static int vhost_scsi_set_features(struct vhost_scsi *vs, u64 features)
{
	if (features & ~VHOST_SCSI_FEATURES)
		return -EOPNOTSUPP;

	mutex_lock(&vs->dev.mutex);
	if ((features & (1 << VHOST_F_LOG_ALL)) &&
	    !vhost_log_access_ok(&vs->dev)) {
		mutex_unlock(&vs->dev.mutex);
		return -EFAULT;
	}
	vs->dev.acked_features = features;
	smp_wmb();
	vhost_scsi_flush(vs);
	mutex_unlock(&vs->dev.mutex);
	return 0;
}

static int vhost_scsi_open(struct inode *inode, struct file *f)
{
	struct vhost_scsi *s;
	int r, i;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	vhost_work_init(&s->vs_completion_work, vhost_scsi_complete_cmd_work);
	vhost_work_init(&s->vs_event_work, tcm_vhost_evt_work);

	s->vs_events_nr = 0;
	s->vs_events_missed = false;

	s->vqs[VHOST_SCSI_VQ_CTL].handle_kick = vhost_scsi_ctl_handle_kick;
	s->vqs[VHOST_SCSI_VQ_EVT].handle_kick = vhost_scsi_evt_handle_kick;
	for (i = VHOST_SCSI_VQ_IO; i < VHOST_SCSI_MAX_VQ; i++)
		s->vqs[i].handle_kick = vhost_scsi_handle_kick;
	r = vhost_dev_init(&s->dev, s->vqs, VHOST_SCSI_MAX_VQ);
	if (r < 0) {
		kfree(s);
		return r;
	}

	f->private_data = s;
	return 0;
}

static int vhost_scsi_release(struct inode *inode, struct file *f)
{
	struct vhost_scsi *s = f->private_data;
	struct vhost_scsi_target t;

	mutex_lock(&s->dev.mutex);
	memcpy(t.vhost_wwpn, s->vs_vhost_wwpn, sizeof(t.vhost_wwpn));
	mutex_unlock(&s->dev.mutex);
	vhost_scsi_clear_endpoint(s, &t);
	vhost_dev_stop(&s->dev);
	vhost_dev_cleanup(&s->dev, false);
	/* Jobs can re-queue themselves in evt kick handler. Do extra flush. */
	vhost_scsi_flush(s);
	kfree(s);
	return 0;
}

static long vhost_scsi_ioctl(struct file *f, unsigned int ioctl,
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
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT];

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

#ifdef CONFIG_COMPAT
static long vhost_scsi_compat_ioctl(struct file *f, unsigned int ioctl,
				unsigned long arg)
{
	return vhost_scsi_ioctl(f, ioctl, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations vhost_scsi_fops = {
	.owner          = THIS_MODULE,
	.release        = vhost_scsi_release,
	.unlocked_ioctl = vhost_scsi_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= vhost_scsi_compat_ioctl,
#endif
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

static int vhost_scsi_deregister(void)
{
	return misc_deregister(&vhost_scsi_misc);
}

static char *tcm_vhost_dump_proto_id(struct tcm_vhost_tport *tport)
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

static void tcm_vhost_do_plug(struct tcm_vhost_tpg *tpg,
	struct se_lun *lun, bool plug)
{

	struct vhost_scsi *vs = tpg->vhost_scsi;
	struct vhost_virtqueue *vq;
	u32 reason;

	if (!vs)
		return;

	mutex_lock(&vs->dev.mutex);
	if (!vhost_has_feature(&vs->dev, VIRTIO_SCSI_F_HOTPLUG)) {
		mutex_unlock(&vs->dev.mutex);
		return;
	}

	if (plug)
		reason = VIRTIO_SCSI_EVT_RESET_RESCAN;
	else
		reason = VIRTIO_SCSI_EVT_RESET_REMOVED;

	vq = &vs->vqs[VHOST_SCSI_VQ_EVT];
	mutex_lock(&vq->mutex);
	tcm_vhost_send_evt(vs, tpg, lun,
			VIRTIO_SCSI_T_TRANSPORT_RESET, reason);
	mutex_unlock(&vq->mutex);
	mutex_unlock(&vs->dev.mutex);
}

static void tcm_vhost_hotplug(struct tcm_vhost_tpg *tpg, struct se_lun *lun)
{
	tcm_vhost_do_plug(tpg, lun, true);
}

static void tcm_vhost_hotunplug(struct tcm_vhost_tpg *tpg, struct se_lun *lun)
{
	tcm_vhost_do_plug(tpg, lun, false);
}

static int tcm_vhost_port_link(struct se_portal_group *se_tpg,
	struct se_lun *lun)
{
	struct tcm_vhost_tpg *tv_tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);

	mutex_lock(&tcm_vhost_mutex);

	mutex_lock(&tv_tpg->tv_tpg_mutex);
	tv_tpg->tv_tpg_port_count++;
	mutex_unlock(&tv_tpg->tv_tpg_mutex);

	tcm_vhost_hotplug(tv_tpg, lun);

	mutex_unlock(&tcm_vhost_mutex);

	return 0;
}

static void tcm_vhost_port_unlink(struct se_portal_group *se_tpg,
	struct se_lun *lun)
{
	struct tcm_vhost_tpg *tv_tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);

	mutex_lock(&tcm_vhost_mutex);

	mutex_lock(&tv_tpg->tv_tpg_mutex);
	tv_tpg->tv_tpg_port_count--;
	mutex_unlock(&tv_tpg->tv_tpg_mutex);

	tcm_vhost_hotunplug(tv_tpg, lun);

	mutex_unlock(&tcm_vhost_mutex);
}

static struct se_node_acl *tcm_vhost_make_nodeacl(
	struct se_portal_group *se_tpg,
	struct config_group *group,
	const char *name)
{
	struct se_node_acl *se_nacl, *se_nacl_new;
	struct tcm_vhost_nacl *nacl;
	u64 wwpn = 0;
	u32 nexus_depth;

	/* tcm_vhost_parse_wwn(name, &wwpn, 1) < 0)
		return ERR_PTR(-EINVAL); */
	se_nacl_new = tcm_vhost_alloc_fabric_acl(se_tpg);
	if (!se_nacl_new)
		return ERR_PTR(-ENOMEM);

	nexus_depth = 1;
	/*
	 * se_nacl_new may be released by core_tpg_add_initiator_node_acl()
	 * when converting a NodeACL from demo mode -> explict
	 */
	se_nacl = core_tpg_add_initiator_node_acl(se_tpg, se_nacl_new,
				name, nexus_depth);
	if (IS_ERR(se_nacl)) {
		tcm_vhost_release_fabric_acl(se_tpg, se_nacl_new);
		return se_nacl;
	}
	/*
	 * Locate our struct tcm_vhost_nacl and set the FC Nport WWPN
	 */
	nacl = container_of(se_nacl, struct tcm_vhost_nacl, se_node_acl);
	nacl->iport_wwpn = wwpn;

	return se_nacl;
}

static void tcm_vhost_drop_nodeacl(struct se_node_acl *se_acl)
{
	struct tcm_vhost_nacl *nacl = container_of(se_acl,
				struct tcm_vhost_nacl, se_node_acl);
	core_tpg_del_initiator_node_acl(se_acl->se_tpg, se_acl, 1);
	kfree(nacl);
}

static int tcm_vhost_make_nexus(struct tcm_vhost_tpg *tv_tpg,
	const char *name)
{
	struct se_portal_group *se_tpg;
	struct tcm_vhost_nexus *tv_nexus;

	mutex_lock(&tv_tpg->tv_tpg_mutex);
	if (tv_tpg->tpg_nexus) {
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
		pr_debug("tv_tpg->tpg_nexus already exists\n");
		return -EEXIST;
	}
	se_tpg = &tv_tpg->se_tpg;

	tv_nexus = kzalloc(sizeof(struct tcm_vhost_nexus), GFP_KERNEL);
	if (!tv_nexus) {
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
		pr_err("Unable to allocate struct tcm_vhost_nexus\n");
		return -ENOMEM;
	}
	/*
	 *  Initialize the struct se_session pointer
	 */
	tv_nexus->tvn_se_sess = transport_init_session();
	if (IS_ERR(tv_nexus->tvn_se_sess)) {
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
		kfree(tv_nexus);
		return -ENOMEM;
	}
	/*
	 * Since we are running in 'demo mode' this call with generate a
	 * struct se_node_acl for the tcm_vhost struct se_portal_group with
	 * the SCSI Initiator port name of the passed configfs group 'name'.
	 */
	tv_nexus->tvn_se_sess->se_node_acl = core_tpg_check_initiator_node_acl(
				se_tpg, (unsigned char *)name);
	if (!tv_nexus->tvn_se_sess->se_node_acl) {
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
		pr_debug("core_tpg_check_initiator_node_acl() failed"
				" for %s\n", name);
		transport_free_session(tv_nexus->tvn_se_sess);
		kfree(tv_nexus);
		return -ENOMEM;
	}
	/*
	 * Now register the TCM vhost virtual I_T Nexus as active with the
	 * call to __transport_register_session()
	 */
	__transport_register_session(se_tpg, tv_nexus->tvn_se_sess->se_node_acl,
			tv_nexus->tvn_se_sess, tv_nexus);
	tv_tpg->tpg_nexus = tv_nexus;

	mutex_unlock(&tv_tpg->tv_tpg_mutex);
	return 0;
}

static int tcm_vhost_drop_nexus(struct tcm_vhost_tpg *tpg)
{
	struct se_session *se_sess;
	struct tcm_vhost_nexus *tv_nexus;

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
		" %s Initiator Port: %s\n", tcm_vhost_dump_proto_id(tpg->tport),
		tv_nexus->tvn_se_sess->se_node_acl->initiatorname);
	/*
	 * Release the SCSI I_T Nexus to the emulated vhost Target Port
	 */
	transport_deregister_session(tv_nexus->tvn_se_sess);
	tpg->tpg_nexus = NULL;
	mutex_unlock(&tpg->tv_tpg_mutex);

	kfree(tv_nexus);
	return 0;
}

static ssize_t tcm_vhost_tpg_show_nexus(struct se_portal_group *se_tpg,
	char *page)
{
	struct tcm_vhost_tpg *tv_tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_nexus *tv_nexus;
	ssize_t ret;

	mutex_lock(&tv_tpg->tv_tpg_mutex);
	tv_nexus = tv_tpg->tpg_nexus;
	if (!tv_nexus) {
		mutex_unlock(&tv_tpg->tv_tpg_mutex);
		return -ENODEV;
	}
	ret = snprintf(page, PAGE_SIZE, "%s\n",
			tv_nexus->tvn_se_sess->se_node_acl->initiatorname);
	mutex_unlock(&tv_tpg->tv_tpg_mutex);

	return ret;
}

static ssize_t tcm_vhost_tpg_store_nexus(struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct tcm_vhost_tpg *tv_tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport_wwn = tv_tpg->tport;
	unsigned char i_port[TCM_VHOST_NAMELEN], *ptr, *port_ptr;
	int ret;
	/*
	 * Shutdown the active I_T nexus if 'NULL' is passed..
	 */
	if (!strncmp(page, "NULL", 4)) {
		ret = tcm_vhost_drop_nexus(tv_tpg);
		return (!ret) ? count : ret;
	}
	/*
	 * Otherwise make sure the passed virtual Initiator port WWN matches
	 * the fabric protocol_id set in tcm_vhost_make_tport(), and call
	 * tcm_vhost_make_nexus().
	 */
	if (strlen(page) >= TCM_VHOST_NAMELEN) {
		pr_err("Emulated NAA Sas Address: %s, exceeds"
				" max: %d\n", page, TCM_VHOST_NAMELEN);
		return -EINVAL;
	}
	snprintf(&i_port[0], TCM_VHOST_NAMELEN, "%s", page);

	ptr = strstr(i_port, "naa.");
	if (ptr) {
		if (tport_wwn->tport_proto_id != SCSI_PROTOCOL_SAS) {
			pr_err("Passed SAS Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				tcm_vhost_dump_proto_id(tport_wwn));
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
				tcm_vhost_dump_proto_id(tport_wwn));
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
				tcm_vhost_dump_proto_id(tport_wwn));
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

	ret = tcm_vhost_make_nexus(tv_tpg, port_ptr);
	if (ret < 0)
		return ret;

	return count;
}

TF_TPG_BASE_ATTR(tcm_vhost, nexus, S_IRUGO | S_IWUSR);

static struct configfs_attribute *tcm_vhost_tpg_attrs[] = {
	&tcm_vhost_tpg_nexus.attr,
	NULL,
};

static struct se_portal_group *tcm_vhost_make_tpg(struct se_wwn *wwn,
	struct config_group *group,
	const char *name)
{
	struct tcm_vhost_tport *tport = container_of(wwn,
			struct tcm_vhost_tport, tport_wwn);

	struct tcm_vhost_tpg *tpg;
	unsigned long tpgt;
	int ret;

	if (strstr(name, "tpgt_") != name)
		return ERR_PTR(-EINVAL);
	if (kstrtoul(name + 5, 10, &tpgt) || tpgt > UINT_MAX)
		return ERR_PTR(-EINVAL);

	tpg = kzalloc(sizeof(struct tcm_vhost_tpg), GFP_KERNEL);
	if (!tpg) {
		pr_err("Unable to allocate struct tcm_vhost_tpg");
		return ERR_PTR(-ENOMEM);
	}
	mutex_init(&tpg->tv_tpg_mutex);
	INIT_LIST_HEAD(&tpg->tv_tpg_list);
	tpg->tport = tport;
	tpg->tport_tpgt = tpgt;

	ret = core_tpg_register(&tcm_vhost_fabric_configfs->tf_ops, wwn,
				&tpg->se_tpg, tpg, TRANSPORT_TPG_TYPE_NORMAL);
	if (ret < 0) {
		kfree(tpg);
		return NULL;
	}
	mutex_lock(&tcm_vhost_mutex);
	list_add_tail(&tpg->tv_tpg_list, &tcm_vhost_list);
	mutex_unlock(&tcm_vhost_mutex);

	return &tpg->se_tpg;
}

static void tcm_vhost_drop_tpg(struct se_portal_group *se_tpg)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);

	mutex_lock(&tcm_vhost_mutex);
	list_del(&tpg->tv_tpg_list);
	mutex_unlock(&tcm_vhost_mutex);
	/*
	 * Release the virtual I_T Nexus for this vhost TPG
	 */
	tcm_vhost_drop_nexus(tpg);
	/*
	 * Deregister the se_tpg from TCM..
	 */
	core_tpg_deregister(se_tpg);
	kfree(tpg);
}

static struct se_wwn *tcm_vhost_make_tport(struct target_fabric_configfs *tf,
	struct config_group *group,
	const char *name)
{
	struct tcm_vhost_tport *tport;
	char *ptr;
	u64 wwpn = 0;
	int off = 0;

	/* if (tcm_vhost_parse_wwn(name, &wwpn, 1) < 0)
		return ERR_PTR(-EINVAL); */

	tport = kzalloc(sizeof(struct tcm_vhost_tport), GFP_KERNEL);
	if (!tport) {
		pr_err("Unable to allocate struct tcm_vhost_tport");
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
	if (strlen(name) >= TCM_VHOST_NAMELEN) {
		pr_err("Emulated %s Address: %s, exceeds"
			" max: %d\n", name, tcm_vhost_dump_proto_id(tport),
			TCM_VHOST_NAMELEN);
		kfree(tport);
		return ERR_PTR(-EINVAL);
	}
	snprintf(&tport->tport_name[0], TCM_VHOST_NAMELEN, "%s", &name[off]);

	pr_debug("TCM_VHost_ConfigFS: Allocated emulated Target"
		" %s Address: %s\n", tcm_vhost_dump_proto_id(tport), name);

	return &tport->tport_wwn;
}

static void tcm_vhost_drop_tport(struct se_wwn *wwn)
{
	struct tcm_vhost_tport *tport = container_of(wwn,
				struct tcm_vhost_tport, tport_wwn);

	pr_debug("TCM_VHost_ConfigFS: Deallocating emulated Target"
		" %s Address: %s\n", tcm_vhost_dump_proto_id(tport),
		tport->tport_name);

	kfree(tport);
}

static ssize_t tcm_vhost_wwn_show_attr_version(
	struct target_fabric_configfs *tf,
	char *page)
{
	return sprintf(page, "TCM_VHOST fabric module %s on %s/%s"
		"on "UTS_RELEASE"\n", TCM_VHOST_VERSION, utsname()->sysname,
		utsname()->machine);
}

TF_WWN_ATTR_RO(tcm_vhost, version);

static struct configfs_attribute *tcm_vhost_wwn_attrs[] = {
	&tcm_vhost_wwn_version.attr,
	NULL,
};

static struct target_core_fabric_ops tcm_vhost_ops = {
	.get_fabric_name		= tcm_vhost_get_fabric_name,
	.get_fabric_proto_ident		= tcm_vhost_get_fabric_proto_ident,
	.tpg_get_wwn			= tcm_vhost_get_fabric_wwn,
	.tpg_get_tag			= tcm_vhost_get_tag,
	.tpg_get_default_depth		= tcm_vhost_get_default_depth,
	.tpg_get_pr_transport_id	= tcm_vhost_get_pr_transport_id,
	.tpg_get_pr_transport_id_len	= tcm_vhost_get_pr_transport_id_len,
	.tpg_parse_pr_out_transport_id	= tcm_vhost_parse_pr_out_transport_id,
	.tpg_check_demo_mode		= tcm_vhost_check_true,
	.tpg_check_demo_mode_cache	= tcm_vhost_check_true,
	.tpg_check_demo_mode_write_protect = tcm_vhost_check_false,
	.tpg_check_prod_mode_write_protect = tcm_vhost_check_false,
	.tpg_alloc_fabric_acl		= tcm_vhost_alloc_fabric_acl,
	.tpg_release_fabric_acl		= tcm_vhost_release_fabric_acl,
	.tpg_get_inst_index		= tcm_vhost_tpg_get_inst_index,
	.release_cmd			= tcm_vhost_release_cmd,
	.shutdown_session		= tcm_vhost_shutdown_session,
	.close_session			= tcm_vhost_close_session,
	.sess_get_index			= tcm_vhost_sess_get_index,
	.sess_get_initiator_sid		= NULL,
	.write_pending			= tcm_vhost_write_pending,
	.write_pending_status		= tcm_vhost_write_pending_status,
	.set_default_node_attributes	= tcm_vhost_set_default_node_attrs,
	.get_task_tag			= tcm_vhost_get_task_tag,
	.get_cmd_state			= tcm_vhost_get_cmd_state,
	.queue_data_in			= tcm_vhost_queue_data_in,
	.queue_status			= tcm_vhost_queue_status,
	.queue_tm_rsp			= tcm_vhost_queue_tm_rsp,
	/*
	 * Setup callers for generic logic in target_core_fabric_configfs.c
	 */
	.fabric_make_wwn		= tcm_vhost_make_tport,
	.fabric_drop_wwn		= tcm_vhost_drop_tport,
	.fabric_make_tpg		= tcm_vhost_make_tpg,
	.fabric_drop_tpg		= tcm_vhost_drop_tpg,
	.fabric_post_link		= tcm_vhost_port_link,
	.fabric_pre_unlink		= tcm_vhost_port_unlink,
	.fabric_make_np			= NULL,
	.fabric_drop_np			= NULL,
	.fabric_make_nodeacl		= tcm_vhost_make_nodeacl,
	.fabric_drop_nodeacl		= tcm_vhost_drop_nodeacl,
};

static int tcm_vhost_register_configfs(void)
{
	struct target_fabric_configfs *fabric;
	int ret;

	pr_debug("TCM_VHOST fabric module %s on %s/%s"
		" on "UTS_RELEASE"\n", TCM_VHOST_VERSION, utsname()->sysname,
		utsname()->machine);
	/*
	 * Register the top level struct config_item_type with TCM core
	 */
	fabric = target_fabric_configfs_init(THIS_MODULE, "vhost");
	if (IS_ERR(fabric)) {
		pr_err("target_fabric_configfs_init() failed\n");
		return PTR_ERR(fabric);
	}
	/*
	 * Setup fabric->tf_ops from our local tcm_vhost_ops
	 */
	fabric->tf_ops = tcm_vhost_ops;
	/*
	 * Setup default attribute lists for various fabric->tf_cit_tmpl
	 */
	TF_CIT_TMPL(fabric)->tfc_wwn_cit.ct_attrs = tcm_vhost_wwn_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_base_cit.ct_attrs = tcm_vhost_tpg_attrs;
	TF_CIT_TMPL(fabric)->tfc_tpg_attrib_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_param_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_np_base_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_base_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_attrib_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_auth_cit.ct_attrs = NULL;
	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_param_cit.ct_attrs = NULL;
	/*
	 * Register the fabric for use within TCM
	 */
	ret = target_fabric_configfs_register(fabric);
	if (ret < 0) {
		pr_err("target_fabric_configfs_register() failed"
				" for TCM_VHOST\n");
		return ret;
	}
	/*
	 * Setup our local pointer to *fabric
	 */
	tcm_vhost_fabric_configfs = fabric;
	pr_debug("TCM_VHOST[0] - Set fabric -> tcm_vhost_fabric_configfs\n");
	return 0;
};

static void tcm_vhost_deregister_configfs(void)
{
	if (!tcm_vhost_fabric_configfs)
		return;

	target_fabric_configfs_deregister(tcm_vhost_fabric_configfs);
	tcm_vhost_fabric_configfs = NULL;
	pr_debug("TCM_VHOST[0] - Cleared tcm_vhost_fabric_configfs\n");
};

static int __init tcm_vhost_init(void)
{
	int ret = -ENOMEM;
	/*
	 * Use our own dedicated workqueue for submitting I/O into
	 * target core to avoid contention within system_wq.
	 */
	tcm_vhost_workqueue = alloc_workqueue("tcm_vhost", 0, 0);
	if (!tcm_vhost_workqueue)
		goto out;

	ret = vhost_scsi_register();
	if (ret < 0)
		goto out_destroy_workqueue;

	ret = tcm_vhost_register_configfs();
	if (ret < 0)
		goto out_vhost_scsi_deregister;

	return 0;

out_vhost_scsi_deregister:
	vhost_scsi_deregister();
out_destroy_workqueue:
	destroy_workqueue(tcm_vhost_workqueue);
out:
	return ret;
};

static void tcm_vhost_exit(void)
{
	tcm_vhost_deregister_configfs();
	vhost_scsi_deregister();
	destroy_workqueue(tcm_vhost_workqueue);
};

MODULE_DESCRIPTION("TCM_VHOST series fabric driver");
MODULE_LICENSE("GPL");
module_init(tcm_vhost_init);
module_exit(tcm_vhost_exit);

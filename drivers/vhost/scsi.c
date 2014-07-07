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
#include <linux/virtio_scsi.h>
#include <linux/llist.h>
#include <linux/bitmap.h>
#include <linux/percpu_ida.h>

#include "vhost.h"

#define TCM_VHOST_VERSION  "v0.1"
#define TCM_VHOST_NAMELEN 256
#define TCM_VHOST_MAX_CDB_SIZE 32
#define TCM_VHOST_DEFAULT_TAGS 256
#define TCM_VHOST_PREALLOC_SGLS 2048
#define TCM_VHOST_PREALLOC_UPAGES 2048
#define TCM_VHOST_PREALLOC_PROT_SGLS 512

struct vhost_scsi_inflight {
	/* Wait for the flush operation to finish */
	struct completion comp;
	/* Refcount for the inflight reqs */
	struct kref kref;
};

struct tcm_vhost_cmd {
	/* Descriptor from vhost_get_vq_desc() for virt_queue segment */
	int tvc_vq_desc;
	/* virtio-scsi initiator task attribute */
	int tvc_task_attr;
	/* virtio-scsi initiator data direction */
	enum dma_data_direction tvc_data_direction;
	/* Expected data transfer length from virtio-scsi header */
	u32 tvc_exp_data_len;
	/* The Tag from include/linux/virtio_scsi.h:struct virtio_scsi_cmd_req */
	u64 tvc_tag;
	/* The number of scatterlists associated with this cmd */
	u32 tvc_sgl_count;
	u32 tvc_prot_sgl_count;
	/* Saved unpacked SCSI LUN for tcm_vhost_submission_work() */
	u32 tvc_lun;
	/* Pointer to the SGL formatted memory from virtio-scsi */
	struct scatterlist *tvc_sgl;
	struct scatterlist *tvc_prot_sgl;
	struct page **tvc_upages;
	/* Pointer to response */
	struct virtio_scsi_cmd_resp __user *tvc_resp;
	/* Pointer to vhost_scsi for our device */
	struct vhost_scsi *tvc_vhost;
	/* Pointer to vhost_virtqueue for the cmd */
	struct vhost_virtqueue *tvc_vq;
	/* Pointer to vhost nexus memory */
	struct tcm_vhost_nexus *tvc_nexus;
	/* The TCM I/O descriptor that is accessed via container_of() */
	struct se_cmd tvc_se_cmd;
	/* work item used for cmwq dispatch to tcm_vhost_submission_work() */
	struct work_struct work;
	/* Copy of the incoming SCSI command descriptor block (CDB) */
	unsigned char tvc_cdb[TCM_VHOST_MAX_CDB_SIZE];
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char tvc_sense_buf[TRANSPORT_SENSE_BUFFER];
	/* Completed commands list, serviced from vhost worker thread */
	struct llist_node tvc_completion_list;
	/* Used to track inflight cmd */
	struct vhost_scsi_inflight *inflight;
};

struct tcm_vhost_nexus {
	/* Pointer to TCM session for I_T Nexus */
	struct se_session *tvn_se_sess;
};

struct tcm_vhost_nacl {
	/* Binary World Wide unique Port Name for Vhost Initiator port */
	u64 iport_wwpn;
	/* ASCII formatted WWPN for Sas Initiator port */
	char iport_name[TCM_VHOST_NAMELEN];
	/* Returned by tcm_vhost_make_nodeacl() */
	struct se_node_acl se_node_acl;
};

struct tcm_vhost_tpg {
	/* Vhost port target portal group tag for TCM */
	u16 tport_tpgt;
	/* Used to track number of TPG Port/Lun Links wrt to explict I_T Nexus shutdown */
	int tv_tpg_port_count;
	/* Used for vhost_scsi device reference to tpg_nexus, protected by tv_tpg_mutex */
	int tv_tpg_vhost_count;
	/* list for tcm_vhost_list */
	struct list_head tv_tpg_list;
	/* Used to protect access for tpg_nexus */
	struct mutex tv_tpg_mutex;
	/* Pointer to the TCM VHost I_T Nexus for this TPG endpoint */
	struct tcm_vhost_nexus *tpg_nexus;
	/* Pointer back to tcm_vhost_tport */
	struct tcm_vhost_tport *tport;
	/* Returned by tcm_vhost_make_tpg() */
	struct se_portal_group se_tpg;
	/* Pointer back to vhost_scsi, protected by tv_tpg_mutex */
	struct vhost_scsi *vhost_scsi;
};

struct tcm_vhost_tport {
	/* SCSI protocol the tport is providing */
	u8 tport_proto_id;
	/* Binary World Wide unique Port Name for Vhost Target port */
	u64 tport_wwpn;
	/* ASCII formatted WWPN for Vhost Target port */
	char tport_name[TCM_VHOST_NAMELEN];
	/* Returned by tcm_vhost_make_tport() */
	struct se_wwn tport_wwn;
};

struct tcm_vhost_evt {
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
};

struct vhost_scsi {
	/* Protected by vhost_scsi->dev.mutex */
	struct tcm_vhost_tpg **vs_tpg;
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

static void tcm_vhost_done_inflight(struct kref *kref)
{
	struct vhost_scsi_inflight *inflight;

	inflight = container_of(kref, struct vhost_scsi_inflight, kref);
	complete(&inflight->comp);
}

static void tcm_vhost_init_inflight(struct vhost_scsi *vs,
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
tcm_vhost_get_inflight(struct vhost_virtqueue *vq)
{
	struct vhost_scsi_inflight *inflight;
	struct vhost_scsi_virtqueue *svq;

	svq = container_of(vq, struct vhost_scsi_virtqueue, vq);
	inflight = &svq->inflights[svq->inflight_idx];
	kref_get(&inflight->kref);

	return inflight;
}

static void tcm_vhost_put_inflight(struct vhost_scsi_inflight *inflight)
{
	kref_put(&inflight->kref, tcm_vhost_done_inflight);
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

static u32
tcm_vhost_get_pr_transport_id(struct se_portal_group *se_tpg,
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

static u32
tcm_vhost_get_pr_transport_id_len(struct se_portal_group *se_tpg,
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

static char *
tcm_vhost_parse_pr_out_transport_id(struct se_portal_group *se_tpg,
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

static struct se_node_acl *
tcm_vhost_alloc_fabric_acl(struct se_portal_group *se_tpg)
{
	struct tcm_vhost_nacl *nacl;

	nacl = kzalloc(sizeof(struct tcm_vhost_nacl), GFP_KERNEL);
	if (!nacl) {
		pr_err("Unable to allocate struct tcm_vhost_nacl\n");
		return NULL;
	}

	return &nacl->se_node_acl;
}

static void
tcm_vhost_release_fabric_acl(struct se_portal_group *se_tpg,
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
	struct tcm_vhost_cmd *tv_cmd = container_of(se_cmd,
				struct tcm_vhost_cmd, tvc_se_cmd);
	struct se_session *se_sess = se_cmd->se_sess;
	int i;

	if (tv_cmd->tvc_sgl_count) {
		for (i = 0; i < tv_cmd->tvc_sgl_count; i++)
			put_page(sg_page(&tv_cmd->tvc_sgl[i]));
	}
	if (tv_cmd->tvc_prot_sgl_count) {
		for (i = 0; i < tv_cmd->tvc_prot_sgl_count; i++)
			put_page(sg_page(&tv_cmd->tvc_prot_sgl[i]));
	}

	tcm_vhost_put_inflight(tv_cmd->inflight);
	percpu_ida_free(&se_sess->sess_tag_pool, se_cmd->map_tag);
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

static void vhost_scsi_complete_cmd(struct tcm_vhost_cmd *cmd)
{
	struct vhost_scsi *vs = cmd->tvc_vhost;

	llist_add(&cmd->tvc_completion_list, &vs->vs_completion_list);

	vhost_work_queue(&vs->dev, &vs->vs_completion_work);
}

static int tcm_vhost_queue_data_in(struct se_cmd *se_cmd)
{
	struct tcm_vhost_cmd *cmd = container_of(se_cmd,
				struct tcm_vhost_cmd, tvc_se_cmd);
	vhost_scsi_complete_cmd(cmd);
	return 0;
}

static int tcm_vhost_queue_status(struct se_cmd *se_cmd)
{
	struct tcm_vhost_cmd *cmd = container_of(se_cmd,
				struct tcm_vhost_cmd, tvc_se_cmd);
	vhost_scsi_complete_cmd(cmd);
	return 0;
}

static void tcm_vhost_queue_tm_rsp(struct se_cmd *se_cmd)
{
	return;
}

static void tcm_vhost_aborted_task(struct se_cmd *se_cmd)
{
	return;
}

static void tcm_vhost_free_evt(struct vhost_scsi *vs, struct tcm_vhost_evt *evt)
{
	vs->vs_events_nr--;
	kfree(evt);
}

static struct tcm_vhost_evt *
tcm_vhost_allocate_evt(struct vhost_scsi *vs,
		       u32 event, u32 reason)
{
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
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

static void vhost_scsi_free_cmd(struct tcm_vhost_cmd *cmd)
{
	struct se_cmd *se_cmd = &cmd->tvc_se_cmd;

	/* TODO locking against target/backend threads? */
	transport_generic_free_cmd(se_cmd, 0);

}

static int vhost_scsi_check_stop_free(struct se_cmd *se_cmd)
{
	return target_put_sess_cmd(se_cmd->se_sess, se_cmd);
}

static void
tcm_vhost_do_evt_work(struct vhost_scsi *vs, struct tcm_vhost_evt *evt)
{
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
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
	struct vhost_virtqueue *vq = &vs->vqs[VHOST_SCSI_VQ_EVT].vq;
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
	struct tcm_vhost_cmd *cmd;
	struct llist_node *llnode;
	struct se_cmd *se_cmd;
	int ret, vq;

	bitmap_zero(signal, VHOST_SCSI_MAX_VQ);
	llnode = llist_del_all(&vs->vs_completion_list);
	while (llnode) {
		cmd = llist_entry(llnode, struct tcm_vhost_cmd,
				     tvc_completion_list);
		llnode = llist_next(llnode);
		se_cmd = &cmd->tvc_se_cmd;

		pr_debug("%s tv_cmd %p resid %u status %#02x\n", __func__,
			cmd, se_cmd->residual_count, se_cmd->scsi_status);

		memset(&v_rsp, 0, sizeof(v_rsp));
		v_rsp.resid = se_cmd->residual_count;
		/* TODO is status_qualifier field needed? */
		v_rsp.status = se_cmd->scsi_status;
		v_rsp.sense_len = se_cmd->scsi_sense_length;
		memcpy(v_rsp.sense, cmd->tvc_sense_buf,
		       v_rsp.sense_len);
		ret = copy_to_user(cmd->tvc_resp, &v_rsp, sizeof(v_rsp));
		if (likely(ret == 0)) {
			struct vhost_scsi_virtqueue *q;
			vhost_add_used(cmd->tvc_vq, cmd->tvc_vq_desc, 0);
			q = container_of(cmd->tvc_vq, struct vhost_scsi_virtqueue, vq);
			vq = q - vs->vqs;
			__set_bit(vq, signal);
		} else
			pr_err("Faulted on virtio_scsi_cmd_resp\n");

		vhost_scsi_free_cmd(cmd);
	}

	vq = -1;
	while ((vq = find_next_bit(signal, VHOST_SCSI_MAX_VQ, vq + 1))
		< VHOST_SCSI_MAX_VQ)
		vhost_signal(&vs->dev, &vs->vqs[vq].vq);
}

static struct tcm_vhost_cmd *
vhost_scsi_get_tag(struct vhost_virtqueue *vq, struct tcm_vhost_tpg *tpg,
		   unsigned char *cdb, u64 scsi_tag, u16 lun, u8 task_attr,
		   u32 exp_data_len, int data_direction)
{
	struct tcm_vhost_cmd *cmd;
	struct tcm_vhost_nexus *tv_nexus;
	struct se_session *se_sess;
	struct scatterlist *sg, *prot_sg;
	struct page **pages;
	int tag;

	tv_nexus = tpg->tpg_nexus;
	if (!tv_nexus) {
		pr_err("Unable to locate active struct tcm_vhost_nexus\n");
		return ERR_PTR(-EIO);
	}
	se_sess = tv_nexus->tvn_se_sess;

	tag = percpu_ida_alloc(&se_sess->sess_tag_pool, TASK_RUNNING);
	if (tag < 0) {
		pr_err("Unable to obtain tag for tcm_vhost_cmd\n");
		return ERR_PTR(-ENOMEM);
	}

	cmd = &((struct tcm_vhost_cmd *)se_sess->sess_cmd_map)[tag];
	sg = cmd->tvc_sgl;
	prot_sg = cmd->tvc_prot_sgl;
	pages = cmd->tvc_upages;
	memset(cmd, 0, sizeof(struct tcm_vhost_cmd));

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
	cmd->inflight = tcm_vhost_get_inflight(vq);

	memcpy(cmd->tvc_cdb, cdb, TCM_VHOST_MAX_CDB_SIZE);

	return cmd;
}

/*
 * Map a user memory range into a scatterlist
 *
 * Returns the number of scatterlist entries used or -errno on error.
 */
static int
vhost_scsi_map_to_sgl(struct tcm_vhost_cmd *tv_cmd,
		      struct scatterlist *sgl,
		      unsigned int sgl_count,
		      struct iovec *iov,
		      struct page **pages,
		      bool write)
{
	unsigned int npages = 0, pages_nr, offset, nbytes;
	struct scatterlist *sg = sgl;
	void __user *ptr = iov->iov_base;
	size_t len = iov->iov_len;
	int ret, i;

	pages_nr = iov_num_pages(iov);
	if (pages_nr > sgl_count) {
		pr_err("vhost_scsi_map_to_sgl() pages_nr: %u greater than"
		       " sgl_count: %u\n", pages_nr, sgl_count);
		return -ENOBUFS;
	}
	if (pages_nr > TCM_VHOST_PREALLOC_UPAGES) {
		pr_err("vhost_scsi_map_to_sgl() pages_nr: %u greater than"
		       " preallocated TCM_VHOST_PREALLOC_UPAGES: %u\n",
			pages_nr, TCM_VHOST_PREALLOC_UPAGES);
		return -ENOBUFS;
	}

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
	return ret;
}

static int
vhost_scsi_map_iov_to_sgl(struct tcm_vhost_cmd *cmd,
			  struct iovec *iov,
			  int niov,
			  bool write)
{
	struct scatterlist *sg = cmd->tvc_sgl;
	unsigned int sgl_count = 0;
	int ret, i;

	for (i = 0; i < niov; i++)
		sgl_count += iov_num_pages(&iov[i]);

	if (sgl_count > TCM_VHOST_PREALLOC_SGLS) {
		pr_err("vhost_scsi_map_iov_to_sgl() sgl_count: %u greater than"
			" preallocated TCM_VHOST_PREALLOC_SGLS: %u\n",
			sgl_count, TCM_VHOST_PREALLOC_SGLS);
		return -ENOBUFS;
	}

	pr_debug("%s sg %p sgl_count %u\n", __func__, sg, sgl_count);
	sg_init_table(sg, sgl_count);
	cmd->tvc_sgl_count = sgl_count;

	pr_debug("Mapping iovec %p for %u pages\n", &iov[0], sgl_count);

	for (i = 0; i < niov; i++) {
		ret = vhost_scsi_map_to_sgl(cmd, sg, sgl_count, &iov[i],
					    cmd->tvc_upages, write);
		if (ret < 0) {
			for (i = 0; i < cmd->tvc_sgl_count; i++)
				put_page(sg_page(&cmd->tvc_sgl[i]));

			cmd->tvc_sgl_count = 0;
			return ret;
		}
		sg += ret;
		sgl_count -= ret;
	}
	return 0;
}

static int
vhost_scsi_map_iov_to_prot(struct tcm_vhost_cmd *cmd,
			   struct iovec *iov,
			   int niov,
			   bool write)
{
	struct scatterlist *prot_sg = cmd->tvc_prot_sgl;
	unsigned int prot_sgl_count = 0;
	int ret, i;

	for (i = 0; i < niov; i++)
		prot_sgl_count += iov_num_pages(&iov[i]);

	if (prot_sgl_count > TCM_VHOST_PREALLOC_PROT_SGLS) {
		pr_err("vhost_scsi_map_iov_to_prot() sgl_count: %u greater than"
			" preallocated TCM_VHOST_PREALLOC_PROT_SGLS: %u\n",
			prot_sgl_count, TCM_VHOST_PREALLOC_PROT_SGLS);
		return -ENOBUFS;
	}

	pr_debug("%s prot_sg %p prot_sgl_count %u\n", __func__,
		 prot_sg, prot_sgl_count);
	sg_init_table(prot_sg, prot_sgl_count);
	cmd->tvc_prot_sgl_count = prot_sgl_count;

	for (i = 0; i < niov; i++) {
		ret = vhost_scsi_map_to_sgl(cmd, prot_sg, prot_sgl_count, &iov[i],
					    cmd->tvc_upages, write);
		if (ret < 0) {
			for (i = 0; i < cmd->tvc_prot_sgl_count; i++)
				put_page(sg_page(&cmd->tvc_prot_sgl[i]));

			cmd->tvc_prot_sgl_count = 0;
			return ret;
		}
		prot_sg += ret;
		prot_sgl_count -= ret;
	}
	return 0;
}

static void tcm_vhost_submission_work(struct work_struct *work)
{
	struct tcm_vhost_cmd *cmd =
		container_of(work, struct tcm_vhost_cmd, work);
	struct tcm_vhost_nexus *tv_nexus;
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

	rc = target_submit_cmd_map_sgls(se_cmd, tv_nexus->tvn_se_sess,
			cmd->tvc_cdb, &cmd->tvc_sense_buf[0],
			cmd->tvc_lun, cmd->tvc_exp_data_len,
			cmd->tvc_task_attr, cmd->tvc_data_direction,
			TARGET_SCF_ACK_KREF, sg_ptr, cmd->tvc_sgl_count,
			NULL, 0, sg_prot_ptr, cmd->tvc_prot_sgl_count);
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

static void
vhost_scsi_handle_vq(struct vhost_scsi *vs, struct vhost_virtqueue *vq)
{
	struct tcm_vhost_tpg **vs_tpg;
	struct virtio_scsi_cmd_req v_req;
	struct virtio_scsi_cmd_req_pi v_req_pi;
	struct tcm_vhost_tpg *tpg;
	struct tcm_vhost_cmd *cmd;
	u64 tag;
	u32 exp_data_len, data_first, data_num, data_direction, prot_first;
	unsigned out, in, i;
	int head, ret, data_niov, prot_niov, prot_bytes;
	size_t req_size;
	u16 lun;
	u8 *target, *lunp, task_attr;
	bool hdr_pi;
	void *req, *cdb;

	mutex_lock(&vq->mutex);
	/*
	 * We can handle the vq only after the endpoint is setup by calling the
	 * VHOST_SCSI_SET_ENDPOINT ioctl.
	 */
	vs_tpg = vq->private_data;
	if (!vs_tpg)
		goto out;

	vhost_disable_notify(&vs->dev, vq);

	for (;;) {
		head = vhost_get_vq_desc(vq, vq->iov,
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

		if (vhost_has_feature(vq, VIRTIO_SCSI_F_T10_PI)) {
			req = &v_req_pi;
			lunp = &v_req_pi.lun[0];
			target = &v_req_pi.lun[1];
			req_size = sizeof(v_req_pi);
			hdr_pi = true;
		} else {
			req = &v_req;
			lunp = &v_req.lun[0];
			target = &v_req.lun[1];
			req_size = sizeof(v_req);
			hdr_pi = false;
		}

		if (unlikely(vq->iov[0].iov_len < req_size)) {
			pr_err("Expecting virtio-scsi header: %zu, got %zu\n",
			       req_size, vq->iov[0].iov_len);
			break;
		}
		ret = memcpy_fromiovecend(req, &vq->iov[0], 0, req_size);
		if (unlikely(ret)) {
			vq_err(vq, "Faulted on virtio_scsi_cmd_req\n");
			break;
		}

		/* virtio-scsi spec requires byte 0 of the lun to be 1 */
		if (unlikely(*lunp != 1)) {
			vhost_scsi_send_bad_target(vs, vq, head, out);
			continue;
		}

		tpg = ACCESS_ONCE(vs_tpg[*target]);

		/* Target does not exist, fail the request */
		if (unlikely(!tpg)) {
			vhost_scsi_send_bad_target(vs, vq, head, out);
			continue;
		}

		data_niov = data_num;
		prot_niov = prot_first = prot_bytes = 0;
		/*
		 * Determine if any protection information iovecs are preceeding
		 * the actual data payload, and adjust data_first + data_niov
		 * values accordingly for vhost_scsi_map_iov_to_sgl() below.
		 *
		 * Also extract virtio_scsi header bits for vhost_scsi_get_tag()
		 */
		if (hdr_pi) {
			if (v_req_pi.pi_bytesout) {
				if (data_direction != DMA_TO_DEVICE) {
					vq_err(vq, "Received non zero do_pi_niov"
						", but wrong data_direction\n");
					goto err_cmd;
				}
				prot_bytes = v_req_pi.pi_bytesout;
			} else if (v_req_pi.pi_bytesin) {
				if (data_direction != DMA_FROM_DEVICE) {
					vq_err(vq, "Received non zero di_pi_niov"
						", but wrong data_direction\n");
					goto err_cmd;
				}
				prot_bytes = v_req_pi.pi_bytesin;
			}
			if (prot_bytes) {
				int tmp = 0;

				for (i = 0; i < data_num; i++) {
					tmp += vq->iov[data_first + i].iov_len;
					prot_niov++;
					if (tmp >= prot_bytes)
						break;
				}
				prot_first = data_first;
				data_first += prot_niov;
				data_niov = data_num - prot_niov;
			}
			tag = v_req_pi.tag;
			task_attr = v_req_pi.task_attr;
			cdb = &v_req_pi.cdb[0];
			lun = ((v_req_pi.lun[2] << 8) | v_req_pi.lun[3]) & 0x3FFF;
		} else {
			tag = v_req.tag;
			task_attr = v_req.task_attr;
			cdb = &v_req.cdb[0];
			lun = ((v_req.lun[2] << 8) | v_req.lun[3]) & 0x3FFF;
		}
		exp_data_len = 0;
		for (i = 0; i < data_niov; i++)
			exp_data_len += vq->iov[data_first + i].iov_len;
		/*
		 * Check that the recieved CDB size does not exceeded our
		 * hardcoded max for vhost-scsi
		 *
		 * TODO what if cdb was too small for varlen cdb header?
		 */
		if (unlikely(scsi_command_size(cdb) > TCM_VHOST_MAX_CDB_SIZE)) {
			vq_err(vq, "Received SCSI CDB with command_size: %d that"
				" exceeds SCSI_MAX_VARLEN_CDB_SIZE: %d\n",
				scsi_command_size(cdb), TCM_VHOST_MAX_CDB_SIZE);
			goto err_cmd;
		}

		cmd = vhost_scsi_get_tag(vq, tpg, cdb, tag, lun, task_attr,
					 exp_data_len + prot_bytes,
					 data_direction);
		if (IS_ERR(cmd)) {
			vq_err(vq, "vhost_scsi_get_tag failed %ld\n",
					PTR_ERR(cmd));
			goto err_cmd;
		}

		pr_debug("Allocated tv_cmd: %p exp_data_len: %d, data_direction"
			": %d\n", cmd, exp_data_len, data_direction);

		cmd->tvc_vhost = vs;
		cmd->tvc_vq = vq;
		cmd->tvc_resp = vq->iov[out].iov_base;

		pr_debug("vhost_scsi got command opcode: %#02x, lun: %d\n",
			cmd->tvc_cdb[0], cmd->tvc_lun);

		if (prot_niov) {
			ret = vhost_scsi_map_iov_to_prot(cmd,
					&vq->iov[prot_first], prot_niov,
					data_direction == DMA_FROM_DEVICE);
			if (unlikely(ret)) {
				vq_err(vq, "Failed to map iov to"
					" prot_sgl\n");
				goto err_free;
			}
		}
		if (data_direction != DMA_NONE) {
			ret = vhost_scsi_map_iov_to_sgl(cmd,
					&vq->iov[data_first], data_niov,
					data_direction == DMA_FROM_DEVICE);
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
		cmd->tvc_vq_desc = head;
		/*
		 * Dispatch tv_cmd descriptor for cmwq execution in process
		 * context provided by tcm_vhost_workqueue.  This also ensures
		 * tv_cmd is executed on the same kworker CPU as this vhost
		 * thread to gain positive L2 cache locality effects..
		 */
		INIT_WORK(&cmd->work, tcm_vhost_submission_work);
		queue_work(tcm_vhost_workqueue, &cmd->work);
	}

	mutex_unlock(&vq->mutex);
	return;

err_free:
	vhost_scsi_free_cmd(cmd);
err_cmd:
	vhost_scsi_send_bad_target(vs, vq, head, out);
out:
	mutex_unlock(&vq->mutex);
}

static void vhost_scsi_ctl_handle_kick(struct vhost_work *work)
{
	pr_debug("%s: The handling func for control queue.\n", __func__);
}

static void
tcm_vhost_send_evt(struct vhost_scsi *vs,
		   struct tcm_vhost_tpg *tpg,
		   struct se_lun *lun,
		   u32 event,
		   u32 reason)
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
	vhost_poll_flush(&vs->vqs[index].vq.poll);
}

/* Callers must hold dev mutex */
static void vhost_scsi_flush(struct vhost_scsi *vs)
{
	struct vhost_scsi_inflight *old_inflight[VHOST_SCSI_MAX_VQ];
	int i;

	/* Init new inflight and remember the old inflight */
	tcm_vhost_init_inflight(vs, old_inflight);

	/*
	 * The inflight->kref was initialized to 1. We decrement it here to
	 * indicate the start of the flush operation so that it will reach 0
	 * when all the reqs are finished.
	 */
	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		kref_put(&old_inflight[i]->kref, tcm_vhost_done_inflight);

	/* Flush both the vhost poll and vhost work */
	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		vhost_scsi_flush_vq(vs, i);
	vhost_work_flush(&vs->dev, &vs->vs_completion_work);
	vhost_work_flush(&vs->dev, &vs->vs_event_work);

	/* Wait for all reqs issued before the flush to be finished */
	for (i = 0; i < VHOST_SCSI_MAX_VQ; i++)
		wait_for_completion(&old_inflight[i]->comp);
}

/*
 * Called from vhost_scsi_ioctl() context to walk the list of available
 * tcm_vhost_tpg with an active struct tcm_vhost_nexus
 *
 *  The lock nesting rule is:
 *    tcm_vhost_mutex -> vs->dev.mutex -> tpg->tv_tpg_mutex -> vq->mutex
 */
static int
vhost_scsi_set_endpoint(struct vhost_scsi *vs,
			struct vhost_scsi_target *t)
{
	struct tcm_vhost_tport *tv_tport;
	struct tcm_vhost_tpg *tpg;
	struct tcm_vhost_tpg **vs_tpg;
	struct vhost_virtqueue *vq;
	int index, ret, i, len;
	bool match = false;

	mutex_lock(&tcm_vhost_mutex);
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

	list_for_each_entry(tpg, &tcm_vhost_list, tv_tpg_list) {
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
				kfree(vs_tpg);
				mutex_unlock(&tpg->tv_tpg_mutex);
				ret = -EEXIST;
				goto out;
			}
			tpg->tv_tpg_vhost_count++;
			tpg->vhost_scsi = vs;
			vs_tpg[tpg->tport_tpgt] = tpg;
			smp_mb__after_atomic();
			match = true;
		}
		mutex_unlock(&tpg->tv_tpg_mutex);
	}

	if (match) {
		memcpy(vs->vs_vhost_wwpn, t->vhost_wwpn,
		       sizeof(vs->vs_vhost_wwpn));
		for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i].vq;
			mutex_lock(&vq->mutex);
			vq->private_data = vs_tpg;
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

static int
vhost_scsi_clear_endpoint(struct vhost_scsi *vs,
			  struct vhost_scsi_target *t)
{
	struct tcm_vhost_tport *tv_tport;
	struct tcm_vhost_tpg *tpg;
	struct vhost_virtqueue *vq;
	bool match = false;
	int index, ret, i;
	u8 target;

	mutex_lock(&tcm_vhost_mutex);
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
	}
	if (match) {
		for (i = 0; i < VHOST_SCSI_MAX_VQ; i++) {
			vq = &vs->vqs[i].vq;
			mutex_lock(&vq->mutex);
			vq->private_data = NULL;
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
	mutex_unlock(&tpg->tv_tpg_mutex);
err_dev:
	mutex_unlock(&vs->dev.mutex);
	mutex_unlock(&tcm_vhost_mutex);
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

	vs = kzalloc(sizeof(*vs), GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	if (!vs) {
		vs = vzalloc(sizeof(*vs));
		if (!vs)
			goto err_vs;
	}

	vqs = kmalloc(VHOST_SCSI_MAX_VQ * sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vqs;

	vhost_work_init(&vs->vs_completion_work, vhost_scsi_complete_cmd_work);
	vhost_work_init(&vs->vs_event_work, tcm_vhost_evt_work);

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
	vhost_dev_init(&vs->dev, vqs, VHOST_SCSI_MAX_VQ);

	tcm_vhost_init_inflight(vs, NULL);

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
	vhost_dev_cleanup(&vs->dev, false);
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

static void
tcm_vhost_do_plug(struct tcm_vhost_tpg *tpg,
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
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);

	mutex_lock(&tcm_vhost_mutex);

	mutex_lock(&tpg->tv_tpg_mutex);
	tpg->tv_tpg_port_count++;
	mutex_unlock(&tpg->tv_tpg_mutex);

	tcm_vhost_hotplug(tpg, lun);

	mutex_unlock(&tcm_vhost_mutex);

	return 0;
}

static void tcm_vhost_port_unlink(struct se_portal_group *se_tpg,
				  struct se_lun *lun)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);

	mutex_lock(&tcm_vhost_mutex);

	mutex_lock(&tpg->tv_tpg_mutex);
	tpg->tv_tpg_port_count--;
	mutex_unlock(&tpg->tv_tpg_mutex);

	tcm_vhost_hotunplug(tpg, lun);

	mutex_unlock(&tcm_vhost_mutex);
}

static struct se_node_acl *
tcm_vhost_make_nodeacl(struct se_portal_group *se_tpg,
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

static void tcm_vhost_free_cmd_map_res(struct tcm_vhost_nexus *nexus,
				       struct se_session *se_sess)
{
	struct tcm_vhost_cmd *tv_cmd;
	unsigned int i;

	if (!se_sess->sess_cmd_map)
		return;

	for (i = 0; i < TCM_VHOST_DEFAULT_TAGS; i++) {
		tv_cmd = &((struct tcm_vhost_cmd *)se_sess->sess_cmd_map)[i];

		kfree(tv_cmd->tvc_sgl);
		kfree(tv_cmd->tvc_prot_sgl);
		kfree(tv_cmd->tvc_upages);
	}
}

static int tcm_vhost_make_nexus(struct tcm_vhost_tpg *tpg,
				const char *name)
{
	struct se_portal_group *se_tpg;
	struct se_session *se_sess;
	struct tcm_vhost_nexus *tv_nexus;
	struct tcm_vhost_cmd *tv_cmd;
	unsigned int i;

	mutex_lock(&tpg->tv_tpg_mutex);
	if (tpg->tpg_nexus) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_debug("tpg->tpg_nexus already exists\n");
		return -EEXIST;
	}
	se_tpg = &tpg->se_tpg;

	tv_nexus = kzalloc(sizeof(struct tcm_vhost_nexus), GFP_KERNEL);
	if (!tv_nexus) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_err("Unable to allocate struct tcm_vhost_nexus\n");
		return -ENOMEM;
	}
	/*
	 *  Initialize the struct se_session pointer and setup tagpool
	 *  for struct tcm_vhost_cmd descriptors
	 */
	tv_nexus->tvn_se_sess = transport_init_session_tags(
					TCM_VHOST_DEFAULT_TAGS,
					sizeof(struct tcm_vhost_cmd),
					TARGET_PROT_DIN_PASS | TARGET_PROT_DOUT_PASS);
	if (IS_ERR(tv_nexus->tvn_se_sess)) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		kfree(tv_nexus);
		return -ENOMEM;
	}
	se_sess = tv_nexus->tvn_se_sess;
	for (i = 0; i < TCM_VHOST_DEFAULT_TAGS; i++) {
		tv_cmd = &((struct tcm_vhost_cmd *)se_sess->sess_cmd_map)[i];

		tv_cmd->tvc_sgl = kzalloc(sizeof(struct scatterlist) *
					TCM_VHOST_PREALLOC_SGLS, GFP_KERNEL);
		if (!tv_cmd->tvc_sgl) {
			mutex_unlock(&tpg->tv_tpg_mutex);
			pr_err("Unable to allocate tv_cmd->tvc_sgl\n");
			goto out;
		}

		tv_cmd->tvc_upages = kzalloc(sizeof(struct page *) *
					TCM_VHOST_PREALLOC_UPAGES, GFP_KERNEL);
		if (!tv_cmd->tvc_upages) {
			mutex_unlock(&tpg->tv_tpg_mutex);
			pr_err("Unable to allocate tv_cmd->tvc_upages\n");
			goto out;
		}

		tv_cmd->tvc_prot_sgl = kzalloc(sizeof(struct scatterlist) *
					TCM_VHOST_PREALLOC_PROT_SGLS, GFP_KERNEL);
		if (!tv_cmd->tvc_prot_sgl) {
			mutex_unlock(&tpg->tv_tpg_mutex);
			pr_err("Unable to allocate tv_cmd->tvc_prot_sgl\n");
			goto out;
		}
	}
	/*
	 * Since we are running in 'demo mode' this call with generate a
	 * struct se_node_acl for the tcm_vhost struct se_portal_group with
	 * the SCSI Initiator port name of the passed configfs group 'name'.
	 */
	tv_nexus->tvn_se_sess->se_node_acl = core_tpg_check_initiator_node_acl(
				se_tpg, (unsigned char *)name);
	if (!tv_nexus->tvn_se_sess->se_node_acl) {
		mutex_unlock(&tpg->tv_tpg_mutex);
		pr_debug("core_tpg_check_initiator_node_acl() failed"
				" for %s\n", name);
		goto out;
	}
	/*
	 * Now register the TCM vhost virtual I_T Nexus as active with the
	 * call to __transport_register_session()
	 */
	__transport_register_session(se_tpg, tv_nexus->tvn_se_sess->se_node_acl,
			tv_nexus->tvn_se_sess, tv_nexus);
	tpg->tpg_nexus = tv_nexus;

	mutex_unlock(&tpg->tv_tpg_mutex);
	return 0;

out:
	tcm_vhost_free_cmd_map_res(tv_nexus, se_sess);
	transport_free_session(se_sess);
	kfree(tv_nexus);
	return -ENOMEM;
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

	tcm_vhost_free_cmd_map_res(tv_nexus, se_sess);
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
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_nexus *tv_nexus;
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

static ssize_t tcm_vhost_tpg_store_nexus(struct se_portal_group *se_tpg,
					 const char *page,
					 size_t count)
{
	struct tcm_vhost_tpg *tpg = container_of(se_tpg,
				struct tcm_vhost_tpg, se_tpg);
	struct tcm_vhost_tport *tport_wwn = tpg->tport;
	unsigned char i_port[TCM_VHOST_NAMELEN], *ptr, *port_ptr;
	int ret;
	/*
	 * Shutdown the active I_T nexus if 'NULL' is passed..
	 */
	if (!strncmp(page, "NULL", 4)) {
		ret = tcm_vhost_drop_nexus(tpg);
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

	ret = tcm_vhost_make_nexus(tpg, port_ptr);
	if (ret < 0)
		return ret;

	return count;
}

TF_TPG_BASE_ATTR(tcm_vhost, nexus, S_IRUGO | S_IWUSR);

static struct configfs_attribute *tcm_vhost_tpg_attrs[] = {
	&tcm_vhost_tpg_nexus.attr,
	NULL,
};

static struct se_portal_group *
tcm_vhost_make_tpg(struct se_wwn *wwn,
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

static struct se_wwn *
tcm_vhost_make_tport(struct target_fabric_configfs *tf,
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

static ssize_t
tcm_vhost_wwn_show_attr_version(struct target_fabric_configfs *tf,
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
	.check_stop_free		= vhost_scsi_check_stop_free,
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
	.aborted_task			= tcm_vhost_aborted_task,
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
	fabric->tf_cit_tmpl.tfc_wwn_cit.ct_attrs = tcm_vhost_wwn_attrs;
	fabric->tf_cit_tmpl.tfc_tpg_base_cit.ct_attrs = tcm_vhost_tpg_attrs;
	fabric->tf_cit_tmpl.tfc_tpg_attrib_cit.ct_attrs = NULL;
	fabric->tf_cit_tmpl.tfc_tpg_param_cit.ct_attrs = NULL;
	fabric->tf_cit_tmpl.tfc_tpg_np_base_cit.ct_attrs = NULL;
	fabric->tf_cit_tmpl.tfc_tpg_nacl_base_cit.ct_attrs = NULL;
	fabric->tf_cit_tmpl.tfc_tpg_nacl_attrib_cit.ct_attrs = NULL;
	fabric->tf_cit_tmpl.tfc_tpg_nacl_auth_cit.ct_attrs = NULL;
	fabric->tf_cit_tmpl.tfc_tpg_nacl_param_cit.ct_attrs = NULL;
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

MODULE_DESCRIPTION("VHOST_SCSI series fabric driver");
MODULE_ALIAS("tcm_vhost");
MODULE_LICENSE("GPL");
module_init(tcm_vhost_init);
module_exit(tcm_vhost_exit);

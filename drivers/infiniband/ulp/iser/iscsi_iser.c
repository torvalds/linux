/*
 * iSCSI Initiator over iSER Data-Path
 *
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 Mike Christie
 * Copyright (c) 2005, 2006 Voltaire, Inc. All rights reserved.
 * maintained by openib-general@openib.org
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Credits:
 *	Christoph Hellwig
 *	FUJITA Tomonori
 *	Arne Redlich
 *	Zhenyu Wang
 * Modified by:
 *      Erez Zilber
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/hardirq.h>
#include <linux/kfifo.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>

#include <net/sock.h>

#include <asm/uaccess.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "iscsi_iser.h"

static struct scsi_host_template iscsi_iser_sht;
static struct iscsi_transport iscsi_iser_transport;
static struct scsi_transport_template *iscsi_iser_scsi_transport;

static unsigned int iscsi_max_lun = 512;
module_param_named(max_lun, iscsi_max_lun, uint, S_IRUGO);

int iser_debug_level = 0;

MODULE_DESCRIPTION("iSER (iSCSI Extensions for RDMA) Datamover "
		   "v" DRV_VER " (" DRV_DATE ")");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Alex Nezhinsky, Dan Bar Dov, Or Gerlitz");

module_param_named(debug_level, iser_debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0 (default:disabled)");

struct iser_global ig;

void
iscsi_iser_recv(struct iscsi_conn *conn,
		struct iscsi_hdr *hdr, char *rx_data, int rx_data_len)
{
	int rc = 0;
	int datalen;
	int ahslen;

	/* verify PDU length */
	datalen = ntoh24(hdr->dlength);
	if (datalen != rx_data_len) {
		printk(KERN_ERR "iscsi_iser: datalen %d (hdr) != %d (IB) \n",
		       datalen, rx_data_len);
		rc = ISCSI_ERR_DATALEN;
		goto error;
	}

	/* read AHS */
	ahslen = hdr->hlength * 4;

	rc = iscsi_complete_pdu(conn, hdr, rx_data, rx_data_len);
	if (rc && rc != ISCSI_ERR_NO_SCSI_CMD)
		goto error;

	return;
error:
	iscsi_conn_failure(conn, rc);
}

static int iscsi_iser_pdu_alloc(struct iscsi_task *task, uint8_t opcode)
{
	struct iscsi_iser_task *iser_task = task->dd_data;

	task->hdr = (struct iscsi_hdr *)&iser_task->desc.iscsi_header;
	task->hdr_max = sizeof(iser_task->desc.iscsi_header);
	return 0;
}

/**
 * iscsi_iser_task_init - Initialize task
 * @task: iscsi task
 *
 * Initialize the task for the scsi command or mgmt command.
 */
static int
iscsi_iser_task_init(struct iscsi_task *task)
{
	struct iscsi_iser_conn *iser_conn  = task->conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;

	/* mgmt task */
	if (!task->sc) {
		iser_task->desc.data = task->data;
		return 0;
	}

	iser_task->command_sent = 0;
	iser_task->iser_conn    = iser_conn;
	iser_task_rdma_init(iser_task);
	return 0;
}

/**
 * iscsi_iser_mtask_xmit - xmit management(immediate) task
 * @conn: iscsi connection
 * @task: task management task
 *
 * Notes:
 *	The function can return -EAGAIN in which case caller must
 *	call it again later, or recover. '0' return code means successful
 *	xmit.
 *
 **/
static int
iscsi_iser_mtask_xmit(struct iscsi_conn *conn, struct iscsi_task *task)
{
	int error = 0;

	iser_dbg("task deq [cid %d itt 0x%x]\n", conn->id, task->itt);

	error = iser_send_control(conn, task);

	/* since iser xmits control with zero copy, tasks can not be recycled
	 * right after sending them.
	 * The recycling scheme is based on whether a response is expected
	 * - if yes, the task is recycled at iscsi_complete_pdu
	 * - if no,  the task is recycled at iser_snd_completion
	 */
	if (error && error != -ENOBUFS)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);

	return error;
}

static int
iscsi_iser_task_xmit_unsol_data(struct iscsi_conn *conn,
				 struct iscsi_task *task)
{
	struct iscsi_r2t_info *r2t = &task->unsol_r2t;
	struct iscsi_data hdr;
	int error = 0;

	/* Send data-out PDUs while there's still unsolicited data to send */
	while (iscsi_task_has_unsol_data(task)) {
		iscsi_prep_data_out_pdu(task, r2t, &hdr);
		iser_dbg("Sending data-out: itt 0x%x, data count %d\n",
			   hdr.itt, r2t->data_count);

		/* the buffer description has been passed with the command */
		/* Send the command */
		error = iser_send_data_out(conn, task, &hdr);
		if (error) {
			r2t->datasn--;
			goto iscsi_iser_task_xmit_unsol_data_exit;
		}
		r2t->sent += r2t->data_count;
		iser_dbg("Need to send %d more as data-out PDUs\n",
			   r2t->data_length - r2t->sent);
	}

iscsi_iser_task_xmit_unsol_data_exit:
	return error;
}

static int
iscsi_iser_task_xmit(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_iser_task *iser_task = task->dd_data;
	int error = 0;

	if (!task->sc)
		return iscsi_iser_mtask_xmit(conn, task);

	if (task->sc->sc_data_direction == DMA_TO_DEVICE) {
		BUG_ON(scsi_bufflen(task->sc) == 0);

		iser_dbg("cmd [itt %x total %d imm %d unsol_data %d\n",
			   task->itt, scsi_bufflen(task->sc),
			   task->imm_count, task->unsol_r2t.data_length);
	}

	iser_dbg("task deq [cid %d itt 0x%x]\n",
		   conn->id, task->itt);

	/* Send the cmd PDU */
	if (!iser_task->command_sent) {
		error = iser_send_command(conn, task);
		if (error)
			goto iscsi_iser_task_xmit_exit;
		iser_task->command_sent = 1;
	}

	/* Send unsolicited data-out PDU(s) if necessary */
	if (iscsi_task_has_unsol_data(task))
		error = iscsi_iser_task_xmit_unsol_data(conn, task);

 iscsi_iser_task_xmit_exit:
	if (error && error != -ENOBUFS)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	return error;
}

static void iscsi_iser_cleanup_task(struct iscsi_task *task)
{
	struct iscsi_iser_task *iser_task = task->dd_data;

	/* mgmt tasks do not need special cleanup */
	if (!task->sc)
		return;

	if (iser_task->status == ISER_TASK_STATUS_STARTED) {
		iser_task->status = ISER_TASK_STATUS_COMPLETED;
		iser_task_rdma_finalize(iser_task);
	}
}

static struct iscsi_cls_conn *
iscsi_iser_conn_create(struct iscsi_cls_session *cls_session, uint32_t conn_idx)
{
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_iser_conn *iser_conn;

	cls_conn = iscsi_conn_setup(cls_session, sizeof(*iser_conn), conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;

	/*
	 * due to issues with the login code re iser sematics
	 * this not set in iscsi_conn_setup - FIXME
	 */
	conn->max_recv_dlength = 128;

	iser_conn = conn->dd_data;
	conn->dd_data = iser_conn;
	iser_conn->iscsi_conn = conn;

	return cls_conn;
}

static void
iscsi_iser_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_iser_conn *iser_conn = conn->dd_data;
	struct iser_conn *ib_conn = iser_conn->ib_conn;

	iscsi_conn_teardown(cls_conn);
	/*
	 * Userspace will normally call the stop callback and
	 * already have freed the ib_conn, but if it goofed up then
	 * we free it here.
	 */
	if (ib_conn) {
		ib_conn->iser_conn = NULL;
		iser_conn_put(ib_conn);
	}
}

static int
iscsi_iser_conn_bind(struct iscsi_cls_session *cls_session,
		     struct iscsi_cls_conn *cls_conn, uint64_t transport_eph,
		     int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_iser_conn *iser_conn;
	struct iser_conn *ib_conn;
	struct iscsi_endpoint *ep;
	int error;

	error = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (error)
		return error;

	/* the transport ep handle comes from user space so it must be
	 * verified against the global ib connections list */
	ep = iscsi_lookup_endpoint(transport_eph);
	if (!ep) {
		iser_err("can't bind eph %llx\n",
			 (unsigned long long)transport_eph);
		return -EINVAL;
	}
	ib_conn = ep->dd_data;

	/* binds the iSER connection retrieved from the previously
	 * connected ep_handle to the iSCSI layer connection. exchanges
	 * connection pointers */
	iser_err("binding iscsi conn %p to iser_conn %p\n",conn,ib_conn);
	iser_conn = conn->dd_data;
	ib_conn->iser_conn = iser_conn;
	iser_conn->ib_conn  = ib_conn;
	iser_conn_get(ib_conn);
	return 0;
}

static void
iscsi_iser_conn_stop(struct iscsi_cls_conn *cls_conn, int flag)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_iser_conn *iser_conn = conn->dd_data;
	struct iser_conn *ib_conn = iser_conn->ib_conn;

	/*
	 * Userspace may have goofed up and not bound the connection or
	 * might have only partially setup the connection.
	 */
	if (ib_conn) {
		iscsi_conn_stop(cls_conn, flag);
		/*
		 * There is no unbind event so the stop callback
		 * must release the ref from the bind.
		 */
		iser_conn_put(ib_conn);
	}
	iser_conn->ib_conn = NULL;
}

static int
iscsi_iser_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	int err;

	err = iser_conn_set_full_featured_mode(conn);
	if (err)
		return err;

	return iscsi_conn_start(cls_conn);
}

static void iscsi_iser_session_destroy(struct iscsi_cls_session *cls_session)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);

	iscsi_session_teardown(cls_session);
	iscsi_host_remove(shost);
	iscsi_host_free(shost);
}

static struct iscsi_cls_session *
iscsi_iser_session_create(struct iscsi_endpoint *ep,
			  uint16_t cmds_max, uint16_t qdepth,
			  uint32_t initial_cmdsn)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	struct Scsi_Host *shost;
	struct iser_conn *ib_conn;

	shost = iscsi_host_alloc(&iscsi_iser_sht, 0, 1);
	if (!shost)
		return NULL;
	shost->transportt = iscsi_iser_scsi_transport;
	shost->max_lun = iscsi_max_lun;
	shost->max_id = 0;
	shost->max_channel = 0;
	shost->max_cmd_len = 16;

	/*
	 * older userspace tools (before 2.0-870) did not pass us
	 * the leading conn's ep so this will be NULL;
	 */
	if (ep)
		ib_conn = ep->dd_data;

	if (iscsi_host_add(shost,
			   ep ? ib_conn->device->ib_device->dma_device : NULL))
		goto free_host;

	/*
	 * we do not support setting can_queue cmd_per_lun from userspace yet
	 * because we preallocate so many resources
	 */
	cls_session = iscsi_session_setup(&iscsi_iser_transport, shost,
					  ISCSI_DEF_XMIT_CMDS_MAX, 0,
					  sizeof(struct iscsi_iser_task),
					  initial_cmdsn, 0);
	if (!cls_session)
		goto remove_host;
	session = cls_session->dd_data;

	shost->can_queue = session->scsi_cmds_max;
	return cls_session;

remove_host:
	iscsi_host_remove(shost);
free_host:
	iscsi_host_free(shost);
	return NULL;
}

static int
iscsi_iser_set_param(struct iscsi_cls_conn *cls_conn,
		     enum iscsi_param param, char *buf, int buflen)
{
	int value;

	switch (param) {
	case ISCSI_PARAM_MAX_RECV_DLENGTH:
		/* TBD */
		break;
	case ISCSI_PARAM_HDRDGST_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			printk(KERN_ERR "DataDigest wasn't negotiated to None");
			return -EPROTO;
		}
		break;
	case ISCSI_PARAM_DATADGST_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			printk(KERN_ERR "DataDigest wasn't negotiated to None");
			return -EPROTO;
		}
		break;
	case ISCSI_PARAM_IFMARKER_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			printk(KERN_ERR "IFMarker wasn't negotiated to No");
			return -EPROTO;
		}
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			printk(KERN_ERR "OFMarker wasn't negotiated to No");
			return -EPROTO;
		}
		break;
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}

	return 0;
}

static void
iscsi_iser_conn_get_stats(struct iscsi_cls_conn *cls_conn, struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;

	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt; /* always 0 */
	stats->r2t_pdus = conn->r2t_pdus_cnt; /* always 0 */
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
	stats->custom_length = 4;
	strcpy(stats->custom[0].desc, "qp_tx_queue_full");
	stats->custom[0].value = 0; /* TB iser_conn->qp_tx_queue_full; */
	strcpy(stats->custom[1].desc, "fmr_map_not_avail");
	stats->custom[1].value = 0; /* TB iser_conn->fmr_map_not_avail */;
	strcpy(stats->custom[2].desc, "eh_abort_cnt");
	stats->custom[2].value = conn->eh_abort_cnt;
	strcpy(stats->custom[3].desc, "fmr_unalign_cnt");
	stats->custom[3].value = conn->fmr_unalign_cnt;
}

static struct iscsi_endpoint *
iscsi_iser_ep_connect(struct Scsi_Host *shost, struct sockaddr *dst_addr,
		      int non_blocking)
{
	int err;
	struct iser_conn *ib_conn;
	struct iscsi_endpoint *ep;

	ep = iscsi_create_endpoint(sizeof(*ib_conn));
	if (!ep)
		return ERR_PTR(-ENOMEM);

	ib_conn = ep->dd_data;
	ib_conn->ep = ep;
	iser_conn_init(ib_conn);

	err = iser_connect(ib_conn, NULL, (struct sockaddr_in *)dst_addr,
			   non_blocking);
	if (err) {
		iscsi_destroy_endpoint(ep);
		return ERR_PTR(err);
	}
	return ep;
}

static int
iscsi_iser_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct iser_conn *ib_conn;
	int rc;

	ib_conn = ep->dd_data;
	rc = wait_event_interruptible_timeout(ib_conn->wait,
			     ib_conn->state == ISER_CONN_UP,
			     msecs_to_jiffies(timeout_ms));

	/* if conn establishment failed, return error code to iscsi */
	if (!rc &&
	    (ib_conn->state == ISER_CONN_TERMINATING ||
	     ib_conn->state == ISER_CONN_DOWN))
		rc = -1;

	iser_err("ib conn %p rc = %d\n", ib_conn, rc);

	if (rc > 0)
		return 1; /* success, this is the equivalent of POLLOUT */
	else if (!rc)
		return 0; /* timeout */
	else
		return rc; /* signal */
}

static void
iscsi_iser_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct iser_conn *ib_conn;

	ib_conn = ep->dd_data;
	if (ib_conn->iser_conn)
		/*
		 * Must suspend xmit path if the ep is bound to the
		 * iscsi_conn, so we know we are not accessing the ib_conn
		 * when we free it.
		 *
		 * This may not be bound if the ep poll failed.
		 */
		iscsi_suspend_tx(ib_conn->iser_conn->iscsi_conn);


	iser_err("ib conn %p state %d\n",ib_conn, ib_conn->state);
	iser_conn_terminate(ib_conn);
}

static struct scsi_host_template iscsi_iser_sht = {
	.module                 = THIS_MODULE,
	.name                   = "iSCSI Initiator over iSER, v." DRV_VER,
	.queuecommand           = iscsi_queuecommand,
	.change_queue_depth	= iscsi_change_queue_depth,
	.sg_tablesize           = ISCSI_ISER_SG_TABLESIZE,
	.max_sectors		= 1024,
	.cmd_per_lun            = ISER_DEF_CMD_PER_LUN,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_device_reset_handler= iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.target_alloc		= iscsi_target_alloc,
	.use_clustering         = DISABLE_CLUSTERING,
	.proc_name              = "iscsi_iser",
	.this_id                = -1,
};

static struct iscsi_transport iscsi_iser_transport = {
	.owner                  = THIS_MODULE,
	.name                   = "iser",
	.caps                   = CAP_RECOVERY_L0 | CAP_MULTI_R2T,
	.param_mask		= ISCSI_MAX_RECV_DLENGTH |
				  ISCSI_MAX_XMIT_DLENGTH |
				  ISCSI_HDRDGST_EN |
				  ISCSI_DATADGST_EN |
				  ISCSI_INITIAL_R2T_EN |
				  ISCSI_MAX_R2T |
				  ISCSI_IMM_DATA_EN |
				  ISCSI_FIRST_BURST |
				  ISCSI_MAX_BURST |
				  ISCSI_PDU_INORDER_EN |
				  ISCSI_DATASEQ_INORDER_EN |
				  ISCSI_EXP_STATSN |
				  ISCSI_PERSISTENT_PORT |
				  ISCSI_PERSISTENT_ADDRESS |
				  ISCSI_TARGET_NAME | ISCSI_TPGT |
				  ISCSI_USERNAME | ISCSI_PASSWORD |
				  ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN |
				  ISCSI_FAST_ABORT | ISCSI_ABORT_TMO |
				  ISCSI_LU_RESET_TMO | ISCSI_TGT_RESET_TMO |
				  ISCSI_PING_TMO | ISCSI_RECV_TMO |
				  ISCSI_IFACE_NAME | ISCSI_INITIATOR_NAME,
	.host_param_mask	= ISCSI_HOST_HWADDRESS |
				  ISCSI_HOST_NETDEV_NAME |
				  ISCSI_HOST_INITIATOR_NAME,
	/* session management */
	.create_session         = iscsi_iser_session_create,
	.destroy_session        = iscsi_iser_session_destroy,
	/* connection management */
	.create_conn            = iscsi_iser_conn_create,
	.bind_conn              = iscsi_iser_conn_bind,
	.destroy_conn           = iscsi_iser_conn_destroy,
	.set_param              = iscsi_iser_set_param,
	.get_conn_param		= iscsi_conn_get_param,
	.get_session_param	= iscsi_session_get_param,
	.start_conn             = iscsi_iser_conn_start,
	.stop_conn              = iscsi_iser_conn_stop,
	/* iscsi host params */
	.get_host_param		= iscsi_host_get_param,
	.set_host_param		= iscsi_host_set_param,
	/* IO */
	.send_pdu		= iscsi_conn_send_pdu,
	.get_stats		= iscsi_iser_conn_get_stats,
	.init_task		= iscsi_iser_task_init,
	.xmit_task		= iscsi_iser_task_xmit,
	.cleanup_task		= iscsi_iser_cleanup_task,
	.alloc_pdu		= iscsi_iser_pdu_alloc,
	/* recovery */
	.session_recovery_timedout = iscsi_session_recovery_timedout,

	.ep_connect             = iscsi_iser_ep_connect,
	.ep_poll                = iscsi_iser_ep_poll,
	.ep_disconnect          = iscsi_iser_ep_disconnect
};

static int __init iser_init(void)
{
	int err;

	iser_dbg("Starting iSER datamover...\n");

	if (iscsi_max_lun < 1) {
		printk(KERN_ERR "Invalid max_lun value of %u\n", iscsi_max_lun);
		return -EINVAL;
	}

	memset(&ig, 0, sizeof(struct iser_global));

	ig.desc_cache = kmem_cache_create("iser_descriptors",
					  sizeof (struct iser_desc),
					  0, SLAB_HWCACHE_ALIGN,
					  NULL);
	if (ig.desc_cache == NULL)
		return -ENOMEM;

	/* device init is called only after the first addr resolution */
	mutex_init(&ig.device_list_mutex);
	INIT_LIST_HEAD(&ig.device_list);
	mutex_init(&ig.connlist_mutex);
	INIT_LIST_HEAD(&ig.connlist);

	iscsi_iser_scsi_transport = iscsi_register_transport(
							&iscsi_iser_transport);
	if (!iscsi_iser_scsi_transport) {
		iser_err("iscsi_register_transport failed\n");
		err = -EINVAL;
		goto register_transport_failure;
	}

	return 0;

register_transport_failure:
	kmem_cache_destroy(ig.desc_cache);

	return err;
}

static void __exit iser_exit(void)
{
	iser_dbg("Removing iSER datamover...\n");
	iscsi_unregister_transport(&iscsi_iser_transport);
	kmem_cache_destroy(ig.desc_cache);
}

module_init(iser_init);
module_exit(iser_exit);

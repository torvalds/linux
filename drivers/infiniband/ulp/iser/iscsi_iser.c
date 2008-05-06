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
 *
 *
 * $Id: iscsi_iser.c 6965 2006-05-07 11:36:20Z ogerlitz $
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
	uint32_t ret_itt;
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

	/* verify itt (itt encoding: age+cid+itt) */
	rc = iscsi_verify_itt(conn, hdr, &ret_itt);

	if (!rc)
		rc = iscsi_complete_pdu(conn, hdr, rx_data, rx_data_len);

	if (rc && rc != ISCSI_ERR_NO_SCSI_CMD)
		goto error;

	return;
error:
	iscsi_conn_failure(conn, rc);
}


/**
 * iscsi_iser_cmd_init - Initialize iSCSI SCSI_READ or SCSI_WRITE commands
 *
 **/
static int
iscsi_iser_cmd_init(struct iscsi_cmd_task *ctask)
{
	struct iscsi_iser_conn     *iser_conn  = ctask->conn->dd_data;
	struct iscsi_iser_cmd_task *iser_ctask = ctask->dd_data;

	iser_ctask->command_sent = 0;
	iser_ctask->iser_conn    = iser_conn;
	iser_ctask_rdma_init(iser_ctask);
	return 0;
}

/**
 * iscsi_mtask_xmit - xmit management(immediate) task
 * @conn: iscsi connection
 * @mtask: task management task
 *
 * Notes:
 *	The function can return -EAGAIN in which case caller must
 *	call it again later, or recover. '0' return code means successful
 *	xmit.
 *
 **/
static int
iscsi_iser_mtask_xmit(struct iscsi_conn *conn,
		      struct iscsi_mgmt_task *mtask)
{
	int error = 0;

	debug_scsi("mtask deq [cid %d itt 0x%x]\n", conn->id, mtask->itt);

	error = iser_send_control(conn, mtask);

	/* since iser xmits control with zero copy, mtasks can not be recycled
	 * right after sending them.
	 * The recycling scheme is based on whether a response is expected
	 * - if yes, the mtask is recycled at iscsi_complete_pdu
	 * - if no,  the mtask is recycled at iser_snd_completion
	 */
	if (error && error != -ENOBUFS)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);

	return error;
}

static int
iscsi_iser_ctask_xmit_unsol_data(struct iscsi_conn *conn,
				 struct iscsi_cmd_task *ctask)
{
	struct iscsi_data  hdr;
	int error = 0;

	/* Send data-out PDUs while there's still unsolicited data to send */
	while (ctask->unsol_count > 0) {
		iscsi_prep_unsolicit_data_pdu(ctask, &hdr);
		debug_scsi("Sending data-out: itt 0x%x, data count %d\n",
			   hdr.itt, ctask->data_count);

		/* the buffer description has been passed with the command */
		/* Send the command */
		error = iser_send_data_out(conn, ctask, &hdr);
		if (error) {
			ctask->unsol_datasn--;
			goto iscsi_iser_ctask_xmit_unsol_data_exit;
		}
		ctask->unsol_count -= ctask->data_count;
		debug_scsi("Need to send %d more as data-out PDUs\n",
			   ctask->unsol_count);
	}

iscsi_iser_ctask_xmit_unsol_data_exit:
	return error;
}

static int
iscsi_iser_ctask_xmit(struct iscsi_conn *conn,
		      struct iscsi_cmd_task *ctask)
{
	struct iscsi_iser_cmd_task *iser_ctask = ctask->dd_data;
	int error = 0;

	if (ctask->sc->sc_data_direction == DMA_TO_DEVICE) {
		BUG_ON(scsi_bufflen(ctask->sc) == 0);

		debug_scsi("cmd [itt %x total %d imm %d unsol_data %d\n",
			   ctask->itt, scsi_bufflen(ctask->sc),
			   ctask->imm_count, ctask->unsol_count);
	}

	debug_scsi("ctask deq [cid %d itt 0x%x]\n",
		   conn->id, ctask->itt);

	/* Send the cmd PDU */
	if (!iser_ctask->command_sent) {
		error = iser_send_command(conn, ctask);
		if (error)
			goto iscsi_iser_ctask_xmit_exit;
		iser_ctask->command_sent = 1;
	}

	/* Send unsolicited data-out PDU(s) if necessary */
	if (ctask->unsol_count)
		error = iscsi_iser_ctask_xmit_unsol_data(conn, ctask);

 iscsi_iser_ctask_xmit_exit:
	if (error && error != -ENOBUFS)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	return error;
}

static void
iscsi_iser_cleanup_ctask(struct iscsi_conn *conn, struct iscsi_cmd_task *ctask)
{
	struct iscsi_iser_cmd_task *iser_ctask = ctask->dd_data;

	if (iser_ctask->status == ISER_TASK_STATUS_STARTED) {
		iser_ctask->status = ISER_TASK_STATUS_COMPLETED;
		iser_ctask_rdma_finalize(iser_ctask);
	}
}

static struct iser_conn *
iscsi_iser_ib_conn_lookup(__u64 ep_handle)
{
	struct iser_conn *ib_conn;
	struct iser_conn *uib_conn = (struct iser_conn *)(unsigned long)ep_handle;

	mutex_lock(&ig.connlist_mutex);
	list_for_each_entry(ib_conn, &ig.connlist, conn_list) {
		if (ib_conn == uib_conn) {
			mutex_unlock(&ig.connlist_mutex);
			return ib_conn;
		}
	}
	mutex_unlock(&ig.connlist_mutex);
	iser_err("no conn exists for eph %llx\n",(unsigned long long)ep_handle);
	return NULL;
}

static struct iscsi_cls_conn *
iscsi_iser_conn_create(struct iscsi_cls_session *cls_session, uint32_t conn_idx)
{
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_iser_conn *iser_conn;

	cls_conn = iscsi_conn_setup(cls_session, conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;

	/*
	 * due to issues with the login code re iser sematics
	 * this not set in iscsi_conn_setup - FIXME
	 */
	conn->max_recv_dlength = 128;

	iser_conn = kzalloc(sizeof(*iser_conn), GFP_KERNEL);
	if (!iser_conn)
		goto conn_alloc_fail;

	/* currently this is the only field which need to be initiated */
	rwlock_init(&iser_conn->lock);

	conn->dd_data = iser_conn;
	iser_conn->iscsi_conn = conn;

	return cls_conn;

conn_alloc_fail:
	iscsi_conn_teardown(cls_conn);
	return NULL;
}

static void
iscsi_iser_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_iser_conn *iser_conn = conn->dd_data;

	iscsi_conn_teardown(cls_conn);
	if (iser_conn->ib_conn)
		iser_conn->ib_conn->iser_conn = NULL;
	kfree(iser_conn);
}

static int
iscsi_iser_conn_bind(struct iscsi_cls_session *cls_session,
		     struct iscsi_cls_conn *cls_conn, uint64_t transport_eph,
		     int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iscsi_iser_conn *iser_conn;
	struct iser_conn *ib_conn;
	int error;

	error = iscsi_conn_bind(cls_session, cls_conn, is_leading);
	if (error)
		return error;

	/* the transport ep handle comes from user space so it must be
	 * verified against the global ib connections list */
	ib_conn = iscsi_iser_ib_conn_lookup(transport_eph);
	if (!ib_conn) {
		iser_err("can't bind eph %llx\n",
			 (unsigned long long)transport_eph);
		return -EINVAL;
	}
	/* binds the iSER connection retrieved from the previously
	 * connected ep_handle to the iSCSI layer connection. exchanges
	 * connection pointers */
	iser_err("binding iscsi conn %p to iser_conn %p\n",conn,ib_conn);
	iser_conn = conn->dd_data;
	ib_conn->iser_conn = iser_conn;
	iser_conn->ib_conn  = ib_conn;

	conn->recv_lock = &iser_conn->lock;

	return 0;
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

static struct iscsi_transport iscsi_iser_transport;

static struct iscsi_cls_session *
iscsi_iser_session_create(struct iscsi_transport *iscsit,
			 struct scsi_transport_template *scsit,
			 uint16_t cmds_max, uint16_t qdepth,
			 uint32_t initial_cmdsn, uint32_t *hostno)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_session *session;
	int i;
	uint32_t hn;
	struct iscsi_cmd_task  *ctask;
	struct iscsi_mgmt_task *mtask;
	struct iscsi_iser_cmd_task *iser_ctask;
	struct iser_desc *desc;

	/*
	 * we do not support setting can_queue cmd_per_lun from userspace yet
	 * because we preallocate so many resources
	 */
	cls_session = iscsi_session_setup(iscsit, scsit,
					  ISCSI_DEF_XMIT_CMDS_MAX,
					  ISCSI_MAX_CMD_PER_LUN,
					  sizeof(struct iscsi_iser_cmd_task),
					  sizeof(struct iser_desc),
					  initial_cmdsn, &hn);
	if (!cls_session)
	return NULL;

	*hostno = hn;
	session = class_to_transport_session(cls_session);

	/* libiscsi setup itts, data and pool so just set desc fields */
	for (i = 0; i < session->cmds_max; i++) {
		ctask      = session->cmds[i];
		iser_ctask = ctask->dd_data;
		ctask->hdr = (struct iscsi_cmd *)&iser_ctask->desc.iscsi_header;
		ctask->hdr_max = sizeof(iser_ctask->desc.iscsi_header);
	}

	for (i = 0; i < session->mgmtpool_max; i++) {
		mtask      = session->mgmt_cmds[i];
		desc       = mtask->dd_data;
		mtask->hdr = &desc->iscsi_header;
		desc->data = mtask->data;
	}

	return cls_session;
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

static int
iscsi_iser_ep_connect(struct sockaddr *dst_addr, int non_blocking,
		      __u64 *ep_handle)
{
	int err;
	struct iser_conn *ib_conn;

	err = iser_conn_init(&ib_conn);
	if (err)
		goto out;

	err = iser_connect(ib_conn, NULL, (struct sockaddr_in *)dst_addr, non_blocking);
	if (!err)
		*ep_handle = (__u64)(unsigned long)ib_conn;

out:
	return err;
}

static int
iscsi_iser_ep_poll(__u64 ep_handle, int timeout_ms)
{
	struct iser_conn *ib_conn = iscsi_iser_ib_conn_lookup(ep_handle);
	int rc;

	if (!ib_conn)
		return -EINVAL;

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
iscsi_iser_ep_disconnect(__u64 ep_handle)
{
	struct iser_conn *ib_conn;

	ib_conn = iscsi_iser_ib_conn_lookup(ep_handle);
	if (!ib_conn)
		return;

	iser_err("ib conn %p state %d\n",ib_conn, ib_conn->state);
	iser_conn_terminate(ib_conn);
}

static struct scsi_host_template iscsi_iser_sht = {
	.module                 = THIS_MODULE,
	.name                   = "iSCSI Initiator over iSER, v." DRV_VER,
	.queuecommand           = iscsi_queuecommand,
	.change_queue_depth	= iscsi_change_queue_depth,
	.can_queue		= ISCSI_DEF_XMIT_CMDS_MAX - 1,
	.sg_tablesize           = ISCSI_ISER_SG_TABLESIZE,
	.max_sectors		= 1024,
	.cmd_per_lun            = ISCSI_MAX_CMD_PER_LUN,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_device_reset_handler= iscsi_eh_device_reset,
	.eh_host_reset_handler	= iscsi_eh_host_reset,
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
				  ISCSI_PING_TMO | ISCSI_RECV_TMO,
	.host_param_mask	= ISCSI_HOST_HWADDRESS |
				  ISCSI_HOST_NETDEV_NAME |
				  ISCSI_HOST_INITIATOR_NAME,
	.host_template          = &iscsi_iser_sht,
	.conndata_size		= sizeof(struct iscsi_conn),
	.max_lun                = ISCSI_ISER_MAX_LUN,
	.max_cmd_len            = ISCSI_ISER_MAX_CMD_LEN,
	/* session management */
	.create_session         = iscsi_iser_session_create,
	.destroy_session        = iscsi_session_teardown,
	/* connection management */
	.create_conn            = iscsi_iser_conn_create,
	.bind_conn              = iscsi_iser_conn_bind,
	.destroy_conn           = iscsi_iser_conn_destroy,
	.set_param              = iscsi_iser_set_param,
	.get_conn_param		= iscsi_conn_get_param,
	.get_session_param	= iscsi_session_get_param,
	.start_conn             = iscsi_iser_conn_start,
	.stop_conn              = iscsi_conn_stop,
	/* iscsi host params */
	.get_host_param		= iscsi_host_get_param,
	.set_host_param		= iscsi_host_set_param,
	/* IO */
	.send_pdu		= iscsi_conn_send_pdu,
	.get_stats		= iscsi_iser_conn_get_stats,
	.init_cmd_task		= iscsi_iser_cmd_init,
	.xmit_cmd_task		= iscsi_iser_ctask_xmit,
	.xmit_mgmt_task		= iscsi_iser_mtask_xmit,
	.cleanup_cmd_task	= iscsi_iser_cleanup_ctask,
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

	iscsi_iser_transport.max_lun = iscsi_max_lun;

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

	if (!iscsi_register_transport(&iscsi_iser_transport)) {
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

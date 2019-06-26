/*
 * iSCSI Initiator over iSER Data-Path
 *
 * Copyright (C) 2004 Dmitry Yusupov
 * Copyright (C) 2004 Alex Aizman
 * Copyright (C) 2005 Mike Christie
 * Copyright (c) 2005, 2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2013-2014 Mellanox Technologies. All rights reserved.
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
#include <linux/slab.h>
#include <linux/module.h>

#include <net/sock.h>

#include <linux/uaccess.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport_iscsi.h>

#include "iscsi_iser.h"

MODULE_DESCRIPTION("iSER (iSCSI Extensions for RDMA) Datamover");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Alex Nezhinsky, Dan Bar Dov, Or Gerlitz");

static struct scsi_host_template iscsi_iser_sht;
static struct iscsi_transport iscsi_iser_transport;
static struct scsi_transport_template *iscsi_iser_scsi_transport;
static struct workqueue_struct *release_wq;
static DEFINE_MUTEX(unbind_iser_conn_mutex);
struct iser_global ig;

int iser_debug_level = 0;
module_param_named(debug_level, iser_debug_level, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0 (default:disabled)");

static unsigned int iscsi_max_lun = 512;
module_param_named(max_lun, iscsi_max_lun, uint, S_IRUGO);
MODULE_PARM_DESC(max_lun, "Max LUNs to allow per session (default:512");

unsigned int iser_max_sectors = ISER_DEF_MAX_SECTORS;
module_param_named(max_sectors, iser_max_sectors, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_sectors, "Max number of sectors in a single scsi command (default:1024");

bool iser_always_reg = true;
module_param_named(always_register, iser_always_reg, bool, S_IRUGO);
MODULE_PARM_DESC(always_register,
		 "Always register memory, even for continuous memory regions (default:true)");

bool iser_pi_enable = false;
module_param_named(pi_enable, iser_pi_enable, bool, S_IRUGO);
MODULE_PARM_DESC(pi_enable, "Enable T10-PI offload support (default:disabled)");

int iser_pi_guard;
module_param_named(pi_guard, iser_pi_guard, int, S_IRUGO);
MODULE_PARM_DESC(pi_guard, "T10-PI guard_type [deprecated]");

/*
 * iscsi_iser_recv() - Process a successful recv completion
 * @conn:         iscsi connection
 * @hdr:          iscsi header
 * @rx_data:      buffer containing receive data payload
 * @rx_data_len:  length of rx_data
 *
 * Notes: In case of data length errors or iscsi PDU completion failures
 *        this routine will signal iscsi layer of connection failure.
 */
void
iscsi_iser_recv(struct iscsi_conn *conn, struct iscsi_hdr *hdr,
		char *rx_data, int rx_data_len)
{
	int rc = 0;
	int datalen;

	/* verify PDU length */
	datalen = ntoh24(hdr->dlength);
	if (datalen > rx_data_len || (datalen + 4) < rx_data_len) {
		iser_err("wrong datalen %d (hdr), %d (IB)\n",
			datalen, rx_data_len);
		rc = ISCSI_ERR_DATALEN;
		goto error;
	}

	if (datalen != rx_data_len)
		iser_dbg("aligned datalen (%d) hdr, %d (IB)\n",
			datalen, rx_data_len);

	rc = iscsi_complete_pdu(conn, hdr, rx_data, rx_data_len);
	if (rc && rc != ISCSI_ERR_NO_SCSI_CMD)
		goto error;

	return;
error:
	iscsi_conn_failure(conn, rc);
}

/**
 * iscsi_iser_pdu_alloc() - allocate an iscsi-iser PDU
 * @task:     iscsi task
 * @opcode:   iscsi command opcode
 *
 * Netes: This routine can't fail, just assign iscsi task
 *        hdr and max hdr size.
 */
static int
iscsi_iser_pdu_alloc(struct iscsi_task *task, uint8_t opcode)
{
	struct iscsi_iser_task *iser_task = task->dd_data;

	task->hdr = (struct iscsi_hdr *)&iser_task->desc.iscsi_header;
	task->hdr_max = sizeof(iser_task->desc.iscsi_header);

	return 0;
}

/**
 * iser_initialize_task_headers() - Initialize task headers
 * @task:       iscsi task
 * @tx_desc:    iser tx descriptor
 *
 * Notes:
 * This routine may race with iser teardown flow for scsi
 * error handling TMFs. So for TMF we should acquire the
 * state mutex to avoid dereferencing the IB device which
 * may have already been terminated.
 */
int
iser_initialize_task_headers(struct iscsi_task *task,
			     struct iser_tx_desc *tx_desc)
{
	struct iser_conn *iser_conn = task->conn->dd_data;
	struct iser_device *device = iser_conn->ib_conn.device;
	struct iscsi_iser_task *iser_task = task->dd_data;
	u64 dma_addr;
	const bool mgmt_task = !task->sc && !in_interrupt();
	int ret = 0;

	if (unlikely(mgmt_task))
		mutex_lock(&iser_conn->state_mutex);

	if (unlikely(iser_conn->state != ISER_CONN_UP)) {
		ret = -ENODEV;
		goto out;
	}

	dma_addr = ib_dma_map_single(device->ib_device, (void *)tx_desc,
				ISER_HEADERS_LEN, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(device->ib_device, dma_addr)) {
		ret = -ENOMEM;
		goto out;
	}

	tx_desc->wr_idx = 0;
	tx_desc->mapped = true;
	tx_desc->dma_addr = dma_addr;
	tx_desc->tx_sg[0].addr   = tx_desc->dma_addr;
	tx_desc->tx_sg[0].length = ISER_HEADERS_LEN;
	tx_desc->tx_sg[0].lkey   = device->pd->local_dma_lkey;

	iser_task->iser_conn = iser_conn;
out:
	if (unlikely(mgmt_task))
		mutex_unlock(&iser_conn->state_mutex);

	return ret;
}

/**
 * iscsi_iser_task_init() - Initialize iscsi-iser task
 * @task: iscsi task
 *
 * Initialize the task for the scsi command or mgmt command.
 *
 * Return: Returns zero on success or -ENOMEM when failing
 *         to init task headers (dma mapping error).
 */
static int
iscsi_iser_task_init(struct iscsi_task *task)
{
	struct iscsi_iser_task *iser_task = task->dd_data;
	int ret;

	ret = iser_initialize_task_headers(task, &iser_task->desc);
	if (ret) {
		iser_err("Failed to init task %p, err = %d\n",
			 iser_task, ret);
		return ret;
	}

	/* mgmt task */
	if (!task->sc)
		return 0;

	iser_task->command_sent = 0;
	iser_task_rdma_init(iser_task);
	iser_task->sc = task->sc;

	return 0;
}

/**
 * iscsi_iser_mtask_xmit() - xmit management (immediate) task
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

	iser_dbg("mtask xmit [cid %d itt 0x%x]\n", conn->id, task->itt);

	error = iser_send_control(conn, task);

	/* since iser xmits control with zero copy, tasks can not be recycled
	 * right after sending them.
	 * The recycling scheme is based on whether a response is expected
	 * - if yes, the task is recycled at iscsi_complete_pdu
	 * - if no,  the task is recycled at iser_snd_completion
	 */
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

/**
 * iscsi_iser_task_xmit() - xmit iscsi-iser task
 * @task: iscsi task
 *
 * Return: zero on success or escalates $error on failure.
 */
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

	iser_dbg("ctask xmit [cid %d itt 0x%x]\n",
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
	return error;
}

/**
 * iscsi_iser_cleanup_task() - cleanup an iscsi-iser task
 * @task: iscsi task
 *
 * Notes: In case the RDMA device is already NULL (might have
 *        been removed in DEVICE_REMOVAL CM event it will bail-out
 *        without doing dma unmapping.
 */
static void iscsi_iser_cleanup_task(struct iscsi_task *task)
{
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_tx_desc *tx_desc = &iser_task->desc;
	struct iser_conn *iser_conn = task->conn->dd_data;
	struct iser_device *device = iser_conn->ib_conn.device;

	/* DEVICE_REMOVAL event might have already released the device */
	if (!device)
		return;

	if (likely(tx_desc->mapped)) {
		ib_dma_unmap_single(device->ib_device, tx_desc->dma_addr,
				    ISER_HEADERS_LEN, DMA_TO_DEVICE);
		tx_desc->mapped = false;
	}

	/* mgmt tasks do not need special cleanup */
	if (!task->sc)
		return;

	if (iser_task->status == ISER_TASK_STATUS_STARTED) {
		iser_task->status = ISER_TASK_STATUS_COMPLETED;
		iser_task_rdma_finalize(iser_task);
	}
}

/**
 * iscsi_iser_check_protection() - check protection information status of task.
 * @task:     iscsi task
 * @sector:   error sector if exsists (output)
 *
 * Return: zero if no data-integrity errors have occured
 *         0x1: data-integrity error occured in the guard-block
 *         0x2: data-integrity error occured in the reference tag
 *         0x3: data-integrity error occured in the application tag
 *
 *         In addition the error sector is marked.
 */
static u8
iscsi_iser_check_protection(struct iscsi_task *task, sector_t *sector)
{
	struct iscsi_iser_task *iser_task = task->dd_data;

	if (iser_task->dir[ISER_DIR_IN])
		return iser_check_task_pi_status(iser_task, ISER_DIR_IN,
						 sector);
	else
		return iser_check_task_pi_status(iser_task, ISER_DIR_OUT,
						 sector);
}

/**
 * iscsi_iser_conn_create() - create a new iscsi-iser connection
 * @cls_session: iscsi class connection
 * @conn_idx:    connection index within the session (for MCS)
 *
 * Return: iscsi_cls_conn when iscsi_conn_setup succeeds or NULL
 *         otherwise.
 */
static struct iscsi_cls_conn *
iscsi_iser_conn_create(struct iscsi_cls_session *cls_session,
		       uint32_t conn_idx)
{
	struct iscsi_conn *conn;
	struct iscsi_cls_conn *cls_conn;

	cls_conn = iscsi_conn_setup(cls_session, 0, conn_idx);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;

	/*
	 * due to issues with the login code re iser sematics
	 * this not set in iscsi_conn_setup - FIXME
	 */
	conn->max_recv_dlength = ISER_RECV_DATA_SEG_LEN;

	return cls_conn;
}

/**
 * iscsi_iser_conn_bind() - bind iscsi and iser connection structures
 * @cls_session:     iscsi class session
 * @cls_conn:        iscsi class connection
 * @transport_eph:   transport end-point handle
 * @is_leading:      indicate if this is the session leading connection (MCS)
 *
 * Return: zero on success, $error if iscsi_conn_bind fails and
 *         -EINVAL in case end-point doesn't exsits anymore or iser connection
 *         state is not UP (teardown already started).
 */
static int
iscsi_iser_conn_bind(struct iscsi_cls_session *cls_session,
		     struct iscsi_cls_conn *cls_conn,
		     uint64_t transport_eph,
		     int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iser_conn *iser_conn;
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
	iser_conn = ep->dd_data;

	mutex_lock(&iser_conn->state_mutex);
	if (iser_conn->state != ISER_CONN_UP) {
		error = -EINVAL;
		iser_err("iser_conn %p state is %d, teardown started\n",
			 iser_conn, iser_conn->state);
		goto out;
	}

	error = iser_alloc_rx_descriptors(iser_conn, conn->session);
	if (error)
		goto out;

	/* binds the iSER connection retrieved from the previously
	 * connected ep_handle to the iSCSI layer connection. exchanges
	 * connection pointers */
	iser_info("binding iscsi conn %p to iser_conn %p\n", conn, iser_conn);

	conn->dd_data = iser_conn;
	iser_conn->iscsi_conn = conn;

out:
	mutex_unlock(&iser_conn->state_mutex);
	return error;
}

/**
 * iscsi_iser_conn_start() - start iscsi-iser connection
 * @cls_conn: iscsi class connection
 *
 * Notes: Here iser intialize (or re-initialize) stop_completion as
 *        from this point iscsi must call conn_stop in session/connection
 *        teardown so iser transport must wait for it.
 */
static int
iscsi_iser_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *iscsi_conn;
	struct iser_conn *iser_conn;

	iscsi_conn = cls_conn->dd_data;
	iser_conn = iscsi_conn->dd_data;
	reinit_completion(&iser_conn->stop_completion);

	return iscsi_conn_start(cls_conn);
}

/**
 * iscsi_iser_conn_stop() - stop iscsi-iser connection
 * @cls_conn:  iscsi class connection
 * @flag:      indicate if recover or terminate (passed as is)
 *
 * Notes: Calling iscsi_conn_stop might theoretically race with
 *        DEVICE_REMOVAL event and dereference a previously freed RDMA device
 *        handle, so we call it under iser the state lock to protect against
 *        this kind of race.
 */
static void
iscsi_iser_conn_stop(struct iscsi_cls_conn *cls_conn, int flag)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct iser_conn *iser_conn = conn->dd_data;

	iser_info("stopping iscsi_conn: %p, iser_conn: %p\n", conn, iser_conn);

	/*
	 * Userspace may have goofed up and not bound the connection or
	 * might have only partially setup the connection.
	 */
	if (iser_conn) {
		mutex_lock(&iser_conn->state_mutex);
		mutex_lock(&unbind_iser_conn_mutex);
		iser_conn_terminate(iser_conn);
		iscsi_conn_stop(cls_conn, flag);

		/* unbind */
		iser_conn->iscsi_conn = NULL;
		conn->dd_data = NULL;
		mutex_unlock(&unbind_iser_conn_mutex);

		complete(&iser_conn->stop_completion);
		mutex_unlock(&iser_conn->state_mutex);
	} else {
		iscsi_conn_stop(cls_conn, flag);
	}
}

/**
 * iscsi_iser_session_destroy() - destroy iscsi-iser session
 * @cls_session: iscsi class session
 *
 * Removes and free iscsi host.
 */
static void
iscsi_iser_session_destroy(struct iscsi_cls_session *cls_session)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);

	iscsi_session_teardown(cls_session);
	iscsi_host_remove(shost);
	iscsi_host_free(shost);
}

static inline unsigned int
iser_dif_prot_caps(int prot_caps)
{
	return ((prot_caps & IB_PROT_T10DIF_TYPE_1) ?
		SHOST_DIF_TYPE1_PROTECTION | SHOST_DIX_TYPE0_PROTECTION |
		SHOST_DIX_TYPE1_PROTECTION : 0) |
	       ((prot_caps & IB_PROT_T10DIF_TYPE_2) ?
		SHOST_DIF_TYPE2_PROTECTION | SHOST_DIX_TYPE2_PROTECTION : 0) |
	       ((prot_caps & IB_PROT_T10DIF_TYPE_3) ?
		SHOST_DIF_TYPE3_PROTECTION | SHOST_DIX_TYPE3_PROTECTION : 0);
}

/**
 * iscsi_iser_session_create() - create an iscsi-iser session
 * @ep:             iscsi end-point handle
 * @cmds_max:       maximum commands in this session
 * @qdepth:         session command queue depth
 * @initial_cmdsn:  initiator command sequnce number
 *
 * Allocates and adds a scsi host, expose DIF supprot if
 * exists, and sets up an iscsi session.
 */
static struct iscsi_cls_session *
iscsi_iser_session_create(struct iscsi_endpoint *ep,
			  uint16_t cmds_max, uint16_t qdepth,
			  uint32_t initial_cmdsn)
{
	struct iscsi_cls_session *cls_session;
	struct Scsi_Host *shost;
	struct iser_conn *iser_conn = NULL;
	struct ib_conn *ib_conn;
	u32 max_fr_sectors;

	shost = iscsi_host_alloc(&iscsi_iser_sht, 0, 0);
	if (!shost)
		return NULL;
	shost->transportt = iscsi_iser_scsi_transport;
	shost->cmd_per_lun = qdepth;
	shost->max_lun = iscsi_max_lun;
	shost->max_id = 0;
	shost->max_channel = 0;
	shost->max_cmd_len = 16;

	/*
	 * older userspace tools (before 2.0-870) did not pass us
	 * the leading conn's ep so this will be NULL;
	 */
	if (ep) {
		iser_conn = ep->dd_data;
		shost->sg_tablesize = iser_conn->scsi_sg_tablesize;
		shost->can_queue = min_t(u16, cmds_max, iser_conn->max_cmds);

		mutex_lock(&iser_conn->state_mutex);
		if (iser_conn->state != ISER_CONN_UP) {
			iser_err("iser conn %p already started teardown\n",
				 iser_conn);
			mutex_unlock(&iser_conn->state_mutex);
			goto free_host;
		}

		ib_conn = &iser_conn->ib_conn;
		if (ib_conn->pi_support) {
			u32 sig_caps = ib_conn->device->ib_device->attrs.sig_prot_cap;

			scsi_host_set_prot(shost, iser_dif_prot_caps(sig_caps));
			scsi_host_set_guard(shost, SHOST_DIX_GUARD_IP |
						   SHOST_DIX_GUARD_CRC);
		}

		if (iscsi_host_add(shost,
				   ib_conn->device->ib_device->dev.parent)) {
			mutex_unlock(&iser_conn->state_mutex);
			goto free_host;
		}
		mutex_unlock(&iser_conn->state_mutex);
	} else {
		shost->can_queue = min_t(u16, cmds_max, ISER_DEF_XMIT_CMDS_MAX);
		if (iscsi_host_add(shost, NULL))
			goto free_host;
	}

	max_fr_sectors = (shost->sg_tablesize * PAGE_SIZE) >> 9;
	shost->max_sectors = min(iser_max_sectors, max_fr_sectors);

	iser_dbg("iser_conn %p, sg_tablesize %u, max_sectors %u\n",
		 iser_conn, shost->sg_tablesize,
		 shost->max_sectors);

	if (shost->max_sectors < iser_max_sectors)
		iser_warn("max_sectors was reduced from %u to %u\n",
			  iser_max_sectors, shost->max_sectors);

	cls_session = iscsi_session_setup(&iscsi_iser_transport, shost,
					  shost->can_queue, 0,
					  sizeof(struct iscsi_iser_task),
					  initial_cmdsn, 0);
	if (!cls_session)
		goto remove_host;

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
			iser_err("DataDigest wasn't negotiated to None\n");
			return -EPROTO;
		}
		break;
	case ISCSI_PARAM_DATADGST_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			iser_err("DataDigest wasn't negotiated to None\n");
			return -EPROTO;
		}
		break;
	case ISCSI_PARAM_IFMARKER_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			iser_err("IFMarker wasn't negotiated to No\n");
			return -EPROTO;
		}
		break;
	case ISCSI_PARAM_OFMARKER_EN:
		sscanf(buf, "%d", &value);
		if (value) {
			iser_err("OFMarker wasn't negotiated to No\n");
			return -EPROTO;
		}
		break;
	default:
		return iscsi_set_param(cls_conn, param, buf, buflen);
	}

	return 0;
}

/**
 * iscsi_iser_set_param() - set class connection parameter
 * @cls_conn:    iscsi class connection
 * @stats:       iscsi stats to output
 *
 * Output connection statistics.
 */
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
	stats->custom_length = 0;
}

static int iscsi_iser_get_ep_param(struct iscsi_endpoint *ep,
				   enum iscsi_param param, char *buf)
{
	struct iser_conn *iser_conn = ep->dd_data;
	int len;

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
	case ISCSI_PARAM_CONN_ADDRESS:
		if (!iser_conn || !iser_conn->ib_conn.cma_id)
			return -ENOTCONN;

		return iscsi_conn_get_addr_param((struct sockaddr_storage *)
				&iser_conn->ib_conn.cma_id->route.addr.dst_addr,
				param, buf);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}

/**
 * iscsi_iser_ep_connect() - Initiate iSER connection establishment
 * @shost:          scsi_host
 * @dst_addr:       destination address
 * @non-blocking:   indicate if routine can block
 *
 * Allocate an iscsi endpoint, an iser_conn structure and bind them.
 * After that start RDMA connection establishment via rdma_cm. We
 * don't allocate iser_conn embedded in iscsi_endpoint since in teardown
 * the endpoint will be destroyed at ep_disconnect while iser_conn will
 * cleanup its resources asynchronuously.
 *
 * Return: iscsi_endpoint created by iscsi layer or ERR_PTR(error)
 *         if fails.
 */
static struct iscsi_endpoint *
iscsi_iser_ep_connect(struct Scsi_Host *shost, struct sockaddr *dst_addr,
		      int non_blocking)
{
	int err;
	struct iser_conn *iser_conn;
	struct iscsi_endpoint *ep;

	ep = iscsi_create_endpoint(0);
	if (!ep)
		return ERR_PTR(-ENOMEM);

	iser_conn = kzalloc(sizeof(*iser_conn), GFP_KERNEL);
	if (!iser_conn) {
		err = -ENOMEM;
		goto failure;
	}

	ep->dd_data = iser_conn;
	iser_conn->ep = ep;
	iser_conn_init(iser_conn);

	err = iser_connect(iser_conn, NULL, dst_addr, non_blocking);
	if (err)
		goto failure;

	return ep;
failure:
	iscsi_destroy_endpoint(ep);
	return ERR_PTR(err);
}

/**
 * iscsi_iser_ep_poll() - poll for iser connection establishment to complete
 * @ep:            iscsi endpoint (created at ep_connect)
 * @timeout_ms:    polling timeout allowed in ms.
 *
 * This routine boils down to waiting for up_completion signaling
 * that cma_id got CONNECTED event.
 *
 * Return: 1 if succeeded in connection establishment, 0 if timeout expired
 *         (libiscsi will retry will kick in) or -1 if interrupted by signal
 *         or more likely iser connection state transitioned to TEMINATING or
 *         DOWN during the wait period.
 */
static int
iscsi_iser_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct iser_conn *iser_conn = ep->dd_data;
	int rc;

	rc = wait_for_completion_interruptible_timeout(&iser_conn->up_completion,
						       msecs_to_jiffies(timeout_ms));
	/* if conn establishment failed, return error code to iscsi */
	if (rc == 0) {
		mutex_lock(&iser_conn->state_mutex);
		if (iser_conn->state == ISER_CONN_TERMINATING ||
		    iser_conn->state == ISER_CONN_DOWN)
			rc = -1;
		mutex_unlock(&iser_conn->state_mutex);
	}

	iser_info("iser conn %p rc = %d\n", iser_conn, rc);

	if (rc > 0)
		return 1; /* success, this is the equivalent of EPOLLOUT */
	else if (!rc)
		return 0; /* timeout */
	else
		return rc; /* signal */
}

/**
 * iscsi_iser_ep_disconnect() - Initiate connection teardown process
 * @ep:    iscsi endpoint handle
 *
 * This routine is not blocked by iser and RDMA termination process
 * completion as we queue a deffered work for iser/RDMA destruction
 * and cleanup or actually call it immediately in case we didn't pass
 * iscsi conn bind/start stage, thus it is safe.
 */
static void
iscsi_iser_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct iser_conn *iser_conn = ep->dd_data;

	iser_info("ep %p iser conn %p\n", ep, iser_conn);

	mutex_lock(&iser_conn->state_mutex);
	iser_conn_terminate(iser_conn);

	/*
	 * if iser_conn and iscsi_conn are bound, we must wait for
	 * iscsi_conn_stop and flush errors completion before freeing
	 * the iser resources. Otherwise we are safe to free resources
	 * immediately.
	 */
	if (iser_conn->iscsi_conn) {
		INIT_WORK(&iser_conn->release_work, iser_release_work);
		queue_work(release_wq, &iser_conn->release_work);
		mutex_unlock(&iser_conn->state_mutex);
	} else {
		iser_conn->state = ISER_CONN_DOWN;
		mutex_unlock(&iser_conn->state_mutex);
		iser_conn_release(iser_conn);
	}

	iscsi_destroy_endpoint(ep);
}

static umode_t iser_attr_is_visible(int param_type, int param)
{
	switch (param_type) {
	case ISCSI_HOST_PARAM:
		switch (param) {
		case ISCSI_HOST_PARAM_NETDEV_NAME:
		case ISCSI_HOST_PARAM_HWADDRESS:
		case ISCSI_HOST_PARAM_INITIATOR_NAME:
			return S_IRUGO;
		default:
			return 0;
		}
	case ISCSI_PARAM:
		switch (param) {
		case ISCSI_PARAM_MAX_RECV_DLENGTH:
		case ISCSI_PARAM_MAX_XMIT_DLENGTH:
		case ISCSI_PARAM_HDRDGST_EN:
		case ISCSI_PARAM_DATADGST_EN:
		case ISCSI_PARAM_CONN_ADDRESS:
		case ISCSI_PARAM_CONN_PORT:
		case ISCSI_PARAM_EXP_STATSN:
		case ISCSI_PARAM_PERSISTENT_ADDRESS:
		case ISCSI_PARAM_PERSISTENT_PORT:
		case ISCSI_PARAM_PING_TMO:
		case ISCSI_PARAM_RECV_TMO:
		case ISCSI_PARAM_INITIAL_R2T_EN:
		case ISCSI_PARAM_MAX_R2T:
		case ISCSI_PARAM_IMM_DATA_EN:
		case ISCSI_PARAM_FIRST_BURST:
		case ISCSI_PARAM_MAX_BURST:
		case ISCSI_PARAM_PDU_INORDER_EN:
		case ISCSI_PARAM_DATASEQ_INORDER_EN:
		case ISCSI_PARAM_TARGET_NAME:
		case ISCSI_PARAM_TPGT:
		case ISCSI_PARAM_USERNAME:
		case ISCSI_PARAM_PASSWORD:
		case ISCSI_PARAM_USERNAME_IN:
		case ISCSI_PARAM_PASSWORD_IN:
		case ISCSI_PARAM_FAST_ABORT:
		case ISCSI_PARAM_ABORT_TMO:
		case ISCSI_PARAM_LU_RESET_TMO:
		case ISCSI_PARAM_TGT_RESET_TMO:
		case ISCSI_PARAM_IFACE_NAME:
		case ISCSI_PARAM_INITIATOR_NAME:
		case ISCSI_PARAM_DISCOVERY_SESS:
			return S_IRUGO;
		default:
			return 0;
		}
	}

	return 0;
}

static int iscsi_iser_slave_alloc(struct scsi_device *sdev)
{
	struct iscsi_session *session;
	struct iser_conn *iser_conn;
	struct ib_device *ib_dev;

	mutex_lock(&unbind_iser_conn_mutex);

	session = starget_to_session(scsi_target(sdev))->dd_data;
	iser_conn = session->leadconn->dd_data;
	if (!iser_conn) {
		mutex_unlock(&unbind_iser_conn_mutex);
		return -ENOTCONN;
	}
	ib_dev = iser_conn->ib_conn.device->ib_device;

	if (!(ib_dev->attrs.device_cap_flags & IB_DEVICE_SG_GAPS_REG))
		blk_queue_virt_boundary(sdev->request_queue, ~MASK_4K);

	mutex_unlock(&unbind_iser_conn_mutex);

	return 0;
}

static struct scsi_host_template iscsi_iser_sht = {
	.module                 = THIS_MODULE,
	.name                   = "iSCSI Initiator over iSER",
	.queuecommand           = iscsi_queuecommand,
	.change_queue_depth	= scsi_change_queue_depth,
	.sg_tablesize           = ISCSI_ISER_DEF_SG_TABLESIZE,
	.cmd_per_lun            = ISER_DEF_CMD_PER_LUN,
	.eh_timed_out		= iscsi_eh_cmd_timed_out,
	.eh_abort_handler       = iscsi_eh_abort,
	.eh_device_reset_handler= iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.target_alloc		= iscsi_target_alloc,
	.slave_alloc            = iscsi_iser_slave_alloc,
	.proc_name              = "iscsi_iser",
	.this_id                = -1,
	.track_queue_depth	= 1,
};

static struct iscsi_transport iscsi_iser_transport = {
	.owner                  = THIS_MODULE,
	.name                   = "iser",
	.caps                   = CAP_RECOVERY_L0 | CAP_MULTI_R2T | CAP_TEXT_NEGO,
	/* session management */
	.create_session         = iscsi_iser_session_create,
	.destroy_session        = iscsi_iser_session_destroy,
	/* connection management */
	.create_conn            = iscsi_iser_conn_create,
	.bind_conn              = iscsi_iser_conn_bind,
	.destroy_conn           = iscsi_conn_teardown,
	.attr_is_visible	= iser_attr_is_visible,
	.set_param              = iscsi_iser_set_param,
	.get_conn_param		= iscsi_conn_get_param,
	.get_ep_param		= iscsi_iser_get_ep_param,
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
	.check_protection	= iscsi_iser_check_protection,
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
		iser_err("Invalid max_lun value of %u\n", iscsi_max_lun);
		return -EINVAL;
	}

	memset(&ig, 0, sizeof(struct iser_global));

	ig.desc_cache = kmem_cache_create("iser_descriptors",
					  sizeof(struct iser_tx_desc),
					  0, SLAB_HWCACHE_ALIGN,
					  NULL);
	if (ig.desc_cache == NULL)
		return -ENOMEM;

	/* device init is called only after the first addr resolution */
	mutex_init(&ig.device_list_mutex);
	INIT_LIST_HEAD(&ig.device_list);
	mutex_init(&ig.connlist_mutex);
	INIT_LIST_HEAD(&ig.connlist);

	release_wq = alloc_workqueue("release workqueue", 0, 0);
	if (!release_wq) {
		iser_err("failed to allocate release workqueue\n");
		err = -ENOMEM;
		goto err_alloc_wq;
	}

	iscsi_iser_scsi_transport = iscsi_register_transport(
							&iscsi_iser_transport);
	if (!iscsi_iser_scsi_transport) {
		iser_err("iscsi_register_transport failed\n");
		err = -EINVAL;
		goto err_reg;
	}

	return 0;

err_reg:
	destroy_workqueue(release_wq);
err_alloc_wq:
	kmem_cache_destroy(ig.desc_cache);

	return err;
}

static void __exit iser_exit(void)
{
	struct iser_conn *iser_conn, *n;
	int connlist_empty;

	iser_dbg("Removing iSER datamover...\n");
	destroy_workqueue(release_wq);

	mutex_lock(&ig.connlist_mutex);
	connlist_empty = list_empty(&ig.connlist);
	mutex_unlock(&ig.connlist_mutex);

	if (!connlist_empty) {
		iser_err("Error cleanup stage completed but we still have iser "
			 "connections, destroying them anyway\n");
		list_for_each_entry_safe(iser_conn, n, &ig.connlist,
					 conn_list) {
			iser_conn_release(iser_conn);
		}
	}

	iscsi_unregister_transport(&iscsi_iser_transport);
	kmem_cache_destroy(ig.desc_cache);
}

module_init(iser_init);
module_exit(iser_exit);

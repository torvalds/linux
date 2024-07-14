/*
 * Copyright (c) 2004, 2005, 2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2013-2014 Mellanox Technologies. All rights reserved.
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
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/kfifo.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>

#include "iscsi_iser.h"

/* Register user buffer memory and initialize passive rdma
 *  dto descriptor. Data size is stored in
 *  task->data[ISER_DIR_IN].data_len, Protection size
 *  os stored in task->prot[ISER_DIR_IN].data_len
 */
static int iser_prepare_read_cmd(struct iscsi_task *task)

{
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_mem_reg *mem_reg;
	int err;
	struct iser_ctrl *hdr = &iser_task->desc.iser_header;

	err = iser_dma_map_task_data(iser_task,
				     ISER_DIR_IN,
				     DMA_FROM_DEVICE);
	if (err)
		return err;

	err = iser_reg_mem_fastreg(iser_task, ISER_DIR_IN, false);
	if (err) {
		iser_err("Failed to set up Data-IN RDMA\n");
		goto out_err;
	}
	mem_reg = &iser_task->rdma_reg[ISER_DIR_IN];

	hdr->flags    |= ISER_RSV;
	hdr->read_stag = cpu_to_be32(mem_reg->rkey);
	hdr->read_va   = cpu_to_be64(mem_reg->sge.addr);

	iser_dbg("Cmd itt:%d READ tags RKEY:%#.4X VA:%#llX\n",
		 task->itt, mem_reg->rkey,
		 (unsigned long long)mem_reg->sge.addr);

	return 0;

out_err:
	iser_dma_unmap_task_data(iser_task, ISER_DIR_IN, DMA_FROM_DEVICE);
	return err;
}

/* Register user buffer memory and initialize passive rdma
 *  dto descriptor. Data size is stored in
 *  task->data[ISER_DIR_OUT].data_len, Protection size
 *  is stored at task->prot[ISER_DIR_OUT].data_len
 */
static int iser_prepare_write_cmd(struct iscsi_task *task, unsigned int imm_sz,
				  unsigned int unsol_sz, unsigned int edtl)
{
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_mem_reg *mem_reg;
	int err;
	struct iser_ctrl *hdr = &iser_task->desc.iser_header;
	struct iser_data_buf *buf_out = &iser_task->data[ISER_DIR_OUT];
	struct ib_sge *tx_dsg = &iser_task->desc.tx_sg[1];

	err = iser_dma_map_task_data(iser_task,
				     ISER_DIR_OUT,
				     DMA_TO_DEVICE);
	if (err)
		return err;

	err = iser_reg_mem_fastreg(iser_task, ISER_DIR_OUT,
				   buf_out->data_len == imm_sz);
	if (err) {
		iser_err("Failed to register write cmd RDMA mem\n");
		goto out_err;
	}

	mem_reg = &iser_task->rdma_reg[ISER_DIR_OUT];

	if (unsol_sz < edtl) {
		hdr->flags     |= ISER_WSV;
		if (buf_out->data_len > imm_sz) {
			hdr->write_stag = cpu_to_be32(mem_reg->rkey);
			hdr->write_va = cpu_to_be64(mem_reg->sge.addr + unsol_sz);
		}

		iser_dbg("Cmd itt:%d, WRITE tags, RKEY:%#.4X VA:%#llX + unsol:%d\n",
			 task->itt, mem_reg->rkey,
			 (unsigned long long)mem_reg->sge.addr, unsol_sz);
	}

	if (imm_sz > 0) {
		iser_dbg("Cmd itt:%d, WRITE, adding imm.data sz: %d\n",
			 task->itt, imm_sz);
		tx_dsg->addr = mem_reg->sge.addr;
		tx_dsg->length = imm_sz;
		tx_dsg->lkey = mem_reg->sge.lkey;
		iser_task->desc.num_sge = 2;
	}

	return 0;

out_err:
	iser_dma_unmap_task_data(iser_task, ISER_DIR_OUT, DMA_TO_DEVICE);
	return err;
}

/* creates a new tx descriptor and adds header regd buffer */
static void iser_create_send_desc(struct iser_conn *iser_conn,
				  struct iser_tx_desc *tx_desc)
{
	struct iser_device *device = iser_conn->ib_conn.device;

	ib_dma_sync_single_for_cpu(device->ib_device,
		tx_desc->dma_addr, ISER_HEADERS_LEN, DMA_TO_DEVICE);

	memset(&tx_desc->iser_header, 0, sizeof(struct iser_ctrl));
	tx_desc->iser_header.flags = ISER_VER;
	tx_desc->num_sge = 1;
}

static void iser_free_login_buf(struct iser_conn *iser_conn)
{
	struct iser_device *device = iser_conn->ib_conn.device;
	struct iser_login_desc *desc = &iser_conn->login_desc;

	if (!desc->req)
		return;

	ib_dma_unmap_single(device->ib_device, desc->req_dma,
			    ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_TO_DEVICE);

	ib_dma_unmap_single(device->ib_device, desc->rsp_dma,
			    ISER_RX_LOGIN_SIZE, DMA_FROM_DEVICE);

	kfree(desc->req);
	kfree(desc->rsp);

	/* make sure we never redo any unmapping */
	desc->req = NULL;
	desc->rsp = NULL;
}

static int iser_alloc_login_buf(struct iser_conn *iser_conn)
{
	struct iser_device *device = iser_conn->ib_conn.device;
	struct iser_login_desc *desc = &iser_conn->login_desc;

	desc->req = kmalloc(ISCSI_DEF_MAX_RECV_SEG_LEN, GFP_KERNEL);
	if (!desc->req)
		return -ENOMEM;

	desc->req_dma = ib_dma_map_single(device->ib_device, desc->req,
					  ISCSI_DEF_MAX_RECV_SEG_LEN,
					  DMA_TO_DEVICE);
	if (ib_dma_mapping_error(device->ib_device,
				desc->req_dma))
		goto free_req;

	desc->rsp = kmalloc(ISER_RX_LOGIN_SIZE, GFP_KERNEL);
	if (!desc->rsp)
		goto unmap_req;

	desc->rsp_dma = ib_dma_map_single(device->ib_device, desc->rsp,
					   ISER_RX_LOGIN_SIZE,
					   DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(device->ib_device,
				desc->rsp_dma))
		goto free_rsp;

	return 0;

free_rsp:
	kfree(desc->rsp);
unmap_req:
	ib_dma_unmap_single(device->ib_device, desc->req_dma,
			    ISCSI_DEF_MAX_RECV_SEG_LEN,
			    DMA_TO_DEVICE);
free_req:
	kfree(desc->req);

	return -ENOMEM;
}

int iser_alloc_rx_descriptors(struct iser_conn *iser_conn,
			      struct iscsi_session *session)
{
	int i, j;
	u64 dma_addr;
	struct iser_rx_desc *rx_desc;
	struct ib_sge       *rx_sg;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;

	iser_conn->qp_max_recv_dtos = session->cmds_max;

	if (iser_alloc_fastreg_pool(ib_conn, session->scsi_cmds_max,
				    iser_conn->pages_per_mr))
		goto create_rdma_reg_res_failed;

	if (iser_alloc_login_buf(iser_conn))
		goto alloc_login_buf_fail;

	iser_conn->num_rx_descs = session->cmds_max;
	iser_conn->rx_descs = kmalloc_array(iser_conn->num_rx_descs,
					    sizeof(struct iser_rx_desc),
					    GFP_KERNEL);
	if (!iser_conn->rx_descs)
		goto rx_desc_alloc_fail;

	rx_desc = iser_conn->rx_descs;

	for (i = 0; i < iser_conn->qp_max_recv_dtos; i++, rx_desc++)  {
		dma_addr = ib_dma_map_single(device->ib_device, (void *)rx_desc,
					ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(device->ib_device, dma_addr))
			goto rx_desc_dma_map_failed;

		rx_desc->dma_addr = dma_addr;
		rx_desc->cqe.done = iser_task_rsp;
		rx_sg = &rx_desc->rx_sg;
		rx_sg->addr = rx_desc->dma_addr;
		rx_sg->length = ISER_RX_PAYLOAD_SIZE;
		rx_sg->lkey = device->pd->local_dma_lkey;
	}

	return 0;

rx_desc_dma_map_failed:
	rx_desc = iser_conn->rx_descs;
	for (j = 0; j < i; j++, rx_desc++)
		ib_dma_unmap_single(device->ib_device, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	kfree(iser_conn->rx_descs);
	iser_conn->rx_descs = NULL;
rx_desc_alloc_fail:
	iser_free_login_buf(iser_conn);
alloc_login_buf_fail:
	iser_free_fastreg_pool(ib_conn);
create_rdma_reg_res_failed:
	iser_err("failed allocating rx descriptors / data buffers\n");
	return -ENOMEM;
}

void iser_free_rx_descriptors(struct iser_conn *iser_conn)
{
	int i;
	struct iser_rx_desc *rx_desc;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;

	iser_free_fastreg_pool(ib_conn);

	rx_desc = iser_conn->rx_descs;
	for (i = 0; i < iser_conn->qp_max_recv_dtos; i++, rx_desc++)
		ib_dma_unmap_single(device->ib_device, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	kfree(iser_conn->rx_descs);
	/* make sure we never redo any unmapping */
	iser_conn->rx_descs = NULL;

	iser_free_login_buf(iser_conn);
}

static int iser_post_rx_bufs(struct iscsi_conn *conn, struct iscsi_hdr *req)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_session *session = conn->session;
	int err = 0;
	int i;

	iser_dbg("req op %x flags %x\n", req->opcode, req->flags);
	/* check if this is the last login - going to full feature phase */
	if ((req->flags & ISCSI_FULL_FEATURE_PHASE) != ISCSI_FULL_FEATURE_PHASE)
		goto out;

	if (session->discovery_sess) {
		iser_info("Discovery session, re-using login RX buffer\n");
		goto out;
	}

	iser_info("Normal session, posting batch of RX %d buffers\n",
		  iser_conn->qp_max_recv_dtos - 1);

	/*
	 * Initial post receive buffers.
	 * There is one already posted recv buffer (for the last login
	 * response). Therefore, the first recv buffer is skipped here.
	 */
	for (i = 1; i < iser_conn->qp_max_recv_dtos; i++) {
		err = iser_post_recvm(iser_conn, &iser_conn->rx_descs[i]);
		if (err)
			goto out;
	}
out:
	return err;
}

/**
 * iser_send_command - send command PDU
 * @conn: link to matching iscsi connection
 * @task: SCSI command task
 */
int iser_send_command(struct iscsi_conn *conn, struct iscsi_task *task)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;
	unsigned long edtl;
	int err;
	struct iser_data_buf *data_buf, *prot_buf;
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)task->hdr;
	struct scsi_cmnd *sc  =  task->sc;
	struct iser_tx_desc *tx_desc = &iser_task->desc;

	edtl = ntohl(hdr->data_length);

	/* build the tx desc regd header and add it to the tx desc dto */
	tx_desc->type = ISCSI_TX_SCSI_COMMAND;
	tx_desc->cqe.done = iser_cmd_comp;
	iser_create_send_desc(iser_conn, tx_desc);

	if (hdr->flags & ISCSI_FLAG_CMD_READ) {
		data_buf = &iser_task->data[ISER_DIR_IN];
		prot_buf = &iser_task->prot[ISER_DIR_IN];
	} else {
		data_buf = &iser_task->data[ISER_DIR_OUT];
		prot_buf = &iser_task->prot[ISER_DIR_OUT];
	}

	if (scsi_sg_count(sc)) { /* using a scatter list */
		data_buf->sg = scsi_sglist(sc);
		data_buf->size = scsi_sg_count(sc);
	}
	data_buf->data_len = scsi_bufflen(sc);

	if (scsi_prot_sg_count(sc)) {
		prot_buf->sg  = scsi_prot_sglist(sc);
		prot_buf->size = scsi_prot_sg_count(sc);
		prot_buf->data_len = (data_buf->data_len >>
				     ilog2(sc->device->sector_size)) * 8;
	}

	if (hdr->flags & ISCSI_FLAG_CMD_READ) {
		err = iser_prepare_read_cmd(task);
		if (err)
			goto send_command_error;
	}
	if (hdr->flags & ISCSI_FLAG_CMD_WRITE) {
		err = iser_prepare_write_cmd(task,
					     task->imm_count,
				             task->imm_count +
					     task->unsol_r2t.data_length,
					     edtl);
		if (err)
			goto send_command_error;
	}

	iser_task->status = ISER_TASK_STATUS_STARTED;

	err = iser_post_send(&iser_conn->ib_conn, tx_desc);
	if (!err)
		return 0;

send_command_error:
	iser_err("conn %p failed task->itt %d err %d\n",conn, task->itt, err);
	return err;
}

/**
 * iser_send_data_out - send data out PDU
 * @conn: link to matching iscsi connection
 * @task: SCSI command task
 * @hdr: pointer to the LLD's iSCSI message header
 */
int iser_send_data_out(struct iscsi_conn *conn, struct iscsi_task *task,
		       struct iscsi_data *hdr)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_tx_desc *tx_desc;
	struct iser_mem_reg *mem_reg;
	unsigned long buf_offset;
	unsigned long data_seg_len;
	uint32_t itt;
	int err;
	struct ib_sge *tx_dsg;

	itt = (__force uint32_t)hdr->itt;
	data_seg_len = ntoh24(hdr->dlength);
	buf_offset   = ntohl(hdr->offset);

	iser_dbg("%s itt %d dseg_len %d offset %d\n",
		 __func__,(int)itt,(int)data_seg_len,(int)buf_offset);

	tx_desc = kmem_cache_zalloc(ig.desc_cache, GFP_ATOMIC);
	if (!tx_desc)
		return -ENOMEM;

	tx_desc->type = ISCSI_TX_DATAOUT;
	tx_desc->cqe.done = iser_dataout_comp;
	tx_desc->iser_header.flags = ISER_VER;
	memcpy(&tx_desc->iscsi_header, hdr, sizeof(struct iscsi_hdr));

	/* build the tx desc */
	err = iser_initialize_task_headers(task, tx_desc);
	if (err)
		goto send_data_out_error;

	mem_reg = &iser_task->rdma_reg[ISER_DIR_OUT];
	tx_dsg = &tx_desc->tx_sg[1];
	tx_dsg->addr = mem_reg->sge.addr + buf_offset;
	tx_dsg->length = data_seg_len;
	tx_dsg->lkey = mem_reg->sge.lkey;
	tx_desc->num_sge = 2;

	if (buf_offset + data_seg_len > iser_task->data[ISER_DIR_OUT].data_len) {
		iser_err("Offset:%ld & DSL:%ld in Data-Out inconsistent with total len:%ld, itt:%d\n",
			 buf_offset, data_seg_len,
			 iser_task->data[ISER_DIR_OUT].data_len, itt);
		err = -EINVAL;
		goto send_data_out_error;
	}
	iser_dbg("data-out itt: %d, offset: %ld, sz: %ld\n",
		 itt, buf_offset, data_seg_len);


	err = iser_post_send(&iser_conn->ib_conn, tx_desc);
	if (!err)
		return 0;

send_data_out_error:
	kmem_cache_free(ig.desc_cache, tx_desc);
	iser_err("conn %p failed err %d\n", conn, err);
	return err;
}

int iser_send_control(struct iscsi_conn *conn, struct iscsi_task *task)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_tx_desc *mdesc = &iser_task->desc;
	unsigned long data_seg_len;
	int err = 0;
	struct iser_device *device;

	/* build the tx desc regd header and add it to the tx desc dto */
	mdesc->type = ISCSI_TX_CONTROL;
	mdesc->cqe.done = iser_ctrl_comp;
	iser_create_send_desc(iser_conn, mdesc);

	device = iser_conn->ib_conn.device;

	data_seg_len = ntoh24(task->hdr->dlength);

	if (data_seg_len > 0) {
		struct iser_login_desc *desc = &iser_conn->login_desc;
		struct ib_sge *tx_dsg = &mdesc->tx_sg[1];

		if (task != conn->login_task) {
			iser_err("data present on non login task!!!\n");
			goto send_control_error;
		}

		ib_dma_sync_single_for_cpu(device->ib_device, desc->req_dma,
					   task->data_count, DMA_TO_DEVICE);

		memcpy(desc->req, task->data, task->data_count);

		ib_dma_sync_single_for_device(device->ib_device, desc->req_dma,
					      task->data_count, DMA_TO_DEVICE);

		tx_dsg->addr = desc->req_dma;
		tx_dsg->length = task->data_count;
		tx_dsg->lkey = device->pd->local_dma_lkey;
		mdesc->num_sge = 2;
	}

	if (task == conn->login_task) {
		iser_dbg("op %x dsl %lx, posting login rx buffer\n",
			 task->hdr->opcode, data_seg_len);
		err = iser_post_recvl(iser_conn);
		if (err)
			goto send_control_error;
		err = iser_post_rx_bufs(conn, task->hdr);
		if (err)
			goto send_control_error;
	}

	err = iser_post_send(&iser_conn->ib_conn, mdesc);
	if (!err)
		return 0;

send_control_error:
	iser_err("conn %p failed err %d\n",conn, err);
	return err;
}

void iser_login_rsp(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_conn *ib_conn = wc->qp->qp_context;
	struct iser_conn *iser_conn = to_iser_conn(ib_conn);
	struct iser_login_desc *desc = iser_login(wc->wr_cqe);
	struct iscsi_hdr *hdr;
	char *data;
	int length;
	bool full_feature_phase;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		iser_err_comp(wc, "login_rsp");
		return;
	}

	ib_dma_sync_single_for_cpu(ib_conn->device->ib_device,
				   desc->rsp_dma, ISER_RX_LOGIN_SIZE,
				   DMA_FROM_DEVICE);

	hdr = desc->rsp + sizeof(struct iser_ctrl);
	data = desc->rsp + ISER_HEADERS_LEN;
	length = wc->byte_len - ISER_HEADERS_LEN;
	full_feature_phase = ((hdr->flags & ISCSI_FULL_FEATURE_PHASE) ==
			      ISCSI_FULL_FEATURE_PHASE) &&
			     (hdr->flags & ISCSI_FLAG_CMD_FINAL);

	iser_dbg("op 0x%x itt 0x%x dlen %d\n", hdr->opcode,
		 hdr->itt, length);

	iscsi_iser_recv(iser_conn->iscsi_conn, hdr, data, length);

	ib_dma_sync_single_for_device(ib_conn->device->ib_device,
				      desc->rsp_dma, ISER_RX_LOGIN_SIZE,
				      DMA_FROM_DEVICE);

	if (!full_feature_phase ||
	    iser_conn->iscsi_conn->session->discovery_sess)
		return;

	/* Post the first RX buffer that is skipped in iser_post_rx_bufs() */
	iser_post_recvm(iser_conn, iser_conn->rx_descs);
}

static inline int iser_inv_desc(struct iser_fr_desc *desc, u32 rkey)
{
	if (unlikely((!desc->sig_protected && rkey != desc->rsc.mr->rkey) ||
		     (desc->sig_protected && rkey != desc->rsc.sig_mr->rkey))) {
		iser_err("Bogus remote invalidation for rkey %#x\n", rkey);
		return -EINVAL;
	}

	if (desc->sig_protected)
		desc->rsc.sig_mr->need_inval = false;
	else
		desc->rsc.mr->need_inval = false;

	return 0;
}

static int iser_check_remote_inv(struct iser_conn *iser_conn, struct ib_wc *wc,
				 struct iscsi_hdr *hdr)
{
	if (wc->wc_flags & IB_WC_WITH_INVALIDATE) {
		struct iscsi_task *task;
		u32 rkey = wc->ex.invalidate_rkey;

		iser_dbg("conn %p: remote invalidation for rkey %#x\n",
			 iser_conn, rkey);

		if (unlikely(!iser_conn->snd_w_inv)) {
			iser_err("conn %p: unexpected remote invalidation, terminating connection\n",
				 iser_conn);
			return -EPROTO;
		}

		task = iscsi_itt_to_ctask(iser_conn->iscsi_conn, hdr->itt);
		if (likely(task)) {
			struct iscsi_iser_task *iser_task = task->dd_data;
			struct iser_fr_desc *desc;

			if (iser_task->dir[ISER_DIR_IN]) {
				desc = iser_task->rdma_reg[ISER_DIR_IN].desc;
				if (unlikely(iser_inv_desc(desc, rkey)))
					return -EINVAL;
			}

			if (iser_task->dir[ISER_DIR_OUT]) {
				desc = iser_task->rdma_reg[ISER_DIR_OUT].desc;
				if (unlikely(iser_inv_desc(desc, rkey)))
					return -EINVAL;
			}
		} else {
			iser_err("failed to get task for itt=%d\n", hdr->itt);
			return -EINVAL;
		}
	}

	return 0;
}


void iser_task_rsp(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_conn *ib_conn = wc->qp->qp_context;
	struct iser_conn *iser_conn = to_iser_conn(ib_conn);
	struct iser_rx_desc *desc = iser_rx(wc->wr_cqe);
	struct iscsi_hdr *hdr;
	int length, err;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		iser_err_comp(wc, "task_rsp");
		return;
	}

	ib_dma_sync_single_for_cpu(ib_conn->device->ib_device,
				   desc->dma_addr, ISER_RX_PAYLOAD_SIZE,
				   DMA_FROM_DEVICE);

	hdr = &desc->iscsi_header;
	length = wc->byte_len - ISER_HEADERS_LEN;

	iser_dbg("op 0x%x itt 0x%x dlen %d\n", hdr->opcode,
		 hdr->itt, length);

	if (iser_check_remote_inv(iser_conn, wc, hdr)) {
		iscsi_conn_failure(iser_conn->iscsi_conn,
				   ISCSI_ERR_CONN_FAILED);
		return;
	}

	iscsi_iser_recv(iser_conn->iscsi_conn, hdr, desc->data, length);

	ib_dma_sync_single_for_device(ib_conn->device->ib_device,
				      desc->dma_addr, ISER_RX_PAYLOAD_SIZE,
				      DMA_FROM_DEVICE);

	err = iser_post_recvm(iser_conn, desc);
	if (err)
		iser_err("posting rx buffer err %d\n", err);
}

void iser_cmd_comp(struct ib_cq *cq, struct ib_wc *wc)
{
	if (unlikely(wc->status != IB_WC_SUCCESS))
		iser_err_comp(wc, "command");
}

void iser_ctrl_comp(struct ib_cq *cq, struct ib_wc *wc)
{
	struct iser_tx_desc *desc = iser_tx(wc->wr_cqe);
	struct iscsi_task *task;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		iser_err_comp(wc, "control");
		return;
	}

	/* this arithmetic is legal by libiscsi dd_data allocation */
	task = (void *)desc - sizeof(struct iscsi_task);
	if (task->hdr->itt == RESERVED_ITT)
		iscsi_put_task(task);
}

void iser_dataout_comp(struct ib_cq *cq, struct ib_wc *wc)
{
	struct iser_tx_desc *desc = iser_tx(wc->wr_cqe);
	struct ib_conn *ib_conn = wc->qp->qp_context;
	struct iser_device *device = ib_conn->device;

	if (unlikely(wc->status != IB_WC_SUCCESS))
		iser_err_comp(wc, "dataout");

	ib_dma_unmap_single(device->ib_device, desc->dma_addr,
			    ISER_HEADERS_LEN, DMA_TO_DEVICE);
	kmem_cache_free(ig.desc_cache, desc);
}

void iser_task_rdma_init(struct iscsi_iser_task *iser_task)

{
	iser_task->status = ISER_TASK_STATUS_INIT;

	iser_task->dir[ISER_DIR_IN] = 0;
	iser_task->dir[ISER_DIR_OUT] = 0;

	iser_task->data[ISER_DIR_IN].data_len  = 0;
	iser_task->data[ISER_DIR_OUT].data_len = 0;

	iser_task->prot[ISER_DIR_IN].data_len  = 0;
	iser_task->prot[ISER_DIR_OUT].data_len = 0;

	iser_task->prot[ISER_DIR_IN].dma_nents = 0;
	iser_task->prot[ISER_DIR_OUT].dma_nents = 0;

	memset(&iser_task->rdma_reg[ISER_DIR_IN], 0,
	       sizeof(struct iser_mem_reg));
	memset(&iser_task->rdma_reg[ISER_DIR_OUT], 0,
	       sizeof(struct iser_mem_reg));
}

void iser_task_rdma_finalize(struct iscsi_iser_task *iser_task)
{

	if (iser_task->dir[ISER_DIR_IN]) {
		iser_unreg_mem_fastreg(iser_task, ISER_DIR_IN);
		iser_dma_unmap_task_data(iser_task, ISER_DIR_IN,
					 DMA_FROM_DEVICE);
	}

	if (iser_task->dir[ISER_DIR_OUT]) {
		iser_unreg_mem_fastreg(iser_task, ISER_DIR_OUT);
		iser_dma_unmap_task_data(iser_task, ISER_DIR_OUT,
					 DMA_TO_DEVICE);
	}
}

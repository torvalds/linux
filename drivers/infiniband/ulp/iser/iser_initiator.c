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
	struct iser_device  *device = iser_task->iser_conn->ib_conn.device;
	struct iser_regd_buf *regd_buf;
	int err;
	struct iser_hdr *hdr = &iser_task->desc.iser_header;
	struct iser_data_buf *buf_in = &iser_task->data[ISER_DIR_IN];

	err = iser_dma_map_task_data(iser_task,
				     buf_in,
				     ISER_DIR_IN,
				     DMA_FROM_DEVICE);
	if (err)
		return err;

	if (scsi_prot_sg_count(iser_task->sc)) {
		struct iser_data_buf *pbuf_in = &iser_task->prot[ISER_DIR_IN];

		err = iser_dma_map_task_data(iser_task,
					     pbuf_in,
					     ISER_DIR_IN,
					     DMA_FROM_DEVICE);
		if (err)
			return err;
	}

	err = device->iser_reg_rdma_mem(iser_task, ISER_DIR_IN);
	if (err) {
		iser_err("Failed to set up Data-IN RDMA\n");
		return err;
	}
	regd_buf = &iser_task->rdma_regd[ISER_DIR_IN];

	hdr->flags    |= ISER_RSV;
	hdr->read_stag = cpu_to_be32(regd_buf->reg.rkey);
	hdr->read_va   = cpu_to_be64(regd_buf->reg.va);

	iser_dbg("Cmd itt:%d READ tags RKEY:%#.4X VA:%#llX\n",
		 task->itt, regd_buf->reg.rkey,
		 (unsigned long long)regd_buf->reg.va);

	return 0;
}

/* Register user buffer memory and initialize passive rdma
 *  dto descriptor. Data size is stored in
 *  task->data[ISER_DIR_OUT].data_len, Protection size
 *  is stored at task->prot[ISER_DIR_OUT].data_len
 */
static int
iser_prepare_write_cmd(struct iscsi_task *task,
		       unsigned int imm_sz,
		       unsigned int unsol_sz,
		       unsigned int edtl)
{
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_device  *device = iser_task->iser_conn->ib_conn.device;
	struct iser_regd_buf *regd_buf;
	int err;
	struct iser_hdr *hdr = &iser_task->desc.iser_header;
	struct iser_data_buf *buf_out = &iser_task->data[ISER_DIR_OUT];
	struct ib_sge *tx_dsg = &iser_task->desc.tx_sg[1];

	err = iser_dma_map_task_data(iser_task,
				     buf_out,
				     ISER_DIR_OUT,
				     DMA_TO_DEVICE);
	if (err)
		return err;

	if (scsi_prot_sg_count(iser_task->sc)) {
		struct iser_data_buf *pbuf_out = &iser_task->prot[ISER_DIR_OUT];

		err = iser_dma_map_task_data(iser_task,
					     pbuf_out,
					     ISER_DIR_OUT,
					     DMA_TO_DEVICE);
		if (err)
			return err;
	}

	err = device->iser_reg_rdma_mem(iser_task, ISER_DIR_OUT);
	if (err != 0) {
		iser_err("Failed to register write cmd RDMA mem\n");
		return err;
	}

	regd_buf = &iser_task->rdma_regd[ISER_DIR_OUT];

	if (unsol_sz < edtl) {
		hdr->flags     |= ISER_WSV;
		hdr->write_stag = cpu_to_be32(regd_buf->reg.rkey);
		hdr->write_va   = cpu_to_be64(regd_buf->reg.va + unsol_sz);

		iser_dbg("Cmd itt:%d, WRITE tags, RKEY:%#.4X "
			 "VA:%#llX + unsol:%d\n",
			 task->itt, regd_buf->reg.rkey,
			 (unsigned long long)regd_buf->reg.va, unsol_sz);
	}

	if (imm_sz > 0) {
		iser_dbg("Cmd itt:%d, WRITE, adding imm.data sz: %d\n",
			 task->itt, imm_sz);
		tx_dsg->addr   = regd_buf->reg.va;
		tx_dsg->length = imm_sz;
		tx_dsg->lkey   = regd_buf->reg.lkey;
		iser_task->desc.num_sge = 2;
	}

	return 0;
}

/* creates a new tx descriptor and adds header regd buffer */
static void iser_create_send_desc(struct iser_conn	*iser_conn,
				  struct iser_tx_desc	*tx_desc)
{
	struct iser_device *device = iser_conn->ib_conn.device;

	ib_dma_sync_single_for_cpu(device->ib_device,
		tx_desc->dma_addr, ISER_HEADERS_LEN, DMA_TO_DEVICE);

	memset(&tx_desc->iser_header, 0, sizeof(struct iser_hdr));
	tx_desc->iser_header.flags = ISER_VER;

	tx_desc->num_sge = 1;

	if (tx_desc->tx_sg[0].lkey != device->mr->lkey) {
		tx_desc->tx_sg[0].lkey = device->mr->lkey;
		iser_dbg("sdesc %p lkey mismatch, fixing\n", tx_desc);
	}
}

static void iser_free_login_buf(struct iser_conn *iser_conn)
{
	struct iser_device *device = iser_conn->ib_conn.device;

	if (!iser_conn->login_buf)
		return;

	if (iser_conn->login_req_dma)
		ib_dma_unmap_single(device->ib_device,
				    iser_conn->login_req_dma,
				    ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_TO_DEVICE);

	if (iser_conn->login_resp_dma)
		ib_dma_unmap_single(device->ib_device,
				    iser_conn->login_resp_dma,
				    ISER_RX_LOGIN_SIZE, DMA_FROM_DEVICE);

	kfree(iser_conn->login_buf);

	/* make sure we never redo any unmapping */
	iser_conn->login_req_dma = 0;
	iser_conn->login_resp_dma = 0;
	iser_conn->login_buf = NULL;
}

static int iser_alloc_login_buf(struct iser_conn *iser_conn)
{
	struct iser_device *device = iser_conn->ib_conn.device;
	int			req_err, resp_err;

	BUG_ON(device == NULL);

	iser_conn->login_buf = kmalloc(ISCSI_DEF_MAX_RECV_SEG_LEN +
				     ISER_RX_LOGIN_SIZE, GFP_KERNEL);
	if (!iser_conn->login_buf)
		goto out_err;

	iser_conn->login_req_buf  = iser_conn->login_buf;
	iser_conn->login_resp_buf = iser_conn->login_buf +
						ISCSI_DEF_MAX_RECV_SEG_LEN;

	iser_conn->login_req_dma = ib_dma_map_single(device->ib_device,
						     iser_conn->login_req_buf,
						     ISCSI_DEF_MAX_RECV_SEG_LEN,
						     DMA_TO_DEVICE);

	iser_conn->login_resp_dma = ib_dma_map_single(device->ib_device,
						      iser_conn->login_resp_buf,
						      ISER_RX_LOGIN_SIZE,
						      DMA_FROM_DEVICE);

	req_err  = ib_dma_mapping_error(device->ib_device,
					iser_conn->login_req_dma);
	resp_err = ib_dma_mapping_error(device->ib_device,
					iser_conn->login_resp_dma);

	if (req_err || resp_err) {
		if (req_err)
			iser_conn->login_req_dma = 0;
		if (resp_err)
			iser_conn->login_resp_dma = 0;
		goto free_login_buf;
	}
	return 0;

free_login_buf:
	iser_free_login_buf(iser_conn);

out_err:
	iser_err("unable to alloc or map login buf\n");
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
	iser_conn->qp_max_recv_dtos_mask = session->cmds_max - 1; /* cmds_max is 2^N */
	iser_conn->min_posted_rx = iser_conn->qp_max_recv_dtos >> 2;

	if (device->iser_alloc_rdma_reg_res(ib_conn, session->scsi_cmds_max))
		goto create_rdma_reg_res_failed;

	if (iser_alloc_login_buf(iser_conn))
		goto alloc_login_buf_fail;

	iser_conn->num_rx_descs = session->cmds_max;
	iser_conn->rx_descs = kmalloc(iser_conn->num_rx_descs *
				sizeof(struct iser_rx_desc), GFP_KERNEL);
	if (!iser_conn->rx_descs)
		goto rx_desc_alloc_fail;

	rx_desc = iser_conn->rx_descs;

	for (i = 0; i < iser_conn->qp_max_recv_dtos; i++, rx_desc++)  {
		dma_addr = ib_dma_map_single(device->ib_device, (void *)rx_desc,
					ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(device->ib_device, dma_addr))
			goto rx_desc_dma_map_failed;

		rx_desc->dma_addr = dma_addr;

		rx_sg = &rx_desc->rx_sg;
		rx_sg->addr   = rx_desc->dma_addr;
		rx_sg->length = ISER_RX_PAYLOAD_SIZE;
		rx_sg->lkey   = device->mr->lkey;
	}

	iser_conn->rx_desc_head = 0;
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
	device->iser_free_rdma_reg_res(ib_conn);
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

	if (!iser_conn->rx_descs)
		goto free_login_buf;

	if (device->iser_free_rdma_reg_res)
		device->iser_free_rdma_reg_res(ib_conn);

	rx_desc = iser_conn->rx_descs;
	for (i = 0; i < iser_conn->qp_max_recv_dtos; i++, rx_desc++)
		ib_dma_unmap_single(device->ib_device, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	kfree(iser_conn->rx_descs);
	/* make sure we never redo any unmapping */
	iser_conn->rx_descs = NULL;

free_login_buf:
	iser_free_login_buf(iser_conn);
}

static int iser_post_rx_bufs(struct iscsi_conn *conn, struct iscsi_hdr *req)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iscsi_session *session = conn->session;

	iser_dbg("req op %x flags %x\n", req->opcode, req->flags);
	/* check if this is the last login - going to full feature phase */
	if ((req->flags & ISCSI_FULL_FEATURE_PHASE) != ISCSI_FULL_FEATURE_PHASE)
		return 0;

	/*
	 * Check that there is one posted recv buffer
	 * (for the last login response).
	 */
	WARN_ON(ib_conn->post_recv_buf_count != 1);

	if (session->discovery_sess) {
		iser_info("Discovery session, re-using login RX buffer\n");
		return 0;
	} else
		iser_info("Normal session, posting batch of RX %d buffers\n",
			  iser_conn->min_posted_rx);

	/* Initial post receive buffers */
	if (iser_post_recvm(iser_conn, iser_conn->min_posted_rx))
		return -ENOMEM;

	return 0;
}

static inline bool iser_signal_comp(u8 sig_count)
{
	return ((sig_count % ISER_SIGNAL_CMD_COUNT) == 0);
}

/**
 * iser_send_command - send command PDU
 */
int iser_send_command(struct iscsi_conn *conn,
		      struct iscsi_task *task)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;
	unsigned long edtl;
	int err;
	struct iser_data_buf *data_buf, *prot_buf;
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)task->hdr;
	struct scsi_cmnd *sc  =  task->sc;
	struct iser_tx_desc *tx_desc = &iser_task->desc;
	u8 sig_count = ++iser_conn->ib_conn.sig_count;

	edtl = ntohl(hdr->data_length);

	/* build the tx desc regd header and add it to the tx desc dto */
	tx_desc->type = ISCSI_TX_SCSI_COMMAND;
	iser_create_send_desc(iser_conn, tx_desc);

	if (hdr->flags & ISCSI_FLAG_CMD_READ) {
		data_buf = &iser_task->data[ISER_DIR_IN];
		prot_buf = &iser_task->prot[ISER_DIR_IN];
	} else {
		data_buf = &iser_task->data[ISER_DIR_OUT];
		prot_buf = &iser_task->prot[ISER_DIR_OUT];
	}

	if (scsi_sg_count(sc)) { /* using a scatter list */
		data_buf->buf  = scsi_sglist(sc);
		data_buf->size = scsi_sg_count(sc);
	}
	data_buf->data_len = scsi_bufflen(sc);

	if (scsi_prot_sg_count(sc)) {
		prot_buf->buf  = scsi_prot_sglist(sc);
		prot_buf->size = scsi_prot_sg_count(sc);
		prot_buf->data_len = data_buf->data_len >>
				     ilog2(sc->device->sector_size) * 8;
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

	err = iser_post_send(&iser_conn->ib_conn, tx_desc,
			     iser_signal_comp(sig_count));
	if (!err)
		return 0;

send_command_error:
	iser_err("conn %p failed task->itt %d err %d\n",conn, task->itt, err);
	return err;
}

/**
 * iser_send_data_out - send data out PDU
 */
int iser_send_data_out(struct iscsi_conn *conn,
		       struct iscsi_task *task,
		       struct iscsi_data *hdr)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_tx_desc *tx_desc = NULL;
	struct iser_regd_buf *regd_buf;
	unsigned long buf_offset;
	unsigned long data_seg_len;
	uint32_t itt;
	int err = 0;
	struct ib_sge *tx_dsg;

	itt = (__force uint32_t)hdr->itt;
	data_seg_len = ntoh24(hdr->dlength);
	buf_offset   = ntohl(hdr->offset);

	iser_dbg("%s itt %d dseg_len %d offset %d\n",
		 __func__,(int)itt,(int)data_seg_len,(int)buf_offset);

	tx_desc = kmem_cache_zalloc(ig.desc_cache, GFP_ATOMIC);
	if (tx_desc == NULL) {
		iser_err("Failed to alloc desc for post dataout\n");
		return -ENOMEM;
	}

	tx_desc->type = ISCSI_TX_DATAOUT;
	tx_desc->iser_header.flags = ISER_VER;
	memcpy(&tx_desc->iscsi_header, hdr, sizeof(struct iscsi_hdr));

	/* build the tx desc */
	iser_initialize_task_headers(task, tx_desc);

	regd_buf = &iser_task->rdma_regd[ISER_DIR_OUT];
	tx_dsg = &tx_desc->tx_sg[1];
	tx_dsg->addr    = regd_buf->reg.va + buf_offset;
	tx_dsg->length  = data_seg_len;
	tx_dsg->lkey    = regd_buf->reg.lkey;
	tx_desc->num_sge = 2;

	if (buf_offset + data_seg_len > iser_task->data[ISER_DIR_OUT].data_len) {
		iser_err("Offset:%ld & DSL:%ld in Data-Out "
			 "inconsistent with total len:%ld, itt:%d\n",
			 buf_offset, data_seg_len,
			 iser_task->data[ISER_DIR_OUT].data_len, itt);
		err = -EINVAL;
		goto send_data_out_error;
	}
	iser_dbg("data-out itt: %d, offset: %ld, sz: %ld\n",
		 itt, buf_offset, data_seg_len);


	err = iser_post_send(&iser_conn->ib_conn, tx_desc, true);
	if (!err)
		return 0;

send_data_out_error:
	kmem_cache_free(ig.desc_cache, tx_desc);
	iser_err("conn %p failed err %d\n",conn, err);
	return err;
}

int iser_send_control(struct iscsi_conn *conn,
		      struct iscsi_task *task)
{
	struct iser_conn *iser_conn = conn->dd_data;
	struct iscsi_iser_task *iser_task = task->dd_data;
	struct iser_tx_desc *mdesc = &iser_task->desc;
	unsigned long data_seg_len;
	int err = 0;
	struct iser_device *device;

	/* build the tx desc regd header and add it to the tx desc dto */
	mdesc->type = ISCSI_TX_CONTROL;
	iser_create_send_desc(iser_conn, mdesc);

	device = iser_conn->ib_conn.device;

	data_seg_len = ntoh24(task->hdr->dlength);

	if (data_seg_len > 0) {
		struct ib_sge *tx_dsg = &mdesc->tx_sg[1];
		if (task != conn->login_task) {
			iser_err("data present on non login task!!!\n");
			goto send_control_error;
		}

		ib_dma_sync_single_for_cpu(device->ib_device,
			iser_conn->login_req_dma, task->data_count,
			DMA_TO_DEVICE);

		memcpy(iser_conn->login_req_buf, task->data, task->data_count);

		ib_dma_sync_single_for_device(device->ib_device,
			iser_conn->login_req_dma, task->data_count,
			DMA_TO_DEVICE);

		tx_dsg->addr    = iser_conn->login_req_dma;
		tx_dsg->length  = task->data_count;
		tx_dsg->lkey    = device->mr->lkey;
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

	err = iser_post_send(&iser_conn->ib_conn, mdesc, true);
	if (!err)
		return 0;

send_control_error:
	iser_err("conn %p failed err %d\n",conn, err);
	return err;
}

/**
 * iser_rcv_dto_completion - recv DTO completion
 */
void iser_rcv_completion(struct iser_rx_desc *rx_desc,
			 unsigned long rx_xfer_len,
			 struct ib_conn *ib_conn)
{
	struct iser_conn *iser_conn = container_of(ib_conn, struct iser_conn,
						   ib_conn);
	struct iscsi_hdr *hdr;
	u64 rx_dma;
	int rx_buflen, outstanding, count, err;

	/* differentiate between login to all other PDUs */
	if ((char *)rx_desc == iser_conn->login_resp_buf) {
		rx_dma = iser_conn->login_resp_dma;
		rx_buflen = ISER_RX_LOGIN_SIZE;
	} else {
		rx_dma = rx_desc->dma_addr;
		rx_buflen = ISER_RX_PAYLOAD_SIZE;
	}

	ib_dma_sync_single_for_cpu(ib_conn->device->ib_device, rx_dma,
				   rx_buflen, DMA_FROM_DEVICE);

	hdr = &rx_desc->iscsi_header;

	iser_dbg("op 0x%x itt 0x%x dlen %d\n", hdr->opcode,
			hdr->itt, (int)(rx_xfer_len - ISER_HEADERS_LEN));

	iscsi_iser_recv(iser_conn->iscsi_conn, hdr, rx_desc->data,
			rx_xfer_len - ISER_HEADERS_LEN);

	ib_dma_sync_single_for_device(ib_conn->device->ib_device, rx_dma,
				      rx_buflen, DMA_FROM_DEVICE);

	/* decrementing conn->post_recv_buf_count only --after-- freeing the   *
	 * task eliminates the need to worry on tasks which are completed in   *
	 * parallel to the execution of iser_conn_term. So the code that waits *
	 * for the posted rx bufs refcount to become zero handles everything   */
	ib_conn->post_recv_buf_count--;

	if (rx_dma == iser_conn->login_resp_dma)
		return;

	outstanding = ib_conn->post_recv_buf_count;
	if (outstanding + iser_conn->min_posted_rx <= iser_conn->qp_max_recv_dtos) {
		count = min(iser_conn->qp_max_recv_dtos - outstanding,
			    iser_conn->min_posted_rx);
		err = iser_post_recvm(iser_conn, count);
		if (err)
			iser_err("posting %d rx bufs err %d\n", count, err);
	}
}

void iser_snd_completion(struct iser_tx_desc *tx_desc,
			struct ib_conn *ib_conn)
{
	struct iscsi_task *task;
	struct iser_device *device = ib_conn->device;

	if (tx_desc->type == ISCSI_TX_DATAOUT) {
		ib_dma_unmap_single(device->ib_device, tx_desc->dma_addr,
					ISER_HEADERS_LEN, DMA_TO_DEVICE);
		kmem_cache_free(ig.desc_cache, tx_desc);
		tx_desc = NULL;
	}

	if (tx_desc && tx_desc->type == ISCSI_TX_CONTROL) {
		/* this arithmetic is legal by libiscsi dd_data allocation */
		task = (void *) ((long)(void *)tx_desc -
				  sizeof(struct iscsi_task));
		if (task->hdr->itt == RESERVED_ITT)
			iscsi_put_task(task);
	}
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

	memset(&iser_task->rdma_regd[ISER_DIR_IN], 0,
	       sizeof(struct iser_regd_buf));
	memset(&iser_task->rdma_regd[ISER_DIR_OUT], 0,
	       sizeof(struct iser_regd_buf));
}

void iser_task_rdma_finalize(struct iscsi_iser_task *iser_task)
{
	struct iser_device *device = iser_task->iser_conn->ib_conn.device;
	int is_rdma_data_aligned = 1;
	int is_rdma_prot_aligned = 1;
	int prot_count = scsi_prot_sg_count(iser_task->sc);

	/* if we were reading, copy back to unaligned sglist,
	 * anyway dma_unmap and free the copy
	 */
	if (iser_task->data_copy[ISER_DIR_IN].copy_buf != NULL) {
		is_rdma_data_aligned = 0;
		iser_finalize_rdma_unaligned_sg(iser_task,
						&iser_task->data[ISER_DIR_IN],
						&iser_task->data_copy[ISER_DIR_IN],
						ISER_DIR_IN);
	}

	if (iser_task->data_copy[ISER_DIR_OUT].copy_buf != NULL) {
		is_rdma_data_aligned = 0;
		iser_finalize_rdma_unaligned_sg(iser_task,
						&iser_task->data[ISER_DIR_OUT],
						&iser_task->data_copy[ISER_DIR_OUT],
						ISER_DIR_OUT);
	}

	if (iser_task->prot_copy[ISER_DIR_IN].copy_buf != NULL) {
		is_rdma_prot_aligned = 0;
		iser_finalize_rdma_unaligned_sg(iser_task,
						&iser_task->prot[ISER_DIR_IN],
						&iser_task->prot_copy[ISER_DIR_IN],
						ISER_DIR_IN);
	}

	if (iser_task->prot_copy[ISER_DIR_OUT].copy_buf != NULL) {
		is_rdma_prot_aligned = 0;
		iser_finalize_rdma_unaligned_sg(iser_task,
						&iser_task->prot[ISER_DIR_OUT],
						&iser_task->prot_copy[ISER_DIR_OUT],
						ISER_DIR_OUT);
	}

	if (iser_task->dir[ISER_DIR_IN]) {
		device->iser_unreg_rdma_mem(iser_task, ISER_DIR_IN);
		if (is_rdma_data_aligned)
			iser_dma_unmap_task_data(iser_task,
						 &iser_task->data[ISER_DIR_IN]);
		if (prot_count && is_rdma_prot_aligned)
			iser_dma_unmap_task_data(iser_task,
						 &iser_task->prot[ISER_DIR_IN]);
	}

	if (iser_task->dir[ISER_DIR_OUT]) {
		device->iser_unreg_rdma_mem(iser_task, ISER_DIR_OUT);
		if (is_rdma_data_aligned)
			iser_dma_unmap_task_data(iser_task,
						 &iser_task->data[ISER_DIR_OUT]);
		if (prot_count && is_rdma_prot_aligned)
			iser_dma_unmap_task_data(iser_task,
						 &iser_task->prot[ISER_DIR_OUT]);
	}
}

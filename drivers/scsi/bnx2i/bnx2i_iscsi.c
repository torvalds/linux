/*
 * bnx2i_iscsi.c: Broadcom NetXtreme II iSCSI driver.
 *
 * Copyright (c) 2006 - 2012 Broadcom Corporation
 * Copyright (c) 2007, 2008 Red Hat, Inc.  All rights reserved.
 * Copyright (c) 2007, 2008 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 * Maintained by: Eddie Wai (eddie.wai@broadcom.com)
 */

#include <linux/slab.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libiscsi.h>
#include "bnx2i.h"

struct scsi_transport_template *bnx2i_scsi_xport_template;
struct iscsi_transport bnx2i_iscsi_transport;
static struct scsi_host_template bnx2i_host_template;

/*
 * Global endpoint resource info
 */
static DEFINE_SPINLOCK(bnx2i_resc_lock); /* protects global resources */

DECLARE_PER_CPU(struct bnx2i_percpu_s, bnx2i_percpu);

static int bnx2i_adapter_ready(struct bnx2i_hba *hba)
{
	int retval = 0;

	if (!hba || !test_bit(ADAPTER_STATE_UP, &hba->adapter_state) ||
	    test_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state) ||
	    test_bit(ADAPTER_STATE_LINK_DOWN, &hba->adapter_state))
		retval = -EPERM;
	return retval;
}

/**
 * bnx2i_get_write_cmd_bd_idx - identifies various BD bookmarks
 * @cmd:		iscsi cmd struct pointer
 * @buf_off:		absolute buffer offset
 * @start_bd_off:	u32 pointer to return the offset within the BD
 *			indicated by 'start_bd_idx' on which 'buf_off' falls
 * @start_bd_idx:	index of the BD on which 'buf_off' falls
 *
 * identifies & marks various bd info for scsi command's imm data,
 * unsolicited data and the first solicited data seq.
 */
static void bnx2i_get_write_cmd_bd_idx(struct bnx2i_cmd *cmd, u32 buf_off,
				       u32 *start_bd_off, u32 *start_bd_idx)
{
	struct iscsi_bd *bd_tbl = cmd->io_tbl.bd_tbl;
	u32 cur_offset = 0;
	u32 cur_bd_idx = 0;

	if (buf_off) {
		while (buf_off >= (cur_offset + bd_tbl->buffer_length)) {
			cur_offset += bd_tbl->buffer_length;
			cur_bd_idx++;
			bd_tbl++;
		}
	}

	*start_bd_off = buf_off - cur_offset;
	*start_bd_idx = cur_bd_idx;
}

/**
 * bnx2i_setup_write_cmd_bd_info - sets up BD various information
 * @task:	transport layer's cmd struct pointer
 *
 * identifies & marks various bd info for scsi command's immediate data,
 * unsolicited data and first solicited data seq which includes BD start
 * index & BD buf off. his function takes into account iscsi parameter such
 * as immediate data and unsolicited data is support on this connection.
 */
static void bnx2i_setup_write_cmd_bd_info(struct iscsi_task *task)
{
	struct bnx2i_cmd *cmd = task->dd_data;
	u32 start_bd_offset;
	u32 start_bd_idx;
	u32 buffer_offset = 0;
	u32 cmd_len = cmd->req.total_data_transfer_length;

	/* if ImmediateData is turned off & IntialR2T is turned on,
	 * there will be no immediate or unsolicited data, just return.
	 */
	if (!iscsi_task_has_unsol_data(task) && !task->imm_count)
		return;

	/* Immediate data */
	buffer_offset += task->imm_count;
	if (task->imm_count == cmd_len)
		return;

	if (iscsi_task_has_unsol_data(task)) {
		bnx2i_get_write_cmd_bd_idx(cmd, buffer_offset,
					   &start_bd_offset, &start_bd_idx);
		cmd->req.ud_buffer_offset = start_bd_offset;
		cmd->req.ud_start_bd_index = start_bd_idx;
		buffer_offset += task->unsol_r2t.data_length;
	}

	if (buffer_offset != cmd_len) {
		bnx2i_get_write_cmd_bd_idx(cmd, buffer_offset,
					   &start_bd_offset, &start_bd_idx);
		if ((start_bd_offset > task->conn->session->first_burst) ||
		    (start_bd_idx > scsi_sg_count(cmd->scsi_cmd))) {
			int i = 0;

			iscsi_conn_printk(KERN_ALERT, task->conn,
					  "bnx2i- error, buf offset 0x%x "
					  "bd_valid %d use_sg %d\n",
					  buffer_offset, cmd->io_tbl.bd_valid,
					  scsi_sg_count(cmd->scsi_cmd));
			for (i = 0; i < cmd->io_tbl.bd_valid; i++)
				iscsi_conn_printk(KERN_ALERT, task->conn,
						  "bnx2i err, bd[%d]: len %x\n",
						  i, cmd->io_tbl.bd_tbl[i].\
						  buffer_length);
		}
		cmd->req.sd_buffer_offset = start_bd_offset;
		cmd->req.sd_start_bd_index = start_bd_idx;
	}
}



/**
 * bnx2i_map_scsi_sg - maps IO buffer and prepares the BD table
 * @hba:	adapter instance
 * @cmd:	iscsi cmd struct pointer
 *
 * map SG list
 */
static int bnx2i_map_scsi_sg(struct bnx2i_hba *hba, struct bnx2i_cmd *cmd)
{
	struct scsi_cmnd *sc = cmd->scsi_cmd;
	struct iscsi_bd *bd = cmd->io_tbl.bd_tbl;
	struct scatterlist *sg;
	int byte_count = 0;
	int bd_count = 0;
	int sg_count;
	int sg_len;
	u64 addr;
	int i;

	BUG_ON(scsi_sg_count(sc) > ISCSI_MAX_BDS_PER_CMD);

	sg_count = scsi_dma_map(sc);

	scsi_for_each_sg(sc, sg, sg_count, i) {
		sg_len = sg_dma_len(sg);
		addr = (u64) sg_dma_address(sg);
		bd[bd_count].buffer_addr_lo = addr & 0xffffffff;
		bd[bd_count].buffer_addr_hi = addr >> 32;
		bd[bd_count].buffer_length = sg_len;
		bd[bd_count].flags = 0;
		if (bd_count == 0)
			bd[bd_count].flags = ISCSI_BD_FIRST_IN_BD_CHAIN;

		byte_count += sg_len;
		bd_count++;
	}

	if (bd_count)
		bd[bd_count - 1].flags |= ISCSI_BD_LAST_IN_BD_CHAIN;

	BUG_ON(byte_count != scsi_bufflen(sc));
	return bd_count;
}

/**
 * bnx2i_iscsi_map_sg_list - maps SG list
 * @cmd:	iscsi cmd struct pointer
 *
 * creates BD list table for the command
 */
static void bnx2i_iscsi_map_sg_list(struct bnx2i_cmd *cmd)
{
	int bd_count;

	bd_count  = bnx2i_map_scsi_sg(cmd->conn->hba, cmd);
	if (!bd_count) {
		struct iscsi_bd *bd = cmd->io_tbl.bd_tbl;

		bd[0].buffer_addr_lo = bd[0].buffer_addr_hi = 0;
		bd[0].buffer_length = bd[0].flags = 0;
	}
	cmd->io_tbl.bd_valid = bd_count;
}


/**
 * bnx2i_iscsi_unmap_sg_list - unmaps SG list
 * @cmd:	iscsi cmd struct pointer
 *
 * unmap IO buffers and invalidate the BD table
 */
void bnx2i_iscsi_unmap_sg_list(struct bnx2i_cmd *cmd)
{
	struct scsi_cmnd *sc = cmd->scsi_cmd;

	if (cmd->io_tbl.bd_valid && sc) {
		scsi_dma_unmap(sc);
		cmd->io_tbl.bd_valid = 0;
	}
}

static void bnx2i_setup_cmd_wqe_template(struct bnx2i_cmd *cmd)
{
	memset(&cmd->req, 0x00, sizeof(cmd->req));
	cmd->req.op_code = 0xFF;
	cmd->req.bd_list_addr_lo = (u32) cmd->io_tbl.bd_tbl_dma;
	cmd->req.bd_list_addr_hi =
		(u32) ((u64) cmd->io_tbl.bd_tbl_dma >> 32);

}


/**
 * bnx2i_bind_conn_to_iscsi_cid - bind conn structure to 'iscsi_cid'
 * @hba:	pointer to adapter instance
 * @conn:	pointer to iscsi connection
 * @iscsi_cid:	iscsi context ID, range 0 - (MAX_CONN - 1)
 *
 * update iscsi cid table entry with connection pointer. This enables
 *	driver to quickly get hold of connection structure pointer in
 *	completion/interrupt thread using iscsi context ID
 */
static int bnx2i_bind_conn_to_iscsi_cid(struct bnx2i_hba *hba,
					struct bnx2i_conn *bnx2i_conn,
					u32 iscsi_cid)
{
	if (hba && hba->cid_que.conn_cid_tbl[iscsi_cid]) {
		iscsi_conn_printk(KERN_ALERT, bnx2i_conn->cls_conn->dd_data,
				 "conn bind - entry #%d not free\n", iscsi_cid);
		return -EBUSY;
	}

	hba->cid_que.conn_cid_tbl[iscsi_cid] = bnx2i_conn;
	return 0;
}


/**
 * bnx2i_get_conn_from_id - maps an iscsi cid to corresponding conn ptr
 * @hba:	pointer to adapter instance
 * @iscsi_cid:	iscsi context ID, range 0 - (MAX_CONN - 1)
 */
struct bnx2i_conn *bnx2i_get_conn_from_id(struct bnx2i_hba *hba,
					  u16 iscsi_cid)
{
	if (!hba->cid_que.conn_cid_tbl) {
		printk(KERN_ERR "bnx2i: ERROR - missing conn<->cid table\n");
		return NULL;

	} else if (iscsi_cid >= hba->max_active_conns) {
		printk(KERN_ERR "bnx2i: wrong cid #%d\n", iscsi_cid);
		return NULL;
	}
	return hba->cid_que.conn_cid_tbl[iscsi_cid];
}


/**
 * bnx2i_alloc_iscsi_cid - allocates a iscsi_cid from free pool
 * @hba:	pointer to adapter instance
 */
static u32 bnx2i_alloc_iscsi_cid(struct bnx2i_hba *hba)
{
	int idx;

	if (!hba->cid_que.cid_free_cnt)
		return -1;

	idx = hba->cid_que.cid_q_cons_idx;
	hba->cid_que.cid_q_cons_idx++;
	if (hba->cid_que.cid_q_cons_idx == hba->cid_que.cid_q_max_idx)
		hba->cid_que.cid_q_cons_idx = 0;

	hba->cid_que.cid_free_cnt--;
	return hba->cid_que.cid_que[idx];
}


/**
 * bnx2i_free_iscsi_cid - returns tcp port to free list
 * @hba: 		pointer to adapter instance
 * @iscsi_cid:		iscsi context ID to free
 */
static void bnx2i_free_iscsi_cid(struct bnx2i_hba *hba, u16 iscsi_cid)
{
	int idx;

	if (iscsi_cid == (u16) -1)
		return;

	hba->cid_que.cid_free_cnt++;

	idx = hba->cid_que.cid_q_prod_idx;
	hba->cid_que.cid_que[idx] = iscsi_cid;
	hba->cid_que.conn_cid_tbl[iscsi_cid] = NULL;
	hba->cid_que.cid_q_prod_idx++;
	if (hba->cid_que.cid_q_prod_idx == hba->cid_que.cid_q_max_idx)
		hba->cid_que.cid_q_prod_idx = 0;
}


/**
 * bnx2i_setup_free_cid_que - sets up free iscsi cid queue
 * @hba:	pointer to adapter instance
 *
 * allocates memory for iscsi cid queue & 'cid - conn ptr' mapping table,
 * 	and initialize table attributes
 */
static int bnx2i_setup_free_cid_que(struct bnx2i_hba *hba)
{
	int mem_size;
	int i;

	mem_size = hba->max_active_conns * sizeof(u32);
	mem_size = (mem_size + (PAGE_SIZE - 1)) & PAGE_MASK;

	hba->cid_que.cid_que_base = kmalloc(mem_size, GFP_KERNEL);
	if (!hba->cid_que.cid_que_base)
		return -ENOMEM;

	mem_size = hba->max_active_conns * sizeof(struct bnx2i_conn *);
	mem_size = (mem_size + (PAGE_SIZE - 1)) & PAGE_MASK;
	hba->cid_que.conn_cid_tbl = kmalloc(mem_size, GFP_KERNEL);
	if (!hba->cid_que.conn_cid_tbl) {
		kfree(hba->cid_que.cid_que_base);
		hba->cid_que.cid_que_base = NULL;
		return -ENOMEM;
	}

	hba->cid_que.cid_que = (u32 *)hba->cid_que.cid_que_base;
	hba->cid_que.cid_q_prod_idx = 0;
	hba->cid_que.cid_q_cons_idx = 0;
	hba->cid_que.cid_q_max_idx = hba->max_active_conns;
	hba->cid_que.cid_free_cnt = hba->max_active_conns;

	for (i = 0; i < hba->max_active_conns; i++) {
		hba->cid_que.cid_que[i] = i;
		hba->cid_que.conn_cid_tbl[i] = NULL;
	}
	return 0;
}


/**
 * bnx2i_release_free_cid_que - releases 'iscsi_cid' queue resources
 * @hba:	pointer to adapter instance
 */
static void bnx2i_release_free_cid_que(struct bnx2i_hba *hba)
{
	kfree(hba->cid_que.cid_que_base);
	hba->cid_que.cid_que_base = NULL;

	kfree(hba->cid_que.conn_cid_tbl);
	hba->cid_que.conn_cid_tbl = NULL;
}


/**
 * bnx2i_alloc_ep - allocates ep structure from global pool
 * @hba:	pointer to adapter instance
 *
 * routine allocates a free endpoint structure from global pool and
 *	a tcp port to be used for this connection.  Global resource lock,
 *	'bnx2i_resc_lock' is held while accessing shared global data structures
 */
static struct iscsi_endpoint *bnx2i_alloc_ep(struct bnx2i_hba *hba)
{
	struct iscsi_endpoint *ep;
	struct bnx2i_endpoint *bnx2i_ep;
	u32 ec_div;

	ep = iscsi_create_endpoint(sizeof(*bnx2i_ep));
	if (!ep) {
		printk(KERN_ERR "bnx2i: Could not allocate ep\n");
		return NULL;
	}

	bnx2i_ep = ep->dd_data;
	bnx2i_ep->cls_ep = ep;
	INIT_LIST_HEAD(&bnx2i_ep->link);
	bnx2i_ep->state = EP_STATE_IDLE;
	bnx2i_ep->ep_iscsi_cid = (u16) -1;
	bnx2i_ep->hba = hba;
	bnx2i_ep->hba_age = hba->age;

	ec_div = event_coal_div;
	while (ec_div >>= 1)
		bnx2i_ep->ec_shift += 1;

	hba->ofld_conns_active++;
	init_waitqueue_head(&bnx2i_ep->ofld_wait);
	return ep;
}


/**
 * bnx2i_free_ep - free endpoint
 * @ep:		pointer to iscsi endpoint structure
 */
static void bnx2i_free_ep(struct iscsi_endpoint *ep)
{
	struct bnx2i_endpoint *bnx2i_ep = ep->dd_data;
	unsigned long flags;

	spin_lock_irqsave(&bnx2i_resc_lock, flags);
	bnx2i_ep->state = EP_STATE_IDLE;
	bnx2i_ep->hba->ofld_conns_active--;

	if (bnx2i_ep->ep_iscsi_cid != (u16) -1)
		bnx2i_free_iscsi_cid(bnx2i_ep->hba, bnx2i_ep->ep_iscsi_cid);

	if (bnx2i_ep->conn) {
		bnx2i_ep->conn->ep = NULL;
		bnx2i_ep->conn = NULL;
	}

	bnx2i_ep->hba = NULL;
	spin_unlock_irqrestore(&bnx2i_resc_lock, flags);
	iscsi_destroy_endpoint(ep);
}


/**
 * bnx2i_alloc_bdt - allocates buffer descriptor (BD) table for the command
 * @hba:	adapter instance pointer
 * @session:	iscsi session pointer
 * @cmd:	iscsi command structure
 */
static int bnx2i_alloc_bdt(struct bnx2i_hba *hba, struct iscsi_session *session,
			   struct bnx2i_cmd *cmd)
{
	struct io_bdt *io = &cmd->io_tbl;
	struct iscsi_bd *bd;

	io->bd_tbl = dma_alloc_coherent(&hba->pcidev->dev,
					ISCSI_MAX_BDS_PER_CMD * sizeof(*bd),
					&io->bd_tbl_dma, GFP_KERNEL);
	if (!io->bd_tbl) {
		iscsi_session_printk(KERN_ERR, session, "Could not "
				     "allocate bdt.\n");
		return -ENOMEM;
	}
	io->bd_valid = 0;
	return 0;
}

/**
 * bnx2i_destroy_cmd_pool - destroys iscsi command pool and release BD table
 * @hba:	adapter instance pointer
 * @session:	iscsi session pointer
 * @cmd:	iscsi command structure
 */
static void bnx2i_destroy_cmd_pool(struct bnx2i_hba *hba,
				   struct iscsi_session *session)
{
	int i;

	for (i = 0; i < session->cmds_max; i++) {
		struct iscsi_task *task = session->cmds[i];
		struct bnx2i_cmd *cmd = task->dd_data;

		if (cmd->io_tbl.bd_tbl)
			dma_free_coherent(&hba->pcidev->dev,
					  ISCSI_MAX_BDS_PER_CMD *
					  sizeof(struct iscsi_bd),
					  cmd->io_tbl.bd_tbl,
					  cmd->io_tbl.bd_tbl_dma);
	}

}


/**
 * bnx2i_setup_cmd_pool - sets up iscsi command pool for the session
 * @hba:	adapter instance pointer
 * @session:	iscsi session pointer
 */
static int bnx2i_setup_cmd_pool(struct bnx2i_hba *hba,
				struct iscsi_session *session)
{
	int i;

	for (i = 0; i < session->cmds_max; i++) {
		struct iscsi_task *task = session->cmds[i];
		struct bnx2i_cmd *cmd = task->dd_data;

		task->hdr = &cmd->hdr;
		task->hdr_max = sizeof(struct iscsi_hdr);

		if (bnx2i_alloc_bdt(hba, session, cmd))
			goto free_bdts;
	}

	return 0;

free_bdts:
	bnx2i_destroy_cmd_pool(hba, session);
	return -ENOMEM;
}


/**
 * bnx2i_setup_mp_bdt - allocate BD table resources
 * @hba:	pointer to adapter structure
 *
 * Allocate memory for dummy buffer and associated BD
 * table to be used by middle path (MP) requests
 */
static int bnx2i_setup_mp_bdt(struct bnx2i_hba *hba)
{
	int rc = 0;
	struct iscsi_bd *mp_bdt;
	u64 addr;

	hba->mp_bd_tbl = dma_alloc_coherent(&hba->pcidev->dev, PAGE_SIZE,
					    &hba->mp_bd_dma, GFP_KERNEL);
	if (!hba->mp_bd_tbl) {
		printk(KERN_ERR "unable to allocate Middle Path BDT\n");
		rc = -1;
		goto out;
	}

	hba->dummy_buffer = dma_alloc_coherent(&hba->pcidev->dev, PAGE_SIZE,
					       &hba->dummy_buf_dma, GFP_KERNEL);
	if (!hba->dummy_buffer) {
		printk(KERN_ERR "unable to alloc Middle Path Dummy Buffer\n");
		dma_free_coherent(&hba->pcidev->dev, PAGE_SIZE,
				  hba->mp_bd_tbl, hba->mp_bd_dma);
		hba->mp_bd_tbl = NULL;
		rc = -1;
		goto out;
	}

	mp_bdt = (struct iscsi_bd *) hba->mp_bd_tbl;
	addr = (unsigned long) hba->dummy_buf_dma;
	mp_bdt->buffer_addr_lo = addr & 0xffffffff;
	mp_bdt->buffer_addr_hi = addr >> 32;
	mp_bdt->buffer_length = PAGE_SIZE;
	mp_bdt->flags = ISCSI_BD_LAST_IN_BD_CHAIN |
			ISCSI_BD_FIRST_IN_BD_CHAIN;
out:
	return rc;
}


/**
 * bnx2i_free_mp_bdt - releases ITT back to free pool
 * @hba:	pointer to adapter instance
 *
 * free MP dummy buffer and associated BD table
 */
static void bnx2i_free_mp_bdt(struct bnx2i_hba *hba)
{
	if (hba->mp_bd_tbl) {
		dma_free_coherent(&hba->pcidev->dev, PAGE_SIZE,
				  hba->mp_bd_tbl, hba->mp_bd_dma);
		hba->mp_bd_tbl = NULL;
	}
	if (hba->dummy_buffer) {
		dma_free_coherent(&hba->pcidev->dev, PAGE_SIZE,
				  hba->dummy_buffer, hba->dummy_buf_dma);
		hba->dummy_buffer = NULL;
	}
		return;
}

/**
 * bnx2i_drop_session - notifies iscsid of connection error.
 * @hba:	adapter instance pointer
 * @session:	iscsi session pointer
 *
 * This notifies iscsid that there is a error, so it can initiate
 * recovery.
 *
 * This relies on caller using the iscsi class iterator so the object
 * is refcounted and does not disapper from under us.
 */
void bnx2i_drop_session(struct iscsi_cls_session *cls_session)
{
	iscsi_session_failure(cls_session->dd_data, ISCSI_ERR_CONN_FAILED);
}

/**
 * bnx2i_ep_destroy_list_add - add an entry to EP destroy list
 * @hba:	pointer to adapter instance
 * @ep:		pointer to endpoint (transport indentifier) structure
 *
 * EP destroy queue manager
 */
static int bnx2i_ep_destroy_list_add(struct bnx2i_hba *hba,
				     struct bnx2i_endpoint *ep)
{
	write_lock_bh(&hba->ep_rdwr_lock);
	list_add_tail(&ep->link, &hba->ep_destroy_list);
	write_unlock_bh(&hba->ep_rdwr_lock);
	return 0;
}

/**
 * bnx2i_ep_destroy_list_del - add an entry to EP destroy list
 *
 * @hba: 		pointer to adapter instance
 * @ep: 		pointer to endpoint (transport indentifier) structure
 *
 * EP destroy queue manager
 */
static int bnx2i_ep_destroy_list_del(struct bnx2i_hba *hba,
				     struct bnx2i_endpoint *ep)
{
	write_lock_bh(&hba->ep_rdwr_lock);
	list_del_init(&ep->link);
	write_unlock_bh(&hba->ep_rdwr_lock);

	return 0;
}

/**
 * bnx2i_ep_ofld_list_add - add an entry to ep offload pending list
 * @hba:	pointer to adapter instance
 * @ep:		pointer to endpoint (transport indentifier) structure
 *
 * pending conn offload completion queue manager
 */
static int bnx2i_ep_ofld_list_add(struct bnx2i_hba *hba,
				  struct bnx2i_endpoint *ep)
{
	write_lock_bh(&hba->ep_rdwr_lock);
	list_add_tail(&ep->link, &hba->ep_ofld_list);
	write_unlock_bh(&hba->ep_rdwr_lock);
	return 0;
}

/**
 * bnx2i_ep_ofld_list_del - add an entry to ep offload pending list
 * @hba: 		pointer to adapter instance
 * @ep: 		pointer to endpoint (transport indentifier) structure
 *
 * pending conn offload completion queue manager
 */
static int bnx2i_ep_ofld_list_del(struct bnx2i_hba *hba,
				  struct bnx2i_endpoint *ep)
{
	write_lock_bh(&hba->ep_rdwr_lock);
	list_del_init(&ep->link);
	write_unlock_bh(&hba->ep_rdwr_lock);
	return 0;
}


/**
 * bnx2i_find_ep_in_ofld_list - find iscsi_cid in pending list of endpoints
 *
 * @hba: 		pointer to adapter instance
 * @iscsi_cid:		iscsi context ID to find
 *
 */
struct bnx2i_endpoint *
bnx2i_find_ep_in_ofld_list(struct bnx2i_hba *hba, u32 iscsi_cid)
{
	struct list_head *list;
	struct list_head *tmp;
	struct bnx2i_endpoint *ep;

	read_lock_bh(&hba->ep_rdwr_lock);
	list_for_each_safe(list, tmp, &hba->ep_ofld_list) {
		ep = (struct bnx2i_endpoint *)list;

		if (ep->ep_iscsi_cid == iscsi_cid)
			break;
		ep = NULL;
	}
	read_unlock_bh(&hba->ep_rdwr_lock);

	if (!ep)
		printk(KERN_ERR "l5 cid %d not found\n", iscsi_cid);
	return ep;
}

/**
 * bnx2i_find_ep_in_destroy_list - find iscsi_cid in destroy list
 * @hba: 		pointer to adapter instance
 * @iscsi_cid:		iscsi context ID to find
 *
 */
struct bnx2i_endpoint *
bnx2i_find_ep_in_destroy_list(struct bnx2i_hba *hba, u32 iscsi_cid)
{
	struct list_head *list;
	struct list_head *tmp;
	struct bnx2i_endpoint *ep;

	read_lock_bh(&hba->ep_rdwr_lock);
	list_for_each_safe(list, tmp, &hba->ep_destroy_list) {
		ep = (struct bnx2i_endpoint *)list;

		if (ep->ep_iscsi_cid == iscsi_cid)
			break;
		ep = NULL;
	}
	read_unlock_bh(&hba->ep_rdwr_lock);

	if (!ep)
		printk(KERN_ERR "l5 cid %d not found\n", iscsi_cid);

	return ep;
}

/**
 * bnx2i_ep_active_list_add - add an entry to ep active list
 * @hba:	pointer to adapter instance
 * @ep:		pointer to endpoint (transport indentifier) structure
 *
 * current active conn queue manager
 */
static void bnx2i_ep_active_list_add(struct bnx2i_hba *hba,
				     struct bnx2i_endpoint *ep)
{
	write_lock_bh(&hba->ep_rdwr_lock);
	list_add_tail(&ep->link, &hba->ep_active_list);
	write_unlock_bh(&hba->ep_rdwr_lock);
}


/**
 * bnx2i_ep_active_list_del - deletes an entry to ep active list
 * @hba:	pointer to adapter instance
 * @ep:		pointer to endpoint (transport indentifier) structure
 *
 * current active conn queue manager
 */
static void bnx2i_ep_active_list_del(struct bnx2i_hba *hba,
				     struct bnx2i_endpoint *ep)
{
	write_lock_bh(&hba->ep_rdwr_lock);
	list_del_init(&ep->link);
	write_unlock_bh(&hba->ep_rdwr_lock);
}


/**
 * bnx2i_setup_host_queue_size - assigns shost->can_queue param
 * @hba:	pointer to adapter instance
 * @shost:	scsi host pointer
 *
 * Initializes 'can_queue' parameter based on how many outstanding commands
 * 	the device can handle. Each device 5708/5709/57710 has different
 *	capabilities
 */
static void bnx2i_setup_host_queue_size(struct bnx2i_hba *hba,
					struct Scsi_Host *shost)
{
	if (test_bit(BNX2I_NX2_DEV_5708, &hba->cnic_dev_type))
		shost->can_queue = ISCSI_MAX_CMDS_PER_HBA_5708;
	else if (test_bit(BNX2I_NX2_DEV_5709, &hba->cnic_dev_type))
		shost->can_queue = ISCSI_MAX_CMDS_PER_HBA_5709;
	else if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type))
		shost->can_queue = ISCSI_MAX_CMDS_PER_HBA_57710;
	else
		shost->can_queue = ISCSI_MAX_CMDS_PER_HBA_5708;
}


/**
 * bnx2i_alloc_hba - allocate and init adapter instance
 * @cnic:	cnic device pointer
 *
 * allocate & initialize adapter structure and call other
 *	support routines to do per adapter initialization
 */
struct bnx2i_hba *bnx2i_alloc_hba(struct cnic_dev *cnic)
{
	struct Scsi_Host *shost;
	struct bnx2i_hba *hba;

	shost = iscsi_host_alloc(&bnx2i_host_template, sizeof(*hba), 0);
	if (!shost)
		return NULL;
	shost->dma_boundary = cnic->pcidev->dma_mask;
	shost->transportt = bnx2i_scsi_xport_template;
	shost->max_id = ISCSI_MAX_CONNS_PER_HBA;
	shost->max_channel = 0;
	shost->max_lun = 512;
	shost->max_cmd_len = 16;

	hba = iscsi_host_priv(shost);
	hba->shost = shost;
	hba->netdev = cnic->netdev;
	/* Get PCI related information and update hba struct members */
	hba->pcidev = cnic->pcidev;
	pci_dev_get(hba->pcidev);
	hba->pci_did = hba->pcidev->device;
	hba->pci_vid = hba->pcidev->vendor;
	hba->pci_sdid = hba->pcidev->subsystem_device;
	hba->pci_svid = hba->pcidev->subsystem_vendor;
	hba->pci_func = PCI_FUNC(hba->pcidev->devfn);
	hba->pci_devno = PCI_SLOT(hba->pcidev->devfn);

	bnx2i_identify_device(hba, cnic);
	bnx2i_setup_host_queue_size(hba, shost);

	hba->reg_base = pci_resource_start(hba->pcidev, 0);
	if (test_bit(BNX2I_NX2_DEV_5709, &hba->cnic_dev_type)) {
		hba->regview = pci_iomap(hba->pcidev, 0, BNX2_MQ_CONFIG2);
		if (!hba->regview)
			goto ioreg_map_err;
	} else if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type)) {
		hba->regview = pci_iomap(hba->pcidev, 0, 4096);
		if (!hba->regview)
			goto ioreg_map_err;
	}

	if (bnx2i_setup_mp_bdt(hba))
		goto mp_bdt_mem_err;

	INIT_LIST_HEAD(&hba->ep_ofld_list);
	INIT_LIST_HEAD(&hba->ep_active_list);
	INIT_LIST_HEAD(&hba->ep_destroy_list);
	rwlock_init(&hba->ep_rdwr_lock);

	hba->mtu_supported = BNX2I_MAX_MTU_SUPPORTED;

	/* different values for 5708/5709/57710 */
	hba->max_active_conns = ISCSI_MAX_CONNS_PER_HBA;

	if (bnx2i_setup_free_cid_que(hba))
		goto cid_que_err;

	/* SQ/RQ/CQ size can be changed via sysfx interface */
	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type)) {
		if (sq_size && sq_size <= BNX2I_5770X_SQ_WQES_MAX)
			hba->max_sqes = sq_size;
		else
			hba->max_sqes = BNX2I_5770X_SQ_WQES_DEFAULT;
	} else {	/* 5706/5708/5709 */
		if (sq_size && sq_size <= BNX2I_570X_SQ_WQES_MAX)
			hba->max_sqes = sq_size;
		else
			hba->max_sqes = BNX2I_570X_SQ_WQES_DEFAULT;
	}

	hba->max_rqes = rq_size;
	hba->max_cqes = hba->max_sqes + rq_size;
	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type)) {
		if (hba->max_cqes > BNX2I_5770X_CQ_WQES_MAX)
			hba->max_cqes = BNX2I_5770X_CQ_WQES_MAX;
	} else if (hba->max_cqes > BNX2I_570X_CQ_WQES_MAX)
		hba->max_cqes = BNX2I_570X_CQ_WQES_MAX;

	hba->num_ccell = hba->max_sqes / 2;

	spin_lock_init(&hba->lock);
	mutex_init(&hba->net_dev_lock);
	init_waitqueue_head(&hba->eh_wait);
	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type)) {
		hba->hba_shutdown_tmo = 30 * HZ;
		hba->conn_teardown_tmo = 20 * HZ;
		hba->conn_ctx_destroy_tmo = 6 * HZ;
	} else {	/* 5706/5708/5709 */
		hba->hba_shutdown_tmo = 20 * HZ;
		hba->conn_teardown_tmo = 10 * HZ;
		hba->conn_ctx_destroy_tmo = 2 * HZ;
	}

#ifdef CONFIG_32BIT
	spin_lock_init(&hba->stat_lock);
#endif
	memset(&hba->stats, 0, sizeof(struct iscsi_stats_info));

	if (iscsi_host_add(shost, &hba->pcidev->dev))
		goto free_dump_mem;
	return hba;

free_dump_mem:
	bnx2i_release_free_cid_que(hba);
cid_que_err:
	bnx2i_free_mp_bdt(hba);
mp_bdt_mem_err:
	if (hba->regview) {
		pci_iounmap(hba->pcidev, hba->regview);
		hba->regview = NULL;
	}
ioreg_map_err:
	pci_dev_put(hba->pcidev);
	scsi_host_put(shost);
	return NULL;
}

/**
 * bnx2i_free_hba- releases hba structure and resources held by the adapter
 * @hba:	pointer to adapter instance
 *
 * free adapter structure and call various cleanup routines.
 */
void bnx2i_free_hba(struct bnx2i_hba *hba)
{
	struct Scsi_Host *shost = hba->shost;

	iscsi_host_remove(shost);
	INIT_LIST_HEAD(&hba->ep_ofld_list);
	INIT_LIST_HEAD(&hba->ep_active_list);
	INIT_LIST_HEAD(&hba->ep_destroy_list);
	pci_dev_put(hba->pcidev);

	if (hba->regview) {
		pci_iounmap(hba->pcidev, hba->regview);
		hba->regview = NULL;
	}
	bnx2i_free_mp_bdt(hba);
	bnx2i_release_free_cid_que(hba);
	iscsi_host_free(shost);
}

/**
 * bnx2i_conn_free_login_resources - free DMA resources used for login process
 * @hba:		pointer to adapter instance
 * @bnx2i_conn:		iscsi connection pointer
 *
 * Login related resources, mostly BDT & payload DMA memory is freed
 */
static void bnx2i_conn_free_login_resources(struct bnx2i_hba *hba,
					    struct bnx2i_conn *bnx2i_conn)
{
	if (bnx2i_conn->gen_pdu.resp_bd_tbl) {
		dma_free_coherent(&hba->pcidev->dev, PAGE_SIZE,
				  bnx2i_conn->gen_pdu.resp_bd_tbl,
				  bnx2i_conn->gen_pdu.resp_bd_dma);
		bnx2i_conn->gen_pdu.resp_bd_tbl = NULL;
	}

	if (bnx2i_conn->gen_pdu.req_bd_tbl) {
		dma_free_coherent(&hba->pcidev->dev, PAGE_SIZE,
				  bnx2i_conn->gen_pdu.req_bd_tbl,
				  bnx2i_conn->gen_pdu.req_bd_dma);
		bnx2i_conn->gen_pdu.req_bd_tbl = NULL;
	}

	if (bnx2i_conn->gen_pdu.resp_buf) {
		dma_free_coherent(&hba->pcidev->dev,
				  ISCSI_DEF_MAX_RECV_SEG_LEN,
				  bnx2i_conn->gen_pdu.resp_buf,
				  bnx2i_conn->gen_pdu.resp_dma_addr);
		bnx2i_conn->gen_pdu.resp_buf = NULL;
	}

	if (bnx2i_conn->gen_pdu.req_buf) {
		dma_free_coherent(&hba->pcidev->dev,
				  ISCSI_DEF_MAX_RECV_SEG_LEN,
				  bnx2i_conn->gen_pdu.req_buf,
				  bnx2i_conn->gen_pdu.req_dma_addr);
		bnx2i_conn->gen_pdu.req_buf = NULL;
	}
}

/**
 * bnx2i_conn_alloc_login_resources - alloc DMA resources for login/nop.
 * @hba:		pointer to adapter instance
 * @bnx2i_conn:		iscsi connection pointer
 *
 * Mgmt task DNA resources are allocated in this routine.
 */
static int bnx2i_conn_alloc_login_resources(struct bnx2i_hba *hba,
					    struct bnx2i_conn *bnx2i_conn)
{
	/* Allocate memory for login request/response buffers */
	bnx2i_conn->gen_pdu.req_buf =
		dma_alloc_coherent(&hba->pcidev->dev,
				   ISCSI_DEF_MAX_RECV_SEG_LEN,
				   &bnx2i_conn->gen_pdu.req_dma_addr,
				   GFP_KERNEL);
	if (bnx2i_conn->gen_pdu.req_buf == NULL)
		goto login_req_buf_failure;

	bnx2i_conn->gen_pdu.req_buf_size = 0;
	bnx2i_conn->gen_pdu.req_wr_ptr = bnx2i_conn->gen_pdu.req_buf;

	bnx2i_conn->gen_pdu.resp_buf =
		dma_alloc_coherent(&hba->pcidev->dev,
				   ISCSI_DEF_MAX_RECV_SEG_LEN,
				   &bnx2i_conn->gen_pdu.resp_dma_addr,
				   GFP_KERNEL);
	if (bnx2i_conn->gen_pdu.resp_buf == NULL)
		goto login_resp_buf_failure;

	bnx2i_conn->gen_pdu.resp_buf_size = ISCSI_DEF_MAX_RECV_SEG_LEN;
	bnx2i_conn->gen_pdu.resp_wr_ptr = bnx2i_conn->gen_pdu.resp_buf;

	bnx2i_conn->gen_pdu.req_bd_tbl =
		dma_alloc_coherent(&hba->pcidev->dev, PAGE_SIZE,
				   &bnx2i_conn->gen_pdu.req_bd_dma, GFP_KERNEL);
	if (bnx2i_conn->gen_pdu.req_bd_tbl == NULL)
		goto login_req_bd_tbl_failure;

	bnx2i_conn->gen_pdu.resp_bd_tbl =
		dma_alloc_coherent(&hba->pcidev->dev, PAGE_SIZE,
				   &bnx2i_conn->gen_pdu.resp_bd_dma,
				   GFP_KERNEL);
	if (bnx2i_conn->gen_pdu.resp_bd_tbl == NULL)
		goto login_resp_bd_tbl_failure;

	return 0;

login_resp_bd_tbl_failure:
	dma_free_coherent(&hba->pcidev->dev, PAGE_SIZE,
			  bnx2i_conn->gen_pdu.req_bd_tbl,
			  bnx2i_conn->gen_pdu.req_bd_dma);
	bnx2i_conn->gen_pdu.req_bd_tbl = NULL;

login_req_bd_tbl_failure:
	dma_free_coherent(&hba->pcidev->dev, ISCSI_DEF_MAX_RECV_SEG_LEN,
			  bnx2i_conn->gen_pdu.resp_buf,
			  bnx2i_conn->gen_pdu.resp_dma_addr);
	bnx2i_conn->gen_pdu.resp_buf = NULL;
login_resp_buf_failure:
	dma_free_coherent(&hba->pcidev->dev, ISCSI_DEF_MAX_RECV_SEG_LEN,
			  bnx2i_conn->gen_pdu.req_buf,
			  bnx2i_conn->gen_pdu.req_dma_addr);
	bnx2i_conn->gen_pdu.req_buf = NULL;
login_req_buf_failure:
	iscsi_conn_printk(KERN_ERR, bnx2i_conn->cls_conn->dd_data,
			  "login resource alloc failed!!\n");
	return -ENOMEM;

}


/**
 * bnx2i_iscsi_prep_generic_pdu_bd - prepares BD table.
 * @bnx2i_conn:		iscsi connection pointer
 *
 * Allocates buffers and BD tables before shipping requests to cnic
 *	for PDUs prepared by 'iscsid' daemon
 */
static void bnx2i_iscsi_prep_generic_pdu_bd(struct bnx2i_conn *bnx2i_conn)
{
	struct iscsi_bd *bd_tbl;

	bd_tbl = (struct iscsi_bd *) bnx2i_conn->gen_pdu.req_bd_tbl;

	bd_tbl->buffer_addr_hi =
		(u32) ((u64) bnx2i_conn->gen_pdu.req_dma_addr >> 32);
	bd_tbl->buffer_addr_lo = (u32) bnx2i_conn->gen_pdu.req_dma_addr;
	bd_tbl->buffer_length = bnx2i_conn->gen_pdu.req_wr_ptr -
				bnx2i_conn->gen_pdu.req_buf;
	bd_tbl->reserved0 = 0;
	bd_tbl->flags = ISCSI_BD_LAST_IN_BD_CHAIN |
			ISCSI_BD_FIRST_IN_BD_CHAIN;

	bd_tbl = (struct iscsi_bd  *) bnx2i_conn->gen_pdu.resp_bd_tbl;
	bd_tbl->buffer_addr_hi = (u64) bnx2i_conn->gen_pdu.resp_dma_addr >> 32;
	bd_tbl->buffer_addr_lo = (u32) bnx2i_conn->gen_pdu.resp_dma_addr;
	bd_tbl->buffer_length = ISCSI_DEF_MAX_RECV_SEG_LEN;
	bd_tbl->reserved0 = 0;
	bd_tbl->flags = ISCSI_BD_LAST_IN_BD_CHAIN |
			ISCSI_BD_FIRST_IN_BD_CHAIN;
}


/**
 * bnx2i_iscsi_send_generic_request - called to send mgmt tasks.
 * @task:	transport layer task pointer
 *
 * called to transmit PDUs prepared by the 'iscsid' daemon. iSCSI login,
 *	Nop-out and Logout requests flow through this path.
 */
static int bnx2i_iscsi_send_generic_request(struct iscsi_task *task)
{
	struct bnx2i_cmd *cmd = task->dd_data;
	struct bnx2i_conn *bnx2i_conn = cmd->conn;
	int rc = 0;
	char *buf;
	int data_len;

	bnx2i_iscsi_prep_generic_pdu_bd(bnx2i_conn);
	switch (task->hdr->opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_LOGIN:
		bnx2i_send_iscsi_login(bnx2i_conn, task);
		break;
	case ISCSI_OP_NOOP_OUT:
		data_len = bnx2i_conn->gen_pdu.req_buf_size;
		buf = bnx2i_conn->gen_pdu.req_buf;
		if (data_len)
			rc = bnx2i_send_iscsi_nopout(bnx2i_conn, task,
						     buf, data_len, 1);
		else
			rc = bnx2i_send_iscsi_nopout(bnx2i_conn, task,
						     NULL, 0, 1);
		break;
	case ISCSI_OP_LOGOUT:
		rc = bnx2i_send_iscsi_logout(bnx2i_conn, task);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		rc = bnx2i_send_iscsi_tmf(bnx2i_conn, task);
		break;
	case ISCSI_OP_TEXT:
		rc = bnx2i_send_iscsi_text(bnx2i_conn, task);
		break;
	default:
		iscsi_conn_printk(KERN_ALERT, bnx2i_conn->cls_conn->dd_data,
				  "send_gen: unsupported op 0x%x\n",
				  task->hdr->opcode);
	}
	return rc;
}


/**********************************************************************
 *		SCSI-ML Interface
 **********************************************************************/

/**
 * bnx2i_cpy_scsi_cdb - copies LUN & CDB fields in required format to sq wqe
 * @sc:		SCSI-ML command pointer
 * @cmd:	iscsi cmd pointer
 */
static void bnx2i_cpy_scsi_cdb(struct scsi_cmnd *sc, struct bnx2i_cmd *cmd)
{
	u32 dword;
	int lpcnt;
	u8 *srcp;
	u32 *dstp;
	u32 scsi_lun[2];

	int_to_scsilun(sc->device->lun, (struct scsi_lun *) scsi_lun);
	cmd->req.lun[0] = be32_to_cpu(scsi_lun[0]);
	cmd->req.lun[1] = be32_to_cpu(scsi_lun[1]);

	lpcnt = cmd->scsi_cmd->cmd_len / sizeof(dword);
	srcp = (u8 *) sc->cmnd;
	dstp = (u32 *) cmd->req.cdb;
	while (lpcnt--) {
		memcpy(&dword, (const void *) srcp, 4);
		*dstp = cpu_to_be32(dword);
		srcp += 4;
		dstp++;
	}
	if (sc->cmd_len & 0x3) {
		dword = (u32) srcp[0] | ((u32) srcp[1] << 8);
		*dstp = cpu_to_be32(dword);
	}
}

static void bnx2i_cleanup_task(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;
	struct bnx2i_hba *hba = bnx2i_conn->hba;

	/*
	 * mgmt task or cmd was never sent to us to transmit.
	 */
	if (!task->sc || task->state == ISCSI_TASK_PENDING)
		return;
	/*
	 * need to clean-up task context to claim dma buffers
	 */
	if (task->state == ISCSI_TASK_ABRT_TMF) {
		bnx2i_send_cmd_cleanup_req(hba, task->dd_data);

		spin_unlock_bh(&conn->session->lock);
		wait_for_completion_timeout(&bnx2i_conn->cmd_cleanup_cmpl,
				msecs_to_jiffies(ISCSI_CMD_CLEANUP_TIMEOUT));
		spin_lock_bh(&conn->session->lock);
	}
	bnx2i_iscsi_unmap_sg_list(task->dd_data);
}

/**
 * bnx2i_mtask_xmit - transmit mtask to chip for further processing
 * @conn:	transport layer conn structure pointer
 * @task:	transport layer command structure pointer
 */
static int
bnx2i_mtask_xmit(struct iscsi_conn *conn, struct iscsi_task *task)
{
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;
	struct bnx2i_hba *hba = bnx2i_conn->hba;
	struct bnx2i_cmd *cmd = task->dd_data;

	memset(bnx2i_conn->gen_pdu.req_buf, 0, ISCSI_DEF_MAX_RECV_SEG_LEN);

	bnx2i_setup_cmd_wqe_template(cmd);
	bnx2i_conn->gen_pdu.req_buf_size = task->data_count;

	/* Tx PDU/data length count */
	ADD_STATS_64(hba, tx_pdus, 1);
	ADD_STATS_64(hba, tx_bytes, task->data_count);

	if (task->data_count) {
		memcpy(bnx2i_conn->gen_pdu.req_buf, task->data,
		       task->data_count);
		bnx2i_conn->gen_pdu.req_wr_ptr =
			bnx2i_conn->gen_pdu.req_buf + task->data_count;
	}
	cmd->conn = conn->dd_data;
	cmd->scsi_cmd = NULL;
	return bnx2i_iscsi_send_generic_request(task);
}

/**
 * bnx2i_task_xmit - transmit iscsi command to chip for further processing
 * @task:	transport layer command structure pointer
 *
 * maps SG buffers and send request to chip/firmware in the form of SQ WQE
 */
static int bnx2i_task_xmit(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = conn->session;
	struct Scsi_Host *shost = iscsi_session_to_shost(session->cls_session);
	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;
	struct scsi_cmnd *sc = task->sc;
	struct bnx2i_cmd *cmd = task->dd_data;
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)task->hdr;

	if (atomic_read(&bnx2i_conn->ep->num_active_cmds) + 1  >
	    hba->max_sqes)
		return -ENOMEM;

	/*
	 * If there is no scsi_cmnd this must be a mgmt task
	 */
	if (!sc)
		return bnx2i_mtask_xmit(conn, task);

	bnx2i_setup_cmd_wqe_template(cmd);
	cmd->req.op_code = ISCSI_OP_SCSI_CMD;
	cmd->conn = bnx2i_conn;
	cmd->scsi_cmd = sc;
	cmd->req.total_data_transfer_length = scsi_bufflen(sc);
	cmd->req.cmd_sn = be32_to_cpu(hdr->cmdsn);

	bnx2i_iscsi_map_sg_list(cmd);
	bnx2i_cpy_scsi_cdb(sc, cmd);

	cmd->req.op_attr = ISCSI_ATTR_SIMPLE;
	if (sc->sc_data_direction == DMA_TO_DEVICE) {
		cmd->req.op_attr |= ISCSI_CMD_REQUEST_WRITE;
		cmd->req.itt = task->itt |
			(ISCSI_TASK_TYPE_WRITE << ISCSI_CMD_REQUEST_TYPE_SHIFT);
		bnx2i_setup_write_cmd_bd_info(task);
	} else {
		if (scsi_bufflen(sc))
			cmd->req.op_attr |= ISCSI_CMD_REQUEST_READ;
		cmd->req.itt = task->itt |
			(ISCSI_TASK_TYPE_READ << ISCSI_CMD_REQUEST_TYPE_SHIFT);
	}

	cmd->req.num_bds = cmd->io_tbl.bd_valid;
	if (!cmd->io_tbl.bd_valid) {
		cmd->req.bd_list_addr_lo = (u32) hba->mp_bd_dma;
		cmd->req.bd_list_addr_hi = (u32) ((u64) hba->mp_bd_dma >> 32);
		cmd->req.num_bds = 1;
	}

	bnx2i_send_iscsi_scsicmd(bnx2i_conn, cmd);
	return 0;
}

/**
 * bnx2i_session_create - create a new iscsi session
 * @cmds_max:		max commands supported
 * @qdepth:		scsi queue depth to support
 * @initial_cmdsn:	initial iscsi CMDSN to be used for this session
 *
 * Creates a new iSCSI session instance on given device.
 */
static struct iscsi_cls_session *
bnx2i_session_create(struct iscsi_endpoint *ep,
		     uint16_t cmds_max, uint16_t qdepth,
		     uint32_t initial_cmdsn)
{
	struct Scsi_Host *shost;
	struct iscsi_cls_session *cls_session;
	struct bnx2i_hba *hba;
	struct bnx2i_endpoint *bnx2i_ep;

	if (!ep) {
		printk(KERN_ERR "bnx2i: missing ep.\n");
		return NULL;
	}

	bnx2i_ep = ep->dd_data;
	shost = bnx2i_ep->hba->shost;
	hba = iscsi_host_priv(shost);
	if (bnx2i_adapter_ready(hba))
		return NULL;

	/*
	 * user can override hw limit as long as it is within
	 * the min/max.
	 */
	if (cmds_max > hba->max_sqes)
		cmds_max = hba->max_sqes;
	else if (cmds_max < BNX2I_SQ_WQES_MIN)
		cmds_max = BNX2I_SQ_WQES_MIN;

	cls_session = iscsi_session_setup(&bnx2i_iscsi_transport, shost,
					  cmds_max, 0, sizeof(struct bnx2i_cmd),
					  initial_cmdsn, ISCSI_MAX_TARGET);
	if (!cls_session)
		return NULL;

	if (bnx2i_setup_cmd_pool(hba, cls_session->dd_data))
		goto session_teardown;
	return cls_session;

session_teardown:
	iscsi_session_teardown(cls_session);
	return NULL;
}


/**
 * bnx2i_session_destroy - destroys iscsi session
 * @cls_session:	pointer to iscsi cls session
 *
 * Destroys previously created iSCSI session instance and releases
 *	all resources held by it
 */
static void bnx2i_session_destroy(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *session = cls_session->dd_data;
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct bnx2i_hba *hba = iscsi_host_priv(shost);

	bnx2i_destroy_cmd_pool(hba, session);
	iscsi_session_teardown(cls_session);
}


/**
 * bnx2i_conn_create - create iscsi connection instance
 * @cls_session:	pointer to iscsi cls session
 * @cid:		iscsi cid as per rfc (not NX2's CID terminology)
 *
 * Creates a new iSCSI connection instance for a given session
 */
static struct iscsi_cls_conn *
bnx2i_conn_create(struct iscsi_cls_session *cls_session, uint32_t cid)
{
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	struct bnx2i_conn *bnx2i_conn;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_conn *conn;

	cls_conn = iscsi_conn_setup(cls_session, sizeof(*bnx2i_conn),
				    cid);
	if (!cls_conn)
		return NULL;
	conn = cls_conn->dd_data;

	bnx2i_conn = conn->dd_data;
	bnx2i_conn->cls_conn = cls_conn;
	bnx2i_conn->hba = hba;

	atomic_set(&bnx2i_conn->work_cnt, 0);

	/* 'ep' ptr will be assigned in bind() call */
	bnx2i_conn->ep = NULL;
	init_completion(&bnx2i_conn->cmd_cleanup_cmpl);

	if (bnx2i_conn_alloc_login_resources(hba, bnx2i_conn)) {
		iscsi_conn_printk(KERN_ALERT, conn,
				  "conn_new: login resc alloc failed!!\n");
		goto free_conn;
	}

	return cls_conn;

free_conn:
	iscsi_conn_teardown(cls_conn);
	return NULL;
}

/**
 * bnx2i_conn_bind - binds iscsi sess, conn and ep objects together
 * @cls_session:	pointer to iscsi cls session
 * @cls_conn:		pointer to iscsi cls conn
 * @transport_fd:	64-bit EP handle
 * @is_leading:		leading connection on this session?
 *
 * Binds together iSCSI session instance, iSCSI connection instance
 *	and the TCP connection. This routine returns error code if
 *	TCP connection does not belong on the device iSCSI sess/conn
 *	is bound
 */
static int bnx2i_conn_bind(struct iscsi_cls_session *cls_session,
			   struct iscsi_cls_conn *cls_conn,
			   uint64_t transport_fd, int is_leading)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;
	struct Scsi_Host *shost = iscsi_session_to_shost(cls_session);
	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	struct bnx2i_endpoint *bnx2i_ep;
	struct iscsi_endpoint *ep;
	int ret_code;

	ep = iscsi_lookup_endpoint(transport_fd);
	if (!ep)
		return -EINVAL;
	/*
	 * Forcefully terminate all in progress connection recovery at the
	 * earliest, either in bind(), send_pdu(LOGIN), or conn_start()
	 */
	if (bnx2i_adapter_ready(hba))
		return -EIO;

	bnx2i_ep = ep->dd_data;
	if ((bnx2i_ep->state == EP_STATE_TCP_FIN_RCVD) ||
	    (bnx2i_ep->state == EP_STATE_TCP_RST_RCVD))
		/* Peer disconnect via' FIN or RST */
		return -EINVAL;

	if (iscsi_conn_bind(cls_session, cls_conn, is_leading))
		return -EINVAL;

	if (bnx2i_ep->hba != hba) {
		/* Error - TCP connection does not belong to this device
		 */
		iscsi_conn_printk(KERN_ALERT, cls_conn->dd_data,
				  "conn bind, ep=0x%p (%s) does not",
				  bnx2i_ep, bnx2i_ep->hba->netdev->name);
		iscsi_conn_printk(KERN_ALERT, cls_conn->dd_data,
				  "belong to hba (%s)\n",
				  hba->netdev->name);
		return -EEXIST;
	}
	bnx2i_ep->conn = bnx2i_conn;
	bnx2i_conn->ep = bnx2i_ep;
	bnx2i_conn->iscsi_conn_cid = bnx2i_ep->ep_iscsi_cid;
	bnx2i_conn->fw_cid = bnx2i_ep->ep_cid;

	ret_code = bnx2i_bind_conn_to_iscsi_cid(hba, bnx2i_conn,
						bnx2i_ep->ep_iscsi_cid);

	/* 5706/5708/5709 FW takes RQ as full when initiated, but for 57710
	 * driver needs to explicitly replenish RQ index during setup.
	 */
	if (test_bit(BNX2I_NX2_DEV_57710, &bnx2i_ep->hba->cnic_dev_type))
		bnx2i_put_rq_buf(bnx2i_conn, 0);

	bnx2i_arm_cq_event_coalescing(bnx2i_conn->ep, CNIC_ARM_CQE);
	return ret_code;
}


/**
 * bnx2i_conn_destroy - destroy iscsi connection instance & release resources
 * @cls_conn:	pointer to iscsi cls conn
 *
 * Destroy an iSCSI connection instance and release memory resources held by
 *	this connection
 */
static void bnx2i_conn_destroy(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;
	struct Scsi_Host *shost;
	struct bnx2i_hba *hba;
	struct bnx2i_work *work, *tmp;
	unsigned cpu = 0;
	struct bnx2i_percpu_s *p;

	shost = iscsi_session_to_shost(iscsi_conn_to_session(cls_conn));
	hba = iscsi_host_priv(shost);

	bnx2i_conn_free_login_resources(hba, bnx2i_conn);

	if (atomic_read(&bnx2i_conn->work_cnt)) {
		for_each_online_cpu(cpu) {
			p = &per_cpu(bnx2i_percpu, cpu);
			spin_lock_bh(&p->p_work_lock);
			list_for_each_entry_safe(work, tmp,
						 &p->work_list, list) {
				if (work->session == conn->session &&
				    work->bnx2i_conn == bnx2i_conn) {
					list_del_init(&work->list);
					kfree(work);
					if (!atomic_dec_and_test(
							&bnx2i_conn->work_cnt))
						break;
				}
			}
			spin_unlock_bh(&p->p_work_lock);
		}
	}

	iscsi_conn_teardown(cls_conn);
}


/**
 * bnx2i_ep_get_param - return iscsi ep parameter to caller
 * @ep:		pointer to iscsi endpoint
 * @param:	parameter type identifier
 * @buf: 	buffer pointer
 *
 * returns iSCSI ep parameters
 */
static int bnx2i_ep_get_param(struct iscsi_endpoint *ep,
			      enum iscsi_param param, char *buf)
{
	struct bnx2i_endpoint *bnx2i_ep = ep->dd_data;
	struct bnx2i_hba *hba = bnx2i_ep->hba;
	int len = -ENOTCONN;

	if (!hba)
		return -ENOTCONN;

	switch (param) {
	case ISCSI_PARAM_CONN_PORT:
		mutex_lock(&hba->net_dev_lock);
		if (bnx2i_ep->cm_sk)
			len = sprintf(buf, "%hu\n", bnx2i_ep->cm_sk->dst_port);
		mutex_unlock(&hba->net_dev_lock);
		break;
	case ISCSI_PARAM_CONN_ADDRESS:
		mutex_lock(&hba->net_dev_lock);
		if (bnx2i_ep->cm_sk)
			len = sprintf(buf, "%pI4\n", &bnx2i_ep->cm_sk->dst_ip);
		mutex_unlock(&hba->net_dev_lock);
		break;
	default:
		return -ENOSYS;
	}

	return len;
}

/**
 * bnx2i_host_get_param - returns host (adapter) related parameters
 * @shost:	scsi host pointer
 * @param:	parameter type identifier
 * @buf:	buffer pointer
 */
static int bnx2i_host_get_param(struct Scsi_Host *shost,
				enum iscsi_host_param param, char *buf)
{
	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	int len = 0;

	switch (param) {
	case ISCSI_HOST_PARAM_HWADDRESS:
		len = sysfs_format_mac(buf, hba->cnic->mac_addr, 6);
		break;
	case ISCSI_HOST_PARAM_NETDEV_NAME:
		len = sprintf(buf, "%s\n", hba->netdev->name);
		break;
	case ISCSI_HOST_PARAM_IPADDRESS: {
		struct list_head *active_list = &hba->ep_active_list;

		read_lock_bh(&hba->ep_rdwr_lock);
		if (!list_empty(&hba->ep_active_list)) {
			struct bnx2i_endpoint *bnx2i_ep;
			struct cnic_sock *csk;

			bnx2i_ep = list_first_entry(active_list,
						    struct bnx2i_endpoint,
						    link);
			csk = bnx2i_ep->cm_sk;
			if (test_bit(SK_F_IPV6, &csk->flags))
				len = sprintf(buf, "%pI6\n", csk->src_ip);
			else
				len = sprintf(buf, "%pI4\n", csk->src_ip);
		}
		read_unlock_bh(&hba->ep_rdwr_lock);
		break;
	}
	default:
		return iscsi_host_get_param(shost, param, buf);
	}
	return len;
}

/**
 * bnx2i_conn_start - completes iscsi connection migration to FFP
 * @cls_conn:	pointer to iscsi cls conn
 *
 * last call in FFP migration to handover iscsi conn to the driver
 */
static int bnx2i_conn_start(struct iscsi_cls_conn *cls_conn)
{
	struct iscsi_conn *conn = cls_conn->dd_data;
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;

	bnx2i_conn->ep->state = EP_STATE_ULP_UPDATE_START;
	bnx2i_update_iscsi_conn(conn);

	/*
	 * this should normally not sleep for a long time so it should
	 * not disrupt the caller.
	 */
	bnx2i_conn->ep->ofld_timer.expires = 1 * HZ + jiffies;
	bnx2i_conn->ep->ofld_timer.function = bnx2i_ep_ofld_timer;
	bnx2i_conn->ep->ofld_timer.data = (unsigned long) bnx2i_conn->ep;
	add_timer(&bnx2i_conn->ep->ofld_timer);
	/* update iSCSI context for this conn, wait for CNIC to complete */
	wait_event_interruptible(bnx2i_conn->ep->ofld_wait,
			bnx2i_conn->ep->state != EP_STATE_ULP_UPDATE_START);

	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&bnx2i_conn->ep->ofld_timer);

	iscsi_conn_start(cls_conn);
	return 0;
}


/**
 * bnx2i_conn_get_stats - returns iSCSI stats
 * @cls_conn:	pointer to iscsi cls conn
 * @stats:	pointer to iscsi statistic struct
 */
static void bnx2i_conn_get_stats(struct iscsi_cls_conn *cls_conn,
				 struct iscsi_stats *stats)
{
	struct iscsi_conn *conn = cls_conn->dd_data;

	stats->txdata_octets = conn->txdata_octets;
	stats->rxdata_octets = conn->rxdata_octets;
	stats->scsicmd_pdus = conn->scsicmd_pdus_cnt;
	stats->dataout_pdus = conn->dataout_pdus_cnt;
	stats->scsirsp_pdus = conn->scsirsp_pdus_cnt;
	stats->datain_pdus = conn->datain_pdus_cnt;
	stats->r2t_pdus = conn->r2t_pdus_cnt;
	stats->tmfcmd_pdus = conn->tmfcmd_pdus_cnt;
	stats->tmfrsp_pdus = conn->tmfrsp_pdus_cnt;
	stats->custom_length = 3;
	strcpy(stats->custom[2].desc, "eh_abort_cnt");
	stats->custom[2].value = conn->eh_abort_cnt;
	stats->digest_err = 0;
	stats->timeout_err = 0;
	stats->custom_length = 0;
}


/**
 * bnx2i_check_route - checks if target IP route belongs to one of NX2 devices
 * @dst_addr:	target IP address
 *
 * check if route resolves to BNX2 device
 */
static struct bnx2i_hba *bnx2i_check_route(struct sockaddr *dst_addr)
{
	struct sockaddr_in *desti = (struct sockaddr_in *) dst_addr;
	struct bnx2i_hba *hba;
	struct cnic_dev *cnic = NULL;

	hba = get_adapter_list_head();
	if (hba && hba->cnic)
		cnic = hba->cnic->cm_select_dev(desti, CNIC_ULP_ISCSI);
	if (!cnic) {
		printk(KERN_ALERT "bnx2i: no route,"
		       "can't connect using cnic\n");
		goto no_nx2_route;
	}
	hba = bnx2i_find_hba_for_cnic(cnic);
	if (!hba)
		goto no_nx2_route;

	if (bnx2i_adapter_ready(hba)) {
		printk(KERN_ALERT "bnx2i: check route, hba not found\n");
		goto no_nx2_route;
	}
	if (hba->netdev->mtu > hba->mtu_supported) {
		printk(KERN_ALERT "bnx2i: %s network i/f mtu is set to %d\n",
				  hba->netdev->name, hba->netdev->mtu);
		printk(KERN_ALERT "bnx2i: iSCSI HBA can support mtu of %d\n",
				  hba->mtu_supported);
		goto no_nx2_route;
	}
	return hba;
no_nx2_route:
	return NULL;
}


/**
 * bnx2i_tear_down_conn - tear down iscsi/tcp connection and free resources
 * @hba:	pointer to adapter instance
 * @ep:		endpoint (transport indentifier) structure
 *
 * destroys cm_sock structure and on chip iscsi context
 */
static int bnx2i_tear_down_conn(struct bnx2i_hba *hba,
				 struct bnx2i_endpoint *ep)
{
	if (test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic) && ep->cm_sk)
		hba->cnic->cm_destroy(ep->cm_sk);

	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type) &&
	    ep->state == EP_STATE_DISCONN_TIMEDOUT) {
		if (ep->conn && ep->conn->cls_conn &&
		    ep->conn->cls_conn->dd_data) {
			struct iscsi_conn *conn = ep->conn->cls_conn->dd_data;

			/* Must suspend all rx queue activity for this ep */
			set_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx);
		}
		/* CONN_DISCONNECT timeout may or may not be an issue depending
		 * on what transcribed in TCP layer, different targets behave
		 * differently
		 */
		printk(KERN_ALERT "bnx2i (%s): - WARN - CONN_DISCON timed out, "
				  "please submit GRC Dump, NW/PCIe trace, "
				  "driver msgs to developers for analysis\n",
				  hba->netdev->name);
	}

	ep->state = EP_STATE_CLEANUP_START;
	init_timer(&ep->ofld_timer);
	ep->ofld_timer.expires = hba->conn_ctx_destroy_tmo + jiffies;
	ep->ofld_timer.function = bnx2i_ep_ofld_timer;
	ep->ofld_timer.data = (unsigned long) ep;
	add_timer(&ep->ofld_timer);

	bnx2i_ep_destroy_list_add(hba, ep);

	/* destroy iSCSI context, wait for it to complete */
	if (bnx2i_send_conn_destroy(hba, ep))
		ep->state = EP_STATE_CLEANUP_CMPL;

	wait_event_interruptible(ep->ofld_wait,
				 (ep->state != EP_STATE_CLEANUP_START));

	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&ep->ofld_timer);

	bnx2i_ep_destroy_list_del(hba, ep);

	if (ep->state != EP_STATE_CLEANUP_CMPL)
		/* should never happen */
		printk(KERN_ALERT "bnx2i - conn destroy failed\n");

	return 0;
}


/**
 * bnx2i_ep_connect - establish TCP connection to target portal
 * @shost:		scsi host
 * @dst_addr:		target IP address
 * @non_blocking:	blocking or non-blocking call
 *
 * this routine initiates the TCP/IP connection by invoking Option-2 i/f
 *	with l5_core and the CNIC. This is a multi-step process of resolving
 *	route to target, create a iscsi connection context, handshaking with
 *	CNIC module to create/initialize the socket struct and finally
 *	sending down option-2 request to complete TCP 3-way handshake
 */
static struct iscsi_endpoint *bnx2i_ep_connect(struct Scsi_Host *shost,
					       struct sockaddr *dst_addr,
					       int non_blocking)
{
	u32 iscsi_cid = BNX2I_CID_RESERVED;
	struct sockaddr_in *desti = (struct sockaddr_in *) dst_addr;
	struct sockaddr_in6 *desti6;
	struct bnx2i_endpoint *bnx2i_ep;
	struct bnx2i_hba *hba;
	struct cnic_dev *cnic;
	struct cnic_sockaddr saddr;
	struct iscsi_endpoint *ep;
	int rc = 0;

	if (shost) {
		/* driver is given scsi host to work with */
		hba = iscsi_host_priv(shost);
	} else
		/*
		 * check if the given destination can be reached through
		 * a iscsi capable NetXtreme2 device
		 */
		hba = bnx2i_check_route(dst_addr);

	if (!hba) {
		rc = -EINVAL;
		goto nohba;
	}
	mutex_lock(&hba->net_dev_lock);

	if (bnx2i_adapter_ready(hba) || !hba->cid_que.cid_free_cnt) {
		rc = -EPERM;
		goto check_busy;
	}
	cnic = hba->cnic;
	ep = bnx2i_alloc_ep(hba);
	if (!ep) {
		rc = -ENOMEM;
		goto check_busy;
	}
	bnx2i_ep = ep->dd_data;

	atomic_set(&bnx2i_ep->num_active_cmds, 0);
	iscsi_cid = bnx2i_alloc_iscsi_cid(hba);
	if (iscsi_cid == -1) {
		printk(KERN_ALERT "bnx2i (%s): alloc_ep - unable to allocate "
			"iscsi cid\n", hba->netdev->name);
		rc = -ENOMEM;
		bnx2i_free_ep(ep);
		goto check_busy;
	}
	bnx2i_ep->hba_age = hba->age;

	rc = bnx2i_alloc_qp_resc(hba, bnx2i_ep);
	if (rc != 0) {
		printk(KERN_ALERT "bnx2i (%s): ep_conn - alloc QP resc error"
			"\n", hba->netdev->name);
		rc = -ENOMEM;
		goto qp_resc_err;
	}

	bnx2i_ep->ep_iscsi_cid = (u16)iscsi_cid;
	bnx2i_ep->state = EP_STATE_OFLD_START;
	bnx2i_ep_ofld_list_add(hba, bnx2i_ep);

	init_timer(&bnx2i_ep->ofld_timer);
	bnx2i_ep->ofld_timer.expires = 2 * HZ + jiffies;
	bnx2i_ep->ofld_timer.function = bnx2i_ep_ofld_timer;
	bnx2i_ep->ofld_timer.data = (unsigned long) bnx2i_ep;
	add_timer(&bnx2i_ep->ofld_timer);

	if (bnx2i_send_conn_ofld_req(hba, bnx2i_ep)) {
		if (bnx2i_ep->state == EP_STATE_OFLD_FAILED_CID_BUSY) {
			printk(KERN_ALERT "bnx2i (%s): iscsi cid %d is busy\n",
				hba->netdev->name, bnx2i_ep->ep_iscsi_cid);
			rc = -EBUSY;
		} else
			rc = -ENOSPC;
		printk(KERN_ALERT "bnx2i (%s): unable to send conn offld kwqe"
			"\n", hba->netdev->name);
		bnx2i_ep_ofld_list_del(hba, bnx2i_ep);
		goto conn_failed;
	}

	/* Wait for CNIC hardware to setup conn context and return 'cid' */
	wait_event_interruptible(bnx2i_ep->ofld_wait,
				 bnx2i_ep->state != EP_STATE_OFLD_START);

	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&bnx2i_ep->ofld_timer);

	bnx2i_ep_ofld_list_del(hba, bnx2i_ep);

	if (bnx2i_ep->state != EP_STATE_OFLD_COMPL) {
		if (bnx2i_ep->state == EP_STATE_OFLD_FAILED_CID_BUSY) {
			printk(KERN_ALERT "bnx2i (%s): iscsi cid %d is busy\n",
				hba->netdev->name, bnx2i_ep->ep_iscsi_cid);
			rc = -EBUSY;
		} else
			rc = -ENOSPC;
		goto conn_failed;
	}

	rc = cnic->cm_create(cnic, CNIC_ULP_ISCSI, bnx2i_ep->ep_cid,
			     iscsi_cid, &bnx2i_ep->cm_sk, bnx2i_ep);
	if (rc) {
		rc = -EINVAL;
		/* Need to terminate and cleanup the connection */
		goto release_ep;
	}

	bnx2i_ep->cm_sk->rcv_buf = 256 * 1024;
	bnx2i_ep->cm_sk->snd_buf = 256 * 1024;
	clear_bit(SK_TCP_TIMESTAMP, &bnx2i_ep->cm_sk->tcp_flags);

	memset(&saddr, 0, sizeof(saddr));
	if (dst_addr->sa_family == AF_INET) {
		desti = (struct sockaddr_in *) dst_addr;
		saddr.remote.v4 = *desti;
		saddr.local.v4.sin_family = desti->sin_family;
	} else if (dst_addr->sa_family == AF_INET6) {
		desti6 = (struct sockaddr_in6 *) dst_addr;
		saddr.remote.v6 = *desti6;
		saddr.local.v6.sin6_family = desti6->sin6_family;
	}

	bnx2i_ep->timestamp = jiffies;
	bnx2i_ep->state = EP_STATE_CONNECT_START;
	if (!test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic)) {
		rc = -EINVAL;
		goto conn_failed;
	} else
		rc = cnic->cm_connect(bnx2i_ep->cm_sk, &saddr);
	if (rc)
		goto release_ep;

	bnx2i_ep_active_list_add(hba, bnx2i_ep);

	if (bnx2i_map_ep_dbell_regs(bnx2i_ep))
		goto del_active_ep;

	mutex_unlock(&hba->net_dev_lock);
	return ep;

del_active_ep:
	bnx2i_ep_active_list_del(hba, bnx2i_ep);
release_ep:
	if (bnx2i_tear_down_conn(hba, bnx2i_ep)) {
		mutex_unlock(&hba->net_dev_lock);
		return ERR_PTR(rc);
	}
conn_failed:
	bnx2i_free_qp_resc(hba, bnx2i_ep);
qp_resc_err:
	bnx2i_free_ep(ep);
check_busy:
	mutex_unlock(&hba->net_dev_lock);
nohba:
	return ERR_PTR(rc);
}


/**
 * bnx2i_ep_poll - polls for TCP connection establishement
 * @ep:			TCP connection (endpoint) handle
 * @timeout_ms:		timeout value in milli secs
 *
 * polls for TCP connect request to complete
 */
static int bnx2i_ep_poll(struct iscsi_endpoint *ep, int timeout_ms)
{
	struct bnx2i_endpoint *bnx2i_ep;
	int rc = 0;

	bnx2i_ep = ep->dd_data;
	if ((bnx2i_ep->state == EP_STATE_IDLE) ||
	    (bnx2i_ep->state == EP_STATE_CONNECT_FAILED) ||
	    (bnx2i_ep->state == EP_STATE_OFLD_FAILED))
		return -1;
	if (bnx2i_ep->state == EP_STATE_CONNECT_COMPL)
		return 1;

	rc = wait_event_interruptible_timeout(bnx2i_ep->ofld_wait,
					      ((bnx2i_ep->state ==
						EP_STATE_OFLD_FAILED) ||
					       (bnx2i_ep->state ==
						EP_STATE_CONNECT_FAILED) ||
					       (bnx2i_ep->state ==
						EP_STATE_CONNECT_COMPL)),
					      msecs_to_jiffies(timeout_ms));
	if (bnx2i_ep->state == EP_STATE_OFLD_FAILED)
		rc = -1;

	if (rc > 0)
		return 1;
	else if (!rc)
		return 0;	/* timeout */
	else
		return rc;
}


/**
 * bnx2i_ep_tcp_conn_active - check EP state transition
 * @ep:		endpoint pointer
 *
 * check if underlying TCP connection is active
 */
static int bnx2i_ep_tcp_conn_active(struct bnx2i_endpoint *bnx2i_ep)
{
	int ret;
	int cnic_dev_10g = 0;

	if (test_bit(BNX2I_NX2_DEV_57710, &bnx2i_ep->hba->cnic_dev_type))
		cnic_dev_10g = 1;

	switch (bnx2i_ep->state) {
	case EP_STATE_CLEANUP_FAILED:
	case EP_STATE_OFLD_FAILED:
	case EP_STATE_DISCONN_TIMEDOUT:
		ret = 0;
		break;
	case EP_STATE_CONNECT_START:
	case EP_STATE_CONNECT_FAILED:
	case EP_STATE_CONNECT_COMPL:
	case EP_STATE_ULP_UPDATE_START:
	case EP_STATE_ULP_UPDATE_COMPL:
	case EP_STATE_TCP_FIN_RCVD:
	case EP_STATE_LOGOUT_SENT:
	case EP_STATE_LOGOUT_RESP_RCVD:
	case EP_STATE_ULP_UPDATE_FAILED:
		ret = 1;
		break;
	case EP_STATE_TCP_RST_RCVD:
		if (cnic_dev_10g)
			ret = 0;
		else
			ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}


/*
 * bnx2i_hw_ep_disconnect - executes TCP connection teardown process in the hw
 * @ep:		TCP connection (bnx2i endpoint) handle
 *
 * executes  TCP connection teardown process
 */
int bnx2i_hw_ep_disconnect(struct bnx2i_endpoint *bnx2i_ep)
{
	struct bnx2i_hba *hba = bnx2i_ep->hba;
	struct cnic_dev *cnic;
	struct iscsi_session *session = NULL;
	struct iscsi_conn *conn = NULL;
	int ret = 0;
	int close = 0;
	int close_ret = 0;

	if (!hba)
		return 0;

	cnic = hba->cnic;
	if (!cnic)
		return 0;

	if (bnx2i_ep->state == EP_STATE_IDLE ||
	    bnx2i_ep->state == EP_STATE_DISCONN_TIMEDOUT)
		return 0;

	if (!bnx2i_ep_tcp_conn_active(bnx2i_ep))
		goto destroy_conn;

	if (bnx2i_ep->conn) {
		conn = bnx2i_ep->conn->cls_conn->dd_data;
		session = conn->session;
	}

	init_timer(&bnx2i_ep->ofld_timer);
	bnx2i_ep->ofld_timer.expires = hba->conn_teardown_tmo + jiffies;
	bnx2i_ep->ofld_timer.function = bnx2i_ep_ofld_timer;
	bnx2i_ep->ofld_timer.data = (unsigned long) bnx2i_ep;
	add_timer(&bnx2i_ep->ofld_timer);

	if (!test_bit(BNX2I_CNIC_REGISTERED, &hba->reg_with_cnic))
		goto out;

	if (session) {
		spin_lock_bh(&session->lock);
		if (bnx2i_ep->state != EP_STATE_TCP_FIN_RCVD) {
			if (session->state == ISCSI_STATE_LOGGING_OUT) {
				if (bnx2i_ep->state == EP_STATE_LOGOUT_SENT) {
					/* Logout sent, but no resp */
					printk(KERN_ALERT "bnx2i (%s): WARNING"
						" logout response was not "
						"received!\n",
						bnx2i_ep->hba->netdev->name);
				} else if (bnx2i_ep->state ==
					   EP_STATE_LOGOUT_RESP_RCVD)
					close = 1;
			}
		} else
			close = 1;

		spin_unlock_bh(&session->lock);
	}

	bnx2i_ep->state = EP_STATE_DISCONN_START;

	if (close)
		close_ret = cnic->cm_close(bnx2i_ep->cm_sk);
	else
		close_ret = cnic->cm_abort(bnx2i_ep->cm_sk);

	if (close_ret)
		printk(KERN_ALERT "bnx2i (%s): close/abort(%d) returned %d\n",
			bnx2i_ep->hba->netdev->name, close, close_ret);
	else
		/* wait for option-2 conn teardown */
		wait_event_interruptible(bnx2i_ep->ofld_wait,
				 bnx2i_ep->state != EP_STATE_DISCONN_START);

	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&bnx2i_ep->ofld_timer);

destroy_conn:
	bnx2i_ep_active_list_del(hba, bnx2i_ep);
	if (bnx2i_tear_down_conn(hba, bnx2i_ep))
		return -EINVAL;
out:
	bnx2i_ep->state = EP_STATE_IDLE;
	return ret;
}


/**
 * bnx2i_ep_disconnect - executes TCP connection teardown process
 * @ep:		TCP connection (iscsi endpoint) handle
 *
 * executes  TCP connection teardown process
 */
static void bnx2i_ep_disconnect(struct iscsi_endpoint *ep)
{
	struct bnx2i_endpoint *bnx2i_ep;
	struct bnx2i_conn *bnx2i_conn = NULL;
	struct iscsi_conn *conn = NULL;
	struct bnx2i_hba *hba;

	bnx2i_ep = ep->dd_data;

	/* driver should not attempt connection cleanup until TCP_CONNECT
	 * completes either successfully or fails. Timeout is 9-secs, so
	 * wait for it to complete
	 */
	while ((bnx2i_ep->state == EP_STATE_CONNECT_START) &&
		!time_after(jiffies, bnx2i_ep->timestamp + (12 * HZ)))
		msleep(250);

	if (bnx2i_ep->conn) {
		bnx2i_conn = bnx2i_ep->conn;
		conn = bnx2i_conn->cls_conn->dd_data;
		iscsi_suspend_queue(conn);
	}
	hba = bnx2i_ep->hba;

	mutex_lock(&hba->net_dev_lock);

	if (bnx2i_ep->state == EP_STATE_DISCONN_TIMEDOUT)
		goto out;

	if (bnx2i_ep->state == EP_STATE_IDLE)
		goto free_resc;

	if (!test_bit(ADAPTER_STATE_UP, &hba->adapter_state) ||
	    (bnx2i_ep->hba_age != hba->age)) {
		bnx2i_ep_active_list_del(hba, bnx2i_ep);
		goto free_resc;
	}

	/* Do all chip cleanup here */
	if (bnx2i_hw_ep_disconnect(bnx2i_ep)) {
		mutex_unlock(&hba->net_dev_lock);
		return;
	}
free_resc:
	bnx2i_free_qp_resc(hba, bnx2i_ep);

	if (bnx2i_conn)
		bnx2i_conn->ep = NULL;

	bnx2i_free_ep(ep);
out:
	mutex_unlock(&hba->net_dev_lock);

	wake_up_interruptible(&hba->eh_wait);
}


/**
 * bnx2i_nl_set_path - ISCSI_UEVENT_PATH_UPDATE user message handler
 * @buf:	pointer to buffer containing iscsi path message
 *
 */
static int bnx2i_nl_set_path(struct Scsi_Host *shost, struct iscsi_path *params)
{
	struct bnx2i_hba *hba = iscsi_host_priv(shost);
	char *buf = (char *) params;
	u16 len = sizeof(*params);

	/* handled by cnic driver */
	hba->cnic->iscsi_nl_msg_recv(hba->cnic, ISCSI_UEVENT_PATH_UPDATE, buf,
				     len);

	return 0;
}

static umode_t bnx2i_attr_is_visible(int param_type, int param)
{
	switch (param_type) {
	case ISCSI_HOST_PARAM:
		switch (param) {
		case ISCSI_HOST_PARAM_NETDEV_NAME:
		case ISCSI_HOST_PARAM_HWADDRESS:
		case ISCSI_HOST_PARAM_IPADDRESS:
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
		case ISCSI_PARAM_ERL:
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
			return S_IRUGO;
		default:
			return 0;
		}
	}

	return 0;
}

/*
 * 'Scsi_Host_Template' structure and 'iscsi_tranport' structure template
 * used while registering with the scsi host and iSCSI transport module.
 */
static struct scsi_host_template bnx2i_host_template = {
	.module			= THIS_MODULE,
	.name			= "Broadcom Offload iSCSI Initiator",
	.proc_name		= "bnx2i",
	.queuecommand		= iscsi_queuecommand,
	.eh_abort_handler	= iscsi_eh_abort,
	.eh_device_reset_handler = iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_recover_target,
	.change_queue_depth	= iscsi_change_queue_depth,
	.target_alloc		= iscsi_target_alloc,
	.can_queue		= 2048,
	.max_sectors		= 127,
	.cmd_per_lun		= 128,
	.this_id		= -1,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= ISCSI_MAX_BDS_PER_CMD,
	.shost_attrs		= bnx2i_dev_attributes,
};

struct iscsi_transport bnx2i_iscsi_transport = {
	.owner			= THIS_MODULE,
	.name			= "bnx2i",
	.caps			= CAP_RECOVERY_L0 | CAP_HDRDGST |
				  CAP_MULTI_R2T | CAP_DATADGST |
				  CAP_DATA_PATH_OFFLOAD |
				  CAP_TEXT_NEGO,
	.create_session		= bnx2i_session_create,
	.destroy_session	= bnx2i_session_destroy,
	.create_conn		= bnx2i_conn_create,
	.bind_conn		= bnx2i_conn_bind,
	.destroy_conn		= bnx2i_conn_destroy,
	.attr_is_visible	= bnx2i_attr_is_visible,
	.set_param		= iscsi_set_param,
	.get_conn_param		= iscsi_conn_get_param,
	.get_session_param	= iscsi_session_get_param,
	.get_host_param		= bnx2i_host_get_param,
	.start_conn		= bnx2i_conn_start,
	.stop_conn		= iscsi_conn_stop,
	.send_pdu		= iscsi_conn_send_pdu,
	.xmit_task		= bnx2i_task_xmit,
	.get_stats		= bnx2i_conn_get_stats,
	/* TCP connect - disconnect - option-2 interface calls */
	.get_ep_param		= bnx2i_ep_get_param,
	.ep_connect		= bnx2i_ep_connect,
	.ep_poll		= bnx2i_ep_poll,
	.ep_disconnect		= bnx2i_ep_disconnect,
	.set_path		= bnx2i_nl_set_path,
	/* Error recovery timeout call */
	.session_recovery_timedout = iscsi_session_recovery_timedout,
	.cleanup_task		= bnx2i_cleanup_task,
};

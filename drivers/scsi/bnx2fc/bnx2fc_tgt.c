/* bnx2fc_tgt.c: QLogic Linux FCoE offload driver.
 * Handles operations such as session offload/upload etc, and manages
 * session resources such as connection id and qp resources.
 *
 * Copyright (c) 2008-2013 Broadcom Corporation
 * Copyright (c) 2014-2015 QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Bhanu Prakash Gollapudi (bprakash@broadcom.com)
 */

#include "bnx2fc.h"
static void bnx2fc_upld_timer(unsigned long data);
static void bnx2fc_ofld_timer(unsigned long data);
static int bnx2fc_init_tgt(struct bnx2fc_rport *tgt,
			   struct fcoe_port *port,
			   struct fc_rport_priv *rdata);
static u32 bnx2fc_alloc_conn_id(struct bnx2fc_hba *hba,
				struct bnx2fc_rport *tgt);
static int bnx2fc_alloc_session_resc(struct bnx2fc_hba *hba,
			      struct bnx2fc_rport *tgt);
static void bnx2fc_free_session_resc(struct bnx2fc_hba *hba,
			      struct bnx2fc_rport *tgt);
static void bnx2fc_free_conn_id(struct bnx2fc_hba *hba, u32 conn_id);

static void bnx2fc_upld_timer(unsigned long data)
{

	struct bnx2fc_rport *tgt = (struct bnx2fc_rport *)data;

	BNX2FC_TGT_DBG(tgt, "upld_timer - Upload compl not received!!\n");
	/* fake upload completion */
	clear_bit(BNX2FC_FLAG_OFFLOADED, &tgt->flags);
	clear_bit(BNX2FC_FLAG_ENABLED, &tgt->flags);
	set_bit(BNX2FC_FLAG_UPLD_REQ_COMPL, &tgt->flags);
	wake_up_interruptible(&tgt->upld_wait);
}

static void bnx2fc_ofld_timer(unsigned long data)
{

	struct bnx2fc_rport *tgt = (struct bnx2fc_rport *)data;

	BNX2FC_TGT_DBG(tgt, "entered bnx2fc_ofld_timer\n");
	/* NOTE: This function should never be called, as
	 * offload should never timeout
	 */
	/*
	 * If the timer has expired, this session is dead
	 * Clear offloaded flag and logout of this device.
	 * Since OFFLOADED flag is cleared, this case
	 * will be considered as offload error and the
	 * port will be logged off, and conn_id, session
	 * resources are freed up in bnx2fc_offload_session
	 */
	clear_bit(BNX2FC_FLAG_OFFLOADED, &tgt->flags);
	clear_bit(BNX2FC_FLAG_ENABLED, &tgt->flags);
	set_bit(BNX2FC_FLAG_OFLD_REQ_CMPL, &tgt->flags);
	wake_up_interruptible(&tgt->ofld_wait);
}

static void bnx2fc_ofld_wait(struct bnx2fc_rport *tgt)
{
	setup_timer(&tgt->ofld_timer, bnx2fc_ofld_timer, (unsigned long)tgt);
	mod_timer(&tgt->ofld_timer, jiffies + BNX2FC_FW_TIMEOUT);

	wait_event_interruptible(tgt->ofld_wait,
				 (test_bit(
				  BNX2FC_FLAG_OFLD_REQ_CMPL,
				  &tgt->flags)));
	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&tgt->ofld_timer);
}

static void bnx2fc_offload_session(struct fcoe_port *port,
					struct bnx2fc_rport *tgt,
					struct fc_rport_priv *rdata)
{
	struct fc_rport *rport = rdata->rport;
	struct bnx2fc_interface *interface = port->priv;
	struct bnx2fc_hba *hba = interface->hba;
	int rval;
	int i = 0;

	/* Initialize bnx2fc_rport */
	/* NOTE: tgt is already bzero'd */
	rval = bnx2fc_init_tgt(tgt, port, rdata);
	if (rval) {
		printk(KERN_ERR PFX "Failed to allocate conn id for "
			"port_id (%6x)\n", rport->port_id);
		goto tgt_init_err;
	}

	/* Allocate session resources */
	rval = bnx2fc_alloc_session_resc(hba, tgt);
	if (rval) {
		printk(KERN_ERR PFX "Failed to allocate resources\n");
		goto ofld_err;
	}

	/*
	 * Initialize FCoE session offload process.
	 * Upon completion of offload process add
	 * rport to list of rports
	 */
retry_ofld:
	clear_bit(BNX2FC_FLAG_OFLD_REQ_CMPL, &tgt->flags);
	rval = bnx2fc_send_session_ofld_req(port, tgt);
	if (rval) {
		printk(KERN_ERR PFX "ofld_req failed\n");
		goto ofld_err;
	}

	/*
	 * wait for the session is offloaded and enabled. 3 Secs
	 * should be ample time for this process to complete.
	 */
	bnx2fc_ofld_wait(tgt);

	if (!(test_bit(BNX2FC_FLAG_OFFLOADED, &tgt->flags))) {
		if (test_and_clear_bit(BNX2FC_FLAG_CTX_ALLOC_FAILURE,
				       &tgt->flags)) {
			BNX2FC_TGT_DBG(tgt, "ctx_alloc_failure, "
				"retry ofld..%d\n", i++);
			msleep_interruptible(1000);
			if (i > 3) {
				i = 0;
				goto ofld_err;
			}
			goto retry_ofld;
		}
		goto ofld_err;
	}
	if (bnx2fc_map_doorbell(tgt)) {
		printk(KERN_ERR PFX "map doorbell failed - no mem\n");
		goto ofld_err;
	}
	clear_bit(BNX2FC_FLAG_OFLD_REQ_CMPL, &tgt->flags);
	rval = bnx2fc_send_session_enable_req(port, tgt);
	if (rval) {
		pr_err(PFX "enable session failed\n");
		goto ofld_err;
	}
	bnx2fc_ofld_wait(tgt);
	if (!(test_bit(BNX2FC_FLAG_ENABLED, &tgt->flags)))
		goto ofld_err;
	return;

ofld_err:
	/* couldn't offload the session. log off from this rport */
	BNX2FC_TGT_DBG(tgt, "bnx2fc_offload_session - offload error\n");
	clear_bit(BNX2FC_FLAG_OFFLOADED, &tgt->flags);
	/* Free session resources */
	bnx2fc_free_session_resc(hba, tgt);
tgt_init_err:
	if (tgt->fcoe_conn_id != -1)
		bnx2fc_free_conn_id(hba, tgt->fcoe_conn_id);
	fc_rport_logoff(rdata);
}

void bnx2fc_flush_active_ios(struct bnx2fc_rport *tgt)
{
	struct bnx2fc_cmd *io_req;
	struct bnx2fc_cmd *tmp;
	int rc;
	int i = 0;
	BNX2FC_TGT_DBG(tgt, "Entered flush_active_ios - %d\n",
		       tgt->num_active_ios.counter);

	spin_lock_bh(&tgt->tgt_lock);
	tgt->flush_in_prog = 1;

	list_for_each_entry_safe(io_req, tmp, &tgt->active_cmd_queue, link) {
		i++;
		list_del_init(&io_req->link);
		io_req->on_active_queue = 0;
		BNX2FC_IO_DBG(io_req, "cmd_queue cleanup\n");

		if (cancel_delayed_work(&io_req->timeout_work)) {
			if (test_and_clear_bit(BNX2FC_FLAG_EH_ABORT,
						&io_req->req_flags)) {
				/* Handle eh_abort timeout */
				BNX2FC_IO_DBG(io_req, "eh_abort for IO "
					      "cleaned up\n");
				complete(&io_req->tm_done);
			}
			kref_put(&io_req->refcount,
				 bnx2fc_cmd_release); /* drop timer hold */
		}

		set_bit(BNX2FC_FLAG_IO_COMPL, &io_req->req_flags);
		set_bit(BNX2FC_FLAG_IO_CLEANUP, &io_req->req_flags);

		/* Do not issue cleanup when disable request failed */
		if (test_bit(BNX2FC_FLAG_DISABLE_FAILED, &tgt->flags))
			bnx2fc_process_cleanup_compl(io_req, io_req->task, 0);
		else {
			rc = bnx2fc_initiate_cleanup(io_req);
			BUG_ON(rc);
		}
	}

	list_for_each_entry_safe(io_req, tmp, &tgt->active_tm_queue, link) {
		i++;
		list_del_init(&io_req->link);
		io_req->on_tmf_queue = 0;
		BNX2FC_IO_DBG(io_req, "tm_queue cleanup\n");
		if (io_req->wait_for_comp)
			complete(&io_req->tm_done);
	}

	list_for_each_entry_safe(io_req, tmp, &tgt->els_queue, link) {
		i++;
		list_del_init(&io_req->link);
		io_req->on_active_queue = 0;

		BNX2FC_IO_DBG(io_req, "els_queue cleanup\n");

		if (cancel_delayed_work(&io_req->timeout_work))
			kref_put(&io_req->refcount,
				 bnx2fc_cmd_release); /* drop timer hold */

		if ((io_req->cb_func) && (io_req->cb_arg)) {
			io_req->cb_func(io_req->cb_arg);
			io_req->cb_arg = NULL;
		}

		/* Do not issue cleanup when disable request failed */
		if (test_bit(BNX2FC_FLAG_DISABLE_FAILED, &tgt->flags))
			bnx2fc_process_cleanup_compl(io_req, io_req->task, 0);
		else {
			rc = bnx2fc_initiate_cleanup(io_req);
			BUG_ON(rc);
		}
	}

	list_for_each_entry_safe(io_req, tmp, &tgt->io_retire_queue, link) {
		i++;
		list_del_init(&io_req->link);

		BNX2FC_IO_DBG(io_req, "retire_queue flush\n");

		if (cancel_delayed_work(&io_req->timeout_work)) {
			if (test_and_clear_bit(BNX2FC_FLAG_EH_ABORT,
						&io_req->req_flags)) {
				/* Handle eh_abort timeout */
				BNX2FC_IO_DBG(io_req, "eh_abort for IO "
					      "in retire_q\n");
				if (io_req->wait_for_comp)
					complete(&io_req->tm_done);
			}
			kref_put(&io_req->refcount, bnx2fc_cmd_release);
		}

		clear_bit(BNX2FC_FLAG_ISSUE_RRQ, &io_req->req_flags);
	}

	BNX2FC_TGT_DBG(tgt, "IOs flushed = %d\n", i);
	i = 0;
	spin_unlock_bh(&tgt->tgt_lock);
	/* wait for active_ios to go to 0 */
	while ((tgt->num_active_ios.counter != 0) && (i++ < BNX2FC_WAIT_CNT))
		msleep(25);
	if (tgt->num_active_ios.counter != 0)
		printk(KERN_ERR PFX "CLEANUP on port 0x%x:"
				    " active_ios = %d\n",
			tgt->rdata->ids.port_id, tgt->num_active_ios.counter);
	spin_lock_bh(&tgt->tgt_lock);
	tgt->flush_in_prog = 0;
	spin_unlock_bh(&tgt->tgt_lock);
}

static void bnx2fc_upld_wait(struct bnx2fc_rport *tgt)
{
	setup_timer(&tgt->upld_timer, bnx2fc_upld_timer, (unsigned long)tgt);
	mod_timer(&tgt->upld_timer, jiffies + BNX2FC_FW_TIMEOUT);
	wait_event_interruptible(tgt->upld_wait,
				 (test_bit(
				  BNX2FC_FLAG_UPLD_REQ_COMPL,
				  &tgt->flags)));
	if (signal_pending(current))
		flush_signals(current);
	del_timer_sync(&tgt->upld_timer);
}

static void bnx2fc_upload_session(struct fcoe_port *port,
					struct bnx2fc_rport *tgt)
{
	struct bnx2fc_interface *interface = port->priv;
	struct bnx2fc_hba *hba = interface->hba;

	BNX2FC_TGT_DBG(tgt, "upload_session: active_ios = %d\n",
		tgt->num_active_ios.counter);

	/*
	 * Called with hba->hba_mutex held.
	 * This is a blocking call
	 */
	clear_bit(BNX2FC_FLAG_UPLD_REQ_COMPL, &tgt->flags);
	bnx2fc_send_session_disable_req(port, tgt);

	/*
	 * wait for upload to complete. 3 Secs
	 * should be sufficient time for this process to complete.
	 */
	BNX2FC_TGT_DBG(tgt, "waiting for disable compl\n");
	bnx2fc_upld_wait(tgt);

	/*
	 * traverse thru the active_q and tmf_q and cleanup
	 * IOs in these lists
	 */
	BNX2FC_TGT_DBG(tgt, "flush/upload - disable wait flags = 0x%lx\n",
		       tgt->flags);
	bnx2fc_flush_active_ios(tgt);

	/* Issue destroy KWQE */
	if (test_bit(BNX2FC_FLAG_DISABLED, &tgt->flags)) {
		BNX2FC_TGT_DBG(tgt, "send destroy req\n");
		clear_bit(BNX2FC_FLAG_UPLD_REQ_COMPL, &tgt->flags);
		bnx2fc_send_session_destroy_req(hba, tgt);

		/* wait for destroy to complete */
		bnx2fc_upld_wait(tgt);

		if (!(test_bit(BNX2FC_FLAG_DESTROYED, &tgt->flags)))
			printk(KERN_ERR PFX "ERROR!! destroy timed out\n");

		BNX2FC_TGT_DBG(tgt, "destroy wait complete flags = 0x%lx\n",
			tgt->flags);

	} else if (test_bit(BNX2FC_FLAG_DISABLE_FAILED, &tgt->flags)) {
		printk(KERN_ERR PFX "ERROR!! DISABLE req failed, destroy"
				" not sent to FW\n");
	} else {
		printk(KERN_ERR PFX "ERROR!! DISABLE req timed out, destroy"
				" not sent to FW\n");
	}

	/* Free session resources */
	bnx2fc_free_session_resc(hba, tgt);
	bnx2fc_free_conn_id(hba, tgt->fcoe_conn_id);
}

static int bnx2fc_init_tgt(struct bnx2fc_rport *tgt,
			   struct fcoe_port *port,
			   struct fc_rport_priv *rdata)
{

	struct fc_rport *rport = rdata->rport;
	struct bnx2fc_interface *interface = port->priv;
	struct bnx2fc_hba *hba = interface->hba;
	struct b577xx_doorbell_set_prod *sq_db = &tgt->sq_db;
	struct b577xx_fcoe_rx_doorbell *rx_db = &tgt->rx_db;

	tgt->rport = rport;
	tgt->rdata = rdata;
	tgt->port = port;

	if (hba->num_ofld_sess >= BNX2FC_NUM_MAX_SESS) {
		BNX2FC_TGT_DBG(tgt, "exceeded max sessions. logoff this tgt\n");
		tgt->fcoe_conn_id = -1;
		return -1;
	}

	tgt->fcoe_conn_id = bnx2fc_alloc_conn_id(hba, tgt);
	if (tgt->fcoe_conn_id == -1)
		return -1;

	BNX2FC_TGT_DBG(tgt, "init_tgt - conn_id = 0x%x\n", tgt->fcoe_conn_id);

	tgt->max_sqes = BNX2FC_SQ_WQES_MAX;
	tgt->max_rqes = BNX2FC_RQ_WQES_MAX;
	tgt->max_cqes = BNX2FC_CQ_WQES_MAX;
	atomic_set(&tgt->free_sqes, BNX2FC_SQ_WQES_MAX);

	/* Initialize the toggle bit */
	tgt->sq_curr_toggle_bit = 1;
	tgt->cq_curr_toggle_bit = 1;
	tgt->sq_prod_idx = 0;
	tgt->cq_cons_idx = 0;
	tgt->rq_prod_idx = 0x8000;
	tgt->rq_cons_idx = 0;
	atomic_set(&tgt->num_active_ios, 0);
	tgt->retry_delay_timestamp = 0;

	if (rdata->flags & FC_RP_FLAGS_RETRY &&
	    rdata->ids.roles & FC_RPORT_ROLE_FCP_TARGET &&
	    !(rdata->ids.roles & FC_RPORT_ROLE_FCP_INITIATOR)) {
		tgt->dev_type = TYPE_TAPE;
		tgt->io_timeout = 0; /* use default ULP timeout */
	} else {
		tgt->dev_type = TYPE_DISK;
		tgt->io_timeout = BNX2FC_IO_TIMEOUT;
	}

	/* initialize sq doorbell */
	sq_db->header.header = B577XX_DOORBELL_HDR_DB_TYPE;
	sq_db->header.header |= B577XX_FCOE_CONNECTION_TYPE <<
					B577XX_DOORBELL_HDR_CONN_TYPE_SHIFT;
	/* initialize rx doorbell */
	rx_db->hdr.header = ((0x1 << B577XX_DOORBELL_HDR_RX_SHIFT) |
			  (0x1 << B577XX_DOORBELL_HDR_DB_TYPE_SHIFT) |
			  (B577XX_FCOE_CONNECTION_TYPE <<
				B577XX_DOORBELL_HDR_CONN_TYPE_SHIFT));
	rx_db->params = (0x2 << B577XX_FCOE_RX_DOORBELL_NEGATIVE_ARM_SHIFT) |
		     (0x3 << B577XX_FCOE_RX_DOORBELL_OPCODE_SHIFT);

	spin_lock_init(&tgt->tgt_lock);
	spin_lock_init(&tgt->cq_lock);

	/* Initialize active_cmd_queue list */
	INIT_LIST_HEAD(&tgt->active_cmd_queue);

	/* Initialize IO retire queue */
	INIT_LIST_HEAD(&tgt->io_retire_queue);

	INIT_LIST_HEAD(&tgt->els_queue);

	/* Initialize active_tm_queue list */
	INIT_LIST_HEAD(&tgt->active_tm_queue);

	init_waitqueue_head(&tgt->ofld_wait);
	init_waitqueue_head(&tgt->upld_wait);

	return 0;
}

/**
 * This event_callback is called after successful completion of libfc
 * initiated target login. bnx2fc can proceed with initiating the session
 * establishment.
 */
void bnx2fc_rport_event_handler(struct fc_lport *lport,
				struct fc_rport_priv *rdata,
				enum fc_rport_event event)
{
	struct fcoe_port *port = lport_priv(lport);
	struct bnx2fc_interface *interface = port->priv;
	struct bnx2fc_hba *hba = interface->hba;
	struct fc_rport *rport = rdata->rport;
	struct fc_rport_libfc_priv *rp;
	struct bnx2fc_rport *tgt;
	u32 port_id;

	BNX2FC_HBA_DBG(lport, "rport_event_hdlr: event = %d, port_id = 0x%x\n",
		event, rdata->ids.port_id);
	switch (event) {
	case RPORT_EV_READY:
		if (!rport) {
			printk(KERN_ERR PFX "rport is NULL: ERROR!\n");
			break;
		}

		rp = rport->dd_data;
		if (rport->port_id == FC_FID_DIR_SERV) {
			/*
			 * bnx2fc_rport structure doesn't exist for
			 * directory server.
			 * We should not come here, as lport will
			 * take care of fabric login
			 */
			printk(KERN_ERR PFX "%x - rport_event_handler ERROR\n",
				rdata->ids.port_id);
			break;
		}

		if (rdata->spp_type != FC_TYPE_FCP) {
			BNX2FC_HBA_DBG(lport, "not FCP type target."
				   " not offloading\n");
			break;
		}
		if (!(rdata->ids.roles & FC_RPORT_ROLE_FCP_TARGET)) {
			BNX2FC_HBA_DBG(lport, "not FCP_TARGET"
				   " not offloading\n");
			break;
		}

		/*
		 * Offlaod process is protected with hba mutex.
		 * Use the same mutex_lock for upload process too
		 */
		mutex_lock(&hba->hba_mutex);
		tgt = (struct bnx2fc_rport *)&rp[1];

		/* This can happen when ADISC finds the same target */
		if (test_bit(BNX2FC_FLAG_ENABLED, &tgt->flags)) {
			BNX2FC_TGT_DBG(tgt, "already offloaded\n");
			mutex_unlock(&hba->hba_mutex);
			return;
		}

		/*
		 * Offload the session. This is a blocking call, and will
		 * wait until the session is offloaded.
		 */
		bnx2fc_offload_session(port, tgt, rdata);

		BNX2FC_TGT_DBG(tgt, "OFFLOAD num_ofld_sess = %d\n",
			hba->num_ofld_sess);

		if (test_bit(BNX2FC_FLAG_ENABLED, &tgt->flags)) {
			/* Session is offloaded and enabled.  */
			BNX2FC_TGT_DBG(tgt, "sess offloaded\n");
			/* This counter is protected with hba mutex */
			hba->num_ofld_sess++;

			set_bit(BNX2FC_FLAG_SESSION_READY, &tgt->flags);
		} else {
			/*
			 * Offload or enable would have failed.
			 * In offload/enable completion path, the
			 * rport would have already been removed
			 */
			BNX2FC_TGT_DBG(tgt, "Port is being logged off as "
				   "offloaded flag not set\n");
		}
		mutex_unlock(&hba->hba_mutex);
		break;
	case RPORT_EV_LOGO:
	case RPORT_EV_FAILED:
	case RPORT_EV_STOP:
		port_id = rdata->ids.port_id;
		if (port_id == FC_FID_DIR_SERV)
			break;

		if (!rport) {
			printk(KERN_INFO PFX "%x - rport not created Yet!!\n",
				port_id);
			break;
		}
		rp = rport->dd_data;
		mutex_lock(&hba->hba_mutex);
		/*
		 * Perform session upload. Note that rdata->peers is already
		 * removed from disc->rports list before we get this event.
		 */
		tgt = (struct bnx2fc_rport *)&rp[1];

		if (!(test_bit(BNX2FC_FLAG_ENABLED, &tgt->flags))) {
			mutex_unlock(&hba->hba_mutex);
			break;
		}
		clear_bit(BNX2FC_FLAG_SESSION_READY, &tgt->flags);

		bnx2fc_upload_session(port, tgt);
		hba->num_ofld_sess--;
		BNX2FC_TGT_DBG(tgt, "UPLOAD num_ofld_sess = %d\n",
			hba->num_ofld_sess);
		/*
		 * Try to wake up the linkdown wait thread. If num_ofld_sess
		 * is 0, the waiting therad wakes up
		 */
		if ((hba->wait_for_link_down) &&
		    (hba->num_ofld_sess == 0)) {
			wake_up_interruptible(&hba->shutdown_wait);
		}
		mutex_unlock(&hba->hba_mutex);

		break;

	case RPORT_EV_NONE:
		break;
	}
}

/**
 * bnx2fc_tgt_lookup() - Lookup a bnx2fc_rport by port_id
 *
 * @port:  fcoe_port struct to lookup the target port on
 * @port_id: The remote port ID to look up
 */
struct bnx2fc_rport *bnx2fc_tgt_lookup(struct fcoe_port *port,
					     u32 port_id)
{
	struct bnx2fc_interface *interface = port->priv;
	struct bnx2fc_hba *hba = interface->hba;
	struct bnx2fc_rport *tgt;
	struct fc_rport_priv *rdata;
	int i;

	for (i = 0; i < BNX2FC_NUM_MAX_SESS; i++) {
		tgt = hba->tgt_ofld_list[i];
		if ((tgt) && (tgt->port == port)) {
			rdata = tgt->rdata;
			if (rdata->ids.port_id == port_id) {
				if (rdata->rp_state != RPORT_ST_DELETE) {
					BNX2FC_TGT_DBG(tgt, "rport "
						"obtained\n");
					return tgt;
				} else {
					BNX2FC_TGT_DBG(tgt, "rport 0x%x "
						"is in DELETED state\n",
						rdata->ids.port_id);
					return NULL;
				}
			}
		}
	}
	return NULL;
}


/**
 * bnx2fc_alloc_conn_id - allocates FCOE Connection id
 *
 * @hba:	pointer to adapter structure
 * @tgt:	pointer to bnx2fc_rport structure
 */
static u32 bnx2fc_alloc_conn_id(struct bnx2fc_hba *hba,
				struct bnx2fc_rport *tgt)
{
	u32 conn_id, next;

	/* called with hba mutex held */

	/*
	 * tgt_ofld_list access is synchronized using
	 * both hba mutex and hba lock. Atleast hba mutex or
	 * hba lock needs to be held for read access.
	 */

	spin_lock_bh(&hba->hba_lock);
	next = hba->next_conn_id;
	conn_id = hba->next_conn_id++;
	if (hba->next_conn_id == BNX2FC_NUM_MAX_SESS)
		hba->next_conn_id = 0;

	while (hba->tgt_ofld_list[conn_id] != NULL) {
		conn_id++;
		if (conn_id == BNX2FC_NUM_MAX_SESS)
			conn_id = 0;

		if (conn_id == next) {
			/* No free conn_ids are available */
			spin_unlock_bh(&hba->hba_lock);
			return -1;
		}
	}
	hba->tgt_ofld_list[conn_id] = tgt;
	tgt->fcoe_conn_id = conn_id;
	spin_unlock_bh(&hba->hba_lock);
	return conn_id;
}

static void bnx2fc_free_conn_id(struct bnx2fc_hba *hba, u32 conn_id)
{
	/* called with hba mutex held */
	spin_lock_bh(&hba->hba_lock);
	hba->tgt_ofld_list[conn_id] = NULL;
	spin_unlock_bh(&hba->hba_lock);
}

/**
 *bnx2fc_alloc_session_resc - Allocate qp resources for the session
 *
 */
static int bnx2fc_alloc_session_resc(struct bnx2fc_hba *hba,
					struct bnx2fc_rport *tgt)
{
	dma_addr_t page;
	int num_pages;
	u32 *pbl;

	/* Allocate and map SQ */
	tgt->sq_mem_size = tgt->max_sqes * BNX2FC_SQ_WQE_SIZE;
	tgt->sq_mem_size = (tgt->sq_mem_size + (CNIC_PAGE_SIZE - 1)) &
			   CNIC_PAGE_MASK;

	tgt->sq = dma_alloc_coherent(&hba->pcidev->dev, tgt->sq_mem_size,
				     &tgt->sq_dma, GFP_KERNEL);
	if (!tgt->sq) {
		printk(KERN_ERR PFX "unable to allocate SQ memory %d\n",
			tgt->sq_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->sq, 0, tgt->sq_mem_size);

	/* Allocate and map CQ */
	tgt->cq_mem_size = tgt->max_cqes * BNX2FC_CQ_WQE_SIZE;
	tgt->cq_mem_size = (tgt->cq_mem_size + (CNIC_PAGE_SIZE - 1)) &
			   CNIC_PAGE_MASK;

	tgt->cq = dma_alloc_coherent(&hba->pcidev->dev, tgt->cq_mem_size,
				     &tgt->cq_dma, GFP_KERNEL);
	if (!tgt->cq) {
		printk(KERN_ERR PFX "unable to allocate CQ memory %d\n",
			tgt->cq_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->cq, 0, tgt->cq_mem_size);

	/* Allocate and map RQ and RQ PBL */
	tgt->rq_mem_size = tgt->max_rqes * BNX2FC_RQ_WQE_SIZE;
	tgt->rq_mem_size = (tgt->rq_mem_size + (CNIC_PAGE_SIZE - 1)) &
			   CNIC_PAGE_MASK;

	tgt->rq = dma_alloc_coherent(&hba->pcidev->dev, tgt->rq_mem_size,
					&tgt->rq_dma, GFP_KERNEL);
	if (!tgt->rq) {
		printk(KERN_ERR PFX "unable to allocate RQ memory %d\n",
			tgt->rq_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->rq, 0, tgt->rq_mem_size);

	tgt->rq_pbl_size = (tgt->rq_mem_size / CNIC_PAGE_SIZE) * sizeof(void *);
	tgt->rq_pbl_size = (tgt->rq_pbl_size + (CNIC_PAGE_SIZE - 1)) &
			   CNIC_PAGE_MASK;

	tgt->rq_pbl = dma_alloc_coherent(&hba->pcidev->dev, tgt->rq_pbl_size,
					 &tgt->rq_pbl_dma, GFP_KERNEL);
	if (!tgt->rq_pbl) {
		printk(KERN_ERR PFX "unable to allocate RQ PBL %d\n",
			tgt->rq_pbl_size);
		goto mem_alloc_failure;
	}

	memset(tgt->rq_pbl, 0, tgt->rq_pbl_size);
	num_pages = tgt->rq_mem_size / CNIC_PAGE_SIZE;
	page = tgt->rq_dma;
	pbl = (u32 *)tgt->rq_pbl;

	while (num_pages--) {
		*pbl = (u32)page;
		pbl++;
		*pbl = (u32)((u64)page >> 32);
		pbl++;
		page += CNIC_PAGE_SIZE;
	}

	/* Allocate and map XFERQ */
	tgt->xferq_mem_size = tgt->max_sqes * BNX2FC_XFERQ_WQE_SIZE;
	tgt->xferq_mem_size = (tgt->xferq_mem_size + (CNIC_PAGE_SIZE - 1)) &
			       CNIC_PAGE_MASK;

	tgt->xferq = dma_alloc_coherent(&hba->pcidev->dev, tgt->xferq_mem_size,
					&tgt->xferq_dma, GFP_KERNEL);
	if (!tgt->xferq) {
		printk(KERN_ERR PFX "unable to allocate XFERQ %d\n",
			tgt->xferq_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->xferq, 0, tgt->xferq_mem_size);

	/* Allocate and map CONFQ & CONFQ PBL */
	tgt->confq_mem_size = tgt->max_sqes * BNX2FC_CONFQ_WQE_SIZE;
	tgt->confq_mem_size = (tgt->confq_mem_size + (CNIC_PAGE_SIZE - 1)) &
			       CNIC_PAGE_MASK;

	tgt->confq = dma_alloc_coherent(&hba->pcidev->dev, tgt->confq_mem_size,
					&tgt->confq_dma, GFP_KERNEL);
	if (!tgt->confq) {
		printk(KERN_ERR PFX "unable to allocate CONFQ %d\n",
			tgt->confq_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->confq, 0, tgt->confq_mem_size);

	tgt->confq_pbl_size =
		(tgt->confq_mem_size / CNIC_PAGE_SIZE) * sizeof(void *);
	tgt->confq_pbl_size =
		(tgt->confq_pbl_size + (CNIC_PAGE_SIZE - 1)) & CNIC_PAGE_MASK;

	tgt->confq_pbl = dma_alloc_coherent(&hba->pcidev->dev,
					    tgt->confq_pbl_size,
					    &tgt->confq_pbl_dma, GFP_KERNEL);
	if (!tgt->confq_pbl) {
		printk(KERN_ERR PFX "unable to allocate CONFQ PBL %d\n",
			tgt->confq_pbl_size);
		goto mem_alloc_failure;
	}

	memset(tgt->confq_pbl, 0, tgt->confq_pbl_size);
	num_pages = tgt->confq_mem_size / CNIC_PAGE_SIZE;
	page = tgt->confq_dma;
	pbl = (u32 *)tgt->confq_pbl;

	while (num_pages--) {
		*pbl = (u32)page;
		pbl++;
		*pbl = (u32)((u64)page >> 32);
		pbl++;
		page += CNIC_PAGE_SIZE;
	}

	/* Allocate and map ConnDB */
	tgt->conn_db_mem_size = sizeof(struct fcoe_conn_db);

	tgt->conn_db = dma_alloc_coherent(&hba->pcidev->dev,
					  tgt->conn_db_mem_size,
					  &tgt->conn_db_dma, GFP_KERNEL);
	if (!tgt->conn_db) {
		printk(KERN_ERR PFX "unable to allocate conn_db %d\n",
						tgt->conn_db_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->conn_db, 0, tgt->conn_db_mem_size);


	/* Allocate and map LCQ */
	tgt->lcq_mem_size = (tgt->max_sqes + 8) * BNX2FC_SQ_WQE_SIZE;
	tgt->lcq_mem_size = (tgt->lcq_mem_size + (CNIC_PAGE_SIZE - 1)) &
			     CNIC_PAGE_MASK;

	tgt->lcq = dma_alloc_coherent(&hba->pcidev->dev, tgt->lcq_mem_size,
				      &tgt->lcq_dma, GFP_KERNEL);

	if (!tgt->lcq) {
		printk(KERN_ERR PFX "unable to allocate lcq %d\n",
		       tgt->lcq_mem_size);
		goto mem_alloc_failure;
	}
	memset(tgt->lcq, 0, tgt->lcq_mem_size);

	tgt->conn_db->rq_prod = 0x8000;

	return 0;

mem_alloc_failure:
	return -ENOMEM;
}

/**
 * bnx2i_free_session_resc - free qp resources for the session
 *
 * @hba:	adapter structure pointer
 * @tgt:	bnx2fc_rport structure pointer
 *
 * Free QP resources - SQ/RQ/CQ/XFERQ memory and PBL
 */
static void bnx2fc_free_session_resc(struct bnx2fc_hba *hba,
						struct bnx2fc_rport *tgt)
{
	void __iomem *ctx_base_ptr;

	BNX2FC_TGT_DBG(tgt, "Freeing up session resources\n");

	spin_lock_bh(&tgt->cq_lock);
	ctx_base_ptr = tgt->ctx_base;
	tgt->ctx_base = NULL;

	/* Free LCQ */
	if (tgt->lcq) {
		dma_free_coherent(&hba->pcidev->dev, tgt->lcq_mem_size,
				    tgt->lcq, tgt->lcq_dma);
		tgt->lcq = NULL;
	}
	/* Free connDB */
	if (tgt->conn_db) {
		dma_free_coherent(&hba->pcidev->dev, tgt->conn_db_mem_size,
				    tgt->conn_db, tgt->conn_db_dma);
		tgt->conn_db = NULL;
	}
	/* Free confq  and confq pbl */
	if (tgt->confq_pbl) {
		dma_free_coherent(&hba->pcidev->dev, tgt->confq_pbl_size,
				    tgt->confq_pbl, tgt->confq_pbl_dma);
		tgt->confq_pbl = NULL;
	}
	if (tgt->confq) {
		dma_free_coherent(&hba->pcidev->dev, tgt->confq_mem_size,
				    tgt->confq, tgt->confq_dma);
		tgt->confq = NULL;
	}
	/* Free XFERQ */
	if (tgt->xferq) {
		dma_free_coherent(&hba->pcidev->dev, tgt->xferq_mem_size,
				    tgt->xferq, tgt->xferq_dma);
		tgt->xferq = NULL;
	}
	/* Free RQ PBL and RQ */
	if (tgt->rq_pbl) {
		dma_free_coherent(&hba->pcidev->dev, tgt->rq_pbl_size,
				    tgt->rq_pbl, tgt->rq_pbl_dma);
		tgt->rq_pbl = NULL;
	}
	if (tgt->rq) {
		dma_free_coherent(&hba->pcidev->dev, tgt->rq_mem_size,
				    tgt->rq, tgt->rq_dma);
		tgt->rq = NULL;
	}
	/* Free CQ */
	if (tgt->cq) {
		dma_free_coherent(&hba->pcidev->dev, tgt->cq_mem_size,
				    tgt->cq, tgt->cq_dma);
		tgt->cq = NULL;
	}
	/* Free SQ */
	if (tgt->sq) {
		dma_free_coherent(&hba->pcidev->dev, tgt->sq_mem_size,
				    tgt->sq, tgt->sq_dma);
		tgt->sq = NULL;
	}
	spin_unlock_bh(&tgt->cq_lock);

	if (ctx_base_ptr)
		iounmap(ctx_base_ptr);
}

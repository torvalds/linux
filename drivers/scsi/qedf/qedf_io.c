// SPDX-License-Identifier: GPL-2.0-only
/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016-2018 Cavium Inc.
 */
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include "qedf.h"
#include <scsi/scsi_tcq.h>

void qedf_cmd_timer_set(struct qedf_ctx *qedf, struct qedf_ioreq *io_req,
	unsigned int timer_msec)
{
	queue_delayed_work(qedf->timer_work_queue, &io_req->timeout_work,
	    msecs_to_jiffies(timer_msec));
}

static void qedf_cmd_timeout(struct work_struct *work)
{

	struct qedf_ioreq *io_req =
	    container_of(work, struct qedf_ioreq, timeout_work.work);
	struct qedf_ctx *qedf;
	struct qedf_rport *fcport;

	fcport = io_req->fcport;
	if (io_req->fcport == NULL) {
		QEDF_INFO(NULL, QEDF_LOG_IO,  "fcport is NULL.\n");
		return;
	}

	qedf = fcport->qedf;

	switch (io_req->cmd_type) {
	case QEDF_ABTS:
		if (qedf == NULL) {
			QEDF_INFO(NULL, QEDF_LOG_IO,
				  "qedf is NULL for ABTS xid=0x%x.\n",
				  io_req->xid);
			return;
		}

		QEDF_ERR((&qedf->dbg_ctx), "ABTS timeout, xid=0x%x.\n",
		    io_req->xid);
		/* Cleanup timed out ABTS */
		qedf_initiate_cleanup(io_req, true);
		complete(&io_req->abts_done);

		/*
		 * Need to call kref_put for reference taken when initiate_abts
		 * was called since abts_compl won't be called now that we've
		 * cleaned up the task.
		 */
		kref_put(&io_req->refcount, qedf_release_cmd);

		/* Clear in abort bit now that we're done with the command */
		clear_bit(QEDF_CMD_IN_ABORT, &io_req->flags);

		/*
		 * Now that the original I/O and the ABTS are complete see
		 * if we need to reconnect to the target.
		 */
		qedf_restart_rport(fcport);
		break;
	case QEDF_ELS:
		if (!qedf) {
			QEDF_INFO(NULL, QEDF_LOG_IO,
				  "qedf is NULL for ELS xid=0x%x.\n",
				  io_req->xid);
			return;
		}
		/* ELS request no longer outstanding since it timed out */
		clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);

		kref_get(&io_req->refcount);
		/*
		 * Don't attempt to clean an ELS timeout as any subseqeunt
		 * ABTS or cleanup requests just hang.  For now just free
		 * the resources of the original I/O and the RRQ
		 */
		QEDF_ERR(&(qedf->dbg_ctx), "ELS timeout, xid=0x%x.\n",
			  io_req->xid);
		qedf_initiate_cleanup(io_req, true);
		io_req->event = QEDF_IOREQ_EV_ELS_TMO;
		/* Call callback function to complete command */
		if (io_req->cb_func && io_req->cb_arg) {
			io_req->cb_func(io_req->cb_arg);
			io_req->cb_arg = NULL;
		}
		kref_put(&io_req->refcount, qedf_release_cmd);
		break;
	case QEDF_SEQ_CLEANUP:
		QEDF_ERR(&(qedf->dbg_ctx), "Sequence cleanup timeout, "
		    "xid=0x%x.\n", io_req->xid);
		qedf_initiate_cleanup(io_req, true);
		io_req->event = QEDF_IOREQ_EV_ELS_TMO;
		qedf_process_seq_cleanup_compl(qedf, NULL, io_req);
		break;
	default:
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "Hit default case, xid=0x%x.\n", io_req->xid);
		break;
	}
}

void qedf_cmd_mgr_free(struct qedf_cmd_mgr *cmgr)
{
	struct io_bdt *bdt_info;
	struct qedf_ctx *qedf = cmgr->qedf;
	size_t bd_tbl_sz;
	u16 min_xid = 0;
	u16 max_xid = (FCOE_PARAMS_NUM_TASKS - 1);
	int num_ios;
	int i;
	struct qedf_ioreq *io_req;

	num_ios = max_xid - min_xid + 1;

	/* Free fcoe_bdt_ctx structures */
	if (!cmgr->io_bdt_pool) {
		QEDF_ERR(&qedf->dbg_ctx, "io_bdt_pool is NULL.\n");
		goto free_cmd_pool;
	}

	bd_tbl_sz = QEDF_MAX_BDS_PER_CMD * sizeof(struct scsi_sge);
	for (i = 0; i < num_ios; i++) {
		bdt_info = cmgr->io_bdt_pool[i];
		if (bdt_info->bd_tbl) {
			dma_free_coherent(&qedf->pdev->dev, bd_tbl_sz,
			    bdt_info->bd_tbl, bdt_info->bd_tbl_dma);
			bdt_info->bd_tbl = NULL;
		}
	}

	/* Destroy io_bdt pool */
	for (i = 0; i < num_ios; i++) {
		kfree(cmgr->io_bdt_pool[i]);
		cmgr->io_bdt_pool[i] = NULL;
	}

	kfree(cmgr->io_bdt_pool);
	cmgr->io_bdt_pool = NULL;

free_cmd_pool:

	for (i = 0; i < num_ios; i++) {
		io_req = &cmgr->cmds[i];
		kfree(io_req->sgl_task_params);
		kfree(io_req->task_params);
		/* Make sure we free per command sense buffer */
		if (io_req->sense_buffer)
			dma_free_coherent(&qedf->pdev->dev,
			    QEDF_SCSI_SENSE_BUFFERSIZE, io_req->sense_buffer,
			    io_req->sense_buffer_dma);
		cancel_delayed_work_sync(&io_req->rrq_work);
	}

	/* Free command manager itself */
	vfree(cmgr);
}

static void qedf_handle_rrq(struct work_struct *work)
{
	struct qedf_ioreq *io_req =
	    container_of(work, struct qedf_ioreq, rrq_work.work);

	atomic_set(&io_req->state, QEDFC_CMD_ST_RRQ_ACTIVE);
	qedf_send_rrq(io_req);

}

struct qedf_cmd_mgr *qedf_cmd_mgr_alloc(struct qedf_ctx *qedf)
{
	struct qedf_cmd_mgr *cmgr;
	struct io_bdt *bdt_info;
	struct qedf_ioreq *io_req;
	u16 xid;
	int i;
	int num_ios;
	u16 min_xid = 0;
	u16 max_xid = (FCOE_PARAMS_NUM_TASKS - 1);

	/* Make sure num_queues is already set before calling this function */
	if (!qedf->num_queues) {
		QEDF_ERR(&(qedf->dbg_ctx), "num_queues is not set.\n");
		return NULL;
	}

	if (max_xid <= min_xid || max_xid == FC_XID_UNKNOWN) {
		QEDF_WARN(&(qedf->dbg_ctx), "Invalid min_xid 0x%x and "
			   "max_xid 0x%x.\n", min_xid, max_xid);
		return NULL;
	}

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC, "min xid 0x%x, max xid "
		   "0x%x.\n", min_xid, max_xid);

	num_ios = max_xid - min_xid + 1;

	cmgr = vzalloc(sizeof(struct qedf_cmd_mgr));
	if (!cmgr) {
		QEDF_WARN(&(qedf->dbg_ctx), "Failed to alloc cmd mgr.\n");
		return NULL;
	}

	cmgr->qedf = qedf;
	spin_lock_init(&cmgr->lock);

	/*
	 * Initialize I/O request fields.
	 */
	xid = 0;

	for (i = 0; i < num_ios; i++) {
		io_req = &cmgr->cmds[i];
		INIT_DELAYED_WORK(&io_req->timeout_work, qedf_cmd_timeout);

		io_req->xid = xid++;

		INIT_DELAYED_WORK(&io_req->rrq_work, qedf_handle_rrq);

		/* Allocate DMA memory to hold sense buffer */
		io_req->sense_buffer = dma_alloc_coherent(&qedf->pdev->dev,
		    QEDF_SCSI_SENSE_BUFFERSIZE, &io_req->sense_buffer_dma,
		    GFP_KERNEL);
		if (!io_req->sense_buffer) {
			QEDF_ERR(&qedf->dbg_ctx,
				 "Failed to alloc sense buffer.\n");
			goto mem_err;
		}

		/* Allocate task parameters to pass to f/w init funcions */
		io_req->task_params = kzalloc(sizeof(*io_req->task_params),
					      GFP_KERNEL);
		if (!io_req->task_params) {
			QEDF_ERR(&(qedf->dbg_ctx),
				 "Failed to allocate task_params for xid=0x%x\n",
				 i);
			goto mem_err;
		}

		/*
		 * Allocate scatter/gather list info to pass to f/w init
		 * functions.
		 */
		io_req->sgl_task_params = kzalloc(
		    sizeof(struct scsi_sgl_task_params), GFP_KERNEL);
		if (!io_req->sgl_task_params) {
			QEDF_ERR(&(qedf->dbg_ctx),
				 "Failed to allocate sgl_task_params for xid=0x%x\n",
				 i);
			goto mem_err;
		}
	}

	/* Allocate pool of io_bdts - one for each qedf_ioreq */
	cmgr->io_bdt_pool = kmalloc_array(num_ios, sizeof(struct io_bdt *),
	    GFP_KERNEL);

	if (!cmgr->io_bdt_pool) {
		QEDF_WARN(&(qedf->dbg_ctx), "Failed to alloc io_bdt_pool.\n");
		goto mem_err;
	}

	for (i = 0; i < num_ios; i++) {
		cmgr->io_bdt_pool[i] = kmalloc(sizeof(struct io_bdt),
		    GFP_KERNEL);
		if (!cmgr->io_bdt_pool[i]) {
			QEDF_WARN(&(qedf->dbg_ctx),
				  "Failed to alloc io_bdt_pool[%d].\n", i);
			goto mem_err;
		}
	}

	for (i = 0; i < num_ios; i++) {
		bdt_info = cmgr->io_bdt_pool[i];
		bdt_info->bd_tbl = dma_alloc_coherent(&qedf->pdev->dev,
		    QEDF_MAX_BDS_PER_CMD * sizeof(struct scsi_sge),
		    &bdt_info->bd_tbl_dma, GFP_KERNEL);
		if (!bdt_info->bd_tbl) {
			QEDF_WARN(&(qedf->dbg_ctx),
				  "Failed to alloc bdt_tbl[%d].\n", i);
			goto mem_err;
		}
	}
	atomic_set(&cmgr->free_list_cnt, num_ios);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
	    "cmgr->free_list_cnt=%d.\n",
	    atomic_read(&cmgr->free_list_cnt));

	return cmgr;

mem_err:
	qedf_cmd_mgr_free(cmgr);
	return NULL;
}

struct qedf_ioreq *qedf_alloc_cmd(struct qedf_rport *fcport, u8 cmd_type)
{
	struct qedf_ctx *qedf = fcport->qedf;
	struct qedf_cmd_mgr *cmd_mgr = qedf->cmd_mgr;
	struct qedf_ioreq *io_req = NULL;
	struct io_bdt *bd_tbl;
	u16 xid;
	uint32_t free_sqes;
	int i;
	unsigned long flags;

	free_sqes = atomic_read(&fcport->free_sqes);

	if (!free_sqes) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Returning NULL, free_sqes=%d.\n ",
		    free_sqes);
		goto out_failed;
	}

	/* Limit the number of outstanding R/W tasks */
	if ((atomic_read(&fcport->num_active_ios) >=
	    NUM_RW_TASKS_PER_CONNECTION)) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Returning NULL, num_active_ios=%d.\n",
		    atomic_read(&fcport->num_active_ios));
		goto out_failed;
	}

	/* Limit global TIDs certain tasks */
	if (atomic_read(&cmd_mgr->free_list_cnt) <= GBL_RSVD_TASKS) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Returning NULL, free_list_cnt=%d.\n",
		    atomic_read(&cmd_mgr->free_list_cnt));
		goto out_failed;
	}

	spin_lock_irqsave(&cmd_mgr->lock, flags);
	for (i = 0; i < FCOE_PARAMS_NUM_TASKS; i++) {
		io_req = &cmd_mgr->cmds[cmd_mgr->idx];
		cmd_mgr->idx++;
		if (cmd_mgr->idx == FCOE_PARAMS_NUM_TASKS)
			cmd_mgr->idx = 0;

		/* Check to make sure command was previously freed */
		if (!io_req->alloc)
			break;
	}

	if (i == FCOE_PARAMS_NUM_TASKS) {
		spin_unlock_irqrestore(&cmd_mgr->lock, flags);
		goto out_failed;
	}

	if (test_bit(QEDF_CMD_DIRTY, &io_req->flags))
		QEDF_ERR(&qedf->dbg_ctx,
			 "io_req found to be dirty ox_id = 0x%x.\n",
			 io_req->xid);

	/* Clear any flags now that we've reallocated the xid */
	io_req->flags = 0;
	io_req->alloc = 1;
	spin_unlock_irqrestore(&cmd_mgr->lock, flags);

	atomic_inc(&fcport->num_active_ios);
	atomic_dec(&fcport->free_sqes);
	xid = io_req->xid;
	atomic_dec(&cmd_mgr->free_list_cnt);

	io_req->cmd_mgr = cmd_mgr;
	io_req->fcport = fcport;

	/* Clear any stale sc_cmd back pointer */
	io_req->sc_cmd = NULL;
	io_req->lun = -1;

	/* Hold the io_req against deletion */
	kref_init(&io_req->refcount);	/* ID: 001 */
	atomic_set(&io_req->state, QEDFC_CMD_ST_IO_ACTIVE);

	/* Bind io_bdt for this io_req */
	/* Have a static link between io_req and io_bdt_pool */
	bd_tbl = io_req->bd_tbl = cmd_mgr->io_bdt_pool[xid];
	if (bd_tbl == NULL) {
		QEDF_ERR(&(qedf->dbg_ctx), "bd_tbl is NULL, xid=%x.\n", xid);
		kref_put(&io_req->refcount, qedf_release_cmd);
		goto out_failed;
	}
	bd_tbl->io_req = io_req;
	io_req->cmd_type = cmd_type;
	io_req->tm_flags = 0;

	/* Reset sequence offset data */
	io_req->rx_buf_off = 0;
	io_req->tx_buf_off = 0;
	io_req->rx_id = 0xffff; /* No OX_ID */

	return io_req;

out_failed:
	/* Record failure for stats and return NULL to caller */
	qedf->alloc_failures++;
	return NULL;
}

static void qedf_free_mp_resc(struct qedf_ioreq *io_req)
{
	struct qedf_mp_req *mp_req = &(io_req->mp_req);
	struct qedf_ctx *qedf = io_req->fcport->qedf;
	uint64_t sz = sizeof(struct scsi_sge);

	/* clear tm flags */
	if (mp_req->mp_req_bd) {
		dma_free_coherent(&qedf->pdev->dev, sz,
		    mp_req->mp_req_bd, mp_req->mp_req_bd_dma);
		mp_req->mp_req_bd = NULL;
	}
	if (mp_req->mp_resp_bd) {
		dma_free_coherent(&qedf->pdev->dev, sz,
		    mp_req->mp_resp_bd, mp_req->mp_resp_bd_dma);
		mp_req->mp_resp_bd = NULL;
	}
	if (mp_req->req_buf) {
		dma_free_coherent(&qedf->pdev->dev, QEDF_PAGE_SIZE,
		    mp_req->req_buf, mp_req->req_buf_dma);
		mp_req->req_buf = NULL;
	}
	if (mp_req->resp_buf) {
		dma_free_coherent(&qedf->pdev->dev, QEDF_PAGE_SIZE,
		    mp_req->resp_buf, mp_req->resp_buf_dma);
		mp_req->resp_buf = NULL;
	}
}

void qedf_release_cmd(struct kref *ref)
{
	struct qedf_ioreq *io_req =
	    container_of(ref, struct qedf_ioreq, refcount);
	struct qedf_cmd_mgr *cmd_mgr = io_req->cmd_mgr;
	struct qedf_rport *fcport = io_req->fcport;
	unsigned long flags;

	if (io_req->cmd_type == QEDF_SCSI_CMD) {
		QEDF_WARN(&fcport->qedf->dbg_ctx,
			  "Cmd released called without scsi_done called, io_req %p xid=0x%x.\n",
			  io_req, io_req->xid);
		WARN_ON(io_req->sc_cmd);
	}

	if (io_req->cmd_type == QEDF_ELS ||
	    io_req->cmd_type == QEDF_TASK_MGMT_CMD)
		qedf_free_mp_resc(io_req);

	atomic_inc(&cmd_mgr->free_list_cnt);
	atomic_dec(&fcport->num_active_ios);
	atomic_set(&io_req->state, QEDF_CMD_ST_INACTIVE);
	if (atomic_read(&fcport->num_active_ios) < 0) {
		QEDF_WARN(&(fcport->qedf->dbg_ctx), "active_ios < 0.\n");
		WARN_ON(1);
	}

	/* Increment task retry identifier now that the request is released */
	io_req->task_retry_identifier++;
	io_req->fcport = NULL;

	clear_bit(QEDF_CMD_DIRTY, &io_req->flags);
	io_req->cpu = 0;
	spin_lock_irqsave(&cmd_mgr->lock, flags);
	io_req->fcport = NULL;
	io_req->alloc = 0;
	spin_unlock_irqrestore(&cmd_mgr->lock, flags);
}

static int qedf_map_sg(struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc = io_req->sc_cmd;
	struct Scsi_Host *host = sc->device->host;
	struct fc_lport *lport = shost_priv(host);
	struct qedf_ctx *qedf = lport_priv(lport);
	struct scsi_sge *bd = io_req->bd_tbl->bd_tbl;
	struct scatterlist *sg;
	int byte_count = 0;
	int sg_count = 0;
	int bd_count = 0;
	u32 sg_len;
	u64 addr;
	int i = 0;

	sg_count = dma_map_sg(&qedf->pdev->dev, scsi_sglist(sc),
	    scsi_sg_count(sc), sc->sc_data_direction);
	sg = scsi_sglist(sc);

	io_req->sge_type = QEDF_IOREQ_UNKNOWN_SGE;

	if (sg_count <= 8 || io_req->io_req_flags == QEDF_READ)
		io_req->sge_type = QEDF_IOREQ_FAST_SGE;

	scsi_for_each_sg(sc, sg, sg_count, i) {
		sg_len = (u32)sg_dma_len(sg);
		addr = (u64)sg_dma_address(sg);

		/*
		 * Intermediate s/g element so check if start address
		 * is page aligned.  Only required for writes and only if the
		 * number of scatter/gather elements is 8 or more.
		 */
		if (io_req->sge_type == QEDF_IOREQ_UNKNOWN_SGE && (i) &&
		    (i != (sg_count - 1)) && sg_len < QEDF_PAGE_SIZE)
			io_req->sge_type = QEDF_IOREQ_SLOW_SGE;

		bd[bd_count].sge_addr.lo = cpu_to_le32(U64_LO(addr));
		bd[bd_count].sge_addr.hi  = cpu_to_le32(U64_HI(addr));
		bd[bd_count].sge_len = cpu_to_le32(sg_len);

		bd_count++;
		byte_count += sg_len;
	}

	/* To catch a case where FAST and SLOW nothing is set, set FAST */
	if (io_req->sge_type == QEDF_IOREQ_UNKNOWN_SGE)
		io_req->sge_type = QEDF_IOREQ_FAST_SGE;

	if (byte_count != scsi_bufflen(sc))
		QEDF_ERR(&(qedf->dbg_ctx), "byte_count = %d != "
			  "scsi_bufflen = %d, task_id = 0x%x.\n", byte_count,
			   scsi_bufflen(sc), io_req->xid);

	return bd_count;
}

static int qedf_build_bd_list_from_sg(struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc = io_req->sc_cmd;
	struct scsi_sge *bd = io_req->bd_tbl->bd_tbl;
	int bd_count;

	if (scsi_sg_count(sc)) {
		bd_count = qedf_map_sg(io_req);
		if (bd_count == 0)
			return -ENOMEM;
	} else {
		bd_count = 0;
		bd[0].sge_addr.lo = bd[0].sge_addr.hi = 0;
		bd[0].sge_len = 0;
	}
	io_req->bd_tbl->bd_valid = bd_count;

	return 0;
}

static void qedf_build_fcp_cmnd(struct qedf_ioreq *io_req,
				  struct fcp_cmnd *fcp_cmnd)
{
	struct scsi_cmnd *sc_cmd = io_req->sc_cmd;

	/* fcp_cmnd is 32 bytes */
	memset(fcp_cmnd, 0, FCP_CMND_LEN);

	/* 8 bytes: SCSI LUN info */
	int_to_scsilun(sc_cmd->device->lun,
			(struct scsi_lun *)&fcp_cmnd->fc_lun);

	/* 4 bytes: flag info */
	fcp_cmnd->fc_pri_ta = 0;
	fcp_cmnd->fc_tm_flags = io_req->tm_flags;
	fcp_cmnd->fc_flags = io_req->io_req_flags;
	fcp_cmnd->fc_cmdref = 0;

	/* Populate data direction */
	if (io_req->cmd_type == QEDF_TASK_MGMT_CMD) {
		fcp_cmnd->fc_flags |= FCP_CFL_RDDATA;
	} else {
		if (sc_cmd->sc_data_direction == DMA_TO_DEVICE)
			fcp_cmnd->fc_flags |= FCP_CFL_WRDATA;
		else if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE)
			fcp_cmnd->fc_flags |= FCP_CFL_RDDATA;
	}

	fcp_cmnd->fc_pri_ta = FCP_PTA_SIMPLE;

	/* 16 bytes: CDB information */
	if (io_req->cmd_type != QEDF_TASK_MGMT_CMD)
		memcpy(fcp_cmnd->fc_cdb, sc_cmd->cmnd, sc_cmd->cmd_len);

	/* 4 bytes: FCP data length */
	fcp_cmnd->fc_dl = htonl(io_req->data_xfer_len);
}

static void  qedf_init_task(struct qedf_rport *fcport, struct fc_lport *lport,
	struct qedf_ioreq *io_req, struct fcoe_task_context *task_ctx,
	struct fcoe_wqe *sqe)
{
	enum fcoe_task_type task_type;
	struct scsi_cmnd *sc_cmd = io_req->sc_cmd;
	struct io_bdt *bd_tbl = io_req->bd_tbl;
	u8 fcp_cmnd[32];
	u32 tmp_fcp_cmnd[8];
	int bd_count = 0;
	struct qedf_ctx *qedf = fcport->qedf;
	uint16_t cq_idx = smp_processor_id() % qedf->num_queues;
	struct regpair sense_data_buffer_phys_addr;
	u32 tx_io_size = 0;
	u32 rx_io_size = 0;
	int i, cnt;

	/* Note init_initiator_rw_fcoe_task memsets the task context */
	io_req->task = task_ctx;
	memset(task_ctx, 0, sizeof(struct fcoe_task_context));
	memset(io_req->task_params, 0, sizeof(struct fcoe_task_params));
	memset(io_req->sgl_task_params, 0, sizeof(struct scsi_sgl_task_params));

	/* Set task type bassed on DMA directio of command */
	if (io_req->cmd_type == QEDF_TASK_MGMT_CMD) {
		task_type = FCOE_TASK_TYPE_READ_INITIATOR;
	} else {
		if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
			task_type = FCOE_TASK_TYPE_WRITE_INITIATOR;
			tx_io_size = io_req->data_xfer_len;
		} else {
			task_type = FCOE_TASK_TYPE_READ_INITIATOR;
			rx_io_size = io_req->data_xfer_len;
		}
	}

	/* Setup the fields for fcoe_task_params */
	io_req->task_params->context = task_ctx;
	io_req->task_params->sqe = sqe;
	io_req->task_params->task_type = task_type;
	io_req->task_params->tx_io_size = tx_io_size;
	io_req->task_params->rx_io_size = rx_io_size;
	io_req->task_params->conn_cid = fcport->fw_cid;
	io_req->task_params->itid = io_req->xid;
	io_req->task_params->cq_rss_number = cq_idx;
	io_req->task_params->is_tape_device = fcport->dev_type;

	/* Fill in information for scatter/gather list */
	if (io_req->cmd_type != QEDF_TASK_MGMT_CMD) {
		bd_count = bd_tbl->bd_valid;
		io_req->sgl_task_params->sgl = bd_tbl->bd_tbl;
		io_req->sgl_task_params->sgl_phys_addr.lo =
			U64_LO(bd_tbl->bd_tbl_dma);
		io_req->sgl_task_params->sgl_phys_addr.hi =
			U64_HI(bd_tbl->bd_tbl_dma);
		io_req->sgl_task_params->num_sges = bd_count;
		io_req->sgl_task_params->total_buffer_size =
		    scsi_bufflen(io_req->sc_cmd);
		if (io_req->sge_type == QEDF_IOREQ_SLOW_SGE)
			io_req->sgl_task_params->small_mid_sge = 1;
		else
			io_req->sgl_task_params->small_mid_sge = 0;
	}

	/* Fill in physical address of sense buffer */
	sense_data_buffer_phys_addr.lo = U64_LO(io_req->sense_buffer_dma);
	sense_data_buffer_phys_addr.hi = U64_HI(io_req->sense_buffer_dma);

	/* fill FCP_CMND IU */
	qedf_build_fcp_cmnd(io_req, (struct fcp_cmnd *)tmp_fcp_cmnd);

	/* Swap fcp_cmnd since FC is big endian */
	cnt = sizeof(struct fcp_cmnd) / sizeof(u32);
	for (i = 0; i < cnt; i++) {
		tmp_fcp_cmnd[i] = cpu_to_be32(tmp_fcp_cmnd[i]);
	}
	memcpy(fcp_cmnd, tmp_fcp_cmnd, sizeof(struct fcp_cmnd));

	init_initiator_rw_fcoe_task(io_req->task_params,
				    io_req->sgl_task_params,
				    sense_data_buffer_phys_addr,
				    io_req->task_retry_identifier, fcp_cmnd);

	/* Increment SGL type counters */
	if (io_req->sge_type == QEDF_IOREQ_SLOW_SGE)
		qedf->slow_sge_ios++;
	else
		qedf->fast_sge_ios++;
}

void qedf_init_mp_task(struct qedf_ioreq *io_req,
	struct fcoe_task_context *task_ctx, struct fcoe_wqe *sqe)
{
	struct qedf_mp_req *mp_req = &(io_req->mp_req);
	struct qedf_rport *fcport = io_req->fcport;
	struct qedf_ctx *qedf = io_req->fcport->qedf;
	struct fc_frame_header *fc_hdr;
	struct fcoe_tx_mid_path_params task_fc_hdr;
	struct scsi_sgl_task_params tx_sgl_task_params;
	struct scsi_sgl_task_params rx_sgl_task_params;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC,
		  "Initializing MP task for cmd_type=%d\n",
		  io_req->cmd_type);

	qedf->control_requests++;

	memset(&tx_sgl_task_params, 0, sizeof(struct scsi_sgl_task_params));
	memset(&rx_sgl_task_params, 0, sizeof(struct scsi_sgl_task_params));
	memset(task_ctx, 0, sizeof(struct fcoe_task_context));
	memset(&task_fc_hdr, 0, sizeof(struct fcoe_tx_mid_path_params));

	/* Setup the task from io_req for easy reference */
	io_req->task = task_ctx;

	/* Setup the fields for fcoe_task_params */
	io_req->task_params->context = task_ctx;
	io_req->task_params->sqe = sqe;
	io_req->task_params->task_type = FCOE_TASK_TYPE_MIDPATH;
	io_req->task_params->tx_io_size = io_req->data_xfer_len;
	/* rx_io_size tells the f/w how large a response buffer we have */
	io_req->task_params->rx_io_size = PAGE_SIZE;
	io_req->task_params->conn_cid = fcport->fw_cid;
	io_req->task_params->itid = io_req->xid;
	/* Return middle path commands on CQ 0 */
	io_req->task_params->cq_rss_number = 0;
	io_req->task_params->is_tape_device = fcport->dev_type;

	fc_hdr = &(mp_req->req_fc_hdr);
	/* Set OX_ID and RX_ID based on driver task id */
	fc_hdr->fh_ox_id = io_req->xid;
	fc_hdr->fh_rx_id = htons(0xffff);

	/* Set up FC header information */
	task_fc_hdr.parameter = fc_hdr->fh_parm_offset;
	task_fc_hdr.r_ctl = fc_hdr->fh_r_ctl;
	task_fc_hdr.type = fc_hdr->fh_type;
	task_fc_hdr.cs_ctl = fc_hdr->fh_cs_ctl;
	task_fc_hdr.df_ctl = fc_hdr->fh_df_ctl;
	task_fc_hdr.rx_id = fc_hdr->fh_rx_id;
	task_fc_hdr.ox_id = fc_hdr->fh_ox_id;

	/* Set up s/g list parameters for request buffer */
	tx_sgl_task_params.sgl = mp_req->mp_req_bd;
	tx_sgl_task_params.sgl_phys_addr.lo = U64_LO(mp_req->mp_req_bd_dma);
	tx_sgl_task_params.sgl_phys_addr.hi = U64_HI(mp_req->mp_req_bd_dma);
	tx_sgl_task_params.num_sges = 1;
	/* Set PAGE_SIZE for now since sg element is that size ??? */
	tx_sgl_task_params.total_buffer_size = io_req->data_xfer_len;
	tx_sgl_task_params.small_mid_sge = 0;

	/* Set up s/g list parameters for request buffer */
	rx_sgl_task_params.sgl = mp_req->mp_resp_bd;
	rx_sgl_task_params.sgl_phys_addr.lo = U64_LO(mp_req->mp_resp_bd_dma);
	rx_sgl_task_params.sgl_phys_addr.hi = U64_HI(mp_req->mp_resp_bd_dma);
	rx_sgl_task_params.num_sges = 1;
	/* Set PAGE_SIZE for now since sg element is that size ??? */
	rx_sgl_task_params.total_buffer_size = PAGE_SIZE;
	rx_sgl_task_params.small_mid_sge = 0;


	/*
	 * Last arg is 0 as previous code did not set that we wanted the
	 * fc header information.
	 */
	init_initiator_midpath_unsolicited_fcoe_task(io_req->task_params,
						     &task_fc_hdr,
						     &tx_sgl_task_params,
						     &rx_sgl_task_params, 0);
}

/* Presumed that fcport->rport_lock is held */
u16 qedf_get_sqe_idx(struct qedf_rport *fcport)
{
	uint16_t total_sqe = (fcport->sq_mem_size)/(sizeof(struct fcoe_wqe));
	u16 rval;

	rval = fcport->sq_prod_idx;

	/* Adjust ring index */
	fcport->sq_prod_idx++;
	fcport->fw_sq_prod_idx++;
	if (fcport->sq_prod_idx == total_sqe)
		fcport->sq_prod_idx = 0;

	return rval;
}

void qedf_ring_doorbell(struct qedf_rport *fcport)
{
	struct fcoe_db_data dbell = { 0 };

	dbell.agg_flags = 0;

	dbell.params |= DB_DEST_XCM << FCOE_DB_DATA_DEST_SHIFT;
	dbell.params |= DB_AGG_CMD_SET << FCOE_DB_DATA_AGG_CMD_SHIFT;
	dbell.params |= DQ_XCM_FCOE_SQ_PROD_CMD <<
	    FCOE_DB_DATA_AGG_VAL_SEL_SHIFT;

	dbell.sq_prod = fcport->fw_sq_prod_idx;
	/* wmb makes sure that the BDs data is updated before updating the
	 * producer, otherwise FW may read old data from the BDs.
	 */
	wmb();
	barrier();
	writel(*(u32 *)&dbell, fcport->p_doorbell);
	/*
	 * Fence required to flush the write combined buffer, since another
	 * CPU may write to the same doorbell address and data may be lost
	 * due to relaxed order nature of write combined bar.
	 */
	wmb();
}

static void qedf_trace_io(struct qedf_rport *fcport, struct qedf_ioreq *io_req,
			  int8_t direction)
{
	struct qedf_ctx *qedf = fcport->qedf;
	struct qedf_io_log *io_log;
	struct scsi_cmnd *sc_cmd = io_req->sc_cmd;
	unsigned long flags;
	uint8_t op;

	spin_lock_irqsave(&qedf->io_trace_lock, flags);

	io_log = &qedf->io_trace_buf[qedf->io_trace_idx];
	io_log->direction = direction;
	io_log->task_id = io_req->xid;
	io_log->port_id = fcport->rdata->ids.port_id;
	io_log->lun = sc_cmd->device->lun;
	io_log->op = op = sc_cmd->cmnd[0];
	io_log->lba[0] = sc_cmd->cmnd[2];
	io_log->lba[1] = sc_cmd->cmnd[3];
	io_log->lba[2] = sc_cmd->cmnd[4];
	io_log->lba[3] = sc_cmd->cmnd[5];
	io_log->bufflen = scsi_bufflen(sc_cmd);
	io_log->sg_count = scsi_sg_count(sc_cmd);
	io_log->result = sc_cmd->result;
	io_log->jiffies = jiffies;
	io_log->refcount = kref_read(&io_req->refcount);

	if (direction == QEDF_IO_TRACE_REQ) {
		/* For requests we only care abot the submission CPU */
		io_log->req_cpu = io_req->cpu;
		io_log->int_cpu = 0;
		io_log->rsp_cpu = 0;
	} else if (direction == QEDF_IO_TRACE_RSP) {
		io_log->req_cpu = io_req->cpu;
		io_log->int_cpu = io_req->int_cpu;
		io_log->rsp_cpu = smp_processor_id();
	}

	io_log->sge_type = io_req->sge_type;

	qedf->io_trace_idx++;
	if (qedf->io_trace_idx == QEDF_IO_TRACE_SIZE)
		qedf->io_trace_idx = 0;

	spin_unlock_irqrestore(&qedf->io_trace_lock, flags);
}

int qedf_post_io_req(struct qedf_rport *fcport, struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc_cmd = io_req->sc_cmd;
	struct Scsi_Host *host = sc_cmd->device->host;
	struct fc_lport *lport = shost_priv(host);
	struct qedf_ctx *qedf = lport_priv(lport);
	struct fcoe_task_context *task_ctx;
	u16 xid;
	struct fcoe_wqe *sqe;
	u16 sqe_idx;

	/* Initialize rest of io_req fileds */
	io_req->data_xfer_len = scsi_bufflen(sc_cmd);
	sc_cmd->SCp.ptr = (char *)io_req;
	io_req->sge_type = QEDF_IOREQ_FAST_SGE; /* Assume fast SGL by default */

	/* Record which cpu this request is associated with */
	io_req->cpu = smp_processor_id();

	if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE) {
		io_req->io_req_flags = QEDF_READ;
		qedf->input_requests++;
	} else if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
		io_req->io_req_flags = QEDF_WRITE;
		qedf->output_requests++;
	} else {
		io_req->io_req_flags = 0;
		qedf->control_requests++;
	}

	xid = io_req->xid;

	/* Build buffer descriptor list for firmware from sg list */
	if (qedf_build_bd_list_from_sg(io_req)) {
		QEDF_ERR(&(qedf->dbg_ctx), "BD list creation failed.\n");
		/* Release cmd will release io_req, but sc_cmd is assigned */
		io_req->sc_cmd = NULL;
		kref_put(&io_req->refcount, qedf_release_cmd);
		return -EAGAIN;
	}

	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags) ||
	    test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "Session not offloaded yet.\n");
		/* Release cmd will release io_req, but sc_cmd is assigned */
		io_req->sc_cmd = NULL;
		kref_put(&io_req->refcount, qedf_release_cmd);
		return -EINVAL;
	}

	/* Record LUN number for later use if we neeed them */
	io_req->lun = (int)sc_cmd->device->lun;

	/* Obtain free SQE */
	sqe_idx = qedf_get_sqe_idx(fcport);
	sqe = &fcport->sq[sqe_idx];
	memset(sqe, 0, sizeof(struct fcoe_wqe));

	/* Get the task context */
	task_ctx = qedf_get_task_mem(&qedf->tasks, xid);
	if (!task_ctx) {
		QEDF_WARN(&(qedf->dbg_ctx), "task_ctx is NULL, xid=%d.\n",
			   xid);
		/* Release cmd will release io_req, but sc_cmd is assigned */
		io_req->sc_cmd = NULL;
		kref_put(&io_req->refcount, qedf_release_cmd);
		return -EINVAL;
	}

	qedf_init_task(fcport, lport, io_req, task_ctx, sqe);

	/* Ring doorbell */
	qedf_ring_doorbell(fcport);

	/* Set that command is with the firmware now */
	set_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);

	if (qedf_io_tracing && io_req->sc_cmd)
		qedf_trace_io(fcport, io_req, QEDF_IO_TRACE_REQ);

	return false;
}

int
qedf_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *sc_cmd)
{
	struct fc_lport *lport = shost_priv(host);
	struct qedf_ctx *qedf = lport_priv(lport);
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct qedf_rport *fcport;
	struct qedf_ioreq *io_req;
	int rc = 0;
	int rval;
	unsigned long flags = 0;
	int num_sgs = 0;

	num_sgs = scsi_sg_count(sc_cmd);
	if (scsi_sg_count(sc_cmd) > QEDF_MAX_BDS_PER_CMD) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "Number of SG elements %d exceeds what hardware limitation of %d.\n",
			 num_sgs, QEDF_MAX_BDS_PER_CMD);
		sc_cmd->result = DID_ERROR;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	if (test_bit(QEDF_UNLOADING, &qedf->flags) ||
	    test_bit(QEDF_DBG_STOP_IO, &qedf->flags)) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "Returning DNC as unloading or stop io, flags 0x%lx.\n",
			  qedf->flags);
		sc_cmd->result = DID_NO_CONNECT << 16;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	if (!qedf->pdev->msix_enabled) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Completing sc_cmd=%p DID_NO_CONNECT as MSI-X is not enabled.\n",
		    sc_cmd);
		sc_cmd->result = DID_NO_CONNECT << 16;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "fc_remote_port_chkready failed=0x%x for port_id=0x%06x.\n",
			  rval, rport->port_id);
		sc_cmd->result = rval;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	/* Retry command if we are doing a qed drain operation */
	if (test_bit(QEDF_DRAIN_ACTIVE, &qedf->flags)) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO, "Drain active.\n");
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto exit_qcmd;
	}

	if (lport->state != LPORT_ST_READY ||
	    atomic_read(&qedf->link_state) != QEDF_LINK_UP) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO, "Link down.\n");
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto exit_qcmd;
	}

	/* rport and tgt are allocated together, so tgt should be non-NULL */
	fcport = (struct qedf_rport *)&rp[1];

	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags) ||
	    test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		/*
		 * Session is not offloaded yet. Let SCSI-ml retry
		 * the command.
		 */
		rc = SCSI_MLQUEUE_TARGET_BUSY;
		goto exit_qcmd;
	}

	atomic_inc(&fcport->ios_to_queue);

	if (fcport->retry_delay_timestamp) {
		/* Take fcport->rport_lock for resetting the delay_timestamp */
		spin_lock_irqsave(&fcport->rport_lock, flags);
		if (time_after(jiffies, fcport->retry_delay_timestamp)) {
			fcport->retry_delay_timestamp = 0;
		} else {
			spin_unlock_irqrestore(&fcport->rport_lock, flags);
			/* If retry_delay timer is active, flow off the ML */
			rc = SCSI_MLQUEUE_TARGET_BUSY;
			atomic_dec(&fcport->ios_to_queue);
			goto exit_qcmd;
		}
		spin_unlock_irqrestore(&fcport->rport_lock, flags);
	}

	io_req = qedf_alloc_cmd(fcport, QEDF_SCSI_CMD);
	if (!io_req) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		atomic_dec(&fcport->ios_to_queue);
		goto exit_qcmd;
	}

	io_req->sc_cmd = sc_cmd;

	/* Take fcport->rport_lock for posting to fcport send queue */
	spin_lock_irqsave(&fcport->rport_lock, flags);
	if (qedf_post_io_req(fcport, io_req)) {
		QEDF_WARN(&(qedf->dbg_ctx), "Unable to post io_req\n");
		/* Return SQE to pool */
		atomic_inc(&fcport->free_sqes);
		rc = SCSI_MLQUEUE_HOST_BUSY;
	}
	spin_unlock_irqrestore(&fcport->rport_lock, flags);
	atomic_dec(&fcport->ios_to_queue);

exit_qcmd:
	return rc;
}

static void qedf_parse_fcp_rsp(struct qedf_ioreq *io_req,
				 struct fcoe_cqe_rsp_info *fcp_rsp)
{
	struct scsi_cmnd *sc_cmd = io_req->sc_cmd;
	struct qedf_ctx *qedf = io_req->fcport->qedf;
	u8 rsp_flags = fcp_rsp->rsp_flags.flags;
	int fcp_sns_len = 0;
	int fcp_rsp_len = 0;
	uint8_t *rsp_info, *sense_data;

	io_req->fcp_status = FC_GOOD;
	io_req->fcp_resid = 0;
	if (rsp_flags & (FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER |
	    FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER))
		io_req->fcp_resid = fcp_rsp->fcp_resid;

	io_req->scsi_comp_flags = rsp_flags;
	CMD_SCSI_STATUS(sc_cmd) = io_req->cdb_status =
	    fcp_rsp->scsi_status_code;

	if (rsp_flags &
	    FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID)
		fcp_rsp_len = fcp_rsp->fcp_rsp_len;

	if (rsp_flags &
	    FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID)
		fcp_sns_len = fcp_rsp->fcp_sns_len;

	io_req->fcp_rsp_len = fcp_rsp_len;
	io_req->fcp_sns_len = fcp_sns_len;
	rsp_info = sense_data = io_req->sense_buffer;

	/* fetch fcp_rsp_code */
	if ((fcp_rsp_len == 4) || (fcp_rsp_len == 8)) {
		/* Only for task management function */
		io_req->fcp_rsp_code = rsp_info[3];
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "fcp_rsp_code = %d\n", io_req->fcp_rsp_code);
		/* Adjust sense-data location. */
		sense_data += fcp_rsp_len;
	}

	if (fcp_sns_len > SCSI_SENSE_BUFFERSIZE) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Truncating sense buffer\n");
		fcp_sns_len = SCSI_SENSE_BUFFERSIZE;
	}

	/* The sense buffer can be NULL for TMF commands */
	if (sc_cmd->sense_buffer) {
		memset(sc_cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		if (fcp_sns_len)
			memcpy(sc_cmd->sense_buffer, sense_data,
			    fcp_sns_len);
	}
}

static void qedf_unmap_sg_list(struct qedf_ctx *qedf, struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc = io_req->sc_cmd;

	if (io_req->bd_tbl->bd_valid && sc && scsi_sg_count(sc)) {
		dma_unmap_sg(&qedf->pdev->dev, scsi_sglist(sc),
		    scsi_sg_count(sc), sc->sc_data_direction);
		io_req->bd_tbl->bd_valid = 0;
	}
}

void qedf_scsi_completion(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc_cmd;
	struct fcoe_cqe_rsp_info *fcp_rsp;
	struct qedf_rport *fcport;
	int refcount;
	u16 scope, qualifier = 0;
	u8 fw_residual_flag = 0;
	unsigned long flags = 0;
	u16 chk_scope = 0;

	if (!io_req)
		return;
	if (!cqe)
		return;

	if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags) ||
	    test_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags) ||
	    test_bit(QEDF_CMD_IN_ABORT, &io_req->flags)) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "io_req xid=0x%x already in cleanup or abort processing or already completed.\n",
			 io_req->xid);
		return;
	}

	sc_cmd = io_req->sc_cmd;
	fcp_rsp = &cqe->cqe_info.rsp_info;

	if (!sc_cmd) {
		QEDF_WARN(&(qedf->dbg_ctx), "sc_cmd is NULL!\n");
		return;
	}

	if (!sc_cmd->SCp.ptr) {
		QEDF_WARN(&(qedf->dbg_ctx), "SCp.ptr is NULL, returned in "
		    "another context.\n");
		return;
	}

	if (!sc_cmd->device) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "Device for sc_cmd %p is NULL.\n", sc_cmd);
		return;
	}

	if (!scsi_cmd_to_rq(sc_cmd)->q) {
		QEDF_WARN(&(qedf->dbg_ctx), "request->q is NULL so request "
		   "is not valid, sc_cmd=%p.\n", sc_cmd);
		return;
	}

	fcport = io_req->fcport;

	/*
	 * When flush is active, let the cmds be completed from the cleanup
	 * context
	 */
	if (test_bit(QEDF_RPORT_IN_TARGET_RESET, &fcport->flags) ||
	    (test_bit(QEDF_RPORT_IN_LUN_RESET, &fcport->flags) &&
	     sc_cmd->device->lun == (u64)fcport->lun_reset_lun)) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "Dropping good completion xid=0x%x as fcport is flushing",
			  io_req->xid);
		return;
	}

	qedf_parse_fcp_rsp(io_req, fcp_rsp);

	qedf_unmap_sg_list(qedf, io_req);

	/* Check for FCP transport error */
	if (io_req->fcp_rsp_len > 3 && io_req->fcp_rsp_code) {
		QEDF_ERR(&(qedf->dbg_ctx),
		    "FCP I/O protocol failure xid=0x%x fcp_rsp_len=%d "
		    "fcp_rsp_code=%d.\n", io_req->xid, io_req->fcp_rsp_len,
		    io_req->fcp_rsp_code);
		sc_cmd->result = DID_BUS_BUSY << 16;
		goto out;
	}

	fw_residual_flag = GET_FIELD(cqe->cqe_info.rsp_info.fw_error_flags,
	    FCOE_CQE_RSP_INFO_FW_UNDERRUN);
	if (fw_residual_flag) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "Firmware detected underrun: xid=0x%x fcp_rsp.flags=0x%02x fcp_resid=%d fw_residual=0x%x lba=%02x%02x%02x%02x.\n",
			 io_req->xid, fcp_rsp->rsp_flags.flags,
			 io_req->fcp_resid,
			 cqe->cqe_info.rsp_info.fw_residual, sc_cmd->cmnd[2],
			 sc_cmd->cmnd[3], sc_cmd->cmnd[4], sc_cmd->cmnd[5]);

		if (io_req->cdb_status == 0)
			sc_cmd->result = (DID_ERROR << 16) | io_req->cdb_status;
		else
			sc_cmd->result = (DID_OK << 16) | io_req->cdb_status;

		/*
		 * Set resid to the whole buffer length so we won't try to resue
		 * any previously data.
		 */
		scsi_set_resid(sc_cmd, scsi_bufflen(sc_cmd));
		goto out;
	}

	switch (io_req->fcp_status) {
	case FC_GOOD:
		if (io_req->cdb_status == 0) {
			/* Good I/O completion */
			sc_cmd->result = DID_OK << 16;
		} else {
			refcount = kref_read(&io_req->refcount);
			QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
			    "%d:0:%d:%lld xid=0x%0x op=0x%02x "
			    "lba=%02x%02x%02x%02x cdb_status=%d "
			    "fcp_resid=0x%x refcount=%d.\n",
			    qedf->lport->host->host_no, sc_cmd->device->id,
			    sc_cmd->device->lun, io_req->xid,
			    sc_cmd->cmnd[0], sc_cmd->cmnd[2], sc_cmd->cmnd[3],
			    sc_cmd->cmnd[4], sc_cmd->cmnd[5],
			    io_req->cdb_status, io_req->fcp_resid,
			    refcount);
			sc_cmd->result = (DID_OK << 16) | io_req->cdb_status;

			if (io_req->cdb_status == SAM_STAT_TASK_SET_FULL ||
			    io_req->cdb_status == SAM_STAT_BUSY) {
				/*
				 * Check whether we need to set retry_delay at
				 * all based on retry_delay module parameter
				 * and the status qualifier.
				 */

				/* Upper 2 bits */
				scope = fcp_rsp->retry_delay_timer & 0xC000;
				/* Lower 14 bits */
				qualifier = fcp_rsp->retry_delay_timer & 0x3FFF;

				if (qedf_retry_delay)
					chk_scope = 1;
				/* Record stats */
				if (io_req->cdb_status ==
				    SAM_STAT_TASK_SET_FULL)
					qedf->task_set_fulls++;
				else
					qedf->busy++;
			}
		}
		if (io_req->fcp_resid)
			scsi_set_resid(sc_cmd, io_req->fcp_resid);

		if (chk_scope == 1) {
			if ((scope == 1 || scope == 2) &&
			    (qualifier > 0 && qualifier <= 0x3FEF)) {
				/* Check we don't go over the max */
				if (qualifier > QEDF_RETRY_DELAY_MAX) {
					qualifier = QEDF_RETRY_DELAY_MAX;
					QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
						  "qualifier = %d\n",
						  (fcp_rsp->retry_delay_timer &
						  0x3FFF));
				}
				QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
					  "Scope = %d and qualifier = %d",
					  scope, qualifier);
				/*  Take fcport->rport_lock to
				 *  update the retry_delay_timestamp
				 */
				spin_lock_irqsave(&fcport->rport_lock, flags);
				fcport->retry_delay_timestamp =
					jiffies + (qualifier * HZ / 10);
				spin_unlock_irqrestore(&fcport->rport_lock,
						       flags);

			} else {
				QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
					  "combination of scope = %d and qualifier = %d is not handled in qedf.\n",
					  scope, qualifier);
			}
		}
		break;
	default:
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "fcp_status=%d.\n",
			   io_req->fcp_status);
		break;
	}

out:
	if (qedf_io_tracing)
		qedf_trace_io(fcport, io_req, QEDF_IO_TRACE_RSP);

	/*
	 * We wait till the end of the function to clear the
	 * outstanding bit in case we need to send an abort
	 */
	clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);

	io_req->sc_cmd = NULL;
	sc_cmd->SCp.ptr =  NULL;
	sc_cmd->scsi_done(sc_cmd);
	kref_put(&io_req->refcount, qedf_release_cmd);
}

/* Return a SCSI command in some other context besides a normal completion */
void qedf_scsi_done(struct qedf_ctx *qedf, struct qedf_ioreq *io_req,
	int result)
{
	struct scsi_cmnd *sc_cmd;
	int refcount;

	if (!io_req) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO, "io_req is NULL\n");
		return;
	}

	if (test_and_set_bit(QEDF_CMD_ERR_SCSI_DONE, &io_req->flags)) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "io_req:%p scsi_done handling already done\n",
			  io_req);
		return;
	}

	/*
	 * We will be done with this command after this call so clear the
	 * outstanding bit.
	 */
	clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);

	sc_cmd = io_req->sc_cmd;

	if (!sc_cmd) {
		QEDF_WARN(&(qedf->dbg_ctx), "sc_cmd is NULL!\n");
		return;
	}

	if (!virt_addr_valid(sc_cmd)) {
		QEDF_ERR(&qedf->dbg_ctx, "sc_cmd=%p is not valid.", sc_cmd);
		goto bad_scsi_ptr;
	}

	if (!sc_cmd->SCp.ptr) {
		QEDF_WARN(&(qedf->dbg_ctx), "SCp.ptr is NULL, returned in "
		    "another context.\n");
		return;
	}

	if (!sc_cmd->device) {
		QEDF_ERR(&qedf->dbg_ctx, "Device for sc_cmd %p is NULL.\n",
			 sc_cmd);
		goto bad_scsi_ptr;
	}

	if (!virt_addr_valid(sc_cmd->device)) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "Device pointer for sc_cmd %p is bad.\n", sc_cmd);
		goto bad_scsi_ptr;
	}

	if (!sc_cmd->sense_buffer) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "sc_cmd->sense_buffer for sc_cmd %p is NULL.\n",
			 sc_cmd);
		goto bad_scsi_ptr;
	}

	if (!virt_addr_valid(sc_cmd->sense_buffer)) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "sc_cmd->sense_buffer for sc_cmd %p is bad.\n",
			 sc_cmd);
		goto bad_scsi_ptr;
	}

	if (!sc_cmd->scsi_done) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "sc_cmd->scsi_done for sc_cmd %p is NULL.\n",
			 sc_cmd);
		goto bad_scsi_ptr;
	}

	qedf_unmap_sg_list(qedf, io_req);

	sc_cmd->result = result << 16;
	refcount = kref_read(&io_req->refcount);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "%d:0:%d:%lld: Completing "
	    "sc_cmd=%p result=0x%08x op=0x%02x lba=0x%02x%02x%02x%02x, "
	    "allowed=%d retries=%d refcount=%d.\n",
	    qedf->lport->host->host_no, sc_cmd->device->id,
	    sc_cmd->device->lun, sc_cmd, sc_cmd->result, sc_cmd->cmnd[0],
	    sc_cmd->cmnd[2], sc_cmd->cmnd[3], sc_cmd->cmnd[4],
	    sc_cmd->cmnd[5], sc_cmd->allowed, sc_cmd->retries,
	    refcount);

	/*
	 * Set resid to the whole buffer length so we won't try to resue any
	 * previously read data
	 */
	scsi_set_resid(sc_cmd, scsi_bufflen(sc_cmd));

	if (qedf_io_tracing)
		qedf_trace_io(io_req->fcport, io_req, QEDF_IO_TRACE_RSP);

	io_req->sc_cmd = NULL;
	sc_cmd->SCp.ptr = NULL;
	sc_cmd->scsi_done(sc_cmd);
	kref_put(&io_req->refcount, qedf_release_cmd);
	return;

bad_scsi_ptr:
	/*
	 * Clear the io_req->sc_cmd backpointer so we don't try to process
	 * this again
	 */
	io_req->sc_cmd = NULL;
	kref_put(&io_req->refcount, qedf_release_cmd);  /* ID: 001 */
}

/*
 * Handle warning type CQE completions. This is mainly used for REC timer
 * popping.
 */
void qedf_process_warning_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	int rval, i;
	struct qedf_rport *fcport = io_req->fcport;
	u64 err_warn_bit_map;
	u8 err_warn = 0xff;

	if (!cqe) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "cqe is NULL for io_req %p xid=0x%x\n",
			  io_req, io_req->xid);
		return;
	}

	QEDF_ERR(&(io_req->fcport->qedf->dbg_ctx), "Warning CQE, "
		  "xid=0x%x\n", io_req->xid);
	QEDF_ERR(&(io_req->fcport->qedf->dbg_ctx),
		  "err_warn_bitmap=%08x:%08x\n",
		  le32_to_cpu(cqe->cqe_info.err_info.err_warn_bitmap_hi),
		  le32_to_cpu(cqe->cqe_info.err_info.err_warn_bitmap_lo));
	QEDF_ERR(&(io_req->fcport->qedf->dbg_ctx), "tx_buff_off=%08x, "
		  "rx_buff_off=%08x, rx_id=%04x\n",
		  le32_to_cpu(cqe->cqe_info.err_info.tx_buf_off),
		  le32_to_cpu(cqe->cqe_info.err_info.rx_buf_off),
		  le32_to_cpu(cqe->cqe_info.err_info.rx_id));

	/* Normalize the error bitmap value to an just an unsigned int */
	err_warn_bit_map = (u64)
	    ((u64)cqe->cqe_info.err_info.err_warn_bitmap_hi << 32) |
	    (u64)cqe->cqe_info.err_info.err_warn_bitmap_lo;
	for (i = 0; i < 64; i++) {
		if (err_warn_bit_map & (u64)((u64)1 << i)) {
			err_warn = i;
			break;
		}
	}

	/* Check if REC TOV expired if this is a tape device */
	if (fcport->dev_type == QEDF_RPORT_TYPE_TAPE) {
		if (err_warn ==
		    FCOE_WARNING_CODE_REC_TOV_TIMER_EXPIRATION) {
			QEDF_ERR(&(qedf->dbg_ctx), "REC timer expired.\n");
			if (!test_bit(QEDF_CMD_SRR_SENT, &io_req->flags)) {
				io_req->rx_buf_off =
				    cqe->cqe_info.err_info.rx_buf_off;
				io_req->tx_buf_off =
				    cqe->cqe_info.err_info.tx_buf_off;
				io_req->rx_id = cqe->cqe_info.err_info.rx_id;
				rval = qedf_send_rec(io_req);
				/*
				 * We only want to abort the io_req if we
				 * can't queue the REC command as we want to
				 * keep the exchange open for recovery.
				 */
				if (rval)
					goto send_abort;
			}
			return;
		}
	}

send_abort:
	init_completion(&io_req->abts_done);
	rval = qedf_initiate_abts(io_req, true);
	if (rval)
		QEDF_ERR(&(qedf->dbg_ctx), "Failed to queue ABTS.\n");
}

/* Cleanup a command when we receive an error detection completion */
void qedf_process_error_detect(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	int rval;

	if (io_req == NULL) {
		QEDF_INFO(NULL, QEDF_LOG_IO, "io_req is NULL.\n");
		return;
	}

	if (io_req->fcport == NULL) {
		QEDF_INFO(NULL, QEDF_LOG_IO, "fcport is NULL.\n");
		return;
	}

	if (!cqe) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			"cqe is NULL for io_req %p\n", io_req);
		return;
	}

	QEDF_ERR(&(io_req->fcport->qedf->dbg_ctx), "Error detection CQE, "
		  "xid=0x%x\n", io_req->xid);
	QEDF_ERR(&(io_req->fcport->qedf->dbg_ctx),
		  "err_warn_bitmap=%08x:%08x\n",
		  le32_to_cpu(cqe->cqe_info.err_info.err_warn_bitmap_hi),
		  le32_to_cpu(cqe->cqe_info.err_info.err_warn_bitmap_lo));
	QEDF_ERR(&(io_req->fcport->qedf->dbg_ctx), "tx_buff_off=%08x, "
		  "rx_buff_off=%08x, rx_id=%04x\n",
		  le32_to_cpu(cqe->cqe_info.err_info.tx_buf_off),
		  le32_to_cpu(cqe->cqe_info.err_info.rx_buf_off),
		  le32_to_cpu(cqe->cqe_info.err_info.rx_id));

	/* When flush is active, let the cmds be flushed out from the cleanup context */
	if (test_bit(QEDF_RPORT_IN_TARGET_RESET, &io_req->fcport->flags) ||
		(test_bit(QEDF_RPORT_IN_LUN_RESET, &io_req->fcport->flags) &&
		 io_req->sc_cmd->device->lun == (u64)io_req->fcport->lun_reset_lun)) {
		QEDF_ERR(&qedf->dbg_ctx,
			"Dropping EQE for xid=0x%x as fcport is flushing",
			io_req->xid);
		return;
	}

	if (qedf->stop_io_on_error) {
		qedf_stop_all_io(qedf);
		return;
	}

	init_completion(&io_req->abts_done);
	rval = qedf_initiate_abts(io_req, true);
	if (rval)
		QEDF_ERR(&(qedf->dbg_ctx), "Failed to queue ABTS.\n");
}

static void qedf_flush_els_req(struct qedf_ctx *qedf,
	struct qedf_ioreq *els_req)
{
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
	    "Flushing ELS request xid=0x%x refcount=%d.\n", els_req->xid,
	    kref_read(&els_req->refcount));

	/*
	 * Need to distinguish this from a timeout when calling the
	 * els_req->cb_func.
	 */
	els_req->event = QEDF_IOREQ_EV_ELS_FLUSH;

	clear_bit(QEDF_CMD_OUTSTANDING, &els_req->flags);

	/* Cancel the timer */
	cancel_delayed_work_sync(&els_req->timeout_work);

	/* Call callback function to complete command */
	if (els_req->cb_func && els_req->cb_arg) {
		els_req->cb_func(els_req->cb_arg);
		els_req->cb_arg = NULL;
	}

	/* Release kref for original initiate_els */
	kref_put(&els_req->refcount, qedf_release_cmd);
}

/* A value of -1 for lun is a wild card that means flush all
 * active SCSI I/Os for the target.
 */
void qedf_flush_active_ios(struct qedf_rport *fcport, int lun)
{
	struct qedf_ioreq *io_req;
	struct qedf_ctx *qedf;
	struct qedf_cmd_mgr *cmd_mgr;
	int i, rc;
	unsigned long flags;
	int flush_cnt = 0;
	int wait_cnt = 100;
	int refcount = 0;

	if (!fcport) {
		QEDF_ERR(NULL, "fcport is NULL\n");
		return;
	}

	/* Check that fcport is still offloaded */
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(NULL, "fcport is no longer offloaded.\n");
		return;
	}

	qedf = fcport->qedf;

	if (!qedf) {
		QEDF_ERR(NULL, "qedf is NULL.\n");
		return;
	}

	/* Only wait for all commands to be queued in the Upload context */
	if (test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags) &&
	    (lun == -1)) {
		while (atomic_read(&fcport->ios_to_queue)) {
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
				  "Waiting for %d I/Os to be queued\n",
				  atomic_read(&fcport->ios_to_queue));
			if (wait_cnt == 0) {
				QEDF_ERR(NULL,
					 "%d IOs request could not be queued\n",
					 atomic_read(&fcport->ios_to_queue));
			}
			msleep(20);
			wait_cnt--;
		}
	}

	cmd_mgr = qedf->cmd_mgr;

	QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
		  "Flush active i/o's num=0x%x fcport=0x%p port_id=0x%06x scsi_id=%d.\n",
		  atomic_read(&fcport->num_active_ios), fcport,
		  fcport->rdata->ids.port_id, fcport->rport->scsi_target_id);
	QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO, "Locking flush mutex.\n");

	mutex_lock(&qedf->flush_mutex);
	if (lun == -1) {
		set_bit(QEDF_RPORT_IN_TARGET_RESET, &fcport->flags);
	} else {
		set_bit(QEDF_RPORT_IN_LUN_RESET, &fcport->flags);
		fcport->lun_reset_lun = lun;
	}

	for (i = 0; i < FCOE_PARAMS_NUM_TASKS; i++) {
		io_req = &cmd_mgr->cmds[i];

		if (!io_req)
			continue;
		if (!io_req->fcport)
			continue;

		spin_lock_irqsave(&cmd_mgr->lock, flags);

		if (io_req->alloc) {
			if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags)) {
				if (io_req->cmd_type == QEDF_SCSI_CMD)
					QEDF_ERR(&qedf->dbg_ctx,
						 "Allocated but not queued, xid=0x%x\n",
						 io_req->xid);
			}
			spin_unlock_irqrestore(&cmd_mgr->lock, flags);
		} else {
			spin_unlock_irqrestore(&cmd_mgr->lock, flags);
			continue;
		}

		if (io_req->fcport != fcport)
			continue;

		/* In case of ABTS, CMD_OUTSTANDING is cleared on ABTS response,
		 * but RRQ is still pending.
		 * Workaround: Within qedf_send_rrq, we check if the fcport is
		 * NULL, and we drop the ref on the io_req to clean it up.
		 */
		if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags)) {
			refcount = kref_read(&io_req->refcount);
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
				  "Not outstanding, xid=0x%x, cmd_type=%d refcount=%d.\n",
				  io_req->xid, io_req->cmd_type, refcount);
			/* If RRQ work has been queue, try to cancel it and
			 * free the io_req
			 */
			if (atomic_read(&io_req->state) ==
			    QEDFC_CMD_ST_RRQ_WAIT) {
				if (cancel_delayed_work_sync
				    (&io_req->rrq_work)) {
					QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
						  "Putting reference for pending RRQ work xid=0x%x.\n",
						  io_req->xid);
					/* ID: 003 */
					kref_put(&io_req->refcount,
						 qedf_release_cmd);
				}
			}
			continue;
		}

		/* Only consider flushing ELS during target reset */
		if (io_req->cmd_type == QEDF_ELS &&
		    lun == -1) {
			rc = kref_get_unless_zero(&io_req->refcount);
			if (!rc) {
				QEDF_ERR(&(qedf->dbg_ctx),
				    "Could not get kref for ELS io_req=0x%p xid=0x%x.\n",
				    io_req, io_req->xid);
				continue;
			}
			qedf_initiate_cleanup(io_req, false);
			flush_cnt++;
			qedf_flush_els_req(qedf, io_req);

			/*
			 * Release the kref and go back to the top of the
			 * loop.
			 */
			goto free_cmd;
		}

		if (io_req->cmd_type == QEDF_ABTS) {
			/* ID: 004 */
			rc = kref_get_unless_zero(&io_req->refcount);
			if (!rc) {
				QEDF_ERR(&(qedf->dbg_ctx),
				    "Could not get kref for abort io_req=0x%p xid=0x%x.\n",
				    io_req, io_req->xid);
				continue;
			}
			if (lun != -1 && io_req->lun != lun)
				goto free_cmd;

			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			    "Flushing abort xid=0x%x.\n", io_req->xid);

			if (cancel_delayed_work_sync(&io_req->rrq_work)) {
				QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
					  "Putting ref for cancelled RRQ work xid=0x%x.\n",
					  io_req->xid);
				kref_put(&io_req->refcount, qedf_release_cmd);
			}

			if (cancel_delayed_work_sync(&io_req->timeout_work)) {
				QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
					  "Putting ref for cancelled tmo work xid=0x%x.\n",
					  io_req->xid);
				qedf_initiate_cleanup(io_req, true);
				/* Notify eh_abort handler that ABTS is
				 * complete
				 */
				complete(&io_req->abts_done);
				clear_bit(QEDF_CMD_IN_ABORT, &io_req->flags);
				/* ID: 002 */
				kref_put(&io_req->refcount, qedf_release_cmd);
			}
			flush_cnt++;
			goto free_cmd;
		}

		if (!io_req->sc_cmd)
			continue;
		if (!io_req->sc_cmd->device) {
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
				  "Device backpointer NULL for sc_cmd=%p.\n",
				  io_req->sc_cmd);
			/* Put reference for non-existent scsi_cmnd */
			io_req->sc_cmd = NULL;
			qedf_initiate_cleanup(io_req, false);
			kref_put(&io_req->refcount, qedf_release_cmd);
			continue;
		}
		if (lun > -1) {
			if (io_req->lun != lun)
				continue;
		}

		/*
		 * Use kref_get_unless_zero in the unlikely case the command
		 * we're about to flush was completed in the normal SCSI path
		 */
		rc = kref_get_unless_zero(&io_req->refcount);
		if (!rc) {
			QEDF_ERR(&(qedf->dbg_ctx), "Could not get kref for "
			    "io_req=0x%p xid=0x%x\n", io_req, io_req->xid);
			continue;
		}

		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Cleanup xid=0x%x.\n", io_req->xid);
		flush_cnt++;

		/* Cleanup task and return I/O mid-layer */
		qedf_initiate_cleanup(io_req, true);

free_cmd:
		kref_put(&io_req->refcount, qedf_release_cmd);	/* ID: 004 */
	}

	wait_cnt = 60;
	QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
		  "Flushed 0x%x I/Os, active=0x%x.\n",
		  flush_cnt, atomic_read(&fcport->num_active_ios));
	/* Only wait for all commands to complete in the Upload context */
	if (test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags) &&
	    (lun == -1)) {
		while (atomic_read(&fcport->num_active_ios)) {
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
				  "Flushed 0x%x I/Os, active=0x%x cnt=%d.\n",
				  flush_cnt,
				  atomic_read(&fcport->num_active_ios),
				  wait_cnt);
			if (wait_cnt == 0) {
				QEDF_ERR(&qedf->dbg_ctx,
					 "Flushed %d I/Os, active=%d.\n",
					 flush_cnt,
					 atomic_read(&fcport->num_active_ios));
				for (i = 0; i < FCOE_PARAMS_NUM_TASKS; i++) {
					io_req = &cmd_mgr->cmds[i];
					if (io_req->fcport &&
					    io_req->fcport == fcport) {
						refcount =
						kref_read(&io_req->refcount);
						set_bit(QEDF_CMD_DIRTY,
							&io_req->flags);
						QEDF_ERR(&qedf->dbg_ctx,
							 "Outstanding io_req =%p xid=0x%x flags=0x%lx, sc_cmd=%p refcount=%d cmd_type=%d.\n",
							 io_req, io_req->xid,
							 io_req->flags,
							 io_req->sc_cmd,
							 refcount,
							 io_req->cmd_type);
					}
				}
				WARN_ON(1);
				break;
			}
			msleep(500);
			wait_cnt--;
		}
	}

	clear_bit(QEDF_RPORT_IN_LUN_RESET, &fcport->flags);
	clear_bit(QEDF_RPORT_IN_TARGET_RESET, &fcport->flags);
	QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO, "Unlocking flush mutex.\n");
	mutex_unlock(&qedf->flush_mutex);
}

/*
 * Initiate a ABTS middle path command. Note that we don't have to initialize
 * the task context for an ABTS task.
 */
int qedf_initiate_abts(struct qedf_ioreq *io_req, bool return_scsi_cmd_on_abts)
{
	struct fc_lport *lport;
	struct qedf_rport *fcport = io_req->fcport;
	struct fc_rport_priv *rdata;
	struct qedf_ctx *qedf;
	u16 xid;
	int rc = 0;
	unsigned long flags;
	struct fcoe_wqe *sqe;
	u16 sqe_idx;
	int refcount = 0;

	/* Sanity check qedf_rport before dereferencing any pointers */
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(NULL, "tgt not offloaded\n");
		rc = 1;
		goto out;
	}

	qedf = fcport->qedf;
	rdata = fcport->rdata;

	if (!rdata || !kref_get_unless_zero(&rdata->kref)) {
		QEDF_ERR(&qedf->dbg_ctx, "stale rport\n");
		rc = 1;
		goto out;
	}

	lport = qedf->lport;

	if (lport->state != LPORT_ST_READY || !(lport->link_up)) {
		QEDF_ERR(&(qedf->dbg_ctx), "link is not ready\n");
		rc = 1;
		goto drop_rdata_kref;
	}

	if (atomic_read(&qedf->link_down_tmo_valid) > 0) {
		QEDF_ERR(&(qedf->dbg_ctx), "link_down_tmo active.\n");
		rc = 1;
		goto drop_rdata_kref;
	}

	/* Ensure room on SQ */
	if (!atomic_read(&fcport->free_sqes)) {
		QEDF_ERR(&(qedf->dbg_ctx), "No SQ entries available\n");
		rc = 1;
		goto drop_rdata_kref;
	}

	if (test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		QEDF_ERR(&qedf->dbg_ctx, "fcport is uploading.\n");
		rc = 1;
		goto drop_rdata_kref;
	}

	if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags) ||
	    test_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags) ||
	    test_bit(QEDF_CMD_IN_ABORT, &io_req->flags)) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "io_req xid=0x%x sc_cmd=%p already in cleanup or abort processing or already completed.\n",
			 io_req->xid, io_req->sc_cmd);
		rc = 1;
		goto drop_rdata_kref;
	}

	kref_get(&io_req->refcount);

	xid = io_req->xid;
	qedf->control_requests++;
	qedf->packet_aborts++;

	/* Set the command type to abort */
	io_req->cmd_type = QEDF_ABTS;
	io_req->return_scsi_cmd_on_abts = return_scsi_cmd_on_abts;

	set_bit(QEDF_CMD_IN_ABORT, &io_req->flags);
	refcount = kref_read(&io_req->refcount);
	QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_SCSI_TM,
		  "ABTS io_req xid = 0x%x refcount=%d\n",
		  xid, refcount);

	qedf_cmd_timer_set(qedf, io_req, QEDF_ABORT_TIMEOUT);

	spin_lock_irqsave(&fcport->rport_lock, flags);

	sqe_idx = qedf_get_sqe_idx(fcport);
	sqe = &fcport->sq[sqe_idx];
	memset(sqe, 0, sizeof(struct fcoe_wqe));
	io_req->task_params->sqe = sqe;

	init_initiator_abort_fcoe_task(io_req->task_params);
	qedf_ring_doorbell(fcport);

	spin_unlock_irqrestore(&fcport->rport_lock, flags);

drop_rdata_kref:
	kref_put(&rdata->kref, fc_rport_destroy);
out:
	return rc;
}

void qedf_process_abts_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	uint32_t r_ctl;
	int rc;
	struct qedf_rport *fcport = io_req->fcport;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM, "Entered with xid = "
		   "0x%x cmd_type = %d\n", io_req->xid, io_req->cmd_type);

	r_ctl = cqe->cqe_info.abts_info.r_ctl;

	/* This was added at a point when we were scheduling abts_compl &
	 * cleanup_compl on different CPUs and there was a possibility of
	 * the io_req to be freed from the other context before we got here.
	 */
	if (!fcport) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "Dropping ABTS completion xid=0x%x as fcport is NULL",
			  io_req->xid);
		return;
	}

	/*
	 * When flush is active, let the cmds be completed from the cleanup
	 * context
	 */
	if (test_bit(QEDF_RPORT_IN_TARGET_RESET, &fcport->flags) ||
	    test_bit(QEDF_RPORT_IN_LUN_RESET, &fcport->flags)) {
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
			  "Dropping ABTS completion xid=0x%x as fcport is flushing",
			  io_req->xid);
		return;
	}

	if (!cancel_delayed_work(&io_req->timeout_work)) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "Wasn't able to cancel abts timeout work.\n");
	}

	switch (r_ctl) {
	case FC_RCTL_BA_ACC:
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM,
		    "ABTS response - ACC Send RRQ after R_A_TOV\n");
		io_req->event = QEDF_IOREQ_EV_ABORT_SUCCESS;
		rc = kref_get_unless_zero(&io_req->refcount);	/* ID: 003 */
		if (!rc) {
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_SCSI_TM,
				  "kref is already zero so ABTS was already completed or flushed xid=0x%x.\n",
				  io_req->xid);
			return;
		}
		/*
		 * Dont release this cmd yet. It will be relesed
		 * after we get RRQ response
		 */
		queue_delayed_work(qedf->dpc_wq, &io_req->rrq_work,
		    msecs_to_jiffies(qedf->lport->r_a_tov));
		atomic_set(&io_req->state, QEDFC_CMD_ST_RRQ_WAIT);
		break;
	/* For error cases let the cleanup return the command */
	case FC_RCTL_BA_RJT:
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM,
		   "ABTS response - RJT\n");
		io_req->event = QEDF_IOREQ_EV_ABORT_FAILED;
		break;
	default:
		QEDF_ERR(&(qedf->dbg_ctx), "Unknown ABTS response\n");
		break;
	}

	clear_bit(QEDF_CMD_IN_ABORT, &io_req->flags);

	if (io_req->sc_cmd) {
		if (!io_req->return_scsi_cmd_on_abts)
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_SCSI_TM,
				  "Not call scsi_done for xid=0x%x.\n",
				  io_req->xid);
		if (io_req->return_scsi_cmd_on_abts)
			qedf_scsi_done(qedf, io_req, DID_ERROR);
	}

	/* Notify eh_abort handler that ABTS is complete */
	complete(&io_req->abts_done);

	kref_put(&io_req->refcount, qedf_release_cmd);
}

int qedf_init_mp_req(struct qedf_ioreq *io_req)
{
	struct qedf_mp_req *mp_req;
	struct scsi_sge *mp_req_bd;
	struct scsi_sge *mp_resp_bd;
	struct qedf_ctx *qedf = io_req->fcport->qedf;
	dma_addr_t addr;
	uint64_t sz;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_MP_REQ, "Entered.\n");

	mp_req = (struct qedf_mp_req *)&(io_req->mp_req);
	memset(mp_req, 0, sizeof(struct qedf_mp_req));

	if (io_req->cmd_type != QEDF_ELS) {
		mp_req->req_len = sizeof(struct fcp_cmnd);
		io_req->data_xfer_len = mp_req->req_len;
	} else
		mp_req->req_len = io_req->data_xfer_len;

	mp_req->req_buf = dma_alloc_coherent(&qedf->pdev->dev, QEDF_PAGE_SIZE,
	    &mp_req->req_buf_dma, GFP_KERNEL);
	if (!mp_req->req_buf) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to alloc MP req buffer\n");
		qedf_free_mp_resc(io_req);
		return -ENOMEM;
	}

	mp_req->resp_buf = dma_alloc_coherent(&qedf->pdev->dev,
	    QEDF_PAGE_SIZE, &mp_req->resp_buf_dma, GFP_KERNEL);
	if (!mp_req->resp_buf) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to alloc TM resp "
			  "buffer\n");
		qedf_free_mp_resc(io_req);
		return -ENOMEM;
	}

	/* Allocate and map mp_req_bd and mp_resp_bd */
	sz = sizeof(struct scsi_sge);
	mp_req->mp_req_bd = dma_alloc_coherent(&qedf->pdev->dev, sz,
	    &mp_req->mp_req_bd_dma, GFP_KERNEL);
	if (!mp_req->mp_req_bd) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to alloc MP req bd\n");
		qedf_free_mp_resc(io_req);
		return -ENOMEM;
	}

	mp_req->mp_resp_bd = dma_alloc_coherent(&qedf->pdev->dev, sz,
	    &mp_req->mp_resp_bd_dma, GFP_KERNEL);
	if (!mp_req->mp_resp_bd) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to alloc MP resp bd\n");
		qedf_free_mp_resc(io_req);
		return -ENOMEM;
	}

	/* Fill bd table */
	addr = mp_req->req_buf_dma;
	mp_req_bd = mp_req->mp_req_bd;
	mp_req_bd->sge_addr.lo = U64_LO(addr);
	mp_req_bd->sge_addr.hi = U64_HI(addr);
	mp_req_bd->sge_len = QEDF_PAGE_SIZE;

	/*
	 * MP buffer is either a task mgmt command or an ELS.
	 * So the assumption is that it consumes a single bd
	 * entry in the bd table
	 */
	mp_resp_bd = mp_req->mp_resp_bd;
	addr = mp_req->resp_buf_dma;
	mp_resp_bd->sge_addr.lo = U64_LO(addr);
	mp_resp_bd->sge_addr.hi = U64_HI(addr);
	mp_resp_bd->sge_len = QEDF_PAGE_SIZE;

	return 0;
}

/*
 * Last ditch effort to clear the port if it's stuck. Used only after a
 * cleanup task times out.
 */
static void qedf_drain_request(struct qedf_ctx *qedf)
{
	if (test_bit(QEDF_DRAIN_ACTIVE, &qedf->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "MCP drain already active.\n");
		return;
	}

	/* Set bit to return all queuecommand requests as busy */
	set_bit(QEDF_DRAIN_ACTIVE, &qedf->flags);

	/* Call qed drain request for function. Should be synchronous */
	qed_ops->common->drain(qedf->cdev);

	/* Settle time for CQEs to be returned */
	msleep(100);

	/* Unplug and continue */
	clear_bit(QEDF_DRAIN_ACTIVE, &qedf->flags);
}

/*
 * Returns SUCCESS if the cleanup task does not timeout, otherwise return
 * FAILURE.
 */
int qedf_initiate_cleanup(struct qedf_ioreq *io_req,
	bool return_scsi_cmd_on_abts)
{
	struct qedf_rport *fcport;
	struct qedf_ctx *qedf;
	int tmo = 0;
	int rc = SUCCESS;
	unsigned long flags;
	struct fcoe_wqe *sqe;
	u16 sqe_idx;
	int refcount = 0;

	fcport = io_req->fcport;
	if (!fcport) {
		QEDF_ERR(NULL, "fcport is NULL.\n");
		return SUCCESS;
	}

	/* Sanity check qedf_rport before dereferencing any pointers */
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(NULL, "tgt not offloaded\n");
		return SUCCESS;
	}

	qedf = fcport->qedf;
	if (!qedf) {
		QEDF_ERR(NULL, "qedf is NULL.\n");
		return SUCCESS;
	}

	if (io_req->cmd_type == QEDF_ELS) {
		goto process_els;
	}

	if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags) ||
	    test_and_set_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "io_req xid=0x%x already in "
			  "cleanup processing or already completed.\n",
			  io_req->xid);
		return SUCCESS;
	}
	set_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags);

process_els:
	/* Ensure room on SQ */
	if (!atomic_read(&fcport->free_sqes)) {
		QEDF_ERR(&(qedf->dbg_ctx), "No SQ entries available\n");
		/* Need to make sure we clear the flag since it was set */
		clear_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags);
		return FAILED;
	}

	if (io_req->cmd_type == QEDF_CLEANUP) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "io_req=0x%x is already a cleanup command cmd_type=%d.\n",
			 io_req->xid, io_req->cmd_type);
		clear_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags);
		return SUCCESS;
	}

	refcount = kref_read(&io_req->refcount);

	QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_IO,
		  "Entered xid=0x%x sc_cmd=%p cmd_type=%d flags=0x%lx refcount=%d fcport=%p port_id=0x%06x\n",
		  io_req->xid, io_req->sc_cmd, io_req->cmd_type, io_req->flags,
		  refcount, fcport, fcport->rdata->ids.port_id);

	/* Cleanup cmds re-use the same TID as the original I/O */
	io_req->cmd_type = QEDF_CLEANUP;
	io_req->return_scsi_cmd_on_abts = return_scsi_cmd_on_abts;

	init_completion(&io_req->cleanup_done);

	spin_lock_irqsave(&fcport->rport_lock, flags);

	sqe_idx = qedf_get_sqe_idx(fcport);
	sqe = &fcport->sq[sqe_idx];
	memset(sqe, 0, sizeof(struct fcoe_wqe));
	io_req->task_params->sqe = sqe;

	init_initiator_cleanup_fcoe_task(io_req->task_params);
	qedf_ring_doorbell(fcport);

	spin_unlock_irqrestore(&fcport->rport_lock, flags);

	tmo = wait_for_completion_timeout(&io_req->cleanup_done,
					  QEDF_CLEANUP_TIMEOUT * HZ);

	if (!tmo) {
		rc = FAILED;
		/* Timeout case */
		QEDF_ERR(&(qedf->dbg_ctx), "Cleanup command timeout, "
			  "xid=%x.\n", io_req->xid);
		clear_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags);
		/* Issue a drain request if cleanup task times out */
		QEDF_ERR(&(qedf->dbg_ctx), "Issuing MCP drain request.\n");
		qedf_drain_request(qedf);
	}

	/* If it TASK MGMT handle it, reference will be decreased
	 * in qedf_execute_tmf
	 */
	if (io_req->tm_flags  == FCP_TMF_LUN_RESET ||
	    io_req->tm_flags == FCP_TMF_TGT_RESET) {
		clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);
		io_req->sc_cmd = NULL;
		complete(&io_req->tm_done);
	}

	if (io_req->sc_cmd) {
		if (!io_req->return_scsi_cmd_on_abts)
			QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_SCSI_TM,
				  "Not call scsi_done for xid=0x%x.\n",
				  io_req->xid);
		if (io_req->return_scsi_cmd_on_abts)
			qedf_scsi_done(qedf, io_req, DID_ERROR);
	}

	if (rc == SUCCESS)
		io_req->event = QEDF_IOREQ_EV_CLEANUP_SUCCESS;
	else
		io_req->event = QEDF_IOREQ_EV_CLEANUP_FAILED;

	return rc;
}

void qedf_process_cleanup_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "Entered xid = 0x%x\n",
		   io_req->xid);

	clear_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags);

	/* Complete so we can finish cleaning up the I/O */
	complete(&io_req->cleanup_done);
}

static int qedf_execute_tmf(struct qedf_rport *fcport, struct scsi_cmnd *sc_cmd,
	uint8_t tm_flags)
{
	struct qedf_ioreq *io_req;
	struct fcoe_task_context *task;
	struct qedf_ctx *qedf = fcport->qedf;
	struct fc_lport *lport = qedf->lport;
	int rc = 0;
	uint16_t xid;
	int tmo = 0;
	int lun = 0;
	unsigned long flags;
	struct fcoe_wqe *sqe;
	u16 sqe_idx;

	if (!sc_cmd) {
		QEDF_ERR(&qedf->dbg_ctx, "sc_cmd is NULL\n");
		return FAILED;
	}

	lun = (int)sc_cmd->device->lun;
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "fcport not offloaded\n");
		rc = FAILED;
		goto no_flush;
	}

	io_req = qedf_alloc_cmd(fcport, QEDF_TASK_MGMT_CMD);
	if (!io_req) {
		QEDF_ERR(&(qedf->dbg_ctx), "Failed TMF");
		rc = -EAGAIN;
		goto no_flush;
	}

	if (tm_flags == FCP_TMF_LUN_RESET)
		qedf->lun_resets++;
	else if (tm_flags == FCP_TMF_TGT_RESET)
		qedf->target_resets++;

	/* Initialize rest of io_req fields */
	io_req->sc_cmd = sc_cmd;
	io_req->fcport = fcport;
	io_req->cmd_type = QEDF_TASK_MGMT_CMD;

	/* Record which cpu this request is associated with */
	io_req->cpu = smp_processor_id();

	/* Set TM flags */
	io_req->io_req_flags = QEDF_READ;
	io_req->data_xfer_len = 0;
	io_req->tm_flags = tm_flags;

	/* Default is to return a SCSI command when an error occurs */
	io_req->return_scsi_cmd_on_abts = false;

	/* Obtain exchange id */
	xid = io_req->xid;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM, "TMF io_req xid = "
		   "0x%x\n", xid);

	/* Initialize task context for this IO request */
	task = qedf_get_task_mem(&qedf->tasks, xid);

	init_completion(&io_req->tm_done);

	spin_lock_irqsave(&fcport->rport_lock, flags);

	sqe_idx = qedf_get_sqe_idx(fcport);
	sqe = &fcport->sq[sqe_idx];
	memset(sqe, 0, sizeof(struct fcoe_wqe));

	qedf_init_task(fcport, lport, io_req, task, sqe);
	qedf_ring_doorbell(fcport);

	spin_unlock_irqrestore(&fcport->rport_lock, flags);

	set_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);
	tmo = wait_for_completion_timeout(&io_req->tm_done,
	    QEDF_TM_TIMEOUT * HZ);

	if (!tmo) {
		rc = FAILED;
		QEDF_ERR(&(qedf->dbg_ctx), "wait for tm_cmpl timeout!\n");
		/* Clear outstanding bit since command timed out */
		clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);
		io_req->sc_cmd = NULL;
	} else {
		/* Check TMF response code */
		if (io_req->fcp_rsp_code == 0)
			rc = SUCCESS;
		else
			rc = FAILED;
	}
	/*
	 * Double check that fcport has not gone into an uploading state before
	 * executing the command flush for the LUN/target.
	 */
	if (test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		QEDF_ERR(&qedf->dbg_ctx,
			 "fcport is uploading, not executing flush.\n");
		goto no_flush;
	}
	/* We do not need this io_req any more */
	kref_put(&io_req->refcount, qedf_release_cmd);


	if (tm_flags == FCP_TMF_LUN_RESET)
		qedf_flush_active_ios(fcport, lun);
	else
		qedf_flush_active_ios(fcport, -1);

no_flush:
	if (rc != SUCCESS) {
		QEDF_ERR(&(qedf->dbg_ctx), "task mgmt command failed...\n");
		rc = FAILED;
	} else {
		QEDF_ERR(&(qedf->dbg_ctx), "task mgmt command success...\n");
		rc = SUCCESS;
	}
	return rc;
}

int qedf_initiate_tmf(struct scsi_cmnd *sc_cmd, u8 tm_flags)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct qedf_rport *fcport = (struct qedf_rport *)&rp[1];
	struct qedf_ctx *qedf;
	struct fc_lport *lport = shost_priv(sc_cmd->device->host);
	int rc = SUCCESS;
	int rval;
	struct qedf_ioreq *io_req = NULL;
	int ref_cnt = 0;
	struct fc_rport_priv *rdata = fcport->rdata;

	QEDF_ERR(NULL,
		 "tm_flags 0x%x sc_cmd %p op = 0x%02x target_id = 0x%x lun=%d\n",
		 tm_flags, sc_cmd, sc_cmd->cmd_len ? sc_cmd->cmnd[0] : 0xff,
		 rport->scsi_target_id, (int)sc_cmd->device->lun);

	if (!rdata || !kref_get_unless_zero(&rdata->kref)) {
		QEDF_ERR(NULL, "stale rport\n");
		return FAILED;
	}

	QEDF_ERR(NULL, "portid=%06x tm_flags =%s\n", rdata->ids.port_id,
		 (tm_flags == FCP_TMF_TGT_RESET) ? "TARGET RESET" :
		 "LUN RESET");

	if (sc_cmd->SCp.ptr) {
		io_req = (struct qedf_ioreq *)sc_cmd->SCp.ptr;
		ref_cnt = kref_read(&io_req->refcount);
		QEDF_ERR(NULL,
			 "orig io_req = %p xid = 0x%x ref_cnt = %d.\n",
			 io_req, io_req->xid, ref_cnt);
	}

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		QEDF_ERR(NULL, "device_reset rport not ready\n");
		rc = FAILED;
		goto tmf_err;
	}

	rc = fc_block_scsi_eh(sc_cmd);
	if (rc)
		goto tmf_err;

	if (!fcport) {
		QEDF_ERR(NULL, "device_reset: rport is NULL\n");
		rc = FAILED;
		goto tmf_err;
	}

	qedf = fcport->qedf;

	if (!qedf) {
		QEDF_ERR(NULL, "qedf is NULL.\n");
		rc = FAILED;
		goto tmf_err;
	}

	if (test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		QEDF_ERR(&qedf->dbg_ctx, "Connection is getting uploaded.\n");
		rc = SUCCESS;
		goto tmf_err;
	}

	if (test_bit(QEDF_UNLOADING, &qedf->flags) ||
	    test_bit(QEDF_DBG_STOP_IO, &qedf->flags)) {
		rc = SUCCESS;
		goto tmf_err;
	}

	if (lport->state != LPORT_ST_READY || !(lport->link_up)) {
		QEDF_ERR(&(qedf->dbg_ctx), "link is not ready\n");
		rc = FAILED;
		goto tmf_err;
	}

	if (test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		if (!fcport->rdata)
			QEDF_ERR(&qedf->dbg_ctx, "fcport %p is uploading.\n",
				 fcport);
		else
			QEDF_ERR(&qedf->dbg_ctx,
				 "fcport %p port_id=%06x is uploading.\n",
				 fcport, fcport->rdata->ids.port_id);
		rc = FAILED;
		goto tmf_err;
	}

	rc = qedf_execute_tmf(fcport, sc_cmd, tm_flags);

tmf_err:
	kref_put(&rdata->kref, fc_rport_destroy);
	return rc;
}

void qedf_process_tmf_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	struct fcoe_cqe_rsp_info *fcp_rsp;

	clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);

	fcp_rsp = &cqe->cqe_info.rsp_info;
	qedf_parse_fcp_rsp(io_req, fcp_rsp);

	io_req->sc_cmd = NULL;
	complete(&io_req->tm_done);
}

void qedf_process_unsol_compl(struct qedf_ctx *qedf, uint16_t que_idx,
	struct fcoe_cqe *cqe)
{
	unsigned long flags;
	uint16_t pktlen = cqe->cqe_info.unsolic_info.pkt_len;
	u32 payload_len, crc;
	struct fc_frame_header *fh;
	struct fc_frame *fp;
	struct qedf_io_work *io_work;
	u32 bdq_idx;
	void *bdq_addr;
	struct scsi_bd *p_bd_info;

	p_bd_info = &cqe->cqe_info.unsolic_info.bd_info;
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_UNSOL,
		  "address.hi=%x, address.lo=%x, opaque_data.hi=%x, opaque_data.lo=%x, bdq_prod_idx=%u, len=%u\n",
		  le32_to_cpu(p_bd_info->address.hi),
		  le32_to_cpu(p_bd_info->address.lo),
		  le32_to_cpu(p_bd_info->opaque.fcoe_opaque.hi),
		  le32_to_cpu(p_bd_info->opaque.fcoe_opaque.lo),
		  qedf->bdq_prod_idx, pktlen);

	bdq_idx = le32_to_cpu(p_bd_info->opaque.fcoe_opaque.lo);
	if (bdq_idx >= QEDF_BDQ_SIZE) {
		QEDF_ERR(&(qedf->dbg_ctx), "bdq_idx is out of range %d.\n",
		    bdq_idx);
		goto increment_prod;
	}

	bdq_addr = qedf->bdq[bdq_idx].buf_addr;
	if (!bdq_addr) {
		QEDF_ERR(&(qedf->dbg_ctx), "bdq_addr is NULL, dropping "
		    "unsolicited packet.\n");
		goto increment_prod;
	}

	if (qedf_dump_frames) {
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_UNSOL,
		    "BDQ frame is at addr=%p.\n", bdq_addr);
		print_hex_dump(KERN_WARNING, "bdq ", DUMP_PREFIX_OFFSET, 16, 1,
		    (void *)bdq_addr, pktlen, false);
	}

	/* Allocate frame */
	payload_len = pktlen - sizeof(struct fc_frame_header);
	fp = fc_frame_alloc(qedf->lport, payload_len);
	if (!fp) {
		QEDF_ERR(&(qedf->dbg_ctx), "Could not allocate fp.\n");
		goto increment_prod;
	}

	/* Copy data from BDQ buffer into fc_frame struct */
	fh = (struct fc_frame_header *)fc_frame_header_get(fp);
	memcpy(fh, (void *)bdq_addr, pktlen);

	QEDF_WARN(&qedf->dbg_ctx,
		  "Processing Unsolicated frame, src=%06x dest=%06x r_ctl=0x%x type=0x%x cmd=%02x\n",
		  ntoh24(fh->fh_s_id), ntoh24(fh->fh_d_id), fh->fh_r_ctl,
		  fh->fh_type, fc_frame_payload_op(fp));

	/* Initialize the frame so libfc sees it as a valid frame */
	crc = fcoe_fc_crc(fp);
	fc_frame_init(fp);
	fr_dev(fp) = qedf->lport;
	fr_sof(fp) = FC_SOF_I3;
	fr_eof(fp) = FC_EOF_T;
	fr_crc(fp) = cpu_to_le32(~crc);

	/*
	 * We need to return the frame back up to libfc in a non-atomic
	 * context
	 */
	io_work = mempool_alloc(qedf->io_mempool, GFP_ATOMIC);
	if (!io_work) {
		QEDF_WARN(&(qedf->dbg_ctx), "Could not allocate "
			   "work for I/O completion.\n");
		fc_frame_free(fp);
		goto increment_prod;
	}
	memset(io_work, 0, sizeof(struct qedf_io_work));

	INIT_WORK(&io_work->work, qedf_fp_io_handler);

	/* Copy contents of CQE for deferred processing */
	memcpy(&io_work->cqe, cqe, sizeof(struct fcoe_cqe));

	io_work->qedf = qedf;
	io_work->fp = fp;

	queue_work_on(smp_processor_id(), qedf_io_wq, &io_work->work);
increment_prod:
	spin_lock_irqsave(&qedf->hba_lock, flags);

	/* Increment producer to let f/w know we've handled the frame */
	qedf->bdq_prod_idx++;

	/* Producer index wraps at uint16_t boundary */
	if (qedf->bdq_prod_idx == 0xffff)
		qedf->bdq_prod_idx = 0;

	writew(qedf->bdq_prod_idx, qedf->bdq_primary_prod);
	readw(qedf->bdq_primary_prod);
	writew(qedf->bdq_prod_idx, qedf->bdq_secondary_prod);
	readw(qedf->bdq_secondary_prod);

	spin_unlock_irqrestore(&qedf->hba_lock, flags);
}

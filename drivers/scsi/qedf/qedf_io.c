/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
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
	struct qedf_ctx *qedf = io_req->fcport->qedf;
	struct qedf_rport *fcport = io_req->fcport;
	u8 op = 0;

	switch (io_req->cmd_type) {
	case QEDF_ABTS:
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

		/*
		 * Now that the original I/O and the ABTS are complete see
		 * if we need to reconnect to the target.
		 */
		qedf_restart_rport(fcport);
		break;
	case QEDF_ELS:
		kref_get(&io_req->refcount);
		/*
		 * Don't attempt to clean an ELS timeout as any subseqeunt
		 * ABTS or cleanup requests just hang.  For now just free
		 * the resources of the original I/O and the RRQ
		 */
		QEDF_ERR(&(qedf->dbg_ctx), "ELS timeout, xid=0x%x.\n",
			  io_req->xid);
		io_req->event = QEDF_IOREQ_EV_ELS_TMO;
		/* Call callback function to complete command */
		if (io_req->cb_func && io_req->cb_arg) {
			op = io_req->cb_arg->op;
			io_req->cb_func(io_req->cb_arg);
			io_req->cb_arg = NULL;
		}
		qedf_initiate_cleanup(io_req, true);
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
		break;
	}
}

void qedf_cmd_mgr_free(struct qedf_cmd_mgr *cmgr)
{
	struct io_bdt *bdt_info;
	struct qedf_ctx *qedf = cmgr->qedf;
	size_t bd_tbl_sz;
	u16 min_xid = QEDF_MIN_XID;
	u16 max_xid = (FCOE_PARAMS_NUM_TASKS - 1);
	int num_ios;
	int i;
	struct qedf_ioreq *io_req;

	num_ios = max_xid - min_xid + 1;

	/* Free fcoe_bdt_ctx structures */
	if (!cmgr->io_bdt_pool)
		goto free_cmd_pool;

	bd_tbl_sz = QEDF_MAX_BDS_PER_CMD * sizeof(struct fcoe_sge);
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
	u16 min_xid = QEDF_MIN_XID;
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
	 * Initialize list of qedf_ioreq.
	 */
	xid = QEDF_MIN_XID;

	for (i = 0; i < num_ios; i++) {
		io_req = &cmgr->cmds[i];
		INIT_DELAYED_WORK(&io_req->timeout_work, qedf_cmd_timeout);

		io_req->xid = xid++;

		INIT_DELAYED_WORK(&io_req->rrq_work, qedf_handle_rrq);

		/* Allocate DMA memory to hold sense buffer */
		io_req->sense_buffer = dma_alloc_coherent(&qedf->pdev->dev,
		    QEDF_SCSI_SENSE_BUFFERSIZE, &io_req->sense_buffer_dma,
		    GFP_KERNEL);
		if (!io_req->sense_buffer)
			goto mem_err;
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
			QEDF_WARN(&(qedf->dbg_ctx), "Failed to alloc "
				   "io_bdt_pool[%d].\n", i);
			goto mem_err;
		}
	}

	for (i = 0; i < num_ios; i++) {
		bdt_info = cmgr->io_bdt_pool[i];
		bdt_info->bd_tbl = dma_alloc_coherent(&qedf->pdev->dev,
		    QEDF_MAX_BDS_PER_CMD * sizeof(struct fcoe_sge),
		    &bdt_info->bd_tbl_dma, GFP_KERNEL);
		if (!bdt_info->bd_tbl) {
			QEDF_WARN(&(qedf->dbg_ctx), "Failed to alloc "
				   "bdt_tbl[%d].\n", i);
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
		if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags))
			break;
	}

	if (i == FCOE_PARAMS_NUM_TASKS) {
		spin_unlock_irqrestore(&cmd_mgr->lock, flags);
		goto out_failed;
	}

	set_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);
	spin_unlock_irqrestore(&cmd_mgr->lock, flags);

	atomic_inc(&fcport->num_active_ios);
	atomic_dec(&fcport->free_sqes);
	xid = io_req->xid;
	atomic_dec(&cmd_mgr->free_list_cnt);

	io_req->cmd_mgr = cmd_mgr;
	io_req->fcport = fcport;

	/* Hold the io_req against deletion */
	kref_init(&io_req->refcount);

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
	uint64_t sz = sizeof(struct fcoe_sge);

	/* clear tm flags */
	mp_req->tm_flags = 0;
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

	if (io_req->cmd_type == QEDF_ELS ||
	    io_req->cmd_type == QEDF_TASK_MGMT_CMD)
		qedf_free_mp_resc(io_req);

	atomic_inc(&cmd_mgr->free_list_cnt);
	atomic_dec(&fcport->num_active_ios);
	if (atomic_read(&fcport->num_active_ios) < 0)
		QEDF_WARN(&(fcport->qedf->dbg_ctx), "active_ios < 0.\n");

	/* Increment task retry identifier now that the request is released */
	io_req->task_retry_identifier++;

	clear_bit(QEDF_CMD_OUTSTANDING, &io_req->flags);
}

static int qedf_split_bd(struct qedf_ioreq *io_req, u64 addr, int sg_len,
	int bd_index)
{
	struct fcoe_sge *bd = io_req->bd_tbl->bd_tbl;
	int frag_size, sg_frags;

	sg_frags = 0;
	while (sg_len) {
		if (sg_len > QEDF_BD_SPLIT_SZ)
			frag_size = QEDF_BD_SPLIT_SZ;
		else
			frag_size = sg_len;
		bd[bd_index + sg_frags].sge_addr.lo = U64_LO(addr);
		bd[bd_index + sg_frags].sge_addr.hi = U64_HI(addr);
		bd[bd_index + sg_frags].size = (uint16_t)frag_size;

		addr += (u64)frag_size;
		sg_frags++;
		sg_len -= frag_size;
	}
	return sg_frags;
}

static int qedf_map_sg(struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc = io_req->sc_cmd;
	struct Scsi_Host *host = sc->device->host;
	struct fc_lport *lport = shost_priv(host);
	struct qedf_ctx *qedf = lport_priv(lport);
	struct fcoe_sge *bd = io_req->bd_tbl->bd_tbl;
	struct scatterlist *sg;
	int byte_count = 0;
	int sg_count = 0;
	int bd_count = 0;
	int sg_frags;
	unsigned int sg_len;
	u64 addr, end_addr;
	int i;

	sg_count = dma_map_sg(&qedf->pdev->dev, scsi_sglist(sc),
	    scsi_sg_count(sc), sc->sc_data_direction);

	sg = scsi_sglist(sc);

	/*
	 * New condition to send single SGE as cached-SGL with length less
	 * than 64k.
	 */
	if ((sg_count == 1) && (sg_dma_len(sg) <=
	    QEDF_MAX_SGLEN_FOR_CACHESGL)) {
		sg_len = sg_dma_len(sg);
		addr = (u64)sg_dma_address(sg);

		bd[bd_count].sge_addr.lo = (addr & 0xffffffff);
		bd[bd_count].sge_addr.hi = (addr >> 32);
		bd[bd_count].size = (u16)sg_len;

		return ++bd_count;
	}

	scsi_for_each_sg(sc, sg, sg_count, i) {
		sg_len = sg_dma_len(sg);
		addr = (u64)sg_dma_address(sg);
		end_addr = (u64)(addr + sg_len);

		/*
		 * First s/g element in the list so check if the end_addr
		 * is paged aligned. Also check to make sure the length is
		 * at least page size.
		 */
		if ((i == 0) && (sg_count > 1) &&
		    ((end_addr % QEDF_PAGE_SIZE) ||
		    sg_len < QEDF_PAGE_SIZE))
			io_req->use_slowpath = true;
		/*
		 * Last s/g element so check if the start address is paged
		 * aligned.
		 */
		else if ((i == (sg_count - 1)) && (sg_count > 1) &&
		    (addr % QEDF_PAGE_SIZE))
			io_req->use_slowpath = true;
		/*
		 * Intermediate s/g element so check if start and end address
		 * is page aligned.
		 */
		else if ((i != 0) && (i != (sg_count - 1)) &&
		    ((addr % QEDF_PAGE_SIZE) || (end_addr % QEDF_PAGE_SIZE)))
			io_req->use_slowpath = true;

		if (sg_len > QEDF_MAX_BD_LEN) {
			sg_frags = qedf_split_bd(io_req, addr, sg_len,
			    bd_count);
		} else {
			sg_frags = 1;
			bd[bd_count].sge_addr.lo = U64_LO(addr);
			bd[bd_count].sge_addr.hi  = U64_HI(addr);
			bd[bd_count].size = (uint16_t)sg_len;
		}

		bd_count += sg_frags;
		byte_count += sg_len;
	}

	if (byte_count != scsi_bufflen(sc))
		QEDF_ERR(&(qedf->dbg_ctx), "byte_count = %d != "
			  "scsi_bufflen = %d, task_id = 0x%x.\n", byte_count,
			   scsi_bufflen(sc), io_req->xid);

	return bd_count;
}

static int qedf_build_bd_list_from_sg(struct qedf_ioreq *io_req)
{
	struct scsi_cmnd *sc = io_req->sc_cmd;
	struct fcoe_sge *bd = io_req->bd_tbl->bd_tbl;
	int bd_count;

	if (scsi_sg_count(sc)) {
		bd_count = qedf_map_sg(io_req);
		if (bd_count == 0)
			return -ENOMEM;
	} else {
		bd_count = 0;
		bd[0].sge_addr.lo = bd[0].sge_addr.hi = 0;
		bd[0].size = 0;
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
	fcp_cmnd->fc_tm_flags = io_req->mp_req.tm_flags;
	fcp_cmnd->fc_flags = io_req->io_req_flags;
	fcp_cmnd->fc_cmdref = 0;

	/* Populate data direction */
	if (sc_cmd->sc_data_direction == DMA_TO_DEVICE)
		fcp_cmnd->fc_flags |= FCP_CFL_WRDATA;
	else if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE)
		fcp_cmnd->fc_flags |= FCP_CFL_RDDATA;

	fcp_cmnd->fc_pri_ta = FCP_PTA_SIMPLE;

	/* 16 bytes: CDB information */
	memcpy(fcp_cmnd->fc_cdb, sc_cmd->cmnd, sc_cmd->cmd_len);

	/* 4 bytes: FCP data length */
	fcp_cmnd->fc_dl = htonl(io_req->data_xfer_len);

}

static void  qedf_init_task(struct qedf_rport *fcport, struct fc_lport *lport,
	struct qedf_ioreq *io_req, u32 *ptu_invalidate,
	struct fcoe_task_context *task_ctx)
{
	enum fcoe_task_type task_type;
	struct scsi_cmnd *sc_cmd = io_req->sc_cmd;
	struct io_bdt *bd_tbl = io_req->bd_tbl;
	union fcoe_data_desc_ctx *data_desc;
	u32 *fcp_cmnd;
	u32 tmp_fcp_cmnd[8];
	int cnt, i;
	int bd_count;
	struct qedf_ctx *qedf = fcport->qedf;
	uint16_t cq_idx = smp_processor_id() % qedf->num_queues;
	u8 tmp_sgl_mode = 0;
	u8 mst_sgl_mode = 0;

	memset(task_ctx, 0, sizeof(struct fcoe_task_context));
	io_req->task = task_ctx;

	if (sc_cmd->sc_data_direction == DMA_TO_DEVICE)
		task_type = FCOE_TASK_TYPE_WRITE_INITIATOR;
	else
		task_type = FCOE_TASK_TYPE_READ_INITIATOR;

	/* Y Storm context */
	task_ctx->ystorm_st_context.expect_first_xfer = 1;
	task_ctx->ystorm_st_context.data_2_trns_rem = io_req->data_xfer_len;
	/* Check if this is required */
	task_ctx->ystorm_st_context.ox_id = io_req->xid;
	task_ctx->ystorm_st_context.task_rety_identifier =
	    io_req->task_retry_identifier;

	/* T Storm ag context */
	SET_FIELD(task_ctx->tstorm_ag_context.flags0,
	    TSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE, PROTOCOLID_FCOE);
	task_ctx->tstorm_ag_context.icid = (u16)fcport->fw_cid;

	/* T Storm st context */
	SET_FIELD(task_ctx->tstorm_st_context.read_write.flags,
	    FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_EXP_FIRST_FRAME,
	    1);
	task_ctx->tstorm_st_context.read_write.rx_id = 0xffff;

	task_ctx->tstorm_st_context.read_only.dev_type =
	    FCOE_TASK_DEV_TYPE_DISK;
	task_ctx->tstorm_st_context.read_only.conf_supported = 0;
	task_ctx->tstorm_st_context.read_only.cid = fcport->fw_cid;

	/* Completion queue for response. */
	task_ctx->tstorm_st_context.read_only.glbl_q_num = cq_idx;
	task_ctx->tstorm_st_context.read_only.fcp_cmd_trns_size =
	    io_req->data_xfer_len;
	task_ctx->tstorm_st_context.read_write.e_d_tov_exp_timeout_val =
	    lport->e_d_tov;

	task_ctx->ustorm_ag_context.global_cq_num = cq_idx;
	io_req->fp_idx = cq_idx;

	bd_count = bd_tbl->bd_valid;
	if (task_type == FCOE_TASK_TYPE_WRITE_INITIATOR) {
		/* Setup WRITE task */
		struct fcoe_sge *fcoe_bd_tbl = bd_tbl->bd_tbl;

		task_ctx->ystorm_st_context.task_type =
		    FCOE_TASK_TYPE_WRITE_INITIATOR;
		data_desc = &task_ctx->ystorm_st_context.data_desc;

		if (io_req->use_slowpath) {
			SET_FIELD(task_ctx->ystorm_st_context.sgl_mode,
			    YSTORM_FCOE_TASK_ST_CTX_TX_SGL_MODE,
			    FCOE_SLOW_SGL);
			data_desc->slow.base_sgl_addr.lo =
			    U64_LO(bd_tbl->bd_tbl_dma);
			data_desc->slow.base_sgl_addr.hi =
			    U64_HI(bd_tbl->bd_tbl_dma);
			data_desc->slow.remainder_num_sges = bd_count;
			data_desc->slow.curr_sge_off = 0;
			data_desc->slow.curr_sgl_index = 0;
			qedf->slow_sge_ios++;
			io_req->sge_type = QEDF_IOREQ_SLOW_SGE;
		} else {
			SET_FIELD(task_ctx->ystorm_st_context.sgl_mode,
			    YSTORM_FCOE_TASK_ST_CTX_TX_SGL_MODE,
			    (bd_count <= 4) ? (enum fcoe_sgl_mode)bd_count :
			    FCOE_MUL_FAST_SGES);

			if (bd_count == 1) {
				data_desc->single_sge.sge_addr.lo =
				    fcoe_bd_tbl->sge_addr.lo;
				data_desc->single_sge.sge_addr.hi =
				    fcoe_bd_tbl->sge_addr.hi;
				data_desc->single_sge.size =
				    fcoe_bd_tbl->size;
				data_desc->single_sge.is_valid_sge = 0;
				qedf->single_sge_ios++;
				io_req->sge_type = QEDF_IOREQ_SINGLE_SGE;
			} else {
				data_desc->fast.sgl_start_addr.lo =
				    U64_LO(bd_tbl->bd_tbl_dma);
				data_desc->fast.sgl_start_addr.hi =
				    U64_HI(bd_tbl->bd_tbl_dma);
				data_desc->fast.sgl_byte_offset =
				    data_desc->fast.sgl_start_addr.lo &
				    (QEDF_PAGE_SIZE - 1);
				if (data_desc->fast.sgl_byte_offset > 0)
					QEDF_ERR(&(qedf->dbg_ctx),
					    "byte_offset=%u for xid=0x%x.\n",
					    io_req->xid,
					    data_desc->fast.sgl_byte_offset);
				data_desc->fast.task_reuse_cnt =
				    io_req->reuse_count;
				io_req->reuse_count++;
				if (io_req->reuse_count == QEDF_MAX_REUSE) {
					*ptu_invalidate = 1;
					io_req->reuse_count = 0;
				}
				qedf->fast_sge_ios++;
				io_req->sge_type = QEDF_IOREQ_FAST_SGE;
			}
		}

		/* T Storm context */
		task_ctx->tstorm_st_context.read_only.task_type =
		    FCOE_TASK_TYPE_WRITE_INITIATOR;

		/* M Storm context */
		tmp_sgl_mode = GET_FIELD(task_ctx->ystorm_st_context.sgl_mode,
		    YSTORM_FCOE_TASK_ST_CTX_TX_SGL_MODE);
		SET_FIELD(task_ctx->mstorm_st_context.non_fp.tx_rx_sgl_mode,
		    FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_TX_SGL_MODE,
		    tmp_sgl_mode);

	} else {
		/* Setup READ task */

		/* M Storm context */
		struct fcoe_sge *fcoe_bd_tbl = bd_tbl->bd_tbl;

		data_desc = &task_ctx->mstorm_st_context.fp.data_desc;
		task_ctx->mstorm_st_context.fp.data_2_trns_rem =
		    io_req->data_xfer_len;

		if (io_req->use_slowpath) {
			SET_FIELD(
			    task_ctx->mstorm_st_context.non_fp.tx_rx_sgl_mode,
			    FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RX_SGL_MODE,
			    FCOE_SLOW_SGL);
			data_desc->slow.base_sgl_addr.lo =
			    U64_LO(bd_tbl->bd_tbl_dma);
			data_desc->slow.base_sgl_addr.hi =
			    U64_HI(bd_tbl->bd_tbl_dma);
			data_desc->slow.remainder_num_sges =
			    bd_count;
			data_desc->slow.curr_sge_off = 0;
			data_desc->slow.curr_sgl_index = 0;
			qedf->slow_sge_ios++;
			io_req->sge_type = QEDF_IOREQ_SLOW_SGE;
		} else {
			SET_FIELD(
			    task_ctx->mstorm_st_context.non_fp.tx_rx_sgl_mode,
			    FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RX_SGL_MODE,
			    (bd_count <= 4) ? (enum fcoe_sgl_mode)bd_count :
			    FCOE_MUL_FAST_SGES);

			if (bd_count == 1) {
				data_desc->single_sge.sge_addr.lo =
				    fcoe_bd_tbl->sge_addr.lo;
				data_desc->single_sge.sge_addr.hi =
				    fcoe_bd_tbl->sge_addr.hi;
				data_desc->single_sge.size =
				    fcoe_bd_tbl->size;
				data_desc->single_sge.is_valid_sge = 0;
				qedf->single_sge_ios++;
				io_req->sge_type = QEDF_IOREQ_SINGLE_SGE;
			} else {
				data_desc->fast.sgl_start_addr.lo =
				    U64_LO(bd_tbl->bd_tbl_dma);
				data_desc->fast.sgl_start_addr.hi =
				    U64_HI(bd_tbl->bd_tbl_dma);
				data_desc->fast.sgl_byte_offset = 0;
				data_desc->fast.task_reuse_cnt =
				    io_req->reuse_count;
				io_req->reuse_count++;
				if (io_req->reuse_count == QEDF_MAX_REUSE) {
					*ptu_invalidate = 1;
					io_req->reuse_count = 0;
				}
				qedf->fast_sge_ios++;
				io_req->sge_type = QEDF_IOREQ_FAST_SGE;
			}
		}

		/* Y Storm context */
		task_ctx->ystorm_st_context.expect_first_xfer = 0;
		task_ctx->ystorm_st_context.task_type =
		    FCOE_TASK_TYPE_READ_INITIATOR;

		/* T Storm context */
		task_ctx->tstorm_st_context.read_only.task_type =
		    FCOE_TASK_TYPE_READ_INITIATOR;
		mst_sgl_mode = GET_FIELD(
		    task_ctx->mstorm_st_context.non_fp.tx_rx_sgl_mode,
		    FCOE_MSTORM_FCOE_TASK_ST_CTX_NON_FP_RX_SGL_MODE);
		SET_FIELD(task_ctx->tstorm_st_context.read_write.flags,
		    FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_RX_SGL_MODE,
		    mst_sgl_mode);
	}

	/* fill FCP_CMND IU */
	fcp_cmnd = (u32 *)task_ctx->ystorm_st_context.tx_info_union.fcp_cmd_payload.opaque;
	qedf_build_fcp_cmnd(io_req, (struct fcp_cmnd *)&tmp_fcp_cmnd);

	/* Swap fcp_cmnd since FC is big endian */
	cnt = sizeof(struct fcp_cmnd) / sizeof(u32);

	for (i = 0; i < cnt; i++) {
		*fcp_cmnd = cpu_to_be32(tmp_fcp_cmnd[i]);
		fcp_cmnd++;
	}

	/* M Storm context - Sense buffer */
	task_ctx->mstorm_st_context.non_fp.rsp_buf_addr.lo =
		U64_LO(io_req->sense_buffer_dma);
	task_ctx->mstorm_st_context.non_fp.rsp_buf_addr.hi =
		U64_HI(io_req->sense_buffer_dma);
}

void qedf_init_mp_task(struct qedf_ioreq *io_req,
	struct fcoe_task_context *task_ctx)
{
	struct qedf_mp_req *mp_req = &(io_req->mp_req);
	struct qedf_rport *fcport = io_req->fcport;
	struct qedf_ctx *qedf = io_req->fcport->qedf;
	struct fc_frame_header *fc_hdr;
	enum fcoe_task_type task_type = 0;
	union fcoe_data_desc_ctx *data_desc;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC, "Initializing MP task "
		   "for cmd_type = %d\n", io_req->cmd_type);

	qedf->control_requests++;

	/* Obtain task_type */
	if ((io_req->cmd_type == QEDF_TASK_MGMT_CMD) ||
	    (io_req->cmd_type == QEDF_ELS)) {
		task_type = FCOE_TASK_TYPE_MIDPATH;
	} else if (io_req->cmd_type == QEDF_ABTS) {
		task_type = FCOE_TASK_TYPE_ABTS;
	}

	memset(task_ctx, 0, sizeof(struct fcoe_task_context));

	/* Setup the task from io_req for easy reference */
	io_req->task = task_ctx;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_DISC, "task type = %d\n",
		   task_type);

	/* YSTORM only */
	{
		/* Initialize YSTORM task context */
		struct fcoe_tx_mid_path_params *task_fc_hdr =
		    &task_ctx->ystorm_st_context.tx_info_union.tx_params.mid_path;
		memset(task_fc_hdr, 0, sizeof(struct fcoe_tx_mid_path_params));
		task_ctx->ystorm_st_context.task_rety_identifier =
		    io_req->task_retry_identifier;

		/* Init SGL parameters */
		if ((task_type == FCOE_TASK_TYPE_MIDPATH) ||
		    (task_type == FCOE_TASK_TYPE_UNSOLICITED)) {
			data_desc = &task_ctx->ystorm_st_context.data_desc;
			data_desc->slow.base_sgl_addr.lo =
			    U64_LO(mp_req->mp_req_bd_dma);
			data_desc->slow.base_sgl_addr.hi =
			    U64_HI(mp_req->mp_req_bd_dma);
			data_desc->slow.remainder_num_sges = 1;
			data_desc->slow.curr_sge_off = 0;
			data_desc->slow.curr_sgl_index = 0;
		}

		fc_hdr = &(mp_req->req_fc_hdr);
		if (task_type == FCOE_TASK_TYPE_MIDPATH) {
			fc_hdr->fh_ox_id = io_req->xid;
			fc_hdr->fh_rx_id = htons(0xffff);
		} else if (task_type == FCOE_TASK_TYPE_UNSOLICITED) {
			fc_hdr->fh_rx_id = io_req->xid;
		}

		/* Fill FC Header into middle path buffer */
		task_fc_hdr->parameter = fc_hdr->fh_parm_offset;
		task_fc_hdr->r_ctl = fc_hdr->fh_r_ctl;
		task_fc_hdr->type = fc_hdr->fh_type;
		task_fc_hdr->cs_ctl = fc_hdr->fh_cs_ctl;
		task_fc_hdr->df_ctl = fc_hdr->fh_df_ctl;
		task_fc_hdr->rx_id = fc_hdr->fh_rx_id;
		task_fc_hdr->ox_id = fc_hdr->fh_ox_id;

		task_ctx->ystorm_st_context.data_2_trns_rem =
		    io_req->data_xfer_len;
		task_ctx->ystorm_st_context.task_type = task_type;
	}

	/* TSTORM ONLY */
	{
		task_ctx->tstorm_ag_context.icid = (u16)fcport->fw_cid;
		task_ctx->tstorm_st_context.read_only.cid = fcport->fw_cid;
		/* Always send middle-path repsonses on CQ #0 */
		task_ctx->tstorm_st_context.read_only.glbl_q_num = 0;
		io_req->fp_idx = 0;
		SET_FIELD(task_ctx->tstorm_ag_context.flags0,
		    TSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE,
		    PROTOCOLID_FCOE);
		task_ctx->tstorm_st_context.read_only.task_type = task_type;
		SET_FIELD(task_ctx->tstorm_st_context.read_write.flags,
		    FCOE_TSTORM_FCOE_TASK_ST_CTX_READ_WRITE_EXP_FIRST_FRAME,
		    1);
		task_ctx->tstorm_st_context.read_write.rx_id = 0xffff;
	}

	/* MSTORM only */
	{
		if (task_type == FCOE_TASK_TYPE_MIDPATH) {
			/* Initialize task context */
			data_desc = &task_ctx->mstorm_st_context.fp.data_desc;

			/* Set cache sges address and length */
			data_desc->slow.base_sgl_addr.lo =
			    U64_LO(mp_req->mp_resp_bd_dma);
			data_desc->slow.base_sgl_addr.hi =
			    U64_HI(mp_req->mp_resp_bd_dma);
			data_desc->slow.remainder_num_sges = 1;
			data_desc->slow.curr_sge_off = 0;
			data_desc->slow.curr_sgl_index = 0;

			/*
			 * Also need to fil in non-fastpath response address
			 * for middle path commands.
			 */
			task_ctx->mstorm_st_context.non_fp.rsp_buf_addr.lo =
			    U64_LO(mp_req->mp_resp_bd_dma);
			task_ctx->mstorm_st_context.non_fp.rsp_buf_addr.hi =
			    U64_HI(mp_req->mp_resp_bd_dma);
		}
	}

	/* USTORM ONLY */
	{
		task_ctx->ustorm_ag_context.global_cq_num = 0;
	}

	/* I/O stats. Middle path commands always use slow SGEs */
	qedf->slow_sge_ios++;
	io_req->sge_type = QEDF_IOREQ_SLOW_SGE;
}

void qedf_add_to_sq(struct qedf_rport *fcport, u16 xid, u32 ptu_invalidate,
	enum fcoe_task_type req_type, u32 offset)
{
	struct fcoe_wqe *sqe;
	uint16_t total_sqe = (fcport->sq_mem_size)/(sizeof(struct fcoe_wqe));

	sqe = &fcport->sq[fcport->sq_prod_idx];

	fcport->sq_prod_idx++;
	fcport->fw_sq_prod_idx++;
	if (fcport->sq_prod_idx == total_sqe)
		fcport->sq_prod_idx = 0;

	switch (req_type) {
	case FCOE_TASK_TYPE_WRITE_INITIATOR:
	case FCOE_TASK_TYPE_READ_INITIATOR:
		SET_FIELD(sqe->flags, FCOE_WQE_REQ_TYPE, SEND_FCOE_CMD);
		if (ptu_invalidate)
			SET_FIELD(sqe->flags, FCOE_WQE_INVALIDATE_PTU, 1);
		break;
	case FCOE_TASK_TYPE_MIDPATH:
		SET_FIELD(sqe->flags, FCOE_WQE_REQ_TYPE, SEND_FCOE_MIDPATH);
		break;
	case FCOE_TASK_TYPE_ABTS:
		SET_FIELD(sqe->flags, FCOE_WQE_REQ_TYPE,
		    SEND_FCOE_ABTS_REQUEST);
		break;
	case FCOE_TASK_TYPE_EXCHANGE_CLEANUP:
		SET_FIELD(sqe->flags, FCOE_WQE_REQ_TYPE,
		     FCOE_EXCHANGE_CLEANUP);
		break;
	case FCOE_TASK_TYPE_SEQUENCE_CLEANUP:
		SET_FIELD(sqe->flags, FCOE_WQE_REQ_TYPE,
		    FCOE_SEQUENCE_RECOVERY);
		/* NOTE: offset param only used for sequence recovery */
		sqe->additional_info_union.seq_rec_updated_offset = offset;
		break;
	case FCOE_TASK_TYPE_UNSOLICITED:
		break;
	default:
		break;
	}

	sqe->task_id = xid;

	/* Make sure SQ data is coherent */
	wmb();

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
	writel(*(u32 *)&dbell, fcport->p_doorbell);
	/* Make sure SQ index is updated so f/w prcesses requests in order */
	wmb();
	mmiowb();
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
	enum fcoe_task_type req_type = 0;
	u32 ptu_invalidate = 0;

	/* Initialize rest of io_req fileds */
	io_req->data_xfer_len = scsi_bufflen(sc_cmd);
	sc_cmd->SCp.ptr = (char *)io_req;
	io_req->use_slowpath = false; /* Assume fast SGL by default */

	/* Record which cpu this request is associated with */
	io_req->cpu = smp_processor_id();

	if (sc_cmd->sc_data_direction == DMA_FROM_DEVICE) {
		req_type = FCOE_TASK_TYPE_READ_INITIATOR;
		io_req->io_req_flags = QEDF_READ;
		qedf->input_requests++;
	} else if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
		req_type = FCOE_TASK_TYPE_WRITE_INITIATOR;
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
		kref_put(&io_req->refcount, qedf_release_cmd);
		return -EAGAIN;
	}

	/* Get the task context */
	task_ctx = qedf_get_task_mem(&qedf->tasks, xid);
	if (!task_ctx) {
		QEDF_WARN(&(qedf->dbg_ctx), "task_ctx is NULL, xid=%d.\n",
			   xid);
		kref_put(&io_req->refcount, qedf_release_cmd);
		return -EINVAL;
	}

	qedf_init_task(fcport, lport, io_req, &ptu_invalidate, task_ctx);

	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "Session not offloaded yet.\n");
		kref_put(&io_req->refcount, qedf_release_cmd);
	}

	/* Obtain free SQ entry */
	qedf_add_to_sq(fcport, xid, ptu_invalidate, req_type, 0);

	/* Ring doorbell */
	qedf_ring_doorbell(fcport);

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
	struct qedf_rport *fcport = rport->dd_data;
	struct qedf_ioreq *io_req;
	int rc = 0;
	int rval;
	unsigned long flags = 0;


	if (test_bit(QEDF_UNLOADING, &qedf->flags) ||
	    test_bit(QEDF_DBG_STOP_IO, &qedf->flags)) {
		sc_cmd->result = DID_NO_CONNECT << 16;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		sc_cmd->result = rval;
		sc_cmd->scsi_done(sc_cmd);
		return 0;
	}

	/* Retry command if we are doing a qed drain operation */
	if (test_bit(QEDF_DRAIN_ACTIVE, &qedf->flags)) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto exit_qcmd;
	}

	if (lport->state != LPORT_ST_READY ||
	    atomic_read(&qedf->link_state) != QEDF_LINK_UP) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto exit_qcmd;
	}

	/* rport and tgt are allocated together, so tgt should be non-NULL */
	fcport = (struct qedf_rport *)&rp[1];

	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		/*
		 * Session is not offloaded yet. Let SCSI-ml retry
		 * the command.
		 */
		rc = SCSI_MLQUEUE_TARGET_BUSY;
		goto exit_qcmd;
	}
	if (fcport->retry_delay_timestamp) {
		if (time_after(jiffies, fcport->retry_delay_timestamp)) {
			fcport->retry_delay_timestamp = 0;
		} else {
			/* If retry_delay timer is active, flow off the ML */
			rc = SCSI_MLQUEUE_TARGET_BUSY;
			goto exit_qcmd;
		}
	}

	io_req = qedf_alloc_cmd(fcport, QEDF_SCSI_CMD);
	if (!io_req) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
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

	memset(sc_cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
	if (fcp_sns_len)
		memcpy(sc_cmd->sense_buffer, sense_data,
		    fcp_sns_len);
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
	u16 xid, rval;
	struct fcoe_task_context *task_ctx;
	struct scsi_cmnd *sc_cmd;
	struct fcoe_cqe_rsp_info *fcp_rsp;
	struct qedf_rport *fcport;
	int refcount;
	u16 scope, qualifier = 0;
	u8 fw_residual_flag = 0;

	if (!io_req)
		return;
	if (!cqe)
		return;

	xid = io_req->xid;
	task_ctx = qedf_get_task_mem(&qedf->tasks, xid);
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

	if (!sc_cmd->request) {
		QEDF_WARN(&(qedf->dbg_ctx), "sc_cmd->request is NULL, "
		    "sc_cmd=%p.\n", sc_cmd);
		return;
	}

	if (!sc_cmd->request->special) {
		QEDF_WARN(&(qedf->dbg_ctx), "request->special is NULL so "
		    "request not valid, sc_cmd=%p.\n", sc_cmd);
		return;
	}

	if (!sc_cmd->request->q) {
		QEDF_WARN(&(qedf->dbg_ctx), "request->q is NULL so request "
		   "is not valid, sc_cmd=%p.\n", sc_cmd);
		return;
	}

	fcport = io_req->fcport;

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
		QEDF_ERR(&(qedf->dbg_ctx),
		    "Firmware detected underrun: xid=0x%x fcp_rsp.flags=0x%02x "
		    "fcp_resid=%d fw_residual=0x%x.\n", io_req->xid,
		    fcp_rsp->rsp_flags.flags, io_req->fcp_resid,
		    cqe->cqe_info.rsp_info.fw_residual);

		if (io_req->cdb_status == 0)
			sc_cmd->result = (DID_ERROR << 16) | io_req->cdb_status;
		else
			sc_cmd->result = (DID_OK << 16) | io_req->cdb_status;

		/* Abort the command since we did not get all the data */
		init_completion(&io_req->abts_done);
		rval = qedf_initiate_abts(io_req, true);
		if (rval) {
			QEDF_ERR(&(qedf->dbg_ctx), "Failed to queue ABTS.\n");
			sc_cmd->result = (DID_ERROR << 16) | io_req->cdb_status;
		}

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
			    "%d:0:%d:%d xid=0x%0x op=0x%02x "
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

				if (qedf_retry_delay &&
				    scope > 0 && qualifier > 0 &&
				    qualifier <= 0x3FEF) {
					/* Check we don't go over the max */
					if (qualifier > QEDF_RETRY_DELAY_MAX)
						qualifier =
						    QEDF_RETRY_DELAY_MAX;
					fcport->retry_delay_timestamp =
					    jiffies + (qualifier * HZ / 10);
				}
			}
		}
		if (io_req->fcp_resid)
			scsi_set_resid(sc_cmd, io_req->fcp_resid);
		break;
	default:
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "fcp_status=%d.\n",
			   io_req->fcp_status);
		break;
	}

out:
	if (qedf_io_tracing)
		qedf_trace_io(fcport, io_req, QEDF_IO_TRACE_RSP);

	io_req->sc_cmd = NULL;
	sc_cmd->SCp.ptr =  NULL;
	sc_cmd->scsi_done(sc_cmd);
	kref_put(&io_req->refcount, qedf_release_cmd);
}

/* Return a SCSI command in some other context besides a normal completion */
void qedf_scsi_done(struct qedf_ctx *qedf, struct qedf_ioreq *io_req,
	int result)
{
	u16 xid;
	struct scsi_cmnd *sc_cmd;
	int refcount;

	if (!io_req)
		return;

	xid = io_req->xid;
	sc_cmd = io_req->sc_cmd;

	if (!sc_cmd) {
		QEDF_WARN(&(qedf->dbg_ctx), "sc_cmd is NULL!\n");
		return;
	}

	if (!sc_cmd->SCp.ptr) {
		QEDF_WARN(&(qedf->dbg_ctx), "SCp.ptr is NULL, returned in "
		    "another context.\n");
		return;
	}

	qedf_unmap_sg_list(qedf, io_req);

	sc_cmd->result = result << 16;
	refcount = kref_read(&io_req->refcount);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "%d:0:%d:%d: Completing "
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

	if (!cqe)
		return;

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

	if (!cqe)
		return;

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

	if (!fcport)
		return;

	qedf = fcport->qedf;
	cmd_mgr = qedf->cmd_mgr;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "Flush active i/o's.\n");

	for (i = 0; i < FCOE_PARAMS_NUM_TASKS; i++) {
		io_req = &cmd_mgr->cmds[i];

		if (!io_req)
			continue;
		if (io_req->fcport != fcport)
			continue;
		if (io_req->cmd_type == QEDF_ELS) {
			rc = kref_get_unless_zero(&io_req->refcount);
			if (!rc) {
				QEDF_ERR(&(qedf->dbg_ctx),
				    "Could not get kref for io_req=0x%p.\n",
				    io_req);
				continue;
			}
			qedf_flush_els_req(qedf, io_req);
			/*
			 * Release the kref and go back to the top of the
			 * loop.
			 */
			goto free_cmd;
		}

		if (!io_req->sc_cmd)
			continue;
		if (lun > 0) {
			if (io_req->sc_cmd->device->lun !=
			    (u64)lun)
				continue;
		}

		/*
		 * Use kref_get_unless_zero in the unlikely case the command
		 * we're about to flush was completed in the normal SCSI path
		 */
		rc = kref_get_unless_zero(&io_req->refcount);
		if (!rc) {
			QEDF_ERR(&(qedf->dbg_ctx), "Could not get kref for "
			    "io_req=0x%p\n", io_req);
			continue;
		}
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO,
		    "Cleanup xid=0x%x.\n", io_req->xid);

		/* Cleanup task and return I/O mid-layer */
		qedf_initiate_cleanup(io_req, true);

free_cmd:
		kref_put(&io_req->refcount, qedf_release_cmd);
	}
}

/*
 * Initiate a ABTS middle path command. Note that we don't have to initialize
 * the task context for an ABTS task.
 */
int qedf_initiate_abts(struct qedf_ioreq *io_req, bool return_scsi_cmd_on_abts)
{
	struct fc_lport *lport;
	struct qedf_rport *fcport = io_req->fcport;
	struct fc_rport_priv *rdata = fcport->rdata;
	struct qedf_ctx *qedf = fcport->qedf;
	u16 xid;
	u32 r_a_tov = 0;
	int rc = 0;
	unsigned long flags;

	r_a_tov = rdata->r_a_tov;
	lport = qedf->lport;

	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "tgt not offloaded\n");
		rc = 1;
		goto abts_err;
	}

	if (lport->state != LPORT_ST_READY || !(lport->link_up)) {
		QEDF_ERR(&(qedf->dbg_ctx), "link is not ready\n");
		rc = 1;
		goto abts_err;
	}

	if (atomic_read(&qedf->link_down_tmo_valid) > 0) {
		QEDF_ERR(&(qedf->dbg_ctx), "link_down_tmo active.\n");
		rc = 1;
		goto abts_err;
	}

	/* Ensure room on SQ */
	if (!atomic_read(&fcport->free_sqes)) {
		QEDF_ERR(&(qedf->dbg_ctx), "No SQ entries available\n");
		rc = 1;
		goto abts_err;
	}


	kref_get(&io_req->refcount);

	xid = io_req->xid;
	qedf->control_requests++;
	qedf->packet_aborts++;

	/* Set the return CPU to be the same as the request one */
	io_req->cpu = smp_processor_id();

	/* Set the command type to abort */
	io_req->cmd_type = QEDF_ABTS;
	io_req->return_scsi_cmd_on_abts = return_scsi_cmd_on_abts;

	set_bit(QEDF_CMD_IN_ABORT, &io_req->flags);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM, "ABTS io_req xid = "
		   "0x%x\n", xid);

	qedf_cmd_timer_set(qedf, io_req, QEDF_ABORT_TIMEOUT * HZ);

	spin_lock_irqsave(&fcport->rport_lock, flags);

	/* Add ABTS to send queue */
	qedf_add_to_sq(fcport, xid, 0, FCOE_TASK_TYPE_ABTS, 0);

	/* Ring doorbell */
	qedf_ring_doorbell(fcport);

	spin_unlock_irqrestore(&fcport->rport_lock, flags);

	return rc;
abts_err:
	/*
	 * If the ABTS task fails to queue then we need to cleanup the
	 * task at the firmware.
	 */
	qedf_initiate_cleanup(io_req, return_scsi_cmd_on_abts);
	return rc;
}

void qedf_process_abts_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	uint32_t r_ctl;
	uint16_t xid;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM, "Entered with xid = "
		   "0x%x cmd_type = %d\n", io_req->xid, io_req->cmd_type);

	cancel_delayed_work(&io_req->timeout_work);

	xid = io_req->xid;
	r_ctl = cqe->cqe_info.abts_info.r_ctl;

	switch (r_ctl) {
	case FC_RCTL_BA_ACC:
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM,
		    "ABTS response - ACC Send RRQ after R_A_TOV\n");
		io_req->event = QEDF_IOREQ_EV_ABORT_SUCCESS;
		/*
		 * Dont release this cmd yet. It will be relesed
		 * after we get RRQ response
		 */
		kref_get(&io_req->refcount);
		queue_delayed_work(qedf->dpc_wq, &io_req->rrq_work,
		    msecs_to_jiffies(qedf->lport->r_a_tov));
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
	struct fcoe_sge *mp_req_bd;
	struct fcoe_sge *mp_resp_bd;
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
	sz = sizeof(struct fcoe_sge);
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
	mp_req_bd->size = QEDF_PAGE_SIZE;

	/*
	 * MP buffer is either a task mgmt command or an ELS.
	 * So the assumption is that it consumes a single bd
	 * entry in the bd table
	 */
	mp_resp_bd = mp_req->mp_resp_bd;
	addr = mp_req->resp_buf_dma;
	mp_resp_bd->sge_addr.lo = U64_LO(addr);
	mp_resp_bd->sge_addr.hi = U64_HI(addr);
	mp_resp_bd->size = QEDF_PAGE_SIZE;

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
	uint16_t xid;
	struct fcoe_task_context *task;
	int tmo = 0;
	int rc = SUCCESS;
	unsigned long flags;

	fcport = io_req->fcport;
	if (!fcport) {
		QEDF_ERR(NULL, "fcport is NULL.\n");
		return SUCCESS;
	}

	qedf = fcport->qedf;
	if (!qedf) {
		QEDF_ERR(NULL, "qedf is NULL.\n");
		return SUCCESS;
	}

	if (!test_bit(QEDF_CMD_OUTSTANDING, &io_req->flags) ||
	    test_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "io_req xid=0x%x already in "
			  "cleanup processing or already completed.\n",
			  io_req->xid);
		return SUCCESS;
	}

	/* Ensure room on SQ */
	if (!atomic_read(&fcport->free_sqes)) {
		QEDF_ERR(&(qedf->dbg_ctx), "No SQ entries available\n");
		return FAILED;
	}


	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_IO, "Entered xid=0x%x\n",
	    io_req->xid);

	/* Cleanup cmds re-use the same TID as the original I/O */
	xid = io_req->xid;
	io_req->cmd_type = QEDF_CLEANUP;
	io_req->return_scsi_cmd_on_abts = return_scsi_cmd_on_abts;

	/* Set the return CPU to be the same as the request one */
	io_req->cpu = smp_processor_id();

	set_bit(QEDF_CMD_IN_CLEANUP, &io_req->flags);

	task = qedf_get_task_mem(&qedf->tasks, xid);

	init_completion(&io_req->tm_done);

	/* Obtain free SQ entry */
	spin_lock_irqsave(&fcport->rport_lock, flags);
	qedf_add_to_sq(fcport, xid, 0, FCOE_TASK_TYPE_EXCHANGE_CLEANUP, 0);

	/* Ring doorbell */
	qedf_ring_doorbell(fcport);
	spin_unlock_irqrestore(&fcport->rport_lock, flags);

	tmo = wait_for_completion_timeout(&io_req->tm_done,
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

	if (io_req->sc_cmd) {
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
	complete(&io_req->tm_done);
}

static int qedf_execute_tmf(struct qedf_rport *fcport, struct scsi_cmnd *sc_cmd,
	uint8_t tm_flags)
{
	struct qedf_ioreq *io_req;
	struct qedf_mp_req *tm_req;
	struct fcoe_task_context *task;
	struct fc_frame_header *fc_hdr;
	struct fcp_cmnd *fcp_cmnd;
	struct qedf_ctx *qedf = fcport->qedf;
	int rc = 0;
	uint16_t xid;
	uint32_t sid, did;
	int tmo = 0;
	unsigned long flags;

	if (!sc_cmd) {
		QEDF_ERR(&(qedf->dbg_ctx), "invalid arg\n");
		return FAILED;
	}

	if (!(test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags))) {
		QEDF_ERR(&(qedf->dbg_ctx), "fcport not offloaded\n");
		rc = FAILED;
		return FAILED;
	}

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM, "portid = 0x%x "
		   "tm_flags = %d\n", fcport->rdata->ids.port_id, tm_flags);

	io_req = qedf_alloc_cmd(fcport, QEDF_TASK_MGMT_CMD);
	if (!io_req) {
		QEDF_ERR(&(qedf->dbg_ctx), "Failed TMF");
		rc = -EAGAIN;
		goto reset_tmf_err;
	}

	/* Initialize rest of io_req fields */
	io_req->sc_cmd = sc_cmd;
	io_req->fcport = fcport;
	io_req->cmd_type = QEDF_TASK_MGMT_CMD;

	/* Set the return CPU to be the same as the request one */
	io_req->cpu = smp_processor_id();

	tm_req = (struct qedf_mp_req *)&(io_req->mp_req);

	rc = qedf_init_mp_req(io_req);
	if (rc == FAILED) {
		QEDF_ERR(&(qedf->dbg_ctx), "Task mgmt MP request init "
			  "failed\n");
		kref_put(&io_req->refcount, qedf_release_cmd);
		goto reset_tmf_err;
	}

	/* Set TM flags */
	io_req->io_req_flags = 0;
	tm_req->tm_flags = tm_flags;

	/* Default is to return a SCSI command when an error occurs */
	io_req->return_scsi_cmd_on_abts = true;

	/* Fill FCP_CMND */
	qedf_build_fcp_cmnd(io_req, (struct fcp_cmnd *)tm_req->req_buf);
	fcp_cmnd = (struct fcp_cmnd *)tm_req->req_buf;
	memset(fcp_cmnd->fc_cdb, 0, FCP_CMND_LEN);
	fcp_cmnd->fc_dl = 0;

	/* Fill FC header */
	fc_hdr = &(tm_req->req_fc_hdr);
	sid = fcport->sid;
	did = fcport->rdata->ids.port_id;
	__fc_fill_fc_hdr(fc_hdr, FC_RCTL_DD_UNSOL_CMD, sid, did,
			   FC_TYPE_FCP, FC_FC_FIRST_SEQ | FC_FC_END_SEQ |
			   FC_FC_SEQ_INIT, 0);
	/* Obtain exchange id */
	xid = io_req->xid;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM, "TMF io_req xid = "
		   "0x%x\n", xid);

	/* Initialize task context for this IO request */
	task = qedf_get_task_mem(&qedf->tasks, xid);
	qedf_init_mp_task(io_req, task);

	init_completion(&io_req->tm_done);

	/* Obtain free SQ entry */
	spin_lock_irqsave(&fcport->rport_lock, flags);
	qedf_add_to_sq(fcport, xid, 0, FCOE_TASK_TYPE_MIDPATH, 0);

	/* Ring doorbell */
	qedf_ring_doorbell(fcport);
	spin_unlock_irqrestore(&fcport->rport_lock, flags);

	tmo = wait_for_completion_timeout(&io_req->tm_done,
	    QEDF_TM_TIMEOUT * HZ);

	if (!tmo) {
		rc = FAILED;
		QEDF_ERR(&(qedf->dbg_ctx), "wait for tm_cmpl timeout!\n");
	} else {
		/* Check TMF response code */
		if (io_req->fcp_rsp_code == 0)
			rc = SUCCESS;
		else
			rc = FAILED;
	}

	if (tm_flags == FCP_TMF_LUN_RESET)
		qedf_flush_active_ios(fcport, (int)sc_cmd->device->lun);
	else
		qedf_flush_active_ios(fcport, -1);

	kref_put(&io_req->refcount, qedf_release_cmd);

	if (rc != SUCCESS) {
		QEDF_ERR(&(qedf->dbg_ctx), "task mgmt command failed...\n");
		rc = FAILED;
	} else {
		QEDF_ERR(&(qedf->dbg_ctx), "task mgmt command success...\n");
		rc = SUCCESS;
	}
reset_tmf_err:
	return rc;
}

int qedf_initiate_tmf(struct scsi_cmnd *sc_cmd, u8 tm_flags)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sc_cmd->device));
	struct fc_rport_libfc_priv *rp = rport->dd_data;
	struct qedf_rport *fcport = (struct qedf_rport *)&rp[1];
	struct qedf_ctx *qedf;
	struct fc_lport *lport;
	int rc = SUCCESS;
	int rval;

	rval = fc_remote_port_chkready(rport);

	if (rval) {
		QEDF_ERR(NULL, "device_reset rport not ready\n");
		rc = FAILED;
		goto tmf_err;
	}

	if (fcport == NULL) {
		QEDF_ERR(NULL, "device_reset: rport is NULL\n");
		rc = FAILED;
		goto tmf_err;
	}

	qedf = fcport->qedf;
	lport = qedf->lport;

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

	rc = qedf_execute_tmf(fcport, sc_cmd, tm_flags);

tmf_err:
	return rc;
}

void qedf_process_tmf_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *io_req)
{
	struct fcoe_cqe_rsp_info *fcp_rsp;
	struct fcoe_cqe_midpath_info *mp_info;


	/* Get TMF response length from CQE */
	mp_info = &cqe->cqe_info.midpath_info;
	io_req->mp_req.resp_len = mp_info->data_placement_size;
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_SCSI_TM,
	    "Response len is %d.\n", io_req->mp_req.resp_len);

	fcp_rsp = &cqe->cqe_info.rsp_info;
	qedf_parse_fcp_rsp(io_req, fcp_rsp);

	io_req->sc_cmd = NULL;
	complete(&io_req->tm_done);
}

void qedf_process_unsol_compl(struct qedf_ctx *qedf, uint16_t que_idx,
	struct fcoe_cqe *cqe)
{
	unsigned long flags;
	uint16_t tmp;
	uint16_t pktlen = cqe->cqe_info.unsolic_info.pkt_len;
	u32 payload_len, crc;
	struct fc_frame_header *fh;
	struct fc_frame *fp;
	struct qedf_io_work *io_work;
	u32 bdq_idx;
	void *bdq_addr;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_UNSOL,
	    "address.hi=%x address.lo=%x opaque_data.hi=%x "
	    "opaque_data.lo=%x bdq_prod_idx=%u len=%u.\n",
	    le32_to_cpu(cqe->cqe_info.unsolic_info.bd_info.address.hi),
	    le32_to_cpu(cqe->cqe_info.unsolic_info.bd_info.address.lo),
	    le32_to_cpu(cqe->cqe_info.unsolic_info.bd_info.opaque.hi),
	    le32_to_cpu(cqe->cqe_info.unsolic_info.bd_info.opaque.lo),
	    qedf->bdq_prod_idx, pktlen);

	bdq_idx = le32_to_cpu(cqe->cqe_info.unsolic_info.bd_info.opaque.lo);
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
	tmp = readw(qedf->bdq_primary_prod);
	writew(qedf->bdq_prod_idx, qedf->bdq_secondary_prod);
	tmp = readw(qedf->bdq_secondary_prod);

	spin_unlock_irqrestore(&qedf->hba_lock, flags);
}

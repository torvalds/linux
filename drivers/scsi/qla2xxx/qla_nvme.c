// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2017 QLogic Corporation
 */
#include "qla_nvme.h"
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/nvme.h>
#include <linux/nvme-fc.h>
#include <linux/blk-mq.h>

static struct nvme_fc_port_template qla_nvme_fc_transport;
static int qla_nvme_ls_reject_iocb(struct scsi_qla_host *vha,
				   struct qla_qpair *qp,
				   struct qla_nvme_lsrjt_pt_arg *a,
				   bool is_xchg_terminate);

struct qla_nvme_unsol_ctx {
	struct list_head elem;
	struct scsi_qla_host *vha;
	struct fc_port *fcport;
	struct srb *sp;
	struct nvmefc_ls_rsp lsrsp;
	struct nvmefc_ls_rsp *fd_rsp;
	struct work_struct lsrsp_work;
	struct work_struct abort_work;
	__le32 exchange_address;
	__le16 nport_handle;
	__le16 ox_id;
	int comp_status;
	spinlock_t cmd_lock;
};

int qla_nvme_register_remote(struct scsi_qla_host *vha, struct fc_port *fcport)
{
	struct qla_nvme_rport *rport;
	struct nvme_fc_port_info req;
	int ret;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return 0;

	if (!vha->flags.nvme_enabled) {
		ql_log(ql_log_info, vha, 0x2100,
		    "%s: Not registering target since Host NVME is not enabled\n",
		    __func__);
		return 0;
	}

	if (qla_nvme_register_hba(vha))
		return 0;

	if (!vha->nvme_local_port)
		return 0;

	if (!(fcport->nvme_prli_service_param &
	    (NVME_PRLI_SP_TARGET | NVME_PRLI_SP_DISCOVERY)) ||
		(fcport->nvme_flag & NVME_FLAG_REGISTERED))
		return 0;

	fcport->nvme_flag &= ~NVME_FLAG_RESETTING;

	memset(&req, 0, sizeof(struct nvme_fc_port_info));
	req.port_name = wwn_to_u64(fcport->port_name);
	req.node_name = wwn_to_u64(fcport->node_name);
	req.port_role = 0;
	req.dev_loss_tmo = fcport->dev_loss_tmo;

	if (fcport->nvme_prli_service_param & NVME_PRLI_SP_INITIATOR)
		req.port_role = FC_PORT_ROLE_NVME_INITIATOR;

	if (fcport->nvme_prli_service_param & NVME_PRLI_SP_TARGET)
		req.port_role |= FC_PORT_ROLE_NVME_TARGET;

	if (fcport->nvme_prli_service_param & NVME_PRLI_SP_DISCOVERY)
		req.port_role |= FC_PORT_ROLE_NVME_DISCOVERY;

	req.port_id = fcport->d_id.b24;

	ql_log(ql_log_info, vha, 0x2102,
	    "%s: traddr=nn-0x%016llx:pn-0x%016llx PortID:%06x\n",
	    __func__, req.node_name, req.port_name,
	    req.port_id);

	ret = nvme_fc_register_remoteport(vha->nvme_local_port, &req,
	    &fcport->nvme_remote_port);
	if (ret) {
		ql_log(ql_log_warn, vha, 0x212e,
		    "Failed to register remote port. Transport returned %d\n",
		    ret);
		return ret;
	}

	nvme_fc_set_remoteport_devloss(fcport->nvme_remote_port,
				       fcport->dev_loss_tmo);

	if (fcport->nvme_prli_service_param & NVME_PRLI_SP_SLER)
		ql_log(ql_log_info, vha, 0x212a,
		       "PortID:%06x Supports SLER\n", req.port_id);

	if (fcport->nvme_prli_service_param & NVME_PRLI_SP_PI_CTRL)
		ql_log(ql_log_info, vha, 0x212b,
		       "PortID:%06x Supports PI control\n", req.port_id);

	rport = fcport->nvme_remote_port->private;
	rport->fcport = fcport;

	fcport->nvme_flag |= NVME_FLAG_REGISTERED;
	return 0;
}

/* Allocate a queue for NVMe traffic */
static int qla_nvme_alloc_queue(struct nvme_fc_local_port *lport,
    unsigned int qidx, u16 qsize, void **handle)
{
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha;
	struct qla_qpair *qpair;

	/* Map admin queue and 1st IO queue to index 0 */
	if (qidx)
		qidx--;

	vha = (struct scsi_qla_host *)lport->private;
	ha = vha->hw;

	ql_log(ql_log_info, vha, 0x2104,
	    "%s: handle %p, idx =%d, qsize %d\n",
	    __func__, handle, qidx, qsize);

	if (qidx > qla_nvme_fc_transport.max_hw_queues) {
		ql_log(ql_log_warn, vha, 0x212f,
		    "%s: Illegal qidx=%d. Max=%d\n",
		    __func__, qidx, qla_nvme_fc_transport.max_hw_queues);
		return -EINVAL;
	}

	/* Use base qpair if max_qpairs is 0 */
	if (!ha->max_qpairs) {
		qpair = ha->base_qpair;
	} else {
		if (ha->queue_pair_map[qidx]) {
			*handle = ha->queue_pair_map[qidx];
			ql_log(ql_log_info, vha, 0x2121,
			       "Returning existing qpair of %p for idx=%x\n",
			       *handle, qidx);
			return 0;
		}

		qpair = qla2xxx_create_qpair(vha, 5, vha->vp_idx, true);
		if (!qpair) {
			ql_log(ql_log_warn, vha, 0x2122,
			       "Failed to allocate qpair\n");
			return -EINVAL;
		}
		qla_adjust_iocb_limit(vha);
	}
	*handle = qpair;

	return 0;
}

static void qla_nvme_release_fcp_cmd_kref(struct kref *kref)
{
	struct srb *sp = container_of(kref, struct srb, cmd_kref);
	struct nvme_private *priv = (struct nvme_private *)sp->priv;
	struct nvmefc_fcp_req *fd;
	struct srb_iocb *nvme;
	unsigned long flags;

	if (!priv)
		goto out;

	nvme = &sp->u.iocb_cmd;
	fd = nvme->u.nvme.desc;

	spin_lock_irqsave(&priv->cmd_lock, flags);
	priv->sp = NULL;
	sp->priv = NULL;
	if (priv->comp_status == QLA_SUCCESS) {
		fd->rcv_rsplen = le16_to_cpu(nvme->u.nvme.rsp_pyld_len);
		fd->status = NVME_SC_SUCCESS;
	} else {
		fd->rcv_rsplen = 0;
		fd->transferred_length = 0;
		fd->status = NVME_SC_INTERNAL;
	}
	spin_unlock_irqrestore(&priv->cmd_lock, flags);

	fd->done(fd);
out:
	qla2xxx_rel_qpair_sp(sp->qpair, sp);
}

static void qla_nvme_release_ls_cmd_kref(struct kref *kref)
{
	struct srb *sp = container_of(kref, struct srb, cmd_kref);
	struct nvme_private *priv = (struct nvme_private *)sp->priv;
	struct nvmefc_ls_req *fd;
	unsigned long flags;

	if (!priv)
		goto out;

	spin_lock_irqsave(&priv->cmd_lock, flags);
	priv->sp = NULL;
	sp->priv = NULL;
	spin_unlock_irqrestore(&priv->cmd_lock, flags);

	fd = priv->fd;

	fd->done(fd, priv->comp_status);
out:
	qla2x00_rel_sp(sp);
}

static void qla_nvme_ls_complete(struct work_struct *work)
{
	struct nvme_private *priv =
		container_of(work, struct nvme_private, ls_work);

	kref_put(&priv->sp->cmd_kref, qla_nvme_release_ls_cmd_kref);
}

static void qla_nvme_sp_ls_done(srb_t *sp, int res)
{
	struct nvme_private *priv = sp->priv;

	if (WARN_ON_ONCE(kref_read(&sp->cmd_kref) == 0))
		return;

	if (res)
		res = -EINVAL;

	priv->comp_status = res;
	INIT_WORK(&priv->ls_work, qla_nvme_ls_complete);
	schedule_work(&priv->ls_work);
}

static void qla_nvme_release_lsrsp_cmd_kref(struct kref *kref)
{
	struct srb *sp = container_of(kref, struct srb, cmd_kref);
	struct qla_nvme_unsol_ctx *uctx = sp->priv;
	struct nvmefc_ls_rsp *fd_rsp;
	unsigned long flags;

	if (!uctx) {
		qla2x00_rel_sp(sp);
		return;
	}

	spin_lock_irqsave(&uctx->cmd_lock, flags);
	uctx->sp = NULL;
	sp->priv = NULL;
	spin_unlock_irqrestore(&uctx->cmd_lock, flags);

	fd_rsp = uctx->fd_rsp;

	list_del(&uctx->elem);

	fd_rsp->done(fd_rsp);
	kfree(uctx);
	qla2x00_rel_sp(sp);
}

static void qla_nvme_lsrsp_complete(struct work_struct *work)
{
	struct qla_nvme_unsol_ctx *uctx =
		container_of(work, struct qla_nvme_unsol_ctx, lsrsp_work);

	kref_put(&uctx->sp->cmd_kref, qla_nvme_release_lsrsp_cmd_kref);
}

static void qla_nvme_sp_lsrsp_done(srb_t *sp, int res)
{
	struct qla_nvme_unsol_ctx *uctx = sp->priv;

	if (WARN_ON_ONCE(kref_read(&sp->cmd_kref) == 0))
		return;

	if (res)
		res = -EINVAL;

	uctx->comp_status = res;
	INIT_WORK(&uctx->lsrsp_work, qla_nvme_lsrsp_complete);
	schedule_work(&uctx->lsrsp_work);
}

/* it assumed that QPair lock is held. */
static void qla_nvme_sp_done(srb_t *sp, int res)
{
	struct nvme_private *priv = sp->priv;

	priv->comp_status = res;
	kref_put(&sp->cmd_kref, qla_nvme_release_fcp_cmd_kref);

	return;
}

static void qla_nvme_abort_work(struct work_struct *work)
{
	struct nvme_private *priv =
		container_of(work, struct nvme_private, abort_work);
	srb_t *sp = priv->sp;
	fc_port_t *fcport = sp->fcport;
	struct qla_hw_data *ha = fcport->vha->hw;
	int rval, abts_done_called = 1;
	bool io_wait_for_abort_done;
	uint32_t handle;

	ql_dbg(ql_dbg_io, fcport->vha, 0xffff,
	       "%s called for sp=%p, hndl=%x on fcport=%p desc=%p deleted=%d\n",
	       __func__, sp, sp->handle, fcport, sp->u.iocb_cmd.u.nvme.desc, fcport->deleted);

	if (!ha->flags.fw_started || fcport->deleted == QLA_SESS_DELETED)
		goto out;

	if (ha->flags.host_shutting_down) {
		ql_log(ql_log_info, sp->fcport->vha, 0xffff,
		    "%s Calling done on sp: %p, type: 0x%x\n",
		    __func__, sp, sp->type);
		sp->done(sp, 0);
		goto out;
	}

	/*
	 * sp may not be valid after abort_command if return code is either
	 * SUCCESS or ERR_FROM_FW codes, so cache the value here.
	 */
	io_wait_for_abort_done = ql2xabts_wait_nvme &&
					QLA_ABTS_WAIT_ENABLED(sp);
	handle = sp->handle;

	rval = ha->isp_ops->abort_command(sp);

	ql_dbg(ql_dbg_io, fcport->vha, 0x212b,
	    "%s: %s command for sp=%p, handle=%x on fcport=%p rval=%x\n",
	    __func__, (rval != QLA_SUCCESS) ? "Failed to abort" : "Aborted",
	    sp, handle, fcport, rval);

	/*
	 * If async tmf is enabled, the abort callback is called only on
	 * return codes QLA_SUCCESS and QLA_ERR_FROM_FW.
	 */
	if (ql2xasynctmfenable &&
	    rval != QLA_SUCCESS && rval != QLA_ERR_FROM_FW)
		abts_done_called = 0;

	/*
	 * Returned before decreasing kref so that I/O requests
	 * are waited until ABTS complete. This kref is decreased
	 * at qla24xx_abort_sp_done function.
	 */
	if (abts_done_called && io_wait_for_abort_done)
		return;
out:
	/* kref_get was done before work was schedule. */
	kref_put(&sp->cmd_kref, sp->put_fn);
}

static int qla_nvme_xmt_ls_rsp(struct nvme_fc_local_port *lport,
			       struct nvme_fc_remote_port *rport,
			       struct nvmefc_ls_rsp *fd_resp)
{
	struct qla_nvme_unsol_ctx *uctx = container_of(fd_resp,
				struct qla_nvme_unsol_ctx, lsrsp);
	struct qla_nvme_rport *qla_rport = rport->private;
	fc_port_t *fcport = qla_rport->fcport;
	struct scsi_qla_host *vha = uctx->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_nvme_lsrjt_pt_arg a;
	struct srb_iocb *nvme;
	srb_t *sp;
	int rval = QLA_FUNCTION_FAILED;
	uint8_t cnt = 0;

	if (!fcport || fcport->deleted)
		goto out;

	if (!ha->flags.fw_started)
		goto out;

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_ATOMIC);
	if (!sp)
		goto out;

	sp->type = SRB_NVME_LS;
	sp->name = "nvme_ls";
	sp->done = qla_nvme_sp_lsrsp_done;
	sp->put_fn = qla_nvme_release_lsrsp_cmd_kref;
	sp->priv = (void *)uctx;
	sp->unsol_rsp = 1;
	uctx->sp = sp;
	spin_lock_init(&uctx->cmd_lock);
	nvme = &sp->u.iocb_cmd;
	uctx->fd_rsp = fd_resp;
	nvme->u.nvme.desc = fd_resp;
	nvme->u.nvme.dir = 0;
	nvme->u.nvme.dl = 0;
	nvme->u.nvme.timeout_sec = 0;
	nvme->u.nvme.cmd_dma = fd_resp->rspdma;
	nvme->u.nvme.cmd_len = cpu_to_le32(fd_resp->rsplen);
	nvme->u.nvme.rsp_len = 0;
	nvme->u.nvme.rsp_dma = 0;
	nvme->u.nvme.exchange_address = uctx->exchange_address;
	nvme->u.nvme.nport_handle = uctx->nport_handle;
	nvme->u.nvme.ox_id = uctx->ox_id;
	dma_sync_single_for_device(&ha->pdev->dev, nvme->u.nvme.cmd_dma,
				   fd_resp->rsplen, DMA_TO_DEVICE);

	ql_dbg(ql_dbg_unsol, vha, 0x2122,
	       "Unsol lsreq portid=%06x %8phC exchange_address 0x%x ox_id 0x%x hdl 0x%x\n",
	       fcport->d_id.b24, fcport->port_name, uctx->exchange_address,
	       uctx->ox_id, uctx->nport_handle);
retry:
	rval = qla2x00_start_sp(sp);
	switch (rval) {
	case QLA_SUCCESS:
		break;
	case EAGAIN:
		msleep(PURLS_MSLEEP_INTERVAL);
		cnt++;
		if (cnt < PURLS_RETRY_COUNT)
			goto retry;

		fallthrough;
	default:
		ql_dbg(ql_log_warn, vha, 0x2123,
		       "Failed to xmit Unsol ls response = %d\n", rval);
		rval = -EIO;
		qla2x00_rel_sp(sp);
		goto out;
	}

	return 0;
out:
	memset((void *)&a, 0, sizeof(a));
	a.vp_idx = vha->vp_idx;
	a.nport_handle = uctx->nport_handle;
	a.xchg_address = uctx->exchange_address;
	qla_nvme_ls_reject_iocb(vha, ha->base_qpair, &a, true);
	kfree(uctx);
	return rval;
}

static void qla_nvme_ls_abort(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, struct nvmefc_ls_req *fd)
{
	struct nvme_private *priv = fd->private;
	unsigned long flags;

	spin_lock_irqsave(&priv->cmd_lock, flags);
	if (!priv->sp) {
		spin_unlock_irqrestore(&priv->cmd_lock, flags);
		return;
	}

	if (!kref_get_unless_zero(&priv->sp->cmd_kref)) {
		spin_unlock_irqrestore(&priv->cmd_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->cmd_lock, flags);

	INIT_WORK(&priv->abort_work, qla_nvme_abort_work);
	schedule_work(&priv->abort_work);
}

static int qla_nvme_ls_req(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, struct nvmefc_ls_req *fd)
{
	struct qla_nvme_rport *qla_rport = rport->private;
	fc_port_t *fcport = qla_rport->fcport;
	struct srb_iocb   *nvme;
	struct nvme_private *priv = fd->private;
	struct scsi_qla_host *vha;
	int     rval = QLA_FUNCTION_FAILED;
	struct qla_hw_data *ha;
	srb_t           *sp;

	if (!fcport || fcport->deleted)
		return rval;

	vha = fcport->vha;
	ha = vha->hw;

	if (!ha->flags.fw_started)
		return rval;

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_ATOMIC);
	if (!sp)
		return rval;

	sp->type = SRB_NVME_LS;
	sp->name = "nvme_ls";
	sp->done = qla_nvme_sp_ls_done;
	sp->put_fn = qla_nvme_release_ls_cmd_kref;
	sp->priv = priv;
	priv->sp = sp;
	kref_init(&sp->cmd_kref);
	spin_lock_init(&priv->cmd_lock);
	nvme = &sp->u.iocb_cmd;
	priv->fd = fd;
	nvme->u.nvme.desc = fd;
	nvme->u.nvme.dir = 0;
	nvme->u.nvme.dl = 0;
	nvme->u.nvme.cmd_len = cpu_to_le32(fd->rqstlen);
	nvme->u.nvme.rsp_len = cpu_to_le32(fd->rsplen);
	nvme->u.nvme.rsp_dma = fd->rspdma;
	nvme->u.nvme.timeout_sec = fd->timeout;
	nvme->u.nvme.cmd_dma = fd->rqstdma;
	dma_sync_single_for_device(&ha->pdev->dev, nvme->u.nvme.cmd_dma,
	    fd->rqstlen, DMA_TO_DEVICE);

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x700e,
		    "qla2x00_start_sp failed = %d\n", rval);
		sp->priv = NULL;
		priv->sp = NULL;
		qla2x00_rel_sp(sp);
		return rval;
	}

	return rval;
}

static void qla_nvme_fcp_abort(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, void *hw_queue_handle,
    struct nvmefc_fcp_req *fd)
{
	struct nvme_private *priv = fd->private;
	unsigned long flags;

	spin_lock_irqsave(&priv->cmd_lock, flags);
	if (!priv->sp) {
		spin_unlock_irqrestore(&priv->cmd_lock, flags);
		return;
	}
	if (!kref_get_unless_zero(&priv->sp->cmd_kref)) {
		spin_unlock_irqrestore(&priv->cmd_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->cmd_lock, flags);

	INIT_WORK(&priv->abort_work, qla_nvme_abort_work);
	schedule_work(&priv->abort_work);
}

static inline int qla2x00_start_nvme_mq(srb_t *sp)
{
	unsigned long   flags;
	uint32_t        *clr_ptr;
	uint32_t        handle;
	struct cmd_nvme *cmd_pkt;
	uint16_t        cnt, i;
	uint16_t        req_cnt;
	uint16_t        tot_dsds;
	uint16_t	avail_dsds;
	struct dsd64	*cur_dsd;
	struct req_que *req = NULL;
	struct rsp_que *rsp = NULL;
	struct scsi_qla_host *vha = sp->fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_qpair *qpair = sp->qpair;
	struct srb_iocb *nvme = &sp->u.iocb_cmd;
	struct scatterlist *sgl, *sg;
	struct nvmefc_fcp_req *fd = nvme->u.nvme.desc;
	struct nvme_fc_cmd_iu *cmd = fd->cmdaddr;
	uint32_t        rval = QLA_SUCCESS;

	/* Setup qpair pointers */
	req = qpair->req;
	rsp = qpair->rsp;
	tot_dsds = fd->sg_cnt;

	/* Acquire qpair specific lock */
	spin_lock_irqsave(&qpair->qp_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0) {
		rval = -EBUSY;
		goto queuing_error;
	}
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);

	sp->iores.res_type = RESOURCE_IOCB | RESOURCE_EXCH;
	sp->iores.exch_cnt = 1;
	sp->iores.iocb_cnt = req_cnt;
	if (qla_get_fw_resources(sp->qpair, &sp->iores)) {
		rval = -EBUSY;
		goto queuing_error;
	}

	if (req->cnt < (req_cnt + 2)) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt)) {
				rval = -EBUSY;
				goto queuing_error;
			}
		}

		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length - (req->ring_index - cnt);

		if (req->cnt < (req_cnt + 2)){
			rval = -EBUSY;
			goto queuing_error;
		}
	}

	if (unlikely(!fd->sqid)) {
		if (cmd->sqe.common.opcode == nvme_admin_async_event) {
			nvme->u.nvme.aen_op = 1;
			atomic_inc(&ha->nvme_active_aen_cnt);
		}
	}

	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	req->cnt -= req_cnt;

	cmd_pkt = (struct cmd_nvme *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	/* Zero out remaining portion of packet. */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);

	cmd_pkt->entry_status = 0;

	/* Update entry type to indicate Command NVME IOCB */
	cmd_pkt->entry_type = COMMAND_NVME;

	/* No data transfer how do we check buffer len == 0?? */
	if (fd->io_dir == NVMEFC_FCP_READ) {
		cmd_pkt->control_flags = cpu_to_le16(CF_READ_DATA);
		qpair->counters.input_bytes += fd->payload_length;
		qpair->counters.input_requests++;
	} else if (fd->io_dir == NVMEFC_FCP_WRITE) {
		cmd_pkt->control_flags = cpu_to_le16(CF_WRITE_DATA);
		if ((vha->flags.nvme_first_burst) &&
		    (sp->fcport->nvme_prli_service_param &
			NVME_PRLI_SP_FIRST_BURST)) {
			if ((fd->payload_length <=
			    sp->fcport->nvme_first_burst_size) ||
				(sp->fcport->nvme_first_burst_size == 0))
				cmd_pkt->control_flags |=
					cpu_to_le16(CF_NVME_FIRST_BURST_ENABLE);
		}
		qpair->counters.output_bytes += fd->payload_length;
		qpair->counters.output_requests++;
	} else if (fd->io_dir == 0) {
		cmd_pkt->control_flags = 0;
	}

	if (sp->fcport->edif.enable && fd->io_dir != 0)
		cmd_pkt->control_flags |= cpu_to_le16(CF_EN_EDIF);

	/* Set BIT_13 of control flags for Async event */
	if (vha->flags.nvme2_enabled &&
	    cmd->sqe.common.opcode == nvme_admin_async_event) {
		cmd_pkt->control_flags |= cpu_to_le16(CF_ADMIN_ASYNC_EVENT);
	}

	/* Set NPORT-ID */
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
	cmd_pkt->vp_index = sp->fcport->vha->vp_idx;

	/* NVME RSP IU */
	cmd_pkt->nvme_rsp_dsd_len = cpu_to_le16(fd->rsplen);
	put_unaligned_le64(fd->rspdma, &cmd_pkt->nvme_rsp_dseg_address);

	/* NVME CNMD IU */
	cmd_pkt->nvme_cmnd_dseg_len = cpu_to_le16(fd->cmdlen);
	cmd_pkt->nvme_cmnd_dseg_address = cpu_to_le64(fd->cmddma);

	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);
	cmd_pkt->byte_count = cpu_to_le32(fd->payload_length);

	/* One DSD is available in the Command Type NVME IOCB */
	avail_dsds = 1;
	cur_dsd = &cmd_pkt->nvme_dsd;
	sgl = fd->first_sgl;

	/* Load data segments */
	for_each_sg(sgl, sg, tot_dsds, i) {
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			 * Five DSDs are available in the Continuation
			 * Type 1 IOCB.
			 */

			/* Adjust ring index */
			req->ring_index++;
			if (req->ring_index == req->length) {
				req->ring_index = 0;
				req->ring_ptr = req->ring;
			} else {
				req->ring_ptr++;
			}
			cont_pkt = (cont_a64_entry_t *)req->ring_ptr;
			put_unaligned_le32(CONTINUE_A64_TYPE,
					   &cont_pkt->entry_type);

			cur_dsd = cont_pkt->dsd;
			avail_dsds = ARRAY_SIZE(cont_pkt->dsd);
		}

		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}

	/* Set total entry count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else {
		req->ring_ptr++;
	}

	/* ignore nvme async cmd due to long timeout */
	if (!nvme->u.nvme.aen_op)
		sp->qpair->cmd_cnt++;

	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

queuing_error:
	if (rval)
		qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(&qpair->qp_lock, flags);

	return rval;
}

/* Post a command */
static int qla_nvme_post_cmd(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, void *hw_queue_handle,
    struct nvmefc_fcp_req *fd)
{
	fc_port_t *fcport;
	struct srb_iocb *nvme;
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha;
	int rval;
	srb_t *sp;
	struct qla_qpair *qpair = hw_queue_handle;
	struct nvme_private *priv = fd->private;
	struct qla_nvme_rport *qla_rport = rport->private;

	if (!priv) {
		/* nvme association has been torn down */
		return -ENODEV;
	}

	fcport = qla_rport->fcport;

	if (unlikely(!qpair || !fcport || fcport->deleted))
		return -EBUSY;

	if (!(fcport->nvme_flag & NVME_FLAG_REGISTERED))
		return -ENODEV;

	vha = fcport->vha;
	ha = vha->hw;

	if (test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags))
		return -EBUSY;

	/*
	 * If we know the dev is going away while the transport is still sending
	 * IO's return busy back to stall the IO Q.  This happens when the
	 * link goes away and fw hasn't notified us yet, but IO's are being
	 * returned. If the dev comes back quickly we won't exhaust the IO
	 * retry count at the core.
	 */
	if (fcport->nvme_flag & NVME_FLAG_RESETTING)
		return -EBUSY;

	qpair = qla_mapq_nvme_select_qpair(ha, qpair);

	/* Alloc SRB structure */
	sp = qla2xxx_get_qpair_sp(vha, qpair, fcport, GFP_ATOMIC);
	if (!sp)
		return -EBUSY;

	kref_init(&sp->cmd_kref);
	spin_lock_init(&priv->cmd_lock);
	sp->priv = priv;
	priv->sp = sp;
	sp->type = SRB_NVME_CMD;
	sp->name = "nvme_cmd";
	sp->done = qla_nvme_sp_done;
	sp->put_fn = qla_nvme_release_fcp_cmd_kref;
	sp->qpair = qpair;
	sp->vha = vha;
	sp->cmd_sp = sp;
	nvme = &sp->u.iocb_cmd;
	nvme->u.nvme.desc = fd;

	rval = qla2x00_start_nvme_mq(sp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_io + ql_dbg_verbose, vha, 0x212d,
		    "qla2x00_start_nvme_mq failed = %d\n", rval);
		sp->priv = NULL;
		priv->sp = NULL;
		qla2xxx_rel_qpair_sp(sp->qpair, sp);
	}

	return rval;
}

static void qla_nvme_map_queues(struct nvme_fc_local_port *lport,
		struct blk_mq_queue_map *map)
{
	struct scsi_qla_host *vha = lport->private;

	blk_mq_map_hw_queues(map, &vha->hw->pdev->dev, vha->irq_offset);
}

static void qla_nvme_localport_delete(struct nvme_fc_local_port *lport)
{
	struct scsi_qla_host *vha = lport->private;

	ql_log(ql_log_info, vha, 0x210f,
	    "localport delete of %p completed.\n", vha->nvme_local_port);
	vha->nvme_local_port = NULL;
	complete(&vha->nvme_del_done);
}

static void qla_nvme_remoteport_delete(struct nvme_fc_remote_port *rport)
{
	fc_port_t *fcport;
	struct qla_nvme_rport *qla_rport = rport->private;

	fcport = qla_rport->fcport;
	fcport->nvme_remote_port = NULL;
	fcport->nvme_flag &= ~NVME_FLAG_REGISTERED;
	fcport->nvme_flag &= ~NVME_FLAG_DELETING;
	ql_log(ql_log_info, fcport->vha, 0x2110,
	    "remoteport_delete of %p %8phN completed.\n",
	    fcport, fcport->port_name);
	complete(&fcport->nvme_del_done);
}

static struct nvme_fc_port_template qla_nvme_fc_transport = {
	.localport_delete = qla_nvme_localport_delete,
	.remoteport_delete = qla_nvme_remoteport_delete,
	.create_queue   = qla_nvme_alloc_queue,
	.delete_queue 	= NULL,
	.ls_req		= qla_nvme_ls_req,
	.ls_abort	= qla_nvme_ls_abort,
	.fcp_io		= qla_nvme_post_cmd,
	.fcp_abort	= qla_nvme_fcp_abort,
	.xmt_ls_rsp	= qla_nvme_xmt_ls_rsp,
	.map_queues	= qla_nvme_map_queues,
	.max_hw_queues  = DEF_NVME_HW_QUEUES,
	.max_sgl_segments = 1024,
	.max_dif_sgl_segments = 64,
	.dma_boundary = 0xFFFFFFFF,
	.local_priv_sz  = 8,
	.remote_priv_sz = sizeof(struct qla_nvme_rport),
	.lsrqst_priv_sz = sizeof(struct nvme_private),
	.fcprqst_priv_sz = sizeof(struct nvme_private),
};

void qla_nvme_unregister_remote_port(struct fc_port *fcport)
{
	int ret;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;

	ql_log(ql_log_warn, fcport->vha, 0x2112,
	    "%s: unregister remoteport on %p %8phN\n",
	    __func__, fcport, fcport->port_name);

	if (test_bit(PFLG_DRIVER_REMOVING, &fcport->vha->pci_flags))
		nvme_fc_set_remoteport_devloss(fcport->nvme_remote_port, 0);

	init_completion(&fcport->nvme_del_done);
	ret = nvme_fc_unregister_remoteport(fcport->nvme_remote_port);
	if (ret)
		ql_log(ql_log_info, fcport->vha, 0x2114,
			"%s: Failed to unregister nvme_remote_port (%d)\n",
			    __func__, ret);
	wait_for_completion(&fcport->nvme_del_done);
}

void qla_nvme_delete(struct scsi_qla_host *vha)
{
	int nv_ret;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;

	if (vha->nvme_local_port) {
		init_completion(&vha->nvme_del_done);
		ql_log(ql_log_info, vha, 0x2116,
			"unregister localport=%p\n",
			vha->nvme_local_port);
		nv_ret = nvme_fc_unregister_localport(vha->nvme_local_port);
		if (nv_ret)
			ql_log(ql_log_info, vha, 0x2115,
			    "Unregister of localport failed\n");
		else
			wait_for_completion(&vha->nvme_del_done);
	}
}

int qla_nvme_register_hba(struct scsi_qla_host *vha)
{
	struct nvme_fc_port_template *tmpl;
	struct qla_hw_data *ha;
	struct nvme_fc_port_info pinfo;
	int ret = -EINVAL;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return ret;

	ha = vha->hw;
	tmpl = &qla_nvme_fc_transport;

	if (ql2xnvme_queues < MIN_NVME_HW_QUEUES) {
		ql_log(ql_log_warn, vha, 0xfffd,
		    "ql2xnvme_queues=%d is lower than minimum queues: %d. Resetting ql2xnvme_queues to:%d\n",
		    ql2xnvme_queues, MIN_NVME_HW_QUEUES, DEF_NVME_HW_QUEUES);
		ql2xnvme_queues = DEF_NVME_HW_QUEUES;
	} else if (ql2xnvme_queues > (ha->max_qpairs - 1)) {
		ql_log(ql_log_warn, vha, 0xfffd,
		       "ql2xnvme_queues=%d is greater than available IRQs: %d. Resetting ql2xnvme_queues to: %d\n",
		       ql2xnvme_queues, (ha->max_qpairs - 1),
		       (ha->max_qpairs - 1));
		ql2xnvme_queues = ((ha->max_qpairs - 1));
	}

	qla_nvme_fc_transport.max_hw_queues =
	    min((uint8_t)(ql2xnvme_queues),
		(uint8_t)((ha->max_qpairs - 1) ? (ha->max_qpairs - 1) : 1));

	ql_log(ql_log_info, vha, 0xfffb,
	       "Number of NVME queues used for this port: %d\n",
	    qla_nvme_fc_transport.max_hw_queues);

	pinfo.node_name = wwn_to_u64(vha->node_name);
	pinfo.port_name = wwn_to_u64(vha->port_name);
	pinfo.port_role = FC_PORT_ROLE_NVME_INITIATOR;
	pinfo.port_id = vha->d_id.b24;

	mutex_lock(&ha->vport_lock);
	/*
	 * Check again for nvme_local_port to see if any other thread raced
	 * with this one and finished registration.
	 */
	if (!vha->nvme_local_port) {
		ql_log(ql_log_info, vha, 0xffff,
		    "register_localport: host-traddr=nn-0x%llx:pn-0x%llx on portID:%x\n",
		    pinfo.node_name, pinfo.port_name, pinfo.port_id);
		qla_nvme_fc_transport.dma_boundary = vha->host->dma_boundary;

		ret = nvme_fc_register_localport(&pinfo, tmpl,
						 get_device(&ha->pdev->dev),
						 &vha->nvme_local_port);
		mutex_unlock(&ha->vport_lock);
	} else {
		mutex_unlock(&ha->vport_lock);
		return 0;
	}
	if (ret) {
		ql_log(ql_log_warn, vha, 0xffff,
		    "register_localport failed: ret=%x\n", ret);
	} else {
		vha->nvme_local_port->private = vha;
	}

	return ret;
}

void qla_nvme_abort_set_option(struct abort_entry_24xx *abt, srb_t *orig_sp)
{
	struct qla_hw_data *ha;

	if (!(ql2xabts_wait_nvme && QLA_ABTS_WAIT_ENABLED(orig_sp)))
		return;

	ha = orig_sp->fcport->vha->hw;

	WARN_ON_ONCE(abt->options & cpu_to_le16(BIT_0));
	/* Use Driver Specified Retry Count */
	abt->options |= cpu_to_le16(AOF_ABTS_RTY_CNT);
	abt->drv.abts_rty_cnt = cpu_to_le16(2);
	/* Use specified response timeout */
	abt->options |= cpu_to_le16(AOF_RSP_TIMEOUT);
	/* set it to 2 * r_a_tov in secs */
	abt->drv.rsp_timeout = cpu_to_le16(2 * (ha->r_a_tov / 10));
}

void qla_nvme_abort_process_comp_status(struct abort_entry_24xx *abt, srb_t *orig_sp)
{
	u16	comp_status;
	struct scsi_qla_host *vha;

	if (!(ql2xabts_wait_nvme && QLA_ABTS_WAIT_ENABLED(orig_sp)))
		return;

	vha = orig_sp->fcport->vha;

	comp_status = le16_to_cpu(abt->comp_status);
	switch (comp_status) {
	case CS_RESET:		/* reset event aborted */
	case CS_ABORTED:	/* IOCB was cleaned */
	/* N_Port handle is not currently logged in */
	case CS_TIMEOUT:
	/* N_Port handle was logged out while waiting for ABTS to complete */
	case CS_PORT_UNAVAILABLE:
	/* Firmware found that the port name changed */
	case CS_PORT_LOGGED_OUT:
	/* BA_RJT was received for the ABTS */
	case CS_PORT_CONFIG_CHG:
		ql_dbg(ql_dbg_async, vha, 0xf09d,
		       "Abort I/O IOCB completed with error, comp_status=%x\n",
		comp_status);
		break;

	/* BA_RJT was received for the ABTS */
	case CS_REJECT_RECEIVED:
		ql_dbg(ql_dbg_async, vha, 0xf09e,
		       "BA_RJT was received for the ABTS rjt_vendorUnique = %u",
			abt->fw.ba_rjt_vendorUnique);
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf09e,
		       "ba_rjt_reasonCodeExpl = %u, ba_rjt_reasonCode = %u\n",
		       abt->fw.ba_rjt_reasonCodeExpl, abt->fw.ba_rjt_reasonCode);
		break;

	case CS_COMPLETE:
		ql_dbg(ql_dbg_async + ql_dbg_verbose, vha, 0xf09f,
		       "IOCB request is completed successfully comp_status=%x\n",
		comp_status);
		break;

	case CS_IOCB_ERROR:
		ql_dbg(ql_dbg_async, vha, 0xf0a0,
		       "IOCB request is failed, comp_status=%x\n", comp_status);
		break;

	default:
		ql_dbg(ql_dbg_async, vha, 0xf0a1,
		       "Invalid Abort IO IOCB Completion Status %x\n",
		comp_status);
		break;
	}
}

inline void qla_wait_nvme_release_cmd_kref(srb_t *orig_sp)
{
	if (!(ql2xabts_wait_nvme && QLA_ABTS_WAIT_ENABLED(orig_sp)))
		return;
	kref_put(&orig_sp->cmd_kref, orig_sp->put_fn);
}

static void qla_nvme_fc_format_rjt(void *buf, u8 ls_cmd, u8 reason,
				   u8 explanation, u8 vendor)
{
	struct fcnvme_ls_rjt *rjt = buf;

	rjt->w0.ls_cmd = FCNVME_LSDESC_RQST;
	rjt->desc_list_len = fcnvme_lsdesc_len(sizeof(struct fcnvme_ls_rjt));
	rjt->rqst.desc_tag = cpu_to_be32(FCNVME_LSDESC_RQST);
	rjt->rqst.desc_len =
		fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rqst));
	rjt->rqst.w0.ls_cmd = ls_cmd;
	rjt->rjt.desc_tag = cpu_to_be32(FCNVME_LSDESC_RJT);
	rjt->rjt.desc_len = fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rjt));
	rjt->rjt.reason_code = reason;
	rjt->rjt.reason_explanation = explanation;
	rjt->rjt.vendor = vendor;
}

static void qla_nvme_lsrjt_pt_iocb(struct scsi_qla_host *vha,
				   struct pt_ls4_request *lsrjt_iocb,
				   struct qla_nvme_lsrjt_pt_arg *a)
{
	lsrjt_iocb->entry_type = PT_LS4_REQUEST;
	lsrjt_iocb->entry_count = 1;
	lsrjt_iocb->sys_define = 0;
	lsrjt_iocb->entry_status = 0;
	lsrjt_iocb->handle = QLA_SKIP_HANDLE;
	lsrjt_iocb->nport_handle = a->nport_handle;
	lsrjt_iocb->exchange_address = a->xchg_address;
	lsrjt_iocb->vp_index = a->vp_idx;

	lsrjt_iocb->control_flags = cpu_to_le16(a->control_flags);

	put_unaligned_le64(a->tx_addr, &lsrjt_iocb->dsd[0].address);
	lsrjt_iocb->dsd[0].length = cpu_to_le32(a->tx_byte_count);
	lsrjt_iocb->tx_dseg_count = cpu_to_le16(1);
	lsrjt_iocb->tx_byte_count = cpu_to_le32(a->tx_byte_count);

	put_unaligned_le64(a->rx_addr, &lsrjt_iocb->dsd[1].address);
	lsrjt_iocb->dsd[1].length = 0;
	lsrjt_iocb->rx_dseg_count = 0;
	lsrjt_iocb->rx_byte_count = 0;
}

static int
qla_nvme_ls_reject_iocb(struct scsi_qla_host *vha, struct qla_qpair *qp,
			struct qla_nvme_lsrjt_pt_arg *a, bool is_xchg_terminate)
{
	struct pt_ls4_request *lsrjt_iocb;

	lsrjt_iocb = __qla2x00_alloc_iocbs(qp, NULL);
	if (!lsrjt_iocb) {
		ql_log(ql_log_warn, vha, 0x210e,
		       "qla2x00_alloc_iocbs failed.\n");
		return QLA_FUNCTION_FAILED;
	}

	if (!is_xchg_terminate) {
		qla_nvme_fc_format_rjt((void *)vha->hw->lsrjt.c, a->opcode,
				       a->reason, a->explanation, 0);

		a->tx_byte_count = sizeof(struct fcnvme_ls_rjt);
		a->tx_addr = vha->hw->lsrjt.cdma;
		a->control_flags = CF_LS4_RESPONDER << CF_LS4_SHIFT;

		ql_dbg(ql_dbg_unsol, vha, 0x211f,
		       "Sending nvme fc ls reject ox_id %04x op %04x\n",
		       a->ox_id, a->opcode);
		ql_dump_buffer(ql_dbg_unsol + ql_dbg_verbose, vha, 0x210f,
			       vha->hw->lsrjt.c, sizeof(*vha->hw->lsrjt.c));
	} else {
		a->tx_byte_count = 0;
		a->control_flags = CF_LS4_RESPONDER_TERM << CF_LS4_SHIFT;
		ql_dbg(ql_dbg_unsol, vha, 0x2110,
		       "Terminate nvme ls xchg 0x%x\n", a->xchg_address);
	}

	qla_nvme_lsrjt_pt_iocb(vha, lsrjt_iocb, a);
	/* flush iocb to mem before notifying hw doorbell */
	wmb();
	qla2x00_start_iocbs(vha, qp->req);
	return 0;
}

/*
 * qla2xxx_process_purls_pkt() - Pass-up Unsolicited
 * Received FC-NVMe Link Service pkt to nvme_fc_rcv_ls_req().
 * LLDD need to provide memory for response buffer, which
 * will be used to reference the exchange corresponding
 * to the LS when issuing an ls response. LLDD will have to free
 * response buffer in lport->ops->xmt_ls_rsp().
 *
 * @vha: SCSI qla host
 * @item: ptr to purex_item
 */
static void
qla2xxx_process_purls_pkt(struct scsi_qla_host *vha, struct purex_item *item)
{
	struct qla_nvme_unsol_ctx *uctx = item->purls_context;
	struct qla_nvme_lsrjt_pt_arg a;
	int ret = 1;

#if (IS_ENABLED(CONFIG_NVME_FC))
	ret = nvme_fc_rcv_ls_req(uctx->fcport->nvme_remote_port, &uctx->lsrsp,
				 &item->iocb, item->size);
#endif
	if (ret) {
		ql_dbg(ql_dbg_unsol, vha, 0x2125, "NVMe transport ls_req failed\n");
		memset((void *)&a, 0, sizeof(a));
		a.vp_idx = vha->vp_idx;
		a.nport_handle = uctx->nport_handle;
		a.xchg_address = uctx->exchange_address;
		qla_nvme_ls_reject_iocb(vha, vha->hw->base_qpair, &a, true);
		list_del(&uctx->elem);
		kfree(uctx);
	}
}

static scsi_qla_host_t *
qla2xxx_get_vha_from_vp_idx(struct qla_hw_data *ha, uint16_t vp_index)
{
	scsi_qla_host_t *base_vha, *vha, *tvp;
	unsigned long flags;

	base_vha = pci_get_drvdata(ha->pdev);

	if (!vp_index && !ha->num_vhosts)
		return base_vha;

	spin_lock_irqsave(&ha->vport_slock, flags);
	list_for_each_entry_safe(vha, tvp, &ha->vp_list, list) {
		if (vha->vp_idx == vp_index) {
			spin_unlock_irqrestore(&ha->vport_slock, flags);
			return vha;
		}
	}
	spin_unlock_irqrestore(&ha->vport_slock, flags);

	return NULL;
}

void qla2xxx_process_purls_iocb(void **pkt, struct rsp_que **rsp)
{
	struct nvme_fc_remote_port *rport;
	struct qla_nvme_rport *qla_rport;
	struct qla_nvme_lsrjt_pt_arg a;
	struct pt_ls4_rx_unsol *p = *pkt;
	struct qla_nvme_unsol_ctx *uctx;
	struct rsp_que *rsp_q = *rsp;
	struct qla_hw_data *ha;
	scsi_qla_host_t	*vha;
	fc_port_t *fcport = NULL;
	struct purex_item *item;
	port_id_t d_id = {0};
	port_id_t id = {0};
	u8 *opcode;
	bool xmt_reject = false;

	ha = rsp_q->hw;

	vha = qla2xxx_get_vha_from_vp_idx(ha, p->vp_index);
	if (!vha) {
		ql_log(ql_log_warn, NULL, 0x2110, "Invalid vp index %d\n", p->vp_index);
		WARN_ON_ONCE(1);
		return;
	}

	memset((void *)&a, 0, sizeof(a));
	opcode = (u8 *)&p->payload[0];
	a.opcode = opcode[3];
	a.vp_idx = p->vp_index;
	a.nport_handle = p->nport_handle;
	a.ox_id = p->ox_id;
	a.xchg_address = p->exchange_address;

	id.b.domain = p->s_id.domain;
	id.b.area   = p->s_id.area;
	id.b.al_pa  = p->s_id.al_pa;
	d_id.b.domain = p->d_id[2];
	d_id.b.area   = p->d_id[1];
	d_id.b.al_pa  = p->d_id[0];

	fcport = qla2x00_find_fcport_by_nportid(vha, &id, 0);
	if (!fcport) {
		ql_dbg(ql_dbg_unsol, vha, 0x211e,
		       "Failed to find sid=%06x did=%06x\n",
		       id.b24, d_id.b24);
		a.reason = FCNVME_RJT_RC_INV_ASSOC;
		a.explanation = FCNVME_RJT_EXP_NONE;
		xmt_reject = true;
		goto out;
	}
	rport = fcport->nvme_remote_port;
	qla_rport = rport->private;

	item = qla27xx_copy_multiple_pkt(vha, pkt, rsp, true, false);
	if (!item) {
		a.reason = FCNVME_RJT_RC_LOGIC;
		a.explanation = FCNVME_RJT_EXP_NONE;
		xmt_reject = true;
		goto out;
	}

	uctx = kzalloc(sizeof(*uctx), GFP_ATOMIC);
	if (!uctx) {
		ql_log(ql_log_info, vha, 0x2126, "Failed allocate memory\n");
		a.reason = FCNVME_RJT_RC_LOGIC;
		a.explanation = FCNVME_RJT_EXP_NONE;
		xmt_reject = true;
		kfree(item);
		goto out;
	}

	uctx->vha = vha;
	uctx->fcport = fcport;
	uctx->exchange_address = p->exchange_address;
	uctx->nport_handle = p->nport_handle;
	uctx->ox_id = p->ox_id;
	qla_rport->uctx = uctx;
	INIT_LIST_HEAD(&uctx->elem);
	list_add_tail(&uctx->elem, &fcport->unsol_ctx_head);
	item->purls_context = (void *)uctx;

	ql_dbg(ql_dbg_unsol, vha, 0x2121,
	       "PURLS OP[%01x] size %d xchg addr 0x%x portid %06x\n",
	       item->iocb.iocb[3], item->size, uctx->exchange_address,
	       fcport->d_id.b24);
	/* +48    0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	 * ----- -----------------------------------------------
	 * 0000: 00 00 00 05 28 00 00 00 07 00 00 00 08 00 00 00
	 * 0010: ab ec 0f cc 00 00 8d 7d 05 00 00 00 10 00 00 00
	 * 0020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
	 */
	ql_dump_buffer(ql_dbg_unsol + ql_dbg_verbose, vha, 0x2120,
		       &item->iocb, item->size);

	qla24xx_queue_purex_item(vha, item, qla2xxx_process_purls_pkt);
out:
	if (xmt_reject) {
		qla_nvme_ls_reject_iocb(vha, (*rsp)->qpair, &a, false);
		__qla_consume_iocb(vha, pkt, rsp);
	}
}

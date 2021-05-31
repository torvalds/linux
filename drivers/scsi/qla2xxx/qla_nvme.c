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

static struct nvme_fc_port_template qla_nvme_fc_transport;

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

	if (!vha->nvme_local_port && qla_nvme_register_hba(vha))
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
	req.dev_loss_tmo = 0;

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

	if (!qidx)
		qidx++;

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

	if (ha->queue_pair_map[qidx]) {
		*handle = ha->queue_pair_map[qidx];
		ql_log(ql_log_info, vha, 0x2121,
		    "Returning existing qpair of %p for idx=%x\n",
		    *handle, qidx);
		return 0;
	}

	qpair = qla2xxx_create_qpair(vha, 5, vha->vp_idx, true);
	if (qpair == NULL) {
		ql_log(ql_log_warn, vha, 0x2122,
		    "Failed to allocate qpair\n");
		return -EINVAL;
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
	int rval;

	ql_dbg(ql_dbg_io, fcport->vha, 0xffff,
	       "%s called for sp=%p, hndl=%x on fcport=%p deleted=%d\n",
	       __func__, sp, sp->handle, fcport, fcport->deleted);

	if (!ha->flags.fw_started || fcport->deleted)
		goto out;

	if (ha->flags.host_shutting_down) {
		ql_log(ql_log_info, sp->fcport->vha, 0xffff,
		    "%s Calling done on sp: %p, type: 0x%x\n",
		    __func__, sp, sp->type);
		sp->done(sp, 0);
		goto out;
	}

	rval = ha->isp_ops->abort_command(sp);

	ql_dbg(ql_dbg_io, fcport->vha, 0x212b,
	    "%s: %s command for sp=%p, handle=%x on fcport=%p rval=%x\n",
	    __func__, (rval != QLA_SUCCESS) ? "Failed to abort" : "Aborted",
	    sp, sp->handle, fcport, rval);

	/*
	 * Returned before decreasing kref so that I/O requests
	 * are waited until ABTS complete. This kref is decreased
	 * at qla24xx_abort_sp_done function.
	 */
	if (ql2xabts_wait_nvme && QLA_ABTS_WAIT_ENABLED(sp))
		return;
out:
	/* kref_get was done before work was schedule. */
	kref_put(&sp->cmd_kref, sp->put_fn);
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
	nvme->u.nvme.cmd_len = fd->rqstlen;
	nvme->u.nvme.rsp_len = fd->rsplen;
	nvme->u.nvme.rsp_dma = fd->rspdma;
	nvme->u.nvme.timeout_sec = fd->timeout;
	nvme->u.nvme.cmd_dma = dma_map_single(&ha->pdev->dev, fd->rqstaddr,
	    fd->rqstlen, DMA_TO_DEVICE);
	dma_sync_single_for_device(&ha->pdev->dev, nvme->u.nvme.cmd_dma,
	    fd->rqstlen, DMA_TO_DEVICE);

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x700e,
		    "qla2x00_start_sp failed = %d\n", rval);
		wake_up(&sp->nvme_ls_waitq);
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
	tot_dsds = fd->sg_cnt;

	/* Acquire qpair specific lock */
	spin_lock_irqsave(&qpair->qp_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0) {
		rval = -EBUSY;
		goto queuing_error;
	}
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);
	if (req->cnt < (req_cnt + 2)) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt))
				goto queuing_error;
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

	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

queuing_error:
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

	/* Alloc SRB structure */
	sp = qla2xxx_get_qpair_sp(vha, qpair, fcport, GFP_ATOMIC);
	if (!sp)
		return -EBUSY;

	init_waitqueue_head(&sp->nvme_ls_waitq);
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
		ql_log(ql_log_warn, vha, 0x212d,
		    "qla2x00_start_nvme_mq failed = %d\n", rval);
		wake_up(&sp->nvme_ls_waitq);
		sp->priv = NULL;
		priv->sp = NULL;
		qla2xxx_rel_qpair_sp(sp->qpair, sp);
	}

	return rval;
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
	.max_hw_queues  = 8,
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

	WARN_ON(vha->nvme_local_port);

	if (ha->max_req_queues < 3) {
		if (!ha->flags.max_req_queue_warned)
			ql_log(ql_log_info, vha, 0x2120,
			       "%s: Disabling FC-NVME due to lack of free queue pairs (%d).\n",
			       __func__, ha->max_req_queues);
		ha->flags.max_req_queue_warned = 1;
		return ret;
	}

	qla_nvme_fc_transport.max_hw_queues =
	    min((uint8_t)(qla_nvme_fc_transport.max_hw_queues),
		(uint8_t)(ha->max_req_queues - 2));

	pinfo.node_name = wwn_to_u64(vha->node_name);
	pinfo.port_name = wwn_to_u64(vha->port_name);
	pinfo.port_role = FC_PORT_ROLE_NVME_INITIATOR;
	pinfo.port_id = vha->d_id.b24;

	ql_log(ql_log_info, vha, 0xffff,
	    "register_localport: host-traddr=nn-0x%llx:pn-0x%llx on portID:%x\n",
	    pinfo.node_name, pinfo.port_name, pinfo.port_id);
	qla_nvme_fc_transport.dma_boundary = vha->host->dma_boundary;

	ret = nvme_fc_register_localport(&pinfo, tmpl,
	    get_device(&ha->pdev->dev), &vha->nvme_local_port);
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
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf09d,
		       "Abort I/O IOCB completed with error, comp_status=%x\n",
		comp_status);
		break;

	/* BA_RJT was received for the ABTS */
	case CS_REJECT_RECEIVED:
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf09e,
		       "BA_RJT was received for the ABTS rjt_vendorUnique = %u",
			abt->fw.ba_rjt_vendorUnique);
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf09e,
		       "ba_rjt_reasonCodeExpl = %u, ba_rjt_reasonCode = %u\n",
		       abt->fw.ba_rjt_reasonCodeExpl, abt->fw.ba_rjt_reasonCode);
		break;

	case CS_COMPLETE:
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf09f,
		       "IOCB request is completed successfully comp_status=%x\n",
		comp_status);
		break;

	case CS_IOCB_ERROR:
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf0a0,
		       "IOCB request is failed, comp_status=%x\n", comp_status);
		break;

	default:
		ql_dbg(ql_dbg_async + ql_dbg_mbx, vha, 0xf0a1,
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

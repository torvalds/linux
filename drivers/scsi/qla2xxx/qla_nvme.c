/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2017 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_nvme.h"
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/nvme.h>
#include <linux/nvme-fc.h>

static struct nvme_fc_port_template qla_nvme_fc_transport;

static void qla_nvme_unregister_remote_port(struct work_struct *);

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

	INIT_WORK(&fcport->nvme_del_work, qla_nvme_unregister_remote_port);
	fcport->nvme_flag &= ~NVME_FLAG_RESETTING;

	memset(&req, 0, sizeof(struct nvme_fc_port_info));
	req.port_name = wwn_to_u64(fcport->port_name);
	req.node_name = wwn_to_u64(fcport->node_name);
	req.port_role = 0;
	req.dev_loss_tmo = NVME_FC_DEV_LOSS_TMO;

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

	rport = fcport->nvme_remote_port->private;
	rport->fcport = fcport;
	list_add_tail(&rport->list, &vha->nvme_rport_list);

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

static void qla_nvme_sp_ls_done(void *ptr, int res)
{
	srb_t *sp = ptr;
	struct srb_iocb *nvme;
	struct nvmefc_ls_req   *fd;
	struct nvme_private *priv;

	if (atomic_read(&sp->ref_count) == 0) {
		ql_log(ql_log_warn, sp->fcport->vha, 0x2123,
		    "SP reference-count to ZERO on LS_done -- sp=%p.\n", sp);
		return;
	}

	if (!atomic_dec_and_test(&sp->ref_count))
		return;

	if (res)
		res = -EINVAL;

	nvme = &sp->u.iocb_cmd;
	fd = nvme->u.nvme.desc;
	priv = fd->private;
	priv->comp_status = res;
	schedule_work(&priv->ls_work);
	/* work schedule doesn't need the sp */
	qla2x00_rel_sp(sp);
}

static void qla_nvme_sp_done(void *ptr, int res)
{
	srb_t *sp = ptr;
	struct srb_iocb *nvme;
	struct nvmefc_fcp_req *fd;

	nvme = &sp->u.iocb_cmd;
	fd = nvme->u.nvme.desc;

	if (!atomic_dec_and_test(&sp->ref_count))
		return;

	if (res == QLA_SUCCESS)
		fd->status = 0;
	else
		fd->status = NVME_SC_INTERNAL;

	fd->rcv_rsplen = nvme->u.nvme.rsp_pyld_len;
	fd->done(fd);
	qla2xxx_rel_qpair_sp(sp->qpair, sp);

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

	rval = ha->isp_ops->abort_command(sp);

	ql_dbg(ql_dbg_io, fcport->vha, 0x212b,
	    "%s: %s command for sp=%p, handle=%x on fcport=%p rval=%x\n",
	    __func__, (rval != QLA_SUCCESS) ? "Failed to abort" : "Aborted",
	    sp, sp->handle, fcport, rval);
}

static void qla_nvme_ls_abort(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, struct nvmefc_ls_req *fd)
{
	struct nvme_private *priv = fd->private;

	INIT_WORK(&priv->abort_work, qla_nvme_abort_work);
	schedule_work(&priv->abort_work);
}

static void qla_nvme_ls_complete(struct work_struct *work)
{
	struct nvme_private *priv =
	    container_of(work, struct nvme_private, ls_work);
	struct nvmefc_ls_req *fd = priv->fd;

	fd->done(fd, priv->comp_status);
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

	vha = fcport->vha;
	ha = vha->hw;
	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_ATOMIC);
	if (!sp)
		return rval;

	sp->type = SRB_NVME_LS;
	sp->name = "nvme_ls";
	sp->done = qla_nvme_sp_ls_done;
	atomic_set(&sp->ref_count, 1);
	nvme = &sp->u.iocb_cmd;
	priv->sp = sp;
	priv->fd = fd;
	INIT_WORK(&priv->ls_work, qla_nvme_ls_complete);
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
		atomic_dec(&sp->ref_count);
		wake_up(&sp->nvme_ls_waitq);
		return rval;
	}

	return rval;
}

static void qla_nvme_fcp_abort(struct nvme_fc_local_port *lport,
    struct nvme_fc_remote_port *rport, void *hw_queue_handle,
    struct nvmefc_fcp_req *fd)
{
	struct nvme_private *priv = fd->private;

	INIT_WORK(&priv->abort_work, qla_nvme_abort_work);
	schedule_work(&priv->abort_work);
}

static void qla_nvme_poll(struct nvme_fc_local_port *lport, void *hw_queue_handle)
{
	struct qla_qpair *qpair = hw_queue_handle;
	unsigned long flags;
	struct scsi_qla_host *vha = lport->private;

	spin_lock_irqsave(&qpair->qp_lock, flags);
	qla24xx_process_response_queue(vha, qpair->rsp);
	spin_unlock_irqrestore(&qpair->qp_lock, flags);
}

static inline int qla2x00_start_nvme_mq(srb_t *sp)
{
	unsigned long   flags;
	uint32_t        *clr_ptr;
	uint32_t        index;
	uint32_t        handle;
	struct cmd_nvme *cmd_pkt;
	uint16_t        cnt, i;
	uint16_t        req_cnt;
	uint16_t        tot_dsds;
	uint16_t	avail_dsds;
	uint32_t	*cur_dsd;
	struct req_que *req = NULL;
	struct scsi_qla_host *vha = sp->fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_qpair *qpair = sp->qpair;
	struct srb_iocb *nvme = &sp->u.iocb_cmd;
	struct scatterlist *sgl, *sg;
	struct nvmefc_fcp_req *fd = nvme->u.nvme.desc;
	uint32_t        rval = QLA_SUCCESS;

	/* Setup qpair pointers */
	req = qpair->req;
	tot_dsds = fd->sg_cnt;

	/* Acquire qpair specific lock */
	spin_lock_irqsave(&qpair->qp_lock, flags);

	/* Check for room in outstanding command list. */
	handle = req->current_outstanding_cmd;
	for (index = 1; index < req->num_outstanding_cmds; index++) {
		handle++;
		if (handle == req->num_outstanding_cmds)
			handle = 1;
		if (!req->outstanding_cmds[handle])
			break;
	}

	if (index == req->num_outstanding_cmds) {
		rval = -EBUSY;
		goto queuing_error;
	}
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);
	if (req->cnt < (req_cnt + 2)) {
		cnt = IS_SHADOW_REG_CAPABLE(ha) ? *req->out_ptr :
		    RD_REG_DWORD_RELAXED(req->req_q_out);

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
		struct nvme_fc_cmd_iu *cmd = fd->cmdaddr;
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
	cmd_pkt->handle = MAKE_HANDLE(req->id, handle);

	/* Zero out remaining portion of packet. */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);

	cmd_pkt->entry_status = 0;

	/* Update entry type to indicate Command NVME IOCB */
	cmd_pkt->entry_type = COMMAND_NVME;

	/* No data transfer how do we check buffer len == 0?? */
	if (fd->io_dir == NVMEFC_FCP_READ) {
		cmd_pkt->control_flags =
		    cpu_to_le16(CF_READ_DATA | CF_NVME_ENABLE);
		vha->qla_stats.input_bytes += fd->payload_length;
		vha->qla_stats.input_requests++;
	} else if (fd->io_dir == NVMEFC_FCP_WRITE) {
		cmd_pkt->control_flags =
		    cpu_to_le16(CF_WRITE_DATA | CF_NVME_ENABLE);
		vha->qla_stats.output_bytes += fd->payload_length;
		vha->qla_stats.output_requests++;
	} else if (fd->io_dir == 0) {
		cmd_pkt->control_flags = cpu_to_le16(CF_NVME_ENABLE);
	}

	/* Set NPORT-ID */
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
	cmd_pkt->vp_index = sp->fcport->vha->vp_idx;

	/* NVME RSP IU */
	cmd_pkt->nvme_rsp_dsd_len = cpu_to_le16(fd->rsplen);
	cmd_pkt->nvme_rsp_dseg_address[0] = cpu_to_le32(LSD(fd->rspdma));
	cmd_pkt->nvme_rsp_dseg_address[1] = cpu_to_le32(MSD(fd->rspdma));

	/* NVME CNMD IU */
	cmd_pkt->nvme_cmnd_dseg_len = cpu_to_le16(fd->cmdlen);
	cmd_pkt->nvme_cmnd_dseg_address[0] = cpu_to_le32(LSD(fd->cmddma));
	cmd_pkt->nvme_cmnd_dseg_address[1] = cpu_to_le32(MSD(fd->cmddma));

	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);
	cmd_pkt->byte_count = cpu_to_le32(fd->payload_length);

	/* One DSD is available in the Command Type NVME IOCB */
	avail_dsds = 1;
	cur_dsd = (uint32_t *)&cmd_pkt->nvme_data_dseg_address[0];
	sgl = fd->first_sgl;

	/* Load data segments */
	for_each_sg(sgl, sg, tot_dsds, i) {
		dma_addr_t      sle_dma;
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
			*((uint32_t *)(&cont_pkt->entry_type)) =
			    cpu_to_le32(CONTINUE_A64_TYPE);

			cur_dsd = (uint32_t *)cont_pkt->dseg_0_address;
			avail_dsds = 5;
		}

		sle_dma = sg_dma_address(sg);
		*cur_dsd++ = cpu_to_le32(LSD(sle_dma));
		*cur_dsd++ = cpu_to_le32(MSD(sle_dma));
		*cur_dsd++ = cpu_to_le32(sg_dma_len(sg));
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
	WRT_REG_DWORD(req->req_q_in, req->ring_index);

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
	int rval = -ENODEV;
	srb_t *sp;
	struct qla_qpair *qpair = hw_queue_handle;
	struct nvme_private *priv;
	struct qla_nvme_rport *qla_rport = rport->private;

	if (!fd || !qpair) {
		ql_log(ql_log_warn, NULL, 0x2134,
		    "NO NVMe request or Queue Handle\n");
		return rval;
	}

	priv = fd->private;
	fcport = qla_rport->fcport;
	if (!fcport) {
		ql_log(ql_log_warn, NULL, 0x210e, "No fcport ptr\n");
		return rval;
	}

	vha = fcport->vha;

	if (test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags))
		return rval;

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

	atomic_set(&sp->ref_count, 1);
	init_waitqueue_head(&sp->nvme_ls_waitq);
	priv->sp = sp;
	sp->type = SRB_NVME_CMD;
	sp->name = "nvme_cmd";
	sp->done = qla_nvme_sp_done;
	sp->qpair = qpair;
	nvme = &sp->u.iocb_cmd;
	nvme->u.nvme.desc = fd;

	rval = qla2x00_start_nvme_mq(sp);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x212d,
		    "qla2x00_start_nvme_mq failed = %d\n", rval);
		atomic_dec(&sp->ref_count);
		wake_up(&sp->nvme_ls_waitq);
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
	struct qla_nvme_rport *qla_rport = rport->private, *trport;

	fcport = qla_rport->fcport;
	fcport->nvme_remote_port = NULL;
	fcport->nvme_flag &= ~NVME_FLAG_REGISTERED;

	list_for_each_entry_safe(qla_rport, trport,
	    &fcport->vha->nvme_rport_list, list) {
		if (qla_rport->fcport == fcport) {
			list_del(&qla_rport->list);
			break;
		}
	}
	complete(&fcport->nvme_del_done);

	if (!test_bit(UNLOADING, &fcport->vha->dpc_flags)) {
		INIT_WORK(&fcport->free_work, qlt_free_session_done);
		schedule_work(&fcport->free_work);
	}

	fcport->nvme_flag &= ~(NVME_FLAG_REGISTERED | NVME_FLAG_DELETING);
	ql_log(ql_log_info, fcport->vha, 0x2110,
	    "remoteport_delete of %p completed.\n", fcport);
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
	.poll_queue	= qla_nvme_poll,
	.max_hw_queues  = 8,
	.max_sgl_segments = 128,
	.max_dif_sgl_segments = 64,
	.dma_boundary = 0xFFFFFFFF,
	.local_priv_sz  = 8,
	.remote_priv_sz = sizeof(struct qla_nvme_rport),
	.lsrqst_priv_sz = sizeof(struct nvme_private),
	.fcprqst_priv_sz = sizeof(struct nvme_private),
};

#define NVME_ABORT_POLLING_PERIOD    2
static int qla_nvme_wait_on_command(srb_t *sp)
{
	int ret = QLA_SUCCESS;

	wait_event_timeout(sp->nvme_ls_waitq, (atomic_read(&sp->ref_count) > 1),
	    NVME_ABORT_POLLING_PERIOD*HZ);

	if (atomic_read(&sp->ref_count) > 1)
		ret = QLA_FUNCTION_FAILED;

	return ret;
}

void qla_nvme_abort(struct qla_hw_data *ha, struct srb *sp, int res)
{
	int rval;

	if (ha->flags.fw_started) {
		rval = ha->isp_ops->abort_command(sp);
		if (!rval && !qla_nvme_wait_on_command(sp))
			ql_log(ql_log_warn, NULL, 0x2112,
			    "timed out waiting on sp=%p\n", sp);
	} else {
		sp->done(sp, res);
	}
}

static void qla_nvme_unregister_remote_port(struct work_struct *work)
{
	struct fc_port *fcport = container_of(work, struct fc_port,
	    nvme_del_work);
	struct qla_nvme_rport *qla_rport, *trport;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;

	ql_log(ql_log_warn, NULL, 0x2112,
	    "%s: unregister remoteport on %p\n",__func__, fcport);

	list_for_each_entry_safe(qla_rport, trport,
	    &fcport->vha->nvme_rport_list, list) {
		if (qla_rport->fcport == fcport) {
			ql_log(ql_log_info, fcport->vha, 0x2113,
			    "%s: fcport=%p\n", __func__, fcport);
			init_completion(&fcport->nvme_del_done);
			nvme_fc_unregister_remoteport(
			    fcport->nvme_remote_port);
			wait_for_completion(&fcport->nvme_del_done);
			break;
		}
	}
}

void qla_nvme_delete(struct scsi_qla_host *vha)
{
	struct qla_nvme_rport *qla_rport, *trport;
	fc_port_t *fcport;
	int nv_ret;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;

	list_for_each_entry_safe(qla_rport, trport,
	    &vha->nvme_rport_list, list) {
		fcport = qla_rport->fcport;

		ql_log(ql_log_info, fcport->vha, 0x2114, "%s: fcport=%p\n",
		    __func__, fcport);

		nvme_fc_set_remoteport_devloss(fcport->nvme_remote_port, 0);
	}

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
	int ret = EINVAL;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return ret;

	ha = vha->hw;
	tmpl = &qla_nvme_fc_transport;

	WARN_ON(vha->nvme_local_port);
	WARN_ON(ha->max_req_queues < 3);

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

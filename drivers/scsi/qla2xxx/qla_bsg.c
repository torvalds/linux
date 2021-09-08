// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */
#include "qla_def.h"
#include "qla_gbl.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/bsg-lib.h>

static void qla2xxx_free_fcport_work(struct work_struct *work)
{
	struct fc_port *fcport = container_of(work, typeof(*fcport),
	    free_work);

	qla2x00_free_fcport(fcport);
}

/* BSG support for ELS/CT pass through */
void qla2x00_bsg_job_done(srb_t *sp, int res)
{
	struct bsg_job *bsg_job = sp->u.bsg_job;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;

	ql_dbg(ql_dbg_user, sp->vha, 0x7009,
	    "%s: sp hdl %x, result=%x bsg ptr %p\n",
	    __func__, sp->handle, res, bsg_job);

	sp->free(sp);

	bsg_reply->result = res;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
}

void qla2x00_bsg_sp_free(srb_t *sp)
{
	struct qla_hw_data *ha = sp->vha->hw;
	struct bsg_job *bsg_job = sp->u.bsg_job;
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct qla_mt_iocb_rqst_fx00 *piocb_rqst;

	if (sp->type == SRB_FXIOCB_BCMD) {
		piocb_rqst = (struct qla_mt_iocb_rqst_fx00 *)
		    &bsg_request->rqst_data.h_vendor.vendor_cmd[1];

		if (piocb_rqst->flags & SRB_FXDISC_REQ_DMA_VALID)
			dma_unmap_sg(&ha->pdev->dev,
			    bsg_job->request_payload.sg_list,
			    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

		if (piocb_rqst->flags & SRB_FXDISC_RESP_DMA_VALID)
			dma_unmap_sg(&ha->pdev->dev,
			    bsg_job->reply_payload.sg_list,
			    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	} else {

		if (sp->remap.remapped) {
			dma_pool_free(ha->purex_dma_pool, sp->remap.rsp.buf,
			    sp->remap.rsp.dma);
			dma_pool_free(ha->purex_dma_pool, sp->remap.req.buf,
			    sp->remap.req.dma);
		} else {
			dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
				bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

			dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
				bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
		}
	}

	if (sp->type == SRB_CT_CMD ||
	    sp->type == SRB_FXIOCB_BCMD ||
	    sp->type == SRB_ELS_CMD_HST) {
		INIT_WORK(&sp->fcport->free_work, qla2xxx_free_fcport_work);
		queue_work(ha->wq, &sp->fcport->free_work);
	}

	qla2x00_rel_sp(sp);
}

int
qla24xx_fcp_prio_cfg_valid(scsi_qla_host_t *vha,
	struct qla_fcp_prio_cfg *pri_cfg, uint8_t flag)
{
	int i, ret, num_valid;
	uint8_t *bcode;
	struct qla_fcp_prio_entry *pri_entry;
	uint32_t *bcode_val_ptr, bcode_val;

	ret = 1;
	num_valid = 0;
	bcode = (uint8_t *)pri_cfg;
	bcode_val_ptr = (uint32_t *)pri_cfg;
	bcode_val = (uint32_t)(*bcode_val_ptr);

	if (bcode_val == 0xFFFFFFFF) {
		/* No FCP Priority config data in flash */
		ql_dbg(ql_dbg_user, vha, 0x7051,
		    "No FCP Priority config data.\n");
		return 0;
	}

	if (memcmp(bcode, "HQOS", 4)) {
		/* Invalid FCP priority data header*/
		ql_dbg(ql_dbg_user, vha, 0x7052,
		    "Invalid FCP Priority data header. bcode=0x%x.\n",
		    bcode_val);
		return 0;
	}
	if (flag != 1)
		return ret;

	pri_entry = &pri_cfg->entry[0];
	for (i = 0; i < pri_cfg->num_entries; i++) {
		if (pri_entry->flags & FCP_PRIO_ENTRY_TAG_VALID)
			num_valid++;
		pri_entry++;
	}

	if (num_valid == 0) {
		/* No valid FCP priority data entries */
		ql_dbg(ql_dbg_user, vha, 0x7053,
		    "No valid FCP Priority data entries.\n");
		ret = 0;
	} else {
		/* FCP priority data is valid */
		ql_dbg(ql_dbg_user, vha, 0x7054,
		    "Valid FCP priority data. num entries = %d.\n",
		    num_valid);
	}

	return ret;
}

static int
qla24xx_proc_fcp_prio_cfg_cmd(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int ret = 0;
	uint32_t len;
	uint32_t oper;

	if (!(IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha) || IS_P3P_TYPE(ha))) {
		ret = -EINVAL;
		goto exit_fcp_prio_cfg;
	}

	/* Get the sub command */
	oper = bsg_request->rqst_data.h_vendor.vendor_cmd[1];

	/* Only set config is allowed if config memory is not allocated */
	if (!ha->fcp_prio_cfg && (oper != QLFC_FCP_PRIO_SET_CONFIG)) {
		ret = -EINVAL;
		goto exit_fcp_prio_cfg;
	}
	switch (oper) {
	case QLFC_FCP_PRIO_DISABLE:
		if (ha->flags.fcp_prio_enabled) {
			ha->flags.fcp_prio_enabled = 0;
			ha->fcp_prio_cfg->attributes &=
				~FCP_PRIO_ATTR_ENABLE;
			qla24xx_update_all_fcp_prio(vha);
			bsg_reply->result = DID_OK;
		} else {
			ret = -EINVAL;
			bsg_reply->result = (DID_ERROR << 16);
			goto exit_fcp_prio_cfg;
		}
		break;

	case QLFC_FCP_PRIO_ENABLE:
		if (!ha->flags.fcp_prio_enabled) {
			if (ha->fcp_prio_cfg) {
				ha->flags.fcp_prio_enabled = 1;
				ha->fcp_prio_cfg->attributes |=
				    FCP_PRIO_ATTR_ENABLE;
				qla24xx_update_all_fcp_prio(vha);
				bsg_reply->result = DID_OK;
			} else {
				ret = -EINVAL;
				bsg_reply->result = (DID_ERROR << 16);
				goto exit_fcp_prio_cfg;
			}
		}
		break;

	case QLFC_FCP_PRIO_GET_CONFIG:
		len = bsg_job->reply_payload.payload_len;
		if (!len || len > FCP_PRIO_CFG_SIZE) {
			ret = -EINVAL;
			bsg_reply->result = (DID_ERROR << 16);
			goto exit_fcp_prio_cfg;
		}

		bsg_reply->result = DID_OK;
		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(
			bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, ha->fcp_prio_cfg,
			len);

		break;

	case QLFC_FCP_PRIO_SET_CONFIG:
		len = bsg_job->request_payload.payload_len;
		if (!len || len > FCP_PRIO_CFG_SIZE) {
			bsg_reply->result = (DID_ERROR << 16);
			ret = -EINVAL;
			goto exit_fcp_prio_cfg;
		}

		if (!ha->fcp_prio_cfg) {
			ha->fcp_prio_cfg = vmalloc(FCP_PRIO_CFG_SIZE);
			if (!ha->fcp_prio_cfg) {
				ql_log(ql_log_warn, vha, 0x7050,
				    "Unable to allocate memory for fcp prio "
				    "config data (%x).\n", FCP_PRIO_CFG_SIZE);
				bsg_reply->result = (DID_ERROR << 16);
				ret = -ENOMEM;
				goto exit_fcp_prio_cfg;
			}
		}

		memset(ha->fcp_prio_cfg, 0, FCP_PRIO_CFG_SIZE);
		sg_copy_to_buffer(bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, ha->fcp_prio_cfg,
			FCP_PRIO_CFG_SIZE);

		/* validate fcp priority data */

		if (!qla24xx_fcp_prio_cfg_valid(vha, ha->fcp_prio_cfg, 1)) {
			bsg_reply->result = (DID_ERROR << 16);
			ret = -EINVAL;
			/* If buffer was invalidatic int
			 * fcp_prio_cfg is of no use
			 */
			vfree(ha->fcp_prio_cfg);
			ha->fcp_prio_cfg = NULL;
			goto exit_fcp_prio_cfg;
		}

		ha->flags.fcp_prio_enabled = 0;
		if (ha->fcp_prio_cfg->attributes & FCP_PRIO_ATTR_ENABLE)
			ha->flags.fcp_prio_enabled = 1;
		qla24xx_update_all_fcp_prio(vha);
		bsg_reply->result = DID_OK;
		break;
	default:
		ret = -EINVAL;
		break;
	}
exit_fcp_prio_cfg:
	if (!ret)
		bsg_job_done(bsg_job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);
	return ret;
}

static int
qla2x00_process_els(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_rport *rport;
	fc_port_t *fcport = NULL;
	struct Scsi_Host *host;
	scsi_qla_host_t *vha;
	struct qla_hw_data *ha;
	srb_t *sp;
	const char *type;
	int req_sg_cnt, rsp_sg_cnt;
	int rval =  (DID_ERROR << 16);
	uint16_t nextlid = 0;
	uint32_t els_cmd = 0;

	if (bsg_request->msgcode == FC_BSG_RPT_ELS) {
		rport = fc_bsg_to_rport(bsg_job);
		fcport = *(fc_port_t **) rport->dd_data;
		host = rport_to_shost(rport);
		vha = shost_priv(host);
		ha = vha->hw;
		type = "FC_BSG_RPT_ELS";
	} else {
		host = fc_bsg_to_shost(bsg_job);
		vha = shost_priv(host);
		ha = vha->hw;
		type = "FC_BSG_HST_ELS_NOLOGIN";
		els_cmd = bsg_request->rqst_data.h_els.command_code;
		if (els_cmd == ELS_AUTH_ELS)
			return qla_edif_process_els(vha, bsg_job);
	}

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x7005, "Host not online.\n");
		rval = -EIO;
		goto done;
	}

	/* pass through is supported only for ISP 4Gb or higher */
	if (!IS_FWI2_CAPABLE(ha)) {
		ql_dbg(ql_dbg_user, vha, 0x7001,
		    "ELS passthru not supported for ISP23xx based adapters.\n");
		rval = -EPERM;
		goto done;
	}

	/*  Multiple SG's are not supported for ELS requests */
	if (bsg_job->request_payload.sg_cnt > 1 ||
		bsg_job->reply_payload.sg_cnt > 1) {
		ql_dbg(ql_dbg_user, vha, 0x7002,
		    "Multiple SG's are not supported for ELS requests, "
		    "request_sg_cnt=%x reply_sg_cnt=%x.\n",
		    bsg_job->request_payload.sg_cnt,
		    bsg_job->reply_payload.sg_cnt);
		rval = -EPERM;
		goto done;
	}

	/* ELS request for rport */
	if (bsg_request->msgcode == FC_BSG_RPT_ELS) {
		/* make sure the rport is logged in,
		 * if not perform fabric login
		 */
		if (qla2x00_fabric_login(vha, fcport, &nextlid)) {
			ql_dbg(ql_dbg_user, vha, 0x7003,
			    "Failed to login port %06X for ELS passthru.\n",
			    fcport->d_id.b24);
			rval = -EIO;
			goto done;
		}
	} else {
		/* Allocate a dummy fcport structure, since functions
		 * preparing the IOCB and mailbox command retrieves port
		 * specific information from fcport structure. For Host based
		 * ELS commands there will be no fcport structure allocated
		 */
		fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
		if (!fcport) {
			rval = -ENOMEM;
			goto done;
		}

		/* Initialize all required  fields of fcport */
		fcport->vha = vha;
		fcport->d_id.b.al_pa =
			bsg_request->rqst_data.h_els.port_id[0];
		fcport->d_id.b.area =
			bsg_request->rqst_data.h_els.port_id[1];
		fcport->d_id.b.domain =
			bsg_request->rqst_data.h_els.port_id[2];
		fcport->loop_id =
			(fcport->d_id.b.al_pa == 0xFD) ?
			NPH_FABRIC_CONTROLLER : NPH_F_PORT;
	}

	req_sg_cnt =
		dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!req_sg_cnt) {
		dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	rsp_sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
        if (!rsp_sg_cnt) {
		dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
		(rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		ql_log(ql_log_warn, vha, 0x7008,
		    "dma mapping resulted in different sg counts, "
		    "request_sg_cnt: %x dma_request_sg_cnt:%x reply_sg_cnt:%x "
		    "dma_reply_sg_cnt:%x.\n", bsg_job->request_payload.sg_cnt,
		    req_sg_cnt, bsg_job->reply_payload.sg_cnt, rsp_sg_cnt);
		rval = -EAGAIN;
		goto done_unmap_sg;
	}

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	sp->type =
		(bsg_request->msgcode == FC_BSG_RPT_ELS ?
		 SRB_ELS_CMD_RPT : SRB_ELS_CMD_HST);
	sp->name =
		(bsg_request->msgcode == FC_BSG_RPT_ELS ?
		 "bsg_els_rpt" : "bsg_els_hst");
	sp->u.bsg_job = bsg_job;
	sp->free = qla2x00_bsg_sp_free;
	sp->done = qla2x00_bsg_job_done;

	ql_dbg(ql_dbg_user, vha, 0x700a,
	    "bsg rqst type: %s els type: %x - loop-id=%x "
	    "portid=%-2x%02x%02x.\n", type,
	    bsg_request->rqst_data.h_els.command_code, fcport->loop_id,
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa);

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x700e,
		    "qla2x00_start_sp failed = %d\n", rval);
		qla2x00_rel_sp(sp);
		rval = -EIO;
		goto done_unmap_sg;
	}
	return rval;

done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	goto done_free_fcport;

done_free_fcport:
	if (bsg_request->msgcode == FC_BSG_RPT_ELS)
		qla2x00_free_fcport(fcport);
done:
	return rval;
}

static inline uint16_t
qla24xx_calc_ct_iocbs(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > 2) {
		iocbs += (dsds - 2) / 5;
		if ((dsds - 2) % 5)
			iocbs++;
	}
	return iocbs;
}

static int
qla2x00_process_ct(struct bsg_job *bsg_job)
{
	srb_t *sp;
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = (DID_ERROR << 16);
	int req_sg_cnt, rsp_sg_cnt;
	uint16_t loop_id;
	struct fc_port *fcport;
	char  *type = "FC_BSG_HST_CT";

	req_sg_cnt =
		dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
			bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!req_sg_cnt) {
		ql_log(ql_log_warn, vha, 0x700f,
		    "dma_map_sg return %d for request\n", req_sg_cnt);
		rval = -ENOMEM;
		goto done;
	}

	rsp_sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	if (!rsp_sg_cnt) {
		ql_log(ql_log_warn, vha, 0x7010,
		    "dma_map_sg return %d for reply\n", rsp_sg_cnt);
		rval = -ENOMEM;
		goto done;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
	    (rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		ql_log(ql_log_warn, vha, 0x7011,
		    "request_sg_cnt: %x dma_request_sg_cnt: %x reply_sg_cnt:%x "
		    "dma_reply_sg_cnt: %x\n", bsg_job->request_payload.sg_cnt,
		    req_sg_cnt, bsg_job->reply_payload.sg_cnt, rsp_sg_cnt);
		rval = -EAGAIN;
		goto done_unmap_sg;
	}

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x7012,
		    "Host is not online.\n");
		rval = -EIO;
		goto done_unmap_sg;
	}

	loop_id =
		(bsg_request->rqst_data.h_ct.preamble_word1 & 0xFF000000)
			>> 24;
	switch (loop_id) {
	case 0xFC:
		loop_id = NPH_SNS;
		break;
	case 0xFA:
		loop_id = vha->mgmt_svr_loop_id;
		break;
	default:
		ql_dbg(ql_dbg_user, vha, 0x7013,
		    "Unknown loop id: %x.\n", loop_id);
		rval = -EINVAL;
		goto done_unmap_sg;
	}

	/* Allocate a dummy fcport structure, since functions preparing the
	 * IOCB and mailbox command retrieves port specific information
	 * from fcport structure. For Host based ELS commands there will be
	 * no fcport structure allocated
	 */
	fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (!fcport) {
		ql_log(ql_log_warn, vha, 0x7014,
		    "Failed to allocate fcport.\n");
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	/* Initialize all required  fields of fcport */
	fcport->vha = vha;
	fcport->d_id.b.al_pa = bsg_request->rqst_data.h_ct.port_id[0];
	fcport->d_id.b.area = bsg_request->rqst_data.h_ct.port_id[1];
	fcport->d_id.b.domain = bsg_request->rqst_data.h_ct.port_id[2];
	fcport->loop_id = loop_id;

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		ql_log(ql_log_warn, vha, 0x7015,
		    "qla2x00_get_sp failed.\n");
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	sp->type = SRB_CT_CMD;
	sp->name = "bsg_ct";
	sp->iocbs = qla24xx_calc_ct_iocbs(req_sg_cnt + rsp_sg_cnt);
	sp->u.bsg_job = bsg_job;
	sp->free = qla2x00_bsg_sp_free;
	sp->done = qla2x00_bsg_job_done;

	ql_dbg(ql_dbg_user, vha, 0x7016,
	    "bsg rqst type: %s else type: %x - "
	    "loop-id=%x portid=%02x%02x%02x.\n", type,
	    (bsg_request->rqst_data.h_ct.preamble_word2 >> 16),
	    fcport->loop_id, fcport->d_id.b.domain, fcport->d_id.b.area,
	    fcport->d_id.b.al_pa);

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x7017,
		    "qla2x00_start_sp failed=%d.\n", rval);
		qla2x00_rel_sp(sp);
		rval = -EIO;
		goto done_free_fcport;
	}
	return rval;

done_free_fcport:
	qla2x00_free_fcport(fcport);
done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
done:
	return rval;
}

/* Disable loopback mode */
static inline int
qla81xx_reset_loopback_mode(scsi_qla_host_t *vha, uint16_t *config,
			    int wait, int wait2)
{
	int ret = 0;
	int rval = 0;
	uint16_t new_config[4];
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha) && !IS_QLA8044(ha))
		goto done_reset_internal;

	memset(new_config, 0 , sizeof(new_config));
	if ((config[0] & INTERNAL_LOOPBACK_MASK) >> 1 ==
	    ENABLE_INTERNAL_LOOPBACK ||
	    (config[0] & INTERNAL_LOOPBACK_MASK) >> 1 ==
	    ENABLE_EXTERNAL_LOOPBACK) {
		new_config[0] = config[0] & ~INTERNAL_LOOPBACK_MASK;
		ql_dbg(ql_dbg_user, vha, 0x70bf, "new_config[0]=%02x\n",
		    (new_config[0] & INTERNAL_LOOPBACK_MASK));
		memcpy(&new_config[1], &config[1], sizeof(uint16_t) * 3) ;

		ha->notify_dcbx_comp = wait;
		ha->notify_lb_portup_comp = wait2;

		ret = qla81xx_set_port_config(vha, new_config);
		if (ret != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x7025,
			    "Set port config failed.\n");
			ha->notify_dcbx_comp = 0;
			ha->notify_lb_portup_comp = 0;
			rval = -EINVAL;
			goto done_reset_internal;
		}

		/* Wait for DCBX complete event */
		if (wait && !wait_for_completion_timeout(&ha->dcbx_comp,
			(DCBX_COMP_TIMEOUT * HZ))) {
			ql_dbg(ql_dbg_user, vha, 0x7026,
			    "DCBX completion not received.\n");
			ha->notify_dcbx_comp = 0;
			ha->notify_lb_portup_comp = 0;
			rval = -EINVAL;
			goto done_reset_internal;
		} else
			ql_dbg(ql_dbg_user, vha, 0x7027,
			    "DCBX completion received.\n");

		if (wait2 &&
		    !wait_for_completion_timeout(&ha->lb_portup_comp,
		    (LB_PORTUP_COMP_TIMEOUT * HZ))) {
			ql_dbg(ql_dbg_user, vha, 0x70c5,
			    "Port up completion not received.\n");
			ha->notify_lb_portup_comp = 0;
			rval = -EINVAL;
			goto done_reset_internal;
		} else
			ql_dbg(ql_dbg_user, vha, 0x70c6,
			    "Port up completion received.\n");

		ha->notify_dcbx_comp = 0;
		ha->notify_lb_portup_comp = 0;
	}
done_reset_internal:
	return rval;
}

/*
 * Set the port configuration to enable the internal or external loopback
 * depending on the loopback mode.
 */
static inline int
qla81xx_set_loopback_mode(scsi_qla_host_t *vha, uint16_t *config,
	uint16_t *new_config, uint16_t mode)
{
	int ret = 0;
	int rval = 0;
	unsigned long rem_tmo = 0, current_tmo = 0;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha) && !IS_QLA8044(ha))
		goto done_set_internal;

	if (mode == INTERNAL_LOOPBACK)
		new_config[0] = config[0] | (ENABLE_INTERNAL_LOOPBACK << 1);
	else if (mode == EXTERNAL_LOOPBACK)
		new_config[0] = config[0] | (ENABLE_EXTERNAL_LOOPBACK << 1);
	ql_dbg(ql_dbg_user, vha, 0x70be,
	     "new_config[0]=%02x\n", (new_config[0] & INTERNAL_LOOPBACK_MASK));

	memcpy(&new_config[1], &config[1], sizeof(uint16_t) * 3);

	ha->notify_dcbx_comp = 1;
	ret = qla81xx_set_port_config(vha, new_config);
	if (ret != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x7021,
		    "set port config failed.\n");
		ha->notify_dcbx_comp = 0;
		rval = -EINVAL;
		goto done_set_internal;
	}

	/* Wait for DCBX complete event */
	current_tmo = DCBX_COMP_TIMEOUT * HZ;
	while (1) {
		rem_tmo = wait_for_completion_timeout(&ha->dcbx_comp,
		    current_tmo);
		if (!ha->idc_extend_tmo || rem_tmo) {
			ha->idc_extend_tmo = 0;
			break;
		}
		current_tmo = ha->idc_extend_tmo * HZ;
		ha->idc_extend_tmo = 0;
	}

	if (!rem_tmo) {
		ql_dbg(ql_dbg_user, vha, 0x7022,
		    "DCBX completion not received.\n");
		ret = qla81xx_reset_loopback_mode(vha, new_config, 0, 0);
		/*
		 * If the reset of the loopback mode doesn't work take a FCoE
		 * dump and reset the chip.
		 */
		if (ret) {
			qla2xxx_dump_fw(vha);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		}
		rval = -EINVAL;
	} else {
		if (ha->flags.idc_compl_status) {
			ql_dbg(ql_dbg_user, vha, 0x70c3,
			    "Bad status in IDC Completion AEN\n");
			rval = -EINVAL;
			ha->flags.idc_compl_status = 0;
		} else
			ql_dbg(ql_dbg_user, vha, 0x7023,
			    "DCBX completion received.\n");
	}

	ha->notify_dcbx_comp = 0;
	ha->idc_extend_tmo = 0;

done_set_internal:
	return rval;
}

static int
qla2x00_process_loopback(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval;
	uint8_t command_sent;
	char *type;
	struct msg_echo_lb elreq;
	uint16_t response[MAILBOX_REGISTER_COUNT];
	uint16_t config[4], new_config[4];
	uint8_t *fw_sts_ptr;
	void *req_data = NULL;
	dma_addr_t req_data_dma;
	uint32_t req_data_len;
	uint8_t *rsp_data = NULL;
	dma_addr_t rsp_data_dma;
	uint32_t rsp_data_len;

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x7019, "Host is not online.\n");
		return -EIO;
	}

	memset(&elreq, 0, sizeof(elreq));

	elreq.req_sg_cnt = dma_map_sg(&ha->pdev->dev,
		bsg_job->request_payload.sg_list, bsg_job->request_payload.sg_cnt,
		DMA_TO_DEVICE);

	if (!elreq.req_sg_cnt) {
		ql_log(ql_log_warn, vha, 0x701a,
		    "dma_map_sg returned %d for request.\n", elreq.req_sg_cnt);
		return -ENOMEM;
	}

	elreq.rsp_sg_cnt = dma_map_sg(&ha->pdev->dev,
		bsg_job->reply_payload.sg_list, bsg_job->reply_payload.sg_cnt,
		DMA_FROM_DEVICE);

	if (!elreq.rsp_sg_cnt) {
		ql_log(ql_log_warn, vha, 0x701b,
		    "dma_map_sg returned %d for reply.\n", elreq.rsp_sg_cnt);
		rval = -ENOMEM;
		goto done_unmap_req_sg;
	}

	if ((elreq.req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
		(elreq.rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		ql_log(ql_log_warn, vha, 0x701c,
		    "dma mapping resulted in different sg counts, "
		    "request_sg_cnt: %x dma_request_sg_cnt: %x "
		    "reply_sg_cnt: %x dma_reply_sg_cnt: %x.\n",
		    bsg_job->request_payload.sg_cnt, elreq.req_sg_cnt,
		    bsg_job->reply_payload.sg_cnt, elreq.rsp_sg_cnt);
		rval = -EAGAIN;
		goto done_unmap_sg;
	}
	req_data_len = rsp_data_len = bsg_job->request_payload.payload_len;
	req_data = dma_alloc_coherent(&ha->pdev->dev, req_data_len,
		&req_data_dma, GFP_KERNEL);
	if (!req_data) {
		ql_log(ql_log_warn, vha, 0x701d,
		    "dma alloc failed for req_data.\n");
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	rsp_data = dma_alloc_coherent(&ha->pdev->dev, rsp_data_len,
		&rsp_data_dma, GFP_KERNEL);
	if (!rsp_data) {
		ql_log(ql_log_warn, vha, 0x7004,
		    "dma alloc failed for rsp_data.\n");
		rval = -ENOMEM;
		goto done_free_dma_req;
	}

	/* Copy the request buffer in req_data now */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, req_data, req_data_len);

	elreq.send_dma = req_data_dma;
	elreq.rcv_dma = rsp_data_dma;
	elreq.transfer_size = req_data_len;

	elreq.options = bsg_request->rqst_data.h_vendor.vendor_cmd[1];
	elreq.iteration_count =
	    bsg_request->rqst_data.h_vendor.vendor_cmd[2];

	if (atomic_read(&vha->loop_state) == LOOP_READY &&
	    ((ha->current_topology == ISP_CFG_F && (elreq.options & 7) >= 2) ||
	    ((IS_QLA81XX(ha) || IS_QLA8031(ha) || IS_QLA8044(ha)) &&
	    get_unaligned_le32(req_data) == ELS_OPCODE_BYTE &&
	    req_data_len == MAX_ELS_FRAME_PAYLOAD &&
	    elreq.options == EXTERNAL_LOOPBACK))) {
		type = "FC_BSG_HST_VENDOR_ECHO_DIAG";
		ql_dbg(ql_dbg_user, vha, 0x701e,
		    "BSG request type: %s.\n", type);
		command_sent = INT_DEF_LB_ECHO_CMD;
		rval = qla2x00_echo_test(vha, &elreq, response);
	} else {
		if (IS_QLA81XX(ha) || IS_QLA8031(ha) || IS_QLA8044(ha)) {
			memset(config, 0, sizeof(config));
			memset(new_config, 0, sizeof(new_config));

			if (qla81xx_get_port_config(vha, config)) {
				ql_log(ql_log_warn, vha, 0x701f,
				    "Get port config failed.\n");
				rval = -EPERM;
				goto done_free_dma_rsp;
			}

			if ((config[0] & INTERNAL_LOOPBACK_MASK) != 0) {
				ql_dbg(ql_dbg_user, vha, 0x70c4,
				    "Loopback operation already in "
				    "progress.\n");
				rval = -EAGAIN;
				goto done_free_dma_rsp;
			}

			ql_dbg(ql_dbg_user, vha, 0x70c0,
			    "elreq.options=%04x\n", elreq.options);

			if (elreq.options == EXTERNAL_LOOPBACK)
				if (IS_QLA8031(ha) || IS_QLA8044(ha))
					rval = qla81xx_set_loopback_mode(vha,
					    config, new_config, elreq.options);
				else
					rval = qla81xx_reset_loopback_mode(vha,
					    config, 1, 0);
			else
				rval = qla81xx_set_loopback_mode(vha, config,
				    new_config, elreq.options);

			if (rval) {
				rval = -EPERM;
				goto done_free_dma_rsp;
			}

			type = "FC_BSG_HST_VENDOR_LOOPBACK";
			ql_dbg(ql_dbg_user, vha, 0x7028,
			    "BSG request type: %s.\n", type);

			command_sent = INT_DEF_LB_LOOPBACK_CMD;
			rval = qla2x00_loopback_test(vha, &elreq, response);

			if (response[0] == MBS_COMMAND_ERROR &&
					response[1] == MBS_LB_RESET) {
				ql_log(ql_log_warn, vha, 0x7029,
				    "MBX command error, Aborting ISP.\n");
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
				qla2xxx_wake_dpc(vha);
				qla2x00_wait_for_chip_reset(vha);
				/* Also reset the MPI */
				if (IS_QLA81XX(ha)) {
					if (qla81xx_restart_mpi_firmware(vha) !=
					    QLA_SUCCESS) {
						ql_log(ql_log_warn, vha, 0x702a,
						    "MPI reset failed.\n");
					}
				}

				rval = -EIO;
				goto done_free_dma_rsp;
			}

			if (new_config[0]) {
				int ret;

				/* Revert back to original port config
				 * Also clear internal loopback
				 */
				ret = qla81xx_reset_loopback_mode(vha,
				    new_config, 0, 1);
				if (ret) {
					/*
					 * If the reset of the loopback mode
					 * doesn't work take FCoE dump and then
					 * reset the chip.
					 */
					qla2xxx_dump_fw(vha);
					set_bit(ISP_ABORT_NEEDED,
					    &vha->dpc_flags);
				}

			}

		} else {
			type = "FC_BSG_HST_VENDOR_LOOPBACK";
			ql_dbg(ql_dbg_user, vha, 0x702b,
			    "BSG request type: %s.\n", type);
			command_sent = INT_DEF_LB_LOOPBACK_CMD;
			rval = qla2x00_loopback_test(vha, &elreq, response);
		}
	}

	if (rval) {
		ql_log(ql_log_warn, vha, 0x702c,
		    "Vendor request %s failed.\n", type);

		rval = 0;
		bsg_reply->result = (DID_ERROR << 16);
		bsg_reply->reply_payload_rcv_len = 0;
	} else {
		ql_dbg(ql_dbg_user, vha, 0x702d,
		    "Vendor request %s completed.\n", type);
		bsg_reply->result = (DID_OK << 16);
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, rsp_data,
			rsp_data_len);
	}

	bsg_job->reply_len = sizeof(struct fc_bsg_reply) +
	    sizeof(response) + sizeof(uint8_t);
	fw_sts_ptr = bsg_job->reply + sizeof(struct fc_bsg_reply);
	memcpy(bsg_job->reply + sizeof(struct fc_bsg_reply), response,
			sizeof(response));
	fw_sts_ptr += sizeof(response);
	*fw_sts_ptr = command_sent;

done_free_dma_rsp:
	dma_free_coherent(&ha->pdev->dev, rsp_data_len,
		rsp_data, rsp_data_dma);
done_free_dma_req:
	dma_free_coherent(&ha->pdev->dev, req_data_len,
		req_data, req_data_dma);
done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
done_unmap_req_sg:
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!rval)
		bsg_job_done(bsg_job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);
	return rval;
}

static int
qla84xx_reset(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint32_t flag;

	if (!IS_QLA84XX(ha)) {
		ql_dbg(ql_dbg_user, vha, 0x702f, "Not 84xx, exiting.\n");
		return -EINVAL;
	}

	flag = bsg_request->rqst_data.h_vendor.vendor_cmd[1];

	rval = qla84xx_reset_chip(vha, flag == A84_ISSUE_RESET_DIAG_FW);

	if (rval) {
		ql_log(ql_log_warn, vha, 0x7030,
		    "Vendor request 84xx reset failed.\n");
		rval = (DID_ERROR << 16);

	} else {
		ql_dbg(ql_dbg_user, vha, 0x7031,
		    "Vendor request 84xx reset completed.\n");
		bsg_reply->result = DID_OK;
		bsg_job_done(bsg_job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);
	}

	return rval;
}

static int
qla84xx_updatefw(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	struct verify_chip_entry_84xx *mn = NULL;
	dma_addr_t mn_dma, fw_dma;
	void *fw_buf = NULL;
	int rval = 0;
	uint32_t sg_cnt;
	uint32_t data_len;
	uint16_t options;
	uint32_t flag;
	uint32_t fw_ver;

	if (!IS_QLA84XX(ha)) {
		ql_dbg(ql_dbg_user, vha, 0x7032,
		    "Not 84xx, exiting.\n");
		return -EINVAL;
	}

	sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!sg_cnt) {
		ql_log(ql_log_warn, vha, 0x7033,
		    "dma_map_sg returned %d for request.\n", sg_cnt);
		return -ENOMEM;
	}

	if (sg_cnt != bsg_job->request_payload.sg_cnt) {
		ql_log(ql_log_warn, vha, 0x7034,
		    "DMA mapping resulted in different sg counts, "
		    "request_sg_cnt: %x dma_request_sg_cnt: %x.\n",
		    bsg_job->request_payload.sg_cnt, sg_cnt);
		rval = -EAGAIN;
		goto done_unmap_sg;
	}

	data_len = bsg_job->request_payload.payload_len;
	fw_buf = dma_alloc_coherent(&ha->pdev->dev, data_len,
		&fw_dma, GFP_KERNEL);
	if (!fw_buf) {
		ql_log(ql_log_warn, vha, 0x7035,
		    "DMA alloc failed for fw_buf.\n");
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, fw_buf, data_len);

	mn = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (!mn) {
		ql_log(ql_log_warn, vha, 0x7036,
		    "DMA alloc failed for fw buffer.\n");
		rval = -ENOMEM;
		goto done_free_fw_buf;
	}

	flag = bsg_request->rqst_data.h_vendor.vendor_cmd[1];
	fw_ver = get_unaligned_le32((uint32_t *)fw_buf + 2);

	mn->entry_type = VERIFY_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	options = VCO_FORCE_UPDATE | VCO_END_OF_DATA;
	if (flag == A84_ISSUE_UPDATE_DIAGFW_CMD)
		options |= VCO_DIAG_FW;

	mn->options = cpu_to_le16(options);
	mn->fw_ver =  cpu_to_le32(fw_ver);
	mn->fw_size =  cpu_to_le32(data_len);
	mn->fw_seq_size =  cpu_to_le32(data_len);
	put_unaligned_le64(fw_dma, &mn->dsd.address);
	mn->dsd.length = cpu_to_le32(data_len);
	mn->data_seg_cnt = cpu_to_le16(1);

	rval = qla2x00_issue_iocb_timeout(vha, mn, mn_dma, 0, 120);

	if (rval) {
		ql_log(ql_log_warn, vha, 0x7037,
		    "Vendor request 84xx updatefw failed.\n");

		rval = (DID_ERROR << 16);
	} else {
		ql_dbg(ql_dbg_user, vha, 0x7038,
		    "Vendor request 84xx updatefw completed.\n");

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		bsg_reply->result = DID_OK;
	}

	dma_pool_free(ha->s_dma_pool, mn, mn_dma);

done_free_fw_buf:
	dma_free_coherent(&ha->pdev->dev, data_len, fw_buf, fw_dma);

done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

	if (!rval)
		bsg_job_done(bsg_job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);
	return rval;
}

static int
qla84xx_mgmt_cmd(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	struct access_chip_84xx *mn = NULL;
	dma_addr_t mn_dma, mgmt_dma;
	void *mgmt_b = NULL;
	int rval = 0;
	struct qla_bsg_a84_mgmt *ql84_mgmt;
	uint32_t sg_cnt;
	uint32_t data_len = 0;
	uint32_t dma_direction = DMA_NONE;

	if (!IS_QLA84XX(ha)) {
		ql_log(ql_log_warn, vha, 0x703a,
		    "Not 84xx, exiting.\n");
		return -EINVAL;
	}

	mn = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (!mn) {
		ql_log(ql_log_warn, vha, 0x703c,
		    "DMA alloc failed for fw buffer.\n");
		return -ENOMEM;
	}

	mn->entry_type = ACCESS_CHIP_IOCB_TYPE;
	mn->entry_count = 1;
	ql84_mgmt = (void *)bsg_request + sizeof(struct fc_bsg_request);
	switch (ql84_mgmt->mgmt.cmd) {
	case QLA84_MGMT_READ_MEM:
	case QLA84_MGMT_GET_INFO:
		sg_cnt = dma_map_sg(&ha->pdev->dev,
			bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
		if (!sg_cnt) {
			ql_log(ql_log_warn, vha, 0x703d,
			    "dma_map_sg returned %d for reply.\n", sg_cnt);
			rval = -ENOMEM;
			goto exit_mgmt;
		}

		dma_direction = DMA_FROM_DEVICE;

		if (sg_cnt != bsg_job->reply_payload.sg_cnt) {
			ql_log(ql_log_warn, vha, 0x703e,
			    "DMA mapping resulted in different sg counts, "
			    "reply_sg_cnt: %x dma_reply_sg_cnt: %x.\n",
			    bsg_job->reply_payload.sg_cnt, sg_cnt);
			rval = -EAGAIN;
			goto done_unmap_sg;
		}

		data_len = bsg_job->reply_payload.payload_len;

		mgmt_b = dma_alloc_coherent(&ha->pdev->dev, data_len,
		    &mgmt_dma, GFP_KERNEL);
		if (!mgmt_b) {
			ql_log(ql_log_warn, vha, 0x703f,
			    "DMA alloc failed for mgmt_b.\n");
			rval = -ENOMEM;
			goto done_unmap_sg;
		}

		if (ql84_mgmt->mgmt.cmd == QLA84_MGMT_READ_MEM) {
			mn->options = cpu_to_le16(ACO_DUMP_MEMORY);
			mn->parameter1 =
				cpu_to_le32(
				ql84_mgmt->mgmt.mgmtp.u.mem.start_addr);

		} else if (ql84_mgmt->mgmt.cmd == QLA84_MGMT_GET_INFO) {
			mn->options = cpu_to_le16(ACO_REQUEST_INFO);
			mn->parameter1 =
				cpu_to_le32(ql84_mgmt->mgmt.mgmtp.u.info.type);

			mn->parameter2 =
				cpu_to_le32(
				ql84_mgmt->mgmt.mgmtp.u.info.context);
		}
		break;

	case QLA84_MGMT_WRITE_MEM:
		sg_cnt = dma_map_sg(&ha->pdev->dev,
			bsg_job->request_payload.sg_list,
			bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

		if (!sg_cnt) {
			ql_log(ql_log_warn, vha, 0x7040,
			    "dma_map_sg returned %d.\n", sg_cnt);
			rval = -ENOMEM;
			goto exit_mgmt;
		}

		dma_direction = DMA_TO_DEVICE;

		if (sg_cnt != bsg_job->request_payload.sg_cnt) {
			ql_log(ql_log_warn, vha, 0x7041,
			    "DMA mapping resulted in different sg counts, "
			    "request_sg_cnt: %x dma_request_sg_cnt: %x.\n",
			    bsg_job->request_payload.sg_cnt, sg_cnt);
			rval = -EAGAIN;
			goto done_unmap_sg;
		}

		data_len = bsg_job->request_payload.payload_len;
		mgmt_b = dma_alloc_coherent(&ha->pdev->dev, data_len,
			&mgmt_dma, GFP_KERNEL);
		if (!mgmt_b) {
			ql_log(ql_log_warn, vha, 0x7042,
			    "DMA alloc failed for mgmt_b.\n");
			rval = -ENOMEM;
			goto done_unmap_sg;
		}

		sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			bsg_job->request_payload.sg_cnt, mgmt_b, data_len);

		mn->options = cpu_to_le16(ACO_LOAD_MEMORY);
		mn->parameter1 =
			cpu_to_le32(ql84_mgmt->mgmt.mgmtp.u.mem.start_addr);
		break;

	case QLA84_MGMT_CHNG_CONFIG:
		mn->options = cpu_to_le16(ACO_CHANGE_CONFIG_PARAM);
		mn->parameter1 =
			cpu_to_le32(ql84_mgmt->mgmt.mgmtp.u.config.id);

		mn->parameter2 =
			cpu_to_le32(ql84_mgmt->mgmt.mgmtp.u.config.param0);

		mn->parameter3 =
			cpu_to_le32(ql84_mgmt->mgmt.mgmtp.u.config.param1);
		break;

	default:
		rval = -EIO;
		goto exit_mgmt;
	}

	if (ql84_mgmt->mgmt.cmd != QLA84_MGMT_CHNG_CONFIG) {
		mn->total_byte_cnt = cpu_to_le32(ql84_mgmt->mgmt.len);
		mn->dseg_count = cpu_to_le16(1);
		put_unaligned_le64(mgmt_dma, &mn->dsd.address);
		mn->dsd.length = cpu_to_le32(ql84_mgmt->mgmt.len);
	}

	rval = qla2x00_issue_iocb(vha, mn, mn_dma, 0);

	if (rval) {
		ql_log(ql_log_warn, vha, 0x7043,
		    "Vendor request 84xx mgmt failed.\n");

		rval = (DID_ERROR << 16);

	} else {
		ql_dbg(ql_dbg_user, vha, 0x7044,
		    "Vendor request 84xx mgmt completed.\n");

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		bsg_reply->result = DID_OK;

		if ((ql84_mgmt->mgmt.cmd == QLA84_MGMT_READ_MEM) ||
			(ql84_mgmt->mgmt.cmd == QLA84_MGMT_GET_INFO)) {
			bsg_reply->reply_payload_rcv_len =
				bsg_job->reply_payload.payload_len;

			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
				bsg_job->reply_payload.sg_cnt, mgmt_b,
				data_len);
		}
	}

done_unmap_sg:
	if (mgmt_b)
		dma_free_coherent(&ha->pdev->dev, data_len, mgmt_b, mgmt_dma);

	if (dma_direction == DMA_TO_DEVICE)
		dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
			bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	else if (dma_direction == DMA_FROM_DEVICE)
		dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

exit_mgmt:
	dma_pool_free(ha->s_dma_pool, mn, mn_dma);

	if (!rval)
		bsg_job_done(bsg_job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);
	return rval;
}

static int
qla24xx_iidma(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	int rval = 0;
	struct qla_port_param *port_param = NULL;
	fc_port_t *fcport = NULL;
	int found = 0;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint8_t *rsp_ptr = NULL;

	if (!IS_IIDMA_CAPABLE(vha->hw)) {
		ql_log(ql_log_info, vha, 0x7046, "iiDMA not supported.\n");
		return -EINVAL;
	}

	port_param = (void *)bsg_request + sizeof(struct fc_bsg_request);
	if (port_param->fc_scsi_addr.dest_type != EXT_DEF_TYPE_WWPN) {
		ql_log(ql_log_warn, vha, 0x7048,
		    "Invalid destination type.\n");
		return -EINVAL;
	}

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;

		if (memcmp(port_param->fc_scsi_addr.dest_addr.wwpn,
			fcport->port_name, sizeof(fcport->port_name)))
			continue;

		found = 1;
		break;
	}

	if (!found) {
		ql_log(ql_log_warn, vha, 0x7049,
		    "Failed to find port.\n");
		return -EINVAL;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		ql_log(ql_log_warn, vha, 0x704a,
		    "Port is not online.\n");
		return -EINVAL;
	}

	if (fcport->flags & FCF_LOGIN_NEEDED) {
		ql_log(ql_log_warn, vha, 0x704b,
		    "Remote port not logged in flags = 0x%x.\n", fcport->flags);
		return -EINVAL;
	}

	if (port_param->mode)
		rval = qla2x00_set_idma_speed(vha, fcport->loop_id,
			port_param->speed, mb);
	else
		rval = qla2x00_get_idma_speed(vha, fcport->loop_id,
			&port_param->speed, mb);

	if (rval) {
		ql_log(ql_log_warn, vha, 0x704c,
		    "iiDMA cmd failed for %8phN -- "
		    "%04x %x %04x %04x.\n", fcport->port_name,
		    rval, fcport->fp_speed, mb[0], mb[1]);
		rval = (DID_ERROR << 16);
	} else {
		if (!port_param->mode) {
			bsg_job->reply_len = sizeof(struct fc_bsg_reply) +
				sizeof(struct qla_port_param);

			rsp_ptr = ((uint8_t *)bsg_reply) +
				sizeof(struct fc_bsg_reply);

			memcpy(rsp_ptr, port_param,
				sizeof(struct qla_port_param));
		}

		bsg_reply->result = DID_OK;
		bsg_job_done(bsg_job, bsg_reply->result,
			       bsg_reply->reply_payload_rcv_len);
	}

	return rval;
}

static int
qla2x00_optrom_setup(struct bsg_job *bsg_job, scsi_qla_host_t *vha,
	uint8_t is_update)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	uint32_t start = 0;
	int valid = 0;
	struct qla_hw_data *ha = vha->hw;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return -EINVAL;

	start = bsg_request->rqst_data.h_vendor.vendor_cmd[1];
	if (start > ha->optrom_size) {
		ql_log(ql_log_warn, vha, 0x7055,
		    "start %d > optrom_size %d.\n", start, ha->optrom_size);
		return -EINVAL;
	}

	if (ha->optrom_state != QLA_SWAITING) {
		ql_log(ql_log_info, vha, 0x7056,
		    "optrom_state %d.\n", ha->optrom_state);
		return -EBUSY;
	}

	ha->optrom_region_start = start;
	ql_dbg(ql_dbg_user, vha, 0x7057, "is_update=%d.\n", is_update);
	if (is_update) {
		if (ha->optrom_size == OPTROM_SIZE_2300 && start == 0)
			valid = 1;
		else if (start == (ha->flt_region_boot * 4) ||
		    start == (ha->flt_region_fw * 4))
			valid = 1;
		else if (IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha) ||
		    IS_CNA_CAPABLE(ha) || IS_QLA2031(ha) || IS_QLA27XX(ha) ||
		    IS_QLA28XX(ha))
			valid = 1;
		if (!valid) {
			ql_log(ql_log_warn, vha, 0x7058,
			    "Invalid start region 0x%x/0x%x.\n", start,
			    bsg_job->request_payload.payload_len);
			return -EINVAL;
		}

		ha->optrom_region_size = start +
		    bsg_job->request_payload.payload_len > ha->optrom_size ?
		    ha->optrom_size - start :
		    bsg_job->request_payload.payload_len;
		ha->optrom_state = QLA_SWRITING;
	} else {
		ha->optrom_region_size = start +
		    bsg_job->reply_payload.payload_len > ha->optrom_size ?
		    ha->optrom_size - start :
		    bsg_job->reply_payload.payload_len;
		ha->optrom_state = QLA_SREADING;
	}

	ha->optrom_buffer = vzalloc(ha->optrom_region_size);
	if (!ha->optrom_buffer) {
		ql_log(ql_log_warn, vha, 0x7059,
		    "Read: Unable to allocate memory for optrom retrieval "
		    "(%x)\n", ha->optrom_region_size);

		ha->optrom_state = QLA_SWAITING;
		return -ENOMEM;
	}

	return 0;
}

static int
qla2x00_read_optrom(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;

	if (ha->flags.nic_core_reset_hdlr_active)
		return -EBUSY;

	mutex_lock(&ha->optrom_mutex);
	rval = qla2x00_optrom_setup(bsg_job, vha, 0);
	if (rval) {
		mutex_unlock(&ha->optrom_mutex);
		return rval;
	}

	ha->isp_ops->read_optrom(vha, ha->optrom_buffer,
	    ha->optrom_region_start, ha->optrom_region_size);

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, ha->optrom_buffer,
	    ha->optrom_region_size);

	bsg_reply->reply_payload_rcv_len = ha->optrom_region_size;
	bsg_reply->result = DID_OK;
	vfree(ha->optrom_buffer);
	ha->optrom_buffer = NULL;
	ha->optrom_state = QLA_SWAITING;
	mutex_unlock(&ha->optrom_mutex);
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return rval;
}

static int
qla2x00_update_optrom(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;

	mutex_lock(&ha->optrom_mutex);
	rval = qla2x00_optrom_setup(bsg_job, vha, 1);
	if (rval) {
		mutex_unlock(&ha->optrom_mutex);
		return rval;
	}

	/* Set the isp82xx_no_md_cap not to capture minidump */
	ha->flags.isp82xx_no_md_cap = 1;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, ha->optrom_buffer,
	    ha->optrom_region_size);

	rval = ha->isp_ops->write_optrom(vha, ha->optrom_buffer,
	    ha->optrom_region_start, ha->optrom_region_size);

	if (rval) {
		bsg_reply->result = -EINVAL;
		rval = -EINVAL;
	} else {
		bsg_reply->result = DID_OK;
	}
	vfree(ha->optrom_buffer);
	ha->optrom_buffer = NULL;
	ha->optrom_state = QLA_SWAITING;
	mutex_unlock(&ha->optrom_mutex);
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return rval;
}

static int
qla2x00_update_fru_versions(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint8_t bsg[DMA_POOL_SIZE];
	struct qla_image_version_list *list = (void *)bsg;
	struct qla_image_version *image;
	uint32_t count;
	dma_addr_t sfp_dma;
	void *sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);

	if (!sfp) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_NO_MEMORY;
		goto done;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, list, sizeof(bsg));

	image = list->version;
	count = list->count;
	while (count--) {
		memcpy(sfp, &image->field_info, sizeof(image->field_info));
		rval = qla2x00_write_sfp(vha, sfp_dma, sfp,
		    image->field_address.device, image->field_address.offset,
		    sizeof(image->field_info), image->field_address.option);
		if (rval) {
			bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
			    EXT_STATUS_MAILBOX;
			goto dealloc;
		}
		image++;
	}

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = 0;

dealloc:
	dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

done:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	return 0;
}

static int
qla2x00_read_fru_status(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint8_t bsg[DMA_POOL_SIZE];
	struct qla_status_reg *sr = (void *)bsg;
	dma_addr_t sfp_dma;
	uint8_t *sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);

	if (!sfp) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_NO_MEMORY;
		goto done;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, sr, sizeof(*sr));

	rval = qla2x00_read_sfp(vha, sfp_dma, sfp,
	    sr->field_address.device, sr->field_address.offset,
	    sizeof(sr->status_reg), sr->field_address.option);
	sr->status_reg = *sfp;

	if (rval) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_MAILBOX;
		goto dealloc;
	}

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, sr, sizeof(*sr));

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = 0;

dealloc:
	dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

done:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->reply_payload_rcv_len = sizeof(*sr);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	return 0;
}

static int
qla2x00_write_fru_status(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint8_t bsg[DMA_POOL_SIZE];
	struct qla_status_reg *sr = (void *)bsg;
	dma_addr_t sfp_dma;
	uint8_t *sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);

	if (!sfp) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_NO_MEMORY;
		goto done;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, sr, sizeof(*sr));

	*sfp = sr->status_reg;
	rval = qla2x00_write_sfp(vha, sfp_dma, sfp,
	    sr->field_address.device, sr->field_address.offset,
	    sizeof(sr->status_reg), sr->field_address.option);

	if (rval) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_MAILBOX;
		goto dealloc;
	}

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = 0;

dealloc:
	dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

done:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	return 0;
}

static int
qla2x00_write_i2c(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint8_t bsg[DMA_POOL_SIZE];
	struct qla_i2c_access *i2c = (void *)bsg;
	dma_addr_t sfp_dma;
	uint8_t *sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);

	if (!sfp) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_NO_MEMORY;
		goto done;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, i2c, sizeof(*i2c));

	memcpy(sfp, i2c->buffer, i2c->length);
	rval = qla2x00_write_sfp(vha, sfp_dma, sfp,
	    i2c->device, i2c->offset, i2c->length, i2c->option);

	if (rval) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_MAILBOX;
		goto dealloc;
	}

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = 0;

dealloc:
	dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

done:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	return 0;
}

static int
qla2x00_read_i2c(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint8_t bsg[DMA_POOL_SIZE];
	struct qla_i2c_access *i2c = (void *)bsg;
	dma_addr_t sfp_dma;
	uint8_t *sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);

	if (!sfp) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_NO_MEMORY;
		goto done;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, i2c, sizeof(*i2c));

	rval = qla2x00_read_sfp(vha, sfp_dma, sfp,
		i2c->device, i2c->offset, i2c->length, i2c->option);

	if (rval) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_MAILBOX;
		goto dealloc;
	}

	memcpy(i2c->buffer, sfp, i2c->length);
	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, i2c, sizeof(*i2c));

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = 0;

dealloc:
	dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

done:
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->reply_payload_rcv_len = sizeof(*i2c);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	return 0;
}

static int
qla24xx_process_bidir_cmd(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	uint32_t rval = EXT_STATUS_OK;
	uint16_t req_sg_cnt = 0;
	uint16_t rsp_sg_cnt = 0;
	uint16_t nextlid = 0;
	uint32_t tot_dsds;
	srb_t *sp = NULL;
	uint32_t req_data_len;
	uint32_t rsp_data_len;

	/* Check the type of the adapter */
	if (!IS_BIDI_CAPABLE(ha)) {
		ql_log(ql_log_warn, vha, 0x70a0,
			"This adapter is not supported\n");
		rval = EXT_STATUS_NOT_SUPPORTED;
		goto done;
	}

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
		test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
		rval =  EXT_STATUS_BUSY;
		goto done;
	}

	/* Check if host is online */
	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x70a1,
			"Host is not online\n");
		rval = EXT_STATUS_DEVICE_OFFLINE;
		goto done;
	}

	/* Check if cable is plugged in or not */
	if (vha->device_flags & DFLG_NO_CABLE) {
		ql_log(ql_log_warn, vha, 0x70a2,
			"Cable is unplugged...\n");
		rval = EXT_STATUS_INVALID_CFG;
		goto done;
	}

	/* Check if the switch is connected or not */
	if (ha->current_topology != ISP_CFG_F) {
		ql_log(ql_log_warn, vha, 0x70a3,
			"Host is not connected to the switch\n");
		rval = EXT_STATUS_INVALID_CFG;
		goto done;
	}

	/* Check if operating mode is P2P */
	if (ha->operating_mode != P2P) {
		ql_log(ql_log_warn, vha, 0x70a4,
		    "Host operating mode is not P2p\n");
		rval = EXT_STATUS_INVALID_CFG;
		goto done;
	}

	mutex_lock(&ha->selflogin_lock);
	if (vha->self_login_loop_id == 0) {
		/* Initialize all required  fields of fcport */
		vha->bidir_fcport.vha = vha;
		vha->bidir_fcport.d_id.b.al_pa = vha->d_id.b.al_pa;
		vha->bidir_fcport.d_id.b.area = vha->d_id.b.area;
		vha->bidir_fcport.d_id.b.domain = vha->d_id.b.domain;
		vha->bidir_fcport.loop_id = vha->loop_id;

		if (qla2x00_fabric_login(vha, &(vha->bidir_fcport), &nextlid)) {
			ql_log(ql_log_warn, vha, 0x70a7,
			    "Failed to login port %06X for bidirectional IOCB\n",
			    vha->bidir_fcport.d_id.b24);
			mutex_unlock(&ha->selflogin_lock);
			rval = EXT_STATUS_MAILBOX;
			goto done;
		}
		vha->self_login_loop_id = nextlid - 1;

	}
	/* Assign the self login loop id to fcport */
	mutex_unlock(&ha->selflogin_lock);

	vha->bidir_fcport.loop_id = vha->self_login_loop_id;

	req_sg_cnt = dma_map_sg(&ha->pdev->dev,
		bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt,
		DMA_TO_DEVICE);

	if (!req_sg_cnt) {
		rval = EXT_STATUS_NO_MEMORY;
		goto done;
	}

	rsp_sg_cnt = dma_map_sg(&ha->pdev->dev,
		bsg_job->reply_payload.sg_list, bsg_job->reply_payload.sg_cnt,
		DMA_FROM_DEVICE);

	if (!rsp_sg_cnt) {
		rval = EXT_STATUS_NO_MEMORY;
		goto done_unmap_req_sg;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
		(rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		ql_dbg(ql_dbg_user, vha, 0x70a9,
		    "Dma mapping resulted in different sg counts "
		    "[request_sg_cnt: %x dma_request_sg_cnt: %x reply_sg_cnt: "
		    "%x dma_reply_sg_cnt: %x]\n",
		    bsg_job->request_payload.sg_cnt, req_sg_cnt,
		    bsg_job->reply_payload.sg_cnt, rsp_sg_cnt);
		rval = EXT_STATUS_NO_MEMORY;
		goto done_unmap_sg;
	}

	req_data_len = bsg_job->request_payload.payload_len;
	rsp_data_len = bsg_job->reply_payload.payload_len;

	if (req_data_len != rsp_data_len) {
		rval = EXT_STATUS_BUSY;
		ql_log(ql_log_warn, vha, 0x70aa,
		    "req_data_len != rsp_data_len\n");
		goto done_unmap_sg;
	}

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, &(vha->bidir_fcport), GFP_KERNEL);
	if (!sp) {
		ql_dbg(ql_dbg_user, vha, 0x70ac,
		    "Alloc SRB structure failed\n");
		rval = EXT_STATUS_NO_MEMORY;
		goto done_unmap_sg;
	}

	/*Populate srb->ctx with bidir ctx*/
	sp->u.bsg_job = bsg_job;
	sp->free = qla2x00_bsg_sp_free;
	sp->type = SRB_BIDI_CMD;
	sp->done = qla2x00_bsg_job_done;

	/* Add the read and write sg count */
	tot_dsds = rsp_sg_cnt + req_sg_cnt;

	rval = qla2x00_start_bidir(sp, vha, tot_dsds);
	if (rval != EXT_STATUS_OK)
		goto done_free_srb;
	/* the bsg request  will be completed in the interrupt handler */
	return rval;

done_free_srb:
	mempool_free(sp, ha->srb_mempool);
done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
done_unmap_req_sg:
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
done:

	/* Return an error vendor specific response
	 * and complete the bsg request
	 */
	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = rval;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->reply_payload_rcv_len = 0;
	bsg_reply->result = (DID_OK) << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	/* Always return success, vendor rsp carries correct status */
	return 0;
}

static int
qlafx00_mgmt_cmd(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = (DID_ERROR << 16);
	struct qla_mt_iocb_rqst_fx00 *piocb_rqst;
	srb_t *sp;
	int req_sg_cnt = 0, rsp_sg_cnt = 0;
	struct fc_port *fcport;
	char  *type = "FC_BSG_HST_FX_MGMT";

	/* Copy the IOCB specific information */
	piocb_rqst = (struct qla_mt_iocb_rqst_fx00 *)
	    &bsg_request->rqst_data.h_vendor.vendor_cmd[1];

	/* Dump the vendor information */
	ql_dump_buffer(ql_dbg_user + ql_dbg_verbose , vha, 0x70cf,
	    piocb_rqst, sizeof(*piocb_rqst));

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x70d0,
		    "Host is not online.\n");
		rval = -EIO;
		goto done;
	}

	if (piocb_rqst->flags & SRB_FXDISC_REQ_DMA_VALID) {
		req_sg_cnt = dma_map_sg(&ha->pdev->dev,
		    bsg_job->request_payload.sg_list,
		    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
		if (!req_sg_cnt) {
			ql_log(ql_log_warn, vha, 0x70c7,
			    "dma_map_sg return %d for request\n", req_sg_cnt);
			rval = -ENOMEM;
			goto done;
		}
	}

	if (piocb_rqst->flags & SRB_FXDISC_RESP_DMA_VALID) {
		rsp_sg_cnt = dma_map_sg(&ha->pdev->dev,
		    bsg_job->reply_payload.sg_list,
		    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
		if (!rsp_sg_cnt) {
			ql_log(ql_log_warn, vha, 0x70c8,
			    "dma_map_sg return %d for reply\n", rsp_sg_cnt);
			rval = -ENOMEM;
			goto done_unmap_req_sg;
		}
	}

	ql_dbg(ql_dbg_user, vha, 0x70c9,
	    "request_sg_cnt: %x dma_request_sg_cnt: %x reply_sg_cnt:%x "
	    "dma_reply_sg_cnt: %x\n", bsg_job->request_payload.sg_cnt,
	    req_sg_cnt, bsg_job->reply_payload.sg_cnt, rsp_sg_cnt);

	/* Allocate a dummy fcport structure, since functions preparing the
	 * IOCB and mailbox command retrieves port specific information
	 * from fcport structure. For Host based ELS commands there will be
	 * no fcport structure allocated
	 */
	fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (!fcport) {
		ql_log(ql_log_warn, vha, 0x70ca,
		    "Failed to allocate fcport.\n");
		rval = -ENOMEM;
		goto done_unmap_rsp_sg;
	}

	/* Alloc SRB structure */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		ql_log(ql_log_warn, vha, 0x70cb,
		    "qla2x00_get_sp failed.\n");
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	/* Initialize all required  fields of fcport */
	fcport->vha = vha;
	fcport->loop_id = le32_to_cpu(piocb_rqst->dataword);

	sp->type = SRB_FXIOCB_BCMD;
	sp->name = "bsg_fx_mgmt";
	sp->iocbs = qla24xx_calc_ct_iocbs(req_sg_cnt + rsp_sg_cnt);
	sp->u.bsg_job = bsg_job;
	sp->free = qla2x00_bsg_sp_free;
	sp->done = qla2x00_bsg_job_done;

	ql_dbg(ql_dbg_user, vha, 0x70cc,
	    "bsg rqst type: %s fx_mgmt_type: %x id=%x\n",
	    type, piocb_rqst->func_type, fcport->loop_id);

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x70cd,
		    "qla2x00_start_sp failed=%d.\n", rval);
		mempool_free(sp, ha->srb_mempool);
		rval = -EIO;
		goto done_free_fcport;
	}
	return rval;

done_free_fcport:
	qla2x00_free_fcport(fcport);

done_unmap_rsp_sg:
	if (piocb_rqst->flags & SRB_FXDISC_RESP_DMA_VALID)
		dma_unmap_sg(&ha->pdev->dev,
		    bsg_job->reply_payload.sg_list,
		    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
done_unmap_req_sg:
	if (piocb_rqst->flags & SRB_FXDISC_REQ_DMA_VALID)
		dma_unmap_sg(&ha->pdev->dev,
		    bsg_job->request_payload.sg_list,
		    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

done:
	return rval;
}

static int
qla26xx_serdes_op(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	int rval = 0;
	struct qla_serdes_reg sr;

	memset(&sr, 0, sizeof(sr));

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &sr, sizeof(sr));

	switch (sr.cmd) {
	case INT_SC_SERDES_WRITE_REG:
		rval = qla2x00_write_serdes_word(vha, sr.addr, sr.val);
		bsg_reply->reply_payload_rcv_len = 0;
		break;
	case INT_SC_SERDES_READ_REG:
		rval = qla2x00_read_serdes_word(vha, sr.addr, &sr.val);
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
		    bsg_job->reply_payload.sg_cnt, &sr, sizeof(sr));
		bsg_reply->reply_payload_rcv_len = sizeof(sr);
		break;
	default:
		ql_dbg(ql_dbg_user, vha, 0x708c,
		    "Unknown serdes cmd %x.\n", sr.cmd);
		rval = -EINVAL;
		break;
	}

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
	    rval ? EXT_STATUS_MAILBOX : 0;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return 0;
}

static int
qla8044_serdes_op(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	int rval = 0;
	struct qla_serdes_reg_ex sr;

	memset(&sr, 0, sizeof(sr));

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &sr, sizeof(sr));

	switch (sr.cmd) {
	case INT_SC_SERDES_WRITE_REG:
		rval = qla8044_write_serdes_word(vha, sr.addr, sr.val);
		bsg_reply->reply_payload_rcv_len = 0;
		break;
	case INT_SC_SERDES_READ_REG:
		rval = qla8044_read_serdes_word(vha, sr.addr, &sr.val);
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
		    bsg_job->reply_payload.sg_cnt, &sr, sizeof(sr));
		bsg_reply->reply_payload_rcv_len = sizeof(sr);
		break;
	default:
		ql_dbg(ql_dbg_user, vha, 0x7020,
		    "Unknown serdes cmd %x.\n", sr.cmd);
		rval = -EINVAL;
		break;
	}

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
	    rval ? EXT_STATUS_MAILBOX : 0;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return 0;
}

static int
qla27xx_get_flash_upd_cap(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	struct qla_flash_update_caps cap;

	if (!(IS_QLA27XX(ha)) && !IS_QLA28XX(ha))
		return -EPERM;

	memset(&cap, 0, sizeof(cap));
	cap.capabilities = (uint64_t)ha->fw_attributes_ext[1] << 48 |
			   (uint64_t)ha->fw_attributes_ext[0] << 32 |
			   (uint64_t)ha->fw_attributes_h << 16 |
			   (uint64_t)ha->fw_attributes;

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, &cap, sizeof(cap));
	bsg_reply->reply_payload_rcv_len = sizeof(cap);

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
	    EXT_STATUS_OK;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return 0;
}

static int
qla27xx_set_flash_upd_cap(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	uint64_t online_fw_attr = 0;
	struct qla_flash_update_caps cap;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return -EPERM;

	memset(&cap, 0, sizeof(cap));
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, &cap, sizeof(cap));

	online_fw_attr = (uint64_t)ha->fw_attributes_ext[1] << 48 |
			 (uint64_t)ha->fw_attributes_ext[0] << 32 |
			 (uint64_t)ha->fw_attributes_h << 16 |
			 (uint64_t)ha->fw_attributes;

	if (online_fw_attr != cap.capabilities) {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_INVALID_PARAM;
		return -EINVAL;
	}

	if (cap.outage_duration < MAX_LOOP_TIMEOUT)  {
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
		    EXT_STATUS_INVALID_PARAM;
		return -EINVAL;
	}

	bsg_reply->reply_payload_rcv_len = 0;

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
	    EXT_STATUS_OK;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return 0;
}

static int
qla27xx_get_bbcr_data(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	struct qla_bbcr_data bbcr;
	uint16_t loop_id, topo, sw_cap;
	uint8_t domain, area, al_pa, state;
	int rval;

	if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return -EPERM;

	memset(&bbcr, 0, sizeof(bbcr));

	if (vha->flags.bbcr_enable)
		bbcr.status = QLA_BBCR_STATUS_ENABLED;
	else
		bbcr.status = QLA_BBCR_STATUS_DISABLED;

	if (bbcr.status == QLA_BBCR_STATUS_ENABLED) {
		rval = qla2x00_get_adapter_id(vha, &loop_id, &al_pa,
			&area, &domain, &topo, &sw_cap);
		if (rval != QLA_SUCCESS) {
			bbcr.status = QLA_BBCR_STATUS_UNKNOWN;
			bbcr.state = QLA_BBCR_STATE_OFFLINE;
			bbcr.mbx1 = loop_id;
			goto done;
		}

		state = (vha->bbcr >> 12) & 0x1;

		if (state) {
			bbcr.state = QLA_BBCR_STATE_OFFLINE;
			bbcr.offline_reason_code = QLA_BBCR_REASON_LOGIN_REJECT;
		} else {
			bbcr.state = QLA_BBCR_STATE_ONLINE;
			bbcr.negotiated_bbscn = (vha->bbcr >> 8) & 0xf;
		}

		bbcr.configured_bbscn = vha->bbcr & 0xf;
	}

done:
	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, &bbcr, sizeof(bbcr));
	bsg_reply->reply_payload_rcv_len = sizeof(bbcr);

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;

	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	return 0;
}

static int
qla2x00_get_priv_stats(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);
	struct link_statistics *stats = NULL;
	dma_addr_t stats_dma;
	int rval;
	uint32_t *cmd = bsg_request->rqst_data.h_vendor.vendor_cmd;
	uint options = cmd[0] == QL_VND_GET_PRIV_STATS_EX ? cmd[1] : 0;

	if (test_bit(UNLOADING, &vha->dpc_flags))
		return -ENODEV;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return -ENODEV;

	if (qla2x00_reset_active(vha))
		return -EBUSY;

	if (!IS_FWI2_CAPABLE(ha))
		return -EPERM;

	stats = dma_alloc_coherent(&ha->pdev->dev, sizeof(*stats), &stats_dma,
				   GFP_KERNEL);
	if (!stats) {
		ql_log(ql_log_warn, vha, 0x70e2,
		    "Failed to allocate memory for stats.\n");
		return -ENOMEM;
	}

	rval = qla24xx_get_isp_stats(base_vha, stats, stats_dma, options);

	if (rval == QLA_SUCCESS) {
		ql_dump_buffer(ql_dbg_user + ql_dbg_verbose, vha, 0x70e5,
			stats, sizeof(*stats));
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, stats, sizeof(*stats));
	}

	bsg_reply->reply_payload_rcv_len = sizeof(*stats);
	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
	    rval ? EXT_STATUS_MAILBOX : EXT_STATUS_OK;

	bsg_job->reply_len = sizeof(*bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	dma_free_coherent(&ha->pdev->dev, sizeof(*stats),
		stats, stats_dma);

	return 0;
}

static int
qla2x00_do_dport_diagnostics(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct Scsi_Host *host = fc_bsg_to_shost(bsg_job);
	scsi_qla_host_t *vha = shost_priv(host);
	int rval;
	struct qla_dport_diag *dd;

	if (!IS_QLA83XX(vha->hw) && !IS_QLA27XX(vha->hw) &&
	    !IS_QLA28XX(vha->hw))
		return -EPERM;

	dd = kmalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd) {
		ql_log(ql_log_warn, vha, 0x70db,
		    "Failed to allocate memory for dport.\n");
		return -ENOMEM;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, dd, sizeof(*dd));

	rval = qla26xx_dport_diagnostics(
	    vha, dd->buf, sizeof(dd->buf), dd->options);
	if (rval == QLA_SUCCESS) {
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
		    bsg_job->reply_payload.sg_cnt, dd, sizeof(*dd));
	}

	bsg_reply->reply_payload_rcv_len = sizeof(*dd);
	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] =
	    rval ? EXT_STATUS_MAILBOX : EXT_STATUS_OK;

	bsg_job->reply_len = sizeof(*bsg_reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);

	kfree(dd);

	return 0;
}

static int
qla2x00_get_flash_image_status(struct bsg_job *bsg_job)
{
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct qla_hw_data *ha = vha->hw;
	struct qla_active_regions regions = { };
	struct active_regions active_regions = { };

	qla27xx_get_active_image(vha, &active_regions);
	regions.global_image = active_regions.global;

	if (IS_QLA28XX(ha)) {
		qla28xx_get_aux_images(vha, &active_regions);
		regions.board_config = active_regions.aux.board_config;
		regions.vpd_nvram = active_regions.aux.vpd_nvram;
		regions.npiv_config_0_1 = active_regions.aux.npiv_config_0_1;
		regions.npiv_config_2_3 = active_regions.aux.npiv_config_2_3;
	}

	ql_dbg(ql_dbg_user, vha, 0x70e1,
	    "%s(%lu): FW=%u BCFG=%u VPDNVR=%u NPIV01=%u NPIV02=%u\n",
	    __func__, vha->host_no, regions.global_image,
	    regions.board_config, regions.vpd_nvram,
	    regions.npiv_config_0_1, regions.npiv_config_2_3);

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, &regions, sizeof(regions));

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;
	bsg_reply->reply_payload_rcv_len = sizeof(regions);
	bsg_reply->result = DID_OK << 16;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	bsg_job_done(bsg_job, bsg_reply->result,
	    bsg_reply->reply_payload_rcv_len);

	return 0;
}

static int
qla2x00_manage_host_stats(struct bsg_job *bsg_job)
{
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct ql_vnd_mng_host_stats_param *req_data;
	struct ql_vnd_mng_host_stats_resp rsp_data;
	u32 req_data_len;
	int ret = 0;

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x0000, "Host is not online.\n");
		return -EIO;
	}

	req_data_len = bsg_job->request_payload.payload_len;

	if (req_data_len != sizeof(struct ql_vnd_mng_host_stats_param)) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data_len invalid.\n");
		return -EIO;
	}

	req_data = kzalloc(sizeof(*req_data), GFP_KERNEL);
	if (!req_data) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data memory allocation failure.\n");
		return -ENOMEM;
	}

	/* Copy the request buffer in req_data */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, req_data,
			  req_data_len);

	switch (req_data->action) {
	case QLA_STOP:
		ret = qla2xxx_stop_stats(vha->host, req_data->stat_type);
		break;
	case QLA_START:
		ret = qla2xxx_start_stats(vha->host, req_data->stat_type);
		break;
	case QLA_CLEAR:
		ret = qla2xxx_reset_stats(vha->host, req_data->stat_type);
		break;
	default:
		ql_log(ql_log_warn, vha, 0x0000, "Invalid action.\n");
		ret = -EIO;
		break;
	}

	kfree(req_data);

	/* Prepare response */
	rsp_data.status = ret;
	bsg_job->reply_payload.payload_len = sizeof(struct ql_vnd_mng_host_stats_resp);

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;
	bsg_reply->reply_payload_rcv_len =
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
				    bsg_job->reply_payload.sg_cnt,
				    &rsp_data,
				    sizeof(struct ql_vnd_mng_host_stats_resp));

	bsg_reply->result = DID_OK;
	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);

	return ret;
}

static int
qla2x00_get_host_stats(struct bsg_job *bsg_job)
{
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct ql_vnd_stats_param *req_data;
	struct ql_vnd_host_stats_resp rsp_data;
	u32 req_data_len;
	int ret = 0;
	u64 ini_entry_count = 0;
	u64 entry_count = 0;
	u64 tgt_num = 0;
	u64 tmp_stat_type = 0;
	u64 response_len = 0;
	void *data;

	req_data_len = bsg_job->request_payload.payload_len;

	if (req_data_len != sizeof(struct ql_vnd_stats_param)) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data_len invalid.\n");
		return -EIO;
	}

	req_data = kzalloc(sizeof(*req_data), GFP_KERNEL);
	if (!req_data) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data memory allocation failure.\n");
		return -ENOMEM;
	}

	/* Copy the request buffer in req_data */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, req_data, req_data_len);

	/* Copy stat type to work on it */
	tmp_stat_type = req_data->stat_type;

	if (tmp_stat_type & QLA2XX_TGT_SHT_LNK_DOWN) {
		/* Num of tgts connected to this host */
		tgt_num = qla2x00_get_num_tgts(vha);
		/* unset BIT_17 */
		tmp_stat_type &= ~(1 << 17);
	}

	/* Total ini stats */
	ini_entry_count = qla2x00_count_set_bits(tmp_stat_type);

	/* Total number of entries */
	entry_count = ini_entry_count + tgt_num;

	response_len = sizeof(struct ql_vnd_host_stats_resp) +
		(sizeof(struct ql_vnd_stat_entry) * entry_count);

	if (response_len > bsg_job->reply_payload.payload_len) {
		rsp_data.status = EXT_STATUS_BUFFER_TOO_SMALL;
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_BUFFER_TOO_SMALL;
		bsg_job->reply_payload.payload_len = sizeof(struct ql_vnd_mng_host_stats_resp);

		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
					    bsg_job->reply_payload.sg_cnt, &rsp_data,
					    sizeof(struct ql_vnd_mng_host_stats_resp));

		bsg_reply->result = DID_OK;
		bsg_job_done(bsg_job, bsg_reply->result,
			     bsg_reply->reply_payload_rcv_len);
		goto host_stat_out;
	}

	data = kzalloc(response_len, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto host_stat_out;
	}

	ret = qla2xxx_get_ini_stats(fc_bsg_to_shost(bsg_job), req_data->stat_type,
				    data, response_len);

	rsp_data.status = EXT_STATUS_OK;
	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;

	bsg_reply->reply_payload_rcv_len = sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
							       bsg_job->reply_payload.sg_cnt,
							       data, response_len);
	bsg_reply->result = DID_OK;
	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);

	kfree(data);
host_stat_out:
	kfree(req_data);
	return ret;
}

static struct fc_rport *
qla2xxx_find_rport(scsi_qla_host_t *vha, uint32_t tgt_num)
{
	fc_port_t *fcport = NULL;

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->rport->number == tgt_num)
			return fcport->rport;
	}
	return NULL;
}

static int
qla2x00_get_tgt_stats(struct bsg_job *bsg_job)
{
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct ql_vnd_tgt_stats_param *req_data;
	u32 req_data_len;
	int ret = 0;
	u64 response_len = 0;
	struct ql_vnd_tgt_stats_resp *data = NULL;
	struct fc_rport *rport = NULL;

	if (!vha->flags.online) {
		ql_log(ql_log_warn, vha, 0x0000, "Host is not online.\n");
		return -EIO;
	}

	req_data_len = bsg_job->request_payload.payload_len;

	if (req_data_len != sizeof(struct ql_vnd_stat_entry)) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data_len invalid.\n");
		return -EIO;
	}

	req_data = kzalloc(sizeof(*req_data), GFP_KERNEL);
	if (!req_data) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data memory allocation failure.\n");
		return -ENOMEM;
	}

	/* Copy the request buffer in req_data */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt,
			  req_data, req_data_len);

	response_len = sizeof(struct ql_vnd_tgt_stats_resp) +
		sizeof(struct ql_vnd_stat_entry);

	/* structure + size for one entry */
	data = kzalloc(response_len, GFP_KERNEL);
	if (!data) {
		kfree(req_data);
		return -ENOMEM;
	}

	if (response_len > bsg_job->reply_payload.payload_len) {
		data->status = EXT_STATUS_BUFFER_TOO_SMALL;
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_BUFFER_TOO_SMALL;
		bsg_job->reply_payload.payload_len = sizeof(struct ql_vnd_mng_host_stats_resp);

		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
					    bsg_job->reply_payload.sg_cnt, data,
					    sizeof(struct ql_vnd_tgt_stats_resp));

		bsg_reply->result = DID_OK;
		bsg_job_done(bsg_job, bsg_reply->result,
			     bsg_reply->reply_payload_rcv_len);
		goto tgt_stat_out;
	}

	rport = qla2xxx_find_rport(vha, req_data->tgt_id);
	if (!rport) {
		ql_log(ql_log_warn, vha, 0x0000, "target %d not found.\n", req_data->tgt_id);
		ret = EXT_STATUS_INVALID_PARAM;
		data->status = EXT_STATUS_INVALID_PARAM;
		goto reply;
	}

	ret = qla2xxx_get_tgt_stats(fc_bsg_to_shost(bsg_job), req_data->stat_type,
				    rport, (void *)data, response_len);

	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;
reply:
	bsg_reply->reply_payload_rcv_len =
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
				    bsg_job->reply_payload.sg_cnt, data,
				    response_len);
	bsg_reply->result = DID_OK;
	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);

tgt_stat_out:
	kfree(data);
	kfree(req_data);

	return ret;
}

static int
qla2x00_manage_host_port(struct bsg_job *bsg_job)
{
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	struct ql_vnd_mng_host_port_param *req_data;
	struct ql_vnd_mng_host_port_resp rsp_data;
	u32 req_data_len;
	int ret = 0;

	req_data_len = bsg_job->request_payload.payload_len;

	if (req_data_len != sizeof(struct ql_vnd_mng_host_port_param)) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data_len invalid.\n");
		return -EIO;
	}

	req_data = kzalloc(sizeof(*req_data), GFP_KERNEL);
	if (!req_data) {
		ql_log(ql_log_warn, vha, 0x0000, "req_data memory allocation failure.\n");
		return -ENOMEM;
	}

	/* Copy the request buffer in req_data */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, req_data, req_data_len);

	switch (req_data->action) {
	case QLA_ENABLE:
		ret = qla2xxx_enable_port(vha->host);
		break;
	case QLA_DISABLE:
		ret = qla2xxx_disable_port(vha->host);
		break;
	default:
		ql_log(ql_log_warn, vha, 0x0000, "Invalid action.\n");
		ret = -EIO;
		break;
	}

	kfree(req_data);

	/* Prepare response */
	rsp_data.status = ret;
	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;
	bsg_job->reply_payload.payload_len = sizeof(struct ql_vnd_mng_host_port_resp);

	bsg_reply->reply_payload_rcv_len =
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
				    bsg_job->reply_payload.sg_cnt, &rsp_data,
				    sizeof(struct ql_vnd_mng_host_port_resp));
	bsg_reply->result = DID_OK;
	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);

	return ret;
}

static int
qla2x00_process_vendor_specific(struct scsi_qla_host *vha, struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;

	ql_dbg(ql_dbg_edif, vha, 0x911b, "%s FC_BSG_HST_VENDOR cmd[0]=0x%x\n",
	    __func__, bsg_request->rqst_data.h_vendor.vendor_cmd[0]);

	switch (bsg_request->rqst_data.h_vendor.vendor_cmd[0]) {
	case QL_VND_LOOPBACK:
		return qla2x00_process_loopback(bsg_job);

	case QL_VND_A84_RESET:
		return qla84xx_reset(bsg_job);

	case QL_VND_A84_UPDATE_FW:
		return qla84xx_updatefw(bsg_job);

	case QL_VND_A84_MGMT_CMD:
		return qla84xx_mgmt_cmd(bsg_job);

	case QL_VND_IIDMA:
		return qla24xx_iidma(bsg_job);

	case QL_VND_FCP_PRIO_CFG_CMD:
		return qla24xx_proc_fcp_prio_cfg_cmd(bsg_job);

	case QL_VND_READ_FLASH:
		return qla2x00_read_optrom(bsg_job);

	case QL_VND_UPDATE_FLASH:
		return qla2x00_update_optrom(bsg_job);

	case QL_VND_SET_FRU_VERSION:
		return qla2x00_update_fru_versions(bsg_job);

	case QL_VND_READ_FRU_STATUS:
		return qla2x00_read_fru_status(bsg_job);

	case QL_VND_WRITE_FRU_STATUS:
		return qla2x00_write_fru_status(bsg_job);

	case QL_VND_WRITE_I2C:
		return qla2x00_write_i2c(bsg_job);

	case QL_VND_READ_I2C:
		return qla2x00_read_i2c(bsg_job);

	case QL_VND_DIAG_IO_CMD:
		return qla24xx_process_bidir_cmd(bsg_job);

	case QL_VND_FX00_MGMT_CMD:
		return qlafx00_mgmt_cmd(bsg_job);

	case QL_VND_SERDES_OP:
		return qla26xx_serdes_op(bsg_job);

	case QL_VND_SERDES_OP_EX:
		return qla8044_serdes_op(bsg_job);

	case QL_VND_GET_FLASH_UPDATE_CAPS:
		return qla27xx_get_flash_upd_cap(bsg_job);

	case QL_VND_SET_FLASH_UPDATE_CAPS:
		return qla27xx_set_flash_upd_cap(bsg_job);

	case QL_VND_GET_BBCR_DATA:
		return qla27xx_get_bbcr_data(bsg_job);

	case QL_VND_GET_PRIV_STATS:
	case QL_VND_GET_PRIV_STATS_EX:
		return qla2x00_get_priv_stats(bsg_job);

	case QL_VND_DPORT_DIAGNOSTICS:
		return qla2x00_do_dport_diagnostics(bsg_job);

	case QL_VND_EDIF_MGMT:
		return qla_edif_app_mgmt(bsg_job);

	case QL_VND_SS_GET_FLASH_IMAGE_STATUS:
		return qla2x00_get_flash_image_status(bsg_job);

	case QL_VND_MANAGE_HOST_STATS:
		return qla2x00_manage_host_stats(bsg_job);

	case QL_VND_GET_HOST_STATS:
		return qla2x00_get_host_stats(bsg_job);

	case QL_VND_GET_TGT_STATS:
		return qla2x00_get_tgt_stats(bsg_job);

	case QL_VND_MANAGE_HOST_PORT:
		return qla2x00_manage_host_port(bsg_job);

	case QL_VND_MBX_PASSTHRU:
		return qla2x00_mailbox_passthru(bsg_job);

	default:
		return -ENOSYS;
	}
}

int
qla24xx_bsg_request(struct bsg_job *bsg_job)
{
	struct fc_bsg_request *bsg_request = bsg_job->request;
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	int ret = -EINVAL;
	struct fc_rport *rport;
	struct Scsi_Host *host;
	scsi_qla_host_t *vha;

	/* In case no data transferred. */
	bsg_reply->reply_payload_rcv_len = 0;

	if (bsg_request->msgcode == FC_BSG_RPT_ELS) {
		rport = fc_bsg_to_rport(bsg_job);
		host = rport_to_shost(rport);
		vha = shost_priv(host);
	} else {
		host = fc_bsg_to_shost(bsg_job);
		vha = shost_priv(host);
	}

	/* Disable port will bring down the chip, allow enable command */
	if (bsg_request->rqst_data.h_vendor.vendor_cmd[0] == QL_VND_MANAGE_HOST_PORT ||
	    bsg_request->rqst_data.h_vendor.vendor_cmd[0] == QL_VND_GET_HOST_STATS)
		goto skip_chip_chk;

	if (vha->hw->flags.port_isolated) {
		bsg_reply->result = DID_ERROR;
		/* operation not permitted */
		return -EPERM;
	}

	if (qla2x00_chip_is_down(vha)) {
		ql_dbg(ql_dbg_user, vha, 0x709f,
		    "BSG: ISP abort active/needed -- cmd=%d.\n",
		    bsg_request->msgcode);
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		return -EBUSY;
	}

	if (test_bit(PFLG_DRIVER_REMOVING, &vha->pci_flags)) {
		SET_DID_STATUS(bsg_reply->result, DID_ERROR);
		return -EIO;
	}

skip_chip_chk:
	ql_dbg(ql_dbg_user + ql_dbg_verbose, vha, 0x7000,
	    "Entered %s msgcode=0x%x. bsg ptr %px\n",
	    __func__, bsg_request->msgcode, bsg_job);

	switch (bsg_request->msgcode) {
	case FC_BSG_RPT_ELS:
	case FC_BSG_HST_ELS_NOLOGIN:
		ret = qla2x00_process_els(bsg_job);
		break;
	case FC_BSG_HST_CT:
		ret = qla2x00_process_ct(bsg_job);
		break;
	case FC_BSG_HST_VENDOR:
		ret = qla2x00_process_vendor_specific(vha, bsg_job);
		break;
	case FC_BSG_HST_ADD_RPORT:
	case FC_BSG_HST_DEL_RPORT:
	case FC_BSG_RPT_CT:
	default:
		ql_log(ql_log_warn, vha, 0x705a, "Unsupported BSG request.\n");
		break;
	}

	ql_dbg(ql_dbg_user + ql_dbg_verbose, vha, 0x7000,
	    "%s done with return %x\n", __func__, ret);

	return ret;
}

int
qla24xx_bsg_timeout(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	int cnt, que;
	unsigned long flags;
	struct req_que *req;

	ql_log(ql_log_info, vha, 0x708b, "%s CMD timeout. bsg ptr %p.\n",
	    __func__, bsg_job);
	/* find the bsg job from the active list of commands */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (que = 0; que < ha->max_req_queues; que++) {
		req = ha->req_q_map[que];
		if (!req)
			continue;

		for (cnt = 1; cnt < req->num_outstanding_cmds; cnt++) {
			sp = req->outstanding_cmds[cnt];
			if (sp &&
			    (sp->type == SRB_CT_CMD ||
			     sp->type == SRB_ELS_CMD_HST ||
			     sp->type == SRB_ELS_CMD_HST_NOLOGIN ||
			     sp->type == SRB_FXIOCB_BCMD) &&
			    sp->u.bsg_job == bsg_job) {
				req->outstanding_cmds[cnt] = NULL;
				spin_unlock_irqrestore(&ha->hardware_lock, flags);
				if (ha->isp_ops->abort_command(sp)) {
					ql_log(ql_log_warn, vha, 0x7089,
					    "mbx abort_command failed.\n");
					bsg_reply->result = -EIO;
				} else {
					ql_dbg(ql_dbg_user, vha, 0x708a,
					    "mbx abort_command success.\n");
					bsg_reply->result = 0;
				}
				spin_lock_irqsave(&ha->hardware_lock, flags);
				goto done;

			}
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	ql_log(ql_log_info, vha, 0x708b, "SRB not found to abort.\n");
	bsg_reply->result = -ENXIO;
	return 0;

done:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	sp->free(sp);
	return 0;
}

int qla2x00_mailbox_passthru(struct bsg_job *bsg_job)
{
	struct fc_bsg_reply *bsg_reply = bsg_job->reply;
	scsi_qla_host_t *vha = shost_priv(fc_bsg_to_shost(bsg_job));
	int ret = -EINVAL;
	int ptsize = sizeof(struct qla_mbx_passthru);
	struct qla_mbx_passthru *req_data = NULL;
	uint32_t req_data_len;

	req_data_len = bsg_job->request_payload.payload_len;
	if (req_data_len != ptsize) {
		ql_log(ql_log_warn, vha, 0xf0a3, "req_data_len invalid.\n");
		return -EIO;
	}
	req_data = kzalloc(ptsize, GFP_KERNEL);
	if (!req_data) {
		ql_log(ql_log_warn, vha, 0xf0a4,
		       "req_data memory allocation failure.\n");
		return -ENOMEM;
	}

	/* Copy the request buffer in req_data */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, req_data, ptsize);
	ret = qla_mailbox_passthru(vha, req_data->mbx_in, req_data->mbx_out);

	/* Copy the req_data in  request buffer */
	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
			    bsg_job->reply_payload.sg_cnt, req_data, ptsize);

	bsg_reply->reply_payload_rcv_len = ptsize;
	if (ret == QLA_SUCCESS)
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_OK;
	else
		bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = EXT_STATUS_ERR;

	bsg_job->reply_len = sizeof(*bsg_job->reply);
	bsg_reply->result = DID_OK << 16;
	bsg_job_done(bsg_job, bsg_reply->result, bsg_reply->reply_payload_rcv_len);

	kfree(req_data);

	return ret;
}

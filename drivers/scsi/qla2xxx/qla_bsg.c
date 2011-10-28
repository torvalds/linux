/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

/* BSG support for ELS/CT pass through */
inline srb_t *
qla2x00_get_ctx_bsg_sp(scsi_qla_host_t *vha, fc_port_t *fcport, size_t size)
{
	srb_t *sp;
	struct qla_hw_data *ha = vha->hw;
	struct srb_ctx *ctx;

	sp = mempool_alloc(ha->srb_mempool, GFP_KERNEL);
	if (!sp)
		goto done;
	ctx = kzalloc(size, GFP_KERNEL);
	if (!ctx) {
		mempool_free(sp, ha->srb_mempool);
		sp = NULL;
		goto done;
	}

	memset(sp, 0, sizeof(*sp));
	sp->fcport = fcport;
	sp->ctx = ctx;
done:
	return sp;
}

int
qla24xx_fcp_prio_cfg_valid(struct qla_fcp_prio_cfg *pri_cfg, uint8_t flag)
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
		DEBUG2(printk(KERN_INFO
		    "%s: No FCP priority config data.\n",
		    __func__));
		return 0;
	}

	if (bcode[0] != 'H' || bcode[1] != 'Q' || bcode[2] != 'O' ||
			bcode[3] != 'S') {
		/* Invalid FCP priority data header*/
		DEBUG2(printk(KERN_ERR
		    "%s: Invalid FCP Priority data header. bcode=0x%x\n",
		    __func__, bcode_val));
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
		DEBUG2(printk(KERN_ERR
		    "%s: No valid FCP Priority data entries.\n",
		    __func__));
		ret = 0;
	} else {
		/* FCP priority data is valid */
		DEBUG2(printk(KERN_INFO
		    "%s: Valid FCP priority data. num entries = %d\n",
		    __func__, num_valid));
	}

	return ret;
}

static int
qla24xx_proc_fcp_prio_cfg_cmd(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int ret = 0;
	uint32_t len;
	uint32_t oper;

	bsg_job->reply->reply_payload_rcv_len = 0;

	if (!(IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha))) {
		ret = -EINVAL;
		goto exit_fcp_prio_cfg;
	}

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
		test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
		ret = -EBUSY;
		goto exit_fcp_prio_cfg;
	}

	/* Get the sub command */
	oper = bsg_job->request->rqst_data.h_vendor.vendor_cmd[1];

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
			bsg_job->reply->result = DID_OK;
		} else {
			ret = -EINVAL;
			bsg_job->reply->result = (DID_ERROR << 16);
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
				bsg_job->reply->result = DID_OK;
			} else {
				ret = -EINVAL;
				bsg_job->reply->result = (DID_ERROR << 16);
				goto exit_fcp_prio_cfg;
			}
		}
		break;

	case QLFC_FCP_PRIO_GET_CONFIG:
		len = bsg_job->reply_payload.payload_len;
		if (!len || len > FCP_PRIO_CFG_SIZE) {
			ret = -EINVAL;
			bsg_job->reply->result = (DID_ERROR << 16);
			goto exit_fcp_prio_cfg;
		}

		bsg_job->reply->result = DID_OK;
		bsg_job->reply->reply_payload_rcv_len =
			sg_copy_from_buffer(
			bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, ha->fcp_prio_cfg,
			len);

		break;

	case QLFC_FCP_PRIO_SET_CONFIG:
		len = bsg_job->request_payload.payload_len;
		if (!len || len > FCP_PRIO_CFG_SIZE) {
			bsg_job->reply->result = (DID_ERROR << 16);
			ret = -EINVAL;
			goto exit_fcp_prio_cfg;
		}

		if (!ha->fcp_prio_cfg) {
			ha->fcp_prio_cfg = vmalloc(FCP_PRIO_CFG_SIZE);
			if (!ha->fcp_prio_cfg) {
				qla_printk(KERN_WARNING, ha,
					"Unable to allocate memory "
					"for fcp prio config data (%x).\n",
					FCP_PRIO_CFG_SIZE);
				bsg_job->reply->result = (DID_ERROR << 16);
				ret = -ENOMEM;
				goto exit_fcp_prio_cfg;
			}
		}

		memset(ha->fcp_prio_cfg, 0, FCP_PRIO_CFG_SIZE);
		sg_copy_to_buffer(bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, ha->fcp_prio_cfg,
			FCP_PRIO_CFG_SIZE);

		/* validate fcp priority data */
		if (!qla24xx_fcp_prio_cfg_valid(
			(struct qla_fcp_prio_cfg *)
			ha->fcp_prio_cfg, 1)) {
			bsg_job->reply->result = (DID_ERROR << 16);
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
		bsg_job->reply->result = DID_OK;
		break;
	default:
		ret = -EINVAL;
		break;
	}
exit_fcp_prio_cfg:
	bsg_job->job_done(bsg_job);
	return ret;
}
static int
qla2x00_process_els(struct fc_bsg_job *bsg_job)
{
	struct fc_rport *rport;
	fc_port_t *fcport = NULL;
	struct Scsi_Host *host;
	scsi_qla_host_t *vha;
	struct qla_hw_data *ha;
	srb_t *sp;
	const char *type;
	int req_sg_cnt, rsp_sg_cnt;
	int rval =  (DRIVER_ERROR << 16);
	uint16_t nextlid = 0;
	struct srb_ctx *els;

	if (bsg_job->request->msgcode == FC_BSG_RPT_ELS) {
		rport = bsg_job->rport;
		fcport = *(fc_port_t **) rport->dd_data;
		host = rport_to_shost(rport);
		vha = shost_priv(host);
		ha = vha->hw;
		type = "FC_BSG_RPT_ELS";
	} else {
		host = bsg_job->shost;
		vha = shost_priv(host);
		ha = vha->hw;
		type = "FC_BSG_HST_ELS_NOLOGIN";
	}

	/* pass through is supported only for ISP 4Gb or higher */
	if (!IS_FWI2_CAPABLE(ha)) {
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "scsi(%ld):ELS passthru not supported for ISP23xx based "
		    "adapters\n", vha->host_no));
		rval = -EPERM;
		goto done;
	}

	/*  Multiple SG's are not supported for ELS requests */
	if (bsg_job->request_payload.sg_cnt > 1 ||
		bsg_job->reply_payload.sg_cnt > 1) {
		DEBUG2(printk(KERN_INFO
			"multiple SG's are not supported for ELS requests"
			" [request_sg_cnt: %x reply_sg_cnt: %x]\n",
			bsg_job->request_payload.sg_cnt,
			bsg_job->reply_payload.sg_cnt));
		rval = -EPERM;
		goto done;
	}

	/* ELS request for rport */
	if (bsg_job->request->msgcode == FC_BSG_RPT_ELS) {
		/* make sure the rport is logged in,
		 * if not perform fabric login
		 */
		if (qla2x00_fabric_login(vha, fcport, &nextlid)) {
			DEBUG2(qla_printk(KERN_WARNING, ha,
			"failed to login port %06X for ELS passthru\n",
			fcport->d_id.b24));
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
		fcport->vp_idx = vha->vp_idx;
		fcport->d_id.b.al_pa =
			bsg_job->request->rqst_data.h_els.port_id[0];
		fcport->d_id.b.area =
			bsg_job->request->rqst_data.h_els.port_id[1];
		fcport->d_id.b.domain =
			bsg_job->request->rqst_data.h_els.port_id[2];
		fcport->loop_id =
			(fcport->d_id.b.al_pa == 0xFD) ?
			NPH_FABRIC_CONTROLLER : NPH_F_PORT;
	}

	if (!vha->flags.online) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		"host not online\n"));
		rval = -EIO;
		goto done;
	}

	req_sg_cnt =
		dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!req_sg_cnt) {
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	rsp_sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
        if (!rsp_sg_cnt) {
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
		(rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		DEBUG2(printk(KERN_INFO
			"dma mapping resulted in different sg counts \
			[request_sg_cnt: %x dma_request_sg_cnt: %x\
			reply_sg_cnt: %x dma_reply_sg_cnt: %x]\n",
			bsg_job->request_payload.sg_cnt, req_sg_cnt,
			bsg_job->reply_payload.sg_cnt, rsp_sg_cnt));
		rval = -EAGAIN;
		goto done_unmap_sg;
	}

	/* Alloc SRB structure */
	sp = qla2x00_get_ctx_bsg_sp(vha, fcport, sizeof(struct srb_ctx));
	if (!sp) {
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	els = sp->ctx;
	els->type =
		(bsg_job->request->msgcode == FC_BSG_RPT_ELS ?
		SRB_ELS_CMD_RPT : SRB_ELS_CMD_HST);
	els->name =
		(bsg_job->request->msgcode == FC_BSG_RPT_ELS ?
		"bsg_els_rpt" : "bsg_els_hst");
	els->u.bsg_job = bsg_job;

	DEBUG2(qla_printk(KERN_INFO, ha,
		"scsi(%ld:%x): bsg rqst type: %s els type: %x - loop-id=%x "
		"portid=%02x%02x%02x.\n", vha->host_no, sp->handle, type,
		bsg_job->request->rqst_data.h_els.command_code,
		fcport->loop_id, fcport->d_id.b.domain, fcport->d_id.b.area,
		fcport->d_id.b.al_pa));

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		kfree(sp->ctx);
		mempool_free(sp, ha->srb_mempool);
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
	if (bsg_job->request->msgcode == FC_BSG_HST_ELS_NOLOGIN)
		kfree(fcport);
done:
	return rval;
}

static int
qla2x00_process_ct(struct fc_bsg_job *bsg_job)
{
	srb_t *sp;
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = (DRIVER_ERROR << 16);
	int req_sg_cnt, rsp_sg_cnt;
	uint16_t loop_id;
	struct fc_port *fcport;
	char  *type = "FC_BSG_HST_CT";
	struct srb_ctx *ct;

	req_sg_cnt =
		dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
			bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!req_sg_cnt) {
		rval = -ENOMEM;
		goto done;
	}

	rsp_sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	if (!rsp_sg_cnt) {
		rval = -ENOMEM;
		goto done;
	}

	if ((req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
	    (rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "[request_sg_cnt: %x dma_request_sg_cnt: %x\
		    reply_sg_cnt: %x dma_reply_sg_cnt: %x]\n",
		    bsg_job->request_payload.sg_cnt, req_sg_cnt,
		    bsg_job->reply_payload.sg_cnt, rsp_sg_cnt));
		rval = -EAGAIN;
		goto done_unmap_sg;
	}

	if (!vha->flags.online) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
			"host not online\n"));
		rval = -EIO;
		goto done_unmap_sg;
	}

	loop_id =
		(bsg_job->request->rqst_data.h_ct.preamble_word1 & 0xFF000000)
			>> 24;
	switch (loop_id) {
	case 0xFC:
		loop_id = cpu_to_le16(NPH_SNS);
		break;
	case 0xFA:
		loop_id = vha->mgmt_svr_loop_id;
		break;
	default:
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "Unknown loop id: %x\n", loop_id));
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
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	/* Initialize all required  fields of fcport */
	fcport->vha = vha;
	fcport->vp_idx = vha->vp_idx;
	fcport->d_id.b.al_pa = bsg_job->request->rqst_data.h_ct.port_id[0];
	fcport->d_id.b.area = bsg_job->request->rqst_data.h_ct.port_id[1];
	fcport->d_id.b.domain = bsg_job->request->rqst_data.h_ct.port_id[2];
	fcport->loop_id = loop_id;

	/* Alloc SRB structure */
	sp = qla2x00_get_ctx_bsg_sp(vha, fcport, sizeof(struct srb_ctx));
	if (!sp) {
		rval = -ENOMEM;
		goto done_free_fcport;
	}

	ct = sp->ctx;
	ct->type = SRB_CT_CMD;
	ct->name = "bsg_ct";
	ct->u.bsg_job = bsg_job;

	DEBUG2(qla_printk(KERN_INFO, ha,
		"scsi(%ld:%x): bsg rqst type: %s els type: %x - loop-id=%x "
		"portid=%02x%02x%02x.\n", vha->host_no, sp->handle, type,
		(bsg_job->request->rqst_data.h_ct.preamble_word2 >> 16),
		fcport->loop_id, fcport->d_id.b.domain, fcport->d_id.b.area,
		fcport->d_id.b.al_pa));

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		kfree(sp->ctx);
		mempool_free(sp, ha->srb_mempool);
		rval = -EIO;
		goto done_free_fcport;
	}
	return rval;

done_free_fcport:
	kfree(fcport);
done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
		bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
done:
	return rval;
}

/* Set the port configuration to enable the
 * internal loopback on ISP81XX
 */
static inline int
qla81xx_set_internal_loopback(scsi_qla_host_t *vha, uint16_t *config,
    uint16_t *new_config)
{
	int ret = 0;
	int rval = 0;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha))
		goto done_set_internal;

	new_config[0] = config[0] | (ENABLE_INTERNAL_LOOPBACK << 1);
	memcpy(&new_config[1], &config[1], sizeof(uint16_t) * 3) ;

	ha->notify_dcbx_comp = 1;
	ret = qla81xx_set_port_config(vha, new_config);
	if (ret != QLA_SUCCESS) {
		DEBUG2(printk(KERN_ERR
		    "%s(%lu): Set port config failed\n",
		    __func__, vha->host_no));
		ha->notify_dcbx_comp = 0;
		rval = -EINVAL;
		goto done_set_internal;
	}

	/* Wait for DCBX complete event */
	if (!wait_for_completion_timeout(&ha->dcbx_comp, (20 * HZ))) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "State change notificaition not received.\n"));
	} else
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "State change RECEIVED\n"));

	ha->notify_dcbx_comp = 0;

done_set_internal:
	return rval;
}

/* Set the port configuration to disable the
 * internal loopback on ISP81XX
 */
static inline int
qla81xx_reset_internal_loopback(scsi_qla_host_t *vha, uint16_t *config,
    int wait)
{
	int ret = 0;
	int rval = 0;
	uint16_t new_config[4];
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA81XX(ha))
		goto done_reset_internal;

	memset(new_config, 0 , sizeof(new_config));
	if ((config[0] & INTERNAL_LOOPBACK_MASK) >> 1 ==
			ENABLE_INTERNAL_LOOPBACK) {
		new_config[0] = config[0] & ~INTERNAL_LOOPBACK_MASK;
		memcpy(&new_config[1], &config[1], sizeof(uint16_t) * 3) ;

		ha->notify_dcbx_comp = wait;
		ret = qla81xx_set_port_config(vha, new_config);
		if (ret != QLA_SUCCESS) {
			DEBUG2(printk(KERN_ERR
			    "%s(%lu): Set port config failed\n",
			     __func__, vha->host_no));
			ha->notify_dcbx_comp = 0;
			rval = -EINVAL;
			goto done_reset_internal;
		}

		/* Wait for DCBX complete event */
		if (wait && !wait_for_completion_timeout(&ha->dcbx_comp,
			(20 * HZ))) {
			DEBUG2(qla_printk(KERN_WARNING, ha,
			    "State change notificaition not received.\n"));
			ha->notify_dcbx_comp = 0;
			rval = -EINVAL;
			goto done_reset_internal;
		} else
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "State change RECEIVED\n"));

		ha->notify_dcbx_comp = 0;
	}
done_reset_internal:
	return rval;
}

static int
qla2x00_process_loopback(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval;
	uint8_t command_sent;
	char *type;
	struct msg_echo_lb elreq;
	uint16_t response[MAILBOX_REGISTER_COUNT];
	uint16_t config[4], new_config[4];
	uint8_t *fw_sts_ptr;
	uint8_t *req_data = NULL;
	dma_addr_t req_data_dma;
	uint32_t req_data_len;
	uint8_t *rsp_data = NULL;
	dma_addr_t rsp_data_dma;
	uint32_t rsp_data_len;

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
		test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &vha->dpc_flags))
		return -EBUSY;

	if (!vha->flags.online) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "host not online\n"));
		return -EIO;
	}

	elreq.req_sg_cnt = dma_map_sg(&ha->pdev->dev,
		bsg_job->request_payload.sg_list, bsg_job->request_payload.sg_cnt,
		DMA_TO_DEVICE);

	if (!elreq.req_sg_cnt)
		return -ENOMEM;

	elreq.rsp_sg_cnt = dma_map_sg(&ha->pdev->dev,
		bsg_job->reply_payload.sg_list, bsg_job->reply_payload.sg_cnt,
		DMA_FROM_DEVICE);

	if (!elreq.rsp_sg_cnt) {
		rval = -ENOMEM;
		goto done_unmap_req_sg;
	}

	if ((elreq.req_sg_cnt !=  bsg_job->request_payload.sg_cnt) ||
		(elreq.rsp_sg_cnt != bsg_job->reply_payload.sg_cnt)) {
		DEBUG2(printk(KERN_INFO
			"dma mapping resulted in different sg counts "
			"[request_sg_cnt: %x dma_request_sg_cnt: %x "
			"reply_sg_cnt: %x dma_reply_sg_cnt: %x]\n",
			bsg_job->request_payload.sg_cnt, elreq.req_sg_cnt,
			bsg_job->reply_payload.sg_cnt, elreq.rsp_sg_cnt));
		rval = -EAGAIN;
		goto done_unmap_sg;
	}
	req_data_len = rsp_data_len = bsg_job->request_payload.payload_len;
	req_data = dma_alloc_coherent(&ha->pdev->dev, req_data_len,
		&req_data_dma, GFP_KERNEL);
	if (!req_data) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for req_data "
			"failed for host=%lu\n", __func__, vha->host_no));
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	rsp_data = dma_alloc_coherent(&ha->pdev->dev, rsp_data_len,
		&rsp_data_dma, GFP_KERNEL);
	if (!rsp_data) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for rsp_data "
			"failed for host=%lu\n", __func__, vha->host_no));
		rval = -ENOMEM;
		goto done_free_dma_req;
	}

	/* Copy the request buffer in req_data now */
	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, req_data, req_data_len);

	elreq.send_dma = req_data_dma;
	elreq.rcv_dma = rsp_data_dma;
	elreq.transfer_size = req_data_len;

	elreq.options = bsg_job->request->rqst_data.h_vendor.vendor_cmd[1];

	if ((ha->current_topology == ISP_CFG_F ||
	    (IS_QLA81XX(ha) &&
	    le32_to_cpu(*(uint32_t *)req_data) == ELS_OPCODE_BYTE
	    && req_data_len == MAX_ELS_FRAME_PAYLOAD)) &&
		elreq.options == EXTERNAL_LOOPBACK) {
		type = "FC_BSG_HST_VENDOR_ECHO_DIAG";
		DEBUG2(qla_printk(KERN_INFO, ha,
			"scsi(%ld) bsg rqst type: %s\n", vha->host_no, type));
		command_sent = INT_DEF_LB_ECHO_CMD;
		rval = qla2x00_echo_test(vha, &elreq, response);
	} else {
		if (IS_QLA81XX(ha)) {
			memset(config, 0, sizeof(config));
			memset(new_config, 0, sizeof(new_config));
			if (qla81xx_get_port_config(vha, config)) {
				DEBUG2(printk(KERN_ERR
					"%s(%lu): Get port config failed\n",
					__func__, vha->host_no));
				bsg_job->reply->reply_payload_rcv_len = 0;
				bsg_job->reply->result = (DID_ERROR << 16);
				rval = -EPERM;
				goto done_free_dma_req;
			}

			if (elreq.options != EXTERNAL_LOOPBACK) {
				DEBUG2(qla_printk(KERN_INFO, ha,
					"Internal: current port config = %x\n",
					config[0]));
				if (qla81xx_set_internal_loopback(vha, config,
					new_config)) {
					bsg_job->reply->reply_payload_rcv_len =
						0;
					bsg_job->reply->result =
						(DID_ERROR << 16);
					rval = -EPERM;
					goto done_free_dma_req;
				}
			} else {
				/* For external loopback to work
				 * ensure internal loopback is disabled
				 */
				if (qla81xx_reset_internal_loopback(vha,
					config, 1)) {
					bsg_job->reply->reply_payload_rcv_len =
						0;
					bsg_job->reply->result =
						(DID_ERROR << 16);
					rval = -EPERM;
					goto done_free_dma_req;
				}
			}

			type = "FC_BSG_HST_VENDOR_LOOPBACK";
			DEBUG2(qla_printk(KERN_INFO, ha,
				"scsi(%ld) bsg rqst type: %s\n",
				vha->host_no, type));

			command_sent = INT_DEF_LB_LOOPBACK_CMD;
			rval = qla2x00_loopback_test(vha, &elreq, response);

			if (new_config[0]) {
				/* Revert back to original port config
				 * Also clear internal loopback
				 */
				qla81xx_reset_internal_loopback(vha,
				    new_config, 0);
			}

			if (response[0] == MBS_COMMAND_ERROR &&
					response[1] == MBS_LB_RESET) {
				DEBUG2(printk(KERN_ERR "%s(%ld): ABORTing "
					"ISP\n", __func__, vha->host_no));
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
				qla2xxx_wake_dpc(vha);
				qla2x00_wait_for_chip_reset(vha);
				/* Also reset the MPI */
				if (qla81xx_restart_mpi_firmware(vha) !=
				    QLA_SUCCESS) {
					qla_printk(KERN_INFO, ha,
					    "MPI reset failed for host%ld.\n",
					    vha->host_no);
				}

				bsg_job->reply->reply_payload_rcv_len = 0;
				bsg_job->reply->result = (DID_ERROR << 16);
				rval = -EIO;
				goto done_free_dma_req;
			}
		} else {
			type = "FC_BSG_HST_VENDOR_LOOPBACK";
			DEBUG2(qla_printk(KERN_INFO, ha,
				"scsi(%ld) bsg rqst type: %s\n",
				vha->host_no, type));
			command_sent = INT_DEF_LB_LOOPBACK_CMD;
			rval = qla2x00_loopback_test(vha, &elreq, response);
		}
	}

	if (rval) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
		    "request %s failed\n", vha->host_no, type));

		fw_sts_ptr = ((uint8_t *)bsg_job->req->sense) +
		    sizeof(struct fc_bsg_reply);

		memcpy(fw_sts_ptr, response, sizeof(response));
		fw_sts_ptr += sizeof(response);
		*fw_sts_ptr = command_sent;
		rval = 0;
		bsg_job->reply->reply_payload_rcv_len = 0;
		bsg_job->reply->result = (DID_ERROR << 16);
	} else {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
			"request %s completed\n", vha->host_no, type));

		bsg_job->reply_len = sizeof(struct fc_bsg_reply) +
			sizeof(response) + sizeof(uint8_t);
		bsg_job->reply->reply_payload_rcv_len =
			bsg_job->reply_payload.payload_len;
		fw_sts_ptr = ((uint8_t *)bsg_job->req->sense) +
			sizeof(struct fc_bsg_reply);
		memcpy(fw_sts_ptr, response, sizeof(response));
		fw_sts_ptr += sizeof(response);
		*fw_sts_ptr = command_sent;
		bsg_job->reply->result = DID_OK;
		sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, rsp_data,
			rsp_data_len);
	}
	bsg_job->job_done(bsg_job);

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
	return rval;
}

static int
qla84xx_reset(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	uint32_t flag;

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
	    test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &vha->dpc_flags))
		return -EBUSY;

	if (!IS_QLA84XX(ha)) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld): Not 84xx, "
		   "exiting.\n", vha->host_no));
		return -EINVAL;
	}

	flag = bsg_job->request->rqst_data.h_vendor.vendor_cmd[1];

	rval = qla84xx_reset_chip(vha, flag == A84_ISSUE_RESET_DIAG_FW);

	if (rval) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
		    "request 84xx reset failed\n", vha->host_no));
		rval = bsg_job->reply->reply_payload_rcv_len = 0;
		bsg_job->reply->result = (DID_ERROR << 16);

	} else {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
		    "request 84xx reset completed\n", vha->host_no));
		bsg_job->reply->result = DID_OK;
	}

	bsg_job->job_done(bsg_job);
	return rval;
}

static int
qla84xx_updatefw(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
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

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
		test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &vha->dpc_flags))
		return -EBUSY;

	if (!IS_QLA84XX(ha)) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld): Not 84xx, "
			"exiting.\n", vha->host_no));
		return -EINVAL;
	}

	sg_cnt = dma_map_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	if (!sg_cnt)
		return -ENOMEM;

	if (sg_cnt != bsg_job->request_payload.sg_cnt) {
		DEBUG2(printk(KERN_INFO
			"dma mapping resulted in different sg counts "
			"request_sg_cnt: %x dma_request_sg_cnt: %x ",
			bsg_job->request_payload.sg_cnt, sg_cnt));
		rval = -EAGAIN;
		goto done_unmap_sg;
	}

	data_len = bsg_job->request_payload.payload_len;
	fw_buf = dma_alloc_coherent(&ha->pdev->dev, data_len,
		&fw_dma, GFP_KERNEL);
	if (!fw_buf) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for fw_buf "
			"failed for host=%lu\n", __func__, vha->host_no));
		rval = -ENOMEM;
		goto done_unmap_sg;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, fw_buf, data_len);

	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (!mn) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for fw buffer "
			"failed for host=%lu\n", __func__, vha->host_no));
		rval = -ENOMEM;
		goto done_free_fw_buf;
	}

	flag = bsg_job->request->rqst_data.h_vendor.vendor_cmd[1];
	fw_ver = le32_to_cpu(*((uint32_t *)((uint32_t *)fw_buf + 2)));

	memset(mn, 0, sizeof(struct access_chip_84xx));
	mn->entry_type = VERIFY_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	options = VCO_FORCE_UPDATE | VCO_END_OF_DATA;
	if (flag == A84_ISSUE_UPDATE_DIAGFW_CMD)
		options |= VCO_DIAG_FW;

	mn->options = cpu_to_le16(options);
	mn->fw_ver =  cpu_to_le32(fw_ver);
	mn->fw_size =  cpu_to_le32(data_len);
	mn->fw_seq_size =  cpu_to_le32(data_len);
	mn->dseg_address[0] = cpu_to_le32(LSD(fw_dma));
	mn->dseg_address[1] = cpu_to_le32(MSD(fw_dma));
	mn->dseg_length = cpu_to_le32(data_len);
	mn->data_seg_cnt = cpu_to_le16(1);

	rval = qla2x00_issue_iocb_timeout(vha, mn, mn_dma, 0, 120);

	if (rval) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
			"request 84xx updatefw failed\n", vha->host_no));

		rval = bsg_job->reply->reply_payload_rcv_len = 0;
		bsg_job->reply->result = (DID_ERROR << 16);

	} else {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
			"request 84xx updatefw completed\n", vha->host_no));

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		bsg_job->reply->result = DID_OK;
	}

	bsg_job->job_done(bsg_job);
	dma_pool_free(ha->s_dma_pool, mn, mn_dma);

done_free_fw_buf:
	dma_free_coherent(&ha->pdev->dev, data_len, fw_buf, fw_dma);

done_unmap_sg:
	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
		bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

	return rval;
}

static int
qla84xx_mgmt_cmd(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
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

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
		test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &vha->dpc_flags))
		return -EBUSY;

	if (!IS_QLA84XX(ha)) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld): Not 84xx, "
			"exiting.\n", vha->host_no));
		return -EINVAL;
	}

	ql84_mgmt = (struct qla_bsg_a84_mgmt *)((char *)bsg_job->request +
		sizeof(struct fc_bsg_request));
	if (!ql84_mgmt) {
		DEBUG2(printk("%s(%ld): mgmt header not provided, exiting.\n",
			__func__, vha->host_no));
		return -EINVAL;
	}

	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (!mn) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for fw buffer "
			"failed for host=%lu\n", __func__, vha->host_no));
		return -ENOMEM;
	}

	memset(mn, 0, sizeof(struct access_chip_84xx));
	mn->entry_type = ACCESS_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	switch (ql84_mgmt->mgmt.cmd) {
	case QLA84_MGMT_READ_MEM:
	case QLA84_MGMT_GET_INFO:
		sg_cnt = dma_map_sg(&ha->pdev->dev,
			bsg_job->reply_payload.sg_list,
			bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
		if (!sg_cnt) {
			rval = -ENOMEM;
			goto exit_mgmt;
		}

		dma_direction = DMA_FROM_DEVICE;

		if (sg_cnt != bsg_job->reply_payload.sg_cnt) {
			DEBUG2(printk(KERN_INFO
				"dma mapping resulted in different sg counts "
				"reply_sg_cnt: %x dma_reply_sg_cnt: %x\n",
				bsg_job->reply_payload.sg_cnt, sg_cnt));
			rval = -EAGAIN;
			goto done_unmap_sg;
		}

		data_len = bsg_job->reply_payload.payload_len;

		mgmt_b = dma_alloc_coherent(&ha->pdev->dev, data_len,
		    &mgmt_dma, GFP_KERNEL);
		if (!mgmt_b) {
			DEBUG2(printk(KERN_ERR "%s: dma alloc for mgmt_b "
				"failed for host=%lu\n",
				__func__, vha->host_no));
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
			rval = -ENOMEM;
			goto exit_mgmt;
		}

		dma_direction = DMA_TO_DEVICE;

		if (sg_cnt != bsg_job->request_payload.sg_cnt) {
			DEBUG2(printk(KERN_INFO
				"dma mapping resulted in different sg counts "
				"request_sg_cnt: %x dma_request_sg_cnt: %x ",
				bsg_job->request_payload.sg_cnt, sg_cnt));
			rval = -EAGAIN;
			goto done_unmap_sg;
		}

		data_len = bsg_job->request_payload.payload_len;
		mgmt_b = dma_alloc_coherent(&ha->pdev->dev, data_len,
			&mgmt_dma, GFP_KERNEL);
		if (!mgmt_b) {
			DEBUG2(printk(KERN_ERR "%s: dma alloc for mgmt_b "
				"failed for host=%lu\n",
				__func__, vha->host_no));
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
		mn->dseg_address[0] = cpu_to_le32(LSD(mgmt_dma));
		mn->dseg_address[1] = cpu_to_le32(MSD(mgmt_dma));
		mn->dseg_length = cpu_to_le32(ql84_mgmt->mgmt.len);
	}

	rval = qla2x00_issue_iocb(vha, mn, mn_dma, 0);

	if (rval) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
			"request 84xx mgmt failed\n", vha->host_no));

		rval = bsg_job->reply->reply_payload_rcv_len = 0;
		bsg_job->reply->result = (DID_ERROR << 16);

	} else {
		DEBUG2(qla_printk(KERN_WARNING, ha, "scsi(%ld) Vendor "
			"request 84xx mgmt completed\n", vha->host_no));

		bsg_job->reply_len = sizeof(struct fc_bsg_reply);
		bsg_job->reply->result = DID_OK;

		if ((ql84_mgmt->mgmt.cmd == QLA84_MGMT_READ_MEM) ||
			(ql84_mgmt->mgmt.cmd == QLA84_MGMT_GET_INFO)) {
			bsg_job->reply->reply_payload_rcv_len =
				bsg_job->reply_payload.payload_len;

			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
				bsg_job->reply_payload.sg_cnt, mgmt_b,
				data_len);
		}
	}

	bsg_job->job_done(bsg_job);

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

	return rval;
}

static int
qla24xx_iidma(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;
	struct qla_port_param *port_param = NULL;
	fc_port_t *fcport = NULL;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint8_t *rsp_ptr = NULL;

	bsg_job->reply->reply_payload_rcv_len = 0;

	if (test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
		test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &vha->dpc_flags))
		return -EBUSY;

	if (!IS_IIDMA_CAPABLE(vha->hw)) {
		DEBUG2(qla_printk(KERN_WARNING, ha, "%s(%lu): iiDMA not "
			"supported\n",  __func__, vha->host_no));
		return -EINVAL;
	}

	port_param = (struct qla_port_param *)((char *)bsg_job->request +
		sizeof(struct fc_bsg_request));
	if (!port_param) {
		DEBUG2(printk("%s(%ld): port_param header not provided, "
			"exiting.\n", __func__, vha->host_no));
		return -EINVAL;
	}

	if (port_param->fc_scsi_addr.dest_type != EXT_DEF_TYPE_WWPN) {
		DEBUG2(printk(KERN_ERR "%s(%ld): Invalid destination type\n",
			__func__, vha->host_no));
		return -EINVAL;
	}

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;

		if (memcmp(port_param->fc_scsi_addr.dest_addr.wwpn,
			fcport->port_name, sizeof(fcport->port_name)))
			continue;
		break;
	}

	if (!fcport) {
		DEBUG2(printk(KERN_ERR "%s(%ld): Failed to find port\n",
			__func__, vha->host_no));
		return -EINVAL;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		DEBUG2(printk(KERN_ERR "%s(%ld): Port not online\n",
			__func__, vha->host_no));
		return -EINVAL;
	}

	if (fcport->flags & FCF_LOGIN_NEEDED) {
		DEBUG2(printk(KERN_ERR "%s(%ld): Remote port not logged in, "
		    "flags = 0x%x\n",
		    __func__, vha->host_no, fcport->flags));
		return -EINVAL;
	}

	if (port_param->mode)
		rval = qla2x00_set_idma_speed(vha, fcport->loop_id,
			port_param->speed, mb);
	else
		rval = qla2x00_get_idma_speed(vha, fcport->loop_id,
			&port_param->speed, mb);

	if (rval) {
		DEBUG16(printk(KERN_ERR "scsi(%ld): iIDMA cmd failed for "
			"%02x%02x%02x%02x%02x%02x%02x%02x -- "
			"%04x %x %04x %04x.\n",
			vha->host_no, fcport->port_name[0],
			fcport->port_name[1],
			fcport->port_name[2], fcport->port_name[3],
			fcport->port_name[4], fcport->port_name[5],
			fcport->port_name[6], fcport->port_name[7], rval,
			fcport->fp_speed, mb[0], mb[1]));
		rval = 0;
		bsg_job->reply->result = (DID_ERROR << 16);

	} else {
		if (!port_param->mode) {
			bsg_job->reply_len = sizeof(struct fc_bsg_reply) +
				sizeof(struct qla_port_param);

			rsp_ptr = ((uint8_t *)bsg_job->reply) +
				sizeof(struct fc_bsg_reply);

			memcpy(rsp_ptr, port_param,
				sizeof(struct qla_port_param));
		}

		bsg_job->reply->result = DID_OK;
	}

	bsg_job->job_done(bsg_job);
	return rval;
}

static int
qla2x00_optrom_setup(struct fc_bsg_job *bsg_job, struct qla_hw_data *ha,
	uint8_t is_update)
{
	uint32_t start = 0;
	int valid = 0;

	bsg_job->reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return -EINVAL;

	start = bsg_job->request->rqst_data.h_vendor.vendor_cmd[1];
	if (start > ha->optrom_size)
		return -EINVAL;

	if (ha->optrom_state != QLA_SWAITING)
		return -EBUSY;

	ha->optrom_region_start = start;

	if (is_update) {
		if (ha->optrom_size == OPTROM_SIZE_2300 && start == 0)
			valid = 1;
		else if (start == (ha->flt_region_boot * 4) ||
		    start == (ha->flt_region_fw * 4))
			valid = 1;
		else if (IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha) ||
		    IS_QLA8XXX_TYPE(ha))
			valid = 1;
		if (!valid) {
			qla_printk(KERN_WARNING, ha,
			    "Invalid start region 0x%x/0x%x.\n",
			    start, bsg_job->request_payload.payload_len);
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

	ha->optrom_buffer = vmalloc(ha->optrom_region_size);
	if (!ha->optrom_buffer) {
		qla_printk(KERN_WARNING, ha,
		    "Read: Unable to allocate memory for optrom retrieval "
		    "(%x).\n", ha->optrom_region_size);

		ha->optrom_state = QLA_SWAITING;
		return -ENOMEM;
	}

	memset(ha->optrom_buffer, 0, ha->optrom_region_size);
	return 0;
}

static int
qla2x00_read_optrom(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;

	rval = qla2x00_optrom_setup(bsg_job, ha, 0);
	if (rval)
		return rval;

	ha->isp_ops->read_optrom(vha, ha->optrom_buffer,
	    ha->optrom_region_start, ha->optrom_region_size);

	sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, ha->optrom_buffer,
	    ha->optrom_region_size);

	bsg_job->reply->reply_payload_rcv_len = ha->optrom_region_size;
	bsg_job->reply->result = DID_OK;
	vfree(ha->optrom_buffer);
	ha->optrom_buffer = NULL;
	ha->optrom_state = QLA_SWAITING;
	bsg_job->job_done(bsg_job);
	return rval;
}

static int
qla2x00_update_optrom(struct fc_bsg_job *bsg_job)
{
	struct Scsi_Host *host = bsg_job->shost;
	scsi_qla_host_t *vha = shost_priv(host);
	struct qla_hw_data *ha = vha->hw;
	int rval = 0;

	rval = qla2x00_optrom_setup(bsg_job, ha, 1);
	if (rval)
		return rval;

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, ha->optrom_buffer,
	    ha->optrom_region_size);

	ha->isp_ops->write_optrom(vha, ha->optrom_buffer,
	    ha->optrom_region_start, ha->optrom_region_size);

	bsg_job->reply->result = DID_OK;
	vfree(ha->optrom_buffer);
	ha->optrom_buffer = NULL;
	ha->optrom_state = QLA_SWAITING;
	bsg_job->job_done(bsg_job);
	return rval;
}

static int
qla2x00_process_vendor_specific(struct fc_bsg_job *bsg_job)
{
	switch (bsg_job->request->rqst_data.h_vendor.vendor_cmd[0]) {
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

	default:
		bsg_job->reply->result = (DID_ERROR << 16);
		bsg_job->job_done(bsg_job);
		return -ENOSYS;
	}
}

int
qla24xx_bsg_request(struct fc_bsg_job *bsg_job)
{
	int ret = -EINVAL;

	switch (bsg_job->request->msgcode) {
	case FC_BSG_RPT_ELS:
	case FC_BSG_HST_ELS_NOLOGIN:
		ret = qla2x00_process_els(bsg_job);
		break;
	case FC_BSG_HST_CT:
		ret = qla2x00_process_ct(bsg_job);
		break;
	case FC_BSG_HST_VENDOR:
		ret = qla2x00_process_vendor_specific(bsg_job);
		break;
	case FC_BSG_HST_ADD_RPORT:
	case FC_BSG_HST_DEL_RPORT:
	case FC_BSG_RPT_CT:
	default:
		DEBUG2(printk("qla2xxx: unsupported BSG request\n"));
		break;
	}
	return ret;
}

int
qla24xx_bsg_timeout(struct fc_bsg_job *bsg_job)
{
	scsi_qla_host_t *vha = shost_priv(bsg_job->shost);
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	int cnt, que;
	unsigned long flags;
	struct req_que *req;
	struct srb_ctx *sp_bsg;

	/* find the bsg job from the active list of commands */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (que = 0; que < ha->max_req_queues; que++) {
		req = ha->req_q_map[que];
		if (!req)
			continue;

		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
			sp = req->outstanding_cmds[cnt];
			if (sp) {
				sp_bsg = sp->ctx;

				if (((sp_bsg->type == SRB_CT_CMD) ||
					(sp_bsg->type == SRB_ELS_CMD_HST))
					&& (sp_bsg->u.bsg_job == bsg_job)) {
					spin_unlock_irqrestore(&ha->hardware_lock, flags);
					if (ha->isp_ops->abort_command(sp)) {
						DEBUG2(qla_printk(KERN_INFO, ha,
						    "scsi(%ld): mbx "
						    "abort_command failed\n",
						    vha->host_no));
						bsg_job->req->errors =
						bsg_job->reply->result = -EIO;
					} else {
						DEBUG2(qla_printk(KERN_INFO, ha,
						    "scsi(%ld): mbx "
						    "abort_command success\n",
						    vha->host_no));
						bsg_job->req->errors =
						bsg_job->reply->result = 0;
					}
					spin_lock_irqsave(&ha->hardware_lock, flags);
					goto done;
				}
			}
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	DEBUG2(qla_printk(KERN_INFO, ha,
		"scsi(%ld) SRB not found to abort\n", vha->host_no));
	bsg_job->req->errors = bsg_job->reply->result = -ENXIO;
	return 0;

done:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (bsg_job->request->msgcode == FC_BSG_HST_CT)
		kfree(sp->fcport);
	kfree(sp->ctx);
	mempool_free(sp, ha->srb_mempool);
	return 0;
}

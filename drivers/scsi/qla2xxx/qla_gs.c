/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

static inline struct ct_sns_req *
qla2x00_prep_ct_req(struct ct_sns_req *, uint16_t, uint16_t);

static inline struct sns_cmd_pkt *
qla2x00_prep_sns_cmd(scsi_qla_host_t *, uint16_t, uint16_t, uint16_t);

static int qla2x00_sns_ga_nxt(scsi_qla_host_t *, fc_port_t *);
static int qla2x00_sns_gid_pt(scsi_qla_host_t *, sw_info_t *);
static int qla2x00_sns_gpn_id(scsi_qla_host_t *, sw_info_t *);
static int qla2x00_sns_gnn_id(scsi_qla_host_t *, sw_info_t *);
static int qla2x00_sns_rft_id(scsi_qla_host_t *);
static int qla2x00_sns_rnn_id(scsi_qla_host_t *);

/**
 * qla2x00_prep_ms_iocb() - Prepare common MS/CT IOCB fields for SNS CT query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
void *
qla2x00_prep_ms_iocb(scsi_qla_host_t *ha, uint32_t req_size, uint32_t rsp_size)
{
	ms_iocb_entry_t *ms_pkt;

	ms_pkt = ha->ms_iocb;
	memset(ms_pkt, 0, sizeof(ms_iocb_entry_t));

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;
	SET_TARGET_ID(ha, ms_pkt->loop_id, SIMPLE_NAME_SERVER);
	ms_pkt->control_flags = __constant_cpu_to_le16(CF_READ | CF_HEAD_TAG);
	ms_pkt->timeout = __constant_cpu_to_le16(25);
	ms_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ms_pkt->total_dsd_count = __constant_cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(rsp_size);
	ms_pkt->req_bytecount = cpu_to_le32(req_size);

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;

	return (ms_pkt);
}

/**
 * qla24xx_prep_ms_iocb() - Prepare common CT IOCB fields for SNS CT query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
void *
qla24xx_prep_ms_iocb(scsi_qla_host_t *ha, uint32_t req_size, uint32_t rsp_size)
{
	struct ct_entry_24xx *ct_pkt;

	ct_pkt = (struct ct_entry_24xx *)ha->ms_iocb;
	memset(ct_pkt, 0, sizeof(struct ct_entry_24xx));

	ct_pkt->entry_type = CT_IOCB_TYPE;
	ct_pkt->entry_count = 1;
	ct_pkt->nport_handle = __constant_cpu_to_le16(NPH_SNS);
	ct_pkt->timeout = __constant_cpu_to_le16(25);
	ct_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_byte_count = cpu_to_le32(rsp_size);
	ct_pkt->cmd_byte_count = cpu_to_le32(req_size);

	ct_pkt->dseg_0_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;

	ct_pkt->dseg_1_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_len = ct_pkt->rsp_byte_count;
	ct_pkt->vp_index = ha->vp_idx;

	return (ct_pkt);
}

/**
 * qla2x00_prep_ct_req() - Prepare common CT request fields for SNS query.
 * @ct_req: CT request buffer
 * @cmd: GS command
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the intitialized @ct_req.
 */
static inline struct ct_sns_req *
qla2x00_prep_ct_req(struct ct_sns_req *ct_req, uint16_t cmd, uint16_t rsp_size)
{
	memset(ct_req, 0, sizeof(struct ct_sns_pkt));

	ct_req->header.revision = 0x01;
	ct_req->header.gs_type = 0xFC;
	ct_req->header.gs_subtype = 0x02;
	ct_req->command = cpu_to_be16(cmd);
	ct_req->max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return (ct_req);
}

static int
qla2x00_chk_ms_status(scsi_qla_host_t *ha, ms_iocb_entry_t *ms_pkt,
    struct ct_sns_rsp *ct_rsp, const char *routine)
{
	int rval;
	uint16_t comp_status;

	rval = QLA_FUNCTION_FAILED;
	if (ms_pkt->entry_status != 0) {
		DEBUG2_3(printk("scsi(%ld): %s failed, error status (%x).\n",
		    ha->host_no, routine, ms_pkt->entry_status));
	} else {
		if (IS_FWI2_CAPABLE(ha))
			comp_status = le16_to_cpu(
			    ((struct ct_entry_24xx *)ms_pkt)->comp_status);
		else
			comp_status = le16_to_cpu(ms_pkt->status);
		switch (comp_status) {
		case CS_COMPLETE:
		case CS_DATA_UNDERRUN:
		case CS_DATA_OVERRUN:		/* Overrun? */
			if (ct_rsp->header.response !=
			    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
				DEBUG2_3(printk("scsi(%ld): %s failed, "
				    "rejected request:\n", ha->host_no,
				    routine));
				DEBUG2_3(qla2x00_dump_buffer(
				    (uint8_t *)&ct_rsp->header,
				    sizeof(struct ct_rsp_hdr)));
				rval = QLA_INVALID_COMMAND;
			} else
				rval = QLA_SUCCESS;
			break;
		default:
			DEBUG2_3(printk("scsi(%ld): %s failed, completion "
			    "status (%x).\n", ha->host_no, routine,
			    comp_status));
			break;
		}
	}
	return rval;
}

/**
 * qla2x00_ga_nxt() - SNS scan for fabric devices via GA_NXT command.
 * @ha: HA context
 * @fcport: fcport entry to updated
 *
 * Returns 0 on success.
 */
int
qla2x00_ga_nxt(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		return (qla2x00_sns_ga_nxt(ha, fcport));
	}

	/* Issue GA_NXT */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(ha, GA_NXT_REQ_SIZE,
	    GA_NXT_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GA_NXT_CMD,
	    GA_NXT_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id */
	ct_req->req.port_id.port_id[0] = fcport->d_id.b.domain;
	ct_req->req.port_id.port_id[1] = fcport->d_id.b.area;
	ct_req->req.port_id.port_id[2] = fcport->d_id.b.al_pa;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GA_NXT issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "GA_NXT") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Populate fc_port_t entry. */
		fcport->d_id.b.domain = ct_rsp->rsp.ga_nxt.port_id[0];
		fcport->d_id.b.area = ct_rsp->rsp.ga_nxt.port_id[1];
		fcport->d_id.b.al_pa = ct_rsp->rsp.ga_nxt.port_id[2];

		memcpy(fcport->node_name, ct_rsp->rsp.ga_nxt.node_name,
		    WWN_SIZE);
		memcpy(fcport->port_name, ct_rsp->rsp.ga_nxt.port_name,
		    WWN_SIZE);

		if (ct_rsp->rsp.ga_nxt.port_type != NS_N_PORT_TYPE &&
		    ct_rsp->rsp.ga_nxt.port_type != NS_NL_PORT_TYPE)
			fcport->d_id.b.domain = 0xf0;

		DEBUG2_3(printk("scsi(%ld): GA_NXT entry - "
		    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "portid=%02x%02x%02x.\n",
		    ha->host_no,
		    fcport->node_name[0], fcport->node_name[1],
		    fcport->node_name[2], fcport->node_name[3],
		    fcport->node_name[4], fcport->node_name[5],
		    fcport->node_name[6], fcport->node_name[7],
		    fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7],
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa));
	}

	return (rval);
}

/**
 * qla2x00_gid_pt() - SNS scan for fabric devices via GID_PT command.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * NOTE: Non-Nx_Ports are not requested.
 *
 * Returns 0 on success.
 */
int
qla2x00_gid_pt(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	struct ct_sns_gid_pt_data *gid_data;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		return (qla2x00_sns_gid_pt(ha, list));
	}

	gid_data = NULL;

	/* Issue GID_PT */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(ha, GID_PT_REQ_SIZE,
	    GID_PT_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GID_PT_CMD,
	    GID_PT_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_type */
	ct_req->req.gid_pt.port_type = NS_NX_PORT_TYPE;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GID_PT issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "GID_PT") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Set port IDs in switch info list. */
		for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
			gid_data = &ct_rsp->rsp.gid_pt.entries[i];
			list[i].d_id.b.domain = gid_data->port_id[0];
			list[i].d_id.b.area = gid_data->port_id[1];
			list[i].d_id.b.al_pa = gid_data->port_id[2];

			/* Last one exit. */
			if (gid_data->control_byte & BIT_7) {
				list[i].d_id.b.rsvd_1 = gid_data->control_byte;
				break;
			}
		}

		/*
		 * If we've used all available slots, then the switch is
		 * reporting back more devices than we can handle with this
		 * single call.  Return a failed status, and let GA_NXT handle
		 * the overload.
		 */
		if (i == MAX_FIBRE_DEVICES)
			rval = QLA_FUNCTION_FAILED;
	}

	return (rval);
}

/**
 * qla2x00_gpn_id() - SNS Get Port Name (GPN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gpn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		return (qla2x00_sns_gpn_id(ha, list));
	}

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GPN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(ha, GPN_ID_REQ_SIZE,
		    GPN_ID_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GPN_ID_CMD,
		    GPN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GPN_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp,
		    "GPN_ID") != QLA_SUCCESS) {
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save portname */
			memcpy(list[i].port_name,
			    ct_rsp->rsp.gpn_id.port_name, WWN_SIZE);
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_gnn_id() - SNS Get Node Name (GNN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gnn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		return (qla2x00_sns_gnn_id(ha, list));
	}

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GNN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(ha, GNN_ID_REQ_SIZE,
		    GNN_ID_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GNN_ID_CMD,
		    GNN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GNN_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp,
		    "GNN_ID") != QLA_SUCCESS) {
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save nodename */
			memcpy(list[i].node_name,
			    ct_rsp->rsp.gnn_id.node_name, WWN_SIZE);

			DEBUG2_3(printk("scsi(%ld): GID_PT entry - "
			    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
			    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
			    "portid=%02x%02x%02x.\n",
			    ha->host_no,
			    list[i].node_name[0], list[i].node_name[1],
			    list[i].node_name[2], list[i].node_name[3],
			    list[i].node_name[4], list[i].node_name[5],
			    list[i].node_name[6], list[i].node_name[7],
			    list[i].port_name[0], list[i].port_name[1],
			    list[i].port_name[2], list[i].port_name[3],
			    list[i].port_name[4], list[i].port_name[5],
			    list[i].port_name[6], list[i].port_name[7],
			    list[i].d_id.b.domain, list[i].d_id.b.area,
			    list[i].d_id.b.al_pa));
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_rft_id() - SNS Register FC-4 TYPEs (RFT_ID) supported by the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rft_id(scsi_qla_host_t *ha)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		return (qla2x00_sns_rft_id(ha));
	}

	/* Issue RFT_ID */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(ha, RFT_ID_REQ_SIZE,
	    RFT_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RFT_ID_CMD,
	    RFT_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, FC-4 types */
	ct_req->req.rft_id.port_id[0] = ha->d_id.b.domain;
	ct_req->req.rft_id.port_id[1] = ha->d_id.b.area;
	ct_req->req.rft_id.port_id[2] = ha->d_id.b.al_pa;

	ct_req->req.rft_id.fc4_types[2] = 0x01;		/* FCP-3 */

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RFT_ID issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RFT_ID") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RFT_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_rff_id() - SNS Register FC-4 Features (RFF_ID) supported by the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rff_id(scsi_qla_host_t *ha)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		DEBUG2(printk("scsi(%ld): RFF_ID call unsupported on "
		    "ISP2100/ISP2200.\n", ha->host_no));
		return (QLA_SUCCESS);
	}

	/* Issue RFF_ID */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(ha, RFF_ID_REQ_SIZE,
	    RFF_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RFF_ID_CMD,
	    RFF_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, FC-4 feature, FC-4 type */
	ct_req->req.rff_id.port_id[0] = ha->d_id.b.domain;
	ct_req->req.rff_id.port_id[1] = ha->d_id.b.area;
	ct_req->req.rff_id.port_id[2] = ha->d_id.b.al_pa;

	ct_req->req.rff_id.fc4_feature = BIT_1;
	ct_req->req.rff_id.fc4_type = 0x08;		/* SCSI - FCP */

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RFF_ID issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RFF_ID") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RFF_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_rnn_id() - SNS Register Node Name (RNN_ID) of the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rnn_id(scsi_qla_host_t *ha)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		return (qla2x00_sns_rnn_id(ha));
	}

	/* Issue RNN_ID */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(ha, RNN_ID_REQ_SIZE,
	    RNN_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RNN_ID_CMD,
	    RNN_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, node_name */
	ct_req->req.rnn_id.port_id[0] = ha->d_id.b.domain;
	ct_req->req.rnn_id.port_id[1] = ha->d_id.b.area;
	ct_req->req.rnn_id.port_id[2] = ha->d_id.b.al_pa;

	memcpy(ct_req->req.rnn_id.node_name, ha->node_name, WWN_SIZE);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RNN_ID issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RNN_ID") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RNN_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

void
qla2x00_get_sym_node_name(scsi_qla_host_t *ha, uint8_t *snn)
{
	sprintf(snn, "%s FW:v%d.%02d.%02d DVR:v%s",ha->model_number,
	    ha->fw_major_version, ha->fw_minor_version,
	    ha->fw_subminor_version, qla2x00_version_str);
}

/**
 * qla2x00_rsnn_nn() - SNS Register Symbolic Node Name (RSNN_NN) of the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rsnn_nn(scsi_qla_host_t *ha)
{
	int		rval;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		DEBUG2(printk("scsi(%ld): RSNN_ID call unsupported on "
		    "ISP2100/ISP2200.\n", ha->host_no));
		return (QLA_SUCCESS);
	}

	/* Issue RSNN_NN */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_iocb(ha, 0, RSNN_NN_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RSNN_NN_CMD,
	    RSNN_NN_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- node_name, symbolic node_name, size */
	memcpy(ct_req->req.rsnn_nn.node_name, ha->node_name, WWN_SIZE);

	/* Prepare the Symbolic Node Name */
	qla2x00_get_sym_node_name(ha, ct_req->req.rsnn_nn.sym_node_name);

	/* Calculate SNN length */
	ct_req->req.rsnn_nn.name_len =
	    (uint8_t)strlen(ct_req->req.rsnn_nn.sym_node_name);

	/* Update MS IOCB request */
	ms_pkt->req_bytecount =
	    cpu_to_le32(24 + 1 + ct_req->req.rsnn_nn.name_len);
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RSNN_NN issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RSNN_NN") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RSNN_NN exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_prep_sns_cmd() - Prepare common SNS command request fields for query.
 * @ha: HA context
 * @cmd: GS command
 * @scmd_len: Subcommand length
 * @data_size: response size in bytes
 *
 * Returns a pointer to the @ha's sns_cmd.
 */
static inline struct sns_cmd_pkt *
qla2x00_prep_sns_cmd(scsi_qla_host_t *ha, uint16_t cmd, uint16_t scmd_len,
    uint16_t data_size)
{
	uint16_t		wc;
	struct sns_cmd_pkt	*sns_cmd;

	sns_cmd = ha->sns_cmd;
	memset(sns_cmd, 0, sizeof(struct sns_cmd_pkt));
	wc = data_size / 2;			/* Size in 16bit words. */
	sns_cmd->p.cmd.buffer_length = cpu_to_le16(wc);
	sns_cmd->p.cmd.buffer_address[0] = cpu_to_le32(LSD(ha->sns_cmd_dma));
	sns_cmd->p.cmd.buffer_address[1] = cpu_to_le32(MSD(ha->sns_cmd_dma));
	sns_cmd->p.cmd.subcommand_length = cpu_to_le16(scmd_len);
	sns_cmd->p.cmd.subcommand = cpu_to_le16(cmd);
	wc = (data_size - 16) / 4;		/* Size in 32bit words. */
	sns_cmd->p.cmd.size = cpu_to_le16(wc);

	return (sns_cmd);
}

/**
 * qla2x00_sns_ga_nxt() - SNS scan for fabric devices via GA_NXT command.
 * @ha: HA context
 * @fcport: fcport entry to updated
 *
 * This command uses the old Exectute SNS Command mailbox routine.
 *
 * Returns 0 on success.
 */
static int
qla2x00_sns_ga_nxt(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	int		rval;

	struct sns_cmd_pkt	*sns_cmd;

	/* Issue GA_NXT. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(ha, GA_NXT_CMD, GA_NXT_SNS_SCMD_LEN,
	    GA_NXT_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_id. */
	sns_cmd->p.cmd.param[0] = fcport->d_id.b.al_pa;
	sns_cmd->p.cmd.param[1] = fcport->d_id.b.area;
	sns_cmd->p.cmd.param[2] = fcport->d_id.b.domain;

	/* Execute SNS command. */
	rval = qla2x00_send_sns(ha, ha->sns_cmd_dma, GA_NXT_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GA_NXT Send SNS failed (%d).\n",
		    ha->host_no, rval));
	} else if (sns_cmd->p.gan_data[8] != 0x80 ||
	    sns_cmd->p.gan_data[9] != 0x02) {
		DEBUG2_3(printk("scsi(%ld): GA_NXT failed, rejected request, "
		    "ga_nxt_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer(sns_cmd->p.gan_data, 16));
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Populate fc_port_t entry. */
		fcport->d_id.b.domain = sns_cmd->p.gan_data[17];
		fcport->d_id.b.area = sns_cmd->p.gan_data[18];
		fcport->d_id.b.al_pa = sns_cmd->p.gan_data[19];

		memcpy(fcport->node_name, &sns_cmd->p.gan_data[284], WWN_SIZE);
		memcpy(fcport->port_name, &sns_cmd->p.gan_data[20], WWN_SIZE);

		if (sns_cmd->p.gan_data[16] != NS_N_PORT_TYPE &&
		    sns_cmd->p.gan_data[16] != NS_NL_PORT_TYPE)
			fcport->d_id.b.domain = 0xf0;

		DEBUG2_3(printk("scsi(%ld): GA_NXT entry - "
		    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "portid=%02x%02x%02x.\n",
		    ha->host_no,
		    fcport->node_name[0], fcport->node_name[1],
		    fcport->node_name[2], fcport->node_name[3],
		    fcport->node_name[4], fcport->node_name[5],
		    fcport->node_name[6], fcport->node_name[7],
		    fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7],
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa));
	}

	return (rval);
}

/**
 * qla2x00_sns_gid_pt() - SNS scan for fabric devices via GID_PT command.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * This command uses the old Exectute SNS Command mailbox routine.
 *
 * NOTE: Non-Nx_Ports are not requested.
 *
 * Returns 0 on success.
 */
static int
qla2x00_sns_gid_pt(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;

	uint16_t	i;
	uint8_t		*entry;
	struct sns_cmd_pkt	*sns_cmd;

	/* Issue GID_PT. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(ha, GID_PT_CMD, GID_PT_SNS_SCMD_LEN,
	    GID_PT_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_type. */
	sns_cmd->p.cmd.param[0] = NS_NX_PORT_TYPE;

	/* Execute SNS command. */
	rval = qla2x00_send_sns(ha, ha->sns_cmd_dma, GID_PT_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GID_PT Send SNS failed (%d).\n",
		    ha->host_no, rval));
	} else if (sns_cmd->p.gid_data[8] != 0x80 ||
	    sns_cmd->p.gid_data[9] != 0x02) {
		DEBUG2_3(printk("scsi(%ld): GID_PT failed, rejected request, "
		    "gid_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer(sns_cmd->p.gid_data, 16));
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Set port IDs in switch info list. */
		for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
			entry = &sns_cmd->p.gid_data[(i * 4) + 16];
			list[i].d_id.b.domain = entry[1];
			list[i].d_id.b.area = entry[2];
			list[i].d_id.b.al_pa = entry[3];

			/* Last one exit. */
			if (entry[0] & BIT_7) {
				list[i].d_id.b.rsvd_1 = entry[0];
				break;
			}
		}

		/*
		 * If we've used all available slots, then the switch is
		 * reporting back more devices that we can handle with this
		 * single call.  Return a failed status, and let GA_NXT handle
		 * the overload.
		 */
		if (i == MAX_FIBRE_DEVICES)
			rval = QLA_FUNCTION_FAILED;
	}

	return (rval);
}

/**
 * qla2x00_sns_gpn_id() - SNS Get Port Name (GPN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * This command uses the old Exectute SNS Command mailbox routine.
 *
 * Returns 0 on success.
 */
static int
qla2x00_sns_gpn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;

	uint16_t	i;
	struct sns_cmd_pkt	*sns_cmd;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GPN_ID */
		/* Prepare SNS command request. */
		sns_cmd = qla2x00_prep_sns_cmd(ha, GPN_ID_CMD,
		    GPN_ID_SNS_SCMD_LEN, GPN_ID_SNS_DATA_SIZE);

		/* Prepare SNS command arguments -- port_id. */
		sns_cmd->p.cmd.param[0] = list[i].d_id.b.al_pa;
		sns_cmd->p.cmd.param[1] = list[i].d_id.b.area;
		sns_cmd->p.cmd.param[2] = list[i].d_id.b.domain;

		/* Execute SNS command. */
		rval = qla2x00_send_sns(ha, ha->sns_cmd_dma,
		    GPN_ID_SNS_CMD_SIZE / 2, sizeof(struct sns_cmd_pkt));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GPN_ID Send SNS failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (sns_cmd->p.gpn_data[8] != 0x80 ||
		    sns_cmd->p.gpn_data[9] != 0x02) {
			DEBUG2_3(printk("scsi(%ld): GPN_ID failed, rejected "
			    "request, gpn_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer(sns_cmd->p.gpn_data, 16));
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save portname */
			memcpy(list[i].port_name, &sns_cmd->p.gpn_data[16],
			    WWN_SIZE);
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_sns_gnn_id() - SNS Get Node Name (GNN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * This command uses the old Exectute SNS Command mailbox routine.
 *
 * Returns 0 on success.
 */
static int
qla2x00_sns_gnn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;

	uint16_t	i;
	struct sns_cmd_pkt	*sns_cmd;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GNN_ID */
		/* Prepare SNS command request. */
		sns_cmd = qla2x00_prep_sns_cmd(ha, GNN_ID_CMD,
		    GNN_ID_SNS_SCMD_LEN, GNN_ID_SNS_DATA_SIZE);

		/* Prepare SNS command arguments -- port_id. */
		sns_cmd->p.cmd.param[0] = list[i].d_id.b.al_pa;
		sns_cmd->p.cmd.param[1] = list[i].d_id.b.area;
		sns_cmd->p.cmd.param[2] = list[i].d_id.b.domain;

		/* Execute SNS command. */
		rval = qla2x00_send_sns(ha, ha->sns_cmd_dma,
		    GNN_ID_SNS_CMD_SIZE / 2, sizeof(struct sns_cmd_pkt));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GNN_ID Send SNS failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (sns_cmd->p.gnn_data[8] != 0x80 ||
		    sns_cmd->p.gnn_data[9] != 0x02) {
			DEBUG2_3(printk("scsi(%ld): GNN_ID failed, rejected "
			    "request, gnn_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer(sns_cmd->p.gnn_data, 16));
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save nodename */
			memcpy(list[i].node_name, &sns_cmd->p.gnn_data[16],
			    WWN_SIZE);

			DEBUG2_3(printk("scsi(%ld): GID_PT entry - "
			    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
			    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
			    "portid=%02x%02x%02x.\n",
			    ha->host_no,
			    list[i].node_name[0], list[i].node_name[1],
			    list[i].node_name[2], list[i].node_name[3],
			    list[i].node_name[4], list[i].node_name[5],
			    list[i].node_name[6], list[i].node_name[7],
			    list[i].port_name[0], list[i].port_name[1],
			    list[i].port_name[2], list[i].port_name[3],
			    list[i].port_name[4], list[i].port_name[5],
			    list[i].port_name[6], list[i].port_name[7],
			    list[i].d_id.b.domain, list[i].d_id.b.area,
			    list[i].d_id.b.al_pa));
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_snd_rft_id() - SNS Register FC-4 TYPEs (RFT_ID) supported by the HBA.
 * @ha: HA context
 *
 * This command uses the old Exectute SNS Command mailbox routine.
 *
 * Returns 0 on success.
 */
static int
qla2x00_sns_rft_id(scsi_qla_host_t *ha)
{
	int		rval;

	struct sns_cmd_pkt	*sns_cmd;

	/* Issue RFT_ID. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(ha, RFT_ID_CMD, RFT_ID_SNS_SCMD_LEN,
	    RFT_ID_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_id, FC-4 types */
	sns_cmd->p.cmd.param[0] = ha->d_id.b.al_pa;
	sns_cmd->p.cmd.param[1] = ha->d_id.b.area;
	sns_cmd->p.cmd.param[2] = ha->d_id.b.domain;

	sns_cmd->p.cmd.param[5] = 0x01;			/* FCP-3 */

	/* Execute SNS command. */
	rval = qla2x00_send_sns(ha, ha->sns_cmd_dma, RFT_ID_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RFT_ID Send SNS failed (%d).\n",
		    ha->host_no, rval));
	} else if (sns_cmd->p.rft_data[8] != 0x80 ||
	    sns_cmd->p.rft_data[9] != 0x02) {
		DEBUG2_3(printk("scsi(%ld): RFT_ID failed, rejected request, "
		    "rft_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer(sns_cmd->p.rft_data, 16));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RFT_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_sns_rnn_id() - SNS Register Node Name (RNN_ID) of the HBA.
 * HBA.
 * @ha: HA context
 *
 * This command uses the old Exectute SNS Command mailbox routine.
 *
 * Returns 0 on success.
 */
static int
qla2x00_sns_rnn_id(scsi_qla_host_t *ha)
{
	int		rval;

	struct sns_cmd_pkt	*sns_cmd;

	/* Issue RNN_ID. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(ha, RNN_ID_CMD, RNN_ID_SNS_SCMD_LEN,
	    RNN_ID_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_id, nodename. */
	sns_cmd->p.cmd.param[0] = ha->d_id.b.al_pa;
	sns_cmd->p.cmd.param[1] = ha->d_id.b.area;
	sns_cmd->p.cmd.param[2] = ha->d_id.b.domain;

	sns_cmd->p.cmd.param[4] = ha->node_name[7];
	sns_cmd->p.cmd.param[5] = ha->node_name[6];
	sns_cmd->p.cmd.param[6] = ha->node_name[5];
	sns_cmd->p.cmd.param[7] = ha->node_name[4];
	sns_cmd->p.cmd.param[8] = ha->node_name[3];
	sns_cmd->p.cmd.param[9] = ha->node_name[2];
	sns_cmd->p.cmd.param[10] = ha->node_name[1];
	sns_cmd->p.cmd.param[11] = ha->node_name[0];

	/* Execute SNS command. */
	rval = qla2x00_send_sns(ha, ha->sns_cmd_dma, RNN_ID_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RNN_ID Send SNS failed (%d).\n",
		    ha->host_no, rval));
	} else if (sns_cmd->p.rnn_data[8] != 0x80 ||
	    sns_cmd->p.rnn_data[9] != 0x02) {
		DEBUG2_3(printk("scsi(%ld): RNN_ID failed, rejected request, "
		    "rnn_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer(sns_cmd->p.rnn_data, 16));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RNN_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_mgmt_svr_login() - Login to fabric Managment Service.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_mgmt_svr_login(scsi_qla_host_t *ha)
{
	int ret;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ret = QLA_SUCCESS;
	if (ha->flags.management_server_logged_in)
		return ret;

	ha->isp_ops->fabric_login(ha, ha->mgmt_svr_loop_id, 0xff, 0xff, 0xfa,
	    mb, BIT_1);
	if (mb[0] != MBS_COMMAND_COMPLETE) {
		DEBUG2_13(printk("%s(%ld): Failed MANAGEMENT_SERVER login: "
		    "loop_id=%x mb[0]=%x mb[1]=%x mb[2]=%x mb[6]=%x mb[7]=%x\n",
		    __func__, ha->host_no, ha->mgmt_svr_loop_id, mb[0], mb[1],
		    mb[2], mb[6], mb[7]));
		ret = QLA_FUNCTION_FAILED;
	} else
		ha->flags.management_server_logged_in = 1;

	return ret;
}

/**
 * qla2x00_prep_ms_fdmi_iocb() - Prepare common MS IOCB fields for FDMI query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
void *
qla2x00_prep_ms_fdmi_iocb(scsi_qla_host_t *ha, uint32_t req_size,
    uint32_t rsp_size)
{
	ms_iocb_entry_t *ms_pkt;

	ms_pkt = ha->ms_iocb;
	memset(ms_pkt, 0, sizeof(ms_iocb_entry_t));

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;
	SET_TARGET_ID(ha, ms_pkt->loop_id, ha->mgmt_svr_loop_id);
	ms_pkt->control_flags = __constant_cpu_to_le16(CF_READ | CF_HEAD_TAG);
	ms_pkt->timeout = __constant_cpu_to_le16(59);
	ms_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ms_pkt->total_dsd_count = __constant_cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(rsp_size);
	ms_pkt->req_bytecount = cpu_to_le32(req_size);

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;

	return ms_pkt;
}

/**
 * qla24xx_prep_ms_fdmi_iocb() - Prepare common MS IOCB fields for FDMI query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
void *
qla24xx_prep_ms_fdmi_iocb(scsi_qla_host_t *ha, uint32_t req_size,
    uint32_t rsp_size)
{
	struct ct_entry_24xx *ct_pkt;

	ct_pkt = (struct ct_entry_24xx *)ha->ms_iocb;
	memset(ct_pkt, 0, sizeof(struct ct_entry_24xx));

	ct_pkt->entry_type = CT_IOCB_TYPE;
	ct_pkt->entry_count = 1;
	ct_pkt->nport_handle = cpu_to_le16(ha->mgmt_svr_loop_id);
	ct_pkt->timeout = __constant_cpu_to_le16(59);
	ct_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_byte_count = cpu_to_le32(rsp_size);
	ct_pkt->cmd_byte_count = cpu_to_le32(req_size);

	ct_pkt->dseg_0_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;

	ct_pkt->dseg_1_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_len = ct_pkt->rsp_byte_count;
	ct_pkt->vp_index = ha->vp_idx;

	return ct_pkt;
}

static inline ms_iocb_entry_t *
qla2x00_update_ms_fdmi_iocb(scsi_qla_host_t *ha, uint32_t req_size)
{
	ms_iocb_entry_t *ms_pkt = ha->ms_iocb;
	struct ct_entry_24xx *ct_pkt = (struct ct_entry_24xx *)ha->ms_iocb;

	if (IS_FWI2_CAPABLE(ha)) {
		ct_pkt->cmd_byte_count = cpu_to_le32(req_size);
		ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;
	} else {
		ms_pkt->req_bytecount = cpu_to_le32(req_size);
		ms_pkt->dseg_req_length = ms_pkt->req_bytecount;
	}

	return ms_pkt;
}

/**
 * qla2x00_prep_ct_req() - Prepare common CT request fields for SNS query.
 * @ct_req: CT request buffer
 * @cmd: GS command
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the intitialized @ct_req.
 */
static inline struct ct_sns_req *
qla2x00_prep_ct_fdmi_req(struct ct_sns_req *ct_req, uint16_t cmd,
    uint16_t rsp_size)
{
	memset(ct_req, 0, sizeof(struct ct_sns_pkt));

	ct_req->header.revision = 0x01;
	ct_req->header.gs_type = 0xFA;
	ct_req->header.gs_subtype = 0x10;
	ct_req->command = cpu_to_be16(cmd);
	ct_req->max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return ct_req;
}

/**
 * qla2x00_fdmi_rhba() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fdmi_rhba(scsi_qla_host_t *ha)
{
	int rval, alen;
	uint32_t size, sn;

	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;
	uint8_t *entries;
	struct ct_fdmi_hba_attr *eiter;

	/* Issue RHBA */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(ha, 0, RHBA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(&ha->ct_sns->p.req, RHBA_CMD,
	    RHBA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- attribute block, attributes. */
	memcpy(ct_req->req.rhba.hba_identifier, ha->port_name, WWN_SIZE);
	ct_req->req.rhba.entry_count = __constant_cpu_to_be32(1);
	memcpy(ct_req->req.rhba.port_name, ha->port_name, WWN_SIZE);
	size = 2 * WWN_SIZE + 4 + 4;

	/* Attributes */
	ct_req->req.rhba.attrs.count =
	    __constant_cpu_to_be32(FDMI_HBA_ATTR_COUNT);
	entries = ct_req->req.rhba.hba_identifier;

	/* Nodename. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_NODE_NAME);
	eiter->len = __constant_cpu_to_be16(4 + WWN_SIZE);
	memcpy(eiter->a.node_name, ha->node_name, WWN_SIZE);
	size += 4 + WWN_SIZE;

	DEBUG13(printk("%s(%ld): NODENAME=%02x%02x%02x%02x%02x%02x%02x%02x.\n",
	    __func__, ha->host_no,
	    eiter->a.node_name[0], eiter->a.node_name[1], eiter->a.node_name[2],
	    eiter->a.node_name[3], eiter->a.node_name[4], eiter->a.node_name[5],
	    eiter->a.node_name[6], eiter->a.node_name[7]));

	/* Manufacturer. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_MANUFACTURER);
	strcpy(eiter->a.manufacturer, "QLogic Corporation");
	alen = strlen(eiter->a.manufacturer);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): MANUFACTURER=%s.\n", __func__, ha->host_no,
	    eiter->a.manufacturer));

	/* Serial number. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_SERIAL_NUMBER);
	sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | ha->serial1;
	sprintf(eiter->a.serial_num, "%c%05d", 'A' + sn / 100000, sn % 100000);
	alen = strlen(eiter->a.serial_num);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): SERIALNO=%s.\n", __func__, ha->host_no,
	    eiter->a.serial_num));

	/* Model name. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_MODEL);
	strcpy(eiter->a.model, ha->model_number);
	alen = strlen(eiter->a.model);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): MODEL_NAME=%s.\n", __func__, ha->host_no,
	    eiter->a.model));

	/* Model description. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_MODEL_DESCRIPTION);
	if (ha->model_desc)
		strncpy(eiter->a.model_desc, ha->model_desc, 80);
	alen = strlen(eiter->a.model_desc);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): MODEL_DESC=%s.\n", __func__, ha->host_no,
	    eiter->a.model_desc));

	/* Hardware version. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_HARDWARE_VERSION);
	strcpy(eiter->a.hw_version, ha->adapter_id);
	alen = strlen(eiter->a.hw_version);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): HARDWAREVER=%s.\n", __func__, ha->host_no,
	    eiter->a.hw_version));

	/* Driver version. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_DRIVER_VERSION);
	strcpy(eiter->a.driver_version, qla2x00_version_str);
	alen = strlen(eiter->a.driver_version);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): DRIVERVER=%s.\n", __func__, ha->host_no,
	    eiter->a.driver_version));

	/* Option ROM version. */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_OPTION_ROM_VERSION);
	strcpy(eiter->a.orom_version, "0.00");
	alen = strlen(eiter->a.orom_version);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): OPTROMVER=%s.\n", __func__, ha->host_no,
	    eiter->a.orom_version));

	/* Firmware version */
	eiter = (struct ct_fdmi_hba_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_HBA_FIRMWARE_VERSION);
	ha->isp_ops->fw_version_str(ha, eiter->a.fw_version);
	alen = strlen(eiter->a.fw_version);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): FIRMWAREVER=%s.\n", __func__, ha->host_no,
	    eiter->a.fw_version));

	/* Update MS request size. */
	qla2x00_update_ms_fdmi_iocb(ha, size + 16);

	DEBUG13(printk("%s(%ld): RHBA identifier="
	    "%02x%02x%02x%02x%02x%02x%02x%02x size=%d.\n", __func__,
	    ha->host_no, ct_req->req.rhba.hba_identifier[0],
	    ct_req->req.rhba.hba_identifier[1],
	    ct_req->req.rhba.hba_identifier[2],
	    ct_req->req.rhba.hba_identifier[3],
	    ct_req->req.rhba.hba_identifier[4],
	    ct_req->req.rhba.hba_identifier[5],
	    ct_req->req.rhba.hba_identifier[6],
	    ct_req->req.rhba.hba_identifier[7], size));
	DEBUG13(qla2x00_dump_buffer(entries, size));

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RHBA issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RHBA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
		if (ct_rsp->header.reason_code == CT_REASON_CANNOT_PERFORM &&
		    ct_rsp->header.explanation_code ==
		    CT_EXPL_ALREADY_REGISTERED) {
			DEBUG2_13(printk("%s(%ld): HBA already registered.\n",
			    __func__, ha->host_no));
			rval = QLA_ALREADY_REGISTERED;
		}
	} else {
		DEBUG2(printk("scsi(%ld): RHBA exiting normally.\n",
		    ha->host_no));
	}

	return rval;
}

/**
 * qla2x00_fdmi_dhba() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fdmi_dhba(scsi_qla_host_t *ha)
{
	int rval;

	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;

	/* Issue RPA */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(ha, DHBA_REQ_SIZE,
	    DHBA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(&ha->ct_sns->p.req, DHBA_CMD,
	    DHBA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- portname. */
	memcpy(ct_req->req.dhba.port_name, ha->port_name, WWN_SIZE);

	DEBUG13(printk("%s(%ld): DHBA portname="
	    "%02x%02x%02x%02x%02x%02x%02x%02x.\n", __func__, ha->host_no,
	    ct_req->req.dhba.port_name[0], ct_req->req.dhba.port_name[1],
	    ct_req->req.dhba.port_name[2], ct_req->req.dhba.port_name[3],
	    ct_req->req.dhba.port_name[4], ct_req->req.dhba.port_name[5],
	    ct_req->req.dhba.port_name[6], ct_req->req.dhba.port_name[7]));

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): DHBA issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "DHBA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): DHBA exiting normally.\n",
		    ha->host_no));
	}

	return rval;
}

/**
 * qla2x00_fdmi_rpa() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fdmi_rpa(scsi_qla_host_t *ha)
{
	int rval, alen;
	uint32_t size, max_frame_size;

	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;
	uint8_t *entries;
	struct ct_fdmi_port_attr *eiter;
	struct init_cb_24xx *icb24 = (struct init_cb_24xx *)ha->init_cb;

	/* Issue RPA */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(ha, 0, RPA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(&ha->ct_sns->p.req, RPA_CMD,
	    RPA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- attribute block, attributes. */
	memcpy(ct_req->req.rpa.port_name, ha->port_name, WWN_SIZE);
	size = WWN_SIZE + 4;

	/* Attributes */
	ct_req->req.rpa.attrs.count =
	    __constant_cpu_to_be32(FDMI_PORT_ATTR_COUNT);
	entries = ct_req->req.rpa.port_name;

	/* FC4 types. */
	eiter = (struct ct_fdmi_port_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_PORT_FC4_TYPES);
	eiter->len = __constant_cpu_to_be16(4 + 32);
	eiter->a.fc4_types[2] = 0x01;
	size += 4 + 32;

	DEBUG13(printk("%s(%ld): FC4_TYPES=%02x %02x.\n", __func__, ha->host_no,
	    eiter->a.fc4_types[2], eiter->a.fc4_types[1]));

	/* Supported speed. */
	eiter = (struct ct_fdmi_port_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_PORT_SUPPORT_SPEED);
	eiter->len = __constant_cpu_to_be16(4 + 4);
	if (IS_QLA25XX(ha))
		eiter->a.sup_speed = __constant_cpu_to_be32(
		    FDMI_PORT_SPEED_1GB|FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_4GB|FDMI_PORT_SPEED_8GB);
	else if (IS_QLA24XX(ha) || IS_QLA54XX(ha))
		eiter->a.sup_speed = __constant_cpu_to_be32(
		    FDMI_PORT_SPEED_1GB|FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_4GB);
	else if (IS_QLA23XX(ha))
		eiter->a.sup_speed =__constant_cpu_to_be32(
		    FDMI_PORT_SPEED_1GB|FDMI_PORT_SPEED_2GB);
	else
		eiter->a.sup_speed = __constant_cpu_to_be32(
		    FDMI_PORT_SPEED_1GB);
	size += 4 + 4;

	DEBUG13(printk("%s(%ld): SUPPORTED_SPEED=%x.\n", __func__, ha->host_no,
	    eiter->a.sup_speed));

	/* Current speed. */
	eiter = (struct ct_fdmi_port_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_PORT_CURRENT_SPEED);
	eiter->len = __constant_cpu_to_be16(4 + 4);
	switch (ha->link_data_rate) {
	case PORT_SPEED_1GB:
		eiter->a.cur_speed =
		    __constant_cpu_to_be32(FDMI_PORT_SPEED_1GB);
		break;
	case PORT_SPEED_2GB:
		eiter->a.cur_speed =
		    __constant_cpu_to_be32(FDMI_PORT_SPEED_2GB);
		break;
	case PORT_SPEED_4GB:
		eiter->a.cur_speed =
		    __constant_cpu_to_be32(FDMI_PORT_SPEED_4GB);
		break;
	case PORT_SPEED_8GB:
		eiter->a.cur_speed =
		    __constant_cpu_to_be32(FDMI_PORT_SPEED_8GB);
		break;
	default:
		eiter->a.cur_speed =
		    __constant_cpu_to_be32(FDMI_PORT_SPEED_UNKNOWN);
		break;
	}
	size += 4 + 4;

	DEBUG13(printk("%s(%ld): CURRENT_SPEED=%x.\n", __func__, ha->host_no,
	    eiter->a.cur_speed));

	/* Max frame size. */
	eiter = (struct ct_fdmi_port_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_PORT_MAX_FRAME_SIZE);
	eiter->len = __constant_cpu_to_be16(4 + 4);
	max_frame_size = IS_FWI2_CAPABLE(ha) ?
		(uint32_t) icb24->frame_payload_size:
		(uint32_t) ha->init_cb->frame_payload_size;
	eiter->a.max_frame_size = cpu_to_be32(max_frame_size);
	size += 4 + 4;

	DEBUG13(printk("%s(%ld): MAX_FRAME_SIZE=%x.\n", __func__, ha->host_no,
	    eiter->a.max_frame_size));

	/* OS device name. */
	eiter = (struct ct_fdmi_port_attr *) (entries + size);
	eiter->type = __constant_cpu_to_be16(FDMI_PORT_OS_DEVICE_NAME);
	sprintf(eiter->a.os_dev_name, "/proc/scsi/qla2xxx/%ld", ha->host_no);
	alen = strlen(eiter->a.os_dev_name);
	alen += (alen & 3) ? (4 - (alen & 3)) : 4;
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	DEBUG13(printk("%s(%ld): OS_DEVICE_NAME=%s.\n", __func__, ha->host_no,
	    eiter->a.os_dev_name));

	/* Hostname. */
	if (strlen(fc_host_system_hostname(ha->host))) {
		eiter = (struct ct_fdmi_port_attr *) (entries + size);
		eiter->type = __constant_cpu_to_be16(FDMI_PORT_HOST_NAME);
		snprintf(eiter->a.host_name, sizeof(eiter->a.host_name),
		    "%s", fc_host_system_hostname(ha->host));
		alen = strlen(eiter->a.host_name);
		alen += (alen & 3) ? (4 - (alen & 3)) : 4;
		eiter->len = cpu_to_be16(4 + alen);
		size += 4 + alen;

		DEBUG13(printk("%s(%ld): HOSTNAME=%s.\n", __func__,
		    ha->host_no, eiter->a.host_name));
	}

	/* Update MS request size. */
	qla2x00_update_ms_fdmi_iocb(ha, size + 16);

	DEBUG13(printk("%s(%ld): RPA portname="
	    "%02x%02x%02x%02x%02x%02x%02x%02x size=%d.\n", __func__,
	    ha->host_no, ct_req->req.rpa.port_name[0],
	    ct_req->req.rpa.port_name[1], ct_req->req.rpa.port_name[2],
	    ct_req->req.rpa.port_name[3], ct_req->req.rpa.port_name[4],
	    ct_req->req.rpa.port_name[5], ct_req->req.rpa.port_name[6],
	    ct_req->req.rpa.port_name[7], size));
	DEBUG13(qla2x00_dump_buffer(entries, size));

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RPA issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RPA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RPA exiting normally.\n",
		    ha->host_no));
	}

	return rval;
}

/**
 * qla2x00_fdmi_register() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_fdmi_register(scsi_qla_host_t *ha)
{
	int rval;

	rval = qla2x00_mgmt_svr_login(ha);
	if (rval)
		return rval;

	rval = qla2x00_fdmi_rhba(ha);
	if (rval) {
		if (rval != QLA_ALREADY_REGISTERED)
			return rval;

		rval = qla2x00_fdmi_dhba(ha);
		if (rval)
			return rval;

		rval = qla2x00_fdmi_rhba(ha);
		if (rval)
			return rval;
	}
	rval = qla2x00_fdmi_rpa(ha);

	return rval;
}

/**
 * qla2x00_gfpn_id() - SNS Get Fabric Port Name (GFPN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gfpn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (!IS_IIDMA_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GFPN_ID */
		memset(list[i].fabric_port_name, 0, WWN_SIZE);

		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(ha, GFPN_ID_REQ_SIZE,
		    GFPN_ID_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GFPN_ID_CMD,
		    GFPN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GFPN_ID issue IOCB "
			    "failed (%d).\n", ha->host_no, rval));
		} else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp,
		    "GFPN_ID") != QLA_SUCCESS) {
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save fabric portname */
			memcpy(list[i].fabric_port_name,
			    ct_rsp->rsp.gfpn_id.port_name, WWN_SIZE);
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

static inline void *
qla24xx_prep_ms_fm_iocb(scsi_qla_host_t *ha, uint32_t req_size,
    uint32_t rsp_size)
{
	struct ct_entry_24xx *ct_pkt;

	ct_pkt = (struct ct_entry_24xx *)ha->ms_iocb;
	memset(ct_pkt, 0, sizeof(struct ct_entry_24xx));

	ct_pkt->entry_type = CT_IOCB_TYPE;
	ct_pkt->entry_count = 1;
	ct_pkt->nport_handle = cpu_to_le16(ha->mgmt_svr_loop_id);
	ct_pkt->timeout = __constant_cpu_to_le16(59);
	ct_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_byte_count = cpu_to_le32(rsp_size);
	ct_pkt->cmd_byte_count = cpu_to_le32(req_size);

	ct_pkt->dseg_0_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;

	ct_pkt->dseg_1_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_len = ct_pkt->rsp_byte_count;
	ct_pkt->vp_index = ha->vp_idx;

	return ct_pkt;
}


static inline struct ct_sns_req *
qla24xx_prep_ct_fm_req(struct ct_sns_req *ct_req, uint16_t cmd,
    uint16_t rsp_size)
{
	memset(ct_req, 0, sizeof(struct ct_sns_pkt));

	ct_req->header.revision = 0x01;
	ct_req->header.gs_type = 0xFA;
	ct_req->header.gs_subtype = 0x01;
	ct_req->command = cpu_to_be16(cmd);
	ct_req->max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return ct_req;
}

/**
 * qla2x00_gpsc() - FCS Get Port Speed Capabilities (GPSC) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gpsc(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	if (!IS_IIDMA_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;
	if (!ha->flags.gpsc_supported)
		return QLA_FUNCTION_FAILED;

	rval = qla2x00_mgmt_svr_login(ha);
	if (rval)
		return rval;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GFPN_ID */
		list[i].fp_speeds = list[i].fp_speed = 0;

		/* Prepare common MS IOCB */
		ms_pkt = qla24xx_prep_ms_fm_iocb(ha, GPSC_REQ_SIZE,
		    GPSC_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla24xx_prep_ct_fm_req(&ha->ct_sns->p.req,
		    GPSC_CMD, GPSC_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_name */
		memcpy(ct_req->req.gpsc.port_name, list[i].fabric_port_name,
		    WWN_SIZE);

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GPSC issue IOCB "
			    "failed (%d).\n", ha->host_no, rval));
		} else if ((rval = qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp,
		    "GPSC")) != QLA_SUCCESS) {
			/* FM command unsupported? */
			if (rval == QLA_INVALID_COMMAND &&
			    ct_rsp->header.reason_code ==
			    CT_REASON_INVALID_COMMAND_CODE) {
				DEBUG2(printk("scsi(%ld): GPSC command "
				    "unsupported, disabling query...\n",
				    ha->host_no));
				ha->flags.gpsc_supported = 0;
				rval = QLA_FUNCTION_FAILED;
				break;
			}
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save portname */
			list[i].fp_speeds = ct_rsp->rsp.gpsc.speeds;
			list[i].fp_speed = ct_rsp->rsp.gpsc.speed;

			DEBUG2_3(printk("scsi(%ld): GPSC ext entry - "
			    "fpn %02x%02x%02x%02x%02x%02x%02x%02x speeds=%04x "
			    "speed=%04x.\n", ha->host_no,
			    list[i].fabric_port_name[0],
			    list[i].fabric_port_name[1],
			    list[i].fabric_port_name[2],
			    list[i].fabric_port_name[3],
			    list[i].fabric_port_name[4],
			    list[i].fabric_port_name[5],
			    list[i].fabric_port_name[6],
			    list[i].fabric_port_name[7],
			    be16_to_cpu(list[i].fp_speeds),
			    be16_to_cpu(list[i].fp_speed)));
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

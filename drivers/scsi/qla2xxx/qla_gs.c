/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_target.h"
#include <linux/utsname.h>

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
qla2x00_prep_ms_iocb(scsi_qla_host_t *vha, struct ct_arg *arg)
{
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t *ms_pkt;

	ms_pkt = (ms_iocb_entry_t *)arg->iocb;
	memset(ms_pkt, 0, sizeof(ms_iocb_entry_t));

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;
	SET_TARGET_ID(ha, ms_pkt->loop_id, SIMPLE_NAME_SERVER);
	ms_pkt->control_flags = cpu_to_le16(CF_READ | CF_HEAD_TAG);
	ms_pkt->timeout = cpu_to_le16(ha->r_a_tov / 10 * 2);
	ms_pkt->cmd_dsd_count = cpu_to_le16(1);
	ms_pkt->total_dsd_count = cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(arg->rsp_size);
	ms_pkt->req_bytecount = cpu_to_le32(arg->req_size);

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(arg->req_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(arg->req_dma));
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(arg->rsp_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(arg->rsp_dma));
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;

	vha->qla_stats.control_requests++;

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
qla24xx_prep_ms_iocb(scsi_qla_host_t *vha, struct ct_arg *arg)
{
	struct qla_hw_data *ha = vha->hw;
	struct ct_entry_24xx *ct_pkt;

	ct_pkt = (struct ct_entry_24xx *)arg->iocb;
	memset(ct_pkt, 0, sizeof(struct ct_entry_24xx));

	ct_pkt->entry_type = CT_IOCB_TYPE;
	ct_pkt->entry_count = 1;
	ct_pkt->nport_handle = cpu_to_le16(arg->nport_handle);
	ct_pkt->timeout = cpu_to_le16(ha->r_a_tov / 10 * 2);
	ct_pkt->cmd_dsd_count = cpu_to_le16(1);
	ct_pkt->rsp_dsd_count = cpu_to_le16(1);
	ct_pkt->rsp_byte_count = cpu_to_le32(arg->rsp_size);
	ct_pkt->cmd_byte_count = cpu_to_le32(arg->req_size);

	ct_pkt->dseg_0_address[0] = cpu_to_le32(LSD(arg->req_dma));
	ct_pkt->dseg_0_address[1] = cpu_to_le32(MSD(arg->req_dma));
	ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;

	ct_pkt->dseg_1_address[0] = cpu_to_le32(LSD(arg->rsp_dma));
	ct_pkt->dseg_1_address[1] = cpu_to_le32(MSD(arg->rsp_dma));
	ct_pkt->dseg_1_len = ct_pkt->rsp_byte_count;
	ct_pkt->vp_index = vha->vp_idx;

	vha->qla_stats.control_requests++;

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
qla2x00_prep_ct_req(struct ct_sns_pkt *p, uint16_t cmd, uint16_t rsp_size)
{
	memset(p, 0, sizeof(struct ct_sns_pkt));

	p->p.req.header.revision = 0x01;
	p->p.req.header.gs_type = 0xFC;
	p->p.req.header.gs_subtype = 0x02;
	p->p.req.command = cpu_to_be16(cmd);
	p->p.req.max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return &p->p.req;
}

int
qla2x00_chk_ms_status(scsi_qla_host_t *vha, ms_iocb_entry_t *ms_pkt,
    struct ct_sns_rsp *ct_rsp, const char *routine)
{
	int rval;
	uint16_t comp_status;
	struct qla_hw_data *ha = vha->hw;
	bool lid_is_sns = false;

	rval = QLA_FUNCTION_FAILED;
	if (ms_pkt->entry_status != 0) {
		ql_dbg(ql_dbg_disc, vha, 0x2031,
		    "%s failed, error status (%x) on port_id: %02x%02x%02x.\n",
		    routine, ms_pkt->entry_status, vha->d_id.b.domain,
		    vha->d_id.b.area, vha->d_id.b.al_pa);
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
			    cpu_to_be16(CT_ACCEPT_RESPONSE)) {
				ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x2077,
				    "%s failed rejected request on port_id: %02x%02x%02x Completion status 0x%x, response 0x%x\n",
				    routine, vha->d_id.b.domain,
				    vha->d_id.b.area, vha->d_id.b.al_pa,
				    comp_status, ct_rsp->header.response);
				ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha,
				    0x2078, (uint8_t *)&ct_rsp->header,
				    sizeof(struct ct_rsp_hdr));
				rval = QLA_INVALID_COMMAND;
			} else
				rval = QLA_SUCCESS;
			break;
		case CS_PORT_LOGGED_OUT:
			if (IS_FWI2_CAPABLE(ha)) {
				if (le16_to_cpu(ms_pkt->loop_id.extended) ==
				    NPH_SNS)
					lid_is_sns = true;
			} else {
				if (le16_to_cpu(ms_pkt->loop_id.extended) ==
				    SIMPLE_NAME_SERVER)
					lid_is_sns = true;
			}
			if (lid_is_sns) {
				ql_dbg(ql_dbg_async, vha, 0x502b,
					"%s failed, Name server has logged out",
					routine);
				rval = QLA_NOT_LOGGED_IN;
				set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
			}
			break;
		default:
			ql_dbg(ql_dbg_disc, vha, 0x2033,
			    "%s failed, completion status (%x) on port_id: "
			    "%02x%02x%02x.\n", routine, comp_status,
			    vha->d_id.b.domain, vha->d_id.b.area,
			    vha->d_id.b.al_pa);
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
qla2x00_ga_nxt(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct qla_hw_data *ha = vha->hw;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return qla2x00_sns_ga_nxt(vha, fcport);

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = GA_NXT_REQ_SIZE;
	arg.rsp_size = GA_NXT_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	/* Issue GA_NXT */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(ha->ct_sns, GA_NXT_CMD,
	    GA_NXT_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id */
	ct_req->req.port_id.port_id[0] = fcport->d_id.b.domain;
	ct_req->req.port_id.port_id[1] = fcport->d_id.b.area;
	ct_req->req.port_id.port_id[2] = fcport->d_id.b.al_pa;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2062,
		    "GA_NXT issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "GA_NXT") !=
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

		fcport->fc4_type = (ct_rsp->rsp.ga_nxt.fc4_types[2] & BIT_0) ?
		    FC4_TYPE_FCP_SCSI : FC4_TYPE_OTHER;

		if (ct_rsp->rsp.ga_nxt.port_type != NS_N_PORT_TYPE &&
		    ct_rsp->rsp.ga_nxt.port_type != NS_NL_PORT_TYPE)
			fcport->d_id.b.domain = 0xf0;

		ql_dbg(ql_dbg_disc, vha, 0x2063,
		    "GA_NXT entry - nn %8phN pn %8phN "
		    "port_id=%02x%02x%02x.\n",
		    fcport->node_name, fcport->port_name,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa);
	}

	return (rval);
}

static inline int
qla2x00_gid_pt_rsp_size(scsi_qla_host_t *vha)
{
	return vha->hw->max_fibre_devices * 4 + 16;
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
qla2x00_gid_pt(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	struct ct_sns_gid_pt_data *gid_data;
	struct qla_hw_data *ha = vha->hw;
	uint16_t gid_pt_rsp_size;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return qla2x00_sns_gid_pt(vha, list);

	gid_data = NULL;
	gid_pt_rsp_size = qla2x00_gid_pt_rsp_size(vha);

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = GID_PT_REQ_SIZE;
	arg.rsp_size = gid_pt_rsp_size;
	arg.nport_handle = NPH_SNS;

	/* Issue GID_PT */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(ha->ct_sns, GID_PT_CMD, gid_pt_rsp_size);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_type */
	ct_req->req.gid_pt.port_type = NS_NX_PORT_TYPE;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2055,
		    "GID_PT issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "GID_PT") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Set port IDs in switch info list. */
		for (i = 0; i < ha->max_fibre_devices; i++) {
			gid_data = &ct_rsp->rsp.gid_pt.entries[i];
			list[i].d_id.b.domain = gid_data->port_id[0];
			list[i].d_id.b.area = gid_data->port_id[1];
			list[i].d_id.b.al_pa = gid_data->port_id[2];
			memset(list[i].fabric_port_name, 0, WWN_SIZE);
			list[i].fp_speed = PORT_SPEED_UNKNOWN;

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
		if (i == ha->max_fibre_devices)
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
qla2x00_gpn_id(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval = QLA_SUCCESS;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct qla_hw_data *ha = vha->hw;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return qla2x00_sns_gpn_id(vha, list);

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = GPN_ID_REQ_SIZE;
	arg.rsp_size = GPN_ID_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Issue GPN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(ha->ct_sns, GPN_ID_CMD,
		    GPN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			ql_dbg(ql_dbg_disc, vha, 0x2056,
			    "GPN_ID issue IOCB failed (%d).\n", rval);
			break;
		} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp,
		    "GPN_ID") != QLA_SUCCESS) {
			rval = QLA_FUNCTION_FAILED;
			break;
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
qla2x00_gnn_id(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval = QLA_SUCCESS;
	uint16_t	i;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return qla2x00_sns_gnn_id(vha, list);

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = GNN_ID_REQ_SIZE;
	arg.rsp_size = GNN_ID_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Issue GNN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(ha->ct_sns, GNN_ID_CMD,
		    GNN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			ql_dbg(ql_dbg_disc, vha, 0x2057,
			    "GNN_ID issue IOCB failed (%d).\n", rval);
			break;
		} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp,
		    "GNN_ID") != QLA_SUCCESS) {
			rval = QLA_FUNCTION_FAILED;
			break;
		} else {
			/* Save nodename */
			memcpy(list[i].node_name,
			    ct_rsp->rsp.gnn_id.node_name, WWN_SIZE);

			ql_dbg(ql_dbg_disc, vha, 0x2058,
			    "GID_PT entry - nn %8phN pn %8phN "
			    "portid=%02x%02x%02x.\n",
			    list[i].node_name, list[i].port_name,
			    list[i].d_id.b.domain, list[i].d_id.b.area,
			    list[i].d_id.b.al_pa);
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
qla2x00_rft_id(scsi_qla_host_t *vha)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return qla2x00_sns_rft_id(vha);

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = RFT_ID_REQ_SIZE;
	arg.rsp_size = RFT_ID_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	/* Issue RFT_ID */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(ha->ct_sns, RFT_ID_CMD,
	    RFT_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, FC-4 types */
	ct_req->req.rft_id.port_id[0] = vha->d_id.b.domain;
	ct_req->req.rft_id.port_id[1] = vha->d_id.b.area;
	ct_req->req.rft_id.port_id[2] = vha->d_id.b.al_pa;

	ct_req->req.rft_id.fc4_types[2] = 0x01;		/* FCP-3 */

	if (vha->flags.nvme_enabled)
		ct_req->req.rft_id.fc4_types[6] = 1;    /* NVMe type 28h */
	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2043,
		    "RFT_ID issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RFT_ID") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2044,
		    "RFT_ID exiting normally.\n");
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
qla2x00_rff_id(scsi_qla_host_t *vha, u8 type)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		ql_dbg(ql_dbg_disc, vha, 0x2046,
		    "RFF_ID call not supported on ISP2100/ISP2200.\n");
		return (QLA_SUCCESS);
	}

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = RFF_ID_REQ_SIZE;
	arg.rsp_size = RFF_ID_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	/* Issue RFF_ID */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(ha->ct_sns, RFF_ID_CMD,
	    RFF_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, FC-4 feature, FC-4 type */
	ct_req->req.rff_id.port_id[0] = vha->d_id.b.domain;
	ct_req->req.rff_id.port_id[1] = vha->d_id.b.area;
	ct_req->req.rff_id.port_id[2] = vha->d_id.b.al_pa;

	qlt_rff_id(vha, ct_req);

	ct_req->req.rff_id.fc4_type = type;		/* SCSI - FCP */

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2047,
		    "RFF_ID issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RFF_ID") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2048,
		    "RFF_ID exiting normally.\n");
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
qla2x00_rnn_id(scsi_qla_host_t *vha)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return qla2x00_sns_rnn_id(vha);

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = RNN_ID_REQ_SIZE;
	arg.rsp_size = RNN_ID_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	/* Issue RNN_ID */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(ha->ct_sns, RNN_ID_CMD, RNN_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, node_name */
	ct_req->req.rnn_id.port_id[0] = vha->d_id.b.domain;
	ct_req->req.rnn_id.port_id[1] = vha->d_id.b.area;
	ct_req->req.rnn_id.port_id[2] = vha->d_id.b.al_pa;

	memcpy(ct_req->req.rnn_id.node_name, vha->node_name, WWN_SIZE);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x204d,
		    "RNN_ID issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RNN_ID") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x204e,
		    "RNN_ID exiting normally.\n");
	}

	return (rval);
}

void
qla2x00_get_sym_node_name(scsi_qla_host_t *vha, uint8_t *snn, size_t size)
{
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLAFX00(ha))
		snprintf(snn, size, "%s FW:v%s DVR:v%s", ha->model_number,
		    ha->mr.fw_version, qla2x00_version_str);
	else
		snprintf(snn, size,
		    "%s FW:v%d.%02d.%02d DVR:v%s", ha->model_number,
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
qla2x00_rsnn_nn(scsi_qla_host_t *vha)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		ql_dbg(ql_dbg_disc, vha, 0x2050,
		    "RSNN_ID call unsupported on ISP2100/ISP2200.\n");
		return (QLA_SUCCESS);
	}

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = 0;
	arg.rsp_size = RSNN_NN_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	/* Issue RSNN_NN */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(ha->ct_sns, RSNN_NN_CMD,
	    RSNN_NN_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- node_name, symbolic node_name, size */
	memcpy(ct_req->req.rsnn_nn.node_name, vha->node_name, WWN_SIZE);

	/* Prepare the Symbolic Node Name */
	qla2x00_get_sym_node_name(vha, ct_req->req.rsnn_nn.sym_node_name,
	    sizeof(ct_req->req.rsnn_nn.sym_node_name));

	/* Calculate SNN length */
	ct_req->req.rsnn_nn.name_len =
	    (uint8_t)strlen(ct_req->req.rsnn_nn.sym_node_name);

	/* Update MS IOCB request */
	ms_pkt->req_bytecount =
	    cpu_to_le32(24 + 1 + ct_req->req.rsnn_nn.name_len);
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2051,
		    "RSNN_NN issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RSNN_NN") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2052,
		    "RSNN_NN exiting normally.\n");
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
qla2x00_prep_sns_cmd(scsi_qla_host_t *vha, uint16_t cmd, uint16_t scmd_len,
    uint16_t data_size)
{
	uint16_t		wc;
	struct sns_cmd_pkt	*sns_cmd;
	struct qla_hw_data *ha = vha->hw;

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

	vha->qla_stats.control_requests++;

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
qla2x00_sns_ga_nxt(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int		rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;
	struct sns_cmd_pkt	*sns_cmd;

	/* Issue GA_NXT. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(vha, GA_NXT_CMD, GA_NXT_SNS_SCMD_LEN,
	    GA_NXT_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_id. */
	sns_cmd->p.cmd.param[0] = fcport->d_id.b.al_pa;
	sns_cmd->p.cmd.param[1] = fcport->d_id.b.area;
	sns_cmd->p.cmd.param[2] = fcport->d_id.b.domain;

	/* Execute SNS command. */
	rval = qla2x00_send_sns(vha, ha->sns_cmd_dma, GA_NXT_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x205f,
		    "GA_NXT Send SNS failed (%d).\n", rval);
	} else if (sns_cmd->p.gan_data[8] != 0x80 ||
	    sns_cmd->p.gan_data[9] != 0x02) {
		ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x2084,
		    "GA_NXT failed, rejected request ga_nxt_rsp:\n");
		ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x2074,
		    sns_cmd->p.gan_data, 16);
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

		ql_dbg(ql_dbg_disc, vha, 0x2061,
		    "GA_NXT entry - nn %8phN pn %8phN "
		    "port_id=%02x%02x%02x.\n",
		    fcport->node_name, fcport->port_name,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa);
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
qla2x00_sns_gid_pt(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	uint16_t	i;
	uint8_t		*entry;
	struct sns_cmd_pkt	*sns_cmd;
	uint16_t gid_pt_sns_data_size;

	gid_pt_sns_data_size = qla2x00_gid_pt_rsp_size(vha);

	/* Issue GID_PT. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(vha, GID_PT_CMD, GID_PT_SNS_SCMD_LEN,
	    gid_pt_sns_data_size);

	/* Prepare SNS command arguments -- port_type. */
	sns_cmd->p.cmd.param[0] = NS_NX_PORT_TYPE;

	/* Execute SNS command. */
	rval = qla2x00_send_sns(vha, ha->sns_cmd_dma, GID_PT_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x206d,
		    "GID_PT Send SNS failed (%d).\n", rval);
	} else if (sns_cmd->p.gid_data[8] != 0x80 ||
	    sns_cmd->p.gid_data[9] != 0x02) {
		ql_dbg(ql_dbg_disc, vha, 0x202f,
		    "GID_PT failed, rejected request, gid_rsp:\n");
		ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x2081,
		    sns_cmd->p.gid_data, 16);
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Set port IDs in switch info list. */
		for (i = 0; i < ha->max_fibre_devices; i++) {
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
		if (i == ha->max_fibre_devices)
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
qla2x00_sns_gpn_id(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;
	uint16_t	i;
	struct sns_cmd_pkt	*sns_cmd;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Issue GPN_ID */
		/* Prepare SNS command request. */
		sns_cmd = qla2x00_prep_sns_cmd(vha, GPN_ID_CMD,
		    GPN_ID_SNS_SCMD_LEN, GPN_ID_SNS_DATA_SIZE);

		/* Prepare SNS command arguments -- port_id. */
		sns_cmd->p.cmd.param[0] = list[i].d_id.b.al_pa;
		sns_cmd->p.cmd.param[1] = list[i].d_id.b.area;
		sns_cmd->p.cmd.param[2] = list[i].d_id.b.domain;

		/* Execute SNS command. */
		rval = qla2x00_send_sns(vha, ha->sns_cmd_dma,
		    GPN_ID_SNS_CMD_SIZE / 2, sizeof(struct sns_cmd_pkt));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			ql_dbg(ql_dbg_disc, vha, 0x2032,
			    "GPN_ID Send SNS failed (%d).\n", rval);
		} else if (sns_cmd->p.gpn_data[8] != 0x80 ||
		    sns_cmd->p.gpn_data[9] != 0x02) {
			ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x207e,
			    "GPN_ID failed, rejected request, gpn_rsp:\n");
			ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x207f,
			    sns_cmd->p.gpn_data, 16);
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
qla2x00_sns_gnn_id(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;
	uint16_t	i;
	struct sns_cmd_pkt	*sns_cmd;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Issue GNN_ID */
		/* Prepare SNS command request. */
		sns_cmd = qla2x00_prep_sns_cmd(vha, GNN_ID_CMD,
		    GNN_ID_SNS_SCMD_LEN, GNN_ID_SNS_DATA_SIZE);

		/* Prepare SNS command arguments -- port_id. */
		sns_cmd->p.cmd.param[0] = list[i].d_id.b.al_pa;
		sns_cmd->p.cmd.param[1] = list[i].d_id.b.area;
		sns_cmd->p.cmd.param[2] = list[i].d_id.b.domain;

		/* Execute SNS command. */
		rval = qla2x00_send_sns(vha, ha->sns_cmd_dma,
		    GNN_ID_SNS_CMD_SIZE / 2, sizeof(struct sns_cmd_pkt));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			ql_dbg(ql_dbg_disc, vha, 0x203f,
			    "GNN_ID Send SNS failed (%d).\n", rval);
		} else if (sns_cmd->p.gnn_data[8] != 0x80 ||
		    sns_cmd->p.gnn_data[9] != 0x02) {
			ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x2082,
			    "GNN_ID failed, rejected request, gnn_rsp:\n");
			ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x207a,
			    sns_cmd->p.gnn_data, 16);
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save nodename */
			memcpy(list[i].node_name, &sns_cmd->p.gnn_data[16],
			    WWN_SIZE);

			ql_dbg(ql_dbg_disc, vha, 0x206e,
			    "GID_PT entry - nn %8phN pn %8phN "
			    "port_id=%02x%02x%02x.\n",
			    list[i].node_name, list[i].port_name,
			    list[i].d_id.b.domain, list[i].d_id.b.area,
			    list[i].d_id.b.al_pa);
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
qla2x00_sns_rft_id(scsi_qla_host_t *vha)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	struct sns_cmd_pkt	*sns_cmd;

	/* Issue RFT_ID. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(vha, RFT_ID_CMD, RFT_ID_SNS_SCMD_LEN,
	    RFT_ID_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_id, FC-4 types */
	sns_cmd->p.cmd.param[0] = vha->d_id.b.al_pa;
	sns_cmd->p.cmd.param[1] = vha->d_id.b.area;
	sns_cmd->p.cmd.param[2] = vha->d_id.b.domain;

	sns_cmd->p.cmd.param[5] = 0x01;			/* FCP-3 */

	/* Execute SNS command. */
	rval = qla2x00_send_sns(vha, ha->sns_cmd_dma, RFT_ID_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2060,
		    "RFT_ID Send SNS failed (%d).\n", rval);
	} else if (sns_cmd->p.rft_data[8] != 0x80 ||
	    sns_cmd->p.rft_data[9] != 0x02) {
		ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x2083,
		    "RFT_ID failed, rejected request rft_rsp:\n");
		ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x2080,
		    sns_cmd->p.rft_data, 16);
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2073,
		    "RFT_ID exiting normally.\n");
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
qla2x00_sns_rnn_id(scsi_qla_host_t *vha)
{
	int		rval;
	struct qla_hw_data *ha = vha->hw;
	struct sns_cmd_pkt	*sns_cmd;

	/* Issue RNN_ID. */
	/* Prepare SNS command request. */
	sns_cmd = qla2x00_prep_sns_cmd(vha, RNN_ID_CMD, RNN_ID_SNS_SCMD_LEN,
	    RNN_ID_SNS_DATA_SIZE);

	/* Prepare SNS command arguments -- port_id, nodename. */
	sns_cmd->p.cmd.param[0] = vha->d_id.b.al_pa;
	sns_cmd->p.cmd.param[1] = vha->d_id.b.area;
	sns_cmd->p.cmd.param[2] = vha->d_id.b.domain;

	sns_cmd->p.cmd.param[4] = vha->node_name[7];
	sns_cmd->p.cmd.param[5] = vha->node_name[6];
	sns_cmd->p.cmd.param[6] = vha->node_name[5];
	sns_cmd->p.cmd.param[7] = vha->node_name[4];
	sns_cmd->p.cmd.param[8] = vha->node_name[3];
	sns_cmd->p.cmd.param[9] = vha->node_name[2];
	sns_cmd->p.cmd.param[10] = vha->node_name[1];
	sns_cmd->p.cmd.param[11] = vha->node_name[0];

	/* Execute SNS command. */
	rval = qla2x00_send_sns(vha, ha->sns_cmd_dma, RNN_ID_SNS_CMD_SIZE / 2,
	    sizeof(struct sns_cmd_pkt));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x204a,
		    "RNN_ID Send SNS failed (%d).\n", rval);
	} else if (sns_cmd->p.rnn_data[8] != 0x80 ||
	    sns_cmd->p.rnn_data[9] != 0x02) {
		ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x207b,
		    "RNN_ID failed, rejected request, rnn_rsp:\n");
		ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x207c,
		    sns_cmd->p.rnn_data, 16);
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x204c,
		    "RNN_ID exiting normally.\n");
	}

	return (rval);
}

/**
 * qla2x00_mgmt_svr_login() - Login to fabric Management Service.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_mgmt_svr_login(scsi_qla_host_t *vha)
{
	int ret, rval;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct qla_hw_data *ha = vha->hw;
	ret = QLA_SUCCESS;
	if (vha->flags.management_server_logged_in)
		return ret;

	rval = ha->isp_ops->fabric_login(vha, vha->mgmt_svr_loop_id, 0xff, 0xff,
	    0xfa, mb, BIT_1);
	if (rval != QLA_SUCCESS || mb[0] != MBS_COMMAND_COMPLETE) {
		if (rval == QLA_MEMORY_ALLOC_FAILED)
			ql_dbg(ql_dbg_disc, vha, 0x2085,
			    "Failed management_server login: loopid=%x "
			    "rval=%d\n", vha->mgmt_svr_loop_id, rval);
		else
			ql_dbg(ql_dbg_disc, vha, 0x2024,
			    "Failed management_server login: loopid=%x "
			    "mb[0]=%x mb[1]=%x mb[2]=%x mb[6]=%x mb[7]=%x.\n",
			    vha->mgmt_svr_loop_id, mb[0], mb[1], mb[2], mb[6],
			    mb[7]);
		ret = QLA_FUNCTION_FAILED;
	} else
		vha->flags.management_server_logged_in = 1;

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
qla2x00_prep_ms_fdmi_iocb(scsi_qla_host_t *vha, uint32_t req_size,
    uint32_t rsp_size)
{
	ms_iocb_entry_t *ms_pkt;
	struct qla_hw_data *ha = vha->hw;
	ms_pkt = ha->ms_iocb;
	memset(ms_pkt, 0, sizeof(ms_iocb_entry_t));

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;
	SET_TARGET_ID(ha, ms_pkt->loop_id, vha->mgmt_svr_loop_id);
	ms_pkt->control_flags = cpu_to_le16(CF_READ | CF_HEAD_TAG);
	ms_pkt->timeout = cpu_to_le16(ha->r_a_tov / 10 * 2);
	ms_pkt->cmd_dsd_count = cpu_to_le16(1);
	ms_pkt->total_dsd_count = cpu_to_le16(2);
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
qla24xx_prep_ms_fdmi_iocb(scsi_qla_host_t *vha, uint32_t req_size,
    uint32_t rsp_size)
{
	struct ct_entry_24xx *ct_pkt;
	struct qla_hw_data *ha = vha->hw;

	ct_pkt = (struct ct_entry_24xx *)ha->ms_iocb;
	memset(ct_pkt, 0, sizeof(struct ct_entry_24xx));

	ct_pkt->entry_type = CT_IOCB_TYPE;
	ct_pkt->entry_count = 1;
	ct_pkt->nport_handle = cpu_to_le16(vha->mgmt_svr_loop_id);
	ct_pkt->timeout = cpu_to_le16(ha->r_a_tov / 10 * 2);
	ct_pkt->cmd_dsd_count = cpu_to_le16(1);
	ct_pkt->rsp_dsd_count = cpu_to_le16(1);
	ct_pkt->rsp_byte_count = cpu_to_le32(rsp_size);
	ct_pkt->cmd_byte_count = cpu_to_le32(req_size);

	ct_pkt->dseg_0_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;

	ct_pkt->dseg_1_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ct_pkt->dseg_1_len = ct_pkt->rsp_byte_count;
	ct_pkt->vp_index = vha->vp_idx;

	return ct_pkt;
}

static inline ms_iocb_entry_t *
qla2x00_update_ms_fdmi_iocb(scsi_qla_host_t *vha, uint32_t req_size)
{
	struct qla_hw_data *ha = vha->hw;
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
qla2x00_prep_ct_fdmi_req(struct ct_sns_pkt *p, uint16_t cmd,
    uint16_t rsp_size)
{
	memset(p, 0, sizeof(struct ct_sns_pkt));

	p->p.req.header.revision = 0x01;
	p->p.req.header.gs_type = 0xFA;
	p->p.req.header.gs_subtype = 0x10;
	p->p.req.command = cpu_to_be16(cmd);
	p->p.req.max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return &p->p.req;
}

/**
 * qla2x00_fdmi_rhba() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fdmi_rhba(scsi_qla_host_t *vha)
{
	int rval, alen;
	uint32_t size, sn;

	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;
	void *entries;
	struct ct_fdmi_hba_attr *eiter;
	struct qla_hw_data *ha = vha->hw;

	/* Issue RHBA */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(vha, 0, RHBA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(ha->ct_sns, RHBA_CMD, RHBA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- attribute block, attributes. */
	memcpy(ct_req->req.rhba.hba_identifier, vha->port_name, WWN_SIZE);
	ct_req->req.rhba.entry_count = cpu_to_be32(1);
	memcpy(ct_req->req.rhba.port_name, vha->port_name, WWN_SIZE);
	size = 2 * WWN_SIZE + 4 + 4;

	/* Attributes */
	ct_req->req.rhba.attrs.count =
	    cpu_to_be32(FDMI_HBA_ATTR_COUNT);
	entries = ct_req->req.rhba.hba_identifier;

	/* Nodename. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_NODE_NAME);
	eiter->len = cpu_to_be16(4 + WWN_SIZE);
	memcpy(eiter->a.node_name, vha->node_name, WWN_SIZE);
	size += 4 + WWN_SIZE;

	ql_dbg(ql_dbg_disc, vha, 0x2025,
	    "NodeName = %8phN.\n", eiter->a.node_name);

	/* Manufacturer. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MANUFACTURER);
	alen = strlen(QLA2XXX_MANUFACTURER);
	snprintf(eiter->a.manufacturer, sizeof(eiter->a.manufacturer),
	    "%s", "QLogic Corporation");
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x2026,
	    "Manufacturer = %s.\n", eiter->a.manufacturer);

	/* Serial number. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_SERIAL_NUMBER);
	if (IS_FWI2_CAPABLE(ha))
		qla2xxx_get_vpd_field(vha, "SN", eiter->a.serial_num,
		    sizeof(eiter->a.serial_num));
	else {
		sn = ((ha->serial0 & 0x1f) << 16) |
			(ha->serial2 << 8) | ha->serial1;
		snprintf(eiter->a.serial_num, sizeof(eiter->a.serial_num),
		    "%c%05d", 'A' + sn / 100000, sn % 100000);
	}
	alen = strlen(eiter->a.serial_num);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x2027,
	    "Serial no. = %s.\n", eiter->a.serial_num);

	/* Model name. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MODEL);
	snprintf(eiter->a.model, sizeof(eiter->a.model),
	    "%s", ha->model_number);
	alen = strlen(eiter->a.model);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x2028,
	    "Model Name = %s.\n", eiter->a.model);

	/* Model description. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MODEL_DESCRIPTION);
	snprintf(eiter->a.model_desc, sizeof(eiter->a.model_desc),
	    "%s", ha->model_desc);
	alen = strlen(eiter->a.model_desc);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x2029,
	    "Model Desc = %s.\n", eiter->a.model_desc);

	/* Hardware version. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_HARDWARE_VERSION);
	if (!IS_FWI2_CAPABLE(ha)) {
		snprintf(eiter->a.hw_version, sizeof(eiter->a.hw_version),
		    "HW:%s", ha->adapter_id);
	} else if (qla2xxx_get_vpd_field(vha, "MN", eiter->a.hw_version,
		    sizeof(eiter->a.hw_version))) {
		;
	} else if (qla2xxx_get_vpd_field(vha, "EC", eiter->a.hw_version,
		    sizeof(eiter->a.hw_version))) {
		;
	} else {
		snprintf(eiter->a.hw_version, sizeof(eiter->a.hw_version),
		    "HW:%s", ha->adapter_id);
	}
	alen = strlen(eiter->a.hw_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x202a,
	    "Hardware ver = %s.\n", eiter->a.hw_version);

	/* Driver version. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_DRIVER_VERSION);
	snprintf(eiter->a.driver_version, sizeof(eiter->a.driver_version),
	    "%s", qla2x00_version_str);
	alen = strlen(eiter->a.driver_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x202b,
	    "Driver ver = %s.\n", eiter->a.driver_version);

	/* Option ROM version. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_OPTION_ROM_VERSION);
	snprintf(eiter->a.orom_version, sizeof(eiter->a.orom_version),
	    "%d.%02d", ha->bios_revision[1], ha->bios_revision[0]);
	alen = strlen(eiter->a.orom_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha , 0x202c,
	    "Optrom vers = %s.\n", eiter->a.orom_version);

	/* Firmware version */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_FIRMWARE_VERSION);
	ha->isp_ops->fw_version_str(vha, eiter->a.fw_version,
	    sizeof(eiter->a.fw_version));
	alen = strlen(eiter->a.fw_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x202d,
	    "Firmware vers = %s.\n", eiter->a.fw_version);

	/* Update MS request size. */
	qla2x00_update_ms_fdmi_iocb(vha, size + 16);

	ql_dbg(ql_dbg_disc, vha, 0x202e,
	    "RHBA identifier = %8phN size=%d.\n",
	    ct_req->req.rhba.hba_identifier, size);
	ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x2076,
	    entries, size);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2030,
		    "RHBA issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RHBA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
		if (ct_rsp->header.reason_code == CT_REASON_CANNOT_PERFORM &&
		    ct_rsp->header.explanation_code ==
		    CT_EXPL_ALREADY_REGISTERED) {
			ql_dbg(ql_dbg_disc, vha, 0x2034,
			    "HBA already registered.\n");
			rval = QLA_ALREADY_REGISTERED;
		} else {
			ql_dbg(ql_dbg_disc, vha, 0x20ad,
			    "RHBA FDMI registration failed, CT Reason code: 0x%x, CT Explanation 0x%x\n",
			    ct_rsp->header.reason_code,
			    ct_rsp->header.explanation_code);
		}
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2035,
		    "RHBA exiting normally.\n");
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
qla2x00_fdmi_rpa(scsi_qla_host_t *vha)
{
	int rval, alen;
	uint32_t size;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;
	void *entries;
	struct ct_fdmi_port_attr *eiter;
	struct init_cb_24xx *icb24 = (struct init_cb_24xx *)ha->init_cb;
	struct new_utsname *p_sysid = NULL;

	/* Issue RPA */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(vha, 0, RPA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(ha->ct_sns, RPA_CMD,
	    RPA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- attribute block, attributes. */
	memcpy(ct_req->req.rpa.port_name, vha->port_name, WWN_SIZE);
	size = WWN_SIZE + 4;

	/* Attributes */
	ct_req->req.rpa.attrs.count = cpu_to_be32(FDMI_PORT_ATTR_COUNT);
	entries = ct_req->req.rpa.port_name;

	/* FC4 types. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_FC4_TYPES);
	eiter->len = cpu_to_be16(4 + 32);
	eiter->a.fc4_types[2] = 0x01;
	size += 4 + 32;

	ql_dbg(ql_dbg_disc, vha, 0x2039,
	    "FC4_TYPES=%02x %02x.\n",
	    eiter->a.fc4_types[2],
	    eiter->a.fc4_types[1]);

	/* Supported speed. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_SUPPORT_SPEED);
	eiter->len = cpu_to_be16(4 + 4);
	if (IS_CNA_CAPABLE(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_10GB);
	else if (IS_QLA27XX(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_32GB|
		    FDMI_PORT_SPEED_16GB|
		    FDMI_PORT_SPEED_8GB);
	else if (IS_QLA2031(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_16GB|
		    FDMI_PORT_SPEED_8GB|
		    FDMI_PORT_SPEED_4GB);
	else if (IS_QLA25XX(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_8GB|
		    FDMI_PORT_SPEED_4GB|
		    FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_1GB);
	else if (IS_QLA24XX_TYPE(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_4GB|
		    FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_1GB);
	else if (IS_QLA23XX(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_1GB);
	else
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_1GB);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x203a,
	    "Supported_Speed=%x.\n", eiter->a.sup_speed);

	/* Current speed. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_CURRENT_SPEED);
	eiter->len = cpu_to_be16(4 + 4);
	switch (ha->link_data_rate) {
	case PORT_SPEED_1GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_1GB);
		break;
	case PORT_SPEED_2GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_2GB);
		break;
	case PORT_SPEED_4GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_4GB);
		break;
	case PORT_SPEED_8GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_8GB);
		break;
	case PORT_SPEED_10GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_10GB);
		break;
	case PORT_SPEED_16GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_16GB);
		break;
	case PORT_SPEED_32GB:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_32GB);
		break;
	default:
		eiter->a.cur_speed =
		    cpu_to_be32(FDMI_PORT_SPEED_UNKNOWN);
		break;
	}
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x203b,
	    "Current_Speed=%x.\n", eiter->a.cur_speed);

	/* Max frame size. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_MAX_FRAME_SIZE);
	eiter->len = cpu_to_be16(4 + 4);
	eiter->a.max_frame_size = IS_FWI2_CAPABLE(ha) ?
	    le16_to_cpu(icb24->frame_payload_size) :
	    le16_to_cpu(ha->init_cb->frame_payload_size);
	eiter->a.max_frame_size = cpu_to_be32(eiter->a.max_frame_size);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x203c,
	    "Max_Frame_Size=%x.\n", eiter->a.max_frame_size);

	/* OS device name. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_OS_DEVICE_NAME);
	snprintf(eiter->a.os_dev_name, sizeof(eiter->a.os_dev_name),
	    "%s:host%lu", QLA2XXX_DRIVER_NAME, vha->host_no);
	alen = strlen(eiter->a.os_dev_name);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x204b,
	    "OS_Device_Name=%s.\n", eiter->a.os_dev_name);

	/* Hostname. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_HOST_NAME);
	p_sysid = utsname();
	if (p_sysid) {
		snprintf(eiter->a.host_name, sizeof(eiter->a.host_name),
		    "%s", p_sysid->nodename);
	} else {
		snprintf(eiter->a.host_name, sizeof(eiter->a.host_name),
		    "%s", fc_host_system_hostname(vha->host));
	}
	alen = strlen(eiter->a.host_name);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x203d, "HostName=%s.\n", eiter->a.host_name);

	/* Update MS request size. */
	qla2x00_update_ms_fdmi_iocb(vha, size + 16);

	ql_dbg(ql_dbg_disc, vha, 0x203e,
	    "RPA portname  %016llx, size = %d.\n",
	    wwn_to_u64(ct_req->req.rpa.port_name), size);
	ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x2079,
	    entries, size);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2040,
		    "RPA issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RPA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
		if (ct_rsp->header.reason_code == CT_REASON_CANNOT_PERFORM &&
		    ct_rsp->header.explanation_code ==
		    CT_EXPL_ALREADY_REGISTERED) {
			ql_dbg(ql_dbg_disc, vha, 0x20cd,
			    "RPA already registered.\n");
			rval = QLA_ALREADY_REGISTERED;
		}

	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2041,
		    "RPA exiting normally.\n");
	}

	return rval;
}

/**
 * qla2x00_fdmiv2_rhba() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fdmiv2_rhba(scsi_qla_host_t *vha)
{
	int rval, alen;
	uint32_t size, sn;
	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;
	void *entries;
	struct ct_fdmiv2_hba_attr *eiter;
	struct qla_hw_data *ha = vha->hw;
	struct init_cb_24xx *icb24 = (struct init_cb_24xx *)ha->init_cb;
	struct new_utsname *p_sysid = NULL;

	/* Issue RHBA */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(vha, 0, RHBA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(ha->ct_sns, RHBA_CMD,
	    RHBA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- attribute block, attributes. */
	memcpy(ct_req->req.rhba2.hba_identifier, vha->port_name, WWN_SIZE);
	ct_req->req.rhba2.entry_count = cpu_to_be32(1);
	memcpy(ct_req->req.rhba2.port_name, vha->port_name, WWN_SIZE);
	size = 2 * WWN_SIZE + 4 + 4;

	/* Attributes */
	ct_req->req.rhba2.attrs.count = cpu_to_be32(FDMIV2_HBA_ATTR_COUNT);
	entries = ct_req->req.rhba2.hba_identifier;

	/* Nodename. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_NODE_NAME);
	eiter->len = cpu_to_be16(4 + WWN_SIZE);
	memcpy(eiter->a.node_name, vha->node_name, WWN_SIZE);
	size += 4 + WWN_SIZE;

	ql_dbg(ql_dbg_disc, vha, 0x207d,
	    "NodeName = %016llx.\n", wwn_to_u64(eiter->a.node_name));

	/* Manufacturer. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MANUFACTURER);
	snprintf(eiter->a.manufacturer, sizeof(eiter->a.manufacturer),
	    "%s", "QLogic Corporation");
	eiter->a.manufacturer[strlen("QLogic Corporation")] = '\0';
	alen = strlen(eiter->a.manufacturer);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20a5,
	    "Manufacturer = %s.\n", eiter->a.manufacturer);

	/* Serial number. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_SERIAL_NUMBER);
	if (IS_FWI2_CAPABLE(ha))
		qla2xxx_get_vpd_field(vha, "SN", eiter->a.serial_num,
		    sizeof(eiter->a.serial_num));
	else {
		sn = ((ha->serial0 & 0x1f) << 16) |
			(ha->serial2 << 8) | ha->serial1;
		snprintf(eiter->a.serial_num, sizeof(eiter->a.serial_num),
		    "%c%05d", 'A' + sn / 100000, sn % 100000);
	}
	alen = strlen(eiter->a.serial_num);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20a6,
	    "Serial no. = %s.\n", eiter->a.serial_num);

	/* Model name. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MODEL);
	snprintf(eiter->a.model, sizeof(eiter->a.model),
	    "%s", ha->model_number);
	alen = strlen(eiter->a.model);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20a7,
	    "Model Name = %s.\n", eiter->a.model);

	/* Model description. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MODEL_DESCRIPTION);
	snprintf(eiter->a.model_desc, sizeof(eiter->a.model_desc),
	    "%s", ha->model_desc);
	alen = strlen(eiter->a.model_desc);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20a8,
	    "Model Desc = %s.\n", eiter->a.model_desc);

	/* Hardware version. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_HARDWARE_VERSION);
	if (!IS_FWI2_CAPABLE(ha)) {
		snprintf(eiter->a.hw_version, sizeof(eiter->a.hw_version),
		    "HW:%s", ha->adapter_id);
	} else if (qla2xxx_get_vpd_field(vha, "MN", eiter->a.hw_version,
		    sizeof(eiter->a.hw_version))) {
		;
	} else if (qla2xxx_get_vpd_field(vha, "EC", eiter->a.hw_version,
		    sizeof(eiter->a.hw_version))) {
		;
	} else {
		snprintf(eiter->a.hw_version, sizeof(eiter->a.hw_version),
		    "HW:%s", ha->adapter_id);
	}
	alen = strlen(eiter->a.hw_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20a9,
	    "Hardware ver = %s.\n", eiter->a.hw_version);

	/* Driver version. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_DRIVER_VERSION);
	snprintf(eiter->a.driver_version, sizeof(eiter->a.driver_version),
	    "%s", qla2x00_version_str);
	alen = strlen(eiter->a.driver_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20aa,
	    "Driver ver = %s.\n", eiter->a.driver_version);

	/* Option ROM version. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_OPTION_ROM_VERSION);
	snprintf(eiter->a.orom_version, sizeof(eiter->a.orom_version),
	    "%d.%02d", ha->bios_revision[1], ha->bios_revision[0]);
	alen = strlen(eiter->a.orom_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha , 0x20ab,
	    "Optrom version = %d.%02d.\n", eiter->a.orom_version[1],
	    eiter->a.orom_version[0]);

	/* Firmware version */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_FIRMWARE_VERSION);
	ha->isp_ops->fw_version_str(vha, eiter->a.fw_version,
	    sizeof(eiter->a.fw_version));
	alen = strlen(eiter->a.fw_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20ac,
	    "Firmware vers = %s.\n", eiter->a.fw_version);

	/* OS Name and Version */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_OS_NAME_AND_VERSION);
	p_sysid = utsname();
	if (p_sysid) {
		snprintf(eiter->a.os_version, sizeof(eiter->a.os_version),
		    "%s %s %s",
		    p_sysid->sysname, p_sysid->release, p_sysid->version);
	} else {
		snprintf(eiter->a.os_version, sizeof(eiter->a.os_version),
		    "%s %s", "Linux", fc_host_system_hostname(vha->host));
	}
	alen = strlen(eiter->a.os_version);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20ae,
	    "OS Name and Version = %s.\n", eiter->a.os_version);

	/* MAX CT Payload Length */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_MAXIMUM_CT_PAYLOAD_LENGTH);
	eiter->a.max_ct_len = IS_FWI2_CAPABLE(ha) ?
	    le16_to_cpu(icb24->frame_payload_size) :
	    le16_to_cpu(ha->init_cb->frame_payload_size);
	eiter->a.max_ct_len = cpu_to_be32(eiter->a.max_ct_len);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20af,
	    "CT Payload Length = 0x%x.\n", eiter->a.max_ct_len);

	/* Node Sybolic Name */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_NODE_SYMBOLIC_NAME);
	qla2x00_get_sym_node_name(vha, eiter->a.sym_name,
	    sizeof(eiter->a.sym_name));
	alen = strlen(eiter->a.sym_name);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20b0,
	    "Symbolic Name = %s.\n", eiter->a.sym_name);

	/* Vendor Id */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_VENDOR_ID);
	eiter->a.vendor_id = cpu_to_be32(0x1077);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20b1,
	    "Vendor Id = %x.\n", eiter->a.vendor_id);

	/* Num Ports */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_NUM_PORTS);
	eiter->a.num_ports = cpu_to_be32(1);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20b2,
	    "Port Num = %x.\n", eiter->a.num_ports);

	/* Fabric Name */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_FABRIC_NAME);
	memcpy(eiter->a.fabric_name, vha->fabric_node_name, WWN_SIZE);
	eiter->len = cpu_to_be16(4 + WWN_SIZE);
	size += 4 + WWN_SIZE;

	ql_dbg(ql_dbg_disc, vha, 0x20b3,
	    "Fabric Name = %016llx.\n", wwn_to_u64(eiter->a.fabric_name));

	/* BIOS Version */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_BOOT_BIOS_NAME);
	snprintf(eiter->a.bios_name, sizeof(eiter->a.bios_name),
	    "BIOS %d.%02d", ha->bios_revision[1], ha->bios_revision[0]);
	alen = strlen(eiter->a.bios_name);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20b4,
	    "BIOS Name = %s\n", eiter->a.bios_name);

	/* Vendor Identifier */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_HBA_TYPE_VENDOR_IDENTIFIER);
	snprintf(eiter->a.vendor_identifier, sizeof(eiter->a.vendor_identifier),
	    "%s", "QLGC");
	alen = strlen(eiter->a.vendor_identifier);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x201b,
	    "Vendor Identifier = %s.\n", eiter->a.vendor_identifier);

	/* Update MS request size. */
	qla2x00_update_ms_fdmi_iocb(vha, size + 16);

	ql_dbg(ql_dbg_disc, vha, 0x20b5,
	    "RHBA identifier = %016llx.\n",
	    wwn_to_u64(ct_req->req.rhba2.hba_identifier));
	ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x20b6,
	    entries, size);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x20b7,
		    "RHBA issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RHBA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;

		if (ct_rsp->header.reason_code == CT_REASON_CANNOT_PERFORM &&
		    ct_rsp->header.explanation_code ==
		    CT_EXPL_ALREADY_REGISTERED) {
			ql_dbg(ql_dbg_disc, vha, 0x20b8,
			    "HBA already registered.\n");
			rval = QLA_ALREADY_REGISTERED;
		} else {
			ql_dbg(ql_dbg_disc, vha, 0x2016,
			    "RHBA FDMI v2 failed, CT Reason code: 0x%x, CT Explanation 0x%x\n",
			    ct_rsp->header.reason_code,
			    ct_rsp->header.explanation_code);
		}
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x20b9,
		    "RHBA FDMI V2 exiting normally.\n");
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
qla2x00_fdmi_dhba(scsi_qla_host_t *vha)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;

	/* Issue RPA */
	/* Prepare common MS IOCB */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(vha, DHBA_REQ_SIZE,
	    DHBA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(ha->ct_sns, DHBA_CMD, DHBA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- portname. */
	memcpy(ct_req->req.dhba.port_name, vha->port_name, WWN_SIZE);

	ql_dbg(ql_dbg_disc, vha, 0x2036,
	    "DHBA portname = %8phN.\n", ct_req->req.dhba.port_name);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x2037,
		    "DHBA issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "DHBA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x2038,
		    "DHBA exiting normally.\n");
	}

	return rval;
}

/**
 * qla2x00_fdmiv2_rpa() -
 * @ha: HA context
 *
 * Returns 0 on success.
 */
static int
qla2x00_fdmiv2_rpa(scsi_qla_host_t *vha)
{
	int rval, alen;
	uint32_t size;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req *ct_req;
	struct ct_sns_rsp *ct_rsp;
	void *entries;
	struct ct_fdmiv2_port_attr *eiter;
	struct init_cb_24xx *icb24 = (struct init_cb_24xx *)ha->init_cb;
	struct new_utsname *p_sysid = NULL;

	/* Issue RPA */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = ha->isp_ops->prep_ms_fdmi_iocb(vha, 0, RPA_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_fdmi_req(ha->ct_sns, RPA_CMD, RPA_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare FDMI command arguments -- attribute block, attributes. */
	memcpy(ct_req->req.rpa2.port_name, vha->port_name, WWN_SIZE);
	size = WWN_SIZE + 4;

	/* Attributes */
	ct_req->req.rpa2.attrs.count = cpu_to_be32(FDMIV2_PORT_ATTR_COUNT);
	entries = ct_req->req.rpa2.port_name;

	/* FC4 types. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_FC4_TYPES);
	eiter->len = cpu_to_be16(4 + 32);
	eiter->a.fc4_types[2] = 0x01;
	size += 4 + 32;

	ql_dbg(ql_dbg_disc, vha, 0x20ba,
	    "FC4_TYPES=%02x %02x.\n",
	    eiter->a.fc4_types[2],
	    eiter->a.fc4_types[1]);

	if (vha->flags.nvme_enabled) {
		eiter->a.fc4_types[6] = 1;	/* NVMe type 28h */
		ql_dbg(ql_dbg_disc, vha, 0x211f,
		    "NVME FC4 Type = %02x 0x0 0x0 0x0 0x0 0x0.\n",
		    eiter->a.fc4_types[6]);
	}

	/* Supported speed. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_SUPPORT_SPEED);
	eiter->len = cpu_to_be16(4 + 4);
	if (IS_CNA_CAPABLE(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_10GB);
	else if (IS_QLA27XX(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_32GB|
		    FDMI_PORT_SPEED_16GB|
		    FDMI_PORT_SPEED_8GB);
	else if (IS_QLA2031(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_16GB|
		    FDMI_PORT_SPEED_8GB|
		    FDMI_PORT_SPEED_4GB);
	else if (IS_QLA25XX(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_8GB|
		    FDMI_PORT_SPEED_4GB|
		    FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_1GB);
	else if (IS_QLA24XX_TYPE(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_4GB|
		    FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_1GB);
	else if (IS_QLA23XX(ha))
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_2GB|
		    FDMI_PORT_SPEED_1GB);
	else
		eiter->a.sup_speed = cpu_to_be32(
		    FDMI_PORT_SPEED_1GB);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20bb,
	    "Supported Port Speed = %x.\n", eiter->a.sup_speed);

	/* Current speed. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_CURRENT_SPEED);
	eiter->len = cpu_to_be16(4 + 4);
	switch (ha->link_data_rate) {
	case PORT_SPEED_1GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_1GB);
		break;
	case PORT_SPEED_2GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_2GB);
		break;
	case PORT_SPEED_4GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_4GB);
		break;
	case PORT_SPEED_8GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_8GB);
		break;
	case PORT_SPEED_10GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_10GB);
		break;
	case PORT_SPEED_16GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_16GB);
		break;
	case PORT_SPEED_32GB:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_32GB);
		break;
	default:
		eiter->a.cur_speed = cpu_to_be32(FDMI_PORT_SPEED_UNKNOWN);
		break;
	}
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x2017,
	    "Current_Speed = %x.\n", eiter->a.cur_speed);

	/* Max frame size. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_MAX_FRAME_SIZE);
	eiter->len = cpu_to_be16(4 + 4);
	eiter->a.max_frame_size = IS_FWI2_CAPABLE(ha) ?
	    le16_to_cpu(icb24->frame_payload_size):
	    le16_to_cpu(ha->init_cb->frame_payload_size);
	eiter->a.max_frame_size = cpu_to_be32(eiter->a.max_frame_size);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20bc,
	    "Max_Frame_Size = %x.\n", eiter->a.max_frame_size);

	/* OS device name. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_OS_DEVICE_NAME);
	alen = strlen(QLA2XXX_DRIVER_NAME);
	snprintf(eiter->a.os_dev_name, sizeof(eiter->a.os_dev_name),
	    "%s:host%lu", QLA2XXX_DRIVER_NAME, vha->host_no);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20be,
	    "OS_Device_Name = %s.\n", eiter->a.os_dev_name);

	/* Hostname. */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_HOST_NAME);
	p_sysid = utsname();
	if (p_sysid) {
		snprintf(eiter->a.host_name, sizeof(eiter->a.host_name),
		    "%s", p_sysid->nodename);
	} else {
		snprintf(eiter->a.host_name, sizeof(eiter->a.host_name),
		    "%s", fc_host_system_hostname(vha->host));
	}
	alen = strlen(eiter->a.host_name);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x201a,
	    "HostName=%s.\n", eiter->a.host_name);

	/* Node Name */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_NODE_NAME);
	memcpy(eiter->a.node_name, vha->node_name, WWN_SIZE);
	eiter->len = cpu_to_be16(4 + WWN_SIZE);
	size += 4 + WWN_SIZE;

	ql_dbg(ql_dbg_disc, vha, 0x20c0,
	    "Node Name = %016llx.\n", wwn_to_u64(eiter->a.node_name));

	/* Port Name */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_NAME);
	memcpy(eiter->a.port_name, vha->port_name, WWN_SIZE);
	eiter->len = cpu_to_be16(4 + WWN_SIZE);
	size += 4 + WWN_SIZE;

	ql_dbg(ql_dbg_disc, vha, 0x20c1,
	    "Port Name = %016llx.\n", wwn_to_u64(eiter->a.port_name));

	/* Port Symbolic Name */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_SYM_NAME);
	qla2x00_get_sym_node_name(vha, eiter->a.port_sym_name,
	    sizeof(eiter->a.port_sym_name));
	alen = strlen(eiter->a.port_sym_name);
	alen += 4 - (alen & 3);
	eiter->len = cpu_to_be16(4 + alen);
	size += 4 + alen;

	ql_dbg(ql_dbg_disc, vha, 0x20c2,
	    "port symbolic name = %s\n", eiter->a.port_sym_name);

	/* Port Type */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_TYPE);
	eiter->a.port_type = cpu_to_be32(NS_NX_PORT_TYPE);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20c3,
	    "Port Type = %x.\n", eiter->a.port_type);

	/* Class of Service  */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_SUPP_COS);
	eiter->a.port_supported_cos = cpu_to_be32(FC_CLASS_3);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20c4,
	    "Supported COS = %08x\n", eiter->a.port_supported_cos);

	/* Port Fabric Name */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_FABRIC_NAME);
	memcpy(eiter->a.fabric_name, vha->fabric_node_name, WWN_SIZE);
	eiter->len = cpu_to_be16(4 + WWN_SIZE);
	size += 4 + WWN_SIZE;

	ql_dbg(ql_dbg_disc, vha, 0x20c5,
	    "Fabric Name = %016llx.\n", wwn_to_u64(eiter->a.fabric_name));

	/* FC4_type */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_FC4_TYPE);
	eiter->a.port_fc4_type[0] = 0;
	eiter->a.port_fc4_type[1] = 0;
	eiter->a.port_fc4_type[2] = 1;
	eiter->a.port_fc4_type[3] = 0;
	eiter->len = cpu_to_be16(4 + 32);
	size += 4 + 32;

	ql_dbg(ql_dbg_disc, vha, 0x20c6,
	    "Port Active FC4 Type = %02x %02x.\n",
	    eiter->a.port_fc4_type[2], eiter->a.port_fc4_type[1]);

	if (vha->flags.nvme_enabled) {
		eiter->a.port_fc4_type[4] = 0;
		eiter->a.port_fc4_type[5] = 0;
		eiter->a.port_fc4_type[6] = 1;	/* NVMe type 28h */
		ql_dbg(ql_dbg_disc, vha, 0x2120,
		    "NVME Port Active FC4 Type = %02x 0x0 0x0 0x0 0x0 0x0.\n",
		    eiter->a.port_fc4_type[6]);
	}

	/* Port State */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_STATE);
	eiter->a.port_state = cpu_to_be32(1);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20c7,
	    "Port State = %x.\n", eiter->a.port_state);

	/* Number of Ports */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_COUNT);
	eiter->a.num_ports = cpu_to_be32(1);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x20c8,
	    "Number of ports = %x.\n", eiter->a.num_ports);

	/* Port Id */
	eiter = entries + size;
	eiter->type = cpu_to_be16(FDMI_PORT_ID);
	eiter->a.port_id = cpu_to_be32(vha->d_id.b24);
	eiter->len = cpu_to_be16(4 + 4);
	size += 4 + 4;

	ql_dbg(ql_dbg_disc, vha, 0x201c,
	    "Port Id = %x.\n", eiter->a.port_id);

	/* Update MS request size. */
	qla2x00_update_ms_fdmi_iocb(vha, size + 16);

	ql_dbg(ql_dbg_disc, vha, 0x2018,
	    "RPA portname= %8phN size=%d.\n", ct_req->req.rpa.port_name, size);
	ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x20ca,
	    entries, size);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_disc, vha, 0x20cb,
		    "RPA FDMI v2 issue IOCB failed (%d).\n", rval);
	} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp, "RPA") !=
	    QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
		if (ct_rsp->header.reason_code == CT_REASON_CANNOT_PERFORM &&
		    ct_rsp->header.explanation_code ==
		    CT_EXPL_ALREADY_REGISTERED) {
			ql_dbg(ql_dbg_disc, vha, 0x20ce,
			    "RPA FDMI v2 already registered\n");
			rval = QLA_ALREADY_REGISTERED;
		} else {
			ql_dbg(ql_dbg_disc, vha, 0x2020,
			    "RPA FDMI v2 failed, CT Reason code: 0x%x, CT Explanation 0x%x\n",
			    ct_rsp->header.reason_code,
			    ct_rsp->header.explanation_code);
		}
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x20cc,
		    "RPA FDMI V2 exiting normally.\n");
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
qla2x00_fdmi_register(scsi_qla_host_t *vha)
{
	int rval = QLA_FUNCTION_FAILED;
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLA2100(ha) || IS_QLA2200(ha) ||
	    IS_QLAFX00(ha))
		return QLA_FUNCTION_FAILED;

	rval = qla2x00_mgmt_svr_login(vha);
	if (rval)
		return rval;

	rval = qla2x00_fdmiv2_rhba(vha);
	if (rval) {
		if (rval != QLA_ALREADY_REGISTERED)
			goto try_fdmi;

		rval = qla2x00_fdmi_dhba(vha);
		if (rval)
			goto try_fdmi;

		rval = qla2x00_fdmiv2_rhba(vha);
		if (rval)
			goto try_fdmi;
	}
	rval = qla2x00_fdmiv2_rpa(vha);
	if (rval)
		goto try_fdmi;

	goto out;

try_fdmi:
	rval = qla2x00_fdmi_rhba(vha);
	if (rval) {
		if (rval != QLA_ALREADY_REGISTERED)
			return rval;

		rval = qla2x00_fdmi_dhba(vha);
		if (rval)
			return rval;

		rval = qla2x00_fdmi_rhba(vha);
		if (rval)
			return rval;
	}
	rval = qla2x00_fdmi_rpa(vha);
out:
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
qla2x00_gfpn_id(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval = QLA_SUCCESS;
	uint16_t	i;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (!IS_IIDMA_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = GFPN_ID_REQ_SIZE;
	arg.rsp_size = GFPN_ID_RSP_SIZE;
	arg.nport_handle = NPH_SNS;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Issue GFPN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(ha->ct_sns, GFPN_ID_CMD,
		    GFPN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			ql_dbg(ql_dbg_disc, vha, 0x2023,
			    "GFPN_ID issue IOCB failed (%d).\n", rval);
			break;
		} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp,
		    "GFPN_ID") != QLA_SUCCESS) {
			rval = QLA_FUNCTION_FAILED;
			break;
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


static inline struct ct_sns_req *
qla24xx_prep_ct_fm_req(struct ct_sns_pkt *p, uint16_t cmd,
    uint16_t rsp_size)
{
	memset(p, 0, sizeof(struct ct_sns_pkt));

	p->p.req.header.revision = 0x01;
	p->p.req.header.gs_type = 0xFA;
	p->p.req.header.gs_subtype = 0x01;
	p->p.req.command = cpu_to_be16(cmd);
	p->p.req.max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return &p->p.req;
}

/**
 * qla2x00_gpsc() - FCS Get Port Speed Capabilities (GPSC) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gpsc(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;
	struct qla_hw_data *ha = vha->hw;
	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct ct_arg arg;

	if (!IS_IIDMA_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;
	if (!ha->flags.gpsc_supported)
		return QLA_FUNCTION_FAILED;

	rval = qla2x00_mgmt_svr_login(vha);
	if (rval)
		return rval;

	arg.iocb = ha->ms_iocb;
	arg.req_dma = ha->ct_sns_dma;
	arg.rsp_dma = ha->ct_sns_dma;
	arg.req_size = GPSC_REQ_SIZE;
	arg.rsp_size = GPSC_RSP_SIZE;
	arg.nport_handle = vha->mgmt_svr_loop_id;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Issue GFPN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = qla24xx_prep_ms_iocb(vha, &arg);

		/* Prepare CT request */
		ct_req = qla24xx_prep_ct_fm_req(ha->ct_sns, GPSC_CMD,
		    GPSC_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_name */
		memcpy(ct_req->req.gpsc.port_name, list[i].fabric_port_name,
		    WWN_SIZE);

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			ql_dbg(ql_dbg_disc, vha, 0x2059,
			    "GPSC issue IOCB failed (%d).\n", rval);
		} else if ((rval = qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp,
		    "GPSC")) != QLA_SUCCESS) {
			/* FM command unsupported? */
			if (rval == QLA_INVALID_COMMAND &&
			    (ct_rsp->header.reason_code ==
				CT_REASON_INVALID_COMMAND_CODE ||
			     ct_rsp->header.reason_code ==
				CT_REASON_COMMAND_UNSUPPORTED)) {
				ql_dbg(ql_dbg_disc, vha, 0x205a,
				    "GPSC command unsupported, disabling "
				    "query.\n");
				ha->flags.gpsc_supported = 0;
				rval = QLA_FUNCTION_FAILED;
				break;
			}
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save port-speed */
			switch (be16_to_cpu(ct_rsp->rsp.gpsc.speed)) {
			case BIT_15:
				list[i].fp_speed = PORT_SPEED_1GB;
				break;
			case BIT_14:
				list[i].fp_speed = PORT_SPEED_2GB;
				break;
			case BIT_13:
				list[i].fp_speed = PORT_SPEED_4GB;
				break;
			case BIT_12:
				list[i].fp_speed = PORT_SPEED_10GB;
				break;
			case BIT_11:
				list[i].fp_speed = PORT_SPEED_8GB;
				break;
			case BIT_10:
				list[i].fp_speed = PORT_SPEED_16GB;
				break;
			case BIT_8:
				list[i].fp_speed = PORT_SPEED_32GB;
				break;
			}

			ql_dbg(ql_dbg_disc, vha, 0x205b,
			    "GPSC ext entry - fpn "
			    "%8phN speeds=%04x speed=%04x.\n",
			    list[i].fabric_port_name,
			    be16_to_cpu(ct_rsp->rsp.gpsc.speeds),
			    be16_to_cpu(ct_rsp->rsp.gpsc.speed));
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_gff_id() - SNS Get FC-4 Features (GFF_ID) query.
 *
 * @ha: HA context
 * @list: switch info entries to populate
 *
 */
void
qla2x00_gff_id(scsi_qla_host_t *vha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;
	struct qla_hw_data *ha = vha->hw;
	uint8_t fcp_scsi_features = 0;
	struct ct_arg arg;

	for (i = 0; i < ha->max_fibre_devices; i++) {
		/* Set default FC4 Type as UNKNOWN so the default is to
		 * Process this port */
		list[i].fc4_type = FC4_TYPE_UNKNOWN;

		/* Do not attempt GFF_ID if we are not FWI_2 capable */
		if (!IS_FWI2_CAPABLE(ha))
			continue;

		arg.iocb = ha->ms_iocb;
		arg.req_dma = ha->ct_sns_dma;
		arg.rsp_dma = ha->ct_sns_dma;
		arg.req_size = GFF_ID_REQ_SIZE;
		arg.rsp_size = GFF_ID_RSP_SIZE;
		arg.nport_handle = NPH_SNS;

		/* Prepare common MS IOCB */
		ms_pkt = ha->isp_ops->prep_ms_iocb(vha, &arg);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(ha->ct_sns, GFF_ID_CMD,
		    GFF_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(vha, ha->ms_iocb, ha->ms_iocb_dma,
		   sizeof(ms_iocb_entry_t));

		if (rval != QLA_SUCCESS) {
			ql_dbg(ql_dbg_disc, vha, 0x205c,
			    "GFF_ID issue IOCB failed (%d).\n", rval);
		} else if (qla2x00_chk_ms_status(vha, ms_pkt, ct_rsp,
			       "GFF_ID") != QLA_SUCCESS) {
			ql_dbg(ql_dbg_disc, vha, 0x205d,
			    "GFF_ID IOCB status had a failure status code.\n");
		} else {
			fcp_scsi_features =
			   ct_rsp->rsp.gff_id.fc4_features[GFF_FCP_SCSI_OFFSET];
			fcp_scsi_features &= 0x0f;

			if (fcp_scsi_features)
				list[i].fc4_type = FC4_TYPE_FCP_SCSI;
			else
				list[i].fc4_type = FC4_TYPE_OTHER;

			list[i].fc4f_nvme =
			    ct_rsp->rsp.gff_id.fc4_features[GFF_NVME_OFFSET];
			list[i].fc4f_nvme &= 0xf;
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}
}

/* GID_PN completion processing. */
void qla24xx_handle_gidpn_event(scsi_qla_host_t *vha, struct event_arg *ea)
{
	fc_port_t *fcport = ea->fcport;

	ql_dbg(ql_dbg_disc, vha, 0x201d,
	    "%s %8phC login state %d\n",
	    __func__, fcport->port_name, fcport->fw_login_state);

	if (ea->sp->gen2 != fcport->login_gen) {
		/* PLOGI/PRLI/LOGO came in while cmd was out.*/
		ql_dbg(ql_dbg_disc, vha, 0x201e,
		    "%s %8phC generation changed rscn %d|%d login %d|%d \n",
		    __func__, fcport->port_name, fcport->last_rscn_gen,
		    fcport->rscn_gen, fcport->last_login_gen, fcport->login_gen);
		return;
	}

	if (!ea->rc) {
		if (ea->sp->gen1 == fcport->rscn_gen) {
			fcport->scan_state = QLA_FCPORT_FOUND;
			fcport->flags |= FCF_FABRIC_DEVICE;

			if (fcport->d_id.b24 == ea->id.b24) {
				/* cable plugged into the same place */
				switch (vha->host->active_mode) {
				case MODE_TARGET:
					/* NOOP. let the other guy login to us.*/
					break;
				case MODE_INITIATOR:
				case MODE_DUAL:
				default:
					ql_dbg(ql_dbg_disc, vha, 0x201f,
					    "%s %d %8phC post %s\n", __func__,
					    __LINE__, fcport->port_name,
					    (atomic_read(&fcport->state) ==
					    FCS_ONLINE) ? "gpdb" : "gnl");

					if (atomic_read(&fcport->state) ==
					    FCS_ONLINE)
						qla24xx_post_gpdb_work(vha,
						    fcport, PDO_FORCE_ADISC);
					else
						qla24xx_post_gnl_work(vha,
						    fcport);
					break;
				}
			} else { /* fcport->d_id.b24 != ea->id.b24 */
				fcport->d_id.b24 = ea->id.b24;
				if (fcport->deleted == QLA_SESS_DELETED) {
					ql_dbg(ql_dbg_disc, vha, 0x2021,
					    "%s %d %8phC post del sess\n",
					    __func__, __LINE__, fcport->port_name);
					qlt_schedule_sess_for_deletion_lock(fcport);
				}
			}
		} else { /* ea->sp->gen1 != fcport->rscn_gen */
			ql_dbg(ql_dbg_disc, vha, 0x2022,
			    "%s %d %8phC post gidpn\n",
			    __func__, __LINE__, fcport->port_name);
			/* rscn came in while cmd was out */
			qla24xx_post_gidpn_work(vha, fcport);
		}
	} else { /* ea->rc */
		/* cable pulled */
		if (ea->sp->gen1 == fcport->rscn_gen) {
			if (ea->sp->gen2 == fcport->login_gen) {
				ql_dbg(ql_dbg_disc, vha, 0x2042,
				    "%s %d %8phC post del sess\n", __func__,
				    __LINE__, fcport->port_name);
				qlt_schedule_sess_for_deletion_lock(fcport);
			} else {
				ql_dbg(ql_dbg_disc, vha, 0x2045,
				    "%s %d %8phC login\n", __func__, __LINE__,
				    fcport->port_name);
				qla24xx_fcport_handle_login(vha, fcport);
			}
		} else {
			ql_dbg(ql_dbg_disc, vha, 0x2049,
			    "%s %d %8phC post gidpn\n", __func__, __LINE__,
			    fcport->port_name);
			qla24xx_post_gidpn_work(vha, fcport);
		}
	}
} /* gidpn_event */

static void qla2x00_async_gidpn_sp_done(void *s, int res)
{
	struct srb *sp = s;
	struct scsi_qla_host *vha = sp->vha;
	fc_port_t *fcport = sp->fcport;
	u8 *id = fcport->ct_desc.ct_sns->p.rsp.rsp.gid_pn.port_id;
	struct event_arg ea;

	fcport->flags &= ~FCF_ASYNC_SENT;

	memset(&ea, 0, sizeof(ea));
	ea.fcport = fcport;
	ea.id.b.domain = id[0];
	ea.id.b.area = id[1];
	ea.id.b.al_pa = id[2];
	ea.sp = sp;
	ea.rc = res;
	ea.event = FCME_GIDPN_DONE;

	ql_dbg(ql_dbg_disc, vha, 0x204f,
	    "Async done-%s res %x, WWPN %8phC ID %3phC \n",
	    sp->name, res, fcport->port_name, id);

	qla2x00_fcport_event_handler(vha, &ea);

	sp->free(sp);
}

int qla24xx_async_gidpn(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int rval = QLA_FUNCTION_FAILED;
	struct ct_sns_req       *ct_req;
	srb_t *sp;

	if (!vha->flags.online)
		goto done;

	fcport->flags |= FCF_ASYNC_SENT;
	fcport->disc_state = DSC_GID_PN;
	fcport->scan_state = QLA_FCPORT_SCAN;
	sp = qla2x00_get_sp(vha, fcport, GFP_ATOMIC);
	if (!sp)
		goto done;

	sp->type = SRB_CT_PTHRU_CMD;
	sp->name = "gidpn";
	sp->gen1 = fcport->rscn_gen;
	sp->gen2 = fcport->login_gen;

	qla2x00_init_timer(sp, qla2x00_get_async_timeout(vha) + 2);

	/* CT_IU preamble  */
	ct_req = qla2x00_prep_ct_req(fcport->ct_desc.ct_sns, GID_PN_CMD,
		GID_PN_RSP_SIZE);

	/* GIDPN req */
	memcpy(ct_req->req.gid_pn.port_name, fcport->port_name,
		WWN_SIZE);

	/* req & rsp use the same buffer */
	sp->u.iocb_cmd.u.ctarg.req = fcport->ct_desc.ct_sns;
	sp->u.iocb_cmd.u.ctarg.req_dma = fcport->ct_desc.ct_sns_dma;
	sp->u.iocb_cmd.u.ctarg.rsp = fcport->ct_desc.ct_sns;
	sp->u.iocb_cmd.u.ctarg.rsp_dma = fcport->ct_desc.ct_sns_dma;
	sp->u.iocb_cmd.u.ctarg.req_size = GID_PN_REQ_SIZE;
	sp->u.iocb_cmd.u.ctarg.rsp_size = GID_PN_RSP_SIZE;
	sp->u.iocb_cmd.u.ctarg.nport_handle = NPH_SNS;

	sp->u.iocb_cmd.timeout = qla2x00_async_iocb_timeout;
	sp->done = qla2x00_async_gidpn_sp_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	ql_dbg(ql_dbg_disc, vha, 0x20a4,
	    "Async-%s - %8phC hdl=%x loopid=%x portid %02x%02x%02x.\n",
	    sp->name, fcport->port_name,
	    sp->handle, fcport->loop_id, fcport->d_id.b.domain,
	    fcport->d_id.b.area, fcport->d_id.b.al_pa);
	return rval;

done_free_sp:
	sp->free(sp);
done:
	fcport->flags &= ~FCF_ASYNC_SENT;
	return rval;
}

int qla24xx_post_gidpn_work(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_work_evt *e;
	int ls;

	ls = atomic_read(&vha->loop_state);
	if (((ls != LOOP_READY) && (ls != LOOP_UP)) ||
		test_bit(UNLOADING, &vha->dpc_flags))
		return 0;

	e = qla2x00_alloc_work(vha, QLA_EVT_GIDPN);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.fcport.fcport = fcport;
	return qla2x00_post_work(vha, e);
}

int qla24xx_post_gpsc_work(struct scsi_qla_host *vha, fc_port_t *fcport)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(vha, QLA_EVT_GPSC);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.fcport.fcport = fcport;
	return qla2x00_post_work(vha, e);
}

static void qla24xx_async_gpsc_sp_done(void *s, int res)
{
	struct srb *sp = s;
	struct scsi_qla_host *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;
	fc_port_t *fcport = sp->fcport;
	struct ct_sns_rsp       *ct_rsp;
	struct event_arg ea;

	ct_rsp = &fcport->ct_desc.ct_sns->p.rsp;

	ql_dbg(ql_dbg_disc, vha, 0x2053,
	    "Async done-%s res %x, WWPN %8phC \n",
	    sp->name, res, fcport->port_name);

	fcport->flags &= ~FCF_ASYNC_SENT;

	if (res == (DID_ERROR << 16)) {
		/* entry status error */
		goto done;
	} else if (res) {
		if ((ct_rsp->header.reason_code ==
			 CT_REASON_INVALID_COMMAND_CODE) ||
			(ct_rsp->header.reason_code ==
			CT_REASON_COMMAND_UNSUPPORTED)) {
			ql_dbg(ql_dbg_disc, vha, 0x2019,
			    "GPSC command unsupported, disabling query.\n");
			ha->flags.gpsc_supported = 0;
			res = QLA_SUCCESS;
		}
	} else {
		switch (be16_to_cpu(ct_rsp->rsp.gpsc.speed)) {
		case BIT_15:
			fcport->fp_speed = PORT_SPEED_1GB;
			break;
		case BIT_14:
			fcport->fp_speed = PORT_SPEED_2GB;
			break;
		case BIT_13:
			fcport->fp_speed = PORT_SPEED_4GB;
			break;
		case BIT_12:
			fcport->fp_speed = PORT_SPEED_10GB;
			break;
		case BIT_11:
			fcport->fp_speed = PORT_SPEED_8GB;
			break;
		case BIT_10:
			fcport->fp_speed = PORT_SPEED_16GB;
			break;
		case BIT_8:
			fcport->fp_speed = PORT_SPEED_32GB;
			break;
		}

		ql_dbg(ql_dbg_disc, vha, 0x2054,
		    "Async-%s OUT WWPN %8phC speeds=%04x speed=%04x.\n",
		    sp->name, fcport->fabric_port_name,
		    be16_to_cpu(ct_rsp->rsp.gpsc.speeds),
		    be16_to_cpu(ct_rsp->rsp.gpsc.speed));
	}
done:
	memset(&ea, 0, sizeof(ea));
	ea.event = FCME_GPSC_DONE;
	ea.rc = res;
	ea.fcport = fcport;
	qla2x00_fcport_event_handler(vha, &ea);

	sp->free(sp);
}

int qla24xx_async_gpsc(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int rval = QLA_FUNCTION_FAILED;
	struct ct_sns_req       *ct_req;
	srb_t *sp;

	if (!vha->flags.online)
		goto done;

	fcport->flags |= FCF_ASYNC_SENT;
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp)
		goto done;

	sp->type = SRB_CT_PTHRU_CMD;
	sp->name = "gpsc";
	sp->gen1 = fcport->rscn_gen;
	sp->gen2 = fcport->login_gen;

	qla2x00_init_timer(sp, qla2x00_get_async_timeout(vha) + 2);

	/* CT_IU preamble  */
	ct_req = qla24xx_prep_ct_fm_req(fcport->ct_desc.ct_sns, GPSC_CMD,
		GPSC_RSP_SIZE);

	/* GPSC req */
	memcpy(ct_req->req.gpsc.port_name, fcport->fabric_port_name,
		WWN_SIZE);

	sp->u.iocb_cmd.u.ctarg.req = fcport->ct_desc.ct_sns;
	sp->u.iocb_cmd.u.ctarg.req_dma = fcport->ct_desc.ct_sns_dma;
	sp->u.iocb_cmd.u.ctarg.rsp = fcport->ct_desc.ct_sns;
	sp->u.iocb_cmd.u.ctarg.rsp_dma = fcport->ct_desc.ct_sns_dma;
	sp->u.iocb_cmd.u.ctarg.req_size = GPSC_REQ_SIZE;
	sp->u.iocb_cmd.u.ctarg.rsp_size = GPSC_RSP_SIZE;
	sp->u.iocb_cmd.u.ctarg.nport_handle = vha->mgmt_svr_loop_id;

	sp->u.iocb_cmd.timeout = qla2x00_async_iocb_timeout;
	sp->done = qla24xx_async_gpsc_sp_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	ql_dbg(ql_dbg_disc, vha, 0x205e,
	    "Async-%s %8phC hdl=%x loopid=%x portid=%02x%02x%02x.\n",
	    sp->name, fcport->port_name, sp->handle,
	    fcport->loop_id, fcport->d_id.b.domain,
	    fcport->d_id.b.area, fcport->d_id.b.al_pa);
	return rval;

done_free_sp:
	sp->free(sp);
done:
	fcport->flags &= ~FCF_ASYNC_SENT;
	return rval;
}

int qla24xx_post_gpnid_work(struct scsi_qla_host *vha, port_id_t *id)
{
	struct qla_work_evt *e;

	if (test_bit(UNLOADING, &vha->dpc_flags))
		return 0;

	e = qla2x00_alloc_work(vha, QLA_EVT_GPNID);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.gpnid.id = *id;
	return qla2x00_post_work(vha, e);
}

void qla24xx_async_gpnid_done(scsi_qla_host_t *vha, srb_t *sp)
{
	if (sp->u.iocb_cmd.u.ctarg.req) {
		dma_free_coherent(&vha->hw->pdev->dev,
			sizeof(struct ct_sns_pkt),
			sp->u.iocb_cmd.u.ctarg.req,
			sp->u.iocb_cmd.u.ctarg.req_dma);
		sp->u.iocb_cmd.u.ctarg.req = NULL;
	}
	if (sp->u.iocb_cmd.u.ctarg.rsp) {
		dma_free_coherent(&vha->hw->pdev->dev,
			sizeof(struct ct_sns_pkt),
			sp->u.iocb_cmd.u.ctarg.rsp,
			sp->u.iocb_cmd.u.ctarg.rsp_dma);
		sp->u.iocb_cmd.u.ctarg.rsp = NULL;
	}

	sp->free(sp);
}

void qla24xx_handle_gpnid_event(scsi_qla_host_t *vha, struct event_arg *ea)
{
	fc_port_t *fcport;
	unsigned long flags;

	spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
	fcport = qla2x00_find_fcport_by_wwpn(vha, ea->port_name, 1);
	spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);

	if (fcport) {
		/* cable moved. just plugged in */
		fcport->rscn_gen++;
		fcport->d_id = ea->id;
		fcport->scan_state = QLA_FCPORT_FOUND;
		fcport->flags |= FCF_FABRIC_DEVICE;

		switch (fcport->disc_state) {
		case DSC_DELETED:
			ql_dbg(ql_dbg_disc, vha, 0x210d,
			    "%s %d %8phC login\n", __func__, __LINE__,
			    fcport->port_name);
			qla24xx_fcport_handle_login(vha, fcport);
			break;
		case DSC_DELETE_PEND:
			break;
		default:
			ql_dbg(ql_dbg_disc, vha, 0x2064,
			    "%s %d %8phC post del sess\n",
			    __func__, __LINE__, fcport->port_name);
			qlt_schedule_sess_for_deletion_lock(fcport);
			break;
		}
	} else {
		/* create new fcport */
		ql_dbg(ql_dbg_disc, vha, 0x2065,
		    "%s %d %8phC post new sess\n",
		    __func__, __LINE__, ea->port_name);

		qla24xx_post_newsess_work(vha, &ea->id, ea->port_name, NULL);
	}
}

static void qla2x00_async_gpnid_sp_done(void *s, int res)
{
	struct srb *sp = s;
	struct scsi_qla_host *vha = sp->vha;
	struct ct_sns_req *ct_req =
	    (struct ct_sns_req *)sp->u.iocb_cmd.u.ctarg.req;
	struct ct_sns_rsp *ct_rsp =
	    (struct ct_sns_rsp *)sp->u.iocb_cmd.u.ctarg.rsp;
	struct event_arg ea;
	struct qla_work_evt *e;

	ql_dbg(ql_dbg_disc, vha, 0x2066,
	    "Async done-%s res %x ID %3phC. %8phC\n",
	    sp->name, res, ct_req->req.port_id.port_id,
	    ct_rsp->rsp.gpn_id.port_name);

	if (res) {
		sp->free(sp);
		return;
	}

	memset(&ea, 0, sizeof(ea));
	memcpy(ea.port_name, ct_rsp->rsp.gpn_id.port_name, WWN_SIZE);
	ea.sp = sp;
	ea.id.b.domain = ct_req->req.port_id.port_id[0];
	ea.id.b.area = ct_req->req.port_id.port_id[1];
	ea.id.b.al_pa = ct_req->req.port_id.port_id[2];
	ea.rc = res;
	ea.event = FCME_GPNID_DONE;

	qla2x00_fcport_event_handler(vha, &ea);

	e = qla2x00_alloc_work(vha, QLA_EVT_GPNID_DONE);
	if (!e) {
		/* please ignore kernel warning. otherwise, we have mem leak. */
		if (sp->u.iocb_cmd.u.ctarg.req) {
			dma_free_coherent(&vha->hw->pdev->dev,
				sizeof(struct ct_sns_pkt),
				sp->u.iocb_cmd.u.ctarg.req,
				sp->u.iocb_cmd.u.ctarg.req_dma);
			sp->u.iocb_cmd.u.ctarg.req = NULL;
		}
		if (sp->u.iocb_cmd.u.ctarg.rsp) {
			dma_free_coherent(&vha->hw->pdev->dev,
				sizeof(struct ct_sns_pkt),
				sp->u.iocb_cmd.u.ctarg.rsp,
				sp->u.iocb_cmd.u.ctarg.rsp_dma);
			sp->u.iocb_cmd.u.ctarg.rsp = NULL;
		}

		sp->free(sp);
		return;
	}

	e->u.iosb.sp = sp;
	qla2x00_post_work(vha, e);
}

/* Get WWPN with Nport ID. */
int qla24xx_async_gpnid(scsi_qla_host_t *vha, port_id_t *id)
{
	int rval = QLA_FUNCTION_FAILED;
	struct ct_sns_req       *ct_req;
	srb_t *sp;
	struct ct_sns_pkt *ct_sns;

	if (!vha->flags.online)
		goto done;

	sp = qla2x00_get_sp(vha, NULL, GFP_KERNEL);
	if (!sp)
		goto done;

	sp->type = SRB_CT_PTHRU_CMD;
	sp->name = "gpnid";
	qla2x00_init_timer(sp, qla2x00_get_async_timeout(vha) + 2);

	sp->u.iocb_cmd.u.ctarg.req = dma_alloc_coherent(&vha->hw->pdev->dev,
		sizeof(struct ct_sns_pkt), &sp->u.iocb_cmd.u.ctarg.req_dma,
		GFP_KERNEL);
	if (!sp->u.iocb_cmd.u.ctarg.req) {
		ql_log(ql_log_warn, vha, 0xd041,
		    "Failed to allocate ct_sns request.\n");
		goto done_free_sp;
	}

	sp->u.iocb_cmd.u.ctarg.rsp = dma_alloc_coherent(&vha->hw->pdev->dev,
		sizeof(struct ct_sns_pkt), &sp->u.iocb_cmd.u.ctarg.rsp_dma,
		GFP_KERNEL);
	if (!sp->u.iocb_cmd.u.ctarg.rsp) {
		ql_log(ql_log_warn, vha, 0xd042,
		    "Failed to allocate ct_sns request.\n");
		goto done_free_sp;
	}

	ct_sns = (struct ct_sns_pkt *)sp->u.iocb_cmd.u.ctarg.rsp;
	memset(ct_sns, 0, sizeof(*ct_sns));

	ct_sns = (struct ct_sns_pkt *)sp->u.iocb_cmd.u.ctarg.req;
	/* CT_IU preamble  */
	ct_req = qla2x00_prep_ct_req(ct_sns, GPN_ID_CMD, GPN_ID_RSP_SIZE);

	/* GPN_ID req */
	ct_req->req.port_id.port_id[0] = id->b.domain;
	ct_req->req.port_id.port_id[1] = id->b.area;
	ct_req->req.port_id.port_id[2] = id->b.al_pa;

	sp->u.iocb_cmd.u.ctarg.req_size = GPN_ID_REQ_SIZE;
	sp->u.iocb_cmd.u.ctarg.rsp_size = GPN_ID_RSP_SIZE;
	sp->u.iocb_cmd.u.ctarg.nport_handle = NPH_SNS;

	sp->u.iocb_cmd.timeout = qla2x00_async_iocb_timeout;
	sp->done = qla2x00_async_gpnid_sp_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	ql_dbg(ql_dbg_disc, vha, 0x2067,
	    "Async-%s hdl=%x ID %3phC.\n", sp->name,
	    sp->handle, ct_req->req.port_id.port_id);
	return rval;

done_free_sp:
	if (sp->u.iocb_cmd.u.ctarg.req) {
		dma_free_coherent(&vha->hw->pdev->dev,
			sizeof(struct ct_sns_pkt),
			sp->u.iocb_cmd.u.ctarg.req,
			sp->u.iocb_cmd.u.ctarg.req_dma);
		sp->u.iocb_cmd.u.ctarg.req = NULL;
	}
	if (sp->u.iocb_cmd.u.ctarg.rsp) {
		dma_free_coherent(&vha->hw->pdev->dev,
			sizeof(struct ct_sns_pkt),
			sp->u.iocb_cmd.u.ctarg.rsp,
			sp->u.iocb_cmd.u.ctarg.rsp_dma);
		sp->u.iocb_cmd.u.ctarg.rsp = NULL;
	}

	sp->free(sp);
done:
	return rval;
}

void qla24xx_handle_gffid_event(scsi_qla_host_t *vha, struct event_arg *ea)
{
       fc_port_t *fcport = ea->fcport;

       qla24xx_post_gnl_work(vha, fcport);
}

void qla24xx_async_gffid_sp_done(void *s, int res)
{
       struct srb *sp = s;
       struct scsi_qla_host *vha = sp->vha;
       fc_port_t *fcport = sp->fcport;
       struct ct_sns_rsp *ct_rsp;
       struct event_arg ea;

       ql_dbg(ql_dbg_disc, vha, 0x2133,
	   "Async done-%s res %x ID %x. %8phC\n",
	   sp->name, res, fcport->d_id.b24, fcport->port_name);

       fcport->flags &= ~FCF_ASYNC_SENT;
       ct_rsp = &fcport->ct_desc.ct_sns->p.rsp;
       /*
	* FC-GS-7, 5.2.3.12 FC-4 Features - format
	* The format of the FC-4 Features object, as defined by the FC-4,
	* Shall be an array of 4-bit values, one for each type code value
	*/
       if (!res) {
	       if (ct_rsp->rsp.gff_id.fc4_features[GFF_FCP_SCSI_OFFSET] & 0xf) {
		       /* w1 b00:03 */
		       fcport->fc4_type =
			   ct_rsp->rsp.gff_id.fc4_features[GFF_FCP_SCSI_OFFSET];
		       fcport->fc4_type &= 0xf;
	       }

	       if (ct_rsp->rsp.gff_id.fc4_features[GFF_NVME_OFFSET] & 0xf) {
		       /* w5 [00:03]/28h */
		       fcport->fc4f_nvme =
			   ct_rsp->rsp.gff_id.fc4_features[GFF_NVME_OFFSET];
		       fcport->fc4f_nvme &= 0xf;
	       }
       }

       memset(&ea, 0, sizeof(ea));
       ea.sp = sp;
       ea.fcport = sp->fcport;
       ea.rc = res;
       ea.event = FCME_GFFID_DONE;

       qla2x00_fcport_event_handler(vha, &ea);
       sp->free(sp);
}

/* Get FC4 Feature with Nport ID. */
int qla24xx_async_gffid(scsi_qla_host_t *vha, fc_port_t *fcport)
{
	int rval = QLA_FUNCTION_FAILED;
	struct ct_sns_req       *ct_req;
	srb_t *sp;

	if (!vha->flags.online)
		return rval;

	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp)
		return rval;

	fcport->flags |= FCF_ASYNC_SENT;
	sp->type = SRB_CT_PTHRU_CMD;
	sp->name = "gffid";
	sp->gen1 = fcport->rscn_gen;
	sp->gen2 = fcport->login_gen;

	qla2x00_init_timer(sp, qla2x00_get_async_timeout(vha) + 2);

	/* CT_IU preamble  */
	ct_req = qla2x00_prep_ct_req(fcport->ct_desc.ct_sns, GFF_ID_CMD,
	    GFF_ID_RSP_SIZE);

	ct_req->req.gff_id.port_id[0] = fcport->d_id.b.domain;
	ct_req->req.gff_id.port_id[1] = fcport->d_id.b.area;
	ct_req->req.gff_id.port_id[2] = fcport->d_id.b.al_pa;

	sp->u.iocb_cmd.u.ctarg.req = fcport->ct_desc.ct_sns;
	sp->u.iocb_cmd.u.ctarg.req_dma = fcport->ct_desc.ct_sns_dma;
	sp->u.iocb_cmd.u.ctarg.rsp = fcport->ct_desc.ct_sns;
	sp->u.iocb_cmd.u.ctarg.rsp_dma = fcport->ct_desc.ct_sns_dma;
	sp->u.iocb_cmd.u.ctarg.req_size = GFF_ID_REQ_SIZE;
	sp->u.iocb_cmd.u.ctarg.rsp_size = GFF_ID_RSP_SIZE;
	sp->u.iocb_cmd.u.ctarg.nport_handle = NPH_SNS;

	sp->u.iocb_cmd.timeout = qla2x00_async_iocb_timeout;
	sp->done = qla24xx_async_gffid_sp_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS)
		goto done_free_sp;

	ql_dbg(ql_dbg_disc, vha, 0x2132,
	    "Async-%s hdl=%x  %8phC.\n", sp->name,
	    sp->handle, fcport->port_name);

	return rval;
done_free_sp:
	sp->free(sp);
	fcport->flags &= ~FCF_ASYNC_SENT;
	return rval;
}

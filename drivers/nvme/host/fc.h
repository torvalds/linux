/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016, Avago Technologies
 */

#ifndef _NVME_FC_TRANSPORT_H
#define _NVME_FC_TRANSPORT_H 1


/*
 * Common definitions between the nvme_fc (host) transport and
 * nvmet_fc (target) transport implementation.
 */

/*
 * ******************  FC-NVME LS HANDLING ******************
 */

union nvmefc_ls_requests {
	struct fcnvme_ls_rqst_w0		w0;
	struct fcnvme_ls_cr_assoc_rqst		rq_cr_assoc;
	struct fcnvme_ls_cr_conn_rqst		rq_cr_conn;
	struct fcnvme_ls_disconnect_assoc_rqst	rq_dis_assoc;
	struct fcnvme_ls_disconnect_conn_rqst	rq_dis_conn;
} __aligned(128);	/* alignment for other things alloc'd with */

union nvmefc_ls_responses {
	struct fcnvme_ls_rjt			rsp_rjt;
	struct fcnvme_ls_cr_assoc_acc		rsp_cr_assoc;
	struct fcnvme_ls_cr_conn_acc		rsp_cr_conn;
	struct fcnvme_ls_disconnect_assoc_acc	rsp_dis_assoc;
	struct fcnvme_ls_disconnect_conn_acc	rsp_dis_conn;
} __aligned(128);	/* alignment for other things alloc'd with */

static inline void
nvme_fc_format_rsp_hdr(void *buf, u8 ls_cmd, __be32 desc_len, u8 rqst_ls_cmd)
{
	struct fcnvme_ls_acc_hdr *acc = buf;

	acc->w0.ls_cmd = ls_cmd;
	acc->desc_list_len = desc_len;
	acc->rqst.desc_tag = cpu_to_be32(FCNVME_LSDESC_RQST);
	acc->rqst.desc_len =
			fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rqst));
	acc->rqst.w0.ls_cmd = rqst_ls_cmd;
}

static inline int
nvme_fc_format_rjt(void *buf, u16 buflen, u8 ls_cmd,
			u8 reason, u8 explanation, u8 vendor)
{
	struct fcnvme_ls_rjt *rjt = buf;

	nvme_fc_format_rsp_hdr(buf, FCNVME_LSDESC_RQST,
			fcnvme_lsdesc_len(sizeof(struct fcnvme_ls_rjt)),
			ls_cmd);
	rjt->rjt.desc_tag = cpu_to_be32(FCNVME_LSDESC_RJT);
	rjt->rjt.desc_len = fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rjt));
	rjt->rjt.reason_code = reason;
	rjt->rjt.reason_explanation = explanation;
	rjt->rjt.vendor = vendor;

	return sizeof(struct fcnvme_ls_rjt);
}

/* Validation Error indexes into the string table below */
enum {
	VERR_NO_ERROR		= 0,
	VERR_CR_ASSOC_LEN	= 1,
	VERR_CR_ASSOC_RQST_LEN	= 2,
	VERR_CR_ASSOC_CMD	= 3,
	VERR_CR_ASSOC_CMD_LEN	= 4,
	VERR_ERSP_RATIO		= 5,
	VERR_ASSOC_ALLOC_FAIL	= 6,
	VERR_QUEUE_ALLOC_FAIL	= 7,
	VERR_CR_CONN_LEN	= 8,
	VERR_CR_CONN_RQST_LEN	= 9,
	VERR_ASSOC_ID		= 10,
	VERR_ASSOC_ID_LEN	= 11,
	VERR_NO_ASSOC		= 12,
	VERR_CONN_ID		= 13,
	VERR_CONN_ID_LEN	= 14,
	VERR_INVAL_CONN		= 15,
	VERR_CR_CONN_CMD	= 16,
	VERR_CR_CONN_CMD_LEN	= 17,
	VERR_DISCONN_LEN	= 18,
	VERR_DISCONN_RQST_LEN	= 19,
	VERR_DISCONN_CMD	= 20,
	VERR_DISCONN_CMD_LEN	= 21,
	VERR_DISCONN_SCOPE	= 22,
	VERR_RS_LEN		= 23,
	VERR_RS_RQST_LEN	= 24,
	VERR_RS_CMD		= 25,
	VERR_RS_CMD_LEN		= 26,
	VERR_RS_RCTL		= 27,
	VERR_RS_RO		= 28,
	VERR_LSACC		= 29,
	VERR_LSDESC_RQST	= 30,
	VERR_LSDESC_RQST_LEN	= 31,
	VERR_CR_ASSOC		= 32,
	VERR_CR_ASSOC_ACC_LEN	= 33,
	VERR_CR_CONN		= 34,
	VERR_CR_CONN_ACC_LEN	= 35,
	VERR_DISCONN		= 36,
	VERR_DISCONN_ACC_LEN	= 37,
};

static char *validation_errors[] = {
	"OK",
	"Bad CR_ASSOC Length",
	"Bad CR_ASSOC Rqst Length",
	"Not CR_ASSOC Cmd",
	"Bad CR_ASSOC Cmd Length",
	"Bad Ersp Ratio",
	"Association Allocation Failed",
	"Queue Allocation Failed",
	"Bad CR_CONN Length",
	"Bad CR_CONN Rqst Length",
	"Not Association ID",
	"Bad Association ID Length",
	"No Association",
	"Not Connection ID",
	"Bad Connection ID Length",
	"Invalid Connection ID",
	"Not CR_CONN Cmd",
	"Bad CR_CONN Cmd Length",
	"Bad DISCONN Length",
	"Bad DISCONN Rqst Length",
	"Not DISCONN Cmd",
	"Bad DISCONN Cmd Length",
	"Bad Disconnect Scope",
	"Bad RS Length",
	"Bad RS Rqst Length",
	"Not RS Cmd",
	"Bad RS Cmd Length",
	"Bad RS R_CTL",
	"Bad RS Relative Offset",
	"Not LS_ACC",
	"Not LSDESC_RQST",
	"Bad LSDESC_RQST Length",
	"Not CR_ASSOC Rqst",
	"Bad CR_ASSOC ACC Length",
	"Not CR_CONN Rqst",
	"Bad CR_CONN ACC Length",
	"Not Disconnect Rqst",
	"Bad Disconnect ACC Length",
};

#define NVME_FC_LAST_LS_CMD_VALUE	FCNVME_LS_DISCONNECT_CONN

static char *nvmefc_ls_names[] = {
	"Reserved (0)",
	"RJT (1)",
	"ACC (2)",
	"Create Association",
	"Create Connection",
	"Disconnect Association",
	"Disconnect Connection",
};

static inline void
nvmefc_fmt_lsreq_discon_assoc(struct nvmefc_ls_req *lsreq,
	struct fcnvme_ls_disconnect_assoc_rqst *discon_rqst,
	struct fcnvme_ls_disconnect_assoc_acc *discon_acc,
	u64 association_id)
{
	lsreq->rqstaddr = discon_rqst;
	lsreq->rqstlen = sizeof(*discon_rqst);
	lsreq->rspaddr = discon_acc;
	lsreq->rsplen = sizeof(*discon_acc);
	lsreq->timeout = NVME_FC_LS_TIMEOUT_SEC;

	discon_rqst->w0.ls_cmd = FCNVME_LS_DISCONNECT_ASSOC;
	discon_rqst->desc_list_len = cpu_to_be32(
				sizeof(struct fcnvme_lsdesc_assoc_id) +
				sizeof(struct fcnvme_lsdesc_disconn_cmd));

	discon_rqst->associd.desc_tag = cpu_to_be32(FCNVME_LSDESC_ASSOC_ID);
	discon_rqst->associd.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id));

	discon_rqst->associd.association_id = cpu_to_be64(association_id);

	discon_rqst->discon_cmd.desc_tag = cpu_to_be32(
						FCNVME_LSDESC_DISCONN_CMD);
	discon_rqst->discon_cmd.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_disconn_cmd));
}

static inline int
nvmefc_vldt_lsreq_discon_assoc(u32 rqstlen,
	struct fcnvme_ls_disconnect_assoc_rqst *rqst)
{
	int ret = 0;

	if (rqstlen < sizeof(struct fcnvme_ls_disconnect_assoc_rqst))
		ret = VERR_DISCONN_LEN;
	else if (rqst->desc_list_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_disconnect_assoc_rqst)))
		ret = VERR_DISCONN_RQST_LEN;
	else if (rqst->associd.desc_tag != cpu_to_be32(FCNVME_LSDESC_ASSOC_ID))
		ret = VERR_ASSOC_ID;
	else if (rqst->associd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id)))
		ret = VERR_ASSOC_ID_LEN;
	else if (rqst->discon_cmd.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_DISCONN_CMD))
		ret = VERR_DISCONN_CMD;
	else if (rqst->discon_cmd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_disconn_cmd)))
		ret = VERR_DISCONN_CMD_LEN;
	/*
	 * As the standard changed on the LS, check if old format and scope
	 * something other than Association (e.g. 0).
	 */
	else if (rqst->discon_cmd.rsvd8[0])
		ret = VERR_DISCONN_SCOPE;

	return ret;
}

#endif /* _NVME_FC_TRANSPORT_H */

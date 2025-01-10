// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */
/*
 * fcbuild.c - FC link service frame building and parsing routines
 */

#include "bfad_drv.h"
#include "bfa_fcbuild.h"

/*
 * static build functions
 */
static void     fc_els_rsp_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
				 __be16 ox_id);
static void     fc_bls_rsp_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
				 __be16 ox_id);
static struct fchs_s fc_els_req_tmpl;
static struct fchs_s fc_els_rsp_tmpl;
static struct fchs_s fc_bls_req_tmpl;
static struct fchs_s fc_bls_rsp_tmpl;
static struct fc_ba_acc_s ba_acc_tmpl;
static struct fc_logi_s plogi_tmpl;
static struct fc_prli_s prli_tmpl;
static struct fc_rrq_s rrq_tmpl;
static struct fchs_s fcp_fchs_tmpl;

void
fcbuild_init(void)
{
	/*
	 * fc_els_req_tmpl
	 */
	fc_els_req_tmpl.routing = FC_RTG_EXT_LINK;
	fc_els_req_tmpl.cat_info = FC_CAT_LD_REQUEST;
	fc_els_req_tmpl.type = FC_TYPE_ELS;
	fc_els_req_tmpl.f_ctl =
		bfa_hton3b(FCTL_SEQ_INI | FCTL_FS_EXCH | FCTL_END_SEQ |
			      FCTL_SI_XFER);
	fc_els_req_tmpl.rx_id = FC_RXID_ANY;

	/*
	 * fc_els_rsp_tmpl
	 */
	fc_els_rsp_tmpl.routing = FC_RTG_EXT_LINK;
	fc_els_rsp_tmpl.cat_info = FC_CAT_LD_REPLY;
	fc_els_rsp_tmpl.type = FC_TYPE_ELS;
	fc_els_rsp_tmpl.f_ctl =
		bfa_hton3b(FCTL_EC_RESP | FCTL_SEQ_INI | FCTL_LS_EXCH |
			      FCTL_END_SEQ | FCTL_SI_XFER);
	fc_els_rsp_tmpl.rx_id = FC_RXID_ANY;

	/*
	 * fc_bls_req_tmpl
	 */
	fc_bls_req_tmpl.routing = FC_RTG_BASIC_LINK;
	fc_bls_req_tmpl.type = FC_TYPE_BLS;
	fc_bls_req_tmpl.f_ctl = bfa_hton3b(FCTL_END_SEQ | FCTL_SI_XFER);
	fc_bls_req_tmpl.rx_id = FC_RXID_ANY;

	/*
	 * fc_bls_rsp_tmpl
	 */
	fc_bls_rsp_tmpl.routing = FC_RTG_BASIC_LINK;
	fc_bls_rsp_tmpl.cat_info = FC_CAT_BA_ACC;
	fc_bls_rsp_tmpl.type = FC_TYPE_BLS;
	fc_bls_rsp_tmpl.f_ctl =
		bfa_hton3b(FCTL_EC_RESP | FCTL_SEQ_INI | FCTL_LS_EXCH |
			      FCTL_END_SEQ | FCTL_SI_XFER);
	fc_bls_rsp_tmpl.rx_id = FC_RXID_ANY;

	/*
	 * ba_acc_tmpl
	 */
	ba_acc_tmpl.seq_id_valid = 0;
	ba_acc_tmpl.low_seq_cnt = 0;
	ba_acc_tmpl.high_seq_cnt = 0xFFFF;

	/*
	 * plogi_tmpl
	 */
	plogi_tmpl.csp.verhi = FC_PH_VER_PH_3;
	plogi_tmpl.csp.verlo = FC_PH_VER_4_3;
	plogi_tmpl.csp.ciro = 0x1;
	plogi_tmpl.csp.cisc = 0x0;
	plogi_tmpl.csp.altbbcred = 0x0;
	plogi_tmpl.csp.conseq = cpu_to_be16(0x00FF);
	plogi_tmpl.csp.ro_bitmap = cpu_to_be16(0x0002);
	plogi_tmpl.csp.e_d_tov = cpu_to_be32(2000);

	plogi_tmpl.class3.class_valid = 1;
	plogi_tmpl.class3.sequential = 1;
	plogi_tmpl.class3.conseq = 0xFF;
	plogi_tmpl.class3.ospx = 1;

	/*
	 * prli_tmpl
	 */
	prli_tmpl.command = FC_ELS_PRLI;
	prli_tmpl.pglen = 0x10;
	prli_tmpl.pagebytes = cpu_to_be16(0x0014);
	prli_tmpl.parampage.type = FC_TYPE_FCP;
	prli_tmpl.parampage.imagepair = 1;
	prli_tmpl.parampage.servparams.rxrdisab = 1;

	/*
	 * rrq_tmpl
	 */
	rrq_tmpl.els_cmd.els_code = FC_ELS_RRQ;

	/*
	 * fcp_struct fchs_s mpl
	 */
	fcp_fchs_tmpl.routing = FC_RTG_FC4_DEV_DATA;
	fcp_fchs_tmpl.cat_info = FC_CAT_UNSOLICIT_CMD;
	fcp_fchs_tmpl.type = FC_TYPE_FCP;
	fcp_fchs_tmpl.f_ctl =
		bfa_hton3b(FCTL_FS_EXCH | FCTL_END_SEQ | FCTL_SI_XFER);
	fcp_fchs_tmpl.seq_id = 1;
	fcp_fchs_tmpl.rx_id = FC_RXID_ANY;
}

static void
fc_gs_fchdr_build(struct fchs_s *fchs, u32 d_id, u32 s_id, u32 ox_id)
{
	memset(fchs, 0, sizeof(struct fchs_s));

	fchs->routing = FC_RTG_FC4_DEV_DATA;
	fchs->cat_info = FC_CAT_UNSOLICIT_CTRL;
	fchs->type = FC_TYPE_SERVICES;
	fchs->f_ctl =
		bfa_hton3b(FCTL_SEQ_INI | FCTL_FS_EXCH | FCTL_END_SEQ |
			      FCTL_SI_XFER);
	fchs->rx_id = FC_RXID_ANY;
	fchs->d_id = (d_id);
	fchs->s_id = (s_id);
	fchs->ox_id = cpu_to_be16(ox_id);

	/*
	 * @todo no need to set ox_id for request
	 *       no need to set rx_id for response
	 */
}

static void
fc_gsresp_fchdr_build(struct fchs_s *fchs, u32 d_id, u32 s_id, u16 ox_id)
{
	memset(fchs, 0, sizeof(struct fchs_s));

	fchs->routing = FC_RTG_FC4_DEV_DATA;
	fchs->cat_info = FC_CAT_SOLICIT_CTRL;
	fchs->type = FC_TYPE_SERVICES;
	fchs->f_ctl =
		bfa_hton3b(FCTL_EC_RESP | FCTL_SEQ_INI | FCTL_LS_EXCH |
			   FCTL_END_SEQ | FCTL_SI_XFER);
	fchs->d_id = d_id;
	fchs->s_id = s_id;
	fchs->ox_id = ox_id;
}

void
fc_els_req_build(struct fchs_s *fchs, u32 d_id, u32 s_id, __be16 ox_id)
{
	memcpy(fchs, &fc_els_req_tmpl, sizeof(struct fchs_s));
	fchs->d_id = (d_id);
	fchs->s_id = (s_id);
	fchs->ox_id = cpu_to_be16(ox_id);
}

static void
fc_els_rsp_build(struct fchs_s *fchs, u32 d_id, u32 s_id, __be16 ox_id)
{
	memcpy(fchs, &fc_els_rsp_tmpl, sizeof(struct fchs_s));
	fchs->d_id = d_id;
	fchs->s_id = s_id;
	fchs->ox_id = ox_id;
}

static void
fc_bls_rsp_build(struct fchs_s *fchs, u32 d_id, u32 s_id, __be16 ox_id)
{
	memcpy(fchs, &fc_bls_rsp_tmpl, sizeof(struct fchs_s));
	fchs->d_id = d_id;
	fchs->s_id = s_id;
	fchs->ox_id = ox_id;
}

static          u16
fc_plogi_x_build(struct fchs_s *fchs, void *pld, u32 d_id, u32 s_id,
		 __be16 ox_id, wwn_t port_name, wwn_t node_name,
		 u16 pdu_size, u16 bb_cr, u8 els_code)
{
	struct fc_logi_s *plogi = (struct fc_logi_s *) (pld);

	memcpy(plogi, &plogi_tmpl, sizeof(struct fc_logi_s));

	/* For FC AL bb_cr is 0 and altbbcred is 1 */
	if (!bb_cr)
		plogi->csp.altbbcred = 1;

	plogi->els_cmd.els_code = els_code;
	if (els_code == FC_ELS_PLOGI)
		fc_els_req_build(fchs, d_id, s_id, ox_id);
	else
		fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	plogi->csp.rxsz = plogi->class3.rxsz = cpu_to_be16(pdu_size);
	plogi->csp.bbcred  = cpu_to_be16(bb_cr);

	memcpy(&plogi->port_name, &port_name, sizeof(wwn_t));
	memcpy(&plogi->node_name, &node_name, sizeof(wwn_t));

	return sizeof(struct fc_logi_s);
}

u16
fc_flogi_acc_build(struct fchs_s *fchs, struct fc_logi_s *flogi, u32 s_id,
		   __be16 ox_id, wwn_t port_name, wwn_t node_name,
		   u16 pdu_size, u16 local_bb_credits, u8 bb_scn)
{
	u32        d_id = 0;
	u16	   bbscn_rxsz = (bb_scn << 12) | pdu_size;

	memcpy(flogi, &plogi_tmpl, sizeof(struct fc_logi_s));
	fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	flogi->els_cmd.els_code = FC_ELS_ACC;
	flogi->class3.rxsz = cpu_to_be16(pdu_size);
	flogi->csp.rxsz  = cpu_to_be16(bbscn_rxsz);	/* bb_scn/rxsz */
	flogi->port_name = port_name;
	flogi->node_name = node_name;

	flogi->csp.bbcred = cpu_to_be16(local_bb_credits);

	return sizeof(struct fc_logi_s);
}

u16
fc_plogi_build(struct fchs_s *fchs, void *pld, u32 d_id, u32 s_id,
	       u16 ox_id, wwn_t port_name, wwn_t node_name,
	       u16 pdu_size, u16 bb_cr)
{
	return fc_plogi_x_build(fchs, pld, d_id, s_id, ox_id, port_name,
				node_name, pdu_size, bb_cr, FC_ELS_PLOGI);
}

u16
fc_plogi_acc_build(struct fchs_s *fchs, void *pld, u32 d_id, u32 s_id,
		   u16 ox_id, wwn_t port_name, wwn_t node_name,
		   u16 pdu_size, u16 bb_cr)
{
	return fc_plogi_x_build(fchs, pld, d_id, s_id, ox_id, port_name,
				node_name, pdu_size, bb_cr, FC_ELS_ACC);
}

enum fc_parse_status
fc_plogi_parse(struct fchs_s *fchs)
{
	struct fc_logi_s *plogi = (struct fc_logi_s *) (fchs + 1);

	if (plogi->class3.class_valid != 1)
		return FC_PARSE_FAILURE;

	if ((be16_to_cpu(plogi->class3.rxsz) < FC_MIN_PDUSZ)
	    || (be16_to_cpu(plogi->class3.rxsz) > FC_MAX_PDUSZ)
	    || (plogi->class3.rxsz == 0))
		return FC_PARSE_FAILURE;

	return FC_PARSE_OK;
}

u16
fc_prli_build(struct fchs_s *fchs, void *pld, u32 d_id, u32 s_id,
	      u16 ox_id)
{
	struct fc_prli_s *prli = (struct fc_prli_s *) (pld);

	fc_els_req_build(fchs, d_id, s_id, ox_id);
	memcpy(prli, &prli_tmpl, sizeof(struct fc_prli_s));

	prli->command = FC_ELS_PRLI;
	prli->parampage.servparams.initiator     = 1;
	prli->parampage.servparams.retry         = 1;
	prli->parampage.servparams.rec_support   = 1;
	prli->parampage.servparams.task_retry_id = 0;
	prli->parampage.servparams.confirm       = 1;

	return sizeof(struct fc_prli_s);
}

u16
fc_prli_acc_build(struct fchs_s *fchs, void *pld, u32 d_id, u32 s_id,
		  __be16 ox_id, enum bfa_lport_role role)
{
	struct fc_prli_s *prli = (struct fc_prli_s *) (pld);

	fc_els_rsp_build(fchs, d_id, s_id, ox_id);
	memcpy(prli, &prli_tmpl, sizeof(struct fc_prli_s));

	prli->command = FC_ELS_ACC;

	prli->parampage.servparams.initiator = 1;

	prli->parampage.rspcode = FC_PRLI_ACC_XQTD;

	return sizeof(struct fc_prli_s);
}

enum fc_parse_status
fc_prli_rsp_parse(struct fc_prli_s *prli, int len)
{
	if (len < sizeof(struct fc_prli_s))
		return FC_PARSE_FAILURE;

	if (prli->command != FC_ELS_ACC)
		return FC_PARSE_FAILURE;

	if ((prli->parampage.rspcode != FC_PRLI_ACC_XQTD)
	    && (prli->parampage.rspcode != FC_PRLI_ACC_PREDEF_IMG))
		return FC_PARSE_FAILURE;

	if (prli->parampage.servparams.target != 1)
		return FC_PARSE_FAILURE;

	return FC_PARSE_OK;
}

u16
fc_logo_build(struct fchs_s *fchs, struct fc_logo_s *logo, u32 d_id, u32 s_id,
	      u16 ox_id, wwn_t port_name)
{
	fc_els_req_build(fchs, d_id, s_id, ox_id);

	memset(logo, '\0', sizeof(struct fc_logo_s));
	logo->els_cmd.els_code = FC_ELS_LOGO;
	logo->nport_id = (s_id);
	logo->orig_port_name = port_name;

	return sizeof(struct fc_logo_s);
}

static u16
fc_adisc_x_build(struct fchs_s *fchs, struct fc_adisc_s *adisc, u32 d_id,
		 u32 s_id, __be16 ox_id, wwn_t port_name,
		 wwn_t node_name, u8 els_code)
{
	memset(adisc, '\0', sizeof(struct fc_adisc_s));

	adisc->els_cmd.els_code = els_code;

	if (els_code == FC_ELS_ADISC)
		fc_els_req_build(fchs, d_id, s_id, ox_id);
	else
		fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	adisc->orig_HA = 0;
	adisc->orig_port_name = port_name;
	adisc->orig_node_name = node_name;
	adisc->nport_id = (s_id);

	return sizeof(struct fc_adisc_s);
}

u16
fc_adisc_build(struct fchs_s *fchs, struct fc_adisc_s *adisc, u32 d_id,
		u32 s_id, __be16 ox_id, wwn_t port_name, wwn_t node_name)
{
	return fc_adisc_x_build(fchs, adisc, d_id, s_id, ox_id, port_name,
				node_name, FC_ELS_ADISC);
}

u16
fc_adisc_acc_build(struct fchs_s *fchs, struct fc_adisc_s *adisc, u32 d_id,
		   u32 s_id, __be16 ox_id, wwn_t port_name,
		   wwn_t node_name)
{
	return fc_adisc_x_build(fchs, adisc, d_id, s_id, ox_id, port_name,
				node_name, FC_ELS_ACC);
}

enum fc_parse_status
fc_adisc_rsp_parse(struct fc_adisc_s *adisc, int len, wwn_t port_name,
				 wwn_t node_name)
{

	if (len < sizeof(struct fc_adisc_s))
		return FC_PARSE_FAILURE;

	if (adisc->els_cmd.els_code != FC_ELS_ACC)
		return FC_PARSE_FAILURE;

	if (!wwn_is_equal(adisc->orig_port_name, port_name))
		return FC_PARSE_FAILURE;

	return FC_PARSE_OK;
}

u16
fc_logo_acc_build(struct fchs_s *fchs, void *pld, u32 d_id, u32 s_id,
		  __be16 ox_id)
{
	struct fc_els_cmd_s *acc = pld;

	fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	memset(acc, 0, sizeof(struct fc_els_cmd_s));
	acc->els_code = FC_ELS_ACC;

	return sizeof(struct fc_els_cmd_s);
}

u16
fc_ls_rjt_build(struct fchs_s *fchs, struct fc_ls_rjt_s *ls_rjt, u32 d_id,
		u32 s_id, __be16 ox_id, u8 reason_code,
		u8 reason_code_expl)
{
	fc_els_rsp_build(fchs, d_id, s_id, ox_id);
	memset(ls_rjt, 0, sizeof(struct fc_ls_rjt_s));

	ls_rjt->els_cmd.els_code = FC_ELS_LS_RJT;
	ls_rjt->reason_code = reason_code;
	ls_rjt->reason_code_expl = reason_code_expl;
	ls_rjt->vendor_unique = 0x00;

	return sizeof(struct fc_ls_rjt_s);
}

u16
fc_ba_acc_build(struct fchs_s *fchs, struct fc_ba_acc_s *ba_acc, u32 d_id,
		u32 s_id, __be16 ox_id, u16 rx_id)
{
	fc_bls_rsp_build(fchs, d_id, s_id, ox_id);

	memcpy(ba_acc, &ba_acc_tmpl, sizeof(struct fc_ba_acc_s));

	fchs->rx_id = rx_id;

	ba_acc->ox_id = fchs->ox_id;
	ba_acc->rx_id = fchs->rx_id;

	return sizeof(struct fc_ba_acc_s);
}

u16
fc_ls_acc_build(struct fchs_s *fchs, struct fc_els_cmd_s *els_cmd, u32 d_id,
		u32 s_id, __be16 ox_id)
{
	fc_els_rsp_build(fchs, d_id, s_id, ox_id);
	memset(els_cmd, 0, sizeof(struct fc_els_cmd_s));
	els_cmd->els_code = FC_ELS_ACC;

	return sizeof(struct fc_els_cmd_s);
}

int
fc_logout_params_pages(struct fchs_s *fc_frame, u8 els_code)
{
	int             num_pages = 0;
	struct fc_prlo_s *prlo;
	struct fc_tprlo_s *tprlo;

	if (els_code == FC_ELS_PRLO) {
		prlo = (struct fc_prlo_s *) (fc_frame + 1);
		num_pages = (be16_to_cpu(prlo->payload_len) - 4) / 16;
	} else {
		tprlo = (struct fc_tprlo_s *) (fc_frame + 1);
		num_pages = (be16_to_cpu(tprlo->payload_len) - 4) / 16;
	}
	return num_pages;
}

u16
fc_prlo_acc_build(struct fchs_s *fchs, struct fc_prlo_acc_s *prlo_acc, u32 d_id,
		  u32 s_id, __be16 ox_id, int num_pages)
{
	int             page;

	fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	memset(prlo_acc, 0, (num_pages * 16) + 4);
	prlo_acc->command = FC_ELS_ACC;
	prlo_acc->page_len = 0x10;
	prlo_acc->payload_len = cpu_to_be16((num_pages * 16) + 4);

	for (page = 0; page < num_pages; page++) {
		prlo_acc->prlo_acc_params[page].opa_valid = 0;
		prlo_acc->prlo_acc_params[page].rpa_valid = 0;
		prlo_acc->prlo_acc_params[page].fc4type_csp = FC_TYPE_FCP;
		prlo_acc->prlo_acc_params[page].orig_process_assc = 0;
		prlo_acc->prlo_acc_params[page].resp_process_assc = 0;
	}

	return be16_to_cpu(prlo_acc->payload_len);
}

u16
fc_rnid_acc_build(struct fchs_s *fchs, struct fc_rnid_acc_s *rnid_acc, u32 d_id,
		  u32 s_id, __be16 ox_id, u32 data_format,
		  struct fc_rnid_common_id_data_s *common_id_data,
		  struct fc_rnid_general_topology_data_s *gen_topo_data)
{
	memset(rnid_acc, 0, sizeof(struct fc_rnid_acc_s));

	fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	rnid_acc->els_cmd.els_code = FC_ELS_ACC;
	rnid_acc->node_id_data_format = data_format;
	rnid_acc->common_id_data_length =
			sizeof(struct fc_rnid_common_id_data_s);
	rnid_acc->common_id_data = *common_id_data;

	if (data_format == RNID_NODEID_DATA_FORMAT_DISCOVERY) {
		rnid_acc->specific_id_data_length =
			sizeof(struct fc_rnid_general_topology_data_s);
		rnid_acc->gen_topology_data = *gen_topo_data;
		return sizeof(struct fc_rnid_acc_s);
	} else {
		return sizeof(struct fc_rnid_acc_s) -
			sizeof(struct fc_rnid_general_topology_data_s);
	}

}

u16
fc_rpsc2_build(struct fchs_s *fchs, struct fc_rpsc2_cmd_s *rpsc2, u32 d_id,
		u32 s_id, u32 *pid_list, u16 npids)
{
	u32 dctlr_id = FC_DOMAIN_CTRLR(bfa_hton3b(d_id));
	int i = 0;

	fc_els_req_build(fchs, bfa_hton3b(dctlr_id), s_id, 0);

	memset(rpsc2, 0, sizeof(struct fc_rpsc2_cmd_s));

	rpsc2->els_cmd.els_code = FC_ELS_RPSC;
	rpsc2->token = cpu_to_be32(FC_BRCD_TOKEN);
	rpsc2->num_pids  = cpu_to_be16(npids);
	for (i = 0; i < npids; i++)
		rpsc2->pid_list[i].pid = pid_list[i];

	return sizeof(struct fc_rpsc2_cmd_s) + ((npids - 1) * (sizeof(u32)));
}

u16
fc_rpsc_acc_build(struct fchs_s *fchs, struct fc_rpsc_acc_s *rpsc_acc,
		u32 d_id, u32 s_id, __be16 ox_id,
		  struct fc_rpsc_speed_info_s *oper_speed)
{
	memset(rpsc_acc, 0, sizeof(struct fc_rpsc_acc_s));

	fc_els_rsp_build(fchs, d_id, s_id, ox_id);

	rpsc_acc->command = FC_ELS_ACC;
	rpsc_acc->num_entries = cpu_to_be16(1);

	rpsc_acc->speed_info[0].port_speed_cap =
		cpu_to_be16(oper_speed->port_speed_cap);

	rpsc_acc->speed_info[0].port_op_speed =
		cpu_to_be16(oper_speed->port_op_speed);

	return sizeof(struct fc_rpsc_acc_s);
}

static void
fc_gs_cthdr_build(struct ct_hdr_s *cthdr, u32 s_id, u16 cmd_code)
{
	memset(cthdr, 0, sizeof(struct ct_hdr_s));
	cthdr->rev_id = CT_GS3_REVISION;
	cthdr->gs_type = CT_GSTYPE_DIRSERVICE;
	cthdr->gs_sub_type = CT_GSSUBTYPE_NAMESERVER;
	cthdr->cmd_rsp_code = cpu_to_be16(cmd_code);
}

static void
fc_gs_fdmi_cthdr_build(struct ct_hdr_s *cthdr, u32 s_id, u16 cmd_code)
{
	memset(cthdr, 0, sizeof(struct ct_hdr_s));
	cthdr->rev_id = CT_GS3_REVISION;
	cthdr->gs_type = CT_GSTYPE_MGMTSERVICE;
	cthdr->gs_sub_type = CT_GSSUBTYPE_HBA_MGMTSERVER;
	cthdr->cmd_rsp_code = cpu_to_be16(cmd_code);
}

static void
fc_gs_ms_cthdr_build(struct ct_hdr_s *cthdr, u32 s_id, u16 cmd_code,
					 u8 sub_type)
{
	memset(cthdr, 0, sizeof(struct ct_hdr_s));
	cthdr->rev_id = CT_GS3_REVISION;
	cthdr->gs_type = CT_GSTYPE_MGMTSERVICE;
	cthdr->gs_sub_type = sub_type;
	cthdr->cmd_rsp_code = cpu_to_be16(cmd_code);
}

u16
fc_gidpn_build(struct fchs_s *fchs, void *pyld, u32 s_id, u16 ox_id,
	       wwn_t port_name)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_gidpn_req_s *gidpn = (struct fcgs_gidpn_req_s *)(cthdr + 1);
	u32        d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, ox_id);
	fc_gs_cthdr_build(cthdr, s_id, GS_GID_PN);

	memset(gidpn, 0, sizeof(struct fcgs_gidpn_req_s));
	gidpn->port_name = port_name;
	return sizeof(struct fcgs_gidpn_req_s) + sizeof(struct ct_hdr_s);
}

u16
fc_gpnid_build(struct fchs_s *fchs, void *pyld, u32 s_id, u16 ox_id,
	       u32 port_id)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	fcgs_gpnid_req_t *gpnid = (fcgs_gpnid_req_t *) (cthdr + 1);
	u32        d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, ox_id);
	fc_gs_cthdr_build(cthdr, s_id, GS_GPN_ID);

	memset(gpnid, 0, sizeof(fcgs_gpnid_req_t));
	gpnid->dap = port_id;
	return sizeof(fcgs_gpnid_req_t) + sizeof(struct ct_hdr_s);
}

u16
fc_gs_rjt_build(struct fchs_s *fchs,  struct ct_hdr_s *cthdr,
		u32 d_id, u32 s_id, u16 ox_id, u8 reason_code,
		u8 reason_code_expl)
{
	fc_gsresp_fchdr_build(fchs, d_id, s_id, ox_id);

	cthdr->cmd_rsp_code = cpu_to_be16(CT_RSP_REJECT);
	cthdr->rev_id = CT_GS3_REVISION;

	cthdr->reason_code = reason_code;
	cthdr->exp_code    = reason_code_expl;
	return sizeof(struct ct_hdr_s);
}

u16
fc_scr_build(struct fchs_s *fchs, struct fc_scr_s *scr,
		u8 set_br_reg, u32 s_id, u16 ox_id)
{
	u32        d_id = bfa_hton3b(FC_FABRIC_CONTROLLER);

	fc_els_req_build(fchs, d_id, s_id, ox_id);

	memset(scr, 0, sizeof(struct fc_scr_s));
	scr->command = FC_ELS_SCR;
	scr->reg_func = FC_SCR_REG_FUNC_FULL;
	if (set_br_reg)
		scr->vu_reg_func = FC_VU_SCR_REG_FUNC_FABRIC_NAME_CHANGE;

	return sizeof(struct fc_scr_s);
}

u16
fc_rftid_build(struct fchs_s *fchs, void *pyld, u32 s_id, u16 ox_id,
	       enum bfa_lport_role roles)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_rftid_req_s *rftid = (struct fcgs_rftid_req_s *)(cthdr + 1);
	u32        type_value, d_id = bfa_hton3b(FC_NAME_SERVER);
	u8         index;

	fc_gs_fchdr_build(fchs, d_id, s_id, ox_id);
	fc_gs_cthdr_build(cthdr, s_id, GS_RFT_ID);

	memset(rftid, 0, sizeof(struct fcgs_rftid_req_s));

	rftid->dap = s_id;

	/* By default, FCP FC4 Type is registered */
	index = FC_TYPE_FCP >> 5;
	type_value = 1 << (FC_TYPE_FCP % 32);
	rftid->fc4_type[index] = cpu_to_be32(type_value);

	return sizeof(struct fcgs_rftid_req_s) + sizeof(struct ct_hdr_s);
}

u16
fc_rffid_build(struct fchs_s *fchs, void *pyld, u32 s_id, u16 ox_id,
	       u8 fc4_type, u8 fc4_ftrs)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_rffid_req_s *rffid = (struct fcgs_rffid_req_s *)(cthdr + 1);
	u32         d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, ox_id);
	fc_gs_cthdr_build(cthdr, s_id, GS_RFF_ID);

	memset(rffid, 0, sizeof(struct fcgs_rffid_req_s));

	rffid->dap	    = s_id;
	rffid->fc4ftr_bits  = fc4_ftrs;
	rffid->fc4_type	    = fc4_type;

	return sizeof(struct fcgs_rffid_req_s) + sizeof(struct ct_hdr_s);
}

u16
fc_rspnid_build(struct fchs_s *fchs, void *pyld, u32 s_id, u16 ox_id,
		u8 *name)
{

	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_rspnid_req_s *rspnid =
			(struct fcgs_rspnid_req_s *)(cthdr + 1);
	u32        d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, ox_id);
	fc_gs_cthdr_build(cthdr, s_id, GS_RSPN_ID);

	memset(rspnid, 0, sizeof(struct fcgs_rspnid_req_s));

	rspnid->dap = s_id;
	strscpy(rspnid->spn, name, sizeof(rspnid->spn));
	rspnid->spn_len = (u8) strlen(rspnid->spn);

	return sizeof(struct fcgs_rspnid_req_s) + sizeof(struct ct_hdr_s);
}

u16
fc_rsnn_nn_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			wwn_t node_name, u8 *name)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_rsnn_nn_req_s *rsnn_nn =
		(struct fcgs_rsnn_nn_req_s *) (cthdr + 1);
	u32	d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, 0);
	fc_gs_cthdr_build(cthdr, s_id, GS_RSNN_NN);

	memset(rsnn_nn, 0, sizeof(struct fcgs_rsnn_nn_req_s));

	rsnn_nn->node_name = node_name;
	strscpy(rsnn_nn->snn, name, sizeof(rsnn_nn->snn));
	rsnn_nn->snn_len = (u8) strlen(rsnn_nn->snn);

	return sizeof(struct fcgs_rsnn_nn_req_s) + sizeof(struct ct_hdr_s);
}

u16
fc_gid_ft_build(struct fchs_s *fchs, void *pyld, u32 s_id, u8 fc4_type)
{

	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_gidft_req_s *gidft = (struct fcgs_gidft_req_s *)(cthdr + 1);
	u32        d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, 0);

	fc_gs_cthdr_build(cthdr, s_id, GS_GID_FT);

	memset(gidft, 0, sizeof(struct fcgs_gidft_req_s));
	gidft->fc4_type = fc4_type;
	gidft->domain_id = 0;
	gidft->area_id = 0;

	return sizeof(struct fcgs_gidft_req_s) + sizeof(struct ct_hdr_s);
}

u16
fc_rnnid_build(struct fchs_s *fchs, void *pyld, u32 s_id, u32 port_id,
	       wwn_t node_name)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	struct fcgs_rnnid_req_s *rnnid = (struct fcgs_rnnid_req_s *)(cthdr + 1);
	u32        d_id = bfa_hton3b(FC_NAME_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, 0);
	fc_gs_cthdr_build(cthdr, s_id, GS_RNN_ID);

	memset(rnnid, 0, sizeof(struct fcgs_rnnid_req_s));
	rnnid->port_id = port_id;
	rnnid->node_name = node_name;

	return sizeof(struct fcgs_rnnid_req_s) + sizeof(struct ct_hdr_s);
}

/*
 * Builds fc hdr and ct hdr for FDMI requests.
 */
u16
fc_fdmi_reqhdr_build(struct fchs_s *fchs, void *pyld, u32 s_id,
		     u16 cmd_code)
{

	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	u32        d_id = bfa_hton3b(FC_MGMT_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, 0);
	fc_gs_fdmi_cthdr_build(cthdr, s_id, cmd_code);

	return sizeof(struct ct_hdr_s);
}

/*
 * Given a FC4 Type, this function returns a fc4 type bitmask
 */
void
fc_get_fc4type_bitmask(u8 fc4_type, u8 *bit_mask)
{
	u8         index;
	__be32       *ptr = (__be32 *) bit_mask;
	u32        type_value;

	/*
	 * @todo : Check for bitmask size
	 */

	index = fc4_type >> 5;
	type_value = 1 << (fc4_type % 32);
	ptr[index] = cpu_to_be32(type_value);

}

/*
 *	GMAL Request
 */
u16
fc_gmal_req_build(struct fchs_s *fchs, void *pyld, u32 s_id, wwn_t wwn)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	fcgs_gmal_req_t *gmal = (fcgs_gmal_req_t *) (cthdr + 1);
	u32        d_id = bfa_hton3b(FC_MGMT_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, 0);
	fc_gs_ms_cthdr_build(cthdr, s_id, GS_FC_GMAL_CMD,
			CT_GSSUBTYPE_CFGSERVER);

	memset(gmal, 0, sizeof(fcgs_gmal_req_t));
	gmal->wwn = wwn;

	return sizeof(struct ct_hdr_s) + sizeof(fcgs_gmal_req_t);
}

/*
 * GFN (Get Fabric Name) Request
 */
u16
fc_gfn_req_build(struct fchs_s *fchs, void *pyld, u32 s_id, wwn_t wwn)
{
	struct ct_hdr_s *cthdr = (struct ct_hdr_s *) pyld;
	fcgs_gfn_req_t *gfn = (fcgs_gfn_req_t *) (cthdr + 1);
	u32        d_id = bfa_hton3b(FC_MGMT_SERVER);

	fc_gs_fchdr_build(fchs, d_id, s_id, 0);
	fc_gs_ms_cthdr_build(cthdr, s_id, GS_FC_GFN_CMD,
			CT_GSSUBTYPE_CFGSERVER);

	memset(gfn, 0, sizeof(fcgs_gfn_req_t));
	gfn->wwn = wwn;

	return sizeof(struct ct_hdr_s) + sizeof(fcgs_gfn_req_t);
}

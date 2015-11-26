/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * fcbuild.h - FC link service frame building and parsing routines
 */

#ifndef __FCBUILD_H__
#define __FCBUILD_H__

#include "bfad_drv.h"
#include "bfa_fc.h"
#include "bfa_defs_fcs.h"

/*
 * Utility Macros/functions
 */

#define wwn_is_equal(_wwn1, _wwn2)		\
	(memcmp(&(_wwn1), &(_wwn2), sizeof(wwn_t)) == 0)

#define fc_roundup(_l, _s) (((_l) + ((_s) - 1)) & ~((_s) - 1))

/*
 * Given the fc response length, this routine will return
 * the length of the actual payload bytes following the CT header.
 *
 * Assumes the input response length does not include the crc, eof, etc.
 */
static inline   u32
fc_get_ctresp_pyld_len(u32 resp_len)
{
	return resp_len - sizeof(struct ct_hdr_s);
}

/*
 * Convert bfa speed to rpsc speed value.
 */
static inline  enum bfa_port_speed
fc_rpsc_operspeed_to_bfa_speed(enum fc_rpsc_op_speed speed)
{
	switch (speed) {

	case RPSC_OP_SPEED_1G:
		return BFA_PORT_SPEED_1GBPS;

	case RPSC_OP_SPEED_2G:
		return BFA_PORT_SPEED_2GBPS;

	case RPSC_OP_SPEED_4G:
		return BFA_PORT_SPEED_4GBPS;

	case RPSC_OP_SPEED_8G:
		return BFA_PORT_SPEED_8GBPS;

	case RPSC_OP_SPEED_16G:
		return BFA_PORT_SPEED_16GBPS;

	case RPSC_OP_SPEED_10G:
		return BFA_PORT_SPEED_10GBPS;

	default:
		return BFA_PORT_SPEED_UNKNOWN;
	}
}

/*
 * Convert RPSC speed to bfa speed value.
 */
static inline   enum fc_rpsc_op_speed
fc_bfa_speed_to_rpsc_operspeed(enum bfa_port_speed op_speed)
{
	switch (op_speed) {

	case BFA_PORT_SPEED_1GBPS:
		return RPSC_OP_SPEED_1G;

	case BFA_PORT_SPEED_2GBPS:
		return RPSC_OP_SPEED_2G;

	case BFA_PORT_SPEED_4GBPS:
		return RPSC_OP_SPEED_4G;

	case BFA_PORT_SPEED_8GBPS:
		return RPSC_OP_SPEED_8G;

	case BFA_PORT_SPEED_16GBPS:
		return RPSC_OP_SPEED_16G;

	case BFA_PORT_SPEED_10GBPS:
		return RPSC_OP_SPEED_10G;

	default:
		return RPSC_OP_SPEED_NOT_EST;
	}
}

enum fc_parse_status {
	FC_PARSE_OK = 0,
	FC_PARSE_FAILURE = 1,
	FC_PARSE_BUSY = 2,
	FC_PARSE_LEN_INVAL,
	FC_PARSE_ACC_INVAL,
	FC_PARSE_PWWN_NOT_EQUAL,
	FC_PARSE_NWWN_NOT_EQUAL,
	FC_PARSE_RXSZ_INVAL,
	FC_PARSE_NOT_FCP,
	FC_PARSE_OPAFLAG_INVAL,
	FC_PARSE_RPAFLAG_INVAL,
	FC_PARSE_OPA_INVAL,
	FC_PARSE_RPA_INVAL,

};

struct fc_templates_s {
	struct fchs_s fc_els_req;
	struct fchs_s fc_bls_req;
	struct fc_logi_s plogi;
	struct fc_rrq_s rrq;
};

void            fcbuild_init(void);

u16        fc_flogi_build(struct fchs_s *fchs, struct fc_logi_s *flogi,
			u32 s_id, u16 ox_id, wwn_t port_name, wwn_t node_name,
			       u16 pdu_size, u8 set_npiv, u8 set_auth,
			       u16 local_bb_credits);

u16        fc_fdisc_build(struct fchs_s *buf, struct fc_logi_s *flogi, u32 s_id,
			       u16 ox_id, wwn_t port_name, wwn_t node_name,
			       u16 pdu_size);

u16        fc_flogi_acc_build(struct fchs_s *fchs, struct fc_logi_s *flogi,
				   u32 s_id, __be16 ox_id,
				   wwn_t port_name, wwn_t node_name,
				   u16 pdu_size,
				   u16 local_bb_credits, u8 bb_scn);

u16        fc_plogi_build(struct fchs_s *fchs, void *pld, u32 d_id,
			       u32 s_id, u16 ox_id, wwn_t port_name,
			       wwn_t node_name, u16 pdu_size, u16 bb_cr);

enum fc_parse_status fc_plogi_parse(struct fchs_s *fchs);

u16        fc_abts_build(struct fchs_s *buf, u32 d_id, u32 s_id,
			      u16 ox_id);

enum fc_parse_status fc_abts_rsp_parse(struct fchs_s *buf, int len);

u16        fc_rrq_build(struct fchs_s *buf, struct fc_rrq_s *rrq, u32 d_id,
			     u32 s_id, u16 ox_id, u16 rrq_oxid);
enum fc_parse_status fc_rrq_rsp_parse(struct fchs_s *buf, int len);

u16        fc_rspnid_build(struct fchs_s *fchs, void *pld, u32 s_id,
				u16 ox_id, u8 *name);
u16	fc_rsnn_nn_build(struct fchs_s *fchs, void *pld, u32 s_id,
				wwn_t node_name, u8 *name);

u16        fc_rftid_build(struct fchs_s *fchs, void *pld, u32 s_id,
			       u16 ox_id, enum bfa_lport_role role);

u16       fc_rftid_build_sol(struct fchs_s *fchs, void *pyld, u32 s_id,
				   u16 ox_id, u8 *fc4_bitmap,
				   u32 bitmap_size);

u16	fc_rffid_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			u16 ox_id, u8 fc4_type, u8 fc4_ftrs);

u16        fc_gidpn_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			       u16 ox_id, wwn_t port_name);

u16        fc_gpnid_build(struct fchs_s *fchs, void *pld, u32 s_id,
			       u16 ox_id, u32 port_id);

u16	fc_gs_rjt_build(struct fchs_s *fchs, struct ct_hdr_s *cthdr,
			u32 d_id, u32 s_id, u16 ox_id,
			u8 reason_code, u8 reason_code_expl);

u16        fc_scr_build(struct fchs_s *fchs, struct fc_scr_s *scr,
			u8 set_br_reg, u32 s_id, u16 ox_id);

u16        fc_plogi_acc_build(struct fchs_s *fchs, void *pld, u32 d_id,
				   u32 s_id, u16 ox_id,
				   wwn_t port_name, wwn_t node_name,
				   u16 pdu_size, u16 bb_cr);

u16        fc_adisc_build(struct fchs_s *fchs, struct fc_adisc_s *adisc,
			u32 d_id, u32 s_id, __be16 ox_id, wwn_t port_name,
			       wwn_t node_name);

enum fc_parse_status fc_adisc_parse(struct fchs_s *fchs, void *pld,
			u32 host_dap, wwn_t node_name, wwn_t port_name);

enum fc_parse_status fc_adisc_rsp_parse(struct fc_adisc_s *adisc, int len,
				 wwn_t port_name, wwn_t node_name);

u16        fc_adisc_acc_build(struct fchs_s *fchs, struct fc_adisc_s *adisc,
				   u32 d_id, u32 s_id, __be16 ox_id,
				   wwn_t port_name, wwn_t node_name);
u16        fc_ls_rjt_build(struct fchs_s *fchs, struct fc_ls_rjt_s *ls_rjt,
				u32 d_id, u32 s_id, __be16 ox_id,
				u8 reason_code, u8 reason_code_expl);
u16        fc_ls_acc_build(struct fchs_s *fchs, struct fc_els_cmd_s *els_cmd,
				u32 d_id, u32 s_id, __be16 ox_id);
u16        fc_prli_build(struct fchs_s *fchs, void *pld, u32 d_id,
			      u32 s_id, u16 ox_id);

enum fc_parse_status fc_prli_rsp_parse(struct fc_prli_s *prli, int len);

u16        fc_prli_acc_build(struct fchs_s *fchs, void *pld, u32 d_id,
				  u32 s_id, __be16 ox_id,
				  enum bfa_lport_role role);

u16        fc_rnid_build(struct fchs_s *fchs, struct fc_rnid_cmd_s *rnid,
			      u32 d_id, u32 s_id, u16 ox_id,
			      u32 data_format);

u16        fc_rnid_acc_build(struct fchs_s *fchs,
			struct fc_rnid_acc_s *rnid_acc, u32 d_id, u32 s_id,
			__be16 ox_id, u32 data_format,
			struct fc_rnid_common_id_data_s *common_id_data,
			struct fc_rnid_general_topology_data_s *gen_topo_data);

u16	fc_rpsc2_build(struct fchs_s *fchs, struct fc_rpsc2_cmd_s *rps2c,
			u32 d_id, u32 s_id, u32 *pid_list, u16 npids);
u16        fc_rpsc_build(struct fchs_s *fchs, struct fc_rpsc_cmd_s *rpsc,
			      u32 d_id, u32 s_id, u16 ox_id);
u16        fc_rpsc_acc_build(struct fchs_s *fchs,
			struct fc_rpsc_acc_s *rpsc_acc, u32 d_id, u32 s_id,
			__be16 ox_id, struct fc_rpsc_speed_info_s *oper_speed);
u16        fc_gid_ft_build(struct fchs_s *fchs, void *pld, u32 s_id,
				u8 fc4_type);

u16        fc_rpnid_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			       u32 port_id, wwn_t port_name);

u16        fc_rnnid_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			       u32 port_id, wwn_t node_name);

u16        fc_rcsid_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			       u32 port_id, u32 cos);

u16        fc_rptid_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			       u32 port_id, u8 port_type);

u16        fc_ganxt_build(struct fchs_s *fchs, void *pyld, u32 s_id,
			       u32 port_id);

u16        fc_logo_build(struct fchs_s *fchs, struct fc_logo_s *logo, u32 d_id,
			      u32 s_id, u16 ox_id, wwn_t port_name);

u16        fc_logo_acc_build(struct fchs_s *fchs, void *pld, u32 d_id,
				  u32 s_id, __be16 ox_id);

u16        fc_fdmi_reqhdr_build(struct fchs_s *fchs, void *pyld, u32 s_id,
				     u16 cmd_code);
u16	fc_gmal_req_build(struct fchs_s *fchs, void *pyld, u32 s_id, wwn_t wwn);
u16	fc_gfn_req_build(struct fchs_s *fchs, void *pyld, u32 s_id, wwn_t wwn);

void		fc_get_fc4type_bitmask(u8 fc4_type, u8 *bit_mask);

void		fc_els_req_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
					 __be16 ox_id);

enum fc_parse_status	fc_els_rsp_parse(struct fchs_s *fchs, int len);

enum fc_parse_status	fc_plogi_rsp_parse(struct fchs_s *fchs, int len,
					wwn_t port_name);

enum fc_parse_status	fc_prli_parse(struct fc_prli_s *prli);

enum fc_parse_status	fc_pdisc_parse(struct fchs_s *fchs, wwn_t node_name,
					wwn_t port_name);

u16 fc_ba_acc_build(struct fchs_s *fchs, struct fc_ba_acc_s *ba_acc, u32 d_id,
		u32 s_id, __be16 ox_id, u16 rx_id);

int fc_logout_params_pages(struct fchs_s *fc_frame, u8 els_code);

u16 fc_tprlo_acc_build(struct fchs_s *fchs, struct fc_tprlo_acc_s *tprlo_acc,
		u32 d_id, u32 s_id, __be16 ox_id, int num_pages);

u16 fc_prlo_acc_build(struct fchs_s *fchs, struct fc_prlo_acc_s *prlo_acc,
		u32 d_id, u32 s_id, __be16 ox_id, int num_pages);

u16 fc_logo_rsp_parse(struct fchs_s *fchs, int len);

u16 fc_pdisc_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
		u16 ox_id, wwn_t port_name, wwn_t node_name,
		u16 pdu_size);

u16 fc_pdisc_rsp_parse(struct fchs_s *fchs, int len, wwn_t port_name);

u16 fc_prlo_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
		u16 ox_id, int num_pages);

u16 fc_prlo_rsp_parse(struct fchs_s *fchs, int len);

u16 fc_tprlo_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
		u16 ox_id, int num_pages, enum fc_tprlo_type tprlo_type,
		u32 tpr_id);

u16 fc_tprlo_rsp_parse(struct fchs_s *fchs, int len);

u16 fc_ba_rjt_build(struct fchs_s *fchs, u32 d_id, u32 s_id,
		__be16 ox_id, u32 reason_code, u32 reason_expl);

u16 fc_gnnid_build(struct fchs_s *fchs, void *pyld, u32 s_id, u16 ox_id,
		u32 port_id);

u16 fc_ct_rsp_parse(struct ct_hdr_s *cthdr);

u16 fc_rscn_build(struct fchs_s *fchs, struct fc_rscn_pl_s *rscn, u32 s_id,
		u16 ox_id);
#endif

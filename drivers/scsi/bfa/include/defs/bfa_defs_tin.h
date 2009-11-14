/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
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

#ifndef __BFA_DEFS_TIN_H__
#define __BFA_DEFS_TIN_H__

#include <protocol/types.h>
#include <protocol/fc.h>

/**
 * FCS tin states
 */
enum bfa_tin_state_e {
	BFA_TIN_SM_OFFLINE = 0,		/*  tin is offline */
	BFA_TIN_SM_WOS_LOGIN = 1,	/*  Waiting PRLI ACC/RJT from ULP */
	BFA_TIN_SM_WFW_ONLINE = 2,	/*  Waiting ACK to PRLI ACC from FW */
	BFA_TIN_SM_ONLINE = 3,		/*  tin login is complete */
	BFA_TIN_SM_WIO_RELOGIN = 4,	/*  tin relogin is in progress */
	BFA_TIN_SM_WIO_LOGOUT = 5,	/*  Processing of PRLO req from
					 *   Initiator is in progress
					 */
	BFA_TIN_SM_WOS_LOGOUT = 6,	/*  Processing of PRLO req from
					 *   Initiator is in progress
					 */
	BFA_TIN_SM_WIO_CLEAN = 7,	/*  Waiting for IO cleanup before tin
					 *   is offline. This can be triggered
					 *   by RPORT LOGO (rcvd/sent) or by
					 *   PRLO (rcvd/sent)
					 */
};

struct bfa_prli_req_s {
	struct fchs_s fchs;
	struct fc_prli_s prli_payload;
};

struct bfa_prlo_req_s {
	struct fchs_s fchs;
	struct fc_prlo_s prlo_payload;
};

void bfa_tin_send_login_rsp(void *bfa_tin, u32 login_rsp,
				struct fc_ls_rjt_s rjt_payload);
void bfa_tin_send_logout_rsp(void *bfa_tin, u32 logout_rsp,
				struct fc_ls_rjt_s rjt_payload);
/**
 * FCS target port statistics
 */
struct bfa_tin_stats_s {
	u32 onlines;	/*  ITN nexus onlines (PRLI done) */
	u32 offlines;	/*  ITN Nexus offlines 	*/
	u32 prli_req_parse_err;	/*  prli req parsing errors */
	u32 prli_rsp_rjt;	/*  num prli rsp rejects sent */
	u32 prli_rsp_acc;	/*  num prli rsp accepts sent */
	u32 cleanup_comps;	/*  ITN cleanup completions */
};

/**
 * FCS tin attributes returned in queries
 */
struct bfa_tin_attr_s {
	enum bfa_tin_state_e state;
	u8	seq_retry;    /*  Sequence retry supported      */
	u8	rsvd[3];
};

/**
 * BFA TIN async event data structure for BFAL
 */
enum bfa_tin_aen_event {
	BFA_TIN_AEN_ONLINE 	= 1,	/*  Target online */
	BFA_TIN_AEN_OFFLINE 	= 2,	/*  Target offline */
	BFA_TIN_AEN_DISCONNECT	= 3,	/*  Target disconnected */
};

/**
 * BFA TIN event data structure.
 */
struct bfa_tin_aen_data_s {
	u16 vf_id;	/*  vf_id of the IT nexus */
	u16 rsvd[3];
	wwn_t lpwwn;	/*  WWN of logical port */
	wwn_t rpwwn;	/*  WWN of remote(target) port */
};

/**
 * Below APIs are needed from BFA driver
 * Move these to BFA driver public header file?
 */
/*  TIN rcvd new PRLI & gets bfad_tin_t ptr from driver this callback */
void *bfad_tin_rcvd_login_req(void *bfad_tm_port, void *bfa_tin,
				wwn_t rp_wwn, u32 rp_fcid,
				struct bfa_prli_req_s prli_req);
/*  TIN rcvd new PRLO */
void bfad_tin_rcvd_logout_req(void *bfad_tin, wwn_t rp_wwn, u32 rp_fcid,
				struct bfa_prlo_req_s prlo_req);
/*  TIN is online and ready for IO */
void bfad_tin_online(void *bfad_tin);
/*  TIN is offline and BFA driver can shutdown its upper stack */
void bfad_tin_offline(void *bfad_tin);
/*  TIN does not need this BFA driver tin tag anymore, so can be freed */
void bfad_tin_res_free(void *bfad_tin);

#endif /* __BFA_DEFS_TIN_H__ */

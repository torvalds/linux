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

#ifndef __BFA_FCS_AUTH_H__
#define __BFA_FCS_AUTH_H__

struct bfa_fcs_s;

#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_auth.h>
#include <defs/bfa_defs_vf.h>
#include <cs/bfa_q.h>
#include <cs/bfa_sm.h>
#include <defs/bfa_defs_pport.h>
#include <fcs/bfa_fcs_lport.h>
#include <protocol/fc_sp.h>

struct bfa_fcs_fabric_s;



struct bfa_fcs_auth_s {
	bfa_sm_t	sm;	/*  state machine */
	bfa_boolean_t   policy;	/*  authentication enabled/disabled */
	enum bfa_auth_status status;	/*  authentication status */
	enum auth_rjt_codes  rjt_code;	/*  auth reject status */
	enum auth_rjt_code_exps  rjt_code_exp;	/*  auth reject reason */
	enum bfa_auth_algo algo;	/*  Authentication algorithm */
	struct bfa_auth_stats_s stats;	/*  Statistics */
	enum auth_dh_gid   group;	/*  DH(diffie-hellman) Group */
	enum bfa_auth_secretsource source;	/*  Secret source */
	char            secret[BFA_AUTH_SECRET_STRING_LEN];
				/*  secret string */
	u8         secret_len;
				/*  secret string length */
	u8         nretries;
				/*  number of retries */
	struct bfa_fcs_fabric_s *fabric;/*  pointer to fabric */
	u8         sentcode;	/*  pointer to response data */
	u8        *response;	/*  pointer to response data */
	struct bfa_timer_s delay_timer; 	/*  delay timer */
	struct bfa_fcxp_s *fcxp;		/*  pointer to fcxp */
	struct bfa_fcxp_wqe_s fcxp_wqe;
};

/**
 * bfa fcs authentication public functions
 */
bfa_status_t    bfa_fcs_auth_get_attr(struct bfa_fcs_s *port,
				      struct bfa_auth_attr_s *attr);
bfa_status_t    bfa_fcs_auth_set_policy(struct bfa_fcs_s *port,
					bfa_boolean_t policy);
enum bfa_auth_status bfa_fcs_auth_get_status(struct bfa_fcs_s *port);
bfa_status_t    bfa_fcs_auth_set_algo(struct bfa_fcs_s *port,
				      enum bfa_auth_algo algo);
bfa_status_t    bfa_fcs_auth_get_stats(struct bfa_fcs_s *port,
				       struct bfa_auth_stats_s *stats);
bfa_status_t    bfa_fcs_auth_set_dh_group(struct bfa_fcs_s *port, int group);
bfa_status_t    bfa_fcs_auth_set_secretstring(struct bfa_fcs_s *port,
					      char *secret);
bfa_status_t    bfa_fcs_auth_set_secretstring_encrypt(struct bfa_fcs_s *port,
					      u32 secret[], u32 len);
bfa_status_t    bfa_fcs_auth_set_secretsource(struct bfa_fcs_s *port,
					      enum bfa_auth_secretsource src);
bfa_status_t    bfa_fcs_auth_reset_stats(struct bfa_fcs_s *port);
bfa_status_t    bfa_fcs_auth_reinit(struct bfa_fcs_s *port);

#endif /* __BFA_FCS_AUTH_H__ */

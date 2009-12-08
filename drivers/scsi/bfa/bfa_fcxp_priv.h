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

#ifndef __BFA_FCXP_PRIV_H__
#define __BFA_FCXP_PRIV_H__

#include <cs/bfa_sm.h>
#include <protocol/fc.h>
#include <bfa_svc.h>
#include <bfi/bfi_fcxp.h>

#define BFA_FCXP_MIN     	(1)
#define BFA_FCXP_MAX_IBUF_SZ	(2 * 1024 + 256)
#define BFA_FCXP_MAX_LBUF_SZ	(4 * 1024 + 256)

struct bfa_fcxp_mod_s {
	struct bfa_s      *bfa;		/*  backpointer to BFA */
	struct bfa_fcxp_s *fcxp_list;	/*  array of FCXPs */
	u16        num_fcxps;	/*  max num FCXP requests */
	struct list_head fcxp_free_q;	/*  free FCXPs */
	struct list_head fcxp_active_q;	/*  active FCXPs */
	void	*req_pld_list_kva;	/*  list of FCXP req pld */
	u64 req_pld_list_pa;	/*  list of FCXP req pld */
	void *rsp_pld_list_kva;		/*  list of FCXP resp pld */
	u64 rsp_pld_list_pa;	/*  list of FCXP resp pld */
	struct list_head  wait_q;		/*  wait queue for free fcxp */
	u32	req_pld_sz;
	u32	rsp_pld_sz;
};

#define BFA_FCXP_MOD(__bfa)		(&(__bfa)->modules.fcxp_mod)
#define BFA_FCXP_FROM_TAG(__mod, __tag)	(&(__mod)->fcxp_list[__tag])

typedef void    (*fcxp_send_cb_t) (struct bfa_s *ioc, struct bfa_fcxp_s *fcxp,
				   void *cb_arg, bfa_status_t req_status,
				   u32 rsp_len, u32 resid_len,
				   struct fchs_s *rsp_fchs);

/**
 * Information needed for a FCXP request
 */
struct bfa_fcxp_req_info_s {
	struct bfa_rport_s *bfa_rport;	/*  Pointer to the bfa rport that was
					 *returned from bfa_rport_create().
					 *This could be left NULL for WKA or for
					 *FCXP interactions before the rport
					 *nexus is established
					 */
	struct fchs_s   fchs;	/*  request FC header structure */
	u8 cts;		/*  continous sequence */
	u8 class;		/*  FC class for the request/response */
	u16 max_frmsz;	/*  max send frame size */
	u16 vf_id;		/*  vsan tag if applicable */
	u8	lp_tag;		/*  lport tag */
	u32 req_tot_len;	/*  request payload total length */
};

struct bfa_fcxp_rsp_info_s {
	struct fchs_s rsp_fchs;		/*  Response frame's FC header will
					 * be *sent back in this field */
	u8         rsp_timeout;	/*  timeout in seconds, 0-no response
					 */
	u8         rsvd2[3];
	u32        rsp_maxlen;	/*  max response length expected */
};

struct bfa_fcxp_s {
	struct list_head 	qe;		/*  fcxp queue element */
	bfa_sm_t        sm;             /*  state machine */
	void           	*caller;	/*  driver or fcs */
	struct bfa_fcxp_mod_s *fcxp_mod;
					/*  back pointer to fcxp mod */
	u16        fcxp_tag;	/*  internal tag */
	struct bfa_fcxp_req_info_s req_info;
					/*  request info */
	struct bfa_fcxp_rsp_info_s rsp_info;
					/*  response info */
	u8 	use_ireqbuf;	/*  use internal req buf */
	u8         use_irspbuf;	/*  use internal rsp buf */
	u32        nreq_sgles;	/*  num request SGLEs */
	u32        nrsp_sgles;	/*  num response SGLEs */
	struct list_head req_sgpg_q;	/*  SG pages for request buf */
	struct list_head req_sgpg_wqe;	/*  wait queue for req SG page */
	struct list_head rsp_sgpg_q;	/*  SG pages for response buf */
	struct list_head rsp_sgpg_wqe;	/*  wait queue for rsp SG page */

	bfa_fcxp_get_sgaddr_t req_sga_cbfn;
					/*  SG elem addr user function */
	bfa_fcxp_get_sglen_t req_sglen_cbfn;
					/*  SG elem len user function */
	bfa_fcxp_get_sgaddr_t rsp_sga_cbfn;
					/*  SG elem addr user function */
	bfa_fcxp_get_sglen_t rsp_sglen_cbfn;
					/*  SG elem len user function */
	bfa_cb_fcxp_send_t send_cbfn;   /*  send completion callback */
	void		*send_cbarg;	/*  callback arg */
	struct bfa_sge_s   req_sge[BFA_FCXP_MAX_SGES];
					/*  req SG elems */
	struct bfa_sge_s   rsp_sge[BFA_FCXP_MAX_SGES];
					/*  rsp SG elems */
	u8         rsp_status;	/*  comp: rsp status */
	u32        rsp_len;	/*  comp: actual response len */
	u32        residue_len;	/*  comp: residual rsp length */
	struct fchs_s          rsp_fchs;	/*  comp: response fchs */
	struct bfa_cb_qe_s    hcb_qe;	/*  comp: callback qelem */
	struct bfa_reqq_wait_s	reqq_wqe;
	bfa_boolean_t	reqq_waiting;
};

#define BFA_FCXP_REQ_PLD(_fcxp) 	(bfa_fcxp_get_reqbuf(_fcxp))

#define BFA_FCXP_RSP_FCHS(_fcxp) 	(&((_fcxp)->rsp_info.fchs))
#define BFA_FCXP_RSP_PLD(_fcxp) 	(bfa_fcxp_get_rspbuf(_fcxp))

#define BFA_FCXP_REQ_PLD_PA(_fcxp)					\
	((_fcxp)->fcxp_mod->req_pld_list_pa +				\
		((_fcxp)->fcxp_mod->req_pld_sz  * (_fcxp)->fcxp_tag))

#define BFA_FCXP_RSP_PLD_PA(_fcxp) 					\
	((_fcxp)->fcxp_mod->rsp_pld_list_pa +				\
		((_fcxp)->fcxp_mod->rsp_pld_sz * (_fcxp)->fcxp_tag))

void	bfa_fcxp_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
#endif /* __BFA_FCXP_PRIV_H__ */

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

#ifndef __BFA_FCS_RPORT_H__
#define __BFA_FCS_RPORT_H__

#include <defs/bfa_defs_status.h>
#include <cs/bfa_q.h>
#include <fcs/bfa_fcs.h>
#include <defs/bfa_defs_rport.h>

#define BFA_FCS_RPORT_DEF_DEL_TIMEOUT 	90 	/* in secs */
/*
 * forward declarations
 */
struct bfad_rport_s;

struct bfa_fcs_itnim_s;
struct bfa_fcs_tin_s;
struct bfa_fcs_iprp_s;

/* Rport Features (RPF) */
struct bfa_fcs_rpf_s {
	bfa_sm_t               sm;	/*  state machine */
	struct bfa_fcs_rport_s *rport;	/*  parent rport */
	struct bfa_timer_s 	timer;	/*  general purpose timer */
	struct bfa_fcxp_s 	*fcxp;	/*  FCXP needed for discarding */
	struct bfa_fcxp_wqe_s 	fcxp_wqe;	/*  fcxp wait queue element */
	int             	rpsc_retries;	/*  max RPSC retry attempts */
	enum bfa_pport_speed 	rpsc_speed;	/* Current Speed from RPSC.
						 * O if RPSC fails */
	enum bfa_pport_speed	assigned_speed;	/* Speed assigned by the user.
						 * will be used if RPSC is not
						 * supported by the rport */
};

struct bfa_fcs_rport_s {
	struct list_head         qe;	/*  used by port/vport */
	struct bfa_fcs_port_s *port;	/*  parent FCS port */
	struct bfa_fcs_s      *fcs;	/*  fcs instance */
	struct bfad_rport_s   *rp_drv;	/*  driver peer instance */
	u32        pid;	/*  port ID of rport */
	u16        maxfrsize;	/*  maximum frame size */
	u16        reply_oxid;	/*  OX_ID of inbound requests */
	enum fc_cos        fc_cos;	/*  FC classes of service supp */
	bfa_boolean_t   cisc;	/*  CISC capable device */
	wwn_t           pwwn;	/*  port wwn of rport */
	wwn_t           nwwn;	/*  node wwn of rport */
	struct bfa_rport_symname_s psym_name; /*  port symbolic name  */
	bfa_sm_t        sm;		/*  state machine */
	struct bfa_timer_s timer;	/*  general purpose timer */
	struct bfa_fcs_itnim_s *itnim;	/*  ITN initiator mode role */
	struct bfa_fcs_tin_s *tin;	/*  ITN initiator mode role */
	struct bfa_fcs_iprp_s *iprp;	/*  IP/FC role */
	struct bfa_rport_s *bfa_rport;	/*  BFA Rport */
	struct bfa_fcxp_s *fcxp;	/*  FCXP needed for discarding */
	int             plogi_retries;	/*  max plogi retry attempts */
	int             ns_retries;	/*  max NS query retry attempts */
	struct bfa_fcxp_wqe_s 	fcxp_wqe; /*  fcxp wait queue element */
	struct bfa_rport_stats_s stats;	/*  rport stats */
	enum bfa_rport_function	scsi_function;  /*  Initiator/Target */
	struct bfa_fcs_rpf_s rpf; 	/* Rport features module */
};

static inline struct bfa_rport_s *
bfa_fcs_rport_get_halrport(struct bfa_fcs_rport_s *rport)
{
	return rport->bfa_rport;
}

/**
 * bfa fcs rport API functions
 */
bfa_status_t bfa_fcs_rport_add(struct bfa_fcs_port_s *port, wwn_t *pwwn,
			struct bfa_fcs_rport_s *rport,
			struct bfad_rport_s *rport_drv);
bfa_status_t bfa_fcs_rport_remove(struct bfa_fcs_rport_s *rport);
void bfa_fcs_rport_get_attr(struct bfa_fcs_rport_s *rport,
			struct bfa_rport_attr_s *attr);
void bfa_fcs_rport_get_stats(struct bfa_fcs_rport_s *rport,
			struct bfa_rport_stats_s *stats);
void bfa_fcs_rport_clear_stats(struct bfa_fcs_rport_s *rport);
struct bfa_fcs_rport_s *bfa_fcs_rport_lookup(struct bfa_fcs_port_s *port,
			wwn_t rpwwn);
struct bfa_fcs_rport_s *bfa_fcs_rport_lookup_by_nwwn(
			struct bfa_fcs_port_s *port, wwn_t rnwwn);
void bfa_fcs_rport_set_del_timeout(u8 rport_tmo);
void bfa_fcs_rport_set_speed(struct bfa_fcs_rport_s *rport,
			enum bfa_pport_speed speed);
#endif /* __BFA_FCS_RPORT_H__ */

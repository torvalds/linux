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

#ifndef __BFA_FCS_FABRIC_H__
#define __BFA_FCS_FABRIC_H__

struct bfa_fcs_s;

#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_vf.h>
#include <cs/bfa_q.h>
#include <cs/bfa_sm.h>
#include <defs/bfa_defs_pport.h>
#include <fcs/bfa_fcs_lport.h>
#include <protocol/fc_sp.h>
#include <fcs/bfa_fcs_auth.h>

/*
 * forward declaration
 */
struct bfad_vf_s;

enum bfa_fcs_fabric_type {
	BFA_FCS_FABRIC_UNKNOWN = 0,
	BFA_FCS_FABRIC_SWITCHED = 1,
	BFA_FCS_FABRIC_PLOOP = 2,
	BFA_FCS_FABRIC_N2N = 3,
};


struct bfa_fcs_fabric_s {
	struct list_head   qe;		/*  queue element */
	bfa_sm_t	 sm;		/*  state machine */
	struct bfa_fcs_s *fcs;		/*  FCS instance */
	struct bfa_fcs_port_s  bport;	/*  base logical port */
	enum bfa_fcs_fabric_type fab_type; /*  fabric type */
	enum bfa_pport_type oper_type;	/*  current link topology */
	u8         is_vf;		/*  is virtual fabric? */
	u8         is_npiv;	/*  is NPIV supported ? */
	u8         is_auth;	/*  is Security/Auth supported ? */
	u16        bb_credit;	/*  BB credit from fabric */
	u16        vf_id;		/*  virtual fabric ID */
	u16        num_vports;	/*  num vports */
	u16        rsvd;
	struct list_head         vport_q;	/*  queue of virtual ports */
	struct list_head         vf_q;	/*  queue of virtual fabrics */
	struct bfad_vf_s      *vf_drv;	/*  driver vf structure */
	struct bfa_timer_s link_timer;	/*  Link Failure timer. Vport */
	wwn_t           fabric_name;	/*  attached fabric name */
	bfa_boolean_t   auth_reqd;	/*  authentication required	*/
	struct bfa_timer_s delay_timer;	/*  delay timer		*/
	union {
		u16        swp_vfid;/*  switch port VF id		*/
	} event_arg;
	struct bfa_fcs_auth_s  auth;	/*  authentication config	*/
	struct bfa_wc_s        wc;	/*  wait counter for delete	*/
	struct bfa_vf_stats_s  stats; 	/*  fabric/vf stats		*/
	struct bfa_lps_s	*lps;	/*  lport login services	*/
	u8	fabric_ip_addr[BFA_FCS_FABRIC_IPADDR_SZ];  /*  attached
							    * fabric's ip addr
							    */
};

#define bfa_fcs_fabric_npiv_capable(__f)    ((__f)->is_npiv)
#define bfa_fcs_fabric_is_switched(__f)			\
	((__f)->fab_type == BFA_FCS_FABRIC_SWITCHED)

/**
 *   The design calls for a single implementation of base fabric and vf.
 */
#define bfa_fcs_vf_t struct bfa_fcs_fabric_s

struct bfa_vf_event_s {
	u32        undefined;
};

/**
 * bfa fcs vf public functions
 */
bfa_status_t bfa_fcs_vf_mode_enable(struct bfa_fcs_s *fcs, u16 vf_id);
bfa_status_t bfa_fcs_vf_mode_disable(struct bfa_fcs_s *fcs);
bfa_status_t bfa_fcs_vf_create(bfa_fcs_vf_t *vf, struct bfa_fcs_s *fcs,
			       u16 vf_id, struct bfa_port_cfg_s *port_cfg,
			       struct bfad_vf_s *vf_drv);
bfa_status_t bfa_fcs_vf_delete(bfa_fcs_vf_t *vf);
void bfa_fcs_vf_start(bfa_fcs_vf_t *vf);
bfa_status_t bfa_fcs_vf_stop(bfa_fcs_vf_t *vf);
void bfa_fcs_vf_list(struct bfa_fcs_s *fcs, u16 *vf_ids, int *nvfs);
void bfa_fcs_vf_list_all(struct bfa_fcs_s *fcs, u16 *vf_ids, int *nvfs);
void bfa_fcs_vf_get_attr(bfa_fcs_vf_t *vf, struct bfa_vf_attr_s *vf_attr);
void bfa_fcs_vf_get_stats(bfa_fcs_vf_t *vf,
			  struct bfa_vf_stats_s *vf_stats);
void bfa_fcs_vf_clear_stats(bfa_fcs_vf_t *vf);
void bfa_fcs_vf_get_ports(bfa_fcs_vf_t *vf, wwn_t vpwwn[], int *nports);
bfa_fcs_vf_t *bfa_fcs_vf_lookup(struct bfa_fcs_s *fcs, u16 vf_id);
struct bfad_vf_s *bfa_fcs_vf_get_drv_vf(bfa_fcs_vf_t *vf);

#endif /* __BFA_FCS_FABRIC_H__ */

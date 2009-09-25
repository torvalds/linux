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

#ifndef __BFA_PORT_PRIV_H__
#define __BFA_PORT_PRIV_H__

#include <defs/bfa_defs_pport.h>
#include <bfi/bfi_pport.h>
#include "bfa_intr_priv.h"

/**
 * BFA physical port data structure
 */
struct bfa_pport_s {
	struct bfa_s 		*bfa;	/*  parent BFA instance */
	bfa_sm_t		sm;	/*  port state machine */
	wwn_t			nwwn;	/*  node wwn of physical port */
	wwn_t			pwwn;	/*  port wwn of physical oprt */
	enum bfa_pport_speed speed_sup;
					/*  supported speeds */
	enum bfa_pport_speed speed;	/*  current speed */
	enum bfa_pport_topology topology;	/*  current topology */
	u8			myalpa;	/*  my ALPA in LOOP topology */
	u8			rsvd[3];
	struct bfa_pport_cfg_s	cfg;	/*  current port configuration */
	struct bfa_qos_attr_s  qos_attr;   /* QoS Attributes */
	struct bfa_qos_vc_attr_s qos_vc_attr;  /*  VC info from ELP */
	struct bfa_reqq_wait_s	reqq_wait;
					/*  to wait for room in reqq */
	struct bfa_reqq_wait_s	svcreq_wait;
					/*  to wait for room in reqq */
	struct bfa_reqq_wait_s	stats_reqq_wait;
					/*  to wait for room in reqq (stats) */
	void			*event_cbarg;
	void			(*event_cbfn) (void *cbarg,
						bfa_pport_event_t event);
	union {
		union bfi_pport_i2h_msg_u i2hmsg;
	} event_arg;
	void			*bfad;	/*  BFA driver handle */
	struct bfa_cb_qe_s		hcb_qe;	/*  BFA callback queue elem */
	enum bfa_pport_linkstate	hcb_event;
					/*  link event for callback */
	u32		msgtag;	/*  fimrware msg tag for reply */
	u8			*stats_kva;
	u64		stats_pa;
	union bfa_pport_stats_u *stats;	/*  pport stats */
	u32		mypid:24;
	u32		rsvd_b:8;
	struct bfa_timer_s 	timer;	/*  timer */
	union bfa_pport_stats_u 	*stats_ret;
					/*  driver stats location */
	bfa_status_t		stats_status;
					/*  stats/statsclr status */
	bfa_boolean_t   	stats_busy;
					/*  outstanding stats/statsclr */
	bfa_boolean_t   	stats_qfull;
	bfa_boolean_t   	diag_busy;
					/*  diag busy status */
	bfa_boolean_t   	beacon;
					/*  port beacon status */
	bfa_boolean_t   	link_e2e_beacon;
					/*  link beacon status */
	bfa_cb_pport_t		stats_cbfn;
					/*  driver callback function */
	void			*stats_cbarg;
					/* *!< user callback arg */
};

#define BFA_PORT_MOD(__bfa)	(&(__bfa)->modules.pport)

/*
 * public functions
 */
void	bfa_pport_isr(struct bfa_s *bfa, struct bfi_msg_s *msg);
#endif /* __BFA_PORT_PRIV_H__ */

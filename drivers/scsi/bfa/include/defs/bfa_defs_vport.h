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

#ifndef __BFA_DEFS_VPORT_H__
#define __BFA_DEFS_VPORT_H__

#include <bfa_os_inc.h>
#include <defs/bfa_defs_port.h>
#include <protocol/types.h>

/**
 * VPORT states
 */
enum bfa_vport_state {
	BFA_FCS_VPORT_UNINIT 		= 0,
	BFA_FCS_VPORT_CREATED 		= 1,
	BFA_FCS_VPORT_OFFLINE 		= 1,
	BFA_FCS_VPORT_FDISC_SEND 	= 2,
	BFA_FCS_VPORT_FDISC 		= 3,
	BFA_FCS_VPORT_FDISC_RETRY 	= 4,
	BFA_FCS_VPORT_ONLINE 		= 5,
	BFA_FCS_VPORT_DELETING 		= 6,
	BFA_FCS_VPORT_CLEANUP 		= 6,
	BFA_FCS_VPORT_LOGO_SEND 	= 7,
	BFA_FCS_VPORT_LOGO 			= 8,
	BFA_FCS_VPORT_ERROR			= 9,
	BFA_FCS_VPORT_MAX_STATE,
};

/**
 * vport statistics
 */
struct bfa_vport_stats_s {
	struct bfa_port_stats_s port_stats;	/*  base class (port) stats */
	/*
	 * TODO - remove
	 */

	u32        fdisc_sent;	/*  num fdisc sent */
	u32        fdisc_accepts;	/*  fdisc accepts */
	u32        fdisc_retries;	/*  fdisc retries */
	u32        fdisc_timeouts;	/*  fdisc timeouts */
	u32        fdisc_rsp_err;	/*  fdisc response error */
	u32        fdisc_acc_bad;	/*  bad fdisc accepts */
	u32        fdisc_rejects;	/*  fdisc rejects */
	u32        fdisc_unknown_rsp;
	/*
	 *!< fdisc rsp unknown error
	 */
	u32        fdisc_alloc_wait;/*  fdisc req (fcxp)alloc wait */

	u32        logo_alloc_wait;/*  logo req (fcxp) alloc wait */
	u32        logo_sent;	/*  logo sent */
	u32        logo_accepts;	/*  logo accepts */
	u32        logo_rejects;	/*  logo rejects */
	u32        logo_rsp_err;	/*  logo rsp errors */
	u32        logo_unknown_rsp;
			/*  logo rsp unknown errors */

	u32        fab_no_npiv;	/*  fabric does not support npiv */

	u32        fab_offline;	/*  offline events from fab SM */
	u32        fab_online;	/*  online events from fab SM */
	u32        fab_cleanup;	/*  cleanup request from fab SM */
	u32        rsvd;
};

/**
 * BFA vport attribute returned in queries
 */
struct bfa_vport_attr_s {
	struct bfa_port_attr_s   port_attr; /*  base class (port) attributes */
	enum bfa_vport_state vport_state; /*  vport state */
	u32          rsvd;
};

#endif /* __BFA_DEFS_VPORT_H__ */

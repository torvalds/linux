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

#ifndef __BFA_DEFS_LPORT_H__
#define __BFA_DEFS_LPORT_H__

#include <defs/bfa_defs_types.h>
#include <defs/bfa_defs_port.h>

/**
 * BFA AEN logical port events.
 * Arguments below are in BFAL context from Mgmt
 * BFA_LPORT_AEN_NEW:       [in]: None         [out]: vf_id, ppwwn, lpwwn, roles
 * BFA_LPORT_AEN_DELETE:    [in]: lpwwn        [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_ONLINE:    [in]: lpwwn        [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_OFFLINE:   [in]: lpwwn        [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_DISCONNECT:[in]: lpwwn        [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_NEW_PROP:  [in]: None         [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_DELETE_PROP:     [in]: lpwwn  [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_NEW_STANDARD:    [in]: None   [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_DELETE_STANDARD: [in]: lpwwn  [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_NPIV_DUP_WWN:    [in]: lpwwn  [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_NPIV_FABRIC_MAX: [in]: lpwwn  [out]: vf_id, ppwwn. lpwwn, roles
 * BFA_LPORT_AEN_NPIV_UNKNOWN:    [in]: lpwwn  [out]: vf_id, ppwwn. lpwwn, roles
 */
enum bfa_lport_aen_event {
	BFA_LPORT_AEN_NEW	= 1,	/*  LPort created event */
	BFA_LPORT_AEN_DELETE	= 2,	/*  LPort deleted event */
	BFA_LPORT_AEN_ONLINE	= 3,	/*  LPort online event */
	BFA_LPORT_AEN_OFFLINE	= 4,	/*  LPort offline event */
	BFA_LPORT_AEN_DISCONNECT = 5,	/*  LPort disconnect event */
	BFA_LPORT_AEN_NEW_PROP	= 6,	/*  VPort created event */
	BFA_LPORT_AEN_DELETE_PROP = 7,	/*  VPort deleted event */
	BFA_LPORT_AEN_NEW_STANDARD = 8,	/*  VPort created event */
	BFA_LPORT_AEN_DELETE_STANDARD = 9,  /*  VPort deleted event */
	BFA_LPORT_AEN_NPIV_DUP_WWN = 10,    /*  VPort configured with
					     *   duplicate WWN event
						 */
	BFA_LPORT_AEN_NPIV_FABRIC_MAX = 11, /*  Max NPIV in fabric/fport */
	BFA_LPORT_AEN_NPIV_UNKNOWN = 12, /*  Unknown NPIV Error code event */
};

/**
 * BFA AEN event data structure
 */
struct bfa_lport_aen_data_s {
	u16        vf_id;	/*  vf_id of this logical port */
	u16        rsvd;
	enum bfa_port_role roles;	/*  Logical port mode,IM/TM/IP etc */
	wwn_t           ppwwn;	/*  WWN of its physical port */
	wwn_t           lpwwn;	/*  WWN of this logical port */
};

#endif /* __BFA_DEFS_LPORT_H__ */

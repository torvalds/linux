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

#ifndef __BFA_DEFS_PORT_H__
#define __BFA_DEFS_PORT_H__

#include <bfa_os_inc.h>
#include <protocol/types.h>
#include <defs/bfa_defs_pport.h>
#include <defs/bfa_defs_ioc.h>

#define BFA_FCS_FABRIC_IPADDR_SZ  16

/**
 * symbolic names for base port/virtual port
 */
#define BFA_SYMNAME_MAXLEN	128	/* vmware/windows uses 128 bytes */
struct bfa_port_symname_s {
	char            symname[BFA_SYMNAME_MAXLEN];
};

/**
* Roles of FCS port:
 *     - FCP IM and FCP TM roles cannot be enabled together for a FCS port
 *     - Create multiple ports if both IM and TM functions required.
 *     - Atleast one role must be specified.
 */
enum bfa_port_role {
	BFA_PORT_ROLE_FCP_IM 	= 0x01,	/*  FCP initiator role */
	BFA_PORT_ROLE_FCP_TM 	= 0x02,	/*  FCP target role */
	BFA_PORT_ROLE_FCP_IPFC 	= 0x04,	/*  IP over FC role */
	BFA_PORT_ROLE_FCP_MAX 	= BFA_PORT_ROLE_FCP_IPFC | BFA_PORT_ROLE_FCP_IM
};

/**
 * FCS port configuration.
 */
struct bfa_port_cfg_s {
    wwn_t               pwwn;       /*  port wwn */
    wwn_t               nwwn;       /*  node wwn */
    struct bfa_port_symname_s  sym_name;   /*  vm port symbolic name */
    enum bfa_port_role     roles;      /*  FCS port roles */
	u32			rsvd;
    u8             tag[16];	/*  opaque tag from application */
};

/**
 * FCS port states
 */
enum bfa_port_state {
	BFA_PORT_UNINIT  = 0,	/*  PORT is not yet initialized */
	BFA_PORT_FDISC   = 1,	/*  FDISC is in progress */
	BFA_PORT_ONLINE  = 2,	/*  login to fabric is complete */
	BFA_PORT_OFFLINE = 3,	/*  No login to fabric */
};

/**
 * FCS port type. Required for VmWare.
 */
enum bfa_port_type {
	BFA_PORT_TYPE_PHYSICAL = 0,
	BFA_PORT_TYPE_VIRTUAL,
};

/**
 * FCS port offline reason. Required for VmWare.
 */
enum bfa_port_offline_reason {
	BFA_PORT_OFFLINE_UNKNOWN = 0,
	BFA_PORT_OFFLINE_LINKDOWN,
	BFA_PORT_OFFLINE_FAB_UNSUPPORTED,	/*  NPIV not supported by the
						 *    fabric */
	BFA_PORT_OFFLINE_FAB_NORESOURCES,
	BFA_PORT_OFFLINE_FAB_LOGOUT,
};

/**
 * FCS lport info. Required for VmWare.
 */
struct bfa_port_info_s {
	u8         port_type;	/* bfa_port_type_t : physical or
					 * virtual */
	u8         port_state;	/* one of bfa_port_state values */
	u8         offline_reason;	/* one of bfa_port_offline_reason_t
					 * values */
	wwn_t           port_wwn;
	wwn_t           node_wwn;

	/*
	 * following 4 feilds are valid for Physical Ports only
	 */
	u32        max_vports_supp;	/* Max supported vports */
	u32        num_vports_inuse;	/* Num of in use vports */
	u32        max_rports_supp;	/* Max supported rports */
	u32        num_rports_inuse;	/* Num of doscovered rports */

};

/**
 * FCS port statistics
 */
struct bfa_port_stats_s {
	u32        ns_plogi_sent;
	u32        ns_plogi_rsp_err;
	u32        ns_plogi_acc_err;
	u32        ns_plogi_accepts;
	u32        ns_rejects;	/* NS command rejects */
	u32        ns_plogi_unknown_rsp;
	u32        ns_plogi_alloc_wait;

	u32        ns_retries;	/* NS command retries */
	u32        ns_timeouts;	/* NS command timeouts */

	u32        ns_rspnid_sent;
	u32        ns_rspnid_accepts;
	u32        ns_rspnid_rsp_err;
	u32        ns_rspnid_rejects;
	u32        ns_rspnid_alloc_wait;

	u32        ns_rftid_sent;
	u32        ns_rftid_accepts;
	u32        ns_rftid_rsp_err;
	u32        ns_rftid_rejects;
	u32        ns_rftid_alloc_wait;

	u32	ns_rffid_sent;
	u32	ns_rffid_accepts;
	u32	ns_rffid_rsp_err;
	u32	ns_rffid_rejects;
	u32	ns_rffid_alloc_wait;

	u32        ns_gidft_sent;
	u32        ns_gidft_accepts;
	u32        ns_gidft_rsp_err;
	u32        ns_gidft_rejects;
	u32        ns_gidft_unknown_rsp;
	u32        ns_gidft_alloc_wait;

	/*
	 * Mgmt Server stats
	 */
	u32        ms_retries;	/* MS command retries */
	u32        ms_timeouts;	/* MS command timeouts */
	u32        ms_plogi_sent;
	u32        ms_plogi_rsp_err;
	u32        ms_plogi_acc_err;
	u32        ms_plogi_accepts;
	u32        ms_rejects;	/* NS command rejects */
	u32        ms_plogi_unknown_rsp;
	u32        ms_plogi_alloc_wait;

	u32        num_rscn;	/* Num of RSCN received */
	u32        num_portid_rscn;/* Num portid format RSCN
								* received */

	u32	uf_recvs; 	/* unsolicited recv frames      */
	u32	uf_recv_drops; 	/* dropped received frames	*/

	u32	rsvd; 		/* padding for 64 bit alignment */
};

/**
 * BFA port attribute returned in queries
 */
struct bfa_port_attr_s {
	enum bfa_port_state state;		/*  port state */
	u32         pid;		/*  port ID */
	struct bfa_port_cfg_s   port_cfg;	/*  port configuration */
	enum bfa_pport_type port_type;	/*  current topology */
	u32         loopback;	/*  cable is externally looped back */
	wwn_t		fabric_name; /*  attached switch's nwwn */
	u8		fabric_ip_addr[BFA_FCS_FABRIC_IPADDR_SZ]; /*  attached
							* fabric's ip addr */
};

/**
 * BFA physical port Level events
 * Arguments below are in BFAL context from Mgmt
 * BFA_PORT_AEN_ONLINE:     [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_OFFLINE:    [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_RLIR:       [in]: None	[out]: pwwn, rlir_data, rlir_len
 * BFA_PORT_AEN_SFP_INSERT: [in]: pwwn	[out]: port_id, pwwn
 * BFA_PORT_AEN_SFP_REMOVE: [in]: pwwn	[out]: port_id, pwwn
 * BFA_PORT_AEN_SFP_POM:    [in]: pwwn	[out]: level, port_id, pwwn
 * BFA_PORT_AEN_ENABLE:     [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_DISABLE:    [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_AUTH_ON:    [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_AUTH_OFF:   [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_DISCONNECT: [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_QOS_NEG:    [in]: pwwn	[out]: pwwn
 * BFA_PORT_AEN_FABRIC_NAME_CHANGE: [in]: pwwn, [out]: pwwn, fwwn
 *
 */
enum bfa_port_aen_event {
	BFA_PORT_AEN_ONLINE     = 1,	/*  Physical Port online event */
	BFA_PORT_AEN_OFFLINE    = 2,	/*  Physical Port offline event */
	BFA_PORT_AEN_RLIR       = 3,	/*  RLIR event, not supported */
	BFA_PORT_AEN_SFP_INSERT = 4,	/*  SFP inserted event */
	BFA_PORT_AEN_SFP_REMOVE = 5,	/*  SFP removed event */
	BFA_PORT_AEN_SFP_POM    = 6,	/*  SFP POM event */
	BFA_PORT_AEN_ENABLE     = 7,	/*  Physical Port enable event */
	BFA_PORT_AEN_DISABLE    = 8,	/*  Physical Port disable event */
	BFA_PORT_AEN_AUTH_ON    = 9,	/*  Physical Port auth success event */
	BFA_PORT_AEN_AUTH_OFF   = 10,	/*  Physical Port auth fail event */
	BFA_PORT_AEN_DISCONNECT = 11,	/*  Physical Port disconnect event */
	BFA_PORT_AEN_QOS_NEG    = 12,  	/*  Base Port QOS negotiation event */
	BFA_PORT_AEN_FABRIC_NAME_CHANGE = 13, /*  Fabric Name/WWN change
					       * event */
	BFA_PORT_AEN_SFP_ACCESS_ERROR = 14, /*  SFP read error event */
	BFA_PORT_AEN_SFP_UNSUPPORT = 15, /*  Unsupported SFP event */
};

enum bfa_port_aen_sfp_pom {
	BFA_PORT_AEN_SFP_POM_GREEN = 1,	/*  Normal */
	BFA_PORT_AEN_SFP_POM_AMBER = 2,	/*  Warning */
	BFA_PORT_AEN_SFP_POM_RED   = 3,	/*  Critical */
	BFA_PORT_AEN_SFP_POM_MAX   = BFA_PORT_AEN_SFP_POM_RED
};

struct bfa_port_aen_data_s {
	enum bfa_ioc_type_e ioc_type;
	wwn_t           pwwn;	      /*  WWN of the physical port */
	wwn_t           fwwn;	      /*  WWN of the fabric port */
	mac_t           mac;	      /*  MAC addres of the ethernet port,
				       * applicable to CNA port only */
	int             phy_port_num; /*! For SFP related events */
	enum bfa_port_aen_sfp_pom level; /*  Only transitions will
					  * be informed */
};

#endif /* __BFA_DEFS_PORT_H__ */

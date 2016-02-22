/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
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

#ifndef __BFA_DEFS_FCS_H__
#define __BFA_DEFS_FCS_H__

#include "bfa_fc.h"
#include "bfa_defs_svc.h"

/*
 * VF states
 */
enum bfa_vf_state {
	BFA_VF_UNINIT    = 0,	/*  fabric is not yet initialized */
	BFA_VF_LINK_DOWN = 1,	/*  link is down */
	BFA_VF_FLOGI     = 2,	/*  flogi is in progress */
	BFA_VF_AUTH      = 3,	/*  authentication in progress */
	BFA_VF_NOFABRIC  = 4,	/*  fabric is not present */
	BFA_VF_ONLINE    = 5,	/*  login to fabric is complete */
	BFA_VF_EVFP      = 6,	/*  EVFP is in progress */
	BFA_VF_ISOLATED  = 7,	/*  port isolated due to vf_id mismatch */
};

/*
 * VF statistics
 */
struct bfa_vf_stats_s {
	u32	flogi_sent;	/*  Num FLOGIs sent */
	u32	flogi_rsp_err;	/*  FLOGI response errors */
	u32	flogi_acc_err;	/*  FLOGI accept errors */
	u32	flogi_accepts;	/*  FLOGI accepts received */
	u32	flogi_rejects;	/*  FLOGI rejects received */
	u32	flogi_unknown_rsp; /*  Unknown responses for FLOGI */
	u32	flogi_alloc_wait; /*  Allocation waits prior to sending FLOGI */
	u32	flogi_rcvd;	/*  FLOGIs received */
	u32	flogi_rejected;	/*  Incoming FLOGIs rejected */
	u32	fabric_onlines;	/*  Internal fabric online notification sent
				 *  to other modules */
	u32	fabric_offlines; /* Internal fabric offline notification sent
				  * to other modules */
	u32	resvd; /*  padding for 64 bit alignment */
};

/*
 * VF attributes returned in queries
 */
struct bfa_vf_attr_s {
	enum bfa_vf_state  state;		/*  VF state */
	u32        rsvd;
	wwn_t           fabric_name;	/*  fabric name */
};

#define BFA_FCS_MAX_LPORTS 256
#define BFA_FCS_FABRIC_IPADDR_SZ  16

/*
 * symbolic names for base port/virtual port
 */
#define BFA_SYMNAME_MAXLEN	128	/* 128 bytes */
struct bfa_lport_symname_s {
	char	    symname[BFA_SYMNAME_MAXLEN];
};

/*
* Roles of FCS port:
 *     - FCP IM and FCP TM roles cannot be enabled together for a FCS port
 *     - Create multiple ports if both IM and TM functions required.
 *     - Atleast one role must be specified.
 */
enum bfa_lport_role {
	BFA_LPORT_ROLE_FCP_IM	= 0x01,	/*  FCP initiator role */
	BFA_LPORT_ROLE_FCP_MAX	= BFA_LPORT_ROLE_FCP_IM,
};

/*
 * FCS port configuration.
 */
struct bfa_lport_cfg_s {
	wwn_t	       pwwn;       /*  port wwn */
	wwn_t	       nwwn;       /*  node wwn */
	struct bfa_lport_symname_s  sym_name;   /*  vm port symbolic name */
	struct bfa_lport_symname_s node_sym_name; /* Node symbolic name */
	enum bfa_lport_role roles;      /* FCS port roles */
	u32     rsvd;
	bfa_boolean_t   preboot_vp;  /*  vport created from PBC */
	u8	tag[16];        /* opaque tag from application */
	u8	padding[4];
};

/*
 * FCS port states
 */
enum bfa_lport_state {
	BFA_LPORT_UNINIT  = 0,	/*  PORT is not yet initialized */
	BFA_LPORT_FDISC   = 1,	/*  FDISC is in progress */
	BFA_LPORT_ONLINE  = 2,	/*  login to fabric is complete */
	BFA_LPORT_OFFLINE = 3,	/*  No login to fabric */
};

/*
 * FCS port type.
 */
enum bfa_lport_type {
	BFA_LPORT_TYPE_PHYSICAL = 0,
	BFA_LPORT_TYPE_VIRTUAL,
};

/*
 * FCS port offline reason.
 */
enum bfa_lport_offline_reason {
	BFA_LPORT_OFFLINE_UNKNOWN = 0,
	BFA_LPORT_OFFLINE_LINKDOWN,
	BFA_LPORT_OFFLINE_FAB_UNSUPPORTED,	/*  NPIV not supported by the
	 *    fabric */
	BFA_LPORT_OFFLINE_FAB_NORESOURCES,
	BFA_LPORT_OFFLINE_FAB_LOGOUT,
};

/*
 * FCS lport info.
 */
struct bfa_lport_info_s {
	u8	 port_type;	/* bfa_lport_type_t : physical or
	 * virtual */
	u8	 port_state;	/* one of bfa_lport_state values */
	u8	 offline_reason;	/* one of bfa_lport_offline_reason_t
	 * values */
	wwn_t	   port_wwn;
	wwn_t	   node_wwn;

	/*
	 * following 4 feilds are valid for Physical Ports only
	 */
	u32	max_vports_supp;	/* Max supported vports */
	u32	num_vports_inuse;	/* Num of in use vports */
	u32	max_rports_supp;	/* Max supported rports */
	u32	num_rports_inuse;	/* Num of doscovered rports */

};

/*
 * FCS port statistics
 */
struct bfa_lport_stats_s {
	u32	ns_plogi_sent;
	u32	ns_plogi_rsp_err;
	u32	ns_plogi_acc_err;
	u32	ns_plogi_accepts;
	u32	ns_rejects;	/* NS command rejects */
	u32	ns_plogi_unknown_rsp;
	u32	ns_plogi_alloc_wait;

	u32	ns_retries;	/* NS command retries */
	u32	ns_timeouts;	/* NS command timeouts */

	u32	ns_rspnid_sent;
	u32	ns_rspnid_accepts;
	u32	ns_rspnid_rsp_err;
	u32	ns_rspnid_rejects;
	u32	ns_rspnid_alloc_wait;

	u32	ns_rftid_sent;
	u32	ns_rftid_accepts;
	u32	ns_rftid_rsp_err;
	u32	ns_rftid_rejects;
	u32	ns_rftid_alloc_wait;

	u32	ns_rffid_sent;
	u32	ns_rffid_accepts;
	u32	ns_rffid_rsp_err;
	u32	ns_rffid_rejects;
	u32	ns_rffid_alloc_wait;

	u32	ns_gidft_sent;
	u32	ns_gidft_accepts;
	u32	ns_gidft_rsp_err;
	u32	ns_gidft_rejects;
	u32	ns_gidft_unknown_rsp;
	u32	ns_gidft_alloc_wait;

	u32	ns_rnnid_sent;
	u32	ns_rnnid_accepts;
	u32	ns_rnnid_rsp_err;
	u32	ns_rnnid_rejects;
	u32	ns_rnnid_alloc_wait;

	u32	ns_rsnn_nn_sent;
	u32	ns_rsnn_nn_accepts;
	u32	ns_rsnn_nn_rsp_err;
	u32	ns_rsnn_nn_rejects;
	u32	ns_rsnn_nn_alloc_wait;

	/*
	 * Mgmt Server stats
	 */
	u32	ms_retries;	/* MS command retries */
	u32	ms_timeouts;	/* MS command timeouts */
	u32	ms_plogi_sent;
	u32	ms_plogi_rsp_err;
	u32	ms_plogi_acc_err;
	u32	ms_plogi_accepts;
	u32	ms_rejects;	/* MS command rejects */
	u32	ms_plogi_unknown_rsp;
	u32	ms_plogi_alloc_wait;

	u32	num_rscn;	/* Num of RSCN received */
	u32	num_portid_rscn;/* Num portid format RSCN
	* received */

	u32	uf_recvs;	/* Unsolicited recv frames	*/
	u32	uf_recv_drops;	/* Dropped received frames	*/

	u32	plogi_rcvd;	/* Received plogi	*/
	u32	prli_rcvd;	/* Received prli	*/
	u32	adisc_rcvd;	/* Received adisc	*/
	u32	prlo_rcvd;	/* Received prlo	*/
	u32	logo_rcvd;	/* Received logo	*/
	u32	rpsc_rcvd;	/* Received rpsc	*/
	u32	un_handled_els_rcvd;	/* Received unhandled ELS	*/
	u32	rport_plogi_timeouts; /* Rport plogi retry timeout count */
	u32	rport_del_max_plogi_retry; /* Deleted rport
					    * (max retry of plogi) */
};

/*
 * BFA port attribute returned in queries
 */
struct bfa_lport_attr_s {
	enum bfa_lport_state state;	/*  port state */
	u32	 pid;	/*  port ID */
	struct bfa_lport_cfg_s   port_cfg;	/*  port configuration */
	enum bfa_port_type port_type;	/*  current topology */
	u32	 loopback;	/*  cable is externally looped back */
	wwn_t	fabric_name; /*  attached switch's nwwn */
	u8	fabric_ip_addr[BFA_FCS_FABRIC_IPADDR_SZ]; /*  attached
	* fabric's ip addr */
	mac_t	   fpma_mac;	/*  Lport's FPMA Mac address */
	u16	authfail;	/*  auth failed state */
};


/*
 * VPORT states
 */
enum bfa_vport_state {
	BFA_FCS_VPORT_UNINIT		= 0,
	BFA_FCS_VPORT_CREATED		= 1,
	BFA_FCS_VPORT_OFFLINE		= 1,
	BFA_FCS_VPORT_FDISC_SEND	= 2,
	BFA_FCS_VPORT_FDISC		= 3,
	BFA_FCS_VPORT_FDISC_RETRY	= 4,
	BFA_FCS_VPORT_FDISC_RSP_WAIT	= 5,
	BFA_FCS_VPORT_ONLINE		= 6,
	BFA_FCS_VPORT_DELETING		= 7,
	BFA_FCS_VPORT_CLEANUP		= 8,
	BFA_FCS_VPORT_LOGO_SEND		= 9,
	BFA_FCS_VPORT_LOGO		= 10,
	BFA_FCS_VPORT_ERROR		= 11,
	BFA_FCS_VPORT_MAX_STATE,
};

/*
 * vport statistics
 */
struct bfa_vport_stats_s {
	struct bfa_lport_stats_s port_stats;	/*  base class (port) stats */
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

/*
 * BFA vport attribute returned in queries
 */
struct bfa_vport_attr_s {
	struct bfa_lport_attr_s   port_attr; /*  base class (port) attributes */
	enum bfa_vport_state vport_state; /*  vport state */
	u32          rsvd;
};

/*
 * FCS remote port states
 */
enum bfa_rport_state {
	BFA_RPORT_UNINIT	= 0,	/*  PORT is not yet initialized */
	BFA_RPORT_OFFLINE	= 1,	/*  rport is offline */
	BFA_RPORT_PLOGI		= 2,	/*  PLOGI to rport is in progress */
	BFA_RPORT_ONLINE	= 3,	/*  login to rport is complete */
	BFA_RPORT_PLOGI_RETRY	= 4,	/*  retrying login to rport */
	BFA_RPORT_NSQUERY	= 5,	/*  nameserver query */
	BFA_RPORT_ADISC		= 6,	/*  ADISC authentication */
	BFA_RPORT_LOGO		= 7,	/*  logging out with rport */
	BFA_RPORT_LOGORCV	= 8,	/*  handling LOGO from rport */
	BFA_RPORT_NSDISC	= 9,	/*  re-discover rport */
};

/*
 *  Rport Scsi Function : Initiator/Target.
 */
enum bfa_rport_function {
	BFA_RPORT_INITIATOR	= 0x01,	/*  SCSI Initiator	*/
	BFA_RPORT_TARGET	= 0x02,	/*  SCSI Target	*/
};

/*
 * port/node symbolic names for rport
 */
#define BFA_RPORT_SYMNAME_MAXLEN	255
struct bfa_rport_symname_s {
	char            symname[BFA_RPORT_SYMNAME_MAXLEN];
};

/*
 * FCS remote port statistics
 */
struct bfa_rport_stats_s {
	u32        offlines;           /*  remote port offline count  */
	u32        onlines;            /*  remote port online count   */
	u32        rscns;              /*  RSCN affecting rport       */
	u32        plogis;		    /*  plogis sent                */
	u32        plogi_accs;	    /*  plogi accepts              */
	u32        plogi_timeouts;	    /*  plogi timeouts             */
	u32        plogi_rejects;	    /*  rcvd plogi rejects         */
	u32        plogi_failed;	    /*  local failure              */
	u32        plogi_rcvd;	    /*  plogis rcvd                */
	u32        prli_rcvd;          /*  inbound PRLIs              */
	u32        adisc_rcvd;         /*  ADISCs received            */
	u32        adisc_rejects;      /*  recvd  ADISC rejects       */
	u32        adisc_sent;         /*  ADISC requests sent        */
	u32        adisc_accs;         /*  ADISC accepted by rport    */
	u32        adisc_failed;       /*  ADISC failed (no response) */
	u32        adisc_rejected;     /*  ADISC rejected by us    */
	u32        logos;              /*  logos sent                 */
	u32        logo_accs;          /*  LOGO accepts from rport    */
	u32        logo_failed;        /*  LOGO failures              */
	u32        logo_rejected;      /*  LOGO rejects from rport    */
	u32        logo_rcvd;          /*  LOGO from remote port      */

	u32        rpsc_rcvd;         /*  RPSC received            */
	u32        rpsc_rejects;      /*  recvd  RPSC rejects       */
	u32        rpsc_sent;         /*  RPSC requests sent        */
	u32        rpsc_accs;         /*  RPSC accepted by rport    */
	u32        rpsc_failed;       /*  RPSC failed (no response) */
	u32        rpsc_rejected;     /*  RPSC rejected by us    */

	u32	rjt_insuff_res;	/*  LS RJT with insuff resources */
	struct bfa_rport_hal_stats_s	hal_stats;  /*  BFA rport stats    */
};

/*
 * FCS remote port attributes returned in queries
 */
struct bfa_rport_attr_s {
	wwn_t		nwwn;	/*  node wwn */
	wwn_t		pwwn;	/*  port wwn */
	enum fc_cos cos_supported;	/*  supported class of services */
	u32		pid;	/*  port ID */
	u32		df_sz;	/*  Max payload size */
	enum bfa_rport_state	state;	/*  Rport State machine state */
	enum fc_cos	fc_cos;	/*  FC classes of services */
	bfa_boolean_t	cisc;	/*  CISC capable device */
	struct bfa_rport_symname_s symname; /*  Symbolic Name */
	enum bfa_rport_function	scsi_function; /*  Initiator/Target */
	struct bfa_rport_qos_attr_s qos_attr; /*  qos attributes  */
	enum bfa_port_speed curr_speed;   /*  operating speed got from
					    * RPSC ELS. UNKNOWN, if RPSC
					    * is not supported */
	bfa_boolean_t	trl_enforced;	/*  TRL enforced ? TRUE/FALSE */
	enum bfa_port_speed	assigned_speed;	/* Speed assigned by the user.
						 * will be used if RPSC is not
						 * supported by the rport */
};

struct bfa_rport_remote_link_stats_s {
	u32 lfc; /*  Link Failure Count */
	u32 lsyc; /*  Loss of Synchronization Count */
	u32 lsic; /*  Loss of Signal Count */
	u32 pspec; /*  Primitive Sequence Protocol Error Count */
	u32 itwc; /*  Invalid Transmission Word Count */
	u32 icc; /*  Invalid CRC Count */
};

struct bfa_rport_qualifier_s {
	wwn_t	pwwn;	/* Port WWN */
	u32	pid;	/* port ID */
	u32	rsvd;
};

#define BFA_MAX_IO_INDEX 7
#define BFA_NO_IO_INDEX 9

/*
 * FCS itnim states
 */
enum bfa_itnim_state {
	BFA_ITNIM_OFFLINE	= 0,	/*  offline */
	BFA_ITNIM_PRLI_SEND	= 1,	/*  prli send */
	BFA_ITNIM_PRLI_SENT	= 2,	/*  prli sent */
	BFA_ITNIM_PRLI_RETRY	= 3,	/*  prli retry */
	BFA_ITNIM_HCB_ONLINE	= 4,	/*  online callback */
	BFA_ITNIM_ONLINE	= 5,	/*  online */
	BFA_ITNIM_HCB_OFFLINE	= 6,	/*  offline callback */
	BFA_ITNIM_INITIATIOR	= 7,	/*  initiator */
};

/*
 * FCS remote port statistics
 */
struct bfa_itnim_stats_s {
	u32        onlines;	/*  num rport online */
	u32        offlines;	/*  num rport offline */
	u32        prli_sent;	/*  num prli sent out */
	u32        fcxp_alloc_wait;/*  num fcxp alloc waits */
	u32        prli_rsp_err;	/*  num prli rsp errors */
	u32        prli_rsp_acc;	/*  num prli rsp accepts */
	u32        initiator;	/*  rport is an initiator */
	u32        prli_rsp_parse_err;	/*  prli rsp parsing errors */
	u32        prli_rsp_rjt;	/*  num prli rsp rejects */
	u32        timeout;	/*  num timeouts detected */
	u32        sler;		/*  num sler notification from BFA */
	u32	rsvd;		/* padding for 64 bit alignment */
};

/*
 * FCS itnim attributes returned in queries
 */
struct bfa_itnim_attr_s {
	enum bfa_itnim_state state; /*  FCS itnim state        */
	u8 retry;		/*  data retransmision support */
	u8	task_retry_id;  /*  task retry ident support   */
	u8 rec_support;    /*  REC supported              */
	u8 conf_comp;      /*  confirmed completion supp  */
};

#endif /* __BFA_DEFS_FCS_H__ */

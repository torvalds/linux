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

#ifndef __BFA_DEFS_RPORT_H__
#define __BFA_DEFS_RPORT_H__

#include <bfa_os_inc.h>
#include <protocol/types.h>
#include <defs/bfa_defs_pport.h>
#include <defs/bfa_defs_port.h>
#include <defs/bfa_defs_qos.h>

/**
 * FCS remote port states
 */
enum bfa_rport_state {
	BFA_RPORT_UNINIT 	= 0,	/*  PORT is not yet initialized */
	BFA_RPORT_OFFLINE 	= 1,	/*  rport is offline */
	BFA_RPORT_PLOGI 	= 2,	/*  PLOGI to rport is in progress */
	BFA_RPORT_ONLINE 	= 3,	/*  login to rport is complete */
	BFA_RPORT_PLOGI_RETRY 	= 4,	/*  retrying login to rport */
	BFA_RPORT_NSQUERY 	= 5,	/*  nameserver query */
	BFA_RPORT_ADISC 	= 6,	/*  ADISC authentication */
	BFA_RPORT_LOGO 		= 7,	/*  logging out with rport */
	BFA_RPORT_LOGORCV 	= 8,	/*  handling LOGO from rport */
	BFA_RPORT_NSDISC 	= 9,	/*  re-discover rport */
};

/**
 *  Rport Scsi Function : Initiator/Target.
 */
enum bfa_rport_function {
	BFA_RPORT_INITIATOR 	= 0x01,	/*  SCSI Initiator	*/
	BFA_RPORT_TARGET 	= 0x02,	/*  SCSI Target	*/
};

/**
 * port/node symbolic names for rport
 */
#define BFA_RPORT_SYMNAME_MAXLEN	255
struct bfa_rport_symname_s {
	char            symname[BFA_RPORT_SYMNAME_MAXLEN];
};

struct bfa_rport_hal_stats_s {
	u32        sm_un_cr;	    /*  uninit: create events      */
	u32        sm_un_unexp;	    /*  uninit: exception events   */
	u32        sm_cr_on;	    /*  created: online events     */
	u32        sm_cr_del;	    /*  created: delete events     */
	u32        sm_cr_hwf;	    /*  created: IOC down          */
	u32        sm_cr_unexp;	    /*  created: exception events  */
	u32        sm_fwc_rsp;	    /*  fw create: f/w responses   */
	u32        sm_fwc_del;	    /*  fw create: delete events   */
	u32        sm_fwc_off;	    /*  fw create: offline events  */
	u32        sm_fwc_hwf;	    /*  fw create: IOC down        */
	u32        sm_fwc_unexp;	    /*  fw create: exception events*/
	u32        sm_on_off;	    /*  online: offline events     */
	u32        sm_on_del;	    /*  online: delete events      */
	u32        sm_on_hwf;	    /*  online: IOC down events    */
	u32        sm_on_unexp;	    /*  online: exception events   */
	u32        sm_fwd_rsp;	    /*  fw delete: fw responses    */
	u32        sm_fwd_del;	    /*  fw delete: delete events   */
	u32        sm_fwd_hwf;	    /*  fw delete: IOC down events */
	u32        sm_fwd_unexp;	    /*  fw delete: exception events*/
	u32        sm_off_del;	    /*  offline: delete events     */
	u32        sm_off_on;	    /*  offline: online events     */
	u32        sm_off_hwf;	    /*  offline: IOC down events   */
	u32        sm_off_unexp;	    /*  offline: exception events  */
	u32        sm_del_fwrsp;	    /*  delete: fw responses       */
	u32        sm_del_hwf;	    /*  delete: IOC down events    */
	u32        sm_del_unexp;	    /*  delete: exception events   */
	u32        sm_delp_fwrsp;	    /*  delete pend: fw responses  */
	u32        sm_delp_hwf;	    /*  delete pend: IOC downs     */
	u32        sm_delp_unexp;	    /*  delete pend: exceptions    */
	u32        sm_offp_fwrsp;	    /*  off-pending: fw responses  */
	u32        sm_offp_del;	    /*  off-pending: deletes       */
	u32        sm_offp_hwf;	    /*  off-pending: IOC downs     */
	u32        sm_offp_unexp;	    /*  off-pending: exceptions    */
	u32        sm_iocd_off;	    /*  IOC down: offline events   */
	u32        sm_iocd_del;	    /*  IOC down: delete events    */
	u32        sm_iocd_on;	    /*  IOC down: online events    */
	u32        sm_iocd_unexp;	    /*  IOC down: exceptions       */
	u32        rsvd;
};

/**
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

	u32        rsvd;
	struct bfa_rport_hal_stats_s	hal_stats;  /*  BFA rport stats    */
};

/**
 *  Rport's QoS attributes
 */
struct bfa_rport_qos_attr_s {
	enum bfa_qos_priority qos_priority;  /*  rport's QoS priority   */
	u32	       qos_flow_id;	  /*  QoS flow Id	 */
};

/**
 * FCS remote port attributes returned in queries
 */
struct bfa_rport_attr_s {
	wwn_t           	nwwn;	/*  node wwn */
	wwn_t           	pwwn;	/*  port wwn */
	enum fc_cos cos_supported;	/*  supported class of services */
	u32        	pid;	/*  port ID */
	u32        	df_sz;	/*  Max payload size */
	enum bfa_rport_state 	state;	/*  Rport State machine state */
	enum fc_cos        	fc_cos;	/*  FC classes of services */
	bfa_boolean_t   	cisc;	/*  CISC capable device */
	struct bfa_rport_symname_s symname; /*  Symbolic Name */
	enum bfa_rport_function	scsi_function; /*  Initiator/Target */
	struct bfa_rport_qos_attr_s qos_attr; /*  qos attributes  */
	enum bfa_pport_speed curr_speed;   /*  operating speed got from
					    * RPSC ELS. UNKNOWN, if RPSC
					    * is not supported */
	bfa_boolean_t 	trl_enforced;	/*  TRL enforced ? TRUE/FALSE */
	enum bfa_pport_speed	assigned_speed;	/* Speed assigned by the user.
						 * will be used if RPSC is not
						 * supported by the rport */
};

#define bfa_rport_aen_qos_data_t struct bfa_rport_qos_attr_s

/**
 * BFA remote port events
 * Arguments below are in BFAL context from Mgmt
 * BFA_RPORT_AEN_ONLINE:    [in]: lpwwn	[out]: vf_id, lpwwn, rpwwn
 * BFA_RPORT_AEN_OFFLINE:   [in]: lpwwn [out]: vf_id, lpwwn, rpwwn
 * BFA_RPORT_AEN_DISCONNECT:[in]: lpwwn [out]: vf_id, lpwwn, rpwwn
 * BFA_RPORT_AEN_QOS_PRIO:  [in]: lpwwn [out]: vf_id, lpwwn, rpwwn, prio
 * BFA_RPORT_AEN_QOS_FLOWID:[in]: lpwwn [out]: vf_id, lpwwn, rpwwn, flow_id
 */
enum bfa_rport_aen_event {
	BFA_RPORT_AEN_ONLINE      = 1,	/*  RPort online event */
	BFA_RPORT_AEN_OFFLINE     = 2,	/*  RPort offline event */
	BFA_RPORT_AEN_DISCONNECT  = 3,	/*  RPort disconnect event */
	BFA_RPORT_AEN_QOS_PRIO    = 4,	/*  QOS priority change event */
	BFA_RPORT_AEN_QOS_FLOWID  = 5,	/*  QOS flow Id change event */
};

struct bfa_rport_aen_data_s {
	u16        vf_id;	/*  vf_id of this logical port */
	u16        rsvd[3];
	wwn_t           ppwwn;	/*  WWN of its physical port */
	wwn_t           lpwwn;	/*  WWN of this logical port */
	wwn_t           rpwwn;	/*  WWN of this remote port */
	union {
		bfa_rport_aen_qos_data_t qos;
	} priv;
};

#endif /* __BFA_DEFS_RPORT_H__ */

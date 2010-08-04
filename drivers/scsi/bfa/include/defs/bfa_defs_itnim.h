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
#ifndef __BFA_DEFS_ITNIM_H__
#define __BFA_DEFS_ITNIM_H__

#include <bfa_os_inc.h>
#include <protocol/types.h>

/**
 * FCS itnim states
 */
enum bfa_itnim_state {
	BFA_ITNIM_OFFLINE 	= 0,	/*  offline */
	BFA_ITNIM_PRLI_SEND 	= 1,	/*  prli send */
	BFA_ITNIM_PRLI_SENT 	= 2,	/*  prli sent */
	BFA_ITNIM_PRLI_RETRY 	= 3,	/*  prli retry */
	BFA_ITNIM_HCB_ONLINE 	= 4,	/*  online callback */
	BFA_ITNIM_ONLINE 	= 5,	/*  online */
	BFA_ITNIM_HCB_OFFLINE 	= 6,	/*  offline callback */
	BFA_ITNIM_INITIATIOR 	= 7,	/*  initiator */
};

struct bfa_itnim_latency_s {
	u32	min;
	u32	max;
	u32	count;
	u32	clock_res;
	u32	avg;
	u32	rsvd;
};

struct bfa_itnim_hal_stats_s {
	u32	onlines;	/*  ITN nexus onlines (PRLI done) */
	u32	offlines;	/*  ITN Nexus offlines 	*/
	u32	creates;	/*  ITN create requests 	*/
	u32	deletes;	/*  ITN delete requests 	*/
	u32	create_comps;	/*  ITN create completions 	*/
	u32	delete_comps;	/*  ITN delete completions 	*/
	u32	sler_events;	/*  SLER (sequence level error
					 * recovery) events */
	u32	ioc_disabled;	/*  Num IOC disables		*/
	u32	cleanup_comps;	/*  ITN cleanup completions */
	u32	tm_cmnds;	/*  task management(TM) cmnds sent */
	u32	tm_fw_rsps;	/*  TM cmds firmware responses */
	u32	tm_success;	/*  TM successes */
	u32	tm_failures;	/*  TM failures */
	u32	tm_io_comps;	/*  TM IO completions */
	u32	tm_qresumes;	/*  TM queue resumes (after waiting
					 * for resources)
					 */
	u32	tm_iocdowns;	/*  TM cmnds affected by IOC down */
	u32	tm_cleanups;	/*  TM cleanups */
	u32	tm_cleanup_comps;
					/*  TM cleanup completions */
	u32	ios;		/*  IO requests */
	u32	io_comps;	/*  IO completions */
	u64	input_reqs;	/*  INPUT requests */
	u64	output_reqs;	/*  OUTPUT requests */
};

/**
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
	u32	rsvd;
	struct bfa_itnim_hal_stats_s	hal_stats;
};

/**
 * FCS itnim attributes returned in queries
 */
struct bfa_itnim_attr_s {
	enum bfa_itnim_state state; /*  FCS itnim state        */
	u8 retry;		/*  data retransmision support */
	u8	task_retry_id;  /*  task retry ident support   */
	u8 rec_support;    /*  REC supported              */
	u8 conf_comp;      /*  confirmed completion supp  */
	struct bfa_itnim_latency_s  io_latency; /* IO latency  */
};

/**
 * BFA ITNIM events.
 * Arguments below are in BFAL context from Mgmt
 * BFA_ITNIM_AEN_NEW:       [in]: None  [out]: vf_id, lpwwn
 * BFA_ITNIM_AEN_DELETE:    [in]: vf_id, lpwwn, rpwwn (0 = all fcp4 targets),
 *				  [out]: vf_id, ppwwn, lpwwn, rpwwn
 * BFA_ITNIM_AEN_ONLINE:    [in]: vf_id, lpwwn, rpwwn (0 = all fcp4 targets),
 *				  [out]: vf_id, ppwwn, lpwwn, rpwwn
 * BFA_ITNIM_AEN_OFFLINE:   [in]: vf_id, lpwwn, rpwwn (0 = all fcp4 targets),
 *				  [out]: vf_id, ppwwn, lpwwn, rpwwn
 * BFA_ITNIM_AEN_DISCONNECT:[in]: vf_id, lpwwn, rpwwn (0 = all fcp4 targets),
 *				  [out]: vf_id, ppwwn, lpwwn, rpwwn
 */
enum bfa_itnim_aen_event {
	BFA_ITNIM_AEN_ONLINE 	= 1,	/*  Target online */
	BFA_ITNIM_AEN_OFFLINE 	= 2,	/*  Target offline */
	BFA_ITNIM_AEN_DISCONNECT = 3,	/*  Target disconnected */
};

/**
 * BFA ITNIM event data structure.
 */
struct bfa_itnim_aen_data_s {
	u16        vf_id;	/*  vf_id of the IT nexus */
	u16        rsvd[3];
	wwn_t           ppwwn;	/*  WWN of its physical port */
	wwn_t           lpwwn;	/*  WWN of logical port */
	wwn_t           rpwwn;	/*  WWN of remote(target) port */
};

#endif /* __BFA_DEFS_ITNIM_H__ */

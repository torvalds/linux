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

#ifndef __BFA_DEFS_QOS_H__
#define __BFA_DEFS_QOS_H__

/**
 * QoS states
 */
enum bfa_qos_state {
	BFA_QOS_ONLINE = 1,		/*  QoS is online */
	BFA_QOS_OFFLINE = 2,		/*  QoS is offline */
};


/**
 * QoS  Priority levels.
 */
enum bfa_qos_priority {
	BFA_QOS_UNKNOWN = 0,
	BFA_QOS_HIGH  = 1,	/*  QoS Priority Level High */
	BFA_QOS_MED  =  2,	/*  QoS Priority Level Medium */
	BFA_QOS_LOW  =  3,	/*  QoS Priority Level Low */
};


/**
 * QoS  bandwidth allocation for each priority level
 */
enum bfa_qos_bw_alloc {
	BFA_QOS_BW_HIGH  = 60,	/*  bandwidth allocation for High */
	BFA_QOS_BW_MED  =  30,	/*  bandwidth allocation for Medium */
	BFA_QOS_BW_LOW  =  10,	/*  bandwidth allocation for Low */
};

/**
 * QoS attribute returned in QoS Query
 */
struct bfa_qos_attr_s {
	enum bfa_qos_state state;		/*  QoS current state */
	u32  total_bb_cr;  	 	/*  Total BB Credits */
};

/**
 * These fields should be displayed only from the CLI.
 * There will be a separate BFAL API (get_qos_vc_attr ?)
 * to retrieve this.
 *
 */
#define  BFA_QOS_MAX_VC  16

struct bfa_qos_vc_info_s {
	u8 vc_credit;
	u8 borrow_credit;
	u8 priority;
	u8 resvd;
};

struct bfa_qos_vc_attr_s {
	u16  total_vc_count;                    /*  Total VC Count */
	u16  shared_credit;
	u32  elp_opmode_flags;
	struct bfa_qos_vc_info_s vc_info[BFA_QOS_MAX_VC];  /*   as many as
							    * total_vc_count */
};

/**
 * QoS statistics
 */
struct bfa_qos_stats_s {
	u32	flogi_sent; 		/*  QoS Flogi sent */
	u32	flogi_acc_recvd;	/*  QoS Flogi Acc received */
	u32	flogi_rjt_recvd; /*  QoS Flogi rejects received */
	u32	flogi_retries;		/*  QoS Flogi retries */

	u32	elp_recvd; 	   	/*  QoS ELP received */
	u32	elp_accepted;       /*  QoS ELP Accepted */
	u32	elp_rejected;       /*  QoS ELP rejected */
	u32	elp_dropped;        /*  QoS ELP dropped  */

	u32	qos_rscn_recvd;     /*  QoS RSCN received */
	u32	rsvd; 		/* padding for 64 bit alignment */
};

#endif /* __BFA_DEFS_QOS_H__ */

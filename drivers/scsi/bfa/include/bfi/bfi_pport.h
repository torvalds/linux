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
#ifndef __BFI_PPORT_H__
#define __BFI_PPORT_H__

#include <bfi/bfi.h>
#include <defs/bfa_defs_pport.h>

#pragma pack(1)

enum bfi_pport_h2i {
	BFI_PPORT_H2I_ENABLE_REQ		= (1),
	BFI_PPORT_H2I_DISABLE_REQ		= (2),
	BFI_PPORT_H2I_GET_STATS_REQ		= (3),
	BFI_PPORT_H2I_CLEAR_STATS_REQ	= (4),
	BFI_PPORT_H2I_SET_SVC_PARAMS_REQ	= (5),
	BFI_PPORT_H2I_ENABLE_RX_VF_TAG_REQ	= (6),
	BFI_PPORT_H2I_ENABLE_TX_VF_TAG_REQ	= (7),
	BFI_PPORT_H2I_GET_QOS_STATS_REQ		= (8),
	BFI_PPORT_H2I_CLEAR_QOS_STATS_REQ	= (9),
};

enum bfi_pport_i2h {
	BFI_PPORT_I2H_ENABLE_RSP		= BFA_I2HM(1),
	BFI_PPORT_I2H_DISABLE_RSP		= BFA_I2HM(2),
	BFI_PPORT_I2H_GET_STATS_RSP		= BFA_I2HM(3),
	BFI_PPORT_I2H_CLEAR_STATS_RSP	= BFA_I2HM(4),
	BFI_PPORT_I2H_SET_SVC_PARAMS_RSP	= BFA_I2HM(5),
	BFI_PPORT_I2H_ENABLE_RX_VF_TAG_RSP	= BFA_I2HM(6),
	BFI_PPORT_I2H_ENABLE_TX_VF_TAG_RSP	= BFA_I2HM(7),
	BFI_PPORT_I2H_EVENT			= BFA_I2HM(8),
	BFI_PPORT_I2H_GET_QOS_STATS_RSP		= BFA_I2HM(9),
	BFI_PPORT_I2H_CLEAR_QOS_STATS_RSP	= BFA_I2HM(10),
};

/**
 * Generic REQ type
 */
struct bfi_pport_generic_req_s {
	struct bfi_mhdr_s  mh;		/*  msg header			    */
	u32        msgtag;		/*  msgtag for reply		    */
};

/**
 * Generic RSP type
 */
struct bfi_pport_generic_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		    */
	u8         status;		/*  port enable status		    */
	u8         rsvd[3];
	u32        msgtag;		/*  msgtag for reply		    */
};

/**
 * BFI_PPORT_H2I_ENABLE_REQ
 */
struct bfi_pport_enable_req_s {
	struct bfi_mhdr_s  mh;		/*  msg header			    */
	u32        rsvd1;
	wwn_t           nwwn;		/*  node wwn of physical port	    */
	wwn_t           pwwn;		/*  port wwn of physical port	    */
	struct bfa_pport_cfg_s port_cfg;	/*  port configuration	    */
	union bfi_addr_u  stats_dma_addr;	/*  DMA address for stats  */
	u32        msgtag;		/*  msgtag for reply		    */
	u32        rsvd2;
};

/**
 * BFI_PPORT_I2H_ENABLE_RSP
 */
#define bfi_pport_enable_rsp_t struct bfi_pport_generic_rsp_s

/**
 * BFI_PPORT_H2I_DISABLE_REQ
 */
#define bfi_pport_disable_req_t struct bfi_pport_generic_req_s

/**
 * BFI_PPORT_I2H_DISABLE_RSP
 */
#define bfi_pport_disable_rsp_t struct bfi_pport_generic_rsp_s

/**
 * BFI_PPORT_H2I_GET_STATS_REQ
 */
#define bfi_pport_get_stats_req_t struct bfi_pport_generic_req_s

/**
 * BFI_PPORT_I2H_GET_STATS_RSP
 */
#define bfi_pport_get_stats_rsp_t struct bfi_pport_generic_rsp_s

/**
 * BFI_PPORT_H2I_CLEAR_STATS_REQ
 */
#define bfi_pport_clear_stats_req_t struct bfi_pport_generic_req_s

/**
 * BFI_PPORT_I2H_CLEAR_STATS_RSP
 */
#define bfi_pport_clear_stats_rsp_t struct bfi_pport_generic_rsp_s

/**
 * BFI_PPORT_H2I_GET_QOS_STATS_REQ
 */
#define bfi_pport_get_qos_stats_req_t struct bfi_pport_generic_req_s

/**
 * BFI_PPORT_H2I_GET_QOS_STATS_RSP
 */
#define bfi_pport_get_qos_stats_rsp_t struct bfi_pport_generic_rsp_s

/**
 * BFI_PPORT_H2I_CLEAR_QOS_STATS_REQ
 */
#define bfi_pport_clear_qos_stats_req_t struct bfi_pport_generic_req_s

/**
 * BFI_PPORT_H2I_CLEAR_QOS_STATS_RSP
 */
#define bfi_pport_clear_qos_stats_rsp_t struct bfi_pport_generic_rsp_s

/**
 * BFI_PPORT_H2I_SET_SVC_PARAMS_REQ
 */
struct bfi_pport_set_svc_params_req_s {
	struct bfi_mhdr_s  mh;		/*  msg header */
	u16        tx_bbcredit;	/*  Tx credits */
	u16        rsvd;
};

/**
 * BFI_PPORT_I2H_SET_SVC_PARAMS_RSP
 */

/**
 * BFI_PPORT_I2H_EVENT
 */
struct bfi_pport_event_s {
	struct bfi_mhdr_s 	mh;	/*  common msg header */
	struct bfa_pport_link_s	link_state;
};

union bfi_pport_h2i_msg_u {
	struct bfi_mhdr_s			*mhdr;
	struct bfi_pport_enable_req_s		*penable;
	struct bfi_pport_generic_req_s		*pdisable;
	struct bfi_pport_generic_req_s		*pgetstats;
	struct bfi_pport_generic_req_s		*pclearstats;
	struct bfi_pport_set_svc_params_req_s	*psetsvcparams;
	struct bfi_pport_get_qos_stats_req_s	*pgetqosstats;
	struct bfi_pport_generic_req_s		*pclearqosstats;
};

union bfi_pport_i2h_msg_u {
	struct bfi_msg_s			*msg;
	struct bfi_pport_generic_rsp_s		*enable_rsp;
	struct bfi_pport_disable_rsp_s		*disable_rsp;
	struct bfi_pport_generic_rsp_s		*getstats_rsp;
	struct bfi_pport_clear_stats_rsp_s	*clearstats_rsp;
	struct bfi_pport_set_svc_params_rsp_s	*setsvcparasm_rsp;
	struct bfi_pport_get_qos_stats_rsp_s	*getqosstats_rsp;
	struct bfi_pport_clear_qos_stats_rsp_s	*clearqosstats_rsp;
	struct bfi_pport_event_s		*event;
};

#pragma pack()

#endif /* __BFI_PPORT_H__ */


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

#ifndef __BFI_LPS_H__
#define __BFI_LPS_H__

#include <bfi/bfi.h>

#pragma pack(1)

enum bfi_lps_h2i_msgs {
	BFI_LPS_H2I_LOGIN_REQ	= 1,
	BFI_LPS_H2I_LOGOUT_REQ	= 2,
};

enum bfi_lps_i2h_msgs {
	BFI_LPS_H2I_LOGIN_RSP	= BFA_I2HM(1),
	BFI_LPS_H2I_LOGOUT_RSP	= BFA_I2HM(2),
};

struct bfi_lps_login_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		alpa;
	u16	pdu_size;
	wwn_t		pwwn;
	wwn_t		nwwn;
	u8		fdisc;
	u8		auth_en;
	u8		rsvd[2];
};

struct bfi_lps_login_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		status;
	u8		lsrjt_rsn;
	u8		lsrjt_expl;
	wwn_t		port_name;
	wwn_t		node_name;
	u16	bb_credit;
	u8		f_port;
	u8		npiv_en;
	u32	lp_pid:24;
	u32	auth_req:8;
	mac_t		lp_mac;
	mac_t		fcf_mac;
	u8		ext_status;
	u8  	brcd_switch;/*  attached peer is brcd switch	*/
};

struct bfi_lps_logout_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		rsvd[3];
	wwn_t		port_name;
};

struct bfi_lps_logout_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8		lp_tag;
	u8		status;
	u8		rsvd[2];
};

union bfi_lps_h2i_msg_u {
	struct bfi_mhdr_s		*msg;
	struct bfi_lps_login_req_s	*login_req;
	struct bfi_lps_logout_req_s	*logout_req;
};

union bfi_lps_i2h_msg_u {
	struct bfi_msg_s		*msg;
	struct bfi_lps_login_rsp_s	*login_rsp;
	struct bfi_lps_logout_rsp_s	*logout_rsp;
};

#pragma pack()

#endif /* __BFI_LPS_H__ */



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

#ifndef __BFI_RPORT_H__
#define __BFI_RPORT_H__

#include <bfi/bfi.h>

#pragma pack(1)

enum bfi_rport_h2i_msgs {
	BFI_RPORT_H2I_CREATE_REQ = 1,
	BFI_RPORT_H2I_DELETE_REQ = 2,
	BFI_RPORT_H2I_SET_SPEED_REQ  = 3,
};

enum bfi_rport_i2h_msgs {
	BFI_RPORT_I2H_CREATE_RSP = BFA_I2HM(1),
	BFI_RPORT_I2H_DELETE_RSP = BFA_I2HM(2),
	BFI_RPORT_I2H_QOS_SCN    = BFA_I2HM(3),
};

struct bfi_rport_create_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16        bfa_handle;	/*  host rport handle		*/
	u16        max_frmsz;	/*  max rcv pdu size		*/
	u32        pid       : 24,	/*  remote port ID		*/
			lp_tag    : 8;	/*  local port tag		*/
	u32        local_pid : 24,	/*  local port ID		*/
			cisc      : 8;
	u8         fc_class;	/*  supported FC classes	*/
	u8         vf_en;		/*  virtual fabric enable	*/
	u16        vf_id;		/*  virtual fabric ID		*/
};

struct bfi_rport_create_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u8         status;		/*  rport creation status	*/
	u8         rsvd[3];
	u16        bfa_handle;	/*  host rport handle		*/
	u16        fw_handle;	/*  firmware rport handle	*/
	struct bfa_rport_qos_attr_s qos_attr;  /* QoS Attributes */
};

struct bfa_rport_speed_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16        fw_handle;	/*  firmware rport handle	*/
	u8		speed;		/*! rport's speed via RPSC  */
	u8		rsvd;
};

struct bfi_rport_delete_req_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16        fw_handle;	/*  firmware rport handle	*/
	u16        rsvd;
};

struct bfi_rport_delete_rsp_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16        bfa_handle;	/*  host rport handle		*/
	u8         status;		/*  rport deletion status	*/
	u8         rsvd;
};

struct bfi_rport_qos_scn_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u16        bfa_handle;	/*  host rport handle		*/
	u16        rsvd;
	struct bfa_rport_qos_attr_s old_qos_attr;  /* Old QoS Attributes */
	struct bfa_rport_qos_attr_s new_qos_attr;  /* New QoS Attributes */
};

union bfi_rport_h2i_msg_u {
	struct bfi_msg_s 		*msg;
	struct bfi_rport_create_req_s	*create_req;
	struct bfi_rport_delete_req_s	*delete_req;
	struct bfi_rport_speed_req_s	*speed_req;
};

union bfi_rport_i2h_msg_u {
	struct bfi_msg_s 		*msg;
	struct bfi_rport_create_rsp_s	*create_rsp;
	struct bfi_rport_delete_rsp_s	*delete_rsp;
	struct bfi_rport_qos_scn_s	*qos_scn_evt;
};

#pragma pack()

#endif /* __BFI_RPORT_H__ */


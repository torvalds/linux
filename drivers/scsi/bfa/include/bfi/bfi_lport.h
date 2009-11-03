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

#ifndef __BFI_LPORT_H__
#define __BFI_LPORT_H__

#include <bfi/bfi.h>

#pragma pack(1)

enum bfi_lport_h2i_msgs {
	BFI_LPORT_H2I_CREATE_REQ = 1,
	BFI_LPORT_H2I_DELETE_REQ = 2,
};

enum bfi_lport_i2h_msgs {
	BFI_LPORT_I2H_CREATE_RSP = BFA_I2HM(1),
	BFI_LPORT_I2H_DELETE_RSP = BFA_I2HM(2),
	BFI_LPORT_I2H_ONLINE	  = BFA_I2HM(3),
	BFI_LPORT_I2H_OFFLINE	  = BFA_I2HM(4),
};

#define BFI_LPORT_MAX_SYNNAME	64

enum bfi_lport_role_e {
	BFI_LPORT_ROLE_FCPIM	= 1,
	BFI_LPORT_ROLE_FCPTM	= 2,
	BFI_LPORT_ROLE_IPFC	= 4,
};

struct bfi_lport_create_req_s {
	bfi_mhdr_t	mh;		/*  common msg header		*/
	u16	fabric_fwhdl;	/*  parent fabric instance	*/
	u8		roles;		/*  lport FC-4 roles		*/
	u8		rsvd;
	wwn_t		pwwn;		/*  port name			*/
	wwn_t		nwwn;		/*  node name			*/
	u8		symname[BFI_LPORT_MAX_SYNNAME];
};

struct bfi_lport_create_rsp_s {
	bfi_mhdr_t	mh;		/*  common msg header		*/
	u8         status;		/*  lport creation status	*/
	u8         rsvd[3];
};

struct bfi_lport_delete_req_s {
	bfi_mhdr_t	mh;		/*  common msg header		*/
	u16        fw_handle;	/*  firmware lport handle	*/
	u16        rsvd;
};

struct bfi_lport_delete_rsp_s {
	bfi_mhdr_t	mh;		/*  common msg header		*/
	u16        bfa_handle;	/*  host lport handle		*/
	u8         status;		/*  lport deletion status	*/
	u8         rsvd;
};

union bfi_lport_h2i_msg_u {
	bfi_msg_t		*msg;
	struct bfi_lport_create_req_s	*create_req;
	struct bfi_lport_delete_req_s	*delete_req;
};

union bfi_lport_i2h_msg_u {
	bfi_msg_t		*msg;
	struct bfi_lport_create_rsp_s	*create_rsp;
	struct bfi_lport_delete_rsp_s	*delete_rsp;
};

#pragma pack()

#endif /* __BFI_LPORT_H__ */


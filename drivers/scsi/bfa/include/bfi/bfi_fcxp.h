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

#ifndef __BFI_FCXP_H__
#define __BFI_FCXP_H__

#include "bfi.h"

#pragma pack(1)

enum bfi_fcxp_h2i {
	BFI_FCXP_H2I_SEND_REQ = 1,
};

enum bfi_fcxp_i2h {
	BFI_FCXP_I2H_SEND_RSP = BFA_I2HM(1),
};

#define BFA_FCXP_MAX_SGES	2

/**
 * FCXP send request structure
 */
struct bfi_fcxp_send_req_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		    */
	u16        fcxp_tag;	/*  driver request tag		    */
	u16        max_frmsz;	/*  max send frame size	    */
	u16        vf_id;		/*  vsan tag if applicable	    */
	u16        rport_fw_hndl;	/*  FW Handle for the remote port  */
	u8         class;		/*  FC class used for req/rsp	    */
	u8         rsp_timeout;	/*  timeout in secs, 0-no response */
	u8         cts;		/*  continue sequence		    */
	u8         lp_tag;		/*  lport tag			    */
	struct fchs_s   fchs;		/*  request FC header structure    */
	u32        req_len;	/*  request payload length	    */
	u32        rsp_maxlen;	/*  max response length expected   */
	struct bfi_sge_s   req_sge[BFA_FCXP_MAX_SGES];	/*  request buf    */
	struct bfi_sge_s   rsp_sge[BFA_FCXP_MAX_SGES];	/*  response buf   */
};

/**
 * FCXP send response structure
 */
struct bfi_fcxp_send_rsp_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		    */
	u16        fcxp_tag;	/*  send request tag		    */
	u8         req_status;	/*  request status		    */
	u8         rsvd;
	u32        rsp_len;	/*  actual response length	    */
	u32        residue_len;	/*  residual response length	    */
	struct fchs_s   fchs;		/*  response FC header structure   */
};

#pragma pack()

#endif /* __BFI_FCXP_H__ */


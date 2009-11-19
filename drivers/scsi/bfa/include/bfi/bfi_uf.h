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

#ifndef __BFI_UF_H__
#define __BFI_UF_H__

#include "bfi.h"

#pragma pack(1)

enum bfi_uf_h2i {
	BFI_UF_H2I_BUF_POST = 1,
};

enum bfi_uf_i2h {
	BFI_UF_I2H_FRM_RCVD = BFA_I2HM(1),
};

#define BFA_UF_MAX_SGES	2

struct bfi_uf_buf_post_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		*/
	u16        buf_tag;	/*  buffer tag			*/
	u16        buf_len;	/*  total buffer length	*/
	struct bfi_sge_s   sge[BFA_UF_MAX_SGES]; /*  buffer DMA SGEs	*/
};

struct bfi_uf_frm_rcvd_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header		*/
	u16        buf_tag;	/*  buffer tag			*/
	u16        rsvd;
	u16        frm_len;	/*  received frame length 	*/
	u16        xfr_len;	/*  tranferred length		*/
};

#pragma pack()

#endif /* __BFI_UF_H__ */

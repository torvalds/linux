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
/**
 *  Copyright (c) 2006-2009 Brocade Communications Systems, Inc.
 *  All rights reserved.
 *
 *  bfi_dcbx.h BFI Interface (Mailbox commands and related structures)
 * between host driver and DCBX/LLDP firmware module.
 *
**/

#ifndef __BFI_CEE_H__
#define __BFI_CEE_H__

#include <bfi/bfi.h>

#pragma pack(1)


enum bfi_cee_h2i_msgs_e {
	BFI_CEE_H2I_GET_CFG_REQ = 1,
	BFI_CEE_H2I_RESET_STATS = 2,
	BFI_CEE_H2I_GET_STATS_REQ = 3,
};


enum bfi_cee_i2h_msgs_e {
	BFI_CEE_I2H_GET_CFG_RSP = BFA_I2HM(1),
	BFI_CEE_I2H_RESET_STATS_RSP = BFA_I2HM(2),
	BFI_CEE_I2H_GET_STATS_RSP = BFA_I2HM(3),
};


/* Data structures */

/*
 * BFI_CEE_H2I_RESET_STATS
 */
struct bfi_lldp_reset_stats_s {
	struct bfi_mhdr_s  mh;
};

/*
 * BFI_CEE_H2I_RESET_STATS
 */
struct bfi_cee_reset_stats_s {
	struct bfi_mhdr_s  mh;
};

/*
 * BFI_CEE_H2I_GET_CFG_REQ
 */
struct bfi_cee_get_req_s {
	struct bfi_mhdr_s  mh;
	union bfi_addr_u   dma_addr;
};


/*
 * BFI_CEE_I2H_GET_CFG_RSP
 */
struct bfi_cee_get_rsp_s {
	struct bfi_mhdr_s  mh;
	u8            cmd_status;
	u8            rsvd[3];
};

/*
 * BFI_CEE_H2I_GET_STATS_REQ
 */
struct bfi_cee_stats_req_s {
	struct bfi_mhdr_s  mh;
	union bfi_addr_u   dma_addr;
};


/*
 * BFI_CEE_I2H_GET_STATS_RSP
 */
struct bfi_cee_stats_rsp_s {
	struct bfi_mhdr_s  mh;
	u8 		   cmd_status;
	u8 		   rsvd[3];
};



union bfi_cee_h2i_msg_u {
	struct bfi_mhdr_s           mh;
	struct bfi_cee_get_req_s   get_req;
	struct bfi_cee_stats_req_s stats_req;
};


union bfi_cee_i2h_msg_u {
	struct bfi_mhdr_s         mh;
	struct bfi_cee_get_rsp_s  get_rsp;
	struct bfi_cee_stats_rsp_s stats_rsp;
};

#pragma pack()


#endif /* __BFI_CEE_H__ */


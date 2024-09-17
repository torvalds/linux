/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */
#ifndef __BFI_CNA_H__
#define __BFI_CNA_H__

#include "bfi.h"
#include "bfa_defs_cna.h"

enum bfi_port_h2i {
	BFI_PORT_H2I_ENABLE_REQ		= (1),
	BFI_PORT_H2I_DISABLE_REQ	= (2),
	BFI_PORT_H2I_GET_STATS_REQ	= (3),
	BFI_PORT_H2I_CLEAR_STATS_REQ	= (4),
};

enum bfi_port_i2h {
	BFI_PORT_I2H_ENABLE_RSP		= BFA_I2HM(1),
	BFI_PORT_I2H_DISABLE_RSP	= BFA_I2HM(2),
	BFI_PORT_I2H_GET_STATS_RSP	= BFA_I2HM(3),
	BFI_PORT_I2H_CLEAR_STATS_RSP	= BFA_I2HM(4),
};

/* Generic REQ type */
struct bfi_port_generic_req {
	struct bfi_mhdr mh;		/*!< msg header			    */
	u32	msgtag;		/*!< msgtag for reply		    */
	u32	rsvd;
} __packed;

/* Generic RSP type */
struct bfi_port_generic_rsp {
	struct bfi_mhdr mh;		/*!< common msg header		    */
	u8		status;		/*!< port enable status		    */
	u8		rsvd[3];
	u32	msgtag;		/*!< msgtag for reply		    */
} __packed;

/* BFI_PORT_H2I_GET_STATS_REQ */
struct bfi_port_get_stats_req {
	struct bfi_mhdr mh;		/*!< common msg header		    */
	union bfi_addr_u   dma_addr;
} __packed;

union bfi_port_h2i_msg_u {
	struct bfi_mhdr mh;
	struct bfi_port_generic_req enable_req;
	struct bfi_port_generic_req disable_req;
	struct bfi_port_get_stats_req getstats_req;
	struct bfi_port_generic_req clearstats_req;
} __packed;

union bfi_port_i2h_msg_u {
	struct bfi_mhdr mh;
	struct bfi_port_generic_rsp enable_rsp;
	struct bfi_port_generic_rsp disable_rsp;
	struct bfi_port_generic_rsp getstats_rsp;
	struct bfi_port_generic_rsp clearstats_rsp;
} __packed;

/* @brief Mailbox commands from host to (DCBX/LLDP) firmware */
enum bfi_cee_h2i_msgs {
	BFI_CEE_H2I_GET_CFG_REQ = 1,
	BFI_CEE_H2I_RESET_STATS = 2,
	BFI_CEE_H2I_GET_STATS_REQ = 3,
};

/* @brief Mailbox reply and AEN messages from DCBX/LLDP firmware to host */
enum bfi_cee_i2h_msgs {
	BFI_CEE_I2H_GET_CFG_RSP = BFA_I2HM(1),
	BFI_CEE_I2H_RESET_STATS_RSP = BFA_I2HM(2),
	BFI_CEE_I2H_GET_STATS_RSP = BFA_I2HM(3),
};

/* Data structures */

/*
 * @brief H2I command structure for resetting the stats.
 * BFI_CEE_H2I_RESET_STATS
 */
struct bfi_lldp_reset_stats {
	struct bfi_mhdr mh;
} __packed;

/*
 * @brief H2I command structure for resetting the stats.
 * BFI_CEE_H2I_RESET_STATS
 */
struct bfi_cee_reset_stats {
	struct bfi_mhdr mh;
} __packed;

/*
 * @brief  get configuration  command from host
 * BFI_CEE_H2I_GET_CFG_REQ
 */
struct bfi_cee_get_req {
	struct bfi_mhdr mh;
	union bfi_addr_u   dma_addr;
} __packed;

/*
 * @brief reply message from firmware
 * BFI_CEE_I2H_GET_CFG_RSP
 */
struct bfi_cee_get_rsp {
	struct bfi_mhdr mh;
	u8			cmd_status;
	u8			rsvd[3];
} __packed;

/*
 * @brief  get configuration  command from host
 * BFI_CEE_H2I_GET_STATS_REQ
 */
struct bfi_cee_stats_req {
	struct bfi_mhdr mh;
	union bfi_addr_u   dma_addr;
} __packed;

/*
 * @brief reply message from firmware
 * BFI_CEE_I2H_GET_STATS_RSP
 */
struct bfi_cee_stats_rsp {
	struct bfi_mhdr mh;
	u8			cmd_status;
	u8			rsvd[3];
} __packed;

/* @brief mailbox command structures from host to firmware */
union bfi_cee_h2i_msg_u {
	struct bfi_mhdr mh;
	struct bfi_cee_get_req get_req;
	struct bfi_cee_stats_req stats_req;
} __packed;

/* @brief mailbox message structures from firmware to host	*/
union bfi_cee_i2h_msg_u {
	struct bfi_mhdr mh;
	struct bfi_cee_get_rsp get_rsp;
	struct bfi_cee_stats_rsp stats_rsp;
} __packed;

#endif /* __BFI_CNA_H__ */

/*
 * Linux network driver for Brocade Converged Network Adapter.
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
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */
#ifndef __BFI_LL_H__
#define __BFI_LL_H__

#include "bfi.h"

#pragma pack(1)

/**
 * @brief
 *	"enums" for all LL mailbox messages other than IOC
 */
enum {
	BFI_LL_H2I_MAC_UCAST_SET_REQ = 1,
	BFI_LL_H2I_MAC_UCAST_ADD_REQ = 2,
	BFI_LL_H2I_MAC_UCAST_DEL_REQ = 3,

	BFI_LL_H2I_MAC_MCAST_ADD_REQ = 4,
	BFI_LL_H2I_MAC_MCAST_DEL_REQ = 5,
	BFI_LL_H2I_MAC_MCAST_FILTER_REQ = 6,
	BFI_LL_H2I_MAC_MCAST_DEL_ALL_REQ = 7,

	BFI_LL_H2I_PORT_ADMIN_REQ = 8,
	BFI_LL_H2I_STATS_GET_REQ = 9,
	BFI_LL_H2I_STATS_CLEAR_REQ = 10,

	BFI_LL_H2I_RXF_PROMISCUOUS_SET_REQ = 11,
	BFI_LL_H2I_RXF_DEFAULT_SET_REQ = 12,

	BFI_LL_H2I_TXQ_STOP_REQ = 13,
	BFI_LL_H2I_RXQ_STOP_REQ = 14,

	BFI_LL_H2I_DIAG_LOOPBACK_REQ = 15,

	BFI_LL_H2I_SET_PAUSE_REQ = 16,
	BFI_LL_H2I_MTU_INFO_REQ = 17,

	BFI_LL_H2I_RX_REQ = 18,
} ;

enum {
	BFI_LL_I2H_MAC_UCAST_SET_RSP = BFA_I2HM(1),
	BFI_LL_I2H_MAC_UCAST_ADD_RSP = BFA_I2HM(2),
	BFI_LL_I2H_MAC_UCAST_DEL_RSP = BFA_I2HM(3),

	BFI_LL_I2H_MAC_MCAST_ADD_RSP = BFA_I2HM(4),
	BFI_LL_I2H_MAC_MCAST_DEL_RSP = BFA_I2HM(5),
	BFI_LL_I2H_MAC_MCAST_FILTER_RSP = BFA_I2HM(6),
	BFI_LL_I2H_MAC_MCAST_DEL_ALL_RSP = BFA_I2HM(7),

	BFI_LL_I2H_PORT_ADMIN_RSP = BFA_I2HM(8),
	BFI_LL_I2H_STATS_GET_RSP = BFA_I2HM(9),
	BFI_LL_I2H_STATS_CLEAR_RSP = BFA_I2HM(10),

	BFI_LL_I2H_RXF_PROMISCUOUS_SET_RSP = BFA_I2HM(11),
	BFI_LL_I2H_RXF_DEFAULT_SET_RSP = BFA_I2HM(12),

	BFI_LL_I2H_TXQ_STOP_RSP = BFA_I2HM(13),
	BFI_LL_I2H_RXQ_STOP_RSP = BFA_I2HM(14),

	BFI_LL_I2H_DIAG_LOOPBACK_RSP = BFA_I2HM(15),

	BFI_LL_I2H_SET_PAUSE_RSP = BFA_I2HM(16),

	BFI_LL_I2H_MTU_INFO_RSP = BFA_I2HM(17),
	BFI_LL_I2H_RX_RSP = BFA_I2HM(18),

	BFI_LL_I2H_LINK_DOWN_AEN = BFA_I2HM(19),
	BFI_LL_I2H_LINK_UP_AEN = BFA_I2HM(20),

	BFI_LL_I2H_PORT_ENABLE_AEN = BFA_I2HM(21),
	BFI_LL_I2H_PORT_DISABLE_AEN = BFA_I2HM(22),
} ;

/**
 * @brief bfi_ll_mac_addr_req is used by:
 *        BFI_LL_H2I_MAC_UCAST_SET_REQ
 *        BFI_LL_H2I_MAC_UCAST_ADD_REQ
 *        BFI_LL_H2I_MAC_UCAST_DEL_REQ
 *        BFI_LL_H2I_MAC_MCAST_ADD_REQ
 *        BFI_LL_H2I_MAC_MCAST_DEL_REQ
 */
struct bfi_ll_mac_addr_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u8		rxf_id;
	u8		rsvd1[3];
	mac_t		mac_addr;
	u8		rsvd2[2];
};

/**
 * @brief bfi_ll_mcast_filter_req is used by:
 *	  BFI_LL_H2I_MAC_MCAST_FILTER_REQ
 */
struct bfi_ll_mcast_filter_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u8		rxf_id;
	u8		enable;
	u8		rsvd[2];
};

/**
 * @brief bfi_ll_mcast_del_all is used by:
 *	  BFI_LL_H2I_MAC_MCAST_DEL_ALL_REQ
 */
struct bfi_ll_mcast_del_all_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u8		   rxf_id;
	u8		   rsvd[3];
};

/**
 * @brief bfi_ll_q_stop_req is used by:
 *	BFI_LL_H2I_TXQ_STOP_REQ
 *	BFI_LL_H2I_RXQ_STOP_REQ
 */
struct bfi_ll_q_stop_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u32	q_id_mask[2];	/* !< bit-mask for queue ids */
};

/**
 * @brief bfi_ll_stats_req is used by:
 *    BFI_LL_I2H_STATS_GET_REQ
 *    BFI_LL_I2H_STATS_CLEAR_REQ
 */
struct bfi_ll_stats_req {
	struct bfi_mhdr mh;	/*!< common msg header */
	u16 stats_mask;	/* !< bit-mask for non-function statistics */
	u8	rsvd[2];
	u32 rxf_id_mask[2];	/* !< bit-mask for RxF Statistics */
	u32 txf_id_mask[2];	/* !< bit-mask for TxF Statistics */
	union bfi_addr_u  host_buffer;	/* !< where statistics are returned */
};

/**
 * @brief defines for "stats_mask" above.
 */
#define BFI_LL_STATS_MAC	(1 << 0)	/* !< MAC Statistics */
#define BFI_LL_STATS_BPC	(1 << 1)	/* !< Pause Stats from BPC */
#define BFI_LL_STATS_RAD	(1 << 2)	/* !< Rx Admission Statistics */
#define BFI_LL_STATS_RX_FC	(1 << 3)	/* !< Rx FC Stats from RxA */
#define BFI_LL_STATS_TX_FC	(1 << 4)	/* !< Tx FC Stats from TxA */

#define BFI_LL_STATS_ALL	0x1f

/**
 * @brief bfi_ll_port_admin_req
 */
struct bfi_ll_port_admin_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u8		 up;
	u8		 rsvd[3];
};

/**
 * @brief bfi_ll_rxf_req is used by:
 *      BFI_LL_H2I_RXF_PROMISCUOUS_SET_REQ
 *      BFI_LL_H2I_RXF_DEFAULT_SET_REQ
 */
struct bfi_ll_rxf_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u8		rxf_id;
	u8		enable;
	u8		rsvd[2];
};

/**
 * @brief bfi_ll_rxf_multi_req is used by:
 *	BFI_LL_H2I_RX_REQ
 */
struct bfi_ll_rxf_multi_req {
	struct bfi_mhdr mh;		/*!< common msg header */
	u32	rxf_id_mask[2];
	u8		enable;
	u8		rsvd[3];
};

/**
 * @brief enum for Loopback opmodes
 */
enum {
	BFI_LL_DIAG_LB_OPMODE_EXT = 0,
	BFI_LL_DIAG_LB_OPMODE_CBL = 1,
};

/**
 * @brief bfi_ll_set_pause_req is used by:
 *	BFI_LL_H2I_SET_PAUSE_REQ
 */
struct bfi_ll_set_pause_req {
	struct bfi_mhdr mh;
	u8		tx_pause; /* 1 = enable, 0 =  disable */
	u8		rx_pause; /* 1 = enable, 0 =  disable */
	u8		rsvd[2];
};

/**
 * @brief bfi_ll_mtu_info_req is used by:
 *	BFI_LL_H2I_MTU_INFO_REQ
 */
struct bfi_ll_mtu_info_req {
	struct bfi_mhdr mh;
	u16	mtu;
	u8		rsvd[2];
};

/**
 * @brief
 *	  Response header format used by all responses
 *	  For both responses and asynchronous notifications
 */
struct bfi_ll_rsp {
	struct bfi_mhdr mh;		/*!< common msg header */
	u8		error;
	u8		rsvd[3];
};

/**
 * @brief bfi_ll_cee_aen is used by:
 *	BFI_LL_I2H_LINK_DOWN_AEN
 *	BFI_LL_I2H_LINK_UP_AEN
 */
struct bfi_ll_aen {
	struct bfi_mhdr mh;		/*!< common msg header */
	u32	reason;
	u8		cee_linkup;
	u8		prio_map;    /*!< LL priority bit-map */
	u8		rsvd[2];
};

/**
 * @brief
 * 	The following error codes can be returned
 *	by the mbox commands
 */
enum {
	BFI_LL_CMD_OK 		= 0,
	BFI_LL_CMD_FAIL 	= 1,
	BFI_LL_CMD_DUP_ENTRY	= 2,	/* !< Duplicate entry in CAM */
	BFI_LL_CMD_CAM_FULL	= 3,	/* !< CAM is full */
	BFI_LL_CMD_NOT_OWNER	= 4,   	/* !< Not permitted, b'cos not owner */
	BFI_LL_CMD_NOT_EXEC	= 5,   	/* !< Was not sent to f/w at all */
	BFI_LL_CMD_WAITING	= 6,	/* !< Waiting for completion (VMware) */
	BFI_LL_CMD_PORT_DISABLED	= 7,	/* !< port in disabled state */
} ;

/* Statistics */
#define BFI_LL_TXF_ID_MAX  	64
#define BFI_LL_RXF_ID_MAX  	64

/* TxF Frame Statistics */
struct bfi_ll_stats_txf {
	u64 ucast_octets;
	u64 ucast;
	u64 ucast_vlan;

	u64 mcast_octets;
	u64 mcast;
	u64 mcast_vlan;

	u64 bcast_octets;
	u64 bcast;
	u64 bcast_vlan;

	u64 errors;
	u64 filter_vlan;      /* frames filtered due to VLAN */
	u64 filter_mac_sa;    /* frames filtered due to SA check */
};

/* RxF Frame Statistics */
struct bfi_ll_stats_rxf {
	u64 ucast_octets;
	u64 ucast;
	u64 ucast_vlan;

	u64 mcast_octets;
	u64 mcast;
	u64 mcast_vlan;

	u64 bcast_octets;
	u64 bcast;
	u64 bcast_vlan;
	u64 frame_drops;
};

/* FC Tx Frame Statistics */
struct bfi_ll_stats_fc_tx {
	u64 txf_ucast_octets;
	u64 txf_ucast;
	u64 txf_ucast_vlan;

	u64 txf_mcast_octets;
	u64 txf_mcast;
	u64 txf_mcast_vlan;

	u64 txf_bcast_octets;
	u64 txf_bcast;
	u64 txf_bcast_vlan;

	u64 txf_parity_errors;
	u64 txf_timeout;
	u64 txf_fid_parity_errors;
};

/* FC Rx Frame Statistics */
struct bfi_ll_stats_fc_rx {
	u64 rxf_ucast_octets;
	u64 rxf_ucast;
	u64 rxf_ucast_vlan;

	u64 rxf_mcast_octets;
	u64 rxf_mcast;
	u64 rxf_mcast_vlan;

	u64 rxf_bcast_octets;
	u64 rxf_bcast;
	u64 rxf_bcast_vlan;
};

/* RAD Frame Statistics */
struct bfi_ll_stats_rad {
	u64 rx_frames;
	u64 rx_octets;
	u64 rx_vlan_frames;

	u64 rx_ucast;
	u64 rx_ucast_octets;
	u64 rx_ucast_vlan;

	u64 rx_mcast;
	u64 rx_mcast_octets;
	u64 rx_mcast_vlan;

	u64 rx_bcast;
	u64 rx_bcast_octets;
	u64 rx_bcast_vlan;

	u64 rx_drops;
};

/* BPC Tx Registers */
struct bfi_ll_stats_bpc {
	/* transmit stats */
	u64 tx_pause[8];
	u64 tx_zero_pause[8];	/*!< Pause cancellation */
	/*!<Pause initiation rather than retention */
	u64 tx_first_pause[8];

	/* receive stats */
	u64 rx_pause[8];
	u64 rx_zero_pause[8];	/*!< Pause cancellation */
	/*!<Pause initiation rather than retention */
	u64 rx_first_pause[8];
};

/* MAC Rx Statistics */
struct bfi_ll_stats_mac {
	u64 frame_64;		/* both rx and tx counter */
	u64 frame_65_127;		/* both rx and tx counter */
	u64 frame_128_255;		/* both rx and tx counter */
	u64 frame_256_511;		/* both rx and tx counter */
	u64 frame_512_1023;	/* both rx and tx counter */
	u64 frame_1024_1518;	/* both rx and tx counter */
	u64 frame_1519_1522;	/* both rx and tx counter */

	/* receive stats */
	u64 rx_bytes;
	u64 rx_packets;
	u64 rx_fcs_error;
	u64 rx_multicast;
	u64 rx_broadcast;
	u64 rx_control_frames;
	u64 rx_pause;
	u64 rx_unknown_opcode;
	u64 rx_alignment_error;
	u64 rx_frame_length_error;
	u64 rx_code_error;
	u64 rx_carrier_sense_error;
	u64 rx_undersize;
	u64 rx_oversize;
	u64 rx_fragments;
	u64 rx_jabber;
	u64 rx_drop;

	/* transmit stats */
	u64 tx_bytes;
	u64 tx_packets;
	u64 tx_multicast;
	u64 tx_broadcast;
	u64 tx_pause;
	u64 tx_deferral;
	u64 tx_excessive_deferral;
	u64 tx_single_collision;
	u64 tx_muliple_collision;
	u64 tx_late_collision;
	u64 tx_excessive_collision;
	u64 tx_total_collision;
	u64 tx_pause_honored;
	u64 tx_drop;
	u64 tx_jabber;
	u64 tx_fcs_error;
	u64 tx_control_frame;
	u64 tx_oversize;
	u64 tx_undersize;
	u64 tx_fragments;
};

/* Complete statistics */
struct bfi_ll_stats {
	struct bfi_ll_stats_mac		mac_stats;
	struct bfi_ll_stats_bpc		bpc_stats;
	struct bfi_ll_stats_rad		rad_stats;
	struct bfi_ll_stats_fc_rx	fc_rx_stats;
	struct bfi_ll_stats_fc_tx	fc_tx_stats;
	struct bfi_ll_stats_rxf	rxf_stats[BFI_LL_RXF_ID_MAX];
	struct bfi_ll_stats_txf	txf_stats[BFI_LL_TXF_ID_MAX];
};

#pragma pack()

#endif  /* __BFI_LL_H__ */

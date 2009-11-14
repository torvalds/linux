/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved.
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

#ifndef __PHYPORT_DEFS_H__
#define __PHYPORT_DEFS_H__

#define BNA_TXF_ID_MAX  	64
#define BNA_RXF_ID_MAX  	64

/*
 * Statistics
 */

/*
 * TxF Frame Statistics
 */
struct bna_stats_txf {
	u64        ucast_octets;
	u64        ucast;
	u64        ucast_vlan;

	u64        mcast_octets;
	u64        mcast;
	u64        mcast_vlan;

	u64        bcast_octets;
	u64        bcast;
	u64        bcast_vlan;

	u64        errors;
	u64        filter_vlan;	/* frames filtered due to VLAN */
	u64        filter_mac_sa;	/* frames filtered due to SA check */
};

/*
 * RxF Frame Statistics
 */
struct bna_stats_rxf {
	u64        ucast_octets;
	u64        ucast;
	u64        ucast_vlan;

	u64        mcast_octets;
	u64        mcast;
	u64        mcast_vlan;

	u64        bcast_octets;
	u64        bcast;
	u64        bcast_vlan;
	u64        frame_drops;
};

/*
 * FC Tx Frame Statistics
 */
struct bna_stats_fc_tx {
	u64        txf_ucast_octets;
	u64        txf_ucast;
	u64        txf_ucast_vlan;

	u64        txf_mcast_octets;
	u64        txf_mcast;
	u64        txf_mcast_vlan;

	u64        txf_bcast_octets;
	u64        txf_bcast;
	u64        txf_bcast_vlan;

	u64        txf_parity_errors;
	u64        txf_timeout;
	u64        txf_fid_parity_errors;
};

/*
 * FC Rx Frame Statistics
 */
struct bna_stats_fc_rx {
	u64        rxf_ucast_octets;
	u64        rxf_ucast;
	u64        rxf_ucast_vlan;

	u64        rxf_mcast_octets;
	u64        rxf_mcast;
	u64        rxf_mcast_vlan;

	u64        rxf_bcast_octets;
	u64        rxf_bcast;
	u64        rxf_bcast_vlan;
};

/*
 * RAD Frame Statistics
 */
struct cna_stats_rad {
	u64        rx_frames;
	u64        rx_octets;
	u64        rx_vlan_frames;

	u64        rx_ucast;
	u64        rx_ucast_octets;
	u64        rx_ucast_vlan;

	u64        rx_mcast;
	u64        rx_mcast_octets;
	u64        rx_mcast_vlan;

	u64        rx_bcast;
	u64        rx_bcast_octets;
	u64        rx_bcast_vlan;

	u64        rx_drops;
};

/*
 * BPC Tx Registers
 */
struct cna_stats_bpc_tx {
	u64        tx_pause[8];
	u64        tx_zero_pause[8];	/*  Pause cancellation */
	u64        tx_first_pause[8];	/*  Pause initiation rather
						 *than retention */
};

/*
 * BPC Rx Registers
 */
struct cna_stats_bpc_rx {
	u64        rx_pause[8];
	u64        rx_zero_pause[8];	/*  Pause cancellation */
	u64        rx_first_pause[8];	/*  Pause initiation rather
						 *than retention */
};

/*
 * MAC Rx Statistics
 */
struct cna_stats_mac_rx {
	u64        frame_64;	/* both rx and tx counter */
	u64        frame_65_127;	/* both rx and tx counter */
	u64        frame_128_255;	/* both rx and tx counter */
	u64        frame_256_511;	/* both rx and tx counter */
	u64        frame_512_1023;	/* both rx and tx counter */
	u64        frame_1024_1518;	/* both rx and tx counter */
	u64        frame_1518_1522;	/* both rx and tx counter */
	u64        rx_bytes;
	u64        rx_packets;
	u64        rx_fcs_error;
	u64        rx_multicast;
	u64        rx_broadcast;
	u64        rx_control_frames;
	u64        rx_pause;
	u64        rx_unknown_opcode;
	u64        rx_alignment_error;
	u64        rx_frame_length_error;
	u64        rx_code_error;
	u64        rx_carrier_sense_error;
	u64        rx_undersize;
	u64        rx_oversize;
	u64        rx_fragments;
	u64        rx_jabber;
	u64        rx_drop;
};

/*
 * MAC Tx Statistics
 */
struct cna_stats_mac_tx {
	u64        tx_bytes;
	u64        tx_packets;
	u64        tx_multicast;
	u64        tx_broadcast;
	u64        tx_pause;
	u64        tx_deferral;
	u64        tx_excessive_deferral;
	u64        tx_single_collision;
	u64        tx_muliple_collision;
	u64        tx_late_collision;
	u64        tx_excessive_collision;
	u64        tx_total_collision;
	u64        tx_pause_honored;
	u64        tx_drop;
	u64        tx_jabber;
	u64        tx_fcs_error;
	u64        tx_control_frame;
	u64        tx_oversize;
	u64        tx_undersize;
	u64        tx_fragments;
};

/*
 * Complete statistics
 */
struct bna_stats {
	struct cna_stats_mac_rx mac_rx_stats;
	struct cna_stats_bpc_rx bpc_rx_stats;
	struct cna_stats_rad rad_stats;
	struct bna_stats_fc_rx fc_rx_stats;
	struct cna_stats_mac_tx mac_tx_stats;
	struct cna_stats_bpc_tx bpc_tx_stats;
	struct bna_stats_fc_tx fc_tx_stats;
	struct bna_stats_rxf rxf_stats[BNA_TXF_ID_MAX];
	struct bna_stats_txf txf_stats[BNA_RXF_ID_MAX];
};

#endif

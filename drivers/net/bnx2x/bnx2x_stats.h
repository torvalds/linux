/* bnx2x_stats.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 */

#ifndef BNX2X_STATS_H
#define BNX2X_STATS_H

#include <linux/types.h>

struct bnx2x_eth_q_stats {
	u32 total_bytes_received_hi;
	u32 total_bytes_received_lo;
	u32 total_bytes_transmitted_hi;
	u32 total_bytes_transmitted_lo;
	u32 total_unicast_packets_received_hi;
	u32 total_unicast_packets_received_lo;
	u32 total_multicast_packets_received_hi;
	u32 total_multicast_packets_received_lo;
	u32 total_broadcast_packets_received_hi;
	u32 total_broadcast_packets_received_lo;
	u32 total_unicast_packets_transmitted_hi;
	u32 total_unicast_packets_transmitted_lo;
	u32 total_multicast_packets_transmitted_hi;
	u32 total_multicast_packets_transmitted_lo;
	u32 total_broadcast_packets_transmitted_hi;
	u32 total_broadcast_packets_transmitted_lo;
	u32 valid_bytes_received_hi;
	u32 valid_bytes_received_lo;

	u32 error_bytes_received_hi;
	u32 error_bytes_received_lo;
	u32 etherstatsoverrsizepkts_hi;
	u32 etherstatsoverrsizepkts_lo;
	u32 no_buff_discard_hi;
	u32 no_buff_discard_lo;

	u32 driver_xoff;
	u32 rx_err_discard_pkt;
	u32 rx_skb_alloc_failed;
	u32 hw_csum_err;
};

#define BNX2X_NUM_Q_STATS		13
#define Q_STATS_OFFSET32(stat_name) \
			(offsetof(struct bnx2x_eth_q_stats, stat_name) / 4)

struct nig_stats {
	u32 brb_discard;
	u32 brb_packet;
	u32 brb_truncate;
	u32 flow_ctrl_discard;
	u32 flow_ctrl_octets;
	u32 flow_ctrl_packet;
	u32 mng_discard;
	u32 mng_octet_inp;
	u32 mng_octet_out;
	u32 mng_packet_inp;
	u32 mng_packet_out;
	u32 pbf_octets;
	u32 pbf_packet;
	u32 safc_inp;
	u32 egress_mac_pkt0_lo;
	u32 egress_mac_pkt0_hi;
	u32 egress_mac_pkt1_lo;
	u32 egress_mac_pkt1_hi;
};


enum bnx2x_stats_event {
	STATS_EVENT_PMF = 0,
	STATS_EVENT_LINK_UP,
	STATS_EVENT_UPDATE,
	STATS_EVENT_STOP,
	STATS_EVENT_MAX
};

enum bnx2x_stats_state {
	STATS_STATE_DISABLED = 0,
	STATS_STATE_ENABLED,
	STATS_STATE_MAX
};

struct bnx2x_eth_stats {
	u32 total_bytes_received_hi;
	u32 total_bytes_received_lo;
	u32 total_bytes_transmitted_hi;
	u32 total_bytes_transmitted_lo;
	u32 total_unicast_packets_received_hi;
	u32 total_unicast_packets_received_lo;
	u32 total_multicast_packets_received_hi;
	u32 total_multicast_packets_received_lo;
	u32 total_broadcast_packets_received_hi;
	u32 total_broadcast_packets_received_lo;
	u32 total_unicast_packets_transmitted_hi;
	u32 total_unicast_packets_transmitted_lo;
	u32 total_multicast_packets_transmitted_hi;
	u32 total_multicast_packets_transmitted_lo;
	u32 total_broadcast_packets_transmitted_hi;
	u32 total_broadcast_packets_transmitted_lo;
	u32 valid_bytes_received_hi;
	u32 valid_bytes_received_lo;

	u32 error_bytes_received_hi;
	u32 error_bytes_received_lo;
	u32 etherstatsoverrsizepkts_hi;
	u32 etherstatsoverrsizepkts_lo;
	u32 no_buff_discard_hi;
	u32 no_buff_discard_lo;

	u32 rx_stat_ifhcinbadoctets_hi;
	u32 rx_stat_ifhcinbadoctets_lo;
	u32 tx_stat_ifhcoutbadoctets_hi;
	u32 tx_stat_ifhcoutbadoctets_lo;
	u32 rx_stat_dot3statsfcserrors_hi;
	u32 rx_stat_dot3statsfcserrors_lo;
	u32 rx_stat_dot3statsalignmenterrors_hi;
	u32 rx_stat_dot3statsalignmenterrors_lo;
	u32 rx_stat_dot3statscarriersenseerrors_hi;
	u32 rx_stat_dot3statscarriersenseerrors_lo;
	u32 rx_stat_falsecarriererrors_hi;
	u32 rx_stat_falsecarriererrors_lo;
	u32 rx_stat_etherstatsundersizepkts_hi;
	u32 rx_stat_etherstatsundersizepkts_lo;
	u32 rx_stat_dot3statsframestoolong_hi;
	u32 rx_stat_dot3statsframestoolong_lo;
	u32 rx_stat_etherstatsfragments_hi;
	u32 rx_stat_etherstatsfragments_lo;
	u32 rx_stat_etherstatsjabbers_hi;
	u32 rx_stat_etherstatsjabbers_lo;
	u32 rx_stat_maccontrolframesreceived_hi;
	u32 rx_stat_maccontrolframesreceived_lo;
	u32 rx_stat_bmac_xpf_hi;
	u32 rx_stat_bmac_xpf_lo;
	u32 rx_stat_bmac_xcf_hi;
	u32 rx_stat_bmac_xcf_lo;
	u32 rx_stat_xoffstateentered_hi;
	u32 rx_stat_xoffstateentered_lo;
	u32 rx_stat_xonpauseframesreceived_hi;
	u32 rx_stat_xonpauseframesreceived_lo;
	u32 rx_stat_xoffpauseframesreceived_hi;
	u32 rx_stat_xoffpauseframesreceived_lo;
	u32 tx_stat_outxonsent_hi;
	u32 tx_stat_outxonsent_lo;
	u32 tx_stat_outxoffsent_hi;
	u32 tx_stat_outxoffsent_lo;
	u32 tx_stat_flowcontroldone_hi;
	u32 tx_stat_flowcontroldone_lo;
	u32 tx_stat_etherstatscollisions_hi;
	u32 tx_stat_etherstatscollisions_lo;
	u32 tx_stat_dot3statssinglecollisionframes_hi;
	u32 tx_stat_dot3statssinglecollisionframes_lo;
	u32 tx_stat_dot3statsmultiplecollisionframes_hi;
	u32 tx_stat_dot3statsmultiplecollisionframes_lo;
	u32 tx_stat_dot3statsdeferredtransmissions_hi;
	u32 tx_stat_dot3statsdeferredtransmissions_lo;
	u32 tx_stat_dot3statsexcessivecollisions_hi;
	u32 tx_stat_dot3statsexcessivecollisions_lo;
	u32 tx_stat_dot3statslatecollisions_hi;
	u32 tx_stat_dot3statslatecollisions_lo;
	u32 tx_stat_etherstatspkts64octets_hi;
	u32 tx_stat_etherstatspkts64octets_lo;
	u32 tx_stat_etherstatspkts65octetsto127octets_hi;
	u32 tx_stat_etherstatspkts65octetsto127octets_lo;
	u32 tx_stat_etherstatspkts128octetsto255octets_hi;
	u32 tx_stat_etherstatspkts128octetsto255octets_lo;
	u32 tx_stat_etherstatspkts256octetsto511octets_hi;
	u32 tx_stat_etherstatspkts256octetsto511octets_lo;
	u32 tx_stat_etherstatspkts512octetsto1023octets_hi;
	u32 tx_stat_etherstatspkts512octetsto1023octets_lo;
	u32 tx_stat_etherstatspkts1024octetsto1522octets_hi;
	u32 tx_stat_etherstatspkts1024octetsto1522octets_lo;
	u32 tx_stat_etherstatspktsover1522octets_hi;
	u32 tx_stat_etherstatspktsover1522octets_lo;
	u32 tx_stat_bmac_2047_hi;
	u32 tx_stat_bmac_2047_lo;
	u32 tx_stat_bmac_4095_hi;
	u32 tx_stat_bmac_4095_lo;
	u32 tx_stat_bmac_9216_hi;
	u32 tx_stat_bmac_9216_lo;
	u32 tx_stat_bmac_16383_hi;
	u32 tx_stat_bmac_16383_lo;
	u32 tx_stat_dot3statsinternalmactransmiterrors_hi;
	u32 tx_stat_dot3statsinternalmactransmiterrors_lo;
	u32 tx_stat_bmac_ufl_hi;
	u32 tx_stat_bmac_ufl_lo;

	u32 pause_frames_received_hi;
	u32 pause_frames_received_lo;
	u32 pause_frames_sent_hi;
	u32 pause_frames_sent_lo;

	u32 etherstatspkts1024octetsto1522octets_hi;
	u32 etherstatspkts1024octetsto1522octets_lo;
	u32 etherstatspktsover1522octets_hi;
	u32 etherstatspktsover1522octets_lo;

	u32 brb_drop_hi;
	u32 brb_drop_lo;
	u32 brb_truncate_hi;
	u32 brb_truncate_lo;

	u32 mac_filter_discard;
	u32 xxoverflow_discard;
	u32 brb_truncate_discard;
	u32 mac_discard;

	u32 driver_xoff;
	u32 rx_err_discard_pkt;
	u32 rx_skb_alloc_failed;
	u32 hw_csum_err;

	u32 nig_timer_max;
};

#define BNX2X_NUM_STATS			43
#define STATS_OFFSET32(stat_name) \
			(offsetof(struct bnx2x_eth_stats, stat_name) / 4)

/* Forward declaration */
struct bnx2x;


void bnx2x_stats_init(struct bnx2x *bp);

extern const u32 dmae_reg_go_c[];
extern int bnx2x_sp_post(struct bnx2x *bp, int command, int cid,
			 u32 data_hi, u32 data_lo, int common);


#endif /* BNX2X_STATS_H */

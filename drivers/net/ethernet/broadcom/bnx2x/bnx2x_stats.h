/* bnx2x_stats.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */
#ifndef BNX2X_STATS_H
#define BNX2X_STATS_H

#include <linux/types.h>

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
	u32 mf_tag_discard;
	u32 brb_truncate_discard;
	u32 mac_discard;

	u32 driver_xoff;
	u32 rx_err_discard_pkt;
	u32 rx_skb_alloc_failed;
	u32 hw_csum_err;

	u32 nig_timer_max;

	/* TPA */
	u32 total_tpa_aggregations_hi;
	u32 total_tpa_aggregations_lo;
	u32 total_tpa_aggregated_frames_hi;
	u32 total_tpa_aggregated_frames_lo;
	u32 total_tpa_bytes_hi;
	u32 total_tpa_bytes_lo;

	/* PFC */
	u32 pfc_frames_received_hi;
	u32 pfc_frames_received_lo;
	u32 pfc_frames_sent_hi;
	u32 pfc_frames_sent_lo;

	/* Recovery */
	u32 recoverable_error;
	u32 unrecoverable_error;
	u32 driver_filtered_tx_pkt;
	/* src: Clear-on-Read register; Will not survive PMF Migration */
	u32 eee_tx_lpi;
};


struct bnx2x_eth_q_stats {
	u32 total_unicast_bytes_received_hi;
	u32 total_unicast_bytes_received_lo;
	u32 total_broadcast_bytes_received_hi;
	u32 total_broadcast_bytes_received_lo;
	u32 total_multicast_bytes_received_hi;
	u32 total_multicast_bytes_received_lo;
	u32 total_bytes_received_hi;
	u32 total_bytes_received_lo;
	u32 total_unicast_bytes_transmitted_hi;
	u32 total_unicast_bytes_transmitted_lo;
	u32 total_broadcast_bytes_transmitted_hi;
	u32 total_broadcast_bytes_transmitted_lo;
	u32 total_multicast_bytes_transmitted_hi;
	u32 total_multicast_bytes_transmitted_lo;
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

	u32 etherstatsoverrsizepkts_hi;
	u32 etherstatsoverrsizepkts_lo;
	u32 no_buff_discard_hi;
	u32 no_buff_discard_lo;

	u32 driver_xoff;
	u32 rx_err_discard_pkt;
	u32 rx_skb_alloc_failed;
	u32 hw_csum_err;

	u32 total_packets_received_checksum_discarded_hi;
	u32 total_packets_received_checksum_discarded_lo;
	u32 total_packets_received_ttl0_discarded_hi;
	u32 total_packets_received_ttl0_discarded_lo;
	u32 total_transmitted_dropped_packets_error_hi;
	u32 total_transmitted_dropped_packets_error_lo;

	/* TPA */
	u32 total_tpa_aggregations_hi;
	u32 total_tpa_aggregations_lo;
	u32 total_tpa_aggregated_frames_hi;
	u32 total_tpa_aggregated_frames_lo;
	u32 total_tpa_bytes_hi;
	u32 total_tpa_bytes_lo;
	u32 driver_filtered_tx_pkt;
};

struct bnx2x_eth_stats_old {
	u32 rx_stat_dot3statsframestoolong_hi;
	u32 rx_stat_dot3statsframestoolong_lo;
};

struct bnx2x_eth_q_stats_old {
	/* Fields to perserve over fw reset*/
	u32 total_unicast_bytes_received_hi;
	u32 total_unicast_bytes_received_lo;
	u32 total_broadcast_bytes_received_hi;
	u32 total_broadcast_bytes_received_lo;
	u32 total_multicast_bytes_received_hi;
	u32 total_multicast_bytes_received_lo;
	u32 total_unicast_bytes_transmitted_hi;
	u32 total_unicast_bytes_transmitted_lo;
	u32 total_broadcast_bytes_transmitted_hi;
	u32 total_broadcast_bytes_transmitted_lo;
	u32 total_multicast_bytes_transmitted_hi;
	u32 total_multicast_bytes_transmitted_lo;
	u32 total_tpa_bytes_hi;
	u32 total_tpa_bytes_lo;

	/* Fields to perserve last of */
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

	u32 total_tpa_bytes_hi_old;
	u32 total_tpa_bytes_lo_old;

	u32 driver_xoff_old;
	u32 rx_err_discard_pkt_old;
	u32 rx_skb_alloc_failed_old;
	u32 hw_csum_err_old;
	u32 driver_filtered_tx_pkt_old;
};

struct bnx2x_net_stats_old {
	 u32 rx_dropped;
};

struct bnx2x_fw_port_stats_old {
	 u32 mac_filter_discard;
	 u32 mf_tag_discard;
	 u32 brb_truncate_discard;
	 u32 mac_discard;
};


/****************************************************************************
* Macros
****************************************************************************/

/* sum[hi:lo] += add[hi:lo] */
#define ADD_64(s_hi, a_hi, s_lo, a_lo) \
	do { \
		s_lo += a_lo; \
		s_hi += a_hi + ((s_lo < a_lo) ? 1 : 0); \
	} while (0)

#define LE32_0 ((__force __le32) 0)
#define LE16_0 ((__force __le16) 0)

/* The _force is for cases where high value is 0 */
#define ADD_64_LE(s_hi, a_hi_le, s_lo, a_lo_le) \
		ADD_64(s_hi, le32_to_cpu(a_hi_le), \
		       s_lo, le32_to_cpu(a_lo_le))

#define ADD_64_LE16(s_hi, a_hi_le, s_lo, a_lo_le) \
		ADD_64(s_hi, le16_to_cpu(a_hi_le), \
		       s_lo, le16_to_cpu(a_lo_le))

/* difference = minuend - subtrahend */
#define DIFF_64(d_hi, m_hi, s_hi, d_lo, m_lo, s_lo) \
	do { \
		if (m_lo < s_lo) { \
			/* underflow */ \
			d_hi = m_hi - s_hi; \
			if (d_hi > 0) { \
				/* we can 'loan' 1 */ \
				d_hi--; \
				d_lo = m_lo + (UINT_MAX - s_lo) + 1; \
			} else { \
				/* m_hi <= s_hi */ \
				d_hi = 0; \
				d_lo = 0; \
			} \
		} else { \
			/* m_lo >= s_lo */ \
			if (m_hi < s_hi) { \
				d_hi = 0; \
				d_lo = 0; \
			} else { \
				/* m_hi >= s_hi */ \
				d_hi = m_hi - s_hi; \
				d_lo = m_lo - s_lo; \
			} \
		} \
	} while (0)

#define UPDATE_STAT64(s, t) \
	do { \
		DIFF_64(diff.hi, new->s##_hi, pstats->mac_stx[0].t##_hi, \
			diff.lo, new->s##_lo, pstats->mac_stx[0].t##_lo); \
		pstats->mac_stx[0].t##_hi = new->s##_hi; \
		pstats->mac_stx[0].t##_lo = new->s##_lo; \
		ADD_64(pstats->mac_stx[1].t##_hi, diff.hi, \
		       pstats->mac_stx[1].t##_lo, diff.lo); \
	} while (0)

#define UPDATE_STAT64_NIG(s, t) \
	do { \
		DIFF_64(diff.hi, new->s##_hi, old->s##_hi, \
			diff.lo, new->s##_lo, old->s##_lo); \
		ADD_64(estats->t##_hi, diff.hi, \
		       estats->t##_lo, diff.lo); \
	} while (0)

/* sum[hi:lo] += add */
#define ADD_EXTEND_64(s_hi, s_lo, a) \
	do { \
		s_lo += a; \
		s_hi += (s_lo < a) ? 1 : 0; \
	} while (0)

#define ADD_STAT64(diff, t) \
	do { \
		ADD_64(pstats->mac_stx[1].t##_hi, new->diff##_hi, \
		       pstats->mac_stx[1].t##_lo, new->diff##_lo); \
	} while (0)

#define UPDATE_EXTEND_STAT(s) \
	do { \
		ADD_EXTEND_64(pstats->mac_stx[1].s##_hi, \
			      pstats->mac_stx[1].s##_lo, \
			      new->s); \
	} while (0)

#define UPDATE_EXTEND_TSTAT_X(s, t, size) \
	do { \
		diff = le##size##_to_cpu(tclient->s) - \
		       le##size##_to_cpu(old_tclient->s); \
		old_tclient->s = tclient->s; \
		ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

#define UPDATE_EXTEND_TSTAT(s, t) UPDATE_EXTEND_TSTAT_X(s, t, 32)

#define UPDATE_EXTEND_E_TSTAT(s, t, size) \
	do { \
		UPDATE_EXTEND_TSTAT_X(s, t, size); \
		ADD_EXTEND_64(estats->t##_hi, estats->t##_lo, diff); \
	} while (0)

#define UPDATE_EXTEND_USTAT(s, t) \
	do { \
		diff = le32_to_cpu(uclient->s) - le32_to_cpu(old_uclient->s); \
		old_uclient->s = uclient->s; \
		ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

#define UPDATE_EXTEND_E_USTAT(s, t) \
	do { \
		UPDATE_EXTEND_USTAT(s, t); \
		ADD_EXTEND_64(estats->t##_hi, estats->t##_lo, diff); \
	} while (0)

#define UPDATE_EXTEND_XSTAT(s, t) \
	do { \
		diff = le32_to_cpu(xclient->s) - le32_to_cpu(old_xclient->s); \
		old_xclient->s = xclient->s; \
		ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

#define UPDATE_QSTAT(s, t) \
	do { \
		qstats->t##_lo = qstats_old->t##_lo + le32_to_cpu(s.lo); \
		qstats->t##_hi = qstats_old->t##_hi + le32_to_cpu(s.hi) \
			+ ((qstats->t##_lo < qstats_old->t##_lo) ? 1 : 0); \
	} while (0)

#define UPDATE_QSTAT_OLD(f) \
	do { \
		qstats_old->f = qstats->f; \
	} while (0)

#define UPDATE_ESTAT_QSTAT_64(s) \
	do { \
		ADD_64(estats->s##_hi, qstats->s##_hi, \
		       estats->s##_lo, qstats->s##_lo); \
		SUB_64(estats->s##_hi, qstats_old->s##_hi_old, \
		       estats->s##_lo, qstats_old->s##_lo_old); \
		qstats_old->s##_hi_old = qstats->s##_hi; \
		qstats_old->s##_lo_old = qstats->s##_lo; \
	} while (0)

#define UPDATE_ESTAT_QSTAT(s) \
	do { \
		estats->s += qstats->s; \
		estats->s -= qstats_old->s##_old; \
		qstats_old->s##_old = qstats->s; \
	} while (0)

#define UPDATE_FSTAT_QSTAT(s) \
	do { \
		ADD_64(fstats->s##_hi, qstats->s##_hi, \
		       fstats->s##_lo, qstats->s##_lo); \
		SUB_64(fstats->s##_hi, qstats_old->s##_hi, \
		       fstats->s##_lo, qstats_old->s##_lo); \
		estats->s##_hi = fstats->s##_hi; \
		estats->s##_lo = fstats->s##_lo; \
		qstats_old->s##_hi = qstats->s##_hi; \
		qstats_old->s##_lo = qstats->s##_lo; \
	} while (0)

#define UPDATE_FW_STAT(s) \
	do { \
		estats->s = le32_to_cpu(tport->s) + fwstats->s; \
	} while (0)

#define UPDATE_FW_STAT_OLD(f) \
	do { \
		fwstats->f = estats->f; \
	} while (0)

#define UPDATE_ESTAT(s, t) \
	do { \
		SUB_64(estats->s##_hi, estats_old->t##_hi, \
		       estats->s##_lo, estats_old->t##_lo); \
		ADD_64(estats->s##_hi, estats->t##_hi, \
		       estats->s##_lo, estats->t##_lo); \
		estats_old->t##_hi = estats->t##_hi; \
		estats_old->t##_lo = estats->t##_lo; \
	} while (0)

/* minuend -= subtrahend */
#define SUB_64(m_hi, s_hi, m_lo, s_lo) \
	do { \
		DIFF_64(m_hi, m_hi, s_hi, m_lo, m_lo, s_lo); \
	} while (0)

/* minuend[hi:lo] -= subtrahend */
#define SUB_EXTEND_64(m_hi, m_lo, s) \
	do { \
		SUB_64(m_hi, 0, m_lo, s); \
	} while (0)

#define SUB_EXTEND_USTAT(s, t) \
	do { \
		diff = le32_to_cpu(uclient->s) - le32_to_cpu(old_uclient->s); \
		SUB_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)


/* forward */
struct bnx2x;

void bnx2x_stats_init(struct bnx2x *bp);

void bnx2x_stats_handle(struct bnx2x *bp, enum bnx2x_stats_event event);

/**
 * bnx2x_save_statistics - save statistics when unloading.
 *
 * @bp:		driver handle
 */
void bnx2x_save_statistics(struct bnx2x *bp);

void bnx2x_afex_collect_stats(struct bnx2x *bp, void *void_afex_stats,
			      u32 stats_type);
#endif /* BNX2X_STATS_H */

/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL18XX_ACX_H__
#define __WL18XX_ACX_H__

#include "../wlcore/wlcore.h"
#include "../wlcore/acx.h"

enum {
	ACX_NS_IPV6_FILTER		 = 0x0050,
	ACX_PEER_HT_OPERATION_MODE_CFG	 = 0x0051,
	ACX_CSUM_CONFIG			 = 0x0052,
	ACX_SIM_CONFIG			 = 0x0053,
	ACX_CLEAR_STATISTICS		 = 0x0054,
	ACX_AUTO_RX_STREAMING		 = 0x0055,
	ACX_PEER_CAP			 = 0x0056
};

/* numbers of bits the length field takes (add 1 for the actual number) */
#define WL18XX_HOST_IF_LEN_SIZE_FIELD 15

#define WL18XX_ACX_EVENTS_VECTOR	(WL1271_ACX_INTR_WATCHDOG	| \
					 WL1271_ACX_INTR_INIT_COMPLETE	| \
					 WL1271_ACX_INTR_EVENT_A	| \
					 WL1271_ACX_INTR_EVENT_B	| \
					 WL1271_ACX_INTR_CMD_COMPLETE	| \
					 WL1271_ACX_INTR_HW_AVAILABLE	| \
					 WL1271_ACX_INTR_DATA		| \
					 WL1271_ACX_SW_INTR_WATCHDOG)

#define WL18XX_INTR_MASK		(WL1271_ACX_INTR_WATCHDOG	| \
					 WL1271_ACX_INTR_EVENT_A	| \
					 WL1271_ACX_INTR_EVENT_B	| \
					 WL1271_ACX_INTR_HW_AVAILABLE	| \
					 WL1271_ACX_INTR_DATA		| \
					 WL1271_ACX_SW_INTR_WATCHDOG)

struct wl18xx_acx_host_config_bitmap {
	struct acx_header header;

	__le32 host_cfg_bitmap;

	__le32 host_sdio_block_size;

	/* extra mem blocks per frame in TX. */
	__le32 extra_mem_blocks;

	/*
	 * number of bits of the length field in the first TX word
	 * (up to 15 - for using the entire 16 bits).
	 */
	__le32 length_field_size;

} __packed;

enum {
	CHECKSUM_OFFLOAD_DISABLED = 0,
	CHECKSUM_OFFLOAD_ENABLED  = 1,
	CHECKSUM_OFFLOAD_FAKE_RX  = 2,
	CHECKSUM_OFFLOAD_INVALID  = 0xFF
};

struct wl18xx_acx_checksum_state {
	struct acx_header header;

	 /* enum acx_checksum_state */
	u8 checksum_state;
	u8 pad[3];
} __packed;


struct wl18xx_acx_error_stats {
	u32 error_frame;
	u32 error_null_Frame_tx_start;
	u32 error_numll_frame_cts_start;
	u32 error_bar_retry;
	u32 error_frame_cts_nul_flid;
} __packed;

struct wl18xx_acx_debug_stats {
	u32 debug1;
	u32 debug2;
	u32 debug3;
	u32 debug4;
	u32 debug5;
	u32 debug6;
} __packed;

struct wl18xx_acx_ring_stats {
	u32 prepared_descs;
	u32 tx_cmplt;
} __packed;

struct wl18xx_acx_tx_stats {
	u32 tx_prepared_descs;
	u32 tx_cmplt;
	u32 tx_template_prepared;
	u32 tx_data_prepared;
	u32 tx_template_programmed;
	u32 tx_data_programmed;
	u32 tx_burst_programmed;
	u32 tx_starts;
	u32 tx_imm_resp;
	u32 tx_start_templates;
	u32 tx_start_int_templates;
	u32 tx_start_fw_gen;
	u32 tx_start_data;
	u32 tx_start_null_frame;
	u32 tx_exch;
	u32 tx_retry_template;
	u32 tx_retry_data;
	u32 tx_exch_pending;
	u32 tx_exch_expiry;
	u32 tx_done_template;
	u32 tx_done_data;
	u32 tx_done_int_template;
	u32 tx_frame_checksum;
	u32 tx_checksum_result;
	u32 frag_called;
	u32 frag_mpdu_alloc_failed;
	u32 frag_init_called;
	u32 frag_in_process_called;
	u32 frag_tkip_called;
	u32 frag_key_not_found;
	u32 frag_need_fragmentation;
	u32 frag_bad_mblk_num;
	u32 frag_failed;
	u32 frag_cache_hit;
	u32 frag_cache_miss;
} __packed;

struct wl18xx_acx_rx_stats {
	u32 rx_beacon_early_term;
	u32 rx_out_of_mpdu_nodes;
	u32 rx_hdr_overflow;
	u32 rx_dropped_frame;
	u32 rx_done_stage;
	u32 rx_done;
	u32 rx_defrag;
	u32 rx_defrag_end;
	u32 rx_cmplt;
	u32 rx_pre_complt;
	u32 rx_cmplt_task;
	u32 rx_phy_hdr;
	u32 rx_timeout;
	u32 rx_timeout_wa;
	u32 rx_wa_density_dropped_frame;
	u32 rx_wa_ba_not_expected;
	u32 rx_frame_checksum;
	u32 rx_checksum_result;
	u32 defrag_called;
	u32 defrag_init_called;
	u32 defrag_in_process_called;
	u32 defrag_tkip_called;
	u32 defrag_need_defrag;
	u32 defrag_decrypt_failed;
	u32 decrypt_key_not_found;
	u32 defrag_need_decrypt;
	u32 rx_tkip_replays;
} __packed;

struct wl18xx_acx_isr_stats {
	u32 irqs;
} __packed;

#define PWR_STAT_MAX_CONT_MISSED_BCNS_SPREAD 10

struct wl18xx_acx_pwr_stats {
	u32 missing_bcns_cnt;
	u32 rcvd_bcns_cnt;
	u32 connection_out_of_sync;
	u32 cont_miss_bcns_spread[PWR_STAT_MAX_CONT_MISSED_BCNS_SPREAD];
	u32 rcvd_awake_bcns_cnt;
} __packed;

struct wl18xx_acx_event_stats {
	u32 calibration;
	u32 rx_mismatch;
	u32 rx_mem_empty;
} __packed;

struct wl18xx_acx_ps_poll_stats {
	u32 ps_poll_timeouts;
	u32 upsd_timeouts;
	u32 upsd_max_ap_turn;
	u32 ps_poll_max_ap_turn;
	u32 ps_poll_utilization;
	u32 upsd_utilization;
} __packed;

struct wl18xx_acx_rx_filter_stats {
	u32 beacon_filter;
	u32 arp_filter;
	u32 mc_filter;
	u32 dup_filter;
	u32 data_filter;
	u32 ibss_filter;
	u32 protection_filter;
	u32 accum_arp_pend_requests;
	u32 max_arp_queue_dep;
} __packed;

struct wl18xx_acx_rx_rate_stats {
	u32 rx_frames_per_rates[50];
} __packed;

#define AGGR_STATS_TX_AGG	16
#define AGGR_STATS_TX_RATE	16
#define AGGR_STATS_RX_SIZE_LEN	16

struct wl18xx_acx_aggr_stats {
	u32 tx_agg_vs_rate[AGGR_STATS_TX_AGG * AGGR_STATS_TX_RATE];
	u32 rx_size[AGGR_STATS_RX_SIZE_LEN];
} __packed;

#define PIPE_STATS_HW_FIFO	11

struct wl18xx_acx_pipeline_stats {
	u32 hs_tx_stat_fifo_int;
	u32 hs_rx_stat_fifo_int;
	u32 tcp_tx_stat_fifo_int;
	u32 tcp_rx_stat_fifo_int;
	u32 enc_tx_stat_fifo_int;
	u32 enc_rx_stat_fifo_int;
	u32 rx_complete_stat_fifo_int;
	u32 pre_proc_swi;
	u32 post_proc_swi;
	u32 sec_frag_swi;
	u32 pre_to_defrag_swi;
	u32 defrag_to_csum_swi;
	u32 csum_to_rx_xfer_swi;
	u32 dec_packet_in;
	u32 dec_packet_in_fifo_full;
	u32 dec_packet_out;
	u32 cs_rx_packet_in;
	u32 cs_rx_packet_out;
	u16 pipeline_fifo_full[PIPE_STATS_HW_FIFO];
} __packed;

struct wl18xx_acx_mem_stats {
	u32 rx_free_mem_blks;
	u32 tx_free_mem_blks;
	u32 fwlog_free_mem_blks;
	u32 fw_gen_free_mem_blks;
} __packed;

struct wl18xx_acx_statistics {
	struct acx_header header;

	struct wl18xx_acx_error_stats		error;
	struct wl18xx_acx_debug_stats		debug;
	struct wl18xx_acx_tx_stats		tx;
	struct wl18xx_acx_rx_stats		rx;
	struct wl18xx_acx_isr_stats		isr;
	struct wl18xx_acx_pwr_stats		pwr;
	struct wl18xx_acx_ps_poll_stats		ps_poll;
	struct wl18xx_acx_rx_filter_stats	rx_filter;
	struct wl18xx_acx_rx_rate_stats		rx_rate;
	struct wl18xx_acx_aggr_stats		aggr_size;
	struct wl18xx_acx_pipeline_stats	pipeline;
	struct wl18xx_acx_mem_stats		mem;
} __packed;

struct wl18xx_acx_clear_statistics {
	struct acx_header header;
};

enum wlcore_bandwidth {
	WLCORE_BANDWIDTH_20MHZ,
	WLCORE_BANDWIDTH_40MHZ,
};

struct wlcore_peer_ht_operation_mode {
	struct acx_header header;

	u8 hlid;
	u8 bandwidth; /* enum wlcore_bandwidth */
	u8 padding[2];
};

int wl18xx_acx_host_if_cfg_bitmap(struct wl1271 *wl, u32 host_cfg_bitmap,
				  u32 sdio_blk_size, u32 extra_mem_blks,
				  u32 len_field_size);
int wl18xx_acx_set_checksum_state(struct wl1271 *wl);
int wl18xx_acx_clear_statistics(struct wl1271 *wl);
int wl18xx_acx_peer_ht_operation_mode(struct wl1271 *wl, u8 hlid, bool wide);

#endif /* __WL18XX_ACX_H__ */

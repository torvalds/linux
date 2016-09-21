/*
 * Copyright (c) 2015-2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __MLX5_EN_STATS_H__
#define __MLX5_EN_STATS_H__

#define MLX5E_READ_CTR64_CPU(ptr, dsc, i) \
	(*(u64 *)((char *)ptr + dsc[i].offset))
#define MLX5E_READ_CTR64_BE(ptr, dsc, i) \
	be64_to_cpu(*(__be64 *)((char *)ptr + dsc[i].offset))
#define MLX5E_READ_CTR32_CPU(ptr, dsc, i) \
	(*(u32 *)((char *)ptr + dsc[i].offset))
#define MLX5E_READ_CTR32_BE(ptr, dsc, i) \
	be64_to_cpu(*(__be32 *)((char *)ptr + dsc[i].offset))

#define MLX5E_DECLARE_STAT(type, fld) #fld, offsetof(type, fld)
#define MLX5E_DECLARE_RX_STAT(type, fld) "rx%d_"#fld, offsetof(type, fld)
#define MLX5E_DECLARE_TX_STAT(type, fld) "tx%d_"#fld, offsetof(type, fld)

struct counter_desc {
	char		format[ETH_GSTRING_LEN];
	int		offset; /* Byte offset */
};

struct mlx5e_sw_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_tso_packets;
	u64 tx_tso_bytes;
	u64 tx_tso_inner_packets;
	u64 tx_tso_inner_bytes;
	u64 rx_lro_packets;
	u64 rx_lro_bytes;
	u64 rx_csum_unnecessary;
	u64 rx_csum_none;
	u64 rx_csum_complete;
	u64 rx_csum_unnecessary_inner;
	u64 rx_xdp_drop;
	u64 tx_csum_partial;
	u64 tx_csum_partial_inner;
	u64 tx_queue_stopped;
	u64 tx_queue_wake;
	u64 tx_queue_dropped;
	u64 tx_xmit_more;
	u64 rx_wqe_err;
	u64 rx_mpwqe_filler;
	u64 rx_buff_alloc_err;
	u64 rx_cqe_compress_blks;
	u64 rx_cqe_compress_pkts;
	u64 rx_cache_reuse;
	u64 rx_cache_full;
	u64 rx_cache_empty;
	u64 rx_cache_busy;

	/* Special handling counters */
	u64 link_down_events_phy;
};

static const struct counter_desc sw_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_inner_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_inner_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_lro_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_lro_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_unnecessary) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_none) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_complete) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_unnecessary_inner) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_csum_partial) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_csum_partial_inner) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_queue_stopped) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_queue_wake) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_queue_dropped) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xmit_more) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_wqe_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_mpwqe_filler) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_buff_alloc_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cqe_compress_blks) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cqe_compress_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_reuse) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_empty) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_busy) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, link_down_events_phy) },
};

struct mlx5e_qcounter_stats {
	u32 rx_out_of_buffer;
};

static const struct counter_desc q_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_qcounter_stats, rx_out_of_buffer) },
};

#define VPORT_COUNTER_OFF(c) MLX5_BYTE_OFF(query_vport_counter_out, c)
#define VPORT_COUNTER_GET(vstats, c) MLX5_GET64(query_vport_counter_out, \
						vstats->query_vport_out, c)

struct mlx5e_vport_stats {
	__be64 query_vport_out[MLX5_ST_SZ_QW(query_vport_counter_out)];
};

static const struct counter_desc vport_stats_desc[] = {
	{ "rx_vport_unicast_packets",
		VPORT_COUNTER_OFF(received_eth_unicast.packets) },
	{ "rx_vport_unicast_bytes",
		VPORT_COUNTER_OFF(received_eth_unicast.octets) },
	{ "tx_vport_unicast_packets",
		VPORT_COUNTER_OFF(transmitted_eth_unicast.packets) },
	{ "tx_vport_unicast_bytes",
		VPORT_COUNTER_OFF(transmitted_eth_unicast.octets) },
	{ "rx_vport_multicast_packets",
		VPORT_COUNTER_OFF(received_eth_multicast.packets) },
	{ "rx_vport_multicast_bytes",
		VPORT_COUNTER_OFF(received_eth_multicast.octets) },
	{ "tx_vport_multicast_packets",
		VPORT_COUNTER_OFF(transmitted_eth_multicast.packets) },
	{ "tx_vport_multicast_bytes",
		VPORT_COUNTER_OFF(transmitted_eth_multicast.octets) },
	{ "rx_vport_broadcast_packets",
		VPORT_COUNTER_OFF(received_eth_broadcast.packets) },
	{ "rx_vport_broadcast_bytes",
		VPORT_COUNTER_OFF(received_eth_broadcast.octets) },
	{ "tx_vport_broadcast_packets",
		VPORT_COUNTER_OFF(transmitted_eth_broadcast.packets) },
	{ "tx_vport_broadcast_bytes",
		VPORT_COUNTER_OFF(transmitted_eth_broadcast.octets) },
	{ "rx_vport_rdma_unicast_packets",
		VPORT_COUNTER_OFF(received_ib_unicast.packets) },
	{ "rx_vport_rdma_unicast_bytes",
		VPORT_COUNTER_OFF(received_ib_unicast.octets) },
	{ "tx_vport_rdma_unicast_packets",
		VPORT_COUNTER_OFF(transmitted_ib_unicast.packets) },
	{ "tx_vport_rdma_unicast_bytes",
		VPORT_COUNTER_OFF(transmitted_ib_unicast.octets) },
	{ "rx_vport_rdma_multicast_packets",
		VPORT_COUNTER_OFF(received_ib_multicast.packets) },
	{ "rx_vport_rdma_multicast_bytes",
		VPORT_COUNTER_OFF(received_ib_multicast.octets) },
	{ "tx_vport_rdma_multicast_packets",
		VPORT_COUNTER_OFF(transmitted_ib_multicast.packets) },
	{ "tx_vport_rdma_multicast_bytes",
		VPORT_COUNTER_OFF(transmitted_ib_multicast.octets) },
};

#define PPORT_802_3_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_802_3_cntrs_grp_data_layout.c##_high)
#define PPORT_802_3_GET(pstats, c) \
	MLX5_GET64(ppcnt_reg, pstats->IEEE_802_3_counters, \
		   counter_set.eth_802_3_cntrs_grp_data_layout.c##_high)
#define PPORT_2863_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_2863_cntrs_grp_data_layout.c##_high)
#define PPORT_2863_GET(pstats, c) \
	MLX5_GET64(ppcnt_reg, pstats->RFC_2863_counters, \
		   counter_set.eth_2863_cntrs_grp_data_layout.c##_high)
#define PPORT_2819_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_2819_cntrs_grp_data_layout.c##_high)
#define PPORT_2819_GET(pstats, c) \
	MLX5_GET64(ppcnt_reg, pstats->RFC_2819_counters, \
		   counter_set.eth_2819_cntrs_grp_data_layout.c##_high)
#define PPORT_PER_PRIO_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_per_prio_grp_data_layout.c##_high)
#define PPORT_PER_PRIO_GET(pstats, prio, c) \
	MLX5_GET64(ppcnt_reg, pstats->per_prio_counters[prio], \
		   counter_set.eth_per_prio_grp_data_layout.c##_high)
#define NUM_PPORT_PRIO				8

struct mlx5e_pport_stats {
	__be64 IEEE_802_3_counters[MLX5_ST_SZ_QW(ppcnt_reg)];
	__be64 RFC_2863_counters[MLX5_ST_SZ_QW(ppcnt_reg)];
	__be64 RFC_2819_counters[MLX5_ST_SZ_QW(ppcnt_reg)];
	__be64 per_prio_counters[NUM_PPORT_PRIO][MLX5_ST_SZ_QW(ppcnt_reg)];
	__be64 phy_counters[MLX5_ST_SZ_QW(ppcnt_reg)];
};

static const struct counter_desc pport_802_3_stats_desc[] = {
	{ "tx_packets_phy", PPORT_802_3_OFF(a_frames_transmitted_ok) },
	{ "rx_packets_phy", PPORT_802_3_OFF(a_frames_received_ok) },
	{ "rx_crc_errors_phy", PPORT_802_3_OFF(a_frame_check_sequence_errors) },
	{ "tx_bytes_phy", PPORT_802_3_OFF(a_octets_transmitted_ok) },
	{ "rx_bytes_phy", PPORT_802_3_OFF(a_octets_received_ok) },
	{ "tx_multicast_phy", PPORT_802_3_OFF(a_multicast_frames_xmitted_ok) },
	{ "tx_broadcast_phy", PPORT_802_3_OFF(a_broadcast_frames_xmitted_ok) },
	{ "rx_multicast_phy", PPORT_802_3_OFF(a_multicast_frames_received_ok) },
	{ "rx_broadcast_phy", PPORT_802_3_OFF(a_broadcast_frames_received_ok) },
	{ "rx_in_range_len_errors_phy", PPORT_802_3_OFF(a_in_range_length_errors) },
	{ "rx_out_of_range_len_phy", PPORT_802_3_OFF(a_out_of_range_length_field) },
	{ "rx_oversize_pkts_phy", PPORT_802_3_OFF(a_frame_too_long_errors) },
	{ "rx_symbol_err_phy", PPORT_802_3_OFF(a_symbol_error_during_carrier) },
	{ "tx_mac_control_phy", PPORT_802_3_OFF(a_mac_control_frames_transmitted) },
	{ "rx_mac_control_phy", PPORT_802_3_OFF(a_mac_control_frames_received) },
	{ "rx_unsupported_op_phy", PPORT_802_3_OFF(a_unsupported_opcodes_received) },
	{ "rx_pause_ctrl_phy", PPORT_802_3_OFF(a_pause_mac_ctrl_frames_received) },
	{ "tx_pause_ctrl_phy", PPORT_802_3_OFF(a_pause_mac_ctrl_frames_transmitted) },
};

static const struct counter_desc pport_2863_stats_desc[] = {
	{ "rx_discards_phy", PPORT_2863_OFF(if_in_discards) },
	{ "tx_discards_phy", PPORT_2863_OFF(if_out_discards) },
	{ "tx_errors_phy", PPORT_2863_OFF(if_out_errors) },
};

static const struct counter_desc pport_2819_stats_desc[] = {
	{ "rx_undersize_pkts_phy", PPORT_2819_OFF(ether_stats_undersize_pkts) },
	{ "rx_fragments_phy", PPORT_2819_OFF(ether_stats_fragments) },
	{ "rx_jabbers_phy", PPORT_2819_OFF(ether_stats_jabbers) },
	{ "rx_64_bytes_phy", PPORT_2819_OFF(ether_stats_pkts64octets) },
	{ "rx_65_to_127_bytes_phy", PPORT_2819_OFF(ether_stats_pkts65to127octets) },
	{ "rx_128_to_255_bytes_phy", PPORT_2819_OFF(ether_stats_pkts128to255octets) },
	{ "rx_256_to_511_bytes_phy", PPORT_2819_OFF(ether_stats_pkts256to511octets) },
	{ "rx_512_to_1023_bytes_phy", PPORT_2819_OFF(ether_stats_pkts512to1023octets) },
	{ "rx_1024_to_1518_bytes_phy", PPORT_2819_OFF(ether_stats_pkts1024to1518octets) },
	{ "rx_1519_to_2047_bytes_phy", PPORT_2819_OFF(ether_stats_pkts1519to2047octets) },
	{ "rx_2048_to_4095_bytes_phy", PPORT_2819_OFF(ether_stats_pkts2048to4095octets) },
	{ "rx_4096_to_8191_bytes_phy", PPORT_2819_OFF(ether_stats_pkts4096to8191octets) },
	{ "rx_8192_to_10239_bytes_phy", PPORT_2819_OFF(ether_stats_pkts8192to10239octets) },
};

static const struct counter_desc pport_per_prio_traffic_stats_desc[] = {
	{ "rx_prio%d_bytes", PPORT_PER_PRIO_OFF(rx_octets) },
	{ "rx_prio%d_packets", PPORT_PER_PRIO_OFF(rx_frames) },
	{ "tx_prio%d_bytes", PPORT_PER_PRIO_OFF(tx_octets) },
	{ "tx_prio%d_packets", PPORT_PER_PRIO_OFF(tx_frames) },
};

static const struct counter_desc pport_per_prio_pfc_stats_desc[] = {
	/* %s is "global" or "prio{i}" */
	{ "rx_%s_pause", PPORT_PER_PRIO_OFF(rx_pause) },
	{ "rx_%s_pause_duration", PPORT_PER_PRIO_OFF(rx_pause_duration) },
	{ "tx_%s_pause", PPORT_PER_PRIO_OFF(tx_pause) },
	{ "tx_%s_pause_duration", PPORT_PER_PRIO_OFF(tx_pause_duration) },
	{ "rx_%s_pause_transition", PPORT_PER_PRIO_OFF(rx_pause_transition) },
};

struct mlx5e_rq_stats {
	u64 packets;
	u64 bytes;
	u64 csum_complete;
	u64 csum_unnecessary_inner;
	u64 csum_none;
	u64 lro_packets;
	u64 lro_bytes;
	u64 xdp_drop;
	u64 wqe_err;
	u64 mpwqe_filler;
	u64 buff_alloc_err;
	u64 cqe_compress_blks;
	u64 cqe_compress_pkts;
	u64 cache_reuse;
	u64 cache_full;
	u64 cache_empty;
	u64 cache_busy;
};

static const struct counter_desc rq_stats_desc[] = {
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_complete) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_unnecessary_inner) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_none) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, xdp_drop) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, lro_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, lro_bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, wqe_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, mpwqe_filler) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, buff_alloc_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cqe_compress_blks) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cqe_compress_pkts) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_reuse) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_full) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_empty) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_busy) },
};

struct mlx5e_sq_stats {
	/* commonly accessed in data path */
	u64 packets;
	u64 bytes;
	u64 xmit_more;
	u64 tso_packets;
	u64 tso_bytes;
	u64 tso_inner_packets;
	u64 tso_inner_bytes;
	u64 csum_partial_inner;
	u64 nop;
	/* less likely accessed in data path */
	u64 csum_none;
	u64 stopped;
	u64 wake;
	u64 dropped;
};

static const struct counter_desc sq_stats_desc[] = {
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_inner_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_inner_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, csum_partial_inner) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, nop) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, csum_none) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, stopped) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, wake) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, dropped) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, xmit_more) },
};

#define NUM_SW_COUNTERS			ARRAY_SIZE(sw_stats_desc)
#define NUM_Q_COUNTERS			ARRAY_SIZE(q_stats_desc)
#define NUM_VPORT_COUNTERS		ARRAY_SIZE(vport_stats_desc)
#define NUM_PPORT_802_3_COUNTERS	ARRAY_SIZE(pport_802_3_stats_desc)
#define NUM_PPORT_2863_COUNTERS		ARRAY_SIZE(pport_2863_stats_desc)
#define NUM_PPORT_2819_COUNTERS		ARRAY_SIZE(pport_2819_stats_desc)
#define NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS \
	ARRAY_SIZE(pport_per_prio_traffic_stats_desc)
#define NUM_PPORT_PER_PRIO_PFC_COUNTERS \
	ARRAY_SIZE(pport_per_prio_pfc_stats_desc)
#define NUM_PPORT_COUNTERS		(NUM_PPORT_802_3_COUNTERS + \
					 NUM_PPORT_2863_COUNTERS  + \
					 NUM_PPORT_2819_COUNTERS  + \
					 NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS * \
					 NUM_PPORT_PRIO)
#define NUM_RQ_STATS			ARRAY_SIZE(rq_stats_desc)
#define NUM_SQ_STATS			ARRAY_SIZE(sq_stats_desc)

struct mlx5e_stats {
	struct mlx5e_sw_stats sw;
	struct mlx5e_qcounter_stats qcnt;
	struct mlx5e_vport_stats vport;
	struct mlx5e_pport_stats pport;
};

#endif /* __MLX5_EN_STATS_H__ */

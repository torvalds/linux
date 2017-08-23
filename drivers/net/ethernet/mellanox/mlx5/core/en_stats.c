/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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

#include "en.h"

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
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_csum_none) },
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
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_page_reuse) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_reuse) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_empty) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_busy) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_waive) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, link_down_events_phy) },
};

#define NUM_SW_COUNTERS			ARRAY_SIZE(sw_stats_desc)

static int mlx5e_grp_sw_get_num_stats(struct mlx5e_priv *priv)
{
	return NUM_SW_COUNTERS;
}

static int mlx5e_grp_sw_fill_strings(struct mlx5e_priv *priv, u8 *data, int idx)
{
	int i;

	for (i = 0; i < NUM_SW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, sw_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_sw_fill_stats(struct mlx5e_priv *priv, u64 *data, int idx)
{
	int i;

	for (i = 0; i < NUM_SW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(&priv->stats.sw, sw_stats_desc, i);
	return idx;
}

static const struct counter_desc q_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_qcounter_stats, rx_out_of_buffer) },
};

#define NUM_Q_COUNTERS			ARRAY_SIZE(q_stats_desc)

static int mlx5e_grp_q_get_num_stats(struct mlx5e_priv *priv)
{
	return priv->q_counter ? NUM_Q_COUNTERS : 0;
}

static int mlx5e_grp_q_fill_strings(struct mlx5e_priv *priv, u8 *data, int idx)
{
	int i;

	for (i = 0; i < NUM_Q_COUNTERS && priv->q_counter; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, q_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_q_fill_stats(struct mlx5e_priv *priv, u64 *data, int idx)
{
	int i;

	for (i = 0; i < NUM_Q_COUNTERS && priv->q_counter; i++)
		data[idx++] = MLX5E_READ_CTR32_CPU(&priv->stats.qcnt, q_stats_desc, i);
	return idx;
}

#define VPORT_COUNTER_OFF(c) MLX5_BYTE_OFF(query_vport_counter_out, c)
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

#define NUM_VPORT_COUNTERS		ARRAY_SIZE(vport_stats_desc)

static int mlx5e_grp_vport_get_num_stats(struct mlx5e_priv *priv)
{
	return NUM_VPORT_COUNTERS;
}

static int mlx5e_grp_vport_fill_strings(struct mlx5e_priv *priv, u8 *data,
					int idx)
{
	int i;

	for (i = 0; i < NUM_VPORT_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, vport_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_vport_fill_stats(struct mlx5e_priv *priv, u64 *data,
				      int idx)
{
	int i;

	for (i = 0; i < NUM_VPORT_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(priv->stats.vport.query_vport_out,
						  vport_stats_desc, i);
	return idx;
}

#define PPORT_802_3_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_802_3_cntrs_grp_data_layout.c##_high)
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

#define NUM_PPORT_802_3_COUNTERS	ARRAY_SIZE(pport_802_3_stats_desc)

static int mlx5e_grp_802_3_get_num_stats(struct mlx5e_priv *priv)
{
	return NUM_PPORT_802_3_COUNTERS;
}

static int mlx5e_grp_802_3_fill_strings(struct mlx5e_priv *priv, u8 *data,
					int idx)
{
	int i;

	for (i = 0; i < NUM_PPORT_802_3_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, pport_802_3_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_802_3_fill_stats(struct mlx5e_priv *priv, u64 *data,
				      int idx)
{
	int i;

	for (i = 0; i < NUM_PPORT_802_3_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.IEEE_802_3_counters,
						  pport_802_3_stats_desc, i);
	return idx;
}

#define PPORT_2863_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_2863_cntrs_grp_data_layout.c##_high)
static const struct counter_desc pport_2863_stats_desc[] = {
	{ "rx_discards_phy", PPORT_2863_OFF(if_in_discards) },
	{ "tx_discards_phy", PPORT_2863_OFF(if_out_discards) },
	{ "tx_errors_phy", PPORT_2863_OFF(if_out_errors) },
};

#define NUM_PPORT_2863_COUNTERS		ARRAY_SIZE(pport_2863_stats_desc)

static int mlx5e_grp_2863_get_num_stats(struct mlx5e_priv *priv)
{
	return NUM_PPORT_2863_COUNTERS;
}

static int mlx5e_grp_2863_fill_strings(struct mlx5e_priv *priv, u8 *data,
				       int idx)
{
	int i;

	for (i = 0; i < NUM_PPORT_2863_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, pport_2863_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_2863_fill_stats(struct mlx5e_priv *priv, u64 *data,
				     int idx)
{
	int i;

	for (i = 0; i < NUM_PPORT_2863_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.RFC_2863_counters,
						  pport_2863_stats_desc, i);
	return idx;
}

#define PPORT_2819_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_2819_cntrs_grp_data_layout.c##_high)
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

#define NUM_PPORT_2819_COUNTERS		ARRAY_SIZE(pport_2819_stats_desc)

static int mlx5e_grp_2819_get_num_stats(struct mlx5e_priv *priv)
{
	return NUM_PPORT_2819_COUNTERS;
}

static int mlx5e_grp_2819_fill_strings(struct mlx5e_priv *priv, u8 *data,
				       int idx)
{
	int i;

	for (i = 0; i < NUM_PPORT_2819_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, pport_2819_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_2819_fill_stats(struct mlx5e_priv *priv, u64 *data,
				     int idx)
{
	int i;

	for (i = 0; i < NUM_PPORT_2819_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.RFC_2819_counters,
						  pport_2819_stats_desc, i);
	return idx;
}

#define PPORT_PHY_STATISTICAL_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.phys_layer_statistical_cntrs.c##_high)
static const struct counter_desc pport_phy_statistical_stats_desc[] = {
	{ "rx_pcs_symbol_err_phy", PPORT_PHY_STATISTICAL_OFF(phy_symbol_errors) },
	{ "rx_corrected_bits_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits) },
};

#define NUM_PPORT_PHY_COUNTERS		ARRAY_SIZE(pport_phy_statistical_stats_desc)

static int mlx5e_grp_phy_get_num_stats(struct mlx5e_priv *priv)
{
	return MLX5_CAP_PCAM_FEATURE((priv)->mdev, ppcnt_statistical_group) ?
		NUM_PPORT_PHY_COUNTERS : 0;
}

static int mlx5e_grp_phy_fill_strings(struct mlx5e_priv *priv, u8 *data,
				      int idx)
{
	int i;

	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, ppcnt_statistical_group))
		for (i = 0; i < NUM_PPORT_PHY_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pport_phy_statistical_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_phy_fill_stats(struct mlx5e_priv *priv, u64 *data, int idx)
{
	int i;

	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, ppcnt_statistical_group))
		for (i = 0; i < NUM_PPORT_PHY_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.phy_statistical_counters,
						    pport_phy_statistical_stats_desc, i);
	return idx;
}

#define PPORT_ETH_EXT_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_extended_cntrs_grp_data_layout.c##_high)
static const struct counter_desc pport_eth_ext_stats_desc[] = {
	{ "rx_buffer_passed_thres_phy", PPORT_ETH_EXT_OFF(rx_buffer_almost_full) },
};

#define NUM_PPORT_ETH_EXT_COUNTERS	ARRAY_SIZE(pport_eth_ext_stats_desc)

static int mlx5e_grp_eth_ext_get_num_stats(struct mlx5e_priv *priv)
{
	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, rx_buffer_fullness_counters))
		return NUM_PPORT_ETH_EXT_COUNTERS;

	return 0;
}

static int mlx5e_grp_eth_ext_fill_strings(struct mlx5e_priv *priv, u8 *data,
					  int idx)
{
	int i;

	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, rx_buffer_fullness_counters))
		for (i = 0; i < NUM_PPORT_ETH_EXT_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pport_eth_ext_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_eth_ext_fill_stats(struct mlx5e_priv *priv, u64 *data,
					int idx)
{
	int i;

	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, rx_buffer_fullness_counters))
		for (i = 0; i < NUM_PPORT_ETH_EXT_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.eth_ext_counters,
						    pport_eth_ext_stats_desc, i);
	return idx;
}

const struct mlx5e_stats_grp mlx5e_stats_grps[] = {
	{
		.get_num_stats = mlx5e_grp_sw_get_num_stats,
		.fill_strings = mlx5e_grp_sw_fill_strings,
		.fill_stats = mlx5e_grp_sw_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_q_get_num_stats,
		.fill_strings = mlx5e_grp_q_fill_strings,
		.fill_stats = mlx5e_grp_q_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_vport_get_num_stats,
		.fill_strings = mlx5e_grp_vport_fill_strings,
		.fill_stats = mlx5e_grp_vport_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_802_3_get_num_stats,
		.fill_strings = mlx5e_grp_802_3_fill_strings,
		.fill_stats = mlx5e_grp_802_3_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_2863_get_num_stats,
		.fill_strings = mlx5e_grp_2863_fill_strings,
		.fill_stats = mlx5e_grp_2863_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_2819_get_num_stats,
		.fill_strings = mlx5e_grp_2819_fill_strings,
		.fill_stats = mlx5e_grp_2819_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_phy_get_num_stats,
		.fill_strings = mlx5e_grp_phy_fill_strings,
		.fill_stats = mlx5e_grp_phy_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_eth_ext_get_num_stats,
		.fill_strings = mlx5e_grp_eth_ext_fill_strings,
		.fill_stats = mlx5e_grp_eth_ext_fill_stats,
	}
};

const int mlx5e_num_stats_grps = ARRAY_SIZE(mlx5e_stats_grps);

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
#include "en_accel/ipsec.h"
#include "en_accel/tls.h"

static const struct counter_desc sw_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_inner_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tso_inner_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_added_vlan_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_nop) },

#ifdef CONFIG_MLX5_EN_TLS
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_ooo) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_resync_bytes) },
#endif

	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_lro_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_lro_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_removed_vlan_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_unnecessary) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_none) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_complete) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_unnecessary_inner) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_redirect) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_xmit) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_cqe) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_csum_none) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_csum_partial) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_csum_partial_inner) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_queue_stopped) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_queue_dropped) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xmit_more) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_recover) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_cqes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_queue_wake) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_udp_seg_rem) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_cqe_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_xmit) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_cqes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_wqe_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_mpwqe_filler_strides) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_buff_alloc_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cqe_compress_blks) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cqe_compress_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_page_reuse) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_reuse) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_empty) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_busy) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_waive) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_congst_umr) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_events) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_poll) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_arm) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_aff_change) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_eq_rearm) },
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

void mlx5e_grp_sw_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_sw_stats temp, *s = &temp;
	int i;

	memset(s, 0, sizeof(*s));

	for (i = 0; i < priv->profile->max_nch(priv->mdev); i++) {
		struct mlx5e_channel_stats *channel_stats =
			&priv->channel_stats[i];
		struct mlx5e_xdpsq_stats *xdpsq_red_stats = &channel_stats->xdpsq;
		struct mlx5e_xdpsq_stats *xdpsq_stats = &channel_stats->rq_xdpsq;
		struct mlx5e_rq_stats *rq_stats = &channel_stats->rq;
		struct mlx5e_ch_stats *ch_stats = &channel_stats->ch;
		int j;

		s->rx_packets	+= rq_stats->packets;
		s->rx_bytes	+= rq_stats->bytes;
		s->rx_lro_packets += rq_stats->lro_packets;
		s->rx_lro_bytes	+= rq_stats->lro_bytes;
		s->rx_removed_vlan_packets += rq_stats->removed_vlan_packets;
		s->rx_csum_none	+= rq_stats->csum_none;
		s->rx_csum_complete += rq_stats->csum_complete;
		s->rx_csum_unnecessary += rq_stats->csum_unnecessary;
		s->rx_csum_unnecessary_inner += rq_stats->csum_unnecessary_inner;
		s->rx_xdp_drop     += rq_stats->xdp_drop;
		s->rx_xdp_redirect += rq_stats->xdp_redirect;
		s->rx_xdp_tx_xmit  += xdpsq_stats->xmit;
		s->rx_xdp_tx_full  += xdpsq_stats->full;
		s->rx_xdp_tx_err   += xdpsq_stats->err;
		s->rx_xdp_tx_cqe   += xdpsq_stats->cqes;
		s->rx_wqe_err   += rq_stats->wqe_err;
		s->rx_mpwqe_filler_cqes    += rq_stats->mpwqe_filler_cqes;
		s->rx_mpwqe_filler_strides += rq_stats->mpwqe_filler_strides;
		s->rx_buff_alloc_err += rq_stats->buff_alloc_err;
		s->rx_cqe_compress_blks += rq_stats->cqe_compress_blks;
		s->rx_cqe_compress_pkts += rq_stats->cqe_compress_pkts;
		s->rx_page_reuse  += rq_stats->page_reuse;
		s->rx_cache_reuse += rq_stats->cache_reuse;
		s->rx_cache_full  += rq_stats->cache_full;
		s->rx_cache_empty += rq_stats->cache_empty;
		s->rx_cache_busy  += rq_stats->cache_busy;
		s->rx_cache_waive += rq_stats->cache_waive;
		s->rx_congst_umr  += rq_stats->congst_umr;
		s->ch_events      += ch_stats->events;
		s->ch_poll        += ch_stats->poll;
		s->ch_arm         += ch_stats->arm;
		s->ch_aff_change  += ch_stats->aff_change;
		s->ch_eq_rearm    += ch_stats->eq_rearm;
		/* xdp redirect */
		s->tx_xdp_xmit    += xdpsq_red_stats->xmit;
		s->tx_xdp_full    += xdpsq_red_stats->full;
		s->tx_xdp_err     += xdpsq_red_stats->err;
		s->tx_xdp_cqes    += xdpsq_red_stats->cqes;

		for (j = 0; j < priv->max_opened_tc; j++) {
			struct mlx5e_sq_stats *sq_stats = &channel_stats->sq[j];

			s->tx_packets		+= sq_stats->packets;
			s->tx_bytes		+= sq_stats->bytes;
			s->tx_tso_packets	+= sq_stats->tso_packets;
			s->tx_tso_bytes		+= sq_stats->tso_bytes;
			s->tx_tso_inner_packets	+= sq_stats->tso_inner_packets;
			s->tx_tso_inner_bytes	+= sq_stats->tso_inner_bytes;
			s->tx_added_vlan_packets += sq_stats->added_vlan_packets;
			s->tx_nop               += sq_stats->nop;
			s->tx_queue_stopped	+= sq_stats->stopped;
			s->tx_queue_wake	+= sq_stats->wake;
			s->tx_udp_seg_rem	+= sq_stats->udp_seg_rem;
			s->tx_queue_dropped	+= sq_stats->dropped;
			s->tx_cqe_err		+= sq_stats->cqe_err;
			s->tx_recover		+= sq_stats->recover;
			s->tx_xmit_more		+= sq_stats->xmit_more;
			s->tx_csum_partial_inner += sq_stats->csum_partial_inner;
			s->tx_csum_none		+= sq_stats->csum_none;
			s->tx_csum_partial	+= sq_stats->csum_partial;
#ifdef CONFIG_MLX5_EN_TLS
			s->tx_tls_ooo		+= sq_stats->tls_ooo;
			s->tx_tls_resync_bytes	+= sq_stats->tls_resync_bytes;
#endif
			s->tx_cqes		+= sq_stats->cqes;
		}
	}

	memcpy(&priv->stats.sw, s, sizeof(*s));
}

static const struct counter_desc q_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_qcounter_stats, rx_out_of_buffer) },
};

static const struct counter_desc drop_rq_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_qcounter_stats, rx_if_down_packets) },
};

#define NUM_Q_COUNTERS			ARRAY_SIZE(q_stats_desc)
#define NUM_DROP_RQ_COUNTERS		ARRAY_SIZE(drop_rq_stats_desc)

static int mlx5e_grp_q_get_num_stats(struct mlx5e_priv *priv)
{
	int num_stats = 0;

	if (priv->q_counter)
		num_stats += NUM_Q_COUNTERS;

	if (priv->drop_rq_q_counter)
		num_stats += NUM_DROP_RQ_COUNTERS;

	return num_stats;
}

static int mlx5e_grp_q_fill_strings(struct mlx5e_priv *priv, u8 *data, int idx)
{
	int i;

	for (i = 0; i < NUM_Q_COUNTERS && priv->q_counter; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       q_stats_desc[i].format);

	for (i = 0; i < NUM_DROP_RQ_COUNTERS && priv->drop_rq_q_counter; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       drop_rq_stats_desc[i].format);

	return idx;
}

static int mlx5e_grp_q_fill_stats(struct mlx5e_priv *priv, u64 *data, int idx)
{
	int i;

	for (i = 0; i < NUM_Q_COUNTERS && priv->q_counter; i++)
		data[idx++] = MLX5E_READ_CTR32_CPU(&priv->stats.qcnt,
						   q_stats_desc, i);
	for (i = 0; i < NUM_DROP_RQ_COUNTERS && priv->drop_rq_q_counter; i++)
		data[idx++] = MLX5E_READ_CTR32_CPU(&priv->stats.qcnt,
						   drop_rq_stats_desc, i);
	return idx;
}

static void mlx5e_grp_q_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_qcounter_stats *qcnt = &priv->stats.qcnt;
	u32 out[MLX5_ST_SZ_DW(query_q_counter_out)];

	if (priv->q_counter &&
	    !mlx5_core_query_q_counter(priv->mdev, priv->q_counter, 0, out,
				       sizeof(out)))
		qcnt->rx_out_of_buffer = MLX5_GET(query_q_counter_out,
						  out, out_of_buffer);
	if (priv->drop_rq_q_counter &&
	    !mlx5_core_query_q_counter(priv->mdev, priv->drop_rq_q_counter, 0,
				       out, sizeof(out)))
		qcnt->rx_if_down_packets = MLX5_GET(query_q_counter_out, out,
						    out_of_buffer);
}

#define VNIC_ENV_OFF(c) MLX5_BYTE_OFF(query_vnic_env_out, c)
static const struct counter_desc vnic_env_stats_desc[] = {
	{ "rx_steer_missed_packets",
		VNIC_ENV_OFF(vport_env.nic_receive_steering_discard) },
};

#define NUM_VNIC_ENV_COUNTERS		ARRAY_SIZE(vnic_env_stats_desc)

static int mlx5e_grp_vnic_env_get_num_stats(struct mlx5e_priv *priv)
{
	return MLX5_CAP_GEN(priv->mdev, nic_receive_steering_discard) ?
		NUM_VNIC_ENV_COUNTERS : 0;
}

static int mlx5e_grp_vnic_env_fill_strings(struct mlx5e_priv *priv, u8 *data,
					   int idx)
{
	int i;

	if (!MLX5_CAP_GEN(priv->mdev, nic_receive_steering_discard))
		return idx;

	for (i = 0; i < NUM_VNIC_ENV_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       vnic_env_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_vnic_env_fill_stats(struct mlx5e_priv *priv, u64 *data,
					 int idx)
{
	int i;

	if (!MLX5_CAP_GEN(priv->mdev, nic_receive_steering_discard))
		return idx;

	for (i = 0; i < NUM_VNIC_ENV_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(priv->stats.vnic.query_vnic_env_out,
						  vnic_env_stats_desc, i);
	return idx;
}

static void mlx5e_grp_vnic_env_update_stats(struct mlx5e_priv *priv)
{
	u32 *out = (u32 *)priv->stats.vnic.query_vnic_env_out;
	int outlen = MLX5_ST_SZ_BYTES(query_vnic_env_out);
	u32 in[MLX5_ST_SZ_DW(query_vnic_env_in)] = {0};
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!MLX5_CAP_GEN(priv->mdev, nic_receive_steering_discard))
		return;

	MLX5_SET(query_vnic_env_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VNIC_ENV);
	MLX5_SET(query_vnic_env_in, in, op_mod, 0);
	MLX5_SET(query_vnic_env_in, in, other_vport, 0);
	mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
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

static void mlx5e_grp_vport_update_stats(struct mlx5e_priv *priv)
{
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	u32 *out = (u32 *)priv->stats.vport.query_vport_out;
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)] = {0};
	struct mlx5_core_dev *mdev = priv->mdev;

	MLX5_SET(query_vport_counter_in, in, opcode, MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	MLX5_SET(query_vport_counter_in, in, op_mod, 0);
	MLX5_SET(query_vport_counter_in, in, other_vport, 0);
	mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
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

static void mlx5e_grp_802_3_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->IEEE_802_3_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_IEEE_802_3_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
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

static void mlx5e_grp_2863_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->RFC_2863_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2863_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
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

static void mlx5e_grp_2819_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->RFC_2819_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2819_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
}

#define PPORT_PHY_STATISTICAL_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.phys_layer_statistical_cntrs.c##_high)
static const struct counter_desc pport_phy_statistical_stats_desc[] = {
	{ "rx_pcs_symbol_err_phy", PPORT_PHY_STATISTICAL_OFF(phy_symbol_errors) },
	{ "rx_corrected_bits_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits) },
};

#define NUM_PPORT_PHY_STATISTICAL_COUNTERS ARRAY_SIZE(pport_phy_statistical_stats_desc)

static int mlx5e_grp_phy_get_num_stats(struct mlx5e_priv *priv)
{
	/* "1" for link_down_events special counter */
	return MLX5_CAP_PCAM_FEATURE((priv)->mdev, ppcnt_statistical_group) ?
		NUM_PPORT_PHY_STATISTICAL_COUNTERS + 1 : 1;
}

static int mlx5e_grp_phy_fill_strings(struct mlx5e_priv *priv, u8 *data,
				      int idx)
{
	int i;

	strcpy(data + (idx++) * ETH_GSTRING_LEN, "link_down_events_phy");

	if (!MLX5_CAP_PCAM_FEATURE((priv)->mdev, ppcnt_statistical_group))
		return idx;

	for (i = 0; i < NUM_PPORT_PHY_STATISTICAL_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       pport_phy_statistical_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_phy_fill_stats(struct mlx5e_priv *priv, u64 *data, int idx)
{
	int i;

	/* link_down_events_phy has special handling since it is not stored in __be64 format */
	data[idx++] = MLX5_GET(ppcnt_reg, priv->stats.pport.phy_counters,
			       counter_set.phys_layer_cntrs.link_down_events);

	if (!MLX5_CAP_PCAM_FEATURE((priv)->mdev, ppcnt_statistical_group))
		return idx;

	for (i = 0; i < NUM_PPORT_PHY_STATISTICAL_COUNTERS; i++)
		data[idx++] =
			MLX5E_READ_CTR64_BE(&priv->stats.pport.phy_statistical_counters,
					    pport_phy_statistical_stats_desc, i);
	return idx;
}

static void mlx5e_grp_phy_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->phy_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PHYSICAL_LAYER_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	if (!MLX5_CAP_PCAM_FEATURE(mdev, ppcnt_statistical_group))
		return;

	out = pstats->phy_statistical_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PHYSICAL_LAYER_STATISTICAL_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
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

static void mlx5e_grp_eth_ext_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	if (!MLX5_CAP_PCAM_FEATURE(mdev, rx_buffer_fullness_counters))
		return;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->eth_ext_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_ETHERNET_EXTENDED_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
}

#define PCIE_PERF_OFF(c) \
	MLX5_BYTE_OFF(mpcnt_reg, counter_set.pcie_perf_cntrs_grp_data_layout.c)
static const struct counter_desc pcie_perf_stats_desc[] = {
	{ "rx_pci_signal_integrity", PCIE_PERF_OFF(rx_errors) },
	{ "tx_pci_signal_integrity", PCIE_PERF_OFF(tx_errors) },
};

#define PCIE_PERF_OFF64(c) \
	MLX5_BYTE_OFF(mpcnt_reg, counter_set.pcie_perf_cntrs_grp_data_layout.c##_high)
static const struct counter_desc pcie_perf_stats_desc64[] = {
	{ "outbound_pci_buffer_overflow", PCIE_PERF_OFF64(tx_overflow_buffer_pkt) },
};

static const struct counter_desc pcie_perf_stall_stats_desc[] = {
	{ "outbound_pci_stalled_rd", PCIE_PERF_OFF(outbound_stalled_reads) },
	{ "outbound_pci_stalled_wr", PCIE_PERF_OFF(outbound_stalled_writes) },
	{ "outbound_pci_stalled_rd_events", PCIE_PERF_OFF(outbound_stalled_reads_events) },
	{ "outbound_pci_stalled_wr_events", PCIE_PERF_OFF(outbound_stalled_writes_events) },
};

#define NUM_PCIE_PERF_COUNTERS		ARRAY_SIZE(pcie_perf_stats_desc)
#define NUM_PCIE_PERF_COUNTERS64	ARRAY_SIZE(pcie_perf_stats_desc64)
#define NUM_PCIE_PERF_STALL_COUNTERS	ARRAY_SIZE(pcie_perf_stall_stats_desc)

static int mlx5e_grp_pcie_get_num_stats(struct mlx5e_priv *priv)
{
	int num_stats = 0;

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, pcie_performance_group))
		num_stats += NUM_PCIE_PERF_COUNTERS;

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, tx_overflow_buffer_pkt))
		num_stats += NUM_PCIE_PERF_COUNTERS64;

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, pcie_outbound_stalled))
		num_stats += NUM_PCIE_PERF_STALL_COUNTERS;

	return num_stats;
}

static int mlx5e_grp_pcie_fill_strings(struct mlx5e_priv *priv, u8 *data,
				       int idx)
{
	int i;

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, pcie_performance_group))
		for (i = 0; i < NUM_PCIE_PERF_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pcie_perf_stats_desc[i].format);

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, tx_overflow_buffer_pkt))
		for (i = 0; i < NUM_PCIE_PERF_COUNTERS64; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pcie_perf_stats_desc64[i].format);

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, pcie_outbound_stalled))
		for (i = 0; i < NUM_PCIE_PERF_STALL_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pcie_perf_stall_stats_desc[i].format);
	return idx;
}

static int mlx5e_grp_pcie_fill_stats(struct mlx5e_priv *priv, u64 *data,
				     int idx)
{
	int i;

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, pcie_performance_group))
		for (i = 0; i < NUM_PCIE_PERF_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR32_BE(&priv->stats.pcie.pcie_perf_counters,
						    pcie_perf_stats_desc, i);

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, tx_overflow_buffer_pkt))
		for (i = 0; i < NUM_PCIE_PERF_COUNTERS64; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pcie.pcie_perf_counters,
						    pcie_perf_stats_desc64, i);

	if (MLX5_CAP_MCAM_FEATURE((priv)->mdev, pcie_outbound_stalled))
		for (i = 0; i < NUM_PCIE_PERF_STALL_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR32_BE(&priv->stats.pcie.pcie_perf_counters,
						    pcie_perf_stall_stats_desc, i);
	return idx;
}

static void mlx5e_grp_pcie_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pcie_stats *pcie_stats = &priv->stats.pcie;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(mpcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(mpcnt_reg);
	void *out;

	if (!MLX5_CAP_MCAM_FEATURE(mdev, pcie_performance_group))
		return;

	out = pcie_stats->pcie_perf_counters;
	MLX5_SET(mpcnt_reg, in, grp, MLX5_PCIE_PERFORMANCE_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_MPCNT, 0, 0);
}

#define PPORT_PER_PRIO_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_per_prio_grp_data_layout.c##_high)
static const struct counter_desc pport_per_prio_traffic_stats_desc[] = {
	{ "rx_prio%d_bytes", PPORT_PER_PRIO_OFF(rx_octets) },
	{ "rx_prio%d_packets", PPORT_PER_PRIO_OFF(rx_frames) },
	{ "tx_prio%d_bytes", PPORT_PER_PRIO_OFF(tx_octets) },
	{ "tx_prio%d_packets", PPORT_PER_PRIO_OFF(tx_frames) },
};

#define NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS	ARRAY_SIZE(pport_per_prio_traffic_stats_desc)

static int mlx5e_grp_per_prio_traffic_get_num_stats(void)
{
	return NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS * NUM_PPORT_PRIO;
}

static int mlx5e_grp_per_prio_traffic_fill_strings(struct mlx5e_priv *priv,
						   u8 *data,
						   int idx)
{
	int i, prio;

	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS; i++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_prio_traffic_stats_desc[i].format, prio);
	}

	return idx;
}

static int mlx5e_grp_per_prio_traffic_fill_stats(struct mlx5e_priv *priv,
						 u64 *data,
						 int idx)
{
	int i, prio;

	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_TRAFFIC_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[prio],
						    pport_per_prio_traffic_stats_desc, i);
	}

	return idx;
}

static const struct counter_desc pport_per_prio_pfc_stats_desc[] = {
	/* %s is "global" or "prio{i}" */
	{ "rx_%s_pause", PPORT_PER_PRIO_OFF(rx_pause) },
	{ "rx_%s_pause_duration", PPORT_PER_PRIO_OFF(rx_pause_duration) },
	{ "tx_%s_pause", PPORT_PER_PRIO_OFF(tx_pause) },
	{ "tx_%s_pause_duration", PPORT_PER_PRIO_OFF(tx_pause_duration) },
	{ "rx_%s_pause_transition", PPORT_PER_PRIO_OFF(rx_pause_transition) },
};

static const struct counter_desc pport_pfc_stall_stats_desc[] = {
	{ "tx_pause_storm_warning_events ", PPORT_PER_PRIO_OFF(device_stall_minor_watermark_cnt) },
	{ "tx_pause_storm_error_events", PPORT_PER_PRIO_OFF(device_stall_critical_watermark_cnt) },
};

#define NUM_PPORT_PER_PRIO_PFC_COUNTERS		ARRAY_SIZE(pport_per_prio_pfc_stats_desc)
#define NUM_PPORT_PFC_STALL_COUNTERS(priv)	(ARRAY_SIZE(pport_pfc_stall_stats_desc) * \
						 MLX5_CAP_PCAM_FEATURE((priv)->mdev, pfcc_mask) * \
						 MLX5_CAP_DEBUG((priv)->mdev, stall_detect))

static unsigned long mlx5e_query_pfc_combined(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 pfc_en_tx;
	u8 pfc_en_rx;
	int err;

	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return 0;

	err = mlx5_query_port_pfc(mdev, &pfc_en_tx, &pfc_en_rx);

	return err ? 0 : pfc_en_tx | pfc_en_rx;
}

static bool mlx5e_query_global_pause_combined(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 rx_pause;
	u32 tx_pause;
	int err;

	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return false;

	err = mlx5_query_port_pause(mdev, &rx_pause, &tx_pause);

	return err ? false : rx_pause | tx_pause;
}

static int mlx5e_grp_per_prio_pfc_get_num_stats(struct mlx5e_priv *priv)
{
	return (mlx5e_query_global_pause_combined(priv) +
		hweight8(mlx5e_query_pfc_combined(priv))) *
		NUM_PPORT_PER_PRIO_PFC_COUNTERS +
		NUM_PPORT_PFC_STALL_COUNTERS(priv);
}

static int mlx5e_grp_per_prio_pfc_fill_strings(struct mlx5e_priv *priv,
					       u8 *data,
					       int idx)
{
	unsigned long pfc_combined;
	int i, prio;

	pfc_combined = mlx5e_query_pfc_combined(priv);
	for_each_set_bit(prio, &pfc_combined, NUM_PPORT_PRIO) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			char pfc_string[ETH_GSTRING_LEN];

			snprintf(pfc_string, sizeof(pfc_string), "prio%d", prio);
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_prio_pfc_stats_desc[i].format, pfc_string);
		}
	}

	if (mlx5e_query_global_pause_combined(priv)) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_prio_pfc_stats_desc[i].format, "global");
		}
	}

	for (i = 0; i < NUM_PPORT_PFC_STALL_COUNTERS(priv); i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       pport_pfc_stall_stats_desc[i].format);

	return idx;
}

static int mlx5e_grp_per_prio_pfc_fill_stats(struct mlx5e_priv *priv,
					     u64 *data,
					     int idx)
{
	unsigned long pfc_combined;
	int i, prio;

	pfc_combined = mlx5e_query_pfc_combined(priv);
	for_each_set_bit(prio, &pfc_combined, NUM_PPORT_PRIO) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[prio],
						    pport_per_prio_pfc_stats_desc, i);
		}
	}

	if (mlx5e_query_global_pause_combined(priv)) {
		for (i = 0; i < NUM_PPORT_PER_PRIO_PFC_COUNTERS; i++) {
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[0],
						    pport_per_prio_pfc_stats_desc, i);
		}
	}

	for (i = 0; i < NUM_PPORT_PFC_STALL_COUNTERS(priv); i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.per_prio_counters[0],
						  pport_pfc_stall_stats_desc, i);

	return idx;
}

static int mlx5e_grp_per_prio_get_num_stats(struct mlx5e_priv *priv)
{
	return mlx5e_grp_per_prio_traffic_get_num_stats() +
		mlx5e_grp_per_prio_pfc_get_num_stats(priv);
}

static int mlx5e_grp_per_prio_fill_strings(struct mlx5e_priv *priv, u8 *data,
					   int idx)
{
	idx = mlx5e_grp_per_prio_traffic_fill_strings(priv, data, idx);
	idx = mlx5e_grp_per_prio_pfc_fill_strings(priv, data, idx);
	return idx;
}

static int mlx5e_grp_per_prio_fill_stats(struct mlx5e_priv *priv, u64 *data,
					 int idx)
{
	idx = mlx5e_grp_per_prio_traffic_fill_stats(priv, data, idx);
	idx = mlx5e_grp_per_prio_pfc_fill_stats(priv, data, idx);
	return idx;
}

static void mlx5e_grp_per_prio_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	int prio;
	void *out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PER_PRIORITY_COUNTERS_GROUP);
	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		out = pstats->per_prio_counters[prio];
		MLX5_SET(ppcnt_reg, in, prio_tc, prio);
		mlx5_core_access_reg(mdev, in, sz, out, sz,
				     MLX5_REG_PPCNT, 0, 0);
	}
}

static const struct counter_desc mlx5e_pme_status_desc[] = {
	{ "module_unplug", 8 },
};

static const struct counter_desc mlx5e_pme_error_desc[] = {
	{ "module_bus_stuck", 16 },       /* bus stuck (I2C or data shorted) */
	{ "module_high_temp", 48 },       /* high temperature */
	{ "module_bad_shorted", 56 },    /* bad or shorted cable/module */
};

#define NUM_PME_STATUS_STATS		ARRAY_SIZE(mlx5e_pme_status_desc)
#define NUM_PME_ERR_STATS		ARRAY_SIZE(mlx5e_pme_error_desc)

static int mlx5e_grp_pme_get_num_stats(struct mlx5e_priv *priv)
{
	return NUM_PME_STATUS_STATS + NUM_PME_ERR_STATS;
}

static int mlx5e_grp_pme_fill_strings(struct mlx5e_priv *priv, u8 *data,
				      int idx)
{
	int i;

	for (i = 0; i < NUM_PME_STATUS_STATS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, mlx5e_pme_status_desc[i].format);

	for (i = 0; i < NUM_PME_ERR_STATS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, mlx5e_pme_error_desc[i].format);

	return idx;
}

static int mlx5e_grp_pme_fill_stats(struct mlx5e_priv *priv, u64 *data,
				    int idx)
{
	struct mlx5_priv *mlx5_priv = &priv->mdev->priv;
	int i;

	for (i = 0; i < NUM_PME_STATUS_STATS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(mlx5_priv->pme_stats.status_counters,
						   mlx5e_pme_status_desc, i);

	for (i = 0; i < NUM_PME_ERR_STATS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(mlx5_priv->pme_stats.error_counters,
						   mlx5e_pme_error_desc, i);

	return idx;
}

static int mlx5e_grp_ipsec_get_num_stats(struct mlx5e_priv *priv)
{
	return mlx5e_ipsec_get_count(priv);
}

static int mlx5e_grp_ipsec_fill_strings(struct mlx5e_priv *priv, u8 *data,
					int idx)
{
	return idx + mlx5e_ipsec_get_strings(priv,
					     data + idx * ETH_GSTRING_LEN);
}

static int mlx5e_grp_ipsec_fill_stats(struct mlx5e_priv *priv, u64 *data,
				      int idx)
{
	return idx + mlx5e_ipsec_get_stats(priv, data + idx);
}

static void mlx5e_grp_ipsec_update_stats(struct mlx5e_priv *priv)
{
	mlx5e_ipsec_update_stats(priv);
}

static int mlx5e_grp_tls_get_num_stats(struct mlx5e_priv *priv)
{
	return mlx5e_tls_get_count(priv);
}

static int mlx5e_grp_tls_fill_strings(struct mlx5e_priv *priv, u8 *data,
				      int idx)
{
	return idx + mlx5e_tls_get_strings(priv, data + idx * ETH_GSTRING_LEN);
}

static int mlx5e_grp_tls_fill_stats(struct mlx5e_priv *priv, u64 *data, int idx)
{
	return idx + mlx5e_tls_get_stats(priv, data + idx);
}

static const struct counter_desc rq_stats_desc[] = {
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_complete) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_unnecessary) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_unnecessary_inner) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_none) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, xdp_drop) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, xdp_redirect) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, lro_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, lro_bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, removed_vlan_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, wqe_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, mpwqe_filler_strides) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, buff_alloc_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cqe_compress_blks) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cqe_compress_pkts) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, page_reuse) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_reuse) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_full) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_empty) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_busy) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_waive) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, congst_umr) },
};

static const struct counter_desc sq_stats_desc[] = {
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_inner_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tso_inner_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, csum_partial) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, csum_partial_inner) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, added_vlan_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, nop) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, csum_none) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, stopped) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, dropped) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, xmit_more) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, recover) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, cqes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, wake) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, cqe_err) },
};

static const struct counter_desc rq_xdpsq_stats_desc[] = {
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, xmit) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, full) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, err) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, cqes) },
};

static const struct counter_desc xdpsq_stats_desc[] = {
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, xmit) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, full) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, err) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, cqes) },
};

static const struct counter_desc ch_stats_desc[] = {
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, events) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, poll) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, arm) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, aff_change) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, eq_rearm) },
};

#define NUM_RQ_STATS			ARRAY_SIZE(rq_stats_desc)
#define NUM_SQ_STATS			ARRAY_SIZE(sq_stats_desc)
#define NUM_XDPSQ_STATS			ARRAY_SIZE(xdpsq_stats_desc)
#define NUM_RQ_XDPSQ_STATS		ARRAY_SIZE(rq_xdpsq_stats_desc)
#define NUM_CH_STATS			ARRAY_SIZE(ch_stats_desc)

static int mlx5e_grp_channels_get_num_stats(struct mlx5e_priv *priv)
{
	int max_nch = priv->profile->max_nch(priv->mdev);

	return (NUM_RQ_STATS * max_nch) +
	       (NUM_CH_STATS * max_nch) +
	       (NUM_SQ_STATS * max_nch * priv->max_opened_tc) +
	       (NUM_RQ_XDPSQ_STATS * max_nch) +
	       (NUM_XDPSQ_STATS * max_nch);
}

static int mlx5e_grp_channels_fill_strings(struct mlx5e_priv *priv, u8 *data,
					   int idx)
{
	int max_nch = priv->profile->max_nch(priv->mdev);
	int i, j, tc;

	for (i = 0; i < max_nch; i++)
		for (j = 0; j < NUM_CH_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				ch_stats_desc[j].format, i);

	for (i = 0; i < max_nch; i++) {
		for (j = 0; j < NUM_RQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				rq_stats_desc[j].format, i);
		for (j = 0; j < NUM_RQ_XDPSQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				rq_xdpsq_stats_desc[j].format, i);
	}

	for (tc = 0; tc < priv->max_opened_tc; tc++)
		for (i = 0; i < max_nch; i++)
			for (j = 0; j < NUM_SQ_STATS; j++)
				sprintf(data + (idx++) * ETH_GSTRING_LEN,
					sq_stats_desc[j].format,
					priv->channel_tc2txq[i][tc]);

	for (i = 0; i < max_nch; i++)
		for (j = 0; j < NUM_XDPSQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				xdpsq_stats_desc[j].format, i);

	return idx;
}

static int mlx5e_grp_channels_fill_stats(struct mlx5e_priv *priv, u64 *data,
					 int idx)
{
	int max_nch = priv->profile->max_nch(priv->mdev);
	int i, j, tc;

	for (i = 0; i < max_nch; i++)
		for (j = 0; j < NUM_CH_STATS; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].ch,
						     ch_stats_desc, j);

	for (i = 0; i < max_nch; i++) {
		for (j = 0; j < NUM_RQ_STATS; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].rq,
						     rq_stats_desc, j);
		for (j = 0; j < NUM_RQ_XDPSQ_STATS; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].rq_xdpsq,
						     rq_xdpsq_stats_desc, j);
	}

	for (tc = 0; tc < priv->max_opened_tc; tc++)
		for (i = 0; i < max_nch; i++)
			for (j = 0; j < NUM_SQ_STATS; j++)
				data[idx++] =
					MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].sq[tc],
							     sq_stats_desc, j);

	for (i = 0; i < max_nch; i++)
		for (j = 0; j < NUM_XDPSQ_STATS; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].xdpsq,
						     xdpsq_stats_desc, j);

	return idx;
}

/* The stats groups order is opposite to the update_stats() order calls */
const struct mlx5e_stats_grp mlx5e_stats_grps[] = {
	{
		.get_num_stats = mlx5e_grp_sw_get_num_stats,
		.fill_strings = mlx5e_grp_sw_fill_strings,
		.fill_stats = mlx5e_grp_sw_fill_stats,
		.update_stats = mlx5e_grp_sw_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_q_get_num_stats,
		.fill_strings = mlx5e_grp_q_fill_strings,
		.fill_stats = mlx5e_grp_q_fill_stats,
		.update_stats_mask = MLX5E_NDO_UPDATE_STATS,
		.update_stats = mlx5e_grp_q_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_vnic_env_get_num_stats,
		.fill_strings = mlx5e_grp_vnic_env_fill_strings,
		.fill_stats = mlx5e_grp_vnic_env_fill_stats,
		.update_stats = mlx5e_grp_vnic_env_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_vport_get_num_stats,
		.fill_strings = mlx5e_grp_vport_fill_strings,
		.fill_stats = mlx5e_grp_vport_fill_stats,
		.update_stats_mask = MLX5E_NDO_UPDATE_STATS,
		.update_stats = mlx5e_grp_vport_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_802_3_get_num_stats,
		.fill_strings = mlx5e_grp_802_3_fill_strings,
		.fill_stats = mlx5e_grp_802_3_fill_stats,
		.update_stats_mask = MLX5E_NDO_UPDATE_STATS,
		.update_stats = mlx5e_grp_802_3_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_2863_get_num_stats,
		.fill_strings = mlx5e_grp_2863_fill_strings,
		.fill_stats = mlx5e_grp_2863_fill_stats,
		.update_stats = mlx5e_grp_2863_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_2819_get_num_stats,
		.fill_strings = mlx5e_grp_2819_fill_strings,
		.fill_stats = mlx5e_grp_2819_fill_stats,
		.update_stats = mlx5e_grp_2819_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_phy_get_num_stats,
		.fill_strings = mlx5e_grp_phy_fill_strings,
		.fill_stats = mlx5e_grp_phy_fill_stats,
		.update_stats = mlx5e_grp_phy_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_eth_ext_get_num_stats,
		.fill_strings = mlx5e_grp_eth_ext_fill_strings,
		.fill_stats = mlx5e_grp_eth_ext_fill_stats,
		.update_stats = mlx5e_grp_eth_ext_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_pcie_get_num_stats,
		.fill_strings = mlx5e_grp_pcie_fill_strings,
		.fill_stats = mlx5e_grp_pcie_fill_stats,
		.update_stats = mlx5e_grp_pcie_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_per_prio_get_num_stats,
		.fill_strings = mlx5e_grp_per_prio_fill_strings,
		.fill_stats = mlx5e_grp_per_prio_fill_stats,
		.update_stats = mlx5e_grp_per_prio_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_pme_get_num_stats,
		.fill_strings = mlx5e_grp_pme_fill_strings,
		.fill_stats = mlx5e_grp_pme_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_ipsec_get_num_stats,
		.fill_strings = mlx5e_grp_ipsec_fill_strings,
		.fill_stats = mlx5e_grp_ipsec_fill_stats,
		.update_stats = mlx5e_grp_ipsec_update_stats,
	},
	{
		.get_num_stats = mlx5e_grp_tls_get_num_stats,
		.fill_strings = mlx5e_grp_tls_fill_strings,
		.fill_stats = mlx5e_grp_tls_fill_stats,
	},
	{
		.get_num_stats = mlx5e_grp_channels_get_num_stats,
		.fill_strings = mlx5e_grp_channels_fill_strings,
		.fill_stats = mlx5e_grp_channels_fill_stats,
	}
};

const int mlx5e_num_stats_grps = ARRAY_SIZE(mlx5e_stats_grps);

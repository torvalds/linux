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

#include "lib/mlx5.h"
#include "en.h"
#include "en_accel/tls.h"
#include "en_accel/en_accel.h"
#include "en/ptp.h"

static unsigned int stats_grps_num(struct mlx5e_priv *priv)
{
	return !priv->profile->stats_grps_num ? 0 :
		priv->profile->stats_grps_num(priv);
}

unsigned int mlx5e_stats_total_num(struct mlx5e_priv *priv)
{
	mlx5e_stats_grp_t *stats_grps = priv->profile->stats_grps;
	const unsigned int num_stats_grps = stats_grps_num(priv);
	unsigned int total = 0;
	int i;

	for (i = 0; i < num_stats_grps; i++)
		total += stats_grps[i]->get_num_stats(priv);

	return total;
}

void mlx5e_stats_update_ndo_stats(struct mlx5e_priv *priv)
{
	mlx5e_stats_grp_t *stats_grps = priv->profile->stats_grps;
	const unsigned int num_stats_grps = stats_grps_num(priv);
	int i;

	for (i = num_stats_grps - 1; i >= 0; i--)
		if (stats_grps[i]->update_stats &&
		    stats_grps[i]->update_stats_mask & MLX5E_NDO_UPDATE_STATS)
			stats_grps[i]->update_stats(priv);
}

void mlx5e_stats_update(struct mlx5e_priv *priv)
{
	mlx5e_stats_grp_t *stats_grps = priv->profile->stats_grps;
	const unsigned int num_stats_grps = stats_grps_num(priv);
	int i;

	for (i = num_stats_grps - 1; i >= 0; i--)
		if (stats_grps[i]->update_stats)
			stats_grps[i]->update_stats(priv);
}

void mlx5e_stats_fill(struct mlx5e_priv *priv, u64 *data, int idx)
{
	mlx5e_stats_grp_t *stats_grps = priv->profile->stats_grps;
	const unsigned int num_stats_grps = stats_grps_num(priv);
	int i;

	for (i = 0; i < num_stats_grps; i++)
		idx = stats_grps[i]->fill_stats(priv, data, idx);
}

void mlx5e_stats_fill_strings(struct mlx5e_priv *priv, u8 *data)
{
	mlx5e_stats_grp_t *stats_grps = priv->profile->stats_grps;
	const unsigned int num_stats_grps = stats_grps_num(priv);
	int i, idx = 0;

	for (i = 0; i < num_stats_grps; i++)
		idx = stats_grps[i]->fill_strings(priv, data, idx);
}

/* Concrete NIC Stats */

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
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_mpwqe_blks) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_mpwqe_pkts) },

#ifdef CONFIG_MLX5_EN_TLS
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_encrypted_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_encrypted_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_ooo) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_dump_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_dump_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_resync_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_skip_no_sync_data) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_drop_no_sync_data) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_tls_drop_bypass_req) },
#endif

	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_lro_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_lro_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_gro_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_gro_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_gro_skbs) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_gro_match_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_gro_large_hds) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_ecn_mark) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_removed_vlan_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_unnecessary) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_none) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_complete) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_complete_tail) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_complete_tail_slow) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_csum_unnecessary_inner) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_redirect) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_xmit) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_mpwqe) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_inlnw) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xdp_tx_nops) },
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
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_cqe_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_xmit) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_mpwqe) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_inlnw) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_nops) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xdp_cqes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_wqe_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_mpwqe_filler_strides) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_oversize_pkts_sw_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_buff_alloc_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cqe_compress_blks) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cqe_compress_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_reuse) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_empty) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_busy) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_cache_waive) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_congst_umr) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_arfs_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_recover) },
#ifdef CONFIG_MLX5_EN_TLS
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_decrypted_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_decrypted_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_req_pkt) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_req_start) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_req_end) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_req_skip) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_res_ok) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_res_retry) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_resync_res_skip) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_tls_err) },
#endif
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_events) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_poll) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_arm) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_aff_change) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_force_irq) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, ch_eq_rearm) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_csum_complete) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_csum_unnecessary) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_csum_unnecessary_inner) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_csum_none) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_ecn_mark) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_removed_vlan_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_xdp_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_xdp_redirect) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_wqe_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_mpwqe_filler_strides) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_oversize_pkts_sw_drop) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_buff_alloc_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_cqe_compress_blks) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_cqe_compress_pkts) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_congst_umr) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_xsk_arfs_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xsk_xmit) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xsk_mpwqe) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xsk_inlnw) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xsk_full) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xsk_err) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_xsk_cqes) },
};

#define NUM_SW_COUNTERS			ARRAY_SIZE(sw_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(sw)
{
	return NUM_SW_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(sw)
{
	int i;

	for (i = 0; i < NUM_SW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, sw_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(sw)
{
	int i;

	for (i = 0; i < NUM_SW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(&priv->stats.sw, sw_stats_desc, i);
	return idx;
}

static void mlx5e_stats_grp_sw_update_stats_xdp_red(struct mlx5e_sw_stats *s,
						    struct mlx5e_xdpsq_stats *xdpsq_red_stats)
{
	s->tx_xdp_xmit  += xdpsq_red_stats->xmit;
	s->tx_xdp_mpwqe += xdpsq_red_stats->mpwqe;
	s->tx_xdp_inlnw += xdpsq_red_stats->inlnw;
	s->tx_xdp_nops  += xdpsq_red_stats->nops;
	s->tx_xdp_full  += xdpsq_red_stats->full;
	s->tx_xdp_err   += xdpsq_red_stats->err;
	s->tx_xdp_cqes  += xdpsq_red_stats->cqes;
}

static void mlx5e_stats_grp_sw_update_stats_xdpsq(struct mlx5e_sw_stats *s,
						  struct mlx5e_xdpsq_stats *xdpsq_stats)
{
	s->rx_xdp_tx_xmit  += xdpsq_stats->xmit;
	s->rx_xdp_tx_mpwqe += xdpsq_stats->mpwqe;
	s->rx_xdp_tx_inlnw += xdpsq_stats->inlnw;
	s->rx_xdp_tx_nops  += xdpsq_stats->nops;
	s->rx_xdp_tx_full  += xdpsq_stats->full;
	s->rx_xdp_tx_err   += xdpsq_stats->err;
	s->rx_xdp_tx_cqe   += xdpsq_stats->cqes;
}

static void mlx5e_stats_grp_sw_update_stats_xsksq(struct mlx5e_sw_stats *s,
						  struct mlx5e_xdpsq_stats *xsksq_stats)
{
	s->tx_xsk_xmit  += xsksq_stats->xmit;
	s->tx_xsk_mpwqe += xsksq_stats->mpwqe;
	s->tx_xsk_inlnw += xsksq_stats->inlnw;
	s->tx_xsk_full  += xsksq_stats->full;
	s->tx_xsk_err   += xsksq_stats->err;
	s->tx_xsk_cqes  += xsksq_stats->cqes;
}

static void mlx5e_stats_grp_sw_update_stats_xskrq(struct mlx5e_sw_stats *s,
						  struct mlx5e_rq_stats *xskrq_stats)
{
	s->rx_xsk_packets                += xskrq_stats->packets;
	s->rx_xsk_bytes                  += xskrq_stats->bytes;
	s->rx_xsk_csum_complete          += xskrq_stats->csum_complete;
	s->rx_xsk_csum_unnecessary       += xskrq_stats->csum_unnecessary;
	s->rx_xsk_csum_unnecessary_inner += xskrq_stats->csum_unnecessary_inner;
	s->rx_xsk_csum_none              += xskrq_stats->csum_none;
	s->rx_xsk_ecn_mark               += xskrq_stats->ecn_mark;
	s->rx_xsk_removed_vlan_packets   += xskrq_stats->removed_vlan_packets;
	s->rx_xsk_xdp_drop               += xskrq_stats->xdp_drop;
	s->rx_xsk_xdp_redirect           += xskrq_stats->xdp_redirect;
	s->rx_xsk_wqe_err                += xskrq_stats->wqe_err;
	s->rx_xsk_mpwqe_filler_cqes      += xskrq_stats->mpwqe_filler_cqes;
	s->rx_xsk_mpwqe_filler_strides   += xskrq_stats->mpwqe_filler_strides;
	s->rx_xsk_oversize_pkts_sw_drop  += xskrq_stats->oversize_pkts_sw_drop;
	s->rx_xsk_buff_alloc_err         += xskrq_stats->buff_alloc_err;
	s->rx_xsk_cqe_compress_blks      += xskrq_stats->cqe_compress_blks;
	s->rx_xsk_cqe_compress_pkts      += xskrq_stats->cqe_compress_pkts;
	s->rx_xsk_congst_umr             += xskrq_stats->congst_umr;
	s->rx_xsk_arfs_err               += xskrq_stats->arfs_err;
}

static void mlx5e_stats_grp_sw_update_stats_rq_stats(struct mlx5e_sw_stats *s,
						     struct mlx5e_rq_stats *rq_stats)
{
	s->rx_packets                 += rq_stats->packets;
	s->rx_bytes                   += rq_stats->bytes;
	s->rx_lro_packets             += rq_stats->lro_packets;
	s->rx_lro_bytes               += rq_stats->lro_bytes;
	s->rx_gro_packets             += rq_stats->gro_packets;
	s->rx_gro_bytes               += rq_stats->gro_bytes;
	s->rx_gro_skbs                += rq_stats->gro_skbs;
	s->rx_gro_match_packets       += rq_stats->gro_match_packets;
	s->rx_gro_large_hds           += rq_stats->gro_large_hds;
	s->rx_ecn_mark                += rq_stats->ecn_mark;
	s->rx_removed_vlan_packets    += rq_stats->removed_vlan_packets;
	s->rx_csum_none               += rq_stats->csum_none;
	s->rx_csum_complete           += rq_stats->csum_complete;
	s->rx_csum_complete_tail      += rq_stats->csum_complete_tail;
	s->rx_csum_complete_tail_slow += rq_stats->csum_complete_tail_slow;
	s->rx_csum_unnecessary        += rq_stats->csum_unnecessary;
	s->rx_csum_unnecessary_inner  += rq_stats->csum_unnecessary_inner;
	s->rx_xdp_drop                += rq_stats->xdp_drop;
	s->rx_xdp_redirect            += rq_stats->xdp_redirect;
	s->rx_wqe_err                 += rq_stats->wqe_err;
	s->rx_mpwqe_filler_cqes       += rq_stats->mpwqe_filler_cqes;
	s->rx_mpwqe_filler_strides    += rq_stats->mpwqe_filler_strides;
	s->rx_oversize_pkts_sw_drop   += rq_stats->oversize_pkts_sw_drop;
	s->rx_buff_alloc_err          += rq_stats->buff_alloc_err;
	s->rx_cqe_compress_blks       += rq_stats->cqe_compress_blks;
	s->rx_cqe_compress_pkts       += rq_stats->cqe_compress_pkts;
	s->rx_cache_reuse             += rq_stats->cache_reuse;
	s->rx_cache_full              += rq_stats->cache_full;
	s->rx_cache_empty             += rq_stats->cache_empty;
	s->rx_cache_busy              += rq_stats->cache_busy;
	s->rx_cache_waive             += rq_stats->cache_waive;
	s->rx_congst_umr              += rq_stats->congst_umr;
	s->rx_arfs_err                += rq_stats->arfs_err;
	s->rx_recover                 += rq_stats->recover;
#ifdef CONFIG_MLX5_EN_TLS
	s->rx_tls_decrypted_packets   += rq_stats->tls_decrypted_packets;
	s->rx_tls_decrypted_bytes     += rq_stats->tls_decrypted_bytes;
	s->rx_tls_resync_req_pkt      += rq_stats->tls_resync_req_pkt;
	s->rx_tls_resync_req_start    += rq_stats->tls_resync_req_start;
	s->rx_tls_resync_req_end      += rq_stats->tls_resync_req_end;
	s->rx_tls_resync_req_skip     += rq_stats->tls_resync_req_skip;
	s->rx_tls_resync_res_ok       += rq_stats->tls_resync_res_ok;
	s->rx_tls_resync_res_retry    += rq_stats->tls_resync_res_retry;
	s->rx_tls_resync_res_skip     += rq_stats->tls_resync_res_skip;
	s->rx_tls_err                 += rq_stats->tls_err;
#endif
}

static void mlx5e_stats_grp_sw_update_stats_ch_stats(struct mlx5e_sw_stats *s,
						     struct mlx5e_ch_stats *ch_stats)
{
	s->ch_events      += ch_stats->events;
	s->ch_poll        += ch_stats->poll;
	s->ch_arm         += ch_stats->arm;
	s->ch_aff_change  += ch_stats->aff_change;
	s->ch_force_irq   += ch_stats->force_irq;
	s->ch_eq_rearm    += ch_stats->eq_rearm;
}

static void mlx5e_stats_grp_sw_update_stats_sq(struct mlx5e_sw_stats *s,
					       struct mlx5e_sq_stats *sq_stats)
{
	s->tx_packets               += sq_stats->packets;
	s->tx_bytes                 += sq_stats->bytes;
	s->tx_tso_packets           += sq_stats->tso_packets;
	s->tx_tso_bytes             += sq_stats->tso_bytes;
	s->tx_tso_inner_packets     += sq_stats->tso_inner_packets;
	s->tx_tso_inner_bytes       += sq_stats->tso_inner_bytes;
	s->tx_added_vlan_packets    += sq_stats->added_vlan_packets;
	s->tx_nop                   += sq_stats->nop;
	s->tx_mpwqe_blks            += sq_stats->mpwqe_blks;
	s->tx_mpwqe_pkts            += sq_stats->mpwqe_pkts;
	s->tx_queue_stopped         += sq_stats->stopped;
	s->tx_queue_wake            += sq_stats->wake;
	s->tx_queue_dropped         += sq_stats->dropped;
	s->tx_cqe_err               += sq_stats->cqe_err;
	s->tx_recover               += sq_stats->recover;
	s->tx_xmit_more             += sq_stats->xmit_more;
	s->tx_csum_partial_inner    += sq_stats->csum_partial_inner;
	s->tx_csum_none             += sq_stats->csum_none;
	s->tx_csum_partial          += sq_stats->csum_partial;
#ifdef CONFIG_MLX5_EN_TLS
	s->tx_tls_encrypted_packets += sq_stats->tls_encrypted_packets;
	s->tx_tls_encrypted_bytes   += sq_stats->tls_encrypted_bytes;
	s->tx_tls_ooo               += sq_stats->tls_ooo;
	s->tx_tls_dump_bytes        += sq_stats->tls_dump_bytes;
	s->tx_tls_dump_packets      += sq_stats->tls_dump_packets;
	s->tx_tls_resync_bytes      += sq_stats->tls_resync_bytes;
	s->tx_tls_skip_no_sync_data += sq_stats->tls_skip_no_sync_data;
	s->tx_tls_drop_no_sync_data += sq_stats->tls_drop_no_sync_data;
	s->tx_tls_drop_bypass_req   += sq_stats->tls_drop_bypass_req;
#endif
	s->tx_cqes                  += sq_stats->cqes;
}

static void mlx5e_stats_grp_sw_update_stats_ptp(struct mlx5e_priv *priv,
						struct mlx5e_sw_stats *s)
{
	int i;

	if (!priv->tx_ptp_opened && !priv->rx_ptp_opened)
		return;

	mlx5e_stats_grp_sw_update_stats_ch_stats(s, &priv->ptp_stats.ch);

	if (priv->tx_ptp_opened) {
		for (i = 0; i < priv->max_opened_tc; i++) {
			mlx5e_stats_grp_sw_update_stats_sq(s, &priv->ptp_stats.sq[i]);

			/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92657 */
			barrier();
		}
	}
	if (priv->rx_ptp_opened) {
		mlx5e_stats_grp_sw_update_stats_rq_stats(s, &priv->ptp_stats.rq);

		/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92657 */
		barrier();
	}
}

static void mlx5e_stats_grp_sw_update_stats_qos(struct mlx5e_priv *priv,
						struct mlx5e_sw_stats *s)
{
	struct mlx5e_sq_stats **stats;
	u16 max_qos_sqs;
	int i;

	/* Pairs with smp_store_release in mlx5e_open_qos_sq. */
	max_qos_sqs = smp_load_acquire(&priv->htb.max_qos_sqs);
	stats = READ_ONCE(priv->htb.qos_sq_stats);

	for (i = 0; i < max_qos_sqs; i++) {
		mlx5e_stats_grp_sw_update_stats_sq(s, READ_ONCE(stats[i]));

		/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92657 */
		barrier();
	}
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(sw)
{
	struct mlx5e_sw_stats *s = &priv->stats.sw;
	int i;

	memset(s, 0, sizeof(*s));

	for (i = 0; i < priv->stats_nch; i++) {
		struct mlx5e_channel_stats *channel_stats =
			&priv->channel_stats[i];
		int j;

		mlx5e_stats_grp_sw_update_stats_rq_stats(s, &channel_stats->rq);
		mlx5e_stats_grp_sw_update_stats_xdpsq(s, &channel_stats->rq_xdpsq);
		mlx5e_stats_grp_sw_update_stats_ch_stats(s, &channel_stats->ch);
		/* xdp redirect */
		mlx5e_stats_grp_sw_update_stats_xdp_red(s, &channel_stats->xdpsq);
		/* AF_XDP zero-copy */
		mlx5e_stats_grp_sw_update_stats_xskrq(s, &channel_stats->xskrq);
		mlx5e_stats_grp_sw_update_stats_xsksq(s, &channel_stats->xsksq);

		for (j = 0; j < priv->max_opened_tc; j++) {
			mlx5e_stats_grp_sw_update_stats_sq(s, &channel_stats->sq[j]);

			/* https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92657 */
			barrier();
		}
	}
	mlx5e_stats_grp_sw_update_stats_ptp(priv, s);
	mlx5e_stats_grp_sw_update_stats_qos(priv, s);
}

static const struct counter_desc q_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_qcounter_stats, rx_out_of_buffer) },
};

static const struct counter_desc drop_rq_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_qcounter_stats, rx_if_down_packets) },
};

#define NUM_Q_COUNTERS			ARRAY_SIZE(q_stats_desc)
#define NUM_DROP_RQ_COUNTERS		ARRAY_SIZE(drop_rq_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(qcnt)
{
	int num_stats = 0;

	if (priv->q_counter)
		num_stats += NUM_Q_COUNTERS;

	if (priv->drop_rq_q_counter)
		num_stats += NUM_DROP_RQ_COUNTERS;

	return num_stats;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(qcnt)
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

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(qcnt)
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

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(qcnt)
{
	struct mlx5e_qcounter_stats *qcnt = &priv->stats.qcnt;
	u32 out[MLX5_ST_SZ_DW(query_q_counter_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_q_counter_in)] = {};
	int ret;

	MLX5_SET(query_q_counter_in, in, opcode, MLX5_CMD_OP_QUERY_Q_COUNTER);

	if (priv->q_counter) {
		MLX5_SET(query_q_counter_in, in, counter_set_id,
			 priv->q_counter);
		ret = mlx5_cmd_exec_inout(priv->mdev, query_q_counter, in, out);
		if (!ret)
			qcnt->rx_out_of_buffer = MLX5_GET(query_q_counter_out,
							  out, out_of_buffer);
	}

	if (priv->drop_rq_q_counter) {
		MLX5_SET(query_q_counter_in, in, counter_set_id,
			 priv->drop_rq_q_counter);
		ret = mlx5_cmd_exec_inout(priv->mdev, query_q_counter, in, out);
		if (!ret)
			qcnt->rx_if_down_packets = MLX5_GET(query_q_counter_out,
							    out, out_of_buffer);
	}
}

#define VNIC_ENV_OFF(c) MLX5_BYTE_OFF(query_vnic_env_out, c)
static const struct counter_desc vnic_env_stats_steer_desc[] = {
	{ "rx_steer_missed_packets",
		VNIC_ENV_OFF(vport_env.nic_receive_steering_discard) },
};

static const struct counter_desc vnic_env_stats_dev_oob_desc[] = {
	{ "dev_internal_queue_oob",
		VNIC_ENV_OFF(vport_env.internal_rq_out_of_buffer) },
};

#define NUM_VNIC_ENV_STEER_COUNTERS(dev) \
	(MLX5_CAP_GEN(dev, nic_receive_steering_discard) ? \
	 ARRAY_SIZE(vnic_env_stats_steer_desc) : 0)
#define NUM_VNIC_ENV_DEV_OOB_COUNTERS(dev) \
	(MLX5_CAP_GEN(dev, vnic_env_int_rq_oob) ? \
	 ARRAY_SIZE(vnic_env_stats_dev_oob_desc) : 0)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(vnic_env)
{
	return NUM_VNIC_ENV_STEER_COUNTERS(priv->mdev) +
		NUM_VNIC_ENV_DEV_OOB_COUNTERS(priv->mdev);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(vnic_env)
{
	int i;

	for (i = 0; i < NUM_VNIC_ENV_STEER_COUNTERS(priv->mdev); i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       vnic_env_stats_steer_desc[i].format);

	for (i = 0; i < NUM_VNIC_ENV_DEV_OOB_COUNTERS(priv->mdev); i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       vnic_env_stats_dev_oob_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(vnic_env)
{
	int i;

	for (i = 0; i < NUM_VNIC_ENV_STEER_COUNTERS(priv->mdev); i++)
		data[idx++] = MLX5E_READ_CTR64_BE(priv->stats.vnic.query_vnic_env_out,
						  vnic_env_stats_steer_desc, i);

	for (i = 0; i < NUM_VNIC_ENV_DEV_OOB_COUNTERS(priv->mdev); i++)
		data[idx++] = MLX5E_READ_CTR32_BE(priv->stats.vnic.query_vnic_env_out,
						  vnic_env_stats_dev_oob_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(vnic_env)
{
	u32 *out = (u32 *)priv->stats.vnic.query_vnic_env_out;
	u32 in[MLX5_ST_SZ_DW(query_vnic_env_in)] = {};
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!MLX5_CAP_GEN(priv->mdev, nic_receive_steering_discard))
		return;

	MLX5_SET(query_vnic_env_in, in, opcode, MLX5_CMD_OP_QUERY_VNIC_ENV);
	mlx5_cmd_exec_inout(mdev, query_vnic_env, in, out);
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

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(vport)
{
	return NUM_VPORT_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(vport)
{
	int i;

	for (i = 0; i < NUM_VPORT_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, vport_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(vport)
{
	int i;

	for (i = 0; i < NUM_VPORT_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(priv->stats.vport.query_vport_out,
						  vport_stats_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(vport)
{
	u32 *out = (u32 *)priv->stats.vport.query_vport_out;
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)] = {};
	struct mlx5_core_dev *mdev = priv->mdev;

	MLX5_SET(query_vport_counter_in, in, opcode, MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	mlx5_cmd_exec_inout(mdev, query_vport_counter, in, out);
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

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(802_3)
{
	return NUM_PPORT_802_3_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(802_3)
{
	int i;

	for (i = 0; i < NUM_PPORT_802_3_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, pport_802_3_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(802_3)
{
	int i;

	for (i = 0; i < NUM_PPORT_802_3_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.IEEE_802_3_counters,
						  pport_802_3_stats_desc, i);
	return idx;
}

#define MLX5_BASIC_PPCNT_SUPPORTED(mdev) \
	(MLX5_CAP_GEN(mdev, pcam_reg) ? MLX5_CAP_PCAM_REG(mdev, ppcnt) : 1)

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(802_3)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	if (!MLX5_BASIC_PPCNT_SUPPORTED(mdev))
		return;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->IEEE_802_3_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_IEEE_802_3_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
}

#define MLX5E_READ_CTR64_BE_F(ptr, set, c)		\
	be64_to_cpu(*(__be64 *)((char *)ptr +		\
		MLX5_BYTE_OFF(ppcnt_reg,		\
			      counter_set.set.c##_high)))

static int mlx5e_stats_get_ieee(struct mlx5_core_dev *mdev,
				u32 *ppcnt_ieee_802_3)
{
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

	if (!MLX5_BASIC_PPCNT_SUPPORTED(mdev))
		return -EOPNOTSUPP;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_IEEE_802_3_COUNTERS_GROUP);
	return mlx5_core_access_reg(mdev, in, sz, ppcnt_ieee_802_3,
				    sz, MLX5_REG_PPCNT, 0, 0);
}

void mlx5e_stats_pause_get(struct mlx5e_priv *priv,
			   struct ethtool_pause_stats *pause_stats)
{
	u32 ppcnt_ieee_802_3[MLX5_ST_SZ_DW(ppcnt_reg)];
	struct mlx5_core_dev *mdev = priv->mdev;

	if (mlx5e_stats_get_ieee(mdev, ppcnt_ieee_802_3))
		return;

	pause_stats->tx_pause_frames =
		MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,
				      eth_802_3_cntrs_grp_data_layout,
				      a_pause_mac_ctrl_frames_transmitted);
	pause_stats->rx_pause_frames =
		MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,
				      eth_802_3_cntrs_grp_data_layout,
				      a_pause_mac_ctrl_frames_received);
}

void mlx5e_stats_eth_phy_get(struct mlx5e_priv *priv,
			     struct ethtool_eth_phy_stats *phy_stats)
{
	u32 ppcnt_ieee_802_3[MLX5_ST_SZ_DW(ppcnt_reg)];
	struct mlx5_core_dev *mdev = priv->mdev;

	if (mlx5e_stats_get_ieee(mdev, ppcnt_ieee_802_3))
		return;

	phy_stats->SymbolErrorDuringCarrier =
		MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,
				      eth_802_3_cntrs_grp_data_layout,
				      a_symbol_error_during_carrier);
}

void mlx5e_stats_eth_mac_get(struct mlx5e_priv *priv,
			     struct ethtool_eth_mac_stats *mac_stats)
{
	u32 ppcnt_ieee_802_3[MLX5_ST_SZ_DW(ppcnt_reg)];
	struct mlx5_core_dev *mdev = priv->mdev;

	if (mlx5e_stats_get_ieee(mdev, ppcnt_ieee_802_3))
		return;

#define RD(name)							\
	MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,				\
			      eth_802_3_cntrs_grp_data_layout,		\
			      name)

	mac_stats->FramesTransmittedOK	= RD(a_frames_transmitted_ok);
	mac_stats->FramesReceivedOK	= RD(a_frames_received_ok);
	mac_stats->FrameCheckSequenceErrors = RD(a_frame_check_sequence_errors);
	mac_stats->OctetsTransmittedOK	= RD(a_octets_transmitted_ok);
	mac_stats->OctetsReceivedOK	= RD(a_octets_received_ok);
	mac_stats->MulticastFramesXmittedOK = RD(a_multicast_frames_xmitted_ok);
	mac_stats->BroadcastFramesXmittedOK = RD(a_broadcast_frames_xmitted_ok);
	mac_stats->MulticastFramesReceivedOK = RD(a_multicast_frames_received_ok);
	mac_stats->BroadcastFramesReceivedOK = RD(a_broadcast_frames_received_ok);
	mac_stats->InRangeLengthErrors	= RD(a_in_range_length_errors);
	mac_stats->OutOfRangeLengthField = RD(a_out_of_range_length_field);
	mac_stats->FrameTooLongErrors	= RD(a_frame_too_long_errors);
#undef RD
}

void mlx5e_stats_eth_ctrl_get(struct mlx5e_priv *priv,
			      struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	u32 ppcnt_ieee_802_3[MLX5_ST_SZ_DW(ppcnt_reg)];
	struct mlx5_core_dev *mdev = priv->mdev;

	if (mlx5e_stats_get_ieee(mdev, ppcnt_ieee_802_3))
		return;

	ctrl_stats->MACControlFramesTransmitted =
		MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,
				      eth_802_3_cntrs_grp_data_layout,
				      a_mac_control_frames_transmitted);
	ctrl_stats->MACControlFramesReceived =
		MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,
				      eth_802_3_cntrs_grp_data_layout,
				      a_mac_control_frames_received);
	ctrl_stats->UnsupportedOpcodesReceived =
		MLX5E_READ_CTR64_BE_F(ppcnt_ieee_802_3,
				      eth_802_3_cntrs_grp_data_layout,
				      a_unsupported_opcodes_received);
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

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(2863)
{
	return NUM_PPORT_2863_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(2863)
{
	int i;

	for (i = 0; i < NUM_PPORT_2863_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, pport_2863_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(2863)
{
	int i;

	for (i = 0; i < NUM_PPORT_2863_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.RFC_2863_counters,
						  pport_2863_stats_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(2863)
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

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(2819)
{
	return NUM_PPORT_2819_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(2819)
{
	int i;

	for (i = 0; i < NUM_PPORT_2819_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, pport_2819_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(2819)
{
	int i;

	for (i = 0; i < NUM_PPORT_2819_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_BE(&priv->stats.pport.RFC_2819_counters,
						  pport_2819_stats_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(2819)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;

	if (!MLX5_BASIC_PPCNT_SUPPORTED(mdev))
		return;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	out = pstats->RFC_2819_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2819_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
}

static const struct ethtool_rmon_hist_range mlx5e_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519,  2047 },
	{ 2048,  4095 },
	{ 4096,  8191 },
	{ 8192, 10239 },
	{}
};

void mlx5e_stats_rmon_get(struct mlx5e_priv *priv,
			  struct ethtool_rmon_stats *rmon,
			  const struct ethtool_rmon_hist_range **ranges)
{
	u32 ppcnt_RFC_2819_counters[MLX5_ST_SZ_DW(ppcnt_reg)];
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2819_COUNTERS_GROUP);
	if (mlx5_core_access_reg(mdev, in, sz, ppcnt_RFC_2819_counters,
				 sz, MLX5_REG_PPCNT, 0, 0))
		return;

#define RD(name)						\
	MLX5E_READ_CTR64_BE_F(ppcnt_RFC_2819_counters,		\
			      eth_2819_cntrs_grp_data_layout,	\
			      name)

	rmon->undersize_pkts	= RD(ether_stats_undersize_pkts);
	rmon->fragments		= RD(ether_stats_fragments);
	rmon->jabbers		= RD(ether_stats_jabbers);

	rmon->hist[0]		= RD(ether_stats_pkts64octets);
	rmon->hist[1]		= RD(ether_stats_pkts65to127octets);
	rmon->hist[2]		= RD(ether_stats_pkts128to255octets);
	rmon->hist[3]		= RD(ether_stats_pkts256to511octets);
	rmon->hist[4]		= RD(ether_stats_pkts512to1023octets);
	rmon->hist[5]		= RD(ether_stats_pkts1024to1518octets);
	rmon->hist[6]		= RD(ether_stats_pkts1519to2047octets);
	rmon->hist[7]		= RD(ether_stats_pkts2048to4095octets);
	rmon->hist[8]		= RD(ether_stats_pkts4096to8191octets);
	rmon->hist[9]		= RD(ether_stats_pkts8192to10239octets);
#undef RD

	*ranges = mlx5e_rmon_ranges;
}

#define PPORT_PHY_STATISTICAL_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.phys_layer_statistical_cntrs.c##_high)
static const struct counter_desc pport_phy_statistical_stats_desc[] = {
	{ "rx_pcs_symbol_err_phy", PPORT_PHY_STATISTICAL_OFF(phy_symbol_errors) },
	{ "rx_corrected_bits_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits) },
};

static const struct counter_desc
pport_phy_statistical_err_lanes_stats_desc[] = {
	{ "rx_err_lane_0_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits_lane0) },
	{ "rx_err_lane_1_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits_lane1) },
	{ "rx_err_lane_2_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits_lane2) },
	{ "rx_err_lane_3_phy", PPORT_PHY_STATISTICAL_OFF(phy_corrected_bits_lane3) },
};

#define NUM_PPORT_PHY_STATISTICAL_COUNTERS \
	ARRAY_SIZE(pport_phy_statistical_stats_desc)
#define NUM_PPORT_PHY_STATISTICAL_PER_LANE_COUNTERS \
	ARRAY_SIZE(pport_phy_statistical_err_lanes_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(phy)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int num_stats;

	/* "1" for link_down_events special counter */
	num_stats = 1;

	num_stats += MLX5_CAP_PCAM_FEATURE(mdev, ppcnt_statistical_group) ?
		     NUM_PPORT_PHY_STATISTICAL_COUNTERS : 0;

	num_stats += MLX5_CAP_PCAM_FEATURE(mdev, per_lane_error_counters) ?
		     NUM_PPORT_PHY_STATISTICAL_PER_LANE_COUNTERS : 0;

	return num_stats;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(phy)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int i;

	strcpy(data + (idx++) * ETH_GSTRING_LEN, "link_down_events_phy");

	if (!MLX5_CAP_PCAM_FEATURE(mdev, ppcnt_statistical_group))
		return idx;

	for (i = 0; i < NUM_PPORT_PHY_STATISTICAL_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       pport_phy_statistical_stats_desc[i].format);

	if (MLX5_CAP_PCAM_FEATURE(mdev, per_lane_error_counters))
		for (i = 0; i < NUM_PPORT_PHY_STATISTICAL_PER_LANE_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pport_phy_statistical_err_lanes_stats_desc[i].format);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(phy)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int i;

	/* link_down_events_phy has special handling since it is not stored in __be64 format */
	data[idx++] = MLX5_GET(ppcnt_reg, priv->stats.pport.phy_counters,
			       counter_set.phys_layer_cntrs.link_down_events);

	if (!MLX5_CAP_PCAM_FEATURE(mdev, ppcnt_statistical_group))
		return idx;

	for (i = 0; i < NUM_PPORT_PHY_STATISTICAL_COUNTERS; i++)
		data[idx++] =
			MLX5E_READ_CTR64_BE(&priv->stats.pport.phy_statistical_counters,
					    pport_phy_statistical_stats_desc, i);

	if (MLX5_CAP_PCAM_FEATURE(mdev, per_lane_error_counters))
		for (i = 0; i < NUM_PPORT_PHY_STATISTICAL_PER_LANE_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.phy_statistical_counters,
						    pport_phy_statistical_err_lanes_stats_desc,
						    i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(phy)
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

void mlx5e_stats_fec_get(struct mlx5e_priv *priv,
			 struct ethtool_fec_stats *fec_stats)
{
	u32 ppcnt_phy_statistical[MLX5_ST_SZ_DW(ppcnt_reg)];
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

	if (!MLX5_CAP_PCAM_FEATURE(mdev, ppcnt_statistical_group))
		return;

	MLX5_SET(ppcnt_reg, in, local_port, 1);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PHYSICAL_LAYER_STATISTICAL_GROUP);
	if (mlx5_core_access_reg(mdev, in, sz, ppcnt_phy_statistical,
				 sz, MLX5_REG_PPCNT, 0, 0))
		return;

	fec_stats->corrected_bits.total =
		MLX5E_READ_CTR64_BE_F(ppcnt_phy_statistical,
				      phys_layer_statistical_cntrs,
				      phy_corrected_bits);
}

#define PPORT_ETH_EXT_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_extended_cntrs_grp_data_layout.c##_high)
static const struct counter_desc pport_eth_ext_stats_desc[] = {
	{ "rx_buffer_passed_thres_phy", PPORT_ETH_EXT_OFF(rx_buffer_almost_full) },
};

#define NUM_PPORT_ETH_EXT_COUNTERS	ARRAY_SIZE(pport_eth_ext_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(eth_ext)
{
	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, rx_buffer_fullness_counters))
		return NUM_PPORT_ETH_EXT_COUNTERS;

	return 0;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(eth_ext)
{
	int i;

	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, rx_buffer_fullness_counters))
		for (i = 0; i < NUM_PPORT_ETH_EXT_COUNTERS; i++)
			strcpy(data + (idx++) * ETH_GSTRING_LEN,
			       pport_eth_ext_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(eth_ext)
{
	int i;

	if (MLX5_CAP_PCAM_FEATURE((priv)->mdev, rx_buffer_fullness_counters))
		for (i = 0; i < NUM_PPORT_ETH_EXT_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&priv->stats.pport.eth_ext_counters,
						    pport_eth_ext_stats_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(eth_ext)
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

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(pcie)
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

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(pcie)
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

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(pcie)
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

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(pcie)
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

#define PPORT_PER_TC_PRIO_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_per_tc_prio_grp_data_layout.c##_high)

static const struct counter_desc pport_per_tc_prio_stats_desc[] = {
	{ "rx_prio%d_buf_discard", PPORT_PER_TC_PRIO_OFF(no_buffer_discard_uc) },
};

#define NUM_PPORT_PER_TC_PRIO_COUNTERS	ARRAY_SIZE(pport_per_tc_prio_stats_desc)

#define PPORT_PER_TC_CONGEST_PRIO_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_per_tc_congest_prio_grp_data_layout.c##_high)

static const struct counter_desc pport_per_tc_congest_prio_stats_desc[] = {
	{ "rx_prio%d_cong_discard", PPORT_PER_TC_CONGEST_PRIO_OFF(wred_discard) },
	{ "rx_prio%d_marked", PPORT_PER_TC_CONGEST_PRIO_OFF(ecn_marked_tc) },
};

#define NUM_PPORT_PER_TC_CONGEST_PRIO_COUNTERS \
	ARRAY_SIZE(pport_per_tc_congest_prio_stats_desc)

static int mlx5e_grp_per_tc_prio_get_num_stats(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return 0;

	return NUM_PPORT_PER_TC_PRIO_COUNTERS * NUM_PPORT_PRIO;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(per_port_buff_congest)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int i, prio;

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return idx;

	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		for (i = 0; i < NUM_PPORT_PER_TC_PRIO_COUNTERS; i++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_tc_prio_stats_desc[i].format, prio);
		for (i = 0; i < NUM_PPORT_PER_TC_CONGEST_PRIO_COUNTERS; i++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				pport_per_tc_congest_prio_stats_desc[i].format, prio);
	}

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(per_port_buff_congest)
{
	struct mlx5e_pport_stats *pport = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	int i, prio;

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return idx;

	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		for (i = 0; i < NUM_PPORT_PER_TC_PRIO_COUNTERS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&pport->per_tc_prio_counters[prio],
						    pport_per_tc_prio_stats_desc, i);
		for (i = 0; i < NUM_PPORT_PER_TC_CONGEST_PRIO_COUNTERS ; i++)
			data[idx++] =
				MLX5E_READ_CTR64_BE(&pport->per_tc_congest_prio_counters[prio],
						    pport_per_tc_congest_prio_stats_desc, i);
	}

	return idx;
}

static void mlx5e_grp_per_tc_prio_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;
	int prio;

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return;

	MLX5_SET(ppcnt_reg, in, pnat, 2);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PER_TRAFFIC_CLASS_COUNTERS_GROUP);
	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		out = pstats->per_tc_prio_counters[prio];
		MLX5_SET(ppcnt_reg, in, prio_tc, prio);
		mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
	}
}

static int mlx5e_grp_per_tc_congest_prio_get_num_stats(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return 0;

	return NUM_PPORT_PER_TC_CONGEST_PRIO_COUNTERS * NUM_PPORT_PRIO;
}

static void mlx5e_grp_per_tc_congest_prio_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	void *out;
	int prio;

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return;

	MLX5_SET(ppcnt_reg, in, pnat, 2);
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PER_TRAFFIC_CLASS_CONGESTION_GROUP);
	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		out = pstats->per_tc_congest_prio_counters[prio];
		MLX5_SET(ppcnt_reg, in, prio_tc, prio);
		mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
	}
}

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(per_port_buff_congest)
{
	return mlx5e_grp_per_tc_prio_get_num_stats(priv) +
		mlx5e_grp_per_tc_congest_prio_get_num_stats(priv);
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(per_port_buff_congest)
{
	mlx5e_grp_per_tc_prio_update_stats(priv);
	mlx5e_grp_per_tc_congest_prio_update_stats(priv);
}

#define PPORT_PER_PRIO_OFF(c) \
	MLX5_BYTE_OFF(ppcnt_reg, \
		      counter_set.eth_per_prio_grp_data_layout.c##_high)
static const struct counter_desc pport_per_prio_traffic_stats_desc[] = {
	{ "rx_prio%d_bytes", PPORT_PER_PRIO_OFF(rx_octets) },
	{ "rx_prio%d_packets", PPORT_PER_PRIO_OFF(rx_frames) },
	{ "rx_prio%d_discards", PPORT_PER_PRIO_OFF(rx_discards) },
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
	{ "tx_pause_storm_warning_events", PPORT_PER_PRIO_OFF(device_stall_minor_watermark_cnt) },
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

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(per_prio)
{
	return mlx5e_grp_per_prio_traffic_get_num_stats() +
		mlx5e_grp_per_prio_pfc_get_num_stats(priv);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(per_prio)
{
	idx = mlx5e_grp_per_prio_traffic_fill_strings(priv, data, idx);
	idx = mlx5e_grp_per_prio_pfc_fill_strings(priv, data, idx);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(per_prio)
{
	idx = mlx5e_grp_per_prio_traffic_fill_stats(priv, data, idx);
	idx = mlx5e_grp_per_prio_pfc_fill_stats(priv, data, idx);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(per_prio)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	int prio;
	void *out;

	if (!MLX5_BASIC_PPCNT_SUPPORTED(mdev))
		return;

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
	{ "module_unplug",       sizeof(u64) * MLX5_MODULE_STATUS_UNPLUGGED },
};

static const struct counter_desc mlx5e_pme_error_desc[] = {
	{ "module_bus_stuck",    sizeof(u64) * MLX5_MODULE_EVENT_ERROR_BUS_STUCK },
	{ "module_high_temp",    sizeof(u64) * MLX5_MODULE_EVENT_ERROR_HIGH_TEMPERATURE },
	{ "module_bad_shorted",  sizeof(u64) * MLX5_MODULE_EVENT_ERROR_BAD_CABLE },
};

#define NUM_PME_STATUS_STATS		ARRAY_SIZE(mlx5e_pme_status_desc)
#define NUM_PME_ERR_STATS		ARRAY_SIZE(mlx5e_pme_error_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(pme)
{
	return NUM_PME_STATUS_STATS + NUM_PME_ERR_STATS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(pme)
{
	int i;

	for (i = 0; i < NUM_PME_STATUS_STATS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, mlx5e_pme_status_desc[i].format);

	for (i = 0; i < NUM_PME_ERR_STATS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, mlx5e_pme_error_desc[i].format);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(pme)
{
	struct mlx5_pme_stats pme_stats;
	int i;

	mlx5_get_pme_stats(priv->mdev, &pme_stats);

	for (i = 0; i < NUM_PME_STATUS_STATS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(pme_stats.status_counters,
						   mlx5e_pme_status_desc, i);

	for (i = 0; i < NUM_PME_ERR_STATS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(pme_stats.error_counters,
						   mlx5e_pme_error_desc, i);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(pme) { return; }

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(tls)
{
	return mlx5e_tls_get_count(priv);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(tls)
{
	return idx + mlx5e_tls_get_strings(priv, data + idx * ETH_GSTRING_LEN);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(tls)
{
	return idx + mlx5e_tls_get_stats(priv, data + idx);
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(tls) { return; }

static const struct counter_desc rq_stats_desc[] = {
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_complete) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_complete_tail) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_complete_tail_slow) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_unnecessary) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_unnecessary_inner) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, csum_none) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, xdp_drop) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, xdp_redirect) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, lro_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, lro_bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, gro_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, gro_bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, gro_skbs) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, gro_match_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, gro_large_hds) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, ecn_mark) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, removed_vlan_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, wqe_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, mpwqe_filler_strides) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, oversize_pkts_sw_drop) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, buff_alloc_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cqe_compress_blks) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cqe_compress_pkts) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_reuse) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_full) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_empty) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_busy) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, cache_waive) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, congst_umr) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, arfs_err) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, recover) },
#ifdef CONFIG_MLX5_EN_TLS
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_decrypted_packets) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_decrypted_bytes) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_req_pkt) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_req_start) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_req_end) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_req_skip) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_res_ok) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_res_retry) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_resync_res_skip) },
	{ MLX5E_DECLARE_RX_STAT(struct mlx5e_rq_stats, tls_err) },
#endif
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
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, mpwqe_blks) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, mpwqe_pkts) },
#ifdef CONFIG_MLX5_EN_TLS
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_encrypted_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_encrypted_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_ooo) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_dump_packets) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_dump_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_resync_bytes) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_skip_no_sync_data) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_drop_no_sync_data) },
	{ MLX5E_DECLARE_TX_STAT(struct mlx5e_sq_stats, tls_drop_bypass_req) },
#endif
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
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, mpwqe) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, inlnw) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, nops) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, full) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, err) },
	{ MLX5E_DECLARE_RQ_XDPSQ_STAT(struct mlx5e_xdpsq_stats, cqes) },
};

static const struct counter_desc xdpsq_stats_desc[] = {
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, xmit) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, mpwqe) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, inlnw) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, nops) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, full) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, err) },
	{ MLX5E_DECLARE_XDPSQ_STAT(struct mlx5e_xdpsq_stats, cqes) },
};

static const struct counter_desc xskrq_stats_desc[] = {
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, packets) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, bytes) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, csum_complete) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, csum_unnecessary) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, csum_unnecessary_inner) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, csum_none) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, ecn_mark) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, removed_vlan_packets) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, xdp_drop) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, xdp_redirect) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, wqe_err) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, mpwqe_filler_strides) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, oversize_pkts_sw_drop) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, buff_alloc_err) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, cqe_compress_blks) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, cqe_compress_pkts) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, congst_umr) },
	{ MLX5E_DECLARE_XSKRQ_STAT(struct mlx5e_rq_stats, arfs_err) },
};

static const struct counter_desc xsksq_stats_desc[] = {
	{ MLX5E_DECLARE_XSKSQ_STAT(struct mlx5e_xdpsq_stats, xmit) },
	{ MLX5E_DECLARE_XSKSQ_STAT(struct mlx5e_xdpsq_stats, mpwqe) },
	{ MLX5E_DECLARE_XSKSQ_STAT(struct mlx5e_xdpsq_stats, inlnw) },
	{ MLX5E_DECLARE_XSKSQ_STAT(struct mlx5e_xdpsq_stats, full) },
	{ MLX5E_DECLARE_XSKSQ_STAT(struct mlx5e_xdpsq_stats, err) },
	{ MLX5E_DECLARE_XSKSQ_STAT(struct mlx5e_xdpsq_stats, cqes) },
};

static const struct counter_desc ch_stats_desc[] = {
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, events) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, poll) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, arm) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, aff_change) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, force_irq) },
	{ MLX5E_DECLARE_CH_STAT(struct mlx5e_ch_stats, eq_rearm) },
};

static const struct counter_desc ptp_sq_stats_desc[] = {
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, packets) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, bytes) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, csum_partial) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, csum_partial_inner) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, added_vlan_packets) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, nop) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, csum_none) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, stopped) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, dropped) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, xmit_more) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, recover) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, cqes) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, wake) },
	{ MLX5E_DECLARE_PTP_TX_STAT(struct mlx5e_sq_stats, cqe_err) },
};

static const struct counter_desc ptp_ch_stats_desc[] = {
	{ MLX5E_DECLARE_PTP_CH_STAT(struct mlx5e_ch_stats, events) },
	{ MLX5E_DECLARE_PTP_CH_STAT(struct mlx5e_ch_stats, poll) },
	{ MLX5E_DECLARE_PTP_CH_STAT(struct mlx5e_ch_stats, arm) },
	{ MLX5E_DECLARE_PTP_CH_STAT(struct mlx5e_ch_stats, eq_rearm) },
};

static const struct counter_desc ptp_cq_stats_desc[] = {
	{ MLX5E_DECLARE_PTP_CQ_STAT(struct mlx5e_ptp_cq_stats, cqe) },
	{ MLX5E_DECLARE_PTP_CQ_STAT(struct mlx5e_ptp_cq_stats, err_cqe) },
	{ MLX5E_DECLARE_PTP_CQ_STAT(struct mlx5e_ptp_cq_stats, abort) },
	{ MLX5E_DECLARE_PTP_CQ_STAT(struct mlx5e_ptp_cq_stats, abort_abs_diff_ns) },
};

static const struct counter_desc ptp_rq_stats_desc[] = {
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, packets) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, bytes) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, csum_complete) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, csum_complete_tail) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, csum_complete_tail_slow) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, csum_unnecessary) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, csum_unnecessary_inner) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, csum_none) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, xdp_drop) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, xdp_redirect) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, lro_packets) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, lro_bytes) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, ecn_mark) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, removed_vlan_packets) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, wqe_err) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, mpwqe_filler_cqes) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, mpwqe_filler_strides) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, oversize_pkts_sw_drop) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, buff_alloc_err) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cqe_compress_blks) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cqe_compress_pkts) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cache_reuse) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cache_full) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cache_empty) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cache_busy) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, cache_waive) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, congst_umr) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, arfs_err) },
	{ MLX5E_DECLARE_PTP_RQ_STAT(struct mlx5e_rq_stats, recover) },
};

static const struct counter_desc qos_sq_stats_desc[] = {
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, packets) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, bytes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tso_packets) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tso_bytes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tso_inner_packets) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tso_inner_bytes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, csum_partial) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, csum_partial_inner) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, added_vlan_packets) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, nop) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, mpwqe_blks) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, mpwqe_pkts) },
#ifdef CONFIG_MLX5_EN_TLS
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_encrypted_packets) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_encrypted_bytes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_ooo) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_dump_packets) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_dump_bytes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_resync_bytes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_skip_no_sync_data) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_drop_no_sync_data) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, tls_drop_bypass_req) },
#endif
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, csum_none) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, stopped) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, dropped) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, xmit_more) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, recover) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, cqes) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, wake) },
	{ MLX5E_DECLARE_QOS_TX_STAT(struct mlx5e_sq_stats, cqe_err) },
};

#define NUM_RQ_STATS			ARRAY_SIZE(rq_stats_desc)
#define NUM_SQ_STATS			ARRAY_SIZE(sq_stats_desc)
#define NUM_XDPSQ_STATS			ARRAY_SIZE(xdpsq_stats_desc)
#define NUM_RQ_XDPSQ_STATS		ARRAY_SIZE(rq_xdpsq_stats_desc)
#define NUM_XSKRQ_STATS			ARRAY_SIZE(xskrq_stats_desc)
#define NUM_XSKSQ_STATS			ARRAY_SIZE(xsksq_stats_desc)
#define NUM_CH_STATS			ARRAY_SIZE(ch_stats_desc)
#define NUM_PTP_SQ_STATS		ARRAY_SIZE(ptp_sq_stats_desc)
#define NUM_PTP_CH_STATS		ARRAY_SIZE(ptp_ch_stats_desc)
#define NUM_PTP_CQ_STATS		ARRAY_SIZE(ptp_cq_stats_desc)
#define NUM_PTP_RQ_STATS                ARRAY_SIZE(ptp_rq_stats_desc)
#define NUM_QOS_SQ_STATS		ARRAY_SIZE(qos_sq_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(qos)
{
	/* Pairs with smp_store_release in mlx5e_open_qos_sq. */
	return NUM_QOS_SQ_STATS * smp_load_acquire(&priv->htb.max_qos_sqs);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(qos)
{
	/* Pairs with smp_store_release in mlx5e_open_qos_sq. */
	u16 max_qos_sqs = smp_load_acquire(&priv->htb.max_qos_sqs);
	int i, qid;

	for (qid = 0; qid < max_qos_sqs; qid++)
		for (i = 0; i < NUM_QOS_SQ_STATS; i++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				qos_sq_stats_desc[i].format, qid);

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(qos)
{
	struct mlx5e_sq_stats **stats;
	u16 max_qos_sqs;
	int i, qid;

	/* Pairs with smp_store_release in mlx5e_open_qos_sq. */
	max_qos_sqs = smp_load_acquire(&priv->htb.max_qos_sqs);
	stats = READ_ONCE(priv->htb.qos_sq_stats);

	for (qid = 0; qid < max_qos_sqs; qid++) {
		struct mlx5e_sq_stats *s = READ_ONCE(stats[qid]);

		for (i = 0; i < NUM_QOS_SQ_STATS; i++)
			data[idx++] = MLX5E_READ_CTR64_CPU(s, qos_sq_stats_desc, i);
	}

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(qos) { return; }

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(ptp)
{
	int num = NUM_PTP_CH_STATS;

	if (!priv->tx_ptp_opened && !priv->rx_ptp_opened)
		return 0;

	if (priv->tx_ptp_opened)
		num += (NUM_PTP_SQ_STATS + NUM_PTP_CQ_STATS) * priv->max_opened_tc;
	if (priv->rx_ptp_opened)
		num += NUM_PTP_RQ_STATS;

	return num;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(ptp)
{
	int i, tc;

	if (!priv->tx_ptp_opened && !priv->rx_ptp_opened)
		return idx;

	for (i = 0; i < NUM_PTP_CH_STATS; i++)
		sprintf(data + (idx++) * ETH_GSTRING_LEN,
			ptp_ch_stats_desc[i].format);

	if (priv->tx_ptp_opened) {
		for (tc = 0; tc < priv->max_opened_tc; tc++)
			for (i = 0; i < NUM_PTP_SQ_STATS; i++)
				sprintf(data + (idx++) * ETH_GSTRING_LEN,
					ptp_sq_stats_desc[i].format, tc);

		for (tc = 0; tc < priv->max_opened_tc; tc++)
			for (i = 0; i < NUM_PTP_CQ_STATS; i++)
				sprintf(data + (idx++) * ETH_GSTRING_LEN,
					ptp_cq_stats_desc[i].format, tc);
	}
	if (priv->rx_ptp_opened) {
		for (i = 0; i < NUM_PTP_RQ_STATS; i++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				ptp_rq_stats_desc[i].format, MLX5E_PTP_CHANNEL_IX);
	}
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(ptp)
{
	int i, tc;

	if (!priv->tx_ptp_opened && !priv->rx_ptp_opened)
		return idx;

	for (i = 0; i < NUM_PTP_CH_STATS; i++)
		data[idx++] =
			MLX5E_READ_CTR64_CPU(&priv->ptp_stats.ch,
					     ptp_ch_stats_desc, i);

	if (priv->tx_ptp_opened) {
		for (tc = 0; tc < priv->max_opened_tc; tc++)
			for (i = 0; i < NUM_PTP_SQ_STATS; i++)
				data[idx++] =
					MLX5E_READ_CTR64_CPU(&priv->ptp_stats.sq[tc],
							     ptp_sq_stats_desc, i);

		for (tc = 0; tc < priv->max_opened_tc; tc++)
			for (i = 0; i < NUM_PTP_CQ_STATS; i++)
				data[idx++] =
					MLX5E_READ_CTR64_CPU(&priv->ptp_stats.cq[tc],
							     ptp_cq_stats_desc, i);
	}
	if (priv->rx_ptp_opened) {
		for (i = 0; i < NUM_PTP_RQ_STATS; i++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->ptp_stats.rq,
						     ptp_rq_stats_desc, i);
	}
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(ptp) { return; }

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(channels)
{
	int max_nch = priv->stats_nch;

	return (NUM_RQ_STATS * max_nch) +
	       (NUM_CH_STATS * max_nch) +
	       (NUM_SQ_STATS * max_nch * priv->max_opened_tc) +
	       (NUM_RQ_XDPSQ_STATS * max_nch) +
	       (NUM_XDPSQ_STATS * max_nch) +
	       (NUM_XSKRQ_STATS * max_nch * priv->xsk.ever_used) +
	       (NUM_XSKSQ_STATS * max_nch * priv->xsk.ever_used);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(channels)
{
	bool is_xsk = priv->xsk.ever_used;
	int max_nch = priv->stats_nch;
	int i, j, tc;

	for (i = 0; i < max_nch; i++)
		for (j = 0; j < NUM_CH_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				ch_stats_desc[j].format, i);

	for (i = 0; i < max_nch; i++) {
		for (j = 0; j < NUM_RQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				rq_stats_desc[j].format, i);
		for (j = 0; j < NUM_XSKRQ_STATS * is_xsk; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				xskrq_stats_desc[j].format, i);
		for (j = 0; j < NUM_RQ_XDPSQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				rq_xdpsq_stats_desc[j].format, i);
	}

	for (tc = 0; tc < priv->max_opened_tc; tc++)
		for (i = 0; i < max_nch; i++)
			for (j = 0; j < NUM_SQ_STATS; j++)
				sprintf(data + (idx++) * ETH_GSTRING_LEN,
					sq_stats_desc[j].format,
					i + tc * max_nch);

	for (i = 0; i < max_nch; i++) {
		for (j = 0; j < NUM_XSKSQ_STATS * is_xsk; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				xsksq_stats_desc[j].format, i);
		for (j = 0; j < NUM_XDPSQ_STATS; j++)
			sprintf(data + (idx++) * ETH_GSTRING_LEN,
				xdpsq_stats_desc[j].format, i);
	}

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(channels)
{
	bool is_xsk = priv->xsk.ever_used;
	int max_nch = priv->stats_nch;
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
		for (j = 0; j < NUM_XSKRQ_STATS * is_xsk; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].xskrq,
						     xskrq_stats_desc, j);
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

	for (i = 0; i < max_nch; i++) {
		for (j = 0; j < NUM_XSKSQ_STATS * is_xsk; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].xsksq,
						     xsksq_stats_desc, j);
		for (j = 0; j < NUM_XDPSQ_STATS; j++)
			data[idx++] =
				MLX5E_READ_CTR64_CPU(&priv->channel_stats[i].xdpsq,
						     xdpsq_stats_desc, j);
	}

	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(channels) { return; }

MLX5E_DEFINE_STATS_GRP(sw, 0);
MLX5E_DEFINE_STATS_GRP(qcnt, MLX5E_NDO_UPDATE_STATS);
MLX5E_DEFINE_STATS_GRP(vnic_env, 0);
MLX5E_DEFINE_STATS_GRP(vport, MLX5E_NDO_UPDATE_STATS);
MLX5E_DEFINE_STATS_GRP(802_3, MLX5E_NDO_UPDATE_STATS);
MLX5E_DEFINE_STATS_GRP(2863, 0);
MLX5E_DEFINE_STATS_GRP(2819, 0);
MLX5E_DEFINE_STATS_GRP(phy, 0);
MLX5E_DEFINE_STATS_GRP(pcie, 0);
MLX5E_DEFINE_STATS_GRP(per_prio, 0);
MLX5E_DEFINE_STATS_GRP(pme, 0);
MLX5E_DEFINE_STATS_GRP(channels, 0);
MLX5E_DEFINE_STATS_GRP(per_port_buff_congest, 0);
MLX5E_DEFINE_STATS_GRP(eth_ext, 0);
static MLX5E_DEFINE_STATS_GRP(tls, 0);
static MLX5E_DEFINE_STATS_GRP(ptp, 0);
static MLX5E_DEFINE_STATS_GRP(qos, 0);

/* The stats groups order is opposite to the update_stats() order calls */
mlx5e_stats_grp_t mlx5e_nic_stats_grps[] = {
	&MLX5E_STATS_GRP(sw),
	&MLX5E_STATS_GRP(qcnt),
	&MLX5E_STATS_GRP(vnic_env),
	&MLX5E_STATS_GRP(vport),
	&MLX5E_STATS_GRP(802_3),
	&MLX5E_STATS_GRP(2863),
	&MLX5E_STATS_GRP(2819),
	&MLX5E_STATS_GRP(phy),
	&MLX5E_STATS_GRP(eth_ext),
	&MLX5E_STATS_GRP(pcie),
	&MLX5E_STATS_GRP(per_prio),
	&MLX5E_STATS_GRP(pme),
#ifdef CONFIG_MLX5_EN_IPSEC
	&MLX5E_STATS_GRP(ipsec_sw),
	&MLX5E_STATS_GRP(ipsec_hw),
#endif
	&MLX5E_STATS_GRP(tls),
	&MLX5E_STATS_GRP(channels),
	&MLX5E_STATS_GRP(per_port_buff_congest),
	&MLX5E_STATS_GRP(ptp),
	&MLX5E_STATS_GRP(qos),
};

unsigned int mlx5e_nic_stats_grps_num(struct mlx5e_priv *priv)
{
	return ARRAY_SIZE(mlx5e_nic_stats_grps);
}

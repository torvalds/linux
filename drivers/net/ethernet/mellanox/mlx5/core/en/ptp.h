/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __MLX5_EN_PTP_H__
#define __MLX5_EN_PTP_H__

#include "en.h"
#include "en_stats.h"
#include "en/txrx.h"
#include <linux/ktime.h>
#include <linux/ptp_classify.h>
#include <linux/time64.h>
#include <linux/workqueue.h>

#define MLX5E_PTP_CHANNEL_IX 0
#define MLX5E_PTP_MAX_LOG_SQ_SIZE (8U)
#define MLX5E_PTP_TS_CQE_UNDELIVERED_TIMEOUT (1 * NSEC_PER_SEC)

struct mlx5e_ptp_metadata_fifo {
	u8  cc;
	u8  pc;
	u8  mask;
	u8  *data;
};

struct mlx5e_ptp_metadata_map {
	u16             undelivered_counter;
	u16             capacity;
	struct sk_buff  **data;
};

struct mlx5e_ptpsq {
	struct mlx5e_txqsq       txqsq;
	struct mlx5e_cq          ts_cq;
	struct mlx5e_ptp_cq_stats *cq_stats;
	u16                      ts_cqe_ctr_mask;

	struct work_struct                 report_unhealthy_work;
	struct mlx5e_ptp_port_ts_cqe_list  *ts_cqe_pending_list;
	struct mlx5e_ptp_metadata_fifo     metadata_freelist;
	struct mlx5e_ptp_metadata_map      metadata_map;
};

enum {
	MLX5E_PTP_STATE_TX,
	MLX5E_PTP_STATE_RX,
	MLX5E_PTP_STATE_NUM_STATES,
};

struct mlx5e_ptp {
	/* data path */
	struct mlx5e_ptpsq         ptpsq[MLX5E_MAX_NUM_TC];
	struct mlx5e_rq            rq;
	struct napi_struct         napi;
	struct device             *pdev;
	struct net_device         *netdev;
	__be32                     mkey_be;
	u8                         num_tc;
	u8                         lag_port;

	/* data path - accessed per napi poll */
	struct mlx5e_ch_stats     *stats;

	/* control */
	struct mlx5e_priv         *priv;
	struct mlx5_core_dev      *mdev;
	struct hwtstamp_config    *tstamp;
	DECLARE_BITMAP(state, MLX5E_PTP_STATE_NUM_STATES);
};

static inline bool mlx5e_use_ptpsq(struct sk_buff *skb)
{
	struct flow_keys fk;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		return false;

	if (!skb_flow_dissect_flow_keys(skb, &fk, 0))
		return false;

	if (fk.basic.n_proto == htons(ETH_P_1588))
		return true;

	if (fk.basic.n_proto != htons(ETH_P_IP) &&
	    fk.basic.n_proto != htons(ETH_P_IPV6))
		return false;

	return (fk.basic.ip_proto == IPPROTO_UDP &&
		fk.ports.dst == htons(PTP_EV_PORT));
}

static inline void mlx5e_ptp_metadata_fifo_push(struct mlx5e_ptp_metadata_fifo *fifo, u8 metadata)
{
	fifo->data[fifo->mask & fifo->pc++] = metadata;
}

static inline u8
mlx5e_ptp_metadata_fifo_pop(struct mlx5e_ptp_metadata_fifo *fifo)
{
	return fifo->data[fifo->mask & fifo->cc++];
}

static inline void
mlx5e_ptp_metadata_map_put(struct mlx5e_ptp_metadata_map *map,
			   struct sk_buff *skb, u8 metadata)
{
	WARN_ON_ONCE(map->data[metadata]);
	map->data[metadata] = skb;
}

static inline bool mlx5e_ptpsq_metadata_freelist_empty(struct mlx5e_ptpsq *ptpsq)
{
	struct mlx5e_ptp_metadata_fifo *freelist;

	if (likely(!ptpsq))
		return false;

	freelist = &ptpsq->metadata_freelist;

	return freelist->pc == freelist->cc;
}

int mlx5e_ptp_open(struct mlx5e_priv *priv, struct mlx5e_params *params,
		   u8 lag_port, struct mlx5e_ptp **cp);
void mlx5e_ptp_close(struct mlx5e_ptp *c);
void mlx5e_ptp_activate_channel(struct mlx5e_ptp *c);
void mlx5e_ptp_deactivate_channel(struct mlx5e_ptp *c);
int mlx5e_ptp_get_rqn(struct mlx5e_ptp *c, u32 *rqn);
int mlx5e_ptp_alloc_rx_fs(struct mlx5e_flow_steering *fs,
			  const struct mlx5e_profile *profile);
void mlx5e_ptp_free_rx_fs(struct mlx5e_flow_steering *fs,
			  const struct mlx5e_profile *profile);
int mlx5e_ptp_rx_manage_fs(struct mlx5e_priv *priv, bool set);

void mlx5e_ptpsq_track_metadata(struct mlx5e_ptpsq *ptpsq, u8 metadata);

enum {
	MLX5E_SKB_CB_CQE_HWTSTAMP  = BIT(0),
	MLX5E_SKB_CB_PORT_HWTSTAMP = BIT(1),
};

void mlx5e_skb_cb_hwtstamp_handler(struct sk_buff *skb, int hwtstamp_type,
				   ktime_t hwtstamp,
				   struct mlx5e_ptp_cq_stats *cq_stats);

void mlx5e_skb_cb_hwtstamp_init(struct sk_buff *skb);
#endif /* __MLX5_EN_PTP_H__ */

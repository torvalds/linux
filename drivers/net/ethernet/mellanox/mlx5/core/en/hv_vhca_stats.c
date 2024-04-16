// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2018 Mellanox Technologies

#include "en.h"
#include "en/hv_vhca_stats.h"
#include "lib/hv_vhca.h"
#include "lib/hv.h"

struct mlx5e_hv_vhca_per_ring_stats {
	u64     rx_packets;
	u64     rx_bytes;
	u64     tx_packets;
	u64     tx_bytes;
};

static void
mlx5e_hv_vhca_fill_ring_stats(struct mlx5e_priv *priv, int ch,
			      struct mlx5e_hv_vhca_per_ring_stats *data)
{
	struct mlx5e_channel_stats *stats;
	int tc;

	stats = priv->channel_stats[ch];
	data->rx_packets = stats->rq.packets;
	data->rx_bytes   = stats->rq.bytes;

	for (tc = 0; tc < priv->max_opened_tc; tc++) {
		data->tx_packets += stats->sq[tc].packets;
		data->tx_bytes   += stats->sq[tc].bytes;
	}
}

static void mlx5e_hv_vhca_fill_stats(struct mlx5e_priv *priv, void *data,
				     int buf_len)
{
	int ch, i = 0;

	for (ch = 0; ch < priv->stats_nch; ch++) {
		void *buf = data + i;

		if (WARN_ON_ONCE(buf +
				 sizeof(struct mlx5e_hv_vhca_per_ring_stats) >
				 data + buf_len))
			return;

		mlx5e_hv_vhca_fill_ring_stats(priv, ch, buf);
		i += sizeof(struct mlx5e_hv_vhca_per_ring_stats);
	}
}

static int mlx5e_hv_vhca_stats_buf_size(struct mlx5e_priv *priv)
{
	return (sizeof(struct mlx5e_hv_vhca_per_ring_stats) *
		priv->stats_nch);
}

static void mlx5e_hv_vhca_stats_work(struct work_struct *work)
{
	struct mlx5e_hv_vhca_stats_agent *sagent;
	struct mlx5_hv_vhca_agent *agent;
	struct delayed_work *dwork;
	struct mlx5e_priv *priv;
	int buf_len, rc;
	void *buf;

	dwork = to_delayed_work(work);
	sagent = container_of(dwork, struct mlx5e_hv_vhca_stats_agent, work);
	priv = container_of(sagent, struct mlx5e_priv, stats_agent);
	buf_len = mlx5e_hv_vhca_stats_buf_size(priv);
	agent = sagent->agent;
	buf = sagent->buf;

	memset(buf, 0, buf_len);
	mlx5e_hv_vhca_fill_stats(priv, buf, buf_len);

	rc = mlx5_hv_vhca_agent_write(agent, buf, buf_len);
	if (rc) {
		mlx5_core_err(priv->mdev,
			      "%s: Failed to write stats, err = %d\n",
			      __func__, rc);
		return;
	}

	if (sagent->delay)
		queue_delayed_work(priv->wq, &sagent->work, sagent->delay);
}

enum {
	MLX5_HV_VHCA_STATS_VERSION     = 1,
	MLX5_HV_VHCA_STATS_UPDATE_ONCE = 0xFFFF,
};

static void mlx5e_hv_vhca_stats_control(struct mlx5_hv_vhca_agent *agent,
					struct mlx5_hv_vhca_control_block *block)
{
	struct mlx5e_hv_vhca_stats_agent *sagent;
	struct mlx5e_priv *priv;

	priv = mlx5_hv_vhca_agent_priv(agent);
	sagent = &priv->stats_agent;

	block->version = MLX5_HV_VHCA_STATS_VERSION;
	block->rings   = priv->stats_nch;

	if (!block->command) {
		cancel_delayed_work_sync(&priv->stats_agent.work);
		return;
	}

	sagent->delay = block->command == MLX5_HV_VHCA_STATS_UPDATE_ONCE ? 0 :
			msecs_to_jiffies(block->command * 100);

	queue_delayed_work(priv->wq, &sagent->work, sagent->delay);
}

static void mlx5e_hv_vhca_stats_cleanup(struct mlx5_hv_vhca_agent *agent)
{
	struct mlx5e_priv *priv = mlx5_hv_vhca_agent_priv(agent);

	cancel_delayed_work_sync(&priv->stats_agent.work);
}

void mlx5e_hv_vhca_stats_create(struct mlx5e_priv *priv)
{
	int buf_len = mlx5e_hv_vhca_stats_buf_size(priv);
	struct mlx5_hv_vhca_agent *agent;

	priv->stats_agent.buf = kvzalloc(buf_len, GFP_KERNEL);
	if (!priv->stats_agent.buf)
		return;

	agent = mlx5_hv_vhca_agent_create(priv->mdev->hv_vhca,
					  MLX5_HV_VHCA_AGENT_STATS,
					  mlx5e_hv_vhca_stats_control, NULL,
					  mlx5e_hv_vhca_stats_cleanup,
					  priv);

	if (IS_ERR_OR_NULL(agent)) {
		if (IS_ERR(agent))
			netdev_warn(priv->netdev,
				    "Failed to create hv vhca stats agent, err = %ld\n",
				    PTR_ERR(agent));

		kvfree(priv->stats_agent.buf);
		return;
	}

	priv->stats_agent.agent = agent;
	INIT_DELAYED_WORK(&priv->stats_agent.work, mlx5e_hv_vhca_stats_work);
}

void mlx5e_hv_vhca_stats_destroy(struct mlx5e_priv *priv)
{
	if (IS_ERR_OR_NULL(priv->stats_agent.agent))
		return;

	mlx5_hv_vhca_agent_destroy(priv->stats_agent.agent);
	kvfree(priv->stats_agent.buf);
}

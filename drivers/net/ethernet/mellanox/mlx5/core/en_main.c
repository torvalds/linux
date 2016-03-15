/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/fs.h>
#include "en.h"
#include "eswitch.h"

struct mlx5e_rq_param {
	u32                        rqc[MLX5_ST_SZ_DW(rqc)];
	struct mlx5_wq_param       wq;
};

struct mlx5e_sq_param {
	u32                        sqc[MLX5_ST_SZ_DW(sqc)];
	struct mlx5_wq_param       wq;
	u16                        max_inline;
};

struct mlx5e_cq_param {
	u32                        cqc[MLX5_ST_SZ_DW(cqc)];
	struct mlx5_wq_param       wq;
	u16                        eq_ix;
};

struct mlx5e_channel_param {
	struct mlx5e_rq_param      rq;
	struct mlx5e_sq_param      sq;
	struct mlx5e_cq_param      rx_cq;
	struct mlx5e_cq_param      tx_cq;
};

static void mlx5e_update_carrier(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 port_state;

	port_state = mlx5_query_vport_state(mdev,
		MLX5_QUERY_VPORT_STATE_IN_OP_MOD_VNIC_VPORT, 0);

	if (port_state == VPORT_STATE_UP)
		netif_carrier_on(priv->netdev);
	else
		netif_carrier_off(priv->netdev);
}

static void mlx5e_update_carrier_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       update_carrier_work);

	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		mlx5e_update_carrier(priv);
	mutex_unlock(&priv->state_lock);
}

static void mlx5e_update_pport_counters(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_pport_stats *s = &priv->stats.pport;
	u32 *in;
	u32 *out;
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

	in  = mlx5_vzalloc(sz);
	out = mlx5_vzalloc(sz);
	if (!in || !out)
		goto free_out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);

	MLX5_SET(ppcnt_reg, in, grp, MLX5_IEEE_802_3_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out,
			     sz, MLX5_REG_PPCNT, 0, 0);
	memcpy(s->IEEE_802_3_counters,
	       MLX5_ADDR_OF(ppcnt_reg, out, counter_set),
	       sizeof(s->IEEE_802_3_counters));

	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2863_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out,
			     sz, MLX5_REG_PPCNT, 0, 0);
	memcpy(s->RFC_2863_counters,
	       MLX5_ADDR_OF(ppcnt_reg, out, counter_set),
	       sizeof(s->RFC_2863_counters));

	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2819_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out,
			     sz, MLX5_REG_PPCNT, 0, 0);
	memcpy(s->RFC_2819_counters,
	       MLX5_ADDR_OF(ppcnt_reg, out, counter_set),
	       sizeof(s->RFC_2819_counters));

free_out:
	kvfree(in);
	kvfree(out);
}

void mlx5e_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_vport_stats *s = &priv->stats.vport;
	struct mlx5e_rq_stats *rq_stats;
	struct mlx5e_sq_stats *sq_stats;
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)];
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	u64 tx_offload_none;
	int i, j;

	out = mlx5_vzalloc(outlen);
	if (!out)
		return;

	/* Collect firts the SW counters and then HW for consistency */
	s->rx_packets		= 0;
	s->rx_bytes		= 0;
	s->tx_packets		= 0;
	s->tx_bytes		= 0;
	s->tso_packets		= 0;
	s->tso_bytes		= 0;
	s->tx_queue_stopped	= 0;
	s->tx_queue_wake	= 0;
	s->tx_queue_dropped	= 0;
	tx_offload_none		= 0;
	s->lro_packets		= 0;
	s->lro_bytes		= 0;
	s->rx_csum_none		= 0;
	s->rx_csum_sw		= 0;
	s->rx_wqe_err		= 0;
	for (i = 0; i < priv->params.num_channels; i++) {
		rq_stats = &priv->channel[i]->rq.stats;

		s->rx_packets	+= rq_stats->packets;
		s->rx_bytes	+= rq_stats->bytes;
		s->lro_packets	+= rq_stats->lro_packets;
		s->lro_bytes	+= rq_stats->lro_bytes;
		s->rx_csum_none	+= rq_stats->csum_none;
		s->rx_csum_sw	+= rq_stats->csum_sw;
		s->rx_wqe_err   += rq_stats->wqe_err;

		for (j = 0; j < priv->params.num_tc; j++) {
			sq_stats = &priv->channel[i]->sq[j].stats;

			s->tx_packets		+= sq_stats->packets;
			s->tx_bytes		+= sq_stats->bytes;
			s->tso_packets		+= sq_stats->tso_packets;
			s->tso_bytes		+= sq_stats->tso_bytes;
			s->tx_queue_stopped	+= sq_stats->stopped;
			s->tx_queue_wake	+= sq_stats->wake;
			s->tx_queue_dropped	+= sq_stats->dropped;
			tx_offload_none		+= sq_stats->csum_offload_none;
		}
	}

	/* HW counters */
	memset(in, 0, sizeof(in));

	MLX5_SET(query_vport_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	MLX5_SET(query_vport_counter_in, in, op_mod, 0);
	MLX5_SET(query_vport_counter_in, in, other_vport, 0);

	memset(out, 0, outlen);

	if (mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen))
		goto free_out;

#define MLX5_GET_CTR(p, x) \
	MLX5_GET64(query_vport_counter_out, p, x)

	s->rx_error_packets     =
		MLX5_GET_CTR(out, received_errors.packets);
	s->rx_error_bytes       =
		MLX5_GET_CTR(out, received_errors.octets);
	s->tx_error_packets     =
		MLX5_GET_CTR(out, transmit_errors.packets);
	s->tx_error_bytes       =
		MLX5_GET_CTR(out, transmit_errors.octets);

	s->rx_unicast_packets   =
		MLX5_GET_CTR(out, received_eth_unicast.packets);
	s->rx_unicast_bytes     =
		MLX5_GET_CTR(out, received_eth_unicast.octets);
	s->tx_unicast_packets   =
		MLX5_GET_CTR(out, transmitted_eth_unicast.packets);
	s->tx_unicast_bytes     =
		MLX5_GET_CTR(out, transmitted_eth_unicast.octets);

	s->rx_multicast_packets =
		MLX5_GET_CTR(out, received_eth_multicast.packets);
	s->rx_multicast_bytes   =
		MLX5_GET_CTR(out, received_eth_multicast.octets);
	s->tx_multicast_packets =
		MLX5_GET_CTR(out, transmitted_eth_multicast.packets);
	s->tx_multicast_bytes   =
		MLX5_GET_CTR(out, transmitted_eth_multicast.octets);

	s->rx_broadcast_packets =
		MLX5_GET_CTR(out, received_eth_broadcast.packets);
	s->rx_broadcast_bytes   =
		MLX5_GET_CTR(out, received_eth_broadcast.octets);
	s->tx_broadcast_packets =
		MLX5_GET_CTR(out, transmitted_eth_broadcast.packets);
	s->tx_broadcast_bytes   =
		MLX5_GET_CTR(out, transmitted_eth_broadcast.octets);

	/* Update calculated offload counters */
	s->tx_csum_offload = s->tx_packets - tx_offload_none;
	s->rx_csum_good    = s->rx_packets - s->rx_csum_none -
			       s->rx_csum_sw;

	mlx5e_update_pport_counters(priv);
free_out:
	kvfree(out);
}

static void mlx5e_update_stats_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlx5e_priv *priv = container_of(dwork, struct mlx5e_priv,
					       update_stats_work);
	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		mlx5e_update_stats(priv);
		schedule_delayed_work(dwork,
				      msecs_to_jiffies(
					      MLX5E_UPDATE_STATS_INTERVAL));
	}
	mutex_unlock(&priv->state_lock);
}

static void __mlx5e_async_event(struct mlx5e_priv *priv,
				enum mlx5_dev_event event)
{
	switch (event) {
	case MLX5_DEV_EVENT_PORT_UP:
	case MLX5_DEV_EVENT_PORT_DOWN:
		schedule_work(&priv->update_carrier_work);
		break;

	default:
		break;
	}
}

static void mlx5e_async_event(struct mlx5_core_dev *mdev, void *vpriv,
			      enum mlx5_dev_event event, unsigned long param)
{
	struct mlx5e_priv *priv = vpriv;

	spin_lock(&priv->async_events_spinlock);
	if (test_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLE, &priv->state))
		__mlx5e_async_event(priv, event);
	spin_unlock(&priv->async_events_spinlock);
}

static void mlx5e_enable_async_events(struct mlx5e_priv *priv)
{
	set_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLE, &priv->state);
}

static void mlx5e_disable_async_events(struct mlx5e_priv *priv)
{
	spin_lock_irq(&priv->async_events_spinlock);
	clear_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLE, &priv->state);
	spin_unlock_irq(&priv->async_events_spinlock);
}

#define MLX5E_HW2SW_MTU(hwmtu) (hwmtu - (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN))
#define MLX5E_SW2HW_MTU(swmtu) (swmtu + (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN))

static int mlx5e_create_rq(struct mlx5e_channel *c,
			   struct mlx5e_rq_param *param,
			   struct mlx5e_rq *rq)
{
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqc = param->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int wq_sz;
	int err;
	int i;

	param->wq.db_numa_node = cpu_to_node(c->cpu);

	err = mlx5_wq_ll_create(mdev, &param->wq, rqc_wq, &rq->wq,
				&rq->wq_ctrl);
	if (err)
		return err;

	rq->wq.db = &rq->wq.db[MLX5_RCV_DBR];

	wq_sz = mlx5_wq_ll_get_size(&rq->wq);
	rq->skb = kzalloc_node(wq_sz * sizeof(*rq->skb), GFP_KERNEL,
			       cpu_to_node(c->cpu));
	if (!rq->skb) {
		err = -ENOMEM;
		goto err_rq_wq_destroy;
	}

	rq->wqe_sz = (priv->params.lro_en) ? priv->params.lro_wqe_sz :
					     MLX5E_SW2HW_MTU(priv->netdev->mtu);
	rq->wqe_sz = SKB_DATA_ALIGN(rq->wqe_sz + MLX5E_NET_IP_ALIGN);

	for (i = 0; i < wq_sz; i++) {
		struct mlx5e_rx_wqe *wqe = mlx5_wq_ll_get_wqe(&rq->wq, i);
		u32 byte_count = rq->wqe_sz - MLX5E_NET_IP_ALIGN;

		wqe->data.lkey       = c->mkey_be;
		wqe->data.byte_count =
			cpu_to_be32(byte_count | MLX5_HW_START_PADDING);
	}

	rq->pdev    = c->pdev;
	rq->netdev  = c->netdev;
	rq->tstamp  = &priv->tstamp;
	rq->channel = c;
	rq->ix      = c->ix;
	rq->priv    = c->priv;

	return 0;

err_rq_wq_destroy:
	mlx5_wq_destroy(&rq->wq_ctrl);

	return err;
}

static void mlx5e_destroy_rq(struct mlx5e_rq *rq)
{
	kfree(rq->skb);
	mlx5_wq_destroy(&rq->wq_ctrl);
}

static int mlx5e_enable_rq(struct mlx5e_rq *rq, struct mlx5e_rq_param *param)
{
	struct mlx5e_priv *priv = rq->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *rqc;
	void *wq;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_rq_in) +
		sizeof(u64) * rq->wq_ctrl.buf.npages;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	wq  = MLX5_ADDR_OF(rqc, rqc, wq);

	memcpy(rqc, param->rqc, sizeof(param->rqc));

	MLX5_SET(rqc,  rqc, cqn,		rq->cq.mcq.cqn);
	MLX5_SET(rqc,  rqc, state,		MLX5_RQC_STATE_RST);
	MLX5_SET(rqc,  rqc, flush_in_error_en,	1);
	MLX5_SET(wq,   wq,  log_wq_pg_sz,	rq->wq_ctrl.buf.page_shift -
						MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq,  dbr_addr,		rq->wq_ctrl.db.dma);

	mlx5_fill_page_array(&rq->wq_ctrl.buf,
			     (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_rq(mdev, in, inlen, &rq->rqn);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq(struct mlx5e_rq *rq, int curr_state, int next_state)
{
	struct mlx5e_channel *c = rq->channel;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, curr_state);
	MLX5_SET(rqc, rqc, state, next_state);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in, inlen);

	kvfree(in);

	return err;
}

static void mlx5e_disable_rq(struct mlx5e_rq *rq)
{
	mlx5_core_destroy_rq(rq->priv->mdev, rq->rqn);
}

static int mlx5e_wait_for_min_rx_wqes(struct mlx5e_rq *rq)
{
	unsigned long exp_time = jiffies + msecs_to_jiffies(20000);
	struct mlx5e_channel *c = rq->channel;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_wq_ll *wq = &rq->wq;

	while (time_before(jiffies, exp_time)) {
		if (wq->cur_sz >= priv->params.min_rx_wqes)
			return 0;

		msleep(20);
	}

	return -ETIMEDOUT;
}

static int mlx5e_open_rq(struct mlx5e_channel *c,
			 struct mlx5e_rq_param *param,
			 struct mlx5e_rq *rq)
{
	int err;

	err = mlx5e_create_rq(c, param, rq);
	if (err)
		return err;

	err = mlx5e_enable_rq(rq, param);
	if (err)
		goto err_destroy_rq;

	err = mlx5e_modify_rq(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_disable_rq;

	set_bit(MLX5E_RQ_STATE_POST_WQES_ENABLE, &rq->state);
	mlx5e_send_nop(&c->sq[0], true); /* trigger mlx5e_post_rx_wqes() */

	return 0;

err_disable_rq:
	mlx5e_disable_rq(rq);
err_destroy_rq:
	mlx5e_destroy_rq(rq);

	return err;
}

static void mlx5e_close_rq(struct mlx5e_rq *rq)
{
	clear_bit(MLX5E_RQ_STATE_POST_WQES_ENABLE, &rq->state);
	napi_synchronize(&rq->channel->napi); /* prevent mlx5e_post_rx_wqes */

	mlx5e_modify_rq(rq, MLX5_RQC_STATE_RDY, MLX5_RQC_STATE_ERR);
	while (!mlx5_wq_ll_is_empty(&rq->wq))
		msleep(20);

	/* avoid destroying rq before mlx5e_poll_rx_cq() is done with it */
	napi_synchronize(&rq->channel->napi);

	mlx5e_disable_rq(rq);
	mlx5e_destroy_rq(rq);
}

static void mlx5e_free_sq_db(struct mlx5e_sq *sq)
{
	kfree(sq->wqe_info);
	kfree(sq->dma_fifo);
	kfree(sq->skb);
}

static int mlx5e_alloc_sq_db(struct mlx5e_sq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	int df_sz = wq_sz * MLX5_SEND_WQEBB_NUM_DS;

	sq->skb = kzalloc_node(wq_sz * sizeof(*sq->skb), GFP_KERNEL, numa);
	sq->dma_fifo = kzalloc_node(df_sz * sizeof(*sq->dma_fifo), GFP_KERNEL,
				    numa);
	sq->wqe_info = kzalloc_node(wq_sz * sizeof(*sq->wqe_info), GFP_KERNEL,
				    numa);

	if (!sq->skb || !sq->dma_fifo || !sq->wqe_info) {
		mlx5e_free_sq_db(sq);
		return -ENOMEM;
	}

	sq->dma_fifo_mask = df_sz - 1;

	return 0;
}

static int mlx5e_create_sq(struct mlx5e_channel *c,
			   int tc,
			   struct mlx5e_sq_param *param,
			   struct mlx5e_sq *sq)
{
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *sqc = param->sqc;
	void *sqc_wq = MLX5_ADDR_OF(sqc, sqc, wq);
	int txq_ix;
	int err;

	err = mlx5_alloc_map_uar(mdev, &sq->uar);
	if (err)
		return err;

	param->wq.db_numa_node = cpu_to_node(c->cpu);

	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, &sq->wq,
				 &sq->wq_ctrl);
	if (err)
		goto err_unmap_free_uar;

	sq->wq.db       = &sq->wq.db[MLX5_SND_DBR];
	sq->uar_map     = sq->uar.map;
	sq->uar_bf_map  = sq->uar.bf_map;
	sq->bf_buf_size = (1 << MLX5_CAP_GEN(mdev, log_bf_reg_size)) / 2;
	sq->max_inline  = param->max_inline;

	err = mlx5e_alloc_sq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	txq_ix = c->ix + tc * priv->params.num_channels;
	sq->txq = netdev_get_tx_queue(priv->netdev, txq_ix);

	sq->pdev      = c->pdev;
	sq->tstamp    = &priv->tstamp;
	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->tc        = tc;
	sq->edge      = (sq->wq.sz_m1 + 1) - MLX5_SEND_WQE_MAX_WQEBBS;
	sq->bf_budget = MLX5E_SQ_BF_BUDGET;
	priv->txq_to_sq_map[txq_ix] = sq;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

err_unmap_free_uar:
	mlx5_unmap_free_uar(mdev, &sq->uar);

	return err;
}

static void mlx5e_destroy_sq(struct mlx5e_sq *sq)
{
	struct mlx5e_channel *c = sq->channel;
	struct mlx5e_priv *priv = c->priv;

	mlx5e_free_sq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
	mlx5_unmap_free_uar(priv->mdev, &sq->uar);
}

static int mlx5e_enable_sq(struct mlx5e_sq *sq, struct mlx5e_sq_param *param)
{
	struct mlx5e_channel *c = sq->channel;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *sqc;
	void *wq;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_sq_in) +
		sizeof(u64) * sq->wq_ctrl.buf.npages;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq = MLX5_ADDR_OF(sqc, sqc, wq);

	memcpy(sqc, param->sqc, sizeof(param->sqc));

	MLX5_SET(sqc,  sqc, tis_num_0,		priv->tisn[sq->tc]);
	MLX5_SET(sqc,  sqc, cqn,		c->sq[sq->tc].cq.mcq.cqn);
	MLX5_SET(sqc,  sqc, state,		MLX5_SQC_STATE_RST);
	MLX5_SET(sqc,  sqc, tis_lst_sz,		1);
	MLX5_SET(sqc,  sqc, flush_in_error_en,	1);

	MLX5_SET(wq,   wq, wq_type,       MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq,   wq, uar_page,      sq->uar.index);
	MLX5_SET(wq,   wq, log_wq_pg_sz,  sq->wq_ctrl.buf.page_shift -
					  MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr,      sq->wq_ctrl.db.dma);

	mlx5_fill_page_array(&sq->wq_ctrl.buf,
			     (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, &sq->sqn);

	kvfree(in);

	return err;
}

static int mlx5e_modify_sq(struct mlx5e_sq *sq, int curr_state, int next_state)
{
	struct mlx5e_channel *c = sq->channel;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *sqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);

	MLX5_SET(modify_sq_in, in, sq_state, curr_state);
	MLX5_SET(sqc, sqc, state, next_state);

	err = mlx5_core_modify_sq(mdev, sq->sqn, in, inlen);

	kvfree(in);

	return err;
}

static void mlx5e_disable_sq(struct mlx5e_sq *sq)
{
	struct mlx5e_channel *c = sq->channel;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	mlx5_core_destroy_sq(mdev, sq->sqn);
}

static int mlx5e_open_sq(struct mlx5e_channel *c,
			 int tc,
			 struct mlx5e_sq_param *param,
			 struct mlx5e_sq *sq)
{
	int err;

	err = mlx5e_create_sq(c, tc, param, sq);
	if (err)
		return err;

	err = mlx5e_enable_sq(sq, param);
	if (err)
		goto err_destroy_sq;

	err = mlx5e_modify_sq(sq, MLX5_SQC_STATE_RST, MLX5_SQC_STATE_RDY);
	if (err)
		goto err_disable_sq;

	set_bit(MLX5E_SQ_STATE_WAKE_TXQ_ENABLE, &sq->state);
	netdev_tx_reset_queue(sq->txq);
	netif_tx_start_queue(sq->txq);

	return 0;

err_disable_sq:
	mlx5e_disable_sq(sq);
err_destroy_sq:
	mlx5e_destroy_sq(sq);

	return err;
}

static inline void netif_tx_disable_queue(struct netdev_queue *txq)
{
	__netif_tx_lock_bh(txq);
	netif_tx_stop_queue(txq);
	__netif_tx_unlock_bh(txq);
}

static void mlx5e_close_sq(struct mlx5e_sq *sq)
{
	clear_bit(MLX5E_SQ_STATE_WAKE_TXQ_ENABLE, &sq->state);
	napi_synchronize(&sq->channel->napi); /* prevent netif_tx_wake_queue */
	netif_tx_disable_queue(sq->txq);

	/* ensure hw is notified of all pending wqes */
	if (mlx5e_sq_has_room_for(sq, 1))
		mlx5e_send_nop(sq, true);

	mlx5e_modify_sq(sq, MLX5_SQC_STATE_RDY, MLX5_SQC_STATE_ERR);
	while (sq->cc != sq->pc) /* wait till sq is empty */
		msleep(20);

	/* avoid destroying sq before mlx5e_poll_tx_cq() is done with it */
	napi_synchronize(&sq->channel->napi);

	mlx5e_disable_sq(sq);
	mlx5e_destroy_sq(sq);
}

static int mlx5e_create_cq(struct mlx5e_channel *c,
			   struct mlx5e_cq_param *param,
			   struct mlx5e_cq *cq)
{
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;
	int eqn_not_used;
	unsigned int irqn;
	int err;
	u32 i;

	param->wq.buf_numa_node = cpu_to_node(c->cpu);
	param->wq.db_numa_node  = cpu_to_node(c->cpu);
	param->eq_ix   = c->ix;

	err = mlx5_cqwq_create(mdev, &param->wq, param->cqc, &cq->wq,
			       &cq->wq_ctrl);
	if (err)
		return err;

	mlx5_vector2eqn(mdev, param->eq_ix, &eqn_not_used, &irqn);

	cq->napi        = &c->napi;

	mcq->cqe_sz     = 64;
	mcq->set_ci_db  = cq->wq_ctrl.db.db;
	mcq->arm_db     = cq->wq_ctrl.db.db + 1;
	*mcq->set_ci_db = 0;
	*mcq->arm_db    = 0;
	mcq->vector     = param->eq_ix;
	mcq->comp       = mlx5e_completion_event;
	mcq->event      = mlx5e_cq_error_event;
	mcq->irqn       = irqn;
	mcq->uar        = &priv->cq_uar;

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(&cq->wq, i);

		cqe->op_own = 0xf1;
	}

	cq->channel = c;
	cq->priv = priv;

	return 0;
}

static void mlx5e_destroy_cq(struct mlx5e_cq *cq)
{
	mlx5_wq_destroy(&cq->wq_ctrl);
}

static int mlx5e_enable_cq(struct mlx5e_cq *cq, struct mlx5e_cq_param *param)
{
	struct mlx5e_priv *priv = cq->priv;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;

	void *in;
	void *cqc;
	int inlen;
	unsigned int irqn_not_used;
	int eqn;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.buf.npages;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);

	memcpy(cqc, param->cqc, sizeof(param->cqc));

	mlx5_fill_page_array(&cq->wq_ctrl.buf,
			     (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas));

	mlx5_vector2eqn(mdev, param->eq_ix, &eqn, &irqn_not_used);

	MLX5_SET(cqc,   cqc, c_eqn,         eqn);
	MLX5_SET(cqc,   cqc, uar_page,      mcq->uar->index);
	MLX5_SET(cqc,   cqc, log_page_size, cq->wq_ctrl.buf.page_shift -
					    MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr,      cq->wq_ctrl.db.dma);

	err = mlx5_core_create_cq(mdev, mcq, in, inlen);

	kvfree(in);

	if (err)
		return err;

	mlx5e_cq_arm(cq);

	return 0;
}

static void mlx5e_disable_cq(struct mlx5e_cq *cq)
{
	struct mlx5e_priv *priv = cq->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	mlx5_core_destroy_cq(mdev, &cq->mcq);
}

static int mlx5e_open_cq(struct mlx5e_channel *c,
			 struct mlx5e_cq_param *param,
			 struct mlx5e_cq *cq,
			 u16 moderation_usecs,
			 u16 moderation_frames)
{
	int err;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	err = mlx5e_create_cq(c, param, cq);
	if (err)
		return err;

	err = mlx5e_enable_cq(cq, param);
	if (err)
		goto err_destroy_cq;

	err = mlx5_core_modify_cq_moderation(mdev, &cq->mcq,
					     moderation_usecs,
					     moderation_frames);
	if (err)
		goto err_destroy_cq;

	return 0;

err_destroy_cq:
	mlx5e_destroy_cq(cq);

	return err;
}

static void mlx5e_close_cq(struct mlx5e_cq *cq)
{
	mlx5e_disable_cq(cq);
	mlx5e_destroy_cq(cq);
}

static int mlx5e_get_cpu(struct mlx5e_priv *priv, int ix)
{
	return cpumask_first(priv->mdev->priv.irq_info[ix].mask);
}

static int mlx5e_open_tx_cqs(struct mlx5e_channel *c,
			     struct mlx5e_channel_param *cparam)
{
	struct mlx5e_priv *priv = c->priv;
	int err;
	int tc;

	for (tc = 0; tc < c->num_tc; tc++) {
		err = mlx5e_open_cq(c, &cparam->tx_cq, &c->sq[tc].cq,
				    priv->params.tx_cq_moderation_usec,
				    priv->params.tx_cq_moderation_pkts);
		if (err)
			goto err_close_tx_cqs;
	}

	return 0;

err_close_tx_cqs:
	for (tc--; tc >= 0; tc--)
		mlx5e_close_cq(&c->sq[tc].cq);

	return err;
}

static void mlx5e_close_tx_cqs(struct mlx5e_channel *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_cq(&c->sq[tc].cq);
}

static int mlx5e_open_sqs(struct mlx5e_channel *c,
			  struct mlx5e_channel_param *cparam)
{
	int err;
	int tc;

	for (tc = 0; tc < c->num_tc; tc++) {
		err = mlx5e_open_sq(c, tc, &cparam->sq, &c->sq[tc]);
		if (err)
			goto err_close_sqs;
	}

	return 0;

err_close_sqs:
	for (tc--; tc >= 0; tc--)
		mlx5e_close_sq(&c->sq[tc]);

	return err;
}

static void mlx5e_close_sqs(struct mlx5e_channel *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_sq(&c->sq[tc]);
}

static void mlx5e_build_channeltc_to_txq_map(struct mlx5e_priv *priv, int ix)
{
	int i;

	for (i = 0; i < MLX5E_MAX_NUM_TC; i++)
		priv->channeltc_to_txq_map[ix][i] =
			ix + i * priv->params.num_channels;
}

static int mlx5e_open_channel(struct mlx5e_priv *priv, int ix,
			      struct mlx5e_channel_param *cparam,
			      struct mlx5e_channel **cp)
{
	struct net_device *netdev = priv->netdev;
	int cpu = mlx5e_get_cpu(priv, ix);
	struct mlx5e_channel *c;
	int err;

	c = kzalloc_node(sizeof(*c), GFP_KERNEL, cpu_to_node(cpu));
	if (!c)
		return -ENOMEM;

	c->priv     = priv;
	c->ix       = ix;
	c->cpu      = cpu;
	c->pdev     = &priv->mdev->pdev->dev;
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mr.key);
	c->num_tc   = priv->params.num_tc;

	mlx5e_build_channeltc_to_txq_map(priv, ix);

	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll, 64);

	err = mlx5e_open_tx_cqs(c, cparam);
	if (err)
		goto err_napi_del;

	err = mlx5e_open_cq(c, &cparam->rx_cq, &c->rq.cq,
			    priv->params.rx_cq_moderation_usec,
			    priv->params.rx_cq_moderation_pkts);
	if (err)
		goto err_close_tx_cqs;

	napi_enable(&c->napi);

	err = mlx5e_open_sqs(c, cparam);
	if (err)
		goto err_disable_napi;

	err = mlx5e_open_rq(c, &cparam->rq, &c->rq);
	if (err)
		goto err_close_sqs;

	netif_set_xps_queue(netdev, get_cpu_mask(c->cpu), ix);
	*cp = c;

	return 0;

err_close_sqs:
	mlx5e_close_sqs(c);

err_disable_napi:
	napi_disable(&c->napi);
	mlx5e_close_cq(&c->rq.cq);

err_close_tx_cqs:
	mlx5e_close_tx_cqs(c);

err_napi_del:
	netif_napi_del(&c->napi);
	napi_hash_del(&c->napi);
	kfree(c);

	return err;
}

static void mlx5e_close_channel(struct mlx5e_channel *c)
{
	mlx5e_close_rq(&c->rq);
	mlx5e_close_sqs(c);
	napi_disable(&c->napi);
	mlx5e_close_cq(&c->rq.cq);
	mlx5e_close_tx_cqs(c);
	netif_napi_del(&c->napi);

	napi_hash_del(&c->napi);
	synchronize_rcu();

	kfree(c);
}

static void mlx5e_build_rq_param(struct mlx5e_priv *priv,
				 struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	MLX5_SET(wq, wq, wq_type,          MLX5_WQ_TYPE_LINKED_LIST);
	MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);
	MLX5_SET(wq, wq, log_wq_stride,    ilog2(sizeof(struct mlx5e_rx_wqe)));
	MLX5_SET(wq, wq, log_wq_sz,        priv->params.log_rq_size);
	MLX5_SET(wq, wq, pd,               priv->pdn);

	param->wq.buf_numa_node = dev_to_node(&priv->mdev->pdev->dev);
	param->wq.linear = 1;
}

static void mlx5e_build_sq_param(struct mlx5e_priv *priv,
				 struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	MLX5_SET(wq, wq, log_wq_sz,     priv->params.log_sq_size);
	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd,            priv->pdn);

	param->wq.buf_numa_node = dev_to_node(&priv->mdev->pdev->dev);
	param->max_inline = priv->params.tx_max_inline;
}

static void mlx5e_build_common_cq_param(struct mlx5e_priv *priv,
					struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, uar_page, priv->cq_uar.index);
}

static void mlx5e_build_rx_cq_param(struct mlx5e_priv *priv,
				    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size,  priv->params.log_rq_size);

	mlx5e_build_common_cq_param(priv, param);
}

static void mlx5e_build_tx_cq_param(struct mlx5e_priv *priv,
				    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size,  priv->params.log_sq_size);

	mlx5e_build_common_cq_param(priv, param);
}

static void mlx5e_build_channel_param(struct mlx5e_priv *priv,
				      struct mlx5e_channel_param *cparam)
{
	memset(cparam, 0, sizeof(*cparam));

	mlx5e_build_rq_param(priv, &cparam->rq);
	mlx5e_build_sq_param(priv, &cparam->sq);
	mlx5e_build_rx_cq_param(priv, &cparam->rx_cq);
	mlx5e_build_tx_cq_param(priv, &cparam->tx_cq);
}

static int mlx5e_open_channels(struct mlx5e_priv *priv)
{
	struct mlx5e_channel_param cparam;
	int nch = priv->params.num_channels;
	int err = -ENOMEM;
	int i;
	int j;

	priv->channel = kcalloc(nch, sizeof(struct mlx5e_channel *),
				GFP_KERNEL);

	priv->txq_to_sq_map = kcalloc(nch * priv->params.num_tc,
				      sizeof(struct mlx5e_sq *), GFP_KERNEL);

	if (!priv->channel || !priv->txq_to_sq_map)
		goto err_free_txq_to_sq_map;

	mlx5e_build_channel_param(priv, &cparam);
	for (i = 0; i < nch; i++) {
		err = mlx5e_open_channel(priv, i, &cparam, &priv->channel[i]);
		if (err)
			goto err_close_channels;
	}

	for (j = 0; j < nch; j++) {
		err = mlx5e_wait_for_min_rx_wqes(&priv->channel[j]->rq);
		if (err)
			goto err_close_channels;
	}

	return 0;

err_close_channels:
	for (i--; i >= 0; i--)
		mlx5e_close_channel(priv->channel[i]);

err_free_txq_to_sq_map:
	kfree(priv->txq_to_sq_map);
	kfree(priv->channel);

	return err;
}

static void mlx5e_close_channels(struct mlx5e_priv *priv)
{
	int i;

	for (i = 0; i < priv->params.num_channels; i++)
		mlx5e_close_channel(priv->channel[i]);

	kfree(priv->txq_to_sq_map);
	kfree(priv->channel);
}

static int mlx5e_rx_hash_fn(int hfunc)
{
	return (hfunc == ETH_RSS_HASH_TOP) ?
	       MLX5_RX_HASH_FN_TOEPLITZ :
	       MLX5_RX_HASH_FN_INVERTED_XOR8;
}

static int mlx5e_bits_invert(unsigned long a, int size)
{
	int inv = 0;
	int i;

	for (i = 0; i < size; i++)
		inv |= (test_bit(size - i - 1, &a) ? 1 : 0) << i;

	return inv;
}

static void mlx5e_fill_indir_rqt_rqns(struct mlx5e_priv *priv, void *rqtc)
{
	int i;

	for (i = 0; i < MLX5E_INDIR_RQT_SIZE; i++) {
		int ix = i;

		if (priv->params.rss_hfunc == ETH_RSS_HASH_XOR)
			ix = mlx5e_bits_invert(i, MLX5E_LOG_INDIR_RQT_SIZE);

		ix = priv->params.indirection_rqt[ix];
		MLX5_SET(rqtc, rqtc, rq_num[i],
			 test_bit(MLX5E_STATE_OPENED, &priv->state) ?
			 priv->channel[ix]->rq.rqn :
			 priv->drop_rq.rqn);
	}
}

static void mlx5e_fill_rqt_rqns(struct mlx5e_priv *priv, void *rqtc,
				enum mlx5e_rqt_ix rqt_ix)
{

	switch (rqt_ix) {
	case MLX5E_INDIRECTION_RQT:
		mlx5e_fill_indir_rqt_rqns(priv, rqtc);

		break;

	default: /* MLX5E_SINGLE_RQ_RQT */
		MLX5_SET(rqtc, rqtc, rq_num[0],
			 test_bit(MLX5E_STATE_OPENED, &priv->state) ?
			 priv->channel[0]->rq.rqn :
			 priv->drop_rq.rqn);

		break;
	}
}

static int mlx5e_create_rqt(struct mlx5e_priv *priv, enum mlx5e_rqt_ix rqt_ix)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 *in;
	void *rqtc;
	int inlen;
	int sz;
	int err;

	sz = (rqt_ix == MLX5E_SINGLE_RQ_RQT) ? 1 : MLX5E_INDIR_RQT_SIZE;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + sizeof(u32) * sz;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	MLX5_SET(rqtc, rqtc, rqt_max_size, sz);

	mlx5e_fill_rqt_rqns(priv, rqtc, rqt_ix);

	err = mlx5_core_create_rqt(mdev, in, inlen, &priv->rqtn[rqt_ix]);

	kvfree(in);

	return err;
}

int mlx5e_redirect_rqt(struct mlx5e_priv *priv, enum mlx5e_rqt_ix rqt_ix)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 *in;
	void *rqtc;
	int inlen;
	int sz;
	int err;

	sz = (rqt_ix == MLX5E_SINGLE_RQ_RQT) ? 1 : MLX5E_INDIR_RQT_SIZE;

	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + sizeof(u32) * sz;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);

	mlx5e_fill_rqt_rqns(priv, rqtc, rqt_ix);

	MLX5_SET(modify_rqt_in, in, bitmask.rqn_list, 1);

	err = mlx5_core_modify_rqt(mdev, priv->rqtn[rqt_ix], in, inlen);

	kvfree(in);

	return err;
}

static void mlx5e_destroy_rqt(struct mlx5e_priv *priv, enum mlx5e_rqt_ix rqt_ix)
{
	mlx5_core_destroy_rqt(priv->mdev, priv->rqtn[rqt_ix]);
}

static void mlx5e_redirect_rqts(struct mlx5e_priv *priv)
{
	mlx5e_redirect_rqt(priv, MLX5E_INDIRECTION_RQT);
	mlx5e_redirect_rqt(priv, MLX5E_SINGLE_RQ_RQT);
}

static void mlx5e_build_tir_ctx_lro(void *tirc, struct mlx5e_priv *priv)
{
	if (!priv->params.lro_en)
		return;

#define ROUGH_MAX_L2_L3_HDR_SZ 256

	MLX5_SET(tirc, tirc, lro_enable_mask,
		 MLX5_TIRC_LRO_ENABLE_MASK_IPV4_LRO |
		 MLX5_TIRC_LRO_ENABLE_MASK_IPV6_LRO);
	MLX5_SET(tirc, tirc, lro_max_ip_payload_size,
		 (priv->params.lro_wqe_sz -
		  ROUGH_MAX_L2_L3_HDR_SZ) >> 8);
	MLX5_SET(tirc, tirc, lro_timeout_period_usecs,
		 MLX5_CAP_ETH(priv->mdev,
			      lro_timer_supported_periods[2]));
}

void mlx5e_build_tir_ctx_hash(void *tirc, struct mlx5e_priv *priv)
{
	MLX5_SET(tirc, tirc, rx_hash_fn,
		 mlx5e_rx_hash_fn(priv->params.rss_hfunc));
	if (priv->params.rss_hfunc == ETH_RSS_HASH_TOP) {
		void *rss_key = MLX5_ADDR_OF(tirc, tirc,
					     rx_hash_toeplitz_key);
		size_t len = MLX5_FLD_SZ_BYTES(tirc,
					       rx_hash_toeplitz_key);

		MLX5_SET(tirc, tirc, rx_hash_symmetric, 1);
		memcpy(rss_key, priv->params.toeplitz_hash_key, len);
	}
}

static int mlx5e_modify_tirs_lro(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *tirc;
	int inlen;
	int err;
	int tt;

	inlen = MLX5_ST_SZ_BYTES(modify_tir_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_tir_in, in, bitmask.lro, 1);
	tirc = MLX5_ADDR_OF(modify_tir_in, in, ctx);

	mlx5e_build_tir_ctx_lro(tirc, priv);

	for (tt = 0; tt < MLX5E_NUM_TT; tt++) {
		err = mlx5_core_modify_tir(mdev, priv->tirn[tt], in, inlen);
		if (err)
			break;
	}

	kvfree(in);

	return err;
}

static int mlx5e_refresh_tir_self_loopback_enable(struct mlx5_core_dev *mdev,
						  u32 tirn)
{
	void *in;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_tir_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_tir_in, in, bitmask.self_lb_en, 1);

	err = mlx5_core_modify_tir(mdev, tirn, in, inlen);

	kvfree(in);

	return err;
}

static int mlx5e_refresh_tirs_self_loopback_enable(struct mlx5e_priv *priv)
{
	int err;
	int i;

	for (i = 0; i < MLX5E_NUM_TT; i++) {
		err = mlx5e_refresh_tir_self_loopback_enable(priv->mdev,
							     priv->tirn[i]);
		if (err)
			return err;
	}

	return 0;
}

static int mlx5e_set_dev_port_mtu(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int hw_mtu;
	int err;

	err = mlx5_set_port_mtu(mdev, MLX5E_SW2HW_MTU(netdev->mtu), 1);
	if (err)
		return err;

	mlx5_query_port_oper_mtu(mdev, &hw_mtu, 1);

	if (MLX5E_HW2SW_MTU(hw_mtu) != netdev->mtu)
		netdev_warn(netdev, "%s: Port MTU %d is different than netdev mtu %d\n",
			    __func__, MLX5E_HW2SW_MTU(hw_mtu), netdev->mtu);

	netdev->mtu = MLX5E_HW2SW_MTU(hw_mtu);
	return 0;
}

int mlx5e_open_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int num_txqs;
	int err;

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	num_txqs = priv->params.num_channels * priv->params.num_tc;
	netif_set_real_num_tx_queues(netdev, num_txqs);
	netif_set_real_num_rx_queues(netdev, priv->params.num_channels);

	err = mlx5e_set_dev_port_mtu(netdev);
	if (err)
		goto err_clear_state_opened_flag;

	err = mlx5e_open_channels(priv);
	if (err) {
		netdev_err(netdev, "%s: mlx5e_open_channels failed, %d\n",
			   __func__, err);
		goto err_clear_state_opened_flag;
	}

	err = mlx5e_refresh_tirs_self_loopback_enable(priv);
	if (err) {
		netdev_err(netdev, "%s: mlx5e_refresh_tirs_self_loopback_enable failed, %d\n",
			   __func__, err);
		goto err_close_channels;
	}

	mlx5e_update_carrier(priv);
	mlx5e_redirect_rqts(priv);
	mlx5e_timestamp_init(priv);

	schedule_delayed_work(&priv->update_stats_work, 0);

	return 0;

err_close_channels:
	mlx5e_close_channels(priv);
err_clear_state_opened_flag:
	clear_bit(MLX5E_STATE_OPENED, &priv->state);
	return err;
}

static int mlx5e_open(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_open_locked(netdev);
	mutex_unlock(&priv->state_lock);

	return err;
}

int mlx5e_close_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	/* May already be CLOSED in case a previous configuration operation
	 * (e.g RX/TX queue size change) that involves close&open failed.
	 */
	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	clear_bit(MLX5E_STATE_OPENED, &priv->state);

	mlx5e_timestamp_cleanup(priv);
	mlx5e_redirect_rqts(priv);
	netif_carrier_off(priv->netdev);
	mlx5e_close_channels(priv);

	return 0;
}

static int mlx5e_close(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_close_locked(netdev);
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_create_drop_rq(struct mlx5e_priv *priv,
				struct mlx5e_rq *rq,
				struct mlx5e_rq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqc = param->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int err;

	param->wq.db_numa_node = param->wq.buf_numa_node;

	err = mlx5_wq_ll_create(mdev, &param->wq, rqc_wq, &rq->wq,
				&rq->wq_ctrl);
	if (err)
		return err;

	rq->priv = priv;

	return 0;
}

static int mlx5e_create_drop_cq(struct mlx5e_priv *priv,
				struct mlx5e_cq *cq,
				struct mlx5e_cq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;
	int eqn_not_used;
	unsigned int irqn;
	int err;

	err = mlx5_cqwq_create(mdev, &param->wq, param->cqc, &cq->wq,
			       &cq->wq_ctrl);
	if (err)
		return err;

	mlx5_vector2eqn(mdev, param->eq_ix, &eqn_not_used, &irqn);

	mcq->cqe_sz     = 64;
	mcq->set_ci_db  = cq->wq_ctrl.db.db;
	mcq->arm_db     = cq->wq_ctrl.db.db + 1;
	*mcq->set_ci_db = 0;
	*mcq->arm_db    = 0;
	mcq->vector     = param->eq_ix;
	mcq->comp       = mlx5e_completion_event;
	mcq->event      = mlx5e_cq_error_event;
	mcq->irqn       = irqn;
	mcq->uar        = &priv->cq_uar;

	cq->priv = priv;

	return 0;
}

static int mlx5e_open_drop_rq(struct mlx5e_priv *priv)
{
	struct mlx5e_cq_param cq_param;
	struct mlx5e_rq_param rq_param;
	struct mlx5e_rq *rq = &priv->drop_rq;
	struct mlx5e_cq *cq = &priv->drop_rq.cq;
	int err;

	memset(&cq_param, 0, sizeof(cq_param));
	memset(&rq_param, 0, sizeof(rq_param));
	mlx5e_build_rx_cq_param(priv, &cq_param);
	mlx5e_build_rq_param(priv, &rq_param);

	err = mlx5e_create_drop_cq(priv, cq, &cq_param);
	if (err)
		return err;

	err = mlx5e_enable_cq(cq, &cq_param);
	if (err)
		goto err_destroy_cq;

	err = mlx5e_create_drop_rq(priv, rq, &rq_param);
	if (err)
		goto err_disable_cq;

	err = mlx5e_enable_rq(rq, &rq_param);
	if (err)
		goto err_destroy_rq;

	return 0;

err_destroy_rq:
	mlx5e_destroy_rq(&priv->drop_rq);

err_disable_cq:
	mlx5e_disable_cq(&priv->drop_rq.cq);

err_destroy_cq:
	mlx5e_destroy_cq(&priv->drop_rq.cq);

	return err;
}

static void mlx5e_close_drop_rq(struct mlx5e_priv *priv)
{
	mlx5e_disable_rq(&priv->drop_rq);
	mlx5e_destroy_rq(&priv->drop_rq);
	mlx5e_disable_cq(&priv->drop_rq.cq);
	mlx5e_destroy_cq(&priv->drop_rq.cq);
}

static int mlx5e_create_tis(struct mlx5e_priv *priv, int tc)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(create_tis_in)];
	void *tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	memset(in, 0, sizeof(in));

	MLX5_SET(tisc, tisc, prio,  tc);
	MLX5_SET(tisc, tisc, transport_domain, priv->tdn);

	return mlx5_core_create_tis(mdev, in, sizeof(in), &priv->tisn[tc]);
}

static void mlx5e_destroy_tis(struct mlx5e_priv *priv, int tc)
{
	mlx5_core_destroy_tis(priv->mdev, priv->tisn[tc]);
}

static int mlx5e_create_tises(struct mlx5e_priv *priv)
{
	int err;
	int tc;

	for (tc = 0; tc < priv->params.num_tc; tc++) {
		err = mlx5e_create_tis(priv, tc);
		if (err)
			goto err_close_tises;
	}

	return 0;

err_close_tises:
	for (tc--; tc >= 0; tc--)
		mlx5e_destroy_tis(priv, tc);

	return err;
}

static void mlx5e_destroy_tises(struct mlx5e_priv *priv)
{
	int tc;

	for (tc = 0; tc < priv->params.num_tc; tc++)
		mlx5e_destroy_tis(priv, tc);
}

static void mlx5e_build_tir_ctx(struct mlx5e_priv *priv, u32 *tirc, int tt)
{
	void *hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);

	MLX5_SET(tirc, tirc, transport_domain, priv->tdn);

#define MLX5_HASH_IP            (MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP)

#define MLX5_HASH_IP_L4PORTS    (MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_L4_SPORT |\
				 MLX5_HASH_FIELD_SEL_L4_DPORT)

#define MLX5_HASH_IP_IPSEC_SPI  (MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_IPSEC_SPI)

	mlx5e_build_tir_ctx_lro(tirc, priv);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);

	switch (tt) {
	case MLX5E_TT_ANY:
		MLX5_SET(tirc, tirc, indirect_table,
			 priv->rqtn[MLX5E_SINGLE_RQ_RQT]);
		MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_INVERTED_XOR8);
		break;
	default:
		MLX5_SET(tirc, tirc, indirect_table,
			 priv->rqtn[MLX5E_INDIRECTION_RQT]);
		mlx5e_build_tir_ctx_hash(tirc, priv);
		break;
	}

	switch (tt) {
	case MLX5E_TT_IPV4_TCP:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV4);
		MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
			 MLX5_L4_PROT_TYPE_TCP);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_L4PORTS);
		break;

	case MLX5E_TT_IPV6_TCP:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV6);
		MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
			 MLX5_L4_PROT_TYPE_TCP);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_L4PORTS);
		break;

	case MLX5E_TT_IPV4_UDP:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV4);
		MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
			 MLX5_L4_PROT_TYPE_UDP);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_L4PORTS);
		break;

	case MLX5E_TT_IPV6_UDP:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV6);
		MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
			 MLX5_L4_PROT_TYPE_UDP);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_L4PORTS);
		break;

	case MLX5E_TT_IPV4_IPSEC_AH:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV4);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_IPSEC_SPI);
		break;

	case MLX5E_TT_IPV6_IPSEC_AH:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV6);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_IPSEC_SPI);
		break;

	case MLX5E_TT_IPV4_IPSEC_ESP:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV4);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_IPSEC_SPI);
		break;

	case MLX5E_TT_IPV6_IPSEC_ESP:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV6);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP_IPSEC_SPI);
		break;

	case MLX5E_TT_IPV4:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV4);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP);
		break;

	case MLX5E_TT_IPV6:
		MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
			 MLX5_L3_PROT_TYPE_IPV6);
		MLX5_SET(rx_hash_field_select, hfso, selected_fields,
			 MLX5_HASH_IP);
		break;
	}
}

static int mlx5e_create_tir(struct mlx5e_priv *priv, int tt)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 *in;
	void *tirc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);

	mlx5e_build_tir_ctx(priv, tirc, tt);

	err = mlx5_core_create_tir(mdev, in, inlen, &priv->tirn[tt]);

	kvfree(in);

	return err;
}

static void mlx5e_destroy_tir(struct mlx5e_priv *priv, int tt)
{
	mlx5_core_destroy_tir(priv->mdev, priv->tirn[tt]);
}

static int mlx5e_create_tirs(struct mlx5e_priv *priv)
{
	int err;
	int i;

	for (i = 0; i < MLX5E_NUM_TT; i++) {
		err = mlx5e_create_tir(priv, i);
		if (err)
			goto err_destroy_tirs;
	}

	return 0;

err_destroy_tirs:
	for (i--; i >= 0; i--)
		mlx5e_destroy_tir(priv, i);

	return err;
}

static void mlx5e_destroy_tirs(struct mlx5e_priv *priv)
{
	int i;

	for (i = 0; i < MLX5E_NUM_TT; i++)
		mlx5e_destroy_tir(priv, i);
}

static struct rtnl_link_stats64 *
mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_vport_stats *vstats = &priv->stats.vport;

	stats->rx_packets = vstats->rx_packets;
	stats->rx_bytes   = vstats->rx_bytes;
	stats->tx_packets = vstats->tx_packets;
	stats->tx_bytes   = vstats->tx_bytes;
	stats->multicast  = vstats->rx_multicast_packets +
			    vstats->tx_multicast_packets;
	stats->tx_errors  = vstats->tx_error_packets;
	stats->rx_errors  = vstats->rx_error_packets;
	stats->tx_dropped = vstats->tx_queue_dropped;
	stats->rx_crc_errors = 0;
	stats->rx_length_errors = 0;

	return stats;
}

static void mlx5e_set_rx_mode(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	schedule_work(&priv->set_rx_mode_work);
}

static int mlx5e_set_mac(struct net_device *netdev, void *addr)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	netif_addr_lock_bh(netdev);
	ether_addr_copy(netdev->dev_addr, saddr->sa_data);
	netif_addr_unlock_bh(netdev);

	schedule_work(&priv->set_rx_mode_work);

	return 0;
}

static int mlx5e_set_features(struct net_device *netdev,
			      netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;
	netdev_features_t changes = features ^ netdev->features;

	mutex_lock(&priv->state_lock);

	if (changes & NETIF_F_LRO) {
		bool was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);

		if (was_opened)
			mlx5e_close_locked(priv->netdev);

		priv->params.lro_en = !!(features & NETIF_F_LRO);
		err = mlx5e_modify_tirs_lro(priv);
		if (err)
			mlx5_core_warn(priv->mdev, "lro modify failed, %d\n",
				       err);

		if (was_opened)
			err = mlx5e_open_locked(priv->netdev);
	}

	mutex_unlock(&priv->state_lock);

	if (changes & NETIF_F_HW_VLAN_CTAG_FILTER) {
		if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
			mlx5e_enable_vlan_filter(priv);
		else
			mlx5e_disable_vlan_filter(priv);
	}

	return err;
}

static int mlx5e_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool was_opened;
	int max_mtu;
	int err = 0;

	mlx5_query_port_max_mtu(mdev, &max_mtu, 1);

	max_mtu = MLX5E_HW2SW_MTU(max_mtu);

	if (new_mtu > max_mtu) {
		netdev_err(netdev,
			   "%s: Bad MTU (%d) > (%d) Max\n",
			   __func__, new_mtu, max_mtu);
		return -EINVAL;
	}

	mutex_lock(&priv->state_lock);

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (was_opened)
		mlx5e_close_locked(netdev);

	netdev->mtu = new_mtu;

	if (was_opened)
		err = mlx5e_open_locked(netdev);

	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCSHWTSTAMP:
		return mlx5e_hwstamp_set(dev, ifr);
	case SIOCGHWTSTAMP:
		return mlx5e_hwstamp_get(dev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_mac(mdev->priv.eswitch, vf + 1, mac);
}

static int mlx5e_set_vf_vlan(struct net_device *dev, int vf, u16 vlan, u8 qos)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_vlan(mdev->priv.eswitch, vf + 1,
					   vlan, qos);
}

static int mlx5_vport_link2ifla(u8 esw_link)
{
	switch (esw_link) {
	case MLX5_ESW_VPORT_ADMIN_STATE_DOWN:
		return IFLA_VF_LINK_STATE_DISABLE;
	case MLX5_ESW_VPORT_ADMIN_STATE_UP:
		return IFLA_VF_LINK_STATE_ENABLE;
	}
	return IFLA_VF_LINK_STATE_AUTO;
}

static int mlx5_ifla_link2vport(u8 ifla_link)
{
	switch (ifla_link) {
	case IFLA_VF_LINK_STATE_DISABLE:
		return MLX5_ESW_VPORT_ADMIN_STATE_DOWN;
	case IFLA_VF_LINK_STATE_ENABLE:
		return MLX5_ESW_VPORT_ADMIN_STATE_UP;
	}
	return MLX5_ESW_VPORT_ADMIN_STATE_AUTO;
}

static int mlx5e_set_vf_link_state(struct net_device *dev, int vf,
				   int link_state)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_state(mdev->priv.eswitch, vf + 1,
					    mlx5_ifla_link2vport(link_state));
}

static int mlx5e_get_vf_config(struct net_device *dev,
			       int vf, struct ifla_vf_info *ivi)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5_eswitch_get_vport_config(mdev->priv.eswitch, vf + 1, ivi);
	if (err)
		return err;
	ivi->linkstate = mlx5_vport_link2ifla(ivi->linkstate);
	return 0;
}

static int mlx5e_get_vf_stats(struct net_device *dev,
			      int vf, struct ifla_vf_stats *vf_stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_get_vport_stats(mdev->priv.eswitch, vf + 1,
					    vf_stats);
}

static const struct net_device_ops mlx5e_netdev_ops_basic = {
	.ndo_open                = mlx5e_open,
	.ndo_stop                = mlx5e_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_get_stats64         = mlx5e_get_stats,
	.ndo_set_rx_mode         = mlx5e_set_rx_mode,
	.ndo_set_mac_address     = mlx5e_set_mac,
	.ndo_vlan_rx_add_vid     = mlx5e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid    = mlx5e_vlan_rx_kill_vid,
	.ndo_set_features        = mlx5e_set_features,
	.ndo_change_mtu          = mlx5e_change_mtu,
	.ndo_do_ioctl            = mlx5e_ioctl,
};

static const struct net_device_ops mlx5e_netdev_ops_sriov = {
	.ndo_open                = mlx5e_open,
	.ndo_stop                = mlx5e_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_get_stats64         = mlx5e_get_stats,
	.ndo_set_rx_mode         = mlx5e_set_rx_mode,
	.ndo_set_mac_address     = mlx5e_set_mac,
	.ndo_vlan_rx_add_vid     = mlx5e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid    = mlx5e_vlan_rx_kill_vid,
	.ndo_set_features        = mlx5e_set_features,
	.ndo_change_mtu          = mlx5e_change_mtu,
	.ndo_do_ioctl            = mlx5e_ioctl,
	.ndo_set_vf_mac          = mlx5e_set_vf_mac,
	.ndo_set_vf_vlan         = mlx5e_set_vf_vlan,
	.ndo_get_vf_config       = mlx5e_get_vf_config,
	.ndo_set_vf_link_state   = mlx5e_set_vf_link_state,
	.ndo_get_vf_stats        = mlx5e_get_vf_stats,
};

static int mlx5e_check_required_hca_cap(struct mlx5_core_dev *mdev)
{
	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return -ENOTSUPP;
	if (!MLX5_CAP_GEN(mdev, eth_net_offloads) ||
	    !MLX5_CAP_GEN(mdev, nic_flow_table) ||
	    !MLX5_CAP_ETH(mdev, csum_cap) ||
	    !MLX5_CAP_ETH(mdev, max_lso_cap) ||
	    !MLX5_CAP_ETH(mdev, vlan_cap) ||
	    !MLX5_CAP_ETH(mdev, rss_ind_tbl_cap) ||
	    MLX5_CAP_FLOWTABLE(mdev,
			       flow_table_properties_nic_receive.max_ft_level)
			       < 3) {
		mlx5_core_warn(mdev,
			       "Not creating net device, some required device capabilities are missing\n");
		return -ENOTSUPP;
	}
	if (!MLX5_CAP_ETH(mdev, self_lb_en_modifiable))
		mlx5_core_warn(mdev, "Self loop back prevention is not supported\n");

	return 0;
}

u16 mlx5e_get_max_inline_cap(struct mlx5_core_dev *mdev)
{
	int bf_buf_size = (1 << MLX5_CAP_GEN(mdev, log_bf_reg_size)) / 2;

	return bf_buf_size -
	       sizeof(struct mlx5e_tx_wqe) +
	       2 /*sizeof(mlx5e_tx_wqe.inline_hdr_start)*/;
}

void mlx5e_build_default_indir_rqt(u32 *indirection_rqt, int len,
				   int num_channels)
{
	int i;

	for (i = 0; i < len; i++)
		indirection_rqt[i] = i % num_channels;
}

static void mlx5e_build_netdev_priv(struct mlx5_core_dev *mdev,
				    struct net_device *netdev,
				    int num_channels)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	priv->params.log_sq_size           =
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	priv->params.log_rq_size           =
		MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE;
	priv->params.rx_cq_moderation_usec =
		MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC;
	priv->params.rx_cq_moderation_pkts =
		MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS;
	priv->params.tx_cq_moderation_usec =
		MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC;
	priv->params.tx_cq_moderation_pkts =
		MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS;
	priv->params.tx_max_inline         = mlx5e_get_max_inline_cap(mdev);
	priv->params.min_rx_wqes           =
		MLX5E_PARAMS_DEFAULT_MIN_RX_WQES;
	priv->params.num_tc                = 1;
	priv->params.default_vlan_prio     = 0;
	priv->params.rss_hfunc             = ETH_RSS_HASH_XOR;

	netdev_rss_key_fill(priv->params.toeplitz_hash_key,
			    sizeof(priv->params.toeplitz_hash_key));

	mlx5e_build_default_indir_rqt(priv->params.indirection_rqt,
				      MLX5E_INDIR_RQT_SIZE, num_channels);

	priv->params.lro_wqe_sz            =
		MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ;

	priv->mdev                         = mdev;
	priv->netdev                       = netdev;
	priv->params.num_channels          = num_channels;
	priv->default_vlan_prio            = priv->params.default_vlan_prio;

	spin_lock_init(&priv->async_events_spinlock);
	mutex_init(&priv->state_lock);

	INIT_WORK(&priv->update_carrier_work, mlx5e_update_carrier_work);
	INIT_WORK(&priv->set_rx_mode_work, mlx5e_set_rx_mode_work);
	INIT_DELAYED_WORK(&priv->update_stats_work, mlx5e_update_stats_work);
}

static void mlx5e_set_netdev_dev_addr(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5_query_nic_vport_mac_address(priv->mdev, 0, netdev->dev_addr);
	if (is_zero_ether_addr(netdev->dev_addr) &&
	    !MLX5_CAP_GEN(priv->mdev, vport_group_manager)) {
		eth_hw_addr_random(netdev);
		mlx5_core_info(priv->mdev, "Assigned random MAC address %pM\n", netdev->dev_addr);
	}
}

static void mlx5e_build_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	SET_NETDEV_DEV(netdev, &mdev->pdev->dev);

	if (MLX5_CAP_GEN(mdev, vport_group_manager))
		netdev->netdev_ops = &mlx5e_netdev_ops_sriov;
	else
		netdev->netdev_ops = &mlx5e_netdev_ops_basic;

	netdev->watchdog_timeo    = 15 * HZ;

	netdev->ethtool_ops	  = &mlx5e_ethtool_ops;

	netdev->vlan_features    |= NETIF_F_SG;
	netdev->vlan_features    |= NETIF_F_IP_CSUM;
	netdev->vlan_features    |= NETIF_F_IPV6_CSUM;
	netdev->vlan_features    |= NETIF_F_GRO;
	netdev->vlan_features    |= NETIF_F_TSO;
	netdev->vlan_features    |= NETIF_F_TSO6;
	netdev->vlan_features    |= NETIF_F_RXCSUM;
	netdev->vlan_features    |= NETIF_F_RXHASH;

	if (!!MLX5_CAP_ETH(mdev, lro_cap))
		netdev->vlan_features    |= NETIF_F_LRO;

	netdev->hw_features       = netdev->vlan_features;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_RX;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->features          = netdev->hw_features;
	if (!priv->params.lro_en)
		netdev->features  &= ~NETIF_F_LRO;

	netdev->features         |= NETIF_F_HIGHDMA;

	netdev->priv_flags       |= IFF_UNICAST_FLT;

	mlx5e_set_netdev_dev_addr(netdev);
}

static int mlx5e_create_mkey(struct mlx5e_priv *priv, u32 pdn,
			     struct mlx5_core_mr *mr)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_create_mkey_mbox_in *in;
	int err;

	in = mlx5_vzalloc(sizeof(*in));
	if (!in)
		return -ENOMEM;

	in->seg.flags = MLX5_PERM_LOCAL_WRITE |
			MLX5_PERM_LOCAL_READ  |
			MLX5_ACCESS_MODE_PA;
	in->seg.flags_pd = cpu_to_be32(pdn | MLX5_MKEY_LEN64);
	in->seg.qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);

	err = mlx5_core_create_mkey(mdev, mr, in, sizeof(*in), NULL, NULL,
				    NULL);

	kvfree(in);

	return err;
}

static void *mlx5e_create_netdev(struct mlx5_core_dev *mdev)
{
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	int nch = mlx5e_get_max_num_channels(mdev);
	int err;

	if (mlx5e_check_required_hca_cap(mdev))
		return NULL;

	netdev = alloc_etherdev_mqs(sizeof(struct mlx5e_priv), nch, nch);
	if (!netdev) {
		mlx5_core_err(mdev, "alloc_etherdev_mqs() failed\n");
		return NULL;
	}

	mlx5e_build_netdev_priv(mdev, netdev, nch);
	mlx5e_build_netdev(netdev);

	netif_carrier_off(netdev);

	priv = netdev_priv(netdev);

	err = mlx5_alloc_map_uar(mdev, &priv->cq_uar);
	if (err) {
		mlx5_core_err(mdev, "alloc_map uar failed, %d\n", err);
		goto err_free_netdev;
	}

	err = mlx5_core_alloc_pd(mdev, &priv->pdn);
	if (err) {
		mlx5_core_err(mdev, "alloc pd failed, %d\n", err);
		goto err_unmap_free_uar;
	}

	err = mlx5_core_alloc_transport_domain(mdev, &priv->tdn);
	if (err) {
		mlx5_core_err(mdev, "alloc td failed, %d\n", err);
		goto err_dealloc_pd;
	}

	err = mlx5e_create_mkey(priv, priv->pdn, &priv->mr);
	if (err) {
		mlx5_core_err(mdev, "create mkey failed, %d\n", err);
		goto err_dealloc_transport_domain;
	}

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(mdev, "create tises failed, %d\n", err);
		goto err_destroy_mkey;
	}

	err = mlx5e_open_drop_rq(priv);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_destroy_tises;
	}

	err = mlx5e_create_rqt(priv, MLX5E_INDIRECTION_RQT);
	if (err) {
		mlx5_core_warn(mdev, "create rqt(INDIR) failed, %d\n", err);
		goto err_close_drop_rq;
	}

	err = mlx5e_create_rqt(priv, MLX5E_SINGLE_RQ_RQT);
	if (err) {
		mlx5_core_warn(mdev, "create rqt(SINGLE) failed, %d\n", err);
		goto err_destroy_rqt_indir;
	}

	err = mlx5e_create_tirs(priv);
	if (err) {
		mlx5_core_warn(mdev, "create tirs failed, %d\n", err);
		goto err_destroy_rqt_single;
	}

	err = mlx5e_create_flow_tables(priv);
	if (err) {
		mlx5_core_warn(mdev, "create flow tables failed, %d\n", err);
		goto err_destroy_tirs;
	}

	mlx5e_init_eth_addr(priv);

	err = register_netdev(netdev);
	if (err) {
		mlx5_core_err(mdev, "register_netdev failed, %d\n", err);
		goto err_destroy_flow_tables;
	}

	mlx5e_enable_async_events(priv);
	schedule_work(&priv->set_rx_mode_work);

	return priv;

err_destroy_flow_tables:
	mlx5e_destroy_flow_tables(priv);

err_destroy_tirs:
	mlx5e_destroy_tirs(priv);

err_destroy_rqt_single:
	mlx5e_destroy_rqt(priv, MLX5E_SINGLE_RQ_RQT);

err_destroy_rqt_indir:
	mlx5e_destroy_rqt(priv, MLX5E_INDIRECTION_RQT);

err_close_drop_rq:
	mlx5e_close_drop_rq(priv);

err_destroy_tises:
	mlx5e_destroy_tises(priv);

err_destroy_mkey:
	mlx5_core_destroy_mkey(mdev, &priv->mr);

err_dealloc_transport_domain:
	mlx5_core_dealloc_transport_domain(mdev, priv->tdn);

err_dealloc_pd:
	mlx5_core_dealloc_pd(mdev, priv->pdn);

err_unmap_free_uar:
	mlx5_unmap_free_uar(mdev, &priv->cq_uar);

err_free_netdev:
	free_netdev(netdev);

	return NULL;
}

static void mlx5e_destroy_netdev(struct mlx5_core_dev *mdev, void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;
	struct net_device *netdev = priv->netdev;

	set_bit(MLX5E_STATE_DESTROYING, &priv->state);

	schedule_work(&priv->set_rx_mode_work);
	mlx5e_disable_async_events(priv);
	flush_scheduled_work();
	unregister_netdev(netdev);
	mlx5e_destroy_flow_tables(priv);
	mlx5e_destroy_tirs(priv);
	mlx5e_destroy_rqt(priv, MLX5E_SINGLE_RQ_RQT);
	mlx5e_destroy_rqt(priv, MLX5E_INDIRECTION_RQT);
	mlx5e_close_drop_rq(priv);
	mlx5e_destroy_tises(priv);
	mlx5_core_destroy_mkey(priv->mdev, &priv->mr);
	mlx5_core_dealloc_transport_domain(priv->mdev, priv->tdn);
	mlx5_core_dealloc_pd(priv->mdev, priv->pdn);
	mlx5_unmap_free_uar(priv->mdev, &priv->cq_uar);
	free_netdev(netdev);
}

static void *mlx5e_get_netdev(void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;

	return priv->netdev;
}

static struct mlx5_interface mlx5e_interface = {
	.add       = mlx5e_create_netdev,
	.remove    = mlx5e_destroy_netdev,
	.event     = mlx5e_async_event,
	.protocol  = MLX5_INTERFACE_PROTOCOL_ETH,
	.get_dev   = mlx5e_get_netdev,
};

void mlx5e_init(void)
{
	mlx5_register_interface(&mlx5e_interface);
}

void mlx5e_cleanup(void)
{
	mlx5_unregister_interface(&mlx5e_interface);
}

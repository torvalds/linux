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

#include <net/tc_act/tc_gact.h>
#include <net/pkt_cls.h>
#include <linux/mlx5/fs.h>
#include <net/vxlan.h>
#include "en.h"
#include "en_tc.h"
#include "eswitch.h"
#include "vxlan.h"

enum {
	MLX5_EN_QP_FLUSH_TIMEOUT_MS	= 5000,
	MLX5_EN_QP_FLUSH_MSLEEP_QUANT	= 20,
	MLX5_EN_QP_FLUSH_MAX_ITER	= MLX5_EN_QP_FLUSH_TIMEOUT_MS /
					  MLX5_EN_QP_FLUSH_MSLEEP_QUANT,
};

struct mlx5e_rq_param {
	u32			rqc[MLX5_ST_SZ_DW(rqc)];
	struct mlx5_wq_param	wq;
	bool			am_enabled;
};

struct mlx5e_sq_param {
	u32                        sqc[MLX5_ST_SZ_DW(sqc)];
	struct mlx5_wq_param       wq;
	u16                        max_inline;
	u8                         min_inline_mode;
	bool                       icosq;
};

struct mlx5e_cq_param {
	u32                        cqc[MLX5_ST_SZ_DW(cqc)];
	struct mlx5_wq_param       wq;
	u16                        eq_ix;
	u8                         cq_period_mode;
};

struct mlx5e_channel_param {
	struct mlx5e_rq_param      rq;
	struct mlx5e_sq_param      sq;
	struct mlx5e_sq_param      icosq;
	struct mlx5e_cq_param      rx_cq;
	struct mlx5e_cq_param      tx_cq;
	struct mlx5e_cq_param      icosq_cq;
};

static void mlx5e_update_carrier(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 port_state;

	port_state = mlx5_query_vport_state(mdev,
		MLX5_QUERY_VPORT_STATE_IN_OP_MOD_VNIC_VPORT, 0);

	if (port_state == VPORT_STATE_UP) {
		netdev_info(priv->netdev, "Link up\n");
		netif_carrier_on(priv->netdev);
	} else {
		netdev_info(priv->netdev, "Link down\n");
		netif_carrier_off(priv->netdev);
	}
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

static void mlx5e_tx_timeout_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       tx_timeout_work);
	int err;

	rtnl_lock();
	mutex_lock(&priv->state_lock);
	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;
	mlx5e_close_locked(priv->netdev);
	err = mlx5e_open_locked(priv->netdev);
	if (err)
		netdev_err(priv->netdev, "mlx5e_open_locked failed recovering from a tx_timeout, err(%d).\n",
			   err);
unlock:
	mutex_unlock(&priv->state_lock);
	rtnl_unlock();
}

static void mlx5e_update_sw_counters(struct mlx5e_priv *priv)
{
	struct mlx5e_sw_stats *s = &priv->stats.sw;
	struct mlx5e_rq_stats *rq_stats;
	struct mlx5e_sq_stats *sq_stats;
	u64 tx_offload_none = 0;
	int i, j;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < priv->params.num_channels; i++) {
		rq_stats = &priv->channel[i]->rq.stats;

		s->rx_packets	+= rq_stats->packets;
		s->rx_bytes	+= rq_stats->bytes;
		s->rx_lro_packets += rq_stats->lro_packets;
		s->rx_lro_bytes	+= rq_stats->lro_bytes;
		s->rx_csum_none	+= rq_stats->csum_none;
		s->rx_csum_complete += rq_stats->csum_complete;
		s->rx_csum_unnecessary_inner += rq_stats->csum_unnecessary_inner;
		s->rx_wqe_err   += rq_stats->wqe_err;
		s->rx_mpwqe_filler += rq_stats->mpwqe_filler;
		s->rx_mpwqe_frag   += rq_stats->mpwqe_frag;
		s->rx_buff_alloc_err += rq_stats->buff_alloc_err;
		s->rx_cqe_compress_blks += rq_stats->cqe_compress_blks;
		s->rx_cqe_compress_pkts += rq_stats->cqe_compress_pkts;

		for (j = 0; j < priv->params.num_tc; j++) {
			sq_stats = &priv->channel[i]->sq[j].stats;

			s->tx_packets		+= sq_stats->packets;
			s->tx_bytes		+= sq_stats->bytes;
			s->tx_tso_packets	+= sq_stats->tso_packets;
			s->tx_tso_bytes		+= sq_stats->tso_bytes;
			s->tx_tso_inner_packets	+= sq_stats->tso_inner_packets;
			s->tx_tso_inner_bytes	+= sq_stats->tso_inner_bytes;
			s->tx_queue_stopped	+= sq_stats->stopped;
			s->tx_queue_wake	+= sq_stats->wake;
			s->tx_queue_dropped	+= sq_stats->dropped;
			s->tx_csum_partial_inner += sq_stats->csum_partial_inner;
			tx_offload_none		+= sq_stats->csum_none;
		}
	}

	/* Update calculated offload counters */
	s->tx_csum_partial = s->tx_packets - tx_offload_none - s->tx_csum_partial_inner;
	s->rx_csum_unnecessary = s->rx_packets - s->rx_csum_none - s->rx_csum_complete;

	s->link_down_events_phy = MLX5_GET(ppcnt_reg,
				priv->stats.pport.phy_counters,
				counter_set.phys_layer_cntrs.link_down_events);
}

static void mlx5e_update_vport_counters(struct mlx5e_priv *priv)
{
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	u32 *out = (u32 *)priv->stats.vport.query_vport_out;
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)];
	struct mlx5_core_dev *mdev = priv->mdev;

	memset(in, 0, sizeof(in));

	MLX5_SET(query_vport_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	MLX5_SET(query_vport_counter_in, in, op_mod, 0);
	MLX5_SET(query_vport_counter_in, in, other_vport, 0);

	memset(out, 0, outlen);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
}

static void mlx5e_update_pport_counters(struct mlx5e_priv *priv)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	int prio;
	void *out;
	u32 *in;

	in = mlx5_vzalloc(sz);
	if (!in)
		goto free_out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);

	out = pstats->IEEE_802_3_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_IEEE_802_3_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	out = pstats->RFC_2863_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2863_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	out = pstats->RFC_2819_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2819_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	out = pstats->phy_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PHYSICAL_LAYER_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	MLX5_SET(ppcnt_reg, in, grp, MLX5_PER_PRIORITY_COUNTERS_GROUP);
	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		out = pstats->per_prio_counters[prio];
		MLX5_SET(ppcnt_reg, in, prio_tc, prio);
		mlx5_core_access_reg(mdev, in, sz, out, sz,
				     MLX5_REG_PPCNT, 0, 0);
	}

free_out:
	kvfree(in);
}

static void mlx5e_update_q_counter(struct mlx5e_priv *priv)
{
	struct mlx5e_qcounter_stats *qcnt = &priv->stats.qcnt;

	if (!priv->q_counter)
		return;

	mlx5_core_query_out_of_buffer(priv->mdev, priv->q_counter,
				      &qcnt->rx_out_of_buffer);
}

void mlx5e_update_stats(struct mlx5e_priv *priv)
{
	mlx5e_update_q_counter(priv);
	mlx5e_update_vport_counters(priv);
	mlx5e_update_pport_counters(priv);
	mlx5e_update_sw_counters(priv);
}

void mlx5e_update_stats_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlx5e_priv *priv = container_of(dwork, struct mlx5e_priv,
					       update_stats_work);
	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		priv->profile->update_stats(priv);
		queue_delayed_work(priv->wq, dwork,
				   msecs_to_jiffies(MLX5E_UPDATE_STATS_INTERVAL));
	}
	mutex_unlock(&priv->state_lock);
}

static void mlx5e_async_event(struct mlx5_core_dev *mdev, void *vpriv,
			      enum mlx5_dev_event event, unsigned long param)
{
	struct mlx5e_priv *priv = vpriv;

	if (!test_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLED, &priv->state))
		return;

	switch (event) {
	case MLX5_DEV_EVENT_PORT_UP:
	case MLX5_DEV_EVENT_PORT_DOWN:
		queue_work(priv->wq, &priv->update_carrier_work);
		break;

	default:
		break;
	}
}

static void mlx5e_enable_async_events(struct mlx5e_priv *priv)
{
	set_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLED, &priv->state);
}

static void mlx5e_disable_async_events(struct mlx5e_priv *priv)
{
	clear_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLED, &priv->state);
	synchronize_irq(mlx5_get_msix_vec(priv->mdev, MLX5_EQ_VEC_ASYNC));
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
	u32 byte_count;
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

	switch (priv->params.rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		rq->wqe_info = kzalloc_node(wq_sz * sizeof(*rq->wqe_info),
					    GFP_KERNEL, cpu_to_node(c->cpu));
		if (!rq->wqe_info) {
			err = -ENOMEM;
			goto err_rq_wq_destroy;
		}
		rq->handle_rx_cqe = mlx5e_handle_rx_cqe_mpwrq;
		rq->alloc_wqe = mlx5e_alloc_rx_mpwqe;
		rq->dealloc_wqe = mlx5e_dealloc_rx_mpwqe;

		rq->mpwqe_stride_sz = BIT(priv->params.mpwqe_log_stride_sz);
		rq->mpwqe_num_strides = BIT(priv->params.mpwqe_log_num_strides);
		rq->wqe_sz = rq->mpwqe_stride_sz * rq->mpwqe_num_strides;
		byte_count = rq->wqe_sz;
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		rq->skb = kzalloc_node(wq_sz * sizeof(*rq->skb), GFP_KERNEL,
				       cpu_to_node(c->cpu));
		if (!rq->skb) {
			err = -ENOMEM;
			goto err_rq_wq_destroy;
		}
		rq->handle_rx_cqe = mlx5e_handle_rx_cqe;
		rq->alloc_wqe = mlx5e_alloc_rx_wqe;
		rq->dealloc_wqe = mlx5e_dealloc_rx_wqe;

		rq->wqe_sz = (priv->params.lro_en) ?
				priv->params.lro_wqe_sz :
				MLX5E_SW2HW_MTU(priv->netdev->mtu);
		rq->wqe_sz = SKB_DATA_ALIGN(rq->wqe_sz);
		byte_count = rq->wqe_sz;
		byte_count |= MLX5_HW_START_PADDING;
	}

	for (i = 0; i < wq_sz; i++) {
		struct mlx5e_rx_wqe *wqe = mlx5_wq_ll_get_wqe(&rq->wq, i);

		wqe->data.byte_count = cpu_to_be32(byte_count);
	}

	INIT_WORK(&rq->am.work, mlx5e_rx_am_work);
	rq->am.mode = priv->params.rx_cq_period_mode;

	rq->wq_type = priv->params.rq_wq_type;
	rq->pdev    = c->pdev;
	rq->netdev  = c->netdev;
	rq->tstamp  = &priv->tstamp;
	rq->channel = c;
	rq->ix      = c->ix;
	rq->priv    = c->priv;
	rq->mkey_be = c->mkey_be;
	rq->umr_mkey_be = cpu_to_be32(c->priv->umr_mkey.key);

	return 0;

err_rq_wq_destroy:
	mlx5_wq_destroy(&rq->wq_ctrl);

	return err;
}

static void mlx5e_destroy_rq(struct mlx5e_rq *rq)
{
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		kfree(rq->wqe_info);
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		kfree(rq->skb);
	}

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
	MLX5_SET(rqc,  rqc, vsd, priv->params.vlan_strip_disable);
	MLX5_SET(wq,   wq,  log_wq_pg_sz,	rq->wq_ctrl.buf.page_shift -
						MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq,  dbr_addr,		rq->wq_ctrl.db.dma);

	mlx5_fill_page_array(&rq->wq_ctrl.buf,
			     (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_rq(mdev, in, inlen, &rq->rqn);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_state(struct mlx5e_rq *rq, int curr_state,
				 int next_state)
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

static int mlx5e_modify_rq_vsd(struct mlx5e_rq *rq, bool vsd)
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

	MLX5_SET(modify_rq_in, in, rq_state, MLX5_RQC_STATE_RDY);
	MLX5_SET64(modify_rq_in, in, modify_bitmask, MLX5_RQ_BITMASK_VSD);
	MLX5_SET(rqc, rqc, vsd, vsd);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RDY);

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
	struct mlx5e_sq *sq = &c->icosq;
	u16 pi = sq->pc & sq->wq.sz_m1;
	int err;

	err = mlx5e_create_rq(c, param, rq);
	if (err)
		return err;

	err = mlx5e_enable_rq(rq, param);
	if (err)
		goto err_destroy_rq;

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_disable_rq;

	if (param->am_enabled)
		set_bit(MLX5E_RQ_STATE_AM, &c->rq.state);

	set_bit(MLX5E_RQ_STATE_POST_WQES_ENABLE, &rq->state);

	sq->ico_wqe_info[pi].opcode     = MLX5_OPCODE_NOP;
	sq->ico_wqe_info[pi].num_wqebbs = 1;
	mlx5e_send_nop(sq, true); /* trigger mlx5e_post_rx_wqes() */

	return 0;

err_disable_rq:
	mlx5e_disable_rq(rq);
err_destroy_rq:
	mlx5e_destroy_rq(rq);

	return err;
}

static void mlx5e_close_rq(struct mlx5e_rq *rq)
{
	int tout = 0;
	int err;

	clear_bit(MLX5E_RQ_STATE_POST_WQES_ENABLE, &rq->state);
	napi_synchronize(&rq->channel->napi); /* prevent mlx5e_post_rx_wqes */

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RDY, MLX5_RQC_STATE_ERR);
	while (!mlx5_wq_ll_is_empty(&rq->wq) && !err &&
	       tout++ < MLX5_EN_QP_FLUSH_MAX_ITER)
		msleep(MLX5_EN_QP_FLUSH_MSLEEP_QUANT);

	if (err || tout == MLX5_EN_QP_FLUSH_MAX_ITER)
		set_bit(MLX5E_RQ_STATE_FLUSH_TIMEOUT, &rq->state);

	/* avoid destroying rq before mlx5e_poll_rx_cq() is done with it */
	napi_synchronize(&rq->channel->napi);

	cancel_work_sync(&rq->am.work);

	mlx5e_disable_rq(rq);
	mlx5e_free_rx_descs(rq);
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
	int err;

	err = mlx5_alloc_map_uar(mdev, &sq->uar, !!MLX5_CAP_GEN(mdev, bf));
	if (err)
		return err;

	param->wq.db_numa_node = cpu_to_node(c->cpu);

	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, &sq->wq,
				 &sq->wq_ctrl);
	if (err)
		goto err_unmap_free_uar;

	sq->wq.db       = &sq->wq.db[MLX5_SND_DBR];
	if (sq->uar.bf_map) {
		set_bit(MLX5E_SQ_STATE_BF_ENABLE, &sq->state);
		sq->uar_map = sq->uar.bf_map;
	} else {
		sq->uar_map = sq->uar.map;
	}
	sq->bf_buf_size = (1 << MLX5_CAP_GEN(mdev, log_bf_reg_size)) / 2;
	sq->max_inline  = param->max_inline;
	sq->min_inline_mode =
		MLX5_CAP_ETH(mdev, wqe_inline_mode) == MLX5E_INLINE_MODE_VPORT_CONTEXT ?
		param->min_inline_mode : 0;

	err = mlx5e_alloc_sq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	if (param->icosq) {
		u8 wq_sz = mlx5_wq_cyc_get_size(&sq->wq);

		sq->ico_wqe_info = kzalloc_node(sizeof(*sq->ico_wqe_info) *
						wq_sz,
						GFP_KERNEL,
						cpu_to_node(c->cpu));
		if (!sq->ico_wqe_info) {
			err = -ENOMEM;
			goto err_free_sq_db;
		}
	} else {
		int txq_ix;

		txq_ix = c->ix + tc * priv->params.num_channels;
		sq->txq = netdev_get_tx_queue(priv->netdev, txq_ix);
		priv->txq_to_sq_map[txq_ix] = sq;
	}

	sq->pdev      = c->pdev;
	sq->tstamp    = &priv->tstamp;
	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->tc        = tc;
	sq->edge      = (sq->wq.sz_m1 + 1) - MLX5_SEND_WQE_MAX_WQEBBS;
	sq->bf_budget = MLX5E_SQ_BF_BUDGET;

	return 0;

err_free_sq_db:
	mlx5e_free_sq_db(sq);

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

	kfree(sq->ico_wqe_info);
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

	MLX5_SET(sqc,  sqc, tis_num_0, param->icosq ? 0 : priv->tisn[sq->tc]);
	MLX5_SET(sqc,  sqc, cqn,		sq->cq.mcq.cqn);
	MLX5_SET(sqc,  sqc, min_wqe_inline_mode, sq->min_inline_mode);
	MLX5_SET(sqc,  sqc, state,		MLX5_SQC_STATE_RST);
	MLX5_SET(sqc,  sqc, tis_lst_sz,		param->icosq ? 0 : 1);
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

static int mlx5e_modify_sq(struct mlx5e_sq *sq, int curr_state,
			   int next_state, bool update_rl, int rl_index)
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
	if (update_rl && next_state == MLX5_SQC_STATE_RDY) {
		MLX5_SET64(modify_sq_in, in, modify_bitmask, 1);
		MLX5_SET(sqc,  sqc, packet_pacing_rate_limit_index, rl_index);
	}

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
	if (sq->rate_limit)
		mlx5_rl_remove_rate(mdev, sq->rate_limit);
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

	err = mlx5e_modify_sq(sq, MLX5_SQC_STATE_RST, MLX5_SQC_STATE_RDY,
			      false, 0);
	if (err)
		goto err_disable_sq;

	if (sq->txq) {
		set_bit(MLX5E_SQ_STATE_WAKE_TXQ_ENABLE, &sq->state);
		netdev_tx_reset_queue(sq->txq);
		netif_tx_start_queue(sq->txq);
	}

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
	int tout = 0;
	int err;

	if (sq->txq) {
		clear_bit(MLX5E_SQ_STATE_WAKE_TXQ_ENABLE, &sq->state);
		/* prevent netif_tx_wake_queue */
		napi_synchronize(&sq->channel->napi);
		netif_tx_disable_queue(sq->txq);

		/* ensure hw is notified of all pending wqes */
		if (mlx5e_sq_has_room_for(sq, 1))
			mlx5e_send_nop(sq, true);

		err = mlx5e_modify_sq(sq, MLX5_SQC_STATE_RDY,
				      MLX5_SQC_STATE_ERR, false, 0);
		if (err)
			set_bit(MLX5E_SQ_STATE_TX_TIMEOUT, &sq->state);
	}

	/* wait till sq is empty, unless a TX timeout occurred on this SQ */
	while (sq->cc != sq->pc &&
	       !test_bit(MLX5E_SQ_STATE_TX_TIMEOUT, &sq->state)) {
		msleep(MLX5_EN_QP_FLUSH_MSLEEP_QUANT);
		if (tout++ > MLX5_EN_QP_FLUSH_MAX_ITER)
			set_bit(MLX5E_SQ_STATE_TX_TIMEOUT, &sq->state);
	}

	/* avoid destroying sq before mlx5e_poll_tx_cq() is done with it */
	napi_synchronize(&sq->channel->napi);

	mlx5e_free_tx_descs(sq);
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
	mcq->uar        = &mdev->mlx5e_res.cq_uar;

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

	MLX5_SET(cqc,   cqc, cq_period_mode, param->cq_period_mode);
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
			 struct mlx5e_cq_moder moderation)
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

	if (MLX5_CAP_GEN(mdev, cq_moderation))
		mlx5_core_modify_cq_moderation(mdev, &cq->mcq,
					       moderation.usec,
					       moderation.pkts);
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
				    priv->params.tx_cq_moderation);
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

	for (i = 0; i < priv->profile->max_tc; i++)
		priv->channeltc_to_txq_map[ix][i] =
			ix + i * priv->params.num_channels;
}

static int mlx5e_set_sq_maxrate(struct net_device *dev,
				struct mlx5e_sq *sq, u32 rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 rl_index = 0;
	int err;

	if (rate == sq->rate_limit)
		/* nothing to do */
		return 0;

	if (sq->rate_limit)
		/* remove current rl index to free space to next ones */
		mlx5_rl_remove_rate(mdev, sq->rate_limit);

	sq->rate_limit = 0;

	if (rate) {
		err = mlx5_rl_add_rate(mdev, rate, &rl_index);
		if (err) {
			netdev_err(dev, "Failed configuring rate %u: %d\n",
				   rate, err);
			return err;
		}
	}

	err = mlx5e_modify_sq(sq, MLX5_SQC_STATE_RDY,
			      MLX5_SQC_STATE_RDY, true, rl_index);
	if (err) {
		netdev_err(dev, "Failed configuring rate %u: %d\n",
			   rate, err);
		/* remove the rate from the table */
		if (rate)
			mlx5_rl_remove_rate(mdev, rate);
		return err;
	}

	sq->rate_limit = rate;
	return 0;
}

static int mlx5e_set_tx_maxrate(struct net_device *dev, int index, u32 rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_sq *sq = priv->txq_to_sq_map[index];
	int err = 0;

	if (!mlx5_rl_is_supported(mdev)) {
		netdev_err(dev, "Rate limiting is not supported on this device\n");
		return -EINVAL;
	}

	/* rate is given in Mb/sec, HW config is in Kb/sec */
	rate = rate << 10;

	/* Check whether rate in valid range, 0 is always valid */
	if (rate && !mlx5_rl_is_in_range(mdev, rate)) {
		netdev_err(dev, "TX rate %u, is not in range\n", rate);
		return -ERANGE;
	}

	mutex_lock(&priv->state_lock);
	if (test_bit(MLX5E_STATE_OPENED, &priv->state))
		err = mlx5e_set_sq_maxrate(dev, sq, rate);
	if (!err)
		priv->tx_rates[index] = rate;
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_open_channel(struct mlx5e_priv *priv, int ix,
			      struct mlx5e_channel_param *cparam,
			      struct mlx5e_channel **cp)
{
	struct mlx5e_cq_moder icosq_cq_moder = {0, 0};
	struct net_device *netdev = priv->netdev;
	struct mlx5e_cq_moder rx_cq_profile;
	int cpu = mlx5e_get_cpu(priv, ix);
	struct mlx5e_channel *c;
	struct mlx5e_sq *sq;
	int err;
	int i;

	c = kzalloc_node(sizeof(*c), GFP_KERNEL, cpu_to_node(cpu));
	if (!c)
		return -ENOMEM;

	c->priv     = priv;
	c->ix       = ix;
	c->cpu      = cpu;
	c->pdev     = &priv->mdev->pdev->dev;
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.mkey.key);
	c->num_tc   = priv->params.num_tc;

	if (priv->params.rx_am_enabled)
		rx_cq_profile = mlx5e_am_get_def_profile(priv->params.rx_cq_period_mode);
	else
		rx_cq_profile = priv->params.rx_cq_moderation;

	mlx5e_build_channeltc_to_txq_map(priv, ix);

	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll, 64);

	err = mlx5e_open_cq(c, &cparam->icosq_cq, &c->icosq.cq, icosq_cq_moder);
	if (err)
		goto err_napi_del;

	err = mlx5e_open_tx_cqs(c, cparam);
	if (err)
		goto err_close_icosq_cq;

	err = mlx5e_open_cq(c, &cparam->rx_cq, &c->rq.cq,
			    rx_cq_profile);
	if (err)
		goto err_close_tx_cqs;

	napi_enable(&c->napi);

	err = mlx5e_open_sq(c, 0, &cparam->icosq, &c->icosq);
	if (err)
		goto err_disable_napi;

	err = mlx5e_open_sqs(c, cparam);
	if (err)
		goto err_close_icosq;

	for (i = 0; i < priv->params.num_tc; i++) {
		u32 txq_ix = priv->channeltc_to_txq_map[ix][i];

		if (priv->tx_rates[txq_ix]) {
			sq = priv->txq_to_sq_map[txq_ix];
			mlx5e_set_sq_maxrate(priv->netdev, sq,
					     priv->tx_rates[txq_ix]);
		}
	}

	err = mlx5e_open_rq(c, &cparam->rq, &c->rq);
	if (err)
		goto err_close_sqs;

	netif_set_xps_queue(netdev, get_cpu_mask(c->cpu), ix);
	*cp = c;

	return 0;

err_close_sqs:
	mlx5e_close_sqs(c);

err_close_icosq:
	mlx5e_close_sq(&c->icosq);

err_disable_napi:
	napi_disable(&c->napi);
	mlx5e_close_cq(&c->rq.cq);

err_close_tx_cqs:
	mlx5e_close_tx_cqs(c);

err_close_icosq_cq:
	mlx5e_close_cq(&c->icosq.cq);

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
	mlx5e_close_sq(&c->icosq);
	napi_disable(&c->napi);
	mlx5e_close_cq(&c->rq.cq);
	mlx5e_close_tx_cqs(c);
	mlx5e_close_cq(&c->icosq.cq);
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

	switch (priv->params.rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		MLX5_SET(wq, wq, log_wqe_num_of_strides,
			 priv->params.mpwqe_log_num_strides - 9);
		MLX5_SET(wq, wq, log_wqe_stride_size,
			 priv->params.mpwqe_log_stride_sz - 6);
		MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ);
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_LINKED_LIST);
	}

	MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);
	MLX5_SET(wq, wq, log_wq_stride,    ilog2(sizeof(struct mlx5e_rx_wqe)));
	MLX5_SET(wq, wq, log_wq_sz,        priv->params.log_rq_size);
	MLX5_SET(wq, wq, pd,               priv->mdev->mlx5e_res.pdn);
	MLX5_SET(rqc, rqc, counter_set_id, priv->q_counter);

	param->wq.buf_numa_node = dev_to_node(&priv->mdev->pdev->dev);
	param->wq.linear = 1;

	param->am_enabled = priv->params.rx_am_enabled;
}

static void mlx5e_build_drop_rq_param(struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_LINKED_LIST);
	MLX5_SET(wq, wq, log_wq_stride,    ilog2(sizeof(struct mlx5e_rx_wqe)));
}

static void mlx5e_build_sq_param_common(struct mlx5e_priv *priv,
					struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd,            priv->mdev->mlx5e_res.pdn);

	param->wq.buf_numa_node = dev_to_node(&priv->mdev->pdev->dev);
}

static void mlx5e_build_sq_param(struct mlx5e_priv *priv,
				 struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);
	MLX5_SET(wq, wq, log_wq_sz,     priv->params.log_sq_size);

	param->max_inline = priv->params.tx_max_inline;
	param->min_inline_mode = priv->params.tx_min_inline_mode;
}

static void mlx5e_build_common_cq_param(struct mlx5e_priv *priv,
					struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, uar_page, priv->mdev->mlx5e_res.cq_uar.index);
}

static void mlx5e_build_rx_cq_param(struct mlx5e_priv *priv,
				    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;
	u8 log_cq_size;

	switch (priv->params.rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		log_cq_size = priv->params.log_rq_size +
			priv->params.mpwqe_log_num_strides;
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		log_cq_size = priv->params.log_rq_size;
	}

	MLX5_SET(cqc, cqc, log_cq_size, log_cq_size);
	if (priv->params.rx_cqe_compress) {
		MLX5_SET(cqc, cqc, mini_cqe_res_format, MLX5_CQE_FORMAT_CSUM);
		MLX5_SET(cqc, cqc, cqe_comp_en, 1);
	}

	mlx5e_build_common_cq_param(priv, param);

	param->cq_period_mode = priv->params.rx_cq_period_mode;
}

static void mlx5e_build_tx_cq_param(struct mlx5e_priv *priv,
				    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, priv->params.log_sq_size);

	mlx5e_build_common_cq_param(priv, param);

	param->cq_period_mode = MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
}

static void mlx5e_build_ico_cq_param(struct mlx5e_priv *priv,
				     struct mlx5e_cq_param *param,
				     u8 log_wq_size)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, log_wq_size);

	mlx5e_build_common_cq_param(priv, param);

	param->cq_period_mode = MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
}

static void mlx5e_build_icosq_param(struct mlx5e_priv *priv,
				    struct mlx5e_sq_param *param,
				    u8 log_wq_size)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);

	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
	MLX5_SET(sqc, sqc, reg_umr, MLX5_CAP_ETH(priv->mdev, reg_umr_sq));

	param->icosq = true;
}

static void mlx5e_build_channel_param(struct mlx5e_priv *priv, struct mlx5e_channel_param *cparam)
{
	u8 icosq_log_wq_sz = MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;

	mlx5e_build_rq_param(priv, &cparam->rq);
	mlx5e_build_sq_param(priv, &cparam->sq);
	mlx5e_build_icosq_param(priv, &cparam->icosq, icosq_log_wq_sz);
	mlx5e_build_rx_cq_param(priv, &cparam->rx_cq);
	mlx5e_build_tx_cq_param(priv, &cparam->tx_cq);
	mlx5e_build_ico_cq_param(priv, &cparam->icosq_cq, icosq_log_wq_sz);
}

static int mlx5e_open_channels(struct mlx5e_priv *priv)
{
	struct mlx5e_channel_param *cparam;
	int nch = priv->params.num_channels;
	int err = -ENOMEM;
	int i;
	int j;

	priv->channel = kcalloc(nch, sizeof(struct mlx5e_channel *),
				GFP_KERNEL);

	priv->txq_to_sq_map = kcalloc(nch * priv->params.num_tc,
				      sizeof(struct mlx5e_sq *), GFP_KERNEL);

	cparam = kzalloc(sizeof(struct mlx5e_channel_param), GFP_KERNEL);

	if (!priv->channel || !priv->txq_to_sq_map || !cparam)
		goto err_free_txq_to_sq_map;

	mlx5e_build_channel_param(priv, cparam);

	for (i = 0; i < nch; i++) {
		err = mlx5e_open_channel(priv, i, cparam, &priv->channel[i]);
		if (err)
			goto err_close_channels;
	}

	for (j = 0; j < nch; j++) {
		err = mlx5e_wait_for_min_rx_wqes(&priv->channel[j]->rq);
		if (err)
			goto err_close_channels;
	}

	/* FIXME: This is a W/A for tx timeout watch dog false alarm when
	 * polling for inactive tx queues.
	 */
	netif_tx_start_all_queues(priv->netdev);

	kfree(cparam);
	return 0;

err_close_channels:
	for (i--; i >= 0; i--)
		mlx5e_close_channel(priv->channel[i]);

err_free_txq_to_sq_map:
	kfree(priv->txq_to_sq_map);
	kfree(priv->channel);
	kfree(cparam);

	return err;
}

static void mlx5e_close_channels(struct mlx5e_priv *priv)
{
	int i;

	/* FIXME: This is a W/A only for tx timeout watch dog false alarm when
	 * polling for inactive tx queues.
	 */
	netif_tx_stop_all_queues(priv->netdev);
	netif_tx_disable(priv->netdev);

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
		u32 rqn;

		if (priv->params.rss_hfunc == ETH_RSS_HASH_XOR)
			ix = mlx5e_bits_invert(i, MLX5E_LOG_INDIR_RQT_SIZE);

		ix = priv->params.indirection_rqt[ix];
		rqn = test_bit(MLX5E_STATE_OPENED, &priv->state) ?
				priv->channel[ix]->rq.rqn :
				priv->drop_rq.rqn;
		MLX5_SET(rqtc, rqtc, rq_num[i], rqn);
	}
}

static void mlx5e_fill_direct_rqt_rqn(struct mlx5e_priv *priv, void *rqtc,
				      int ix)
{
	u32 rqn = test_bit(MLX5E_STATE_OPENED, &priv->state) ?
			priv->channel[ix]->rq.rqn :
			priv->drop_rq.rqn;

	MLX5_SET(rqtc, rqtc, rq_num[0], rqn);
}

static int mlx5e_create_rqt(struct mlx5e_priv *priv, int sz,
			    int ix, struct mlx5e_rqt *rqt)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqtc;
	int inlen;
	int err;
	u32 *in;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + sizeof(u32) * sz;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	MLX5_SET(rqtc, rqtc, rqt_max_size, sz);

	if (sz > 1) /* RSS */
		mlx5e_fill_indir_rqt_rqns(priv, rqtc);
	else
		mlx5e_fill_direct_rqt_rqn(priv, rqtc, ix);

	err = mlx5_core_create_rqt(mdev, in, inlen, &rqt->rqtn);
	if (!err)
		rqt->enabled = true;

	kvfree(in);
	return err;
}

void mlx5e_destroy_rqt(struct mlx5e_priv *priv, struct mlx5e_rqt *rqt)
{
	rqt->enabled = false;
	mlx5_core_destroy_rqt(priv->mdev, rqt->rqtn);
}

static int mlx5e_create_indirect_rqts(struct mlx5e_priv *priv)
{
	struct mlx5e_rqt *rqt = &priv->indir_rqt;

	return mlx5e_create_rqt(priv, MLX5E_INDIR_RQT_SIZE, 0, rqt);
}

int mlx5e_create_direct_rqts(struct mlx5e_priv *priv)
{
	struct mlx5e_rqt *rqt;
	int err;
	int ix;

	for (ix = 0; ix < priv->profile->max_nch(priv->mdev); ix++) {
		rqt = &priv->direct_tir[ix].rqt;
		err = mlx5e_create_rqt(priv, 1 /*size */, ix, rqt);
		if (err)
			goto err_destroy_rqts;
	}

	return 0;

err_destroy_rqts:
	for (ix--; ix >= 0; ix--)
		mlx5e_destroy_rqt(priv, &priv->direct_tir[ix].rqt);

	return err;
}

int mlx5e_redirect_rqt(struct mlx5e_priv *priv, u32 rqtn, int sz, int ix)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqtc;
	int inlen;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + sizeof(u32) * sz;
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	if (sz > 1) /* RSS */
		mlx5e_fill_indir_rqt_rqns(priv, rqtc);
	else
		mlx5e_fill_direct_rqt_rqn(priv, rqtc, ix);

	MLX5_SET(modify_rqt_in, in, bitmask.rqn_list, 1);

	err = mlx5_core_modify_rqt(mdev, rqtn, in, inlen);

	kvfree(in);

	return err;
}

static void mlx5e_redirect_rqts(struct mlx5e_priv *priv)
{
	u32 rqtn;
	int ix;

	if (priv->indir_rqt.enabled) {
		rqtn = priv->indir_rqt.rqtn;
		mlx5e_redirect_rqt(priv, rqtn, MLX5E_INDIR_RQT_SIZE, 0);
	}

	for (ix = 0; ix < priv->params.num_channels; ix++) {
		if (!priv->direct_tir[ix].rqt.enabled)
			continue;
		rqtn = priv->direct_tir[ix].rqt.rqtn;
		mlx5e_redirect_rqt(priv, rqtn, 1, ix);
	}
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
	int ix;

	inlen = MLX5_ST_SZ_BYTES(modify_tir_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_tir_in, in, bitmask.lro, 1);
	tirc = MLX5_ADDR_OF(modify_tir_in, in, ctx);

	mlx5e_build_tir_ctx_lro(tirc, priv);

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		err = mlx5_core_modify_tir(mdev, priv->indir_tir[tt].tirn, in,
					   inlen);
		if (err)
			goto free_in;
	}

	for (ix = 0; ix < priv->profile->max_nch(priv->mdev); ix++) {
		err = mlx5_core_modify_tir(mdev, priv->direct_tir[ix].tirn,
					   in, inlen);
		if (err)
			goto free_in;
	}

free_in:
	kvfree(in);

	return err;
}

static int mlx5e_set_mtu(struct mlx5e_priv *priv, u16 mtu)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 hw_mtu = MLX5E_SW2HW_MTU(mtu);
	int err;

	err = mlx5_set_port_mtu(mdev, hw_mtu, 1);
	if (err)
		return err;

	/* Update vport context MTU */
	mlx5_modify_nic_vport_mtu(mdev, hw_mtu);
	return 0;
}

static void mlx5e_query_mtu(struct mlx5e_priv *priv, u16 *mtu)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 hw_mtu = 0;
	int err;

	err = mlx5_query_nic_vport_mtu(mdev, &hw_mtu);
	if (err || !hw_mtu) /* fallback to port oper mtu */
		mlx5_query_port_oper_mtu(mdev, &hw_mtu, 1);

	*mtu = MLX5E_HW2SW_MTU(hw_mtu);
}

static int mlx5e_set_dev_port_mtu(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	u16 mtu;
	int err;

	err = mlx5e_set_mtu(priv, netdev->mtu);
	if (err)
		return err;

	mlx5e_query_mtu(priv, &mtu);
	if (mtu != netdev->mtu)
		netdev_warn(netdev, "%s: VPort MTU %d is different than netdev mtu %d\n",
			    __func__, mtu, netdev->mtu);

	netdev->mtu = mtu;
	return 0;
}

static void mlx5e_netdev_set_tcs(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int nch = priv->params.num_channels;
	int ntc = priv->params.num_tc;
	int tc;

	netdev_reset_tc(netdev);

	if (ntc == 1)
		return;

	netdev_set_num_tc(netdev, ntc);

	/* Map netdev TCs to offset 0
	 * We have our own UP to TXQ mapping for QoS
	 */
	for (tc = 0; tc < ntc; tc++)
		netdev_set_tc_queue(netdev, tc, nch, 0);
}

int mlx5e_open_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int num_txqs;
	int err;

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	mlx5e_netdev_set_tcs(netdev);

	num_txqs = priv->params.num_channels * priv->params.num_tc;
	netif_set_real_num_tx_queues(netdev, num_txqs);
	netif_set_real_num_rx_queues(netdev, priv->params.num_channels);

	err = mlx5e_open_channels(priv);
	if (err) {
		netdev_err(netdev, "%s: mlx5e_open_channels failed, %d\n",
			   __func__, err);
		goto err_clear_state_opened_flag;
	}

	err = mlx5e_refresh_tirs_self_loopback_enable(priv->mdev);
	if (err) {
		netdev_err(netdev, "%s: mlx5e_refresh_tirs_self_loopback_enable failed, %d\n",
			   __func__, err);
		goto err_close_channels;
	}

	mlx5e_redirect_rqts(priv);
	mlx5e_update_carrier(priv);
	mlx5e_timestamp_init(priv);
#ifdef CONFIG_RFS_ACCEL
	priv->netdev->rx_cpu_rmap = priv->mdev->rmap;
#endif
	if (priv->profile->update_stats)
		queue_delayed_work(priv->wq, &priv->update_stats_work, 0);

	if (MLX5_CAP_GEN(mdev, vport_group_manager)) {
		err = mlx5e_add_sqs_fwd_rules(priv);
		if (err)
			goto err_close_channels;
	}
	return 0;

err_close_channels:
	mlx5e_close_channels(priv);
err_clear_state_opened_flag:
	clear_bit(MLX5E_STATE_OPENED, &priv->state);
	return err;
}

int mlx5e_open(struct net_device *netdev)
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
	struct mlx5_core_dev *mdev = priv->mdev;

	/* May already be CLOSED in case a previous configuration operation
	 * (e.g RX/TX queue size change) that involves close&open failed.
	 */
	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	clear_bit(MLX5E_STATE_OPENED, &priv->state);

	if (MLX5_CAP_GEN(mdev, vport_group_manager))
		mlx5e_remove_sqs_fwd_rules(priv);

	mlx5e_timestamp_cleanup(priv);
	netif_carrier_off(priv->netdev);
	mlx5e_redirect_rqts(priv);
	mlx5e_close_channels(priv);

	return 0;
}

int mlx5e_close(struct net_device *netdev)
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
	mcq->uar        = &mdev->mlx5e_res.cq_uar;

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
	mlx5e_build_drop_rq_param(&rq_param);

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

	MLX5_SET(tisc, tisc, prio, tc << 1);
	MLX5_SET(tisc, tisc, transport_domain, mdev->mlx5e_res.td.tdn);

	return mlx5_core_create_tis(mdev, in, sizeof(in), &priv->tisn[tc]);
}

static void mlx5e_destroy_tis(struct mlx5e_priv *priv, int tc)
{
	mlx5_core_destroy_tis(priv->mdev, priv->tisn[tc]);
}

int mlx5e_create_tises(struct mlx5e_priv *priv)
{
	int err;
	int tc;

	for (tc = 0; tc < priv->profile->max_tc; tc++) {
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

void mlx5e_cleanup_nic_tx(struct mlx5e_priv *priv)
{
	int tc;

	for (tc = 0; tc < priv->profile->max_tc; tc++)
		mlx5e_destroy_tis(priv, tc);
}

static void mlx5e_build_indir_tir_ctx(struct mlx5e_priv *priv, u32 *tirc,
				      enum mlx5e_traffic_types tt)
{
	void *hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);

	MLX5_SET(tirc, tirc, transport_domain, priv->mdev->mlx5e_res.td.tdn);

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
	MLX5_SET(tirc, tirc, indirect_table, priv->indir_rqt.rqtn);
	mlx5e_build_tir_ctx_hash(tirc, priv);

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
	default:
		WARN_ONCE(true,
			  "mlx5e_build_indir_tir_ctx: bad traffic type!\n");
	}
}

static void mlx5e_build_direct_tir_ctx(struct mlx5e_priv *priv, u32 *tirc,
				       u32 rqtn)
{
	MLX5_SET(tirc, tirc, transport_domain, priv->mdev->mlx5e_res.td.tdn);

	mlx5e_build_tir_ctx_lro(tirc, priv);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table, rqtn);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_INVERTED_XOR8);
}

static int mlx5e_create_indirect_tirs(struct mlx5e_priv *priv)
{
	struct mlx5e_tir *tir;
	void *tirc;
	int inlen;
	int err;
	u32 *in;
	int tt;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		memset(in, 0, inlen);
		tir = &priv->indir_tir[tt];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_indir_tir_ctx(priv, tirc, tt);
		err = mlx5e_create_tir(priv->mdev, tir, in, inlen);
		if (err)
			goto err_destroy_tirs;
	}

	kvfree(in);

	return 0;

err_destroy_tirs:
	for (tt--; tt >= 0; tt--)
		mlx5e_destroy_tir(priv->mdev, &priv->indir_tir[tt]);

	kvfree(in);

	return err;
}

int mlx5e_create_direct_tirs(struct mlx5e_priv *priv)
{
	int nch = priv->profile->max_nch(priv->mdev);
	struct mlx5e_tir *tir;
	void *tirc;
	int inlen;
	int err;
	u32 *in;
	int ix;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	for (ix = 0; ix < nch; ix++) {
		memset(in, 0, inlen);
		tir = &priv->direct_tir[ix];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_direct_tir_ctx(priv, tirc,
					   priv->direct_tir[ix].rqt.rqtn);
		err = mlx5e_create_tir(priv->mdev, tir, in, inlen);
		if (err)
			goto err_destroy_ch_tirs;
	}

	kvfree(in);

	return 0;

err_destroy_ch_tirs:
	for (ix--; ix >= 0; ix--)
		mlx5e_destroy_tir(priv->mdev, &priv->direct_tir[ix]);

	kvfree(in);

	return err;
}

static void mlx5e_destroy_indirect_tirs(struct mlx5e_priv *priv)
{
	int i;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->indir_tir[i]);
}

void mlx5e_destroy_direct_tirs(struct mlx5e_priv *priv)
{
	int nch = priv->profile->max_nch(priv->mdev);
	int i;

	for (i = 0; i < nch; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->direct_tir[i]);
}

int mlx5e_modify_rqs_vsd(struct mlx5e_priv *priv, bool vsd)
{
	int err = 0;
	int i;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	for (i = 0; i < priv->params.num_channels; i++) {
		err = mlx5e_modify_rq_vsd(&priv->channel[i]->rq, vsd);
		if (err)
			return err;
	}

	return 0;
}

static int mlx5e_setup_tc(struct net_device *netdev, u8 tc)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	bool was_opened;
	int err = 0;

	if (tc && tc != MLX5E_MAX_NUM_TC)
		return -EINVAL;

	mutex_lock(&priv->state_lock);

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (was_opened)
		mlx5e_close_locked(priv->netdev);

	priv->params.num_tc = tc ? tc : 1;

	if (was_opened)
		err = mlx5e_open_locked(priv->netdev);

	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_ndo_setup_tc(struct net_device *dev, u32 handle,
			      __be16 proto, struct tc_to_netdev *tc)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (TC_H_MAJ(handle) != TC_H_MAJ(TC_H_INGRESS))
		goto mqprio;

	switch (tc->type) {
	case TC_SETUP_CLSFLOWER:
		switch (tc->cls_flower->command) {
		case TC_CLSFLOWER_REPLACE:
			return mlx5e_configure_flower(priv, proto, tc->cls_flower);
		case TC_CLSFLOWER_DESTROY:
			return mlx5e_delete_flower(priv, tc->cls_flower);
		case TC_CLSFLOWER_STATS:
			return mlx5e_stats_flower(priv, tc->cls_flower);
		}
	default:
		return -EOPNOTSUPP;
	}

mqprio:
	if (tc->type != TC_SETUP_MQPRIO)
		return -EINVAL;

	return mlx5e_setup_tc(dev, tc->tc);
}

struct rtnl_link_stats64 *
mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_sw_stats *sstats = &priv->stats.sw;
	struct mlx5e_vport_stats *vstats = &priv->stats.vport;
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;

	stats->rx_packets = sstats->rx_packets;
	stats->rx_bytes   = sstats->rx_bytes;
	stats->tx_packets = sstats->tx_packets;
	stats->tx_bytes   = sstats->tx_bytes;

	stats->rx_dropped = priv->stats.qcnt.rx_out_of_buffer;
	stats->tx_dropped = sstats->tx_queue_dropped;

	stats->rx_length_errors =
		PPORT_802_3_GET(pstats, a_in_range_length_errors) +
		PPORT_802_3_GET(pstats, a_out_of_range_length_field) +
		PPORT_802_3_GET(pstats, a_frame_too_long_errors);
	stats->rx_crc_errors =
		PPORT_802_3_GET(pstats, a_frame_check_sequence_errors);
	stats->rx_frame_errors = PPORT_802_3_GET(pstats, a_alignment_errors);
	stats->tx_aborted_errors = PPORT_2863_GET(pstats, if_out_discards);
	stats->tx_carrier_errors =
		PPORT_802_3_GET(pstats, a_symbol_error_during_carrier);
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
			   stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_carrier_errors;

	/* vport multicast also counts packets that are dropped due to steering
	 * or rx out of buffer
	 */
	stats->multicast =
		VPORT_COUNTER_GET(vstats, received_eth_multicast.packets);

	return stats;
}

static void mlx5e_set_rx_mode(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	queue_work(priv->wq, &priv->set_rx_mode_work);
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

	queue_work(priv->wq, &priv->set_rx_mode_work);

	return 0;
}

#define MLX5E_SET_FEATURE(netdev, feature, enable)	\
	do {						\
		if (enable)				\
			netdev->features |= feature;	\
		else					\
			netdev->features &= ~feature;	\
	} while (0)

typedef int (*mlx5e_feature_handler)(struct net_device *netdev, bool enable);

static int set_feature_lro(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	bool was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	int err;

	mutex_lock(&priv->state_lock);

	if (was_opened && (priv->params.rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST))
		mlx5e_close_locked(priv->netdev);

	priv->params.lro_en = enable;
	err = mlx5e_modify_tirs_lro(priv);
	if (err) {
		netdev_err(netdev, "lro modify failed, %d\n", err);
		priv->params.lro_en = !enable;
	}

	if (was_opened && (priv->params.rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST))
		mlx5e_open_locked(priv->netdev);

	mutex_unlock(&priv->state_lock);

	return err;
}

static int set_feature_vlan_filter(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (enable)
		mlx5e_enable_vlan_filter(priv);
	else
		mlx5e_disable_vlan_filter(priv);

	return 0;
}

static int set_feature_tc_num_filters(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (!enable && mlx5e_tc_num_filters(priv)) {
		netdev_err(netdev,
			   "Active offloaded tc filters, can't turn hw_tc_offload off\n");
		return -EINVAL;
	}

	return 0;
}

static int set_feature_rx_all(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_set_port_fcs(mdev, !enable);
}

static int set_feature_rx_vlan(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mutex_lock(&priv->state_lock);

	priv->params.vlan_strip_disable = !enable;
	err = mlx5e_modify_rqs_vsd(priv, !enable);
	if (err)
		priv->params.vlan_strip_disable = enable;

	mutex_unlock(&priv->state_lock);

	return err;
}

#ifdef CONFIG_RFS_ACCEL
static int set_feature_arfs(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	if (enable)
		err = mlx5e_arfs_enable(priv);
	else
		err = mlx5e_arfs_disable(priv);

	return err;
}
#endif

static int mlx5e_handle_feature(struct net_device *netdev,
				netdev_features_t wanted_features,
				netdev_features_t feature,
				mlx5e_feature_handler feature_handler)
{
	netdev_features_t changes = wanted_features ^ netdev->features;
	bool enable = !!(wanted_features & feature);
	int err;

	if (!(changes & feature))
		return 0;

	err = feature_handler(netdev, enable);
	if (err) {
		netdev_err(netdev, "%s feature 0x%llx failed err %d\n",
			   enable ? "Enable" : "Disable", feature, err);
		return err;
	}

	MLX5E_SET_FEATURE(netdev, feature, enable);
	return 0;
}

static int mlx5e_set_features(struct net_device *netdev,
			      netdev_features_t features)
{
	int err;

	err  = mlx5e_handle_feature(netdev, features, NETIF_F_LRO,
				    set_feature_lro);
	err |= mlx5e_handle_feature(netdev, features,
				    NETIF_F_HW_VLAN_CTAG_FILTER,
				    set_feature_vlan_filter);
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_HW_TC,
				    set_feature_tc_num_filters);
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_RXALL,
				    set_feature_rx_all);
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_HW_VLAN_CTAG_RX,
				    set_feature_rx_vlan);
#ifdef CONFIG_RFS_ACCEL
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_NTUPLE,
				    set_feature_arfs);
#endif

	return err ? -EINVAL : 0;
}

#define MXL5_HW_MIN_MTU 64
#define MXL5E_MIN_MTU (MXL5_HW_MIN_MTU + ETH_FCS_LEN)

static int mlx5e_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool was_opened;
	u16 max_mtu;
	u16 min_mtu;
	int err = 0;
	bool reset;

	mlx5_query_port_max_mtu(mdev, &max_mtu, 1);

	max_mtu = MLX5E_HW2SW_MTU(max_mtu);
	min_mtu = MLX5E_HW2SW_MTU(MXL5E_MIN_MTU);

	if (new_mtu > max_mtu || new_mtu < min_mtu) {
		netdev_err(netdev,
			   "%s: Bad MTU (%d), valid range is: [%d..%d]\n",
			   __func__, new_mtu, min_mtu, max_mtu);
		return -EINVAL;
	}

	mutex_lock(&priv->state_lock);

	reset = !priv->params.lro_en &&
		(priv->params.rq_wq_type !=
		 MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ);

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (was_opened && reset)
		mlx5e_close_locked(netdev);

	netdev->mtu = new_mtu;
	mlx5e_set_dev_port_mtu(netdev);

	if (was_opened && reset)
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

static int mlx5e_set_vf_spoofchk(struct net_device *dev, int vf, bool setting)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_spoofchk(mdev->priv.eswitch, vf + 1, setting);
}

static int mlx5e_set_vf_trust(struct net_device *dev, int vf, bool setting)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_trust(mdev->priv.eswitch, vf + 1, setting);
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

static void mlx5e_add_vxlan_port(struct net_device *netdev,
				 struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	if (!mlx5e_vxlan_allowed(priv->mdev))
		return;

	mlx5e_vxlan_queue_work(priv, ti->sa_family, be16_to_cpu(ti->port), 1);
}

static void mlx5e_del_vxlan_port(struct net_device *netdev,
				 struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	if (!mlx5e_vxlan_allowed(priv->mdev))
		return;

	mlx5e_vxlan_queue_work(priv, ti->sa_family, be16_to_cpu(ti->port), 0);
}

static netdev_features_t mlx5e_vxlan_features_check(struct mlx5e_priv *priv,
						    struct sk_buff *skb,
						    netdev_features_t features)
{
	struct udphdr *udph;
	u16 proto;
	u16 port = 0;

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		proto = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		proto = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		goto out;
	}

	if (proto == IPPROTO_UDP) {
		udph = udp_hdr(skb);
		port = be16_to_cpu(udph->dest);
	}

	/* Verify if UDP port is being offloaded by HW */
	if (port && mlx5e_vxlan_lookup_port(priv, port))
		return features;

out:
	/* Disable CSUM and GSO if the udp dport is not offloaded by HW */
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

static netdev_features_t mlx5e_features_check(struct sk_buff *skb,
					      struct net_device *netdev,
					      netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	features = vlan_features_check(skb, features);
	features = vxlan_features_check(skb, features);

	/* Validate if the tunneled packet is being offloaded by HW */
	if (skb->encapsulation &&
	    (features & NETIF_F_CSUM_MASK || features & NETIF_F_GSO_MASK))
		return mlx5e_vxlan_features_check(priv, skb, features);

	return features;
}

static void mlx5e_tx_timeout(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	bool sched_work = false;
	int i;

	netdev_err(dev, "TX timeout detected\n");

	for (i = 0; i < priv->params.num_channels * priv->params.num_tc; i++) {
		struct mlx5e_sq *sq = priv->txq_to_sq_map[i];

		if (!netif_xmit_stopped(netdev_get_tx_queue(dev, i)))
			continue;
		sched_work = true;
		set_bit(MLX5E_SQ_STATE_TX_TIMEOUT, &sq->state);
		netdev_err(dev, "TX timeout on queue: %d, SQ: 0x%x, CQ: 0x%x, SQ Cons: 0x%x SQ Prod: 0x%x\n",
			   i, sq->sqn, sq->cq.mcq.cqn, sq->cc, sq->pc);
	}

	if (sched_work && test_bit(MLX5E_STATE_OPENED, &priv->state))
		schedule_work(&priv->tx_timeout_work);
}

static const struct net_device_ops mlx5e_netdev_ops_basic = {
	.ndo_open                = mlx5e_open,
	.ndo_stop                = mlx5e_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_setup_tc            = mlx5e_ndo_setup_tc,
	.ndo_select_queue        = mlx5e_select_queue,
	.ndo_get_stats64         = mlx5e_get_stats,
	.ndo_set_rx_mode         = mlx5e_set_rx_mode,
	.ndo_set_mac_address     = mlx5e_set_mac,
	.ndo_vlan_rx_add_vid     = mlx5e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid    = mlx5e_vlan_rx_kill_vid,
	.ndo_set_features        = mlx5e_set_features,
	.ndo_change_mtu          = mlx5e_change_mtu,
	.ndo_do_ioctl            = mlx5e_ioctl,
	.ndo_set_tx_maxrate      = mlx5e_set_tx_maxrate,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	 = mlx5e_rx_flow_steer,
#endif
	.ndo_tx_timeout          = mlx5e_tx_timeout,
};

static const struct net_device_ops mlx5e_netdev_ops_sriov = {
	.ndo_open                = mlx5e_open,
	.ndo_stop                = mlx5e_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_setup_tc            = mlx5e_ndo_setup_tc,
	.ndo_select_queue        = mlx5e_select_queue,
	.ndo_get_stats64         = mlx5e_get_stats,
	.ndo_set_rx_mode         = mlx5e_set_rx_mode,
	.ndo_set_mac_address     = mlx5e_set_mac,
	.ndo_vlan_rx_add_vid     = mlx5e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid    = mlx5e_vlan_rx_kill_vid,
	.ndo_set_features        = mlx5e_set_features,
	.ndo_change_mtu          = mlx5e_change_mtu,
	.ndo_do_ioctl            = mlx5e_ioctl,
	.ndo_udp_tunnel_add	 = mlx5e_add_vxlan_port,
	.ndo_udp_tunnel_del	 = mlx5e_del_vxlan_port,
	.ndo_set_tx_maxrate      = mlx5e_set_tx_maxrate,
	.ndo_features_check      = mlx5e_features_check,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	 = mlx5e_rx_flow_steer,
#endif
	.ndo_set_vf_mac          = mlx5e_set_vf_mac,
	.ndo_set_vf_vlan         = mlx5e_set_vf_vlan,
	.ndo_set_vf_spoofchk     = mlx5e_set_vf_spoofchk,
	.ndo_set_vf_trust        = mlx5e_set_vf_trust,
	.ndo_get_vf_config       = mlx5e_get_vf_config,
	.ndo_set_vf_link_state   = mlx5e_set_vf_link_state,
	.ndo_get_vf_stats        = mlx5e_get_vf_stats,
	.ndo_tx_timeout          = mlx5e_tx_timeout,
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
	if (!MLX5_CAP_GEN(mdev, cq_moderation))
		mlx5_core_warn(mdev, "CQ modiration is not supported\n");

	return 0;
}

u16 mlx5e_get_max_inline_cap(struct mlx5_core_dev *mdev)
{
	int bf_buf_size = (1 << MLX5_CAP_GEN(mdev, log_bf_reg_size)) / 2;

	return bf_buf_size -
	       sizeof(struct mlx5e_tx_wqe) +
	       2 /*sizeof(mlx5e_tx_wqe.inline_hdr_start)*/;
}

#ifdef CONFIG_MLX5_CORE_EN_DCB
static void mlx5e_ets_init(struct mlx5e_priv *priv)
{
	int i;

	priv->params.ets.ets_cap = mlx5_max_tc(priv->mdev) + 1;
	for (i = 0; i < priv->params.ets.ets_cap; i++) {
		priv->params.ets.tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
		priv->params.ets.tc_tsa[i] = IEEE_8021QAZ_TSA_VENDOR;
		priv->params.ets.prio_tc[i] = i;
	}

	/* tclass[prio=0]=1, tclass[prio=1]=0, tclass[prio=i]=i (for i>1) */
	priv->params.ets.prio_tc[0] = 1;
	priv->params.ets.prio_tc[1] = 0;
}
#endif

void mlx5e_build_default_indir_rqt(struct mlx5_core_dev *mdev,
				   u32 *indirection_rqt, int len,
				   int num_channels)
{
	int node = mdev->priv.numa_node;
	int node_num_of_cores;
	int i;

	if (node == -1)
		node = first_online_node;

	node_num_of_cores = cpumask_weight(cpumask_of_node(node));

	if (node_num_of_cores)
		num_channels = min_t(int, num_channels, node_num_of_cores);

	for (i = 0; i < len; i++)
		indirection_rqt[i] = i % num_channels;
}

static bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_GEN(mdev, striding_rq) &&
		MLX5_CAP_GEN(mdev, umr_ptr_rlky) &&
		MLX5_CAP_ETH(mdev, reg_umr_sq);
}

static int mlx5e_get_pci_bw(struct mlx5_core_dev *mdev, u32 *pci_bw)
{
	enum pcie_link_width width;
	enum pci_bus_speed speed;
	int err = 0;

	err = pcie_get_minimum_link(mdev->pdev, &speed, &width);
	if (err)
		return err;

	if (speed == PCI_SPEED_UNKNOWN || width == PCIE_LNK_WIDTH_UNKNOWN)
		return -EINVAL;

	switch (speed) {
	case PCIE_SPEED_2_5GT:
		*pci_bw = 2500 * width;
		break;
	case PCIE_SPEED_5_0GT:
		*pci_bw = 5000 * width;
		break;
	case PCIE_SPEED_8_0GT:
		*pci_bw = 8000 * width;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static bool cqe_compress_heuristic(u32 link_speed, u32 pci_bw)
{
	return (link_speed && pci_bw &&
		(pci_bw < 40000) && (pci_bw < link_speed));
}

void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode)
{
	params->rx_cq_period_mode = cq_period_mode;

	params->rx_cq_moderation.pkts =
		MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS;
	params->rx_cq_moderation.usec =
			MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC;

	if (cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE)
		params->rx_cq_moderation.usec =
			MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE;
}

static void mlx5e_query_min_inline(struct mlx5_core_dev *mdev,
				   u8 *min_inline_mode)
{
	switch (MLX5_CAP_ETH(mdev, wqe_inline_mode)) {
	case MLX5E_INLINE_MODE_L2:
		*min_inline_mode = MLX5_INLINE_MODE_L2;
		break;
	case MLX5E_INLINE_MODE_VPORT_CONTEXT:
		mlx5_query_nic_vport_min_inline(mdev,
						min_inline_mode);
		break;
	case MLX5_INLINE_MODE_NOT_REQUIRED:
		*min_inline_mode = MLX5_INLINE_MODE_NONE;
		break;
	}
}

static void mlx5e_build_nic_netdev_priv(struct mlx5_core_dev *mdev,
					struct net_device *netdev,
					const struct mlx5e_profile *profile,
					void *ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	u32 link_speed = 0;
	u32 pci_bw = 0;
	u8 cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
					 MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
					 MLX5_CQ_PERIOD_MODE_START_FROM_EQE;

	priv->params.log_sq_size           =
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	priv->params.rq_wq_type = mlx5e_check_fragmented_striding_rq_cap(mdev) ?
		MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ :
		MLX5_WQ_TYPE_LINKED_LIST;

	/* set CQE compression */
	priv->params.rx_cqe_compress_admin = false;
	if (MLX5_CAP_GEN(mdev, cqe_compression) &&
	    MLX5_CAP_GEN(mdev, vport_group_manager)) {
		mlx5e_get_max_linkspeed(mdev, &link_speed);
		mlx5e_get_pci_bw(mdev, &pci_bw);
		mlx5_core_dbg(mdev, "Max link speed = %d, PCI BW = %d\n",
			      link_speed, pci_bw);
		priv->params.rx_cqe_compress_admin =
			cqe_compress_heuristic(link_speed, pci_bw);
	}

	priv->params.rx_cqe_compress = priv->params.rx_cqe_compress_admin;

	switch (priv->params.rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		priv->params.log_rq_size = MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE_MPW;
		priv->params.mpwqe_log_stride_sz =
			priv->params.rx_cqe_compress ?
			MLX5_MPWRQ_LOG_STRIDE_SIZE_CQE_COMPRESS :
			MLX5_MPWRQ_LOG_STRIDE_SIZE;
		priv->params.mpwqe_log_num_strides = MLX5_MPWRQ_LOG_WQE_SZ -
			priv->params.mpwqe_log_stride_sz;
		priv->params.lro_en = true;
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		priv->params.log_rq_size = MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE;
	}

	mlx5_core_info(mdev,
		       "MLX5E: StrdRq(%d) RqSz(%ld) StrdSz(%ld) RxCqeCmprss(%d)\n",
		       priv->params.rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ,
		       BIT(priv->params.log_rq_size),
		       BIT(priv->params.mpwqe_log_stride_sz),
		       priv->params.rx_cqe_compress_admin);

	priv->params.min_rx_wqes = mlx5_min_rx_wqes(priv->params.rq_wq_type,
					    BIT(priv->params.log_rq_size));

	priv->params.rx_am_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(&priv->params, cq_period_mode);

	priv->params.tx_cq_moderation.usec =
		MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC;
	priv->params.tx_cq_moderation.pkts =
		MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS;
	priv->params.tx_max_inline         = mlx5e_get_max_inline_cap(mdev);
	mlx5e_query_min_inline(mdev, &priv->params.tx_min_inline_mode);
	priv->params.num_tc                = 1;
	priv->params.rss_hfunc             = ETH_RSS_HASH_XOR;

	netdev_rss_key_fill(priv->params.toeplitz_hash_key,
			    sizeof(priv->params.toeplitz_hash_key));

	mlx5e_build_default_indir_rqt(mdev, priv->params.indirection_rqt,
				      MLX5E_INDIR_RQT_SIZE, profile->max_nch(mdev));

	priv->params.lro_wqe_sz            =
		MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ;

	/* Initialize pflags */
	MLX5E_SET_PRIV_FLAG(priv, MLX5E_PFLAG_RX_CQE_BASED_MODER,
			    priv->params.rx_cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE);

	priv->mdev                         = mdev;
	priv->netdev                       = netdev;
	priv->params.num_channels          = profile->max_nch(mdev);
	priv->profile                      = profile;
	priv->ppriv                        = ppriv;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	mlx5e_ets_init(priv);
#endif

	mutex_init(&priv->state_lock);

	INIT_WORK(&priv->update_carrier_work, mlx5e_update_carrier_work);
	INIT_WORK(&priv->set_rx_mode_work, mlx5e_set_rx_mode_work);
	INIT_WORK(&priv->tx_timeout_work, mlx5e_tx_timeout_work);
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

static const struct switchdev_ops mlx5e_switchdev_ops = {
	.switchdev_port_attr_get	= mlx5e_attr_get,
};

static void mlx5e_build_nic_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool fcs_supported;
	bool fcs_enabled;

	SET_NETDEV_DEV(netdev, &mdev->pdev->dev);

	if (MLX5_CAP_GEN(mdev, vport_group_manager)) {
		netdev->netdev_ops = &mlx5e_netdev_ops_sriov;
#ifdef CONFIG_MLX5_CORE_EN_DCB
		netdev->dcbnl_ops = &mlx5e_dcbnl_ops;
#endif
	} else {
		netdev->netdev_ops = &mlx5e_netdev_ops_basic;
	}

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

	if (mlx5e_vxlan_allowed(mdev)) {
		netdev->hw_features     |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM |
					   NETIF_F_GSO_PARTIAL;
		netdev->hw_enc_features |= NETIF_F_IP_CSUM;
		netdev->hw_enc_features |= NETIF_F_IPV6_CSUM;
		netdev->hw_enc_features |= NETIF_F_TSO;
		netdev->hw_enc_features |= NETIF_F_TSO6;
		netdev->hw_enc_features |= NETIF_F_GSO_UDP_TUNNEL;
		netdev->hw_enc_features |= NETIF_F_GSO_UDP_TUNNEL_CSUM |
					   NETIF_F_GSO_PARTIAL;
		netdev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM;
	}

	mlx5_query_port_fcs(mdev, &fcs_supported, &fcs_enabled);

	if (fcs_supported)
		netdev->hw_features |= NETIF_F_RXALL;

	netdev->features          = netdev->hw_features;
	if (!priv->params.lro_en)
		netdev->features  &= ~NETIF_F_LRO;

	if (fcs_enabled)
		netdev->features  &= ~NETIF_F_RXALL;

#define FT_CAP(f) MLX5_CAP_FLOWTABLE(mdev, flow_table_properties_nic_receive.f)
	if (FT_CAP(flow_modify_en) &&
	    FT_CAP(modify_root) &&
	    FT_CAP(identified_miss_table_mode) &&
	    FT_CAP(flow_table_modify)) {
		netdev->hw_features      |= NETIF_F_HW_TC;
#ifdef CONFIG_RFS_ACCEL
		netdev->hw_features	 |= NETIF_F_NTUPLE;
#endif
	}

	netdev->features         |= NETIF_F_HIGHDMA;

	netdev->priv_flags       |= IFF_UNICAST_FLT;

	mlx5e_set_netdev_dev_addr(netdev);

#ifdef CONFIG_NET_SWITCHDEV
	if (MLX5_CAP_GEN(mdev, vport_group_manager))
		netdev->switchdev_ops = &mlx5e_switchdev_ops;
#endif
}

static void mlx5e_create_q_counter(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5_core_alloc_q_counter(mdev, &priv->q_counter);
	if (err) {
		mlx5_core_warn(mdev, "alloc queue counter failed, %d\n", err);
		priv->q_counter = 0;
	}
}

static void mlx5e_destroy_q_counter(struct mlx5e_priv *priv)
{
	if (!priv->q_counter)
		return;

	mlx5_core_dealloc_q_counter(priv->mdev, priv->q_counter);
}

static int mlx5e_create_umr_mkey(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_create_mkey_mbox_in *in;
	struct mlx5_mkey_seg *mkc;
	int inlen = sizeof(*in);
	u64 npages =
		priv->profile->max_nch(mdev) * MLX5_CHANNEL_MAX_NUM_MTTS;
	int err;

	in = mlx5_vzalloc(inlen);
	if (!in)
		return -ENOMEM;

	mkc = &in->seg;
	mkc->status = MLX5_MKEY_STATUS_FREE;
	mkc->flags = MLX5_PERM_UMR_EN |
		     MLX5_PERM_LOCAL_READ |
		     MLX5_PERM_LOCAL_WRITE |
		     MLX5_ACCESS_MODE_MTT;

	mkc->qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);
	mkc->flags_pd = cpu_to_be32(mdev->mlx5e_res.pdn);
	mkc->len = cpu_to_be64(npages << PAGE_SHIFT);
	mkc->xlt_oct_size = cpu_to_be32(mlx5e_get_mtt_octw(npages));
	mkc->log2_page_size = PAGE_SHIFT;

	err = mlx5_core_create_mkey(mdev, &priv->umr_mkey, in, inlen, NULL,
				    NULL, NULL);

	kvfree(in);

	return err;
}

static void mlx5e_nic_init(struct mlx5_core_dev *mdev,
			   struct net_device *netdev,
			   const struct mlx5e_profile *profile,
			   void *ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_build_nic_netdev_priv(mdev, netdev, profile, ppriv);
	mlx5e_build_nic_netdev(netdev);
	mlx5e_vxlan_init(priv);
}

static void mlx5e_nic_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	mlx5e_vxlan_cleanup(priv);

	if (MLX5_CAP_GEN(mdev, vport_group_manager))
		mlx5_eswitch_unregister_vport_rep(esw, 0);
}

static int mlx5e_init_nic_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;
	int i;

	err = mlx5e_create_indirect_rqts(priv);
	if (err) {
		mlx5_core_warn(mdev, "create indirect rqts failed, %d\n", err);
		return err;
	}

	err = mlx5e_create_direct_rqts(priv);
	if (err) {
		mlx5_core_warn(mdev, "create direct rqts failed, %d\n", err);
		goto err_destroy_indirect_rqts;
	}

	err = mlx5e_create_indirect_tirs(priv);
	if (err) {
		mlx5_core_warn(mdev, "create indirect tirs failed, %d\n", err);
		goto err_destroy_direct_rqts;
	}

	err = mlx5e_create_direct_tirs(priv);
	if (err) {
		mlx5_core_warn(mdev, "create direct tirs failed, %d\n", err);
		goto err_destroy_indirect_tirs;
	}

	err = mlx5e_create_flow_steering(priv);
	if (err) {
		mlx5_core_warn(mdev, "create flow steering failed, %d\n", err);
		goto err_destroy_direct_tirs;
	}

	err = mlx5e_tc_init(priv);
	if (err)
		goto err_destroy_flow_steering;

	return 0;

err_destroy_flow_steering:
	mlx5e_destroy_flow_steering(priv);
err_destroy_direct_tirs:
	mlx5e_destroy_direct_tirs(priv);
err_destroy_indirect_tirs:
	mlx5e_destroy_indirect_tirs(priv);
err_destroy_direct_rqts:
	for (i = 0; i < priv->profile->max_nch(mdev); i++)
		mlx5e_destroy_rqt(priv, &priv->direct_tir[i].rqt);
err_destroy_indirect_rqts:
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
	return err;
}

static void mlx5e_cleanup_nic_rx(struct mlx5e_priv *priv)
{
	int i;

	mlx5e_tc_cleanup(priv);
	mlx5e_destroy_flow_steering(priv);
	mlx5e_destroy_direct_tirs(priv);
	mlx5e_destroy_indirect_tirs(priv);
	for (i = 0; i < priv->profile->max_nch(priv->mdev); i++)
		mlx5e_destroy_rqt(priv, &priv->direct_tir[i].rqt);
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
}

static int mlx5e_init_nic_tx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tises failed, %d\n", err);
		return err;
	}

#ifdef CONFIG_MLX5_CORE_EN_DCB
	mlx5e_dcbnl_ieee_setets_core(priv, &priv->params.ets);
#endif
	return 0;
}

static void mlx5e_nic_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	struct mlx5_eswitch_rep rep;

	if (mlx5e_vxlan_allowed(mdev)) {
		rtnl_lock();
		udp_tunnel_get_rx_info(netdev);
		rtnl_unlock();
	}

	mlx5e_enable_async_events(priv);
	queue_work(priv->wq, &priv->set_rx_mode_work);

	if (MLX5_CAP_GEN(mdev, vport_group_manager)) {
		rep.load = mlx5e_nic_rep_load;
		rep.unload = mlx5e_nic_rep_unload;
		rep.vport = 0;
		rep.priv_data = priv;
		mlx5_eswitch_register_vport_rep(esw, &rep);
	}
}

static void mlx5e_nic_disable(struct mlx5e_priv *priv)
{
	queue_work(priv->wq, &priv->set_rx_mode_work);
	mlx5e_disable_async_events(priv);
}

static const struct mlx5e_profile mlx5e_nic_profile = {
	.init		   = mlx5e_nic_init,
	.cleanup	   = mlx5e_nic_cleanup,
	.init_rx	   = mlx5e_init_nic_rx,
	.cleanup_rx	   = mlx5e_cleanup_nic_rx,
	.init_tx	   = mlx5e_init_nic_tx,
	.cleanup_tx	   = mlx5e_cleanup_nic_tx,
	.enable		   = mlx5e_nic_enable,
	.disable	   = mlx5e_nic_disable,
	.update_stats	   = mlx5e_update_stats,
	.max_nch	   = mlx5e_get_max_num_channels,
	.max_tc		   = MLX5E_MAX_NUM_TC,
};

void *mlx5e_create_netdev(struct mlx5_core_dev *mdev,
			  const struct mlx5e_profile *profile, void *ppriv)
{
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	int nch = profile->max_nch(mdev);
	int err;

	netdev = alloc_etherdev_mqs(sizeof(struct mlx5e_priv),
				    nch * profile->max_tc,
				    nch);
	if (!netdev) {
		mlx5_core_err(mdev, "alloc_etherdev_mqs() failed\n");
		return NULL;
	}

	profile->init(mdev, netdev, profile, ppriv);

	netif_carrier_off(netdev);

	priv = netdev_priv(netdev);

	priv->wq = create_singlethread_workqueue("mlx5e");
	if (!priv->wq)
		goto err_free_netdev;

	err = mlx5e_create_umr_mkey(priv);
	if (err) {
		mlx5_core_err(mdev, "create umr mkey failed, %d\n", err);
		goto err_destroy_wq;
	}

	err = profile->init_tx(priv);
	if (err)
		goto err_destroy_umr_mkey;

	err = mlx5e_open_drop_rq(priv);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_cleanup_tx;
	}

	err = profile->init_rx(priv);
	if (err)
		goto err_close_drop_rq;

	mlx5e_create_q_counter(priv);

	mlx5e_init_l2_addr(priv);

	mlx5e_set_dev_port_mtu(netdev);

	err = register_netdev(netdev);
	if (err) {
		mlx5_core_err(mdev, "register_netdev failed, %d\n", err);
		goto err_dealloc_q_counters;
	}

	if (profile->enable)
		profile->enable(priv);

	return priv;

err_dealloc_q_counters:
	mlx5e_destroy_q_counter(priv);
	profile->cleanup_rx(priv);

err_close_drop_rq:
	mlx5e_close_drop_rq(priv);

err_cleanup_tx:
	profile->cleanup_tx(priv);

err_destroy_umr_mkey:
	mlx5_core_destroy_mkey(mdev, &priv->umr_mkey);

err_destroy_wq:
	destroy_workqueue(priv->wq);

err_free_netdev:
	free_netdev(netdev);

	return NULL;
}

static void mlx5e_register_vport_rep(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int total_vfs = MLX5_TOTAL_VPORTS(mdev);
	int vport;

	if (!MLX5_CAP_GEN(mdev, vport_group_manager))
		return;

	for (vport = 1; vport < total_vfs; vport++) {
		struct mlx5_eswitch_rep rep;

		rep.load = mlx5e_vport_rep_load;
		rep.unload = mlx5e_vport_rep_unload;
		rep.vport = vport;
		mlx5_eswitch_register_vport_rep(esw, &rep);
	}
}

static void *mlx5e_add(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	void *ppriv = NULL;
	void *ret;

	if (mlx5e_check_required_hca_cap(mdev))
		return NULL;

	if (mlx5e_create_mdev_resources(mdev))
		return NULL;

	mlx5e_register_vport_rep(mdev);

	if (MLX5_CAP_GEN(mdev, vport_group_manager))
		ppriv = &esw->offloads.vport_reps[0];

	ret = mlx5e_create_netdev(mdev, &mlx5e_nic_profile, ppriv);
	if (!ret) {
		mlx5e_destroy_mdev_resources(mdev);
		return NULL;
	}
	return ret;
}

void mlx5e_destroy_netdev(struct mlx5_core_dev *mdev, struct mlx5e_priv *priv)
{
	const struct mlx5e_profile *profile = priv->profile;
	struct net_device *netdev = priv->netdev;

	set_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (profile->disable)
		profile->disable(priv);

	flush_workqueue(priv->wq);
	if (test_bit(MLX5_INTERFACE_STATE_SHUTDOWN, &mdev->intf_state)) {
		netif_device_detach(netdev);
		mlx5e_close(netdev);
	} else {
		unregister_netdev(netdev);
	}

	mlx5e_destroy_q_counter(priv);
	profile->cleanup_rx(priv);
	mlx5e_close_drop_rq(priv);
	profile->cleanup_tx(priv);
	mlx5_core_destroy_mkey(priv->mdev, &priv->umr_mkey);
	cancel_delayed_work_sync(&priv->update_stats_work);
	destroy_workqueue(priv->wq);
	if (profile->cleanup)
		profile->cleanup(priv);

	if (!test_bit(MLX5_INTERFACE_STATE_SHUTDOWN, &mdev->intf_state))
		free_netdev(netdev);
}

static void mlx5e_remove(struct mlx5_core_dev *mdev, void *vpriv)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int total_vfs = MLX5_TOTAL_VPORTS(mdev);
	struct mlx5e_priv *priv = vpriv;
	int vport;

	mlx5e_destroy_netdev(mdev, priv);

	for (vport = 1; vport < total_vfs; vport++)
		mlx5_eswitch_unregister_vport_rep(esw, vport);

	mlx5e_destroy_mdev_resources(mdev);
}

static void *mlx5e_get_netdev(void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;

	return priv->netdev;
}

static struct mlx5_interface mlx5e_interface = {
	.add       = mlx5e_add,
	.remove    = mlx5e_remove,
	.event     = mlx5e_async_event,
	.protocol  = MLX5_INTERFACE_PROTOCOL_ETH,
	.get_dev   = mlx5e_get_netdev,
};

void mlx5e_init(void)
{
	mlx5e_build_ptys2ethtool_map();
	mlx5_register_interface(&mlx5e_interface);
}

void mlx5e_cleanup(void)
{
	mlx5_unregister_interface(&mlx5e_interface);
}

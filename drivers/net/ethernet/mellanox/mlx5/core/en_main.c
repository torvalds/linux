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
#include <linux/bpf.h>
#include "eswitch.h"
#include "en.h"
#include "en_tc.h"
#include "en_rep.h"
#include "en_accel/ipsec.h"
#include "en_accel/ipsec_rxtx.h"
#include "accel/ipsec.h"
#include "vxlan.h"

struct mlx5e_rq_param {
	u32			rqc[MLX5_ST_SZ_DW(rqc)];
	struct mlx5_wq_param	wq;
};

struct mlx5e_sq_param {
	u32                        sqc[MLX5_ST_SZ_DW(sqc)];
	struct mlx5_wq_param       wq;
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
	struct mlx5e_sq_param      xdp_sq;
	struct mlx5e_sq_param      icosq;
	struct mlx5e_cq_param      rx_cq;
	struct mlx5e_cq_param      tx_cq;
	struct mlx5e_cq_param      icosq_cq;
};

static int mlx5e_get_node(struct mlx5e_priv *priv, int ix)
{
	return pci_irq_get_node(priv->mdev->pdev, MLX5_EQ_VEC_COMP_BASE + ix);
}

static bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_GEN(mdev, striding_rq) &&
		MLX5_CAP_GEN(mdev, umr_ptr_rlky) &&
		MLX5_CAP_ETH(mdev, reg_umr_sq);
}

void mlx5e_set_rq_type_params(struct mlx5_core_dev *mdev,
			      struct mlx5e_params *params, u8 rq_type)
{
	params->rq_wq_type = rq_type;
	params->lro_wqe_sz = MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ;
	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		params->log_rq_size = is_kdump_kernel() ?
			MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW :
			MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE_MPW;
		params->mpwqe_log_stride_sz =
			MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS) ?
			MLX5_MPWRQ_CQE_CMPRS_LOG_STRIDE_SZ(mdev) :
			MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev);
		params->mpwqe_log_num_strides = MLX5_MPWRQ_LOG_WQE_SZ -
			params->mpwqe_log_stride_sz;
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		params->log_rq_size = is_kdump_kernel() ?
			MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE :
			MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE;
		params->rq_headroom = params->xdp_prog ?
			XDP_PACKET_HEADROOM : MLX5_RX_HEADROOM;
		params->rq_headroom += NET_IP_ALIGN;

		/* Extra room needed for build_skb */
		params->lro_wqe_sz -= params->rq_headroom +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	}

	mlx5_core_info(mdev, "MLX5E: StrdRq(%d) RqSz(%ld) StrdSz(%ld) RxCqeCmprss(%d)\n",
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ,
		       BIT(params->log_rq_size),
		       BIT(params->mpwqe_log_stride_sz),
		       MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS));
}

static void mlx5e_set_rq_params(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	u8 rq_type = mlx5e_check_fragmented_striding_rq_cap(mdev) &&
		    !params->xdp_prog && !MLX5_IPSEC_DEV(mdev) ?
		    MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ :
		    MLX5_WQ_TYPE_LINKED_LIST;
	mlx5e_set_rq_type_params(mdev, params, rq_type);
}

static void mlx5e_update_carrier(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 port_state;

	port_state = mlx5_query_vport_state(mdev,
					    MLX5_QUERY_VPORT_STATE_IN_OP_MOD_VNIC_VPORT,
					    0);

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
		if (priv->profile->update_carrier)
			priv->profile->update_carrier(priv);
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
	struct mlx5e_sw_stats temp, *s = &temp;
	struct mlx5e_rq_stats *rq_stats;
	struct mlx5e_sq_stats *sq_stats;
	int i, j;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		rq_stats = &c->rq.stats;

		s->rx_packets	+= rq_stats->packets;
		s->rx_bytes	+= rq_stats->bytes;
		s->rx_lro_packets += rq_stats->lro_packets;
		s->rx_lro_bytes	+= rq_stats->lro_bytes;
		s->rx_csum_none	+= rq_stats->csum_none;
		s->rx_csum_complete += rq_stats->csum_complete;
		s->rx_csum_unnecessary += rq_stats->csum_unnecessary;
		s->rx_csum_unnecessary_inner += rq_stats->csum_unnecessary_inner;
		s->rx_xdp_drop += rq_stats->xdp_drop;
		s->rx_xdp_tx += rq_stats->xdp_tx;
		s->rx_xdp_tx_full += rq_stats->xdp_tx_full;
		s->rx_wqe_err   += rq_stats->wqe_err;
		s->rx_mpwqe_filler += rq_stats->mpwqe_filler;
		s->rx_buff_alloc_err += rq_stats->buff_alloc_err;
		s->rx_cqe_compress_blks += rq_stats->cqe_compress_blks;
		s->rx_cqe_compress_pkts += rq_stats->cqe_compress_pkts;
		s->rx_page_reuse  += rq_stats->page_reuse;
		s->rx_cache_reuse += rq_stats->cache_reuse;
		s->rx_cache_full  += rq_stats->cache_full;
		s->rx_cache_empty += rq_stats->cache_empty;
		s->rx_cache_busy  += rq_stats->cache_busy;
		s->rx_cache_waive += rq_stats->cache_waive;

		for (j = 0; j < priv->channels.params.num_tc; j++) {
			sq_stats = &c->sq[j].stats;

			s->tx_packets		+= sq_stats->packets;
			s->tx_bytes		+= sq_stats->bytes;
			s->tx_tso_packets	+= sq_stats->tso_packets;
			s->tx_tso_bytes		+= sq_stats->tso_bytes;
			s->tx_tso_inner_packets	+= sq_stats->tso_inner_packets;
			s->tx_tso_inner_bytes	+= sq_stats->tso_inner_bytes;
			s->tx_queue_stopped	+= sq_stats->stopped;
			s->tx_queue_wake	+= sq_stats->wake;
			s->tx_queue_dropped	+= sq_stats->dropped;
			s->tx_xmit_more		+= sq_stats->xmit_more;
			s->tx_csum_partial_inner += sq_stats->csum_partial_inner;
			s->tx_csum_none		+= sq_stats->csum_none;
			s->tx_csum_partial	+= sq_stats->csum_partial;
		}
	}

	s->link_down_events_phy = MLX5_GET(ppcnt_reg,
				priv->stats.pport.phy_counters,
				counter_set.phys_layer_cntrs.link_down_events);
	memcpy(&priv->stats.sw, s, sizeof(*s));
}

static void mlx5e_update_vport_counters(struct mlx5e_priv *priv)
{
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	u32 *out = (u32 *)priv->stats.vport.query_vport_out;
	u32 in[MLX5_ST_SZ_DW(query_vport_counter_in)] = {0};
	struct mlx5_core_dev *mdev = priv->mdev;

	MLX5_SET(query_vport_counter_in, in, opcode,
		 MLX5_CMD_OP_QUERY_VPORT_COUNTER);
	MLX5_SET(query_vport_counter_in, in, op_mod, 0);
	MLX5_SET(query_vport_counter_in, in, other_vport, 0);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
}

static void mlx5e_update_pport_counters(struct mlx5e_priv *priv, bool full)
{
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {0};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);
	int prio;
	void *out;

	MLX5_SET(ppcnt_reg, in, local_port, 1);

	out = pstats->IEEE_802_3_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_IEEE_802_3_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	if (!full)
		return;

	out = pstats->RFC_2863_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2863_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	out = pstats->RFC_2819_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_RFC_2819_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	out = pstats->phy_counters;
	MLX5_SET(ppcnt_reg, in, grp, MLX5_PHYSICAL_LAYER_COUNTERS_GROUP);
	mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);

	if (MLX5_CAP_PCAM_FEATURE(mdev, ppcnt_statistical_group)) {
		out = pstats->phy_statistical_counters;
		MLX5_SET(ppcnt_reg, in, grp, MLX5_PHYSICAL_LAYER_STATISTICAL_GROUP);
		mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
	}

	if (MLX5_CAP_PCAM_FEATURE(mdev, rx_buffer_fullness_counters)) {
		out = pstats->eth_ext_counters;
		MLX5_SET(ppcnt_reg, in, grp, MLX5_ETHERNET_EXTENDED_COUNTERS_GROUP);
		mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPCNT, 0, 0);
	}

	MLX5_SET(ppcnt_reg, in, grp, MLX5_PER_PRIORITY_COUNTERS_GROUP);
	for (prio = 0; prio < NUM_PPORT_PRIO; prio++) {
		out = pstats->per_prio_counters[prio];
		MLX5_SET(ppcnt_reg, in, prio_tc, prio);
		mlx5_core_access_reg(mdev, in, sz, out, sz,
				     MLX5_REG_PPCNT, 0, 0);
	}
}

static void mlx5e_update_q_counter(struct mlx5e_priv *priv)
{
	struct mlx5e_qcounter_stats *qcnt = &priv->stats.qcnt;
	u32 out[MLX5_ST_SZ_DW(query_q_counter_out)];
	int err;

	if (!priv->q_counter)
		return;

	err = mlx5_core_query_q_counter(priv->mdev, priv->q_counter, 0, out, sizeof(out));
	if (err)
		return;

	qcnt->rx_out_of_buffer = MLX5_GET(query_q_counter_out, out, out_of_buffer);
}

static void mlx5e_update_pcie_counters(struct mlx5e_priv *priv)
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

void mlx5e_update_stats(struct mlx5e_priv *priv, bool full)
{
	if (full) {
		mlx5e_update_pcie_counters(priv);
		mlx5e_ipsec_update_stats(priv);
	}
	mlx5e_update_pport_counters(priv, full);
	mlx5e_update_vport_counters(priv);
	mlx5e_update_q_counter(priv);
	mlx5e_update_sw_counters(priv);
}

static void mlx5e_update_ndo_stats(struct mlx5e_priv *priv)
{
	mlx5e_update_stats(priv, false);
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
	struct ptp_clock_event ptp_event;
	struct mlx5_eqe *eqe = NULL;

	if (!test_bit(MLX5E_STATE_ASYNC_EVENTS_ENABLED, &priv->state))
		return;

	switch (event) {
	case MLX5_DEV_EVENT_PORT_UP:
	case MLX5_DEV_EVENT_PORT_DOWN:
		queue_work(priv->wq, &priv->update_carrier_work);
		break;
	case MLX5_DEV_EVENT_PPS:
		eqe = (struct mlx5_eqe *)param;
		ptp_event.index = eqe->data.pps.pin;
		ptp_event.timestamp =
			timecounter_cyc2time(&priv->tstamp.clock,
					     be64_to_cpu(eqe->data.pps.time_stamp));
		mlx5e_pps_event_handler(vpriv, &ptp_event);
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
	synchronize_irq(pci_irq_vector(priv->mdev->pdev, MLX5_EQ_VEC_ASYNC));
}

static inline int mlx5e_get_wqe_mtt_sz(void)
{
	/* UMR copies MTTs in units of MLX5_UMR_MTT_ALIGNMENT bytes.
	 * To avoid copying garbage after the mtt array, we allocate
	 * a little more.
	 */
	return ALIGN(MLX5_MPWRQ_PAGES_PER_WQE * sizeof(__be64),
		     MLX5_UMR_MTT_ALIGNMENT);
}

static inline void mlx5e_build_umr_wqe(struct mlx5e_rq *rq,
				       struct mlx5e_icosq *sq,
				       struct mlx5e_umr_wqe *wqe,
				       u16 ix)
{
	struct mlx5_wqe_ctrl_seg      *cseg = &wqe->ctrl;
	struct mlx5_wqe_umr_ctrl_seg *ucseg = &wqe->uctrl;
	struct mlx5_wqe_data_seg      *dseg = &wqe->data;
	struct mlx5e_mpw_info *wi = &rq->mpwqe.info[ix];
	u8 ds_cnt = DIV_ROUND_UP(sizeof(*wqe), MLX5_SEND_WQE_DS);
	u32 umr_wqe_mtt_offset = mlx5e_get_wqe_mtt_offset(rq, ix);

	cseg->qpn_ds    = cpu_to_be32((sq->sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
				      ds_cnt);
	cseg->fm_ce_se  = MLX5_WQE_CTRL_CQ_UPDATE;
	cseg->imm       = rq->mkey_be;

	ucseg->flags = MLX5_UMR_TRANSLATION_OFFSET_EN;
	ucseg->xlt_octowords =
		cpu_to_be16(MLX5_MTT_OCTW(MLX5_MPWRQ_PAGES_PER_WQE));
	ucseg->bsf_octowords =
		cpu_to_be16(MLX5_MTT_OCTW(umr_wqe_mtt_offset));
	ucseg->mkey_mask     = cpu_to_be64(MLX5_MKEY_MASK_FREE);

	dseg->lkey = sq->mkey_be;
	dseg->addr = cpu_to_be64(wi->umr.mtt_addr);
}

static int mlx5e_rq_alloc_mpwqe_info(struct mlx5e_rq *rq,
				     struct mlx5e_channel *c)
{
	int wq_sz = mlx5_wq_ll_get_size(&rq->wq);
	int mtt_sz = mlx5e_get_wqe_mtt_sz();
	int mtt_alloc = mtt_sz + MLX5_UMR_ALIGN - 1;
	int node = mlx5e_get_node(c->priv, c->ix);
	int i;

	rq->mpwqe.info = kzalloc_node(wq_sz * sizeof(*rq->mpwqe.info),
					GFP_KERNEL, node);
	if (!rq->mpwqe.info)
		goto err_out;

	/* We allocate more than mtt_sz as we will align the pointer */
	rq->mpwqe.mtt_no_align = kzalloc_node(mtt_alloc * wq_sz,
					GFP_KERNEL, node);
	if (unlikely(!rq->mpwqe.mtt_no_align))
		goto err_free_wqe_info;

	for (i = 0; i < wq_sz; i++) {
		struct mlx5e_mpw_info *wi = &rq->mpwqe.info[i];

		wi->umr.mtt = PTR_ALIGN(rq->mpwqe.mtt_no_align + i * mtt_alloc,
					MLX5_UMR_ALIGN);
		wi->umr.mtt_addr = dma_map_single(c->pdev, wi->umr.mtt, mtt_sz,
						  PCI_DMA_TODEVICE);
		if (unlikely(dma_mapping_error(c->pdev, wi->umr.mtt_addr)))
			goto err_unmap_mtts;

		mlx5e_build_umr_wqe(rq, &c->icosq, &wi->umr.wqe, i);
	}

	return 0;

err_unmap_mtts:
	while (--i >= 0) {
		struct mlx5e_mpw_info *wi = &rq->mpwqe.info[i];

		dma_unmap_single(c->pdev, wi->umr.mtt_addr, mtt_sz,
				 PCI_DMA_TODEVICE);
	}
	kfree(rq->mpwqe.mtt_no_align);
err_free_wqe_info:
	kfree(rq->mpwqe.info);

err_out:
	return -ENOMEM;
}

static void mlx5e_rq_free_mpwqe_info(struct mlx5e_rq *rq)
{
	int wq_sz = mlx5_wq_ll_get_size(&rq->wq);
	int mtt_sz = mlx5e_get_wqe_mtt_sz();
	int i;

	for (i = 0; i < wq_sz; i++) {
		struct mlx5e_mpw_info *wi = &rq->mpwqe.info[i];

		dma_unmap_single(rq->pdev, wi->umr.mtt_addr, mtt_sz,
				 PCI_DMA_TODEVICE);
	}
	kfree(rq->mpwqe.mtt_no_align);
	kfree(rq->mpwqe.info);
}

static int mlx5e_create_umr_mkey(struct mlx5_core_dev *mdev,
				 u64 npages, u8 page_shift,
				 struct mlx5_core_mkey *umr_mkey)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	void *mkc;
	u32 *in;
	int err;

	if (!MLX5E_VALID_NUM_MTTS(npages))
		return -EINVAL;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode, MLX5_MKC_ACCESS_MODE_MTT);

	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mdev->mlx5e_res.pdn);
	MLX5_SET64(mkc, mkc, len, npages << page_shift);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 MLX5_MTT_OCTW(npages));
	MLX5_SET(mkc, mkc, log_page_size, page_shift);

	err = mlx5_core_create_mkey(mdev, umr_mkey, in, inlen);

	kvfree(in);
	return err;
}

static int mlx5e_create_rq_umr_mkey(struct mlx5_core_dev *mdev, struct mlx5e_rq *rq)
{
	u64 num_mtts = MLX5E_REQUIRED_MTTS(mlx5_wq_ll_get_size(&rq->wq));

	return mlx5e_create_umr_mkey(mdev, num_mtts, PAGE_SHIFT, &rq->umr_mkey);
}

static int mlx5e_alloc_rq(struct mlx5e_channel *c,
			  struct mlx5e_params *params,
			  struct mlx5e_rq_param *rqp,
			  struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = c->mdev;
	void *rqc = rqp->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	u32 byte_count;
	int npages;
	int wq_sz;
	int err;
	int i;

	rqp->wq.db_numa_node = mlx5e_get_node(c->priv, c->ix);

	err = mlx5_wq_ll_create(mdev, &rqp->wq, rqc_wq, &rq->wq,
				&rq->wq_ctrl);
	if (err)
		return err;

	rq->wq.db = &rq->wq.db[MLX5_RCV_DBR];

	wq_sz = mlx5_wq_ll_get_size(&rq->wq);

	rq->wq_type = params->rq_wq_type;
	rq->pdev    = c->pdev;
	rq->netdev  = c->netdev;
	rq->tstamp  = c->tstamp;
	rq->channel = c;
	rq->ix      = c->ix;
	rq->mdev    = mdev;

	rq->xdp_prog = params->xdp_prog ? bpf_prog_inc(params->xdp_prog) : NULL;
	if (IS_ERR(rq->xdp_prog)) {
		err = PTR_ERR(rq->xdp_prog);
		rq->xdp_prog = NULL;
		goto err_rq_wq_destroy;
	}

	rq->buff.map_dir = rq->xdp_prog ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;
	rq->buff.headroom = params->rq_headroom;

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:

		rq->post_wqes = mlx5e_post_rx_mpwqes;
		rq->dealloc_wqe = mlx5e_dealloc_rx_mpwqe;

		rq->handle_rx_cqe = c->priv->profile->rx_handlers.handle_rx_cqe_mpwqe;
#ifdef CONFIG_MLX5_EN_IPSEC
		if (MLX5_IPSEC_DEV(mdev)) {
			err = -EINVAL;
			netdev_err(c->netdev, "MPWQE RQ with IPSec offload not supported\n");
			goto err_rq_wq_destroy;
		}
#endif
		if (!rq->handle_rx_cqe) {
			err = -EINVAL;
			netdev_err(c->netdev, "RX handler of MPWQE RQ is not set, err %d\n", err);
			goto err_rq_wq_destroy;
		}

		rq->mpwqe.log_stride_sz = params->mpwqe_log_stride_sz;
		rq->mpwqe.num_strides = BIT(params->mpwqe_log_num_strides);

		byte_count = rq->mpwqe.num_strides << rq->mpwqe.log_stride_sz;

		err = mlx5e_create_rq_umr_mkey(mdev, rq);
		if (err)
			goto err_rq_wq_destroy;
		rq->mkey_be = cpu_to_be32(rq->umr_mkey.key);

		err = mlx5e_rq_alloc_mpwqe_info(rq, c);
		if (err)
			goto err_destroy_umr_mkey;
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		rq->wqe.frag_info =
			kzalloc_node(wq_sz * sizeof(*rq->wqe.frag_info),
				     GFP_KERNEL,
				     mlx5e_get_node(c->priv, c->ix));
		if (!rq->wqe.frag_info) {
			err = -ENOMEM;
			goto err_rq_wq_destroy;
		}
		rq->post_wqes = mlx5e_post_rx_wqes;
		rq->dealloc_wqe = mlx5e_dealloc_rx_wqe;

#ifdef CONFIG_MLX5_EN_IPSEC
		if (c->priv->ipsec)
			rq->handle_rx_cqe = mlx5e_ipsec_handle_rx_cqe;
		else
#endif
			rq->handle_rx_cqe = c->priv->profile->rx_handlers.handle_rx_cqe;
		if (!rq->handle_rx_cqe) {
			kfree(rq->wqe.frag_info);
			err = -EINVAL;
			netdev_err(c->netdev, "RX handler of RQ is not set, err %d\n", err);
			goto err_rq_wq_destroy;
		}

		byte_count = params->lro_en  ?
				params->lro_wqe_sz :
				MLX5E_SW2HW_MTU(c->priv, c->netdev->mtu);
#ifdef CONFIG_MLX5_EN_IPSEC
		if (MLX5_IPSEC_DEV(mdev))
			byte_count += MLX5E_METADATA_ETHER_LEN;
#endif
		rq->wqe.page_reuse = !params->xdp_prog && !params->lro_en;

		/* calc the required page order */
		rq->wqe.frag_sz = MLX5_SKB_FRAG_SZ(rq->buff.headroom + byte_count);
		npages = DIV_ROUND_UP(rq->wqe.frag_sz, PAGE_SIZE);
		rq->buff.page_order = order_base_2(npages);

		byte_count |= MLX5_HW_START_PADDING;
		rq->mkey_be = c->mkey_be;
	}

	for (i = 0; i < wq_sz; i++) {
		struct mlx5e_rx_wqe *wqe = mlx5_wq_ll_get_wqe(&rq->wq, i);

		if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
			u64 dma_offset = (u64)mlx5e_get_wqe_mtt_offset(rq, i) << PAGE_SHIFT;

			wqe->data.addr = cpu_to_be64(dma_offset);
		}

		wqe->data.byte_count = cpu_to_be32(byte_count);
		wqe->data.lkey = rq->mkey_be;
	}

	INIT_WORK(&rq->am.work, mlx5e_rx_am_work);
	rq->am.mode = params->rx_cq_period_mode;
	rq->page_cache.head = 0;
	rq->page_cache.tail = 0;

	return 0;

err_destroy_umr_mkey:
	mlx5_core_destroy_mkey(mdev, &rq->umr_mkey);

err_rq_wq_destroy:
	if (rq->xdp_prog)
		bpf_prog_put(rq->xdp_prog);
	mlx5_wq_destroy(&rq->wq_ctrl);

	return err;
}

static void mlx5e_free_rq(struct mlx5e_rq *rq)
{
	int i;

	if (rq->xdp_prog)
		bpf_prog_put(rq->xdp_prog);

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		mlx5e_rq_free_mpwqe_info(rq);
		mlx5_core_destroy_mkey(rq->mdev, &rq->umr_mkey);
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		kfree(rq->wqe.frag_info);
	}

	for (i = rq->page_cache.head; i != rq->page_cache.tail;
	     i = (i + 1) & (MLX5E_CACHE_SIZE - 1)) {
		struct mlx5e_dma_info *dma_info = &rq->page_cache.page_cache[i];

		mlx5e_page_release(rq, dma_info, false);
	}
	mlx5_wq_destroy(&rq->wq_ctrl);
}

static int mlx5e_create_rq(struct mlx5e_rq *rq,
			   struct mlx5e_rq_param *param)
{
	struct mlx5_core_dev *mdev = rq->mdev;

	void *in;
	void *rqc;
	void *wq;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_rq_in) +
		sizeof(u64) * rq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	wq  = MLX5_ADDR_OF(rqc, rqc, wq);

	memcpy(rqc, param->rqc, sizeof(param->rqc));

	MLX5_SET(rqc,  rqc, cqn,		rq->cq.mcq.cqn);
	MLX5_SET(rqc,  rqc, state,		MLX5_RQC_STATE_RST);
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
	struct mlx5_core_dev *mdev = c->mdev;

	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, curr_state);
	MLX5_SET(rqc, rqc, state, next_state);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in, inlen);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_scatter_fcs(struct mlx5e_rq *rq, bool enable)
{
	struct mlx5e_channel *c = rq->channel;
	struct mlx5e_priv *priv = c->priv;
	struct mlx5_core_dev *mdev = priv->mdev;

	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, MLX5_RQC_STATE_RDY);
	MLX5_SET64(modify_rq_in, in, modify_bitmask,
		   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_SCATTER_FCS);
	MLX5_SET(rqc, rqc, scatter_fcs, enable);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RDY);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in, inlen);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_vsd(struct mlx5e_rq *rq, bool vsd)
{
	struct mlx5e_channel *c = rq->channel;
	struct mlx5_core_dev *mdev = c->mdev;
	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, MLX5_RQC_STATE_RDY);
	MLX5_SET64(modify_rq_in, in, modify_bitmask,
		   MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_VSD);
	MLX5_SET(rqc, rqc, vsd, vsd);
	MLX5_SET(rqc, rqc, state, MLX5_RQC_STATE_RDY);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in, inlen);

	kvfree(in);

	return err;
}

static void mlx5e_destroy_rq(struct mlx5e_rq *rq)
{
	mlx5_core_destroy_rq(rq->mdev, rq->rqn);
}

static int mlx5e_wait_for_min_rx_wqes(struct mlx5e_rq *rq)
{
	unsigned long exp_time = jiffies + msecs_to_jiffies(20000);
	struct mlx5e_channel *c = rq->channel;

	struct mlx5_wq_ll *wq = &rq->wq;
	u16 min_wqes = mlx5_min_rx_wqes(rq->wq_type, mlx5_wq_ll_get_size(wq));

	while (time_before(jiffies, exp_time)) {
		if (wq->cur_sz >= min_wqes)
			return 0;

		msleep(20);
	}

	netdev_warn(c->netdev, "Failed to get min RX wqes on RQN[0x%x] wq cur_sz(%d) min_rx_wqes(%d)\n",
		    rq->rqn, wq->cur_sz, min_wqes);
	return -ETIMEDOUT;
}

static void mlx5e_free_rx_descs(struct mlx5e_rq *rq)
{
	struct mlx5_wq_ll *wq = &rq->wq;
	struct mlx5e_rx_wqe *wqe;
	__be16 wqe_ix_be;
	u16 wqe_ix;

	/* UMR WQE (if in progress) is always at wq->head */
	if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ &&
	    rq->mpwqe.umr_in_progress)
		mlx5e_free_rx_mpwqe(rq, &rq->mpwqe.info[wq->head]);

	while (!mlx5_wq_ll_is_empty(wq)) {
		wqe_ix_be = *wq->tail_next;
		wqe_ix    = be16_to_cpu(wqe_ix_be);
		wqe       = mlx5_wq_ll_get_wqe(&rq->wq, wqe_ix);
		rq->dealloc_wqe(rq, wqe_ix);
		mlx5_wq_ll_pop(&rq->wq, wqe_ix_be,
			       &wqe->next.next_wqe_index);
	}

	if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST && rq->wqe.page_reuse) {
		/* Clean outstanding pages on handled WQEs that decided to do page-reuse,
		 * but yet to be re-posted.
		 */
		int wq_sz = mlx5_wq_ll_get_size(&rq->wq);

		for (wqe_ix = 0; wqe_ix < wq_sz; wqe_ix++)
			rq->dealloc_wqe(rq, wqe_ix);
	}
}

static int mlx5e_open_rq(struct mlx5e_channel *c,
			 struct mlx5e_params *params,
			 struct mlx5e_rq_param *param,
			 struct mlx5e_rq *rq)
{
	int err;

	err = mlx5e_alloc_rq(c, params, param, rq);
	if (err)
		return err;

	err = mlx5e_create_rq(rq, param);
	if (err)
		goto err_free_rq;

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_destroy_rq;

	if (params->rx_am_enabled)
		c->rq.state |= BIT(MLX5E_RQ_STATE_AM);

	return 0;

err_destroy_rq:
	mlx5e_destroy_rq(rq);
err_free_rq:
	mlx5e_free_rq(rq);

	return err;
}

static void mlx5e_activate_rq(struct mlx5e_rq *rq)
{
	struct mlx5e_icosq *sq = &rq->channel->icosq;
	u16 pi = sq->pc & sq->wq.sz_m1;
	struct mlx5e_tx_wqe *nopwqe;

	set_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
	sq->db.ico_wqe[pi].opcode     = MLX5_OPCODE_NOP;
	nopwqe = mlx5e_post_nop(&sq->wq, sq->sqn, &sq->pc);
	mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, &nopwqe->ctrl);
}

static void mlx5e_deactivate_rq(struct mlx5e_rq *rq)
{
	clear_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
	napi_synchronize(&rq->channel->napi); /* prevent mlx5e_post_rx_wqes */
}

static void mlx5e_close_rq(struct mlx5e_rq *rq)
{
	cancel_work_sync(&rq->am.work);
	mlx5e_destroy_rq(rq);
	mlx5e_free_rx_descs(rq);
	mlx5e_free_rq(rq);
}

static void mlx5e_free_xdpsq_db(struct mlx5e_xdpsq *sq)
{
	kfree(sq->db.di);
}

static int mlx5e_alloc_xdpsq_db(struct mlx5e_xdpsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);

	sq->db.di = kzalloc_node(sizeof(*sq->db.di) * wq_sz,
				     GFP_KERNEL, numa);
	if (!sq->db.di) {
		mlx5e_free_xdpsq_db(sq);
		return -ENOMEM;
	}

	return 0;
}

static int mlx5e_alloc_xdpsq(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_xdpsq *sq)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	int err;

	sq->pdev      = c->pdev;
	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;

	param->wq.db_numa_node = mlx5e_get_node(c->priv, c->ix);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, &sq->wq, &sq->wq_ctrl);
	if (err)
		return err;
	sq->wq.db = &sq->wq.db[MLX5_SND_DBR];

	err = mlx5e_alloc_xdpsq_db(sq, mlx5e_get_node(c->priv, c->ix));
	if (err)
		goto err_sq_wq_destroy;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

static void mlx5e_free_xdpsq(struct mlx5e_xdpsq *sq)
{
	mlx5e_free_xdpsq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static void mlx5e_free_icosq_db(struct mlx5e_icosq *sq)
{
	kfree(sq->db.ico_wqe);
}

static int mlx5e_alloc_icosq_db(struct mlx5e_icosq *sq, int numa)
{
	u8 wq_sz = mlx5_wq_cyc_get_size(&sq->wq);

	sq->db.ico_wqe = kzalloc_node(sizeof(*sq->db.ico_wqe) * wq_sz,
				      GFP_KERNEL, numa);
	if (!sq->db.ico_wqe)
		return -ENOMEM;

	return 0;
}

static int mlx5e_alloc_icosq(struct mlx5e_channel *c,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_icosq *sq)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	int err;

	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;

	param->wq.db_numa_node = mlx5e_get_node(c->priv, c->ix);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, &sq->wq, &sq->wq_ctrl);
	if (err)
		return err;
	sq->wq.db = &sq->wq.db[MLX5_SND_DBR];

	err = mlx5e_alloc_icosq_db(sq, mlx5e_get_node(c->priv, c->ix));
	if (err)
		goto err_sq_wq_destroy;

	sq->edge = (sq->wq.sz_m1 + 1) - MLX5E_ICOSQ_MAX_WQEBBS;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

static void mlx5e_free_icosq(struct mlx5e_icosq *sq)
{
	mlx5e_free_icosq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static void mlx5e_free_txqsq_db(struct mlx5e_txqsq *sq)
{
	kfree(sq->db.wqe_info);
	kfree(sq->db.dma_fifo);
}

static int mlx5e_alloc_txqsq_db(struct mlx5e_txqsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	int df_sz = wq_sz * MLX5_SEND_WQEBB_NUM_DS;

	sq->db.dma_fifo = kzalloc_node(df_sz * sizeof(*sq->db.dma_fifo),
					   GFP_KERNEL, numa);
	sq->db.wqe_info = kzalloc_node(wq_sz * sizeof(*sq->db.wqe_info),
					   GFP_KERNEL, numa);
	if (!sq->db.dma_fifo || !sq->db.wqe_info) {
		mlx5e_free_txqsq_db(sq);
		return -ENOMEM;
	}

	sq->dma_fifo_mask = df_sz - 1;

	return 0;
}

static int mlx5e_alloc_txqsq(struct mlx5e_channel *c,
			     int txq_ix,
			     struct mlx5e_params *params,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_txqsq *sq)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	int err;

	sq->pdev      = c->pdev;
	sq->tstamp    = c->tstamp;
	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;
	sq->max_inline      = params->tx_max_inline;
	sq->min_inline_mode = params->tx_min_inline_mode;
	if (MLX5_IPSEC_DEV(c->priv->mdev))
		set_bit(MLX5E_SQ_STATE_IPSEC, &sq->state);

	param->wq.db_numa_node = mlx5e_get_node(c->priv, c->ix);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, &sq->wq, &sq->wq_ctrl);
	if (err)
		return err;
	sq->wq.db    = &sq->wq.db[MLX5_SND_DBR];

	err = mlx5e_alloc_txqsq_db(sq, mlx5e_get_node(c->priv, c->ix));
	if (err)
		goto err_sq_wq_destroy;

	sq->edge = (sq->wq.sz_m1 + 1) - MLX5_SEND_WQE_MAX_WQEBBS;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

static void mlx5e_free_txqsq(struct mlx5e_txqsq *sq)
{
	mlx5e_free_txqsq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

struct mlx5e_create_sq_param {
	struct mlx5_wq_ctrl        *wq_ctrl;
	u32                         cqn;
	u32                         tisn;
	u8                          tis_lst_sz;
	u8                          min_inline_mode;
};

static int mlx5e_create_sq(struct mlx5_core_dev *mdev,
			   struct mlx5e_sq_param *param,
			   struct mlx5e_create_sq_param *csp,
			   u32 *sqn)
{
	void *in;
	void *sqc;
	void *wq;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_sq_in) +
		sizeof(u64) * csp->wq_ctrl->buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq = MLX5_ADDR_OF(sqc, sqc, wq);

	memcpy(sqc, param->sqc, sizeof(param->sqc));
	MLX5_SET(sqc,  sqc, tis_lst_sz, csp->tis_lst_sz);
	MLX5_SET(sqc,  sqc, tis_num_0, csp->tisn);
	MLX5_SET(sqc,  sqc, cqn, csp->cqn);

	if (MLX5_CAP_ETH(mdev, wqe_inline_mode) == MLX5_CAP_INLINE_MODE_VPORT_CONTEXT)
		MLX5_SET(sqc,  sqc, min_wqe_inline_mode, csp->min_inline_mode);

	MLX5_SET(sqc,  sqc, state, MLX5_SQC_STATE_RST);

	MLX5_SET(wq,   wq, wq_type,       MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq,   wq, uar_page,      mdev->mlx5e_res.bfreg.index);
	MLX5_SET(wq,   wq, log_wq_pg_sz,  csp->wq_ctrl->buf.page_shift -
					  MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr,      csp->wq_ctrl->db.dma);

	mlx5_fill_page_array(&csp->wq_ctrl->buf, (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, sqn);

	kvfree(in);

	return err;
}

struct mlx5e_modify_sq_param {
	int curr_state;
	int next_state;
	bool rl_update;
	int rl_index;
};

static int mlx5e_modify_sq(struct mlx5_core_dev *mdev, u32 sqn,
			   struct mlx5e_modify_sq_param *p)
{
	void *in;
	void *sqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);

	MLX5_SET(modify_sq_in, in, sq_state, p->curr_state);
	MLX5_SET(sqc, sqc, state, p->next_state);
	if (p->rl_update && p->next_state == MLX5_SQC_STATE_RDY) {
		MLX5_SET64(modify_sq_in, in, modify_bitmask, 1);
		MLX5_SET(sqc,  sqc, packet_pacing_rate_limit_index, p->rl_index);
	}

	err = mlx5_core_modify_sq(mdev, sqn, in, inlen);

	kvfree(in);

	return err;
}

static void mlx5e_destroy_sq(struct mlx5_core_dev *mdev, u32 sqn)
{
	mlx5_core_destroy_sq(mdev, sqn);
}

static int mlx5e_create_sq_rdy(struct mlx5_core_dev *mdev,
			       struct mlx5e_sq_param *param,
			       struct mlx5e_create_sq_param *csp,
			       u32 *sqn)
{
	struct mlx5e_modify_sq_param msp = {0};
	int err;

	err = mlx5e_create_sq(mdev, param, csp, sqn);
	if (err)
		return err;

	msp.curr_state = MLX5_SQC_STATE_RST;
	msp.next_state = MLX5_SQC_STATE_RDY;
	err = mlx5e_modify_sq(mdev, *sqn, &msp);
	if (err)
		mlx5e_destroy_sq(mdev, *sqn);

	return err;
}

static int mlx5e_set_sq_maxrate(struct net_device *dev,
				struct mlx5e_txqsq *sq, u32 rate);

static int mlx5e_open_txqsq(struct mlx5e_channel *c,
			    u32 tisn,
			    int txq_ix,
			    struct mlx5e_params *params,
			    struct mlx5e_sq_param *param,
			    struct mlx5e_txqsq *sq)
{
	struct mlx5e_create_sq_param csp = {};
	u32 tx_rate;
	int err;

	err = mlx5e_alloc_txqsq(c, txq_ix, params, param, sq);
	if (err)
		return err;

	csp.tisn            = tisn;
	csp.tis_lst_sz      = 1;
	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = sq->min_inline_mode;
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, &sq->sqn);
	if (err)
		goto err_free_txqsq;

	tx_rate = c->priv->tx_rates[sq->txq_ix];
	if (tx_rate)
		mlx5e_set_sq_maxrate(c->netdev, sq, tx_rate);

	return 0;

err_free_txqsq:
	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	mlx5e_free_txqsq(sq);

	return err;
}

static void mlx5e_activate_txqsq(struct mlx5e_txqsq *sq)
{
	sq->txq = netdev_get_tx_queue(sq->channel->netdev, sq->txq_ix);
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	netdev_tx_reset_queue(sq->txq);
	netif_tx_start_queue(sq->txq);
}

static inline void netif_tx_disable_queue(struct netdev_queue *txq)
{
	__netif_tx_lock_bh(txq);
	netif_tx_stop_queue(txq);
	__netif_tx_unlock_bh(txq);
}

static void mlx5e_deactivate_txqsq(struct mlx5e_txqsq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	/* prevent netif_tx_wake_queue */
	napi_synchronize(&c->napi);

	netif_tx_disable_queue(sq->txq);

	/* last doorbell out, godspeed .. */
	if (mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc, 1)) {
		struct mlx5e_tx_wqe *nop;

		sq->db.wqe_info[(sq->pc & sq->wq.sz_m1)].skb = NULL;
		nop = mlx5e_post_nop(&sq->wq, sq->sqn, &sq->pc);
		mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, &nop->ctrl);
	}
}

static void mlx5e_close_txqsq(struct mlx5e_txqsq *sq)
{
	struct mlx5e_channel *c = sq->channel;
	struct mlx5_core_dev *mdev = c->mdev;

	mlx5e_destroy_sq(mdev, sq->sqn);
	if (sq->rate_limit)
		mlx5_rl_remove_rate(mdev, sq->rate_limit);
	mlx5e_free_txqsq_descs(sq);
	mlx5e_free_txqsq(sq);
}

static int mlx5e_open_icosq(struct mlx5e_channel *c,
			    struct mlx5e_params *params,
			    struct mlx5e_sq_param *param,
			    struct mlx5e_icosq *sq)
{
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_alloc_icosq(c, param, sq);
	if (err)
		return err;

	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = params->tx_min_inline_mode;
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, &sq->sqn);
	if (err)
		goto err_free_icosq;

	return 0;

err_free_icosq:
	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	mlx5e_free_icosq(sq);

	return err;
}

static void mlx5e_close_icosq(struct mlx5e_icosq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	napi_synchronize(&c->napi);

	mlx5e_destroy_sq(c->mdev, sq->sqn);
	mlx5e_free_icosq(sq);
}

static int mlx5e_open_xdpsq(struct mlx5e_channel *c,
			    struct mlx5e_params *params,
			    struct mlx5e_sq_param *param,
			    struct mlx5e_xdpsq *sq)
{
	unsigned int ds_cnt = MLX5E_XDP_TX_DS_COUNT;
	struct mlx5e_create_sq_param csp = {};
	unsigned int inline_hdr_sz = 0;
	int err;
	int i;

	err = mlx5e_alloc_xdpsq(c, params, param, sq);
	if (err)
		return err;

	csp.tis_lst_sz      = 1;
	csp.tisn            = c->priv->tisn[0]; /* tc = 0 */
	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = sq->min_inline_mode;
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, &sq->sqn);
	if (err)
		goto err_free_xdpsq;

	if (sq->min_inline_mode != MLX5_INLINE_MODE_NONE) {
		inline_hdr_sz = MLX5E_XDP_MIN_INLINE;
		ds_cnt++;
	}

	/* Pre initialize fixed WQE fields */
	for (i = 0; i < mlx5_wq_cyc_get_size(&sq->wq); i++) {
		struct mlx5e_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(&sq->wq, i);
		struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
		struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;
		struct mlx5_wqe_data_seg *dseg;

		cseg->qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
		eseg->inline_hdr.sz = cpu_to_be16(inline_hdr_sz);

		dseg = (struct mlx5_wqe_data_seg *)cseg + (ds_cnt - 1);
		dseg->lkey = sq->mkey_be;
	}

	return 0;

err_free_xdpsq:
	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	mlx5e_free_xdpsq(sq);

	return err;
}

static void mlx5e_close_xdpsq(struct mlx5e_xdpsq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	napi_synchronize(&c->napi);

	mlx5e_destroy_sq(c->mdev, sq->sqn);
	mlx5e_free_xdpsq_descs(sq);
	mlx5e_free_xdpsq(sq);
}

static int mlx5e_alloc_cq_common(struct mlx5_core_dev *mdev,
				 struct mlx5e_cq_param *param,
				 struct mlx5e_cq *cq)
{
	struct mlx5_core_cq *mcq = &cq->mcq;
	int eqn_not_used;
	unsigned int irqn;
	int err;
	u32 i;

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

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(&cq->wq, i);

		cqe->op_own = 0xf1;
	}

	cq->mdev = mdev;

	return 0;
}

static int mlx5e_alloc_cq(struct mlx5e_channel *c,
			  struct mlx5e_cq_param *param,
			  struct mlx5e_cq *cq)
{
	struct mlx5_core_dev *mdev = c->priv->mdev;
	int err;

	param->wq.buf_numa_node = mlx5e_get_node(c->priv, c->ix);
	param->wq.db_numa_node  = mlx5e_get_node(c->priv, c->ix);
	param->eq_ix   = c->ix;

	err = mlx5e_alloc_cq_common(mdev, param, cq);

	cq->napi    = &c->napi;
	cq->channel = c;

	return err;
}

static void mlx5e_free_cq(struct mlx5e_cq *cq)
{
	mlx5_cqwq_destroy(&cq->wq_ctrl);
}

static int mlx5e_create_cq(struct mlx5e_cq *cq, struct mlx5e_cq_param *param)
{
	struct mlx5_core_dev *mdev = cq->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;

	void *in;
	void *cqc;
	int inlen;
	unsigned int irqn_not_used;
	int eqn;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.frag_buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);

	memcpy(cqc, param->cqc, sizeof(param->cqc));

	mlx5_fill_page_frag_array(&cq->wq_ctrl.frag_buf,
				  (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas));

	mlx5_vector2eqn(mdev, param->eq_ix, &eqn, &irqn_not_used);

	MLX5_SET(cqc,   cqc, cq_period_mode, param->cq_period_mode);
	MLX5_SET(cqc,   cqc, c_eqn,         eqn);
	MLX5_SET(cqc,   cqc, uar_page,      mdev->priv.uar->index);
	MLX5_SET(cqc,   cqc, log_page_size, cq->wq_ctrl.frag_buf.page_shift -
					    MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr,      cq->wq_ctrl.db.dma);

	err = mlx5_core_create_cq(mdev, mcq, in, inlen);

	kvfree(in);

	if (err)
		return err;

	mlx5e_cq_arm(cq);

	return 0;
}

static void mlx5e_destroy_cq(struct mlx5e_cq *cq)
{
	mlx5_core_destroy_cq(cq->mdev, &cq->mcq);
}

static int mlx5e_open_cq(struct mlx5e_channel *c,
			 struct mlx5e_cq_moder moder,
			 struct mlx5e_cq_param *param,
			 struct mlx5e_cq *cq)
{
	struct mlx5_core_dev *mdev = c->mdev;
	int err;

	err = mlx5e_alloc_cq(c, param, cq);
	if (err)
		return err;

	err = mlx5e_create_cq(cq, param);
	if (err)
		goto err_free_cq;

	if (MLX5_CAP_GEN(mdev, cq_moderation))
		mlx5_core_modify_cq_moderation(mdev, &cq->mcq, moder.usec, moder.pkts);
	return 0;

err_free_cq:
	mlx5e_free_cq(cq);

	return err;
}

static void mlx5e_close_cq(struct mlx5e_cq *cq)
{
	mlx5e_destroy_cq(cq);
	mlx5e_free_cq(cq);
}

static int mlx5e_open_tx_cqs(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct mlx5e_channel_param *cparam)
{
	int err;
	int tc;

	for (tc = 0; tc < c->num_tc; tc++) {
		err = mlx5e_open_cq(c, params->tx_cq_moderation,
				    &cparam->tx_cq, &c->sq[tc].cq);
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
			  struct mlx5e_params *params,
			  struct mlx5e_channel_param *cparam)
{
	int err;
	int tc;

	for (tc = 0; tc < params->num_tc; tc++) {
		int txq_ix = c->ix + tc * params->num_channels;

		err = mlx5e_open_txqsq(c, c->priv->tisn[tc], txq_ix,
				       params, &cparam->sq, &c->sq[tc]);
		if (err)
			goto err_close_sqs;
	}

	return 0;

err_close_sqs:
	for (tc--; tc >= 0; tc--)
		mlx5e_close_txqsq(&c->sq[tc]);

	return err;
}

static void mlx5e_close_sqs(struct mlx5e_channel *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_txqsq(&c->sq[tc]);
}

static int mlx5e_set_sq_maxrate(struct net_device *dev,
				struct mlx5e_txqsq *sq, u32 rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_modify_sq_param msp = {0};
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

	msp.curr_state = MLX5_SQC_STATE_RDY;
	msp.next_state = MLX5_SQC_STATE_RDY;
	msp.rl_index   = rl_index;
	msp.rl_update  = true;
	err = mlx5e_modify_sq(mdev, sq->sqn, &msp);
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
	struct mlx5e_txqsq *sq = priv->txq2sq[index];
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
			      struct mlx5e_params *params,
			      struct mlx5e_channel_param *cparam,
			      struct mlx5e_channel **cp)
{
	struct mlx5e_cq_moder icocq_moder = {0, 0};
	struct net_device *netdev = priv->netdev;
	struct mlx5e_channel *c;
	unsigned int irq;
	int err;
	int eqn;

	c = kzalloc_node(sizeof(*c), GFP_KERNEL, mlx5e_get_node(priv, ix));
	if (!c)
		return -ENOMEM;

	c->priv     = priv;
	c->mdev     = priv->mdev;
	c->tstamp   = &priv->tstamp;
	c->ix       = ix;
	c->pdev     = &priv->mdev->pdev->dev;
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.mkey.key);
	c->num_tc   = params->num_tc;
	c->xdp      = !!params->xdp_prog;

	mlx5_vector2eqn(priv->mdev, ix, &eqn, &irq);
	c->irq_desc = irq_to_desc(irq);

	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll, 64);

	err = mlx5e_open_cq(c, icocq_moder, &cparam->icosq_cq, &c->icosq.cq);
	if (err)
		goto err_napi_del;

	err = mlx5e_open_tx_cqs(c, params, cparam);
	if (err)
		goto err_close_icosq_cq;

	err = mlx5e_open_cq(c, params->rx_cq_moderation, &cparam->rx_cq, &c->rq.cq);
	if (err)
		goto err_close_tx_cqs;

	/* XDP SQ CQ params are same as normal TXQ sq CQ params */
	err = c->xdp ? mlx5e_open_cq(c, params->tx_cq_moderation,
				     &cparam->tx_cq, &c->rq.xdpsq.cq) : 0;
	if (err)
		goto err_close_rx_cq;

	napi_enable(&c->napi);

	err = mlx5e_open_icosq(c, params, &cparam->icosq, &c->icosq);
	if (err)
		goto err_disable_napi;

	err = mlx5e_open_sqs(c, params, cparam);
	if (err)
		goto err_close_icosq;

	err = c->xdp ? mlx5e_open_xdpsq(c, params, &cparam->xdp_sq, &c->rq.xdpsq) : 0;
	if (err)
		goto err_close_sqs;

	err = mlx5e_open_rq(c, params, &cparam->rq, &c->rq);
	if (err)
		goto err_close_xdp_sq;

	*cp = c;

	return 0;
err_close_xdp_sq:
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq.xdpsq);

err_close_sqs:
	mlx5e_close_sqs(c);

err_close_icosq:
	mlx5e_close_icosq(&c->icosq);

err_disable_napi:
	napi_disable(&c->napi);
	if (c->xdp)
		mlx5e_close_cq(&c->rq.xdpsq.cq);

err_close_rx_cq:
	mlx5e_close_cq(&c->rq.cq);

err_close_tx_cqs:
	mlx5e_close_tx_cqs(c);

err_close_icosq_cq:
	mlx5e_close_cq(&c->icosq.cq);

err_napi_del:
	netif_napi_del(&c->napi);
	kfree(c);

	return err;
}

static void mlx5e_activate_channel(struct mlx5e_channel *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_activate_txqsq(&c->sq[tc]);
	mlx5e_activate_rq(&c->rq);
	netif_set_xps_queue(c->netdev,
		mlx5_get_vector_affinity(c->priv->mdev, c->ix), c->ix);
}

static void mlx5e_deactivate_channel(struct mlx5e_channel *c)
{
	int tc;

	mlx5e_deactivate_rq(&c->rq);
	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_deactivate_txqsq(&c->sq[tc]);
}

static void mlx5e_close_channel(struct mlx5e_channel *c)
{
	mlx5e_close_rq(&c->rq);
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq.xdpsq);
	mlx5e_close_sqs(c);
	mlx5e_close_icosq(&c->icosq);
	napi_disable(&c->napi);
	if (c->xdp)
		mlx5e_close_cq(&c->rq.xdpsq.cq);
	mlx5e_close_cq(&c->rq.cq);
	mlx5e_close_tx_cqs(c);
	mlx5e_close_cq(&c->icosq.cq);
	netif_napi_del(&c->napi);

	kfree(c);
}

static void mlx5e_build_rq_param(struct mlx5e_priv *priv,
				 struct mlx5e_params *params,
				 struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		MLX5_SET(wq, wq, log_wqe_num_of_strides, params->mpwqe_log_num_strides - 9);
		MLX5_SET(wq, wq, log_wqe_stride_size, params->mpwqe_log_stride_sz - 6);
		MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ);
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_LINKED_LIST);
	}

	MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);
	MLX5_SET(wq, wq, log_wq_stride,    ilog2(sizeof(struct mlx5e_rx_wqe)));
	MLX5_SET(wq, wq, log_wq_sz,        params->log_rq_size);
	MLX5_SET(wq, wq, pd,               priv->mdev->mlx5e_res.pdn);
	MLX5_SET(rqc, rqc, counter_set_id, priv->q_counter);
	MLX5_SET(rqc, rqc, vsd,            params->vlan_strip_disable);
	MLX5_SET(rqc, rqc, scatter_fcs,    params->scatter_fcs_en);

	param->wq.buf_numa_node = dev_to_node(&priv->mdev->pdev->dev);
	param->wq.linear = 1;
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
				 struct mlx5e_params *params,
				 struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	MLX5_SET(sqc, sqc, allow_swp, !!MLX5_IPSEC_DEV(priv->mdev));
}

static void mlx5e_build_common_cq_param(struct mlx5e_priv *priv,
					struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, uar_page, priv->mdev->priv.uar->index);
}

static void mlx5e_build_rx_cq_param(struct mlx5e_priv *priv,
				    struct mlx5e_params *params,
				    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;
	u8 log_cq_size;

	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		log_cq_size = params->log_rq_size + params->mpwqe_log_num_strides;
		break;
	default: /* MLX5_WQ_TYPE_LINKED_LIST */
		log_cq_size = params->log_rq_size;
	}

	MLX5_SET(cqc, cqc, log_cq_size, log_cq_size);
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		MLX5_SET(cqc, cqc, mini_cqe_res_format, MLX5_CQE_FORMAT_CSUM);
		MLX5_SET(cqc, cqc, cqe_comp_en, 1);
	}

	mlx5e_build_common_cq_param(priv, param);
	param->cq_period_mode = params->rx_cq_period_mode;
}

static void mlx5e_build_tx_cq_param(struct mlx5e_priv *priv,
				    struct mlx5e_params *params,
				    struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, params->log_sq_size);

	mlx5e_build_common_cq_param(priv, param);

	param->cq_period_mode = MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
}

static void mlx5e_build_ico_cq_param(struct mlx5e_priv *priv,
				     u8 log_wq_size,
				     struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, log_wq_size);

	mlx5e_build_common_cq_param(priv, param);

	param->cq_period_mode = MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
}

static void mlx5e_build_icosq_param(struct mlx5e_priv *priv,
				    u8 log_wq_size,
				    struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);

	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
	MLX5_SET(sqc, sqc, reg_umr, MLX5_CAP_ETH(priv->mdev, reg_umr_sq));
}

static void mlx5e_build_xdpsq_param(struct mlx5e_priv *priv,
				    struct mlx5e_params *params,
				    struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
}

static void mlx5e_build_channel_param(struct mlx5e_priv *priv,
				      struct mlx5e_params *params,
				      struct mlx5e_channel_param *cparam)
{
	u8 icosq_log_wq_sz = MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;

	mlx5e_build_rq_param(priv, params, &cparam->rq);
	mlx5e_build_sq_param(priv, params, &cparam->sq);
	mlx5e_build_xdpsq_param(priv, params, &cparam->xdp_sq);
	mlx5e_build_icosq_param(priv, icosq_log_wq_sz, &cparam->icosq);
	mlx5e_build_rx_cq_param(priv, params, &cparam->rx_cq);
	mlx5e_build_tx_cq_param(priv, params, &cparam->tx_cq);
	mlx5e_build_ico_cq_param(priv, icosq_log_wq_sz, &cparam->icosq_cq);
}

int mlx5e_open_channels(struct mlx5e_priv *priv,
			struct mlx5e_channels *chs)
{
	struct mlx5e_channel_param *cparam;
	int err = -ENOMEM;
	int i;

	chs->num = chs->params.num_channels;

	chs->c = kcalloc(chs->num, sizeof(struct mlx5e_channel *), GFP_KERNEL);
	cparam = kzalloc(sizeof(struct mlx5e_channel_param), GFP_KERNEL);
	if (!chs->c || !cparam)
		goto err_free;

	mlx5e_build_channel_param(priv, &chs->params, cparam);
	for (i = 0; i < chs->num; i++) {
		err = mlx5e_open_channel(priv, i, &chs->params, cparam, &chs->c[i]);
		if (err)
			goto err_close_channels;
	}

	kfree(cparam);
	return 0;

err_close_channels:
	for (i--; i >= 0; i--)
		mlx5e_close_channel(chs->c[i]);

err_free:
	kfree(chs->c);
	kfree(cparam);
	chs->num = 0;
	return err;
}

static void mlx5e_activate_channels(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_activate_channel(chs->c[i]);
}

static int mlx5e_wait_channels_min_rx_wqes(struct mlx5e_channels *chs)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		err = mlx5e_wait_for_min_rx_wqes(&chs->c[i]->rq);
		if (err)
			break;
	}

	return err;
}

static void mlx5e_deactivate_channels(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_deactivate_channel(chs->c[i]);
}

void mlx5e_close_channels(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_close_channel(chs->c[i]);

	kfree(chs->c);
	chs->num = 0;
}

static int
mlx5e_create_rqt(struct mlx5e_priv *priv, int sz, struct mlx5e_rqt *rqt)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqtc;
	int inlen;
	int err;
	u32 *in;
	int i;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + sizeof(u32) * sz;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	MLX5_SET(rqtc, rqtc, rqt_max_size, sz);

	for (i = 0; i < sz; i++)
		MLX5_SET(rqtc, rqtc, rq_num[i], priv->drop_rq.rqn);

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

int mlx5e_create_indirect_rqt(struct mlx5e_priv *priv)
{
	struct mlx5e_rqt *rqt = &priv->indir_rqt;
	int err;

	err = mlx5e_create_rqt(priv, MLX5E_INDIR_RQT_SIZE, rqt);
	if (err)
		mlx5_core_warn(priv->mdev, "create indirect rqts failed, %d\n", err);
	return err;
}

int mlx5e_create_direct_rqts(struct mlx5e_priv *priv)
{
	struct mlx5e_rqt *rqt;
	int err;
	int ix;

	for (ix = 0; ix < priv->profile->max_nch(priv->mdev); ix++) {
		rqt = &priv->direct_tir[ix].rqt;
		err = mlx5e_create_rqt(priv, 1 /*size */, rqt);
		if (err)
			goto err_destroy_rqts;
	}

	return 0;

err_destroy_rqts:
	mlx5_core_warn(priv->mdev, "create direct rqts failed, %d\n", err);
	for (ix--; ix >= 0; ix--)
		mlx5e_destroy_rqt(priv, &priv->direct_tir[ix].rqt);

	return err;
}

void mlx5e_destroy_direct_rqts(struct mlx5e_priv *priv)
{
	int i;

	for (i = 0; i < priv->profile->max_nch(priv->mdev); i++)
		mlx5e_destroy_rqt(priv, &priv->direct_tir[i].rqt);
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

static void mlx5e_fill_rqt_rqns(struct mlx5e_priv *priv, int sz,
				struct mlx5e_redirect_rqt_param rrp, void *rqtc)
{
	int i;

	for (i = 0; i < sz; i++) {
		u32 rqn;

		if (rrp.is_rss) {
			int ix = i;

			if (rrp.rss.hfunc == ETH_RSS_HASH_XOR)
				ix = mlx5e_bits_invert(i, ilog2(sz));

			ix = priv->channels.params.indirection_rqt[ix];
			rqn = rrp.rss.channels->c[ix]->rq.rqn;
		} else {
			rqn = rrp.rqn;
		}
		MLX5_SET(rqtc, rqtc, rq_num[i], rqn);
	}
}

int mlx5e_redirect_rqt(struct mlx5e_priv *priv, u32 rqtn, int sz,
		       struct mlx5e_redirect_rqt_param rrp)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqtc;
	int inlen;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rqt_in) + sizeof(u32) * sz;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(modify_rqt_in, in, ctx);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	MLX5_SET(modify_rqt_in, in, bitmask.rqn_list, 1);
	mlx5e_fill_rqt_rqns(priv, sz, rrp, rqtc);
	err = mlx5_core_modify_rqt(mdev, rqtn, in, inlen);

	kvfree(in);
	return err;
}

static u32 mlx5e_get_direct_rqn(struct mlx5e_priv *priv, int ix,
				struct mlx5e_redirect_rqt_param rrp)
{
	if (!rrp.is_rss)
		return rrp.rqn;

	if (ix >= rrp.rss.channels->num)
		return priv->drop_rq.rqn;

	return rrp.rss.channels->c[ix]->rq.rqn;
}

static void mlx5e_redirect_rqts(struct mlx5e_priv *priv,
				struct mlx5e_redirect_rqt_param rrp)
{
	u32 rqtn;
	int ix;

	if (priv->indir_rqt.enabled) {
		/* RSS RQ table */
		rqtn = priv->indir_rqt.rqtn;
		mlx5e_redirect_rqt(priv, rqtn, MLX5E_INDIR_RQT_SIZE, rrp);
	}

	for (ix = 0; ix < priv->profile->max_nch(priv->mdev); ix++) {
		struct mlx5e_redirect_rqt_param direct_rrp = {
			.is_rss = false,
			{
				.rqn    = mlx5e_get_direct_rqn(priv, ix, rrp)
			},
		};

		/* Direct RQ Tables */
		if (!priv->direct_tir[ix].rqt.enabled)
			continue;

		rqtn = priv->direct_tir[ix].rqt.rqtn;
		mlx5e_redirect_rqt(priv, rqtn, 1, direct_rrp);
	}
}

static void mlx5e_redirect_rqts_to_channels(struct mlx5e_priv *priv,
					    struct mlx5e_channels *chs)
{
	struct mlx5e_redirect_rqt_param rrp = {
		.is_rss        = true,
		{
			.rss = {
				.channels  = chs,
				.hfunc     = chs->params.rss_hfunc,
			}
		},
	};

	mlx5e_redirect_rqts(priv, rrp);
}

static void mlx5e_redirect_rqts_to_drop(struct mlx5e_priv *priv)
{
	struct mlx5e_redirect_rqt_param drop_rrp = {
		.is_rss = false,
		{
			.rqn = priv->drop_rq.rqn,
		},
	};

	mlx5e_redirect_rqts(priv, drop_rrp);
}

static void mlx5e_build_tir_ctx_lro(struct mlx5e_params *params, void *tirc)
{
	if (!params->lro_en)
		return;

#define ROUGH_MAX_L2_L3_HDR_SZ 256

	MLX5_SET(tirc, tirc, lro_enable_mask,
		 MLX5_TIRC_LRO_ENABLE_MASK_IPV4_LRO |
		 MLX5_TIRC_LRO_ENABLE_MASK_IPV6_LRO);
	MLX5_SET(tirc, tirc, lro_max_ip_payload_size,
		 (params->lro_wqe_sz - ROUGH_MAX_L2_L3_HDR_SZ) >> 8);
	MLX5_SET(tirc, tirc, lro_timeout_period_usecs, params->lro_timeout);
}

void mlx5e_build_indir_tir_ctx_hash(struct mlx5e_params *params,
				    enum mlx5e_traffic_types tt,
				    void *tirc, bool inner)
{
	void *hfso = inner ? MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_inner) :
			     MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);

#define MLX5_HASH_IP            (MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP)

#define MLX5_HASH_IP_L4PORTS    (MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_L4_SPORT |\
				 MLX5_HASH_FIELD_SEL_L4_DPORT)

#define MLX5_HASH_IP_IPSEC_SPI  (MLX5_HASH_FIELD_SEL_SRC_IP   |\
				 MLX5_HASH_FIELD_SEL_DST_IP   |\
				 MLX5_HASH_FIELD_SEL_IPSEC_SPI)

	MLX5_SET(tirc, tirc, rx_hash_fn, mlx5e_rx_hash_fn(params->rss_hfunc));
	if (params->rss_hfunc == ETH_RSS_HASH_TOP) {
		void *rss_key = MLX5_ADDR_OF(tirc, tirc,
					     rx_hash_toeplitz_key);
		size_t len = MLX5_FLD_SZ_BYTES(tirc,
					       rx_hash_toeplitz_key);

		MLX5_SET(tirc, tirc, rx_hash_symmetric, 1);
		memcpy(rss_key, params->toeplitz_hash_key, len);
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
	default:
		WARN_ONCE(true, "%s: bad traffic type!\n", __func__);
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
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_tir_in, in, bitmask.lro, 1);
	tirc = MLX5_ADDR_OF(modify_tir_in, in, ctx);

	mlx5e_build_tir_ctx_lro(&priv->channels.params, tirc);

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

static void mlx5e_build_inner_indir_tir_ctx(struct mlx5e_priv *priv,
					    enum mlx5e_traffic_types tt,
					    u32 *tirc)
{
	MLX5_SET(tirc, tirc, transport_domain, priv->mdev->mlx5e_res.td.tdn);

	mlx5e_build_tir_ctx_lro(&priv->channels.params, tirc);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table, priv->indir_rqt.rqtn);
	MLX5_SET(tirc, tirc, tunneled_offload_en, 0x1);

	mlx5e_build_indir_tir_ctx_hash(&priv->channels.params, tt, tirc, true);
}

static int mlx5e_set_mtu(struct mlx5e_priv *priv, u16 mtu)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 hw_mtu = MLX5E_SW2HW_MTU(priv, mtu);
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

	*mtu = MLX5E_HW2SW_MTU(priv, hw_mtu);
}

static int mlx5e_set_dev_port_mtu(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
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
	int nch = priv->channels.params.num_channels;
	int ntc = priv->channels.params.num_tc;
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

static void mlx5e_build_channels_tx_maps(struct mlx5e_priv *priv)
{
	struct mlx5e_channel *c;
	struct mlx5e_txqsq *sq;
	int i, tc;

	for (i = 0; i < priv->channels.num; i++)
		for (tc = 0; tc < priv->profile->max_tc; tc++)
			priv->channel_tc2txq[i][tc] = i + tc * priv->channels.num;

	for (i = 0; i < priv->channels.num; i++) {
		c = priv->channels.c[i];
		for (tc = 0; tc < c->num_tc; tc++) {
			sq = &c->sq[tc];
			priv->txq2sq[sq->txq_ix] = sq;
		}
	}
}

void mlx5e_activate_priv_channels(struct mlx5e_priv *priv)
{
	int num_txqs = priv->channels.num * priv->channels.params.num_tc;
	struct net_device *netdev = priv->netdev;

	mlx5e_netdev_set_tcs(netdev);
	netif_set_real_num_tx_queues(netdev, num_txqs);
	netif_set_real_num_rx_queues(netdev, priv->channels.num);

	mlx5e_build_channels_tx_maps(priv);
	mlx5e_activate_channels(&priv->channels);
	netif_tx_start_all_queues(priv->netdev);

	if (MLX5_VPORT_MANAGER(priv->mdev))
		mlx5e_add_sqs_fwd_rules(priv);

	mlx5e_wait_channels_min_rx_wqes(&priv->channels);
	mlx5e_redirect_rqts_to_channels(priv, &priv->channels);
}

void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv)
{
	mlx5e_redirect_rqts_to_drop(priv);

	if (MLX5_VPORT_MANAGER(priv->mdev))
		mlx5e_remove_sqs_fwd_rules(priv);

	/* FIXME: This is a W/A only for tx timeout watch dog false alarm when
	 * polling for inactive tx queues.
	 */
	netif_tx_stop_all_queues(priv->netdev);
	netif_tx_disable(priv->netdev);
	mlx5e_deactivate_channels(&priv->channels);
}

void mlx5e_switch_priv_channels(struct mlx5e_priv *priv,
				struct mlx5e_channels *new_chs,
				mlx5e_fp_hw_modify hw_modify)
{
	struct net_device *netdev = priv->netdev;
	int new_num_txqs;
	int carrier_ok;
	new_num_txqs = new_chs->num * new_chs->params.num_tc;

	carrier_ok = netif_carrier_ok(netdev);
	netif_carrier_off(netdev);

	if (new_num_txqs < netdev->real_num_tx_queues)
		netif_set_real_num_tx_queues(netdev, new_num_txqs);

	mlx5e_deactivate_priv_channels(priv);
	mlx5e_close_channels(&priv->channels);

	priv->channels = *new_chs;

	/* New channels are ready to roll, modify HW settings if needed */
	if (hw_modify)
		hw_modify(priv);

	mlx5e_refresh_tirs(priv, false);
	mlx5e_activate_priv_channels(priv);

	/* return carrier back if needed */
	if (carrier_ok)
		netif_carrier_on(netdev);
}

int mlx5e_open_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	err = mlx5e_open_channels(priv, &priv->channels);
	if (err)
		goto err_clear_state_opened_flag;

	mlx5e_refresh_tirs(priv, false);
	mlx5e_activate_priv_channels(priv);
	if (priv->profile->update_carrier)
		priv->profile->update_carrier(priv);
	mlx5e_timestamp_init(priv);

	if (priv->profile->update_stats)
		queue_delayed_work(priv->wq, &priv->update_stats_work, 0);

	return 0;

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
	if (!err)
		mlx5_set_port_admin_status(priv->mdev, MLX5_PORT_UP);
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
	netif_carrier_off(priv->netdev);
	mlx5e_deactivate_priv_channels(priv);
	mlx5e_close_channels(&priv->channels);

	return 0;
}

int mlx5e_close(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	if (!netif_device_present(netdev))
		return -ENODEV;

	mutex_lock(&priv->state_lock);
	mlx5_set_port_admin_status(priv->mdev, MLX5_PORT_DOWN);
	err = mlx5e_close_locked(netdev);
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_alloc_drop_rq(struct mlx5_core_dev *mdev,
			       struct mlx5e_rq *rq,
			       struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int err;

	param->wq.db_numa_node = param->wq.buf_numa_node;

	err = mlx5_wq_ll_create(mdev, &param->wq, rqc_wq, &rq->wq,
				&rq->wq_ctrl);
	if (err)
		return err;

	rq->mdev = mdev;

	return 0;
}

static int mlx5e_alloc_drop_cq(struct mlx5_core_dev *mdev,
			       struct mlx5e_cq *cq,
			       struct mlx5e_cq_param *param)
{
	return mlx5e_alloc_cq_common(mdev, param, cq);
}

static int mlx5e_open_drop_rq(struct mlx5_core_dev *mdev,
			      struct mlx5e_rq *drop_rq)
{
	struct mlx5e_cq_param cq_param = {};
	struct mlx5e_rq_param rq_param = {};
	struct mlx5e_cq *cq = &drop_rq->cq;
	int err;

	mlx5e_build_drop_rq_param(&rq_param);

	err = mlx5e_alloc_drop_cq(mdev, cq, &cq_param);
	if (err)
		return err;

	err = mlx5e_create_cq(cq, &cq_param);
	if (err)
		goto err_free_cq;

	err = mlx5e_alloc_drop_rq(mdev, drop_rq, &rq_param);
	if (err)
		goto err_destroy_cq;

	err = mlx5e_create_rq(drop_rq, &rq_param);
	if (err)
		goto err_free_rq;

	return 0;

err_free_rq:
	mlx5e_free_rq(drop_rq);

err_destroy_cq:
	mlx5e_destroy_cq(cq);

err_free_cq:
	mlx5e_free_cq(cq);

	return err;
}

static void mlx5e_close_drop_rq(struct mlx5e_rq *drop_rq)
{
	mlx5e_destroy_rq(drop_rq);
	mlx5e_free_rq(drop_rq);
	mlx5e_destroy_cq(&drop_rq->cq);
	mlx5e_free_cq(&drop_rq->cq);
}

int mlx5e_create_tis(struct mlx5_core_dev *mdev, int tc,
		     u32 underlay_qpn, u32 *tisn)
{
	u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {0};
	void *tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(tisc, tisc, prio, tc << 1);
	MLX5_SET(tisc, tisc, underlay_qpn, underlay_qpn);
	MLX5_SET(tisc, tisc, transport_domain, mdev->mlx5e_res.td.tdn);

	if (mlx5_lag_is_lacp_owner(mdev))
		MLX5_SET(tisc, tisc, strict_lag_tx_port_affinity, 1);

	return mlx5_core_create_tis(mdev, in, sizeof(in), tisn);
}

void mlx5e_destroy_tis(struct mlx5_core_dev *mdev, u32 tisn)
{
	mlx5_core_destroy_tis(mdev, tisn);
}

int mlx5e_create_tises(struct mlx5e_priv *priv)
{
	int err;
	int tc;

	for (tc = 0; tc < priv->profile->max_tc; tc++) {
		err = mlx5e_create_tis(priv->mdev, tc, 0, &priv->tisn[tc]);
		if (err)
			goto err_close_tises;
	}

	return 0;

err_close_tises:
	for (tc--; tc >= 0; tc--)
		mlx5e_destroy_tis(priv->mdev, priv->tisn[tc]);

	return err;
}

void mlx5e_cleanup_nic_tx(struct mlx5e_priv *priv)
{
	int tc;

	for (tc = 0; tc < priv->profile->max_tc; tc++)
		mlx5e_destroy_tis(priv->mdev, priv->tisn[tc]);
}

static void mlx5e_build_indir_tir_ctx(struct mlx5e_priv *priv,
				      enum mlx5e_traffic_types tt,
				      u32 *tirc)
{
	MLX5_SET(tirc, tirc, transport_domain, priv->mdev->mlx5e_res.td.tdn);

	mlx5e_build_tir_ctx_lro(&priv->channels.params, tirc);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table, priv->indir_rqt.rqtn);
	mlx5e_build_indir_tir_ctx_hash(&priv->channels.params, tt, tirc, false);
}

static void mlx5e_build_direct_tir_ctx(struct mlx5e_priv *priv, u32 rqtn, u32 *tirc)
{
	MLX5_SET(tirc, tirc, transport_domain, priv->mdev->mlx5e_res.td.tdn);

	mlx5e_build_tir_ctx_lro(&priv->channels.params, tirc);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table, rqtn);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_INVERTED_XOR8);
}

int mlx5e_create_indirect_tirs(struct mlx5e_priv *priv)
{
	struct mlx5e_tir *tir;
	void *tirc;
	int inlen;
	int i = 0;
	int err;
	u32 *in;
	int tt;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		memset(in, 0, inlen);
		tir = &priv->indir_tir[tt];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_indir_tir_ctx(priv, tt, tirc);
		err = mlx5e_create_tir(priv->mdev, tir, in, inlen);
		if (err) {
			mlx5_core_warn(priv->mdev, "create indirect tirs failed, %d\n", err);
			goto err_destroy_inner_tirs;
		}
	}

	if (!mlx5e_tunnel_inner_ft_supported(priv->mdev))
		goto out;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++) {
		memset(in, 0, inlen);
		tir = &priv->inner_indir_tir[i];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_inner_indir_tir_ctx(priv, i, tirc);
		err = mlx5e_create_tir(priv->mdev, tir, in, inlen);
		if (err) {
			mlx5_core_warn(priv->mdev, "create inner indirect tirs failed, %d\n", err);
			goto err_destroy_inner_tirs;
		}
	}

out:
	kvfree(in);

	return 0;

err_destroy_inner_tirs:
	for (i--; i >= 0; i--)
		mlx5e_destroy_tir(priv->mdev, &priv->inner_indir_tir[i]);

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
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	for (ix = 0; ix < nch; ix++) {
		memset(in, 0, inlen);
		tir = &priv->direct_tir[ix];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_direct_tir_ctx(priv, priv->direct_tir[ix].rqt.rqtn, tirc);
		err = mlx5e_create_tir(priv->mdev, tir, in, inlen);
		if (err)
			goto err_destroy_ch_tirs;
	}

	kvfree(in);

	return 0;

err_destroy_ch_tirs:
	mlx5_core_warn(priv->mdev, "create direct tirs failed, %d\n", err);
	for (ix--; ix >= 0; ix--)
		mlx5e_destroy_tir(priv->mdev, &priv->direct_tir[ix]);

	kvfree(in);

	return err;
}

void mlx5e_destroy_indirect_tirs(struct mlx5e_priv *priv)
{
	int i;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->indir_tir[i]);

	if (!mlx5e_tunnel_inner_ft_supported(priv->mdev))
		return;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->inner_indir_tir[i]);
}

void mlx5e_destroy_direct_tirs(struct mlx5e_priv *priv)
{
	int nch = priv->profile->max_nch(priv->mdev);
	int i;

	for (i = 0; i < nch; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->direct_tir[i]);
}

static int mlx5e_modify_channels_scatter_fcs(struct mlx5e_channels *chs, bool enable)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		err = mlx5e_modify_rq_scatter_fcs(&chs->c[i]->rq, enable);
		if (err)
			return err;
	}

	return 0;
}

static int mlx5e_modify_channels_vsd(struct mlx5e_channels *chs, bool vsd)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		err = mlx5e_modify_rq_vsd(&chs->c[i]->rq, vsd);
		if (err)
			return err;
	}

	return 0;
}

static int mlx5e_setup_tc_mqprio(struct net_device *netdev,
				 struct tc_mqprio_qopt *mqprio)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_channels new_channels = {};
	u8 tc = mqprio->num_tc;
	int err = 0;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (tc && tc != MLX5E_MAX_NUM_TC)
		return -EINVAL;

	mutex_lock(&priv->state_lock);

	new_channels.params = priv->channels.params;
	new_channels.params.num_tc = tc ? tc : 1;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		priv->channels.params = new_channels.params;
		goto out;
	}

	err = mlx5e_open_channels(priv, &new_channels);
	if (err)
		goto out;

	mlx5e_switch_priv_channels(priv, &new_channels, NULL);
out:
	mutex_unlock(&priv->state_lock);
	return err;
}

#ifdef CONFIG_MLX5_ESWITCH
static int mlx5e_setup_tc_cls_flower(struct net_device *dev,
				     struct tc_cls_flower_offload *cls_flower)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (!is_classid_clsact_ingress(cls_flower->common.classid) ||
	    cls_flower->common.chain_index)
		return -EOPNOTSUPP;

	switch (cls_flower->command) {
	case TC_CLSFLOWER_REPLACE:
		return mlx5e_configure_flower(priv, cls_flower);
	case TC_CLSFLOWER_DESTROY:
		return mlx5e_delete_flower(priv, cls_flower);
	case TC_CLSFLOWER_STATS:
		return mlx5e_stats_flower(priv, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}
#endif

static int mlx5e_setup_tc(struct net_device *dev, enum tc_setup_type type,
			  void *type_data)
{
	switch (type) {
#ifdef CONFIG_MLX5_ESWITCH
	case TC_SETUP_CLSFLOWER:
		return mlx5e_setup_tc_cls_flower(dev, type_data);
#endif
	case TC_SETUP_MQPRIO:
		return mlx5e_setup_tc_mqprio(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static void
mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_sw_stats *sstats = &priv->stats.sw;
	struct mlx5e_vport_stats *vstats = &priv->stats.vport;
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;

	if (mlx5e_is_uplink_rep(priv)) {
		stats->rx_packets = PPORT_802_3_GET(pstats, a_frames_received_ok);
		stats->rx_bytes   = PPORT_802_3_GET(pstats, a_octets_received_ok);
		stats->tx_packets = PPORT_802_3_GET(pstats, a_frames_transmitted_ok);
		stats->tx_bytes   = PPORT_802_3_GET(pstats, a_octets_transmitted_ok);
	} else {
		stats->rx_packets = sstats->rx_packets;
		stats->rx_bytes   = sstats->rx_bytes;
		stats->tx_packets = sstats->tx_packets;
		stats->tx_bytes   = sstats->tx_bytes;
		stats->tx_dropped = sstats->tx_queue_dropped;
	}

	stats->rx_dropped = priv->stats.qcnt.rx_out_of_buffer;

	stats->rx_length_errors =
		PPORT_802_3_GET(pstats, a_in_range_length_errors) +
		PPORT_802_3_GET(pstats, a_out_of_range_length_field) +
		PPORT_802_3_GET(pstats, a_frame_too_long_errors);
	stats->rx_crc_errors =
		PPORT_802_3_GET(pstats, a_frame_check_sequence_errors);
	stats->rx_frame_errors = PPORT_802_3_GET(pstats, a_alignment_errors);
	stats->tx_aborted_errors = PPORT_2863_GET(pstats, if_out_discards);
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
			   stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_carrier_errors;

	/* vport multicast also counts packets that are dropped due to steering
	 * or rx out of buffer
	 */
	stats->multicast =
		VPORT_COUNTER_GET(vstats, received_eth_multicast.packets);
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
	struct mlx5e_channels new_channels = {};
	int err = 0;
	bool reset;

	mutex_lock(&priv->state_lock);

	reset = (priv->channels.params.rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST);
	reset = reset && test_bit(MLX5E_STATE_OPENED, &priv->state);

	new_channels.params = priv->channels.params;
	new_channels.params.lro_en = enable;

	if (!reset) {
		priv->channels.params = new_channels.params;
		err = mlx5e_modify_tirs_lro(priv);
		goto out;
	}

	err = mlx5e_open_channels(priv, &new_channels);
	if (err)
		goto out;

	mlx5e_switch_priv_channels(priv, &new_channels, mlx5e_modify_tirs_lro);
out:
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

static int set_feature_rx_fcs(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mutex_lock(&priv->state_lock);

	priv->channels.params.scatter_fcs_en = enable;
	err = mlx5e_modify_channels_scatter_fcs(&priv->channels, enable);
	if (err)
		priv->channels.params.scatter_fcs_en = !enable;

	mutex_unlock(&priv->state_lock);

	return err;
}

static int set_feature_rx_vlan(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

	mutex_lock(&priv->state_lock);

	priv->channels.params.vlan_strip_disable = !enable;
	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	err = mlx5e_modify_channels_vsd(&priv->channels, !enable);
	if (err)
		priv->channels.params.vlan_strip_disable = enable;

unlock:
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
		netdev_err(netdev, "%s feature %pNF failed, err %d\n",
			   enable ? "Enable" : "Disable", &feature, err);
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
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_RXFCS,
				    set_feature_rx_fcs);
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_HW_VLAN_CTAG_RX,
				    set_feature_rx_vlan);
#ifdef CONFIG_RFS_ACCEL
	err |= mlx5e_handle_feature(netdev, features, NETIF_F_NTUPLE,
				    set_feature_arfs);
#endif

	return err ? -EINVAL : 0;
}

static int mlx5e_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_channels new_channels = {};
	int curr_mtu;
	int err = 0;
	bool reset;

	mutex_lock(&priv->state_lock);

	reset = !priv->channels.params.lro_en &&
		(priv->channels.params.rq_wq_type !=
		 MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ);

	reset = reset && test_bit(MLX5E_STATE_OPENED, &priv->state);

	curr_mtu    = netdev->mtu;
	netdev->mtu = new_mtu;

	if (!reset) {
		mlx5e_set_dev_port_mtu(priv);
		goto out;
	}

	new_channels.params = priv->channels.params;
	err = mlx5e_open_channels(priv, &new_channels);
	if (err) {
		netdev->mtu = curr_mtu;
		goto out;
	}

	mlx5e_switch_priv_channels(priv, &new_channels, mlx5e_set_dev_port_mtu);

out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return mlx5e_hwstamp_set(priv, ifr);
	case SIOCGHWTSTAMP:
		return mlx5e_hwstamp_get(priv, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

#ifdef CONFIG_MLX5_ESWITCH
static int mlx5e_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_mac(mdev->priv.eswitch, vf + 1, mac);
}

static int mlx5e_set_vf_vlan(struct net_device *dev, int vf, u16 vlan, u8 qos,
			     __be16 vlan_proto)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

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

static int mlx5e_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate,
			     int max_tx_rate)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_rate(mdev->priv.eswitch, vf + 1,
					   max_tx_rate, min_tx_rate);
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
#endif

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

static netdev_features_t mlx5e_tunnel_features_check(struct mlx5e_priv *priv,
						     struct sk_buff *skb,
						     netdev_features_t features)
{
	struct udphdr *udph;
	u8 proto;
	u16 port;

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

	switch (proto) {
	case IPPROTO_GRE:
		return features;
	case IPPROTO_UDP:
		udph = udp_hdr(skb);
		port = be16_to_cpu(udph->dest);

		/* Verify if UDP port is being offloaded by HW */
		if (mlx5e_vxlan_lookup_port(priv, port))
			return features;
	}

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

#ifdef CONFIG_MLX5_EN_IPSEC
	if (mlx5e_ipsec_feature_check(skb, netdev, features))
		return features;
#endif

	/* Validate if the tunneled packet is being offloaded by HW */
	if (skb->encapsulation &&
	    (features & NETIF_F_CSUM_MASK || features & NETIF_F_GSO_MASK))
		return mlx5e_tunnel_features_check(priv, skb, features);

	return features;
}

static void mlx5e_tx_timeout(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	bool sched_work = false;
	int i;

	netdev_err(dev, "TX timeout detected\n");

	for (i = 0; i < priv->channels.num * priv->channels.params.num_tc; i++) {
		struct mlx5e_txqsq *sq = priv->txq2sq[i];

		if (!netif_xmit_stopped(netdev_get_tx_queue(dev, i)))
			continue;
		sched_work = true;
		clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
		netdev_err(dev, "TX timeout on queue: %d, SQ: 0x%x, CQ: 0x%x, SQ Cons: 0x%x SQ Prod: 0x%x\n",
			   i, sq->sqn, sq->cq.mcq.cqn, sq->cc, sq->pc);
	}

	if (sched_work && test_bit(MLX5E_STATE_OPENED, &priv->state))
		schedule_work(&priv->tx_timeout_work);
}

static int mlx5e_xdp_set(struct net_device *netdev, struct bpf_prog *prog)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct bpf_prog *old_prog;
	int err = 0;
	bool reset, was_opened;
	int i;

	mutex_lock(&priv->state_lock);

	if ((netdev->features & NETIF_F_LRO) && prog) {
		netdev_warn(netdev, "can't set XDP while LRO is on, disable LRO first\n");
		err = -EINVAL;
		goto unlock;
	}

	if ((netdev->features & NETIF_F_HW_ESP) && prog) {
		netdev_warn(netdev, "can't set XDP with IPSec offload\n");
		err = -EINVAL;
		goto unlock;
	}

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	/* no need for full reset when exchanging programs */
	reset = (!priv->channels.params.xdp_prog || !prog);

	if (was_opened && reset)
		mlx5e_close_locked(netdev);
	if (was_opened && !reset) {
		/* num_channels is invariant here, so we can take the
		 * batched reference right upfront.
		 */
		prog = bpf_prog_add(prog, priv->channels.num);
		if (IS_ERR(prog)) {
			err = PTR_ERR(prog);
			goto unlock;
		}
	}

	/* exchange programs, extra prog reference we got from caller
	 * as long as we don't fail from this point onwards.
	 */
	old_prog = xchg(&priv->channels.params.xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	if (reset) /* change RQ type according to priv->xdp_prog */
		mlx5e_set_rq_params(priv->mdev, &priv->channels.params);

	if (was_opened && reset)
		mlx5e_open_locked(netdev);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state) || reset)
		goto unlock;

	/* exchanging programs w/o reset, we update ref counts on behalf
	 * of the channels RQs here.
	 */
	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		clear_bit(MLX5E_RQ_STATE_ENABLED, &c->rq.state);
		napi_synchronize(&c->napi);
		/* prevent mlx5e_poll_rx_cq from accessing rq->xdp_prog */

		old_prog = xchg(&c->rq.xdp_prog, prog);

		set_bit(MLX5E_RQ_STATE_ENABLED, &c->rq.state);
		/* napi_schedule in case we have missed anything */
		napi_schedule(&c->napi);

		if (old_prog)
			bpf_prog_put(old_prog);
	}

unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

static u32 mlx5e_xdp_query(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	const struct bpf_prog *xdp_prog;
	u32 prog_id = 0;

	mutex_lock(&priv->state_lock);
	xdp_prog = priv->channels.params.xdp_prog;
	if (xdp_prog)
		prog_id = xdp_prog->aux->id;
	mutex_unlock(&priv->state_lock);

	return prog_id;
}

static int mlx5e_xdp(struct net_device *dev, struct netdev_xdp *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return mlx5e_xdp_set(dev, xdp->prog);
	case XDP_QUERY_PROG:
		xdp->prog_id = mlx5e_xdp_query(dev);
		xdp->prog_attached = !!xdp->prog_id;
		return 0;
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/* Fake "interrupt" called by netpoll (eg netconsole) to send skbs without
 * reenabling interrupts.
 */
static void mlx5e_netpoll(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_channels *chs = &priv->channels;

	int i;

	for (i = 0; i < chs->num; i++)
		napi_schedule(&chs->c[i]->napi);
}
#endif

static const struct net_device_ops mlx5e_netdev_ops = {
	.ndo_open                = mlx5e_open,
	.ndo_stop                = mlx5e_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_setup_tc            = mlx5e_setup_tc,
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
	.ndo_udp_tunnel_add      = mlx5e_add_vxlan_port,
	.ndo_udp_tunnel_del      = mlx5e_del_vxlan_port,
	.ndo_features_check      = mlx5e_features_check,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	 = mlx5e_rx_flow_steer,
#endif
	.ndo_tx_timeout          = mlx5e_tx_timeout,
	.ndo_xdp		 = mlx5e_xdp,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller     = mlx5e_netpoll,
#endif
#ifdef CONFIG_MLX5_ESWITCH
	/* SRIOV E-Switch NDOs */
	.ndo_set_vf_mac          = mlx5e_set_vf_mac,
	.ndo_set_vf_vlan         = mlx5e_set_vf_vlan,
	.ndo_set_vf_spoofchk     = mlx5e_set_vf_spoofchk,
	.ndo_set_vf_trust        = mlx5e_set_vf_trust,
	.ndo_set_vf_rate         = mlx5e_set_vf_rate,
	.ndo_get_vf_config       = mlx5e_get_vf_config,
	.ndo_set_vf_link_state   = mlx5e_set_vf_link_state,
	.ndo_get_vf_stats        = mlx5e_get_vf_stats,
	.ndo_has_offload_stats	 = mlx5e_has_offload_stats,
	.ndo_get_offload_stats	 = mlx5e_get_offload_stats,
#endif
};

static int mlx5e_check_required_hca_cap(struct mlx5_core_dev *mdev)
{
	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH)
		return -EOPNOTSUPP;
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
		return -EOPNOTSUPP;
	}
	if (!MLX5_CAP_ETH(mdev, self_lb_en_modifiable))
		mlx5_core_warn(mdev, "Self loop back prevention is not supported\n");
	if (!MLX5_CAP_GEN(mdev, cq_moderation))
		mlx5_core_warn(mdev, "CQ moderation is not supported\n");

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

static bool hw_lro_heuristic(u32 link_speed, u32 pci_bw)
{
	return !(link_speed && pci_bw &&
		 (pci_bw <= 16000) && (pci_bw < link_speed));
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

	if (params->rx_am_enabled)
		params->rx_cq_moderation =
			mlx5e_am_get_def_profile(params->rx_cq_period_mode);

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_BASED_MODER,
			params->rx_cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE);
}

u32 mlx5e_choose_lro_timeout(struct mlx5_core_dev *mdev, u32 wanted_timeout)
{
	int i;

	/* The supported periods are organized in ascending order */
	for (i = 0; i < MLX5E_LRO_TIMEOUT_ARR_SIZE - 1; i++)
		if (MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]) >= wanted_timeout)
			break;

	return MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]);
}

void mlx5e_build_nic_params(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params,
			    u16 max_channels)
{
	u8 cq_period_mode = 0;
	u32 link_speed = 0;
	u32 pci_bw = 0;

	params->num_channels = max_channels;
	params->num_tc       = 1;

	mlx5e_get_max_linkspeed(mdev, &link_speed);
	mlx5e_get_pci_bw(mdev, &pci_bw);
	mlx5_core_dbg(mdev, "Max link speed = %d, PCI BW = %d\n",
		      link_speed, pci_bw);

	/* SQ */
	params->log_sq_size = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;

	/* set CQE compression */
	params->rx_cqe_compress_def = false;
	if (MLX5_CAP_GEN(mdev, cqe_compression) &&
	    MLX5_CAP_GEN(mdev, vport_group_manager))
		params->rx_cqe_compress_def = cqe_compress_heuristic(link_speed, pci_bw);

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS, params->rx_cqe_compress_def);

	/* RQ */
	mlx5e_set_rq_params(mdev, params);

	/* HW LRO */

	/* TODO: && MLX5_CAP_ETH(mdev, lro_cap) */
	if (params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		params->lro_en = hw_lro_heuristic(link_speed, pci_bw);
	params->lro_timeout = mlx5e_choose_lro_timeout(mdev, MLX5E_DEFAULT_LRO_TIMEOUT);

	/* CQ moderation params */
	cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
			MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
			MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
	params->rx_am_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(params, cq_period_mode);

	params->tx_cq_moderation.usec = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC;
	params->tx_cq_moderation.pkts = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS;

	/* TX inline */
	params->tx_max_inline = mlx5e_get_max_inline_cap(mdev);
	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);
	if (params->tx_min_inline_mode == MLX5_INLINE_MODE_NONE &&
	    !MLX5_CAP_ETH(mdev, wqe_vlan_insert))
		params->tx_min_inline_mode = MLX5_INLINE_MODE_L2;

	/* RSS */
	params->rss_hfunc = ETH_RSS_HASH_XOR;
	netdev_rss_key_fill(params->toeplitz_hash_key, sizeof(params->toeplitz_hash_key));
	mlx5e_build_default_indir_rqt(params->indirection_rqt,
				      MLX5E_INDIR_RQT_SIZE, max_channels);
}

static void mlx5e_build_nic_netdev_priv(struct mlx5_core_dev *mdev,
					struct net_device *netdev,
					const struct mlx5e_profile *profile,
					void *ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	priv->mdev        = mdev;
	priv->netdev      = netdev;
	priv->profile     = profile;
	priv->ppriv       = ppriv;
	priv->hard_mtu = MLX5E_ETH_HARD_MTU;

	mlx5e_build_nic_params(mdev, &priv->channels.params, profile->max_nch(mdev));

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

#if IS_ENABLED(CONFIG_NET_SWITCHDEV) && IS_ENABLED(CONFIG_MLX5_ESWITCH)
static const struct switchdev_ops mlx5e_switchdev_ops = {
	.switchdev_port_attr_get	= mlx5e_attr_get,
};
#endif

static void mlx5e_build_nic_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool fcs_supported;
	bool fcs_enabled;

	SET_NETDEV_DEV(netdev, &mdev->pdev->dev);

	netdev->netdev_ops = &mlx5e_netdev_ops;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (MLX5_CAP_GEN(mdev, vport_group_manager) && MLX5_CAP_GEN(mdev, qos))
		netdev->dcbnl_ops = &mlx5e_dcbnl_ops;
#endif

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

	if (mlx5e_vxlan_allowed(mdev) || MLX5_CAP_ETH(mdev, tunnel_stateless_gre)) {
		netdev->hw_features     |= NETIF_F_GSO_PARTIAL;
		netdev->hw_enc_features |= NETIF_F_IP_CSUM;
		netdev->hw_enc_features |= NETIF_F_IPV6_CSUM;
		netdev->hw_enc_features |= NETIF_F_TSO;
		netdev->hw_enc_features |= NETIF_F_TSO6;
		netdev->hw_enc_features |= NETIF_F_GSO_PARTIAL;
	}

	if (mlx5e_vxlan_allowed(mdev)) {
		netdev->hw_features     |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
		netdev->hw_enc_features |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
		netdev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM;
	}

	if (MLX5_CAP_ETH(mdev, tunnel_stateless_gre)) {
		netdev->hw_features     |= NETIF_F_GSO_GRE |
					   NETIF_F_GSO_GRE_CSUM;
		netdev->hw_enc_features |= NETIF_F_GSO_GRE |
					   NETIF_F_GSO_GRE_CSUM;
		netdev->gso_partial_features |= NETIF_F_GSO_GRE |
						NETIF_F_GSO_GRE_CSUM;
	}

	mlx5_query_port_fcs(mdev, &fcs_supported, &fcs_enabled);

	if (fcs_supported)
		netdev->hw_features |= NETIF_F_RXALL;

	if (MLX5_CAP_ETH(mdev, scatter_fcs))
		netdev->hw_features |= NETIF_F_RXFCS;

	netdev->features          = netdev->hw_features;
	if (!priv->channels.params.lro_en)
		netdev->features  &= ~NETIF_F_LRO;

	if (fcs_enabled)
		netdev->features  &= ~NETIF_F_RXALL;

	if (!priv->channels.params.scatter_fcs_en)
		netdev->features  &= ~NETIF_F_RXFCS;

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

#if IS_ENABLED(CONFIG_NET_SWITCHDEV) && IS_ENABLED(CONFIG_MLX5_ESWITCH)
	if (MLX5_VPORT_MANAGER(mdev))
		netdev->switchdev_ops = &mlx5e_switchdev_ops;
#endif

	mlx5e_ipsec_build_netdev(priv);
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

static void mlx5e_nic_init(struct mlx5_core_dev *mdev,
			   struct net_device *netdev,
			   const struct mlx5e_profile *profile,
			   void *ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mlx5e_build_nic_netdev_priv(mdev, netdev, profile, ppriv);
	err = mlx5e_ipsec_init(priv);
	if (err)
		mlx5_core_err(mdev, "IPSec initialization failed, %d\n", err);
	mlx5e_build_nic_netdev(netdev);
	mlx5e_vxlan_init(priv);
}

static void mlx5e_nic_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_ipsec_cleanup(priv);
	mlx5e_vxlan_cleanup(priv);

	if (priv->channels.params.xdp_prog)
		bpf_prog_put(priv->channels.params.xdp_prog);
}

static int mlx5e_init_nic_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5e_create_indirect_rqt(priv);
	if (err)
		return err;

	err = mlx5e_create_direct_rqts(priv);
	if (err)
		goto err_destroy_indirect_rqts;

	err = mlx5e_create_indirect_tirs(priv);
	if (err)
		goto err_destroy_direct_rqts;

	err = mlx5e_create_direct_tirs(priv);
	if (err)
		goto err_destroy_indirect_tirs;

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
	mlx5e_destroy_direct_rqts(priv);
err_destroy_indirect_rqts:
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
	return err;
}

static void mlx5e_cleanup_nic_rx(struct mlx5e_priv *priv)
{
	mlx5e_tc_cleanup(priv);
	mlx5e_destroy_flow_steering(priv);
	mlx5e_destroy_direct_tirs(priv);
	mlx5e_destroy_indirect_tirs(priv);
	mlx5e_destroy_direct_rqts(priv);
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
	mlx5e_dcbnl_initialize(priv);
#endif
	return 0;
}

static void mlx5e_nic_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 max_mtu;

	mlx5e_init_l2_addr(priv);

	/* Marking the link as currently not needed by the Driver */
	if (!netif_running(netdev))
		mlx5_set_port_admin_status(mdev, MLX5_PORT_DOWN);

	/* MTU range: 68 - hw-specific max */
	netdev->min_mtu = ETH_MIN_MTU;
	mlx5_query_port_max_mtu(priv->mdev, &max_mtu, 1);
	netdev->max_mtu = MLX5E_HW2SW_MTU(priv, max_mtu);
	mlx5e_set_dev_port_mtu(priv);

	mlx5_lag_add(mdev, netdev);

	mlx5e_enable_async_events(priv);

	if (MLX5_VPORT_MANAGER(priv->mdev))
		mlx5e_register_vport_reps(priv);

	if (netdev->reg_state != NETREG_REGISTERED)
		return;

	/* Device already registered: sync netdev system state */
	if (mlx5e_vxlan_allowed(mdev)) {
		rtnl_lock();
		udp_tunnel_get_rx_info(netdev);
		rtnl_unlock();
	}

	queue_work(priv->wq, &priv->set_rx_mode_work);

	rtnl_lock();
	if (netif_running(netdev))
		mlx5e_open(netdev);
	netif_device_attach(netdev);
	rtnl_unlock();
}

static void mlx5e_nic_disable(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	rtnl_lock();
	if (netif_running(priv->netdev))
		mlx5e_close(priv->netdev);
	netif_device_detach(priv->netdev);
	rtnl_unlock();

	queue_work(priv->wq, &priv->set_rx_mode_work);

	if (MLX5_VPORT_MANAGER(priv->mdev))
		mlx5e_unregister_vport_reps(priv);

	mlx5e_disable_async_events(priv);
	mlx5_lag_remove(mdev);
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
	.update_stats	   = mlx5e_update_ndo_stats,
	.max_nch	   = mlx5e_get_max_num_channels,
	.update_carrier	   = mlx5e_update_carrier,
	.rx_handlers.handle_rx_cqe       = mlx5e_handle_rx_cqe,
	.rx_handlers.handle_rx_cqe_mpwqe = mlx5e_handle_rx_cqe_mpwrq,
	.max_tc		   = MLX5E_MAX_NUM_TC,
};

/* mlx5e generic netdev management API (move to en_common.c) */

struct net_device *mlx5e_create_netdev(struct mlx5_core_dev *mdev,
				       const struct mlx5e_profile *profile,
				       void *ppriv)
{
	int nch = profile->max_nch(mdev);
	struct net_device *netdev;
	struct mlx5e_priv *priv;

	netdev = alloc_etherdev_mqs(sizeof(struct mlx5e_priv),
				    nch * profile->max_tc,
				    nch);
	if (!netdev) {
		mlx5_core_err(mdev, "alloc_etherdev_mqs() failed\n");
		return NULL;
	}

#ifdef CONFIG_RFS_ACCEL
	netdev->rx_cpu_rmap = mdev->rmap;
#endif

	profile->init(mdev, netdev, profile, ppriv);

	netif_carrier_off(netdev);

	priv = netdev_priv(netdev);

	priv->wq = create_singlethread_workqueue("mlx5e");
	if (!priv->wq)
		goto err_cleanup_nic;

	return netdev;

err_cleanup_nic:
	if (profile->cleanup)
		profile->cleanup(priv);
	free_netdev(netdev);

	return NULL;
}

int mlx5e_attach_netdev(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	const struct mlx5e_profile *profile;
	int err;

	profile = priv->profile;
	clear_bit(MLX5E_STATE_DESTROYING, &priv->state);

	err = profile->init_tx(priv);
	if (err)
		goto out;

	err = mlx5e_open_drop_rq(mdev, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_cleanup_tx;
	}

	err = profile->init_rx(priv);
	if (err)
		goto err_close_drop_rq;

	mlx5e_create_q_counter(priv);

	if (profile->enable)
		profile->enable(priv);

	return 0;

err_close_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);

err_cleanup_tx:
	profile->cleanup_tx(priv);

out:
	return err;
}

void mlx5e_detach_netdev(struct mlx5e_priv *priv)
{
	const struct mlx5e_profile *profile = priv->profile;

	set_bit(MLX5E_STATE_DESTROYING, &priv->state);

	if (profile->disable)
		profile->disable(priv);
	flush_workqueue(priv->wq);

	mlx5e_destroy_q_counter(priv);
	profile->cleanup_rx(priv);
	mlx5e_close_drop_rq(&priv->drop_rq);
	profile->cleanup_tx(priv);
	cancel_delayed_work_sync(&priv->update_stats_work);
}

void mlx5e_destroy_netdev(struct mlx5e_priv *priv)
{
	const struct mlx5e_profile *profile = priv->profile;
	struct net_device *netdev = priv->netdev;

	destroy_workqueue(priv->wq);
	if (profile->cleanup)
		profile->cleanup(priv);
	free_netdev(netdev);
}

/* mlx5e_attach and mlx5e_detach scope should be only creating/destroying
 * hardware contexts and to connect it to the current netdev.
 */
static int mlx5e_attach(struct mlx5_core_dev *mdev, void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;
	struct net_device *netdev = priv->netdev;
	int err;

	if (netif_device_present(netdev))
		return 0;

	err = mlx5e_create_mdev_resources(mdev);
	if (err)
		return err;

	err = mlx5e_attach_netdev(priv);
	if (err) {
		mlx5e_destroy_mdev_resources(mdev);
		return err;
	}

	return 0;
}

static void mlx5e_detach(struct mlx5_core_dev *mdev, void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;
	struct net_device *netdev = priv->netdev;

	if (!netif_device_present(netdev))
		return;

	mlx5e_detach_netdev(priv);
	mlx5e_destroy_mdev_resources(mdev);
}

static void *mlx5e_add(struct mlx5_core_dev *mdev)
{
	struct net_device *netdev;
	void *rpriv = NULL;
	void *priv;
	int err;

	err = mlx5e_check_required_hca_cap(mdev);
	if (err)
		return NULL;

#ifdef CONFIG_MLX5_ESWITCH
	if (MLX5_VPORT_MANAGER(mdev)) {
		rpriv = mlx5e_alloc_nic_rep_priv(mdev);
		if (!rpriv) {
			mlx5_core_warn(mdev, "Failed to alloc NIC rep priv data\n");
			return NULL;
		}
	}
#endif

	netdev = mlx5e_create_netdev(mdev, &mlx5e_nic_profile, rpriv);
	if (!netdev) {
		mlx5_core_err(mdev, "mlx5e_create_netdev failed\n");
		goto err_free_rpriv;
	}

	priv = netdev_priv(netdev);

	err = mlx5e_attach(mdev, priv);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_attach failed, %d\n", err);
		goto err_destroy_netdev;
	}

	err = register_netdev(netdev);
	if (err) {
		mlx5_core_err(mdev, "register_netdev failed, %d\n", err);
		goto err_detach;
	}

	return priv;

err_detach:
	mlx5e_detach(mdev, priv);
err_destroy_netdev:
	mlx5e_destroy_netdev(priv);
err_free_rpriv:
	kfree(rpriv);
	return NULL;
}

static void mlx5e_remove(struct mlx5_core_dev *mdev, void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;
	void *ppriv = priv->ppriv;

	unregister_netdev(priv->netdev);
	mlx5e_detach(mdev, vpriv);
	mlx5e_destroy_netdev(priv);
	kfree(ppriv);
}

static void *mlx5e_get_netdev(void *vpriv)
{
	struct mlx5e_priv *priv = vpriv;

	return priv->netdev;
}

static struct mlx5_interface mlx5e_interface = {
	.add       = mlx5e_add,
	.remove    = mlx5e_remove,
	.attach    = mlx5e_attach,
	.detach    = mlx5e_detach,
	.event     = mlx5e_async_event,
	.protocol  = MLX5_INTERFACE_PROTOCOL_ETH,
	.get_dev   = mlx5e_get_netdev,
};

void mlx5e_init(void)
{
	mlx5e_ipsec_build_inverse_table();
	mlx5e_build_ptys2ethtool_map();
	mlx5_register_interface(&mlx5e_interface);
}

void mlx5e_cleanup(void)
{
	mlx5_unregister_interface(&mlx5e_interface);
}

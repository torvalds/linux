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
#include <net/geneve.h>
#include <linux/bpf.h>
#include <linux/if_bridge.h>
#include <net/page_pool.h>
#include <net/xdp_sock_drv.h>
#include "eswitch.h"
#include "en.h"
#include "en/txrx.h"
#include "en_tc.h"
#include "en_rep.h"
#include "en_accel/ipsec.h"
#include "en_accel/en_accel.h"
#include "en_accel/tls.h"
#include "accel/ipsec.h"
#include "accel/tls.h"
#include "lib/vxlan.h"
#include "lib/clock.h"
#include "en/port.h"
#include "en/xdp.h"
#include "lib/eq.h"
#include "en/monitor_stats.h"
#include "en/health.h"
#include "en/params.h"
#include "en/xsk/pool.h"
#include "en/xsk/setup.h"
#include "en/xsk/rx.h"
#include "en/xsk/tx.h"
#include "en/hv_vhca_stats.h"
#include "en/devlink.h"
#include "lib/mlx5.h"
#include "en/ptp.h"
#include "qos.h"
#include "en/trap.h"
#include "fpga/ipsec.h"

bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev)
{
	bool striding_rq_umr = MLX5_CAP_GEN(mdev, striding_rq) &&
		MLX5_CAP_GEN(mdev, umr_ptr_rlky) &&
		MLX5_CAP_ETH(mdev, reg_umr_sq);
	u16 max_wqe_sz_cap = MLX5_CAP_GEN(mdev, max_wqe_sz_sq);
	bool inline_umr = MLX5E_UMR_WQE_INLINE_SZ <= max_wqe_sz_cap;

	if (!striding_rq_umr)
		return false;
	if (!inline_umr) {
		mlx5_core_warn(mdev, "Cannot support Striding RQ: UMR WQE size (%d) exceeds maximum supported (%d).\n",
			       (int)MLX5E_UMR_WQE_INLINE_SZ, max_wqe_sz_cap);
		return false;
	}
	return true;
}

void mlx5e_init_rq_type_params(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params)
{
	params->log_rq_mtu_frames = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE;

	mlx5_core_info(mdev, "MLX5E: StrdRq(%d) RqSz(%ld) StrdSz(%ld) RxCqeCmprss(%d)\n",
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ,
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ ?
		       BIT(mlx5e_mpwqe_get_log_rq_size(params, NULL)) :
		       BIT(params->log_rq_mtu_frames),
		       BIT(mlx5e_mpwqe_get_log_stride_size(mdev, params, NULL)),
		       MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS));
}

bool mlx5e_striding_rq_possible(struct mlx5_core_dev *mdev,
				struct mlx5e_params *params)
{
	if (!mlx5e_check_fragmented_striding_rq_cap(mdev))
		return false;

	if (mlx5_fpga_is_ipsec_device(mdev))
		return false;

	if (params->xdp_prog) {
		/* XSK params are not considered here. If striding RQ is in use,
		 * and an XSK is being opened, mlx5e_rx_mpwqe_is_linear_skb will
		 * be called with the known XSK params.
		 */
		if (!mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL))
			return false;
	}

	return true;
}

void mlx5e_set_rq_type(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	params->rq_wq_type = mlx5e_striding_rq_possible(mdev, params) &&
		MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ) ?
		MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ :
		MLX5_WQ_TYPE_CYCLIC;
}

void mlx5e_update_carrier(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 port_state;

	port_state = mlx5_query_vport_state(mdev,
					    MLX5_VPORT_STATE_OP_MOD_VNIC_VPORT,
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

static void mlx5e_update_stats_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       update_stats_work);

	mutex_lock(&priv->state_lock);
	priv->profile->update_stats(priv);
	mutex_unlock(&priv->state_lock);
}

void mlx5e_queue_update_stats(struct mlx5e_priv *priv)
{
	if (!priv->profile->update_stats)
		return;

	if (unlikely(test_bit(MLX5E_STATE_DESTROYING, &priv->state)))
		return;

	queue_work(priv->wq, &priv->update_stats_work);
}

static int async_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, events_nb);
	struct mlx5_eqe   *eqe = data;

	if (event != MLX5_EVENT_TYPE_PORT_CHANGE)
		return NOTIFY_DONE;

	switch (eqe->sub_type) {
	case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
	case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
		queue_work(priv->wq, &priv->update_carrier_work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static void mlx5e_enable_async_events(struct mlx5e_priv *priv)
{
	priv->events_nb.notifier_call = async_event;
	mlx5_notifier_register(priv->mdev, &priv->events_nb);
}

static void mlx5e_disable_async_events(struct mlx5e_priv *priv)
{
	mlx5_notifier_unregister(priv->mdev, &priv->events_nb);
}

static int blocking_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, blocking_events_nb);
	int err;

	switch (event) {
	case MLX5_DRIVER_EVENT_TYPE_TRAP:
		err = mlx5e_handle_trap_event(priv, data);
		break;
	default:
		netdev_warn(priv->netdev, "Sync event: Unknown event %ld\n", event);
		err = -EINVAL;
	}
	return err;
}

static void mlx5e_enable_blocking_events(struct mlx5e_priv *priv)
{
	priv->blocking_events_nb.notifier_call = blocking_event;
	mlx5_blocking_notifier_register(priv->mdev, &priv->blocking_events_nb);
}

static void mlx5e_disable_blocking_events(struct mlx5e_priv *priv)
{
	mlx5_blocking_notifier_unregister(priv->mdev, &priv->blocking_events_nb);
}

static inline void mlx5e_build_umr_wqe(struct mlx5e_rq *rq,
				       struct mlx5e_icosq *sq,
				       struct mlx5e_umr_wqe *wqe)
{
	struct mlx5_wqe_ctrl_seg      *cseg = &wqe->ctrl;
	struct mlx5_wqe_umr_ctrl_seg *ucseg = &wqe->uctrl;
	u8 ds_cnt = DIV_ROUND_UP(MLX5E_UMR_WQE_INLINE_SZ, MLX5_SEND_WQE_DS);

	cseg->qpn_ds    = cpu_to_be32((sq->sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
				      ds_cnt);
	cseg->umr_mkey  = rq->mkey_be;

	ucseg->flags = MLX5_UMR_TRANSLATION_OFFSET_EN | MLX5_UMR_INLINE;
	ucseg->xlt_octowords =
		cpu_to_be16(MLX5_MTT_OCTW(MLX5_MPWRQ_PAGES_PER_WQE));
	ucseg->mkey_mask     = cpu_to_be64(MLX5_MKEY_MASK_FREE);
}

static int mlx5e_rq_alloc_mpwqe_info(struct mlx5e_rq *rq,
				     struct mlx5e_channel *c)
{
	int wq_sz = mlx5_wq_ll_get_size(&rq->mpwqe.wq);

	rq->mpwqe.info = kvzalloc_node(array_size(wq_sz,
						  sizeof(*rq->mpwqe.info)),
				       GFP_KERNEL, cpu_to_node(c->cpu));
	if (!rq->mpwqe.info)
		return -ENOMEM;

	mlx5e_build_umr_wqe(rq, &c->icosq, &rq->mpwqe.umr_wqe);

	return 0;
}

static int mlx5e_create_umr_mkey(struct mlx5_core_dev *mdev,
				 u64 npages, u8 page_shift,
				 struct mlx5_core_mkey *umr_mkey,
				 dma_addr_t filler_addr)
{
	struct mlx5_mtt *mtt;
	int inlen;
	void *mkc;
	u32 *in;
	int err;
	int i;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in) + sizeof(*mtt) * npages;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_MTT);
	mlx5e_mkey_set_relaxed_ordering(mdev, mkc);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mdev->mlx5e_res.pdn);
	MLX5_SET64(mkc, mkc, len, npages << page_shift);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 MLX5_MTT_OCTW(npages));
	MLX5_SET(mkc, mkc, log_page_size, page_shift);
	MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
		 MLX5_MTT_OCTW(npages));

	/* Initialize the mkey with all MTTs pointing to a default
	 * page (filler_addr). When the channels are activated, UMR
	 * WQEs will redirect the RX WQEs to the actual memory from
	 * the RQ's pool, while the gaps (wqe_overflow) remain mapped
	 * to the default page.
	 */
	mtt = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
	for (i = 0 ; i < npages ; i++)
		mtt[i].ptag = cpu_to_be64(filler_addr);

	err = mlx5_core_create_mkey(mdev, umr_mkey, in, inlen);

	kvfree(in);
	return err;
}

static int mlx5e_create_rq_umr_mkey(struct mlx5_core_dev *mdev, struct mlx5e_rq *rq)
{
	u64 num_mtts = MLX5E_REQUIRED_MTTS(mlx5_wq_ll_get_size(&rq->mpwqe.wq));

	return mlx5e_create_umr_mkey(mdev, num_mtts, PAGE_SHIFT, &rq->umr_mkey,
				     rq->wqe_overflow.addr);
}

static u64 mlx5e_get_mpwqe_offset(u16 wqe_ix)
{
	return MLX5E_REQUIRED_MTTS(wqe_ix) << PAGE_SHIFT;
}

static void mlx5e_init_frags_partition(struct mlx5e_rq *rq)
{
	struct mlx5e_wqe_frag_info next_frag = {};
	struct mlx5e_wqe_frag_info *prev = NULL;
	int i;

	next_frag.di = &rq->wqe.di[0];

	for (i = 0; i < mlx5_wq_cyc_get_size(&rq->wqe.wq); i++) {
		struct mlx5e_rq_frag_info *frag_info = &rq->wqe.info.arr[0];
		struct mlx5e_wqe_frag_info *frag =
			&rq->wqe.frags[i << rq->wqe.info.log_num_frags];
		int f;

		for (f = 0; f < rq->wqe.info.num_frags; f++, frag++) {
			if (next_frag.offset + frag_info[f].frag_stride > PAGE_SIZE) {
				next_frag.di++;
				next_frag.offset = 0;
				if (prev)
					prev->last_in_page = true;
			}
			*frag = next_frag;

			/* prepare next */
			next_frag.offset += frag_info[f].frag_stride;
			prev = frag;
		}
	}

	if (prev)
		prev->last_in_page = true;
}

int mlx5e_init_di_list(struct mlx5e_rq *rq, int wq_sz, int node)
{
	int len = wq_sz << rq->wqe.info.log_num_frags;

	rq->wqe.di = kvzalloc_node(array_size(len, sizeof(*rq->wqe.di)), GFP_KERNEL, node);
	if (!rq->wqe.di)
		return -ENOMEM;

	mlx5e_init_frags_partition(rq);

	return 0;
}

void mlx5e_free_di_list(struct mlx5e_rq *rq)
{
	kvfree(rq->wqe.di);
}

static void mlx5e_rq_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_rq *rq = container_of(recover_work, struct mlx5e_rq, recover_work);

	mlx5e_reporter_rq_cqe_err(rq);
}

static int mlx5e_alloc_mpwqe_rq_drop_page(struct mlx5e_rq *rq)
{
	rq->wqe_overflow.page = alloc_page(GFP_KERNEL);
	if (!rq->wqe_overflow.page)
		return -ENOMEM;

	rq->wqe_overflow.addr = dma_map_page(rq->pdev, rq->wqe_overflow.page, 0,
					     PAGE_SIZE, rq->buff.map_dir);
	if (dma_mapping_error(rq->pdev, rq->wqe_overflow.addr)) {
		__free_page(rq->wqe_overflow.page);
		return -ENOMEM;
	}
	return 0;
}

static void mlx5e_free_mpwqe_rq_drop_page(struct mlx5e_rq *rq)
{
	 dma_unmap_page(rq->pdev, rq->wqe_overflow.addr, PAGE_SIZE,
			rq->buff.map_dir);
	 __free_page(rq->wqe_overflow.page);
}

static int mlx5e_alloc_rq(struct mlx5e_channel *c,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk,
			  struct xsk_buff_pool *xsk_pool,
			  struct mlx5e_rq_param *rqp,
			  struct mlx5e_rq *rq)
{
	struct page_pool_params pp_params = { 0 };
	struct mlx5_core_dev *mdev = c->mdev;
	void *rqc = rqp->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	u32 rq_xdp_ix;
	u32 pool_size;
	int wq_sz;
	int err;
	int i;

	rqp->wq.db_numa_node = cpu_to_node(c->cpu);

	rq->wq_type = params->rq_wq_type;
	rq->pdev    = c->pdev;
	rq->netdev  = c->netdev;
	rq->priv    = c->priv;
	rq->tstamp  = c->tstamp;
	rq->clock   = &mdev->clock;
	rq->icosq   = &c->icosq;
	rq->ix      = c->ix;
	rq->mdev    = mdev;
	rq->hw_mtu  = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	rq->xdpsq   = &c->rq_xdpsq;
	rq->xsk_pool = xsk_pool;
	rq->ptp_cyc2time = mlx5_is_real_time_rq(mdev) ?
			   mlx5_real_time_cyc2time :
			   mlx5_timecounter_cyc2time;

	if (rq->xsk_pool)
		rq->stats = &c->priv->channel_stats[c->ix].xskrq;
	else
		rq->stats = &c->priv->channel_stats[c->ix].rq;
	INIT_WORK(&rq->recover_work, mlx5e_rq_err_cqe_work);

	if (params->xdp_prog)
		bpf_prog_inc(params->xdp_prog);
	RCU_INIT_POINTER(rq->xdp_prog, params->xdp_prog);

	rq_xdp_ix = rq->ix;
	if (xsk)
		rq_xdp_ix += params->num_channels * MLX5E_RQ_GROUP_XSK;
	err = xdp_rxq_info_reg(&rq->xdp_rxq, rq->netdev, rq_xdp_ix, 0);
	if (err < 0)
		goto err_rq_xdp_prog;

	rq->buff.map_dir = params->xdp_prog ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;
	rq->buff.headroom = mlx5e_get_rq_headroom(mdev, params, xsk);
	pool_size = 1 << params->log_rq_mtu_frames;

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		err = mlx5_wq_ll_create(mdev, &rqp->wq, rqc_wq, &rq->mpwqe.wq,
					&rq->wq_ctrl);
		if (err)
			goto err_rq_xdp;

		err = mlx5e_alloc_mpwqe_rq_drop_page(rq);
		if (err)
			goto err_rq_wq_destroy;

		rq->mpwqe.wq.db = &rq->mpwqe.wq.db[MLX5_RCV_DBR];

		wq_sz = mlx5_wq_ll_get_size(&rq->mpwqe.wq);

		pool_size = MLX5_MPWRQ_PAGES_PER_WQE <<
			mlx5e_mpwqe_get_log_rq_size(params, xsk);

		rq->mpwqe.log_stride_sz = mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
		rq->mpwqe.num_strides =
			BIT(mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk));

		rq->buff.frame0_sz = (1 << rq->mpwqe.log_stride_sz);

		err = mlx5e_create_rq_umr_mkey(mdev, rq);
		if (err)
			goto err_rq_drop_page;
		rq->mkey_be = cpu_to_be32(rq->umr_mkey.key);

		err = mlx5e_rq_alloc_mpwqe_info(rq, c);
		if (err)
			goto err_rq_mkey;
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		err = mlx5_wq_cyc_create(mdev, &rqp->wq, rqc_wq, &rq->wqe.wq,
					 &rq->wq_ctrl);
		if (err)
			goto err_rq_xdp;

		rq->wqe.wq.db = &rq->wqe.wq.db[MLX5_RCV_DBR];

		wq_sz = mlx5_wq_cyc_get_size(&rq->wqe.wq);

		rq->wqe.info = rqp->frags_info;
		rq->buff.frame0_sz = rq->wqe.info.arr[0].frag_stride;

		rq->wqe.frags =
			kvzalloc_node(array_size(sizeof(*rq->wqe.frags),
					(wq_sz << rq->wqe.info.log_num_frags)),
				      GFP_KERNEL, cpu_to_node(c->cpu));
		if (!rq->wqe.frags) {
			err = -ENOMEM;
			goto err_rq_wq_destroy;
		}

		err = mlx5e_init_di_list(rq, wq_sz, cpu_to_node(c->cpu));
		if (err)
			goto err_rq_frags;

		rq->mkey_be = c->mkey_be;
	}

	err = mlx5e_rq_set_handlers(rq, params, xsk);
	if (err)
		goto err_free_by_rq_type;

	if (xsk) {
		err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
						 MEM_TYPE_XSK_BUFF_POOL, NULL);
		xsk_pool_set_rxq_info(rq->xsk_pool, &rq->xdp_rxq);
	} else {
		/* Create a page_pool and register it with rxq */
		pp_params.order     = 0;
		pp_params.flags     = 0; /* No-internal DMA mapping in page_pool */
		pp_params.pool_size = pool_size;
		pp_params.nid       = cpu_to_node(c->cpu);
		pp_params.dev       = c->pdev;
		pp_params.dma_dir   = rq->buff.map_dir;

		/* page_pool can be used even when there is no rq->xdp_prog,
		 * given page_pool does not handle DMA mapping there is no
		 * required state to clear. And page_pool gracefully handle
		 * elevated refcnt.
		 */
		rq->page_pool = page_pool_create(&pp_params);
		if (IS_ERR(rq->page_pool)) {
			err = PTR_ERR(rq->page_pool);
			rq->page_pool = NULL;
			goto err_free_by_rq_type;
		}
		err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
						 MEM_TYPE_PAGE_POOL, rq->page_pool);
	}
	if (err)
		goto err_free_by_rq_type;

	for (i = 0; i < wq_sz; i++) {
		if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
			struct mlx5e_rx_wqe_ll *wqe =
				mlx5_wq_ll_get_wqe(&rq->mpwqe.wq, i);
			u32 byte_count =
				rq->mpwqe.num_strides << rq->mpwqe.log_stride_sz;
			u64 dma_offset = mlx5e_get_mpwqe_offset(i);

			wqe->data[0].addr = cpu_to_be64(dma_offset + rq->buff.headroom);
			wqe->data[0].byte_count = cpu_to_be32(byte_count);
			wqe->data[0].lkey = rq->mkey_be;
		} else {
			struct mlx5e_rx_wqe_cyc *wqe =
				mlx5_wq_cyc_get_wqe(&rq->wqe.wq, i);
			int f;

			for (f = 0; f < rq->wqe.info.num_frags; f++) {
				u32 frag_size = rq->wqe.info.arr[f].frag_size |
					MLX5_HW_START_PADDING;

				wqe->data[f].byte_count = cpu_to_be32(frag_size);
				wqe->data[f].lkey = rq->mkey_be;
			}
			/* check if num_frags is not a pow of two */
			if (rq->wqe.info.num_frags < (1 << rq->wqe.info.log_num_frags)) {
				wqe->data[f].byte_count = 0;
				wqe->data[f].lkey = cpu_to_be32(MLX5_INVALID_LKEY);
				wqe->data[f].addr = 0;
			}
		}
	}

	INIT_WORK(&rq->dim.work, mlx5e_rx_dim_work);

	switch (params->rx_cq_moderation.cq_period_mode) {
	case MLX5_CQ_PERIOD_MODE_START_FROM_CQE:
		rq->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_CQE;
		break;
	case MLX5_CQ_PERIOD_MODE_START_FROM_EQE:
	default:
		rq->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	}

	rq->page_cache.head = 0;
	rq->page_cache.tail = 0;

	return 0;

err_free_by_rq_type:
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		kvfree(rq->mpwqe.info);
err_rq_mkey:
		mlx5_core_destroy_mkey(mdev, &rq->umr_mkey);
err_rq_drop_page:
		mlx5e_free_mpwqe_rq_drop_page(rq);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		mlx5e_free_di_list(rq);
err_rq_frags:
		kvfree(rq->wqe.frags);
	}
err_rq_wq_destroy:
	mlx5_wq_destroy(&rq->wq_ctrl);
err_rq_xdp:
	xdp_rxq_info_unreg(&rq->xdp_rxq);
err_rq_xdp_prog:
	if (params->xdp_prog)
		bpf_prog_put(params->xdp_prog);

	return err;
}

static void mlx5e_free_rq(struct mlx5e_rq *rq)
{
	struct bpf_prog *old_prog;
	int i;

	old_prog = rcu_dereference_protected(rq->xdp_prog,
					     lockdep_is_held(&rq->priv->state_lock));
	if (old_prog)
		bpf_prog_put(old_prog);

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		kvfree(rq->mpwqe.info);
		mlx5_core_destroy_mkey(rq->mdev, &rq->umr_mkey);
		mlx5e_free_mpwqe_rq_drop_page(rq);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		kvfree(rq->wqe.frags);
		mlx5e_free_di_list(rq);
	}

	for (i = rq->page_cache.head; i != rq->page_cache.tail;
	     i = (i + 1) & (MLX5E_CACHE_SIZE - 1)) {
		struct mlx5e_dma_info *dma_info = &rq->page_cache.page_cache[i];

		/* With AF_XDP, page_cache is not used, so this loop is not
		 * entered, and it's safe to call mlx5e_page_release_dynamic
		 * directly.
		 */
		mlx5e_page_release_dynamic(rq, dma_info, false);
	}

	xdp_rxq_info_unreg(&rq->xdp_rxq);
	page_pool_destroy(rq->page_pool);
	mlx5_wq_destroy(&rq->wq_ctrl);
}

int mlx5e_create_rq(struct mlx5e_rq *rq, struct mlx5e_rq_param *param)
{
	struct mlx5_core_dev *mdev = rq->mdev;
	u8 ts_format;
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

	ts_format = mlx5_is_real_time_rq(mdev) ?
		    MLX5_RQC_TIMESTAMP_FORMAT_REAL_TIME :
		    MLX5_RQC_TIMESTAMP_FORMAT_FREE_RUNNING;
	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	wq  = MLX5_ADDR_OF(rqc, rqc, wq);

	memcpy(rqc, param->rqc, sizeof(param->rqc));

	MLX5_SET(rqc,  rqc, cqn,		rq->cq.mcq.cqn);
	MLX5_SET(rqc,  rqc, state,		MLX5_RQC_STATE_RST);
	MLX5_SET(rqc,  rqc, ts_format,		ts_format);
	MLX5_SET(wq,   wq,  log_wq_pg_sz,	rq->wq_ctrl.buf.page_shift -
						MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq,  dbr_addr,		rq->wq_ctrl.db.dma);

	mlx5_fill_page_frag_array(&rq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_rq(mdev, in, inlen, &rq->rqn);

	kvfree(in);

	return err;
}

int mlx5e_modify_rq_state(struct mlx5e_rq *rq, int curr_state, int next_state)
{
	struct mlx5_core_dev *mdev = rq->mdev;

	void *in;
	void *rqc;
	int inlen;
	int err;

	inlen = MLX5_ST_SZ_BYTES(modify_rq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	if (curr_state == MLX5_RQC_STATE_RST && next_state == MLX5_RQC_STATE_RDY)
		mlx5e_rqwq_reset(rq);

	rqc = MLX5_ADDR_OF(modify_rq_in, in, ctx);

	MLX5_SET(modify_rq_in, in, rq_state, curr_state);
	MLX5_SET(rqc, rqc, state, next_state);

	err = mlx5_core_modify_rq(mdev, rq->rqn, in);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_scatter_fcs(struct mlx5e_rq *rq, bool enable)
{
	struct mlx5_core_dev *mdev = rq->mdev;

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

	err = mlx5_core_modify_rq(mdev, rq->rqn, in);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_vsd(struct mlx5e_rq *rq, bool vsd)
{
	struct mlx5_core_dev *mdev = rq->mdev;
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

	err = mlx5_core_modify_rq(mdev, rq->rqn, in);

	kvfree(in);

	return err;
}

void mlx5e_destroy_rq(struct mlx5e_rq *rq)
{
	mlx5_core_destroy_rq(rq->mdev, rq->rqn);
}

int mlx5e_wait_for_min_rx_wqes(struct mlx5e_rq *rq, int wait_time)
{
	unsigned long exp_time = jiffies + msecs_to_jiffies(wait_time);

	u16 min_wqes = mlx5_min_rx_wqes(rq->wq_type, mlx5e_rqwq_get_size(rq));

	do {
		if (mlx5e_rqwq_get_cur_sz(rq) >= min_wqes)
			return 0;

		msleep(20);
	} while (time_before(jiffies, exp_time));

	netdev_warn(rq->netdev, "Failed to get min RX wqes on Channel[%d] RQN[0x%x] wq cur_sz(%d) min_rx_wqes(%d)\n",
		    rq->ix, rq->rqn, mlx5e_rqwq_get_cur_sz(rq), min_wqes);

	mlx5e_reporter_rx_timeout(rq);
	return -ETIMEDOUT;
}

void mlx5e_free_rx_in_progress_descs(struct mlx5e_rq *rq)
{
	struct mlx5_wq_ll *wq;
	u16 head;
	int i;

	if (rq->wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return;

	wq = &rq->mpwqe.wq;
	head = wq->head;

	/* Outstanding UMR WQEs (in progress) start at wq->head */
	for (i = 0; i < rq->mpwqe.umr_in_progress; i++) {
		rq->dealloc_wqe(rq, head);
		head = mlx5_wq_ll_get_wqe_next_ix(wq, head);
	}

	rq->mpwqe.actual_wq_head = wq->head;
	rq->mpwqe.umr_in_progress = 0;
	rq->mpwqe.umr_completed = 0;
}

void mlx5e_free_rx_descs(struct mlx5e_rq *rq)
{
	__be16 wqe_ix_be;
	u16 wqe_ix;

	if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
		struct mlx5_wq_ll *wq = &rq->mpwqe.wq;

		mlx5e_free_rx_in_progress_descs(rq);

		while (!mlx5_wq_ll_is_empty(wq)) {
			struct mlx5e_rx_wqe_ll *wqe;

			wqe_ix_be = *wq->tail_next;
			wqe_ix    = be16_to_cpu(wqe_ix_be);
			wqe       = mlx5_wq_ll_get_wqe(wq, wqe_ix);
			rq->dealloc_wqe(rq, wqe_ix);
			mlx5_wq_ll_pop(wq, wqe_ix_be,
				       &wqe->next.next_wqe_index);
		}
	} else {
		struct mlx5_wq_cyc *wq = &rq->wqe.wq;

		while (!mlx5_wq_cyc_is_empty(wq)) {
			wqe_ix = mlx5_wq_cyc_get_tail(wq);
			rq->dealloc_wqe(rq, wqe_ix);
			mlx5_wq_cyc_pop(wq);
		}
	}

}

int mlx5e_open_rq(struct mlx5e_channel *c, struct mlx5e_params *params,
		  struct mlx5e_rq_param *param, struct mlx5e_xsk_param *xsk,
		  struct xsk_buff_pool *xsk_pool, struct mlx5e_rq *rq)
{
	int err;

	err = mlx5e_alloc_rq(c, params, xsk, xsk_pool, param, rq);
	if (err)
		return err;

	err = mlx5e_create_rq(rq, param);
	if (err)
		goto err_free_rq;

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_destroy_rq;

	if (mlx5e_is_tls_on(c->priv) && !mlx5_accel_is_ktls_device(c->mdev))
		__set_bit(MLX5E_RQ_STATE_FPGA_TLS, &c->rq.state); /* must be FPGA */

	if (MLX5_CAP_ETH(c->mdev, cqe_checksum_full))
		__set_bit(MLX5E_RQ_STATE_CSUM_FULL, &c->rq.state);

	if (params->rx_dim_enabled)
		__set_bit(MLX5E_RQ_STATE_AM, &c->rq.state);

	/* We disable csum_complete when XDP is enabled since
	 * XDP programs might manipulate packets which will render
	 * skb->checksum incorrect.
	 */
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE) || c->xdp)
		__set_bit(MLX5E_RQ_STATE_NO_CSUM_COMPLETE, &c->rq.state);

	/* For CQE compression on striding RQ, use stride index provided by
	 * HW if capability is supported.
	 */
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ) &&
	    MLX5_CAP_GEN(c->mdev, mini_cqe_resp_stride_index))
		__set_bit(MLX5E_RQ_STATE_MINI_CQE_HW_STRIDX, &c->rq.state);

	return 0;

err_destroy_rq:
	mlx5e_destroy_rq(rq);
err_free_rq:
	mlx5e_free_rq(rq);

	return err;
}

void mlx5e_activate_rq(struct mlx5e_rq *rq)
{
	set_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
	mlx5e_trigger_irq(rq->icosq);
}

void mlx5e_deactivate_rq(struct mlx5e_rq *rq)
{
	clear_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
	synchronize_net(); /* Sync with NAPI to prevent mlx5e_post_rx_wqes. */
}

void mlx5e_close_rq(struct mlx5e_rq *rq)
{
	cancel_work_sync(&rq->dim.work);
	cancel_work_sync(&rq->icosq->recover_work);
	cancel_work_sync(&rq->recover_work);
	mlx5e_destroy_rq(rq);
	mlx5e_free_rx_descs(rq);
	mlx5e_free_rq(rq);
}

static void mlx5e_free_xdpsq_db(struct mlx5e_xdpsq *sq)
{
	kvfree(sq->db.xdpi_fifo.xi);
	kvfree(sq->db.wqe_info);
}

static int mlx5e_alloc_xdpsq_fifo(struct mlx5e_xdpsq *sq, int numa)
{
	struct mlx5e_xdp_info_fifo *xdpi_fifo = &sq->db.xdpi_fifo;
	int wq_sz        = mlx5_wq_cyc_get_size(&sq->wq);
	int dsegs_per_wq = wq_sz * MLX5_SEND_WQEBB_NUM_DS;

	xdpi_fifo->xi = kvzalloc_node(sizeof(*xdpi_fifo->xi) * dsegs_per_wq,
				      GFP_KERNEL, numa);
	if (!xdpi_fifo->xi)
		return -ENOMEM;

	xdpi_fifo->pc   = &sq->xdpi_fifo_pc;
	xdpi_fifo->cc   = &sq->xdpi_fifo_cc;
	xdpi_fifo->mask = dsegs_per_wq - 1;

	return 0;
}

static int mlx5e_alloc_xdpsq_db(struct mlx5e_xdpsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	int err;

	sq->db.wqe_info = kvzalloc_node(sizeof(*sq->db.wqe_info) * wq_sz,
					GFP_KERNEL, numa);
	if (!sq->db.wqe_info)
		return -ENOMEM;

	err = mlx5e_alloc_xdpsq_fifo(sq, numa);
	if (err) {
		mlx5e_free_xdpsq_db(sq);
		return err;
	}

	return 0;
}

static int mlx5e_alloc_xdpsq(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct xsk_buff_pool *xsk_pool,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_xdpsq *sq,
			     bool is_redirect)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->pdev      = c->pdev;
	sq->mkey_be   = c->mkey_be;
	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	sq->xsk_pool  = xsk_pool;

	sq->stats = sq->xsk_pool ?
		&c->priv->channel_stats[c->ix].xsksq :
		is_redirect ?
			&c->priv->channel_stats[c->ix].xdpsq :
			&c->priv->channel_stats[c->ix].rq_xdpsq;

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_xdpsq_db(sq, cpu_to_node(c->cpu));
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
	kvfree(sq->db.wqe_info);
}

static int mlx5e_alloc_icosq_db(struct mlx5e_icosq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	size_t size;

	size = array_size(wq_sz, sizeof(*sq->db.wqe_info));
	sq->db.wqe_info = kvzalloc_node(size, GFP_KERNEL, numa);
	if (!sq->db.wqe_info)
		return -ENOMEM;

	return 0;
}

static void mlx5e_icosq_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_icosq *sq = container_of(recover_work, struct mlx5e_icosq,
					      recover_work);

	mlx5e_reporter_icosq_cqe_err(sq);
}

static int mlx5e_alloc_icosq(struct mlx5e_channel *c,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_icosq *sq)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;
	sq->reserved_room = param->stop_room;

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_icosq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	INIT_WORK(&sq->recover_work, mlx5e_icosq_err_cqe_work);

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

void mlx5e_free_txqsq_db(struct mlx5e_txqsq *sq)
{
	kvfree(sq->db.wqe_info);
	kvfree(sq->db.skb_fifo.fifo);
	kvfree(sq->db.dma_fifo);
}

int mlx5e_alloc_txqsq_db(struct mlx5e_txqsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	int df_sz = wq_sz * MLX5_SEND_WQEBB_NUM_DS;

	sq->db.dma_fifo = kvzalloc_node(array_size(df_sz,
						   sizeof(*sq->db.dma_fifo)),
					GFP_KERNEL, numa);
	sq->db.skb_fifo.fifo = kvzalloc_node(array_size(df_sz,
							sizeof(*sq->db.skb_fifo.fifo)),
					GFP_KERNEL, numa);
	sq->db.wqe_info = kvzalloc_node(array_size(wq_sz,
						   sizeof(*sq->db.wqe_info)),
					GFP_KERNEL, numa);
	if (!sq->db.dma_fifo || !sq->db.skb_fifo.fifo || !sq->db.wqe_info) {
		mlx5e_free_txqsq_db(sq);
		return -ENOMEM;
	}

	sq->dma_fifo_mask = df_sz - 1;

	sq->db.skb_fifo.pc   = &sq->skb_fifo_pc;
	sq->db.skb_fifo.cc   = &sq->skb_fifo_cc;
	sq->db.skb_fifo.mask = df_sz - 1;

	return 0;
}

static int mlx5e_alloc_txqsq(struct mlx5e_channel *c,
			     int txq_ix,
			     struct mlx5e_params *params,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_txqsq *sq,
			     int tc)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->pdev      = c->pdev;
	sq->tstamp    = c->tstamp;
	sq->clock     = &mdev->clock;
	sq->mkey_be   = c->mkey_be;
	sq->netdev    = c->netdev;
	sq->mdev      = c->mdev;
	sq->priv      = c->priv;
	sq->ch_ix     = c->ix;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	INIT_WORK(&sq->recover_work, mlx5e_tx_err_cqe_work);
	if (!MLX5_CAP_ETH(mdev, wqe_vlan_insert))
		set_bit(MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE, &sq->state);
	if (MLX5_IPSEC_DEV(c->priv->mdev))
		set_bit(MLX5E_SQ_STATE_IPSEC, &sq->state);
	if (mlx5_accel_is_tls_device(c->priv->mdev))
		set_bit(MLX5E_SQ_STATE_TLS, &sq->state);
	if (param->is_mpw)
		set_bit(MLX5E_SQ_STATE_MPWQE, &sq->state);
	sq->stop_room = param->stop_room;
	sq->ptp_cyc2time = mlx5_is_real_time_sq(mdev) ?
			   mlx5_real_time_cyc2time :
			   mlx5_timecounter_cyc2time;

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db    = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_txqsq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	INIT_WORK(&sq->dim.work, mlx5e_tx_dim_work);
	sq->dim.mode = params->tx_cq_moderation.cq_period_mode;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

void mlx5e_free_txqsq(struct mlx5e_txqsq *sq)
{
	mlx5e_free_txqsq_db(sq);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static int mlx5e_create_sq(struct mlx5_core_dev *mdev,
			   struct mlx5e_sq_param *param,
			   struct mlx5e_create_sq_param *csp,
			   u32 *sqn)
{
	u8 ts_format;
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

	ts_format = mlx5_is_real_time_sq(mdev) ?
		    MLX5_SQC_TIMESTAMP_FORMAT_REAL_TIME :
		    MLX5_SQC_TIMESTAMP_FORMAT_FREE_RUNNING;
	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq = MLX5_ADDR_OF(sqc, sqc, wq);

	memcpy(sqc, param->sqc, sizeof(param->sqc));
	MLX5_SET(sqc,  sqc, tis_lst_sz, csp->tis_lst_sz);
	MLX5_SET(sqc,  sqc, tis_num_0, csp->tisn);
	MLX5_SET(sqc,  sqc, cqn, csp->cqn);
	MLX5_SET(sqc,  sqc, ts_cqe_to_dest_cqn, csp->ts_cqe_to_dest_cqn);
	MLX5_SET(sqc,  sqc, ts_format, ts_format);


	if (MLX5_CAP_ETH(mdev, wqe_inline_mode) == MLX5_CAP_INLINE_MODE_VPORT_CONTEXT)
		MLX5_SET(sqc,  sqc, min_wqe_inline_mode, csp->min_inline_mode);

	MLX5_SET(sqc,  sqc, state, MLX5_SQC_STATE_RST);
	MLX5_SET(sqc,  sqc, flush_in_error_en, 1);

	MLX5_SET(wq,   wq, wq_type,       MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq,   wq, uar_page,      mdev->mlx5e_res.bfreg.index);
	MLX5_SET(wq,   wq, log_wq_pg_sz,  csp->wq_ctrl->buf.page_shift -
					  MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr,      csp->wq_ctrl->db.dma);

	mlx5_fill_page_frag_array(&csp->wq_ctrl->buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, sqn);

	kvfree(in);

	return err;
}

int mlx5e_modify_sq(struct mlx5_core_dev *mdev, u32 sqn,
		    struct mlx5e_modify_sq_param *p)
{
	u64 bitmask = 0;
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
		bitmask |= 1;
		MLX5_SET(sqc, sqc, packet_pacing_rate_limit_index, p->rl_index);
	}
	if (p->qos_update && p->next_state == MLX5_SQC_STATE_RDY) {
		bitmask |= 1 << 2;
		MLX5_SET(sqc, sqc, qos_queue_group_id, p->qos_queue_group_id);
	}
	MLX5_SET64(modify_sq_in, in, modify_bitmask, bitmask);

	err = mlx5_core_modify_sq(mdev, sqn, in);

	kvfree(in);

	return err;
}

static void mlx5e_destroy_sq(struct mlx5_core_dev *mdev, u32 sqn)
{
	mlx5_core_destroy_sq(mdev, sqn);
}

int mlx5e_create_sq_rdy(struct mlx5_core_dev *mdev,
			struct mlx5e_sq_param *param,
			struct mlx5e_create_sq_param *csp,
			u16 qos_queue_group_id,
			u32 *sqn)
{
	struct mlx5e_modify_sq_param msp = {0};
	int err;

	err = mlx5e_create_sq(mdev, param, csp, sqn);
	if (err)
		return err;

	msp.curr_state = MLX5_SQC_STATE_RST;
	msp.next_state = MLX5_SQC_STATE_RDY;
	if (qos_queue_group_id) {
		msp.qos_update = true;
		msp.qos_queue_group_id = qos_queue_group_id;
	}
	err = mlx5e_modify_sq(mdev, *sqn, &msp);
	if (err)
		mlx5e_destroy_sq(mdev, *sqn);

	return err;
}

static int mlx5e_set_sq_maxrate(struct net_device *dev,
				struct mlx5e_txqsq *sq, u32 rate);

int mlx5e_open_txqsq(struct mlx5e_channel *c, u32 tisn, int txq_ix,
		     struct mlx5e_params *params, struct mlx5e_sq_param *param,
		     struct mlx5e_txqsq *sq, int tc, u16 qos_queue_group_id, u16 qos_qid)
{
	struct mlx5e_create_sq_param csp = {};
	u32 tx_rate;
	int err;

	err = mlx5e_alloc_txqsq(c, txq_ix, params, param, sq, tc);
	if (err)
		return err;

	if (qos_queue_group_id)
		sq->stats = c->priv->htb.qos_sq_stats[qos_qid];
	else
		sq->stats = &c->priv->channel_stats[c->ix].sq[tc];

	csp.tisn            = tisn;
	csp.tis_lst_sz      = 1;
	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = sq->min_inline_mode;
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, qos_queue_group_id, &sq->sqn);
	if (err)
		goto err_free_txqsq;

	tx_rate = c->priv->tx_rates[sq->txq_ix];
	if (tx_rate)
		mlx5e_set_sq_maxrate(c->netdev, sq, tx_rate);

	if (params->tx_dim_enabled)
		sq->state |= BIT(MLX5E_SQ_STATE_AM);

	return 0;

err_free_txqsq:
	mlx5e_free_txqsq(sq);

	return err;
}

void mlx5e_activate_txqsq(struct mlx5e_txqsq *sq)
{
	sq->txq = netdev_get_tx_queue(sq->netdev, sq->txq_ix);
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	netdev_tx_reset_queue(sq->txq);
	netif_tx_start_queue(sq->txq);
}

void mlx5e_tx_disable_queue(struct netdev_queue *txq)
{
	__netif_tx_lock_bh(txq);
	netif_tx_stop_queue(txq);
	__netif_tx_unlock_bh(txq);
}

void mlx5e_deactivate_txqsq(struct mlx5e_txqsq *sq)
{
	struct mlx5_wq_cyc *wq = &sq->wq;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	synchronize_net(); /* Sync with NAPI to prevent netif_tx_wake_queue. */

	mlx5e_tx_disable_queue(sq->txq);

	/* last doorbell out, godspeed .. */
	if (mlx5e_wqc_has_room_for(wq, sq->cc, sq->pc, 1)) {
		u16 pi = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
		struct mlx5e_tx_wqe *nop;

		sq->db.wqe_info[pi] = (struct mlx5e_tx_wqe_info) {
			.num_wqebbs = 1,
		};

		nop = mlx5e_post_nop(wq, sq->sqn, &sq->pc);
		mlx5e_notify_hw(wq, sq->pc, sq->uar_map, &nop->ctrl);
	}
}

void mlx5e_close_txqsq(struct mlx5e_txqsq *sq)
{
	struct mlx5_core_dev *mdev = sq->mdev;
	struct mlx5_rate_limit rl = {0};

	cancel_work_sync(&sq->dim.work);
	cancel_work_sync(&sq->recover_work);
	mlx5e_destroy_sq(mdev, sq->sqn);
	if (sq->rate_limit) {
		rl.rate = sq->rate_limit;
		mlx5_rl_remove_rate(mdev, &rl);
	}
	mlx5e_free_txqsq_descs(sq);
	mlx5e_free_txqsq(sq);
}

void mlx5e_tx_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_txqsq *sq = container_of(recover_work, struct mlx5e_txqsq,
					      recover_work);

	mlx5e_reporter_tx_err_cqe(sq);
}

int mlx5e_open_icosq(struct mlx5e_channel *c, struct mlx5e_params *params,
		     struct mlx5e_sq_param *param, struct mlx5e_icosq *sq)
{
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_alloc_icosq(c, param, sq);
	if (err)
		return err;

	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = params->tx_min_inline_mode;
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, 0, &sq->sqn);
	if (err)
		goto err_free_icosq;

	return 0;

err_free_icosq:
	mlx5e_free_icosq(sq);

	return err;
}

void mlx5e_activate_icosq(struct mlx5e_icosq *icosq)
{
	set_bit(MLX5E_SQ_STATE_ENABLED, &icosq->state);
}

void mlx5e_deactivate_icosq(struct mlx5e_icosq *icosq)
{
	clear_bit(MLX5E_SQ_STATE_ENABLED, &icosq->state);
	synchronize_net(); /* Sync with NAPI. */
}

void mlx5e_close_icosq(struct mlx5e_icosq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	mlx5e_destroy_sq(c->mdev, sq->sqn);
	mlx5e_free_icosq_descs(sq);
	mlx5e_free_icosq(sq);
}

int mlx5e_open_xdpsq(struct mlx5e_channel *c, struct mlx5e_params *params,
		     struct mlx5e_sq_param *param, struct xsk_buff_pool *xsk_pool,
		     struct mlx5e_xdpsq *sq, bool is_redirect)
{
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_alloc_xdpsq(c, params, xsk_pool, param, sq, is_redirect);
	if (err)
		return err;

	csp.tis_lst_sz      = 1;
	csp.tisn            = c->priv->tisn[c->lag_port][0]; /* tc = 0 */
	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = sq->min_inline_mode;
	set_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, 0, &sq->sqn);
	if (err)
		goto err_free_xdpsq;

	mlx5e_set_xmit_fp(sq, param->is_mpw);

	if (!param->is_mpw) {
		unsigned int ds_cnt = MLX5E_XDP_TX_DS_COUNT;
		unsigned int inline_hdr_sz = 0;
		int i;

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

			sq->db.wqe_info[i] = (struct mlx5e_xdp_wqe_info) {
				.num_wqebbs = 1,
				.num_pkts   = 1,
			};

			cseg->qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
			eseg->inline_hdr.sz = cpu_to_be16(inline_hdr_sz);

			dseg = (struct mlx5_wqe_data_seg *)cseg + (ds_cnt - 1);
			dseg->lkey = sq->mkey_be;
		}
	}

	return 0;

err_free_xdpsq:
	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	mlx5e_free_xdpsq(sq);

	return err;
}

void mlx5e_close_xdpsq(struct mlx5e_xdpsq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	synchronize_net(); /* Sync with NAPI. */

	mlx5e_destroy_sq(c->mdev, sq->sqn);
	mlx5e_free_xdpsq_descs(sq);
	mlx5e_free_xdpsq(sq);
}

static int mlx5e_alloc_cq_common(struct mlx5e_priv *priv,
				 struct mlx5e_cq_param *param,
				 struct mlx5e_cq *cq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;
	int eqn_not_used;
	unsigned int irqn;
	int err;
	u32 i;

	err = mlx5_vector2eqn(mdev, param->eq_ix, &eqn_not_used, &irqn);
	if (err)
		return err;

	err = mlx5_cqwq_create(mdev, &param->wq, param->cqc, &cq->wq,
			       &cq->wq_ctrl);
	if (err)
		return err;

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
	cq->netdev = priv->netdev;
	cq->priv = priv;

	return 0;
}

static int mlx5e_alloc_cq(struct mlx5e_priv *priv,
			  struct mlx5e_cq_param *param,
			  struct mlx5e_create_cq_param *ccp,
			  struct mlx5e_cq *cq)
{
	int err;

	param->wq.buf_numa_node = ccp->node;
	param->wq.db_numa_node  = ccp->node;
	param->eq_ix            = ccp->ix;

	err = mlx5e_alloc_cq_common(priv, param, cq);

	cq->napi     = ccp->napi;
	cq->ch_stats = ccp->ch_stats;

	return err;
}

static void mlx5e_free_cq(struct mlx5e_cq *cq)
{
	mlx5_wq_destroy(&cq->wq_ctrl);
}

static int mlx5e_create_cq(struct mlx5e_cq *cq, struct mlx5e_cq_param *param)
{
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_core_dev *mdev = cq->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;

	void *in;
	void *cqc;
	int inlen;
	unsigned int irqn_not_used;
	int eqn;
	int err;

	err = mlx5_vector2eqn(mdev, param->eq_ix, &eqn, &irqn_not_used);
	if (err)
		return err;

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);

	memcpy(cqc, param->cqc, sizeof(param->cqc));

	mlx5_fill_page_frag_array(&cq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas));

	MLX5_SET(cqc,   cqc, cq_period_mode, param->cq_period_mode);
	MLX5_SET(cqc,   cqc, c_eqn,         eqn);
	MLX5_SET(cqc,   cqc, uar_page,      mdev->priv.uar->index);
	MLX5_SET(cqc,   cqc, log_page_size, cq->wq_ctrl.buf.page_shift -
					    MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr,      cq->wq_ctrl.db.dma);

	err = mlx5_core_create_cq(mdev, mcq, in, inlen, out, sizeof(out));

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

int mlx5e_open_cq(struct mlx5e_priv *priv, struct dim_cq_moder moder,
		  struct mlx5e_cq_param *param, struct mlx5e_create_cq_param *ccp,
		  struct mlx5e_cq *cq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5e_alloc_cq(priv, param, ccp, cq);
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

void mlx5e_close_cq(struct mlx5e_cq *cq)
{
	mlx5e_destroy_cq(cq);
	mlx5e_free_cq(cq);
}

static int mlx5e_open_tx_cqs(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct mlx5e_create_cq_param *ccp,
			     struct mlx5e_channel_param *cparam)
{
	int err;
	int tc;

	for (tc = 0; tc < c->num_tc; tc++) {
		err = mlx5e_open_cq(c->priv, params->tx_cq_moderation, &cparam->txq_sq.cqp,
				    ccp, &c->sq[tc].cq);
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
	int err, tc;

	for (tc = 0; tc < params->num_tc; tc++) {
		int txq_ix = c->ix + tc * params->num_channels;

		err = mlx5e_open_txqsq(c, c->priv->tisn[c->lag_port][tc], txq_ix,
				       params, &cparam->txq_sq, &c->sq[tc], tc, 0, 0);
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
	struct mlx5_rate_limit rl = {0};
	u16 rl_index = 0;
	int err;

	if (rate == sq->rate_limit)
		/* nothing to do */
		return 0;

	if (sq->rate_limit) {
		rl.rate = sq->rate_limit;
		/* remove current rl index to free space to next ones */
		mlx5_rl_remove_rate(mdev, &rl);
	}

	sq->rate_limit = 0;

	if (rate) {
		rl.rate = rate;
		err = mlx5_rl_add_rate(mdev, &rl_index, &rl);
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
			mlx5_rl_remove_rate(mdev, &rl);
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

void mlx5e_build_create_cq_param(struct mlx5e_create_cq_param *ccp, struct mlx5e_channel *c)
{
	*ccp = (struct mlx5e_create_cq_param) {
		.napi = &c->napi,
		.ch_stats = c->stats,
		.node = cpu_to_node(c->cpu),
		.ix = c->ix,
	};
}

static int mlx5e_open_queues(struct mlx5e_channel *c,
			     struct mlx5e_params *params,
			     struct mlx5e_channel_param *cparam)
{
	struct dim_cq_moder icocq_moder = {0, 0};
	struct mlx5e_create_cq_param ccp;
	int err;

	mlx5e_build_create_cq_param(&ccp, c);

	err = mlx5e_open_cq(c->priv, icocq_moder, &cparam->async_icosq.cqp, &ccp,
			    &c->async_icosq.cq);
	if (err)
		return err;

	err = mlx5e_open_cq(c->priv, icocq_moder, &cparam->icosq.cqp, &ccp,
			    &c->icosq.cq);
	if (err)
		goto err_close_async_icosq_cq;

	err = mlx5e_open_tx_cqs(c, params, &ccp, cparam);
	if (err)
		goto err_close_icosq_cq;

	err = mlx5e_open_cq(c->priv, params->tx_cq_moderation, &cparam->xdp_sq.cqp, &ccp,
			    &c->xdpsq.cq);
	if (err)
		goto err_close_tx_cqs;

	err = mlx5e_open_cq(c->priv, params->rx_cq_moderation, &cparam->rq.cqp, &ccp,
			    &c->rq.cq);
	if (err)
		goto err_close_xdp_tx_cqs;

	err = c->xdp ? mlx5e_open_cq(c->priv, params->tx_cq_moderation, &cparam->xdp_sq.cqp,
				     &ccp, &c->rq_xdpsq.cq) : 0;
	if (err)
		goto err_close_rx_cq;

	spin_lock_init(&c->async_icosq_lock);

	err = mlx5e_open_icosq(c, params, &cparam->async_icosq, &c->async_icosq);
	if (err)
		goto err_close_xdpsq_cq;

	err = mlx5e_open_icosq(c, params, &cparam->icosq, &c->icosq);
	if (err)
		goto err_close_async_icosq;

	err = mlx5e_open_sqs(c, params, cparam);
	if (err)
		goto err_close_icosq;

	if (c->xdp) {
		err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq, NULL,
				       &c->rq_xdpsq, false);
		if (err)
			goto err_close_sqs;
	}

	err = mlx5e_open_rq(c, params, &cparam->rq, NULL, NULL, &c->rq);
	if (err)
		goto err_close_xdp_sq;

	err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq, NULL, &c->xdpsq, true);
	if (err)
		goto err_close_rq;

	return 0;

err_close_rq:
	mlx5e_close_rq(&c->rq);

err_close_xdp_sq:
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq_xdpsq);

err_close_sqs:
	mlx5e_close_sqs(c);

err_close_icosq:
	mlx5e_close_icosq(&c->icosq);

err_close_async_icosq:
	mlx5e_close_icosq(&c->async_icosq);

err_close_xdpsq_cq:
	if (c->xdp)
		mlx5e_close_cq(&c->rq_xdpsq.cq);

err_close_rx_cq:
	mlx5e_close_cq(&c->rq.cq);

err_close_xdp_tx_cqs:
	mlx5e_close_cq(&c->xdpsq.cq);

err_close_tx_cqs:
	mlx5e_close_tx_cqs(c);

err_close_icosq_cq:
	mlx5e_close_cq(&c->icosq.cq);

err_close_async_icosq_cq:
	mlx5e_close_cq(&c->async_icosq.cq);

	return err;
}

static void mlx5e_close_queues(struct mlx5e_channel *c)
{
	mlx5e_close_xdpsq(&c->xdpsq);
	mlx5e_close_rq(&c->rq);
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq_xdpsq);
	mlx5e_close_sqs(c);
	mlx5e_close_icosq(&c->icosq);
	mlx5e_close_icosq(&c->async_icosq);
	if (c->xdp)
		mlx5e_close_cq(&c->rq_xdpsq.cq);
	mlx5e_close_cq(&c->rq.cq);
	mlx5e_close_cq(&c->xdpsq.cq);
	mlx5e_close_tx_cqs(c);
	mlx5e_close_cq(&c->icosq.cq);
	mlx5e_close_cq(&c->async_icosq.cq);
}

static u8 mlx5e_enumerate_lag_port(struct mlx5_core_dev *mdev, int ix)
{
	u16 port_aff_bias = mlx5_core_is_pf(mdev) ? 0 : MLX5_CAP_GEN(mdev, vhca_id);

	return (ix + port_aff_bias) % mlx5e_get_num_lag_ports(mdev);
}

static int mlx5e_open_channel(struct mlx5e_priv *priv, int ix,
			      struct mlx5e_params *params,
			      struct mlx5e_channel_param *cparam,
			      struct xsk_buff_pool *xsk_pool,
			      struct mlx5e_channel **cp)
{
	int cpu = cpumask_first(mlx5_comp_irq_get_affinity_mask(priv->mdev, ix));
	struct net_device *netdev = priv->netdev;
	struct mlx5e_xsk_param xsk;
	struct mlx5e_channel *c;
	unsigned int irq;
	int err;
	int eqn;

	err = mlx5_vector2eqn(priv->mdev, ix, &eqn, &irq);
	if (err)
		return err;

	c = kvzalloc_node(sizeof(*c), GFP_KERNEL, cpu_to_node(cpu));
	if (!c)
		return -ENOMEM;

	c->priv     = priv;
	c->mdev     = priv->mdev;
	c->tstamp   = &priv->tstamp;
	c->ix       = ix;
	c->cpu      = cpu;
	c->pdev     = mlx5_core_dma_dev(priv->mdev);
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.mkey.key);
	c->num_tc   = params->num_tc;
	c->xdp      = !!params->xdp_prog;
	c->stats    = &priv->channel_stats[ix].ch;
	c->aff_mask = irq_get_effective_affinity_mask(irq);
	c->lag_port = mlx5e_enumerate_lag_port(priv->mdev, ix);

	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll, 64);

	err = mlx5e_open_queues(c, params, cparam);
	if (unlikely(err))
		goto err_napi_del;

	if (xsk_pool) {
		mlx5e_build_xsk_param(xsk_pool, &xsk);
		err = mlx5e_open_xsk(priv, params, &xsk, xsk_pool, c);
		if (unlikely(err))
			goto err_close_queues;
	}

	*cp = c;

	return 0;

err_close_queues:
	mlx5e_close_queues(c);

err_napi_del:
	netif_napi_del(&c->napi);

	kvfree(c);

	return err;
}

static void mlx5e_activate_channel(struct mlx5e_channel *c)
{
	int tc;

	napi_enable(&c->napi);

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_activate_txqsq(&c->sq[tc]);
	mlx5e_activate_icosq(&c->icosq);
	mlx5e_activate_icosq(&c->async_icosq);
	mlx5e_activate_rq(&c->rq);

	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_activate_xsk(c);
}

static void mlx5e_deactivate_channel(struct mlx5e_channel *c)
{
	int tc;

	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_deactivate_xsk(c);

	mlx5e_deactivate_rq(&c->rq);
	mlx5e_deactivate_icosq(&c->async_icosq);
	mlx5e_deactivate_icosq(&c->icosq);
	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_deactivate_txqsq(&c->sq[tc]);
	mlx5e_qos_deactivate_queues(c);

	napi_disable(&c->napi);
}

static void mlx5e_close_channel(struct mlx5e_channel *c)
{
	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_close_xsk(c);
	mlx5e_close_queues(c);
	mlx5e_qos_close_queues(c);
	netif_napi_del(&c->napi);

	kvfree(c);
}

#define DEFAULT_FRAG_SIZE (2048)

static void mlx5e_build_rq_frags_info(struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params,
				      struct mlx5e_xsk_param *xsk,
				      struct mlx5e_rq_frags_info *info)
{
	u32 byte_count = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	int frag_size_max = DEFAULT_FRAG_SIZE;
	u32 buf_size = 0;
	int i;

	if (mlx5_fpga_is_ipsec_device(mdev))
		byte_count += MLX5E_METADATA_ETHER_LEN;

	if (mlx5e_rx_is_linear_skb(params, xsk)) {
		int frag_stride;

		frag_stride = mlx5e_rx_get_linear_frag_sz(params, xsk);
		frag_stride = roundup_pow_of_two(frag_stride);

		info->arr[0].frag_size = byte_count;
		info->arr[0].frag_stride = frag_stride;
		info->num_frags = 1;
		info->wqe_bulk = PAGE_SIZE / frag_stride;
		goto out;
	}

	if (byte_count > PAGE_SIZE +
	    (MLX5E_MAX_RX_FRAGS - 1) * frag_size_max)
		frag_size_max = PAGE_SIZE;

	i = 0;
	while (buf_size < byte_count) {
		int frag_size = byte_count - buf_size;

		if (i < MLX5E_MAX_RX_FRAGS - 1)
			frag_size = min(frag_size, frag_size_max);

		info->arr[i].frag_size = frag_size;
		info->arr[i].frag_stride = roundup_pow_of_two(frag_size);

		buf_size += frag_size;
		i++;
	}
	info->num_frags = i;
	/* number of different wqes sharing a page */
	info->wqe_bulk = 1 + (info->num_frags % 2);

out:
	info->wqe_bulk = max_t(u8, info->wqe_bulk, 8);
	info->log_num_frags = order_base_2(info->num_frags);
}

static inline u8 mlx5e_get_rqwq_log_stride(u8 wq_type, int ndsegs)
{
	int sz = sizeof(struct mlx5_wqe_data_seg) * ndsegs;

	switch (wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		sz += sizeof(struct mlx5e_rx_wqe_ll);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		sz += sizeof(struct mlx5e_rx_wqe_cyc);
	}

	return order_base_2(sz);
}

static u8 mlx5e_get_rq_log_wq_sz(void *rqc)
{
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	return MLX5_GET(wq, wq, log_wq_sz);
}

void mlx5e_build_rq_param(struct mlx5e_priv *priv,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk,
			  struct mlx5e_rq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int ndsegs = 1;

	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		MLX5_SET(wq, wq, log_wqe_num_of_strides,
			 mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk) -
			 MLX5_MPWQE_LOG_NUM_STRIDES_BASE);
		MLX5_SET(wq, wq, log_wqe_stride_size,
			 mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk) -
			 MLX5_MPWQE_LOG_STRIDE_SZ_BASE);
		MLX5_SET(wq, wq, log_wq_sz, mlx5e_mpwqe_get_log_rq_size(params, xsk));
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		MLX5_SET(wq, wq, log_wq_sz, params->log_rq_mtu_frames);
		mlx5e_build_rq_frags_info(mdev, params, xsk, &param->frags_info);
		ndsegs = param->frags_info.num_frags;
	}

	MLX5_SET(wq, wq, wq_type,          params->rq_wq_type);
	MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);
	MLX5_SET(wq, wq, log_wq_stride,
		 mlx5e_get_rqwq_log_stride(params->rq_wq_type, ndsegs));
	MLX5_SET(wq, wq, pd,               mdev->mlx5e_res.pdn);
	MLX5_SET(rqc, rqc, counter_set_id, priv->q_counter);
	MLX5_SET(rqc, rqc, vsd,            params->vlan_strip_disable);
	MLX5_SET(rqc, rqc, scatter_fcs,    params->scatter_fcs_en);

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
	mlx5e_build_rx_cq_param(priv, params, xsk, &param->cqp);
}

static void mlx5e_build_drop_rq_param(struct mlx5e_priv *priv,
				      struct mlx5e_rq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq, wq, log_wq_stride,
		 mlx5e_get_rqwq_log_stride(MLX5_WQ_TYPE_CYCLIC, 1));
	MLX5_SET(rqc, rqc, counter_set_id, priv->drop_rq_q_counter);

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
}

void mlx5e_build_sq_param_common(struct mlx5e_priv *priv,
				 struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd,            priv->mdev->mlx5e_res.pdn);

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(priv->mdev));
}

void mlx5e_build_sq_param(struct mlx5e_priv *priv, struct mlx5e_params *params,
			  struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);
	bool allow_swp;

	allow_swp = mlx5_geneve_tx_allowed(priv->mdev) ||
		    !!MLX5_IPSEC_DEV(priv->mdev);
	mlx5e_build_sq_param_common(priv, param);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	MLX5_SET(sqc, sqc, allow_swp, allow_swp);
	param->is_mpw = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE);
	param->stop_room = mlx5e_calc_sq_stop_room(priv->mdev, params);
	mlx5e_build_tx_cq_param(priv, params, &param->cqp);
}

static void mlx5e_build_common_cq_param(struct mlx5e_priv *priv,
					struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, uar_page, priv->mdev->priv.uar->index);
	if (MLX5_CAP_GEN(priv->mdev, cqe_128_always) && cache_line_size() >= 128)
		MLX5_SET(cqc, cqc, cqe_sz, CQE_STRIDE_128_PAD);
}

void mlx5e_build_rx_cq_param(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk,
			     struct mlx5e_cq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	bool hw_stridx = false;
	void *cqc = param->cqc;
	u8 log_cq_size;

	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		log_cq_size = mlx5e_mpwqe_get_log_rq_size(params, xsk) +
			mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk);
		hw_stridx = MLX5_CAP_GEN(mdev, mini_cqe_resp_stride_index);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		log_cq_size = params->log_rq_mtu_frames;
	}

	MLX5_SET(cqc, cqc, log_cq_size, log_cq_size);
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		MLX5_SET(cqc, cqc, mini_cqe_res_format, hw_stridx ?
			 MLX5_CQE_FORMAT_CSUM_STRIDX : MLX5_CQE_FORMAT_CSUM);
		MLX5_SET(cqc, cqc, cqe_comp_en, 1);
	}

	mlx5e_build_common_cq_param(priv, param);
	param->cq_period_mode = params->rx_cq_moderation.cq_period_mode;
}

void mlx5e_build_tx_cq_param(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, params->log_sq_size);

	mlx5e_build_common_cq_param(priv, param);
	param->cq_period_mode = params->tx_cq_moderation.cq_period_mode;
}

void mlx5e_build_ico_cq_param(struct mlx5e_priv *priv,
			      u8 log_wq_size,
			      struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, log_wq_size);

	mlx5e_build_common_cq_param(priv, param);

	param->cq_period_mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
}

void mlx5e_build_icosq_param(struct mlx5e_priv *priv,
			     u8 log_wq_size,
			     struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);

	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
	MLX5_SET(sqc, sqc, reg_umr, MLX5_CAP_ETH(priv->mdev, reg_umr_sq));
	mlx5e_build_ico_cq_param(priv, log_wq_size, &param->cqp);
}

static void mlx5e_build_async_icosq_param(struct mlx5e_priv *priv,
					  struct mlx5e_params *params,
					  u8 log_wq_size,
					  struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);

	/* async_icosq is used by XSK only if xdp_prog is active */
	if (params->xdp_prog)
		param->stop_room = mlx5e_stop_room_for_wqe(1); /* for XSK NOP */
	MLX5_SET(sqc, sqc, reg_umr, MLX5_CAP_ETH(priv->mdev, reg_umr_sq));
	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
	mlx5e_build_ico_cq_param(priv, log_wq_size, &param->cqp);
}

void mlx5e_build_xdpsq_param(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	param->is_mpw = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_XDP_TX_MPWQE);
	mlx5e_build_tx_cq_param(priv, params, &param->cqp);
}

static u8 mlx5e_build_icosq_log_wq_sz(struct mlx5e_params *params,
				      struct mlx5e_rq_param *rqp)
{
	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return max_t(u8, MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE,
			     order_base_2(MLX5E_UMR_WQEBBS) +
			     mlx5e_get_rq_log_wq_sz(rqp->rqc));
	default: /* MLX5_WQ_TYPE_CYCLIC */
		return MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;
	}
}

static u8 mlx5e_build_async_icosq_log_wq_sz(struct net_device *netdev)
{
	if (netdev->hw_features & NETIF_F_HW_TLS_RX)
		return MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;

	return MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;
}

static void mlx5e_build_channel_param(struct mlx5e_priv *priv,
				      struct mlx5e_params *params,
				      struct mlx5e_channel_param *cparam)
{
	u8 icosq_log_wq_sz, async_icosq_log_wq_sz;

	mlx5e_build_rq_param(priv, params, NULL, &cparam->rq);

	icosq_log_wq_sz = mlx5e_build_icosq_log_wq_sz(params, &cparam->rq);
	async_icosq_log_wq_sz = mlx5e_build_async_icosq_log_wq_sz(priv->netdev);

	mlx5e_build_sq_param(priv, params, &cparam->txq_sq);
	mlx5e_build_xdpsq_param(priv, params, &cparam->xdp_sq);
	mlx5e_build_icosq_param(priv, icosq_log_wq_sz, &cparam->icosq);
	mlx5e_build_async_icosq_param(priv, params, async_icosq_log_wq_sz, &cparam->async_icosq);
}

int mlx5e_open_channels(struct mlx5e_priv *priv,
			struct mlx5e_channels *chs)
{
	struct mlx5e_channel_param *cparam;
	int err = -ENOMEM;
	int i;

	chs->num = chs->params.num_channels;

	chs->c = kcalloc(chs->num, sizeof(struct mlx5e_channel *), GFP_KERNEL);
	cparam = kvzalloc(sizeof(struct mlx5e_channel_param), GFP_KERNEL);
	if (!chs->c || !cparam)
		goto err_free;

	mlx5e_build_channel_param(priv, &chs->params, cparam);
	for (i = 0; i < chs->num; i++) {
		struct xsk_buff_pool *xsk_pool = NULL;

		if (chs->params.xdp_prog)
			xsk_pool = mlx5e_xsk_get_pool(&chs->params, chs->params.xsk, i);

		err = mlx5e_open_channel(priv, i, &chs->params, cparam, xsk_pool, &chs->c[i]);
		if (err)
			goto err_close_channels;
	}

	if (MLX5E_GET_PFLAG(&chs->params, MLX5E_PFLAG_TX_PORT_TS)) {
		err = mlx5e_port_ptp_open(priv, &chs->params, chs->c[0]->lag_port,
					  &chs->port_ptp);
		if (err)
			goto err_close_channels;
	}

	err = mlx5e_qos_open_queues(priv, chs);
	if (err)
		goto err_close_ptp;

	mlx5e_health_channels_update(priv);
	kvfree(cparam);
	return 0;

err_close_ptp:
	if (chs->port_ptp)
		mlx5e_port_ptp_close(chs->port_ptp);

err_close_channels:
	for (i--; i >= 0; i--)
		mlx5e_close_channel(chs->c[i]);

err_free:
	kfree(chs->c);
	kvfree(cparam);
	chs->num = 0;
	return err;
}

static void mlx5e_activate_channels(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_activate_channel(chs->c[i]);

	if (chs->port_ptp)
		mlx5e_ptp_activate_channel(chs->port_ptp);
}

#define MLX5E_RQ_WQES_TIMEOUT 20000 /* msecs */

static int mlx5e_wait_channels_min_rx_wqes(struct mlx5e_channels *chs)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		int timeout = err ? 0 : MLX5E_RQ_WQES_TIMEOUT;

		err |= mlx5e_wait_for_min_rx_wqes(&chs->c[i]->rq, timeout);

		/* Don't wait on the XSK RQ, because the newer xdpsock sample
		 * doesn't provide any Fill Ring entries at the setup stage.
		 */
	}

	return err ? -ETIMEDOUT : 0;
}

static void mlx5e_deactivate_channels(struct mlx5e_channels *chs)
{
	int i;

	if (chs->port_ptp)
		mlx5e_ptp_deactivate_channel(chs->port_ptp);

	for (i = 0; i < chs->num; i++)
		mlx5e_deactivate_channel(chs->c[i]);
}

void mlx5e_close_channels(struct mlx5e_channels *chs)
{
	int i;

	if (chs->port_ptp) {
		mlx5e_port_ptp_close(chs->port_ptp);
		chs->port_ptp = NULL;
	}

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

int mlx5e_create_direct_rqts(struct mlx5e_priv *priv, struct mlx5e_tir *tirs)
{
	int err;
	int ix;

	for (ix = 0; ix < priv->max_nch; ix++) {
		err = mlx5e_create_rqt(priv, 1 /*size */, &tirs[ix].rqt);
		if (unlikely(err))
			goto err_destroy_rqts;
	}

	return 0;

err_destroy_rqts:
	mlx5_core_warn(priv->mdev, "create rqts failed, %d\n", err);
	for (ix--; ix >= 0; ix--)
		mlx5e_destroy_rqt(priv, &tirs[ix].rqt);

	return err;
}

void mlx5e_destroy_direct_rqts(struct mlx5e_priv *priv, struct mlx5e_tir *tirs)
{
	int i;

	for (i = 0; i < priv->max_nch; i++)
		mlx5e_destroy_rqt(priv, &tirs[i].rqt);
}

static int mlx5e_rx_hash_fn(int hfunc)
{
	return (hfunc == ETH_RSS_HASH_TOP) ?
	       MLX5_RX_HASH_FN_TOEPLITZ :
	       MLX5_RX_HASH_FN_INVERTED_XOR8;
}

int mlx5e_bits_invert(unsigned long a, int size)
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

			ix = priv->rss_params.indirection_rqt[ix];
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

	for (ix = 0; ix < priv->max_nch; ix++) {
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
				.hfunc     = priv->rss_params.hfunc,
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

static const struct mlx5e_tirc_config tirc_default_config[MLX5E_NUM_INDIR_TIRS] = {
	[MLX5E_TT_IPV4_TCP] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
				.l4_prot_type = MLX5_L4_PROT_TYPE_TCP,
				.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV6_TCP] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
				.l4_prot_type = MLX5_L4_PROT_TYPE_TCP,
				.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV4_UDP] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
				.l4_prot_type = MLX5_L4_PROT_TYPE_UDP,
				.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV6_UDP] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
				.l4_prot_type = MLX5_L4_PROT_TYPE_UDP,
				.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV4_IPSEC_AH] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
				     .l4_prot_type = 0,
				     .rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV6_IPSEC_AH] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
				     .l4_prot_type = 0,
				     .rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV4_IPSEC_ESP] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
				      .l4_prot_type = 0,
				      .rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV6_IPSEC_ESP] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
				      .l4_prot_type = 0,
				      .rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV4] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
			    .l4_prot_type = 0,
			    .rx_hash_fields = MLX5_HASH_IP,
	},
	[MLX5E_TT_IPV6] = { .l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
			    .l4_prot_type = 0,
			    .rx_hash_fields = MLX5_HASH_IP,
	},
};

struct mlx5e_tirc_config mlx5e_tirc_get_default_config(enum mlx5e_traffic_types tt)
{
	return tirc_default_config[tt];
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
		 (MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ - ROUGH_MAX_L2_L3_HDR_SZ) >> 8);
	MLX5_SET(tirc, tirc, lro_timeout_period_usecs, params->lro_timeout);
}

void mlx5e_build_indir_tir_ctx_hash(struct mlx5e_rss_params *rss_params,
				    const struct mlx5e_tirc_config *ttconfig,
				    void *tirc, bool inner)
{
	void *hfso = inner ? MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_inner) :
			     MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);

	MLX5_SET(tirc, tirc, rx_hash_fn, mlx5e_rx_hash_fn(rss_params->hfunc));
	if (rss_params->hfunc == ETH_RSS_HASH_TOP) {
		void *rss_key = MLX5_ADDR_OF(tirc, tirc,
					     rx_hash_toeplitz_key);
		size_t len = MLX5_FLD_SZ_BYTES(tirc,
					       rx_hash_toeplitz_key);

		MLX5_SET(tirc, tirc, rx_hash_symmetric, 1);
		memcpy(rss_key, rss_params->toeplitz_hash_key, len);
	}
	MLX5_SET(rx_hash_field_select, hfso, l3_prot_type,
		 ttconfig->l3_prot_type);
	MLX5_SET(rx_hash_field_select, hfso, l4_prot_type,
		 ttconfig->l4_prot_type);
	MLX5_SET(rx_hash_field_select, hfso, selected_fields,
		 ttconfig->rx_hash_fields);
}

static void mlx5e_update_rx_hash_fields(struct mlx5e_tirc_config *ttconfig,
					enum mlx5e_traffic_types tt,
					u32 rx_hash_fields)
{
	*ttconfig                = tirc_default_config[tt];
	ttconfig->rx_hash_fields = rx_hash_fields;
}

void mlx5e_modify_tirs_hash(struct mlx5e_priv *priv, void *in)
{
	void *tirc = MLX5_ADDR_OF(modify_tir_in, in, ctx);
	struct mlx5e_rss_params *rss = &priv->rss_params;
	struct mlx5_core_dev *mdev = priv->mdev;
	int ctxlen = MLX5_ST_SZ_BYTES(tirc);
	struct mlx5e_tirc_config ttconfig;
	int tt;

	MLX5_SET(modify_tir_in, in, bitmask.hash, 1);

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		memset(tirc, 0, ctxlen);
		mlx5e_update_rx_hash_fields(&ttconfig, tt,
					    rss->rx_hash_fields[tt]);
		mlx5e_build_indir_tir_ctx_hash(rss, &ttconfig, tirc, false);
		mlx5_core_modify_tir(mdev, priv->indir_tir[tt].tirn, in);
	}

	/* Verify inner tirs resources allocated */
	if (!priv->inner_indir_tir[0].tirn)
		return;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		memset(tirc, 0, ctxlen);
		mlx5e_update_rx_hash_fields(&ttconfig, tt,
					    rss->rx_hash_fields[tt]);
		mlx5e_build_indir_tir_ctx_hash(rss, &ttconfig, tirc, true);
		mlx5_core_modify_tir(mdev, priv->inner_indir_tir[tt].tirn, in);
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
		err = mlx5_core_modify_tir(mdev, priv->indir_tir[tt].tirn, in);
		if (err)
			goto free_in;
	}

	for (ix = 0; ix < priv->max_nch; ix++) {
		err = mlx5_core_modify_tir(mdev, priv->direct_tir[ix].tirn, in);
		if (err)
			goto free_in;
	}

free_in:
	kvfree(in);

	return err;
}

static MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_modify_tirs_lro);

static int mlx5e_set_mtu(struct mlx5_core_dev *mdev,
			 struct mlx5e_params *params, u16 mtu)
{
	u16 hw_mtu = MLX5E_SW2HW_MTU(params, mtu);
	int err;

	err = mlx5_set_port_mtu(mdev, hw_mtu, 1);
	if (err)
		return err;

	/* Update vport context MTU */
	mlx5_modify_nic_vport_mtu(mdev, hw_mtu);
	return 0;
}

static void mlx5e_query_mtu(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params, u16 *mtu)
{
	u16 hw_mtu = 0;
	int err;

	err = mlx5_query_nic_vport_mtu(mdev, &hw_mtu);
	if (err || !hw_mtu) /* fallback to port oper mtu */
		mlx5_query_port_oper_mtu(mdev, &hw_mtu, 1);

	*mtu = MLX5E_HW2SW_MTU(params, hw_mtu);
}

int mlx5e_set_dev_port_mtu(struct mlx5e_priv *priv)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 mtu;
	int err;

	err = mlx5e_set_mtu(mdev, params, params->sw_mtu);
	if (err)
		return err;

	mlx5e_query_mtu(mdev, params, &mtu);
	if (mtu != params->sw_mtu)
		netdev_warn(netdev, "%s: VPort MTU %d is different than netdev mtu %d\n",
			    __func__, mtu, params->sw_mtu);

	params->sw_mtu = mtu;
	return 0;
}

MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_set_dev_port_mtu);

void mlx5e_set_netdev_mtu_boundaries(struct mlx5e_priv *priv)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct net_device *netdev   = priv->netdev;
	struct mlx5_core_dev *mdev  = priv->mdev;
	u16 max_mtu;

	/* MTU range: 68 - hw-specific max */
	netdev->min_mtu = ETH_MIN_MTU;

	mlx5_query_port_max_mtu(mdev, &max_mtu, 1);
	netdev->max_mtu = min_t(unsigned int, MLX5E_HW2SW_MTU(params, max_mtu),
				ETH_MAX_MTU);
}

static void mlx5e_netdev_set_tcs(struct net_device *netdev, u16 nch, u8 ntc)
{
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

int mlx5e_update_tx_netdev_queues(struct mlx5e_priv *priv)
{
	int qos_queues, nch, ntc, num_txqs, err;

	qos_queues = mlx5e_qos_cur_leaf_nodes(priv);

	nch = priv->channels.params.num_channels;
	ntc = priv->channels.params.num_tc;
	num_txqs = nch * ntc + qos_queues;
	if (MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_TX_PORT_TS))
		num_txqs += ntc;

	mlx5e_dbg(DRV, priv, "Setting num_txqs %d\n", num_txqs);
	err = netif_set_real_num_tx_queues(priv->netdev, num_txqs);
	if (err)
		netdev_warn(priv->netdev, "netif_set_real_num_tx_queues failed, %d\n", err);

	return err;
}

static int mlx5e_update_netdev_queues(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	int old_num_txqs, old_ntc;
	int num_rxqs, nch, ntc;
	int err;

	old_num_txqs = netdev->real_num_tx_queues;
	old_ntc = netdev->num_tc;

	nch = priv->channels.params.num_channels;
	ntc = priv->channels.params.num_tc;
	num_rxqs = nch * priv->profile->rq_groups;

	mlx5e_netdev_set_tcs(netdev, nch, ntc);

	err = mlx5e_update_tx_netdev_queues(priv);
	if (err)
		goto err_tcs;
	err = netif_set_real_num_rx_queues(netdev, num_rxqs);
	if (err) {
		netdev_warn(netdev, "netif_set_real_num_rx_queues failed, %d\n", err);
		goto err_txqs;
	}

	return 0;

err_txqs:
	/* netif_set_real_num_rx_queues could fail only when nch increased. Only
	 * one of nch and ntc is changed in this function. That means, the call
	 * to netif_set_real_num_tx_queues below should not fail, because it
	 * decreases the number of TX queues.
	 */
	WARN_ON_ONCE(netif_set_real_num_tx_queues(netdev, old_num_txqs));

err_tcs:
	mlx5e_netdev_set_tcs(netdev, old_num_txqs / old_ntc, old_ntc);
	return err;
}

static void mlx5e_set_default_xps_cpumasks(struct mlx5e_priv *priv,
					   struct mlx5e_params *params)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int num_comp_vectors, ix, irq;

	num_comp_vectors = mlx5_comp_vectors_count(mdev);

	for (ix = 0; ix < params->num_channels; ix++) {
		cpumask_clear(priv->scratchpad.cpumask);

		for (irq = ix; irq < num_comp_vectors; irq += params->num_channels) {
			int cpu = cpumask_first(mlx5_comp_irq_get_affinity_mask(mdev, irq));

			cpumask_set_cpu(cpu, priv->scratchpad.cpumask);
		}

		netif_set_xps_queue(priv->netdev, priv->scratchpad.cpumask, ix);
	}
}

int mlx5e_num_channels_changed(struct mlx5e_priv *priv)
{
	u16 count = priv->channels.params.num_channels;
	int err;

	err = mlx5e_update_netdev_queues(priv);
	if (err)
		return err;

	mlx5e_set_default_xps_cpumasks(priv, &priv->channels.params);

	if (!netif_is_rxfh_configured(priv->netdev))
		mlx5e_build_default_indir_rqt(priv->rss_params.indirection_rqt,
					      MLX5E_INDIR_RQT_SIZE, count);

	return 0;
}

MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_num_channels_changed);

static void mlx5e_build_txq_maps(struct mlx5e_priv *priv)
{
	int i, ch, tc, num_tc;

	ch = priv->channels.num;
	num_tc = priv->channels.params.num_tc;

	for (i = 0; i < ch; i++) {
		for (tc = 0; tc < num_tc; tc++) {
			struct mlx5e_channel *c = priv->channels.c[i];
			struct mlx5e_txqsq *sq = &c->sq[tc];

			priv->txq2sq[sq->txq_ix] = sq;
			priv->channel_tc2realtxq[i][tc] = i + tc * ch;
		}
	}

	if (!priv->channels.port_ptp)
		return;

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_port_ptp *c = priv->channels.port_ptp;
		struct mlx5e_txqsq *sq = &c->ptpsq[tc].txqsq;

		priv->txq2sq[sq->txq_ix] = sq;
		priv->port_ptp_tc2realtxq[tc] = priv->num_tc_x_num_ch + tc;
	}
}

static void mlx5e_update_num_tc_x_num_ch(struct mlx5e_priv *priv)
{
	/* Sync with mlx5e_select_queue. */
	WRITE_ONCE(priv->num_tc_x_num_ch,
		   priv->channels.params.num_tc * priv->channels.num);
}

void mlx5e_activate_priv_channels(struct mlx5e_priv *priv)
{
	mlx5e_update_num_tc_x_num_ch(priv);
	mlx5e_build_txq_maps(priv);
	mlx5e_activate_channels(&priv->channels);
	mlx5e_qos_activate_queues(priv);
	mlx5e_xdp_tx_enable(priv);
	netif_tx_start_all_queues(priv->netdev);

	if (mlx5e_is_vport_rep(priv))
		mlx5e_add_sqs_fwd_rules(priv);

	mlx5e_wait_channels_min_rx_wqes(&priv->channels);
	mlx5e_redirect_rqts_to_channels(priv, &priv->channels);

	mlx5e_xsk_redirect_rqts_to_channels(priv, &priv->channels);
}

void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv)
{
	mlx5e_xsk_redirect_rqts_to_drop(priv, &priv->channels);

	mlx5e_redirect_rqts_to_drop(priv);

	if (mlx5e_is_vport_rep(priv))
		mlx5e_remove_sqs_fwd_rules(priv);

	/* FIXME: This is a W/A only for tx timeout watch dog false alarm when
	 * polling for inactive tx queues.
	 */
	netif_tx_stop_all_queues(priv->netdev);
	netif_tx_disable(priv->netdev);
	mlx5e_xdp_tx_disable(priv);
	mlx5e_deactivate_channels(&priv->channels);
}

static int mlx5e_switch_priv_channels(struct mlx5e_priv *priv,
				      struct mlx5e_channels *new_chs,
				      mlx5e_fp_preactivate preactivate,
				      void *context)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5e_channels old_chs;
	int carrier_ok;
	int err = 0;

	carrier_ok = netif_carrier_ok(netdev);
	netif_carrier_off(netdev);

	mlx5e_deactivate_priv_channels(priv);

	old_chs = priv->channels;
	priv->channels = *new_chs;

	/* New channels are ready to roll, call the preactivate hook if needed
	 * to modify HW settings or update kernel parameters.
	 */
	if (preactivate) {
		err = preactivate(priv, context);
		if (err) {
			priv->channels = old_chs;
			goto out;
		}
	}

	mlx5e_close_channels(&old_chs);
	priv->profile->update_rx(priv);

out:
	mlx5e_activate_priv_channels(priv);

	/* return carrier back if needed */
	if (carrier_ok)
		netif_carrier_on(netdev);

	return err;
}

int mlx5e_safe_switch_channels(struct mlx5e_priv *priv,
			       struct mlx5e_channels *new_chs,
			       mlx5e_fp_preactivate preactivate,
			       void *context)
{
	int err;

	err = mlx5e_open_channels(priv, new_chs);
	if (err)
		return err;

	err = mlx5e_switch_priv_channels(priv, new_chs, preactivate, context);
	if (err)
		goto err_close;

	return 0;

err_close:
	mlx5e_close_channels(new_chs);

	return err;
}

int mlx5e_safe_reopen_channels(struct mlx5e_priv *priv)
{
	struct mlx5e_channels new_channels = {};

	new_channels.params = priv->channels.params;
	return mlx5e_safe_switch_channels(priv, &new_channels, NULL, NULL);
}

void mlx5e_timestamp_init(struct mlx5e_priv *priv)
{
	priv->tstamp.tx_type   = HWTSTAMP_TX_OFF;
	priv->tstamp.rx_filter = HWTSTAMP_FILTER_NONE;
}

static void mlx5e_modify_admin_state(struct mlx5_core_dev *mdev,
				     enum mlx5_port_status state)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int vport_admin_state;

	mlx5_set_port_admin_status(mdev, state);

	if (mlx5_eswitch_mode(mdev) == MLX5_ESWITCH_OFFLOADS ||
	    !MLX5_CAP_GEN(mdev, uplink_follow))
		return;

	if (state == MLX5_PORT_UP)
		vport_admin_state = MLX5_VPORT_ADMIN_STATE_AUTO;
	else
		vport_admin_state = MLX5_VPORT_ADMIN_STATE_DOWN;

	mlx5_eswitch_set_vport_state(esw, MLX5_VPORT_UPLINK, vport_admin_state);
}

int mlx5e_open_locked(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	err = mlx5e_open_channels(priv, &priv->channels);
	if (err)
		goto err_clear_state_opened_flag;

	priv->profile->update_rx(priv);
	mlx5e_activate_priv_channels(priv);
	mlx5e_apply_traps(priv, true);
	if (priv->profile->update_carrier)
		priv->profile->update_carrier(priv);

	mlx5e_queue_update_stats(priv);
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
		mlx5e_modify_admin_state(priv->mdev, MLX5_PORT_UP);
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

	mlx5e_apply_traps(priv, false);
	clear_bit(MLX5E_STATE_OPENED, &priv->state);

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
	mlx5e_modify_admin_state(priv->mdev, MLX5_PORT_DOWN);
	err = mlx5e_close_locked(netdev);
	mutex_unlock(&priv->state_lock);

	return err;
}

static void mlx5e_free_drop_rq(struct mlx5e_rq *rq)
{
	mlx5_wq_destroy(&rq->wq_ctrl);
}

static int mlx5e_alloc_drop_rq(struct mlx5_core_dev *mdev,
			       struct mlx5e_rq *rq,
			       struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int err;

	param->wq.db_numa_node = param->wq.buf_numa_node;

	err = mlx5_wq_cyc_create(mdev, &param->wq, rqc_wq, &rq->wqe.wq,
				 &rq->wq_ctrl);
	if (err)
		return err;

	/* Mark as unused given "Drop-RQ" packets never reach XDP */
	xdp_rxq_info_unused(&rq->xdp_rxq);

	rq->mdev = mdev;

	return 0;
}

static int mlx5e_alloc_drop_cq(struct mlx5e_priv *priv,
			       struct mlx5e_cq *cq,
			       struct mlx5e_cq_param *param)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
	param->wq.db_numa_node  = dev_to_node(mlx5_core_dma_dev(mdev));

	return mlx5e_alloc_cq_common(priv, param, cq);
}

int mlx5e_open_drop_rq(struct mlx5e_priv *priv,
		       struct mlx5e_rq *drop_rq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_cq_param cq_param = {};
	struct mlx5e_rq_param rq_param = {};
	struct mlx5e_cq *cq = &drop_rq->cq;
	int err;

	mlx5e_build_drop_rq_param(priv, &rq_param);

	err = mlx5e_alloc_drop_cq(priv, cq, &cq_param);
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

	err = mlx5e_modify_rq_state(drop_rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		mlx5_core_warn(priv->mdev, "modify_rq_state failed, rx_if_down_packets won't be counted %d\n", err);

	return 0;

err_free_rq:
	mlx5e_free_drop_rq(drop_rq);

err_destroy_cq:
	mlx5e_destroy_cq(cq);

err_free_cq:
	mlx5e_free_cq(cq);

	return err;
}

void mlx5e_close_drop_rq(struct mlx5e_rq *drop_rq)
{
	mlx5e_destroy_rq(drop_rq);
	mlx5e_free_drop_rq(drop_rq);
	mlx5e_destroy_cq(&drop_rq->cq);
	mlx5e_free_cq(&drop_rq->cq);
}

int mlx5e_create_tis(struct mlx5_core_dev *mdev, void *in, u32 *tisn)
{
	void *tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(tisc, tisc, transport_domain, mdev->mlx5e_res.td.tdn);

	if (MLX5_GET(tisc, tisc, tls_en))
		MLX5_SET(tisc, tisc, pd, mdev->mlx5e_res.pdn);

	if (mlx5_lag_is_lacp_owner(mdev))
		MLX5_SET(tisc, tisc, strict_lag_tx_port_affinity, 1);

	return mlx5_core_create_tis(mdev, in, tisn);
}

void mlx5e_destroy_tis(struct mlx5_core_dev *mdev, u32 tisn)
{
	mlx5_core_destroy_tis(mdev, tisn);
}

void mlx5e_destroy_tises(struct mlx5e_priv *priv)
{
	int tc, i;

	for (i = 0; i < mlx5e_get_num_lag_ports(priv->mdev); i++)
		for (tc = 0; tc < priv->profile->max_tc; tc++)
			mlx5e_destroy_tis(priv->mdev, priv->tisn[i][tc]);
}

static bool mlx5e_lag_should_assign_affinity(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_GEN(mdev, lag_tx_port_affinity) && mlx5e_get_num_lag_ports(mdev) > 1;
}

int mlx5e_create_tises(struct mlx5e_priv *priv)
{
	int tc, i;
	int err;

	for (i = 0; i < mlx5e_get_num_lag_ports(priv->mdev); i++) {
		for (tc = 0; tc < priv->profile->max_tc; tc++) {
			u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
			void *tisc;

			tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

			MLX5_SET(tisc, tisc, prio, tc << 1);

			if (mlx5e_lag_should_assign_affinity(priv->mdev))
				MLX5_SET(tisc, tisc, lag_tx_port_affinity, i + 1);

			err = mlx5e_create_tis(priv->mdev, in, &priv->tisn[i][tc]);
			if (err)
				goto err_close_tises;
		}
	}

	return 0;

err_close_tises:
	for (; i >= 0; i--) {
		for (tc--; tc >= 0; tc--)
			mlx5e_destroy_tis(priv->mdev, priv->tisn[i][tc]);
		tc = priv->profile->max_tc;
	}

	return err;
}

static void mlx5e_cleanup_nic_tx(struct mlx5e_priv *priv)
{
	mlx5e_destroy_tises(priv);
}

static void mlx5e_build_indir_tir_ctx_common(struct mlx5e_priv *priv,
					     u32 rqtn, u32 *tirc)
{
	MLX5_SET(tirc, tirc, transport_domain, priv->mdev->mlx5e_res.td.tdn);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table, rqtn);
	MLX5_SET(tirc, tirc, tunneled_offload_en,
		 priv->channels.params.tunneled_offload_en);

	mlx5e_build_tir_ctx_lro(&priv->channels.params, tirc);
}

static void mlx5e_build_indir_tir_ctx(struct mlx5e_priv *priv,
				      enum mlx5e_traffic_types tt,
				      u32 *tirc)
{
	mlx5e_build_indir_tir_ctx_common(priv, priv->indir_rqt.rqtn, tirc);
	mlx5e_build_indir_tir_ctx_hash(&priv->rss_params,
				       &tirc_default_config[tt], tirc, false);
}

static void mlx5e_build_direct_tir_ctx(struct mlx5e_priv *priv, u32 rqtn, u32 *tirc)
{
	mlx5e_build_indir_tir_ctx_common(priv, rqtn, tirc);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_INVERTED_XOR8);
}

static void mlx5e_build_inner_indir_tir_ctx(struct mlx5e_priv *priv,
					    enum mlx5e_traffic_types tt,
					    u32 *tirc)
{
	mlx5e_build_indir_tir_ctx_common(priv, priv->indir_rqt.rqtn, tirc);
	mlx5e_build_indir_tir_ctx_hash(&priv->rss_params,
				       &tirc_default_config[tt], tirc, true);
}

int mlx5e_create_indirect_tirs(struct mlx5e_priv *priv, bool inner_ttc)
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
		err = mlx5e_create_tir(priv->mdev, tir, in);
		if (err) {
			mlx5_core_warn(priv->mdev, "create indirect tirs failed, %d\n", err);
			goto err_destroy_inner_tirs;
		}
	}

	if (!inner_ttc || !mlx5e_tunnel_inner_ft_supported(priv->mdev))
		goto out;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++) {
		memset(in, 0, inlen);
		tir = &priv->inner_indir_tir[i];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_inner_indir_tir_ctx(priv, i, tirc);
		err = mlx5e_create_tir(priv->mdev, tir, in);
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

int mlx5e_create_direct_tirs(struct mlx5e_priv *priv, struct mlx5e_tir *tirs)
{
	struct mlx5e_tir *tir;
	void *tirc;
	int inlen;
	int err = 0;
	u32 *in;
	int ix;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	for (ix = 0; ix < priv->max_nch; ix++) {
		memset(in, 0, inlen);
		tir = &tirs[ix];
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
		mlx5e_build_direct_tir_ctx(priv, tir->rqt.rqtn, tirc);
		err = mlx5e_create_tir(priv->mdev, tir, in);
		if (unlikely(err))
			goto err_destroy_ch_tirs;
	}

	goto out;

err_destroy_ch_tirs:
	mlx5_core_warn(priv->mdev, "create tirs failed, %d\n", err);
	for (ix--; ix >= 0; ix--)
		mlx5e_destroy_tir(priv->mdev, &tirs[ix]);

out:
	kvfree(in);

	return err;
}

void mlx5e_destroy_indirect_tirs(struct mlx5e_priv *priv)
{
	int i;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->indir_tir[i]);

	/* Verify inner tirs resources allocated */
	if (!priv->inner_indir_tir[0].tirn)
		return;

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++)
		mlx5e_destroy_tir(priv->mdev, &priv->inner_indir_tir[i]);
}

void mlx5e_destroy_direct_tirs(struct mlx5e_priv *priv, struct mlx5e_tir *tirs)
{
	int i;

	for (i = 0; i < priv->max_nch; i++)
		mlx5e_destroy_tir(priv->mdev, &tirs[i]);
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

static int mlx5e_setup_tc_mqprio(struct mlx5e_priv *priv,
				 struct tc_mqprio_qopt *mqprio)
{
	struct mlx5e_channels new_channels = {};
	u8 tc = mqprio->num_tc;
	int err = 0;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (tc && tc != MLX5E_MAX_NUM_TC)
		return -EINVAL;

	mutex_lock(&priv->state_lock);

	/* MQPRIO is another toplevel qdisc that can't be attached
	 * simultaneously with the offloaded HTB.
	 */
	if (WARN_ON(priv->htb.maj_id)) {
		err = -EINVAL;
		goto out;
	}

	new_channels.params = priv->channels.params;
	new_channels.params.num_tc = tc ? tc : 1;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		struct mlx5e_params old_params;

		old_params = priv->channels.params;
		priv->channels.params = new_channels.params;
		err = mlx5e_num_channels_changed(priv);
		if (err)
			priv->channels.params = old_params;

		goto out;
	}

	err = mlx5e_safe_switch_channels(priv, &new_channels,
					 mlx5e_num_channels_changed_ctx, NULL);

out:
	priv->max_opened_tc = max_t(u8, priv->max_opened_tc,
				    priv->channels.params.num_tc);
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_setup_tc_htb(struct mlx5e_priv *priv, struct tc_htb_qopt_offload *htb)
{
	int res;

	switch (htb->command) {
	case TC_HTB_CREATE:
		return mlx5e_htb_root_add(priv, htb->parent_classid, htb->classid,
					  htb->extack);
	case TC_HTB_DESTROY:
		return mlx5e_htb_root_del(priv);
	case TC_HTB_LEAF_ALLOC_QUEUE:
		res = mlx5e_htb_leaf_alloc_queue(priv, htb->classid, htb->parent_classid,
						 htb->rate, htb->ceil, htb->extack);
		if (res < 0)
			return res;
		htb->qid = res;
		return 0;
	case TC_HTB_LEAF_TO_INNER:
		return mlx5e_htb_leaf_to_inner(priv, htb->parent_classid, htb->classid,
					       htb->rate, htb->ceil, htb->extack);
	case TC_HTB_LEAF_DEL:
		return mlx5e_htb_leaf_del(priv, htb->classid, &htb->moved_qid, &htb->qid,
					  htb->extack);
	case TC_HTB_LEAF_DEL_LAST:
	case TC_HTB_LEAF_DEL_LAST_FORCE:
		return mlx5e_htb_leaf_del_last(priv, htb->classid,
					       htb->command == TC_HTB_LEAF_DEL_LAST_FORCE,
					       htb->extack);
	case TC_HTB_NODE_MODIFY:
		return mlx5e_htb_node_modify(priv, htb->classid, htb->rate, htb->ceil,
					     htb->extack);
	case TC_HTB_LEAF_QUERY_QUEUE:
		res = mlx5e_get_txq_by_classid(priv, htb->classid);
		if (res < 0)
			return res;
		htb->qid = res;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(mlx5e_block_cb_list);

static int mlx5e_setup_tc(struct net_device *dev, enum tc_setup_type type,
			  void *type_data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int err;

	switch (type) {
	case TC_SETUP_BLOCK: {
		struct flow_block_offload *f = type_data;

		f->unlocked_driver_cb = true;
		return flow_block_cb_setup_simple(type_data,
						  &mlx5e_block_cb_list,
						  mlx5e_setup_tc_block_cb,
						  priv, priv, true);
	}
	case TC_SETUP_QDISC_MQPRIO:
		return mlx5e_setup_tc_mqprio(priv, type_data);
	case TC_SETUP_QDISC_HTB:
		mutex_lock(&priv->state_lock);
		err = mlx5e_setup_tc_htb(priv, type_data);
		mutex_unlock(&priv->state_lock);
		return err;
	default:
		return -EOPNOTSUPP;
	}
}

void mlx5e_fold_sw_stats64(struct mlx5e_priv *priv, struct rtnl_link_stats64 *s)
{
	int i;

	for (i = 0; i < priv->max_nch; i++) {
		struct mlx5e_channel_stats *channel_stats = &priv->channel_stats[i];
		struct mlx5e_rq_stats *xskrq_stats = &channel_stats->xskrq;
		struct mlx5e_rq_stats *rq_stats = &channel_stats->rq;
		int j;

		s->rx_packets   += rq_stats->packets + xskrq_stats->packets;
		s->rx_bytes     += rq_stats->bytes + xskrq_stats->bytes;
		s->multicast    += rq_stats->mcast_packets + xskrq_stats->mcast_packets;

		for (j = 0; j < priv->max_opened_tc; j++) {
			struct mlx5e_sq_stats *sq_stats = &channel_stats->sq[j];

			s->tx_packets    += sq_stats->packets;
			s->tx_bytes      += sq_stats->bytes;
			s->tx_dropped    += sq_stats->dropped;
		}
	}
	if (priv->port_ptp_opened) {
		for (i = 0; i < priv->max_opened_tc; i++) {
			struct mlx5e_sq_stats *sq_stats = &priv->port_ptp_stats.sq[i];

			s->tx_packets    += sq_stats->packets;
			s->tx_bytes      += sq_stats->bytes;
			s->tx_dropped    += sq_stats->dropped;
		}
	}
}

void
mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;

	/* In switchdev mode, monitor counters doesn't monitor
	 * rx/tx stats of 802_3. The update stats mechanism
	 * should keep the 802_3 layout counters updated
	 */
	if (!mlx5e_monitor_counter_supported(priv) ||
	    mlx5e_is_uplink_rep(priv)) {
		/* update HW stats in background for next time */
		mlx5e_queue_update_stats(priv);
	}

	if (mlx5e_is_uplink_rep(priv)) {
		struct mlx5e_vport_stats *vstats = &priv->stats.vport;

		stats->rx_packets = PPORT_802_3_GET(pstats, a_frames_received_ok);
		stats->rx_bytes   = PPORT_802_3_GET(pstats, a_octets_received_ok);
		stats->tx_packets = PPORT_802_3_GET(pstats, a_frames_transmitted_ok);
		stats->tx_bytes   = PPORT_802_3_GET(pstats, a_octets_transmitted_ok);

		/* vport multicast also counts packets that are dropped due to steering
		 * or rx out of buffer
		 */
		stats->multicast = VPORT_COUNTER_GET(vstats, received_eth_multicast.packets);
	} else {
		mlx5e_fold_sw_stats64(priv, stats);
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

#define MLX5E_SET_FEATURE(features, feature, enable)	\
	do {						\
		if (enable)				\
			*features |= feature;		\
		else					\
			*features &= ~feature;		\
	} while (0)

typedef int (*mlx5e_feature_handler)(struct net_device *netdev, bool enable);

static int set_feature_lro(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_channels new_channels = {};
	struct mlx5e_params *cur_params;
	int err = 0;
	bool reset;

	mutex_lock(&priv->state_lock);

	if (enable && priv->xsk.refcnt) {
		netdev_warn(netdev, "LRO is incompatible with AF_XDP (%u XSKs are active)\n",
			    priv->xsk.refcnt);
		err = -EINVAL;
		goto out;
	}

	cur_params = &priv->channels.params;
	if (enable && !MLX5E_GET_PFLAG(cur_params, MLX5E_PFLAG_RX_STRIDING_RQ)) {
		netdev_warn(netdev, "can't set LRO with legacy RQ\n");
		err = -EINVAL;
		goto out;
	}

	reset = test_bit(MLX5E_STATE_OPENED, &priv->state);

	new_channels.params = *cur_params;
	new_channels.params.lro_en = enable;

	if (cur_params->rq_wq_type != MLX5_WQ_TYPE_CYCLIC) {
		if (mlx5e_rx_mpwqe_is_linear_skb(mdev, cur_params, NULL) ==
		    mlx5e_rx_mpwqe_is_linear_skb(mdev, &new_channels.params, NULL))
			reset = false;
	}

	if (!reset) {
		struct mlx5e_params old_params;

		old_params = *cur_params;
		*cur_params = new_channels.params;
		err = mlx5e_modify_tirs_lro(priv);
		if (err)
			*cur_params = old_params;
		goto out;
	}

	err = mlx5e_safe_switch_channels(priv, &new_channels,
					 mlx5e_modify_tirs_lro_ctx, NULL);
out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int set_feature_cvlan_filter(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (enable)
		mlx5e_enable_cvlan_filter(priv);
	else
		mlx5e_disable_cvlan_filter(priv);

	return 0;
}

static int set_feature_hw_tc(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
	if (!enable && mlx5e_tc_num_filters(priv, MLX5_TC_FLAG(NIC_OFFLOAD))) {
		netdev_err(netdev,
			   "Active offloaded tc filters, can't turn hw_tc_offload off\n");
		return -EINVAL;
	}
#endif

	if (!enable && priv->htb.maj_id) {
		netdev_err(netdev, "Active HTB offload, can't turn hw_tc_offload off\n");
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

#ifdef CONFIG_MLX5_EN_ARFS
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
				netdev_features_t *features,
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

	MLX5E_SET_FEATURE(features, feature, enable);
	return 0;
}

int mlx5e_set_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t oper_features = netdev->features;
	int err = 0;

#define MLX5E_HANDLE_FEATURE(feature, handler) \
	mlx5e_handle_feature(netdev, &oper_features, features, feature, handler)

	err |= MLX5E_HANDLE_FEATURE(NETIF_F_LRO, set_feature_lro);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_VLAN_CTAG_FILTER,
				    set_feature_cvlan_filter);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_TC, set_feature_hw_tc);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_RXALL, set_feature_rx_all);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_RXFCS, set_feature_rx_fcs);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_VLAN_CTAG_RX, set_feature_rx_vlan);
#ifdef CONFIG_MLX5_EN_ARFS
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_NTUPLE, set_feature_arfs);
#endif
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_HW_TLS_RX, mlx5e_ktls_set_feature_rx);

	if (err) {
		netdev->features = oper_features;
		return -EINVAL;
	}

	return 0;
}

static netdev_features_t mlx5e_fix_features(struct net_device *netdev,
					    netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params *params;

	mutex_lock(&priv->state_lock);
	params = &priv->channels.params;
	if (!bitmap_empty(priv->fs.vlan.active_svlans, VLAN_N_VID)) {
		/* HW strips the outer C-tag header, this is a problem
		 * for S-tag traffic.
		 */
		features &= ~NETIF_F_HW_VLAN_CTAG_RX;
		if (!params->vlan_strip_disable)
			netdev_warn(netdev, "Dropping C-tag vlan stripping offload due to S-tag vlan\n");
	}

	if (!MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ)) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "Disabling LRO, not supported in legacy RQ\n");
			features &= ~NETIF_F_LRO;
		}
	}

	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		features &= ~NETIF_F_RXHASH;
		if (netdev->features & NETIF_F_RXHASH)
			netdev_warn(netdev, "Disabling rxhash, not supported when CQE compress is active\n");
	}

	mutex_unlock(&priv->state_lock);

	return features;
}

static bool mlx5e_xsk_validate_mtu(struct net_device *netdev,
				   struct mlx5e_channels *chs,
				   struct mlx5e_params *new_params,
				   struct mlx5_core_dev *mdev)
{
	u16 ix;

	for (ix = 0; ix < chs->params.num_channels; ix++) {
		struct xsk_buff_pool *xsk_pool =
			mlx5e_xsk_get_pool(&chs->params, chs->params.xsk, ix);
		struct mlx5e_xsk_param xsk;

		if (!xsk_pool)
			continue;

		mlx5e_build_xsk_param(xsk_pool, &xsk);

		if (!mlx5e_validate_xsk_param(new_params, &xsk, mdev)) {
			u32 hr = mlx5e_get_linear_rq_headroom(new_params, &xsk);
			int max_mtu_frame, max_mtu_page, max_mtu;

			/* Two criteria must be met:
			 * 1. HW MTU + all headrooms <= XSK frame size.
			 * 2. Size of SKBs allocated on XDP_PASS <= PAGE_SIZE.
			 */
			max_mtu_frame = MLX5E_HW2SW_MTU(new_params, xsk.chunk_size - hr);
			max_mtu_page = mlx5e_xdp_max_mtu(new_params, &xsk);
			max_mtu = min(max_mtu_frame, max_mtu_page);

			netdev_err(netdev, "MTU %d is too big for an XSK running on channel %u. Try MTU <= %d\n",
				   new_params->sw_mtu, ix, max_mtu);
			return false;
		}
	}

	return true;
}

int mlx5e_change_mtu(struct net_device *netdev, int new_mtu,
		     mlx5e_fp_preactivate preactivate)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_channels new_channels = {};
	struct mlx5e_params *params;
	int err = 0;
	bool reset;

	mutex_lock(&priv->state_lock);

	params = &priv->channels.params;

	reset = !params->lro_en;
	reset = reset && test_bit(MLX5E_STATE_OPENED, &priv->state);

	new_channels.params = *params;
	new_channels.params.sw_mtu = new_mtu;
	err = mlx5e_validate_params(priv, &new_channels.params);
	if (err)
		goto out;

	if (params->xdp_prog &&
	    !mlx5e_rx_is_linear_skb(&new_channels.params, NULL)) {
		netdev_err(netdev, "MTU(%d) > %d is not allowed while XDP enabled\n",
			   new_mtu, mlx5e_xdp_max_mtu(params, NULL));
		err = -EINVAL;
		goto out;
	}

	if (priv->xsk.refcnt &&
	    !mlx5e_xsk_validate_mtu(netdev, &priv->channels,
				    &new_channels.params, priv->mdev)) {
		err = -EINVAL;
		goto out;
	}

	if (params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
		bool is_linear = mlx5e_rx_mpwqe_is_linear_skb(priv->mdev,
							      &new_channels.params,
							      NULL);
		u8 ppw_old = mlx5e_mpwqe_log_pkts_per_wqe(params, NULL);
		u8 ppw_new = mlx5e_mpwqe_log_pkts_per_wqe(&new_channels.params, NULL);

		/* If XSK is active, XSK RQs are linear. */
		is_linear |= priv->xsk.refcnt;

		/* Always reset in linear mode - hw_mtu is used in data path. */
		reset = reset && (is_linear || (ppw_old != ppw_new));
	}

	if (!reset) {
		unsigned int old_mtu = params->sw_mtu;

		params->sw_mtu = new_mtu;
		if (preactivate) {
			err = preactivate(priv, NULL);
			if (err) {
				params->sw_mtu = old_mtu;
				goto out;
			}
		}
		netdev->mtu = params->sw_mtu;
		goto out;
	}

	err = mlx5e_safe_switch_channels(priv, &new_channels, preactivate, NULL);
	if (err)
		goto out;

	netdev->mtu = new_channels.params.sw_mtu;

out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_change_nic_mtu(struct net_device *netdev, int new_mtu)
{
	return mlx5e_change_mtu(netdev, new_mtu, mlx5e_set_dev_port_mtu_ctx);
}

int mlx5e_hwstamp_set(struct mlx5e_priv *priv, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz) ||
	    (mlx5_clock_get_ptp_index(priv->mdev) == -1))
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* TX HW timestamp */
	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	mutex_lock(&priv->state_lock);
	/* RX HW timestamp */
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		/* Reset CQE compression to Admin default */
		mlx5e_modify_rx_cqe_compression_locked(priv, priv->channels.params.rx_cqe_compress_def);
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
		/* Disable CQE compression */
		if (MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_RX_CQE_COMPRESS))
			netdev_warn(priv->netdev, "Disabling RX cqe compression\n");
		err = mlx5e_modify_rx_cqe_compression_locked(priv, false);
		if (err) {
			netdev_err(priv->netdev, "Failed disabling cqe compression err=%d\n", err);
			mutex_unlock(&priv->state_lock);
			return err;
		}
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		mutex_unlock(&priv->state_lock);
		return -ERANGE;
	}

	memcpy(&priv->tstamp, &config, sizeof(config));
	mutex_unlock(&priv->state_lock);

	/* might need to fix some features */
	netdev_update_features(priv->netdev);

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

int mlx5e_hwstamp_get(struct mlx5e_priv *priv, struct ifreq *ifr)
{
	struct hwtstamp_config *cfg = &priv->tstamp;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, cfg, sizeof(*cfg)) ? -EFAULT : 0;
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
int mlx5e_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
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

int mlx5e_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate,
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
	case MLX5_VPORT_ADMIN_STATE_DOWN:
		return IFLA_VF_LINK_STATE_DISABLE;
	case MLX5_VPORT_ADMIN_STATE_UP:
		return IFLA_VF_LINK_STATE_ENABLE;
	}
	return IFLA_VF_LINK_STATE_AUTO;
}

static int mlx5_ifla_link2vport(u8 ifla_link)
{
	switch (ifla_link) {
	case IFLA_VF_LINK_STATE_DISABLE:
		return MLX5_VPORT_ADMIN_STATE_DOWN;
	case IFLA_VF_LINK_STATE_ENABLE:
		return MLX5_VPORT_ADMIN_STATE_UP;
	}
	return MLX5_VPORT_ADMIN_STATE_AUTO;
}

static int mlx5e_set_vf_link_state(struct net_device *dev, int vf,
				   int link_state)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_set_vport_state(mdev->priv.eswitch, vf + 1,
					    mlx5_ifla_link2vport(link_state));
}

int mlx5e_get_vf_config(struct net_device *dev,
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

int mlx5e_get_vf_stats(struct net_device *dev,
		       int vf, struct ifla_vf_stats *vf_stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_eswitch_get_vport_stats(mdev->priv.eswitch, vf + 1,
					    vf_stats);
}
#endif

static bool mlx5e_tunnel_proto_supported_tx(struct mlx5_core_dev *mdev, u8 proto_type)
{
	switch (proto_type) {
	case IPPROTO_GRE:
		return MLX5_CAP_ETH(mdev, tunnel_stateless_gre);
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		return (MLX5_CAP_ETH(mdev, tunnel_stateless_ip_over_ip) ||
			MLX5_CAP_ETH(mdev, tunnel_stateless_ip_over_ip_tx));
	default:
		return false;
	}
}

static bool mlx5e_gre_tunnel_inner_proto_offload_supported(struct mlx5_core_dev *mdev,
							   struct sk_buff *skb)
{
	switch (skb->inner_protocol) {
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
	case htons(ETH_P_TEB):
		return true;
	case htons(ETH_P_MPLS_UC):
	case htons(ETH_P_MPLS_MC):
		return MLX5_CAP_ETH(mdev, tunnel_stateless_mpls_over_gre);
	}
	return false;
}

static netdev_features_t mlx5e_tunnel_features_check(struct mlx5e_priv *priv,
						     struct sk_buff *skb,
						     netdev_features_t features)
{
	unsigned int offset = 0;
	struct udphdr *udph;
	u8 proto;
	u16 port;

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		proto = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		proto = ipv6_find_hdr(skb, &offset, -1, NULL, NULL);
		break;
	default:
		goto out;
	}

	switch (proto) {
	case IPPROTO_GRE:
		if (mlx5e_gre_tunnel_inner_proto_offload_supported(priv->mdev, skb))
			return features;
		break;
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		if (mlx5e_tunnel_proto_supported_tx(priv->mdev, IPPROTO_IPIP))
			return features;
		break;
	case IPPROTO_UDP:
		udph = udp_hdr(skb);
		port = be16_to_cpu(udph->dest);

		/* Verify if UDP port is being offloaded by HW */
		if (mlx5_vxlan_lookup_port(priv->mdev->vxlan, port))
			return features;

#if IS_ENABLED(CONFIG_GENEVE)
		/* Support Geneve offload for default UDP port */
		if (port == GENEVE_UDP_PORT && mlx5_geneve_tx_allowed(priv->mdev))
			return features;
#endif
	}

out:
	/* Disable CSUM and GSO if the udp dport is not offloaded by HW */
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

netdev_features_t mlx5e_features_check(struct sk_buff *skb,
				       struct net_device *netdev,
				       netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	features = vlan_features_check(skb, features);
	features = vxlan_features_check(skb, features);

	if (mlx5e_ipsec_feature_check(skb, netdev, features))
		return features;

	/* Validate if the tunneled packet is being offloaded by HW */
	if (skb->encapsulation &&
	    (features & NETIF_F_CSUM_MASK || features & NETIF_F_GSO_MASK))
		return mlx5e_tunnel_features_check(priv, skb, features);

	return features;
}

static void mlx5e_tx_timeout_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       tx_timeout_work);
	struct net_device *netdev = priv->netdev;
	int i;

	rtnl_lock();
	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	for (i = 0; i < netdev->real_num_tx_queues; i++) {
		struct netdev_queue *dev_queue =
			netdev_get_tx_queue(netdev, i);
		struct mlx5e_txqsq *sq = priv->txq2sq[i];

		if (!netif_xmit_stopped(dev_queue))
			continue;

		if (mlx5e_reporter_tx_timeout(sq))
		/* break if tried to reopened channels */
			break;
	}

unlock:
	mutex_unlock(&priv->state_lock);
	rtnl_unlock();
}

static void mlx5e_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	netdev_err(dev, "TX timeout detected\n");
	queue_work(priv->wq, &priv->tx_timeout_work);
}

static int mlx5e_xdp_allowed(struct mlx5e_priv *priv, struct bpf_prog *prog)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5e_channels new_channels = {};

	if (priv->channels.params.lro_en) {
		netdev_warn(netdev, "can't set XDP while LRO is on, disable LRO first\n");
		return -EINVAL;
	}

	if (mlx5_fpga_is_ipsec_device(priv->mdev)) {
		netdev_warn(netdev,
			    "XDP is not available on Innova cards with IPsec support\n");
		return -EINVAL;
	}

	new_channels.params = priv->channels.params;
	new_channels.params.xdp_prog = prog;

	/* No XSK params: AF_XDP can't be enabled yet at the point of setting
	 * the XDP program.
	 */
	if (!mlx5e_rx_is_linear_skb(&new_channels.params, NULL)) {
		netdev_warn(netdev, "XDP is not allowed with MTU(%d) > %d\n",
			    new_channels.params.sw_mtu,
			    mlx5e_xdp_max_mtu(&new_channels.params, NULL));
		return -EINVAL;
	}

	return 0;
}

static void mlx5e_rq_replace_xdp_prog(struct mlx5e_rq *rq, struct bpf_prog *prog)
{
	struct bpf_prog *old_prog;

	old_prog = rcu_replace_pointer(rq->xdp_prog, prog,
				       lockdep_is_held(&rq->priv->state_lock));
	if (old_prog)
		bpf_prog_put(old_prog);
}

static int mlx5e_xdp_set(struct net_device *netdev, struct bpf_prog *prog)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct bpf_prog *old_prog;
	bool reset, was_opened;
	int err = 0;
	int i;

	mutex_lock(&priv->state_lock);

	if (prog) {
		err = mlx5e_xdp_allowed(priv, prog);
		if (err)
			goto unlock;
	}

	was_opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	/* no need for full reset when exchanging programs */
	reset = (!priv->channels.params.xdp_prog || !prog);

	if (was_opened && !reset)
		/* num_channels is invariant here, so we can take the
		 * batched reference right upfront.
		 */
		bpf_prog_add(prog, priv->channels.num);

	if (was_opened && reset) {
		struct mlx5e_channels new_channels = {};

		new_channels.params = priv->channels.params;
		new_channels.params.xdp_prog = prog;
		mlx5e_set_rq_type(priv->mdev, &new_channels.params);
		old_prog = priv->channels.params.xdp_prog;

		err = mlx5e_safe_switch_channels(priv, &new_channels, NULL, NULL);
		if (err)
			goto unlock;
	} else {
		/* exchange programs, extra prog reference we got from caller
		 * as long as we don't fail from this point onwards.
		 */
		old_prog = xchg(&priv->channels.params.xdp_prog, prog);
	}

	if (old_prog)
		bpf_prog_put(old_prog);

	if (!was_opened && reset) /* change RQ type according to priv->xdp_prog */
		mlx5e_set_rq_type(priv->mdev, &priv->channels.params);

	if (!was_opened || reset)
		goto unlock;

	/* exchanging programs w/o reset, we update ref counts on behalf
	 * of the channels RQs here.
	 */
	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		mlx5e_rq_replace_xdp_prog(&c->rq, prog);
		if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state)) {
			bpf_prog_inc(prog);
			mlx5e_rq_replace_xdp_prog(&c->xskrq, prog);
		}
	}

unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return mlx5e_xdp_set(dev, xdp->prog);
	case XDP_SETUP_XSK_POOL:
		return mlx5e_xsk_setup_pool(dev, xdp->xsk.pool,
					    xdp->xsk.queue_id);
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_MLX5_ESWITCH
static int mlx5e_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				struct net_device *dev, u32 filter_mask,
				int nlflags)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 mode, setting;
	int err;

	err = mlx5_eswitch_get_vepa(mdev->priv.eswitch, &setting);
	if (err)
		return err;
	mode = setting ? BRIDGE_MODE_VEPA : BRIDGE_MODE_VEB;
	return ndo_dflt_bridge_getlink(skb, pid, seq, dev,
				       mode,
				       0, 0, nlflags, filter_mask, NULL);
}

static int mlx5e_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
				u16 flags, struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct nlattr *attr, *br_spec;
	u16 mode = BRIDGE_MODE_UNDEF;
	u8 setting;
	int rem;

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!br_spec)
		return -EINVAL;

	nla_for_each_nested(attr, br_spec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;

		if (nla_len(attr) < sizeof(mode))
			return -EINVAL;

		mode = nla_get_u16(attr);
		if (mode > BRIDGE_MODE_VEPA)
			return -EINVAL;

		break;
	}

	if (mode == BRIDGE_MODE_UNDEF)
		return -EINVAL;

	setting = (mode == BRIDGE_MODE_VEPA) ?  1 : 0;
	return mlx5_eswitch_set_vepa(mdev->priv.eswitch, setting);
}
#endif

const struct net_device_ops mlx5e_netdev_ops = {
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
	.ndo_fix_features        = mlx5e_fix_features,
	.ndo_change_mtu          = mlx5e_change_nic_mtu,
	.ndo_do_ioctl            = mlx5e_ioctl,
	.ndo_set_tx_maxrate      = mlx5e_set_tx_maxrate,
	.ndo_features_check      = mlx5e_features_check,
	.ndo_tx_timeout          = mlx5e_tx_timeout,
	.ndo_bpf		 = mlx5e_xdp,
	.ndo_xdp_xmit            = mlx5e_xdp_xmit,
	.ndo_xsk_wakeup          = mlx5e_xsk_wakeup,
#ifdef CONFIG_MLX5_EN_ARFS
	.ndo_rx_flow_steer	 = mlx5e_rx_flow_steer,
#endif
#ifdef CONFIG_MLX5_ESWITCH
	.ndo_bridge_setlink      = mlx5e_bridge_setlink,
	.ndo_bridge_getlink      = mlx5e_bridge_getlink,

	/* SRIOV E-Switch NDOs */
	.ndo_set_vf_mac          = mlx5e_set_vf_mac,
	.ndo_set_vf_vlan         = mlx5e_set_vf_vlan,
	.ndo_set_vf_spoofchk     = mlx5e_set_vf_spoofchk,
	.ndo_set_vf_trust        = mlx5e_set_vf_trust,
	.ndo_set_vf_rate         = mlx5e_set_vf_rate,
	.ndo_get_vf_config       = mlx5e_get_vf_config,
	.ndo_set_vf_link_state   = mlx5e_set_vf_link_state,
	.ndo_get_vf_stats        = mlx5e_get_vf_stats,
#endif
	.ndo_get_devlink_port    = mlx5e_get_devlink_port,
};

void mlx5e_build_default_indir_rqt(u32 *indirection_rqt, int len,
				   int num_channels)
{
	int i;

	for (i = 0; i < len; i++)
		indirection_rqt[i] = i % num_channels;
}

static bool slow_pci_heuristic(struct mlx5_core_dev *mdev)
{
	u32 link_speed = 0;
	u32 pci_bw = 0;

	mlx5e_port_max_linkspeed(mdev, &link_speed);
	pci_bw = pcie_bandwidth_available(mdev->pdev, NULL, NULL, NULL);
	mlx5_core_dbg_once(mdev, "Max link speed = %d, PCI BW = %d\n",
			   link_speed, pci_bw);

#define MLX5E_SLOW_PCI_RATIO (2)

	return link_speed && pci_bw &&
		link_speed > MLX5E_SLOW_PCI_RATIO * pci_bw;
}

static struct dim_cq_moder mlx5e_get_def_tx_moderation(u8 cq_period_mode)
{
	struct dim_cq_moder moder;

	moder.cq_period_mode = cq_period_mode;
	moder.pkts = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS;
	moder.usec = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC;
	if (cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE)
		moder.usec = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC_FROM_CQE;

	return moder;
}

static struct dim_cq_moder mlx5e_get_def_rx_moderation(u8 cq_period_mode)
{
	struct dim_cq_moder moder;

	moder.cq_period_mode = cq_period_mode;
	moder.pkts = MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS;
	moder.usec = MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC;
	if (cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE)
		moder.usec = MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE;

	return moder;
}

static u8 mlx5_to_net_dim_cq_period_mode(u8 cq_period_mode)
{
	return cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE ?
		DIM_CQ_PERIOD_MODE_START_FROM_CQE :
		DIM_CQ_PERIOD_MODE_START_FROM_EQE;
}

void mlx5e_reset_tx_moderation(struct mlx5e_params *params, u8 cq_period_mode)
{
	if (params->tx_dim_enabled) {
		u8 dim_period_mode = mlx5_to_net_dim_cq_period_mode(cq_period_mode);

		params->tx_cq_moderation = net_dim_get_def_tx_moderation(dim_period_mode);
	} else {
		params->tx_cq_moderation = mlx5e_get_def_tx_moderation(cq_period_mode);
	}
}

void mlx5e_reset_rx_moderation(struct mlx5e_params *params, u8 cq_period_mode)
{
	if (params->rx_dim_enabled) {
		u8 dim_period_mode = mlx5_to_net_dim_cq_period_mode(cq_period_mode);

		params->rx_cq_moderation = net_dim_get_def_rx_moderation(dim_period_mode);
	} else {
		params->rx_cq_moderation = mlx5e_get_def_rx_moderation(cq_period_mode);
	}
}

void mlx5e_set_tx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode)
{
	mlx5e_reset_tx_moderation(params, cq_period_mode);
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_TX_CQE_BASED_MODER,
			params->tx_cq_moderation.cq_period_mode ==
				MLX5_CQ_PERIOD_MODE_START_FROM_CQE);
}

void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode)
{
	mlx5e_reset_rx_moderation(params, cq_period_mode);
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_BASED_MODER,
			params->rx_cq_moderation.cq_period_mode ==
				MLX5_CQ_PERIOD_MODE_START_FROM_CQE);
}

static u32 mlx5e_choose_lro_timeout(struct mlx5_core_dev *mdev, u32 wanted_timeout)
{
	int i;

	/* The supported periods are organized in ascending order */
	for (i = 0; i < MLX5E_LRO_TIMEOUT_ARR_SIZE - 1; i++)
		if (MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]) >= wanted_timeout)
			break;

	return MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]);
}

void mlx5e_build_rq_params(struct mlx5_core_dev *mdev,
			   struct mlx5e_params *params)
{
	/* Prefer Striding RQ, unless any of the following holds:
	 * - Striding RQ configuration is not possible/supported.
	 * - Slow PCI heuristic.
	 * - Legacy RQ would use linear SKB while Striding RQ would use non-linear.
	 *
	 * No XSK params: checking the availability of striding RQ in general.
	 */
	if (!slow_pci_heuristic(mdev) &&
	    mlx5e_striding_rq_possible(mdev, params) &&
	    (mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL) ||
	     !mlx5e_rx_is_linear_skb(params, NULL)))
		MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ, true);
	mlx5e_set_rq_type(mdev, params);
	mlx5e_init_rq_type_params(mdev, params);
}

void mlx5e_build_rss_params(struct mlx5e_rss_params *rss_params,
			    u16 num_channels)
{
	enum mlx5e_traffic_types tt;

	rss_params->hfunc = ETH_RSS_HASH_TOP;
	netdev_rss_key_fill(rss_params->toeplitz_hash_key,
			    sizeof(rss_params->toeplitz_hash_key));
	mlx5e_build_default_indir_rqt(rss_params->indirection_rqt,
				      MLX5E_INDIR_RQT_SIZE, num_channels);
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		rss_params->rx_hash_fields[tt] =
			tirc_default_config[tt].rx_hash_fields;
}

void mlx5e_build_nic_params(struct mlx5e_priv *priv, struct mlx5e_xsk *xsk, u16 mtu)
{
	struct mlx5e_rss_params *rss_params = &priv->rss_params;
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 rx_cq_period_mode;

	priv->max_nch = mlx5e_calc_max_nch(priv, priv->profile);

	params->sw_mtu = mtu;
	params->hard_mtu = MLX5E_ETH_HARD_MTU;
	params->num_channels = min_t(unsigned int, MLX5E_MAX_NUM_CHANNELS / 2,
				     priv->max_nch);
	params->num_tc       = 1;

	/* Set an initial non-zero value, so that mlx5e_select_queue won't
	 * divide by zero if called before first activating channels.
	 */
	priv->num_tc_x_num_ch = params->num_channels * params->num_tc;

	/* SQ */
	params->log_sq_size = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE,
			MLX5_CAP_ETH(mdev, enhanced_multi_pkt_send_wqe));

	/* XDP SQ */
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_XDP_TX_MPWQE,
			MLX5_CAP_ETH(mdev, enhanced_multi_pkt_send_wqe));

	/* set CQE compression */
	params->rx_cqe_compress_def = false;
	if (MLX5_CAP_GEN(mdev, cqe_compression) &&
	    MLX5_CAP_GEN(mdev, vport_group_manager))
		params->rx_cqe_compress_def = slow_pci_heuristic(mdev);

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS, params->rx_cqe_compress_def);
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE, false);

	/* RQ */
	mlx5e_build_rq_params(mdev, params);

	/* HW LRO */
	if (MLX5_CAP_ETH(mdev, lro_cap) &&
	    params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
		/* No XSK params: checking the availability of striding RQ in general. */
		if (!mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL))
			params->lro_en = !slow_pci_heuristic(mdev);
	}
	params->lro_timeout = mlx5e_choose_lro_timeout(mdev, MLX5E_DEFAULT_LRO_TIMEOUT);

	/* CQ moderation params */
	rx_cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
			MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
			MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
	params->rx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	params->tx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(params, rx_cq_period_mode);
	mlx5e_set_tx_cq_mode_params(params, MLX5_CQ_PERIOD_MODE_START_FROM_EQE);

	/* TX inline */
	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);

	/* RSS */
	mlx5e_build_rss_params(rss_params, params->num_channels);
	params->tunneled_offload_en =
		mlx5e_tunnel_inner_ft_supported(mdev);

	/* AF_XDP */
	params->xsk = xsk;

	/* Do not update netdev->features directly in here
	 * on mlx5e_attach_netdev() we will call mlx5e_update_features()
	 * To update netdev->features please modify mlx5e_fix_features()
	 */
}

static void mlx5e_set_netdev_dev_addr(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5_query_mac_address(priv->mdev, netdev->dev_addr);
	if (is_zero_ether_addr(netdev->dev_addr) &&
	    !MLX5_CAP_GEN(priv->mdev, vport_group_manager)) {
		eth_hw_addr_random(netdev);
		mlx5_core_info(priv->mdev, "Assigned random MAC address %pM\n", netdev->dev_addr);
	}
}

static int mlx5e_vxlan_set_port(struct net_device *netdev, unsigned int table,
				unsigned int entry, struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5_vxlan_add_port(priv->mdev->vxlan, ntohs(ti->port));
}

static int mlx5e_vxlan_unset_port(struct net_device *netdev, unsigned int table,
				  unsigned int entry, struct udp_tunnel_info *ti)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5_vxlan_del_port(priv->mdev->vxlan, ntohs(ti->port));
}

void mlx5e_vxlan_set_netdev_info(struct mlx5e_priv *priv)
{
	if (!mlx5_vxlan_allowed(priv->mdev->vxlan))
		return;

	priv->nic_info.set_port = mlx5e_vxlan_set_port;
	priv->nic_info.unset_port = mlx5e_vxlan_unset_port;
	priv->nic_info.flags = UDP_TUNNEL_NIC_INFO_MAY_SLEEP |
				UDP_TUNNEL_NIC_INFO_STATIC_IANA_VXLAN;
	priv->nic_info.tables[0].tunnel_types = UDP_TUNNEL_TYPE_VXLAN;
	/* Don't count the space hard-coded to the IANA port */
	priv->nic_info.tables[0].n_entries =
		mlx5_vxlan_max_udp_ports(priv->mdev) - 1;

	priv->netdev->udp_tunnel_nic_info = &priv->nic_info;
}

static bool mlx5e_tunnel_any_tx_proto_supported(struct mlx5_core_dev *mdev)
{
	int tt;

	for (tt = 0; tt < MLX5E_NUM_TUNNEL_TT; tt++) {
		if (mlx5e_tunnel_proto_supported_tx(mdev, mlx5e_get_proto_by_tunnel_type(tt)))
			return true;
	}
	return (mlx5_vxlan_allowed(mdev->vxlan) || mlx5_geneve_tx_allowed(mdev));
}

static void mlx5e_build_nic_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool fcs_supported;
	bool fcs_enabled;

	SET_NETDEV_DEV(netdev, mdev->device);

	netdev->netdev_ops = &mlx5e_netdev_ops;

	mlx5e_dcbnl_build_netdev(netdev);

	netdev->watchdog_timeo    = 15 * HZ;

	netdev->ethtool_ops	  = &mlx5e_ethtool_ops;

	netdev->vlan_features    |= NETIF_F_SG;
	netdev->vlan_features    |= NETIF_F_HW_CSUM;
	netdev->vlan_features    |= NETIF_F_GRO;
	netdev->vlan_features    |= NETIF_F_TSO;
	netdev->vlan_features    |= NETIF_F_TSO6;
	netdev->vlan_features    |= NETIF_F_RXCSUM;
	netdev->vlan_features    |= NETIF_F_RXHASH;

	netdev->mpls_features    |= NETIF_F_SG;
	netdev->mpls_features    |= NETIF_F_HW_CSUM;
	netdev->mpls_features    |= NETIF_F_TSO;
	netdev->mpls_features    |= NETIF_F_TSO6;

	netdev->hw_enc_features  |= NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_enc_features  |= NETIF_F_HW_VLAN_CTAG_RX;

	if (!!MLX5_CAP_ETH(mdev, lro_cap) &&
	    mlx5e_check_fragmented_striding_rq_cap(mdev))
		netdev->vlan_features    |= NETIF_F_LRO;

	netdev->hw_features       = netdev->vlan_features;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_RX;
	netdev->hw_features      |= NETIF_F_HW_VLAN_CTAG_FILTER;
	netdev->hw_features      |= NETIF_F_HW_VLAN_STAG_TX;

	if (mlx5e_tunnel_any_tx_proto_supported(mdev)) {
		netdev->hw_enc_features |= NETIF_F_HW_CSUM;
		netdev->hw_enc_features |= NETIF_F_TSO;
		netdev->hw_enc_features |= NETIF_F_TSO6;
		netdev->hw_enc_features |= NETIF_F_GSO_PARTIAL;
	}

	if (mlx5_vxlan_allowed(mdev->vxlan) || mlx5_geneve_tx_allowed(mdev)) {
		netdev->hw_features     |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
		netdev->hw_enc_features |= NETIF_F_GSO_UDP_TUNNEL |
					   NETIF_F_GSO_UDP_TUNNEL_CSUM;
		netdev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM;
		netdev->vlan_features |= NETIF_F_GSO_UDP_TUNNEL |
					 NETIF_F_GSO_UDP_TUNNEL_CSUM;
	}

	if (mlx5e_tunnel_proto_supported_tx(mdev, IPPROTO_GRE)) {
		netdev->hw_features     |= NETIF_F_GSO_GRE |
					   NETIF_F_GSO_GRE_CSUM;
		netdev->hw_enc_features |= NETIF_F_GSO_GRE |
					   NETIF_F_GSO_GRE_CSUM;
		netdev->gso_partial_features |= NETIF_F_GSO_GRE |
						NETIF_F_GSO_GRE_CSUM;
	}

	if (mlx5e_tunnel_proto_supported_tx(mdev, IPPROTO_IPIP)) {
		netdev->hw_features |= NETIF_F_GSO_IPXIP4 |
				       NETIF_F_GSO_IPXIP6;
		netdev->hw_enc_features |= NETIF_F_GSO_IPXIP4 |
					   NETIF_F_GSO_IPXIP6;
		netdev->gso_partial_features |= NETIF_F_GSO_IPXIP4 |
						NETIF_F_GSO_IPXIP6;
	}

	netdev->hw_features	                 |= NETIF_F_GSO_PARTIAL;
	netdev->gso_partial_features             |= NETIF_F_GSO_UDP_L4;
	netdev->hw_features                      |= NETIF_F_GSO_UDP_L4;
	netdev->features                         |= NETIF_F_GSO_UDP_L4;

	mlx5_query_port_fcs(mdev, &fcs_supported, &fcs_enabled);

	if (fcs_supported)
		netdev->hw_features |= NETIF_F_RXALL;

	if (MLX5_CAP_ETH(mdev, scatter_fcs))
		netdev->hw_features |= NETIF_F_RXFCS;

	netdev->features          = netdev->hw_features;

	/* Defaults */
	if (fcs_enabled)
		netdev->features  &= ~NETIF_F_RXALL;
	netdev->features  &= ~NETIF_F_LRO;
	netdev->features  &= ~NETIF_F_RXFCS;

#define FT_CAP(f) MLX5_CAP_FLOWTABLE(mdev, flow_table_properties_nic_receive.f)
	if (FT_CAP(flow_modify_en) &&
	    FT_CAP(modify_root) &&
	    FT_CAP(identified_miss_table_mode) &&
	    FT_CAP(flow_table_modify)) {
#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
		netdev->hw_features      |= NETIF_F_HW_TC;
#endif
#ifdef CONFIG_MLX5_EN_ARFS
		netdev->hw_features	 |= NETIF_F_NTUPLE;
#endif
	}
	if (mlx5_qos_is_supported(mdev))
		netdev->features |= NETIF_F_HW_TC;

	netdev->features         |= NETIF_F_HIGHDMA;
	netdev->features         |= NETIF_F_HW_VLAN_STAG_FILTER;

	netdev->priv_flags       |= IFF_UNICAST_FLT;

	mlx5e_set_netdev_dev_addr(netdev);
	mlx5e_ipsec_build_netdev(priv);
	mlx5e_tls_build_netdev(priv);
}

void mlx5e_create_q_counters(struct mlx5e_priv *priv)
{
	u32 out[MLX5_ST_SZ_DW(alloc_q_counter_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_q_counter_in)] = {};
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	MLX5_SET(alloc_q_counter_in, in, opcode, MLX5_CMD_OP_ALLOC_Q_COUNTER);
	err = mlx5_cmd_exec_inout(mdev, alloc_q_counter, in, out);
	if (!err)
		priv->q_counter =
			MLX5_GET(alloc_q_counter_out, out, counter_set_id);

	err = mlx5_cmd_exec_inout(mdev, alloc_q_counter, in, out);
	if (!err)
		priv->drop_rq_q_counter =
			MLX5_GET(alloc_q_counter_out, out, counter_set_id);
}

void mlx5e_destroy_q_counters(struct mlx5e_priv *priv)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_q_counter_in)] = {};

	MLX5_SET(dealloc_q_counter_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_Q_COUNTER);
	if (priv->q_counter) {
		MLX5_SET(dealloc_q_counter_in, in, counter_set_id,
			 priv->q_counter);
		mlx5_cmd_exec_in(priv->mdev, dealloc_q_counter, in);
	}

	if (priv->drop_rq_q_counter) {
		MLX5_SET(dealloc_q_counter_in, in, counter_set_id,
			 priv->drop_rq_q_counter);
		mlx5_cmd_exec_in(priv->mdev, dealloc_q_counter, in);
	}
}

static int mlx5e_nic_init(struct mlx5_core_dev *mdev,
			  struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	mlx5e_build_nic_params(priv, &priv->xsk, netdev->mtu);
	mlx5e_vxlan_set_netdev_info(priv);

	mlx5e_timestamp_init(priv);

	err = mlx5e_ipsec_init(priv);
	if (err)
		mlx5_core_err(mdev, "IPSec initialization failed, %d\n", err);

	err = mlx5e_tls_init(priv);
	if (err)
		mlx5_core_err(mdev, "TLS initialization failed, %d\n", err);

	err = mlx5e_devlink_port_register(priv);
	if (err)
		mlx5_core_err(mdev, "mlx5e_devlink_port_register failed, %d\n", err);

	mlx5e_health_create_reporters(priv);

	return 0;
}

static void mlx5e_nic_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_health_destroy_reporters(priv);
	mlx5e_devlink_port_unregister(priv);
	mlx5e_tls_cleanup(priv);
	mlx5e_ipsec_cleanup(priv);
}

static int mlx5e_init_nic_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	mlx5e_create_q_counters(priv);

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_destroy_q_counters;
	}

	err = mlx5e_create_indirect_rqt(priv);
	if (err)
		goto err_close_drop_rq;

	err = mlx5e_create_direct_rqts(priv, priv->direct_tir);
	if (err)
		goto err_destroy_indirect_rqts;

	err = mlx5e_create_indirect_tirs(priv, true);
	if (err)
		goto err_destroy_direct_rqts;

	err = mlx5e_create_direct_tirs(priv, priv->direct_tir);
	if (err)
		goto err_destroy_indirect_tirs;

	err = mlx5e_create_direct_rqts(priv, priv->xsk_tir);
	if (unlikely(err))
		goto err_destroy_direct_tirs;

	err = mlx5e_create_direct_tirs(priv, priv->xsk_tir);
	if (unlikely(err))
		goto err_destroy_xsk_rqts;

	err = mlx5e_create_flow_steering(priv);
	if (err) {
		mlx5_core_warn(mdev, "create flow steering failed, %d\n", err);
		goto err_destroy_xsk_tirs;
	}

	err = mlx5e_tc_nic_init(priv);
	if (err)
		goto err_destroy_flow_steering;

	err = mlx5e_accel_init_rx(priv);
	if (err)
		goto err_tc_nic_cleanup;

#ifdef CONFIG_MLX5_EN_ARFS
	priv->netdev->rx_cpu_rmap =  mlx5_eq_table_get_rmap(priv->mdev);
#endif

	return 0;

err_tc_nic_cleanup:
	mlx5e_tc_nic_cleanup(priv);
err_destroy_flow_steering:
	mlx5e_destroy_flow_steering(priv);
err_destroy_xsk_tirs:
	mlx5e_destroy_direct_tirs(priv, priv->xsk_tir);
err_destroy_xsk_rqts:
	mlx5e_destroy_direct_rqts(priv, priv->xsk_tir);
err_destroy_direct_tirs:
	mlx5e_destroy_direct_tirs(priv, priv->direct_tir);
err_destroy_indirect_tirs:
	mlx5e_destroy_indirect_tirs(priv);
err_destroy_direct_rqts:
	mlx5e_destroy_direct_rqts(priv, priv->direct_tir);
err_destroy_indirect_rqts:
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
err_close_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);
err_destroy_q_counters:
	mlx5e_destroy_q_counters(priv);
	return err;
}

static void mlx5e_cleanup_nic_rx(struct mlx5e_priv *priv)
{
	mlx5e_accel_cleanup_rx(priv);
	mlx5e_tc_nic_cleanup(priv);
	mlx5e_destroy_flow_steering(priv);
	mlx5e_destroy_direct_tirs(priv, priv->xsk_tir);
	mlx5e_destroy_direct_rqts(priv, priv->xsk_tir);
	mlx5e_destroy_direct_tirs(priv, priv->direct_tir);
	mlx5e_destroy_indirect_tirs(priv);
	mlx5e_destroy_direct_rqts(priv, priv->direct_tir);
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
	mlx5e_close_drop_rq(&priv->drop_rq);
	mlx5e_destroy_q_counters(priv);
}

static int mlx5e_init_nic_tx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tises failed, %d\n", err);
		return err;
	}

	mlx5e_dcbnl_initialize(priv);
	return 0;
}

static void mlx5e_nic_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;

	mlx5e_init_l2_addr(priv);

	/* Marking the link as currently not needed by the Driver */
	if (!netif_running(netdev))
		mlx5e_modify_admin_state(mdev, MLX5_PORT_DOWN);

	mlx5e_set_netdev_mtu_boundaries(priv);
	mlx5e_set_dev_port_mtu(priv);

	mlx5_lag_add(mdev, netdev);

	mlx5e_enable_async_events(priv);
	mlx5e_enable_blocking_events(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_init(priv);

	mlx5e_hv_vhca_stats_create(priv);
	if (netdev->reg_state != NETREG_REGISTERED)
		return;
	mlx5e_dcbnl_init_app(priv);

	queue_work(priv->wq, &priv->set_rx_mode_work);

	rtnl_lock();
	if (netif_running(netdev))
		mlx5e_open(netdev);
	udp_tunnel_nic_reset_ntf(priv->netdev);
	netif_device_attach(netdev);
	rtnl_unlock();
}

static void mlx5e_nic_disable(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	if (priv->netdev->reg_state == NETREG_REGISTERED)
		mlx5e_dcbnl_delete_app(priv);

	rtnl_lock();
	if (netif_running(priv->netdev))
		mlx5e_close(priv->netdev);
	netif_device_detach(priv->netdev);
	rtnl_unlock();

	queue_work(priv->wq, &priv->set_rx_mode_work);

	mlx5e_hv_vhca_stats_destroy(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_cleanup(priv);

	mlx5e_disable_blocking_events(priv);
	if (priv->en_trap) {
		mlx5e_deactivate_trap(priv);
		mlx5e_close_trap(priv->en_trap);
		priv->en_trap = NULL;
	}
	mlx5e_disable_async_events(priv);
	mlx5_lag_remove(mdev);
	mlx5_vxlan_reset_to_default(mdev->vxlan);
}

int mlx5e_update_nic_rx(struct mlx5e_priv *priv)
{
	return mlx5e_refresh_tirs(priv, false, false);
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
	.update_rx	   = mlx5e_update_nic_rx,
	.update_stats	   = mlx5e_stats_update_ndo_stats,
	.update_carrier	   = mlx5e_update_carrier,
	.rx_handlers       = &mlx5e_rx_handlers_nic,
	.max_tc		   = MLX5E_MAX_NUM_TC,
	.rq_groups	   = MLX5E_NUM_RQ_GROUPS(XSK),
	.stats_grps	   = mlx5e_nic_stats_grps,
	.stats_grps_num	   = mlx5e_nic_stats_grps_num,
};

/* mlx5e generic netdev management API (move to en_common.c) */
int mlx5e_priv_init(struct mlx5e_priv *priv,
		    struct net_device *netdev,
		    struct mlx5_core_dev *mdev)
{
	/* priv init */
	priv->mdev        = mdev;
	priv->netdev      = netdev;
	priv->msglevel    = MLX5E_MSG_LEVEL;
	priv->max_opened_tc = 1;

	if (!alloc_cpumask_var(&priv->scratchpad.cpumask, GFP_KERNEL))
		return -ENOMEM;

	mutex_init(&priv->state_lock);
	hash_init(priv->htb.qos_tc2node);
	INIT_WORK(&priv->update_carrier_work, mlx5e_update_carrier_work);
	INIT_WORK(&priv->set_rx_mode_work, mlx5e_set_rx_mode_work);
	INIT_WORK(&priv->tx_timeout_work, mlx5e_tx_timeout_work);
	INIT_WORK(&priv->update_stats_work, mlx5e_update_stats_work);

	priv->wq = create_singlethread_workqueue("mlx5e");
	if (!priv->wq)
		goto err_free_cpumask;

	return 0;

err_free_cpumask:
	free_cpumask_var(priv->scratchpad.cpumask);

	return -ENOMEM;
}

void mlx5e_priv_cleanup(struct mlx5e_priv *priv)
{
	int i;

	/* bail if change profile failed and also rollback failed */
	if (!priv->mdev)
		return;

	destroy_workqueue(priv->wq);
	free_cpumask_var(priv->scratchpad.cpumask);

	for (i = 0; i < priv->htb.max_qos_sqs; i++)
		kfree(priv->htb.qos_sq_stats[i]);
	kvfree(priv->htb.qos_sq_stats);

	memset(priv, 0, sizeof(*priv));
}

struct net_device *
mlx5e_create_netdev(struct mlx5_core_dev *mdev, unsigned int txqs, unsigned int rxqs)
{
	struct net_device *netdev;
	int err;

	netdev = alloc_etherdev_mqs(sizeof(struct mlx5e_priv), txqs, rxqs);
	if (!netdev) {
		mlx5_core_err(mdev, "alloc_etherdev_mqs() failed\n");
		return NULL;
	}

	err = mlx5e_priv_init(netdev_priv(netdev), netdev, mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_priv_init failed, err=%d\n", err);
		goto err_free_netdev;
	}

	netif_carrier_off(netdev);
	dev_net_set(netdev, mlx5_core_net(mdev));

	return netdev;

err_free_netdev:
	free_netdev(netdev);

	return NULL;
}

static void mlx5e_update_features(struct net_device *netdev)
{
	if (netdev->reg_state != NETREG_REGISTERED)
		return; /* features will be updated on netdev registration */

	rtnl_lock();
	netdev_update_features(netdev);
	rtnl_unlock();
}

int mlx5e_attach_netdev(struct mlx5e_priv *priv)
{
	const bool take_rtnl = priv->netdev->reg_state == NETREG_REGISTERED;
	const struct mlx5e_profile *profile = priv->profile;
	int max_nch;
	int err;

	clear_bit(MLX5E_STATE_DESTROYING, &priv->state);

	/* max number of channels may have changed */
	max_nch = mlx5e_get_max_num_channels(priv->mdev);
	if (priv->channels.params.num_channels > max_nch) {
		mlx5_core_warn(priv->mdev, "MLX5E: Reducing number of channels to %d\n", max_nch);
		/* Reducing the number of channels - RXFH has to be reset, and
		 * mlx5e_num_channels_changed below will build the RQT.
		 */
		priv->netdev->priv_flags &= ~IFF_RXFH_CONFIGURED;
		priv->channels.params.num_channels = max_nch;
	}
	/* 1. Set the real number of queues in the kernel the first time.
	 * 2. Set our default XPS cpumask.
	 * 3. Build the RQT.
	 *
	 * rtnl_lock is required by netif_set_real_num_*_queues in case the
	 * netdev has been registered by this point (if this function was called
	 * in the reload or resume flow).
	 */
	if (take_rtnl)
		rtnl_lock();
	err = mlx5e_num_channels_changed(priv);
	if (take_rtnl)
		rtnl_unlock();
	if (err)
		goto out;

	err = profile->init_tx(priv);
	if (err)
		goto out;

	err = profile->init_rx(priv);
	if (err)
		goto err_cleanup_tx;

	if (profile->enable)
		profile->enable(priv);

	mlx5e_update_features(priv->netdev);

	return 0;

err_cleanup_tx:
	profile->cleanup_tx(priv);

out:
	set_bit(MLX5E_STATE_DESTROYING, &priv->state);
	cancel_work_sync(&priv->update_stats_work);
	return err;
}

void mlx5e_detach_netdev(struct mlx5e_priv *priv)
{
	const struct mlx5e_profile *profile = priv->profile;

	set_bit(MLX5E_STATE_DESTROYING, &priv->state);

	if (profile->disable)
		profile->disable(priv);
	flush_workqueue(priv->wq);

	profile->cleanup_rx(priv);
	profile->cleanup_tx(priv);
	cancel_work_sync(&priv->update_stats_work);
}

static int
mlx5e_netdev_attach_profile(struct net_device *netdev, struct mlx5_core_dev *mdev,
			    const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	err = mlx5e_priv_init(priv, netdev, mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_priv_init failed, err=%d\n", err);
		return err;
	}
	netif_carrier_off(netdev);
	priv->profile = new_profile;
	priv->ppriv = new_ppriv;
	err = new_profile->init(priv->mdev, priv->netdev);
	if (err)
		goto priv_cleanup;
	err = mlx5e_attach_netdev(priv);
	if (err)
		goto profile_cleanup;
	return err;

profile_cleanup:
	new_profile->cleanup(priv);
priv_cleanup:
	mlx5e_priv_cleanup(priv);
	return err;
}

int mlx5e_netdev_change_profile(struct mlx5e_priv *priv,
				const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	unsigned int new_max_nch = mlx5e_calc_max_nch(priv, new_profile);
	const struct mlx5e_profile *orig_profile = priv->profile;
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	void *orig_ppriv = priv->ppriv;
	int err, rollback_err;

	/* sanity */
	if (new_max_nch != priv->max_nch) {
		netdev_warn(netdev, "%s: Replacing profile with different max channels\n",
			    __func__);
		return -EINVAL;
	}

	/* cleanup old profile */
	mlx5e_detach_netdev(priv);
	priv->profile->cleanup(priv);
	mlx5e_priv_cleanup(priv);

	err = mlx5e_netdev_attach_profile(netdev, mdev, new_profile, new_ppriv);
	if (err) { /* roll back to original profile */
		netdev_warn(netdev, "%s: new profile init failed, %d\n", __func__, err);
		goto rollback;
	}

	return 0;

rollback:
	rollback_err = mlx5e_netdev_attach_profile(netdev, mdev, orig_profile, orig_ppriv);
	if (rollback_err)
		netdev_err(netdev, "%s: failed to rollback to orig profile, %d\n",
			   __func__, rollback_err);
	return err;
}

void mlx5e_destroy_netdev(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;

	mlx5e_priv_cleanup(priv);
	free_netdev(netdev);
}

static int mlx5e_resume(struct auxiliary_device *adev)
{
	struct mlx5_adev *edev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5e_priv *priv = dev_get_drvdata(&adev->dev);
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = edev->mdev;
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

static int mlx5e_suspend(struct auxiliary_device *adev, pm_message_t state)
{
	struct mlx5e_priv *priv = dev_get_drvdata(&adev->dev);
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!netif_device_present(netdev))
		return -ENODEV;

	mlx5e_detach_netdev(priv);
	mlx5e_destroy_mdev_resources(mdev);
	return 0;
}

static int mlx5e_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)
{
	struct mlx5_adev *edev = container_of(adev, struct mlx5_adev, adev);
	const struct mlx5e_profile *profile = &mlx5e_nic_profile;
	struct mlx5_core_dev *mdev = edev->mdev;
	struct net_device *netdev;
	pm_message_t state = {};
	unsigned int txqs, rxqs, ptp_txqs = 0;
	struct mlx5e_priv *priv;
	int qos_sqs = 0;
	int err;
	int nch;

	if (MLX5_CAP_GEN(mdev, ts_cqe_to_dest_cqn))
		ptp_txqs = profile->max_tc;

	if (mlx5_qos_is_supported(mdev))
		qos_sqs = mlx5e_qos_max_leaf_nodes(mdev);

	nch = mlx5e_get_max_num_channels(mdev);
	txqs = nch * profile->max_tc + ptp_txqs + qos_sqs;
	rxqs = nch * profile->rq_groups;
	netdev = mlx5e_create_netdev(mdev, txqs, rxqs);
	if (!netdev) {
		mlx5_core_err(mdev, "mlx5e_create_netdev failed\n");
		return -ENOMEM;
	}

	mlx5e_build_nic_netdev(netdev);

	priv = netdev_priv(netdev);
	dev_set_drvdata(&adev->dev, priv);

	priv->profile = profile;
	priv->ppriv = NULL;
	err = profile->init(mdev, netdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_nic_profile init failed, %d\n", err);
		goto err_destroy_netdev;
	}

	err = mlx5e_resume(adev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_resume failed, %d\n", err);
		goto err_profile_cleanup;
	}

	err = register_netdev(netdev);
	if (err) {
		mlx5_core_err(mdev, "register_netdev failed, %d\n", err);
		goto err_resume;
	}

	mlx5e_devlink_port_type_eth_set(priv);

	mlx5e_dcbnl_init_app(priv);
	return 0;

err_resume:
	mlx5e_suspend(adev, state);
err_profile_cleanup:
	profile->cleanup(priv);
err_destroy_netdev:
	mlx5e_destroy_netdev(priv);
	return err;
}

static void mlx5e_remove(struct auxiliary_device *adev)
{
	struct mlx5e_priv *priv = dev_get_drvdata(&adev->dev);
	pm_message_t state = {};

	mlx5e_dcbnl_delete_app(priv);
	unregister_netdev(priv->netdev);
	mlx5e_suspend(adev, state);
	priv->profile->cleanup(priv);
	mlx5e_destroy_netdev(priv);
}

static const struct auxiliary_device_id mlx5e_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".eth", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx5e_id_table);

static struct auxiliary_driver mlx5e_driver = {
	.name = "eth",
	.probe = mlx5e_probe,
	.remove = mlx5e_remove,
	.suspend = mlx5e_suspend,
	.resume = mlx5e_resume,
	.id_table = mlx5e_id_table,
};

int mlx5e_init(void)
{
	int ret;

	mlx5e_ipsec_build_inverse_table();
	mlx5e_build_ptys2ethtool_map();
	ret = mlx5e_rep_init();
	if (ret)
		return ret;

	ret = auxiliary_driver_register(&mlx5e_driver);
	if (ret)
		mlx5e_rep_cleanup();
	return ret;
}

void mlx5e_cleanup(void)
{
	auxiliary_driver_unregister(&mlx5e_driver);
	mlx5e_rep_cleanup();
}

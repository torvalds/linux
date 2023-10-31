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
#include <linux/mlx5/fs.h>
#include <net/vxlan.h>
#include <net/geneve.h>
#include <linux/bpf.h>
#include <linux/debugfs.h>
#include <linux/if_bridge.h>
#include <linux/filter.h>
#include <net/page_pool/types.h>
#include <net/pkt_sched.h>
#include <net/xdp_sock_drv.h>
#include "eswitch.h"
#include "en.h"
#include "en/txrx.h"
#include "en_tc.h"
#include "en_rep.h"
#include "en_accel/ipsec.h"
#include "en_accel/macsec.h"
#include "en_accel/en_accel.h"
#include "en_accel/ktls.h"
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
#include "en/htb.h"
#include "qos.h"
#include "en/trap.h"
#include "lib/devcom.h"

bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev, u8 page_shift,
					    enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u16 umr_wqebbs, max_wqebbs;
	bool striding_rq_umr;

	striding_rq_umr = MLX5_CAP_GEN(mdev, striding_rq) && MLX5_CAP_GEN(mdev, umr_ptr_rlky) &&
			  MLX5_CAP_ETH(mdev, reg_umr_sq);
	if (!striding_rq_umr)
		return false;

	umr_wqebbs = mlx5e_mpwrq_umr_wqebbs(mdev, page_shift, umr_mode);
	max_wqebbs = mlx5e_get_max_sq_aligned_wqebbs(mdev);
	/* Sanity check; should never happen, because mlx5e_mpwrq_umr_wqebbs is
	 * calculated from mlx5e_get_max_sq_aligned_wqebbs.
	 */
	if (WARN_ON(umr_wqebbs > max_wqebbs))
		return false;

	return true;
}

void mlx5e_update_carrier(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 port_state;
	bool up;

	port_state = mlx5_query_vport_state(mdev,
					    MLX5_VPORT_STATE_OP_MOD_VNIC_VPORT,
					    0);

	up = port_state == VPORT_STATE_UP;
	if (up == netif_carrier_ok(priv->netdev))
		netif_carrier_event(priv->netdev);
	if (up) {
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

static int mlx5e_devcom_event_mpv(int event, void *my_data, void *event_data)
{
	struct mlx5e_priv *slave_priv = my_data;

	switch (event) {
	case MPV_DEVCOM_MASTER_UP:
		mlx5_devcom_comp_set_ready(slave_priv->devcom, true);
		break;
	case MPV_DEVCOM_MASTER_DOWN:
		/* no need for comp set ready false since we unregister after
		 * and it hurts cleanup flow.
		 */
		break;
	case MPV_DEVCOM_IPSEC_MASTER_UP:
	case MPV_DEVCOM_IPSEC_MASTER_DOWN:
		mlx5e_ipsec_handle_mpv_event(event, my_data, event_data);
		break;
	}

	return 0;
}

static int mlx5e_devcom_init_mpv(struct mlx5e_priv *priv, u64 *data)
{
	priv->devcom = mlx5_devcom_register_component(priv->mdev->priv.devc,
						      MLX5_DEVCOM_MPV,
						      *data,
						      mlx5e_devcom_event_mpv,
						      priv);
	if (IS_ERR_OR_NULL(priv->devcom))
		return -EOPNOTSUPP;

	if (mlx5_core_is_mp_master(priv->mdev)) {
		mlx5_devcom_send_event(priv->devcom, MPV_DEVCOM_MASTER_UP,
				       MPV_DEVCOM_MASTER_UP, priv);
		mlx5e_ipsec_send_event(priv, MPV_DEVCOM_IPSEC_MASTER_UP);
	}

	return 0;
}

static void mlx5e_devcom_cleanup_mpv(struct mlx5e_priv *priv)
{
	if (IS_ERR_OR_NULL(priv->devcom))
		return;

	if (mlx5_core_is_mp_master(priv->mdev)) {
		mlx5_devcom_send_event(priv->devcom, MPV_DEVCOM_MASTER_DOWN,
				       MPV_DEVCOM_MASTER_DOWN, priv);
		mlx5e_ipsec_send_event(priv, MPV_DEVCOM_IPSEC_MASTER_DOWN);
	}

	mlx5_devcom_unregister_component(priv->devcom);
}

static int blocking_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, blocking_events_nb);
	struct mlx5_devlink_trap_event_ctx *trap_event_ctx = data;
	int err;

	switch (event) {
	case MLX5_DRIVER_EVENT_TYPE_TRAP:
		err = mlx5e_handle_trap_event(priv, trap_event_ctx->trap);
		if (err) {
			trap_event_ctx->err = err;
			return NOTIFY_BAD;
		}
		break;
	case MLX5_DRIVER_EVENT_AFFILIATION_DONE:
		if (mlx5e_devcom_init_mpv(priv, data))
			return NOTIFY_BAD;
		break;
	case MLX5_DRIVER_EVENT_AFFILIATION_REMOVED:
		mlx5e_devcom_cleanup_mpv(priv);
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
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

static u16 mlx5e_mpwrq_umr_octowords(u32 entries, enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 umr_entry_size = mlx5e_mpwrq_umr_entry_size(umr_mode);
	u32 sz;

	sz = ALIGN(entries * umr_entry_size, MLX5_UMR_FLEX_ALIGNMENT);

	return sz / MLX5_OCTWORD;
}

static inline void mlx5e_build_umr_wqe(struct mlx5e_rq *rq,
				       struct mlx5e_icosq *sq,
				       struct mlx5e_umr_wqe *wqe)
{
	struct mlx5_wqe_ctrl_seg      *cseg = &wqe->ctrl;
	struct mlx5_wqe_umr_ctrl_seg *ucseg = &wqe->uctrl;
	u16 octowords;
	u8 ds_cnt;

	ds_cnt = DIV_ROUND_UP(mlx5e_mpwrq_umr_wqe_sz(rq->mdev, rq->mpwqe.page_shift,
						     rq->mpwqe.umr_mode),
			      MLX5_SEND_WQE_DS);

	cseg->qpn_ds    = cpu_to_be32((sq->sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
				      ds_cnt);
	cseg->umr_mkey  = rq->mpwqe.umr_mkey_be;

	ucseg->flags = MLX5_UMR_TRANSLATION_OFFSET_EN | MLX5_UMR_INLINE;
	octowords = mlx5e_mpwrq_umr_octowords(rq->mpwqe.pages_per_wqe, rq->mpwqe.umr_mode);
	ucseg->xlt_octowords = cpu_to_be16(octowords);
	ucseg->mkey_mask     = cpu_to_be64(MLX5_MKEY_MASK_FREE);
}

static int mlx5e_rq_shampo_hd_alloc(struct mlx5e_rq *rq, int node)
{
	rq->mpwqe.shampo = kvzalloc_node(sizeof(*rq->mpwqe.shampo),
					 GFP_KERNEL, node);
	if (!rq->mpwqe.shampo)
		return -ENOMEM;
	return 0;
}

static void mlx5e_rq_shampo_hd_free(struct mlx5e_rq *rq)
{
	kvfree(rq->mpwqe.shampo);
}

static int mlx5e_rq_shampo_hd_info_alloc(struct mlx5e_rq *rq, int node)
{
	struct mlx5e_shampo_hd *shampo = rq->mpwqe.shampo;

	shampo->bitmap = bitmap_zalloc_node(shampo->hd_per_wq, GFP_KERNEL,
					    node);
	shampo->info = kvzalloc_node(array_size(shampo->hd_per_wq,
						sizeof(*shampo->info)),
				     GFP_KERNEL, node);
	shampo->pages = kvzalloc_node(array_size(shampo->hd_per_wq,
						 sizeof(*shampo->pages)),
				     GFP_KERNEL, node);
	if (!shampo->bitmap || !shampo->info || !shampo->pages)
		goto err_nomem;

	return 0;

err_nomem:
	kvfree(shampo->info);
	kvfree(shampo->bitmap);
	kvfree(shampo->pages);

	return -ENOMEM;
}

static void mlx5e_rq_shampo_hd_info_free(struct mlx5e_rq *rq)
{
	kvfree(rq->mpwqe.shampo->bitmap);
	kvfree(rq->mpwqe.shampo->info);
	kvfree(rq->mpwqe.shampo->pages);
}

static int mlx5e_rq_alloc_mpwqe_info(struct mlx5e_rq *rq, int node)
{
	int wq_sz = mlx5_wq_ll_get_size(&rq->mpwqe.wq);
	size_t alloc_size;

	alloc_size = array_size(wq_sz, struct_size(rq->mpwqe.info,
						   alloc_units.frag_pages,
						   rq->mpwqe.pages_per_wqe));

	rq->mpwqe.info = kvzalloc_node(alloc_size, GFP_KERNEL, node);
	if (!rq->mpwqe.info)
		return -ENOMEM;

	/* For deferred page release (release right before alloc), make sure
	 * that on first round release is not called.
	 */
	for (int i = 0; i < wq_sz; i++) {
		struct mlx5e_mpw_info *wi = mlx5e_get_mpw_info(rq, i);

		bitmap_fill(wi->skip_release_bitmap, rq->mpwqe.pages_per_wqe);
	}

	mlx5e_build_umr_wqe(rq, rq->icosq, &rq->mpwqe.umr_wqe);

	return 0;
}


static u8 mlx5e_mpwrq_access_mode(enum mlx5e_mpwrq_umr_mode umr_mode)
{
	switch (umr_mode) {
	case MLX5E_MPWRQ_UMR_MODE_ALIGNED:
		return MLX5_MKC_ACCESS_MODE_MTT;
	case MLX5E_MPWRQ_UMR_MODE_UNALIGNED:
		return MLX5_MKC_ACCESS_MODE_KSM;
	case MLX5E_MPWRQ_UMR_MODE_OVERSIZED:
		return MLX5_MKC_ACCESS_MODE_KLMS;
	case MLX5E_MPWRQ_UMR_MODE_TRIPLE:
		return MLX5_MKC_ACCESS_MODE_KSM;
	}
	WARN_ONCE(1, "MPWRQ UMR mode %d is not known\n", umr_mode);
	return 0;
}

static int mlx5e_create_umr_mkey(struct mlx5_core_dev *mdev,
				 u32 npages, u8 page_shift, u32 *umr_mkey,
				 dma_addr_t filler_addr,
				 enum mlx5e_mpwrq_umr_mode umr_mode,
				 u32 xsk_chunk_size)
{
	struct mlx5_mtt *mtt;
	struct mlx5_ksm *ksm;
	struct mlx5_klm *klm;
	u32 octwords;
	int inlen;
	void *mkc;
	u32 *in;
	int err;
	int i;

	if ((umr_mode == MLX5E_MPWRQ_UMR_MODE_UNALIGNED ||
	     umr_mode == MLX5E_MPWRQ_UMR_MODE_TRIPLE) &&
	    !MLX5_CAP_GEN(mdev, fixed_buffer_size)) {
		mlx5_core_warn(mdev, "Unaligned AF_XDP requires fixed_buffer_size capability\n");
		return -EINVAL;
	}

	octwords = mlx5e_mpwrq_umr_octowords(npages, umr_mode);

	inlen = MLX5_FLEXIBLE_INLEN(mdev, MLX5_ST_SZ_BYTES(create_mkey_in),
				    MLX5_OCTWORD, octwords);
	if (inlen < 0)
		return inlen;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, mlx5e_mpwrq_access_mode(umr_mode));
	mlx5e_mkey_set_relaxed_ordering(mdev, mkc);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mdev->mlx5e_res.hw_objs.pdn);
	MLX5_SET64(mkc, mkc, len, npages << page_shift);
	MLX5_SET(mkc, mkc, translations_octword_size, octwords);
	if (umr_mode == MLX5E_MPWRQ_UMR_MODE_TRIPLE)
		MLX5_SET(mkc, mkc, log_page_size, page_shift - 2);
	else if (umr_mode != MLX5E_MPWRQ_UMR_MODE_OVERSIZED)
		MLX5_SET(mkc, mkc, log_page_size, page_shift);
	MLX5_SET(create_mkey_in, in, translations_octword_actual_size, octwords);

	/* Initialize the mkey with all MTTs pointing to a default
	 * page (filler_addr). When the channels are activated, UMR
	 * WQEs will redirect the RX WQEs to the actual memory from
	 * the RQ's pool, while the gaps (wqe_overflow) remain mapped
	 * to the default page.
	 */
	switch (umr_mode) {
	case MLX5E_MPWRQ_UMR_MODE_OVERSIZED:
		klm = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages; i++) {
			klm[i << 1] = (struct mlx5_klm) {
				.va = cpu_to_be64(filler_addr),
				.bcount = cpu_to_be32(xsk_chunk_size),
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
			};
			klm[(i << 1) + 1] = (struct mlx5_klm) {
				.va = cpu_to_be64(filler_addr),
				.bcount = cpu_to_be32((1 << page_shift) - xsk_chunk_size),
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
			};
		}
		break;
	case MLX5E_MPWRQ_UMR_MODE_UNALIGNED:
		ksm = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages; i++)
			ksm[i] = (struct mlx5_ksm) {
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
				.va = cpu_to_be64(filler_addr),
			};
		break;
	case MLX5E_MPWRQ_UMR_MODE_ALIGNED:
		mtt = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages; i++)
			mtt[i] = (struct mlx5_mtt) {
				.ptag = cpu_to_be64(filler_addr),
			};
		break;
	case MLX5E_MPWRQ_UMR_MODE_TRIPLE:
		ksm = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
		for (i = 0; i < npages * 4; i++) {
			ksm[i] = (struct mlx5_ksm) {
				.key = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey),
				.va = cpu_to_be64(filler_addr),
			};
		}
		break;
	}

	err = mlx5_core_create_mkey(mdev, umr_mkey, in, inlen);

	kvfree(in);
	return err;
}

static int mlx5e_create_umr_klm_mkey(struct mlx5_core_dev *mdev,
				     u64 nentries,
				     u32 *umr_mkey)
{
	int inlen;
	void *mkc;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in);

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_KLMS);
	mlx5e_mkey_set_relaxed_ordering(mdev, mkc);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mdev->mlx5e_res.hw_objs.pdn);
	MLX5_SET(mkc, mkc, translations_octword_size, nentries);
	MLX5_SET(mkc, mkc, length64, 1);
	err = mlx5_core_create_mkey(mdev, umr_mkey, in, inlen);

	kvfree(in);
	return err;
}

static int mlx5e_create_rq_umr_mkey(struct mlx5_core_dev *mdev, struct mlx5e_rq *rq)
{
	u32 xsk_chunk_size = rq->xsk_pool ? rq->xsk_pool->chunk_size : 0;
	u32 wq_size = mlx5_wq_ll_get_size(&rq->mpwqe.wq);
	u32 num_entries, max_num_entries;
	u32 umr_mkey;
	int err;

	max_num_entries = mlx5e_mpwrq_max_num_entries(mdev, rq->mpwqe.umr_mode);

	/* Shouldn't overflow, the result is at most MLX5E_MAX_RQ_NUM_MTTS. */
	if (WARN_ON_ONCE(check_mul_overflow(wq_size, (u32)rq->mpwqe.mtts_per_wqe,
					    &num_entries) ||
			 num_entries > max_num_entries))
		mlx5_core_err(mdev, "%s: multiplication overflow: %u * %u > %u\n",
			      __func__, wq_size, rq->mpwqe.mtts_per_wqe,
			      max_num_entries);

	err = mlx5e_create_umr_mkey(mdev, num_entries, rq->mpwqe.page_shift,
				    &umr_mkey, rq->wqe_overflow.addr,
				    rq->mpwqe.umr_mode, xsk_chunk_size);
	rq->mpwqe.umr_mkey_be = cpu_to_be32(umr_mkey);
	return err;
}

static int mlx5e_create_rq_hd_umr_mkey(struct mlx5_core_dev *mdev,
				       struct mlx5e_rq *rq)
{
	u32 max_klm_size = BIT(MLX5_CAP_GEN(mdev, log_max_klm_list_size));

	if (max_klm_size < rq->mpwqe.shampo->hd_per_wq) {
		mlx5_core_err(mdev, "max klm list size 0x%x is smaller than shampo header buffer list size 0x%x\n",
			      max_klm_size, rq->mpwqe.shampo->hd_per_wq);
		return -EINVAL;
	}
	return mlx5e_create_umr_klm_mkey(mdev, rq->mpwqe.shampo->hd_per_wq,
					 &rq->mpwqe.shampo->mkey);
}

static void mlx5e_init_frags_partition(struct mlx5e_rq *rq)
{
	struct mlx5e_wqe_frag_info next_frag = {};
	struct mlx5e_wqe_frag_info *prev = NULL;
	int i;

	WARN_ON(rq->xsk_pool);

	next_frag.frag_page = &rq->wqe.alloc_units->frag_pages[0];

	/* Skip first release due to deferred release. */
	next_frag.flags = BIT(MLX5E_WQE_FRAG_SKIP_RELEASE);

	for (i = 0; i < mlx5_wq_cyc_get_size(&rq->wqe.wq); i++) {
		struct mlx5e_rq_frag_info *frag_info = &rq->wqe.info.arr[0];
		struct mlx5e_wqe_frag_info *frag =
			&rq->wqe.frags[i << rq->wqe.info.log_num_frags];
		int f;

		for (f = 0; f < rq->wqe.info.num_frags; f++, frag++) {
			if (next_frag.offset + frag_info[f].frag_stride > PAGE_SIZE) {
				/* Pages are assigned at runtime. */
				next_frag.frag_page++;
				next_frag.offset = 0;
				if (prev)
					prev->flags |= BIT(MLX5E_WQE_FRAG_LAST_IN_PAGE);
			}
			*frag = next_frag;

			/* prepare next */
			next_frag.offset += frag_info[f].frag_stride;
			prev = frag;
		}
	}

	if (prev)
		prev->flags |= BIT(MLX5E_WQE_FRAG_LAST_IN_PAGE);
}

static void mlx5e_init_xsk_buffs(struct mlx5e_rq *rq)
{
	int i;

	/* Assumptions used by XSK batched allocator. */
	WARN_ON(rq->wqe.info.num_frags != 1);
	WARN_ON(rq->wqe.info.log_num_frags != 0);
	WARN_ON(rq->wqe.info.arr[0].frag_stride != PAGE_SIZE);

	/* Considering the above assumptions a fragment maps to a single
	 * xsk_buff.
	 */
	for (i = 0; i < mlx5_wq_cyc_get_size(&rq->wqe.wq); i++) {
		rq->wqe.frags[i].xskp = &rq->wqe.alloc_units->xsk_buffs[i];

		/* Skip first release due to deferred release as WQES are
		 * not allocated yet.
		 */
		rq->wqe.frags[i].flags |= BIT(MLX5E_WQE_FRAG_SKIP_RELEASE);
	}
}

static int mlx5e_init_wqe_alloc_info(struct mlx5e_rq *rq, int node)
{
	int wq_sz = mlx5_wq_cyc_get_size(&rq->wqe.wq);
	int len = wq_sz << rq->wqe.info.log_num_frags;
	struct mlx5e_wqe_frag_info *frags;
	union mlx5e_alloc_units *aus;
	int aus_sz;

	if (rq->xsk_pool)
		aus_sz = sizeof(*aus->xsk_buffs);
	else
		aus_sz = sizeof(*aus->frag_pages);

	aus = kvzalloc_node(array_size(len, aus_sz), GFP_KERNEL, node);
	if (!aus)
		return -ENOMEM;

	frags = kvzalloc_node(array_size(len, sizeof(*frags)), GFP_KERNEL, node);
	if (!frags) {
		kvfree(aus);
		return -ENOMEM;
	}

	rq->wqe.alloc_units = aus;
	rq->wqe.frags = frags;

	if (rq->xsk_pool)
		mlx5e_init_xsk_buffs(rq);
	else
		mlx5e_init_frags_partition(rq);

	return 0;
}

static void mlx5e_free_wqe_alloc_info(struct mlx5e_rq *rq)
{
	kvfree(rq->wqe.frags);
	kvfree(rq->wqe.alloc_units);
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

static int mlx5e_init_rxq_rq(struct mlx5e_channel *c, struct mlx5e_params *params,
			     u32 xdp_frag_size, struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = c->mdev;
	int err;

	rq->wq_type      = params->rq_wq_type;
	rq->pdev         = c->pdev;
	rq->netdev       = c->netdev;
	rq->priv         = c->priv;
	rq->tstamp       = c->tstamp;
	rq->clock        = &mdev->clock;
	rq->icosq        = &c->icosq;
	rq->ix           = c->ix;
	rq->channel      = c;
	rq->mdev         = mdev;
	rq->hw_mtu =
		MLX5E_SW2HW_MTU(params, params->sw_mtu) - ETH_FCS_LEN * !params->scatter_fcs_en;
	rq->xdpsq        = &c->rq_xdpsq;
	rq->stats        = &c->priv->channel_stats[c->ix]->rq;
	rq->ptp_cyc2time = mlx5_rq_ts_translator(mdev);
	err = mlx5e_rq_set_handlers(rq, params, NULL);
	if (err)
		return err;

	return __xdp_rxq_info_reg(&rq->xdp_rxq, rq->netdev, rq->ix, c->napi.napi_id,
				  xdp_frag_size);
}

static int mlx5_rq_shampo_alloc(struct mlx5_core_dev *mdev,
				struct mlx5e_params *params,
				struct mlx5e_rq_param *rqp,
				struct mlx5e_rq *rq,
				u32 *pool_size,
				int node)
{
	void *wqc = MLX5_ADDR_OF(rqc, rqp->rqc, wq);
	int wq_size;
	int err;

	if (!test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state))
		return 0;
	err = mlx5e_rq_shampo_hd_alloc(rq, node);
	if (err)
		goto out;
	rq->mpwqe.shampo->hd_per_wq =
		mlx5e_shampo_hd_per_wq(mdev, params, rqp);
	err = mlx5e_create_rq_hd_umr_mkey(mdev, rq);
	if (err)
		goto err_shampo_hd;
	err = mlx5e_rq_shampo_hd_info_alloc(rq, node);
	if (err)
		goto err_shampo_info;
	rq->hw_gro_data = kvzalloc_node(sizeof(*rq->hw_gro_data), GFP_KERNEL, node);
	if (!rq->hw_gro_data) {
		err = -ENOMEM;
		goto err_hw_gro_data;
	}
	rq->mpwqe.shampo->key =
		cpu_to_be32(rq->mpwqe.shampo->mkey);
	rq->mpwqe.shampo->hd_per_wqe =
		mlx5e_shampo_hd_per_wqe(mdev, params, rqp);
	wq_size = BIT(MLX5_GET(wq, wqc, log_wq_sz));
	*pool_size += (rq->mpwqe.shampo->hd_per_wqe * wq_size) /
		     MLX5E_SHAMPO_WQ_HEADER_PER_PAGE;
	return 0;

err_hw_gro_data:
	mlx5e_rq_shampo_hd_info_free(rq);
err_shampo_info:
	mlx5_core_destroy_mkey(mdev, rq->mpwqe.shampo->mkey);
err_shampo_hd:
	mlx5e_rq_shampo_hd_free(rq);
out:
	return err;
}

static void mlx5e_rq_free_shampo(struct mlx5e_rq *rq)
{
	if (!test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state))
		return;

	kvfree(rq->hw_gro_data);
	mlx5e_rq_shampo_hd_info_free(rq);
	mlx5_core_destroy_mkey(rq->mdev, rq->mpwqe.shampo->mkey);
	mlx5e_rq_shampo_hd_free(rq);
}

static int mlx5e_alloc_rq(struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk,
			  struct mlx5e_rq_param *rqp,
			  int node, struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = rq->mdev;
	void *rqc = rqp->rqc;
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqc, wq);
	u32 pool_size;
	int wq_sz;
	int err;
	int i;

	rqp->wq.db_numa_node = node;
	INIT_WORK(&rq->recover_work, mlx5e_rq_err_cqe_work);

	if (params->xdp_prog)
		bpf_prog_inc(params->xdp_prog);
	RCU_INIT_POINTER(rq->xdp_prog, params->xdp_prog);

	rq->buff.map_dir = params->xdp_prog ? DMA_BIDIRECTIONAL : DMA_FROM_DEVICE;
	rq->buff.headroom = mlx5e_get_rq_headroom(mdev, params, xsk);
	pool_size = 1 << params->log_rq_mtu_frames;

	rq->mkey_be = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey);

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		err = mlx5_wq_ll_create(mdev, &rqp->wq, rqc_wq, &rq->mpwqe.wq,
					&rq->wq_ctrl);
		if (err)
			goto err_rq_xdp_prog;

		err = mlx5e_alloc_mpwqe_rq_drop_page(rq);
		if (err)
			goto err_rq_wq_destroy;

		rq->mpwqe.wq.db = &rq->mpwqe.wq.db[MLX5_RCV_DBR];

		wq_sz = mlx5_wq_ll_get_size(&rq->mpwqe.wq);

		rq->mpwqe.page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
		rq->mpwqe.umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
		rq->mpwqe.pages_per_wqe =
			mlx5e_mpwrq_pages_per_wqe(mdev, rq->mpwqe.page_shift,
						  rq->mpwqe.umr_mode);
		rq->mpwqe.umr_wqebbs =
			mlx5e_mpwrq_umr_wqebbs(mdev, rq->mpwqe.page_shift,
					       rq->mpwqe.umr_mode);
		rq->mpwqe.mtts_per_wqe =
			mlx5e_mpwrq_mtts_per_wqe(mdev, rq->mpwqe.page_shift,
						 rq->mpwqe.umr_mode);

		pool_size = rq->mpwqe.pages_per_wqe <<
			mlx5e_mpwqe_get_log_rq_size(mdev, params, xsk);

		if (!mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk) && params->xdp_prog)
			pool_size *= 2; /* additional page per packet for the linear part */

		rq->mpwqe.log_stride_sz = mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
		rq->mpwqe.num_strides =
			BIT(mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk));
		rq->mpwqe.min_wqe_bulk = mlx5e_mpwqe_get_min_wqe_bulk(wq_sz);

		rq->buff.frame0_sz = (1 << rq->mpwqe.log_stride_sz);

		err = mlx5e_create_rq_umr_mkey(mdev, rq);
		if (err)
			goto err_rq_drop_page;

		err = mlx5e_rq_alloc_mpwqe_info(rq, node);
		if (err)
			goto err_rq_mkey;

		err = mlx5_rq_shampo_alloc(mdev, params, rqp, rq, &pool_size, node);
		if (err)
			goto err_free_mpwqe_info;

		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		err = mlx5_wq_cyc_create(mdev, &rqp->wq, rqc_wq, &rq->wqe.wq,
					 &rq->wq_ctrl);
		if (err)
			goto err_rq_xdp_prog;

		rq->wqe.wq.db = &rq->wqe.wq.db[MLX5_RCV_DBR];

		wq_sz = mlx5_wq_cyc_get_size(&rq->wqe.wq);

		rq->wqe.info = rqp->frags_info;
		rq->buff.frame0_sz = rq->wqe.info.arr[0].frag_stride;

		err = mlx5e_init_wqe_alloc_info(rq, node);
		if (err)
			goto err_rq_wq_destroy;
	}

	if (xsk) {
		err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
						 MEM_TYPE_XSK_BUFF_POOL, NULL);
		xsk_pool_set_rxq_info(rq->xsk_pool, &rq->xdp_rxq);
	} else {
		/* Create a page_pool and register it with rxq */
		struct page_pool_params pp_params = { 0 };

		pp_params.order     = 0;
		pp_params.flags     = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV | PP_FLAG_PAGE_FRAG;
		pp_params.pool_size = pool_size;
		pp_params.nid       = node;
		pp_params.dev       = rq->pdev;
		pp_params.napi      = rq->cq.napi;
		pp_params.dma_dir   = rq->buff.map_dir;
		pp_params.max_len   = PAGE_SIZE;

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
		if (xdp_rxq_info_is_reg(&rq->xdp_rxq))
			err = xdp_rxq_info_reg_mem_model(&rq->xdp_rxq,
							 MEM_TYPE_PAGE_POOL, rq->page_pool);
	}
	if (err)
		goto err_destroy_page_pool;

	for (i = 0; i < wq_sz; i++) {
		if (rq->wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
			struct mlx5e_rx_wqe_ll *wqe =
				mlx5_wq_ll_get_wqe(&rq->mpwqe.wq, i);
			u32 byte_count =
				rq->mpwqe.num_strides << rq->mpwqe.log_stride_sz;
			u64 dma_offset = mul_u32_u32(i, rq->mpwqe.mtts_per_wqe) <<
				rq->mpwqe.page_shift;
			u16 headroom = test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state) ?
				       0 : rq->buff.headroom;

			wqe->data[0].addr = cpu_to_be64(dma_offset + headroom);
			wqe->data[0].byte_count = cpu_to_be32(byte_count);
			wqe->data[0].lkey = rq->mpwqe.umr_mkey_be;
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
				wqe->data[f].lkey = params->terminate_lkey_be;
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

	return 0;

err_destroy_page_pool:
	page_pool_destroy(rq->page_pool);
err_free_by_rq_type:
	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		mlx5e_rq_free_shampo(rq);
err_free_mpwqe_info:
		kvfree(rq->mpwqe.info);
err_rq_mkey:
		mlx5_core_destroy_mkey(mdev, be32_to_cpu(rq->mpwqe.umr_mkey_be));
err_rq_drop_page:
		mlx5e_free_mpwqe_rq_drop_page(rq);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		mlx5e_free_wqe_alloc_info(rq);
	}
err_rq_wq_destroy:
	mlx5_wq_destroy(&rq->wq_ctrl);
err_rq_xdp_prog:
	if (params->xdp_prog)
		bpf_prog_put(params->xdp_prog);

	return err;
}

static void mlx5e_free_rq(struct mlx5e_rq *rq)
{
	struct bpf_prog *old_prog;

	if (xdp_rxq_info_is_reg(&rq->xdp_rxq)) {
		old_prog = rcu_dereference_protected(rq->xdp_prog,
						     lockdep_is_held(&rq->priv->state_lock));
		if (old_prog)
			bpf_prog_put(old_prog);
	}

	switch (rq->wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		kvfree(rq->mpwqe.info);
		mlx5_core_destroy_mkey(rq->mdev, be32_to_cpu(rq->mpwqe.umr_mkey_be));
		mlx5e_free_mpwqe_rq_drop_page(rq);
		mlx5e_rq_free_shampo(rq);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		mlx5e_free_wqe_alloc_info(rq);
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
			    MLX5_TIMESTAMP_FORMAT_REAL_TIME :
			    MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
	rqc = MLX5_ADDR_OF(create_rq_in, in, ctx);
	wq  = MLX5_ADDR_OF(rqc, rqc, wq);

	memcpy(rqc, param->rqc, sizeof(param->rqc));

	MLX5_SET(rqc,  rqc, cqn,		rq->cq.mcq.cqn);
	MLX5_SET(rqc,  rqc, state,		MLX5_RQC_STATE_RST);
	MLX5_SET(rqc,  rqc, ts_format,		ts_format);
	MLX5_SET(wq,   wq,  log_wq_pg_sz,	rq->wq_ctrl.buf.page_shift -
						MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq,  dbr_addr,		rq->wq_ctrl.db.dma);

	if (test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state)) {
		MLX5_SET(wq, wq, log_headers_buffer_entry_num,
			 order_base_2(rq->mpwqe.shampo->hd_per_wq));
		MLX5_SET(wq, wq, headers_mkey, rq->mpwqe.shampo->mkey);
	}

	mlx5_fill_page_frag_array(&rq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_rq(mdev, in, inlen, &rq->rqn);

	kvfree(in);

	return err;
}

static int mlx5e_modify_rq_state(struct mlx5e_rq *rq, int curr_state, int next_state)
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

static void mlx5e_flush_rq_cq(struct mlx5e_rq *rq)
{
	struct mlx5_cqwq *cqwq = &rq->cq.wq;
	struct mlx5_cqe64 *cqe;

	if (test_bit(MLX5E_RQ_STATE_MINI_CQE_ENHANCED, &rq->state)) {
		while ((cqe = mlx5_cqwq_get_cqe_enahnced_comp(cqwq)))
			mlx5_cqwq_pop(cqwq);
	} else {
		while ((cqe = mlx5_cqwq_get_cqe(cqwq)))
			mlx5_cqwq_pop(cqwq);
	}

	mlx5_cqwq_update_db_record(cqwq);
}

int mlx5e_flush_rq(struct mlx5e_rq *rq, int curr_state)
{
	struct net_device *dev = rq->netdev;
	int err;

	err = mlx5e_modify_rq_state(rq, curr_state, MLX5_RQC_STATE_RST);
	if (err) {
		netdev_err(dev, "Failed to move rq 0x%x to reset\n", rq->rqn);
		return err;
	}

	mlx5e_free_rx_descs(rq);
	mlx5e_flush_rq_cq(rq);

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err) {
		netdev_err(dev, "Failed to move rq 0x%x to ready\n", rq->rqn);
		return err;
	}

	return 0;
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

void mlx5e_free_rx_missing_descs(struct mlx5e_rq *rq)
{
	struct mlx5_wq_ll *wq;
	u16 head;
	int i;

	if (rq->wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return;

	wq = &rq->mpwqe.wq;
	head = wq->head;

	/* Release WQEs that are in missing state: they have been
	 * popped from the list after completion but were not freed
	 * due to deferred release.
	 * Also free the linked-list reserved entry, hence the "+ 1".
	 */
	for (i = 0; i < mlx5_wq_ll_missing(wq) + 1; i++) {
		rq->dealloc_wqe(rq, head);
		head = mlx5_wq_ll_get_wqe_next_ix(wq, head);
	}

	if (test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state)) {
		u16 len;

		len = (rq->mpwqe.shampo->pi - rq->mpwqe.shampo->ci) &
		      (rq->mpwqe.shampo->hd_per_wq - 1);
		mlx5e_shampo_dealloc_hd(rq, len, rq->mpwqe.shampo->ci, false);
		rq->mpwqe.shampo->pi = rq->mpwqe.shampo->ci;
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

		mlx5e_free_rx_missing_descs(rq);

		while (!mlx5_wq_ll_is_empty(wq)) {
			struct mlx5e_rx_wqe_ll *wqe;

			wqe_ix_be = *wq->tail_next;
			wqe_ix    = be16_to_cpu(wqe_ix_be);
			wqe       = mlx5_wq_ll_get_wqe(wq, wqe_ix);
			rq->dealloc_wqe(rq, wqe_ix);
			mlx5_wq_ll_pop(wq, wqe_ix_be,
				       &wqe->next.next_wqe_index);
		}

		if (test_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state))
			mlx5e_shampo_dealloc_hd(rq, rq->mpwqe.shampo->hd_per_wq,
						0, true);
	} else {
		struct mlx5_wq_cyc *wq = &rq->wqe.wq;
		u16 missing = mlx5_wq_cyc_missing(wq);
		u16 head = mlx5_wq_cyc_get_head(wq);

		while (!mlx5_wq_cyc_is_empty(wq)) {
			wqe_ix = mlx5_wq_cyc_get_tail(wq);
			rq->dealloc_wqe(rq, wqe_ix);
			mlx5_wq_cyc_pop(wq);
		}
		/* Missing slots might also contain unreleased pages due to
		 * deferred release.
		 */
		while (missing--) {
			wqe_ix = mlx5_wq_cyc_ctr2ix(wq, head++);
			rq->dealloc_wqe(rq, wqe_ix);
		}
	}

}

int mlx5e_open_rq(struct mlx5e_params *params, struct mlx5e_rq_param *param,
		  struct mlx5e_xsk_param *xsk, int node,
		  struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = rq->mdev;
	int err;

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO)
		__set_bit(MLX5E_RQ_STATE_SHAMPO, &rq->state);

	err = mlx5e_alloc_rq(params, xsk, param, node, rq);
	if (err)
		return err;

	err = mlx5e_create_rq(rq, param);
	if (err)
		goto err_free_rq;

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_destroy_rq;

	if (MLX5_CAP_ETH(mdev, cqe_checksum_full))
		__set_bit(MLX5E_RQ_STATE_CSUM_FULL, &rq->state);

	if (params->rx_dim_enabled)
		__set_bit(MLX5E_RQ_STATE_DIM, &rq->state);

	/* We disable csum_complete when XDP is enabled since
	 * XDP programs might manipulate packets which will render
	 * skb->checksum incorrect.
	 */
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE) || params->xdp_prog)
		__set_bit(MLX5E_RQ_STATE_NO_CSUM_COMPLETE, &rq->state);

	/* For CQE compression on striding RQ, use stride index provided by
	 * HW if capability is supported.
	 */
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ) &&
	    MLX5_CAP_GEN(mdev, mini_cqe_resp_stride_index))
		__set_bit(MLX5E_RQ_STATE_MINI_CQE_HW_STRIDX, &rq->state);

	/* For enhanced CQE compression packet processing. decompress
	 * session according to the enhanced layout.
	 */
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS) &&
	    MLX5_CAP_GEN(mdev, enhanced_cqe_compression))
		__set_bit(MLX5E_RQ_STATE_MINI_CQE_ENHANCED, &rq->state);

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
}

void mlx5e_deactivate_rq(struct mlx5e_rq *rq)
{
	clear_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
	synchronize_net(); /* Sync with NAPI to prevent mlx5e_post_rx_wqes. */
}

void mlx5e_close_rq(struct mlx5e_rq *rq)
{
	cancel_work_sync(&rq->dim.work);
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
	int entries;
	size_t size;

	/* upper bound for maximum num of entries of all xmit_modes. */
	entries = roundup_pow_of_two(wq_sz * MLX5_SEND_WQEBB_NUM_DS *
				     MLX5E_XDP_FIFO_ENTRIES2DS_MAX_RATIO);

	size = array_size(sizeof(*xdpi_fifo->xi), entries);
	xdpi_fifo->xi = kvzalloc_node(size, GFP_KERNEL, numa);
	if (!xdpi_fifo->xi)
		return -ENOMEM;

	xdpi_fifo->pc   = &sq->xdpi_fifo_pc;
	xdpi_fifo->cc   = &sq->xdpi_fifo_cc;
	xdpi_fifo->mask = entries - 1;

	return 0;
}

static int mlx5e_alloc_xdpsq_db(struct mlx5e_xdpsq *sq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&sq->wq);
	size_t size;
	int err;

	size = array_size(sizeof(*sq->db.wqe_info), wq_sz);
	sq->db.wqe_info = kvzalloc_node(size, GFP_KERNEL, numa);
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
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu) - ETH_FCS_LEN;
	sq->xsk_pool  = xsk_pool;

	sq->stats = sq->xsk_pool ?
		&c->priv->channel_stats[c->ix]->xsksq :
		is_redirect ?
			&c->priv->channel_stats[c->ix]->xdpsq :
			&c->priv->channel_stats[c->ix]->rq_xdpsq;
	sq->stop_room = param->is_mpw ? mlx5e_stop_room_for_mpwqe(mdev) :
					mlx5e_stop_room_for_max_wqe(mdev);
	sq->max_sq_mpw_wqebbs = mlx5e_get_max_sq_aligned_wqebbs(mdev);

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

static void mlx5e_async_icosq_err_cqe_work(struct work_struct *recover_work)
{
	struct mlx5e_icosq *sq = container_of(recover_work, struct mlx5e_icosq,
					      recover_work);

	/* Not implemented yet. */

	netdev_warn(sq->channel->netdev, "async_icosq recovery is not implemented\n");
}

static int mlx5e_alloc_icosq(struct mlx5e_channel *c,
			     struct mlx5e_sq_param *param,
			     struct mlx5e_icosq *sq,
			     work_func_t recover_work_func)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;

	sq->channel   = c;
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->reserved_room = param->stop_room;

	param->wq.db_numa_node = cpu_to_node(c->cpu);
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_icosq_db(sq, cpu_to_node(c->cpu));
	if (err)
		goto err_sq_wq_destroy;

	INIT_WORK(&sq->recover_work, recover_work_func);

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
	sq->clock     = &mdev->clock;
	sq->mkey_be   = c->mkey_be;
	sq->netdev    = c->netdev;
	sq->mdev      = c->mdev;
	sq->channel   = c;
	sq->priv      = c->priv;
	sq->ch_ix     = c->ix;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	sq->max_sq_mpw_wqebbs = mlx5e_get_max_sq_aligned_wqebbs(mdev);
	INIT_WORK(&sq->recover_work, mlx5e_tx_err_cqe_work);
	if (!MLX5_CAP_ETH(mdev, wqe_vlan_insert))
		set_bit(MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE, &sq->state);
	if (mlx5_ipsec_device_caps(c->priv->mdev))
		set_bit(MLX5E_SQ_STATE_IPSEC, &sq->state);
	if (param->is_mpw)
		set_bit(MLX5E_SQ_STATE_MPWQE, &sq->state);
	sq->stop_room = param->stop_room;
	sq->ptp_cyc2time = mlx5_sq_ts_translator(mdev);

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
			    MLX5_TIMESTAMP_FORMAT_REAL_TIME :
			    MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
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
	MLX5_SET(wq,   wq, uar_page,      mdev->mlx5e_res.hw_objs.bfreg.index);
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
		     struct mlx5e_txqsq *sq, int tc, u16 qos_queue_group_id,
		     struct mlx5e_sq_stats *sq_stats)
{
	struct mlx5e_create_sq_param csp = {};
	u32 tx_rate;
	int err;

	err = mlx5e_alloc_txqsq(c, txq_ix, params, param, sq, tc);
	if (err)
		return err;

	sq->stats = sq_stats;

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
		sq->state |= BIT(MLX5E_SQ_STATE_DIM);

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

static int mlx5e_open_icosq(struct mlx5e_channel *c, struct mlx5e_params *params,
			    struct mlx5e_sq_param *param, struct mlx5e_icosq *sq,
			    work_func_t recover_work_func)
{
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_alloc_icosq(c, param, sq, recover_work_func);
	if (err)
		return err;

	csp.cqn             = sq->cq.mcq.cqn;
	csp.wq_ctrl         = &sq->wq_ctrl;
	csp.min_inline_mode = params->tx_min_inline_mode;
	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, 0, &sq->sqn);
	if (err)
		goto err_free_icosq;

	if (param->is_tls) {
		sq->ktls_resync = mlx5e_ktls_rx_resync_create_resp_list();
		if (IS_ERR(sq->ktls_resync)) {
			err = PTR_ERR(sq->ktls_resync);
			goto err_destroy_icosq;
		}
	}
	return 0;

err_destroy_icosq:
	mlx5e_destroy_sq(c->mdev, sq->sqn);
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

static void mlx5e_close_icosq(struct mlx5e_icosq *sq)
{
	struct mlx5e_channel *c = sq->channel;

	if (sq->ktls_resync)
		mlx5e_ktls_rx_resync_destroy_resp_list(sq->ktls_resync);
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

	if (param->is_xdp_mb)
		set_bit(MLX5E_SQ_STATE_XDP_MULTIBUF, &sq->state);

	err = mlx5e_create_sq_rdy(c->mdev, param, &csp, 0, &sq->sqn);
	if (err)
		goto err_free_xdpsq;

	mlx5e_set_xmit_fp(sq, param->is_mpw);

	if (!param->is_mpw && !test_bit(MLX5E_SQ_STATE_XDP_MULTIBUF, &sq->state)) {
		unsigned int ds_cnt = MLX5E_TX_WQE_EMPTY_DS_COUNT + 1;
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

			sq->db.wqe_info[i] = (struct mlx5e_xdp_wqe_info) {
				.num_wqebbs = 1,
				.num_pkts   = 1,
			};

			cseg->qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
			eseg->inline_hdr.sz = cpu_to_be16(inline_hdr_sz);
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
	int err;
	u32 i;

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

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(&cq->wq, i);

		cqe->op_own = 0xf1;
		cqe->validity_iteration_count = 0xff;
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
	int eqn;
	int err;

	err = mlx5_comp_eqn_get(mdev, param->eq_ix, &eqn);
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
	MLX5_SET(cqc,   cqc, c_eqn_or_apu_element, eqn);
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

static int mlx5e_mqprio_txq_to_tc(struct netdev_tc_txq *tc_to_txq, unsigned int txq)
{
	int tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++)
		if (txq - tc_to_txq[tc].offset < tc_to_txq[tc].count)
			return tc;

	WARN(1, "Unexpected TCs configuration. No match found for txq %u", txq);
	return -ENOENT;
}

static int mlx5e_txq_get_qos_node_hw_id(struct mlx5e_params *params, int txq_ix,
					u32 *hw_id)
{
	int tc;

	if (params->mqprio.mode != TC_MQPRIO_MODE_CHANNEL) {
		*hw_id = 0;
		return 0;
	}

	tc = mlx5e_mqprio_txq_to_tc(params->mqprio.tc_to_txq, txq_ix);
	if (tc < 0)
		return tc;

	if (tc >= params->mqprio.num_tc) {
		WARN(1, "Unexpected TCs configuration. tc %d is out of range of %u",
		     tc, params->mqprio.num_tc);
		return -EINVAL;
	}

	*hw_id = params->mqprio.channel.hw_id[tc];
	return 0;
}

static int mlx5e_open_sqs(struct mlx5e_channel *c,
			  struct mlx5e_params *params,
			  struct mlx5e_channel_param *cparam)
{
	int err, tc;

	for (tc = 0; tc < mlx5e_get_dcb_num_tc(params); tc++) {
		int txq_ix = c->ix + tc * params->num_channels;
		u32 qos_queue_group_id;

		err = mlx5e_txq_get_qos_node_hw_id(params, txq_ix, &qos_queue_group_id);
		if (err)
			goto err_close_sqs;

		err = mlx5e_open_txqsq(c, c->priv->tisn[c->lag_port][tc], txq_ix,
				       params, &cparam->txq_sq, &c->sq[tc], tc,
				       qos_queue_group_id,
				       &c->priv->channel_stats[c->ix]->sq[tc]);
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

static int mlx5e_open_rxq_rq(struct mlx5e_channel *c, struct mlx5e_params *params,
			     struct mlx5e_rq_param *rq_params)
{
	int err;

	err = mlx5e_init_rxq_rq(c, params, rq_params->xdp_frag_size, &c->rq);
	if (err)
		return err;

	return mlx5e_open_rq(params, rq_params, NULL, cpu_to_node(c->cpu), &c->rq);
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

	err = mlx5e_open_icosq(c, params, &cparam->async_icosq, &c->async_icosq,
			       mlx5e_async_icosq_err_cqe_work);
	if (err)
		goto err_close_xdpsq_cq;

	mutex_init(&c->icosq_recovery_lock);

	err = mlx5e_open_icosq(c, params, &cparam->icosq, &c->icosq,
			       mlx5e_icosq_err_cqe_work);
	if (err)
		goto err_close_async_icosq;

	err = mlx5e_open_sqs(c, params, cparam);
	if (err)
		goto err_close_icosq;

	err = mlx5e_open_rxq_rq(c, params, &cparam->rq);
	if (err)
		goto err_close_sqs;

	if (c->xdp) {
		err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq, NULL,
				       &c->rq_xdpsq, false);
		if (err)
			goto err_close_rq;
	}

	err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq, NULL, &c->xdpsq, true);
	if (err)
		goto err_close_xdp_sq;

	return 0;

err_close_xdp_sq:
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq_xdpsq);

err_close_rq:
	mlx5e_close_rq(&c->rq);

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
	if (c->xdp)
		mlx5e_close_xdpsq(&c->rq_xdpsq);
	/* The same ICOSQ is used for UMRs for both RQ and XSKRQ. */
	cancel_work_sync(&c->icosq.recover_work);
	mlx5e_close_rq(&c->rq);
	mlx5e_close_sqs(c);
	mlx5e_close_icosq(&c->icosq);
	mutex_destroy(&c->icosq_recovery_lock);
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

static int mlx5e_channel_stats_alloc(struct mlx5e_priv *priv, int ix, int cpu)
{
	if (ix > priv->stats_nch)  {
		netdev_warn(priv->netdev, "Unexpected channel stats index %d > %d\n", ix,
			    priv->stats_nch);
		return -EINVAL;
	}

	if (priv->channel_stats[ix])
		return 0;

	/* Asymmetric dynamic memory allocation.
	 * Freed in mlx5e_priv_arrays_free, not on channel closure.
	 */
	netdev_dbg(priv->netdev, "Creating channel stats %d\n", ix);
	priv->channel_stats[ix] = kvzalloc_node(sizeof(**priv->channel_stats),
						GFP_KERNEL, cpu_to_node(cpu));
	if (!priv->channel_stats[ix])
		return -ENOMEM;
	priv->stats_nch++;

	return 0;
}

void mlx5e_trigger_napi_icosq(struct mlx5e_channel *c)
{
	spin_lock_bh(&c->async_icosq_lock);
	mlx5e_trigger_irq(&c->async_icosq);
	spin_unlock_bh(&c->async_icosq_lock);
}

void mlx5e_trigger_napi_sched(struct napi_struct *napi)
{
	local_bh_disable();
	napi_schedule(napi);
	local_bh_enable();
}

static int mlx5e_open_channel(struct mlx5e_priv *priv, int ix,
			      struct mlx5e_params *params,
			      struct mlx5e_channel_param *cparam,
			      struct xsk_buff_pool *xsk_pool,
			      struct mlx5e_channel **cp)
{
	int cpu = mlx5_comp_vector_get_cpu(priv->mdev, ix);
	struct net_device *netdev = priv->netdev;
	struct mlx5e_xsk_param xsk;
	struct mlx5e_channel *c;
	unsigned int irq;
	int err;

	err = mlx5_comp_irqn_get(priv->mdev, ix, &irq);
	if (err)
		return err;

	err = mlx5e_channel_stats_alloc(priv, ix, cpu);
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
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.hw_objs.mkey);
	c->num_tc   = mlx5e_get_dcb_num_tc(params);
	c->xdp      = !!params->xdp_prog;
	c->stats    = &priv->channel_stats[ix]->ch;
	c->aff_mask = irq_get_effective_affinity_mask(irq);
	c->lag_port = mlx5e_enumerate_lag_port(priv->mdev, ix);

	netif_napi_add(netdev, &c->napi, mlx5e_napi_poll);

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

	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_activate_xsk(c);
	else
		mlx5e_activate_rq(&c->rq);
}

static void mlx5e_deactivate_channel(struct mlx5e_channel *c)
{
	int tc;

	if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		mlx5e_deactivate_xsk(c);
	else
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

	err = mlx5e_build_channel_param(priv->mdev, &chs->params, priv->q_counter, cparam);
	if (err)
		goto err_free;

	for (i = 0; i < chs->num; i++) {
		struct xsk_buff_pool *xsk_pool = NULL;

		if (chs->params.xdp_prog)
			xsk_pool = mlx5e_xsk_get_pool(&chs->params, chs->params.xsk, i);

		err = mlx5e_open_channel(priv, i, &chs->params, cparam, xsk_pool, &chs->c[i]);
		if (err)
			goto err_close_channels;
	}

	if (MLX5E_GET_PFLAG(&chs->params, MLX5E_PFLAG_TX_PORT_TS) || chs->params.ptp_rx) {
		err = mlx5e_ptp_open(priv, &chs->params, chs->c[0]->lag_port, &chs->ptp);
		if (err)
			goto err_close_channels;
	}

	if (priv->htb) {
		err = mlx5e_qos_open_queues(priv, chs);
		if (err)
			goto err_close_ptp;
	}

	mlx5e_health_channels_update(priv);
	kvfree(cparam);
	return 0;

err_close_ptp:
	if (chs->ptp)
		mlx5e_ptp_close(chs->ptp);

err_close_channels:
	for (i--; i >= 0; i--)
		mlx5e_close_channel(chs->c[i]);

err_free:
	kfree(chs->c);
	kvfree(cparam);
	chs->num = 0;
	return err;
}

static void mlx5e_activate_channels(struct mlx5e_priv *priv, struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++)
		mlx5e_activate_channel(chs->c[i]);

	if (priv->htb)
		mlx5e_qos_activate_queues(priv);

	for (i = 0; i < chs->num; i++)
		mlx5e_trigger_napi_icosq(chs->c[i]);

	if (chs->ptp)
		mlx5e_ptp_activate_channel(chs->ptp);
}

static int mlx5e_wait_channels_min_rx_wqes(struct mlx5e_channels *chs)
{
	int err = 0;
	int i;

	for (i = 0; i < chs->num; i++) {
		int timeout = err ? 0 : MLX5E_RQ_WQES_TIMEOUT;
		struct mlx5e_channel *c = chs->c[i];

		if (test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
			continue;

		err |= mlx5e_wait_for_min_rx_wqes(&c->rq, timeout);

		/* Don't wait on the XSK RQ, because the newer xdpsock sample
		 * doesn't provide any Fill Ring entries at the setup stage.
		 */
	}

	return err ? -ETIMEDOUT : 0;
}

static void mlx5e_deactivate_channels(struct mlx5e_channels *chs)
{
	int i;

	if (chs->ptp)
		mlx5e_ptp_deactivate_channel(chs->ptp);

	for (i = 0; i < chs->num; i++)
		mlx5e_deactivate_channel(chs->c[i]);
}

void mlx5e_close_channels(struct mlx5e_channels *chs)
{
	int i;

	if (chs->ptp) {
		mlx5e_ptp_close(chs->ptp);
		chs->ptp = NULL;
	}
	for (i = 0; i < chs->num; i++)
		mlx5e_close_channel(chs->c[i]);

	kfree(chs->c);
	chs->num = 0;
}

static int mlx5e_modify_tirs_packet_merge(struct mlx5e_priv *priv)
{
	struct mlx5e_rx_res *res = priv->rx_res;

	return mlx5e_rx_res_packet_merge_set_param(res, &priv->channels.params.packet_merge);
}

static MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_modify_tirs_packet_merge);

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

static int mlx5e_netdev_set_tcs(struct net_device *netdev, u16 nch, u8 ntc,
				struct netdev_tc_txq *tc_to_txq)
{
	int tc, err;

	netdev_reset_tc(netdev);

	if (ntc == 1)
		return 0;

	err = netdev_set_num_tc(netdev, ntc);
	if (err) {
		netdev_WARN(netdev, "netdev_set_num_tc failed (%d), ntc = %d\n", err, ntc);
		return err;
	}

	for (tc = 0; tc < ntc; tc++) {
		u16 count, offset;

		count = tc_to_txq[tc].count;
		offset = tc_to_txq[tc].offset;
		netdev_set_tc_queue(netdev, tc, count, offset);
	}

	return 0;
}

int mlx5e_update_tx_netdev_queues(struct mlx5e_priv *priv)
{
	int nch, ntc, num_txqs, err;
	int qos_queues = 0;

	if (priv->htb)
		qos_queues = mlx5e_htb_cur_leaf_nodes(priv->htb);

	nch = priv->channels.params.num_channels;
	ntc = mlx5e_get_dcb_num_tc(&priv->channels.params);
	num_txqs = nch * ntc + qos_queues;
	if (MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_TX_PORT_TS))
		num_txqs += ntc;

	netdev_dbg(priv->netdev, "Setting num_txqs %d\n", num_txqs);
	err = netif_set_real_num_tx_queues(priv->netdev, num_txqs);
	if (err)
		netdev_warn(priv->netdev, "netif_set_real_num_tx_queues failed, %d\n", err);

	return err;
}

static int mlx5e_update_netdev_queues(struct mlx5e_priv *priv)
{
	struct netdev_tc_txq old_tc_to_txq[TC_MAX_QUEUE], *tc_to_txq;
	struct net_device *netdev = priv->netdev;
	int old_num_txqs, old_ntc;
	int nch, ntc;
	int err;
	int i;

	old_num_txqs = netdev->real_num_tx_queues;
	old_ntc = netdev->num_tc ? : 1;
	for (i = 0; i < ARRAY_SIZE(old_tc_to_txq); i++)
		old_tc_to_txq[i] = netdev->tc_to_txq[i];

	nch = priv->channels.params.num_channels;
	ntc = priv->channels.params.mqprio.num_tc;
	tc_to_txq = priv->channels.params.mqprio.tc_to_txq;

	err = mlx5e_netdev_set_tcs(netdev, nch, ntc, tc_to_txq);
	if (err)
		goto err_out;
	err = mlx5e_update_tx_netdev_queues(priv);
	if (err)
		goto err_tcs;
	err = netif_set_real_num_rx_queues(netdev, nch);
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
	WARN_ON_ONCE(mlx5e_netdev_set_tcs(netdev, old_num_txqs / old_ntc, old_ntc,
					  old_tc_to_txq));
err_out:
	return err;
}

static MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_update_netdev_queues);

static void mlx5e_set_default_xps_cpumasks(struct mlx5e_priv *priv,
					   struct mlx5e_params *params)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int num_comp_vectors, ix, irq;

	num_comp_vectors = mlx5_comp_vectors_max(mdev);

	for (ix = 0; ix < params->num_channels; ix++) {
		cpumask_clear(priv->scratchpad.cpumask);

		for (irq = ix; irq < num_comp_vectors; irq += params->num_channels) {
			int cpu = mlx5_comp_vector_get_cpu(mdev, irq);

			cpumask_set_cpu(cpu, priv->scratchpad.cpumask);
		}

		netif_set_xps_queue(priv->netdev, priv->scratchpad.cpumask, ix);
	}
}

static int mlx5e_num_channels_changed(struct mlx5e_priv *priv)
{
	u16 count = priv->channels.params.num_channels;
	int err;

	err = mlx5e_update_netdev_queues(priv);
	if (err)
		return err;

	mlx5e_set_default_xps_cpumasks(priv, &priv->channels.params);

	/* This function may be called on attach, before priv->rx_res is created. */
	if (!netif_is_rxfh_configured(priv->netdev) && priv->rx_res)
		mlx5e_rx_res_rss_set_indir_uniform(priv->rx_res, count);

	return 0;
}

MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(mlx5e_num_channels_changed);

static void mlx5e_build_txq_maps(struct mlx5e_priv *priv)
{
	int i, ch, tc, num_tc;

	ch = priv->channels.num;
	num_tc = mlx5e_get_dcb_num_tc(&priv->channels.params);

	for (i = 0; i < ch; i++) {
		for (tc = 0; tc < num_tc; tc++) {
			struct mlx5e_channel *c = priv->channels.c[i];
			struct mlx5e_txqsq *sq = &c->sq[tc];

			priv->txq2sq[sq->txq_ix] = sq;
		}
	}

	if (!priv->channels.ptp)
		goto out;

	if (!test_bit(MLX5E_PTP_STATE_TX, priv->channels.ptp->state))
		goto out;

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_ptp *c = priv->channels.ptp;
		struct mlx5e_txqsq *sq = &c->ptpsq[tc].txqsq;

		priv->txq2sq[sq->txq_ix] = sq;
	}

out:
	/* Make the change to txq2sq visible before the queue is started.
	 * As mlx5e_xmit runs under a spinlock, there is an implicit ACQUIRE,
	 * which pairs with this barrier.
	 */
	smp_wmb();
}

void mlx5e_activate_priv_channels(struct mlx5e_priv *priv)
{
	mlx5e_build_txq_maps(priv);
	mlx5e_activate_channels(priv, &priv->channels);
	mlx5e_xdp_tx_enable(priv);

	/* dev_watchdog() wants all TX queues to be started when the carrier is
	 * OK, including the ones in range real_num_tx_queues..num_tx_queues-1.
	 * Make it happy to avoid TX timeout false alarms.
	 */
	netif_tx_start_all_queues(priv->netdev);

	if (mlx5e_is_vport_rep(priv))
		mlx5e_rep_activate_channels(priv);

	mlx5e_wait_channels_min_rx_wqes(&priv->channels);

	if (priv->rx_res)
		mlx5e_rx_res_channels_activate(priv->rx_res, &priv->channels);
}

void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv)
{
	if (priv->rx_res)
		mlx5e_rx_res_channels_deactivate(priv->rx_res);

	if (mlx5e_is_vport_rep(priv))
		mlx5e_rep_deactivate_channels(priv);

	/* The results of ndo_select_queue are unreliable, while netdev config
	 * is being changed (real_num_tx_queues, num_tc). Stop all queues to
	 * prevent ndo_start_xmit from being called, so that it can assume that
	 * the selected queue is always valid.
	 */
	netif_tx_disable(priv->netdev);

	mlx5e_xdp_tx_disable(priv);
	mlx5e_deactivate_channels(&priv->channels);
}

static int mlx5e_switch_priv_params(struct mlx5e_priv *priv,
				    struct mlx5e_params *new_params,
				    mlx5e_fp_preactivate preactivate,
				    void *context)
{
	struct mlx5e_params old_params;

	old_params = priv->channels.params;
	priv->channels.params = *new_params;

	if (preactivate) {
		int err;

		err = preactivate(priv, context);
		if (err) {
			priv->channels.params = old_params;
			return err;
		}
	}

	return 0;
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

	mlx5e_selq_apply(&priv->selq);
out:
	mlx5e_activate_priv_channels(priv);

	/* return carrier back if needed */
	if (carrier_ok)
		netif_carrier_on(netdev);

	return err;
}

int mlx5e_safe_switch_params(struct mlx5e_priv *priv,
			     struct mlx5e_params *params,
			     mlx5e_fp_preactivate preactivate,
			     void *context, bool reset)
{
	struct mlx5e_channels *new_chs;
	int err;

	reset &= test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (!reset)
		return mlx5e_switch_priv_params(priv, params, preactivate, context);

	new_chs = kzalloc(sizeof(*new_chs), GFP_KERNEL);
	if (!new_chs)
		return -ENOMEM;
	new_chs->params = *params;

	mlx5e_selq_prepare_params(&priv->selq, &new_chs->params);

	err = mlx5e_open_channels(priv, new_chs);
	if (err)
		goto err_cancel_selq;

	err = mlx5e_switch_priv_channels(priv, new_chs, preactivate, context);
	if (err)
		goto err_close;

	kfree(new_chs);
	return 0;

err_close:
	mlx5e_close_channels(new_chs);

err_cancel_selq:
	mlx5e_selq_cancel(&priv->selq);
	kfree(new_chs);
	return err;
}

int mlx5e_safe_reopen_channels(struct mlx5e_priv *priv)
{
	return mlx5e_safe_switch_params(priv, &priv->channels.params, NULL, NULL, true);
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

	mlx5e_selq_prepare_params(&priv->selq, &priv->channels.params);

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	err = mlx5e_open_channels(priv, &priv->channels);
	if (err)
		goto err_clear_state_opened_flag;

	err = priv->profile->update_rx(priv);
	if (err)
		goto err_close_channels;

	mlx5e_selq_apply(&priv->selq);
	mlx5e_activate_priv_channels(priv);
	mlx5e_apply_traps(priv, true);
	if (priv->profile->update_carrier)
		priv->profile->update_carrier(priv);

	mlx5e_queue_update_stats(priv);
	return 0;

err_close_channels:
	mlx5e_close_channels(&priv->channels);
err_clear_state_opened_flag:
	clear_bit(MLX5E_STATE_OPENED, &priv->state);
	mlx5e_selq_cancel(&priv->selq);
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

	mlx5e_build_drop_rq_param(mdev, priv->drop_rq_q_counter, &rq_param);

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

	MLX5_SET(tisc, tisc, transport_domain, mdev->mlx5e_res.hw_objs.td.tdn);

	if (MLX5_GET(tisc, tisc, tls_en))
		MLX5_SET(tisc, tisc, pd, mdev->mlx5e_res.hw_objs.pdn);

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
	if (priv->mqprio_rl) {
		mlx5e_mqprio_rl_cleanup(priv->mqprio_rl);
		mlx5e_mqprio_rl_free(priv->mqprio_rl);
		priv->mqprio_rl = NULL;
	}
	mlx5e_accel_cleanup_tx(priv);
	mlx5e_destroy_tises(priv);
}

static int mlx5e_modify_channels_vsd(struct mlx5e_channels *chs, bool vsd)
{
	int err;
	int i;

	for (i = 0; i < chs->num; i++) {
		err = mlx5e_modify_rq_vsd(&chs->c[i]->rq, vsd);
		if (err)
			return err;
	}
	if (chs->ptp && test_bit(MLX5E_PTP_STATE_RX, chs->ptp->state))
		return mlx5e_modify_rq_vsd(&chs->ptp->rq, vsd);

	return 0;
}

static void mlx5e_mqprio_build_default_tc_to_txq(struct netdev_tc_txq *tc_to_txq,
						 int ntc, int nch)
{
	int tc;

	memset(tc_to_txq, 0, sizeof(*tc_to_txq) * TC_MAX_QUEUE);

	/* Map netdev TCs to offset 0.
	 * We have our own UP to TXQ mapping for DCB mode of QoS
	 */
	for (tc = 0; tc < ntc; tc++) {
		tc_to_txq[tc] = (struct netdev_tc_txq) {
			.count = nch,
			.offset = 0,
		};
	}
}

static void mlx5e_mqprio_build_tc_to_txq(struct netdev_tc_txq *tc_to_txq,
					 struct tc_mqprio_qopt *qopt)
{
	int tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++) {
		tc_to_txq[tc] = (struct netdev_tc_txq) {
			.count = qopt->count[tc],
			.offset = qopt->offset[tc],
		};
	}
}

static void mlx5e_params_mqprio_dcb_set(struct mlx5e_params *params, u8 num_tc)
{
	params->mqprio.mode = TC_MQPRIO_MODE_DCB;
	params->mqprio.num_tc = num_tc;
	mlx5e_mqprio_build_default_tc_to_txq(params->mqprio.tc_to_txq, num_tc,
					     params->num_channels);
}

static void mlx5e_mqprio_rl_update_params(struct mlx5e_params *params,
					  struct mlx5e_mqprio_rl *rl)
{
	int tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++) {
		u32 hw_id = 0;

		if (rl)
			mlx5e_mqprio_rl_get_node_hw_id(rl, tc, &hw_id);
		params->mqprio.channel.hw_id[tc] = hw_id;
	}
}

static void mlx5e_params_mqprio_channel_set(struct mlx5e_params *params,
					    struct tc_mqprio_qopt_offload *mqprio,
					    struct mlx5e_mqprio_rl *rl)
{
	int tc;

	params->mqprio.mode = TC_MQPRIO_MODE_CHANNEL;
	params->mqprio.num_tc = mqprio->qopt.num_tc;

	for (tc = 0; tc < TC_MAX_QUEUE; tc++)
		params->mqprio.channel.max_rate[tc] = mqprio->max_rate[tc];

	mlx5e_mqprio_rl_update_params(params, rl);
	mlx5e_mqprio_build_tc_to_txq(params->mqprio.tc_to_txq, &mqprio->qopt);
}

static void mlx5e_params_mqprio_reset(struct mlx5e_params *params)
{
	mlx5e_params_mqprio_dcb_set(params, 1);
}

static int mlx5e_setup_tc_mqprio_dcb(struct mlx5e_priv *priv,
				     struct tc_mqprio_qopt *mqprio)
{
	struct mlx5e_params new_params;
	u8 tc = mqprio->num_tc;
	int err;

	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (tc && tc != MLX5E_MAX_NUM_TC)
		return -EINVAL;

	new_params = priv->channels.params;
	mlx5e_params_mqprio_dcb_set(&new_params, tc ? tc : 1);

	err = mlx5e_safe_switch_params(priv, &new_params,
				       mlx5e_num_channels_changed_ctx, NULL, true);

	if (!err && priv->mqprio_rl) {
		mlx5e_mqprio_rl_cleanup(priv->mqprio_rl);
		mlx5e_mqprio_rl_free(priv->mqprio_rl);
		priv->mqprio_rl = NULL;
	}

	priv->max_opened_tc = max_t(u8, priv->max_opened_tc,
				    mlx5e_get_dcb_num_tc(&priv->channels.params));
	return err;
}

static int mlx5e_mqprio_channel_validate(struct mlx5e_priv *priv,
					 struct tc_mqprio_qopt_offload *mqprio)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5e_ptp *ptp_channel;
	int agg_count = 0;
	int i;

	ptp_channel = priv->channels.ptp;
	if (ptp_channel && test_bit(MLX5E_PTP_STATE_TX, ptp_channel->state)) {
		netdev_err(netdev,
			   "Cannot activate MQPRIO mode channel since it conflicts with TX port TS\n");
		return -EINVAL;
	}

	if (mqprio->qopt.offset[0] != 0 || mqprio->qopt.num_tc < 1 ||
	    mqprio->qopt.num_tc > MLX5E_MAX_NUM_MQPRIO_CH_TC)
		return -EINVAL;

	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		if (!mqprio->qopt.count[i]) {
			netdev_err(netdev, "Zero size for queue-group (%d) is not supported\n", i);
			return -EINVAL;
		}
		if (mqprio->min_rate[i]) {
			netdev_err(netdev, "Min tx rate is not supported\n");
			return -EINVAL;
		}

		if (mqprio->max_rate[i]) {
			int err;

			err = mlx5e_qos_bytes_rate_check(priv->mdev, mqprio->max_rate[i]);
			if (err)
				return err;
		}

		if (mqprio->qopt.offset[i] != agg_count) {
			netdev_err(netdev, "Discontinuous queues config is not supported\n");
			return -EINVAL;
		}
		agg_count += mqprio->qopt.count[i];
	}

	if (priv->channels.params.num_channels != agg_count) {
		netdev_err(netdev, "Num of queues (%d) does not match available (%d)\n",
			   agg_count, priv->channels.params.num_channels);
		return -EINVAL;
	}

	return 0;
}

static bool mlx5e_mqprio_rate_limit(u8 num_tc, u64 max_rate[])
{
	int tc;

	for (tc = 0; tc < num_tc; tc++)
		if (max_rate[tc])
			return true;
	return false;
}

static struct mlx5e_mqprio_rl *mlx5e_mqprio_rl_create(struct mlx5_core_dev *mdev,
						      u8 num_tc, u64 max_rate[])
{
	struct mlx5e_mqprio_rl *rl;
	int err;

	if (!mlx5e_mqprio_rate_limit(num_tc, max_rate))
		return NULL;

	rl = mlx5e_mqprio_rl_alloc();
	if (!rl)
		return ERR_PTR(-ENOMEM);

	err = mlx5e_mqprio_rl_init(rl, mdev, num_tc, max_rate);
	if (err) {
		mlx5e_mqprio_rl_free(rl);
		return ERR_PTR(err);
	}

	return rl;
}

static int mlx5e_setup_tc_mqprio_channel(struct mlx5e_priv *priv,
					 struct tc_mqprio_qopt_offload *mqprio)
{
	mlx5e_fp_preactivate preactivate;
	struct mlx5e_params new_params;
	struct mlx5e_mqprio_rl *rl;
	bool nch_changed;
	int err;

	err = mlx5e_mqprio_channel_validate(priv, mqprio);
	if (err)
		return err;

	rl = mlx5e_mqprio_rl_create(priv->mdev, mqprio->qopt.num_tc, mqprio->max_rate);
	if (IS_ERR(rl))
		return PTR_ERR(rl);

	new_params = priv->channels.params;
	mlx5e_params_mqprio_channel_set(&new_params, mqprio, rl);

	nch_changed = mlx5e_get_dcb_num_tc(&priv->channels.params) > 1;
	preactivate = nch_changed ? mlx5e_num_channels_changed_ctx :
		mlx5e_update_netdev_queues_ctx;
	err = mlx5e_safe_switch_params(priv, &new_params, preactivate, NULL, true);
	if (err) {
		if (rl) {
			mlx5e_mqprio_rl_cleanup(rl);
			mlx5e_mqprio_rl_free(rl);
		}
		return err;
	}

	if (priv->mqprio_rl) {
		mlx5e_mqprio_rl_cleanup(priv->mqprio_rl);
		mlx5e_mqprio_rl_free(priv->mqprio_rl);
	}
	priv->mqprio_rl = rl;

	return 0;
}

static int mlx5e_setup_tc_mqprio(struct mlx5e_priv *priv,
				 struct tc_mqprio_qopt_offload *mqprio)
{
	/* MQPRIO is another toplevel qdisc that can't be attached
	 * simultaneously with the offloaded HTB.
	 */
	if (WARN_ON(mlx5e_selq_is_htb_enabled(&priv->selq)))
		return -EINVAL;

	switch (mqprio->mode) {
	case TC_MQPRIO_MODE_DCB:
		return mlx5e_setup_tc_mqprio_dcb(priv, &mqprio->qopt);
	case TC_MQPRIO_MODE_CHANNEL:
		return mlx5e_setup_tc_mqprio_channel(priv, mqprio);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(mlx5e_block_cb_list);

static int mlx5e_setup_tc(struct net_device *dev, enum tc_setup_type type,
			  void *type_data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	bool tc_unbind = false;
	int err;

	if (type == TC_SETUP_BLOCK &&
	    ((struct flow_block_offload *)type_data)->command == FLOW_BLOCK_UNBIND)
		tc_unbind = true;

	if (!netif_device_present(dev) && !tc_unbind)
		return -ENODEV;

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
		mutex_lock(&priv->state_lock);
		err = mlx5e_setup_tc_mqprio(priv, type_data);
		mutex_unlock(&priv->state_lock);
		return err;
	case TC_SETUP_QDISC_HTB:
		mutex_lock(&priv->state_lock);
		err = mlx5e_htb_setup_tc(priv, type_data);
		mutex_unlock(&priv->state_lock);
		return err;
	default:
		return -EOPNOTSUPP;
	}
}

void mlx5e_fold_sw_stats64(struct mlx5e_priv *priv, struct rtnl_link_stats64 *s)
{
	int i;

	for (i = 0; i < priv->stats_nch; i++) {
		struct mlx5e_channel_stats *channel_stats = priv->channel_stats[i];
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
	if (priv->tx_ptp_opened) {
		for (i = 0; i < priv->max_opened_tc; i++) {
			struct mlx5e_sq_stats *sq_stats = &priv->ptp_stats.sq[i];

			s->tx_packets    += sq_stats->packets;
			s->tx_bytes      += sq_stats->bytes;
			s->tx_dropped    += sq_stats->dropped;
		}
	}
	if (priv->rx_ptp_opened) {
		struct mlx5e_rq_stats *rq_stats = &priv->ptp_stats.rq;

		s->rx_packets   += rq_stats->packets;
		s->rx_bytes     += rq_stats->bytes;
		s->multicast    += rq_stats->mcast_packets;
	}
}

void
mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;

	if (!netif_device_present(dev))
		return;

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
		PPORT_802_3_GET(pstats, a_frame_too_long_errors) +
		VNIC_ENV_GET(&priv->stats.vnic, eth_wqe_too_small);
	stats->rx_crc_errors =
		PPORT_802_3_GET(pstats, a_frame_check_sequence_errors);
	stats->rx_frame_errors = PPORT_802_3_GET(pstats, a_alignment_errors);
	stats->tx_aborted_errors = PPORT_2863_GET(pstats, if_out_discards);
	stats->rx_errors = stats->rx_length_errors + stats->rx_crc_errors +
			   stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_carrier_errors;
}

static void mlx5e_nic_set_rx_mode(struct mlx5e_priv *priv)
{
	if (mlx5e_is_uplink_rep(priv))
		return; /* no rx mode for uplink rep */

	queue_work(priv->wq, &priv->set_rx_mode_work);
}

static void mlx5e_set_rx_mode(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_nic_set_rx_mode(priv);
}

static int mlx5e_set_mac(struct net_device *netdev, void *addr)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	netif_addr_lock_bh(netdev);
	eth_hw_addr_set(netdev, saddr->sa_data);
	netif_addr_unlock_bh(netdev);

	mlx5e_nic_set_rx_mode(priv);

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
	struct mlx5e_params *cur_params;
	struct mlx5e_params new_params;
	bool reset = true;
	int err = 0;

	mutex_lock(&priv->state_lock);

	cur_params = &priv->channels.params;
	new_params = *cur_params;

	if (enable)
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_LRO;
	else if (new_params.packet_merge.type == MLX5E_PACKET_MERGE_LRO)
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_NONE;
	else
		goto out;

	if (!(cur_params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO &&
	      new_params.packet_merge.type == MLX5E_PACKET_MERGE_LRO)) {
		if (cur_params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ) {
			if (mlx5e_rx_mpwqe_is_linear_skb(mdev, cur_params, NULL) ==
			    mlx5e_rx_mpwqe_is_linear_skb(mdev, &new_params, NULL))
				reset = false;
		}
	}

	err = mlx5e_safe_switch_params(priv, &new_params,
				       mlx5e_modify_tirs_packet_merge_ctx, NULL, reset);
out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int set_feature_hw_gro(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params new_params;
	bool reset = true;
	int err = 0;

	mutex_lock(&priv->state_lock);
	new_params = priv->channels.params;

	if (enable) {
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_SHAMPO;
		new_params.packet_merge.shampo.match_criteria_type =
			MLX5_RQC_SHAMPO_MATCH_CRITERIA_TYPE_EXTENDED;
		new_params.packet_merge.shampo.alignment_granularity =
			MLX5_RQC_SHAMPO_NO_MATCH_ALIGNMENT_GRANULARITY_STRIDE;
	} else if (new_params.packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO) {
		new_params.packet_merge.type = MLX5E_PACKET_MERGE_NONE;
	} else {
		goto out;
	}

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, reset);
out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int set_feature_cvlan_filter(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (enable)
		mlx5e_enable_cvlan_filter(priv->fs,
					  !!(priv->netdev->flags & IFF_PROMISC));
	else
		mlx5e_disable_cvlan_filter(priv->fs,
					   !!(priv->netdev->flags & IFF_PROMISC));

	return 0;
}

static int set_feature_hw_tc(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
	int tc_flag = mlx5e_is_uplink_rep(priv) ? MLX5_TC_FLAG(ESW_OFFLOAD) :
						  MLX5_TC_FLAG(NIC_OFFLOAD);
	if (!enable && mlx5e_tc_num_filters(priv, tc_flag)) {
		netdev_err(netdev,
			   "Active offloaded tc filters, can't turn hw_tc_offload off\n");
		return -EINVAL;
	}
#endif

	mutex_lock(&priv->state_lock);
	if (!enable && mlx5e_selq_is_htb_enabled(&priv->selq)) {
		netdev_err(netdev, "Active HTB offload, can't turn hw_tc_offload off\n");
		err = -EINVAL;
	}
	mutex_unlock(&priv->state_lock);

	return err;
}

static int set_feature_rx_all(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	return mlx5_set_port_fcs(mdev, !enable);
}

static int mlx5e_set_rx_port_ts(struct mlx5_core_dev *mdev, bool enable)
{
	u32 in[MLX5_ST_SZ_DW(pcmr_reg)] = {};
	bool supported, curr_state;
	int err;

	if (!MLX5_CAP_GEN(mdev, ports_check))
		return 0;

	err = mlx5_query_ports_check(mdev, in, sizeof(in));
	if (err)
		return err;

	supported = MLX5_GET(pcmr_reg, in, rx_ts_over_crc_cap);
	curr_state = MLX5_GET(pcmr_reg, in, rx_ts_over_crc);

	if (!supported || enable == curr_state)
		return 0;

	MLX5_SET(pcmr_reg, in, local_port, 1);
	MLX5_SET(pcmr_reg, in, rx_ts_over_crc, enable);

	return mlx5_set_ports_check(mdev, in, sizeof(in));
}

static int mlx5e_set_rx_port_ts_wrap(struct mlx5e_priv *priv, void *ctx)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	bool enable = *(bool *)ctx;

	return mlx5e_set_rx_port_ts(mdev, enable);
}

static int set_feature_rx_fcs(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_channels *chs = &priv->channels;
	struct mlx5e_params new_params;
	int err;
	bool rx_ts_over_crc = !enable;

	mutex_lock(&priv->state_lock);

	new_params = chs->params;
	new_params.scatter_fcs_en = enable;
	err = mlx5e_safe_switch_params(priv, &new_params, mlx5e_set_rx_port_ts_wrap,
				       &rx_ts_over_crc, true);
	mutex_unlock(&priv->state_lock);
	return err;
}

static int set_feature_rx_vlan(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err = 0;

	mutex_lock(&priv->state_lock);

	mlx5e_fs_set_vlan_strip_disable(priv->fs, !enable);
	priv->channels.params.vlan_strip_disable = !enable;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	err = mlx5e_modify_channels_vsd(&priv->channels, !enable);
	if (err) {
		mlx5e_fs_set_vlan_strip_disable(priv->fs, enable);
		priv->channels.params.vlan_strip_disable = enable;
	}
unlock:
	mutex_unlock(&priv->state_lock);

	return err;
}

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_flow_steering *fs = priv->fs;

	if (mlx5e_is_uplink_rep(priv))
		return 0; /* no vlan table for uplink rep */

	return mlx5e_fs_vlan_rx_add_vid(fs, dev, proto, vid);
}

int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_flow_steering *fs = priv->fs;

	if (mlx5e_is_uplink_rep(priv))
		return 0; /* no vlan table for uplink rep */

	return mlx5e_fs_vlan_rx_kill_vid(fs, dev, proto, vid);
}

#ifdef CONFIG_MLX5_EN_ARFS
static int set_feature_arfs(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	if (enable)
		err = mlx5e_arfs_enable(priv->fs);
	else
		err = mlx5e_arfs_disable(priv->fs);

	return err;
}
#endif

static int mlx5e_handle_feature(struct net_device *netdev,
				netdev_features_t *features,
				netdev_features_t feature,
				mlx5e_feature_handler feature_handler)
{
	netdev_features_t changes = *features ^ netdev->features;
	bool enable = !!(*features & feature);
	int err;

	if (!(changes & feature))
		return 0;

	err = feature_handler(netdev, enable);
	if (err) {
		MLX5E_SET_FEATURE(features, feature, !enable);
		netdev_err(netdev, "%s feature %pNF failed, err %d\n",
			   enable ? "Enable" : "Disable", &feature, err);
		return err;
	}

	return 0;
}

void mlx5e_set_xdp_feature(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params *params = &priv->channels.params;
	xdp_features_t val;

	if (params->packet_merge.type != MLX5E_PACKET_MERGE_NONE) {
		xdp_clear_features_flag(netdev);
		return;
	}

	val = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
	      NETDEV_XDP_ACT_XSK_ZEROCOPY |
	      NETDEV_XDP_ACT_RX_SG |
	      NETDEV_XDP_ACT_NDO_XMIT |
	      NETDEV_XDP_ACT_NDO_XMIT_SG;
	xdp_set_features_flag(netdev, val);
}

int mlx5e_set_features(struct net_device *netdev, netdev_features_t features)
{
	netdev_features_t oper_features = features;
	int err = 0;

#define MLX5E_HANDLE_FEATURE(feature, handler) \
	mlx5e_handle_feature(netdev, &oper_features, feature, handler)

	err |= MLX5E_HANDLE_FEATURE(NETIF_F_LRO, set_feature_lro);
	err |= MLX5E_HANDLE_FEATURE(NETIF_F_GRO_HW, set_feature_hw_gro);
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

	/* update XDP supported features */
	mlx5e_set_xdp_feature(netdev);

	return 0;
}

static netdev_features_t mlx5e_fix_uplink_rep_features(struct net_device *netdev,
						       netdev_features_t features)
{
	features &= ~NETIF_F_HW_TLS_RX;
	if (netdev->features & NETIF_F_HW_TLS_RX)
		netdev_warn(netdev, "Disabling hw_tls_rx, not supported in switchdev mode\n");

	features &= ~NETIF_F_HW_TLS_TX;
	if (netdev->features & NETIF_F_HW_TLS_TX)
		netdev_warn(netdev, "Disabling hw_tls_tx, not supported in switchdev mode\n");

	features &= ~NETIF_F_NTUPLE;
	if (netdev->features & NETIF_F_NTUPLE)
		netdev_warn(netdev, "Disabling ntuple, not supported in switchdev mode\n");

	features &= ~NETIF_F_GRO_HW;
	if (netdev->features & NETIF_F_GRO_HW)
		netdev_warn(netdev, "Disabling HW_GRO, not supported in switchdev mode\n");

	features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
	if (netdev->features & NETIF_F_HW_VLAN_CTAG_FILTER)
		netdev_warn(netdev, "Disabling HW_VLAN CTAG FILTERING, not supported in switchdev mode\n");

	return features;
}

static netdev_features_t mlx5e_fix_features(struct net_device *netdev,
					    netdev_features_t features)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_vlan_table *vlan;
	struct mlx5e_params *params;

	if (!netif_device_present(netdev))
		return features;

	vlan = mlx5e_fs_get_vlan(priv->fs);
	mutex_lock(&priv->state_lock);
	params = &priv->channels.params;
	if (!vlan ||
	    !bitmap_empty(mlx5e_vlan_get_active_svlans(vlan), VLAN_N_VID)) {
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
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "Disabling HW-GRO, not supported in legacy RQ\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	if (params->xdp_prog) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "LRO is incompatible with XDP\n");
			features &= ~NETIF_F_LRO;
		}
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "HW GRO is incompatible with XDP\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	if (priv->xsk.refcnt) {
		if (features & NETIF_F_LRO) {
			netdev_warn(netdev, "LRO is incompatible with AF_XDP (%u XSKs are active)\n",
				    priv->xsk.refcnt);
			features &= ~NETIF_F_LRO;
		}
		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "HW GRO is incompatible with AF_XDP (%u XSKs are active)\n",
				    priv->xsk.refcnt);
			features &= ~NETIF_F_GRO_HW;
		}
	}

	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		features &= ~NETIF_F_RXHASH;
		if (netdev->features & NETIF_F_RXHASH)
			netdev_warn(netdev, "Disabling rxhash, not supported when CQE compress is active\n");

		if (features & NETIF_F_GRO_HW) {
			netdev_warn(netdev, "Disabling HW-GRO, not supported when CQE compress is active\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	if (mlx5e_is_uplink_rep(priv)) {
		features = mlx5e_fix_uplink_rep_features(netdev, features);
		features |= NETIF_F_NETNS_LOCAL;
	} else {
		features &= ~NETIF_F_NETNS_LOCAL;
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
		int max_xdp_mtu;

		if (!xsk_pool)
			continue;

		mlx5e_build_xsk_param(xsk_pool, &xsk);
		max_xdp_mtu = mlx5e_xdp_max_mtu(new_params, &xsk);

		/* Validate XSK params and XDP MTU in advance */
		if (!mlx5e_validate_xsk_param(new_params, &xsk, mdev) ||
		    new_params->sw_mtu > max_xdp_mtu) {
			u32 hr = mlx5e_get_linear_rq_headroom(new_params, &xsk);
			int max_mtu_frame, max_mtu_page, max_mtu;

			/* Two criteria must be met:
			 * 1. HW MTU + all headrooms <= XSK frame size.
			 * 2. Size of SKBs allocated on XDP_PASS <= PAGE_SIZE.
			 */
			max_mtu_frame = MLX5E_HW2SW_MTU(new_params, xsk.chunk_size - hr);
			max_mtu_page = MLX5E_HW2SW_MTU(new_params, SKB_MAX_HEAD(0));
			max_mtu = min3(max_mtu_frame, max_mtu_page, max_xdp_mtu);

			netdev_err(netdev, "MTU %d is too big for an XSK running on channel %u or its redirection XDP program. Try MTU <= %d\n",
				   new_params->sw_mtu, ix, max_mtu);
			return false;
		}
	}

	return true;
}

static bool mlx5e_params_validate_xdp(struct net_device *netdev,
				      struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params)
{
	bool is_linear;

	/* No XSK params: AF_XDP can't be enabled yet at the point of setting
	 * the XDP program.
	 */
	is_linear = params->rq_wq_type == MLX5_WQ_TYPE_CYCLIC ?
		mlx5e_rx_is_linear_skb(mdev, params, NULL) :
		mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL);

	if (!is_linear) {
		if (!params->xdp_prog->aux->xdp_has_frags) {
			netdev_warn(netdev, "MTU(%d) > %d, too big for an XDP program not aware of multi buffer\n",
				    params->sw_mtu,
				    mlx5e_xdp_max_mtu(params, NULL));
			return false;
		}
		if (params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ &&
		    !mlx5e_verify_params_rx_mpwqe_strides(mdev, params, NULL)) {
			netdev_warn(netdev, "XDP is not allowed with striding RQ and MTU(%d) > %d\n",
				    params->sw_mtu,
				    mlx5e_xdp_max_mtu(params, NULL));
			return false;
		}
	}

	return true;
}

int mlx5e_change_mtu(struct net_device *netdev, int new_mtu,
		     mlx5e_fp_preactivate preactivate)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_params new_params;
	struct mlx5e_params *params;
	bool reset = true;
	int err = 0;

	mutex_lock(&priv->state_lock);

	params = &priv->channels.params;

	new_params = *params;
	new_params.sw_mtu = new_mtu;
	err = mlx5e_validate_params(priv->mdev, &new_params);
	if (err)
		goto out;

	if (new_params.xdp_prog && !mlx5e_params_validate_xdp(netdev, priv->mdev,
							      &new_params)) {
		err = -EINVAL;
		goto out;
	}

	if (priv->xsk.refcnt &&
	    !mlx5e_xsk_validate_mtu(netdev, &priv->channels,
				    &new_params, priv->mdev)) {
		err = -EINVAL;
		goto out;
	}

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_LRO)
		reset = false;

	if (params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ &&
	    params->packet_merge.type != MLX5E_PACKET_MERGE_SHAMPO) {
		bool is_linear_old = mlx5e_rx_mpwqe_is_linear_skb(priv->mdev, params, NULL);
		bool is_linear_new = mlx5e_rx_mpwqe_is_linear_skb(priv->mdev,
								  &new_params, NULL);
		u8 sz_old = mlx5e_mpwqe_get_log_rq_size(priv->mdev, params, NULL);
		u8 sz_new = mlx5e_mpwqe_get_log_rq_size(priv->mdev, &new_params, NULL);

		/* Always reset in linear mode - hw_mtu is used in data path.
		 * Check that the mode was non-linear and didn't change.
		 * If XSK is active, XSK RQs are linear.
		 * Reset if the RQ size changed, even if it's non-linear.
		 */
		if (!is_linear_old && !is_linear_new && !priv->xsk.refcnt &&
		    sz_old == sz_new)
			reset = false;
	}

	err = mlx5e_safe_switch_params(priv, &new_params, preactivate, NULL, reset);

out:
	netdev->mtu = params->sw_mtu;
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_change_nic_mtu(struct net_device *netdev, int new_mtu)
{
	return mlx5e_change_mtu(netdev, new_mtu, mlx5e_set_dev_port_mtu_ctx);
}

int mlx5e_ptp_rx_manage_fs_ctx(struct mlx5e_priv *priv, void *ctx)
{
	bool set  = *(bool *)ctx;

	return mlx5e_ptp_rx_manage_fs(priv, set);
}

static int mlx5e_hwstamp_config_no_ptp_rx(struct mlx5e_priv *priv, bool rx_filter)
{
	bool rx_cqe_compress_def = priv->channels.params.rx_cqe_compress_def;
	int err;

	if (!rx_filter)
		/* Reset CQE compression to Admin default */
		return mlx5e_modify_rx_cqe_compression_locked(priv, rx_cqe_compress_def, false);

	if (!MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_RX_CQE_COMPRESS))
		return 0;

	/* Disable CQE compression */
	netdev_warn(priv->netdev, "Disabling RX cqe compression\n");
	err = mlx5e_modify_rx_cqe_compression_locked(priv, false, true);
	if (err)
		netdev_err(priv->netdev, "Failed disabling cqe compression err=%d\n", err);

	return err;
}

static int mlx5e_hwstamp_config_ptp_rx(struct mlx5e_priv *priv, bool ptp_rx)
{
	struct mlx5e_params new_params;

	if (ptp_rx == priv->channels.params.ptp_rx)
		return 0;

	new_params = priv->channels.params;
	new_params.ptp_rx = ptp_rx;
	return mlx5e_safe_switch_params(priv, &new_params, mlx5e_ptp_rx_manage_fs_ctx,
					&new_params.ptp_rx, true);
}

int mlx5e_hwstamp_set(struct mlx5e_priv *priv, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	bool rx_cqe_compress_def;
	bool ptp_rx;
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
	rx_cqe_compress_def = priv->channels.params.rx_cqe_compress_def;

	/* RX HW timestamp */
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ptp_rx = false;
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
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		/* ptp_rx is set if both HW TS is set and CQE
		 * compression is set
		 */
		ptp_rx = rx_cqe_compress_def;
		break;
	default:
		err = -ERANGE;
		goto err_unlock;
	}

	if (!mlx5e_profile_feature_cap(priv->profile, PTP_RX))
		err = mlx5e_hwstamp_config_no_ptp_rx(priv,
						     config.rx_filter != HWTSTAMP_FILTER_NONE);
	else
		err = mlx5e_hwstamp_config_ptp_rx(priv, ptp_rx);
	if (err)
		goto err_unlock;

	memcpy(&priv->tstamp, &config, sizeof(config));
	mutex_unlock(&priv->state_lock);

	/* might need to fix some features */
	netdev_update_features(priv->netdev);

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
err_unlock:
	mutex_unlock(&priv->state_lock);
	return err;
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

	if (mlx5e_is_uplink_rep(priv))
		return -EOPNOTSUPP;

	return mlx5_eswitch_set_vport_state(mdev->priv.eswitch, vf + 1,
					    mlx5_ifla_link2vport(link_state));
}

int mlx5e_get_vf_config(struct net_device *dev,
			int vf, struct ifla_vf_info *ivi)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (!netif_device_present(dev))
		return -EOPNOTSUPP;

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

static bool
mlx5e_has_offload_stats(const struct net_device *dev, int attr_id)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (!netif_device_present(dev))
		return false;

	if (!mlx5e_is_uplink_rep(priv))
		return false;

	return mlx5e_rep_has_offload_stats(dev, attr_id);
}

static int
mlx5e_get_offload_stats(int attr_id, const struct net_device *dev,
			void *sp)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	if (!mlx5e_is_uplink_rep(priv))
		return -EOPNOTSUPP;

	return mlx5e_rep_get_offload_stats(attr_id, dev, sp);
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
		break;
#ifdef CONFIG_MLX5_EN_IPSEC
	case IPPROTO_ESP:
		return mlx5e_ipsec_feature_check(skb, features);
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

static int mlx5e_xdp_allowed(struct net_device *netdev, struct mlx5_core_dev *mdev,
			     struct mlx5e_params *params)
{
	if (params->packet_merge.type != MLX5E_PACKET_MERGE_NONE) {
		netdev_warn(netdev, "can't set XDP while HW-GRO/LRO is on, disable them first\n");
		return -EINVAL;
	}

	if (!mlx5e_params_validate_xdp(netdev, mdev, params))
		return -EINVAL;

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
	struct mlx5e_params new_params;
	struct bpf_prog *old_prog;
	int err = 0;
	bool reset;
	int i;

	mutex_lock(&priv->state_lock);

	new_params = priv->channels.params;
	new_params.xdp_prog = prog;

	if (prog) {
		err = mlx5e_xdp_allowed(netdev, priv->mdev, &new_params);
		if (err)
			goto unlock;
	}

	/* no need for full reset when exchanging programs */
	reset = (!priv->channels.params.xdp_prog || !prog);

	old_prog = priv->channels.params.xdp_prog;

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, reset);
	if (err)
		goto unlock;

	if (old_prog)
		bpf_prog_put(old_prog);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state) || reset)
		goto unlock;

	/* exchanging programs w/o reset, we update ref counts on behalf
	 * of the channels RQs here.
	 */
	bpf_prog_add(prog, priv->channels.num);
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

	/* Need to fix some features. */
	if (!err)
		netdev_update_features(netdev);

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
	.ndo_eth_ioctl            = mlx5e_ioctl,
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
	.ndo_has_offload_stats   = mlx5e_has_offload_stats,
	.ndo_get_offload_stats   = mlx5e_get_offload_stats,
#endif
};

static u32 mlx5e_choose_lro_timeout(struct mlx5_core_dev *mdev, u32 wanted_timeout)
{
	int i;

	/* The supported periods are organized in ascending order */
	for (i = 0; i < MLX5E_LRO_TIMEOUT_ARR_SIZE - 1; i++)
		if (MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]) >= wanted_timeout)
			break;

	return MLX5_CAP_ETH(mdev, lro_timer_supported_periods[i]);
}

void mlx5e_build_nic_params(struct mlx5e_priv *priv, struct mlx5e_xsk *xsk, u16 mtu)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 rx_cq_period_mode;

	params->sw_mtu = mtu;
	params->hard_mtu = MLX5E_ETH_HARD_MTU;
	params->num_channels = min_t(unsigned int, MLX5E_MAX_NUM_CHANNELS / 2,
				     priv->max_nch);
	mlx5e_params_mqprio_reset(params);

	/* SQ */
	params->log_sq_size = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE, mlx5e_tx_mpwqe_supported(mdev));

	/* XDP SQ */
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_XDP_TX_MPWQE, mlx5e_tx_mpwqe_supported(mdev));

	/* set CQE compression */
	params->rx_cqe_compress_def = false;
	if (MLX5_CAP_GEN(mdev, cqe_compression) &&
	    MLX5_CAP_GEN(mdev, vport_group_manager))
		params->rx_cqe_compress_def = slow_pci_heuristic(mdev);

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS, params->rx_cqe_compress_def);
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE, false);

	/* RQ */
	mlx5e_build_rq_params(mdev, params);

	params->terminate_lkey_be = mlx5_core_get_terminate_scatter_list_mkey(mdev);

	params->packet_merge.timeout = mlx5e_choose_lro_timeout(mdev, MLX5E_DEFAULT_LRO_TIMEOUT);

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
	u8 addr[ETH_ALEN];

	mlx5_query_mac_address(priv->mdev, addr);
	if (is_zero_ether_addr(addr) &&
	    !MLX5_CAP_GEN(priv->mdev, vport_group_manager)) {
		eth_hw_addr_random(netdev);
		mlx5_core_info(priv->mdev, "Assigned random MAC address %pM\n", netdev->dev_addr);
		return;
	}

	eth_hw_addr_set(netdev, addr);
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

	for (tt = 0; tt < MLX5_NUM_TUNNEL_TT; tt++) {
		if (mlx5e_tunnel_proto_supported_tx(mdev, mlx5_get_proto_by_tunnel_type(tt)))
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
	netdev->xdp_metadata_ops = &mlx5e_xdp_metadata_ops;

	mlx5e_dcbnl_build_netdev(netdev);

	netdev->watchdog_timeo    = 15 * HZ;

	netdev->ethtool_ops	  = &mlx5e_ethtool_ops;

	netdev->vlan_features    |= NETIF_F_SG;
	netdev->vlan_features    |= NETIF_F_HW_CSUM;
	netdev->vlan_features    |= NETIF_F_HW_MACSEC;
	netdev->vlan_features    |= NETIF_F_GRO;
	netdev->vlan_features    |= NETIF_F_TSO;
	netdev->vlan_features    |= NETIF_F_TSO6;
	netdev->vlan_features    |= NETIF_F_RXCSUM;
	netdev->vlan_features    |= NETIF_F_RXHASH;
	netdev->vlan_features    |= NETIF_F_GSO_PARTIAL;

	netdev->mpls_features    |= NETIF_F_SG;
	netdev->mpls_features    |= NETIF_F_HW_CSUM;
	netdev->mpls_features    |= NETIF_F_TSO;
	netdev->mpls_features    |= NETIF_F_TSO6;

	netdev->hw_enc_features  |= NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_enc_features  |= NETIF_F_HW_VLAN_CTAG_RX;

	/* Tunneled LRO is not supported in the driver, and the same RQs are
	 * shared between inner and outer TIRs, so the driver can't disable LRO
	 * for inner TIRs while having it enabled for outer TIRs. Due to this,
	 * block LRO altogether if the firmware declares tunneled LRO support.
	 */
	if (!!MLX5_CAP_ETH(mdev, lro_cap) &&
	    !MLX5_CAP_ETH(mdev, tunnel_lro_vxlan) &&
	    !MLX5_CAP_ETH(mdev, tunnel_lro_gre) &&
	    mlx5e_check_fragmented_striding_rq_cap(mdev, PAGE_SHIFT,
						   MLX5E_MPWRQ_UMR_MODE_ALIGNED))
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

	netdev->gso_partial_features             |= NETIF_F_GSO_UDP_L4;
	netdev->hw_features                      |= NETIF_F_GSO_UDP_L4;
	netdev->features                         |= NETIF_F_GSO_UDP_L4;

	mlx5_query_port_fcs(mdev, &fcs_supported, &fcs_enabled);

	if (fcs_supported)
		netdev->hw_features |= NETIF_F_RXALL;

	if (MLX5_CAP_ETH(mdev, scatter_fcs))
		netdev->hw_features |= NETIF_F_RXFCS;

	if (mlx5_qos_is_supported(mdev))
		netdev->hw_features |= NETIF_F_HW_TC;

	netdev->features          = netdev->hw_features;

	/* Defaults */
	if (fcs_enabled)
		netdev->features  &= ~NETIF_F_RXALL;
	netdev->features  &= ~NETIF_F_LRO;
	netdev->features  &= ~NETIF_F_GRO_HW;
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

	netdev->features         |= NETIF_F_HIGHDMA;
	netdev->features         |= NETIF_F_HW_VLAN_STAG_FILTER;

	netdev->priv_flags       |= IFF_UNICAST_FLT;

	netif_set_tso_max_size(netdev, GSO_MAX_SIZE);
	mlx5e_set_xdp_feature(netdev);
	mlx5e_set_netdev_dev_addr(netdev);
	mlx5e_macsec_build_netdev(priv);
	mlx5e_ipsec_build_netdev(priv);
	mlx5e_ktls_build_netdev(priv);
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
	const bool take_rtnl = netdev->reg_state == NETREG_REGISTERED;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_flow_steering *fs;
	int err;

	mlx5e_build_nic_params(priv, &priv->xsk, netdev->mtu);
	mlx5e_vxlan_set_netdev_info(priv);

	mlx5e_timestamp_init(priv);

	priv->dfs_root = debugfs_create_dir("nic",
					    mlx5_debugfs_get_dev_root(mdev));

	fs = mlx5e_fs_init(priv->profile, mdev,
			   !test_bit(MLX5E_STATE_DESTROYING, &priv->state),
			   priv->dfs_root);
	if (!fs) {
		err = -ENOMEM;
		mlx5_core_err(mdev, "FS initialization failed, %d\n", err);
		debugfs_remove_recursive(priv->dfs_root);
		return err;
	}
	priv->fs = fs;

	err = mlx5e_ktls_init(priv);
	if (err)
		mlx5_core_err(mdev, "TLS initialization failed, %d\n", err);

	mlx5e_health_create_reporters(priv);

	/* If netdev is already registered (e.g. move from uplink to nic profile),
	 * RTNL lock must be held before triggering netdev notifiers.
	 */
	if (take_rtnl)
		rtnl_lock();

	/* update XDP supported features */
	mlx5e_set_xdp_feature(netdev);

	if (take_rtnl)
		rtnl_unlock();

	return 0;
}

static void mlx5e_nic_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_health_destroy_reporters(priv);
	mlx5e_ktls_cleanup(priv);
	mlx5e_fs_cleanup(priv->fs);
	debugfs_remove_recursive(priv->dfs_root);
	priv->fs = NULL;
}

static int mlx5e_init_nic_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	enum mlx5e_rx_res_features features;
	int err;

	priv->rx_res = mlx5e_rx_res_alloc();
	if (!priv->rx_res)
		return -ENOMEM;

	mlx5e_create_q_counters(priv);

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_destroy_q_counters;
	}

	features = MLX5E_RX_RES_FEATURE_PTP;
	if (mlx5_tunnel_inner_ft_supported(mdev))
		features |= MLX5E_RX_RES_FEATURE_INNER_FT;
	err = mlx5e_rx_res_init(priv->rx_res, priv->mdev, features,
				priv->max_nch, priv->drop_rq.rqn,
				&priv->channels.params.packet_merge,
				priv->channels.params.num_channels);
	if (err)
		goto err_close_drop_rq;

	err = mlx5e_create_flow_steering(priv->fs, priv->rx_res, priv->profile,
					 priv->netdev);
	if (err) {
		mlx5_core_warn(mdev, "create flow steering failed, %d\n", err);
		goto err_destroy_rx_res;
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
	mlx5e_destroy_flow_steering(priv->fs, !!(priv->netdev->hw_features & NETIF_F_NTUPLE),
				    priv->profile);
err_destroy_rx_res:
	mlx5e_rx_res_destroy(priv->rx_res);
err_close_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);
err_destroy_q_counters:
	mlx5e_destroy_q_counters(priv);
	mlx5e_rx_res_free(priv->rx_res);
	priv->rx_res = NULL;
	return err;
}

static void mlx5e_cleanup_nic_rx(struct mlx5e_priv *priv)
{
	mlx5e_accel_cleanup_rx(priv);
	mlx5e_tc_nic_cleanup(priv);
	mlx5e_destroy_flow_steering(priv->fs, !!(priv->netdev->hw_features & NETIF_F_NTUPLE),
				    priv->profile);
	mlx5e_rx_res_destroy(priv->rx_res);
	mlx5e_close_drop_rq(&priv->drop_rq);
	mlx5e_destroy_q_counters(priv);
	mlx5e_rx_res_free(priv->rx_res);
	priv->rx_res = NULL;
}

static void mlx5e_set_mqprio_rl(struct mlx5e_priv *priv)
{
	struct mlx5e_params *params;
	struct mlx5e_mqprio_rl *rl;

	params = &priv->channels.params;
	if (params->mqprio.mode != TC_MQPRIO_MODE_CHANNEL)
		return;

	rl = mlx5e_mqprio_rl_create(priv->mdev, params->mqprio.num_tc,
				    params->mqprio.channel.max_rate);
	if (IS_ERR(rl))
		rl = NULL;
	priv->mqprio_rl = rl;
	mlx5e_mqprio_rl_update_params(params, rl);
}

static int mlx5e_init_nic_tx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tises failed, %d\n", err);
		return err;
	}

	err = mlx5e_accel_init_tx(priv);
	if (err)
		goto err_destroy_tises;

	mlx5e_set_mqprio_rl(priv);
	mlx5e_dcbnl_initialize(priv);
	return 0;

err_destroy_tises:
	mlx5e_destroy_tises(priv);
	return err;
}

static void mlx5e_nic_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	mlx5e_fs_init_l2_addr(priv->fs, netdev);
	mlx5e_ipsec_init(priv);

	err = mlx5e_macsec_init(priv);
	if (err)
		mlx5_core_err(mdev, "MACsec initialization failed, %d\n", err);

	/* Marking the link as currently not needed by the Driver */
	if (!netif_running(netdev))
		mlx5e_modify_admin_state(mdev, MLX5_PORT_DOWN);

	mlx5e_set_netdev_mtu_boundaries(priv);
	mlx5e_set_dev_port_mtu(priv);

	mlx5_lag_add_netdev(mdev, netdev);

	mlx5e_enable_async_events(priv);
	mlx5e_enable_blocking_events(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_init(priv);

	mlx5e_hv_vhca_stats_create(priv);
	if (netdev->reg_state != NETREG_REGISTERED)
		return;
	mlx5e_dcbnl_init_app(priv);

	mlx5e_nic_set_rx_mode(priv);

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

	mlx5e_nic_set_rx_mode(priv);

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
	mlx5_lag_remove_netdev(mdev, priv->netdev);
	mlx5_vxlan_reset_to_default(mdev->vxlan);
	mlx5e_macsec_cleanup(priv);
	mlx5e_ipsec_cleanup(priv);
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
	.stats_grps	   = mlx5e_nic_stats_grps,
	.stats_grps_num	   = mlx5e_nic_stats_grps_num,
	.features          = BIT(MLX5E_PROFILE_FEATURE_PTP_RX) |
		BIT(MLX5E_PROFILE_FEATURE_PTP_TX) |
		BIT(MLX5E_PROFILE_FEATURE_QOS_HTB) |
		BIT(MLX5E_PROFILE_FEATURE_FS_VLAN) |
		BIT(MLX5E_PROFILE_FEATURE_FS_TC),
};

static int mlx5e_profile_max_num_channels(struct mlx5_core_dev *mdev,
					  const struct mlx5e_profile *profile)
{
	int nch;

	nch = mlx5e_get_max_num_channels(mdev);

	if (profile->max_nch_limit)
		nch = min_t(int, nch, profile->max_nch_limit(mdev));
	return nch;
}

static unsigned int
mlx5e_calc_max_nch(struct mlx5_core_dev *mdev, struct net_device *netdev,
		   const struct mlx5e_profile *profile)

{
	unsigned int max_nch, tmp;

	/* core resources */
	max_nch = mlx5e_profile_max_num_channels(mdev, profile);

	/* netdev rx queues */
	max_nch = min_t(unsigned int, max_nch, netdev->num_rx_queues);

	/* netdev tx queues */
	tmp = netdev->num_tx_queues;
	if (mlx5_qos_is_supported(mdev))
		tmp -= mlx5e_qos_max_leaf_nodes(mdev);
	if (MLX5_CAP_GEN(mdev, ts_cqe_to_dest_cqn))
		tmp -= profile->max_tc;
	tmp = tmp / profile->max_tc;
	max_nch = min_t(unsigned int, max_nch, tmp);

	return max_nch;
}

int mlx5e_get_pf_num_tirs(struct mlx5_core_dev *mdev)
{
	/* Indirect TIRS: 2 sets of TTCs (inner + outer steering)
	 * and 1 set of direct TIRS
	 */
	return 2 * MLX5E_NUM_INDIR_TIRS
		+ mlx5e_profile_max_num_channels(mdev, &mlx5e_nic_profile);
}

void mlx5e_set_rx_mode_work(struct work_struct *work)
{
	struct mlx5e_priv *priv = container_of(work, struct mlx5e_priv,
					       set_rx_mode_work);

	return mlx5e_fs_set_rx_mode_work(priv->fs, priv->netdev);
}

/* mlx5e generic netdev management API (move to en_common.c) */
int mlx5e_priv_init(struct mlx5e_priv *priv,
		    const struct mlx5e_profile *profile,
		    struct net_device *netdev,
		    struct mlx5_core_dev *mdev)
{
	int nch, num_txqs, node;
	int err;

	num_txqs = netdev->num_tx_queues;
	nch = mlx5e_calc_max_nch(mdev, netdev, profile);
	node = dev_to_node(mlx5_core_dma_dev(mdev));

	/* priv init */
	priv->mdev        = mdev;
	priv->netdev      = netdev;
	priv->max_nch     = nch;
	priv->max_opened_tc = 1;

	if (!alloc_cpumask_var(&priv->scratchpad.cpumask, GFP_KERNEL))
		return -ENOMEM;

	mutex_init(&priv->state_lock);

	err = mlx5e_selq_init(&priv->selq, &priv->state_lock);
	if (err)
		goto err_free_cpumask;

	INIT_WORK(&priv->update_carrier_work, mlx5e_update_carrier_work);
	INIT_WORK(&priv->set_rx_mode_work, mlx5e_set_rx_mode_work);
	INIT_WORK(&priv->tx_timeout_work, mlx5e_tx_timeout_work);
	INIT_WORK(&priv->update_stats_work, mlx5e_update_stats_work);

	priv->wq = create_singlethread_workqueue("mlx5e");
	if (!priv->wq)
		goto err_free_selq;

	priv->txq2sq = kcalloc_node(num_txqs, sizeof(*priv->txq2sq), GFP_KERNEL, node);
	if (!priv->txq2sq)
		goto err_destroy_workqueue;

	priv->tx_rates = kcalloc_node(num_txqs, sizeof(*priv->tx_rates), GFP_KERNEL, node);
	if (!priv->tx_rates)
		goto err_free_txq2sq;

	priv->channel_stats =
		kcalloc_node(nch, sizeof(*priv->channel_stats), GFP_KERNEL, node);
	if (!priv->channel_stats)
		goto err_free_tx_rates;

	return 0;

err_free_tx_rates:
	kfree(priv->tx_rates);
err_free_txq2sq:
	kfree(priv->txq2sq);
err_destroy_workqueue:
	destroy_workqueue(priv->wq);
err_free_selq:
	mlx5e_selq_cleanup(&priv->selq);
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

	for (i = 0; i < priv->stats_nch; i++)
		kvfree(priv->channel_stats[i]);
	kfree(priv->channel_stats);
	kfree(priv->tx_rates);
	kfree(priv->txq2sq);
	destroy_workqueue(priv->wq);
	mutex_lock(&priv->state_lock);
	mlx5e_selq_cleanup(&priv->selq);
	mutex_unlock(&priv->state_lock);
	free_cpumask_var(priv->scratchpad.cpumask);

	for (i = 0; i < priv->htb_max_qos_sqs; i++)
		kfree(priv->htb_qos_sq_stats[i]);
	kvfree(priv->htb_qos_sq_stats);

	memset(priv, 0, sizeof(*priv));
}

static unsigned int mlx5e_get_max_num_txqs(struct mlx5_core_dev *mdev,
					   const struct mlx5e_profile *profile)
{
	unsigned int nch, ptp_txqs, qos_txqs;

	nch = mlx5e_profile_max_num_channels(mdev, profile);

	ptp_txqs = MLX5_CAP_GEN(mdev, ts_cqe_to_dest_cqn) &&
		mlx5e_profile_feature_cap(profile, PTP_TX) ?
		profile->max_tc : 0;

	qos_txqs = mlx5_qos_is_supported(mdev) &&
		mlx5e_profile_feature_cap(profile, QOS_HTB) ?
		mlx5e_qos_max_leaf_nodes(mdev) : 0;

	return nch * profile->max_tc + ptp_txqs + qos_txqs;
}

static unsigned int mlx5e_get_max_num_rxqs(struct mlx5_core_dev *mdev,
					   const struct mlx5e_profile *profile)
{
	return mlx5e_profile_max_num_channels(mdev, profile);
}

struct net_device *
mlx5e_create_netdev(struct mlx5_core_dev *mdev, const struct mlx5e_profile *profile)
{
	struct net_device *netdev;
	unsigned int txqs, rxqs;
	int err;

	txqs = mlx5e_get_max_num_txqs(mdev, profile);
	rxqs = mlx5e_get_max_num_rxqs(mdev, profile);

	netdev = alloc_etherdev_mqs(sizeof(struct mlx5e_priv), txqs, rxqs);
	if (!netdev) {
		mlx5_core_err(mdev, "alloc_etherdev_mqs() failed\n");
		return NULL;
	}

	err = mlx5e_priv_init(netdev_priv(netdev), profile, netdev, mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_priv_init failed, err=%d\n", err);
		goto err_free_netdev;
	}

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
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

static void mlx5e_reset_channels(struct net_device *netdev)
{
	netdev_reset_tc(netdev);
}

int mlx5e_attach_netdev(struct mlx5e_priv *priv)
{
	const bool take_rtnl = priv->netdev->reg_state == NETREG_REGISTERED;
	const struct mlx5e_profile *profile = priv->profile;
	int max_nch;
	int err;

	clear_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (priv->fs)
		mlx5e_fs_set_state_destroy(priv->fs,
					   !test_bit(MLX5E_STATE_DESTROYING, &priv->state));

	/* Validate the max_wqe_size_sq capability. */
	if (WARN_ON_ONCE(mlx5e_get_max_sq_wqebbs(priv->mdev) < MLX5E_MAX_TX_WQEBBS)) {
		mlx5_core_warn(priv->mdev, "MLX5E: Max SQ WQEBBs firmware capability: %u, needed %u\n",
			       mlx5e_get_max_sq_wqebbs(priv->mdev), (unsigned int)MLX5E_MAX_TX_WQEBBS);
		return -EIO;
	}

	/* max number of channels may have changed */
	max_nch = mlx5e_calc_max_nch(priv->mdev, priv->netdev, profile);
	if (priv->channels.params.num_channels > max_nch) {
		mlx5_core_warn(priv->mdev, "MLX5E: Reducing number of channels to %d\n", max_nch);
		/* Reducing the number of channels - RXFH has to be reset, and
		 * mlx5e_num_channels_changed below will build the RQT.
		 */
		priv->netdev->priv_flags &= ~IFF_RXFH_CONFIGURED;
		priv->channels.params.num_channels = max_nch;
		if (priv->channels.params.mqprio.mode == TC_MQPRIO_MODE_CHANNEL) {
			mlx5_core_warn(priv->mdev, "MLX5E: Disabling MQPRIO channel mode\n");
			mlx5e_params_mqprio_reset(&priv->channels.params);
		}
	}
	if (max_nch != priv->max_nch) {
		mlx5_core_warn(priv->mdev,
			       "MLX5E: Updating max number of channels from %u to %u\n",
			       priv->max_nch, max_nch);
		priv->max_nch = max_nch;
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
	mlx5e_reset_channels(priv->netdev);
	set_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (priv->fs)
		mlx5e_fs_set_state_destroy(priv->fs,
					   !test_bit(MLX5E_STATE_DESTROYING, &priv->state));
	cancel_work_sync(&priv->update_stats_work);
	return err;
}

void mlx5e_detach_netdev(struct mlx5e_priv *priv)
{
	const struct mlx5e_profile *profile = priv->profile;

	set_bit(MLX5E_STATE_DESTROYING, &priv->state);
	if (priv->fs)
		mlx5e_fs_set_state_destroy(priv->fs,
					   !test_bit(MLX5E_STATE_DESTROYING, &priv->state));

	if (profile->disable)
		profile->disable(priv);
	flush_workqueue(priv->wq);

	profile->cleanup_rx(priv);
	profile->cleanup_tx(priv);
	mlx5e_reset_channels(priv->netdev);
	cancel_work_sync(&priv->update_stats_work);
}

static int
mlx5e_netdev_init_profile(struct net_device *netdev, struct mlx5_core_dev *mdev,
			  const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	err = mlx5e_priv_init(priv, new_profile, netdev, mdev);
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

	return 0;

priv_cleanup:
	mlx5e_priv_cleanup(priv);
	return err;
}

static int
mlx5e_netdev_attach_profile(struct net_device *netdev, struct mlx5_core_dev *mdev,
			    const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	err = mlx5e_netdev_init_profile(netdev, mdev, new_profile, new_ppriv);
	if (err)
		return err;

	err = mlx5e_attach_netdev(priv);
	if (err)
		goto profile_cleanup;
	return err;

profile_cleanup:
	new_profile->cleanup(priv);
	mlx5e_priv_cleanup(priv);
	return err;
}

int mlx5e_netdev_change_profile(struct mlx5e_priv *priv,
				const struct mlx5e_profile *new_profile, void *new_ppriv)
{
	const struct mlx5e_profile *orig_profile = priv->profile;
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	void *orig_ppriv = priv->ppriv;
	int err, rollback_err;

	/* cleanup old profile */
	mlx5e_detach_netdev(priv);
	priv->profile->cleanup(priv);
	mlx5e_priv_cleanup(priv);

	if (mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		mlx5e_netdev_init_profile(netdev, mdev, new_profile, new_ppriv);
		set_bit(MLX5E_STATE_DESTROYING, &priv->state);
		return -EIO;
	}

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

void mlx5e_netdev_attach_nic_profile(struct mlx5e_priv *priv)
{
	mlx5e_netdev_change_profile(priv, &mlx5e_nic_profile, NULL);
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
	struct mlx5e_dev *mlx5e_dev = auxiliary_get_drvdata(adev);
	struct mlx5e_priv *priv = mlx5e_dev->priv;
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
	struct mlx5e_dev *mlx5e_dev = auxiliary_get_drvdata(adev);
	struct mlx5e_priv *priv = mlx5e_dev->priv;
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!netif_device_present(netdev)) {
		if (test_bit(MLX5E_STATE_DESTROYING, &priv->state))
			mlx5e_destroy_mdev_resources(mdev);
		return -ENODEV;
	}

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
	struct mlx5e_dev *mlx5e_dev;
	struct net_device *netdev;
	pm_message_t state = {};
	struct mlx5e_priv *priv;
	int err;

	mlx5e_dev = mlx5e_create_devlink(&adev->dev, mdev);
	if (IS_ERR(mlx5e_dev))
		return PTR_ERR(mlx5e_dev);
	auxiliary_set_drvdata(adev, mlx5e_dev);

	err = mlx5e_devlink_port_register(mlx5e_dev, mdev);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_devlink_port_register failed, %d\n", err);
		goto err_devlink_unregister;
	}

	netdev = mlx5e_create_netdev(mdev, profile);
	if (!netdev) {
		mlx5_core_err(mdev, "mlx5e_create_netdev failed\n");
		err = -ENOMEM;
		goto err_devlink_port_unregister;
	}
	SET_NETDEV_DEVLINK_PORT(netdev, &mlx5e_dev->dl_port);

	mlx5e_build_nic_netdev(netdev);

	priv = netdev_priv(netdev);
	mlx5e_dev->priv = priv;

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

	mlx5e_dcbnl_init_app(priv);
	mlx5_core_uplink_netdev_set(mdev, netdev);
	mlx5e_params_print_info(mdev, &priv->channels.params);
	return 0;

err_resume:
	mlx5e_suspend(adev, state);
err_profile_cleanup:
	profile->cleanup(priv);
err_destroy_netdev:
	mlx5e_destroy_netdev(priv);
err_devlink_port_unregister:
	mlx5e_devlink_port_unregister(mlx5e_dev);
err_devlink_unregister:
	mlx5e_destroy_devlink(mlx5e_dev);
	return err;
}

static void mlx5e_remove(struct auxiliary_device *adev)
{
	struct mlx5e_dev *mlx5e_dev = auxiliary_get_drvdata(adev);
	struct mlx5e_priv *priv = mlx5e_dev->priv;
	pm_message_t state = {};

	mlx5_core_uplink_netdev_set(priv->mdev, NULL);
	mlx5e_dcbnl_delete_app(priv);
	unregister_netdev(priv->netdev);
	mlx5e_suspend(adev, state);
	priv->profile->cleanup(priv);
	mlx5e_destroy_netdev(priv);
	mlx5e_devlink_port_unregister(mlx5e_dev);
	mlx5e_destroy_devlink(mlx5e_dev);
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

	mlx5e_build_ptys2ethtool_map();
	ret = auxiliary_driver_register(&mlx5e_driver);
	if (ret)
		return ret;

	ret = mlx5e_rep_init();
	if (ret)
		auxiliary_driver_unregister(&mlx5e_driver);
	return ret;
}

void mlx5e_cleanup(void)
{
	mlx5e_rep_cleanup();
	auxiliary_driver_unregister(&mlx5e_driver);
}

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

#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <net/dsfield.h>
#include "en.h"
#include "ipoib/ipoib.h"
#include "en_accel/en_accel.h"
#include "lib/clock.h"

#define MLX5E_SQ_NOPS_ROOM  MLX5_SEND_WQE_MAX_WQEBBS

#ifndef CONFIG_MLX5_EN_TLS
#define MLX5E_SQ_STOP_ROOM (MLX5_SEND_WQE_MAX_WQEBBS +\
			    MLX5E_SQ_NOPS_ROOM)
#else
/* TLS offload requires MLX5E_SQ_STOP_ROOM to have
 * enough room for a resync SKB, a normal SKB and a NOP
 */
#define MLX5E_SQ_STOP_ROOM (2 * MLX5_SEND_WQE_MAX_WQEBBS +\
			    MLX5E_SQ_NOPS_ROOM)
#endif

static inline void mlx5e_tx_dma_unmap(struct device *pdev,
				      struct mlx5e_sq_dma *dma)
{
	switch (dma->type) {
	case MLX5E_DMA_MAP_SINGLE:
		dma_unmap_single(pdev, dma->addr, dma->size, DMA_TO_DEVICE);
		break;
	case MLX5E_DMA_MAP_PAGE:
		dma_unmap_page(pdev, dma->addr, dma->size, DMA_TO_DEVICE);
		break;
	default:
		WARN_ONCE(true, "mlx5e_tx_dma_unmap unknown DMA type!\n");
	}
}

static inline void mlx5e_dma_push(struct mlx5e_txqsq *sq,
				  dma_addr_t addr,
				  u32 size,
				  enum mlx5e_dma_map_type map_type)
{
	u32 i = sq->dma_fifo_pc & sq->dma_fifo_mask;

	sq->db.dma_fifo[i].addr = addr;
	sq->db.dma_fifo[i].size = size;
	sq->db.dma_fifo[i].type = map_type;
	sq->dma_fifo_pc++;
}

static inline struct mlx5e_sq_dma *mlx5e_dma_get(struct mlx5e_txqsq *sq, u32 i)
{
	return &sq->db.dma_fifo[i & sq->dma_fifo_mask];
}

static void mlx5e_dma_unmap_wqe_err(struct mlx5e_txqsq *sq, u8 num_dma)
{
	int i;

	for (i = 0; i < num_dma; i++) {
		struct mlx5e_sq_dma *last_pushed_dma =
			mlx5e_dma_get(sq, --sq->dma_fifo_pc);

		mlx5e_tx_dma_unmap(sq->pdev, last_pushed_dma);
	}
}

#ifdef CONFIG_MLX5_CORE_EN_DCB
static inline int mlx5e_get_dscp_up(struct mlx5e_priv *priv, struct sk_buff *skb)
{
	int dscp_cp = 0;

	if (skb->protocol == htons(ETH_P_IP))
		dscp_cp = ipv4_get_dsfield(ip_hdr(skb)) >> 2;
	else if (skb->protocol == htons(ETH_P_IPV6))
		dscp_cp = ipv6_get_dsfield(ipv6_hdr(skb)) >> 2;

	return priv->dcbx_dp.dscp2prio[dscp_cp];
}
#endif

u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       void *accel_priv, select_queue_fallback_t fallback)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int channel_ix = fallback(dev, skb);
	u16 num_channels;
	int up = 0;

	if (!netdev_get_num_tc(dev))
		return channel_ix;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state == MLX5_QPTS_TRUST_DSCP)
		up = mlx5e_get_dscp_up(priv, skb);
	else
#endif
		if (skb_vlan_tag_present(skb))
			up = skb->vlan_tci >> VLAN_PRIO_SHIFT;

	/* channel_ix can be larger than num_channels since
	 * dev->num_real_tx_queues = num_channels * num_tc
	 */
	num_channels = priv->channels.params.num_channels;
	if (channel_ix >= num_channels)
		channel_ix = reciprocal_scale(channel_ix, num_channels);

	return priv->channel_tc2txq[channel_ix][up];
}

static inline int mlx5e_skb_l2_header_offset(struct sk_buff *skb)
{
#define MLX5E_MIN_INLINE (ETH_HLEN + VLAN_HLEN)

	return max(skb_network_offset(skb), MLX5E_MIN_INLINE);
}

static inline int mlx5e_skb_l3_header_offset(struct sk_buff *skb)
{
	struct flow_keys keys;

	if (skb_transport_header_was_set(skb))
		return skb_transport_offset(skb);
	else if (skb_flow_dissect_flow_keys(skb, &keys, 0))
		return keys.control.thoff;
	else
		return mlx5e_skb_l2_header_offset(skb);
}

static inline u16 mlx5e_calc_min_inline(enum mlx5_inline_modes mode,
					struct sk_buff *skb)
{
	u16 hlen;

	switch (mode) {
	case MLX5_INLINE_MODE_NONE:
		return 0;
	case MLX5_INLINE_MODE_TCP_UDP:
		hlen = eth_get_headlen(skb->data, skb_headlen(skb));
		if (hlen == ETH_HLEN && !skb_vlan_tag_present(skb))
			hlen += VLAN_HLEN;
		break;
	case MLX5_INLINE_MODE_IP:
		/* When transport header is set to zero, it means no transport
		 * header. When transport header is set to 0xff's, it means
		 * transport header wasn't set.
		 */
		if (skb_transport_offset(skb)) {
			hlen = mlx5e_skb_l3_header_offset(skb);
			break;
		}
		/* fall through */
	case MLX5_INLINE_MODE_L2:
	default:
		hlen = mlx5e_skb_l2_header_offset(skb);
	}
	return min_t(u16, hlen, skb_headlen(skb));
}

static inline void mlx5e_tx_skb_pull_inline(unsigned char **skb_data,
					    unsigned int *skb_len,
					    unsigned int len)
{
	*skb_len -= len;
	*skb_data += len;
}

static inline void mlx5e_insert_vlan(void *start, struct sk_buff *skb, u16 ihs,
				     unsigned char **skb_data,
				     unsigned int *skb_len)
{
	struct vlan_ethhdr *vhdr = (struct vlan_ethhdr *)start;
	int cpy1_sz = 2 * ETH_ALEN;
	int cpy2_sz = ihs - cpy1_sz;

	memcpy(vhdr, *skb_data, cpy1_sz);
	mlx5e_tx_skb_pull_inline(skb_data, skb_len, cpy1_sz);
	vhdr->h_vlan_proto = skb->vlan_proto;
	vhdr->h_vlan_TCI = cpu_to_be16(skb_vlan_tag_get(skb));
	memcpy(&vhdr->h_vlan_encapsulated_proto, *skb_data, cpy2_sz);
	mlx5e_tx_skb_pull_inline(skb_data, skb_len, cpy2_sz);
}

static inline void
mlx5e_txwqe_build_eseg_csum(struct mlx5e_txqsq *sq, struct sk_buff *skb, struct mlx5_wqe_eth_seg *eseg)
{
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM;
		if (skb->encapsulation) {
			eseg->cs_flags |= MLX5_ETH_WQE_L3_INNER_CSUM |
					  MLX5_ETH_WQE_L4_INNER_CSUM;
			sq->stats.csum_partial_inner++;
		} else {
			eseg->cs_flags |= MLX5_ETH_WQE_L4_CSUM;
			sq->stats.csum_partial++;
		}
	} else
		sq->stats.csum_none++;
}

static inline u16
mlx5e_txwqe_build_eseg_gso(struct mlx5e_txqsq *sq, struct sk_buff *skb,
			   struct mlx5_wqe_eth_seg *eseg, unsigned int *num_bytes)
{
	u16 ihs;

	eseg->mss    = cpu_to_be16(skb_shinfo(skb)->gso_size);

	if (skb->encapsulation) {
		ihs = skb_inner_transport_offset(skb) + inner_tcp_hdrlen(skb);
		sq->stats.tso_inner_packets++;
		sq->stats.tso_inner_bytes += skb->len - ihs;
	} else {
		ihs = skb_transport_offset(skb) + tcp_hdrlen(skb);
		sq->stats.tso_packets++;
		sq->stats.tso_bytes += skb->len - ihs;
	}

	*num_bytes = skb->len + (skb_shinfo(skb)->gso_segs - 1) * ihs;
	return ihs;
}

static inline int
mlx5e_txwqe_build_dsegs(struct mlx5e_txqsq *sq, struct sk_buff *skb,
			unsigned char *skb_data, u16 headlen,
			struct mlx5_wqe_data_seg *dseg)
{
	dma_addr_t dma_addr = 0;
	u8 num_dma          = 0;
	int i;

	if (headlen) {
		dma_addr = dma_map_single(sq->pdev, skb_data, headlen,
					  DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(sq->pdev, dma_addr)))
			goto dma_unmap_wqe_err;

		dseg->addr       = cpu_to_be64(dma_addr);
		dseg->lkey       = sq->mkey_be;
		dseg->byte_count = cpu_to_be32(headlen);

		mlx5e_dma_push(sq, dma_addr, headlen, MLX5E_DMA_MAP_SINGLE);
		num_dma++;
		dseg++;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i];
		int fsz = skb_frag_size(frag);

		dma_addr = skb_frag_dma_map(sq->pdev, frag, 0, fsz,
					    DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(sq->pdev, dma_addr)))
			goto dma_unmap_wqe_err;

		dseg->addr       = cpu_to_be64(dma_addr);
		dseg->lkey       = sq->mkey_be;
		dseg->byte_count = cpu_to_be32(fsz);

		mlx5e_dma_push(sq, dma_addr, fsz, MLX5E_DMA_MAP_PAGE);
		num_dma++;
		dseg++;
	}

	return num_dma;

dma_unmap_wqe_err:
	mlx5e_dma_unmap_wqe_err(sq, num_dma);
	return -ENOMEM;
}

static inline void
mlx5e_txwqe_complete(struct mlx5e_txqsq *sq, struct sk_buff *skb,
		     u8 opcode, u16 ds_cnt, u32 num_bytes, u8 num_dma,
		     struct mlx5e_tx_wqe_info *wi, struct mlx5_wqe_ctrl_seg *cseg)
{
	struct mlx5_wq_cyc *wq = &sq->wq;
	u16 pi;

	wi->num_bytes = num_bytes;
	wi->num_dma = num_dma;
	wi->num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
	wi->skb = skb;

	cseg->opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | opcode);
	cseg->qpn_ds           = cpu_to_be32((sq->sqn << 8) | ds_cnt);

	netdev_tx_sent_queue(sq->txq, num_bytes);

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	sq->pc += wi->num_wqebbs;
	if (unlikely(!mlx5e_wqc_has_room_for(wq, sq->cc, sq->pc, MLX5E_SQ_STOP_ROOM))) {
		netif_tx_stop_queue(sq->txq);
		sq->stats.stopped++;
	}

	if (!skb->xmit_more || netif_xmit_stopped(sq->txq))
		mlx5e_notify_hw(wq, sq->pc, sq->uar_map, cseg);

	/* fill sq edge with nops to avoid wqe wrap around */
	while ((pi = (sq->pc & wq->sz_m1)) > sq->edge) {
		sq->db.wqe_info[pi].skb = NULL;
		mlx5e_post_nop(wq, sq->sqn, &sq->pc);
		sq->stats.nop++;
	}
}

netdev_tx_t mlx5e_sq_xmit(struct mlx5e_txqsq *sq, struct sk_buff *skb,
			  struct mlx5e_tx_wqe *wqe, u16 pi)
{
	struct mlx5e_tx_wqe_info *wi   = &sq->db.wqe_info[pi];

	struct mlx5_wqe_ctrl_seg *cseg = &wqe->ctrl;
	struct mlx5_wqe_eth_seg  *eseg = &wqe->eth;

	unsigned char *skb_data = skb->data;
	unsigned int skb_len = skb->len;
	u8  opcode = MLX5_OPCODE_SEND;
	unsigned int num_bytes;
	int num_dma;
	u16 headlen;
	u16 ds_cnt;
	u16 ihs;

	mlx5e_txwqe_build_eseg_csum(sq, skb, eseg);

	if (skb_is_gso(skb)) {
		opcode = MLX5_OPCODE_LSO;
		ihs = mlx5e_txwqe_build_eseg_gso(sq, skb, eseg, &num_bytes);
		sq->stats.packets += skb_shinfo(skb)->gso_segs;
	} else {
		ihs = mlx5e_calc_min_inline(sq->min_inline_mode, skb);
		num_bytes = max_t(unsigned int, skb->len, ETH_ZLEN);
		sq->stats.packets++;
	}
	sq->stats.bytes += num_bytes;
	sq->stats.xmit_more += skb->xmit_more;

	ds_cnt = sizeof(*wqe) / MLX5_SEND_WQE_DS;
	if (ihs) {
		if (skb_vlan_tag_present(skb)) {
			mlx5e_insert_vlan(eseg->inline_hdr.start, skb, ihs, &skb_data, &skb_len);
			ihs += VLAN_HLEN;
			sq->stats.added_vlan_packets++;
		} else {
			memcpy(eseg->inline_hdr.start, skb_data, ihs);
			mlx5e_tx_skb_pull_inline(&skb_data, &skb_len, ihs);
		}
		eseg->inline_hdr.sz = cpu_to_be16(ihs);
		ds_cnt += DIV_ROUND_UP(ihs - sizeof(eseg->inline_hdr.start), MLX5_SEND_WQE_DS);
	} else if (skb_vlan_tag_present(skb)) {
		eseg->insert.type = cpu_to_be16(MLX5_ETH_WQE_INSERT_VLAN);
		if (skb->vlan_proto == cpu_to_be16(ETH_P_8021AD))
			eseg->insert.type |= cpu_to_be16(MLX5_ETH_WQE_SVLAN);
		eseg->insert.vlan_tci = cpu_to_be16(skb_vlan_tag_get(skb));
		sq->stats.added_vlan_packets++;
	}

	headlen = skb_len - skb->data_len;
	num_dma = mlx5e_txwqe_build_dsegs(sq, skb, skb_data, headlen,
					  (struct mlx5_wqe_data_seg *)cseg + ds_cnt);
	if (unlikely(num_dma < 0))
		goto err_drop;

	mlx5e_txwqe_complete(sq, skb, opcode, ds_cnt + num_dma,
			     num_bytes, num_dma, wi, cseg);

	return NETDEV_TX_OK;

err_drop:
	sq->stats.dropped++;
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_tx_wqe *wqe;
	struct mlx5e_txqsq *sq;
	u16 pi;

	sq = priv->txq2sq[skb_get_queue_mapping(skb)];
	mlx5e_sq_fetch_wqe(sq, &wqe, &pi);

#ifdef CONFIG_MLX5_ACCEL
	/* might send skbs and update wqe and pi */
	skb = mlx5e_accel_handle_tx(skb, sq, dev, &wqe, &pi);
	if (unlikely(!skb))
		return NETDEV_TX_OK;
#endif
	return mlx5e_sq_xmit(sq, skb, wqe, pi);
}

static void mlx5e_dump_error_cqe(struct mlx5e_txqsq *sq,
				 struct mlx5_err_cqe *err_cqe)
{
	u32 ci = mlx5_cqwq_get_ci(&sq->cq.wq);

	netdev_err(sq->channel->netdev,
		   "Error cqe on cqn 0x%x, ci 0x%x, sqn 0x%x, syndrome 0x%x, vendor syndrome 0x%x\n",
		   sq->cq.mcq.cqn, ci, sq->sqn, err_cqe->syndrome,
		   err_cqe->vendor_err_synd);
	mlx5_dump_err_cqe(sq->cq.mdev, err_cqe);
}

bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget)
{
	struct mlx5e_txqsq *sq;
	struct mlx5_cqe64 *cqe;
	u32 dma_fifo_cc;
	u32 nbytes;
	u16 npkts;
	u16 sqcc;
	int i;

	sq = container_of(cq, struct mlx5e_txqsq, cq);

	if (unlikely(!MLX5E_TEST_BIT(sq->state, MLX5E_SQ_STATE_ENABLED)))
		return false;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return false;

	npkts = 0;
	nbytes = 0;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	/* avoid dirtying sq cache line every cqe */
	dma_fifo_cc = sq->dma_fifo_cc;

	i = 0;
	do {
		u16 wqe_counter;
		bool last_wqe;

		mlx5_cqwq_pop(&cq->wq);

		wqe_counter = be16_to_cpu(cqe->wqe_counter);

		if (unlikely(cqe->op_own >> 4 == MLX5_CQE_REQ_ERR)) {
			if (!test_and_set_bit(MLX5E_SQ_STATE_RECOVERING,
					      &sq->state)) {
				mlx5e_dump_error_cqe(sq,
						     (struct mlx5_err_cqe *)cqe);
				queue_work(cq->channel->priv->wq,
					   &sq->recover.recover_work);
			}
			sq->stats.cqe_err++;
		}

		do {
			struct mlx5e_tx_wqe_info *wi;
			struct sk_buff *skb;
			u16 ci;
			int j;

			last_wqe = (sqcc == wqe_counter);

			ci = sqcc & sq->wq.sz_m1;
			wi = &sq->db.wqe_info[ci];
			skb = wi->skb;

			if (unlikely(!skb)) { /* nop */
				sqcc++;
				continue;
			}

			if (unlikely(skb_shinfo(skb)->tx_flags &
				     SKBTX_HW_TSTAMP)) {
				struct skb_shared_hwtstamps hwts = {};

				hwts.hwtstamp =
					mlx5_timecounter_cyc2time(sq->clock,
								  get_cqe_ts(cqe));
				skb_tstamp_tx(skb, &hwts);
			}

			for (j = 0; j < wi->num_dma; j++) {
				struct mlx5e_sq_dma *dma =
					mlx5e_dma_get(sq, dma_fifo_cc++);

				mlx5e_tx_dma_unmap(sq->pdev, dma);
			}

			npkts++;
			nbytes += wi->num_bytes;
			sqcc += wi->num_wqebbs;
			napi_consume_skb(skb, napi_budget);
		} while (!last_wqe);

	} while ((++i < MLX5E_TX_CQ_POLL_BUDGET) && (cqe = mlx5_cqwq_get_cqe(&cq->wq)));

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->dma_fifo_cc = dma_fifo_cc;
	sq->cc = sqcc;

	netdev_tx_completed_queue(sq->txq, npkts, nbytes);

	if (netif_tx_queue_stopped(sq->txq) &&
	    mlx5e_wqc_has_room_for(&sq->wq, sq->cc, sq->pc,
				   MLX5E_SQ_STOP_ROOM) &&
	    !test_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state)) {
		netif_tx_wake_queue(sq->txq);
		sq->stats.wake++;
	}

	return (i == MLX5E_TX_CQ_POLL_BUDGET);
}

void mlx5e_free_txqsq_descs(struct mlx5e_txqsq *sq)
{
	struct mlx5e_tx_wqe_info *wi;
	struct sk_buff *skb;
	u16 ci;
	int i;

	while (sq->cc != sq->pc) {
		ci = sq->cc & sq->wq.sz_m1;
		wi = &sq->db.wqe_info[ci];
		skb = wi->skb;

		if (!skb) { /* nop */
			sq->cc++;
			continue;
		}

		for (i = 0; i < wi->num_dma; i++) {
			struct mlx5e_sq_dma *dma =
				mlx5e_dma_get(sq, sq->dma_fifo_cc++);

			mlx5e_tx_dma_unmap(sq->pdev, dma);
		}

		dev_kfree_skb_any(skb);
		sq->cc += wi->num_wqebbs;
	}
}

#ifdef CONFIG_MLX5_CORE_IPOIB

struct mlx5_wqe_eth_pad {
	u8 rsvd0[16];
};

struct mlx5i_tx_wqe {
	struct mlx5_wqe_ctrl_seg     ctrl;
	struct mlx5_wqe_datagram_seg datagram;
	struct mlx5_wqe_eth_pad      pad;
	struct mlx5_wqe_eth_seg      eth;
};

static inline void
mlx5i_txwqe_build_datagram(struct mlx5_av *av, u32 dqpn, u32 dqkey,
			   struct mlx5_wqe_datagram_seg *dseg)
{
	memcpy(&dseg->av, av, sizeof(struct mlx5_av));
	dseg->av.dqp_dct = cpu_to_be32(dqpn | MLX5_EXTENDED_UD_AV);
	dseg->av.key.qkey.qkey = cpu_to_be32(dqkey);
}

netdev_tx_t mlx5i_sq_xmit(struct mlx5e_txqsq *sq, struct sk_buff *skb,
			  struct mlx5_av *av, u32 dqpn, u32 dqkey)
{
	struct mlx5_wq_cyc       *wq   = &sq->wq;
	u16                       pi   = sq->pc & wq->sz_m1;
	struct mlx5i_tx_wqe      *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);
	struct mlx5e_tx_wqe_info *wi   = &sq->db.wqe_info[pi];

	struct mlx5_wqe_ctrl_seg     *cseg = &wqe->ctrl;
	struct mlx5_wqe_datagram_seg *datagram = &wqe->datagram;
	struct mlx5_wqe_eth_seg      *eseg = &wqe->eth;

	unsigned char *skb_data = skb->data;
	unsigned int skb_len = skb->len;
	u8  opcode = MLX5_OPCODE_SEND;
	unsigned int num_bytes;
	int num_dma;
	u16 headlen;
	u16 ds_cnt;
	u16 ihs;

	memset(wqe, 0, sizeof(*wqe));

	mlx5i_txwqe_build_datagram(av, dqpn, dqkey, datagram);

	mlx5e_txwqe_build_eseg_csum(sq, skb, eseg);

	if (skb_is_gso(skb)) {
		opcode = MLX5_OPCODE_LSO;
		ihs = mlx5e_txwqe_build_eseg_gso(sq, skb, eseg, &num_bytes);
		sq->stats.packets += skb_shinfo(skb)->gso_segs;
	} else {
		ihs = mlx5e_calc_min_inline(sq->min_inline_mode, skb);
		num_bytes = max_t(unsigned int, skb->len, ETH_ZLEN);
		sq->stats.packets++;
	}

	sq->stats.bytes += num_bytes;
	sq->stats.xmit_more += skb->xmit_more;

	ds_cnt = sizeof(*wqe) / MLX5_SEND_WQE_DS;
	if (ihs) {
		memcpy(eseg->inline_hdr.start, skb_data, ihs);
		mlx5e_tx_skb_pull_inline(&skb_data, &skb_len, ihs);
		eseg->inline_hdr.sz = cpu_to_be16(ihs);
		ds_cnt += DIV_ROUND_UP(ihs - sizeof(eseg->inline_hdr.start), MLX5_SEND_WQE_DS);
	}

	headlen = skb_len - skb->data_len;
	num_dma = mlx5e_txwqe_build_dsegs(sq, skb, skb_data, headlen,
					  (struct mlx5_wqe_data_seg *)cseg + ds_cnt);
	if (unlikely(num_dma < 0))
		goto err_drop;

	mlx5e_txwqe_complete(sq, skb, opcode, ds_cnt + num_dma,
			     num_bytes, num_dma, wi, cseg);

	return NETDEV_TX_OK;

err_drop:
	sq->stats.dropped++;
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

#endif

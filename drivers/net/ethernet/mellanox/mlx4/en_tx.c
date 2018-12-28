/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <asm/page.h>
#include <linux/mlx4/cq.h>
#include <linux/slab.h>
#include <linux/mlx4/qp.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/prefetch.h>
#include <linux/vmalloc.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/moduleparam.h>

#include "mlx4_en.h"

int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_tx_ring **pring, u32 size,
			   u16 stride, int node, int queue_index)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring *ring;
	int tmp;
	int err;

	ring = kzalloc_node(sizeof(*ring), GFP_KERNEL, node);
	if (!ring) {
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring) {
			en_err(priv, "Failed allocating TX ring\n");
			return -ENOMEM;
		}
	}

	ring->size = size;
	ring->size_mask = size - 1;
	ring->sp_stride = stride;
	ring->full_size = ring->size - HEADROOM - MAX_DESC_TXBBS;

	tmp = size * sizeof(struct mlx4_en_tx_info);
	ring->tx_info = kvmalloc_node(tmp, GFP_KERNEL, node);
	if (!ring->tx_info) {
		err = -ENOMEM;
		goto err_ring;
	}

	en_dbg(DRV, priv, "Allocated tx_info ring at addr:%p size:%d\n",
		 ring->tx_info, tmp);

	ring->bounce_buf = kmalloc_node(MAX_DESC_SIZE, GFP_KERNEL, node);
	if (!ring->bounce_buf) {
		ring->bounce_buf = kmalloc(MAX_DESC_SIZE, GFP_KERNEL);
		if (!ring->bounce_buf) {
			err = -ENOMEM;
			goto err_info;
		}
	}
	ring->buf_size = ALIGN(size * ring->sp_stride, MLX4_EN_PAGE_SIZE);

	/* Allocate HW buffers on provided NUMA node */
	set_dev_node(&mdev->dev->persist->pdev->dev, node);
	err = mlx4_alloc_hwq_res(mdev->dev, &ring->sp_wqres, ring->buf_size);
	set_dev_node(&mdev->dev->persist->pdev->dev, mdev->dev->numa_node);
	if (err) {
		en_err(priv, "Failed allocating hwq resources\n");
		goto err_bounce;
	}

	ring->buf = ring->sp_wqres.buf.direct.buf;

	en_dbg(DRV, priv, "Allocated TX ring (addr:%p) - buf:%p size:%d buf_size:%d dma:%llx\n",
	       ring, ring->buf, ring->size, ring->buf_size,
	       (unsigned long long) ring->sp_wqres.buf.direct.map);

	err = mlx4_qp_reserve_range(mdev->dev, 1, 1, &ring->qpn,
				    MLX4_RESERVE_ETH_BF_QP,
				    MLX4_RES_USAGE_DRIVER);
	if (err) {
		en_err(priv, "failed reserving qp for TX ring\n");
		goto err_hwq_res;
	}

	err = mlx4_qp_alloc(mdev->dev, ring->qpn, &ring->sp_qp);
	if (err) {
		en_err(priv, "Failed allocating qp %d\n", ring->qpn);
		goto err_reserve;
	}
	ring->sp_qp.event = mlx4_en_sqp_event;

	err = mlx4_bf_alloc(mdev->dev, &ring->bf, node);
	if (err) {
		en_dbg(DRV, priv, "working without blueflame (%d)\n", err);
		ring->bf.uar = &mdev->priv_uar;
		ring->bf.uar->map = mdev->uar_map;
		ring->bf_enabled = false;
		ring->bf_alloced = false;
		priv->pflags &= ~MLX4_EN_PRIV_FLAGS_BLUEFLAME;
	} else {
		ring->bf_alloced = true;
		ring->bf_enabled = !!(priv->pflags &
				      MLX4_EN_PRIV_FLAGS_BLUEFLAME);
	}

	ring->hwtstamp_tx_type = priv->hwtstamp_config.tx_type;
	ring->queue_index = queue_index;

	if (queue_index < priv->num_tx_rings_p_up)
		cpumask_set_cpu(cpumask_local_spread(queue_index,
						     priv->mdev->dev->numa_node),
				&ring->sp_affinity_mask);

	*pring = ring;
	return 0;

err_reserve:
	mlx4_qp_release_range(mdev->dev, ring->qpn, 1);
err_hwq_res:
	mlx4_free_hwq_res(mdev->dev, &ring->sp_wqres, ring->buf_size);
err_bounce:
	kfree(ring->bounce_buf);
	ring->bounce_buf = NULL;
err_info:
	kvfree(ring->tx_info);
	ring->tx_info = NULL;
err_ring:
	kfree(ring);
	*pring = NULL;
	return err;
}

void mlx4_en_destroy_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring **pring)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring *ring = *pring;
	en_dbg(DRV, priv, "Destroying tx ring, qpn: %d\n", ring->qpn);

	if (ring->bf_alloced)
		mlx4_bf_free(mdev->dev, &ring->bf);
	mlx4_qp_remove(mdev->dev, &ring->sp_qp);
	mlx4_qp_free(mdev->dev, &ring->sp_qp);
	mlx4_qp_release_range(priv->mdev->dev, ring->qpn, 1);
	mlx4_free_hwq_res(mdev->dev, &ring->sp_wqres, ring->buf_size);
	kfree(ring->bounce_buf);
	ring->bounce_buf = NULL;
	kvfree(ring->tx_info);
	ring->tx_info = NULL;
	kfree(ring);
	*pring = NULL;
}

int mlx4_en_activate_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring *ring,
			     int cq, int user_prio)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	ring->sp_cqn = cq;
	ring->prod = 0;
	ring->cons = 0xffffffff;
	ring->last_nr_txbb = 1;
	memset(ring->tx_info, 0, ring->size * sizeof(struct mlx4_en_tx_info));
	memset(ring->buf, 0, ring->buf_size);
	ring->free_tx_desc = mlx4_en_free_tx_desc;

	ring->sp_qp_state = MLX4_QP_STATE_RST;
	ring->doorbell_qpn = cpu_to_be32(ring->sp_qp.qpn << 8);
	ring->mr_key = cpu_to_be32(mdev->mr.key);

	mlx4_en_fill_qp_context(priv, ring->size, ring->sp_stride, 1, 0, ring->qpn,
				ring->sp_cqn, user_prio, &ring->sp_context);
	if (ring->bf_alloced)
		ring->sp_context.usr_page =
			cpu_to_be32(mlx4_to_hw_uar_index(mdev->dev,
							 ring->bf.uar->index));

	err = mlx4_qp_to_ready(mdev->dev, &ring->sp_wqres.mtt, &ring->sp_context,
			       &ring->sp_qp, &ring->sp_qp_state);
	if (!cpumask_empty(&ring->sp_affinity_mask))
		netif_set_xps_queue(priv->dev, &ring->sp_affinity_mask,
				    ring->queue_index);

	return err;
}

void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	mlx4_qp_modify(mdev->dev, NULL, ring->sp_qp_state,
		       MLX4_QP_STATE_RST, NULL, 0, 0, &ring->sp_qp);
}

static inline bool mlx4_en_is_tx_ring_full(struct mlx4_en_tx_ring *ring)
{
	return ring->prod - ring->cons > ring->full_size;
}

static void mlx4_en_stamp_wqe(struct mlx4_en_priv *priv,
			      struct mlx4_en_tx_ring *ring, int index,
			      u8 owner)
{
	__be32 stamp = cpu_to_be32(STAMP_VAL | (!!owner << STAMP_SHIFT));
	struct mlx4_en_tx_desc *tx_desc = ring->buf + (index << LOG_TXBB_SIZE);
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	void *end = ring->buf + ring->buf_size;
	__be32 *ptr = (__be32 *)tx_desc;
	int i;

	/* Optimize the common case when there are no wraparounds */
	if (likely((void *)tx_desc +
		   (tx_info->nr_txbb << LOG_TXBB_SIZE) <= end)) {
		/* Stamp the freed descriptor */
		for (i = 0; i < tx_info->nr_txbb << LOG_TXBB_SIZE;
		     i += STAMP_STRIDE) {
			*ptr = stamp;
			ptr += STAMP_DWORDS;
		}
	} else {
		/* Stamp the freed descriptor */
		for (i = 0; i < tx_info->nr_txbb << LOG_TXBB_SIZE;
		     i += STAMP_STRIDE) {
			*ptr = stamp;
			ptr += STAMP_DWORDS;
			if ((void *)ptr >= end) {
				ptr = ring->buf;
				stamp ^= cpu_to_be32(0x80000000);
			}
		}
	}
}


u32 mlx4_en_free_tx_desc(struct mlx4_en_priv *priv,
			 struct mlx4_en_tx_ring *ring,
			 int index, u64 timestamp,
			 int napi_mode)
{
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	struct mlx4_en_tx_desc *tx_desc = ring->buf + (index << LOG_TXBB_SIZE);
	struct mlx4_wqe_data_seg *data = (void *) tx_desc + tx_info->data_offset;
	void *end = ring->buf + ring->buf_size;
	struct sk_buff *skb = tx_info->skb;
	int nr_maps = tx_info->nr_maps;
	int i;

	/* We do not touch skb here, so prefetch skb->users location
	 * to speedup consume_skb()
	 */
	prefetchw(&skb->users);

	if (unlikely(timestamp)) {
		struct skb_shared_hwtstamps hwts;

		mlx4_en_fill_hwtstamps(priv->mdev, &hwts, timestamp);
		skb_tstamp_tx(skb, &hwts);
	}

	if (!tx_info->inl) {
		if (tx_info->linear)
			dma_unmap_single(priv->ddev,
					 tx_info->map0_dma,
					 tx_info->map0_byte_count,
					 PCI_DMA_TODEVICE);
		else
			dma_unmap_page(priv->ddev,
				       tx_info->map0_dma,
				       tx_info->map0_byte_count,
				       PCI_DMA_TODEVICE);
		/* Optimize the common case when there are no wraparounds */
		if (likely((void *)tx_desc +
			   (tx_info->nr_txbb << LOG_TXBB_SIZE) <= end)) {
			for (i = 1; i < nr_maps; i++) {
				data++;
				dma_unmap_page(priv->ddev,
					(dma_addr_t)be64_to_cpu(data->addr),
					be32_to_cpu(data->byte_count),
					PCI_DMA_TODEVICE);
			}
		} else {
			if ((void *)data >= end)
				data = ring->buf + ((void *)data - end);

			for (i = 1; i < nr_maps; i++) {
				data++;
				/* Check for wraparound before unmapping */
				if ((void *) data >= end)
					data = ring->buf;
				dma_unmap_page(priv->ddev,
					(dma_addr_t)be64_to_cpu(data->addr),
					be32_to_cpu(data->byte_count),
					PCI_DMA_TODEVICE);
			}
		}
	}
	napi_consume_skb(skb, napi_mode);

	return tx_info->nr_txbb;
}

u32 mlx4_en_recycle_tx_desc(struct mlx4_en_priv *priv,
			    struct mlx4_en_tx_ring *ring,
			    int index, u64 timestamp,
			    int napi_mode)
{
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	struct mlx4_en_rx_alloc frame = {
		.page = tx_info->page,
		.dma = tx_info->map0_dma,
	};

	if (!mlx4_en_rx_recycle(ring->recycle_ring, &frame)) {
		dma_unmap_page(priv->ddev, tx_info->map0_dma,
			       PAGE_SIZE, priv->dma_dir);
		put_page(tx_info->page);
	}

	return tx_info->nr_txbb;
}

int mlx4_en_free_tx_buf(struct net_device *dev, struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int cnt = 0;

	/* Skip last polled descriptor */
	ring->cons += ring->last_nr_txbb;
	en_dbg(DRV, priv, "Freeing Tx buf - cons:0x%x prod:0x%x\n",
		 ring->cons, ring->prod);

	if ((u32) (ring->prod - ring->cons) > ring->size) {
		if (netif_msg_tx_err(priv))
			en_warn(priv, "Tx consumer passed producer!\n");
		return 0;
	}

	while (ring->cons != ring->prod) {
		ring->last_nr_txbb = ring->free_tx_desc(priv, ring,
						ring->cons & ring->size_mask,
						0, 0 /* Non-NAPI caller */);
		ring->cons += ring->last_nr_txbb;
		cnt++;
	}

	if (ring->tx_queue)
		netdev_tx_reset_queue(ring->tx_queue);

	if (cnt)
		en_dbg(DRV, priv, "Freed %d uncompleted tx descriptors\n", cnt);

	return cnt;
}

bool mlx4_en_process_tx_cq(struct net_device *dev,
			   struct mlx4_en_cq *cq, int napi_budget)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_cq *mcq = &cq->mcq;
	struct mlx4_en_tx_ring *ring = priv->tx_ring[cq->type][cq->ring];
	struct mlx4_cqe *cqe;
	u16 index, ring_index, stamp_index;
	u32 txbbs_skipped = 0;
	u32 txbbs_stamp = 0;
	u32 cons_index = mcq->cons_index;
	int size = cq->size;
	u32 size_mask = ring->size_mask;
	struct mlx4_cqe *buf = cq->buf;
	u32 packets = 0;
	u32 bytes = 0;
	int factor = priv->cqe_factor;
	int done = 0;
	int budget = priv->tx_work_limit;
	u32 last_nr_txbb;
	u32 ring_cons;

	if (unlikely(!priv->port_up))
		return true;

	netdev_txq_bql_complete_prefetchw(ring->tx_queue);

	index = cons_index & size_mask;
	cqe = mlx4_en_get_cqe(buf, index, priv->cqe_size) + factor;
	last_nr_txbb = READ_ONCE(ring->last_nr_txbb);
	ring_cons = READ_ONCE(ring->cons);
	ring_index = ring_cons & size_mask;
	stamp_index = ring_index;

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
			cons_index & size) && (done < budget)) {
		u16 new_index;

		/*
		 * make sure we read the CQE after we read the
		 * ownership bit
		 */
		dma_rmb();

		if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
			     MLX4_CQE_OPCODE_ERROR)) {
			struct mlx4_err_cqe *cqe_err = (struct mlx4_err_cqe *)cqe;

			en_err(priv, "CQE error - vendor syndrome: 0x%x syndrome: 0x%x\n",
			       cqe_err->vendor_err_syndrome,
			       cqe_err->syndrome);
		}

		/* Skip over last polled CQE */
		new_index = be16_to_cpu(cqe->wqe_index) & size_mask;

		do {
			u64 timestamp = 0;

			txbbs_skipped += last_nr_txbb;
			ring_index = (ring_index + last_nr_txbb) & size_mask;

			if (unlikely(ring->tx_info[ring_index].ts_requested))
				timestamp = mlx4_en_get_cqe_ts(cqe);

			/* free next descriptor */
			last_nr_txbb = ring->free_tx_desc(
					priv, ring, ring_index,
					timestamp, napi_budget);

			mlx4_en_stamp_wqe(priv, ring, stamp_index,
					  !!((ring_cons + txbbs_stamp) &
						ring->size));
			stamp_index = ring_index;
			txbbs_stamp = txbbs_skipped;
			packets++;
			bytes += ring->tx_info[ring_index].nr_bytes;
		} while ((++done < budget) && (ring_index != new_index));

		++cons_index;
		index = cons_index & size_mask;
		cqe = mlx4_en_get_cqe(buf, index, priv->cqe_size) + factor;
	}

	/*
	 * To prevent CQ overflow we first update CQ consumer and only then
	 * the ring consumer.
	 */
	mcq->cons_index = cons_index;
	mlx4_cq_set_ci(mcq);
	wmb();

	/* we want to dirty this cache line once */
	WRITE_ONCE(ring->last_nr_txbb, last_nr_txbb);
	WRITE_ONCE(ring->cons, ring_cons + txbbs_skipped);

	if (cq->type == TX_XDP)
		return done < budget;

	netdev_tx_completed_queue(ring->tx_queue, packets, bytes);

	/* Wakeup Tx queue if this stopped, and ring is not full.
	 */
	if (netif_tx_queue_stopped(ring->tx_queue) &&
	    !mlx4_en_is_tx_ring_full(ring)) {
		netif_tx_wake_queue(ring->tx_queue);
		ring->wake_queue++;
	}

	return done < budget;
}

void mlx4_en_tx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);

	if (likely(priv->port_up))
		napi_schedule_irqoff(&cq->napi);
	else
		mlx4_en_arm_cq(priv, cq);
}

/* TX CQ polling - called by NAPI */
int mlx4_en_poll_tx_cq(struct napi_struct *napi, int budget)
{
	struct mlx4_en_cq *cq = container_of(napi, struct mlx4_en_cq, napi);
	struct net_device *dev = cq->dev;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	bool clean_complete;

	clean_complete = mlx4_en_process_tx_cq(dev, cq, budget);
	if (!clean_complete)
		return budget;

	napi_complete(napi);
	mlx4_en_arm_cq(priv, cq);

	return 0;
}

static struct mlx4_en_tx_desc *mlx4_en_bounce_to_desc(struct mlx4_en_priv *priv,
						      struct mlx4_en_tx_ring *ring,
						      u32 index,
						      unsigned int desc_size)
{
	u32 copy = (ring->size - index) << LOG_TXBB_SIZE;
	int i;

	for (i = desc_size - copy - 4; i >= 0; i -= 4) {
		if ((i & (TXBB_SIZE - 1)) == 0)
			wmb();

		*((u32 *) (ring->buf + i)) =
			*((u32 *) (ring->bounce_buf + copy + i));
	}

	for (i = copy - 4; i >= 4 ; i -= 4) {
		if ((i & (TXBB_SIZE - 1)) == 0)
			wmb();

		*((u32 *)(ring->buf + (index << LOG_TXBB_SIZE) + i)) =
			*((u32 *) (ring->bounce_buf + i));
	}

	/* Return real descriptor location */
	return ring->buf + (index << LOG_TXBB_SIZE);
}

/* Decide if skb can be inlined in tx descriptor to avoid dma mapping
 *
 * It seems strange we do not simply use skb_copy_bits().
 * This would allow to inline all skbs iff skb->len <= inline_thold
 *
 * Note that caller already checked skb was not a gso packet
 */
static bool is_inline(int inline_thold, const struct sk_buff *skb,
		      const struct skb_shared_info *shinfo,
		      void **pfrag)
{
	void *ptr;

	if (skb->len > inline_thold || !inline_thold)
		return false;

	if (shinfo->nr_frags == 1) {
		ptr = skb_frag_address_safe(&shinfo->frags[0]);
		if (unlikely(!ptr))
			return false;
		*pfrag = ptr;
		return true;
	}
	if (shinfo->nr_frags)
		return false;
	return true;
}

static int inline_size(const struct sk_buff *skb)
{
	if (skb->len + CTRL_SIZE + sizeof(struct mlx4_wqe_inline_seg)
	    <= MLX4_INLINE_ALIGN)
		return ALIGN(skb->len + CTRL_SIZE +
			     sizeof(struct mlx4_wqe_inline_seg), 16);
	else
		return ALIGN(skb->len + CTRL_SIZE + 2 *
			     sizeof(struct mlx4_wqe_inline_seg), 16);
}

static int get_real_size(const struct sk_buff *skb,
			 const struct skb_shared_info *shinfo,
			 struct net_device *dev,
			 int *lso_header_size,
			 bool *inline_ok,
			 void **pfrag)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int real_size;

	if (shinfo->gso_size) {
		*inline_ok = false;
		if (skb->encapsulation)
			*lso_header_size = (skb_inner_transport_header(skb) - skb->data) + inner_tcp_hdrlen(skb);
		else
			*lso_header_size = skb_transport_offset(skb) + tcp_hdrlen(skb);
		real_size = CTRL_SIZE + shinfo->nr_frags * DS_SIZE +
			ALIGN(*lso_header_size + 4, DS_SIZE);
		if (unlikely(*lso_header_size != skb_headlen(skb))) {
			/* We add a segment for the skb linear buffer only if
			 * it contains data */
			if (*lso_header_size < skb_headlen(skb))
				real_size += DS_SIZE;
			else {
				if (netif_msg_tx_err(priv))
					en_warn(priv, "Non-linear headers\n");
				return 0;
			}
		}
	} else {
		*lso_header_size = 0;
		*inline_ok = is_inline(priv->prof->inline_thold, skb,
				       shinfo, pfrag);

		if (*inline_ok)
			real_size = inline_size(skb);
		else
			real_size = CTRL_SIZE +
				    (shinfo->nr_frags + 1) * DS_SIZE;
	}

	return real_size;
}

static void build_inline_wqe(struct mlx4_en_tx_desc *tx_desc,
			     const struct sk_buff *skb,
			     const struct skb_shared_info *shinfo,
			     void *fragptr)
{
	struct mlx4_wqe_inline_seg *inl = &tx_desc->inl;
	int spc = MLX4_INLINE_ALIGN - CTRL_SIZE - sizeof(*inl);
	unsigned int hlen = skb_headlen(skb);

	if (skb->len <= spc) {
		if (likely(skb->len >= MIN_PKT_LEN)) {
			inl->byte_count = cpu_to_be32(1 << 31 | skb->len);
		} else {
			inl->byte_count = cpu_to_be32(1 << 31 | MIN_PKT_LEN);
			memset(((void *)(inl + 1)) + skb->len, 0,
			       MIN_PKT_LEN - skb->len);
		}
		skb_copy_from_linear_data(skb, inl + 1, hlen);
		if (shinfo->nr_frags)
			memcpy(((void *)(inl + 1)) + hlen, fragptr,
			       skb_frag_size(&shinfo->frags[0]));

	} else {
		inl->byte_count = cpu_to_be32(1 << 31 | spc);
		if (hlen <= spc) {
			skb_copy_from_linear_data(skb, inl + 1, hlen);
			if (hlen < spc) {
				memcpy(((void *)(inl + 1)) + hlen,
				       fragptr, spc - hlen);
				fragptr +=  spc - hlen;
			}
			inl = (void *) (inl + 1) + spc;
			memcpy(((void *)(inl + 1)), fragptr, skb->len - spc);
		} else {
			skb_copy_from_linear_data(skb, inl + 1, spc);
			inl = (void *) (inl + 1) + spc;
			skb_copy_from_linear_data_offset(skb, spc, inl + 1,
							 hlen - spc);
			if (shinfo->nr_frags)
				memcpy(((void *)(inl + 1)) + hlen - spc,
				       fragptr,
				       skb_frag_size(&shinfo->frags[0]));
		}

		dma_wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (skb->len - spc));
	}
}

u16 mlx4_en_select_queue(struct net_device *dev, struct sk_buff *skb,
			 struct net_device *sb_dev,
			 select_queue_fallback_t fallback)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	u16 rings_p_up = priv->num_tx_rings_p_up;

	if (netdev_get_num_tc(dev))
		return fallback(dev, skb, NULL);

	return fallback(dev, skb, NULL) % rings_p_up;
}

static void mlx4_bf_copy(void __iomem *dst, const void *src,
			 unsigned int bytecnt)
{
	__iowrite64_copy(dst, src, bytecnt / 8);
}

void mlx4_en_xmit_doorbell(struct mlx4_en_tx_ring *ring)
{
	wmb();
	/* Since there is no iowrite*_native() that writes the
	 * value as is, without byteswapping - using the one
	 * the doesn't do byteswapping in the relevant arch
	 * endianness.
	 */
#if defined(__LITTLE_ENDIAN)
	iowrite32(
#else
	iowrite32be(
#endif
		  (__force u32)ring->doorbell_qpn,
		  ring->bf.uar->map + MLX4_SEND_DOORBELL);
}

static void mlx4_en_tx_write_desc(struct mlx4_en_tx_ring *ring,
				  struct mlx4_en_tx_desc *tx_desc,
				  union mlx4_wqe_qpn_vlan qpn_vlan,
				  int desc_size, int bf_index,
				  __be32 op_own, bool bf_ok,
				  bool send_doorbell)
{
	tx_desc->ctrl.qpn_vlan = qpn_vlan;

	if (bf_ok) {
		op_own |= htonl((bf_index & 0xffff) << 8);
		/* Ensure new descriptor hits memory
		 * before setting ownership of this descriptor to HW
		 */
		dma_wmb();
		tx_desc->ctrl.owner_opcode = op_own;

		wmb();

		mlx4_bf_copy(ring->bf.reg + ring->bf.offset, &tx_desc->ctrl,
			     desc_size);

		wmb();

		ring->bf.offset ^= ring->bf.buf_size;
	} else {
		/* Ensure new descriptor hits memory
		 * before setting ownership of this descriptor to HW
		 */
		dma_wmb();
		tx_desc->ctrl.owner_opcode = op_own;
		if (send_doorbell)
			mlx4_en_xmit_doorbell(ring);
		else
			ring->xmit_more++;
	}
}

static bool mlx4_en_build_dma_wqe(struct mlx4_en_priv *priv,
				  struct skb_shared_info *shinfo,
				  struct mlx4_wqe_data_seg *data,
				  struct sk_buff *skb,
				  int lso_header_size,
				  __be32 mr_key,
				  struct mlx4_en_tx_info *tx_info)
{
	struct device *ddev = priv->ddev;
	dma_addr_t dma = 0;
	u32 byte_count = 0;
	int i_frag;

	/* Map fragments if any */
	for (i_frag = shinfo->nr_frags - 1; i_frag >= 0; i_frag--) {
		const struct skb_frag_struct *frag;

		frag = &shinfo->frags[i_frag];
		byte_count = skb_frag_size(frag);
		dma = skb_frag_dma_map(ddev, frag,
				       0, byte_count,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(ddev, dma))
			goto tx_drop_unmap;

		data->addr = cpu_to_be64(dma);
		data->lkey = mr_key;
		dma_wmb();
		data->byte_count = cpu_to_be32(byte_count);
		--data;
	}

	/* Map linear part if needed */
	if (tx_info->linear) {
		byte_count = skb_headlen(skb) - lso_header_size;

		dma = dma_map_single(ddev, skb->data +
				     lso_header_size, byte_count,
				     PCI_DMA_TODEVICE);
		if (dma_mapping_error(ddev, dma))
			goto tx_drop_unmap;

		data->addr = cpu_to_be64(dma);
		data->lkey = mr_key;
		dma_wmb();
		data->byte_count = cpu_to_be32(byte_count);
	}
	/* tx completion can avoid cache line miss for common cases */
	tx_info->map0_dma = dma;
	tx_info->map0_byte_count = byte_count;

	return true;

tx_drop_unmap:
	en_err(priv, "DMA mapping error\n");

	while (++i_frag < shinfo->nr_frags) {
		++data;
		dma_unmap_page(ddev, (dma_addr_t)be64_to_cpu(data->addr),
			       be32_to_cpu(data->byte_count),
			       PCI_DMA_TODEVICE);
	}

	return false;
}

netdev_tx_t mlx4_en_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct mlx4_en_priv *priv = netdev_priv(dev);
	union mlx4_wqe_qpn_vlan	qpn_vlan = {};
	struct mlx4_en_tx_ring *ring;
	struct mlx4_en_tx_desc *tx_desc;
	struct mlx4_wqe_data_seg *data;
	struct mlx4_en_tx_info *tx_info;
	int tx_ind;
	int nr_txbb;
	int desc_size;
	int real_size;
	u32 index, bf_index;
	__be32 op_own;
	int lso_header_size;
	void *fragptr = NULL;
	bool bounce = false;
	bool send_doorbell;
	bool stop_queue;
	bool inline_ok;
	u8 data_offset;
	u32 ring_cons;
	bool bf_ok;

	tx_ind = skb_get_queue_mapping(skb);
	ring = priv->tx_ring[TX][tx_ind];

	if (unlikely(!priv->port_up))
		goto tx_drop;

	/* fetch ring->cons far ahead before needing it to avoid stall */
	ring_cons = READ_ONCE(ring->cons);

	real_size = get_real_size(skb, shinfo, dev, &lso_header_size,
				  &inline_ok, &fragptr);
	if (unlikely(!real_size))
		goto tx_drop_count;

	/* Align descriptor to TXBB size */
	desc_size = ALIGN(real_size, TXBB_SIZE);
	nr_txbb = desc_size >> LOG_TXBB_SIZE;
	if (unlikely(nr_txbb > MAX_DESC_TXBBS)) {
		if (netif_msg_tx_err(priv))
			en_warn(priv, "Oversized header or SG list\n");
		goto tx_drop_count;
	}

	bf_ok = ring->bf_enabled;
	if (skb_vlan_tag_present(skb)) {
		u16 vlan_proto;

		qpn_vlan.vlan_tag = cpu_to_be16(skb_vlan_tag_get(skb));
		vlan_proto = be16_to_cpu(skb->vlan_proto);
		if (vlan_proto == ETH_P_8021AD)
			qpn_vlan.ins_vlan = MLX4_WQE_CTRL_INS_SVLAN;
		else if (vlan_proto == ETH_P_8021Q)
			qpn_vlan.ins_vlan = MLX4_WQE_CTRL_INS_CVLAN;
		else
			qpn_vlan.ins_vlan = 0;
		bf_ok = false;
	}

	netdev_txq_bql_enqueue_prefetchw(ring->tx_queue);

	/* Track current inflight packets for performance analysis */
	AVG_PERF_COUNTER(priv->pstats.inflight_avg,
			 (u32)(ring->prod - ring_cons - 1));

	/* Packet is good - grab an index and transmit it */
	index = ring->prod & ring->size_mask;
	bf_index = ring->prod;

	/* See if we have enough space for whole descriptor TXBB for setting
	 * SW ownership on next descriptor; if not, use a bounce buffer. */
	if (likely(index + nr_txbb <= ring->size))
		tx_desc = ring->buf + (index << LOG_TXBB_SIZE);
	else {
		tx_desc = (struct mlx4_en_tx_desc *) ring->bounce_buf;
		bounce = true;
		bf_ok = false;
	}

	/* Save skb in tx_info ring */
	tx_info = &ring->tx_info[index];
	tx_info->skb = skb;
	tx_info->nr_txbb = nr_txbb;

	if (!lso_header_size) {
		data = &tx_desc->data;
		data_offset = offsetof(struct mlx4_en_tx_desc, data);
	} else {
		int lso_align = ALIGN(lso_header_size + 4, DS_SIZE);

		data = (void *)&tx_desc->lso + lso_align;
		data_offset = offsetof(struct mlx4_en_tx_desc, lso) + lso_align;
	}

	/* valid only for none inline segments */
	tx_info->data_offset = data_offset;

	tx_info->inl = inline_ok;

	tx_info->linear = lso_header_size < skb_headlen(skb) && !inline_ok;

	tx_info->nr_maps = shinfo->nr_frags + tx_info->linear;
	data += tx_info->nr_maps - 1;

	if (!tx_info->inl)
		if (!mlx4_en_build_dma_wqe(priv, shinfo, data, skb,
					   lso_header_size, ring->mr_key,
					   tx_info))
			goto tx_drop_count;

	/*
	 * For timestamping add flag to skb_shinfo and
	 * set flag for further reference
	 */
	tx_info->ts_requested = 0;
	if (unlikely(ring->hwtstamp_tx_type == HWTSTAMP_TX_ON &&
		     shinfo->tx_flags & SKBTX_HW_TSTAMP)) {
		shinfo->tx_flags |= SKBTX_IN_PROGRESS;
		tx_info->ts_requested = 1;
	}

	/* Prepare ctrl segement apart opcode+ownership, which depends on
	 * whether LSO is used */
	tx_desc->ctrl.srcrb_flags = priv->ctrl_flags;
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		if (!skb->encapsulation)
			tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM |
								 MLX4_WQE_CTRL_TCP_UDP_CSUM);
		else
			tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM);
		ring->tx_csum++;
	}

	if (priv->flags & MLX4_EN_FLAG_ENABLE_HW_LOOPBACK) {
		struct ethhdr *ethh;

		/* Copy dst mac address to wqe. This allows loopback in eSwitch,
		 * so that VFs and PF can communicate with each other
		 */
		ethh = (struct ethhdr *)skb->data;
		tx_desc->ctrl.srcrb_flags16[0] = get_unaligned((__be16 *)ethh->h_dest);
		tx_desc->ctrl.imm = get_unaligned((__be32 *)(ethh->h_dest + 2));
	}

	/* Handle LSO (TSO) packets */
	if (lso_header_size) {
		int i;

		/* Mark opcode as LSO */
		op_own = cpu_to_be32(MLX4_OPCODE_LSO | (1 << 6)) |
			((ring->prod & ring->size) ?
				cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);

		/* Fill in the LSO prefix */
		tx_desc->lso.mss_hdr_size = cpu_to_be32(
			shinfo->gso_size << 16 | lso_header_size);

		/* Copy headers;
		 * note that we already verified that it is linear */
		memcpy(tx_desc->lso.header, skb->data, lso_header_size);

		ring->tso_packets++;

		i = shinfo->gso_segs;
		tx_info->nr_bytes = skb->len + (i - 1) * lso_header_size;
		ring->packets += i;
	} else {
		/* Normal (Non LSO) packet */
		op_own = cpu_to_be32(MLX4_OPCODE_SEND) |
			((ring->prod & ring->size) ?
			 cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);
		tx_info->nr_bytes = max_t(unsigned int, skb->len, ETH_ZLEN);
		ring->packets++;
	}
	ring->bytes += tx_info->nr_bytes;
	AVG_PERF_COUNTER(priv->pstats.tx_pktsz_avg, skb->len);

	if (tx_info->inl)
		build_inline_wqe(tx_desc, skb, shinfo, fragptr);

	if (skb->encapsulation) {
		union {
			struct iphdr *v4;
			struct ipv6hdr *v6;
			unsigned char *hdr;
		} ip;
		u8 proto;

		ip.hdr = skb_inner_network_header(skb);
		proto = (ip.v4->version == 4) ? ip.v4->protocol :
						ip.v6->nexthdr;

		if (proto == IPPROTO_TCP || proto == IPPROTO_UDP)
			op_own |= cpu_to_be32(MLX4_WQE_CTRL_IIP | MLX4_WQE_CTRL_ILP);
		else
			op_own |= cpu_to_be32(MLX4_WQE_CTRL_IIP);
	}

	ring->prod += nr_txbb;

	/* If we used a bounce buffer then copy descriptor back into place */
	if (unlikely(bounce))
		tx_desc = mlx4_en_bounce_to_desc(priv, ring, index, desc_size);

	skb_tx_timestamp(skb);

	/* Check available TXBBs And 2K spare for prefetch */
	stop_queue = mlx4_en_is_tx_ring_full(ring);
	if (unlikely(stop_queue)) {
		netif_tx_stop_queue(ring->tx_queue);
		ring->queue_stopped++;
	}

	send_doorbell = __netdev_tx_sent_queue(ring->tx_queue,
					       tx_info->nr_bytes,
					       skb->xmit_more);

	real_size = (real_size / 16) & 0x3f;

	bf_ok &= desc_size <= MAX_BF && send_doorbell;

	if (bf_ok)
		qpn_vlan.bf_qpn = ring->doorbell_qpn | cpu_to_be32(real_size);
	else
		qpn_vlan.fence_size = real_size;

	mlx4_en_tx_write_desc(ring, tx_desc, qpn_vlan, desc_size, bf_index,
			      op_own, bf_ok, send_doorbell);

	if (unlikely(stop_queue)) {
		/* If queue was emptied after the if (stop_queue) , and before
		 * the netif_tx_stop_queue() - need to wake the queue,
		 * or else it will remain stopped forever.
		 * Need a memory barrier to make sure ring->cons was not
		 * updated before queue was stopped.
		 */
		smp_rmb();

		ring_cons = READ_ONCE(ring->cons);
		if (unlikely(!mlx4_en_is_tx_ring_full(ring))) {
			netif_tx_wake_queue(ring->tx_queue);
			ring->wake_queue++;
		}
	}
	return NETDEV_TX_OK;

tx_drop_count:
	ring->tx_dropped++;
tx_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

#define MLX4_EN_XDP_TX_NRTXBB  1
#define MLX4_EN_XDP_TX_REAL_SZ (((CTRL_SIZE + MLX4_EN_XDP_TX_NRTXBB * DS_SIZE) \
				 / 16) & 0x3f)

void mlx4_en_init_tx_xdp_ring_descs(struct mlx4_en_priv *priv,
				    struct mlx4_en_tx_ring *ring)
{
	int i;

	for (i = 0; i < ring->size; i++) {
		struct mlx4_en_tx_info *tx_info = &ring->tx_info[i];
		struct mlx4_en_tx_desc *tx_desc = ring->buf +
			(i << LOG_TXBB_SIZE);

		tx_info->map0_byte_count = PAGE_SIZE;
		tx_info->nr_txbb = MLX4_EN_XDP_TX_NRTXBB;
		tx_info->data_offset = offsetof(struct mlx4_en_tx_desc, data);
		tx_info->ts_requested = 0;
		tx_info->nr_maps = 1;
		tx_info->linear = 1;
		tx_info->inl = 0;

		tx_desc->data.lkey = ring->mr_key;
		tx_desc->ctrl.qpn_vlan.fence_size = MLX4_EN_XDP_TX_REAL_SZ;
		tx_desc->ctrl.srcrb_flags = priv->ctrl_flags;
	}
}

netdev_tx_t mlx4_en_xmit_frame(struct mlx4_en_rx_ring *rx_ring,
			       struct mlx4_en_rx_alloc *frame,
			       struct mlx4_en_priv *priv, unsigned int length,
			       int tx_ind, bool *doorbell_pending)
{
	struct mlx4_en_tx_desc *tx_desc;
	struct mlx4_en_tx_info *tx_info;
	struct mlx4_wqe_data_seg *data;
	struct mlx4_en_tx_ring *ring;
	dma_addr_t dma;
	__be32 op_own;
	int index;

	if (unlikely(!priv->port_up))
		goto tx_drop;

	ring = priv->tx_ring[TX_XDP][tx_ind];

	if (unlikely(mlx4_en_is_tx_ring_full(ring)))
		goto tx_drop_count;

	index = ring->prod & ring->size_mask;
	tx_info = &ring->tx_info[index];

	/* Track current inflight packets for performance analysis */
	AVG_PERF_COUNTER(priv->pstats.inflight_avg,
			 (u32)(ring->prod - READ_ONCE(ring->cons) - 1));

	tx_desc = ring->buf + (index << LOG_TXBB_SIZE);
	data = &tx_desc->data;

	dma = frame->dma;

	tx_info->page = frame->page;
	frame->page = NULL;
	tx_info->map0_dma = dma;
	tx_info->nr_bytes = max_t(unsigned int, length, ETH_ZLEN);

	dma_sync_single_range_for_device(priv->ddev, dma, frame->page_offset,
					 length, PCI_DMA_TODEVICE);

	data->addr = cpu_to_be64(dma + frame->page_offset);
	dma_wmb();
	data->byte_count = cpu_to_be32(length);

	/* tx completion can avoid cache line miss for common cases */

	op_own = cpu_to_be32(MLX4_OPCODE_SEND) |
		((ring->prod & ring->size) ?
		 cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0);

	rx_ring->xdp_tx++;
	AVG_PERF_COUNTER(priv->pstats.tx_pktsz_avg, length);

	ring->prod += MLX4_EN_XDP_TX_NRTXBB;

	/* Ensure new descriptor hits memory
	 * before setting ownership of this descriptor to HW
	 */
	dma_wmb();
	tx_desc->ctrl.owner_opcode = op_own;
	ring->xmit_more++;

	*doorbell_pending = true;

	return NETDEV_TX_OK;

tx_drop_count:
	rx_ring->xdp_tx_full++;
	*doorbell_pending = true;
tx_drop:
	return NETDEV_TX_BUSY;
}

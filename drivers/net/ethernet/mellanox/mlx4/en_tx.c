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
#include <linux/moduleparam.h>

#include "mlx4_en.h"

int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_tx_ring **pring, int qpn, u32 size,
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
	ring->stride = stride;
	ring->inline_thold = priv->prof->inline_thold;

	tmp = size * sizeof(struct mlx4_en_tx_info);
	ring->tx_info = kmalloc_node(tmp, GFP_KERNEL | __GFP_NOWARN, node);
	if (!ring->tx_info) {
		ring->tx_info = vmalloc(tmp);
		if (!ring->tx_info) {
			err = -ENOMEM;
			goto err_ring;
		}
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
	ring->buf_size = ALIGN(size * ring->stride, MLX4_EN_PAGE_SIZE);

	/* Allocate HW buffers on provided NUMA node */
	set_dev_node(&mdev->dev->pdev->dev, node);
	err = mlx4_alloc_hwq_res(mdev->dev, &ring->wqres, ring->buf_size,
				 2 * PAGE_SIZE);
	set_dev_node(&mdev->dev->pdev->dev, mdev->dev->numa_node);
	if (err) {
		en_err(priv, "Failed allocating hwq resources\n");
		goto err_bounce;
	}

	err = mlx4_en_map_buffer(&ring->wqres.buf);
	if (err) {
		en_err(priv, "Failed to map TX buffer\n");
		goto err_hwq_res;
	}

	ring->buf = ring->wqres.buf.direct.buf;

	en_dbg(DRV, priv, "Allocated TX ring (addr:%p) - buf:%p size:%d buf_size:%d dma:%llx\n",
	       ring, ring->buf, ring->size, ring->buf_size,
	       (unsigned long long) ring->wqres.buf.direct.map);

	ring->qpn = qpn;
	err = mlx4_qp_alloc(mdev->dev, ring->qpn, &ring->qp, GFP_KERNEL);
	if (err) {
		en_err(priv, "Failed allocating qp %d\n", ring->qpn);
		goto err_map;
	}
	ring->qp.event = mlx4_en_sqp_event;

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

	if (queue_index < priv->num_tx_rings_p_up && cpu_online(queue_index))
		cpumask_set_cpu(queue_index, &ring->affinity_mask);

	*pring = ring;
	return 0;

err_map:
	mlx4_en_unmap_buffer(&ring->wqres.buf);
err_hwq_res:
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
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
	mlx4_qp_remove(mdev->dev, &ring->qp);
	mlx4_qp_free(mdev->dev, &ring->qp);
	mlx4_en_unmap_buffer(&ring->wqres.buf);
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
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

	ring->cqn = cq;
	ring->prod = 0;
	ring->cons = 0xffffffff;
	ring->last_nr_txbb = 1;
	memset(ring->tx_info, 0, ring->size * sizeof(struct mlx4_en_tx_info));
	memset(ring->buf, 0, ring->buf_size);

	ring->qp_state = MLX4_QP_STATE_RST;
	ring->doorbell_qpn = cpu_to_be32(ring->qp.qpn << 8);
	ring->mr_key = cpu_to_be32(mdev->mr.key);

	mlx4_en_fill_qp_context(priv, ring->size, ring->stride, 1, 0, ring->qpn,
				ring->cqn, user_prio, &ring->context);
	if (ring->bf_alloced)
		ring->context.usr_page = cpu_to_be32(ring->bf.uar->index);

	err = mlx4_qp_to_ready(mdev->dev, &ring->wqres.mtt, &ring->context,
			       &ring->qp, &ring->qp_state);
	if (!user_prio && cpu_online(ring->queue_index))
		netif_set_xps_queue(priv->dev, &ring->affinity_mask,
				    ring->queue_index);

	return err;
}

void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	mlx4_qp_modify(mdev->dev, NULL, ring->qp_state,
		       MLX4_QP_STATE_RST, NULL, 0, 0, &ring->qp);
}

static void mlx4_en_stamp_wqe(struct mlx4_en_priv *priv,
			      struct mlx4_en_tx_ring *ring, int index,
			      u8 owner)
{
	__be32 stamp = cpu_to_be32(STAMP_VAL | (!!owner << STAMP_SHIFT));
	struct mlx4_en_tx_desc *tx_desc = ring->buf + index * TXBB_SIZE;
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	void *end = ring->buf + ring->buf_size;
	__be32 *ptr = (__be32 *)tx_desc;
	int i;

	/* Optimize the common case when there are no wraparounds */
	if (likely((void *)tx_desc + tx_info->nr_txbb * TXBB_SIZE <= end)) {
		/* Stamp the freed descriptor */
		for (i = 0; i < tx_info->nr_txbb * TXBB_SIZE;
		     i += STAMP_STRIDE) {
			*ptr = stamp;
			ptr += STAMP_DWORDS;
		}
	} else {
		/* Stamp the freed descriptor */
		for (i = 0; i < tx_info->nr_txbb * TXBB_SIZE;
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


static u32 mlx4_en_free_tx_desc(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring,
				int index, u8 owner, u64 timestamp)
{
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	struct mlx4_en_tx_desc *tx_desc = ring->buf + index * TXBB_SIZE;
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

	/* Optimize the common case when there are no wraparounds */
	if (likely((void *) tx_desc + tx_info->nr_txbb * TXBB_SIZE <= end)) {
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
			for (i = 1; i < nr_maps; i++) {
				data++;
				dma_unmap_page(priv->ddev,
					(dma_addr_t)be64_to_cpu(data->addr),
					be32_to_cpu(data->byte_count),
					PCI_DMA_TODEVICE);
			}
		}
	} else {
		if (!tx_info->inl) {
			if ((void *) data >= end) {
				data = ring->buf + ((void *)data - end);
			}

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
	dev_consume_skb_any(skb);
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
		ring->last_nr_txbb = mlx4_en_free_tx_desc(priv, ring,
						ring->cons & ring->size_mask,
						!!(ring->cons & ring->size), 0);
		ring->cons += ring->last_nr_txbb;
		cnt++;
	}

	netdev_tx_reset_queue(ring->tx_queue);

	if (cnt)
		en_dbg(DRV, priv, "Freed %d uncompleted tx descriptors\n", cnt);

	return cnt;
}

static bool mlx4_en_process_tx_cq(struct net_device *dev,
				 struct mlx4_en_cq *cq)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_cq *mcq = &cq->mcq;
	struct mlx4_en_tx_ring *ring = priv->tx_ring[cq->ring];
	struct mlx4_cqe *cqe;
	u16 index;
	u16 new_index, ring_index, stamp_index;
	u32 txbbs_skipped = 0;
	u32 txbbs_stamp = 0;
	u32 cons_index = mcq->cons_index;
	int size = cq->size;
	u32 size_mask = ring->size_mask;
	struct mlx4_cqe *buf = cq->buf;
	u32 packets = 0;
	u32 bytes = 0;
	int factor = priv->cqe_factor;
	u64 timestamp = 0;
	int done = 0;
	int budget = priv->tx_work_limit;
	u32 last_nr_txbb;
	u32 ring_cons;

	if (!priv->port_up)
		return true;

	prefetchw(&ring->tx_queue->dql.limit);
	index = cons_index & size_mask;
	cqe = mlx4_en_get_cqe(buf, index, priv->cqe_size) + factor;
	last_nr_txbb = ACCESS_ONCE(ring->last_nr_txbb);
	ring_cons = ACCESS_ONCE(ring->cons);
	ring_index = ring_cons & size_mask;
	stamp_index = ring_index;

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
			cons_index & size) && (done < budget)) {
		/*
		 * make sure we read the CQE after we read the
		 * ownership bit
		 */
		rmb();

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
			txbbs_skipped += last_nr_txbb;
			ring_index = (ring_index + last_nr_txbb) & size_mask;
			if (ring->tx_info[ring_index].ts_requested)
				timestamp = mlx4_en_get_cqe_ts(cqe);

			/* free next descriptor */
			last_nr_txbb = mlx4_en_free_tx_desc(
					priv, ring, ring_index,
					!!((ring_cons + txbbs_skipped) &
					ring->size), timestamp);

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
	ACCESS_ONCE(ring->last_nr_txbb) = last_nr_txbb;
	ACCESS_ONCE(ring->cons) = ring_cons + txbbs_skipped;

	netdev_tx_completed_queue(ring->tx_queue, packets, bytes);

	/*
	 * Wakeup Tx queue if this stopped, and at least 1 packet
	 * was completed
	 */
	if (netif_tx_queue_stopped(ring->tx_queue) && txbbs_skipped > 0) {
		netif_tx_wake_queue(ring->tx_queue);
		ring->wake_queue++;
	}
	return done < budget;
}

void mlx4_en_tx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);

	if (priv->port_up)
		napi_schedule(&cq->napi);
	else
		mlx4_en_arm_cq(priv, cq);
}

/* TX CQ polling - called by NAPI */
int mlx4_en_poll_tx_cq(struct napi_struct *napi, int budget)
{
	struct mlx4_en_cq *cq = container_of(napi, struct mlx4_en_cq, napi);
	struct net_device *dev = cq->dev;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int clean_complete;

	clean_complete = mlx4_en_process_tx_cq(dev, cq);
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
	u32 copy = (ring->size - index) * TXBB_SIZE;
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

		*((u32 *) (ring->buf + index * TXBB_SIZE + i)) =
			*((u32 *) (ring->bounce_buf + i));
	}

	/* Return real descriptor location */
	return ring->buf + index * TXBB_SIZE;
}

static bool is_inline(int inline_thold, const struct sk_buff *skb,
		      const struct skb_shared_info *shinfo,
		      void **pfrag)
{
	void *ptr;

	if (inline_thold && !skb_is_gso(skb) && skb->len <= inline_thold) {
		if (shinfo->nr_frags == 1) {
			ptr = skb_frag_address_safe(&shinfo->frags[0]);
			if (unlikely(!ptr))
				return 0;

			if (pfrag)
				*pfrag = ptr;

			return 1;
		} else if (unlikely(shinfo->nr_frags))
			return 0;
		else
			return 1;
	}

	return 0;
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
			 int *lso_header_size)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int real_size;

	if (shinfo->gso_size) {
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
		if (!is_inline(priv->prof->inline_thold, skb, shinfo, NULL))
			real_size = CTRL_SIZE + (shinfo->nr_frags + 1) * DS_SIZE;
		else
			real_size = inline_size(skb);
	}

	return real_size;
}

static void build_inline_wqe(struct mlx4_en_tx_desc *tx_desc,
			     const struct sk_buff *skb,
			     const struct skb_shared_info *shinfo,
			     int real_size, u16 *vlan_tag,
			     int tx_ind, void *fragptr)
{
	struct mlx4_wqe_inline_seg *inl = &tx_desc->inl;
	int spc = MLX4_INLINE_ALIGN - CTRL_SIZE - sizeof *inl;
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

		wmb();
		inl->byte_count = cpu_to_be32(1 << 31 | (skb->len - spc));
	}
}

u16 mlx4_en_select_queue(struct net_device *dev, struct sk_buff *skb,
			 void *accel_priv, select_queue_fallback_t fallback)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	u16 rings_p_up = priv->num_tx_rings_p_up;
	u8 up = 0;

	if (dev->num_tc)
		return skb_tx_hash(dev, skb);

	if (vlan_tx_tag_present(skb))
		up = vlan_tx_tag_get(skb) >> VLAN_PRIO_SHIFT;

	return fallback(dev, skb) % rings_p_up + up * rings_p_up;
}

static void mlx4_bf_copy(void __iomem *dst, const void *src,
			 unsigned int bytecnt)
{
	__iowrite64_copy(dst, src, bytecnt / 8);
}

netdev_tx_t mlx4_en_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct device *ddev = priv->ddev;
	struct mlx4_en_tx_ring *ring;
	struct mlx4_en_tx_desc *tx_desc;
	struct mlx4_wqe_data_seg *data;
	struct mlx4_en_tx_info *tx_info;
	int tx_ind = 0;
	int nr_txbb;
	int desc_size;
	int real_size;
	u32 index, bf_index;
	__be32 op_own;
	u16 vlan_tag = 0;
	int i_frag;
	int lso_header_size;
	void *fragptr;
	bool bounce = false;
	bool send_doorbell;
	u32 ring_cons;

	if (!priv->port_up)
		goto tx_drop;

	tx_ind = skb_get_queue_mapping(skb);
	ring = priv->tx_ring[tx_ind];

	/* fetch ring->cons far ahead before needing it to avoid stall */
	ring_cons = ACCESS_ONCE(ring->cons);

	real_size = get_real_size(skb, shinfo, dev, &lso_header_size);
	if (unlikely(!real_size))
		goto tx_drop;

	/* Align descriptor to TXBB size */
	desc_size = ALIGN(real_size, TXBB_SIZE);
	nr_txbb = desc_size / TXBB_SIZE;
	if (unlikely(nr_txbb > MAX_DESC_TXBBS)) {
		if (netif_msg_tx_err(priv))
			en_warn(priv, "Oversized header or SG list\n");
		goto tx_drop;
	}

	if (vlan_tx_tag_present(skb))
		vlan_tag = vlan_tx_tag_get(skb);

	/* Check available TXBBs And 2K spare for prefetch */
	if (unlikely(((int)(ring->prod - ring_cons)) >
		     ring->size - HEADROOM - MAX_DESC_TXBBS)) {
		/* every full Tx ring stops queue */
		netif_tx_stop_queue(ring->tx_queue);
		ring->queue_stopped++;

		/* If queue was emptied after the if, and before the
		 * stop_queue - need to wake the queue, or else it will remain
		 * stopped forever.
		 * Need a memory barrier to make sure ring->cons was not
		 * updated before queue was stopped.
		 */
		wmb();

		ring_cons = ACCESS_ONCE(ring->cons);
		if (unlikely(((int)(ring->prod - ring_cons)) <=
			     ring->size - HEADROOM - MAX_DESC_TXBBS)) {
			netif_tx_wake_queue(ring->tx_queue);
			ring->wake_queue++;
		} else {
			return NETDEV_TX_BUSY;
		}
	}

	prefetchw(&ring->tx_queue->dql);

	/* Track current inflight packets for performance analysis */
	AVG_PERF_COUNTER(priv->pstats.inflight_avg,
			 (u32)(ring->prod - ring_cons - 1));

	/* Packet is good - grab an index and transmit it */
	index = ring->prod & ring->size_mask;
	bf_index = ring->prod;

	/* See if we have enough space for whole descriptor TXBB for setting
	 * SW ownership on next descriptor; if not, use a bounce buffer. */
	if (likely(index + nr_txbb <= ring->size))
		tx_desc = ring->buf + index * TXBB_SIZE;
	else {
		tx_desc = (struct mlx4_en_tx_desc *) ring->bounce_buf;
		bounce = true;
	}

	/* Save skb in tx_info ring */
	tx_info = &ring->tx_info[index];
	tx_info->skb = skb;
	tx_info->nr_txbb = nr_txbb;

	data = &tx_desc->data;
	if (lso_header_size)
		data = ((void *)&tx_desc->lso + ALIGN(lso_header_size + 4,
						      DS_SIZE));

	/* valid only for none inline segments */
	tx_info->data_offset = (void *)data - (void *)tx_desc;

	tx_info->linear = (lso_header_size < skb_headlen(skb) &&
			   !is_inline(ring->inline_thold, skb, shinfo, NULL)) ? 1 : 0;

	tx_info->nr_maps = shinfo->nr_frags + tx_info->linear;
	data += tx_info->nr_maps - 1;

	if (is_inline(ring->inline_thold, skb, shinfo, &fragptr)) {
		tx_info->inl = 1;
	} else {
		dma_addr_t dma = 0;
		u32 byte_count = 0;

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
			data->lkey = ring->mr_key;
			wmb();
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
			data->lkey = ring->mr_key;
			wmb();
			data->byte_count = cpu_to_be32(byte_count);
		}
		tx_info->inl = 0;
		/* tx completion can avoid cache line miss for common cases */
		tx_info->map0_dma = dma;
		tx_info->map0_byte_count = byte_count;
	}

	/*
	 * For timestamping add flag to skb_shinfo and
	 * set flag for further reference
	 */
	if (unlikely(ring->hwtstamp_tx_type == HWTSTAMP_TX_ON &&
		     shinfo->tx_flags & SKBTX_HW_TSTAMP)) {
		shinfo->tx_flags |= SKBTX_IN_PROGRESS;
		tx_info->ts_requested = 1;
	}

	/* Prepare ctrl segement apart opcode+ownership, which depends on
	 * whether LSO is used */
	tx_desc->ctrl.srcrb_flags = priv->ctrl_flags;
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM |
							 MLX4_WQE_CTRL_TCP_UDP_CSUM);
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

		i = ((skb->len - lso_header_size) / shinfo->gso_size) +
			!!((skb->len - lso_header_size) % shinfo->gso_size);
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
	netdev_tx_sent_queue(ring->tx_queue, tx_info->nr_bytes);
	AVG_PERF_COUNTER(priv->pstats.tx_pktsz_avg, skb->len);

	if (tx_info->inl) {
		build_inline_wqe(tx_desc, skb, shinfo, real_size, &vlan_tag,
				 tx_ind, fragptr);
		tx_info->inl = 1;
	}

	if (skb->encapsulation) {
		struct iphdr *ipv4 = (struct iphdr *)skb_inner_network_header(skb);
		if (ipv4->protocol == IPPROTO_TCP || ipv4->protocol == IPPROTO_UDP)
			op_own |= cpu_to_be32(MLX4_WQE_CTRL_IIP | MLX4_WQE_CTRL_ILP);
		else
			op_own |= cpu_to_be32(MLX4_WQE_CTRL_IIP);
	}

	ring->prod += nr_txbb;

	/* If we used a bounce buffer then copy descriptor back into place */
	if (unlikely(bounce))
		tx_desc = mlx4_en_bounce_to_desc(priv, ring, index, desc_size);

	skb_tx_timestamp(skb);

	send_doorbell = !skb->xmit_more || netif_xmit_stopped(ring->tx_queue);

	real_size = (real_size / 16) & 0x3f;

	if (ring->bf_enabled && desc_size <= MAX_BF && !bounce &&
	    !vlan_tx_tag_present(skb) && send_doorbell) {
		tx_desc->ctrl.bf_qpn = ring->doorbell_qpn |
				       cpu_to_be32(real_size);

		op_own |= htonl((bf_index & 0xffff) << 8);
		/* Ensure new descriptor hits memory
		 * before setting ownership of this descriptor to HW
		 */
		wmb();
		tx_desc->ctrl.owner_opcode = op_own;

		wmb();

		mlx4_bf_copy(ring->bf.reg + ring->bf.offset, &tx_desc->ctrl,
			     desc_size);

		wmb();

		ring->bf.offset ^= ring->bf.buf_size;
	} else {
		tx_desc->ctrl.vlan_tag = cpu_to_be16(vlan_tag);
		tx_desc->ctrl.ins_vlan = MLX4_WQE_CTRL_INS_VLAN *
			!!vlan_tx_tag_present(skb);
		tx_desc->ctrl.fence_size = real_size;

		/* Ensure new descriptor hits memory
		 * before setting ownership of this descriptor to HW
		 */
		wmb();
		tx_desc->ctrl.owner_opcode = op_own;
		if (send_doorbell) {
			wmb();
			iowrite32(ring->doorbell_qpn,
				  ring->bf.uar->map + MLX4_SEND_DOORBELL);
		} else {
			ring->xmit_more++;
		}
	}

	return NETDEV_TX_OK;

tx_drop_unmap:
	en_err(priv, "DMA mapping error\n");

	while (++i_frag < shinfo->nr_frags) {
		++data;
		dma_unmap_page(ddev, (dma_addr_t) be64_to_cpu(data->addr),
			       be32_to_cpu(data->byte_count),
			       PCI_DMA_TODEVICE);
	}

tx_drop:
	dev_kfree_skb_any(skb);
	priv->stats.tx_dropped++;
	return NETDEV_TX_OK;
}


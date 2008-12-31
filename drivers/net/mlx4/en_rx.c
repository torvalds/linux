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

#include <linux/mlx4/cq.h>
#include <linux/mlx4/qp.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>

#include "mlx4_en.h"

static void *get_wqe(struct mlx4_en_rx_ring *ring, int n)
{
	int offset = n << ring->srq.wqe_shift;
	return ring->buf + offset;
}

static void mlx4_en_srq_event(struct mlx4_srq *srq, enum mlx4_event type)
{
	return;
}

static int mlx4_en_get_frag_header(struct skb_frag_struct *frags, void **mac_hdr,
				   void **ip_hdr, void **tcpudp_hdr,
				   u64 *hdr_flags, void *priv)
{
	*mac_hdr = page_address(frags->page) + frags->page_offset;
	*ip_hdr = *mac_hdr + ETH_HLEN;
	*tcpudp_hdr = (struct tcphdr *)(*ip_hdr + sizeof(struct iphdr));
	*hdr_flags = LRO_IPV4 | LRO_TCP;

	return 0;
}

static int mlx4_en_alloc_frag(struct mlx4_en_priv *priv,
			      struct mlx4_en_rx_desc *rx_desc,
			      struct skb_frag_struct *skb_frags,
			      struct mlx4_en_rx_alloc *ring_alloc,
			      int i)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_frag_info *frag_info = &priv->frag_info[i];
	struct mlx4_en_rx_alloc *page_alloc = &ring_alloc[i];
	struct page *page;
	dma_addr_t dma;

	if (page_alloc->offset == frag_info->last_offset) {
		/* Allocate new page */
		page = alloc_pages(GFP_ATOMIC | __GFP_COMP, MLX4_EN_ALLOC_ORDER);
		if (!page)
			return -ENOMEM;

		skb_frags[i].page = page_alloc->page;
		skb_frags[i].page_offset = page_alloc->offset;
		page_alloc->page = page;
		page_alloc->offset = frag_info->frag_align;
	} else {
		page = page_alloc->page;
		get_page(page);

		skb_frags[i].page = page;
		skb_frags[i].page_offset = page_alloc->offset;
		page_alloc->offset += frag_info->frag_stride;
	}
	dma = pci_map_single(mdev->pdev, page_address(skb_frags[i].page) +
			     skb_frags[i].page_offset, frag_info->frag_size,
			     PCI_DMA_FROMDEVICE);
	rx_desc->data[i].addr = cpu_to_be64(dma);
	return 0;
}

static int mlx4_en_init_allocator(struct mlx4_en_priv *priv,
				  struct mlx4_en_rx_ring *ring)
{
	struct mlx4_en_rx_alloc *page_alloc;
	int i;

	for (i = 0; i < priv->num_frags; i++) {
		page_alloc = &ring->page_alloc[i];
		page_alloc->page = alloc_pages(GFP_ATOMIC | __GFP_COMP,
					       MLX4_EN_ALLOC_ORDER);
		if (!page_alloc->page)
			goto out;

		page_alloc->offset = priv->frag_info[i].frag_align;
		mlx4_dbg(DRV, priv, "Initialized allocator:%d with page:%p\n",
			 i, page_alloc->page);
	}
	return 0;

out:
	while (i--) {
		page_alloc = &ring->page_alloc[i];
		put_page(page_alloc->page);
		page_alloc->page = NULL;
	}
	return -ENOMEM;
}

static void mlx4_en_destroy_allocator(struct mlx4_en_priv *priv,
				      struct mlx4_en_rx_ring *ring)
{
	struct mlx4_en_rx_alloc *page_alloc;
	int i;

	for (i = 0; i < priv->num_frags; i++) {
		page_alloc = &ring->page_alloc[i];
		mlx4_dbg(DRV, priv, "Freeing allocator:%d count:%d\n",
			 i, page_count(page_alloc->page));

		put_page(page_alloc->page);
		page_alloc->page = NULL;
	}
}


static void mlx4_en_init_rx_desc(struct mlx4_en_priv *priv,
				 struct mlx4_en_rx_ring *ring, int index)
{
	struct mlx4_en_rx_desc *rx_desc = ring->buf + ring->stride * index;
	struct skb_frag_struct *skb_frags = ring->rx_info +
					    (index << priv->log_rx_info);
	int possible_frags;
	int i;

	/* Pre-link descriptor */
	rx_desc->next.next_wqe_index = cpu_to_be16((index + 1) & ring->size_mask);

	/* Set size and memtype fields */
	for (i = 0; i < priv->num_frags; i++) {
		skb_frags[i].size = priv->frag_info[i].frag_size;
		rx_desc->data[i].byte_count =
			cpu_to_be32(priv->frag_info[i].frag_size);
		rx_desc->data[i].lkey = cpu_to_be32(priv->mdev->mr.key);
	}

	/* If the number of used fragments does not fill up the ring stride,
	 * remaining (unused) fragments must be padded with null address/size
	 * and a special memory key */
	possible_frags = (ring->stride - sizeof(struct mlx4_en_rx_desc)) / DS_SIZE;
	for (i = priv->num_frags; i < possible_frags; i++) {
		rx_desc->data[i].byte_count = 0;
		rx_desc->data[i].lkey = cpu_to_be32(MLX4_EN_MEMTYPE_PAD);
		rx_desc->data[i].addr = 0;
	}
}


static int mlx4_en_prepare_rx_desc(struct mlx4_en_priv *priv,
				   struct mlx4_en_rx_ring *ring, int index)
{
	struct mlx4_en_rx_desc *rx_desc = ring->buf + (index * ring->stride);
	struct skb_frag_struct *skb_frags = ring->rx_info +
					    (index << priv->log_rx_info);
	int i;

	for (i = 0; i < priv->num_frags; i++)
		if (mlx4_en_alloc_frag(priv, rx_desc, skb_frags, ring->page_alloc, i))
			goto err;

	return 0;

err:
	while (i--)
		put_page(skb_frags[i].page);
	return -ENOMEM;
}

static inline void mlx4_en_update_rx_prod_db(struct mlx4_en_rx_ring *ring)
{
	*ring->wqres.db.db = cpu_to_be32(ring->prod & 0xffff);
}

static int mlx4_en_fill_rx_buffers(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rx_ring *ring;
	int ring_ind;
	int buf_ind;

	for (buf_ind = 0; buf_ind < priv->prof->rx_ring_size; buf_ind++) {
		for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
			ring = &priv->rx_ring[ring_ind];

			if (mlx4_en_prepare_rx_desc(priv, ring,
						    ring->actual_size)) {
				if (ring->actual_size < MLX4_EN_MIN_RX_SIZE) {
					mlx4_err(mdev, "Failed to allocate "
						       "enough rx buffers\n");
					return -ENOMEM;
				} else {
					if (netif_msg_rx_err(priv))
						mlx4_warn(mdev,
							  "Only %d buffers allocated\n",
							  ring->actual_size);
					goto out;
				}
			}
			ring->actual_size++;
			ring->prod++;
		}
	}
out:
	return 0;
}

static int mlx4_en_fill_rx_buf(struct net_device *dev,
			       struct mlx4_en_rx_ring *ring)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int num = 0;
	int err;

	while ((u32) (ring->prod - ring->cons) < ring->actual_size) {
		err = mlx4_en_prepare_rx_desc(priv, ring, ring->prod &
					      ring->size_mask);
		if (err) {
			if (netif_msg_rx_err(priv))
				mlx4_warn(priv->mdev,
					  "Failed preparing rx descriptor\n");
			priv->port_stats.rx_alloc_failed++;
			break;
		}
		++num;
		++ring->prod;
	}
	if ((u32) (ring->prod - ring->cons) == ring->size)
		ring->full = 1;

	return num;
}

static void mlx4_en_free_rx_buf(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct skb_frag_struct *skb_frags;
	struct mlx4_en_rx_desc *rx_desc;
	dma_addr_t dma;
	int index;
	int nr;

	mlx4_dbg(DRV, priv, "Freeing Rx buf - cons:%d prod:%d\n",
			ring->cons, ring->prod);

	/* Unmap and free Rx buffers */
	BUG_ON((u32) (ring->prod - ring->cons) > ring->size);
	while (ring->cons != ring->prod) {
		index = ring->cons & ring->size_mask;
		rx_desc = ring->buf + (index << ring->log_stride);
		skb_frags = ring->rx_info + (index << priv->log_rx_info);
		mlx4_dbg(DRV, priv, "Processing descriptor:%d\n", index);

		for (nr = 0; nr < priv->num_frags; nr++) {
			mlx4_dbg(DRV, priv, "Freeing fragment:%d\n", nr);
			dma = be64_to_cpu(rx_desc->data[nr].addr);

			mlx4_dbg(DRV, priv, "Unmaping buffer at dma:0x%llx\n", (u64) dma);
			pci_unmap_single(mdev->pdev, dma, skb_frags[nr].size,
					 PCI_DMA_FROMDEVICE);
			put_page(skb_frags[nr].page);
		}
		++ring->cons;
	}
}


void mlx4_en_rx_refill(struct work_struct *work)
{
	struct delayed_work *delay = container_of(work, struct delayed_work, work);
	struct mlx4_en_priv *priv = container_of(delay, struct mlx4_en_priv,
						 refill_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;
	struct mlx4_en_rx_ring *ring;
	int need_refill = 0;
	int i;

	mutex_lock(&mdev->state_lock);
	if (!mdev->device_up || !priv->port_up)
		goto out;

	/* We only get here if there are no receive buffers, so we can't race
	 * with Rx interrupts while filling buffers */
	for (i = 0; i < priv->rx_ring_num; i++) {
		ring = &priv->rx_ring[i];
		if (ring->need_refill) {
			if (mlx4_en_fill_rx_buf(dev, ring)) {
				ring->need_refill = 0;
				mlx4_en_update_rx_prod_db(ring);
			} else
				need_refill = 1;
		}
	}
	if (need_refill)
		queue_delayed_work(mdev->workqueue, &priv->refill_task, HZ);

out:
	mutex_unlock(&mdev->state_lock);
}


int mlx4_en_create_rx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_rx_ring *ring, u32 size, u16 stride)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;
	int tmp;

	/* Sanity check SRQ size before proceeding */
	if (size >= mdev->dev->caps.max_srq_wqes)
		return -EINVAL;

	ring->prod = 0;
	ring->cons = 0;
	ring->size = size;
	ring->size_mask = size - 1;
	ring->stride = stride;
	ring->log_stride = ffs(ring->stride) - 1;
	ring->buf_size = ring->size * ring->stride;

	tmp = size * roundup_pow_of_two(MLX4_EN_MAX_RX_FRAGS *
					sizeof(struct skb_frag_struct));
	ring->rx_info = vmalloc(tmp);
	if (!ring->rx_info) {
		mlx4_err(mdev, "Failed allocating rx_info ring\n");
		return -ENOMEM;
	}
	mlx4_dbg(DRV, priv, "Allocated rx_info ring at addr:%p size:%d\n",
		 ring->rx_info, tmp);

	err = mlx4_alloc_hwq_res(mdev->dev, &ring->wqres,
				 ring->buf_size, 2 * PAGE_SIZE);
	if (err)
		goto err_ring;

	err = mlx4_en_map_buffer(&ring->wqres.buf);
	if (err) {
		mlx4_err(mdev, "Failed to map RX buffer\n");
		goto err_hwq;
	}
	ring->buf = ring->wqres.buf.direct.buf;

	/* Configure lro mngr */
	memset(&ring->lro, 0, sizeof(struct net_lro_mgr));
	ring->lro.dev = priv->dev;
	ring->lro.features = LRO_F_NAPI;
	ring->lro.frag_align_pad = NET_IP_ALIGN;
	ring->lro.ip_summed = CHECKSUM_UNNECESSARY;
	ring->lro.ip_summed_aggr = CHECKSUM_UNNECESSARY;
	ring->lro.max_desc = mdev->profile.num_lro;
	ring->lro.max_aggr = MAX_SKB_FRAGS;
	ring->lro.lro_arr = kzalloc(mdev->profile.num_lro *
				    sizeof(struct net_lro_desc),
				    GFP_KERNEL);
	if (!ring->lro.lro_arr) {
		mlx4_err(mdev, "Failed to allocate lro array\n");
		goto err_map;
	}
	ring->lro.get_frag_header = mlx4_en_get_frag_header;

	return 0;

err_map:
	mlx4_en_unmap_buffer(&ring->wqres.buf);
err_hwq:
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
err_ring:
	vfree(ring->rx_info);
	ring->rx_info = NULL;
	return err;
}

int mlx4_en_activate_rx_rings(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_wqe_srq_next_seg *next;
	struct mlx4_en_rx_ring *ring;
	int i;
	int ring_ind;
	int err;
	int stride = roundup_pow_of_two(sizeof(struct mlx4_en_rx_desc) +
					DS_SIZE * priv->num_frags);
	int max_gs = (stride - sizeof(struct mlx4_wqe_srq_next_seg)) / DS_SIZE;

	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
		ring = &priv->rx_ring[ring_ind];

		ring->prod = 0;
		ring->cons = 0;
		ring->actual_size = 0;
		ring->cqn = priv->rx_cq[ring_ind].mcq.cqn;

		ring->stride = stride;
		ring->log_stride = ffs(ring->stride) - 1;
		ring->buf_size = ring->size * ring->stride;

		memset(ring->buf, 0, ring->buf_size);
		mlx4_en_update_rx_prod_db(ring);

		/* Initailize all descriptors */
		for (i = 0; i < ring->size; i++)
			mlx4_en_init_rx_desc(priv, ring, i);

		/* Initialize page allocators */
		err = mlx4_en_init_allocator(priv, ring);
		if (err) {
			 mlx4_err(mdev, "Failed initializing ring allocator\n");
			 goto err_allocator;
		}

		/* Fill Rx buffers */
		ring->full = 0;
	}
	err = mlx4_en_fill_rx_buffers(priv);
	if (err)
		goto err_buffers;

	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
		ring = &priv->rx_ring[ring_ind];

		mlx4_en_update_rx_prod_db(ring);

		/* Configure SRQ representing the ring */
		ring->srq.max    = ring->size;
		ring->srq.max_gs = max_gs;
		ring->srq.wqe_shift = ilog2(ring->stride);

		for (i = 0; i < ring->srq.max; ++i) {
			next = get_wqe(ring, i);
			next->next_wqe_index =
			cpu_to_be16((i + 1) & (ring->srq.max - 1));
		}

		err = mlx4_srq_alloc(mdev->dev, mdev->priv_pdn, &ring->wqres.mtt,
				     ring->wqres.db.dma, &ring->srq);
		if (err){
			mlx4_err(mdev, "Failed to allocate srq\n");
			goto err_srq;
		}
		ring->srq.event = mlx4_en_srq_event;
	}

	return 0;

err_srq:
	while (ring_ind >= 0) {
		ring = &priv->rx_ring[ring_ind];
		mlx4_srq_free(mdev->dev, &ring->srq);
		ring_ind--;
	}

err_buffers:
	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++)
		mlx4_en_free_rx_buf(priv, &priv->rx_ring[ring_ind]);

	ring_ind = priv->rx_ring_num - 1;
err_allocator:
	while (ring_ind >= 0) {
		mlx4_en_destroy_allocator(priv, &priv->rx_ring[ring_ind]);
		ring_ind--;
	}
	return err;
}

void mlx4_en_destroy_rx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	kfree(ring->lro.lro_arr);
	mlx4_en_unmap_buffer(&ring->wqres.buf);
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
	vfree(ring->rx_info);
	ring->rx_info = NULL;
}

void mlx4_en_deactivate_rx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	mlx4_srq_free(mdev->dev, &ring->srq);
	mlx4_en_free_rx_buf(priv, ring);
	mlx4_en_destroy_allocator(priv, ring);
}


/* Unmap a completed descriptor and free unused pages */
static int mlx4_en_complete_rx_desc(struct mlx4_en_priv *priv,
				    struct mlx4_en_rx_desc *rx_desc,
				    struct skb_frag_struct *skb_frags,
				    struct skb_frag_struct *skb_frags_rx,
				    struct mlx4_en_rx_alloc *page_alloc,
				    int length)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_frag_info *frag_info;
	int nr;
	dma_addr_t dma;

	/* Collect used fragments while replacing them in the HW descirptors */
	for (nr = 0; nr < priv->num_frags; nr++) {
		frag_info = &priv->frag_info[nr];
		if (length <= frag_info->frag_prefix_size)
			break;

		/* Save page reference in skb */
		skb_frags_rx[nr].page = skb_frags[nr].page;
		skb_frags_rx[nr].size = skb_frags[nr].size;
		skb_frags_rx[nr].page_offset = skb_frags[nr].page_offset;
		dma = be64_to_cpu(rx_desc->data[nr].addr);

		/* Allocate a replacement page */
		if (mlx4_en_alloc_frag(priv, rx_desc, skb_frags, page_alloc, nr))
			goto fail;

		/* Unmap buffer */
		pci_unmap_single(mdev->pdev, dma, skb_frags[nr].size,
				 PCI_DMA_FROMDEVICE);
	}
	/* Adjust size of last fragment to match actual length */
	skb_frags_rx[nr - 1].size = length -
		priv->frag_info[nr - 1].frag_prefix_size;
	return nr;

fail:
	/* Drop all accumulated fragments (which have already been replaced in
	 * the descriptor) of this packet; remaining fragments are reused... */
	while (nr > 0) {
		nr--;
		put_page(skb_frags_rx[nr].page);
	}
	return 0;
}


static struct sk_buff *mlx4_en_rx_skb(struct mlx4_en_priv *priv,
				      struct mlx4_en_rx_desc *rx_desc,
				      struct skb_frag_struct *skb_frags,
				      struct mlx4_en_rx_alloc *page_alloc,
				      unsigned int length)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct sk_buff *skb;
	void *va;
	int used_frags;
	dma_addr_t dma;

	skb = dev_alloc_skb(SMALL_PACKET_SIZE + NET_IP_ALIGN);
	if (!skb) {
		mlx4_dbg(RX_ERR, priv, "Failed allocating skb\n");
		return NULL;
	}
	skb->dev = priv->dev;
	skb_reserve(skb, NET_IP_ALIGN);
	skb->len = length;
	skb->truesize = length + sizeof(struct sk_buff);

	/* Get pointer to first fragment so we could copy the headers into the
	 * (linear part of the) skb */
	va = page_address(skb_frags[0].page) + skb_frags[0].page_offset;

	if (length <= SMALL_PACKET_SIZE) {
		/* We are copying all relevant data to the skb - temporarily
		 * synch buffers for the copy */
		dma = be64_to_cpu(rx_desc->data[0].addr);
		dma_sync_single_range_for_cpu(&mdev->pdev->dev, dma, 0,
					      length, DMA_FROM_DEVICE);
		skb_copy_to_linear_data(skb, va, length);
		dma_sync_single_range_for_device(&mdev->pdev->dev, dma, 0,
						 length, DMA_FROM_DEVICE);
		skb->tail += length;
	} else {

		/* Move relevant fragments to skb */
		used_frags = mlx4_en_complete_rx_desc(priv, rx_desc, skb_frags,
						      skb_shinfo(skb)->frags,
						      page_alloc, length);
		skb_shinfo(skb)->nr_frags = used_frags;

		/* Copy headers into the skb linear buffer */
		memcpy(skb->data, va, HEADER_COPY_SIZE);
		skb->tail += HEADER_COPY_SIZE;

		/* Skip headers in first fragment */
		skb_shinfo(skb)->frags[0].page_offset += HEADER_COPY_SIZE;

		/* Adjust size of first fragment */
		skb_shinfo(skb)->frags[0].size -= HEADER_COPY_SIZE;
		skb->data_len = length - HEADER_COPY_SIZE;
	}
	return skb;
}

static void mlx4_en_copy_desc(struct mlx4_en_priv *priv,
			      struct mlx4_en_rx_ring *ring,
			      int from, int to, int num)
{
	struct skb_frag_struct *skb_frags_from;
	struct skb_frag_struct *skb_frags_to;
	struct mlx4_en_rx_desc *rx_desc_from;
	struct mlx4_en_rx_desc *rx_desc_to;
	int from_index, to_index;
	int nr, i;

	for (i = 0; i < num; i++) {
		from_index = (from + i) & ring->size_mask;
		to_index = (to + i) & ring->size_mask;
		skb_frags_from = ring->rx_info + (from_index << priv->log_rx_info);
		skb_frags_to = ring->rx_info + (to_index << priv->log_rx_info);
		rx_desc_from = ring->buf + (from_index << ring->log_stride);
		rx_desc_to = ring->buf + (to_index << ring->log_stride);

		for (nr = 0; nr < priv->num_frags; nr++) {
			skb_frags_to[nr].page = skb_frags_from[nr].page;
			skb_frags_to[nr].page_offset = skb_frags_from[nr].page_offset;
			rx_desc_to->data[nr].addr = rx_desc_from->data[nr].addr;
		}
	}
}


int mlx4_en_process_rx_cq(struct net_device *dev, struct mlx4_en_cq *cq, int budget)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_cqe *cqe;
	struct mlx4_en_rx_ring *ring = &priv->rx_ring[cq->ring];
	struct skb_frag_struct *skb_frags;
	struct skb_frag_struct lro_frags[MLX4_EN_MAX_RX_FRAGS];
	struct mlx4_en_rx_desc *rx_desc;
	struct sk_buff *skb;
	int index;
	int nr;
	unsigned int length;
	int polled = 0;
	int ip_summed;

	if (!priv->port_up)
		return 0;

	/* We assume a 1:1 mapping between CQEs and Rx descriptors, so Rx
	 * descriptor offset can be deduced from the CQE index instead of
	 * reading 'cqe->index' */
	index = cq->mcq.cons_index & ring->size_mask;
	cqe = &cq->buf[index];

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
		    cq->mcq.cons_index & cq->size)) {

		skb_frags = ring->rx_info + (index << priv->log_rx_info);
		rx_desc = ring->buf + (index << ring->log_stride);

		/*
		 * make sure we read the CQE after we read the ownership bit
		 */
		rmb();

		/* Drop packet on bad receive or bad checksum */
		if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
						MLX4_CQE_OPCODE_ERROR)) {
			mlx4_err(mdev, "CQE completed in error - vendor "
				  "syndrom:%d syndrom:%d\n",
				  ((struct mlx4_err_cqe *) cqe)->vendor_err_syndrome,
				  ((struct mlx4_err_cqe *) cqe)->syndrome);
			goto next;
		}
		if (unlikely(cqe->badfcs_enc & MLX4_CQE_BAD_FCS)) {
			mlx4_dbg(RX_ERR, priv, "Accepted frame with bad FCS\n");
			goto next;
		}

		/*
		 * Packet is OK - process it.
		 */
		length = be32_to_cpu(cqe->byte_cnt);
		ring->bytes += length;
		ring->packets++;

		if (likely(priv->rx_csum)) {
			if ((cqe->status & cpu_to_be16(MLX4_CQE_STATUS_IPOK)) &&
			    (cqe->checksum == cpu_to_be16(0xffff))) {
				priv->port_stats.rx_chksum_good++;
				/* This packet is eligible for LRO if it is:
				 * - DIX Ethernet (type interpretation)
				 * - TCP/IP (v4)
				 * - without IP options
				 * - not an IP fragment */
				if (mlx4_en_can_lro(cqe->status) &&
				    dev->features & NETIF_F_LRO) {

					nr = mlx4_en_complete_rx_desc(
						priv, rx_desc,
						skb_frags, lro_frags,
						ring->page_alloc, length);
					if (!nr)
						goto next;

					if (priv->vlgrp && (cqe->vlan_my_qpn &
							    cpu_to_be32(MLX4_CQE_VLAN_PRESENT_MASK))) {
						lro_vlan_hwaccel_receive_frags(
						       &ring->lro, lro_frags,
						       length, length,
						       priv->vlgrp,
						       be16_to_cpu(cqe->sl_vid),
						       NULL, 0);
					} else
						lro_receive_frags(&ring->lro,
								  lro_frags,
								  length,
								  length,
								  NULL, 0);

					goto next;
				}

				/* LRO not possible, complete processing here */
				ip_summed = CHECKSUM_UNNECESSARY;
				INC_PERF_COUNTER(priv->pstats.lro_misses);
			} else {
				ip_summed = CHECKSUM_NONE;
				priv->port_stats.rx_chksum_none++;
			}
		} else {
			ip_summed = CHECKSUM_NONE;
			priv->port_stats.rx_chksum_none++;
		}

		skb = mlx4_en_rx_skb(priv, rx_desc, skb_frags,
				     ring->page_alloc, length);
		if (!skb) {
			priv->stats.rx_dropped++;
			goto next;
		}

		skb->ip_summed = ip_summed;
		skb->protocol = eth_type_trans(skb, dev);

		/* Push it up the stack */
		if (priv->vlgrp && (be32_to_cpu(cqe->vlan_my_qpn) &
				    MLX4_CQE_VLAN_PRESENT_MASK)) {
			vlan_hwaccel_receive_skb(skb, priv->vlgrp,
						be16_to_cpu(cqe->sl_vid));
		} else
			netif_receive_skb(skb);

next:
		++cq->mcq.cons_index;
		index = (cq->mcq.cons_index) & ring->size_mask;
		cqe = &cq->buf[index];
		if (++polled == budget) {
			/* We are here because we reached the NAPI budget -
			 * flush only pending LRO sessions */
			lro_flush_all(&ring->lro);
			goto out;
		}
	}

	/* If CQ is empty flush all LRO sessions unconditionally */
	lro_flush_all(&ring->lro);

out:
	AVG_PERF_COUNTER(priv->pstats.rx_coal_avg, polled);
	mlx4_cq_set_ci(&cq->mcq);
	wmb(); /* ensure HW sees CQ consumer before we post new buffers */
	ring->cons = cq->mcq.cons_index;
	ring->prod += polled; /* Polled descriptors were realocated in place */
	if (unlikely(!ring->full)) {
		mlx4_en_copy_desc(priv, ring, ring->cons - polled,
				  ring->prod - polled, polled);
		mlx4_en_fill_rx_buf(dev, ring);
	}
	mlx4_en_update_rx_prod_db(ring);
	return polled;
}


void mlx4_en_rx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);

	if (priv->port_up)
		netif_rx_schedule(&cq->napi);
	else
		mlx4_en_arm_cq(priv, cq);
}

/* Rx CQ polling - called by NAPI */
int mlx4_en_poll_rx_cq(struct napi_struct *napi, int budget)
{
	struct mlx4_en_cq *cq = container_of(napi, struct mlx4_en_cq, napi);
	struct net_device *dev = cq->dev;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int done;

	done = mlx4_en_process_rx_cq(dev, cq, budget);

	/* If we used up all the quota - we're probably not done yet... */
	if (done == budget)
		INC_PERF_COUNTER(priv->pstats.napi_quota);
	else {
		/* Done for now */
		netif_rx_complete(napi);
		mlx4_en_arm_cq(priv, cq);
	}
	return done;
}


/* Calculate the last offset position that accomodates a full fragment
 * (assuming fagment size = stride-align) */
static int mlx4_en_last_alloc_offset(struct mlx4_en_priv *priv, u16 stride, u16 align)
{
	u16 res = MLX4_EN_ALLOC_SIZE % stride;
	u16 offset = MLX4_EN_ALLOC_SIZE - stride - res + align;

	mlx4_dbg(DRV, priv, "Calculated last offset for stride:%d align:%d "
			    "res:%d offset:%d\n", stride, align, res, offset);
	return offset;
}


static int frag_sizes[] = {
	FRAG_SZ0,
	FRAG_SZ1,
	FRAG_SZ2,
	FRAG_SZ3
};

void mlx4_en_calc_rx_buf(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int eff_mtu = dev->mtu + ETH_HLEN + VLAN_HLEN + ETH_LLC_SNAP_SIZE;
	int buf_size = 0;
	int i = 0;

	while (buf_size < eff_mtu) {
		priv->frag_info[i].frag_size =
			(eff_mtu > buf_size + frag_sizes[i]) ?
				frag_sizes[i] : eff_mtu - buf_size;
		priv->frag_info[i].frag_prefix_size = buf_size;
		if (!i)	{
			priv->frag_info[i].frag_align = NET_IP_ALIGN;
			priv->frag_info[i].frag_stride =
				ALIGN(frag_sizes[i] + NET_IP_ALIGN, SMP_CACHE_BYTES);
		} else {
			priv->frag_info[i].frag_align = 0;
			priv->frag_info[i].frag_stride =
				ALIGN(frag_sizes[i], SMP_CACHE_BYTES);
		}
		priv->frag_info[i].last_offset = mlx4_en_last_alloc_offset(
						priv, priv->frag_info[i].frag_stride,
						priv->frag_info[i].frag_align);
		buf_size += priv->frag_info[i].frag_size;
		i++;
	}

	priv->num_frags = i;
	priv->rx_skb_size = eff_mtu;
	priv->log_rx_info = ROUNDUP_LOG2(i * sizeof(struct skb_frag_struct));

	mlx4_dbg(DRV, priv, "Rx buffer scatter-list (effective-mtu:%d "
		  "num_frags:%d):\n", eff_mtu, priv->num_frags);
	for (i = 0; i < priv->num_frags; i++) {
		mlx4_dbg(DRV, priv, "  frag:%d - size:%d prefix:%d align:%d "
				"stride:%d last_offset:%d\n", i,
				priv->frag_info[i].frag_size,
				priv->frag_info[i].frag_prefix_size,
				priv->frag_info[i].frag_align,
				priv->frag_info[i].frag_stride,
				priv->frag_info[i].last_offset);
	}
}

/* RSS related functions */

/* Calculate rss size and map each entry in rss table to rx ring */
void mlx4_en_set_default_rss_map(struct mlx4_en_priv *priv,
				 struct mlx4_en_rss_map *rss_map,
				 int num_entries, int num_rings)
{
	int i;

	rss_map->size = roundup_pow_of_two(num_entries);
	mlx4_dbg(DRV, priv, "Setting default RSS map of %d entires\n",
		 rss_map->size);

	for (i = 0; i < rss_map->size; i++) {
		rss_map->map[i] = i % num_rings;
		mlx4_dbg(DRV, priv, "Entry %d ---> ring %d\n", i, rss_map->map[i]);
	}
}

static void mlx4_en_sqp_event(struct mlx4_qp *qp, enum mlx4_event event)
{
    return;
}


static int mlx4_en_config_rss_qp(struct mlx4_en_priv *priv,
				 int qpn, int srqn, int cqn,
				 enum mlx4_qp_state *state,
				 struct mlx4_qp *qp)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_qp_context *context;
	int err = 0;

	context = kmalloc(sizeof *context , GFP_KERNEL);
	if (!context) {
		mlx4_err(mdev, "Failed to allocate qp context\n");
		return -ENOMEM;
	}

	err = mlx4_qp_alloc(mdev->dev, qpn, qp);
	if (err) {
		mlx4_err(mdev, "Failed to allocate qp #%d\n", qpn);
		goto out;
		return err;
	}
	qp->event = mlx4_en_sqp_event;

	memset(context, 0, sizeof *context);
	mlx4_en_fill_qp_context(priv, 0, 0, 0, 0, qpn, cqn, srqn, context);

	err = mlx4_qp_to_ready(mdev->dev, &priv->res.mtt, context, qp, state);
	if (err) {
		mlx4_qp_remove(mdev->dev, qp);
		mlx4_qp_free(mdev->dev, qp);
	}
out:
	kfree(context);
	return err;
}

/* Allocate rx qp's and configure them according to rss map */
int mlx4_en_config_rss_steer(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rss_map *rss_map = &priv->rss_map;
	struct mlx4_qp_context context;
	struct mlx4_en_rss_context *rss_context;
	void *ptr;
	int rss_xor = mdev->profile.rss_xor;
	u8 rss_mask = mdev->profile.rss_mask;
	int i, srqn, qpn, cqn;
	int err = 0;
	int good_qps = 0;

	mlx4_dbg(DRV, priv, "Configuring rss steering for port %u\n", priv->port);
	err = mlx4_qp_reserve_range(mdev->dev, rss_map->size,
				    rss_map->size, &rss_map->base_qpn);
	if (err) {
		mlx4_err(mdev, "Failed reserving %d qps for port %u\n",
			 rss_map->size, priv->port);
		return err;
	}

	for (i = 0; i < rss_map->size; i++) {
		cqn = priv->rx_ring[rss_map->map[i]].cqn;
		srqn = priv->rx_ring[rss_map->map[i]].srq.srqn;
		qpn = rss_map->base_qpn + i;
		err = mlx4_en_config_rss_qp(priv, qpn, srqn, cqn,
					    &rss_map->state[i],
					    &rss_map->qps[i]);
		if (err)
			goto rss_err;

		++good_qps;
	}

	/* Configure RSS indirection qp */
	err = mlx4_qp_reserve_range(mdev->dev, 1, 1, &priv->base_qpn);
	if (err) {
		mlx4_err(mdev, "Failed to reserve range for RSS "
			       "indirection qp\n");
		goto rss_err;
	}
	err = mlx4_qp_alloc(mdev->dev, priv->base_qpn, &rss_map->indir_qp);
	if (err) {
		mlx4_err(mdev, "Failed to allocate RSS indirection QP\n");
		goto reserve_err;
	}
	rss_map->indir_qp.event = mlx4_en_sqp_event;
	mlx4_en_fill_qp_context(priv, 0, 0, 0, 1, priv->base_qpn,
				priv->rx_ring[0].cqn, 0, &context);

	ptr = ((void *) &context) + 0x3c;
	rss_context = (struct mlx4_en_rss_context *) ptr;
	rss_context->base_qpn = cpu_to_be32(ilog2(rss_map->size) << 24 |
					    (rss_map->base_qpn));
	rss_context->default_qpn = cpu_to_be32(rss_map->base_qpn);
	rss_context->hash_fn = rss_xor & 0x3;
	rss_context->flags = rss_mask << 2;

	err = mlx4_qp_to_ready(mdev->dev, &priv->res.mtt, &context,
			       &rss_map->indir_qp, &rss_map->indir_state);
	if (err)
		goto indir_err;

	return 0;

indir_err:
	mlx4_qp_modify(mdev->dev, NULL, rss_map->indir_state,
		       MLX4_QP_STATE_RST, NULL, 0, 0, &rss_map->indir_qp);
	mlx4_qp_remove(mdev->dev, &rss_map->indir_qp);
	mlx4_qp_free(mdev->dev, &rss_map->indir_qp);
reserve_err:
	mlx4_qp_release_range(mdev->dev, priv->base_qpn, 1);
rss_err:
	for (i = 0; i < good_qps; i++) {
		mlx4_qp_modify(mdev->dev, NULL, rss_map->state[i],
			       MLX4_QP_STATE_RST, NULL, 0, 0, &rss_map->qps[i]);
		mlx4_qp_remove(mdev->dev, &rss_map->qps[i]);
		mlx4_qp_free(mdev->dev, &rss_map->qps[i]);
	}
	mlx4_qp_release_range(mdev->dev, rss_map->base_qpn, rss_map->size);
	return err;
}

void mlx4_en_release_rss_steer(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rss_map *rss_map = &priv->rss_map;
	int i;

	mlx4_qp_modify(mdev->dev, NULL, rss_map->indir_state,
		       MLX4_QP_STATE_RST, NULL, 0, 0, &rss_map->indir_qp);
	mlx4_qp_remove(mdev->dev, &rss_map->indir_qp);
	mlx4_qp_free(mdev->dev, &rss_map->indir_qp);
	mlx4_qp_release_range(mdev->dev, priv->base_qpn, 1);

	for (i = 0; i < rss_map->size; i++) {
		mlx4_qp_modify(mdev->dev, NULL, rss_map->state[i],
			       MLX4_QP_STATE_RST, NULL, 0, 0, &rss_map->qps[i]);
		mlx4_qp_remove(mdev->dev, &rss_map->qps[i]);
		mlx4_qp_free(mdev->dev, &rss_map->qps[i]);
	}
	mlx4_qp_release_range(mdev->dev, rss_map->base_qpn, rss_map->size);
}






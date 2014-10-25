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

#include <net/busy_poll.h>
#include <linux/mlx4/cq.h>
#include <linux/slab.h>
#include <linux/mlx4/qp.h>
#include <linux/skbuff.h>
#include <linux/rculist.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>
#include <linux/irq.h>

#include "mlx4_en.h"

static int mlx4_alloc_pages(struct mlx4_en_priv *priv,
			    struct mlx4_en_rx_alloc *page_alloc,
			    const struct mlx4_en_frag_info *frag_info,
			    gfp_t _gfp)
{
	int order;
	struct page *page;
	dma_addr_t dma;

	for (order = MLX4_EN_ALLOC_PREFER_ORDER; ;) {
		gfp_t gfp = _gfp;

		if (order)
			gfp |= __GFP_COMP | __GFP_NOWARN;
		page = alloc_pages(gfp, order);
		if (likely(page))
			break;
		if (--order < 0 ||
		    ((PAGE_SIZE << order) < frag_info->frag_size))
			return -ENOMEM;
	}
	dma = dma_map_page(priv->ddev, page, 0, PAGE_SIZE << order,
			   PCI_DMA_FROMDEVICE);
	if (dma_mapping_error(priv->ddev, dma)) {
		put_page(page);
		return -ENOMEM;
	}
	page_alloc->page_size = PAGE_SIZE << order;
	page_alloc->page = page;
	page_alloc->dma = dma;
	page_alloc->page_offset = frag_info->frag_align;
	/* Not doing get_page() for each frag is a big win
	 * on asymetric workloads. Note we can not use atomic_set().
	 */
	atomic_add(page_alloc->page_size / frag_info->frag_stride - 1,
		   &page->_count);
	return 0;
}

static int mlx4_en_alloc_frags(struct mlx4_en_priv *priv,
			       struct mlx4_en_rx_desc *rx_desc,
			       struct mlx4_en_rx_alloc *frags,
			       struct mlx4_en_rx_alloc *ring_alloc,
			       gfp_t gfp)
{
	struct mlx4_en_rx_alloc page_alloc[MLX4_EN_MAX_RX_FRAGS];
	const struct mlx4_en_frag_info *frag_info;
	struct page *page;
	dma_addr_t dma;
	int i;

	for (i = 0; i < priv->num_frags; i++) {
		frag_info = &priv->frag_info[i];
		page_alloc[i] = ring_alloc[i];
		page_alloc[i].page_offset += frag_info->frag_stride;

		if (page_alloc[i].page_offset + frag_info->frag_stride <=
		    ring_alloc[i].page_size)
			continue;

		if (mlx4_alloc_pages(priv, &page_alloc[i], frag_info, gfp))
			goto out;
	}

	for (i = 0; i < priv->num_frags; i++) {
		frags[i] = ring_alloc[i];
		dma = ring_alloc[i].dma + ring_alloc[i].page_offset;
		ring_alloc[i] = page_alloc[i];
		rx_desc->data[i].addr = cpu_to_be64(dma);
	}

	return 0;

out:
	while (i--) {
		frag_info = &priv->frag_info[i];
		if (page_alloc[i].page != ring_alloc[i].page) {
			dma_unmap_page(priv->ddev, page_alloc[i].dma,
				page_alloc[i].page_size, PCI_DMA_FROMDEVICE);
			page = page_alloc[i].page;
			atomic_set(&page->_count, 1);
			put_page(page);
		}
	}
	return -ENOMEM;
}

static void mlx4_en_free_frag(struct mlx4_en_priv *priv,
			      struct mlx4_en_rx_alloc *frags,
			      int i)
{
	const struct mlx4_en_frag_info *frag_info = &priv->frag_info[i];
	u32 next_frag_end = frags[i].page_offset + 2 * frag_info->frag_stride;


	if (next_frag_end > frags[i].page_size)
		dma_unmap_page(priv->ddev, frags[i].dma, frags[i].page_size,
			       PCI_DMA_FROMDEVICE);

	if (frags[i].page)
		put_page(frags[i].page);
}

static int mlx4_en_init_allocator(struct mlx4_en_priv *priv,
				  struct mlx4_en_rx_ring *ring)
{
	int i;
	struct mlx4_en_rx_alloc *page_alloc;

	for (i = 0; i < priv->num_frags; i++) {
		const struct mlx4_en_frag_info *frag_info = &priv->frag_info[i];

		if (mlx4_alloc_pages(priv, &ring->page_alloc[i],
				     frag_info, GFP_KERNEL))
			goto out;
	}
	return 0;

out:
	while (i--) {
		struct page *page;

		page_alloc = &ring->page_alloc[i];
		dma_unmap_page(priv->ddev, page_alloc->dma,
			       page_alloc->page_size, PCI_DMA_FROMDEVICE);
		page = page_alloc->page;
		atomic_set(&page->_count, 1);
		put_page(page);
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
		const struct mlx4_en_frag_info *frag_info = &priv->frag_info[i];

		page_alloc = &ring->page_alloc[i];
		en_dbg(DRV, priv, "Freeing allocator:%d count:%d\n",
		       i, page_count(page_alloc->page));

		dma_unmap_page(priv->ddev, page_alloc->dma,
				page_alloc->page_size, PCI_DMA_FROMDEVICE);
		while (page_alloc->page_offset + frag_info->frag_stride <
		       page_alloc->page_size) {
			put_page(page_alloc->page);
			page_alloc->page_offset += frag_info->frag_stride;
		}
		page_alloc->page = NULL;
	}
}

static void mlx4_en_init_rx_desc(struct mlx4_en_priv *priv,
				 struct mlx4_en_rx_ring *ring, int index)
{
	struct mlx4_en_rx_desc *rx_desc = ring->buf + ring->stride * index;
	int possible_frags;
	int i;

	/* Set size and memtype fields */
	for (i = 0; i < priv->num_frags; i++) {
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
				   struct mlx4_en_rx_ring *ring, int index,
				   gfp_t gfp)
{
	struct mlx4_en_rx_desc *rx_desc = ring->buf + (index * ring->stride);
	struct mlx4_en_rx_alloc *frags = ring->rx_info +
					(index << priv->log_rx_info);

	return mlx4_en_alloc_frags(priv, rx_desc, frags, ring->page_alloc, gfp);
}

static inline void mlx4_en_update_rx_prod_db(struct mlx4_en_rx_ring *ring)
{
	*ring->wqres.db.db = cpu_to_be32(ring->prod & 0xffff);
}

static void mlx4_en_free_rx_desc(struct mlx4_en_priv *priv,
				 struct mlx4_en_rx_ring *ring,
				 int index)
{
	struct mlx4_en_rx_alloc *frags;
	int nr;

	frags = ring->rx_info + (index << priv->log_rx_info);
	for (nr = 0; nr < priv->num_frags; nr++) {
		en_dbg(DRV, priv, "Freeing fragment:%d\n", nr);
		mlx4_en_free_frag(priv, frags, nr);
	}
}

static int mlx4_en_fill_rx_buffers(struct mlx4_en_priv *priv)
{
	struct mlx4_en_rx_ring *ring;
	int ring_ind;
	int buf_ind;
	int new_size;

	for (buf_ind = 0; buf_ind < priv->prof->rx_ring_size; buf_ind++) {
		for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
			ring = priv->rx_ring[ring_ind];

			if (mlx4_en_prepare_rx_desc(priv, ring,
						    ring->actual_size,
						    GFP_KERNEL)) {
				if (ring->actual_size < MLX4_EN_MIN_RX_SIZE) {
					en_err(priv, "Failed to allocate enough rx buffers\n");
					return -ENOMEM;
				} else {
					new_size = rounddown_pow_of_two(ring->actual_size);
					en_warn(priv, "Only %d buffers allocated reducing ring size to %d\n",
						ring->actual_size, new_size);
					goto reduce_rings;
				}
			}
			ring->actual_size++;
			ring->prod++;
		}
	}
	return 0;

reduce_rings:
	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
		ring = priv->rx_ring[ring_ind];
		while (ring->actual_size > new_size) {
			ring->actual_size--;
			ring->prod--;
			mlx4_en_free_rx_desc(priv, ring, ring->actual_size);
		}
	}

	return 0;
}

static void mlx4_en_free_rx_buf(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring)
{
	int index;

	en_dbg(DRV, priv, "Freeing Rx buf - cons:%d prod:%d\n",
	       ring->cons, ring->prod);

	/* Unmap and free Rx buffers */
	BUG_ON((u32) (ring->prod - ring->cons) > ring->actual_size);
	while (ring->cons != ring->prod) {
		index = ring->cons & ring->size_mask;
		en_dbg(DRV, priv, "Processing descriptor:%d\n", index);
		mlx4_en_free_rx_desc(priv, ring, index);
		++ring->cons;
	}
}

void mlx4_en_set_num_rx_rings(struct mlx4_en_dev *mdev)
{
	int i;
	int num_of_eqs;
	int num_rx_rings;
	struct mlx4_dev *dev = mdev->dev;

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH) {
		if (!dev->caps.comp_pool)
			num_of_eqs = max_t(int, MIN_RX_RINGS,
					   min_t(int,
						 dev->caps.num_comp_vectors,
						 DEF_RX_RINGS));
		else
			num_of_eqs = min_t(int, MAX_MSIX_P_PORT,
					   dev->caps.comp_pool/
					   dev->caps.num_ports) - 1;

		num_rx_rings = mlx4_low_memory_profile() ? MIN_RX_RINGS :
			min_t(int, num_of_eqs,
			      netif_get_num_default_rss_queues());
		mdev->profile.prof[i].rx_ring_num =
			rounddown_pow_of_two(num_rx_rings);
	}
}

int mlx4_en_create_rx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_rx_ring **pring,
			   u32 size, u16 stride, int node)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rx_ring *ring;
	int err = -ENOMEM;
	int tmp;

	ring = kzalloc_node(sizeof(*ring), GFP_KERNEL, node);
	if (!ring) {
		ring = kzalloc(sizeof(*ring), GFP_KERNEL);
		if (!ring) {
			en_err(priv, "Failed to allocate RX ring structure\n");
			return -ENOMEM;
		}
	}

	ring->prod = 0;
	ring->cons = 0;
	ring->size = size;
	ring->size_mask = size - 1;
	ring->stride = stride;
	ring->log_stride = ffs(ring->stride) - 1;
	ring->buf_size = ring->size * ring->stride + TXBB_SIZE;

	tmp = size * roundup_pow_of_two(MLX4_EN_MAX_RX_FRAGS *
					sizeof(struct mlx4_en_rx_alloc));
	ring->rx_info = vmalloc_node(tmp, node);
	if (!ring->rx_info) {
		ring->rx_info = vmalloc(tmp);
		if (!ring->rx_info) {
			err = -ENOMEM;
			goto err_ring;
		}
	}

	en_dbg(DRV, priv, "Allocated rx_info ring at addr:%p size:%d\n",
		 ring->rx_info, tmp);

	/* Allocate HW buffers on provided NUMA node */
	set_dev_node(&mdev->dev->pdev->dev, node);
	err = mlx4_alloc_hwq_res(mdev->dev, &ring->wqres,
				 ring->buf_size, 2 * PAGE_SIZE);
	set_dev_node(&mdev->dev->pdev->dev, mdev->dev->numa_node);
	if (err)
		goto err_info;

	err = mlx4_en_map_buffer(&ring->wqres.buf);
	if (err) {
		en_err(priv, "Failed to map RX buffer\n");
		goto err_hwq;
	}
	ring->buf = ring->wqres.buf.direct.buf;

	ring->hwtstamp_rx_filter = priv->hwtstamp_config.rx_filter;

	*pring = ring;
	return 0;

err_hwq:
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
err_info:
	vfree(ring->rx_info);
	ring->rx_info = NULL;
err_ring:
	kfree(ring);
	*pring = NULL;

	return err;
}

int mlx4_en_activate_rx_rings(struct mlx4_en_priv *priv)
{
	struct mlx4_en_rx_ring *ring;
	int i;
	int ring_ind;
	int err;
	int stride = roundup_pow_of_two(sizeof(struct mlx4_en_rx_desc) +
					DS_SIZE * priv->num_frags);

	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
		ring = priv->rx_ring[ring_ind];

		ring->prod = 0;
		ring->cons = 0;
		ring->actual_size = 0;
		ring->cqn = priv->rx_cq[ring_ind]->mcq.cqn;

		ring->stride = stride;
		if (ring->stride <= TXBB_SIZE)
			ring->buf += TXBB_SIZE;

		ring->log_stride = ffs(ring->stride) - 1;
		ring->buf_size = ring->size * ring->stride;

		memset(ring->buf, 0, ring->buf_size);
		mlx4_en_update_rx_prod_db(ring);

		/* Initialize all descriptors */
		for (i = 0; i < ring->size; i++)
			mlx4_en_init_rx_desc(priv, ring, i);

		/* Initialize page allocators */
		err = mlx4_en_init_allocator(priv, ring);
		if (err) {
			en_err(priv, "Failed initializing ring allocator\n");
			if (ring->stride <= TXBB_SIZE)
				ring->buf -= TXBB_SIZE;
			ring_ind--;
			goto err_allocator;
		}
	}
	err = mlx4_en_fill_rx_buffers(priv);
	if (err)
		goto err_buffers;

	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++) {
		ring = priv->rx_ring[ring_ind];

		ring->size_mask = ring->actual_size - 1;
		mlx4_en_update_rx_prod_db(ring);
	}

	return 0;

err_buffers:
	for (ring_ind = 0; ring_ind < priv->rx_ring_num; ring_ind++)
		mlx4_en_free_rx_buf(priv, priv->rx_ring[ring_ind]);

	ring_ind = priv->rx_ring_num - 1;
err_allocator:
	while (ring_ind >= 0) {
		if (priv->rx_ring[ring_ind]->stride <= TXBB_SIZE)
			priv->rx_ring[ring_ind]->buf -= TXBB_SIZE;
		mlx4_en_destroy_allocator(priv, priv->rx_ring[ring_ind]);
		ring_ind--;
	}
	return err;
}

void mlx4_en_destroy_rx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring **pring,
			     u32 size, u16 stride)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rx_ring *ring = *pring;

	mlx4_en_unmap_buffer(&ring->wqres.buf);
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, size * stride + TXBB_SIZE);
	vfree(ring->rx_info);
	ring->rx_info = NULL;
	kfree(ring);
	*pring = NULL;
#ifdef CONFIG_RFS_ACCEL
	mlx4_en_cleanup_filters(priv);
#endif
}

void mlx4_en_deactivate_rx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring)
{
	mlx4_en_free_rx_buf(priv, ring);
	if (ring->stride <= TXBB_SIZE)
		ring->buf -= TXBB_SIZE;
	mlx4_en_destroy_allocator(priv, ring);
}


static int mlx4_en_complete_rx_desc(struct mlx4_en_priv *priv,
				    struct mlx4_en_rx_desc *rx_desc,
				    struct mlx4_en_rx_alloc *frags,
				    struct sk_buff *skb,
				    int length)
{
	struct skb_frag_struct *skb_frags_rx = skb_shinfo(skb)->frags;
	struct mlx4_en_frag_info *frag_info;
	int nr;
	dma_addr_t dma;

	/* Collect used fragments while replacing them in the HW descriptors */
	for (nr = 0; nr < priv->num_frags; nr++) {
		frag_info = &priv->frag_info[nr];
		if (length <= frag_info->frag_prefix_size)
			break;
		if (!frags[nr].page)
			goto fail;

		dma = be64_to_cpu(rx_desc->data[nr].addr);
		dma_sync_single_for_cpu(priv->ddev, dma, frag_info->frag_size,
					DMA_FROM_DEVICE);

		/* Save page reference in skb */
		__skb_frag_set_page(&skb_frags_rx[nr], frags[nr].page);
		skb_frag_size_set(&skb_frags_rx[nr], frag_info->frag_size);
		skb_frags_rx[nr].page_offset = frags[nr].page_offset;
		skb->truesize += frag_info->frag_stride;
		frags[nr].page = NULL;
	}
	/* Adjust size of last fragment to match actual length */
	if (nr > 0)
		skb_frag_size_set(&skb_frags_rx[nr - 1],
			length - priv->frag_info[nr - 1].frag_prefix_size);
	return nr;

fail:
	while (nr > 0) {
		nr--;
		__skb_frag_unref(&skb_frags_rx[nr]);
	}
	return 0;
}


static struct sk_buff *mlx4_en_rx_skb(struct mlx4_en_priv *priv,
				      struct mlx4_en_rx_desc *rx_desc,
				      struct mlx4_en_rx_alloc *frags,
				      unsigned int length)
{
	struct sk_buff *skb;
	void *va;
	int used_frags;
	dma_addr_t dma;

	skb = netdev_alloc_skb(priv->dev, SMALL_PACKET_SIZE + NET_IP_ALIGN);
	if (!skb) {
		en_dbg(RX_ERR, priv, "Failed allocating skb\n");
		return NULL;
	}
	skb_reserve(skb, NET_IP_ALIGN);
	skb->len = length;

	/* Get pointer to first fragment so we could copy the headers into the
	 * (linear part of the) skb */
	va = page_address(frags[0].page) + frags[0].page_offset;

	if (length <= SMALL_PACKET_SIZE) {
		/* We are copying all relevant data to the skb - temporarily
		 * sync buffers for the copy */
		dma = be64_to_cpu(rx_desc->data[0].addr);
		dma_sync_single_for_cpu(priv->ddev, dma, length,
					DMA_FROM_DEVICE);
		skb_copy_to_linear_data(skb, va, length);
		skb->tail += length;
	} else {
		unsigned int pull_len;

		/* Move relevant fragments to skb */
		used_frags = mlx4_en_complete_rx_desc(priv, rx_desc, frags,
							skb, length);
		if (unlikely(!used_frags)) {
			kfree_skb(skb);
			return NULL;
		}
		skb_shinfo(skb)->nr_frags = used_frags;

		pull_len = eth_get_headlen(va, SMALL_PACKET_SIZE);
		/* Copy headers into the skb linear buffer */
		memcpy(skb->data, va, pull_len);
		skb->tail += pull_len;

		/* Skip headers in first fragment */
		skb_shinfo(skb)->frags[0].page_offset += pull_len;

		/* Adjust size of first fragment */
		skb_frag_size_sub(&skb_shinfo(skb)->frags[0], pull_len);
		skb->data_len = length - pull_len;
	}
	return skb;
}

static void validate_loopback(struct mlx4_en_priv *priv, struct sk_buff *skb)
{
	int i;
	int offset = ETH_HLEN;

	for (i = 0; i < MLX4_LOOPBACK_TEST_PAYLOAD; i++, offset++) {
		if (*(skb->data + offset) != (unsigned char) (i & 0xff))
			goto out_loopback;
	}
	/* Loopback found */
	priv->loopback_ok = 1;

out_loopback:
	dev_kfree_skb_any(skb);
}

static void mlx4_en_refill_rx_buffers(struct mlx4_en_priv *priv,
				     struct mlx4_en_rx_ring *ring)
{
	int index = ring->prod & ring->size_mask;

	while ((u32) (ring->prod - ring->cons) < ring->actual_size) {
		if (mlx4_en_prepare_rx_desc(priv, ring, index, GFP_ATOMIC))
			break;
		ring->prod++;
		index = ring->prod & ring->size_mask;
	}
}

int mlx4_en_process_rx_cq(struct net_device *dev, struct mlx4_en_cq *cq, int budget)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_cqe *cqe;
	struct mlx4_en_rx_ring *ring = priv->rx_ring[cq->ring];
	struct mlx4_en_rx_alloc *frags;
	struct mlx4_en_rx_desc *rx_desc;
	struct sk_buff *skb;
	int index;
	int nr;
	unsigned int length;
	int polled = 0;
	int ip_summed;
	int factor = priv->cqe_factor;
	u64 timestamp;
	bool l2_tunnel;

	if (!priv->port_up)
		return 0;

	if (budget <= 0)
		return polled;

	/* We assume a 1:1 mapping between CQEs and Rx descriptors, so Rx
	 * descriptor offset can be deduced from the CQE index instead of
	 * reading 'cqe->index' */
	index = cq->mcq.cons_index & ring->size_mask;
	cqe = mlx4_en_get_cqe(cq->buf, index, priv->cqe_size) + factor;

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
		    cq->mcq.cons_index & cq->size)) {

		frags = ring->rx_info + (index << priv->log_rx_info);
		rx_desc = ring->buf + (index << ring->log_stride);

		/*
		 * make sure we read the CQE after we read the ownership bit
		 */
		rmb();

		/* Drop packet on bad receive or bad checksum */
		if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
						MLX4_CQE_OPCODE_ERROR)) {
			en_err(priv, "CQE completed in error - vendor syndrom:%d syndrom:%d\n",
			       ((struct mlx4_err_cqe *)cqe)->vendor_err_syndrome,
			       ((struct mlx4_err_cqe *)cqe)->syndrome);
			goto next;
		}
		if (unlikely(cqe->badfcs_enc & MLX4_CQE_BAD_FCS)) {
			en_dbg(RX_ERR, priv, "Accepted frame with bad FCS\n");
			goto next;
		}

		/* Check if we need to drop the packet if SRIOV is not enabled
		 * and not performing the selftest or flb disabled
		 */
		if (priv->flags & MLX4_EN_FLAG_RX_FILTER_NEEDED) {
			struct ethhdr *ethh;
			dma_addr_t dma;
			/* Get pointer to first fragment since we haven't
			 * skb yet and cast it to ethhdr struct
			 */
			dma = be64_to_cpu(rx_desc->data[0].addr);
			dma_sync_single_for_cpu(priv->ddev, dma, sizeof(*ethh),
						DMA_FROM_DEVICE);
			ethh = (struct ethhdr *)(page_address(frags[0].page) +
						 frags[0].page_offset);

			if (is_multicast_ether_addr(ethh->h_dest)) {
				struct mlx4_mac_entry *entry;
				struct hlist_head *bucket;
				unsigned int mac_hash;

				/* Drop the packet, since HW loopback-ed it */
				mac_hash = ethh->h_source[MLX4_EN_MAC_HASH_IDX];
				bucket = &priv->mac_hash[mac_hash];
				rcu_read_lock();
				hlist_for_each_entry_rcu(entry, bucket, hlist) {
					if (ether_addr_equal_64bits(entry->mac,
								    ethh->h_source)) {
						rcu_read_unlock();
						goto next;
					}
				}
				rcu_read_unlock();
			}
		}

		/*
		 * Packet is OK - process it.
		 */
		length = be32_to_cpu(cqe->byte_cnt);
		length -= ring->fcs_del;
		ring->bytes += length;
		ring->packets++;
		l2_tunnel = (dev->hw_enc_features & NETIF_F_RXCSUM) &&
			(cqe->vlan_my_qpn & cpu_to_be32(MLX4_CQE_L2_TUNNEL));

		if (likely(dev->features & NETIF_F_RXCSUM)) {
			if ((cqe->status & cpu_to_be16(MLX4_CQE_STATUS_IPOK)) &&
			    (cqe->checksum == cpu_to_be16(0xffff))) {
				ring->csum_ok++;
				/* This packet is eligible for GRO if it is:
				 * - DIX Ethernet (type interpretation)
				 * - TCP/IP (v4)
				 * - without IP options
				 * - not an IP fragment
				 * - no LLS polling in progress
				 */
				if (!mlx4_en_cq_busy_polling(cq) &&
				    (dev->features & NETIF_F_GRO)) {
					struct sk_buff *gro_skb = napi_get_frags(&cq->napi);
					if (!gro_skb)
						goto next;

					nr = mlx4_en_complete_rx_desc(priv,
						rx_desc, frags, gro_skb,
						length);
					if (!nr)
						goto next;

					skb_shinfo(gro_skb)->nr_frags = nr;
					gro_skb->len = length;
					gro_skb->data_len = length;
					gro_skb->ip_summed = CHECKSUM_UNNECESSARY;

					if (l2_tunnel)
						gro_skb->csum_level = 1;
					if ((cqe->vlan_my_qpn &
					    cpu_to_be32(MLX4_CQE_VLAN_PRESENT_MASK)) &&
					    (dev->features & NETIF_F_HW_VLAN_CTAG_RX)) {
						u16 vid = be16_to_cpu(cqe->sl_vid);

						__vlan_hwaccel_put_tag(gro_skb, htons(ETH_P_8021Q), vid);
					}

					if (dev->features & NETIF_F_RXHASH)
						skb_set_hash(gro_skb,
							     be32_to_cpu(cqe->immed_rss_invalid),
							     PKT_HASH_TYPE_L3);

					skb_record_rx_queue(gro_skb, cq->ring);
					skb_mark_napi_id(gro_skb, &cq->napi);

					if (ring->hwtstamp_rx_filter == HWTSTAMP_FILTER_ALL) {
						timestamp = mlx4_en_get_cqe_ts(cqe);
						mlx4_en_fill_hwtstamps(mdev,
								       skb_hwtstamps(gro_skb),
								       timestamp);
					}

					napi_gro_frags(&cq->napi);
					goto next;
				}

				/* GRO not possible, complete processing here */
				ip_summed = CHECKSUM_UNNECESSARY;
			} else {
				ip_summed = CHECKSUM_NONE;
				ring->csum_none++;
			}
		} else {
			ip_summed = CHECKSUM_NONE;
			ring->csum_none++;
		}

		skb = mlx4_en_rx_skb(priv, rx_desc, frags, length);
		if (!skb) {
			priv->stats.rx_dropped++;
			goto next;
		}

                if (unlikely(priv->validate_loopback)) {
			validate_loopback(priv, skb);
			goto next;
		}

		skb->ip_summed = ip_summed;
		skb->protocol = eth_type_trans(skb, dev);
		skb_record_rx_queue(skb, cq->ring);

		if (l2_tunnel && ip_summed == CHECKSUM_UNNECESSARY)
			skb->csum_level = 1;

		if (dev->features & NETIF_F_RXHASH)
			skb_set_hash(skb,
				     be32_to_cpu(cqe->immed_rss_invalid),
				     PKT_HASH_TYPE_L3);

		if ((be32_to_cpu(cqe->vlan_my_qpn) &
		    MLX4_CQE_VLAN_PRESENT_MASK) &&
		    (dev->features & NETIF_F_HW_VLAN_CTAG_RX))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), be16_to_cpu(cqe->sl_vid));

		if (ring->hwtstamp_rx_filter == HWTSTAMP_FILTER_ALL) {
			timestamp = mlx4_en_get_cqe_ts(cqe);
			mlx4_en_fill_hwtstamps(mdev, skb_hwtstamps(skb),
					       timestamp);
		}

		skb_mark_napi_id(skb, &cq->napi);

		if (!mlx4_en_cq_busy_polling(cq))
			napi_gro_receive(&cq->napi, skb);
		else
			netif_receive_skb(skb);

next:
		for (nr = 0; nr < priv->num_frags; nr++)
			mlx4_en_free_frag(priv, frags, nr);

		++cq->mcq.cons_index;
		index = (cq->mcq.cons_index) & ring->size_mask;
		cqe = mlx4_en_get_cqe(cq->buf, index, priv->cqe_size) + factor;
		if (++polled == budget)
			goto out;
	}

out:
	AVG_PERF_COUNTER(priv->pstats.rx_coal_avg, polled);
	mlx4_cq_set_ci(&cq->mcq);
	wmb(); /* ensure HW sees CQ consumer before we post new buffers */
	ring->cons = cq->mcq.cons_index;
	mlx4_en_refill_rx_buffers(priv, ring);
	mlx4_en_update_rx_prod_db(ring);
	return polled;
}


void mlx4_en_rx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);

	if (priv->port_up)
		napi_schedule(&cq->napi);
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

	if (!mlx4_en_cq_lock_napi(cq))
		return budget;

	done = mlx4_en_process_rx_cq(dev, cq, budget);

	mlx4_en_cq_unlock_napi(cq);

	/* If we used up all the quota - we're probably not done yet... */
	if (done == budget) {
		int cpu_curr;
		const struct cpumask *aff;

		INC_PERF_COUNTER(priv->pstats.napi_quota);

		cpu_curr = smp_processor_id();
		aff = irq_desc_get_irq_data(cq->irq_desc)->affinity;

		if (unlikely(!cpumask_test_cpu(cpu_curr, aff))) {
			/* Current cpu is not according to smp_irq_affinity -
			 * probably affinity changed. need to stop this NAPI
			 * poll, and restart it on the right CPU
			 */
			napi_complete(napi);
			mlx4_en_arm_cq(priv, cq);
			return 0;
		}
	} else {
		/* Done for now */
		napi_complete(napi);
		mlx4_en_arm_cq(priv, cq);
	}
	return done;
}

static const int frag_sizes[] = {
	FRAG_SZ0,
	FRAG_SZ1,
	FRAG_SZ2,
	FRAG_SZ3
};

void mlx4_en_calc_rx_buf(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int eff_mtu = dev->mtu + ETH_HLEN + VLAN_HLEN;
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
		buf_size += priv->frag_info[i].frag_size;
		i++;
	}

	priv->num_frags = i;
	priv->rx_skb_size = eff_mtu;
	priv->log_rx_info = ROUNDUP_LOG2(i * sizeof(struct mlx4_en_rx_alloc));

	en_dbg(DRV, priv, "Rx buffer scatter-list (effective-mtu:%d num_frags:%d):\n",
	       eff_mtu, priv->num_frags);
	for (i = 0; i < priv->num_frags; i++) {
		en_err(priv,
		       "  frag:%d - size:%d prefix:%d align:%d stride:%d\n",
		       i,
		       priv->frag_info[i].frag_size,
		       priv->frag_info[i].frag_prefix_size,
		       priv->frag_info[i].frag_align,
		       priv->frag_info[i].frag_stride);
	}
}

/* RSS related functions */

static int mlx4_en_config_rss_qp(struct mlx4_en_priv *priv, int qpn,
				 struct mlx4_en_rx_ring *ring,
				 enum mlx4_qp_state *state,
				 struct mlx4_qp *qp)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_qp_context *context;
	int err = 0;

	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	err = mlx4_qp_alloc(mdev->dev, qpn, qp, GFP_KERNEL);
	if (err) {
		en_err(priv, "Failed to allocate qp #%x\n", qpn);
		goto out;
	}
	qp->event = mlx4_en_sqp_event;

	memset(context, 0, sizeof *context);
	mlx4_en_fill_qp_context(priv, ring->actual_size, ring->stride, 0, 0,
				qpn, ring->cqn, -1, context);
	context->db_rec_addr = cpu_to_be64(ring->wqres.db.dma);

	/* Cancel FCS removal if FW allows */
	if (mdev->dev->caps.flags & MLX4_DEV_CAP_FLAG_FCS_KEEP) {
		context->param3 |= cpu_to_be32(1 << 29);
		ring->fcs_del = ETH_FCS_LEN;
	} else
		ring->fcs_del = 0;

	err = mlx4_qp_to_ready(mdev->dev, &ring->wqres.mtt, context, qp, state);
	if (err) {
		mlx4_qp_remove(mdev->dev, qp);
		mlx4_qp_free(mdev->dev, qp);
	}
	mlx4_en_update_rx_prod_db(ring);
out:
	kfree(context);
	return err;
}

int mlx4_en_create_drop_qp(struct mlx4_en_priv *priv)
{
	int err;
	u32 qpn;

	err = mlx4_qp_reserve_range(priv->mdev->dev, 1, 1, &qpn);
	if (err) {
		en_err(priv, "Failed reserving drop qpn\n");
		return err;
	}
	err = mlx4_qp_alloc(priv->mdev->dev, qpn, &priv->drop_qp, GFP_KERNEL);
	if (err) {
		en_err(priv, "Failed allocating drop qp\n");
		mlx4_qp_release_range(priv->mdev->dev, qpn, 1);
		return err;
	}

	return 0;
}

void mlx4_en_destroy_drop_qp(struct mlx4_en_priv *priv)
{
	u32 qpn;

	qpn = priv->drop_qp.qpn;
	mlx4_qp_remove(priv->mdev->dev, &priv->drop_qp);
	mlx4_qp_free(priv->mdev->dev, &priv->drop_qp);
	mlx4_qp_release_range(priv->mdev->dev, qpn, 1);
}

/* Allocate rx qp's and configure them according to rss map */
int mlx4_en_config_rss_steer(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rss_map *rss_map = &priv->rss_map;
	struct mlx4_qp_context context;
	struct mlx4_rss_context *rss_context;
	int rss_rings;
	void *ptr;
	u8 rss_mask = (MLX4_RSS_IPV4 | MLX4_RSS_TCP_IPV4 | MLX4_RSS_IPV6 |
			MLX4_RSS_TCP_IPV6);
	int i, qpn;
	int err = 0;
	int good_qps = 0;
	static const u32 rsskey[10] = { 0xD181C62C, 0xF7F4DB5B, 0x1983A2FC,
				0x943E1ADB, 0xD9389E6B, 0xD1039C2C, 0xA74499AD,
				0x593D56D9, 0xF3253C06, 0x2ADC1FFC};

	en_dbg(DRV, priv, "Configuring rss steering\n");
	err = mlx4_qp_reserve_range(mdev->dev, priv->rx_ring_num,
				    priv->rx_ring_num,
				    &rss_map->base_qpn);
	if (err) {
		en_err(priv, "Failed reserving %d qps\n", priv->rx_ring_num);
		return err;
	}

	for (i = 0; i < priv->rx_ring_num; i++) {
		qpn = rss_map->base_qpn + i;
		err = mlx4_en_config_rss_qp(priv, qpn, priv->rx_ring[i],
					    &rss_map->state[i],
					    &rss_map->qps[i]);
		if (err)
			goto rss_err;

		++good_qps;
	}

	/* Configure RSS indirection qp */
	err = mlx4_qp_alloc(mdev->dev, priv->base_qpn, &rss_map->indir_qp, GFP_KERNEL);
	if (err) {
		en_err(priv, "Failed to allocate RSS indirection QP\n");
		goto rss_err;
	}
	rss_map->indir_qp.event = mlx4_en_sqp_event;
	mlx4_en_fill_qp_context(priv, 0, 0, 0, 1, priv->base_qpn,
				priv->rx_ring[0]->cqn, -1, &context);

	if (!priv->prof->rss_rings || priv->prof->rss_rings > priv->rx_ring_num)
		rss_rings = priv->rx_ring_num;
	else
		rss_rings = priv->prof->rss_rings;

	ptr = ((void *) &context) + offsetof(struct mlx4_qp_context, pri_path)
					+ MLX4_RSS_OFFSET_IN_QPC_PRI_PATH;
	rss_context = ptr;
	rss_context->base_qpn = cpu_to_be32(ilog2(rss_rings) << 24 |
					    (rss_map->base_qpn));
	rss_context->default_qpn = cpu_to_be32(rss_map->base_qpn);
	if (priv->mdev->profile.udp_rss) {
		rss_mask |=  MLX4_RSS_UDP_IPV4 | MLX4_RSS_UDP_IPV6;
		rss_context->base_qpn_udp = rss_context->default_qpn;
	}

	if (mdev->dev->caps.tunnel_offload_mode == MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) {
		en_info(priv, "Setting RSS context tunnel type to RSS on inner headers\n");
		rss_mask |= MLX4_RSS_BY_INNER_HEADERS;
	}

	rss_context->flags = rss_mask;
	rss_context->hash_fn = MLX4_RSS_HASH_TOP;
	for (i = 0; i < 10; i++)
		rss_context->rss_key[i] = cpu_to_be32(rsskey[i]);

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
rss_err:
	for (i = 0; i < good_qps; i++) {
		mlx4_qp_modify(mdev->dev, NULL, rss_map->state[i],
			       MLX4_QP_STATE_RST, NULL, 0, 0, &rss_map->qps[i]);
		mlx4_qp_remove(mdev->dev, &rss_map->qps[i]);
		mlx4_qp_free(mdev->dev, &rss_map->qps[i]);
	}
	mlx4_qp_release_range(mdev->dev, rss_map->base_qpn, priv->rx_ring_num);
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

	for (i = 0; i < priv->rx_ring_num; i++) {
		mlx4_qp_modify(mdev->dev, NULL, rss_map->state[i],
			       MLX4_QP_STATE_RST, NULL, 0, 0, &rss_map->qps[i]);
		mlx4_qp_remove(mdev->dev, &rss_map->qps[i]);
		mlx4_qp_free(mdev->dev, &rss_map->qps[i]);
	}
	mlx4_qp_release_range(mdev->dev, rss_map->base_qpn, priv->rx_ring_num);
}

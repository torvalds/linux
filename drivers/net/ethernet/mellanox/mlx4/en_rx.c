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
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/mlx4/cq.h>
#include <linux/slab.h>
#include <linux/mlx4/qp.h>
#include <linux/skbuff.h>
#include <linux/rculist.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>
#include <linux/irq.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ip6_checksum.h>
#endif

#include "mlx4_en.h"

static int mlx4_alloc_page(struct mlx4_en_priv *priv,
			   struct mlx4_en_rx_alloc *frag,
			   gfp_t gfp)
{
	struct page *page;
	dma_addr_t dma;

	page = alloc_page(gfp);
	if (unlikely(!page))
		return -ENOMEM;
	dma = dma_map_page(priv->ddev, page, 0, PAGE_SIZE, priv->dma_dir);
	if (unlikely(dma_mapping_error(priv->ddev, dma))) {
		__free_page(page);
		return -ENOMEM;
	}
	frag->page = page;
	frag->dma = dma;
	frag->page_offset = priv->rx_headroom;
	return 0;
}

static int mlx4_en_alloc_frags(struct mlx4_en_priv *priv,
			       struct mlx4_en_rx_ring *ring,
			       struct mlx4_en_rx_desc *rx_desc,
			       struct mlx4_en_rx_alloc *frags,
			       gfp_t gfp)
{
	int i;

	for (i = 0; i < priv->num_frags; i++, frags++) {
		if (!frags->page) {
			if (mlx4_alloc_page(priv, frags, gfp))
				return -ENOMEM;
			ring->rx_alloc_pages++;
		}
		rx_desc->data[i].addr = cpu_to_be64(frags->dma +
						    frags->page_offset);
	}
	return 0;
}

static void mlx4_en_free_frag(const struct mlx4_en_priv *priv,
			      struct mlx4_en_rx_alloc *frag)
{
	if (frag->page) {
		dma_unmap_page(priv->ddev, frag->dma,
			       PAGE_SIZE, priv->dma_dir);
		__free_page(frag->page);
	}
	/* We need to clear all fields, otherwise a change of priv->log_rx_info
	 * could lead to see garbage later in frag->page.
	 */
	memset(frag, 0, sizeof(*frag));
}

static void mlx4_en_init_rx_desc(const struct mlx4_en_priv *priv,
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
	if (ring->page_cache.index > 0) {
		/* XDP uses a single page per frame */
		if (!frags->page) {
			ring->page_cache.index--;
			frags->page = ring->page_cache.buf[ring->page_cache.index].page;
			frags->dma  = ring->page_cache.buf[ring->page_cache.index].dma;
		}
		frags->page_offset = XDP_PACKET_HEADROOM;
		rx_desc->data[0].addr = cpu_to_be64(frags->dma +
						    XDP_PACKET_HEADROOM);
		return 0;
	}

	return mlx4_en_alloc_frags(priv, ring, rx_desc, frags, gfp);
}

static bool mlx4_en_is_ring_empty(const struct mlx4_en_rx_ring *ring)
{
	return ring->prod == ring->cons;
}

static inline void mlx4_en_update_rx_prod_db(struct mlx4_en_rx_ring *ring)
{
	*ring->wqres.db.db = cpu_to_be32(ring->prod & 0xffff);
}

/* slow path */
static void mlx4_en_free_rx_desc(const struct mlx4_en_priv *priv,
				 struct mlx4_en_rx_ring *ring,
				 int index)
{
	struct mlx4_en_rx_alloc *frags;
	int nr;

	frags = ring->rx_info + (index << priv->log_rx_info);
	for (nr = 0; nr < priv->num_frags; nr++) {
		en_dbg(DRV, priv, "Freeing fragment:%d\n", nr);
		mlx4_en_free_frag(priv, frags + nr);
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
						    GFP_KERNEL | __GFP_COLD)) {
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
	for (index = 0; index < ring->size; index++) {
		en_dbg(DRV, priv, "Processing descriptor:%d\n", index);
		mlx4_en_free_rx_desc(priv, ring, index);
	}
	ring->cons = 0;
	ring->prod = 0;
}

void mlx4_en_set_num_rx_rings(struct mlx4_en_dev *mdev)
{
	int i;
	int num_of_eqs;
	int num_rx_rings;
	struct mlx4_dev *dev = mdev->dev;

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH) {
		num_of_eqs = max_t(int, MIN_RX_RINGS,
				   min_t(int,
					 mlx4_get_eqs_per_port(mdev->dev, i),
					 DEF_RX_RINGS));

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
	ring->rx_info = vzalloc_node(tmp, node);
	if (!ring->rx_info) {
		ring->rx_info = vzalloc(tmp);
		if (!ring->rx_info) {
			err = -ENOMEM;
			goto err_ring;
		}
	}

	en_dbg(DRV, priv, "Allocated rx_info ring at addr:%p size:%d\n",
		 ring->rx_info, tmp);

	/* Allocate HW buffers on provided NUMA node */
	set_dev_node(&mdev->dev->persist->pdev->dev, node);
	err = mlx4_alloc_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
	set_dev_node(&mdev->dev->persist->pdev->dev, mdev->dev->numa_node);
	if (err)
		goto err_info;

	ring->buf = ring->wqres.buf.direct.buf;

	ring->hwtstamp_rx_filter = priv->hwtstamp_config.rx_filter;

	*pring = ring;
	return 0;

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
		if (ring->stride <= TXBB_SIZE) {
			/* Stamp first unused send wqe */
			__be32 *ptr = (__be32 *)ring->buf;
			__be32 stamp = cpu_to_be32(1 << STAMP_SHIFT);
			*ptr = stamp;
			/* Move pointer to start of rx section */
			ring->buf += TXBB_SIZE;
		}

		ring->log_stride = ffs(ring->stride) - 1;
		ring->buf_size = ring->size * ring->stride;

		memset(ring->buf, 0, ring->buf_size);
		mlx4_en_update_rx_prod_db(ring);

		/* Initialize all descriptors */
		for (i = 0; i < ring->size; i++)
			mlx4_en_init_rx_desc(priv, ring, i);
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
	while (ring_ind >= 0) {
		if (priv->rx_ring[ring_ind]->stride <= TXBB_SIZE)
			priv->rx_ring[ring_ind]->buf -= TXBB_SIZE;
		ring_ind--;
	}
	return err;
}

/* We recover from out of memory by scheduling our napi poll
 * function (mlx4_en_process_cq), which tries to allocate
 * all missing RX buffers (call to mlx4_en_refill_rx_buffers).
 */
void mlx4_en_recover_from_oom(struct mlx4_en_priv *priv)
{
	int ring;

	if (!priv->port_up)
		return;

	for (ring = 0; ring < priv->rx_ring_num; ring++) {
		if (mlx4_en_is_ring_empty(priv->rx_ring[ring])) {
			local_bh_disable();
			napi_reschedule(&priv->rx_cq[ring]->napi);
			local_bh_enable();
		}
	}
}

/* When the rx ring is running in page-per-packet mode, a released frame can go
 * directly into a small cache, to avoid unmapping or touching the page
 * allocator. In bpf prog performance scenarios, buffers are either forwarded
 * or dropped, never converted to skbs, so every page can come directly from
 * this cache when it is sized to be a multiple of the napi budget.
 */
bool mlx4_en_rx_recycle(struct mlx4_en_rx_ring *ring,
			struct mlx4_en_rx_alloc *frame)
{
	struct mlx4_en_page_cache *cache = &ring->page_cache;

	if (cache->index >= MLX4_EN_CACHE_SIZE)
		return false;

	cache->buf[cache->index].page = frame->page;
	cache->buf[cache->index].dma = frame->dma;
	cache->index++;
	return true;
}

void mlx4_en_destroy_rx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring **pring,
			     u32 size, u16 stride)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_rx_ring *ring = *pring;
	struct bpf_prog *old_prog;

	old_prog = rcu_dereference_protected(
					ring->xdp_prog,
					lockdep_is_held(&mdev->state_lock));
	if (old_prog)
		bpf_prog_put(old_prog);
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, size * stride + TXBB_SIZE);
	vfree(ring->rx_info);
	ring->rx_info = NULL;
	kfree(ring);
	*pring = NULL;
}

void mlx4_en_deactivate_rx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring)
{
	int i;

	for (i = 0; i < ring->page_cache.index; i++) {
		dma_unmap_page(priv->ddev, ring->page_cache.buf[i].dma,
			       PAGE_SIZE, priv->dma_dir);
		put_page(ring->page_cache.buf[i].page);
	}
	ring->page_cache.index = 0;
	mlx4_en_free_rx_buf(priv, ring);
	if (ring->stride <= TXBB_SIZE)
		ring->buf -= TXBB_SIZE;
}


static int mlx4_en_complete_rx_desc(struct mlx4_en_priv *priv,
				    struct mlx4_en_rx_alloc *frags,
				    struct sk_buff *skb,
				    int length)
{
	const struct mlx4_en_frag_info *frag_info = priv->frag_info;
	unsigned int truesize = 0;
	int nr, frag_size;
	struct page *page;
	dma_addr_t dma;
	bool release;

	/* Collect used fragments while replacing them in the HW descriptors */
	for (nr = 0;; frags++) {
		frag_size = min_t(int, length, frag_info->frag_size);

		page = frags->page;
		if (unlikely(!page))
			goto fail;

		dma = frags->dma;
		dma_sync_single_range_for_cpu(priv->ddev, dma, frags->page_offset,
					      frag_size, priv->dma_dir);

		__skb_fill_page_desc(skb, nr, page, frags->page_offset,
				     frag_size);

		truesize += frag_info->frag_stride;
		if (frag_info->frag_stride == PAGE_SIZE / 2) {
			frags->page_offset ^= PAGE_SIZE / 2;
			release = page_count(page) != 1 ||
				  page_is_pfmemalloc(page) ||
				  page_to_nid(page) != numa_mem_id();
		} else {
			u32 sz_align = ALIGN(frag_size, SMP_CACHE_BYTES);

			frags->page_offset += sz_align;
			release = frags->page_offset + frag_info->frag_size > PAGE_SIZE;
		}
		if (release) {
			dma_unmap_page(priv->ddev, dma, PAGE_SIZE, priv->dma_dir);
			frags->page = NULL;
		} else {
			page_ref_inc(page);
		}

		nr++;
		length -= frag_size;
		if (!length)
			break;
		frag_info++;
	}
	skb->truesize += truesize;
	return nr;

fail:
	while (nr > 0) {
		nr--;
		__skb_frag_unref(skb_shinfo(skb)->frags + nr);
	}
	return 0;
}

static void validate_loopback(struct mlx4_en_priv *priv, void *va)
{
	const unsigned char *data = va + ETH_HLEN;
	int i;

	for (i = 0; i < MLX4_LOOPBACK_TEST_PAYLOAD; i++) {
		if (data[i] != (unsigned char)i)
			return;
	}
	/* Loopback found */
	priv->loopback_ok = 1;
}

static bool mlx4_en_refill_rx_buffers(struct mlx4_en_priv *priv,
				      struct mlx4_en_rx_ring *ring)
{
	u32 missing = ring->actual_size - (ring->prod - ring->cons);

	/* Try to batch allocations, but not too much. */
	if (missing < 8)
		return false;
	do {
		if (mlx4_en_prepare_rx_desc(priv, ring,
					    ring->prod & ring->size_mask,
					    GFP_ATOMIC | __GFP_COLD |
					    __GFP_MEMALLOC))
			break;
		ring->prod++;
	} while (--missing);

	return true;
}

/* When hardware doesn't strip the vlan, we need to calculate the checksum
 * over it and add it to the hardware's checksum calculation
 */
static inline __wsum get_fixed_vlan_csum(__wsum hw_checksum,
					 struct vlan_hdr *vlanh)
{
	return csum_add(hw_checksum, *(__wsum *)vlanh);
}

/* Although the stack expects checksum which doesn't include the pseudo
 * header, the HW adds it. To address that, we are subtracting the pseudo
 * header checksum from the checksum value provided by the HW.
 */
static void get_fixed_ipv4_csum(__wsum hw_checksum, struct sk_buff *skb,
				struct iphdr *iph)
{
	__u16 length_for_csum = 0;
	__wsum csum_pseudo_header = 0;

	length_for_csum = (be16_to_cpu(iph->tot_len) - (iph->ihl << 2));
	csum_pseudo_header = csum_tcpudp_nofold(iph->saddr, iph->daddr,
						length_for_csum, iph->protocol, 0);
	skb->csum = csum_sub(hw_checksum, csum_pseudo_header);
}

#if IS_ENABLED(CONFIG_IPV6)
/* In IPv6 packets, besides subtracting the pseudo header checksum,
 * we also compute/add the IP header checksum which
 * is not added by the HW.
 */
static int get_fixed_ipv6_csum(__wsum hw_checksum, struct sk_buff *skb,
			       struct ipv6hdr *ipv6h)
{
	__wsum csum_pseudo_hdr = 0;

	if (unlikely(ipv6h->nexthdr == IPPROTO_FRAGMENT ||
		     ipv6h->nexthdr == IPPROTO_HOPOPTS))
		return -1;
	hw_checksum = csum_add(hw_checksum, (__force __wsum)htons(ipv6h->nexthdr));

	csum_pseudo_hdr = csum_partial(&ipv6h->saddr,
				       sizeof(ipv6h->saddr) + sizeof(ipv6h->daddr), 0);
	csum_pseudo_hdr = csum_add(csum_pseudo_hdr, (__force __wsum)ipv6h->payload_len);
	csum_pseudo_hdr = csum_add(csum_pseudo_hdr, (__force __wsum)ntohs(ipv6h->nexthdr));

	skb->csum = csum_sub(hw_checksum, csum_pseudo_hdr);
	skb->csum = csum_add(skb->csum, csum_partial(ipv6h, sizeof(struct ipv6hdr), 0));
	return 0;
}
#endif
static int check_csum(struct mlx4_cqe *cqe, struct sk_buff *skb, void *va,
		      netdev_features_t dev_features)
{
	__wsum hw_checksum = 0;

	void *hdr = (u8 *)va + sizeof(struct ethhdr);

	hw_checksum = csum_unfold((__force __sum16)cqe->checksum);

	if (cqe->vlan_my_qpn & cpu_to_be32(MLX4_CQE_CVLAN_PRESENT_MASK) &&
	    !(dev_features & NETIF_F_HW_VLAN_CTAG_RX)) {
		hw_checksum = get_fixed_vlan_csum(hw_checksum, hdr);
		hdr += sizeof(struct vlan_hdr);
	}

	if (cqe->status & cpu_to_be16(MLX4_CQE_STATUS_IPV4))
		get_fixed_ipv4_csum(hw_checksum, skb, hdr);
#if IS_ENABLED(CONFIG_IPV6)
	else if (cqe->status & cpu_to_be16(MLX4_CQE_STATUS_IPV6))
		if (unlikely(get_fixed_ipv6_csum(hw_checksum, skb, hdr)))
			return -1;
#endif
	return 0;
}

int mlx4_en_process_rx_cq(struct net_device *dev, struct mlx4_en_cq *cq, int budget)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_cqe *cqe;
	struct mlx4_en_rx_ring *ring = priv->rx_ring[cq->ring];
	struct mlx4_en_rx_alloc *frags;
	struct bpf_prog *xdp_prog;
	int doorbell_pending;
	struct sk_buff *skb;
	int index;
	int nr;
	unsigned int length;
	int polled = 0;
	int ip_summed;
	int factor = priv->cqe_factor;
	u64 timestamp;
	bool l2_tunnel;

	if (unlikely(!priv->port_up))
		return 0;

	if (unlikely(budget <= 0))
		return polled;

	/* Protect accesses to: ring->xdp_prog, priv->mac_hash list */
	rcu_read_lock();
	xdp_prog = rcu_dereference(ring->xdp_prog);
	doorbell_pending = 0;

	/* We assume a 1:1 mapping between CQEs and Rx descriptors, so Rx
	 * descriptor offset can be deduced from the CQE index instead of
	 * reading 'cqe->index' */
	index = cq->mcq.cons_index & ring->size_mask;
	cqe = mlx4_en_get_cqe(cq->buf, index, priv->cqe_size) + factor;

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
		    cq->mcq.cons_index & cq->size)) {
		void *va;

		frags = ring->rx_info + (index << priv->log_rx_info);
		va = page_address(frags[0].page) + frags[0].page_offset;
		/*
		 * make sure we read the CQE after we read the ownership bit
		 */
		dma_rmb();

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
			const struct ethhdr *ethh = va;
			dma_addr_t dma;
			/* Get pointer to first fragment since we haven't
			 * skb yet and cast it to ethhdr struct
			 */
			dma = frags[0].dma + frags[0].page_offset;
			dma_sync_single_for_cpu(priv->ddev, dma, sizeof(*ethh),
						DMA_FROM_DEVICE);

			if (is_multicast_ether_addr(ethh->h_dest)) {
				struct mlx4_mac_entry *entry;
				struct hlist_head *bucket;
				unsigned int mac_hash;

				/* Drop the packet, since HW loopback-ed it */
				mac_hash = ethh->h_source[MLX4_EN_MAC_HASH_IDX];
				bucket = &priv->mac_hash[mac_hash];
				hlist_for_each_entry_rcu(entry, bucket, hlist) {
					if (ether_addr_equal_64bits(entry->mac,
								    ethh->h_source))
						goto next;
				}
			}
		}

		if (unlikely(priv->validate_loopback)) {
			validate_loopback(priv, va);
			goto next;
		}

		/*
		 * Packet is OK - process it.
		 */
		length = be32_to_cpu(cqe->byte_cnt);
		length -= ring->fcs_del;

		/* A bpf program gets first chance to drop the packet. It may
		 * read bytes but not past the end of the frag.
		 */
		if (xdp_prog) {
			struct xdp_buff xdp;
			dma_addr_t dma;
			void *orig_data;
			u32 act;

			dma = frags[0].dma + frags[0].page_offset;
			dma_sync_single_for_cpu(priv->ddev, dma,
						priv->frag_info[0].frag_size,
						DMA_FROM_DEVICE);

			xdp.data_hard_start = va - frags[0].page_offset;
			xdp.data = va;
			xdp.data_end = xdp.data + length;
			orig_data = xdp.data;

			act = bpf_prog_run_xdp(xdp_prog, &xdp);

			if (xdp.data != orig_data) {
				length = xdp.data_end - xdp.data;
				frags[0].page_offset = xdp.data -
					xdp.data_hard_start;
				va = xdp.data;
			}

			switch (act) {
			case XDP_PASS:
				break;
			case XDP_TX:
				if (likely(!mlx4_en_xmit_frame(ring, frags, dev,
							length, cq->ring,
							&doorbell_pending))) {
					frags[0].page = NULL;
					goto next;
				}
				trace_xdp_exception(dev, xdp_prog, act);
				goto xdp_drop_no_cnt; /* Drop on xmit failure */
			default:
				bpf_warn_invalid_xdp_action(act);
			case XDP_ABORTED:
				trace_xdp_exception(dev, xdp_prog, act);
			case XDP_DROP:
				ring->xdp_drop++;
xdp_drop_no_cnt:
				goto next;
			}
		}

		ring->bytes += length;
		ring->packets++;

		skb = napi_get_frags(&cq->napi);
		if (!skb)
			goto next;

		if (unlikely(ring->hwtstamp_rx_filter == HWTSTAMP_FILTER_ALL)) {
			timestamp = mlx4_en_get_cqe_ts(cqe);
			mlx4_en_fill_hwtstamps(mdev, skb_hwtstamps(skb),
					       timestamp);
		}
		skb_record_rx_queue(skb, cq->ring);

		if (likely(dev->features & NETIF_F_RXCSUM)) {
			if (cqe->status & cpu_to_be16(MLX4_CQE_STATUS_TCP |
						      MLX4_CQE_STATUS_UDP)) {
				if ((cqe->status & cpu_to_be16(MLX4_CQE_STATUS_IPOK)) &&
				    cqe->checksum == cpu_to_be16(0xffff)) {
					ip_summed = CHECKSUM_UNNECESSARY;
					l2_tunnel = (dev->hw_enc_features & NETIF_F_RXCSUM) &&
						(cqe->vlan_my_qpn & cpu_to_be32(MLX4_CQE_L2_TUNNEL));
					if (l2_tunnel)
						skb->csum_level = 1;
					ring->csum_ok++;
				} else {
					goto csum_none;
				}
			} else {
				if (priv->flags & MLX4_EN_FLAG_RX_CSUM_NON_TCP_UDP &&
				    (cqe->status & cpu_to_be16(MLX4_CQE_STATUS_IPV4 |
							       MLX4_CQE_STATUS_IPV6))) {
					if (check_csum(cqe, skb, va, dev->features)) {
						goto csum_none;
					} else {
						ip_summed = CHECKSUM_COMPLETE;
						ring->csum_complete++;
					}
				} else {
					goto csum_none;
				}
			}
		} else {
csum_none:
			ip_summed = CHECKSUM_NONE;
			ring->csum_none++;
		}
		skb->ip_summed = ip_summed;
		if (dev->features & NETIF_F_RXHASH)
			skb_set_hash(skb,
				     be32_to_cpu(cqe->immed_rss_invalid),
				     (ip_summed == CHECKSUM_UNNECESSARY) ?
					PKT_HASH_TYPE_L4 :
					PKT_HASH_TYPE_L3);


		if ((cqe->vlan_my_qpn &
		     cpu_to_be32(MLX4_CQE_CVLAN_PRESENT_MASK)) &&
		    (dev->features & NETIF_F_HW_VLAN_CTAG_RX))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       be16_to_cpu(cqe->sl_vid));
		else if ((cqe->vlan_my_qpn &
			  cpu_to_be32(MLX4_CQE_SVLAN_PRESENT_MASK)) &&
			 (dev->features & NETIF_F_HW_VLAN_STAG_RX))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021AD),
					       be16_to_cpu(cqe->sl_vid));

		nr = mlx4_en_complete_rx_desc(priv, frags, skb, length);
		if (likely(nr)) {
			skb_shinfo(skb)->nr_frags = nr;
			skb->len = length;
			skb->data_len = length;
			napi_gro_frags(&cq->napi);
		} else {
			skb->vlan_tci = 0;
			skb_clear_hash(skb);
		}
next:
		++cq->mcq.cons_index;
		index = (cq->mcq.cons_index) & ring->size_mask;
		cqe = mlx4_en_get_cqe(cq->buf, index, priv->cqe_size) + factor;
		if (++polled == budget)
			break;
	}

	rcu_read_unlock();

	if (polled) {
		if (doorbell_pending)
			mlx4_en_xmit_doorbell(priv->tx_ring[TX_XDP][cq->ring]);

		mlx4_cq_set_ci(&cq->mcq);
		wmb(); /* ensure HW sees CQ consumer before we post new buffers */
		ring->cons = cq->mcq.cons_index;
	}
	AVG_PERF_COUNTER(priv->pstats.rx_coal_avg, polled);

	if (mlx4_en_refill_rx_buffers(priv, ring))
		mlx4_en_update_rx_prod_db(ring);

	return polled;
}


void mlx4_en_rx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);

	if (likely(priv->port_up))
		napi_schedule_irqoff(&cq->napi);
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
	if (done == budget) {
		const struct cpumask *aff;
		struct irq_data *idata;
		int cpu_curr;

		INC_PERF_COUNTER(priv->pstats.napi_quota);

		cpu_curr = smp_processor_id();
		idata = irq_desc_get_irq_data(cq->irq_desc);
		aff = irq_data_get_affinity_mask(idata);

		if (likely(cpumask_test_cpu(cpu_curr, aff)))
			return budget;

		/* Current cpu is not according to smp_irq_affinity -
		 * probably affinity changed. Need to stop this NAPI
		 * poll, and restart it on the right CPU.
		 * Try to avoid returning a too small value (like 0),
		 * to not fool net_rx_action() and its netdev_budget
		 */
		if (done)
			done--;
	}
	/* Done for now */
	if (napi_complete_done(napi, done))
		mlx4_en_arm_cq(priv, cq);
	return done;
}

void mlx4_en_calc_rx_buf(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int eff_mtu = MLX4_EN_EFF_MTU(dev->mtu);
	int i = 0;

	/* bpf requires buffers to be set up as 1 packet per page.
	 * This only works when num_frags == 1.
	 */
	if (priv->tx_ring_num[TX_XDP]) {
		priv->frag_info[0].frag_size = eff_mtu;
		/* This will gain efficient xdp frame recycling at the
		 * expense of more costly truesize accounting
		 */
		priv->frag_info[0].frag_stride = PAGE_SIZE;
		priv->dma_dir = PCI_DMA_BIDIRECTIONAL;
		priv->rx_headroom = XDP_PACKET_HEADROOM;
		i = 1;
	} else {
		int frag_size_max = 2048, buf_size = 0;

		/* should not happen, right ? */
		if (eff_mtu > PAGE_SIZE + (MLX4_EN_MAX_RX_FRAGS - 1) * 2048)
			frag_size_max = PAGE_SIZE;

		while (buf_size < eff_mtu) {
			int frag_stride, frag_size = eff_mtu - buf_size;
			int pad, nb;

			if (i < MLX4_EN_MAX_RX_FRAGS - 1)
				frag_size = min(frag_size, frag_size_max);

			priv->frag_info[i].frag_size = frag_size;
			frag_stride = ALIGN(frag_size, SMP_CACHE_BYTES);
			/* We can only pack 2 1536-bytes frames in on 4K page
			 * Therefore, each frame would consume more bytes (truesize)
			 */
			nb = PAGE_SIZE / frag_stride;
			pad = (PAGE_SIZE - nb * frag_stride) / nb;
			pad &= ~(SMP_CACHE_BYTES - 1);
			priv->frag_info[i].frag_stride = frag_stride + pad;

			buf_size += frag_size;
			i++;
		}
		priv->dma_dir = PCI_DMA_FROMDEVICE;
		priv->rx_headroom = 0;
	}

	priv->num_frags = i;
	priv->rx_skb_size = eff_mtu;
	priv->log_rx_info = ROUNDUP_LOG2(i * sizeof(struct mlx4_en_rx_alloc));

	en_dbg(DRV, priv, "Rx buffer scatter-list (effective-mtu:%d num_frags:%d):\n",
	       eff_mtu, priv->num_frags);
	for (i = 0; i < priv->num_frags; i++) {
		en_dbg(DRV,
		       priv,
		       "  frag:%d - size:%d stride:%d\n",
		       i,
		       priv->frag_info[i].frag_size,
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
		if (priv->dev->features & NETIF_F_RXFCS)
			ring->fcs_del = 0;
		else
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

	err = mlx4_qp_reserve_range(priv->mdev->dev, 1, 1, &qpn,
				    MLX4_RESERVE_A0_QP);
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

	en_dbg(DRV, priv, "Configuring rss steering\n");
	err = mlx4_qp_reserve_range(mdev->dev, priv->rx_ring_num,
				    priv->rx_ring_num,
				    &rss_map->base_qpn, 0);
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
	if (priv->rss_hash_fn == ETH_RSS_HASH_XOR) {
		rss_context->hash_fn = MLX4_RSS_HASH_XOR;
	} else if (priv->rss_hash_fn == ETH_RSS_HASH_TOP) {
		rss_context->hash_fn = MLX4_RSS_HASH_TOP;
		memcpy(rss_context->rss_key, priv->rss_key,
		       MLX4_EN_RSS_KEY_SIZE);
	} else {
		en_err(priv, "Unknown RSS hash function requested\n");
		err = -EINVAL;
		goto indir_err;
	}
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

/*
 * Copyright (c) 2012-2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <linux/prefetch.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include "wil6210.h"
#include "txrx_edma.h"
#include "txrx.h"

#define WIL_EDMA_MAX_DATA_OFFSET (2)

static void wil_tx_desc_unmap_edma(struct device *dev,
				   struct wil_tx_enhanced_desc *d,
				   struct wil_ctx *ctx)
{
	dma_addr_t pa = wil_tx_desc_get_addr_edma(&d->dma);
	u16 dmalen = le16_to_cpu(d->dma.length);

	switch (ctx->mapped_as) {
	case wil_mapped_as_single:
		dma_unmap_single(dev, pa, dmalen, DMA_TO_DEVICE);
		break;
	case wil_mapped_as_page:
		dma_unmap_page(dev, pa, dmalen, DMA_TO_DEVICE);
		break;
	default:
		break;
	}
}

static int wil_find_free_sring(struct wil6210_priv *wil)
{
	int i;

	for (i = 0; i < WIL6210_MAX_STATUS_RINGS; i++) {
		if (!wil->srings[i].va)
			return i;
	}

	return -EINVAL;
}

static void wil_sring_free(struct wil6210_priv *wil,
			   struct wil_status_ring *sring)
{
	struct device *dev = wil_to_dev(wil);
	size_t sz;

	if (!sring || !sring->va)
		return;

	sz = sring->elem_size * sring->size;

	wil_dbg_misc(wil, "status_ring_free, size(bytes)=%zu, 0x%p:%pad\n",
		     sz, sring->va, &sring->pa);

	dma_free_coherent(dev, sz, (void *)sring->va, sring->pa);
	sring->pa = 0;
	sring->va = NULL;
}

static int wil_sring_alloc(struct wil6210_priv *wil,
			   struct wil_status_ring *sring)
{
	struct device *dev = wil_to_dev(wil);
	size_t sz = sring->elem_size * sring->size;

	wil_dbg_misc(wil, "status_ring_alloc: size=%zu\n", sz);

	if (sz == 0) {
		wil_err(wil, "Cannot allocate a zero size status ring\n");
		return -EINVAL;
	}

	sring->swhead = 0;

	/* Status messages are allocated and initialized to 0. This is necessary
	 * since DR bit should be initialized to 0.
	 */
	sring->va = dma_zalloc_coherent(dev, sz, &sring->pa, GFP_KERNEL);
	if (!sring->va)
		return -ENOMEM;

	wil_dbg_misc(wil, "status_ring[%d] 0x%p:%pad\n", sring->size, sring->va,
		     &sring->pa);

	return 0;
}

static int wil_tx_init_edma(struct wil6210_priv *wil)
{
	int ring_id = wil_find_free_sring(wil);
	struct wil_status_ring *sring;
	int rc;
	u16 status_ring_size;

	if (wil->tx_status_ring_order < WIL_SRING_SIZE_ORDER_MIN ||
	    wil->tx_status_ring_order > WIL_SRING_SIZE_ORDER_MAX)
		wil->tx_status_ring_order = WIL_TX_SRING_SIZE_ORDER_DEFAULT;

	status_ring_size = 1 << wil->tx_status_ring_order;

	wil_dbg_misc(wil, "init TX sring: size=%u, ring_id=%u\n",
		     status_ring_size, ring_id);

	if (ring_id < 0)
		return ring_id;

	/* Allocate Tx status ring. Tx descriptor rings will be
	 * allocated on WMI connect event
	 */
	sring = &wil->srings[ring_id];

	sring->is_rx = false;
	sring->size = status_ring_size;
	sring->elem_size = sizeof(struct wil_ring_tx_status);
	rc = wil_sring_alloc(wil, sring);
	if (rc)
		return rc;

	rc = wil_wmi_tx_sring_cfg(wil, ring_id);
	if (rc)
		goto out_free;

	sring->desc_rdy_pol = 1;
	wil->tx_sring_idx = ring_id;

	return 0;
out_free:
	wil_sring_free(wil, sring);
	return rc;
}

/**
 * Allocate one skb for Rx descriptor RING
 */
static int wil_ring_alloc_skb_edma(struct wil6210_priv *wil,
				   struct wil_ring *ring, u32 i)
{
	struct device *dev = wil_to_dev(wil);
	unsigned int sz = wil->rx_buf_len + ETH_HLEN +
		WIL_EDMA_MAX_DATA_OFFSET;
	dma_addr_t pa;
	u16 buff_id;
	struct list_head *active = &wil->rx_buff_mgmt.active;
	struct list_head *free = &wil->rx_buff_mgmt.free;
	struct wil_rx_buff *rx_buff;
	struct wil_rx_buff *buff_arr = wil->rx_buff_mgmt.buff_arr;
	struct sk_buff *skb;
	struct wil_rx_enhanced_desc dd, *d = &dd;
	struct wil_rx_enhanced_desc *_d = (struct wil_rx_enhanced_desc *)
		&ring->va[i].rx.enhanced;

	if (unlikely(list_empty(free))) {
		wil->rx_buff_mgmt.free_list_empty_cnt++;
		return -EAGAIN;
	}

	skb = dev_alloc_skb(sz);
	if (unlikely(!skb))
		return -ENOMEM;

	skb_put(skb, sz);

	pa = dma_map_single(dev, skb->data, skb->len, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, pa))) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	/* Get the buffer ID - the index of the rx buffer in the buff_arr */
	rx_buff = list_first_entry(free, struct wil_rx_buff, list);
	buff_id = rx_buff->id;

	/* Move a buffer from the free list to the active list */
	list_move(&rx_buff->list, active);

	buff_arr[buff_id].skb = skb;

	wil_desc_set_addr_edma(&d->dma.addr, &d->dma.addr_high_high, pa);
	d->dma.length = cpu_to_le16(sz);
	d->mac.buff_id = cpu_to_le16(buff_id);
	*_d = *d;

	/* Save the physical address in skb->cb for later use in dma_unmap */
	memcpy(skb->cb, &pa, sizeof(pa));

	return 0;
}

static int wil_rx_refill_edma(struct wil6210_priv *wil)
{
	struct wil_ring *ring = &wil->ring_rx;
	u32 next_head;
	int rc = 0;
	u32 swtail = *ring->edma_rx_swtail.va;

	for (; next_head = wil_ring_next_head(ring), (next_head != swtail);
	     ring->swhead = next_head) {
		rc = wil_ring_alloc_skb_edma(wil, ring, ring->swhead);
		if (unlikely(rc)) {
			if (rc == -EAGAIN)
				wil_dbg_txrx(wil, "No free buffer ID found\n");
			else
				wil_err_ratelimited(wil,
						    "Error %d in refill desc[%d]\n",
						    rc, ring->swhead);
			break;
		}
	}

	/* make sure all writes to descriptors (shared memory) are done before
	 * committing them to HW
	 */
	wmb();

	wil_w(wil, ring->hwtail, ring->swhead);

	return rc;
}

static void wil_move_all_rx_buff_to_free_list(struct wil6210_priv *wil,
					      struct wil_ring *ring)
{
	struct device *dev = wil_to_dev(wil);
	u32 next_tail;
	u32 swhead = (ring->swhead + 1) % ring->size;
	dma_addr_t pa;
	u16 dmalen;

	for (; next_tail = wil_ring_next_tail(ring), (next_tail != swhead);
	     ring->swtail = next_tail) {
		struct wil_rx_enhanced_desc dd, *d = &dd;
		struct wil_rx_enhanced_desc *_d =
			(struct wil_rx_enhanced_desc *)
			&ring->va[ring->swtail].rx.enhanced;
		struct sk_buff *skb;
		u16 buff_id;

		*d = *_d;
		pa = wil_rx_desc_get_addr_edma(&d->dma);
		dmalen = le16_to_cpu(d->dma.length);
		dma_unmap_single(dev, pa, dmalen, DMA_FROM_DEVICE);

		/* Extract the SKB from the rx_buff management array */
		buff_id = __le16_to_cpu(d->mac.buff_id);
		if (buff_id >= wil->rx_buff_mgmt.size) {
			wil_err(wil, "invalid buff_id %d\n", buff_id);
			continue;
		}
		skb = wil->rx_buff_mgmt.buff_arr[buff_id].skb;
		wil->rx_buff_mgmt.buff_arr[buff_id].skb = NULL;
		if (unlikely(!skb))
			wil_err(wil, "No Rx skb at buff_id %d\n", buff_id);
		else
			kfree_skb(skb);

		/* Move the buffer from the active to the free list */
		list_move(&wil->rx_buff_mgmt.buff_arr[buff_id].list,
			  &wil->rx_buff_mgmt.free);
	}
}

static void wil_free_rx_buff_arr(struct wil6210_priv *wil)
{
	struct wil_ring *ring = &wil->ring_rx;

	if (!wil->rx_buff_mgmt.buff_arr)
		return;

	/* Move all the buffers to the free list in case active list is
	 * not empty in order to release all SKBs before deleting the array
	 */
	wil_move_all_rx_buff_to_free_list(wil, ring);

	kfree(wil->rx_buff_mgmt.buff_arr);
	wil->rx_buff_mgmt.buff_arr = NULL;
}

static int wil_init_rx_buff_arr(struct wil6210_priv *wil,
				size_t size)
{
	struct wil_rx_buff *buff_arr;
	struct list_head *active = &wil->rx_buff_mgmt.active;
	struct list_head *free = &wil->rx_buff_mgmt.free;
	int i;

	wil->rx_buff_mgmt.buff_arr = kcalloc(size, sizeof(struct wil_rx_buff),
					     GFP_KERNEL);
	if (!wil->rx_buff_mgmt.buff_arr)
		return -ENOMEM;

	/* Set list heads */
	INIT_LIST_HEAD(active);
	INIT_LIST_HEAD(free);

	/* Linkify the list */
	buff_arr = wil->rx_buff_mgmt.buff_arr;
	for (i = 0; i < size; i++) {
		list_add(&buff_arr[i].list, free);
		buff_arr[i].id = i;
	}

	wil->rx_buff_mgmt.size = size;

	return 0;
}

static int wil_init_rx_sring(struct wil6210_priv *wil,
			     u16 status_ring_size,
			     size_t elem_size,
			     u16 ring_id)
{
	struct wil_status_ring *sring = &wil->srings[ring_id];
	int rc;

	wil_dbg_misc(wil, "init RX sring: size=%u, ring_id=%u\n", sring->size,
		     ring_id);

	memset(&sring->rx_data, 0, sizeof(sring->rx_data));

	sring->is_rx = true;
	sring->size = status_ring_size;
	sring->elem_size = elem_size;
	rc = wil_sring_alloc(wil, sring);
	if (rc)
		return rc;

	rc = wil_wmi_rx_sring_add(wil, ring_id);
	if (rc)
		goto out_free;

	sring->desc_rdy_pol = 1;

	return 0;
out_free:
	wil_sring_free(wil, sring);
	return rc;
}

static int wil_ring_alloc_desc_ring(struct wil6210_priv *wil,
				    struct wil_ring *ring)
{
	struct device *dev = wil_to_dev(wil);
	size_t sz = ring->size * sizeof(ring->va[0]);

	wil_dbg_misc(wil, "alloc_desc_ring:\n");

	BUILD_BUG_ON(sizeof(ring->va[0]) != 32);

	ring->swhead = 0;
	ring->swtail = 0;
	ring->ctx = kcalloc(ring->size, sizeof(ring->ctx[0]), GFP_KERNEL);
	if (!ring->ctx)
		goto err;

	ring->va = dma_zalloc_coherent(dev, sz, &ring->pa, GFP_KERNEL);
	if (!ring->va)
		goto err_free_ctx;

	if (ring->is_rx) {
		sz = sizeof(*ring->edma_rx_swtail.va);
		ring->edma_rx_swtail.va =
			dma_zalloc_coherent(dev, sz, &ring->edma_rx_swtail.pa,
					    GFP_KERNEL);
		if (!ring->edma_rx_swtail.va)
			goto err_free_va;
	}

	wil_dbg_misc(wil, "%s ring[%d] 0x%p:%pad 0x%p\n",
		     ring->is_rx ? "RX" : "TX",
		     ring->size, ring->va, &ring->pa, ring->ctx);

	return 0;
err_free_va:
	dma_free_coherent(dev, ring->size * sizeof(ring->va[0]),
			  (void *)ring->va, ring->pa);
	ring->va = NULL;
err_free_ctx:
	kfree(ring->ctx);
	ring->ctx = NULL;
err:
	return -ENOMEM;
}

static void wil_ring_free_edma(struct wil6210_priv *wil, struct wil_ring *ring)
{
	struct device *dev = wil_to_dev(wil);
	size_t sz;
	int ring_index = 0;

	if (!ring->va)
		return;

	sz = ring->size * sizeof(ring->va[0]);

	lockdep_assert_held(&wil->mutex);
	if (ring->is_rx) {
		wil_dbg_misc(wil, "free Rx ring [%d] 0x%p:%pad 0x%p\n",
			     ring->size, ring->va,
			     &ring->pa, ring->ctx);

		wil_move_all_rx_buff_to_free_list(wil, ring);
		goto out;
	}

	/* TX ring */
	ring_index = ring - wil->ring_tx;

	wil_dbg_misc(wil, "free Tx ring %d [%d] 0x%p:%pad 0x%p\n",
		     ring_index, ring->size, ring->va,
		     &ring->pa, ring->ctx);

	while (!wil_ring_is_empty(ring)) {
		struct wil_ctx *ctx;

		struct wil_tx_enhanced_desc dd, *d = &dd;
		struct wil_tx_enhanced_desc *_d =
			(struct wil_tx_enhanced_desc *)
			&ring->va[ring->swtail].tx.enhanced;

		ctx = &ring->ctx[ring->swtail];
		if (!ctx) {
			wil_dbg_txrx(wil,
				     "ctx(%d) was already completed\n",
				     ring->swtail);
			ring->swtail = wil_ring_next_tail(ring);
			continue;
		}
		*d = *_d;
		wil_tx_desc_unmap_edma(dev, d, ctx);
		if (ctx->skb)
			dev_kfree_skb_any(ctx->skb);
		ring->swtail = wil_ring_next_tail(ring);
	}

out:
	dma_free_coherent(dev, sz, (void *)ring->va, ring->pa);
	kfree(ring->ctx);
	ring->pa = 0;
	ring->va = NULL;
	ring->ctx = NULL;
}

static int wil_init_rx_desc_ring(struct wil6210_priv *wil, u16 desc_ring_size,
				 int status_ring_id)
{
	struct wil_ring *ring = &wil->ring_rx;
	int rc;

	wil_dbg_misc(wil, "init RX desc ring\n");

	ring->size = desc_ring_size;
	ring->is_rx = true;
	rc = wil_ring_alloc_desc_ring(wil, ring);
	if (rc)
		return rc;

	rc = wil_wmi_rx_desc_ring_add(wil, status_ring_id);
	if (rc)
		goto out_free;

	return 0;
out_free:
	wil_ring_free_edma(wil, ring);
	return rc;
}

static void wil_rx_buf_len_init_edma(struct wil6210_priv *wil)
{
	wil->rx_buf_len = rx_large_buf ?
		WIL_MAX_ETH_MTU : TXRX_BUF_LEN_DEFAULT - WIL_MAX_MPDU_OVERHEAD;
}

static int wil_rx_init_edma(struct wil6210_priv *wil, u16 desc_ring_size)
{
	u16 status_ring_size;
	struct wil_ring *ring = &wil->ring_rx;
	int rc;
	size_t elem_size = wil->use_compressed_rx_status ?
		sizeof(struct wil_rx_status_compressed) :
		sizeof(struct wil_rx_status_extended);
	int i;
	u16 max_rx_pl_per_desc;

	if (wil->rx_status_ring_order < WIL_SRING_SIZE_ORDER_MIN ||
	    wil->rx_status_ring_order > WIL_SRING_SIZE_ORDER_MAX)
		wil->rx_status_ring_order = WIL_RX_SRING_SIZE_ORDER_DEFAULT;

	status_ring_size = 1 << wil->rx_status_ring_order;

	wil_dbg_misc(wil,
		     "rx_init, desc_ring_size=%u, status_ring_size=%u, elem_size=%zu\n",
		     desc_ring_size, status_ring_size, elem_size);

	wil_rx_buf_len_init_edma(wil);

	max_rx_pl_per_desc = wil->rx_buf_len + ETH_HLEN +
		WIL_EDMA_MAX_DATA_OFFSET;

	/* Use debugfs dbg_num_rx_srings if set, reserve one sring for TX */
	if (wil->num_rx_status_rings > WIL6210_MAX_STATUS_RINGS - 1)
		wil->num_rx_status_rings = WIL6210_MAX_STATUS_RINGS - 1;

	wil_dbg_misc(wil, "rx_init: allocate %d status rings\n",
		     wil->num_rx_status_rings);

	rc = wil_wmi_cfg_def_rx_offload(wil, max_rx_pl_per_desc);
	if (rc)
		return rc;

	/* Allocate status ring */
	for (i = 0; i < wil->num_rx_status_rings; i++) {
		int sring_id = wil_find_free_sring(wil);

		if (sring_id < 0) {
			rc = -EFAULT;
			goto err_free_status;
		}
		rc = wil_init_rx_sring(wil, status_ring_size, elem_size,
				       sring_id);
		if (rc)
			goto err_free_status;
	}

	/* Allocate descriptor ring */
	rc = wil_init_rx_desc_ring(wil, desc_ring_size,
				   WIL_DEFAULT_RX_STATUS_RING_ID);
	if (rc)
		goto err_free_status;

	if (wil->rx_buff_id_count >= status_ring_size) {
		wil_info(wil,
			 "rx_buff_id_count %d exceeds sring_size %d. set it to %d\n",
			 wil->rx_buff_id_count, status_ring_size,
			 status_ring_size - 1);
		wil->rx_buff_id_count = status_ring_size - 1;
	}

	/* Allocate Rx buffer array */
	rc = wil_init_rx_buff_arr(wil, wil->rx_buff_id_count);
	if (rc)
		goto err_free_desc;

	/* Fill descriptor ring with credits */
	rc = wil_rx_refill_edma(wil);
	if (rc)
		goto err_free_rx_buff_arr;

	return 0;
err_free_rx_buff_arr:
	wil_free_rx_buff_arr(wil);
err_free_desc:
	wil_ring_free_edma(wil, ring);
err_free_status:
	for (i = 0; i < wil->num_rx_status_rings; i++)
		wil_sring_free(wil, &wil->srings[i]);

	return rc;
}

static int wil_ring_init_tx_edma(struct wil6210_vif *vif, int ring_id,
				 int size, int cid, int tid)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	int rc;
	struct wil_ring *ring = &wil->ring_tx[ring_id];
	struct wil_ring_tx_data *txdata = &wil->ring_tx_data[ring_id];

	lockdep_assert_held(&wil->mutex);

	wil_dbg_misc(wil,
		     "init TX ring: ring_id=%u, cid=%u, tid=%u, sring_id=%u\n",
		     ring_id, cid, tid, wil->tx_sring_idx);

	wil_tx_data_init(txdata);
	ring->size = size;
	rc = wil_ring_alloc_desc_ring(wil, ring);
	if (rc)
		goto out;

	wil->ring2cid_tid[ring_id][0] = cid;
	wil->ring2cid_tid[ring_id][1] = tid;
	if (!vif->privacy)
		txdata->dot1x_open = true;

	rc = wil_wmi_tx_desc_ring_add(vif, ring_id, cid, tid);
	if (rc) {
		wil_err(wil, "WMI_TX_DESC_RING_ADD_CMD failed\n");
		goto out_free;
	}

	if (txdata->dot1x_open && agg_wsize >= 0)
		wil_addba_tx_request(wil, ring_id, agg_wsize);

	return 0;
 out_free:
	spin_lock_bh(&txdata->lock);
	txdata->dot1x_open = false;
	txdata->enabled = 0;
	spin_unlock_bh(&txdata->lock);
	wil_ring_free_edma(wil, ring);
	wil->ring2cid_tid[ring_id][0] = WIL6210_MAX_CID;
	wil->ring2cid_tid[ring_id][1] = 0;

 out:
	return rc;
}

static int wil_ring_init_bcast_edma(struct wil6210_vif *vif, int ring_id,
				    int size)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wil_ring *ring = &wil->ring_tx[ring_id];
	int rc;
	struct wil_ring_tx_data *txdata = &wil->ring_tx_data[ring_id];

	wil_dbg_misc(wil, "init bcast: ring_id=%d, sring_id=%d\n",
		     ring_id, wil->tx_sring_idx);

	lockdep_assert_held(&wil->mutex);

	wil_tx_data_init(txdata);
	ring->size = size;
	ring->is_rx = false;
	rc = wil_ring_alloc_desc_ring(wil, ring);
	if (rc)
		goto out;

	wil->ring2cid_tid[ring_id][0] = WIL6210_MAX_CID; /* CID */
	wil->ring2cid_tid[ring_id][1] = 0; /* TID */
	if (!vif->privacy)
		txdata->dot1x_open = true;

	rc = wil_wmi_bcast_desc_ring_add(vif, ring_id);
	if (rc)
		goto out_free;

	return 0;

 out_free:
	spin_lock_bh(&txdata->lock);
	txdata->enabled = 0;
	txdata->dot1x_open = false;
	spin_unlock_bh(&txdata->lock);
	wil_ring_free_edma(wil, ring);

out:
	return rc;
}

static void wil_tx_fini_edma(struct wil6210_priv *wil)
{
	struct wil_status_ring *sring = &wil->srings[wil->tx_sring_idx];

	wil_dbg_misc(wil, "free TX sring\n");

	wil_sring_free(wil, sring);
}

static void wil_rx_data_free(struct wil_status_ring *sring)
{
	if (!sring)
		return;

	kfree_skb(sring->rx_data.skb);
	sring->rx_data.skb = NULL;
}

static void wil_rx_fini_edma(struct wil6210_priv *wil)
{
	struct wil_ring *ring = &wil->ring_rx;
	int i;

	wil_dbg_misc(wil, "rx_fini_edma\n");

	wil_ring_free_edma(wil, ring);

	for (i = 0; i < wil->num_rx_status_rings; i++) {
		wil_rx_data_free(&wil->srings[i]);
		wil_sring_free(wil, &wil->srings[i]);
	}

	wil_free_rx_buff_arr(wil);
}

void wil_init_txrx_ops_edma(struct wil6210_priv *wil)
{
	wil->txrx_ops.configure_interrupt_moderation =
		wil_configure_interrupt_moderation_edma;
	/* TX ops */
	wil->txrx_ops.ring_init_tx = wil_ring_init_tx_edma;
	wil->txrx_ops.ring_fini_tx = wil_ring_free_edma;
	wil->txrx_ops.ring_init_bcast = wil_ring_init_bcast_edma;
	wil->txrx_ops.tx_init = wil_tx_init_edma;
	wil->txrx_ops.tx_fini = wil_tx_fini_edma;
	/* RX ops */
	wil->txrx_ops.rx_init = wil_rx_init_edma;
	wil->txrx_ops.rx_fini = wil_rx_fini_edma;
}


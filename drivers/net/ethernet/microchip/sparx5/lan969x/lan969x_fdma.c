// SPDX-License-Identifier: GPL-2.0+
/* Microchip lan969x Switch driver
 *
 * Copyright (c) 2025 Microchip Technology Inc. and its subsidiaries.
 */
#include <net/page_pool/helpers.h>

#include "../sparx5_main.h"
#include "../sparx5_main_regs.h"
#include "../sparx5_port.h"

#include "fdma_api.h"
#include "lan969x.h"

#define FDMA_PRIV(fdma) ((struct sparx5 *)((fdma)->priv))

static int lan969x_fdma_tx_dataptr_cb(struct fdma *fdma, int dcb, int db,
				      u64 *dataptr)
{
	*dataptr = FDMA_PRIV(fdma)->tx.dbs[dcb].dma_addr;

	return 0;
}

static int lan969x_fdma_rx_dataptr_cb(struct fdma *fdma, int dcb, int db,
				      u64 *dataptr)
{
	struct sparx5_rx *rx = &FDMA_PRIV(fdma)->rx;
	struct page *page;

	page = page_pool_dev_alloc_pages(rx->page_pool);
	if (unlikely(!page))
		return -ENOMEM;

	rx->page[dcb][db] = page;

	*dataptr = page_pool_get_dma_addr(page);

	return 0;
}

static int lan969x_fdma_get_next_dcb(struct sparx5_tx *tx)
{
	struct fdma *fdma = &tx->fdma;

	for (int i = 0; i < fdma->n_dcbs; ++i)
		if (!tx->dbs[i].used && !fdma_is_last(fdma, &fdma->dcbs[i]))
			return i;

	return -ENOSPC;
}

static void lan969x_fdma_tx_clear_buf(struct sparx5 *sparx5, int weight)
{
	struct fdma *fdma = &sparx5->tx.fdma;
	struct sparx5_tx_buf *db;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&sparx5->tx_lock, flags);

	for (i = 0; i < fdma->n_dcbs; ++i) {
		db = &sparx5->tx.dbs[i];

		if (!db->used)
			continue;

		if (!fdma_db_is_done(fdma_db_get(fdma, i, 0)))
			continue;

		db->dev->stats.tx_bytes += db->skb->len;
		db->dev->stats.tx_packets++;
		sparx5->tx.packets++;

		dma_unmap_single(sparx5->dev,
				 db->dma_addr,
				 db->skb->len,
				 DMA_TO_DEVICE);

		if (!db->ptp)
			napi_consume_skb(db->skb, weight);

		db->used = false;
	}

	spin_unlock_irqrestore(&sparx5->tx_lock, flags);
}

static void lan969x_fdma_free_pages(struct sparx5_rx *rx)
{
	struct fdma *fdma = &rx->fdma;

	for (int i = 0; i < fdma->n_dcbs; ++i) {
		for (int j = 0; j < fdma->n_dbs; ++j)
			page_pool_put_full_page(rx->page_pool,
						rx->page[i][j], false);
	}
}

static struct sk_buff *lan969x_fdma_rx_get_frame(struct sparx5 *sparx5,
						 struct sparx5_rx *rx)
{
	const struct sparx5_consts *consts = sparx5->data->consts;
	struct fdma *fdma = &rx->fdma;
	struct sparx5_port *port;
	struct frame_info fi;
	struct sk_buff *skb;
	struct fdma_db *db;
	struct page *page;

	db = &fdma->dcbs[fdma->dcb_index].db[fdma->db_index];
	page = rx->page[fdma->dcb_index][fdma->db_index];

	sparx5_ifh_parse(sparx5, page_address(page), &fi);
	port = fi.src_port < consts->n_ports ? sparx5->ports[fi.src_port] :
					       NULL;
	if (WARN_ON(!port))
		goto free_page;

	skb = build_skb(page_address(page), fdma->db_size);
	if (unlikely(!skb))
		goto free_page;

	skb_mark_for_recycle(skb);
	skb_put(skb, fdma_db_len_get(db));
	skb_pull(skb, IFH_LEN * sizeof(u32));

	skb->dev = port->ndev;

	if (likely(!(skb->dev->features & NETIF_F_RXFCS)))
		skb_trim(skb, skb->len - ETH_FCS_LEN);

	sparx5_ptp_rxtstamp(sparx5, skb, fi.timestamp);
	skb->protocol = eth_type_trans(skb, skb->dev);

	if (test_bit(port->portno, sparx5->bridge_mask))
		skb->offload_fwd_mark = 1;

	skb->dev->stats.rx_bytes += skb->len;
	skb->dev->stats.rx_packets++;

	return skb;

free_page:
	page_pool_recycle_direct(rx->page_pool, page);

	return NULL;
}

static int lan969x_fdma_rx_alloc(struct sparx5 *sparx5)
{
	struct sparx5_rx *rx = &sparx5->rx;
	struct fdma *fdma = &rx->fdma;
	int err;

	struct page_pool_params pp_params = {
		.order = 0,
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.pool_size = fdma->n_dcbs * fdma->n_dbs,
		.nid = NUMA_NO_NODE,
		.dev = sparx5->dev,
		.dma_dir = DMA_FROM_DEVICE,
		.offset = 0,
		.max_len = fdma->db_size -
			   SKB_DATA_ALIGN(sizeof(struct skb_shared_info)),
	};

	rx->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rx->page_pool))
		return PTR_ERR(rx->page_pool);

	err = fdma_alloc_coherent(sparx5->dev, fdma);
	if (err)
		return err;

	fdma_dcbs_init(fdma,
		       FDMA_DCB_INFO_DATAL(fdma->db_size),
		       FDMA_DCB_STATUS_INTR);

	return 0;
}

static int lan969x_fdma_tx_alloc(struct sparx5 *sparx5)
{
	struct sparx5_tx *tx = &sparx5->tx;
	struct fdma *fdma = &tx->fdma;
	int err;

	tx->dbs = kcalloc(fdma->n_dcbs,
			  sizeof(struct sparx5_tx_buf),
			  GFP_KERNEL);
	if (!tx->dbs)
		return -ENOMEM;

	err = fdma_alloc_coherent(sparx5->dev, fdma);
	if (err) {
		kfree(tx->dbs);
		return err;
	}

	fdma_dcbs_init(fdma,
		       FDMA_DCB_INFO_DATAL(fdma->db_size),
		       FDMA_DCB_STATUS_DONE);

	return 0;
}

static void lan969x_fdma_rx_init(struct sparx5 *sparx5)
{
	struct fdma *fdma = &sparx5->rx.fdma;

	fdma->channel_id = FDMA_XTR_CHANNEL;
	fdma->n_dcbs = FDMA_DCB_MAX;
	fdma->n_dbs = 1;
	fdma->priv = sparx5;
	fdma->size = fdma_get_size(fdma);
	fdma->db_size = PAGE_SIZE;
	fdma->ops.dataptr_cb = &lan969x_fdma_rx_dataptr_cb;
	fdma->ops.nextptr_cb = &fdma_nextptr_cb;

	/* Fetch a netdev for SKB and NAPI use, any will do */
	for (int idx = 0; idx < sparx5->data->consts->n_ports; ++idx) {
		struct sparx5_port *port = sparx5->ports[idx];

		if (port && port->ndev) {
			sparx5->rx.ndev = port->ndev;
			break;
		}
	}
}

static void lan969x_fdma_tx_init(struct sparx5 *sparx5)
{
	struct fdma *fdma = &sparx5->tx.fdma;

	fdma->channel_id = FDMA_INJ_CHANNEL;
	fdma->n_dcbs = FDMA_DCB_MAX;
	fdma->n_dbs = 1;
	fdma->priv = sparx5;
	fdma->size = fdma_get_size(fdma);
	fdma->db_size = PAGE_SIZE;
	fdma->ops.dataptr_cb = &lan969x_fdma_tx_dataptr_cb;
	fdma->ops.nextptr_cb = &fdma_nextptr_cb;
}

int lan969x_fdma_napi_poll(struct napi_struct *napi, int weight)
{
	struct sparx5_rx *rx = container_of(napi, struct sparx5_rx, napi);
	struct sparx5 *sparx5 = container_of(rx, struct sparx5, rx);
	int old_dcb, dcb_reload, counter = 0;
	struct fdma *fdma = &rx->fdma;
	struct sk_buff *skb;

	dcb_reload = fdma->dcb_index;

	lan969x_fdma_tx_clear_buf(sparx5, weight);

	/* Process RX data */
	while (counter < weight) {
		if (!fdma_has_frames(fdma))
			break;

		skb = lan969x_fdma_rx_get_frame(sparx5, rx);
		if (!skb)
			break;

		napi_gro_receive(&rx->napi, skb);

		fdma_db_advance(fdma);
		counter++;
		/* Check if the DCB can be reused */
		if (fdma_dcb_is_reusable(fdma))
			continue;

		fdma_db_reset(fdma);
		fdma_dcb_advance(fdma);
	}

	/* Allocate new pages and map them */
	while (dcb_reload != fdma->dcb_index) {
		old_dcb = dcb_reload;
		dcb_reload++;
		 /* n_dcbs must be a power of 2 */
		dcb_reload &= fdma->n_dcbs - 1;

		fdma_dcb_add(fdma,
			     old_dcb,
			     FDMA_DCB_INFO_DATAL(fdma->db_size),
			     FDMA_DCB_STATUS_INTR);

		sparx5_fdma_reload(sparx5, fdma);
	}

	if (counter < weight && napi_complete_done(napi, counter))
		spx5_wr(0xff, sparx5, FDMA_INTR_DB_ENA);

	return counter;
}

int lan969x_fdma_xmit(struct sparx5 *sparx5, u32 *ifh, struct sk_buff *skb,
		      struct net_device *dev)
{
	int next_dcb, needed_headroom, needed_tailroom, err;
	struct sparx5_tx *tx = &sparx5->tx;
	struct fdma *fdma = &tx->fdma;
	struct sparx5_tx_buf *db_buf;
	u64 status;

	next_dcb = lan969x_fdma_get_next_dcb(tx);
	if (next_dcb < 0)
		return -EBUSY;

	needed_headroom = max_t(int, IFH_LEN * 4 - skb_headroom(skb), 0);
	needed_tailroom = max_t(int, ETH_FCS_LEN - skb_tailroom(skb), 0);
	if (needed_headroom || needed_tailroom || skb_header_cloned(skb)) {
		err = pskb_expand_head(skb, needed_headroom, needed_tailroom,
				       GFP_ATOMIC);
		if (unlikely(err))
			return err;
	}

	skb_push(skb, IFH_LEN * 4);
	memcpy(skb->data, ifh, IFH_LEN * 4);
	skb_put(skb, ETH_FCS_LEN);

	db_buf = &tx->dbs[next_dcb];
	db_buf->dma_addr = dma_map_single(sparx5->dev,
					  skb->data,
					  skb->len,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(sparx5->dev, db_buf->dma_addr))
		return -ENOMEM;

	db_buf->dev = dev;
	db_buf->skb = skb;
	db_buf->ptp = false;
	db_buf->used = true;

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
	    SPARX5_SKB_CB(skb)->rew_op == IFH_REW_OP_TWO_STEP_PTP)
		db_buf->ptp = true;

	status = FDMA_DCB_STATUS_SOF |
		 FDMA_DCB_STATUS_EOF |
		 FDMA_DCB_STATUS_BLOCKO(0) |
		 FDMA_DCB_STATUS_BLOCKL(skb->len) |
		 FDMA_DCB_STATUS_INTR;

	fdma_dcb_advance(fdma);
	fdma_dcb_add(fdma, next_dcb, 0, status);

	sparx5_fdma_reload(sparx5, fdma);

	return NETDEV_TX_OK;
}

int lan969x_fdma_init(struct sparx5 *sparx5)
{
	struct sparx5_rx *rx = &sparx5->rx;
	int err;

	lan969x_fdma_rx_init(sparx5);
	lan969x_fdma_tx_init(sparx5);
	sparx5_fdma_injection_mode(sparx5);

	err = dma_set_mask_and_coherent(sparx5->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(sparx5->dev, "Failed to set 64-bit FDMA mask");
		return err;
	}

	err = lan969x_fdma_rx_alloc(sparx5);
	if (err) {
		dev_err(sparx5->dev, "Failed to allocate RX buffers: %d\n",
			err);
		return err;
	}

	err = lan969x_fdma_tx_alloc(sparx5);
	if (err) {
		fdma_free_coherent(sparx5->dev, &rx->fdma);
		dev_err(sparx5->dev, "Failed to allocate TX buffers: %d\n",
			err);
		return err;
	}

	/* Reset FDMA state */
	spx5_wr(FDMA_CTRL_NRESET_SET(0), sparx5, FDMA_CTRL);
	spx5_wr(FDMA_CTRL_NRESET_SET(1), sparx5, FDMA_CTRL);

	return err;
}

int lan969x_fdma_deinit(struct sparx5 *sparx5)
{
	struct sparx5_rx *rx = &sparx5->rx;
	struct sparx5_tx *tx = &sparx5->tx;

	sparx5_fdma_stop(sparx5);
	fdma_free_coherent(sparx5->dev, &tx->fdma);
	fdma_free_coherent(sparx5->dev, &rx->fdma);
	lan969x_fdma_free_pages(rx);
	page_pool_destroy(rx->page_pool);

	return 0;
}

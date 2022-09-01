// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

static int lan966x_fdma_channel_active(struct lan966x *lan966x)
{
	return lan_rd(lan966x, FDMA_CH_ACTIVE);
}

static struct page *lan966x_fdma_rx_alloc_page(struct lan966x_rx *rx,
					       struct lan966x_db *db)
{
	struct lan966x *lan966x = rx->lan966x;
	dma_addr_t dma_addr;
	struct page *page;

	page = dev_alloc_pages(rx->page_order);
	if (unlikely(!page))
		return NULL;

	dma_addr = dma_map_page(lan966x->dev, page, 0,
				PAGE_SIZE << rx->page_order,
				DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(lan966x->dev, dma_addr)))
		goto free_page;

	db->dataptr = dma_addr;

	return page;

free_page:
	__free_pages(page, rx->page_order);
	return NULL;
}

static void lan966x_fdma_rx_free_pages(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;
	struct lan966x_rx_dcb *dcb;
	struct lan966x_db *db;
	int i, j;

	for (i = 0; i < FDMA_DCB_MAX; ++i) {
		dcb = &rx->dcbs[i];

		for (j = 0; j < FDMA_RX_DCB_MAX_DBS; ++j) {
			db = &dcb->db[j];
			dma_unmap_single(lan966x->dev,
					 (dma_addr_t)db->dataptr,
					 PAGE_SIZE << rx->page_order,
					 DMA_FROM_DEVICE);
			__free_pages(rx->page[i][j], rx->page_order);
		}
	}
}

static void lan966x_fdma_rx_add_dcb(struct lan966x_rx *rx,
				    struct lan966x_rx_dcb *dcb,
				    u64 nextptr)
{
	struct lan966x_db *db;
	int i;

	for (i = 0; i < FDMA_RX_DCB_MAX_DBS; ++i) {
		db = &dcb->db[i];
		db->status = FDMA_DCB_STATUS_INTR;
	}

	dcb->nextptr = FDMA_DCB_INVALID_DATA;
	dcb->info = FDMA_DCB_INFO_DATAL(PAGE_SIZE << rx->page_order);

	rx->last_entry->nextptr = nextptr;
	rx->last_entry = dcb;
}

static int lan966x_fdma_rx_alloc(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;
	struct lan966x_rx_dcb *dcb;
	struct lan966x_db *db;
	struct page *page;
	int i, j;
	int size;

	/* calculate how many pages are needed to allocate the dcbs */
	size = sizeof(struct lan966x_rx_dcb) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);

	rx->dcbs = dma_alloc_coherent(lan966x->dev, size, &rx->dma, GFP_KERNEL);
	if (!rx->dcbs)
		return -ENOMEM;

	rx->last_entry = rx->dcbs;
	rx->db_index = 0;
	rx->dcb_index = 0;

	/* Now for each dcb allocate the dbs */
	for (i = 0; i < FDMA_DCB_MAX; ++i) {
		dcb = &rx->dcbs[i];
		dcb->info = 0;

		/* For each db allocate a page and map it to the DB dataptr. */
		for (j = 0; j < FDMA_RX_DCB_MAX_DBS; ++j) {
			db = &dcb->db[j];
			page = lan966x_fdma_rx_alloc_page(rx, db);
			if (!page)
				return -ENOMEM;

			db->status = 0;
			rx->page[i][j] = page;
		}

		lan966x_fdma_rx_add_dcb(rx, dcb, rx->dma + sizeof(*dcb) * i);
	}

	return 0;
}

static void lan966x_fdma_rx_free(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;
	u32 size;

	/* Now it is possible to do the cleanup of dcb */
	size = sizeof(struct lan966x_tx_dcb) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	dma_free_coherent(lan966x->dev, size, rx->dcbs, rx->dma);
}

static void lan966x_fdma_rx_start(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;
	u32 mask;

	/* When activating a channel, first is required to write the first DCB
	 * address and then to activate it
	 */
	lan_wr(lower_32_bits((u64)rx->dma), lan966x,
	       FDMA_DCB_LLP(rx->channel_id));
	lan_wr(upper_32_bits((u64)rx->dma), lan966x,
	       FDMA_DCB_LLP1(rx->channel_id));

	lan_wr(FDMA_CH_CFG_CH_DCB_DB_CNT_SET(FDMA_RX_DCB_MAX_DBS) |
	       FDMA_CH_CFG_CH_INTR_DB_EOF_ONLY_SET(1) |
	       FDMA_CH_CFG_CH_INJ_PORT_SET(0) |
	       FDMA_CH_CFG_CH_MEM_SET(1),
	       lan966x, FDMA_CH_CFG(rx->channel_id));

	/* Start fdma */
	lan_rmw(FDMA_PORT_CTRL_XTR_STOP_SET(0),
		FDMA_PORT_CTRL_XTR_STOP,
		lan966x, FDMA_PORT_CTRL(0));

	/* Enable interrupts */
	mask = lan_rd(lan966x, FDMA_INTR_DB_ENA);
	mask = FDMA_INTR_DB_ENA_INTR_DB_ENA_GET(mask);
	mask |= BIT(rx->channel_id);
	lan_rmw(FDMA_INTR_DB_ENA_INTR_DB_ENA_SET(mask),
		FDMA_INTR_DB_ENA_INTR_DB_ENA,
		lan966x, FDMA_INTR_DB_ENA);

	/* Activate the channel */
	lan_rmw(FDMA_CH_ACTIVATE_CH_ACTIVATE_SET(BIT(rx->channel_id)),
		FDMA_CH_ACTIVATE_CH_ACTIVATE,
		lan966x, FDMA_CH_ACTIVATE);
}

static void lan966x_fdma_rx_disable(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;
	u32 val;

	/* Disable the channel */
	lan_rmw(FDMA_CH_DISABLE_CH_DISABLE_SET(BIT(rx->channel_id)),
		FDMA_CH_DISABLE_CH_DISABLE,
		lan966x, FDMA_CH_DISABLE);

	readx_poll_timeout_atomic(lan966x_fdma_channel_active, lan966x,
				  val, !(val & BIT(rx->channel_id)),
				  READL_SLEEP_US, READL_TIMEOUT_US);

	lan_rmw(FDMA_CH_DB_DISCARD_DB_DISCARD_SET(BIT(rx->channel_id)),
		FDMA_CH_DB_DISCARD_DB_DISCARD,
		lan966x, FDMA_CH_DB_DISCARD);
}

static void lan966x_fdma_rx_reload(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;

	lan_rmw(FDMA_CH_RELOAD_CH_RELOAD_SET(BIT(rx->channel_id)),
		FDMA_CH_RELOAD_CH_RELOAD,
		lan966x, FDMA_CH_RELOAD);
}

static void lan966x_fdma_tx_add_dcb(struct lan966x_tx *tx,
				    struct lan966x_tx_dcb *dcb)
{
	dcb->nextptr = FDMA_DCB_INVALID_DATA;
	dcb->info = 0;
}

static int lan966x_fdma_tx_alloc(struct lan966x_tx *tx)
{
	struct lan966x *lan966x = tx->lan966x;
	struct lan966x_tx_dcb *dcb;
	struct lan966x_db *db;
	int size;
	int i, j;

	tx->dcbs_buf = kcalloc(FDMA_DCB_MAX, sizeof(struct lan966x_tx_dcb_buf),
			       GFP_KERNEL);
	if (!tx->dcbs_buf)
		return -ENOMEM;

	/* calculate how many pages are needed to allocate the dcbs */
	size = sizeof(struct lan966x_tx_dcb) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	tx->dcbs = dma_alloc_coherent(lan966x->dev, size, &tx->dma, GFP_KERNEL);
	if (!tx->dcbs)
		goto out;

	/* Now for each dcb allocate the db */
	for (i = 0; i < FDMA_DCB_MAX; ++i) {
		dcb = &tx->dcbs[i];

		for (j = 0; j < FDMA_TX_DCB_MAX_DBS; ++j) {
			db = &dcb->db[j];
			db->dataptr = 0;
			db->status = 0;
		}

		lan966x_fdma_tx_add_dcb(tx, dcb);
	}

	return 0;

out:
	kfree(tx->dcbs_buf);
	return -ENOMEM;
}

static void lan966x_fdma_tx_free(struct lan966x_tx *tx)
{
	struct lan966x *lan966x = tx->lan966x;
	int size;

	kfree(tx->dcbs_buf);

	size = sizeof(struct lan966x_tx_dcb) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	dma_free_coherent(lan966x->dev, size, tx->dcbs, tx->dma);
}

static void lan966x_fdma_tx_activate(struct lan966x_tx *tx)
{
	struct lan966x *lan966x = tx->lan966x;
	u32 mask;

	/* When activating a channel, first is required to write the first DCB
	 * address and then to activate it
	 */
	lan_wr(lower_32_bits((u64)tx->dma), lan966x,
	       FDMA_DCB_LLP(tx->channel_id));
	lan_wr(upper_32_bits((u64)tx->dma), lan966x,
	       FDMA_DCB_LLP1(tx->channel_id));

	lan_wr(FDMA_CH_CFG_CH_DCB_DB_CNT_SET(FDMA_TX_DCB_MAX_DBS) |
	       FDMA_CH_CFG_CH_INTR_DB_EOF_ONLY_SET(1) |
	       FDMA_CH_CFG_CH_INJ_PORT_SET(0) |
	       FDMA_CH_CFG_CH_MEM_SET(1),
	       lan966x, FDMA_CH_CFG(tx->channel_id));

	/* Start fdma */
	lan_rmw(FDMA_PORT_CTRL_INJ_STOP_SET(0),
		FDMA_PORT_CTRL_INJ_STOP,
		lan966x, FDMA_PORT_CTRL(0));

	/* Enable interrupts */
	mask = lan_rd(lan966x, FDMA_INTR_DB_ENA);
	mask = FDMA_INTR_DB_ENA_INTR_DB_ENA_GET(mask);
	mask |= BIT(tx->channel_id);
	lan_rmw(FDMA_INTR_DB_ENA_INTR_DB_ENA_SET(mask),
		FDMA_INTR_DB_ENA_INTR_DB_ENA,
		lan966x, FDMA_INTR_DB_ENA);

	/* Activate the channel */
	lan_rmw(FDMA_CH_ACTIVATE_CH_ACTIVATE_SET(BIT(tx->channel_id)),
		FDMA_CH_ACTIVATE_CH_ACTIVATE,
		lan966x, FDMA_CH_ACTIVATE);
}

static void lan966x_fdma_tx_disable(struct lan966x_tx *tx)
{
	struct lan966x *lan966x = tx->lan966x;
	u32 val;

	/* Disable the channel */
	lan_rmw(FDMA_CH_DISABLE_CH_DISABLE_SET(BIT(tx->channel_id)),
		FDMA_CH_DISABLE_CH_DISABLE,
		lan966x, FDMA_CH_DISABLE);

	readx_poll_timeout_atomic(lan966x_fdma_channel_active, lan966x,
				  val, !(val & BIT(tx->channel_id)),
				  READL_SLEEP_US, READL_TIMEOUT_US);

	lan_rmw(FDMA_CH_DB_DISCARD_DB_DISCARD_SET(BIT(tx->channel_id)),
		FDMA_CH_DB_DISCARD_DB_DISCARD,
		lan966x, FDMA_CH_DB_DISCARD);

	tx->activated = false;
}

static void lan966x_fdma_tx_reload(struct lan966x_tx *tx)
{
	struct lan966x *lan966x = tx->lan966x;

	/* Write the registers to reload the channel */
	lan_rmw(FDMA_CH_RELOAD_CH_RELOAD_SET(BIT(tx->channel_id)),
		FDMA_CH_RELOAD_CH_RELOAD,
		lan966x, FDMA_CH_RELOAD);
}

static void lan966x_fdma_wakeup_netdev(struct lan966x *lan966x)
{
	struct lan966x_port *port;
	int i;

	for (i = 0; i < lan966x->num_phys_ports; ++i) {
		port = lan966x->ports[i];
		if (!port)
			continue;

		if (netif_queue_stopped(port->dev))
			netif_wake_queue(port->dev);
	}
}

static void lan966x_fdma_stop_netdev(struct lan966x *lan966x)
{
	struct lan966x_port *port;
	int i;

	for (i = 0; i < lan966x->num_phys_ports; ++i) {
		port = lan966x->ports[i];
		if (!port)
			continue;

		netif_stop_queue(port->dev);
	}
}

static void lan966x_fdma_tx_clear_buf(struct lan966x *lan966x, int weight)
{
	struct lan966x_tx *tx = &lan966x->tx;
	struct lan966x_tx_dcb_buf *dcb_buf;
	struct lan966x_db *db;
	unsigned long flags;
	bool clear = false;
	int i;

	spin_lock_irqsave(&lan966x->tx_lock, flags);
	for (i = 0; i < FDMA_DCB_MAX; ++i) {
		dcb_buf = &tx->dcbs_buf[i];

		if (!dcb_buf->used)
			continue;

		db = &tx->dcbs[i].db[0];
		if (!(db->status & FDMA_DCB_STATUS_DONE))
			continue;

		dcb_buf->dev->stats.tx_packets++;
		dcb_buf->dev->stats.tx_bytes += dcb_buf->skb->len;

		dcb_buf->used = false;
		dma_unmap_single(lan966x->dev,
				 dcb_buf->dma_addr,
				 dcb_buf->skb->len,
				 DMA_TO_DEVICE);
		if (!dcb_buf->ptp)
			dev_kfree_skb_any(dcb_buf->skb);

		clear = true;
	}

	if (clear)
		lan966x_fdma_wakeup_netdev(lan966x);

	spin_unlock_irqrestore(&lan966x->tx_lock, flags);
}

static bool lan966x_fdma_rx_more_frames(struct lan966x_rx *rx)
{
	struct lan966x_db *db;

	/* Check if there is any data */
	db = &rx->dcbs[rx->dcb_index].db[rx->db_index];
	if (unlikely(!(db->status & FDMA_DCB_STATUS_DONE)))
		return false;

	return true;
}

static struct sk_buff *lan966x_fdma_rx_get_frame(struct lan966x_rx *rx)
{
	struct lan966x *lan966x = rx->lan966x;
	u64 src_port, timestamp;
	struct lan966x_db *db;
	struct sk_buff *skb;
	struct page *page;

	/* Get the received frame and unmap it */
	db = &rx->dcbs[rx->dcb_index].db[rx->db_index];
	page = rx->page[rx->dcb_index][rx->db_index];
	skb = build_skb(page_address(page), PAGE_SIZE << rx->page_order);
	if (unlikely(!skb))
		goto unmap_page;

	dma_unmap_single(lan966x->dev, (dma_addr_t)db->dataptr,
			 FDMA_DCB_STATUS_BLOCKL(db->status),
			 DMA_FROM_DEVICE);
	skb_put(skb, FDMA_DCB_STATUS_BLOCKL(db->status));

	lan966x_ifh_get_src_port(skb->data, &src_port);
	lan966x_ifh_get_timestamp(skb->data, &timestamp);

	if (WARN_ON(src_port >= lan966x->num_phys_ports))
		goto free_skb;

	skb->dev = lan966x->ports[src_port]->dev;
	skb_pull(skb, IFH_LEN * sizeof(u32));

	if (likely(!(skb->dev->features & NETIF_F_RXFCS)))
		skb_trim(skb, skb->len - ETH_FCS_LEN);

	lan966x_ptp_rxtstamp(lan966x, skb, timestamp);
	skb->protocol = eth_type_trans(skb, skb->dev);

	if (lan966x->bridge_mask & BIT(src_port)) {
		skb->offload_fwd_mark = 1;

		skb_reset_network_header(skb);
		if (!lan966x_hw_offload(lan966x, src_port, skb))
			skb->offload_fwd_mark = 0;
	}

	skb->dev->stats.rx_bytes += skb->len;
	skb->dev->stats.rx_packets++;

	return skb;

free_skb:
	kfree_skb(skb);
unmap_page:
	dma_unmap_page(lan966x->dev, (dma_addr_t)db->dataptr,
		       FDMA_DCB_STATUS_BLOCKL(db->status),
		       DMA_FROM_DEVICE);
	__free_pages(page, rx->page_order);

	return NULL;
}

static int lan966x_fdma_napi_poll(struct napi_struct *napi, int weight)
{
	struct lan966x *lan966x = container_of(napi, struct lan966x, napi);
	struct lan966x_rx *rx = &lan966x->rx;
	int dcb_reload = rx->dcb_index;
	struct lan966x_rx_dcb *old_dcb;
	struct lan966x_db *db;
	struct sk_buff *skb;
	struct page *page;
	int counter = 0;
	u64 nextptr;

	lan966x_fdma_tx_clear_buf(lan966x, weight);

	/* Get all received skb */
	while (counter < weight) {
		if (!lan966x_fdma_rx_more_frames(rx))
			break;

		skb = lan966x_fdma_rx_get_frame(rx);

		rx->page[rx->dcb_index][rx->db_index] = NULL;
		rx->dcb_index++;
		rx->dcb_index &= FDMA_DCB_MAX - 1;

		if (!skb)
			break;

		napi_gro_receive(&lan966x->napi, skb);
		counter++;
	}

	/* Allocate new pages and map them */
	while (dcb_reload != rx->dcb_index) {
		db = &rx->dcbs[dcb_reload].db[rx->db_index];
		page = lan966x_fdma_rx_alloc_page(rx, db);
		if (unlikely(!page))
			break;
		rx->page[dcb_reload][rx->db_index] = page;

		old_dcb = &rx->dcbs[dcb_reload];
		dcb_reload++;
		dcb_reload &= FDMA_DCB_MAX - 1;

		nextptr = rx->dma + ((unsigned long)old_dcb -
				     (unsigned long)rx->dcbs);
		lan966x_fdma_rx_add_dcb(rx, old_dcb, nextptr);
		lan966x_fdma_rx_reload(rx);
	}

	if (counter < weight && napi_complete_done(napi, counter))
		lan_wr(0xff, lan966x, FDMA_INTR_DB_ENA);

	return counter;
}

irqreturn_t lan966x_fdma_irq_handler(int irq, void *args)
{
	struct lan966x *lan966x = args;
	u32 db, err, err_type;

	db = lan_rd(lan966x, FDMA_INTR_DB);
	err = lan_rd(lan966x, FDMA_INTR_ERR);

	if (db) {
		lan_wr(0, lan966x, FDMA_INTR_DB_ENA);
		lan_wr(db, lan966x, FDMA_INTR_DB);

		napi_schedule(&lan966x->napi);
	}

	if (err) {
		err_type = lan_rd(lan966x, FDMA_ERRORS);

		WARN(1, "Unexpected error: %d, error_type: %d\n", err, err_type);

		lan_wr(err, lan966x, FDMA_INTR_ERR);
		lan_wr(err_type, lan966x, FDMA_ERRORS);
	}

	return IRQ_HANDLED;
}

static int lan966x_fdma_get_next_dcb(struct lan966x_tx *tx)
{
	struct lan966x_tx_dcb_buf *dcb_buf;
	int i;

	for (i = 0; i < FDMA_DCB_MAX; ++i) {
		dcb_buf = &tx->dcbs_buf[i];
		if (!dcb_buf->used && i != tx->last_in_use)
			return i;
	}

	return -1;
}

int lan966x_fdma_xmit(struct sk_buff *skb, __be32 *ifh, struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_tx_dcb_buf *next_dcb_buf;
	struct lan966x_tx_dcb *next_dcb, *dcb;
	struct lan966x_tx *tx = &lan966x->tx;
	struct lan966x_db *next_db;
	int needed_headroom;
	int needed_tailroom;
	dma_addr_t dma_addr;
	int next_to_use;
	int err;

	/* Get next index */
	next_to_use = lan966x_fdma_get_next_dcb(tx);
	if (next_to_use < 0) {
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	if (skb_put_padto(skb, ETH_ZLEN)) {
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* skb processing */
	needed_headroom = max_t(int, IFH_LEN * sizeof(u32) - skb_headroom(skb), 0);
	needed_tailroom = max_t(int, ETH_FCS_LEN - skb_tailroom(skb), 0);
	if (needed_headroom || needed_tailroom || skb_header_cloned(skb)) {
		err = pskb_expand_head(skb, needed_headroom, needed_tailroom,
				       GFP_ATOMIC);
		if (unlikely(err)) {
			dev->stats.tx_dropped++;
			err = NETDEV_TX_OK;
			goto release;
		}
	}

	skb_tx_timestamp(skb);
	skb_push(skb, IFH_LEN * sizeof(u32));
	memcpy(skb->data, ifh, IFH_LEN * sizeof(u32));
	skb_put(skb, 4);

	dma_addr = dma_map_single(lan966x->dev, skb->data, skb->len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(lan966x->dev, dma_addr)) {
		dev->stats.tx_dropped++;
		err = NETDEV_TX_OK;
		goto release;
	}

	/* Setup next dcb */
	next_dcb = &tx->dcbs[next_to_use];
	next_dcb->nextptr = FDMA_DCB_INVALID_DATA;

	next_db = &next_dcb->db[0];
	next_db->dataptr = dma_addr;
	next_db->status = FDMA_DCB_STATUS_SOF |
			  FDMA_DCB_STATUS_EOF |
			  FDMA_DCB_STATUS_INTR |
			  FDMA_DCB_STATUS_BLOCKO(0) |
			  FDMA_DCB_STATUS_BLOCKL(skb->len);

	/* Fill up the buffer */
	next_dcb_buf = &tx->dcbs_buf[next_to_use];
	next_dcb_buf->skb = skb;
	next_dcb_buf->dma_addr = dma_addr;
	next_dcb_buf->used = true;
	next_dcb_buf->ptp = false;
	next_dcb_buf->dev = dev;

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
	    LAN966X_SKB_CB(skb)->rew_op == IFH_REW_OP_TWO_STEP_PTP)
		next_dcb_buf->ptp = true;

	if (likely(lan966x->tx.activated)) {
		/* Connect current dcb to the next db */
		dcb = &tx->dcbs[tx->last_in_use];
		dcb->nextptr = tx->dma + (next_to_use *
					  sizeof(struct lan966x_tx_dcb));

		lan966x_fdma_tx_reload(tx);
	} else {
		/* Because it is first time, then just activate */
		lan966x->tx.activated = true;
		lan966x_fdma_tx_activate(tx);
	}

	/* Move to next dcb because this last in use */
	tx->last_in_use = next_to_use;

	return NETDEV_TX_OK;

release:
	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
	    LAN966X_SKB_CB(skb)->rew_op == IFH_REW_OP_TWO_STEP_PTP)
		lan966x_ptp_txtstamp_release(port, skb);

	dev_kfree_skb_any(skb);
	return err;
}

static int lan966x_fdma_get_max_mtu(struct lan966x *lan966x)
{
	int max_mtu = 0;
	int i;

	for (i = 0; i < lan966x->num_phys_ports; ++i) {
		int mtu;

		if (!lan966x->ports[i])
			continue;

		mtu = lan966x->ports[i]->dev->mtu;
		if (mtu > max_mtu)
			max_mtu = mtu;
	}

	return max_mtu;
}

static int lan966x_qsys_sw_status(struct lan966x *lan966x)
{
	return lan_rd(lan966x, QSYS_SW_STATUS(CPU_PORT));
}

static int lan966x_fdma_reload(struct lan966x *lan966x, int new_mtu)
{
	void *rx_dcbs, *tx_dcbs, *tx_dcbs_buf;
	dma_addr_t rx_dma, tx_dma;
	u32 size;
	int err;

	/* Store these for later to free them */
	rx_dma = lan966x->rx.dma;
	tx_dma = lan966x->tx.dma;
	rx_dcbs = lan966x->rx.dcbs;
	tx_dcbs = lan966x->tx.dcbs;
	tx_dcbs_buf = lan966x->tx.dcbs_buf;

	napi_synchronize(&lan966x->napi);
	napi_disable(&lan966x->napi);
	lan966x_fdma_stop_netdev(lan966x);

	lan966x_fdma_rx_disable(&lan966x->rx);
	lan966x_fdma_rx_free_pages(&lan966x->rx);
	lan966x->rx.page_order = round_up(new_mtu, PAGE_SIZE) / PAGE_SIZE - 1;
	err = lan966x_fdma_rx_alloc(&lan966x->rx);
	if (err)
		goto restore;
	lan966x_fdma_rx_start(&lan966x->rx);

	size = sizeof(struct lan966x_rx_dcb) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	dma_free_coherent(lan966x->dev, size, rx_dcbs, rx_dma);

	lan966x_fdma_tx_disable(&lan966x->tx);
	err = lan966x_fdma_tx_alloc(&lan966x->tx);
	if (err)
		goto restore_tx;

	size = sizeof(struct lan966x_tx_dcb) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	dma_free_coherent(lan966x->dev, size, tx_dcbs, tx_dma);

	kfree(tx_dcbs_buf);

	lan966x_fdma_wakeup_netdev(lan966x);
	napi_enable(&lan966x->napi);

	return err;
restore:
	lan966x->rx.dma = rx_dma;
	lan966x->rx.dcbs = rx_dcbs;
	lan966x_fdma_rx_start(&lan966x->rx);

restore_tx:
	lan966x->tx.dma = tx_dma;
	lan966x->tx.dcbs = tx_dcbs;
	lan966x->tx.dcbs_buf = tx_dcbs_buf;

	return err;
}

int lan966x_fdma_change_mtu(struct lan966x *lan966x)
{
	int max_mtu;
	int err;
	u32 val;

	max_mtu = lan966x_fdma_get_max_mtu(lan966x);
	max_mtu += IFH_LEN * sizeof(u32);

	if (round_up(max_mtu, PAGE_SIZE) / PAGE_SIZE - 1 ==
	    lan966x->rx.page_order)
		return 0;

	/* Disable the CPU port */
	lan_rmw(QSYS_SW_PORT_MODE_PORT_ENA_SET(0),
		QSYS_SW_PORT_MODE_PORT_ENA,
		lan966x, QSYS_SW_PORT_MODE(CPU_PORT));

	/* Flush the CPU queues */
	readx_poll_timeout(lan966x_qsys_sw_status, lan966x,
			   val, !(QSYS_SW_STATUS_EQ_AVAIL_GET(val)),
			   READL_SLEEP_US, READL_TIMEOUT_US);

	/* Add a sleep in case there are frames between the queues and the CPU
	 * port
	 */
	usleep_range(1000, 2000);

	err = lan966x_fdma_reload(lan966x, max_mtu);

	/* Enable back the CPU port */
	lan_rmw(QSYS_SW_PORT_MODE_PORT_ENA_SET(1),
		QSYS_SW_PORT_MODE_PORT_ENA,
		lan966x,  QSYS_SW_PORT_MODE(CPU_PORT));

	return err;
}

void lan966x_fdma_netdev_init(struct lan966x *lan966x, struct net_device *dev)
{
	if (lan966x->fdma_ndev)
		return;

	lan966x->fdma_ndev = dev;
	netif_napi_add(dev, &lan966x->napi, lan966x_fdma_napi_poll,
		       NAPI_POLL_WEIGHT);
	napi_enable(&lan966x->napi);
}

void lan966x_fdma_netdev_deinit(struct lan966x *lan966x, struct net_device *dev)
{
	if (lan966x->fdma_ndev == dev) {
		netif_napi_del(&lan966x->napi);
		lan966x->fdma_ndev = NULL;
	}
}

int lan966x_fdma_init(struct lan966x *lan966x)
{
	int err;

	if (!lan966x->fdma)
		return 0;

	lan966x->rx.lan966x = lan966x;
	lan966x->rx.channel_id = FDMA_XTR_CHANNEL;
	lan966x->tx.lan966x = lan966x;
	lan966x->tx.channel_id = FDMA_INJ_CHANNEL;
	lan966x->tx.last_in_use = -1;

	err = lan966x_fdma_rx_alloc(&lan966x->rx);
	if (err)
		return err;

	err = lan966x_fdma_tx_alloc(&lan966x->tx);
	if (err) {
		lan966x_fdma_rx_free(&lan966x->rx);
		return err;
	}

	lan966x_fdma_rx_start(&lan966x->rx);

	return 0;
}

void lan966x_fdma_deinit(struct lan966x *lan966x)
{
	if (!lan966x->fdma)
		return;

	lan966x_fdma_rx_disable(&lan966x->rx);
	lan966x_fdma_tx_disable(&lan966x->tx);

	napi_synchronize(&lan966x->napi);
	napi_disable(&lan966x->napi);

	lan966x_fdma_rx_free_pages(&lan966x->rx);
	lan966x_fdma_rx_free(&lan966x->rx);
	lan966x_fdma_tx_free(&lan966x->tx);
}

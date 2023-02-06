// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

/* TSN endpoint Ethernet MAC driver
 *
 * The TSN endpoint Ethernet MAC is a FPGA based network device for real-time
 * communication. It is designed for endpoints within TSN (Time Sensitive
 * Networking) networks; e.g., for PLCs in the industrial automation case.
 *
 * It supports multiple TX/RX queue pairs. The first TX/RX queue pair is used
 * by the driver.
 *
 * More information can be found here:
 * - www.embedded-experts.at/tsn
 * - www.engleder-embedded.com
 */

#include "tsnep.h"
#include "tsnep_hw.h"

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>
#include <linux/iopoll.h>

#define TSNEP_SKB_PAD (NET_SKB_PAD + NET_IP_ALIGN)
#define TSNEP_HEADROOM ALIGN(TSNEP_SKB_PAD, 4)
#define TSNEP_MAX_RX_BUF_SIZE (PAGE_SIZE - TSNEP_HEADROOM - \
			       SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#define DMA_ADDR_HIGH(dma_addr) ((u32)(((dma_addr) >> 32) & 0xFFFFFFFF))
#else
#define DMA_ADDR_HIGH(dma_addr) ((u32)(0))
#endif
#define DMA_ADDR_LOW(dma_addr) ((u32)((dma_addr) & 0xFFFFFFFF))

#define TSNEP_COALESCE_USECS_DEFAULT 64
#define TSNEP_COALESCE_USECS_MAX     ((ECM_INT_DELAY_MASK >> ECM_INT_DELAY_SHIFT) * \
				      ECM_INT_DELAY_BASE_US + ECM_INT_DELAY_BASE_US - 1)

static void tsnep_enable_irq(struct tsnep_adapter *adapter, u32 mask)
{
	iowrite32(mask, adapter->addr + ECM_INT_ENABLE);
}

static void tsnep_disable_irq(struct tsnep_adapter *adapter, u32 mask)
{
	mask |= ECM_INT_DISABLE;
	iowrite32(mask, adapter->addr + ECM_INT_ENABLE);
}

static irqreturn_t tsnep_irq(int irq, void *arg)
{
	struct tsnep_adapter *adapter = arg;
	u32 active = ioread32(adapter->addr + ECM_INT_ACTIVE);

	/* acknowledge interrupt */
	if (active != 0)
		iowrite32(active, adapter->addr + ECM_INT_ACKNOWLEDGE);

	/* handle link interrupt */
	if ((active & ECM_INT_LINK) != 0)
		phy_mac_interrupt(adapter->netdev->phydev);

	/* handle TX/RX queue 0 interrupt */
	if ((active & adapter->queue[0].irq_mask) != 0) {
		tsnep_disable_irq(adapter, adapter->queue[0].irq_mask);
		napi_schedule(&adapter->queue[0].napi);
	}

	return IRQ_HANDLED;
}

static irqreturn_t tsnep_irq_txrx(int irq, void *arg)
{
	struct tsnep_queue *queue = arg;

	/* handle TX/RX queue interrupt */
	tsnep_disable_irq(queue->adapter, queue->irq_mask);
	napi_schedule(&queue->napi);

	return IRQ_HANDLED;
}

int tsnep_set_irq_coalesce(struct tsnep_queue *queue, u32 usecs)
{
	if (usecs > TSNEP_COALESCE_USECS_MAX)
		return -ERANGE;

	usecs /= ECM_INT_DELAY_BASE_US;
	usecs <<= ECM_INT_DELAY_SHIFT;
	usecs &= ECM_INT_DELAY_MASK;

	queue->irq_delay &= ~ECM_INT_DELAY_MASK;
	queue->irq_delay |= usecs;
	iowrite8(queue->irq_delay, queue->irq_delay_addr);

	return 0;
}

u32 tsnep_get_irq_coalesce(struct tsnep_queue *queue)
{
	u32 usecs;

	usecs = (queue->irq_delay & ECM_INT_DELAY_MASK);
	usecs >>= ECM_INT_DELAY_SHIFT;
	usecs *= ECM_INT_DELAY_BASE_US;

	return usecs;
}

static int tsnep_mdiobus_read(struct mii_bus *bus, int addr, int regnum)
{
	struct tsnep_adapter *adapter = bus->priv;
	u32 md;
	int retval;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	md = ECM_MD_READ;
	if (!adapter->suppress_preamble)
		md |= ECM_MD_PREAMBLE;
	md |= (regnum << ECM_MD_ADDR_SHIFT) & ECM_MD_ADDR_MASK;
	md |= (addr << ECM_MD_PHY_ADDR_SHIFT) & ECM_MD_PHY_ADDR_MASK;
	iowrite32(md, adapter->addr + ECM_MD_CONTROL);
	retval = readl_poll_timeout_atomic(adapter->addr + ECM_MD_STATUS, md,
					   !(md & ECM_MD_BUSY), 16, 1000);
	if (retval != 0)
		return retval;

	return (md & ECM_MD_DATA_MASK) >> ECM_MD_DATA_SHIFT;
}

static int tsnep_mdiobus_write(struct mii_bus *bus, int addr, int regnum,
			       u16 val)
{
	struct tsnep_adapter *adapter = bus->priv;
	u32 md;
	int retval;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	md = ECM_MD_WRITE;
	if (!adapter->suppress_preamble)
		md |= ECM_MD_PREAMBLE;
	md |= (regnum << ECM_MD_ADDR_SHIFT) & ECM_MD_ADDR_MASK;
	md |= (addr << ECM_MD_PHY_ADDR_SHIFT) & ECM_MD_PHY_ADDR_MASK;
	md |= ((u32)val << ECM_MD_DATA_SHIFT) & ECM_MD_DATA_MASK;
	iowrite32(md, adapter->addr + ECM_MD_CONTROL);
	retval = readl_poll_timeout_atomic(adapter->addr + ECM_MD_STATUS, md,
					   !(md & ECM_MD_BUSY), 16, 1000);
	if (retval != 0)
		return retval;

	return 0;
}

static void tsnep_set_link_mode(struct tsnep_adapter *adapter)
{
	u32 mode;

	switch (adapter->phydev->speed) {
	case SPEED_100:
		mode = ECM_LINK_MODE_100;
		break;
	case SPEED_1000:
		mode = ECM_LINK_MODE_1000;
		break;
	default:
		mode = ECM_LINK_MODE_OFF;
		break;
	}
	iowrite32(mode, adapter->addr + ECM_STATUS);
}

static void tsnep_phy_link_status_change(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;

	if (phydev->link)
		tsnep_set_link_mode(adapter);

	phy_print_status(netdev->phydev);
}

static int tsnep_phy_loopback(struct tsnep_adapter *adapter, bool enable)
{
	int retval;

	retval = phy_loopback(adapter->phydev, enable);

	/* PHY link state change is not signaled if loopback is enabled, it
	 * would delay a working loopback anyway, let's ensure that loopback
	 * is working immediately by setting link mode directly
	 */
	if (!retval && enable)
		tsnep_set_link_mode(adapter);

	return retval;
}

static int tsnep_phy_open(struct tsnep_adapter *adapter)
{
	struct phy_device *phydev;
	struct ethtool_eee ethtool_eee;
	int retval;

	retval = phy_connect_direct(adapter->netdev, adapter->phydev,
				    tsnep_phy_link_status_change,
				    adapter->phy_mode);
	if (retval)
		return retval;
	phydev = adapter->netdev->phydev;

	/* MAC supports only 100Mbps|1000Mbps full duplex
	 * SPE (Single Pair Ethernet) is also an option but not implemented yet
	 */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	/* disable EEE autoneg, EEE not supported by TSNEP */
	memset(&ethtool_eee, 0, sizeof(ethtool_eee));
	phy_ethtool_set_eee(adapter->phydev, &ethtool_eee);

	adapter->phydev->irq = PHY_MAC_INTERRUPT;
	phy_start(adapter->phydev);

	return 0;
}

static void tsnep_phy_close(struct tsnep_adapter *adapter)
{
	phy_stop(adapter->netdev->phydev);
	phy_disconnect(adapter->netdev->phydev);
	adapter->netdev->phydev = NULL;
}

static void tsnep_tx_ring_cleanup(struct tsnep_tx *tx)
{
	struct device *dmadev = tx->adapter->dmadev;
	int i;

	memset(tx->entry, 0, sizeof(tx->entry));

	for (i = 0; i < TSNEP_RING_PAGE_COUNT; i++) {
		if (tx->page[i]) {
			dma_free_coherent(dmadev, PAGE_SIZE, tx->page[i],
					  tx->page_dma[i]);
			tx->page[i] = NULL;
			tx->page_dma[i] = 0;
		}
	}
}

static int tsnep_tx_ring_init(struct tsnep_tx *tx)
{
	struct device *dmadev = tx->adapter->dmadev;
	struct tsnep_tx_entry *entry;
	struct tsnep_tx_entry *next_entry;
	int i, j;
	int retval;

	for (i = 0; i < TSNEP_RING_PAGE_COUNT; i++) {
		tx->page[i] =
			dma_alloc_coherent(dmadev, PAGE_SIZE, &tx->page_dma[i],
					   GFP_KERNEL);
		if (!tx->page[i]) {
			retval = -ENOMEM;
			goto alloc_failed;
		}
		for (j = 0; j < TSNEP_RING_ENTRIES_PER_PAGE; j++) {
			entry = &tx->entry[TSNEP_RING_ENTRIES_PER_PAGE * i + j];
			entry->desc_wb = (struct tsnep_tx_desc_wb *)
				(((u8 *)tx->page[i]) + TSNEP_DESC_SIZE * j);
			entry->desc = (struct tsnep_tx_desc *)
				(((u8 *)entry->desc_wb) + TSNEP_DESC_OFFSET);
			entry->desc_dma = tx->page_dma[i] + TSNEP_DESC_SIZE * j;
		}
	}
	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		entry = &tx->entry[i];
		next_entry = &tx->entry[(i + 1) % TSNEP_RING_SIZE];
		entry->desc->next = __cpu_to_le64(next_entry->desc_dma);
	}

	return 0;

alloc_failed:
	tsnep_tx_ring_cleanup(tx);
	return retval;
}

static void tsnep_tx_activate(struct tsnep_tx *tx, int index, int length,
			      bool last)
{
	struct tsnep_tx_entry *entry = &tx->entry[index];

	entry->properties = 0;
	if (entry->skb) {
		entry->properties = length & TSNEP_DESC_LENGTH_MASK;
		entry->properties |= TSNEP_DESC_INTERRUPT_FLAG;
		if (skb_shinfo(entry->skb)->tx_flags & SKBTX_IN_PROGRESS)
			entry->properties |= TSNEP_DESC_EXTENDED_WRITEBACK_FLAG;

		/* toggle user flag to prevent false acknowledge
		 *
		 * Only the first fragment is acknowledged. For all other
		 * fragments no acknowledge is done and the last written owner
		 * counter stays in the writeback descriptor. Therefore, it is
		 * possible that the last written owner counter is identical to
		 * the new incremented owner counter and a false acknowledge is
		 * detected before the real acknowledge has been done by
		 * hardware.
		 *
		 * The user flag is used to prevent this situation. The user
		 * flag is copied to the writeback descriptor by the hardware
		 * and is used as additional acknowledge data. By toggeling the
		 * user flag only for the first fragment (which is
		 * acknowledged), it is guaranteed that the last acknowledge
		 * done for this descriptor has used a different user flag and
		 * cannot be detected as false acknowledge.
		 */
		entry->owner_user_flag = !entry->owner_user_flag;
	}
	if (last)
		entry->properties |= TSNEP_TX_DESC_LAST_FRAGMENT_FLAG;
	if (index == tx->increment_owner_counter) {
		tx->owner_counter++;
		if (tx->owner_counter == 4)
			tx->owner_counter = 1;
		tx->increment_owner_counter--;
		if (tx->increment_owner_counter < 0)
			tx->increment_owner_counter = TSNEP_RING_SIZE - 1;
	}
	entry->properties |=
		(tx->owner_counter << TSNEP_DESC_OWNER_COUNTER_SHIFT) &
		TSNEP_DESC_OWNER_COUNTER_MASK;
	if (entry->owner_user_flag)
		entry->properties |= TSNEP_TX_DESC_OWNER_USER_FLAG;
	entry->desc->more_properties =
		__cpu_to_le32(entry->len & TSNEP_DESC_LENGTH_MASK);

	/* descriptor properties shall be written last, because valid data is
	 * signaled there
	 */
	dma_wmb();

	entry->desc->properties = __cpu_to_le32(entry->properties);
}

static int tsnep_tx_desc_available(struct tsnep_tx *tx)
{
	if (tx->read <= tx->write)
		return TSNEP_RING_SIZE - tx->write + tx->read - 1;
	else
		return tx->read - tx->write - 1;
}

static int tsnep_tx_map(struct sk_buff *skb, struct tsnep_tx *tx, int count)
{
	struct device *dmadev = tx->adapter->dmadev;
	struct tsnep_tx_entry *entry;
	unsigned int len;
	dma_addr_t dma;
	int map_len = 0;
	int i;

	for (i = 0; i < count; i++) {
		entry = &tx->entry[(tx->write + i) % TSNEP_RING_SIZE];

		if (i == 0) {
			len = skb_headlen(skb);
			dma = dma_map_single(dmadev, skb->data, len,
					     DMA_TO_DEVICE);
		} else {
			len = skb_frag_size(&skb_shinfo(skb)->frags[i - 1]);
			dma = skb_frag_dma_map(dmadev,
					       &skb_shinfo(skb)->frags[i - 1],
					       0, len, DMA_TO_DEVICE);
		}
		if (dma_mapping_error(dmadev, dma))
			return -ENOMEM;

		entry->len = len;
		dma_unmap_addr_set(entry, dma, dma);

		entry->desc->tx = __cpu_to_le64(dma);

		map_len += len;
	}

	return map_len;
}

static int tsnep_tx_unmap(struct tsnep_tx *tx, int index, int count)
{
	struct device *dmadev = tx->adapter->dmadev;
	struct tsnep_tx_entry *entry;
	int map_len = 0;
	int i;

	for (i = 0; i < count; i++) {
		entry = &tx->entry[(index + i) % TSNEP_RING_SIZE];

		if (entry->len) {
			if (i == 0)
				dma_unmap_single(dmadev,
						 dma_unmap_addr(entry, dma),
						 dma_unmap_len(entry, len),
						 DMA_TO_DEVICE);
			else
				dma_unmap_page(dmadev,
					       dma_unmap_addr(entry, dma),
					       dma_unmap_len(entry, len),
					       DMA_TO_DEVICE);
			map_len += entry->len;
			entry->len = 0;
		}
	}

	return map_len;
}

static netdev_tx_t tsnep_xmit_frame_ring(struct sk_buff *skb,
					 struct tsnep_tx *tx)
{
	unsigned long flags;
	int count = 1;
	struct tsnep_tx_entry *entry;
	int length;
	int i;
	int retval;

	if (skb_shinfo(skb)->nr_frags > 0)
		count += skb_shinfo(skb)->nr_frags;

	spin_lock_irqsave(&tx->lock, flags);

	if (tsnep_tx_desc_available(tx) < count) {
		/* ring full, shall not happen because queue is stopped if full
		 * below
		 */
		netif_stop_subqueue(tx->adapter->netdev, tx->queue_index);

		spin_unlock_irqrestore(&tx->lock, flags);

		return NETDEV_TX_BUSY;
	}

	entry = &tx->entry[tx->write];
	entry->skb = skb;

	retval = tsnep_tx_map(skb, tx, count);
	if (retval < 0) {
		tsnep_tx_unmap(tx, tx->write, count);
		dev_kfree_skb_any(entry->skb);
		entry->skb = NULL;

		tx->dropped++;

		spin_unlock_irqrestore(&tx->lock, flags);

		netdev_err(tx->adapter->netdev, "TX DMA map failed\n");

		return NETDEV_TX_OK;
	}
	length = retval;

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	for (i = 0; i < count; i++)
		tsnep_tx_activate(tx, (tx->write + i) % TSNEP_RING_SIZE, length,
				  i == (count - 1));
	tx->write = (tx->write + count) % TSNEP_RING_SIZE;

	skb_tx_timestamp(skb);

	/* descriptor properties shall be valid before hardware is notified */
	dma_wmb();

	iowrite32(TSNEP_CONTROL_TX_ENABLE, tx->addr + TSNEP_CONTROL);

	if (tsnep_tx_desc_available(tx) < (MAX_SKB_FRAGS + 1)) {
		/* ring can get full with next frame */
		netif_stop_subqueue(tx->adapter->netdev, tx->queue_index);
	}

	spin_unlock_irqrestore(&tx->lock, flags);

	return NETDEV_TX_OK;
}

static bool tsnep_tx_poll(struct tsnep_tx *tx, int napi_budget)
{
	struct tsnep_tx_entry *entry;
	struct netdev_queue *nq;
	unsigned long flags;
	int budget = 128;
	int length;
	int count;

	nq = netdev_get_tx_queue(tx->adapter->netdev, tx->queue_index);

	spin_lock_irqsave(&tx->lock, flags);

	do {
		if (tx->read == tx->write)
			break;

		entry = &tx->entry[tx->read];
		if ((__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_TX_DESC_OWNER_MASK) !=
		    (entry->properties & TSNEP_TX_DESC_OWNER_MASK))
			break;

		/* descriptor properties shall be read first, because valid data
		 * is signaled there
		 */
		dma_rmb();

		count = 1;
		if (skb_shinfo(entry->skb)->nr_frags > 0)
			count += skb_shinfo(entry->skb)->nr_frags;

		length = tsnep_tx_unmap(tx, tx->read, count);

		if ((skb_shinfo(entry->skb)->tx_flags & SKBTX_IN_PROGRESS) &&
		    (__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_DESC_EXTENDED_WRITEBACK_FLAG)) {
			struct skb_shared_hwtstamps hwtstamps;
			u64 timestamp;

			if (skb_shinfo(entry->skb)->tx_flags &
			    SKBTX_HW_TSTAMP_USE_CYCLES)
				timestamp =
					__le64_to_cpu(entry->desc_wb->counter);
			else
				timestamp =
					__le64_to_cpu(entry->desc_wb->timestamp);

			memset(&hwtstamps, 0, sizeof(hwtstamps));
			hwtstamps.hwtstamp = ns_to_ktime(timestamp);

			skb_tstamp_tx(entry->skb, &hwtstamps);
		}

		napi_consume_skb(entry->skb, budget);
		entry->skb = NULL;

		tx->read = (tx->read + count) % TSNEP_RING_SIZE;

		tx->packets++;
		tx->bytes += length + ETH_FCS_LEN;

		budget--;
	} while (likely(budget));

	if ((tsnep_tx_desc_available(tx) >= ((MAX_SKB_FRAGS + 1) * 2)) &&
	    netif_tx_queue_stopped(nq)) {
		netif_tx_wake_queue(nq);
	}

	spin_unlock_irqrestore(&tx->lock, flags);

	return (budget != 0);
}

static bool tsnep_tx_pending(struct tsnep_tx *tx)
{
	unsigned long flags;
	struct tsnep_tx_entry *entry;
	bool pending = false;

	spin_lock_irqsave(&tx->lock, flags);

	if (tx->read != tx->write) {
		entry = &tx->entry[tx->read];
		if ((__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_TX_DESC_OWNER_MASK) ==
		    (entry->properties & TSNEP_TX_DESC_OWNER_MASK))
			pending = true;
	}

	spin_unlock_irqrestore(&tx->lock, flags);

	return pending;
}

static int tsnep_tx_open(struct tsnep_adapter *adapter, void __iomem *addr,
			 int queue_index, struct tsnep_tx *tx)
{
	dma_addr_t dma;
	int retval;

	memset(tx, 0, sizeof(*tx));
	tx->adapter = adapter;
	tx->addr = addr;
	tx->queue_index = queue_index;

	retval = tsnep_tx_ring_init(tx);
	if (retval)
		return retval;

	dma = tx->entry[0].desc_dma | TSNEP_RESET_OWNER_COUNTER;
	iowrite32(DMA_ADDR_LOW(dma), tx->addr + TSNEP_TX_DESC_ADDR_LOW);
	iowrite32(DMA_ADDR_HIGH(dma), tx->addr + TSNEP_TX_DESC_ADDR_HIGH);
	tx->owner_counter = 1;
	tx->increment_owner_counter = TSNEP_RING_SIZE - 1;

	spin_lock_init(&tx->lock);

	return 0;
}

static void tsnep_tx_close(struct tsnep_tx *tx)
{
	u32 val;

	readx_poll_timeout(ioread32, tx->addr + TSNEP_CONTROL, val,
			   ((val & TSNEP_CONTROL_TX_ENABLE) == 0), 10000,
			   1000000);

	tsnep_tx_ring_cleanup(tx);
}

static void tsnep_rx_ring_cleanup(struct tsnep_rx *rx)
{
	struct device *dmadev = rx->adapter->dmadev;
	struct tsnep_rx_entry *entry;
	int i;

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		entry = &rx->entry[i];
		if (entry->page)
			page_pool_put_full_page(rx->page_pool, entry->page,
						false);
		entry->page = NULL;
	}

	if (rx->page_pool)
		page_pool_destroy(rx->page_pool);

	memset(rx->entry, 0, sizeof(rx->entry));

	for (i = 0; i < TSNEP_RING_PAGE_COUNT; i++) {
		if (rx->page[i]) {
			dma_free_coherent(dmadev, PAGE_SIZE, rx->page[i],
					  rx->page_dma[i]);
			rx->page[i] = NULL;
			rx->page_dma[i] = 0;
		}
	}
}

static int tsnep_rx_ring_init(struct tsnep_rx *rx)
{
	struct device *dmadev = rx->adapter->dmadev;
	struct tsnep_rx_entry *entry;
	struct page_pool_params pp_params = { 0 };
	struct tsnep_rx_entry *next_entry;
	int i, j;
	int retval;

	for (i = 0; i < TSNEP_RING_PAGE_COUNT; i++) {
		rx->page[i] =
			dma_alloc_coherent(dmadev, PAGE_SIZE, &rx->page_dma[i],
					   GFP_KERNEL);
		if (!rx->page[i]) {
			retval = -ENOMEM;
			goto failed;
		}
		for (j = 0; j < TSNEP_RING_ENTRIES_PER_PAGE; j++) {
			entry = &rx->entry[TSNEP_RING_ENTRIES_PER_PAGE * i + j];
			entry->desc_wb = (struct tsnep_rx_desc_wb *)
				(((u8 *)rx->page[i]) + TSNEP_DESC_SIZE * j);
			entry->desc = (struct tsnep_rx_desc *)
				(((u8 *)entry->desc_wb) + TSNEP_DESC_OFFSET);
			entry->desc_dma = rx->page_dma[i] + TSNEP_DESC_SIZE * j;
		}
	}

	pp_params.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;
	pp_params.order = 0;
	pp_params.pool_size = TSNEP_RING_SIZE;
	pp_params.nid = dev_to_node(dmadev);
	pp_params.dev = dmadev;
	pp_params.dma_dir = DMA_FROM_DEVICE;
	pp_params.max_len = TSNEP_MAX_RX_BUF_SIZE;
	pp_params.offset = TSNEP_SKB_PAD;
	rx->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rx->page_pool)) {
		retval = PTR_ERR(rx->page_pool);
		rx->page_pool = NULL;
		goto failed;
	}

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		entry = &rx->entry[i];
		next_entry = &rx->entry[(i + 1) % TSNEP_RING_SIZE];
		entry->desc->next = __cpu_to_le64(next_entry->desc_dma);
	}

	return 0;

failed:
	tsnep_rx_ring_cleanup(rx);
	return retval;
}

static int tsnep_rx_desc_available(struct tsnep_rx *rx)
{
	if (rx->read <= rx->write)
		return TSNEP_RING_SIZE - rx->write + rx->read - 1;
	else
		return rx->read - rx->write - 1;
}

static void tsnep_rx_set_page(struct tsnep_rx *rx, struct tsnep_rx_entry *entry,
			      struct page *page)
{
	entry->page = page;
	entry->len = TSNEP_MAX_RX_BUF_SIZE;
	entry->dma = page_pool_get_dma_addr(entry->page);
	entry->desc->rx = __cpu_to_le64(entry->dma + TSNEP_SKB_PAD);
}

static int tsnep_rx_alloc_buffer(struct tsnep_rx *rx, int index)
{
	struct tsnep_rx_entry *entry = &rx->entry[index];
	struct page *page;

	page = page_pool_dev_alloc_pages(rx->page_pool);
	if (unlikely(!page))
		return -ENOMEM;
	tsnep_rx_set_page(rx, entry, page);

	return 0;
}

static void tsnep_rx_reuse_buffer(struct tsnep_rx *rx, int index)
{
	struct tsnep_rx_entry *entry = &rx->entry[index];
	struct tsnep_rx_entry *read = &rx->entry[rx->read];

	tsnep_rx_set_page(rx, entry, read->page);
	read->page = NULL;
}

static void tsnep_rx_activate(struct tsnep_rx *rx, int index)
{
	struct tsnep_rx_entry *entry = &rx->entry[index];

	/* TSNEP_MAX_RX_BUF_SIZE is a multiple of 4 */
	entry->properties = entry->len & TSNEP_DESC_LENGTH_MASK;
	entry->properties |= TSNEP_DESC_INTERRUPT_FLAG;
	if (index == rx->increment_owner_counter) {
		rx->owner_counter++;
		if (rx->owner_counter == 4)
			rx->owner_counter = 1;
		rx->increment_owner_counter--;
		if (rx->increment_owner_counter < 0)
			rx->increment_owner_counter = TSNEP_RING_SIZE - 1;
	}
	entry->properties |=
		(rx->owner_counter << TSNEP_DESC_OWNER_COUNTER_SHIFT) &
		TSNEP_DESC_OWNER_COUNTER_MASK;

	/* descriptor properties shall be written last, because valid data is
	 * signaled there
	 */
	dma_wmb();

	entry->desc->properties = __cpu_to_le32(entry->properties);
}

static int tsnep_rx_refill(struct tsnep_rx *rx, int count, bool reuse)
{
	int index;
	bool alloc_failed = false;
	bool enable = false;
	int i;
	int retval;

	for (i = 0; i < count && !alloc_failed; i++) {
		index = (rx->write + i) % TSNEP_RING_SIZE;

		retval = tsnep_rx_alloc_buffer(rx, index);
		if (unlikely(retval)) {
			rx->alloc_failed++;
			alloc_failed = true;

			/* reuse only if no other allocation was successful */
			if (i == 0 && reuse)
				tsnep_rx_reuse_buffer(rx, index);
			else
				break;
		}

		tsnep_rx_activate(rx, index);

		enable = true;
	}

	if (enable) {
		rx->write = (rx->write + i) % TSNEP_RING_SIZE;

		/* descriptor properties shall be valid before hardware is
		 * notified
		 */
		dma_wmb();

		iowrite32(TSNEP_CONTROL_RX_ENABLE, rx->addr + TSNEP_CONTROL);
	}

	return i;
}

static struct sk_buff *tsnep_build_skb(struct tsnep_rx *rx, struct page *page,
				       int length)
{
	struct sk_buff *skb;

	skb = napi_build_skb(page_address(page), PAGE_SIZE);
	if (unlikely(!skb))
		return NULL;

	/* update pointers within the skb to store the data */
	skb_reserve(skb, TSNEP_SKB_PAD + TSNEP_RX_INLINE_METADATA_SIZE);
	__skb_put(skb, length - TSNEP_RX_INLINE_METADATA_SIZE - ETH_FCS_LEN);

	if (rx->adapter->hwtstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) {
		struct skb_shared_hwtstamps *hwtstamps = skb_hwtstamps(skb);
		struct tsnep_rx_inline *rx_inline =
			(struct tsnep_rx_inline *)(page_address(page) +
						   TSNEP_SKB_PAD);

		skb_shinfo(skb)->tx_flags |=
			SKBTX_HW_TSTAMP_NETDEV;
		memset(hwtstamps, 0, sizeof(*hwtstamps));
		hwtstamps->netdev_data = rx_inline;
	}

	skb_record_rx_queue(skb, rx->queue_index);
	skb->protocol = eth_type_trans(skb, rx->adapter->netdev);

	return skb;
}

static int tsnep_rx_poll(struct tsnep_rx *rx, struct napi_struct *napi,
			 int budget)
{
	struct device *dmadev = rx->adapter->dmadev;
	int desc_available;
	int done = 0;
	enum dma_data_direction dma_dir;
	struct tsnep_rx_entry *entry;
	struct sk_buff *skb;
	int length;

	desc_available = tsnep_rx_desc_available(rx);
	dma_dir = page_pool_get_dma_dir(rx->page_pool);

	while (likely(done < budget) && (rx->read != rx->write)) {
		entry = &rx->entry[rx->read];
		if ((__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_DESC_OWNER_COUNTER_MASK) !=
		    (entry->properties & TSNEP_DESC_OWNER_COUNTER_MASK))
			break;
		done++;

		if (desc_available >= TSNEP_RING_RX_REFILL) {
			bool reuse = desc_available >= TSNEP_RING_RX_REUSE;

			desc_available -= tsnep_rx_refill(rx, desc_available,
							  reuse);
			if (!entry->page) {
				/* buffer has been reused for refill to prevent
				 * empty RX ring, thus buffer cannot be used for
				 * RX processing
				 */
				rx->read = (rx->read + 1) % TSNEP_RING_SIZE;
				desc_available++;

				rx->dropped++;

				continue;
			}
		}

		/* descriptor properties shall be read first, because valid data
		 * is signaled there
		 */
		dma_rmb();

		prefetch(page_address(entry->page) + TSNEP_SKB_PAD);
		length = __le32_to_cpu(entry->desc_wb->properties) &
			 TSNEP_DESC_LENGTH_MASK;
		dma_sync_single_range_for_cpu(dmadev, entry->dma, TSNEP_SKB_PAD,
					      length, dma_dir);

		rx->read = (rx->read + 1) % TSNEP_RING_SIZE;
		desc_available++;

		skb = tsnep_build_skb(rx, entry->page, length);
		if (skb) {
			page_pool_release_page(rx->page_pool, entry->page);

			rx->packets++;
			rx->bytes += length - TSNEP_RX_INLINE_METADATA_SIZE;
			if (skb->pkt_type == PACKET_MULTICAST)
				rx->multicast++;

			napi_gro_receive(napi, skb);
		} else {
			page_pool_recycle_direct(rx->page_pool, entry->page);

			rx->dropped++;
		}
		entry->page = NULL;
	}

	if (desc_available)
		tsnep_rx_refill(rx, desc_available, false);

	return done;
}

static bool tsnep_rx_pending(struct tsnep_rx *rx)
{
	struct tsnep_rx_entry *entry;

	if (rx->read != rx->write) {
		entry = &rx->entry[rx->read];
		if ((__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_DESC_OWNER_COUNTER_MASK) ==
		    (entry->properties & TSNEP_DESC_OWNER_COUNTER_MASK))
			return true;
	}

	return false;
}

static int tsnep_rx_open(struct tsnep_adapter *adapter, void __iomem *addr,
			 int queue_index, struct tsnep_rx *rx)
{
	dma_addr_t dma;
	int retval;

	memset(rx, 0, sizeof(*rx));
	rx->adapter = adapter;
	rx->addr = addr;
	rx->queue_index = queue_index;

	retval = tsnep_rx_ring_init(rx);
	if (retval)
		return retval;

	dma = rx->entry[0].desc_dma | TSNEP_RESET_OWNER_COUNTER;
	iowrite32(DMA_ADDR_LOW(dma), rx->addr + TSNEP_RX_DESC_ADDR_LOW);
	iowrite32(DMA_ADDR_HIGH(dma), rx->addr + TSNEP_RX_DESC_ADDR_HIGH);
	rx->owner_counter = 1;
	rx->increment_owner_counter = TSNEP_RING_SIZE - 1;

	tsnep_rx_refill(rx, tsnep_rx_desc_available(rx), false);

	return 0;
}

static void tsnep_rx_close(struct tsnep_rx *rx)
{
	u32 val;

	iowrite32(TSNEP_CONTROL_RX_DISABLE, rx->addr + TSNEP_CONTROL);
	readx_poll_timeout(ioread32, rx->addr + TSNEP_CONTROL, val,
			   ((val & TSNEP_CONTROL_RX_ENABLE) == 0), 10000,
			   1000000);

	tsnep_rx_ring_cleanup(rx);
}

static bool tsnep_pending(struct tsnep_queue *queue)
{
	if (queue->tx && tsnep_tx_pending(queue->tx))
		return true;

	if (queue->rx && tsnep_rx_pending(queue->rx))
		return true;

	return false;
}

static int tsnep_poll(struct napi_struct *napi, int budget)
{
	struct tsnep_queue *queue = container_of(napi, struct tsnep_queue,
						 napi);
	bool complete = true;
	int done = 0;

	if (queue->tx)
		complete = tsnep_tx_poll(queue->tx, budget);

	if (queue->rx) {
		done = tsnep_rx_poll(queue->rx, napi, budget);
		if (done >= budget)
			complete = false;
	}

	/* if all work not completed, return budget and keep polling */
	if (!complete)
		return budget;

	if (likely(napi_complete_done(napi, done))) {
		tsnep_enable_irq(queue->adapter, queue->irq_mask);

		/* reschedule if work is already pending, prevent rotten packets
		 * which are transmitted or received after polling but before
		 * interrupt enable
		 */
		if (tsnep_pending(queue)) {
			tsnep_disable_irq(queue->adapter, queue->irq_mask);
			napi_schedule(napi);
		}
	}

	return min(done, budget - 1);
}

static int tsnep_request_irq(struct tsnep_queue *queue, bool first)
{
	const char *name = netdev_name(queue->adapter->netdev);
	irq_handler_t handler;
	void *dev;
	int retval;

	if (first) {
		sprintf(queue->name, "%s-mac", name);
		handler = tsnep_irq;
		dev = queue->adapter;
	} else {
		if (queue->tx && queue->rx)
			sprintf(queue->name, "%s-txrx-%d", name,
				queue->rx->queue_index);
		else if (queue->tx)
			sprintf(queue->name, "%s-tx-%d", name,
				queue->tx->queue_index);
		else
			sprintf(queue->name, "%s-rx-%d", name,
				queue->rx->queue_index);
		handler = tsnep_irq_txrx;
		dev = queue;
	}

	retval = request_irq(queue->irq, handler, 0, queue->name, dev);
	if (retval) {
		/* if name is empty, then interrupt won't be freed */
		memset(queue->name, 0, sizeof(queue->name));
	}

	return retval;
}

static void tsnep_free_irq(struct tsnep_queue *queue, bool first)
{
	void *dev;

	if (!strlen(queue->name))
		return;

	if (first)
		dev = queue->adapter;
	else
		dev = queue;

	free_irq(queue->irq, dev);
	memset(queue->name, 0, sizeof(queue->name));
}

static int tsnep_netdev_open(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int i;
	void __iomem *addr;
	int tx_queue_index = 0;
	int rx_queue_index = 0;
	int retval;

	for (i = 0; i < adapter->num_queues; i++) {
		adapter->queue[i].adapter = adapter;
		if (adapter->queue[i].tx) {
			addr = adapter->addr + TSNEP_QUEUE(tx_queue_index);
			retval = tsnep_tx_open(adapter, addr, tx_queue_index,
					       adapter->queue[i].tx);
			if (retval)
				goto failed;
			tx_queue_index++;
		}
		if (adapter->queue[i].rx) {
			addr = adapter->addr + TSNEP_QUEUE(rx_queue_index);
			retval = tsnep_rx_open(adapter, addr,
					       rx_queue_index,
					       adapter->queue[i].rx);
			if (retval)
				goto failed;
			rx_queue_index++;
		}

		retval = tsnep_request_irq(&adapter->queue[i], i == 0);
		if (retval) {
			netif_err(adapter, drv, adapter->netdev,
				  "can't get assigned irq %d.\n",
				  adapter->queue[i].irq);
			goto failed;
		}
	}

	retval = netif_set_real_num_tx_queues(adapter->netdev,
					      adapter->num_tx_queues);
	if (retval)
		goto failed;
	retval = netif_set_real_num_rx_queues(adapter->netdev,
					      adapter->num_rx_queues);
	if (retval)
		goto failed;

	tsnep_enable_irq(adapter, ECM_INT_LINK);
	retval = tsnep_phy_open(adapter);
	if (retval)
		goto phy_failed;

	for (i = 0; i < adapter->num_queues; i++) {
		netif_napi_add(adapter->netdev, &adapter->queue[i].napi,
			       tsnep_poll);
		napi_enable(&adapter->queue[i].napi);

		tsnep_enable_irq(adapter, adapter->queue[i].irq_mask);
	}

	return 0;

phy_failed:
	tsnep_disable_irq(adapter, ECM_INT_LINK);
	tsnep_phy_close(adapter);
failed:
	for (i = 0; i < adapter->num_queues; i++) {
		tsnep_free_irq(&adapter->queue[i], i == 0);

		if (adapter->queue[i].rx)
			tsnep_rx_close(adapter->queue[i].rx);
		if (adapter->queue[i].tx)
			tsnep_tx_close(adapter->queue[i].tx);
	}
	return retval;
}

static int tsnep_netdev_close(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int i;

	tsnep_disable_irq(adapter, ECM_INT_LINK);
	tsnep_phy_close(adapter);

	for (i = 0; i < adapter->num_queues; i++) {
		tsnep_disable_irq(adapter, adapter->queue[i].irq_mask);

		napi_disable(&adapter->queue[i].napi);
		netif_napi_del(&adapter->queue[i].napi);

		tsnep_free_irq(&adapter->queue[i], i == 0);

		if (adapter->queue[i].rx)
			tsnep_rx_close(adapter->queue[i].rx);
		if (adapter->queue[i].tx)
			tsnep_tx_close(adapter->queue[i].tx);
	}

	return 0;
}

static netdev_tx_t tsnep_netdev_xmit_frame(struct sk_buff *skb,
					   struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	u16 queue_mapping = skb_get_queue_mapping(skb);

	if (queue_mapping >= adapter->num_tx_queues)
		queue_mapping = 0;

	return tsnep_xmit_frame_ring(skb, &adapter->tx[queue_mapping]);
}

static int tsnep_netdev_ioctl(struct net_device *netdev, struct ifreq *ifr,
			      int cmd)
{
	if (!netif_running(netdev))
		return -EINVAL;
	if (cmd == SIOCSHWTSTAMP || cmd == SIOCGHWTSTAMP)
		return tsnep_ptp_ioctl(netdev, ifr, cmd);
	return phy_mii_ioctl(netdev->phydev, ifr, cmd);
}

static void tsnep_netdev_set_multicast(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	u16 rx_filter = 0;

	/* configured MAC address and broadcasts are never filtered */
	if (netdev->flags & IFF_PROMISC) {
		rx_filter |= TSNEP_RX_FILTER_ACCEPT_ALL_MULTICASTS;
		rx_filter |= TSNEP_RX_FILTER_ACCEPT_ALL_UNICASTS;
	} else if (!netdev_mc_empty(netdev) || (netdev->flags & IFF_ALLMULTI)) {
		rx_filter |= TSNEP_RX_FILTER_ACCEPT_ALL_MULTICASTS;
	}
	iowrite16(rx_filter, adapter->addr + TSNEP_RX_FILTER);
}

static void tsnep_netdev_get_stats64(struct net_device *netdev,
				     struct rtnl_link_stats64 *stats)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	u32 reg;
	u32 val;
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		stats->tx_packets += adapter->tx[i].packets;
		stats->tx_bytes += adapter->tx[i].bytes;
		stats->tx_dropped += adapter->tx[i].dropped;
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		stats->rx_packets += adapter->rx[i].packets;
		stats->rx_bytes += adapter->rx[i].bytes;
		stats->rx_dropped += adapter->rx[i].dropped;
		stats->multicast += adapter->rx[i].multicast;

		reg = ioread32(adapter->addr + TSNEP_QUEUE(i) +
			       TSNEP_RX_STATISTIC);
		val = (reg & TSNEP_RX_STATISTIC_NO_DESC_MASK) >>
		      TSNEP_RX_STATISTIC_NO_DESC_SHIFT;
		stats->rx_dropped += val;
		val = (reg & TSNEP_RX_STATISTIC_BUFFER_TOO_SMALL_MASK) >>
		      TSNEP_RX_STATISTIC_BUFFER_TOO_SMALL_SHIFT;
		stats->rx_dropped += val;
		val = (reg & TSNEP_RX_STATISTIC_FIFO_OVERFLOW_MASK) >>
		      TSNEP_RX_STATISTIC_FIFO_OVERFLOW_SHIFT;
		stats->rx_errors += val;
		stats->rx_fifo_errors += val;
		val = (reg & TSNEP_RX_STATISTIC_INVALID_FRAME_MASK) >>
		      TSNEP_RX_STATISTIC_INVALID_FRAME_SHIFT;
		stats->rx_errors += val;
		stats->rx_frame_errors += val;
	}

	reg = ioread32(adapter->addr + ECM_STAT);
	val = (reg & ECM_STAT_RX_ERR_MASK) >> ECM_STAT_RX_ERR_SHIFT;
	stats->rx_errors += val;
	val = (reg & ECM_STAT_INV_FRM_MASK) >> ECM_STAT_INV_FRM_SHIFT;
	stats->rx_errors += val;
	stats->rx_crc_errors += val;
	val = (reg & ECM_STAT_FWD_RX_ERR_MASK) >> ECM_STAT_FWD_RX_ERR_SHIFT;
	stats->rx_errors += val;
}

static void tsnep_mac_set_address(struct tsnep_adapter *adapter, u8 *addr)
{
	iowrite32(*(u32 *)addr, adapter->addr + TSNEP_MAC_ADDRESS_LOW);
	iowrite16(*(u16 *)(addr + sizeof(u32)),
		  adapter->addr + TSNEP_MAC_ADDRESS_HIGH);

	ether_addr_copy(adapter->mac_address, addr);
	netif_info(adapter, drv, adapter->netdev, "MAC address set to %pM\n",
		   addr);
}

static int tsnep_netdev_set_mac_address(struct net_device *netdev, void *addr)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *sock_addr = addr;
	int retval;

	retval = eth_prepare_mac_addr_change(netdev, sock_addr);
	if (retval)
		return retval;
	eth_hw_addr_set(netdev, sock_addr->sa_data);
	tsnep_mac_set_address(adapter, sock_addr->sa_data);

	return 0;
}

static int tsnep_netdev_set_features(struct net_device *netdev,
				     netdev_features_t features)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	netdev_features_t changed = netdev->features ^ features;
	bool enable;
	int retval = 0;

	if (changed & NETIF_F_LOOPBACK) {
		enable = !!(features & NETIF_F_LOOPBACK);
		retval = tsnep_phy_loopback(adapter, enable);
	}

	return retval;
}

static ktime_t tsnep_netdev_get_tstamp(struct net_device *netdev,
				       const struct skb_shared_hwtstamps *hwtstamps,
				       bool cycles)
{
	struct tsnep_rx_inline *rx_inline = hwtstamps->netdev_data;
	u64 timestamp;

	if (cycles)
		timestamp = __le64_to_cpu(rx_inline->counter);
	else
		timestamp = __le64_to_cpu(rx_inline->timestamp);

	return ns_to_ktime(timestamp);
}

static const struct net_device_ops tsnep_netdev_ops = {
	.ndo_open = tsnep_netdev_open,
	.ndo_stop = tsnep_netdev_close,
	.ndo_start_xmit = tsnep_netdev_xmit_frame,
	.ndo_eth_ioctl = tsnep_netdev_ioctl,
	.ndo_set_rx_mode = tsnep_netdev_set_multicast,
	.ndo_get_stats64 = tsnep_netdev_get_stats64,
	.ndo_set_mac_address = tsnep_netdev_set_mac_address,
	.ndo_set_features = tsnep_netdev_set_features,
	.ndo_get_tstamp = tsnep_netdev_get_tstamp,
	.ndo_setup_tc = tsnep_tc_setup,
};

static int tsnep_mac_init(struct tsnep_adapter *adapter)
{
	int retval;

	/* initialize RX filtering, at least configured MAC address and
	 * broadcast are not filtered
	 */
	iowrite16(0, adapter->addr + TSNEP_RX_FILTER);

	/* try to get MAC address in the following order:
	 * - device tree
	 * - valid MAC address already set
	 * - MAC address register if valid
	 * - random MAC address
	 */
	retval = of_get_mac_address(adapter->pdev->dev.of_node,
				    adapter->mac_address);
	if (retval == -EPROBE_DEFER)
		return retval;
	if (retval && !is_valid_ether_addr(adapter->mac_address)) {
		*(u32 *)adapter->mac_address =
			ioread32(adapter->addr + TSNEP_MAC_ADDRESS_LOW);
		*(u16 *)(adapter->mac_address + sizeof(u32)) =
			ioread16(adapter->addr + TSNEP_MAC_ADDRESS_HIGH);
		if (!is_valid_ether_addr(adapter->mac_address))
			eth_random_addr(adapter->mac_address);
	}

	tsnep_mac_set_address(adapter, adapter->mac_address);
	eth_hw_addr_set(adapter->netdev, adapter->mac_address);

	return 0;
}

static int tsnep_mdio_init(struct tsnep_adapter *adapter)
{
	struct device_node *np = adapter->pdev->dev.of_node;
	int retval;

	if (np) {
		np = of_get_child_by_name(np, "mdio");
		if (!np)
			return 0;

		adapter->suppress_preamble =
			of_property_read_bool(np, "suppress-preamble");
	}

	adapter->mdiobus = devm_mdiobus_alloc(&adapter->pdev->dev);
	if (!adapter->mdiobus) {
		retval = -ENOMEM;

		goto out;
	}

	adapter->mdiobus->priv = (void *)adapter;
	adapter->mdiobus->parent = &adapter->pdev->dev;
	adapter->mdiobus->read = tsnep_mdiobus_read;
	adapter->mdiobus->write = tsnep_mdiobus_write;
	adapter->mdiobus->name = TSNEP "-mdiobus";
	snprintf(adapter->mdiobus->id, MII_BUS_ID_SIZE, "%s",
		 adapter->pdev->name);

	/* do not scan broadcast address */
	adapter->mdiobus->phy_mask = 0x0000001;

	retval = of_mdiobus_register(adapter->mdiobus, np);

out:
	of_node_put(np);

	return retval;
}

static int tsnep_phy_init(struct tsnep_adapter *adapter)
{
	struct device_node *phy_node;
	int retval;

	retval = of_get_phy_mode(adapter->pdev->dev.of_node,
				 &adapter->phy_mode);
	if (retval)
		adapter->phy_mode = PHY_INTERFACE_MODE_GMII;

	phy_node = of_parse_phandle(adapter->pdev->dev.of_node, "phy-handle",
				    0);
	adapter->phydev = of_phy_find_device(phy_node);
	of_node_put(phy_node);
	if (!adapter->phydev && adapter->mdiobus)
		adapter->phydev = phy_find_first(adapter->mdiobus);
	if (!adapter->phydev)
		return -EIO;

	return 0;
}

static int tsnep_queue_init(struct tsnep_adapter *adapter, int queue_count)
{
	u32 irq_mask = ECM_INT_TX_0 | ECM_INT_RX_0;
	char name[8];
	int i;
	int retval;

	/* one TX/RX queue pair for netdev is mandatory */
	if (platform_irq_count(adapter->pdev) == 1)
		retval = platform_get_irq(adapter->pdev, 0);
	else
		retval = platform_get_irq_byname(adapter->pdev, "mac");
	if (retval < 0)
		return retval;
	adapter->num_tx_queues = 1;
	adapter->num_rx_queues = 1;
	adapter->num_queues = 1;
	adapter->queue[0].irq = retval;
	adapter->queue[0].tx = &adapter->tx[0];
	adapter->queue[0].rx = &adapter->rx[0];
	adapter->queue[0].irq_mask = irq_mask;
	adapter->queue[0].irq_delay_addr = adapter->addr + ECM_INT_DELAY;
	retval = tsnep_set_irq_coalesce(&adapter->queue[0],
					TSNEP_COALESCE_USECS_DEFAULT);
	if (retval < 0)
		return retval;

	adapter->netdev->irq = adapter->queue[0].irq;

	/* add additional TX/RX queue pairs only if dedicated interrupt is
	 * available
	 */
	for (i = 1; i < queue_count; i++) {
		sprintf(name, "txrx-%d", i);
		retval = platform_get_irq_byname_optional(adapter->pdev, name);
		if (retval < 0)
			break;

		adapter->num_tx_queues++;
		adapter->num_rx_queues++;
		adapter->num_queues++;
		adapter->queue[i].irq = retval;
		adapter->queue[i].tx = &adapter->tx[i];
		adapter->queue[i].rx = &adapter->rx[i];
		adapter->queue[i].irq_mask =
			irq_mask << (ECM_INT_TXRX_SHIFT * i);
		adapter->queue[i].irq_delay_addr =
			adapter->addr + ECM_INT_DELAY + ECM_INT_DELAY_OFFSET * i;
		retval = tsnep_set_irq_coalesce(&adapter->queue[i],
						TSNEP_COALESCE_USECS_DEFAULT);
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int tsnep_probe(struct platform_device *pdev)
{
	struct tsnep_adapter *adapter;
	struct net_device *netdev;
	struct resource *io;
	u32 type;
	int revision;
	int version;
	int queue_count;
	int retval;

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct tsnep_adapter),
					 TSNEP_MAX_QUEUES, TSNEP_MAX_QUEUES);
	if (!netdev)
		return -ENODEV;
	SET_NETDEV_DEV(netdev, &pdev->dev);
	adapter = netdev_priv(netdev);
	platform_set_drvdata(pdev, adapter);
	adapter->pdev = pdev;
	adapter->dmadev = &pdev->dev;
	adapter->netdev = netdev;
	adapter->msg_enable = NETIF_MSG_DRV | NETIF_MSG_PROBE |
			      NETIF_MSG_LINK | NETIF_MSG_IFUP |
			      NETIF_MSG_IFDOWN | NETIF_MSG_TX_QUEUED;

	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = TSNEP_MAX_FRAME_SIZE;

	mutex_init(&adapter->gate_control_lock);
	mutex_init(&adapter->rxnfc_lock);
	INIT_LIST_HEAD(&adapter->rxnfc_rules);

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	adapter->addr = devm_ioremap_resource(&pdev->dev, io);
	if (IS_ERR(adapter->addr))
		return PTR_ERR(adapter->addr);
	netdev->mem_start = io->start;
	netdev->mem_end = io->end;

	type = ioread32(adapter->addr + ECM_TYPE);
	revision = (type & ECM_REVISION_MASK) >> ECM_REVISION_SHIFT;
	version = (type & ECM_VERSION_MASK) >> ECM_VERSION_SHIFT;
	queue_count = (type & ECM_QUEUE_COUNT_MASK) >> ECM_QUEUE_COUNT_SHIFT;
	adapter->gate_control = type & ECM_GATE_CONTROL;
	adapter->rxnfc_max = TSNEP_RX_ASSIGN_ETHER_TYPE_COUNT;

	tsnep_disable_irq(adapter, ECM_INT_ALL);

	retval = tsnep_queue_init(adapter, queue_count);
	if (retval)
		return retval;

	retval = dma_set_mask_and_coherent(&adapter->pdev->dev,
					   DMA_BIT_MASK(64));
	if (retval) {
		dev_err(&adapter->pdev->dev, "no usable DMA configuration.\n");
		return retval;
	}

	retval = tsnep_mac_init(adapter);
	if (retval)
		return retval;

	retval = tsnep_mdio_init(adapter);
	if (retval)
		goto mdio_init_failed;

	retval = tsnep_phy_init(adapter);
	if (retval)
		goto phy_init_failed;

	retval = tsnep_ptp_init(adapter);
	if (retval)
		goto ptp_init_failed;

	retval = tsnep_tc_init(adapter);
	if (retval)
		goto tc_init_failed;

	retval = tsnep_rxnfc_init(adapter);
	if (retval)
		goto rxnfc_init_failed;

	netdev->netdev_ops = &tsnep_netdev_ops;
	netdev->ethtool_ops = &tsnep_ethtool_ops;
	netdev->features = NETIF_F_SG;
	netdev->hw_features = netdev->features | NETIF_F_LOOPBACK;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	retval = register_netdev(netdev);
	if (retval)
		goto register_failed;

	dev_info(&adapter->pdev->dev, "device version %d.%02d\n", version,
		 revision);
	if (adapter->gate_control)
		dev_info(&adapter->pdev->dev, "gate control detected\n");

	return 0;

register_failed:
	tsnep_rxnfc_cleanup(adapter);
rxnfc_init_failed:
	tsnep_tc_cleanup(adapter);
tc_init_failed:
	tsnep_ptp_cleanup(adapter);
ptp_init_failed:
phy_init_failed:
	if (adapter->mdiobus)
		mdiobus_unregister(adapter->mdiobus);
mdio_init_failed:
	return retval;
}

static int tsnep_remove(struct platform_device *pdev)
{
	struct tsnep_adapter *adapter = platform_get_drvdata(pdev);

	unregister_netdev(adapter->netdev);

	tsnep_rxnfc_cleanup(adapter);

	tsnep_tc_cleanup(adapter);

	tsnep_ptp_cleanup(adapter);

	if (adapter->mdiobus)
		mdiobus_unregister(adapter->mdiobus);

	tsnep_disable_irq(adapter, ECM_INT_ALL);

	return 0;
}

static const struct of_device_id tsnep_of_match[] = {
	{ .compatible = "engleder,tsnep", },
{ },
};
MODULE_DEVICE_TABLE(of, tsnep_of_match);

static struct platform_driver tsnep_driver = {
	.driver = {
		.name = TSNEP,
		.of_match_table = tsnep_of_match,
	},
	.probe = tsnep_probe,
	.remove = tsnep_remove,
};
module_platform_driver(tsnep_driver);

MODULE_AUTHOR("Gerhard Engleder <gerhard@engleder-embedded.com>");
MODULE_DESCRIPTION("TSN endpoint Ethernet MAC driver");
MODULE_LICENSE("GPL");

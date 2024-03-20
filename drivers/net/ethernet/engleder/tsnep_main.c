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
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <net/page_pool/helpers.h>
#include <net/xdp_sock_drv.h>

#define TSNEP_RX_OFFSET (max(NET_SKB_PAD, XDP_PACKET_HEADROOM) + NET_IP_ALIGN)
#define TSNEP_HEADROOM ALIGN(TSNEP_RX_OFFSET, 4)
#define TSNEP_MAX_RX_BUF_SIZE (PAGE_SIZE - TSNEP_HEADROOM - \
			       SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
/* XSK buffer shall store at least Q-in-Q frame */
#define TSNEP_XSK_RX_BUF_SIZE (ALIGN(TSNEP_RX_INLINE_METADATA_SIZE + \
				     ETH_FRAME_LEN + ETH_FCS_LEN + \
				     VLAN_HLEN * 2, 4))

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#define DMA_ADDR_HIGH(dma_addr) ((u32)(((dma_addr) >> 32) & 0xFFFFFFFF))
#else
#define DMA_ADDR_HIGH(dma_addr) ((u32)(0))
#endif
#define DMA_ADDR_LOW(dma_addr) ((u32)((dma_addr) & 0xFFFFFFFF))

#define TSNEP_COALESCE_USECS_DEFAULT 64
#define TSNEP_COALESCE_USECS_MAX     ((ECM_INT_DELAY_MASK >> ECM_INT_DELAY_SHIFT) * \
				      ECM_INT_DELAY_BASE_US + ECM_INT_DELAY_BASE_US - 1)

#define TSNEP_TX_TYPE_SKB	BIT(0)
#define TSNEP_TX_TYPE_SKB_FRAG	BIT(1)
#define TSNEP_TX_TYPE_XDP_TX	BIT(2)
#define TSNEP_TX_TYPE_XDP_NDO	BIT(3)
#define TSNEP_TX_TYPE_XDP	(TSNEP_TX_TYPE_XDP_TX | TSNEP_TX_TYPE_XDP_NDO)
#define TSNEP_TX_TYPE_XSK	BIT(4)

#define TSNEP_XDP_TX		BIT(0)
#define TSNEP_XDP_REDIRECT	BIT(1)

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
		if (napi_schedule_prep(&adapter->queue[0].napi)) {
			tsnep_disable_irq(adapter, adapter->queue[0].irq_mask);
			/* schedule after masking to avoid races */
			__napi_schedule(&adapter->queue[0].napi);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t tsnep_irq_txrx(int irq, void *arg)
{
	struct tsnep_queue *queue = arg;

	/* handle TX/RX queue interrupt */
	if (napi_schedule_prep(&queue->napi)) {
		tsnep_disable_irq(queue->adapter, queue->irq_mask);
		/* schedule after masking to avoid races */
		__napi_schedule(&queue->napi);
	}

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

static int tsnep_tx_ring_create(struct tsnep_tx *tx)
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
			entry->owner_user_flag = false;
		}
	}
	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		entry = &tx->entry[i];
		next_entry = &tx->entry[(i + 1) & TSNEP_RING_MASK];
		entry->desc->next = __cpu_to_le64(next_entry->desc_dma);
	}

	return 0;

alloc_failed:
	tsnep_tx_ring_cleanup(tx);
	return retval;
}

static void tsnep_tx_init(struct tsnep_tx *tx)
{
	dma_addr_t dma;

	dma = tx->entry[0].desc_dma | TSNEP_RESET_OWNER_COUNTER;
	iowrite32(DMA_ADDR_LOW(dma), tx->addr + TSNEP_TX_DESC_ADDR_LOW);
	iowrite32(DMA_ADDR_HIGH(dma), tx->addr + TSNEP_TX_DESC_ADDR_HIGH);
	tx->write = 0;
	tx->read = 0;
	tx->owner_counter = 1;
	tx->increment_owner_counter = TSNEP_RING_SIZE - 1;
}

static void tsnep_tx_enable(struct tsnep_tx *tx)
{
	struct netdev_queue *nq;

	nq = netdev_get_tx_queue(tx->adapter->netdev, tx->queue_index);

	__netif_tx_lock_bh(nq);
	netif_tx_wake_queue(nq);
	__netif_tx_unlock_bh(nq);
}

static void tsnep_tx_disable(struct tsnep_tx *tx, struct napi_struct *napi)
{
	struct netdev_queue *nq;
	u32 val;

	nq = netdev_get_tx_queue(tx->adapter->netdev, tx->queue_index);

	__netif_tx_lock_bh(nq);
	netif_tx_stop_queue(nq);
	__netif_tx_unlock_bh(nq);

	/* wait until TX is done in hardware */
	readx_poll_timeout(ioread32, tx->addr + TSNEP_CONTROL, val,
			   ((val & TSNEP_CONTROL_TX_ENABLE) == 0), 10000,
			   1000000);

	/* wait until TX is also done in software */
	while (READ_ONCE(tx->read) != tx->write) {
		napi_schedule(napi);
		napi_synchronize(napi);
	}
}

static void tsnep_tx_activate(struct tsnep_tx *tx, int index, int length,
			      bool last)
{
	struct tsnep_tx_entry *entry = &tx->entry[index];

	entry->properties = 0;
	/* xdpf and zc are union with skb */
	if (entry->skb) {
		entry->properties = length & TSNEP_DESC_LENGTH_MASK;
		entry->properties |= TSNEP_DESC_INTERRUPT_FLAG;
		if ((entry->type & TSNEP_TX_TYPE_SKB) &&
		    (skb_shinfo(entry->skb)->tx_flags & SKBTX_IN_PROGRESS))
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
		entry = &tx->entry[(tx->write + i) & TSNEP_RING_MASK];

		if (!i) {
			len = skb_headlen(skb);
			dma = dma_map_single(dmadev, skb->data, len,
					     DMA_TO_DEVICE);

			entry->type = TSNEP_TX_TYPE_SKB;
		} else {
			len = skb_frag_size(&skb_shinfo(skb)->frags[i - 1]);
			dma = skb_frag_dma_map(dmadev,
					       &skb_shinfo(skb)->frags[i - 1],
					       0, len, DMA_TO_DEVICE);

			entry->type = TSNEP_TX_TYPE_SKB_FRAG;
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
		entry = &tx->entry[(index + i) & TSNEP_RING_MASK];

		if (entry->len) {
			if (entry->type & TSNEP_TX_TYPE_SKB)
				dma_unmap_single(dmadev,
						 dma_unmap_addr(entry, dma),
						 dma_unmap_len(entry, len),
						 DMA_TO_DEVICE);
			else if (entry->type &
				 (TSNEP_TX_TYPE_SKB_FRAG | TSNEP_TX_TYPE_XDP_NDO))
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
	int count = 1;
	struct tsnep_tx_entry *entry;
	int length;
	int i;
	int retval;

	if (skb_shinfo(skb)->nr_frags > 0)
		count += skb_shinfo(skb)->nr_frags;

	if (tsnep_tx_desc_available(tx) < count) {
		/* ring full, shall not happen because queue is stopped if full
		 * below
		 */
		netif_stop_subqueue(tx->adapter->netdev, tx->queue_index);

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

		return NETDEV_TX_OK;
	}
	length = retval;

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	for (i = 0; i < count; i++)
		tsnep_tx_activate(tx, (tx->write + i) & TSNEP_RING_MASK, length,
				  i == count - 1);
	tx->write = (tx->write + count) & TSNEP_RING_MASK;

	skb_tx_timestamp(skb);

	/* descriptor properties shall be valid before hardware is notified */
	dma_wmb();

	iowrite32(TSNEP_CONTROL_TX_ENABLE, tx->addr + TSNEP_CONTROL);

	if (tsnep_tx_desc_available(tx) < (MAX_SKB_FRAGS + 1)) {
		/* ring can get full with next frame */
		netif_stop_subqueue(tx->adapter->netdev, tx->queue_index);
	}

	return NETDEV_TX_OK;
}

static int tsnep_xdp_tx_map(struct xdp_frame *xdpf, struct tsnep_tx *tx,
			    struct skb_shared_info *shinfo, int count, u32 type)
{
	struct device *dmadev = tx->adapter->dmadev;
	struct tsnep_tx_entry *entry;
	struct page *page;
	skb_frag_t *frag;
	unsigned int len;
	int map_len = 0;
	dma_addr_t dma;
	void *data;
	int i;

	frag = NULL;
	len = xdpf->len;
	for (i = 0; i < count; i++) {
		entry = &tx->entry[(tx->write + i) & TSNEP_RING_MASK];
		if (type & TSNEP_TX_TYPE_XDP_NDO) {
			data = unlikely(frag) ? skb_frag_address(frag) :
						xdpf->data;
			dma = dma_map_single(dmadev, data, len, DMA_TO_DEVICE);
			if (dma_mapping_error(dmadev, dma))
				return -ENOMEM;

			entry->type = TSNEP_TX_TYPE_XDP_NDO;
		} else {
			page = unlikely(frag) ? skb_frag_page(frag) :
						virt_to_page(xdpf->data);
			dma = page_pool_get_dma_addr(page);
			if (unlikely(frag))
				dma += skb_frag_off(frag);
			else
				dma += sizeof(*xdpf) + xdpf->headroom;
			dma_sync_single_for_device(dmadev, dma, len,
						   DMA_BIDIRECTIONAL);

			entry->type = TSNEP_TX_TYPE_XDP_TX;
		}

		entry->len = len;
		dma_unmap_addr_set(entry, dma, dma);

		entry->desc->tx = __cpu_to_le64(dma);

		map_len += len;

		if (i + 1 < count) {
			frag = &shinfo->frags[i];
			len = skb_frag_size(frag);
		}
	}

	return map_len;
}

/* This function requires __netif_tx_lock is held by the caller. */
static bool tsnep_xdp_xmit_frame_ring(struct xdp_frame *xdpf,
				      struct tsnep_tx *tx, u32 type)
{
	struct skb_shared_info *shinfo = xdp_get_shared_info_from_frame(xdpf);
	struct tsnep_tx_entry *entry;
	int count, length, retval, i;

	count = 1;
	if (unlikely(xdp_frame_has_frags(xdpf)))
		count += shinfo->nr_frags;

	/* ensure that TX ring is not filled up by XDP, always MAX_SKB_FRAGS
	 * will be available for normal TX path and queue is stopped there if
	 * necessary
	 */
	if (tsnep_tx_desc_available(tx) < (MAX_SKB_FRAGS + 1 + count))
		return false;

	entry = &tx->entry[tx->write];
	entry->xdpf = xdpf;

	retval = tsnep_xdp_tx_map(xdpf, tx, shinfo, count, type);
	if (retval < 0) {
		tsnep_tx_unmap(tx, tx->write, count);
		entry->xdpf = NULL;

		tx->dropped++;

		return false;
	}
	length = retval;

	for (i = 0; i < count; i++)
		tsnep_tx_activate(tx, (tx->write + i) & TSNEP_RING_MASK, length,
				  i == count - 1);
	tx->write = (tx->write + count) & TSNEP_RING_MASK;

	/* descriptor properties shall be valid before hardware is notified */
	dma_wmb();

	return true;
}

static void tsnep_xdp_xmit_flush(struct tsnep_tx *tx)
{
	iowrite32(TSNEP_CONTROL_TX_ENABLE, tx->addr + TSNEP_CONTROL);
}

static bool tsnep_xdp_xmit_back(struct tsnep_adapter *adapter,
				struct xdp_buff *xdp,
				struct netdev_queue *tx_nq, struct tsnep_tx *tx,
				bool zc)
{
	struct xdp_frame *xdpf = xdp_convert_buff_to_frame(xdp);
	bool xmit;
	u32 type;

	if (unlikely(!xdpf))
		return false;

	/* no page pool for zero copy */
	if (zc)
		type = TSNEP_TX_TYPE_XDP_NDO;
	else
		type = TSNEP_TX_TYPE_XDP_TX;

	__netif_tx_lock(tx_nq, smp_processor_id());

	xmit = tsnep_xdp_xmit_frame_ring(xdpf, tx, type);

	/* Avoid transmit queue timeout since we share it with the slow path */
	if (xmit)
		txq_trans_cond_update(tx_nq);

	__netif_tx_unlock(tx_nq);

	return xmit;
}

static int tsnep_xdp_tx_map_zc(struct xdp_desc *xdpd, struct tsnep_tx *tx)
{
	struct tsnep_tx_entry *entry;
	dma_addr_t dma;

	entry = &tx->entry[tx->write];
	entry->zc = true;

	dma = xsk_buff_raw_get_dma(tx->xsk_pool, xdpd->addr);
	xsk_buff_raw_dma_sync_for_device(tx->xsk_pool, dma, xdpd->len);

	entry->type = TSNEP_TX_TYPE_XSK;
	entry->len = xdpd->len;

	entry->desc->tx = __cpu_to_le64(dma);

	return xdpd->len;
}

static void tsnep_xdp_xmit_frame_ring_zc(struct xdp_desc *xdpd,
					 struct tsnep_tx *tx)
{
	int length;

	length = tsnep_xdp_tx_map_zc(xdpd, tx);

	tsnep_tx_activate(tx, tx->write, length, true);
	tx->write = (tx->write + 1) & TSNEP_RING_MASK;
}

static void tsnep_xdp_xmit_zc(struct tsnep_tx *tx)
{
	int desc_available = tsnep_tx_desc_available(tx);
	struct xdp_desc *descs = tx->xsk_pool->tx_descs;
	int batch, i;

	/* ensure that TX ring is not filled up by XDP, always MAX_SKB_FRAGS
	 * will be available for normal TX path and queue is stopped there if
	 * necessary
	 */
	if (desc_available <= (MAX_SKB_FRAGS + 1))
		return;
	desc_available -= MAX_SKB_FRAGS + 1;

	batch = xsk_tx_peek_release_desc_batch(tx->xsk_pool, desc_available);
	for (i = 0; i < batch; i++)
		tsnep_xdp_xmit_frame_ring_zc(&descs[i], tx);

	if (batch) {
		/* descriptor properties shall be valid before hardware is
		 * notified
		 */
		dma_wmb();

		tsnep_xdp_xmit_flush(tx);
	}
}

static bool tsnep_tx_poll(struct tsnep_tx *tx, int napi_budget)
{
	struct tsnep_tx_entry *entry;
	struct netdev_queue *nq;
	int xsk_frames = 0;
	int budget = 128;
	int length;
	int count;

	nq = netdev_get_tx_queue(tx->adapter->netdev, tx->queue_index);
	__netif_tx_lock(nq, smp_processor_id());

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
		if ((entry->type & TSNEP_TX_TYPE_SKB) &&
		    skb_shinfo(entry->skb)->nr_frags > 0)
			count += skb_shinfo(entry->skb)->nr_frags;
		else if ((entry->type & TSNEP_TX_TYPE_XDP) &&
			 xdp_frame_has_frags(entry->xdpf))
			count += xdp_get_shared_info_from_frame(entry->xdpf)->nr_frags;

		length = tsnep_tx_unmap(tx, tx->read, count);

		if ((entry->type & TSNEP_TX_TYPE_SKB) &&
		    (skb_shinfo(entry->skb)->tx_flags & SKBTX_IN_PROGRESS) &&
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

		if (entry->type & TSNEP_TX_TYPE_SKB)
			napi_consume_skb(entry->skb, napi_budget);
		else if (entry->type & TSNEP_TX_TYPE_XDP)
			xdp_return_frame_rx_napi(entry->xdpf);
		else
			xsk_frames++;
		/* xdpf and zc are union with skb */
		entry->skb = NULL;

		tx->read = (tx->read + count) & TSNEP_RING_MASK;

		tx->packets++;
		tx->bytes += length + ETH_FCS_LEN;

		budget--;
	} while (likely(budget));

	if (tx->xsk_pool) {
		if (xsk_frames)
			xsk_tx_completed(tx->xsk_pool, xsk_frames);
		if (xsk_uses_need_wakeup(tx->xsk_pool))
			xsk_set_tx_need_wakeup(tx->xsk_pool);
		tsnep_xdp_xmit_zc(tx);
	}

	if ((tsnep_tx_desc_available(tx) >= ((MAX_SKB_FRAGS + 1) * 2)) &&
	    netif_tx_queue_stopped(nq)) {
		netif_tx_wake_queue(nq);
	}

	__netif_tx_unlock(nq);

	return budget != 0;
}

static bool tsnep_tx_pending(struct tsnep_tx *tx)
{
	struct tsnep_tx_entry *entry;
	struct netdev_queue *nq;
	bool pending = false;

	nq = netdev_get_tx_queue(tx->adapter->netdev, tx->queue_index);
	__netif_tx_lock(nq, smp_processor_id());

	if (tx->read != tx->write) {
		entry = &tx->entry[tx->read];
		if ((__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_TX_DESC_OWNER_MASK) ==
		    (entry->properties & TSNEP_TX_DESC_OWNER_MASK))
			pending = true;
	}

	__netif_tx_unlock(nq);

	return pending;
}

static int tsnep_tx_open(struct tsnep_tx *tx)
{
	int retval;

	retval = tsnep_tx_ring_create(tx);
	if (retval)
		return retval;

	tsnep_tx_init(tx);

	return 0;
}

static void tsnep_tx_close(struct tsnep_tx *tx)
{
	tsnep_tx_ring_cleanup(tx);
}

static void tsnep_rx_ring_cleanup(struct tsnep_rx *rx)
{
	struct device *dmadev = rx->adapter->dmadev;
	struct tsnep_rx_entry *entry;
	int i;

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		entry = &rx->entry[i];
		if (!rx->xsk_pool && entry->page)
			page_pool_put_full_page(rx->page_pool, entry->page,
						false);
		if (rx->xsk_pool && entry->xdp)
			xsk_buff_free(entry->xdp);
		/* xdp is union with page */
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

static int tsnep_rx_ring_create(struct tsnep_rx *rx)
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
	pp_params.dma_dir = DMA_BIDIRECTIONAL;
	pp_params.max_len = TSNEP_MAX_RX_BUF_SIZE;
	pp_params.offset = TSNEP_RX_OFFSET;
	rx->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rx->page_pool)) {
		retval = PTR_ERR(rx->page_pool);
		rx->page_pool = NULL;
		goto failed;
	}

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		entry = &rx->entry[i];
		next_entry = &rx->entry[(i + 1) & TSNEP_RING_MASK];
		entry->desc->next = __cpu_to_le64(next_entry->desc_dma);
	}

	return 0;

failed:
	tsnep_rx_ring_cleanup(rx);
	return retval;
}

static void tsnep_rx_init(struct tsnep_rx *rx)
{
	dma_addr_t dma;

	dma = rx->entry[0].desc_dma | TSNEP_RESET_OWNER_COUNTER;
	iowrite32(DMA_ADDR_LOW(dma), rx->addr + TSNEP_RX_DESC_ADDR_LOW);
	iowrite32(DMA_ADDR_HIGH(dma), rx->addr + TSNEP_RX_DESC_ADDR_HIGH);
	rx->write = 0;
	rx->read = 0;
	rx->owner_counter = 1;
	rx->increment_owner_counter = TSNEP_RING_SIZE - 1;
}

static void tsnep_rx_enable(struct tsnep_rx *rx)
{
	/* descriptor properties shall be valid before hardware is notified */
	dma_wmb();

	iowrite32(TSNEP_CONTROL_RX_ENABLE, rx->addr + TSNEP_CONTROL);
}

static void tsnep_rx_disable(struct tsnep_rx *rx)
{
	u32 val;

	iowrite32(TSNEP_CONTROL_RX_DISABLE, rx->addr + TSNEP_CONTROL);
	readx_poll_timeout(ioread32, rx->addr + TSNEP_CONTROL, val,
			   ((val & TSNEP_CONTROL_RX_ENABLE) == 0), 10000,
			   1000000);
}

static int tsnep_rx_desc_available(struct tsnep_rx *rx)
{
	if (rx->read <= rx->write)
		return TSNEP_RING_SIZE - rx->write + rx->read - 1;
	else
		return rx->read - rx->write - 1;
}

static void tsnep_rx_free_page_buffer(struct tsnep_rx *rx)
{
	struct page **page;

	/* last entry of page_buffer is always zero, because ring cannot be
	 * filled completely
	 */
	page = rx->page_buffer;
	while (*page) {
		page_pool_put_full_page(rx->page_pool, *page, false);
		*page = NULL;
		page++;
	}
}

static int tsnep_rx_alloc_page_buffer(struct tsnep_rx *rx)
{
	int i;

	/* alloc for all ring entries except the last one, because ring cannot
	 * be filled completely
	 */
	for (i = 0; i < TSNEP_RING_SIZE - 1; i++) {
		rx->page_buffer[i] = page_pool_dev_alloc_pages(rx->page_pool);
		if (!rx->page_buffer[i]) {
			tsnep_rx_free_page_buffer(rx);

			return -ENOMEM;
		}
	}

	return 0;
}

static void tsnep_rx_set_page(struct tsnep_rx *rx, struct tsnep_rx_entry *entry,
			      struct page *page)
{
	entry->page = page;
	entry->len = TSNEP_MAX_RX_BUF_SIZE;
	entry->dma = page_pool_get_dma_addr(entry->page);
	entry->desc->rx = __cpu_to_le64(entry->dma + TSNEP_RX_OFFSET);
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

	/* TSNEP_MAX_RX_BUF_SIZE and TSNEP_XSK_RX_BUF_SIZE are multiple of 4 */
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

static int tsnep_rx_alloc(struct tsnep_rx *rx, int count, bool reuse)
{
	bool alloc_failed = false;
	int i, index;

	for (i = 0; i < count && !alloc_failed; i++) {
		index = (rx->write + i) & TSNEP_RING_MASK;

		if (unlikely(tsnep_rx_alloc_buffer(rx, index))) {
			rx->alloc_failed++;
			alloc_failed = true;

			/* reuse only if no other allocation was successful */
			if (i == 0 && reuse)
				tsnep_rx_reuse_buffer(rx, index);
			else
				break;
		}

		tsnep_rx_activate(rx, index);
	}

	if (i)
		rx->write = (rx->write + i) & TSNEP_RING_MASK;

	return i;
}

static int tsnep_rx_refill(struct tsnep_rx *rx, int count, bool reuse)
{
	int desc_refilled;

	desc_refilled = tsnep_rx_alloc(rx, count, reuse);
	if (desc_refilled)
		tsnep_rx_enable(rx);

	return desc_refilled;
}

static void tsnep_rx_set_xdp(struct tsnep_rx *rx, struct tsnep_rx_entry *entry,
			     struct xdp_buff *xdp)
{
	entry->xdp = xdp;
	entry->len = TSNEP_XSK_RX_BUF_SIZE;
	entry->dma = xsk_buff_xdp_get_dma(entry->xdp);
	entry->desc->rx = __cpu_to_le64(entry->dma);
}

static void tsnep_rx_reuse_buffer_zc(struct tsnep_rx *rx, int index)
{
	struct tsnep_rx_entry *entry = &rx->entry[index];
	struct tsnep_rx_entry *read = &rx->entry[rx->read];

	tsnep_rx_set_xdp(rx, entry, read->xdp);
	read->xdp = NULL;
}

static int tsnep_rx_alloc_zc(struct tsnep_rx *rx, int count, bool reuse)
{
	u32 allocated;
	int i;

	allocated = xsk_buff_alloc_batch(rx->xsk_pool, rx->xdp_batch, count);
	for (i = 0; i < allocated; i++) {
		int index = (rx->write + i) & TSNEP_RING_MASK;
		struct tsnep_rx_entry *entry = &rx->entry[index];

		tsnep_rx_set_xdp(rx, entry, rx->xdp_batch[i]);
		tsnep_rx_activate(rx, index);
	}
	if (i == 0) {
		rx->alloc_failed++;

		if (reuse) {
			tsnep_rx_reuse_buffer_zc(rx, rx->write);
			tsnep_rx_activate(rx, rx->write);
		}
	}

	if (i)
		rx->write = (rx->write + i) & TSNEP_RING_MASK;

	return i;
}

static void tsnep_rx_free_zc(struct tsnep_rx *rx)
{
	int i;

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		struct tsnep_rx_entry *entry = &rx->entry[i];

		if (entry->xdp)
			xsk_buff_free(entry->xdp);
		entry->xdp = NULL;
	}
}

static int tsnep_rx_refill_zc(struct tsnep_rx *rx, int count, bool reuse)
{
	int desc_refilled;

	desc_refilled = tsnep_rx_alloc_zc(rx, count, reuse);
	if (desc_refilled)
		tsnep_rx_enable(rx);

	return desc_refilled;
}

static bool tsnep_xdp_run_prog(struct tsnep_rx *rx, struct bpf_prog *prog,
			       struct xdp_buff *xdp, int *status,
			       struct netdev_queue *tx_nq, struct tsnep_tx *tx)
{
	unsigned int length;
	unsigned int sync;
	u32 act;

	length = xdp->data_end - xdp->data_hard_start - XDP_PACKET_HEADROOM;

	act = bpf_prog_run_xdp(prog, xdp);
	switch (act) {
	case XDP_PASS:
		return false;
	case XDP_TX:
		if (!tsnep_xdp_xmit_back(rx->adapter, xdp, tx_nq, tx, false))
			goto out_failure;
		*status |= TSNEP_XDP_TX;
		return true;
	case XDP_REDIRECT:
		if (xdp_do_redirect(rx->adapter->netdev, xdp, prog) < 0)
			goto out_failure;
		*status |= TSNEP_XDP_REDIRECT;
		return true;
	default:
		bpf_warn_invalid_xdp_action(rx->adapter->netdev, prog, act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx->adapter->netdev, prog, act);
		fallthrough;
	case XDP_DROP:
		/* Due xdp_adjust_tail: DMA sync for_device cover max len CPU
		 * touch
		 */
		sync = xdp->data_end - xdp->data_hard_start -
		       XDP_PACKET_HEADROOM;
		sync = max(sync, length);
		page_pool_put_page(rx->page_pool, virt_to_head_page(xdp->data),
				   sync, true);
		return true;
	}
}

static bool tsnep_xdp_run_prog_zc(struct tsnep_rx *rx, struct bpf_prog *prog,
				  struct xdp_buff *xdp, int *status,
				  struct netdev_queue *tx_nq,
				  struct tsnep_tx *tx)
{
	u32 act;

	act = bpf_prog_run_xdp(prog, xdp);

	/* XDP_REDIRECT is the main action for zero-copy */
	if (likely(act == XDP_REDIRECT)) {
		if (xdp_do_redirect(rx->adapter->netdev, xdp, prog) < 0)
			goto out_failure;
		*status |= TSNEP_XDP_REDIRECT;
		return true;
	}

	switch (act) {
	case XDP_PASS:
		return false;
	case XDP_TX:
		if (!tsnep_xdp_xmit_back(rx->adapter, xdp, tx_nq, tx, true))
			goto out_failure;
		*status |= TSNEP_XDP_TX;
		return true;
	default:
		bpf_warn_invalid_xdp_action(rx->adapter->netdev, prog, act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx->adapter->netdev, prog, act);
		fallthrough;
	case XDP_DROP:
		xsk_buff_free(xdp);
		return true;
	}
}

static void tsnep_finalize_xdp(struct tsnep_adapter *adapter, int status,
			       struct netdev_queue *tx_nq, struct tsnep_tx *tx)
{
	if (status & TSNEP_XDP_TX) {
		__netif_tx_lock(tx_nq, smp_processor_id());
		tsnep_xdp_xmit_flush(tx);
		__netif_tx_unlock(tx_nq);
	}

	if (status & TSNEP_XDP_REDIRECT)
		xdp_do_flush();
}

static struct sk_buff *tsnep_build_skb(struct tsnep_rx *rx, struct page *page,
				       int length)
{
	struct sk_buff *skb;

	skb = napi_build_skb(page_address(page), PAGE_SIZE);
	if (unlikely(!skb))
		return NULL;

	/* update pointers within the skb to store the data */
	skb_reserve(skb, TSNEP_RX_OFFSET + TSNEP_RX_INLINE_METADATA_SIZE);
	__skb_put(skb, length - ETH_FCS_LEN);

	if (rx->adapter->hwtstamp_config.rx_filter == HWTSTAMP_FILTER_ALL) {
		struct skb_shared_hwtstamps *hwtstamps = skb_hwtstamps(skb);
		struct tsnep_rx_inline *rx_inline =
			(struct tsnep_rx_inline *)(page_address(page) +
						   TSNEP_RX_OFFSET);

		skb_shinfo(skb)->tx_flags |=
			SKBTX_HW_TSTAMP_NETDEV;
		memset(hwtstamps, 0, sizeof(*hwtstamps));
		hwtstamps->netdev_data = rx_inline;
	}

	skb_record_rx_queue(skb, rx->queue_index);
	skb->protocol = eth_type_trans(skb, rx->adapter->netdev);

	return skb;
}

static void tsnep_rx_page(struct tsnep_rx *rx, struct napi_struct *napi,
			  struct page *page, int length)
{
	struct sk_buff *skb;

	skb = tsnep_build_skb(rx, page, length);
	if (skb) {
		skb_mark_for_recycle(skb);

		rx->packets++;
		rx->bytes += length;
		if (skb->pkt_type == PACKET_MULTICAST)
			rx->multicast++;

		napi_gro_receive(napi, skb);
	} else {
		page_pool_recycle_direct(rx->page_pool, page);

		rx->dropped++;
	}
}

static int tsnep_rx_poll(struct tsnep_rx *rx, struct napi_struct *napi,
			 int budget)
{
	struct device *dmadev = rx->adapter->dmadev;
	enum dma_data_direction dma_dir;
	struct tsnep_rx_entry *entry;
	struct netdev_queue *tx_nq;
	struct bpf_prog *prog;
	struct xdp_buff xdp;
	struct tsnep_tx *tx;
	int desc_available;
	int xdp_status = 0;
	int done = 0;
	int length;

	desc_available = tsnep_rx_desc_available(rx);
	dma_dir = page_pool_get_dma_dir(rx->page_pool);
	prog = READ_ONCE(rx->adapter->xdp_prog);
	if (prog) {
		tx_nq = netdev_get_tx_queue(rx->adapter->netdev,
					    rx->tx_queue_index);
		tx = &rx->adapter->tx[rx->tx_queue_index];

		xdp_init_buff(&xdp, PAGE_SIZE, &rx->xdp_rxq);
	}

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
				rx->read = (rx->read + 1) & TSNEP_RING_MASK;
				desc_available++;

				rx->dropped++;

				continue;
			}
		}

		/* descriptor properties shall be read first, because valid data
		 * is signaled there
		 */
		dma_rmb();

		prefetch(page_address(entry->page) + TSNEP_RX_OFFSET);
		length = __le32_to_cpu(entry->desc_wb->properties) &
			 TSNEP_DESC_LENGTH_MASK;
		dma_sync_single_range_for_cpu(dmadev, entry->dma,
					      TSNEP_RX_OFFSET, length, dma_dir);

		/* RX metadata with timestamps is in front of actual data,
		 * subtract metadata size to get length of actual data and
		 * consider metadata size as offset of actual data during RX
		 * processing
		 */
		length -= TSNEP_RX_INLINE_METADATA_SIZE;

		rx->read = (rx->read + 1) & TSNEP_RING_MASK;
		desc_available++;

		if (prog) {
			bool consume;

			xdp_prepare_buff(&xdp, page_address(entry->page),
					 XDP_PACKET_HEADROOM + TSNEP_RX_INLINE_METADATA_SIZE,
					 length - ETH_FCS_LEN, false);

			consume = tsnep_xdp_run_prog(rx, prog, &xdp,
						     &xdp_status, tx_nq, tx);
			if (consume) {
				rx->packets++;
				rx->bytes += length;

				entry->page = NULL;

				continue;
			}
		}

		tsnep_rx_page(rx, napi, entry->page, length);
		entry->page = NULL;
	}

	if (xdp_status)
		tsnep_finalize_xdp(rx->adapter, xdp_status, tx_nq, tx);

	if (desc_available)
		tsnep_rx_refill(rx, desc_available, false);

	return done;
}

static int tsnep_rx_poll_zc(struct tsnep_rx *rx, struct napi_struct *napi,
			    int budget)
{
	struct tsnep_rx_entry *entry;
	struct netdev_queue *tx_nq;
	struct bpf_prog *prog;
	struct tsnep_tx *tx;
	int desc_available;
	int xdp_status = 0;
	struct page *page;
	int done = 0;
	int length;

	desc_available = tsnep_rx_desc_available(rx);
	prog = READ_ONCE(rx->adapter->xdp_prog);
	if (prog) {
		tx_nq = netdev_get_tx_queue(rx->adapter->netdev,
					    rx->tx_queue_index);
		tx = &rx->adapter->tx[rx->tx_queue_index];
	}

	while (likely(done < budget) && (rx->read != rx->write)) {
		entry = &rx->entry[rx->read];
		if ((__le32_to_cpu(entry->desc_wb->properties) &
		     TSNEP_DESC_OWNER_COUNTER_MASK) !=
		    (entry->properties & TSNEP_DESC_OWNER_COUNTER_MASK))
			break;
		done++;

		if (desc_available >= TSNEP_RING_RX_REFILL) {
			bool reuse = desc_available >= TSNEP_RING_RX_REUSE;

			desc_available -= tsnep_rx_refill_zc(rx, desc_available,
							     reuse);
			if (!entry->xdp) {
				/* buffer has been reused for refill to prevent
				 * empty RX ring, thus buffer cannot be used for
				 * RX processing
				 */
				rx->read = (rx->read + 1) & TSNEP_RING_MASK;
				desc_available++;

				rx->dropped++;

				continue;
			}
		}

		/* descriptor properties shall be read first, because valid data
		 * is signaled there
		 */
		dma_rmb();

		prefetch(entry->xdp->data);
		length = __le32_to_cpu(entry->desc_wb->properties) &
			 TSNEP_DESC_LENGTH_MASK;
		xsk_buff_set_size(entry->xdp, length - ETH_FCS_LEN);
		xsk_buff_dma_sync_for_cpu(entry->xdp, rx->xsk_pool);

		/* RX metadata with timestamps is in front of actual data,
		 * subtract metadata size to get length of actual data and
		 * consider metadata size as offset of actual data during RX
		 * processing
		 */
		length -= TSNEP_RX_INLINE_METADATA_SIZE;

		rx->read = (rx->read + 1) & TSNEP_RING_MASK;
		desc_available++;

		if (prog) {
			bool consume;

			entry->xdp->data += TSNEP_RX_INLINE_METADATA_SIZE;
			entry->xdp->data_meta += TSNEP_RX_INLINE_METADATA_SIZE;

			consume = tsnep_xdp_run_prog_zc(rx, prog, entry->xdp,
							&xdp_status, tx_nq, tx);
			if (consume) {
				rx->packets++;
				rx->bytes += length;

				entry->xdp = NULL;

				continue;
			}
		}

		page = page_pool_dev_alloc_pages(rx->page_pool);
		if (page) {
			memcpy(page_address(page) + TSNEP_RX_OFFSET,
			       entry->xdp->data - TSNEP_RX_INLINE_METADATA_SIZE,
			       length + TSNEP_RX_INLINE_METADATA_SIZE);
			tsnep_rx_page(rx, napi, page, length);
		} else {
			rx->dropped++;
		}
		xsk_buff_free(entry->xdp);
		entry->xdp = NULL;
	}

	if (xdp_status)
		tsnep_finalize_xdp(rx->adapter, xdp_status, tx_nq, tx);

	if (desc_available)
		desc_available -= tsnep_rx_refill_zc(rx, desc_available, false);

	if (xsk_uses_need_wakeup(rx->xsk_pool)) {
		if (desc_available)
			xsk_set_rx_need_wakeup(rx->xsk_pool);
		else
			xsk_clear_rx_need_wakeup(rx->xsk_pool);

		return done;
	}

	return desc_available ? budget : done;
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

static int tsnep_rx_open(struct tsnep_rx *rx)
{
	int desc_available;
	int retval;

	retval = tsnep_rx_ring_create(rx);
	if (retval)
		return retval;

	tsnep_rx_init(rx);

	desc_available = tsnep_rx_desc_available(rx);
	if (rx->xsk_pool)
		retval = tsnep_rx_alloc_zc(rx, desc_available, false);
	else
		retval = tsnep_rx_alloc(rx, desc_available, false);
	if (retval != desc_available) {
		retval = -ENOMEM;

		goto alloc_failed;
	}

	/* prealloc pages to prevent allocation failures when XSK pool is
	 * disabled at runtime
	 */
	if (rx->xsk_pool) {
		retval = tsnep_rx_alloc_page_buffer(rx);
		if (retval)
			goto alloc_failed;
	}

	return 0;

alloc_failed:
	tsnep_rx_ring_cleanup(rx);
	return retval;
}

static void tsnep_rx_close(struct tsnep_rx *rx)
{
	if (rx->xsk_pool)
		tsnep_rx_free_page_buffer(rx);

	tsnep_rx_ring_cleanup(rx);
}

static void tsnep_rx_reopen(struct tsnep_rx *rx)
{
	struct page **page = rx->page_buffer;
	int i;

	tsnep_rx_init(rx);

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		struct tsnep_rx_entry *entry = &rx->entry[i];

		/* defined initial values for properties are required for
		 * correct owner counter checking
		 */
		entry->desc->properties = 0;
		entry->desc_wb->properties = 0;

		/* prevent allocation failures by reusing kept pages */
		if (*page) {
			tsnep_rx_set_page(rx, entry, *page);
			tsnep_rx_activate(rx, rx->write);
			rx->write++;

			*page = NULL;
			page++;
		}
	}
}

static void tsnep_rx_reopen_xsk(struct tsnep_rx *rx)
{
	struct page **page = rx->page_buffer;
	u32 allocated;
	int i;

	tsnep_rx_init(rx);

	/* alloc all ring entries except the last one, because ring cannot be
	 * filled completely, as many buffers as possible is enough as wakeup is
	 * done if new buffers are available
	 */
	allocated = xsk_buff_alloc_batch(rx->xsk_pool, rx->xdp_batch,
					 TSNEP_RING_SIZE - 1);

	for (i = 0; i < TSNEP_RING_SIZE; i++) {
		struct tsnep_rx_entry *entry = &rx->entry[i];

		/* keep pages to prevent allocation failures when xsk is
		 * disabled
		 */
		if (entry->page) {
			*page = entry->page;
			entry->page = NULL;

			page++;
		}

		/* defined initial values for properties are required for
		 * correct owner counter checking
		 */
		entry->desc->properties = 0;
		entry->desc_wb->properties = 0;

		if (allocated) {
			tsnep_rx_set_xdp(rx, entry,
					 rx->xdp_batch[allocated - 1]);
			tsnep_rx_activate(rx, rx->write);
			rx->write++;

			allocated--;
		}
	}

	/* set need wakeup flag immediately if ring is not filled completely,
	 * first polling would be too late as need wakeup signalisation would
	 * be delayed for an indefinite time
	 */
	if (xsk_uses_need_wakeup(rx->xsk_pool)) {
		int desc_available = tsnep_rx_desc_available(rx);

		if (desc_available)
			xsk_set_rx_need_wakeup(rx->xsk_pool);
		else
			xsk_clear_rx_need_wakeup(rx->xsk_pool);
	}
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

	/* handle case where we are called by netpoll with a budget of 0 */
	if (unlikely(budget <= 0))
		return budget;

	if (queue->rx) {
		done = queue->rx->xsk_pool ?
		       tsnep_rx_poll_zc(queue->rx, napi, budget) :
		       tsnep_rx_poll(queue->rx, napi, budget);
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
			snprintf(queue->name, sizeof(queue->name), "%s-txrx-%d",
				 name, queue->rx->queue_index);
		else if (queue->tx)
			snprintf(queue->name, sizeof(queue->name), "%s-tx-%d",
				 name, queue->tx->queue_index);
		else
			snprintf(queue->name, sizeof(queue->name), "%s-rx-%d",
				 name, queue->rx->queue_index);
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

static void tsnep_queue_close(struct tsnep_queue *queue, bool first)
{
	struct tsnep_rx *rx = queue->rx;

	tsnep_free_irq(queue, first);

	if (rx) {
		if (xdp_rxq_info_is_reg(&rx->xdp_rxq))
			xdp_rxq_info_unreg(&rx->xdp_rxq);
		if (xdp_rxq_info_is_reg(&rx->xdp_rxq_zc))
			xdp_rxq_info_unreg(&rx->xdp_rxq_zc);
	}

	netif_napi_del(&queue->napi);
}

static int tsnep_queue_open(struct tsnep_adapter *adapter,
			    struct tsnep_queue *queue, bool first)
{
	struct tsnep_rx *rx = queue->rx;
	struct tsnep_tx *tx = queue->tx;
	int retval;

	netif_napi_add(adapter->netdev, &queue->napi, tsnep_poll);

	if (rx) {
		/* choose TX queue for XDP_TX */
		if (tx)
			rx->tx_queue_index = tx->queue_index;
		else if (rx->queue_index < adapter->num_tx_queues)
			rx->tx_queue_index = rx->queue_index;
		else
			rx->tx_queue_index = 0;

		/* prepare both memory models to eliminate possible registration
		 * errors when memory model is switched between page pool and
		 * XSK pool during runtime
		 */
		retval = xdp_rxq_info_reg(&rx->xdp_rxq, adapter->netdev,
					  rx->queue_index, queue->napi.napi_id);
		if (retval)
			goto failed;
		retval = xdp_rxq_info_reg_mem_model(&rx->xdp_rxq,
						    MEM_TYPE_PAGE_POOL,
						    rx->page_pool);
		if (retval)
			goto failed;
		retval = xdp_rxq_info_reg(&rx->xdp_rxq_zc, adapter->netdev,
					  rx->queue_index, queue->napi.napi_id);
		if (retval)
			goto failed;
		retval = xdp_rxq_info_reg_mem_model(&rx->xdp_rxq_zc,
						    MEM_TYPE_XSK_BUFF_POOL,
						    NULL);
		if (retval)
			goto failed;
		if (rx->xsk_pool)
			xsk_pool_set_rxq_info(rx->xsk_pool, &rx->xdp_rxq_zc);
	}

	retval = tsnep_request_irq(queue, first);
	if (retval) {
		netif_err(adapter, drv, adapter->netdev,
			  "can't get assigned irq %d.\n", queue->irq);
		goto failed;
	}

	return 0;

failed:
	tsnep_queue_close(queue, first);

	return retval;
}

static void tsnep_queue_enable(struct tsnep_queue *queue)
{
	napi_enable(&queue->napi);
	tsnep_enable_irq(queue->adapter, queue->irq_mask);

	if (queue->tx)
		tsnep_tx_enable(queue->tx);

	if (queue->rx)
		tsnep_rx_enable(queue->rx);
}

static void tsnep_queue_disable(struct tsnep_queue *queue)
{
	if (queue->tx)
		tsnep_tx_disable(queue->tx, &queue->napi);

	napi_disable(&queue->napi);
	tsnep_disable_irq(queue->adapter, queue->irq_mask);

	/* disable RX after NAPI polling has been disabled, because RX can be
	 * enabled during NAPI polling
	 */
	if (queue->rx)
		tsnep_rx_disable(queue->rx);
}

static int tsnep_netdev_open(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int i, retval;

	for (i = 0; i < adapter->num_queues; i++) {
		if (adapter->queue[i].tx) {
			retval = tsnep_tx_open(adapter->queue[i].tx);
			if (retval)
				goto failed;
		}
		if (adapter->queue[i].rx) {
			retval = tsnep_rx_open(adapter->queue[i].rx);
			if (retval)
				goto failed;
		}

		retval = tsnep_queue_open(adapter, &adapter->queue[i], i == 0);
		if (retval)
			goto failed;
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

	for (i = 0; i < adapter->num_queues; i++)
		tsnep_queue_enable(&adapter->queue[i]);

	return 0;

phy_failed:
	tsnep_disable_irq(adapter, ECM_INT_LINK);
failed:
	for (i = 0; i < adapter->num_queues; i++) {
		tsnep_queue_close(&adapter->queue[i], i == 0);

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
		tsnep_queue_disable(&adapter->queue[i]);

		tsnep_queue_close(&adapter->queue[i], i == 0);

		if (adapter->queue[i].rx)
			tsnep_rx_close(adapter->queue[i].rx);
		if (adapter->queue[i].tx)
			tsnep_tx_close(adapter->queue[i].tx);
	}

	return 0;
}

int tsnep_enable_xsk(struct tsnep_queue *queue, struct xsk_buff_pool *pool)
{
	bool running = netif_running(queue->adapter->netdev);
	u32 frame_size;

	frame_size = xsk_pool_get_rx_frame_size(pool);
	if (frame_size < TSNEP_XSK_RX_BUF_SIZE)
		return -EOPNOTSUPP;

	queue->rx->page_buffer = kcalloc(TSNEP_RING_SIZE,
					 sizeof(*queue->rx->page_buffer),
					 GFP_KERNEL);
	if (!queue->rx->page_buffer)
		return -ENOMEM;
	queue->rx->xdp_batch = kcalloc(TSNEP_RING_SIZE,
				       sizeof(*queue->rx->xdp_batch),
				       GFP_KERNEL);
	if (!queue->rx->xdp_batch) {
		kfree(queue->rx->page_buffer);
		queue->rx->page_buffer = NULL;

		return -ENOMEM;
	}

	xsk_pool_set_rxq_info(pool, &queue->rx->xdp_rxq_zc);

	if (running)
		tsnep_queue_disable(queue);

	queue->tx->xsk_pool = pool;
	queue->rx->xsk_pool = pool;

	if (running) {
		tsnep_rx_reopen_xsk(queue->rx);
		tsnep_queue_enable(queue);
	}

	return 0;
}

void tsnep_disable_xsk(struct tsnep_queue *queue)
{
	bool running = netif_running(queue->adapter->netdev);

	if (running)
		tsnep_queue_disable(queue);

	tsnep_rx_free_zc(queue->rx);

	queue->rx->xsk_pool = NULL;
	queue->tx->xsk_pool = NULL;

	if (running) {
		tsnep_rx_reopen(queue->rx);
		tsnep_queue_enable(queue);
	}

	kfree(queue->rx->xdp_batch);
	queue->rx->xdp_batch = NULL;
	kfree(queue->rx->page_buffer);
	queue->rx->page_buffer = NULL;
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

static int tsnep_netdev_bpf(struct net_device *dev, struct netdev_bpf *bpf)
{
	struct tsnep_adapter *adapter = netdev_priv(dev);

	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return tsnep_xdp_setup_prog(adapter, bpf->prog, bpf->extack);
	case XDP_SETUP_XSK_POOL:
		return tsnep_xdp_setup_pool(adapter, bpf->xsk.pool,
					    bpf->xsk.queue_id);
	default:
		return -EOPNOTSUPP;
	}
}

static struct tsnep_tx *tsnep_xdp_get_tx(struct tsnep_adapter *adapter, u32 cpu)
{
	if (cpu >= TSNEP_MAX_QUEUES)
		cpu &= TSNEP_MAX_QUEUES - 1;

	while (cpu >= adapter->num_tx_queues)
		cpu -= adapter->num_tx_queues;

	return &adapter->tx[cpu];
}

static int tsnep_netdev_xdp_xmit(struct net_device *dev, int n,
				 struct xdp_frame **xdp, u32 flags)
{
	struct tsnep_adapter *adapter = netdev_priv(dev);
	u32 cpu = smp_processor_id();
	struct netdev_queue *nq;
	struct tsnep_tx *tx;
	int nxmit;
	bool xmit;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	tx = tsnep_xdp_get_tx(adapter, cpu);
	nq = netdev_get_tx_queue(adapter->netdev, tx->queue_index);

	__netif_tx_lock(nq, cpu);

	for (nxmit = 0; nxmit < n; nxmit++) {
		xmit = tsnep_xdp_xmit_frame_ring(xdp[nxmit], tx,
						 TSNEP_TX_TYPE_XDP_NDO);
		if (!xmit)
			break;

		/* avoid transmit queue timeout since we share it with the slow
		 * path
		 */
		txq_trans_cond_update(nq);
	}

	if (flags & XDP_XMIT_FLUSH)
		tsnep_xdp_xmit_flush(tx);

	__netif_tx_unlock(nq);

	return nxmit;
}

static int tsnep_netdev_xsk_wakeup(struct net_device *dev, u32 queue_id,
				   u32 flags)
{
	struct tsnep_adapter *adapter = netdev_priv(dev);
	struct tsnep_queue *queue;

	if (queue_id >= adapter->num_rx_queues ||
	    queue_id >= adapter->num_tx_queues)
		return -EINVAL;

	queue = &adapter->queue[queue_id];

	if (!napi_if_scheduled_mark_missed(&queue->napi))
		napi_schedule(&queue->napi);

	return 0;
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
	.ndo_bpf = tsnep_netdev_bpf,
	.ndo_xdp_xmit = tsnep_netdev_xdp_xmit,
	.ndo_xsk_wakeup = tsnep_netdev_xsk_wakeup,
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
	adapter->queue[0].adapter = adapter;
	adapter->queue[0].irq = retval;
	adapter->queue[0].tx = &adapter->tx[0];
	adapter->queue[0].tx->adapter = adapter;
	adapter->queue[0].tx->addr = adapter->addr + TSNEP_QUEUE(0);
	adapter->queue[0].tx->queue_index = 0;
	adapter->queue[0].rx = &adapter->rx[0];
	adapter->queue[0].rx->adapter = adapter;
	adapter->queue[0].rx->addr = adapter->addr + TSNEP_QUEUE(0);
	adapter->queue[0].rx->queue_index = 0;
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
		adapter->queue[i].adapter = adapter;
		adapter->queue[i].irq = retval;
		adapter->queue[i].tx = &adapter->tx[i];
		adapter->queue[i].tx->adapter = adapter;
		adapter->queue[i].tx->addr = adapter->addr + TSNEP_QUEUE(i);
		adapter->queue[i].tx->queue_index = i;
		adapter->queue[i].rx = &adapter->rx[i];
		adapter->queue[i].rx->adapter = adapter;
		adapter->queue[i].rx->addr = adapter->addr + TSNEP_QUEUE(i);
		adapter->queue[i].rx->queue_index = i;
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

	netdev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
			       NETDEV_XDP_ACT_NDO_XMIT |
			       NETDEV_XDP_ACT_NDO_XMIT_SG |
			       NETDEV_XDP_ACT_XSK_ZEROCOPY;

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

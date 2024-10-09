// SPDX-License-Identifier: GPL-2.0-only
/* Altera Triple-Speed Ethernet MAC driver
 * Copyright (C) 2008-2014 Altera Corporation. All rights reserved
 *
 * Contributors:
 *   Dalon Westergreen
 *   Thomas Chou
 *   Ian Abbott
 *   Yuriy Kozlov
 *   Tobias Klauser
 *   Andriy Smolskyy
 *   Roman Bulgakov
 *   Dmytro Mytarchuk
 *   Matthew Gerlach
 *
 * Original driver contributed by SLS.
 * Major updates contributed by GlobalLogic
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/mdio/mdio-regmap.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/pcs-lynx.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/skbuff.h>
#include <asm/cacheflush.h>

#include "altera_utils.h"
#include "altera_tse.h"
#include "altera_sgdma.h"
#include "altera_msgdma.h"

static atomic_t instance_count = ATOMIC_INIT(~0);
/* Module parameters */
static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Message Level (-1: default, 0: no output, 16: all)");

static const u32 default_msg_level = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
					NETIF_MSG_LINK | NETIF_MSG_IFUP |
					NETIF_MSG_IFDOWN);

#define RX_DESCRIPTORS 64
static int dma_rx_num = RX_DESCRIPTORS;
module_param(dma_rx_num, int, 0644);
MODULE_PARM_DESC(dma_rx_num, "Number of descriptors in the RX list");

#define TX_DESCRIPTORS 64
static int dma_tx_num = TX_DESCRIPTORS;
module_param(dma_tx_num, int, 0644);
MODULE_PARM_DESC(dma_tx_num, "Number of descriptors in the TX list");


#define POLL_PHY (-1)

/* Make sure DMA buffer size is larger than the max frame size
 * plus some alignment offset and a VLAN header. If the max frame size is
 * 1518, a VLAN header would be additional 4 bytes and additional
 * headroom for alignment is 2 bytes, 2048 is just fine.
 */
#define ALTERA_RXDMABUFFER_SIZE	2048

/* Allow network stack to resume queuing packets after we've
 * finished transmitting at least 1/4 of the packets in the queue.
 */
#define TSE_TX_THRESH(x)	(x->tx_ring_size / 4)

#define TXQUEUESTOP_THRESHHOLD	2

static inline u32 tse_tx_avail(struct altera_tse_private *priv)
{
	return priv->tx_cons + priv->tx_ring_size - priv->tx_prod - 1;
}

/* MDIO specific functions
 */
static int altera_tse_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct net_device *ndev = bus->priv;
	struct altera_tse_private *priv = netdev_priv(ndev);

	/* set MDIO address */
	csrwr32((mii_id & 0x1f), priv->mac_dev,
		tse_csroffs(mdio_phy1_addr));

	/* get the data */
	return csrrd32(priv->mac_dev,
		       tse_csroffs(mdio_phy1) + regnum * 4) & 0xffff;
}

static int altera_tse_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
				 u16 value)
{
	struct net_device *ndev = bus->priv;
	struct altera_tse_private *priv = netdev_priv(ndev);

	/* set MDIO address */
	csrwr32((mii_id & 0x1f), priv->mac_dev,
		tse_csroffs(mdio_phy1_addr));

	/* write the data */
	csrwr32(value, priv->mac_dev, tse_csroffs(mdio_phy1) + regnum * 4);
	return 0;
}

static int altera_tse_mdio_create(struct net_device *dev, unsigned int id)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	struct device_node *mdio_node = NULL;
	struct device_node *child_node = NULL;
	struct mii_bus *mdio = NULL;
	int ret;

	for_each_child_of_node(priv->device->of_node, child_node) {
		if (of_device_is_compatible(child_node, "altr,tse-mdio")) {
			mdio_node = child_node;
			break;
		}
	}

	if (mdio_node) {
		netdev_dbg(dev, "FOUND MDIO subnode\n");
	} else {
		netdev_dbg(dev, "NO MDIO subnode\n");
		return 0;
	}

	mdio = mdiobus_alloc();
	if (mdio == NULL) {
		netdev_err(dev, "Error allocating MDIO bus\n");
		ret = -ENOMEM;
		goto put_node;
	}

	mdio->name = ALTERA_TSE_RESOURCE_NAME;
	mdio->read = &altera_tse_mdio_read;
	mdio->write = &altera_tse_mdio_write;
	snprintf(mdio->id, MII_BUS_ID_SIZE, "%s-%u", mdio->name, id);

	mdio->priv = dev;
	mdio->parent = priv->device;

	ret = of_mdiobus_register(mdio, mdio_node);
	if (ret != 0) {
		netdev_err(dev, "Cannot register MDIO bus %s\n",
			   mdio->id);
		goto out_free_mdio;
	}
	of_node_put(mdio_node);

	if (netif_msg_drv(priv))
		netdev_info(dev, "MDIO bus %s: created\n", mdio->id);

	priv->mdio = mdio;
	return 0;
out_free_mdio:
	mdiobus_free(mdio);
	mdio = NULL;
put_node:
	of_node_put(mdio_node);
	return ret;
}

static void altera_tse_mdio_destroy(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);

	if (priv->mdio == NULL)
		return;

	if (netif_msg_drv(priv))
		netdev_info(dev, "MDIO bus %s: removed\n",
			    priv->mdio->id);

	mdiobus_unregister(priv->mdio);
	mdiobus_free(priv->mdio);
	priv->mdio = NULL;
}

static int tse_init_rx_buffer(struct altera_tse_private *priv,
			      struct tse_buffer *rxbuffer, int len)
{
	rxbuffer->skb = netdev_alloc_skb_ip_align(priv->dev, len);
	if (!rxbuffer->skb)
		return -ENOMEM;

	rxbuffer->dma_addr = dma_map_single(priv->device, rxbuffer->skb->data,
						len,
						DMA_FROM_DEVICE);

	if (dma_mapping_error(priv->device, rxbuffer->dma_addr)) {
		netdev_err(priv->dev, "%s: DMA mapping error\n", __func__);
		dev_kfree_skb_any(rxbuffer->skb);
		return -EINVAL;
	}
	rxbuffer->dma_addr &= (dma_addr_t)~3;
	rxbuffer->len = len;
	return 0;
}

static void tse_free_rx_buffer(struct altera_tse_private *priv,
			       struct tse_buffer *rxbuffer)
{
	dma_addr_t dma_addr = rxbuffer->dma_addr;
	struct sk_buff *skb = rxbuffer->skb;

	if (skb != NULL) {
		if (dma_addr)
			dma_unmap_single(priv->device, dma_addr,
					 rxbuffer->len,
					 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		rxbuffer->skb = NULL;
		rxbuffer->dma_addr = 0;
	}
}

/* Unmap and free Tx buffer resources
 */
static void tse_free_tx_buffer(struct altera_tse_private *priv,
			       struct tse_buffer *buffer)
{
	if (buffer->dma_addr) {
		if (buffer->mapped_as_page)
			dma_unmap_page(priv->device, buffer->dma_addr,
				       buffer->len, DMA_TO_DEVICE);
		else
			dma_unmap_single(priv->device, buffer->dma_addr,
					 buffer->len, DMA_TO_DEVICE);
		buffer->dma_addr = 0;
	}
	if (buffer->skb) {
		dev_kfree_skb_any(buffer->skb);
		buffer->skb = NULL;
	}
}

static int alloc_init_skbufs(struct altera_tse_private *priv)
{
	unsigned int rx_descs = priv->rx_ring_size;
	unsigned int tx_descs = priv->tx_ring_size;
	int ret = -ENOMEM;
	int i;

	/* Create Rx ring buffer */
	priv->rx_ring = kcalloc(rx_descs, sizeof(struct tse_buffer),
				GFP_KERNEL);
	if (!priv->rx_ring)
		goto err_rx_ring;

	/* Create Tx ring buffer */
	priv->tx_ring = kcalloc(tx_descs, sizeof(struct tse_buffer),
				GFP_KERNEL);
	if (!priv->tx_ring)
		goto err_tx_ring;

	priv->tx_cons = 0;
	priv->tx_prod = 0;

	/* Init Rx ring */
	for (i = 0; i < rx_descs; i++) {
		ret = tse_init_rx_buffer(priv, &priv->rx_ring[i],
					 priv->rx_dma_buf_sz);
		if (ret)
			goto err_init_rx_buffers;
	}

	priv->rx_cons = 0;
	priv->rx_prod = 0;

	return 0;
err_init_rx_buffers:
	while (--i >= 0)
		tse_free_rx_buffer(priv, &priv->rx_ring[i]);
	kfree(priv->tx_ring);
err_tx_ring:
	kfree(priv->rx_ring);
err_rx_ring:
	return ret;
}

static void free_skbufs(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	unsigned int rx_descs = priv->rx_ring_size;
	unsigned int tx_descs = priv->tx_ring_size;
	int i;

	/* Release the DMA TX/RX socket buffers */
	for (i = 0; i < rx_descs; i++)
		tse_free_rx_buffer(priv, &priv->rx_ring[i]);
	for (i = 0; i < tx_descs; i++)
		tse_free_tx_buffer(priv, &priv->tx_ring[i]);


	kfree(priv->tx_ring);
}

/* Reallocate the skb for the reception process
 */
static inline void tse_rx_refill(struct altera_tse_private *priv)
{
	unsigned int rxsize = priv->rx_ring_size;
	unsigned int entry;
	int ret;

	for (; priv->rx_cons - priv->rx_prod > 0;
			priv->rx_prod++) {
		entry = priv->rx_prod % rxsize;
		if (likely(priv->rx_ring[entry].skb == NULL)) {
			ret = tse_init_rx_buffer(priv, &priv->rx_ring[entry],
				priv->rx_dma_buf_sz);
			if (unlikely(ret != 0))
				break;
			priv->dmaops->add_rx_desc(priv, &priv->rx_ring[entry]);
		}
	}
}

/* Pull out the VLAN tag and fix up the packet
 */
static inline void tse_rx_vlan(struct net_device *dev, struct sk_buff *skb)
{
	struct ethhdr *eth_hdr;
	u16 vid;

	if ((dev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    !__vlan_get_tag(skb, &vid)) {
		eth_hdr = (struct ethhdr *)skb->data;
		memmove(skb->data + VLAN_HLEN, eth_hdr, ETH_ALEN * 2);
		skb_pull(skb, VLAN_HLEN);
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}
}

/* Receive a packet: retrieve and pass over to upper levels
 */
static int tse_rx(struct altera_tse_private *priv, int limit)
{
	unsigned int entry = priv->rx_cons % priv->rx_ring_size;
	unsigned int next_entry;
	unsigned int count = 0;
	struct sk_buff *skb;
	u32 rxstatus;
	u16 pktlength;
	u16 pktstatus;

	/* Check for count < limit first as get_rx_status is changing
	* the response-fifo so we must process the next packet
	* after calling get_rx_status if a response is pending.
	* (reading the last byte of the response pops the value from the fifo.)
	*/
	while ((count < limit) &&
	       ((rxstatus = priv->dmaops->get_rx_status(priv)) != 0)) {
		pktstatus = rxstatus >> 16;
		pktlength = rxstatus & 0xffff;

		if ((pktstatus & 0xFF) || (pktlength == 0))
			netdev_err(priv->dev,
				   "RCV pktstatus %08X pktlength %08X\n",
				   pktstatus, pktlength);

		/* DMA transfer from TSE starts with 2 additional bytes for
		 * IP payload alignment. Status returned by get_rx_status()
		 * contains DMA transfer length. Packet is 2 bytes shorter.
		 */
		pktlength -= 2;

		count++;
		next_entry = (++priv->rx_cons) % priv->rx_ring_size;

		skb = priv->rx_ring[entry].skb;
		if (unlikely(!skb)) {
			netdev_err(priv->dev,
				   "%s: Inconsistent Rx descriptor chain\n",
				   __func__);
			priv->dev->stats.rx_dropped++;
			break;
		}
		priv->rx_ring[entry].skb = NULL;

		skb_put(skb, pktlength);

		dma_unmap_single(priv->device, priv->rx_ring[entry].dma_addr,
				 priv->rx_ring[entry].len, DMA_FROM_DEVICE);

		if (netif_msg_pktdata(priv)) {
			netdev_info(priv->dev, "frame received %d bytes\n",
				    pktlength);
			print_hex_dump(KERN_ERR, "data: ", DUMP_PREFIX_OFFSET,
				       16, 1, skb->data, pktlength, true);
		}

		tse_rx_vlan(priv->dev, skb);

		skb->protocol = eth_type_trans(skb, priv->dev);
		skb_checksum_none_assert(skb);

		napi_gro_receive(&priv->napi, skb);

		priv->dev->stats.rx_packets++;
		priv->dev->stats.rx_bytes += pktlength;

		entry = next_entry;

		tse_rx_refill(priv);
	}

	return count;
}

/* Reclaim resources after transmission completes
 */
static int tse_tx_complete(struct altera_tse_private *priv)
{
	unsigned int txsize = priv->tx_ring_size;
	struct tse_buffer *tx_buff;
	unsigned int entry;
	int txcomplete = 0;
	u32 ready;

	spin_lock(&priv->tx_lock);

	ready = priv->dmaops->tx_completions(priv);

	/* Free sent buffers */
	while (ready && (priv->tx_cons != priv->tx_prod)) {
		entry = priv->tx_cons % txsize;
		tx_buff = &priv->tx_ring[entry];

		if (netif_msg_tx_done(priv))
			netdev_dbg(priv->dev, "%s: curr %d, dirty %d\n",
				   __func__, priv->tx_prod, priv->tx_cons);

		if (likely(tx_buff->skb))
			priv->dev->stats.tx_packets++;

		tse_free_tx_buffer(priv, tx_buff);
		priv->tx_cons++;

		txcomplete++;
		ready--;
	}

	if (unlikely(netif_queue_stopped(priv->dev) &&
		     tse_tx_avail(priv) > TSE_TX_THRESH(priv))) {
		if (netif_queue_stopped(priv->dev) &&
		    tse_tx_avail(priv) > TSE_TX_THRESH(priv)) {
			if (netif_msg_tx_done(priv))
				netdev_dbg(priv->dev, "%s: restart transmit\n",
					   __func__);
			netif_wake_queue(priv->dev);
		}
	}

	spin_unlock(&priv->tx_lock);
	return txcomplete;
}

/* NAPI polling function
 */
static int tse_poll(struct napi_struct *napi, int budget)
{
	struct altera_tse_private *priv =
			container_of(napi, struct altera_tse_private, napi);
	unsigned long int flags;
	int rxcomplete = 0;

	tse_tx_complete(priv);

	rxcomplete = tse_rx(priv, budget);

	if (rxcomplete < budget) {

		napi_complete_done(napi, rxcomplete);

		netdev_dbg(priv->dev,
			   "NAPI Complete, did %d packets with budget %d\n",
			   rxcomplete, budget);

		spin_lock_irqsave(&priv->rxdma_irq_lock, flags);
		priv->dmaops->enable_rxirq(priv);
		priv->dmaops->enable_txirq(priv);
		spin_unlock_irqrestore(&priv->rxdma_irq_lock, flags);
	}
	return rxcomplete;
}

/* DMA TX & RX FIFO interrupt routing
 */
static irqreturn_t altera_isr(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct altera_tse_private *priv;

	if (unlikely(!dev)) {
		pr_err("%s: invalid dev pointer\n", __func__);
		return IRQ_NONE;
	}
	priv = netdev_priv(dev);

	spin_lock(&priv->rxdma_irq_lock);
	/* reset IRQs */
	priv->dmaops->clear_rxirq(priv);
	priv->dmaops->clear_txirq(priv);
	spin_unlock(&priv->rxdma_irq_lock);

	if (likely(napi_schedule_prep(&priv->napi))) {
		spin_lock(&priv->rxdma_irq_lock);
		priv->dmaops->disable_rxirq(priv);
		priv->dmaops->disable_txirq(priv);
		spin_unlock(&priv->rxdma_irq_lock);
		__napi_schedule(&priv->napi);
	}


	return IRQ_HANDLED;
}

/* Transmit a packet (called by the kernel). Dispatches
 * either the SGDMA method for transmitting or the
 * MSGDMA method, assumes no scatter/gather support,
 * implying an assumption that there's only one
 * physically contiguous fragment starting at
 * skb->data, for length of skb_headlen(skb).
 */
static netdev_tx_t tse_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	unsigned int txsize = priv->tx_ring_size;
	int nfrags = skb_shinfo(skb)->nr_frags;
	struct tse_buffer *buffer = NULL;
	netdev_tx_t ret = NETDEV_TX_OK;
	dma_addr_t dma_addr;
	unsigned int entry;

	spin_lock_bh(&priv->tx_lock);

	if (unlikely(tse_tx_avail(priv) < nfrags + 1)) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);
			/* This is a hard error, log it. */
			netdev_err(priv->dev,
				   "%s: Tx list full when queue awake\n",
				   __func__);
		}
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	/* Map the first skb fragment */
	entry = priv->tx_prod % txsize;
	buffer = &priv->tx_ring[entry];

	dma_addr = dma_map_single(priv->device, skb->data, nopaged_len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, dma_addr)) {
		netdev_err(priv->dev, "%s: DMA mapping error\n", __func__);
		ret = NETDEV_TX_OK;
		goto out;
	}

	buffer->skb = skb;
	buffer->dma_addr = dma_addr;
	buffer->len = nopaged_len;

	priv->dmaops->tx_buffer(priv, buffer);

	skb_tx_timestamp(skb);

	priv->tx_prod++;
	dev->stats.tx_bytes += skb->len;

	if (unlikely(tse_tx_avail(priv) <= TXQUEUESTOP_THRESHHOLD)) {
		if (netif_msg_hw(priv))
			netdev_dbg(priv->dev, "%s: stop transmitted packets\n",
				   __func__);
		netif_stop_queue(dev);
	}

out:
	spin_unlock_bh(&priv->tx_lock);

	return ret;
}

static int altera_tse_phy_get_addr_mdio_create(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	struct device_node *np = priv->device->of_node;
	int ret;

	ret = of_get_phy_mode(np, &priv->phy_iface);

	/* Avoid get phy addr and create mdio if no phy is present */
	if (ret)
		return 0;

	/* try to get PHY address from device tree, use PHY autodetection if
	 * no valid address is given
	 */

	if (of_property_read_u32(priv->device->of_node, "phy-addr",
			 &priv->phy_addr)) {
		priv->phy_addr = POLL_PHY;
	}

	if (!((priv->phy_addr == POLL_PHY) ||
		  ((priv->phy_addr >= 0) && (priv->phy_addr < PHY_MAX_ADDR)))) {
		netdev_err(dev, "invalid phy-addr specified %d\n",
			priv->phy_addr);
		return -ENODEV;
	}

	/* Create/attach to MDIO bus */
	ret = altera_tse_mdio_create(dev,
					 atomic_add_return(1, &instance_count));

	if (ret)
		return -ENODEV;

	return 0;
}

static void tse_update_mac_addr(struct altera_tse_private *priv, const u8 *addr)
{
	u32 msb;
	u32 lsb;

	msb = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	lsb = ((addr[5] << 8) | addr[4]) & 0xffff;

	/* Set primary MAC address */
	csrwr32(msb, priv->mac_dev, tse_csroffs(mac_addr_0));
	csrwr32(lsb, priv->mac_dev, tse_csroffs(mac_addr_1));
}

/* MAC software reset.
 * When reset is triggered, the MAC function completes the current
 * transmission or reception, and subsequently disables the transmit and
 * receive logic, flushes the receive FIFO buffer, and resets the statistics
 * counters.
 */
static int reset_mac(struct altera_tse_private *priv)
{
	int counter;
	u32 dat;

	dat = csrrd32(priv->mac_dev, tse_csroffs(command_config));
	dat &= ~(MAC_CMDCFG_TX_ENA | MAC_CMDCFG_RX_ENA);
	dat |= MAC_CMDCFG_SW_RESET | MAC_CMDCFG_CNT_RESET;
	csrwr32(dat, priv->mac_dev, tse_csroffs(command_config));

	counter = 0;
	while (counter++ < ALTERA_TSE_SW_RESET_WATCHDOG_CNTR) {
		if (tse_bit_is_clear(priv->mac_dev, tse_csroffs(command_config),
				     MAC_CMDCFG_SW_RESET))
			break;
		udelay(1);
	}

	if (counter >= ALTERA_TSE_SW_RESET_WATCHDOG_CNTR) {
		dat = csrrd32(priv->mac_dev, tse_csroffs(command_config));
		dat &= ~MAC_CMDCFG_SW_RESET;
		csrwr32(dat, priv->mac_dev, tse_csroffs(command_config));
		return -1;
	}
	return 0;
}

/* Initialize MAC core registers
*/
static int init_mac(struct altera_tse_private *priv)
{
	unsigned int cmd = 0;
	u32 frm_length;

	/* Setup Rx FIFO */
	csrwr32(priv->rx_fifo_depth - ALTERA_TSE_RX_SECTION_EMPTY,
		priv->mac_dev, tse_csroffs(rx_section_empty));

	csrwr32(ALTERA_TSE_RX_SECTION_FULL, priv->mac_dev,
		tse_csroffs(rx_section_full));

	csrwr32(ALTERA_TSE_RX_ALMOST_EMPTY, priv->mac_dev,
		tse_csroffs(rx_almost_empty));

	csrwr32(ALTERA_TSE_RX_ALMOST_FULL, priv->mac_dev,
		tse_csroffs(rx_almost_full));

	/* Setup Tx FIFO */
	csrwr32(priv->tx_fifo_depth - ALTERA_TSE_TX_SECTION_EMPTY,
		priv->mac_dev, tse_csroffs(tx_section_empty));

	csrwr32(ALTERA_TSE_TX_SECTION_FULL, priv->mac_dev,
		tse_csroffs(tx_section_full));

	csrwr32(ALTERA_TSE_TX_ALMOST_EMPTY, priv->mac_dev,
		tse_csroffs(tx_almost_empty));

	csrwr32(ALTERA_TSE_TX_ALMOST_FULL, priv->mac_dev,
		tse_csroffs(tx_almost_full));

	/* MAC Address Configuration */
	tse_update_mac_addr(priv, priv->dev->dev_addr);

	/* MAC Function Configuration */
	frm_length = ETH_HLEN + priv->dev->mtu + ETH_FCS_LEN;
	csrwr32(frm_length, priv->mac_dev, tse_csroffs(frm_length));

	csrwr32(ALTERA_TSE_TX_IPG_LENGTH, priv->mac_dev,
		tse_csroffs(tx_ipg_length));

	/* Disable RX/TX shift 16 for alignment of all received frames on 16-bit
	 * start address
	 */
	tse_set_bit(priv->mac_dev, tse_csroffs(rx_cmd_stat),
		    ALTERA_TSE_RX_CMD_STAT_RX_SHIFT16);

	tse_clear_bit(priv->mac_dev, tse_csroffs(tx_cmd_stat),
		      ALTERA_TSE_TX_CMD_STAT_TX_SHIFT16 |
		      ALTERA_TSE_TX_CMD_STAT_OMIT_CRC);

	/* Set the MAC options */
	cmd = csrrd32(priv->mac_dev, tse_csroffs(command_config));
	cmd &= ~MAC_CMDCFG_PAD_EN;	/* No padding Removal on Receive */
	cmd &= ~MAC_CMDCFG_CRC_FWD;	/* CRC Removal */
	cmd |= MAC_CMDCFG_RX_ERR_DISC;	/* Automatically discard frames
					 * with CRC errors
					 */
	cmd |= MAC_CMDCFG_CNTL_FRM_ENA;
	cmd &= ~MAC_CMDCFG_TX_ENA;
	cmd &= ~MAC_CMDCFG_RX_ENA;

	/* Default speed and duplex setting, full/100 */
	cmd &= ~MAC_CMDCFG_HD_ENA;
	cmd &= ~MAC_CMDCFG_ETH_SPEED;
	cmd &= ~MAC_CMDCFG_ENA_10;

	csrwr32(cmd, priv->mac_dev, tse_csroffs(command_config));

	csrwr32(ALTERA_TSE_PAUSE_QUANTA, priv->mac_dev,
		tse_csroffs(pause_quanta));

	if (netif_msg_hw(priv))
		dev_dbg(priv->device,
			"MAC post-initialization: CMD_CONFIG = 0x%08x\n", cmd);

	return 0;
}

/* Start/stop MAC transmission logic
 */
static void tse_set_mac(struct altera_tse_private *priv, bool enable)
{
	u32 value = csrrd32(priv->mac_dev, tse_csroffs(command_config));

	if (enable)
		value |= MAC_CMDCFG_TX_ENA | MAC_CMDCFG_RX_ENA;
	else
		value &= ~(MAC_CMDCFG_TX_ENA | MAC_CMDCFG_RX_ENA);

	csrwr32(value, priv->mac_dev, tse_csroffs(command_config));
}

/* Change the MTU
 */
static int tse_change_mtu(struct net_device *dev, int new_mtu)
{
	if (netif_running(dev)) {
		netdev_err(dev, "must be stopped to change its MTU\n");
		return -EBUSY;
	}

	WRITE_ONCE(dev->mtu, new_mtu);
	netdev_update_features(dev);

	return 0;
}

static void altera_tse_set_mcfilter(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int i;

	/* clear the hash filter */
	for (i = 0; i < 64; i++)
		csrwr32(0, priv->mac_dev, tse_csroffs(hash_table) + i * 4);

	netdev_for_each_mc_addr(ha, dev) {
		unsigned int hash = 0;
		int mac_octet;

		for (mac_octet = 5; mac_octet >= 0; mac_octet--) {
			unsigned char xor_bit = 0;
			unsigned char octet = ha->addr[mac_octet];
			unsigned int bitshift;

			for (bitshift = 0; bitshift < 8; bitshift++)
				xor_bit ^= ((octet >> bitshift) & 0x01);

			hash = (hash << 1) | xor_bit;
		}
		csrwr32(1, priv->mac_dev, tse_csroffs(hash_table) + hash * 4);
	}
}


static void altera_tse_set_mcfilterall(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	int i;

	/* set the hash filter */
	for (i = 0; i < 64; i++)
		csrwr32(1, priv->mac_dev, tse_csroffs(hash_table) + i * 4);
}

/* Set or clear the multicast filter for this adapter
 */
static void tse_set_rx_mode_hashfilter(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);

	spin_lock(&priv->mac_cfg_lock);

	if (dev->flags & IFF_PROMISC)
		tse_set_bit(priv->mac_dev, tse_csroffs(command_config),
			    MAC_CMDCFG_PROMIS_EN);

	if (dev->flags & IFF_ALLMULTI)
		altera_tse_set_mcfilterall(dev);
	else
		altera_tse_set_mcfilter(dev);

	spin_unlock(&priv->mac_cfg_lock);
}

/* Set or clear the multicast filter for this adapter
 */
static void tse_set_rx_mode(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);

	spin_lock(&priv->mac_cfg_lock);

	if ((dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI) ||
	    !netdev_mc_empty(dev) || !netdev_uc_empty(dev))
		tse_set_bit(priv->mac_dev, tse_csroffs(command_config),
			    MAC_CMDCFG_PROMIS_EN);
	else
		tse_clear_bit(priv->mac_dev, tse_csroffs(command_config),
			      MAC_CMDCFG_PROMIS_EN);

	spin_unlock(&priv->mac_cfg_lock);
}

/* Open and initialize the interface
 */
static int tse_open(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	unsigned long flags;
	int ret = 0;
	int i;

	/* Reset and configure TSE MAC and probe associated PHY */
	ret = priv->dmaops->init_dma(priv);
	if (ret != 0) {
		netdev_err(dev, "Cannot initialize DMA\n");
		goto phy_error;
	}

	if (netif_msg_ifup(priv))
		netdev_warn(dev, "device MAC address %pM\n",
			    dev->dev_addr);

	if ((priv->revision < 0xd00) || (priv->revision > 0xe00))
		netdev_warn(dev, "TSE revision %x\n", priv->revision);

	spin_lock(&priv->mac_cfg_lock);

	ret = reset_mac(priv);
	/* Note that reset_mac will fail if the clocks are gated by the PHY
	 * due to the PHY being put into isolation or power down mode.
	 * This is not an error if reset fails due to no clock.
	 */
	if (ret)
		netdev_dbg(dev, "Cannot reset MAC core (error: %d)\n", ret);

	ret = init_mac(priv);
	spin_unlock(&priv->mac_cfg_lock);
	if (ret) {
		netdev_err(dev, "Cannot init MAC core (error: %d)\n", ret);
		goto alloc_skbuf_error;
	}

	priv->dmaops->reset_dma(priv);

	/* Create and initialize the TX/RX descriptors chains. */
	priv->rx_ring_size = dma_rx_num;
	priv->tx_ring_size = dma_tx_num;
	ret = alloc_init_skbufs(priv);
	if (ret) {
		netdev_err(dev, "DMA descriptors initialization failed\n");
		goto alloc_skbuf_error;
	}


	/* Register RX interrupt */
	ret = request_irq(priv->rx_irq, altera_isr, IRQF_SHARED,
			  dev->name, dev);
	if (ret) {
		netdev_err(dev, "Unable to register RX interrupt %d\n",
			   priv->rx_irq);
		goto init_error;
	}

	/* Register TX interrupt */
	ret = request_irq(priv->tx_irq, altera_isr, IRQF_SHARED,
			  dev->name, dev);
	if (ret) {
		netdev_err(dev, "Unable to register TX interrupt %d\n",
			   priv->tx_irq);
		goto tx_request_irq_error;
	}

	/* Enable DMA interrupts */
	spin_lock_irqsave(&priv->rxdma_irq_lock, flags);
	priv->dmaops->enable_rxirq(priv);
	priv->dmaops->enable_txirq(priv);

	/* Setup RX descriptor chain */
	for (i = 0; i < priv->rx_ring_size; i++)
		priv->dmaops->add_rx_desc(priv, &priv->rx_ring[i]);

	spin_unlock_irqrestore(&priv->rxdma_irq_lock, flags);

	ret = phylink_of_phy_connect(priv->phylink, priv->device->of_node, 0);
	if (ret) {
		netdev_err(dev, "could not connect phylink (%d)\n", ret);
		goto tx_request_irq_error;
	}
	phylink_start(priv->phylink);

	napi_enable(&priv->napi);
	netif_start_queue(dev);

	priv->dmaops->start_rxdma(priv);

	/* Start MAC Rx/Tx */
	spin_lock(&priv->mac_cfg_lock);
	tse_set_mac(priv, true);
	spin_unlock(&priv->mac_cfg_lock);

	return 0;

tx_request_irq_error:
	free_irq(priv->rx_irq, dev);
init_error:
	free_skbufs(dev);
alloc_skbuf_error:
phy_error:
	return ret;
}

/* Stop TSE MAC interface and put the device in an inactive state
 */
static int tse_shutdown(struct net_device *dev)
{
	struct altera_tse_private *priv = netdev_priv(dev);
	unsigned long int flags;
	int ret;

	phylink_stop(priv->phylink);
	phylink_disconnect_phy(priv->phylink);
	netif_stop_queue(dev);
	napi_disable(&priv->napi);

	/* Disable DMA interrupts */
	spin_lock_irqsave(&priv->rxdma_irq_lock, flags);
	priv->dmaops->disable_rxirq(priv);
	priv->dmaops->disable_txirq(priv);
	spin_unlock_irqrestore(&priv->rxdma_irq_lock, flags);

	/* Free the IRQ lines */
	free_irq(priv->rx_irq, dev);
	free_irq(priv->tx_irq, dev);

	/* disable and reset the MAC, empties fifo */
	spin_lock(&priv->mac_cfg_lock);
	spin_lock(&priv->tx_lock);

	ret = reset_mac(priv);
	/* Note that reset_mac will fail if the clocks are gated by the PHY
	 * due to the PHY being put into isolation or power down mode.
	 * This is not an error if reset fails due to no clock.
	 */
	if (ret)
		netdev_dbg(dev, "Cannot reset MAC core (error: %d)\n", ret);
	priv->dmaops->reset_dma(priv);
	free_skbufs(dev);

	spin_unlock(&priv->tx_lock);
	spin_unlock(&priv->mac_cfg_lock);

	priv->dmaops->uninit_dma(priv);

	return 0;
}

static struct net_device_ops altera_tse_netdev_ops = {
	.ndo_open		= tse_open,
	.ndo_stop		= tse_shutdown,
	.ndo_start_xmit		= tse_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_rx_mode	= tse_set_rx_mode,
	.ndo_change_mtu		= tse_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static void alt_tse_mac_config(struct phylink_config *config, unsigned int mode,
			       const struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct altera_tse_private *priv = netdev_priv(ndev);

	spin_lock(&priv->mac_cfg_lock);
	reset_mac(priv);
	tse_set_mac(priv, true);
	spin_unlock(&priv->mac_cfg_lock);
}

static void alt_tse_mac_link_down(struct phylink_config *config,
				  unsigned int mode, phy_interface_t interface)
{
}

static void alt_tse_mac_link_up(struct phylink_config *config,
				struct phy_device *phy, unsigned int mode,
				phy_interface_t interface, int speed,
				int duplex, bool tx_pause, bool rx_pause)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct altera_tse_private *priv = netdev_priv(ndev);
	u32 ctrl;

	ctrl = csrrd32(priv->mac_dev, tse_csroffs(command_config));
	ctrl &= ~(MAC_CMDCFG_ENA_10 | MAC_CMDCFG_ETH_SPEED | MAC_CMDCFG_HD_ENA);

	if (duplex == DUPLEX_HALF)
		ctrl |= MAC_CMDCFG_HD_ENA;

	if (speed == SPEED_1000)
		ctrl |= MAC_CMDCFG_ETH_SPEED;
	else if (speed == SPEED_10)
		ctrl |= MAC_CMDCFG_ENA_10;

	spin_lock(&priv->mac_cfg_lock);
	csrwr32(ctrl, priv->mac_dev, tse_csroffs(command_config));
	spin_unlock(&priv->mac_cfg_lock);
}

static struct phylink_pcs *alt_tse_select_pcs(struct phylink_config *config,
					      phy_interface_t interface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct altera_tse_private *priv = netdev_priv(ndev);

	if (interface == PHY_INTERFACE_MODE_SGMII ||
	    interface == PHY_INTERFACE_MODE_1000BASEX)
		return priv->pcs;
	else
		return NULL;
}

static const struct phylink_mac_ops alt_tse_phylink_ops = {
	.mac_config = alt_tse_mac_config,
	.mac_link_down = alt_tse_mac_link_down,
	.mac_link_up = alt_tse_mac_link_up,
	.mac_select_pcs = alt_tse_select_pcs,
};

static int request_and_map(struct platform_device *pdev, const char *name,
			   struct resource **res, void __iomem **ptr)
{
	struct device *device = &pdev->dev;
	struct resource *region;

	*res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (*res == NULL) {
		dev_err(device, "resource %s not defined\n", name);
		return -ENODEV;
	}

	region = devm_request_mem_region(device, (*res)->start,
					 resource_size(*res), dev_name(device));
	if (region == NULL) {
		dev_err(device, "unable to request %s\n", name);
		return -EBUSY;
	}

	*ptr = devm_ioremap(device, region->start,
				    resource_size(region));
	if (*ptr == NULL) {
		dev_err(device, "ioremap of %s failed!", name);
		return -ENOMEM;
	}

	return 0;
}

/* Probe Altera TSE MAC device
 */
static int altera_tse_probe(struct platform_device *pdev)
{
	struct regmap_config pcs_regmap_cfg;
	struct altera_tse_private *priv;
	struct mdio_regmap_config mrc;
	struct resource *control_port;
	struct regmap *pcs_regmap;
	struct resource *dma_res;
	struct resource *pcs_res;
	struct mii_bus *pcs_bus;
	struct net_device *ndev;
	void __iomem *descmap;
	int ret = -ENODEV;

	ndev = alloc_etherdev(sizeof(struct altera_tse_private));
	if (!ndev) {
		dev_err(&pdev->dev, "Could not allocate network device\n");
		return -ENODEV;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	priv = netdev_priv(ndev);
	priv->device = &pdev->dev;
	priv->dev = ndev;
	priv->msg_enable = netif_msg_init(debug, default_msg_level);

	priv->dmaops = device_get_match_data(&pdev->dev);

	if (priv->dmaops &&
	    priv->dmaops->altera_dtype == ALTERA_DTYPE_SGDMA) {
		/* Get the mapped address to the SGDMA descriptor memory */
		ret = request_and_map(pdev, "s1", &dma_res, &descmap);
		if (ret)
			goto err_free_netdev;

		/* Start of that memory is for transmit descriptors */
		priv->tx_dma_desc = descmap;

		/* First half is for tx descriptors, other half for tx */
		priv->txdescmem = resource_size(dma_res)/2;

		priv->txdescmem_busaddr = (dma_addr_t)dma_res->start;

		priv->rx_dma_desc = (void __iomem *)((uintptr_t)(descmap +
						     priv->txdescmem));
		priv->rxdescmem = resource_size(dma_res)/2;
		priv->rxdescmem_busaddr = dma_res->start;
		priv->rxdescmem_busaddr += priv->txdescmem;

		if (upper_32_bits(priv->rxdescmem_busaddr)) {
			dev_dbg(priv->device,
				"SGDMA bus addresses greater than 32-bits\n");
			ret = -EINVAL;
			goto err_free_netdev;
		}
		if (upper_32_bits(priv->txdescmem_busaddr)) {
			dev_dbg(priv->device,
				"SGDMA bus addresses greater than 32-bits\n");
			ret = -EINVAL;
			goto err_free_netdev;
		}
	} else if (priv->dmaops &&
		   priv->dmaops->altera_dtype == ALTERA_DTYPE_MSGDMA) {
		ret = request_and_map(pdev, "rx_resp", &dma_res,
				      &priv->rx_dma_resp);
		if (ret)
			goto err_free_netdev;

		ret = request_and_map(pdev, "tx_desc", &dma_res,
				      &priv->tx_dma_desc);
		if (ret)
			goto err_free_netdev;

		priv->txdescmem = resource_size(dma_res);
		priv->txdescmem_busaddr = dma_res->start;

		ret = request_and_map(pdev, "rx_desc", &dma_res,
				      &priv->rx_dma_desc);
		if (ret)
			goto err_free_netdev;

		priv->rxdescmem = resource_size(dma_res);
		priv->rxdescmem_busaddr = dma_res->start;

	} else {
		ret = -ENODEV;
		goto err_free_netdev;
	}

	if (!dma_set_mask(priv->device, DMA_BIT_MASK(priv->dmaops->dmamask))) {
		dma_set_coherent_mask(priv->device,
				      DMA_BIT_MASK(priv->dmaops->dmamask));
	} else if (!dma_set_mask(priv->device, DMA_BIT_MASK(32))) {
		dma_set_coherent_mask(priv->device, DMA_BIT_MASK(32));
	} else {
		ret = -EIO;
		goto err_free_netdev;
	}

	/* MAC address space */
	ret = request_and_map(pdev, "control_port", &control_port,
			      (void __iomem **)&priv->mac_dev);
	if (ret)
		goto err_free_netdev;

	/* xSGDMA Rx Dispatcher address space */
	ret = request_and_map(pdev, "rx_csr", &dma_res,
			      &priv->rx_dma_csr);
	if (ret)
		goto err_free_netdev;


	/* xSGDMA Tx Dispatcher address space */
	ret = request_and_map(pdev, "tx_csr", &dma_res,
			      &priv->tx_dma_csr);
	if (ret)
		goto err_free_netdev;

	memset(&pcs_regmap_cfg, 0, sizeof(pcs_regmap_cfg));
	memset(&mrc, 0, sizeof(mrc));
	/* SGMII PCS address space. The location can vary depending on how the
	 * IP is integrated. We can have a resource dedicated to it at a specific
	 * address space, but if it's not the case, we fallback to the mdiophy0
	 * from the MAC's address space
	 */
	ret = request_and_map(pdev, "pcs", &pcs_res, &priv->pcs_base);
	if (ret) {
		/* If we can't find a dedicated resource for the PCS, fallback
		 * to the internal PCS, that has a different address stride
		 */
		priv->pcs_base = priv->mac_dev + tse_csroffs(mdio_phy0);
		pcs_regmap_cfg.reg_bits = 32;
		/* Values are MDIO-like values, on 16 bits */
		pcs_regmap_cfg.val_bits = 16;
		pcs_regmap_cfg.reg_shift = REGMAP_UPSHIFT(2);
	} else {
		pcs_regmap_cfg.reg_bits = 16;
		pcs_regmap_cfg.val_bits = 16;
		pcs_regmap_cfg.reg_shift = REGMAP_UPSHIFT(1);
	}

	/* Create a regmap for the PCS so that it can be used by the PCS driver */
	pcs_regmap = devm_regmap_init_mmio(&pdev->dev, priv->pcs_base,
					   &pcs_regmap_cfg);
	if (IS_ERR(pcs_regmap)) {
		ret = PTR_ERR(pcs_regmap);
		goto err_free_netdev;
	}
	mrc.regmap = pcs_regmap;
	mrc.parent = &pdev->dev;
	mrc.valid_addr = 0x0;
	mrc.autoscan = false;

	/* Rx IRQ */
	priv->rx_irq = platform_get_irq_byname(pdev, "rx_irq");
	if (priv->rx_irq == -ENXIO) {
		dev_err(&pdev->dev, "cannot obtain Rx IRQ\n");
		ret = -ENXIO;
		goto err_free_netdev;
	}

	/* Tx IRQ */
	priv->tx_irq = platform_get_irq_byname(pdev, "tx_irq");
	if (priv->tx_irq == -ENXIO) {
		dev_err(&pdev->dev, "cannot obtain Tx IRQ\n");
		ret = -ENXIO;
		goto err_free_netdev;
	}

	/* get FIFO depths from device tree */
	if (of_property_read_u32(pdev->dev.of_node, "rx-fifo-depth",
				 &priv->rx_fifo_depth)) {
		dev_err(&pdev->dev, "cannot obtain rx-fifo-depth\n");
		ret = -ENXIO;
		goto err_free_netdev;
	}

	if (of_property_read_u32(pdev->dev.of_node, "tx-fifo-depth",
				 &priv->tx_fifo_depth)) {
		dev_err(&pdev->dev, "cannot obtain tx-fifo-depth\n");
		ret = -ENXIO;
		goto err_free_netdev;
	}

	/* get hash filter settings for this instance */
	priv->hash_filter =
		of_property_read_bool(pdev->dev.of_node,
				      "altr,has-hash-multicast-filter");

	/* Set hash filter to not set for now until the
	 * multicast filter receive issue is debugged
	 */
	priv->hash_filter = 0;

	/* get supplemental address settings for this instance */
	priv->added_unicast =
		of_property_read_bool(pdev->dev.of_node,
				      "altr,has-supplementary-unicast");

	priv->dev->min_mtu = ETH_ZLEN + ETH_FCS_LEN;
	/* Max MTU is 1500, ETH_DATA_LEN */
	priv->dev->max_mtu = ETH_DATA_LEN;

	/* Get the max mtu from the device tree. Note that the
	 * "max-frame-size" parameter is actually max mtu. Definition
	 * in the ePAPR v1.1 spec and usage differ, so go with usage.
	 */
	of_property_read_u32(pdev->dev.of_node, "max-frame-size",
			     &priv->dev->max_mtu);

	/* The DMA buffer size already accounts for an alignment bias
	 * to avoid unaligned access exceptions for the NIOS processor,
	 */
	priv->rx_dma_buf_sz = ALTERA_RXDMABUFFER_SIZE;

	/* get default MAC address from device tree */
	ret = of_get_ethdev_address(pdev->dev.of_node, ndev);
	if (ret)
		eth_hw_addr_random(ndev);

	/* get phy addr and create mdio */
	ret = altera_tse_phy_get_addr_mdio_create(ndev);

	if (ret)
		goto err_free_netdev;

	/* initialize netdev */
	ndev->mem_start = control_port->start;
	ndev->mem_end = control_port->end;
	ndev->netdev_ops = &altera_tse_netdev_ops;
	altera_tse_set_ethtool_ops(ndev);

	altera_tse_netdev_ops.ndo_set_rx_mode = tse_set_rx_mode;

	if (priv->hash_filter)
		altera_tse_netdev_ops.ndo_set_rx_mode =
			tse_set_rx_mode_hashfilter;

	/* Scatter/gather IO is not supported,
	 * so it is turned off
	 */
	ndev->hw_features &= ~NETIF_F_SG;
	ndev->features |= ndev->hw_features | NETIF_F_HIGHDMA;

	/* VLAN offloading of tagging, stripping and filtering is not
	 * supported by hardware, but driver will accommodate the
	 * extra 4-byte VLAN tag for processing by upper layers
	 */
	ndev->features |= NETIF_F_HW_VLAN_CTAG_RX;

	/* setup NAPI interface */
	netif_napi_add(ndev, &priv->napi, tse_poll);

	spin_lock_init(&priv->mac_cfg_lock);
	spin_lock_init(&priv->tx_lock);
	spin_lock_init(&priv->rxdma_irq_lock);

	netif_carrier_off(ndev);
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register TSE net device\n");
		goto err_register_netdev;
	}

	platform_set_drvdata(pdev, ndev);

	priv->revision = ioread32(&priv->mac_dev->megacore_revision);

	if (netif_msg_probe(priv))
		dev_info(&pdev->dev, "Altera TSE MAC version %d.%d at 0x%08lx irq %d/%d\n",
			 (priv->revision >> 8) & 0xff,
			 priv->revision & 0xff,
			 (unsigned long) control_port->start, priv->rx_irq,
			 priv->tx_irq);

	snprintf(mrc.name, MII_BUS_ID_SIZE, "%s-pcs-mii", ndev->name);
	pcs_bus = devm_mdio_regmap_register(&pdev->dev, &mrc);
	if (IS_ERR(pcs_bus)) {
		ret = PTR_ERR(pcs_bus);
		goto err_init_pcs;
	}

	priv->pcs = lynx_pcs_create_mdiodev(pcs_bus, 0);
	if (IS_ERR(priv->pcs)) {
		ret = PTR_ERR(priv->pcs);
		goto err_init_pcs;
	}

	priv->phylink_config.dev = &ndev->dev;
	priv->phylink_config.type = PHYLINK_NETDEV;
	priv->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_10 |
						MAC_100 | MAC_1000FD;

	phy_interface_set_rgmii(priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_MII,
		  priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_GMII,
		  priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  priv->phylink_config.supported_interfaces);

	priv->phylink = phylink_create(&priv->phylink_config,
				       of_fwnode_handle(priv->device->of_node),
				       priv->phy_iface, &alt_tse_phylink_ops);
	if (IS_ERR(priv->phylink)) {
		dev_err(&pdev->dev, "failed to create phylink\n");
		ret = PTR_ERR(priv->phylink);
		goto err_init_phylink;
	}

	return 0;
err_init_phylink:
	lynx_pcs_destroy(priv->pcs);
err_init_pcs:
	unregister_netdev(ndev);
err_register_netdev:
	netif_napi_del(&priv->napi);
	altera_tse_mdio_destroy(ndev);
err_free_netdev:
	free_netdev(ndev);
	return ret;
}

/* Remove Altera TSE MAC device
 */
static void altera_tse_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct altera_tse_private *priv = netdev_priv(ndev);

	platform_set_drvdata(pdev, NULL);
	altera_tse_mdio_destroy(ndev);
	unregister_netdev(ndev);
	phylink_destroy(priv->phylink);
	lynx_pcs_destroy(priv->pcs);

	free_netdev(ndev);
}

static const struct altera_dmaops altera_dtype_sgdma = {
	.altera_dtype = ALTERA_DTYPE_SGDMA,
	.dmamask = 32,
	.reset_dma = sgdma_reset,
	.enable_txirq = sgdma_enable_txirq,
	.enable_rxirq = sgdma_enable_rxirq,
	.disable_txirq = sgdma_disable_txirq,
	.disable_rxirq = sgdma_disable_rxirq,
	.clear_txirq = sgdma_clear_txirq,
	.clear_rxirq = sgdma_clear_rxirq,
	.tx_buffer = sgdma_tx_buffer,
	.tx_completions = sgdma_tx_completions,
	.add_rx_desc = sgdma_add_rx_desc,
	.get_rx_status = sgdma_rx_status,
	.init_dma = sgdma_initialize,
	.uninit_dma = sgdma_uninitialize,
	.start_rxdma = sgdma_start_rxdma,
};

static const struct altera_dmaops altera_dtype_msgdma = {
	.altera_dtype = ALTERA_DTYPE_MSGDMA,
	.dmamask = 64,
	.reset_dma = msgdma_reset,
	.enable_txirq = msgdma_enable_txirq,
	.enable_rxirq = msgdma_enable_rxirq,
	.disable_txirq = msgdma_disable_txirq,
	.disable_rxirq = msgdma_disable_rxirq,
	.clear_txirq = msgdma_clear_txirq,
	.clear_rxirq = msgdma_clear_rxirq,
	.tx_buffer = msgdma_tx_buffer,
	.tx_completions = msgdma_tx_completions,
	.add_rx_desc = msgdma_add_rx_desc,
	.get_rx_status = msgdma_rx_status,
	.init_dma = msgdma_initialize,
	.uninit_dma = msgdma_uninitialize,
	.start_rxdma = msgdma_start_rxdma,
};

static const struct of_device_id altera_tse_ids[] = {
	{ .compatible = "altr,tse-msgdma-1.0", .data = &altera_dtype_msgdma, },
	{ .compatible = "altr,tse-1.0", .data = &altera_dtype_sgdma, },
	{ .compatible = "ALTR,tse-1.0", .data = &altera_dtype_sgdma, },
	{},
};
MODULE_DEVICE_TABLE(of, altera_tse_ids);

static struct platform_driver altera_tse_driver = {
	.probe		= altera_tse_probe,
	.remove		= altera_tse_remove,
	.suspend	= NULL,
	.resume		= NULL,
	.driver		= {
		.name	= ALTERA_TSE_RESOURCE_NAME,
		.of_match_table = altera_tse_ids,
	},
};

module_platform_driver(altera_tse_driver);

MODULE_AUTHOR("Altera Corporation");
MODULE_DESCRIPTION("Altera Triple Speed Ethernet MAC driver");
MODULE_LICENSE("GPL v2");

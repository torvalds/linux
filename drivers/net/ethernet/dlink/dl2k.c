// SPDX-License-Identifier: GPL-2.0-or-later
/*  D-Link DL2000-based Gigabit Ethernet Adapter Linux driver */
/*
    Copyright (c) 2001, 2002 by D-Link Corporation
    Written by Edward Peng.<edward_peng@dlink.com.tw>
    Created 03-May-2001, base on Linux' sundance.c.

*/

#include "dl2k.h"
#include <linux/dma-mapping.h>

#define dw32(reg, val)	iowrite32(val, ioaddr + (reg))
#define dw16(reg, val)	iowrite16(val, ioaddr + (reg))
#define dw8(reg, val)	iowrite8(val, ioaddr + (reg))
#define dr32(reg)	ioread32(ioaddr + (reg))
#define dr16(reg)	ioread16(ioaddr + (reg))
#define dr8(reg)	ioread8(ioaddr + (reg))

#define MAX_UNITS 8
static int mtu[MAX_UNITS];
static int vlan[MAX_UNITS];
static int jumbo[MAX_UNITS];
static char *media[MAX_UNITS];
static int tx_flow=-1;
static int rx_flow=-1;
static int copy_thresh;
static int rx_coalesce=10;	/* Rx frame count each interrupt */
static int rx_timeout=200;	/* Rx DMA wait time in 640ns increments */
static int tx_coalesce=16;	/* HW xmit count each TxDMAComplete */


MODULE_AUTHOR ("Edward Peng");
MODULE_DESCRIPTION ("D-Link DL2000-based Gigabit Ethernet Adapter");
MODULE_LICENSE("GPL");
module_param_array(mtu, int, NULL, 0);
module_param_array(media, charp, NULL, 0);
module_param_array(vlan, int, NULL, 0);
module_param_array(jumbo, int, NULL, 0);
module_param(tx_flow, int, 0);
module_param(rx_flow, int, 0);
module_param(copy_thresh, int, 0);
module_param(rx_coalesce, int, 0);	/* Rx frame count each interrupt */
module_param(rx_timeout, int, 0);	/* Rx DMA wait time in 64ns increments */
module_param(tx_coalesce, int, 0); /* HW xmit count each TxDMAComplete */


/* Enable the default interrupts */
#define DEFAULT_INTR (RxDMAComplete | HostError | IntRequested | TxDMAComplete| \
       UpdateStats | LinkEvent)

static void dl2k_enable_int(struct netdev_private *np)
{
	void __iomem *ioaddr = np->ioaddr;

	dw16(IntEnable, DEFAULT_INTR);
}

static const int max_intrloop = 50;
static const int multicast_filter_limit = 0x40;

static int rio_open (struct net_device *dev);
static void rio_timer (struct timer_list *t);
static void rio_tx_timeout (struct net_device *dev, unsigned int txqueue);
static netdev_tx_t start_xmit (struct sk_buff *skb, struct net_device *dev);
static irqreturn_t rio_interrupt (int irq, void *dev_instance);
static void rio_free_tx (struct net_device *dev, int irq);
static void tx_error (struct net_device *dev, int tx_status);
static int receive_packet (struct net_device *dev);
static void rio_error (struct net_device *dev, int int_status);
static void set_multicast (struct net_device *dev);
static struct net_device_stats *get_stats (struct net_device *dev);
static int clear_stats (struct net_device *dev);
static int rio_ioctl (struct net_device *dev, struct ifreq *rq, int cmd);
static int rio_close (struct net_device *dev);
static int find_miiphy (struct net_device *dev);
static int parse_eeprom (struct net_device *dev);
static int read_eeprom (struct netdev_private *, int eep_addr);
static int mii_wait_link (struct net_device *dev, int wait);
static int mii_set_media (struct net_device *dev);
static int mii_get_media (struct net_device *dev);
static int mii_set_media_pcs (struct net_device *dev);
static int mii_get_media_pcs (struct net_device *dev);
static int mii_read (struct net_device *dev, int phy_addr, int reg_num);
static int mii_write (struct net_device *dev, int phy_addr, int reg_num,
		      u16 data);

static const struct ethtool_ops ethtool_ops;

static const struct net_device_ops netdev_ops = {
	.ndo_open		= rio_open,
	.ndo_start_xmit	= start_xmit,
	.ndo_stop		= rio_close,
	.ndo_get_stats		= get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_set_rx_mode	= set_multicast,
	.ndo_eth_ioctl		= rio_ioctl,
	.ndo_tx_timeout		= rio_tx_timeout,
};

static int
rio_probe1 (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct netdev_private *np;
	static int card_idx;
	int chip_idx = ent->driver_data;
	int err, irq;
	void __iomem *ioaddr;
	void *ring_space;
	dma_addr_t ring_dma;

	err = pci_enable_device (pdev);
	if (err)
		return err;

	irq = pdev->irq;
	err = pci_request_regions (pdev, "dl2k");
	if (err)
		goto err_out_disable;

	pci_set_master (pdev);

	err = -ENOMEM;

	dev = alloc_etherdev (sizeof (*np));
	if (!dev)
		goto err_out_res;
	SET_NETDEV_DEV(dev, &pdev->dev);

	np = netdev_priv(dev);

	/* IO registers range. */
	ioaddr = pci_iomap(pdev, 0, 0);
	if (!ioaddr)
		goto err_out_dev;
	np->eeprom_addr = ioaddr;

#ifdef MEM_MAPPING
	/* MM registers range. */
	ioaddr = pci_iomap(pdev, 1, 0);
	if (!ioaddr)
		goto err_out_iounmap;
#endif
	np->ioaddr = ioaddr;
	np->chip_id = chip_idx;
	np->pdev = pdev;
	spin_lock_init (&np->tx_lock);
	spin_lock_init (&np->rx_lock);

	/* Parse manual configuration */
	np->an_enable = 1;
	np->tx_coalesce = 1;
	if (card_idx < MAX_UNITS) {
		if (media[card_idx] != NULL) {
			np->an_enable = 0;
			if (strcmp (media[card_idx], "auto") == 0 ||
			    strcmp (media[card_idx], "autosense") == 0 ||
			    strcmp (media[card_idx], "0") == 0 ) {
				np->an_enable = 2;
			} else if (strcmp (media[card_idx], "100mbps_fd") == 0 ||
			    strcmp (media[card_idx], "4") == 0) {
				np->speed = 100;
				np->full_duplex = 1;
			} else if (strcmp (media[card_idx], "100mbps_hd") == 0 ||
				   strcmp (media[card_idx], "3") == 0) {
				np->speed = 100;
				np->full_duplex = 0;
			} else if (strcmp (media[card_idx], "10mbps_fd") == 0 ||
				   strcmp (media[card_idx], "2") == 0) {
				np->speed = 10;
				np->full_duplex = 1;
			} else if (strcmp (media[card_idx], "10mbps_hd") == 0 ||
				   strcmp (media[card_idx], "1") == 0) {
				np->speed = 10;
				np->full_duplex = 0;
			} else if (strcmp (media[card_idx], "1000mbps_fd") == 0 ||
				 strcmp (media[card_idx], "6") == 0) {
				np->speed=1000;
				np->full_duplex=1;
			} else if (strcmp (media[card_idx], "1000mbps_hd") == 0 ||
				 strcmp (media[card_idx], "5") == 0) {
				np->speed = 1000;
				np->full_duplex = 0;
			} else {
				np->an_enable = 1;
			}
		}
		if (jumbo[card_idx] != 0) {
			np->jumbo = 1;
			dev->mtu = MAX_JUMBO;
		} else {
			np->jumbo = 0;
			if (mtu[card_idx] > 0 && mtu[card_idx] < PACKET_SIZE)
				dev->mtu = mtu[card_idx];
		}
		np->vlan = (vlan[card_idx] > 0 && vlan[card_idx] < 4096) ?
		    vlan[card_idx] : 0;
		if (rx_coalesce > 0 && rx_timeout > 0) {
			np->rx_coalesce = rx_coalesce;
			np->rx_timeout = rx_timeout;
			np->coalesce = 1;
		}
		np->tx_flow = (tx_flow == 0) ? 0 : 1;
		np->rx_flow = (rx_flow == 0) ? 0 : 1;

		if (tx_coalesce < 1)
			tx_coalesce = 1;
		else if (tx_coalesce > TX_RING_SIZE-1)
			tx_coalesce = TX_RING_SIZE - 1;
	}
	dev->netdev_ops = &netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->ethtool_ops = &ethtool_ops;
#if 0
	dev->features = NETIF_F_IP_CSUM;
#endif
	/* MTU range: 68 - 1536 or 8000 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = np->jumbo ? MAX_JUMBO : PACKET_SIZE;

	pci_set_drvdata (pdev, dev);

	ring_space = dma_alloc_coherent(&pdev->dev, TX_TOTAL_SIZE, &ring_dma,
					GFP_KERNEL);
	if (!ring_space)
		goto err_out_iounmap;
	np->tx_ring = ring_space;
	np->tx_ring_dma = ring_dma;

	ring_space = dma_alloc_coherent(&pdev->dev, RX_TOTAL_SIZE, &ring_dma,
					GFP_KERNEL);
	if (!ring_space)
		goto err_out_unmap_tx;
	np->rx_ring = ring_space;
	np->rx_ring_dma = ring_dma;

	/* Parse eeprom data */
	parse_eeprom (dev);

	/* Find PHY address */
	err = find_miiphy (dev);
	if (err)
		goto err_out_unmap_rx;

	/* Fiber device? */
	np->phy_media = (dr16(ASICCtrl) & PhyMedia) ? 1 : 0;
	np->link_status = 0;
	/* Set media and reset PHY */
	if (np->phy_media) {
		/* default Auto-Negotiation for fiber deivices */
	 	if (np->an_enable == 2) {
			np->an_enable = 1;
		}
	} else {
		/* Auto-Negotiation is mandatory for 1000BASE-T,
		   IEEE 802.3ab Annex 28D page 14 */
		if (np->speed == 1000)
			np->an_enable = 1;
	}

	err = register_netdev (dev);
	if (err)
		goto err_out_unmap_rx;

	card_idx++;

	printk (KERN_INFO "%s: %s, %pM, IRQ %d\n",
		dev->name, np->name, dev->dev_addr, irq);
	if (tx_coalesce > 1)
		printk(KERN_INFO "tx_coalesce:\t%d packets\n",
				tx_coalesce);
	if (np->coalesce)
		printk(KERN_INFO
		       "rx_coalesce:\t%d packets\n"
		       "rx_timeout: \t%d ns\n",
				np->rx_coalesce, np->rx_timeout*640);
	if (np->vlan)
		printk(KERN_INFO "vlan(id):\t%d\n", np->vlan);
	return 0;

err_out_unmap_rx:
	dma_free_coherent(&pdev->dev, RX_TOTAL_SIZE, np->rx_ring,
			  np->rx_ring_dma);
err_out_unmap_tx:
	dma_free_coherent(&pdev->dev, TX_TOTAL_SIZE, np->tx_ring,
			  np->tx_ring_dma);
err_out_iounmap:
#ifdef MEM_MAPPING
	pci_iounmap(pdev, np->ioaddr);
#endif
	pci_iounmap(pdev, np->eeprom_addr);
err_out_dev:
	free_netdev (dev);
err_out_res:
	pci_release_regions (pdev);
err_out_disable:
	pci_disable_device (pdev);
	return err;
}

static int
find_miiphy (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	int i, phy_found = 0;

	np->phy_addr = 1;

	for (i = 31; i >= 0; i--) {
		int mii_status = mii_read (dev, i, 1);
		if (mii_status != 0xffff && mii_status != 0x0000) {
			np->phy_addr = i;
			phy_found++;
		}
	}
	if (!phy_found) {
		printk (KERN_ERR "%s: No MII PHY found!\n", dev->name);
		return -ENODEV;
	}
	return 0;
}

static int
parse_eeprom (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	int i, j;
	u8 sromdata[256];
	u8 *psib;
	u32 crc;
	PSROM_t psrom = (PSROM_t) sromdata;

	int cid, next;

	for (i = 0; i < 128; i++)
		((__le16 *) sromdata)[i] = cpu_to_le16(read_eeprom(np, i));

	if (np->pdev->vendor == PCI_VENDOR_ID_DLINK) {	/* D-Link Only */
		/* Check CRC */
		crc = ~ether_crc_le (256 - 4, sromdata);
		if (psrom->crc != cpu_to_le32(crc)) {
			printk (KERN_ERR "%s: EEPROM data CRC error.\n",
					dev->name);
			return -1;
		}
	}

	/* Set MAC address */
	eth_hw_addr_set(dev, psrom->mac_addr);

	if (np->chip_id == CHIP_IP1000A) {
		np->led_mode = psrom->led_mode;
		return 0;
	}

	if (np->pdev->vendor != PCI_VENDOR_ID_DLINK) {
		return 0;
	}

	/* Parse Software Information Block */
	i = 0x30;
	psib = (u8 *) sromdata;
	do {
		cid = psib[i++];
		next = psib[i++];
		if ((cid == 0 && next == 0) || (cid == 0xff && next == 0xff)) {
			printk (KERN_ERR "Cell data error\n");
			return -1;
		}
		switch (cid) {
		case 0:	/* Format version */
			break;
		case 1:	/* End of cell */
			return 0;
		case 2:	/* Duplex Polarity */
			np->duplex_polarity = psib[i];
			dw8(PhyCtrl, dr8(PhyCtrl) | psib[i]);
			break;
		case 3:	/* Wake Polarity */
			np->wake_polarity = psib[i];
			break;
		case 9:	/* Adapter description */
			j = (next - i > 255) ? 255 : next - i;
			memcpy (np->name, &(psib[i]), j);
			break;
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:	/* Reversed */
			break;
		default:	/* Unknown cell */
			return -1;
		}
		i = next;
	} while (1);

	return 0;
}

static void rio_set_led_mode(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	u32 mode;

	if (np->chip_id != CHIP_IP1000A)
		return;

	mode = dr32(ASICCtrl);
	mode &= ~(IPG_AC_LED_MODE_BIT_1 | IPG_AC_LED_MODE | IPG_AC_LED_SPEED);

	if (np->led_mode & 0x01)
		mode |= IPG_AC_LED_MODE;
	if (np->led_mode & 0x02)
		mode |= IPG_AC_LED_MODE_BIT_1;
	if (np->led_mode & 0x08)
		mode |= IPG_AC_LED_SPEED;

	dw32(ASICCtrl, mode);
}

static inline dma_addr_t desc_to_dma(struct netdev_desc *desc)
{
	return le64_to_cpu(desc->fraginfo) & DMA_BIT_MASK(48);
}

static void free_list(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	struct sk_buff *skb;
	int i;

	/* Free all the skbuffs in the queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = np->rx_skbuff[i];
		if (skb) {
			dma_unmap_single(&np->pdev->dev,
					 desc_to_dma(&np->rx_ring[i]),
					 skb->len, DMA_FROM_DEVICE);
			dev_kfree_skb(skb);
			np->rx_skbuff[i] = NULL;
		}
		np->rx_ring[i].status = 0;
		np->rx_ring[i].fraginfo = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		skb = np->tx_skbuff[i];
		if (skb) {
			dma_unmap_single(&np->pdev->dev,
					 desc_to_dma(&np->tx_ring[i]),
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb(skb);
			np->tx_skbuff[i] = NULL;
		}
	}
}

static void rio_reset_ring(struct netdev_private *np)
{
	int i;

	np->cur_rx = 0;
	np->cur_tx = 0;
	np->old_rx = 0;
	np->old_tx = 0;

	for (i = 0; i < TX_RING_SIZE; i++)
		np->tx_ring[i].status = cpu_to_le64(TFDDone);

	for (i = 0; i < RX_RING_SIZE; i++)
		np->rx_ring[i].status = 0;
}

 /* allocate and initialize Tx and Rx descriptors */
static int alloc_list(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	int i;

	rio_reset_ring(np);
	np->rx_buf_sz = (dev->mtu <= 1500 ? PACKET_SIZE : dev->mtu + 32);

	/* Initialize Tx descriptors, TFDListPtr leaves in start_xmit(). */
	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = NULL;
		np->tx_ring[i].next_desc = cpu_to_le64(np->tx_ring_dma +
					      ((i + 1) % TX_RING_SIZE) *
					      sizeof(struct netdev_desc));
	}

	/* Initialize Rx descriptors & allocate buffers */
	for (i = 0; i < RX_RING_SIZE; i++) {
		/* Allocated fixed size of skbuff */
		struct sk_buff *skb;

		skb = netdev_alloc_skb_ip_align(dev, np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (!skb) {
			free_list(dev);
			return -ENOMEM;
		}

		np->rx_ring[i].next_desc = cpu_to_le64(np->rx_ring_dma +
						((i + 1) % RX_RING_SIZE) *
						sizeof(struct netdev_desc));
		/* Rubicon now supports 40 bits of addressing space. */
		np->rx_ring[i].fraginfo =
		    cpu_to_le64(dma_map_single(&np->pdev->dev, skb->data,
					       np->rx_buf_sz, DMA_FROM_DEVICE));
		np->rx_ring[i].fraginfo |= cpu_to_le64((u64)np->rx_buf_sz << 48);
	}

	return 0;
}

static void rio_hw_init(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	int i;
	u16 macctrl;

	/* Reset all logic functions */
	dw16(ASICCtrl + 2,
	     GlobalReset | DMAReset | FIFOReset | NetworkReset | HostReset);
	mdelay(10);

	rio_set_led_mode(dev);

	/* DebugCtrl bit 4, 5, 9 must set */
	dw32(DebugCtrl, dr32(DebugCtrl) | 0x0230);

	if (np->chip_id == CHIP_IP1000A &&
	    (np->pdev->revision == 0x40 || np->pdev->revision == 0x41)) {
		/* PHY magic taken from ipg driver, undocumented registers */
		mii_write(dev, np->phy_addr, 31, 0x0001);
		mii_write(dev, np->phy_addr, 27, 0x01e0);
		mii_write(dev, np->phy_addr, 31, 0x0002);
		mii_write(dev, np->phy_addr, 27, 0xeb8e);
		mii_write(dev, np->phy_addr, 31, 0x0000);
		mii_write(dev, np->phy_addr, 30, 0x005e);
		/* advertise 1000BASE-T half & full duplex, prefer MASTER */
		mii_write(dev, np->phy_addr, MII_CTRL1000, 0x0700);
	}

	if (np->phy_media)
		mii_set_media_pcs(dev);
	else
		mii_set_media(dev);

	/* Jumbo frame */
	if (np->jumbo != 0)
		dw16(MaxFrameSize, MAX_JUMBO+14);

	/* Set RFDListPtr */
	dw32(RFDListPtr0, np->rx_ring_dma);
	dw32(RFDListPtr1, 0);

	/* Set station address */
	/* 16 or 32-bit access is required by TC9020 datasheet but 8-bit works
	 * too. However, it doesn't work on IP1000A so we use 16-bit access.
	 */
	for (i = 0; i < 3; i++)
		dw16(StationAddr0 + 2 * i, get_unaligned_le16(&dev->dev_addr[2 * i]));

	set_multicast (dev);
	if (np->coalesce) {
		dw32(RxDMAIntCtrl, np->rx_coalesce | np->rx_timeout << 16);
	}
	/* Set RIO to poll every N*320nsec. */
	dw8(RxDMAPollPeriod, 0x20);
	dw8(TxDMAPollPeriod, 0xff);
	dw8(RxDMABurstThresh, 0x30);
	dw8(RxDMAUrgentThresh, 0x30);
	dw32(RmonStatMask, 0x0007ffff);
	/* clear statistics */
	clear_stats (dev);

	/* VLAN supported */
	if (np->vlan) {
		/* priority field in RxDMAIntCtrl  */
		dw32(RxDMAIntCtrl, dr32(RxDMAIntCtrl) | 0x7 << 10);
		/* VLANId */
		dw16(VLANId, np->vlan);
		/* Length/Type should be 0x8100 */
		dw32(VLANTag, 0x8100 << 16 | np->vlan);
		/* Enable AutoVLANuntagging, but disable AutoVLANtagging.
		   VLAN information tagged by TFC' VID, CFI fields. */
		dw32(MACCtrl, dr32(MACCtrl) | AutoVLANuntagging);
	}

	/* Start Tx/Rx */
	dw32(MACCtrl, dr32(MACCtrl) | StatsEnable | RxEnable | TxEnable);

	macctrl = 0;
	macctrl |= (np->vlan) ? AutoVLANuntagging : 0;
	macctrl |= (np->full_duplex) ? DuplexSelect : 0;
	macctrl |= (np->tx_flow) ? TxFlowControlEnable : 0;
	macctrl |= (np->rx_flow) ? RxFlowControlEnable : 0;
	dw16(MACCtrl, macctrl);
}

static void rio_hw_stop(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;

	/* Disable interrupts */
	dw16(IntEnable, 0);

	/* Stop Tx and Rx logics */
	dw32(MACCtrl, TxDisable | RxDisable | StatsDisable);
}

static int rio_open(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	const int irq = np->pdev->irq;
	int i;

	i = alloc_list(dev);
	if (i)
		return i;

	rio_hw_init(dev);

	i = request_irq(irq, rio_interrupt, IRQF_SHARED, dev->name, dev);
	if (i) {
		rio_hw_stop(dev);
		free_list(dev);
		return i;
	}

	timer_setup(&np->timer, rio_timer, 0);
	np->timer.expires = jiffies + 1 * HZ;
	add_timer(&np->timer);

	netif_start_queue (dev);

	dl2k_enable_int(np);
	return 0;
}

static void
rio_timer (struct timer_list *t)
{
	struct netdev_private *np = from_timer(np, t, timer);
	struct net_device *dev = pci_get_drvdata(np->pdev);
	unsigned int entry;
	int next_tick = 1*HZ;
	unsigned long flags;

	spin_lock_irqsave(&np->rx_lock, flags);
	/* Recover rx ring exhausted error */
	if (np->cur_rx - np->old_rx >= RX_RING_SIZE) {
		printk(KERN_INFO "Try to recover rx ring exhausted...\n");
		/* Re-allocate skbuffs to fill the descriptor ring */
		for (; np->cur_rx - np->old_rx > 0; np->old_rx++) {
			struct sk_buff *skb;
			entry = np->old_rx % RX_RING_SIZE;
			/* Dropped packets don't need to re-allocate */
			if (np->rx_skbuff[entry] == NULL) {
				skb = netdev_alloc_skb_ip_align(dev,
								np->rx_buf_sz);
				if (skb == NULL) {
					np->rx_ring[entry].fraginfo = 0;
					printk (KERN_INFO
						"%s: Still unable to re-allocate Rx skbuff.#%d\n",
						dev->name, entry);
					break;
				}
				np->rx_skbuff[entry] = skb;
				np->rx_ring[entry].fraginfo =
				    cpu_to_le64 (dma_map_single(&np->pdev->dev, skb->data,
								np->rx_buf_sz, DMA_FROM_DEVICE));
			}
			np->rx_ring[entry].fraginfo |=
			    cpu_to_le64((u64)np->rx_buf_sz << 48);
			np->rx_ring[entry].status = 0;
		} /* end for */
	} /* end if */
	spin_unlock_irqrestore (&np->rx_lock, flags);
	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void
rio_tx_timeout (struct net_device *dev, unsigned int txqueue)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;

	printk (KERN_INFO "%s: Tx timed out (%4.4x), is buffer full?\n",
		dev->name, dr32(TxStatus));
	rio_free_tx(dev, 0);
	dev->if_port = 0;
	netif_trans_update(dev); /* prevent tx timeout */
}

static netdev_tx_t
start_xmit (struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	struct netdev_desc *txdesc;
	unsigned entry;
	u64 tfc_vlan_tag = 0;

	if (np->link_status == 0) {	/* Link Down */
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	entry = np->cur_tx % TX_RING_SIZE;
	np->tx_skbuff[entry] = skb;
	txdesc = &np->tx_ring[entry];

#if 0
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		txdesc->status |=
		    cpu_to_le64 (TCPChecksumEnable | UDPChecksumEnable |
				 IPChecksumEnable);
	}
#endif
	if (np->vlan) {
		tfc_vlan_tag = VLANTagInsert |
		    ((u64)np->vlan << 32) |
		    ((u64)skb->priority << 45);
	}
	txdesc->fraginfo = cpu_to_le64 (dma_map_single(&np->pdev->dev, skb->data,
						       skb->len, DMA_TO_DEVICE));
	txdesc->fraginfo |= cpu_to_le64((u64)skb->len << 48);

	/* DL2K bug: DMA fails to get next descriptor ptr in 10Mbps mode
	 * Work around: Always use 1 descriptor in 10Mbps mode */
	if (entry % np->tx_coalesce == 0 || np->speed == 10)
		txdesc->status = cpu_to_le64 (entry | tfc_vlan_tag |
					      WordAlignDisable |
					      TxDMAIndicate |
					      (1 << FragCountShift));
	else
		txdesc->status = cpu_to_le64 (entry | tfc_vlan_tag |
					      WordAlignDisable |
					      (1 << FragCountShift));

	/* TxDMAPollNow */
	dw32(DMACtrl, dr32(DMACtrl) | 0x00001000);
	/* Schedule ISR */
	dw32(CountDown, 10000);
	np->cur_tx = (np->cur_tx + 1) % TX_RING_SIZE;
	if ((np->cur_tx - np->old_tx + TX_RING_SIZE) % TX_RING_SIZE
			< TX_QUEUE_LEN - 1 && np->speed != 10) {
		/* do nothing */
	} else if (!netif_queue_stopped(dev)) {
		netif_stop_queue (dev);
	}

	/* The first TFDListPtr */
	if (!dr32(TFDListPtr0)) {
		dw32(TFDListPtr0, np->tx_ring_dma +
		     entry * sizeof (struct netdev_desc));
		dw32(TFDListPtr1, 0);
	}

	return NETDEV_TX_OK;
}

static irqreturn_t
rio_interrupt (int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	unsigned int_status;
	int cnt = max_intrloop;
	int handled = 0;

	while (1) {
		int_status = dr16(IntStatus);
		dw16(IntStatus, int_status);
		int_status &= DEFAULT_INTR;
		if (int_status == 0 || --cnt < 0)
			break;
		handled = 1;
		/* Processing received packets */
		if (int_status & RxDMAComplete)
			receive_packet (dev);
		/* TxDMAComplete interrupt */
		if ((int_status & (TxDMAComplete|IntRequested))) {
			int tx_status;
			tx_status = dr32(TxStatus);
			if (tx_status & 0x01)
				tx_error (dev, tx_status);
			/* Free used tx skbuffs */
			rio_free_tx (dev, 1);
		}

		/* Handle uncommon events */
		if (int_status &
		    (HostError | LinkEvent | UpdateStats))
			rio_error (dev, int_status);
	}
	if (np->cur_tx != np->old_tx)
		dw32(CountDown, 100);
	return IRQ_RETVAL(handled);
}

static void
rio_free_tx (struct net_device *dev, int irq)
{
	struct netdev_private *np = netdev_priv(dev);
	int entry = np->old_tx % TX_RING_SIZE;
	unsigned long flag = 0;

	if (irq)
		spin_lock(&np->tx_lock);
	else
		spin_lock_irqsave(&np->tx_lock, flag);

	/* Free used tx skbuffs */
	while (entry != np->cur_tx) {
		struct sk_buff *skb;

		if (!(np->tx_ring[entry].status & cpu_to_le64(TFDDone)))
			break;
		skb = np->tx_skbuff[entry];
		dma_unmap_single(&np->pdev->dev,
				 desc_to_dma(&np->tx_ring[entry]), skb->len,
				 DMA_TO_DEVICE);
		if (irq)
			dev_consume_skb_irq(skb);
		else
			dev_kfree_skb(skb);

		np->tx_skbuff[entry] = NULL;
		entry = (entry + 1) % TX_RING_SIZE;
	}
	if (irq)
		spin_unlock(&np->tx_lock);
	else
		spin_unlock_irqrestore(&np->tx_lock, flag);
	np->old_tx = entry;

	/* If the ring is no longer full, clear tx_full and
	   call netif_wake_queue() */

	if (netif_queue_stopped(dev) &&
	    ((np->cur_tx - np->old_tx + TX_RING_SIZE) % TX_RING_SIZE
	    < TX_QUEUE_LEN - 1 || np->speed == 10)) {
		netif_wake_queue (dev);
	}
}

static void
tx_error (struct net_device *dev, int tx_status)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	int frame_id;
	int i;

	frame_id = (tx_status & 0xffff0000);
	printk (KERN_ERR "%s: Transmit error, TxStatus %4.4x, FrameId %d.\n",
		dev->name, tx_status, frame_id);
	dev->stats.tx_errors++;
	/* Ttransmit Underrun */
	if (tx_status & 0x10) {
		dev->stats.tx_fifo_errors++;
		dw16(TxStartThresh, dr16(TxStartThresh) + 0x10);
		/* Transmit Underrun need to set TxReset, DMARest, FIFOReset */
		dw16(ASICCtrl + 2,
		     TxReset | DMAReset | FIFOReset | NetworkReset);
		/* Wait for ResetBusy bit clear */
		for (i = 50; i > 0; i--) {
			if (!(dr16(ASICCtrl + 2) & ResetBusy))
				break;
			mdelay (1);
		}
		rio_set_led_mode(dev);
		rio_free_tx (dev, 1);
		/* Reset TFDListPtr */
		dw32(TFDListPtr0, np->tx_ring_dma +
		     np->old_tx * sizeof (struct netdev_desc));
		dw32(TFDListPtr1, 0);

		/* Let TxStartThresh stay default value */
	}
	/* Late Collision */
	if (tx_status & 0x04) {
		dev->stats.tx_fifo_errors++;
		/* TxReset and clear FIFO */
		dw16(ASICCtrl + 2, TxReset | FIFOReset);
		/* Wait reset done */
		for (i = 50; i > 0; i--) {
			if (!(dr16(ASICCtrl + 2) & ResetBusy))
				break;
			mdelay (1);
		}
		rio_set_led_mode(dev);
		/* Let TxStartThresh stay default value */
	}
	/* Maximum Collisions */
	if (tx_status & 0x08)
		dev->stats.collisions++;
	/* Restart the Tx */
	dw32(MACCtrl, dr16(MACCtrl) | TxEnable);
}

static int
receive_packet (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	int entry = np->cur_rx % RX_RING_SIZE;
	int cnt = 30;

	/* If RFDDone, FrameStart and FrameEnd set, there is a new packet in. */
	while (1) {
		struct netdev_desc *desc = &np->rx_ring[entry];
		int pkt_len;
		u64 frame_status;

		if (!(desc->status & cpu_to_le64(RFDDone)) ||
		    !(desc->status & cpu_to_le64(FrameStart)) ||
		    !(desc->status & cpu_to_le64(FrameEnd)))
			break;

		/* Chip omits the CRC. */
		frame_status = le64_to_cpu(desc->status);
		pkt_len = frame_status & 0xffff;
		if (--cnt < 0)
			break;
		/* Update rx error statistics, drop packet. */
		if (frame_status & RFS_Errors) {
			dev->stats.rx_errors++;
			if (frame_status & (RxRuntFrame | RxLengthError))
				dev->stats.rx_length_errors++;
			if (frame_status & RxFCSError)
				dev->stats.rx_crc_errors++;
			if (frame_status & RxAlignmentError && np->speed != 1000)
				dev->stats.rx_frame_errors++;
			if (frame_status & RxFIFOOverrun)
				dev->stats.rx_fifo_errors++;
		} else {
			struct sk_buff *skb;

			/* Small skbuffs for short packets */
			if (pkt_len > copy_thresh) {
				dma_unmap_single(&np->pdev->dev,
						 desc_to_dma(desc),
						 np->rx_buf_sz,
						 DMA_FROM_DEVICE);
				skb_put (skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			} else if ((skb = netdev_alloc_skb_ip_align(dev, pkt_len))) {
				dma_sync_single_for_cpu(&np->pdev->dev,
							desc_to_dma(desc),
							np->rx_buf_sz,
							DMA_FROM_DEVICE);
				skb_copy_to_linear_data (skb,
						  np->rx_skbuff[entry]->data,
						  pkt_len);
				skb_put (skb, pkt_len);
				dma_sync_single_for_device(&np->pdev->dev,
							   desc_to_dma(desc),
							   np->rx_buf_sz,
							   DMA_FROM_DEVICE);
			}
			skb->protocol = eth_type_trans (skb, dev);
#if 0
			/* Checksum done by hw, but csum value unavailable. */
			if (np->pdev->pci_rev_id >= 0x0c &&
				!(frame_status & (TCPError | UDPError | IPError))) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			}
#endif
			netif_rx (skb);
		}
		entry = (entry + 1) % RX_RING_SIZE;
	}
	spin_lock(&np->rx_lock);
	np->cur_rx = entry;
	/* Re-allocate skbuffs to fill the descriptor ring */
	entry = np->old_rx;
	while (entry != np->cur_rx) {
		struct sk_buff *skb;
		/* Dropped packets don't need to re-allocate */
		if (np->rx_skbuff[entry] == NULL) {
			skb = netdev_alloc_skb_ip_align(dev, np->rx_buf_sz);
			if (skb == NULL) {
				np->rx_ring[entry].fraginfo = 0;
				printk (KERN_INFO
					"%s: receive_packet: "
					"Unable to re-allocate Rx skbuff.#%d\n",
					dev->name, entry);
				break;
			}
			np->rx_skbuff[entry] = skb;
			np->rx_ring[entry].fraginfo =
			    cpu_to_le64(dma_map_single(&np->pdev->dev, skb->data,
						       np->rx_buf_sz, DMA_FROM_DEVICE));
		}
		np->rx_ring[entry].fraginfo |=
		    cpu_to_le64((u64)np->rx_buf_sz << 48);
		np->rx_ring[entry].status = 0;
		entry = (entry + 1) % RX_RING_SIZE;
	}
	np->old_rx = entry;
	spin_unlock(&np->rx_lock);
	return 0;
}

static void
rio_error (struct net_device *dev, int int_status)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	u16 macctrl;

	/* Link change event */
	if (int_status & LinkEvent) {
		if (mii_wait_link (dev, 10) == 0) {
			printk (KERN_INFO "%s: Link up\n", dev->name);
			if (np->phy_media)
				mii_get_media_pcs (dev);
			else
				mii_get_media (dev);
			if (np->speed == 1000)
				np->tx_coalesce = tx_coalesce;
			else
				np->tx_coalesce = 1;
			macctrl = 0;
			macctrl |= (np->vlan) ? AutoVLANuntagging : 0;
			macctrl |= (np->full_duplex) ? DuplexSelect : 0;
			macctrl |= (np->tx_flow) ?
				TxFlowControlEnable : 0;
			macctrl |= (np->rx_flow) ?
				RxFlowControlEnable : 0;
			dw16(MACCtrl, macctrl);
			np->link_status = 1;
			netif_carrier_on(dev);
		} else {
			printk (KERN_INFO "%s: Link off\n", dev->name);
			np->link_status = 0;
			netif_carrier_off(dev);
		}
	}

	/* UpdateStats statistics registers */
	if (int_status & UpdateStats) {
		get_stats (dev);
	}

	/* PCI Error, a catastronphic error related to the bus interface
	   occurs, set GlobalReset and HostReset to reset. */
	if (int_status & HostError) {
		printk (KERN_ERR "%s: HostError! IntStatus %4.4x.\n",
			dev->name, int_status);
		dw16(ASICCtrl + 2, GlobalReset | HostReset);
		mdelay (500);
		rio_set_led_mode(dev);
	}
}

static struct net_device_stats *
get_stats (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
#ifdef MEM_MAPPING
	int i;
#endif
	unsigned int stat_reg;

	/* All statistics registers need to be acknowledged,
	   else statistic overflow could cause problems */

	dev->stats.rx_packets += dr32(FramesRcvOk);
	dev->stats.tx_packets += dr32(FramesXmtOk);
	dev->stats.rx_bytes += dr32(OctetRcvOk);
	dev->stats.tx_bytes += dr32(OctetXmtOk);

	dev->stats.multicast = dr32(McstFramesRcvdOk);
	dev->stats.collisions += dr32(SingleColFrames)
			     +  dr32(MultiColFrames);

	/* detailed tx errors */
	stat_reg = dr16(FramesAbortXSColls);
	dev->stats.tx_aborted_errors += stat_reg;
	dev->stats.tx_errors += stat_reg;

	stat_reg = dr16(CarrierSenseErrors);
	dev->stats.tx_carrier_errors += stat_reg;
	dev->stats.tx_errors += stat_reg;

	/* Clear all other statistic register. */
	dr32(McstOctetXmtOk);
	dr16(BcstFramesXmtdOk);
	dr32(McstFramesXmtdOk);
	dr16(BcstFramesRcvdOk);
	dr16(MacControlFramesRcvd);
	dr16(FrameTooLongErrors);
	dr16(InRangeLengthErrors);
	dr16(FramesCheckSeqErrors);
	dr16(FramesLostRxErrors);
	dr32(McstOctetXmtOk);
	dr32(BcstOctetXmtOk);
	dr32(McstFramesXmtdOk);
	dr32(FramesWDeferredXmt);
	dr32(LateCollisions);
	dr16(BcstFramesXmtdOk);
	dr16(MacControlFramesXmtd);
	dr16(FramesWEXDeferal);

#ifdef MEM_MAPPING
	for (i = 0x100; i <= 0x150; i += 4)
		dr32(i);
#endif
	dr16(TxJumboFrames);
	dr16(RxJumboFrames);
	dr16(TCPCheckSumErrors);
	dr16(UDPCheckSumErrors);
	dr16(IPCheckSumErrors);
	return &dev->stats;
}

static int
clear_stats (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
#ifdef MEM_MAPPING
	int i;
#endif

	/* All statistics registers need to be acknowledged,
	   else statistic overflow could cause problems */
	dr32(FramesRcvOk);
	dr32(FramesXmtOk);
	dr32(OctetRcvOk);
	dr32(OctetXmtOk);

	dr32(McstFramesRcvdOk);
	dr32(SingleColFrames);
	dr32(MultiColFrames);
	dr32(LateCollisions);
	/* detailed rx errors */
	dr16(FrameTooLongErrors);
	dr16(InRangeLengthErrors);
	dr16(FramesCheckSeqErrors);
	dr16(FramesLostRxErrors);

	/* detailed tx errors */
	dr16(FramesAbortXSColls);
	dr16(CarrierSenseErrors);

	/* Clear all other statistic register. */
	dr32(McstOctetXmtOk);
	dr16(BcstFramesXmtdOk);
	dr32(McstFramesXmtdOk);
	dr16(BcstFramesRcvdOk);
	dr16(MacControlFramesRcvd);
	dr32(McstOctetXmtOk);
	dr32(BcstOctetXmtOk);
	dr32(McstFramesXmtdOk);
	dr32(FramesWDeferredXmt);
	dr16(BcstFramesXmtdOk);
	dr16(MacControlFramesXmtd);
	dr16(FramesWEXDeferal);
#ifdef MEM_MAPPING
	for (i = 0x100; i <= 0x150; i += 4)
		dr32(i);
#endif
	dr16(TxJumboFrames);
	dr16(RxJumboFrames);
	dr16(TCPCheckSumErrors);
	dr16(UDPCheckSumErrors);
	dr16(IPCheckSumErrors);
	return 0;
}

static void
set_multicast (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	u32 hash_table[2];
	u16 rx_mode = 0;

	hash_table[0] = hash_table[1] = 0;
	/* RxFlowcontrol DA: 01-80-C2-00-00-01. Hash index=0x39 */
	hash_table[1] |= 0x02000000;
	if (dev->flags & IFF_PROMISC) {
		/* Receive all frames promiscuously. */
		rx_mode = ReceiveAllFrames;
	} else if ((dev->flags & IFF_ALLMULTI) ||
			(netdev_mc_count(dev) > multicast_filter_limit)) {
		/* Receive broadcast and multicast frames */
		rx_mode = ReceiveBroadcast | ReceiveMulticast | ReceiveUnicast;
	} else if (!netdev_mc_empty(dev)) {
		struct netdev_hw_addr *ha;
		/* Receive broadcast frames and multicast frames filtering
		   by Hashtable */
		rx_mode =
		    ReceiveBroadcast | ReceiveMulticastHash | ReceiveUnicast;
		netdev_for_each_mc_addr(ha, dev) {
			int bit, index = 0;
			int crc = ether_crc_le(ETH_ALEN, ha->addr);
			/* The inverted high significant 6 bits of CRC are
			   used as an index to hashtable */
			for (bit = 0; bit < 6; bit++)
				if (crc & (1 << (31 - bit)))
					index |= (1 << bit);
			hash_table[index / 32] |= (1 << (index % 32));
		}
	} else {
		rx_mode = ReceiveBroadcast | ReceiveUnicast;
	}
	if (np->vlan) {
		/* ReceiveVLANMatch field in ReceiveMode */
		rx_mode |= ReceiveVLANMatch;
	}

	dw32(HashTable0, hash_table[0]);
	dw32(HashTable1, hash_table[1]);
	dw16(ReceiveMode, rx_mode);
}

static void rio_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct netdev_private *np = netdev_priv(dev);

	strscpy(info->driver, "dl2k", sizeof(info->driver));
	strscpy(info->bus_info, pci_name(np->pdev), sizeof(info->bus_info));
}

static int rio_get_link_ksettings(struct net_device *dev,
				  struct ethtool_link_ksettings *cmd)
{
	struct netdev_private *np = netdev_priv(dev);
	u32 supported, advertising;

	if (np->phy_media) {
		/* fiber device */
		supported = SUPPORTED_Autoneg | SUPPORTED_FIBRE;
		advertising = ADVERTISED_Autoneg | ADVERTISED_FIBRE;
		cmd->base.port = PORT_FIBRE;
	} else {
		/* copper device */
		supported = SUPPORTED_10baseT_Half |
			SUPPORTED_10baseT_Full | SUPPORTED_100baseT_Half
			| SUPPORTED_100baseT_Full | SUPPORTED_1000baseT_Full |
			SUPPORTED_Autoneg | SUPPORTED_MII;
		advertising = ADVERTISED_10baseT_Half |
			ADVERTISED_10baseT_Full | ADVERTISED_100baseT_Half |
			ADVERTISED_100baseT_Full | ADVERTISED_1000baseT_Full |
			ADVERTISED_Autoneg | ADVERTISED_MII;
		cmd->base.port = PORT_MII;
	}
	if (np->link_status) {
		cmd->base.speed = np->speed;
		cmd->base.duplex = np->full_duplex ? DUPLEX_FULL : DUPLEX_HALF;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}
	if (np->an_enable)
		cmd->base.autoneg = AUTONEG_ENABLE;
	else
		cmd->base.autoneg = AUTONEG_DISABLE;

	cmd->base.phy_address = np->phy_addr;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int rio_set_link_ksettings(struct net_device *dev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct netdev_private *np = netdev_priv(dev);
	u32 speed = cmd->base.speed;
	u8 duplex = cmd->base.duplex;

	netif_carrier_off(dev);
	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		if (np->an_enable) {
			return 0;
		} else {
			np->an_enable = 1;
			mii_set_media(dev);
			return 0;
		}
	} else {
		np->an_enable = 0;
		if (np->speed == 1000) {
			speed = SPEED_100;
			duplex = DUPLEX_FULL;
			printk("Warning!! Can't disable Auto negotiation in 1000Mbps, change to Manual 100Mbps, Full duplex.\n");
		}
		switch (speed) {
		case SPEED_10:
			np->speed = 10;
			np->full_duplex = (duplex == DUPLEX_FULL);
			break;
		case SPEED_100:
			np->speed = 100;
			np->full_duplex = (duplex == DUPLEX_FULL);
			break;
		case SPEED_1000: /* not supported */
		default:
			return -EINVAL;
		}
		mii_set_media(dev);
	}
	return 0;
}

static u32 rio_get_link(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	return np->link_status;
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo = rio_get_drvinfo,
	.get_link = rio_get_link,
	.get_link_ksettings = rio_get_link_ksettings,
	.set_link_ksettings = rio_set_link_ksettings,
};

static int
rio_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	int phy_addr;
	struct netdev_private *np = netdev_priv(dev);
	struct mii_ioctl_data *miidata = if_mii(rq);

	phy_addr = np->phy_addr;
	switch (cmd) {
	case SIOCGMIIPHY:
		miidata->phy_id = phy_addr;
		break;
	case SIOCGMIIREG:
		miidata->val_out = mii_read (dev, phy_addr, miidata->reg_num);
		break;
	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		mii_write (dev, phy_addr, miidata->reg_num, miidata->val_in);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

#define EEP_READ 0x0200
#define EEP_BUSY 0x8000
/* Read the EEPROM word */
/* We use I/O instruction to read/write eeprom to avoid fail on some machines */
static int read_eeprom(struct netdev_private *np, int eep_addr)
{
	void __iomem *ioaddr = np->eeprom_addr;
	int i = 1000;

	dw16(EepromCtrl, EEP_READ | (eep_addr & 0xff));
	while (i-- > 0) {
		if (!(dr16(EepromCtrl) & EEP_BUSY))
			return dr16(EepromData);
	}
	return 0;
}

enum phy_ctrl_bits {
	MII_READ = 0x00, MII_CLK = 0x01, MII_DATA1 = 0x02, MII_WRITE = 0x04,
	MII_DUPLEX = 0x08,
};

#define mii_delay() dr8(PhyCtrl)
static void
mii_sendbit (struct net_device *dev, u32 data)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;

	data = ((data) ? MII_DATA1 : 0) | (dr8(PhyCtrl) & 0xf8) | MII_WRITE;
	dw8(PhyCtrl, data);
	mii_delay ();
	dw8(PhyCtrl, data | MII_CLK);
	mii_delay ();
}

static int
mii_getbit (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->ioaddr;
	u8 data;

	data = (dr8(PhyCtrl) & 0xf8) | MII_READ;
	dw8(PhyCtrl, data);
	mii_delay ();
	dw8(PhyCtrl, data | MII_CLK);
	mii_delay ();
	return (dr8(PhyCtrl) >> 1) & 1;
}

static void
mii_send_bits (struct net_device *dev, u32 data, int len)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		mii_sendbit (dev, data & (1 << i));
	}
}

static int
mii_read (struct net_device *dev, int phy_addr, int reg_num)
{
	u32 cmd;
	int i;
	u32 retval = 0;

	/* Preamble */
	mii_send_bits (dev, 0xffffffff, 32);
	/* ST(2), OP(2), ADDR(5), REG#(5), TA(2), Data(16) total 32 bits */
	/* ST,OP = 0110'b for read operation */
	cmd = (0x06 << 10 | phy_addr << 5 | reg_num);
	mii_send_bits (dev, cmd, 14);
	/* Turnaround */
	if (mii_getbit (dev))
		goto err_out;
	/* Read data */
	for (i = 0; i < 16; i++) {
		retval |= mii_getbit (dev);
		retval <<= 1;
	}
	/* End cycle */
	mii_getbit (dev);
	return (retval >> 1) & 0xffff;

      err_out:
	return 0;
}
static int
mii_write (struct net_device *dev, int phy_addr, int reg_num, u16 data)
{
	u32 cmd;

	/* Preamble */
	mii_send_bits (dev, 0xffffffff, 32);
	/* ST(2), OP(2), ADDR(5), REG#(5), TA(2), Data(16) total 32 bits */
	/* ST,OP,AAAAA,RRRRR,TA = 0101xxxxxxxxxx10'b = 0x5002 for write */
	cmd = (0x5002 << 16) | (phy_addr << 23) | (reg_num << 18) | data;
	mii_send_bits (dev, cmd, 32);
	/* End cycle */
	mii_getbit (dev);
	return 0;
}
static int
mii_wait_link (struct net_device *dev, int wait)
{
	__u16 bmsr;
	int phy_addr;
	struct netdev_private *np;

	np = netdev_priv(dev);
	phy_addr = np->phy_addr;

	do {
		bmsr = mii_read (dev, phy_addr, MII_BMSR);
		if (bmsr & BMSR_LSTATUS)
			return 0;
		mdelay (1);
	} while (--wait > 0);
	return -1;
}
static int
mii_get_media (struct net_device *dev)
{
	__u16 negotiate;
	__u16 bmsr;
	__u16 mscr;
	__u16 mssr;
	int phy_addr;
	struct netdev_private *np;

	np = netdev_priv(dev);
	phy_addr = np->phy_addr;

	bmsr = mii_read (dev, phy_addr, MII_BMSR);
	if (np->an_enable) {
		if (!(bmsr & BMSR_ANEGCOMPLETE)) {
			/* Auto-Negotiation not completed */
			return -1;
		}
		negotiate = mii_read (dev, phy_addr, MII_ADVERTISE) &
			mii_read (dev, phy_addr, MII_LPA);
		mscr = mii_read (dev, phy_addr, MII_CTRL1000);
		mssr = mii_read (dev, phy_addr, MII_STAT1000);
		if (mscr & ADVERTISE_1000FULL && mssr & LPA_1000FULL) {
			np->speed = 1000;
			np->full_duplex = 1;
			printk (KERN_INFO "Auto 1000 Mbps, Full duplex\n");
		} else if (mscr & ADVERTISE_1000HALF && mssr & LPA_1000HALF) {
			np->speed = 1000;
			np->full_duplex = 0;
			printk (KERN_INFO "Auto 1000 Mbps, Half duplex\n");
		} else if (negotiate & ADVERTISE_100FULL) {
			np->speed = 100;
			np->full_duplex = 1;
			printk (KERN_INFO "Auto 100 Mbps, Full duplex\n");
		} else if (negotiate & ADVERTISE_100HALF) {
			np->speed = 100;
			np->full_duplex = 0;
			printk (KERN_INFO "Auto 100 Mbps, Half duplex\n");
		} else if (negotiate & ADVERTISE_10FULL) {
			np->speed = 10;
			np->full_duplex = 1;
			printk (KERN_INFO "Auto 10 Mbps, Full duplex\n");
		} else if (negotiate & ADVERTISE_10HALF) {
			np->speed = 10;
			np->full_duplex = 0;
			printk (KERN_INFO "Auto 10 Mbps, Half duplex\n");
		}
		if (negotiate & ADVERTISE_PAUSE_CAP) {
			np->tx_flow &= 1;
			np->rx_flow &= 1;
		} else if (negotiate & ADVERTISE_PAUSE_ASYM) {
			np->tx_flow = 0;
			np->rx_flow &= 1;
		}
		/* else tx_flow, rx_flow = user select  */
	} else {
		__u16 bmcr = mii_read (dev, phy_addr, MII_BMCR);
		switch (bmcr & (BMCR_SPEED100 | BMCR_SPEED1000)) {
		case BMCR_SPEED1000:
			printk (KERN_INFO "Operating at 1000 Mbps, ");
			break;
		case BMCR_SPEED100:
			printk (KERN_INFO "Operating at 100 Mbps, ");
			break;
		case 0:
			printk (KERN_INFO "Operating at 10 Mbps, ");
		}
		if (bmcr & BMCR_FULLDPLX) {
			printk (KERN_CONT "Full duplex\n");
		} else {
			printk (KERN_CONT "Half duplex\n");
		}
	}
	if (np->tx_flow)
		printk(KERN_INFO "Enable Tx Flow Control\n");
	else
		printk(KERN_INFO "Disable Tx Flow Control\n");
	if (np->rx_flow)
		printk(KERN_INFO "Enable Rx Flow Control\n");
	else
		printk(KERN_INFO "Disable Rx Flow Control\n");

	return 0;
}

static int
mii_set_media (struct net_device *dev)
{
	__u16 pscr;
	__u16 bmcr;
	__u16 bmsr;
	__u16 anar;
	int phy_addr;
	struct netdev_private *np;
	np = netdev_priv(dev);
	phy_addr = np->phy_addr;

	/* Does user set speed? */
	if (np->an_enable) {
		/* Advertise capabilities */
		bmsr = mii_read (dev, phy_addr, MII_BMSR);
		anar = mii_read (dev, phy_addr, MII_ADVERTISE) &
			~(ADVERTISE_100FULL | ADVERTISE_10FULL |
			  ADVERTISE_100HALF | ADVERTISE_10HALF |
			  ADVERTISE_100BASE4);
		if (bmsr & BMSR_100FULL)
			anar |= ADVERTISE_100FULL;
		if (bmsr & BMSR_100HALF)
			anar |= ADVERTISE_100HALF;
		if (bmsr & BMSR_100BASE4)
			anar |= ADVERTISE_100BASE4;
		if (bmsr & BMSR_10FULL)
			anar |= ADVERTISE_10FULL;
		if (bmsr & BMSR_10HALF)
			anar |= ADVERTISE_10HALF;
		anar |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
		mii_write (dev, phy_addr, MII_ADVERTISE, anar);

		/* Enable Auto crossover */
		pscr = mii_read (dev, phy_addr, MII_PHY_SCR);
		pscr |= 3 << 5;	/* 11'b */
		mii_write (dev, phy_addr, MII_PHY_SCR, pscr);

		/* Soft reset PHY */
		mii_write (dev, phy_addr, MII_BMCR, BMCR_RESET);
		bmcr = BMCR_ANENABLE | BMCR_ANRESTART | BMCR_RESET;
		mii_write (dev, phy_addr, MII_BMCR, bmcr);
		mdelay(1);
	} else {
		/* Force speed setting */
		/* 1) Disable Auto crossover */
		pscr = mii_read (dev, phy_addr, MII_PHY_SCR);
		pscr &= ~(3 << 5);
		mii_write (dev, phy_addr, MII_PHY_SCR, pscr);

		/* 2) PHY Reset */
		bmcr = mii_read (dev, phy_addr, MII_BMCR);
		bmcr |= BMCR_RESET;
		mii_write (dev, phy_addr, MII_BMCR, bmcr);

		/* 3) Power Down */
		bmcr = 0x1940;	/* must be 0x1940 */
		mii_write (dev, phy_addr, MII_BMCR, bmcr);
		mdelay (100);	/* wait a certain time */

		/* 4) Advertise nothing */
		mii_write (dev, phy_addr, MII_ADVERTISE, 0);

		/* 5) Set media and Power Up */
		bmcr = BMCR_PDOWN;
		if (np->speed == 100) {
			bmcr |= BMCR_SPEED100;
			printk (KERN_INFO "Manual 100 Mbps, ");
		} else if (np->speed == 10) {
			printk (KERN_INFO "Manual 10 Mbps, ");
		}
		if (np->full_duplex) {
			bmcr |= BMCR_FULLDPLX;
			printk (KERN_CONT "Full duplex\n");
		} else {
			printk (KERN_CONT "Half duplex\n");
		}
#if 0
		/* Set 1000BaseT Master/Slave setting */
		mscr = mii_read (dev, phy_addr, MII_CTRL1000);
		mscr |= MII_MSCR_CFG_ENABLE;
		mscr &= ~MII_MSCR_CFG_VALUE = 0;
#endif
		mii_write (dev, phy_addr, MII_BMCR, bmcr);
		mdelay(10);
	}
	return 0;
}

static int
mii_get_media_pcs (struct net_device *dev)
{
	__u16 negotiate;
	__u16 bmsr;
	int phy_addr;
	struct netdev_private *np;

	np = netdev_priv(dev);
	phy_addr = np->phy_addr;

	bmsr = mii_read (dev, phy_addr, PCS_BMSR);
	if (np->an_enable) {
		if (!(bmsr & BMSR_ANEGCOMPLETE)) {
			/* Auto-Negotiation not completed */
			return -1;
		}
		negotiate = mii_read (dev, phy_addr, PCS_ANAR) &
			mii_read (dev, phy_addr, PCS_ANLPAR);
		np->speed = 1000;
		if (negotiate & PCS_ANAR_FULL_DUPLEX) {
			printk (KERN_INFO "Auto 1000 Mbps, Full duplex\n");
			np->full_duplex = 1;
		} else {
			printk (KERN_INFO "Auto 1000 Mbps, half duplex\n");
			np->full_duplex = 0;
		}
		if (negotiate & PCS_ANAR_PAUSE) {
			np->tx_flow &= 1;
			np->rx_flow &= 1;
		} else if (negotiate & PCS_ANAR_ASYMMETRIC) {
			np->tx_flow = 0;
			np->rx_flow &= 1;
		}
		/* else tx_flow, rx_flow = user select  */
	} else {
		__u16 bmcr = mii_read (dev, phy_addr, PCS_BMCR);
		printk (KERN_INFO "Operating at 1000 Mbps, ");
		if (bmcr & BMCR_FULLDPLX) {
			printk (KERN_CONT "Full duplex\n");
		} else {
			printk (KERN_CONT "Half duplex\n");
		}
	}
	if (np->tx_flow)
		printk(KERN_INFO "Enable Tx Flow Control\n");
	else
		printk(KERN_INFO "Disable Tx Flow Control\n");
	if (np->rx_flow)
		printk(KERN_INFO "Enable Rx Flow Control\n");
	else
		printk(KERN_INFO "Disable Rx Flow Control\n");

	return 0;
}

static int
mii_set_media_pcs (struct net_device *dev)
{
	__u16 bmcr;
	__u16 esr;
	__u16 anar;
	int phy_addr;
	struct netdev_private *np;
	np = netdev_priv(dev);
	phy_addr = np->phy_addr;

	/* Auto-Negotiation? */
	if (np->an_enable) {
		/* Advertise capabilities */
		esr = mii_read (dev, phy_addr, PCS_ESR);
		anar = mii_read (dev, phy_addr, MII_ADVERTISE) &
			~PCS_ANAR_HALF_DUPLEX &
			~PCS_ANAR_FULL_DUPLEX;
		if (esr & (MII_ESR_1000BT_HD | MII_ESR_1000BX_HD))
			anar |= PCS_ANAR_HALF_DUPLEX;
		if (esr & (MII_ESR_1000BT_FD | MII_ESR_1000BX_FD))
			anar |= PCS_ANAR_FULL_DUPLEX;
		anar |= PCS_ANAR_PAUSE | PCS_ANAR_ASYMMETRIC;
		mii_write (dev, phy_addr, MII_ADVERTISE, anar);

		/* Soft reset PHY */
		mii_write (dev, phy_addr, MII_BMCR, BMCR_RESET);
		bmcr = BMCR_ANENABLE | BMCR_ANRESTART | BMCR_RESET;
		mii_write (dev, phy_addr, MII_BMCR, bmcr);
		mdelay(1);
	} else {
		/* Force speed setting */
		/* PHY Reset */
		bmcr = BMCR_RESET;
		mii_write (dev, phy_addr, MII_BMCR, bmcr);
		mdelay(10);
		if (np->full_duplex) {
			bmcr = BMCR_FULLDPLX;
			printk (KERN_INFO "Manual full duplex\n");
		} else {
			bmcr = 0;
			printk (KERN_INFO "Manual half duplex\n");
		}
		mii_write (dev, phy_addr, MII_BMCR, bmcr);
		mdelay(10);

		/*  Advertise nothing */
		mii_write (dev, phy_addr, MII_ADVERTISE, 0);
	}
	return 0;
}


static int
rio_close (struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	struct pci_dev *pdev = np->pdev;

	netif_stop_queue (dev);

	rio_hw_stop(dev);

	free_irq(pdev->irq, dev);
	timer_delete_sync(&np->timer);

	free_list(dev);

	return 0;
}

static void
rio_remove1 (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);

	if (dev) {
		struct netdev_private *np = netdev_priv(dev);

		unregister_netdev (dev);
		dma_free_coherent(&pdev->dev, RX_TOTAL_SIZE, np->rx_ring,
				  np->rx_ring_dma);
		dma_free_coherent(&pdev->dev, TX_TOTAL_SIZE, np->tx_ring,
				  np->tx_ring_dma);
#ifdef MEM_MAPPING
		pci_iounmap(pdev, np->ioaddr);
#endif
		pci_iounmap(pdev, np->eeprom_addr);
		free_netdev (dev);
		pci_release_regions (pdev);
		pci_disable_device (pdev);
	}
}

#ifdef CONFIG_PM_SLEEP
static int rio_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct netdev_private *np = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	netif_device_detach(dev);
	timer_delete_sync(&np->timer);
	rio_hw_stop(dev);

	return 0;
}

static int rio_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct netdev_private *np = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	rio_reset_ring(np);
	rio_hw_init(dev);
	np->timer.expires = jiffies + 1 * HZ;
	add_timer(&np->timer);
	netif_device_attach(dev);
	dl2k_enable_int(np);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(rio_pm_ops, rio_suspend, rio_resume);
#define RIO_PM_OPS    (&rio_pm_ops)

#else

#define RIO_PM_OPS	NULL

#endif /* CONFIG_PM_SLEEP */

static struct pci_driver rio_driver = {
	.name		= "dl2k",
	.id_table	= rio_pci_tbl,
	.probe		= rio_probe1,
	.remove		= rio_remove1,
	.driver.pm	= RIO_PM_OPS,
};

module_pci_driver(rio_driver);

/* Read Documentation/networking/device_drivers/ethernet/dlink/dl2k.rst. */

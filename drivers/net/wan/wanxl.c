// SPDX-License-Identifier: GPL-2.0-only
/*
 * wanXL serial card driver for Linux
 * host part
 *
 * Copyright (C) 2003 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * Status:
 *   - Only DTE (external clock) support with NRZ and NRZI encodings
 *   - wanXL100 will require minor driver modifications, no access to hw
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/hdlc.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "wanxl.h"

static const char *version = "wanXL serial card driver version: 0.48";

#define PLX_CTL_RESET   0x40000000 /* adapter reset */

#undef DEBUG_PKT
#undef DEBUG_PCI

/* MAILBOX #1 - PUTS COMMANDS */
#define MBX1_CMD_ABORTJ 0x85000000 /* Abort and Jump */
#ifdef __LITTLE_ENDIAN
#define MBX1_CMD_BSWAP  0x8C000001 /* little-endian Byte Swap Mode */
#else
#define MBX1_CMD_BSWAP  0x8C000000 /* big-endian Byte Swap Mode */
#endif

/* MAILBOX #2 - DRAM SIZE */
#define MBX2_MEMSZ_MASK 0xFFFF0000 /* PUTS Memory Size Register mask */

struct port {
	struct net_device *dev;
	struct card *card;
	spinlock_t lock;	/* for wanxl_xmit */
	int node;		/* physical port #0 - 3 */
	unsigned int clock_type;
	int tx_in, tx_out;
	struct sk_buff *tx_skbs[TX_BUFFERS];
};

struct card_status {
	desc_t rx_descs[RX_QUEUE_LENGTH];
	port_status_t port_status[4];
};

struct card {
	int n_ports;		/* 1, 2 or 4 ports */
	u8 irq;

	u8 __iomem *plx;	/* PLX PCI9060 virtual base address */
	struct pci_dev *pdev;	/* for pci_name(pdev) */
	int rx_in;
	struct sk_buff *rx_skbs[RX_QUEUE_LENGTH];
	struct card_status *status;	/* shared between host and card */
	dma_addr_t status_address;
	struct port ports[];	/* 1 - 4 port structures follow */
};

static inline struct port *dev_to_port(struct net_device *dev)
{
	return (struct port *)dev_to_hdlc(dev)->priv;
}

static inline port_status_t *get_status(struct port *port)
{
	return &port->card->status->port_status[port->node];
}

#ifdef DEBUG_PCI
static inline dma_addr_t pci_map_single_debug(struct pci_dev *pdev, void *ptr,
					      size_t size, int direction)
{
	dma_addr_t addr = dma_map_single(&pdev->dev, ptr, size, direction);

	if (addr + size > 0x100000000LL)
		pr_crit("%s: pci_map_single() returned memory at 0x%llx!\n",
			pci_name(pdev), (unsigned long long)addr);
	return addr;
}

#undef pci_map_single
#define pci_map_single pci_map_single_debug
#endif

/* Cable and/or personality module change interrupt service */
static inline void wanxl_cable_intr(struct port *port)
{
	u32 value = get_status(port)->cable;
	int valid = 1;
	const char *cable, *pm, *dte = "", *dsr = "", *dcd = "";

	switch (value & 0x7) {
	case STATUS_CABLE_V35:
		cable = "V.35";
		break;
	case STATUS_CABLE_X21:
		cable = "X.21";
		break;
	case STATUS_CABLE_V24:
		cable = "V.24";
		break;
	case STATUS_CABLE_EIA530:
		cable = "EIA530";
		break;
	case STATUS_CABLE_NONE:
		cable = "no";
		break;
	default:
		cable = "invalid";
	}

	switch ((value >> STATUS_CABLE_PM_SHIFT) & 0x7) {
	case STATUS_CABLE_V35:
		pm = "V.35";
		break;
	case STATUS_CABLE_X21:
		pm = "X.21";
		break;
	case STATUS_CABLE_V24:
		pm = "V.24";
		break;
	case STATUS_CABLE_EIA530:
		pm = "EIA530";
		break;
	case STATUS_CABLE_NONE:
		pm = "no personality";
		valid = 0;
		break;
	default:
		pm = "invalid personality";
		valid = 0;
	}

	if (valid) {
		if ((value & 7) == ((value >> STATUS_CABLE_PM_SHIFT) & 7)) {
			dsr = (value & STATUS_CABLE_DSR) ? ", DSR ON" :
				", DSR off";
			dcd = (value & STATUS_CABLE_DCD) ? ", carrier ON" :
				", carrier off";
		}
		dte = (value & STATUS_CABLE_DCE) ? " DCE" : " DTE";
	}
	netdev_info(port->dev, "%s%s module, %s cable%s%s\n",
		    pm, dte, cable, dsr, dcd);

	if (value & STATUS_CABLE_DCD)
		netif_carrier_on(port->dev);
	else
		netif_carrier_off(port->dev);
}

/* Transmit complete interrupt service */
static inline void wanxl_tx_intr(struct port *port)
{
	struct net_device *dev = port->dev;

	while (1) {
		desc_t *desc = &get_status(port)->tx_descs[port->tx_in];
		struct sk_buff *skb = port->tx_skbs[port->tx_in];

		switch (desc->stat) {
		case PACKET_FULL:
		case PACKET_EMPTY:
			netif_wake_queue(dev);
			return;

		case PACKET_UNDERRUN:
			dev->stats.tx_errors++;
			dev->stats.tx_fifo_errors++;
			break;

		default:
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += skb->len;
		}
		desc->stat = PACKET_EMPTY; /* Free descriptor */
		dma_unmap_single(&port->card->pdev->dev, desc->address,
				 skb->len, DMA_TO_DEVICE);
		dev_consume_skb_irq(skb);
		port->tx_in = (port->tx_in + 1) % TX_BUFFERS;
	}
}

/* Receive complete interrupt service */
static inline void wanxl_rx_intr(struct card *card)
{
	desc_t *desc;

	while (desc = &card->status->rx_descs[card->rx_in],
	       desc->stat != PACKET_EMPTY) {
		if ((desc->stat & PACKET_PORT_MASK) > card->n_ports) {
			pr_crit("%s: received packet for nonexistent port\n",
				pci_name(card->pdev));
		} else {
			struct sk_buff *skb = card->rx_skbs[card->rx_in];
			struct port *port = &card->ports[desc->stat &
						    PACKET_PORT_MASK];
			struct net_device *dev = port->dev;

			if (!skb) {
				dev->stats.rx_dropped++;
			} else {
				dma_unmap_single(&card->pdev->dev,
						 desc->address, BUFFER_LENGTH,
						 DMA_FROM_DEVICE);
				skb_put(skb, desc->length);

#ifdef DEBUG_PKT
				printk(KERN_DEBUG "%s RX(%i):", dev->name,
				       skb->len);
				debug_frame(skb);
#endif
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += skb->len;
				skb->protocol = hdlc_type_trans(skb, dev);
				netif_rx(skb);
				skb = NULL;
			}

			if (!skb) {
				skb = dev_alloc_skb(BUFFER_LENGTH);
				desc->address = skb ?
					dma_map_single(&card->pdev->dev,
						       skb->data,
						       BUFFER_LENGTH,
						       DMA_FROM_DEVICE) : 0;
				card->rx_skbs[card->rx_in] = skb;
			}
		}
		desc->stat = PACKET_EMPTY; /* Free descriptor */
		card->rx_in = (card->rx_in + 1) % RX_QUEUE_LENGTH;
	}
}

static irqreturn_t wanxl_intr(int irq, void *dev_id)
{
	struct card *card = dev_id;
	int i;
	u32 stat;
	int handled = 0;

	while ((stat = readl(card->plx + PLX_DOORBELL_FROM_CARD)) != 0) {
		handled = 1;
		writel(stat, card->plx + PLX_DOORBELL_FROM_CARD);

		for (i = 0; i < card->n_ports; i++) {
			if (stat & (1 << (DOORBELL_FROM_CARD_TX_0 + i)))
				wanxl_tx_intr(&card->ports[i]);
			if (stat & (1 << (DOORBELL_FROM_CARD_CABLE_0 + i)))
				wanxl_cable_intr(&card->ports[i]);
		}
		if (stat & (1 << DOORBELL_FROM_CARD_RX))
			wanxl_rx_intr(card);
	}

	return IRQ_RETVAL(handled);
}

static netdev_tx_t wanxl_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct port *port = dev_to_port(dev);
	desc_t *desc;

	spin_lock(&port->lock);

	desc = &get_status(port)->tx_descs[port->tx_out];
	if (desc->stat != PACKET_EMPTY) {
		/* should never happen - previous xmit should stop queue */
#ifdef DEBUG_PKT
                printk(KERN_DEBUG "%s: transmitter buffer full\n", dev->name);
#endif
		netif_stop_queue(dev);
		spin_unlock(&port->lock);
		return NETDEV_TX_BUSY;       /* request packet to be queued */
	}

#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s TX(%i):", dev->name, skb->len);
	debug_frame(skb);
#endif

	port->tx_skbs[port->tx_out] = skb;
	desc->address = dma_map_single(&port->card->pdev->dev, skb->data,
				       skb->len, DMA_TO_DEVICE);
	desc->length = skb->len;
	desc->stat = PACKET_FULL;
	writel(1 << (DOORBELL_TO_CARD_TX_0 + port->node),
	       port->card->plx + PLX_DOORBELL_TO_CARD);

	port->tx_out = (port->tx_out + 1) % TX_BUFFERS;

	if (get_status(port)->tx_descs[port->tx_out].stat != PACKET_EMPTY) {
		netif_stop_queue(dev);
#ifdef DEBUG_PKT
		printk(KERN_DEBUG "%s: transmitter buffer full\n", dev->name);
#endif
	}

	spin_unlock(&port->lock);
	return NETDEV_TX_OK;
}

static int wanxl_attach(struct net_device *dev, unsigned short encoding,
			unsigned short parity)
{
	struct port *port = dev_to_port(dev);

	if (encoding != ENCODING_NRZ &&
	    encoding != ENCODING_NRZI)
		return -EINVAL;

	if (parity != PARITY_NONE &&
	    parity != PARITY_CRC32_PR1_CCITT &&
	    parity != PARITY_CRC16_PR1_CCITT &&
	    parity != PARITY_CRC32_PR0_CCITT &&
	    parity != PARITY_CRC16_PR0_CCITT)
		return -EINVAL;

	get_status(port)->encoding = encoding;
	get_status(port)->parity = parity;
	return 0;
}

static int wanxl_ioctl(struct net_device *dev, struct if_settings *ifs)
{
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings line;
	struct port *port = dev_to_port(dev);

	switch (ifs->type) {
	case IF_GET_IFACE:
		ifs->type = IF_IFACE_SYNC_SERIAL;
		if (ifs->size < size) {
			ifs->size = size; /* data size wanted */
			return -ENOBUFS;
		}
		memset(&line, 0, sizeof(line));
		line.clock_type = get_status(port)->clocking;
		line.clock_rate = 0;
		line.loopback = 0;

		if (copy_to_user(ifs->ifs_ifsu.sync, &line, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (dev->flags & IFF_UP)
			return -EBUSY;

		if (copy_from_user(&line, ifs->ifs_ifsu.sync,
				   size))
			return -EFAULT;

		if (line.clock_type != CLOCK_EXT &&
		    line.clock_type != CLOCK_TXFROMRX)
			return -EINVAL; /* No such clock setting */

		if (line.loopback != 0)
			return -EINVAL;

		get_status(port)->clocking = line.clock_type;
		return 0;

	default:
		return hdlc_ioctl(dev, ifs);
	}
}

static int wanxl_open(struct net_device *dev)
{
	struct port *port = dev_to_port(dev);
	u8 __iomem *dbr = port->card->plx + PLX_DOORBELL_TO_CARD;
	unsigned long timeout;
	int i;

	if (get_status(port)->open) {
		netdev_err(dev, "port already open\n");
		return -EIO;
	}

	i = hdlc_open(dev);
	if (i)
		return i;

	port->tx_in = port->tx_out = 0;
	for (i = 0; i < TX_BUFFERS; i++)
		get_status(port)->tx_descs[i].stat = PACKET_EMPTY;
	/* signal the card */
	writel(1 << (DOORBELL_TO_CARD_OPEN_0 + port->node), dbr);

	timeout = jiffies + HZ;
	do {
		if (get_status(port)->open) {
			netif_start_queue(dev);
			return 0;
		}
	} while (time_after(timeout, jiffies));

	netdev_err(dev, "unable to open port\n");
	/* ask the card to close the port, should it be still alive */
	writel(1 << (DOORBELL_TO_CARD_CLOSE_0 + port->node), dbr);
	return -EFAULT;
}

static int wanxl_close(struct net_device *dev)
{
	struct port *port = dev_to_port(dev);
	unsigned long timeout;
	int i;

	hdlc_close(dev);
	/* signal the card */
	writel(1 << (DOORBELL_TO_CARD_CLOSE_0 + port->node),
	       port->card->plx + PLX_DOORBELL_TO_CARD);

	timeout = jiffies + HZ;
	do {
		if (!get_status(port)->open)
			break;
	} while (time_after(timeout, jiffies));

	if (get_status(port)->open)
		netdev_err(dev, "unable to close port\n");

	netif_stop_queue(dev);

	for (i = 0; i < TX_BUFFERS; i++) {
		desc_t *desc = &get_status(port)->tx_descs[i];

		if (desc->stat != PACKET_EMPTY) {
			desc->stat = PACKET_EMPTY;
			dma_unmap_single(&port->card->pdev->dev,
					 desc->address, port->tx_skbs[i]->len,
					 DMA_TO_DEVICE);
			dev_kfree_skb(port->tx_skbs[i]);
		}
	}
	return 0;
}

static struct net_device_stats *wanxl_get_stats(struct net_device *dev)
{
	struct port *port = dev_to_port(dev);

	dev->stats.rx_over_errors = get_status(port)->rx_overruns;
	dev->stats.rx_frame_errors = get_status(port)->rx_frame_errors;
	dev->stats.rx_errors = dev->stats.rx_over_errors +
		dev->stats.rx_frame_errors;
	return &dev->stats;
}

static int wanxl_puts_command(struct card *card, u32 cmd)
{
	unsigned long timeout = jiffies + 5 * HZ;

	writel(cmd, card->plx + PLX_MAILBOX_1);
	do {
		if (readl(card->plx + PLX_MAILBOX_1) == 0)
			return 0;

		schedule();
	} while (time_after(timeout, jiffies));

	return -1;
}

static void wanxl_reset(struct card *card)
{
	u32 old_value = readl(card->plx + PLX_CONTROL) & ~PLX_CTL_RESET;

	writel(0x80, card->plx + PLX_MAILBOX_0);
	writel(old_value | PLX_CTL_RESET, card->plx + PLX_CONTROL);
	readl(card->plx + PLX_CONTROL); /* wait for posted write */
	udelay(1);
	writel(old_value, card->plx + PLX_CONTROL);
	readl(card->plx + PLX_CONTROL); /* wait for posted write */
}

static void wanxl_pci_remove_one(struct pci_dev *pdev)
{
	struct card *card = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < card->n_ports; i++) {
		unregister_hdlc_device(card->ports[i].dev);
		free_netdev(card->ports[i].dev);
	}

	/* unregister and free all host resources */
	if (card->irq)
		free_irq(card->irq, card);

	wanxl_reset(card);

	for (i = 0; i < RX_QUEUE_LENGTH; i++)
		if (card->rx_skbs[i]) {
			dma_unmap_single(&card->pdev->dev,
					 card->status->rx_descs[i].address,
					 BUFFER_LENGTH, DMA_FROM_DEVICE);
			dev_kfree_skb(card->rx_skbs[i]);
		}

	if (card->plx)
		iounmap(card->plx);

	if (card->status)
		dma_free_coherent(&pdev->dev, sizeof(struct card_status),
				  card->status, card->status_address);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(card);
}

#include "wanxlfw.inc"

static const struct net_device_ops wanxl_ops = {
	.ndo_open       = wanxl_open,
	.ndo_stop       = wanxl_close,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_siocwandev = wanxl_ioctl,
	.ndo_get_stats  = wanxl_get_stats,
};

static int wanxl_pci_init_one(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct card *card;
	u32 ramsize, stat;
	unsigned long timeout;
	u32 plx_phy;		/* PLX PCI base address */
	u32 mem_phy;		/* memory PCI base addr */
	u8 __iomem *mem;	/* memory virtual base addr */
	int i, ports;

#ifndef MODULE
	pr_info_once("%s\n", version);
#endif

	i = pci_enable_device(pdev);
	if (i)
		return i;

	/* QUICC can only access first 256 MB of host RAM directly,
	 * but PLX9060 DMA does 32-bits for actual packet data transfers
	 */

	/* FIXME when PCI/DMA subsystems are fixed.
	 * We set both dma_mask and consistent_dma_mask to 28 bits
	 * and pray pci_alloc_consistent() will use this info. It should
	 * work on most platforms
	 */
	if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(28)) ||
	    dma_set_mask(&pdev->dev, DMA_BIT_MASK(28))) {
		pr_err("No usable DMA configuration\n");
		pci_disable_device(pdev);
		return -EIO;
	}

	i = pci_request_regions(pdev, "wanXL");
	if (i) {
		pci_disable_device(pdev);
		return i;
	}

	switch (pdev->device) {
	case PCI_DEVICE_ID_SBE_WANXL100:
		ports = 1;
		break;
	case PCI_DEVICE_ID_SBE_WANXL200:
		ports = 2;
		break;
	default:
		ports = 4;
	}

	card = kzalloc(struct_size(card, ports, ports), GFP_KERNEL);
	if (!card) {
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		return -ENOBUFS;
	}

	pci_set_drvdata(pdev, card);
	card->pdev = pdev;

	card->status = dma_alloc_coherent(&pdev->dev,
					  sizeof(struct card_status),
					  &card->status_address, GFP_KERNEL);
	if (!card->status) {
		wanxl_pci_remove_one(pdev);
		return -ENOBUFS;
	}

#ifdef DEBUG_PCI
	printk(KERN_DEBUG "wanXL %s: pci_alloc_consistent() returned memory"
	       " at 0x%LX\n", pci_name(pdev),
	       (unsigned long long)card->status_address);
#endif

	/* FIXME when PCI/DMA subsystems are fixed.
	 * We set both dma_mask and consistent_dma_mask back to 32 bits
	 * to indicate the card can do 32-bit DMA addressing
	 */
	if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32)) ||
	    dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		pr_err("No usable DMA configuration\n");
		wanxl_pci_remove_one(pdev);
		return -EIO;
	}

	/* set up PLX mapping */
	plx_phy = pci_resource_start(pdev, 0);

	card->plx = ioremap(plx_phy, 0x70);
	if (!card->plx) {
		pr_err("ioremap() failed\n");
		wanxl_pci_remove_one(pdev);
		return -EFAULT;
	}

#if RESET_WHILE_LOADING
	wanxl_reset(card);
#endif

	timeout = jiffies + 20 * HZ;
	while ((stat = readl(card->plx + PLX_MAILBOX_0)) != 0) {
		if (time_before(timeout, jiffies)) {
			pr_warn("%s: timeout waiting for PUTS to complete\n",
				pci_name(pdev));
			wanxl_pci_remove_one(pdev);
			return -ENODEV;
		}

		switch (stat & 0xC0) {
		case 0x00:	/* hmm - PUTS completed with non-zero code? */
		case 0x80:	/* PUTS still testing the hardware */
			break;

		default:
			pr_warn("%s: PUTS test 0x%X failed\n",
				pci_name(pdev), stat & 0x30);
			wanxl_pci_remove_one(pdev);
			return -ENODEV;
		}

		schedule();
	}

	/* get on-board memory size (PUTS detects no more than 4 MB) */
	ramsize = readl(card->plx + PLX_MAILBOX_2) & MBX2_MEMSZ_MASK;

	/* set up on-board RAM mapping */
	mem_phy = pci_resource_start(pdev, 2);

	/* sanity check the board's reported memory size */
	if (ramsize < BUFFERS_ADDR +
	    (TX_BUFFERS + RX_BUFFERS) * BUFFER_LENGTH * ports) {
		pr_warn("%s: no enough on-board RAM (%u bytes detected, %u bytes required)\n",
			pci_name(pdev), ramsize,
			BUFFERS_ADDR +
			(TX_BUFFERS + RX_BUFFERS) * BUFFER_LENGTH * ports);
		wanxl_pci_remove_one(pdev);
		return -ENODEV;
	}

	if (wanxl_puts_command(card, MBX1_CMD_BSWAP)) {
		pr_warn("%s: unable to Set Byte Swap Mode\n", pci_name(pdev));
		wanxl_pci_remove_one(pdev);
		return -ENODEV;
	}

	for (i = 0; i < RX_QUEUE_LENGTH; i++) {
		struct sk_buff *skb = dev_alloc_skb(BUFFER_LENGTH);

		card->rx_skbs[i] = skb;
		if (skb)
			card->status->rx_descs[i].address =
				dma_map_single(&card->pdev->dev, skb->data,
					       BUFFER_LENGTH, DMA_FROM_DEVICE);
	}

	mem = ioremap(mem_phy, PDM_OFFSET + sizeof(firmware));
	if (!mem) {
		pr_err("ioremap() failed\n");
		wanxl_pci_remove_one(pdev);
		return -EFAULT;
	}

	for (i = 0; i < sizeof(firmware); i += 4)
		writel(ntohl(*(__be32 *)(firmware + i)), mem + PDM_OFFSET + i);

	for (i = 0; i < ports; i++)
		writel(card->status_address +
		       (void *)&card->status->port_status[i] -
		       (void *)card->status, mem + PDM_OFFSET + 4 + i * 4);
	writel(card->status_address, mem + PDM_OFFSET + 20);
	writel(PDM_OFFSET, mem);
	iounmap(mem);

	writel(0, card->plx + PLX_MAILBOX_5);

	if (wanxl_puts_command(card, MBX1_CMD_ABORTJ)) {
		pr_warn("%s: unable to Abort and Jump\n", pci_name(pdev));
		wanxl_pci_remove_one(pdev);
		return -ENODEV;
	}

	timeout = jiffies + 5 * HZ;
	do {
		stat = readl(card->plx + PLX_MAILBOX_5);
		if (stat)
			break;
		schedule();
	} while (time_after(timeout, jiffies));

	if (!stat) {
		pr_warn("%s: timeout while initializing card firmware\n",
			pci_name(pdev));
		wanxl_pci_remove_one(pdev);
		return -ENODEV;
	}

#if DETECT_RAM
	ramsize = stat;
#endif

	pr_info("%s: at 0x%X, %u KB of RAM at 0x%X, irq %u\n",
		pci_name(pdev), plx_phy, ramsize / 1024, mem_phy, pdev->irq);

	/* Allocate IRQ */
	if (request_irq(pdev->irq, wanxl_intr, IRQF_SHARED, "wanXL", card)) {
		pr_warn("%s: could not allocate IRQ%i\n",
			pci_name(pdev), pdev->irq);
		wanxl_pci_remove_one(pdev);
		return -EBUSY;
	}
	card->irq = pdev->irq;

	for (i = 0; i < ports; i++) {
		hdlc_device *hdlc;
		struct port *port = &card->ports[i];
		struct net_device *dev = alloc_hdlcdev(port);

		if (!dev) {
			pr_err("%s: unable to allocate memory\n",
			       pci_name(pdev));
			wanxl_pci_remove_one(pdev);
			return -ENOMEM;
		}

		port->dev = dev;
		hdlc = dev_to_hdlc(dev);
		spin_lock_init(&port->lock);
		dev->tx_queue_len = 50;
		dev->netdev_ops = &wanxl_ops;
		hdlc->attach = wanxl_attach;
		hdlc->xmit = wanxl_xmit;
		port->card = card;
		port->node = i;
		get_status(port)->clocking = CLOCK_EXT;
		if (register_hdlc_device(dev)) {
			pr_err("%s: unable to register hdlc device\n",
			       pci_name(pdev));
			free_netdev(dev);
			wanxl_pci_remove_one(pdev);
			return -ENOBUFS;
		}
		card->n_ports++;
	}

	pr_info("%s: port", pci_name(pdev));
	for (i = 0; i < ports; i++)
		pr_cont("%s #%i: %s",
			i ? "," : "", i, card->ports[i].dev->name);
	pr_cont("\n");

	for (i = 0; i < ports; i++)
		wanxl_cable_intr(&card->ports[i]); /* get carrier status etc.*/

	return 0;
}

static const struct pci_device_id wanxl_pci_tbl[] = {
	{ PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_SBE_WANXL100, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_SBE_WANXL200, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_SBE_WANXL400, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};

static struct pci_driver wanxl_pci_driver = {
	.name		= "wanXL",
	.id_table	= wanxl_pci_tbl,
	.probe		= wanxl_pci_init_one,
	.remove		= wanxl_pci_remove_one,
};

static int __init wanxl_init_module(void)
{
#ifdef MODULE
	pr_info("%s\n", version);
#endif
	return pci_register_driver(&wanxl_pci_driver);
}

static void __exit wanxl_cleanup_module(void)
{
	pci_unregister_driver(&wanxl_pci_driver);
}

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("SBE Inc. wanXL serial port driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, wanxl_pci_tbl);

module_init(wanxl_init_module);
module_exit(wanxl_cleanup_module);

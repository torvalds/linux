/*
 * Goramo PCI200SYN synchronous serial card driver for Linux
 *
 * Copyright (C) 2002-2008 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * For information see <http://www.kernel.org/pub/linux/utils/net/hdlc/>
 *
 * Sources of information:
 *    Hitachi HD64572 SCA-II User's Manual
 *    PLX Technology Inc. PCI9052 Data Book
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/hdlc.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "hd64572.h"

#undef DEBUG_PKT
#define DEBUG_RINGS

#define PCI200SYN_PLX_SIZE	0x80	/* PLX control window size (128b) */
#define PCI200SYN_SCA_SIZE	0x400	/* SCA window size (1Kb) */
#define MAX_TX_BUFFERS		10

static int pci_clock_freq = 33000000;
#define CLOCK_BASE pci_clock_freq

/*
 *      PLX PCI9052 local configuration and shared runtime registers.
 *      This structure can be used to access 9052 registers (memory mapped).
 */
typedef struct {
	u32 loc_addr_range[4];	/* 00-0Ch : Local Address Ranges */
	u32 loc_rom_range;	/* 10h : Local ROM Range */
	u32 loc_addr_base[4];	/* 14-20h : Local Address Base Addrs */
	u32 loc_rom_base;	/* 24h : Local ROM Base */
	u32 loc_bus_descr[4];	/* 28-34h : Local Bus Descriptors */
	u32 rom_bus_descr;	/* 38h : ROM Bus Descriptor */
	u32 cs_base[4];		/* 3C-48h : Chip Select Base Addrs */
	u32 intr_ctrl_stat;	/* 4Ch : Interrupt Control/Status */
	u32 init_ctrl;		/* 50h : EEPROM ctrl, Init Ctrl, etc */
}plx9052;



typedef struct port_s {
	struct napi_struct napi;
	struct net_device *netdev;
	struct card_s *card;
	spinlock_t lock;	/* TX lock */
	sync_serial_settings settings;
	int rxpart;		/* partial frame received, next frame invalid*/
	unsigned short encoding;
	unsigned short parity;
	u16 rxin;		/* rx ring buffer 'in' pointer */
	u16 txin;		/* tx ring buffer 'in' and 'last' pointers */
	u16 txlast;
	u8 rxs, txs, tmc;	/* SCA registers */
	u8 chan;		/* physical port # - 0 or 1 */
}port_t;



typedef struct card_s {
	u8 __iomem *rambase;	/* buffer memory base (virtual) */
	u8 __iomem *scabase;	/* SCA memory base (virtual) */
	plx9052 __iomem *plxbase;/* PLX registers memory base (virtual) */
	u16 rx_ring_buffers;	/* number of buffers in a ring */
	u16 tx_ring_buffers;
	u16 buff_offset;	/* offset of first buffer of first channel */
	u8 irq;			/* interrupt request level */

	port_t ports[2];
}card_t;


#define get_port(card, port)	     (&card->ports[port])
#define sca_flush(card)		     (sca_in(IER0, card));

static inline void new_memcpy_toio(char __iomem *dest, char *src, int length)
{
	int len;
	do {
		len = length > 256 ? 256 : length;
		memcpy_toio(dest, src, len);
		dest += len;
		src += len;
		length -= len;
		readb(dest);
	} while (len);
}

#undef memcpy_toio
#define memcpy_toio new_memcpy_toio

#include "hd64572.c"


static void pci200_set_iface(port_t *port)
{
	card_t *card = port->card;
	u16 msci = get_msci(port);
	u8 rxs = port->rxs & CLK_BRG_MASK;
	u8 txs = port->txs & CLK_BRG_MASK;

	sca_out(EXS_TES1, (port->chan ? MSCI1_OFFSET : MSCI0_OFFSET) + EXS,
		port->card);
	switch(port->settings.clock_type) {
	case CLOCK_INT:
		rxs |= CLK_BRG; /* BRG output */
		txs |= CLK_PIN_OUT | CLK_TX_RXCLK; /* RX clock */
		break;

	case CLOCK_TXINT:
		rxs |= CLK_LINE; /* RXC input */
		txs |= CLK_PIN_OUT | CLK_BRG; /* BRG output */
		break;

	case CLOCK_TXFROMRX:
		rxs |= CLK_LINE; /* RXC input */
		txs |= CLK_PIN_OUT | CLK_TX_RXCLK; /* RX clock */
		break;

	default:		/* EXTernal clock */
		rxs |= CLK_LINE; /* RXC input */
		txs |= CLK_PIN_OUT | CLK_LINE; /* TXC input */
		break;
	}

	port->rxs = rxs;
	port->txs = txs;
	sca_out(rxs, msci + RXS, card);
	sca_out(txs, msci + TXS, card);
	sca_set_port(port);
}



static int pci200_open(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);

	int result = hdlc_open(dev);
	if (result)
		return result;

	sca_open(dev);
	pci200_set_iface(port);
	sca_flush(port->card);
	return 0;
}



static int pci200_close(struct net_device *dev)
{
	sca_close(dev);
	sca_flush(dev_to_port(dev)->card);
	hdlc_close(dev);
	return 0;
}



static int pci200_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings new_line;
	sync_serial_settings __user *line = ifr->ifr_settings.ifs_ifsu.sync;
	port_t *port = dev_to_port(dev);

#ifdef DEBUG_RINGS
	if (cmd == SIOCDEVPRIVATE) {
		sca_dump_rings(dev);
		return 0;
	}
#endif
	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	switch(ifr->ifr_settings.type) {
	case IF_GET_IFACE:
		ifr->ifr_settings.type = IF_IFACE_V35;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(line, &port->settings, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_V35:
	case IF_IFACE_SYNC_SERIAL:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&new_line, line, size))
			return -EFAULT;

		if (new_line.clock_type != CLOCK_EXT &&
		    new_line.clock_type != CLOCK_TXFROMRX &&
		    new_line.clock_type != CLOCK_INT &&
		    new_line.clock_type != CLOCK_TXINT)
		return -EINVAL;	/* No such clock setting */

		if (new_line.loopback != 0 && new_line.loopback != 1)
			return -EINVAL;

		memcpy(&port->settings, &new_line, size); /* Update settings */
		pci200_set_iface(port);
		sca_flush(port->card);
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}



static void pci200_pci_remove_one(struct pci_dev *pdev)
{
	int i;
	card_t *card = pci_get_drvdata(pdev);

	for (i = 0; i < 2; i++)
		if (card->ports[i].card)
			unregister_hdlc_device(card->ports[i].netdev);

	if (card->irq)
		free_irq(card->irq, card);

	if (card->rambase)
		iounmap(card->rambase);
	if (card->scabase)
		iounmap(card->scabase);
	if (card->plxbase)
		iounmap(card->plxbase);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	if (card->ports[0].netdev)
		free_netdev(card->ports[0].netdev);
	if (card->ports[1].netdev)
		free_netdev(card->ports[1].netdev);
	kfree(card);
}

static const struct net_device_ops pci200_ops = {
	.ndo_open       = pci200_open,
	.ndo_stop       = pci200_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = pci200_ioctl,
};

static int __devinit pci200_pci_init_one(struct pci_dev *pdev,
					 const struct pci_device_id *ent)
{
	card_t *card;
	u32 __iomem *p;
	int i;
	u32 ramsize;
	u32 ramphys;		/* buffer memory base */
	u32 scaphys;		/* SCA memory base */
	u32 plxphys;		/* PLX registers memory base */

	i = pci_enable_device(pdev);
	if (i)
		return i;

	i = pci_request_regions(pdev, "PCI200SYN");
	if (i) {
		pci_disable_device(pdev);
		return i;
	}

	card = kzalloc(sizeof(card_t), GFP_KERNEL);
	if (card == NULL) {
		printk(KERN_ERR "pci200syn: unable to allocate memory\n");
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		return -ENOBUFS;
	}
	pci_set_drvdata(pdev, card);
	card->ports[0].netdev = alloc_hdlcdev(&card->ports[0]);
	card->ports[1].netdev = alloc_hdlcdev(&card->ports[1]);
	if (!card->ports[0].netdev || !card->ports[1].netdev) {
		printk(KERN_ERR "pci200syn: unable to allocate memory\n");
		pci200_pci_remove_one(pdev);
		return -ENOMEM;
	}

	if (pci_resource_len(pdev, 0) != PCI200SYN_PLX_SIZE ||
	    pci_resource_len(pdev, 2) != PCI200SYN_SCA_SIZE ||
	    pci_resource_len(pdev, 3) < 16384) {
		printk(KERN_ERR "pci200syn: invalid card EEPROM parameters\n");
		pci200_pci_remove_one(pdev);
		return -EFAULT;
	}

	plxphys = pci_resource_start(pdev,0) & PCI_BASE_ADDRESS_MEM_MASK;
	card->plxbase = ioremap(plxphys, PCI200SYN_PLX_SIZE);

	scaphys = pci_resource_start(pdev,2) & PCI_BASE_ADDRESS_MEM_MASK;
	card->scabase = ioremap(scaphys, PCI200SYN_SCA_SIZE);

	ramphys = pci_resource_start(pdev,3) & PCI_BASE_ADDRESS_MEM_MASK;
	card->rambase = pci_ioremap_bar(pdev, 3);

	if (card->plxbase == NULL ||
	    card->scabase == NULL ||
	    card->rambase == NULL) {
		printk(KERN_ERR "pci200syn: ioremap() failed\n");
		pci200_pci_remove_one(pdev);
		return -EFAULT;
	}

	/* Reset PLX */
	p = &card->plxbase->init_ctrl;
	writel(readl(p) | 0x40000000, p);
	readl(p);		/* Flush the write - do not use sca_flush */
	udelay(1);

	writel(readl(p) & ~0x40000000, p);
	readl(p);		/* Flush the write - do not use sca_flush */
	udelay(1);

	ramsize = sca_detect_ram(card, card->rambase,
				 pci_resource_len(pdev, 3));

	/* number of TX + RX buffers for one port - this is dual port card */
	i = ramsize / (2 * (sizeof(pkt_desc) + HDLC_MAX_MRU));
	card->tx_ring_buffers = min(i / 2, MAX_TX_BUFFERS);
	card->rx_ring_buffers = i - card->tx_ring_buffers;

	card->buff_offset = 2 * sizeof(pkt_desc) * (card->tx_ring_buffers +
						    card->rx_ring_buffers);

	printk(KERN_INFO "pci200syn: %u KB RAM at 0x%x, IRQ%u, using %u TX +"
	       " %u RX packets rings\n", ramsize / 1024, ramphys,
	       pdev->irq, card->tx_ring_buffers, card->rx_ring_buffers);

	if (pdev->subsystem_device == PCI_DEVICE_ID_PLX_9050) {
		printk(KERN_ERR "Detected PCI200SYN card with old "
		       "configuration data.\n");
		printk(KERN_ERR "See <http://www.kernel.org/pub/"
		       "linux/utils/net/hdlc/pci200syn/> for update.\n");
		printk(KERN_ERR "The card will stop working with"
		       " future versions of Linux if not updated.\n");
	}

	if (card->tx_ring_buffers < 1) {
		printk(KERN_ERR "pci200syn: RAM test failed\n");
		pci200_pci_remove_one(pdev);
		return -EFAULT;
	}

	/* Enable interrupts on the PCI bridge */
	p = &card->plxbase->intr_ctrl_stat;
	writew(readw(p) | 0x0040, p);

	/* Allocate IRQ */
	if (request_irq(pdev->irq, sca_intr, IRQF_SHARED, "pci200syn", card)) {
		printk(KERN_WARNING "pci200syn: could not allocate IRQ%d.\n",
		       pdev->irq);
		pci200_pci_remove_one(pdev);
		return -EBUSY;
	}
	card->irq = pdev->irq;

	sca_init(card, 0);

	for (i = 0; i < 2; i++) {
		port_t *port = &card->ports[i];
		struct net_device *dev = port->netdev;
		hdlc_device *hdlc = dev_to_hdlc(dev);
		port->chan = i;

		spin_lock_init(&port->lock);
		dev->irq = card->irq;
		dev->mem_start = ramphys;
		dev->mem_end = ramphys + ramsize - 1;
		dev->tx_queue_len = 50;
		dev->netdev_ops = &pci200_ops;
		hdlc->attach = sca_attach;
		hdlc->xmit = sca_xmit;
		port->settings.clock_type = CLOCK_EXT;
		port->card = card;
		sca_init_port(port);
		if (register_hdlc_device(dev)) {
			printk(KERN_ERR "pci200syn: unable to register hdlc "
			       "device\n");
			port->card = NULL;
			pci200_pci_remove_one(pdev);
			return -ENOBUFS;
		}

		printk(KERN_INFO "%s: PCI200SYN channel %d\n",
		       dev->name, port->chan);
	}

	sca_flush(card);
	return 0;
}



static struct pci_device_id pci200_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050, PCI_VENDOR_ID_PLX,
	  PCI_DEVICE_ID_PLX_9050, 0, 0, 0 },
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050, PCI_VENDOR_ID_PLX,
	  PCI_DEVICE_ID_PLX_PCI200SYN, 0, 0, 0 },
	{ 0, }
};


static struct pci_driver pci200_pci_driver = {
	.name		= "PCI200SYN",
	.id_table	= pci200_pci_tbl,
	.probe		= pci200_pci_init_one,
	.remove		= pci200_pci_remove_one,
};


static int __init pci200_init_module(void)
{
	if (pci_clock_freq < 1000000 || pci_clock_freq > 80000000) {
		printk(KERN_ERR "pci200syn: Invalid PCI clock frequency\n");
		return -EINVAL;
	}
	return pci_register_driver(&pci200_pci_driver);
}



static void __exit pci200_cleanup_module(void)
{
	pci_unregister_driver(&pci200_pci_driver);
}

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("Goramo PCI200SYN serial port driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, pci200_pci_tbl);
module_param(pci_clock_freq, int, 0444);
MODULE_PARM_DESC(pci_clock_freq, "System PCI clock frequency in Hz");
module_init(pci200_init_module);
module_exit(pci200_cleanup_module);

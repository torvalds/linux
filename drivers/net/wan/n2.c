/*
 * SDL Inc. RISCom/N2 synchronous serial card driver for Linux
 *
 * Copyright (C) 1998-2003 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * For information see <http://www.kernel.org/pub/linux/utils/net/hdlc/>
 *
 * Note: integrated CSU/DSU/DDS are not supported by this driver
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    SDL Inc. PPP/HDLC/CISCO driver
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
#include <asm/io.h>
#include "hd64570.h"


static const char* version = "SDL RISCom/N2 driver version: 1.15";
static const char* devname = "RISCom/N2";

#undef DEBUG_PKT
#define DEBUG_RINGS

#define USE_WINDOWSIZE 16384
#define USE_BUS16BITS 1
#define CLOCK_BASE 9830400	/* 9.8304 MHz */
#define MAX_PAGES      16	/* 16 RAM pages at max */
#define MAX_RAM_SIZE 0x80000	/* 512 KB */
#if MAX_RAM_SIZE > MAX_PAGES * USE_WINDOWSIZE
#undef MAX_RAM_SIZE
#define MAX_RAM_SIZE (MAX_PAGES * USE_WINDOWSIZE)
#endif
#define N2_IOPORTS 0x10
#define NEED_DETECT_RAM
#define NEED_SCA_MSCI_INTR
#define MAX_TX_BUFFERS 10

static char *hw;	/* pointer to hw=xxx command line string */

/* RISCom/N2 Board Registers */

/* PC Control Register */
#define N2_PCR 0
#define PCR_RUNSCA 1     /* Run 64570 */
#define PCR_VPM    2     /* Enable VPM - needed if using RAM above 1 MB */
#define PCR_ENWIN  4     /* Open window */
#define PCR_BUS16  8     /* 16-bit bus */


/* Memory Base Address Register */
#define N2_BAR 2


/* Page Scan Register  */
#define N2_PSR 4
#define WIN16K       0x00
#define WIN32K       0x20
#define WIN64K       0x40
#define PSR_WINBITS  0x60
#define PSR_DMAEN    0x80
#define PSR_PAGEBITS 0x0F


/* Modem Control Reg */
#define N2_MCR 6
#define CLOCK_OUT_PORT1 0x80
#define CLOCK_OUT_PORT0 0x40
#define TX422_PORT1     0x20
#define TX422_PORT0     0x10
#define DSR_PORT1       0x08
#define DSR_PORT0       0x04
#define DTR_PORT1       0x02
#define DTR_PORT0       0x01


typedef struct port_s {
	struct net_device *dev;
	struct card_s *card;
	spinlock_t lock;	/* TX lock */
	sync_serial_settings settings;
	int valid;		/* port enabled */
	int rxpart;		/* partial frame received, next frame invalid*/
	unsigned short encoding;
	unsigned short parity;
	u16 rxin;		/* rx ring buffer 'in' pointer */
	u16 txin;		/* tx ring buffer 'in' and 'last' pointers */
	u16 txlast;
	u8 rxs, txs, tmc;	/* SCA registers */
	u8 phy_node;		/* physical port # - 0 or 1 */
	u8 log_node;		/* logical port # */
}port_t;



typedef struct card_s {
	u8 __iomem *winbase;		/* ISA window base address */
	u32 phy_winbase;	/* ISA physical base address */
	u32 ram_size;		/* number of bytes */
	u16 io;			/* IO Base address */
	u16 buff_offset;	/* offset of first buffer of first channel */
	u16 rx_ring_buffers;	/* number of buffers in a ring */
	u16 tx_ring_buffers;
	u8 irq;			/* IRQ (3-15) */

	port_t ports[2];
	struct card_s *next_card;
}card_t;


static card_t *first_card;
static card_t **new_card = &first_card;


#define sca_reg(reg, card) (0x8000 | (card)->io | \
			    ((reg) & 0x0F) | (((reg) & 0xF0) << 6))
#define sca_in(reg, card)		inb(sca_reg(reg, card))
#define sca_out(value, reg, card)	outb(value, sca_reg(reg, card))
#define sca_inw(reg, card)		inw(sca_reg(reg, card))
#define sca_outw(value, reg, card)	outw(value, sca_reg(reg, card))

#define port_to_card(port)		((port)->card)
#define log_node(port)			((port)->log_node)
#define phy_node(port)			((port)->phy_node)
#define winsize(card)			(USE_WINDOWSIZE)
#define winbase(card)      	     	((card)->winbase)
#define get_port(card, port)		((card)->ports[port].valid ? \
					 &(card)->ports[port] : NULL)


static __inline__ u8 sca_get_page(card_t *card)
{
	return inb(card->io + N2_PSR) & PSR_PAGEBITS;
}


static __inline__ void openwin(card_t *card, u8 page)
{
	u8 psr = inb(card->io + N2_PSR);
	outb((psr & ~PSR_PAGEBITS) | page, card->io + N2_PSR);
}


#include "hd64570.c"


static void n2_set_iface(port_t *port)
{
	card_t *card = port->card;
	int io = card->io;
	u8 mcr = inb(io + N2_MCR);
	u8 msci = get_msci(port);
	u8 rxs = port->rxs & CLK_BRG_MASK;
	u8 txs = port->txs & CLK_BRG_MASK;

	switch(port->settings.clock_type) {
	case CLOCK_INT:
		mcr |= port->phy_node ? CLOCK_OUT_PORT1 : CLOCK_OUT_PORT0;
		rxs |= CLK_BRG_RX; /* BRG output */
		txs |= CLK_RXCLK_TX; /* RX clock */
		break;

	case CLOCK_TXINT:
		mcr |= port->phy_node ? CLOCK_OUT_PORT1 : CLOCK_OUT_PORT0;
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_BRG_TX; /* BRG output */
		break;

	case CLOCK_TXFROMRX:
		mcr |= port->phy_node ? CLOCK_OUT_PORT1 : CLOCK_OUT_PORT0;
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_RXCLK_TX; /* RX clock */
		break;

	default:		/* Clock EXTernal */
		mcr &= port->phy_node ? ~CLOCK_OUT_PORT1 : ~CLOCK_OUT_PORT0;
		rxs |= CLK_LINE_RX; /* RXC input */
		txs |= CLK_LINE_TX; /* TXC input */
	}

	outb(mcr, io + N2_MCR);
	port->rxs = rxs;
	port->txs = txs;
	sca_out(rxs, msci + RXS, card);
	sca_out(txs, msci + TXS, card);
	sca_set_port(port);
}



static int n2_open(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	int io = port->card->io;
	u8 mcr = inb(io + N2_MCR) | (port->phy_node ? TX422_PORT1:TX422_PORT0);
	int result;

	result = hdlc_open(dev);
	if (result)
		return result;

	mcr &= port->phy_node ? ~DTR_PORT1 : ~DTR_PORT0; /* set DTR ON */
	outb(mcr, io + N2_MCR);

	outb(inb(io + N2_PCR) | PCR_ENWIN, io + N2_PCR); /* open window */
	outb(inb(io + N2_PSR) | PSR_DMAEN, io + N2_PSR); /* enable dma */
	sca_open(dev);
	n2_set_iface(port);
	return 0;
}



static int n2_close(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	int io = port->card->io;
	u8 mcr = inb(io+N2_MCR) | (port->phy_node ? TX422_PORT1 : TX422_PORT0);

	sca_close(dev);
	mcr |= port->phy_node ? DTR_PORT1 : DTR_PORT0; /* set DTR OFF */
	outb(mcr, io + N2_MCR);
	hdlc_close(dev);
	return 0;
}



static int n2_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
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
		ifr->ifr_settings.type = IF_IFACE_SYNC_SERIAL;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(line, &port->settings, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL:
		if(!capable(CAP_NET_ADMIN))
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
		n2_set_iface(port);
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}



static void n2_destroy_card(card_t *card)
{
	int cnt;

	for (cnt = 0; cnt < 2; cnt++)
		if (card->ports[cnt].card) {
			struct net_device *dev = port_to_dev(&card->ports[cnt]);
			unregister_hdlc_device(dev);
		}

	if (card->irq)
		free_irq(card->irq, card);

	if (card->winbase) {
		iounmap(card->winbase);
		release_mem_region(card->phy_winbase, USE_WINDOWSIZE);
	}

	if (card->io)
		release_region(card->io, N2_IOPORTS);
	if (card->ports[0].dev)
		free_netdev(card->ports[0].dev);
	if (card->ports[1].dev)
		free_netdev(card->ports[1].dev);
	kfree(card);
}

static const struct net_device_ops n2_ops = {
	.ndo_open       = n2_open,
	.ndo_stop       = n2_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = n2_ioctl,
};

static int __init n2_run(unsigned long io, unsigned long irq,
			 unsigned long winbase, long valid0, long valid1)
{
	card_t *card;
	u8 cnt, pcr;
	int i;

	if (io < 0x200 || io > 0x3FF || (io % N2_IOPORTS) != 0) {
		printk(KERN_ERR "n2: invalid I/O port value\n");
		return -ENODEV;
	}

	if (irq < 3 || irq > 15 || irq == 6) /* FIXME */ {
		printk(KERN_ERR "n2: invalid IRQ value\n");
		return -ENODEV;
	}

	if (winbase < 0xA0000 || winbase > 0xFFFFF || (winbase & 0xFFF) != 0) {
		printk(KERN_ERR "n2: invalid RAM value\n");
		return -ENODEV;
	}

	card = kzalloc(sizeof(card_t), GFP_KERNEL);
	if (card == NULL) {
		printk(KERN_ERR "n2: unable to allocate memory\n");
		return -ENOBUFS;
	}

	card->ports[0].dev = alloc_hdlcdev(&card->ports[0]);
	card->ports[1].dev = alloc_hdlcdev(&card->ports[1]);
	if (!card->ports[0].dev || !card->ports[1].dev) {
		printk(KERN_ERR "n2: unable to allocate memory\n");
		n2_destroy_card(card);
		return -ENOMEM;
	}

	if (!request_region(io, N2_IOPORTS, devname)) {
		printk(KERN_ERR "n2: I/O port region in use\n");
		n2_destroy_card(card);
		return -EBUSY;
	}
	card->io = io;

	if (request_irq(irq, &sca_intr, 0, devname, card)) {
		printk(KERN_ERR "n2: could not allocate IRQ\n");
		n2_destroy_card(card);
		return(-EBUSY);
	}
	card->irq = irq;

	if (!request_mem_region(winbase, USE_WINDOWSIZE, devname)) {
		printk(KERN_ERR "n2: could not request RAM window\n");
		n2_destroy_card(card);
		return(-EBUSY);
	}
	card->phy_winbase = winbase;
	card->winbase = ioremap(winbase, USE_WINDOWSIZE);
	if (!card->winbase) {
		printk(KERN_ERR "n2: ioremap() failed\n");
		n2_destroy_card(card);
		return -EFAULT;
	}

	outb(0, io + N2_PCR);
	outb(winbase >> 12, io + N2_BAR);

	switch (USE_WINDOWSIZE) {
	case 16384:
		outb(WIN16K, io + N2_PSR);
		break;

	case 32768:
		outb(WIN32K, io + N2_PSR);
		break;

	case 65536:
		outb(WIN64K, io + N2_PSR);
		break;

	default:
		printk(KERN_ERR "n2: invalid window size\n");
		n2_destroy_card(card);
		return -ENODEV;
	}

	pcr = PCR_ENWIN | PCR_VPM | (USE_BUS16BITS ? PCR_BUS16 : 0);
	outb(pcr, io + N2_PCR);

	card->ram_size = sca_detect_ram(card, card->winbase, MAX_RAM_SIZE);

	/* number of TX + RX buffers for one port */
	i = card->ram_size / ((valid0 + valid1) * (sizeof(pkt_desc) +
						   HDLC_MAX_MRU));

	card->tx_ring_buffers = min(i / 2, MAX_TX_BUFFERS);
	card->rx_ring_buffers = i - card->tx_ring_buffers;

	card->buff_offset = (valid0 + valid1) * sizeof(pkt_desc) *
		(card->tx_ring_buffers + card->rx_ring_buffers);

	printk(KERN_INFO "n2: RISCom/N2 %u KB RAM, IRQ%u, "
	       "using %u TX + %u RX packets rings\n", card->ram_size / 1024,
	       card->irq, card->tx_ring_buffers, card->rx_ring_buffers);

	if (card->tx_ring_buffers < 1) {
		printk(KERN_ERR "n2: RAM test failed\n");
		n2_destroy_card(card);
		return -EIO;
	}

	pcr |= PCR_RUNSCA;		/* run SCA */
	outb(pcr, io + N2_PCR);
	outb(0, io + N2_MCR);

	sca_init(card, 0);
	for (cnt = 0; cnt < 2; cnt++) {
		port_t *port = &card->ports[cnt];
		struct net_device *dev = port_to_dev(port);
		hdlc_device *hdlc = dev_to_hdlc(dev);

		if ((cnt == 0 && !valid0) || (cnt == 1 && !valid1))
			continue;

		port->phy_node = cnt;
		port->valid = 1;

		if ((cnt == 1) && valid0)
			port->log_node = 1;

		spin_lock_init(&port->lock);
		dev->irq = irq;
		dev->mem_start = winbase;
		dev->mem_end = winbase + USE_WINDOWSIZE - 1;
		dev->tx_queue_len = 50;
		dev->netdev_ops = &n2_ops;
		hdlc->attach = sca_attach;
		hdlc->xmit = sca_xmit;
		port->settings.clock_type = CLOCK_EXT;
		port->card = card;

		if (register_hdlc_device(dev)) {
			printk(KERN_WARNING "n2: unable to register hdlc "
			       "device\n");
			port->card = NULL;
			n2_destroy_card(card);
			return -ENOBUFS;
		}
		sca_init_port(port); /* Set up SCA memory */

		printk(KERN_INFO "%s: RISCom/N2 node %d\n",
		       dev->name, port->phy_node);
	}

	*new_card = card;
	new_card = &card->next_card;

	return 0;
}



static int __init n2_init(void)
{
	if (hw==NULL) {
#ifdef MODULE
		printk(KERN_INFO "n2: no card initialized\n");
#endif
		return -EINVAL;	/* no parameters specified, abort */
	}

	printk(KERN_INFO "%s\n", version);

	do {
		unsigned long io, irq, ram;
		long valid[2] = { 0, 0 }; /* Default = both ports disabled */

		io = simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		irq = simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		ram = simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		while(1) {
			if (*hw == '0' && !valid[0])
				valid[0] = 1; /* Port 0 enabled */
			else if (*hw == '1' && !valid[1])
				valid[1] = 1; /* Port 1 enabled */
			else
				break;
			hw++;
		}

		if (!valid[0] && !valid[1])
			break;	/* at least one port must be used */

		if (*hw == ':' || *hw == '\x0')
			n2_run(io, irq, ram, valid[0], valid[1]);

		if (*hw == '\x0')
			return first_card ? 0 : -EINVAL;
	}while(*hw++ == ':');

	printk(KERN_ERR "n2: invalid hardware parameters\n");
	return first_card ? 0 : -EINVAL;
}


static void __exit n2_cleanup(void)
{
	card_t *card = first_card;

	while (card) {
		card_t *ptr = card;
		card = card->next_card;
		n2_destroy_card(ptr);
	}
}


module_init(n2_init);
module_exit(n2_cleanup);

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("RISCom/N2 serial port driver");
MODULE_LICENSE("GPL v2");
module_param(hw, charp, 0444);
MODULE_PARM_DESC(hw, "io,irq,ram,ports:io,irq,...");

/*
 * Hitachi SCA HD64570 driver for Linux
 *
 * Copyright (C) 1998-2003 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * Source of information: Hitachi HD64570 SCA User's Manual
 *
 * We use the following SCA memory map:
 *
 * Packet buffer descriptor rings - starting from winbase or win0base:
 * rx_ring_buffers * sizeof(pkt_desc) = logical channel #0 RX ring
 * tx_ring_buffers * sizeof(pkt_desc) = logical channel #0 TX ring
 * rx_ring_buffers * sizeof(pkt_desc) = logical channel #1 RX ring (if used)
 * tx_ring_buffers * sizeof(pkt_desc) = logical channel #1 TX ring (if used)
 *
 * Packet data buffers - starting from winbase + buff_offset:
 * rx_ring_buffers * HDLC_MAX_MRU     = logical channel #0 RX buffers
 * tx_ring_buffers * HDLC_MAX_MRU     = logical channel #0 TX buffers
 * rx_ring_buffers * HDLC_MAX_MRU     = logical channel #0 RX buffers (if used)
 * tx_ring_buffers * HDLC_MAX_MRU     = logical channel #0 TX buffers (if used)
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/hdlc.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include "hd64570.h"

#define get_msci(port)	  (phy_node(port) ?   MSCI1_OFFSET :   MSCI0_OFFSET)
#define get_dmac_rx(port) (phy_node(port) ? DMAC1RX_OFFSET : DMAC0RX_OFFSET)
#define get_dmac_tx(port) (phy_node(port) ? DMAC1TX_OFFSET : DMAC0TX_OFFSET)

#define SCA_INTR_MSCI(node)    (node ? 0x10 : 0x01)
#define SCA_INTR_DMAC_RX(node) (node ? 0x20 : 0x02)
#define SCA_INTR_DMAC_TX(node) (node ? 0x40 : 0x04)


static inline struct net_device *port_to_dev(port_t *port)
{
	return port->dev;
}

static inline int sca_intr_status(card_t *card)
{
	u8 result = 0;
	u8 isr0 = sca_in(ISR0, card);
	u8 isr1 = sca_in(ISR1, card);

	if (isr1 & 0x03) result |= SCA_INTR_DMAC_RX(0);
	if (isr1 & 0x0C) result |= SCA_INTR_DMAC_TX(0);
	if (isr1 & 0x30) result |= SCA_INTR_DMAC_RX(1);
	if (isr1 & 0xC0) result |= SCA_INTR_DMAC_TX(1);
	if (isr0 & 0x0F) result |= SCA_INTR_MSCI(0);
	if (isr0 & 0xF0) result |= SCA_INTR_MSCI(1);

	if (!(result & SCA_INTR_DMAC_TX(0)))
		if (sca_in(DSR_TX(0), card) & DSR_EOM)
			result |= SCA_INTR_DMAC_TX(0);
	if (!(result & SCA_INTR_DMAC_TX(1)))
		if (sca_in(DSR_TX(1), card) & DSR_EOM)
			result |= SCA_INTR_DMAC_TX(1);

	return result;
}

static inline port_t* dev_to_port(struct net_device *dev)
{
	return dev_to_hdlc(dev)->priv;
}

static inline u16 next_desc(port_t *port, u16 desc, int transmit)
{
	return (desc + 1) % (transmit ? port_to_card(port)->tx_ring_buffers
			     : port_to_card(port)->rx_ring_buffers);
}


static inline u16 desc_abs_number(port_t *port, u16 desc, int transmit)
{
	u16 rx_buffs = port_to_card(port)->rx_ring_buffers;
	u16 tx_buffs = port_to_card(port)->tx_ring_buffers;

	desc %= (transmit ? tx_buffs : rx_buffs); // called with "X + 1" etc.
	return log_node(port) * (rx_buffs + tx_buffs) +
		transmit * rx_buffs + desc;
}


static inline u16 desc_offset(port_t *port, u16 desc, int transmit)
{
	/* Descriptor offset always fits in 16 bits */
	return desc_abs_number(port, desc, transmit) * sizeof(pkt_desc);
}


static inline pkt_desc __iomem *desc_address(port_t *port, u16 desc,
					     int transmit)
{
#ifdef PAGE0_ALWAYS_MAPPED
	return (pkt_desc __iomem *)(win0base(port_to_card(port))
				    + desc_offset(port, desc, transmit));
#else
	return (pkt_desc __iomem *)(winbase(port_to_card(port))
				    + desc_offset(port, desc, transmit));
#endif
}


static inline u32 buffer_offset(port_t *port, u16 desc, int transmit)
{
	return port_to_card(port)->buff_offset +
		desc_abs_number(port, desc, transmit) * (u32)HDLC_MAX_MRU;
}


static inline void sca_set_carrier(port_t *port)
{
	if (!(sca_in(get_msci(port) + ST3, port_to_card(port)) & ST3_DCD)) {
#ifdef DEBUG_LINK
		printk(KERN_DEBUG "%s: sca_set_carrier on\n",
		       port_to_dev(port)->name);
#endif
		netif_carrier_on(port_to_dev(port));
	} else {
#ifdef DEBUG_LINK
		printk(KERN_DEBUG "%s: sca_set_carrier off\n",
		       port_to_dev(port)->name);
#endif
		netif_carrier_off(port_to_dev(port));
	}
}


static void sca_init_port(port_t *port)
{
	card_t *card = port_to_card(port);
	int transmit, i;

	port->rxin = 0;
	port->txin = 0;
	port->txlast = 0;

#ifndef PAGE0_ALWAYS_MAPPED
	openwin(card, 0);
#endif

	for (transmit = 0; transmit < 2; transmit++) {
		u16 dmac = transmit ? get_dmac_tx(port) : get_dmac_rx(port);
		u16 buffs = transmit ? card->tx_ring_buffers
			: card->rx_ring_buffers;

		for (i = 0; i < buffs; i++) {
			pkt_desc __iomem *desc = desc_address(port, i, transmit);
			u16 chain_off = desc_offset(port, i + 1, transmit);
			u32 buff_off = buffer_offset(port, i, transmit);

			writew(chain_off, &desc->cp);
			writel(buff_off, &desc->bp);
			writew(0, &desc->len);
			writeb(0, &desc->stat);
		}

		/* DMA disable - to halt state */
		sca_out(0, transmit ? DSR_TX(phy_node(port)) :
			DSR_RX(phy_node(port)), card);
		/* software ABORT - to initial state */
		sca_out(DCR_ABORT, transmit ? DCR_TX(phy_node(port)) :
			DCR_RX(phy_node(port)), card);

		/* current desc addr */
		sca_out(0, dmac + CPB, card); /* pointer base */
		sca_outw(desc_offset(port, 0, transmit), dmac + CDAL, card);
		if (!transmit)
			sca_outw(desc_offset(port, buffs - 1, transmit),
				 dmac + EDAL, card);
		else
			sca_outw(desc_offset(port, 0, transmit), dmac + EDAL,
				 card);

		/* clear frame end interrupt counter */
		sca_out(DCR_CLEAR_EOF, transmit ? DCR_TX(phy_node(port)) :
			DCR_RX(phy_node(port)), card);

		if (!transmit) { /* Receive */
			/* set buffer length */
			sca_outw(HDLC_MAX_MRU, dmac + BFLL, card);
			/* Chain mode, Multi-frame */
			sca_out(0x14, DMR_RX(phy_node(port)), card);
			sca_out(DIR_EOME | DIR_BOFE, DIR_RX(phy_node(port)),
				card);
			/* DMA enable */
			sca_out(DSR_DE, DSR_RX(phy_node(port)), card);
		} else {	/* Transmit */
			/* Chain mode, Multi-frame */
			sca_out(0x14, DMR_TX(phy_node(port)), card);
			/* enable underflow interrupts */
			sca_out(DIR_BOFE, DIR_TX(phy_node(port)), card);
		}
	}
	sca_set_carrier(port);
}


#ifdef NEED_SCA_MSCI_INTR
/* MSCI interrupt service */
static inline void sca_msci_intr(port_t *port)
{
	u16 msci = get_msci(port);
	card_t* card = port_to_card(port);
	u8 stat = sca_in(msci + ST1, card); /* read MSCI ST1 status */

	/* Reset MSCI TX underrun and CDCD status bit */
	sca_out(stat & (ST1_UDRN | ST1_CDCD), msci + ST1, card);

	if (stat & ST1_UDRN) {
		/* TX Underrun error detected */
		port_to_dev(port)->stats.tx_errors++;
		port_to_dev(port)->stats.tx_fifo_errors++;
	}

	if (stat & ST1_CDCD)
		sca_set_carrier(port);
}
#endif


static inline void sca_rx(card_t *card, port_t *port, pkt_desc __iomem *desc,
			  u16 rxin)
{
	struct net_device *dev = port_to_dev(port);
	struct sk_buff *skb;
	u16 len;
	u32 buff;
	u32 maxlen;
	u8 page;

	len = readw(&desc->len);
	skb = dev_alloc_skb(len);
	if (!skb) {
		dev->stats.rx_dropped++;
		return;
	}

	buff = buffer_offset(port, rxin, 0);
	page = buff / winsize(card);
	buff = buff % winsize(card);
	maxlen = winsize(card) - buff;

	openwin(card, page);

	if (len > maxlen) {
		memcpy_fromio(skb->data, winbase(card) + buff, maxlen);
		openwin(card, page + 1);
		memcpy_fromio(skb->data + maxlen, winbase(card), len - maxlen);
	} else
		memcpy_fromio(skb->data, winbase(card) + buff, len);

#ifndef PAGE0_ALWAYS_MAPPED
	openwin(card, 0);	/* select pkt_desc table page back */
#endif
	skb_put(skb, len);
#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s RX(%i):", dev->name, skb->len);
	debug_frame(skb);
#endif
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;
	skb->protocol = hdlc_type_trans(skb, dev);
	netif_rx(skb);
}


/* Receive DMA interrupt service */
static inline void sca_rx_intr(port_t *port)
{
	struct net_device *dev = port_to_dev(port);
	u16 dmac = get_dmac_rx(port);
	card_t *card = port_to_card(port);
	u8 stat = sca_in(DSR_RX(phy_node(port)), card); /* read DMA Status */

	/* Reset DSR status bits */
	sca_out((stat & (DSR_EOT | DSR_EOM | DSR_BOF | DSR_COF)) | DSR_DWE,
		DSR_RX(phy_node(port)), card);

	if (stat & DSR_BOF)
		/* Dropped one or more frames */
		dev->stats.rx_over_errors++;

	while (1) {
		u32 desc_off = desc_offset(port, port->rxin, 0);
		pkt_desc __iomem *desc;
		u32 cda = sca_inw(dmac + CDAL, card);

		if ((cda >= desc_off) && (cda < desc_off + sizeof(pkt_desc)))
			break;	/* No frame received */

		desc = desc_address(port, port->rxin, 0);
		stat = readb(&desc->stat);
		if (!(stat & ST_RX_EOM))
			port->rxpart = 1; /* partial frame received */
		else if ((stat & ST_ERROR_MASK) || port->rxpart) {
			dev->stats.rx_errors++;
			if (stat & ST_RX_OVERRUN)
				dev->stats.rx_fifo_errors++;
			else if ((stat & (ST_RX_SHORT | ST_RX_ABORT |
					  ST_RX_RESBIT)) || port->rxpart)
				dev->stats.rx_frame_errors++;
			else if (stat & ST_RX_CRC)
				dev->stats.rx_crc_errors++;
			if (stat & ST_RX_EOM)
				port->rxpart = 0; /* received last fragment */
		} else
			sca_rx(card, port, desc, port->rxin);

		/* Set new error descriptor address */
		sca_outw(desc_off, dmac + EDAL, card);
		port->rxin = next_desc(port, port->rxin, 0);
	}

	/* make sure RX DMA is enabled */
	sca_out(DSR_DE, DSR_RX(phy_node(port)), card);
}


/* Transmit DMA interrupt service */
static inline void sca_tx_intr(port_t *port)
{
	struct net_device *dev = port_to_dev(port);
	u16 dmac = get_dmac_tx(port);
	card_t* card = port_to_card(port);
	u8 stat;

	spin_lock(&port->lock);

	stat = sca_in(DSR_TX(phy_node(port)), card); /* read DMA Status */

	/* Reset DSR status bits */
	sca_out((stat & (DSR_EOT | DSR_EOM | DSR_BOF | DSR_COF)) | DSR_DWE,
		DSR_TX(phy_node(port)), card);

	while (1) {
		pkt_desc __iomem *desc;

		u32 desc_off = desc_offset(port, port->txlast, 1);
		u32 cda = sca_inw(dmac + CDAL, card);
		if ((cda >= desc_off) && (cda < desc_off + sizeof(pkt_desc)))
			break;	/* Transmitter is/will_be sending this frame */

		desc = desc_address(port, port->txlast, 1);
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += readw(&desc->len);
		writeb(0, &desc->stat);	/* Free descriptor */
		port->txlast = next_desc(port, port->txlast, 1);
	}

	netif_wake_queue(dev);
	spin_unlock(&port->lock);
}


static irqreturn_t sca_intr(int irq, void* dev_id)
{
	card_t *card = dev_id;
	int i;
	u8 stat;
	int handled = 0;
	u8 page = sca_get_page(card);

	while((stat = sca_intr_status(card)) != 0) {
		handled = 1;
		for (i = 0; i < 2; i++) {
			port_t *port = get_port(card, i);
			if (port) {
				if (stat & SCA_INTR_MSCI(i))
					sca_msci_intr(port);

				if (stat & SCA_INTR_DMAC_RX(i))
					sca_rx_intr(port);

				if (stat & SCA_INTR_DMAC_TX(i))
					sca_tx_intr(port);
			}
		}
	}

	openwin(card, page);		/* Restore original page */
	return IRQ_RETVAL(handled);
}


static void sca_set_port(port_t *port)
{
	card_t* card = port_to_card(port);
	u16 msci = get_msci(port);
	u8 md2 = sca_in(msci + MD2, card);
	unsigned int tmc, br = 10, brv = 1024;


	if (port->settings.clock_rate > 0) {
		/* Try lower br for better accuracy*/
		do {
			br--;
			brv >>= 1; /* brv = 2^9 = 512 max in specs */

			/* Baud Rate = CLOCK_BASE / TMC / 2^BR */
			tmc = CLOCK_BASE / brv / port->settings.clock_rate;
		}while (br > 1 && tmc <= 128);

		if (tmc < 1) {
			tmc = 1;
			br = 0;	/* For baud=CLOCK_BASE we use tmc=1 br=0 */
			brv = 1;
		} else if (tmc > 255)
			tmc = 256; /* tmc=0 means 256 - low baud rates */

		port->settings.clock_rate = CLOCK_BASE / brv / tmc;
	} else {
		br = 9; /* Minimum clock rate */
		tmc = 256;	/* 8bit = 0 */
		port->settings.clock_rate = CLOCK_BASE / (256 * 512);
	}

	port->rxs = (port->rxs & ~CLK_BRG_MASK) | br;
	port->txs = (port->txs & ~CLK_BRG_MASK) | br;
	port->tmc = tmc;

	/* baud divisor - time constant*/
	sca_out(port->tmc, msci + TMC, card);

	/* Set BRG bits */
	sca_out(port->rxs, msci + RXS, card);
	sca_out(port->txs, msci + TXS, card);

	if (port->settings.loopback)
		md2 |= MD2_LOOPBACK;
	else
		md2 &= ~MD2_LOOPBACK;

	sca_out(md2, msci + MD2, card);

}


static void sca_open(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	card_t* card = port_to_card(port);
	u16 msci = get_msci(port);
	u8 md0, md2;

	switch(port->encoding) {
	case ENCODING_NRZ:	md2 = MD2_NRZ;		break;
	case ENCODING_NRZI:	md2 = MD2_NRZI;		break;
	case ENCODING_FM_MARK:	md2 = MD2_FM_MARK;	break;
	case ENCODING_FM_SPACE:	md2 = MD2_FM_SPACE;	break;
	default:		md2 = MD2_MANCHESTER;
	}

	if (port->settings.loopback)
		md2 |= MD2_LOOPBACK;

	switch(port->parity) {
	case PARITY_CRC16_PR0:	     md0 = MD0_HDLC | MD0_CRC_16_0;  break;
	case PARITY_CRC16_PR1:	     md0 = MD0_HDLC | MD0_CRC_16;    break;
	case PARITY_CRC16_PR0_CCITT: md0 = MD0_HDLC | MD0_CRC_ITU_0; break;
	case PARITY_CRC16_PR1_CCITT: md0 = MD0_HDLC | MD0_CRC_ITU;   break;
	default:		     md0 = MD0_HDLC | MD0_CRC_NONE;
	}

	sca_out(CMD_RESET, msci + CMD, card);
	sca_out(md0, msci + MD0, card);
	sca_out(0x00, msci + MD1, card); /* no address field check */
	sca_out(md2, msci + MD2, card);
	sca_out(0x7E, msci + IDL, card); /* flag character 0x7E */
	sca_out(CTL_IDLE, msci + CTL, card);

	/* Allow at least 8 bytes before requesting RX DMA operation */
	/* TX with higher priority and possibly with shorter transfers */
	sca_out(0x07, msci + RRC, card); /* +1=RXRDY/DMA activation condition*/
	sca_out(0x10, msci + TRC0, card); /* = TXRDY/DMA activation condition*/
	sca_out(0x14, msci + TRC1, card); /* +1=TXRDY/DMA deactiv condition */

/* We're using the following interrupts:
   - TXINT (DMAC completed all transmisions, underrun or DCD change)
   - all DMA interrupts
*/
	sca_set_carrier(port);

	/* MSCI TX INT and RX INT A IRQ enable */
	sca_out(IE0_TXINT | IE0_RXINTA, msci + IE0, card);
	sca_out(IE1_UDRN | IE1_CDCD, msci + IE1, card);
	sca_out(sca_in(IER0, card) | (phy_node(port) ? 0xC0 : 0x0C),
		IER0, card); /* TXINT and RXINT */
	/* enable DMA IRQ */
	sca_out(sca_in(IER1, card) | (phy_node(port) ? 0xF0 : 0x0F),
		IER1, card);

	sca_out(port->tmc, msci + TMC, card); /* Restore registers */
	sca_out(port->rxs, msci + RXS, card);
	sca_out(port->txs, msci + TXS, card);
	sca_out(CMD_TX_ENABLE, msci + CMD, card);
	sca_out(CMD_RX_ENABLE, msci + CMD, card);

	netif_start_queue(dev);
}


static void sca_close(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	card_t* card = port_to_card(port);

	/* reset channel */
	sca_out(CMD_RESET, get_msci(port) + CMD, port_to_card(port));
	/* disable MSCI interrupts */
	sca_out(sca_in(IER0, card) & (phy_node(port) ? 0x0F : 0xF0),
		IER0, card);
	/* disable DMA interrupts */
	sca_out(sca_in(IER1, card) & (phy_node(port) ? 0x0F : 0xF0),
		IER1, card);

	netif_stop_queue(dev);
}


static int sca_attach(struct net_device *dev, unsigned short encoding,
		      unsigned short parity)
{
	if (encoding != ENCODING_NRZ &&
	    encoding != ENCODING_NRZI &&
	    encoding != ENCODING_FM_MARK &&
	    encoding != ENCODING_FM_SPACE &&
	    encoding != ENCODING_MANCHESTER)
		return -EINVAL;

	if (parity != PARITY_NONE &&
	    parity != PARITY_CRC16_PR0 &&
	    parity != PARITY_CRC16_PR1 &&
	    parity != PARITY_CRC16_PR0_CCITT &&
	    parity != PARITY_CRC16_PR1_CCITT)
		return -EINVAL;

	dev_to_port(dev)->encoding = encoding;
	dev_to_port(dev)->parity = parity;
	return 0;
}


#ifdef DEBUG_RINGS
static void sca_dump_rings(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	card_t *card = port_to_card(port);
	u16 cnt;
#ifndef PAGE0_ALWAYS_MAPPED
	u8 page = sca_get_page(card);

	openwin(card, 0);
#endif

	printk(KERN_DEBUG "RX ring: CDA=%u EDA=%u DSR=%02X in=%u %sactive",
	       sca_inw(get_dmac_rx(port) + CDAL, card),
	       sca_inw(get_dmac_rx(port) + EDAL, card),
	       sca_in(DSR_RX(phy_node(port)), card), port->rxin,
	       sca_in(DSR_RX(phy_node(port)), card) & DSR_DE ? "" : "in");
	for (cnt = 0; cnt < port_to_card(port)->rx_ring_buffers; cnt++)
		printk(" %02X", readb(&(desc_address(port, cnt, 0)->stat)));
	printk(KERN_CONT "\n");

	printk(KERN_DEBUG "TX ring: CDA=%u EDA=%u DSR=%02X in=%u "
	       "last=%u %sactive",
	       sca_inw(get_dmac_tx(port) + CDAL, card),
	       sca_inw(get_dmac_tx(port) + EDAL, card),
	       sca_in(DSR_TX(phy_node(port)), card), port->txin, port->txlast,
	       sca_in(DSR_TX(phy_node(port)), card) & DSR_DE ? "" : "in");

	for (cnt = 0; cnt < port_to_card(port)->tx_ring_buffers; cnt++)
		printk(" %02X", readb(&(desc_address(port, cnt, 1)->stat)));
	printk("\n");

	printk(KERN_DEBUG "MSCI: MD: %02x %02x %02x, ST: %02x %02x %02x %02x,"
	       " FST: %02x CST: %02x %02x\n",
	       sca_in(get_msci(port) + MD0, card),
	       sca_in(get_msci(port) + MD1, card),
	       sca_in(get_msci(port) + MD2, card),
	       sca_in(get_msci(port) + ST0, card),
	       sca_in(get_msci(port) + ST1, card),
	       sca_in(get_msci(port) + ST2, card),
	       sca_in(get_msci(port) + ST3, card),
	       sca_in(get_msci(port) + FST, card),
	       sca_in(get_msci(port) + CST0, card),
	       sca_in(get_msci(port) + CST1, card));

	printk(KERN_DEBUG "ISR: %02x %02x %02x\n", sca_in(ISR0, card),
	       sca_in(ISR1, card), sca_in(ISR2, card));

#ifndef PAGE0_ALWAYS_MAPPED
	openwin(card, page); /* Restore original page */
#endif
}
#endif /* DEBUG_RINGS */


static int sca_xmit(struct sk_buff *skb, struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	card_t *card = port_to_card(port);
	pkt_desc __iomem *desc;
	u32 buff, len;
	u8 page;
	u32 maxlen;

	spin_lock_irq(&port->lock);

	desc = desc_address(port, port->txin + 1, 1);
	BUG_ON(readb(&desc->stat)); /* previous xmit should stop queue */

#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s TX(%i):", dev->name, skb->len);
	debug_frame(skb);
#endif

	desc = desc_address(port, port->txin, 1);
	buff = buffer_offset(port, port->txin, 1);
	len = skb->len;
	page = buff / winsize(card);
	buff = buff % winsize(card);
	maxlen = winsize(card) - buff;

	openwin(card, page);
	if (len > maxlen) {
		memcpy_toio(winbase(card) + buff, skb->data, maxlen);
		openwin(card, page + 1);
		memcpy_toio(winbase(card), skb->data + maxlen, len - maxlen);
	} else
		memcpy_toio(winbase(card) + buff, skb->data, len);

#ifndef PAGE0_ALWAYS_MAPPED
	openwin(card, 0);	/* select pkt_desc table page back */
#endif
	writew(len, &desc->len);
	writeb(ST_TX_EOM, &desc->stat);
	dev->trans_start = jiffies;

	port->txin = next_desc(port, port->txin, 1);
	sca_outw(desc_offset(port, port->txin, 1),
		 get_dmac_tx(port) + EDAL, card);

	sca_out(DSR_DE, DSR_TX(phy_node(port)), card); /* Enable TX DMA */

	desc = desc_address(port, port->txin + 1, 1);
	if (readb(&desc->stat)) /* allow 1 packet gap */
		netif_stop_queue(dev);

	spin_unlock_irq(&port->lock);

	dev_kfree_skb(skb);
	return 0;
}


#ifdef NEED_DETECT_RAM
static u32 __devinit sca_detect_ram(card_t *card, u8 __iomem *rambase,
				    u32 ramsize)
{
	/* Round RAM size to 32 bits, fill from end to start */
	u32 i = ramsize &= ~3;
	u32 size = winsize(card);

	openwin(card, (i - 4) / size); /* select last window */

	do {
		i -= 4;
		if ((i + 4) % size == 0)
			openwin(card, i / size);
		writel(i ^ 0x12345678, rambase + i % size);
	} while (i > 0);

	for (i = 0; i < ramsize ; i += 4) {
		if (i % size == 0)
			openwin(card, i / size);

		if (readl(rambase + i % size) != (i ^ 0x12345678))
			break;
	}

	return i;
}
#endif /* NEED_DETECT_RAM */


static void __devinit sca_init(card_t *card, int wait_states)
{
	sca_out(wait_states, WCRL, card); /* Wait Control */
	sca_out(wait_states, WCRM, card);
	sca_out(wait_states, WCRH, card);

	sca_out(0, DMER, card);	/* DMA Master disable */
	sca_out(0x03, PCR, card); /* DMA priority */
	sca_out(0, DSR_RX(0), card); /* DMA disable - to halt state */
	sca_out(0, DSR_TX(0), card);
	sca_out(0, DSR_RX(1), card);
	sca_out(0, DSR_TX(1), card);
	sca_out(DMER_DME, DMER, card); /* DMA Master enable */
}

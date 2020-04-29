// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hitachi (now Renesas) SCA-II HD64572 driver for Linux
 *
 * Copyright (C) 1998-2008 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * Source of information: HD64572 SCA-II User's Manual
 *
 * We use the following SCA memory map:
 *
 * Packet buffer descriptor rings - starting from card->rambase:
 * rx_ring_buffers * sizeof(pkt_desc) = logical channel #0 RX ring
 * tx_ring_buffers * sizeof(pkt_desc) = logical channel #0 TX ring
 * rx_ring_buffers * sizeof(pkt_desc) = logical channel #1 RX ring (if used)
 * tx_ring_buffers * sizeof(pkt_desc) = logical channel #1 TX ring (if used)
 *
 * Packet data buffers - starting from card->rambase + buff_offset:
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
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include "hd64572.h"

#define NAPI_WEIGHT		16

#define get_msci(port)	  (port->chan ?   MSCI1_OFFSET :   MSCI0_OFFSET)
#define get_dmac_rx(port) (port->chan ? DMAC1RX_OFFSET : DMAC0RX_OFFSET)
#define get_dmac_tx(port) (port->chan ? DMAC1TX_OFFSET : DMAC0TX_OFFSET)

#define sca_in(reg, card)	     readb(card->scabase + (reg))
#define sca_out(value, reg, card)    writeb(value, card->scabase + (reg))
#define sca_inw(reg, card)	     readw(card->scabase + (reg))
#define sca_outw(value, reg, card)   writew(value, card->scabase + (reg))
#define sca_inl(reg, card)	     readl(card->scabase + (reg))
#define sca_outl(value, reg, card)   writel(value, card->scabase + (reg))

static int sca_poll(struct napi_struct *napi, int budget);

static inline port_t* dev_to_port(struct net_device *dev)
{
	return dev_to_hdlc(dev)->priv;
}

static inline void enable_intr(port_t *port)
{
	/* enable DMIB and MSCI RXINTA interrupts */
	sca_outl(sca_inl(IER0, port->card) |
		 (port->chan ? 0x08002200 : 0x00080022), IER0, port->card);
}

static inline void disable_intr(port_t *port)
{
	sca_outl(sca_inl(IER0, port->card) &
		 (port->chan ? 0x00FF00FF : 0xFF00FF00), IER0, port->card);
}

static inline u16 desc_abs_number(port_t *port, u16 desc, int transmit)
{
	u16 rx_buffs = port->card->rx_ring_buffers;
	u16 tx_buffs = port->card->tx_ring_buffers;

	desc %= (transmit ? tx_buffs : rx_buffs); // called with "X + 1" etc.
	return port->chan * (rx_buffs + tx_buffs) + transmit * rx_buffs + desc;
}


static inline u16 desc_offset(port_t *port, u16 desc, int transmit)
{
	/* Descriptor offset always fits in 16 bits */
	return desc_abs_number(port, desc, transmit) * sizeof(pkt_desc);
}


static inline pkt_desc __iomem *desc_address(port_t *port, u16 desc,
					     int transmit)
{
	return (pkt_desc __iomem *)(port->card->rambase +
				    desc_offset(port, desc, transmit));
}


static inline u32 buffer_offset(port_t *port, u16 desc, int transmit)
{
	return port->card->buff_offset +
		desc_abs_number(port, desc, transmit) * (u32)HDLC_MAX_MRU;
}


static inline void sca_set_carrier(port_t *port)
{
	if (!(sca_in(get_msci(port) + ST3, port->card) & ST3_DCD)) {
#ifdef DEBUG_LINK
		printk(KERN_DEBUG "%s: sca_set_carrier on\n",
		       port->netdev.name);
#endif
		netif_carrier_on(port->netdev);
	} else {
#ifdef DEBUG_LINK
		printk(KERN_DEBUG "%s: sca_set_carrier off\n",
		       port->netdev.name);
#endif
		netif_carrier_off(port->netdev);
	}
}


static void sca_init_port(port_t *port)
{
	card_t *card = port->card;
	u16 dmac_rx = get_dmac_rx(port), dmac_tx = get_dmac_tx(port);
	int transmit, i;

	port->rxin = 0;
	port->txin = 0;
	port->txlast = 0;

	for (transmit = 0; transmit < 2; transmit++) {
		u16 buffs = transmit ? card->tx_ring_buffers
			: card->rx_ring_buffers;

		for (i = 0; i < buffs; i++) {
			pkt_desc __iomem *desc = desc_address(port, i, transmit);
			u16 chain_off = desc_offset(port, i + 1, transmit);
			u32 buff_off = buffer_offset(port, i, transmit);

			writel(chain_off, &desc->cp);
			writel(buff_off, &desc->bp);
			writew(0, &desc->len);
			writeb(0, &desc->stat);
		}
	}

	/* DMA disable - to halt state */
	sca_out(0, DSR_RX(port->chan), card);
	sca_out(0, DSR_TX(port->chan), card);

	/* software ABORT - to initial state */
	sca_out(DCR_ABORT, DCR_RX(port->chan), card);
	sca_out(DCR_ABORT, DCR_TX(port->chan), card);

	/* current desc addr */
	sca_outl(desc_offset(port, 0, 0), dmac_rx + CDAL, card);
	sca_outl(desc_offset(port, card->tx_ring_buffers - 1, 0),
		 dmac_rx + EDAL, card);
	sca_outl(desc_offset(port, 0, 1), dmac_tx + CDAL, card);
	sca_outl(desc_offset(port, 0, 1), dmac_tx + EDAL, card);

	/* clear frame end interrupt counter */
	sca_out(DCR_CLEAR_EOF, DCR_RX(port->chan), card);
	sca_out(DCR_CLEAR_EOF, DCR_TX(port->chan), card);

	/* Receive */
	sca_outw(HDLC_MAX_MRU, dmac_rx + BFLL, card); /* set buffer length */
	sca_out(0x14, DMR_RX(port->chan), card); /* Chain mode, Multi-frame */
	sca_out(DIR_EOME, DIR_RX(port->chan), card); /* enable interrupts */
	sca_out(DSR_DE, DSR_RX(port->chan), card); /* DMA enable */

	/* Transmit */
	sca_out(0x14, DMR_TX(port->chan), card); /* Chain mode, Multi-frame */
	sca_out(DIR_EOME, DIR_TX(port->chan), card); /* enable interrupts */

	sca_set_carrier(port);
	netif_napi_add(port->netdev, &port->napi, sca_poll, NAPI_WEIGHT);
}


/* MSCI interrupt service */
static inline void sca_msci_intr(port_t *port)
{
	u16 msci = get_msci(port);
	card_t* card = port->card;

	if (sca_in(msci + ST1, card) & ST1_CDCD) {
		/* Reset MSCI CDCD status bit */
		sca_out(ST1_CDCD, msci + ST1, card);
		sca_set_carrier(port);
	}
}


static inline void sca_rx(card_t *card, port_t *port, pkt_desc __iomem *desc,
			  u16 rxin)
{
	struct net_device *dev = port->netdev;
	struct sk_buff *skb;
	u16 len;
	u32 buff;

	len = readw(&desc->len);
	skb = dev_alloc_skb(len);
	if (!skb) {
		dev->stats.rx_dropped++;
		return;
	}

	buff = buffer_offset(port, rxin, 0);
	memcpy_fromio(skb->data, card->rambase + buff, len);

	skb_put(skb, len);
#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s RX(%i):", dev->name, skb->len);
	debug_frame(skb);
#endif
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;
	skb->protocol = hdlc_type_trans(skb, dev);
	netif_receive_skb(skb);
}


/* Receive DMA service */
static inline int sca_rx_done(port_t *port, int budget)
{
	struct net_device *dev = port->netdev;
	u16 dmac = get_dmac_rx(port);
	card_t *card = port->card;
	u8 stat = sca_in(DSR_RX(port->chan), card); /* read DMA Status */
	int received = 0;

	/* Reset DSR status bits */
	sca_out((stat & (DSR_EOT | DSR_EOM | DSR_BOF | DSR_COF)) | DSR_DWE,
		DSR_RX(port->chan), card);

	if (stat & DSR_BOF)
		/* Dropped one or more frames */
		dev->stats.rx_over_errors++;

	while (received < budget) {
		u32 desc_off = desc_offset(port, port->rxin, 0);
		pkt_desc __iomem *desc;
		u32 cda = sca_inl(dmac + CDAL, card);

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
		} else {
			sca_rx(card, port, desc, port->rxin);
			received++;
		}

		/* Set new error descriptor address */
		sca_outl(desc_off, dmac + EDAL, card);
		port->rxin = (port->rxin + 1) % card->rx_ring_buffers;
	}

	/* make sure RX DMA is enabled */
	sca_out(DSR_DE, DSR_RX(port->chan), card);
	return received;
}


/* Transmit DMA service */
static inline void sca_tx_done(port_t *port)
{
	struct net_device *dev = port->netdev;
	card_t* card = port->card;
	u8 stat;
	unsigned count = 0;

	spin_lock(&port->lock);

	stat = sca_in(DSR_TX(port->chan), card); /* read DMA Status */

	/* Reset DSR status bits */
	sca_out((stat & (DSR_EOT | DSR_EOM | DSR_BOF | DSR_COF)) | DSR_DWE,
		DSR_TX(port->chan), card);

	while (1) {
		pkt_desc __iomem *desc = desc_address(port, port->txlast, 1);
		u8 stat = readb(&desc->stat);

		if (!(stat & ST_TX_OWNRSHP))
			break; /* not yet transmitted */
		if (stat & ST_TX_UNDRRUN) {
			dev->stats.tx_errors++;
			dev->stats.tx_fifo_errors++;
		} else {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += readw(&desc->len);
		}
		writeb(0, &desc->stat);	/* Free descriptor */
		count++;
		port->txlast = (port->txlast + 1) % card->tx_ring_buffers;
	}

	if (count)
		netif_wake_queue(dev);
	spin_unlock(&port->lock);
}


static int sca_poll(struct napi_struct *napi, int budget)
{
	port_t *port = container_of(napi, port_t, napi);
	u32 isr0 = sca_inl(ISR0, port->card);
	int received = 0;

	if (isr0 & (port->chan ? 0x08000000 : 0x00080000))
		sca_msci_intr(port);

	if (isr0 & (port->chan ? 0x00002000 : 0x00000020))
		sca_tx_done(port);

	if (isr0 & (port->chan ? 0x00000200 : 0x00000002))
		received = sca_rx_done(port, budget);

	if (received < budget) {
		napi_complete_done(napi, received);
		enable_intr(port);
	}

	return received;
}

static irqreturn_t sca_intr(int irq, void *dev_id)
{
	card_t *card = dev_id;
	u32 isr0 = sca_inl(ISR0, card);
	int i, handled = 0;

	for (i = 0; i < 2; i++) {
		port_t *port = get_port(card, i);
		if (port && (isr0 & (i ? 0x08002200 : 0x00080022))) {
			handled = 1;
			disable_intr(port);
			napi_schedule(&port->napi);
		}
	}

	return IRQ_RETVAL(handled);
}


static void sca_set_port(port_t *port)
{
	card_t* card = port->card;
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
	sca_out(port->tmc, msci + TMCR, card);
	sca_out(port->tmc, msci + TMCT, card);

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
	card_t* card = port->card;
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
	case PARITY_CRC32_PR1_CCITT: md0 = MD0_HDLC | MD0_CRC_ITU32; break;
	case PARITY_CRC16_PR1_CCITT: md0 = MD0_HDLC | MD0_CRC_ITU;   break;
	default:		     md0 = MD0_HDLC | MD0_CRC_NONE;
	}

	sca_out(CMD_RESET, msci + CMD, card);
	sca_out(md0, msci + MD0, card);
	sca_out(0x00, msci + MD1, card); /* no address field check */
	sca_out(md2, msci + MD2, card);
	sca_out(0x7E, msci + IDL, card); /* flag character 0x7E */
	/* Skip the rest of underrun frame */
	sca_out(CTL_IDLE | CTL_URCT | CTL_URSKP, msci + CTL, card);
	sca_out(0x0F, msci + RNR, card); /* +1=RX DMA activation condition */
	sca_out(0x3C, msci + TFS, card); /* +1 = TX start */
	sca_out(0x38, msci + TCR, card); /* =Critical TX DMA activ condition */
	sca_out(0x38, msci + TNR0, card); /* =TX DMA activation condition */
	sca_out(0x3F, msci + TNR1, card); /* +1=TX DMA deactivation condition*/

/* We're using the following interrupts:
   - RXINTA (DCD changes only)
   - DMIB (EOM - single frame transfer complete)
*/
	sca_outl(IE0_RXINTA | IE0_CDCD, msci + IE0, card);

	sca_out(port->tmc, msci + TMCR, card);
	sca_out(port->tmc, msci + TMCT, card);
	sca_out(port->rxs, msci + RXS, card);
	sca_out(port->txs, msci + TXS, card);
	sca_out(CMD_TX_ENABLE, msci + CMD, card);
	sca_out(CMD_RX_ENABLE, msci + CMD, card);

	sca_set_carrier(port);
	enable_intr(port);
	napi_enable(&port->napi);
	netif_start_queue(dev);
}


static void sca_close(struct net_device *dev)
{
	port_t *port = dev_to_port(dev);

	/* reset channel */
	sca_out(CMD_RESET, get_msci(port) + CMD, port->card);
	disable_intr(port);
	napi_disable(&port->napi);
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
	    parity != PARITY_CRC32_PR1_CCITT &&
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
	card_t *card = port->card;
	u16 cnt;

	printk(KERN_DEBUG "RX ring: CDA=%u EDA=%u DSR=%02X in=%u %sactive",
	       sca_inl(get_dmac_rx(port) + CDAL, card),
	       sca_inl(get_dmac_rx(port) + EDAL, card),
	       sca_in(DSR_RX(port->chan), card), port->rxin,
	       sca_in(DSR_RX(port->chan), card) & DSR_DE ? "" : "in");
	for (cnt = 0; cnt < port->card->rx_ring_buffers; cnt++)
		pr_cont(" %02X", readb(&(desc_address(port, cnt, 0)->stat)));
	pr_cont("\n");

	printk(KERN_DEBUG "TX ring: CDA=%u EDA=%u DSR=%02X in=%u "
	       "last=%u %sactive",
	       sca_inl(get_dmac_tx(port) + CDAL, card),
	       sca_inl(get_dmac_tx(port) + EDAL, card),
	       sca_in(DSR_TX(port->chan), card), port->txin, port->txlast,
	       sca_in(DSR_TX(port->chan), card) & DSR_DE ? "" : "in");

	for (cnt = 0; cnt < port->card->tx_ring_buffers; cnt++)
		pr_cont(" %02X", readb(&(desc_address(port, cnt, 1)->stat)));
	pr_cont("\n");

	printk(KERN_DEBUG "MSCI: MD: %02x %02x %02x,"
	       " ST: %02x %02x %02x %02x %02x, FST: %02x CST: %02x %02x\n",
	       sca_in(get_msci(port) + MD0, card),
	       sca_in(get_msci(port) + MD1, card),
	       sca_in(get_msci(port) + MD2, card),
	       sca_in(get_msci(port) + ST0, card),
	       sca_in(get_msci(port) + ST1, card),
	       sca_in(get_msci(port) + ST2, card),
	       sca_in(get_msci(port) + ST3, card),
	       sca_in(get_msci(port) + ST4, card),
	       sca_in(get_msci(port) + FST, card),
	       sca_in(get_msci(port) + CST0, card),
	       sca_in(get_msci(port) + CST1, card));

	printk(KERN_DEBUG "ILAR: %02x ISR: %08x %08x\n", sca_in(ILAR, card),
	       sca_inl(ISR0, card), sca_inl(ISR1, card));
}
#endif /* DEBUG_RINGS */


static netdev_tx_t sca_xmit(struct sk_buff *skb, struct net_device *dev)
{
	port_t *port = dev_to_port(dev);
	card_t *card = port->card;
	pkt_desc __iomem *desc;
	u32 buff, len;

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
	memcpy_toio(card->rambase + buff, skb->data, len);

	writew(len, &desc->len);
	writeb(ST_TX_EOM, &desc->stat);

	port->txin = (port->txin + 1) % card->tx_ring_buffers;
	sca_outl(desc_offset(port, port->txin, 1),
		 get_dmac_tx(port) + EDAL, card);

	sca_out(DSR_DE, DSR_TX(port->chan), card); /* Enable TX DMA */

	desc = desc_address(port, port->txin + 1, 1);
	if (readb(&desc->stat)) /* allow 1 packet gap */
		netif_stop_queue(dev);

	spin_unlock_irq(&port->lock);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}


static u32 sca_detect_ram(card_t *card, u8 __iomem *rambase, u32 ramsize)
{
	/* Round RAM size to 32 bits, fill from end to start */
	u32 i = ramsize &= ~3;

	do {
		i -= 4;
		writel(i ^ 0x12345678, rambase + i);
	} while (i > 0);

	for (i = 0; i < ramsize ; i += 4) {
		if (readl(rambase + i) != (i ^ 0x12345678))
			break;
	}

	return i;
}


static void sca_init(card_t *card, int wait_states)
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

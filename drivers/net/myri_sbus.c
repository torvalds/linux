/* myri_sbus.c: MyriCOM MyriNET SBUS card driver.
 *
 * Copyright (C) 1996, 1999, 2006 David S. Miller (davem@davemloft.net)
 */

static char version[] =
        "myri_sbus.c:v2.0 June 23, 2006 David S. Miller (davem@davemloft.net)\n";

#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <net/dst.h>
#include <net/arp.h>
#include <net/sock.h>
#include <net/ipv6.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>
#include <asm/idprom.h>
#include <asm/sbus.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/checksum.h>

#include "myri_sbus.h"
#include "myri_code.h"

/* #define DEBUG_DETECT */
/* #define DEBUG_IRQ */
/* #define DEBUG_TRANSMIT */
/* #define DEBUG_RECEIVE */
/* #define DEBUG_HEADER */

#ifdef DEBUG_DETECT
#define DET(x)   printk x
#else
#define DET(x)
#endif

#ifdef DEBUG_IRQ
#define DIRQ(x)  printk x
#else
#define DIRQ(x)
#endif

#ifdef DEBUG_TRANSMIT
#define DTX(x)  printk x
#else
#define DTX(x)
#endif

#ifdef DEBUG_RECEIVE
#define DRX(x)  printk x
#else
#define DRX(x)
#endif

#ifdef DEBUG_HEADER
#define DHDR(x) printk x
#else
#define DHDR(x)
#endif

static void myri_reset_off(void __iomem *lp, void __iomem *cregs)
{
	/* Clear IRQ mask. */
	sbus_writel(0, lp + LANAI_EIMASK);

	/* Turn RESET function off. */
	sbus_writel(CONTROL_ROFF, cregs + MYRICTRL_CTRL);
}

static void myri_reset_on(void __iomem *cregs)
{
	/* Enable RESET function. */
	sbus_writel(CONTROL_RON, cregs + MYRICTRL_CTRL);

	/* Disable IRQ's. */
	sbus_writel(CONTROL_DIRQ, cregs + MYRICTRL_CTRL);
}

static void myri_disable_irq(void __iomem *lp, void __iomem *cregs)
{
	sbus_writel(CONTROL_DIRQ, cregs + MYRICTRL_CTRL);
	sbus_writel(0, lp + LANAI_EIMASK);
	sbus_writel(ISTAT_HOST, lp + LANAI_ISTAT);
}

static void myri_enable_irq(void __iomem *lp, void __iomem *cregs)
{
	sbus_writel(CONTROL_EIRQ, cregs + MYRICTRL_CTRL);
	sbus_writel(ISTAT_HOST, lp + LANAI_EIMASK);
}

static inline void bang_the_chip(struct myri_eth *mp)
{
	struct myri_shmem __iomem *shmem = mp->shmem;
	void __iomem *cregs		= mp->cregs;

	sbus_writel(1, &shmem->send);
	sbus_writel(CONTROL_WON, cregs + MYRICTRL_CTRL);
}

static int myri_do_handshake(struct myri_eth *mp)
{
	struct myri_shmem __iomem *shmem = mp->shmem;
	void __iomem *cregs = mp->cregs;
	struct myri_channel __iomem *chan = &shmem->channel;
	int tick 			= 0;

	DET(("myri_do_handshake: "));
	if (sbus_readl(&chan->state) == STATE_READY) {
		DET(("Already STATE_READY, failed.\n"));
		return -1;	/* We're hosed... */
	}

	myri_disable_irq(mp->lregs, cregs);

	while (tick++ <= 25) {
		u32 softstate;

		/* Wake it up. */
		DET(("shakedown, CONTROL_WON, "));
		sbus_writel(1, &shmem->shakedown);
		sbus_writel(CONTROL_WON, cregs + MYRICTRL_CTRL);

		softstate = sbus_readl(&chan->state);
		DET(("chanstate[%08x] ", softstate));
		if (softstate == STATE_READY) {
			DET(("wakeup successful, "));
			break;
		}

		if (softstate != STATE_WFN) {
			DET(("not WFN setting that, "));
			sbus_writel(STATE_WFN, &chan->state);
		}

		udelay(20);
	}

	myri_enable_irq(mp->lregs, cregs);

	if (tick > 25) {
		DET(("25 ticks we lose, failure.\n"));
		return -1;
	}
	DET(("success\n"));
	return 0;
}

static int myri_load_lanai(struct myri_eth *mp)
{
	struct net_device	*dev = mp->dev;
	struct myri_shmem __iomem *shmem = mp->shmem;
	void __iomem		*rptr;
	int 			i;

	myri_disable_irq(mp->lregs, mp->cregs);
	myri_reset_on(mp->cregs);

	rptr = mp->lanai;
	for (i = 0; i < mp->eeprom.ramsz; i++)
		sbus_writeb(0, rptr + i);

	if (mp->eeprom.cpuvers >= CPUVERS_3_0)
		sbus_writel(mp->eeprom.cval, mp->lregs + LANAI_CVAL);

	/* Load executable code. */
	for (i = 0; i < sizeof(lanai4_code); i++)
		sbus_writeb(lanai4_code[i], rptr + (lanai4_code_off * 2) + i);

	/* Load data segment. */
	for (i = 0; i < sizeof(lanai4_data); i++)
		sbus_writeb(lanai4_data[i], rptr + (lanai4_data_off * 2) + i);

	/* Set device address. */
	sbus_writeb(0, &shmem->addr[0]);
	sbus_writeb(0, &shmem->addr[1]);
	for (i = 0; i < 6; i++)
		sbus_writeb(dev->dev_addr[i],
			    &shmem->addr[i + 2]);

	/* Set SBUS bursts and interrupt mask. */
	sbus_writel(((mp->myri_bursts & 0xf8) >> 3), &shmem->burst);
	sbus_writel(SHMEM_IMASK_RX, &shmem->imask);

	/* Release the LANAI. */
	myri_disable_irq(mp->lregs, mp->cregs);
	myri_reset_off(mp->lregs, mp->cregs);
	myri_disable_irq(mp->lregs, mp->cregs);

	/* Wait for the reset to complete. */
	for (i = 0; i < 5000; i++) {
		if (sbus_readl(&shmem->channel.state) != STATE_READY)
			break;
		else
			udelay(10);
	}

	if (i == 5000)
		printk(KERN_ERR "myricom: Chip would not reset after firmware load.\n");

	i = myri_do_handshake(mp);
	if (i)
		printk(KERN_ERR "myricom: Handshake with LANAI failed.\n");

	if (mp->eeprom.cpuvers == CPUVERS_4_0)
		sbus_writel(0, mp->lregs + LANAI_VERS);

	return i;
}

static void myri_clean_rings(struct myri_eth *mp)
{
	struct sendq __iomem *sq = mp->sq;
	struct recvq __iomem *rq = mp->rq;
	int i;

	sbus_writel(0, &rq->tail);
	sbus_writel(0, &rq->head);
	for (i = 0; i < (RX_RING_SIZE+1); i++) {
		if (mp->rx_skbs[i] != NULL) {
			struct myri_rxd __iomem *rxd = &rq->myri_rxd[i];
			u32 dma_addr;

			dma_addr = sbus_readl(&rxd->myri_scatters[0].addr);
			sbus_unmap_single(mp->myri_sdev, dma_addr, RX_ALLOC_SIZE, SBUS_DMA_FROMDEVICE);
			dev_kfree_skb(mp->rx_skbs[i]);
			mp->rx_skbs[i] = NULL;
		}
	}

	mp->tx_old = 0;
	sbus_writel(0, &sq->tail);
	sbus_writel(0, &sq->head);
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (mp->tx_skbs[i] != NULL) {
			struct sk_buff *skb = mp->tx_skbs[i];
			struct myri_txd __iomem *txd = &sq->myri_txd[i];
			u32 dma_addr;

			dma_addr = sbus_readl(&txd->myri_gathers[0].addr);
			sbus_unmap_single(mp->myri_sdev, dma_addr, (skb->len + 3) & ~3, SBUS_DMA_TODEVICE);
			dev_kfree_skb(mp->tx_skbs[i]);
			mp->tx_skbs[i] = NULL;
		}
	}
}

static void myri_init_rings(struct myri_eth *mp, int from_irq)
{
	struct recvq __iomem *rq = mp->rq;
	struct myri_rxd __iomem *rxd = &rq->myri_rxd[0];
	struct net_device *dev = mp->dev;
	gfp_t gfp_flags = GFP_KERNEL;
	int i;

	if (from_irq || in_interrupt())
		gfp_flags = GFP_ATOMIC;

	myri_clean_rings(mp);
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = myri_alloc_skb(RX_ALLOC_SIZE, gfp_flags);
		u32 dma_addr;

		if (!skb)
			continue;
		mp->rx_skbs[i] = skb;
		skb->dev = dev;
		skb_put(skb, RX_ALLOC_SIZE);

		dma_addr = sbus_map_single(mp->myri_sdev, skb->data, RX_ALLOC_SIZE, SBUS_DMA_FROMDEVICE);
		sbus_writel(dma_addr, &rxd[i].myri_scatters[0].addr);
		sbus_writel(RX_ALLOC_SIZE, &rxd[i].myri_scatters[0].len);
		sbus_writel(i, &rxd[i].ctx);
		sbus_writel(1, &rxd[i].num_sg);
	}
	sbus_writel(0, &rq->head);
	sbus_writel(RX_RING_SIZE, &rq->tail);
}

static int myri_init(struct myri_eth *mp, int from_irq)
{
	myri_init_rings(mp, from_irq);
	return 0;
}

static void myri_is_not_so_happy(struct myri_eth *mp)
{
}

#ifdef DEBUG_HEADER
static void dump_ehdr(struct ethhdr *ehdr)
{
	printk("ehdr[h_dst(%02x:%02x:%02x:%02x:%02x:%02x)"
	       "h_source(%02x:%02x:%02x:%02x:%02x:%02x)h_proto(%04x)]\n",
	       ehdr->h_dest[0], ehdr->h_dest[1], ehdr->h_dest[2],
	       ehdr->h_dest[3], ehdr->h_dest[4], ehdr->h_dest[4],
	       ehdr->h_source[0], ehdr->h_source[1], ehdr->h_source[2],
	       ehdr->h_source[3], ehdr->h_source[4], ehdr->h_source[4],
	       ehdr->h_proto);
}

static void dump_ehdr_and_myripad(unsigned char *stuff)
{
	struct ethhdr *ehdr = (struct ethhdr *) (stuff + 2);

	printk("pad[%02x:%02x]", stuff[0], stuff[1]);
	printk("ehdr[h_dst(%02x:%02x:%02x:%02x:%02x:%02x)"
	       "h_source(%02x:%02x:%02x:%02x:%02x:%02x)h_proto(%04x)]\n",
	       ehdr->h_dest[0], ehdr->h_dest[1], ehdr->h_dest[2],
	       ehdr->h_dest[3], ehdr->h_dest[4], ehdr->h_dest[4],
	       ehdr->h_source[0], ehdr->h_source[1], ehdr->h_source[2],
	       ehdr->h_source[3], ehdr->h_source[4], ehdr->h_source[4],
	       ehdr->h_proto);
}
#endif

static void myri_tx(struct myri_eth *mp, struct net_device *dev)
{
	struct sendq __iomem *sq= mp->sq;
	int entry		= mp->tx_old;
	int limit		= sbus_readl(&sq->head);

	DTX(("entry[%d] limit[%d] ", entry, limit));
	if (entry == limit)
		return;
	while (entry != limit) {
		struct sk_buff *skb = mp->tx_skbs[entry];
		u32 dma_addr;

		DTX(("SKB[%d] ", entry));
		dma_addr = sbus_readl(&sq->myri_txd[entry].myri_gathers[0].addr);
		sbus_unmap_single(mp->myri_sdev, dma_addr, skb->len, SBUS_DMA_TODEVICE);
		dev_kfree_skb(skb);
		mp->tx_skbs[entry] = NULL;
		mp->enet_stats.tx_packets++;
		entry = NEXT_TX(entry);
	}
	mp->tx_old = entry;
}

/* Determine the packet's protocol ID. The rule here is that we 
 * assume 802.3 if the type field is short enough to be a length.
 * This is normal practice and works for any 'now in use' protocol.
 */
static __be16 myri_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;
	
	skb->mac.raw = (((unsigned char *)skb->data) + MYRI_PAD_LEN);
	skb_pull(skb, dev->hard_header_len);
	eth = eth_hdr(skb);
	
#ifdef DEBUG_HEADER
	DHDR(("myri_type_trans: "));
	dump_ehdr(eth);
#endif
	if (*eth->h_dest & 1) {
		if (memcmp(eth->h_dest, dev->broadcast, ETH_ALEN)==0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	} else if (dev->flags & (IFF_PROMISC|IFF_ALLMULTI)) {
		if (memcmp(eth->h_dest, dev->dev_addr, ETH_ALEN))
			skb->pkt_type = PACKET_OTHERHOST;
	}
	
	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;
		
	rawp = skb->data;
	
	/* This is a magic hack to spot IPX packets. Older Novell breaks
	 * the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 * layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 * won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);
		
	/* Real 802.2 LLC */
	return htons(ETH_P_802_2);
}

static void myri_rx(struct myri_eth *mp, struct net_device *dev)
{
	struct recvq __iomem *rq = mp->rq;
	struct recvq __iomem *rqa = mp->rqack;
	int entry		= sbus_readl(&rqa->head);
	int limit		= sbus_readl(&rqa->tail);
	int drops;

	DRX(("entry[%d] limit[%d] ", entry, limit));
	if (entry == limit)
		return;
	drops = 0;
	DRX(("\n"));
	while (entry != limit) {
		struct myri_rxd __iomem *rxdack = &rqa->myri_rxd[entry];
		u32 csum		= sbus_readl(&rxdack->csum);
		int len			= sbus_readl(&rxdack->myri_scatters[0].len);
		int index		= sbus_readl(&rxdack->ctx);
		struct myri_rxd __iomem *rxd = &rq->myri_rxd[sbus_readl(&rq->tail)];
		struct sk_buff *skb	= mp->rx_skbs[index];

		/* Ack it. */
		sbus_writel(NEXT_RX(entry), &rqa->head);

		/* Check for errors. */
		DRX(("rxd[%d]: %p len[%d] csum[%08x] ", entry, rxd, len, csum));
		sbus_dma_sync_single_for_cpu(mp->myri_sdev,
					     sbus_readl(&rxd->myri_scatters[0].addr),
					     RX_ALLOC_SIZE, SBUS_DMA_FROMDEVICE);
		if (len < (ETH_HLEN + MYRI_PAD_LEN) || (skb->data[0] != MYRI_PAD_LEN)) {
			DRX(("ERROR["));
			mp->enet_stats.rx_errors++;
			if (len < (ETH_HLEN + MYRI_PAD_LEN)) {
				DRX(("BAD_LENGTH] "));
				mp->enet_stats.rx_length_errors++;
			} else {
				DRX(("NO_PADDING] "));
				mp->enet_stats.rx_frame_errors++;
			}

			/* Return it to the LANAI. */
	drop_it:
			drops++;
			DRX(("DROP "));
			mp->enet_stats.rx_dropped++;
			sbus_dma_sync_single_for_device(mp->myri_sdev,
							sbus_readl(&rxd->myri_scatters[0].addr),
							RX_ALLOC_SIZE,
							SBUS_DMA_FROMDEVICE);
			sbus_writel(RX_ALLOC_SIZE, &rxd->myri_scatters[0].len);
			sbus_writel(index, &rxd->ctx);
			sbus_writel(1, &rxd->num_sg);
			sbus_writel(NEXT_RX(sbus_readl(&rq->tail)), &rq->tail);
			goto next;
		}

		DRX(("len[%d] ", len));
		if (len > RX_COPY_THRESHOLD) {
			struct sk_buff *new_skb;
			u32 dma_addr;

			DRX(("BIGBUFF "));
			new_skb = myri_alloc_skb(RX_ALLOC_SIZE, GFP_ATOMIC);
			if (new_skb == NULL) {
				DRX(("skb_alloc(FAILED) "));
				goto drop_it;
			}
			sbus_unmap_single(mp->myri_sdev,
					  sbus_readl(&rxd->myri_scatters[0].addr),
					  RX_ALLOC_SIZE,
					  SBUS_DMA_FROMDEVICE);
			mp->rx_skbs[index] = new_skb;
			new_skb->dev = dev;
			skb_put(new_skb, RX_ALLOC_SIZE);
			dma_addr = sbus_map_single(mp->myri_sdev,
						   new_skb->data,
						   RX_ALLOC_SIZE,
						   SBUS_DMA_FROMDEVICE);
			sbus_writel(dma_addr, &rxd->myri_scatters[0].addr);
			sbus_writel(RX_ALLOC_SIZE, &rxd->myri_scatters[0].len);
			sbus_writel(index, &rxd->ctx);
			sbus_writel(1, &rxd->num_sg);
			sbus_writel(NEXT_RX(sbus_readl(&rq->tail)), &rq->tail);

			/* Trim the original skb for the netif. */
			DRX(("trim(%d) ", len));
			skb_trim(skb, len);
		} else {
			struct sk_buff *copy_skb = dev_alloc_skb(len);

			DRX(("SMALLBUFF "));
			if (copy_skb == NULL) {
				DRX(("dev_alloc_skb(FAILED) "));
				goto drop_it;
			}
			/* DMA sync already done above. */
			copy_skb->dev = dev;
			DRX(("resv_and_put "));
			skb_put(copy_skb, len);
			memcpy(copy_skb->data, skb->data, len);

			/* Reuse original ring buffer. */
			DRX(("reuse "));
			sbus_dma_sync_single_for_device(mp->myri_sdev,
							sbus_readl(&rxd->myri_scatters[0].addr),
							RX_ALLOC_SIZE,
							SBUS_DMA_FROMDEVICE);
			sbus_writel(RX_ALLOC_SIZE, &rxd->myri_scatters[0].len);
			sbus_writel(index, &rxd->ctx);
			sbus_writel(1, &rxd->num_sg);
			sbus_writel(NEXT_RX(sbus_readl(&rq->tail)), &rq->tail);

			skb = copy_skb;
		}

		/* Just like the happy meal we get checksums from this card. */
		skb->csum = csum;
		skb->ip_summed = CHECKSUM_UNNECESSARY; /* XXX */

		skb->protocol = myri_type_trans(skb, dev);
		DRX(("prot[%04x] netif_rx ", skb->protocol));
		netif_rx(skb);

		dev->last_rx = jiffies;
		mp->enet_stats.rx_packets++;
		mp->enet_stats.rx_bytes += len;
	next:
		DRX(("NEXT\n"));
		entry = NEXT_RX(entry);
	}
}

static irqreturn_t myri_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev		= (struct net_device *) dev_id;
	struct myri_eth *mp		= (struct myri_eth *) dev->priv;
	void __iomem *lregs		= mp->lregs;
	struct myri_channel __iomem *chan = &mp->shmem->channel;
	unsigned long flags;
	u32 status;
	int handled = 0;

	spin_lock_irqsave(&mp->irq_lock, flags);

	status = sbus_readl(lregs + LANAI_ISTAT);
	DIRQ(("myri_interrupt: status[%08x] ", status));
	if (status & ISTAT_HOST) {
		u32 softstate;

		handled = 1;
		DIRQ(("IRQ_DISAB "));
		myri_disable_irq(lregs, mp->cregs);
		softstate = sbus_readl(&chan->state);
		DIRQ(("state[%08x] ", softstate));
		if (softstate != STATE_READY) {
			DIRQ(("myri_not_so_happy "));
			myri_is_not_so_happy(mp);
		}
		DIRQ(("\nmyri_rx: "));
		myri_rx(mp, dev);
		DIRQ(("\nistat=ISTAT_HOST "));
		sbus_writel(ISTAT_HOST, lregs + LANAI_ISTAT);
		DIRQ(("IRQ_ENAB "));
		myri_enable_irq(lregs, mp->cregs);
	}
	DIRQ(("\n"));

	spin_unlock_irqrestore(&mp->irq_lock, flags);

	return IRQ_RETVAL(handled);
}

static int myri_open(struct net_device *dev)
{
	struct myri_eth *mp = (struct myri_eth *) dev->priv;

	return myri_init(mp, in_interrupt());
}

static int myri_close(struct net_device *dev)
{
	struct myri_eth *mp = (struct myri_eth *) dev->priv;

	myri_clean_rings(mp);
	return 0;
}

static void myri_tx_timeout(struct net_device *dev)
{
	struct myri_eth *mp = (struct myri_eth *) dev->priv;

	printk(KERN_ERR "%s: transmit timed out, resetting\n", dev->name);

	mp->enet_stats.tx_errors++;
	myri_init(mp, 0);
	netif_wake_queue(dev);
}

static int myri_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct myri_eth *mp = (struct myri_eth *) dev->priv;
	struct sendq __iomem *sq = mp->sq;
	struct myri_txd __iomem *txd;
	unsigned long flags;
	unsigned int head, tail;
	int len, entry;
	u32 dma_addr;

	DTX(("myri_start_xmit: "));

	myri_tx(mp, dev);

	netif_stop_queue(dev);

	/* This is just to prevent multiple PIO reads for TX_BUFFS_AVAIL. */
	head = sbus_readl(&sq->head);
	tail = sbus_readl(&sq->tail);

	if (!TX_BUFFS_AVAIL(head, tail)) {
		DTX(("no buffs available, returning 1\n"));
		return 1;
	}

	spin_lock_irqsave(&mp->irq_lock, flags);

	DHDR(("xmit[skbdata(%p)]\n", skb->data));
#ifdef DEBUG_HEADER
	dump_ehdr_and_myripad(((unsigned char *) skb->data));
#endif

	/* XXX Maybe this can go as well. */
	len = skb->len;
	if (len & 3) {
		DTX(("len&3 "));
		len = (len + 4) & (~3);
	}

	entry = sbus_readl(&sq->tail);

	txd = &sq->myri_txd[entry];
	mp->tx_skbs[entry] = skb;

	/* Must do this before we sbus map it. */
	if (skb->data[MYRI_PAD_LEN] & 0x1) {
		sbus_writew(0xffff, &txd->addr[0]);
		sbus_writew(0xffff, &txd->addr[1]);
		sbus_writew(0xffff, &txd->addr[2]);
		sbus_writew(0xffff, &txd->addr[3]);
	} else {
		sbus_writew(0xffff, &txd->addr[0]);
		sbus_writew((skb->data[0] << 8) | skb->data[1], &txd->addr[1]);
		sbus_writew((skb->data[2] << 8) | skb->data[3], &txd->addr[2]);
		sbus_writew((skb->data[4] << 8) | skb->data[5], &txd->addr[3]);
	}

	dma_addr = sbus_map_single(mp->myri_sdev, skb->data, len, SBUS_DMA_TODEVICE);
	sbus_writel(dma_addr, &txd->myri_gathers[0].addr);
	sbus_writel(len, &txd->myri_gathers[0].len);
	sbus_writel(1, &txd->num_sg);
	sbus_writel(KERNEL_CHANNEL, &txd->chan);
	sbus_writel(len, &txd->len);
	sbus_writel((u32)-1, &txd->csum_off);
	sbus_writel(0, &txd->csum_field);

	sbus_writel(NEXT_TX(entry), &sq->tail);
	DTX(("BangTheChip "));
	bang_the_chip(mp);

	DTX(("tbusy=0, returning 0\n"));
	netif_start_queue(dev);
	spin_unlock_irqrestore(&mp->irq_lock, flags);
	return 0;
}

/* Create the MyriNet MAC header for an arbitrary protocol layer 
 *
 * saddr=NULL	means use device source address
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */
static int myri_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
		       void *daddr, void *saddr, unsigned len)
{
	struct ethhdr *eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);
	unsigned char *pad = (unsigned char *) skb_push(skb, MYRI_PAD_LEN);

#ifdef DEBUG_HEADER
	DHDR(("myri_header: pad[%02x,%02x] ", pad[0], pad[1]));
	dump_ehdr(eth);
#endif

	/* Set the MyriNET padding identifier. */
	pad[0] = MYRI_PAD_LEN;
	pad[1] = 0xab;

	/* Set the protocol type. For a packet of type ETH_P_802_3 we put the length
	 * in here instead. It is up to the 802.2 layer to carry protocol information.
	 */
	if (type != ETH_P_802_3) 
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/* Set the source hardware address. */
	if (saddr)
		memcpy(eth->h_source, saddr, dev->addr_len);
	else
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);

	/* Anyway, the loopback-device should never use this function... */
	if (dev->flags & IFF_LOOPBACK) {
		int i;
		for (i = 0; i < dev->addr_len; i++)
			eth->h_dest[i] = 0;
		return(dev->hard_header_len);
	}
	
	if (daddr) {
		memcpy(eth->h_dest, daddr, dev->addr_len);
		return dev->hard_header_len;
	}
	return -dev->hard_header_len;
}

/* Rebuild the MyriNet MAC header. This is called after an ARP
 * (or in future other address resolution) has completed on this
 * sk_buff. We now let ARP fill in the other fields.
 */
static int myri_rebuild_header(struct sk_buff *skb)
{
	unsigned char *pad = (unsigned char *) skb->data;
	struct ethhdr *eth = (struct ethhdr *) (pad + MYRI_PAD_LEN);
	struct net_device *dev = skb->dev;

#ifdef DEBUG_HEADER
	DHDR(("myri_rebuild_header: pad[%02x,%02x] ", pad[0], pad[1]));
	dump_ehdr(eth);
#endif

	/* Refill MyriNet padding identifiers, this is just being anal. */
	pad[0] = MYRI_PAD_LEN;
	pad[1] = 0xab;

	switch (eth->h_proto)
	{
#ifdef CONFIG_INET
	case __constant_htons(ETH_P_IP):
 		return arp_find(eth->h_dest, skb);
#endif

	default:
		printk(KERN_DEBUG 
		       "%s: unable to resolve type %X addresses.\n", 
		       dev->name, (int)eth->h_proto);
		
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		return 0;
		break;
	}

	return 0;	
}

int myri_header_cache(struct neighbour *neigh, struct hh_cache *hh)
{
	unsigned short type = hh->hh_type;
	unsigned char *pad;
	struct ethhdr *eth;
	struct net_device *dev = neigh->dev;

	pad = ((unsigned char *) hh->hh_data) +
		HH_DATA_OFF(sizeof(*eth) + MYRI_PAD_LEN);
	eth = (struct ethhdr *) (pad + MYRI_PAD_LEN);

	if (type == __constant_htons(ETH_P_802_3))
		return -1;

	/* Refill MyriNet padding identifiers, this is just being anal. */
	pad[0] = MYRI_PAD_LEN;
	pad[1] = 0xab;

	eth->h_proto = type;
	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, neigh->ha, dev->addr_len);
	hh->hh_len = 16;
	return 0;
}


/* Called by Address Resolution module to notify changes in address. */
void myri_header_cache_update(struct hh_cache *hh, struct net_device *dev, unsigned char * haddr)
{
	memcpy(((u8*)hh->hh_data) + HH_DATA_OFF(sizeof(struct ethhdr)),
	       haddr, dev->addr_len);
}

static int myri_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < (ETH_HLEN + MYRI_PAD_LEN)) || (new_mtu > MYRINET_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static struct net_device_stats *myri_get_stats(struct net_device *dev)
{ return &(((struct myri_eth *)dev->priv)->enet_stats); }

static void myri_set_multicast(struct net_device *dev)
{
	/* Do nothing, all MyriCOM nodes transmit multicast frames
	 * as broadcast packets...
	 */
}

static inline void set_boardid_from_idprom(struct myri_eth *mp, int num)
{
	mp->eeprom.id[0] = 0;
	mp->eeprom.id[1] = idprom->id_machtype;
	mp->eeprom.id[2] = (idprom->id_sernum >> 16) & 0xff;
	mp->eeprom.id[3] = (idprom->id_sernum >> 8) & 0xff;
	mp->eeprom.id[4] = (idprom->id_sernum >> 0) & 0xff;
	mp->eeprom.id[5] = num;
}

static inline void determine_reg_space_size(struct myri_eth *mp)
{
	switch(mp->eeprom.cpuvers) {
	case CPUVERS_2_3:
	case CPUVERS_3_0:
	case CPUVERS_3_1:
	case CPUVERS_3_2:
		mp->reg_size = (3 * 128 * 1024) + 4096;
		break;

	case CPUVERS_4_0:
	case CPUVERS_4_1:
		mp->reg_size = ((4096<<1) + mp->eeprom.ramsz);
		break;

	case CPUVERS_4_2:
	case CPUVERS_5_0:
	default:
		printk("myricom: AIEEE weird cpu version %04x assuming pre4.0\n",
		       mp->eeprom.cpuvers);
		mp->reg_size = (3 * 128 * 1024) + 4096;
	};
}

#ifdef DEBUG_DETECT
static void dump_eeprom(struct myri_eth *mp)
{
	printk("EEPROM: clockval[%08x] cpuvers[%04x] "
	       "id[%02x,%02x,%02x,%02x,%02x,%02x]\n",
	       mp->eeprom.cval, mp->eeprom.cpuvers,
	       mp->eeprom.id[0], mp->eeprom.id[1], mp->eeprom.id[2],
	       mp->eeprom.id[3], mp->eeprom.id[4], mp->eeprom.id[5]);
	printk("EEPROM: ramsz[%08x]\n", mp->eeprom.ramsz);
	printk("EEPROM: fvers[%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
	       mp->eeprom.fvers[0], mp->eeprom.fvers[1], mp->eeprom.fvers[2],
	       mp->eeprom.fvers[3], mp->eeprom.fvers[4], mp->eeprom.fvers[5],
	       mp->eeprom.fvers[6], mp->eeprom.fvers[7]);
	printk("EEPROM:       %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
	       mp->eeprom.fvers[8], mp->eeprom.fvers[9], mp->eeprom.fvers[10],
	       mp->eeprom.fvers[11], mp->eeprom.fvers[12], mp->eeprom.fvers[13],
	       mp->eeprom.fvers[14], mp->eeprom.fvers[15]);
	printk("EEPROM:       %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
	       mp->eeprom.fvers[16], mp->eeprom.fvers[17], mp->eeprom.fvers[18],
	       mp->eeprom.fvers[19], mp->eeprom.fvers[20], mp->eeprom.fvers[21],
	       mp->eeprom.fvers[22], mp->eeprom.fvers[23]);
	printk("EEPROM:       %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x]\n",
	       mp->eeprom.fvers[24], mp->eeprom.fvers[25], mp->eeprom.fvers[26],
	       mp->eeprom.fvers[27], mp->eeprom.fvers[28], mp->eeprom.fvers[29],
	       mp->eeprom.fvers[30], mp->eeprom.fvers[31]);
	printk("EEPROM: mvers[%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
	       mp->eeprom.mvers[0], mp->eeprom.mvers[1], mp->eeprom.mvers[2],
	       mp->eeprom.mvers[3], mp->eeprom.mvers[4], mp->eeprom.mvers[5],
	       mp->eeprom.mvers[6], mp->eeprom.mvers[7]);
	printk("EEPROM:       %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x]\n",
	       mp->eeprom.mvers[8], mp->eeprom.mvers[9], mp->eeprom.mvers[10],
	       mp->eeprom.mvers[11], mp->eeprom.mvers[12], mp->eeprom.mvers[13],
	       mp->eeprom.mvers[14], mp->eeprom.mvers[15]);
	printk("EEPROM: dlval[%04x] brd_type[%04x] bus_type[%04x] prod_code[%04x]\n",
	       mp->eeprom.dlval, mp->eeprom.brd_type, mp->eeprom.bus_type,
	       mp->eeprom.prod_code);
	printk("EEPROM: serial_num[%08x]\n", mp->eeprom.serial_num);
}
#endif

static int __init myri_ether_init(struct sbus_dev *sdev)
{
	static int num;
	static unsigned version_printed;
	struct net_device *dev;
	struct myri_eth *mp;
	unsigned char prop_buf[32];
	int i;

	DET(("myri_ether_init(%p,%d):\n", sdev, num));
	dev = alloc_etherdev(sizeof(struct myri_eth));

	if (!dev)
		return -ENOMEM;

	if (version_printed++ == 0)
		printk(version);

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &sdev->ofdev.dev);

	mp = (struct myri_eth *) dev->priv;
	spin_lock_init(&mp->irq_lock);
	mp->myri_sdev = sdev;

	/* Clean out skb arrays. */
	for (i = 0; i < (RX_RING_SIZE + 1); i++)
		mp->rx_skbs[i] = NULL;

	for (i = 0; i < TX_RING_SIZE; i++)
		mp->tx_skbs[i] = NULL;

	/* First check for EEPROM information. */
	i = prom_getproperty(sdev->prom_node, "myrinet-eeprom-info",
			     (char *)&mp->eeprom, sizeof(struct myri_eeprom));
	DET(("prom_getprop(myrinet-eeprom-info) returns %d\n", i));
	if (i == 0 || i == -1) {
		/* No eeprom property, must cook up the values ourselves. */
		DET(("No EEPROM: "));
		mp->eeprom.bus_type = BUS_TYPE_SBUS;
		mp->eeprom.cpuvers = prom_getintdefault(sdev->prom_node,"cpu_version",0);
		mp->eeprom.cval = prom_getintdefault(sdev->prom_node,"clock_value",0);
		mp->eeprom.ramsz = prom_getintdefault(sdev->prom_node,"sram_size",0);
		DET(("cpuvers[%d] cval[%d] ramsz[%d]\n", mp->eeprom.cpuvers,
		     mp->eeprom.cval, mp->eeprom.ramsz));
		if (mp->eeprom.cpuvers == 0) {
			DET(("EEPROM: cpuvers was zero, setting to %04x\n",CPUVERS_2_3));
			mp->eeprom.cpuvers = CPUVERS_2_3;
		}
		if (mp->eeprom.cpuvers < CPUVERS_3_0) {
			DET(("EEPROM: cpuvers < CPUVERS_3_0, clockval set to zero.\n"));
			mp->eeprom.cval = 0;
		}
		if (mp->eeprom.ramsz == 0) {
			DET(("EEPROM: ramsz == 0, setting to 128k\n"));
			mp->eeprom.ramsz = (128 * 1024);
		}
		i = prom_getproperty(sdev->prom_node, "myrinet-board-id",
				     &prop_buf[0], 10);
		DET(("EEPROM: prom_getprop(myrinet-board-id) returns %d\n", i));
		if ((i != 0) && (i != -1))
			memcpy(&mp->eeprom.id[0], &prop_buf[0], 6);
		else
			set_boardid_from_idprom(mp, num);
		i = prom_getproperty(sdev->prom_node, "fpga_version",
				     &mp->eeprom.fvers[0], 32);
		DET(("EEPROM: prom_getprop(fpga_version) returns %d\n", i));
		if (i == 0 || i == -1)
			memset(&mp->eeprom.fvers[0], 0, 32);

		if (mp->eeprom.cpuvers == CPUVERS_4_1) {
			DET(("EEPROM: cpuvers CPUVERS_4_1, "));
			if (mp->eeprom.ramsz == (128 * 1024)) {
				DET(("ramsize 128k, setting to 256k, "));
				mp->eeprom.ramsz = (256 * 1024);
			}
			if ((mp->eeprom.cval==0x40414041)||(mp->eeprom.cval==0x90449044)){
				DET(("changing cval from %08x to %08x ",
				     mp->eeprom.cval, 0x50e450e4));
				mp->eeprom.cval = 0x50e450e4;
			}
			DET(("\n"));
		}
	}
#ifdef DEBUG_DETECT
	dump_eeprom(mp);
#endif

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = mp->eeprom.id[i];

	determine_reg_space_size(mp);

	/* Map in the MyriCOM register/localram set. */
	if (mp->eeprom.cpuvers < CPUVERS_4_0) {
		/* XXX Makes no sense, if control reg is non-existant this
		 * XXX driver cannot function at all... maybe pre-4.0 is
		 * XXX only a valid version for PCI cards?  Ask feldy...
		 */
		DET(("Mapping regs for cpuvers < CPUVERS_4_0\n"));
		mp->regs = sbus_ioremap(&sdev->resource[0], 0,
					mp->reg_size, "MyriCOM Regs");
		if (!mp->regs) {
			printk("MyriCOM: Cannot map MyriCOM registers.\n");
			goto err;
		}
		mp->lanai = mp->regs + (256 * 1024);
		mp->lregs = mp->lanai + (0x10000 * 2);
	} else {
		DET(("Mapping regs for cpuvers >= CPUVERS_4_0\n"));
		mp->cregs = sbus_ioremap(&sdev->resource[0], 0,
					 PAGE_SIZE, "MyriCOM Control Regs");
		mp->lregs = sbus_ioremap(&sdev->resource[0], (256 * 1024),
					 PAGE_SIZE, "MyriCOM LANAI Regs");
		mp->lanai =
			sbus_ioremap(&sdev->resource[0], (512 * 1024),
				     mp->eeprom.ramsz, "MyriCOM SRAM");
	}
	DET(("Registers mapped: cregs[%p] lregs[%p] lanai[%p]\n",
	     mp->cregs, mp->lregs, mp->lanai));

	if (mp->eeprom.cpuvers >= CPUVERS_4_0)
		mp->shmem_base = 0xf000;
	else
		mp->shmem_base = 0x8000;

	DET(("Shared memory base is %04x, ", mp->shmem_base));

	mp->shmem = (struct myri_shmem __iomem *)
		(mp->lanai + (mp->shmem_base * 2));
	DET(("shmem mapped at %p\n", mp->shmem));

	mp->rqack	= &mp->shmem->channel.recvqa;
	mp->rq		= &mp->shmem->channel.recvq;
	mp->sq		= &mp->shmem->channel.sendq;

	/* Reset the board. */
	DET(("Resetting LANAI\n"));
	myri_reset_off(mp->lregs, mp->cregs);
	myri_reset_on(mp->cregs);

	/* Turn IRQ's off. */
	myri_disable_irq(mp->lregs, mp->cregs);

	/* Reset once more. */
	myri_reset_on(mp->cregs);

	/* Get the supported DVMA burst sizes from our SBUS. */
	mp->myri_bursts = prom_getintdefault(mp->myri_sdev->bus->prom_node,
					     "burst-sizes", 0x00);

	if (!sbus_can_burst64(sdev))
		mp->myri_bursts &= ~(DMA_BURST64);

	DET(("MYRI bursts %02x\n", mp->myri_bursts));

	/* Encode SBUS interrupt level in second control register. */
	i = prom_getint(sdev->prom_node, "interrupts");
	if (i == 0)
		i = 4;
	DET(("prom_getint(interrupts)==%d, irqlvl set to %04x\n",
	     i, (1 << i)));

	sbus_writel((1 << i), mp->cregs + MYRICTRL_IRQLVL);

	mp->dev = dev;
	dev->open = &myri_open;
	dev->stop = &myri_close;
	dev->hard_start_xmit = &myri_start_xmit;
	dev->tx_timeout = &myri_tx_timeout;
	dev->watchdog_timeo = 5*HZ;
	dev->get_stats = &myri_get_stats;
	dev->set_multicast_list = &myri_set_multicast;
	dev->irq = sdev->irqs[0];

	/* Register interrupt handler now. */
	DET(("Requesting MYRIcom IRQ line.\n"));
	if (request_irq(dev->irq, &myri_interrupt,
			SA_SHIRQ, "MyriCOM Ethernet", (void *) dev)) {
		printk("MyriCOM: Cannot register interrupt handler.\n");
		goto err;
	}

	dev->mtu		= MYRINET_MTU;
	dev->change_mtu		= myri_change_mtu;
	dev->hard_header	= myri_header;
	dev->rebuild_header	= myri_rebuild_header;
	dev->hard_header_len	= (ETH_HLEN + MYRI_PAD_LEN);
	dev->hard_header_cache 	= myri_header_cache;
	dev->header_cache_update= myri_header_cache_update;

	/* Load code onto the LANai. */
	DET(("Loading LANAI firmware\n"));
	myri_load_lanai(mp);

	if (register_netdev(dev)) {
		printk("MyriCOM: Cannot register device.\n");
		goto err_free_irq;
	}

	dev_set_drvdata(&sdev->ofdev.dev, mp);

	num++;

	printk("%s: MyriCOM MyriNET Ethernet ", dev->name);

	for (i = 0; i < 6; i++)
		printk("%2.2x%c", dev->dev_addr[i],
		       i == 5 ? ' ' : ':');
	printk("\n");

	return 0;

err_free_irq:
	free_irq(dev->irq, dev);
err:
	/* This will also free the co-allocated 'dev->priv' */
	free_netdev(dev);
	return -ENODEV;
}


static int __devinit myri_sbus_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct sbus_dev *sdev = to_sbus_device(&dev->dev);

	return myri_ether_init(sdev);
}

static int __devexit myri_sbus_remove(struct of_device *dev)
{
	struct myri_eth *mp = dev_get_drvdata(&dev->dev);
	struct net_device *net_dev = mp->dev;

	unregister_netdevice(net_dev);

	free_irq(net_dev->irq, net_dev);

	if (mp->eeprom.cpuvers < CPUVERS_4_0) {
		sbus_iounmap(mp->regs, mp->reg_size);
	} else {
		sbus_iounmap(mp->cregs, PAGE_SIZE);
		sbus_iounmap(mp->lregs, (256 * 1024));
		sbus_iounmap(mp->lanai, (512 * 1024));
	}

	free_netdev(net_dev);

	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

static struct of_device_id myri_sbus_match[] = {
	{
		.name = "MYRICOM,mlanai",
	},
	{
		.name = "myri",
	},
	{},
};

MODULE_DEVICE_TABLE(of, myri_sbus_match);

static struct of_platform_driver myri_sbus_driver = {
	.name		= "myri",
	.match_table	= myri_sbus_match,
	.probe		= myri_sbus_probe,
	.remove		= __devexit_p(myri_sbus_remove),
};

static int __init myri_sbus_init(void)
{
	return of_register_driver(&myri_sbus_driver, &sbus_bus_type);
}

static void __exit myri_sbus_exit(void)
{
	of_unregister_driver(&myri_sbus_driver);
}

module_init(myri_sbus_init);
module_exit(myri_sbus_exit);

MODULE_LICENSE("GPL");

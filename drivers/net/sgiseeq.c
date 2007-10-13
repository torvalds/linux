/*
 * sgiseeq.c: Seeq8003 ethernet driver for SGI machines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>
#include <asm/sgi/seeq.h>

#include "sgiseeq.h"

static char *sgiseeqstr = "SGI Seeq8003";

/*
 * If you want speed, you do something silly, it always has worked for me.  So,
 * with that in mind, I've decided to make this driver look completely like a
 * stupid Lance from a driver architecture perspective.  Only difference is that
 * here our "ring buffer" looks and acts like a real Lance one does but is
 * layed out like how the HPC DMA and the Seeq want it to.  You'd be surprised
 * how a stupid idea like this can pay off in performance, not to mention
 * making this driver 2,000 times easier to write. ;-)
 */

/* Tune these if we tend to run out often etc. */
#define SEEQ_RX_BUFFERS  16
#define SEEQ_TX_BUFFERS  16

#define PKT_BUF_SZ       1584

#define NEXT_RX(i)  (((i) + 1) & (SEEQ_RX_BUFFERS - 1))
#define NEXT_TX(i)  (((i) + 1) & (SEEQ_TX_BUFFERS - 1))
#define PREV_RX(i)  (((i) - 1) & (SEEQ_RX_BUFFERS - 1))
#define PREV_TX(i)  (((i) - 1) & (SEEQ_TX_BUFFERS - 1))

#define TX_BUFFS_AVAIL(sp) ((sp->tx_old <= sp->tx_new) ? \
			    sp->tx_old + (SEEQ_TX_BUFFERS - 1) - sp->tx_new : \
			    sp->tx_old - sp->tx_new - 1)

struct sgiseeq_rx_desc {
	volatile struct hpc_dma_desc rdma;
	volatile signed int buf_vaddr;
};

struct sgiseeq_tx_desc {
	volatile struct hpc_dma_desc tdma;
	volatile signed int buf_vaddr;
};

/*
 * Warning: This structure is layed out in a certain way because HPC dma
 *          descriptors must be 8-byte aligned.  So don't touch this without
 *          some care.
 */
struct sgiseeq_init_block { /* Note the name ;-) */
	struct sgiseeq_rx_desc rxvector[SEEQ_RX_BUFFERS];
	struct sgiseeq_tx_desc txvector[SEEQ_TX_BUFFERS];
};

struct sgiseeq_private {
	struct sgiseeq_init_block *srings;

	/* Ptrs to the descriptors in uncached space. */
	struct sgiseeq_rx_desc *rx_desc;
	struct sgiseeq_tx_desc *tx_desc;

	char *name;
	struct hpc3_ethregs *hregs;
	struct sgiseeq_regs *sregs;

	/* Ring entry counters. */
	unsigned int rx_new, tx_new;
	unsigned int rx_old, tx_old;

	int is_edlc;
	unsigned char control;
	unsigned char mode;

	struct net_device_stats stats;

	spinlock_t tx_lock;
};

static inline void hpc3_eth_reset(struct hpc3_ethregs *hregs)
{
	hregs->reset = HPC3_ERST_CRESET | HPC3_ERST_CLRIRQ;
	udelay(20);
	hregs->reset = 0;
}

static inline void reset_hpc3_and_seeq(struct hpc3_ethregs *hregs,
				       struct sgiseeq_regs *sregs)
{
	hregs->rx_ctrl = hregs->tx_ctrl = 0;
	hpc3_eth_reset(hregs);
}

#define RSTAT_GO_BITS (SEEQ_RCMD_IGOOD | SEEQ_RCMD_IEOF | SEEQ_RCMD_ISHORT | \
		       SEEQ_RCMD_IDRIB | SEEQ_RCMD_ICRC)

static inline void seeq_go(struct sgiseeq_private *sp,
			   struct hpc3_ethregs *hregs,
			   struct sgiseeq_regs *sregs)
{
	sregs->rstat = sp->mode | RSTAT_GO_BITS;
	hregs->rx_ctrl = HPC3_ERXCTRL_ACTIVE;
}

static inline void __sgiseeq_set_mac_address(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	int i;

	sregs->tstat = SEEQ_TCMD_RB0;
	for (i = 0; i < 6; i++)
		sregs->rw.eth_addr[i] = dev->dev_addr[i];
}

static int sgiseeq_set_mac_address(struct net_device *dev, void *addr)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sockaddr *sa = addr;

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	spin_lock_irq(&sp->tx_lock);
	__sgiseeq_set_mac_address(dev);
	spin_unlock_irq(&sp->tx_lock);

	return 0;
}

#define TCNTINFO_INIT (HPCDMA_EOX | HPCDMA_ETXD)
#define RCNTCFG_INIT  (HPCDMA_OWN | HPCDMA_EORP | HPCDMA_XIE)
#define RCNTINFO_INIT (RCNTCFG_INIT | (PKT_BUF_SZ & HPCDMA_BCNT))

static int seeq_init_ring(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	int i;

	netif_stop_queue(dev);
	sp->rx_new = sp->tx_new = 0;
	sp->rx_old = sp->tx_old = 0;

	__sgiseeq_set_mac_address(dev);

	/* Setup tx ring. */
	for(i = 0; i < SEEQ_TX_BUFFERS; i++) {
		if (!sp->tx_desc[i].tdma.pbuf) {
			unsigned long buffer;

			buffer = (unsigned long) kmalloc(PKT_BUF_SZ, GFP_KERNEL);
			if (!buffer)
				return -ENOMEM;
			sp->tx_desc[i].buf_vaddr = CKSEG1ADDR(buffer);
			sp->tx_desc[i].tdma.pbuf = CPHYSADDR(buffer);
		}
		sp->tx_desc[i].tdma.cntinfo = TCNTINFO_INIT;
	}

	/* And now the rx ring. */
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		if (!sp->rx_desc[i].rdma.pbuf) {
			unsigned long buffer;

			buffer = (unsigned long) kmalloc(PKT_BUF_SZ, GFP_KERNEL);
			if (!buffer)
				return -ENOMEM;
			sp->rx_desc[i].buf_vaddr = CKSEG1ADDR(buffer);
			sp->rx_desc[i].rdma.pbuf = CPHYSADDR(buffer);
		}
		sp->rx_desc[i].rdma.cntinfo = RCNTINFO_INIT;
	}
	sp->rx_desc[i - 1].rdma.cntinfo |= HPCDMA_EOR;
	return 0;
}

#ifdef DEBUG
static struct sgiseeq_private *gpriv;
static struct net_device *gdev;

static void sgiseeq_dump_rings(void)
{
	static int once;
	struct sgiseeq_rx_desc *r = gpriv->rx_desc;
	struct sgiseeq_tx_desc *t = gpriv->tx_desc;
	struct hpc3_ethregs *hregs = gpriv->hregs;
	int i;

	if (once)
		return;
	once++;
	printk("RING DUMP:\n");
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		printk("RX [%d]: @(%p) [%08x,%08x,%08x] ",
		       i, (&r[i]), r[i].rdma.pbuf, r[i].rdma.cntinfo,
		       r[i].rdma.pnext);
		i += 1;
		printk("-- [%d]: @(%p) [%08x,%08x,%08x]\n",
		       i, (&r[i]), r[i].rdma.pbuf, r[i].rdma.cntinfo,
		       r[i].rdma.pnext);
	}
	for (i = 0; i < SEEQ_TX_BUFFERS; i++) {
		printk("TX [%d]: @(%p) [%08x,%08x,%08x] ",
		       i, (&t[i]), t[i].tdma.pbuf, t[i].tdma.cntinfo,
		       t[i].tdma.pnext);
		i += 1;
		printk("-- [%d]: @(%p) [%08x,%08x,%08x]\n",
		       i, (&t[i]), t[i].tdma.pbuf, t[i].tdma.cntinfo,
		       t[i].tdma.pnext);
	}
	printk("INFO: [rx_new = %d rx_old=%d] [tx_new = %d tx_old = %d]\n",
	       gpriv->rx_new, gpriv->rx_old, gpriv->tx_new, gpriv->tx_old);
	printk("RREGS: rx_cbptr[%08x] rx_ndptr[%08x] rx_ctrl[%08x]\n",
	       hregs->rx_cbptr, hregs->rx_ndptr, hregs->rx_ctrl);
	printk("TREGS: tx_cbptr[%08x] tx_ndptr[%08x] tx_ctrl[%08x]\n",
	       hregs->tx_cbptr, hregs->tx_ndptr, hregs->tx_ctrl);
}
#endif

#define TSTAT_INIT_SEEQ (SEEQ_TCMD_IPT|SEEQ_TCMD_I16|SEEQ_TCMD_IC|SEEQ_TCMD_IUF)
#define TSTAT_INIT_EDLC ((TSTAT_INIT_SEEQ) | SEEQ_TCMD_RB2)

static int init_seeq(struct net_device *dev, struct sgiseeq_private *sp,
		     struct sgiseeq_regs *sregs)
{
	struct hpc3_ethregs *hregs = sp->hregs;
	int err;

	reset_hpc3_and_seeq(hregs, sregs);
	err = seeq_init_ring(dev);
	if (err)
		return err;

	/* Setup to field the proper interrupt types. */
	if (sp->is_edlc) {
		sregs->tstat = TSTAT_INIT_EDLC;
		sregs->rw.wregs.control = sp->control;
		sregs->rw.wregs.frame_gap = 0;
	} else {
		sregs->tstat = TSTAT_INIT_SEEQ;
	}

	hregs->rx_ndptr = CPHYSADDR(sp->rx_desc);
	hregs->tx_ndptr = CPHYSADDR(sp->tx_desc);

	seeq_go(sp, hregs, sregs);
	return 0;
}

static inline void record_rx_errors(struct sgiseeq_private *sp,
				    unsigned char status)
{
	if (status & SEEQ_RSTAT_OVERF ||
	    status & SEEQ_RSTAT_SFRAME)
		sp->stats.rx_over_errors++;
	if (status & SEEQ_RSTAT_CERROR)
		sp->stats.rx_crc_errors++;
	if (status & SEEQ_RSTAT_DERROR)
		sp->stats.rx_frame_errors++;
	if (status & SEEQ_RSTAT_REOF)
		sp->stats.rx_errors++;
}

static inline void rx_maybe_restart(struct sgiseeq_private *sp,
				    struct hpc3_ethregs *hregs,
				    struct sgiseeq_regs *sregs)
{
	if (!(hregs->rx_ctrl & HPC3_ERXCTRL_ACTIVE)) {
		hregs->rx_ndptr = CPHYSADDR(sp->rx_desc + sp->rx_new);
		seeq_go(sp, hregs, sregs);
	}
}

#define for_each_rx(rd, sp) for((rd) = &(sp)->rx_desc[(sp)->rx_new]; \
				!((rd)->rdma.cntinfo & HPCDMA_OWN); \
				(rd) = &(sp)->rx_desc[(sp)->rx_new])

static inline void sgiseeq_rx(struct net_device *dev, struct sgiseeq_private *sp,
			      struct hpc3_ethregs *hregs,
			      struct sgiseeq_regs *sregs)
{
	struct sgiseeq_rx_desc *rd;
	struct sk_buff *skb = NULL;
	unsigned char pkt_status;
	unsigned char *pkt_pointer = NULL;
	int len = 0;
	unsigned int orig_end = PREV_RX(sp->rx_new);

	/* Service every received packet. */
	for_each_rx(rd, sp) {
		len = PKT_BUF_SZ - (rd->rdma.cntinfo & HPCDMA_BCNT) - 3;
		pkt_pointer = (unsigned char *)(long)rd->buf_vaddr;
		pkt_status = pkt_pointer[len + 2];

		if (pkt_status & SEEQ_RSTAT_FIG) {
			/* Packet is OK. */
			skb = dev_alloc_skb(len + 2);

			if (skb) {
				skb_reserve(skb, 2);
				skb_put(skb, len);

				/* Copy out of kseg1 to avoid silly cache flush. */
				skb_copy_to_linear_data(skb, pkt_pointer + 2, len);
				skb->protocol = eth_type_trans(skb, dev);

				/* We don't want to receive our own packets */
				if (memcmp(eth_hdr(skb)->h_source, dev->dev_addr, ETH_ALEN)) {
					netif_rx(skb);
					dev->last_rx = jiffies;
					sp->stats.rx_packets++;
					sp->stats.rx_bytes += len;
				} else {
					/* Silently drop my own packets */
					dev_kfree_skb_irq(skb);
				}
			} else {
				printk (KERN_NOTICE "%s: Memory squeeze, deferring packet.\n",
					dev->name);
				sp->stats.rx_dropped++;
			}
		} else {
			record_rx_errors(sp, pkt_status);
		}

		/* Return the entry to the ring pool. */
		rd->rdma.cntinfo = RCNTINFO_INIT;
		sp->rx_new = NEXT_RX(sp->rx_new);
	}
	sp->rx_desc[orig_end].rdma.cntinfo &= ~(HPCDMA_EOR);
	sp->rx_desc[PREV_RX(sp->rx_new)].rdma.cntinfo |= HPCDMA_EOR;
	rx_maybe_restart(sp, hregs, sregs);
}

static inline void tx_maybe_reset_collisions(struct sgiseeq_private *sp,
					     struct sgiseeq_regs *sregs)
{
	if (sp->is_edlc) {
		sregs->rw.wregs.control = sp->control & ~(SEEQ_CTRL_XCNT);
		sregs->rw.wregs.control = sp->control;
	}
}

static inline void kick_tx(struct sgiseeq_tx_desc *td,
			   struct hpc3_ethregs *hregs)
{
	/* If the HPC aint doin nothin, and there are more packets
	 * with ETXD cleared and XIU set we must make very certain
	 * that we restart the HPC else we risk locking up the
	 * adapter.  The following code is only safe iff the HPCDMA
	 * is not active!
	 */
	while ((td->tdma.cntinfo & (HPCDMA_XIU | HPCDMA_ETXD)) ==
	      (HPCDMA_XIU | HPCDMA_ETXD))
		td = (struct sgiseeq_tx_desc *)(long) CKSEG1ADDR(td->tdma.pnext);
	if (td->tdma.cntinfo & HPCDMA_XIU) {
		hregs->tx_ndptr = CPHYSADDR(td);
		hregs->tx_ctrl = HPC3_ETXCTRL_ACTIVE;
	}
}

static inline void sgiseeq_tx(struct net_device *dev, struct sgiseeq_private *sp,
			      struct hpc3_ethregs *hregs,
			      struct sgiseeq_regs *sregs)
{
	struct sgiseeq_tx_desc *td;
	unsigned long status = hregs->tx_ctrl;
	int j;

	tx_maybe_reset_collisions(sp, sregs);

	if (!(status & (HPC3_ETXCTRL_ACTIVE | SEEQ_TSTAT_PTRANS))) {
		/* Oops, HPC detected some sort of error. */
		if (status & SEEQ_TSTAT_R16)
			sp->stats.tx_aborted_errors++;
		if (status & SEEQ_TSTAT_UFLOW)
			sp->stats.tx_fifo_errors++;
		if (status & SEEQ_TSTAT_LCLS)
			sp->stats.collisions++;
	}

	/* Ack 'em... */
	for (j = sp->tx_old; j != sp->tx_new; j = NEXT_TX(j)) {
		td = &sp->tx_desc[j];

		if (!(td->tdma.cntinfo & (HPCDMA_XIU)))
			break;
		if (!(td->tdma.cntinfo & (HPCDMA_ETXD))) {
			if (!(status & HPC3_ETXCTRL_ACTIVE)) {
				hregs->tx_ndptr = CPHYSADDR(td);
				hregs->tx_ctrl = HPC3_ETXCTRL_ACTIVE;
			}
			break;
		}
		sp->stats.tx_packets++;
		sp->tx_old = NEXT_TX(sp->tx_old);
		td->tdma.cntinfo &= ~(HPCDMA_XIU | HPCDMA_XIE);
		td->tdma.cntinfo |= HPCDMA_EOX;
	}
}

static irqreturn_t sgiseeq_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct hpc3_ethregs *hregs = sp->hregs;
	struct sgiseeq_regs *sregs = sp->sregs;

	spin_lock(&sp->tx_lock);

	/* Ack the IRQ and set software state. */
	hregs->reset = HPC3_ERST_CLRIRQ;

	/* Always check for received packets. */
	sgiseeq_rx(dev, sp, hregs, sregs);

	/* Only check for tx acks if we have something queued. */
	if (sp->tx_old != sp->tx_new)
		sgiseeq_tx(dev, sp, hregs, sregs);

	if ((TX_BUFFS_AVAIL(sp) > 0) && netif_queue_stopped(dev)) {
		netif_wake_queue(dev);
	}
	spin_unlock(&sp->tx_lock);

	return IRQ_HANDLED;
}

static int sgiseeq_open(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	unsigned int irq = dev->irq;
	int err;

	if (request_irq(irq, sgiseeq_interrupt, 0, sgiseeqstr, dev)) {
		printk(KERN_ERR "Seeq8003: Can't get irq %d\n", dev->irq);
		err = -EAGAIN;
	}

	err = init_seeq(dev, sp, sregs);
	if (err)
		goto out_free_irq;

	netif_start_queue(dev);

	return 0;

out_free_irq:
	free_irq(irq, dev);

	return err;
}

static int sgiseeq_close(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	unsigned int irq = dev->irq;

	netif_stop_queue(dev);

	/* Shutdown the Seeq. */
	reset_hpc3_and_seeq(sp->hregs, sregs);
	free_irq(irq, dev);

	return 0;
}

static inline int sgiseeq_reset(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	int err;

	err = init_seeq(dev, sp, sregs);
	if (err)
		return err;

	dev->trans_start = jiffies;
	netif_wake_queue(dev);

	return 0;
}

static int sgiseeq_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct hpc3_ethregs *hregs = sp->hregs;
	unsigned long flags;
	struct sgiseeq_tx_desc *td;
	int skblen, len, entry;

	spin_lock_irqsave(&sp->tx_lock, flags);

	/* Setup... */
	skblen = skb->len;
	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;
	sp->stats.tx_bytes += len;
	entry = sp->tx_new;
	td = &sp->tx_desc[entry];

	/* Create entry.  There are so many races with adding a new
	 * descriptor to the chain:
	 * 1) Assume that the HPC is off processing a DMA chain while
	 *    we are changing all of the following.
	 * 2) Do no allow the HPC to look at a new descriptor until
	 *    we have completely set up it's state.  This means, do
	 *    not clear HPCDMA_EOX in the current last descritptor
	 *    until the one we are adding looks consistent and could
	 *    be processes right now.
	 * 3) The tx interrupt code must notice when we've added a new
	 *    entry and the HPC got to the end of the chain before we
	 *    added this new entry and restarted it.
	 */
	skb_copy_from_linear_data(skb, (char *)(long)td->buf_vaddr, skblen);
	if (len != skblen)
		memset((char *)(long)td->buf_vaddr + skb->len, 0, len-skblen);
	td->tdma.cntinfo = (len & HPCDMA_BCNT) |
	                   HPCDMA_XIU | HPCDMA_EOXP | HPCDMA_XIE | HPCDMA_EOX;
	if (sp->tx_old != sp->tx_new) {
		struct sgiseeq_tx_desc *backend;

		backend = &sp->tx_desc[PREV_TX(sp->tx_new)];
		backend->tdma.cntinfo &= ~HPCDMA_EOX;
	}
	sp->tx_new = NEXT_TX(sp->tx_new); /* Advance. */

	/* Maybe kick the HPC back into motion. */
	if (!(hregs->tx_ctrl & HPC3_ETXCTRL_ACTIVE))
		kick_tx(&sp->tx_desc[sp->tx_old], hregs);

	dev->trans_start = jiffies;
	dev_kfree_skb(skb);

	if (!TX_BUFFS_AVAIL(sp))
		netif_stop_queue(dev);
	spin_unlock_irqrestore(&sp->tx_lock, flags);

	return 0;
}

static void timeout(struct net_device *dev)
{
	printk(KERN_NOTICE "%s: transmit timed out, resetting\n", dev->name);
	sgiseeq_reset(dev);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static struct net_device_stats *sgiseeq_get_stats(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);

	return &sp->stats;
}

static void sgiseeq_set_multicast(struct net_device *dev)
{
	struct sgiseeq_private *sp = (struct sgiseeq_private *) dev->priv;
	unsigned char oldmode = sp->mode;

	if(dev->flags & IFF_PROMISC)
		sp->mode = SEEQ_RCMD_RANY;
	else if ((dev->flags & IFF_ALLMULTI) || dev->mc_count)
		sp->mode = SEEQ_RCMD_RBMCAST;
	else
		sp->mode = SEEQ_RCMD_RBCAST;

	/* XXX I know this sucks, but is there a better way to reprogram
	 * XXX the receiver? At least, this shouldn't happen too often.
	 */

	if (oldmode != sp->mode)
		sgiseeq_reset(dev);
}

static inline void setup_tx_ring(struct sgiseeq_tx_desc *buf, int nbufs)
{
	int i = 0;

	while (i < (nbufs - 1)) {
		buf[i].tdma.pnext = CPHYSADDR(buf + i + 1);
		buf[i].tdma.pbuf = 0;
		i++;
	}
	buf[i].tdma.pnext = CPHYSADDR(buf);
}

static inline void setup_rx_ring(struct sgiseeq_rx_desc *buf, int nbufs)
{
	int i = 0;

	while (i < (nbufs - 1)) {
		buf[i].rdma.pnext = CPHYSADDR(buf + i + 1);
		buf[i].rdma.pbuf = 0;
		i++;
	}
	buf[i].rdma.pbuf = 0;
	buf[i].rdma.pnext = CPHYSADDR(buf);
}

#define ALIGNED(x)  ((((unsigned long)(x)) + 0xf) & ~(0xf))

static int __init sgiseeq_probe(struct platform_device *pdev)
{
	struct sgiseeq_platform_data *pd = pdev->dev.platform_data;
	struct hpc3_regs *hpcregs = pd->hpc;
	struct sgiseeq_init_block *sr;
	unsigned int irq = pd->irq;
	struct sgiseeq_private *sp;
	struct net_device *dev;
	int err, i;

	dev = alloc_etherdev(sizeof (struct sgiseeq_private));
	if (!dev) {
		printk(KERN_ERR "Sgiseeq: Etherdev alloc failed, aborting.\n");
		err = -ENOMEM;
		goto err_out;
	}

	platform_set_drvdata(pdev, dev);
	sp = netdev_priv(dev);

	/* Make private data page aligned */
	sr = (struct sgiseeq_init_block *) get_zeroed_page(GFP_KERNEL);
	if (!sr) {
		printk(KERN_ERR "Sgiseeq: Page alloc failed, aborting.\n");
		err = -ENOMEM;
		goto err_out_free_dev;
	}
	sp->srings = sr;

	memcpy(dev->dev_addr, pd->mac, ETH_ALEN);

#ifdef DEBUG
	gpriv = sp;
	gdev = dev;
#endif
	sp->sregs = (struct sgiseeq_regs *) &hpcregs->eth_ext[0];
	sp->hregs = &hpcregs->ethregs;
	sp->name = sgiseeqstr;
	sp->mode = SEEQ_RCMD_RBCAST;

	sp->rx_desc = (struct sgiseeq_rx_desc *)
	              CKSEG1ADDR(ALIGNED(&sp->srings->rxvector[0]));
	dma_cache_wback_inv((unsigned long)&sp->srings->rxvector,
	                    sizeof(sp->srings->rxvector));
	sp->tx_desc = (struct sgiseeq_tx_desc *)
	              CKSEG1ADDR(ALIGNED(&sp->srings->txvector[0]));
	dma_cache_wback_inv((unsigned long)&sp->srings->txvector,
	                    sizeof(sp->srings->txvector));

	/* A couple calculations now, saves many cycles later. */
	setup_rx_ring(sp->rx_desc, SEEQ_RX_BUFFERS);
	setup_tx_ring(sp->tx_desc, SEEQ_TX_BUFFERS);

	/* Setup PIO and DMA transfer timing */
	sp->hregs->pconfig = 0x161;
	sp->hregs->dconfig = HPC3_EDCFG_FIRQ | HPC3_EDCFG_FEOP |
			     HPC3_EDCFG_FRXDC | HPC3_EDCFG_PTO | 0x026;

	/* Setup PIO and DMA transfer timing */
	sp->hregs->pconfig = 0x161;
	sp->hregs->dconfig = HPC3_EDCFG_FIRQ | HPC3_EDCFG_FEOP |
			     HPC3_EDCFG_FRXDC | HPC3_EDCFG_PTO | 0x026;

	/* Reset the chip. */
	hpc3_eth_reset(sp->hregs);

	sp->is_edlc = !(sp->sregs->rw.rregs.collision_tx[0] & 0xff);
	if (sp->is_edlc)
		sp->control = SEEQ_CTRL_XCNT | SEEQ_CTRL_ACCNT |
			      SEEQ_CTRL_SFLAG | SEEQ_CTRL_ESHORT |
			      SEEQ_CTRL_ENCARR;

	dev->open		= sgiseeq_open;
	dev->stop		= sgiseeq_close;
	dev->hard_start_xmit	= sgiseeq_start_xmit;
	dev->tx_timeout		= timeout;
	dev->watchdog_timeo	= (200 * HZ) / 1000;
	dev->get_stats		= sgiseeq_get_stats;
	dev->set_multicast_list	= sgiseeq_set_multicast;
	dev->set_mac_address	= sgiseeq_set_mac_address;
	dev->irq		= irq;

	if (register_netdev(dev)) {
		printk(KERN_ERR "Sgiseeq: Cannot register net device, "
		       "aborting.\n");
		err = -ENODEV;
		goto err_out_free_page;
	}

	printk(KERN_INFO "%s: %s ", dev->name, sgiseeqstr);
	for (i = 0; i < 6; i++)
		printk("%2.2x%c", dev->dev_addr[i], i == 5 ? '\n' : ':');

	return 0;

err_out_free_page:
	free_page((unsigned long) sp->srings);
err_out_free_dev:
	kfree(dev);

err_out:
	return err;
}

static int __exit sgiseeq_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct sgiseeq_private *sp = netdev_priv(dev);

	unregister_netdev(dev);
	free_page((unsigned long) sp->srings);
	free_netdev(dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver sgiseeq_driver = {
	.probe	= sgiseeq_probe,
	.remove	= __devexit_p(sgiseeq_remove),
	.driver = {
		.name	= "sgiseeq"
	}
};

static int __init sgiseeq_module_init(void)
{
	if (platform_driver_register(&sgiseeq_driver)) {
		printk(KERN_ERR "Driver registration failed\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit sgiseeq_module_exit(void)
{
	platform_driver_unregister(&sgiseeq_driver);
}

module_init(sgiseeq_module_init);
module_exit(sgiseeq_module_exit);

MODULE_DESCRIPTION("SGI Seeq 8003 driver");
MODULE_AUTHOR("Linux/MIPS Mailing List <linux-mips@linux-mips.org>");
MODULE_LICENSE("GPL");

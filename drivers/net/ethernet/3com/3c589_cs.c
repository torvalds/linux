/* ======================================================================
 *
 * A PCMCIA ethernet driver for the 3com 3c589 card.
 *
 * Copyright (C) 1999 David A. Hinds -- dahinds@users.sourceforge.net
 *
 * 3c589_cs.c 1.162 2001/10/13 00:08:50
 *
 * The network driver code is based on Donald Becker's 3c589 code:
 *
 * Written 1994 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may be used and
 * distributed according to the terms of the GNU General Public License,
 * incorporated herein by reference.
 * Donald Becker may be reached at becker@scyld.com
 *
 * Updated for 2.5.x by Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * ======================================================================
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"3c589_cs"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>


/* To minimize the size of the driver source I only define operating
 * constants if they are used several times. You'll need the manual
 * if you want to understand driver details.
 */

/* Offsets from base I/O address. */
#define EL3_DATA	0x00
#define EL3_TIMER	0x0a
#define EL3_CMD		0x0e
#define EL3_STATUS	0x0e

#define EEPROM_READ	0x0080
#define EEPROM_BUSY	0x8000

#define EL3WINDOW(win_num) outw(SelectWindow + (win_num), ioaddr + EL3_CMD)

/* The top five bits written to EL3_CMD are a command, the lower
 * 11 bits are the parameter, if applicable.
 */

enum c509cmd {
	TotalReset	= 0<<11,
	SelectWindow	= 1<<11,
	StartCoax	= 2<<11,
	RxDisable	= 3<<11,
	RxEnable	= 4<<11,
	RxReset		= 5<<11,
	RxDiscard	= 8<<11,
	TxEnable	= 9<<11,
	TxDisable	= 10<<11,
	TxReset		= 11<<11,
	FakeIntr	= 12<<11,
	AckIntr		= 13<<11,
	SetIntrEnb	= 14<<11,
	SetStatusEnb	= 15<<11,
	SetRxFilter	= 16<<11,
	SetRxThreshold	= 17<<11,
	SetTxThreshold	= 18<<11,
	SetTxStart	= 19<<11,
	StatsEnable	= 21<<11,
	StatsDisable	= 22<<11,
	StopCoax	= 23<<11
};

enum c509status {
	IntLatch	= 0x0001,
	AdapterFailure	= 0x0002,
	TxComplete	= 0x0004,
	TxAvailable	= 0x0008,
	RxComplete	= 0x0010,
	RxEarly		= 0x0020,
	IntReq		= 0x0040,
	StatsFull	= 0x0080,
	CmdBusy		= 0x1000
};

/* The SetRxFilter command accepts the following classes: */
enum RxFilter {
	RxStation	= 1,
	RxMulticast	= 2,
	RxBroadcast	= 4,
	RxProm		= 8
};

/* Register window 1 offsets, the window used in normal operation. */
#define TX_FIFO		0x00
#define RX_FIFO		0x00
#define RX_STATUS	0x08
#define TX_STATUS	0x0B
#define TX_FREE		0x0C	/* Remaining free bytes in Tx buffer. */

#define WN0_IRQ		0x08	/* Window 0: Set IRQ line in bits 12-15. */
#define WN4_MEDIA	0x0A	/* Window 4: Various transcvr/media bits. */
#define MEDIA_TP	0x00C0	/* Enable link beat and jabber for 10baseT. */
#define MEDIA_LED	0x0001	/* Enable link light on 3C589E cards. */

/* Time in jiffies before concluding Tx hung */
#define TX_TIMEOUT	((400*HZ)/1000)

struct el3_private {
	struct pcmcia_device	*p_dev;
	/* For transceiver monitoring */
	struct timer_list	media;
	u16			media_status;
	u16			fast_poll;
	unsigned long		last_irq;
	spinlock_t		lock;
};

static const char *if_names[] = { "auto", "10baseT", "10base2", "AUI" };

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("3Com 3c589 series PCMCIA ethernet driver");
MODULE_LICENSE("GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

/* Special hook for setting if_port when module is loaded */
INT_MODULE_PARM(if_port, 0);


/*====================================================================*/

static int tc589_config(struct pcmcia_device *link);
static void tc589_release(struct pcmcia_device *link);

static u16 read_eeprom(unsigned int ioaddr, int index);
static void tc589_reset(struct net_device *dev);
static void media_check(struct timer_list *t);
static int el3_config(struct net_device *dev, struct ifmap *map);
static int el3_open(struct net_device *dev);
static netdev_tx_t el3_start_xmit(struct sk_buff *skb,
					struct net_device *dev);
static irqreturn_t el3_interrupt(int irq, void *dev_id);
static void update_stats(struct net_device *dev);
static struct net_device_stats *el3_get_stats(struct net_device *dev);
static int el3_rx(struct net_device *dev);
static int el3_close(struct net_device *dev);
static void el3_tx_timeout(struct net_device *dev, unsigned int txqueue);
static void set_rx_mode(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static const struct ethtool_ops netdev_ethtool_ops;

static void tc589_detach(struct pcmcia_device *p_dev);

static const struct net_device_ops el3_netdev_ops = {
	.ndo_open		= el3_open,
	.ndo_stop		= el3_close,
	.ndo_start_xmit		= el3_start_xmit,
	.ndo_tx_timeout		= el3_tx_timeout,
	.ndo_set_config		= el3_config,
	.ndo_get_stats		= el3_get_stats,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int tc589_probe(struct pcmcia_device *link)
{
	struct el3_private *lp;
	struct net_device *dev;

	dev_dbg(&link->dev, "3c589_attach()\n");

	/* Create new ethernet device */
	dev = alloc_etherdev(sizeof(struct el3_private));
	if (!dev)
		return -ENOMEM;
	lp = netdev_priv(dev);
	link->priv = dev;
	lp->p_dev = link;

	spin_lock_init(&lp->lock);
	link->resource[0]->end = 16;
	link->resource[0]->flags |= IO_DATA_PATH_WIDTH_16;

	link->config_flags |= CONF_ENABLE_IRQ;
	link->config_index = 1;

	dev->netdev_ops = &el3_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->ethtool_ops = &netdev_ethtool_ops;

	return tc589_config(link);
}

static void tc589_detach(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	dev_dbg(&link->dev, "3c589_detach\n");

	unregister_netdev(dev);

	tc589_release(link);

	free_netdev(dev);
} /* tc589_detach */

static int tc589_config(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	int ret, i, j, multi = 0, fifo;
	__be16 addr[ETH_ALEN / 2];
	unsigned int ioaddr;
	static const char * const ram_split[] = {"5:3", "3:1", "1:1", "3:5"};
	u8 *buf;
	size_t len;

	dev_dbg(&link->dev, "3c589_config\n");

	/* Is this a 3c562? */
	if (link->manf_id != MANFID_3COM)
		dev_info(&link->dev, "hmmm, is this really a 3Com card??\n");
	multi = (link->card_id == PRODID_3COM_3C562);

	link->io_lines = 16;

	/* For the 3c562, the base address must be xx00-xx7f */
	for (i = j = 0; j < 0x400; j += 0x10) {
		if (multi && (j & 0x80))
			continue;
		link->resource[0]->start = j ^ 0x300;
		i = pcmcia_request_io(link);
		if (i == 0)
			break;
	}
	if (i != 0)
		goto failed;

	ret = pcmcia_request_irq(link, el3_interrupt);
	if (ret)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	dev->irq = link->irq;
	dev->base_addr = link->resource[0]->start;
	ioaddr = dev->base_addr;
	EL3WINDOW(0);

	/* The 3c589 has an extra EEPROM for configuration info, including
	 * the hardware address.  The 3c562 puts the address in the CIS.
	 */
	len = pcmcia_get_tuple(link, 0x88, &buf);
	if (buf && len >= 6) {
		for (i = 0; i < 3; i++)
			addr[i] = htons(le16_to_cpu(buf[i*2]));
		kfree(buf);
	} else {
		kfree(buf); /* 0 < len < 6 */
		for (i = 0; i < 3; i++)
			addr[i] = htons(read_eeprom(ioaddr, i));
		if (addr[0] == htons(0x6060)) {
			dev_err(&link->dev, "IO port conflict at 0x%03lx-0x%03lx\n",
					dev->base_addr, dev->base_addr+15);
			goto failed;
		}
	}
	eth_hw_addr_set(dev, (u8 *)addr);

	/* The address and resource configuration register aren't loaded from
	 * the EEPROM and *must* be set to 0 and IRQ3 for the PCMCIA version.
	 */

	outw(0x3f00, ioaddr + 8);
	fifo = inl(ioaddr);

	/* The if_port symbol can be set when the module is loaded */
	if ((if_port >= 0) && (if_port <= 3))
		dev->if_port = if_port;
	else
		dev_err(&link->dev, "invalid if_port requested\n");

	SET_NETDEV_DEV(dev, &link->dev);

	if (register_netdev(dev) != 0) {
		dev_err(&link->dev, "register_netdev() failed\n");
		goto failed;
	}

	netdev_info(dev, "3Com 3c%s, io %#3lx, irq %d, hw_addr %pM\n",
			(multi ? "562" : "589"), dev->base_addr, dev->irq,
			dev->dev_addr);
	netdev_info(dev, "  %dK FIFO split %s Rx:Tx, %s xcvr\n",
			(fifo & 7) ? 32 : 8, ram_split[(fifo >> 16) & 3],
			if_names[dev->if_port]);
	return 0;

failed:
	tc589_release(link);
	return -ENODEV;
} /* tc589_config */

static void tc589_release(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
}

static int tc589_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int tc589_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		tc589_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}

/*====================================================================*/

/* Use this for commands that may take time to finish */

static void tc589_wait_for_completion(struct net_device *dev, int cmd)
{
	int i = 100;
	outw(cmd, dev->base_addr + EL3_CMD);
	while (--i > 0)
		if (!(inw(dev->base_addr + EL3_STATUS) & 0x1000))
			break;
	if (i == 0)
		netdev_warn(dev, "command 0x%04x did not complete!\n", cmd);
}

/* Read a word from the EEPROM using the regular EEPROM access register.
 * Assume that we are in register window zero.
 */

static u16 read_eeprom(unsigned int ioaddr, int index)
{
	int i;
	outw(EEPROM_READ + index, ioaddr + 10);
	/* Reading the eeprom takes 162 us */
	for (i = 1620; i >= 0; i--)
		if ((inw(ioaddr + 10) & EEPROM_BUSY) == 0)
			break;
	return inw(ioaddr + 12);
}

/* Set transceiver type, perhaps to something other than what the user
 * specified in dev->if_port.
 */

static void tc589_set_xcvr(struct net_device *dev, int if_port)
{
	struct el3_private *lp = netdev_priv(dev);
	unsigned int ioaddr = dev->base_addr;

	EL3WINDOW(0);
	switch (if_port) {
	case 0:
	case 1:
		outw(0, ioaddr + 6);
		break;
	case 2:
		outw(3<<14, ioaddr + 6);
		break;
	case 3:
		outw(1<<14, ioaddr + 6);
		break;
	}
	/* On PCMCIA, this just turns on the LED */
	outw((if_port == 2) ? StartCoax : StopCoax, ioaddr + EL3_CMD);
	/* 10baseT interface, enable link beat and jabber check. */
	EL3WINDOW(4);
	outw(MEDIA_LED | ((if_port < 2) ? MEDIA_TP : 0), ioaddr + WN4_MEDIA);
	EL3WINDOW(1);
	if (if_port == 2)
		lp->media_status = ((dev->if_port == 0) ? 0x8000 : 0x4000);
	else
		lp->media_status = ((dev->if_port == 0) ? 0x4010 : 0x8800);
}

static void dump_status(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	EL3WINDOW(1);
	netdev_info(dev, "  irq status %04x, rx status %04x, tx status %02x  tx free %04x\n",
			inw(ioaddr+EL3_STATUS), inw(ioaddr+RX_STATUS),
			inb(ioaddr+TX_STATUS), inw(ioaddr+TX_FREE));
	EL3WINDOW(4);
	netdev_info(dev, "  diagnostics: fifo %04x net %04x ethernet %04x media %04x\n",
			inw(ioaddr+0x04), inw(ioaddr+0x06), inw(ioaddr+0x08),
			inw(ioaddr+0x0a));
	EL3WINDOW(1);
}

/* Reset and restore all of the 3c589 registers. */
static void tc589_reset(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	int i;

	EL3WINDOW(0);
	outw(0x0001, ioaddr + 4);			/* Activate board. */
	outw(0x3f00, ioaddr + 8);			/* Set the IRQ line. */

	/* Set the station address in window 2. */
	EL3WINDOW(2);
	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + i);

	tc589_set_xcvr(dev, dev->if_port);

	/* Switch to the stats window, and clear all stats by reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	EL3WINDOW(6);
	for (i = 0; i < 9; i++)
		inb(ioaddr+i);
	inw(ioaddr + 10);
	inw(ioaddr + 12);

	/* Switch to register set 1 for normal use. */
	EL3WINDOW(1);

	set_rx_mode(dev);
	outw(StatsEnable, ioaddr + EL3_CMD); /* Turn on statistics. */
	outw(RxEnable, ioaddr + EL3_CMD); /* Enable the receiver. */
	outw(TxEnable, ioaddr + EL3_CMD); /* Enable transmitter. */
	/* Allow status bits to be seen. */
	outw(SetStatusEnb | 0xff, ioaddr + EL3_CMD);
	/* Ack all pending events, and set active indicator mask. */
	outw(AckIntr | IntLatch | TxAvailable | RxEarly | IntReq,
	 ioaddr + EL3_CMD);
	outw(SetIntrEnb | IntLatch | TxAvailable | RxComplete | StatsFull
	 | AdapterFailure, ioaddr + EL3_CMD);
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	snprintf(info->bus_info, sizeof(info->bus_info),
		"PCMCIA 0x%lx", dev->base_addr);
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

static int el3_config(struct net_device *dev, struct ifmap *map)
{
	if ((map->port != (u_char)(-1)) && (map->port != dev->if_port)) {
		if (map->port <= 3) {
			dev->if_port = map->port;
			netdev_info(dev, "switched to %s port\n", if_names[dev->if_port]);
			tc589_set_xcvr(dev, dev->if_port);
		} else {
			return -EINVAL;
		}
	}
	return 0;
}

static int el3_open(struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	struct pcmcia_device *link = lp->p_dev;

	if (!pcmcia_dev_present(link))
		return -ENODEV;

	link->open++;
	netif_start_queue(dev);

	tc589_reset(dev);
	timer_setup(&lp->media, media_check, 0);
	mod_timer(&lp->media, jiffies + HZ);

	dev_dbg(&link->dev, "%s: opened, status %4.4x.\n",
	  dev->name, inw(dev->base_addr + EL3_STATUS));

	return 0;
}

static void el3_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	unsigned int ioaddr = dev->base_addr;

	netdev_warn(dev, "Transmit timed out!\n");
	dump_status(dev);
	dev->stats.tx_errors++;
	netif_trans_update(dev); /* prevent tx timeout */
	/* Issue TX_RESET and TX_START commands. */
	tc589_wait_for_completion(dev, TxReset);
	outw(TxEnable, ioaddr + EL3_CMD);
	netif_wake_queue(dev);
}

static void pop_tx_status(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	int i;

	/* Clear the Tx status stack. */
	for (i = 32; i > 0; i--) {
		u_char tx_status = inb(ioaddr + TX_STATUS);
		if (!(tx_status & 0x84))
			break;
		/* reset transmitter on jabber error or underrun */
		if (tx_status & 0x30)
			tc589_wait_for_completion(dev, TxReset);
		if (tx_status & 0x38) {
			netdev_dbg(dev, "transmit error: status 0x%02x\n", tx_status);
			outw(TxEnable, ioaddr + EL3_CMD);
			dev->stats.tx_aborted_errors++;
		}
		outb(0x00, ioaddr + TX_STATUS); /* Pop the status stack. */
	}
}

static netdev_tx_t el3_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	struct el3_private *priv = netdev_priv(dev);
	unsigned long flags;

	netdev_dbg(dev, "el3_start_xmit(length = %ld) called, status %4.4x.\n",
	       (long)skb->len, inw(ioaddr + EL3_STATUS));

	spin_lock_irqsave(&priv->lock, flags);

	dev->stats.tx_bytes += skb->len;

	/* Put out the doubleword header... */
	outw(skb->len, ioaddr + TX_FIFO);
	outw(0x00, ioaddr + TX_FIFO);
	/* ... and the packet rounded to a doubleword. */
	outsl(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);

	if (inw(ioaddr + TX_FREE) <= 1536) {
		netif_stop_queue(dev);
		/* Interrupt us when the FIFO has room for max-sized packet. */
		outw(SetTxThreshold + 1536, ioaddr + EL3_CMD);
	}

	pop_tx_status(dev);
	spin_unlock_irqrestore(&priv->lock, flags);
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/* The EL3 interrupt handler. */
static irqreturn_t el3_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct el3_private *lp = netdev_priv(dev);
	unsigned int ioaddr;
	__u16 status;
	int i = 0, handled = 1;

	if (!netif_device_present(dev))
		return IRQ_NONE;

	ioaddr = dev->base_addr;

	netdev_dbg(dev, "interrupt, status %4.4x.\n", inw(ioaddr + EL3_STATUS));

	spin_lock(&lp->lock);
	while ((status = inw(ioaddr + EL3_STATUS)) &
	(IntLatch | RxComplete | StatsFull)) {
		if ((status & 0xe000) != 0x2000) {
			netdev_dbg(dev, "interrupt from dead card\n");
			handled = 0;
			break;
		}
		if (status & RxComplete)
			el3_rx(dev);
		if (status & TxAvailable) {
			netdev_dbg(dev, "    TX room bit was handled.\n");
			/* There's room in the FIFO for a full-sized packet. */
			outw(AckIntr | TxAvailable, ioaddr + EL3_CMD);
			netif_wake_queue(dev);
		}
		if (status & TxComplete)
			pop_tx_status(dev);
		if (status & (AdapterFailure | RxEarly | StatsFull)) {
			/* Handle all uncommon interrupts. */
			if (status & StatsFull)		/* Empty statistics. */
				update_stats(dev);
			if (status & RxEarly) {
				/* Rx early is unused. */
				el3_rx(dev);
				outw(AckIntr | RxEarly, ioaddr + EL3_CMD);
			}
			if (status & AdapterFailure) {
				u16 fifo_diag;
				EL3WINDOW(4);
				fifo_diag = inw(ioaddr + 4);
				EL3WINDOW(1);
				netdev_warn(dev, "adapter failure, FIFO diagnostic register %04x.\n",
			    fifo_diag);
				if (fifo_diag & 0x0400) {
					/* Tx overrun */
					tc589_wait_for_completion(dev, TxReset);
					outw(TxEnable, ioaddr + EL3_CMD);
				}
				if (fifo_diag & 0x2000) {
					/* Rx underrun */
					tc589_wait_for_completion(dev, RxReset);
					set_rx_mode(dev);
					outw(RxEnable, ioaddr + EL3_CMD);
				}
				outw(AckIntr | AdapterFailure, ioaddr + EL3_CMD);
			}
		}
		if (++i > 10) {
			netdev_err(dev, "infinite loop in interrupt, status %4.4x.\n",
					status);
			/* Clear all interrupts */
			outw(AckIntr | 0xFF, ioaddr + EL3_CMD);
			break;
		}
		/* Acknowledge the IRQ. */
		outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD);
	}
	lp->last_irq = jiffies;
	spin_unlock(&lp->lock);
	netdev_dbg(dev, "exiting interrupt, status %4.4x.\n",
			inw(ioaddr + EL3_STATUS));
	return IRQ_RETVAL(handled);
}

static void media_check(struct timer_list *t)
{
	struct el3_private *lp = from_timer(lp, t, media);
	struct net_device *dev = lp->p_dev->priv;
	unsigned int ioaddr = dev->base_addr;
	u16 media, errs;
	unsigned long flags;

	if (!netif_device_present(dev))
		goto reschedule;

	/* Check for pending interrupt with expired latency timer: with
	 * this, we can limp along even if the interrupt is blocked
	 */
	if ((inw(ioaddr + EL3_STATUS) & IntLatch) &&
	(inb(ioaddr + EL3_TIMER) == 0xff)) {
		if (!lp->fast_poll)
			netdev_warn(dev, "interrupt(s) dropped!\n");

		local_irq_save(flags);
		el3_interrupt(dev->irq, dev);
		local_irq_restore(flags);

		lp->fast_poll = HZ;
	}
	if (lp->fast_poll) {
		lp->fast_poll--;
		lp->media.expires = jiffies + HZ/100;
		add_timer(&lp->media);
		return;
	}

	/* lp->lock guards the EL3 window. Window should always be 1 except
	 * when the lock is held
	 */

	spin_lock_irqsave(&lp->lock, flags);
	EL3WINDOW(4);
	media = inw(ioaddr+WN4_MEDIA) & 0xc810;

	/* Ignore collisions unless we've had no irq's recently */
	if (time_before(jiffies, lp->last_irq + HZ)) {
		media &= ~0x0010;
	} else {
		/* Try harder to detect carrier errors */
		EL3WINDOW(6);
		outw(StatsDisable, ioaddr + EL3_CMD);
		errs = inb(ioaddr + 0);
		outw(StatsEnable, ioaddr + EL3_CMD);
		dev->stats.tx_carrier_errors += errs;
		if (errs || (lp->media_status & 0x0010))
			media |= 0x0010;
	}

	if (media != lp->media_status) {
		if ((media & lp->media_status & 0x8000) &&
				((lp->media_status ^ media) & 0x0800))
		netdev_info(dev, "%s link beat\n",
				(lp->media_status & 0x0800 ? "lost" : "found"));
		else if ((media & lp->media_status & 0x4000) &&
		 ((lp->media_status ^ media) & 0x0010))
		netdev_info(dev, "coax cable %s\n",
				(lp->media_status & 0x0010 ? "ok" : "problem"));
		if (dev->if_port == 0) {
			if (media & 0x8000) {
				if (media & 0x0800)
					netdev_info(dev, "flipped to 10baseT\n");
				else
			tc589_set_xcvr(dev, 2);
			} else if (media & 0x4000) {
				if (media & 0x0010)
					tc589_set_xcvr(dev, 1);
				else
					netdev_info(dev, "flipped to 10base2\n");
			}
		}
		lp->media_status = media;
	}

	EL3WINDOW(1);
	spin_unlock_irqrestore(&lp->lock, flags);

reschedule:
	lp->media.expires = jiffies + HZ;
	add_timer(&lp->media);
}

static struct net_device_stats *el3_get_stats(struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	unsigned long flags;
	struct pcmcia_device *link = lp->p_dev;

	if (pcmcia_dev_present(link)) {
		spin_lock_irqsave(&lp->lock, flags);
		update_stats(dev);
		spin_unlock_irqrestore(&lp->lock, flags);
	}
	return &dev->stats;
}

/* Update statistics.  We change to register window 6, so this should be run
* single-threaded if the device is active. This is expected to be a rare
* operation, and it's simpler for the rest of the driver to assume that
* window 1 is always valid rather than use a special window-state variable.
*
* Caller must hold the lock for this
*/

static void update_stats(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;

	netdev_dbg(dev, "updating the statistics.\n");
	/* Turn off statistics updates while reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	/* Switch to the stats window, and read everything. */
	EL3WINDOW(6);
	dev->stats.tx_carrier_errors	+= inb(ioaddr + 0);
	dev->stats.tx_heartbeat_errors	+= inb(ioaddr + 1);
	/* Multiple collisions. */
	inb(ioaddr + 2);
	dev->stats.collisions		+= inb(ioaddr + 3);
	dev->stats.tx_window_errors		+= inb(ioaddr + 4);
	dev->stats.rx_fifo_errors		+= inb(ioaddr + 5);
	dev->stats.tx_packets		+= inb(ioaddr + 6);
	/* Rx packets   */
	inb(ioaddr + 7);
	/* Tx deferrals */
	inb(ioaddr + 8);
	/* Rx octets */
	inw(ioaddr + 10);
	/* Tx octets */
	inw(ioaddr + 12);

	/* Back to window 1, and turn statistics back on. */
	EL3WINDOW(1);
	outw(StatsEnable, ioaddr + EL3_CMD);
}

static int el3_rx(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	int worklimit = 32;
	short rx_status;

	netdev_dbg(dev, "in rx_packet(), status %4.4x, rx_status %4.4x.\n",
	       inw(ioaddr+EL3_STATUS), inw(ioaddr+RX_STATUS));
	while (!((rx_status = inw(ioaddr + RX_STATUS)) & 0x8000) &&
		    worklimit > 0) {
		worklimit--;
		if (rx_status & 0x4000) { /* Error, update stats. */
			short error = rx_status & 0x3800;
			dev->stats.rx_errors++;
			switch (error) {
			case 0x0000:
				dev->stats.rx_over_errors++;
				break;
			case 0x0800:
				dev->stats.rx_length_errors++;
				break;
			case 0x1000:
				dev->stats.rx_frame_errors++;
				break;
			case 0x1800:
				dev->stats.rx_length_errors++;
				break;
			case 0x2000:
				dev->stats.rx_frame_errors++;
				break;
			case 0x2800:
				dev->stats.rx_crc_errors++;
				break;
			}
		} else {
			short pkt_len = rx_status & 0x7ff;
			struct sk_buff *skb;

			skb = netdev_alloc_skb(dev, pkt_len + 5);

			netdev_dbg(dev, "    Receiving packet size %d status %4.4x.\n",
		       pkt_len, rx_status);
			if (skb != NULL) {
				skb_reserve(skb, 2);
				insl(ioaddr+RX_FIFO, skb_put(skb, pkt_len),
			(pkt_len+3)>>2);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += pkt_len;
			} else {
				netdev_dbg(dev, "couldn't allocate a sk_buff of size %d.\n",
			   pkt_len);
				dev->stats.rx_dropped++;
			}
		}
		/* Pop the top of the Rx FIFO */
		tc589_wait_for_completion(dev, RxDiscard);
	}
	if (worklimit == 0)
		netdev_warn(dev, "too much work in el3_rx!\n");
	return 0;
}

static void set_rx_mode(struct net_device *dev)
{
	unsigned int ioaddr = dev->base_addr;
	u16 opts = SetRxFilter | RxStation | RxBroadcast;

	if (dev->flags & IFF_PROMISC)
		opts |= RxMulticast | RxProm;
	else if (!netdev_mc_empty(dev) || (dev->flags & IFF_ALLMULTI))
		opts |= RxMulticast;
	outw(opts, ioaddr + EL3_CMD);
}

static void set_multicast_list(struct net_device *dev)
{
	struct el3_private *priv = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	set_rx_mode(dev);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int el3_close(struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	struct pcmcia_device *link = lp->p_dev;
	unsigned int ioaddr = dev->base_addr;

	dev_dbg(&link->dev, "%s: shutting down ethercard.\n", dev->name);

	if (pcmcia_dev_present(link)) {
		/* Turn off statistics ASAP.  We update dev->stats below. */
		outw(StatsDisable, ioaddr + EL3_CMD);

		/* Disable the receiver and transmitter. */
		outw(RxDisable, ioaddr + EL3_CMD);
		outw(TxDisable, ioaddr + EL3_CMD);

		if (dev->if_port == 2)
			/* Turn off thinnet power.  Green! */
			outw(StopCoax, ioaddr + EL3_CMD);
		else if (dev->if_port == 1) {
			/* Disable link beat and jabber */
			EL3WINDOW(4);
			outw(0, ioaddr + WN4_MEDIA);
		}

		/* Switching back to window 0 disables the IRQ. */
		EL3WINDOW(0);
		/* But we explicitly zero the IRQ line select anyway. */
		outw(0x0f00, ioaddr + WN0_IRQ);

		/* Check if the card still exists */
		if ((inw(ioaddr+EL3_STATUS) & 0xe000) == 0x2000)
			update_stats(dev);
	}

	link->open--;
	netif_stop_queue(dev);
	del_timer_sync(&lp->media);

	return 0;
}

static const struct pcmcia_device_id tc589_ids[] = {
	PCMCIA_MFC_DEVICE_MANF_CARD(0, 0x0101, 0x0562),
	PCMCIA_MFC_DEVICE_PROD_ID1(0, "Motorola MARQUIS", 0xf03e4e77),
	PCMCIA_DEVICE_MANF_CARD(0x0101, 0x0589),
	PCMCIA_DEVICE_PROD_ID12("Farallon", "ENet", 0x58d93fc4, 0x992c2202),
	PCMCIA_MFC_DEVICE_CIS_MANF_CARD(0, 0x0101, 0x0035, "cis/3CXEM556.cis"),
	PCMCIA_MFC_DEVICE_CIS_MANF_CARD(0, 0x0101, 0x003d, "cis/3CXEM556.cis"),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, tc589_ids);

static struct pcmcia_driver tc589_driver = {
	.owner		= THIS_MODULE,
	.name		= "3c589_cs",
	.probe		= tc589_probe,
	.remove		= tc589_detach,
	.id_table	= tc589_ids,
	.suspend	= tc589_suspend,
	.resume		= tc589_resume,
};
module_pcmcia_driver(tc589_driver);

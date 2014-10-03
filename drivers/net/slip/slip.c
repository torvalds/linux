/*
 * slip.c	This module implements the SLIP protocol for kernel-based
 *		devices like TTY.  It interfaces between a raw TTY, and the
 *		kernel's INET protocol layers.
 *
 * Version:	@(#)slip.c	0.8.3	12/24/94
 *
 * Authors:	Laurence Culhane, <loz@holmes.demon.co.uk>
 *		Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 * Fixes:
 *		Alan Cox	: 	Sanity checks and avoid tx overruns.
 *					Has a new sl->mtu field.
 *		Alan Cox	: 	Found cause of overrun. ifconfig sl0
 *					mtu upwards. Driver now spots this
 *					and grows/shrinks its buffers(hack!).
 *					Memory leak if you run out of memory
 *					setting up a slip driver fixed.
 *		Matt Dillon	:	Printable slip (borrowed from NET2E)
 *	Pauline Middelink	:	Slip driver fixes.
 *		Alan Cox	:	Honours the old SL_COMPRESSED flag
 *		Alan Cox	:	KISS AX.25 and AXUI IP support
 *		Michael Riepe	:	Automatic CSLIP recognition added
 *		Charles Hedrick :	CSLIP header length problem fix.
 *		Alan Cox	:	Corrected non-IP cases of the above.
 *		Alan Cox	:	Now uses hardware type as per FvK.
 *		Alan Cox	:	Default to 192.168.0.0 (RFC 1597)
 *		A.N.Kuznetsov	:	dev_tint() recursion fix.
 *	Dmitry Gorodchanin	:	SLIP memory leaks
 *      Dmitry Gorodchanin      :       Code cleanup. Reduce tty driver
 *                                      buffering from 4096 to 256 bytes.
 *                                      Improving SLIP response time.
 *                                      CONFIG_SLIP_MODE_SLIP6.
 *                                      ifconfig sl? up & down now works
 *					correctly.
 *					Modularization.
 *              Alan Cox        :       Oops - fix AX.25 buffer lengths
 *      Dmitry Gorodchanin      :       Even more cleanups. Preserve CSLIP
 *                                      statistics. Include CSLIP code only
 *                                      if it really needed.
 *		Alan Cox	:	Free slhc buffers in the right place.
 *		Alan Cox	:	Allow for digipeated IP over AX.25
 *		Matti Aarnio	:	Dynamic SLIP devices, with ideas taken
 *					from Jim Freeman's <jfree@caldera.com>
 *					dynamic PPP devices.  We do NOT kfree()
 *					device entries, just reg./unreg. them
 *					as they are needed.  We kfree() them
 *					at module cleanup.
 *					With MODULE-loading ``insmod'', user
 *					can issue parameter:  slip_maxdev=1024
 *					(Or how much he/she wants.. Default
 *					is 256)
 *	Stanislav Voronyi	:	Slip line checking, with ideas taken
 *					from multislip BSDI driver which was
 *					written by Igor Chechik, RELCOM Corp.
 *					Only algorithms have been ported to
 *					Linux SLIP driver.
 *	Vitaly E. Lavrov	:	Sane behaviour on tty hangup.
 *	Alexey Kuznetsov	:	Cleanup interfaces to tty & netdevice
 *					modules.
 */

#define SL_CHECK_TRANSMIT
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/if_slip.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "slip.h"
#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/slhc_vj.h>
#endif

#define SLIP_VERSION	"0.8.4-NET3.019-NEWTTY"

static struct net_device **slip_devs;

static int slip_maxdev = SL_NRUNIT;
module_param(slip_maxdev, int, 0);
MODULE_PARM_DESC(slip_maxdev, "Maximum number of slip devices");

static int slip_esc(unsigned char *p, unsigned char *d, int len);
static void slip_unesc(struct slip *sl, unsigned char c);
#ifdef CONFIG_SLIP_MODE_SLIP6
static int slip_esc6(unsigned char *p, unsigned char *d, int len);
static void slip_unesc6(struct slip *sl, unsigned char c);
#endif
#ifdef CONFIG_SLIP_SMART
static void sl_keepalive(unsigned long sls);
static void sl_outfill(unsigned long sls);
static int sl_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
#endif

/********************************
*  Buffer administration routines:
*	sl_alloc_bufs()
*	sl_free_bufs()
*	sl_realloc_bufs()
*
* NOTE: sl_realloc_bufs != sl_free_bufs + sl_alloc_bufs, because
*	sl_realloc_bufs provides strong atomicity and reallocation
*	on actively running device.
*********************************/

/*
   Allocate channel buffers.
 */

static int sl_alloc_bufs(struct slip *sl, int mtu)
{
	int err = -ENOBUFS;
	unsigned long len;
	char *rbuff = NULL;
	char *xbuff = NULL;
#ifdef SL_INCLUDE_CSLIP
	char *cbuff = NULL;
	struct slcompress *slcomp = NULL;
#endif

	/*
	 * Allocate the SLIP frame buffers:
	 *
	 * rbuff	Receive buffer.
	 * xbuff	Transmit buffer.
	 * cbuff        Temporary compression buffer.
	 */
	len = mtu * 2;

	/*
	 * allow for arrival of larger UDP packets, even if we say not to
	 * also fixes a bug in which SunOS sends 512-byte packets even with
	 * an MSS of 128
	 */
	if (len < 576 * 2)
		len = 576 * 2;
	rbuff = kmalloc(len + 4, GFP_KERNEL);
	if (rbuff == NULL)
		goto err_exit;
	xbuff = kmalloc(len + 4, GFP_KERNEL);
	if (xbuff == NULL)
		goto err_exit;
#ifdef SL_INCLUDE_CSLIP
	cbuff = kmalloc(len + 4, GFP_KERNEL);
	if (cbuff == NULL)
		goto err_exit;
	slcomp = slhc_init(16, 16);
	if (slcomp == NULL)
		goto err_exit;
#endif
	spin_lock_bh(&sl->lock);
	if (sl->tty == NULL) {
		spin_unlock_bh(&sl->lock);
		err = -ENODEV;
		goto err_exit;
	}
	sl->mtu	     = mtu;
	sl->buffsize = len;
	sl->rcount   = 0;
	sl->xleft    = 0;
	rbuff = xchg(&sl->rbuff, rbuff);
	xbuff = xchg(&sl->xbuff, xbuff);
#ifdef SL_INCLUDE_CSLIP
	cbuff = xchg(&sl->cbuff, cbuff);
	slcomp = xchg(&sl->slcomp, slcomp);
#endif
#ifdef CONFIG_SLIP_MODE_SLIP6
	sl->xdata    = 0;
	sl->xbits    = 0;
#endif
	spin_unlock_bh(&sl->lock);
	err = 0;

	/* Cleanup */
err_exit:
#ifdef SL_INCLUDE_CSLIP
	kfree(cbuff);
	slhc_free(slcomp);
#endif
	kfree(xbuff);
	kfree(rbuff);
	return err;
}

/* Free a SLIP channel buffers. */
static void sl_free_bufs(struct slip *sl)
{
	/* Free all SLIP frame buffers. */
	kfree(xchg(&sl->rbuff, NULL));
	kfree(xchg(&sl->xbuff, NULL));
#ifdef SL_INCLUDE_CSLIP
	kfree(xchg(&sl->cbuff, NULL));
	slhc_free(xchg(&sl->slcomp, NULL));
#endif
}

/*
   Reallocate slip channel buffers.
 */

static int sl_realloc_bufs(struct slip *sl, int mtu)
{
	int err = 0;
	struct net_device *dev = sl->dev;
	unsigned char *xbuff, *rbuff;
#ifdef SL_INCLUDE_CSLIP
	unsigned char *cbuff;
#endif
	int len = mtu * 2;

/*
 * allow for arrival of larger UDP packets, even if we say not to
 * also fixes a bug in which SunOS sends 512-byte packets even with
 * an MSS of 128
 */
	if (len < 576 * 2)
		len = 576 * 2;

	xbuff = kmalloc(len + 4, GFP_ATOMIC);
	rbuff = kmalloc(len + 4, GFP_ATOMIC);
#ifdef SL_INCLUDE_CSLIP
	cbuff = kmalloc(len + 4, GFP_ATOMIC);
#endif


#ifdef SL_INCLUDE_CSLIP
	if (xbuff == NULL || rbuff == NULL || cbuff == NULL)  {
#else
	if (xbuff == NULL || rbuff == NULL)  {
#endif
		if (mtu > sl->mtu) {
			printk(KERN_WARNING "%s: unable to grow slip buffers, MTU change cancelled.\n",
			       dev->name);
			err = -ENOBUFS;
		}
		goto done;
	}
	spin_lock_bh(&sl->lock);

	err = -ENODEV;
	if (sl->tty == NULL)
		goto done_on_bh;

	xbuff    = xchg(&sl->xbuff, xbuff);
	rbuff    = xchg(&sl->rbuff, rbuff);
#ifdef SL_INCLUDE_CSLIP
	cbuff    = xchg(&sl->cbuff, cbuff);
#endif
	if (sl->xleft)  {
		if (sl->xleft <= len)  {
			memcpy(sl->xbuff, sl->xhead, sl->xleft);
		} else  {
			sl->xleft = 0;
			dev->stats.tx_dropped++;
		}
	}
	sl->xhead = sl->xbuff;

	if (sl->rcount)  {
		if (sl->rcount <= len) {
			memcpy(sl->rbuff, rbuff, sl->rcount);
		} else  {
			sl->rcount = 0;
			dev->stats.rx_over_errors++;
			set_bit(SLF_ERROR, &sl->flags);
		}
	}
	sl->mtu      = mtu;
	dev->mtu      = mtu;
	sl->buffsize = len;
	err = 0;

done_on_bh:
	spin_unlock_bh(&sl->lock);

done:
	kfree(xbuff);
	kfree(rbuff);
#ifdef SL_INCLUDE_CSLIP
	kfree(cbuff);
#endif
	return err;
}


/* Set the "sending" flag.  This must be atomic hence the set_bit. */
static inline void sl_lock(struct slip *sl)
{
	netif_stop_queue(sl->dev);
}


/* Clear the "sending" flag.  This must be atomic, hence the ASM. */
static inline void sl_unlock(struct slip *sl)
{
	netif_wake_queue(sl->dev);
}

/* Send one completely decapsulated IP datagram to the IP layer. */
static void sl_bump(struct slip *sl)
{
	struct net_device *dev = sl->dev;
	struct sk_buff *skb;
	int count;

	count = sl->rcount;
#ifdef SL_INCLUDE_CSLIP
	if (sl->mode & (SL_MODE_ADAPTIVE | SL_MODE_CSLIP)) {
		unsigned char c = sl->rbuff[0];
		if (c & SL_TYPE_COMPRESSED_TCP) {
			/* ignore compressed packets when CSLIP is off */
			if (!(sl->mode & SL_MODE_CSLIP)) {
				printk(KERN_WARNING "%s: compressed packet ignored\n", dev->name);
				return;
			}
			/* make sure we've reserved enough space for uncompress
			   to use */
			if (count + 80 > sl->buffsize) {
				dev->stats.rx_over_errors++;
				return;
			}
			count = slhc_uncompress(sl->slcomp, sl->rbuff, count);
			if (count <= 0)
				return;
		} else if (c >= SL_TYPE_UNCOMPRESSED_TCP) {
			if (!(sl->mode & SL_MODE_CSLIP)) {
				/* turn on header compression */
				sl->mode |= SL_MODE_CSLIP;
				sl->mode &= ~SL_MODE_ADAPTIVE;
				printk(KERN_INFO "%s: header compression turned on\n", dev->name);
			}
			sl->rbuff[0] &= 0x4f;
			if (slhc_remember(sl->slcomp, sl->rbuff, count) <= 0)
				return;
		}
	}
#endif  /* SL_INCLUDE_CSLIP */

	dev->stats.rx_bytes += count;

	skb = dev_alloc_skb(count);
	if (skb == NULL) {
		printk(KERN_WARNING "%s: memory squeeze, dropping packet.\n", dev->name);
		dev->stats.rx_dropped++;
		return;
	}
	skb->dev = dev;
	memcpy(skb_put(skb, count), sl->rbuff, count);
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IP);
	netif_rx_ni(skb);
	dev->stats.rx_packets++;
}

/* Encapsulate one IP datagram and stuff into a TTY queue. */
static void sl_encaps(struct slip *sl, unsigned char *icp, int len)
{
	unsigned char *p;
	int actual, count;

	if (len > sl->mtu) {		/* Sigh, shouldn't occur BUT ... */
		printk(KERN_WARNING "%s: truncating oversized transmit packet!\n", sl->dev->name);
		sl->dev->stats.tx_dropped++;
		sl_unlock(sl);
		return;
	}

	p = icp;
#ifdef SL_INCLUDE_CSLIP
	if (sl->mode & SL_MODE_CSLIP)
		len = slhc_compress(sl->slcomp, p, len, sl->cbuff, &p, 1);
#endif
#ifdef CONFIG_SLIP_MODE_SLIP6
	if (sl->mode & SL_MODE_SLIP6)
		count = slip_esc6(p, sl->xbuff, len);
	else
#endif
		count = slip_esc(p, sl->xbuff, len);

	/* Order of next two lines is *very* important.
	 * When we are sending a little amount of data,
	 * the transfer may be completed inside the ops->write()
	 * routine, because it's running with interrupts enabled.
	 * In this case we *never* got WRITE_WAKEUP event,
	 * if we did not request it before write operation.
	 *       14 Oct 1994  Dmitry Gorodchanin.
	 */
	set_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	actual = sl->tty->ops->write(sl->tty, sl->xbuff, count);
#ifdef SL_CHECK_TRANSMIT
	sl->dev->trans_start = jiffies;
#endif
	sl->xleft = count - actual;
	sl->xhead = sl->xbuff + actual;
#ifdef CONFIG_SLIP_SMART
	/* VSV */
	clear_bit(SLF_OUTWAIT, &sl->flags);	/* reset outfill flag */
#endif
}

/* Write out any remaining transmit buffer. Scheduled when tty is writable */
static void slip_transmit(struct work_struct *work)
{
	struct slip *sl = container_of(work, struct slip, tx_work);
	int actual;

	spin_lock_bh(&sl->lock);
	/* First make sure we're connected. */
	if (!sl->tty || sl->magic != SLIP_MAGIC || !netif_running(sl->dev)) {
		spin_unlock_bh(&sl->lock);
		return;
	}

	if (sl->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet */
		sl->dev->stats.tx_packets++;
		clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
		spin_unlock_bh(&sl->lock);
		sl_unlock(sl);
		return;
	}

	actual = sl->tty->ops->write(sl->tty, sl->xhead, sl->xleft);
	sl->xleft -= actual;
	sl->xhead += actual;
	spin_unlock_bh(&sl->lock);
}

/*
 * Called by the driver when there's room for more data.
 * Schedule the transmit.
 */
static void slip_write_wakeup(struct tty_struct *tty)
{
	struct slip *sl = tty->disc_data;

	schedule_work(&sl->tx_work);
}

static void sl_tx_timeout(struct net_device *dev)
{
	struct slip *sl = netdev_priv(dev);

	spin_lock(&sl->lock);

	if (netif_queue_stopped(dev)) {
		if (!netif_running(dev))
			goto out;

		/* May be we must check transmitter timeout here ?
		 *      14 Oct 1994 Dmitry Gorodchanin.
		 */
#ifdef SL_CHECK_TRANSMIT
		if (time_before(jiffies, dev_trans_start(dev) + 20 * HZ))  {
			/* 20 sec timeout not reached */
			goto out;
		}
		printk(KERN_WARNING "%s: transmit timed out, %s?\n",
			dev->name,
			(tty_chars_in_buffer(sl->tty) || sl->xleft) ?
				"bad line quality" : "driver error");
		sl->xleft = 0;
		clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
		sl_unlock(sl);
#endif
	}
out:
	spin_unlock(&sl->lock);
}


/* Encapsulate an IP datagram and kick it into a TTY queue. */
static netdev_tx_t
sl_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct slip *sl = netdev_priv(dev);

	spin_lock(&sl->lock);
	if (!netif_running(dev)) {
		spin_unlock(&sl->lock);
		printk(KERN_WARNING "%s: xmit call when iface is down\n", dev->name);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	if (sl->tty == NULL) {
		spin_unlock(&sl->lock);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	sl_lock(sl);
	dev->stats.tx_bytes += skb->len;
	sl_encaps(sl, skb->data, skb->len);
	spin_unlock(&sl->lock);

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}


/******************************************
 *   Routines looking at netdevice side.
 ******************************************/

/* Netdevice UP -> DOWN routine */

static int
sl_close(struct net_device *dev)
{
	struct slip *sl = netdev_priv(dev);

	spin_lock_bh(&sl->lock);
	if (sl->tty)
		/* TTY discipline is running. */
		clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	netif_stop_queue(dev);
	sl->rcount   = 0;
	sl->xleft    = 0;
	spin_unlock_bh(&sl->lock);

	return 0;
}

/* Netdevice DOWN -> UP routine */

static int sl_open(struct net_device *dev)
{
	struct slip *sl = netdev_priv(dev);

	if (sl->tty == NULL)
		return -ENODEV;

	sl->flags &= (1 << SLF_INUSE);
	netif_start_queue(dev);
	return 0;
}

/* Netdevice change MTU request */

static int sl_change_mtu(struct net_device *dev, int new_mtu)
{
	struct slip *sl = netdev_priv(dev);

	if (new_mtu < 68 || new_mtu > 65534)
		return -EINVAL;

	if (new_mtu != dev->mtu)
		return sl_realloc_bufs(sl, new_mtu);
	return 0;
}

/* Netdevice get statistics request */

static struct rtnl_link_stats64 *
sl_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct net_device_stats *devstats = &dev->stats;
#ifdef SL_INCLUDE_CSLIP
	struct slip *sl = netdev_priv(dev);
	struct slcompress *comp = sl->slcomp;
#endif
	stats->rx_packets     = devstats->rx_packets;
	stats->tx_packets     = devstats->tx_packets;
	stats->rx_bytes       = devstats->rx_bytes;
	stats->tx_bytes       = devstats->tx_bytes;
	stats->rx_dropped     = devstats->rx_dropped;
	stats->tx_dropped     = devstats->tx_dropped;
	stats->tx_errors      = devstats->tx_errors;
	stats->rx_errors      = devstats->rx_errors;
	stats->rx_over_errors = devstats->rx_over_errors;

#ifdef SL_INCLUDE_CSLIP
	if (comp) {
		/* Generic compressed statistics */
		stats->rx_compressed   = comp->sls_i_compressed;
		stats->tx_compressed   = comp->sls_o_compressed;

		/* Are we really still needs this? */
		stats->rx_fifo_errors += comp->sls_i_compressed;
		stats->rx_dropped     += comp->sls_i_tossed;
		stats->tx_fifo_errors += comp->sls_o_compressed;
		stats->collisions     += comp->sls_o_misses;
	}
#endif
	return stats;
}

/* Netdevice register callback */

static int sl_init(struct net_device *dev)
{
	struct slip *sl = netdev_priv(dev);

	/*
	 *	Finish setting up the DEVICE info.
	 */

	dev->mtu		= sl->mtu;
	dev->type		= ARPHRD_SLIP + sl->mode;
#ifdef SL_CHECK_TRANSMIT
	dev->watchdog_timeo	= 20*HZ;
#endif
	return 0;
}


static void sl_uninit(struct net_device *dev)
{
	struct slip *sl = netdev_priv(dev);

	sl_free_bufs(sl);
}

/* Hook the destructor so we can free slip devices at the right point in time */
static void sl_free_netdev(struct net_device *dev)
{
	int i = dev->base_addr;
	free_netdev(dev);
	slip_devs[i] = NULL;
}

static const struct net_device_ops sl_netdev_ops = {
	.ndo_init		= sl_init,
	.ndo_uninit	  	= sl_uninit,
	.ndo_open		= sl_open,
	.ndo_stop		= sl_close,
	.ndo_start_xmit		= sl_xmit,
	.ndo_get_stats64        = sl_get_stats64,
	.ndo_change_mtu		= sl_change_mtu,
	.ndo_tx_timeout		= sl_tx_timeout,
#ifdef CONFIG_SLIP_SMART
	.ndo_do_ioctl		= sl_ioctl,
#endif
};


static void sl_setup(struct net_device *dev)
{
	dev->netdev_ops		= &sl_netdev_ops;
	dev->destructor		= sl_free_netdev;

	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->tx_queue_len	= 10;

	/* New-style flags. */
	dev->flags		= IFF_NOARP|IFF_POINTOPOINT|IFF_MULTICAST;
}

/******************************************
  Routines looking at TTY side.
 ******************************************/


/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing. This will not
 * be re-entered while running but other ldisc functions may be called
 * in parallel
 */

static void slip_receive_buf(struct tty_struct *tty, const unsigned char *cp,
							char *fp, int count)
{
	struct slip *sl = tty->disc_data;

	if (!sl || sl->magic != SLIP_MAGIC || !netif_running(sl->dev))
		return;

	/* Read the characters out of the buffer */
	while (count--) {
		if (fp && *fp++) {
			if (!test_and_set_bit(SLF_ERROR, &sl->flags))
				sl->dev->stats.rx_errors++;
			cp++;
			continue;
		}
#ifdef CONFIG_SLIP_MODE_SLIP6
		if (sl->mode & SL_MODE_SLIP6)
			slip_unesc6(sl, *cp++);
		else
#endif
			slip_unesc(sl, *cp++);
	}
}

/************************************
 *  slip_open helper routines.
 ************************************/

/* Collect hanged up channels */
static void sl_sync(void)
{
	int i;
	struct net_device *dev;
	struct slip	  *sl;

	for (i = 0; i < slip_maxdev; i++) {
		dev = slip_devs[i];
		if (dev == NULL)
			break;

		sl = netdev_priv(dev);
		if (sl->tty || sl->leased)
			continue;
		if (dev->flags & IFF_UP)
			dev_close(dev);
	}
}


/* Find a free SLIP channel, and link in this `tty' line. */
static struct slip *sl_alloc(dev_t line)
{
	int i;
	char name[IFNAMSIZ];
	struct net_device *dev = NULL;
	struct slip       *sl;

	for (i = 0; i < slip_maxdev; i++) {
		dev = slip_devs[i];
		if (dev == NULL)
			break;
	}
	/* Sorry, too many, all slots in use */
	if (i >= slip_maxdev)
		return NULL;

	sprintf(name, "sl%d", i);
	dev = alloc_netdev(sizeof(*sl), name, NET_NAME_UNKNOWN, sl_setup);
	if (!dev)
		return NULL;

	dev->base_addr  = i;
	sl = netdev_priv(dev);

	/* Initialize channel control data */
	sl->magic       = SLIP_MAGIC;
	sl->dev	      	= dev;
	spin_lock_init(&sl->lock);
	INIT_WORK(&sl->tx_work, slip_transmit);
	sl->mode        = SL_MODE_DEFAULT;
#ifdef CONFIG_SLIP_SMART
	/* initialize timer_list struct */
	init_timer(&sl->keepalive_timer);
	sl->keepalive_timer.data = (unsigned long)sl;
	sl->keepalive_timer.function = sl_keepalive;
	init_timer(&sl->outfill_timer);
	sl->outfill_timer.data = (unsigned long)sl;
	sl->outfill_timer.function = sl_outfill;
#endif
	slip_devs[i] = dev;
	return sl;
}

/*
 * Open the high-level part of the SLIP channel.
 * This function is called by the TTY module when the
 * SLIP line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free SLIP channel...
 *
 * Called in process context serialized from other ldisc calls.
 */

static int slip_open(struct tty_struct *tty)
{
	struct slip *sl;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	/* RTnetlink lock is misused here to serialize concurrent
	   opens of slip channels. There are better ways, but it is
	   the simplest one.
	 */
	rtnl_lock();

	/* Collect hanged up channels. */
	sl_sync();

	sl = tty->disc_data;

	err = -EEXIST;
	/* First make sure we're not already connected. */
	if (sl && sl->magic == SLIP_MAGIC)
		goto err_exit;

	/* OK.  Find a free SLIP channel to use. */
	err = -ENFILE;
	sl = sl_alloc(tty_devnum(tty));
	if (sl == NULL)
		goto err_exit;

	sl->tty = tty;
	tty->disc_data = sl;
	sl->pid = current->pid;

	if (!test_bit(SLF_INUSE, &sl->flags)) {
		/* Perform the low-level SLIP initialization. */
		err = sl_alloc_bufs(sl, SL_MTU);
		if (err)
			goto err_free_chan;

		set_bit(SLF_INUSE, &sl->flags);

		err = register_netdevice(sl->dev);
		if (err)
			goto err_free_bufs;
	}

#ifdef CONFIG_SLIP_SMART
	if (sl->keepalive) {
		sl->keepalive_timer.expires = jiffies + sl->keepalive * HZ;
		add_timer(&sl->keepalive_timer);
	}
	if (sl->outfill) {
		sl->outfill_timer.expires = jiffies + sl->outfill * HZ;
		add_timer(&sl->outfill_timer);
	}
#endif

	/* Done.  We have linked the TTY line to a channel. */
	rtnl_unlock();
	tty->receive_room = 65536;	/* We don't flow control */

	/* TTY layer expects 0 on success */
	return 0;

err_free_bufs:
	sl_free_bufs(sl);

err_free_chan:
	sl->tty = NULL;
	tty->disc_data = NULL;
	clear_bit(SLF_INUSE, &sl->flags);

err_exit:
	rtnl_unlock();

	/* Count references from TTY module */
	return err;
}

/*
 * Close down a SLIP channel.
 * This means flushing out any pending queues, and then returning. This
 * call is serialized against other ldisc functions.
 *
 * We also use this method fo a hangup event
 */

static void slip_close(struct tty_struct *tty)
{
	struct slip *sl = tty->disc_data;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLIP_MAGIC || sl->tty != tty)
		return;

	spin_lock_bh(&sl->lock);
	tty->disc_data = NULL;
	sl->tty = NULL;
	spin_unlock_bh(&sl->lock);

	flush_work(&sl->tx_work);

	/* VSV = very important to remove timers */
#ifdef CONFIG_SLIP_SMART
	del_timer_sync(&sl->keepalive_timer);
	del_timer_sync(&sl->outfill_timer);
#endif
	/* Flush network side */
	unregister_netdev(sl->dev);
	/* This will complete via sl_free_netdev */
}

static int slip_hangup(struct tty_struct *tty)
{
	slip_close(tty);
	return 0;
}
 /************************************************************************
  *			STANDARD SLIP ENCAPSULATION		  	 *
  ************************************************************************/

static int slip_esc(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = END;

	/*
	 * For each byte in the packet, send the appropriate
	 * character sequence, according to the SLIP protocol.
	 */

	while (len-- > 0) {
		switch (c = *s++) {
		case END:
			*ptr++ = ESC;
			*ptr++ = ESC_END;
			break;
		case ESC:
			*ptr++ = ESC;
			*ptr++ = ESC_ESC;
			break;
		default:
			*ptr++ = c;
			break;
		}
	}
	*ptr++ = END;
	return ptr - d;
}

static void slip_unesc(struct slip *sl, unsigned char s)
{

	switch (s) {
	case END:
#ifdef CONFIG_SLIP_SMART
		/* drop keeptest bit = VSV */
		if (test_bit(SLF_KEEPTEST, &sl->flags))
			clear_bit(SLF_KEEPTEST, &sl->flags);
#endif

		if (!test_and_clear_bit(SLF_ERROR, &sl->flags) &&
		    (sl->rcount > 2))
			sl_bump(sl);
		clear_bit(SLF_ESCAPE, &sl->flags);
		sl->rcount = 0;
		return;

	case ESC:
		set_bit(SLF_ESCAPE, &sl->flags);
		return;
	case ESC_ESC:
		if (test_and_clear_bit(SLF_ESCAPE, &sl->flags))
			s = ESC;
		break;
	case ESC_END:
		if (test_and_clear_bit(SLF_ESCAPE, &sl->flags))
			s = END;
		break;
	}
	if (!test_bit(SLF_ERROR, &sl->flags))  {
		if (sl->rcount < sl->buffsize)  {
			sl->rbuff[sl->rcount++] = s;
			return;
		}
		sl->dev->stats.rx_over_errors++;
		set_bit(SLF_ERROR, &sl->flags);
	}
}


#ifdef CONFIG_SLIP_MODE_SLIP6
/************************************************************************
 *			 6 BIT SLIP ENCAPSULATION			*
 ************************************************************************/

static int slip_esc6(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;
	int i;
	unsigned short v = 0;
	short bits = 0;

	/*
	 * Send an initial END character to flush out any
	 * data that may have accumulated in the receiver
	 * due to line noise.
	 */

	*ptr++ = 0x70;

	/*
	 * Encode the packet into printable ascii characters
	 */

	for (i = 0; i < len; ++i) {
		v = (v << 8) | s[i];
		bits += 8;
		while (bits >= 6) {
			bits -= 6;
			c = 0x30 + ((v >> bits) & 0x3F);
			*ptr++ = c;
		}
	}
	if (bits) {
		c = 0x30 + ((v << (6 - bits)) & 0x3F);
		*ptr++ = c;
	}
	*ptr++ = 0x70;
	return ptr - d;
}

static void slip_unesc6(struct slip *sl, unsigned char s)
{
	unsigned char c;

	if (s == 0x70) {
#ifdef CONFIG_SLIP_SMART
		/* drop keeptest bit = VSV */
		if (test_bit(SLF_KEEPTEST, &sl->flags))
			clear_bit(SLF_KEEPTEST, &sl->flags);
#endif

		if (!test_and_clear_bit(SLF_ERROR, &sl->flags) &&
		    (sl->rcount > 2))
			sl_bump(sl);
		sl->rcount = 0;
		sl->xbits = 0;
		sl->xdata = 0;
	} else if (s >= 0x30 && s < 0x70) {
		sl->xdata = (sl->xdata << 6) | ((s - 0x30) & 0x3F);
		sl->xbits += 6;
		if (sl->xbits >= 8) {
			sl->xbits -= 8;
			c = (unsigned char)(sl->xdata >> sl->xbits);
			if (!test_bit(SLF_ERROR, &sl->flags))  {
				if (sl->rcount < sl->buffsize)  {
					sl->rbuff[sl->rcount++] = c;
					return;
				}
				sl->dev->stats.rx_over_errors++;
				set_bit(SLF_ERROR, &sl->flags);
			}
		}
	}
}
#endif /* CONFIG_SLIP_MODE_SLIP6 */

/* Perform I/O control on an active SLIP channel. */
static int slip_ioctl(struct tty_struct *tty, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct slip *sl = tty->disc_data;
	unsigned int tmp;
	int __user *p = (int __user *)arg;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLIP_MAGIC)
		return -EINVAL;

	switch (cmd) {
	case SIOCGIFNAME:
		tmp = strlen(sl->dev->name) + 1;
		if (copy_to_user((void __user *)arg, sl->dev->name, tmp))
			return -EFAULT;
		return 0;

	case SIOCGIFENCAP:
		if (put_user(sl->mode, p))
			return -EFAULT;
		return 0;

	case SIOCSIFENCAP:
		if (get_user(tmp, p))
			return -EFAULT;
#ifndef SL_INCLUDE_CSLIP
		if (tmp & (SL_MODE_CSLIP|SL_MODE_ADAPTIVE))
			return -EINVAL;
#else
		if ((tmp & (SL_MODE_ADAPTIVE | SL_MODE_CSLIP)) ==
		    (SL_MODE_ADAPTIVE | SL_MODE_CSLIP))
			/* return -EINVAL; */
			tmp &= ~SL_MODE_ADAPTIVE;
#endif
#ifndef CONFIG_SLIP_MODE_SLIP6
		if (tmp & SL_MODE_SLIP6)
			return -EINVAL;
#endif
		sl->mode = tmp;
		sl->dev->type = ARPHRD_SLIP + sl->mode;
		return 0;

	case SIOCSIFHWADDR:
		return -EINVAL;

#ifdef CONFIG_SLIP_SMART
	/* VSV changes start here */
	case SIOCSKEEPALIVE:
		if (get_user(tmp, p))
			return -EFAULT;
		if (tmp > 255) /* max for unchar */
			return -EINVAL;

		spin_lock_bh(&sl->lock);
		if (!sl->tty) {
			spin_unlock_bh(&sl->lock);
			return -ENODEV;
		}
		sl->keepalive = (u8)tmp;
		if (sl->keepalive != 0) {
			mod_timer(&sl->keepalive_timer,
					jiffies + sl->keepalive * HZ);
			set_bit(SLF_KEEPTEST, &sl->flags);
		} else
			del_timer(&sl->keepalive_timer);
		spin_unlock_bh(&sl->lock);
		return 0;

	case SIOCGKEEPALIVE:
		if (put_user(sl->keepalive, p))
			return -EFAULT;
		return 0;

	case SIOCSOUTFILL:
		if (get_user(tmp, p))
			return -EFAULT;
		if (tmp > 255) /* max for unchar */
			return -EINVAL;
		spin_lock_bh(&sl->lock);
		if (!sl->tty) {
			spin_unlock_bh(&sl->lock);
			return -ENODEV;
		}
		sl->outfill = (u8)tmp;
		if (sl->outfill != 0) {
			mod_timer(&sl->outfill_timer,
						jiffies + sl->outfill * HZ);
			set_bit(SLF_OUTWAIT, &sl->flags);
		} else
			del_timer(&sl->outfill_timer);
		spin_unlock_bh(&sl->lock);
		return 0;

	case SIOCGOUTFILL:
		if (put_user(sl->outfill, p))
			return -EFAULT;
		return 0;
	/* VSV changes end */
#endif
	default:
		return tty_mode_ioctl(tty, file, cmd, arg);
	}
}

#ifdef CONFIG_COMPAT
static long slip_compat_ioctl(struct tty_struct *tty, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SIOCGIFNAME:
	case SIOCGIFENCAP:
	case SIOCSIFENCAP:
	case SIOCSIFHWADDR:
	case SIOCSKEEPALIVE:
	case SIOCGKEEPALIVE:
	case SIOCSOUTFILL:
	case SIOCGOUTFILL:
		return slip_ioctl(tty, file, cmd,
				  (unsigned long)compat_ptr(arg));
	}

	return -ENOIOCTLCMD;
}
#endif

/* VSV changes start here */
#ifdef CONFIG_SLIP_SMART
/* function do_ioctl called from net/core/dev.c
   to allow get/set outfill/keepalive parameter
   by ifconfig                                 */

static int sl_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct slip *sl = netdev_priv(dev);
	unsigned long *p = (unsigned long *)&rq->ifr_ifru;

	if (sl == NULL)		/* Allocation failed ?? */
		return -ENODEV;

	spin_lock_bh(&sl->lock);

	if (!sl->tty) {
		spin_unlock_bh(&sl->lock);
		return -ENODEV;
	}

	switch (cmd) {
	case SIOCSKEEPALIVE:
		/* max for unchar */
		if ((unsigned)*p > 255) {
			spin_unlock_bh(&sl->lock);
			return -EINVAL;
		}
		sl->keepalive = (u8)*p;
		if (sl->keepalive != 0) {
			sl->keepalive_timer.expires =
						jiffies + sl->keepalive * HZ;
			mod_timer(&sl->keepalive_timer,
						jiffies + sl->keepalive * HZ);
			set_bit(SLF_KEEPTEST, &sl->flags);
		} else
			del_timer(&sl->keepalive_timer);
		break;

	case SIOCGKEEPALIVE:
		*p = sl->keepalive;
		break;

	case SIOCSOUTFILL:
		if ((unsigned)*p > 255) { /* max for unchar */
			spin_unlock_bh(&sl->lock);
			return -EINVAL;
		}
		sl->outfill = (u8)*p;
		if (sl->outfill != 0) {
			mod_timer(&sl->outfill_timer,
						jiffies + sl->outfill * HZ);
			set_bit(SLF_OUTWAIT, &sl->flags);
		} else
			del_timer(&sl->outfill_timer);
		break;

	case SIOCGOUTFILL:
		*p = sl->outfill;
		break;

	case SIOCSLEASE:
		/* Resolve race condition, when ioctl'ing hanged up
		   and opened by another process device.
		 */
		if (sl->tty != current->signal->tty &&
						sl->pid != current->pid) {
			spin_unlock_bh(&sl->lock);
			return -EPERM;
		}
		sl->leased = 0;
		if (*p)
			sl->leased = 1;
		break;

	case SIOCGLEASE:
		*p = sl->leased;
	}
	spin_unlock_bh(&sl->lock);
	return 0;
}
#endif
/* VSV changes end */

static struct tty_ldisc_ops sl_ldisc = {
	.owner 		= THIS_MODULE,
	.magic 		= TTY_LDISC_MAGIC,
	.name 		= "slip",
	.open 		= slip_open,
	.close	 	= slip_close,
	.hangup	 	= slip_hangup,
	.ioctl		= slip_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= slip_compat_ioctl,
#endif
	.receive_buf	= slip_receive_buf,
	.write_wakeup	= slip_write_wakeup,
};

static int __init slip_init(void)
{
	int status;

	if (slip_maxdev < 4)
		slip_maxdev = 4; /* Sanity */

	printk(KERN_INFO "SLIP: version %s (dynamic channels, max=%d)"
#ifdef CONFIG_SLIP_MODE_SLIP6
	       " (6 bit encapsulation enabled)"
#endif
	       ".\n",
	       SLIP_VERSION, slip_maxdev);
#if defined(SL_INCLUDE_CSLIP)
	printk(KERN_INFO "CSLIP: code copyright 1989 Regents of the University of California.\n");
#endif
#ifdef CONFIG_SLIP_SMART
	printk(KERN_INFO "SLIP linefill/keepalive option.\n");
#endif

	slip_devs = kzalloc(sizeof(struct net_device *)*slip_maxdev,
								GFP_KERNEL);
	if (!slip_devs)
		return -ENOMEM;

	/* Fill in our line protocol discipline, and register it */
	status = tty_register_ldisc(N_SLIP, &sl_ldisc);
	if (status != 0) {
		printk(KERN_ERR "SLIP: can't register line discipline (err = %d)\n", status);
		kfree(slip_devs);
	}
	return status;
}

static void __exit slip_exit(void)
{
	int i;
	struct net_device *dev;
	struct slip *sl;
	unsigned long timeout = jiffies + HZ;
	int busy = 0;

	if (slip_devs == NULL)
		return;

	/* First of all: check for active disciplines and hangup them.
	 */
	do {
		if (busy)
			msleep_interruptible(100);

		busy = 0;
		for (i = 0; i < slip_maxdev; i++) {
			dev = slip_devs[i];
			if (!dev)
				continue;
			sl = netdev_priv(dev);
			spin_lock_bh(&sl->lock);
			if (sl->tty) {
				busy++;
				tty_hangup(sl->tty);
			}
			spin_unlock_bh(&sl->lock);
		}
	} while (busy && time_before(jiffies, timeout));

	/* FIXME: hangup is async so we should wait when doing this second
	   phase */

	for (i = 0; i < slip_maxdev; i++) {
		dev = slip_devs[i];
		if (!dev)
			continue;
		slip_devs[i] = NULL;

		sl = netdev_priv(dev);
		if (sl->tty) {
			printk(KERN_ERR "%s: tty discipline still running\n",
			       dev->name);
			/* Intentionally leak the control block. */
			dev->destructor = NULL;
		}

		unregister_netdev(dev);
	}

	kfree(slip_devs);
	slip_devs = NULL;

	i = tty_unregister_ldisc(N_SLIP);
	if (i != 0)
		printk(KERN_ERR "SLIP: can't unregister line discipline (err = %d)\n", i);
}

module_init(slip_init);
module_exit(slip_exit);

#ifdef CONFIG_SLIP_SMART
/*
 * This is start of the code for multislip style line checking
 * added by Stanislav Voronyi. All changes before marked VSV
 */

static void sl_outfill(unsigned long sls)
{
	struct slip *sl = (struct slip *)sls;

	spin_lock(&sl->lock);

	if (sl->tty == NULL)
		goto out;

	if (sl->outfill) {
		if (test_bit(SLF_OUTWAIT, &sl->flags)) {
			/* no packets were transmitted, do outfill */
#ifdef CONFIG_SLIP_MODE_SLIP6
			unsigned char s = (sl->mode & SL_MODE_SLIP6)?0x70:END;
#else
			unsigned char s = END;
#endif
			/* put END into tty queue. Is it right ??? */
			if (!netif_queue_stopped(sl->dev)) {
				/* if device busy no outfill */
				sl->tty->ops->write(sl->tty, &s, 1);
			}
		} else
			set_bit(SLF_OUTWAIT, &sl->flags);

		mod_timer(&sl->outfill_timer, jiffies+sl->outfill*HZ);
	}
out:
	spin_unlock(&sl->lock);
}

static void sl_keepalive(unsigned long sls)
{
	struct slip *sl = (struct slip *)sls;

	spin_lock(&sl->lock);

	if (sl->tty == NULL)
		goto out;

	if (sl->keepalive) {
		if (test_bit(SLF_KEEPTEST, &sl->flags)) {
			/* keepalive still high :(, we must hangup */
			if (sl->outfill)
				/* outfill timer must be deleted too */
				(void)del_timer(&sl->outfill_timer);
			printk(KERN_DEBUG "%s: no packets received during keepalive timeout, hangup.\n", sl->dev->name);
			/* this must hangup tty & close slip */
			tty_hangup(sl->tty);
			/* I think we need not something else */
			goto out;
		} else
			set_bit(SLF_KEEPTEST, &sl->flags);

		mod_timer(&sl->keepalive_timer, jiffies+sl->keepalive*HZ);
	}
out:
	spin_unlock(&sl->lock);
}

#endif
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_SLIP);

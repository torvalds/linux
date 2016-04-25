/*
 * 6pack.c	This module implements the 6pack protocol for kernel-based
 *		devices like TTY. It interfaces between a raw TTY and the
 *		kernel's AX.25 protocol layers.
 *
 * Authors:	Andreas Könsgen <ajk@comnets.uni-bremen.de>
 *              Ralf Baechle DL5RB <ralf@linux-mips.org>
 *
 * Quite a lot of stuff "stolen" by Joerg Reuter from slip.c, written by
 *
 *		Laurence Culhane, <loz@holmes.demon.co.uk>
 *		Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/semaphore.h>
#include <linux/compat.h>
#include <linux/atomic.h>

#define SIXPACK_VERSION    "Revision: 0.3.0"

/* sixpack priority commands */
#define SIXP_SEOF		0x40	/* start and end of a 6pack frame */
#define SIXP_TX_URUN		0x48	/* transmit overrun */
#define SIXP_RX_ORUN		0x50	/* receive overrun */
#define SIXP_RX_BUF_OVL		0x58	/* receive buffer overflow */

#define SIXP_CHKSUM		0xFF	/* valid checksum of a 6pack frame */

/* masks to get certain bits out of the status bytes sent by the TNC */

#define SIXP_CMD_MASK		0xC0
#define SIXP_CHN_MASK		0x07
#define SIXP_PRIO_CMD_MASK	0x80
#define SIXP_STD_CMD_MASK	0x40
#define SIXP_PRIO_DATA_MASK	0x38
#define SIXP_TX_MASK		0x20
#define SIXP_RX_MASK		0x10
#define SIXP_RX_DCD_MASK	0x18
#define SIXP_LEDS_ON		0x78
#define SIXP_LEDS_OFF		0x60
#define SIXP_CON		0x08
#define SIXP_STA		0x10

#define SIXP_FOUND_TNC		0xe9
#define SIXP_CON_ON		0x68
#define SIXP_DCD_MASK		0x08
#define SIXP_DAMA_OFF		0

/* default level 2 parameters */
#define SIXP_TXDELAY			(HZ/4)	/* in 1 s */
#define SIXP_PERSIST			50	/* in 256ths */
#define SIXP_SLOTTIME			(HZ/10)	/* in 1 s */
#define SIXP_INIT_RESYNC_TIMEOUT	(3*HZ/2) /* in 1 s */
#define SIXP_RESYNC_TIMEOUT		5*HZ	/* in 1 s */

/* 6pack configuration. */
#define SIXP_NRUNIT			31      /* MAX number of 6pack channels */
#define SIXP_MTU			256	/* Default MTU */

enum sixpack_flags {
	SIXPF_ERROR,	/* Parity, etc. error	*/
};

struct sixpack {
	/* Various fields. */
	struct tty_struct	*tty;		/* ptr to TTY structure	*/
	struct net_device	*dev;		/* easy for intr handling  */

	/* These are pointers to the malloc()ed frame buffers. */
	unsigned char		*rbuff;		/* receiver buffer	*/
	int			rcount;         /* received chars counter  */
	unsigned char		*xbuff;		/* transmitter buffer	*/
	unsigned char		*xhead;         /* next byte to XMIT */
	int			xleft;          /* bytes left in XMIT queue  */

	unsigned char		raw_buf[4];
	unsigned char		cooked_buf[400];

	unsigned int		rx_count;
	unsigned int		rx_count_cooked;

	int			mtu;		/* Our mtu (to spot changes!) */
	int			buffsize;       /* Max buffers sizes */

	unsigned long		flags;		/* Flag values/ mode etc */
	unsigned char		mode;		/* 6pack mode */

	/* 6pack stuff */
	unsigned char		tx_delay;
	unsigned char		persistence;
	unsigned char		slottime;
	unsigned char		duplex;
	unsigned char		led_state;
	unsigned char		status;
	unsigned char		status1;
	unsigned char		status2;
	unsigned char		tx_enable;
	unsigned char		tnc_state;

	struct timer_list	tx_t;
	struct timer_list	resync_t;
	atomic_t		refcnt;
	struct semaphore	dead_sem;
	spinlock_t		lock;
};

#define AX25_6PACK_HEADER_LEN 0

static void sixpack_decode(struct sixpack *, unsigned char[], int);
static int encode_sixpack(unsigned char *, unsigned char *, int, unsigned char);

/*
 * Perform the persistence/slottime algorithm for CSMA access. If the
 * persistence check was successful, write the data to the serial driver.
 * Note that in case of DAMA operation, the data is not sent here.
 */

static void sp_xmit_on_air(unsigned long channel)
{
	struct sixpack *sp = (struct sixpack *) channel;
	int actual, when = sp->slottime;
	static unsigned char random;

	random = random * 17 + 41;

	if (((sp->status1 & SIXP_DCD_MASK) == 0) && (random < sp->persistence)) {
		sp->led_state = 0x70;
		sp->tty->ops->write(sp->tty, &sp->led_state, 1);
		sp->tx_enable = 1;
		actual = sp->tty->ops->write(sp->tty, sp->xbuff, sp->status2);
		sp->xleft -= actual;
		sp->xhead += actual;
		sp->led_state = 0x60;
		sp->tty->ops->write(sp->tty, &sp->led_state, 1);
		sp->status2 = 0;
	} else
		mod_timer(&sp->tx_t, jiffies + ((when + 1) * HZ) / 100);
}

/* ----> 6pack timer interrupt handler and friends. <---- */

/* Encapsulate one AX.25 frame and stuff into a TTY queue. */
static void sp_encaps(struct sixpack *sp, unsigned char *icp, int len)
{
	unsigned char *msg, *p = icp;
	int actual, count;

	if (len > sp->mtu) {	/* sp->mtu = AX25_MTU = max. PACLEN = 256 */
		msg = "oversized transmit packet!";
		goto out_drop;
	}

	if (len > sp->mtu) {	/* sp->mtu = AX25_MTU = max. PACLEN = 256 */
		msg = "oversized transmit packet!";
		goto out_drop;
	}

	if (p[0] > 5) {
		msg = "invalid KISS command";
		goto out_drop;
	}

	if ((p[0] != 0) && (len > 2)) {
		msg = "KISS control packet too long";
		goto out_drop;
	}

	if ((p[0] == 0) && (len < 15)) {
		msg = "bad AX.25 packet to transmit";
		goto out_drop;
	}

	count = encode_sixpack(p, sp->xbuff, len, sp->tx_delay);
	set_bit(TTY_DO_WRITE_WAKEUP, &sp->tty->flags);

	switch (p[0]) {
	case 1:	sp->tx_delay = p[1];
		return;
	case 2:	sp->persistence = p[1];
		return;
	case 3:	sp->slottime = p[1];
		return;
	case 4:	/* ignored */
		return;
	case 5:	sp->duplex = p[1];
		return;
	}

	if (p[0] != 0)
		return;

	/*
	 * In case of fullduplex or DAMA operation, we don't take care about the
	 * state of the DCD or of any timers, as the determination of the
	 * correct time to send is the job of the AX.25 layer. We send
	 * immediately after data has arrived.
	 */
	if (sp->duplex == 1) {
		sp->led_state = 0x70;
		sp->tty->ops->write(sp->tty, &sp->led_state, 1);
		sp->tx_enable = 1;
		actual = sp->tty->ops->write(sp->tty, sp->xbuff, count);
		sp->xleft = count - actual;
		sp->xhead = sp->xbuff + actual;
		sp->led_state = 0x60;
		sp->tty->ops->write(sp->tty, &sp->led_state, 1);
	} else {
		sp->xleft = count;
		sp->xhead = sp->xbuff;
		sp->status2 = count;
		sp_xmit_on_air((unsigned long)sp);
	}

	return;

out_drop:
	sp->dev->stats.tx_dropped++;
	netif_start_queue(sp->dev);
	if (net_ratelimit())
		printk(KERN_DEBUG "%s: %s - dropped.\n", sp->dev->name, msg);
}

/* Encapsulate an IP datagram and kick it into a TTY queue. */

static netdev_tx_t sp_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sixpack *sp = netdev_priv(dev);

	if (skb->protocol == htons(ETH_P_IP))
		return ax25_ip_xmit(skb);

	spin_lock_bh(&sp->lock);
	/* We were not busy, so we are now... :-) */
	netif_stop_queue(dev);
	dev->stats.tx_bytes += skb->len;
	sp_encaps(sp, skb->data, skb->len);
	spin_unlock_bh(&sp->lock);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int sp_open_dev(struct net_device *dev)
{
	struct sixpack *sp = netdev_priv(dev);

	if (sp->tty == NULL)
		return -ENODEV;
	return 0;
}

/* Close the low-level part of the 6pack channel. */
static int sp_close(struct net_device *dev)
{
	struct sixpack *sp = netdev_priv(dev);

	spin_lock_bh(&sp->lock);
	if (sp->tty) {
		/* TTY discipline is running. */
		clear_bit(TTY_DO_WRITE_WAKEUP, &sp->tty->flags);
	}
	netif_stop_queue(dev);
	spin_unlock_bh(&sp->lock);

	return 0;
}

static int sp_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr_ax25 *sa = addr;

	netif_tx_lock_bh(dev);
	netif_addr_lock(dev);
	memcpy(dev->dev_addr, &sa->sax25_call, AX25_ADDR_LEN);
	netif_addr_unlock(dev);
	netif_tx_unlock_bh(dev);

	return 0;
}

static const struct net_device_ops sp_netdev_ops = {
	.ndo_open		= sp_open_dev,
	.ndo_stop		= sp_close,
	.ndo_start_xmit		= sp_xmit,
	.ndo_set_mac_address    = sp_set_mac_address,
};

static void sp_setup(struct net_device *dev)
{
	/* Finish setting up the DEVICE info. */
	dev->netdev_ops		= &sp_netdev_ops;
	dev->destructor		= free_netdev;
	dev->mtu		= SIXP_MTU;
	dev->hard_header_len	= AX25_MAX_HEADER_LEN;
	dev->header_ops 	= &ax25_header_ops;

	dev->addr_len		= AX25_ADDR_LEN;
	dev->type		= ARPHRD_AX25;
	dev->tx_queue_len	= 10;

	/* Only activated in AX.25 mode */
	memcpy(dev->broadcast, &ax25_bcast, AX25_ADDR_LEN);
	memcpy(dev->dev_addr, &ax25_defaddr, AX25_ADDR_LEN);

	dev->flags		= 0;
}

/* Send one completely decapsulated IP datagram to the IP layer. */

/*
 * This is the routine that sends the received data to the kernel AX.25.
 * 'cmd' is the KISS command. For AX.25 data, it is zero.
 */

static void sp_bump(struct sixpack *sp, char cmd)
{
	struct sk_buff *skb;
	int count;
	unsigned char *ptr;

	count = sp->rcount + 1;

	sp->dev->stats.rx_bytes += count;

	if ((skb = dev_alloc_skb(count)) == NULL)
		goto out_mem;

	ptr = skb_put(skb, count);
	*ptr++ = cmd;	/* KISS command */

	memcpy(ptr, sp->cooked_buf + 1, count);
	skb->protocol = ax25_type_trans(skb, sp->dev);
	netif_rx(skb);
	sp->dev->stats.rx_packets++;

	return;

out_mem:
	sp->dev->stats.rx_dropped++;
}


/* ----------------------------------------------------------------------- */

/*
 * We have a potential race on dereferencing tty->disc_data, because the tty
 * layer provides no locking at all - thus one cpu could be running
 * sixpack_receive_buf while another calls sixpack_close, which zeroes
 * tty->disc_data and frees the memory that sixpack_receive_buf is using.  The
 * best way to fix this is to use a rwlock in the tty struct, but for now we
 * use a single global rwlock for all ttys in ppp line discipline.
 */
static DEFINE_RWLOCK(disc_data_lock);
                                                                                
static struct sixpack *sp_get(struct tty_struct *tty)
{
	struct sixpack *sp;

	read_lock(&disc_data_lock);
	sp = tty->disc_data;
	if (sp)
		atomic_inc(&sp->refcnt);
	read_unlock(&disc_data_lock);

	return sp;
}

static void sp_put(struct sixpack *sp)
{
	if (atomic_dec_and_test(&sp->refcnt))
		up(&sp->dead_sem);
}

/*
 * Called by the TTY driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */
static void sixpack_write_wakeup(struct tty_struct *tty)
{
	struct sixpack *sp = sp_get(tty);
	int actual;

	if (!sp)
		return;
	if (sp->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet */
		sp->dev->stats.tx_packets++;
		clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		sp->tx_enable = 0;
		netif_wake_queue(sp->dev);
		goto out;
	}

	if (sp->tx_enable) {
		actual = tty->ops->write(tty, sp->xhead, sp->xleft);
		sp->xleft -= actual;
		sp->xhead += actual;
	}

out:
	sp_put(sp);
}

/* ----------------------------------------------------------------------- */

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of 6pack data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */
static void sixpack_receive_buf(struct tty_struct *tty,
	const unsigned char *cp, char *fp, int count)
{
	struct sixpack *sp;
	unsigned char buf[512];
	int count1;

	if (!count)
		return;

	sp = sp_get(tty);
	if (!sp)
		return;

	memcpy(buf, cp, count < sizeof(buf) ? count : sizeof(buf));

	/* Read the characters out of the buffer */

	count1 = count;
	while (count) {
		count--;
		if (fp && *fp++) {
			if (!test_and_set_bit(SIXPF_ERROR, &sp->flags))
				sp->dev->stats.rx_errors++;
			continue;
		}
	}
	sixpack_decode(sp, buf, count1);

	sp_put(sp);
	tty_unthrottle(tty);
}

/*
 * Try to resync the TNC. Called by the resync timer defined in
 * decode_prio_command
 */

#define TNC_UNINITIALIZED	0
#define TNC_UNSYNC_STARTUP	1
#define TNC_UNSYNCED		2
#define TNC_IN_SYNC		3

static void __tnc_set_sync_state(struct sixpack *sp, int new_tnc_state)
{
	char *msg;

	switch (new_tnc_state) {
	default:			/* gcc oh piece-o-crap ... */
	case TNC_UNSYNC_STARTUP:
		msg = "Synchronizing with TNC";
		break;
	case TNC_UNSYNCED:
		msg = "Lost synchronization with TNC\n";
		break;
	case TNC_IN_SYNC:
		msg = "Found TNC";
		break;
	}

	sp->tnc_state = new_tnc_state;
	printk(KERN_INFO "%s: %s\n", sp->dev->name, msg);
}

static inline void tnc_set_sync_state(struct sixpack *sp, int new_tnc_state)
{
	int old_tnc_state = sp->tnc_state;

	if (old_tnc_state != new_tnc_state)
		__tnc_set_sync_state(sp, new_tnc_state);
}

static void resync_tnc(unsigned long channel)
{
	struct sixpack *sp = (struct sixpack *) channel;
	static char resync_cmd = 0xe8;

	/* clear any data that might have been received */

	sp->rx_count = 0;
	sp->rx_count_cooked = 0;

	/* reset state machine */

	sp->status = 1;
	sp->status1 = 1;
	sp->status2 = 0;

	/* resync the TNC */

	sp->led_state = 0x60;
	sp->tty->ops->write(sp->tty, &sp->led_state, 1);
	sp->tty->ops->write(sp->tty, &resync_cmd, 1);


	/* Start resync timer again -- the TNC might be still absent */

	del_timer(&sp->resync_t);
	sp->resync_t.data	= (unsigned long) sp;
	sp->resync_t.function	= resync_tnc;
	sp->resync_t.expires	= jiffies + SIXP_RESYNC_TIMEOUT;
	add_timer(&sp->resync_t);
}

static inline int tnc_init(struct sixpack *sp)
{
	unsigned char inbyte = 0xe8;

	tnc_set_sync_state(sp, TNC_UNSYNC_STARTUP);

	sp->tty->ops->write(sp->tty, &inbyte, 1);

	del_timer(&sp->resync_t);
	sp->resync_t.data = (unsigned long) sp;
	sp->resync_t.function = resync_tnc;
	sp->resync_t.expires = jiffies + SIXP_RESYNC_TIMEOUT;
	add_timer(&sp->resync_t);

	return 0;
}

/*
 * Open the high-level part of the 6pack channel.
 * This function is called by the TTY module when the
 * 6pack line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free 6pcack channel...
 */
static int sixpack_open(struct tty_struct *tty)
{
	char *rbuff = NULL, *xbuff = NULL;
	struct net_device *dev;
	struct sixpack *sp;
	unsigned long len;
	int err = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	dev = alloc_netdev(sizeof(struct sixpack), "sp%d", NET_NAME_UNKNOWN,
			   sp_setup);
	if (!dev) {
		err = -ENOMEM;
		goto out;
	}

	sp = netdev_priv(dev);
	sp->dev = dev;

	spin_lock_init(&sp->lock);
	atomic_set(&sp->refcnt, 1);
	sema_init(&sp->dead_sem, 0);

	/* !!! length of the buffers. MTU is IP MTU, not PACLEN!  */

	len = dev->mtu * 2;

	rbuff = kmalloc(len + 4, GFP_KERNEL);
	xbuff = kmalloc(len + 4, GFP_KERNEL);

	if (rbuff == NULL || xbuff == NULL) {
		err = -ENOBUFS;
		goto out_free;
	}

	spin_lock_bh(&sp->lock);

	sp->tty = tty;

	sp->rbuff	= rbuff;
	sp->xbuff	= xbuff;

	sp->mtu		= AX25_MTU + 73;
	sp->buffsize	= len;
	sp->rcount	= 0;
	sp->rx_count	= 0;
	sp->rx_count_cooked = 0;
	sp->xleft	= 0;

	sp->flags	= 0;		/* Clear ESCAPE & ERROR flags */

	sp->duplex	= 0;
	sp->tx_delay    = SIXP_TXDELAY;
	sp->persistence = SIXP_PERSIST;
	sp->slottime    = SIXP_SLOTTIME;
	sp->led_state   = 0x60;
	sp->status      = 1;
	sp->status1     = 1;
	sp->status2     = 0;
	sp->tx_enable   = 0;

	netif_start_queue(dev);

	init_timer(&sp->tx_t);
	sp->tx_t.function = sp_xmit_on_air;
	sp->tx_t.data = (unsigned long) sp;

	init_timer(&sp->resync_t);

	spin_unlock_bh(&sp->lock);

	/* Done.  We have linked the TTY line to a channel. */
	tty->disc_data = sp;
	tty->receive_room = 65536;

	/* Now we're ready to register. */
	err = register_netdev(dev);
	if (err)
		goto out_free;

	tnc_init(sp);

	return 0;

out_free:
	kfree(xbuff);
	kfree(rbuff);

	free_netdev(dev);

out:
	return err;
}


/*
 * Close down a 6pack channel.
 * This means flushing out any pending queues, and then restoring the
 * TTY line discipline to what it was before it got hooked to 6pack
 * (which usually is TTY again).
 */
static void sixpack_close(struct tty_struct *tty)
{
	struct sixpack *sp;

	write_lock_bh(&disc_data_lock);
	sp = tty->disc_data;
	tty->disc_data = NULL;
	write_unlock_bh(&disc_data_lock);
	if (!sp)
		return;

	/*
	 * We have now ensured that nobody can start using ap from now on, but
	 * we have to wait for all existing users to finish.
	 */
	if (!atomic_dec_and_test(&sp->refcnt))
		down(&sp->dead_sem);

	/* We must stop the queue to avoid potentially scribbling
	 * on the free buffers. The sp->dead_sem is not sufficient
	 * to protect us from sp->xbuff access.
	 */
	netif_stop_queue(sp->dev);

	del_timer_sync(&sp->tx_t);
	del_timer_sync(&sp->resync_t);

	/* Free all 6pack frame buffers. */
	kfree(sp->rbuff);
	kfree(sp->xbuff);

	unregister_netdev(sp->dev);
}

/* Perform I/O control on an active 6pack channel. */
static int sixpack_ioctl(struct tty_struct *tty, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct sixpack *sp = sp_get(tty);
	struct net_device *dev;
	unsigned int tmp, err;

	if (!sp)
		return -ENXIO;
	dev = sp->dev;

	switch(cmd) {
	case SIOCGIFNAME:
		err = copy_to_user((void __user *) arg, dev->name,
		                   strlen(dev->name) + 1) ? -EFAULT : 0;
		break;

	case SIOCGIFENCAP:
		err = put_user(0, (int __user *) arg);
		break;

	case SIOCSIFENCAP:
		if (get_user(tmp, (int __user *) arg)) {
			err = -EFAULT;
			break;
		}

		sp->mode = tmp;
		dev->addr_len        = AX25_ADDR_LEN;
		dev->hard_header_len = AX25_KISS_HEADER_LEN +
		                       AX25_MAX_HEADER_LEN + 3;
		dev->type            = ARPHRD_AX25;

		err = 0;
		break;

	 case SIOCSIFHWADDR: {
		char addr[AX25_ADDR_LEN];

		if (copy_from_user(&addr,
		                   (void __user *) arg, AX25_ADDR_LEN)) {
				err = -EFAULT;
				break;
			}

			netif_tx_lock_bh(dev);
			memcpy(dev->dev_addr, &addr, AX25_ADDR_LEN);
			netif_tx_unlock_bh(dev);

			err = 0;
			break;
		}

	default:
		err = tty_mode_ioctl(tty, file, cmd, arg);
	}

	sp_put(sp);

	return err;
}

#ifdef CONFIG_COMPAT
static long sixpack_compat_ioctl(struct tty_struct * tty, struct file * file,
				unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SIOCGIFNAME:
	case SIOCGIFENCAP:
	case SIOCSIFENCAP:
	case SIOCSIFHWADDR:
		return sixpack_ioctl(tty, file, cmd,
				(unsigned long)compat_ptr(arg));
	}

	return -ENOIOCTLCMD;
}
#endif

static struct tty_ldisc_ops sp_ldisc = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= "6pack",
	.open		= sixpack_open,
	.close		= sixpack_close,
	.ioctl		= sixpack_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= sixpack_compat_ioctl,
#endif
	.receive_buf	= sixpack_receive_buf,
	.write_wakeup	= sixpack_write_wakeup,
};

/* Initialize 6pack control device -- register 6pack line discipline */

static const char msg_banner[]  __initconst = KERN_INFO \
	"AX.25: 6pack driver, " SIXPACK_VERSION "\n";
static const char msg_regfail[] __initconst = KERN_ERR  \
	"6pack: can't register line discipline (err = %d)\n";

static int __init sixpack_init_driver(void)
{
	int status;

	printk(msg_banner);

	/* Register the provided line protocol discipline */
	if ((status = tty_register_ldisc(N_6PACK, &sp_ldisc)) != 0)
		printk(msg_regfail, status);

	return status;
}

static const char msg_unregfail[] = KERN_ERR \
	"6pack: can't unregister line discipline (err = %d)\n";

static void __exit sixpack_exit_driver(void)
{
	int ret;

	if ((ret = tty_unregister_ldisc(N_6PACK)))
		printk(msg_unregfail, ret);
}

/* encode an AX.25 packet into 6pack */

static int encode_sixpack(unsigned char *tx_buf, unsigned char *tx_buf_raw,
	int length, unsigned char tx_delay)
{
	int count = 0;
	unsigned char checksum = 0, buf[400];
	int raw_count = 0;

	tx_buf_raw[raw_count++] = SIXP_PRIO_CMD_MASK | SIXP_TX_MASK;
	tx_buf_raw[raw_count++] = SIXP_SEOF;

	buf[0] = tx_delay;
	for (count = 1; count < length; count++)
		buf[count] = tx_buf[count];

	for (count = 0; count < length; count++)
		checksum += buf[count];
	buf[length] = (unsigned char) 0xff - checksum;

	for (count = 0; count <= length; count++) {
		if ((count % 3) == 0) {
			tx_buf_raw[raw_count++] = (buf[count] & 0x3f);
			tx_buf_raw[raw_count] = ((buf[count] >> 2) & 0x30);
		} else if ((count % 3) == 1) {
			tx_buf_raw[raw_count++] |= (buf[count] & 0x0f);
			tx_buf_raw[raw_count] =	((buf[count] >> 2) & 0x3c);
		} else {
			tx_buf_raw[raw_count++] |= (buf[count] & 0x03);
			tx_buf_raw[raw_count++] = (buf[count] >> 2);
		}
	}
	if ((length % 3) != 2)
		raw_count++;
	tx_buf_raw[raw_count++] = SIXP_SEOF;
	return raw_count;
}

/* decode 4 sixpack-encoded bytes into 3 data bytes */

static void decode_data(struct sixpack *sp, unsigned char inbyte)
{
	unsigned char *buf;

	if (sp->rx_count != 3) {
		sp->raw_buf[sp->rx_count++] = inbyte;

		return;
	}

	buf = sp->raw_buf;
	sp->cooked_buf[sp->rx_count_cooked++] =
		buf[0] | ((buf[1] << 2) & 0xc0);
	sp->cooked_buf[sp->rx_count_cooked++] =
		(buf[1] & 0x0f) | ((buf[2] << 2) & 0xf0);
	sp->cooked_buf[sp->rx_count_cooked++] =
		(buf[2] & 0x03) | (inbyte << 2);
	sp->rx_count = 0;
}

/* identify and execute a 6pack priority command byte */

static void decode_prio_command(struct sixpack *sp, unsigned char cmd)
{
	unsigned char channel;
	int actual;

	channel = cmd & SIXP_CHN_MASK;
	if ((cmd & SIXP_PRIO_DATA_MASK) != 0) {     /* idle ? */

	/* RX and DCD flags can only be set in the same prio command,
	   if the DCD flag has been set without the RX flag in the previous
	   prio command. If DCD has not been set before, something in the
	   transmission has gone wrong. In this case, RX and DCD are
	   cleared in order to prevent the decode_data routine from
	   reading further data that might be corrupt. */

		if (((sp->status & SIXP_DCD_MASK) == 0) &&
			((cmd & SIXP_RX_DCD_MASK) == SIXP_RX_DCD_MASK)) {
				if (sp->status != 1)
					printk(KERN_DEBUG "6pack: protocol violation\n");
				else
					sp->status = 0;
				cmd &= ~SIXP_RX_DCD_MASK;
		}
		sp->status = cmd & SIXP_PRIO_DATA_MASK;
	} else { /* output watchdog char if idle */
		if ((sp->status2 != 0) && (sp->duplex == 1)) {
			sp->led_state = 0x70;
			sp->tty->ops->write(sp->tty, &sp->led_state, 1);
			sp->tx_enable = 1;
			actual = sp->tty->ops->write(sp->tty, sp->xbuff, sp->status2);
			sp->xleft -= actual;
			sp->xhead += actual;
			sp->led_state = 0x60;
			sp->status2 = 0;

		}
	}

	/* needed to trigger the TNC watchdog */
	sp->tty->ops->write(sp->tty, &sp->led_state, 1);

        /* if the state byte has been received, the TNC is present,
           so the resync timer can be reset. */

	if (sp->tnc_state == TNC_IN_SYNC) {
		del_timer(&sp->resync_t);
		sp->resync_t.data	= (unsigned long) sp;
		sp->resync_t.function	= resync_tnc;
		sp->resync_t.expires	= jiffies + SIXP_INIT_RESYNC_TIMEOUT;
		add_timer(&sp->resync_t);
	}

	sp->status1 = cmd & SIXP_PRIO_DATA_MASK;
}

/* identify and execute a standard 6pack command byte */

static void decode_std_command(struct sixpack *sp, unsigned char cmd)
{
	unsigned char checksum = 0, rest = 0, channel;
	short i;

	channel = cmd & SIXP_CHN_MASK;
	switch (cmd & SIXP_CMD_MASK) {     /* normal command */
	case SIXP_SEOF:
		if ((sp->rx_count == 0) && (sp->rx_count_cooked == 0)) {
			if ((sp->status & SIXP_RX_DCD_MASK) ==
				SIXP_RX_DCD_MASK) {
				sp->led_state = 0x68;
				sp->tty->ops->write(sp->tty, &sp->led_state, 1);
			}
		} else {
			sp->led_state = 0x60;
			/* fill trailing bytes with zeroes */
			sp->tty->ops->write(sp->tty, &sp->led_state, 1);
			rest = sp->rx_count;
			if (rest != 0)
				 for (i = rest; i <= 3; i++)
					decode_data(sp, 0);
			if (rest == 2)
				sp->rx_count_cooked -= 2;
			else if (rest == 3)
				sp->rx_count_cooked -= 1;
			for (i = 0; i < sp->rx_count_cooked; i++)
				checksum += sp->cooked_buf[i];
			if (checksum != SIXP_CHKSUM) {
				printk(KERN_DEBUG "6pack: bad checksum %2.2x\n", checksum);
			} else {
				sp->rcount = sp->rx_count_cooked-2;
				sp_bump(sp, 0);
			}
			sp->rx_count_cooked = 0;
		}
		break;
	case SIXP_TX_URUN: printk(KERN_DEBUG "6pack: TX underrun\n");
		break;
	case SIXP_RX_ORUN: printk(KERN_DEBUG "6pack: RX overrun\n");
		break;
	case SIXP_RX_BUF_OVL:
		printk(KERN_DEBUG "6pack: RX buffer overflow\n");
	}
}

/* decode a 6pack packet */

static void
sixpack_decode(struct sixpack *sp, unsigned char *pre_rbuff, int count)
{
	unsigned char inbyte;
	int count1;

	for (count1 = 0; count1 < count; count1++) {
		inbyte = pre_rbuff[count1];
		if (inbyte == SIXP_FOUND_TNC) {
			tnc_set_sync_state(sp, TNC_IN_SYNC);
			del_timer(&sp->resync_t);
		}
		if ((inbyte & SIXP_PRIO_CMD_MASK) != 0)
			decode_prio_command(sp, inbyte);
		else if ((inbyte & SIXP_STD_CMD_MASK) != 0)
			decode_std_command(sp, inbyte);
		else if ((sp->status & SIXP_RX_DCD_MASK) == SIXP_RX_DCD_MASK)
			decode_data(sp, inbyte);
	}
}

MODULE_AUTHOR("Ralf Baechle DO1GRB <ralf@linux-mips.org>");
MODULE_DESCRIPTION("6pack driver for AX.25");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_6PACK);

module_init(sixpack_init_driver);
module_exit(sixpack_exit_driver);

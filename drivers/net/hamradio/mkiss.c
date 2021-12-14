// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) Hans Alblas PE1AYX <hans@esrac.ele.tue.nl>
 * Copyright (C) 2004, 05 Ralf Baechle DL5RB <ralf@linux-mips.org>
 * Copyright (C) 2004, 05 Thomas Osterried DL9SAU <thomas@x-berg.in-berlin.de>
 */
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/crc16.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/jiffies.h>
#include <linux/refcount.h>

#include <net/ax25.h>

#define AX_MTU		236

/* SLIP/KISS protocol characters. */
#define END             0300		/* indicates end of frame	*/
#define ESC             0333		/* indicates byte stuffing	*/
#define ESC_END         0334		/* ESC ESC_END means END 'data'	*/
#define ESC_ESC         0335		/* ESC ESC_ESC means ESC 'data'	*/

struct mkiss {
	struct tty_struct	*tty;	/* ptr to TTY structure		*/
	struct net_device	*dev;	/* easy for intr handling	*/

	/* These are pointers to the malloc()ed frame buffers. */
	spinlock_t		buflock;/* lock for rbuf and xbuf */
	unsigned char		*rbuff;	/* receiver buffer		*/
	int			rcount;	/* received chars counter       */
	unsigned char		*xbuff;	/* transmitter buffer		*/
	unsigned char		*xhead;	/* pointer to next byte to XMIT */
	int			xleft;	/* bytes left in XMIT queue     */

	/* Detailed SLIP statistics. */
	int		mtu;		/* Our mtu (to spot changes!)   */
	int		buffsize;	/* Max buffers sizes            */

	unsigned long	flags;		/* Flag values/ mode etc	*/
					/* long req'd: used by set_bit --RR */
#define AXF_INUSE	0		/* Channel in use               */
#define AXF_ESCAPE	1               /* ESC received                 */
#define AXF_ERROR	2               /* Parity, etc. error           */
#define AXF_KEEPTEST	3		/* Keepalive test flag		*/
#define AXF_OUTWAIT	4		/* is outpacket was flag	*/

	int		mode;
        int		crcmode;	/* MW: for FlexNet, SMACK etc.  */
	int		crcauto;	/* CRC auto mode */

#define CRC_MODE_NONE		0
#define CRC_MODE_FLEX		1
#define CRC_MODE_SMACK		2
#define CRC_MODE_FLEX_TEST	3
#define CRC_MODE_SMACK_TEST	4

	refcount_t		refcnt;
	struct completion	dead;
};

/*---------------------------------------------------------------------------*/

static const unsigned short crc_flex_table[] = {
	0x0f87, 0x1e0e, 0x2c95, 0x3d1c, 0x49a3, 0x582a, 0x6ab1, 0x7b38,
	0x83cf, 0x9246, 0xa0dd, 0xb154, 0xc5eb, 0xd462, 0xe6f9, 0xf770,
	0x1f06, 0x0e8f, 0x3c14, 0x2d9d, 0x5922, 0x48ab, 0x7a30, 0x6bb9,
	0x934e, 0x82c7, 0xb05c, 0xa1d5, 0xd56a, 0xc4e3, 0xf678, 0xe7f1,
	0x2e85, 0x3f0c, 0x0d97, 0x1c1e, 0x68a1, 0x7928, 0x4bb3, 0x5a3a,
	0xa2cd, 0xb344, 0x81df, 0x9056, 0xe4e9, 0xf560, 0xc7fb, 0xd672,
	0x3e04, 0x2f8d, 0x1d16, 0x0c9f, 0x7820, 0x69a9, 0x5b32, 0x4abb,
	0xb24c, 0xa3c5, 0x915e, 0x80d7, 0xf468, 0xe5e1, 0xd77a, 0xc6f3,
	0x4d83, 0x5c0a, 0x6e91, 0x7f18, 0x0ba7, 0x1a2e, 0x28b5, 0x393c,
	0xc1cb, 0xd042, 0xe2d9, 0xf350, 0x87ef, 0x9666, 0xa4fd, 0xb574,
	0x5d02, 0x4c8b, 0x7e10, 0x6f99, 0x1b26, 0x0aaf, 0x3834, 0x29bd,
	0xd14a, 0xc0c3, 0xf258, 0xe3d1, 0x976e, 0x86e7, 0xb47c, 0xa5f5,
	0x6c81, 0x7d08, 0x4f93, 0x5e1a, 0x2aa5, 0x3b2c, 0x09b7, 0x183e,
	0xe0c9, 0xf140, 0xc3db, 0xd252, 0xa6ed, 0xb764, 0x85ff, 0x9476,
	0x7c00, 0x6d89, 0x5f12, 0x4e9b, 0x3a24, 0x2bad, 0x1936, 0x08bf,
	0xf048, 0xe1c1, 0xd35a, 0xc2d3, 0xb66c, 0xa7e5, 0x957e, 0x84f7,
	0x8b8f, 0x9a06, 0xa89d, 0xb914, 0xcdab, 0xdc22, 0xeeb9, 0xff30,
	0x07c7, 0x164e, 0x24d5, 0x355c, 0x41e3, 0x506a, 0x62f1, 0x7378,
	0x9b0e, 0x8a87, 0xb81c, 0xa995, 0xdd2a, 0xcca3, 0xfe38, 0xefb1,
	0x1746, 0x06cf, 0x3454, 0x25dd, 0x5162, 0x40eb, 0x7270, 0x63f9,
	0xaa8d, 0xbb04, 0x899f, 0x9816, 0xeca9, 0xfd20, 0xcfbb, 0xde32,
	0x26c5, 0x374c, 0x05d7, 0x145e, 0x60e1, 0x7168, 0x43f3, 0x527a,
	0xba0c, 0xab85, 0x991e, 0x8897, 0xfc28, 0xeda1, 0xdf3a, 0xceb3,
	0x3644, 0x27cd, 0x1556, 0x04df, 0x7060, 0x61e9, 0x5372, 0x42fb,
	0xc98b, 0xd802, 0xea99, 0xfb10, 0x8faf, 0x9e26, 0xacbd, 0xbd34,
	0x45c3, 0x544a, 0x66d1, 0x7758, 0x03e7, 0x126e, 0x20f5, 0x317c,
	0xd90a, 0xc883, 0xfa18, 0xeb91, 0x9f2e, 0x8ea7, 0xbc3c, 0xadb5,
	0x5542, 0x44cb, 0x7650, 0x67d9, 0x1366, 0x02ef, 0x3074, 0x21fd,
	0xe889, 0xf900, 0xcb9b, 0xda12, 0xaead, 0xbf24, 0x8dbf, 0x9c36,
	0x64c1, 0x7548, 0x47d3, 0x565a, 0x22e5, 0x336c, 0x01f7, 0x107e,
	0xf808, 0xe981, 0xdb1a, 0xca93, 0xbe2c, 0xafa5, 0x9d3e, 0x8cb7,
	0x7440, 0x65c9, 0x5752, 0x46db, 0x3264, 0x23ed, 0x1176, 0x00ff
};

static unsigned short calc_crc_flex(unsigned char *cp, int size)
{
	unsigned short crc = 0xffff;

	while (size--)
		crc = (crc << 8) ^ crc_flex_table[((crc >> 8) ^ *cp++) & 0xff];

	return crc;
}

static int check_crc_flex(unsigned char *cp, int size)
{
	unsigned short crc = 0xffff;

	if (size < 3)
		return -1;

	while (size--)
		crc = (crc << 8) ^ crc_flex_table[((crc >> 8) ^ *cp++) & 0xff];

	if ((crc & 0xffff) != 0x7070)
		return -1;

	return 0;
}

static int check_crc_16(unsigned char *cp, int size)
{
	unsigned short crc = 0x0000;

	if (size < 3)
		return -1;

	crc = crc16(0, cp, size);

	if (crc != 0x0000)
		return -1;

	return 0;
}

/*
 * Standard encapsulation
 */

static int kiss_esc(unsigned char *s, unsigned char *d, int len)
{
	unsigned char *ptr = d;
	unsigned char c;

	/*
	 * Send an initial END character to flush out any data that may have
	 * accumulated in the receiver due to line noise.
	 */

	*ptr++ = END;

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

/*
 * MW:
 * OK its ugly, but tell me a better solution without copying the
 * packet to a temporary buffer :-)
 */
static int kiss_esc_crc(unsigned char *s, unsigned char *d, unsigned short crc,
	int len)
{
	unsigned char *ptr = d;
	unsigned char c=0;

	*ptr++ = END;
	while (len > 0) {
		if (len > 2)
			c = *s++;
		else if (len > 1)
			c = crc >> 8;
		else
			c = crc & 0xff;

		len--;

		switch (c) {
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

/* Send one completely decapsulated AX.25 packet to the AX.25 layer. */
static void ax_bump(struct mkiss *ax)
{
	struct sk_buff *skb;
	int count;

	spin_lock_bh(&ax->buflock);
	if (ax->rbuff[0] > 0x0f) {
		if (ax->rbuff[0] & 0x80) {
			if (check_crc_16(ax->rbuff, ax->rcount) < 0) {
				ax->dev->stats.rx_errors++;
				spin_unlock_bh(&ax->buflock);

				return;
			}
			if (ax->crcmode != CRC_MODE_SMACK && ax->crcauto) {
				printk(KERN_INFO
				       "mkiss: %s: Switching to crc-smack\n",
				       ax->dev->name);
				ax->crcmode = CRC_MODE_SMACK;
			}
			ax->rcount -= 2;
			*ax->rbuff &= ~0x80;
		} else if (ax->rbuff[0] & 0x20)  {
			if (check_crc_flex(ax->rbuff, ax->rcount) < 0) {
				ax->dev->stats.rx_errors++;
				spin_unlock_bh(&ax->buflock);
				return;
			}
			if (ax->crcmode != CRC_MODE_FLEX && ax->crcauto) {
				printk(KERN_INFO
				       "mkiss: %s: Switching to crc-flexnet\n",
				       ax->dev->name);
				ax->crcmode = CRC_MODE_FLEX;
			}
			ax->rcount -= 2;

			/*
			 * dl9sau bugfix: the trailling two bytes flexnet crc
			 * will not be passed to the kernel. thus we have to
			 * correct the kissparm signature, because it indicates
			 * a crc but there's none
			 */
			*ax->rbuff &= ~0x20;
		}
 	}

	count = ax->rcount;

	if ((skb = dev_alloc_skb(count)) == NULL) {
		printk(KERN_ERR "mkiss: %s: memory squeeze, dropping packet.\n",
		       ax->dev->name);
		ax->dev->stats.rx_dropped++;
		spin_unlock_bh(&ax->buflock);
		return;
	}

	skb_put_data(skb, ax->rbuff, count);
	skb->protocol = ax25_type_trans(skb, ax->dev);
	netif_rx(skb);
	ax->dev->stats.rx_packets++;
	ax->dev->stats.rx_bytes += count;
	spin_unlock_bh(&ax->buflock);
}

static void kiss_unesc(struct mkiss *ax, unsigned char s)
{
	switch (s) {
	case END:
		/* drop keeptest bit = VSV */
		if (test_bit(AXF_KEEPTEST, &ax->flags))
			clear_bit(AXF_KEEPTEST, &ax->flags);

		if (!test_and_clear_bit(AXF_ERROR, &ax->flags) && (ax->rcount > 2))
			ax_bump(ax);

		clear_bit(AXF_ESCAPE, &ax->flags);
		ax->rcount = 0;
		return;

	case ESC:
		set_bit(AXF_ESCAPE, &ax->flags);
		return;
	case ESC_ESC:
		if (test_and_clear_bit(AXF_ESCAPE, &ax->flags))
			s = ESC;
		break;
	case ESC_END:
		if (test_and_clear_bit(AXF_ESCAPE, &ax->flags))
			s = END;
		break;
	}

	spin_lock_bh(&ax->buflock);
	if (!test_bit(AXF_ERROR, &ax->flags)) {
		if (ax->rcount < ax->buffsize) {
			ax->rbuff[ax->rcount++] = s;
			spin_unlock_bh(&ax->buflock);
			return;
		}

		ax->dev->stats.rx_over_errors++;
		set_bit(AXF_ERROR, &ax->flags);
	}
	spin_unlock_bh(&ax->buflock);
}

static int ax_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr_ax25 *sa = addr;

	netif_tx_lock_bh(dev);
	netif_addr_lock(dev);
	memcpy(dev->dev_addr, &sa->sax25_call, AX25_ADDR_LEN);
	netif_addr_unlock(dev);
	netif_tx_unlock_bh(dev);

	return 0;
}

/*---------------------------------------------------------------------------*/

static void ax_changedmtu(struct mkiss *ax)
{
	struct net_device *dev = ax->dev;
	unsigned char *xbuff, *rbuff, *oxbuff, *orbuff;
	int len;

	len = dev->mtu * 2;

	/*
	 * allow for arrival of larger UDP packets, even if we say not to
	 * also fixes a bug in which SunOS sends 512-byte packets even with
	 * an MSS of 128
	 */
	if (len < 576 * 2)
		len = 576 * 2;

	xbuff = kmalloc(len + 4, GFP_ATOMIC);
	rbuff = kmalloc(len + 4, GFP_ATOMIC);

	if (xbuff == NULL || rbuff == NULL)  {
		printk(KERN_ERR "mkiss: %s: unable to grow ax25 buffers, "
		       "MTU change cancelled.\n",
		       ax->dev->name);
		dev->mtu = ax->mtu;
		kfree(xbuff);
		kfree(rbuff);
		return;
	}

	spin_lock_bh(&ax->buflock);

	oxbuff    = ax->xbuff;
	ax->xbuff = xbuff;
	orbuff    = ax->rbuff;
	ax->rbuff = rbuff;

	if (ax->xleft) {
		if (ax->xleft <= len) {
			memcpy(ax->xbuff, ax->xhead, ax->xleft);
		} else  {
			ax->xleft = 0;
			dev->stats.tx_dropped++;
		}
	}

	ax->xhead = ax->xbuff;

	if (ax->rcount) {
		if (ax->rcount <= len) {
			memcpy(ax->rbuff, orbuff, ax->rcount);
		} else  {
			ax->rcount = 0;
			dev->stats.rx_over_errors++;
			set_bit(AXF_ERROR, &ax->flags);
		}
	}

	ax->mtu      = dev->mtu + 73;
	ax->buffsize = len;

	spin_unlock_bh(&ax->buflock);

	kfree(oxbuff);
	kfree(orbuff);
}

/* Encapsulate one AX.25 packet and stuff into a TTY queue. */
static void ax_encaps(struct net_device *dev, unsigned char *icp, int len)
{
	struct mkiss *ax = netdev_priv(dev);
	unsigned char *p;
	int actual, count;

	if (ax->mtu != ax->dev->mtu + 73)	/* Someone has been ifconfigging */
		ax_changedmtu(ax);

	if (len > ax->mtu) {		/* Sigh, shouldn't occur BUT ... */
		printk(KERN_ERR "mkiss: %s: truncating oversized transmit packet!\n", ax->dev->name);
		dev->stats.tx_dropped++;
		netif_start_queue(dev);
		return;
	}

	p = icp;

	spin_lock_bh(&ax->buflock);
	if ((*p & 0x0f) != 0) {
		/* Configuration Command (kissparms(1).
		 * Protocol spec says: never append CRC.
		 * This fixes a very old bug in the linux
		 * kiss driver. -- dl9sau */
		switch (*p & 0xff) {
		case 0x85:
			/* command from userspace especially for us,
			 * not for delivery to the tnc */
			if (len > 1) {
				int cmd = (p[1] & 0xff);
				switch(cmd) {
				case 3:
				  ax->crcmode = CRC_MODE_SMACK;
				  break;
				case 2:
				  ax->crcmode = CRC_MODE_FLEX;
				  break;
				case 1:
				  ax->crcmode = CRC_MODE_NONE;
				  break;
				case 0:
				default:
				  ax->crcmode = CRC_MODE_SMACK_TEST;
				  cmd = 0;
				}
				ax->crcauto = (cmd ? 0 : 1);
				printk(KERN_INFO "mkiss: %s: crc mode set to %d\n",
				       ax->dev->name, cmd);
			}
			spin_unlock_bh(&ax->buflock);
			netif_start_queue(dev);

			return;
		default:
			count = kiss_esc(p, ax->xbuff, len);
		}
	} else {
		unsigned short crc;
		switch (ax->crcmode) {
		case CRC_MODE_SMACK_TEST:
			ax->crcmode  = CRC_MODE_FLEX_TEST;
			printk(KERN_INFO "mkiss: %s: Trying crc-smack\n", ax->dev->name);
			fallthrough;
		case CRC_MODE_SMACK:
			*p |= 0x80;
			crc = swab16(crc16(0, p, len));
			count = kiss_esc_crc(p, ax->xbuff, crc, len+2);
			break;
		case CRC_MODE_FLEX_TEST:
			ax->crcmode = CRC_MODE_NONE;
			printk(KERN_INFO "mkiss: %s: Trying crc-flexnet\n", ax->dev->name);
			fallthrough;
		case CRC_MODE_FLEX:
			*p |= 0x20;
			crc = calc_crc_flex(p, len);
			count = kiss_esc_crc(p, ax->xbuff, crc, len+2);
			break;

		default:
			count = kiss_esc(p, ax->xbuff, len);
		}
  	}
	spin_unlock_bh(&ax->buflock);

	set_bit(TTY_DO_WRITE_WAKEUP, &ax->tty->flags);
	actual = ax->tty->ops->write(ax->tty, ax->xbuff, count);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += actual;

	netif_trans_update(ax->dev);
	ax->xleft = count - actual;
	ax->xhead = ax->xbuff + actual;
}

/* Encapsulate an AX.25 packet and kick it into a TTY queue. */
static netdev_tx_t ax_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mkiss *ax = netdev_priv(dev);

	if (skb->protocol == htons(ETH_P_IP))
		return ax25_ip_xmit(skb);

	if (!netif_running(dev))  {
		printk(KERN_ERR "mkiss: %s: xmit call when iface is down\n", dev->name);
		return NETDEV_TX_BUSY;
	}

	if (netif_queue_stopped(dev)) {
		/*
		 * May be we must check transmitter timeout here ?
		 *      14 Oct 1994 Dmitry Gorodchanin.
		 */
		if (time_before(jiffies, dev_trans_start(dev) + 20 * HZ)) {
			/* 20 sec timeout not reached */
			return NETDEV_TX_BUSY;
		}

		printk(KERN_ERR "mkiss: %s: transmit timed out, %s?\n", dev->name,
		       (tty_chars_in_buffer(ax->tty) || ax->xleft) ?
		       "bad line quality" : "driver error");

		ax->xleft = 0;
		clear_bit(TTY_DO_WRITE_WAKEUP, &ax->tty->flags);
		netif_start_queue(dev);
	}

	/* We were not busy, so we are now... :-) */
	netif_stop_queue(dev);
	ax_encaps(dev, skb->data, skb->len);
	kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int ax_open_dev(struct net_device *dev)
{
	struct mkiss *ax = netdev_priv(dev);

	if (ax->tty == NULL)
		return -ENODEV;

	return 0;
}

/* Open the low-level part of the AX25 channel. Easy! */
static int ax_open(struct net_device *dev)
{
	struct mkiss *ax = netdev_priv(dev);
	unsigned long len;

	if (ax->tty == NULL)
		return -ENODEV;

	/*
	 * Allocate the frame buffers:
	 *
	 * rbuff	Receive buffer.
	 * xbuff	Transmit buffer.
	 */
	len = dev->mtu * 2;

	/*
	 * allow for arrival of larger UDP packets, even if we say not to
	 * also fixes a bug in which SunOS sends 512-byte packets even with
	 * an MSS of 128
	 */
	if (len < 576 * 2)
		len = 576 * 2;

	if ((ax->rbuff = kmalloc(len + 4, GFP_KERNEL)) == NULL)
		goto norbuff;

	if ((ax->xbuff = kmalloc(len + 4, GFP_KERNEL)) == NULL)
		goto noxbuff;

	ax->mtu	     = dev->mtu + 73;
	ax->buffsize = len;
	ax->rcount   = 0;
	ax->xleft    = 0;

	ax->flags   &= (1 << AXF_INUSE);      /* Clear ESCAPE & ERROR flags */

	spin_lock_init(&ax->buflock);

	return 0;

noxbuff:
	kfree(ax->rbuff);

norbuff:
	return -ENOMEM;
}


/* Close the low-level part of the AX25 channel. Easy! */
static int ax_close(struct net_device *dev)
{
	struct mkiss *ax = netdev_priv(dev);

	if (ax->tty)
		clear_bit(TTY_DO_WRITE_WAKEUP, &ax->tty->flags);

	netif_stop_queue(dev);

	return 0;
}

static const struct net_device_ops ax_netdev_ops = {
	.ndo_open            = ax_open_dev,
	.ndo_stop            = ax_close,
	.ndo_start_xmit	     = ax_xmit,
	.ndo_set_mac_address = ax_set_mac_address,
};

static void ax_setup(struct net_device *dev)
{
	/* Finish setting up the DEVICE info. */
	dev->mtu             = AX_MTU;
	dev->hard_header_len = AX25_MAX_HEADER_LEN;
	dev->addr_len        = AX25_ADDR_LEN;
	dev->type            = ARPHRD_AX25;
	dev->tx_queue_len    = 10;
	dev->header_ops      = &ax25_header_ops;
	dev->netdev_ops	     = &ax_netdev_ops;


	memcpy(dev->broadcast, &ax25_bcast, AX25_ADDR_LEN);
	memcpy(dev->dev_addr,  &ax25_defaddr,  AX25_ADDR_LEN);

	dev->flags      = IFF_BROADCAST | IFF_MULTICAST;
}

/*
 * We have a potential race on dereferencing tty->disc_data, because the tty
 * layer provides no locking at all - thus one cpu could be running
 * sixpack_receive_buf while another calls sixpack_close, which zeroes
 * tty->disc_data and frees the memory that sixpack_receive_buf is using.  The
 * best way to fix this is to use a rwlock in the tty struct, but for now we
 * use a single global rwlock for all ttys in ppp line discipline.
 */
static DEFINE_RWLOCK(disc_data_lock);

static struct mkiss *mkiss_get(struct tty_struct *tty)
{
	struct mkiss *ax;

	read_lock(&disc_data_lock);
	ax = tty->disc_data;
	if (ax)
		refcount_inc(&ax->refcnt);
	read_unlock(&disc_data_lock);

	return ax;
}

static void mkiss_put(struct mkiss *ax)
{
	if (refcount_dec_and_test(&ax->refcnt))
		complete(&ax->dead);
}

static int crc_force = 0;	/* Can be overridden with insmod */

static int mkiss_open(struct tty_struct *tty)
{
	struct net_device *dev;
	struct mkiss *ax;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	dev = alloc_netdev(sizeof(struct mkiss), "ax%d", NET_NAME_UNKNOWN,
			   ax_setup);
	if (!dev) {
		err = -ENOMEM;
		goto out;
	}

	ax = netdev_priv(dev);
	ax->dev = dev;

	spin_lock_init(&ax->buflock);
	refcount_set(&ax->refcnt, 1);
	init_completion(&ax->dead);

	ax->tty = tty;
	tty->disc_data = ax;
	tty->receive_room = 65535;

	tty_driver_flush_buffer(tty);

	/* Restore default settings */
	dev->type = ARPHRD_AX25;

	/* Perform the low-level AX25 initialization. */
	err = ax_open(ax->dev);
	if (err)
		goto out_free_netdev;

	err = register_netdev(dev);
	if (err)
		goto out_free_buffers;

	/* after register_netdev() - because else printk smashes the kernel */
	switch (crc_force) {
	case 3:
		ax->crcmode  = CRC_MODE_SMACK;
		printk(KERN_INFO "mkiss: %s: crc mode smack forced.\n",
		       ax->dev->name);
		break;
	case 2:
		ax->crcmode  = CRC_MODE_FLEX;
		printk(KERN_INFO "mkiss: %s: crc mode flexnet forced.\n",
		       ax->dev->name);
		break;
	case 1:
		ax->crcmode  = CRC_MODE_NONE;
		printk(KERN_INFO "mkiss: %s: crc mode disabled.\n",
		       ax->dev->name);
		break;
	case 0:
	default:
		crc_force = 0;
		printk(KERN_INFO "mkiss: %s: crc mode is auto.\n",
		       ax->dev->name);
		ax->crcmode  = CRC_MODE_SMACK_TEST;
	}
	ax->crcauto = (crc_force ? 0 : 1);

	netif_start_queue(dev);

	/* Done.  We have linked the TTY line to a channel. */
	return 0;

out_free_buffers:
	kfree(ax->rbuff);
	kfree(ax->xbuff);

out_free_netdev:
	free_netdev(dev);

out:
	return err;
}

static void mkiss_close(struct tty_struct *tty)
{
	struct mkiss *ax;

	write_lock_irq(&disc_data_lock);
	ax = tty->disc_data;
	tty->disc_data = NULL;
	write_unlock_irq(&disc_data_lock);

	if (!ax)
		return;

	/*
	 * We have now ensured that nobody can start using ap from now on, but
	 * we have to wait for all existing users to finish.
	 */
	if (!refcount_dec_and_test(&ax->refcnt))
		wait_for_completion(&ax->dead);
	/*
	 * Halt the transmit queue so that a new transmit cannot scribble
	 * on our buffers
	 */
	netif_stop_queue(ax->dev);

	/* Free all AX25 frame buffers. */
	kfree(ax->rbuff);
	kfree(ax->xbuff);

	ax->tty = NULL;

	unregister_netdev(ax->dev);
	free_netdev(ax->dev);
}

/* Perform I/O control on an active ax25 channel. */
static int mkiss_ioctl(struct tty_struct *tty, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct mkiss *ax = mkiss_get(tty);
	struct net_device *dev;
	unsigned int tmp, err;

	/* First make sure we're connected. */
	if (ax == NULL)
		return -ENXIO;
	dev = ax->dev;

	switch (cmd) {
 	case SIOCGIFNAME:
		err = copy_to_user((void __user *) arg, ax->dev->name,
		                   strlen(ax->dev->name) + 1) ? -EFAULT : 0;
		break;

	case SIOCGIFENCAP:
		err = put_user(4, (int __user *) arg);
		break;

	case SIOCSIFENCAP:
		if (get_user(tmp, (int __user *) arg)) {
			err = -EFAULT;
			break;
		}

		ax->mode = tmp;
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
		memcpy(dev->dev_addr, addr, AX25_ADDR_LEN);
		netif_tx_unlock_bh(dev);

		err = 0;
		break;
	}
	default:
		err = -ENOIOCTLCMD;
	}

	mkiss_put(ax);

	return err;
}

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of data has been received, which can now be decapsulated
 * and sent on to the AX.25 layer for further processing.
 */
static void mkiss_receive_buf(struct tty_struct *tty, const unsigned char *cp,
	char *fp, int count)
{
	struct mkiss *ax = mkiss_get(tty);

	if (!ax)
		return;

	/*
	 * Argh! mtu change time! - costs us the packet part received
	 * at the change
	 */
	if (ax->mtu != ax->dev->mtu + 73)
		ax_changedmtu(ax);

	/* Read the characters out of the buffer */
	while (count--) {
		if (fp != NULL && *fp++) {
			if (!test_and_set_bit(AXF_ERROR, &ax->flags))
				ax->dev->stats.rx_errors++;
			cp++;
			continue;
		}

		kiss_unesc(ax, *cp++);
	}

	mkiss_put(ax);
	tty_unthrottle(tty);
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */
static void mkiss_write_wakeup(struct tty_struct *tty)
{
	struct mkiss *ax = mkiss_get(tty);
	int actual;

	if (!ax)
		return;

	if (ax->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet
		 */
		clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

		netif_wake_queue(ax->dev);
		goto out;
	}

	actual = tty->ops->write(tty, ax->xhead, ax->xleft);
	ax->xleft -= actual;
	ax->xhead += actual;

out:
	mkiss_put(ax);
}

static struct tty_ldisc_ops ax_ldisc = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= "mkiss",
	.open		= mkiss_open,
	.close		= mkiss_close,
	.ioctl		= mkiss_ioctl,
	.receive_buf	= mkiss_receive_buf,
	.write_wakeup	= mkiss_write_wakeup
};

static const char banner[] __initconst = KERN_INFO \
	"mkiss: AX.25 Multikiss, Hans Albas PE1AYX\n";
static const char msg_regfail[] __initconst = KERN_ERR \
	"mkiss: can't register line discipline (err = %d)\n";

static int __init mkiss_init_driver(void)
{
	int status;

	printk(banner);

	status = tty_register_ldisc(N_AX25, &ax_ldisc);
	if (status != 0)
		printk(msg_regfail, status);

	return status;
}

static const char msg_unregfail[] = KERN_ERR \
	"mkiss: can't unregister line discipline (err = %d)\n";

static void __exit mkiss_exit_driver(void)
{
	int ret;

	if ((ret = tty_unregister_ldisc(N_AX25)))
		printk(msg_unregfail, ret);
}

MODULE_AUTHOR("Ralf Baechle DL5RB <ralf@linux-mips.org>");
MODULE_DESCRIPTION("KISS driver for AX.25 over TTYs");
module_param(crc_force, int, 0);
MODULE_PARM_DESC(crc_force, "crc [0 = auto | 1 = none | 2 = flexnet | 3 = smack]");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_AX25);

module_init(mkiss_init_driver);
module_exit(mkiss_exit_driver);

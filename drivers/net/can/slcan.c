/*
 * slcan.c - serial line CAN interface driver (using tty line discipline)
 *
 * This file is derived from linux/drivers/net/slip/slip.c
 *
 * slip.c Authors  : Laurence Culhane <loz@holmes.demon.co.uk>
 *                   Fred N. van Kempen <waltje@uwalt.nl.mugnet.org>
 * slcan.c Author  : Oliver Hartkopp <socketcan@hartkopp.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see http://www.gnu.org/licenses/gpl.html
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/can.h>
#include <linux/can/skb.h>

static __initconst const char banner[] =
	KERN_INFO "slcan: serial line CAN interface driver\n";

MODULE_ALIAS_LDISC(N_SLCAN);
MODULE_DESCRIPTION("serial line CAN interface");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oliver Hartkopp <socketcan@hartkopp.net>");

#define SLCAN_MAGIC 0x53CA

static int maxdev = 10;		/* MAX number of SLCAN channels;
				   This can be overridden with
				   insmod slcan.ko maxdev=nnn	*/
module_param(maxdev, int, 0);
MODULE_PARM_DESC(maxdev, "Maximum number of slcan interfaces");

/* maximum rx buffer len: extended CAN frame with timestamp */
#define SLC_MTU (sizeof("T1111222281122334455667788EA5F\r")+1)

#define SLC_CMD_LEN 1
#define SLC_SFF_ID_LEN 3
#define SLC_EFF_ID_LEN 8

struct slcan {
	int			magic;

	/* Various fields. */
	struct tty_struct	*tty;		/* ptr to TTY structure	     */
	struct net_device	*dev;		/* easy for intr handling    */
	spinlock_t		lock;
	struct work_struct	tx_work;	/* Flushes transmit buffer   */

	/* These are pointers to the malloc()ed frame buffers. */
	unsigned char		rbuff[SLC_MTU];	/* receiver buffer	     */
	int			rcount;         /* received chars counter    */
	unsigned char		xbuff[SLC_MTU];	/* transmitter buffer	     */
	unsigned char		*xhead;         /* pointer to next XMIT byte */
	int			xleft;          /* bytes left in XMIT queue  */

	unsigned long		flags;		/* Flag values/ mode etc     */
#define SLF_INUSE		0		/* Channel in use            */
#define SLF_ERROR		1               /* Parity, etc. error        */
};

static struct net_device **slcan_devs;

 /************************************************************************
  *			SLCAN ENCAPSULATION FORMAT			 *
  ************************************************************************/

/*
 * A CAN frame has a can_id (11 bit standard frame format OR 29 bit extended
 * frame format) a data length code (can_dlc) which can be from 0 to 8
 * and up to <can_dlc> data bytes as payload.
 * Additionally a CAN frame may become a remote transmission frame if the
 * RTR-bit is set. This causes another ECU to send a CAN frame with the
 * given can_id.
 *
 * The SLCAN ASCII representation of these different frame types is:
 * <type> <id> <dlc> <data>*
 *
 * Extended frames (29 bit) are defined by capital characters in the type.
 * RTR frames are defined as 'r' types - normal frames have 't' type:
 * t => 11 bit data frame
 * r => 11 bit RTR frame
 * T => 29 bit data frame
 * R => 29 bit RTR frame
 *
 * The <id> is 3 (standard) or 8 (extended) bytes in ASCII Hex (base64).
 * The <dlc> is a one byte ASCII number ('0' - '8')
 * The <data> section has at much ASCII Hex bytes as defined by the <dlc>
 *
 * Examples:
 *
 * t1230 : can_id 0x123, can_dlc 0, no data
 * t4563112233 : can_id 0x456, can_dlc 3, data 0x11 0x22 0x33
 * T12ABCDEF2AA55 : extended can_id 0x12ABCDEF, can_dlc 2, data 0xAA 0x55
 * r1230 : can_id 0x123, can_dlc 0, no data, remote transmission request
 *
 */

 /************************************************************************
  *			STANDARD SLCAN DECAPSULATION			 *
  ************************************************************************/

/* Send one completely decapsulated can_frame to the network layer */
static void slc_bump(struct slcan *sl)
{
	struct sk_buff *skb;
	struct can_frame cf;
	int i, tmp;
	u32 tmpid;
	char *cmd = sl->rbuff;

	cf.can_id = 0;

	switch (*cmd) {
	case 'r':
		cf.can_id = CAN_RTR_FLAG;
		/* fallthrough */
	case 't':
		/* store dlc ASCII value and terminate SFF CAN ID string */
		cf.can_dlc = sl->rbuff[SLC_CMD_LEN + SLC_SFF_ID_LEN];
		sl->rbuff[SLC_CMD_LEN + SLC_SFF_ID_LEN] = 0;
		/* point to payload data behind the dlc */
		cmd += SLC_CMD_LEN + SLC_SFF_ID_LEN + 1;
		break;
	case 'R':
		cf.can_id = CAN_RTR_FLAG;
		/* fallthrough */
	case 'T':
		cf.can_id |= CAN_EFF_FLAG;
		/* store dlc ASCII value and terminate EFF CAN ID string */
		cf.can_dlc = sl->rbuff[SLC_CMD_LEN + SLC_EFF_ID_LEN];
		sl->rbuff[SLC_CMD_LEN + SLC_EFF_ID_LEN] = 0;
		/* point to payload data behind the dlc */
		cmd += SLC_CMD_LEN + SLC_EFF_ID_LEN + 1;
		break;
	default:
		return;
	}

	if (kstrtou32(sl->rbuff + SLC_CMD_LEN, 16, &tmpid))
		return;

	cf.can_id |= tmpid;

	/* get can_dlc from sanitized ASCII value */
	if (cf.can_dlc >= '0' && cf.can_dlc < '9')
		cf.can_dlc -= '0';
	else
		return;

	*(u64 *) (&cf.data) = 0; /* clear payload */

	/* RTR frames may have a dlc > 0 but they never have any data bytes */
	if (!(cf.can_id & CAN_RTR_FLAG)) {
		for (i = 0; i < cf.can_dlc; i++) {
			tmp = hex_to_bin(*cmd++);
			if (tmp < 0)
				return;
			cf.data[i] = (tmp << 4);
			tmp = hex_to_bin(*cmd++);
			if (tmp < 0)
				return;
			cf.data[i] |= tmp;
		}
	}

	skb = dev_alloc_skb(sizeof(struct can_frame) +
			    sizeof(struct can_skb_priv));
	if (!skb)
		return;

	skb->dev = sl->dev;
	skb->protocol = htons(ETH_P_CAN);
	skb->pkt_type = PACKET_BROADCAST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = sl->dev->ifindex;

	memcpy(skb_put(skb, sizeof(struct can_frame)),
	       &cf, sizeof(struct can_frame));
	netif_rx_ni(skb);

	sl->dev->stats.rx_packets++;
	sl->dev->stats.rx_bytes += cf.can_dlc;
}

/* parse tty input stream */
static void slcan_unesc(struct slcan *sl, unsigned char s)
{
	if ((s == '\r') || (s == '\a')) { /* CR or BEL ends the pdu */
		if (!test_and_clear_bit(SLF_ERROR, &sl->flags) &&
		    (sl->rcount > 4))  {
			slc_bump(sl);
		}
		sl->rcount = 0;
	} else {
		if (!test_bit(SLF_ERROR, &sl->flags))  {
			if (sl->rcount < SLC_MTU)  {
				sl->rbuff[sl->rcount++] = s;
				return;
			} else {
				sl->dev->stats.rx_over_errors++;
				set_bit(SLF_ERROR, &sl->flags);
			}
		}
	}
}

 /************************************************************************
  *			STANDARD SLCAN ENCAPSULATION			 *
  ************************************************************************/

/* Encapsulate one can_frame and stuff into a TTY queue. */
static void slc_encaps(struct slcan *sl, struct can_frame *cf)
{
	int actual, i;
	unsigned char *pos;
	unsigned char *endpos;
	canid_t id = cf->can_id;

	pos = sl->xbuff;

	if (cf->can_id & CAN_RTR_FLAG)
		*pos = 'R'; /* becomes 'r' in standard frame format (SFF) */
	else
		*pos = 'T'; /* becomes 't' in standard frame format (SSF) */

	/* determine number of chars for the CAN-identifier */
	if (cf->can_id & CAN_EFF_FLAG) {
		id &= CAN_EFF_MASK;
		endpos = pos + SLC_EFF_ID_LEN;
	} else {
		*pos |= 0x20; /* convert R/T to lower case for SFF */
		id &= CAN_SFF_MASK;
		endpos = pos + SLC_SFF_ID_LEN;
	}

	/* build 3 (SFF) or 8 (EFF) digit CAN identifier */
	pos++;
	while (endpos >= pos) {
		*endpos-- = hex_asc_upper[id & 0xf];
		id >>= 4;
	}

	pos += (cf->can_id & CAN_EFF_FLAG) ? SLC_EFF_ID_LEN : SLC_SFF_ID_LEN;

	*pos++ = cf->can_dlc + '0';

	/* RTR frames may have a dlc > 0 but they never have any data bytes */
	if (!(cf->can_id & CAN_RTR_FLAG)) {
		for (i = 0; i < cf->can_dlc; i++)
			pos = hex_byte_pack_upper(pos, cf->data[i]);
	}

	*pos++ = '\r';

	/* Order of next two lines is *very* important.
	 * When we are sending a little amount of data,
	 * the transfer may be completed inside the ops->write()
	 * routine, because it's running with interrupts enabled.
	 * In this case we *never* got WRITE_WAKEUP event,
	 * if we did not request it before write operation.
	 *       14 Oct 1994  Dmitry Gorodchanin.
	 */
	set_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	actual = sl->tty->ops->write(sl->tty, sl->xbuff, pos - sl->xbuff);
	sl->xleft = (pos - sl->xbuff) - actual;
	sl->xhead = sl->xbuff + actual;
	sl->dev->stats.tx_bytes += cf->can_dlc;
}

/* Write out any remaining transmit buffer. Scheduled when tty is writable */
static void slcan_transmit(struct work_struct *work)
{
	struct slcan *sl = container_of(work, struct slcan, tx_work);
	int actual;

	spin_lock_bh(&sl->lock);
	/* First make sure we're connected. */
	if (!sl->tty || sl->magic != SLCAN_MAGIC || !netif_running(sl->dev)) {
		spin_unlock_bh(&sl->lock);
		return;
	}

	if (sl->xleft <= 0)  {
		/* Now serial buffer is almost free & we can start
		 * transmission of another packet */
		sl->dev->stats.tx_packets++;
		clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
		spin_unlock_bh(&sl->lock);
		netif_wake_queue(sl->dev);
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
static void slcan_write_wakeup(struct tty_struct *tty)
{
	struct slcan *sl = tty->disc_data;

	schedule_work(&sl->tx_work);
}

/* Send a can_frame to a TTY queue. */
static netdev_tx_t slc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct slcan *sl = netdev_priv(dev);

	if (skb->len != sizeof(struct can_frame))
		goto out;

	spin_lock(&sl->lock);
	if (!netif_running(dev))  {
		spin_unlock(&sl->lock);
		printk(KERN_WARNING "%s: xmit: iface is down\n", dev->name);
		goto out;
	}
	if (sl->tty == NULL) {
		spin_unlock(&sl->lock);
		goto out;
	}

	netif_stop_queue(sl->dev);
	slc_encaps(sl, (struct can_frame *) skb->data); /* encaps & send */
	spin_unlock(&sl->lock);

out:
	kfree_skb(skb);
	return NETDEV_TX_OK;
}


/******************************************
 *   Routines looking at netdevice side.
 ******************************************/

/* Netdevice UP -> DOWN routine */
static int slc_close(struct net_device *dev)
{
	struct slcan *sl = netdev_priv(dev);

	spin_lock_bh(&sl->lock);
	if (sl->tty) {
		/* TTY discipline is running. */
		clear_bit(TTY_DO_WRITE_WAKEUP, &sl->tty->flags);
	}
	netif_stop_queue(dev);
	sl->rcount   = 0;
	sl->xleft    = 0;
	spin_unlock_bh(&sl->lock);

	return 0;
}

/* Netdevice DOWN -> UP routine */
static int slc_open(struct net_device *dev)
{
	struct slcan *sl = netdev_priv(dev);

	if (sl->tty == NULL)
		return -ENODEV;

	sl->flags &= (1 << SLF_INUSE);
	netif_start_queue(dev);
	return 0;
}

/* Hook the destructor so we can free slcan devs at the right point in time */
static void slc_free_netdev(struct net_device *dev)
{
	int i = dev->base_addr;
	free_netdev(dev);
	slcan_devs[i] = NULL;
}

static int slcan_change_mtu(struct net_device *dev, int new_mtu)
{
	return -EINVAL;
}

static const struct net_device_ops slc_netdev_ops = {
	.ndo_open               = slc_open,
	.ndo_stop               = slc_close,
	.ndo_start_xmit         = slc_xmit,
	.ndo_change_mtu         = slcan_change_mtu,
};

static void slc_setup(struct net_device *dev)
{
	dev->netdev_ops		= &slc_netdev_ops;
	dev->destructor		= slc_free_netdev;

	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->tx_queue_len	= 10;

	dev->mtu		= sizeof(struct can_frame);
	dev->type		= ARPHRD_CAN;

	/* New-style flags. */
	dev->flags		= IFF_NOARP;
	dev->features           = NETIF_F_HW_CSUM;
}

/******************************************
  Routines looking at TTY side.
 ******************************************/

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of SLCAN data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing. This will not
 * be re-entered while running but other ldisc functions may be called
 * in parallel
 */

static void slcan_receive_buf(struct tty_struct *tty,
			      const unsigned char *cp, char *fp, int count)
{
	struct slcan *sl = (struct slcan *) tty->disc_data;

	if (!sl || sl->magic != SLCAN_MAGIC || !netif_running(sl->dev))
		return;

	/* Read the characters out of the buffer */
	while (count--) {
		if (fp && *fp++) {
			if (!test_and_set_bit(SLF_ERROR, &sl->flags))
				sl->dev->stats.rx_errors++;
			cp++;
			continue;
		}
		slcan_unesc(sl, *cp++);
	}
}

/************************************
 *  slcan_open helper routines.
 ************************************/

/* Collect hanged up channels */
static void slc_sync(void)
{
	int i;
	struct net_device *dev;
	struct slcan	  *sl;

	for (i = 0; i < maxdev; i++) {
		dev = slcan_devs[i];
		if (dev == NULL)
			break;

		sl = netdev_priv(dev);
		if (sl->tty)
			continue;
		if (dev->flags & IFF_UP)
			dev_close(dev);
	}
}

/* Find a free SLCAN channel, and link in this `tty' line. */
static struct slcan *slc_alloc(dev_t line)
{
	int i;
	char name[IFNAMSIZ];
	struct net_device *dev = NULL;
	struct slcan       *sl;

	for (i = 0; i < maxdev; i++) {
		dev = slcan_devs[i];
		if (dev == NULL)
			break;

	}

	/* Sorry, too many, all slots in use */
	if (i >= maxdev)
		return NULL;

	sprintf(name, "slcan%d", i);
	dev = alloc_netdev(sizeof(*sl), name, slc_setup);
	if (!dev)
		return NULL;

	dev->base_addr  = i;
	sl = netdev_priv(dev);

	/* Initialize channel control data */
	sl->magic = SLCAN_MAGIC;
	sl->dev	= dev;
	spin_lock_init(&sl->lock);
	INIT_WORK(&sl->tx_work, slcan_transmit);
	slcan_devs[i] = dev;

	return sl;
}

/*
 * Open the high-level part of the SLCAN channel.
 * This function is called by the TTY module when the
 * SLCAN line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free SLCAN channel...
 *
 * Called in process context serialized from other ldisc calls.
 */

static int slcan_open(struct tty_struct *tty)
{
	struct slcan *sl;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	/* RTnetlink lock is misused here to serialize concurrent
	   opens of slcan channels. There are better ways, but it is
	   the simplest one.
	 */
	rtnl_lock();

	/* Collect hanged up channels. */
	slc_sync();

	sl = tty->disc_data;

	err = -EEXIST;
	/* First make sure we're not already connected. */
	if (sl && sl->magic == SLCAN_MAGIC)
		goto err_exit;

	/* OK.  Find a free SLCAN channel to use. */
	err = -ENFILE;
	sl = slc_alloc(tty_devnum(tty));
	if (sl == NULL)
		goto err_exit;

	sl->tty = tty;
	tty->disc_data = sl;

	if (!test_bit(SLF_INUSE, &sl->flags)) {
		/* Perform the low-level SLCAN initialization. */
		sl->rcount   = 0;
		sl->xleft    = 0;

		set_bit(SLF_INUSE, &sl->flags);

		err = register_netdevice(sl->dev);
		if (err)
			goto err_free_chan;
	}

	/* Done.  We have linked the TTY line to a channel. */
	rtnl_unlock();
	tty->receive_room = 65536;	/* We don't flow control */

	/* TTY layer expects 0 on success */
	return 0;

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
 * Close down a SLCAN channel.
 * This means flushing out any pending queues, and then returning. This
 * call is serialized against other ldisc functions.
 *
 * We also use this method for a hangup event.
 */

static void slcan_close(struct tty_struct *tty)
{
	struct slcan *sl = (struct slcan *) tty->disc_data;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLCAN_MAGIC || sl->tty != tty)
		return;

	spin_lock_bh(&sl->lock);
	tty->disc_data = NULL;
	sl->tty = NULL;
	spin_unlock_bh(&sl->lock);

	flush_work(&sl->tx_work);

	/* Flush network side */
	unregister_netdev(sl->dev);
	/* This will complete via sl_free_netdev */
}

static int slcan_hangup(struct tty_struct *tty)
{
	slcan_close(tty);
	return 0;
}

/* Perform I/O control on an active SLCAN channel. */
static int slcan_ioctl(struct tty_struct *tty, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct slcan *sl = (struct slcan *) tty->disc_data;
	unsigned int tmp;

	/* First make sure we're connected. */
	if (!sl || sl->magic != SLCAN_MAGIC)
		return -EINVAL;

	switch (cmd) {
	case SIOCGIFNAME:
		tmp = strlen(sl->dev->name) + 1;
		if (copy_to_user((void __user *)arg, sl->dev->name, tmp))
			return -EFAULT;
		return 0;

	case SIOCSIFHWADDR:
		return -EINVAL;

	default:
		return tty_mode_ioctl(tty, file, cmd, arg);
	}
}

static struct tty_ldisc_ops slc_ldisc = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= "slcan",
	.open		= slcan_open,
	.close		= slcan_close,
	.hangup		= slcan_hangup,
	.ioctl		= slcan_ioctl,
	.receive_buf	= slcan_receive_buf,
	.write_wakeup	= slcan_write_wakeup,
};

static int __init slcan_init(void)
{
	int status;

	if (maxdev < 4)
		maxdev = 4; /* Sanity */

	printk(banner);
	printk(KERN_INFO "slcan: %d dynamic interface channels.\n", maxdev);

	slcan_devs = kzalloc(sizeof(struct net_device *)*maxdev, GFP_KERNEL);
	if (!slcan_devs)
		return -ENOMEM;

	/* Fill in our line protocol discipline, and register it */
	status = tty_register_ldisc(N_SLCAN, &slc_ldisc);
	if (status)  {
		printk(KERN_ERR "slcan: can't register line discipline\n");
		kfree(slcan_devs);
	}
	return status;
}

static void __exit slcan_exit(void)
{
	int i;
	struct net_device *dev;
	struct slcan *sl;
	unsigned long timeout = jiffies + HZ;
	int busy = 0;

	if (slcan_devs == NULL)
		return;

	/* First of all: check for active disciplines and hangup them.
	 */
	do {
		if (busy)
			msleep_interruptible(100);

		busy = 0;
		for (i = 0; i < maxdev; i++) {
			dev = slcan_devs[i];
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

	for (i = 0; i < maxdev; i++) {
		dev = slcan_devs[i];
		if (!dev)
			continue;
		slcan_devs[i] = NULL;

		sl = netdev_priv(dev);
		if (sl->tty) {
			printk(KERN_ERR "%s: tty discipline still running\n",
			       dev->name);
			/* Intentionally leak the control block. */
			dev->destructor = NULL;
		}

		unregister_netdev(dev);
	}

	kfree(slcan_devs);
	slcan_devs = NULL;

	i = tty_unregister_ldisc(N_SLCAN);
	if (i)
		printk(KERN_ERR "slcan: can't unregister ldisc (err %d)\n", i);
}

module_init(slcan_init);
module_exit(slcan_exit);

/*
 *  TUN - Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2002 Maxim Krasnyansky <maxk@qualcomm.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  $Id: tun.c,v 1.15 2002/03/01 02:44:24 maxk Exp $
 */

/*
 *  Changes:
 *
 *  Brian Braunstein <linuxkernel@bristyle.com> 2007/03/23
 *    Fixed hw address handling.  Now net_device.dev_addr is kept consistent
 *    with tun.dev_addr when the address is set by this module.
 *
 *  Mike Kershaw <dragorn@kismetwireless.net> 2005/08/14
 *    Add TUNSETLINK ioctl to set the link encapsulation
 *
 *  Mark Smith <markzzzsmith@yahoo.com.au>
 *   Use random_ether_addr() for tap MAC address.
 *
 *  Harald Roelle <harald.roelle@ifi.lmu.de>  2004/04/20
 *    Fixes in packet dropping, queue length setting and queue wakeup.
 *    Increased default tx queue length.
 *    Added ethtool API.
 *    Minor cleanups
 *
 *  Daniel Podlejski <underley@underley.eu.org>
 *    Modifications for 2.3.99-pre5 kernel.
 */

#define DRV_NAME	"tun"
#define DRV_VERSION	"1.6"
#define DRV_DESCRIPTION	"Universal TUN/TAP device driver"
#define DRV_COPYRIGHT	"(C) 1999-2004 Max Krasnyansky <maxk@qualcomm.com>"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/miscdevice.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/crc32.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include <asm/system.h>
#include <asm/uaccess.h>

/* Uncomment to enable debugging */
/* #define TUN_DEBUG 1 */

#ifdef TUN_DEBUG
static int debug;

#define DBG  if(tun->debug)printk
#define DBG1 if(debug==2)printk
#else
#define DBG( a... )
#define DBG1( a... )
#endif

struct tun_struct {
	struct list_head        list;
	unsigned long 		flags;
	int			attached;
	uid_t			owner;
	gid_t			group;

	wait_queue_head_t	read_wait;
	struct sk_buff_head	readq;

	struct net_device	*dev;

	struct fasync_struct    *fasync;

	unsigned long if_flags;
	u8 dev_addr[ETH_ALEN];
	u32 chr_filter[2];
	u32 net_filter[2];

#ifdef TUN_DEBUG
	int debug;
#endif
};

/* Network device part of the driver */

static unsigned int tun_net_id;
struct tun_net {
	struct list_head dev_list;
};

static const struct ethtool_ops tun_ethtool_ops;

/* Net device open. */
static int tun_net_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

/* Net device close. */
static int tun_net_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

/* Net device start xmit */
static int tun_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	DBG(KERN_INFO "%s: tun_net_xmit %d\n", tun->dev->name, skb->len);

	/* Drop packet if interface is not attached */
	if (!tun->attached)
		goto drop;

	/* Packet dropping */
	if (skb_queue_len(&tun->readq) >= dev->tx_queue_len) {
		if (!(tun->flags & TUN_ONE_QUEUE)) {
			/* Normal queueing mode. */
			/* Packet scheduler handles dropping of further packets. */
			netif_stop_queue(dev);

			/* We won't see all dropped packets individually, so overrun
			 * error is more appropriate. */
			dev->stats.tx_fifo_errors++;
		} else {
			/* Single queue mode.
			 * Driver handles dropping of all packets itself. */
			goto drop;
		}
	}

	/* Queue packet */
	skb_queue_tail(&tun->readq, skb);
	dev->trans_start = jiffies;

	/* Notify and wake up reader process */
	if (tun->flags & TUN_FASYNC)
		kill_fasync(&tun->fasync, SIGIO, POLL_IN);
	wake_up_interruptible(&tun->read_wait);
	return 0;

drop:
	dev->stats.tx_dropped++;
	kfree_skb(skb);
	return 0;
}

/** Add the specified Ethernet address to this multicast filter. */
static void
add_multi(u32* filter, const u8* addr)
{
	int bit_nr = ether_crc(ETH_ALEN, addr) >> 26;
	filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
}

/** Remove the specified Ethernet addres from this multicast filter. */
static void
del_multi(u32* filter, const u8* addr)
{
	int bit_nr = ether_crc(ETH_ALEN, addr) >> 26;
	filter[bit_nr >> 5] &= ~(1 << (bit_nr & 31));
}

/** Update the list of multicast groups to which the network device belongs.
 * This list is used to filter packets being sent from the character device to
 * the network device. */
static void
tun_net_mclist(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	const struct dev_mc_list *mclist;
	int i;
	DECLARE_MAC_BUF(mac);
	DBG(KERN_DEBUG "%s: tun_net_mclist: mc_count %d\n",
			dev->name, dev->mc_count);
	memset(tun->chr_filter, 0, sizeof tun->chr_filter);
	for (i = 0, mclist = dev->mc_list; i < dev->mc_count && mclist != NULL;
			i++, mclist = mclist->next) {
		add_multi(tun->net_filter, mclist->dmi_addr);
		DBG(KERN_DEBUG "%s: tun_net_mclist: %s\n",
		    dev->name, print_mac(mac, mclist->dmi_addr));
	}
}

#define MIN_MTU 68
#define MAX_MTU 65535

static int
tun_net_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < MIN_MTU || new_mtu + dev->hard_header_len > MAX_MTU)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

/* Initialize net device. */
static void tun_net_init(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	switch (tun->flags & TUN_TYPE_MASK) {
	case TUN_TUN_DEV:
		/* Point-to-Point TUN Device */
		dev->hard_header_len = 0;
		dev->addr_len = 0;
		dev->mtu = 1500;
		dev->change_mtu = tun_net_change_mtu;

		/* Zero header length */
		dev->type = ARPHRD_NONE;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
		dev->tx_queue_len = TUN_READQ_SIZE;  /* We prefer our own queue length */
		break;

	case TUN_TAP_DEV:
		/* Ethernet TAP Device */
		dev->set_multicast_list = tun_net_mclist;

		ether_setup(dev);
		dev->change_mtu = tun_net_change_mtu;

		/* random address already created for us by tun_set_iff, use it */
		memcpy(dev->dev_addr, tun->dev_addr, min(sizeof(tun->dev_addr), sizeof(dev->dev_addr)) );

		dev->tx_queue_len = TUN_READQ_SIZE;  /* We prefer our own queue length */
		break;
	}
}

/* Character device part */

/* Poll */
static unsigned int tun_chr_poll(struct file *file, poll_table * wait)
{
	struct tun_struct *tun = file->private_data;
	unsigned int mask = POLLOUT | POLLWRNORM;

	if (!tun)
		return -EBADFD;

	DBG(KERN_INFO "%s: tun_chr_poll\n", tun->dev->name);

	poll_wait(file, &tun->read_wait, wait);

	if (!skb_queue_empty(&tun->readq))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

/* Get packet from user space buffer */
static __inline__ ssize_t tun_get_user(struct tun_struct *tun, struct iovec *iv, size_t count)
{
	struct tun_pi pi = { 0, __constant_htons(ETH_P_IP) };
	struct sk_buff *skb;
	size_t len = count, align = 0;

	if (!(tun->flags & TUN_NO_PI)) {
		if ((len -= sizeof(pi)) > count)
			return -EINVAL;

		if(memcpy_fromiovec((void *)&pi, iv, sizeof(pi)))
			return -EFAULT;
	}

	if ((tun->flags & TUN_TYPE_MASK) == TUN_TAP_DEV) {
		align = NET_IP_ALIGN;
		if (unlikely(len < ETH_HLEN))
			return -EINVAL;
	}

	if (!(skb = alloc_skb(len + align, GFP_KERNEL))) {
		tun->dev->stats.rx_dropped++;
		return -ENOMEM;
	}

	if (align)
		skb_reserve(skb, align);
	if (memcpy_fromiovec(skb_put(skb, len), iv, len)) {
		tun->dev->stats.rx_dropped++;
		kfree_skb(skb);
		return -EFAULT;
	}

	switch (tun->flags & TUN_TYPE_MASK) {
	case TUN_TUN_DEV:
		skb_reset_mac_header(skb);
		skb->protocol = pi.proto;
		skb->dev = tun->dev;
		break;
	case TUN_TAP_DEV:
		skb->protocol = eth_type_trans(skb, tun->dev);
		break;
	};

	if (tun->flags & TUN_NOCHECKSUM)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	netif_rx_ni(skb);
	tun->dev->last_rx = jiffies;

	tun->dev->stats.rx_packets++;
	tun->dev->stats.rx_bytes += len;

	return count;
}

static ssize_t tun_chr_aio_write(struct kiocb *iocb, const struct iovec *iv,
			      unsigned long count, loff_t pos)
{
	struct tun_struct *tun = iocb->ki_filp->private_data;

	if (!tun)
		return -EBADFD;

	DBG(KERN_INFO "%s: tun_chr_write %ld\n", tun->dev->name, count);

	return tun_get_user(tun, (struct iovec *) iv, iov_length(iv, count));
}

/* Put packet to the user space buffer */
static __inline__ ssize_t tun_put_user(struct tun_struct *tun,
				       struct sk_buff *skb,
				       struct iovec *iv, int len)
{
	struct tun_pi pi = { 0, skb->protocol };
	ssize_t total = 0;

	if (!(tun->flags & TUN_NO_PI)) {
		if ((len -= sizeof(pi)) < 0)
			return -EINVAL;

		if (len < skb->len) {
			/* Packet will be striped */
			pi.flags |= TUN_PKT_STRIP;
		}

		if (memcpy_toiovec(iv, (void *) &pi, sizeof(pi)))
			return -EFAULT;
		total += sizeof(pi);
	}

	len = min_t(int, skb->len, len);

	skb_copy_datagram_iovec(skb, 0, iv, len);
	total += len;

	tun->dev->stats.tx_packets++;
	tun->dev->stats.tx_bytes += len;

	return total;
}

static ssize_t tun_chr_aio_read(struct kiocb *iocb, const struct iovec *iv,
			    unsigned long count, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct tun_struct *tun = file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	ssize_t len, ret = 0;
	DECLARE_MAC_BUF(mac);

	if (!tun)
		return -EBADFD;

	DBG(KERN_INFO "%s: tun_chr_read\n", tun->dev->name);

	len = iov_length(iv, count);
	if (len < 0)
		return -EINVAL;

	add_wait_queue(&tun->read_wait, &wait);
	while (len) {
		const u8 ones[ ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
		u8 addr[ ETH_ALEN];
		int bit_nr;

		current->state = TASK_INTERRUPTIBLE;

		/* Read frames from the queue */
		if (!(skb=skb_dequeue(&tun->readq))) {
			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			/* Nothing to read, let's sleep */
			schedule();
			continue;
		}
		netif_wake_queue(tun->dev);

		/** Decide whether to accept this packet. This code is designed to
		 * behave identically to an Ethernet interface. Accept the packet if
		 * - we are promiscuous.
		 * - the packet is addressed to us.
		 * - the packet is broadcast.
		 * - the packet is multicast and
		 *   - we are multicast promiscous.
		 *   - we belong to the multicast group.
		 */
		skb_copy_from_linear_data(skb, addr, min_t(size_t, sizeof addr,
								   skb->len));
		bit_nr = ether_crc(sizeof addr, addr) >> 26;
		if ((tun->if_flags & IFF_PROMISC) ||
				memcmp(addr, tun->dev_addr, sizeof addr) == 0 ||
				memcmp(addr, ones, sizeof addr) == 0 ||
				(((addr[0] == 1 && addr[1] == 0 && addr[2] == 0x5e) ||
				  (addr[0] == 0x33 && addr[1] == 0x33)) &&
				 ((tun->if_flags & IFF_ALLMULTI) ||
				  (tun->chr_filter[bit_nr >> 5] & (1 << (bit_nr & 31)))))) {
			DBG(KERN_DEBUG "%s: tun_chr_readv: accepted: %s\n",
					tun->dev->name, print_mac(mac, addr));
			ret = tun_put_user(tun, skb, (struct iovec *) iv, len);
			kfree_skb(skb);
			break;
		} else {
			DBG(KERN_DEBUG "%s: tun_chr_readv: rejected: %s\n",
					tun->dev->name, print_mac(mac, addr));
			kfree_skb(skb);
			continue;
		}
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&tun->read_wait, &wait);

	return ret;
}

static void tun_setup(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);

	skb_queue_head_init(&tun->readq);
	init_waitqueue_head(&tun->read_wait);

	tun->owner = -1;
	tun->group = -1;

	dev->open = tun_net_open;
	dev->hard_start_xmit = tun_net_xmit;
	dev->stop = tun_net_close;
	dev->ethtool_ops = &tun_ethtool_ops;
	dev->destructor = free_netdev;
	dev->features |= NETIF_F_NETNS_LOCAL;
}

static struct tun_struct *tun_get_by_name(struct tun_net *tn, const char *name)
{
	struct tun_struct *tun;

	ASSERT_RTNL();
	list_for_each_entry(tun, &tn->dev_list, list) {
		if (!strncmp(tun->dev->name, name, IFNAMSIZ))
		    return tun;
	}

	return NULL;
}

static int tun_set_iff(struct net *net, struct file *file, struct ifreq *ifr)
{
	struct tun_net *tn;
	struct tun_struct *tun;
	struct net_device *dev;
	int err;

	tn = net_generic(net, tun_net_id);
	tun = tun_get_by_name(tn, ifr->ifr_name);
	if (tun) {
		if (tun->attached)
			return -EBUSY;

		/* Check permissions */
		if (((tun->owner != -1 &&
		      current->euid != tun->owner) ||
		     (tun->group != -1 &&
		      current->egid != tun->group)) &&
		     !capable(CAP_NET_ADMIN))
			return -EPERM;
	}
	else if (__dev_get_by_name(net, ifr->ifr_name))
		return -EINVAL;
	else {
		char *name;
		unsigned long flags = 0;

		err = -EINVAL;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		/* Set dev type */
		if (ifr->ifr_flags & IFF_TUN) {
			/* TUN device */
			flags |= TUN_TUN_DEV;
			name = "tun%d";
		} else if (ifr->ifr_flags & IFF_TAP) {
			/* TAP device */
			flags |= TUN_TAP_DEV;
			name = "tap%d";
		} else
			goto failed;

		if (*ifr->ifr_name)
			name = ifr->ifr_name;

		dev = alloc_netdev(sizeof(struct tun_struct), name,
				   tun_setup);
		if (!dev)
			return -ENOMEM;

		dev_net_set(dev, net);
		tun = netdev_priv(dev);
		tun->dev = dev;
		tun->flags = flags;
		/* Be promiscuous by default to maintain previous behaviour. */
		tun->if_flags = IFF_PROMISC;
		/* Generate random Ethernet address. */
		*(__be16 *)tun->dev_addr = htons(0x00FF);
		get_random_bytes(tun->dev_addr + sizeof(u16), 4);
		memset(tun->chr_filter, 0, sizeof tun->chr_filter);

		tun_net_init(dev);

		if (strchr(dev->name, '%')) {
			err = dev_alloc_name(dev, dev->name);
			if (err < 0)
				goto err_free_dev;
		}

		err = register_netdevice(tun->dev);
		if (err < 0)
			goto err_free_dev;

		list_add(&tun->list, &tn->dev_list);
	}

	DBG(KERN_INFO "%s: tun_set_iff\n", tun->dev->name);

	if (ifr->ifr_flags & IFF_NO_PI)
		tun->flags |= TUN_NO_PI;
	else
		tun->flags &= ~TUN_NO_PI;

	if (ifr->ifr_flags & IFF_ONE_QUEUE)
		tun->flags |= TUN_ONE_QUEUE;
	else
		tun->flags &= ~TUN_ONE_QUEUE;

	file->private_data = tun;
	tun->attached = 1;
	get_net(dev_net(tun->dev));

	strcpy(ifr->ifr_name, tun->dev->name);
	return 0;

 err_free_dev:
	free_netdev(dev);
 failed:
	return err;
}

static int tun_chr_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct tun_struct *tun = file->private_data;
	void __user* argp = (void __user*)arg;
	struct ifreq ifr;
	DECLARE_MAC_BUF(mac);

	if (cmd == TUNSETIFF || _IOC_TYPE(cmd) == 0x89)
		if (copy_from_user(&ifr, argp, sizeof ifr))
			return -EFAULT;

	if (cmd == TUNSETIFF && !tun) {
		int err;

		ifr.ifr_name[IFNAMSIZ-1] = '\0';

		rtnl_lock();
		err = tun_set_iff(current->nsproxy->net_ns, file, &ifr);
		rtnl_unlock();

		if (err)
			return err;

		if (copy_to_user(argp, &ifr, sizeof(ifr)))
			return -EFAULT;
		return 0;
	}

	if (!tun)
		return -EBADFD;

	DBG(KERN_INFO "%s: tun_chr_ioctl cmd %d\n", tun->dev->name, cmd);

	switch (cmd) {
	case TUNSETNOCSUM:
		/* Disable/Enable checksum */
		if (arg)
			tun->flags |= TUN_NOCHECKSUM;
		else
			tun->flags &= ~TUN_NOCHECKSUM;

		DBG(KERN_INFO "%s: checksum %s\n",
		    tun->dev->name, arg ? "disabled" : "enabled");
		break;

	case TUNSETPERSIST:
		/* Disable/Enable persist mode */
		if (arg)
			tun->flags |= TUN_PERSIST;
		else
			tun->flags &= ~TUN_PERSIST;

		DBG(KERN_INFO "%s: persist %s\n",
		    tun->dev->name, arg ? "enabled" : "disabled");
		break;

	case TUNSETOWNER:
		/* Set owner of the device */
		tun->owner = (uid_t) arg;

		DBG(KERN_INFO "%s: owner set to %d\n", tun->dev->name, tun->owner);
		break;

	case TUNSETGROUP:
		/* Set group of the device */
		tun->group= (gid_t) arg;

		DBG(KERN_INFO "%s: group set to %d\n", tun->dev->name, tun->group);
		break;

	case TUNSETLINK:
		/* Only allow setting the type when the interface is down */
		if (tun->dev->flags & IFF_UP) {
			DBG(KERN_INFO "%s: Linktype set failed because interface is up\n",
				tun->dev->name);
			return -EBUSY;
		} else {
			tun->dev->type = (int) arg;
			DBG(KERN_INFO "%s: linktype set to %d\n", tun->dev->name, tun->dev->type);
		}
		break;

#ifdef TUN_DEBUG
	case TUNSETDEBUG:
		tun->debug = arg;
		break;
#endif

	case SIOCGIFFLAGS:
		ifr.ifr_flags = tun->if_flags;
		if (copy_to_user( argp, &ifr, sizeof ifr))
			return -EFAULT;
		return 0;

	case SIOCSIFFLAGS:
		/** Set the character device's interface flags. Currently only
		 * IFF_PROMISC and IFF_ALLMULTI are used. */
		tun->if_flags = ifr.ifr_flags;
		DBG(KERN_INFO "%s: interface flags 0x%lx\n",
				tun->dev->name, tun->if_flags);
		return 0;

	case SIOCGIFHWADDR:
		/* Note: the actual net device's address may be different */
		memcpy(ifr.ifr_hwaddr.sa_data, tun->dev_addr,
				min(sizeof ifr.ifr_hwaddr.sa_data, sizeof tun->dev_addr));
		if (copy_to_user( argp, &ifr, sizeof ifr))
			return -EFAULT;
		return 0;

	case SIOCSIFHWADDR:
	{
		/* try to set the actual net device's hw address */
		int ret;

		rtnl_lock();
		ret = dev_set_mac_address(tun->dev, &ifr.ifr_hwaddr);
		rtnl_unlock();

		if (ret == 0) {
			/** Set the character device's hardware address. This is used when
			 * filtering packets being sent from the network device to the character
			 * device. */
			memcpy(tun->dev_addr, ifr.ifr_hwaddr.sa_data,
					min(sizeof ifr.ifr_hwaddr.sa_data, sizeof tun->dev_addr));
			DBG(KERN_DEBUG "%s: set hardware address: %x:%x:%x:%x:%x:%x\n",
					tun->dev->name,
					tun->dev_addr[0], tun->dev_addr[1], tun->dev_addr[2],
					tun->dev_addr[3], tun->dev_addr[4], tun->dev_addr[5]);
		}

		return  ret;
	}

	case SIOCADDMULTI:
		/** Add the specified group to the character device's multicast filter
		 * list. */
		add_multi(tun->chr_filter, ifr.ifr_hwaddr.sa_data);
		DBG(KERN_DEBUG "%s: add multi: %s\n",
		    tun->dev->name, print_mac(mac, ifr.ifr_hwaddr.sa_data));
		return 0;

	case SIOCDELMULTI:
		/** Remove the specified group from the character device's multicast
		 * filter list. */
		del_multi(tun->chr_filter, ifr.ifr_hwaddr.sa_data);
		DBG(KERN_DEBUG "%s: del multi: %s\n",
		    tun->dev->name, print_mac(mac, ifr.ifr_hwaddr.sa_data));
		return 0;

	default:
		return -EINVAL;
	};

	return 0;
}

static int tun_chr_fasync(int fd, struct file *file, int on)
{
	struct tun_struct *tun = file->private_data;
	int ret;

	if (!tun)
		return -EBADFD;

	DBG(KERN_INFO "%s: tun_chr_fasync %d\n", tun->dev->name, on);

	if ((ret = fasync_helper(fd, file, on, &tun->fasync)) < 0)
		return ret;

	if (on) {
		ret = __f_setown(file, task_pid(current), PIDTYPE_PID, 0);
		if (ret)
			return ret;
		tun->flags |= TUN_FASYNC;
	} else
		tun->flags &= ~TUN_FASYNC;

	return 0;
}

static int tun_chr_open(struct inode *inode, struct file * file)
{
	DBG1(KERN_INFO "tunX: tun_chr_open\n");
	file->private_data = NULL;
	return 0;
}

static int tun_chr_close(struct inode *inode, struct file *file)
{
	struct tun_struct *tun = file->private_data;

	if (!tun)
		return 0;

	DBG(KERN_INFO "%s: tun_chr_close\n", tun->dev->name);

	tun_chr_fasync(-1, file, 0);

	rtnl_lock();

	/* Detach from net device */
	file->private_data = NULL;
	tun->attached = 0;
	put_net(dev_net(tun->dev));

	/* Drop read queue */
	skb_queue_purge(&tun->readq);

	if (!(tun->flags & TUN_PERSIST)) {
		list_del(&tun->list);
		unregister_netdevice(tun->dev);
	}

	rtnl_unlock();

	return 0;
}

static const struct file_operations tun_fops = {
	.owner	= THIS_MODULE,
	.llseek = no_llseek,
	.read  = do_sync_read,
	.aio_read  = tun_chr_aio_read,
	.write = do_sync_write,
	.aio_write = tun_chr_aio_write,
	.poll	= tun_chr_poll,
	.ioctl	= tun_chr_ioctl,
	.open	= tun_chr_open,
	.release = tun_chr_close,
	.fasync = tun_chr_fasync
};

static struct miscdevice tun_miscdev = {
	.minor = TUN_MINOR,
	.name = "tun",
	.fops = &tun_fops,
};

/* ethtool interface */

static int tun_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	cmd->supported		= 0;
	cmd->advertising	= 0;
	cmd->speed		= SPEED_10;
	cmd->duplex		= DUPLEX_FULL;
	cmd->port		= PORT_TP;
	cmd->phy_address	= 0;
	cmd->transceiver	= XCVR_INTERNAL;
	cmd->autoneg		= AUTONEG_DISABLE;
	cmd->maxtxpkt		= 0;
	cmd->maxrxpkt		= 0;
	return 0;
}

static void tun_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct tun_struct *tun = netdev_priv(dev);

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->fw_version, "N/A");

	switch (tun->flags & TUN_TYPE_MASK) {
	case TUN_TUN_DEV:
		strcpy(info->bus_info, "tun");
		break;
	case TUN_TAP_DEV:
		strcpy(info->bus_info, "tap");
		break;
	}
}

static u32 tun_get_msglevel(struct net_device *dev)
{
#ifdef TUN_DEBUG
	struct tun_struct *tun = netdev_priv(dev);
	return tun->debug;
#else
	return -EOPNOTSUPP;
#endif
}

static void tun_set_msglevel(struct net_device *dev, u32 value)
{
#ifdef TUN_DEBUG
	struct tun_struct *tun = netdev_priv(dev);
	tun->debug = value;
#endif
}

static u32 tun_get_link(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	return tun->attached;
}

static u32 tun_get_rx_csum(struct net_device *dev)
{
	struct tun_struct *tun = netdev_priv(dev);
	return (tun->flags & TUN_NOCHECKSUM) == 0;
}

static int tun_set_rx_csum(struct net_device *dev, u32 data)
{
	struct tun_struct *tun = netdev_priv(dev);
	if (data)
		tun->flags &= ~TUN_NOCHECKSUM;
	else
		tun->flags |= TUN_NOCHECKSUM;
	return 0;
}

static const struct ethtool_ops tun_ethtool_ops = {
	.get_settings	= tun_get_settings,
	.get_drvinfo	= tun_get_drvinfo,
	.get_msglevel	= tun_get_msglevel,
	.set_msglevel	= tun_set_msglevel,
	.get_link	= tun_get_link,
	.get_rx_csum	= tun_get_rx_csum,
	.set_rx_csum	= tun_set_rx_csum
};

static int tun_init_net(struct net *net)
{
	struct tun_net *tn;

	tn = kmalloc(sizeof(*tn), GFP_KERNEL);
	if (tn == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&tn->dev_list);

	if (net_assign_generic(net, tun_net_id, tn)) {
		kfree(tn);
		return -ENOMEM;
	}

	return 0;
}

static void tun_exit_net(struct net *net)
{
	struct tun_net *tn;
	struct tun_struct *tun, *nxt;

	tn = net_generic(net, tun_net_id);

	rtnl_lock();
	list_for_each_entry_safe(tun, nxt, &tn->dev_list, list) {
		DBG(KERN_INFO "%s cleaned up\n", tun->dev->name);
		unregister_netdevice(tun->dev);
	}
	rtnl_unlock();

	kfree(tn);
}

static struct pernet_operations tun_net_ops = {
	.init = tun_init_net,
	.exit = tun_exit_net,
};

static int __init tun_init(void)
{
	int ret = 0;

	printk(KERN_INFO "tun: %s, %s\n", DRV_DESCRIPTION, DRV_VERSION);
	printk(KERN_INFO "tun: %s\n", DRV_COPYRIGHT);

	ret = register_pernet_gen_device(&tun_net_id, &tun_net_ops);
	if (ret) {
		printk(KERN_ERR "tun: Can't register pernet ops\n");
		goto err_pernet;
	}

	ret = misc_register(&tun_miscdev);
	if (ret) {
		printk(KERN_ERR "tun: Can't register misc device %d\n", TUN_MINOR);
		goto err_misc;
	}
	return 0;

err_misc:
	unregister_pernet_gen_device(tun_net_id, &tun_net_ops);
err_pernet:
	return ret;
}

static void tun_cleanup(void)
{
	misc_deregister(&tun_miscdev);
	unregister_pernet_gen_device(tun_net_id, &tun_net_ops);
}

module_init(tun_init);
module_exit(tun_cleanup);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(TUN_MINOR);

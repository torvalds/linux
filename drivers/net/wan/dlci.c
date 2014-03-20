/*
 * DLCI		Implementation of Frame Relay protocol for Linux, according to
 *		RFC 1490.  This generic device provides en/decapsulation for an
 *		underlying hardware driver.  Routes & IPs are assigned to these
 *		interfaces.  Requires 'dlcicfg' program to create usable 
 *		interfaces, the initial one, 'dlci' is for IOCTL use only.
 *
 * Version:	@(#)dlci.c	0.35	4 Jan 1997
 *
 * Author:	Mike McLagan <mike.mclagan@linux.org>
 *
 * Changes:
 *
 *		0.15	Mike Mclagan	Packet freeing, bug in kmalloc call
 *					DLCI_RET handling
 *		0.20	Mike McLagan	More conservative on which packets
 *					are returned for retry and which are
 *					are dropped.  If DLCI_RET_DROP is
 *					returned from the FRAD, the packet is
 *				 	sent back to Linux for re-transmission
 *		0.25	Mike McLagan	Converted to use SIOC IOCTL calls
 *		0.30	Jim Freeman	Fixed to allow IPX traffic
 *		0.35	Michael Elizabeth	Fixed incorrect memcpy_fromfs
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_frad.h>
#include <linux/bitops.h>

#include <net/sock.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

static const char version[] = "DLCI driver v0.35, 4 Jan 1997, mike.mclagan@linux.org";

static LIST_HEAD(dlci_devs);

static void dlci_setup(struct net_device *);

/* 
 * these encapsulate the RFC 1490 requirements as well as 
 * deal with packet transmission and reception, working with
 * the upper network layers 
 */

static int dlci_header(struct sk_buff *skb, struct net_device *dev, 
		       unsigned short type, const void *daddr,
		       const void *saddr, unsigned len)
{
	struct frhdr		hdr;
	unsigned int		hlen;
	char			*dest;

	hdr.control = FRAD_I_UI;
	switch (type)
	{
		case ETH_P_IP:
			hdr.IP_NLPID = FRAD_P_IP;
			hlen = sizeof(hdr.control) + sizeof(hdr.IP_NLPID);
			break;

		/* feel free to add other types, if necessary */

		default:
			hdr.pad = FRAD_P_PADDING;
			hdr.NLPID = FRAD_P_SNAP;
			memset(hdr.OUI, 0, sizeof(hdr.OUI));
			hdr.PID = htons(type);
			hlen = sizeof(hdr);
			break;
	}

	dest = skb_push(skb, hlen);
	if (!dest)
		return 0;

	memcpy(dest, &hdr, hlen);

	return hlen;
}

static void dlci_receive(struct sk_buff *skb, struct net_device *dev)
{
	struct frhdr		*hdr;
	int					process, header;

	if (!pskb_may_pull(skb, sizeof(*hdr))) {
		netdev_notice(dev, "invalid data no header\n");
		dev->stats.rx_errors++;
		kfree_skb(skb);
		return;
	}

	hdr = (struct frhdr *) skb->data;
	process = 0;
	header = 0;
	skb->dev = dev;

	if (hdr->control != FRAD_I_UI)
	{
		netdev_notice(dev, "Invalid header flag 0x%02X\n",
			      hdr->control);
		dev->stats.rx_errors++;
	}
	else
		switch (hdr->IP_NLPID)
		{
			case FRAD_P_PADDING:
				if (hdr->NLPID != FRAD_P_SNAP)
				{
					netdev_notice(dev, "Unsupported NLPID 0x%02X\n",
						      hdr->NLPID);
					dev->stats.rx_errors++;
					break;
				}
	 
				if (hdr->OUI[0] + hdr->OUI[1] + hdr->OUI[2] != 0)
				{
					netdev_notice(dev, "Unsupported organizationally unique identifier 0x%02X-%02X-%02X\n",
						      hdr->OUI[0],
						      hdr->OUI[1],
						      hdr->OUI[2]);
					dev->stats.rx_errors++;
					break;
				}

				/* at this point, it's an EtherType frame */
				header = sizeof(struct frhdr);
				/* Already in network order ! */
				skb->protocol = hdr->PID;
				process = 1;
				break;

			case FRAD_P_IP:
				header = sizeof(hdr->control) + sizeof(hdr->IP_NLPID);
				skb->protocol = htons(ETH_P_IP);
				process = 1;
				break;

			case FRAD_P_SNAP:
			case FRAD_P_Q933:
			case FRAD_P_CLNP:
				netdev_notice(dev, "Unsupported NLPID 0x%02X\n",
					      hdr->pad);
				dev->stats.rx_errors++;
				break;

			default:
				netdev_notice(dev, "Invalid pad byte 0x%02X\n",
					      hdr->pad);
				dev->stats.rx_errors++;
				break;				
		}

	if (process)
	{
		/* we've set up the protocol, so discard the header */
		skb_reset_mac_header(skb);
		skb_pull(skb, header);
		dev->stats.rx_bytes += skb->len;
		netif_rx(skb);
		dev->stats.rx_packets++;
	}
	else
		dev_kfree_skb(skb);
}

static netdev_tx_t dlci_transmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dlci_local *dlp = netdev_priv(dev);

	if (skb)
		dlp->slave->netdev_ops->ndo_start_xmit(skb, dlp->slave);
	return NETDEV_TX_OK;
}

static int dlci_config(struct net_device *dev, struct dlci_conf __user *conf, int get)
{
	struct dlci_conf	config;
	struct dlci_local	*dlp;
	struct frad_local	*flp;
	int			err;

	dlp = netdev_priv(dev);

	flp = netdev_priv(dlp->slave);

	if (!get)
	{
		if (copy_from_user(&config, conf, sizeof(struct dlci_conf)))
			return -EFAULT;
		if (config.flags & ~DLCI_VALID_FLAGS)
			return -EINVAL;
		memcpy(&dlp->config, &config, sizeof(struct dlci_conf));
		dlp->configured = 1;
	}

	err = (*flp->dlci_conf)(dlp->slave, dev, get);
	if (err)
		return err;

	if (get)
	{
		if (copy_to_user(conf, &dlp->config, sizeof(struct dlci_conf)))
			return -EFAULT;
	}

	return 0;
}

static int dlci_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dlci_local *dlp;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	dlp = netdev_priv(dev);

	switch (cmd)
	{
		case DLCI_GET_SLAVE:
			if (!*(short *)(dev->dev_addr))
				return -EINVAL;

			strncpy(ifr->ifr_slave, dlp->slave->name, sizeof(ifr->ifr_slave));
			break;

		case DLCI_GET_CONF:
		case DLCI_SET_CONF:
			if (!*(short *)(dev->dev_addr))
				return -EINVAL;

			return dlci_config(dev, ifr->ifr_data, cmd == DLCI_GET_CONF);
			break;

		default: 
			return -EOPNOTSUPP;
	}
	return 0;
}

static int dlci_change_mtu(struct net_device *dev, int new_mtu)
{
	struct dlci_local *dlp = netdev_priv(dev);

	return dev_set_mtu(dlp->slave, new_mtu);
}

static int dlci_open(struct net_device *dev)
{
	struct dlci_local	*dlp;
	struct frad_local	*flp;
	int			err;

	dlp = netdev_priv(dev);

	if (!*(short *)(dev->dev_addr))
		return -EINVAL;

	if (!netif_running(dlp->slave))
		return -ENOTCONN;

	flp = netdev_priv(dlp->slave);
	err = (*flp->activate)(dlp->slave, dev);
	if (err)
		return err;

	netif_start_queue(dev);

	return 0;
}

static int dlci_close(struct net_device *dev)
{
	struct dlci_local	*dlp;
	struct frad_local	*flp;
	int			err;

	netif_stop_queue(dev);

	dlp = netdev_priv(dev);

	flp = netdev_priv(dlp->slave);
	err = (*flp->deactivate)(dlp->slave, dev);

	return 0;
}

static int dlci_add(struct dlci_add *dlci)
{
	struct net_device	*master, *slave;
	struct dlci_local	*dlp;
	struct frad_local	*flp;
	int			err = -EINVAL;


	/* validate slave device */
	slave = dev_get_by_name(&init_net, dlci->devname);
	if (!slave)
		return -ENODEV;

	if (slave->type != ARPHRD_FRAD || netdev_priv(slave) == NULL)
		goto err1;

	/* create device name */
	master = alloc_netdev( sizeof(struct dlci_local), "dlci%d",
			      dlci_setup);
	if (!master) {
		err = -ENOMEM;
		goto err1;
	}

	/* make sure same slave not already registered */
	rtnl_lock();
	list_for_each_entry(dlp, &dlci_devs, list) {
		if (dlp->slave == slave) {
			err = -EBUSY;
			goto err2;
		}
	}

	*(short *)(master->dev_addr) = dlci->dlci;

	dlp = netdev_priv(master);
	dlp->slave = slave;
	dlp->master = master;

	flp = netdev_priv(slave);
	err = (*flp->assoc)(slave, master);
	if (err < 0)
		goto err2;

	err = register_netdevice(master);
	if (err < 0) 
		goto err2;

	strcpy(dlci->devname, master->name);

	list_add(&dlp->list, &dlci_devs);
	rtnl_unlock();

	return 0;

 err2:
	rtnl_unlock();
	free_netdev(master);
 err1:
	dev_put(slave);
	return err;
}

static int dlci_del(struct dlci_add *dlci)
{
	struct dlci_local	*dlp;
	struct frad_local	*flp;
	struct net_device	*master, *slave;
	int			err;
	bool			found = false;

	rtnl_lock();

	/* validate slave device */
	master = __dev_get_by_name(&init_net, dlci->devname);
	if (!master) {
		err = -ENODEV;
		goto out;
	}

	list_for_each_entry(dlp, &dlci_devs, list) {
		if (dlp->master == master) {
			found = true;
			break;
		}
	}
	if (!found) {
		err = -ENODEV;
		goto out;
	}

	if (netif_running(master)) {
		err = -EBUSY;
		goto out;
	}

	dlp = netdev_priv(master);
	slave = dlp->slave;
	flp = netdev_priv(slave);

	err = (*flp->deassoc)(slave, master);
	if (!err) {
		list_del(&dlp->list);

		unregister_netdevice(master);

		dev_put(slave);
	}
out:
	rtnl_unlock();
	return err;
}

static int dlci_ioctl(unsigned int cmd, void __user *arg)
{
	struct dlci_add add;
	int err;
	
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&add, arg, sizeof(struct dlci_add)))
		return -EFAULT;

	switch (cmd)
	{
		case SIOCADDDLCI:
			err = dlci_add(&add);

			if (!err)
				if (copy_to_user(arg, &add, sizeof(struct dlci_add)))
					return -EFAULT;
			break;

		case SIOCDELDLCI:
			err = dlci_del(&add);
			break;

		default:
			err = -EINVAL;
	}

	return err;
}

static const struct header_ops dlci_header_ops = {
	.create	= dlci_header,
};

static const struct net_device_ops dlci_netdev_ops = {
	.ndo_open	= dlci_open,
	.ndo_stop	= dlci_close,
	.ndo_do_ioctl	= dlci_dev_ioctl,
	.ndo_start_xmit	= dlci_transmit,
	.ndo_change_mtu	= dlci_change_mtu,
};

static void dlci_setup(struct net_device *dev)
{
	struct dlci_local *dlp = netdev_priv(dev);

	dev->flags		= 0;
	dev->header_ops		= &dlci_header_ops;
	dev->netdev_ops		= &dlci_netdev_ops;
	dev->destructor		= free_netdev;

	dlp->receive		= dlci_receive;

	dev->type		= ARPHRD_DLCI;
	dev->hard_header_len	= sizeof(struct frhdr);
	dev->addr_len		= sizeof(short);

}

/* if slave is unregistering, then cleanup master */
static int dlci_dev_event(struct notifier_block *unused,
			  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (dev_net(dev) != &init_net)
		return NOTIFY_DONE;

	if (event == NETDEV_UNREGISTER) {
		struct dlci_local *dlp;

		list_for_each_entry(dlp, &dlci_devs, list) {
			if (dlp->slave == dev) {
				list_del(&dlp->list);
				unregister_netdevice(dlp->master);
				dev_put(dlp->slave);
				break;
			}
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block dlci_notifier = {
	.notifier_call = dlci_dev_event,
};

static int __init init_dlci(void)
{
	dlci_ioctl_set(dlci_ioctl);
	register_netdevice_notifier(&dlci_notifier);

	printk("%s.\n", version);

	return 0;
}

static void __exit dlci_exit(void)
{
	struct dlci_local	*dlp, *nxt;
	
	dlci_ioctl_set(NULL);
	unregister_netdevice_notifier(&dlci_notifier);

	rtnl_lock();
	list_for_each_entry_safe(dlp, nxt, &dlci_devs, list) {
		unregister_netdevice(dlp->master);
		dev_put(dlp->slave);
	}
	rtnl_unlock();
}

module_init(init_dlci);
module_exit(dlci_exit);

MODULE_AUTHOR("Mike McLagan");
MODULE_DESCRIPTION("Frame Relay DLCI layer");
MODULE_LICENSE("GPL");

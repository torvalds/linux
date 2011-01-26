/*
 *	G8BPQ compatible "AX.25 via ethernet" driver release 004
 *
 *	This code REQUIRES 2.0.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	This is a "pseudo" network driver to allow AX.25 over Ethernet
 *	using G8BPQ encapsulation. It has been extracted from the protocol
 *	implementation because
 *
 *		- things got unreadable within the protocol stack
 *		- to cure the protocol stack from "feature-ism"
 *		- a protocol implementation shouldn't need to know on
 *		  which hardware it is running
 *		- user-level programs like the AX.25 utilities shouldn't
 *		  need to know about the hardware.
 *		- IP over ethernet encapsulated AX.25 was impossible
 *		- rxecho.c did not work
 *		- to have room for extensions
 *		- it just deserves to "live" as an own driver
 *
 *	This driver can use any ethernet destination address, and can be
 *	limited to accept frames from one dedicated ethernet card only.
 *
 *	Note that the driver sets up the BPQ devices automagically on
 *	startup or (if started before the "insmod" of an ethernet device)
 *	on "ifconfig up". It hopefully will remove the BPQ on "rmmod"ing
 *	the ethernet device (in fact: as soon as another ethernet or bpq
 *	device gets "ifconfig"ured).
 *
 *	I have heard that several people are thinking of experiments
 *	with highspeed packet radio using existing ethernet cards.
 *	Well, this driver is prepared for this purpose, just add
 *	your tx key control and a txdelay / tailtime algorithm,
 *	probably some buffering, and /voila/...
 *
 *	History
 *	BPQ   001	Joerg(DL1BKE)		Extracted BPQ code from AX.25
 *						protocol stack and added my own
 *						yet existing patches
 *	BPQ   002	Joerg(DL1BKE)		Scan network device list on
 *						startup.
 *	BPQ   003	Joerg(DL1BKE)		Ethernet destination address
 *						and accepted source address
 *						can be configured by an ioctl()
 *						call.
 *						Fixed to match Linux networking
 *						changes - 2.1.15.
 *	BPQ   004	Joerg(DL1BKE)		Fixed to not lock up on ifconfig.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>

#include <net/ip.h>
#include <net/arp.h>
#include <net/net_namespace.h>

#include <linux/bpqether.h>

static const char banner[] __initdata = KERN_INFO \
	"AX.25: bpqether driver version 004\n";

static char bcast_addr[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static char bpq_eth_addr[6];

static int bpq_rcv(struct sk_buff *, struct net_device *, struct packet_type *, struct net_device *);
static int bpq_device_event(struct notifier_block *, unsigned long, void *);

static struct packet_type bpq_packet_type __read_mostly = {
	.type	= cpu_to_be16(ETH_P_BPQ),
	.func	= bpq_rcv,
};

static struct notifier_block bpq_dev_notifier = {
	.notifier_call =bpq_device_event,
};


struct bpqdev {
	struct list_head bpq_list;	/* list of bpq devices chain */
	struct net_device *ethdev;	/* link to ethernet device */
	struct net_device *axdev;	/* bpq device (bpq#) */
	char   dest_addr[6];		/* ether destination address */
	char   acpt_addr[6];		/* accept ether frames from this address only */
};

static LIST_HEAD(bpq_devices);

/*
 * bpqether network devices are paired with ethernet devices below them, so
 * form a special "super class" of normal ethernet devices; split their locks
 * off into a separate class since they always nest.
 */
static struct lock_class_key bpq_netdev_xmit_lock_key;
static struct lock_class_key bpq_netdev_addr_lock_key;

static void bpq_set_lockdep_class_one(struct net_device *dev,
				      struct netdev_queue *txq,
				      void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock, &bpq_netdev_xmit_lock_key);
}

static void bpq_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock, &bpq_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, bpq_set_lockdep_class_one, NULL);
}

/* ------------------------------------------------------------------------ */


/*
 *	Get the ethernet device for a BPQ device
 */
static inline struct net_device *bpq_get_ether_dev(struct net_device *dev)
{
	struct bpqdev *bpq = netdev_priv(dev);

	return bpq ? bpq->ethdev : NULL;
}

/*
 *	Get the BPQ device for the ethernet device
 */
static inline struct net_device *bpq_get_ax25_dev(struct net_device *dev)
{
	struct bpqdev *bpq;

	list_for_each_entry_rcu(bpq, &bpq_devices, bpq_list) {
		if (bpq->ethdev == dev)
			return bpq->axdev;
	}
	return NULL;
}

static inline int dev_is_ethdev(struct net_device *dev)
{
	return dev->type == ARPHRD_ETHER && strncmp(dev->name, "dummy", 5);
}

/* ------------------------------------------------------------------------ */


/*
 *	Receive an AX.25 frame via an ethernet interface.
 */
static int bpq_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *ptype, struct net_device *orig_dev)
{
	int len;
	char * ptr;
	struct ethhdr *eth;
	struct bpqdev *bpq;

	if (!net_eq(dev_net(dev), &init_net))
		goto drop;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		return NET_RX_DROP;

	if (!pskb_may_pull(skb, sizeof(struct ethhdr)))
		goto drop;

	rcu_read_lock();
	dev = bpq_get_ax25_dev(dev);

	if (dev == NULL || !netif_running(dev)) 
		goto drop_unlock;

	/*
	 * if we want to accept frames from just one ethernet device
	 * we check the source address of the sender.
	 */

	bpq = netdev_priv(dev);

	eth = eth_hdr(skb);

	if (!(bpq->acpt_addr[0] & 0x01) &&
	    memcmp(eth->h_source, bpq->acpt_addr, ETH_ALEN))
		goto drop_unlock;

	if (skb_cow(skb, sizeof(struct ethhdr)))
		goto drop_unlock;

	len = skb->data[0] + skb->data[1] * 256 - 5;

	skb_pull(skb, 2);	/* Remove the length bytes */
	skb_trim(skb, len);	/* Set the length of the data */

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	ptr = skb_push(skb, 1);
	*ptr = 0;

	skb->protocol = ax25_type_trans(skb, dev);
	netif_rx(skb);
unlock:

	rcu_read_unlock();

	return 0;
drop_unlock:
	kfree_skb(skb);
	goto unlock;

drop:
	kfree_skb(skb);
	return 0;
}

/*
 * 	Send an AX.25 frame via an ethernet interface
 */
static netdev_tx_t bpq_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *ptr;
	struct bpqdev *bpq;
	struct net_device *orig_dev;
	int size;

	/*
	 * Just to be *really* sure not to send anything if the interface
	 * is down, the ethernet device may have gone.
	 */
	if (!netif_running(dev)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb_pull(skb, 1);			/* Drop KISS byte */
	size = skb->len;

	/*
	 * We're about to mess with the skb which may still shared with the
	 * generic networking code so unshare and ensure it's got enough
	 * space for the BPQ headers.
	 */
	if (skb_cow(skb, AX25_BPQ_HEADER_LEN)) {
		if (net_ratelimit())
			pr_err("bpqether: out of memory\n");
		kfree_skb(skb);

		return NETDEV_TX_OK;
	}

	ptr = skb_push(skb, 2);			/* Make space for length */

	*ptr++ = (size + 5) % 256;
	*ptr++ = (size + 5) / 256;

	bpq = netdev_priv(dev);

	orig_dev = dev;
	if ((dev = bpq_get_ether_dev(dev)) == NULL) {
		orig_dev->stats.tx_dropped++;
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->protocol = ax25_type_trans(skb, dev);
	skb_reset_network_header(skb);
	dev_hard_header(skb, dev, ETH_P_BPQ, bpq->dest_addr, NULL, 0);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes+=skb->len;
  
	dev_queue_xmit(skb);
	netif_wake_queue(dev);
	return NETDEV_TX_OK;
}

/*
 *	Set AX.25 callsign
 */
static int bpq_set_mac_address(struct net_device *dev, void *addr)
{
    struct sockaddr *sa = (struct sockaddr *)addr;

    memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

    return 0;
}

/*	Ioctl commands
 *
 *		SIOCSBPQETHOPT		reserved for enhancements
 *		SIOCSBPQETHADDR		set the destination and accepted
 *					source ethernet address (broadcast
 *					or multicast: accept all)
 */
static int bpq_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct bpq_ethaddr __user *ethaddr = ifr->ifr_data;
	struct bpqdev *bpq = netdev_priv(dev);
	struct bpq_req req;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
		case SIOCSBPQETHOPT:
			if (copy_from_user(&req, ifr->ifr_data, sizeof(struct bpq_req)))
				return -EFAULT;
			switch (req.cmd) {
				case SIOCGBPQETHPARAM:
				case SIOCSBPQETHPARAM:
				default:
					return -EINVAL;
			}

			break;

		case SIOCSBPQETHADDR:
			if (copy_from_user(bpq->dest_addr, ethaddr->destination, ETH_ALEN))
				return -EFAULT;
			if (copy_from_user(bpq->acpt_addr, ethaddr->accept, ETH_ALEN))
				return -EFAULT;
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

/*
 * open/close a device
 */
static int bpq_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int bpq_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}


/* ------------------------------------------------------------------------ */


/*
 *	Proc filesystem
 */
static void *bpq_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	int i = 1;
	struct bpqdev *bpqdev;

	rcu_read_lock();

	if (*pos == 0)
		return SEQ_START_TOKEN;
	
	list_for_each_entry_rcu(bpqdev, &bpq_devices, bpq_list) {
		if (i == *pos)
			return bpqdev;
	}
	return NULL;
}

static void *bpq_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *p;

	++*pos;

	if (v == SEQ_START_TOKEN)
		p = rcu_dereference(bpq_devices.next);
	else
		p = rcu_dereference(((struct bpqdev *)v)->bpq_list.next);

	return (p == &bpq_devices) ? NULL 
		: list_entry(p, struct bpqdev, bpq_list);
}

static void bpq_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}


static int bpq_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, 
			 "dev   ether      destination        accept from\n");
	else {
		const struct bpqdev *bpqdev = v;

		seq_printf(seq, "%-5s %-10s %pM  ",
			bpqdev->axdev->name, bpqdev->ethdev->name,
			bpqdev->dest_addr);

		if (is_multicast_ether_addr(bpqdev->acpt_addr))
			seq_printf(seq, "*\n");
		else
			seq_printf(seq, "%pM\n", bpqdev->acpt_addr);

	}
	return 0;
}

static const struct seq_operations bpq_seqops = {
	.start = bpq_seq_start,
	.next = bpq_seq_next,
	.stop = bpq_seq_stop,
	.show = bpq_seq_show,
};

static int bpq_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &bpq_seqops);
}

static const struct file_operations bpq_info_fops = {
	.owner = THIS_MODULE,
	.open = bpq_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};


/* ------------------------------------------------------------------------ */

static const struct net_device_ops bpq_netdev_ops = {
	.ndo_open	     = bpq_open,
	.ndo_stop	     = bpq_close,
	.ndo_start_xmit	     = bpq_xmit,
	.ndo_set_mac_address = bpq_set_mac_address,
	.ndo_do_ioctl	     = bpq_ioctl,
};

static void bpq_setup(struct net_device *dev)
{
	dev->netdev_ops	     = &bpq_netdev_ops;
	dev->destructor	     = free_netdev;

	memcpy(dev->broadcast, &ax25_bcast, AX25_ADDR_LEN);
	memcpy(dev->dev_addr,  &ax25_defaddr, AX25_ADDR_LEN);

	dev->flags      = 0;

#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
	dev->header_ops      = &ax25_header_ops;
#endif

	dev->type            = ARPHRD_AX25;
	dev->hard_header_len = AX25_MAX_HEADER_LEN + AX25_BPQ_HEADER_LEN;
	dev->mtu             = AX25_DEF_PACLEN;
	dev->addr_len        = AX25_ADDR_LEN;

}

/*
 *	Setup a new device.
 */
static int bpq_new_device(struct net_device *edev)
{
	int err;
	struct net_device *ndev;
	struct bpqdev *bpq;

	ndev = alloc_netdev(sizeof(struct bpqdev), "bpq%d",
			   bpq_setup);
	if (!ndev)
		return -ENOMEM;

		
	bpq = netdev_priv(ndev);
	dev_hold(edev);
	bpq->ethdev = edev;
	bpq->axdev = ndev;

	memcpy(bpq->dest_addr, bcast_addr, sizeof(bpq_eth_addr));
	memcpy(bpq->acpt_addr, bcast_addr, sizeof(bpq_eth_addr));

	err = dev_alloc_name(ndev, ndev->name);
	if (err < 0) 
		goto error;

	err = register_netdevice(ndev);
	if (err)
		goto error;
	bpq_set_lockdep_class(ndev);

	/* List protected by RTNL */
	list_add_rcu(&bpq->bpq_list, &bpq_devices);
	return 0;

 error:
	dev_put(edev);
	free_netdev(ndev);
	return err;
	
}

static void bpq_free_device(struct net_device *ndev)
{
	struct bpqdev *bpq = netdev_priv(ndev);

	dev_put(bpq->ethdev);
	list_del_rcu(&bpq->bpq_list);

	unregister_netdevice(ndev);
}

/*
 *	Handle device status changes.
 */
static int bpq_device_event(struct notifier_block *this,unsigned long event, void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (!dev_is_ethdev(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:		/* new ethernet device -> new BPQ interface */
		if (bpq_get_ax25_dev(dev) == NULL)
			bpq_new_device(dev);
		break;

	case NETDEV_DOWN:	/* ethernet device closed -> close BPQ interface */
		if ((dev = bpq_get_ax25_dev(dev)) != NULL)
			dev_close(dev);
		break;

	case NETDEV_UNREGISTER:	/* ethernet device removed -> free BPQ interface */
		if ((dev = bpq_get_ax25_dev(dev)) != NULL)
			bpq_free_device(dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}


/* ------------------------------------------------------------------------ */

/*
 * Initialize driver. To be called from af_ax25 if not compiled as a
 * module
 */
static int __init bpq_init_driver(void)
{
#ifdef CONFIG_PROC_FS
	if (!proc_net_fops_create(&init_net, "bpqether", S_IRUGO, &bpq_info_fops)) {
		printk(KERN_ERR
			"bpq: cannot create /proc/net/bpqether entry.\n");
		return -ENOENT;
	}
#endif  /* CONFIG_PROC_FS */

	dev_add_pack(&bpq_packet_type);

	register_netdevice_notifier(&bpq_dev_notifier);

	printk(banner);

	return 0;
}

static void __exit bpq_cleanup_driver(void)
{
	struct bpqdev *bpq;

	dev_remove_pack(&bpq_packet_type);

	unregister_netdevice_notifier(&bpq_dev_notifier);

	proc_net_remove(&init_net, "bpqether");

	rtnl_lock();
	while (!list_empty(&bpq_devices)) {
		bpq = list_entry(bpq_devices.next, struct bpqdev, bpq_list);
		bpq_free_device(bpq->axdev);
	}
	rtnl_unlock();
}

MODULE_AUTHOR("Joerg Reuter DL1BKE <jreuter@yaina.de>");
MODULE_DESCRIPTION("Transmit and receive AX.25 packets over Ethernet");
MODULE_LICENSE("GPL");
module_init(bpq_init_driver);
module_exit(bpq_cleanup_driver);

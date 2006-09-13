/* dummy.c: a dummy net driver

	The purpose of this driver is to provide a device to point a
	route through, but not to actually transmit packets.

	Why?  If you have a machine whose only connection is an occasional
	PPP/SLIP/PLIP link, you can only connect to your own hostname
	when the link is up.  Otherwise you have to use localhost.
	This isn't very consistent.

	One solution is to set up a dummy link using PPP/SLIP/PLIP,
	but this seems (to me) too much overhead for too little gain.
	This driver provides a small alternative. Thus you can do

	[when not running slip]
		ifconfig dummy slip.addr.ess.here up
	[to go to slip]
		ifconfig dummy down
		dip whatever

	This was written by looking at Donald Becker's skeleton driver
	and the loopback driver.  I then threw away anything that didn't
	apply!	Thanks to Alan Cox for the key clue on what to do with
	misguided packets.

			Nick Holloway, 27th May 1994
	[I tweaked this explanation a little but that's all]
			Alan Cox, 30th May 1994
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

static int numdummies = 1;

static int dummy_xmit(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *dummy_get_stats(struct net_device *dev);

static int dummy_set_address(struct net_device *dev, void *p)
{
	struct sockaddr *sa = p;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, sa->sa_data, ETH_ALEN);
	return 0;
}

/* fake multicast ability */
static void set_multicast_list(struct net_device *dev)
{
}

static void __init dummy_setup(struct net_device *dev)
{
	/* Initialize the device structure. */
	dev->get_stats = dummy_get_stats;
	dev->hard_start_xmit = dummy_xmit;
	dev->set_multicast_list = set_multicast_list;
	dev->set_mac_address = dummy_set_address;

	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->change_mtu = NULL;
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	SET_MODULE_OWNER(dev);
	random_ether_addr(dev->dev_addr);
}

static int dummy_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = netdev_priv(dev);

	stats->tx_packets++;
	stats->tx_bytes+=skb->len;

	dev_kfree_skb(skb);
	return 0;
}

static struct net_device_stats *dummy_get_stats(struct net_device *dev)
{
	return netdev_priv(dev);
}

static struct net_device **dummies;

/* Number of dummy devices to be set up by this module. */
module_param(numdummies, int, 0);
MODULE_PARM_DESC(numdummies, "Number of dummy pseudo devices");

static int __init dummy_init_one(int index)
{
	struct net_device *dev_dummy;
	int err;

	dev_dummy = alloc_netdev(sizeof(struct net_device_stats),
				 "dummy%d", dummy_setup);

	if (!dev_dummy)
		return -ENOMEM;

	if ((err = register_netdev(dev_dummy))) {
		free_netdev(dev_dummy);
		dev_dummy = NULL;
	} else {
		dummies[index] = dev_dummy;
	}

	return err;
}

static void dummy_free_one(int index)
{
	unregister_netdev(dummies[index]);
	free_netdev(dummies[index]);
}

static int __init dummy_init_module(void)
{
	int i, err = 0;
	dummies = kmalloc(numdummies * sizeof(void *), GFP_KERNEL);
	if (!dummies)
		return -ENOMEM;
	for (i = 0; i < numdummies && !err; i++)
		err = dummy_init_one(i);
	if (err) {
		i--;
		while (--i >= 0)
			dummy_free_one(i);
	}
	return err;
}

static void __exit dummy_cleanup_module(void)
{
	int i;
	for (i = 0; i < numdummies; i++)
		dummy_free_one(i);
	kfree(dummies);
}

module_init(dummy_init_module);
module_exit(dummy_cleanup_module);
MODULE_LICENSE("GPL");

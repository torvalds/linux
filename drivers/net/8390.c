/* 8390 core for usual drivers */

static const char version[] =
    "8390.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include "lib8390.c"

int ei_open(struct net_device *dev)
{
	return __ei_open(dev);
}
EXPORT_SYMBOL(ei_open);

int ei_close(struct net_device *dev)
{
	return __ei_close(dev);
}
EXPORT_SYMBOL(ei_close);

int ei_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return __ei_start_xmit(skb, dev);
}
EXPORT_SYMBOL(ei_start_xmit);

struct net_device_stats *ei_get_stats(struct net_device *dev)
{
	return __ei_get_stats(dev);
}
EXPORT_SYMBOL(ei_get_stats);

void ei_set_multicast_list(struct net_device *dev)
{
	__ei_set_multicast_list(dev);
}
EXPORT_SYMBOL(ei_set_multicast_list);

void ei_tx_timeout(struct net_device *dev)
{
	__ei_tx_timeout(dev);
}
EXPORT_SYMBOL(ei_tx_timeout);

irqreturn_t ei_interrupt(int irq, void *dev_id)
{
	return __ei_interrupt(irq, dev_id);
}
EXPORT_SYMBOL(ei_interrupt);

#ifdef CONFIG_NET_POLL_CONTROLLER
void ei_poll(struct net_device *dev)
{
	__ei_poll(dev);
}
EXPORT_SYMBOL(ei_poll);
#endif

const struct net_device_ops ei_netdev_ops = {
	.ndo_open		= ei_open,
	.ndo_stop		= ei_close,
	.ndo_start_xmit		= ei_start_xmit,
	.ndo_tx_timeout		= ei_tx_timeout,
	.ndo_get_stats		= ei_get_stats,
	.ndo_set_multicast_list = ei_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ei_poll,
#endif
};
EXPORT_SYMBOL(ei_netdev_ops);

struct net_device *__alloc_ei_netdev(int size)
{
	struct net_device *dev = ____alloc_ei_netdev(size);
#ifdef CONFIG_COMPAT_NET_DEV_OPS
	if (dev) {
		dev->hard_start_xmit = ei_start_xmit;
		dev->get_stats	= ei_get_stats;
		dev->set_multicast_list = ei_set_multicast_list;
		dev->tx_timeout = ei_tx_timeout;
	}
#endif
	return dev;
}
EXPORT_SYMBOL(__alloc_ei_netdev);

void NS8390_init(struct net_device *dev, int startp)
{
	__NS8390_init(dev, startp);
}
EXPORT_SYMBOL(NS8390_init);

#if defined(MODULE)

static int __init ns8390_module_init(void)
{
	return 0;
}

static void __exit ns8390_module_exit(void)
{
}

module_init(ns8390_module_init);
module_exit(ns8390_module_exit);
#endif /* MODULE */
MODULE_LICENSE("GPL");

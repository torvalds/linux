/* 8390 core for usual drivers */

static const char version[] =
    "8390.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include "lib8390.c"

int ei_open(struct net_device *dev)
{
	return __ei_open(dev);
}

int ei_close(struct net_device *dev)
{
	return __ei_close(dev);
}

irqreturn_t ei_interrupt(int irq, void *dev_id)
{
	return __ei_interrupt(irq, dev_id);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
void ei_poll(struct net_device *dev)
{
	__ei_poll(dev);
}
#endif

struct net_device *__alloc_ei_netdev(int size)
{
	return ____alloc_ei_netdev(size);
}

void NS8390_init(struct net_device *dev, int startp)
{
	return __NS8390_init(dev, startp);
}

EXPORT_SYMBOL(ei_open);
EXPORT_SYMBOL(ei_close);
EXPORT_SYMBOL(ei_interrupt);
#ifdef CONFIG_NET_POLL_CONTROLLER
EXPORT_SYMBOL(ei_poll);
#endif
EXPORT_SYMBOL(NS8390_init);
EXPORT_SYMBOL(__alloc_ei_netdev);

#if defined(MODULE)

int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}

#endif /* MODULE */
MODULE_LICENSE("GPL");

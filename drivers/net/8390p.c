/* 8390 core for ISA devices needing bus delays */

static const char version[] =
    "8390p.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#define ei_inb(_p)	inb(_p)
#define ei_outb(_v, _p)	outb(_v, _p)
#define ei_inb_p(_p)	inb_p(_p)
#define ei_outb_p(_v, _p) outb_p(_v, _p)

#include "lib8390.c"

int eip_open(struct net_device *dev)
{
	return __ei_open(dev);
}
EXPORT_SYMBOL(eip_open);

int eip_close(struct net_device *dev)
{
	return __ei_close(dev);
}
EXPORT_SYMBOL(eip_close);

irqreturn_t eip_interrupt(int irq, void *dev_id)
{
	return __ei_interrupt(irq, dev_id);
}
EXPORT_SYMBOL(eip_interrupt);

#ifdef CONFIG_NET_POLL_CONTROLLER
void eip_poll(struct net_device *dev)
{
	__ei_poll(dev);
}
EXPORT_SYMBOL(eip_poll);
#endif

struct net_device *__alloc_eip_netdev(int size)
{
	return ____alloc_ei_netdev(size);
}
EXPORT_SYMBOL(__alloc_eip_netdev);

void NS8390p_init(struct net_device *dev, int startp)
{
	__NS8390_init(dev, startp);
}
EXPORT_SYMBOL(NS8390p_init);

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

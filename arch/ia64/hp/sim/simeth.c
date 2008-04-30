/*
 * Simulated Ethernet Driver
 *
 * Copyright (C) 1999-2001, 2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/notifier.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/hpsim.h>

#include "hpsim_ssc.h"

#define SIMETH_RECV_MAX	10

/*
 * Maximum possible received frame for Ethernet.
 * We preallocate an sk_buff of that size to avoid costly
 * memcpy for temporary buffer into sk_buff. We do basically
 * what's done in other drivers, like eepro with a ring.
 * The difference is, of course, that we don't have real DMA !!!
 */
#define SIMETH_FRAME_SIZE	ETH_FRAME_LEN


#define NETWORK_INTR			8

struct simeth_local {
	struct net_device_stats stats;
	int 			simfd;	 /* descriptor in the simulator */
};

static int simeth_probe1(void);
static int simeth_open(struct net_device *dev);
static int simeth_close(struct net_device *dev);
static int simeth_tx(struct sk_buff *skb, struct net_device *dev);
static int simeth_rx(struct net_device *dev);
static struct net_device_stats *simeth_get_stats(struct net_device *dev);
static irqreturn_t simeth_interrupt(int irq, void *dev_id);
static void set_multicast_list(struct net_device *dev);
static int simeth_device_event(struct notifier_block *this,unsigned long event, void *ptr);

static char *simeth_version="0.3";

/*
 * This variable is used to establish a mapping between the Linux/ia64 kernel
 * and the host linux kernel.
 *
 * As of today, we support only one card, even though most of the code
 * is ready for many more. The mapping is then:
 *	linux/ia64 -> linux/x86
 * 	   eth0    -> eth1
 *
 * In the future, we some string operations, we could easily support up
 * to 10 cards (0-9).
 *
 * The default mapping can be changed on the kernel command line by
 * specifying simeth=ethX (or whatever string you want).
 */
static char *simeth_device="eth0";	 /* default host interface to use */



static volatile unsigned int card_count; /* how many cards "found" so far */
static int simeth_debug;		/* set to 1 to get debug information */

/*
 * Used to catch IFF_UP & IFF_DOWN events
 */
static struct notifier_block simeth_dev_notifier = {
	simeth_device_event,
	NULL
};


/*
 * Function used when using a kernel command line option.
 *
 * Format: simeth=interface_name (like eth0)
 */
static int __init
simeth_setup(char *str)
{
	simeth_device = str;
	return 1;
}

__setup("simeth=", simeth_setup);

/*
 * Function used to probe for simeth devices when not installed
 * as a loadable module
 */

int __init
simeth_probe (void)
{
	int r;

	printk(KERN_INFO "simeth: v%s\n", simeth_version);

	r = simeth_probe1();

	if (r == 0) register_netdevice_notifier(&simeth_dev_notifier);

	return r;
}

static inline int
netdev_probe(char *name, unsigned char *ether)
{
	return ia64_ssc(__pa(name), __pa(ether), 0,0, SSC_NETDEV_PROBE);
}


static inline int
netdev_connect(int irq)
{
	/* XXX Fix me
	 * this does not support multiple cards
	 * also no return value
	 */
	ia64_ssc_connect_irq(NETWORK_INTR, irq);
	return 0;
}

static inline int
netdev_attach(int fd, int irq, unsigned int ipaddr)
{
	/* this puts the host interface in the right mode (start interrupting) */
	return ia64_ssc(fd, ipaddr, 0,0, SSC_NETDEV_ATTACH);
}


static inline int
netdev_detach(int fd)
{
	/*
	 * inactivate the host interface (don't interrupt anymore) */
	return ia64_ssc(fd, 0,0,0, SSC_NETDEV_DETACH);
}

static inline int
netdev_send(int fd, unsigned char *buf, unsigned int len)
{
	return ia64_ssc(fd, __pa(buf), len, 0, SSC_NETDEV_SEND);
}

static inline int
netdev_read(int fd, unsigned char *buf, unsigned int len)
{
	return ia64_ssc(fd, __pa(buf), len, 0, SSC_NETDEV_RECV);
}

/*
 * Function shared with module code, so cannot be in init section
 *
 * So far this function "detects" only one card (test_&_set) but could
 * be extended easily.
 *
 * Return:
 * 	- -ENODEV is no device found
 *	- -ENOMEM is no more memory
 *	- 0 otherwise
 */
static int
simeth_probe1(void)
{
	unsigned char mac_addr[ETH_ALEN];
	struct simeth_local *local;
	struct net_device *dev;
	int fd, i, err, rc;

	/*
	 * XXX Fix me
	 * let's support just one card for now
	 */
	if (test_and_set_bit(0, &card_count))
		return -ENODEV;

	/*
	 * check with the simulator for the device
	 */
	fd = netdev_probe(simeth_device, mac_addr);
	if (fd == -1)
		return -ENODEV;

	dev = alloc_etherdev(sizeof(struct simeth_local));
	if (!dev)
		return -ENOMEM;

	memcpy(dev->dev_addr, mac_addr, sizeof(mac_addr));

	local = dev->priv;
	local->simfd = fd; /* keep track of underlying file descriptor */

	dev->open		= simeth_open;
	dev->stop		= simeth_close;
	dev->hard_start_xmit	= simeth_tx;
	dev->get_stats		= simeth_get_stats;
	dev->set_multicast_list = set_multicast_list; /* no yet used */

	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}

	if ((rc = assign_irq_vector(AUTO_ASSIGN)) < 0)
		panic("%s: out of interrupt vectors!\n", __func__);
	dev->irq = rc;

	/*
	 * attach the interrupt in the simulator, this does enable interrupts
	 * until a netdev_attach() is called
	 */
	netdev_connect(dev->irq);

	printk(KERN_INFO "%s: hosteth=%s simfd=%d, HwAddr",
	       dev->name, simeth_device, local->simfd);
	for(i = 0; i < ETH_ALEN; i++) {
		printk(" %2.2x", dev->dev_addr[i]);
	}
	printk(", IRQ %d\n", dev->irq);

	return 0;
}

/*
 * actually binds the device to an interrupt vector
 */
static int
simeth_open(struct net_device *dev)
{
	if (request_irq(dev->irq, simeth_interrupt, 0, "simeth", dev)) {
		printk(KERN_WARNING "simeth: unable to get IRQ %d.\n", dev->irq);
		return -EAGAIN;
	}

	netif_start_queue(dev);

	return 0;
}

/* copied from lapbether.c */
static __inline__ int dev_is_ethdev(struct net_device *dev)
{
       return ( dev->type == ARPHRD_ETHER && strncmp(dev->name, "dummy", 5));
}


/*
 * Handler for IFF_UP or IFF_DOWN
 *
 * The reason for that is that we don't want to be interrupted when the
 * interface is down. There is no way to unconnect in the simualtor. Instead
 * we use this function to shutdown packet processing in the frame filter
 * in the simulator. Thus no interrupts are generated
 *
 *
 * That's also the place where we pass the IP address of this device to the
 * simulator so that that we can start filtering packets for it
 *
 * There may be a better way of doing this, but I don't know which yet.
 */
static int
simeth_device_event(struct notifier_block *this,unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct simeth_local *local;
	struct in_device *in_dev;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	int r;


	if ( ! dev ) {
		printk(KERN_WARNING "simeth_device_event dev=0\n");
		return NOTIFY_DONE;
	}

	if (dev_net(dev) != &init_net)
		return NOTIFY_DONE;

	if ( event != NETDEV_UP && event != NETDEV_DOWN ) return NOTIFY_DONE;

	/*
	 * Check whether or not it's for an ethernet device
	 *
	 * XXX Fixme: This works only as long as we support one
	 * type of ethernet device.
	 */
	if ( !dev_is_ethdev(dev) ) return NOTIFY_DONE;

	if ((in_dev=dev->ip_ptr) != NULL) {
		for (ifap=&in_dev->ifa_list; (ifa=*ifap) != NULL; ifap=&ifa->ifa_next)
			if (strcmp(dev->name, ifa->ifa_label) == 0) break;
	}
	if ( ifa == NULL ) {
		printk(KERN_ERR "simeth_open: can't find device %s's ifa\n", dev->name);
		return NOTIFY_DONE;
	}

	printk(KERN_INFO "simeth_device_event: %s ipaddr=0x%x\n",
	       dev->name, ntohl(ifa->ifa_local));

	/*
	 * XXX Fix me
	 * if the device was up, and we're simply reconfiguring it, not sure
	 * we get DOWN then UP.
	 */

	local = dev->priv;
	/* now do it for real */
	r = event == NETDEV_UP ?
		netdev_attach(local->simfd, dev->irq, ntohl(ifa->ifa_local)):
		netdev_detach(local->simfd);

	printk(KERN_INFO "simeth: netdev_attach/detach: event=%s ->%d\n",
	       event == NETDEV_UP ? "attach":"detach", r);

	return NOTIFY_DONE;
}

static int
simeth_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	free_irq(dev->irq, dev);

	return 0;
}

/*
 * Only used for debug
 */
static void
frame_print(unsigned char *from, unsigned char *frame, int len)
{
	int i;

	printk("%s: (%d) %02x", from, len, frame[0] & 0xff);
	for(i=1; i < 6; i++ ) {
		printk(":%02x", frame[i] &0xff);
	}
	printk(" %2x", frame[6] &0xff);
	for(i=7; i < 12; i++ ) {
		printk(":%02x", frame[i] &0xff);
	}
	printk(" [%02x%02x]\n", frame[12], frame[13]);

	for(i=14; i < len; i++ ) {
		printk("%02x ", frame[i] &0xff);
		if ( (i%10)==0) printk("\n");
	}
	printk("\n");
}


/*
 * Function used to transmit of frame, very last one on the path before
 * going to the simulator.
 */
static int
simeth_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct simeth_local *local = dev->priv;

#if 0
	/* ensure we have at least ETH_ZLEN bytes (min frame size) */
	unsigned int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	/* Where do the extra padding bytes comes from inthe skbuff ? */
#else
	/* the real driver in the host system is going to take care of that
	 * or maybe it's the NIC itself.
	 */
	unsigned int length = skb->len;
#endif

	local->stats.tx_bytes += skb->len;
	local->stats.tx_packets++;


	if (simeth_debug > 5) frame_print("simeth_tx", skb->data, length);

	netdev_send(local->simfd, skb->data, length);

	/*
	 * we are synchronous on write, so we don't simulate a
	 * trasnmit complete interrupt, thus we don't need to arm a tx
	 */

	dev_kfree_skb(skb);
	return 0;
}

static inline struct sk_buff *
make_new_skb(struct net_device *dev)
{
	struct sk_buff *nskb;

	/*
	 * The +2 is used to make sure that the IP header is nicely
	 * aligned (on 4byte boundary I assume 14+2=16)
	 */
	nskb = dev_alloc_skb(SIMETH_FRAME_SIZE + 2);
	if ( nskb == NULL ) {
		printk(KERN_NOTICE "%s: memory squeeze. dropping packet.\n", dev->name);
		return NULL;
	}

	skb_reserve(nskb, 2);	/* Align IP on 16 byte boundaries */

	skb_put(nskb,SIMETH_FRAME_SIZE);

	return nskb;
}

/*
 * called from interrupt handler to process a received frame
 */
static int
simeth_rx(struct net_device *dev)
{
	struct simeth_local	*local;
	struct sk_buff		*skb;
	int			len;
	int			rcv_count = SIMETH_RECV_MAX;

	local = dev->priv;
	/*
	 * the loop concept has been borrowed from other drivers
	 * looks to me like it's a throttling thing to avoid pushing to many
	 * packets at one time into the stack. Making sure we can process them
	 * upstream and make forward progress overall
	 */
	do {
		if ( (skb=make_new_skb(dev)) == NULL ) {
			printk(KERN_NOTICE "%s: memory squeeze. dropping packet.\n", dev->name);
			local->stats.rx_dropped++;
			return 0;
		}
		/*
		 * Read only one frame at a time
		 */
		len = netdev_read(local->simfd, skb->data, SIMETH_FRAME_SIZE);
		if ( len == 0 ) {
			if ( simeth_debug > 0 ) printk(KERN_WARNING "%s: count=%d netdev_read=0\n",
						       dev->name, SIMETH_RECV_MAX-rcv_count);
			break;
		}
#if 0
		/*
		 * XXX Fix me
		 * Should really do a csum+copy here
		 */
		skb_copy_to_linear_data(skb, frame, len);
#endif
		skb->protocol = eth_type_trans(skb, dev);

		if ( simeth_debug > 6 ) frame_print("simeth_rx", skb->data, len);

		/*
		 * push the packet up & trigger software interrupt
		 */
		netif_rx(skb);

		local->stats.rx_packets++;
		local->stats.rx_bytes += len;

	} while ( --rcv_count );

	return len; /* 0 = nothing left to read, otherwise, we can try again */
}

/*
 * Interrupt handler (Yes, we can do it too !!!)
 */
static irqreturn_t
simeth_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;

	/*
	 * very simple loop because we get interrupts only when receiving
	 */
	while (simeth_rx(dev));
	return IRQ_HANDLED;
}

static struct net_device_stats *
simeth_get_stats(struct net_device *dev)
{
	struct simeth_local *local = dev->priv;

	return &local->stats;
}

/* fake multicast ability */
static void
set_multicast_list(struct net_device *dev)
{
	printk(KERN_WARNING "%s: set_multicast_list called\n", dev->name);
}

__initcall(simeth_probe);

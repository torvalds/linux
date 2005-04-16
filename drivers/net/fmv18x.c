/* fmv18x.c: A network device driver for the Fujitsu FMV-181/182/183/184.

	Original: at1700.c (1993-94 by Donald Becker).
		Copyright 1993 United States Government as represented by the
		Director, National Security Agency.
		The author may be reached as becker@scyld.com, or C/O
			Scyld Computing Corporation
			410 Severn Ave., Suite 210
			Annapolis MD 21403

	Modified by Yutaka TAMIYA (tamy@flab.fujitsu.co.jp)
		Copyright 1994 Fujitsu Laboratories Ltd.
	Special thanks to:
		Masayoshi UTAKA (utaka@ace.yk.fujitsu.co.jp)
			for testing this driver.
		H. NEGISHI (agy, negishi@sun45.psd.cs.fujitsu.co.jp)
			for suggestion of some program modification.
		Masahiro SEKIGUCHI <seki@sysrap.cs.fujitsu.co.jp>
			for suggestion of some program modification.
		Kazutoshi MORIOKA (morioka@aurora.oaks.cs.fujitsu.co.jp)
			for testing this driver.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	This is a device driver for the Fujitsu FMV-181/182/183/184, which
	is a straight-forward Fujitsu MB86965 implementation.

  Sources:
    at1700.c
    The Fujitsu MB86965 datasheet.
    The Fujitsu FMV-181/182 user's guide
*/

static const char version[] =
	"fmv18x.c:v2.2.0 09/24/98  Yutaka TAMIYA (tamy@flab.fujitsu.co.jp)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>

#define DRV_NAME "fmv18x"

static unsigned fmv18x_probe_list[] __initdata = {
	0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x300, 0x340, 0
};

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 1
#endif
static unsigned int net_debug = NET_DEBUG;

typedef unsigned char uchar;

/* Information that need to be kept for each board. */
struct net_local {
	struct net_device_stats stats;
	long open_time;				/* Useless example local info. */
	uint tx_started:1;			/* Number of packet on the Tx queue. */
	uint tx_queue_ready:1;		/* Tx queue is ready to be sent. */
	uint rx_started:1;			/* Packets are Rxing. */
	uchar tx_queue;				/* Number of packet on the Tx queue. */
	ushort tx_queue_len;		/* Current length of the Tx queue. */
	spinlock_t lock;
};


/* Offsets from the base address. */
#define STATUS			0
#define TX_STATUS		0
#define RX_STATUS		1
#define TX_INTR			2		/* Bit-mapped interrupt enable registers. */
#define RX_INTR			3
#define TX_MODE			4
#define RX_MODE			5
#define CONFIG_0		6		/* Misc. configuration settings. */
#define CONFIG_1		7
/* Run-time register bank 2 definitions. */
#define DATAPORT		8		/* Word-wide DMA or programmed-I/O dataport. */
#define TX_START		10
#define COL16CNTL		11		/* Controll Reg for 16 collisions */
#define MODE13			13
/* Fujitsu FMV-18x Card Configuration */
#define	FJ_STATUS0		0x10
#define	FJ_STATUS1		0x11
#define	FJ_CONFIG0		0x12
#define	FJ_CONFIG1		0x13
#define	FJ_MACADDR		0x14	/* 0x14 - 0x19 */
#define	FJ_BUFCNTL		0x1A
#define	FJ_BUFDATA		0x1C
#define FMV18X_IO_EXTENT	32

/* Index to functions, as function prototypes. */

static int fmv18x_probe1(struct net_device *dev, short ioaddr);
static int net_open(struct net_device *dev);
static int net_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t net_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void net_rx(struct net_device *dev);
static void net_timeout(struct net_device *dev);
static int net_close(struct net_device *dev);
static struct net_device_stats *net_get_stats(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);


/* Check for a network adaptor of this type, and return '0' iff one exists.
   If dev->base_addr == 0, probe all likely locations.
   If dev->base_addr == 1, always return failure.
   If dev->base_addr == 2, allocate space for the device and return success
   (detachable devices only).
   */

static int io = 0x220;
static int irq;

struct net_device * __init fmv18x_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct net_local));
	unsigned *port;
	int err = 0;

	if (!dev)
		return ERR_PTR(-ENODEV);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		io = dev->base_addr;
		irq = dev->irq;
	}

	SET_MODULE_OWNER(dev);

	if (io > 0x1ff) {	/* Check a single specified location. */
		err = fmv18x_probe1(dev, io);
	} else if (io != 0) {	/* Don't probe at all. */
		err = -ENXIO;
	} else {
		for (port = fmv18x_probe_list; *port; port++)
			if (fmv18x_probe1(dev, *port) == 0)
				break;
		if (!*port)
			err = -ENODEV;
	}
	if (err)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, FMV18X_IO_EXTENT);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

/* The Fujitsu datasheet suggests that the NIC be probed for by checking its
   "signature", the default bit pattern after a reset.  This *doesn't* work --
   there is no way to reset the bus interface without a complete power-cycle!

   It turns out that ATI came to the same conclusion I did: the only thing
   that can be done is checking a few bits and then diving right into MAC
   address check. */

static int __init fmv18x_probe1(struct net_device *dev, short ioaddr)
{
	char irqmap[4] = {3, 7, 10, 15};
	char irqmap_pnp[8] = {3, 4, 5, 7, 9, 10, 11, 15};
	unsigned int i, retval;
	struct net_local *lp;

	/* Resetting the chip doesn't reset the ISA interface, so don't bother.
	   That means we have to be careful with the register values we probe for.
	   */

	if (!request_region(ioaddr, FMV18X_IO_EXTENT, DRV_NAME))
		return -EBUSY;

	dev->irq = irq;
	dev->base_addr = ioaddr;

	/* Check I/O address configuration and Fujitsu vendor code */
	if (inb(ioaddr+FJ_MACADDR  ) != 0x00
	||  inb(ioaddr+FJ_MACADDR+1) != 0x00
	||  inb(ioaddr+FJ_MACADDR+2) != 0x0e) {
		retval = -ENODEV;
		goto out;
	}

	/* Check PnP mode for FMV-183/184/183A/184A. */
	/* This PnP routine is very poor. IO and IRQ should be known. */
	if (inb(ioaddr + FJ_STATUS1) & 0x20) {
		for (i = 0; i < 8; i++) {
			if (dev->irq == irqmap_pnp[i])
				break;
		}
		if (i == 8) {
			retval = -ENODEV;
			goto out;
		}
	} else {
		if (fmv18x_probe_list[inb(ioaddr + FJ_CONFIG0) & 0x07] != ioaddr)
			return -ENODEV;
		dev->irq = irqmap[(inb(ioaddr + FJ_CONFIG0)>>6) & 0x03];
	}

	/* Snarf the interrupt vector now. */
	retval = request_irq(dev->irq, &net_interrupt, 0, DRV_NAME, dev);
	if (retval) {
		printk ("FMV-18x found at %#3x, but it's unusable due to a conflict on"
				"IRQ %d.\n", ioaddr, dev->irq);
		goto out;
	}

	printk("%s: FMV-18x found at %#3x, IRQ %d, address ", dev->name,
		   ioaddr, dev->irq);

	for(i = 0; i < 6; i++) {
		unsigned char val = inb(ioaddr + FJ_MACADDR + i);
		printk("%02x", val);
		dev->dev_addr[i] = val;
	}

	/* "FJ_STATUS0" 12 bit 0x0400 means use regular 100 ohm 10baseT signals,
	   rather than 150 ohm shielded twisted pair compensation.
	   0x0000 == auto-sense the interface
	   0x0800 == use TP interface
	   0x1800 == use coax interface
	   */
	{
		const char *porttype[] = {"auto-sense", "10baseT", "auto-sense", "10base2/5"};
		ushort setup_value = inb(ioaddr + FJ_STATUS0);

		switch( setup_value & 0x07 ){
		case 0x01 /* 10base5 */:
		case 0x02 /* 10base2 */: dev->if_port = 0x18; break;
		case 0x04 /* 10baseT */: dev->if_port = 0x08; break;
		default /* auto-sense*/: dev->if_port = 0x00; break;
		}
		printk(" %s interface.\n", porttype[(dev->if_port>>3) & 3]);
	}

	/* Initialize LAN Controller and LAN Card */
	outb(0xda, ioaddr + CONFIG_0);	 /* Initialize LAN Controller */
	outb(0x00, ioaddr + CONFIG_1);	 /* Stand by mode */
	outb(0x00, ioaddr + FJ_CONFIG1); /* Disable IRQ of LAN Card */
	outb(0x00, ioaddr + FJ_BUFCNTL); /* Reset ? I'm not sure (TAMIYA) */

	/* wait for a while */
	udelay(200);

	/* Set the station address in bank zero. */
	outb(0x00, ioaddr + CONFIG_1);
	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + 8 + i);

	/* Switch to bank 1 and set the multicast table to accept none. */
	outb(0x04, ioaddr + CONFIG_1);
	for (i = 0; i < 8; i++)
		outb(0x00, ioaddr + 8 + i);

	/* Switch to bank 2 and lock our I/O address. */
	outb(0x08, ioaddr + CONFIG_1);
	outb(dev->if_port, ioaddr + MODE13);
	outb(0x00, ioaddr + COL16CNTL);

	if (net_debug)
		printk(version);

	/* Initialize the device structure. */
	dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	if (!dev->priv) {
		retval = -ENOMEM;
		goto out_irq;
	}
	memset(dev->priv, 0, sizeof(struct net_local));
	lp = dev->priv;
	spin_lock_init(&lp->lock);

	dev->open		= net_open;
	dev->stop		= net_close;
	dev->hard_start_xmit	= net_send_packet;
	dev->tx_timeout		= net_timeout;
	dev->watchdog_timeo	= HZ/10;
	dev->get_stats		= net_get_stats;
	dev->set_multicast_list = set_multicast_list;
	return 0;

out_irq:
	free_irq(dev->irq, dev);
out:
	release_region(ioaddr, FMV18X_IO_EXTENT);
	return retval;
}


static int net_open(struct net_device *dev)
{
	struct net_local *lp = dev->priv;
	int ioaddr = dev->base_addr;

	/* Set the configuration register 0 to 32K 100ns. byte-wide memory,
	   16 bit bus access, and two 4K Tx, enable the Rx and Tx. */
	outb(0x5a, ioaddr + CONFIG_0);

	/* Powerup and switch to register bank 2 for the run-time registers. */
	outb(0xe8, ioaddr + CONFIG_1);

	lp->tx_started = 0;
	lp->tx_queue_ready = 1;
	lp->rx_started = 0;
	lp->tx_queue = 0;
	lp->tx_queue_len = 0;

	/* Clear Tx and Rx Status */
	outb(0xff, ioaddr + TX_STATUS);
	outb(0xff, ioaddr + RX_STATUS);
	lp->open_time = jiffies;

	netif_start_queue(dev);
	
	/* Enable the IRQ of the LAN Card */
	outb(0x80, ioaddr + FJ_CONFIG1);

	/* Enable both Tx and Rx interrupts */
	outw(0x8182, ioaddr+TX_INTR);

	return 0;
}

static void net_timeout(struct net_device *dev)
{
	struct net_local *lp = dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;
	
	
	printk(KERN_WARNING "%s: transmit timed out with status %04x, %s?\n", dev->name,
		   htons(inw(ioaddr + TX_STATUS)),
		   inb(ioaddr + TX_STATUS) & 0x80
		   ? "IRQ conflict" : "network cable problem");
	printk(KERN_WARNING "%s: timeout registers: %04x %04x %04x %04x %04x %04x %04x %04x.\n",
		   dev->name, htons(inw(ioaddr + 0)),
		   htons(inw(ioaddr + 2)), htons(inw(ioaddr + 4)),
		   htons(inw(ioaddr + 6)), htons(inw(ioaddr + 8)),
		   htons(inw(ioaddr +10)), htons(inw(ioaddr +12)),
		   htons(inw(ioaddr +14)));
	printk(KERN_WARNING "eth card: %04x %04x\n",
		htons(inw(ioaddr+FJ_STATUS0)),
		htons(inw(ioaddr+FJ_CONFIG0)));
	lp->stats.tx_errors++;
	/* ToDo: We should try to restart the adaptor... */
	spin_lock_irqsave(&lp->lock, flags);

	/* Initialize LAN Controller and LAN Card */
	outb(0xda, ioaddr + CONFIG_0);   /* Initialize LAN Controller */
	outb(0x00, ioaddr + CONFIG_1);   /* Stand by mode */
	outb(0x00, ioaddr + FJ_CONFIG1); /* Disable IRQ of LAN Card */
	outb(0x00, ioaddr + FJ_BUFCNTL); /* Reset ? I'm not sure */
	net_open(dev);
	spin_unlock_irqrestore(&lp->lock, flags);

	netif_wake_queue(dev);
}

static int net_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp = dev->priv;
	int ioaddr = dev->base_addr;
	short length = skb->len;
	unsigned char *buf;
	unsigned long flags;

	/* Block a transmit from overlapping.  */
	
	if (length > ETH_FRAME_LEN) {
		if (net_debug)
			printk("%s: Attempting to send a large packet (%d bytes).\n",
				dev->name, length);
		return 1;
	}
	
	if (length < ETH_ZLEN) {
		skb = skb_padto(skb, ETH_ZLEN);
		if (skb == NULL)
			return 0;
		length = ETH_ZLEN;
	}
	buf = skb->data;
	
	if (net_debug > 4)
		printk("%s: Transmitting a packet of length %lu.\n", dev->name,
			   (unsigned long)skb->len);
	/* We may not start transmitting unless we finish transferring
	   a packet into the Tx queue. During executing the following
	   codes we possibly catch a Tx interrupt. Thus we flag off
	   tx_queue_ready, so that we prevent the interrupt routine
	   (net_interrupt) to start transmitting. */
	spin_lock_irqsave(&lp->lock, flags);
	lp->tx_queue_ready = 0;
	{
		outw(length, ioaddr + DATAPORT);
		outsw(ioaddr + DATAPORT, buf, (length + 1) >> 1);
		lp->tx_queue++;
		lp->tx_queue_len += length + 2;
	}
	lp->tx_queue_ready = 1;
	spin_unlock_irqrestore(&lp->lock, flags);

	if (lp->tx_started == 0) {
		/* If the Tx is idle, always trigger a transmit. */
		outb(0x80 | lp->tx_queue, ioaddr + TX_START);
		lp->tx_queue = 0;
		lp->tx_queue_len = 0;
		dev->trans_start = jiffies;
		lp->tx_started = 1;
	} else if (lp->tx_queue_len >= 4096 - 1502) /* No room for a packet */
		netif_stop_queue(dev);

	dev_kfree_skb(skb);
	return 0;
}

/* The typical workload of the driver:
   Handle the network interface interrupts. */
static irqreturn_t
net_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct net_local *lp;
	int ioaddr, status;

	ioaddr = dev->base_addr;
	lp = dev->priv;
	status = inw(ioaddr + TX_STATUS);
	outw(status, ioaddr + TX_STATUS);

	if (net_debug > 4)
		printk("%s: Interrupt with status %04x.\n", dev->name, status);
	if (lp->rx_started == 0 &&
		(status & 0xff00 || (inb(ioaddr + RX_MODE) & 0x40) == 0)) {
		/* Got a packet(s).
		   We cannot execute net_rx more than once at the same time for
		   the same device. During executing net_rx, we possibly catch a
		   Tx interrupt. Thus we flag on rx_started, so that we prevent
		   the interrupt routine (net_interrupt) to dive into net_rx
		   again. */
		lp->rx_started = 1;
		outb(0x00, ioaddr + RX_INTR);	/* Disable RX intr. */
		net_rx(dev);
		outb(0x81, ioaddr + RX_INTR);	/* Enable  RX intr. */
		lp->rx_started = 0;
	}
	if (status & 0x00ff) {
		if (status & 0x02) {
			/* More than 16 collisions occurred */
			if (net_debug > 4)
				printk("%s: 16 Collision occur during Txing.\n", dev->name);
			/* Cancel sending a packet. */
			outb(0x03, ioaddr + COL16CNTL);
			lp->stats.collisions++;
		}
		if (status & 0x82) {
			spin_lock(&lp->lock);
			lp->stats.tx_packets++;
			if (lp->tx_queue && lp->tx_queue_ready) {
				outb(0x80 | lp->tx_queue, ioaddr + TX_START);
				lp->tx_queue = 0;
				lp->tx_queue_len = 0;
				dev->trans_start = jiffies;
				netif_wake_queue(dev);	/* Inform upper layers. */
			} else {
				lp->tx_started = 0;
				netif_wake_queue(dev);	/* Inform upper layers. */
			}
			spin_unlock(&lp->lock);
		}
	}
	return IRQ_RETVAL(status);
}

/* We have a good packet(s), get it/them out of the buffers. */
static void net_rx(struct net_device *dev)
{
	struct net_local *lp = dev->priv;
	int ioaddr = dev->base_addr;
	int boguscount = 5;

	while ((inb(ioaddr + RX_MODE) & 0x40) == 0) {
		/* Clear PKT_RDY bit: by agy 19940922 */
		/* outb(0x80, ioaddr + RX_STATUS); */
		ushort status = inw(ioaddr + DATAPORT);

		if (net_debug > 4)
			printk("%s: Rxing packet mode %02x status %04x.\n",
				   dev->name, inb(ioaddr + RX_MODE), status);
#ifndef final_version
		if (status == 0) {
			outb(0x05, ioaddr + 14);
			break;
		}
#endif

		if ((status & 0xF0) != 0x20) {	/* There was an error. */
			lp->stats.rx_errors++;
			if (status & 0x08) lp->stats.rx_length_errors++;
			if (status & 0x04) lp->stats.rx_frame_errors++;
			if (status & 0x02) lp->stats.rx_crc_errors++;
			if (status & 0x01) lp->stats.rx_over_errors++;
		} else {
			ushort pkt_len = inw(ioaddr + DATAPORT);
			/* Malloc up new buffer. */
			struct sk_buff *skb;

			if (pkt_len > 1550) {
				printk("%s: The FMV-18x claimed a very large packet, size %d.\n",
					   dev->name, pkt_len);
				outb(0x05, ioaddr + 14);
				lp->stats.rx_errors++;
				break;
			}
			skb = dev_alloc_skb(pkt_len+3);
			if (skb == NULL) {
				printk("%s: Memory squeeze, dropping packet (len %d).\n",
					   dev->name, pkt_len);
				outb(0x05, ioaddr + 14);
				lp->stats.rx_dropped++;
				break;
			}
			skb->dev = dev;
			skb_reserve(skb,2);

			insw(ioaddr + DATAPORT, skb_put(skb,pkt_len), (pkt_len + 1) >> 1);

			if (net_debug > 5) {
				int i;
				printk("%s: Rxed packet of length %d: ", dev->name, pkt_len);
				for (i = 0; i < 14; i++)
					printk(" %02x", skb->data[i]);
				printk(".\n");
			}

			skb->protocol=eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pkt_len;
		}
		if (--boguscount <= 0)
			break;
	}

	/* If any worth-while packets have been received, dev_rint()
	   has done a mark_bh(NET_BH) for us and will work on them
	   when we get to the bottom-half routine. */
	{
		int i;
		for (i = 0; i < 20; i++) {
			if ((inb(ioaddr + RX_MODE) & 0x40) == 0x40)
				break;
			(void)inw(ioaddr + DATAPORT);				/* dummy status read */
			outb(0x05, ioaddr + 14);
		}

		if (net_debug > 5 && i > 0)
			printk("%s: Exint Rx packet with mode %02x after %d ticks.\n",
				   dev->name, inb(ioaddr + RX_MODE), i);
	}

	return;
}

/* The inverse routine to net_open(). */
static int net_close(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	((struct net_local *)dev->priv)->open_time = 0;

	netif_stop_queue(dev);
	
	/* Set configuration register 0 to disable Tx and Rx. */
	outb(0xda, ioaddr + CONFIG_0);

	/* Update the statistics -- ToDo. */

	/* Power-down the chip.  Green, green, green! */
	outb(0x00, ioaddr + CONFIG_1);

	/* Set the ethernet adaptor disable IRQ */
	outb(0x00, ioaddr + FJ_CONFIG1);

	return 0;
}

/* Get the current statistics.	This may be called with the card open or
   closed. */
static struct net_device_stats *net_get_stats(struct net_device *dev)
{
	struct net_local *lp = dev->priv;
	return &lp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
 */
 
static void set_multicast_list(struct net_device *dev)
{
	short ioaddr = dev->base_addr;
	if (dev->mc_count || dev->flags&(IFF_PROMISC|IFF_ALLMULTI))
	{
		/*
		 *	We must make the kernel realise we had to move
		 *	into promisc mode or we start all out war on
		 *	the cable. - AC
		 */
		dev->flags|=IFF_PROMISC;

		outb(3, ioaddr + RX_MODE);	/* Enable promiscuous mode */
	}
	else
		outb(2, ioaddr + RX_MODE);	/* Disable promiscuous, use normal mode */
}

#ifdef MODULE
static struct net_device *dev_fmv18x;

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(net_debug, "i");
MODULE_PARM_DESC(io, "FMV-18X I/O address");
MODULE_PARM_DESC(irq, "FMV-18X IRQ number");
MODULE_PARM_DESC(net_debug, "FMV-18X debug level (0-1,5-6)");
MODULE_LICENSE("GPL");

int init_module(void)
{
	if (io == 0)
		printk("fmv18x: You should not use auto-probing with insmod!\n");
	dev_fmv18x = fmv18x_probe(-1);
	if (IS_ERR(dev_fmv18x))
		return PTR_ERR(dev_fmv18x);
	return 0;
}

void
cleanup_module(void)
{
	unregister_netdev(dev_fmv18x);
	free_irq(dev_fmv18x->irq, dev_fmv18x);
	release_region(dev_fmv18x->base_addr, FMV18X_IO_EXTENT);
	free_netdev(dev_fmv18x);
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c fmv18x.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 *  c-indent-level: 4
 * End:
 */

/* seeq8005.c: A network driver for linux. */
/*
	Based on skeleton.c,
	Written 1993-94 by Donald Becker.
	See the skeleton.c file for further copyright information.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as hamish@zot.apana.org.au

	This file is a network device driver for the SEEQ 8005 chipset and
	the Linux operating system.

*/

static const char version[] =
	"seeq8005.c:v1.00 8/07/95 Hamish Coleman (hamish@zot.apana.org.au)\n";

/*
  Sources:
  	SEEQ 8005 databook

  Version history:
  	1.00	Public release. cosmetic changes (no warnings now)
  	0.68	Turning per- packet,interrupt debug messages off - testing for release.
  	0.67	timing problems/bad buffer reads seem to be fixed now
  	0.63	*!@$ protocol=eth_type_trans -- now packets flow
  	0.56	Send working
  	0.48	Receive working
*/

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
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>

#include "seeq8005.h"

/* First, a few definitions that the brave might change. */
/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int seeq8005_portlist[] __initdata =
   { 0x300, 0x320, 0x340, 0x360, 0};

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 1
#endif
static unsigned int net_debug = NET_DEBUG;

/* Information that need to be kept for each board. */
struct net_local {
	unsigned short receive_ptr;		/* What address in packet memory do we expect a recv_pkt_header? */
	long open_time;				/* Useless example local info. */
};

/* The station (ethernet) address prefix, used for IDing the board. */
#define SA_ADDR0 0x00
#define SA_ADDR1 0x80
#define SA_ADDR2 0x4b

/* Index to functions, as function prototypes. */

static int seeq8005_probe1(struct net_device *dev, int ioaddr);
static int seeq8005_open(struct net_device *dev);
static void seeq8005_timeout(struct net_device *dev);
static int seeq8005_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t seeq8005_interrupt(int irq, void *dev_id);
static void seeq8005_rx(struct net_device *dev);
static int seeq8005_close(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);

/* Example routines you must write ;->. */
#define tx_done(dev)	(inw(SEEQ_STATUS) & SEEQSTAT_TX_ON)
static void hardware_send_packet(struct net_device *dev, char *buf, int length);
extern void seeq8005_init(struct net_device *dev, int startp);
static inline void wait_for_buffer(struct net_device *dev);


/* Check for a network adaptor of this type, and return '0' iff one exists.
   If dev->base_addr == 0, probe all likely locations.
   If dev->base_addr == 1, always return failure.
   */

static int io = 0x320;
static int irq = 10;

struct net_device * __init seeq8005_probe(int unit)
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

	if (io > 0x1ff) {	/* Check a single specified location. */
		err = seeq8005_probe1(dev, io);
	} else if (io != 0) {	/* Don't probe at all. */
		err = -ENXIO;
	} else {
		for (port = seeq8005_portlist; *port; port++) {
			if (seeq8005_probe1(dev, *port) == 0)
				break;
		}
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
	release_region(dev->base_addr, SEEQ8005_IO_EXTENT);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

/* This is the real probe routine.  Linux has a history of friendly device
   probes on the ISA bus.  A good device probes avoids doing writes, and
   verifies that the correct device exists and functions.  */

static int __init seeq8005_probe1(struct net_device *dev, int ioaddr)
{
	static unsigned version_printed;
	int i,j;
	unsigned char SA_prom[32];
	int old_cfg1;
	int old_cfg2;
	int old_stat;
	int old_dmaar;
	int old_rear;
	int retval;
	DECLARE_MAC_BUF(mac);

	if (!request_region(ioaddr, SEEQ8005_IO_EXTENT, "seeq8005"))
		return -ENODEV;

	if (net_debug>1)
		printk("seeq8005: probing at 0x%x\n",ioaddr);

	old_stat = inw(SEEQ_STATUS);					/* read status register */
	if (old_stat == 0xffff) {
		retval = -ENODEV;
		goto out;						/* assume that 0xffff == no device */
	}
	if ( (old_stat & 0x1800) != 0x1800 ) {				/* assume that unused bits are 1, as my manual says */
		if (net_debug>1) {
			printk("seeq8005: reserved stat bits != 0x1800\n");
			printk("          == 0x%04x\n",old_stat);
		}
	 	retval = -ENODEV;
		goto out;
	}

	old_rear = inw(SEEQ_REA);
	if (old_rear == 0xffff) {
		outw(0,SEEQ_REA);
		if (inw(SEEQ_REA) == 0xffff) {				/* assume that 0xffff == no device */
			retval = -ENODEV;
			goto out;
		}
	} else if ((old_rear & 0xff00) != 0xff00) {			/* assume that unused bits are 1 */
		if (net_debug>1) {
			printk("seeq8005: unused rear bits != 0xff00\n");
			printk("          == 0x%04x\n",old_rear);
		}
		retval = -ENODEV;
		goto out;
	}

	old_cfg2 = inw(SEEQ_CFG2);					/* read CFG2 register */
	old_cfg1 = inw(SEEQ_CFG1);
	old_dmaar = inw(SEEQ_DMAAR);

	if (net_debug>4) {
		printk("seeq8005: stat = 0x%04x\n",old_stat);
		printk("seeq8005: cfg1 = 0x%04x\n",old_cfg1);
		printk("seeq8005: cfg2 = 0x%04x\n",old_cfg2);
		printk("seeq8005: raer = 0x%04x\n",old_rear);
		printk("seeq8005: dmaar= 0x%04x\n",old_dmaar);
	}

	outw( SEEQCMD_FIFO_WRITE | SEEQCMD_SET_ALL_OFF, SEEQ_CMD);	/* setup for reading PROM */
	outw( 0, SEEQ_DMAAR);						/* set starting PROM address */
	outw( SEEQCFG1_BUFFER_PROM, SEEQ_CFG1);				/* set buffer to look at PROM */


	j=0;
	for(i=0; i <32; i++) {
		j+= SA_prom[i] = inw(SEEQ_BUFFER) & 0xff;
	}

#if 0
	/* untested because I only have the one card */
	if ( (j&0xff) != 0 ) {						/* checksum appears to be 8bit = 0 */
		if (net_debug>1) {					/* check this before deciding that we have a card */
			printk("seeq8005: prom sum error\n");
		}
		outw( old_stat, SEEQ_STATUS);
		outw( old_dmaar, SEEQ_DMAAR);
		outw( old_cfg1, SEEQ_CFG1);
		retval = -ENODEV;
		goto out;
	}
#endif

	outw( SEEQCFG2_RESET, SEEQ_CFG2);				/* reset the card */
	udelay(5);
	outw( SEEQCMD_SET_ALL_OFF, SEEQ_CMD);

	if (net_debug) {
		printk("seeq8005: prom sum = 0x%08x\n",j);
		for(j=0; j<32; j+=16) {
			printk("seeq8005: prom %02x: ",j);
			for(i=0;i<16;i++) {
				printk("%02x ",SA_prom[j|i]);
			}
			printk(" ");
			for(i=0;i<16;i++) {
				if ((SA_prom[j|i]>31)&&(SA_prom[j|i]<127)) {
					printk("%c", SA_prom[j|i]);
				} else {
					printk(" ");
				}
			}
			printk("\n");
		}
	}

#if 0
	/*
	 * testing the packet buffer memory doesn't work yet
	 * but all other buffer accesses do
	 *			- fixing is not a priority
	 */
	if (net_debug>1) {					/* test packet buffer memory */
		printk("seeq8005: testing packet buffer ... ");
		outw( SEEQCFG1_BUFFER_BUFFER, SEEQ_CFG1);
		outw( SEEQCMD_FIFO_WRITE | SEEQCMD_SET_ALL_OFF, SEEQ_CMD);
		outw( 0 , SEEQ_DMAAR);
		for(i=0;i<32768;i++) {
			outw(0x5a5a, SEEQ_BUFFER);
		}
		j=jiffies+HZ;
		while ( ((inw(SEEQ_STATUS) & SEEQSTAT_FIFO_EMPTY) != SEEQSTAT_FIFO_EMPTY) && time_before(jiffies, j) )
			mb();
		outw( 0 , SEEQ_DMAAR);
		while ( ((inw(SEEQ_STATUS) & SEEQSTAT_WINDOW_INT) != SEEQSTAT_WINDOW_INT) && time_before(jiffies, j+HZ))
			mb();
		if ( (inw(SEEQ_STATUS) & SEEQSTAT_WINDOW_INT) == SEEQSTAT_WINDOW_INT)
			outw( SEEQCMD_WINDOW_INT_ACK | (inw(SEEQ_STATUS)& SEEQCMD_INT_MASK), SEEQ_CMD);
		outw( SEEQCMD_FIFO_READ | SEEQCMD_SET_ALL_OFF, SEEQ_CMD);
		j=0;
		for(i=0;i<32768;i++) {
			if (inw(SEEQ_BUFFER) != 0x5a5a)
				j++;
		}
		if (j) {
			printk("%i\n",j);
		} else {
			printk("ok.\n");
		}
	}
#endif

	if (net_debug  &&  version_printed++ == 0)
		printk(version);

	printk("%s: %s found at %#3x, ", dev->name, "seeq8005", ioaddr);

	/* Fill in the 'dev' fields. */
	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Retrieve and print the ethernet address. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = SA_prom[i+6];
	printk("%s", print_mac(mac, dev->dev_addr));

	if (dev->irq == 0xff)
		;			/* Do nothing: a user-level program will set it. */
	else if (dev->irq < 2) {	/* "Auto-IRQ" */
		unsigned long cookie = probe_irq_on();

		outw( SEEQCMD_RX_INT_EN | SEEQCMD_SET_RX_ON | SEEQCMD_SET_RX_OFF, SEEQ_CMD );

		dev->irq = probe_irq_off(cookie);

		if (net_debug >= 2)
			printk(" autoirq is %d\n", dev->irq);
	} else if (dev->irq == 2)
	  /* Fixup for users that don't know that IRQ 2 is really IRQ 9,
	   * or don't know which one to set.
	   */
	  dev->irq = 9;

#if 0
	{
		 int irqval = request_irq(dev->irq, &seeq8005_interrupt, 0, "seeq8005", dev);
		 if (irqval) {
			 printk ("%s: unable to get IRQ %d (irqval=%d).\n", dev->name,
					 dev->irq, irqval);
			 retval = -EAGAIN;
			 goto out;
		 }
	}
#endif
	dev->open		= seeq8005_open;
	dev->stop		= seeq8005_close;
	dev->hard_start_xmit 	= seeq8005_send_packet;
	dev->tx_timeout		= seeq8005_timeout;
	dev->watchdog_timeo	= HZ/20;
	dev->set_multicast_list = set_multicast_list;
	dev->flags &= ~IFF_MULTICAST;

	return 0;
out:
	release_region(ioaddr, SEEQ8005_IO_EXTENT);
	return retval;
}


/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
   */
static int seeq8005_open(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);

	{
		 int irqval = request_irq(dev->irq, &seeq8005_interrupt, 0, "seeq8005", dev);
		 if (irqval) {
			 printk ("%s: unable to get IRQ %d (irqval=%d).\n", dev->name,
					 dev->irq, irqval);
			 return -EAGAIN;
		 }
	}

	/* Reset the hardware here.  Don't forget to set the station address. */
	seeq8005_init(dev, 1);

	lp->open_time = jiffies;

	netif_start_queue(dev);
	return 0;
}

static void seeq8005_timeout(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	printk(KERN_WARNING "%s: transmit timed out, %s?\n", dev->name,
		   tx_done(dev) ? "IRQ conflict" : "network cable problem");
	/* Try to restart the adaptor. */
	seeq8005_init(dev, 1);
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static int seeq8005_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	short length = skb->len;
	unsigned char *buf;

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return 0;
		length = ETH_ZLEN;
	}
	buf = skb->data;

	/* Block a timer-based transmit from overlapping */
	netif_stop_queue(dev);

	hardware_send_packet(dev, buf, length);
	dev->trans_start = jiffies;
	dev->stats.tx_bytes += length;
	dev_kfree_skb (skb);
	/* You might need to clean up and record Tx statistics here. */

	return 0;
}

/*
 * wait_for_buffer
 *
 * This routine waits for the SEEQ chip to assert that the FIFO is ready
 * by checking for a window interrupt, and then clearing it. This has to
 * occur in the interrupt handler!
 */
inline void wait_for_buffer(struct net_device * dev)
{
	int ioaddr = dev->base_addr;
	unsigned long tmp;
	int status;

	tmp = jiffies + HZ;
	while ( ( ((status=inw(SEEQ_STATUS)) & SEEQSTAT_WINDOW_INT) != SEEQSTAT_WINDOW_INT) && time_before(jiffies, tmp))
		cpu_relax();

	if ( (status & SEEQSTAT_WINDOW_INT) == SEEQSTAT_WINDOW_INT)
		outw( SEEQCMD_WINDOW_INT_ACK | (status & SEEQCMD_INT_MASK), SEEQ_CMD);
}

/* The typical workload of the driver:
   Handle the network interface interrupts. */
static irqreturn_t seeq8005_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp;
	int ioaddr, status, boguscount = 0;
	int handled = 0;

	ioaddr = dev->base_addr;
	lp = netdev_priv(dev);

	status = inw(SEEQ_STATUS);
	do {
		if (net_debug >2) {
			printk("%s: int, status=0x%04x\n",dev->name,status);
		}

		if (status & SEEQSTAT_WINDOW_INT) {
			handled = 1;
			outw( SEEQCMD_WINDOW_INT_ACK | (status & SEEQCMD_INT_MASK), SEEQ_CMD);
			if (net_debug) {
				printk("%s: window int!\n",dev->name);
			}
		}
		if (status & SEEQSTAT_TX_INT) {
			handled = 1;
			outw( SEEQCMD_TX_INT_ACK | (status & SEEQCMD_INT_MASK), SEEQ_CMD);
			dev->stats.tx_packets++;
			netif_wake_queue(dev);	/* Inform upper layers. */
		}
		if (status & SEEQSTAT_RX_INT) {
			handled = 1;
			/* Got a packet(s). */
			seeq8005_rx(dev);
		}
		status = inw(SEEQ_STATUS);
	} while ( (++boguscount < 10) && (status & SEEQSTAT_ANY_INT)) ;

	if(net_debug>2) {
		printk("%s: eoi\n",dev->name);
	}
	return IRQ_RETVAL(handled);
}

/* We have a good packet(s), get it/them out of the buffers. */
static void seeq8005_rx(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int boguscount = 10;
	int pkt_hdr;
	int ioaddr = dev->base_addr;

	do {
		int next_packet;
		int pkt_len;
		int i;
		int status;

		status = inw(SEEQ_STATUS);
	  	outw( lp->receive_ptr, SEEQ_DMAAR);
		outw(SEEQCMD_FIFO_READ | SEEQCMD_RX_INT_ACK | (status & SEEQCMD_INT_MASK), SEEQ_CMD);
	  	wait_for_buffer(dev);
	  	next_packet = ntohs(inw(SEEQ_BUFFER));
	  	pkt_hdr = inw(SEEQ_BUFFER);

		if (net_debug>2) {
			printk("%s: 0x%04x recv next=0x%04x, hdr=0x%04x\n",dev->name,lp->receive_ptr,next_packet,pkt_hdr);
		}

		if ((next_packet == 0) || ((pkt_hdr & SEEQPKTH_CHAIN)==0)) {	/* Read all the frames? */
			return;							/* Done for now */
		}

		if ((pkt_hdr & SEEQPKTS_DONE)==0)
			break;

		if (next_packet < lp->receive_ptr) {
			pkt_len = (next_packet + 0x10000 - ((DEFAULT_TEA+1)<<8)) - lp->receive_ptr - 4;
		} else {
			pkt_len = next_packet - lp->receive_ptr - 4;
		}

		if (next_packet < ((DEFAULT_TEA+1)<<8)) {			/* is the next_packet address sane? */
			printk("%s: recv packet ring corrupt, resetting board\n",dev->name);
			seeq8005_init(dev,1);
			return;
		}

		lp->receive_ptr = next_packet;

		if (net_debug>2) {
			printk("%s: recv len=0x%04x\n",dev->name,pkt_len);
		}

		if (pkt_hdr & SEEQPKTS_ANY_ERROR) {				/* There was an error. */
			dev->stats.rx_errors++;
			if (pkt_hdr & SEEQPKTS_SHORT) dev->stats.rx_frame_errors++;
			if (pkt_hdr & SEEQPKTS_DRIB) dev->stats.rx_frame_errors++;
			if (pkt_hdr & SEEQPKTS_OVERSIZE) dev->stats.rx_over_errors++;
			if (pkt_hdr & SEEQPKTS_CRC_ERR) dev->stats.rx_crc_errors++;
			/* skip over this packet */
			outw( SEEQCMD_FIFO_WRITE | SEEQCMD_DMA_INT_ACK | (status & SEEQCMD_INT_MASK), SEEQ_CMD);
			outw( (lp->receive_ptr & 0xff00)>>8, SEEQ_REA);
		} else {
			/* Malloc up new buffer. */
			struct sk_buff *skb;
			unsigned char *buf;

			skb = dev_alloc_skb(pkt_len);
			if (skb == NULL) {
				printk("%s: Memory squeeze, dropping packet.\n", dev->name);
				dev->stats.rx_dropped++;
				break;
			}
			skb_reserve(skb, 2);	/* align data on 16 byte */
			buf = skb_put(skb,pkt_len);

			insw(SEEQ_BUFFER, buf, (pkt_len + 1) >> 1);

			if (net_debug>2) {
				char * p = buf;
				printk("%s: recv ",dev->name);
				for(i=0;i<14;i++) {
					printk("%02x ",*(p++)&0xff);
				}
				printk("\n");
			}

			skb->protocol=eth_type_trans(skb,dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;
		}
	} while ((--boguscount) && (pkt_hdr & SEEQPKTH_CHAIN));

	/* If any worth-while packets have been received, netif_rx()
	   has done a mark_bh(NET_BH) for us and will work on them
	   when we get to the bottom-half routine. */
	return;
}

/* The inverse routine to net_open(). */
static int seeq8005_close(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	lp->open_time = 0;

	netif_stop_queue(dev);

	/* Flush the Tx and disable Rx here. */
	outw( SEEQCMD_SET_ALL_OFF, SEEQ_CMD);

	free_irq(dev->irq, dev);

	/* Update the statistics here. */

	return 0;

}

/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
 */
static void set_multicast_list(struct net_device *dev)
{
/*
 * I _could_ do up to 6 addresses here, but won't (yet?)
 */

#if 0
	int ioaddr = dev->base_addr;
/*
 * hmm, not even sure if my matching works _anyway_ - seem to be receiving
 * _everything_ . . .
 */

	if (num_addrs) {			/* Enable promiscuous mode */
		outw( (inw(SEEQ_CFG1) & ~SEEQCFG1_MATCH_MASK)| SEEQCFG1_MATCH_ALL,  SEEQ_CFG1);
		dev->flags|=IFF_PROMISC;
	} else {				/* Disable promiscuous mode, use normal mode */
		outw( (inw(SEEQ_CFG1) & ~SEEQCFG1_MATCH_MASK)| SEEQCFG1_MATCH_BROAD, SEEQ_CFG1);
	}
#endif
}

void seeq8005_init(struct net_device *dev, int startp)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int i;

	outw(SEEQCFG2_RESET, SEEQ_CFG2);	/* reset device */
	udelay(5);

	outw( SEEQCMD_FIFO_WRITE | SEEQCMD_SET_ALL_OFF, SEEQ_CMD);
	outw( 0, SEEQ_DMAAR);			/* load start address into both low and high byte */
/*	wait_for_buffer(dev); */		/* I think that you only need a wait for memory buffer */
	outw( SEEQCFG1_BUFFER_MAC0, SEEQ_CFG1);

	for(i=0;i<6;i++) {			/* set Station address */
		outb(dev->dev_addr[i], SEEQ_BUFFER);
		udelay(2);
	}

	outw( SEEQCFG1_BUFFER_TEA, SEEQ_CFG1);	/* set xmit end area pointer to 16K */
	outb( DEFAULT_TEA, SEEQ_BUFFER);	/* this gives us 16K of send buffer and 48K of recv buffer */

	lp->receive_ptr = (DEFAULT_TEA+1)<<8;	/* so we can find our packet_header */
	outw( lp->receive_ptr, SEEQ_RPR);	/* Receive Pointer Register is set to recv buffer memory */

	outw( 0x00ff, SEEQ_REA);		/* Receive Area End */

	if (net_debug>4) {
		printk("%s: SA0 = ",dev->name);

		outw( SEEQCMD_FIFO_READ | SEEQCMD_SET_ALL_OFF, SEEQ_CMD);
		outw( 0, SEEQ_DMAAR);
		outw( SEEQCFG1_BUFFER_MAC0, SEEQ_CFG1);

		for(i=0;i<6;i++) {
			printk("%02x ",inb(SEEQ_BUFFER));
		}
		printk("\n");
	}

	outw( SEEQCFG1_MAC0_EN | SEEQCFG1_MATCH_BROAD | SEEQCFG1_BUFFER_BUFFER, SEEQ_CFG1);
	outw( SEEQCFG2_AUTO_REA | SEEQCFG2_CTRLO, SEEQ_CFG2);
	outw( SEEQCMD_SET_RX_ON | SEEQCMD_TX_INT_EN | SEEQCMD_RX_INT_EN, SEEQ_CMD);

	if (net_debug>4) {
		int old_cfg1;
		old_cfg1 = inw(SEEQ_CFG1);
		printk("%s: stat = 0x%04x\n",dev->name,inw(SEEQ_STATUS));
		printk("%s: cfg1 = 0x%04x\n",dev->name,old_cfg1);
		printk("%s: cfg2 = 0x%04x\n",dev->name,inw(SEEQ_CFG2));
		printk("%s: raer = 0x%04x\n",dev->name,inw(SEEQ_REA));
		printk("%s: dmaar= 0x%04x\n",dev->name,inw(SEEQ_DMAAR));

	}
}


static void hardware_send_packet(struct net_device * dev, char *buf, int length)
{
	int ioaddr = dev->base_addr;
	int status = inw(SEEQ_STATUS);
	int transmit_ptr = 0;
	unsigned long tmp;

	if (net_debug>4) {
		printk("%s: send 0x%04x\n",dev->name,length);
	}

	/* Set FIFO to writemode and set packet-buffer address */
	outw( SEEQCMD_FIFO_WRITE | (status & SEEQCMD_INT_MASK), SEEQ_CMD);
	outw( transmit_ptr, SEEQ_DMAAR);

	/* output SEEQ Packet header barfage */
	outw( htons(length + 4), SEEQ_BUFFER);
	outw( SEEQPKTH_XMIT | SEEQPKTH_DATA_FOLLOWS | SEEQPKTH_XMIT_INT_EN, SEEQ_BUFFER );

	/* blat the buffer */
	outsw( SEEQ_BUFFER, buf, (length +1) >> 1);
	/* paranoia !! */
	outw( 0, SEEQ_BUFFER);
	outw( 0, SEEQ_BUFFER);

	/* set address of start of transmit chain */
	outw( transmit_ptr, SEEQ_TPR);

	/* drain FIFO */
	tmp = jiffies;
	while ( (((status=inw(SEEQ_STATUS)) & SEEQSTAT_FIFO_EMPTY) == 0) && time_before(jiffies, tmp + HZ))
		mb();

	/* doit ! */
	outw( SEEQCMD_WINDOW_INT_ACK | SEEQCMD_SET_TX_ON | (status & SEEQCMD_INT_MASK), SEEQ_CMD);

}


#ifdef MODULE

static struct net_device *dev_seeq;
MODULE_LICENSE("GPL");
module_param(io, int, 0);
module_param(irq, int, 0);
MODULE_PARM_DESC(io, "SEEQ 8005 I/O base address");
MODULE_PARM_DESC(irq, "SEEQ 8005 IRQ number");

int __init init_module(void)
{
	dev_seeq = seeq8005_probe(-1);
	if (IS_ERR(dev_seeq))
		return PTR_ERR(dev_seeq);
	return 0;
}

void __exit cleanup_module(void)
{
	unregister_netdev(dev_seeq);
	release_region(dev_seeq->base_addr, SEEQ8005_IO_EXTENT);
	free_netdev(dev_seeq);
}

#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c skeleton.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */

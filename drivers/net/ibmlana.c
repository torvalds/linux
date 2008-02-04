/*
net-3-driver for the IBM LAN Adapter/A

This is an extension to the Linux operating system, and is covered by the
same GNU General Public License that covers that work.

Copyright 1999 by Alfred Arnold (alfred@ccac.rwth-aachen.de,
                                 alfred.arnold@lancom.de)

This driver is based both on the SK_MCA driver, which is itself based on the
SK_G16 and 3C523 driver.

paper sources:
  'PC Hardware: Aufbau, Funktionsweise, Programmierung' by
  Hans-Peter Messmer for the basic Microchannel stuff

  'Linux Geraetetreiber' by Allesandro Rubini, Kalle Dalheimer
  for help on Ethernet driver programming

  'DP83934CVUL-20/25 MHz SONIC-T Ethernet Controller Datasheet' by National
  Semiconductor for info on the MAC chip

  'LAN Technical Reference Ethernet Adapter Interface Version 1 Release 1.0
   Document Number SC30-3661-00' by IBM for info on the adapter itself

  Also see http://www.natsemi.com/

special acknowledgements to:
  - Bob Eager for helping me out with documentation from IBM
  - Jim Shorney for his endless patience with me while I was using
    him as a beta tester to trace down the address filter bug ;-)

  Missing things:

  -> set debug level via ioctl instead of compile-time switches
  -> I didn't follow the development of the 2.1.x kernels, so my
     assumptions about which things changed with which kernel version
     are probably nonsense

History:
  Nov 6th, 1999
  	startup from SK_MCA driver
  Dec 6th, 1999
	finally got docs about the card.  A big thank you to Bob Eager!
  Dec 12th, 1999
	first packet received
  Dec 13th, 1999
	recv queue done, tcpdump works
  Dec 15th, 1999
	transmission part works
  Dec 28th, 1999
	added usage of the isa_functions for Linux 2.3 .  Things should
	still work with 2.0.x....
  Jan 28th, 2000
	in Linux 2.2.13, the version.h file mysteriously didn't get
	included.  Added a workaround for this.  Futhermore, it now
	not only compiles as a modules ;-)
  Jan 30th, 2000
	newer kernels automatically probe more than one board, so the
	'startslot' as a variable is also needed here
  Apr 12th, 2000
	the interrupt mask register is not set 'hard' instead of individually
	setting registers, since this seems to set bits that shouldn't be
	set
  May 21st, 2000
	reset interrupt status immediately after CAM load
	add a recovery delay after releasing the chip's reset line
  May 24th, 2000
	finally found the bug in the address filter setup - damned signed
        chars!
  June 1st, 2000
	corrected version codes, added support for the latest 2.3 changes
  Oct 28th, 2002
  	cleaned up for the 2.5 tree <alan@redhat.com>

 *************************************************************************/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/mca.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/io.h>

#define _IBM_LANA_DRIVER_
#include "ibmlana.h"

#undef DEBUG

#define DRV_NAME "ibmlana"

/* ------------------------------------------------------------------------
 * global static data - not more since we can handle multiple boards and
 * have to pack all state info into the device struct!
 * ------------------------------------------------------------------------ */

static char *MediaNames[Media_Count] = {
	"10BaseT", "10Base5", "Unknown", "10Base2"
};

/* ------------------------------------------------------------------------
 * private subfunctions
 * ------------------------------------------------------------------------ */

#ifdef DEBUG
  /* dump all registers */

static void dumpregs(struct net_device *dev)
{
	int z;

	for (z = 0; z < 160; z += 2) {
		if (!(z & 15))
			printk("REGS: %04x:", z);
		printk(" %04x", inw(dev->base_addr + z));
		if ((z & 15) == 14)
			printk("\n");
	}
}

/* dump parts of shared memory - only needed during debugging */

static void dumpmem(struct net_device *dev, u32 start, u32 len)
{
	ibmlana_priv *priv = netdev_priv(dev);
	int z;

	printk("Address %04x:\n", start);
	for (z = 0; z < len; z++) {
		if ((z & 15) == 0)
			printk("%04x:", z);
		printk(" %02x", readb(priv->base + start + z));
		if ((z & 15) == 15)
			printk("\n");
	}
	if ((z & 15) != 0)
		printk("\n");
}

/* print exact time - ditto */

static void PrTime(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	printk("%9d:%06d: ", (int) tv.tv_sec, (int) tv.tv_usec);
}
#endif				/* DEBUG */

/* deduce resources out of POS registers */

static void getaddrs(struct mca_device *mdev, int *base, int *memlen,
		     int *iobase, int *irq, ibmlana_medium *medium)
{
	u_char pos0, pos1;

	pos0 = mca_device_read_stored_pos(mdev, 2);
	pos1 = mca_device_read_stored_pos(mdev, 3);

	*base = 0xc0000 + ((pos1 & 0xf0) << 9);
	*memlen = (pos1 & 0x01) ? 0x8000 : 0x4000;
	*iobase = (pos0 & 0xe0) << 7;
	switch (pos0 & 0x06) {
	case 0:
		*irq = 5;
		break;
	case 2:
		*irq = 15;
		break;
	case 4:
		*irq = 10;
		break;
	case 6:
		*irq = 11;
		break;
	}
	*medium = (pos0 & 0x18) >> 3;
}

/* wait on register value with mask and timeout */

static int wait_timeout(struct net_device *dev, int regoffs, u16 mask,
			u16 value, int timeout)
{
	unsigned long fin = jiffies + timeout;

	while (time_before(jiffies,fin))
		if ((inw(dev->base_addr + regoffs) & mask) == value)
			return 1;

	return 0;
}


/* reset the whole board */

static void ResetBoard(struct net_device *dev)
{
	unsigned char bcmval;

	/* read original board control value */

	bcmval = inb(dev->base_addr + BCMREG);

	/* set reset bit for a while */

	bcmval |= BCMREG_RESET;
	outb(bcmval, dev->base_addr + BCMREG);
	udelay(10);
	bcmval &= ~BCMREG_RESET;
	outb(bcmval, dev->base_addr + BCMREG);

	/* switch over to RAM again */

	bcmval |= BCMREG_RAMEN | BCMREG_RAMWIN;
	outb(bcmval, dev->base_addr + BCMREG);
}

/* calculate RAM layout & set up descriptors in RAM */

static void InitDscrs(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);
	u32 addr, baddr, raddr;
	int z;
	tda_t tda;
	rda_t rda;
	rra_t rra;

	/* initialize RAM */

	memset_io(priv->base, 0xaa,
		      dev->mem_start - dev->mem_start);	/* XXX: typo? */

	/* setup n TX descriptors - independent of RAM size */

	priv->tdastart = addr = 0;
	priv->txbufstart = baddr = sizeof(tda_t) * TXBUFCNT;
	for (z = 0; z < TXBUFCNT; z++) {
		tda.status = 0;
		tda.config = 0;
		tda.length = 0;
		tda.fragcount = 1;
		tda.startlo = baddr;
		tda.starthi = 0;
		tda.fraglength = 0;
		if (z == TXBUFCNT - 1)
			tda.link = priv->tdastart;
		else
			tda.link = addr + sizeof(tda_t);
		tda.link |= 1;
		memcpy_toio(priv->base + addr, &tda, sizeof(tda_t));
		addr += sizeof(tda_t);
		baddr += PKTSIZE;
	}

	/* calculate how many receive buffers fit into remaining memory */

	priv->rxbufcnt = (dev->mem_end - dev->mem_start - baddr) / (sizeof(rra_t) + sizeof(rda_t) + PKTSIZE);

	/* calculate receive addresses */

	priv->rrastart = raddr = priv->txbufstart + (TXBUFCNT * PKTSIZE);
	priv->rdastart = addr = priv->rrastart + (priv->rxbufcnt * sizeof(rra_t));
	priv->rxbufstart = baddr = priv->rdastart + (priv->rxbufcnt * sizeof(rda_t));

	for (z = 0; z < priv->rxbufcnt; z++) {
		rra.startlo = baddr;
		rra.starthi = 0;
		rra.cntlo = PKTSIZE >> 1;
		rra.cnthi = 0;
		memcpy_toio(priv->base + raddr, &rra, sizeof(rra_t));

		rda.status = 0;
		rda.length = 0;
		rda.startlo = 0;
		rda.starthi = 0;
		rda.seqno = 0;
		if (z < priv->rxbufcnt - 1)
			rda.link = addr + sizeof(rda_t);
		else
			rda.link = 1;
		rda.inuse = 1;
		memcpy_toio(priv->base + addr, &rda, sizeof(rda_t));

		baddr += PKTSIZE;
		raddr += sizeof(rra_t);
		addr += sizeof(rda_t);
	}

	/* initialize current pointers */

	priv->nextrxdescr = 0;
	priv->lastrxdescr = priv->rxbufcnt - 1;
	priv->nexttxdescr = 0;
	priv->currtxdescr = 0;
	priv->txusedcnt = 0;
	memset(priv->txused, 0, sizeof(priv->txused));
}

/* set up Rx + Tx descriptors in SONIC */

static int InitSONIC(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);

	/* set up start & end of resource area */

	outw(0, SONIC_URRA);
	outw(priv->rrastart, dev->base_addr + SONIC_RSA);
	outw(priv->rrastart + (priv->rxbufcnt * sizeof(rra_t)), dev->base_addr + SONIC_REA);
	outw(priv->rrastart, dev->base_addr + SONIC_RRP);
	outw(priv->rrastart, dev->base_addr + SONIC_RWP);

	/* set EOBC so that only one packet goes into one buffer */

	outw((PKTSIZE - 4) >> 1, dev->base_addr + SONIC_EOBC);

	/* let SONIC read the first RRA descriptor */

	outw(CMDREG_RRRA, dev->base_addr + SONIC_CMDREG);
	if (!wait_timeout(dev, SONIC_CMDREG, CMDREG_RRRA, 0, 2)) {
		printk(KERN_ERR "%s: SONIC did not respond on RRRA command - giving up.", dev->name);
		return 0;
	}

	/* point SONIC to the first RDA */

	outw(0, dev->base_addr + SONIC_URDA);
	outw(priv->rdastart, dev->base_addr + SONIC_CRDA);

	/* set upper half of TDA address */

	outw(0, dev->base_addr + SONIC_UTDA);

	return 1;
}

/* stop SONIC so we can reinitialize it */

static void StopSONIC(struct net_device *dev)
{
	/* disable interrupts */

	outb(inb(dev->base_addr + BCMREG) & (~BCMREG_IEN), dev->base_addr + BCMREG);
	outb(0, dev->base_addr + SONIC_IMREG);

	/* reset the SONIC */

	outw(CMDREG_RST, dev->base_addr + SONIC_CMDREG);
	udelay(10);
	outw(CMDREG_RST, dev->base_addr + SONIC_CMDREG);
}

/* initialize card and SONIC for proper operation */

static void putcam(camentry_t * cams, int *camcnt, char *addr)
{
	camentry_t *pcam = cams + (*camcnt);
	u8 *uaddr = (u8 *) addr;

	pcam->index = *camcnt;
	pcam->addr0 = (((u16) uaddr[1]) << 8) | uaddr[0];
	pcam->addr1 = (((u16) uaddr[3]) << 8) | uaddr[2];
	pcam->addr2 = (((u16) uaddr[5]) << 8) | uaddr[4];
	(*camcnt)++;
}

static void InitBoard(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);
	int camcnt;
	camentry_t cams[16];
	u32 cammask;
	struct dev_mc_list *mcptr;
	u16 rcrval;

	/* reset the SONIC */

	outw(CMDREG_RST, dev->base_addr + SONIC_CMDREG);
	udelay(10);

	/* clear all spurious interrupts */

	outw(inw(dev->base_addr + SONIC_ISREG), dev->base_addr + SONIC_ISREG);

	/* set up the SONIC's bus interface - constant for this adapter -
	   must be done while the SONIC is in reset */

	outw(DCREG_USR1 | DCREG_USR0 | DCREG_WC1 | DCREG_DW32, dev->base_addr + SONIC_DCREG);
	outw(0, dev->base_addr + SONIC_DCREG2);

	/* remove reset form the SONIC */

	outw(0, dev->base_addr + SONIC_CMDREG);
	udelay(10);

	/* data sheet requires URRA to be programmed before setting up the CAM contents */

	outw(0, dev->base_addr + SONIC_URRA);

	/* program the CAM entry 0 to the device address */

	camcnt = 0;
	putcam(cams, &camcnt, dev->dev_addr);

	/* start putting the multicast addresses into the CAM list.  Stop if
	   it is full. */

	for (mcptr = dev->mc_list; mcptr != NULL; mcptr = mcptr->next) {
		putcam(cams, &camcnt, mcptr->dmi_addr);
		if (camcnt == 16)
			break;
	}

	/* calculate CAM mask */

	cammask = (1 << camcnt) - 1;

	/* feed CDA into SONIC, initialize RCR value (always get broadcasts) */

	memcpy_toio(priv->base, cams, sizeof(camentry_t) * camcnt);
	memcpy_toio(priv->base + (sizeof(camentry_t) * camcnt), &cammask, sizeof(cammask));

#ifdef DEBUG
	printk("CAM setup:\n");
	dumpmem(dev, 0, sizeof(camentry_t) * camcnt + sizeof(cammask));
#endif

	outw(0, dev->base_addr + SONIC_CAMPTR);
	outw(camcnt, dev->base_addr + SONIC_CAMCNT);
	outw(CMDREG_LCAM, dev->base_addr + SONIC_CMDREG);
	if (!wait_timeout(dev, SONIC_CMDREG, CMDREG_LCAM, 0, 2)) {
		printk(KERN_ERR "%s:SONIC did not respond on LCAM command - giving up.", dev->name);
		return;
	} else {
		/* clear interrupt condition */

		outw(ISREG_LCD, dev->base_addr + SONIC_ISREG);

#ifdef DEBUG
		printk("Loading CAM done, address pointers %04x:%04x\n",
		       inw(dev->base_addr + SONIC_URRA),
		       inw(dev->base_addr + SONIC_CAMPTR));
		{
			int z;

			printk("\n-->CAM: PTR %04x CNT %04x\n",
			       inw(dev->base_addr + SONIC_CAMPTR),
			       inw(dev->base_addr + SONIC_CAMCNT));
			outw(CMDREG_RST, dev->base_addr + SONIC_CMDREG);
			for (z = 0; z < camcnt; z++) {
				outw(z, dev->base_addr + SONIC_CAMEPTR);
				printk("Entry %d: %04x %04x %04x\n", z,
				       inw(dev->base_addr + SONIC_CAMADDR0),
				       inw(dev->base_addr + SONIC_CAMADDR1),
				       inw(dev->base_addr + SONIC_CAMADDR2));
			}
			outw(0, dev->base_addr + SONIC_CMDREG);
		}
#endif
	}

	rcrval = RCREG_BRD | RCREG_LB_NONE;

	/* if still multicast addresses left or ALLMULTI is set, set the multicast
	   enable bit */

	if ((dev->flags & IFF_ALLMULTI) || (mcptr != NULL))
		rcrval |= RCREG_AMC;

	/* promiscous mode ? */

	if (dev->flags & IFF_PROMISC)
		rcrval |= RCREG_PRO;

	/* program receive mode */

	outw(rcrval, dev->base_addr + SONIC_RCREG);
#ifdef DEBUG
	printk("\nRCRVAL: %04x\n", rcrval);
#endif

	/* set up descriptors in shared memory + feed them into SONIC registers */

	InitDscrs(dev);
	if (!InitSONIC(dev))
		return;

	/* reset all pending interrupts */

	outw(0xffff, dev->base_addr + SONIC_ISREG);

	/* enable transmitter + receiver interrupts */

	outw(CMDREG_RXEN, dev->base_addr + SONIC_CMDREG);
	outw(IMREG_PRXEN | IMREG_RBEEN | IMREG_PTXEN | IMREG_TXEREN, dev->base_addr + SONIC_IMREG);

	/* turn on card interrupts */

	outb(inb(dev->base_addr + BCMREG) | BCMREG_IEN, dev->base_addr + BCMREG);

#ifdef DEBUG
	printk("Register dump after initialization:\n");
	dumpregs(dev);
#endif
}

/* start transmission of a descriptor */

static void StartTx(struct net_device *dev, int descr)
{
	ibmlana_priv *priv = netdev_priv(dev);
	int addr;

	addr = priv->tdastart + (descr * sizeof(tda_t));

	/* put descriptor address into SONIC */

	outw(addr, dev->base_addr + SONIC_CTDA);

	/* trigger transmitter */

	priv->currtxdescr = descr;
	outw(CMDREG_TXP, dev->base_addr + SONIC_CMDREG);
}

/* ------------------------------------------------------------------------
 * interrupt handler(s)
 * ------------------------------------------------------------------------ */

/* receive buffer area exhausted */

static void irqrbe_handler(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);

	/* point the SONIC back to the RRA start */

	outw(priv->rrastart, dev->base_addr + SONIC_RRP);
	outw(priv->rrastart, dev->base_addr + SONIC_RWP);
}

/* receive interrupt */

static void irqrx_handler(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);
	rda_t rda;
	u32 rdaaddr, lrdaaddr;

	/* loop until ... */

	while (1) {
		/* read descriptor that was next to be filled by SONIC */

		rdaaddr = priv->rdastart + (priv->nextrxdescr * sizeof(rda_t));
		lrdaaddr = priv->rdastart + (priv->lastrxdescr * sizeof(rda_t));
		memcpy_fromio(&rda, priv->base + rdaaddr, sizeof(rda_t));

		/* iron out upper word halves of fields we use - SONIC will duplicate
		   bits 0..15 to 16..31 */

		rda.status &= 0xffff;
		rda.length &= 0xffff;
		rda.startlo &= 0xffff;

		/* stop if the SONIC still owns it, i.e. there is no data for us */

		if (rda.inuse)
			break;

		/* good packet? */

		else if (rda.status & RCREG_PRX) {
			struct sk_buff *skb;

			/* fetch buffer */

			skb = dev_alloc_skb(rda.length + 2);
			if (skb == NULL)
				dev->stats.rx_dropped++;
			else {
				/* copy out data */

				memcpy_fromio(skb_put(skb, rda.length),
					       priv->base +
					       rda.startlo, rda.length);

				/* set up skb fields */

				skb->protocol = eth_type_trans(skb, dev);
				skb->ip_summed = CHECKSUM_NONE;

				/* bookkeeping */
				dev->last_rx = jiffies;
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += rda.length;

				/* pass to the upper layers */
				netif_rx(skb);
			}
		}

		/* otherwise check error status bits and increase statistics */

		else {
			dev->stats.rx_errors++;
			if (rda.status & RCREG_FAER)
				dev->stats.rx_frame_errors++;
			if (rda.status & RCREG_CRCR)
				dev->stats.rx_crc_errors++;
		}

		/* descriptor processed, will become new last descriptor in queue */

		rda.link = 1;
		rda.inuse = 1;
		memcpy_toio(priv->base + rdaaddr, &rda,
			     sizeof(rda_t));

		/* set up link and EOL = 0 in currently last descriptor. Only write
		   the link field since the SONIC may currently already access the
		   other fields. */

		memcpy_toio(priv->base + lrdaaddr + 20, &rdaaddr, 4);

		/* advance indices */

		priv->lastrxdescr = priv->nextrxdescr;
		if ((++priv->nextrxdescr) >= priv->rxbufcnt)
			priv->nextrxdescr = 0;
	}
}

/* transmit interrupt */

static void irqtx_handler(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);
	tda_t tda;

	/* fetch descriptor (we forgot the size ;-) */
	memcpy_fromio(&tda, priv->base + priv->tdastart + (priv->currtxdescr * sizeof(tda_t)), sizeof(tda_t));

	/* update statistics */
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += tda.length;

	/* update our pointers */
	priv->txused[priv->currtxdescr] = 0;
	priv->txusedcnt--;

	/* if there are more descriptors present in RAM, start them */
	if (priv->txusedcnt > 0)
		StartTx(dev, (priv->currtxdescr + 1) % TXBUFCNT);

	/* tell the upper layer we can go on transmitting */
	netif_wake_queue(dev);
}

static void irqtxerr_handler(struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);
	tda_t tda;

	/* fetch descriptor to check status */
	memcpy_fromio(&tda, priv->base + priv->tdastart + (priv->currtxdescr * sizeof(tda_t)), sizeof(tda_t));

	/* update statistics */
	dev->stats.tx_errors++;
	if (tda.status & (TCREG_NCRS | TCREG_CRSL))
		dev->stats.tx_carrier_errors++;
	if (tda.status & TCREG_EXC)
		dev->stats.tx_aborted_errors++;
	if (tda.status & TCREG_OWC)
		dev->stats.tx_window_errors++;
	if (tda.status & TCREG_FU)
		dev->stats.tx_fifo_errors++;

	/* update our pointers */
	priv->txused[priv->currtxdescr] = 0;
	priv->txusedcnt--;

	/* if there are more descriptors present in RAM, start them */
	if (priv->txusedcnt > 0)
		StartTx(dev, (priv->currtxdescr + 1) % TXBUFCNT);

	/* tell the upper layer we can go on transmitting */
	netif_wake_queue(dev);
}

/* general interrupt entry */

static irqreturn_t irq_handler(int dummy, void *device)
{
	struct net_device *dev = device;
	u16 ival;

	/* in case we're not meant... */
	if (!(inb(dev->base_addr + BCMREG) & BCMREG_IPEND))
		return IRQ_NONE;

	/* loop through the interrupt bits until everything is clear */
	while (1) {
		ival = inw(dev->base_addr + SONIC_ISREG);

		if (ival & ISREG_RBE) {
			irqrbe_handler(dev);
			outw(ISREG_RBE, dev->base_addr + SONIC_ISREG);
		}
		if (ival & ISREG_PKTRX) {
			irqrx_handler(dev);
			outw(ISREG_PKTRX, dev->base_addr + SONIC_ISREG);
		}
		if (ival & ISREG_TXDN) {
			irqtx_handler(dev);
			outw(ISREG_TXDN, dev->base_addr + SONIC_ISREG);
		}
		if (ival & ISREG_TXER) {
			irqtxerr_handler(dev);
			outw(ISREG_TXER, dev->base_addr + SONIC_ISREG);
		}
		break;
	}
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------------
 * driver methods
 * ------------------------------------------------------------------------ */

/* MCA info */

#if 0 /* info available elsewhere, but this is kept for reference */
static int ibmlana_getinfo(char *buf, int slot, void *d)
{
	int len = 0, i;
	struct net_device *dev = (struct net_device *) d;
	ibmlana_priv *priv;

	/* can't say anything about an uninitialized device... */

	if (dev == NULL)
		return len;
	priv = netdev_priv(dev);

	/* print info */

	len += sprintf(buf + len, "IRQ: %d\n", priv->realirq);
	len += sprintf(buf + len, "I/O: %#lx\n", dev->base_addr);
	len += sprintf(buf + len, "Memory: %#lx-%#lx\n", dev->mem_start, dev->mem_end - 1);
	len += sprintf(buf + len, "Transceiver: %s\n", MediaNames[priv->medium]);
	len += sprintf(buf + len, "Device: %s\n", dev->name);
	len += sprintf(buf + len, "MAC address:");
	for (i = 0; i < 6; i++)
		len += sprintf(buf + len, " %02x", dev->dev_addr[i]);
	buf[len++] = '\n';
	buf[len] = 0;

	return len;
}
#endif

/* open driver.  Means also initialization and start of LANCE */

static int ibmlana_open(struct net_device *dev)
{
	int result;
	ibmlana_priv *priv = netdev_priv(dev);

	/* register resources - only necessary for IRQ */

	result = request_irq(priv->realirq, irq_handler, IRQF_SHARED | IRQF_SAMPLE_RANDOM, dev->name, dev);
	if (result != 0) {
		printk(KERN_ERR "%s: failed to register irq %d\n", dev->name, dev->irq);
		return result;
	}
	dev->irq = priv->realirq;

	/* set up the card and SONIC */
	InitBoard(dev);

	/* initialize operational flags */
	netif_start_queue(dev);
	return 0;
}

/* close driver.  Shut down board and free allocated resources */

static int ibmlana_close(struct net_device *dev)
{
	/* turn off board */

	/* release resources */
	if (dev->irq != 0)
		free_irq(dev->irq, dev);
	dev->irq = 0;
	return 0;
}

/* transmit a block. */

static int ibmlana_tx(struct sk_buff *skb, struct net_device *dev)
{
	ibmlana_priv *priv = netdev_priv(dev);
	int retval = 0, tmplen, addr;
	unsigned long flags;
	tda_t tda;
	int baddr;

	/* find out if there are free slots for a frame to transmit. If not,
	   the upper layer is in deep desperation and we simply ignore the frame. */

	if (priv->txusedcnt >= TXBUFCNT) {
		retval = -EIO;
		dev->stats.tx_dropped++;
		goto tx_done;
	}

	/* copy the frame data into the next free transmit buffer - fillup missing */
	tmplen = skb->len;
	if (tmplen < 60)
		tmplen = 60;
	baddr = priv->txbufstart + (priv->nexttxdescr * PKTSIZE);
	memcpy_toio(priv->base + baddr, skb->data, skb->len);

	/* copy filler into RAM - in case we're filling up...
	   we're filling a bit more than necessary, but that doesn't harm
	   since the buffer is far larger...
	   Sorry Linus for the filler string but I couldn't resist ;-) */

	if (tmplen > skb->len) {
		char *fill = "NetBSD is a nice OS too! ";
		unsigned int destoffs = skb->len, l = strlen(fill);

		while (destoffs < tmplen) {
			memcpy_toio(priv->base + baddr + destoffs, fill, l);
			destoffs += l;
		}
	}

	/* set up the new frame descriptor */
	addr = priv->tdastart + (priv->nexttxdescr * sizeof(tda_t));
	memcpy_fromio(&tda, priv->base + addr, sizeof(tda_t));
	tda.length = tda.fraglength = tmplen;
	memcpy_toio(priv->base + addr, &tda, sizeof(tda_t));

	/* if there were no active descriptors, trigger the SONIC */
	spin_lock_irqsave(&priv->lock, flags);

	priv->txusedcnt++;
	priv->txused[priv->nexttxdescr] = 1;

	/* are all transmission slots used up ? */
	if (priv->txusedcnt >= TXBUFCNT)
		netif_stop_queue(dev);

	if (priv->txusedcnt == 1)
		StartTx(dev, priv->nexttxdescr);
	priv->nexttxdescr = (priv->nexttxdescr + 1) % TXBUFCNT;

	spin_unlock_irqrestore(&priv->lock, flags);
tx_done:
	dev_kfree_skb(skb);
	return retval;
}

/* switch receiver mode. */

static void ibmlana_set_multicast_list(struct net_device *dev)
{
	/* first stop the SONIC... */
	StopSONIC(dev);
	/* ...then reinit it with the new flags */
	InitBoard(dev);
}

/* ------------------------------------------------------------------------
 * hardware check
 * ------------------------------------------------------------------------ */

static int ibmlana_irq;
static int ibmlana_io;
static int startslot;		/* counts through slots when probing multiple devices */

static short ibmlana_adapter_ids[] __initdata = {
	IBM_LANA_ID,
	0x0000
};

static char *ibmlana_adapter_names[] __devinitdata = {
	"IBM LAN Adapter/A",
	NULL
};

static int __devinit ibmlana_init_one(struct device *kdev)
{
	struct mca_device *mdev = to_mca_device(kdev);
	struct net_device *dev;
	int slot = mdev->slot, z, rc;
	int base = 0, irq = 0, iobase = 0, memlen = 0;
	ibmlana_priv *priv;
	ibmlana_medium medium;
	DECLARE_MAC_BUF(mac);

	dev = alloc_etherdev(sizeof(ibmlana_priv));
	if (!dev)
		return -ENOMEM;

	dev->irq = ibmlana_irq;
	dev->base_addr = ibmlana_io;

	base = dev->mem_start;
	irq = dev->irq;

	/* deduce card addresses */
	getaddrs(mdev, &base, &memlen, &iobase, &irq, &medium);

	/* were we looking for something different ? */
	if (dev->irq && dev->irq != irq) {
		rc = -ENODEV;
		goto err_out;
	}
	if (dev->mem_start && dev->mem_start != base) {
		rc = -ENODEV;
		goto err_out;
	}

	/* announce success */
	printk(KERN_INFO "%s: IBM LAN Adapter/A found in slot %d\n", dev->name, slot + 1);

	/* try to obtain I/O range */
	if (!request_region(iobase, IBM_LANA_IORANGE, DRV_NAME)) {
		printk(KERN_ERR "%s: cannot allocate I/O range at %#x!\n", DRV_NAME, iobase);
		startslot = slot + 1;
		rc = -EBUSY;
		goto err_out;
	}

	priv = netdev_priv(dev);
	priv->slot = slot;
	priv->realirq = mca_device_transform_irq(mdev, irq);
	priv->medium = medium;
	spin_lock_init(&priv->lock);

	/* set base + irq for this device (irq not allocated so far) */

	dev->irq = 0;
	dev->mem_start = base;
	dev->mem_end = base + memlen;
	dev->base_addr = iobase;

	priv->base = ioremap(base, memlen);
	if (!priv->base) {
		printk(KERN_ERR "%s: cannot remap memory!\n", DRV_NAME);
		startslot = slot + 1;
		rc = -EBUSY;
		goto err_out_reg;
	}

	mca_device_set_name(mdev, ibmlana_adapter_names[mdev->index]);
	mca_device_set_claim(mdev, 1);

	/* set methods */

	dev->open = ibmlana_open;
	dev->stop = ibmlana_close;
	dev->hard_start_xmit = ibmlana_tx;
	dev->set_multicast_list = ibmlana_set_multicast_list;
	dev->flags |= IFF_MULTICAST;

	/* copy out MAC address */

	for (z = 0; z < sizeof(dev->dev_addr); z++)
		dev->dev_addr[z] = inb(dev->base_addr + MACADDRPROM + z);

	/* print config */

	printk(KERN_INFO "%s: IRQ %d, I/O %#lx, memory %#lx-%#lx, "
	       "MAC address %s.\n",
	       dev->name, priv->realirq, dev->base_addr,
	       dev->mem_start, dev->mem_end - 1,
	       print_mac(mac, dev->dev_addr));
	printk(KERN_INFO "%s: %s medium\n", dev->name, MediaNames[priv->medium]);

	/* reset board */

	ResetBoard(dev);

	/* next probe will start at next slot */

	startslot = slot + 1;

	rc = register_netdev(dev);
	if (rc)
		goto err_out_claimed;

	dev_set_drvdata(kdev, dev);
	return 0;

err_out_claimed:
	mca_device_set_claim(mdev, 0);
	iounmap(priv->base);
err_out_reg:
	release_region(iobase, IBM_LANA_IORANGE);
err_out:
	free_netdev(dev);
	return rc;
}

static int ibmlana_remove_one(struct device *kdev)
{
	struct mca_device *mdev = to_mca_device(kdev);
	struct net_device *dev = dev_get_drvdata(kdev);
	ibmlana_priv *priv = netdev_priv(dev);

	unregister_netdev(dev);
	/*DeinitBoard(dev); */
	release_region(dev->base_addr, IBM_LANA_IORANGE);
	mca_device_set_claim(mdev, 0);
	iounmap(priv->base);
	free_netdev(dev);
	return 0;
}

/* ------------------------------------------------------------------------
 * modularization support
 * ------------------------------------------------------------------------ */

module_param_named(irq, ibmlana_irq, int, 0);
module_param_named(io, ibmlana_io, int, 0);
MODULE_PARM_DESC(irq, "IBM LAN/A IRQ number");
MODULE_PARM_DESC(io, "IBM LAN/A I/O base address");
MODULE_LICENSE("GPL");

static struct mca_driver ibmlana_driver = {
	.id_table = ibmlana_adapter_ids,
	.driver = {
		.name	= "ibmlana",
		.bus	= &mca_bus_type,
		.probe	= ibmlana_init_one,
		.remove	= ibmlana_remove_one,
	},
};

static int __init ibmlana_init_module(void)
{
	return mca_register_driver(&ibmlana_driver);
}

static void __exit ibmlana_cleanup_module(void)
{
	mca_unregister_driver(&ibmlana_driver);
}

module_init(ibmlana_init_module);
module_exit(ibmlana_cleanup_module);

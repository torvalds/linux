/*
net-3-driver for the SKNET MCA-based cards

This is an extension to the Linux operating system, and is covered by the
same GNU General Public License that covers that work.

Copyright 1999 by Alfred Arnold (alfred@ccac.rwth-aachen.de,
                                 alfred.arnold@lancom.de)

This driver is based both on the 3C523 driver and the SK_G16 driver.

paper sources:
  'PC Hardware: Aufbau, Funktionsweise, Programmierung' by
  Hans-Peter Messmer for the basic Microchannel stuff

  'Linux Geraetetreiber' by Allesandro Rubini, Kalle Dalheimer
  for help on Ethernet driver programming

  'Ethernet/IEEE 802.3 Family 1992 World Network Data Book/Handbook' by AMD
  for documentation on the AM7990 LANCE

  'SKNET Personal Technisches Manual', Version 1.2 by Schneider&Koch
  for documentation on the Junior board

  'SK-NET MC2+ Technical Manual", Version 1.1 by Schneider&Koch for
  documentation on the MC2 bord

  A big thank you to the S&K support for providing me so quickly with
  documentation!

  Also see http://www.syskonnect.com/

  Missing things:

  -> set debug level via ioctl instead of compile-time switches
  -> I didn't follow the development of the 2.1.x kernels, so my
     assumptions about which things changed with which kernel version
     are probably nonsense

History:
  May 16th, 1999
  	startup
  May 22st, 1999
	added private structure, methods
        begun building data structures in RAM
  May 23nd, 1999
	can receive frames, send frames
  May 24th, 1999
        modularized initialization of LANCE
        loadable as module
	still Tx problem :-(
  May 26th, 1999
  	MC2 works
  	support for multiple devices
  	display media type for MC2+
  May 28th, 1999
	fixed problem in GetLANCE leaving interrupts turned off
        increase TX queue to 4 packets to improve send performance
  May 29th, 1999
	a few corrections in statistics, caught rcvr overruns
        reinitialization of LANCE/board in critical situations
        MCA info implemented
	implemented LANCE multicast filter
  Jun 6th, 1999
	additions for Linux 2.2
  Dec 25th, 1999
  	unfortunately there seem to be newer MC2+ boards that react
  	on IRQ 3/5/9/10 instead of 3/5/10/11, so we have to autoprobe
  	in questionable cases...
  Dec 28th, 1999
	integrated patches from David Weinehall & Bill Wendling for 2.3
	kernels (isa_...functions).  Things are defined in a way that
        it still works with 2.0.x 8-)
  Dec 30th, 1999
	added handling of the remaining interrupt conditions.  That
        should cure the spurious hangs.
  Jan 30th, 2000
	newer kernels automatically probe more than one board, so the
	'startslot' as a variable is also needed here
  June 1st, 2000
	added changes for recent 2.3 kernels

 *************************************************************************/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/mca-legacy.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/io.h>

#define _SK_MCA_DRIVER_
#include "sk_mca.h"

/* ------------------------------------------------------------------------
 * global static data - not more since we can handle multiple boards and
 * have to pack all state info into the device struct!
 * ------------------------------------------------------------------------ */

static char *MediaNames[Media_Count] =
    { "10Base2", "10BaseT", "10Base5", "Unknown" };

static unsigned char poly[] =
    { 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0,
	1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0
};

/* ------------------------------------------------------------------------
 * private subfunctions
 * ------------------------------------------------------------------------ */

/* dump parts of shared memory - only needed during debugging */

#ifdef DEBUG
static void dumpmem(struct net_device *dev, u32 start, u32 len)
{
	skmca_priv *priv = netdev_priv(dev);
	int z;

	for (z = 0; z < len; z++) {
		if ((z & 15) == 0)
			printk("%04x:", z);
		printk(" %02x", readb(priv->base + start + z));
		if ((z & 15) == 15)
			printk("\n");
	}
}

/* print exact time - ditto */

static void PrTime(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	printk("%9d:%06d: ", tv.tv_sec, tv.tv_usec);
}
#endif

/* deduce resources out of POS registers */

static void __init getaddrs(int slot, int junior, int *base, int *irq,
		     skmca_medium * medium)
{
	u_char pos0, pos1, pos2;

	if (junior) {
		pos0 = mca_read_stored_pos(slot, 2);
		*base = ((pos0 & 0x0e) << 13) + 0xc0000;
		*irq = ((pos0 & 0x10) >> 4) + 10;
		*medium = Media_Unknown;
	} else {
		/* reset POS 104 Bits 0+1 so the shared memory region goes to the
		   configured area between 640K and 1M.  Afterwards, enable the MC2.
		   I really don't know what rode SK to do this... */

		mca_write_pos(slot, 4,
			      mca_read_stored_pos(slot, 4) & 0xfc);
		mca_write_pos(slot, 2,
			      mca_read_stored_pos(slot, 2) | 0x01);

		pos1 = mca_read_stored_pos(slot, 3);
		pos2 = mca_read_stored_pos(slot, 4);
		*base = ((pos1 & 0x07) << 14) + 0xc0000;
		switch (pos2 & 0x0c) {
		case 0:
			*irq = 3;
			break;
		case 4:
			*irq = 5;
			break;
		case 8:
			*irq = -10;
			break;
		case 12:
			*irq = -11;
			break;
		}
		*medium = (pos2 >> 6) & 3;
	}
}

/* check for both cards:
   When the MC2 is turned off, it was configured for more than 15MB RAM,
   is disabled and won't get detected using the standard probe.  We
   therefore have to scan the slots manually :-( */

static int __init dofind(int *junior, int firstslot)
{
	int slot;
	unsigned int id;

	for (slot = firstslot; slot < MCA_MAX_SLOT_NR; slot++) {
		id = mca_read_stored_pos(slot, 0)
		    + (((unsigned int) mca_read_stored_pos(slot, 1)) << 8);

		*junior = 0;
		if (id == SKNET_MCA_ID)
			return slot;
		*junior = 1;
		if (id == SKNET_JUNIOR_MCA_ID)
			return slot;
	}
	return MCA_NOTFOUND;
}

/* reset the whole board */

static void ResetBoard(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);

	writeb(CTRL_RESET_ON, priv->ctrladdr);
	udelay(10);
	writeb(CTRL_RESET_OFF, priv->ctrladdr);
}

/* wait for LANCE interface to become not busy */

static int WaitLANCE(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);
	int t = 0;

	while ((readb(priv->ctrladdr) & STAT_IO_BUSY) ==
	       STAT_IO_BUSY) {
		udelay(1);
		if (++t > 1000) {
			printk("%s: LANCE access timeout", dev->name);
			return 0;
		}
	}

	return 1;
}

/* set LANCE register - must be atomic */

static void SetLANCE(struct net_device *dev, u16 addr, u16 value)
{
	skmca_priv *priv = netdev_priv(dev);
	unsigned long flags;

	/* disable interrupts */

	spin_lock_irqsave(&priv->lock, flags);

	/* wait until no transfer is pending */

	WaitLANCE(dev);

	/* transfer register address to RAP */

	writeb(CTRL_RESET_OFF | CTRL_RW_WRITE | CTRL_ADR_RAP, priv->ctrladdr);
	writew(addr, priv->ioregaddr);
	writeb(IOCMD_GO, priv->cmdaddr);
	udelay(1);
	WaitLANCE(dev);

	/* transfer data to register */

	writeb(CTRL_RESET_OFF | CTRL_RW_WRITE | CTRL_ADR_DATA, priv->ctrladdr);
	writew(value, priv->ioregaddr);
	writeb(IOCMD_GO, priv->cmdaddr);
	udelay(1);
	WaitLANCE(dev);

	/* reenable interrupts */

	spin_unlock_irqrestore(&priv->lock, flags);
}

/* get LANCE register */

static u16 GetLANCE(struct net_device *dev, u16 addr)
{
	skmca_priv *priv = netdev_priv(dev);
	unsigned long flags;
	unsigned int res;

	/* disable interrupts */

	spin_lock_irqsave(&priv->lock, flags);

	/* wait until no transfer is pending */

	WaitLANCE(dev);

	/* transfer register address to RAP */

	writeb(CTRL_RESET_OFF | CTRL_RW_WRITE | CTRL_ADR_RAP, priv->ctrladdr);
	writew(addr, priv->ioregaddr);
	writeb(IOCMD_GO, priv->cmdaddr);
	udelay(1);
	WaitLANCE(dev);

	/* transfer data from register */

	writeb(CTRL_RESET_OFF | CTRL_RW_READ | CTRL_ADR_DATA, priv->ctrladdr);
	writeb(IOCMD_GO, priv->cmdaddr);
	udelay(1);
	WaitLANCE(dev);
	res = readw(priv->ioregaddr);

	/* reenable interrupts */

	spin_unlock_irqrestore(&priv->lock, flags);

	return res;
}

/* build up descriptors in shared RAM */

static void InitDscrs(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);
	u32 bufaddr;

	/* Set up Tx descriptors. The board has only 16K RAM so bits 16..23
	   are always 0. */

	bufaddr = RAM_DATABASE;
	{
		LANCE_TxDescr descr;
		int z;

		for (z = 0; z < TXCOUNT; z++) {
			descr.LowAddr = bufaddr;
			descr.Flags = 0;
			descr.Len = 0xf000;
			descr.Status = 0;
			memcpy_toio(priv->base + RAM_TXBASE +
				   (z * sizeof(LANCE_TxDescr)), &descr,
				   sizeof(LANCE_TxDescr));
			memset_io(priv->base + bufaddr, 0, RAM_BUFSIZE);
			bufaddr += RAM_BUFSIZE;
		}
	}

	/* do the same for the Rx descriptors */

	{
		LANCE_RxDescr descr;
		int z;

		for (z = 0; z < RXCOUNT; z++) {
			descr.LowAddr = bufaddr;
			descr.Flags = RXDSCR_FLAGS_OWN;
			descr.MaxLen = -RAM_BUFSIZE;
			descr.Len = 0;
			memcpy_toio(priv->base + RAM_RXBASE +
				   (z * sizeof(LANCE_RxDescr)), &descr,
				   sizeof(LANCE_RxDescr));
			memset_io(priv->base + bufaddr, 0, RAM_BUFSIZE);
			bufaddr += RAM_BUFSIZE;
		}
	}
}

/* calculate the hash bit position for a given multicast address
   taken more or less directly from the AMD datasheet... */

static void UpdateCRC(unsigned char *CRC, int bit)
{
	int j;

	/* shift CRC one bit */

	memmove(CRC + 1, CRC, 32 * sizeof(unsigned char));
	CRC[0] = 0;

	/* if bit XOR controlbit = 1, set CRC = CRC XOR polynomial */

	if (bit ^ CRC[32])
		for (j = 0; j < 32; j++)
			CRC[j] ^= poly[j];
}

static unsigned int GetHash(char *address)
{
	unsigned char CRC[33];
	int i, byte, hashcode;

	/* a multicast address has bit 0 in the first byte set */

	if ((address[0] & 1) == 0)
		return -1;

	/* initialize CRC */

	memset(CRC, 1, sizeof(CRC));

	/* loop through address bits */

	for (byte = 0; byte < 6; byte++)
		for (i = 0; i < 8; i++)
			UpdateCRC(CRC, (address[byte] >> i) & 1);

	/* hashcode is the 6 least significant bits of the CRC */

	hashcode = 0;
	for (i = 0; i < 6; i++)
		hashcode = (hashcode << 1) + CRC[i];
	return hashcode;
}

/* feed ready-built initialization block into LANCE */

static void InitLANCE(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);

	/* build up descriptors. */

	InitDscrs(dev);

	/* next RX descriptor to be read is the first one.  Since the LANCE
	   will start from the beginning after initialization, we have to
	   reset out pointers too. */

	priv->nextrx = 0;

	/* no TX descriptors active */

	priv->nexttxput = priv->nexttxdone = priv->txbusy = 0;

	/* set up the LANCE bus control register - constant for SKnet boards */

	SetLANCE(dev, LANCE_CSR3,
		 CSR3_BSWAP_OFF | CSR3_ALE_LOW | CSR3_BCON_HOLD);

	/* write address of initialization block into LANCE */

	SetLANCE(dev, LANCE_CSR1, RAM_INITBASE & 0xffff);
	SetLANCE(dev, LANCE_CSR2, (RAM_INITBASE >> 16) & 0xff);

	/* we don't get ready until the LANCE has read the init block */

	netif_stop_queue(dev);

	/* let LANCE read the initialization block.  LANCE is ready
	   when we receive the corresponding interrupt. */

	SetLANCE(dev, LANCE_CSR0, CSR0_INEA | CSR0_INIT);
}

/* stop the LANCE so we can reinitialize it */

static void StopLANCE(struct net_device *dev)
{
	/* can't take frames any more */

	netif_stop_queue(dev);

	/* disable interrupts, stop it */

	SetLANCE(dev, LANCE_CSR0, CSR0_STOP);
}

/* initialize card and LANCE for proper operation */

static void InitBoard(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);
	LANCE_InitBlock block;

	/* Lay out the shared RAM - first we create the init block for the LANCE.
	   We do not overwrite it later because we need it again when we switch
	   promiscous mode on/off. */

	block.Mode = 0;
	if (dev->flags & IFF_PROMISC)
		block.Mode |= LANCE_INIT_PROM;
	memcpy(block.PAdr, dev->dev_addr, 6);
	memset(block.LAdrF, 0, sizeof(block.LAdrF));
	block.RdrP = (RAM_RXBASE & 0xffffff) | (LRXCOUNT << 29);
	block.TdrP = (RAM_TXBASE & 0xffffff) | (LTXCOUNT << 29);

	memcpy_toio(priv->base + RAM_INITBASE, &block, sizeof(block));

	/* initialize LANCE. Implicitly sets up other structures in RAM. */

	InitLANCE(dev);
}

/* deinitialize card and LANCE */

static void DeinitBoard(struct net_device *dev)
{
	/* stop LANCE */

	StopLANCE(dev);

	/* reset board */

	ResetBoard(dev);
}

/* probe for device's irq */

static int __init ProbeIRQ(struct net_device *dev)
{
	unsigned long imaskval, njiffies, irq;
	u16 csr0val;

	/* enable all interrupts */

	imaskval = probe_irq_on();

	/* initialize the board. Wait for interrupt 'Initialization done'. */

	ResetBoard(dev);
	InitBoard(dev);

	njiffies = jiffies + HZ;
	do {
		csr0val = GetLANCE(dev, LANCE_CSR0);
	}
	while (((csr0val & CSR0_IDON) == 0) && (jiffies != njiffies));

	/* turn of interrupts again */

	irq = probe_irq_off(imaskval);

	/* if we found something, ack the interrupt */

	if (irq)
		SetLANCE(dev, LANCE_CSR0, csr0val | CSR0_IDON);

	/* back to idle state */

	DeinitBoard(dev);

	return irq;
}

/* ------------------------------------------------------------------------
 * interrupt handler(s)
 * ------------------------------------------------------------------------ */

/* LANCE has read initialization block -> start it */

static u16 irqstart_handler(struct net_device *dev, u16 oldcsr0)
{
	/* now we're ready to transmit */

	netif_wake_queue(dev);

	/* reset IDON bit, start LANCE */

	SetLANCE(dev, LANCE_CSR0, oldcsr0 | CSR0_IDON | CSR0_STRT);
	return GetLANCE(dev, LANCE_CSR0);
}

/* did we lose blocks due to a FIFO overrun ? */

static u16 irqmiss_handler(struct net_device *dev, u16 oldcsr0)
{
	skmca_priv *priv = netdev_priv(dev);

	/* update statistics */

	priv->stat.rx_fifo_errors++;

	/* reset MISS bit */

	SetLANCE(dev, LANCE_CSR0, oldcsr0 | CSR0_MISS);
	return GetLANCE(dev, LANCE_CSR0);
}

/* receive interrupt */

static u16 irqrx_handler(struct net_device *dev, u16 oldcsr0)
{
	skmca_priv *priv = netdev_priv(dev);
	LANCE_RxDescr descr;
	unsigned int descraddr;

	/* run through queue until we reach a descriptor we do not own */

	descraddr = RAM_RXBASE + (priv->nextrx * sizeof(LANCE_RxDescr));
	while (1) {
		/* read descriptor */
		memcpy_fromio(&descr, priv->base + descraddr,
			     sizeof(LANCE_RxDescr));

		/* if we reach a descriptor we do not own, we're done */
		if ((descr.Flags & RXDSCR_FLAGS_OWN) != 0)
			break;

#ifdef DEBUG
		PrTime();
		printk("Receive packet on descr %d len %d\n", priv->nextrx,
		       descr.Len);
#endif

		/* erroneous packet ? */
		if ((descr.Flags & RXDSCR_FLAGS_ERR) != 0) {
			priv->stat.rx_errors++;
			if ((descr.Flags & RXDSCR_FLAGS_CRC) != 0)
				priv->stat.rx_crc_errors++;
			else if ((descr.Flags & RXDSCR_FLAGS_CRC) != 0)
				priv->stat.rx_frame_errors++;
			else if ((descr.Flags & RXDSCR_FLAGS_OFLO) != 0)
				priv->stat.rx_fifo_errors++;
		}

		/* good packet ? */
		else {
			struct sk_buff *skb;

			skb = dev_alloc_skb(descr.Len + 2);
			if (skb == NULL)
				priv->stat.rx_dropped++;
			else {
				memcpy_fromio(skb_put(skb, descr.Len),
					     priv->base +
					     descr.LowAddr, descr.Len);
				skb->dev = dev;
				skb->protocol = eth_type_trans(skb, dev);
				skb->ip_summed = CHECKSUM_NONE;
				priv->stat.rx_packets++;
				priv->stat.rx_bytes += descr.Len;
				netif_rx(skb);
				dev->last_rx = jiffies;
			}
		}

		/* give descriptor back to LANCE */
		descr.Len = 0;
		descr.Flags |= RXDSCR_FLAGS_OWN;

		/* update descriptor in shared RAM */
		memcpy_toio(priv->base + descraddr, &descr,
			   sizeof(LANCE_RxDescr));

		/* go to next descriptor */
		priv->nextrx++;
		descraddr += sizeof(LANCE_RxDescr);
		if (priv->nextrx >= RXCOUNT) {
			priv->nextrx = 0;
			descraddr = RAM_RXBASE;
		}
	}

	/* reset RINT bit */

	SetLANCE(dev, LANCE_CSR0, oldcsr0 | CSR0_RINT);
	return GetLANCE(dev, LANCE_CSR0);
}

/* transmit interrupt */

static u16 irqtx_handler(struct net_device *dev, u16 oldcsr0)
{
	skmca_priv *priv = netdev_priv(dev);
	LANCE_TxDescr descr;
	unsigned int descraddr;

	/* check descriptors at most until no busy one is left */

	descraddr =
	    RAM_TXBASE + (priv->nexttxdone * sizeof(LANCE_TxDescr));
	while (priv->txbusy > 0) {
		/* read descriptor */
		memcpy_fromio(&descr, priv->base + descraddr,
			     sizeof(LANCE_TxDescr));

		/* if the LANCE still owns this one, we've worked out all sent packets */
		if ((descr.Flags & TXDSCR_FLAGS_OWN) != 0)
			break;

#ifdef DEBUG
		PrTime();
		printk("Send packet done on descr %d\n", priv->nexttxdone);
#endif

		/* update statistics */
		if ((descr.Flags & TXDSCR_FLAGS_ERR) == 0) {
			priv->stat.tx_packets++;
			priv->stat.tx_bytes++;
		} else {
			priv->stat.tx_errors++;
			if ((descr.Status & TXDSCR_STATUS_UFLO) != 0) {
				priv->stat.tx_fifo_errors++;
				InitLANCE(dev);
			}
				else
			    if ((descr.Status & TXDSCR_STATUS_LCOL) !=
				0) priv->stat.tx_window_errors++;
			else if ((descr.Status & TXDSCR_STATUS_LCAR) != 0)
				priv->stat.tx_carrier_errors++;
			else if ((descr.Status & TXDSCR_STATUS_RTRY) != 0)
				priv->stat.tx_aborted_errors++;
		}

		/* go to next descriptor */
		priv->nexttxdone++;
		descraddr += sizeof(LANCE_TxDescr);
		if (priv->nexttxdone >= TXCOUNT) {
			priv->nexttxdone = 0;
			descraddr = RAM_TXBASE;
		}
		priv->txbusy--;
	}

	/* reset TX interrupt bit */

	SetLANCE(dev, LANCE_CSR0, oldcsr0 | CSR0_TINT);
	oldcsr0 = GetLANCE(dev, LANCE_CSR0);

	/* at least one descriptor is freed.  Therefore we can accept
	   a new one */
	/* inform upper layers we're in business again */

	netif_wake_queue(dev);

	return oldcsr0;
}

/* general interrupt entry */

static irqreturn_t irq_handler(int irq, void *device)
{
	struct net_device *dev = (struct net_device *) device;
	u16 csr0val;

	/* read CSR0 to get interrupt cause */

	csr0val = GetLANCE(dev, LANCE_CSR0);

	/* in case we're not meant... */

	if ((csr0val & CSR0_INTR) == 0)
		return IRQ_NONE;

#if 0
	set_bit(LINK_STATE_RXSEM, &dev->state);
#endif

	/* loop through the interrupt bits until everything is clear */

	do {
		if ((csr0val & CSR0_IDON) != 0)
			csr0val = irqstart_handler(dev, csr0val);
		if ((csr0val & CSR0_RINT) != 0)
			csr0val = irqrx_handler(dev, csr0val);
		if ((csr0val & CSR0_MISS) != 0)
			csr0val = irqmiss_handler(dev, csr0val);
		if ((csr0val & CSR0_TINT) != 0)
			csr0val = irqtx_handler(dev, csr0val);
		if ((csr0val & CSR0_MERR) != 0) {
			SetLANCE(dev, LANCE_CSR0, csr0val | CSR0_MERR);
			csr0val = GetLANCE(dev, LANCE_CSR0);
		}
		if ((csr0val & CSR0_BABL) != 0) {
			SetLANCE(dev, LANCE_CSR0, csr0val | CSR0_BABL);
			csr0val = GetLANCE(dev, LANCE_CSR0);
		}
	}
	while ((csr0val & CSR0_INTR) != 0);

#if 0
	clear_bit(LINK_STATE_RXSEM, &dev->state);
#endif
	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------------
 * driver methods
 * ------------------------------------------------------------------------ */

/* MCA info */

static int skmca_getinfo(char *buf, int slot, void *d)
{
	int len = 0, i;
	struct net_device *dev = (struct net_device *) d;
	skmca_priv *priv;

	/* can't say anything about an uninitialized device... */

	if (dev == NULL)
		return len;
	priv = netdev_priv(dev);

	/* print info */

	len += sprintf(buf + len, "IRQ: %d\n", priv->realirq);
	len += sprintf(buf + len, "Memory: %#lx-%#lx\n", dev->mem_start,
		       dev->mem_end - 1);
	len +=
	    sprintf(buf + len, "Transceiver: %s\n",
		    MediaNames[priv->medium]);
	len += sprintf(buf + len, "Device: %s\n", dev->name);
	len += sprintf(buf + len, "MAC address:");
	for (i = 0; i < 6; i++)
		len += sprintf(buf + len, " %02x", dev->dev_addr[i]);
	buf[len++] = '\n';
	buf[len] = 0;

	return len;
}

/* open driver.  Means also initialization and start of LANCE */

static int skmca_open(struct net_device *dev)
{
	int result;
	skmca_priv *priv = netdev_priv(dev);

	/* register resources - only necessary for IRQ */
	result =
	    request_irq(priv->realirq, irq_handler,
			IRQF_SHARED | IRQF_SAMPLE_RANDOM, "sk_mca", dev);
	if (result != 0) {
		printk("%s: failed to register irq %d\n", dev->name,
		       dev->irq);
		return result;
	}
	dev->irq = priv->realirq;

	/* set up the card and LANCE */

	InitBoard(dev);

	/* set up flags */

	netif_start_queue(dev);

	return 0;
}

/* close driver.  Shut down board and free allocated resources */

static int skmca_close(struct net_device *dev)
{
	/* turn off board */
	DeinitBoard(dev);

	/* release resources */
	if (dev->irq != 0)
		free_irq(dev->irq, dev);
	dev->irq = 0;

	return 0;
}

/* transmit a block. */

static int skmca_tx(struct sk_buff *skb, struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);
	LANCE_TxDescr descr;
	unsigned int address;
	int tmplen, retval = 0;
	unsigned long flags;

	/* if we get called with a NULL descriptor, the Ethernet layer thinks
	   our card is stuck an we should reset it.  We'll do this completely: */

	if (skb == NULL) {
		DeinitBoard(dev);
		InitBoard(dev);
		return 0;	/* don't try to free the block here ;-) */
	}

	/* is there space in the Tx queue ? If no, the upper layer gave us a
	   packet in spite of us not being ready and is really in trouble.
	   We'll do the dropping for him: */
	if (priv->txbusy >= TXCOUNT) {
		priv->stat.tx_dropped++;
		retval = -EIO;
		goto tx_done;
	}

	/* get TX descriptor */
	address = RAM_TXBASE + (priv->nexttxput * sizeof(LANCE_TxDescr));
	memcpy_fromio(&descr, priv->base + address, sizeof(LANCE_TxDescr));

	/* enter packet length as 2s complement - assure minimum length */
	tmplen = skb->len;
	if (tmplen < 60)
		tmplen = 60;
	descr.Len = 65536 - tmplen;

	/* copy filler into RAM - in case we're filling up...
	   we're filling a bit more than necessary, but that doesn't harm
	   since the buffer is far larger... */
	if (tmplen > skb->len) {
		char *fill = "NetBSD is a nice OS too! ";
		unsigned int destoffs = 0, l = strlen(fill);

		while (destoffs < tmplen) {
			memcpy_toio(priv->base + descr.LowAddr +
				   destoffs, fill, l);
			destoffs += l;
		}
	}

	/* do the real data copying */
	memcpy_toio(priv->base + descr.LowAddr, skb->data, skb->len);

	/* hand descriptor over to LANCE - this is the first and last chunk */
	descr.Flags =
	    TXDSCR_FLAGS_OWN | TXDSCR_FLAGS_STP | TXDSCR_FLAGS_ENP;

#ifdef DEBUG
	PrTime();
	printk("Send packet on descr %d len %d\n", priv->nexttxput,
	       skb->len);
#endif

	/* one more descriptor busy */

	spin_lock_irqsave(&priv->lock, flags);

	priv->nexttxput++;
	if (priv->nexttxput >= TXCOUNT)
		priv->nexttxput = 0;
	priv->txbusy++;

	/* are we saturated ? */

	if (priv->txbusy >= TXCOUNT)
		netif_stop_queue(dev);

	/* write descriptor back to RAM */
	memcpy_toio(priv->base + address, &descr, sizeof(LANCE_TxDescr));

	/* if no descriptors were active, give the LANCE a hint to read it
	   immediately */

	if (priv->txbusy == 0)
		SetLANCE(dev, LANCE_CSR0, CSR0_INEA | CSR0_TDMD);

	spin_unlock_irqrestore(&priv->lock, flags);

      tx_done:

	dev_kfree_skb(skb);

	return retval;
}

/* return pointer to Ethernet statistics */

static struct net_device_stats *skmca_stats(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);

	return &(priv->stat);
}

/* switch receiver mode.  We use the LANCE's multicast filter to prefilter
   multicast addresses. */

static void skmca_set_multicast_list(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);
	LANCE_InitBlock block;

	/* first stop the LANCE... */
	StopLANCE(dev);

	/* ...then modify the initialization block... */
	memcpy_fromio(&block, priv->base + RAM_INITBASE, sizeof(block));
	if (dev->flags & IFF_PROMISC)
		block.Mode |= LANCE_INIT_PROM;
	else
		block.Mode &= ~LANCE_INIT_PROM;

	if (dev->flags & IFF_ALLMULTI) {	/* get all multicasts */
		memset(block.LAdrF, 0xff, sizeof(block.LAdrF));
	} else {		/* get selected/no multicasts */

		struct dev_mc_list *mptr;
		int code;

		memset(block.LAdrF, 0, sizeof(block.LAdrF));
		for (mptr = dev->mc_list; mptr != NULL; mptr = mptr->next) {
			code = GetHash(mptr->dmi_addr);
			block.LAdrF[(code >> 3) & 7] |= 1 << (code & 7);
		}
	}

	memcpy_toio(priv->base + RAM_INITBASE, &block, sizeof(block));

	/* ...then reinit LANCE with the correct flags */
	InitLANCE(dev);
}

/* ------------------------------------------------------------------------
 * hardware check
 * ------------------------------------------------------------------------ */

static int startslot;		/* counts through slots when probing multiple devices */

static void cleanup_card(struct net_device *dev)
{
	skmca_priv *priv = netdev_priv(dev);
	DeinitBoard(dev);
	if (dev->irq != 0)
		free_irq(dev->irq, dev);
	iounmap(priv->base);
	mca_mark_as_unused(priv->slot);
	mca_set_adapter_procfn(priv->slot, NULL, NULL);
}

struct net_device * __init skmca_probe(int unit)
{
	struct net_device *dev;
	int force_detect = 0;
	int junior, slot, i;
	int base = 0, irq = 0;
	skmca_priv *priv;
	skmca_medium medium;
	int err;

	/* can't work without an MCA bus ;-) */

	if (MCA_bus == 0)
		return ERR_PTR(-ENODEV);

	dev = alloc_etherdev(sizeof(skmca_priv));
	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	SET_MODULE_OWNER(dev);

	/* start address of 1 --> forced detection */

	if (dev->mem_start == 1)
		force_detect = 1;

	/* search through slots */

	base = dev->mem_start;
	irq = dev->base_addr;
	for (slot = startslot; (slot = dofind(&junior, slot)) != -1; slot++) {
		/* deduce card addresses */

		getaddrs(slot, junior, &base, &irq, &medium);

		/* slot already in use ? */

		if (mca_is_adapter_used(slot))
			continue;

		/* were we looking for something different ? */

		if (dev->irq && dev->irq != irq)
			continue;
		if (dev->mem_start && dev->mem_start != base)
			continue;

		/* found something that matches */

		break;
	}

	/* nothing found ? */

	if (slot == -1) {
		free_netdev(dev);
		return (base || irq) ? ERR_PTR(-ENXIO) : ERR_PTR(-ENODEV);
	}

	/* make procfs entries */

	if (junior)
		mca_set_adapter_name(slot,
				     "SKNET junior MC2 Ethernet Adapter");
	else
		mca_set_adapter_name(slot, "SKNET MC2+ Ethernet Adapter");
	mca_set_adapter_procfn(slot, (MCA_ProcFn) skmca_getinfo, dev);

	mca_mark_as_used(slot);

	/* announce success */
	printk("%s: SKNet %s adapter found in slot %d\n", dev->name,
	       junior ? "Junior MC2" : "MC2+", slot + 1);

	priv = netdev_priv(dev);
	priv->base = ioremap(base, 0x4000);
	if (!priv->base) {
		mca_set_adapter_procfn(slot, NULL, NULL);
		mca_mark_as_unused(slot);
		free_netdev(dev);
		return ERR_PTR(-ENOMEM);
	}

	priv->slot = slot;
	priv->macbase = priv->base + 0x3fc0;
	priv->ioregaddr = priv->base + 0x3ff0;
	priv->ctrladdr = priv->base + 0x3ff2;
	priv->cmdaddr = priv->base + 0x3ff3;
	priv->medium = medium;
	memset(&priv->stat, 0, sizeof(struct net_device_stats));
	spin_lock_init(&priv->lock);

	/* set base + irq for this device (irq not allocated so far) */
	dev->irq = 0;
	dev->mem_start = base;
	dev->mem_end = base + 0x4000;

	/* autoprobe ? */
	if (irq < 0) {
		int nirq;

		printk
		    ("%s: ambigous POS bit combination, must probe for IRQ...\n",
		     dev->name);
		nirq = ProbeIRQ(dev);
		if (nirq <= 0)
			printk("%s: IRQ probe failed, assuming IRQ %d",
			       dev->name, priv->realirq = -irq);
		else
			priv->realirq = nirq;
	} else
		priv->realirq = irq;

	/* set methods */
	dev->open = skmca_open;
	dev->stop = skmca_close;
	dev->hard_start_xmit = skmca_tx;
	dev->do_ioctl = NULL;
	dev->get_stats = skmca_stats;
	dev->set_multicast_list = skmca_set_multicast_list;
	dev->flags |= IFF_MULTICAST;

	/* copy out MAC address */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(priv->macbase + (i << 1));

	/* print config */
	printk("%s: IRQ %d, memory %#lx-%#lx, "
	       "MAC address %02x:%02x:%02x:%02x:%02x:%02x.\n",
	       dev->name, priv->realirq, dev->mem_start, dev->mem_end - 1,
	       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
	printk("%s: %s medium\n", dev->name, MediaNames[priv->medium]);

	/* reset board */

	ResetBoard(dev);

	startslot = slot + 1;

	err = register_netdev(dev);
	if (err) {
		cleanup_card(dev);
		free_netdev(dev);
		dev = ERR_PTR(err);
	}
	return dev;
}

/* ------------------------------------------------------------------------
 * modularization support
 * ------------------------------------------------------------------------ */

#ifdef MODULE
MODULE_LICENSE("GPL");

#define DEVMAX 5

static struct net_device *moddevs[DEVMAX];

int init_module(void)
{
	int z;

	startslot = 0;
	for (z = 0; z < DEVMAX; z++) {
		struct net_device *dev = skmca_probe(-1);
		if (IS_ERR(dev))
			break;
		moddevs[z] = dev;
	}
	if (!z)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	int z;

	for (z = 0; z < DEVMAX; z++) {
		struct net_device *dev = moddevs[z];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}
#endif				/* MODULE */

/*
 *  linux/drivers/net/am79c961.c
 *
 *  by Russell King <rmk@arm.linux.org.uk> 1995-2001.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from various things including skeleton.c
 *
 * This is a special driver for the am79c961A Lance chip used in the
 * Intel (formally Digital Equipment Corp) EBSA110 platform.  Please
 * note that this can not be built as a module (it doesn't make sense).
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/system.h>

#define TX_BUFFERS 15
#define RX_BUFFERS 25

#include "am79c961a.h"

static irqreturn_t
am79c961_interrupt (int irq, void *dev_id);

static unsigned int net_debug = NET_DEBUG;

static const char version[] =
	"am79c961 ethernet driver (C) 1995-2001 Russell King v0.04\n";

/* --------------------------------------------------------------------------- */

#ifdef __arm__
static void write_rreg(u_long base, u_int reg, u_int val)
{
	__asm__(
	"str%?h	%1, [%2]	@ NET_RAP\n\t"
	"str%?h	%0, [%2, #-4]	@ NET_RDP"
	:
	: "r" (val), "r" (reg), "r" (ISAIO_BASE + 0x0464));
}

static inline unsigned short read_rreg(u_long base_addr, u_int reg)
{
	unsigned short v;
	__asm__(
	"str%?h	%1, [%2]	@ NET_RAP\n\t"
	"ldr%?h	%0, [%2, #-4]	@ NET_RDP"
	: "=r" (v)
	: "r" (reg), "r" (ISAIO_BASE + 0x0464));
	return v;
}

static inline void write_ireg(u_long base, u_int reg, u_int val)
{
	__asm__(
	"str%?h	%1, [%2]	@ NET_RAP\n\t"
	"str%?h	%0, [%2, #8]	@ NET_IDP"
	:
	: "r" (val), "r" (reg), "r" (ISAIO_BASE + 0x0464));
}

static inline unsigned short read_ireg(u_long base_addr, u_int reg)
{
	u_short v;
	__asm__(
	"str%?h	%1, [%2]	@ NAT_RAP\n\t"
	"ldr%?h	%0, [%2, #8]	@ NET_IDP\n\t"
	: "=r" (v)
	: "r" (reg), "r" (ISAIO_BASE + 0x0464));
	return v;
}

#define am_writeword(dev,off,val) __raw_writew(val, ISAMEM_BASE + ((off) << 1))
#define am_readword(dev,off)      __raw_readw(ISAMEM_BASE + ((off) << 1))

static inline void
am_writebuffer(struct net_device *dev, u_int offset, unsigned char *buf, unsigned int length)
{
	offset = ISAMEM_BASE + (offset << 1);
	length = (length + 1) & ~1;
	if ((int)buf & 2) {
		__asm__ __volatile__("str%?h	%2, [%0], #4"
		 : "=&r" (offset) : "0" (offset), "r" (buf[0] | (buf[1] << 8)));
		buf += 2;
		length -= 2;
	}
	while (length > 8) {
		unsigned int tmp, tmp2;
		__asm__ __volatile__(
			"ldm%?ia	%1!, {%2, %3}\n\t"
			"str%?h	%2, [%0], #4\n\t"
			"mov%?	%2, %2, lsr #16\n\t"
			"str%?h	%2, [%0], #4\n\t"
			"str%?h	%3, [%0], #4\n\t"
			"mov%?	%3, %3, lsr #16\n\t"
			"str%?h	%3, [%0], #4"
		: "=&r" (offset), "=&r" (buf), "=r" (tmp), "=r" (tmp2)
		: "0" (offset), "1" (buf));
		length -= 8;
	}
	while (length > 0) {
		__asm__ __volatile__("str%?h	%2, [%0], #4"
		 : "=&r" (offset) : "0" (offset), "r" (buf[0] | (buf[1] << 8)));
		buf += 2;
		length -= 2;
	}
}

static inline void
am_readbuffer(struct net_device *dev, u_int offset, unsigned char *buf, unsigned int length)
{
	offset = ISAMEM_BASE + (offset << 1);
	length = (length + 1) & ~1;
	if ((int)buf & 2) {
		unsigned int tmp;
		__asm__ __volatile__(
			"ldr%?h	%2, [%0], #4\n\t"
			"str%?b	%2, [%1], #1\n\t"
			"mov%?	%2, %2, lsr #8\n\t"
			"str%?b	%2, [%1], #1"
		: "=&r" (offset), "=&r" (buf), "=r" (tmp): "0" (offset), "1" (buf));
		length -= 2;
	}
	while (length > 8) {
		unsigned int tmp, tmp2, tmp3;
		__asm__ __volatile__(
			"ldr%?h	%2, [%0], #4\n\t"
			"ldr%?h	%3, [%0], #4\n\t"
			"orr%?	%2, %2, %3, lsl #16\n\t"
			"ldr%?h	%3, [%0], #4\n\t"
			"ldr%?h	%4, [%0], #4\n\t"
			"orr%?	%3, %3, %4, lsl #16\n\t"
			"stm%?ia	%1!, {%2, %3}"
		: "=&r" (offset), "=&r" (buf), "=r" (tmp), "=r" (tmp2), "=r" (tmp3)
		: "0" (offset), "1" (buf));
		length -= 8;
	}
	while (length > 0) {
		unsigned int tmp;
		__asm__ __volatile__(
			"ldr%?h	%2, [%0], #4\n\t"
			"str%?b	%2, [%1], #1\n\t"
			"mov%?	%2, %2, lsr #8\n\t"
			"str%?b	%2, [%1], #1"
		: "=&r" (offset), "=&r" (buf), "=r" (tmp) : "0" (offset), "1" (buf));
		length -= 2;
	}
}
#else
#error Not compatible
#endif

static int
am79c961_ramtest(struct net_device *dev, unsigned int val)
{
	unsigned char *buffer = kmalloc (65536, GFP_KERNEL);
	int i, error = 0, errorcount = 0;

	if (!buffer)
		return 0;
	memset (buffer, val, 65536);
	am_writebuffer(dev, 0, buffer, 65536);
	memset (buffer, val ^ 255, 65536);
	am_readbuffer(dev, 0, buffer, 65536);
	for (i = 0; i < 65536; i++) {
		if (buffer[i] != val && !error) {
			printk ("%s: buffer error (%02X %02X) %05X - ", dev->name, val, buffer[i], i);
			error = 1;
			errorcount ++;
		} else if (error && buffer[i] == val) {
			printk ("%05X\n", i);
			error = 0;
		}
	}
	if (error)
		printk ("10000\n");
	kfree (buffer);
	return errorcount;
}

static void
am79c961_init_for_open(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	unsigned long flags;
	unsigned char *p;
	u_int hdr_addr, first_free_addr;
	int i;

	/*
	 * Stop the chip.
	 */
	spin_lock_irqsave(&priv->chip_lock, flags);
	write_rreg (dev->base_addr, CSR0, CSR0_BABL|CSR0_CERR|CSR0_MISS|CSR0_MERR|CSR0_TINT|CSR0_RINT|CSR0_STOP);
	spin_unlock_irqrestore(&priv->chip_lock, flags);

	write_ireg (dev->base_addr, 5, 0x00a0); /* Receive address LED */
	write_ireg (dev->base_addr, 6, 0x0081); /* Collision LED */
	write_ireg (dev->base_addr, 7, 0x0090); /* XMIT LED */
	write_ireg (dev->base_addr, 2, 0x0000); /* MODE register selects media */

	for (i = LADRL; i <= LADRH; i++)
		write_rreg (dev->base_addr, i, 0);

	for (i = PADRL, p = dev->dev_addr; i <= PADRH; i++, p += 2)
		write_rreg (dev->base_addr, i, p[0] | (p[1] << 8));

	i = MODE_PORT_10BT;
	if (dev->flags & IFF_PROMISC)
		i |= MODE_PROMISC;

	write_rreg (dev->base_addr, MODE, i);
	write_rreg (dev->base_addr, POLLINT, 0);
	write_rreg (dev->base_addr, SIZERXR, -RX_BUFFERS);
	write_rreg (dev->base_addr, SIZETXR, -TX_BUFFERS);

	first_free_addr = RX_BUFFERS * 8 + TX_BUFFERS * 8 + 16;
	hdr_addr = 0;

	priv->rxhead = 0;
	priv->rxtail = 0;
	priv->rxhdr = hdr_addr;

	for (i = 0; i < RX_BUFFERS; i++) {
		priv->rxbuffer[i] = first_free_addr;
		am_writeword (dev, hdr_addr, first_free_addr);
		am_writeword (dev, hdr_addr + 2, RMD_OWN);
		am_writeword (dev, hdr_addr + 4, (-1600));
		am_writeword (dev, hdr_addr + 6, 0);
		first_free_addr += 1600;
		hdr_addr += 8;
	}
	priv->txhead = 0;
	priv->txtail = 0;
	priv->txhdr = hdr_addr;
	for (i = 0; i < TX_BUFFERS; i++) {
		priv->txbuffer[i] = first_free_addr;
		am_writeword (dev, hdr_addr, first_free_addr);
		am_writeword (dev, hdr_addr + 2, TMD_STP|TMD_ENP);
		am_writeword (dev, hdr_addr + 4, 0xf000);
		am_writeword (dev, hdr_addr + 6, 0);
		first_free_addr += 1600;
		hdr_addr += 8;
	}

	write_rreg (dev->base_addr, BASERXL, priv->rxhdr);
	write_rreg (dev->base_addr, BASERXH, 0);
	write_rreg (dev->base_addr, BASETXL, priv->txhdr);
	write_rreg (dev->base_addr, BASERXH, 0);
	write_rreg (dev->base_addr, CSR0, CSR0_STOP);
	write_rreg (dev->base_addr, CSR3, CSR3_IDONM|CSR3_BABLM|CSR3_DXSUFLO);
	write_rreg (dev->base_addr, CSR4, CSR4_APAD_XMIT|CSR4_MFCOM|CSR4_RCVCCOM|CSR4_TXSTRTM|CSR4_JABM);
	write_rreg (dev->base_addr, CSR0, CSR0_IENA|CSR0_STRT);
}

static void am79c961_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct dev_priv *priv = netdev_priv(dev);
	unsigned int lnkstat, carrier;

	lnkstat = read_ireg(dev->base_addr, ISALED0) & ISALED0_LNKST;
	carrier = netif_carrier_ok(dev);

	if (lnkstat && !carrier) {
		netif_carrier_on(dev);
		printk("%s: link up\n", dev->name);
	} else if (!lnkstat && carrier) {
		netif_carrier_off(dev);
		printk("%s: link down\n", dev->name);
	}

	mod_timer(&priv->timer, jiffies + msecs_to_jiffies(500));
}

/*
 * Open/initialize the board.
 */
static int
am79c961_open(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	int ret;

	memset (&priv->stats, 0, sizeof (priv->stats));

	ret = request_irq(dev->irq, am79c961_interrupt, 0, dev->name, dev);
	if (ret)
		return ret;

	am79c961_init_for_open(dev);

	netif_carrier_off(dev);

	priv->timer.expires = jiffies;
	add_timer(&priv->timer);

	netif_start_queue(dev);

	return 0;
}

/*
 * The inverse routine to am79c961_open().
 */
static int
am79c961_close(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	unsigned long flags;

	del_timer_sync(&priv->timer);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	spin_lock_irqsave(&priv->chip_lock, flags);
	write_rreg (dev->base_addr, CSR0, CSR0_STOP);
	write_rreg (dev->base_addr, CSR3, CSR3_MASKALL);
	spin_unlock_irqrestore(&priv->chip_lock, flags);

	free_irq (dev->irq, dev);

	return 0;
}

/*
 * Get the current statistics.
 */
static struct net_device_stats *am79c961_getstats (struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

static void am79c961_mc_hash(char *addr, unsigned short *hash)
{
	if (addr[0] & 0x01) {
		int idx, bit;
		u32 crc;

		crc = ether_crc_le(ETH_ALEN, addr);

		idx = crc >> 30;
		bit = (crc >> 26) & 15;

		hash[idx] |= 1 << bit;
	}
}

/*
 * Set or clear promiscuous/multicast mode filter for this adapter.
 */
static void am79c961_setmulticastlist (struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	unsigned long flags;
	unsigned short multi_hash[4], mode;
	int i, stopped;

	mode = MODE_PORT_10BT;

	if (dev->flags & IFF_PROMISC) {
		mode |= MODE_PROMISC;
	} else if (dev->flags & IFF_ALLMULTI) {
		memset(multi_hash, 0xff, sizeof(multi_hash));
	} else {
		struct netdev_hw_addr *ha;

		memset(multi_hash, 0x00, sizeof(multi_hash));

		netdev_for_each_mc_addr(ha, dev)
			am79c961_mc_hash(ha->addr, multi_hash);
	}

	spin_lock_irqsave(&priv->chip_lock, flags);

	stopped = read_rreg(dev->base_addr, CSR0) & CSR0_STOP;

	if (!stopped) {
		/*
		 * Put the chip into suspend mode
		 */
		write_rreg(dev->base_addr, CTRL1, CTRL1_SPND);

		/*
		 * Spin waiting for chip to report suspend mode
		 */
		while ((read_rreg(dev->base_addr, CTRL1) & CTRL1_SPND) == 0) {
			spin_unlock_irqrestore(&priv->chip_lock, flags);
			nop();
			spin_lock_irqsave(&priv->chip_lock, flags);
		}
	}

	/*
	 * Update the multicast hash table
	 */
	for (i = 0; i < ARRAY_SIZE(multi_hash); i++)
		write_rreg(dev->base_addr, i + LADRL, multi_hash[i]);

	/*
	 * Write the mode register
	 */
	write_rreg(dev->base_addr, MODE, mode);

	if (!stopped) {
		/*
		 * Put the chip back into running mode
		 */
		write_rreg(dev->base_addr, CTRL1, 0);
	}

	spin_unlock_irqrestore(&priv->chip_lock, flags);
}

static void am79c961_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: transmit timed out, network cable problem?\n",
		dev->name);

	/*
	 * ought to do some setup of the tx side here
	 */

	netif_wake_queue(dev);
}

/*
 * Transmit a packet
 */
static int
am79c961_sendpacket(struct sk_buff *skb, struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	unsigned int hdraddr, bufaddr;
	unsigned int head;
	unsigned long flags;

	head = priv->txhead;
	hdraddr = priv->txhdr + (head << 3);
	bufaddr = priv->txbuffer[head];
	head += 1;
	if (head >= TX_BUFFERS)
		head = 0;

	am_writebuffer (dev, bufaddr, skb->data, skb->len);
	am_writeword (dev, hdraddr + 4, -skb->len);
	am_writeword (dev, hdraddr + 2, TMD_OWN|TMD_STP|TMD_ENP);
	priv->txhead = head;

	spin_lock_irqsave(&priv->chip_lock, flags);
	write_rreg (dev->base_addr, CSR0, CSR0_TDMD|CSR0_IENA);
	spin_unlock_irqrestore(&priv->chip_lock, flags);

	/*
	 * If the next packet is owned by the ethernet device,
	 * then the tx ring is full and we can't add another
	 * packet.
	 */
	if (am_readword(dev, priv->txhdr + (priv->txhead << 3) + 2) & TMD_OWN)
		netif_stop_queue(dev);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/*
 * If we have a good packet(s), get it/them out of the buffers.
 */
static void
am79c961_rx(struct net_device *dev, struct dev_priv *priv)
{
	do {
		struct sk_buff *skb;
		u_int hdraddr;
		u_int pktaddr;
		u_int status;
		int len;

		hdraddr = priv->rxhdr + (priv->rxtail << 3);
		pktaddr = priv->rxbuffer[priv->rxtail];

		status = am_readword (dev, hdraddr + 2);
		if (status & RMD_OWN) /* do we own it? */
			break;

		priv->rxtail ++;
		if (priv->rxtail >= RX_BUFFERS)
			priv->rxtail = 0;

		if ((status & (RMD_ERR|RMD_STP|RMD_ENP)) != (RMD_STP|RMD_ENP)) {
			am_writeword (dev, hdraddr + 2, RMD_OWN);
			priv->stats.rx_errors ++;
			if (status & RMD_ERR) {
				if (status & RMD_FRAM)
					priv->stats.rx_frame_errors ++;
				if (status & RMD_CRC)
					priv->stats.rx_crc_errors ++;
			} else if (status & RMD_STP)
				priv->stats.rx_length_errors ++;
			continue;
		}

		len = am_readword(dev, hdraddr + 6);
		skb = dev_alloc_skb(len + 2);

		if (skb) {
			skb_reserve(skb, 2);

			am_readbuffer(dev, pktaddr, skb_put(skb, len), len);
			am_writeword(dev, hdraddr + 2, RMD_OWN);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			priv->stats.rx_bytes += len;
			priv->stats.rx_packets ++;
		} else {
			am_writeword (dev, hdraddr + 2, RMD_OWN);
			printk (KERN_WARNING "%s: memory squeeze, dropping packet.\n", dev->name);
			priv->stats.rx_dropped ++;
			break;
		}
	} while (1);
}

/*
 * Update stats for the transmitted packet
 */
static void
am79c961_tx(struct net_device *dev, struct dev_priv *priv)
{
	do {
		short len;
		u_int hdraddr;
		u_int status;

		hdraddr = priv->txhdr + (priv->txtail << 3);
		status = am_readword (dev, hdraddr + 2);
		if (status & TMD_OWN)
			break;

		priv->txtail ++;
		if (priv->txtail >= TX_BUFFERS)
			priv->txtail = 0;

		if (status & TMD_ERR) {
			u_int status2;

			priv->stats.tx_errors ++;

			status2 = am_readword (dev, hdraddr + 6);

			/*
			 * Clear the error byte
			 */
			am_writeword (dev, hdraddr + 6, 0);

			if (status2 & TST_RTRY)
				priv->stats.collisions += 16;
			if (status2 & TST_LCOL)
				priv->stats.tx_window_errors ++;
			if (status2 & TST_LCAR)
				priv->stats.tx_carrier_errors ++;
			if (status2 & TST_UFLO)
				priv->stats.tx_fifo_errors ++;
			continue;
		}
		priv->stats.tx_packets ++;
		len = am_readword (dev, hdraddr + 4);
		priv->stats.tx_bytes += -len;
	} while (priv->txtail != priv->txhead);

	netif_wake_queue(dev);
}

static irqreturn_t
am79c961_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct dev_priv *priv = netdev_priv(dev);
	u_int status, n = 100;
	int handled = 0;

	do {
		status = read_rreg(dev->base_addr, CSR0);
		write_rreg(dev->base_addr, CSR0, status &
			   (CSR0_IENA|CSR0_TINT|CSR0_RINT|
			    CSR0_MERR|CSR0_MISS|CSR0_CERR|CSR0_BABL));

		if (status & CSR0_RINT) {
			handled = 1;
			am79c961_rx(dev, priv);
		}
		if (status & CSR0_TINT) {
			handled = 1;
			am79c961_tx(dev, priv);
		}
		if (status & CSR0_MISS) {
			handled = 1;
			priv->stats.rx_dropped ++;
		}
		if (status & CSR0_CERR) {
			handled = 1;
			mod_timer(&priv->timer, jiffies);
		}
	} while (--n && status & (CSR0_RINT | CSR0_TINT));

	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void am79c961_poll_controller(struct net_device *dev)
{
	unsigned long flags;
	local_irq_save(flags);
	am79c961_interrupt(dev->irq, dev);
	local_irq_restore(flags);
}
#endif

/*
 * Initialise the chip.  Note that we always expect
 * to be entered with interrupts enabled.
 */
static int
am79c961_hw_init(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);

	spin_lock_irq(&priv->chip_lock);
	write_rreg (dev->base_addr, CSR0, CSR0_STOP);
	write_rreg (dev->base_addr, CSR3, CSR3_MASKALL);
	spin_unlock_irq(&priv->chip_lock);

	am79c961_ramtest(dev, 0x66);
	am79c961_ramtest(dev, 0x99);

	return 0;
}

static void __init am79c961_banner(void)
{
	static unsigned version_printed;

	if (net_debug && version_printed++ == 0)
		printk(KERN_INFO "%s", version);
}
static const struct net_device_ops am79c961_netdev_ops = {
	.ndo_open		= am79c961_open,
	.ndo_stop		= am79c961_close,
	.ndo_start_xmit		= am79c961_sendpacket,
	.ndo_get_stats		= am79c961_getstats,
	.ndo_set_multicast_list	= am79c961_setmulticastlist,
	.ndo_tx_timeout		= am79c961_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= am79c961_poll_controller,
#endif
};

static int __devinit am79c961_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct net_device *dev;
	struct dev_priv *priv;
	int i, ret;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENODEV;

	dev = alloc_etherdev(sizeof(struct dev_priv));
	ret = -ENOMEM;
	if (!dev)
		goto out;

	SET_NETDEV_DEV(dev, &pdev->dev);

	priv = netdev_priv(dev);

	/*
	 * Fixed address and IRQ lines here.
	 * The PNP initialisation should have been
	 * done by the ether bootp loader.
	 */
	dev->base_addr = res->start;
	ret = platform_get_irq(pdev, 0);

	if (ret < 0) {
		ret = -ENODEV;
		goto nodev;
	}
	dev->irq = ret;

	ret = -ENODEV;
	if (!request_region(dev->base_addr, 0x18, dev->name))
		goto nodev;

	/*
	 * Reset the device.
	 */
	inb(dev->base_addr + NET_RESET);
	udelay(5);

	/*
	 * Check the manufacturer part of the
	 * ether address.
	 */
	if (inb(dev->base_addr) != 0x08 ||
	    inb(dev->base_addr + 2) != 0x00 ||
	    inb(dev->base_addr + 4) != 0x2b)
	    	goto release;

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(dev->base_addr + i * 2) & 0xff;

	am79c961_banner();

	spin_lock_init(&priv->chip_lock);
	init_timer(&priv->timer);
	priv->timer.data = (unsigned long)dev;
	priv->timer.function = am79c961_timer;

	if (am79c961_hw_init(dev))
		goto release;

	dev->netdev_ops = &am79c961_netdev_ops;

	ret = register_netdev(dev);
	if (ret == 0) {
		printk(KERN_INFO "%s: ether address %pM\n",
		       dev->name, dev->dev_addr);
		return 0;
	}

release:
	release_region(dev->base_addr, 0x18);
nodev:
	free_netdev(dev);
out:
	return ret;
}

static struct platform_driver am79c961_driver = {
	.probe		= am79c961_probe,
	.driver		= {
		.name	= "am79c961",
	},
};

static int __init am79c961_init(void)
{
	return platform_driver_register(&am79c961_driver);
}

__initcall(am79c961_init);

/*
 *  Amiga Linux/m68k and Linux/PPC Zorro NS8390 Ethernet Driver
 *
 *  (C) Copyright 1998-2000 by some Elitist 680x0 Users(TM)
 *
 *  ---------------------------------------------------------------------------
 *
 *  This program is based on all the other NE2000 drivers for Linux
 *
 *  ---------------------------------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 *
 *  ---------------------------------------------------------------------------
 *
 *  The Ariadne II and X-Surf are Zorro-II boards containing Realtek RTL8019AS
 *  Ethernet Controllers.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/zorro.h>
#include <linux/jiffies.h>

#include <asm/irq.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>

#define EI_SHIFT(x)		(ei_local->reg_offset[x])
#define ei_inb(port)		in_8(port)
#define ei_outb(val, port)	out_8(port, val)
#define ei_inb_p(port)		in_8(port)
#define ei_outb_p(val, port)	out_8(port, val)

static const char version[] =
	"8390.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include "lib8390.c"

#define DRV_NAME	"zorro8390"

#define NE_BASE		(dev->base_addr)
#define NE_CMD		(0x00 * 2)
#define NE_DATAPORT	(0x10 * 2)	/* NatSemi-defined port window offset */
#define NE_RESET	(0x1f * 2)	/* Issue a read to reset,
					 * a write to clear. */
#define NE_IO_EXTENT	(0x20 * 2)

#define NE_EN0_ISR	(0x07 * 2)
#define NE_EN0_DCFG	(0x0e * 2)

#define NE_EN0_RSARLO	(0x08 * 2)
#define NE_EN0_RSARHI	(0x09 * 2)
#define NE_EN0_RCNTLO	(0x0a * 2)
#define NE_EN0_RXCR	(0x0c * 2)
#define NE_EN0_TXCR	(0x0d * 2)
#define NE_EN0_RCNTHI	(0x0b * 2)
#define NE_EN0_IMR	(0x0f * 2)

#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

#define WORDSWAP(a)	((((a) >> 8) & 0xff) | ((a) << 8))

static struct card_info {
	zorro_id id;
	const char *name;
	unsigned int offset;
} cards[] __devinitdata = {
	{ ZORRO_PROD_VILLAGE_TRONIC_ARIADNE2, "Ariadne II", 0x0600 },
	{ ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF, "X-Surf", 0x8600 },
};

/* Hard reset the card.  This used to pause for the same period that a
 * 8390 reset command required, but that shouldn't be necessary.
 */
static void zorro8390_reset_8390(struct net_device *dev)
{
	unsigned long reset_start_time = jiffies;

	if (ei_debug > 1)
		netdev_dbg(dev, "resetting - t=%ld...\n", jiffies);

	z_writeb(z_readb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((z_readb(NE_BASE + NE_EN0_ISR) & ENISR_RESET) == 0)
		if (time_after(jiffies, reset_start_time + 2 * HZ / 100)) {
			netdev_warn(dev, "%s: did not complete\n", __func__);
			break;
		}
	z_writeb(ENISR_RESET, NE_BASE + NE_EN0_ISR);	/* Ack intr */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
 * we don't need to be concerned with ring wrap as the header will be at
 * the start of a page, so we optimize accordingly.
 */
static void zorro8390_get_8390_hdr(struct net_device *dev,
				   struct e8390_pkt_hdr *hdr, int ring_page)
{
	int nic_base = dev->base_addr;
	int cnt;
	short *ptrs;

	/* This *shouldn't* happen.
	 * If it does, it's the last thing you'll see
	 */
	if (ei_status.dmaing) {
		netdev_err(dev, "%s: DMAing conflict [DMAstat:%d][irqlock:%d]\n",
			   __func__, ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	z_writeb(E8390_NODMA + E8390_PAGE0 + E8390_START, nic_base + NE_CMD);
	z_writeb(ENISR_RDC, nic_base + NE_EN0_ISR);
	z_writeb(sizeof(struct e8390_pkt_hdr), nic_base + NE_EN0_RCNTLO);
	z_writeb(0, nic_base + NE_EN0_RCNTHI);
	z_writeb(0, nic_base + NE_EN0_RSARLO);		/* On page boundary */
	z_writeb(ring_page, nic_base + NE_EN0_RSARHI);
	z_writeb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

	ptrs = (short *)hdr;
	for (cnt = 0; cnt < sizeof(struct e8390_pkt_hdr) >> 1; cnt++)
		*ptrs++ = z_readw(NE_BASE + NE_DATAPORT);

	z_writeb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr */

	hdr->count = WORDSWAP(hdr->count);

	ei_status.dmaing &= ~0x01;
}

/* Block input and output, similar to the Crynwr packet driver.
 * If you are porting to a new ethercard, look at the packet driver source
 * for hints. The NEx000 doesn't share the on-board packet memory --
 * you have to put the packet out through the "remote DMA" dataport
 * using z_writeb.
 */
static void zorro8390_block_input(struct net_device *dev, int count,
				  struct sk_buff *skb, int ring_offset)
{
	int nic_base = dev->base_addr;
	char *buf = skb->data;
	short *ptrs;
	int cnt;

	/* This *shouldn't* happen.
	 * If it does, it's the last thing you'll see
	 */
	if (ei_status.dmaing) {
		netdev_err(dev, "%s: DMAing conflict [DMAstat:%d][irqlock:%d]\n",
			   __func__, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	z_writeb(E8390_NODMA + E8390_PAGE0 + E8390_START, nic_base + NE_CMD);
	z_writeb(ENISR_RDC, nic_base + NE_EN0_ISR);
	z_writeb(count & 0xff, nic_base + NE_EN0_RCNTLO);
	z_writeb(count >> 8, nic_base + NE_EN0_RCNTHI);
	z_writeb(ring_offset & 0xff, nic_base + NE_EN0_RSARLO);
	z_writeb(ring_offset >> 8, nic_base + NE_EN0_RSARHI);
	z_writeb(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	ptrs = (short *)buf;
	for (cnt = 0; cnt < count >> 1; cnt++)
		*ptrs++ = z_readw(NE_BASE + NE_DATAPORT);
	if (count & 0x01)
		buf[count - 1] = z_readb(NE_BASE + NE_DATAPORT);

	z_writeb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr */
	ei_status.dmaing &= ~0x01;
}

static void zorro8390_block_output(struct net_device *dev, int count,
				   const unsigned char *buf,
				   const int start_page)
{
	int nic_base = NE_BASE;
	unsigned long dma_start;
	short *ptrs;
	int cnt;

	/* Round the count up for word writes.  Do we need to do this?
	 * What effect will an odd byte count have on the 8390?
	 * I should check someday.
	 */
	if (count & 0x01)
		count++;

	/* This *shouldn't* happen.
	 * If it does, it's the last thing you'll see
	 */
	if (ei_status.dmaing) {
		netdev_err(dev, "%s: DMAing conflict [DMAstat:%d][irqlock:%d]\n",
			   __func__, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	z_writeb(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

	z_writeb(ENISR_RDC, nic_base + NE_EN0_ISR);

	/* Now the normal output. */
	z_writeb(count & 0xff, nic_base + NE_EN0_RCNTLO);
	z_writeb(count >> 8,   nic_base + NE_EN0_RCNTHI);
	z_writeb(0x00, nic_base + NE_EN0_RSARLO);
	z_writeb(start_page, nic_base + NE_EN0_RSARHI);

	z_writeb(E8390_RWRITE + E8390_START, nic_base + NE_CMD);
	ptrs = (short *)buf;
	for (cnt = 0; cnt < count >> 1; cnt++)
		z_writew(*ptrs++, NE_BASE + NE_DATAPORT);

	dma_start = jiffies;

	while ((z_readb(NE_BASE + NE_EN0_ISR) & ENISR_RDC) == 0)
		if (time_after(jiffies, dma_start + 2 * HZ / 100)) {
					/* 20ms */
			netdev_err(dev, "timeout waiting for Tx RDC\n");
			zorro8390_reset_8390(dev);
			__NS8390_init(dev, 1);
			break;
		}

	z_writeb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr */
	ei_status.dmaing &= ~0x01;
}

static int zorro8390_open(struct net_device *dev)
{
	__ei_open(dev);
	return 0;
}

static int zorro8390_close(struct net_device *dev)
{
	if (ei_debug > 1)
		netdev_dbg(dev, "Shutting down ethercard\n");
	__ei_close(dev);
	return 0;
}

static void __devexit zorro8390_remove_one(struct zorro_dev *z)
{
	struct net_device *dev = zorro_get_drvdata(z);

	unregister_netdev(dev);
	free_irq(IRQ_AMIGA_PORTS, dev);
	release_mem_region(ZTWO_PADDR(dev->base_addr), NE_IO_EXTENT * 2);
	free_netdev(dev);
}

static struct zorro_device_id zorro8390_zorro_tbl[] __devinitdata = {
	{ ZORRO_PROD_VILLAGE_TRONIC_ARIADNE2, },
	{ ZORRO_PROD_INDIVIDUAL_COMPUTERS_X_SURF, },
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, zorro8390_zorro_tbl);

static const struct net_device_ops zorro8390_netdev_ops = {
	.ndo_open		= zorro8390_open,
	.ndo_stop		= zorro8390_close,
	.ndo_start_xmit		= __ei_start_xmit,
	.ndo_tx_timeout		= __ei_tx_timeout,
	.ndo_get_stats		= __ei_get_stats,
	.ndo_set_rx_mode	= __ei_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= __ei_poll,
#endif
};

static int __devinit zorro8390_init(struct net_device *dev,
				    unsigned long board, const char *name,
				    unsigned long ioaddr)
{
	int i;
	int err;
	unsigned char SA_prom[32];
	int start_page, stop_page;
	static u32 zorro8390_offsets[16] = {
		0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e,
		0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e,
	};

	/* Reset card. Who knows what dain-bramaged state it was left in. */
	{
		unsigned long reset_start_time = jiffies;

		z_writeb(z_readb(ioaddr + NE_RESET), ioaddr + NE_RESET);

		while ((z_readb(ioaddr + NE_EN0_ISR) & ENISR_RESET) == 0)
			if (time_after(jiffies,
				       reset_start_time + 2 * HZ / 100)) {
				netdev_warn(dev, "not found (no reset ack)\n");
				return -ENODEV;
			}

		z_writeb(0xff, ioaddr + NE_EN0_ISR);	/* Ack all intr. */
	}

	/* Read the 16 bytes of station address PROM.
	 * We must first initialize registers,
	 * similar to NS8390_init(eifdev, 0).
	 * We can't reliably read the SAPROM address without this.
	 * (I learned the hard way!).
	 */
	{
		static const struct {
			u32 value;
			u32 offset;
		} program_seq[] = {
			{E8390_NODMA + E8390_PAGE0 + E8390_STOP, NE_CMD},
						/* Select page 0 */
			{0x48,	NE_EN0_DCFG},	/* 0x48: Set byte-wide access */
			{0x00,	NE_EN0_RCNTLO},	/* Clear the count regs */
			{0x00,	NE_EN0_RCNTHI},
			{0x00,	NE_EN0_IMR},	/* Mask completion irq */
			{0xFF,	NE_EN0_ISR},
			{E8390_RXOFF, NE_EN0_RXCR}, /* 0x20 Set to monitor */
			{E8390_TXOFF, NE_EN0_TXCR}, /* 0x02 and loopback mode */
			{32,	NE_EN0_RCNTLO},
			{0x00,	NE_EN0_RCNTHI},
			{0x00,	NE_EN0_RSARLO},	/* DMA starting at 0x0000 */
			{0x00,	NE_EN0_RSARHI},
			{E8390_RREAD + E8390_START, NE_CMD},
		};
		for (i = 0; i < ARRAY_SIZE(program_seq); i++)
			z_writeb(program_seq[i].value,
				 ioaddr + program_seq[i].offset);
	}
	for (i = 0; i < 16; i++) {
		SA_prom[i] = z_readb(ioaddr + NE_DATAPORT);
		(void)z_readb(ioaddr + NE_DATAPORT);
	}

	/* We must set the 8390 for word mode. */
	z_writeb(0x49, ioaddr + NE_EN0_DCFG);
	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;

	dev->base_addr = ioaddr;
	dev->irq = IRQ_AMIGA_PORTS;

	/* Install the Interrupt handler */
	i = request_irq(IRQ_AMIGA_PORTS, __ei_interrupt,
			IRQF_SHARED, DRV_NAME, dev);
	if (i)
		return i;

	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = SA_prom[i];

	pr_debug("Found ethernet address: %pM\n", dev->dev_addr);

	ei_status.name = name;
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = 1;

	ei_status.rx_start_page = start_page + TX_PAGES;

	ei_status.reset_8390 = zorro8390_reset_8390;
	ei_status.block_input = zorro8390_block_input;
	ei_status.block_output = zorro8390_block_output;
	ei_status.get_8390_hdr = zorro8390_get_8390_hdr;
	ei_status.reg_offset = zorro8390_offsets;

	dev->netdev_ops = &zorro8390_netdev_ops;
	__NS8390_init(dev, 0);
	err = register_netdev(dev);
	if (err) {
		free_irq(IRQ_AMIGA_PORTS, dev);
		return err;
	}

	netdev_info(dev, "%s at 0x%08lx, Ethernet Address %pM\n",
		    name, board, dev->dev_addr);

	return 0;
}

static int __devinit zorro8390_init_one(struct zorro_dev *z,
					const struct zorro_device_id *ent)
{
	struct net_device *dev;
	unsigned long board, ioaddr;
	int err, i;

	for (i = ARRAY_SIZE(cards) - 1; i >= 0; i--)
		if (z->id == cards[i].id)
			break;
	if (i < 0)
		return -ENODEV;

	board = z->resource.start;
	ioaddr = board + cards[i].offset;
	dev = ____alloc_ei_netdev(0);
	if (!dev)
		return -ENOMEM;
	if (!request_mem_region(ioaddr, NE_IO_EXTENT * 2, DRV_NAME)) {
		free_netdev(dev);
		return -EBUSY;
	}
	err = zorro8390_init(dev, board, cards[i].name, ZTWO_VADDR(ioaddr));
	if (err) {
		release_mem_region(ioaddr, NE_IO_EXTENT * 2);
		free_netdev(dev);
		return err;
	}
	zorro_set_drvdata(z, dev);
	return 0;
}

static struct zorro_driver zorro8390_driver = {
	.name		= "zorro8390",
	.id_table	= zorro8390_zorro_tbl,
	.probe		= zorro8390_init_one,
	.remove		= __devexit_p(zorro8390_remove_one),
};

static int __init zorro8390_init_module(void)
{
	return zorro_register_driver(&zorro8390_driver);
}

static void __exit zorro8390_cleanup_module(void)
{
	zorro_unregister_driver(&zorro8390_driver);
}

module_init(zorro8390_init_module);
module_exit(zorro8390_cleanup_module);

MODULE_LICENSE("GPL");

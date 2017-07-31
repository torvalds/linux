/* wd.c: A WD80x3 ethernet driver for linux. */
/*
	Written 1993-94 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	This is a driver for WD8003 and WD8013 "compatible" ethercards.

	Thanks to Russ Nelson (nelson@crnwyr.com) for loaning me a WD8013.

	Changelog:

	Paul Gortmaker	: multiple card support for module users, support
			  for non-standard memory sizes.


*/

static const char version[] =
	"wd.c:v1.10 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/io.h>

#include "8390.h"

#define DRV_NAME "wd"

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int wd_portlist[] __initdata =
{0x300, 0x280, 0x380, 0x240, 0};

static int wd_probe1(struct net_device *dev, int ioaddr);

static int wd_open(struct net_device *dev);
static void wd_reset_8390(struct net_device *dev);
static void wd_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void wd_block_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void wd_block_output(struct net_device *dev, int count,
							const unsigned char *buf, int start_page);
static int wd_close(struct net_device *dev);

static u32 wd_msg_enable;

#define WD_START_PG		0x00	/* First page of TX buffer */
#define WD03_STOP_PG	0x20	/* Last page +1 of RX ring */
#define WD13_STOP_PG	0x40	/* Last page +1 of RX ring */

#define WD_CMDREG		0		/* Offset to ASIC command register. */
#define	 WD_RESET		0x80	/* Board reset, in WD_CMDREG. */
#define	 WD_MEMENB		0x40	/* Enable the shared memory. */
#define WD_CMDREG5		5		/* Offset to 16-bit-only ASIC register 5. */
#define	 ISA16			0x80	/* Enable 16 bit access from the ISA bus. */
#define	 NIC16			0x40	/* Enable 16 bit access from the 8390. */
#define WD_NIC_OFFSET	16		/* Offset to the 8390 from the base_addr. */
#define WD_IO_EXTENT	32


/*	Probe for the WD8003 and WD8013.  These cards have the station
	address PROM at I/O ports <base>+8 to <base>+13, with a checksum
	following. A Soundblaster can have the same checksum as an WDethercard,
	so we have an extra exclusionary check for it.

	The wd_probe1() routine initializes the card and fills the
	station address field. */

static int __init do_wd_probe(struct net_device *dev)
{
	int i;
	struct resource *r;
	int base_addr = dev->base_addr;
	int irq = dev->irq;
	int mem_start = dev->mem_start;
	int mem_end = dev->mem_end;

	if (base_addr > 0x1ff) {	/* Check a user specified location. */
		r = request_region(base_addr, WD_IO_EXTENT, "wd-probe");
		if ( r == NULL)
			return -EBUSY;
		i = wd_probe1(dev, base_addr);
		if (i != 0)
			release_region(base_addr, WD_IO_EXTENT);
		else
			r->name = dev->name;
		return i;
	}
	else if (base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;

	for (i = 0; wd_portlist[i]; i++) {
		int ioaddr = wd_portlist[i];
		r = request_region(ioaddr, WD_IO_EXTENT, "wd-probe");
		if (r == NULL)
			continue;
		if (wd_probe1(dev, ioaddr) == 0) {
			r->name = dev->name;
			return 0;
		}
		release_region(ioaddr, WD_IO_EXTENT);
		dev->irq = irq;
		dev->mem_start = mem_start;
		dev->mem_end = mem_end;
	}

	return -ENODEV;
}

#ifndef MODULE
struct net_device * __init wd_probe(int unit)
{
	struct net_device *dev = alloc_ei_netdev();
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_wd_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static const struct net_device_ops wd_netdev_ops = {
	.ndo_open		= wd_open,
	.ndo_stop		= wd_close,
	.ndo_start_xmit		= ei_start_xmit,
	.ndo_tx_timeout		= ei_tx_timeout,
	.ndo_get_stats		= ei_get_stats,
	.ndo_set_rx_mode	= ei_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller 	= ei_poll,
#endif
};

static int __init wd_probe1(struct net_device *dev, int ioaddr)
{
	int i;
	int err;
	int checksum = 0;
	int ancient = 0;			/* An old card without config registers. */
	int word16 = 0;				/* 0 = 8 bit, 1 = 16 bit */
	const char *model_name;
	static unsigned version_printed;
	struct ei_device *ei_local = netdev_priv(dev);

	for (i = 0; i < 8; i++)
		checksum += inb(ioaddr + 8 + i);
	if (inb(ioaddr + 8) == 0xff 	/* Extra check to avoid soundcard. */
		|| inb(ioaddr + 9) == 0xff
		|| (checksum & 0xff) != 0xFF)
		return -ENODEV;

	/* Check for semi-valid mem_start/end values if supplied. */
	if ((dev->mem_start % 0x2000) || (dev->mem_end % 0x2000)) {
		netdev_warn(dev,
			    "wd.c: user supplied mem_start or mem_end not on 8kB boundary - ignored.\n");
		dev->mem_start = 0;
		dev->mem_end = 0;
	}

	if ((wd_msg_enable & NETIF_MSG_DRV) && (version_printed++ == 0))
		netdev_info(dev, version);

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(ioaddr + 8 + i);

	netdev_info(dev, "WD80x3 at %#3x, %pM", ioaddr, dev->dev_addr);

	/* The following PureData probe code was contributed by
	   Mike Jagdis <jaggy@purplet.demon.co.uk>. Puredata does software
	   configuration differently from others so we have to check for them.
	   This detects an 8 bit, 16 bit or dumb (Toshiba, jumpered) card.
	   */
	if (inb(ioaddr+0) == 'P' && inb(ioaddr+1) == 'D') {
		unsigned char reg5 = inb(ioaddr+5);

		switch (inb(ioaddr+2)) {
		case 0x03: word16 = 0; model_name = "PDI8023-8";	break;
		case 0x05: word16 = 0; model_name = "PDUC8023";	break;
		case 0x0a: word16 = 1; model_name = "PDI8023-16"; break;
			/* Either 0x01 (dumb) or they've released a new version. */
		default:	 word16 = 0; model_name = "PDI8023";	break;
		}
		dev->mem_start = ((reg5 & 0x1c) + 0xc0) << 12;
		dev->irq = (reg5 & 0xe0) == 0xe0 ? 10 : (reg5 >> 5) + 1;
	} else {								/* End of PureData probe */
		/* This method of checking for a 16-bit board is borrowed from the
		   we.c driver.  A simpler method is just to look in ASIC reg. 0x03.
		   I'm comparing the two method in alpha test to make certain they
		   return the same result. */
		/* Check for the old 8 bit board - it has register 0/8 aliasing.
		   Do NOT check i>=6 here -- it hangs the old 8003 boards! */
		for (i = 0; i < 6; i++)
			if (inb(ioaddr+i) != inb(ioaddr+8+i))
				break;
		if (i >= 6) {
			ancient = 1;
			model_name = "WD8003-old";
			word16 = 0;
		} else {
			int tmp = inb(ioaddr+1); /* fiddle with 16bit bit */
			outb( tmp ^ 0x01, ioaddr+1 ); /* attempt to clear 16bit bit */
			if (((inb( ioaddr+1) & 0x01) == 0x01) /* A 16 bit card */
				&& (tmp & 0x01) == 0x01	) {				/* In a 16 slot. */
				int asic_reg5 = inb(ioaddr+WD_CMDREG5);
				/* Magic to set ASIC to word-wide mode. */
				outb( NIC16 | (asic_reg5&0x1f), ioaddr+WD_CMDREG5);
				outb(tmp, ioaddr+1);
				model_name = "WD8013";
				word16 = 1;		/* We have a 16bit board here! */
			} else {
				model_name = "WD8003";
				word16 = 0;
			}
			outb(tmp, ioaddr+1);			/* Restore original reg1 value. */
		}
#ifndef final_version
		if ( !ancient && (inb(ioaddr+1) & 0x01) != (word16 & 0x01))
			pr_cont("\nWD80?3: Bus width conflict, %d (probe) != %d (reg report).",
				word16 ? 16 : 8,
				(inb(ioaddr+1) & 0x01) ? 16 : 8);
#endif
	}

#if defined(WD_SHMEM) && WD_SHMEM > 0x80000
	/* Allow a compile-time override.	 */
	dev->mem_start = WD_SHMEM;
#else
	if (dev->mem_start == 0) {
		/* Sanity and old 8003 check */
		int reg0 = inb(ioaddr);
		if (reg0 == 0xff || reg0 == 0) {
			/* Future plan: this could check a few likely locations first. */
			dev->mem_start = 0xd0000;
			pr_cont(" assigning address %#lx", dev->mem_start);
		} else {
			int high_addr_bits = inb(ioaddr+WD_CMDREG5) & 0x1f;
			/* Some boards don't have the register 5 -- it returns 0xff. */
			if (high_addr_bits == 0x1f || word16 == 0)
				high_addr_bits = 0x01;
			dev->mem_start = ((reg0&0x3f) << 13) + (high_addr_bits << 19);
		}
	}
#endif

	/* The 8390 isn't at the base address -- the ASIC regs are there! */
	dev->base_addr = ioaddr+WD_NIC_OFFSET;

	if (dev->irq < 2) {
		static const int irqmap[] = {9, 3, 5, 7, 10, 11, 15, 4};
		int reg1 = inb(ioaddr+1);
		int reg4 = inb(ioaddr+4);
		if (ancient || reg1 == 0xff) {	/* Ack!! No way to read the IRQ! */
			short nic_addr = ioaddr+WD_NIC_OFFSET;
			unsigned long irq_mask;

			/* We have an old-style ethercard that doesn't report its IRQ
			   line.  Do autoirq to find the IRQ line. Note that this IS NOT
			   a reliable way to trigger an interrupt. */
			outb_p(E8390_NODMA + E8390_STOP, nic_addr);
			outb(0x00, nic_addr+EN0_IMR);	/* Disable all intrs. */

			irq_mask = probe_irq_on();
			outb_p(0xff, nic_addr + EN0_IMR);	/* Enable all interrupts. */
			outb_p(0x00, nic_addr + EN0_RCNTLO);
			outb_p(0x00, nic_addr + EN0_RCNTHI);
			outb(E8390_RREAD+E8390_START, nic_addr); /* Trigger it... */
			mdelay(20);
			dev->irq = probe_irq_off(irq_mask);

			outb_p(0x00, nic_addr+EN0_IMR);	/* Mask all intrs. again. */

			if (netif_msg_drv(ei_local))
				pr_cont(" autoirq is %d", dev->irq);
			if (dev->irq < 2)
				dev->irq = word16 ? 10 : 5;
		} else
			dev->irq = irqmap[((reg4 >> 5) & 0x03) + (reg1 & 0x04)];
	} else if (dev->irq == 2)		/* Fixup bogosity: IRQ2 is really IRQ9 */
		dev->irq = 9;

	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share and the board will usually be enabled. */
	i = request_irq(dev->irq, ei_interrupt, 0, DRV_NAME, dev);
	if (i) {
		pr_cont(" unable to get IRQ %d.\n", dev->irq);
		return i;
	}

	/* OK, were are certain this is going to work.  Setup the device. */
	ei_status.name = model_name;
	ei_status.word16 = word16;
	ei_status.tx_start_page = WD_START_PG;
	ei_status.rx_start_page = WD_START_PG + TX_PAGES;

	/* Don't map in the shared memory until the board is actually opened. */

	/* Some cards (eg WD8003EBT) can be jumpered for more (32k!) memory. */
	if (dev->mem_end != 0) {
		ei_status.stop_page = (dev->mem_end - dev->mem_start)/256;
		ei_status.priv = dev->mem_end - dev->mem_start;
	} else {
		ei_status.stop_page = word16 ? WD13_STOP_PG : WD03_STOP_PG;
		dev->mem_end = dev->mem_start + (ei_status.stop_page - WD_START_PG)*256;
		ei_status.priv = (ei_status.stop_page - WD_START_PG)*256;
	}

	ei_status.mem = ioremap(dev->mem_start, ei_status.priv);
	if (!ei_status.mem) {
		free_irq(dev->irq, dev);
		return -ENOMEM;
	}

	pr_cont(" %s, IRQ %d, shared memory at %#lx-%#lx.\n",
		model_name, dev->irq, dev->mem_start, dev->mem_end-1);

	ei_status.reset_8390 = wd_reset_8390;
	ei_status.block_input = wd_block_input;
	ei_status.block_output = wd_block_output;
	ei_status.get_8390_hdr = wd_get_8390_hdr;

	dev->netdev_ops = &wd_netdev_ops;
	NS8390_init(dev, 0);
	ei_local->msg_enable = wd_msg_enable;

#if 1
	/* Enable interrupt generation on softconfig cards -- M.U */
	/* .. but possibly potentially unsafe - Donald */
	if (inb(ioaddr+14) & 0x20)
		outb(inb(ioaddr+4)|0x80, ioaddr+4);
#endif

	err = register_netdev(dev);
	if (err) {
		free_irq(dev->irq, dev);
		iounmap(ei_status.mem);
	}
	return err;
}

static int
wd_open(struct net_device *dev)
{
  int ioaddr = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */

  /* Map in the shared memory. Always set register 0 last to remain
	 compatible with very old boards. */
  ei_status.reg0 = ((dev->mem_start>>13) & 0x3f) | WD_MEMENB;
  ei_status.reg5 = ((dev->mem_start>>19) & 0x1f) | NIC16;

  if (ei_status.word16)
	  outb(ei_status.reg5, ioaddr+WD_CMDREG5);
  outb(ei_status.reg0, ioaddr); /* WD_CMDREG */

  return ei_open(dev);
}

static void
wd_reset_8390(struct net_device *dev)
{
	int wd_cmd_port = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	struct ei_device *ei_local = netdev_priv(dev);

	outb(WD_RESET, wd_cmd_port);
	netif_dbg(ei_local, hw, dev, "resetting the WD80x3 t=%lu...\n",
		  jiffies);
	ei_status.txing = 0;

	/* Set up the ASIC registers, just in case something changed them. */
	outb((((dev->mem_start>>13) & 0x3f)|WD_MEMENB), wd_cmd_port);
	if (ei_status.word16)
		outb(NIC16 | ((dev->mem_start>>19) & 0x1f), wd_cmd_port+WD_CMDREG5);

	netif_dbg(ei_local, hw, dev, "reset done\n");
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void
wd_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{

	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	void __iomem *hdr_start = ei_status.mem + ((ring_page - WD_START_PG)<<8);

	/* We'll always get a 4 byte header read followed by a packet read, so
	   we enable 16 bit mode before the header, and disable after the body. */
	if (ei_status.word16)
		outb(ISA16 | ei_status.reg5, wd_cmdreg+WD_CMDREG5);

#ifdef __BIG_ENDIAN
	/* Officially this is what we are doing, but the readl() is faster */
	/* unfortunately it isn't endian aware of the struct               */
	memcpy_fromio(hdr, hdr_start, sizeof(struct e8390_pkt_hdr));
	hdr->count = le16_to_cpu(hdr->count);
#else
	((unsigned int*)hdr)[0] = readl(hdr_start);
#endif
}

/* Block input and output are easy on shared memory ethercards, and trivial
   on the Western digital card where there is no choice of how to do it.
   The only complications are that the ring buffer wraps, and need to map
   switch between 8- and 16-bit modes. */

static void
wd_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	unsigned long offset = ring_offset - (WD_START_PG<<8);
	void __iomem *xfer_start = ei_status.mem + offset;

	if (offset + count > ei_status.priv) {
		/* We must wrap the input move. */
		int semi_count = ei_status.priv - offset;
		memcpy_fromio(skb->data, xfer_start, semi_count);
		count -= semi_count;
		memcpy_fromio(skb->data + semi_count, ei_status.mem + TX_PAGES * 256, count);
	} else {
		/* Packet is in one chunk -- we can copy + cksum. */
		memcpy_fromio(skb->data, xfer_start, count);
	}

	/* Turn off 16 bit access so that reboot works.	 ISA brain-damage */
	if (ei_status.word16)
		outb(ei_status.reg5, wd_cmdreg+WD_CMDREG5);
}

static void
wd_block_output(struct net_device *dev, int count, const unsigned char *buf,
				int start_page)
{
	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	void __iomem *shmem = ei_status.mem + ((start_page - WD_START_PG)<<8);


	if (ei_status.word16) {
		/* Turn on and off 16 bit access so that reboot works. */
		outb(ISA16 | ei_status.reg5, wd_cmdreg+WD_CMDREG5);
		memcpy_toio(shmem, buf, count);
		outb(ei_status.reg5, wd_cmdreg+WD_CMDREG5);
	} else
		memcpy_toio(shmem, buf, count);
}


static int
wd_close(struct net_device *dev)
{
	int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
	struct ei_device *ei_local = netdev_priv(dev);

	netif_dbg(ei_local, ifdown, dev, "Shutting down ethercard.\n");
	ei_close(dev);

	/* Change from 16-bit to 8-bit shared memory so reboot works. */
	if (ei_status.word16)
		outb(ei_status.reg5, wd_cmdreg + WD_CMDREG5 );

	/* And disable the shared memory. */
	outb(ei_status.reg0 & ~WD_MEMENB, wd_cmdreg);

	return 0;
}


#ifdef MODULE
#define MAX_WD_CARDS	4	/* Max number of wd cards per module */
static struct net_device *dev_wd[MAX_WD_CARDS];
static int io[MAX_WD_CARDS];
static int irq[MAX_WD_CARDS];
static int mem[MAX_WD_CARDS];
static int mem_end[MAX_WD_CARDS];	/* for non std. mem size */

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(mem, int, NULL, 0);
module_param_array(mem_end, int, NULL, 0);
module_param_named(msg_enable, wd_msg_enable, uint, (S_IRUSR|S_IRGRP|S_IROTH));
MODULE_PARM_DESC(io, "I/O base address(es)");
MODULE_PARM_DESC(irq, "IRQ number(s) (ignored for PureData boards)");
MODULE_PARM_DESC(mem, "memory base address(es)(ignored for PureData boards)");
MODULE_PARM_DESC(mem_end, "memory end address(es)");
MODULE_PARM_DESC(msg_enable, "Debug message level (see linux/netdevice.h for bitmap)");
MODULE_DESCRIPTION("ISA Western Digital wd8003/wd8013 ; SMC Elite, Elite16 ethernet driver");
MODULE_LICENSE("GPL");

/* This is set up so that only a single autoprobe takes place per call.
ISA device autoprobes on a running machine are not recommended. */

int __init init_module(void)
{
	struct net_device *dev;
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_WD_CARDS; this_dev++) {
		if (io[this_dev] == 0)  {
			if (this_dev != 0) break; /* only autoprobe 1st one */
			printk(KERN_NOTICE "wd.c: Presently autoprobing (not recommended) for a single card.\n");
		}
		dev = alloc_ei_netdev();
		if (!dev)
			break;
		dev->irq = irq[this_dev];
		dev->base_addr = io[this_dev];
		dev->mem_start = mem[this_dev];
		dev->mem_end = mem_end[this_dev];
		if (do_wd_probe(dev) == 0) {
			dev_wd[found++] = dev;
			continue;
		}
		free_netdev(dev);
		printk(KERN_WARNING "wd.c: No wd80x3 card found (i/o = 0x%x).\n", io[this_dev]);
		break;
	}
	if (found)
		return 0;
	return -ENXIO;
}

static void cleanup_card(struct net_device *dev)
{
	free_irq(dev->irq, dev);
	release_region(dev->base_addr - WD_NIC_OFFSET, WD_IO_EXTENT);
	iounmap(ei_status.mem);
}

void __exit
cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_WD_CARDS; this_dev++) {
		struct net_device *dev = dev_wd[this_dev];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}
#endif /* MODULE */

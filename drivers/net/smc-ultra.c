/* smc-ultra.c: A SMC Ultra ethernet driver for linux. */
/*
	This is a driver for the SMC Ultra and SMC EtherEZ ISA ethercards.

	Written 1993-1998 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	This driver uses the cards in the 8390-compatible mode.
	Most of the run-time complexity is handled by the generic code in
	8390.c.  The code in this file is responsible for

		ultra_probe()	 	Detecting and initializing the card.
		ultra_probe1()
		ultra_probe_isapnp()

		ultra_open()		The card-specific details of starting, stopping
		ultra_reset_8390()	and resetting the 8390 NIC core.
		ultra_close()

		ultra_block_input()		Routines for reading and writing blocks of
		ultra_block_output()	packet buffer memory.
		ultra_pio_input()
		ultra_pio_output()

	This driver enables the shared memory only when doing the actual data
	transfers to avoid a bug in early version of the card that corrupted
	data transferred by a AHA1542.

	This driver now supports the programmed-I/O (PIO) data transfer mode of
	the EtherEZ. It does not use the non-8390-compatible "Altego" mode.
	That support (if available) is in smc-ez.c.

	Changelog:

	Paul Gortmaker	: multiple card support for module users.
	Donald Becker	: 4/17/96 PIO support, minor potential problems avoided.
	Donald Becker	: 6/6/96 correctly set auto-wrap bit.
	Alexander Sotirov : 1/20/01 Added support for ISAPnP cards

	Note about the ISA PnP support:

	This driver can not autoprobe for more than one SMC EtherEZ PnP card.
	You have to configure the second card manually through the /proc/isapnp
	interface and then load the module with an explicit io=0x___ option.
*/

static const char version[] =
	"smc-ultra.c:v2.02 2/3/98 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/isapnp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "8390.h"

#define DRV_NAME "smc-ultra"

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int ultra_portlist[] __initdata =
{0x200, 0x220, 0x240, 0x280, 0x300, 0x340, 0x380, 0};

static int ultra_probe1(struct net_device *dev, int ioaddr);

#ifdef __ISAPNP__
static int ultra_probe_isapnp(struct net_device *dev);
#endif

static int ultra_open(struct net_device *dev);
static void ultra_reset_8390(struct net_device *dev);
static void ultra_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void ultra_block_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void ultra_block_output(struct net_device *dev, int count,
							const unsigned char *buf, const int start_page);
static void ultra_pio_get_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void ultra_pio_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void ultra_pio_output(struct net_device *dev, int count,
							 const unsigned char *buf, const int start_page);
static int ultra_close_card(struct net_device *dev);

#ifdef __ISAPNP__
static struct isapnp_device_id ultra_device_ids[] __initdata = {
        {       ISAPNP_VENDOR('S','M','C'), ISAPNP_FUNCTION(0x8416),
                ISAPNP_VENDOR('S','M','C'), ISAPNP_FUNCTION(0x8416),
                (long) "SMC EtherEZ (8416)" },
        { }	/* terminate list */
};

MODULE_DEVICE_TABLE(isapnp, ultra_device_ids);
#endif


#define START_PG		0x00	/* First page of TX buffer */

#define ULTRA_CMDREG	0		/* Offset to ASIC command register. */
#define	 ULTRA_RESET	0x80	/* Board reset, in ULTRA_CMDREG. */
#define	 ULTRA_MEMENB	0x40	/* Enable the shared memory. */
#define IOPD	0x02			/* I/O Pipe Data (16 bits), PIO operation. */
#define IOPA	0x07			/* I/O Pipe Address for PIO operation. */
#define ULTRA_NIC_OFFSET  16	/* NIC register offset from the base_addr. */
#define ULTRA_IO_EXTENT 32
#define EN0_ERWCNT		0x08	/* Early receive warning count. */

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ultra_poll(struct net_device *dev)
{
	disable_irq(dev->irq);
	ei_interrupt(dev->irq, dev, NULL);
	enable_irq(dev->irq);
}
#endif
/*	Probe for the Ultra.  This looks like a 8013 with the station
	address PROM at I/O ports <base>+8 to <base>+13, with a checksum
	following.
*/

static int __init do_ultra_probe(struct net_device *dev)
{
	int i;
	int base_addr = dev->base_addr;
	int irq = dev->irq;

	SET_MODULE_OWNER(dev);

#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = &ultra_poll;
#endif
	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return ultra_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;

#ifdef __ISAPNP__
	/* Look for any installed ISAPnP cards */
	if (isapnp_present() && (ultra_probe_isapnp(dev) == 0))
		return 0;
#endif

	for (i = 0; ultra_portlist[i]; i++) {
		dev->irq = irq;
		if (ultra_probe1(dev, ultra_portlist[i]) == 0)
			return 0;
	}

	return -ENODEV;
}

#ifndef MODULE
struct net_device * __init ultra_probe(int unit)
{
	struct net_device *dev = alloc_ei_netdev();
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_ultra_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static int __init ultra_probe1(struct net_device *dev, int ioaddr)
{
	int i, retval;
	int checksum = 0;
	const char *model_name;
	unsigned char eeprom_irq = 0;
	static unsigned version_printed;
	/* Values from various config regs. */
	unsigned char num_pages, irqreg, addr, piomode;
	unsigned char idreg = inb(ioaddr + 7);
	unsigned char reg4 = inb(ioaddr + 4) & 0x7f;

	if (!request_region(ioaddr, ULTRA_IO_EXTENT, DRV_NAME))
		return -EBUSY;

	/* Check the ID nibble. */
	if ((idreg & 0xF0) != 0x20 			/* SMC Ultra */
		&& (idreg & 0xF0) != 0x40) {		/* SMC EtherEZ */
		retval = -ENODEV;
		goto out;
	}

	/* Select the station address register set. */
	outb(reg4, ioaddr + 4);

	for (i = 0; i < 8; i++)
		checksum += inb(ioaddr + 8 + i);
	if ((checksum & 0xff) != 0xFF) {
		retval = -ENODEV;
		goto out;
	}

	if (ei_debug  &&  version_printed++ == 0)
		printk(version);

	model_name = (idreg & 0xF0) == 0x20 ? "SMC Ultra" : "SMC EtherEZ";

	printk("%s: %s at %#3x,", dev->name, model_name, ioaddr);

	for (i = 0; i < 6; i++)
		printk(" %2.2X", dev->dev_addr[i] = inb(ioaddr + 8 + i));

	/* Switch from the station address to the alternate register set and
	   read the useful registers there. */
	outb(0x80 | reg4, ioaddr + 4);

	/* Enabled FINE16 mode to avoid BIOS ROM width mismatches @ reboot. */
	outb(0x80 | inb(ioaddr + 0x0c), ioaddr + 0x0c);
	piomode = inb(ioaddr + 0x8);
	addr = inb(ioaddr + 0xb);
	irqreg = inb(ioaddr + 0xd);

	/* Switch back to the station address register set so that the MS-DOS driver
	   can find the card after a warm boot. */
	outb(reg4, ioaddr + 4);

	if (dev->irq < 2) {
		unsigned char irqmap[] = {0, 9, 3, 5, 7, 10, 11, 15};
		int irq;

		/* The IRQ bits are split. */
		irq = irqmap[((irqreg & 0x40) >> 4) + ((irqreg & 0x0c) >> 2)];

		if (irq == 0) {
			printk(", failed to detect IRQ line.\n");
			retval =  -EAGAIN;
			goto out;
		}
		dev->irq = irq;
		eeprom_irq = 1;
	}

	/* The 8390 isn't at the base address, so fake the offset */
	dev->base_addr = ioaddr+ULTRA_NIC_OFFSET;

	{
		int addr_tbl[4] = {0x0C0000, 0x0E0000, 0xFC0000, 0xFE0000};
		short num_pages_tbl[4] = {0x20, 0x40, 0x80, 0xff};

		dev->mem_start = ((addr & 0x0f) << 13) + addr_tbl[(addr >> 6) & 3] ;
		num_pages = num_pages_tbl[(addr >> 4) & 3];
	}

	ei_status.name = model_name;
	ei_status.word16 = 1;
	ei_status.tx_start_page = START_PG;
	ei_status.rx_start_page = START_PG + TX_PAGES;
	ei_status.stop_page = num_pages;

	ei_status.mem = ioremap(dev->mem_start, (ei_status.stop_page - START_PG)*256);
	if (!ei_status.mem) {
		printk(", failed to ioremap.\n");
		retval =  -ENOMEM;
		goto out;
	}

	dev->mem_end = dev->mem_start + (ei_status.stop_page - START_PG)*256;

	if (piomode) {
		printk(",%s IRQ %d programmed-I/O mode.\n",
			   eeprom_irq ? "EEPROM" : "assigned ", dev->irq);
		ei_status.block_input = &ultra_pio_input;
		ei_status.block_output = &ultra_pio_output;
		ei_status.get_8390_hdr = &ultra_pio_get_hdr;
	} else {
		printk(",%s IRQ %d memory %#lx-%#lx.\n", eeprom_irq ? "" : "assigned ",
			   dev->irq, dev->mem_start, dev->mem_end-1);
		ei_status.block_input = &ultra_block_input;
		ei_status.block_output = &ultra_block_output;
		ei_status.get_8390_hdr = &ultra_get_8390_hdr;
	}
	ei_status.reset_8390 = &ultra_reset_8390;
	dev->open = &ultra_open;
	dev->stop = &ultra_close_card;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = ei_poll;
#endif
	NS8390_init(dev, 0);

	retval = register_netdev(dev);
	if (retval)
		goto out;
	return 0;
out:
	release_region(ioaddr, ULTRA_IO_EXTENT);
	return retval;
}

#ifdef __ISAPNP__
static int __init ultra_probe_isapnp(struct net_device *dev)
{
        int i;

        for (i = 0; ultra_device_ids[i].vendor != 0; i++) {
		struct pnp_dev *idev = NULL;

                while ((idev = pnp_find_dev(NULL,
                                            ultra_device_ids[i].vendor,
                                            ultra_device_ids[i].function,
                                            idev))) {
                        /* Avoid already found cards from previous calls */
                        if (pnp_device_attach(idev) < 0)
                        	continue;
                        if (pnp_activate_dev(idev) < 0) {
                              __again:
                        	pnp_device_detach(idev);
                        	continue;
                        }
			/* if no io and irq, search for next */
			if (!pnp_port_valid(idev, 0) || !pnp_irq_valid(idev, 0))
				goto __again;
                        /* found it */
			dev->base_addr = pnp_port_start(idev, 0);
			dev->irq = pnp_irq(idev, 0);
                        printk(KERN_INFO "smc-ultra.c: ISAPnP reports %s at i/o %#lx, irq %d.\n",
                                (char *) ultra_device_ids[i].driver_data,
                                dev->base_addr, dev->irq);
                        if (ultra_probe1(dev, dev->base_addr) != 0) {      /* Shouldn't happen. */
                                printk(KERN_ERR "smc-ultra.c: Probe of ISAPnP card at %#lx failed.\n", dev->base_addr);
                                pnp_device_detach(idev);
				return -ENXIO;
                        }
                        ei_status.priv = (unsigned long)idev;
                        break;
                }
                if (!idev)
                        continue;
                return 0;
        }

        return -ENODEV;
}
#endif

static int
ultra_open(struct net_device *dev)
{
	int retval;
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */
	unsigned char irq2reg[] = {0, 0, 0x04, 0x08, 0, 0x0C, 0, 0x40,
				   0, 0x04, 0x44, 0x48, 0, 0, 0, 0x4C, };

	retval = request_irq(dev->irq, ei_interrupt, 0, dev->name, dev);
	if (retval)
		return retval;

	outb(0x00, ioaddr);	/* Disable shared memory for safety. */
	outb(0x80, ioaddr + 5);
	/* Set the IRQ line. */
	outb(inb(ioaddr + 4) | 0x80, ioaddr + 4);
	outb((inb(ioaddr + 13) & ~0x4C) | irq2reg[dev->irq], ioaddr + 13);
	outb(inb(ioaddr + 4) & 0x7f, ioaddr + 4);

	if (ei_status.block_input == &ultra_pio_input) {
		outb(0x11, ioaddr + 6);		/* Enable interrupts and PIO. */
		outb(0x01, ioaddr + 0x19);  	/* Enable ring read auto-wrap. */
	} else
		outb(0x01, ioaddr + 6);		/* Enable interrupts and memory. */
	/* Set the early receive warning level in window 0 high enough not
	   to receive ERW interrupts. */
	outb_p(E8390_NODMA+E8390_PAGE0, dev->base_addr);
	outb(0xff, dev->base_addr + EN0_ERWCNT);
	ei_open(dev);
	return 0;
}

static void
ultra_reset_8390(struct net_device *dev)
{
	int cmd_port = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC base addr */

	outb(ULTRA_RESET, cmd_port);
	if (ei_debug > 1) printk("resetting Ultra, t=%ld...", jiffies);
	ei_status.txing = 0;

	outb(0x00, cmd_port);	/* Disable shared memory for safety. */
	outb(0x80, cmd_port + 5);
	if (ei_status.block_input == &ultra_pio_input)
		outb(0x11, cmd_port + 6);		/* Enable interrupts and PIO. */
	else
		outb(0x01, cmd_port + 6);		/* Enable interrupts and memory. */

	if (ei_debug > 1) printk("reset done\n");
	return;
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void
ultra_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	void __iomem *hdr_start = ei_status.mem + ((ring_page - START_PG)<<8);

	outb(ULTRA_MEMENB, dev->base_addr - ULTRA_NIC_OFFSET);	/* shmem on */
#ifdef __BIG_ENDIAN
	/* Officially this is what we are doing, but the readl() is faster */
	/* unfortunately it isn't endian aware of the struct               */
	memcpy_fromio(hdr, hdr_start, sizeof(struct e8390_pkt_hdr));
	hdr->count = le16_to_cpu(hdr->count);
#else
	((unsigned int*)hdr)[0] = readl(hdr_start);
#endif
	outb(0x00, dev->base_addr - ULTRA_NIC_OFFSET); /* shmem off */
}

/* Block input and output are easy on shared memory ethercards, the only
   complication is when the ring buffer wraps. */

static void
ultra_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	void __iomem *xfer_start = ei_status.mem + ring_offset - (START_PG<<8);

	/* Enable shared memory. */
	outb(ULTRA_MEMENB, dev->base_addr - ULTRA_NIC_OFFSET);

	if (ring_offset + count > ei_status.stop_page*256) {
		/* We must wrap the input move. */
		int semi_count = ei_status.stop_page*256 - ring_offset;
		memcpy_fromio(skb->data, xfer_start, semi_count);
		count -= semi_count;
		memcpy_fromio(skb->data + semi_count, ei_status.mem + TX_PAGES * 256, count);
	} else {
		/* Packet is in one chunk -- we can copy + cksum. */
		eth_io_copy_and_sum(skb, xfer_start, count, 0);
	}

	outb(0x00, dev->base_addr - ULTRA_NIC_OFFSET);	/* Disable memory. */
}

static void
ultra_block_output(struct net_device *dev, int count, const unsigned char *buf,
				int start_page)
{
	void __iomem *shmem = ei_status.mem + ((start_page - START_PG)<<8);

	/* Enable shared memory. */
	outb(ULTRA_MEMENB, dev->base_addr - ULTRA_NIC_OFFSET);

	memcpy_toio(shmem, buf, count);

	outb(0x00, dev->base_addr - ULTRA_NIC_OFFSET); /* Disable memory. */
}

/* The identical operations for programmed I/O cards.
   The PIO model is trivial to use: the 16 bit start address is written
   byte-sequentially to IOPA, with no intervening I/O operations, and the
   data is read or written to the IOPD data port.
   The only potential complication is that the address register is shared
   and must be always be rewritten between each read/write direction change.
   This is no problem for us, as the 8390 code ensures that we are single
   threaded. */
static void ultra_pio_get_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */
	outb(0x00, ioaddr + IOPA);	/* Set the address, LSB first. */
	outb(ring_page, ioaddr + IOPA);
	insw(ioaddr + IOPD, hdr, sizeof(struct e8390_pkt_hdr)>>1);
}

static void ultra_pio_input(struct net_device *dev, int count,
						  struct sk_buff *skb, int ring_offset)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */
    char *buf = skb->data;

	/* For now set the address again, although it should already be correct. */
	outb(ring_offset, ioaddr + IOPA);	/* Set the address, LSB first. */
	outb(ring_offset >> 8, ioaddr + IOPA);
	/* We know skbuffs are padded to at least word alignment. */
	insw(ioaddr + IOPD, buf, (count+1)>>1);
}

static void ultra_pio_output(struct net_device *dev, int count,
							const unsigned char *buf, const int start_page)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* ASIC addr */
	outb(0x00, ioaddr + IOPA);	/* Set the address, LSB first. */
	outb(start_page, ioaddr + IOPA);
	/* An extra odd byte is OK here as well. */
	outsw(ioaddr + IOPD, buf, (count+1)>>1);
}

static int
ultra_close_card(struct net_device *dev)
{
	int ioaddr = dev->base_addr - ULTRA_NIC_OFFSET; /* CMDREG */

	netif_stop_queue(dev);

	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);

	outb(0x00, ioaddr + 6);		/* Disable interrupts. */
	free_irq(dev->irq, dev);

	NS8390_init(dev, 0);

	/* We should someday disable shared memory and change to 8-bit mode
	   "just in case"... */

	return 0;
}


#ifdef MODULE
#define MAX_ULTRA_CARDS	4	/* Max number of Ultra cards per module */
static struct net_device *dev_ultra[MAX_ULTRA_CARDS];
static int io[MAX_ULTRA_CARDS];
static int irq[MAX_ULTRA_CARDS];

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(io, "I/O base address(es)");
MODULE_PARM_DESC(irq, "IRQ number(s) (assigned)");
MODULE_DESCRIPTION("SMC Ultra/EtherEZ ISA/PnP Ethernet driver");
MODULE_LICENSE("GPL");

/* This is set up so that only a single autoprobe takes place per call.
ISA device autoprobes on a running machine are not recommended. */
int
init_module(void)
{
	struct net_device *dev;
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_ULTRA_CARDS; this_dev++) {
		if (io[this_dev] == 0)  {
			if (this_dev != 0) break; /* only autoprobe 1st one */
			printk(KERN_NOTICE "smc-ultra.c: Presently autoprobing (not recommended) for a single card.\n");
		}
		dev = alloc_ei_netdev();
		if (!dev)
			break;
		dev->irq = irq[this_dev];
		dev->base_addr = io[this_dev];
		if (do_ultra_probe(dev) == 0) {
			dev_ultra[found++] = dev;
			continue;
		}
		free_netdev(dev);
		printk(KERN_WARNING "smc-ultra.c: No SMC Ultra card found (i/o = 0x%x).\n", io[this_dev]);
		break;
	}
	if (found)
		return 0;
	return -ENXIO;
}

static void cleanup_card(struct net_device *dev)
{
	/* NB: ultra_close_card() does free_irq */
#ifdef __ISAPNP__
	struct pnp_dev *idev = (struct pnp_dev *)ei_status.priv;
	if (idev)
		pnp_device_detach(idev);
#endif
	release_region(dev->base_addr - ULTRA_NIC_OFFSET, ULTRA_IO_EXTENT);
	iounmap(ei_status.mem);
}

void
cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_ULTRA_CARDS; this_dev++) {
		struct net_device *dev = dev_ultra[this_dev];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}
#endif /* MODULE */

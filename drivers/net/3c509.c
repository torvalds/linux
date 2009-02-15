/* 3c509.c: A 3c509 EtherLink3 ethernet driver for linux. */
/*
	Written 1993-2000 by Donald Becker.

	Copyright 1994-2000 by Donald Becker.
	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.	 This software may be used and
	distributed according to the terms of the GNU General Public License,
	incorporated herein by reference.

	This driver is for the 3Com EtherLinkIII series.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Known limitations:
	Because of the way 3c509 ISA detection works it's difficult to predict
	a priori which of several ISA-mode cards will be detected first.

	This driver does not use predictive interrupt mode, resulting in higher
	packet latency but lower overhead.  If interrupts are disabled for an
	unusually long time it could also result in missed packets, but in
	practice this rarely happens.


	FIXES:
		Alan Cox:       Removed the 'Unexpected interrupt' bug.
		Michael Meskes:	Upgraded to Donald Becker's version 1.07.
		Alan Cox:	Increased the eeprom delay. Regardless of
				what the docs say some people definitely
				get problems with lower (but in card spec)
				delays
		v1.10 4/21/97 Fixed module code so that multiple cards may be detected,
				other cleanups.  -djb
		Andrea Arcangeli:	Upgraded to Donald Becker's version 1.12.
		Rick Payne:	Fixed SMP race condition
		v1.13 9/8/97 Made 'max_interrupt_work' an insmod-settable variable -djb
		v1.14 10/15/97 Avoided waiting..discard message for fast machines -djb
		v1.15 1/31/98 Faster recovery for Tx errors. -djb
		v1.16 2/3/98 Different ID port handling to avoid sound cards. -djb
		v1.18 12Mar2001 Andrew Morton
			- Avoid bogus detect of 3c590's (Andrzej Krzysztofowicz)
			- Reviewed against 1.18 from scyld.com
		v1.18a 17Nov2001 Jeff Garzik <jgarzik@pobox.com>
			- ethtool support
		v1.18b 1Mar2002 Zwane Mwaikambo <zwane@commfireservices.com>
			- Power Management support
		v1.18c 1Mar2002 David Ruggiero <jdr@farfalle.com>
			- Full duplex support
		v1.19  16Oct2002 Zwane Mwaikambo <zwane@linuxpower.ca>
			- Additional ethtool features
		v1.19a 28Oct2002 Davud Ruggiero <jdr@farfalle.com>
			- Increase *read_eeprom udelay to workaround oops with 2 cards.
		v1.19b 08Nov2002 Marc Zyngier <maz@wild-wind.fr.eu.org>
			- Introduce driver model for EISA cards.
		v1.20  04Feb2008 Ondrej Zary <linux@rainbow-software.org>
			- convert to isa_driver and pnp_driver and some cleanups
*/

#define DRV_NAME	"3c509"
#define DRV_VERSION	"1.20"
#define DRV_RELDATE	"04Feb2008"

/* A few values that may be tweaked. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (400*HZ/1000)

#include <linux/module.h>
#include <linux/mca.h>
#include <linux/isa.h>
#include <linux/pnp.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/pm.h>
#include <linux/skbuff.h>
#include <linux/delay.h>	/* for udelay() */
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/device.h>
#include <linux/eisa.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

static char version[] __devinitdata = DRV_NAME ".c:" DRV_VERSION " " DRV_RELDATE " becker@scyld.com\n";

#ifdef EL3_DEBUG
static int el3_debug = EL3_DEBUG;
#else
static int el3_debug = 2;
#endif

/* Used to do a global count of all the cards in the system.  Must be
 * a global variable so that the mca/eisa probe routines can increment
 * it */
static int el3_cards = 0;
#define EL3_MAX_CARDS 8

/* To minimize the size of the driver source I only define operating
   constants if they are used several times.  You'll need the manual
   anyway if you want to understand driver details. */
/* Offsets from base I/O address. */
#define EL3_DATA 0x00
#define EL3_CMD 0x0e
#define EL3_STATUS 0x0e
#define	EEPROM_READ 0x80

#define EL3_IO_EXTENT	16

#define EL3WINDOW(win_num) outw(SelectWindow + (win_num), ioaddr + EL3_CMD)


/* The top five bits written to EL3_CMD are a command, the lower
   11 bits are the parameter, if applicable. */
enum c509cmd {
	TotalReset = 0<<11, SelectWindow = 1<<11, StartCoax = 2<<11,
	RxDisable = 3<<11, RxEnable = 4<<11, RxReset = 5<<11, RxDiscard = 8<<11,
	TxEnable = 9<<11, TxDisable = 10<<11, TxReset = 11<<11,
	FakeIntr = 12<<11, AckIntr = 13<<11, SetIntrEnb = 14<<11,
	SetStatusEnb = 15<<11, SetRxFilter = 16<<11, SetRxThreshold = 17<<11,
	SetTxThreshold = 18<<11, SetTxStart = 19<<11, StatsEnable = 21<<11,
	StatsDisable = 22<<11, StopCoax = 23<<11, PowerUp = 27<<11,
	PowerDown = 28<<11, PowerAuto = 29<<11};

enum c509status {
	IntLatch = 0x0001, AdapterFailure = 0x0002, TxComplete = 0x0004,
	TxAvailable = 0x0008, RxComplete = 0x0010, RxEarly = 0x0020,
	IntReq = 0x0040, StatsFull = 0x0080, CmdBusy = 0x1000, };

/* The SetRxFilter command accepts the following classes: */
enum RxFilter {
	RxStation = 1, RxMulticast = 2, RxBroadcast = 4, RxProm = 8 };

/* Register window 1 offsets, the window used in normal operation. */
#define TX_FIFO		0x00
#define RX_FIFO		0x00
#define RX_STATUS 	0x08
#define TX_STATUS 	0x0B
#define TX_FREE		0x0C		/* Remaining free bytes in Tx buffer. */

#define WN0_CONF_CTRL	0x04		/* Window 0: Configuration control register */
#define WN0_ADDR_CONF	0x06		/* Window 0: Address configuration register */
#define WN0_IRQ		0x08		/* Window 0: Set IRQ line in bits 12-15. */
#define WN4_MEDIA	0x0A		/* Window 4: Various transcvr/media bits. */
#define	MEDIA_TP	0x00C0		/* Enable link beat and jabber for 10baseT. */
#define WN4_NETDIAG	0x06		/* Window 4: Net diagnostic */
#define FD_ENABLE	0x8000		/* Enable full-duplex ("external loopback") */

/*
 * Must be a power of two (we use a binary and in the
 * circular queue)
 */
#define SKB_QUEUE_SIZE	64

enum el3_cardtype { EL3_ISA, EL3_PNP, EL3_MCA, EL3_EISA };

struct el3_private {
	spinlock_t lock;
	/* skb send-queue */
	int head, size;
	struct sk_buff *queue[SKB_QUEUE_SIZE];
	enum el3_cardtype type;
};
static int id_port;
static int current_tag;
static struct net_device *el3_devs[EL3_MAX_CARDS];

/* Parameters that may be passed into the module. */
static int debug = -1;
static int irq[] = {-1, -1, -1, -1, -1, -1, -1, -1};
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 10;
#ifdef CONFIG_PNP
static int nopnp;
#endif

static int __devinit el3_common_init(struct net_device *dev);
static void el3_common_remove(struct net_device *dev);
static ushort id_read_eeprom(int index);
static ushort read_eeprom(int ioaddr, int index);
static int el3_open(struct net_device *dev);
static int el3_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t el3_interrupt(int irq, void *dev_id);
static void update_stats(struct net_device *dev);
static struct net_device_stats *el3_get_stats(struct net_device *dev);
static int el3_rx(struct net_device *dev);
static int el3_close(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static void el3_tx_timeout (struct net_device *dev);
static void el3_down(struct net_device *dev);
static void el3_up(struct net_device *dev);
static const struct ethtool_ops ethtool_ops;
#ifdef CONFIG_PM
static int el3_suspend(struct device *, pm_message_t);
static int el3_resume(struct device *);
#else
#define el3_suspend NULL
#define el3_resume NULL
#endif


/* generic device remove for all device types */
static int el3_device_remove (struct device *device);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void el3_poll_controller(struct net_device *dev);
#endif

/* Return 0 on success, 1 on error, 2 when found already detected PnP card */
static int el3_isa_id_sequence(__be16 *phys_addr)
{
	short lrs_state = 0xff;
	int i;

	/* ISA boards are detected by sending the ID sequence to the
	   ID_PORT.  We find cards past the first by setting the 'current_tag'
	   on cards as they are found.  Cards with their tag set will not
	   respond to subsequent ID sequences. */

	outb(0x00, id_port);
	outb(0x00, id_port);
	for (i = 0; i < 255; i++) {
		outb(lrs_state, id_port);
		lrs_state <<= 1;
		lrs_state = lrs_state & 0x100 ? lrs_state ^ 0xcf : lrs_state;
	}
	/* For the first probe, clear all board's tag registers. */
	if (current_tag == 0)
		outb(0xd0, id_port);
	else			/* Otherwise kill off already-found boards. */
		outb(0xd8, id_port);
	if (id_read_eeprom(7) != 0x6d50)
		return 1;
	/* Read in EEPROM data, which does contention-select.
	   Only the lowest address board will stay "on-line".
	   3Com got the byte order backwards. */
	for (i = 0; i < 3; i++)
		phys_addr[i] = htons(id_read_eeprom(i));
#ifdef CONFIG_PNP
	if (!nopnp) {
		/* The ISA PnP 3c509 cards respond to the ID sequence too.
		   This check is needed in order not to register them twice. */
		for (i = 0; i < el3_cards; i++) {
			struct el3_private *lp = netdev_priv(el3_devs[i]);
			if (lp->type == EL3_PNP
			    && !memcmp(phys_addr, el3_devs[i]->dev_addr,
				       ETH_ALEN)) {
				if (el3_debug > 3)
					printk(KERN_DEBUG "3c509 with address %02x %02x %02x %02x %02x %02x was found by ISAPnP\n",
						phys_addr[0] & 0xff, phys_addr[0] >> 8,
						phys_addr[1] & 0xff, phys_addr[1] >> 8,
						phys_addr[2] & 0xff, phys_addr[2] >> 8);
				/* Set the adaptor tag so that the next card can be found. */
				outb(0xd0 + ++current_tag, id_port);
				return 2;
			}
		}
	}
#endif /* CONFIG_PNP */
	return 0;

}

static void __devinit el3_dev_fill(struct net_device *dev, __be16 *phys_addr,
				   int ioaddr, int irq, int if_port,
				   enum el3_cardtype type)
{
	struct el3_private *lp = netdev_priv(dev);

	memcpy(dev->dev_addr, phys_addr, ETH_ALEN);
	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->if_port = if_port;
	lp->type = type;
}

static int __devinit el3_isa_match(struct device *pdev,
				   unsigned int ndev)
{
	struct net_device *dev;
	int ioaddr, isa_irq, if_port, err;
	unsigned int iobase;
	__be16 phys_addr[3];

	while ((err = el3_isa_id_sequence(phys_addr)) == 2)
		;	/* Skip to next card when PnP card found */
	if (err == 1)
		return 0;

	iobase = id_read_eeprom(8);
	if_port = iobase >> 14;
	ioaddr = 0x200 + ((iobase & 0x1f) << 4);
	if (irq[el3_cards] > 1 && irq[el3_cards] < 16)
		isa_irq = irq[el3_cards];
	else
		isa_irq = id_read_eeprom(9) >> 12;

	dev = alloc_etherdev(sizeof(struct el3_private));
	if (!dev)
		return -ENOMEM;

	netdev_boot_setup_check(dev);

	if (!request_region(ioaddr, EL3_IO_EXTENT, "3c509-isa")) {
		free_netdev(dev);
		return 0;
	}

	/* Set the adaptor tag so that the next card can be found. */
	outb(0xd0 + ++current_tag, id_port);

	/* Activate the adaptor at the EEPROM location. */
	outb((ioaddr >> 4) | 0xe0, id_port);

	EL3WINDOW(0);
	if (inw(ioaddr) != 0x6d50) {
		free_netdev(dev);
		return 0;
	}

	/* Free the interrupt so that some other card can use it. */
	outw(0x0f00, ioaddr + WN0_IRQ);

	el3_dev_fill(dev, phys_addr, ioaddr, isa_irq, if_port, EL3_ISA);
	dev_set_drvdata(pdev, dev);
	if (el3_common_init(dev)) {
		free_netdev(dev);
		return 0;
	}

	el3_devs[el3_cards++] = dev;
	return 1;
}

static int __devexit el3_isa_remove(struct device *pdev,
				    unsigned int ndev)
{
	el3_device_remove(pdev);
	dev_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int el3_isa_suspend(struct device *dev, unsigned int n,
			   pm_message_t state)
{
	current_tag = 0;
	return el3_suspend(dev, state);
}

static int el3_isa_resume(struct device *dev, unsigned int n)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int ioaddr = ndev->base_addr, err;
	__be16 phys_addr[3];

	while ((err = el3_isa_id_sequence(phys_addr)) == 2)
		;	/* Skip to next card when PnP card found */
	if (err == 1)
		return 0;
	/* Set the adaptor tag so that the next card can be found. */
	outb(0xd0 + ++current_tag, id_port);
	/* Enable the card */
	outb((ioaddr >> 4) | 0xe0, id_port);
	EL3WINDOW(0);
	if (inw(ioaddr) != 0x6d50)
		return 1;
	/* Free the interrupt so that some other card can use it. */
	outw(0x0f00, ioaddr + WN0_IRQ);
	return el3_resume(dev);
}
#endif

static struct isa_driver el3_isa_driver = {
	.match		= el3_isa_match,
	.remove		= __devexit_p(el3_isa_remove),
#ifdef CONFIG_PM
	.suspend	= el3_isa_suspend,
	.resume		= el3_isa_resume,
#endif
	.driver		= {
		.name	= "3c509"
	},
};
static int isa_registered;

#ifdef CONFIG_PNP
static struct pnp_device_id el3_pnp_ids[] = {
	{ .id = "TCM5090" }, /* 3Com Etherlink III (TP) */
	{ .id = "TCM5091" }, /* 3Com Etherlink III */
	{ .id = "TCM5094" }, /* 3Com Etherlink III (combo) */
	{ .id = "TCM5095" }, /* 3Com Etherlink III (TPO) */
	{ .id = "TCM5098" }, /* 3Com Etherlink III (TPC) */
	{ .id = "PNP80f7" }, /* 3Com Etherlink III compatible */
	{ .id = "PNP80f8" }, /* 3Com Etherlink III compatible */
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, el3_pnp_ids);

static int __devinit el3_pnp_probe(struct pnp_dev *pdev,
				    const struct pnp_device_id *id)
{
	short i;
	int ioaddr, irq, if_port;
	__be16 phys_addr[3];
	struct net_device *dev = NULL;
	int err;

	ioaddr = pnp_port_start(pdev, 0);
	if (!request_region(ioaddr, EL3_IO_EXTENT, "3c509-pnp"))
		return -EBUSY;
	irq = pnp_irq(pdev, 0);
	EL3WINDOW(0);
	for (i = 0; i < 3; i++)
		phys_addr[i] = htons(read_eeprom(ioaddr, i));
	if_port = read_eeprom(ioaddr, 8) >> 14;
	dev = alloc_etherdev(sizeof(struct el3_private));
	if (!dev) {
		release_region(ioaddr, EL3_IO_EXTENT);
		return -ENOMEM;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	netdev_boot_setup_check(dev);

	el3_dev_fill(dev, phys_addr, ioaddr, irq, if_port, EL3_PNP);
	pnp_set_drvdata(pdev, dev);
	err = el3_common_init(dev);

	if (err) {
		pnp_set_drvdata(pdev, NULL);
		free_netdev(dev);
		return err;
	}

	el3_devs[el3_cards++] = dev;
	return 0;
}

static void __devexit el3_pnp_remove(struct pnp_dev *pdev)
{
	el3_common_remove(pnp_get_drvdata(pdev));
	pnp_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
static int el3_pnp_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	return el3_suspend(&pdev->dev, state);
}

static int el3_pnp_resume(struct pnp_dev *pdev)
{
	return el3_resume(&pdev->dev);
}
#endif

static struct pnp_driver el3_pnp_driver = {
	.name		= "3c509",
	.id_table	= el3_pnp_ids,
	.probe		= el3_pnp_probe,
	.remove		= __devexit_p(el3_pnp_remove),
#ifdef CONFIG_PM
	.suspend	= el3_pnp_suspend,
	.resume		= el3_pnp_resume,
#endif
};
static int pnp_registered;
#endif /* CONFIG_PNP */

#ifdef CONFIG_EISA
static struct eisa_device_id el3_eisa_ids[] = {
		{ "TCM5092" },
		{ "TCM5093" },
		{ "TCM5095" },
		{ "" }
};
MODULE_DEVICE_TABLE(eisa, el3_eisa_ids);

static int el3_eisa_probe (struct device *device);

static struct eisa_driver el3_eisa_driver = {
		.id_table = el3_eisa_ids,
		.driver   = {
				.name    = "3c579",
				.probe   = el3_eisa_probe,
				.remove  = __devexit_p (el3_device_remove),
				.suspend = el3_suspend,
				.resume  = el3_resume,
		}
};
static int eisa_registered;
#endif

#ifdef CONFIG_MCA
static int el3_mca_probe(struct device *dev);

static short el3_mca_adapter_ids[] __initdata = {
		0x627c,
		0x627d,
		0x62db,
		0x62f6,
		0x62f7,
		0x0000
};

static char *el3_mca_adapter_names[] __initdata = {
		"3Com 3c529 EtherLink III (10base2)",
		"3Com 3c529 EtherLink III (10baseT)",
		"3Com 3c529 EtherLink III (test mode)",
		"3Com 3c529 EtherLink III (TP or coax)",
		"3Com 3c529 EtherLink III (TP)",
		NULL
};

static struct mca_driver el3_mca_driver = {
		.id_table = el3_mca_adapter_ids,
		.driver = {
				.name = "3c529",
				.bus = &mca_bus_type,
				.probe = el3_mca_probe,
				.remove = __devexit_p(el3_device_remove),
				.suspend = el3_suspend,
				.resume  = el3_resume,
		},
};
static int mca_registered;
#endif /* CONFIG_MCA */

static int __devinit el3_common_init(struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	int err;
	const char *if_names[] = {"10baseT", "AUI", "undefined", "BNC"};

	spin_lock_init(&lp->lock);

	if (dev->mem_start & 0x05) { /* xcvr codes 1/3/4/12 */
		dev->if_port = (dev->mem_start & 0x0f);
	} else { /* xcvr codes 0/8 */
		/* use eeprom value, but save user's full-duplex selection */
		dev->if_port |= (dev->mem_start & 0x08);
	}

	/* The EL3-specific entries in the device structure. */
	dev->open = &el3_open;
	dev->hard_start_xmit = &el3_start_xmit;
	dev->stop = &el3_close;
	dev->get_stats = &el3_get_stats;
	dev->set_multicast_list = &set_multicast_list;
	dev->tx_timeout = el3_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = el3_poll_controller;
#endif
	SET_ETHTOOL_OPS(dev, &ethtool_ops);

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR "Failed to register 3c5x9 at %#3.3lx, IRQ %d.\n",
			dev->base_addr, dev->irq);
		release_region(dev->base_addr, EL3_IO_EXTENT);
		return err;
	}

	printk(KERN_INFO "%s: 3c5x9 found at %#3.3lx, %s port, "
	       "address %pM, IRQ %d.\n",
	       dev->name, dev->base_addr, if_names[(dev->if_port & 0x03)],
	       dev->dev_addr, dev->irq);

	if (el3_debug > 0)
		printk(KERN_INFO "%s", version);
	return 0;

}

static void el3_common_remove (struct net_device *dev)
{
	unregister_netdev (dev);
	release_region(dev->base_addr, EL3_IO_EXTENT);
	free_netdev (dev);
}

#ifdef CONFIG_MCA
static int __init el3_mca_probe(struct device *device)
{
	/* Based on Erik Nygren's (nygren@mit.edu) 3c529 patch,
	 * heavily modified by Chris Beauregard
	 * (cpbeaure@csclub.uwaterloo.ca) to support standard MCA
	 * probing.
	 *
	 * redone for multi-card detection by ZP Gu (zpg@castle.net)
	 * now works as a module */

	short i;
	int ioaddr, irq, if_port;
	__be16 phys_addr[3];
	struct net_device *dev = NULL;
	u_char pos4, pos5;
	struct mca_device *mdev = to_mca_device(device);
	int slot = mdev->slot;
	int err;

	pos4 = mca_device_read_stored_pos(mdev, 4);
	pos5 = mca_device_read_stored_pos(mdev, 5);

	ioaddr = ((short)((pos4&0xfc)|0x02)) << 8;
	irq = pos5 & 0x0f;


	printk(KERN_INFO "3c529: found %s at slot %d\n",
		   el3_mca_adapter_names[mdev->index], slot + 1);

	/* claim the slot */
	strncpy(mdev->name, el3_mca_adapter_names[mdev->index],
			sizeof(mdev->name));
	mca_device_set_claim(mdev, 1);

	if_port = pos4 & 0x03;

	irq = mca_device_transform_irq(mdev, irq);
	ioaddr = mca_device_transform_ioport(mdev, ioaddr);
	if (el3_debug > 2) {
			printk(KERN_DEBUG "3c529: irq %d  ioaddr 0x%x  ifport %d\n", irq, ioaddr, if_port);
	}
	EL3WINDOW(0);
	for (i = 0; i < 3; i++)
		phys_addr[i] = htons(read_eeprom(ioaddr, i));

	dev = alloc_etherdev(sizeof (struct el3_private));
	if (dev == NULL) {
		release_region(ioaddr, EL3_IO_EXTENT);
		return -ENOMEM;
	}

	netdev_boot_setup_check(dev);

	el3_dev_fill(dev, phys_addr, ioaddr, irq, if_port, EL3_MCA);
	device->driver_data = dev;
	err = el3_common_init(dev);

	if (err) {
		device->driver_data = NULL;
		free_netdev(dev);
		return -ENOMEM;
	}

	el3_devs[el3_cards++] = dev;
	return 0;
}

#endif /* CONFIG_MCA */

#ifdef CONFIG_EISA
static int __init el3_eisa_probe (struct device *device)
{
	short i;
	int ioaddr, irq, if_port;
	__be16 phys_addr[3];
	struct net_device *dev = NULL;
	struct eisa_device *edev;
	int err;

	/* Yeepee, The driver framework is calling us ! */
	edev = to_eisa_device (device);
	ioaddr = edev->base_addr;

	if (!request_region(ioaddr, EL3_IO_EXTENT, "3c579-eisa"))
		return -EBUSY;

	/* Change the register set to the configuration window 0. */
	outw(SelectWindow | 0, ioaddr + 0xC80 + EL3_CMD);

	irq = inw(ioaddr + WN0_IRQ) >> 12;
	if_port = inw(ioaddr + 6)>>14;
	for (i = 0; i < 3; i++)
		phys_addr[i] = htons(read_eeprom(ioaddr, i));

	/* Restore the "Product ID" to the EEPROM read register. */
	read_eeprom(ioaddr, 3);

	dev = alloc_etherdev(sizeof (struct el3_private));
	if (dev == NULL) {
		release_region(ioaddr, EL3_IO_EXTENT);
		return -ENOMEM;
	}

	netdev_boot_setup_check(dev);

	el3_dev_fill(dev, phys_addr, ioaddr, irq, if_port, EL3_EISA);
	eisa_set_drvdata (edev, dev);
	err = el3_common_init(dev);

	if (err) {
		eisa_set_drvdata (edev, NULL);
		free_netdev(dev);
		return err;
	}

	el3_devs[el3_cards++] = dev;
	return 0;
}
#endif

/* This remove works for all device types.
 *
 * The net dev must be stored in the driver_data field */
static int __devexit el3_device_remove (struct device *device)
{
	struct net_device *dev;

	dev  = device->driver_data;

	el3_common_remove (dev);
	return 0;
}

/* Read a word from the EEPROM using the regular EEPROM access register.
   Assume that we are in register window zero.
 */
static ushort read_eeprom(int ioaddr, int index)
{
	outw(EEPROM_READ + index, ioaddr + 10);
	/* Pause for at least 162 us. for the read to take place.
	   Some chips seem to require much longer */
	mdelay(2);
	return inw(ioaddr + 12);
}

/* Read a word from the EEPROM when in the ISA ID probe state. */
static ushort id_read_eeprom(int index)
{
	int bit, word = 0;

	/* Issue read command, and pause for at least 162 us. for it to complete.
	   Assume extra-fast 16Mhz bus. */
	outb(EEPROM_READ + index, id_port);

	/* Pause for at least 162 us. for the read to take place. */
	/* Some chips seem to require much longer */
	mdelay(4);

	for (bit = 15; bit >= 0; bit--)
		word = (word << 1) + (inb(id_port) & 0x01);

	if (el3_debug > 3)
		printk(KERN_DEBUG "  3c509 EEPROM word %d %#4.4x.\n", index, word);

	return word;
}


static int
el3_open(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	int i;

	outw(TxReset, ioaddr + EL3_CMD);
	outw(RxReset, ioaddr + EL3_CMD);
	outw(SetStatusEnb | 0x00, ioaddr + EL3_CMD);

	i = request_irq(dev->irq, &el3_interrupt, 0, dev->name, dev);
	if (i)
		return i;

	EL3WINDOW(0);
	if (el3_debug > 3)
		printk(KERN_DEBUG "%s: Opening, IRQ %d	 status@%x %4.4x.\n", dev->name,
			   dev->irq, ioaddr + EL3_STATUS, inw(ioaddr + EL3_STATUS));

	el3_up(dev);

	if (el3_debug > 3)
		printk(KERN_DEBUG "%s: Opened 3c509  IRQ %d  status %4.4x.\n",
			   dev->name, dev->irq, inw(ioaddr + EL3_STATUS));

	return 0;
}

static void
el3_tx_timeout (struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	/* Transmitter timeout, serious problems. */
	printk(KERN_WARNING "%s: transmit timed out, Tx_status %2.2x status %4.4x "
		   "Tx FIFO room %d.\n",
		   dev->name, inb(ioaddr + TX_STATUS), inw(ioaddr + EL3_STATUS),
		   inw(ioaddr + TX_FREE));
	dev->stats.tx_errors++;
	dev->trans_start = jiffies;
	/* Issue TX_RESET and TX_START commands. */
	outw(TxReset, ioaddr + EL3_CMD);
	outw(TxEnable, ioaddr + EL3_CMD);
	netif_wake_queue(dev);
}


static int
el3_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	unsigned long flags;

	netif_stop_queue (dev);

	dev->stats.tx_bytes += skb->len;

	if (el3_debug > 4) {
		printk(KERN_DEBUG "%s: el3_start_xmit(length = %u) called, status %4.4x.\n",
			   dev->name, skb->len, inw(ioaddr + EL3_STATUS));
	}
#if 0
#ifndef final_version
	{	/* Error-checking code, delete someday. */
		ushort status = inw(ioaddr + EL3_STATUS);
		if (status & 0x0001 		/* IRQ line active, missed one. */
			&& inw(ioaddr + EL3_STATUS) & 1) { 			/* Make sure. */
			printk(KERN_DEBUG "%s: Missed interrupt, status then %04x now %04x"
				   "  Tx %2.2x Rx %4.4x.\n", dev->name, status,
				   inw(ioaddr + EL3_STATUS), inb(ioaddr + TX_STATUS),
				   inw(ioaddr + RX_STATUS));
			/* Fake interrupt trigger by masking, acknowledge interrupts. */
			outw(SetStatusEnb | 0x00, ioaddr + EL3_CMD);
			outw(AckIntr | IntLatch | TxAvailable | RxEarly | IntReq,
				 ioaddr + EL3_CMD);
			outw(SetStatusEnb | 0xff, ioaddr + EL3_CMD);
		}
	}
#endif
#endif
	/*
	 *	We lock the driver against other processors. Note
	 *	we don't need to lock versus the IRQ as we suspended
	 *	that. This means that we lose the ability to take
	 *	an RX during a TX upload. That sucks a bit with SMP
	 *	on an original 3c509 (2K buffer)
	 *
	 *	Using disable_irq stops us crapping on other
	 *	time sensitive devices.
	 */

	spin_lock_irqsave(&lp->lock, flags);

	/* Put out the doubleword header... */
	outw(skb->len, ioaddr + TX_FIFO);
	outw(0x00, ioaddr + TX_FIFO);
	/* ... and the packet rounded to a doubleword. */
	outsl(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);

	dev->trans_start = jiffies;
	if (inw(ioaddr + TX_FREE) > 1536)
		netif_start_queue(dev);
	else
		/* Interrupt us when the FIFO has room for max-sized packet. */
		outw(SetTxThreshold + 1536, ioaddr + EL3_CMD);

	spin_unlock_irqrestore(&lp->lock, flags);

	dev_kfree_skb (skb);

	/* Clear the Tx status stack. */
	{
		short tx_status;
		int i = 4;

		while (--i > 0	&&	(tx_status = inb(ioaddr + TX_STATUS)) > 0) {
			if (tx_status & 0x38) dev->stats.tx_aborted_errors++;
			if (tx_status & 0x30) outw(TxReset, ioaddr + EL3_CMD);
			if (tx_status & 0x3C) outw(TxEnable, ioaddr + EL3_CMD);
			outb(0x00, ioaddr + TX_STATUS); /* Pop the status stack. */
		}
	}
	return 0;
}

/* The EL3 interrupt handler. */
static irqreturn_t
el3_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct el3_private *lp;
	int ioaddr, status;
	int i = max_interrupt_work;

	lp = netdev_priv(dev);
	spin_lock(&lp->lock);

	ioaddr = dev->base_addr;

	if (el3_debug > 4) {
		status = inw(ioaddr + EL3_STATUS);
		printk(KERN_DEBUG "%s: interrupt, status %4.4x.\n", dev->name, status);
	}

	while ((status = inw(ioaddr + EL3_STATUS)) &
		   (IntLatch | RxComplete | StatsFull)) {

		if (status & RxComplete)
			el3_rx(dev);

		if (status & TxAvailable) {
			if (el3_debug > 5)
				printk(KERN_DEBUG "	TX room bit was handled.\n");
			/* There's room in the FIFO for a full-sized packet. */
			outw(AckIntr | TxAvailable, ioaddr + EL3_CMD);
			netif_wake_queue (dev);
		}
		if (status & (AdapterFailure | RxEarly | StatsFull | TxComplete)) {
			/* Handle all uncommon interrupts. */
			if (status & StatsFull)				/* Empty statistics. */
				update_stats(dev);
			if (status & RxEarly) {				/* Rx early is unused. */
				el3_rx(dev);
				outw(AckIntr | RxEarly, ioaddr + EL3_CMD);
			}
			if (status & TxComplete) {			/* Really Tx error. */
				short tx_status;
				int i = 4;

				while (--i>0 && (tx_status = inb(ioaddr + TX_STATUS)) > 0) {
					if (tx_status & 0x38) dev->stats.tx_aborted_errors++;
					if (tx_status & 0x30) outw(TxReset, ioaddr + EL3_CMD);
					if (tx_status & 0x3C) outw(TxEnable, ioaddr + EL3_CMD);
					outb(0x00, ioaddr + TX_STATUS); /* Pop the status stack. */
				}
			}
			if (status & AdapterFailure) {
				/* Adapter failure requires Rx reset and reinit. */
				outw(RxReset, ioaddr + EL3_CMD);
				/* Set the Rx filter to the current state. */
				outw(SetRxFilter | RxStation | RxBroadcast
					 | (dev->flags & IFF_ALLMULTI ? RxMulticast : 0)
					 | (dev->flags & IFF_PROMISC ? RxProm : 0),
					 ioaddr + EL3_CMD);
				outw(RxEnable, ioaddr + EL3_CMD); /* Re-enable the receiver. */
				outw(AckIntr | AdapterFailure, ioaddr + EL3_CMD);
			}
		}

		if (--i < 0) {
			printk(KERN_ERR "%s: Infinite loop in interrupt, status %4.4x.\n",
				   dev->name, status);
			/* Clear all interrupts. */
			outw(AckIntr | 0xFF, ioaddr + EL3_CMD);
			break;
		}
		/* Acknowledge the IRQ. */
		outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD); /* Ack IRQ */
	}

	if (el3_debug > 4) {
		printk(KERN_DEBUG "%s: exiting interrupt, status %4.4x.\n", dev->name,
			   inw(ioaddr + EL3_STATUS));
	}
	spin_unlock(&lp->lock);
	return IRQ_HANDLED;
}


#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */
static void el3_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	el3_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static struct net_device_stats *
el3_get_stats(struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	unsigned long flags;

	/*
	 *	This is fast enough not to bother with disable IRQ
	 *	stuff.
	 */

	spin_lock_irqsave(&lp->lock, flags);
	update_stats(dev);
	spin_unlock_irqrestore(&lp->lock, flags);
	return &dev->stats;
}

/*  Update statistics.  We change to register window 6, so this should be run
	single-threaded if the device is active. This is expected to be a rare
	operation, and it's simpler for the rest of the driver to assume that
	window 1 is always valid rather than use a special window-state variable.
	*/
static void update_stats(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	if (el3_debug > 5)
		printk("   Updating the statistics.\n");
	/* Turn off statistics updates while reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	/* Switch to the stats window, and read everything. */
	EL3WINDOW(6);
	dev->stats.tx_carrier_errors 	+= inb(ioaddr + 0);
	dev->stats.tx_heartbeat_errors	+= inb(ioaddr + 1);
	/* Multiple collisions. */	   inb(ioaddr + 2);
	dev->stats.collisions		+= inb(ioaddr + 3);
	dev->stats.tx_window_errors	+= inb(ioaddr + 4);
	dev->stats.rx_fifo_errors	+= inb(ioaddr + 5);
	dev->stats.tx_packets		+= inb(ioaddr + 6);
	/* Rx packets	*/		   inb(ioaddr + 7);
	/* Tx deferrals */		   inb(ioaddr + 8);
	inw(ioaddr + 10);	/* Total Rx and Tx octets. */
	inw(ioaddr + 12);

	/* Back to window 1, and turn statistics back on. */
	EL3WINDOW(1);
	outw(StatsEnable, ioaddr + EL3_CMD);
	return;
}

static int
el3_rx(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	short rx_status;

	if (el3_debug > 5)
		printk("   In rx_packet(), status %4.4x, rx_status %4.4x.\n",
			   inw(ioaddr+EL3_STATUS), inw(ioaddr+RX_STATUS));
	while ((rx_status = inw(ioaddr + RX_STATUS)) > 0) {
		if (rx_status & 0x4000) { /* Error, update stats. */
			short error = rx_status & 0x3800;

			outw(RxDiscard, ioaddr + EL3_CMD);
			dev->stats.rx_errors++;
			switch (error) {
			case 0x0000:		dev->stats.rx_over_errors++; break;
			case 0x0800:		dev->stats.rx_length_errors++; break;
			case 0x1000:		dev->stats.rx_frame_errors++; break;
			case 0x1800:		dev->stats.rx_length_errors++; break;
			case 0x2000:		dev->stats.rx_frame_errors++; break;
			case 0x2800:		dev->stats.rx_crc_errors++; break;
			}
		} else {
			short pkt_len = rx_status & 0x7ff;
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len+5);
			if (el3_debug > 4)
				printk("Receiving packet size %d status %4.4x.\n",
					   pkt_len, rx_status);
			if (skb != NULL) {
				skb_reserve(skb, 2);     /* Align IP on 16 byte */

				/* 'skb->data' points to the start of sk_buff data area. */
				insl(ioaddr + RX_FIFO, skb_put(skb,pkt_len),
					 (pkt_len + 3) >> 2);

				outw(RxDiscard, ioaddr + EL3_CMD); /* Pop top Rx packet. */
				skb->protocol = eth_type_trans(skb,dev);
				netif_rx(skb);
				dev->stats.rx_bytes += pkt_len;
				dev->stats.rx_packets++;
				continue;
			}
			outw(RxDiscard, ioaddr + EL3_CMD);
			dev->stats.rx_dropped++;
			if (el3_debug)
				printk("%s: Couldn't allocate a sk_buff of size %d.\n",
					   dev->name, pkt_len);
		}
		inw(ioaddr + EL3_STATUS); 				/* Delay. */
		while (inw(ioaddr + EL3_STATUS) & 0x1000)
			printk(KERN_DEBUG "	Waiting for 3c509 to discard packet, status %x.\n",
				   inw(ioaddr + EL3_STATUS) );
	}

	return 0;
}

/*
 *     Set or clear the multicast filter for this adaptor.
 */
static void
set_multicast_list(struct net_device *dev)
{
	unsigned long flags;
	struct el3_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	if (el3_debug > 1) {
		static int old;
		if (old != dev->mc_count) {
			old = dev->mc_count;
			printk("%s: Setting Rx mode to %d addresses.\n", dev->name, dev->mc_count);
		}
	}
	spin_lock_irqsave(&lp->lock, flags);
	if (dev->flags&IFF_PROMISC) {
		outw(SetRxFilter | RxStation | RxMulticast | RxBroadcast | RxProm,
			 ioaddr + EL3_CMD);
	}
	else if (dev->mc_count || (dev->flags&IFF_ALLMULTI)) {
		outw(SetRxFilter | RxStation | RxMulticast | RxBroadcast, ioaddr + EL3_CMD);
	}
	else
		outw(SetRxFilter | RxStation | RxBroadcast, ioaddr + EL3_CMD);
	spin_unlock_irqrestore(&lp->lock, flags);
}

static int
el3_close(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct el3_private *lp = netdev_priv(dev);

	if (el3_debug > 2)
		printk("%s: Shutting down ethercard.\n", dev->name);

	el3_down(dev);

	free_irq(dev->irq, dev);
	/* Switching back to window 0 disables the IRQ. */
	EL3WINDOW(0);
	if (lp->type != EL3_EISA) {
		/* But we explicitly zero the IRQ line select anyway. Don't do
		 * it on EISA cards, it prevents the module from getting an
		 * IRQ after unload+reload... */
		outw(0x0f00, ioaddr + WN0_IRQ);
	}

	return 0;
}

static int
el3_link_ok(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	u16 tmp;

	EL3WINDOW(4);
	tmp = inw(ioaddr + WN4_MEDIA);
	EL3WINDOW(1);
	return tmp & (1<<11);
}

static int
el3_netdev_get_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	u16 tmp;
	int ioaddr = dev->base_addr;

	EL3WINDOW(0);
	/* obtain current transceiver via WN4_MEDIA? */
	tmp = inw(ioaddr + WN0_ADDR_CONF);
	ecmd->transceiver = XCVR_INTERNAL;
	switch (tmp >> 14) {
	case 0:
		ecmd->port = PORT_TP;
		break;
	case 1:
		ecmd->port = PORT_AUI;
		ecmd->transceiver = XCVR_EXTERNAL;
		break;
	case 3:
		ecmd->port = PORT_BNC;
	default:
		break;
	}

	ecmd->duplex = DUPLEX_HALF;
	ecmd->supported = 0;
	tmp = inw(ioaddr + WN0_CONF_CTRL);
	if (tmp & (1<<13))
		ecmd->supported |= SUPPORTED_AUI;
	if (tmp & (1<<12))
		ecmd->supported |= SUPPORTED_BNC;
	if (tmp & (1<<9)) {
		ecmd->supported |= SUPPORTED_TP | SUPPORTED_10baseT_Half |
				SUPPORTED_10baseT_Full;	/* hmm... */
		EL3WINDOW(4);
		tmp = inw(ioaddr + WN4_NETDIAG);
		if (tmp & FD_ENABLE)
			ecmd->duplex = DUPLEX_FULL;
	}

	ecmd->speed = SPEED_10;
	EL3WINDOW(1);
	return 0;
}

static int
el3_netdev_set_ecmd(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	u16 tmp;
	int ioaddr = dev->base_addr;

	if (ecmd->speed != SPEED_10)
		return -EINVAL;
	if ((ecmd->duplex != DUPLEX_HALF) && (ecmd->duplex != DUPLEX_FULL))
		return -EINVAL;
	if ((ecmd->transceiver != XCVR_INTERNAL) && (ecmd->transceiver != XCVR_EXTERNAL))
		return -EINVAL;

	/* change XCVR type */
	EL3WINDOW(0);
	tmp = inw(ioaddr + WN0_ADDR_CONF);
	switch (ecmd->port) {
	case PORT_TP:
		tmp &= ~(3<<14);
		dev->if_port = 0;
		break;
	case PORT_AUI:
		tmp |= (1<<14);
		dev->if_port = 1;
		break;
	case PORT_BNC:
		tmp |= (3<<14);
		dev->if_port = 3;
		break;
	default:
		return -EINVAL;
	}

	outw(tmp, ioaddr + WN0_ADDR_CONF);
	if (dev->if_port == 3) {
		/* fire up the DC-DC convertor if BNC gets enabled */
		tmp = inw(ioaddr + WN0_ADDR_CONF);
		if (tmp & (3 << 14)) {
			outw(StartCoax, ioaddr + EL3_CMD);
			udelay(800);
		} else
			return -EIO;
	}

	EL3WINDOW(4);
	tmp = inw(ioaddr + WN4_NETDIAG);
	if (ecmd->duplex == DUPLEX_FULL)
		tmp |= FD_ENABLE;
	else
		tmp &= ~FD_ENABLE;
	outw(tmp, ioaddr + WN4_NETDIAG);
	EL3WINDOW(1);

	return 0;
}

static void el3_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
}

static int el3_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct el3_private *lp = netdev_priv(dev);
	int ret;

	spin_lock_irq(&lp->lock);
	ret = el3_netdev_get_ecmd(dev, ecmd);
	spin_unlock_irq(&lp->lock);
	return ret;
}

static int el3_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct el3_private *lp = netdev_priv(dev);
	int ret;

	spin_lock_irq(&lp->lock);
	ret = el3_netdev_set_ecmd(dev, ecmd);
	spin_unlock_irq(&lp->lock);
	return ret;
}

static u32 el3_get_link(struct net_device *dev)
{
	struct el3_private *lp = netdev_priv(dev);
	u32 ret;

	spin_lock_irq(&lp->lock);
	ret = el3_link_ok(dev);
	spin_unlock_irq(&lp->lock);
	return ret;
}

static u32 el3_get_msglevel(struct net_device *dev)
{
	return el3_debug;
}

static void el3_set_msglevel(struct net_device *dev, u32 v)
{
	el3_debug = v;
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo = el3_get_drvinfo,
	.get_settings = el3_get_settings,
	.set_settings = el3_set_settings,
	.get_link = el3_get_link,
	.get_msglevel = el3_get_msglevel,
	.set_msglevel = el3_set_msglevel,
};

static void
el3_down(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	netif_stop_queue(dev);

	/* Turn off statistics ASAP.  We update lp->stats below. */
	outw(StatsDisable, ioaddr + EL3_CMD);

	/* Disable the receiver and transmitter. */
	outw(RxDisable, ioaddr + EL3_CMD);
	outw(TxDisable, ioaddr + EL3_CMD);

	if (dev->if_port == 3)
		/* Turn off thinnet power.  Green! */
		outw(StopCoax, ioaddr + EL3_CMD);
	else if (dev->if_port == 0) {
		/* Disable link beat and jabber, if_port may change here next open(). */
		EL3WINDOW(4);
		outw(inw(ioaddr + WN4_MEDIA) & ~MEDIA_TP, ioaddr + WN4_MEDIA);
	}

	outw(SetIntrEnb | 0x0000, ioaddr + EL3_CMD);

	update_stats(dev);
}

static void
el3_up(struct net_device *dev)
{
	int i, sw_info, net_diag;
	int ioaddr = dev->base_addr;

	/* Activating the board required and does no harm otherwise */
	outw(0x0001, ioaddr + 4);

	/* Set the IRQ line. */
	outw((dev->irq << 12) | 0x0f00, ioaddr + WN0_IRQ);

	/* Set the station address in window 2 each time opened. */
	EL3WINDOW(2);

	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + i);

	if ((dev->if_port & 0x03) == 3) /* BNC interface */
		/* Start the thinnet transceiver. We should really wait 50ms...*/
		outw(StartCoax, ioaddr + EL3_CMD);
	else if ((dev->if_port & 0x03) == 0) { /* 10baseT interface */
		/* Combine secondary sw_info word (the adapter level) and primary
			sw_info word (duplex setting plus other useless bits) */
		EL3WINDOW(0);
		sw_info = (read_eeprom(ioaddr, 0x14) & 0x400f) |
			(read_eeprom(ioaddr, 0x0d) & 0xBff0);

		EL3WINDOW(4);
		net_diag = inw(ioaddr + WN4_NETDIAG);
		net_diag = (net_diag | FD_ENABLE); /* temporarily assume full-duplex will be set */
		printk("%s: ", dev->name);
		switch (dev->if_port & 0x0c) {
			case 12:
				/* force full-duplex mode if 3c5x9b */
				if (sw_info & 0x000f) {
					printk("Forcing 3c5x9b full-duplex mode");
					break;
				}
			case 8:
				/* set full-duplex mode based on eeprom config setting */
				if ((sw_info & 0x000f) && (sw_info & 0x8000)) {
					printk("Setting 3c5x9b full-duplex mode (from EEPROM configuration bit)");
					break;
				}
			default:
				/* xcvr=(0 || 4) OR user has an old 3c5x9 non "B" model */
				printk("Setting 3c5x9/3c5x9B half-duplex mode");
				net_diag = (net_diag & ~FD_ENABLE); /* disable full duplex */
		}

		outw(net_diag, ioaddr + WN4_NETDIAG);
		printk(" if_port: %d, sw_info: %4.4x\n", dev->if_port, sw_info);
		if (el3_debug > 3)
			printk("%s: 3c5x9 net diag word is now: %4.4x.\n", dev->name, net_diag);
		/* Enable link beat and jabber check. */
		outw(inw(ioaddr + WN4_MEDIA) | MEDIA_TP, ioaddr + WN4_MEDIA);
	}

	/* Switch to the stats window, and clear all stats by reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	EL3WINDOW(6);
	for (i = 0; i < 9; i++)
		inb(ioaddr + i);
	inw(ioaddr + 10);
	inw(ioaddr + 12);

	/* Switch to register set 1 for normal use. */
	EL3WINDOW(1);

	/* Accept b-case and phys addr only. */
	outw(SetRxFilter | RxStation | RxBroadcast, ioaddr + EL3_CMD);
	outw(StatsEnable, ioaddr + EL3_CMD); /* Turn on statistics. */

	outw(RxEnable, ioaddr + EL3_CMD); /* Enable the receiver. */
	outw(TxEnable, ioaddr + EL3_CMD); /* Enable transmitter. */
	/* Allow status bits to be seen. */
	outw(SetStatusEnb | 0xff, ioaddr + EL3_CMD);
	/* Ack all pending events, and set active indicator mask. */
	outw(AckIntr | IntLatch | TxAvailable | RxEarly | IntReq,
		 ioaddr + EL3_CMD);
	outw(SetIntrEnb | IntLatch|TxAvailable|TxComplete|RxComplete|StatsFull,
		 ioaddr + EL3_CMD);

	netif_start_queue(dev);
}

/* Power Management support functions */
#ifdef CONFIG_PM

static int
el3_suspend(struct device *pdev, pm_message_t state)
{
	unsigned long flags;
	struct net_device *dev;
	struct el3_private *lp;
	int ioaddr;

	dev = pdev->driver_data;
	lp = netdev_priv(dev);
	ioaddr = dev->base_addr;

	spin_lock_irqsave(&lp->lock, flags);

	if (netif_running(dev))
		netif_device_detach(dev);

	el3_down(dev);
	outw(PowerDown, ioaddr + EL3_CMD);

	spin_unlock_irqrestore(&lp->lock, flags);
	return 0;
}

static int
el3_resume(struct device *pdev)
{
	unsigned long flags;
	struct net_device *dev;
	struct el3_private *lp;
	int ioaddr;

	dev = pdev->driver_data;
	lp = netdev_priv(dev);
	ioaddr = dev->base_addr;

	spin_lock_irqsave(&lp->lock, flags);

	outw(PowerUp, ioaddr + EL3_CMD);
	EL3WINDOW(0);
	el3_up(dev);

	if (netif_running(dev))
		netif_device_attach(dev);

	spin_unlock_irqrestore(&lp->lock, flags);
	return 0;
}

#endif /* CONFIG_PM */

module_param(debug,int, 0);
module_param_array(irq, int, NULL, 0);
module_param(max_interrupt_work, int, 0);
MODULE_PARM_DESC(debug, "debug level (0-6)");
MODULE_PARM_DESC(irq, "IRQ number(s) (assigned)");
MODULE_PARM_DESC(max_interrupt_work, "maximum events handled per interrupt");
#ifdef CONFIG_PNP
module_param(nopnp, int, 0);
MODULE_PARM_DESC(nopnp, "disable ISA PnP support (0-1)");
#endif	/* CONFIG_PNP */
MODULE_DESCRIPTION("3Com Etherlink III (3c509, 3c509B, 3c529, 3c579) ethernet driver");
MODULE_LICENSE("GPL");

static int __init el3_init_module(void)
{
	int ret = 0;

	if (debug >= 0)
		el3_debug = debug;

#ifdef CONFIG_PNP
	if (!nopnp) {
		ret = pnp_register_driver(&el3_pnp_driver);
		if (!ret)
			pnp_registered = 1;
	}
#endif
	/* Select an open I/O location at 0x1*0 to do ISA contention select. */
	/* Start with 0x110 to avoid some sound cards.*/
	for (id_port = 0x110 ; id_port < 0x200; id_port += 0x10) {
		if (!request_region(id_port, 1, "3c509-control"))
			continue;
		outb(0x00, id_port);
		outb(0xff, id_port);
		if (inb(id_port) & 0x01)
			break;
		else
			release_region(id_port, 1);
	}
	if (id_port >= 0x200) {
		id_port = 0;
		printk(KERN_ERR "No I/O port available for 3c509 activation.\n");
	} else {
		ret = isa_register_driver(&el3_isa_driver, EL3_MAX_CARDS);
		if (!ret)
			isa_registered = 1;
	}
#ifdef CONFIG_EISA
	ret = eisa_driver_register(&el3_eisa_driver);
	if (!ret)
		eisa_registered = 1;
#endif
#ifdef CONFIG_MCA
	ret = mca_register_driver(&el3_mca_driver);
	if (!ret)
		mca_registered = 1;
#endif

#ifdef CONFIG_PNP
	if (pnp_registered)
		ret = 0;
#endif
	if (isa_registered)
		ret = 0;
#ifdef CONFIG_EISA
	if (eisa_registered)
		ret = 0;
#endif
#ifdef CONFIG_MCA
	if (mca_registered)
		ret = 0;
#endif
	return ret;
}

static void __exit el3_cleanup_module(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_driver(&el3_pnp_driver);
#endif
	if (isa_registered)
		isa_unregister_driver(&el3_isa_driver);
	if (id_port)
		release_region(id_port, 1);
#ifdef CONFIG_EISA
	if (eisa_registered)
		eisa_driver_unregister(&el3_eisa_driver);
#endif
#ifdef CONFIG_MCA
	if (mca_registered)
		mca_unregister_driver(&el3_mca_driver);
#endif
}

module_init (el3_init_module);
module_exit (el3_cleanup_module);

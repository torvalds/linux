/* epic100.c: A SMC 83c170 EPIC/100 Fast Ethernet driver for Linux. */
/*
	Written/copyright 1997-2001 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	This driver is for the SMC83c170/175 "EPIC" series, as used on the
	SMC EtherPower II 9432 PCI adapter, and several CardBus cards.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Information and updates available at
	http://www.scyld.com/network/epic100.html
	[this link no longer provides anything useful -jgarzik]

	---------------------------------------------------------------------

*/

#define DRV_NAME        "epic100"
#define DRV_VERSION     "2.1"
#define DRV_RELDATE     "Sept 11, 2006"

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */

/* Used to pass the full-duplex flag, etc. */
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak;

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for operational efficiency.
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	256
#define TX_QUEUE_LEN	240		/* Limit ring entries actually used.  */
#define RX_RING_SIZE	256
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct epic_tx_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct epic_rx_desc)

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

/* Bytes transferred to chip before transmission starts. */
/* Initial threshold, increased on underflow, rounded down to 4 byte units. */
#define TX_FIFO_THRESH 256
#define RX_FIFO_THRESH 1		/* 0-3, 0==32, 64,96, or 3==128 bytes  */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
DRV_NAME ".c:v1.11 1/7/2001 Written by Donald Becker <becker@scyld.com>\n";
static char version2[] __devinitdata =
"  (unofficial 2.4.x kernel port, version " DRV_VERSION ", " DRV_RELDATE ")\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("SMC 83c170 EPIC series Ethernet driver");
MODULE_LICENSE("GPL");

module_param(debug, int, 0);
module_param(rx_copybreak, int, 0);
module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);
MODULE_PARM_DESC(debug, "EPIC/100 debug level (0-5)");
MODULE_PARM_DESC(options, "EPIC/100: Bits 0-3: media type, bit 4: full duplex");
MODULE_PARM_DESC(rx_copybreak, "EPIC/100 copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(full_duplex, "EPIC/100 full duplex setting(s) (1)");

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the SMC "EPIC/100", the SMC
single-chip Ethernet controllers for PCI.  This chip is used on
the SMC EtherPower II boards.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS will assign the
PCI INTA signal to a (preferably otherwise unused) system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

III. Driver operation

IIIa. Ring buffers

IVb. References

http://www.smsc.com/media/Downloads_Public/discontinued/83c171.pdf
http://www.smsc.com/media/Downloads_Public/discontinued/83c175.pdf
http://scyld.com/expert/NWay.html
http://www.national.com/pf/DP/DP83840A.html

IVc. Errata

*/


enum chip_capability_flags { MII_PWRDWN=1, TYPE2_INTR=2, NO_MII=4 };

#define EPIC_TOTAL_SIZE 0x100
#define USE_IO_OPS 1

typedef enum {
	SMSC_83C170_0,
	SMSC_83C170,
	SMSC_83C175,
} chip_t;


struct epic_chip_info {
	const char *name;
        int drv_flags;                          /* Driver use, intended as capability flags. */
};


/* indexed by chip_t */
static const struct epic_chip_info pci_id_tbl[] = {
	{ "SMSC EPIC/100 83c170",	TYPE2_INTR | NO_MII | MII_PWRDWN },
	{ "SMSC EPIC/100 83c170",	TYPE2_INTR },
	{ "SMSC EPIC/C 83c175",		TYPE2_INTR | MII_PWRDWN },
};


static DEFINE_PCI_DEVICE_TABLE(epic_pci_tbl) = {
	{ 0x10B8, 0x0005, 0x1092, 0x0AB4, 0, 0, SMSC_83C170_0 },
	{ 0x10B8, 0x0005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SMSC_83C170 },
	{ 0x10B8, 0x0006, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, SMSC_83C175 },
	{ 0,}
};
MODULE_DEVICE_TABLE (pci, epic_pci_tbl);


#ifndef USE_IO_OPS
#undef inb
#undef inw
#undef inl
#undef outb
#undef outw
#undef outl
#define inb readb
#define inw readw
#define inl readl
#define outb writeb
#define outw writew
#define outl writel
#endif

/* Offsets to registers, using the (ugh) SMC names. */
enum epic_registers {
  COMMAND=0, INTSTAT=4, INTMASK=8, GENCTL=0x0C, NVCTL=0x10, EECTL=0x14,
  PCIBurstCnt=0x18,
  TEST1=0x1C, CRCCNT=0x20, ALICNT=0x24, MPCNT=0x28,	/* Rx error counters. */
  MIICtrl=0x30, MIIData=0x34, MIICfg=0x38,
  LAN0=64,						/* MAC address. */
  MC0=80,						/* Multicast filter table. */
  RxCtrl=96, TxCtrl=112, TxSTAT=0x74,
  PRxCDAR=0x84, RxSTAT=0xA4, EarlyRx=0xB0, PTxCDAR=0xC4, TxThresh=0xDC,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatus {
	TxIdle=0x40000, RxIdle=0x20000, IntrSummary=0x010000,
	PCIBusErr170=0x7000, PCIBusErr175=0x1000, PhyEvent175=0x8000,
	RxStarted=0x0800, RxEarlyWarn=0x0400, CntFull=0x0200, TxUnderrun=0x0100,
	TxEmpty=0x0080, TxDone=0x0020, RxError=0x0010,
	RxOverflow=0x0008, RxFull=0x0004, RxHeader=0x0002, RxDone=0x0001,
};
enum CommandBits {
	StopRx=1, StartRx=2, TxQueued=4, RxQueued=8,
	StopTxDMA=0x20, StopRxDMA=0x40, RestartTx=0x80,
};

#define EpicRemoved	0xffffffff	/* Chip failed or removed (CardBus) */

#define EpicNapiEvent	(TxEmpty | TxDone | \
			 RxDone | RxStarted | RxEarlyWarn | RxOverflow | RxFull)
#define EpicNormalEvent	(0x0000ffff & ~EpicNapiEvent)

static const u16 media2miictl[16] = {
	0, 0x0C00, 0x0C00, 0x2000,  0x0100, 0x2100, 0, 0,
	0, 0, 0, 0,  0, 0, 0, 0 };

/*
 * The EPIC100 Rx and Tx buffer descriptors.  Note that these
 * really ARE host-endian; it's not a misannotation.  We tell
 * the card to byteswap them internally on big-endian hosts -
 * look for #ifdef __BIG_ENDIAN in epic_open().
 */

struct epic_tx_desc {
	u32 txstatus;
	u32 bufaddr;
	u32 buflength;
	u32 next;
};

struct epic_rx_desc {
	u32 rxstatus;
	u32 bufaddr;
	u32 buflength;
	u32 next;
};

enum desc_status_bits {
	DescOwn=0x8000,
};

#define PRIV_ALIGN	15 	/* Required alignment mask */
struct epic_private {
	struct epic_rx_desc *rx_ring;
	struct epic_tx_desc *tx_ring;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];

	dma_addr_t tx_ring_dma;
	dma_addr_t rx_ring_dma;

	/* Ring pointers. */
	spinlock_t lock;				/* Group with Tx control cache line. */
	spinlock_t napi_lock;
	struct napi_struct napi;
	unsigned int reschedule_in_poll;
	unsigned int cur_tx, dirty_tx;

	unsigned int cur_rx, dirty_rx;
	u32 irq_mask;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */

	struct pci_dev *pci_dev;			/* PCI bus location. */
	int chip_id, chip_flags;

	struct timer_list timer;			/* Media selection timer. */
	int tx_threshold;
	unsigned char mc_filter[8];
	signed char phys[4];				/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	int mii_phy_cnt;
	struct mii_if_info mii;
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
};

static int epic_open(struct net_device *dev);
static int read_eeprom(long ioaddr, int location);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int loc, int val);
static void epic_restart(struct net_device *dev);
static void epic_timer(unsigned long data);
static void epic_tx_timeout(struct net_device *dev);
static void epic_init_ring(struct net_device *dev);
static netdev_tx_t epic_start_xmit(struct sk_buff *skb,
				   struct net_device *dev);
static int epic_rx(struct net_device *dev, int budget);
static int epic_poll(struct napi_struct *napi, int budget);
static irqreturn_t epic_interrupt(int irq, void *dev_instance);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static const struct ethtool_ops netdev_ethtool_ops;
static int epic_close(struct net_device *dev);
static struct net_device_stats *epic_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);

static const struct net_device_ops epic_netdev_ops = {
	.ndo_open		= epic_open,
	.ndo_stop		= epic_close,
	.ndo_start_xmit		= epic_start_xmit,
	.ndo_tx_timeout 	= epic_tx_timeout,
	.ndo_get_stats		= epic_get_stats,
	.ndo_set_multicast_list = set_rx_mode,
	.ndo_do_ioctl 		= netdev_ioctl,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __devinit epic_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	static int card_idx = -1;
	long ioaddr;
	int chip_idx = (int) ent->driver_data;
	int irq;
	struct net_device *dev;
	struct epic_private *ep;
	int i, ret, option = 0, duplex = 0;
	void *ring_space;
	dma_addr_t ring_dma;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(KERN_INFO "%s%s", version, version2);
#endif

	card_idx++;

	ret = pci_enable_device(pdev);
	if (ret)
		goto out;
	irq = pdev->irq;

	if (pci_resource_len(pdev, 0) < EPIC_TOTAL_SIZE) {
		dev_err(&pdev->dev, "no PCI region space\n");
		ret = -ENODEV;
		goto err_out_disable;
	}

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret < 0)
		goto err_out_disable;

	ret = -ENOMEM;

	dev = alloc_etherdev(sizeof (*ep));
	if (!dev) {
		dev_err(&pdev->dev, "no memory for eth device\n");
		goto err_out_free_res;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);

#ifdef USE_IO_OPS
	ioaddr = pci_resource_start (pdev, 0);
#else
	ioaddr = pci_resource_start (pdev, 1);
	ioaddr = (long) pci_ioremap_bar(pdev, 1);
	if (!ioaddr) {
		dev_err(&pdev->dev, "ioremap failed\n");
		goto err_out_free_netdev;
	}
#endif

	pci_set_drvdata(pdev, dev);
	ep = netdev_priv(dev);
	ep->mii.dev = dev;
	ep->mii.mdio_read = mdio_read;
	ep->mii.mdio_write = mdio_write;
	ep->mii.phy_id_mask = 0x1f;
	ep->mii.reg_num_mask = 0x1f;

	ring_space = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_iounmap;
	ep->tx_ring = (struct epic_tx_desc *)ring_space;
	ep->tx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_unmap_tx;
	ep->rx_ring = (struct epic_rx_desc *)ring_space;
	ep->rx_ring_dma = ring_dma;

	if (dev->mem_start) {
		option = dev->mem_start;
		duplex = (dev->mem_start & 16) ? 1 : 0;
	} else if (card_idx >= 0  &&  card_idx < MAX_UNITS) {
		if (options[card_idx] >= 0)
			option = options[card_idx];
		if (full_duplex[card_idx] >= 0)
			duplex = full_duplex[card_idx];
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;

	spin_lock_init(&ep->lock);
	spin_lock_init(&ep->napi_lock);
	ep->reschedule_in_poll = 0;

	/* Bring the chip out of low-power mode. */
	outl(0x4200, ioaddr + GENCTL);
	/* Magic?!  If we don't set this bit the MII interface won't work. */
	/* This magic is documented in SMSC app note 7.15 */
	for (i = 16; i > 0; i--)
		outl(0x0008, ioaddr + TEST1);

	/* Turn on the MII transceiver. */
	outl(0x12, ioaddr + MIICfg);
	if (chip_idx == 1)
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);
	outl(0x0200, ioaddr + GENCTL);

	/* Note: the '175 does not have a serial EEPROM. */
	for (i = 0; i < 3; i++)
		((__le16 *)dev->dev_addr)[i] = cpu_to_le16(inw(ioaddr + LAN0 + i*4));

	if (debug > 2) {
		dev_printk(KERN_DEBUG, &pdev->dev, "EEPROM contents:\n");
		for (i = 0; i < 64; i++)
			printk(" %4.4x%s", read_eeprom(ioaddr, i),
				   i % 16 == 15 ? "\n" : "");
	}

	ep->pci_dev = pdev;
	ep->chip_id = chip_idx;
	ep->chip_flags = pci_id_tbl[chip_idx].drv_flags;
	ep->irq_mask =
		(ep->chip_flags & TYPE2_INTR ?  PCIBusErr175 : PCIBusErr170)
		 | CntFull | TxUnderrun | EpicNapiEvent;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later, but
	   takes much time and no cards have external MII. */
	{
		int phy, phy_idx = 0;
		for (phy = 1; phy < 32 && phy_idx < sizeof(ep->phys); phy++) {
			int mii_status = mdio_read(dev, phy, MII_BMSR);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				ep->phys[phy_idx++] = phy;
				dev_info(&pdev->dev,
					"MII transceiver #%d control "
					"%4.4x status %4.4x.\n",
					phy, mdio_read(dev, phy, 0), mii_status);
			}
		}
		ep->mii_phy_cnt = phy_idx;
		if (phy_idx != 0) {
			phy = ep->phys[0];
			ep->mii.advertising = mdio_read(dev, phy, MII_ADVERTISE);
			dev_info(&pdev->dev,
				"Autonegotiation advertising %4.4x link "
				   "partner %4.4x.\n",
				   ep->mii.advertising, mdio_read(dev, phy, 5));
		} else if ( ! (ep->chip_flags & NO_MII)) {
			dev_warn(&pdev->dev,
				"***WARNING***: No MII transceiver found!\n");
			/* Use the known PHY address of the EPII. */
			ep->phys[0] = 3;
		}
		ep->mii.phy_id = ep->phys[0];
	}

	/* Turn off the MII xcvr (175 only!), leave the chip in low-power mode. */
	if (ep->chip_flags & MII_PWRDWN)
		outl(inl(ioaddr + NVCTL) & ~0x483C, ioaddr + NVCTL);
	outl(0x0008, ioaddr + GENCTL);

	/* The lower four bits are the media type. */
	if (duplex) {
		ep->mii.force_media = ep->mii.full_duplex = 1;
		dev_info(&pdev->dev, "Forced full duplex requested.\n");
	}
	dev->if_port = ep->default_port = option;

	/* The Epic-specific entries in the device structure. */
	dev->netdev_ops = &epic_netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
	netif_napi_add(dev, &ep->napi, epic_poll, 64);

	ret = register_netdev(dev);
	if (ret < 0)
		goto err_out_unmap_rx;

	printk(KERN_INFO "%s: %s at %#lx, IRQ %d, %pM\n",
	       dev->name, pci_id_tbl[chip_idx].name, ioaddr, dev->irq,
	       dev->dev_addr);

out:
	return ret;

err_out_unmap_rx:
	pci_free_consistent(pdev, RX_TOTAL_SIZE, ep->rx_ring, ep->rx_ring_dma);
err_out_unmap_tx:
	pci_free_consistent(pdev, TX_TOTAL_SIZE, ep->tx_ring, ep->tx_ring_dma);
err_out_iounmap:
#ifndef USE_IO_OPS
	iounmap(ioaddr);
err_out_free_netdev:
#endif
	free_netdev(dev);
err_out_free_res:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
	goto out;
}

/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS			0x02	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x08	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x09
#define EE_DATA_READ	0x10	/* EEPROM chip data out. */
#define EE_ENB			(0x0001 | EE_CS)

/* Delay between EEPROM clock transitions.
   This serves to flush the operation to the PCI bus.
 */

#define eeprom_delay()	inl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5 << 6)
#define EE_READ64_CMD	(6 << 6)
#define EE_READ256_CMD	(6 << 8)
#define EE_ERASE_CMD	(7 << 6)

static void epic_disable_int(struct net_device *dev, struct epic_private *ep)
{
	long ioaddr = dev->base_addr;

	outl(0x00000000, ioaddr + INTMASK);
}

static inline void __epic_pci_commit(long ioaddr)
{
#ifndef USE_IO_OPS
	inl(ioaddr + INTMASK);
#endif
}

static inline void epic_napi_irq_off(struct net_device *dev,
				     struct epic_private *ep)
{
	long ioaddr = dev->base_addr;

	outl(ep->irq_mask & ~EpicNapiEvent, ioaddr + INTMASK);
	__epic_pci_commit(ioaddr);
}

static inline void epic_napi_irq_on(struct net_device *dev,
				    struct epic_private *ep)
{
	long ioaddr = dev->base_addr;

	/* No need to commit possible posted write */
	outl(ep->irq_mask | EpicNapiEvent, ioaddr + INTMASK);
}

static int __devinit read_eeprom(long ioaddr, int location)
{
	int i;
	int retval = 0;
	long ee_addr = ioaddr + EECTL;
	int read_cmd = location |
		(inl(ee_addr) & 0x40 ? EE_READ64_CMD : EE_READ256_CMD);

	outl(EE_ENB & ~EE_CS, ee_addr);
	outl(EE_ENB, ee_addr);

	/* Shift the read command bits out. */
	for (i = 12; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_WRITE_1 : EE_WRITE_0;
		outl(EE_ENB | dataval, ee_addr);
		eeprom_delay();
		outl(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
	}
	outl(EE_ENB, ee_addr);

	for (i = 16; i > 0; i--) {
		outl(EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
		retval = (retval << 1) | ((inl(ee_addr) & EE_DATA_READ) ? 1 : 0);
		outl(EE_ENB, ee_addr);
		eeprom_delay();
	}

	/* Terminate the EEPROM access. */
	outl(EE_ENB & ~EE_CS, ee_addr);
	return retval;
}

#define MII_READOP		1
#define MII_WRITEOP		2
static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	long ioaddr = dev->base_addr;
	int read_cmd = (phy_id << 9) | (location << 4) | MII_READOP;
	int i;

	outl(read_cmd, ioaddr + MIICtrl);
	/* Typical operation takes 25 loops. */
	for (i = 400; i > 0; i--) {
		barrier();
		if ((inl(ioaddr + MIICtrl) & MII_READOP) == 0) {
			/* Work around read failure bug. */
			if (phy_id == 1 && location < 6 &&
			    inw(ioaddr + MIIData) == 0xffff) {
				outl(read_cmd, ioaddr + MIICtrl);
				continue;
			}
			return inw(ioaddr + MIIData);
		}
	}
	return 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int loc, int value)
{
	long ioaddr = dev->base_addr;
	int i;

	outw(value, ioaddr + MIIData);
	outl((phy_id << 9) | (loc << 4) | MII_WRITEOP, ioaddr + MIICtrl);
	for (i = 10000; i > 0; i--) {
		barrier();
		if ((inl(ioaddr + MIICtrl) & MII_WRITEOP) == 0)
			break;
	}
}


static int epic_open(struct net_device *dev)
{
	struct epic_private *ep = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	int i;
	int retval;

	/* Soft reset the chip. */
	outl(0x4001, ioaddr + GENCTL);

	napi_enable(&ep->napi);
	if ((retval = request_irq(dev->irq, epic_interrupt, IRQF_SHARED, dev->name, dev))) {
		napi_disable(&ep->napi);
		return retval;
	}

	epic_init_ring(dev);

	outl(0x4000, ioaddr + GENCTL);
	/* This magic is documented in SMSC app note 7.15 */
	for (i = 16; i > 0; i--)
		outl(0x0008, ioaddr + TEST1);

	/* Pull the chip out of low-power mode, enable interrupts, and set for
	   PCI read multiple.  The MIIcfg setting and strange write order are
	   required by the details of which bits are reset and the transceiver
	   wiring on the Ositech CardBus card.
	*/
#if 0
	outl(dev->if_port == 1 ? 0x13 : 0x12, ioaddr + MIICfg);
#endif
	if (ep->chip_flags & MII_PWRDWN)
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);

	/* Tell the chip to byteswap descriptors on big-endian hosts */
#ifdef __BIG_ENDIAN
	outl(0x4432 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
	inl(ioaddr + GENCTL);
	outl(0x0432 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#else
	outl(0x4412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
	inl(ioaddr + GENCTL);
	outl(0x0412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#endif

	udelay(20); /* Looks like EPII needs that if you want reliable RX init. FIXME: pci posting bug? */

	for (i = 0; i < 3; i++)
		outl(le16_to_cpu(((__le16*)dev->dev_addr)[i]), ioaddr + LAN0 + i*4);

	ep->tx_threshold = TX_FIFO_THRESH;
	outl(ep->tx_threshold, ioaddr + TxThresh);

	if (media2miictl[dev->if_port & 15]) {
		if (ep->mii_phy_cnt)
			mdio_write(dev, ep->phys[0], MII_BMCR, media2miictl[dev->if_port&15]);
		if (dev->if_port == 1) {
			if (debug > 1)
				printk(KERN_INFO "%s: Using the 10base2 transceiver, MII "
					   "status %4.4x.\n",
					   dev->name, mdio_read(dev, ep->phys[0], MII_BMSR));
		}
	} else {
		int mii_lpa = mdio_read(dev, ep->phys[0], MII_LPA);
		if (mii_lpa != 0xffff) {
			if ((mii_lpa & LPA_100FULL) || (mii_lpa & 0x01C0) == LPA_10FULL)
				ep->mii.full_duplex = 1;
			else if (! (mii_lpa & LPA_LPACK))
				mdio_write(dev, ep->phys[0], MII_BMCR, BMCR_ANENABLE|BMCR_ANRESTART);
			if (debug > 1)
				printk(KERN_INFO "%s: Setting %s-duplex based on MII xcvr %d"
					   " register read of %4.4x.\n", dev->name,
					   ep->mii.full_duplex ? "full" : "half",
					   ep->phys[0], mii_lpa);
		}
	}

	outl(ep->mii.full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	outl(ep->rx_ring_dma, ioaddr + PRxCDAR);
	outl(ep->tx_ring_dma, ioaddr + PTxCDAR);

	/* Start the chip's Rx process. */
	set_rx_mode(dev);
	outl(StartRx | RxQueued, ioaddr + COMMAND);

	netif_start_queue(dev);

	/* Enable interrupts by setting the interrupt mask. */
	outl((ep->chip_flags & TYPE2_INTR ? PCIBusErr175 : PCIBusErr170)
		 | CntFull | TxUnderrun
		 | RxError | RxHeader | EpicNapiEvent, ioaddr + INTMASK);

	if (debug > 1)
		printk(KERN_DEBUG "%s: epic_open() ioaddr %lx IRQ %d status %4.4x "
			   "%s-duplex.\n",
			   dev->name, ioaddr, dev->irq, (int)inl(ioaddr + GENCTL),
			   ep->mii.full_duplex ? "full" : "half");

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&ep->timer);
	ep->timer.expires = jiffies + 3*HZ;
	ep->timer.data = (unsigned long)dev;
	ep->timer.function = epic_timer;				/* timer handler */
	add_timer(&ep->timer);

	return 0;
}

/* Reset the chip to recover from a PCI transaction error.
   This may occur at interrupt time. */
static void epic_pause(struct net_device *dev)
{
	long ioaddr = dev->base_addr;

	netif_stop_queue (dev);

	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x00000000, ioaddr + INTMASK);
	/* Stop the chip's Tx and Rx DMA processes. */
	outw(StopRx | StopTxDMA | StopRxDMA, ioaddr + COMMAND);

	/* Update the error counts. */
	if (inw(ioaddr + COMMAND) != 0xffff) {
		dev->stats.rx_missed_errors += inb(ioaddr + MPCNT);
		dev->stats.rx_frame_errors += inb(ioaddr + ALICNT);
		dev->stats.rx_crc_errors += inb(ioaddr + CRCCNT);
	}

	/* Remove the packets on the Rx queue. */
	epic_rx(dev, RX_RING_SIZE);
}

static void epic_restart(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = netdev_priv(dev);
	int i;

	/* Soft reset the chip. */
	outl(0x4001, ioaddr + GENCTL);

	printk(KERN_DEBUG "%s: Restarting the EPIC chip, Rx %d/%d Tx %d/%d.\n",
		   dev->name, ep->cur_rx, ep->dirty_rx, ep->dirty_tx, ep->cur_tx);
	udelay(1);

	/* This magic is documented in SMSC app note 7.15 */
	for (i = 16; i > 0; i--)
		outl(0x0008, ioaddr + TEST1);

#ifdef __BIG_ENDIAN
	outl(0x0432 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#else
	outl(0x0412 | (RX_FIFO_THRESH<<8), ioaddr + GENCTL);
#endif
	outl(dev->if_port == 1 ? 0x13 : 0x12, ioaddr + MIICfg);
	if (ep->chip_flags & MII_PWRDWN)
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);

	for (i = 0; i < 3; i++)
		outl(le16_to_cpu(((__le16*)dev->dev_addr)[i]), ioaddr + LAN0 + i*4);

	ep->tx_threshold = TX_FIFO_THRESH;
	outl(ep->tx_threshold, ioaddr + TxThresh);
	outl(ep->mii.full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	outl(ep->rx_ring_dma + (ep->cur_rx%RX_RING_SIZE)*
		sizeof(struct epic_rx_desc), ioaddr + PRxCDAR);
	outl(ep->tx_ring_dma + (ep->dirty_tx%TX_RING_SIZE)*
		 sizeof(struct epic_tx_desc), ioaddr + PTxCDAR);

	/* Start the chip's Rx process. */
	set_rx_mode(dev);
	outl(StartRx | RxQueued, ioaddr + COMMAND);

	/* Enable interrupts by setting the interrupt mask. */
	outl((ep->chip_flags & TYPE2_INTR ? PCIBusErr175 : PCIBusErr170)
		 | CntFull | TxUnderrun
		 | RxError | RxHeader | EpicNapiEvent, ioaddr + INTMASK);

	printk(KERN_DEBUG "%s: epic_restart() done, cmd status %4.4x, ctl %4.4x"
		   " interrupt %4.4x.\n",
		   dev->name, (int)inl(ioaddr + COMMAND), (int)inl(ioaddr + GENCTL),
		   (int)inl(ioaddr + INTSTAT));
}

static void check_media(struct net_device *dev)
{
	struct epic_private *ep = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	int mii_lpa = ep->mii_phy_cnt ? mdio_read(dev, ep->phys[0], MII_LPA) : 0;
	int negotiated = mii_lpa & ep->mii.advertising;
	int duplex = (negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040;

	if (ep->mii.force_media)
		return;
	if (mii_lpa == 0xffff)		/* Bogus read */
		return;
	if (ep->mii.full_duplex != duplex) {
		ep->mii.full_duplex = duplex;
		printk(KERN_INFO "%s: Setting %s-duplex based on MII #%d link"
			   " partner capability of %4.4x.\n", dev->name,
			   ep->mii.full_duplex ? "full" : "half", ep->phys[0], mii_lpa);
		outl(ep->mii.full_duplex ? 0x7F : 0x79, ioaddr + TxCtrl);
	}
}

static void epic_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct epic_private *ep = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	int next_tick = 5*HZ;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: Media monitor tick, Tx status %8.8x.\n",
			   dev->name, (int)inl(ioaddr + TxSTAT));
		printk(KERN_DEBUG "%s: Other registers are IntMask %4.4x "
			   "IntStatus %4.4x RxStatus %4.4x.\n",
			   dev->name, (int)inl(ioaddr + INTMASK),
			   (int)inl(ioaddr + INTSTAT), (int)inl(ioaddr + RxSTAT));
	}

	check_media(dev);

	ep->timer.expires = jiffies + next_tick;
	add_timer(&ep->timer);
}

static void epic_tx_timeout(struct net_device *dev)
{
	struct epic_private *ep = netdev_priv(dev);
	long ioaddr = dev->base_addr;

	if (debug > 0) {
		printk(KERN_WARNING "%s: Transmit timeout using MII device, "
			   "Tx status %4.4x.\n",
			   dev->name, (int)inw(ioaddr + TxSTAT));
		if (debug > 1) {
			printk(KERN_DEBUG "%s: Tx indices: dirty_tx %d, cur_tx %d.\n",
				   dev->name, ep->dirty_tx, ep->cur_tx);
		}
	}
	if (inw(ioaddr + TxSTAT) & 0x10) {		/* Tx FIFO underflow. */
		dev->stats.tx_fifo_errors++;
		outl(RestartTx, ioaddr + COMMAND);
	} else {
		epic_restart(dev);
		outl(TxQueued, dev->base_addr + COMMAND);
	}

	dev->trans_start = jiffies; /* prevent tx timeout */
	dev->stats.tx_errors++;
	if (!ep->tx_full)
		netif_wake_queue(dev);
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void epic_init_ring(struct net_device *dev)
{
	struct epic_private *ep = netdev_priv(dev);
	int i;

	ep->tx_full = 0;
	ep->dirty_tx = ep->cur_tx = 0;
	ep->cur_rx = ep->dirty_rx = 0;
	ep->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		ep->rx_ring[i].rxstatus = 0;
		ep->rx_ring[i].buflength = ep->rx_buf_sz;
		ep->rx_ring[i].next = ep->rx_ring_dma +
				      (i+1)*sizeof(struct epic_rx_desc);
		ep->rx_skbuff[i] = NULL;
	}
	/* Mark the last entry as wrapping the ring. */
	ep->rx_ring[i-1].next = ep->rx_ring_dma;

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(ep->rx_buf_sz);
		ep->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb_reserve(skb, 2);	/* 16 byte align the IP header. */
		ep->rx_ring[i].bufaddr = pci_map_single(ep->pci_dev,
			skb->data, ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
		ep->rx_ring[i].rxstatus = DescOwn;
	}
	ep->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		ep->tx_skbuff[i] = NULL;
		ep->tx_ring[i].txstatus = 0x0000;
		ep->tx_ring[i].next = ep->tx_ring_dma +
			(i+1)*sizeof(struct epic_tx_desc);
	}
	ep->tx_ring[i-1].next = ep->tx_ring_dma;
}

static netdev_tx_t epic_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct epic_private *ep = netdev_priv(dev);
	int entry, free_count;
	u32 ctrl_word;
	unsigned long flags;

	if (skb_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	/* Caution: the write order is important here, set the field with the
	   "ownership" bit last. */

	/* Calculate the next Tx descriptor entry. */
	spin_lock_irqsave(&ep->lock, flags);
	free_count = ep->cur_tx - ep->dirty_tx;
	entry = ep->cur_tx % TX_RING_SIZE;

	ep->tx_skbuff[entry] = skb;
	ep->tx_ring[entry].bufaddr = pci_map_single(ep->pci_dev, skb->data,
		 			            skb->len, PCI_DMA_TODEVICE);
	if (free_count < TX_QUEUE_LEN/2) {/* Typical path */
		ctrl_word = 0x100000; /* No interrupt */
	} else if (free_count == TX_QUEUE_LEN/2) {
		ctrl_word = 0x140000; /* Tx-done intr. */
	} else if (free_count < TX_QUEUE_LEN - 1) {
		ctrl_word = 0x100000; /* No Tx-done intr. */
	} else {
		/* Leave room for an additional entry. */
		ctrl_word = 0x140000; /* Tx-done intr. */
		ep->tx_full = 1;
	}
	ep->tx_ring[entry].buflength = ctrl_word | skb->len;
	ep->tx_ring[entry].txstatus =
		((skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN) << 16)
			    | DescOwn;

	ep->cur_tx++;
	if (ep->tx_full)
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&ep->lock, flags);
	/* Trigger an immediate transmit demand. */
	outl(TxQueued, dev->base_addr + COMMAND);

	if (debug > 4)
		printk(KERN_DEBUG "%s: Queued Tx packet size %d to slot %d, "
			   "flag %2.2x Tx status %8.8x.\n",
			   dev->name, (int)skb->len, entry, ctrl_word,
			   (int)inl(dev->base_addr + TxSTAT));

	return NETDEV_TX_OK;
}

static void epic_tx_error(struct net_device *dev, struct epic_private *ep,
			  int status)
{
	struct net_device_stats *stats = &dev->stats;

#ifndef final_version
	/* There was an major error, log it. */
	if (debug > 1)
		printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
		       dev->name, status);
#endif
	stats->tx_errors++;
	if (status & 0x1050)
		stats->tx_aborted_errors++;
	if (status & 0x0008)
		stats->tx_carrier_errors++;
	if (status & 0x0040)
		stats->tx_window_errors++;
	if (status & 0x0010)
		stats->tx_fifo_errors++;
}

static void epic_tx(struct net_device *dev, struct epic_private *ep)
{
	unsigned int dirty_tx, cur_tx;

	/*
	 * Note: if this lock becomes a problem we can narrow the locked
	 * region at the cost of occasionally grabbing the lock more times.
	 */
	cur_tx = ep->cur_tx;
	for (dirty_tx = ep->dirty_tx; cur_tx - dirty_tx > 0; dirty_tx++) {
		struct sk_buff *skb;
		int entry = dirty_tx % TX_RING_SIZE;
		int txstatus = ep->tx_ring[entry].txstatus;

		if (txstatus & DescOwn)
			break;	/* It still hasn't been Txed */

		if (likely(txstatus & 0x0001)) {
			dev->stats.collisions += (txstatus >> 8) & 15;
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += ep->tx_skbuff[entry]->len;
		} else
			epic_tx_error(dev, ep, txstatus);

		/* Free the original skb. */
		skb = ep->tx_skbuff[entry];
		pci_unmap_single(ep->pci_dev, ep->tx_ring[entry].bufaddr,
				 skb->len, PCI_DMA_TODEVICE);
		dev_kfree_skb_irq(skb);
		ep->tx_skbuff[entry] = NULL;
	}

#ifndef final_version
	if (cur_tx - dirty_tx > TX_RING_SIZE) {
		printk(KERN_WARNING
		       "%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
		       dev->name, dirty_tx, cur_tx, ep->tx_full);
		dirty_tx += TX_RING_SIZE;
	}
#endif
	ep->dirty_tx = dirty_tx;
	if (ep->tx_full && cur_tx - dirty_tx < TX_QUEUE_LEN - 4) {
		/* The ring is no longer full, allow new TX entries. */
		ep->tx_full = 0;
		netif_wake_queue(dev);
	}
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t epic_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct epic_private *ep = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	unsigned int handled = 0;
	int status;

	status = inl(ioaddr + INTSTAT);
	/* Acknowledge all of the current interrupt sources ASAP. */
	outl(status & EpicNormalEvent, ioaddr + INTSTAT);

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Interrupt, status=%#8.8x new "
				   "intstat=%#8.8x.\n", dev->name, status,
				   (int)inl(ioaddr + INTSTAT));
	}

	if ((status & IntrSummary) == 0)
		goto out;

	handled = 1;

	if ((status & EpicNapiEvent) && !ep->reschedule_in_poll) {
		spin_lock(&ep->napi_lock);
		if (napi_schedule_prep(&ep->napi)) {
			epic_napi_irq_off(dev, ep);
			__napi_schedule(&ep->napi);
		} else
			ep->reschedule_in_poll++;
		spin_unlock(&ep->napi_lock);
	}
	status &= ~EpicNapiEvent;

	/* Check uncommon events all at once. */
	if (status & (CntFull | TxUnderrun | PCIBusErr170 | PCIBusErr175)) {
		if (status == EpicRemoved)
			goto out;

		/* Always update the error counts to avoid overhead later. */
		dev->stats.rx_missed_errors += inb(ioaddr + MPCNT);
		dev->stats.rx_frame_errors += inb(ioaddr + ALICNT);
		dev->stats.rx_crc_errors += inb(ioaddr + CRCCNT);

		if (status & TxUnderrun) { /* Tx FIFO underflow. */
			dev->stats.tx_fifo_errors++;
			outl(ep->tx_threshold += 128, ioaddr + TxThresh);
			/* Restart the transmit process. */
			outl(RestartTx, ioaddr + COMMAND);
		}
		if (status & PCIBusErr170) {
			printk(KERN_ERR "%s: PCI Bus Error! status %4.4x.\n",
					 dev->name, status);
			epic_pause(dev);
			epic_restart(dev);
		}
		/* Clear all error sources. */
		outl(status & 0x7f18, ioaddr + INTSTAT);
	}

out:
	if (debug > 3) {
		printk(KERN_DEBUG "%s: exit interrupt, intr_status=%#4.4x.\n",
				   dev->name, status);
	}

	return IRQ_RETVAL(handled);
}

static int epic_rx(struct net_device *dev, int budget)
{
	struct epic_private *ep = netdev_priv(dev);
	int entry = ep->cur_rx % RX_RING_SIZE;
	int rx_work_limit = ep->dirty_rx + RX_RING_SIZE - ep->cur_rx;
	int work_done = 0;

	if (debug > 4)
		printk(KERN_DEBUG " In epic_rx(), entry %d %8.8x.\n", entry,
			   ep->rx_ring[entry].rxstatus);

	if (rx_work_limit > budget)
		rx_work_limit = budget;

	/* If we own the next entry, it's a new packet. Send it up. */
	while ((ep->rx_ring[entry].rxstatus & DescOwn) == 0) {
		int status = ep->rx_ring[entry].rxstatus;

		if (debug > 4)
			printk(KERN_DEBUG "  epic_rx() status was %8.8x.\n", status);
		if (--rx_work_limit < 0)
			break;
		if (status & 0x2006) {
			if (debug > 2)
				printk(KERN_DEBUG "%s: epic_rx() error status was %8.8x.\n",
					   dev->name, status);
			if (status & 0x2000) {
				printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
					   "multiple buffers, status %4.4x!\n", dev->name, status);
				dev->stats.rx_length_errors++;
			} else if (status & 0x0006)
				/* Rx Frame errors are counted in hardware. */
				dev->stats.rx_errors++;
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			/* Omit the four octet CRC from the length. */
			short pkt_len = (status >> 16) - 4;
			struct sk_buff *skb;

			if (pkt_len > PKT_BUF_SZ - 4) {
				printk(KERN_ERR "%s: Oversized Ethernet frame, status %x "
					   "%d bytes.\n",
					   dev->name, status, pkt_len);
				pkt_len = 1514;
			}
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak &&
			    (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				pci_dma_sync_single_for_cpu(ep->pci_dev,
							    ep->rx_ring[entry].bufaddr,
							    ep->rx_buf_sz,
							    PCI_DMA_FROMDEVICE);
				skb_copy_to_linear_data(skb, ep->rx_skbuff[entry]->data, pkt_len);
				skb_put(skb, pkt_len);
				pci_dma_sync_single_for_device(ep->pci_dev,
							       ep->rx_ring[entry].bufaddr,
							       ep->rx_buf_sz,
							       PCI_DMA_FROMDEVICE);
			} else {
				pci_unmap_single(ep->pci_dev,
					ep->rx_ring[entry].bufaddr,
					ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
				skb_put(skb = ep->rx_skbuff[entry], pkt_len);
				ep->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_receive_skb(skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;
		}
		work_done++;
		entry = (++ep->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; ep->cur_rx - ep->dirty_rx > 0; ep->dirty_rx++) {
		entry = ep->dirty_rx % RX_RING_SIZE;
		if (ep->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb;
			skb = ep->rx_skbuff[entry] = dev_alloc_skb(ep->rx_buf_sz);
			if (skb == NULL)
				break;
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			ep->rx_ring[entry].bufaddr = pci_map_single(ep->pci_dev,
				skb->data, ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
			work_done++;
		}
		/* AV: shouldn't we add a barrier here? */
		ep->rx_ring[entry].rxstatus = DescOwn;
	}
	return work_done;
}

static void epic_rx_err(struct net_device *dev, struct epic_private *ep)
{
	long ioaddr = dev->base_addr;
	int status;

	status = inl(ioaddr + INTSTAT);

	if (status == EpicRemoved)
		return;
	if (status & RxOverflow) 	/* Missed a Rx frame. */
		dev->stats.rx_errors++;
	if (status & (RxOverflow | RxFull))
		outw(RxQueued, ioaddr + COMMAND);
}

static int epic_poll(struct napi_struct *napi, int budget)
{
	struct epic_private *ep = container_of(napi, struct epic_private, napi);
	struct net_device *dev = ep->mii.dev;
	int work_done = 0;
	long ioaddr = dev->base_addr;

rx_action:

	epic_tx(dev, ep);

	work_done += epic_rx(dev, budget);

	epic_rx_err(dev, ep);

	if (work_done < budget) {
		unsigned long flags;
		int more;

		/* A bit baroque but it avoids a (space hungry) spin_unlock */

		spin_lock_irqsave(&ep->napi_lock, flags);

		more = ep->reschedule_in_poll;
		if (!more) {
			__napi_complete(napi);
			outl(EpicNapiEvent, ioaddr + INTSTAT);
			epic_napi_irq_on(dev, ep);
		} else
			ep->reschedule_in_poll--;

		spin_unlock_irqrestore(&ep->napi_lock, flags);

		if (more)
			goto rx_action;
	}

	return work_done;
}

static int epic_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = netdev_priv(dev);
	struct sk_buff *skb;
	int i;

	netif_stop_queue(dev);
	napi_disable(&ep->napi);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, (int)inl(ioaddr + INTSTAT));

	del_timer_sync(&ep->timer);

	epic_disable_int(dev, ep);

	free_irq(dev->irq, dev);

	epic_pause(dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = ep->rx_skbuff[i];
		ep->rx_skbuff[i] = NULL;
		ep->rx_ring[i].rxstatus = 0;		/* Not owned by Epic chip. */
		ep->rx_ring[i].buflength = 0;
		if (skb) {
			pci_unmap_single(ep->pci_dev, ep->rx_ring[i].bufaddr,
				 	 ep->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
		}
		ep->rx_ring[i].bufaddr = 0xBADF00D0; /* An invalid address. */
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		skb = ep->tx_skbuff[i];
		ep->tx_skbuff[i] = NULL;
		if (!skb)
			continue;
		pci_unmap_single(ep->pci_dev, ep->tx_ring[i].bufaddr,
				 skb->len, PCI_DMA_TODEVICE);
		dev_kfree_skb(skb);
	}

	/* Green! Leave the chip in low-power mode. */
	outl(0x0008, ioaddr + GENCTL);

	return 0;
}

static struct net_device_stats *epic_get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;

	if (netif_running(dev)) {
		/* Update the error counts. */
		dev->stats.rx_missed_errors += inb(ioaddr + MPCNT);
		dev->stats.rx_frame_errors += inb(ioaddr + ALICNT);
		dev->stats.rx_crc_errors += inb(ioaddr + CRCCNT);
	}

	return &dev->stats;
}

/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling ep->setup_frame.  This is non-deterministic
   when re-entered but still correct. */

static void set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct epic_private *ep = netdev_priv(dev);
	unsigned char mc_filter[8];		 /* Multicast hash filter */
	int i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		outl(0x002C, ioaddr + RxCtrl);
		/* Unconditionally log net taps. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
	} else if ((!netdev_mc_empty(dev)) || (dev->flags & IFF_ALLMULTI)) {
		/* There is apparently a chip bug, so the multicast filter
		   is never enabled. */
		/* Too many to filter perfectly -- accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		outl(0x000C, ioaddr + RxCtrl);
	} else if (netdev_mc_empty(dev)) {
		outl(0x0004, ioaddr + RxCtrl);
		return;
	} else {					/* Never executed, for now. */
		struct netdev_hw_addr *ha;

		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, dev) {
			unsigned int bit_nr =
				ether_crc_le(ETH_ALEN, ha->addr) & 0x3f;
			mc_filter[bit_nr >> 3] |= (1 << bit_nr);
		}
	}
	/* ToDo: perhaps we need to stop the Tx and Rx process here? */
	if (memcmp(mc_filter, ep->mc_filter, sizeof(mc_filter))) {
		for (i = 0; i < 4; i++)
			outw(((u16 *)mc_filter)[i], ioaddr + MC0 + i*4);
		memcpy(ep->mc_filter, mc_filter, sizeof(mc_filter));
	}
}

static void netdev_get_drvinfo (struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct epic_private *np = netdev_priv(dev);

	strcpy (info->driver, DRV_NAME);
	strcpy (info->version, DRV_VERSION);
	strcpy (info->bus_info, pci_name(np->pci_dev));
}

static int netdev_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct epic_private *np = netdev_priv(dev);
	int rc;

	spin_lock_irq(&np->lock);
	rc = mii_ethtool_gset(&np->mii, cmd);
	spin_unlock_irq(&np->lock);

	return rc;
}

static int netdev_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct epic_private *np = netdev_priv(dev);
	int rc;

	spin_lock_irq(&np->lock);
	rc = mii_ethtool_sset(&np->mii, cmd);
	spin_unlock_irq(&np->lock);

	return rc;
}

static int netdev_nway_reset(struct net_device *dev)
{
	struct epic_private *np = netdev_priv(dev);
	return mii_nway_restart(&np->mii);
}

static u32 netdev_get_link(struct net_device *dev)
{
	struct epic_private *np = netdev_priv(dev);
	return mii_link_ok(&np->mii);
}

static u32 netdev_get_msglevel(struct net_device *dev)
{
	return debug;
}

static void netdev_set_msglevel(struct net_device *dev, u32 value)
{
	debug = value;
}

static int ethtool_begin(struct net_device *dev)
{
	unsigned long ioaddr = dev->base_addr;
	/* power-up, if interface is down */
	if (! netif_running(dev)) {
		outl(0x0200, ioaddr + GENCTL);
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);
	}
	return 0;
}

static void ethtool_complete(struct net_device *dev)
{
	unsigned long ioaddr = dev->base_addr;
	/* power-down, if interface is down */
	if (! netif_running(dev)) {
		outl(0x0008, ioaddr + GENCTL);
		outl((inl(ioaddr + NVCTL) & ~0x483C) | 0x0000, ioaddr + NVCTL);
	}
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_settings		= netdev_get_settings,
	.set_settings		= netdev_set_settings,
	.nway_reset		= netdev_nway_reset,
	.get_link		= netdev_get_link,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
	.begin			= ethtool_begin,
	.complete		= ethtool_complete
};

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct epic_private *np = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	struct mii_ioctl_data *data = if_mii(rq);
	int rc;

	/* power-up, if interface is down */
	if (! netif_running(dev)) {
		outl(0x0200, ioaddr + GENCTL);
		outl((inl(ioaddr + NVCTL) & ~0x003C) | 0x4800, ioaddr + NVCTL);
	}

	/* all non-ethtool ioctls (the SIOC[GS]MIIxxx ioctls) */
	spin_lock_irq(&np->lock);
	rc = generic_mii_ioctl(&np->mii, data, cmd, NULL);
	spin_unlock_irq(&np->lock);

	/* power-down, if interface is down */
	if (! netif_running(dev)) {
		outl(0x0008, ioaddr + GENCTL);
		outl((inl(ioaddr + NVCTL) & ~0x483C) | 0x0000, ioaddr + NVCTL);
	}
	return rc;
}


static void __devexit epic_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct epic_private *ep = netdev_priv(dev);

	pci_free_consistent(pdev, TX_TOTAL_SIZE, ep->tx_ring, ep->tx_ring_dma);
	pci_free_consistent(pdev, RX_TOTAL_SIZE, ep->rx_ring, ep->rx_ring_dma);
	unregister_netdev(dev);
#ifndef USE_IO_OPS
	iounmap((void*) dev->base_addr);
#endif
	pci_release_regions(pdev);
	free_netdev(dev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	/* pci_power_off(pdev, -1); */
}


#ifdef CONFIG_PM

static int epic_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	long ioaddr = dev->base_addr;

	if (!netif_running(dev))
		return 0;
	epic_pause(dev);
	/* Put the chip into low-power mode. */
	outl(0x0008, ioaddr + GENCTL);
	/* pci_power_off(pdev, -1); */
	return 0;
}


static int epic_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (!netif_running(dev))
		return 0;
	epic_restart(dev);
	/* pci_power_on(pdev); */
	return 0;
}

#endif /* CONFIG_PM */


static struct pci_driver epic_driver = {
	.name		= DRV_NAME,
	.id_table	= epic_pci_tbl,
	.probe		= epic_init_one,
	.remove		= __devexit_p(epic_remove_one),
#ifdef CONFIG_PM
	.suspend	= epic_suspend,
	.resume		= epic_resume,
#endif /* CONFIG_PM */
};


static int __init epic_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk (KERN_INFO "%s%s",
		version, version2);
#endif

	return pci_register_driver(&epic_driver);
}


static void __exit epic_cleanup (void)
{
	pci_unregister_driver (&epic_driver);
}


module_init(epic_init);
module_exit(epic_cleanup);

/*
	Written 1998-2000 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support information and updates available at
	http://www.scyld.com/network/pci-skeleton.html

	Linux kernel updates:

	Version 2.51, Nov 17, 2001 (jgarzik):
	- Add ethtool support
	- Replace some MII-related magic numbers with constants

*/

#define DRV_NAME	"fealnx"
#define DRV_VERSION	"2.52"
#define DRV_RELDATE	"Sep-11-2006"

static int debug;		/* 1-> print debug message */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast). */
static int multicast_filter_limit = 32;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme. */
/* Setting to > 1518 effectively disables this feature.          */
static int rx_copybreak;

/* Used to pass the media type, etc.                            */
/* Both 'options[]' and 'full_duplex[]' should exist for driver */
/* interoperability.                                            */
/* The media type is usually passed in 'options[]'.             */
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int full_duplex[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };

/* Operational parameters that are set at compile time.                 */
/* Keep the ring sizes a power of two for compile efficiency.           */
/* The compiler will convert <unsigned>'%'<2^N> into a bit mask.        */
/* Making the Tx ring too large decreases the effectiveness of channel  */
/* bonding and packet priority.                                         */
/* There are no ill effects from too-large receive rings.               */
// 88-12-9 modify,
// #define TX_RING_SIZE    16
// #define RX_RING_SIZE    32
#define TX_RING_SIZE    6
#define RX_RING_SIZE    12
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct fealnx_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct fealnx_desc)

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT      (2*HZ)

#define PKT_BUF_SZ      1536	/* Size of each temporary Rx buffer. */


/* Include files, designed to support most kernel versions 2.0.0 and later. */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>

/* These identify the driver base version and may not be removed. */
static const char version[] =
	KERN_INFO DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE "\n";


/* This driver was written to use PCI memory space, however some x86 systems
   work only with I/O space accesses. */
#ifndef __alpha__
#define USE_IO_OPS
#endif

/* Kernel compatibility defines, some common to David Hinds' PCMCIA package. */
/* This is only in the support-all-kernels source code. */

#define RUN_AT(x) (jiffies + (x))

MODULE_AUTHOR("Myson or whoever");
MODULE_DESCRIPTION("Myson MTD-8xx 100/10M Ethernet PCI Adapter Driver");
MODULE_LICENSE("GPL");
module_param(max_interrupt_work, int, 0);
module_param(debug, int, 0);
module_param(rx_copybreak, int, 0);
module_param(multicast_filter_limit, int, 0);
module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);
MODULE_PARM_DESC(max_interrupt_work, "fealnx maximum events handled per interrupt");
MODULE_PARM_DESC(debug, "fealnx enable debugging (0-1)");
MODULE_PARM_DESC(rx_copybreak, "fealnx copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(multicast_filter_limit, "fealnx maximum number of filtered multicast addresses");
MODULE_PARM_DESC(options, "fealnx: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "fealnx full duplex setting(s) (1)");

enum {
	MIN_REGION_SIZE		= 136,
};

/* A chip capabilities table, matching the entries in pci_tbl[] above. */
enum chip_capability_flags {
	HAS_MII_XCVR,
	HAS_CHIP_XCVR,
};

/* 89/6/13 add, */
/* for different PHY */
enum phy_type_flags {
	MysonPHY = 1,
	AhdocPHY = 2,
	SeeqPHY = 3,
	MarvellPHY = 4,
	Myson981 = 5,
	LevelOnePHY = 6,
	OtherPHY = 10,
};

struct chip_info {
	char *chip_name;
	int flags;
};

static const struct chip_info skel_netdrv_tbl[] = {
 	{ "100/10M Ethernet PCI Adapter",	HAS_MII_XCVR },
	{ "100/10M Ethernet PCI Adapter",	HAS_CHIP_XCVR },
	{ "1000/100/10M Ethernet PCI Adapter",	HAS_MII_XCVR },
};

/* Offsets to the Command and Status Registers. */
enum fealnx_offsets {
	PAR0 = 0x0,		/* physical address 0-3 */
	PAR1 = 0x04,		/* physical address 4-5 */
	MAR0 = 0x08,		/* multicast address 0-3 */
	MAR1 = 0x0C,		/* multicast address 4-7 */
	FAR0 = 0x10,		/* flow-control address 0-3 */
	FAR1 = 0x14,		/* flow-control address 4-5 */
	TCRRCR = 0x18,		/* receive & transmit configuration */
	BCR = 0x1C,		/* bus command */
	TXPDR = 0x20,		/* transmit polling demand */
	RXPDR = 0x24,		/* receive polling demand */
	RXCWP = 0x28,		/* receive current word pointer */
	TXLBA = 0x2C,		/* transmit list base address */
	RXLBA = 0x30,		/* receive list base address */
	ISR = 0x34,		/* interrupt status */
	IMR = 0x38,		/* interrupt mask */
	FTH = 0x3C,		/* flow control high/low threshold */
	MANAGEMENT = 0x40,	/* bootrom/eeprom and mii management */
	TALLY = 0x44,		/* tally counters for crc and mpa */
	TSR = 0x48,		/* tally counter for transmit status */
	BMCRSR = 0x4c,		/* basic mode control and status */
	PHYIDENTIFIER = 0x50,	/* phy identifier */
	ANARANLPAR = 0x54,	/* auto-negotiation advertisement and link
				   partner ability */
	ANEROCR = 0x58,		/* auto-negotiation expansion and pci conf. */
	BPREMRPSR = 0x5c,	/* bypass & receive error mask and phy status */
};

/* Bits in the interrupt status/enable registers. */
/* The bits in the Intr Status/Enable registers, mostly interrupt sources. */
enum intr_status_bits {
	RFCON = 0x00020000,	/* receive flow control xon packet */
	RFCOFF = 0x00010000,	/* receive flow control xoff packet */
	LSCStatus = 0x00008000,	/* link status change */
	ANCStatus = 0x00004000,	/* autonegotiation completed */
	FBE = 0x00002000,	/* fatal bus error */
	FBEMask = 0x00001800,	/* mask bit12-11 */
	ParityErr = 0x00000000,	/* parity error */
	TargetErr = 0x00001000,	/* target abort */
	MasterErr = 0x00000800,	/* master error */
	TUNF = 0x00000400,	/* transmit underflow */
	ROVF = 0x00000200,	/* receive overflow */
	ETI = 0x00000100,	/* transmit early int */
	ERI = 0x00000080,	/* receive early int */
	CNTOVF = 0x00000040,	/* counter overflow */
	RBU = 0x00000020,	/* receive buffer unavailable */
	TBU = 0x00000010,	/* transmit buffer unavilable */
	TI = 0x00000008,	/* transmit interrupt */
	RI = 0x00000004,	/* receive interrupt */
	RxErr = 0x00000002,	/* receive error */
};

/* Bits in the NetworkConfig register, W for writing, R for reading */
/* FIXME: some names are invented by me. Marked with (name?) */
/* If you have docs and know bit names, please fix 'em */
enum rx_mode_bits {
	CR_W_ENH	= 0x02000000,	/* enhanced mode (name?) */
	CR_W_FD		= 0x00100000,	/* full duplex */
	CR_W_PS10	= 0x00080000,	/* 10 mbit */
	CR_W_TXEN	= 0x00040000,	/* tx enable (name?) */
	CR_W_PS1000	= 0x00010000,	/* 1000 mbit */
     /* CR_W_RXBURSTMASK= 0x00000e00, Im unsure about this */
	CR_W_RXMODEMASK	= 0x000000e0,
	CR_W_PROM	= 0x00000080,	/* promiscuous mode */
	CR_W_AB		= 0x00000040,	/* accept broadcast */
	CR_W_AM		= 0x00000020,	/* accept mutlicast */
	CR_W_ARP	= 0x00000008,	/* receive runt pkt */
	CR_W_ALP	= 0x00000004,	/* receive long pkt */
	CR_W_SEP	= 0x00000002,	/* receive error pkt */
	CR_W_RXEN	= 0x00000001,	/* rx enable (unicast?) (name?) */

	CR_R_TXSTOP	= 0x04000000,	/* tx stopped (name?) */
	CR_R_FD		= 0x00100000,	/* full duplex detected */
	CR_R_PS10	= 0x00080000,	/* 10 mbit detected */
	CR_R_RXSTOP	= 0x00008000,	/* rx stopped (name?) */
};

/* The Tulip Rx and Tx buffer descriptors. */
struct fealnx_desc {
	s32 status;
	s32 control;
	u32 buffer;
	u32 next_desc;
	struct fealnx_desc *next_desc_logical;
	struct sk_buff *skbuff;
	u32 reserved1;
	u32 reserved2;
};

/* Bits in network_desc.status */
enum rx_desc_status_bits {
	RXOWN = 0x80000000,	/* own bit */
	FLNGMASK = 0x0fff0000,	/* frame length */
	FLNGShift = 16,
	MARSTATUS = 0x00004000,	/* multicast address received */
	BARSTATUS = 0x00002000,	/* broadcast address received */
	PHYSTATUS = 0x00001000,	/* physical address received */
	RXFSD = 0x00000800,	/* first descriptor */
	RXLSD = 0x00000400,	/* last descriptor */
	ErrorSummary = 0x80,	/* error summary */
	RUNT = 0x40,		/* runt packet received */
	LONG = 0x20,		/* long packet received */
	FAE = 0x10,		/* frame align error */
	CRC = 0x08,		/* crc error */
	RXER = 0x04,		/* receive error */
};

enum rx_desc_control_bits {
	RXIC = 0x00800000,	/* interrupt control */
	RBSShift = 0,
};

enum tx_desc_status_bits {
	TXOWN = 0x80000000,	/* own bit */
	JABTO = 0x00004000,	/* jabber timeout */
	CSL = 0x00002000,	/* carrier sense lost */
	LC = 0x00001000,	/* late collision */
	EC = 0x00000800,	/* excessive collision */
	UDF = 0x00000400,	/* fifo underflow */
	DFR = 0x00000200,	/* deferred */
	HF = 0x00000100,	/* heartbeat fail */
	NCRMask = 0x000000ff,	/* collision retry count */
	NCRShift = 0,
};

enum tx_desc_control_bits {
	TXIC = 0x80000000,	/* interrupt control */
	ETIControl = 0x40000000,	/* early transmit interrupt */
	TXLD = 0x20000000,	/* last descriptor */
	TXFD = 0x10000000,	/* first descriptor */
	CRCEnable = 0x08000000,	/* crc control */
	PADEnable = 0x04000000,	/* padding control */
	RetryTxLC = 0x02000000,	/* retry late collision */
	PKTSMask = 0x3ff800,	/* packet size bit21-11 */
	PKTSShift = 11,
	TBSMask = 0x000007ff,	/* transmit buffer bit 10-0 */
	TBSShift = 0,
};

/* BootROM/EEPROM/MII Management Register */
#define MASK_MIIR_MII_READ       0x00000000
#define MASK_MIIR_MII_WRITE      0x00000008
#define MASK_MIIR_MII_MDO        0x00000004
#define MASK_MIIR_MII_MDI        0x00000002
#define MASK_MIIR_MII_MDC        0x00000001

/* ST+OP+PHYAD+REGAD+TA */
#define OP_READ             0x6000	/* ST:01+OP:10+PHYAD+REGAD+TA:Z0 */
#define OP_WRITE            0x5002	/* ST:01+OP:01+PHYAD+REGAD+TA:10 */

/* ------------------------------------------------------------------------- */
/*      Constants for Myson PHY                                              */
/* ------------------------------------------------------------------------- */
#define MysonPHYID      0xd0000302
/* 89-7-27 add, (begin) */
#define MysonPHYID0     0x0302
#define StatusRegister  18
#define SPEED100        0x0400	// bit10
#define FULLMODE        0x0800	// bit11
/* 89-7-27 add, (end) */

/* ------------------------------------------------------------------------- */
/*      Constants for Seeq 80225 PHY                                         */
/* ------------------------------------------------------------------------- */
#define SeeqPHYID0      0x0016

#define MIIRegister18   18
#define SPD_DET_100     0x80
#define DPLX_DET_FULL   0x40

/* ------------------------------------------------------------------------- */
/*      Constants for Ahdoc 101 PHY                                          */
/* ------------------------------------------------------------------------- */
#define AhdocPHYID0     0x0022

#define DiagnosticReg   18
#define DPLX_FULL       0x0800
#define Speed_100       0x0400

/* 89/6/13 add, */
/* -------------------------------------------------------------------------- */
/*      Constants                                                             */
/* -------------------------------------------------------------------------- */
#define MarvellPHYID0           0x0141
#define LevelOnePHYID0		0x0013

#define MII1000BaseTControlReg  9
#define MII1000BaseTStatusReg   10
#define SpecificReg		17

/* for 1000BaseT Control Register */
#define PHYAbletoPerform1000FullDuplex  0x0200
#define PHYAbletoPerform1000HalfDuplex  0x0100
#define PHY1000AbilityMask              0x300

// for phy specific status register, marvell phy.
#define SpeedMask       0x0c000
#define Speed_1000M     0x08000
#define Speed_100M      0x4000
#define Speed_10M       0
#define Full_Duplex     0x2000

// 89/12/29 add, for phy specific status register, levelone phy, (begin)
#define LXT1000_100M    0x08000
#define LXT1000_1000M   0x0c000
#define LXT1000_Full    0x200
// 89/12/29 add, for phy specific status register, levelone phy, (end)

/* for 3-in-1 case, BMCRSR register */
#define LinkIsUp2	0x00040000

/* for PHY */
#define LinkIsUp        0x0004


struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct fealnx_desc *rx_ring;
	struct fealnx_desc *tx_ring;

	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;

	spinlock_t lock;

	/* Media monitoring timer. */
	struct timer_list timer;

	/* Reset timer */
	struct timer_list reset_timer;
	int reset_timer_armed;
	unsigned long crvalue_sv;
	unsigned long imrvalue_sv;

	/* Frequently used values: keep some adjacent for cache effect. */
	int flags;
	struct pci_dev *pci_dev;
	unsigned long crvalue;
	unsigned long bcrvalue;
	unsigned long imrvalue;
	struct fealnx_desc *cur_rx;
	struct fealnx_desc *lack_rxbuf;
	int really_rx_count;
	struct fealnx_desc *cur_tx;
	struct fealnx_desc *cur_tx_copy;
	int really_tx_count;
	int free_tx_count;
	unsigned int rx_buf_sz;	/* Based on MTU+slack. */

	/* These values are keep track of the transceiver/media in use. */
	unsigned int linkok;
	unsigned int line_speed;
	unsigned int duplexmode;
	unsigned int default_port:4;	/* Last dev->if_port value. */
	unsigned int PHYType;

	/* MII transceiver section. */
	int mii_cnt;		/* MII device addresses. */
	unsigned char phys[2];	/* MII device addresses. */
	struct mii_if_info mii;
	void __iomem *mem;
};


static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int netdev_open(struct net_device *dev);
static void getlinktype(struct net_device *dev);
static void getlinkstatus(struct net_device *dev);
static void netdev_timer(unsigned long data);
static void reset_timer(unsigned long data);
static void fealnx_tx_timeout(struct net_device *dev);
static void init_ring(struct net_device *dev);
static netdev_tx_t start_tx(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t intr_handler(int irq, void *dev_instance);
static int netdev_rx(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static void __set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static const struct ethtool_ops netdev_ethtool_ops;
static int netdev_close(struct net_device *dev);
static void reset_rx_descriptors(struct net_device *dev);
static void reset_tx_descriptors(struct net_device *dev);

static void stop_nic_rx(void __iomem *ioaddr, long crvalue)
{
	int delay = 0x1000;
	iowrite32(crvalue & ~(CR_W_RXEN), ioaddr + TCRRCR);
	while (--delay) {
		if ( (ioread32(ioaddr + TCRRCR) & CR_R_RXSTOP) == CR_R_RXSTOP)
			break;
	}
}


static void stop_nic_rxtx(void __iomem *ioaddr, long crvalue)
{
	int delay = 0x1000;
	iowrite32(crvalue & ~(CR_W_RXEN+CR_W_TXEN), ioaddr + TCRRCR);
	while (--delay) {
		if ( (ioread32(ioaddr + TCRRCR) & (CR_R_RXSTOP+CR_R_TXSTOP))
					    == (CR_R_RXSTOP+CR_R_TXSTOP) )
			break;
	}
}

static const struct net_device_ops netdev_ops = {
	.ndo_open		= netdev_open,
	.ndo_stop		= netdev_close,
	.ndo_start_xmit		= start_tx,
	.ndo_get_stats 		= get_stats,
	.ndo_set_rx_mode	= set_rx_mode,
	.ndo_do_ioctl		= mii_ioctl,
	.ndo_tx_timeout		= fealnx_tx_timeout,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int fealnx_init_one(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	struct netdev_private *np;
	int i, option, err, irq;
	static int card_idx = -1;
	char boardname[12];
	void __iomem *ioaddr;
	unsigned long len;
	unsigned int chip_id = ent->driver_data;
	struct net_device *dev;
	void *ring_space;
	dma_addr_t ring_dma;
#ifdef USE_IO_OPS
	int bar = 0;
#else
	int bar = 1;
#endif

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	card_idx++;
	sprintf(boardname, "fealnx%d", card_idx);

	option = card_idx < MAX_UNITS ? options[card_idx] : 0;

	i = pci_enable_device(pdev);
	if (i) return i;
	pci_set_master(pdev);

	len = pci_resource_len(pdev, bar);
	if (len < MIN_REGION_SIZE) {
		dev_err(&pdev->dev,
			   "region size %ld too small, aborting\n", len);
		return -ENODEV;
	}

	i = pci_request_regions(pdev, boardname);
	if (i)
		return i;

	irq = pdev->irq;

	ioaddr = pci_iomap(pdev, bar, len);
	if (!ioaddr) {
		err = -ENOMEM;
		goto err_out_res;
	}

	dev = alloc_etherdev(sizeof(struct netdev_private));
	if (!dev) {
		err = -ENOMEM;
		goto err_out_unmap;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);

	/* read ethernet id */
	for (i = 0; i < 6; ++i)
		dev->dev_addr[i] = ioread8(ioaddr + PAR0 + i);

	/* Reset the chip to erase previous misconfiguration. */
	iowrite32(0x00000001, ioaddr + BCR);

	/* Make certain the descriptor lists are aligned. */
	np = netdev_priv(dev);
	np->mem = ioaddr;
	spin_lock_init(&np->lock);
	np->pci_dev = pdev;
	np->flags = skel_netdrv_tbl[chip_id].flags;
	pci_set_drvdata(pdev, dev);
	np->mii.dev = dev;
	np->mii.mdio_read = mdio_read;
	np->mii.mdio_write = mdio_write;
	np->mii.phy_id_mask = 0x1f;
	np->mii.reg_num_mask = 0x1f;

	ring_space = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	if (!ring_space) {
		err = -ENOMEM;
		goto err_out_free_dev;
	}
	np->rx_ring = ring_space;
	np->rx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	if (!ring_space) {
		err = -ENOMEM;
		goto err_out_free_rx;
	}
	np->tx_ring = ring_space;
	np->tx_ring_dma = ring_dma;

	/* find the connected MII xcvrs */
	if (np->flags == HAS_MII_XCVR) {
		int phy, phy_idx = 0;

		for (phy = 1; phy < 32 && phy_idx < ARRAY_SIZE(np->phys);
			       phy++) {
			int mii_status = mdio_read(dev, phy, 1);

			if (mii_status != 0xffff && mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				dev_info(&pdev->dev,
				       "MII PHY found at address %d, status "
				       "0x%4.4x.\n", phy, mii_status);
				/* get phy type */
				{
					unsigned int data;

					data = mdio_read(dev, np->phys[0], 2);
					if (data == SeeqPHYID0)
						np->PHYType = SeeqPHY;
					else if (data == AhdocPHYID0)
						np->PHYType = AhdocPHY;
					else if (data == MarvellPHYID0)
						np->PHYType = MarvellPHY;
					else if (data == MysonPHYID0)
						np->PHYType = Myson981;
					else if (data == LevelOnePHYID0)
						np->PHYType = LevelOnePHY;
					else
						np->PHYType = OtherPHY;
				}
			}
		}

		np->mii_cnt = phy_idx;
		if (phy_idx == 0)
			dev_warn(&pdev->dev,
				"MII PHY not found -- this device may "
			       "not operate correctly.\n");
	} else {
		np->phys[0] = 32;
/* 89/6/23 add, (begin) */
		/* get phy type */
		if (ioread32(ioaddr + PHYIDENTIFIER) == MysonPHYID)
			np->PHYType = MysonPHY;
		else
			np->PHYType = OtherPHY;
	}
	np->mii.phy_id = np->phys[0];

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->mii.full_duplex = 1;
		np->default_port = option & 15;
	}

	if (card_idx < MAX_UNITS && full_duplex[card_idx] > 0)
		np->mii.full_duplex = full_duplex[card_idx];

	if (np->mii.full_duplex) {
		dev_info(&pdev->dev, "Media type forced to Full Duplex.\n");
/* 89/6/13 add, (begin) */
//      if (np->PHYType==MarvellPHY)
		if ((np->PHYType == MarvellPHY) || (np->PHYType == LevelOnePHY)) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], 9);
			data = (data & 0xfcff) | 0x0200;
			mdio_write(dev, np->phys[0], 9, data);
		}
/* 89/6/13 add, (end) */
		if (np->flags == HAS_MII_XCVR)
			mdio_write(dev, np->phys[0], MII_ADVERTISE, ADVERTISE_FULL);
		else
			iowrite32(ADVERTISE_FULL, ioaddr + ANARANLPAR);
		np->mii.force_media = 1;
	}

	dev->netdev_ops = &netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	err = register_netdev(dev);
	if (err)
		goto err_out_free_tx;

	printk(KERN_INFO "%s: %s at %p, %pM, IRQ %d.\n",
	       dev->name, skel_netdrv_tbl[chip_id].chip_name, ioaddr,
	       dev->dev_addr, irq);

	return 0;

err_out_free_tx:
	pci_free_consistent(pdev, TX_TOTAL_SIZE, np->tx_ring, np->tx_ring_dma);
err_out_free_rx:
	pci_free_consistent(pdev, RX_TOTAL_SIZE, np->rx_ring, np->rx_ring_dma);
err_out_free_dev:
	free_netdev(dev);
err_out_unmap:
	pci_iounmap(pdev, ioaddr);
err_out_res:
	pci_release_regions(pdev);
	return err;
}


static void fealnx_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct netdev_private *np = netdev_priv(dev);

		pci_free_consistent(pdev, TX_TOTAL_SIZE, np->tx_ring,
			np->tx_ring_dma);
		pci_free_consistent(pdev, RX_TOTAL_SIZE, np->rx_ring,
			np->rx_ring_dma);
		unregister_netdev(dev);
		pci_iounmap(pdev, np->mem);
		free_netdev(dev);
		pci_release_regions(pdev);
	} else
		printk(KERN_ERR "fealnx: remove for unknown device\n");
}


static ulong m80x_send_cmd_to_phy(void __iomem *miiport, int opcode, int phyad, int regad)
{
	ulong miir;
	int i;
	unsigned int mask, data;

	/* enable MII output */
	miir = (ulong) ioread32(miiport);
	miir &= 0xfffffff0;

	miir |= MASK_MIIR_MII_WRITE + MASK_MIIR_MII_MDO;

	/* send 32 1's preamble */
	for (i = 0; i < 32; i++) {
		/* low MDC; MDO is already high (miir) */
		miir &= ~MASK_MIIR_MII_MDC;
		iowrite32(miir, miiport);

		/* high MDC */
		miir |= MASK_MIIR_MII_MDC;
		iowrite32(miir, miiport);
	}

	/* calculate ST+OP+PHYAD+REGAD+TA */
	data = opcode | (phyad << 7) | (regad << 2);

	/* sent out */
	mask = 0x8000;
	while (mask) {
		/* low MDC, prepare MDO */
		miir &= ~(MASK_MIIR_MII_MDC + MASK_MIIR_MII_MDO);
		if (mask & data)
			miir |= MASK_MIIR_MII_MDO;

		iowrite32(miir, miiport);
		/* high MDC */
		miir |= MASK_MIIR_MII_MDC;
		iowrite32(miir, miiport);
		udelay(30);

		/* next */
		mask >>= 1;
		if (mask == 0x2 && opcode == OP_READ)
			miir &= ~MASK_MIIR_MII_WRITE;
	}
	return miir;
}


static int mdio_read(struct net_device *dev, int phyad, int regad)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *miiport = np->mem + MANAGEMENT;
	ulong miir;
	unsigned int mask, data;

	miir = m80x_send_cmd_to_phy(miiport, OP_READ, phyad, regad);

	/* read data */
	mask = 0x8000;
	data = 0;
	while (mask) {
		/* low MDC */
		miir &= ~MASK_MIIR_MII_MDC;
		iowrite32(miir, miiport);

		/* read MDI */
		miir = ioread32(miiport);
		if (miir & MASK_MIIR_MII_MDI)
			data |= mask;

		/* high MDC, and wait */
		miir |= MASK_MIIR_MII_MDC;
		iowrite32(miir, miiport);
		udelay(30);

		/* next */
		mask >>= 1;
	}

	/* low MDC */
	miir &= ~MASK_MIIR_MII_MDC;
	iowrite32(miir, miiport);

	return data & 0xffff;
}


static void mdio_write(struct net_device *dev, int phyad, int regad, int data)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *miiport = np->mem + MANAGEMENT;
	ulong miir;
	unsigned int mask;

	miir = m80x_send_cmd_to_phy(miiport, OP_WRITE, phyad, regad);

	/* write data */
	mask = 0x8000;
	while (mask) {
		/* low MDC, prepare MDO */
		miir &= ~(MASK_MIIR_MII_MDC + MASK_MIIR_MII_MDO);
		if (mask & data)
			miir |= MASK_MIIR_MII_MDO;
		iowrite32(miir, miiport);

		/* high MDC */
		miir |= MASK_MIIR_MII_MDC;
		iowrite32(miir, miiport);

		/* next */
		mask >>= 1;
	}

	/* low MDC */
	miir &= ~MASK_MIIR_MII_MDC;
	iowrite32(miir, miiport);
}


static int netdev_open(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	const int irq = np->pci_dev->irq;
	int rc, i;

	iowrite32(0x00000001, ioaddr + BCR);	/* Reset */

	rc = request_irq(irq, intr_handler, IRQF_SHARED, dev->name, dev);
	if (rc)
		return -EAGAIN;

	for (i = 0; i < 3; i++)
		iowrite16(((unsigned short*)dev->dev_addr)[i],
				ioaddr + PAR0 + i*2);

	init_ring(dev);

	iowrite32(np->rx_ring_dma, ioaddr + RXLBA);
	iowrite32(np->tx_ring_dma, ioaddr + TXLBA);

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds.
	   486: Set 8 longword burst.
	   586: no burst limit.
	   Burst length 5:3
	   0 0 0   1
	   0 0 1   4
	   0 1 0   8
	   0 1 1   16
	   1 0 0   32
	   1 0 1   64
	   1 1 0   128
	   1 1 1   256
	   Wait the specified 50 PCI cycles after a reset by initializing
	   Tx and Rx queues and the address filter list.
	   FIXME (Ueimor): optimistic for alpha + posted writes ? */

	np->bcrvalue = 0x10;	/* little-endian, 8 burst length */
#ifdef __BIG_ENDIAN
	np->bcrvalue |= 0x04;	/* big-endian */
#endif

#if defined(__i386__) && !defined(MODULE)
	if (boot_cpu_data.x86 <= 4)
		np->crvalue = 0xa00;
	else
#endif
		np->crvalue = 0xe00;	/* rx 128 burst length */


// 89/12/29 add,
// 90/1/16 modify,
//   np->imrvalue=FBE|TUNF|CNTOVF|RBU|TI|RI;
	np->imrvalue = TUNF | CNTOVF | RBU | TI | RI;
	if (np->pci_dev->device == 0x891) {
		np->bcrvalue |= 0x200;	/* set PROG bit */
		np->crvalue |= CR_W_ENH;	/* set enhanced bit */
		np->imrvalue |= ETI;
	}
	iowrite32(np->bcrvalue, ioaddr + BCR);

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	iowrite32(0, ioaddr + RXPDR);
// 89/9/1 modify,
//   np->crvalue = 0x00e40001;    /* tx store and forward, tx/rx enable */
	np->crvalue |= 0x00e40001;	/* tx store and forward, tx/rx enable */
	np->mii.full_duplex = np->mii.force_media;
	getlinkstatus(dev);
	if (np->linkok)
		getlinktype(dev);
	__set_rx_mode(dev);

	netif_start_queue(dev);

	/* Clear and Enable interrupts by setting the interrupt mask. */
	iowrite32(FBE | TUNF | CNTOVF | RBU | TI | RI, ioaddr + ISR);
	iowrite32(np->imrvalue, ioaddr + IMR);

	if (debug)
		printk(KERN_DEBUG "%s: Done netdev_open().\n", dev->name);

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = RUN_AT(3 * HZ);
	np->timer.data = (unsigned long) dev;
	np->timer.function = netdev_timer;

	/* timer handler */
	add_timer(&np->timer);

	init_timer(&np->reset_timer);
	np->reset_timer.data = (unsigned long) dev;
	np->reset_timer.function = reset_timer;
	np->reset_timer_armed = 0;
	return rc;
}


static void getlinkstatus(struct net_device *dev)
/* function: Routine will read MII Status Register to get link status.       */
/* input   : dev... pointer to the adapter block.                            */
/* output  : none.                                                           */
{
	struct netdev_private *np = netdev_priv(dev);
	unsigned int i, DelayTime = 0x1000;

	np->linkok = 0;

	if (np->PHYType == MysonPHY) {
		for (i = 0; i < DelayTime; ++i) {
			if (ioread32(np->mem + BMCRSR) & LinkIsUp2) {
				np->linkok = 1;
				return;
			}
			udelay(100);
		}
	} else {
		for (i = 0; i < DelayTime; ++i) {
			if (mdio_read(dev, np->phys[0], MII_BMSR) & BMSR_LSTATUS) {
				np->linkok = 1;
				return;
			}
			udelay(100);
		}
	}
}


static void getlinktype(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);

	if (np->PHYType == MysonPHY) {	/* 3-in-1 case */
		if (ioread32(np->mem + TCRRCR) & CR_R_FD)
			np->duplexmode = 2;	/* full duplex */
		else
			np->duplexmode = 1;	/* half duplex */
		if (ioread32(np->mem + TCRRCR) & CR_R_PS10)
			np->line_speed = 1;	/* 10M */
		else
			np->line_speed = 2;	/* 100M */
	} else {
		if (np->PHYType == SeeqPHY) {	/* this PHY is SEEQ 80225 */
			unsigned int data;

			data = mdio_read(dev, np->phys[0], MIIRegister18);
			if (data & SPD_DET_100)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
			if (data & DPLX_DET_FULL)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
		} else if (np->PHYType == AhdocPHY) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], DiagnosticReg);
			if (data & Speed_100)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
			if (data & DPLX_FULL)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
		}
/* 89/6/13 add, (begin) */
		else if (np->PHYType == MarvellPHY) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], SpecificReg);
			if (data & Full_Duplex)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
			data &= SpeedMask;
			if (data == Speed_1000M)
				np->line_speed = 3;	/* 1000M */
			else if (data == Speed_100M)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
		}
/* 89/6/13 add, (end) */
/* 89/7/27 add, (begin) */
		else if (np->PHYType == Myson981) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], StatusRegister);

			if (data & SPEED100)
				np->line_speed = 2;
			else
				np->line_speed = 1;

			if (data & FULLMODE)
				np->duplexmode = 2;
			else
				np->duplexmode = 1;
		}
/* 89/7/27 add, (end) */
/* 89/12/29 add */
		else if (np->PHYType == LevelOnePHY) {
			unsigned int data;

			data = mdio_read(dev, np->phys[0], SpecificReg);
			if (data & LXT1000_Full)
				np->duplexmode = 2;	/* full duplex mode */
			else
				np->duplexmode = 1;	/* half duplex mode */
			data &= SpeedMask;
			if (data == LXT1000_1000M)
				np->line_speed = 3;	/* 1000M */
			else if (data == LXT1000_100M)
				np->line_speed = 2;	/* 100M */
			else
				np->line_speed = 1;	/* 10M */
		}
		np->crvalue &= (~CR_W_PS10) & (~CR_W_FD) & (~CR_W_PS1000);
		if (np->line_speed == 1)
			np->crvalue |= CR_W_PS10;
		else if (np->line_speed == 3)
			np->crvalue |= CR_W_PS1000;
		if (np->duplexmode == 2)
			np->crvalue |= CR_W_FD;
	}
}


/* Take lock before calling this */
static void allocate_rx_buffers(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);

	/*  allocate skb for rx buffers */
	while (np->really_rx_count != RX_RING_SIZE) {
		struct sk_buff *skb;

		skb = netdev_alloc_skb(dev, np->rx_buf_sz);
		if (skb == NULL)
			break;	/* Better luck next round. */

		while (np->lack_rxbuf->skbuff)
			np->lack_rxbuf = np->lack_rxbuf->next_desc_logical;

		np->lack_rxbuf->skbuff = skb;
		np->lack_rxbuf->buffer = pci_map_single(np->pci_dev, skb->data,
			np->rx_buf_sz, PCI_DMA_FROMDEVICE);
		np->lack_rxbuf->status = RXOWN;
		++np->really_rx_count;
	}
}


static void netdev_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	int old_crvalue = np->crvalue;
	unsigned int old_linkok = np->linkok;
	unsigned long flags;

	if (debug)
		printk(KERN_DEBUG "%s: Media selection timer tick, status %8.8x "
		       "config %8.8x.\n", dev->name, ioread32(ioaddr + ISR),
		       ioread32(ioaddr + TCRRCR));

	spin_lock_irqsave(&np->lock, flags);

	if (np->flags == HAS_MII_XCVR) {
		getlinkstatus(dev);
		if ((old_linkok == 0) && (np->linkok == 1)) {	/* we need to detect the media type again */
			getlinktype(dev);
			if (np->crvalue != old_crvalue) {
				stop_nic_rxtx(ioaddr, np->crvalue);
				iowrite32(np->crvalue, ioaddr + TCRRCR);
			}
		}
	}

	allocate_rx_buffers(dev);

	spin_unlock_irqrestore(&np->lock, flags);

	np->timer.expires = RUN_AT(10 * HZ);
	add_timer(&np->timer);
}


/* Take lock before calling */
/* Reset chip and disable rx, tx and interrupts */
static void reset_and_disable_rxtx(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	int delay=51;

	/* Reset the chip's Tx and Rx processes. */
	stop_nic_rxtx(ioaddr, 0);

	/* Disable interrupts by clearing the interrupt mask. */
	iowrite32(0, ioaddr + IMR);

	/* Reset the chip to erase previous misconfiguration. */
	iowrite32(0x00000001, ioaddr + BCR);

	/* Ueimor: wait for 50 PCI cycles (and flush posted writes btw).
	   We surely wait too long (address+data phase). Who cares? */
	while (--delay) {
		ioread32(ioaddr + BCR);
		rmb();
	}
}


/* Take lock before calling */
/* Restore chip after reset */
static void enable_rxtx(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;

	reset_rx_descriptors(dev);

	iowrite32(np->tx_ring_dma + ((char*)np->cur_tx - (char*)np->tx_ring),
		ioaddr + TXLBA);
	iowrite32(np->rx_ring_dma + ((char*)np->cur_rx - (char*)np->rx_ring),
		ioaddr + RXLBA);

	iowrite32(np->bcrvalue, ioaddr + BCR);

	iowrite32(0, ioaddr + RXPDR);
	__set_rx_mode(dev); /* changes np->crvalue, writes it into TCRRCR */

	/* Clear and Enable interrupts by setting the interrupt mask. */
	iowrite32(FBE | TUNF | CNTOVF | RBU | TI | RI, ioaddr + ISR);
	iowrite32(np->imrvalue, ioaddr + IMR);

	iowrite32(0, ioaddr + TXPDR);
}


static void reset_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct netdev_private *np = netdev_priv(dev);
	unsigned long flags;

	printk(KERN_WARNING "%s: resetting tx and rx machinery\n", dev->name);

	spin_lock_irqsave(&np->lock, flags);
	np->crvalue = np->crvalue_sv;
	np->imrvalue = np->imrvalue_sv;

	reset_and_disable_rxtx(dev);
	/* works for me without this:
	reset_tx_descriptors(dev); */
	enable_rxtx(dev);
	netif_start_queue(dev); /* FIXME: or netif_wake_queue(dev); ? */

	np->reset_timer_armed = 0;

	spin_unlock_irqrestore(&np->lock, flags);
}


static void fealnx_tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	unsigned long flags;
	int i;

	printk(KERN_WARNING
	       "%s: Transmit timed out, status %8.8x, resetting...\n",
	       dev->name, ioread32(ioaddr + ISR));

	{
		printk(KERN_DEBUG "  Rx ring %p: ", np->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(KERN_CONT " %8.8x",
			       (unsigned int) np->rx_ring[i].status);
		printk(KERN_CONT "\n");
		printk(KERN_DEBUG "  Tx ring %p: ", np->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(KERN_CONT " %4.4x", np->tx_ring[i].status);
		printk(KERN_CONT "\n");
	}

	spin_lock_irqsave(&np->lock, flags);

	reset_and_disable_rxtx(dev);
	reset_tx_descriptors(dev);
	enable_rxtx(dev);

	spin_unlock_irqrestore(&np->lock, flags);

	netif_trans_update(dev); /* prevent tx timeout */
	dev->stats.tx_errors++;
	netif_wake_queue(dev); /* or .._start_.. ?? */
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	int i;

	/* initialize rx variables */
	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->cur_rx = &np->rx_ring[0];
	np->lack_rxbuf = np->rx_ring;
	np->really_rx_count = 0;

	/* initial rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].status = 0;
		np->rx_ring[i].control = np->rx_buf_sz << RBSShift;
		np->rx_ring[i].next_desc = np->rx_ring_dma +
			(i + 1)*sizeof(struct fealnx_desc);
		np->rx_ring[i].next_desc_logical = &np->rx_ring[i + 1];
		np->rx_ring[i].skbuff = NULL;
	}

	/* for the last rx descriptor */
	np->rx_ring[i - 1].next_desc = np->rx_ring_dma;
	np->rx_ring[i - 1].next_desc_logical = np->rx_ring;

	/* allocate skb for rx buffers */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = netdev_alloc_skb(dev, np->rx_buf_sz);

		if (skb == NULL) {
			np->lack_rxbuf = &np->rx_ring[i];
			break;
		}

		++np->really_rx_count;
		np->rx_ring[i].skbuff = skb;
		np->rx_ring[i].buffer = pci_map_single(np->pci_dev, skb->data,
			np->rx_buf_sz, PCI_DMA_FROMDEVICE);
		np->rx_ring[i].status = RXOWN;
		np->rx_ring[i].control |= RXIC;
	}

	/* initialize tx variables */
	np->cur_tx = &np->tx_ring[0];
	np->cur_tx_copy = &np->tx_ring[0];
	np->really_tx_count = 0;
	np->free_tx_count = TX_RING_SIZE;

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_ring[i].status = 0;
		/* do we need np->tx_ring[i].control = XXX; ?? */
		np->tx_ring[i].next_desc = np->tx_ring_dma +
			(i + 1)*sizeof(struct fealnx_desc);
		np->tx_ring[i].next_desc_logical = &np->tx_ring[i + 1];
		np->tx_ring[i].skbuff = NULL;
	}

	/* for the last tx descriptor */
	np->tx_ring[i - 1].next_desc = np->tx_ring_dma;
	np->tx_ring[i - 1].next_desc_logical = &np->tx_ring[0];
}


static netdev_tx_t start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&np->lock, flags);

	np->cur_tx_copy->skbuff = skb;

#define one_buffer
#define BPT 1022
#if defined(one_buffer)
	np->cur_tx_copy->buffer = pci_map_single(np->pci_dev, skb->data,
		skb->len, PCI_DMA_TODEVICE);
	np->cur_tx_copy->control = TXIC | TXLD | TXFD | CRCEnable | PADEnable;
	np->cur_tx_copy->control |= (skb->len << PKTSShift);	/* pkt size */
	np->cur_tx_copy->control |= (skb->len << TBSShift);	/* buffer size */
// 89/12/29 add,
	if (np->pci_dev->device == 0x891)
		np->cur_tx_copy->control |= ETIControl | RetryTxLC;
	np->cur_tx_copy->status = TXOWN;
	np->cur_tx_copy = np->cur_tx_copy->next_desc_logical;
	--np->free_tx_count;
#elif defined(two_buffer)
	if (skb->len > BPT) {
		struct fealnx_desc *next;

		/* for the first descriptor */
		np->cur_tx_copy->buffer = pci_map_single(np->pci_dev, skb->data,
			BPT, PCI_DMA_TODEVICE);
		np->cur_tx_copy->control = TXIC | TXFD | CRCEnable | PADEnable;
		np->cur_tx_copy->control |= (skb->len << PKTSShift);	/* pkt size */
		np->cur_tx_copy->control |= (BPT << TBSShift);	/* buffer size */

		/* for the last descriptor */
		next = np->cur_tx_copy->next_desc_logical;
		next->skbuff = skb;
		next->control = TXIC | TXLD | CRCEnable | PADEnable;
		next->control |= (skb->len << PKTSShift);	/* pkt size */
		next->control |= ((skb->len - BPT) << TBSShift);	/* buf size */
// 89/12/29 add,
		if (np->pci_dev->device == 0x891)
			np->cur_tx_copy->control |= ETIControl | RetryTxLC;
		next->buffer = pci_map_single(ep->pci_dev, skb->data + BPT,
                                skb->len - BPT, PCI_DMA_TODEVICE);

		next->status = TXOWN;
		np->cur_tx_copy->status = TXOWN;

		np->cur_tx_copy = next->next_desc_logical;
		np->free_tx_count -= 2;
	} else {
		np->cur_tx_copy->buffer = pci_map_single(np->pci_dev, skb->data,
			skb->len, PCI_DMA_TODEVICE);
		np->cur_tx_copy->control = TXIC | TXLD | TXFD | CRCEnable | PADEnable;
		np->cur_tx_copy->control |= (skb->len << PKTSShift);	/* pkt size */
		np->cur_tx_copy->control |= (skb->len << TBSShift);	/* buffer size */
// 89/12/29 add,
		if (np->pci_dev->device == 0x891)
			np->cur_tx_copy->control |= ETIControl | RetryTxLC;
		np->cur_tx_copy->status = TXOWN;
		np->cur_tx_copy = np->cur_tx_copy->next_desc_logical;
		--np->free_tx_count;
	}
#endif

	if (np->free_tx_count < 2)
		netif_stop_queue(dev);
	++np->really_tx_count;
	iowrite32(0, np->mem + TXPDR);

	spin_unlock_irqrestore(&np->lock, flags);
	return NETDEV_TX_OK;
}


/* Take lock before calling */
/* Chip probably hosed tx ring. Clean up. */
static void reset_tx_descriptors(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	struct fealnx_desc *cur;
	int i;

	/* initialize tx variables */
	np->cur_tx = &np->tx_ring[0];
	np->cur_tx_copy = &np->tx_ring[0];
	np->really_tx_count = 0;
	np->free_tx_count = TX_RING_SIZE;

	for (i = 0; i < TX_RING_SIZE; i++) {
		cur = &np->tx_ring[i];
		if (cur->skbuff) {
			pci_unmap_single(np->pci_dev, cur->buffer,
				cur->skbuff->len, PCI_DMA_TODEVICE);
			dev_kfree_skb_any(cur->skbuff);
			cur->skbuff = NULL;
		}
		cur->status = 0;
		cur->control = 0;	/* needed? */
		/* probably not needed. We do it for purely paranoid reasons */
		cur->next_desc = np->tx_ring_dma +
			(i + 1)*sizeof(struct fealnx_desc);
		cur->next_desc_logical = &np->tx_ring[i + 1];
	}
	/* for the last tx descriptor */
	np->tx_ring[TX_RING_SIZE - 1].next_desc = np->tx_ring_dma;
	np->tx_ring[TX_RING_SIZE - 1].next_desc_logical = &np->tx_ring[0];
}


/* Take lock and stop rx before calling this */
static void reset_rx_descriptors(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	struct fealnx_desc *cur = np->cur_rx;
	int i;

	allocate_rx_buffers(dev);

	for (i = 0; i < RX_RING_SIZE; i++) {
		if (cur->skbuff)
			cur->status = RXOWN;
		cur = cur->next_desc_logical;
	}

	iowrite32(np->rx_ring_dma + ((char*)np->cur_rx - (char*)np->rx_ring),
		np->mem + RXLBA);
}


/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t intr_handler(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	long boguscnt = max_interrupt_work;
	unsigned int num_tx = 0;
	int handled = 0;

	spin_lock(&np->lock);

	iowrite32(0, ioaddr + IMR);

	do {
		u32 intr_status = ioread32(ioaddr + ISR);

		/* Acknowledge all of the current interrupt sources ASAP. */
		iowrite32(intr_status, ioaddr + ISR);

		if (debug)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n", dev->name,
			       intr_status);

		if (!(intr_status & np->imrvalue))
			break;

		handled = 1;

// 90/1/16 delete,
//
//      if (intr_status & FBE)
//      {   /* fatal error */
//          stop_nic_tx(ioaddr, 0);
//          stop_nic_rx(ioaddr, 0);
//          break;
//      };

		if (intr_status & TUNF)
			iowrite32(0, ioaddr + TXPDR);

		if (intr_status & CNTOVF) {
			/* missed pkts */
			dev->stats.rx_missed_errors +=
				ioread32(ioaddr + TALLY) & 0x7fff;

			/* crc error */
			dev->stats.rx_crc_errors +=
			    (ioread32(ioaddr + TALLY) & 0x7fff0000) >> 16;
		}

		if (intr_status & (RI | RBU)) {
			if (intr_status & RI)
				netdev_rx(dev);
			else {
				stop_nic_rx(ioaddr, np->crvalue);
				reset_rx_descriptors(dev);
				iowrite32(np->crvalue, ioaddr + TCRRCR);
			}
		}

		while (np->really_tx_count) {
			long tx_status = np->cur_tx->status;
			long tx_control = np->cur_tx->control;

			if (!(tx_control & TXLD)) {	/* this pkt is combined by two tx descriptors */
				struct fealnx_desc *next;

				next = np->cur_tx->next_desc_logical;
				tx_status = next->status;
				tx_control = next->control;
			}

			if (tx_status & TXOWN)
				break;

			if (!(np->crvalue & CR_W_ENH)) {
				if (tx_status & (CSL | LC | EC | UDF | HF)) {
					dev->stats.tx_errors++;
					if (tx_status & EC)
						dev->stats.tx_aborted_errors++;
					if (tx_status & CSL)
						dev->stats.tx_carrier_errors++;
					if (tx_status & LC)
						dev->stats.tx_window_errors++;
					if (tx_status & UDF)
						dev->stats.tx_fifo_errors++;
					if ((tx_status & HF) && np->mii.full_duplex == 0)
						dev->stats.tx_heartbeat_errors++;

				} else {
					dev->stats.tx_bytes +=
					    ((tx_control & PKTSMask) >> PKTSShift);

					dev->stats.collisions +=
					    ((tx_status & NCRMask) >> NCRShift);
					dev->stats.tx_packets++;
				}
			} else {
				dev->stats.tx_bytes +=
				    ((tx_control & PKTSMask) >> PKTSShift);
				dev->stats.tx_packets++;
			}

			/* Free the original skb. */
			pci_unmap_single(np->pci_dev, np->cur_tx->buffer,
				np->cur_tx->skbuff->len, PCI_DMA_TODEVICE);
			dev_kfree_skb_irq(np->cur_tx->skbuff);
			np->cur_tx->skbuff = NULL;
			--np->really_tx_count;
			if (np->cur_tx->control & TXLD) {
				np->cur_tx = np->cur_tx->next_desc_logical;
				++np->free_tx_count;
			} else {
				np->cur_tx = np->cur_tx->next_desc_logical;
				np->cur_tx = np->cur_tx->next_desc_logical;
				np->free_tx_count += 2;
			}
			num_tx++;
		}		/* end of for loop */

		if (num_tx && np->free_tx_count >= 2)
			netif_wake_queue(dev);

		/* read transmit status for enhanced mode only */
		if (np->crvalue & CR_W_ENH) {
			long data;

			data = ioread32(ioaddr + TSR);
			dev->stats.tx_errors += (data & 0xff000000) >> 24;
			dev->stats.tx_aborted_errors +=
				(data & 0xff000000) >> 24;
			dev->stats.tx_window_errors +=
				(data & 0x00ff0000) >> 16;
			dev->stats.collisions += (data & 0x0000ffff);
		}

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
			       "status=0x%4.4x.\n", dev->name, intr_status);
			if (!np->reset_timer_armed) {
				np->reset_timer_armed = 1;
				np->reset_timer.expires = RUN_AT(HZ/2);
				add_timer(&np->reset_timer);
				stop_nic_rxtx(ioaddr, 0);
				netif_stop_queue(dev);
				/* or netif_tx_disable(dev); ?? */
				/* Prevent other paths from enabling tx,rx,intrs */
				np->crvalue_sv = np->crvalue;
				np->imrvalue_sv = np->imrvalue;
				np->crvalue &= ~(CR_W_TXEN | CR_W_RXEN); /* or simply = 0? */
				np->imrvalue = 0;
			}

			break;
		}
	} while (1);

	/* read the tally counters */
	/* missed pkts */
	dev->stats.rx_missed_errors += ioread32(ioaddr + TALLY) & 0x7fff;

	/* crc error */
	dev->stats.rx_crc_errors +=
		(ioread32(ioaddr + TALLY) & 0x7fff0000) >> 16;

	if (debug)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
		       dev->name, ioread32(ioaddr + ISR));

	iowrite32(np->imrvalue, ioaddr + IMR);

	spin_unlock(&np->lock);

	return IRQ_RETVAL(handled);
}


/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int netdev_rx(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (!(np->cur_rx->status & RXOWN) && np->cur_rx->skbuff) {
		s32 rx_status = np->cur_rx->status;

		if (np->really_rx_count == 0)
			break;

		if (debug)
			printk(KERN_DEBUG "  netdev_rx() status was %8.8x.\n", rx_status);

		if ((!((rx_status & RXFSD) && (rx_status & RXLSD))) ||
		    (rx_status & ErrorSummary)) {
			if (rx_status & ErrorSummary) {	/* there was a fatal error */
				if (debug)
					printk(KERN_DEBUG
					       "%s: Receive error, Rx status %8.8x.\n",
					       dev->name, rx_status);

				dev->stats.rx_errors++;	/* end of a packet. */
				if (rx_status & (LONG | RUNT))
					dev->stats.rx_length_errors++;
				if (rx_status & RXER)
					dev->stats.rx_frame_errors++;
				if (rx_status & CRC)
					dev->stats.rx_crc_errors++;
			} else {
				int need_to_reset = 0;
				int desno = 0;

				if (rx_status & RXFSD) {	/* this pkt is too long, over one rx buffer */
					struct fealnx_desc *cur;

					/* check this packet is received completely? */
					cur = np->cur_rx;
					while (desno <= np->really_rx_count) {
						++desno;
						if ((!(cur->status & RXOWN)) &&
						    (cur->status & RXLSD))
							break;
						/* goto next rx descriptor */
						cur = cur->next_desc_logical;
					}
					if (desno > np->really_rx_count)
						need_to_reset = 1;
				} else	/* RXLSD did not find, something error */
					need_to_reset = 1;

				if (need_to_reset == 0) {
					int i;

					dev->stats.rx_length_errors++;

					/* free all rx descriptors related this long pkt */
					for (i = 0; i < desno; ++i) {
						if (!np->cur_rx->skbuff) {
							printk(KERN_DEBUG
								"%s: I'm scared\n", dev->name);
							break;
						}
						np->cur_rx->status = RXOWN;
						np->cur_rx = np->cur_rx->next_desc_logical;
					}
					continue;
				} else {        /* rx error, need to reset this chip */
					stop_nic_rx(ioaddr, np->crvalue);
					reset_rx_descriptors(dev);
					iowrite32(np->crvalue, ioaddr + TCRRCR);
				}
				break;	/* exit the while loop */
			}
		} else {	/* this received pkt is ok */

			struct sk_buff *skb;
			/* Omit the four octet CRC from the length. */
			short pkt_len = ((rx_status & FLNGMASK) >> FLNGShift) - 4;

#ifndef final_version
			if (debug)
				printk(KERN_DEBUG "  netdev_rx() normal Rx pkt length %d"
				       " status %x.\n", pkt_len, rx_status);
#endif

			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak &&
			    (skb = netdev_alloc_skb(dev, pkt_len + 2)) != NULL) {
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				pci_dma_sync_single_for_cpu(np->pci_dev,
							    np->cur_rx->buffer,
							    np->rx_buf_sz,
							    PCI_DMA_FROMDEVICE);
				/* Call copy + cksum if available. */

#if ! defined(__alpha__)
				skb_copy_to_linear_data(skb,
					np->cur_rx->skbuff->data, pkt_len);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len),
					np->cur_rx->skbuff->data, pkt_len);
#endif
				pci_dma_sync_single_for_device(np->pci_dev,
							       np->cur_rx->buffer,
							       np->rx_buf_sz,
							       PCI_DMA_FROMDEVICE);
			} else {
				pci_unmap_single(np->pci_dev,
						 np->cur_rx->buffer,
						 np->rx_buf_sz,
						 PCI_DMA_FROMDEVICE);
				skb_put(skb = np->cur_rx->skbuff, pkt_len);
				np->cur_rx->skbuff = NULL;
				--np->really_rx_count;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;
		}

		np->cur_rx = np->cur_rx->next_desc_logical;
	}			/* end of while loop */

	/*  allocate skb for rx buffers */
	allocate_rx_buffers(dev);

	return 0;
}


static struct net_device_stats *get_stats(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;

	/* The chip only need report frame silently dropped. */
	if (netif_running(dev)) {
		dev->stats.rx_missed_errors +=
			ioread32(ioaddr + TALLY) & 0x7fff;
		dev->stats.rx_crc_errors +=
			(ioread32(ioaddr + TALLY) & 0x7fff0000) >> 16;
	}

	return &dev->stats;
}


/* for dev->set_multicast_list */
static void set_rx_mode(struct net_device *dev)
{
	spinlock_t *lp = &((struct netdev_private *)netdev_priv(dev))->lock;
	unsigned long flags;
	spin_lock_irqsave(lp, flags);
	__set_rx_mode(dev);
	spin_unlock_irqrestore(lp, flags);
}


/* Take lock before calling */
static void __set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	u32 mc_filter[2];	/* Multicast hash filter */
	u32 rx_mode;

	if (dev->flags & IFF_PROMISC) {	/* Set promiscuous. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = CR_W_PROM | CR_W_AB | CR_W_AM;
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		memset(mc_filter, 0xff, sizeof(mc_filter));
		rx_mode = CR_W_AB | CR_W_AM;
	} else {
		struct netdev_hw_addr *ha;

		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, dev) {
			unsigned int bit;
			bit = (ether_crc(ETH_ALEN, ha->addr) >> 26) ^ 0x3F;
			mc_filter[bit >> 5] |= (1 << bit);
		}
		rx_mode = CR_W_AB | CR_W_AM;
	}

	stop_nic_rxtx(ioaddr, np->crvalue);

	iowrite32(mc_filter[0], ioaddr + MAR0);
	iowrite32(mc_filter[1], ioaddr + MAR1);
	np->crvalue &= ~CR_W_RXMODEMASK;
	np->crvalue |= rx_mode;
	iowrite32(np->crvalue, ioaddr + TCRRCR);
}

static void netdev_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct netdev_private *np = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(np->pci_dev), sizeof(info->bus_info));
}

static int netdev_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct netdev_private *np = netdev_priv(dev);
	int rc;

	spin_lock_irq(&np->lock);
	rc = mii_ethtool_gset(&np->mii, cmd);
	spin_unlock_irq(&np->lock);

	return rc;
}

static int netdev_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct netdev_private *np = netdev_priv(dev);
	int rc;

	spin_lock_irq(&np->lock);
	rc = mii_ethtool_sset(&np->mii, cmd);
	spin_unlock_irq(&np->lock);

	return rc;
}

static int netdev_nway_reset(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	return mii_nway_restart(&np->mii);
}

static u32 netdev_get_link(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
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

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_settings		= netdev_get_settings,
	.set_settings		= netdev_set_settings,
	.nway_reset		= netdev_nway_reset,
	.get_link		= netdev_get_link,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
};

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = netdev_priv(dev);
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irq(&np->lock);
	rc = generic_mii_ioctl(&np->mii, if_mii(rq), cmd, NULL);
	spin_unlock_irq(&np->lock);

	return rc;
}


static int netdev_close(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->mem;
	int i;

	netif_stop_queue(dev);

	/* Disable interrupts by clearing the interrupt mask. */
	iowrite32(0x0000, ioaddr + IMR);

	/* Stop the chip's Tx and Rx processes. */
	stop_nic_rxtx(ioaddr, 0);

	del_timer_sync(&np->timer);
	del_timer_sync(&np->reset_timer);

	free_irq(np->pci_dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = np->rx_ring[i].skbuff;

		np->rx_ring[i].status = 0;
		if (skb) {
			pci_unmap_single(np->pci_dev, np->rx_ring[i].buffer,
				np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
			np->rx_ring[i].skbuff = NULL;
		}
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = np->tx_ring[i].skbuff;

		if (skb) {
			pci_unmap_single(np->pci_dev, np->tx_ring[i].buffer,
				skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
			np->tx_ring[i].skbuff = NULL;
		}
	}

	return 0;
}

static const struct pci_device_id fealnx_pci_tbl[] = {
	{0x1516, 0x0800, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1516, 0x0803, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{0x1516, 0x0891, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{} /* terminate list */
};
MODULE_DEVICE_TABLE(pci, fealnx_pci_tbl);


static struct pci_driver fealnx_driver = {
	.name		= "fealnx",
	.id_table	= fealnx_pci_tbl,
	.probe		= fealnx_init_one,
	.remove		= fealnx_remove_one,
};

static int __init fealnx_init(void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif

	return pci_register_driver(&fealnx_driver);
}

static void __exit fealnx_exit(void)
{
	pci_unregister_driver(&fealnx_driver);
}

module_init(fealnx_init);
module_exit(fealnx_exit);

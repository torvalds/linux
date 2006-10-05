/* xircom_tulip_cb.c: A Xircom CBE-100 ethernet driver for Linux. */
/*
	Written/copyright 1994-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

*/

#define DRV_NAME	"xircom_tulip_cb"
#define DRV_VERSION	"0.92"
#define DRV_RELDATE	"June 27, 2006"

/* A few user-configurable values. */

#define xircom_debug debug
#ifdef XIRCOM_DEBUG
static int xircom_debug = XIRCOM_DEBUG;
#else
static int xircom_debug = 1;
#endif

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 25;

#define MAX_UNITS 4
/* Used to pass the full-duplex flag, etc. */
static int full_duplex[MAX_UNITS];
static int options[MAX_UNITS];
static int mtu[MAX_UNITS];			/* Jumbo MTU for interfaces. */

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32

/* Set the copy breakpoint for the copy-only-tiny-buffer Rx structure. */
#ifdef __alpha__
static int rx_copybreak = 1518;
#else
static int rx_copybreak = 100;
#endif

/*
  Set the bus performance register.
	Typical: Set 16 longword cache alignment, no burst limit.
	Cache alignment bits 15:14	     Burst length 13:8
		0000	No alignment  0x00000000 unlimited		0800 8 longwords
		4000	8  longwords		0100 1 longword		1000 16 longwords
		8000	16 longwords		0200 2 longwords	2000 32 longwords
		C000	32  longwords		0400 4 longwords
	Warning: many older 486 systems are broken and require setting 0x00A04800
	   8 longword cache alignment, 8 longword burst.
	ToDo: Non-Intel setting could be better.
*/

#if defined(__alpha__) || defined(__ia64__) || defined(__x86_64__)
static int csr0 = 0x01A00000 | 0xE000;
#elif defined(__powerpc__)
static int csr0 = 0x01B00000 | 0x8000;
#elif defined(__sparc__)
static int csr0 = 0x01B00080 | 0x8000;
#elif defined(__i386__)
static int csr0 = 0x01A00000 | 0x8000;
#else
#warning Processor architecture undefined!
static int csr0 = 0x00A00000 | 0x4800;
#endif

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT		(4 * HZ)
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/
#define PKT_SETUP_SZ		192			/* Size of the setup frame */

/* PCI registers */
#define PCI_POWERMGMT 	0x40

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>

#include <asm/io.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/uaccess.h>


/* These identify the driver base version and may not be removed. */
static char version[] __devinitdata =
KERN_INFO DRV_NAME ".c derived from tulip.c:v0.91 4/14/99 becker@scyld.com\n"
KERN_INFO " unofficial 2.4.x kernel port, version " DRV_VERSION ", " DRV_RELDATE "\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Xircom CBE-100 ethernet driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

module_param(debug, int, 0);
module_param(max_interrupt_work, int, 0);
module_param(rx_copybreak, int, 0);
module_param(csr0, int, 0);

module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);

#define RUN_AT(x) (jiffies + (x))

/*
				Theory of Operation

I. Board Compatibility

This device driver was forked from the driver for the DECchip "Tulip",
Digital's single-chip ethernet controllers for PCI.  It supports Xircom's
almost-Tulip-compatible CBE-100 CardBus adapters.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS preferably should assign the
PCI INTA signal to an otherwise unused system IRQ line.

III. Driver operation

IIIa. Ring buffers

The Xircom can use either ring buffers or lists of Tx and Rx descriptors.
This driver uses statically allocated rings of Rx and Tx descriptors, set at
compile time by RX/TX_RING_SIZE.  This version of the driver allocates skbuffs
for the Rx ring buffers at open() time and passes the skb->data field to the
Xircom as receive data buffers.  When an incoming frame is less than
RX_COPYBREAK bytes long, a fresh skbuff is allocated and the frame is
copied to the new skbuff.  When the incoming frame is larger, the skbuff is
passed directly up the protocol stack and replaced by a newly allocated
skbuff.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  For small frames the copying cost is negligible (esp. considering
that we are pre-loading the cache with immediately useful header
information).  For large frames the copying cost is non-trivial, and the
larger copy might flush the cache of useful data.  A subtle aspect of this
choice is that the Xircom only receives into longword aligned buffers, thus
the IP header at offset 14 isn't longword aligned for further processing.
Copied frames are put into the new skbuff at an offset of "+2", thus copying
has the beneficial effect of aligning the IP header and preloading the
cache.

IIIC. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'tp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  (The Tx-done interrupt can't be selectively turned off, so
we can't avoid the interrupt overhead by having the Tx routine reap the Tx
stats.)	 After reaping the stats, it marks the queue entry as empty by setting
the 'base' to zero.	 Iff the 'tp->tx_full' flag is set, it clears both the
tx_full and tbusy flags.

IV. Notes

IVb. References

http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://www.digital.com  (search for current 21*4* datasheets and "21X4 SROM")
http://www.national.com/pf/DP/DP83840A.html

IVc. Errata

*/

/* A full-duplex map for media types. */
enum MediaIs {
	MediaIsFD = 1, MediaAlwaysFD=2, MediaIsMII=4, MediaIsFx=8,
	MediaIs100=16};
static const char media_cap[] =
{0,0,0,16,  3,19,16,24,  27,4,7,5, 0,20,23,20 };

/* Offsets to the Command and Status Registers, "CSRs".  All accesses
   must be longword instructions and quadword aligned. */
enum xircom_offsets {
	CSR0=0,    CSR1=0x08, CSR2=0x10, CSR3=0x18, CSR4=0x20, CSR5=0x28,
	CSR6=0x30, CSR7=0x38, CSR8=0x40, CSR9=0x48, CSR10=0x50, CSR11=0x58,
	CSR12=0x60, CSR13=0x68, CSR14=0x70, CSR15=0x78, CSR16=0x04, };

/* The bits in the CSR5 status registers, mostly interrupt sources. */
enum status_bits {
	LinkChange=0x08000000,
	NormalIntr=0x10000, NormalIntrMask=0x00014045,
	AbnormalIntr=0x8000, AbnormalIntrMask=0x0a00a5a2,
	ReservedIntrMask=0xe0001a18,
	EarlyRxIntr=0x4000, BusErrorIntr=0x2000,
	EarlyTxIntr=0x400, RxDied=0x100, RxNoBuf=0x80, RxIntr=0x40,
	TxFIFOUnderflow=0x20, TxNoBuf=0x04, TxDied=0x02, TxIntr=0x01,
};

enum csr0_control_bits {
	EnableMWI=0x01000000, EnableMRL=0x00800000,
	EnableMRM=0x00200000, EqualBusPrio=0x02,
	SoftwareReset=0x01,
};

enum csr6_control_bits {
	ReceiveAllBit=0x40000000, AllMultiBit=0x80, PromiscBit=0x40,
	HashFilterBit=0x01, FullDuplexBit=0x0200,
	TxThresh10=0x400000, TxStoreForw=0x200000,
	TxThreshMask=0xc000, TxThreshShift=14,
	EnableTx=0x2000, EnableRx=0x02,
	ReservedZeroMask=0x8d930134, ReservedOneMask=0x320c0000,
	EnableTxRx=(EnableTx | EnableRx),
};


enum tbl_flag {
	HAS_MII=1, HAS_ACPI=2,
};
static struct xircom_chip_table {
	char *chip_name;
	int valid_intrs;			/* CSR7 interrupt enable settings */
	int flags;
} xircom_tbl[] = {
  { "Xircom Cardbus Adapter",
	LinkChange | NormalIntr | AbnormalIntr | BusErrorIntr |
	RxDied | RxNoBuf | RxIntr | TxFIFOUnderflow | TxNoBuf | TxDied | TxIntr,
	HAS_MII | HAS_ACPI, },
  { NULL, },
};
/* This matches the table above. */
enum chips {
	X3201_3,
};


/* The Xircom Rx and Tx buffer descriptors. */
struct xircom_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;
};

struct xircom_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;				/* We use only buffer 1.  */
};

enum tx_desc0_status_bits {
	Tx0DescOwned=0x80000000, Tx0DescError=0x8000, Tx0NoCarrier=0x0800,
	Tx0LateColl=0x0200, Tx0ManyColl=0x0100, Tx0Underflow=0x02,
};
enum tx_desc1_status_bits {
	Tx1ComplIntr=0x80000000, Tx1LastSeg=0x40000000, Tx1FirstSeg=0x20000000,
	Tx1SetupPkt=0x08000000, Tx1DisableCRC=0x04000000, Tx1RingWrap=0x02000000,
	Tx1ChainDesc=0x01000000, Tx1NoPad=0x800000, Tx1HashSetup=0x400000,
	Tx1WholePkt=(Tx1FirstSeg | Tx1LastSeg),
};
enum rx_desc0_status_bits {
	Rx0DescOwned=0x80000000, Rx0DescError=0x8000, Rx0NoSpace=0x4000,
	Rx0Runt=0x0800, Rx0McastPkt=0x0400, Rx0FirstSeg=0x0200, Rx0LastSeg=0x0100,
	Rx0HugeFrame=0x80, Rx0CRCError=0x02,
	Rx0WholePkt=(Rx0FirstSeg | Rx0LastSeg),
};
enum rx_desc1_status_bits {
	Rx1RingWrap=0x02000000, Rx1ChainDesc=0x01000000,
};

struct xircom_private {
	struct xircom_rx_desc rx_ring[RX_RING_SIZE];
	struct xircom_tx_desc tx_ring[TX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];

	/* The X3201-3 requires 4-byte aligned tx bufs */
	struct sk_buff* tx_aligned_skbuff[TX_RING_SIZE];

	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	u16 setup_frame[PKT_SETUP_SZ / sizeof(u16)];	/* Pseudo-Tx frame to init address table. */
	int chip_id;
	struct net_device_stats stats;
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int speed100:1;
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int autoneg:1;
	unsigned int default_port:4;		/* Last dev->if_port value. */
	unsigned int open:1;
	unsigned int csr0;					/* CSR0 setting. */
	unsigned int csr6;					/* Current CSR6 control settings. */
	u16 to_advertise;					/* NWay capabilities advertised.  */
	u16 advertising[4];
	signed char phys[4], mii_cnt;		/* MII device addresses. */
	int saved_if_port;
	struct pci_dev *pdev;
	spinlock_t lock;
};

static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static void xircom_up(struct net_device *dev);
static void xircom_down(struct net_device *dev);
static int xircom_open(struct net_device *dev);
static void xircom_tx_timeout(struct net_device *dev);
static void xircom_init_ring(struct net_device *dev);
static int xircom_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int xircom_rx(struct net_device *dev);
static void xircom_media_change(struct net_device *dev);
static irqreturn_t xircom_interrupt(int irq, void *dev_instance);
static int xircom_close(struct net_device *dev);
static struct net_device_stats *xircom_get_stats(struct net_device *dev);
static int xircom_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void set_rx_mode(struct net_device *dev);
static void check_duplex(struct net_device *dev);
static const struct ethtool_ops ops;


/* The Xircom cards are picky about when certain bits in CSR6 can be
   manipulated.  Keith Owens <kaos@ocs.com.au>. */
static void outl_CSR6(u32 newcsr6, long ioaddr)
{
	const int strict_bits =
		TxThresh10 | TxStoreForw | TxThreshMask | EnableTxRx | FullDuplexBit;
    int csr5, csr5_22_20, csr5_19_17, currcsr6, attempts = 200;
    unsigned long flags;
    save_flags(flags);
    cli();
	/* mask out the reserved bits that always read 0 on the Xircom cards */
	newcsr6 &= ~ReservedZeroMask;
	/* or in the reserved bits that always read 1 */
	newcsr6 |= ReservedOneMask;
    currcsr6 = inl(ioaddr + CSR6);
    if (((newcsr6 & strict_bits) == (currcsr6 & strict_bits)) ||
	((currcsr6 & ~EnableTxRx) == 0)) {
		outl(newcsr6, ioaddr + CSR6);	/* safe */
		restore_flags(flags);
		return;
    }
    /* make sure the transmitter and receiver are stopped first */
    currcsr6 &= ~EnableTxRx;
    while (1) {
		csr5 = inl(ioaddr + CSR5);
		if (csr5 == 0xffffffff)
			break;  /* cannot read csr5, card removed? */
		csr5_22_20 = csr5 & 0x700000;
		csr5_19_17 = csr5 & 0x0e0000;
		if ((csr5_22_20 == 0 || csr5_22_20 == 0x600000) &&
			(csr5_19_17 == 0 || csr5_19_17 == 0x80000 || csr5_19_17 == 0xc0000))
			break;  /* both are stopped or suspended */
		if (!--attempts) {
			printk(KERN_INFO DRV_NAME ": outl_CSR6 too many attempts,"
				   "csr5=0x%08x\n", csr5);
			outl(newcsr6, ioaddr + CSR6);  /* unsafe but do it anyway */
			restore_flags(flags);
			return;
		}
		outl(currcsr6, ioaddr + CSR6);
		udelay(1);
    }
    /* now it is safe to change csr6 */
    outl(newcsr6, ioaddr + CSR6);
    restore_flags(flags);
}


static void __devinit read_mac_address(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	int i, j;
	unsigned char tuple, link, data_id, data_count;

	/* Xircom has its address stored in the CIS;
	 * we access it through the boot rom interface for now
	 * this might not work, as the CIS is not parsed but I
	 * (danilo) use the offset I found on my card's CIS !!!
	 *
	 * Doug Ledford: I changed this routine around so that it
	 * walks the CIS memory space, parsing the config items, and
	 * finds the proper lan_node_id tuple and uses the data
	 * stored there.
	 */
	outl(1 << 12, ioaddr + CSR9); /* enable boot rom access */
	for (i = 0x100; i < 0x1f7; i += link+2) {
		outl(i, ioaddr + CSR10);
		tuple = inl(ioaddr + CSR9) & 0xff;
		outl(i + 1, ioaddr + CSR10);
		link = inl(ioaddr + CSR9) & 0xff;
		outl(i + 2, ioaddr + CSR10);
		data_id = inl(ioaddr + CSR9) & 0xff;
		outl(i + 3, ioaddr + CSR10);
		data_count = inl(ioaddr + CSR9) & 0xff;
		if ( (tuple == 0x22) &&
			 (data_id == 0x04) && (data_count == 0x06) ) {
			/*
			 * This is it.  We have the data we want.
			 */
			for (j = 0; j < 6; j++) {
				outl(i + j + 4, ioaddr + CSR10);
				dev->dev_addr[j] = inl(ioaddr + CSR9) & 0xff;
			}
			break;
		} else if (link == 0) {
			break;
		}
	}
}


/*
 * locate the MII interfaces and initialize them.
 * we disable full-duplex modes here,
 * because we don't know how to handle them.
 */
static void find_mii_transceivers(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	int phy, phy_idx;

	if (media_cap[tp->default_port] & MediaIsMII) {
		u16 media2advert[] = { 0x20, 0x40, 0x03e0, 0x60, 0x80, 0x100, 0x200 };
		tp->to_advertise = media2advert[tp->default_port - 9];
	} else
		tp->to_advertise =
			/*ADVERTISE_100BASE4 | ADVERTISE_100FULL |*/ ADVERTISE_100HALF |
			/*ADVERTISE_10FULL |*/ ADVERTISE_10HALF | ADVERTISE_CSMA;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later,
	   but takes much time. */
	for (phy = 0, phy_idx = 0; phy < 32 && phy_idx < sizeof(tp->phys); phy++) {
		int mii_status = mdio_read(dev, phy, MII_BMSR);
		if ((mii_status & (BMSR_100BASE4 | BMSR_100HALF | BMSR_10HALF)) == BMSR_100BASE4 ||
			((mii_status & BMSR_100BASE4) == 0 &&
			 (mii_status & (BMSR_100FULL | BMSR_100HALF | BMSR_10FULL | BMSR_10HALF)) != 0)) {
			int mii_reg0 = mdio_read(dev, phy, MII_BMCR);
			int mii_advert = mdio_read(dev, phy, MII_ADVERTISE);
			int reg4 = ((mii_status >> 6) & tp->to_advertise) | ADVERTISE_CSMA;
			tp->phys[phy_idx] = phy;
			tp->advertising[phy_idx++] = reg4;
			printk(KERN_INFO "%s:  MII transceiver #%d "
				   "config %4.4x status %4.4x advertising %4.4x.\n",
				   dev->name, phy, mii_reg0, mii_status, mii_advert);
		}
	}
	tp->mii_cnt = phy_idx;
	if (phy_idx == 0) {
		printk(KERN_INFO "%s: ***WARNING***: No MII transceiver found!\n",
			   dev->name);
		tp->phys[0] = 0;
	}
}


/*
 * To quote Arjan van de Ven:
 *   transceiver_voodoo() enables the external UTP plug thingy.
 *   it's called voodoo as I stole this code and cannot cross-reference
 *   it with the specification.
 * Actually it seems to go like this:
 * - GPIO2 enables the MII itself so we can talk to it. The MII gets reset
 *   so any prior MII settings are lost.
 * - GPIO0 enables the TP port so the MII can talk to the network.
 * - a software reset will reset both GPIO pins.
 * I also moved the software reset here, because doing it in xircom_up()
 * required enabling the GPIO pins each time, which reset the MII each time.
 * Thus we couldn't control the MII -- which sucks because we don't know
 * how to handle full-duplex modes so we *must* disable them.
 */
static void transceiver_voodoo(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	long ioaddr = dev->base_addr;

	/* Reset the chip, holding bit 0 set at least 50 PCI cycles. */
	outl(SoftwareReset, ioaddr + CSR0);
	udelay(2);

	/* Deassert reset. */
	outl(tp->csr0, ioaddr + CSR0);

	/* Reset the xcvr interface and turn on heartbeat. */
	outl(0x0008, ioaddr + CSR15);
	udelay(5);  /* The delays are Xircom-recommended to give the
				 * chipset time to reset the actual hardware
				 * on the PCMCIA card
				 */
	outl(0xa8050000, ioaddr + CSR15);
	udelay(5);
	outl(0xa00f0000, ioaddr + CSR15);
	udelay(5);

	outl_CSR6(0, ioaddr);
	//outl_CSR6(FullDuplexBit, ioaddr);
}


static int __devinit xircom_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *dev;
	struct xircom_private *tp;
	static int board_idx = -1;
	int chip_idx = id->driver_data;
	long ioaddr;
	int i;
	u8 chip_rev;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	//printk(KERN_INFO "xircom_init_one(%s)\n", pci_name(pdev));

	board_idx++;

	if (pci_enable_device(pdev))
		return -ENODEV;

	pci_set_master(pdev);

	ioaddr = pci_resource_start(pdev, 0);
	dev = alloc_etherdev(sizeof(*tp));
	if (!dev) {
		printk (KERN_ERR DRV_NAME "%d: cannot alloc etherdev, aborting\n", board_idx);
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	dev->base_addr = ioaddr;
	dev->irq = pdev->irq;

	if (pci_request_regions(pdev, dev->name)) {
		printk (KERN_ERR DRV_NAME " %d: cannot reserve PCI resources, aborting\n", board_idx);
		goto err_out_free_netdev;
	}

	/* Bring the chip out of sleep mode.
	   Caution: Snooze mode does not work with some boards! */
	if (xircom_tbl[chip_idx].flags & HAS_ACPI)
		pci_write_config_dword(pdev, PCI_POWERMGMT, 0);

	/* Stop the chip's Tx and Rx processes. */
	outl_CSR6(inl(ioaddr + CSR6) & ~EnableTxRx, ioaddr);
	/* Clear the missed-packet counter. */
	(volatile int)inl(ioaddr + CSR8);

	tp = netdev_priv(dev);

	spin_lock_init(&tp->lock);
	tp->pdev = pdev;
	tp->chip_id = chip_idx;
	/* BugFixes: The 21143-TD hangs with PCI Write-and-Invalidate cycles. */
	/* XXX: is this necessary for Xircom? */
	tp->csr0 = csr0 & ~EnableMWI;

	pci_set_drvdata(pdev, dev);

	/* The lower four bits are the media type. */
	if (board_idx >= 0 && board_idx < MAX_UNITS) {
		tp->default_port = options[board_idx] & 15;
		if ((options[board_idx] & 0x90) || full_duplex[board_idx] > 0)
			tp->full_duplex = 1;
		if (mtu[board_idx] > 0)
			dev->mtu = mtu[board_idx];
	}
	if (dev->mem_start)
		tp->default_port = dev->mem_start;
	if (tp->default_port) {
		if (media_cap[tp->default_port] & MediaAlwaysFD)
			tp->full_duplex = 1;
	}
	if (tp->full_duplex)
		tp->autoneg = 0;
	else
		tp->autoneg = 1;
	tp->speed100 = 1;

	/* The Xircom-specific entries in the device structure. */
	dev->open = &xircom_open;
	dev->hard_start_xmit = &xircom_start_xmit;
	dev->stop = &xircom_close;
	dev->get_stats = &xircom_get_stats;
	dev->do_ioctl = &xircom_ioctl;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_rx_mode;
#endif
	dev->tx_timeout = xircom_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	SET_ETHTOOL_OPS(dev, &ops);

	transceiver_voodoo(dev);

	read_mac_address(dev);

	if (register_netdev(dev))
		goto err_out_cleardev;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &chip_rev);
	printk(KERN_INFO "%s: %s rev %d at %#3lx,",
	       dev->name, xircom_tbl[chip_idx].chip_name, chip_rev, ioaddr);
	for (i = 0; i < 6; i++)
		printk("%c%2.2X", i ? ':' : ' ', dev->dev_addr[i]);
	printk(", IRQ %d.\n", dev->irq);

	if (xircom_tbl[chip_idx].flags & HAS_MII) {
		find_mii_transceivers(dev);
		check_duplex(dev);
	}

	return 0;

err_out_cleardev:
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
err_out_free_netdev:
	free_netdev(dev);
	return -ENODEV;
}


/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.  See the MII specifications or DP83840A data sheet
   for details. */

/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues or future 66Mhz PCI. */
#define mdio_delay() inl(mdio_addr)

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK	0x10000
#define MDIO_DATA_WRITE0 0x00000
#define MDIO_DATA_WRITE1 0x20000
#define MDIO_ENB		0x00000		/* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ	0x80000

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	return (retval>>1) & 0xffff;
}


static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	int i;
	int cmd = (0x5002 << 16) | (phy_id << 23) | (location << 18) | value;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	return;
}


static void
xircom_up(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	int i;

	xircom_init_ring(dev);
	/* Clear the tx ring */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_skbuff[i] = NULL;
		tp->tx_ring[i].status = 0;
	}

	if (xircom_debug > 1)
		printk(KERN_DEBUG "%s: xircom_up() irq %d.\n", dev->name, dev->irq);

	outl(virt_to_bus(tp->rx_ring), ioaddr + CSR3);
	outl(virt_to_bus(tp->tx_ring), ioaddr + CSR4);

	tp->saved_if_port = dev->if_port;
	if (dev->if_port == 0)
		dev->if_port = tp->default_port;

	tp->csr6 = TxThresh10 /*| FullDuplexBit*/;						/* XXX: why 10 and not 100? */

	set_rx_mode(dev);

	/* Start the chip's Tx to process setup frame. */
	outl_CSR6(tp->csr6, ioaddr);
	outl_CSR6(tp->csr6 | EnableTx, ioaddr);

	/* Acknowledge all outstanding interrupts sources */
	outl(xircom_tbl[tp->chip_id].valid_intrs, ioaddr + CSR5);
	/* Enable interrupts by setting the interrupt mask. */
	outl(xircom_tbl[tp->chip_id].valid_intrs, ioaddr + CSR7);
	/* Enable Rx */
	outl_CSR6(tp->csr6 | EnableTxRx, ioaddr);
	/* Rx poll demand */
	outl(0, ioaddr + CSR2);

	/* Tell the net layer we're ready */
	netif_start_queue (dev);

	/* Check current media state */
	xircom_media_change(dev);

	if (xircom_debug > 2) {
		printk(KERN_DEBUG "%s: Done xircom_up(), CSR0 %8.8x, CSR5 %8.8x CSR6 %8.8x.\n",
			   dev->name, inl(ioaddr + CSR0), inl(ioaddr + CSR5),
			   inl(ioaddr + CSR6));
	}
}


static int
xircom_open(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);

	if (request_irq(dev->irq, &xircom_interrupt, IRQF_SHARED, dev->name, dev))
		return -EAGAIN;

	xircom_up(dev);
	tp->open = 1;

	return 0;
}


static void xircom_tx_timeout(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	long ioaddr = dev->base_addr;

	if (media_cap[dev->if_port] & MediaIsMII) {
		/* Do nothing -- the media monitor should handle this. */
		if (xircom_debug > 1)
			printk(KERN_WARNING "%s: Transmit timeout using MII device.\n",
				   dev->name);
	}

#if defined(way_too_many_messages)
	if (xircom_debug > 3) {
		int i;
		for (i = 0; i < RX_RING_SIZE; i++) {
			u8 *buf = (u8 *)(tp->rx_ring[i].buffer1);
			int j;
			printk(KERN_DEBUG "%2d: %8.8x %8.8x %8.8x %8.8x  "
				   "%2.2x %2.2x %2.2x.\n",
				   i, (unsigned int)tp->rx_ring[i].status,
				   (unsigned int)tp->rx_ring[i].length,
				   (unsigned int)tp->rx_ring[i].buffer1,
				   (unsigned int)tp->rx_ring[i].buffer2,
				   buf[0], buf[1], buf[2]);
			for (j = 0; buf[j] != 0xee && j < 1600; j++)
				if (j < 100) printk(" %2.2x", buf[j]);
			printk(" j=%d.\n", j);
		}
		printk(KERN_DEBUG "  Rx ring %8.8x: ", (int)tp->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)tp->rx_ring[i].status);
		printk("\n" KERN_DEBUG "  Tx ring %8.8x: ", (int)tp->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)tp->tx_ring[i].status);
		printk("\n");
	}
#endif

	/* Stop and restart the chip's Tx/Rx processes . */
	outl_CSR6(tp->csr6 | EnableRx, ioaddr);
	outl_CSR6(tp->csr6 | EnableTxRx, ioaddr);
	/* Trigger an immediate transmit demand. */
	outl(0, ioaddr + CSR1);

	dev->trans_start = jiffies;
	netif_wake_queue (dev);
	tp->stats.tx_errors++;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void xircom_init_ring(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	int i;

	tp->tx_full = 0;
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		tp->rx_ring[i].status = 0;
		tp->rx_ring[i].length = PKT_BUF_SZ;
		tp->rx_ring[i].buffer2 = virt_to_bus(&tp->rx_ring[i+1]);
		tp->rx_skbuff[i] = NULL;
	}
	/* Mark the last entry as wrapping the ring. */
	tp->rx_ring[i-1].length = PKT_BUF_SZ | Rx1RingWrap;
	tp->rx_ring[i-1].buffer2 = virt_to_bus(&tp->rx_ring[0]);

	for (i = 0; i < RX_RING_SIZE; i++) {
		/* Note the receive buffer must be longword aligned.
		   dev_alloc_skb() provides 16 byte alignment.  But do *not*
		   use skb_reserve() to align the IP header! */
		struct sk_buff *skb = dev_alloc_skb(PKT_BUF_SZ);
		tp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		tp->rx_ring[i].status = Rx0DescOwned;	/* Owned by Xircom chip */
		tp->rx_ring[i].buffer1 = virt_to_bus(skb->data);
	}
	tp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_skbuff[i] = NULL;
		tp->tx_ring[i].status = 0;
		tp->tx_ring[i].buffer2 = virt_to_bus(&tp->tx_ring[i+1]);
		if (tp->chip_id == X3201_3)
			tp->tx_aligned_skbuff[i] = dev_alloc_skb(PKT_BUF_SZ);
	}
	tp->tx_ring[i-1].buffer2 = virt_to_bus(&tp->tx_ring[0]);
}


static int
xircom_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	int entry;
	u32 flag;

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % TX_RING_SIZE;

	tp->tx_skbuff[entry] = skb;
	if (tp->chip_id == X3201_3) {
		memcpy(tp->tx_aligned_skbuff[entry]->data,skb->data,skb->len);
		tp->tx_ring[entry].buffer1 = virt_to_bus(tp->tx_aligned_skbuff[entry]->data);
	} else
		tp->tx_ring[entry].buffer1 = virt_to_bus(skb->data);

	if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE/2) {/* Typical path */
		flag = Tx1WholePkt; /* No interrupt */
	} else if (tp->cur_tx - tp->dirty_tx == TX_RING_SIZE/2) {
		flag = Tx1WholePkt | Tx1ComplIntr; /* Tx-done intr. */
	} else if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE - 2) {
		flag = Tx1WholePkt; /* No Tx-done intr. */
	} else {
		/* Leave room for set_rx_mode() to fill entries. */
		flag = Tx1WholePkt | Tx1ComplIntr; /* Tx-done intr. */
		tp->tx_full = 1;
	}
	if (entry == TX_RING_SIZE - 1)
		flag |= Tx1WholePkt | Tx1ComplIntr | Tx1RingWrap;

	tp->tx_ring[entry].length = skb->len | flag;
	tp->tx_ring[entry].status = Tx0DescOwned;	/* Pass ownership to the chip. */
	tp->cur_tx++;
	if (tp->tx_full)
		netif_stop_queue (dev);
	else
		netif_wake_queue (dev);

	/* Trigger an immediate transmit demand. */
	outl(0, dev->base_addr + CSR1);

	dev->trans_start = jiffies;

	return 0;
}


static void xircom_media_change(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	u16 reg0, reg1, reg4, reg5;
	u32 csr6 = inl(ioaddr + CSR6), newcsr6;

	/* reset status first */
	mdio_read(dev, tp->phys[0], MII_BMCR);
	mdio_read(dev, tp->phys[0], MII_BMSR);

	reg0 = mdio_read(dev, tp->phys[0], MII_BMCR);
	reg1 = mdio_read(dev, tp->phys[0], MII_BMSR);

	if (reg1 & BMSR_LSTATUS) {
		/* link is up */
		if (reg0 & BMCR_ANENABLE) {
			/* autonegotiation is enabled */
			reg4 = mdio_read(dev, tp->phys[0], MII_ADVERTISE);
			reg5 = mdio_read(dev, tp->phys[0], MII_LPA);
			if (reg4 & ADVERTISE_100FULL && reg5 & LPA_100FULL) {
				tp->speed100 = 1;
				tp->full_duplex = 1;
			} else if (reg4 & ADVERTISE_100HALF && reg5 & LPA_100HALF) {
				tp->speed100 = 1;
				tp->full_duplex = 0;
			} else if (reg4 & ADVERTISE_10FULL && reg5 & LPA_10FULL) {
				tp->speed100 = 0;
				tp->full_duplex = 1;
			} else {
				tp->speed100 = 0;
				tp->full_duplex = 0;
			}
		} else {
			/* autonegotiation is disabled */
			if (reg0 & BMCR_SPEED100)
				tp->speed100 = 1;
			else
				tp->speed100 = 0;
			if (reg0 & BMCR_FULLDPLX)
				tp->full_duplex = 1;
			else
				tp->full_duplex = 0;
		}
		printk(KERN_DEBUG "%s: Link is up, running at %sMbit %s-duplex\n",
		       dev->name,
		       tp->speed100 ? "100" : "10",
		       tp->full_duplex ? "full" : "half");
		netif_carrier_on(dev);
		newcsr6 = csr6 & ~FullDuplexBit;
		if (tp->full_duplex)
			newcsr6 |= FullDuplexBit;
		if (newcsr6 != csr6)
			outl_CSR6(newcsr6, ioaddr + CSR6);
	} else {
		printk(KERN_DEBUG "%s: Link is down\n", dev->name);
		netif_carrier_off(dev);
	}
}


static void check_duplex(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	u16 reg0;

	mdio_write(dev, tp->phys[0], MII_BMCR, BMCR_RESET);
	udelay(500);
	while (mdio_read(dev, tp->phys[0], MII_BMCR) & BMCR_RESET);

	reg0 = mdio_read(dev, tp->phys[0], MII_BMCR);
	mdio_write(dev, tp->phys[0], MII_ADVERTISE, tp->advertising[0]);

	if (tp->autoneg) {
		reg0 &= ~(BMCR_SPEED100 | BMCR_FULLDPLX);
		reg0 |= BMCR_ANENABLE | BMCR_ANRESTART;
	} else {
		reg0 &= ~(BMCR_ANENABLE | BMCR_ANRESTART);
		if (tp->speed100)
			reg0 |= BMCR_SPEED100;
		if (tp->full_duplex)
			reg0 |= BMCR_FULLDPLX;
		printk(KERN_DEBUG "%s: Link forced to %sMbit %s-duplex\n",
		       dev->name,
		       tp->speed100 ? "100" : "10",
		       tp->full_duplex ? "full" : "half");
	}
	mdio_write(dev, tp->phys[0], MII_BMCR, reg0);
}


/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t xircom_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct xircom_private *tp = netdev_priv(dev);
	long ioaddr = dev->base_addr;
	int csr5, work_budget = max_interrupt_work;
	int handled = 0;

	spin_lock (&tp->lock);

	do {
		csr5 = inl(ioaddr + CSR5);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outl(csr5 & 0x0001ffff, ioaddr + CSR5);

		if (xircom_debug > 4)
			printk(KERN_DEBUG "%s: interrupt  csr5=%#8.8x new csr5=%#8.8x.\n",
				   dev->name, csr5, inl(dev->base_addr + CSR5));

		if (csr5 == 0xffffffff)
			break;	/* all bits set, assume PCMCIA card removed */

		if ((csr5 & (NormalIntr|AbnormalIntr)) == 0)
			break;

		handled = 1;

		if (csr5 & (RxIntr | RxNoBuf))
			work_budget -= xircom_rx(dev);

		if (csr5 & (TxNoBuf | TxDied | TxIntr)) {
			unsigned int dirty_tx;

			for (dirty_tx = tp->dirty_tx; tp->cur_tx - dirty_tx > 0;
				 dirty_tx++) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status = tp->tx_ring[entry].status;

				if (status < 0)
					break;			/* It still hasn't been Txed */
				/* Check for Rx filter setup frames. */
				if (tp->tx_skbuff[entry] == NULL)
				  continue;

				if (status & Tx0DescError) {
					/* There was an major error, log it. */
#ifndef final_version
					if (xircom_debug > 1)
						printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
							   dev->name, status);
#endif
					tp->stats.tx_errors++;
					if (status & Tx0ManyColl) {
						tp->stats.tx_aborted_errors++;
					}
					if (status & Tx0NoCarrier) tp->stats.tx_carrier_errors++;
					if (status & Tx0LateColl) tp->stats.tx_window_errors++;
					if (status & Tx0Underflow) tp->stats.tx_fifo_errors++;
				} else {
					tp->stats.tx_bytes += tp->tx_ring[entry].length & 0x7ff;
					tp->stats.collisions += (status >> 3) & 15;
					tp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb_irq(tp->tx_skbuff[entry]);
				tp->tx_skbuff[entry] = NULL;
			}

#ifndef final_version
			if (tp->cur_tx - dirty_tx > TX_RING_SIZE) {
				printk(KERN_ERR "%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dev->name, dirty_tx, tp->cur_tx, tp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (tp->tx_full &&
			    tp->cur_tx - dirty_tx  < TX_RING_SIZE - 2)
				/* The ring is no longer full */
				tp->tx_full = 0;

			if (tp->tx_full)
				netif_stop_queue (dev);
			else
				netif_wake_queue (dev);

			tp->dirty_tx = dirty_tx;
			if (csr5 & TxDied) {
				if (xircom_debug > 2)
					printk(KERN_WARNING "%s: The transmitter stopped."
						   "  CSR5 is %x, CSR6 %x, new CSR6 %x.\n",
						   dev->name, csr5, inl(ioaddr + CSR6), tp->csr6);
				outl_CSR6(tp->csr6 | EnableRx, ioaddr);
				outl_CSR6(tp->csr6 | EnableTxRx, ioaddr);
			}
		}

		/* Log errors. */
		if (csr5 & AbnormalIntr) {	/* Abnormal error summary bit. */
			if (csr5 & LinkChange)
				xircom_media_change(dev);
			if (csr5 & TxFIFOUnderflow) {
				if ((tp->csr6 & TxThreshMask) != TxThreshMask)
					tp->csr6 += (1 << TxThreshShift);	/* Bump up the Tx threshold */
				else
					tp->csr6 |= TxStoreForw;  /* Store-n-forward. */
				/* Restart the transmit process. */
				outl_CSR6(tp->csr6 | EnableRx, ioaddr);
				outl_CSR6(tp->csr6 | EnableTxRx, ioaddr);
			}
			if (csr5 & RxDied) {		/* Missed a Rx frame. */
				tp->stats.rx_errors++;
				tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;
				outl_CSR6(tp->csr6 | EnableTxRx, ioaddr);
			}
			/* Clear all error sources, included undocumented ones! */
			outl(0x0800f7ba, ioaddr + CSR5);
		}
		if (--work_budget < 0) {
			if (xircom_debug > 1)
				printk(KERN_WARNING "%s: Too much work during an interrupt, "
					   "csr5=0x%8.8x.\n", dev->name, csr5);
			/* Acknowledge all interrupt sources. */
			outl(0x8001ffff, ioaddr + CSR5);
			break;
		}
	} while (1);

	if (xircom_debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, csr5=%#4.4x.\n",
			   dev->name, inl(ioaddr + CSR5));

	spin_unlock (&tp->lock);
	return IRQ_RETVAL(handled);
}


static int
xircom_rx(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	int entry = tp->cur_rx % RX_RING_SIZE;
	int rx_work_limit = tp->dirty_rx + RX_RING_SIZE - tp->cur_rx;
	int work_done = 0;

	if (xircom_debug > 4)
		printk(KERN_DEBUG " In xircom_rx(), entry %d %8.8x.\n", entry,
			   tp->rx_ring[entry].status);
	/* If we own the next entry, it's a new packet. Send it up. */
	while (tp->rx_ring[entry].status >= 0) {
		s32 status = tp->rx_ring[entry].status;

		if (xircom_debug > 5)
			printk(KERN_DEBUG " In xircom_rx(), entry %d %8.8x.\n", entry,
				   tp->rx_ring[entry].status);
		if (--rx_work_limit < 0)
			break;
		if ((status & 0x38008300) != 0x0300) {
			if ((status & 0x38000300) != 0x0300) {
				/* Ignore earlier buffers. */
				if ((status & 0xffff) != 0x7fff) {
					if (xircom_debug > 1)
						printk(KERN_WARNING "%s: Oversized Ethernet frame "
							   "spanned multiple buffers, status %8.8x!\n",
							   dev->name, status);
					tp->stats.rx_length_errors++;
				}
			} else if (status & Rx0DescError) {
				/* There was a fatal error. */
				if (xircom_debug > 2)
					printk(KERN_DEBUG "%s: Receive error, Rx status %8.8x.\n",
						   dev->name, status);
				tp->stats.rx_errors++; /* end of a packet.*/
				if (status & (Rx0Runt | Rx0HugeFrame)) tp->stats.rx_length_errors++;
				if (status & Rx0CRCError) tp->stats.rx_crc_errors++;
			}
		} else {
			/* Omit the four octet CRC from the length. */
			short pkt_len = ((status >> 16) & 0x7ff) - 4;
			struct sk_buff *skb;

#ifndef final_version
			if (pkt_len > 1518) {
				printk(KERN_WARNING "%s: Bogus packet size of %d (%#x).\n",
					   dev->name, pkt_len, pkt_len);
				pkt_len = 1518;
				tp->stats.rx_length_errors++;
			}
#endif
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if ! defined(__alpha__)
				eth_copy_and_sum(skb, bus_to_virt(tp->rx_ring[entry].buffer1),
								 pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(tp->rx_ring[entry].buffer1), pkt_len);
#endif
				work_done++;
			} else { 	/* Pass up the skb already on the Rx ring. */
				skb_put(skb = tp->rx_skbuff[entry], pkt_len);
				tp->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			tp->stats.rx_packets++;
			tp->stats.rx_bytes += pkt_len;
		}
		entry = (++tp->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; tp->cur_rx - tp->dirty_rx > 0; tp->dirty_rx++) {
		entry = tp->dirty_rx % RX_RING_SIZE;
		if (tp->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb;
			skb = tp->rx_skbuff[entry] = dev_alloc_skb(PKT_BUF_SZ);
			if (skb == NULL)
				break;
			skb->dev = dev;			/* Mark as being used by this device. */
			tp->rx_ring[entry].buffer1 = virt_to_bus(skb->data);
			work_done++;
		}
		tp->rx_ring[entry].status = Rx0DescOwned;
	}

	return work_done;
}


static void
xircom_down(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct xircom_private *tp = netdev_priv(dev);

	/* Disable interrupts by clearing the interrupt mask. */
	outl(0, ioaddr + CSR7);
	/* Stop the chip's Tx and Rx processes. */
	outl_CSR6(inl(ioaddr + CSR6) & ~EnableTxRx, ioaddr);

	if (inl(ioaddr + CSR6) != 0xffffffff)
		tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;

	dev->if_port = tp->saved_if_port;
}


static int
xircom_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct xircom_private *tp = netdev_priv(dev);
	int i;

	if (xircom_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, inl(ioaddr + CSR5));

	netif_stop_queue(dev);

	if (netif_device_present(dev))
		xircom_down(dev);

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = tp->rx_skbuff[i];
		tp->rx_skbuff[i] = NULL;
		tp->rx_ring[i].status = 0;		/* Not owned by Xircom chip. */
		tp->rx_ring[i].length = 0;
		tp->rx_ring[i].buffer1 = 0xBADF00D0; /* An invalid address. */
		if (skb) {
			dev_kfree_skb(skb);
		}
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (tp->tx_skbuff[i])
			dev_kfree_skb(tp->tx_skbuff[i]);
		tp->tx_skbuff[i] = NULL;
	}

	tp->open = 0;
	return 0;
}


static struct net_device_stats *xircom_get_stats(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	long ioaddr = dev->base_addr;

	if (netif_device_present(dev))
		tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;

	return &tp->stats;
}

static int xircom_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct xircom_private *tp = netdev_priv(dev);
	ecmd->supported =
			SUPPORTED_10baseT_Half |
			SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half |
			SUPPORTED_100baseT_Full |
			SUPPORTED_Autoneg |
			SUPPORTED_MII;

	ecmd->advertising = ADVERTISED_MII;
	if (tp->advertising[0] & ADVERTISE_10HALF)
		ecmd->advertising |= ADVERTISED_10baseT_Half;
	if (tp->advertising[0] & ADVERTISE_10FULL)
		ecmd->advertising |= ADVERTISED_10baseT_Full;
	if (tp->advertising[0] & ADVERTISE_100HALF)
		ecmd->advertising |= ADVERTISED_100baseT_Half;
	if (tp->advertising[0] & ADVERTISE_100FULL)
		ecmd->advertising |= ADVERTISED_100baseT_Full;
	if (tp->autoneg) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		ecmd->autoneg = AUTONEG_ENABLE;
	} else
		ecmd->autoneg = AUTONEG_DISABLE;

	ecmd->port = PORT_MII;
	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->phy_address = tp->phys[0];
	ecmd->speed = tp->speed100 ? SPEED_100 : SPEED_10;
	ecmd->duplex = tp->full_duplex ? DUPLEX_FULL : DUPLEX_HALF;
	ecmd->maxtxpkt = TX_RING_SIZE / 2;
	ecmd->maxrxpkt = 0;
	return 0;
}

static int xircom_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct xircom_private *tp = netdev_priv(dev);
	u16 autoneg, speed100, full_duplex;

	autoneg = (ecmd->autoneg == AUTONEG_ENABLE);
	speed100 = (ecmd->speed == SPEED_100);
	full_duplex = (ecmd->duplex == DUPLEX_FULL);

	tp->autoneg = autoneg;
	if (speed100 != tp->speed100 ||
	    full_duplex != tp->full_duplex) {
		tp->speed100 = speed100;
		tp->full_duplex = full_duplex;
		/* change advertising bits */
		tp->advertising[0] &= ~(ADVERTISE_10HALF |
				     ADVERTISE_10FULL |
				     ADVERTISE_100HALF |
				     ADVERTISE_100FULL |
				     ADVERTISE_100BASE4);
		if (speed100) {
			if (full_duplex)
				tp->advertising[0] |= ADVERTISE_100FULL;
			else
				tp->advertising[0] |= ADVERTISE_100HALF;
		} else {
			if (full_duplex)
				tp->advertising[0] |= ADVERTISE_10FULL;
			else
				tp->advertising[0] |= ADVERTISE_10HALF;
		}
	}
	check_duplex(dev);
	return 0;
}

static void xircom_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct xircom_private *tp = netdev_priv(dev);
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, pci_name(tp->pdev));
}

static const struct ethtool_ops ops = {
	.get_settings = xircom_get_settings,
	.set_settings = xircom_set_settings,
	.get_drvinfo = xircom_get_drvinfo,
};

/* Provide ioctl() calls to examine the MII xcvr state. */
static int xircom_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct xircom_private *tp = netdev_priv(dev);
	u16 *data = (u16 *)&rq->ifr_ifru;
	int phy = tp->phys[0] & 0x1f;
	unsigned long flags;

	switch(cmd) {
	/* Legacy mii-diag interface */
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		if (tp->mii_cnt)
			data[0] = phy;
		else
			return -ENODEV;
		return 0;
	case SIOCGMIIREG:		/* Read MII PHY register. */
		save_flags(flags);
		cli();
		data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
		restore_flags(flags);
		return 0;
	case SIOCSMIIREG:		/* Write MII PHY register. */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		save_flags(flags);
		cli();
		if (data[0] == tp->phys[0]) {
			u16 value = data[2];
			switch (data[1]) {
			case 0:
				if (value & (BMCR_RESET | BMCR_ANENABLE))
					/* Autonegotiation. */
					tp->autoneg = 1;
				else {
					tp->full_duplex = (value & BMCR_FULLDPLX) ? 1 : 0;
					tp->autoneg = 0;
				}
				break;
			case 4:
				tp->advertising[0] = value;
				break;
			}
			check_duplex(dev);
		}
		mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		restore_flags(flags);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling tp->setup_frame.  This is non-deterministic
   when re-entered but still correct. */
static void set_rx_mode(struct net_device *dev)
{
	struct xircom_private *tp = netdev_priv(dev);
	struct dev_mc_list *mclist;
	long ioaddr = dev->base_addr;
	int csr6 = inl(ioaddr + CSR6);
	u16 *eaddrs, *setup_frm;
	u32 tx_flags;
	int i;

	tp->csr6 &= ~(AllMultiBit | PromiscBit | HashFilterBit);
	csr6 &= ~(AllMultiBit | PromiscBit | HashFilterBit);
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		tp->csr6 |= PromiscBit;
		csr6 |= PromiscBit;
		goto out;
	}

	if ((dev->mc_count > 1000) || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter well -- accept all multicasts. */
		tp->csr6 |= AllMultiBit;
		csr6 |= AllMultiBit;
		goto out;
	}

	tx_flags = Tx1WholePkt | Tx1SetupPkt | PKT_SETUP_SZ;

	/* Note that only the low-address shortword of setup_frame is valid! */
	setup_frm = tp->setup_frame;
	mclist = dev->mc_list;

	/* Fill the first entry with our physical address. */
	eaddrs = (u16 *)dev->dev_addr;
	*setup_frm = cpu_to_le16(eaddrs[0]); setup_frm += 2;
	*setup_frm = cpu_to_le16(eaddrs[1]); setup_frm += 2;
	*setup_frm = cpu_to_le16(eaddrs[2]); setup_frm += 2;

	if (dev->mc_count > 14) { /* Must use a multicast hash table. */
		u32 *hash_table = (u32 *)(tp->setup_frame + 4 * 12);
		u32 hash, hash2;

		tx_flags |= Tx1HashSetup;
		tp->csr6 |= HashFilterBit;
		csr6 |= HashFilterBit;

		/* Fill the unused 3 entries with the broadcast address.
		   At least one entry *must* contain the broadcast address!!!*/
		for (i = 0; i < 3; i++) {
			*setup_frm = 0xffff; setup_frm += 2;
			*setup_frm = 0xffff; setup_frm += 2;
			*setup_frm = 0xffff; setup_frm += 2;
		}

		/* Truly brain-damaged hash filter layout */
		/* XXX: not sure if I should take the last or the first 9 bits */
		for (i = 0; i < dev->mc_count; i++, mclist = mclist->next) {
			u32 *hptr;
			hash = ether_crc(ETH_ALEN, mclist->dmi_addr) & 0x1ff;
			if (hash < 384) {
				hash2 = hash + ((hash >> 4) << 4) +
					((hash >> 5) << 5);
			} else {
				hash -= 384;
				hash2 = 64 + hash + (hash >> 4) * 80;
			}
			hptr = &hash_table[hash2 & ~0x1f];
			*hptr |= cpu_to_le32(1 << (hash2 & 0x1f));
		}
	} else {
		/* We have <= 14 mcast addresses so we can use Xircom's
		   wonderful 16-address perfect filter. */
		for (i = 0; i < dev->mc_count; i++, mclist = mclist->next) {
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_frm = cpu_to_le16(eaddrs[0]); setup_frm += 2;
			*setup_frm = cpu_to_le16(eaddrs[1]); setup_frm += 2;
			*setup_frm = cpu_to_le16(eaddrs[2]); setup_frm += 2;
		}
		/* Fill the unused entries with the broadcast address.
		   At least one entry *must* contain the broadcast address!!!*/
		for (; i < 15; i++) {
			*setup_frm = 0xffff; setup_frm += 2;
			*setup_frm = 0xffff; setup_frm += 2;
			*setup_frm = 0xffff; setup_frm += 2;
		}
	}

	/* Now add this frame to the Tx list. */
	if (tp->cur_tx - tp->dirty_tx > TX_RING_SIZE - 2) {
		/* Same setup recently queued, we need not add it. */
		/* XXX: Huh? All it means is that the Tx list is full...*/
	} else {
		unsigned long flags;
		unsigned int entry;
		int dummy = -1;

		save_flags(flags); cli();
		entry = tp->cur_tx++ % TX_RING_SIZE;

		if (entry != 0) {
			/* Avoid a chip errata by prefixing a dummy entry. */
			tp->tx_skbuff[entry] = NULL;
			tp->tx_ring[entry].length =
				(entry == TX_RING_SIZE - 1) ? Tx1RingWrap : 0;
			tp->tx_ring[entry].buffer1 = 0;
			/* race with chip, set Tx0DescOwned later */
			dummy = entry;
			entry = tp->cur_tx++ % TX_RING_SIZE;
		}

		tp->tx_skbuff[entry] = NULL;
		/* Put the setup frame on the Tx list. */
		if (entry == TX_RING_SIZE - 1)
			tx_flags |= Tx1RingWrap;		/* Wrap ring. */
		tp->tx_ring[entry].length = tx_flags;
		tp->tx_ring[entry].buffer1 = virt_to_bus(tp->setup_frame);
		tp->tx_ring[entry].status = Tx0DescOwned;
		if (tp->cur_tx - tp->dirty_tx >= TX_RING_SIZE - 2) {
			tp->tx_full = 1;
			netif_stop_queue (dev);
		}
		if (dummy >= 0)
			tp->tx_ring[dummy].status = Tx0DescOwned;
		restore_flags(flags);
		/* Trigger an immediate transmit demand. */
		outl(0, ioaddr + CSR1);
	}

out:
	outl_CSR6(csr6, ioaddr);
}


static struct pci_device_id xircom_pci_table[] = {
  { 0x115D, 0x0003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, X3201_3 },
  {0},
};
MODULE_DEVICE_TABLE(pci, xircom_pci_table);


#ifdef CONFIG_PM
static int xircom_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct xircom_private *tp = netdev_priv(dev);
	printk(KERN_INFO "xircom_suspend(%s)\n", dev->name);
	if (tp->open)
		xircom_down(dev);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, 3);

	return 0;
}


static int xircom_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct xircom_private *tp = netdev_priv(dev);
	printk(KERN_INFO "xircom_resume(%s)\n", dev->name);

	pci_set_power_state(pdev,0);
	pci_enable_device(pdev);
	pci_restore_state(pdev);

	/* Bring the chip out of sleep mode.
	   Caution: Snooze mode does not work with some boards! */
	if (xircom_tbl[tp->chip_id].flags & HAS_ACPI)
		pci_write_config_dword(tp->pdev, PCI_POWERMGMT, 0);

	transceiver_voodoo(dev);
	if (xircom_tbl[tp->chip_id].flags & HAS_MII)
		check_duplex(dev);

	if (tp->open)
		xircom_up(dev);
	return 0;
}
#endif /* CONFIG_PM */


static void __devexit xircom_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	printk(KERN_INFO "xircom_remove_one(%s)\n", dev->name);
	unregister_netdev(dev);
	pci_release_regions(pdev);
	free_netdev(dev);
	pci_set_drvdata(pdev, NULL);
}


static struct pci_driver xircom_driver = {
	.name		= DRV_NAME,
	.id_table	= xircom_pci_table,
	.probe		= xircom_init_one,
	.remove		= __devexit_p(xircom_remove_one),
#ifdef CONFIG_PM
	.suspend	= xircom_suspend,
	.resume		= xircom_resume
#endif /* CONFIG_PM */
};


static int __init xircom_init(void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif
	return pci_register_driver(&xircom_driver);
}


static void __exit xircom_exit(void)
{
	pci_unregister_driver(&xircom_driver);
}

module_init(xircom_init)
module_exit(xircom_exit)

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */

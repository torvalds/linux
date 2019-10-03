/* yellowfin.c: A Packet Engines G-NIC ethernet driver for linux. */
/*
	Written 1997-2001 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	This driver is for the Packet Engines G-NIC PCI Gigabit Ethernet adapter.
	It also supports the Symbios Logic version of the same chip core.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support and updates available at
	http://www.scyld.com/network/yellowfin.html
	[link no longer provides useful info -jgarzik]

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"yellowfin"
#define DRV_VERSION	"2.1"
#define DRV_RELDATE	"Sep 11, 2006"

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;
static int mtu;
#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
/* System-wide count of bogus-rx frames. */
static int bogus_rx;
static int dma_ctrl = 0x004A0263; 			/* Constrained by errata */
static int fifo_cfg = 0x0020;				/* Bypass external Tx FIFO. */
#elif defined(YF_NEW)					/* A future perfect board :->.  */
static int dma_ctrl = 0x00CAC277;			/* Override when loading module! */
static int fifo_cfg = 0x0028;
#else
static const int dma_ctrl = 0x004A0263; 			/* Constrained by errata */
static const int fifo_cfg = 0x0020;				/* Bypass external Tx FIFO. */
#endif

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1514 effectively disables this feature. */
static int rx_copybreak;

/* Used to pass the media type, etc.
   No media types are currently defined.  These exist for driver
   interoperability.
*/
#define MAX_UNITS 8				/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Do ugly workaround for GX server chipset errata. */
static int gx_fix;

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too long decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define TX_QUEUE_SIZE	12		/* Must be > 4 && <= TX_RING_SIZE */
#define RX_RING_SIZE	64
#define STATUS_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct tx_status_words)
#define TX_TOTAL_SIZE		2*TX_RING_SIZE*sizeof(struct yellowfin_desc)
#define RX_TOTAL_SIZE		RX_RING_SIZE*sizeof(struct yellowfin_desc)

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#define yellowfin_debug debug

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/unaligned.h>
#include <asm/io.h>

/* These identify the driver base version and may not be removed. */
static const char version[] =
  KERN_INFO DRV_NAME ".c:v1.05  1/09/2001  Written by Donald Becker <becker@scyld.com>\n"
  "  (unofficial 2.4.x port, " DRV_VERSION ", " DRV_RELDATE ")\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Packet Engines Yellowfin G-NIC Gigabit Ethernet driver");
MODULE_LICENSE("GPL");

module_param(max_interrupt_work, int, 0);
module_param(mtu, int, 0);
module_param(debug, int, 0);
module_param(rx_copybreak, int, 0);
module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);
module_param(gx_fix, int, 0);
MODULE_PARM_DESC(max_interrupt_work, "G-NIC maximum events handled per interrupt");
MODULE_PARM_DESC(mtu, "G-NIC MTU (all boards)");
MODULE_PARM_DESC(debug, "G-NIC debug level (0-7)");
MODULE_PARM_DESC(rx_copybreak, "G-NIC copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(options, "G-NIC: Bits 0-3: media type, bit 17: full duplex");
MODULE_PARM_DESC(full_duplex, "G-NIC full duplex setting(s) (1)");
MODULE_PARM_DESC(gx_fix, "G-NIC: enable GX server chipset bug workaround (0-1)");

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the Packet Engines "Yellowfin" Gigabit
Ethernet adapter.  The G-NIC 64-bit PCI card is supported, as well as the
Symbios 53C885E dual function chip.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS preferably should assign the
PCI INTA signal to an otherwise unused system IRQ line.
Note: Kernel versions earlier than 1.3.73 do not support shared PCI
interrupt lines.

III. Driver operation

IIIa. Ring buffers

The Yellowfin uses the Descriptor Based DMA Architecture specified by Apple.
This is a descriptor list scheme similar to that used by the EEPro100 and
Tulip.  This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.

The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the Yellowfin as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack and replaced by a newly allocated skbuff.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  For small frames the copying cost is negligible (esp. considering
that we are pre-loading the cache with immediately useful header
information).  For large frames the copying cost is non-trivial, and the
larger copy might flush the cache of useful data.

IIIC. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'yp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'yp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

Thanks to Kim Stearns of Packet Engines for providing a pair of G-NIC boards.
Thanks to Bruce Faust of Digitalscape for providing both their SYM53C885 board
and an AlphaStation to verifty the Alpha port!

IVb. References

Yellowfin Engineering Design Specification, 4/23/97 Preliminary/Confidential
Symbios SYM53C885 PCI-SCSI/Fast Ethernet Multifunction Controller Preliminary
   Data Manual v3.0
http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://cesdis.gsfc.nasa.gov/linux/misc/100mbps.html

IVc. Errata

See Packet Engines confidential appendix (prototype chips only).
*/



enum capability_flags {
	HasMII=1, FullTxStatus=2, IsGigabit=4, HasMulticastBug=8, FullRxStatus=16,
	HasMACAddrBug=32, /* Only on early revs.  */
	DontUseEeprom=64, /* Don't read the MAC from the EEPROm. */
};

/* The PCI I/O space extent. */
enum {
	YELLOWFIN_SIZE	= 0x100,
};

struct pci_id_info {
        const char *name;
        struct match_info {
                int     pci, pci_mask, subsystem, subsystem_mask;
                int revision, revision_mask;                            /* Only 8 bits. */
        } id;
        int drv_flags;                          /* Driver use, intended as capability flags. */
};

static const struct pci_id_info pci_id_tbl[] = {
	{"Yellowfin G-NIC Gigabit Ethernet", { 0x07021000, 0xffffffff},
	 FullTxStatus | IsGigabit | HasMulticastBug | HasMACAddrBug | DontUseEeprom},
	{"Symbios SYM83C885", { 0x07011000, 0xffffffff},
	  HasMII | DontUseEeprom },
	{ }
};

static const struct pci_device_id yellowfin_pci_tbl[] = {
	{ 0x1000, 0x0702, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0x1000, 0x0701, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
	{ }
};
MODULE_DEVICE_TABLE (pci, yellowfin_pci_tbl);


/* Offsets to the Yellowfin registers.  Various sizes and alignments. */
enum yellowfin_offsets {
	TxCtrl=0x00, TxStatus=0x04, TxPtr=0x0C,
	TxIntrSel=0x10, TxBranchSel=0x14, TxWaitSel=0x18,
	RxCtrl=0x40, RxStatus=0x44, RxPtr=0x4C,
	RxIntrSel=0x50, RxBranchSel=0x54, RxWaitSel=0x58,
	EventStatus=0x80, IntrEnb=0x82, IntrClear=0x84, IntrStatus=0x86,
	ChipRev=0x8C, DMACtrl=0x90, TxThreshold=0x94,
	Cnfg=0xA0, FrameGap0=0xA2, FrameGap1=0xA4,
	MII_Cmd=0xA6, MII_Addr=0xA8, MII_Wr_Data=0xAA, MII_Rd_Data=0xAC,
	MII_Status=0xAE,
	RxDepth=0xB8, FlowCtrl=0xBC,
	AddrMode=0xD0, StnAddr=0xD2, HashTbl=0xD8, FIFOcfg=0xF8,
	EEStatus=0xF0, EECtrl=0xF1, EEAddr=0xF2, EERead=0xF3, EEWrite=0xF4,
	EEFeature=0xF5,
};

/* The Yellowfin Rx and Tx buffer descriptors.
   Elements are written as 32 bit for endian portability. */
struct yellowfin_desc {
	__le32 dbdma_cmd;
	__le32 addr;
	__le32 branch_addr;
	__le32 result_status;
};

struct tx_status_words {
#ifdef __BIG_ENDIAN
	u16 tx_errs;
	u16 tx_cnt;
	u16 paused;
	u16 total_tx_cnt;
#else  /* Little endian chips. */
	u16 tx_cnt;
	u16 tx_errs;
	u16 total_tx_cnt;
	u16 paused;
#endif /* __BIG_ENDIAN */
};

/* Bits in yellowfin_desc.cmd */
enum desc_cmd_bits {
	CMD_TX_PKT=0x10000000, CMD_RX_BUF=0x20000000, CMD_TXSTATUS=0x30000000,
	CMD_NOP=0x60000000, CMD_STOP=0x70000000,
	BRANCH_ALWAYS=0x0C0000, INTR_ALWAYS=0x300000, WAIT_ALWAYS=0x030000,
	BRANCH_IFTRUE=0x040000,
};

/* Bits in yellowfin_desc.status */
enum desc_status_bits { RX_EOP=0x0040, };

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x01, IntrRxInvalid=0x02, IntrRxPCIFault=0x04,IntrRxPCIErr=0x08,
	IntrTxDone=0x10, IntrTxInvalid=0x20, IntrTxPCIFault=0x40,IntrTxPCIErr=0x80,
	IntrEarlyRx=0x100, IntrWakeup=0x200, };

#define PRIV_ALIGN	31 	/* Required alignment mask */
#define MII_CNT		4
struct yellowfin_private {
	/* Descriptor rings first for alignment.
	   Tx requires a second descriptor for status. */
	struct yellowfin_desc *rx_ring;
	struct yellowfin_desc *tx_ring;
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;

	struct tx_status_words *tx_status;
	dma_addr_t tx_status_dma;

	struct timer_list timer;	/* Media selection timer. */
	/* Frequently used and paired value: keep adjacent for cache effect. */
	int chip_id, drv_flags;
	struct pci_dev *pci_dev;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	struct tx_status_words *tx_tail_desc;
	unsigned int cur_tx, dirty_tx;
	int tx_threshold;
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[MII_CNT];		/* MII device addresses, only first one used */
	spinlock_t lock;
	void __iomem *base;
};

static int read_eeprom(void __iomem *ioaddr, int location);
static int mdio_read(void __iomem *ioaddr, int phy_id, int location);
static void mdio_write(void __iomem *ioaddr, int phy_id, int location, int value);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int yellowfin_open(struct net_device *dev);
static void yellowfin_timer(struct timer_list *t);
static void yellowfin_tx_timeout(struct net_device *dev);
static int yellowfin_init_ring(struct net_device *dev);
static netdev_tx_t yellowfin_start_xmit(struct sk_buff *skb,
					struct net_device *dev);
static irqreturn_t yellowfin_interrupt(int irq, void *dev_instance);
static int yellowfin_rx(struct net_device *dev);
static void yellowfin_error(struct net_device *dev, int intr_status);
static int yellowfin_close(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static const struct ethtool_ops ethtool_ops;

static const struct net_device_ops netdev_ops = {
	.ndo_open 		= yellowfin_open,
	.ndo_stop 		= yellowfin_close,
	.ndo_start_xmit 	= yellowfin_start_xmit,
	.ndo_set_rx_mode	= set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_do_ioctl 		= netdev_ioctl,
	.ndo_tx_timeout 	= yellowfin_tx_timeout,
};

static int yellowfin_init_one(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct yellowfin_private *np;
	int irq;
	int chip_idx = ent->driver_data;
	static int find_cnt;
	void __iomem *ioaddr;
	int i, option = find_cnt < MAX_UNITS ? options[find_cnt] : 0;
	int drv_flags = pci_id_tbl[chip_idx].drv_flags;
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

	i = pci_enable_device(pdev);
	if (i) return i;

	dev = alloc_etherdev(sizeof(*np));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);

	np = netdev_priv(dev);

	if (pci_request_regions(pdev, DRV_NAME))
		goto err_out_free_netdev;

	pci_set_master (pdev);

	ioaddr = pci_iomap(pdev, bar, YELLOWFIN_SIZE);
	if (!ioaddr)
		goto err_out_free_res;

	irq = pdev->irq;

	if (drv_flags & DontUseEeprom)
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = ioread8(ioaddr + StnAddr + i);
	else {
		int ee_offset = (read_eeprom(ioaddr, 6) == 0xff ? 0x100 : 0);
		for (i = 0; i < 6; i++)
			dev->dev_addr[i] = read_eeprom(ioaddr, ee_offset + i);
	}

	/* Reset the chip. */
	iowrite32(0x80000000, ioaddr + DMACtrl);

	pci_set_drvdata(pdev, dev);
	spin_lock_init(&np->lock);

	np->pci_dev = pdev;
	np->chip_id = chip_idx;
	np->drv_flags = drv_flags;
	np->base = ioaddr;

	ring_space = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_cleardev;
	np->tx_ring = ring_space;
	np->tx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_unmap_tx;
	np->rx_ring = ring_space;
	np->rx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, STATUS_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_unmap_rx;
	np->tx_status = ring_space;
	np->tx_status_dma = ring_dma;

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		np->default_port = option & 15;
		if (np->default_port)
			np->medialock = 1;
	}
	if (find_cnt < MAX_UNITS  &&  full_duplex[find_cnt] > 0)
		np->full_duplex = 1;

	if (np->full_duplex)
		np->duplex_lock = 1;

	/* The Yellowfin-specific entries in the device structure. */
	dev->netdev_ops = &netdev_ops;
	dev->ethtool_ops = &ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	if (mtu)
		dev->mtu = mtu;

	i = register_netdev(dev);
	if (i)
		goto err_out_unmap_status;

	netdev_info(dev, "%s type %8x at %p, %pM, IRQ %d\n",
		    pci_id_tbl[chip_idx].name,
		    ioread32(ioaddr + ChipRev), ioaddr,
		    dev->dev_addr, irq);

	if (np->drv_flags & HasMII) {
		int phy, phy_idx = 0;
		for (phy = 0; phy < 32 && phy_idx < MII_CNT; phy++) {
			int mii_status = mdio_read(ioaddr, phy, 1);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				np->advertising = mdio_read(ioaddr, phy, 4);
				netdev_info(dev, "MII PHY found at address %d, status 0x%04x advertising %04x\n",
					    phy, mii_status, np->advertising);
			}
		}
		np->mii_cnt = phy_idx;
	}

	find_cnt++;

	return 0;

err_out_unmap_status:
        pci_free_consistent(pdev, STATUS_TOTAL_SIZE, np->tx_status,
		np->tx_status_dma);
err_out_unmap_rx:
        pci_free_consistent(pdev, RX_TOTAL_SIZE, np->rx_ring, np->rx_ring_dma);
err_out_unmap_tx:
        pci_free_consistent(pdev, TX_TOTAL_SIZE, np->tx_ring, np->tx_ring_dma);
err_out_cleardev:
	pci_iounmap(pdev, ioaddr);
err_out_free_res:
	pci_release_regions(pdev);
err_out_free_netdev:
	free_netdev (dev);
	return -ENODEV;
}

static int read_eeprom(void __iomem *ioaddr, int location)
{
	int bogus_cnt = 10000;		/* Typical 33Mhz: 1050 ticks */

	iowrite8(location, ioaddr + EEAddr);
	iowrite8(0x30 | ((location >> 8) & 7), ioaddr + EECtrl);
	while ((ioread8(ioaddr + EEStatus) & 0x80)  &&  --bogus_cnt > 0)
		;
	return ioread8(ioaddr + EERead);
}

/* MII Managemen Data I/O accesses.
   These routines assume the MDIO controller is idle, and do not exit until
   the command is finished. */

static int mdio_read(void __iomem *ioaddr, int phy_id, int location)
{
	int i;

	iowrite16((phy_id<<8) + location, ioaddr + MII_Addr);
	iowrite16(1, ioaddr + MII_Cmd);
	for (i = 10000; i >= 0; i--)
		if ((ioread16(ioaddr + MII_Status) & 1) == 0)
			break;
	return ioread16(ioaddr + MII_Rd_Data);
}

static void mdio_write(void __iomem *ioaddr, int phy_id, int location, int value)
{
	int i;

	iowrite16((phy_id<<8) + location, ioaddr + MII_Addr);
	iowrite16(value, ioaddr + MII_Wr_Data);

	/* Wait for the command to finish. */
	for (i = 10000; i >= 0; i--)
		if ((ioread16(ioaddr + MII_Status) & 1) == 0)
			break;
}


static int yellowfin_open(struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	const int irq = yp->pci_dev->irq;
	void __iomem *ioaddr = yp->base;
	int i, rc;

	/* Reset the chip. */
	iowrite32(0x80000000, ioaddr + DMACtrl);

	rc = request_irq(irq, yellowfin_interrupt, IRQF_SHARED, dev->name, dev);
	if (rc)
		return rc;

	rc = yellowfin_init_ring(dev);
	if (rc < 0)
		goto err_free_irq;

	iowrite32(yp->rx_ring_dma, ioaddr + RxPtr);
	iowrite32(yp->tx_ring_dma, ioaddr + TxPtr);

	for (i = 0; i < 6; i++)
		iowrite8(dev->dev_addr[i], ioaddr + StnAddr + i);

	/* Set up various condition 'select' registers.
	   There are no options here. */
	iowrite32(0x00800080, ioaddr + TxIntrSel); 	/* Interrupt on Tx abort */
	iowrite32(0x00800080, ioaddr + TxBranchSel);	/* Branch on Tx abort */
	iowrite32(0x00400040, ioaddr + TxWaitSel); 	/* Wait on Tx status */
	iowrite32(0x00400040, ioaddr + RxIntrSel);	/* Interrupt on Rx done */
	iowrite32(0x00400040, ioaddr + RxBranchSel);	/* Branch on Rx error */
	iowrite32(0x00400040, ioaddr + RxWaitSel);	/* Wait on Rx done */

	/* Initialize other registers: with so many this eventually this will
	   converted to an offset/value list. */
	iowrite32(dma_ctrl, ioaddr + DMACtrl);
	iowrite16(fifo_cfg, ioaddr + FIFOcfg);
	/* Enable automatic generation of flow control frames, period 0xffff. */
	iowrite32(0x0030FFFF, ioaddr + FlowCtrl);

	yp->tx_threshold = 32;
	iowrite32(yp->tx_threshold, ioaddr + TxThreshold);

	if (dev->if_port == 0)
		dev->if_port = yp->default_port;

	netif_start_queue(dev);

	/* Setting the Rx mode will start the Rx process. */
	if (yp->drv_flags & IsGigabit) {
		/* We are always in full-duplex mode with gigabit! */
		yp->full_duplex = 1;
		iowrite16(0x01CF, ioaddr + Cnfg);
	} else {
		iowrite16(0x0018, ioaddr + FrameGap0); /* 0060/4060 for non-MII 10baseT */
		iowrite16(0x1018, ioaddr + FrameGap1);
		iowrite16(0x101C | (yp->full_duplex ? 2 : 0), ioaddr + Cnfg);
	}
	set_rx_mode(dev);

	/* Enable interrupts by setting the interrupt mask. */
	iowrite16(0x81ff, ioaddr + IntrEnb);			/* See enum intr_status_bits */
	iowrite16(0x0000, ioaddr + EventStatus);		/* Clear non-interrupting events */
	iowrite32(0x80008000, ioaddr + RxCtrl);		/* Start Rx and Tx channels. */
	iowrite32(0x80008000, ioaddr + TxCtrl);

	if (yellowfin_debug > 2) {
		netdev_printk(KERN_DEBUG, dev, "Done %s()\n", __func__);
	}

	/* Set the timer to check for link beat. */
	timer_setup(&yp->timer, yellowfin_timer, 0);
	yp->timer.expires = jiffies + 3*HZ;
	add_timer(&yp->timer);
out:
	return rc;

err_free_irq:
	free_irq(irq, dev);
	goto out;
}

static void yellowfin_timer(struct timer_list *t)
{
	struct yellowfin_private *yp = from_timer(yp, t, timer);
	struct net_device *dev = pci_get_drvdata(yp->pci_dev);
	void __iomem *ioaddr = yp->base;
	int next_tick = 60*HZ;

	if (yellowfin_debug > 3) {
		netdev_printk(KERN_DEBUG, dev, "Yellowfin timer tick, status %08x\n",
			      ioread16(ioaddr + IntrStatus));
	}

	if (yp->mii_cnt) {
		int bmsr = mdio_read(ioaddr, yp->phys[0], MII_BMSR);
		int lpa = mdio_read(ioaddr, yp->phys[0], MII_LPA);
		int negotiated = lpa & yp->advertising;
		if (yellowfin_debug > 1)
			netdev_printk(KERN_DEBUG, dev, "MII #%d status register is %04x, link partner capability %04x\n",
				      yp->phys[0], bmsr, lpa);

		yp->full_duplex = mii_duplex(yp->duplex_lock, negotiated);

		iowrite16(0x101C | (yp->full_duplex ? 2 : 0), ioaddr + Cnfg);

		if (bmsr & BMSR_LSTATUS)
			next_tick = 60*HZ;
		else
			next_tick = 3*HZ;
	}

	yp->timer.expires = jiffies + next_tick;
	add_timer(&yp->timer);
}

static void yellowfin_tx_timeout(struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	void __iomem *ioaddr = yp->base;

	netdev_warn(dev, "Yellowfin transmit timed out at %d/%d Tx status %04x, Rx status %04x, resetting...\n",
		    yp->cur_tx, yp->dirty_tx,
		    ioread32(ioaddr + TxStatus),
		    ioread32(ioaddr + RxStatus));

	/* Note: these should be KERN_DEBUG. */
	if (yellowfin_debug) {
		int i;
		pr_warn("  Rx ring %p: ", yp->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			pr_cont(" %08x", yp->rx_ring[i].result_status);
		pr_cont("\n");
		pr_warn("  Tx ring %p: ", yp->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			pr_cont(" %04x /%08x",
			       yp->tx_status[i].tx_errs,
			       yp->tx_ring[i].result_status);
		pr_cont("\n");
	}

	/* If the hardware is found to hang regularly, we will update the code
	   to reinitialize the chip here. */
	dev->if_port = 0;

	/* Wake the potentially-idle transmit channel. */
	iowrite32(0x10001000, yp->base + TxCtrl);
	if (yp->cur_tx - yp->dirty_tx < TX_QUEUE_SIZE)
		netif_wake_queue (dev);		/* Typical path */

	netif_trans_update(dev); /* prevent tx timeout */
	dev->stats.tx_errors++;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static int yellowfin_init_ring(struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	int i, j;

	yp->tx_full = 0;
	yp->cur_rx = yp->cur_tx = 0;
	yp->dirty_tx = 0;

	yp->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

	for (i = 0; i < RX_RING_SIZE; i++) {
		yp->rx_ring[i].dbdma_cmd =
			cpu_to_le32(CMD_RX_BUF | INTR_ALWAYS | yp->rx_buf_sz);
		yp->rx_ring[i].branch_addr = cpu_to_le32(yp->rx_ring_dma +
			((i+1)%RX_RING_SIZE)*sizeof(struct yellowfin_desc));
	}

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = netdev_alloc_skb(dev, yp->rx_buf_sz + 2);
		yp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb_reserve(skb, 2);	/* 16 byte align the IP header. */
		yp->rx_ring[i].addr = cpu_to_le32(pci_map_single(yp->pci_dev,
			skb->data, yp->rx_buf_sz, PCI_DMA_FROMDEVICE));
	}
	if (i != RX_RING_SIZE) {
		for (j = 0; j < i; j++)
			dev_kfree_skb(yp->rx_skbuff[j]);
		return -ENOMEM;
	}
	yp->rx_ring[i-1].dbdma_cmd = cpu_to_le32(CMD_STOP);
	yp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

#define NO_TXSTATS
#ifdef NO_TXSTATS
	/* In this mode the Tx ring needs only a single descriptor. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		yp->tx_skbuff[i] = NULL;
		yp->tx_ring[i].dbdma_cmd = cpu_to_le32(CMD_STOP);
		yp->tx_ring[i].branch_addr = cpu_to_le32(yp->tx_ring_dma +
			((i+1)%TX_RING_SIZE)*sizeof(struct yellowfin_desc));
	}
	/* Wrap ring */
	yp->tx_ring[--i].dbdma_cmd = cpu_to_le32(CMD_STOP | BRANCH_ALWAYS);
#else
{
	/* Tx ring needs a pair of descriptors, the second for the status. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		j = 2*i;
		yp->tx_skbuff[i] = 0;
		/* Branch on Tx error. */
		yp->tx_ring[j].dbdma_cmd = cpu_to_le32(CMD_STOP);
		yp->tx_ring[j].branch_addr = cpu_to_le32(yp->tx_ring_dma +
			(j+1)*sizeof(struct yellowfin_desc));
		j++;
		if (yp->flags & FullTxStatus) {
			yp->tx_ring[j].dbdma_cmd =
				cpu_to_le32(CMD_TXSTATUS | sizeof(*yp->tx_status));
			yp->tx_ring[j].request_cnt = sizeof(*yp->tx_status);
			yp->tx_ring[j].addr = cpu_to_le32(yp->tx_status_dma +
				i*sizeof(struct tx_status_words));
		} else {
			/* Symbios chips write only tx_errs word. */
			yp->tx_ring[j].dbdma_cmd =
				cpu_to_le32(CMD_TXSTATUS | INTR_ALWAYS | 2);
			yp->tx_ring[j].request_cnt = 2;
			/* Om pade ummmmm... */
			yp->tx_ring[j].addr = cpu_to_le32(yp->tx_status_dma +
				i*sizeof(struct tx_status_words) +
				&(yp->tx_status[0].tx_errs) -
				&(yp->tx_status[0]));
		}
		yp->tx_ring[j].branch_addr = cpu_to_le32(yp->tx_ring_dma +
			((j+1)%(2*TX_RING_SIZE))*sizeof(struct yellowfin_desc));
	}
	/* Wrap ring */
	yp->tx_ring[++j].dbdma_cmd |= cpu_to_le32(BRANCH_ALWAYS | INTR_ALWAYS);
}
#endif
	yp->tx_tail_desc = &yp->tx_status[0];
	return 0;
}

static netdev_tx_t yellowfin_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	unsigned entry;
	int len = skb->len;

	netif_stop_queue (dev);

	/* Note: Ordering is important here, set the field with the
	   "ownership" bit last, and only then increment cur_tx. */

	/* Calculate the next Tx descriptor entry. */
	entry = yp->cur_tx % TX_RING_SIZE;

	if (gx_fix) {	/* Note: only works for paddable protocols e.g.  IP. */
		int cacheline_end = ((unsigned long)skb->data + skb->len) % 32;
		/* Fix GX chipset errata. */
		if (cacheline_end > 24  || cacheline_end == 0) {
			len = skb->len + 32 - cacheline_end + 1;
			if (skb_padto(skb, len)) {
				yp->tx_skbuff[entry] = NULL;
				netif_wake_queue(dev);
				return NETDEV_TX_OK;
			}
		}
	}
	yp->tx_skbuff[entry] = skb;

#ifdef NO_TXSTATS
	yp->tx_ring[entry].addr = cpu_to_le32(pci_map_single(yp->pci_dev,
		skb->data, len, PCI_DMA_TODEVICE));
	yp->tx_ring[entry].result_status = 0;
	if (entry >= TX_RING_SIZE-1) {
		/* New stop command. */
		yp->tx_ring[0].dbdma_cmd = cpu_to_le32(CMD_STOP);
		yp->tx_ring[TX_RING_SIZE-1].dbdma_cmd =
			cpu_to_le32(CMD_TX_PKT|BRANCH_ALWAYS | len);
	} else {
		yp->tx_ring[entry+1].dbdma_cmd = cpu_to_le32(CMD_STOP);
		yp->tx_ring[entry].dbdma_cmd =
			cpu_to_le32(CMD_TX_PKT | BRANCH_IFTRUE | len);
	}
	yp->cur_tx++;
#else
	yp->tx_ring[entry<<1].request_cnt = len;
	yp->tx_ring[entry<<1].addr = cpu_to_le32(pci_map_single(yp->pci_dev,
		skb->data, len, PCI_DMA_TODEVICE));
	/* The input_last (status-write) command is constant, but we must
	   rewrite the subsequent 'stop' command. */

	yp->cur_tx++;
	{
		unsigned next_entry = yp->cur_tx % TX_RING_SIZE;
		yp->tx_ring[next_entry<<1].dbdma_cmd = cpu_to_le32(CMD_STOP);
	}
	/* Final step -- overwrite the old 'stop' command. */

	yp->tx_ring[entry<<1].dbdma_cmd =
		cpu_to_le32( ((entry % 6) == 0 ? CMD_TX_PKT|INTR_ALWAYS|BRANCH_IFTRUE :
					  CMD_TX_PKT | BRANCH_IFTRUE) | len);
#endif

	/* Non-x86 Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	iowrite32(0x10001000, yp->base + TxCtrl);

	if (yp->cur_tx - yp->dirty_tx < TX_QUEUE_SIZE)
		netif_start_queue (dev);		/* Typical path */
	else
		yp->tx_full = 1;

	if (yellowfin_debug > 4) {
		netdev_printk(KERN_DEBUG, dev, "Yellowfin transmit frame #%d queued in slot %d\n",
			      yp->cur_tx, entry);
	}
	return NETDEV_TX_OK;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t yellowfin_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct yellowfin_private *yp;
	void __iomem *ioaddr;
	int boguscnt = max_interrupt_work;
	unsigned int handled = 0;

	yp = netdev_priv(dev);
	ioaddr = yp->base;

	spin_lock (&yp->lock);

	do {
		u16 intr_status = ioread16(ioaddr + IntrClear);

		if (yellowfin_debug > 4)
			netdev_printk(KERN_DEBUG, dev, "Yellowfin interrupt, status %04x\n",
				      intr_status);

		if (intr_status == 0)
			break;
		handled = 1;

		if (intr_status & (IntrRxDone | IntrEarlyRx)) {
			yellowfin_rx(dev);
			iowrite32(0x10001000, ioaddr + RxCtrl);		/* Wake Rx engine. */
		}

#ifdef NO_TXSTATS
		for (; yp->cur_tx - yp->dirty_tx > 0; yp->dirty_tx++) {
			int entry = yp->dirty_tx % TX_RING_SIZE;
			struct sk_buff *skb;

			if (yp->tx_ring[entry].result_status == 0)
				break;
			skb = yp->tx_skbuff[entry];
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += skb->len;
			/* Free the original skb. */
			pci_unmap_single(yp->pci_dev, le32_to_cpu(yp->tx_ring[entry].addr),
				skb->len, PCI_DMA_TODEVICE);
			dev_consume_skb_irq(skb);
			yp->tx_skbuff[entry] = NULL;
		}
		if (yp->tx_full &&
		    yp->cur_tx - yp->dirty_tx < TX_QUEUE_SIZE - 4) {
			/* The ring is no longer full, clear tbusy. */
			yp->tx_full = 0;
			netif_wake_queue(dev);
		}
#else
		if ((intr_status & IntrTxDone) || (yp->tx_tail_desc->tx_errs)) {
			unsigned dirty_tx = yp->dirty_tx;

			for (dirty_tx = yp->dirty_tx; yp->cur_tx - dirty_tx > 0;
				 dirty_tx++) {
				/* Todo: optimize this. */
				int entry = dirty_tx % TX_RING_SIZE;
				u16 tx_errs = yp->tx_status[entry].tx_errs;
				struct sk_buff *skb;

#ifndef final_version
				if (yellowfin_debug > 5)
					netdev_printk(KERN_DEBUG, dev, "Tx queue %d check, Tx status %04x %04x %04x %04x\n",
						      entry,
						      yp->tx_status[entry].tx_cnt,
						      yp->tx_status[entry].tx_errs,
						      yp->tx_status[entry].total_tx_cnt,
						      yp->tx_status[entry].paused);
#endif
				if (tx_errs == 0)
					break;	/* It still hasn't been Txed */
				skb = yp->tx_skbuff[entry];
				if (tx_errs & 0xF810) {
					/* There was an major error, log it. */
#ifndef final_version
					if (yellowfin_debug > 1)
						netdev_printk(KERN_DEBUG, dev, "Transmit error, Tx status %04x\n",
							      tx_errs);
#endif
					dev->stats.tx_errors++;
					if (tx_errs & 0xF800) dev->stats.tx_aborted_errors++;
					if (tx_errs & 0x0800) dev->stats.tx_carrier_errors++;
					if (tx_errs & 0x2000) dev->stats.tx_window_errors++;
					if (tx_errs & 0x8000) dev->stats.tx_fifo_errors++;
				} else {
#ifndef final_version
					if (yellowfin_debug > 4)
						netdev_printk(KERN_DEBUG, dev, "Normal transmit, Tx status %04x\n",
							      tx_errs);
#endif
					dev->stats.tx_bytes += skb->len;
					dev->stats.collisions += tx_errs & 15;
					dev->stats.tx_packets++;
				}
				/* Free the original skb. */
				pci_unmap_single(yp->pci_dev,
					yp->tx_ring[entry<<1].addr, skb->len,
					PCI_DMA_TODEVICE);
				dev_consume_skb_irq(skb);
				yp->tx_skbuff[entry] = 0;
				/* Mark status as empty. */
				yp->tx_status[entry].tx_errs = 0;
			}

#ifndef final_version
			if (yp->cur_tx - dirty_tx > TX_RING_SIZE) {
				netdev_err(dev, "Out-of-sync dirty pointer, %d vs. %d, full=%d\n",
					   dirty_tx, yp->cur_tx, yp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (yp->tx_full &&
			    yp->cur_tx - dirty_tx < TX_QUEUE_SIZE - 2) {
				/* The ring is no longer full, clear tbusy. */
				yp->tx_full = 0;
				netif_wake_queue(dev);
			}

			yp->dirty_tx = dirty_tx;
			yp->tx_tail_desc = &yp->tx_status[dirty_tx % TX_RING_SIZE];
		}
#endif

		/* Log errors and other uncommon events. */
		if (intr_status & 0x2ee)	/* Abnormal error summary. */
			yellowfin_error(dev, intr_status);

		if (--boguscnt < 0) {
			netdev_warn(dev, "Too much work at interrupt, status=%#04x\n",
				    intr_status);
			break;
		}
	} while (1);

	if (yellowfin_debug > 3)
		netdev_printk(KERN_DEBUG, dev, "exiting interrupt, status=%#04x\n",
			      ioread16(ioaddr + IntrStatus));

	spin_unlock (&yp->lock);
	return IRQ_RETVAL(handled);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int yellowfin_rx(struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	int entry = yp->cur_rx % RX_RING_SIZE;
	int boguscnt = yp->dirty_rx + RX_RING_SIZE - yp->cur_rx;

	if (yellowfin_debug > 4) {
		printk(KERN_DEBUG " In yellowfin_rx(), entry %d status %08x\n",
			   entry, yp->rx_ring[entry].result_status);
		printk(KERN_DEBUG "   #%d desc. %08x %08x %08x\n",
			   entry, yp->rx_ring[entry].dbdma_cmd, yp->rx_ring[entry].addr,
			   yp->rx_ring[entry].result_status);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (1) {
		struct yellowfin_desc *desc = &yp->rx_ring[entry];
		struct sk_buff *rx_skb = yp->rx_skbuff[entry];
		s16 frame_status;
		u16 desc_status;
		int data_size, yf_size;
		u8 *buf_addr;

		if(!desc->result_status)
			break;
		pci_dma_sync_single_for_cpu(yp->pci_dev, le32_to_cpu(desc->addr),
			yp->rx_buf_sz, PCI_DMA_FROMDEVICE);
		desc_status = le32_to_cpu(desc->result_status) >> 16;
		buf_addr = rx_skb->data;
		data_size = (le32_to_cpu(desc->dbdma_cmd) -
			le32_to_cpu(desc->result_status)) & 0xffff;
		frame_status = get_unaligned_le16(&(buf_addr[data_size - 2]));
		if (yellowfin_debug > 4)
			printk(KERN_DEBUG "  %s() status was %04x\n",
			       __func__, frame_status);
		if (--boguscnt < 0)
			break;

		yf_size = sizeof(struct yellowfin_desc);

		if ( ! (desc_status & RX_EOP)) {
			if (data_size != 0)
				netdev_warn(dev, "Oversized Ethernet frame spanned multiple buffers, status %04x, data_size %d!\n",
					    desc_status, data_size);
			dev->stats.rx_length_errors++;
		} else if ((yp->drv_flags & IsGigabit)  &&  (frame_status & 0x0038)) {
			/* There was a error. */
			if (yellowfin_debug > 3)
				printk(KERN_DEBUG "  %s() Rx error was %04x\n",
				       __func__, frame_status);
			dev->stats.rx_errors++;
			if (frame_status & 0x0060) dev->stats.rx_length_errors++;
			if (frame_status & 0x0008) dev->stats.rx_frame_errors++;
			if (frame_status & 0x0010) dev->stats.rx_crc_errors++;
			if (frame_status < 0) dev->stats.rx_dropped++;
		} else if ( !(yp->drv_flags & IsGigabit)  &&
				   ((buf_addr[data_size-1] & 0x85) || buf_addr[data_size-2] & 0xC0)) {
			u8 status1 = buf_addr[data_size-2];
			u8 status2 = buf_addr[data_size-1];
			dev->stats.rx_errors++;
			if (status1 & 0xC0) dev->stats.rx_length_errors++;
			if (status2 & 0x03) dev->stats.rx_frame_errors++;
			if (status2 & 0x04) dev->stats.rx_crc_errors++;
			if (status2 & 0x80) dev->stats.rx_dropped++;
#ifdef YF_PROTOTYPE		/* Support for prototype hardware errata. */
		} else if ((yp->flags & HasMACAddrBug)  &&
			!ether_addr_equal(le32_to_cpu(yp->rx_ring_dma +
						      entry * yf_size),
					  dev->dev_addr) &&
			!ether_addr_equal(le32_to_cpu(yp->rx_ring_dma +
						      entry * yf_size),
					  "\377\377\377\377\377\377")) {
			if (bogus_rx++ == 0)
				netdev_warn(dev, "Bad frame to %pM\n",
					    buf_addr);
#endif
		} else {
			struct sk_buff *skb;
			int pkt_len = data_size -
				(yp->chip_id ? 7 : 8 + buf_addr[data_size - 8]);
			/* To verify: Yellowfin Length should omit the CRC! */

#ifndef final_version
			if (yellowfin_debug > 4)
				printk(KERN_DEBUG "  %s() normal Rx pkt length %d of %d, bogus_cnt %d\n",
				       __func__, pkt_len, data_size, boguscnt);
#endif
			/* Check if the packet is long enough to just pass up the skbuff
			   without copying to a properly sized skbuff. */
			if (pkt_len > rx_copybreak) {
				skb_put(skb = rx_skb, pkt_len);
				pci_unmap_single(yp->pci_dev,
					le32_to_cpu(yp->rx_ring[entry].addr),
					yp->rx_buf_sz,
					PCI_DMA_FROMDEVICE);
				yp->rx_skbuff[entry] = NULL;
			} else {
				skb = netdev_alloc_skb(dev, pkt_len + 2);
				if (skb == NULL)
					break;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				skb_copy_to_linear_data(skb, rx_skb->data, pkt_len);
				skb_put(skb, pkt_len);
				pci_dma_sync_single_for_device(yp->pci_dev,
								le32_to_cpu(desc->addr),
								yp->rx_buf_sz,
								PCI_DMA_FROMDEVICE);
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;
		}
		entry = (++yp->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; yp->cur_rx - yp->dirty_rx > 0; yp->dirty_rx++) {
		entry = yp->dirty_rx % RX_RING_SIZE;
		if (yp->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb = netdev_alloc_skb(dev, yp->rx_buf_sz + 2);
			if (skb == NULL)
				break;				/* Better luck next round. */
			yp->rx_skbuff[entry] = skb;
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			yp->rx_ring[entry].addr = cpu_to_le32(pci_map_single(yp->pci_dev,
				skb->data, yp->rx_buf_sz, PCI_DMA_FROMDEVICE));
		}
		yp->rx_ring[entry].dbdma_cmd = cpu_to_le32(CMD_STOP);
		yp->rx_ring[entry].result_status = 0;	/* Clear complete bit. */
		if (entry != 0)
			yp->rx_ring[entry - 1].dbdma_cmd =
				cpu_to_le32(CMD_RX_BUF | INTR_ALWAYS | yp->rx_buf_sz);
		else
			yp->rx_ring[RX_RING_SIZE - 1].dbdma_cmd =
				cpu_to_le32(CMD_RX_BUF | INTR_ALWAYS | BRANCH_ALWAYS
							| yp->rx_buf_sz);
	}

	return 0;
}

static void yellowfin_error(struct net_device *dev, int intr_status)
{
	netdev_err(dev, "Something Wicked happened! %04x\n", intr_status);
	/* Hmmmmm, it's not clear what to do here. */
	if (intr_status & (IntrTxPCIErr | IntrTxPCIFault))
		dev->stats.tx_errors++;
	if (intr_status & (IntrRxPCIErr | IntrRxPCIFault))
		dev->stats.rx_errors++;
}

static int yellowfin_close(struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	void __iomem *ioaddr = yp->base;
	int i;

	netif_stop_queue (dev);

	if (yellowfin_debug > 1) {
		netdev_printk(KERN_DEBUG, dev, "Shutting down ethercard, status was Tx %04x Rx %04x Int %02x\n",
			      ioread16(ioaddr + TxStatus),
			      ioread16(ioaddr + RxStatus),
			      ioread16(ioaddr + IntrStatus));
		netdev_printk(KERN_DEBUG, dev, "Queue pointers were Tx %d / %d,  Rx %d / %d\n",
			      yp->cur_tx, yp->dirty_tx,
			      yp->cur_rx, yp->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	iowrite16(0x0000, ioaddr + IntrEnb);

	/* Stop the chip's Tx and Rx processes. */
	iowrite32(0x80000000, ioaddr + RxCtrl);
	iowrite32(0x80000000, ioaddr + TxCtrl);

	del_timer(&yp->timer);

#if defined(__i386__)
	if (yellowfin_debug > 2) {
		printk(KERN_DEBUG "  Tx ring at %08llx:\n",
				(unsigned long long)yp->tx_ring_dma);
		for (i = 0; i < TX_RING_SIZE*2; i++)
			printk(KERN_DEBUG " %c #%d desc. %08x %08x %08x %08x\n",
				   ioread32(ioaddr + TxPtr) == (long)&yp->tx_ring[i] ? '>' : ' ',
				   i, yp->tx_ring[i].dbdma_cmd, yp->tx_ring[i].addr,
				   yp->tx_ring[i].branch_addr, yp->tx_ring[i].result_status);
		printk(KERN_DEBUG "  Tx status %p:\n", yp->tx_status);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(KERN_DEBUG "   #%d status %04x %04x %04x %04x\n",
				   i, yp->tx_status[i].tx_cnt, yp->tx_status[i].tx_errs,
				   yp->tx_status[i].total_tx_cnt, yp->tx_status[i].paused);

		printk(KERN_DEBUG "  Rx ring %08llx:\n",
				(unsigned long long)yp->rx_ring_dma);
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " %c #%d desc. %08x %08x %08x\n",
				   ioread32(ioaddr + RxPtr) == (long)&yp->rx_ring[i] ? '>' : ' ',
				   i, yp->rx_ring[i].dbdma_cmd, yp->rx_ring[i].addr,
				   yp->rx_ring[i].result_status);
			if (yellowfin_debug > 6) {
				if (get_unaligned((u8*)yp->rx_ring[i].addr) != 0x69) {
					int j;

					printk(KERN_DEBUG);
					for (j = 0; j < 0x50; j++)
						pr_cont(" %04x",
							get_unaligned(((u16*)yp->rx_ring[i].addr) + j));
					pr_cont("\n");
				}
			}
		}
	}
#endif /* __i386__ debugging only */

	free_irq(yp->pci_dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		yp->rx_ring[i].dbdma_cmd = cpu_to_le32(CMD_STOP);
		yp->rx_ring[i].addr = cpu_to_le32(0xBADF00D0); /* An invalid address. */
		if (yp->rx_skbuff[i]) {
			dev_kfree_skb(yp->rx_skbuff[i]);
		}
		yp->rx_skbuff[i] = NULL;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		dev_kfree_skb(yp->tx_skbuff[i]);
		yp->tx_skbuff[i] = NULL;
	}

#ifdef YF_PROTOTYPE			/* Support for prototype hardware errata. */
	if (yellowfin_debug > 0) {
		netdev_printk(KERN_DEBUG, dev, "Received %d frames that we should not have\n",
			      bogus_rx);
	}
#endif

	return 0;
}

/* Set or clear the multicast filter for this adaptor. */

static void set_rx_mode(struct net_device *dev)
{
	struct yellowfin_private *yp = netdev_priv(dev);
	void __iomem *ioaddr = yp->base;
	u16 cfg_value = ioread16(ioaddr + Cnfg);

	/* Stop the Rx process to change any value. */
	iowrite16(cfg_value & ~0x1000, ioaddr + Cnfg);
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		iowrite16(0x000F, ioaddr + AddrMode);
	} else if ((netdev_mc_count(dev) > 64) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter well, or accept all multicasts. */
		iowrite16(0x000B, ioaddr + AddrMode);
	} else if (!netdev_mc_empty(dev)) { /* Must use the multicast hash table. */
		struct netdev_hw_addr *ha;
		u16 hash_table[4];
		int i;

		memset(hash_table, 0, sizeof(hash_table));
		netdev_for_each_mc_addr(ha, dev) {
			unsigned int bit;

			/* Due to a bug in the early chip versions, multiple filter
			   slots must be set for each address. */
			if (yp->drv_flags & HasMulticastBug) {
				bit = (ether_crc_le(3, ha->addr) >> 3) & 0x3f;
				hash_table[bit >> 4] |= (1 << bit);
				bit = (ether_crc_le(4, ha->addr) >> 3) & 0x3f;
				hash_table[bit >> 4] |= (1 << bit);
				bit = (ether_crc_le(5, ha->addr) >> 3) & 0x3f;
				hash_table[bit >> 4] |= (1 << bit);
			}
			bit = (ether_crc_le(6, ha->addr) >> 3) & 0x3f;
			hash_table[bit >> 4] |= (1 << bit);
		}
		/* Copy the hash table to the chip. */
		for (i = 0; i < 4; i++)
			iowrite16(hash_table[i], ioaddr + HashTbl + i*2);
		iowrite16(0x0003, ioaddr + AddrMode);
	} else {					/* Normal, unicast/broadcast-only mode. */
		iowrite16(0x0001, ioaddr + AddrMode);
	}
	/* Restart the Rx process. */
	iowrite16(cfg_value | 0x1000, ioaddr + Cnfg);
}

static void yellowfin_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct yellowfin_private *np = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(np->pci_dev), sizeof(info->bus_info));
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo = yellowfin_get_drvinfo
};

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct yellowfin_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	struct mii_ioctl_data *data = if_mii(rq);

	switch(cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		data->phy_id = np->phys[0] & 0x1f;
		/* Fall Through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
		data->val_out = mdio_read(ioaddr, data->phy_id & 0x1f, data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
		if (data->phy_id == np->phys[0]) {
			u16 value = data->val_in;
			switch (data->reg_num) {
			case 0:
				/* Check for autonegotiation on or reset. */
				np->medialock = (value & 0x9000) ? 0 : 1;
				if (np->medialock)
					np->full_duplex = (value & 0x0100) ? 1 : 0;
				break;
			case 4: np->advertising = value; break;
			}
			/* Perhaps check_duplex(dev), depending on chip semantics. */
		}
		mdio_write(ioaddr, data->phy_id & 0x1f, data->reg_num & 0x1f, data->val_in);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}


static void yellowfin_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct yellowfin_private *np;

	BUG_ON(!dev);
	np = netdev_priv(dev);

        pci_free_consistent(pdev, STATUS_TOTAL_SIZE, np->tx_status,
		np->tx_status_dma);
	pci_free_consistent(pdev, RX_TOTAL_SIZE, np->rx_ring, np->rx_ring_dma);
	pci_free_consistent(pdev, TX_TOTAL_SIZE, np->tx_ring, np->tx_ring_dma);
	unregister_netdev (dev);

	pci_iounmap(pdev, np->base);

	pci_release_regions (pdev);

	free_netdev (dev);
}


static struct pci_driver yellowfin_driver = {
	.name		= DRV_NAME,
	.id_table	= yellowfin_pci_tbl,
	.probe		= yellowfin_init_one,
	.remove		= yellowfin_remove_one,
};


static int __init yellowfin_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif
	return pci_register_driver(&yellowfin_driver);
}


static void __exit yellowfin_cleanup (void)
{
	pci_unregister_driver (&yellowfin_driver);
}


module_init(yellowfin_init);
module_exit(yellowfin_cleanup);

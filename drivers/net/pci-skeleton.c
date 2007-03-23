/*

	drivers/net/pci-skeleton.c

	Maintained by Jeff Garzik <jgarzik@pobox.com>

	Original code came from 8139too.c, which in turns was based
	originally on Donald Becker's rtl8139.c driver, versions 1.11
	and older.  This driver was originally based on rtl8139.c
	version 1.07.  Header of rtl8139.c version 1.11:

	-----<snip>-----

        	Written 1997-2000 by Donald Becker.
		This software may be used and distributed according to the
		terms of the GNU General Public License (GPL), incorporated
		herein by reference.  Drivers based on or derived from this
		code fall under the GPL and must retain the authorship,
		copyright and license notice.  This file is not a complete
		program and may only be used when the entire operating
		system is licensed under the GPL.

		This driver is for boards based on the RTL8129 and RTL8139
		PCI ethernet chips.

		The author may be reached as becker@scyld.com, or C/O Scyld
		Computing Corporation 410 Severn Ave., Suite 210 Annapolis
		MD 21403

		Support and updates available at
		http://www.scyld.com/network/rtl8139.html

		Twister-tuning table provided by Kinston
		<shangh@realtek.com.tw>.

	-----<snip>-----

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.


-----------------------------------------------------------------------------

				Theory of Operation

I. Board Compatibility

This device driver is designed for the RealTek RTL8139 series, the RealTek
Fast Ethernet controllers for PCI and CardBus.  This chip is used on many
low-end boards, sometimes with its markings changed.


II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS will assign the
PCI INTA signal to a (preferably otherwise unused) system IRQ line.

III. Driver operation

IIIa. Rx Ring buffers

The receive unit uses a single linear ring buffer rather than the more
common (and more efficient) descriptor-based architecture.  Incoming frames
are sequentially stored into the Rx region, and the host copies them into
skbuffs.

Comment: While it is theoretically possible to process many frames in place,
any delay in Rx processing would cause us to drop frames.  More importantly,
the Linux protocol stack is not designed to operate in this manner.

IIIb. Tx operation

The RTL8139 uses a fixed set of four Tx descriptors in register space.
In a stunningly bad design choice, Tx frames must be 32 bit aligned.  Linux
aligns the IP header on word boundaries, and 14 byte ethernet header means
that almost all frames will need to be copied to an alignment buffer.

IVb. References

http://www.realtek.com.tw/cn/cn.html
http://www.scyld.com/expert/NWay.html

IVc. Errata

*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <asm/io.h>

#define NETDRV_VERSION		"1.0.1"
#define MODNAME			"netdrv"
#define NETDRV_DRIVER_LOAD_MSG	"MyVendor Fast Ethernet driver " NETDRV_VERSION " loaded"
#define PFX			MODNAME ": "

static char version[] __devinitdata =
KERN_INFO NETDRV_DRIVER_LOAD_MSG "\n"
KERN_INFO "  Support available from http://foo.com/bar/baz.html\n";

/* define to 1 to enable PIO instead of MMIO */
#undef USE_IO_OPS

/* define to 1 to enable copious debugging info */
#undef NETDRV_DEBUG

/* define to 1 to disable lightweight runtime debugging checks */
#undef NETDRV_NDEBUG


#ifdef NETDRV_DEBUG
/* note: prints function name for you */
#  define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

#ifdef NETDRV_NDEBUG
#  define assert(expr) do {} while (0)
#else
#  define assert(expr) \
        if(!(expr)) {					\
        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        #expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#endif


/* A few user-configurable values. */
/* media options */
static int media[] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;

/* Size of the in-memory receive ring. */
#define RX_BUF_LEN_IDX	2	/* 0==8K, 1==16K, 2==32K, 3==64K */
#define RX_BUF_LEN (8192 << RX_BUF_LEN_IDX)
#define RX_BUF_PAD 16
#define RX_BUF_WRAP_PAD 2048 /* spare padding to handle lack of packet wrap */
#define RX_BUF_TOT_LEN (RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP_PAD)

/* Number of Tx descriptor registers. */
#define NUM_TX_DESC	4

/* max supported ethernet frame size -- must be at least (dev->mtu+14+4).*/
#define MAX_ETH_FRAME_SIZE	1536

/* Size of the Tx bounce buffers -- must be at least (dev->mtu+14+4). */
#define TX_BUF_SIZE	MAX_ETH_FRAME_SIZE
#define TX_BUF_TOT_LEN	(TX_BUF_SIZE * NUM_TX_DESC)

/* PCI Tuning Parameters
   Threshold is bytes transferred to chip before transmission starts. */
#define TX_FIFO_THRESH 256	/* In bytes, rounded down to 32 byte units. */

/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024, 7==end of packet. */
#define RX_FIFO_THRESH	6	/* Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 */


/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (6*HZ)


enum {
	HAS_CHIP_XCVR = 0x020000,
	HAS_LNK_CHNG = 0x040000,
};

#define NETDRV_MIN_IO_SIZE 0x80
#define RTL8139B_IO_SIZE 256

#define NETDRV_CAPS	HAS_CHIP_XCVR|HAS_LNK_CHNG

typedef enum {
	RTL8139 = 0,
	NETDRV_CB,
	SMC1211TX,
	/*MPX5030,*/
	DELTA8139,
	ADDTRON8139,
} board_t;


/* indexed by board_t, above */
static struct {
	const char *name;
} board_info[] __devinitdata = {
	{ "RealTek RTL8139 Fast Ethernet" },
	{ "RealTek RTL8139B PCI/CardBus" },
	{ "SMC1211TX EZCard 10/100 (RealTek RTL8139)" },
/*	{ MPX5030, "Accton MPX5030 (RealTek RTL8139)" },*/
	{ "Delta Electronics 8139 10/100BaseTX" },
	{ "Addtron Technolgy 8139 10/100BaseTX" },
};


static struct pci_device_id netdrv_pci_tbl[] = {
	{0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, RTL8139 },
	{0x10ec, 0x8138, PCI_ANY_ID, PCI_ANY_ID, 0, 0, NETDRV_CB },
	{0x1113, 0x1211, PCI_ANY_ID, PCI_ANY_ID, 0, 0, SMC1211TX },
/*	{0x1113, 0x1211, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MPX5030 },*/
	{0x1500, 0x1360, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DELTA8139 },
	{0x4033, 0x1360, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ADDTRON8139 },
	{0,}
};
MODULE_DEVICE_TABLE (pci, netdrv_pci_tbl);


/* The rest of these values should never change. */

/* Symbolic offsets to registers. */
enum NETDRV_registers {
	MAC0 = 0,		/* Ethernet hardware address. */
	MAR0 = 8,		/* Multicast filter. */
	TxStatus0 = 0x10,	/* Transmit status (Four 32bit registers). */
	TxAddr0 = 0x20,		/* Tx descriptors (also four 32bit). */
	RxBuf = 0x30,
	RxEarlyCnt = 0x34,
	RxEarlyStatus = 0x36,
	ChipCmd = 0x37,
	RxBufPtr = 0x38,
	RxBufAddr = 0x3A,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	ChipVersion = 0x43,
	RxConfig = 0x44,
	Timer = 0x48,		/* A general-purpose counter. */
	RxMissed = 0x4C,	/* 24 bits valid, write clears. */
	Cfg9346 = 0x50,
	Config0 = 0x51,
	Config1 = 0x52,
	FlashReg = 0x54,
	MediaStatus = 0x58,
	Config3 = 0x59,
	Config4 = 0x5A,		/* absent on RTL-8139A */
	HltClk = 0x5B,
	MultiIntr = 0x5C,
	TxSummary = 0x60,
	BasicModeCtrl = 0x62,
	BasicModeStatus = 0x64,
	NWayAdvert = 0x66,
	NWayLPAR = 0x68,
	NWayExpansion = 0x6A,
	/* Undocumented registers, but required for proper operation. */
	FIFOTMS = 0x70,		/* FIFO Control and test. */
	CSCR = 0x74,		/* Chip Status and Configuration Register. */
	PARA78 = 0x78,
	PARA7c = 0x7c,		/* Magic transceiver parameter register. */
	Config5 = 0xD8,		/* absent on RTL-8139A */
};

enum ClearBitMasks {
	MultiIntrClear = 0xF000,
	ChipCmdClear = 0xE2,
	Config1Clear = (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1),
};

enum ChipCmdBits {
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
	PCIErr = 0x8000,
	PCSTimeout = 0x4000,
	RxFIFOOver = 0x40,
	RxUnderrun = 0x20,
	RxOverflow = 0x10,
	TxErr = 0x08,
	TxOK = 0x04,
	RxErr = 0x02,
	RxOK = 0x01,
};
enum TxStatusBits {
	TxHostOwns = 0x2000,
	TxUnderrun = 0x4000,
	TxStatOK = 0x8000,
	TxOutOfWindow = 0x20000000,
	TxAborted = 0x40000000,
	TxCarrierLost = 0x80000000,
};
enum RxStatusBits {
	RxMulticast = 0x8000,
	RxPhysical = 0x4000,
	RxBroadcast = 0x2000,
	RxBadSymbol = 0x0020,
	RxRunt = 0x0010,
	RxTooLong = 0x0008,
	RxCRCErr = 0x0004,
	RxBadAlign = 0x0002,
	RxStatusOK = 0x0001,
};

/* Bits in RxConfig. */
enum rx_mode_bits {
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,
};

/* Bits in TxConfig. */
enum tx_config_bits {
	TxIFG1 = (1 << 25),	/* Interframe Gap Time */
	TxIFG0 = (1 << 24),	/* Enabling these bits violates IEEE 802.3 */
	TxLoopBack = (1 << 18) | (1 << 17), /* enable loopback test mode */
	TxCRC = (1 << 16),	/* DISABLE appending CRC to end of Tx packets */
	TxClearAbt = (1 << 0),	/* Clear abort (WO) */
	TxDMAShift = 8,		/* DMA burst value (0-7) is shift this many bits */

	TxVersionMask = 0x7C800000, /* mask out version bits 30-26, 23 */
};

/* Bits in Config1 */
enum Config1Bits {
	Cfg1_PM_Enable = 0x01,
	Cfg1_VPD_Enable = 0x02,
	Cfg1_PIO = 0x04,
	Cfg1_MMIO = 0x08,
	Cfg1_LWAKE = 0x10,
	Cfg1_Driver_Load = 0x20,
	Cfg1_LED0 = 0x40,
	Cfg1_LED1 = 0x80,
};

enum RxConfigBits {
	/* Early Rx threshold, none or X/16 */
	RxCfgEarlyRxNone = 0,
	RxCfgEarlyRxShift = 24,

	/* rx fifo threshold */
	RxCfgFIFOShift = 13,
	RxCfgFIFONone = (7 << RxCfgFIFOShift),

	/* Max DMA burst */
	RxCfgDMAShift = 8,
	RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

	/* rx ring buffer length */
	RxCfgRcv8K = 0,
	RxCfgRcv16K = (1 << 11),
	RxCfgRcv32K = (1 << 12),
	RxCfgRcv64K = (1 << 11) | (1 << 12),

	/* Disable packet wrap at end of Rx buffer */
	RxNoWrap = (1 << 7),
};


/* Twister tuning parameters from RealTek.
   Completely undocumented, but required to tune bad links. */
enum CSCRBits {
	CSCR_LinkOKBit = 0x0400,
	CSCR_LinkChangeBit = 0x0800,
	CSCR_LinkStatusBits = 0x0f000,
	CSCR_LinkDownOffCmd = 0x003c0,
	CSCR_LinkDownCmd = 0x0f3c0,
};


enum Cfg9346Bits {
	Cfg9346_Lock = 0x00,
	Cfg9346_Unlock = 0xC0,
};


#define PARA78_default	0x78fa8388
#define PARA7c_default	0xcb38de43	/* param[0][3] */
#define PARA7c_xxx		0xcb38de43
static const unsigned long param[4][4] = {
	{0xcb39de43, 0xcb39ce43, 0xfb38de03, 0xcb38de43},
	{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
	{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
	{0xbb39de43, 0xbb39ce43, 0xbb39ce83, 0xbb39ce83}
};

struct ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};


typedef enum {
	CH_8139 = 0,
	CH_8139_K,
	CH_8139A,
	CH_8139B,
	CH_8130,
	CH_8139C,
} chip_t;


/* directly indexed by chip_t, above */
static const struct {
	const char *name;
	u8 version; /* from RTL8139C docs */
	u32 RxConfigMask; /* should clear the bits supported by this chip */
} rtl_chip_info[] = {
	{ "RTL-8139",
	  0x40,
	  0xf0fe0040, /* XXX copied from RTL8139A, verify */
	},

	{ "RTL-8139 rev K",
	  0x60,
	  0xf0fe0040,
	},

	{ "RTL-8139A",
	  0x70,
	  0xf0fe0040,
	},

	{ "RTL-8139B",
	  0x78,
	  0xf0fc0040
	},

	{ "RTL-8130",
	  0x7C,
	  0xf0fe0040, /* XXX copied from RTL8139A, verify */
	},

	{ "RTL-8139C",
	  0x74,
	  0xf0fc0040, /* XXX copied from RTL8139B, verify */
	},

};


struct netdrv_private {
	board_t board;
	void *mmio_addr;
	int drv_flags;
	struct pci_dev *pci_dev;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media selection timer. */
	unsigned char *rx_ring;
	unsigned int cur_rx;	/* Index into the Rx buffer of next Rx pkt. */
	unsigned int tx_flag;
	atomic_t cur_tx;
	atomic_t dirty_tx;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct ring_info tx_info[NUM_TX_DESC];
	unsigned char *tx_buf[NUM_TX_DESC];	/* Tx bounce buffers */
	unsigned char *tx_bufs;	/* Tx bounce buffer region. */
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_bufs_dma;
	char phys[4];		/* MII device addresses. */
	char twistie, twist_row, twist_col;	/* Twister tune state. */
	unsigned int full_duplex:1;	/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int default_port:4;	/* Last dev->if_port value. */
	unsigned int media2:4;	/* Secondary monitored media port. */
	unsigned int medialock:1;	/* Don't sense media type. */
	unsigned int mediasense:1;	/* Media sensing in progress. */
	spinlock_t lock;
	chip_t chipset;
};

MODULE_AUTHOR ("Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION ("Skeleton for a PCI Fast Ethernet driver");
MODULE_LICENSE("GPL");
module_param(multicast_filter_limit, int, 0);
module_param(max_interrupt_work, int, 0);
module_param_array(media, int, NULL, 0);
MODULE_PARM_DESC (multicast_filter_limit, "pci-skeleton maximum number of filtered multicast addresses");
MODULE_PARM_DESC (max_interrupt_work, "pci-skeleton maximum events handled per interrupt");
MODULE_PARM_DESC (media, "pci-skeleton: Bits 0-3: media type, bit 17: full duplex");

static int read_eeprom (void *ioaddr, int location, int addr_len);
static int netdrv_open (struct net_device *dev);
static int mdio_read (struct net_device *dev, int phy_id, int location);
static void mdio_write (struct net_device *dev, int phy_id, int location,
			int val);
static void netdrv_timer (unsigned long data);
static void netdrv_tx_timeout (struct net_device *dev);
static void netdrv_init_ring (struct net_device *dev);
static int netdrv_start_xmit (struct sk_buff *skb,
			       struct net_device *dev);
static irqreturn_t netdrv_interrupt (int irq, void *dev_instance);
static int netdrv_close (struct net_device *dev);
static int netdrv_ioctl (struct net_device *dev, struct ifreq *rq, int cmd);
static struct net_device_stats *netdrv_get_stats (struct net_device *dev);
static void netdrv_set_rx_mode (struct net_device *dev);
static void netdrv_hw_start (struct net_device *dev);


#ifdef USE_IO_OPS

#define NETDRV_R8(reg)		inb (((unsigned long)ioaddr) + (reg))
#define NETDRV_R16(reg)		inw (((unsigned long)ioaddr) + (reg))
#define NETDRV_R32(reg)		((unsigned long) inl (((unsigned long)ioaddr) + (reg)))
#define NETDRV_W8(reg, val8)	outb ((val8), ((unsigned long)ioaddr) + (reg))
#define NETDRV_W16(reg, val16)	outw ((val16), ((unsigned long)ioaddr) + (reg))
#define NETDRV_W32(reg, val32)	outl ((val32), ((unsigned long)ioaddr) + (reg))
#define NETDRV_W8_F		NETDRV_W8
#define NETDRV_W16_F		NETDRV_W16
#define NETDRV_W32_F		NETDRV_W32
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel
#define readb(addr) inb((unsigned long)(addr))
#define readw(addr) inw((unsigned long)(addr))
#define readl(addr) inl((unsigned long)(addr))
#define writeb(val,addr) outb((val),(unsigned long)(addr))
#define writew(val,addr) outw((val),(unsigned long)(addr))
#define writel(val,addr) outl((val),(unsigned long)(addr))

#else

/* write MMIO register, with flush */
/* Flush avoids rtl8139 bug w/ posted MMIO writes */
#define NETDRV_W8_F(reg, val8)	do { writeb ((val8), ioaddr + (reg)); readb (ioaddr + (reg)); } while (0)
#define NETDRV_W16_F(reg, val16)	do { writew ((val16), ioaddr + (reg)); readw (ioaddr + (reg)); } while (0)
#define NETDRV_W32_F(reg, val32)	do { writel ((val32), ioaddr + (reg)); readl (ioaddr + (reg)); } while (0)


#if MMIO_FLUSH_AUDIT_COMPLETE

/* write MMIO register */
#define NETDRV_W8(reg, val8)	writeb ((val8), ioaddr + (reg))
#define NETDRV_W16(reg, val16)	writew ((val16), ioaddr + (reg))
#define NETDRV_W32(reg, val32)	writel ((val32), ioaddr + (reg))

#else

/* write MMIO register, then flush */
#define NETDRV_W8		NETDRV_W8_F
#define NETDRV_W16		NETDRV_W16_F
#define NETDRV_W32		NETDRV_W32_F

#endif /* MMIO_FLUSH_AUDIT_COMPLETE */

/* read MMIO register */
#define NETDRV_R8(reg)		readb (ioaddr + (reg))
#define NETDRV_R16(reg)		readw (ioaddr + (reg))
#define NETDRV_R32(reg)		((unsigned long) readl (ioaddr + (reg)))

#endif /* USE_IO_OPS */


static const u16 netdrv_intr_mask =
	PCIErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver |
	TxErr | TxOK | RxErr | RxOK;

static const unsigned int netdrv_rx_config =
	  RxCfgEarlyRxNone | RxCfgRcv32K | RxNoWrap |
	  (RX_FIFO_THRESH << RxCfgFIFOShift) |
	  (RX_DMA_BURST << RxCfgDMAShift);


static int __devinit netdrv_init_board (struct pci_dev *pdev,
					 struct net_device **dev_out,
					 void **ioaddr_out)
{
	void *ioaddr = NULL;
	struct net_device *dev;
	struct netdrv_private *tp;
	int rc, i;
	u32 pio_start, pio_end, pio_flags, pio_len;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;
	u32 tmp;

	DPRINTK ("ENTER\n");

	assert (pdev != NULL);
	assert (ioaddr_out != NULL);

	*ioaddr_out = NULL;
	*dev_out = NULL;

	/* dev zeroed in alloc_etherdev */
	dev = alloc_etherdev (sizeof (*tp));
	if (dev == NULL) {
		dev_err(&pdev->dev, "unable to alloc new ethernet\n");
		DPRINTK ("EXIT, returning -ENOMEM\n");
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	tp = dev->priv;

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;

	pio_start = pci_resource_start (pdev, 0);
	pio_end = pci_resource_end (pdev, 0);
	pio_flags = pci_resource_flags (pdev, 0);
	pio_len = pci_resource_len (pdev, 0);

	mmio_start = pci_resource_start (pdev, 1);
	mmio_end = pci_resource_end (pdev, 1);
	mmio_flags = pci_resource_flags (pdev, 1);
	mmio_len = pci_resource_len (pdev, 1);

	/* set this immediately, we need to know before
	 * we talk to the chip directly */
	DPRINTK("PIO region size == 0x%02X\n", pio_len);
	DPRINTK("MMIO region size == 0x%02lX\n", mmio_len);

	/* make sure PCI base addr 0 is PIO */
	if (!(pio_flags & IORESOURCE_IO)) {
		dev_err(&pdev->dev, "region #0 not a PIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	/* make sure PCI base addr 1 is MMIO */
	if (!(mmio_flags & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "region #1 not an MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	/* check for weird/broken PCI region reporting */
	if ((pio_len < NETDRV_MIN_IO_SIZE) ||
	    (mmio_len < NETDRV_MIN_IO_SIZE)) {
		dev_err(&pdev->dev, "Invalid PCI region size(s), aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	rc = pci_request_regions (pdev, MODNAME);
	if (rc)
		goto err_out;

	pci_set_master (pdev);

#ifdef USE_IO_OPS
	ioaddr = (void *) pio_start;
#else
	/* ioremap MMIO region */
	ioaddr = ioremap (mmio_start, mmio_len);
	if (ioaddr == NULL) {
		dev_err(&pdev->dev, "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out_free_res;
	}
#endif /* USE_IO_OPS */

	/* Soft reset the chip. */
	NETDRV_W8 (ChipCmd, (NETDRV_R8 (ChipCmd) & ChipCmdClear) | CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--)
		if ((NETDRV_R8 (ChipCmd) & CmdReset) == 0)
			break;
		else
			udelay (10);

	/* Bring the chip out of low-power mode. */
	/* <insert device-specific code here> */

#ifndef USE_IO_OPS
	/* sanity checks -- ensure PIO and MMIO registers agree */
	assert (inb (pio_start+Config0) == readb (ioaddr+Config0));
	assert (inb (pio_start+Config1) == readb (ioaddr+Config1));
	assert (inb (pio_start+TxConfig) == readb (ioaddr+TxConfig));
	assert (inb (pio_start+RxConfig) == readb (ioaddr+RxConfig));
#endif /* !USE_IO_OPS */

	/* identify chip attached to board */
	tmp = NETDRV_R8 (ChipVersion);
	for (i = ARRAY_SIZE (rtl_chip_info) - 1; i >= 0; i--)
		if (tmp == rtl_chip_info[i].version) {
			tp->chipset = i;
			goto match;
		}

	/* if unknown chip, assume array element #0, original RTL-8139 in this case */
	dev_printk (KERN_DEBUG, &pdev->dev,
		"unknown chip version, assuming RTL-8139\n");
	dev_printk (KERN_DEBUG, &pdev->dev, "TxConfig = 0x%lx\n",
		NETDRV_R32 (TxConfig));
	tp->chipset = 0;

match:
	DPRINTK ("chipset id (%d) == index %d, '%s'\n",
		tmp,
		tp->chipset,
		rtl_chip_info[tp->chipset].name);

	rc = register_netdev (dev);
	if (rc)
		goto err_out_unmap;

	DPRINTK ("EXIT, returning 0\n");
	*ioaddr_out = ioaddr;
	*dev_out = dev;
	return 0;

err_out_unmap:
#ifndef USE_IO_OPS
	iounmap(ioaddr);
err_out_free_res:
#endif
	pci_release_regions (pdev);
err_out:
	free_netdev (dev);
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static int __devinit netdrv_init_one (struct pci_dev *pdev,
				       const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct netdrv_private *tp;
	int i, addr_len, option;
	void *ioaddr = NULL;
	static int board_idx = -1;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	DPRINTK ("ENTER\n");

	assert (pdev != NULL);
	assert (ent != NULL);

	board_idx++;

	i = netdrv_init_board (pdev, &dev, &ioaddr);
	if (i < 0) {
		DPRINTK ("EXIT, returning %d\n", i);
		return i;
	}

	tp = dev->priv;

	assert (ioaddr != NULL);
	assert (dev != NULL);
	assert (tp != NULL);

	addr_len = read_eeprom (ioaddr, 0, 8) == 0x8129 ? 8 : 6;
	for (i = 0; i < 3; i++)
		((u16 *) (dev->dev_addr))[i] =
		    le16_to_cpu (read_eeprom (ioaddr, i + 7, addr_len));

	/* The Rtl8139-specific entries in the device structure. */
	dev->open = netdrv_open;
	dev->hard_start_xmit = netdrv_start_xmit;
	dev->stop = netdrv_close;
	dev->get_stats = netdrv_get_stats;
	dev->set_multicast_list = netdrv_set_rx_mode;
	dev->do_ioctl = netdrv_ioctl;
	dev->tx_timeout = netdrv_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) ioaddr;

	/* dev->priv/tp zeroed and aligned in alloc_etherdev */
	tp = dev->priv;

	/* note: tp->chipset set in netdrv_init_board */
	tp->drv_flags = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER | NETDRV_CAPS;
	tp->pci_dev = pdev;
	tp->board = ent->driver_data;
	tp->mmio_addr = ioaddr;
	spin_lock_init(&tp->lock);

	pci_set_drvdata(pdev, dev);

	tp->phys[0] = 32;

	printk (KERN_INFO "%s: %s at 0x%lx, "
		"%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
		"IRQ %d\n",
		dev->name,
		board_info[ent->driver_data].name,
		dev->base_addr,
		dev->dev_addr[0], dev->dev_addr[1],
		dev->dev_addr[2], dev->dev_addr[3],
		dev->dev_addr[4], dev->dev_addr[5],
		dev->irq);

	printk (KERN_DEBUG "%s:  Identified 8139 chip type '%s'\n",
		dev->name, rtl_chip_info[tp->chipset].name);

	/* Put the chip into low-power mode. */
	NETDRV_W8_F (Cfg9346, Cfg9346_Unlock);

	/* The lower four bits are the media type. */
	option = (board_idx > 7) ? 0 : media[board_idx];
	if (option > 0) {
		tp->full_duplex = (option & 0x200) ? 1 : 0;
		tp->default_port = option & 15;
		if (tp->default_port)
			tp->medialock = 1;
	}

	if (tp->full_duplex) {
		printk (KERN_INFO
			"%s: Media type forced to Full Duplex.\n",
			dev->name);
		mdio_write (dev, tp->phys[0], MII_ADVERTISE, ADVERTISE_FULL);
		tp->duplex_lock = 1;
	}

	DPRINTK ("EXIT - returning 0\n");
	return 0;
}


static void __devexit netdrv_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdrv_private *np;

	DPRINTK ("ENTER\n");

	assert (dev != NULL);

	np = dev->priv;
	assert (np != NULL);

	unregister_netdev (dev);

#ifndef USE_IO_OPS
	iounmap (np->mmio_addr);
#endif /* !USE_IO_OPS */

	pci_release_regions (pdev);

	free_netdev (dev);

	pci_set_drvdata (pdev, NULL);

	pci_disable_device (pdev);

	DPRINTK ("EXIT\n");
}


/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS			0x08	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x02	/* EEPROM chip data in. */
#define EE_WRITE_0		0x00
#define EE_WRITE_1		0x02
#define EE_DATA_READ	0x01	/* EEPROM chip data out. */
#define EE_ENB			(0x80 | EE_CS)

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but 66Mhz may change this.
 */

#define eeprom_delay()	readl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5)
#define EE_READ_CMD		(6)
#define EE_ERASE_CMD	(7)

static int __devinit read_eeprom (void *ioaddr, int location, int addr_len)
{
	int i;
	unsigned retval = 0;
	void *ee_addr = ioaddr + Cfg9346;
	int read_cmd = location | (EE_READ_CMD << addr_len);

	DPRINTK ("ENTER\n");

	writeb (EE_ENB & ~EE_CS, ee_addr);
	writeb (EE_ENB, ee_addr);
	eeprom_delay ();

	/* Shift the read command bits out. */
	for (i = 4 + addr_len; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		writeb (EE_ENB | dataval, ee_addr);
		eeprom_delay ();
		writeb (EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay ();
	}
	writeb (EE_ENB, ee_addr);
	eeprom_delay ();

	for (i = 16; i > 0; i--) {
		writeb (EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay ();
		retval =
		    (retval << 1) | ((readb (ee_addr) & EE_DATA_READ) ? 1 :
				     0);
		writeb (EE_ENB, ee_addr);
		eeprom_delay ();
	}

	/* Terminate the EEPROM access. */
	writeb (~EE_CS, ee_addr);
	eeprom_delay ();

	DPRINTK ("EXIT - returning %d\n", retval);
	return retval;
}

/* MII serial management: mostly bogus for now. */
/* Read and write the MII management registers using software-generated
   serial MDIO protocol.
   The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues. */
#define MDIO_DIR		0x80
#define MDIO_DATA_OUT	0x04
#define MDIO_DATA_IN	0x02
#define MDIO_CLK		0x01
#define MDIO_WRITE0 (MDIO_DIR)
#define MDIO_WRITE1 (MDIO_DIR | MDIO_DATA_OUT)

#define mdio_delay()	readb(mdio_addr)


static char mii_2_8139_map[8] = {
	BasicModeCtrl,
	BasicModeStatus,
	0,
	0,
	NWayAdvert,
	NWayLPAR,
	NWayExpansion,
	0
};


/* Syncronize the MII management interface by shifting 32 one bits out. */
static void mdio_sync (void *mdio_addr)
{
	int i;

	DPRINTK ("ENTER\n");

	for (i = 32; i >= 0; i--) {
		writeb (MDIO_WRITE1, mdio_addr);
		mdio_delay ();
		writeb (MDIO_WRITE1 | MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	DPRINTK ("EXIT\n");
}


static int mdio_read (struct net_device *dev, int phy_id, int location)
{
	struct netdrv_private *tp = dev->priv;
	void *mdio_addr = tp->mmio_addr + Config4;
	int mii_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	int i;

	DPRINTK ("ENTER\n");

	if (phy_id > 31) {	/* Really a 8139.  Use internal registers. */
		DPRINTK ("EXIT after directly using 8139 internal regs\n");
		return location < 8 && mii_2_8139_map[location] ?
		    readw (tp->mmio_addr + mii_2_8139_map[location]) : 0;
	}
	mdio_sync (mdio_addr);
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDIO_DATA_OUT : 0;

		writeb (MDIO_DIR | dataval, mdio_addr);
		mdio_delay ();
		writeb (MDIO_DIR | dataval | MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		writeb (0, mdio_addr);
		mdio_delay ();
		retval =
		    (retval << 1) | ((readb (mdio_addr) & MDIO_DATA_IN) ? 1
				     : 0);
		writeb (MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	DPRINTK ("EXIT, returning %d\n", (retval >> 1) & 0xffff);
	return (retval >> 1) & 0xffff;
}


static void mdio_write (struct net_device *dev, int phy_id, int location,
			int value)
{
	struct netdrv_private *tp = dev->priv;
	void *mdio_addr = tp->mmio_addr + Config4;
	int mii_cmd =
	    (0x5002 << 16) | (phy_id << 23) | (location << 18) | value;
	int i;

	DPRINTK ("ENTER\n");

	if (phy_id > 31) {	/* Really a 8139.  Use internal registers. */
		if (location < 8 && mii_2_8139_map[location]) {
			writew (value,
				tp->mmio_addr + mii_2_8139_map[location]);
			readw (tp->mmio_addr + mii_2_8139_map[location]);
		}
		DPRINTK ("EXIT after directly using 8139 internal regs\n");
		return;
	}
	mdio_sync (mdio_addr);

	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval =
		    (mii_cmd & (1 << i)) ? MDIO_WRITE1 : MDIO_WRITE0;
		writeb (dataval, mdio_addr);
		mdio_delay ();
		writeb (dataval | MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		writeb (0, mdio_addr);
		mdio_delay ();
		writeb (MDIO_CLK, mdio_addr);
		mdio_delay ();
	}

	DPRINTK ("EXIT\n");
}


static int netdrv_open (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	int retval;
#ifdef NETDRV_DEBUG
	void *ioaddr = tp->mmio_addr;
#endif

	DPRINTK ("ENTER\n");

	retval = request_irq (dev->irq, netdrv_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval) {
		DPRINTK ("EXIT, returning %d\n", retval);
		return retval;
	}

	tp->tx_bufs = pci_alloc_consistent(tp->pci_dev, TX_BUF_TOT_LEN,
					   &tp->tx_bufs_dma);
	tp->rx_ring = pci_alloc_consistent(tp->pci_dev, RX_BUF_TOT_LEN,
					   &tp->rx_ring_dma);
	if (tp->tx_bufs == NULL || tp->rx_ring == NULL) {
		free_irq(dev->irq, dev);

		if (tp->tx_bufs)
			pci_free_consistent(tp->pci_dev, TX_BUF_TOT_LEN,
					    tp->tx_bufs, tp->tx_bufs_dma);
		if (tp->rx_ring)
			pci_free_consistent(tp->pci_dev, RX_BUF_TOT_LEN,
					    tp->rx_ring, tp->rx_ring_dma);

		DPRINTK ("EXIT, returning -ENOMEM\n");
		return -ENOMEM;

	}

	tp->full_duplex = tp->duplex_lock;
	tp->tx_flag = (TX_FIFO_THRESH << 11) & 0x003f0000;

	netdrv_init_ring (dev);
	netdrv_hw_start (dev);

	DPRINTK ("%s: netdrv_open() ioaddr %#lx IRQ %d"
			" GP Pins %2.2x %s-duplex.\n",
			dev->name, pci_resource_start (tp->pci_dev, 1),
			dev->irq, NETDRV_R8 (MediaStatus),
			tp->full_duplex ? "full" : "half");

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer (&tp->timer);
	tp->timer.expires = jiffies + 3 * HZ;
	tp->timer.data = (unsigned long) dev;
	tp->timer.function = &netdrv_timer;
	add_timer (&tp->timer);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/* Start the hardware at open or resume. */
static void netdrv_hw_start (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	u32 i;

	DPRINTK ("ENTER\n");

	/* Soft reset the chip. */
	NETDRV_W8 (ChipCmd, (NETDRV_R8 (ChipCmd) & ChipCmdClear) | CmdReset);
	udelay (100);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--)
		if ((NETDRV_R8 (ChipCmd) & CmdReset) == 0)
			break;

	/* Restore our idea of the MAC address. */
	NETDRV_W32_F (MAC0 + 0, cpu_to_le32 (*(u32 *) (dev->dev_addr + 0)));
	NETDRV_W32_F (MAC0 + 4, cpu_to_le32 (*(u32 *) (dev->dev_addr + 4)));

	/* Must enable Tx/Rx before setting transfer thresholds! */
	NETDRV_W8_F (ChipCmd, (NETDRV_R8 (ChipCmd) & ChipCmdClear) |
			   CmdRxEnb | CmdTxEnb);

	i = netdrv_rx_config |
	    (NETDRV_R32 (RxConfig) & rtl_chip_info[tp->chipset].RxConfigMask);
	NETDRV_W32_F (RxConfig, i);

	/* Check this value: the documentation for IFG contradicts ifself. */
	NETDRV_W32 (TxConfig, (TX_DMA_BURST << TxDMAShift));

	/* unlock Config[01234] and BMCR register writes */
	NETDRV_W8_F (Cfg9346, Cfg9346_Unlock);
	udelay (10);

	tp->cur_rx = 0;

	/* Lock Config[01234] and BMCR register writes */
	NETDRV_W8_F (Cfg9346, Cfg9346_Lock);
	udelay (10);

	/* init Rx ring buffer DMA address */
	NETDRV_W32_F (RxBuf, tp->rx_ring_dma);

	/* init Tx buffer DMA addresses */
	for (i = 0; i < NUM_TX_DESC; i++)
		NETDRV_W32_F (TxAddr0 + (i * 4), tp->tx_bufs_dma + (tp->tx_buf[i] - tp->tx_bufs));

	NETDRV_W32_F (RxMissed, 0);

	netdrv_set_rx_mode (dev);

	/* no early-rx interrupts */
	NETDRV_W16 (MultiIntr, NETDRV_R16 (MultiIntr) & MultiIntrClear);

	/* make sure RxTx has started */
	NETDRV_W8_F (ChipCmd, (NETDRV_R8 (ChipCmd) & ChipCmdClear) |
			   CmdRxEnb | CmdTxEnb);

	/* Enable all known interrupts by setting the interrupt mask. */
	NETDRV_W16_F (IntrMask, netdrv_intr_mask);

	netif_start_queue (dev);

	DPRINTK ("EXIT\n");
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void netdrv_init_ring (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	int i;

	DPRINTK ("ENTER\n");

	tp->cur_rx = 0;
	atomic_set (&tp->cur_tx, 0);
	atomic_set (&tp->dirty_tx, 0);

	for (i = 0; i < NUM_TX_DESC; i++) {
		tp->tx_info[i].skb = NULL;
		tp->tx_info[i].mapping = 0;
		tp->tx_buf[i] = &tp->tx_bufs[i * TX_BUF_SIZE];
	}

	DPRINTK ("EXIT\n");
}


static void netdrv_timer (unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	int next_tick = 60 * HZ;
	int mii_lpa;

	mii_lpa = mdio_read (dev, tp->phys[0], MII_LPA);

	if (!tp->duplex_lock && mii_lpa != 0xffff) {
		int duplex = (mii_lpa & LPA_100FULL)
		    || (mii_lpa & 0x01C0) == 0x0040;
		if (tp->full_duplex != duplex) {
			tp->full_duplex = duplex;
			printk (KERN_INFO
				"%s: Setting %s-duplex based on MII #%d link"
				" partner ability of %4.4x.\n", dev->name,
				tp->full_duplex ? "full" : "half",
				tp->phys[0], mii_lpa);
			NETDRV_W8 (Cfg9346, Cfg9346_Unlock);
			NETDRV_W8 (Config1, tp->full_duplex ? 0x60 : 0x20);
			NETDRV_W8 (Cfg9346, Cfg9346_Lock);
		}
	}

	DPRINTK ("%s: Media selection tick, Link partner %4.4x.\n",
		 dev->name, NETDRV_R16 (NWayLPAR));
	DPRINTK ("%s:  Other registers are IntMask %4.4x IntStatus %4.4x"
		 " RxStatus %4.4x.\n", dev->name,
		 NETDRV_R16 (IntrMask),
		 NETDRV_R16 (IntrStatus),
		 NETDRV_R32 (RxEarlyStatus));
	DPRINTK ("%s:  Chip config %2.2x %2.2x.\n",
		 dev->name, NETDRV_R8 (Config0),
		 NETDRV_R8 (Config1));

	tp->timer.expires = jiffies + next_tick;
	add_timer (&tp->timer);
}


static void netdrv_tx_clear (struct netdrv_private *tp)
{
	int i;

	atomic_set (&tp->cur_tx, 0);
	atomic_set (&tp->dirty_tx, 0);

	/* Dump the unsent Tx packets. */
	for (i = 0; i < NUM_TX_DESC; i++) {
		struct ring_info *rp = &tp->tx_info[i];
		if (rp->mapping != 0) {
			pci_unmap_single (tp->pci_dev, rp->mapping,
					  rp->skb->len, PCI_DMA_TODEVICE);
			rp->mapping = 0;
		}
		if (rp->skb) {
			dev_kfree_skb (rp->skb);
			rp->skb = NULL;
			tp->stats.tx_dropped++;
		}
	}
}


static void netdrv_tx_timeout (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	int i;
	u8 tmp8;
	unsigned long flags;

	DPRINTK ("%s: Transmit timeout, status %2.2x %4.4x "
		 "media %2.2x.\n", dev->name,
		 NETDRV_R8 (ChipCmd),
		 NETDRV_R16 (IntrStatus),
		 NETDRV_R8 (MediaStatus));

	/* disable Tx ASAP, if not already */
	tmp8 = NETDRV_R8 (ChipCmd);
	if (tmp8 & CmdTxEnb)
		NETDRV_W8 (ChipCmd, tmp8 & ~CmdTxEnb);

	/* Disable interrupts by clearing the interrupt mask. */
	NETDRV_W16 (IntrMask, 0x0000);

	/* Emit info to figure out what went wrong. */
	printk (KERN_DEBUG "%s: Tx queue start entry %d  dirty entry %d.\n",
		dev->name, atomic_read (&tp->cur_tx),
		atomic_read (&tp->dirty_tx));
	for (i = 0; i < NUM_TX_DESC; i++)
		printk (KERN_DEBUG "%s:  Tx descriptor %d is %8.8lx.%s\n",
			dev->name, i, NETDRV_R32 (TxStatus0 + (i * 4)),
			i == atomic_read (&tp->dirty_tx) % NUM_TX_DESC ?
				" (queue head)" : "");

	/* Stop a shared interrupt from scavenging while we are. */
	spin_lock_irqsave (&tp->lock, flags);

	netdrv_tx_clear (tp);

	spin_unlock_irqrestore (&tp->lock, flags);

	/* ...and finally, reset everything */
	netdrv_hw_start (dev);

	netif_wake_queue (dev);
}



static int netdrv_start_xmit (struct sk_buff *skb, struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	int entry;

	/* Calculate the next Tx descriptor entry. */
	entry = atomic_read (&tp->cur_tx) % NUM_TX_DESC;

	assert (tp->tx_info[entry].skb == NULL);
	assert (tp->tx_info[entry].mapping == 0);

	tp->tx_info[entry].skb = skb;
	/* tp->tx_info[entry].mapping = 0; */
	memcpy (tp->tx_buf[entry], skb->data, skb->len);

	/* Note: the chip doesn't have auto-pad! */
	NETDRV_W32 (TxStatus0 + (entry * sizeof(u32)),
		 tp->tx_flag | (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN));

	dev->trans_start = jiffies;
	atomic_inc (&tp->cur_tx);
	if ((atomic_read (&tp->cur_tx) - atomic_read (&tp->dirty_tx)) >= NUM_TX_DESC)
		netif_stop_queue (dev);

	DPRINTK ("%s: Queued Tx packet at %p size %u to slot %d.\n",
		 dev->name, skb->data, skb->len, entry);

	return 0;
}


static void netdrv_tx_interrupt (struct net_device *dev,
				  struct netdrv_private *tp,
				  void *ioaddr)
{
	int cur_tx, dirty_tx, tx_left;

	assert (dev != NULL);
	assert (tp != NULL);
	assert (ioaddr != NULL);

	dirty_tx = atomic_read (&tp->dirty_tx);

	cur_tx = atomic_read (&tp->cur_tx);
	tx_left = cur_tx - dirty_tx;
	while (tx_left > 0) {
		int entry = dirty_tx % NUM_TX_DESC;
		int txstatus;

		txstatus = NETDRV_R32 (TxStatus0 + (entry * sizeof (u32)));

		if (!(txstatus & (TxStatOK | TxUnderrun | TxAborted)))
			break;	/* It still hasn't been Txed */

		/* Note: TxCarrierLost is always asserted at 100mbps. */
		if (txstatus & (TxOutOfWindow | TxAborted)) {
			/* There was an major error, log it. */
			DPRINTK ("%s: Transmit error, Tx status %8.8x.\n",
				 dev->name, txstatus);
			tp->stats.tx_errors++;
			if (txstatus & TxAborted) {
				tp->stats.tx_aborted_errors++;
				NETDRV_W32 (TxConfig, TxClearAbt | (TX_DMA_BURST << TxDMAShift));
			}
			if (txstatus & TxCarrierLost)
				tp->stats.tx_carrier_errors++;
			if (txstatus & TxOutOfWindow)
				tp->stats.tx_window_errors++;
		} else {
			if (txstatus & TxUnderrun) {
				/* Add 64 to the Tx FIFO threshold. */
				if (tp->tx_flag < 0x00300000)
					tp->tx_flag += 0x00020000;
				tp->stats.tx_fifo_errors++;
			}
			tp->stats.collisions += (txstatus >> 24) & 15;
			tp->stats.tx_bytes += txstatus & 0x7ff;
			tp->stats.tx_packets++;
		}

		/* Free the original skb. */
		if (tp->tx_info[entry].mapping != 0) {
			pci_unmap_single(tp->pci_dev,
					 tp->tx_info[entry].mapping,
					 tp->tx_info[entry].skb->len,
					 PCI_DMA_TODEVICE);
			tp->tx_info[entry].mapping = 0;
		}
		dev_kfree_skb_irq (tp->tx_info[entry].skb);
		tp->tx_info[entry].skb = NULL;
		dirty_tx++;
		if (dirty_tx < 0) { /* handle signed int overflow */
			atomic_sub (cur_tx, &tp->cur_tx); /* XXX racy? */
			dirty_tx = cur_tx - tx_left + 1;
		}
		if (netif_queue_stopped (dev))
			netif_wake_queue (dev);

		cur_tx = atomic_read (&tp->cur_tx);
		tx_left = cur_tx - dirty_tx;

	}

#ifndef NETDRV_NDEBUG
	if (atomic_read (&tp->cur_tx) - dirty_tx > NUM_TX_DESC) {
		printk (KERN_ERR
		  "%s: Out-of-sync dirty pointer, %d vs. %d.\n",
		     dev->name, dirty_tx, atomic_read (&tp->cur_tx));
		dirty_tx += NUM_TX_DESC;
	}
#endif /* NETDRV_NDEBUG */

	atomic_set (&tp->dirty_tx, dirty_tx);
}


/* TODO: clean this up!  Rx reset need not be this intensive */
static void netdrv_rx_err (u32 rx_status, struct net_device *dev,
			    struct netdrv_private *tp, void *ioaddr)
{
	u8 tmp8;
	int tmp_work = 1000;

	DPRINTK ("%s: Ethernet frame had errors, status %8.8x.\n",
	         dev->name, rx_status);
	if (rx_status & RxTooLong) {
		DPRINTK ("%s: Oversized Ethernet frame, status %4.4x!\n",
			 dev->name, rx_status);
		/* A.C.: The chip hangs here. */
	}
	tp->stats.rx_errors++;
	if (rx_status & (RxBadSymbol | RxBadAlign))
		tp->stats.rx_frame_errors++;
	if (rx_status & (RxRunt | RxTooLong))
		tp->stats.rx_length_errors++;
	if (rx_status & RxCRCErr)
		tp->stats.rx_crc_errors++;
	/* Reset the receiver, based on RealTek recommendation. (Bug?) */
	tp->cur_rx = 0;

	/* disable receive */
	tmp8 = NETDRV_R8 (ChipCmd) & ChipCmdClear;
	NETDRV_W8_F (ChipCmd, tmp8 | CmdTxEnb);

	/* A.C.: Reset the multicast list. */
	netdrv_set_rx_mode (dev);

	/* XXX potentially temporary hack to
	 * restart hung receiver */
	while (--tmp_work > 0) {
		tmp8 = NETDRV_R8 (ChipCmd);
		if ((tmp8 & CmdRxEnb) && (tmp8 & CmdTxEnb))
			break;
		NETDRV_W8_F (ChipCmd,
			  (tmp8 & ChipCmdClear) | CmdRxEnb | CmdTxEnb);
	}

	/* G.S.: Re-enable receiver */
	/* XXX temporary hack to work around receiver hang */
	netdrv_set_rx_mode (dev);

	if (tmp_work <= 0)
		printk (KERN_WARNING PFX "tx/rx enable wait too long\n");
}


/* The data sheet doesn't describe the Rx ring at all, so I'm guessing at the
   field alignments and semantics. */
static void netdrv_rx_interrupt (struct net_device *dev,
				  struct netdrv_private *tp, void *ioaddr)
{
	unsigned char *rx_ring;
	u16 cur_rx;

	assert (dev != NULL);
	assert (tp != NULL);
	assert (ioaddr != NULL);

	rx_ring = tp->rx_ring;
	cur_rx = tp->cur_rx;

	DPRINTK ("%s: In netdrv_rx(), current %4.4x BufAddr %4.4x,"
		 " free to %4.4x, Cmd %2.2x.\n", dev->name, cur_rx,
		 NETDRV_R16 (RxBufAddr),
		 NETDRV_R16 (RxBufPtr), NETDRV_R8 (ChipCmd));

	while ((NETDRV_R8 (ChipCmd) & RxBufEmpty) == 0) {
		int ring_offset = cur_rx % RX_BUF_LEN;
		u32 rx_status;
		unsigned int rx_size;
		unsigned int pkt_size;
		struct sk_buff *skb;

		/* read size+status of next frame from DMA ring buffer */
		rx_status = le32_to_cpu (*(u32 *) (rx_ring + ring_offset));
		rx_size = rx_status >> 16;
		pkt_size = rx_size - 4;

		DPRINTK ("%s:  netdrv_rx() status %4.4x, size %4.4x,"
			 " cur %4.4x.\n", dev->name, rx_status,
			 rx_size, cur_rx);
#if NETDRV_DEBUG > 2
		{
			int i;
			DPRINTK ("%s: Frame contents ", dev->name);
			for (i = 0; i < 70; i++)
				printk (" %2.2x",
					rx_ring[ring_offset + i]);
			printk (".\n");
		}
#endif

		/* If Rx err or invalid rx_size/rx_status received
		 * (which happens if we get lost in the ring),
		 * Rx process gets reset, so we abort any further
		 * Rx processing.
		 */
		if ((rx_size > (MAX_ETH_FRAME_SIZE+4)) ||
		    (!(rx_status & RxStatusOK))) {
			netdrv_rx_err (rx_status, dev, tp, ioaddr);
			return;
		}

		/* Malloc up new buffer, compatible with net-2e. */
		/* Omit the four octet CRC from the length. */

		/* TODO: consider allocating skb's outside of
		 * interrupt context, both to speed interrupt processing,
		 * and also to reduce the chances of having to
		 * drop packets here under memory pressure.
		 */

		skb = dev_alloc_skb (pkt_size + 2);
		if (skb) {
			skb->dev = dev;
			skb_reserve (skb, 2);	/* 16 byte align the IP fields. */

			eth_copy_and_sum (skb, &rx_ring[ring_offset + 4], pkt_size, 0);
			skb_put (skb, pkt_size);

			skb->protocol = eth_type_trans (skb, dev);
			netif_rx (skb);
			dev->last_rx = jiffies;
			tp->stats.rx_bytes += pkt_size;
			tp->stats.rx_packets++;
		} else {
			printk (KERN_WARNING
				"%s: Memory squeeze, dropping packet.\n",
				dev->name);
			tp->stats.rx_dropped++;
		}

		cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;
		NETDRV_W16_F (RxBufPtr, cur_rx - 16);
	}

	DPRINTK ("%s: Done netdrv_rx(), current %4.4x BufAddr %4.4x,"
		 " free to %4.4x, Cmd %2.2x.\n", dev->name, cur_rx,
		 NETDRV_R16 (RxBufAddr),
		 NETDRV_R16 (RxBufPtr), NETDRV_R8 (ChipCmd));

	tp->cur_rx = cur_rx;
}


static void netdrv_weird_interrupt (struct net_device *dev,
				     struct netdrv_private *tp,
				     void *ioaddr,
				     int status, int link_changed)
{
	printk (KERN_DEBUG "%s: Abnormal interrupt, status %8.8x.\n",
		dev->name, status);

	assert (dev != NULL);
	assert (tp != NULL);
	assert (ioaddr != NULL);

	/* Update the error count. */
	tp->stats.rx_missed_errors += NETDRV_R32 (RxMissed);
	NETDRV_W32 (RxMissed, 0);

	if ((status & RxUnderrun) && link_changed &&
	    (tp->drv_flags & HAS_LNK_CHNG)) {
		/* Really link-change on new chips. */
		int lpar = NETDRV_R16 (NWayLPAR);
		int duplex = (lpar & 0x0100) || (lpar & 0x01C0) == 0x0040
				|| tp->duplex_lock;
		if (tp->full_duplex != duplex) {
			tp->full_duplex = duplex;
			NETDRV_W8 (Cfg9346, Cfg9346_Unlock);
			NETDRV_W8 (Config1, tp->full_duplex ? 0x60 : 0x20);
			NETDRV_W8 (Cfg9346, Cfg9346_Lock);
		}
		status &= ~RxUnderrun;
	}

	/* XXX along with netdrv_rx_err, are we double-counting errors? */
	if (status &
	    (RxUnderrun | RxOverflow | RxErr | RxFIFOOver))
		tp->stats.rx_errors++;

	if (status & (PCSTimeout))
		tp->stats.rx_length_errors++;
	if (status & (RxUnderrun | RxFIFOOver))
		tp->stats.rx_fifo_errors++;
	if (status & RxOverflow) {
		tp->stats.rx_over_errors++;
		tp->cur_rx = NETDRV_R16 (RxBufAddr) % RX_BUF_LEN;
		NETDRV_W16_F (RxBufPtr, tp->cur_rx - 16);
	}
	if (status & PCIErr) {
		u16 pci_cmd_status;
		pci_read_config_word (tp->pci_dev, PCI_STATUS, &pci_cmd_status);

		printk (KERN_ERR "%s: PCI Bus error %4.4x.\n",
			dev->name, pci_cmd_status);
	}
}


/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t netdrv_interrupt (int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct netdrv_private *tp = dev->priv;
	int boguscnt = max_interrupt_work;
	void *ioaddr = tp->mmio_addr;
	int status = 0, link_changed = 0; /* avoid bogus "uninit" warning */
	int handled = 0;

	spin_lock (&tp->lock);

	do {
		status = NETDRV_R16 (IntrStatus);

		/* h/w no longer present (hotplug?) or major error, bail */
		if (status == 0xFFFF)
			break;

		handled = 1;
		/* Acknowledge all of the current interrupt sources ASAP */
		NETDRV_W16_F (IntrStatus, status);

		DPRINTK ("%s: interrupt  status=%#4.4x new intstat=%#4.4x.\n",
				dev->name, status,
				NETDRV_R16 (IntrStatus));

		if ((status &
		     (PCIErr | PCSTimeout | RxUnderrun | RxOverflow |
		      RxFIFOOver | TxErr | TxOK | RxErr | RxOK)) == 0)
			break;

		/* Check uncommon events with one test. */
		if (status & (PCIErr | PCSTimeout | RxUnderrun | RxOverflow |
		  	      RxFIFOOver | TxErr | RxErr))
			netdrv_weird_interrupt (dev, tp, ioaddr,
						 status, link_changed);

		if (status & (RxOK | RxUnderrun | RxOverflow | RxFIFOOver))	/* Rx interrupt */
			netdrv_rx_interrupt (dev, tp, ioaddr);

		if (status & (TxOK | TxErr))
			netdrv_tx_interrupt (dev, tp, ioaddr);

		boguscnt--;
	} while (boguscnt > 0);

	if (boguscnt <= 0) {
		printk (KERN_WARNING
			"%s: Too much work at interrupt, "
			"IntrStatus=0x%4.4x.\n", dev->name,
			status);

		/* Clear all interrupt sources. */
		NETDRV_W16 (IntrStatus, 0xffff);
	}

	spin_unlock (&tp->lock);

	DPRINTK ("%s: exiting interrupt, intr_status=%#4.4x.\n",
		 dev->name, NETDRV_R16 (IntrStatus));
	return IRQ_RETVAL(handled);
}


static int netdrv_close (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	unsigned long flags;

	DPRINTK ("ENTER\n");

	netif_stop_queue (dev);

	DPRINTK ("%s: Shutting down ethercard, status was 0x%4.4x.\n",
			dev->name, NETDRV_R16 (IntrStatus));

	del_timer_sync (&tp->timer);

	spin_lock_irqsave (&tp->lock, flags);

	/* Stop the chip's Tx and Rx DMA processes. */
	NETDRV_W8 (ChipCmd, (NETDRV_R8 (ChipCmd) & ChipCmdClear));

	/* Disable interrupts by clearing the interrupt mask. */
	NETDRV_W16 (IntrMask, 0x0000);

	/* Update the error counts. */
	tp->stats.rx_missed_errors += NETDRV_R32 (RxMissed);
	NETDRV_W32 (RxMissed, 0);

	spin_unlock_irqrestore (&tp->lock, flags);

	synchronize_irq ();
	free_irq (dev->irq, dev);

	netdrv_tx_clear (tp);

	pci_free_consistent(tp->pci_dev, RX_BUF_TOT_LEN,
			    tp->rx_ring, tp->rx_ring_dma);
	pci_free_consistent(tp->pci_dev, TX_BUF_TOT_LEN,
			    tp->tx_bufs, tp->tx_bufs_dma);
	tp->rx_ring = NULL;
	tp->tx_bufs = NULL;

	/* Green! Put the chip in low-power mode. */
	NETDRV_W8 (Cfg9346, Cfg9346_Unlock);
	NETDRV_W8 (Config1, 0x03);
	NETDRV_W8 (Cfg9346, Cfg9346_Lock);

	DPRINTK ("EXIT\n");
	return 0;
}


static int netdrv_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdrv_private *tp = dev->priv;
	struct mii_ioctl_data *data = if_mii(rq);
	unsigned long flags;
	int rc = 0;

	DPRINTK ("ENTER\n");

	switch (cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		data->phy_id = tp->phys[0] & 0x3f;
		/* Fall Through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
		spin_lock_irqsave (&tp->lock, flags);
		data->val_out = mdio_read (dev, data->phy_id & 0x1f, data->reg_num & 0x1f);
		spin_unlock_irqrestore (&tp->lock, flags);
		break;

	case SIOCSMIIREG:		/* Write MII PHY register. */
		if (!capable (CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}

		spin_lock_irqsave (&tp->lock, flags);
		mdio_write (dev, data->phy_id & 0x1f, data->reg_num & 0x1f, data->val_in);
		spin_unlock_irqrestore (&tp->lock, flags);
		break;

	default:
		rc = -EOPNOTSUPP;
		break;
	}

	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static struct net_device_stats *netdrv_get_stats (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;

	DPRINTK ("ENTER\n");

	assert (tp != NULL);

	if (netif_running(dev)) {
		unsigned long flags;

		spin_lock_irqsave (&tp->lock, flags);

		tp->stats.rx_missed_errors += NETDRV_R32 (RxMissed);
		NETDRV_W32 (RxMissed, 0);

		spin_unlock_irqrestore (&tp->lock, flags);
	}

	DPRINTK ("EXIT\n");
	return &tp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */

static void netdrv_set_rx_mode (struct net_device *dev)
{
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	u32 mc_filter[2];	/* Multicast hash filter */
	int i, rx_mode;
	u32 tmp;

	DPRINTK ("ENTER\n");

	DPRINTK ("%s:   netdrv_set_rx_mode(%4.4x) done -- Rx config %8.8x.\n",
			dev->name, dev->flags, NETDRV_R32 (RxConfig));

	/* Note: do not reorder, GCC is clever about common statements. */
	if (dev->flags & IFF_PROMISC) {
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct dev_mc_list *mclist;
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {
			int bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;

			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
		}
	}

	/* if called from irq handler, lock already acquired */
	if (!in_irq ())
		spin_lock_irq (&tp->lock);

	/* We can safely update without stopping the chip. */
	tmp = netdrv_rx_config | rx_mode |
		(NETDRV_R32 (RxConfig) & rtl_chip_info[tp->chipset].RxConfigMask);
	NETDRV_W32_F (RxConfig, tmp);
	NETDRV_W32_F (MAR0 + 0, mc_filter[0]);
	NETDRV_W32_F (MAR0 + 4, mc_filter[1]);

	if (!in_irq ())
		spin_unlock_irq (&tp->lock);

	DPRINTK ("EXIT\n");
}


#ifdef CONFIG_PM

static int netdrv_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdrv_private *tp = dev->priv;
	void *ioaddr = tp->mmio_addr;
	unsigned long flags;

	if (!netif_running(dev))
		return 0;
	netif_device_detach (dev);

	spin_lock_irqsave (&tp->lock, flags);

	/* Disable interrupts, stop Tx and Rx. */
	NETDRV_W16 (IntrMask, 0x0000);
	NETDRV_W8 (ChipCmd, (NETDRV_R8 (ChipCmd) & ChipCmdClear));

	/* Update the error counts. */
	tp->stats.rx_missed_errors += NETDRV_R32 (RxMissed);
	NETDRV_W32 (RxMissed, 0);

	spin_unlock_irqrestore (&tp->lock, flags);

	pci_save_state (pdev);
	pci_set_power_state (pdev, PCI_D3hot);

	return 0;
}


static int netdrv_resume (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct netdrv_private *tp = dev->priv;

	if (!netif_running(dev))
		return 0;
	pci_set_power_state (pdev, PCI_D0);
	pci_restore_state (pdev);
	netif_device_attach (dev);
	netdrv_hw_start (dev);

	return 0;
}

#endif /* CONFIG_PM */


static struct pci_driver netdrv_pci_driver = {
	.name		= MODNAME,
	.id_table	= netdrv_pci_tbl,
	.probe		= netdrv_init_one,
	.remove		= __devexit_p(netdrv_remove_one),
#ifdef CONFIG_PM
	.suspend	= netdrv_suspend,
	.resume		= netdrv_resume,
#endif /* CONFIG_PM */
};


static int __init netdrv_init_module (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif
	return pci_register_driver(&netdrv_pci_driver);
}


static void __exit netdrv_cleanup_module (void)
{
	pci_unregister_driver (&netdrv_pci_driver);
}


module_init(netdrv_init_module);
module_exit(netdrv_cleanup_module);

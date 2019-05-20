/* via-rhine.c: A Linux Ethernet device driver for VIA Rhine family chips. */
/*
	Written 1998-2001 by Donald Becker.

	Current Maintainer: Roger Luethi <rl@hellgate.ch>

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	This driver is designed for the VIA VT86C100A Rhine-I.
	It also works with the Rhine-II (6102) and Rhine-III (6105/6105L/6105LOM
	and management NIC 6105M).

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403


	This driver contains some changes from the original Donald Becker
	version. He may or may not be interested in bug reports on this
	code. You can find his versions at:
	http://www.scyld.com/network/via-rhine.html
	[link no longer provides useful info -jgarzik]

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"via-rhine"
#define DRV_VERSION	"1.5.1"
#define DRV_RELDATE	"2010-10-09"

#include <linux/types.h>

/* A few user-configurable values.
   These may be modified when a driver module is loaded. */
static int debug = 0;
#define RHINE_MSG_DEFAULT \
        (0x0000)

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
#if defined(__alpha__) || defined(__arm__) || defined(__hppa__) || \
	defined(CONFIG_SPARC) || defined(__ia64__) ||		   \
	defined(__sh__) || defined(__mips__)
static int rx_copybreak = 1518;
#else
static int rx_copybreak;
#endif

/* Work-around for broken BIOSes: they are unable to get the chip back out of
   power state D3 so PXE booting fails. bootparam(7): via-rhine.avoid_D3=1 */
static bool avoid_D3;

/*
 * In case you are looking for 'options[]' or 'full_duplex[]', they
 * are gone. Use ethtool(8) instead.
 */

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   The Rhine has a 64 element 8390-like hash table. */
static const int multicast_filter_limit = 32;


/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
 * The compiler will convert <unsigned>'%'<2^N> into a bit mask.
 * Making the Tx ring too large decreases the effectiveness of channel
 * bonding and packet priority.
 * With BQL support, we can increase TX ring safely.
 * There are no ill effects from too-large receive rings.
 */
#define TX_RING_SIZE	64
#define TX_QUEUE_LEN	(TX_RING_SIZE - 6)	/* Limit ring entries actually used. */
#define RX_RING_SIZE	64

/* Operational parameters that usually are not changed. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT	(2*HZ)

#define PKT_BUF_SZ	1536	/* Size of each temporary Rx buffer.*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/bitops.h>
#include <linux/workqueue.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>
#include <linux/dmi.h>

/* These identify the driver base version and may not be removed. */
static const char version[] =
	"v1.10-LK" DRV_VERSION " " DRV_RELDATE " Written by Donald Becker";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("VIA Rhine PCI Fast Ethernet driver");
MODULE_LICENSE("GPL");

module_param(debug, int, 0);
module_param(rx_copybreak, int, 0);
module_param(avoid_D3, bool, 0);
MODULE_PARM_DESC(debug, "VIA Rhine debug message flags");
MODULE_PARM_DESC(rx_copybreak, "VIA Rhine copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(avoid_D3, "Avoid power state D3 (work-around for broken BIOSes)");

#define MCAM_SIZE	32
#define VCAM_SIZE	32

/*
		Theory of Operation

I. Board Compatibility

This driver is designed for the VIA 86c100A Rhine-II PCI Fast Ethernet
controller.

II. Board-specific settings

Boards with this chip are functional only in a bus-master PCI slot.

Many operational settings are loaded from the EEPROM to the Config word at
offset 0x78. For most of these settings, this driver assumes that they are
correct.
If this driver is compiled to use PCI memory space operations the EEPROM
must be configured to enable memory ops.

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list. The ring sizes are set at compile time by RX/TX_RING_SIZE.

IIIb/c. Transmit/Receive Structure

This driver attempts to use a zero-copy receive and transmit scheme.

Alas, all data buffers are required to start on a 32 bit boundary, so
the driver must often copy transmit packets into bounce buffers.

The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the chip as receive data
buffers. When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack. Buffers consumed this way are replaced by newly allocated
skbuffs in the last phase of rhine_rx().

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames. New boards are typically used in generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets. When copying is done, the cost is usually mitigated by using
a combined copy/checksum routine. Copying also preloads the cache, which is
most useful with small frames.

Since the VIA chips are only able to transfer data to buffers on 32 bit
boundaries, the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing. Copying these unaligned buffers
has the beneficial effect of 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control. One
is the send-packet routine, which enforces single-threaded use by the
netdev_priv(dev)->lock spinlock. The other thread is the interrupt handler,
which is single threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring. It locks the
netdev_priv(dev)->lock whenever it's queuing a Tx packet. If the next slot in
the ring is not available it stops the transmit queue by
calling netif_stop_queue.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring. After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. If at least half of the entries in
the Rx ring are available the transmit queue is woken up if it was stopped.

IV. Notes

IVb. References

Preliminary VT86C100A manual from http://www.via.com.tw/
http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html
ftp://ftp.via.com.tw/public/lan/Products/NIC/VT86C100A/Datasheet/VT86C100A03.pdf
ftp://ftp.via.com.tw/public/lan/Products/NIC/VT6102/Datasheet/VT6102_021.PDF


IVc. Errata

The VT86C100A manual is not reliable information.
The 3043 chip does not handle unaligned transmit or receive buffers, resulting
in significant performance degradation for bounce buffer copies on transmit
and unaligned IP headers on receive.
The chip does not pad to minimum transmit length.

*/


/* This table drives the PCI probe routines. It's mostly boilerplate in all
   of the drivers, and will likely be provided by some future kernel.
   Note the matching code -- the first table entry matchs all 56** cards but
   second only the 1234 card.
*/

enum rhine_revs {
	VT86C100A	= 0x00,
	VTunknown0	= 0x20,
	VT6102		= 0x40,
	VT8231		= 0x50,	/* Integrated MAC */
	VT8233		= 0x60,	/* Integrated MAC */
	VT8235		= 0x74,	/* Integrated MAC */
	VT8237		= 0x78,	/* Integrated MAC */
	VTunknown1	= 0x7C,
	VT6105		= 0x80,
	VT6105_B0	= 0x83,
	VT6105L		= 0x8A,
	VT6107		= 0x8C,
	VTunknown2	= 0x8E,
	VT6105M		= 0x90,	/* Management adapter */
};

enum rhine_quirks {
	rqWOL		= 0x0001,	/* Wake-On-LAN support */
	rqForceReset	= 0x0002,
	rq6patterns	= 0x0040,	/* 6 instead of 4 patterns for WOL */
	rqStatusWBRace	= 0x0080,	/* Tx Status Writeback Error possible */
	rqRhineI	= 0x0100,	/* See comment below */
	rqIntPHY	= 0x0200,	/* Integrated PHY */
	rqMgmt		= 0x0400,	/* Management adapter */
	rqNeedEnMMIO	= 0x0800,	/* Whether the core needs to be
					 * switched from PIO mode to MMIO
					 * (only applies to PCI)
					 */
};
/*
 * rqRhineI: VT86C100A (aka Rhine-I) uses different bits to enable
 * MMIO as well as for the collision counter and the Tx FIFO underflow
 * indicator. In addition, Tx and Rx buffers need to 4 byte aligned.
 */

/* Beware of PCI posted writes */
#define IOSYNC	do { ioread8(ioaddr + StationAddr); } while (0)

static const struct pci_device_id rhine_pci_tbl[] = {
	{ 0x1106, 0x3043, PCI_ANY_ID, PCI_ANY_ID, },	/* VT86C100A */
	{ 0x1106, 0x3065, PCI_ANY_ID, PCI_ANY_ID, },	/* VT6102 */
	{ 0x1106, 0x3106, PCI_ANY_ID, PCI_ANY_ID, },	/* 6105{,L,LOM} */
	{ 0x1106, 0x3053, PCI_ANY_ID, PCI_ANY_ID, },	/* VT6105M */
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, rhine_pci_tbl);

/* OpenFirmware identifiers for platform-bus devices
 * The .data field is currently only used to store quirks
 */
static u32 vt8500_quirks = rqWOL | rqForceReset | rq6patterns;
static const struct of_device_id rhine_of_tbl[] = {
	{ .compatible = "via,vt8500-rhine", .data = &vt8500_quirks },
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(of, rhine_of_tbl);

/* Offsets to the device registers. */
enum register_offsets {
	StationAddr=0x00, RxConfig=0x06, TxConfig=0x07, ChipCmd=0x08,
	ChipCmd1=0x09, TQWake=0x0A,
	IntrStatus=0x0C, IntrEnable=0x0E,
	MulticastFilter0=0x10, MulticastFilter1=0x14,
	RxRingPtr=0x18, TxRingPtr=0x1C, GFIFOTest=0x54,
	MIIPhyAddr=0x6C, MIIStatus=0x6D, PCIBusConfig=0x6E, PCIBusConfig1=0x6F,
	MIICmd=0x70, MIIRegAddr=0x71, MIIData=0x72, MACRegEEcsr=0x74,
	ConfigA=0x78, ConfigB=0x79, ConfigC=0x7A, ConfigD=0x7B,
	RxMissed=0x7C, RxCRCErrs=0x7E, MiscCmd=0x81,
	StickyHW=0x83, IntrStatus2=0x84,
	CamMask=0x88, CamCon=0x92, CamAddr=0x93,
	WOLcrSet=0xA0, PwcfgSet=0xA1, WOLcgSet=0xA3, WOLcrClr=0xA4,
	WOLcrClr1=0xA6, WOLcgClr=0xA7,
	PwrcsrSet=0xA8, PwrcsrSet1=0xA9, PwrcsrClr=0xAC, PwrcsrClr1=0xAD,
};

/* Bits in ConfigD */
enum backoff_bits {
	BackOptional=0x01, BackModify=0x02,
	BackCaptureEffect=0x04, BackRandom=0x08
};

/* Bits in the TxConfig (TCR) register */
enum tcr_bits {
	TCR_PQEN=0x01,
	TCR_LB0=0x02,		/* loopback[0] */
	TCR_LB1=0x04,		/* loopback[1] */
	TCR_OFSET=0x08,
	TCR_RTGOPT=0x10,
	TCR_RTFT0=0x20,
	TCR_RTFT1=0x40,
	TCR_RTSF=0x80,
};

/* Bits in the CamCon (CAMC) register */
enum camcon_bits {
	CAMC_CAMEN=0x01,
	CAMC_VCAMSL=0x02,
	CAMC_CAMWR=0x04,
	CAMC_CAMRD=0x08,
};

/* Bits in the PCIBusConfig1 (BCR1) register */
enum bcr1_bits {
	BCR1_POT0=0x01,
	BCR1_POT1=0x02,
	BCR1_POT2=0x04,
	BCR1_CTFT0=0x08,
	BCR1_CTFT1=0x10,
	BCR1_CTSF=0x20,
	BCR1_TXQNOBK=0x40,	/* for VT6105 */
	BCR1_VIDFR=0x80,	/* for VT6105 */
	BCR1_MED0=0x40,		/* for VT6102 */
	BCR1_MED1=0x80,		/* for VT6102 */
};

/* Registers we check that mmio and reg are the same. */
static const int mmio_verify_registers[] = {
	RxConfig, TxConfig, IntrEnable, ConfigA, ConfigB, ConfigC, ConfigD,
	0
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone	= 0x0001,
	IntrTxDone	= 0x0002,
	IntrRxErr	= 0x0004,
	IntrTxError	= 0x0008,
	IntrRxEmpty	= 0x0020,
	IntrPCIErr	= 0x0040,
	IntrStatsMax	= 0x0080,
	IntrRxEarly	= 0x0100,
	IntrTxUnderrun	= 0x0210,
	IntrRxOverflow	= 0x0400,
	IntrRxDropped	= 0x0800,
	IntrRxNoBuf	= 0x1000,
	IntrTxAborted	= 0x2000,
	IntrLinkChange	= 0x4000,
	IntrRxWakeUp	= 0x8000,
	IntrTxDescRace		= 0x080000,	/* mapped from IntrStatus2 */
	IntrNormalSummary	= IntrRxDone | IntrTxDone,
	IntrTxErrSummary	= IntrTxDescRace | IntrTxAborted | IntrTxError |
				  IntrTxUnderrun,
};

/* Bits in WOLcrSet/WOLcrClr and PwrcsrSet/PwrcsrClr */
enum wol_bits {
	WOLucast	= 0x10,
	WOLmagic	= 0x20,
	WOLbmcast	= 0x30,
	WOLlnkon	= 0x40,
	WOLlnkoff	= 0x80,
};

/* The Rx and Tx buffer descriptors. */
struct rx_desc {
	__le32 rx_status;
	__le32 desc_length; /* Chain flag, Buffer/frame length */
	__le32 addr;
	__le32 next_desc;
};
struct tx_desc {
	__le32 tx_status;
	__le32 desc_length; /* Chain flag, Tx Config, Frame length */
	__le32 addr;
	__le32 next_desc;
};

/* Initial value for tx_desc.desc_length, Buffer size goes to bits 0-10 */
#define TXDESC		0x00e08000

enum rx_status_bits {
	RxOK=0x8000, RxWholePkt=0x0300, RxErr=0x008F
};

/* Bits in *_desc.*_status */
enum desc_status_bits {
	DescOwn=0x80000000
};

/* Bits in *_desc.*_length */
enum desc_length_bits {
	DescTag=0x00010000
};

/* Bits in ChipCmd. */
enum chip_cmd_bits {
	CmdInit=0x01, CmdStart=0x02, CmdStop=0x04, CmdRxOn=0x08,
	CmdTxOn=0x10, Cmd1TxDemand=0x20, CmdRxDemand=0x40,
	Cmd1EarlyRx=0x01, Cmd1EarlyTx=0x02, Cmd1FDuplex=0x04,
	Cmd1NoTxPoll=0x08, Cmd1Reset=0x80,
};

struct rhine_stats {
	u64		packets;
	u64		bytes;
	struct u64_stats_sync syncp;
};

struct rhine_private {
	/* Bit mask for configured VLAN ids */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	/* Descriptor rings */
	struct rx_desc *rx_ring;
	struct tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;

	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff *rx_skbuff[RX_RING_SIZE];
	dma_addr_t rx_skbuff_dma[RX_RING_SIZE];

	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff *tx_skbuff[TX_RING_SIZE];
	dma_addr_t tx_skbuff_dma[TX_RING_SIZE];

	/* Tx bounce buffers (Rhine-I only) */
	unsigned char *tx_buf[TX_RING_SIZE];
	unsigned char *tx_bufs;
	dma_addr_t tx_bufs_dma;

	int irq;
	long pioaddr;
	struct net_device *dev;
	struct napi_struct napi;
	spinlock_t lock;
	struct mutex task_lock;
	bool task_enable;
	struct work_struct slow_event_task;
	struct work_struct reset_task;

	u32 msg_enable;

	/* Frequently used values: keep some adjacent for cache effect. */
	u32 quirks;
	unsigned int cur_rx;
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;		/* Based on MTU+slack. */
	struct rhine_stats rx_stats;
	struct rhine_stats tx_stats;
	u8 wolopts;

	u8 tx_thresh, rx_thresh;

	struct mii_if_info mii_if;
	void __iomem *base;
};

#define BYTE_REG_BITS_ON(x, p)      do { iowrite8((ioread8((p))|(x)), (p)); } while (0)
#define WORD_REG_BITS_ON(x, p)      do { iowrite16((ioread16((p))|(x)), (p)); } while (0)
#define DWORD_REG_BITS_ON(x, p)     do { iowrite32((ioread32((p))|(x)), (p)); } while (0)

#define BYTE_REG_BITS_IS_ON(x, p)   (ioread8((p)) & (x))
#define WORD_REG_BITS_IS_ON(x, p)   (ioread16((p)) & (x))
#define DWORD_REG_BITS_IS_ON(x, p)  (ioread32((p)) & (x))

#define BYTE_REG_BITS_OFF(x, p)     do { iowrite8(ioread8((p)) & (~(x)), (p)); } while (0)
#define WORD_REG_BITS_OFF(x, p)     do { iowrite16(ioread16((p)) & (~(x)), (p)); } while (0)
#define DWORD_REG_BITS_OFF(x, p)    do { iowrite32(ioread32((p)) & (~(x)), (p)); } while (0)

#define BYTE_REG_BITS_SET(x, m, p)   do { iowrite8((ioread8((p)) & (~(m)))|(x), (p)); } while (0)
#define WORD_REG_BITS_SET(x, m, p)   do { iowrite16((ioread16((p)) & (~(m)))|(x), (p)); } while (0)
#define DWORD_REG_BITS_SET(x, m, p)  do { iowrite32((ioread32((p)) & (~(m)))|(x), (p)); } while (0)


static int  mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int  rhine_open(struct net_device *dev);
static void rhine_reset_task(struct work_struct *work);
static void rhine_slow_event_task(struct work_struct *work);
static void rhine_tx_timeout(struct net_device *dev);
static netdev_tx_t rhine_start_tx(struct sk_buff *skb,
				  struct net_device *dev);
static irqreturn_t rhine_interrupt(int irq, void *dev_instance);
static void rhine_tx(struct net_device *dev);
static int rhine_rx(struct net_device *dev, int limit);
static void rhine_set_rx_mode(struct net_device *dev);
static void rhine_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *stats);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static const struct ethtool_ops netdev_ethtool_ops;
static int  rhine_close(struct net_device *dev);
static int rhine_vlan_rx_add_vid(struct net_device *dev,
				 __be16 proto, u16 vid);
static int rhine_vlan_rx_kill_vid(struct net_device *dev,
				  __be16 proto, u16 vid);
static void rhine_restart_tx(struct net_device *dev);

static void rhine_wait_bit(struct rhine_private *rp, u8 reg, u8 mask, bool low)
{
	void __iomem *ioaddr = rp->base;
	int i;

	for (i = 0; i < 1024; i++) {
		bool has_mask_bits = !!(ioread8(ioaddr + reg) & mask);

		if (low ^ has_mask_bits)
			break;
		udelay(10);
	}
	if (i > 64) {
		netif_dbg(rp, hw, rp->dev, "%s bit wait (%02x/%02x) cycle "
			  "count: %04d\n", low ? "low" : "high", reg, mask, i);
	}
}

static void rhine_wait_bit_high(struct rhine_private *rp, u8 reg, u8 mask)
{
	rhine_wait_bit(rp, reg, mask, false);
}

static void rhine_wait_bit_low(struct rhine_private *rp, u8 reg, u8 mask)
{
	rhine_wait_bit(rp, reg, mask, true);
}

static u32 rhine_get_events(struct rhine_private *rp)
{
	void __iomem *ioaddr = rp->base;
	u32 intr_status;

	intr_status = ioread16(ioaddr + IntrStatus);
	/* On Rhine-II, Bit 3 indicates Tx descriptor write-back race. */
	if (rp->quirks & rqStatusWBRace)
		intr_status |= ioread8(ioaddr + IntrStatus2) << 16;
	return intr_status;
}

static void rhine_ack_events(struct rhine_private *rp, u32 mask)
{
	void __iomem *ioaddr = rp->base;

	if (rp->quirks & rqStatusWBRace)
		iowrite8(mask >> 16, ioaddr + IntrStatus2);
	iowrite16(mask, ioaddr + IntrStatus);
}

/*
 * Get power related registers into sane state.
 * Notify user about past WOL event.
 */
static void rhine_power_init(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	u16 wolstat;

	if (rp->quirks & rqWOL) {
		/* Make sure chip is in power state D0 */
		iowrite8(ioread8(ioaddr + StickyHW) & 0xFC, ioaddr + StickyHW);

		/* Disable "force PME-enable" */
		iowrite8(0x80, ioaddr + WOLcgClr);

		/* Clear power-event config bits (WOL) */
		iowrite8(0xFF, ioaddr + WOLcrClr);
		/* More recent cards can manage two additional patterns */
		if (rp->quirks & rq6patterns)
			iowrite8(0x03, ioaddr + WOLcrClr1);

		/* Save power-event status bits */
		wolstat = ioread8(ioaddr + PwrcsrSet);
		if (rp->quirks & rq6patterns)
			wolstat |= (ioread8(ioaddr + PwrcsrSet1) & 0x03) << 8;

		/* Clear power-event status bits */
		iowrite8(0xFF, ioaddr + PwrcsrClr);
		if (rp->quirks & rq6patterns)
			iowrite8(0x03, ioaddr + PwrcsrClr1);

		if (wolstat) {
			char *reason;
			switch (wolstat) {
			case WOLmagic:
				reason = "Magic packet";
				break;
			case WOLlnkon:
				reason = "Link went up";
				break;
			case WOLlnkoff:
				reason = "Link went down";
				break;
			case WOLucast:
				reason = "Unicast packet";
				break;
			case WOLbmcast:
				reason = "Multicast/broadcast packet";
				break;
			default:
				reason = "Unknown";
			}
			netdev_info(dev, "Woke system up. Reason: %s\n",
				    reason);
		}
	}
}

static void rhine_chip_reset(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	u8 cmd1;

	iowrite8(Cmd1Reset, ioaddr + ChipCmd1);
	IOSYNC;

	if (ioread8(ioaddr + ChipCmd1) & Cmd1Reset) {
		netdev_info(dev, "Reset not complete yet. Trying harder.\n");

		/* Force reset */
		if (rp->quirks & rqForceReset)
			iowrite8(0x40, ioaddr + MiscCmd);

		/* Reset can take somewhat longer (rare) */
		rhine_wait_bit_low(rp, ChipCmd1, Cmd1Reset);
	}

	cmd1 = ioread8(ioaddr + ChipCmd1);
	netif_info(rp, hw, dev, "Reset %s\n", (cmd1 & Cmd1Reset) ?
		   "failed" : "succeeded");
}

static void enable_mmio(long pioaddr, u32 quirks)
{
	int n;

	if (quirks & rqNeedEnMMIO) {
		if (quirks & rqRhineI) {
			/* More recent docs say that this bit is reserved */
			n = inb(pioaddr + ConfigA) | 0x20;
			outb(n, pioaddr + ConfigA);
		} else {
			n = inb(pioaddr + ConfigD) | 0x80;
			outb(n, pioaddr + ConfigD);
		}
	}
}

static inline int verify_mmio(struct device *hwdev,
			      long pioaddr,
			      void __iomem *ioaddr,
			      u32 quirks)
{
	if (quirks & rqNeedEnMMIO) {
		int i = 0;

		/* Check that selected MMIO registers match the PIO ones */
		while (mmio_verify_registers[i]) {
			int reg = mmio_verify_registers[i++];
			unsigned char a = inb(pioaddr+reg);
			unsigned char b = readb(ioaddr+reg);

			if (a != b) {
				dev_err(hwdev,
					"MMIO do not match PIO [%02x] (%02x != %02x)\n",
					reg, a, b);
				return -EIO;
			}
		}
	}
	return 0;
}

/*
 * Loads bytes 0x00-0x05, 0x6E-0x6F, 0x78-0x7B from EEPROM
 * (plus 0x6C for Rhine-I/II)
 */
static void rhine_reload_eeprom(long pioaddr, struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	int i;

	outb(0x20, pioaddr + MACRegEEcsr);
	for (i = 0; i < 1024; i++) {
		if (!(inb(pioaddr + MACRegEEcsr) & 0x20))
			break;
	}
	if (i > 512)
		pr_info("%4d cycles used @ %s:%d\n", i, __func__, __LINE__);

	/*
	 * Reloading from EEPROM overwrites ConfigA-D, so we must re-enable
	 * MMIO. If reloading EEPROM was done first this could be avoided, but
	 * it is not known if that still works with the "win98-reboot" problem.
	 */
	enable_mmio(pioaddr, rp->quirks);

	/* Turn off EEPROM-controlled wake-up (magic packet) */
	if (rp->quirks & rqWOL)
		iowrite8(ioread8(ioaddr + ConfigA) & 0xFC, ioaddr + ConfigA);

}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void rhine_poll(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	const int irq = rp->irq;

	disable_irq(irq);
	rhine_interrupt(irq, dev);
	enable_irq(irq);
}
#endif

static void rhine_kick_tx_threshold(struct rhine_private *rp)
{
	if (rp->tx_thresh < 0xe0) {
		void __iomem *ioaddr = rp->base;

		rp->tx_thresh += 0x20;
		BYTE_REG_BITS_SET(rp->tx_thresh, 0x80, ioaddr + TxConfig);
	}
}

static void rhine_tx_err(struct rhine_private *rp, u32 status)
{
	struct net_device *dev = rp->dev;

	if (status & IntrTxAborted) {
		netif_info(rp, tx_err, dev,
			   "Abort %08x, frame dropped\n", status);
	}

	if (status & IntrTxUnderrun) {
		rhine_kick_tx_threshold(rp);
		netif_info(rp, tx_err ,dev, "Transmitter underrun, "
			   "Tx threshold now %02x\n", rp->tx_thresh);
	}

	if (status & IntrTxDescRace)
		netif_info(rp, tx_err, dev, "Tx descriptor write-back race\n");

	if ((status & IntrTxError) &&
	    (status & (IntrTxAborted | IntrTxUnderrun | IntrTxDescRace)) == 0) {
		rhine_kick_tx_threshold(rp);
		netif_info(rp, tx_err, dev, "Unspecified error. "
			   "Tx threshold now %02x\n", rp->tx_thresh);
	}

	rhine_restart_tx(dev);
}

static void rhine_update_rx_crc_and_missed_errord(struct rhine_private *rp)
{
	void __iomem *ioaddr = rp->base;
	struct net_device_stats *stats = &rp->dev->stats;

	stats->rx_crc_errors    += ioread16(ioaddr + RxCRCErrs);
	stats->rx_missed_errors += ioread16(ioaddr + RxMissed);

	/*
	 * Clears the "tally counters" for CRC errors and missed frames(?).
	 * It has been reported that some chips need a write of 0 to clear
	 * these, for others the counters are set to 1 when written to and
	 * instead cleared when read. So we clear them both ways ...
	 */
	iowrite32(0, ioaddr + RxMissed);
	ioread16(ioaddr + RxCRCErrs);
	ioread16(ioaddr + RxMissed);
}

#define RHINE_EVENT_NAPI_RX	(IntrRxDone | \
				 IntrRxErr | \
				 IntrRxEmpty | \
				 IntrRxOverflow	| \
				 IntrRxDropped | \
				 IntrRxNoBuf | \
				 IntrRxWakeUp)

#define RHINE_EVENT_NAPI_TX_ERR	(IntrTxError | \
				 IntrTxAborted | \
				 IntrTxUnderrun | \
				 IntrTxDescRace)
#define RHINE_EVENT_NAPI_TX	(IntrTxDone | RHINE_EVENT_NAPI_TX_ERR)

#define RHINE_EVENT_NAPI	(RHINE_EVENT_NAPI_RX | \
				 RHINE_EVENT_NAPI_TX | \
				 IntrStatsMax)
#define RHINE_EVENT_SLOW	(IntrPCIErr | IntrLinkChange)
#define RHINE_EVENT		(RHINE_EVENT_NAPI | RHINE_EVENT_SLOW)

static int rhine_napipoll(struct napi_struct *napi, int budget)
{
	struct rhine_private *rp = container_of(napi, struct rhine_private, napi);
	struct net_device *dev = rp->dev;
	void __iomem *ioaddr = rp->base;
	u16 enable_mask = RHINE_EVENT & 0xffff;
	int work_done = 0;
	u32 status;

	status = rhine_get_events(rp);
	rhine_ack_events(rp, status & ~RHINE_EVENT_SLOW);

	if (status & RHINE_EVENT_NAPI_RX)
		work_done += rhine_rx(dev, budget);

	if (status & RHINE_EVENT_NAPI_TX) {
		if (status & RHINE_EVENT_NAPI_TX_ERR) {
			/* Avoid scavenging before Tx engine turned off */
			rhine_wait_bit_low(rp, ChipCmd, CmdTxOn);
			if (ioread8(ioaddr + ChipCmd) & CmdTxOn)
				netif_warn(rp, tx_err, dev, "Tx still on\n");
		}

		rhine_tx(dev);

		if (status & RHINE_EVENT_NAPI_TX_ERR)
			rhine_tx_err(rp, status);
	}

	if (status & IntrStatsMax) {
		spin_lock(&rp->lock);
		rhine_update_rx_crc_and_missed_errord(rp);
		spin_unlock(&rp->lock);
	}

	if (status & RHINE_EVENT_SLOW) {
		enable_mask &= ~RHINE_EVENT_SLOW;
		schedule_work(&rp->slow_event_task);
	}

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		iowrite16(enable_mask, ioaddr + IntrEnable);
	}
	return work_done;
}

static void rhine_hw_init(struct net_device *dev, long pioaddr)
{
	struct rhine_private *rp = netdev_priv(dev);

	/* Reset the chip to erase previous misconfiguration. */
	rhine_chip_reset(dev);

	/* Rhine-I needs extra time to recuperate before EEPROM reload */
	if (rp->quirks & rqRhineI)
		msleep(5);

	/* Reload EEPROM controlled bytes cleared by soft reset */
	if (dev_is_pci(dev->dev.parent))
		rhine_reload_eeprom(pioaddr, dev);
}

static const struct net_device_ops rhine_netdev_ops = {
	.ndo_open		 = rhine_open,
	.ndo_stop		 = rhine_close,
	.ndo_start_xmit		 = rhine_start_tx,
	.ndo_get_stats64	 = rhine_get_stats64,
	.ndo_set_rx_mode	 = rhine_set_rx_mode,
	.ndo_validate_addr	 = eth_validate_addr,
	.ndo_set_mac_address 	 = eth_mac_addr,
	.ndo_do_ioctl		 = netdev_ioctl,
	.ndo_tx_timeout 	 = rhine_tx_timeout,
	.ndo_vlan_rx_add_vid	 = rhine_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	 = rhine_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	 = rhine_poll,
#endif
};

static int rhine_init_one_common(struct device *hwdev, u32 quirks,
				 long pioaddr, void __iomem *ioaddr, int irq)
{
	struct net_device *dev;
	struct rhine_private *rp;
	int i, rc, phy_id;
	const char *name;

	/* this should always be supported */
	rc = dma_set_mask(hwdev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(hwdev, "32-bit DMA addresses not supported by the card!?\n");
		goto err_out;
	}

	dev = alloc_etherdev(sizeof(struct rhine_private));
	if (!dev) {
		rc = -ENOMEM;
		goto err_out;
	}
	SET_NETDEV_DEV(dev, hwdev);

	rp = netdev_priv(dev);
	rp->dev = dev;
	rp->quirks = quirks;
	rp->pioaddr = pioaddr;
	rp->base = ioaddr;
	rp->irq = irq;
	rp->msg_enable = netif_msg_init(debug, RHINE_MSG_DEFAULT);

	phy_id = rp->quirks & rqIntPHY ? 1 : 0;

	u64_stats_init(&rp->tx_stats.syncp);
	u64_stats_init(&rp->rx_stats.syncp);

	/* Get chip registers into a sane state */
	rhine_power_init(dev);
	rhine_hw_init(dev, pioaddr);

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = ioread8(ioaddr + StationAddr + i);

	if (!is_valid_ether_addr(dev->dev_addr)) {
		/* Report it and use a random ethernet address instead */
		netdev_err(dev, "Invalid MAC address: %pM\n", dev->dev_addr);
		eth_hw_addr_random(dev);
		netdev_info(dev, "Using random MAC address: %pM\n",
			    dev->dev_addr);
	}

	/* For Rhine-I/II, phy_id is loaded from EEPROM */
	if (!phy_id)
		phy_id = ioread8(ioaddr + 0x6C);

	spin_lock_init(&rp->lock);
	mutex_init(&rp->task_lock);
	INIT_WORK(&rp->reset_task, rhine_reset_task);
	INIT_WORK(&rp->slow_event_task, rhine_slow_event_task);

	rp->mii_if.dev = dev;
	rp->mii_if.mdio_read = mdio_read;
	rp->mii_if.mdio_write = mdio_write;
	rp->mii_if.phy_id_mask = 0x1f;
	rp->mii_if.reg_num_mask = 0x1f;

	/* The chip-specific entries in the device structure. */
	dev->netdev_ops = &rhine_netdev_ops;
	dev->ethtool_ops = &netdev_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	netif_napi_add(dev, &rp->napi, rhine_napipoll, 64);

	if (rp->quirks & rqRhineI)
		dev->features |= NETIF_F_SG|NETIF_F_HW_CSUM;

	if (rp->quirks & rqMgmt)
		dev->features |= NETIF_F_HW_VLAN_CTAG_TX |
				 NETIF_F_HW_VLAN_CTAG_RX |
				 NETIF_F_HW_VLAN_CTAG_FILTER;

	/* dev->name not defined before register_netdev()! */
	rc = register_netdev(dev);
	if (rc)
		goto err_out_free_netdev;

	if (rp->quirks & rqRhineI)
		name = "Rhine";
	else if (rp->quirks & rqStatusWBRace)
		name = "Rhine II";
	else if (rp->quirks & rqMgmt)
		name = "Rhine III (Management Adapter)";
	else
		name = "Rhine III";

	netdev_info(dev, "VIA %s at %p, %pM, IRQ %d\n",
		    name, ioaddr, dev->dev_addr, rp->irq);

	dev_set_drvdata(hwdev, dev);

	{
		u16 mii_cmd;
		int mii_status = mdio_read(dev, phy_id, 1);
		mii_cmd = mdio_read(dev, phy_id, MII_BMCR) & ~BMCR_ISOLATE;
		mdio_write(dev, phy_id, MII_BMCR, mii_cmd);
		if (mii_status != 0xffff && mii_status != 0x0000) {
			rp->mii_if.advertising = mdio_read(dev, phy_id, 4);
			netdev_info(dev,
				    "MII PHY found at address %d, status 0x%04x advertising %04x Link %04x\n",
				    phy_id,
				    mii_status, rp->mii_if.advertising,
				    mdio_read(dev, phy_id, 5));

			/* set IFF_RUNNING */
			if (mii_status & BMSR_LSTATUS)
				netif_carrier_on(dev);
			else
				netif_carrier_off(dev);

		}
	}
	rp->mii_if.phy_id = phy_id;
	if (avoid_D3)
		netif_info(rp, probe, dev, "No D3 power state at shutdown\n");

	return 0;

err_out_free_netdev:
	free_netdev(dev);
err_out:
	return rc;
}

static int rhine_init_one_pci(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	struct device *hwdev = &pdev->dev;
	int rc;
	long pioaddr, memaddr;
	void __iomem *ioaddr;
	int io_size = pdev->revision < VTunknown0 ? 128 : 256;

/* This driver was written to use PCI memory space. Some early versions
 * of the Rhine may only work correctly with I/O space accesses.
 * TODO: determine for which revisions this is true and assign the flag
 *	 in code as opposed to this Kconfig option (???)
 */
#ifdef CONFIG_VIA_RHINE_MMIO
	u32 quirks = rqNeedEnMMIO;
#else
	u32 quirks = 0;
#endif

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	pr_info_once("%s\n", version);
#endif

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out;

	if (pdev->revision < VTunknown0) {
		quirks |= rqRhineI;
	} else if (pdev->revision >= VT6102) {
		quirks |= rqWOL | rqForceReset;
		if (pdev->revision < VT6105) {
			quirks |= rqStatusWBRace;
		} else {
			quirks |= rqIntPHY;
			if (pdev->revision >= VT6105_B0)
				quirks |= rq6patterns;
			if (pdev->revision >= VT6105M)
				quirks |= rqMgmt;
		}
	}

	/* sanity check */
	if ((pci_resource_len(pdev, 0) < io_size) ||
	    (pci_resource_len(pdev, 1) < io_size)) {
		rc = -EIO;
		dev_err(hwdev, "Insufficient PCI resources, aborting\n");
		goto err_out_pci_disable;
	}

	pioaddr = pci_resource_start(pdev, 0);
	memaddr = pci_resource_start(pdev, 1);

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_pci_disable;

	ioaddr = pci_iomap(pdev, (quirks & rqNeedEnMMIO ? 1 : 0), io_size);
	if (!ioaddr) {
		rc = -EIO;
		dev_err(hwdev,
			"ioremap failed for device %s, region 0x%X @ 0x%lX\n",
			dev_name(hwdev), io_size, memaddr);
		goto err_out_free_res;
	}

	enable_mmio(pioaddr, quirks);

	rc = verify_mmio(hwdev, pioaddr, ioaddr, quirks);
	if (rc)
		goto err_out_unmap;

	rc = rhine_init_one_common(&pdev->dev, quirks,
				   pioaddr, ioaddr, pdev->irq);
	if (!rc)
		return 0;

err_out_unmap:
	pci_iounmap(pdev, ioaddr);
err_out_free_res:
	pci_release_regions(pdev);
err_out_pci_disable:
	pci_disable_device(pdev);
err_out:
	return rc;
}

static int rhine_init_one_platform(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const u32 *quirks;
	int irq;
	struct resource *res;
	void __iomem *ioaddr;

	match = of_match_device(rhine_of_tbl, &pdev->dev);
	if (!match)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ioaddr))
		return PTR_ERR(ioaddr);

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!irq)
		return -EINVAL;

	quirks = match->data;
	if (!quirks)
		return -EINVAL;

	return rhine_init_one_common(&pdev->dev, *quirks,
				     (long)ioaddr, ioaddr, irq);
}

static int alloc_ring(struct net_device* dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	void *ring;
	dma_addr_t ring_dma;

	ring = dma_alloc_coherent(hwdev,
				  RX_RING_SIZE * sizeof(struct rx_desc) +
				  TX_RING_SIZE * sizeof(struct tx_desc),
				  &ring_dma,
				  GFP_ATOMIC);
	if (!ring) {
		netdev_err(dev, "Could not allocate DMA memory\n");
		return -ENOMEM;
	}
	if (rp->quirks & rqRhineI) {
		rp->tx_bufs = dma_alloc_coherent(hwdev,
						 PKT_BUF_SZ * TX_RING_SIZE,
						 &rp->tx_bufs_dma,
						 GFP_ATOMIC);
		if (rp->tx_bufs == NULL) {
			dma_free_coherent(hwdev,
					  RX_RING_SIZE * sizeof(struct rx_desc) +
					  TX_RING_SIZE * sizeof(struct tx_desc),
					  ring, ring_dma);
			return -ENOMEM;
		}
	}

	rp->rx_ring = ring;
	rp->tx_ring = ring + RX_RING_SIZE * sizeof(struct rx_desc);
	rp->rx_ring_dma = ring_dma;
	rp->tx_ring_dma = ring_dma + RX_RING_SIZE * sizeof(struct rx_desc);

	return 0;
}

static void free_ring(struct net_device* dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;

	dma_free_coherent(hwdev,
			  RX_RING_SIZE * sizeof(struct rx_desc) +
			  TX_RING_SIZE * sizeof(struct tx_desc),
			  rp->rx_ring, rp->rx_ring_dma);
	rp->tx_ring = NULL;

	if (rp->tx_bufs)
		dma_free_coherent(hwdev, PKT_BUF_SZ * TX_RING_SIZE,
				  rp->tx_bufs, rp->tx_bufs_dma);

	rp->tx_bufs = NULL;

}

struct rhine_skb_dma {
	struct sk_buff *skb;
	dma_addr_t dma;
};

static inline int rhine_skb_dma_init(struct net_device *dev,
				     struct rhine_skb_dma *sd)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	const int size = rp->rx_buf_sz;

	sd->skb = netdev_alloc_skb(dev, size);
	if (!sd->skb)
		return -ENOMEM;

	sd->dma = dma_map_single(hwdev, sd->skb->data, size, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(hwdev, sd->dma))) {
		netif_err(rp, drv, dev, "Rx DMA mapping failure\n");
		dev_kfree_skb_any(sd->skb);
		return -EIO;
	}

	return 0;
}

static void rhine_reset_rbufs(struct rhine_private *rp)
{
	int i;

	rp->cur_rx = 0;

	for (i = 0; i < RX_RING_SIZE; i++)
		rp->rx_ring[i].rx_status = cpu_to_le32(DescOwn);
}

static inline void rhine_skb_dma_nic_store(struct rhine_private *rp,
					   struct rhine_skb_dma *sd, int entry)
{
	rp->rx_skbuff_dma[entry] = sd->dma;
	rp->rx_skbuff[entry] = sd->skb;

	rp->rx_ring[entry].addr = cpu_to_le32(sd->dma);
	dma_wmb();
}

static void free_rbufs(struct net_device* dev);

static int alloc_rbufs(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	dma_addr_t next;
	int rc, i;

	rp->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	next = rp->rx_ring_dma;

	/* Init the ring entries */
	for (i = 0; i < RX_RING_SIZE; i++) {
		rp->rx_ring[i].rx_status = 0;
		rp->rx_ring[i].desc_length = cpu_to_le32(rp->rx_buf_sz);
		next += sizeof(struct rx_desc);
		rp->rx_ring[i].next_desc = cpu_to_le32(next);
		rp->rx_skbuff[i] = NULL;
	}
	/* Mark the last entry as wrapping the ring. */
	rp->rx_ring[i-1].next_desc = cpu_to_le32(rp->rx_ring_dma);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct rhine_skb_dma sd;

		rc = rhine_skb_dma_init(dev, &sd);
		if (rc < 0) {
			free_rbufs(dev);
			goto out;
		}

		rhine_skb_dma_nic_store(rp, &sd, i);
	}

	rhine_reset_rbufs(rp);
out:
	return rc;
}

static void free_rbufs(struct net_device* dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	int i;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		rp->rx_ring[i].rx_status = 0;
		rp->rx_ring[i].addr = cpu_to_le32(0xBADF00D0); /* An invalid address. */
		if (rp->rx_skbuff[i]) {
			dma_unmap_single(hwdev,
					 rp->rx_skbuff_dma[i],
					 rp->rx_buf_sz, DMA_FROM_DEVICE);
			dev_kfree_skb(rp->rx_skbuff[i]);
		}
		rp->rx_skbuff[i] = NULL;
	}
}

static void alloc_tbufs(struct net_device* dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	dma_addr_t next;
	int i;

	rp->dirty_tx = rp->cur_tx = 0;
	next = rp->tx_ring_dma;
	for (i = 0; i < TX_RING_SIZE; i++) {
		rp->tx_skbuff[i] = NULL;
		rp->tx_ring[i].tx_status = 0;
		rp->tx_ring[i].desc_length = cpu_to_le32(TXDESC);
		next += sizeof(struct tx_desc);
		rp->tx_ring[i].next_desc = cpu_to_le32(next);
		if (rp->quirks & rqRhineI)
			rp->tx_buf[i] = &rp->tx_bufs[i * PKT_BUF_SZ];
	}
	rp->tx_ring[i-1].next_desc = cpu_to_le32(rp->tx_ring_dma);

	netdev_reset_queue(dev);
}

static void free_tbufs(struct net_device* dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	int i;

	for (i = 0; i < TX_RING_SIZE; i++) {
		rp->tx_ring[i].tx_status = 0;
		rp->tx_ring[i].desc_length = cpu_to_le32(TXDESC);
		rp->tx_ring[i].addr = cpu_to_le32(0xBADF00D0); /* An invalid address. */
		if (rp->tx_skbuff[i]) {
			if (rp->tx_skbuff_dma[i]) {
				dma_unmap_single(hwdev,
						 rp->tx_skbuff_dma[i],
						 rp->tx_skbuff[i]->len,
						 DMA_TO_DEVICE);
			}
			dev_kfree_skb(rp->tx_skbuff[i]);
		}
		rp->tx_skbuff[i] = NULL;
		rp->tx_buf[i] = NULL;
	}
}

static void rhine_check_media(struct net_device *dev, unsigned int init_media)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;

	if (!rp->mii_if.force_media)
		mii_check_media(&rp->mii_if, netif_msg_link(rp), init_media);

	if (rp->mii_if.full_duplex)
	    iowrite8(ioread8(ioaddr + ChipCmd1) | Cmd1FDuplex,
		   ioaddr + ChipCmd1);
	else
	    iowrite8(ioread8(ioaddr + ChipCmd1) & ~Cmd1FDuplex,
		   ioaddr + ChipCmd1);

	netif_info(rp, link, dev, "force_media %d, carrier %d\n",
		   rp->mii_if.force_media, netif_carrier_ok(dev));
}

/* Called after status of force_media possibly changed */
static void rhine_set_carrier(struct mii_if_info *mii)
{
	struct net_device *dev = mii->dev;
	struct rhine_private *rp = netdev_priv(dev);

	if (mii->force_media) {
		/* autoneg is off: Link is always assumed to be up */
		if (!netif_carrier_ok(dev))
			netif_carrier_on(dev);
	}

	rhine_check_media(dev, 0);

	netif_info(rp, link, dev, "force_media %d, carrier %d\n",
		   mii->force_media, netif_carrier_ok(dev));
}

/**
 * rhine_set_cam - set CAM multicast filters
 * @ioaddr: register block of this Rhine
 * @idx: multicast CAM index [0..MCAM_SIZE-1]
 * @addr: multicast address (6 bytes)
 *
 * Load addresses into multicast filters.
 */
static void rhine_set_cam(void __iomem *ioaddr, int idx, u8 *addr)
{
	int i;

	iowrite8(CAMC_CAMEN, ioaddr + CamCon);
	wmb();

	/* Paranoid -- idx out of range should never happen */
	idx &= (MCAM_SIZE - 1);

	iowrite8((u8) idx, ioaddr + CamAddr);

	for (i = 0; i < 6; i++, addr++)
		iowrite8(*addr, ioaddr + MulticastFilter0 + i);
	udelay(10);
	wmb();

	iowrite8(CAMC_CAMWR | CAMC_CAMEN, ioaddr + CamCon);
	udelay(10);

	iowrite8(0, ioaddr + CamCon);
}

/**
 * rhine_set_vlan_cam - set CAM VLAN filters
 * @ioaddr: register block of this Rhine
 * @idx: VLAN CAM index [0..VCAM_SIZE-1]
 * @addr: VLAN ID (2 bytes)
 *
 * Load addresses into VLAN filters.
 */
static void rhine_set_vlan_cam(void __iomem *ioaddr, int idx, u8 *addr)
{
	iowrite8(CAMC_CAMEN | CAMC_VCAMSL, ioaddr + CamCon);
	wmb();

	/* Paranoid -- idx out of range should never happen */
	idx &= (VCAM_SIZE - 1);

	iowrite8((u8) idx, ioaddr + CamAddr);

	iowrite16(*((u16 *) addr), ioaddr + MulticastFilter0 + 6);
	udelay(10);
	wmb();

	iowrite8(CAMC_CAMWR | CAMC_CAMEN, ioaddr + CamCon);
	udelay(10);

	iowrite8(0, ioaddr + CamCon);
}

/**
 * rhine_set_cam_mask - set multicast CAM mask
 * @ioaddr: register block of this Rhine
 * @mask: multicast CAM mask
 *
 * Mask sets multicast filters active/inactive.
 */
static void rhine_set_cam_mask(void __iomem *ioaddr, u32 mask)
{
	iowrite8(CAMC_CAMEN, ioaddr + CamCon);
	wmb();

	/* write mask */
	iowrite32(mask, ioaddr + CamMask);

	/* disable CAMEN */
	iowrite8(0, ioaddr + CamCon);
}

/**
 * rhine_set_vlan_cam_mask - set VLAN CAM mask
 * @ioaddr: register block of this Rhine
 * @mask: VLAN CAM mask
 *
 * Mask sets VLAN filters active/inactive.
 */
static void rhine_set_vlan_cam_mask(void __iomem *ioaddr, u32 mask)
{
	iowrite8(CAMC_CAMEN | CAMC_VCAMSL, ioaddr + CamCon);
	wmb();

	/* write mask */
	iowrite32(mask, ioaddr + CamMask);

	/* disable CAMEN */
	iowrite8(0, ioaddr + CamCon);
}

/**
 * rhine_init_cam_filter - initialize CAM filters
 * @dev: network device
 *
 * Initialize (disable) hardware VLAN and multicast support on this
 * Rhine.
 */
static void rhine_init_cam_filter(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;

	/* Disable all CAMs */
	rhine_set_vlan_cam_mask(ioaddr, 0);
	rhine_set_cam_mask(ioaddr, 0);

	/* disable hardware VLAN support */
	BYTE_REG_BITS_ON(TCR_PQEN, ioaddr + TxConfig);
	BYTE_REG_BITS_OFF(BCR1_VIDFR, ioaddr + PCIBusConfig1);
}

/**
 * rhine_update_vcam - update VLAN CAM filters
 * @rp: rhine_private data of this Rhine
 *
 * Update VLAN CAM filters to match configuration change.
 */
static void rhine_update_vcam(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	u16 vid;
	u32 vCAMmask = 0;	/* 32 vCAMs (6105M and better) */
	unsigned int i = 0;

	for_each_set_bit(vid, rp->active_vlans, VLAN_N_VID) {
		rhine_set_vlan_cam(ioaddr, i, (u8 *)&vid);
		vCAMmask |= 1 << i;
		if (++i >= VCAM_SIZE)
			break;
	}
	rhine_set_vlan_cam_mask(ioaddr, vCAMmask);
}

static int rhine_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct rhine_private *rp = netdev_priv(dev);

	spin_lock_bh(&rp->lock);
	set_bit(vid, rp->active_vlans);
	rhine_update_vcam(dev);
	spin_unlock_bh(&rp->lock);
	return 0;
}

static int rhine_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct rhine_private *rp = netdev_priv(dev);

	spin_lock_bh(&rp->lock);
	clear_bit(vid, rp->active_vlans);
	rhine_update_vcam(dev);
	spin_unlock_bh(&rp->lock);
	return 0;
}

static void init_registers(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	int i;

	for (i = 0; i < 6; i++)
		iowrite8(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	iowrite16(0x0006, ioaddr + PCIBusConfig);	/* Tune configuration??? */
	/* Configure initial FIFO thresholds. */
	iowrite8(0x20, ioaddr + TxConfig);
	rp->tx_thresh = 0x20;
	rp->rx_thresh = 0x60;		/* Written in rhine_set_rx_mode(). */

	iowrite32(rp->rx_ring_dma, ioaddr + RxRingPtr);
	iowrite32(rp->tx_ring_dma, ioaddr + TxRingPtr);

	rhine_set_rx_mode(dev);

	if (rp->quirks & rqMgmt)
		rhine_init_cam_filter(dev);

	napi_enable(&rp->napi);

	iowrite16(RHINE_EVENT & 0xffff, ioaddr + IntrEnable);

	iowrite16(CmdStart | CmdTxOn | CmdRxOn | (Cmd1NoTxPoll << 8),
	       ioaddr + ChipCmd);
	rhine_check_media(dev, 1);
}

/* Enable MII link status auto-polling (required for IntrLinkChange) */
static void rhine_enable_linkmon(struct rhine_private *rp)
{
	void __iomem *ioaddr = rp->base;

	iowrite8(0, ioaddr + MIICmd);
	iowrite8(MII_BMSR, ioaddr + MIIRegAddr);
	iowrite8(0x80, ioaddr + MIICmd);

	rhine_wait_bit_high(rp, MIIRegAddr, 0x20);

	iowrite8(MII_BMSR | 0x40, ioaddr + MIIRegAddr);
}

/* Disable MII link status auto-polling (required for MDIO access) */
static void rhine_disable_linkmon(struct rhine_private *rp)
{
	void __iomem *ioaddr = rp->base;

	iowrite8(0, ioaddr + MIICmd);

	if (rp->quirks & rqRhineI) {
		iowrite8(0x01, ioaddr + MIIRegAddr);	// MII_BMSR

		/* Can be called from ISR. Evil. */
		mdelay(1);

		/* 0x80 must be set immediately before turning it off */
		iowrite8(0x80, ioaddr + MIICmd);

		rhine_wait_bit_high(rp, MIIRegAddr, 0x20);

		/* Heh. Now clear 0x80 again. */
		iowrite8(0, ioaddr + MIICmd);
	}
	else
		rhine_wait_bit_high(rp, MIIRegAddr, 0x80);
}

/* Read and write over the MII Management Data I/O (MDIO) interface. */

static int mdio_read(struct net_device *dev, int phy_id, int regnum)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	int result;

	rhine_disable_linkmon(rp);

	/* rhine_disable_linkmon already cleared MIICmd */
	iowrite8(phy_id, ioaddr + MIIPhyAddr);
	iowrite8(regnum, ioaddr + MIIRegAddr);
	iowrite8(0x40, ioaddr + MIICmd);		/* Trigger read */
	rhine_wait_bit_low(rp, MIICmd, 0x40);
	result = ioread16(ioaddr + MIIData);

	rhine_enable_linkmon(rp);
	return result;
}

static void mdio_write(struct net_device *dev, int phy_id, int regnum, int value)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;

	rhine_disable_linkmon(rp);

	/* rhine_disable_linkmon already cleared MIICmd */
	iowrite8(phy_id, ioaddr + MIIPhyAddr);
	iowrite8(regnum, ioaddr + MIIRegAddr);
	iowrite16(value, ioaddr + MIIData);
	iowrite8(0x20, ioaddr + MIICmd);		/* Trigger write */
	rhine_wait_bit_low(rp, MIICmd, 0x20);

	rhine_enable_linkmon(rp);
}

static void rhine_task_disable(struct rhine_private *rp)
{
	mutex_lock(&rp->task_lock);
	rp->task_enable = false;
	mutex_unlock(&rp->task_lock);

	cancel_work_sync(&rp->slow_event_task);
	cancel_work_sync(&rp->reset_task);
}

static void rhine_task_enable(struct rhine_private *rp)
{
	mutex_lock(&rp->task_lock);
	rp->task_enable = true;
	mutex_unlock(&rp->task_lock);
}

static int rhine_open(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	int rc;

	rc = request_irq(rp->irq, rhine_interrupt, IRQF_SHARED, dev->name, dev);
	if (rc)
		goto out;

	netif_dbg(rp, ifup, dev, "%s() irq %d\n", __func__, rp->irq);

	rc = alloc_ring(dev);
	if (rc < 0)
		goto out_free_irq;

	rc = alloc_rbufs(dev);
	if (rc < 0)
		goto out_free_ring;

	alloc_tbufs(dev);
	rhine_chip_reset(dev);
	rhine_task_enable(rp);
	init_registers(dev);

	netif_dbg(rp, ifup, dev, "%s() Done - status %04x MII status: %04x\n",
		  __func__, ioread16(ioaddr + ChipCmd),
		  mdio_read(dev, rp->mii_if.phy_id, MII_BMSR));

	netif_start_queue(dev);

out:
	return rc;

out_free_ring:
	free_ring(dev);
out_free_irq:
	free_irq(rp->irq, dev);
	goto out;
}

static void rhine_reset_task(struct work_struct *work)
{
	struct rhine_private *rp = container_of(work, struct rhine_private,
						reset_task);
	struct net_device *dev = rp->dev;

	mutex_lock(&rp->task_lock);

	if (!rp->task_enable)
		goto out_unlock;

	napi_disable(&rp->napi);
	netif_tx_disable(dev);
	spin_lock_bh(&rp->lock);

	/* clear all descriptors */
	free_tbufs(dev);
	alloc_tbufs(dev);

	rhine_reset_rbufs(rp);

	/* Reinitialize the hardware. */
	rhine_chip_reset(dev);
	init_registers(dev);

	spin_unlock_bh(&rp->lock);

	netif_trans_update(dev); /* prevent tx timeout */
	dev->stats.tx_errors++;
	netif_wake_queue(dev);

out_unlock:
	mutex_unlock(&rp->task_lock);
}

static void rhine_tx_timeout(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;

	netdev_warn(dev, "Transmit timed out, status %04x, PHY status %04x, resetting...\n",
		    ioread16(ioaddr + IntrStatus),
		    mdio_read(dev, rp->mii_if.phy_id, MII_BMSR));

	schedule_work(&rp->reset_task);
}

static inline bool rhine_tx_queue_full(struct rhine_private *rp)
{
	return (rp->cur_tx - rp->dirty_tx) >= TX_QUEUE_LEN;
}

static netdev_tx_t rhine_start_tx(struct sk_buff *skb,
				  struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	void __iomem *ioaddr = rp->base;
	unsigned entry;

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = rp->cur_tx % TX_RING_SIZE;

	if (skb_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	rp->tx_skbuff[entry] = skb;

	if ((rp->quirks & rqRhineI) &&
	    (((unsigned long)skb->data & 3) || skb_shinfo(skb)->nr_frags != 0 || skb->ip_summed == CHECKSUM_PARTIAL)) {
		/* Must use alignment buffer. */
		if (skb->len > PKT_BUF_SZ) {
			/* packet too long, drop it */
			dev_kfree_skb_any(skb);
			rp->tx_skbuff[entry] = NULL;
			dev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}

		/* Padding is not copied and so must be redone. */
		skb_copy_and_csum_dev(skb, rp->tx_buf[entry]);
		if (skb->len < ETH_ZLEN)
			memset(rp->tx_buf[entry] + skb->len, 0,
			       ETH_ZLEN - skb->len);
		rp->tx_skbuff_dma[entry] = 0;
		rp->tx_ring[entry].addr = cpu_to_le32(rp->tx_bufs_dma +
						      (rp->tx_buf[entry] -
						       rp->tx_bufs));
	} else {
		rp->tx_skbuff_dma[entry] =
			dma_map_single(hwdev, skb->data, skb->len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(hwdev, rp->tx_skbuff_dma[entry])) {
			dev_kfree_skb_any(skb);
			rp->tx_skbuff_dma[entry] = 0;
			dev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}
		rp->tx_ring[entry].addr = cpu_to_le32(rp->tx_skbuff_dma[entry]);
	}

	rp->tx_ring[entry].desc_length =
		cpu_to_le32(TXDESC | (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN));

	if (unlikely(skb_vlan_tag_present(skb))) {
		u16 vid_pcp = skb_vlan_tag_get(skb);

		/* drop CFI/DEI bit, register needs VID and PCP */
		vid_pcp = (vid_pcp & VLAN_VID_MASK) |
			  ((vid_pcp & VLAN_PRIO_MASK) >> 1);
		rp->tx_ring[entry].tx_status = cpu_to_le32((vid_pcp) << 16);
		/* request tagging */
		rp->tx_ring[entry].desc_length |= cpu_to_le32(0x020000);
	}
	else
		rp->tx_ring[entry].tx_status = 0;

	netdev_sent_queue(dev, skb->len);
	/* lock eth irq */
	dma_wmb();
	rp->tx_ring[entry].tx_status |= cpu_to_le32(DescOwn);
	wmb();

	rp->cur_tx++;
	/*
	 * Nobody wants cur_tx write to rot for ages after the NIC will have
	 * seen the transmit request, especially as the transmit completion
	 * handler could miss it.
	 */
	smp_wmb();

	/* Non-x86 Todo: explicitly flush cache lines here. */

	if (skb_vlan_tag_present(skb))
		/* Tx queues are bits 7-0 (first Tx queue: bit 7) */
		BYTE_REG_BITS_ON(1 << 7, ioaddr + TQWake);

	/* Wake the potentially-idle transmit channel */
	iowrite8(ioread8(ioaddr + ChipCmd1) | Cmd1TxDemand,
	       ioaddr + ChipCmd1);
	IOSYNC;

	/* dirty_tx may be pessimistically out-of-sync. See rhine_tx. */
	if (rhine_tx_queue_full(rp)) {
		netif_stop_queue(dev);
		smp_rmb();
		/* Rejuvenate. */
		if (!rhine_tx_queue_full(rp))
			netif_wake_queue(dev);
	}

	netif_dbg(rp, tx_queued, dev, "Transmit frame #%d queued in slot %d\n",
		  rp->cur_tx - 1, entry);

	return NETDEV_TX_OK;
}

static void rhine_irq_disable(struct rhine_private *rp)
{
	iowrite16(0x0000, rp->base + IntrEnable);
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t rhine_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct rhine_private *rp = netdev_priv(dev);
	u32 status;
	int handled = 0;

	status = rhine_get_events(rp);

	netif_dbg(rp, intr, dev, "Interrupt, status %08x\n", status);

	if (status & RHINE_EVENT) {
		handled = 1;

		rhine_irq_disable(rp);
		napi_schedule(&rp->napi);
	}

	if (status & ~(IntrLinkChange | IntrStatsMax | RHINE_EVENT_NAPI)) {
		netif_err(rp, intr, dev, "Something Wicked happened! %08x\n",
			  status);
	}

	return IRQ_RETVAL(handled);
}

/* This routine is logically part of the interrupt handler, but isolated
   for clarity. */
static void rhine_tx(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	unsigned int pkts_compl = 0, bytes_compl = 0;
	unsigned int dirty_tx = rp->dirty_tx;
	unsigned int cur_tx;
	struct sk_buff *skb;

	/*
	 * The race with rhine_start_tx does not matter here as long as the
	 * driver enforces a value of cur_tx that was relevant when the
	 * packet was scheduled to the network chipset.
	 * Executive summary: smp_rmb() balances smp_wmb() in rhine_start_tx.
	 */
	smp_rmb();
	cur_tx = rp->cur_tx;
	/* find and cleanup dirty tx descriptors */
	while (dirty_tx != cur_tx) {
		unsigned int entry = dirty_tx % TX_RING_SIZE;
		u32 txstatus = le32_to_cpu(rp->tx_ring[entry].tx_status);

		netif_dbg(rp, tx_done, dev, "Tx scavenge %d status %08x\n",
			  entry, txstatus);
		if (txstatus & DescOwn)
			break;
		skb = rp->tx_skbuff[entry];
		if (txstatus & 0x8000) {
			netif_dbg(rp, tx_done, dev,
				  "Transmit error, Tx status %08x\n", txstatus);
			dev->stats.tx_errors++;
			if (txstatus & 0x0400)
				dev->stats.tx_carrier_errors++;
			if (txstatus & 0x0200)
				dev->stats.tx_window_errors++;
			if (txstatus & 0x0100)
				dev->stats.tx_aborted_errors++;
			if (txstatus & 0x0080)
				dev->stats.tx_heartbeat_errors++;
			if (((rp->quirks & rqRhineI) && txstatus & 0x0002) ||
			    (txstatus & 0x0800) || (txstatus & 0x1000)) {
				dev->stats.tx_fifo_errors++;
				rp->tx_ring[entry].tx_status = cpu_to_le32(DescOwn);
				break; /* Keep the skb - we try again */
			}
			/* Transmitter restarted in 'abnormal' handler. */
		} else {
			if (rp->quirks & rqRhineI)
				dev->stats.collisions += (txstatus >> 3) & 0x0F;
			else
				dev->stats.collisions += txstatus & 0x0F;
			netif_dbg(rp, tx_done, dev, "collisions: %1.1x:%1.1x\n",
				  (txstatus >> 3) & 0xF, txstatus & 0xF);

			u64_stats_update_begin(&rp->tx_stats.syncp);
			rp->tx_stats.bytes += skb->len;
			rp->tx_stats.packets++;
			u64_stats_update_end(&rp->tx_stats.syncp);
		}
		/* Free the original skb. */
		if (rp->tx_skbuff_dma[entry]) {
			dma_unmap_single(hwdev,
					 rp->tx_skbuff_dma[entry],
					 skb->len,
					 DMA_TO_DEVICE);
		}
		bytes_compl += skb->len;
		pkts_compl++;
		dev_consume_skb_any(skb);
		rp->tx_skbuff[entry] = NULL;
		dirty_tx++;
	}

	rp->dirty_tx = dirty_tx;
	/* Pity we can't rely on the nearby BQL completion implicit barrier. */
	smp_wmb();

	netdev_completed_queue(dev, pkts_compl, bytes_compl);

	/* cur_tx may be optimistically out-of-sync. See rhine_start_tx. */
	if (!rhine_tx_queue_full(rp) && netif_queue_stopped(dev)) {
		netif_wake_queue(dev);
		smp_rmb();
		/* Rejuvenate. */
		if (rhine_tx_queue_full(rp))
			netif_stop_queue(dev);
	}
}

/**
 * rhine_get_vlan_tci - extract TCI from Rx data buffer
 * @skb: pointer to sk_buff
 * @data_size: used data area of the buffer including CRC
 *
 * If hardware VLAN tag extraction is enabled and the chip indicates a 802.1Q
 * packet, the extracted 802.1Q header (2 bytes TPID + 2 bytes TCI) is 4-byte
 * aligned following the CRC.
 */
static inline u16 rhine_get_vlan_tci(struct sk_buff *skb, int data_size)
{
	u8 *trailer = (u8 *)skb->data + ((data_size + 3) & ~3) + 2;
	return be16_to_cpup((__be16 *)trailer);
}

static inline void rhine_rx_vlan_tag(struct sk_buff *skb, struct rx_desc *desc,
				     int data_size)
{
	dma_rmb();
	if (unlikely(desc->desc_length & cpu_to_le32(DescTag))) {
		u16 vlan_tci;

		vlan_tci = rhine_get_vlan_tci(skb, data_size);
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tci);
	}
}

/* Process up to limit frames from receive ring */
static int rhine_rx(struct net_device *dev, int limit)
{
	struct rhine_private *rp = netdev_priv(dev);
	struct device *hwdev = dev->dev.parent;
	int entry = rp->cur_rx % RX_RING_SIZE;
	int count;

	netif_dbg(rp, rx_status, dev, "%s(), entry %d status %08x\n", __func__,
		  entry, le32_to_cpu(rp->rx_ring[entry].rx_status));

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	for (count = 0; count < limit; ++count) {
		struct rx_desc *desc = rp->rx_ring + entry;
		u32 desc_status = le32_to_cpu(desc->rx_status);
		int data_size = desc_status >> 16;

		if (desc_status & DescOwn)
			break;

		netif_dbg(rp, rx_status, dev, "%s() status %08x\n", __func__,
			  desc_status);

		if ((desc_status & (RxWholePkt | RxErr)) != RxWholePkt) {
			if ((desc_status & RxWholePkt) != RxWholePkt) {
				netdev_warn(dev,
	"Oversized Ethernet frame spanned multiple buffers, "
	"entry %#x length %d status %08x!\n",
					    entry, data_size,
					    desc_status);
				dev->stats.rx_length_errors++;
			} else if (desc_status & RxErr) {
				/* There was a error. */
				netif_dbg(rp, rx_err, dev,
					  "%s() Rx error %08x\n", __func__,
					  desc_status);
				dev->stats.rx_errors++;
				if (desc_status & 0x0030)
					dev->stats.rx_length_errors++;
				if (desc_status & 0x0048)
					dev->stats.rx_fifo_errors++;
				if (desc_status & 0x0004)
					dev->stats.rx_frame_errors++;
				if (desc_status & 0x0002) {
					/* this can also be updated outside the interrupt handler */
					spin_lock(&rp->lock);
					dev->stats.rx_crc_errors++;
					spin_unlock(&rp->lock);
				}
			}
		} else {
			/* Length should omit the CRC */
			int pkt_len = data_size - 4;
			struct sk_buff *skb;

			/* Check if the packet is long enough to accept without
			   copying to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak) {
				skb = netdev_alloc_skb_ip_align(dev, pkt_len);
				if (unlikely(!skb))
					goto drop;

				dma_sync_single_for_cpu(hwdev,
							rp->rx_skbuff_dma[entry],
							rp->rx_buf_sz,
							DMA_FROM_DEVICE);

				skb_copy_to_linear_data(skb,
						 rp->rx_skbuff[entry]->data,
						 pkt_len);

				dma_sync_single_for_device(hwdev,
							   rp->rx_skbuff_dma[entry],
							   rp->rx_buf_sz,
							   DMA_FROM_DEVICE);
			} else {
				struct rhine_skb_dma sd;

				if (unlikely(rhine_skb_dma_init(dev, &sd) < 0))
					goto drop;

				skb = rp->rx_skbuff[entry];

				dma_unmap_single(hwdev,
						 rp->rx_skbuff_dma[entry],
						 rp->rx_buf_sz,
						 DMA_FROM_DEVICE);
				rhine_skb_dma_nic_store(rp, &sd, entry);
			}

			skb_put(skb, pkt_len);

			rhine_rx_vlan_tag(skb, desc, data_size);

			skb->protocol = eth_type_trans(skb, dev);

			netif_receive_skb(skb);

			u64_stats_update_begin(&rp->rx_stats.syncp);
			rp->rx_stats.bytes += pkt_len;
			rp->rx_stats.packets++;
			u64_stats_update_end(&rp->rx_stats.syncp);
		}
give_descriptor_to_nic:
		desc->rx_status = cpu_to_le32(DescOwn);
		entry = (++rp->cur_rx) % RX_RING_SIZE;
	}

	return count;

drop:
	dev->stats.rx_dropped++;
	goto give_descriptor_to_nic;
}

static void rhine_restart_tx(struct net_device *dev) {
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	int entry = rp->dirty_tx % TX_RING_SIZE;
	u32 intr_status;

	/*
	 * If new errors occurred, we need to sort them out before doing Tx.
	 * In that case the ISR will be back here RSN anyway.
	 */
	intr_status = rhine_get_events(rp);

	if ((intr_status & IntrTxErrSummary) == 0) {

		/* We know better than the chip where it should continue. */
		iowrite32(rp->tx_ring_dma + entry * sizeof(struct tx_desc),
		       ioaddr + TxRingPtr);

		iowrite8(ioread8(ioaddr + ChipCmd) | CmdTxOn,
		       ioaddr + ChipCmd);

		if (rp->tx_ring[entry].desc_length & cpu_to_le32(0x020000))
			/* Tx queues are bits 7-0 (first Tx queue: bit 7) */
			BYTE_REG_BITS_ON(1 << 7, ioaddr + TQWake);

		iowrite8(ioread8(ioaddr + ChipCmd1) | Cmd1TxDemand,
		       ioaddr + ChipCmd1);
		IOSYNC;
	}
	else {
		/* This should never happen */
		netif_warn(rp, tx_err, dev, "another error occurred %08x\n",
			   intr_status);
	}

}

static void rhine_slow_event_task(struct work_struct *work)
{
	struct rhine_private *rp =
		container_of(work, struct rhine_private, slow_event_task);
	struct net_device *dev = rp->dev;
	u32 intr_status;

	mutex_lock(&rp->task_lock);

	if (!rp->task_enable)
		goto out_unlock;

	intr_status = rhine_get_events(rp);
	rhine_ack_events(rp, intr_status & RHINE_EVENT_SLOW);

	if (intr_status & IntrLinkChange)
		rhine_check_media(dev, 0);

	if (intr_status & IntrPCIErr)
		netif_warn(rp, hw, dev, "PCI error\n");

	iowrite16(RHINE_EVENT & 0xffff, rp->base + IntrEnable);

out_unlock:
	mutex_unlock(&rp->task_lock);
}

static void
rhine_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct rhine_private *rp = netdev_priv(dev);
	unsigned int start;

	spin_lock_bh(&rp->lock);
	rhine_update_rx_crc_and_missed_errord(rp);
	spin_unlock_bh(&rp->lock);

	netdev_stats_to_stats64(stats, &dev->stats);

	do {
		start = u64_stats_fetch_begin_irq(&rp->rx_stats.syncp);
		stats->rx_packets = rp->rx_stats.packets;
		stats->rx_bytes = rp->rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&rp->rx_stats.syncp, start));

	do {
		start = u64_stats_fetch_begin_irq(&rp->tx_stats.syncp);
		stats->tx_packets = rp->tx_stats.packets;
		stats->tx_bytes = rp->tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&rp->tx_stats.syncp, start));
}

static void rhine_set_rx_mode(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;
	u32 mc_filter[2];	/* Multicast hash filter */
	u8 rx_mode = 0x0C;	/* Note: 0x02=accept runt, 0x01=accept errs */
	struct netdev_hw_addr *ha;

	if (dev->flags & IFF_PROMISC) {		/* Set promiscuous. */
		rx_mode = 0x1C;
		iowrite32(0xffffffff, ioaddr + MulticastFilter0);
		iowrite32(0xffffffff, ioaddr + MulticastFilter1);
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		iowrite32(0xffffffff, ioaddr + MulticastFilter0);
		iowrite32(0xffffffff, ioaddr + MulticastFilter1);
	} else if (rp->quirks & rqMgmt) {
		int i = 0;
		u32 mCAMmask = 0;	/* 32 mCAMs (6105M and better) */
		netdev_for_each_mc_addr(ha, dev) {
			if (i == MCAM_SIZE)
				break;
			rhine_set_cam(ioaddr, i, ha->addr);
			mCAMmask |= 1 << i;
			i++;
		}
		rhine_set_cam_mask(ioaddr, mCAMmask);
	} else {
		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, dev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;

			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
		}
		iowrite32(mc_filter[0], ioaddr + MulticastFilter0);
		iowrite32(mc_filter[1], ioaddr + MulticastFilter1);
	}
	/* enable/disable VLAN receive filtering */
	if (rp->quirks & rqMgmt) {
		if (dev->flags & IFF_PROMISC)
			BYTE_REG_BITS_OFF(BCR1_VIDFR, ioaddr + PCIBusConfig1);
		else
			BYTE_REG_BITS_ON(BCR1_VIDFR, ioaddr + PCIBusConfig1);
	}
	BYTE_REG_BITS_ON(rx_mode, ioaddr + RxConfig);
}

static void netdev_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct device *hwdev = dev->dev.parent;

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(hwdev), sizeof(info->bus_info));
}

static int netdev_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct rhine_private *rp = netdev_priv(dev);

	mutex_lock(&rp->task_lock);
	mii_ethtool_get_link_ksettings(&rp->mii_if, cmd);
	mutex_unlock(&rp->task_lock);

	return 0;
}

static int netdev_set_link_ksettings(struct net_device *dev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct rhine_private *rp = netdev_priv(dev);
	int rc;

	mutex_lock(&rp->task_lock);
	rc = mii_ethtool_set_link_ksettings(&rp->mii_if, cmd);
	rhine_set_carrier(&rp->mii_if);
	mutex_unlock(&rp->task_lock);

	return rc;
}

static int netdev_nway_reset(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);

	return mii_nway_restart(&rp->mii_if);
}

static u32 netdev_get_link(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);

	return mii_link_ok(&rp->mii_if);
}

static u32 netdev_get_msglevel(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);

	return rp->msg_enable;
}

static void netdev_set_msglevel(struct net_device *dev, u32 value)
{
	struct rhine_private *rp = netdev_priv(dev);

	rp->msg_enable = value;
}

static void rhine_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rhine_private *rp = netdev_priv(dev);

	if (!(rp->quirks & rqWOL))
		return;

	spin_lock_irq(&rp->lock);
	wol->supported = WAKE_PHY | WAKE_MAGIC |
			 WAKE_UCAST | WAKE_MCAST | WAKE_BCAST;	/* Untested */
	wol->wolopts = rp->wolopts;
	spin_unlock_irq(&rp->lock);
}

static int rhine_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rhine_private *rp = netdev_priv(dev);
	u32 support = WAKE_PHY | WAKE_MAGIC |
		      WAKE_UCAST | WAKE_MCAST | WAKE_BCAST;	/* Untested */

	if (!(rp->quirks & rqWOL))
		return -EINVAL;

	if (wol->wolopts & ~support)
		return -EINVAL;

	spin_lock_irq(&rp->lock);
	rp->wolopts = wol->wolopts;
	spin_unlock_irq(&rp->lock);

	return 0;
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.nway_reset		= netdev_nway_reset,
	.get_link		= netdev_get_link,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
	.get_wol		= rhine_get_wol,
	.set_wol		= rhine_set_wol,
	.get_link_ksettings	= netdev_get_link_ksettings,
	.set_link_ksettings	= netdev_set_link_ksettings,
};

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct rhine_private *rp = netdev_priv(dev);
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	mutex_lock(&rp->task_lock);
	rc = generic_mii_ioctl(&rp->mii_if, if_mii(rq), cmd, NULL);
	rhine_set_carrier(&rp->mii_if);
	mutex_unlock(&rp->task_lock);

	return rc;
}

static int rhine_close(struct net_device *dev)
{
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;

	rhine_task_disable(rp);
	napi_disable(&rp->napi);
	netif_stop_queue(dev);

	netif_dbg(rp, ifdown, dev, "Shutting down ethercard, status was %04x\n",
		  ioread16(ioaddr + ChipCmd));

	/* Switch to loopback mode to avoid hardware races. */
	iowrite8(rp->tx_thresh | 0x02, ioaddr + TxConfig);

	rhine_irq_disable(rp);

	/* Stop the chip's Tx and Rx processes. */
	iowrite16(CmdStop, ioaddr + ChipCmd);

	free_irq(rp->irq, dev);
	free_rbufs(dev);
	free_tbufs(dev);
	free_ring(dev);

	return 0;
}


static void rhine_remove_one_pci(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rhine_private *rp = netdev_priv(dev);

	unregister_netdev(dev);

	pci_iounmap(pdev, rp->base);
	pci_release_regions(pdev);

	free_netdev(dev);
	pci_disable_device(pdev);
}

static int rhine_remove_one_platform(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct rhine_private *rp = netdev_priv(dev);

	unregister_netdev(dev);

	iounmap(rp->base);

	free_netdev(dev);

	return 0;
}

static void rhine_shutdown_pci(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rhine_private *rp = netdev_priv(dev);
	void __iomem *ioaddr = rp->base;

	if (!(rp->quirks & rqWOL))
		return; /* Nothing to do for non-WOL adapters */

	rhine_power_init(dev);

	/* Make sure we use pattern 0, 1 and not 4, 5 */
	if (rp->quirks & rq6patterns)
		iowrite8(0x04, ioaddr + WOLcgClr);

	spin_lock(&rp->lock);

	if (rp->wolopts & WAKE_MAGIC) {
		iowrite8(WOLmagic, ioaddr + WOLcrSet);
		/*
		 * Turn EEPROM-controlled wake-up back on -- some hardware may
		 * not cooperate otherwise.
		 */
		iowrite8(ioread8(ioaddr + ConfigA) | 0x03, ioaddr + ConfigA);
	}

	if (rp->wolopts & (WAKE_BCAST|WAKE_MCAST))
		iowrite8(WOLbmcast, ioaddr + WOLcgSet);

	if (rp->wolopts & WAKE_PHY)
		iowrite8(WOLlnkon | WOLlnkoff, ioaddr + WOLcrSet);

	if (rp->wolopts & WAKE_UCAST)
		iowrite8(WOLucast, ioaddr + WOLcrSet);

	if (rp->wolopts) {
		/* Enable legacy WOL (for old motherboards) */
		iowrite8(0x01, ioaddr + PwcfgSet);
		iowrite8(ioread8(ioaddr + StickyHW) | 0x04, ioaddr + StickyHW);
	}

	spin_unlock(&rp->lock);

	if (system_state == SYSTEM_POWER_OFF && !avoid_D3) {
		iowrite8(ioread8(ioaddr + StickyHW) | 0x03, ioaddr + StickyHW);

		pci_wake_from_d3(pdev, true);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

#ifdef CONFIG_PM_SLEEP
static int rhine_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rhine_private *rp = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	rhine_task_disable(rp);
	rhine_irq_disable(rp);
	napi_disable(&rp->napi);

	netif_device_detach(dev);

	if (dev_is_pci(device))
		rhine_shutdown_pci(to_pci_dev(device));

	return 0;
}

static int rhine_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rhine_private *rp = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	enable_mmio(rp->pioaddr, rp->quirks);
	rhine_power_init(dev);
	free_tbufs(dev);
	alloc_tbufs(dev);
	rhine_reset_rbufs(rp);
	rhine_task_enable(rp);
	spin_lock_bh(&rp->lock);
	init_registers(dev);
	spin_unlock_bh(&rp->lock);

	netif_device_attach(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rhine_pm_ops, rhine_suspend, rhine_resume);
#define RHINE_PM_OPS	(&rhine_pm_ops)

#else

#define RHINE_PM_OPS	NULL

#endif /* !CONFIG_PM_SLEEP */

static struct pci_driver rhine_driver_pci = {
	.name		= DRV_NAME,
	.id_table	= rhine_pci_tbl,
	.probe		= rhine_init_one_pci,
	.remove		= rhine_remove_one_pci,
	.shutdown	= rhine_shutdown_pci,
	.driver.pm	= RHINE_PM_OPS,
};

static struct platform_driver rhine_driver_platform = {
	.probe		= rhine_init_one_platform,
	.remove		= rhine_remove_one_platform,
	.driver = {
		.name	= DRV_NAME,
		.of_match_table	= rhine_of_tbl,
		.pm		= RHINE_PM_OPS,
	}
};

static const struct dmi_system_id rhine_dmi_table[] __initconst = {
	{
		.ident = "EPIA-M",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "Award Software International, Inc."),
			DMI_MATCH(DMI_BIOS_VERSION, "6.00 PG"),
		},
	},
	{
		.ident = "KV7",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies, LTD"),
			DMI_MATCH(DMI_BIOS_VERSION, "6.00 PG"),
		},
	},
	{ NULL }
};

static int __init rhine_init(void)
{
	int ret_pci, ret_platform;

/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	pr_info("%s\n", version);
#endif
	if (dmi_check_system(rhine_dmi_table)) {
		/* these BIOSes fail at PXE boot if chip is in D3 */
		avoid_D3 = true;
		pr_warn("Broken BIOS detected, avoid_D3 enabled\n");
	}
	else if (avoid_D3)
		pr_info("avoid_D3 set\n");

	ret_pci = pci_register_driver(&rhine_driver_pci);
	ret_platform = platform_driver_register(&rhine_driver_platform);
	if ((ret_pci < 0) && (ret_platform < 0))
		return ret_pci;

	return 0;
}


static void __exit rhine_cleanup(void)
{
	platform_driver_unregister(&rhine_driver_platform);
	pci_unregister_driver(&rhine_driver_pci);
}


module_init(rhine_init);
module_exit(rhine_cleanup);

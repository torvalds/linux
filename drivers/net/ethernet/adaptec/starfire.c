/* starfire.c: Linux device driver for the Adaptec Starfire network adapter. */
/*
	Written 1998-2000 by Donald Becker.

	Current maintainer is Ion Badulescu <ionut ta badula tod org>. Please
	send all bug reports to me, and not to Donald Becker, as this code
	has been heavily modified from Donald's original version.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The information below comes from Donald Becker's original driver:

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support and updates available at
	http://www.scyld.com/network/starfire.html
	[link no longer provides useful info -jgarzik]

*/

#define DRV_NAME	"starfire"
#define DRV_VERSION	"2.1"
#define DRV_RELDATE	"July  6, 2008"

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/mm.h>
#include <linux/firmware.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/uaccess.h>
#include <asm/io.h>

/*
 * The current frame processor firmware fails to checksum a fragment
 * of length 1. If and when this is fixed, the #define below can be removed.
 */
#define HAS_BROKEN_FIRMWARE

/*
 * If using the broken firmware, data must be padded to the next 32-bit boundary.
 */
#ifdef HAS_BROKEN_FIRMWARE
#define PADDING_MASK 3
#endif

/*
 * Define this if using the driver with the zero-copy patch
 */
#define ZEROCOPY

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define VLAN_SUPPORT
#endif

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

/* Used for tuning interrupt latency vs. overhead. */
static int intr_latency;
static int small_frames;

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;
static int mtu;
/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   The Starfire has a 512 element hash table based on the Ethernet CRC. */
static const int multicast_filter_limit = 512;
/* Whether to do TCP/UDP checksums in hardware */
static int enable_hw_cksum = 1;

#define PKT_BUF_SZ	1536		/* Size of each temporary Rx buffer.*/
/*
 * Set the copy breakpoint for the copy-only-tiny-frames scheme.
 * Setting to > 1518 effectively disables this feature.
 *
 * NOTE:
 * The ia64 doesn't allow for unaligned loads even of integers being
 * misaligned on a 2 byte boundary. Thus always force copying of
 * packets as the starfire doesn't allow for misaligned DMAs ;-(
 * 23/10/2000 - Jes
 *
 * The Alpha and the Sparc don't like unaligned loads, either. On Sparc64,
 * at least, having unaligned frames leads to a rather serious performance
 * penalty. -Ion
 */
#if defined(__ia64__) || defined(__alpha__) || defined(__sparc__)
static int rx_copybreak = PKT_BUF_SZ;
#else
static int rx_copybreak /* = 0 */;
#endif

/* PCI DMA burst size -- on sparc64 we want to force it to 64 bytes, on the others the default of 128 is fine. */
#ifdef __sparc__
#define DMA_BURST_SIZE 64
#else
#define DMA_BURST_SIZE 128
#endif

/* Operational parameters that are set at compile time. */

/* The "native" ring sizes are either 256 or 2048.
   However in some modes a descriptor may be marked to wrap the ring earlier.
*/
#define RX_RING_SIZE	256
#define TX_RING_SIZE	32
/* The completion queues are fixed at 1024 entries i.e. 4K or 8KB. */
#define DONE_Q_SIZE	1024
/* All queues must be aligned on a 256-byte boundary */
#define QUEUE_ALIGN	256

#if RX_RING_SIZE > 256
#define RX_Q_ENTRIES Rx2048QEntries
#else
#define RX_Q_ENTRIES Rx256QEntries
#endif

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT	(2 * HZ)

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
/* 64-bit dma_addr_t */
#define ADDR_64BITS	/* This chip uses 64 bit addresses. */
#define netdrv_addr_t __le64
#define cpu_to_dma(x) cpu_to_le64(x)
#define dma_to_cpu(x) le64_to_cpu(x)
#define RX_DESC_Q_ADDR_SIZE RxDescQAddr64bit
#define TX_DESC_Q_ADDR_SIZE TxDescQAddr64bit
#define RX_COMPL_Q_ADDR_SIZE RxComplQAddr64bit
#define TX_COMPL_Q_ADDR_SIZE TxComplQAddr64bit
#define RX_DESC_ADDR_SIZE RxDescAddr64bit
#else  /* 32-bit dma_addr_t */
#define netdrv_addr_t __le32
#define cpu_to_dma(x) cpu_to_le32(x)
#define dma_to_cpu(x) le32_to_cpu(x)
#define RX_DESC_Q_ADDR_SIZE RxDescQAddr32bit
#define TX_DESC_Q_ADDR_SIZE TxDescQAddr32bit
#define RX_COMPL_Q_ADDR_SIZE RxComplQAddr32bit
#define TX_COMPL_Q_ADDR_SIZE TxComplQAddr32bit
#define RX_DESC_ADDR_SIZE RxDescAddr32bit
#endif

#define skb_first_frag_len(skb)	skb_headlen(skb)
#define skb_num_frags(skb) (skb_shinfo(skb)->nr_frags + 1)

/* Firmware names */
#define FIRMWARE_RX	"adaptec/starfire_rx.bin"
#define FIRMWARE_TX	"adaptec/starfire_tx.bin"

/* These identify the driver base version and may not be removed. */
static const char version[] =
KERN_INFO "starfire.c:v1.03 7/26/2000  Written by Donald Becker <becker@scyld.com>\n"
" (unofficial 2.2/2.4 kernel port, version " DRV_VERSION ", " DRV_RELDATE ")\n";

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Adaptec Starfire Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_FIRMWARE(FIRMWARE_RX);
MODULE_FIRMWARE(FIRMWARE_TX);

module_param(max_interrupt_work, int, 0);
module_param(mtu, int, 0);
module_param(debug, int, 0);
module_param(rx_copybreak, int, 0);
module_param(intr_latency, int, 0);
module_param(small_frames, int, 0);
module_param(enable_hw_cksum, int, 0);
MODULE_PARM_DESC(max_interrupt_work, "Maximum events handled per interrupt");
MODULE_PARM_DESC(mtu, "MTU (all boards)");
MODULE_PARM_DESC(debug, "Debug level (0-6)");
MODULE_PARM_DESC(rx_copybreak, "Copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(intr_latency, "Maximum interrupt latency, in microseconds");
MODULE_PARM_DESC(small_frames, "Maximum size of receive frames that bypass interrupt latency (0,64,128,256,512)");
MODULE_PARM_DESC(enable_hw_cksum, "Enable/disable hardware cksum support (0/1)");

/*
				Theory of Operation

I. Board Compatibility

This driver is for the Adaptec 6915 "Starfire" 64 bit PCI Ethernet adapter.

II. Board-specific settings

III. Driver operation

IIIa. Ring buffers

The Starfire hardware uses multiple fixed-size descriptor queues/rings.  The
ring sizes are set fixed by the hardware, but may optionally be wrapped
earlier by the END bit in the descriptor.
This driver uses that hardware queue size for the Rx ring, where a large
number of entries has no ill effect beyond increases the potential backlog.
The Tx ring is wrapped with the END bit, since a large hardware Tx queue
disables the queue layer priority ordering and we have no mechanism to
utilize the hardware two-level priority queue.  When modifying the
RX/TX_RING_SIZE pay close attention to page sizes and the ring-empty warning
levels.

IIIb/c. Transmit/Receive Structure

See the Adaptec manual for the many possible structures, and options for
each structure.  There are far too many to document all of them here.

For transmit this driver uses type 0/1 transmit descriptors (depending
on the 32/64 bitness of the architecture), and relies on automatic
minimum-length padding.  It does not use the completion queue
consumer index, but instead checks for non-zero status entries.

For receive this driver uses type 2/3 receive descriptors.  The driver
allocates full frame size skbuffs for the Rx ring buffers, so all frames
should fit in a single descriptor.  The driver does not use the completion
queue consumer index, but instead checks for non-zero status entries.

When an incoming frame is less than RX_COPYBREAK bytes long, a fresh skbuff
is allocated and the frame is copied to the new skbuff.  When the incoming
frame is larger, the skbuff is passed directly up the protocol stack.
Buffers consumed this way are replaced by newly allocated skbuffs in a later
phase of receive.

A notable aspect of operation is that unaligned buffers are not permitted by
the Starfire hardware.  Thus the IP header at offset 14 in an ethernet frame
isn't longword aligned, which may cause problems on some machine
e.g. Alphas and IA64. For these architectures, the driver is forced to copy
the frame into a new skbuff unconditionally. Copied frames are put into the
skbuff at an offset of "+2", thus 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring and the netif_queue
status. If the number of free Tx slots in the ring falls below a certain number
(currently hardcoded to 4), it signals the upper layer to stop the queue.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the netif_queue is stopped and the
number of free Tx slow is above the threshold, it signals the upper layer to
restart the queue.

IV. Notes

IVb. References

The Adaptec Starfire manuals, available only from Adaptec.
http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html

IVc. Errata

- StopOnPerr is broken, don't enable
- Hardware ethernet padding exposes random data, perform software padding
  instead (unverified -- works correctly for all the hardware I have)

*/



enum chip_capability_flags {CanHaveMII=1, };

enum chipset {
	CH_6915 = 0,
};

static const struct pci_device_id starfire_pci_tbl[] = {
	{ PCI_VDEVICE(ADAPTEC, 0x6915), CH_6915 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, starfire_pci_tbl);

/* A chip capabilities table, matching the CH_xxx entries in xxx_pci_tbl[] above. */
static const struct chip_info {
	const char *name;
	int drv_flags;
} netdrv_tbl[] = {
	{ "Adaptec Starfire 6915", CanHaveMII },
};


/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.  The name can only partially document the semantics and make
   the driver longer and more difficult to read.
   In general, only the important configuration values or bits changed
   multiple times should be defined symbolically.
*/
enum register_offsets {
	PCIDeviceConfig=0x50040, GenCtrl=0x50070, IntrTimerCtrl=0x50074,
	IntrClear=0x50080, IntrStatus=0x50084, IntrEnable=0x50088,
	MIICtrl=0x52000, TxStationAddr=0x50120, EEPROMCtrl=0x51000,
	GPIOCtrl=0x5008C, TxDescCtrl=0x50090,
	TxRingPtr=0x50098, HiPriTxRingPtr=0x50094, /* Low and High priority. */
	TxRingHiAddr=0x5009C,		/* 64 bit address extension. */
	TxProducerIdx=0x500A0, TxConsumerIdx=0x500A4,
	TxThreshold=0x500B0,
	CompletionHiAddr=0x500B4, TxCompletionAddr=0x500B8,
	RxCompletionAddr=0x500BC, RxCompletionQ2Addr=0x500C0,
	CompletionQConsumerIdx=0x500C4, RxDMACtrl=0x500D0,
	RxDescQCtrl=0x500D4, RxDescQHiAddr=0x500DC, RxDescQAddr=0x500E0,
	RxDescQIdx=0x500E8, RxDMAStatus=0x500F0, RxFilterMode=0x500F4,
	TxMode=0x55000, VlanType=0x55064,
	PerfFilterTable=0x56000, HashTable=0x56100,
	TxGfpMem=0x58000, RxGfpMem=0x5a000,
};

/*
 * Bits in the interrupt status/mask registers.
 * Warning: setting Intr[Ab]NormalSummary in the IntrEnable register
 * enables all the interrupt sources that are or'ed into those status bits.
 */
enum intr_status_bits {
	IntrLinkChange=0xf0000000, IntrStatsMax=0x08000000,
	IntrAbnormalSummary=0x02000000, IntrGeneralTimer=0x01000000,
	IntrSoftware=0x800000, IntrRxComplQ1Low=0x400000,
	IntrTxComplQLow=0x200000, IntrPCI=0x100000,
	IntrDMAErr=0x080000, IntrTxDataLow=0x040000,
	IntrRxComplQ2Low=0x020000, IntrRxDescQ1Low=0x010000,
	IntrNormalSummary=0x8000, IntrTxDone=0x4000,
	IntrTxDMADone=0x2000, IntrTxEmpty=0x1000,
	IntrEarlyRxQ2=0x0800, IntrEarlyRxQ1=0x0400,
	IntrRxQ2Done=0x0200, IntrRxQ1Done=0x0100,
	IntrRxGFPDead=0x80, IntrRxDescQ2Low=0x40,
	IntrNoTxCsum=0x20, IntrTxBadID=0x10,
	IntrHiPriTxBadID=0x08, IntrRxGfp=0x04,
	IntrTxGfp=0x02, IntrPCIPad=0x01,
	/* not quite bits */
	IntrRxDone=IntrRxQ2Done | IntrRxQ1Done,
	IntrRxEmpty=IntrRxDescQ1Low | IntrRxDescQ2Low,
	IntrNormalMask=0xff00, IntrAbnormalMask=0x3ff00fe,
};

/* Bits in the RxFilterMode register. */
enum rx_mode_bits {
	AcceptBroadcast=0x04, AcceptAllMulticast=0x02, AcceptAll=0x01,
	AcceptMulticast=0x10, PerfectFilter=0x40, HashFilter=0x30,
	PerfectFilterVlan=0x80, MinVLANPrio=0xE000, VlanMode=0x0200,
	WakeupOnGFP=0x0800,
};

/* Bits in the TxMode register */
enum tx_mode_bits {
	MiiSoftReset=0x8000, MIILoopback=0x4000,
	TxFlowEnable=0x0800, RxFlowEnable=0x0400,
	PadEnable=0x04, FullDuplex=0x02, HugeFrame=0x01,
};

/* Bits in the TxDescCtrl register. */
enum tx_ctrl_bits {
	TxDescSpaceUnlim=0x00, TxDescSpace32=0x10, TxDescSpace64=0x20,
	TxDescSpace128=0x30, TxDescSpace256=0x40,
	TxDescType0=0x00, TxDescType1=0x01, TxDescType2=0x02,
	TxDescType3=0x03, TxDescType4=0x04,
	TxNoDMACompletion=0x08,
	TxDescQAddr64bit=0x80, TxDescQAddr32bit=0,
	TxHiPriFIFOThreshShift=24, TxPadLenShift=16,
	TxDMABurstSizeShift=8,
};

/* Bits in the RxDescQCtrl register. */
enum rx_ctrl_bits {
	RxBufferLenShift=16, RxMinDescrThreshShift=0,
	RxPrefetchMode=0x8000, RxVariableQ=0x2000,
	Rx2048QEntries=0x4000, Rx256QEntries=0,
	RxDescAddr64bit=0x1000, RxDescAddr32bit=0,
	RxDescQAddr64bit=0x0100, RxDescQAddr32bit=0,
	RxDescSpace4=0x000, RxDescSpace8=0x100,
	RxDescSpace16=0x200, RxDescSpace32=0x300,
	RxDescSpace64=0x400, RxDescSpace128=0x500,
	RxConsumerWrEn=0x80,
};

/* Bits in the RxDMACtrl register. */
enum rx_dmactrl_bits {
	RxReportBadFrames=0x80000000, RxDMAShortFrames=0x40000000,
	RxDMABadFrames=0x20000000, RxDMACrcErrorFrames=0x10000000,
	RxDMAControlFrame=0x08000000, RxDMAPauseFrame=0x04000000,
	RxChecksumIgnore=0, RxChecksumRejectTCPUDP=0x02000000,
	RxChecksumRejectTCPOnly=0x01000000,
	RxCompletionQ2Enable=0x800000,
	RxDMAQ2Disable=0, RxDMAQ2FPOnly=0x100000,
	RxDMAQ2SmallPkt=0x200000, RxDMAQ2HighPrio=0x300000,
	RxDMAQ2NonIP=0x400000,
	RxUseBackupQueue=0x080000, RxDMACRC=0x040000,
	RxEarlyIntThreshShift=12, RxHighPrioThreshShift=8,
	RxBurstSizeShift=0,
};

/* Bits in the RxCompletionAddr register */
enum rx_compl_bits {
	RxComplQAddr64bit=0x80, RxComplQAddr32bit=0,
	RxComplProducerWrEn=0x40,
	RxComplType0=0x00, RxComplType1=0x10,
	RxComplType2=0x20, RxComplType3=0x30,
	RxComplThreshShift=0,
};

/* Bits in the TxCompletionAddr register */
enum tx_compl_bits {
	TxComplQAddr64bit=0x80, TxComplQAddr32bit=0,
	TxComplProducerWrEn=0x40,
	TxComplIntrStatus=0x20,
	CommonQueueMode=0x10,
	TxComplThreshShift=0,
};

/* Bits in the GenCtrl register */
enum gen_ctrl_bits {
	RxEnable=0x05, TxEnable=0x0a,
	RxGFPEnable=0x10, TxGFPEnable=0x20,
};

/* Bits in the IntrTimerCtrl register */
enum intr_ctrl_bits {
	Timer10X=0x800, EnableIntrMasking=0x60, SmallFrameBypass=0x100,
	SmallFrame64=0, SmallFrame128=0x200, SmallFrame256=0x400, SmallFrame512=0x600,
	IntrLatencyMask=0x1f,
};

/* The Rx and Tx buffer descriptors. */
struct starfire_rx_desc {
	netdrv_addr_t rxaddr;
};
enum rx_desc_bits {
	RxDescValid=1, RxDescEndRing=2,
};

/* Completion queue entry. */
struct short_rx_done_desc {
	__le32 status;			/* Low 16 bits is length. */
};
struct basic_rx_done_desc {
	__le32 status;			/* Low 16 bits is length. */
	__le16 vlanid;
	__le16 status2;
};
struct csum_rx_done_desc {
	__le32 status;			/* Low 16 bits is length. */
	__le16 csum;			/* Partial checksum */
	__le16 status2;
};
struct full_rx_done_desc {
	__le32 status;			/* Low 16 bits is length. */
	__le16 status3;
	__le16 status2;
	__le16 vlanid;
	__le16 csum;			/* partial checksum */
	__le32 timestamp;
};
/* XXX: this is ugly and I'm not sure it's worth the trouble -Ion */
#ifdef VLAN_SUPPORT
typedef struct full_rx_done_desc rx_done_desc;
#define RxComplType RxComplType3
#else  /* not VLAN_SUPPORT */
typedef struct csum_rx_done_desc rx_done_desc;
#define RxComplType RxComplType2
#endif /* not VLAN_SUPPORT */

enum rx_done_bits {
	RxOK=0x20000000, RxFIFOErr=0x10000000, RxBufQ2=0x08000000,
};

/* Type 1 Tx descriptor. */
struct starfire_tx_desc_1 {
	__le32 status;			/* Upper bits are status, lower 16 length. */
	__le32 addr;
};

/* Type 2 Tx descriptor. */
struct starfire_tx_desc_2 {
	__le32 status;			/* Upper bits are status, lower 16 length. */
	__le32 reserved;
	__le64 addr;
};

#ifdef ADDR_64BITS
typedef struct starfire_tx_desc_2 starfire_tx_desc;
#define TX_DESC_TYPE TxDescType2
#else  /* not ADDR_64BITS */
typedef struct starfire_tx_desc_1 starfire_tx_desc;
#define TX_DESC_TYPE TxDescType1
#endif /* not ADDR_64BITS */
#define TX_DESC_SPACING TxDescSpaceUnlim

enum tx_desc_bits {
	TxDescID=0xB0000000,
	TxCRCEn=0x01000000, TxDescIntr=0x08000000,
	TxRingWrap=0x04000000, TxCalTCP=0x02000000,
};
struct tx_done_desc {
	__le32 status;			/* timestamp, index. */
#if 0
	__le32 intrstatus;		/* interrupt status */
#endif
};

struct rx_ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};
struct tx_ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
	unsigned int used_slots;
};

#define PHY_CNT		2
struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct starfire_rx_desc *rx_ring;
	starfire_tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	/* The addresses of rx/tx-in-place skbuffs. */
	struct rx_ring_info rx_info[RX_RING_SIZE];
	struct tx_ring_info tx_info[TX_RING_SIZE];
	/* Pointers to completion queues (full pages). */
	rx_done_desc *rx_done_q;
	dma_addr_t rx_done_q_dma;
	unsigned int rx_done;
	struct tx_done_desc *tx_done_q;
	dma_addr_t tx_done_q_dma;
	unsigned int tx_done;
	struct napi_struct napi;
	struct net_device *dev;
	struct pci_dev *pci_dev;
#ifdef VLAN_SUPPORT
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
#endif
	void *queue_mem;
	dma_addr_t queue_mem_dma;
	size_t queue_mem_size;

	/* Frequently used values: keep some adjacent for cache effect. */
	spinlock_t lock;
	unsigned int cur_rx, dirty_rx;	/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx, reap_tx;
	unsigned int rx_buf_sz;		/* Based on MTU+slack. */
	/* These values keep track of the transceiver/media in use. */
	int speed100;			/* Set if speed == 100MBit. */
	u32 tx_mode;
	u32 intr_timer_ctrl;
	u8 tx_threshold;
	/* MII transceiver section. */
	struct mii_if_info mii_if;		/* MII lib hooks/info */
	int phy_cnt;			/* MII device addresses. */
	unsigned char phys[PHY_CNT];	/* MII device addresses. */
	void __iomem *base;
};


static int	mdio_read(struct net_device *dev, int phy_id, int location);
static void	mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int	netdev_open(struct net_device *dev);
static void	check_duplex(struct net_device *dev);
static void	tx_timeout(struct net_device *dev);
static void	init_ring(struct net_device *dev);
static netdev_tx_t start_tx(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t intr_handler(int irq, void *dev_instance);
static void	netdev_error(struct net_device *dev, int intr_status);
static int	__netdev_rx(struct net_device *dev, int *quota);
static int	netdev_poll(struct napi_struct *napi, int budget);
static void	refill_rx_ring(struct net_device *dev);
static void	netdev_error(struct net_device *dev, int intr_status);
static void	set_rx_mode(struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static int	netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int	netdev_close(struct net_device *dev);
static void	netdev_media_change(struct net_device *dev);
static const struct ethtool_ops ethtool_ops;


#ifdef VLAN_SUPPORT
static int netdev_vlan_rx_add_vid(struct net_device *dev,
				  __be16 proto, u16 vid)
{
	struct netdev_private *np = netdev_priv(dev);

	spin_lock(&np->lock);
	if (debug > 1)
		printk("%s: Adding vlanid %d to vlan filter\n", dev->name, vid);
	set_bit(vid, np->active_vlans);
	set_rx_mode(dev);
	spin_unlock(&np->lock);

	return 0;
}

static int netdev_vlan_rx_kill_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct netdev_private *np = netdev_priv(dev);

	spin_lock(&np->lock);
	if (debug > 1)
		printk("%s: removing vlanid %d from vlan filter\n", dev->name, vid);
	clear_bit(vid, np->active_vlans);
	set_rx_mode(dev);
	spin_unlock(&np->lock);

	return 0;
}
#endif /* VLAN_SUPPORT */


static const struct net_device_ops netdev_ops = {
	.ndo_open		= netdev_open,
	.ndo_stop		= netdev_close,
	.ndo_start_xmit		= start_tx,
	.ndo_tx_timeout		= tx_timeout,
	.ndo_get_stats		= get_stats,
	.ndo_set_rx_mode	= set_rx_mode,
	.ndo_do_ioctl		= netdev_ioctl,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef VLAN_SUPPORT
	.ndo_vlan_rx_add_vid	= netdev_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= netdev_vlan_rx_kill_vid,
#endif
};

static int starfire_init_one(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	struct device *d = &pdev->dev;
	struct netdev_private *np;
	int i, irq, chip_idx = ent->driver_data;
	struct net_device *dev;
	long ioaddr;
	void __iomem *base;
	int drv_flags, io_size;
	int boguscnt;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	if (pci_enable_device (pdev))
		return -EIO;

	ioaddr = pci_resource_start(pdev, 0);
	io_size = pci_resource_len(pdev, 0);
	if (!ioaddr || ((pci_resource_flags(pdev, 0) & IORESOURCE_MEM) == 0)) {
		dev_err(d, "no PCI MEM resources, aborting\n");
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(*np));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);

	irq = pdev->irq;

	if (pci_request_regions (pdev, DRV_NAME)) {
		dev_err(d, "cannot reserve PCI resources, aborting\n");
		goto err_out_free_netdev;
	}

	base = ioremap(ioaddr, io_size);
	if (!base) {
		dev_err(d, "cannot remap %#x @ %#lx, aborting\n",
			io_size, ioaddr);
		goto err_out_free_res;
	}

	pci_set_master(pdev);

	/* enable MWI -- it vastly improves Rx performance on sparc64 */
	pci_try_set_mwi(pdev);

#ifdef ZEROCOPY
	/* Starfire can do TCP/UDP checksumming */
	if (enable_hw_cksum)
		dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
#endif /* ZEROCOPY */

#ifdef VLAN_SUPPORT
	dev->features |= NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_FILTER;
#endif /* VLAN_RX_KILL_VID */
#ifdef ADDR_64BITS
	dev->features |= NETIF_F_HIGHDMA;
#endif /* ADDR_64BITS */

	/* Serial EEPROM reads are hidden by the hardware. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(base + EEPROMCtrl + 20 - i);

#if ! defined(final_version) /* Dump the EEPROM contents during development. */
	if (debug > 4)
		for (i = 0; i < 0x20; i++)
			printk("%2.2x%s",
			       (unsigned int)readb(base + EEPROMCtrl + i),
			       i % 16 != 15 ? " " : "\n");
#endif

	/* Issue soft reset */
	writel(MiiSoftReset, base + TxMode);
	udelay(1000);
	writel(0, base + TxMode);

	/* Reset the chip to erase previous misconfiguration. */
	writel(1, base + PCIDeviceConfig);
	boguscnt = 1000;
	while (--boguscnt > 0) {
		udelay(10);
		if ((readl(base + PCIDeviceConfig) & 1) == 0)
			break;
	}
	if (boguscnt == 0)
		printk("%s: chipset reset never completed!\n", dev->name);
	/* wait a little longer */
	udelay(1000);

	np = netdev_priv(dev);
	np->dev = dev;
	np->base = base;
	spin_lock_init(&np->lock);
	pci_set_drvdata(pdev, dev);

	np->pci_dev = pdev;

	np->mii_if.dev = dev;
	np->mii_if.mdio_read = mdio_read;
	np->mii_if.mdio_write = mdio_write;
	np->mii_if.phy_id_mask = 0x1f;
	np->mii_if.reg_num_mask = 0x1f;

	drv_flags = netdrv_tbl[chip_idx].drv_flags;

	np->speed100 = 1;

	/* timer resolution is 128 * 0.8us */
	np->intr_timer_ctrl = (((intr_latency * 10) / 1024) & IntrLatencyMask) |
		Timer10X | EnableIntrMasking;

	if (small_frames > 0) {
		np->intr_timer_ctrl |= SmallFrameBypass;
		switch (small_frames) {
		case 1 ... 64:
			np->intr_timer_ctrl |= SmallFrame64;
			break;
		case 65 ... 128:
			np->intr_timer_ctrl |= SmallFrame128;
			break;
		case 129 ... 256:
			np->intr_timer_ctrl |= SmallFrame256;
			break;
		default:
			np->intr_timer_ctrl |= SmallFrame512;
			if (small_frames > 512)
				printk("Adjusting small_frames down to 512\n");
			break;
		}
	}

	dev->netdev_ops = &netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->ethtool_ops = &ethtool_ops;

	netif_napi_add(dev, &np->napi, netdev_poll, max_interrupt_work);

	if (mtu)
		dev->mtu = mtu;

	if (register_netdev(dev))
		goto err_out_cleardev;

	printk(KERN_INFO "%s: %s at %p, %pM, IRQ %d.\n",
	       dev->name, netdrv_tbl[chip_idx].name, base,
	       dev->dev_addr, irq);

	if (drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		int mii_status;
		for (phy = 0; phy < 32 && phy_idx < PHY_CNT; phy++) {
			mdio_write(dev, phy, MII_BMCR, BMCR_RESET);
			mdelay(100);
			boguscnt = 1000;
			while (--boguscnt > 0)
				if ((mdio_read(dev, phy, MII_BMCR) & BMCR_RESET) == 0)
					break;
			if (boguscnt == 0) {
				printk("%s: PHY#%d reset never completed!\n", dev->name, phy);
				continue;
			}
			mii_status = mdio_read(dev, phy, MII_BMSR);
			if (mii_status != 0) {
				np->phys[phy_idx++] = phy;
				np->mii_if.advertising = mdio_read(dev, phy, MII_ADVERTISE);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "%#4.4x advertising %#4.4x.\n",
					   dev->name, phy, mii_status, np->mii_if.advertising);
				/* there can be only one PHY on-board */
				break;
			}
		}
		np->phy_cnt = phy_idx;
		if (np->phy_cnt > 0)
			np->mii_if.phy_id = np->phys[0];
		else
			memset(&np->mii_if, 0, sizeof(np->mii_if));
	}

	printk(KERN_INFO "%s: scatter-gather and hardware TCP cksumming %s.\n",
	       dev->name, enable_hw_cksum ? "enabled" : "disabled");
	return 0;

err_out_cleardev:
	iounmap(base);
err_out_free_res:
	pci_release_regions (pdev);
err_out_free_netdev:
	free_netdev(dev);
	return -ENODEV;
}


/* Read the MII Management Data I/O (MDIO) interfaces. */
static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *mdio_addr = np->base + MIICtrl + (phy_id<<7) + (location<<2);
	int result, boguscnt=1000;
	/* ??? Should we add a busy-wait here? */
	do {
		result = readl(mdio_addr);
	} while ((result & 0xC0000000) != 0x80000000 && --boguscnt > 0);
	if (boguscnt == 0)
		return 0;
	if ((result & 0xffff) == 0xffff)
		return 0;
	return result & 0xffff;
}


static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *mdio_addr = np->base + MIICtrl + (phy_id<<7) + (location<<2);
	writel(value, mdio_addr);
	/* The busy-wait will occur before a read. */
}


static int netdev_open(struct net_device *dev)
{
	const struct firmware *fw_rx, *fw_tx;
	const __be32 *fw_rx_data, *fw_tx_data;
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	const int irq = np->pci_dev->irq;
	int i, retval;
	size_t tx_size, rx_size;
	size_t tx_done_q_size, rx_done_q_size, tx_ring_size, rx_ring_size;

	/* Do we ever need to reset the chip??? */

	retval = request_irq(irq, intr_handler, IRQF_SHARED, dev->name, dev);
	if (retval)
		return retval;

	/* Disable the Rx and Tx, and reset the chip. */
	writel(0, ioaddr + GenCtrl);
	writel(1, ioaddr + PCIDeviceConfig);
	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
		       dev->name, irq);

	/* Allocate the various queues. */
	if (!np->queue_mem) {
		tx_done_q_size = ((sizeof(struct tx_done_desc) * DONE_Q_SIZE + QUEUE_ALIGN - 1) / QUEUE_ALIGN) * QUEUE_ALIGN;
		rx_done_q_size = ((sizeof(rx_done_desc) * DONE_Q_SIZE + QUEUE_ALIGN - 1) / QUEUE_ALIGN) * QUEUE_ALIGN;
		tx_ring_size = ((sizeof(starfire_tx_desc) * TX_RING_SIZE + QUEUE_ALIGN - 1) / QUEUE_ALIGN) * QUEUE_ALIGN;
		rx_ring_size = sizeof(struct starfire_rx_desc) * RX_RING_SIZE;
		np->queue_mem_size = tx_done_q_size + rx_done_q_size + tx_ring_size + rx_ring_size;
		np->queue_mem = pci_alloc_consistent(np->pci_dev, np->queue_mem_size, &np->queue_mem_dma);
		if (np->queue_mem == NULL) {
			free_irq(irq, dev);
			return -ENOMEM;
		}

		np->tx_done_q     = np->queue_mem;
		np->tx_done_q_dma = np->queue_mem_dma;
		np->rx_done_q     = (void *) np->tx_done_q + tx_done_q_size;
		np->rx_done_q_dma = np->tx_done_q_dma + tx_done_q_size;
		np->tx_ring       = (void *) np->rx_done_q + rx_done_q_size;
		np->tx_ring_dma   = np->rx_done_q_dma + rx_done_q_size;
		np->rx_ring       = (void *) np->tx_ring + tx_ring_size;
		np->rx_ring_dma   = np->tx_ring_dma + tx_ring_size;
	}

	/* Start with no carrier, it gets adjusted later */
	netif_carrier_off(dev);
	init_ring(dev);
	/* Set the size of the Rx buffers. */
	writel((np->rx_buf_sz << RxBufferLenShift) |
	       (0 << RxMinDescrThreshShift) |
	       RxPrefetchMode | RxVariableQ |
	       RX_Q_ENTRIES |
	       RX_DESC_Q_ADDR_SIZE | RX_DESC_ADDR_SIZE |
	       RxDescSpace4,
	       ioaddr + RxDescQCtrl);

	/* Set up the Rx DMA controller. */
	writel(RxChecksumIgnore |
	       (0 << RxEarlyIntThreshShift) |
	       (6 << RxHighPrioThreshShift) |
	       ((DMA_BURST_SIZE / 32) << RxBurstSizeShift),
	       ioaddr + RxDMACtrl);

	/* Set Tx descriptor */
	writel((2 << TxHiPriFIFOThreshShift) |
	       (0 << TxPadLenShift) |
	       ((DMA_BURST_SIZE / 32) << TxDMABurstSizeShift) |
	       TX_DESC_Q_ADDR_SIZE |
	       TX_DESC_SPACING | TX_DESC_TYPE,
	       ioaddr + TxDescCtrl);

	writel( (np->queue_mem_dma >> 16) >> 16, ioaddr + RxDescQHiAddr);
	writel( (np->queue_mem_dma >> 16) >> 16, ioaddr + TxRingHiAddr);
	writel( (np->queue_mem_dma >> 16) >> 16, ioaddr + CompletionHiAddr);
	writel(np->rx_ring_dma, ioaddr + RxDescQAddr);
	writel(np->tx_ring_dma, ioaddr + TxRingPtr);

	writel(np->tx_done_q_dma, ioaddr + TxCompletionAddr);
	writel(np->rx_done_q_dma |
	       RxComplType |
	       (0 << RxComplThreshShift),
	       ioaddr + RxCompletionAddr);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Filling in the station address.\n", dev->name);

	/* Fill both the Tx SA register and the Rx perfect filter. */
	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + TxStationAddr + 5 - i);
	/* The first entry is special because it bypasses the VLAN filter.
	   Don't use it. */
	writew(0, ioaddr + PerfFilterTable);
	writew(0, ioaddr + PerfFilterTable + 4);
	writew(0, ioaddr + PerfFilterTable + 8);
	for (i = 1; i < 16; i++) {
		__be16 *eaddrs = (__be16 *)dev->dev_addr;
		void __iomem *setup_frm = ioaddr + PerfFilterTable + i * 16;
		writew(be16_to_cpu(eaddrs[2]), setup_frm); setup_frm += 4;
		writew(be16_to_cpu(eaddrs[1]), setup_frm); setup_frm += 4;
		writew(be16_to_cpu(eaddrs[0]), setup_frm); setup_frm += 8;
	}

	/* Initialize other registers. */
	/* Configure the PCI bus bursts and FIFO thresholds. */
	np->tx_mode = TxFlowEnable|RxFlowEnable|PadEnable;	/* modified when link is up. */
	writel(MiiSoftReset | np->tx_mode, ioaddr + TxMode);
	udelay(1000);
	writel(np->tx_mode, ioaddr + TxMode);
	np->tx_threshold = 4;
	writel(np->tx_threshold, ioaddr + TxThreshold);

	writel(np->intr_timer_ctrl, ioaddr + IntrTimerCtrl);

	napi_enable(&np->napi);

	netif_start_queue(dev);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Setting the Rx and Tx modes.\n", dev->name);
	set_rx_mode(dev);

	np->mii_if.advertising = mdio_read(dev, np->phys[0], MII_ADVERTISE);
	check_duplex(dev);

	/* Enable GPIO interrupts on link change */
	writel(0x0f00ff00, ioaddr + GPIOCtrl);

	/* Set the interrupt mask */
	writel(IntrRxDone | IntrRxEmpty | IntrDMAErr |
	       IntrTxDMADone | IntrStatsMax | IntrLinkChange |
	       IntrRxGFPDead | IntrNoTxCsum | IntrTxBadID,
	       ioaddr + IntrEnable);
	/* Enable PCI interrupts. */
	writel(0x00800000 | readl(ioaddr + PCIDeviceConfig),
	       ioaddr + PCIDeviceConfig);

#ifdef VLAN_SUPPORT
	/* Set VLAN type to 802.1q */
	writel(ETH_P_8021Q, ioaddr + VlanType);
#endif /* VLAN_SUPPORT */

	retval = request_firmware(&fw_rx, FIRMWARE_RX, &np->pci_dev->dev);
	if (retval) {
		printk(KERN_ERR "starfire: Failed to load firmware \"%s\"\n",
		       FIRMWARE_RX);
		goto out_init;
	}
	if (fw_rx->size % 4) {
		printk(KERN_ERR "starfire: bogus length %zu in \"%s\"\n",
		       fw_rx->size, FIRMWARE_RX);
		retval = -EINVAL;
		goto out_rx;
	}
	retval = request_firmware(&fw_tx, FIRMWARE_TX, &np->pci_dev->dev);
	if (retval) {
		printk(KERN_ERR "starfire: Failed to load firmware \"%s\"\n",
		       FIRMWARE_TX);
		goto out_rx;
	}
	if (fw_tx->size % 4) {
		printk(KERN_ERR "starfire: bogus length %zu in \"%s\"\n",
		       fw_tx->size, FIRMWARE_TX);
		retval = -EINVAL;
		goto out_tx;
	}
	fw_rx_data = (const __be32 *)&fw_rx->data[0];
	fw_tx_data = (const __be32 *)&fw_tx->data[0];
	rx_size = fw_rx->size / 4;
	tx_size = fw_tx->size / 4;

	/* Load Rx/Tx firmware into the frame processors */
	for (i = 0; i < rx_size; i++)
		writel(be32_to_cpup(&fw_rx_data[i]), ioaddr + RxGfpMem + i * 4);
	for (i = 0; i < tx_size; i++)
		writel(be32_to_cpup(&fw_tx_data[i]), ioaddr + TxGfpMem + i * 4);
	if (enable_hw_cksum)
		/* Enable the Rx and Tx units, and the Rx/Tx frame processors. */
		writel(TxEnable|TxGFPEnable|RxEnable|RxGFPEnable, ioaddr + GenCtrl);
	else
		/* Enable the Rx and Tx units only. */
		writel(TxEnable|RxEnable, ioaddr + GenCtrl);

	if (debug > 1)
		printk(KERN_DEBUG "%s: Done netdev_open().\n",
		       dev->name);

out_tx:
	release_firmware(fw_tx);
out_rx:
	release_firmware(fw_rx);
out_init:
	if (retval)
		netdev_close(dev);
	return retval;
}


static void check_duplex(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	u16 reg0;
	int silly_count = 1000;

	mdio_write(dev, np->phys[0], MII_ADVERTISE, np->mii_if.advertising);
	mdio_write(dev, np->phys[0], MII_BMCR, BMCR_RESET);
	udelay(500);
	while (--silly_count && mdio_read(dev, np->phys[0], MII_BMCR) & BMCR_RESET)
		/* do nothing */;
	if (!silly_count) {
		printk("%s: MII reset failed!\n", dev->name);
		return;
	}

	reg0 = mdio_read(dev, np->phys[0], MII_BMCR);

	if (!np->mii_if.force_media) {
		reg0 |= BMCR_ANENABLE | BMCR_ANRESTART;
	} else {
		reg0 &= ~(BMCR_ANENABLE | BMCR_ANRESTART);
		if (np->speed100)
			reg0 |= BMCR_SPEED100;
		if (np->mii_if.full_duplex)
			reg0 |= BMCR_FULLDPLX;
		printk(KERN_DEBUG "%s: Link forced to %sMbit %s-duplex\n",
		       dev->name,
		       np->speed100 ? "100" : "10",
		       np->mii_if.full_duplex ? "full" : "half");
	}
	mdio_write(dev, np->phys[0], MII_BMCR, reg0);
}


static void tx_timeout(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	int old_debug;

	printk(KERN_WARNING "%s: Transmit timed out, status %#8.8x, "
	       "resetting...\n", dev->name, (int) readl(ioaddr + IntrStatus));

	/* Perhaps we should reinitialize the hardware here. */

	/*
	 * Stop and restart the interface.
	 * Cheat and increase the debug level temporarily.
	 */
	old_debug = debug;
	debug = 2;
	netdev_close(dev);
	netdev_open(dev);
	debug = old_debug;

	/* Trigger an immediate transmit demand. */

	dev->trans_start = jiffies; /* prevent tx timeout */
	dev->stats.tx_errors++;
	netif_wake_queue(dev);
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void init_ring(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	int i;

	np->cur_rx = np->cur_tx = np->reap_tx = 0;
	np->dirty_rx = np->dirty_tx = np->rx_done = np->tx_done = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = netdev_alloc_skb(dev, np->rx_buf_sz);
		np->rx_info[i].skb = skb;
		if (skb == NULL)
			break;
		np->rx_info[i].mapping = pci_map_single(np->pci_dev, skb->data, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
		if (pci_dma_mapping_error(np->pci_dev,
					  np->rx_info[i].mapping)) {
			dev_kfree_skb(skb);
			np->rx_info[i].skb = NULL;
			break;
		}
		/* Grrr, we cannot offset to correctly align the IP header. */
		np->rx_ring[i].rxaddr = cpu_to_dma(np->rx_info[i].mapping | RxDescValid);
	}
	writew(i - 1, np->base + RxDescQIdx);
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* Clear the remainder of the Rx buffer ring. */
	for (  ; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rxaddr = 0;
		np->rx_info[i].skb = NULL;
		np->rx_info[i].mapping = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[RX_RING_SIZE - 1].rxaddr |= cpu_to_dma(RxDescEndRing);

	/* Clear the completion rings. */
	for (i = 0; i < DONE_Q_SIZE; i++) {
		np->rx_done_q[i].status = 0;
		np->tx_done_q[i].status = 0;
	}

	for (i = 0; i < TX_RING_SIZE; i++)
		memset(&np->tx_info[i], 0, sizeof(np->tx_info[i]));
}


static netdev_tx_t start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	unsigned int entry;
	unsigned int prev_tx;
	u32 status;
	int i, j;

	/*
	 * be cautious here, wrapping the queue has weird semantics
	 * and we may not have enough slots even when it seems we do.
	 */
	if ((np->cur_tx - np->dirty_tx) + skb_num_frags(skb) * 2 > TX_RING_SIZE) {
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

#if defined(ZEROCOPY) && defined(HAS_BROKEN_FIRMWARE)
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb_padto(skb, (skb->len + PADDING_MASK) & ~PADDING_MASK))
			return NETDEV_TX_OK;
	}
#endif /* ZEROCOPY && HAS_BROKEN_FIRMWARE */

	prev_tx = np->cur_tx;
	entry = np->cur_tx % TX_RING_SIZE;
	for (i = 0; i < skb_num_frags(skb); i++) {
		int wrap_ring = 0;
		status = TxDescID;

		if (i == 0) {
			np->tx_info[entry].skb = skb;
			status |= TxCRCEn;
			if (entry >= TX_RING_SIZE - skb_num_frags(skb)) {
				status |= TxRingWrap;
				wrap_ring = 1;
			}
			if (np->reap_tx) {
				status |= TxDescIntr;
				np->reap_tx = 0;
			}
			if (skb->ip_summed == CHECKSUM_PARTIAL) {
				status |= TxCalTCP;
				dev->stats.tx_compressed++;
			}
			status |= skb_first_frag_len(skb) | (skb_num_frags(skb) << 16);

			np->tx_info[entry].mapping =
				pci_map_single(np->pci_dev, skb->data, skb_first_frag_len(skb), PCI_DMA_TODEVICE);
		} else {
			const skb_frag_t *this_frag = &skb_shinfo(skb)->frags[i - 1];
			status |= skb_frag_size(this_frag);
			np->tx_info[entry].mapping =
				pci_map_single(np->pci_dev,
					       skb_frag_address(this_frag),
					       skb_frag_size(this_frag),
					       PCI_DMA_TODEVICE);
		}
		if (pci_dma_mapping_error(np->pci_dev,
					  np->tx_info[entry].mapping)) {
			dev->stats.tx_dropped++;
			goto err_out;
		}

		np->tx_ring[entry].addr = cpu_to_dma(np->tx_info[entry].mapping);
		np->tx_ring[entry].status = cpu_to_le32(status);
		if (debug > 3)
			printk(KERN_DEBUG "%s: Tx #%d/#%d slot %d status %#8.8x.\n",
			       dev->name, np->cur_tx, np->dirty_tx,
			       entry, status);
		if (wrap_ring) {
			np->tx_info[entry].used_slots = TX_RING_SIZE - entry;
			np->cur_tx += np->tx_info[entry].used_slots;
			entry = 0;
		} else {
			np->tx_info[entry].used_slots = 1;
			np->cur_tx += np->tx_info[entry].used_slots;
			entry++;
		}
		/* scavenge the tx descriptors twice per TX_RING_SIZE */
		if (np->cur_tx % (TX_RING_SIZE / 2) == 0)
			np->reap_tx = 1;
	}

	/* Non-x86: explicitly flush descriptor cache lines here. */
	/* Ensure all descriptors are written back before the transmit is
	   initiated. - Jes */
	wmb();

	/* Update the producer index. */
	writel(entry * (sizeof(starfire_tx_desc) / 8), np->base + TxProducerIdx);

	/* 4 is arbitrary, but should be ok */
	if ((np->cur_tx - np->dirty_tx) + 4 > TX_RING_SIZE)
		netif_stop_queue(dev);

	return NETDEV_TX_OK;

err_out:
	entry = prev_tx % TX_RING_SIZE;
	np->tx_info[entry].skb = NULL;
	if (i > 0) {
		pci_unmap_single(np->pci_dev,
				 np->tx_info[entry].mapping,
				 skb_first_frag_len(skb),
				 PCI_DMA_TODEVICE);
		np->tx_info[entry].mapping = 0;
		entry = (entry + np->tx_info[entry].used_slots) % TX_RING_SIZE;
		for (j = 1; j < i; j++) {
			pci_unmap_single(np->pci_dev,
					 np->tx_info[entry].mapping,
					 skb_frag_size(
						&skb_shinfo(skb)->frags[j-1]),
					 PCI_DMA_TODEVICE);
			entry++;
		}
	}
	dev_kfree_skb_any(skb);
	np->cur_tx = prev_tx;
	return NETDEV_TX_OK;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t intr_handler(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	int boguscnt = max_interrupt_work;
	int consumer;
	int tx_status;
	int handled = 0;

	do {
		u32 intr_status = readl(ioaddr + IntrClear);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt status %#8.8x.\n",
			       dev->name, intr_status);

		if (intr_status == 0 || intr_status == (u32) -1)
			break;

		handled = 1;

		if (intr_status & (IntrRxDone | IntrRxEmpty)) {
			u32 enable;

			if (likely(napi_schedule_prep(&np->napi))) {
				__napi_schedule(&np->napi);
				enable = readl(ioaddr + IntrEnable);
				enable &= ~(IntrRxDone | IntrRxEmpty);
				writel(enable, ioaddr + IntrEnable);
				/* flush PCI posting buffers */
				readl(ioaddr + IntrEnable);
			} else {
				/* Paranoia check */
				enable = readl(ioaddr + IntrEnable);
				if (enable & (IntrRxDone | IntrRxEmpty)) {
					printk(KERN_INFO
					       "%s: interrupt while in poll!\n",
					       dev->name);
					enable &= ~(IntrRxDone | IntrRxEmpty);
					writel(enable, ioaddr + IntrEnable);
				}
			}
		}

		/* Scavenge the skbuff list based on the Tx-done queue.
		   There are redundant checks here that may be cleaned up
		   after the driver has proven to be reliable. */
		consumer = readl(ioaddr + TxConsumerIdx);
		if (debug > 3)
			printk(KERN_DEBUG "%s: Tx Consumer index is %d.\n",
			       dev->name, consumer);

		while ((tx_status = le32_to_cpu(np->tx_done_q[np->tx_done].status)) != 0) {
			if (debug > 3)
				printk(KERN_DEBUG "%s: Tx completion #%d entry %d is %#8.8x.\n",
				       dev->name, np->dirty_tx, np->tx_done, tx_status);
			if ((tx_status & 0xe0000000) == 0xa0000000) {
				dev->stats.tx_packets++;
			} else if ((tx_status & 0xe0000000) == 0x80000000) {
				u16 entry = (tx_status & 0x7fff) / sizeof(starfire_tx_desc);
				struct sk_buff *skb = np->tx_info[entry].skb;
				np->tx_info[entry].skb = NULL;
				pci_unmap_single(np->pci_dev,
						 np->tx_info[entry].mapping,
						 skb_first_frag_len(skb),
						 PCI_DMA_TODEVICE);
				np->tx_info[entry].mapping = 0;
				np->dirty_tx += np->tx_info[entry].used_slots;
				entry = (entry + np->tx_info[entry].used_slots) % TX_RING_SIZE;
				{
					int i;
					for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
						pci_unmap_single(np->pci_dev,
								 np->tx_info[entry].mapping,
								 skb_frag_size(&skb_shinfo(skb)->frags[i]),
								 PCI_DMA_TODEVICE);
						np->dirty_tx++;
						entry++;
					}
				}

				dev_kfree_skb_irq(skb);
			}
			np->tx_done_q[np->tx_done].status = 0;
			np->tx_done = (np->tx_done + 1) % DONE_Q_SIZE;
		}
		writew(np->tx_done, ioaddr + CompletionQConsumerIdx + 2);

		if (netif_queue_stopped(dev) &&
		    (np->cur_tx - np->dirty_tx + 4 < TX_RING_SIZE)) {
			/* The ring is no longer full, wake the queue. */
			netif_wake_queue(dev);
		}

		/* Stats overflow */
		if (intr_status & IntrStatsMax)
			get_stats(dev);

		/* Media change interrupt. */
		if (intr_status & IntrLinkChange)
			netdev_media_change(dev);

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & IntrAbnormalSummary)
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			if (debug > 1)
				printk(KERN_WARNING "%s: Too much work at interrupt, "
				       "status=%#8.8x.\n",
				       dev->name, intr_status);
			break;
		}
	} while (1);

	if (debug > 4)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#8.8x.\n",
		       dev->name, (int) readl(ioaddr + IntrStatus));
	return IRQ_RETVAL(handled);
}


/*
 * This routine is logically part of the interrupt/poll handler, but separated
 * for clarity and better register allocation.
 */
static int __netdev_rx(struct net_device *dev, int *quota)
{
	struct netdev_private *np = netdev_priv(dev);
	u32 desc_status;
	int retcode = 0;

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while ((desc_status = le32_to_cpu(np->rx_done_q[np->rx_done].status)) != 0) {
		struct sk_buff *skb;
		u16 pkt_len;
		int entry;
		rx_done_desc *desc = &np->rx_done_q[np->rx_done];

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status of %d was %#8.8x.\n", np->rx_done, desc_status);
		if (!(desc_status & RxOK)) {
			/* There was an error. */
			if (debug > 2)
				printk(KERN_DEBUG "  netdev_rx() Rx error was %#8.8x.\n", desc_status);
			dev->stats.rx_errors++;
			if (desc_status & RxFIFOErr)
				dev->stats.rx_fifo_errors++;
			goto next_rx;
		}

		if (*quota <= 0) {	/* out of rx quota */
			retcode = 1;
			goto out;
		}
		(*quota)--;

		pkt_len = desc_status;	/* Implicitly Truncate */
		entry = (desc_status >> 16) & 0x7ff;

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() normal Rx pkt length %d, quota %d.\n", pkt_len, *quota);
		/* Check if the packet is long enough to accept without copying
		   to a minimally-sized skbuff. */
		if (pkt_len < rx_copybreak &&
		    (skb = netdev_alloc_skb(dev, pkt_len + 2)) != NULL) {
			skb_reserve(skb, 2);	/* 16 byte align the IP header */
			pci_dma_sync_single_for_cpu(np->pci_dev,
						    np->rx_info[entry].mapping,
						    pkt_len, PCI_DMA_FROMDEVICE);
			skb_copy_to_linear_data(skb, np->rx_info[entry].skb->data, pkt_len);
			pci_dma_sync_single_for_device(np->pci_dev,
						       np->rx_info[entry].mapping,
						       pkt_len, PCI_DMA_FROMDEVICE);
			skb_put(skb, pkt_len);
		} else {
			pci_unmap_single(np->pci_dev, np->rx_info[entry].mapping, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			skb = np->rx_info[entry].skb;
			skb_put(skb, pkt_len);
			np->rx_info[entry].skb = NULL;
			np->rx_info[entry].mapping = 0;
		}
#ifndef final_version			/* Remove after testing. */
		/* You will want this info for the initial debug. */
		if (debug > 5) {
			printk(KERN_DEBUG "  Rx data %pM %pM %2.2x%2.2x.\n",
			       skb->data, skb->data + 6,
			       skb->data[12], skb->data[13]);
		}
#endif

		skb->protocol = eth_type_trans(skb, dev);
#ifdef VLAN_SUPPORT
		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status2 of %d was %#4.4x.\n", np->rx_done, le16_to_cpu(desc->status2));
#endif
		if (le16_to_cpu(desc->status2) & 0x0100) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			dev->stats.rx_compressed++;
		}
		/*
		 * This feature doesn't seem to be working, at least
		 * with the two firmware versions I have. If the GFP sees
		 * an IP fragment, it either ignores it completely, or reports
		 * "bad checksum" on it.
		 *
		 * Maybe I missed something -- corrections are welcome.
		 * Until then, the printk stays. :-) -Ion
		 */
		else if (le16_to_cpu(desc->status2) & 0x0040) {
			skb->ip_summed = CHECKSUM_COMPLETE;
			skb->csum = le16_to_cpu(desc->csum);
			printk(KERN_DEBUG "%s: checksum_hw, status2 = %#x\n", dev->name, le16_to_cpu(desc->status2));
		}
#ifdef VLAN_SUPPORT
		if (le16_to_cpu(desc->status2) & 0x0200) {
			u16 vlid = le16_to_cpu(desc->vlanid);

			if (debug > 4) {
				printk(KERN_DEBUG "  netdev_rx() vlanid = %d\n",
				       vlid);
			}
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlid);
		}
#endif /* VLAN_SUPPORT */
		netif_receive_skb(skb);
		dev->stats.rx_packets++;

	next_rx:
		np->cur_rx++;
		desc->status = 0;
		np->rx_done = (np->rx_done + 1) % DONE_Q_SIZE;
	}

	if (*quota == 0) {	/* out of rx quota */
		retcode = 1;
		goto out;
	}
	writew(np->rx_done, np->base + CompletionQConsumerIdx);

 out:
	refill_rx_ring(dev);
	if (debug > 5)
		printk(KERN_DEBUG "  exiting netdev_rx(): %d, status of %d was %#8.8x.\n",
		       retcode, np->rx_done, desc_status);
	return retcode;
}

static int netdev_poll(struct napi_struct *napi, int budget)
{
	struct netdev_private *np = container_of(napi, struct netdev_private, napi);
	struct net_device *dev = np->dev;
	u32 intr_status;
	void __iomem *ioaddr = np->base;
	int quota = budget;

	do {
		writel(IntrRxDone | IntrRxEmpty, ioaddr + IntrClear);

		if (__netdev_rx(dev, &quota))
			goto out;

		intr_status = readl(ioaddr + IntrStatus);
	} while (intr_status & (IntrRxDone | IntrRxEmpty));

	napi_complete(napi);
	intr_status = readl(ioaddr + IntrEnable);
	intr_status |= IntrRxDone | IntrRxEmpty;
	writel(intr_status, ioaddr + IntrEnable);

 out:
	if (debug > 5)
		printk(KERN_DEBUG "  exiting netdev_poll(): %d.\n",
		       budget - quota);

	/* Restart Rx engine if stopped. */
	return budget - quota;
}

static void refill_rx_ring(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	struct sk_buff *skb;
	int entry = -1;

	/* Refill the Rx ring buffers. */
	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_info[entry].skb == NULL) {
			skb = netdev_alloc_skb(dev, np->rx_buf_sz);
			np->rx_info[entry].skb = skb;
			if (skb == NULL)
				break;	/* Better luck next round. */
			np->rx_info[entry].mapping =
				pci_map_single(np->pci_dev, skb->data, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(np->pci_dev,
						np->rx_info[entry].mapping)) {
				dev_kfree_skb(skb);
				np->rx_info[entry].skb = NULL;
				break;
			}
			np->rx_ring[entry].rxaddr =
				cpu_to_dma(np->rx_info[entry].mapping | RxDescValid);
		}
		if (entry == RX_RING_SIZE - 1)
			np->rx_ring[entry].rxaddr |= cpu_to_dma(RxDescEndRing);
	}
	if (entry >= 0)
		writew(entry, np->base + RxDescQIdx);
}


static void netdev_media_change(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	u16 reg0, reg1, reg4, reg5;
	u32 new_tx_mode;
	u32 new_intr_timer_ctrl;

	/* reset status first */
	mdio_read(dev, np->phys[0], MII_BMCR);
	mdio_read(dev, np->phys[0], MII_BMSR);

	reg0 = mdio_read(dev, np->phys[0], MII_BMCR);
	reg1 = mdio_read(dev, np->phys[0], MII_BMSR);

	if (reg1 & BMSR_LSTATUS) {
		/* link is up */
		if (reg0 & BMCR_ANENABLE) {
			/* autonegotiation is enabled */
			reg4 = mdio_read(dev, np->phys[0], MII_ADVERTISE);
			reg5 = mdio_read(dev, np->phys[0], MII_LPA);
			if (reg4 & ADVERTISE_100FULL && reg5 & LPA_100FULL) {
				np->speed100 = 1;
				np->mii_if.full_duplex = 1;
			} else if (reg4 & ADVERTISE_100HALF && reg5 & LPA_100HALF) {
				np->speed100 = 1;
				np->mii_if.full_duplex = 0;
			} else if (reg4 & ADVERTISE_10FULL && reg5 & LPA_10FULL) {
				np->speed100 = 0;
				np->mii_if.full_duplex = 1;
			} else {
				np->speed100 = 0;
				np->mii_if.full_duplex = 0;
			}
		} else {
			/* autonegotiation is disabled */
			if (reg0 & BMCR_SPEED100)
				np->speed100 = 1;
			else
				np->speed100 = 0;
			if (reg0 & BMCR_FULLDPLX)
				np->mii_if.full_duplex = 1;
			else
				np->mii_if.full_duplex = 0;
		}
		netif_carrier_on(dev);
		printk(KERN_DEBUG "%s: Link is up, running at %sMbit %s-duplex\n",
		       dev->name,
		       np->speed100 ? "100" : "10",
		       np->mii_if.full_duplex ? "full" : "half");

		new_tx_mode = np->tx_mode & ~FullDuplex;	/* duplex setting */
		if (np->mii_if.full_duplex)
			new_tx_mode |= FullDuplex;
		if (np->tx_mode != new_tx_mode) {
			np->tx_mode = new_tx_mode;
			writel(np->tx_mode | MiiSoftReset, ioaddr + TxMode);
			udelay(1000);
			writel(np->tx_mode, ioaddr + TxMode);
		}

		new_intr_timer_ctrl = np->intr_timer_ctrl & ~Timer10X;
		if (np->speed100)
			new_intr_timer_ctrl |= Timer10X;
		if (np->intr_timer_ctrl != new_intr_timer_ctrl) {
			np->intr_timer_ctrl = new_intr_timer_ctrl;
			writel(new_intr_timer_ctrl, ioaddr + IntrTimerCtrl);
		}
	} else {
		netif_carrier_off(dev);
		printk(KERN_DEBUG "%s: Link is down\n", dev->name);
	}
}


static void netdev_error(struct net_device *dev, int intr_status)
{
	struct netdev_private *np = netdev_priv(dev);

	/* Came close to underrunning the Tx FIFO, increase threshold. */
	if (intr_status & IntrTxDataLow) {
		if (np->tx_threshold <= PKT_BUF_SZ / 16) {
			writel(++np->tx_threshold, np->base + TxThreshold);
			printk(KERN_NOTICE "%s: PCI bus congestion, increasing Tx FIFO threshold to %d bytes\n",
			       dev->name, np->tx_threshold * 16);
		} else
			printk(KERN_WARNING "%s: PCI Tx underflow -- adapter is probably malfunctioning\n", dev->name);
	}
	if (intr_status & IntrRxGFPDead) {
		dev->stats.rx_fifo_errors++;
		dev->stats.rx_errors++;
	}
	if (intr_status & (IntrNoTxCsum | IntrDMAErr)) {
		dev->stats.tx_fifo_errors++;
		dev->stats.tx_errors++;
	}
	if ((intr_status & ~(IntrNormalMask | IntrAbnormalSummary | IntrLinkChange | IntrStatsMax | IntrTxDataLow | IntrRxGFPDead | IntrNoTxCsum | IntrPCIPad)) && debug)
		printk(KERN_ERR "%s: Something Wicked happened! %#8.8x.\n",
		       dev->name, intr_status);
}


static struct net_device_stats *get_stats(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;

	/* This adapter architecture needs no SMP locks. */
	dev->stats.tx_bytes = readl(ioaddr + 0x57010);
	dev->stats.rx_bytes = readl(ioaddr + 0x57044);
	dev->stats.tx_packets = readl(ioaddr + 0x57000);
	dev->stats.tx_aborted_errors =
		readl(ioaddr + 0x57024) + readl(ioaddr + 0x57028);
	dev->stats.tx_window_errors = readl(ioaddr + 0x57018);
	dev->stats.collisions =
		readl(ioaddr + 0x57004) + readl(ioaddr + 0x57008);

	/* The chip only need report frame silently dropped. */
	dev->stats.rx_dropped += readw(ioaddr + RxDMAStatus);
	writew(0, ioaddr + RxDMAStatus);
	dev->stats.rx_crc_errors = readl(ioaddr + 0x5703C);
	dev->stats.rx_frame_errors = readl(ioaddr + 0x57040);
	dev->stats.rx_length_errors = readl(ioaddr + 0x57058);
	dev->stats.rx_missed_errors = readl(ioaddr + 0x5707C);

	return &dev->stats;
}

#ifdef VLAN_SUPPORT
static u32 set_vlan_mode(struct netdev_private *np)
{
	u32 ret = VlanMode;
	u16 vid;
	void __iomem *filter_addr = np->base + HashTable + 8;
	int vlan_count = 0;

	for_each_set_bit(vid, np->active_vlans, VLAN_N_VID) {
		if (vlan_count == 32)
			break;
		writew(vid, filter_addr);
		filter_addr += 16;
		vlan_count++;
	}
	if (vlan_count == 32) {
		ret |= PerfectFilterVlan;
		while (vlan_count < 32) {
			writew(0, filter_addr);
			filter_addr += 16;
			vlan_count++;
		}
	}
	return ret;
}
#endif /* VLAN_SUPPORT */

static void set_rx_mode(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	u32 rx_mode = MinVLANPrio;
	struct netdev_hw_addr *ha;
	int i;

#ifdef VLAN_SUPPORT
	rx_mode |= set_vlan_mode(np);
#endif /* VLAN_SUPPORT */

	if (dev->flags & IFF_PROMISC) {	/* Set promiscuous. */
		rx_mode |= AcceptAll;
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		rx_mode |= AcceptBroadcast|AcceptAllMulticast|PerfectFilter;
	} else if (netdev_mc_count(dev) <= 14) {
		/* Use the 16 element perfect filter, skip first two entries. */
		void __iomem *filter_addr = ioaddr + PerfFilterTable + 2 * 16;
		__be16 *eaddrs;
		netdev_for_each_mc_addr(ha, dev) {
			eaddrs = (__be16 *) ha->addr;
			writew(be16_to_cpu(eaddrs[2]), filter_addr); filter_addr += 4;
			writew(be16_to_cpu(eaddrs[1]), filter_addr); filter_addr += 4;
			writew(be16_to_cpu(eaddrs[0]), filter_addr); filter_addr += 8;
		}
		eaddrs = (__be16 *)dev->dev_addr;
		i = netdev_mc_count(dev) + 2;
		while (i++ < 16) {
			writew(be16_to_cpu(eaddrs[0]), filter_addr); filter_addr += 4;
			writew(be16_to_cpu(eaddrs[1]), filter_addr); filter_addr += 4;
			writew(be16_to_cpu(eaddrs[2]), filter_addr); filter_addr += 8;
		}
		rx_mode |= AcceptBroadcast|PerfectFilter;
	} else {
		/* Must use a multicast hash table. */
		void __iomem *filter_addr;
		__be16 *eaddrs;
		__le16 mc_filter[32] __attribute__ ((aligned(sizeof(long))));	/* Multicast hash filter */

		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, dev) {
			/* The chip uses the upper 9 CRC bits
			   as index into the hash table */
			int bit_nr = ether_crc_le(ETH_ALEN, ha->addr) >> 23;
			__le32 *fptr = (__le32 *) &mc_filter[(bit_nr >> 4) & ~1];

			*fptr |= cpu_to_le32(1 << (bit_nr & 31));
		}
		/* Clear the perfect filter list, skip first two entries. */
		filter_addr = ioaddr + PerfFilterTable + 2 * 16;
		eaddrs = (__be16 *)dev->dev_addr;
		for (i = 2; i < 16; i++) {
			writew(be16_to_cpu(eaddrs[0]), filter_addr); filter_addr += 4;
			writew(be16_to_cpu(eaddrs[1]), filter_addr); filter_addr += 4;
			writew(be16_to_cpu(eaddrs[2]), filter_addr); filter_addr += 8;
		}
		for (filter_addr = ioaddr + HashTable, i = 0; i < 32; filter_addr+= 16, i++)
			writew(mc_filter[i], filter_addr);
		rx_mode |= AcceptBroadcast|PerfectFilter|HashFilter;
	}
	writel(rx_mode, ioaddr + RxFilterMode);
}

static int check_if_running(struct net_device *dev)
{
	if (!netif_running(dev))
		return -EINVAL;
	return 0;
}

static void get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct netdev_private *np = netdev_priv(dev);
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(np->pci_dev), sizeof(info->bus_info));
}

static int get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netdev_private *np = netdev_priv(dev);
	spin_lock_irq(&np->lock);
	mii_ethtool_gset(&np->mii_if, ecmd);
	spin_unlock_irq(&np->lock);
	return 0;
}

static int set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct netdev_private *np = netdev_priv(dev);
	int res;
	spin_lock_irq(&np->lock);
	res = mii_ethtool_sset(&np->mii_if, ecmd);
	spin_unlock_irq(&np->lock);
	check_duplex(dev);
	return res;
}

static int nway_reset(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	return mii_nway_restart(&np->mii_if);
}

static u32 get_link(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	return mii_link_ok(&np->mii_if);
}

static u32 get_msglevel(struct net_device *dev)
{
	return debug;
}

static void set_msglevel(struct net_device *dev, u32 val)
{
	debug = val;
}

static const struct ethtool_ops ethtool_ops = {
	.begin = check_if_running,
	.get_drvinfo = get_drvinfo,
	.get_settings = get_settings,
	.set_settings = set_settings,
	.nway_reset = nway_reset,
	.get_link = get_link,
	.get_msglevel = get_msglevel,
	.set_msglevel = set_msglevel,
};

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct netdev_private *np = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irq(&np->lock);
	rc = generic_mii_ioctl(&np->mii_if, data, cmd, NULL);
	spin_unlock_irq(&np->lock);

	if ((cmd == SIOCSMIIREG) && (data->phy_id == np->phys[0]))
		check_duplex(dev);

	return rc;
}

static int netdev_close(struct net_device *dev)
{
	struct netdev_private *np = netdev_priv(dev);
	void __iomem *ioaddr = np->base;
	int i;

	netif_stop_queue(dev);

	napi_disable(&np->napi);

	if (debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, Intr status %#8.8x.\n",
			   dev->name, (int) readl(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d, Rx %d / %d.\n",
		       dev->name, np->cur_tx, np->dirty_tx,
		       np->cur_rx, np->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	writel(0, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */
	writel(0, ioaddr + GenCtrl);
	readl(ioaddr + GenCtrl);

	if (debug > 5) {
		printk(KERN_DEBUG"  Tx ring at %#llx:\n",
		       (long long) np->tx_ring_dma);
		for (i = 0; i < 8 /* TX_RING_SIZE is huge! */; i++)
			printk(KERN_DEBUG " #%d desc. %#8.8x %#llx -> %#8.8x.\n",
			       i, le32_to_cpu(np->tx_ring[i].status),
			       (long long) dma_to_cpu(np->tx_ring[i].addr),
			       le32_to_cpu(np->tx_done_q[i].status));
		printk(KERN_DEBUG "  Rx ring at %#llx -> %p:\n",
		       (long long) np->rx_ring_dma, np->rx_done_q);
		if (np->rx_done_q)
			for (i = 0; i < 8 /* RX_RING_SIZE */; i++) {
				printk(KERN_DEBUG " #%d desc. %#llx -> %#8.8x\n",
				       i, (long long) dma_to_cpu(np->rx_ring[i].rxaddr), le32_to_cpu(np->rx_done_q[i].status));
		}
	}

	free_irq(np->pci_dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rxaddr = cpu_to_dma(0xBADF00D0); /* An invalid address. */
		if (np->rx_info[i].skb != NULL) {
			pci_unmap_single(np->pci_dev, np->rx_info[i].mapping, np->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(np->rx_info[i].skb);
		}
		np->rx_info[i].skb = NULL;
		np->rx_info[i].mapping = 0;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = np->tx_info[i].skb;
		if (skb == NULL)
			continue;
		pci_unmap_single(np->pci_dev,
				 np->tx_info[i].mapping,
				 skb_first_frag_len(skb), PCI_DMA_TODEVICE);
		np->tx_info[i].mapping = 0;
		dev_kfree_skb(skb);
		np->tx_info[i].skb = NULL;
	}

	return 0;
}

#ifdef CONFIG_PM
static int starfire_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (netif_running(dev)) {
		netif_device_detach(dev);
		netdev_close(dev);
	}

	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev,state));

	return 0;
}

static int starfire_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (netif_running(dev)) {
		netdev_open(dev);
		netif_device_attach(dev);
	}

	return 0;
}
#endif /* CONFIG_PM */


static void starfire_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct netdev_private *np = netdev_priv(dev);

	BUG_ON(!dev);

	unregister_netdev(dev);

	if (np->queue_mem)
		pci_free_consistent(pdev, np->queue_mem_size, np->queue_mem, np->queue_mem_dma);


	/* XXX: add wakeup code -- requires firmware for MagicPacket */
	pci_set_power_state(pdev, PCI_D3hot);	/* go to sleep in D3 mode */
	pci_disable_device(pdev);

	iounmap(np->base);
	pci_release_regions(pdev);

	free_netdev(dev);			/* Will also free np!! */
}


static struct pci_driver starfire_driver = {
	.name		= DRV_NAME,
	.probe		= starfire_init_one,
	.remove		= starfire_remove_one,
#ifdef CONFIG_PM
	.suspend	= starfire_suspend,
	.resume		= starfire_resume,
#endif /* CONFIG_PM */
	.id_table	= starfire_pci_tbl,
};


static int __init starfire_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);

	printk(KERN_INFO DRV_NAME ": polling (NAPI) enabled\n");
#endif

	BUILD_BUG_ON(sizeof(dma_addr_t) != sizeof(netdrv_addr_t));

	return pci_register_driver(&starfire_driver);
}


static void __exit starfire_cleanup (void)
{
	pci_unregister_driver (&starfire_driver);
}


module_init(starfire_init);
module_exit(starfire_cleanup);


/*
 * Local variables:
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */

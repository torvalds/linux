/* hamachi.c: A Packet Engines GNIC-II Gigabit Ethernet driver for Linux. */
/*
	Written 1998-2000 by Donald Becker.
	Updates 2000 by Keith Underwood.

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

	This driver is for the Packet Engines GNIC-II PCI Gigabit Ethernet
	adapter.

	Support and updates available at
	http://www.scyld.com/network/hamachi.html
	[link no longer provides useful info -jgarzik]
	or
	http://www.parl.clemson.edu/~keithu/hamachi.html

*/

#define DRV_NAME	"hamachi"
#define DRV_VERSION	"2.1"
#define DRV_RELDATE	"Sept 11, 2006"


/* A few user-configurable values. */

static int debug = 1;		/* 1 normal messages, 0 quiet .. 7 verbose.  */
#define final_version
#define hamachi_debug debug
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 40;
static int mtu;
/* Default values selected by testing on a dual processor PIII-450 */
/* These six interrupt control parameters may be set directly when loading the
 * module, or through the rx_params and tx_params variables
 */
static int max_rx_latency = 0x11;
static int max_rx_gap = 0x05;
static int min_rx_pkt = 0x18;
static int max_tx_latency = 0x00;
static int max_tx_gap = 0x00;
static int min_tx_pkt = 0x30;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   -Setting to > 1518 causes all frames to be copied
	-Setting to 0 disables copies
*/
static int rx_copybreak;

/* An override for the hardware detection of bus width.
	Set to 1 to force 32 bit PCI bus detection.  Set to 4 to force 64 bit.
	Add 2 to disable parity detection.
*/
static int force32;


/* Used to pass the media type, etc.
   These exist for driver interoperability.
   No media types are currently defined.
		- The lower 4 bits are reserved for the media type.
		- The next three bits may be set to one of the following:
			0x00000000 : Autodetect PCI bus
			0x00000010 : Force 32 bit PCI bus
			0x00000020 : Disable parity detection
			0x00000040 : Force 64 bit PCI bus
			Default is autodetect
		- The next bit can be used to force half-duplex.  This is a bad
		  idea since no known implementations implement half-duplex, and,
		  in general, half-duplex for gigabit ethernet is a bad idea.
			0x00000080 : Force half-duplex
			Default is full-duplex.
		- In the original driver, the ninth bit could be used to force
		  full-duplex.  Maintain that for compatibility
		   0x00000200 : Force full-duplex
*/
#define MAX_UNITS 8				/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
/* The Hamachi chipset supports 3 parameters each for Rx and Tx
 * interruput management.  Parameters will be loaded as specified into
 * the TxIntControl and RxIntControl registers.
 *
 * The registers are arranged as follows:
 *     23 - 16   15 -  8   7    -    0
 *    _________________________________
 *   | min_pkt | max_gap | max_latency |
 *    ---------------------------------
 *   min_pkt      : The minimum number of packets processed between
 *                  interrupts.
 *   max_gap      : The maximum inter-packet gap in units of 8.192 us
 *   max_latency  : The absolute time between interrupts in units of 8.192 us
 *
 */
static int rx_params[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int tx_params[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
	The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings, except for
	excessive memory usage */
/* Empirically it appears that the Tx ring needs to be a little bigger
   for these Gbit adapters or you get into an overrun condition really
   easily.  Also, things appear to work a bit better in back-to-back
   configurations if the Rx ring is 8 times the size of the Tx ring
*/
#define TX_RING_SIZE	64
#define RX_RING_SIZE	512
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct hamachi_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct hamachi_desc)

/*
 * Enable netdev_ioctl.  Added interrupt coalescing parameter adjustment.
 * 2/19/99 Pete Wyckoff <wyckoff@ca.sandia.gov>
 */

/* play with 64-bit addrlen; seems to be a teensy bit slower  --pw */
/* #define ADDRLEN 64 */

/*
 * RX_CHECKSUM turns on card-generated receive checksum generation for
 *   TCP and UDP packets.  Otherwise the upper layers do the calculation.
 * TX_CHECKSUM won't do anything too useful, even if it works.  There's no
 *   easy mechanism by which to tell the TCP/UDP stack that it need not
 *   generate checksums for this device.  But if somebody can find a way
 *   to get that to work, most of the card work is in here already.
 * 3/10/1999 Pete Wyckoff <wyckoff@ca.sandia.gov>
 */
#undef  TX_CHECKSUM
#define RX_CHECKSUM

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (5*HZ)

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/unaligned.h>
#include <asm/cache.h>

static char version[] __devinitdata =
KERN_INFO DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE "  Written by Donald Becker\n"
KERN_INFO "   Some modifications by Eric kasten <kasten@nscl.msu.edu>\n"
KERN_INFO "   Further modifications by Keith Underwood <keithu@parl.clemson.edu>\n";


/* IP_MF appears to be only defined in <netinet/ip.h>, however,
   we need it for hardware checksumming support.  FYI... some of
   the definitions in <netinet/ip.h> conflict/duplicate those in
   other linux headers causing many compiler warnings.
*/
#ifndef IP_MF
  #define IP_MF 0x2000   /* IP more frags from <netinet/ip.h> */
#endif

/* Define IP_OFFSET to be IPOPT_OFFSET */
#ifndef IP_OFFSET
  #ifdef IPOPT_OFFSET
    #define IP_OFFSET IPOPT_OFFSET
  #else
    #define IP_OFFSET 2
  #endif
#endif

#define RUN_AT(x) (jiffies + (x))

#ifndef ADDRLEN
#define ADDRLEN 32
#endif

/* Condensed bus+endian portability operations. */
#if ADDRLEN == 64
#define cpu_to_leXX(addr)	cpu_to_le64(addr)
#else
#define cpu_to_leXX(addr)	cpu_to_le32(addr)
#endif


/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the Packet Engines "Hamachi"
Gigabit Ethernet chip.  The only PCA currently supported is the GNIC-II 64-bit
66Mhz PCI card.

II. Board-specific settings

No jumpers exist on the board.  The chip supports software correction of
various motherboard wiring errors, however this driver does not support
that feature.

III. Driver operation

IIIa. Ring buffers

The Hamachi uses a typical descriptor based bus-master architecture.
The descriptor list is similar to that used by the Digital Tulip.
This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.

This driver uses a zero-copy receive and transmit scheme similar my other
network drivers.
The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the Hamachi as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack and replaced by a newly allocated skbuff.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  Gigabit cards are typically used on generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets.

IIIb/c. Transmit/Receive Structure

The Rx and Tx descriptor structure are straight-forward, with no historical
baggage that must be explained.  Unlike the awkward DBDMA structure, there
are no unused fields or option bits that had only one allowable setting.

Two details should be noted about the descriptors: The chip supports both 32
bit and 64 bit address structures, and the length field is overwritten on
the receive descriptors.  The descriptor length is set in the control word
for each channel. The development driver uses 32 bit addresses only, however
64 bit addresses may be enabled for 64 bit architectures e.g. the Alpha.

IIId. Synchronization

This driver is very similar to my other network drivers.
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'hmp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'hmp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

Thanks to Kim Stearns of Packet Engines for providing a pair of GNIC-II boards.

IVb. References

Hamachi Engineering Design Specification, 5/15/97
(Note: This version was marked "Confidential".)

IVc. Errata

None noted.

V.  Recent Changes

01/15/1999 EPK  Enlargement of the TX and RX ring sizes.  This appears
    to help avoid some stall conditions -- this needs further research.

01/15/1999 EPK  Creation of the hamachi_tx function.  This function cleans
    the Tx ring and is called from hamachi_start_xmit (this used to be
    called from hamachi_interrupt but it tends to delay execution of the
    interrupt handler and thus reduce bandwidth by reducing the latency
    between hamachi_rx()'s).  Notably, some modification has been made so
    that the cleaning loop checks only to make sure that the DescOwn bit
    isn't set in the status flag since the card is not required
    to set the entire flag to zero after processing.

01/15/1999 EPK In the hamachi_start_tx function, the Tx ring full flag is
    checked before attempting to add a buffer to the ring.  If the ring is full
    an attempt is made to free any dirty buffers and thus find space for
    the new buffer or the function returns non-zero which should case the
    scheduler to reschedule the buffer later.

01/15/1999 EPK Some adjustments were made to the chip initialization.
    End-to-end flow control should now be fully active and the interrupt
    algorithm vars have been changed.  These could probably use further tuning.

01/15/1999 EPK Added the max_{rx,tx}_latency options.  These are used to
    set the rx and tx latencies for the Hamachi interrupts. If you're having
    problems with network stalls, try setting these to higher values.
    Valid values are 0x00 through 0xff.

01/15/1999 EPK In general, the overall bandwidth has increased and
    latencies are better (sometimes by a factor of 2).  Stalls are rare at
    this point, however there still appears to be a bug somewhere between the
    hardware and driver.  TCP checksum errors under load also appear to be
    eliminated at this point.

01/18/1999 EPK Ensured that the DescEndRing bit was being set on both the
    Rx and Tx rings.  This appears to have been affecting whether a particular
    peer-to-peer connection would hang under high load.  I believe the Rx
    rings was typically getting set correctly, but the Tx ring wasn't getting
    the DescEndRing bit set during initialization. ??? Does this mean the
    hamachi card is using the DescEndRing in processing even if a particular
    slot isn't in use -- hypothetically, the card might be searching the
    entire Tx ring for slots with the DescOwn bit set and then processing
    them.  If the DescEndRing bit isn't set, then it might just wander off
    through memory until it hits a chunk of data with that bit set
    and then looping back.

02/09/1999 EPK Added Michel Mueller's TxDMA Interrupt and Tx-timeout
    problem (TxCmd and RxCmd need only to be set when idle or stopped.

02/09/1999 EPK Added code to check/reset dev->tbusy in hamachi_interrupt.
    (Michel Mueller pointed out the ``permanently busy'' potential
    problem here).

02/22/1999 EPK Added Pete Wyckoff's ioctl to control the Tx/Rx latencies.

02/23/1999 EPK Verified that the interrupt status field bits for Tx were
    incorrectly defined and corrected (as per Michel Mueller).

02/23/1999 EPK Corrected the Tx full check to check that at least 4 slots
    were available before reseting the tbusy and tx_full flags
    (as per Michel Mueller).

03/11/1999 EPK Added Pete Wyckoff's hardware checksumming support.

12/31/1999 KDU Cleaned up assorted things and added Don's code to force
32 bit.

02/20/2000 KDU Some of the control was just plain odd.  Cleaned up the
hamachi_start_xmit() and hamachi_interrupt() code.  There is still some
re-structuring I would like to do.

03/01/2000 KDU Experimenting with a WIDE range of interrupt mitigation
parameters on a dual P3-450 setup yielded the new default interrupt
mitigation parameters.  Tx should interrupt VERY infrequently due to
Eric's scheme.  Rx should be more often...

03/13/2000 KDU Added a patch to make the Rx Checksum code interact
nicely with non-linux machines.

03/13/2000 KDU Experimented with some of the configuration values:

	-It seems that enabling PCI performance commands for descriptors
	(changing RxDMACtrl and TxDMACtrl lower nibble from 5 to D) has minimal
	performance impact for any of my tests. (ttcp, netpipe, netperf)  I will
	leave them that way until I hear further feedback.

	-Increasing the PCI_LATENCY_TIMER to 130
	(2 + (burst size of 128 * (0 wait states + 1))) seems to slightly
	degrade performance.  Leaving default at 64 pending further information.

03/14/2000 KDU Further tuning:

	-adjusted boguscnt in hamachi_rx() to depend on interrupt
	mitigation parameters chosen.

	-Selected a set of interrupt parameters based on some extensive testing.
	These may change with more testing.

TO DO:

-Consider borrowing from the acenic driver code to check PCI_COMMAND for
PCI_COMMAND_INVALIDATE.  Set maximum burst size to cache line size in
that case.

-fix the reset procedure.  It doesn't quite work.
*/

/* A few values that may be tweaked. */
/* Size of each temporary Rx buffer, calculated as:
 * 1518 bytes (ethernet packet) + 2 bytes (to get 8 byte alignment for
 * the card) + 8 bytes of status info + 8 bytes for the Rx Checksum +
 * 2 more because we use skb_reserve.
 */
#define PKT_BUF_SZ		1538

/* For now, this is going to be set to the maximum size of an ethernet
 * packet.  Eventually, we may want to make it a variable that is
 * related to the MTU
 */
#define MAX_FRAME_SIZE  1518

/* The rest of these values should never change. */

static void hamachi_timer(unsigned long data);

enum capability_flags {CanHaveMII=1, };
static const struct chip_info {
	u16	vendor_id, device_id, device_id_mask, pad;
	const char *name;
	void (*media_timer)(unsigned long data);
	int flags;
} chip_tbl[] = {
	{0x1318, 0x0911, 0xffff, 0, "Hamachi GNIC-II", hamachi_timer, 0},
	{0,},
};

/* Offsets to the Hamachi registers.  Various sizes. */
enum hamachi_offsets {
	TxDMACtrl=0x00, TxCmd=0x04, TxStatus=0x06, TxPtr=0x08, TxCurPtr=0x10,
	RxDMACtrl=0x20, RxCmd=0x24, RxStatus=0x26, RxPtr=0x28, RxCurPtr=0x30,
	PCIClkMeas=0x060, MiscStatus=0x066, ChipRev=0x68, ChipReset=0x06B,
	LEDCtrl=0x06C, VirtualJumpers=0x06D, GPIO=0x6E,
	TxChecksum=0x074, RxChecksum=0x076,
	TxIntrCtrl=0x078, RxIntrCtrl=0x07C,
	InterruptEnable=0x080, InterruptClear=0x084, IntrStatus=0x088,
	EventStatus=0x08C,
	MACCnfg=0x0A0, FrameGap0=0x0A2, FrameGap1=0x0A4,
	/* See enum MII_offsets below. */
	MACCnfg2=0x0B0, RxDepth=0x0B8, FlowCtrl=0x0BC, MaxFrameSize=0x0CE,
	AddrMode=0x0D0, StationAddr=0x0D2,
	/* Gigabit AutoNegotiation. */
	ANCtrl=0x0E0, ANStatus=0x0E2, ANXchngCtrl=0x0E4, ANAdvertise=0x0E8,
	ANLinkPartnerAbility=0x0EA,
	EECmdStatus=0x0F0, EEData=0x0F1, EEAddr=0x0F2,
	FIFOcfg=0x0F8,
};

/* Offsets to the MII-mode registers. */
enum MII_offsets {
	MII_Cmd=0xA6, MII_Addr=0xA8, MII_Wr_Data=0xAA, MII_Rd_Data=0xAC,
	MII_Status=0xAE,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x01, IntrRxPCIFault=0x02, IntrRxPCIErr=0x04,
	IntrTxDone=0x100, IntrTxPCIFault=0x200, IntrTxPCIErr=0x400,
	LinkChange=0x10000, NegotiationChange=0x20000, StatsMax=0x40000, };

/* The Hamachi Rx and Tx buffer descriptors. */
struct hamachi_desc {
	u32 status_n_length;
#if ADDRLEN == 64
	u32 pad;
	u64 addr;
#else
	u32 addr;
#endif
};

/* Bits in hamachi_desc.status_n_length */
enum desc_status_bits {
	DescOwn=0x80000000, DescEndPacket=0x40000000, DescEndRing=0x20000000,
	DescIntr=0x10000000,
};

#define PRIV_ALIGN	15  			/* Required alignment mask */
#define MII_CNT		4
struct hamachi_private {
	/* Descriptor rings first for alignment.  Tx requires a second descriptor
	   for status. */
	struct hamachi_desc *rx_ring;
	struct hamachi_desc *tx_ring;
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	dma_addr_t tx_ring_dma;
	dma_addr_t rx_ring_dma;
	struct net_device_stats stats;
	struct timer_list timer;		/* Media selection timer. */
	/* Frequently used and paired value: keep adjacent for cache effect. */
	spinlock_t lock;
	int chip_id;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;			/* Based on MTU+slack. */
	unsigned int tx_full:1;			/* The Tx queue is full. */
	unsigned int duplex_lock:1;
	unsigned int default_port:4;		/* Last dev->if_port value. */
	/* MII transceiver section. */
	int mii_cnt;								/* MII device addresses. */
	struct mii_if_info mii_if;		/* MII lib hooks/info */
	unsigned char phys[MII_CNT];		/* MII device addresses, only first one used. */
	u32 rx_int_var, tx_int_var;	/* interrupt control variables */
	u32 option;							/* Hold on to a copy of the options */
	struct pci_dev *pci_dev;
	void __iomem *base;
};

MODULE_AUTHOR("Donald Becker <becker@scyld.com>, Eric Kasten <kasten@nscl.msu.edu>, Keith Underwood <keithu@parl.clemson.edu>");
MODULE_DESCRIPTION("Packet Engines 'Hamachi' GNIC-II Gigabit Ethernet driver");
MODULE_LICENSE("GPL");

module_param(max_interrupt_work, int, 0);
module_param(mtu, int, 0);
module_param(debug, int, 0);
module_param(min_rx_pkt, int, 0);
module_param(max_rx_gap, int, 0);
module_param(max_rx_latency, int, 0);
module_param(min_tx_pkt, int, 0);
module_param(max_tx_gap, int, 0);
module_param(max_tx_latency, int, 0);
module_param(rx_copybreak, int, 0);
module_param_array(rx_params, int, NULL, 0);
module_param_array(tx_params, int, NULL, 0);
module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);
module_param(force32, int, 0);
MODULE_PARM_DESC(max_interrupt_work, "GNIC-II maximum events handled per interrupt");
MODULE_PARM_DESC(mtu, "GNIC-II MTU (all boards)");
MODULE_PARM_DESC(debug, "GNIC-II debug level (0-7)");
MODULE_PARM_DESC(min_rx_pkt, "GNIC-II minimum Rx packets processed between interrupts");
MODULE_PARM_DESC(max_rx_gap, "GNIC-II maximum Rx inter-packet gap in 8.192 microsecond units");
MODULE_PARM_DESC(max_rx_latency, "GNIC-II time between Rx interrupts in 8.192 microsecond units");
MODULE_PARM_DESC(min_tx_pkt, "GNIC-II minimum Tx packets processed between interrupts");
MODULE_PARM_DESC(max_tx_gap, "GNIC-II maximum Tx inter-packet gap in 8.192 microsecond units");
MODULE_PARM_DESC(max_tx_latency, "GNIC-II time between Tx interrupts in 8.192 microsecond units");
MODULE_PARM_DESC(rx_copybreak, "GNIC-II copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(rx_params, "GNIC-II min_rx_pkt+max_rx_gap+max_rx_latency");
MODULE_PARM_DESC(tx_params, "GNIC-II min_tx_pkt+max_tx_gap+max_tx_latency");
MODULE_PARM_DESC(options, "GNIC-II Bits 0-3: media type, bits 4-6: as force32, bit 7: half duplex, bit 9 full duplex");
MODULE_PARM_DESC(full_duplex, "GNIC-II full duplex setting(s) (1)");
MODULE_PARM_DESC(force32, "GNIC-II: Bit 0: 32 bit PCI, bit 1: disable parity, bit 2: 64 bit PCI (all boards)");

static int read_eeprom(void __iomem *ioaddr, int location);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int hamachi_open(struct net_device *dev);
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void hamachi_timer(unsigned long data);
static void hamachi_tx_timeout(struct net_device *dev);
static void hamachi_init_ring(struct net_device *dev);
static int hamachi_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t hamachi_interrupt(int irq, void *dev_instance);
static int hamachi_rx(struct net_device *dev);
static inline int hamachi_tx(struct net_device *dev);
static void hamachi_error(struct net_device *dev, int intr_status);
static int hamachi_close(struct net_device *dev);
static struct net_device_stats *hamachi_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static const struct ethtool_ops ethtool_ops;
static const struct ethtool_ops ethtool_ops_no_mii;

static int __devinit hamachi_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	struct hamachi_private *hmp;
	int option, i, rx_int_var, tx_int_var, boguscnt;
	int chip_id = ent->driver_data;
	int irq;
	void __iomem *ioaddr;
	unsigned long base;
	static int card_idx;
	struct net_device *dev;
	void *ring_space;
	dma_addr_t ring_dma;
	int ret = -ENOMEM;

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	if (pci_enable_device(pdev)) {
		ret = -EIO;
		goto err_out;
	}

	base = pci_resource_start(pdev, 0);
#ifdef __alpha__				/* Really "64 bit addrs" */
	base |= (pci_resource_start(pdev, 1) << 32);
#endif

	pci_set_master(pdev);

	i = pci_request_regions(pdev, DRV_NAME);
	if (i)
		return i;

	irq = pdev->irq;
	ioaddr = ioremap(base, 0x400);
	if (!ioaddr)
		goto err_out_release;

	dev = alloc_etherdev(sizeof(struct hamachi_private));
	if (!dev)
		goto err_out_iounmap;

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

#ifdef TX_CHECKSUM
	printk("check that skbcopy in ip_queue_xmit isn't happening\n");
	dev->hard_header_len += 8;  /* for cksum tag */
#endif

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = 1 ? read_eeprom(ioaddr, 4 + i)
			: readb(ioaddr + StationAddr + i);

#if ! defined(final_version)
	if (hamachi_debug > 4)
		for (i = 0; i < 0x10; i++)
			printk("%2.2x%s",
				   read_eeprom(ioaddr, i), i % 16 != 15 ? " " : "\n");
#endif

	hmp = netdev_priv(dev);
	spin_lock_init(&hmp->lock);

	hmp->mii_if.dev = dev;
	hmp->mii_if.mdio_read = mdio_read;
	hmp->mii_if.mdio_write = mdio_write;
	hmp->mii_if.phy_id_mask = 0x1f;
	hmp->mii_if.reg_num_mask = 0x1f;

	ring_space = pci_alloc_consistent(pdev, TX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_cleardev;
	hmp->tx_ring = (struct hamachi_desc *)ring_space;
	hmp->tx_ring_dma = ring_dma;

	ring_space = pci_alloc_consistent(pdev, RX_TOTAL_SIZE, &ring_dma);
	if (!ring_space)
		goto err_out_unmap_tx;
	hmp->rx_ring = (struct hamachi_desc *)ring_space;
	hmp->rx_ring_dma = ring_dma;

	/* Check for options being passed in */
	option = card_idx < MAX_UNITS ? options[card_idx] : 0;
	if (dev->mem_start)
		option = dev->mem_start;

	/* If the bus size is misidentified, do the following. */
	force32 = force32 ? force32 :
		((option  >= 0) ? ((option & 0x00000070) >> 4) : 0 );
	if (force32)
		writeb(force32, ioaddr + VirtualJumpers);

	/* Hmmm, do we really need to reset the chip???. */
	writeb(0x01, ioaddr + ChipReset);

	/* After a reset, the clock speed measurement of the PCI bus will not
	 * be valid for a moment.  Wait for a little while until it is.  If
	 * it takes more than 10ms, forget it.
	 */
	udelay(10);
	i = readb(ioaddr + PCIClkMeas);
	for (boguscnt = 0; (!(i & 0x080)) && boguscnt < 1000; boguscnt++){
		udelay(10);
		i = readb(ioaddr + PCIClkMeas);
	}

	hmp->base = ioaddr;
	dev->base_addr = (unsigned long)ioaddr;
	dev->irq = irq;
	pci_set_drvdata(pdev, dev);

	hmp->chip_id = chip_id;
	hmp->pci_dev = pdev;

	/* The lower four bits are the media type. */
	if (option > 0) {
		hmp->option = option;
		if (option & 0x200)
			hmp->mii_if.full_duplex = 1;
		else if (option & 0x080)
			hmp->mii_if.full_duplex = 0;
		hmp->default_port = option & 15;
		if (hmp->default_port)
			hmp->mii_if.force_media = 1;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		hmp->mii_if.full_duplex = 1;

	/* lock the duplex mode if someone specified a value */
	if (hmp->mii_if.full_duplex || (option & 0x080))
		hmp->duplex_lock = 1;

	/* Set interrupt tuning parameters */
	max_rx_latency = max_rx_latency & 0x00ff;
	max_rx_gap = max_rx_gap & 0x00ff;
	min_rx_pkt = min_rx_pkt & 0x00ff;
	max_tx_latency = max_tx_latency & 0x00ff;
	max_tx_gap = max_tx_gap & 0x00ff;
	min_tx_pkt = min_tx_pkt & 0x00ff;

	rx_int_var = card_idx < MAX_UNITS ? rx_params[card_idx] : -1;
	tx_int_var = card_idx < MAX_UNITS ? tx_params[card_idx] : -1;
	hmp->rx_int_var = rx_int_var >= 0 ? rx_int_var :
		(min_rx_pkt << 16 | max_rx_gap << 8 | max_rx_latency);
	hmp->tx_int_var = tx_int_var >= 0 ? tx_int_var :
		(min_tx_pkt << 16 | max_tx_gap << 8 | max_tx_latency);


	/* The Hamachi-specific entries in the device structure. */
	dev->open = &hamachi_open;
	dev->hard_start_xmit = &hamachi_start_xmit;
	dev->stop = &hamachi_close;
	dev->get_stats = &hamachi_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &netdev_ioctl;
	if (chip_tbl[hmp->chip_id].flags & CanHaveMII)
		SET_ETHTOOL_OPS(dev, &ethtool_ops);
	else
		SET_ETHTOOL_OPS(dev, &ethtool_ops_no_mii);
	dev->tx_timeout = &hamachi_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	if (mtu)
		dev->mtu = mtu;

	i = register_netdev(dev);
	if (i) {
		ret = i;
		goto err_out_unmap_rx;
	}

	printk(KERN_INFO "%s: %s type %x at %p, ",
		   dev->name, chip_tbl[chip_id].name, readl(ioaddr + ChipRev),
		   ioaddr);
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);
	i = readb(ioaddr + PCIClkMeas);
	printk(KERN_INFO "%s:  %d-bit %d Mhz PCI bus (%d), Virtual Jumpers "
		   "%2.2x, LPA %4.4x.\n",
		   dev->name, readw(ioaddr + MiscStatus) & 1 ? 64 : 32,
		   i ? 2000/(i&0x7f) : 0, i&0x7f, (int)readb(ioaddr + VirtualJumpers),
		   readw(ioaddr + ANLinkPartnerAbility));

	if (chip_tbl[hmp->chip_id].flags & CanHaveMII) {
		int phy, phy_idx = 0;
		for (phy = 0; phy < 32 && phy_idx < MII_CNT; phy++) {
			int mii_status = mdio_read(dev, phy, MII_BMSR);
			if (mii_status != 0xffff  &&
				mii_status != 0x0000) {
				hmp->phys[phy_idx++] = phy;
				hmp->mii_if.advertising = mdio_read(dev, phy, MII_ADVERTISE);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x.\n",
					   dev->name, phy, mii_status, hmp->mii_if.advertising);
			}
		}
		hmp->mii_cnt = phy_idx;
		if (hmp->mii_cnt > 0)
			hmp->mii_if.phy_id = hmp->phys[0];
		else
			memset(&hmp->mii_if, 0, sizeof(hmp->mii_if));
	}
	/* Configure gigabit autonegotiation. */
	writew(0x0400, ioaddr + ANXchngCtrl);	/* Enable legacy links. */
	writew(0x08e0, ioaddr + ANAdvertise);	/* Set our advertise word. */
	writew(0x1000, ioaddr + ANCtrl);			/* Enable negotiation */

	card_idx++;
	return 0;

err_out_unmap_rx:
	pci_free_consistent(pdev, RX_TOTAL_SIZE, hmp->rx_ring,
		hmp->rx_ring_dma);
err_out_unmap_tx:
	pci_free_consistent(pdev, TX_TOTAL_SIZE, hmp->tx_ring,
		hmp->tx_ring_dma);
err_out_cleardev:
	free_netdev (dev);
err_out_iounmap:
	iounmap(ioaddr);
err_out_release:
	pci_release_regions(pdev);
err_out:
	return ret;
}

static int __devinit read_eeprom(void __iomem *ioaddr, int location)
{
	int bogus_cnt = 1000;

	/* We should check busy first - per docs -KDU */
	while ((readb(ioaddr + EECmdStatus) & 0x40)  && --bogus_cnt > 0);
	writew(location, ioaddr + EEAddr);
	writeb(0x02, ioaddr + EECmdStatus);
	bogus_cnt = 1000;
	while ((readb(ioaddr + EECmdStatus) & 0x40)  && --bogus_cnt > 0);
	if (hamachi_debug > 5)
		printk("   EEPROM status is %2.2x after %d ticks.\n",
			   (int)readb(ioaddr + EECmdStatus), 1000- bogus_cnt);
	return readb(ioaddr + EEData);
}

/* MII Managemen Data I/O accesses.
   These routines assume the MDIO controller is idle, and do not exit until
   the command is finished. */

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;
	int i;

	/* We should check busy first - per docs -KDU */
	for (i = 10000; i >= 0; i--)
		if ((readw(ioaddr + MII_Status) & 1) == 0)
			break;
	writew((phy_id<<8) + location, ioaddr + MII_Addr);
	writew(0x0001, ioaddr + MII_Cmd);
	for (i = 10000; i >= 0; i--)
		if ((readw(ioaddr + MII_Status) & 1) == 0)
			break;
	return readw(ioaddr + MII_Rd_Data);
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;
	int i;

	/* We should check busy first - per docs -KDU */
	for (i = 10000; i >= 0; i--)
		if ((readw(ioaddr + MII_Status) & 1) == 0)
			break;
	writew((phy_id<<8) + location, ioaddr + MII_Addr);
	writew(value, ioaddr + MII_Wr_Data);

	/* Wait for the command to finish. */
	for (i = 10000; i >= 0; i--)
		if ((readw(ioaddr + MII_Status) & 1) == 0)
			break;
	return;
}


static int hamachi_open(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;
	int i;
	u32 rx_int_var, tx_int_var;
	u16 fifo_info;

	i = request_irq(dev->irq, &hamachi_interrupt, IRQF_SHARED, dev->name, dev);
	if (i)
		return i;

	if (hamachi_debug > 1)
		printk(KERN_DEBUG "%s: hamachi_open() irq %d.\n",
			   dev->name, dev->irq);

	hamachi_init_ring(dev);

#if ADDRLEN == 64
	/* writellll anyone ? */
	writel(cpu_to_le64(hmp->rx_ring_dma), ioaddr + RxPtr);
	writel(cpu_to_le64(hmp->rx_ring_dma) >> 32, ioaddr + RxPtr + 4);
	writel(cpu_to_le64(hmp->tx_ring_dma), ioaddr + TxPtr);
	writel(cpu_to_le64(hmp->tx_ring_dma) >> 32, ioaddr + TxPtr + 4);
#else
	writel(cpu_to_le32(hmp->rx_ring_dma), ioaddr + RxPtr);
	writel(cpu_to_le32(hmp->tx_ring_dma), ioaddr + TxPtr);
#endif

	/* TODO:  It would make sense to organize this as words since the card
	 * documentation does. -KDU
	 */
	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers: with so many this eventually this will
	   converted to an offset/value list. */

	/* Configure the FIFO */
	fifo_info = (readw(ioaddr + GPIO) & 0x00C0) >> 6;
	switch (fifo_info){
		case 0 :
			/* No FIFO */
			writew(0x0000, ioaddr + FIFOcfg);
			break;
		case 1 :
			/* Configure the FIFO for 512K external, 16K used for Tx. */
			writew(0x0028, ioaddr + FIFOcfg);
			break;
		case 2 :
			/* Configure the FIFO for 1024 external, 32K used for Tx. */
			writew(0x004C, ioaddr + FIFOcfg);
			break;
		case 3 :
			/* Configure the FIFO for 2048 external, 32K used for Tx. */
			writew(0x006C, ioaddr + FIFOcfg);
			break;
		default :
			printk(KERN_WARNING "%s:  Unsupported external memory config!\n",
				dev->name);
			/* Default to no FIFO */
			writew(0x0000, ioaddr + FIFOcfg);
			break;
	}

	if (dev->if_port == 0)
		dev->if_port = hmp->default_port;


	/* Setting the Rx mode will start the Rx process. */
	/* If someone didn't choose a duplex, default to full-duplex */
	if (hmp->duplex_lock != 1)
		hmp->mii_if.full_duplex = 1;

	/* always 1, takes no more time to do it */
	writew(0x0001, ioaddr + RxChecksum);
#ifdef TX_CHECKSUM
	writew(0x0001, ioaddr + TxChecksum);
#else
	writew(0x0000, ioaddr + TxChecksum);
#endif
	writew(0x8000, ioaddr + MACCnfg); /* Soft reset the MAC */
	writew(0x215F, ioaddr + MACCnfg);
	writew(0x000C, ioaddr + FrameGap0);
	/* WHAT?!?!?  Why isn't this documented somewhere? -KDU */
	writew(0x1018, ioaddr + FrameGap1);
	/* Why do we enable receives/transmits here? -KDU */
	writew(0x0780, ioaddr + MACCnfg2); /* Upper 16 bits control LEDs. */
	/* Enable automatic generation of flow control frames, period 0xffff. */
	writel(0x0030FFFF, ioaddr + FlowCtrl);
	writew(MAX_FRAME_SIZE, ioaddr + MaxFrameSize); 	/* dev->mtu+14 ??? */

	/* Enable legacy links. */
	writew(0x0400, ioaddr + ANXchngCtrl);	/* Enable legacy links. */
	/* Initial Link LED to blinking red. */
	writeb(0x03, ioaddr + LEDCtrl);

	/* Configure interrupt mitigation.  This has a great effect on
	   performance, so systems tuning should start here!. */

	rx_int_var = hmp->rx_int_var;
	tx_int_var = hmp->tx_int_var;

	if (hamachi_debug > 1) {
		printk("max_tx_latency: %d, max_tx_gap: %d, min_tx_pkt: %d\n",
			tx_int_var & 0x00ff, (tx_int_var & 0x00ff00) >> 8,
			(tx_int_var & 0x00ff0000) >> 16);
		printk("max_rx_latency: %d, max_rx_gap: %d, min_rx_pkt: %d\n",
			rx_int_var & 0x00ff, (rx_int_var & 0x00ff00) >> 8,
			(rx_int_var & 0x00ff0000) >> 16);
		printk("rx_int_var: %x, tx_int_var: %x\n", rx_int_var, tx_int_var);
	}

	writel(tx_int_var, ioaddr + TxIntrCtrl);
	writel(rx_int_var, ioaddr + RxIntrCtrl);

	set_rx_mode(dev);

	netif_start_queue(dev);

	/* Enable interrupts by setting the interrupt mask. */
	writel(0x80878787, ioaddr + InterruptEnable);
	writew(0x0000, ioaddr + EventStatus);	/* Clear non-interrupting events */

	/* Configure and start the DMA channels. */
	/* Burst sizes are in the low three bits: size = 4<<(val&7) */
#if ADDRLEN == 64
	writew(0x005D, ioaddr + RxDMACtrl); 		/* 128 dword bursts */
	writew(0x005D, ioaddr + TxDMACtrl);
#else
	writew(0x001D, ioaddr + RxDMACtrl);
	writew(0x001D, ioaddr + TxDMACtrl);
#endif
	writew(0x0001, ioaddr + RxCmd);

	if (hamachi_debug > 2) {
		printk(KERN_DEBUG "%s: Done hamachi_open(), status: Rx %x Tx %x.\n",
			   dev->name, readw(ioaddr + RxStatus), readw(ioaddr + TxStatus));
	}
	/* Set the timer to check for link beat. */
	init_timer(&hmp->timer);
	hmp->timer.expires = RUN_AT((24*HZ)/10);			/* 2.4 sec. */
	hmp->timer.data = (unsigned long)dev;
	hmp->timer.function = &hamachi_timer;				/* timer handler */
	add_timer(&hmp->timer);

	return 0;
}

static inline int hamachi_tx(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);

	/* Update the dirty pointer until we find an entry that is
		still owned by the card */
	for (; hmp->cur_tx - hmp->dirty_tx > 0; hmp->dirty_tx++) {
		int entry = hmp->dirty_tx % TX_RING_SIZE;
		struct sk_buff *skb;

		if (hmp->tx_ring[entry].status_n_length & cpu_to_le32(DescOwn))
			break;
		/* Free the original skb. */
		skb = hmp->tx_skbuff[entry];
		if (skb != 0) {
			pci_unmap_single(hmp->pci_dev,
				hmp->tx_ring[entry].addr, skb->len,
				PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
			hmp->tx_skbuff[entry] = NULL;
		}
		hmp->tx_ring[entry].status_n_length = 0;
		if (entry >= TX_RING_SIZE-1)
			hmp->tx_ring[TX_RING_SIZE-1].status_n_length |=
				cpu_to_le32(DescEndRing);
		hmp->stats.tx_packets++;
	}

	return 0;
}

static void hamachi_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;
	int next_tick = 10*HZ;

	if (hamachi_debug > 2) {
		printk(KERN_INFO "%s: Hamachi Autonegotiation status %4.4x, LPA "
			   "%4.4x.\n", dev->name, readw(ioaddr + ANStatus),
			   readw(ioaddr + ANLinkPartnerAbility));
		printk(KERN_INFO "%s: Autonegotiation regs %4.4x %4.4x %4.4x "
		       "%4.4x %4.4x %4.4x.\n", dev->name,
		       readw(ioaddr + 0x0e0),
		       readw(ioaddr + 0x0e2),
		       readw(ioaddr + 0x0e4),
		       readw(ioaddr + 0x0e6),
		       readw(ioaddr + 0x0e8),
		       readw(ioaddr + 0x0eA));
	}
	/* We could do something here... nah. */
	hmp->timer.expires = RUN_AT(next_tick);
	add_timer(&hmp->timer);
}

static void hamachi_tx_timeout(struct net_device *dev)
{
	int i;
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;

	printk(KERN_WARNING "%s: Hamachi transmit timed out, status %8.8x,"
		   " resetting...\n", dev->name, (int)readw(ioaddr + TxStatus));

	{
		int i;
		printk(KERN_DEBUG "  Rx ring %p: ", hmp->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)hmp->rx_ring[i].status_n_length);
		printk("\n"KERN_DEBUG"  Tx ring %p: ", hmp->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %4.4x", hmp->tx_ring[i].status_n_length);
		printk("\n");
	}

	/* Reinit the hardware and make sure the Rx and Tx processes
		are up and running.
	 */
	dev->if_port = 0;
	/* The right way to do Reset. -KDU
	 *		-Clear OWN bit in all Rx/Tx descriptors
	 *		-Wait 50 uS for channels to go idle
	 *		-Turn off MAC receiver
	 *		-Issue Reset
	 */

	for (i = 0; i < RX_RING_SIZE; i++)
		hmp->rx_ring[i].status_n_length &= cpu_to_le32(~DescOwn);

	/* Presume that all packets in the Tx queue are gone if we have to
	 * re-init the hardware.
	 */
	for (i = 0; i < TX_RING_SIZE; i++){
		struct sk_buff *skb;

		if (i >= TX_RING_SIZE - 1)
			hmp->tx_ring[i].status_n_length = cpu_to_le32(
				DescEndRing |
				(hmp->tx_ring[i].status_n_length & 0x0000FFFF));
		else
			hmp->tx_ring[i].status_n_length &= 0x0000ffff;
		skb = hmp->tx_skbuff[i];
		if (skb){
			pci_unmap_single(hmp->pci_dev, hmp->tx_ring[i].addr,
				skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
			hmp->tx_skbuff[i] = NULL;
		}
	}

	udelay(60); /* Sleep 60 us just for safety sake */
	writew(0x0002, ioaddr + RxCmd); /* STOP Rx */

	writeb(0x01, ioaddr + ChipReset);  /* Reinit the hardware */

	hmp->tx_full = 0;
	hmp->cur_rx = hmp->cur_tx = 0;
	hmp->dirty_rx = hmp->dirty_tx = 0;
	/* Rx packets are also presumed lost; however, we need to make sure a
	 * ring of buffers is in tact. -KDU
	 */
	for (i = 0; i < RX_RING_SIZE; i++){
		struct sk_buff *skb = hmp->rx_skbuff[i];

		if (skb){
			pci_unmap_single(hmp->pci_dev, hmp->rx_ring[i].addr,
				hmp->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
			hmp->rx_skbuff[i] = NULL;
		}
	}
	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(hmp->rx_buf_sz);
		hmp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;         /* Mark as being used by this device. */
		skb_reserve(skb, 2); /* 16 byte align the IP header. */
                hmp->rx_ring[i].addr = cpu_to_leXX(pci_map_single(hmp->pci_dev,
			skb->data, hmp->rx_buf_sz, PCI_DMA_FROMDEVICE));
		hmp->rx_ring[i].status_n_length = cpu_to_le32(DescOwn |
			DescEndPacket | DescIntr | (hmp->rx_buf_sz - 2));
	}
	hmp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
	/* Mark the last entry as wrapping the ring. */
	hmp->rx_ring[RX_RING_SIZE-1].status_n_length |= cpu_to_le32(DescEndRing);

	/* Trigger an immediate transmit demand. */
	dev->trans_start = jiffies;
	hmp->stats.tx_errors++;

	/* Restart the chip's Tx/Rx processes . */
	writew(0x0002, ioaddr + TxCmd); /* STOP Tx */
	writew(0x0001, ioaddr + TxCmd); /* START Tx */
	writew(0x0001, ioaddr + RxCmd); /* START Rx */

	netif_wake_queue(dev);
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void hamachi_init_ring(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	int i;

	hmp->tx_full = 0;
	hmp->cur_rx = hmp->cur_tx = 0;
	hmp->dirty_rx = hmp->dirty_tx = 0;

#if 0
	/* This is wrong.  I'm not sure what the original plan was, but this
	 * is wrong.  An MTU of 1 gets you a buffer of 1536, while an MTU
	 * of 1501 gets a buffer of 1533? -KDU
	 */
	hmp->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
#endif
	/* My attempt at a reasonable correction */
	/* +26 gets the maximum ethernet encapsulation, +7 & ~7 because the
	 * card needs room to do 8 byte alignment, +2 so we can reserve
	 * the first 2 bytes, and +16 gets room for the status word from the
	 * card.  -KDU
	 */
	hmp->rx_buf_sz = (dev->mtu <= 1492 ? PKT_BUF_SZ :
		(((dev->mtu+26+7) & ~7) + 2 + 16));

	/* Initialize all Rx descriptors. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		hmp->rx_ring[i].status_n_length = 0;
		hmp->rx_skbuff[i] = NULL;
	}
	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(hmp->rx_buf_sz);
		hmp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;         /* Mark as being used by this device. */
		skb_reserve(skb, 2); /* 16 byte align the IP header. */
                hmp->rx_ring[i].addr = cpu_to_leXX(pci_map_single(hmp->pci_dev,
			skb->data, hmp->rx_buf_sz, PCI_DMA_FROMDEVICE));
		/* -2 because it doesn't REALLY have that first 2 bytes -KDU */
		hmp->rx_ring[i].status_n_length = cpu_to_le32(DescOwn |
			DescEndPacket | DescIntr | (hmp->rx_buf_sz -2));
	}
	hmp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
	hmp->rx_ring[RX_RING_SIZE-1].status_n_length |= cpu_to_le32(DescEndRing);

	for (i = 0; i < TX_RING_SIZE; i++) {
		hmp->tx_skbuff[i] = NULL;
		hmp->tx_ring[i].status_n_length = 0;
	}
	/* Mark the last entry of the ring */
	hmp->tx_ring[TX_RING_SIZE-1].status_n_length |= cpu_to_le32(DescEndRing);

	return;
}


#ifdef TX_CHECKSUM
#define csum_add(it, val) \
do { \
    it += (u16) (val); \
    if (it & 0xffff0000) { \
	it &= 0xffff; \
	++it; \
    } \
} while (0)
    /* printk("add %04x --> %04x\n", val, it); \ */

/* uh->len already network format, do not swap */
#define pseudo_csum_udp(sum,ih,uh) do { \
    sum = 0; \
    csum_add(sum, (ih)->saddr >> 16); \
    csum_add(sum, (ih)->saddr & 0xffff); \
    csum_add(sum, (ih)->daddr >> 16); \
    csum_add(sum, (ih)->daddr & 0xffff); \
    csum_add(sum, __constant_htons(IPPROTO_UDP)); \
    csum_add(sum, (uh)->len); \
} while (0)

/* swap len */
#define pseudo_csum_tcp(sum,ih,len) do { \
    sum = 0; \
    csum_add(sum, (ih)->saddr >> 16); \
    csum_add(sum, (ih)->saddr & 0xffff); \
    csum_add(sum, (ih)->daddr >> 16); \
    csum_add(sum, (ih)->daddr & 0xffff); \
    csum_add(sum, __constant_htons(IPPROTO_TCP)); \
    csum_add(sum, htons(len)); \
} while (0)
#endif

static int hamachi_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	unsigned entry;
	u16 status;

	/* Ok, now make sure that the queue has space before trying to
		add another skbuff.  if we return non-zero the scheduler
		should interpret this as a queue full and requeue the buffer
		for later.
	 */
	if (hmp->tx_full) {
		/* We should NEVER reach this point -KDU */
		printk(KERN_WARNING "%s: Hamachi transmit queue full at slot %d.\n",dev->name, hmp->cur_tx);

		/* Wake the potentially-idle transmit channel. */
		/* If we don't need to read status, DON'T -KDU */
		status=readw(hmp->base + TxStatus);
		if( !(status & 0x0001) || (status & 0x0002))
			writew(0x0001, hmp->base + TxCmd);
		return 1;
	}

	/* Caution: the write order is important here, set the field
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = hmp->cur_tx % TX_RING_SIZE;

	hmp->tx_skbuff[entry] = skb;

#ifdef TX_CHECKSUM
	{
	    /* tack on checksum tag */
	    u32 tagval = 0;
	    struct ethhdr *eh = (struct ethhdr *)skb->data;
	    if (eh->h_proto == __constant_htons(ETH_P_IP)) {
		struct iphdr *ih = (struct iphdr *)((char *)eh + ETH_HLEN);
		if (ih->protocol == IPPROTO_UDP) {
		    struct udphdr *uh
		      = (struct udphdr *)((char *)ih + ih->ihl*4);
		    u32 offset = ((unsigned char *)uh + 6) - skb->data;
		    u32 pseudo;
		    pseudo_csum_udp(pseudo, ih, uh);
		    pseudo = htons(pseudo);
		    printk("udp cksum was %04x, sending pseudo %04x\n",
		      uh->check, pseudo);
		    uh->check = 0;  /* zero out uh->check before card calc */
		    /*
		     * start at 14 (skip ethhdr), store at offset (uh->check),
		     * use pseudo value given.
		     */
		    tagval = (14 << 24) | (offset << 16) | pseudo;
		} else if (ih->protocol == IPPROTO_TCP) {
		    printk("tcp, no auto cksum\n");
		}
	    }
	    *(u32 *)skb_push(skb, 8) = tagval;
	}
#endif

        hmp->tx_ring[entry].addr = cpu_to_leXX(pci_map_single(hmp->pci_dev,
		skb->data, skb->len, PCI_DMA_TODEVICE));

	/* Hmmmm, could probably put a DescIntr on these, but the way
		the driver is currently coded makes Tx interrupts unnecessary
		since the clearing of the Tx ring is handled by the start_xmit
		routine.  This organization helps mitigate the interrupts a
		bit and probably renders the max_tx_latency param useless.

		Update: Putting a DescIntr bit on all of the descriptors and
		mitigating interrupt frequency with the tx_min_pkt parameter. -KDU
	*/
	if (entry >= TX_RING_SIZE-1)		 /* Wrap ring */
		hmp->tx_ring[entry].status_n_length = cpu_to_le32(DescOwn |
			DescEndPacket | DescEndRing | DescIntr | skb->len);
	else
		hmp->tx_ring[entry].status_n_length = cpu_to_le32(DescOwn |
			DescEndPacket | DescIntr | skb->len);
	hmp->cur_tx++;

	/* Non-x86 Todo: explicitly flush cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	/* If we don't need to read status, DON'T -KDU */
	status=readw(hmp->base + TxStatus);
	if( !(status & 0x0001) || (status & 0x0002))
		writew(0x0001, hmp->base + TxCmd);

	/* Immediately before returning, let's clear as many entries as we can. */
	hamachi_tx(dev);

	/* We should kick the bottom half here, since we are not accepting
	 * interrupts with every packet.  i.e. realize that Gigabit ethernet
	 * can transmit faster than ordinary machines can load packets;
	 * hence, any packet that got put off because we were in the transmit
	 * routine should IMMEDIATELY get a chance to be re-queued. -KDU
	 */
	if ((hmp->cur_tx - hmp->dirty_tx) < (TX_RING_SIZE - 4))
		netif_wake_queue(dev);  /* Typical path */
	else {
		hmp->tx_full = 1;
		netif_stop_queue(dev);
	}
	dev->trans_start = jiffies;

	if (hamachi_debug > 4) {
		printk(KERN_DEBUG "%s: Hamachi transmit frame #%d queued in slot %d.\n",
			   dev->name, hmp->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t hamachi_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;
	long boguscnt = max_interrupt_work;
	int handled = 0;

#ifndef final_version			/* Can never occur. */
	if (dev == NULL) {
		printk (KERN_ERR "hamachi_interrupt(): irq %d for unknown device.\n", irq);
		return IRQ_NONE;
	}
#endif

	spin_lock(&hmp->lock);

	do {
		u32 intr_status = readl(ioaddr + InterruptClear);

		if (hamachi_debug > 4)
			printk(KERN_DEBUG "%s: Hamachi interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		handled = 1;

		if (intr_status & IntrRxDone)
			hamachi_rx(dev);

		if (intr_status & IntrTxDone){
			/* This code should RARELY need to execute. After all, this is
			 * a gigabit link, it should consume packets as fast as we put
			 * them in AND we clear the Tx ring in hamachi_start_xmit().
			 */
			if (hmp->tx_full){
				for (; hmp->cur_tx - hmp->dirty_tx > 0; hmp->dirty_tx++){
					int entry = hmp->dirty_tx % TX_RING_SIZE;
					struct sk_buff *skb;

					if (hmp->tx_ring[entry].status_n_length & cpu_to_le32(DescOwn))
						break;
					skb = hmp->tx_skbuff[entry];
					/* Free the original skb. */
					if (skb){
						pci_unmap_single(hmp->pci_dev,
							hmp->tx_ring[entry].addr,
							skb->len,
							PCI_DMA_TODEVICE);
						dev_kfree_skb_irq(skb);
						hmp->tx_skbuff[entry] = NULL;
					}
					hmp->tx_ring[entry].status_n_length = 0;
					if (entry >= TX_RING_SIZE-1)
						hmp->tx_ring[TX_RING_SIZE-1].status_n_length |=
							cpu_to_le32(DescEndRing);
					hmp->stats.tx_packets++;
				}
				if (hmp->cur_tx - hmp->dirty_tx < TX_RING_SIZE - 4){
					/* The ring is no longer full */
					hmp->tx_full = 0;
					netif_wake_queue(dev);
				}
			} else {
				netif_wake_queue(dev);
			}
		}


		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status &
			(IntrTxPCIFault | IntrTxPCIErr | IntrRxPCIFault | IntrRxPCIErr |
			 LinkChange | NegotiationChange | StatsMax))
			hamachi_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	} while (1);

	if (hamachi_debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, readl(ioaddr + IntrStatus));

#ifndef final_version
	/* Code that should never be run!  Perhaps remove after testing.. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk(KERN_ERR "%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
			free_irq(irq, dev);
		}
	}
#endif

	spin_unlock(&hmp->lock);
	return IRQ_RETVAL(handled);
}

/* This routine is logically part of the interrupt handler, but separated
   for clarity and better register allocation. */
static int hamachi_rx(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	int entry = hmp->cur_rx % RX_RING_SIZE;
	int boguscnt = (hmp->dirty_rx + RX_RING_SIZE) - hmp->cur_rx;

	if (hamachi_debug > 4) {
		printk(KERN_DEBUG " In hamachi_rx(), entry %d status %4.4x.\n",
			   entry, hmp->rx_ring[entry].status_n_length);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while (1) {
		struct hamachi_desc *desc = &(hmp->rx_ring[entry]);
		u32 desc_status = le32_to_cpu(desc->status_n_length);
		u16 data_size = desc_status;	/* Implicit truncate */
		u8 *buf_addr;
		s32 frame_status;

		if (desc_status & DescOwn)
			break;
		pci_dma_sync_single_for_cpu(hmp->pci_dev,
					    desc->addr,
					    hmp->rx_buf_sz,
					    PCI_DMA_FROMDEVICE);
		buf_addr = (u8 *) hmp->rx_skbuff[entry]->data;
		frame_status = le32_to_cpu(get_unaligned((s32*)&(buf_addr[data_size - 12])));
		if (hamachi_debug > 4)
			printk(KERN_DEBUG "  hamachi_rx() status was %8.8x.\n",
				frame_status);
		if (--boguscnt < 0)
			break;
		if ( ! (desc_status & DescEndPacket)) {
			printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
				   "multiple buffers, entry %#x length %d status %4.4x!\n",
				   dev->name, hmp->cur_rx, data_size, desc_status);
			printk(KERN_WARNING "%s: Oversized Ethernet frame %p vs %p.\n",
				   dev->name, desc, &hmp->rx_ring[hmp->cur_rx % RX_RING_SIZE]);
			printk(KERN_WARNING "%s: Oversized Ethernet frame -- next status %x/%x last status %x.\n",
				   dev->name,
				   hmp->rx_ring[(hmp->cur_rx+1) % RX_RING_SIZE].status_n_length & 0xffff0000,
				   hmp->rx_ring[(hmp->cur_rx+1) % RX_RING_SIZE].status_n_length & 0x0000ffff,
				   hmp->rx_ring[(hmp->cur_rx-1) % RX_RING_SIZE].status_n_length);
			hmp->stats.rx_length_errors++;
		} /* else  Omit for prototype errata??? */
		if (frame_status & 0x00380000) {
			/* There was an error. */
			if (hamachi_debug > 2)
				printk(KERN_DEBUG "  hamachi_rx() Rx error was %8.8x.\n",
					   frame_status);
			hmp->stats.rx_errors++;
			if (frame_status & 0x00600000) hmp->stats.rx_length_errors++;
			if (frame_status & 0x00080000) hmp->stats.rx_frame_errors++;
			if (frame_status & 0x00100000) hmp->stats.rx_crc_errors++;
			if (frame_status < 0) hmp->stats.rx_dropped++;
		} else {
			struct sk_buff *skb;
			/* Omit CRC */
			u16 pkt_len = (frame_status & 0x07ff) - 4;
#ifdef RX_CHECKSUM
			u32 pfck = *(u32 *) &buf_addr[data_size - 8];
#endif


#ifndef final_version
			if (hamachi_debug > 4)
				printk(KERN_DEBUG "  hamachi_rx() normal Rx pkt length %d"
					   " of %d, bogus_cnt %d.\n",
					   pkt_len, data_size, boguscnt);
			if (hamachi_debug > 5)
				printk(KERN_DEBUG"%s:  rx status %8.8x %8.8x %8.8x %8.8x %8.8x.\n",
					   dev->name,
					   *(s32*)&(buf_addr[data_size - 20]),
					   *(s32*)&(buf_addr[data_size - 16]),
					   *(s32*)&(buf_addr[data_size - 12]),
					   *(s32*)&(buf_addr[data_size - 8]),
					   *(s32*)&(buf_addr[data_size - 4]));
#endif
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
#ifdef RX_CHECKSUM
				printk(KERN_ERR "%s: rx_copybreak non-zero "
				  "not good with RX_CHECKSUM\n", dev->name);
#endif
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
				pci_dma_sync_single_for_cpu(hmp->pci_dev,
							    hmp->rx_ring[entry].addr,
							    hmp->rx_buf_sz,
							    PCI_DMA_FROMDEVICE);
				/* Call copy + cksum if available. */
#if 1 || USE_IP_COPYSUM
				skb_copy_to_linear_data(skb,
					hmp->rx_skbuff[entry]->data, pkt_len);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), hmp->rx_ring_dma
					+ entry*sizeof(*desc), pkt_len);
#endif
				pci_dma_sync_single_for_device(hmp->pci_dev,
							       hmp->rx_ring[entry].addr,
							       hmp->rx_buf_sz,
							       PCI_DMA_FROMDEVICE);
			} else {
				pci_unmap_single(hmp->pci_dev,
						 hmp->rx_ring[entry].addr,
						 hmp->rx_buf_sz, PCI_DMA_FROMDEVICE);
				skb_put(skb = hmp->rx_skbuff[entry], pkt_len);
				hmp->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);


#ifdef RX_CHECKSUM
			/* TCP or UDP on ipv4, DIX encoding */
			if (pfck>>24 == 0x91 || pfck>>24 == 0x51) {
				struct iphdr *ih = (struct iphdr *) skb->data;
				/* Check that IP packet is at least 46 bytes, otherwise,
				 * there may be pad bytes included in the hardware checksum.
				 * This wouldn't happen if everyone padded with 0.
				 */
				if (ntohs(ih->tot_len) >= 46){
					/* don't worry about frags */
					if (!(ih->frag_off & __constant_htons(IP_MF|IP_OFFSET))) {
						u32 inv = *(u32 *) &buf_addr[data_size - 16];
						u32 *p = (u32 *) &buf_addr[data_size - 20];
						register u32 crc, p_r, p_r1;

						if (inv & 4) {
							inv &= ~4;
							--p;
						}
						p_r = *p;
						p_r1 = *(p-1);
						switch (inv) {
							case 0:
								crc = (p_r & 0xffff) + (p_r >> 16);
								break;
							case 1:
								crc = (p_r >> 16) + (p_r & 0xffff)
									+ (p_r1 >> 16 & 0xff00);
								break;
							case 2:
								crc = p_r + (p_r1 >> 16);
								break;
							case 3:
								crc = p_r + (p_r1 & 0xff00) + (p_r1 >> 16);
								break;
							default:	/*NOTREACHED*/ crc = 0;
						}
						if (crc & 0xffff0000) {
							crc &= 0xffff;
							++crc;
						}
						/* tcp/udp will add in pseudo */
						skb->csum = ntohs(pfck & 0xffff);
						if (skb->csum > crc)
							skb->csum -= crc;
						else
							skb->csum += (~crc & 0xffff);
						/*
						* could do the pseudo myself and return
						* CHECKSUM_UNNECESSARY
						*/
						skb->ip_summed = CHECKSUM_COMPLETE;
					}
				}
			}
#endif  /* RX_CHECKSUM */

			netif_rx(skb);
			dev->last_rx = jiffies;
			hmp->stats.rx_packets++;
		}
		entry = (++hmp->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; hmp->cur_rx - hmp->dirty_rx > 0; hmp->dirty_rx++) {
		struct hamachi_desc *desc;

		entry = hmp->dirty_rx % RX_RING_SIZE;
		desc = &(hmp->rx_ring[entry]);
		if (hmp->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb = dev_alloc_skb(hmp->rx_buf_sz);

			hmp->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;		/* Better luck next round. */
			skb->dev = dev;		/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
                	desc->addr = cpu_to_leXX(pci_map_single(hmp->pci_dev,
				skb->data, hmp->rx_buf_sz, PCI_DMA_FROMDEVICE));
		}
		desc->status_n_length = cpu_to_le32(hmp->rx_buf_sz);
		if (entry >= RX_RING_SIZE-1)
			desc->status_n_length |= cpu_to_le32(DescOwn |
				DescEndPacket | DescEndRing | DescIntr);
		else
			desc->status_n_length |= cpu_to_le32(DescOwn |
				DescEndPacket | DescIntr);
	}

	/* Restart Rx engine if stopped. */
	/* If we don't need to check status, don't. -KDU */
	if (readw(hmp->base + RxStatus) & 0x0002)
		writew(0x0001, hmp->base + RxCmd);

	return 0;
}

/* This is more properly named "uncommon interrupt events", as it covers more
   than just errors. */
static void hamachi_error(struct net_device *dev, int intr_status)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;

	if (intr_status & (LinkChange|NegotiationChange)) {
		if (hamachi_debug > 1)
			printk(KERN_INFO "%s: Link changed: AutoNegotiation Ctrl"
				   " %4.4x, Status %4.4x %4.4x Intr status %4.4x.\n",
				   dev->name, readw(ioaddr + 0x0E0), readw(ioaddr + 0x0E2),
				   readw(ioaddr + ANLinkPartnerAbility),
				   readl(ioaddr + IntrStatus));
		if (readw(ioaddr + ANStatus) & 0x20)
			writeb(0x01, ioaddr + LEDCtrl);
		else
			writeb(0x03, ioaddr + LEDCtrl);
	}
	if (intr_status & StatsMax) {
		hamachi_get_stats(dev);
		/* Read the overflow bits to clear. */
		readl(ioaddr + 0x370);
		readl(ioaddr + 0x3F0);
	}
	if ((intr_status & ~(LinkChange|StatsMax|NegotiationChange|IntrRxDone|IntrTxDone))
		&& hamachi_debug)
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
	/* Hmmmmm, it's not clear how to recover from PCI faults. */
	if (intr_status & (IntrTxPCIErr | IntrTxPCIFault))
		hmp->stats.tx_fifo_errors++;
	if (intr_status & (IntrRxPCIErr | IntrRxPCIFault))
		hmp->stats.rx_fifo_errors++;
}

static int hamachi_close(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;
	struct sk_buff *skb;
	int i;

	netif_stop_queue(dev);

	if (hamachi_debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was Tx %4.4x Rx %4.4x Int %2.2x.\n",
			   dev->name, readw(ioaddr + TxStatus),
			   readw(ioaddr + RxStatus), readl(ioaddr + IntrStatus));
		printk(KERN_DEBUG "%s: Queue pointers were Tx %d / %d,  Rx %d / %d.\n",
			   dev->name, hmp->cur_tx, hmp->dirty_tx, hmp->cur_rx, hmp->dirty_rx);
	}

	/* Disable interrupts by clearing the interrupt mask. */
	writel(0x0000, ioaddr + InterruptEnable);

	/* Stop the chip's Tx and Rx processes. */
	writel(2, ioaddr + RxCmd);
	writew(2, ioaddr + TxCmd);

#ifdef __i386__
	if (hamachi_debug > 2) {
		printk("\n"KERN_DEBUG"  Tx ring at %8.8x:\n",
			   (int)hmp->tx_ring_dma);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %c #%d desc. %8.8x %8.8x.\n",
				   readl(ioaddr + TxCurPtr) == (long)&hmp->tx_ring[i] ? '>' : ' ',
				   i, hmp->tx_ring[i].status_n_length, hmp->tx_ring[i].addr);
		printk("\n"KERN_DEBUG "  Rx ring %8.8x:\n",
			   (int)hmp->rx_ring_dma);
		for (i = 0; i < RX_RING_SIZE; i++) {
			printk(KERN_DEBUG " %c #%d desc. %4.4x %8.8x\n",
				   readl(ioaddr + RxCurPtr) == (long)&hmp->rx_ring[i] ? '>' : ' ',
				   i, hmp->rx_ring[i].status_n_length, hmp->rx_ring[i].addr);
			if (hamachi_debug > 6) {
				if (*(u8*)hmp->rx_skbuff[i]->data != 0x69) {
					u16 *addr = (u16 *)
						hmp->rx_skbuff[i]->data;
					int j;

					for (j = 0; j < 0x50; j++)
						printk(" %4.4x", addr[j]);
					printk("\n");
				}
			}
		}
	}
#endif /* __i386__ debugging only */

	free_irq(dev->irq, dev);

	del_timer_sync(&hmp->timer);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = hmp->rx_skbuff[i];
		hmp->rx_ring[i].status_n_length = 0;
		hmp->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (skb) {
			pci_unmap_single(hmp->pci_dev,
				hmp->rx_ring[i].addr, hmp->rx_buf_sz,
				PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
			hmp->rx_skbuff[i] = NULL;
		}
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		skb = hmp->tx_skbuff[i];
		if (skb) {
			pci_unmap_single(hmp->pci_dev,
				hmp->tx_ring[i].addr, skb->len,
				PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
			hmp->tx_skbuff[i] = NULL;
		}
	}

	writeb(0x00, ioaddr + LEDCtrl);

	return 0;
}

static struct net_device_stats *hamachi_get_stats(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;

	/* We should lock this segment of code for SMP eventually, although
	   the vulnerability window is very small and statistics are
	   non-critical. */
        /* Ok, what goes here?  This appears to be stuck at 21 packets
           according to ifconfig.  It does get incremented in hamachi_tx(),
           so I think I'll comment it out here and see if better things
           happen.
        */
	/* hmp->stats.tx_packets	= readl(ioaddr + 0x000); */

	hmp->stats.rx_bytes = readl(ioaddr + 0x330); /* Total Uni+Brd+Multi */
	hmp->stats.tx_bytes = readl(ioaddr + 0x3B0); /* Total Uni+Brd+Multi */
	hmp->stats.multicast		= readl(ioaddr + 0x320); /* Multicast Rx */

	hmp->stats.rx_length_errors	= readl(ioaddr + 0x368); /* Over+Undersized */
	hmp->stats.rx_over_errors	= readl(ioaddr + 0x35C); /* Jabber */
	hmp->stats.rx_crc_errors	= readl(ioaddr + 0x360); /* Jabber */
	hmp->stats.rx_frame_errors	= readl(ioaddr + 0x364); /* Symbol Errs */
	hmp->stats.rx_missed_errors	= readl(ioaddr + 0x36C); /* Dropped */

	return &hmp->stats;
}

static void set_rx_mode(struct net_device *dev)
{
	struct hamachi_private *hmp = netdev_priv(dev);
	void __iomem *ioaddr = hmp->base;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		writew(0x000F, ioaddr + AddrMode);
	} else if ((dev->mc_count > 63)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		writew(0x000B, ioaddr + AddrMode);
	} else if (dev->mc_count > 0) { /* Must use the CAM filter. */
		struct dev_mc_list *mclist;
		int i;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			writel(*(u32*)(mclist->dmi_addr), ioaddr + 0x100 + i*8);
			writel(0x20000 | (*(u16*)&mclist->dmi_addr[4]),
				   ioaddr + 0x104 + i*8);
		}
		/* Clear remaining entries. */
		for (; i < 64; i++)
			writel(0, ioaddr + 0x104 + i*8);
		writew(0x0003, ioaddr + AddrMode);
	} else {					/* Normal, unicast/broadcast-only mode. */
		writew(0x0001, ioaddr + AddrMode);
	}
}

static int check_if_running(struct net_device *dev)
{
	if (!netif_running(dev))
		return -EINVAL;
	return 0;
}

static void hamachi_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct hamachi_private *np = netdev_priv(dev);
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, pci_name(np->pci_dev));
}

static int hamachi_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct hamachi_private *np = netdev_priv(dev);
	spin_lock_irq(&np->lock);
	mii_ethtool_gset(&np->mii_if, ecmd);
	spin_unlock_irq(&np->lock);
	return 0;
}

static int hamachi_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct hamachi_private *np = netdev_priv(dev);
	int res;
	spin_lock_irq(&np->lock);
	res = mii_ethtool_sset(&np->mii_if, ecmd);
	spin_unlock_irq(&np->lock);
	return res;
}

static int hamachi_nway_reset(struct net_device *dev)
{
	struct hamachi_private *np = netdev_priv(dev);
	return mii_nway_restart(&np->mii_if);
}

static u32 hamachi_get_link(struct net_device *dev)
{
	struct hamachi_private *np = netdev_priv(dev);
	return mii_link_ok(&np->mii_if);
}

static const struct ethtool_ops ethtool_ops = {
	.begin = check_if_running,
	.get_drvinfo = hamachi_get_drvinfo,
	.get_settings = hamachi_get_settings,
	.set_settings = hamachi_set_settings,
	.nway_reset = hamachi_nway_reset,
	.get_link = hamachi_get_link,
};

static const struct ethtool_ops ethtool_ops_no_mii = {
	.begin = check_if_running,
	.get_drvinfo = hamachi_get_drvinfo,
};

static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct hamachi_private *np = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	if (cmd == (SIOCDEVPRIVATE+3)) { /* set rx,tx intr params */
		u32 *d = (u32 *)&rq->ifr_ifru;
		/* Should add this check here or an ordinary user can do nasty
		 * things. -KDU
		 *
		 * TODO: Shut down the Rx and Tx engines while doing this.
		 */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		writel(d[0], np->base + TxIntrCtrl);
		writel(d[1], np->base + RxIntrCtrl);
		printk(KERN_NOTICE "%s: tx %08x, rx %08x intr\n", dev->name,
		  (u32) readl(np->base + TxIntrCtrl),
		  (u32) readl(np->base + RxIntrCtrl));
		rc = 0;
	}

	else {
		spin_lock_irq(&np->lock);
		rc = generic_mii_ioctl(&np->mii_if, data, cmd, NULL);
		spin_unlock_irq(&np->lock);
	}

	return rc;
}


static void __devexit hamachi_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct hamachi_private *hmp = netdev_priv(dev);

		pci_free_consistent(pdev, RX_TOTAL_SIZE, hmp->rx_ring,
			hmp->rx_ring_dma);
		pci_free_consistent(pdev, TX_TOTAL_SIZE, hmp->tx_ring,
			hmp->tx_ring_dma);
		unregister_netdev(dev);
		iounmap(hmp->base);
		free_netdev(dev);
		pci_release_regions(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

static struct pci_device_id hamachi_pci_tbl[] = {
	{ 0x1318, 0x0911, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hamachi_pci_tbl);

static struct pci_driver hamachi_driver = {
	.name		= DRV_NAME,
	.id_table	= hamachi_pci_tbl,
	.probe		= hamachi_init_one,
	.remove		= __devexit_p(hamachi_remove_one),
};

static int __init hamachi_init (void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif
	return pci_register_driver(&hamachi_driver);
}

static void __exit hamachi_exit (void)
{
	pci_unregister_driver(&hamachi_driver);
}


module_init(hamachi_init);
module_exit(hamachi_exit);

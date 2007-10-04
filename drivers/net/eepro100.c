/* drivers/net/eepro100.c: An Intel i82557-559 Ethernet driver for Linux. */
/*
	Written 1996-1999 by Donald Becker.

	The driver also contains updates by different kernel developers
	(see incomplete list below).
	Current maintainer is Andrey V. Savochkin <saw@saw.sw.com.sg>.
	Please use this email address and linux-kernel mailing list for bug reports.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	This driver is for the Intel EtherExpress Pro100 (Speedo3) design.
	It should work with all i82557/558/559 boards.

	Version history:
	1998 Apr - 2000 Feb  Andrey V. Savochkin <saw@saw.sw.com.sg>
		Serious fixes for multicast filter list setting, TX timeout routine;
		RX ring refilling logic;  other stuff
	2000 Feb  Jeff Garzik <jgarzik@pobox.com>
		Convert to new PCI driver interface
	2000 Mar 24  Dragan Stancevic <visitor@valinux.com>
		Disabled FC and ER, to avoid lockups when when we get FCP interrupts.
	2000 Jul 17 Goutham Rao <goutham.rao@intel.com>
		PCI DMA API fixes, adding pci_dma_sync_single calls where neccesary
	2000 Aug 31 David Mosberger <davidm@hpl.hp.com>
		rx_align support: enables rx DMA without causing unaligned accesses.
*/

static const char * const version =
"eepro100.c:v1.09j-t 9/29/99 Donald Becker\n"
"eepro100.c: $Revision: 1.36 $ 2000/11/17 Modified by Andrey V. Savochkin <saw@saw.sw.com.sg> and others\n";

/* A few user-configurable values that apply to all boards.
   First set is undocumented and spelled per Intel recommendations. */

static int congenb /* = 0 */; /* Enable congestion control in the DP83840. */
static int txfifo = 8;		/* Tx FIFO threshold in 4 byte units, 0-15 */
static int rxfifo = 8;		/* Rx FIFO threshold, default 32 bytes. */
/* Tx/Rx DMA burst length, 0-127, 0 == no preemption, tx==128 -> disabled. */
static int txdmacount = 128;
static int rxdmacount /* = 0 */;

#if defined(__ia64__) || defined(__alpha__) || defined(__sparc__) || defined(__mips__) || \
	defined(__arm__)
  /* align rx buffers to 2 bytes so that IP header is aligned */
# define rx_align(skb)		skb_reserve((skb), 2)
# define RxFD_ALIGNMENT		__attribute__ ((aligned (2), packed))
#else
# define rx_align(skb)
# define RxFD_ALIGNMENT
#endif

/* Set the copy breakpoint for the copy-only-tiny-buffer Rx method.
   Lower values use more memory, but are faster. */
static int rx_copybreak = 200;

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast) */
static int multicast_filter_limit = 64;

/* 'options' is used to pass a transceiver override or full-duplex flag
   e.g. "options=16" for FD, "options=32" for 100mbps-only. */
static int full_duplex[] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int options[] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* A few values that may be tweaked. */
/* The ring sizes should be a power of two for efficiency. */
#define TX_RING_SIZE	64
#define RX_RING_SIZE	64
/* How much slots multicast filter setup may take.
   Do not descrease without changing set_rx_mode() implementaion. */
#define TX_MULTICAST_SIZE   2
#define TX_MULTICAST_RESERV (TX_MULTICAST_SIZE*2)
/* Actual number of TX packets queued, must be
   <= TX_RING_SIZE-TX_MULTICAST_RESERV. */
#define TX_QUEUE_LIMIT  (TX_RING_SIZE-TX_MULTICAST_RESERV)
/* Hysteresis marking queue as no longer full. */
#define TX_QUEUE_UNFULL (TX_QUEUE_LIMIT-4)

/* Operational parameters that usually are not changed. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT		(2*HZ)
/* Size of an pre-allocated Rx buffer: <Ethernet MTU> + slack.*/
#define PKT_BUF_SZ		1536

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>

static int use_io;
static int debug = -1;
#define DEBUG_DEFAULT		(NETIF_MSG_DRV		| \
				 NETIF_MSG_HW		| \
				 NETIF_MSG_RX_ERR	| \
				 NETIF_MSG_TX_ERR)
#define DEBUG			((debug >= 0) ? (1<<debug)-1 : DEBUG_DEFAULT)


MODULE_AUTHOR("Maintainer: Andrey V. Savochkin <saw@saw.sw.com.sg>");
MODULE_DESCRIPTION("Intel i82557/i82558/i82559 PCI EtherExpressPro driver");
MODULE_LICENSE("GPL");
module_param(use_io, int, 0);
module_param(debug, int, 0);
module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);
module_param(congenb, int, 0);
module_param(txfifo, int, 0);
module_param(rxfifo, int, 0);
module_param(txdmacount, int, 0);
module_param(rxdmacount, int, 0);
module_param(rx_copybreak, int, 0);
module_param(max_interrupt_work, int, 0);
module_param(multicast_filter_limit, int, 0);
MODULE_PARM_DESC(debug, "debug level (0-6)");
MODULE_PARM_DESC(options, "Bits 0-3: transceiver type, bit 4: full duplex, bit 5: 100Mbps");
MODULE_PARM_DESC(full_duplex, "full duplex setting(s) (1)");
MODULE_PARM_DESC(congenb, "Enable congestion control (1)");
MODULE_PARM_DESC(txfifo, "Tx FIFO threshold in 4 byte units, (0-15)");
MODULE_PARM_DESC(rxfifo, "Rx FIFO threshold in 4 byte units, (0-15)");
MODULE_PARM_DESC(txdmacount, "Tx DMA burst length; 128 - disable (0-128)");
MODULE_PARM_DESC(rxdmacount, "Rx DMA burst length; 128 - disable (0-128)");
MODULE_PARM_DESC(rx_copybreak, "copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(max_interrupt_work, "maximum events handled per interrupt");
MODULE_PARM_DESC(multicast_filter_limit, "maximum number of filtered multicast addresses");

#define RUN_AT(x) (jiffies + (x))

#define netdevice_start(dev)
#define netdevice_stop(dev)
#define netif_set_tx_timeout(dev, tf, tm) \
								do { \
									(dev)->tx_timeout = (tf); \
									(dev)->watchdog_timeo = (tm); \
								} while(0)



/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the Intel i82557 "Speedo3" chip, Intel's
single-chip fast Ethernet controller for PCI, as used on the Intel
EtherExpress Pro 100 adapter.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS should be set to assign the
PCI INTA signal to an otherwise unused system IRQ line.  While it's
possible to share PCI interrupt lines, it negatively impacts performance and
only recent kernels support it.

III. Driver operation

IIIA. General
The Speedo3 is very similar to other Intel network chips, that is to say
"apparently designed on a different planet".  This chips retains the complex
Rx and Tx descriptors and multiple buffers pointers as previous chips, but
also has simplified Tx and Rx buffer modes.  This driver uses the "flexible"
Tx mode, but in a simplified lower-overhead manner: it associates only a
single buffer descriptor with each frame descriptor.

Despite the extra space overhead in each receive skbuff, the driver must use
the simplified Rx buffer mode to assure that only a single data buffer is
associated with each RxFD. The driver implements this by reserving space
for the Rx descriptor at the head of each Rx skbuff.

The Speedo-3 has receive and command unit base addresses that are added to
almost all descriptor pointers.  The driver sets these to zero, so that all
pointer fields are absolute addresses.

The System Control Block (SCB) of some previous Intel chips exists on the
chip in both PCI I/O and memory space.  This driver uses the I/O space
registers, but might switch to memory mapped mode to better support non-x86
processors.

IIIB. Transmit structure

The driver must use the complex Tx command+descriptor mode in order to
have a indirect pointer to the skbuff data section.  Each Tx command block
(TxCB) is associated with two immediately appended Tx Buffer Descriptor
(TxBD).  A fixed ring of these TxCB+TxBD pairs are kept as part of the
speedo_private data structure for each adapter instance.

The newer i82558 explicitly supports this structure, and can read the two
TxBDs in the same PCI burst as the TxCB.

This ring structure is used for all normal transmit packets, but the
transmit packet descriptors aren't long enough for most non-Tx commands such
as CmdConfigure.  This is complicated by the possibility that the chip has
already loaded the link address in the previous descriptor.  So for these
commands we convert the next free descriptor on the ring to a NoOp, and point
that descriptor's link to the complex command.

An additional complexity of these non-transmit commands are that they may be
added asynchronous to the normal transmit queue, so we disable interrupts
whenever the Tx descriptor ring is manipulated.

A notable aspect of these special configure commands is that they do
work with the normal Tx ring entry scavenge method.  The Tx ring scavenge
is done at interrupt time using the 'dirty_tx' index, and checking for the
command-complete bit.  While the setup frames may have the NoOp command on the
Tx ring marked as complete, but not have completed the setup command, this
is not a problem.  The tx_ring entry can be still safely reused, as the
tx_skbuff[] entry is always empty for config_cmd and mc_setup frames.

Commands may have bits set e.g. CmdSuspend in the command word to either
suspend or stop the transmit/command unit.  This driver always flags the last
command with CmdSuspend, erases the CmdSuspend in the previous command, and
then issues a CU_RESUME.
Note: Watch out for the potential race condition here: imagine
	erasing the previous suspend
		the chip processes the previous command
		the chip processes the final command, and suspends
	doing the CU_RESUME
		the chip processes the next-yet-valid post-final-command.
So blindly sending a CU_RESUME is only safe if we do it immediately after
after erasing the previous CmdSuspend, without the possibility of an
intervening delay.  Thus the resume command is always within the
interrupts-disabled region.  This is a timing dependence, but handling this
condition in a timing-independent way would considerably complicate the code.

Note: In previous generation Intel chips, restarting the command unit was a
notoriously slow process.  This is presumably no longer true.

IIIC. Receive structure

Because of the bus-master support on the Speedo3 this driver uses the new
SKBUFF_RX_COPYBREAK scheme, rather than a fixed intermediate receive buffer.
This scheme allocates full-sized skbuffs as receive buffers.  The value
SKBUFF_RX_COPYBREAK is used as the copying breakpoint: it is chosen to
trade-off the memory wasted by passing the full-sized skbuff to the queue
layer for all frames vs. the copying cost of copying a frame to a
correctly-sized skbuff.

For small frames the copying cost is negligible (esp. considering that we
are pre-loading the cache with immediately useful header information), so we
allocate a new, minimally-sized skbuff.  For large frames the copying cost
is non-trivial, and the larger copy might flush the cache of useful data, so
we pass up the skbuff the packet was received into.

IV. Notes

Thanks to Steve Williams of Intel for arranging the non-disclosure agreement
that stated that I could disclose the information.  But I still resent
having to sign an Intel NDA when I'm helping Intel sell their own product!

*/

static int speedo_found1(struct pci_dev *pdev, void __iomem *ioaddr, int fnd_cnt, int acpi_idle_state);

/* Offsets to the various registers.
   All accesses need not be longword aligned. */
enum speedo_offsets {
	SCBStatus = 0, SCBCmd = 2,	/* Rx/Command Unit command and status. */
	SCBIntmask = 3,
	SCBPointer = 4,				/* General purpose pointer. */
	SCBPort = 8,				/* Misc. commands and operands.  */
	SCBflash = 12, SCBeeprom = 14, /* EEPROM and flash memory control. */
	SCBCtrlMDI = 16,			/* MDI interface control. */
	SCBEarlyRx = 20,			/* Early receive byte count. */
};
/* Commands that can be put in a command list entry. */
enum commands {
	CmdNOp = 0, CmdIASetup = 0x10000, CmdConfigure = 0x20000,
	CmdMulticastList = 0x30000, CmdTx = 0x40000, CmdTDR = 0x50000,
	CmdDump = 0x60000, CmdDiagnose = 0x70000,
	CmdSuspend = 0x40000000,	/* Suspend after completion. */
	CmdIntr = 0x20000000,		/* Interrupt after completion. */
	CmdTxFlex = 0x00080000,		/* Use "Flexible mode" for CmdTx command. */
};
/* Clear CmdSuspend (1<<30) avoiding interference with the card access to the
   status bits.  Previous driver versions used separate 16 bit fields for
   commands and statuses.  --SAW
 */
#if defined(__alpha__)
# define clear_suspend(cmd)  clear_bit(30, &(cmd)->cmd_status);
#else
# if defined(__LITTLE_ENDIAN)
#  define clear_suspend(cmd)  ((__u16 *)&(cmd)->cmd_status)[1] &= ~0x4000
# elif defined(__BIG_ENDIAN)
#  define clear_suspend(cmd)  ((__u16 *)&(cmd)->cmd_status)[1] &= ~0x0040
# else
#  error Unsupported byteorder
# endif
#endif

enum SCBCmdBits {
	SCBMaskCmdDone=0x8000, SCBMaskRxDone=0x4000, SCBMaskCmdIdle=0x2000,
	SCBMaskRxSuspend=0x1000, SCBMaskEarlyRx=0x0800, SCBMaskFlowCtl=0x0400,
	SCBTriggerIntr=0x0200, SCBMaskAll=0x0100,
	/* The rest are Rx and Tx commands. */
	CUStart=0x0010, CUResume=0x0020, CUStatsAddr=0x0040, CUShowStats=0x0050,
	CUCmdBase=0x0060,	/* CU Base address (set to zero) . */
	CUDumpStats=0x0070, /* Dump then reset stats counters. */
	RxStart=0x0001, RxResume=0x0002, RxAbort=0x0004, RxAddrLoad=0x0006,
	RxResumeNoResources=0x0007,
};

enum SCBPort_cmds {
	PortReset=0, PortSelfTest=1, PortPartialReset=2, PortDump=3,
};

/* The Speedo3 Rx and Tx frame/buffer descriptors. */
struct descriptor {			    /* A generic descriptor. */
	volatile s32 cmd_status;	/* All command and status fields. */
	u32 link;				    /* struct descriptor *  */
	unsigned char params[0];
};

/* The Speedo3 Rx and Tx buffer descriptors. */
struct RxFD {					/* Receive frame descriptor. */
	volatile s32 status;
	u32 link;					/* struct RxFD * */
	u32 rx_buf_addr;			/* void * */
	u32 count;
} RxFD_ALIGNMENT;

/* Selected elements of the Tx/RxFD.status word. */
enum RxFD_bits {
	RxComplete=0x8000, RxOK=0x2000,
	RxErrCRC=0x0800, RxErrAlign=0x0400, RxErrTooBig=0x0200, RxErrSymbol=0x0010,
	RxEth2Type=0x0020, RxNoMatch=0x0004, RxNoIAMatch=0x0002,
	TxUnderrun=0x1000,  StatusComplete=0x8000,
};

#define CONFIG_DATA_SIZE 22
struct TxFD {					/* Transmit frame descriptor set. */
	s32 status;
	u32 link;					/* void * */
	u32 tx_desc_addr;			/* Always points to the tx_buf_addr element. */
	s32 count;					/* # of TBD (=1), Tx start thresh., etc. */
	/* This constitutes two "TBD" entries -- we only use one. */
#define TX_DESCR_BUF_OFFSET 16
	u32 tx_buf_addr0;			/* void *, frame to be transmitted.  */
	s32 tx_buf_size0;			/* Length of Tx frame. */
	u32 tx_buf_addr1;			/* void *, frame to be transmitted.  */
	s32 tx_buf_size1;			/* Length of Tx frame. */
	/* the structure must have space for at least CONFIG_DATA_SIZE starting
	 * from tx_desc_addr field */
};

/* Multicast filter setting block.  --SAW */
struct speedo_mc_block {
	struct speedo_mc_block *next;
	unsigned int tx;
	dma_addr_t frame_dma;
	unsigned int len;
	struct descriptor frame __attribute__ ((__aligned__(16)));
};

/* Elements of the dump_statistics block. This block must be lword aligned. */
struct speedo_stats {
	u32 tx_good_frames;
	u32 tx_coll16_errs;
	u32 tx_late_colls;
	u32 tx_underruns;
	u32 tx_lost_carrier;
	u32 tx_deferred;
	u32 tx_one_colls;
	u32 tx_multi_colls;
	u32 tx_total_colls;
	u32 rx_good_frames;
	u32 rx_crc_errs;
	u32 rx_align_errs;
	u32 rx_resource_errs;
	u32 rx_overrun_errs;
	u32 rx_colls_errs;
	u32 rx_runt_errs;
	u32 done_marker;
};

enum Rx_ring_state_bits {
	RrNoMem=1, RrPostponed=2, RrNoResources=4, RrOOMReported=8,
};

/* Do not change the position (alignment) of the first few elements!
   The later elements are grouped for cache locality.

   Unfortunately, all the positions have been shifted since there.
   A new re-alignment is required.  2000/03/06  SAW */
struct speedo_private {
    void __iomem *regs;
	struct TxFD	*tx_ring;		/* Commands (usually CmdTxPacket). */
	struct RxFD *rx_ringp[RX_RING_SIZE];	/* Rx descriptor, used as ring. */
	/* The addresses of a Tx/Rx-in-place packets/buffers. */
	struct sk_buff *tx_skbuff[TX_RING_SIZE];
	struct sk_buff *rx_skbuff[RX_RING_SIZE];
	/* Mapped addresses of the rings. */
	dma_addr_t tx_ring_dma;
#define TX_RING_ELEM_DMA(sp, n) ((sp)->tx_ring_dma + (n)*sizeof(struct TxFD))
	dma_addr_t rx_ring_dma[RX_RING_SIZE];
	struct descriptor *last_cmd;		/* Last command sent. */
	unsigned int cur_tx, dirty_tx;		/* The ring entries to be free()ed. */
	spinlock_t lock;			/* Group with Tx control cache line. */
	u32 tx_threshold;			/* The value for txdesc.count. */
	struct RxFD *last_rxf;			/* Last filled RX buffer. */
	dma_addr_t last_rxf_dma;
	unsigned int cur_rx, dirty_rx;		/* The next free ring entry */
	long last_rx_time;			/* Last Rx, in jiffies, to handle Rx hang. */
	struct net_device_stats stats;
	struct speedo_stats *lstats;
	dma_addr_t lstats_dma;
	int chip_id;
	struct pci_dev *pdev;
	struct timer_list timer;		/* Media selection timer. */
	struct speedo_mc_block *mc_setup_head;	/* Multicast setup frame list head. */
	struct speedo_mc_block *mc_setup_tail;	/* Multicast setup frame list tail. */
	long in_interrupt;			/* Word-aligned dev->interrupt */
	unsigned char acpi_pwr;
	signed char rx_mode;			/* Current PROMISC/ALLMULTI setting. */
	unsigned int tx_full:1;			/* The Tx queue is full. */
	unsigned int flow_ctrl:1;		/* Use 802.3x flow control. */
	unsigned int rx_bug:1;			/* Work around receiver hang errata. */
	unsigned char default_port:8;		/* Last dev->if_port value. */
	unsigned char rx_ring_state;		/* RX ring status flags. */
	unsigned short phy[2];			/* PHY media interfaces available. */
	unsigned short partner;			/* Link partner caps. */
	struct mii_if_info mii_if;		/* MII API hooks, info */
	u32 msg_enable;				/* debug message level */
};

/* The parameters for a CmdConfigure operation.
   There are so many options that it would be difficult to document each bit.
   We mostly use the default or recommended settings. */
static const char i82557_config_cmd[CONFIG_DATA_SIZE] = {
	22, 0x08, 0, 0,  0, 0, 0x32, 0x03,  1, /* 1=Use MII  0=Use AUI */
	0, 0x2E, 0,  0x60, 0,
	0xf2, 0x48,   0, 0x40, 0xf2, 0x80, 		/* 0x40=Force full-duplex */
	0x3f, 0x05, };
static const char i82558_config_cmd[CONFIG_DATA_SIZE] = {
	22, 0x08, 0, 1,  0, 0, 0x22, 0x03,  1, /* 1=Use MII  0=Use AUI */
	0, 0x2E, 0,  0x60, 0x08, 0x88,
	0x68, 0, 0x40, 0xf2, 0x84,		/* Disable FC */
	0x31, 0x05, };

/* PHY media interface chips. */
static const char * const phys[] = {
	"None", "i82553-A/B", "i82553-C", "i82503",
	"DP83840", "80c240", "80c24", "i82555",
	"unknown-8", "unknown-9", "DP83840A", "unknown-11",
	"unknown-12", "unknown-13", "unknown-14", "unknown-15", };
enum phy_chips { NonSuchPhy=0, I82553AB, I82553C, I82503, DP83840, S80C240,
					 S80C24, I82555, DP83840A=10, };
static const char is_mii[] = { 0, 1, 1, 0, 1, 1, 0, 1 };
#define EE_READ_CMD		(6)

static int eepro100_init_one(struct pci_dev *pdev,
		const struct pci_device_id *ent);

static int do_eeprom_cmd(void __iomem *ioaddr, int cmd, int cmd_len);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static int speedo_open(struct net_device *dev);
static void speedo_resume(struct net_device *dev);
static void speedo_timer(unsigned long data);
static void speedo_init_rx_ring(struct net_device *dev);
static void speedo_tx_timeout(struct net_device *dev);
static int speedo_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void speedo_refill_rx_buffers(struct net_device *dev, int force);
static int speedo_rx(struct net_device *dev);
static void speedo_tx_buffer_gc(struct net_device *dev);
static irqreturn_t speedo_interrupt(int irq, void *dev_instance);
static int speedo_close(struct net_device *dev);
static struct net_device_stats *speedo_get_stats(struct net_device *dev);
static int speedo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void set_rx_mode(struct net_device *dev);
static void speedo_show_state(struct net_device *dev);
static const struct ethtool_ops ethtool_ops;



#ifdef honor_default_port
/* Optional driver feature to allow forcing the transceiver setting.
   Not recommended. */
static int mii_ctrl[8] = { 0x3300, 0x3100, 0x0000, 0x0100,
						   0x2000, 0x2100, 0x0400, 0x3100};
#endif

/* How to wait for the command unit to accept a command.
   Typically this takes 0 ticks. */
static inline unsigned char wait_for_cmd_done(struct net_device *dev,
											  	struct speedo_private *sp)
{
	int wait = 1000;
	void __iomem *cmd_ioaddr = sp->regs + SCBCmd;
	unsigned char r;

	do  {
		udelay(1);
		r = ioread8(cmd_ioaddr);
	} while(r && --wait >= 0);

	if (wait < 0)
		printk(KERN_ALERT "%s: wait_for_cmd_done timeout!\n", dev->name);
	return r;
}

static int __devinit eepro100_init_one (struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	void __iomem *ioaddr;
	int irq, pci_bar;
	int acpi_idle_state = 0, pm;
	static int cards_found /* = 0 */;
	unsigned long pci_base;

#ifndef MODULE
	/* when built-in, we only print version if device is found */
	static int did_version;
	if (did_version++ == 0)
		printk(version);
#endif

	/* save power state before pci_enable_device overwrites it */
	pm = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm) {
		u16 pwr_command;
		pci_read_config_word(pdev, pm + PCI_PM_CTRL, &pwr_command);
		acpi_idle_state = pwr_command & PCI_PM_CTRL_STATE_MASK;
	}

	if (pci_enable_device(pdev))
		goto err_out_free_mmio_region;

	pci_set_master(pdev);

	if (!request_region(pci_resource_start(pdev, 1),
			pci_resource_len(pdev, 1), "eepro100")) {
		dev_err(&pdev->dev, "eepro100: cannot reserve I/O ports\n");
		goto err_out_none;
	}
	if (!request_mem_region(pci_resource_start(pdev, 0),
			pci_resource_len(pdev, 0), "eepro100")) {
		dev_err(&pdev->dev, "eepro100: cannot reserve MMIO region\n");
		goto err_out_free_pio_region;
	}

	irq = pdev->irq;
	pci_bar = use_io ? 1 : 0;
	pci_base = pci_resource_start(pdev, pci_bar);
	if (DEBUG & NETIF_MSG_PROBE)
		printk("Found Intel i82557 PCI Speedo at %#lx, IRQ %d.\n",
		       pci_base, irq);

	ioaddr = pci_iomap(pdev, pci_bar, 0);
	if (!ioaddr) {
		dev_err(&pdev->dev, "eepro100: cannot remap IO\n");
		goto err_out_free_mmio_region;
	}

	if (speedo_found1(pdev, ioaddr, cards_found, acpi_idle_state) == 0)
		cards_found++;
	else
		goto err_out_iounmap;

	return 0;

err_out_iounmap: ;
	pci_iounmap(pdev, ioaddr);
err_out_free_mmio_region:
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
err_out_free_pio_region:
	release_region(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1));
err_out_none:
	return -ENODEV;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */

static void poll_speedo (struct net_device *dev)
{
	/* disable_irq is not very nice, but with the funny lockless design
	   we have no other choice. */
	disable_irq(dev->irq);
	speedo_interrupt (dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static int __devinit speedo_found1(struct pci_dev *pdev,
		void __iomem *ioaddr, int card_idx, int acpi_idle_state)
{
	struct net_device *dev;
	struct speedo_private *sp;
	const char *product;
	int i, option;
	u16 eeprom[0x100];
	int size;
	void *tx_ring_space;
	dma_addr_t tx_ring_dma;
	DECLARE_MAC_BUF(mac);

	size = TX_RING_SIZE * sizeof(struct TxFD) + sizeof(struct speedo_stats);
	tx_ring_space = pci_alloc_consistent(pdev, size, &tx_ring_dma);
	if (tx_ring_space == NULL)
		return -1;

	dev = alloc_etherdev(sizeof(struct speedo_private));
	if (dev == NULL) {
		printk(KERN_ERR "eepro100: Could not allocate ethernet device.\n");
		pci_free_consistent(pdev, size, tx_ring_space, tx_ring_dma);
		return -1;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);

	if (dev->mem_start > 0)
		option = dev->mem_start;
	else if (card_idx >= 0  &&  options[card_idx] >= 0)
		option = options[card_idx];
	else
		option = 0;

	rtnl_lock();
	if (dev_alloc_name(dev, dev->name) < 0)
		goto err_free_unlock;

	/* Read the station address EEPROM before doing the reset.
	   Nominally his should even be done before accepting the device, but
	   then we wouldn't have a device name with which to report the error.
	   The size test is for 6 bit vs. 8 bit address serial EEPROMs.
	*/
	{
		void __iomem *iobase;
		int read_cmd, ee_size;
		u16 sum;
		int j;

		/* Use IO only to avoid postponed writes and satisfy EEPROM timing
		   requirements. */
		iobase = pci_iomap(pdev, 1, pci_resource_len(pdev, 1));
		if (!iobase)
			goto err_free_unlock;
		if ((do_eeprom_cmd(iobase, EE_READ_CMD << 24, 27) & 0xffe0000)
			== 0xffe0000) {
			ee_size = 0x100;
			read_cmd = EE_READ_CMD << 24;
		} else {
			ee_size = 0x40;
			read_cmd = EE_READ_CMD << 22;
		}

		for (j = 0, i = 0, sum = 0; i < ee_size; i++) {
			u16 value = do_eeprom_cmd(iobase, read_cmd | (i << 16), 27);
			eeprom[i] = value;
			sum += value;
			if (i < 3) {
				dev->dev_addr[j++] = value;
				dev->dev_addr[j++] = value >> 8;
			}
		}
		if (sum != 0xBABA)
			printk(KERN_WARNING "%s: Invalid EEPROM checksum %#4.4x, "
				   "check settings before activating this device!\n",
				   dev->name, sum);
		/* Don't  unregister_netdev(dev);  as the EEPro may actually be
		   usable, especially if the MAC address is set later.
		   On the other hand, it may be unusable if MDI data is corrupted. */

		pci_iounmap(pdev, iobase);
	}

	/* Reset the chip: stop Tx and Rx processes and clear counters.
	   This takes less than 10usec and will easily finish before the next
	   action. */
	iowrite32(PortReset, ioaddr + SCBPort);
	ioread32(ioaddr + SCBPort);
	udelay(10);

	if (eeprom[3] & 0x0100)
		product = "OEM i82557/i82558 10/100 Ethernet";
	else
		product = pci_name(pdev);

	printk(KERN_INFO "%s: %s, %s, IRQ %d.\n", dev->name, product,
		   print_mac(mac, dev->dev_addr), pdev->irq);

	sp = netdev_priv(dev);

	/* we must initialize this early, for mdio_{read,write} */
	sp->regs = ioaddr;

#if 1 || defined(kernel_bloat)
	/* OK, this is pure kernel bloat.  I don't like it when other drivers
	   waste non-pageable kernel space to emit similar messages, but I need
	   them for bug reports. */
	{
		const char *connectors[] = {" RJ45", " BNC", " AUI", " MII"};
		/* The self-test results must be paragraph aligned. */
		volatile s32 *self_test_results;
		int boguscnt = 16000;	/* Timeout for set-test. */
		if ((eeprom[3] & 0x03) != 0x03)
			printk(KERN_INFO "  Receiver lock-up bug exists -- enabling"
				   " work-around.\n");
		printk(KERN_INFO "  Board assembly %4.4x%2.2x-%3.3d, Physical"
			   " connectors present:",
			   eeprom[8], eeprom[9]>>8, eeprom[9] & 0xff);
		for (i = 0; i < 4; i++)
			if (eeprom[5] & (1<<i))
				printk(connectors[i]);
		printk("\n"KERN_INFO"  Primary interface chip %s PHY #%d.\n",
			   phys[(eeprom[6]>>8)&15], eeprom[6] & 0x1f);
		if (eeprom[7] & 0x0700)
			printk(KERN_INFO "    Secondary interface chip %s.\n",
				   phys[(eeprom[7]>>8)&7]);
		if (((eeprom[6]>>8) & 0x3f) == DP83840
			||  ((eeprom[6]>>8) & 0x3f) == DP83840A) {
			int mdi_reg23 = mdio_read(dev, eeprom[6] & 0x1f, 23) | 0x0422;
			if (congenb)
			  mdi_reg23 |= 0x0100;
			printk(KERN_INFO"  DP83840 specific setup, setting register 23 to %4.4x.\n",
				   mdi_reg23);
			mdio_write(dev, eeprom[6] & 0x1f, 23, mdi_reg23);
		}
		if ((option >= 0) && (option & 0x70)) {
			printk(KERN_INFO "  Forcing %dMbs %s-duplex operation.\n",
				   (option & 0x20 ? 100 : 10),
				   (option & 0x10 ? "full" : "half"));
			mdio_write(dev, eeprom[6] & 0x1f, MII_BMCR,
					   ((option & 0x20) ? 0x2000 : 0) | 	/* 100mbps? */
					   ((option & 0x10) ? 0x0100 : 0)); /* Full duplex? */
		}

		/* Perform a system self-test. */
		self_test_results = (s32*) ((((long) tx_ring_space) + 15) & ~0xf);
		self_test_results[0] = 0;
		self_test_results[1] = -1;
		iowrite32(tx_ring_dma | PortSelfTest, ioaddr + SCBPort);
		do {
			udelay(10);
		} while (self_test_results[1] == -1  &&  --boguscnt >= 0);

		if (boguscnt < 0) {		/* Test optimized out. */
			printk(KERN_ERR "Self test failed, status %8.8x:\n"
				   KERN_ERR " Failure to initialize the i82557.\n"
				   KERN_ERR " Verify that the card is a bus-master"
				   " capable slot.\n",
				   self_test_results[1]);
		} else
			printk(KERN_INFO "  General self-test: %s.\n"
				   KERN_INFO "  Serial sub-system self-test: %s.\n"
				   KERN_INFO "  Internal registers self-test: %s.\n"
				   KERN_INFO "  ROM checksum self-test: %s (%#8.8x).\n",
				   self_test_results[1] & 0x1000 ? "failed" : "passed",
				   self_test_results[1] & 0x0020 ? "failed" : "passed",
				   self_test_results[1] & 0x0008 ? "failed" : "passed",
				   self_test_results[1] & 0x0004 ? "failed" : "passed",
				   self_test_results[0]);
	}
#endif  /* kernel_bloat */

	iowrite32(PortReset, ioaddr + SCBPort);
	ioread32(ioaddr + SCBPort);
	udelay(10);

	/* Return the chip to its original power state. */
	pci_set_power_state(pdev, acpi_idle_state);

	pci_set_drvdata (pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	dev->irq = pdev->irq;

	sp->pdev = pdev;
	sp->msg_enable = DEBUG;
	sp->acpi_pwr = acpi_idle_state;
	sp->tx_ring = tx_ring_space;
	sp->tx_ring_dma = tx_ring_dma;
	sp->lstats = (struct speedo_stats *)(sp->tx_ring + TX_RING_SIZE);
	sp->lstats_dma = TX_RING_ELEM_DMA(sp, TX_RING_SIZE);
	init_timer(&sp->timer); /* used in ioctl() */
	spin_lock_init(&sp->lock);

	sp->mii_if.full_duplex = option >= 0 && (option & 0x10) ? 1 : 0;
	if (card_idx >= 0) {
		if (full_duplex[card_idx] >= 0)
			sp->mii_if.full_duplex = full_duplex[card_idx];
	}
	sp->default_port = option >= 0 ? (option & 0x0f) : 0;

	sp->phy[0] = eeprom[6];
	sp->phy[1] = eeprom[7];

	sp->mii_if.phy_id = eeprom[6] & 0x1f;
	sp->mii_if.phy_id_mask = 0x1f;
	sp->mii_if.reg_num_mask = 0x1f;
	sp->mii_if.dev = dev;
	sp->mii_if.mdio_read = mdio_read;
	sp->mii_if.mdio_write = mdio_write;

	sp->rx_bug = (eeprom[3] & 0x03) == 3 ? 0 : 1;
	if (((pdev->device > 0x1030 && (pdev->device < 0x103F)))
	    || (pdev->device == 0x2449) || (pdev->device == 0x2459)
            || (pdev->device == 0x245D)) {
	    	sp->chip_id = 1;
	}

	if (sp->rx_bug)
		printk(KERN_INFO "  Receiver lock-up workaround activated.\n");

	/* The Speedo-specific entries in the device structure. */
	dev->open = &speedo_open;
	dev->hard_start_xmit = &speedo_start_xmit;
	netif_set_tx_timeout(dev, &speedo_tx_timeout, TX_TIMEOUT);
	dev->stop = &speedo_close;
	dev->get_stats = &speedo_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &speedo_ioctl;
	SET_ETHTOOL_OPS(dev, &ethtool_ops);
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = &poll_speedo;
#endif

	if (register_netdevice(dev))
		goto err_free_unlock;
	rtnl_unlock();

	return 0;

 err_free_unlock:
	rtnl_unlock();
	free_netdev(dev);
	return -1;
}

static void do_slow_command(struct net_device *dev, struct speedo_private *sp, int cmd)
{
	void __iomem *cmd_ioaddr = sp->regs + SCBCmd;
	int wait = 0;
	do
		if (ioread8(cmd_ioaddr) == 0) break;
	while(++wait <= 200);
	if (wait > 100)
		printk(KERN_ERR "Command %4.4x never accepted (%d polls)!\n",
		       ioread8(cmd_ioaddr), wait);

	iowrite8(cmd, cmd_ioaddr);

	for (wait = 0; wait <= 100; wait++)
		if (ioread8(cmd_ioaddr) == 0) return;
	for (; wait <= 20000; wait++)
		if (ioread8(cmd_ioaddr) == 0) return;
		else udelay(1);
	printk(KERN_ERR "Command %4.4x was not accepted after %d polls!"
	       "  Current status %8.8x.\n",
	       cmd, wait, ioread32(sp->regs + SCBStatus));
}

/* Serial EEPROM section.
   A "bit" grungy, but we work our way through bit-by-bit :->. */
/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x01	/* EEPROM shift clock. */
#define EE_CS			0x02	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* EEPROM chip data in. */
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */
#define EE_ENB			(0x4800 | EE_CS)
#define EE_WRITE_0		0x4802
#define EE_WRITE_1		0x4806
#define EE_OFFSET		SCBeeprom

/* The fixes for the code were kindly provided by Dragan Stancevic
   <visitor@valinux.com> to strictly follow Intel specifications of EEPROM
   access timing.
   The publicly available sheet 64486302 (sec. 3.1) specifies 1us access
   interval for serial EEPROM.  However, it looks like that there is an
   additional requirement dictating larger udelay's in the code below.
   2000/05/24  SAW */
static int __devinit do_eeprom_cmd(void __iomem *ioaddr, int cmd, int cmd_len)
{
	unsigned retval = 0;
	void __iomem *ee_addr = ioaddr + SCBeeprom;

	iowrite16(EE_ENB, ee_addr); udelay(2);
	iowrite16(EE_ENB | EE_SHIFT_CLK, ee_addr); udelay(2);

	/* Shift the command bits out. */
	do {
		short dataval = (cmd & (1 << cmd_len)) ? EE_WRITE_1 : EE_WRITE_0;
		iowrite16(dataval, ee_addr); udelay(2);
		iowrite16(dataval | EE_SHIFT_CLK, ee_addr); udelay(2);
		retval = (retval << 1) | ((ioread16(ee_addr) & EE_DATA_READ) ? 1 : 0);
	} while (--cmd_len >= 0);
	iowrite16(EE_ENB, ee_addr); udelay(2);

	/* Terminate the EEPROM access. */
	iowrite16(EE_ENB & ~EE_CS, ee_addr);
	return retval;
}

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int val, boguscnt = 64*10;		/* <64 usec. to complete, typ 27 ticks */
	iowrite32(0x08000000 | (location<<16) | (phy_id<<21), ioaddr + SCBCtrlMDI);
	do {
		val = ioread32(ioaddr + SCBCtrlMDI);
		if (--boguscnt < 0) {
			printk(KERN_ERR " mdio_read() timed out with val = %8.8x.\n", val);
			break;
		}
	} while (! (val & 0x10000000));
	return val & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int val, boguscnt = 64*10;		/* <64 usec. to complete, typ 27 ticks */
	iowrite32(0x04000000 | (location<<16) | (phy_id<<21) | value,
		 ioaddr + SCBCtrlMDI);
	do {
		val = ioread32(ioaddr + SCBCtrlMDI);
		if (--boguscnt < 0) {
			printk(KERN_ERR" mdio_write() timed out with val = %8.8x.\n", val);
			break;
		}
	} while (! (val & 0x10000000));
}

static int
speedo_open(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int retval;

	if (netif_msg_ifup(sp))
		printk(KERN_DEBUG "%s: speedo_open() irq %d.\n", dev->name, dev->irq);

	pci_set_power_state(sp->pdev, PCI_D0);

	/* Set up the Tx queue early.. */
	sp->cur_tx = 0;
	sp->dirty_tx = 0;
	sp->last_cmd = NULL;
	sp->tx_full = 0;
	sp->in_interrupt = 0;

	/* .. we can safely take handler calls during init. */
	retval = request_irq(dev->irq, &speedo_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval) {
		return retval;
	}

	dev->if_port = sp->default_port;

#ifdef oh_no_you_dont_unless_you_honour_the_options_passed_in_to_us
	/* Retrigger negotiation to reset previous errors. */
	if ((sp->phy[0] & 0x8000) == 0) {
		int phy_addr = sp->phy[0] & 0x1f ;
		/* Use 0x3300 for restarting NWay, other values to force xcvr:
		   0x0000 10-HD
		   0x0100 10-FD
		   0x2000 100-HD
		   0x2100 100-FD
		*/
#ifdef honor_default_port
		mdio_write(dev, phy_addr, MII_BMCR, mii_ctrl[dev->default_port & 7]);
#else
		mdio_write(dev, phy_addr, MII_BMCR, 0x3300);
#endif
	}
#endif

	speedo_init_rx_ring(dev);

	/* Fire up the hardware. */
	iowrite16(SCBMaskAll, ioaddr + SCBCmd);
	speedo_resume(dev);

	netdevice_start(dev);
	netif_start_queue(dev);

	/* Setup the chip and configure the multicast list. */
	sp->mc_setup_head = NULL;
	sp->mc_setup_tail = NULL;
	sp->flow_ctrl = sp->partner = 0;
	sp->rx_mode = -1;			/* Invalid -> always reset the mode. */
	set_rx_mode(dev);
	if ((sp->phy[0] & 0x8000) == 0)
		sp->mii_if.advertising = mdio_read(dev, sp->phy[0] & 0x1f, MII_ADVERTISE);

	mii_check_link(&sp->mii_if);

	if (netif_msg_ifup(sp)) {
		printk(KERN_DEBUG "%s: Done speedo_open(), status %8.8x.\n",
			   dev->name, ioread16(ioaddr + SCBStatus));
	}

	/* Set the timer.  The timer serves a dual purpose:
	   1) to monitor the media interface (e.g. link beat) and perhaps switch
	   to an alternate media type
	   2) to monitor Rx activity, and restart the Rx process if the receiver
	   hangs. */
	sp->timer.expires = RUN_AT((24*HZ)/10); 			/* 2.4 sec. */
	sp->timer.data = (unsigned long)dev;
	sp->timer.function = &speedo_timer;					/* timer handler */
	add_timer(&sp->timer);

	/* No need to wait for the command unit to accept here. */
	if ((sp->phy[0] & 0x8000) == 0)
		mdio_read(dev, sp->phy[0] & 0x1f, MII_BMCR);

	return 0;
}

/* Start the chip hardware after a full reset. */
static void speedo_resume(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;

	/* Start with a Tx threshold of 256 (0x..20.... 8 byte units). */
	sp->tx_threshold = 0x01208000;

	/* Set the segment registers to '0'. */
	if (wait_for_cmd_done(dev, sp) != 0) {
		iowrite32(PortPartialReset, ioaddr + SCBPort);
		udelay(10);
	}

        iowrite32(0, ioaddr + SCBPointer);
        ioread32(ioaddr + SCBPointer);			/* Flush to PCI. */
        udelay(10);			/* Bogus, but it avoids the bug. */

        /* Note: these next two operations can take a while. */
        do_slow_command(dev, sp, RxAddrLoad);
        do_slow_command(dev, sp, CUCmdBase);

	/* Load the statistics block and rx ring addresses. */
	iowrite32(sp->lstats_dma, ioaddr + SCBPointer);
	ioread32(ioaddr + SCBPointer);			/* Flush to PCI */

	iowrite8(CUStatsAddr, ioaddr + SCBCmd);
	sp->lstats->done_marker = 0;
	wait_for_cmd_done(dev, sp);

	if (sp->rx_ringp[sp->cur_rx % RX_RING_SIZE] == NULL) {
		if (netif_msg_rx_err(sp))
			printk(KERN_DEBUG "%s: NULL cur_rx in speedo_resume().\n",
					dev->name);
	} else {
		iowrite32(sp->rx_ring_dma[sp->cur_rx % RX_RING_SIZE],
			 ioaddr + SCBPointer);
		ioread32(ioaddr + SCBPointer);		/* Flush to PCI */
	}

	/* Note: RxStart should complete instantly. */
	do_slow_command(dev, sp, RxStart);
	do_slow_command(dev, sp, CUDumpStats);

	/* Fill the first command with our physical address. */
	{
		struct descriptor *ias_cmd;

		ias_cmd =
			(struct descriptor *)&sp->tx_ring[sp->cur_tx++ % TX_RING_SIZE];
		/* Avoid a bug(?!) here by marking the command already completed. */
		ias_cmd->cmd_status = cpu_to_le32((CmdSuspend | CmdIASetup) | 0xa000);
		ias_cmd->link =
			cpu_to_le32(TX_RING_ELEM_DMA(sp, sp->cur_tx % TX_RING_SIZE));
		memcpy(ias_cmd->params, dev->dev_addr, 6);
		if (sp->last_cmd)
			clear_suspend(sp->last_cmd);
		sp->last_cmd = ias_cmd;
	}

	/* Start the chip's Tx process and unmask interrupts. */
	iowrite32(TX_RING_ELEM_DMA(sp, sp->dirty_tx % TX_RING_SIZE),
		 ioaddr + SCBPointer);
	/* We are not ACK-ing FCP and ER in the interrupt handler yet so they should
	   remain masked --Dragan */
	iowrite16(CUStart | SCBMaskEarlyRx | SCBMaskFlowCtl, ioaddr + SCBCmd);
}

/*
 * Sometimes the receiver stops making progress.  This routine knows how to
 * get it going again, without losing packets or being otherwise nasty like
 * a chip reset would be.  Previously the driver had a whole sequence
 * of if RxSuspended, if it's no buffers do one thing, if it's no resources,
 * do another, etc.  But those things don't really matter.  Separate logic
 * in the ISR provides for allocating buffers--the other half of operation
 * is just making sure the receiver is active.  speedo_rx_soft_reset does that.
 * This problem with the old, more involved algorithm is shown up under
 * ping floods on the order of 60K packets/second on a 100Mbps fdx network.
 */
static void
speedo_rx_soft_reset(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	struct RxFD *rfd;
	void __iomem *ioaddr;

	ioaddr = sp->regs;
	if (wait_for_cmd_done(dev, sp) != 0) {
		printk("%s: previous command stalled\n", dev->name);
		return;
	}
	/*
	* Put the hardware into a known state.
	*/
	iowrite8(RxAbort, ioaddr + SCBCmd);

	rfd = sp->rx_ringp[sp->cur_rx % RX_RING_SIZE];

	rfd->rx_buf_addr = 0xffffffff;

	if (wait_for_cmd_done(dev, sp) != 0) {
		printk("%s: RxAbort command stalled\n", dev->name);
		return;
	}
	iowrite32(sp->rx_ring_dma[sp->cur_rx % RX_RING_SIZE],
		ioaddr + SCBPointer);
	iowrite8(RxStart, ioaddr + SCBCmd);
}


/* Media monitoring and control. */
static void speedo_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int phy_num = sp->phy[0] & 0x1f;

	/* We have MII and lost link beat. */
	if ((sp->phy[0] & 0x8000) == 0) {
		int partner = mdio_read(dev, phy_num, MII_LPA);
		if (partner != sp->partner) {
			int flow_ctrl = sp->mii_if.advertising & partner & 0x0400 ? 1 : 0;
			if (netif_msg_link(sp)) {
				printk(KERN_DEBUG "%s: Link status change.\n", dev->name);
				printk(KERN_DEBUG "%s: Old partner %x, new %x, adv %x.\n",
					   dev->name, sp->partner, partner, sp->mii_if.advertising);
			}
			sp->partner = partner;
			if (flow_ctrl != sp->flow_ctrl) {
				sp->flow_ctrl = flow_ctrl;
				sp->rx_mode = -1;	/* Trigger a reload. */
			}
		}
	}
	mii_check_link(&sp->mii_if);
	if (netif_msg_timer(sp)) {
		printk(KERN_DEBUG "%s: Media control tick, status %4.4x.\n",
			   dev->name, ioread16(ioaddr + SCBStatus));
	}
	if (sp->rx_mode < 0  ||
		(sp->rx_bug  && jiffies - sp->last_rx_time > 2*HZ)) {
		/* We haven't received a packet in a Long Time.  We might have been
		   bitten by the receiver hang bug.  This can be cleared by sending
		   a set multicast list command. */
		if (netif_msg_timer(sp))
			printk(KERN_DEBUG "%s: Sending a multicast list set command"
				   " from a timer routine,"
				   " m=%d, j=%ld, l=%ld.\n",
				   dev->name, sp->rx_mode, jiffies, sp->last_rx_time);
		set_rx_mode(dev);
	}
	/* We must continue to monitor the media. */
	sp->timer.expires = RUN_AT(2*HZ); 			/* 2.0 sec. */
	add_timer(&sp->timer);
}

static void speedo_show_state(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	int i;

	if (netif_msg_pktdata(sp)) {
		printk(KERN_DEBUG "%s: Tx ring dump,  Tx queue %u / %u:\n",
		    dev->name, sp->cur_tx, sp->dirty_tx);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(KERN_DEBUG "%s:  %c%c%2d %8.8x.\n", dev->name,
			    i == sp->dirty_tx % TX_RING_SIZE ? '*' : ' ',
			    i == sp->cur_tx % TX_RING_SIZE ? '=' : ' ',
			    i, sp->tx_ring[i].status);

		printk(KERN_DEBUG "%s: Printing Rx ring"
		    " (next to receive into %u, dirty index %u).\n",
		    dev->name, sp->cur_rx, sp->dirty_rx);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(KERN_DEBUG "%s: %c%c%c%2d %8.8x.\n", dev->name,
			    sp->rx_ringp[i] == sp->last_rxf ? 'l' : ' ',
			    i == sp->dirty_rx % RX_RING_SIZE ? '*' : ' ',
			    i == sp->cur_rx % RX_RING_SIZE ? '=' : ' ',
			    i, (sp->rx_ringp[i] != NULL) ?
			    (unsigned)sp->rx_ringp[i]->status : 0);
	}

#if 0
	{
		void __iomem *ioaddr = sp->regs;
		int phy_num = sp->phy[0] & 0x1f;
		for (i = 0; i < 16; i++) {
			/* FIXME: what does it mean?  --SAW */
			if (i == 6) i = 21;
			printk(KERN_DEBUG "%s:  PHY index %d register %d is %4.4x.\n",
				   dev->name, phy_num, i, mdio_read(dev, phy_num, i));
		}
	}
#endif

}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
speedo_init_rx_ring(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	struct RxFD *rxf, *last_rxf = NULL;
	dma_addr_t last_rxf_dma = 0 /* to shut up the compiler */;
	int i;

	sp->cur_rx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb;
		skb = dev_alloc_skb(PKT_BUF_SZ + sizeof(struct RxFD));
		if (skb)
			rx_align(skb);        /* Align IP on 16 byte boundary */
		sp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;			/* OK.  Just initially short of Rx bufs. */
		skb->dev = dev;			/* Mark as being used by this device. */
		rxf = (struct RxFD *)skb->data;
		sp->rx_ringp[i] = rxf;
		sp->rx_ring_dma[i] =
			pci_map_single(sp->pdev, rxf,
					PKT_BUF_SZ + sizeof(struct RxFD), PCI_DMA_BIDIRECTIONAL);
		skb_reserve(skb, sizeof(struct RxFD));
		if (last_rxf) {
			last_rxf->link = cpu_to_le32(sp->rx_ring_dma[i]);
			pci_dma_sync_single_for_device(sp->pdev, last_rxf_dma,
										   sizeof(struct RxFD), PCI_DMA_TODEVICE);
		}
		last_rxf = rxf;
		last_rxf_dma = sp->rx_ring_dma[i];
		rxf->status = cpu_to_le32(0x00000001);	/* '1' is flag value only. */
		rxf->link = 0;						/* None yet. */
		/* This field unused by i82557. */
		rxf->rx_buf_addr = 0xffffffff;
		rxf->count = cpu_to_le32(PKT_BUF_SZ << 16);
		pci_dma_sync_single_for_device(sp->pdev, sp->rx_ring_dma[i],
									   sizeof(struct RxFD), PCI_DMA_TODEVICE);
	}
	sp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
	/* Mark the last entry as end-of-list. */
	last_rxf->status = cpu_to_le32(0xC0000002);	/* '2' is flag value only. */
	pci_dma_sync_single_for_device(sp->pdev, sp->rx_ring_dma[RX_RING_SIZE-1],
								   sizeof(struct RxFD), PCI_DMA_TODEVICE);
	sp->last_rxf = last_rxf;
	sp->last_rxf_dma = last_rxf_dma;
}

static void speedo_purge_tx(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	int entry;

	while ((int)(sp->cur_tx - sp->dirty_tx) > 0) {
		entry = sp->dirty_tx % TX_RING_SIZE;
		if (sp->tx_skbuff[entry]) {
			sp->stats.tx_errors++;
			pci_unmap_single(sp->pdev,
					le32_to_cpu(sp->tx_ring[entry].tx_buf_addr0),
					sp->tx_skbuff[entry]->len, PCI_DMA_TODEVICE);
			dev_kfree_skb_irq(sp->tx_skbuff[entry]);
			sp->tx_skbuff[entry] = NULL;
		}
		sp->dirty_tx++;
	}
	while (sp->mc_setup_head != NULL) {
		struct speedo_mc_block *t;
		if (netif_msg_tx_err(sp))
			printk(KERN_DEBUG "%s: freeing mc frame.\n", dev->name);
		pci_unmap_single(sp->pdev, sp->mc_setup_head->frame_dma,
				sp->mc_setup_head->len, PCI_DMA_TODEVICE);
		t = sp->mc_setup_head->next;
		kfree(sp->mc_setup_head);
		sp->mc_setup_head = t;
	}
	sp->mc_setup_tail = NULL;
	sp->tx_full = 0;
	netif_wake_queue(dev);
}

static void reset_mii(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);

	/* Reset the MII transceiver, suggested by Fred Young @ scalable.com. */
	if ((sp->phy[0] & 0x8000) == 0) {
		int phy_addr = sp->phy[0] & 0x1f;
		int advertising = mdio_read(dev, phy_addr, MII_ADVERTISE);
		int mii_bmcr = mdio_read(dev, phy_addr, MII_BMCR);
		mdio_write(dev, phy_addr, MII_BMCR, 0x0400);
		mdio_write(dev, phy_addr, MII_BMSR, 0x0000);
		mdio_write(dev, phy_addr, MII_ADVERTISE, 0x0000);
		mdio_write(dev, phy_addr, MII_BMCR, 0x8000);
#ifdef honor_default_port
		mdio_write(dev, phy_addr, MII_BMCR, mii_ctrl[dev->default_port & 7]);
#else
		mdio_read(dev, phy_addr, MII_BMCR);
		mdio_write(dev, phy_addr, MII_BMCR, mii_bmcr);
		mdio_write(dev, phy_addr, MII_ADVERTISE, advertising);
#endif
	}
}

static void speedo_tx_timeout(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int status = ioread16(ioaddr + SCBStatus);
	unsigned long flags;

	if (netif_msg_tx_err(sp)) {
		printk(KERN_WARNING "%s: Transmit timed out: status %4.4x "
		   " %4.4x at %d/%d command %8.8x.\n",
		   dev->name, status, ioread16(ioaddr + SCBCmd),
		   sp->dirty_tx, sp->cur_tx,
		   sp->tx_ring[sp->dirty_tx % TX_RING_SIZE].status);

	}
	speedo_show_state(dev);
#if 0
	if ((status & 0x00C0) != 0x0080
		&&  (status & 0x003C) == 0x0010) {
		/* Only the command unit has stopped. */
		printk(KERN_WARNING "%s: Trying to restart the transmitter...\n",
			   dev->name);
		iowrite32(TX_RING_ELEM_DMA(sp, dirty_tx % TX_RING_SIZE]),
			 ioaddr + SCBPointer);
		iowrite16(CUStart, ioaddr + SCBCmd);
		reset_mii(dev);
	} else {
#else
	{
#endif
		del_timer_sync(&sp->timer);
		/* Reset the Tx and Rx units. */
		iowrite32(PortReset, ioaddr + SCBPort);
		/* We may get spurious interrupts here.  But I don't think that they
		   may do much harm.  1999/12/09 SAW */
		udelay(10);
		/* Disable interrupts. */
		iowrite16(SCBMaskAll, ioaddr + SCBCmd);
		synchronize_irq(dev->irq);
		speedo_tx_buffer_gc(dev);
		/* Free as much as possible.
		   It helps to recover from a hang because of out-of-memory.
		   It also simplifies speedo_resume() in case TX ring is full or
		   close-to-be full. */
		speedo_purge_tx(dev);
		speedo_refill_rx_buffers(dev, 1);
		spin_lock_irqsave(&sp->lock, flags);
		speedo_resume(dev);
		sp->rx_mode = -1;
		dev->trans_start = jiffies;
		spin_unlock_irqrestore(&sp->lock, flags);
		set_rx_mode(dev); /* it takes the spinlock itself --SAW */
		/* Reset MII transceiver.  Do it before starting the timer to serialize
		   mdio_xxx operations.  Yes, it's a paranoya :-)  2000/05/09 SAW */
		reset_mii(dev);
		sp->timer.expires = RUN_AT(2*HZ);
		add_timer(&sp->timer);
	}
	return;
}

static int
speedo_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int entry;

	/* Prevent interrupts from changing the Tx ring from underneath us. */
	unsigned long flags;

	spin_lock_irqsave(&sp->lock, flags);

	/* Check if there are enough space. */
	if ((int)(sp->cur_tx - sp->dirty_tx) >= TX_QUEUE_LIMIT) {
		printk(KERN_ERR "%s: incorrect tbusy state, fixed.\n", dev->name);
		netif_stop_queue(dev);
		sp->tx_full = 1;
		spin_unlock_irqrestore(&sp->lock, flags);
		return 1;
	}

	/* Calculate the Tx descriptor entry. */
	entry = sp->cur_tx++ % TX_RING_SIZE;

	sp->tx_skbuff[entry] = skb;
	sp->tx_ring[entry].status =
		cpu_to_le32(CmdSuspend | CmdTx | CmdTxFlex);
	if (!(entry & ((TX_RING_SIZE>>2)-1)))
		sp->tx_ring[entry].status |= cpu_to_le32(CmdIntr);
	sp->tx_ring[entry].link =
		cpu_to_le32(TX_RING_ELEM_DMA(sp, sp->cur_tx % TX_RING_SIZE));
	sp->tx_ring[entry].tx_desc_addr =
		cpu_to_le32(TX_RING_ELEM_DMA(sp, entry) + TX_DESCR_BUF_OFFSET);
	/* The data region is always in one buffer descriptor. */
	sp->tx_ring[entry].count = cpu_to_le32(sp->tx_threshold);
	sp->tx_ring[entry].tx_buf_addr0 =
		cpu_to_le32(pci_map_single(sp->pdev, skb->data,
					   skb->len, PCI_DMA_TODEVICE));
	sp->tx_ring[entry].tx_buf_size0 = cpu_to_le32(skb->len);

	/* workaround for hardware bug on 10 mbit half duplex */

	if ((sp->partner == 0) && (sp->chip_id == 1)) {
		wait_for_cmd_done(dev, sp);
		iowrite8(0 , ioaddr + SCBCmd);
		udelay(1);
	}

	/* Trigger the command unit resume. */
	wait_for_cmd_done(dev, sp);
	clear_suspend(sp->last_cmd);
	/* We want the time window between clearing suspend flag on the previous
	   command and resuming CU to be as small as possible.
	   Interrupts in between are very undesired.  --SAW */
	iowrite8(CUResume, ioaddr + SCBCmd);
	sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];

	/* Leave room for set_rx_mode(). If there is no more space than reserved
	   for multicast filter mark the ring as full. */
	if ((int)(sp->cur_tx - sp->dirty_tx) >= TX_QUEUE_LIMIT) {
		netif_stop_queue(dev);
		sp->tx_full = 1;
	}

	spin_unlock_irqrestore(&sp->lock, flags);

	dev->trans_start = jiffies;

	return 0;
}

static void speedo_tx_buffer_gc(struct net_device *dev)
{
	unsigned int dirty_tx;
	struct speedo_private *sp = netdev_priv(dev);

	dirty_tx = sp->dirty_tx;
	while ((int)(sp->cur_tx - dirty_tx) > 0) {
		int entry = dirty_tx % TX_RING_SIZE;
		int status = le32_to_cpu(sp->tx_ring[entry].status);

		if (netif_msg_tx_done(sp))
			printk(KERN_DEBUG " scavenge candidate %d status %4.4x.\n",
				   entry, status);
		if ((status & StatusComplete) == 0)
			break;			/* It still hasn't been processed. */
		if (status & TxUnderrun)
			if (sp->tx_threshold < 0x01e08000) {
				if (netif_msg_tx_err(sp))
					printk(KERN_DEBUG "%s: TX underrun, threshold adjusted.\n",
						   dev->name);
				sp->tx_threshold += 0x00040000;
			}
		/* Free the original skb. */
		if (sp->tx_skbuff[entry]) {
			sp->stats.tx_packets++;	/* Count only user packets. */
			sp->stats.tx_bytes += sp->tx_skbuff[entry]->len;
			pci_unmap_single(sp->pdev,
					le32_to_cpu(sp->tx_ring[entry].tx_buf_addr0),
					sp->tx_skbuff[entry]->len, PCI_DMA_TODEVICE);
			dev_kfree_skb_irq(sp->tx_skbuff[entry]);
			sp->tx_skbuff[entry] = NULL;
		}
		dirty_tx++;
	}

	if (netif_msg_tx_err(sp) && (int)(sp->cur_tx - dirty_tx) > TX_RING_SIZE) {
		printk(KERN_ERR "out-of-sync dirty pointer, %d vs. %d,"
			   " full=%d.\n",
			   dirty_tx, sp->cur_tx, sp->tx_full);
		dirty_tx += TX_RING_SIZE;
	}

	while (sp->mc_setup_head != NULL
		   && (int)(dirty_tx - sp->mc_setup_head->tx - 1) > 0) {
		struct speedo_mc_block *t;
		if (netif_msg_tx_err(sp))
			printk(KERN_DEBUG "%s: freeing mc frame.\n", dev->name);
		pci_unmap_single(sp->pdev, sp->mc_setup_head->frame_dma,
				sp->mc_setup_head->len, PCI_DMA_TODEVICE);
		t = sp->mc_setup_head->next;
		kfree(sp->mc_setup_head);
		sp->mc_setup_head = t;
	}
	if (sp->mc_setup_head == NULL)
		sp->mc_setup_tail = NULL;

	sp->dirty_tx = dirty_tx;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t speedo_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	struct speedo_private *sp;
	void __iomem *ioaddr;
	long boguscnt = max_interrupt_work;
	unsigned short status;
	unsigned int handled = 0;

	sp = netdev_priv(dev);
	ioaddr = sp->regs;

#ifndef final_version
	/* A lock to prevent simultaneous entry on SMP machines. */
	if (test_and_set_bit(0, (void*)&sp->in_interrupt)) {
		printk(KERN_ERR"%s: SMP simultaneous entry of an interrupt handler.\n",
			   dev->name);
		sp->in_interrupt = 0;	/* Avoid halting machine. */
		return IRQ_NONE;
	}
#endif

	do {
		status = ioread16(ioaddr + SCBStatus);
		/* Acknowledge all of the current interrupt sources ASAP. */
		/* Will change from 0xfc00 to 0xff00 when we start handling
		   FCP and ER interrupts --Dragan */
		iowrite16(status & 0xfc00, ioaddr + SCBStatus);

		if (netif_msg_intr(sp))
			printk(KERN_DEBUG "%s: interrupt  status=%#4.4x.\n",
				   dev->name, status);

		if ((status & 0xfc00) == 0)
			break;
		handled = 1;


		if ((status & 0x5000) ||	/* Packet received, or Rx error. */
			(sp->rx_ring_state&(RrNoMem|RrPostponed)) == RrPostponed)
									/* Need to gather the postponed packet. */
			speedo_rx(dev);

		/* Always check if all rx buffers are allocated.  --SAW */
		speedo_refill_rx_buffers(dev, 0);

		spin_lock(&sp->lock);
		/*
		 * The chip may have suspended reception for various reasons.
		 * Check for that, and re-prime it should this be the case.
		 */
		switch ((status >> 2) & 0xf) {
		case 0: /* Idle */
			break;
		case 1:	/* Suspended */
		case 2:	/* No resources (RxFDs) */
		case 9:	/* Suspended with no more RBDs */
		case 10: /* No resources due to no RBDs */
		case 12: /* Ready with no RBDs */
			speedo_rx_soft_reset(dev);
			break;
		case 3:  case 5:  case 6:  case 7:  case 8:
		case 11:  case 13:  case 14:  case 15:
			/* these are all reserved values */
			break;
		}


		/* User interrupt, Command/Tx unit interrupt or CU not active. */
		if (status & 0xA400) {
			speedo_tx_buffer_gc(dev);
			if (sp->tx_full
				&& (int)(sp->cur_tx - sp->dirty_tx) < TX_QUEUE_UNFULL) {
				/* The ring is no longer full. */
				sp->tx_full = 0;
				netif_wake_queue(dev); /* Attention: under a spinlock.  --SAW */
			}
		}

		spin_unlock(&sp->lock);

		if (--boguscnt < 0) {
			printk(KERN_ERR "%s: Too much work at interrupt, status=0x%4.4x.\n",
				   dev->name, status);
			/* Clear all interrupt sources. */
			/* Will change from 0xfc00 to 0xff00 when we start handling
			   FCP and ER interrupts --Dragan */
			iowrite16(0xfc00, ioaddr + SCBStatus);
			break;
		}
	} while (1);

	if (netif_msg_intr(sp))
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, ioread16(ioaddr + SCBStatus));

	clear_bit(0, (void*)&sp->in_interrupt);
	return IRQ_RETVAL(handled);
}

static inline struct RxFD *speedo_rx_alloc(struct net_device *dev, int entry)
{
	struct speedo_private *sp = netdev_priv(dev);
	struct RxFD *rxf;
	struct sk_buff *skb;
	/* Get a fresh skbuff to replace the consumed one. */
	skb = dev_alloc_skb(PKT_BUF_SZ + sizeof(struct RxFD));
	if (skb)
		rx_align(skb);		/* Align IP on 16 byte boundary */
	sp->rx_skbuff[entry] = skb;
	if (skb == NULL) {
		sp->rx_ringp[entry] = NULL;
		return NULL;
	}
	rxf = sp->rx_ringp[entry] = (struct RxFD *)skb->data;
	sp->rx_ring_dma[entry] =
		pci_map_single(sp->pdev, rxf,
					   PKT_BUF_SZ + sizeof(struct RxFD), PCI_DMA_FROMDEVICE);
	skb->dev = dev;
	skb_reserve(skb, sizeof(struct RxFD));
	rxf->rx_buf_addr = 0xffffffff;
	pci_dma_sync_single_for_device(sp->pdev, sp->rx_ring_dma[entry],
								   sizeof(struct RxFD), PCI_DMA_TODEVICE);
	return rxf;
}

static inline void speedo_rx_link(struct net_device *dev, int entry,
								  struct RxFD *rxf, dma_addr_t rxf_dma)
{
	struct speedo_private *sp = netdev_priv(dev);
	rxf->status = cpu_to_le32(0xC0000001); 	/* '1' for driver use only. */
	rxf->link = 0;			/* None yet. */
	rxf->count = cpu_to_le32(PKT_BUF_SZ << 16);
	sp->last_rxf->link = cpu_to_le32(rxf_dma);
	sp->last_rxf->status &= cpu_to_le32(~0xC0000000);
	pci_dma_sync_single_for_device(sp->pdev, sp->last_rxf_dma,
								   sizeof(struct RxFD), PCI_DMA_TODEVICE);
	sp->last_rxf = rxf;
	sp->last_rxf_dma = rxf_dma;
}

static int speedo_refill_rx_buf(struct net_device *dev, int force)
{
	struct speedo_private *sp = netdev_priv(dev);
	int entry;
	struct RxFD *rxf;

	entry = sp->dirty_rx % RX_RING_SIZE;
	if (sp->rx_skbuff[entry] == NULL) {
		rxf = speedo_rx_alloc(dev, entry);
		if (rxf == NULL) {
			unsigned int forw;
			int forw_entry;
			if (netif_msg_rx_err(sp) || !(sp->rx_ring_state & RrOOMReported)) {
				printk(KERN_WARNING "%s: can't fill rx buffer (force %d)!\n",
						dev->name, force);
				sp->rx_ring_state |= RrOOMReported;
			}
			speedo_show_state(dev);
			if (!force)
				return -1;	/* Better luck next time!  */
			/* Borrow an skb from one of next entries. */
			for (forw = sp->dirty_rx + 1; forw != sp->cur_rx; forw++)
				if (sp->rx_skbuff[forw % RX_RING_SIZE] != NULL)
					break;
			if (forw == sp->cur_rx)
				return -1;
			forw_entry = forw % RX_RING_SIZE;
			sp->rx_skbuff[entry] = sp->rx_skbuff[forw_entry];
			sp->rx_skbuff[forw_entry] = NULL;
			rxf = sp->rx_ringp[forw_entry];
			sp->rx_ringp[forw_entry] = NULL;
			sp->rx_ringp[entry] = rxf;
		}
	} else {
		rxf = sp->rx_ringp[entry];
	}
	speedo_rx_link(dev, entry, rxf, sp->rx_ring_dma[entry]);
	sp->dirty_rx++;
	sp->rx_ring_state &= ~(RrNoMem|RrOOMReported); /* Mark the progress. */
	return 0;
}

static void speedo_refill_rx_buffers(struct net_device *dev, int force)
{
	struct speedo_private *sp = netdev_priv(dev);

	/* Refill the RX ring. */
	while ((int)(sp->cur_rx - sp->dirty_rx) > 0 &&
			speedo_refill_rx_buf(dev, force) != -1);
}

static int
speedo_rx(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	int entry = sp->cur_rx % RX_RING_SIZE;
	int rx_work_limit = sp->dirty_rx + RX_RING_SIZE - sp->cur_rx;
	int alloc_ok = 1;
	int npkts = 0;

	if (netif_msg_intr(sp))
		printk(KERN_DEBUG " In speedo_rx().\n");
	/* If we own the next entry, it's a new packet. Send it up. */
	while (sp->rx_ringp[entry] != NULL) {
		int status;
		int pkt_len;

		pci_dma_sync_single_for_cpu(sp->pdev, sp->rx_ring_dma[entry],
									sizeof(struct RxFD), PCI_DMA_FROMDEVICE);
		status = le32_to_cpu(sp->rx_ringp[entry]->status);
		pkt_len = le32_to_cpu(sp->rx_ringp[entry]->count) & 0x3fff;

		if (!(status & RxComplete))
			break;

		if (--rx_work_limit < 0)
			break;

		/* Check for a rare out-of-memory case: the current buffer is
		   the last buffer allocated in the RX ring.  --SAW */
		if (sp->last_rxf == sp->rx_ringp[entry]) {
			/* Postpone the packet.  It'll be reaped at an interrupt when this
			   packet is no longer the last packet in the ring. */
			if (netif_msg_rx_err(sp))
				printk(KERN_DEBUG "%s: RX packet postponed!\n",
					   dev->name);
			sp->rx_ring_state |= RrPostponed;
			break;
		}

		if (netif_msg_rx_status(sp))
			printk(KERN_DEBUG "  speedo_rx() status %8.8x len %d.\n", status,
				   pkt_len);
		if ((status & (RxErrTooBig|RxOK|0x0f90)) != RxOK) {
			if (status & RxErrTooBig)
				printk(KERN_ERR "%s: Ethernet frame overran the Rx buffer, "
					   "status %8.8x!\n", dev->name, status);
			else if (! (status & RxOK)) {
				/* There was a fatal error.  This *should* be impossible. */
				sp->stats.rx_errors++;
				printk(KERN_ERR "%s: Anomalous event in speedo_rx(), "
					   "status %8.8x.\n",
					   dev->name, status);
			}
		} else {
			struct sk_buff *skb;

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != 0) {
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
				pci_dma_sync_single_for_cpu(sp->pdev, sp->rx_ring_dma[entry],
											sizeof(struct RxFD) + pkt_len,
											PCI_DMA_FROMDEVICE);

#if 1 || USE_IP_CSUM
				/* Packet is in one chunk -- we can copy + cksum. */
				skb_copy_to_linear_data(skb, sp->rx_skbuff[entry]->data, pkt_len);
				skb_put(skb, pkt_len);
#else
				skb_copy_from_linear_data(sp->rx_skbuff[entry],
							  skb_put(skb, pkt_len),
							  pkt_len);
#endif
				pci_dma_sync_single_for_device(sp->pdev, sp->rx_ring_dma[entry],
											   sizeof(struct RxFD) + pkt_len,
											   PCI_DMA_FROMDEVICE);
				npkts++;
			} else {
				/* Pass up the already-filled skbuff. */
				skb = sp->rx_skbuff[entry];
				if (skb == NULL) {
					printk(KERN_ERR "%s: Inconsistent Rx descriptor chain.\n",
						   dev->name);
					break;
				}
				sp->rx_skbuff[entry] = NULL;
				skb_put(skb, pkt_len);
				npkts++;
				sp->rx_ringp[entry] = NULL;
				pci_unmap_single(sp->pdev, sp->rx_ring_dma[entry],
								 PKT_BUF_SZ + sizeof(struct RxFD),
								 PCI_DMA_FROMDEVICE);
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			sp->stats.rx_packets++;
			sp->stats.rx_bytes += pkt_len;
		}
		entry = (++sp->cur_rx) % RX_RING_SIZE;
		sp->rx_ring_state &= ~RrPostponed;
		/* Refill the recently taken buffers.
		   Do it one-by-one to handle traffic bursts better. */
		if (alloc_ok && speedo_refill_rx_buf(dev, 0) == -1)
			alloc_ok = 0;
	}

	/* Try hard to refill the recently taken buffers. */
	speedo_refill_rx_buffers(dev, 1);

	if (npkts)
		sp->last_rx_time = jiffies;

	return 0;
}

static int
speedo_close(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int i;

	netdevice_stop(dev);
	netif_stop_queue(dev);

	if (netif_msg_ifdown(sp))
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, ioread16(ioaddr + SCBStatus));

	/* Shut off the media monitoring timer. */
	del_timer_sync(&sp->timer);

	iowrite16(SCBMaskAll, ioaddr + SCBCmd);

	/* Shutting down the chip nicely fails to disable flow control. So.. */
	iowrite32(PortPartialReset, ioaddr + SCBPort);
	ioread32(ioaddr + SCBPort); /* flush posted write */
	/*
	 * The chip requires a 10 microsecond quiet period.  Wait here!
	 */
	udelay(10);

	free_irq(dev->irq, dev);
	speedo_show_state(dev);

    /* Free all the skbuffs in the Rx and Tx queues. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = sp->rx_skbuff[i];
		sp->rx_skbuff[i] = NULL;
		/* Clear the Rx descriptors. */
		if (skb) {
			pci_unmap_single(sp->pdev,
					 sp->rx_ring_dma[i],
					 PKT_BUF_SZ + sizeof(struct RxFD), PCI_DMA_FROMDEVICE);
			dev_kfree_skb(skb);
		}
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = sp->tx_skbuff[i];
		sp->tx_skbuff[i] = NULL;
		/* Clear the Tx descriptors. */
		if (skb) {
			pci_unmap_single(sp->pdev,
					 le32_to_cpu(sp->tx_ring[i].tx_buf_addr0),
					 skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb(skb);
		}
	}

	/* Free multicast setting blocks. */
	for (i = 0; sp->mc_setup_head != NULL; i++) {
		struct speedo_mc_block *t;
		t = sp->mc_setup_head->next;
		kfree(sp->mc_setup_head);
		sp->mc_setup_head = t;
	}
	sp->mc_setup_tail = NULL;
	if (netif_msg_ifdown(sp))
		printk(KERN_DEBUG "%s: %d multicast blocks dropped.\n", dev->name, i);

	pci_set_power_state(sp->pdev, PCI_D2);

	return 0;
}

/* The Speedo-3 has an especially awkward and unusable method of getting
   statistics out of the chip.  It takes an unpredictable length of time
   for the dump-stats command to complete.  To avoid a busy-wait loop we
   update the stats with the previous dump results, and then trigger a
   new dump.

   Oh, and incoming frames are dropped while executing dump-stats!
   */
static struct net_device_stats *
speedo_get_stats(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;

	/* Update only if the previous dump finished. */
	if (sp->lstats->done_marker == le32_to_cpu(0xA007)) {
		sp->stats.tx_aborted_errors += le32_to_cpu(sp->lstats->tx_coll16_errs);
		sp->stats.tx_window_errors += le32_to_cpu(sp->lstats->tx_late_colls);
		sp->stats.tx_fifo_errors += le32_to_cpu(sp->lstats->tx_underruns);
		sp->stats.tx_fifo_errors += le32_to_cpu(sp->lstats->tx_lost_carrier);
		/*sp->stats.tx_deferred += le32_to_cpu(sp->lstats->tx_deferred);*/
		sp->stats.collisions += le32_to_cpu(sp->lstats->tx_total_colls);
		sp->stats.rx_crc_errors += le32_to_cpu(sp->lstats->rx_crc_errs);
		sp->stats.rx_frame_errors += le32_to_cpu(sp->lstats->rx_align_errs);
		sp->stats.rx_over_errors += le32_to_cpu(sp->lstats->rx_resource_errs);
		sp->stats.rx_fifo_errors += le32_to_cpu(sp->lstats->rx_overrun_errs);
		sp->stats.rx_length_errors += le32_to_cpu(sp->lstats->rx_runt_errs);
		sp->lstats->done_marker = 0x0000;
		if (netif_running(dev)) {
			unsigned long flags;
			/* Take a spinlock to make wait_for_cmd_done and sending the
			   command atomic.  --SAW */
			spin_lock_irqsave(&sp->lock, flags);
			wait_for_cmd_done(dev, sp);
			iowrite8(CUDumpStats, ioaddr + SCBCmd);
			spin_unlock_irqrestore(&sp->lock, flags);
		}
	}
	return &sp->stats;
}

static void speedo_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct speedo_private *sp = netdev_priv(dev);
	strncpy(info->driver, "eepro100", sizeof(info->driver)-1);
	strncpy(info->version, version, sizeof(info->version)-1);
	if (sp->pdev)
		strcpy(info->bus_info, pci_name(sp->pdev));
}

static int speedo_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct speedo_private *sp = netdev_priv(dev);
	spin_lock_irq(&sp->lock);
	mii_ethtool_gset(&sp->mii_if, ecmd);
	spin_unlock_irq(&sp->lock);
	return 0;
}

static int speedo_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct speedo_private *sp = netdev_priv(dev);
	int res;
	spin_lock_irq(&sp->lock);
	res = mii_ethtool_sset(&sp->mii_if, ecmd);
	spin_unlock_irq(&sp->lock);
	return res;
}

static int speedo_nway_reset(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	return mii_nway_restart(&sp->mii_if);
}

static u32 speedo_get_link(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	return mii_link_ok(&sp->mii_if);
}

static u32 speedo_get_msglevel(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	return sp->msg_enable;
}

static void speedo_set_msglevel(struct net_device *dev, u32 v)
{
	struct speedo_private *sp = netdev_priv(dev);
	sp->msg_enable = v;
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo = speedo_get_drvinfo,
	.get_settings = speedo_get_settings,
	.set_settings = speedo_set_settings,
	.nway_reset = speedo_nway_reset,
	.get_link = speedo_get_link,
	.get_msglevel = speedo_get_msglevel,
	.set_msglevel = speedo_set_msglevel,
};

static int speedo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct speedo_private *sp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	int phy = sp->phy[0] & 0x1f;
	int saved_acpi;
	int t;

    switch(cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		data->phy_id = phy;

	case SIOCGMIIREG:		/* Read MII PHY register. */
		/* FIXME: these operations need to be serialized with MDIO
		   access from the timeout handler.
		   They are currently serialized only with MDIO access from the
		   timer routine.  2000/05/09 SAW */
		saved_acpi = pci_set_power_state(sp->pdev, PCI_D0);
		t = del_timer_sync(&sp->timer);
		data->val_out = mdio_read(dev, data->phy_id & 0x1f, data->reg_num & 0x1f);
		if (t)
			add_timer(&sp->timer); /* may be set to the past  --SAW */
		pci_set_power_state(sp->pdev, saved_acpi);
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		saved_acpi = pci_set_power_state(sp->pdev, PCI_D0);
		t = del_timer_sync(&sp->timer);
		mdio_write(dev, data->phy_id, data->reg_num, data->val_in);
		if (t)
			add_timer(&sp->timer); /* may be set to the past  --SAW */
		pci_set_power_state(sp->pdev, saved_acpi);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

/* Set or clear the multicast filter for this adaptor.
   This is very ugly with Intel chips -- we usually have to execute an
   entire configuration command, plus process a multicast command.
   This is complicated.  We must put a large configuration command and
   an arbitrarily-sized multicast command in the transmit list.
   To minimize the disruption -- the previous command might have already
   loaded the link -- we convert the current command block, normally a Tx
   command, into a no-op and link it to the new command.
*/
static void set_rx_mode(struct net_device *dev)
{
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	struct descriptor *last_cmd;
	char new_rx_mode;
	unsigned long flags;
	int entry, i;

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		new_rx_mode = 3;
	} else if ((dev->flags & IFF_ALLMULTI)  ||
			   dev->mc_count > multicast_filter_limit) {
		new_rx_mode = 1;
	} else
		new_rx_mode = 0;

	if (netif_msg_rx_status(sp))
		printk(KERN_DEBUG "%s: set_rx_mode %d -> %d\n", dev->name,
				sp->rx_mode, new_rx_mode);

	if ((int)(sp->cur_tx - sp->dirty_tx) > TX_RING_SIZE - TX_MULTICAST_SIZE) {
	    /* The Tx ring is full -- don't add anything!  Hope the mode will be
		 * set again later. */
		sp->rx_mode = -1;
		return;
	}

	if (new_rx_mode != sp->rx_mode) {
		u8 *config_cmd_data;

		spin_lock_irqsave(&sp->lock, flags);
		entry = sp->cur_tx++ % TX_RING_SIZE;
		last_cmd = sp->last_cmd;
		sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];

		sp->tx_skbuff[entry] = NULL;			/* Redundant. */
		sp->tx_ring[entry].status = cpu_to_le32(CmdSuspend | CmdConfigure);
		sp->tx_ring[entry].link =
			cpu_to_le32(TX_RING_ELEM_DMA(sp, (entry + 1) % TX_RING_SIZE));
		config_cmd_data = (void *)&sp->tx_ring[entry].tx_desc_addr;
		/* Construct a full CmdConfig frame. */
		memcpy(config_cmd_data, i82558_config_cmd, CONFIG_DATA_SIZE);
		config_cmd_data[1] = (txfifo << 4) | rxfifo;
		config_cmd_data[4] = rxdmacount;
		config_cmd_data[5] = txdmacount + 0x80;
		config_cmd_data[15] |= (new_rx_mode & 2) ? 1 : 0;
		/* 0x80 doesn't disable FC 0x84 does.
		   Disable Flow control since we are not ACK-ing any FC interrupts
		   for now. --Dragan */
		config_cmd_data[19] = 0x84;
		config_cmd_data[19] |= sp->mii_if.full_duplex ? 0x40 : 0;
		config_cmd_data[21] = (new_rx_mode & 1) ? 0x0D : 0x05;
		if (sp->phy[0] & 0x8000) {			/* Use the AUI port instead. */
			config_cmd_data[15] |= 0x80;
			config_cmd_data[8] = 0;
		}
		/* Trigger the command unit resume. */
		wait_for_cmd_done(dev, sp);
		clear_suspend(last_cmd);
		iowrite8(CUResume, ioaddr + SCBCmd);
		if ((int)(sp->cur_tx - sp->dirty_tx) >= TX_QUEUE_LIMIT) {
			netif_stop_queue(dev);
			sp->tx_full = 1;
		}
		spin_unlock_irqrestore(&sp->lock, flags);
	}

	if (new_rx_mode == 0  &&  dev->mc_count < 4) {
		/* The simple case of 0-3 multicast list entries occurs often, and
		   fits within one tx_ring[] entry. */
		struct dev_mc_list *mclist;
		u16 *setup_params, *eaddrs;

		spin_lock_irqsave(&sp->lock, flags);
		entry = sp->cur_tx++ % TX_RING_SIZE;
		last_cmd = sp->last_cmd;
		sp->last_cmd = (struct descriptor *)&sp->tx_ring[entry];

		sp->tx_skbuff[entry] = NULL;
		sp->tx_ring[entry].status = cpu_to_le32(CmdSuspend | CmdMulticastList);
		sp->tx_ring[entry].link =
			cpu_to_le32(TX_RING_ELEM_DMA(sp, (entry + 1) % TX_RING_SIZE));
		sp->tx_ring[entry].tx_desc_addr = 0; /* Really MC list count. */
		setup_params = (u16 *)&sp->tx_ring[entry].tx_desc_addr;
		*setup_params++ = cpu_to_le16(dev->mc_count*6);
		/* Fill in the multicast addresses. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
			 i++, mclist = mclist->next) {
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
		}

		wait_for_cmd_done(dev, sp);
		clear_suspend(last_cmd);
		/* Immediately trigger the command unit resume. */
		iowrite8(CUResume, ioaddr + SCBCmd);

		if ((int)(sp->cur_tx - sp->dirty_tx) >= TX_QUEUE_LIMIT) {
			netif_stop_queue(dev);
			sp->tx_full = 1;
		}
		spin_unlock_irqrestore(&sp->lock, flags);
	} else if (new_rx_mode == 0) {
		struct dev_mc_list *mclist;
		u16 *setup_params, *eaddrs;
		struct speedo_mc_block *mc_blk;
		struct descriptor *mc_setup_frm;
		int i;

		mc_blk = kmalloc(sizeof(*mc_blk) + 2 + multicast_filter_limit*6,
						 GFP_ATOMIC);
		if (mc_blk == NULL) {
			printk(KERN_ERR "%s: Failed to allocate a setup frame.\n",
				   dev->name);
			sp->rx_mode = -1; /* We failed, try again. */
			return;
		}
		mc_blk->next = NULL;
		mc_blk->len = 2 + multicast_filter_limit*6;
		mc_blk->frame_dma =
			pci_map_single(sp->pdev, &mc_blk->frame, mc_blk->len,
					PCI_DMA_TODEVICE);
		mc_setup_frm = &mc_blk->frame;

		/* Fill the setup frame. */
		if (netif_msg_ifup(sp))
			printk(KERN_DEBUG "%s: Constructing a setup frame at %p.\n",
				   dev->name, mc_setup_frm);
		mc_setup_frm->cmd_status =
			cpu_to_le32(CmdSuspend | CmdIntr | CmdMulticastList);
		/* Link set below. */
		setup_params = (u16 *)&mc_setup_frm->params;
		*setup_params++ = cpu_to_le16(dev->mc_count*6);
		/* Fill in the multicast addresses. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
			 i++, mclist = mclist->next) {
			eaddrs = (u16 *)mclist->dmi_addr;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
			*setup_params++ = *eaddrs++;
		}

		/* Disable interrupts while playing with the Tx Cmd list. */
		spin_lock_irqsave(&sp->lock, flags);

		if (sp->mc_setup_tail)
			sp->mc_setup_tail->next = mc_blk;
		else
			sp->mc_setup_head = mc_blk;
		sp->mc_setup_tail = mc_blk;
		mc_blk->tx = sp->cur_tx;

		entry = sp->cur_tx++ % TX_RING_SIZE;
		last_cmd = sp->last_cmd;
		sp->last_cmd = mc_setup_frm;

		/* Change the command to a NoOp, pointing to the CmdMulti command. */
		sp->tx_skbuff[entry] = NULL;
		sp->tx_ring[entry].status = cpu_to_le32(CmdNOp);
		sp->tx_ring[entry].link = cpu_to_le32(mc_blk->frame_dma);

		/* Set the link in the setup frame. */
		mc_setup_frm->link =
			cpu_to_le32(TX_RING_ELEM_DMA(sp, (entry + 1) % TX_RING_SIZE));

		pci_dma_sync_single_for_device(sp->pdev, mc_blk->frame_dma,
									   mc_blk->len, PCI_DMA_TODEVICE);

		wait_for_cmd_done(dev, sp);
		clear_suspend(last_cmd);
		/* Immediately trigger the command unit resume. */
		iowrite8(CUResume, ioaddr + SCBCmd);

		if ((int)(sp->cur_tx - sp->dirty_tx) >= TX_QUEUE_LIMIT) {
			netif_stop_queue(dev);
			sp->tx_full = 1;
		}
		spin_unlock_irqrestore(&sp->lock, flags);

		if (netif_msg_rx_status(sp))
			printk(" CmdMCSetup frame length %d in entry %d.\n",
				   dev->mc_count, entry);
	}

	sp->rx_mode = new_rx_mode;
}

#ifdef CONFIG_PM
static int eepro100_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;

	pci_save_state(pdev);

	if (!netif_running(dev))
		return 0;

	del_timer_sync(&sp->timer);

	netif_device_detach(dev);
	iowrite32(PortPartialReset, ioaddr + SCBPort);

	/* XXX call pci_set_power_state ()? */
	pci_disable_device(pdev);
	pci_set_power_state (pdev, PCI_D3hot);
	return 0;
}

static int eepro100_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct speedo_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->regs;
	int rc;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	pci_set_master(pdev);

	if (!netif_running(dev))
		return 0;

	/* I'm absolutely uncertain if this part of code may work.
	   The problems are:
	    - correct hardware reinitialization;
		- correct driver behavior between different steps of the
		  reinitialization;
		- serialization with other driver calls.
	   2000/03/08  SAW */
	iowrite16(SCBMaskAll, ioaddr + SCBCmd);
	speedo_resume(dev);
	netif_device_attach(dev);
	sp->rx_mode = -1;
	sp->flow_ctrl = sp->partner = 0;
	set_rx_mode(dev);
	sp->timer.expires = RUN_AT(2*HZ);
	add_timer(&sp->timer);
	return 0;
}
#endif /* CONFIG_PM */

static void __devexit eepro100_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct speedo_private *sp = netdev_priv(dev);

	unregister_netdev(dev);

	release_region(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1));
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));

	pci_iounmap(pdev, sp->regs);
	pci_free_consistent(pdev, TX_RING_SIZE * sizeof(struct TxFD)
								+ sizeof(struct speedo_stats),
						sp->tx_ring, sp->tx_ring_dma);
	pci_disable_device(pdev);
	free_netdev(dev);
}

static struct pci_device_id eepro100_pci_tbl[] = {
	{ PCI_VENDOR_ID_INTEL, 0x1229, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1209, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1029, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1030, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1031, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1032, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1033, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1034, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1035, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1036, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1037, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1038, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1039, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x103A, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x103B, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x103C, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x103D, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x103E, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1050, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1059, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x1227, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x2449, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x2459, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x245D, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x5200, PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_INTEL, 0x5201, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, eepro100_pci_tbl);

static struct pci_driver eepro100_driver = {
	.name		= "eepro100",
	.id_table	= eepro100_pci_tbl,
	.probe		= eepro100_init_one,
	.remove		= __devexit_p(eepro100_remove_one),
#ifdef CONFIG_PM
	.suspend	= eepro100_suspend,
	.resume		= eepro100_resume,
#endif /* CONFIG_PM */
};

static int __init eepro100_init_module(void)
{
#ifdef MODULE
	printk(version);
#endif
	return pci_register_driver(&eepro100_driver);
}

static void __exit eepro100_cleanup_module(void)
{
	pci_unregister_driver(&eepro100_driver);
}

module_init(eepro100_init_module);
module_exit(eepro100_cleanup_module);

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c eepro100.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */

/*
	Written 1997-1998 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	This driver is for the 3Com ISA EtherLink XL "Corkscrew" 3c515 ethercard.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403


	2000/2/2- Added support for kernel-level ISAPnP 
		by Stephen Frost <sfrost@snowman.net> and Alessandro Zummo
	Cleaned up for 2.3.x/softnet by Jeff Garzik and Alan Cox.
	
	2001/11/17 - Added ethtool support (jgarzik)
	
	2002/10/28 - Locking updates for 2.5 (alan@redhat.com)

*/

#define DRV_NAME		"3c515"
#define DRV_VERSION		"0.99t-ac"
#define DRV_RELDATE		"28-Oct-2002"

static char *version =
DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE " becker@scyld.com and others\n";

#define CORKSCREW 1

/* "Knobs" that adjust features and parameters. */
/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1512 effectively disables this feature. */
static int rx_copybreak = 200;

/* Allow setting MTU to a larger size, bypassing the normal ethernet setup. */
static const int mtu = 1500;

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Enable the automatic media selection code -- usually set. */
#define AUTOMEDIA 1

/* Allow the use of fragment bus master transfers instead of only
   programmed-I/O for Vortex cards.  Full-bus-master transfers are always
   enabled by default on Boomerang cards.  If VORTEX_BUS_MASTER is defined,
   the feature may be turned on using 'options'. */
#define VORTEX_BUS_MASTER

/* A few values that may be tweaked. */
/* Keep the ring sizes a power of two for efficiency. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	16
#define PKT_BUF_SZ		1536	/* Size of each temporary Rx buffer. */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/isapnp.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>

#define NEW_MULTICAST
#include <linux/delay.h>

#define MAX_UNITS 8

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("3Com 3c515 Corkscrew driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/* "Knobs" for adjusting internal parameters. */
/* Put out somewhat more debugging messages. (0 - no msg, 1 minimal msgs). */
#define DRIVER_DEBUG 1
/* Some values here only for performance evaluation and path-coverage
   debugging. */
static int rx_nocopy, rx_copy, queued_packet;

/* Number of times to check to see if the Tx FIFO has space, used in some
   limited cases. */
#define WAIT_TX_AVAIL 200

/* Operational parameter that usually are not changed. */
#define TX_TIMEOUT  40		/* Time in jiffies before concluding Tx hung */

/* The size here is somewhat misleading: the Corkscrew also uses the ISA
   aliased registers at <base>+0x400.
   */
#define CORKSCREW_TOTAL_SIZE 0x20

#ifdef DRIVER_DEBUG
static int corkscrew_debug = DRIVER_DEBUG;
#else
static int corkscrew_debug = 1;
#endif

#define CORKSCREW_ID 10

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the 3Com 3c515 ISA Fast EtherLink XL,
3Com's ISA bus adapter for Fast Ethernet.  Due to the unique I/O port layout,
it's not practical to integrate this driver with the other EtherLink drivers.

II. Board-specific settings

The Corkscrew has an EEPROM for configuration, but no special settings are
needed for Linux.

III. Driver operation

The 3c515 series use an interface that's very similar to the 3c900 "Boomerang"
PCI cards, with the bus master interface extensively modified to work with
the ISA bus.

The card is capable of full-bus-master transfers with separate
lists of transmit and receive descriptors, similar to the AMD LANCE/PCnet,
DEC Tulip and Intel Speedo3.

This driver uses a "RX_COPYBREAK" scheme rather than a fixed intermediate
receive buffer.  This scheme allocates full-sized skbuffs as receive
buffers.  The value RX_COPYBREAK is used as the copying breakpoint: it is
chosen to trade-off the memory wasted by passing the full-sized skbuff to
the queue layer for all frames vs. the copying cost of copying a frame to a
correctly-sized skbuff.


IIIC. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the netif
layer.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

IV. Notes

Thanks to Terry Murphy of 3Com for providing documentation and a development
board.

The names "Vortex", "Boomerang" and "Corkscrew" are the internal 3Com
project names.  I use these names to eliminate confusion -- 3Com product
numbers and names are very similar and often confused.

The new chips support both ethernet (1.5K) and FDDI (4.5K) frame sizes!
This driver only supports ethernet frames because of the recent MTU limit
of 1.5K, but the changes to support 4.5K are minimal.
*/

/* Operational definitions.
   These are not used by other compilation units and thus are not
   exported in a ".h" file.

   First the windows.  There are eight register windows, with the command
   and status registers available in each.
   */
#define EL3WINDOW(win_num) outw(SelectWindow + (win_num), ioaddr + EL3_CMD)
#define EL3_CMD 0x0e
#define EL3_STATUS 0x0e

/* The top five bits written to EL3_CMD are a command, the lower
   11 bits are the parameter, if applicable.
   Note that 11 parameters bits was fine for ethernet, but the new chips
   can handle FDDI length frames (~4500 octets) and now parameters count
   32-bit 'Dwords' rather than octets. */

enum corkscrew_cmd {
	TotalReset = 0 << 11, SelectWindow = 1 << 11, StartCoax = 2 << 11,
	RxDisable = 3 << 11, RxEnable = 4 << 11, RxReset = 5 << 11,
	UpStall = 6 << 11, UpUnstall = (6 << 11) + 1, DownStall = (6 << 11) + 2,
	DownUnstall = (6 << 11) + 3, RxDiscard = 8 << 11, TxEnable = 9 << 11, 
	TxDisable = 10 << 11, TxReset = 11 << 11, FakeIntr = 12 << 11, 
	AckIntr = 13 << 11, SetIntrEnb = 14 << 11, SetStatusEnb = 15 << 11, 
	SetRxFilter = 16 << 11, SetRxThreshold = 17 << 11,
	SetTxThreshold = 18 << 11, SetTxStart = 19 << 11, StartDMAUp = 20 << 11,
	StartDMADown = (20 << 11) + 1, StatsEnable = 21 << 11,
	StatsDisable = 22 << 11, StopCoax = 23 << 11,
};

/* The SetRxFilter command accepts the following classes: */
enum RxFilter {
	RxStation = 1, RxMulticast = 2, RxBroadcast = 4, RxProm = 8
};

/* Bits in the general status register. */
enum corkscrew_status {
	IntLatch = 0x0001, AdapterFailure = 0x0002, TxComplete = 0x0004,
	TxAvailable = 0x0008, RxComplete = 0x0010, RxEarly = 0x0020,
	IntReq = 0x0040, StatsFull = 0x0080,
	DMADone = 1 << 8, DownComplete = 1 << 9, UpComplete = 1 << 10,
	DMAInProgress = 1 << 11,	/* DMA controller is still busy. */
	CmdInProgress = 1 << 12,	/* EL3_CMD is still busy. */
};

/* Register window 1 offsets, the window used in normal operation.
   On the Corkscrew this window is always mapped at offsets 0x10-0x1f. */
enum Window1 {
	TX_FIFO = 0x10, RX_FIFO = 0x10, RxErrors = 0x14,
	RxStatus = 0x18, Timer = 0x1A, TxStatus = 0x1B,
	TxFree = 0x1C,		/* Remaining free bytes in Tx buffer. */
};
enum Window0 {
	Wn0IRQ = 0x08,
#if defined(CORKSCREW)
	Wn0EepromCmd = 0x200A,	/* Corkscrew EEPROM command register. */
	Wn0EepromData = 0x200C,	/* Corkscrew EEPROM results register. */
#else
	Wn0EepromCmd = 10,	/* Window 0: EEPROM command register. */
	Wn0EepromData = 12,	/* Window 0: EEPROM results register. */
#endif
};
enum Win0_EEPROM_bits {
	EEPROM_Read = 0x80, EEPROM_WRITE = 0x40, EEPROM_ERASE = 0xC0,
	EEPROM_EWENB = 0x30,	/* Enable erasing/writing for 10 msec. */
	EEPROM_EWDIS = 0x00,	/* Disable EWENB before 10 msec timeout. */
};

/* EEPROM locations. */
enum eeprom_offset {
	PhysAddr01 = 0, PhysAddr23 = 1, PhysAddr45 = 2, ModelID = 3,
	EtherLink3ID = 7,
};

enum Window3 {			/* Window 3: MAC/config bits. */
	Wn3_Config = 0, Wn3_MAC_Ctrl = 6, Wn3_Options = 8,
};
union wn3_config {
	int i;
	struct w3_config_fields {
		unsigned int ram_size:3, ram_width:1, ram_speed:2, rom_size:2;
		int pad8:8;
		unsigned int ram_split:2, pad18:2, xcvr:3, pad21:1, autoselect:1;
		int pad24:7;
	} u;
};

enum Window4 {
	Wn4_NetDiag = 6, Wn4_Media = 10,	/* Window 4: Xcvr/media bits. */
};
enum Win4_Media_bits {
	Media_SQE = 0x0008,	/* Enable SQE error counting for AUI. */
	Media_10TP = 0x00C0,	/* Enable link beat and jabber for 10baseT. */
	Media_Lnk = 0x0080,	/* Enable just link beat for 100TX/100FX. */
	Media_LnkBeat = 0x0800,
};
enum Window7 {			/* Window 7: Bus Master control. */
	Wn7_MasterAddr = 0, Wn7_MasterLen = 6, Wn7_MasterStatus = 12,
};

/* Boomerang-style bus master control registers.  Note ISA aliases! */
enum MasterCtrl {
	PktStatus = 0x400, DownListPtr = 0x404, FragAddr = 0x408, FragLen =
	    0x40c,
	TxFreeThreshold = 0x40f, UpPktStatus = 0x410, UpListPtr = 0x418,
};

/* The Rx and Tx descriptor lists.
   Caution Alpha hackers: these types are 32 bits!  Note also the 8 byte
   alignment contraint on tx_ring[] and rx_ring[]. */
struct boom_rx_desc {
	u32 next;
	s32 status;
	u32 addr;
	s32 length;
};

/* Values for the Rx status entry. */
enum rx_desc_status {
	RxDComplete = 0x00008000, RxDError = 0x4000,
	/* See boomerang_rx() for actual error bits */
};

struct boom_tx_desc {
	u32 next;
	s32 status;
	u32 addr;
	s32 length;
};

struct corkscrew_private {
	const char *product_name;
	struct list_head list;
	struct net_device *our_dev;
	/* The Rx and Tx rings are here to keep them quad-word-aligned. */
	struct boom_rx_desc rx_ring[RX_RING_SIZE];
	struct boom_tx_desc tx_ring[TX_RING_SIZE];
	/* The addresses of transmit- and receive-in-place skbuffs. */
	struct sk_buff *rx_skbuff[RX_RING_SIZE];
	struct sk_buff *tx_skbuff[TX_RING_SIZE];
	unsigned int cur_rx, cur_tx;	/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;/* The ring entries to be free()ed. */
	struct net_device_stats stats;
	struct sk_buff *tx_skb;	/* Packet being eaten by bus master ctrl.  */
	struct timer_list timer;	/* Media selection timer. */
	int capabilities	;	/* Adapter capabilities word. */
	int options;			/* User-settable misc. driver options. */
	int last_rx_packets;		/* For media autoselection. */
	unsigned int available_media:8,	/* From Wn3_Options */
		media_override:3,	/* Passed-in media type. */
		default_media:3,	/* Read from the EEPROM. */
		full_duplex:1, autoselect:1, bus_master:1,	/* Vortex can only do a fragment bus-m. */
		full_bus_master_tx:1, full_bus_master_rx:1,	/* Boomerang  */
		tx_full:1;
	spinlock_t lock;
	struct device *dev;
};

/* The action to take with a media selection timer tick.
   Note that we deviate from the 3Com order by checking 10base2 before AUI.
 */
enum xcvr_types {
	XCVR_10baseT = 0, XCVR_AUI, XCVR_10baseTOnly, XCVR_10base2, XCVR_100baseTx,
	XCVR_100baseFx, XCVR_MII = 6, XCVR_Default = 8,
};

static struct media_table {
	char *name;
	unsigned int media_bits:16,	/* Bits to set in Wn4_Media register. */
		mask:8,			/* The transceiver-present bit in Wn3_Config. */
		next:8;			/* The media type to try next. */
	short wait;			/* Time before we check media status. */
} media_tbl[] = {	
	{ "10baseT", Media_10TP, 0x08, XCVR_10base2, (14 * HZ) / 10 }, 
	{ "10Mbs AUI", Media_SQE, 0x20, XCVR_Default, (1 * HZ) / 10}, 
	{ "undefined", 0, 0x80, XCVR_10baseT, 10000}, 
	{ "10base2", 0, 0x10, XCVR_AUI, (1 * HZ) / 10}, 
	{ "100baseTX", Media_Lnk, 0x02, XCVR_100baseFx, (14 * HZ) / 10}, 
	{ "100baseFX", Media_Lnk, 0x04, XCVR_MII, (14 * HZ) / 10}, 
	{ "MII", 0, 0x40, XCVR_10baseT, 3 * HZ}, 
	{ "undefined", 0, 0x01, XCVR_10baseT, 10000}, 
	{ "Default", 0, 0xFF, XCVR_10baseT, 10000},
};

#ifdef __ISAPNP__
static struct isapnp_device_id corkscrew_isapnp_adapters[] = {
	{	ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('T', 'C', 'M'), ISAPNP_FUNCTION(0x5051),
		(long) "3Com Fast EtherLink ISA" },
	{ }	/* terminate list */
};

MODULE_DEVICE_TABLE(isapnp, corkscrew_isapnp_adapters);

static int nopnp;
#endif /* __ISAPNP__ */

static struct net_device *corkscrew_scan(int unit);
static int corkscrew_setup(struct net_device *dev, int ioaddr,
			    struct pnp_dev *idev, int card_number);
static int corkscrew_open(struct net_device *dev);
static void corkscrew_timer(unsigned long arg);
static int corkscrew_start_xmit(struct sk_buff *skb,
				struct net_device *dev);
static int corkscrew_rx(struct net_device *dev);
static void corkscrew_timeout(struct net_device *dev);
static int boomerang_rx(struct net_device *dev);
static irqreturn_t corkscrew_interrupt(int irq, void *dev_id,
				    struct pt_regs *regs);
static int corkscrew_close(struct net_device *dev);
static void update_stats(int addr, struct net_device *dev);
static struct net_device_stats *corkscrew_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static struct ethtool_ops netdev_ethtool_ops;


/* 
   Unfortunately maximizing the shared code between the integrated and
   module version of the driver results in a complicated set of initialization
   procedures.
   init_module() -- modules /  tc59x_init()  -- built-in
		The wrappers for corkscrew_scan()
   corkscrew_scan()  		 The common routine that scans for PCI and EISA cards
   corkscrew_found_device() Allocate a device structure when we find a card.
					Different versions exist for modules and built-in.
   corkscrew_probe1()		Fill in the device structure -- this is separated
					so that the modules code can put it in dev->init.
*/
/* This driver uses 'options' to pass the media type, full-duplex flag, etc. */
/* Note: this is the only limit on the number of cards supported!! */
static int options[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1, };

#ifdef MODULE
static int debug = -1;

module_param(debug, int, 0);
module_param_array(options, int, NULL, 0);
module_param(rx_copybreak, int, 0);
module_param(max_interrupt_work, int, 0);
MODULE_PARM_DESC(debug, "3c515 debug level (0-6)");
MODULE_PARM_DESC(options, "3c515: Bits 0-2: media type, bit 3: full duplex, bit 4: bus mastering");
MODULE_PARM_DESC(rx_copybreak, "3c515 copy breakpoint for copy-only-tiny-frames");
MODULE_PARM_DESC(max_interrupt_work, "3c515 maximum events handled per interrupt");

/* A list of all installed Vortex devices, for removing the driver module. */
/* we will need locking (and refcounting) if we ever use it for more */
static LIST_HEAD(root_corkscrew_dev);

int init_module(void)
{
	int found = 0;
	if (debug >= 0)
		corkscrew_debug = debug;
	if (corkscrew_debug)
		printk(version);
	while (corkscrew_scan(-1))
		found++;
	return found ? 0 : -ENODEV;
}

#else
struct net_device *tc515_probe(int unit)
{
	struct net_device *dev = corkscrew_scan(unit);
	static int printed;

	if (!dev)
		return ERR_PTR(-ENODEV);

	if (corkscrew_debug > 0 && !printed) {
		printed = 1;
		printk(version);
	}

	return dev;
}
#endif				/* not MODULE */

static int check_device(unsigned ioaddr)
{
	int timer;

	if (!request_region(ioaddr, CORKSCREW_TOTAL_SIZE, "3c515"))
		return 0;
	/* Check the resource configuration for a matching ioaddr. */
	if ((inw(ioaddr + 0x2002) & 0x1f0) != (ioaddr & 0x1f0)) {
		release_region(ioaddr, CORKSCREW_TOTAL_SIZE);
		return 0;
	}
	/* Verify by reading the device ID from the EEPROM. */
	outw(EEPROM_Read + 7, ioaddr + Wn0EepromCmd);
	/* Pause for at least 162 us. for the read to take place. */
	for (timer = 4; timer >= 0; timer--) {
		udelay(162);
		if ((inw(ioaddr + Wn0EepromCmd) & 0x0200) == 0)
			break;
	}
	if (inw(ioaddr + Wn0EepromData) != 0x6d50) {
		release_region(ioaddr, CORKSCREW_TOTAL_SIZE);
		return 0;
	}
	return 1;
}

static void cleanup_card(struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	list_del_init(&vp->list);
	if (dev->dma)
		free_dma(dev->dma);
	outw(TotalReset, dev->base_addr + EL3_CMD);
	release_region(dev->base_addr, CORKSCREW_TOTAL_SIZE);
	if (vp->dev)
		pnp_device_detach(to_pnp_dev(vp->dev));
}

static struct net_device *corkscrew_scan(int unit)
{
	struct net_device *dev;
	static int cards_found = 0;
	static int ioaddr;
	int err;
#ifdef __ISAPNP__
	short i;
	static int pnp_cards;
#endif

	dev = alloc_etherdev(sizeof(struct corkscrew_private));
	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	SET_MODULE_OWNER(dev);

#ifdef __ISAPNP__
	if(nopnp == 1)
		goto no_pnp;
	for(i=0; corkscrew_isapnp_adapters[i].vendor != 0; i++) {
		struct pnp_dev *idev = NULL;
		int irq;
		while((idev = pnp_find_dev(NULL,
					   corkscrew_isapnp_adapters[i].vendor,
					   corkscrew_isapnp_adapters[i].function,
					   idev))) {

			if (pnp_device_attach(idev) < 0)
				continue;
			if (pnp_activate_dev(idev) < 0) {
				printk("pnp activate failed (out of resources?)\n");
				pnp_device_detach(idev);
				continue;
			}
			if (!pnp_port_valid(idev, 0) || !pnp_irq_valid(idev, 0)) {
				pnp_device_detach(idev);
				continue;
			}
			ioaddr = pnp_port_start(idev, 0);
			irq = pnp_irq(idev, 0);
			if (!check_device(ioaddr)) {
				pnp_device_detach(idev);
				continue;
			}
			if(corkscrew_debug)
				printk ("ISAPNP reports %s at i/o 0x%x, irq %d\n",
					(char*) corkscrew_isapnp_adapters[i].driver_data, ioaddr, irq);
			printk(KERN_INFO "3c515 Resource configuration register %#4.4x, DCR %4.4x.\n",
		     		inl(ioaddr + 0x2002), inw(ioaddr + 0x2000));
			/* irq = inw(ioaddr + 0x2002) & 15; */ /* Use the irq from isapnp */
			SET_NETDEV_DEV(dev, &idev->dev);
			pnp_cards++;
			err = corkscrew_setup(dev, ioaddr, idev, cards_found++);
			if (!err)
				return dev;
			cleanup_card(dev);
		}
	}
no_pnp:
#endif /* __ISAPNP__ */

	/* Check all locations on the ISA bus -- evil! */
	for (ioaddr = 0x100; ioaddr < 0x400; ioaddr += 0x20) {
		if (!check_device(ioaddr))
			continue;

		printk(KERN_INFO "3c515 Resource configuration register %#4.4x, DCR %4.4x.\n",
		     inl(ioaddr + 0x2002), inw(ioaddr + 0x2000));
		err = corkscrew_setup(dev, ioaddr, NULL, cards_found++);
		if (!err)
			return dev;
		cleanup_card(dev);
	}
	free_netdev(dev);
	return NULL;
}

static int corkscrew_setup(struct net_device *dev, int ioaddr,
			    struct pnp_dev *idev, int card_number)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	unsigned int eeprom[0x40], checksum = 0;	/* EEPROM contents */
	int i;
	int irq;

	if (idev) {
		irq = pnp_irq(idev, 0);
		vp->dev = &idev->dev;
	} else {
		irq = inw(ioaddr + 0x2002) & 15;
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->dma = inw(ioaddr + 0x2000) & 7;
	vp->product_name = "3c515";
	vp->options = dev->mem_start;
	vp->our_dev = dev;

	if (!vp->options) {
		 if (card_number >= MAX_UNITS)
			vp->options = -1;
		else
			vp->options = options[card_number];
	}

	if (vp->options >= 0) {
		vp->media_override = vp->options & 7;
		if (vp->media_override == 2)
			vp->media_override = 0;
		vp->full_duplex = (vp->options & 8) ? 1 : 0;
		vp->bus_master = (vp->options & 16) ? 1 : 0;
	} else {
		vp->media_override = 7;
		vp->full_duplex = 0;
		vp->bus_master = 0;
	}
#ifdef MODULE
	list_add(&vp->list, &root_corkscrew_dev);
#endif

	printk(KERN_INFO "%s: 3Com %s at %#3x,", dev->name, vp->product_name, ioaddr);

	spin_lock_init(&vp->lock);
	
	/* Read the station address from the EEPROM. */
	EL3WINDOW(0);
	for (i = 0; i < 0x18; i++) {
		short *phys_addr = (short *) dev->dev_addr;
		int timer;
		outw(EEPROM_Read + i, ioaddr + Wn0EepromCmd);
		/* Pause for at least 162 us. for the read to take place. */
		for (timer = 4; timer >= 0; timer--) {
			udelay(162);
			if ((inw(ioaddr + Wn0EepromCmd) & 0x0200) == 0)
				break;
		}
		eeprom[i] = inw(ioaddr + Wn0EepromData);
		checksum ^= eeprom[i];
		if (i < 3)
			phys_addr[i] = htons(eeprom[i]);
	}
	checksum = (checksum ^ (checksum >> 8)) & 0xff;
	if (checksum != 0x00)
		printk(" ***INVALID CHECKSUM %4.4x*** ", checksum);
	for (i = 0; i < 6; i++)
		printk("%c%2.2x", i ? ':' : ' ', dev->dev_addr[i]);
	if (eeprom[16] == 0x11c7) {	/* Corkscrew */
		if (request_dma(dev->dma, "3c515")) {
			printk(", DMA %d allocation failed", dev->dma);
			dev->dma = 0;
		} else
			printk(", DMA %d", dev->dma);
	}
	printk(", IRQ %d\n", dev->irq);
	/* Tell them about an invalid IRQ. */
	if (corkscrew_debug && (dev->irq <= 0 || dev->irq > 15))
		printk(KERN_WARNING " *** Warning: this IRQ is unlikely to work! ***\n");

	{
		char *ram_split[] = { "5:3", "3:1", "1:1", "3:5" };
		union wn3_config config;
		EL3WINDOW(3);
		vp->available_media = inw(ioaddr + Wn3_Options);
		config.i = inl(ioaddr + Wn3_Config);
		if (corkscrew_debug > 1)
			printk(KERN_INFO "  Internal config register is %4.4x, transceivers %#x.\n",
				config.i, inw(ioaddr + Wn3_Options));
		printk(KERN_INFO "  %dK %s-wide RAM %s Rx:Tx split, %s%s interface.\n",
			8 << config.u.ram_size,
			config.u.ram_width ? "word" : "byte",
			ram_split[config.u.ram_split],
			config.u.autoselect ? "autoselect/" : "",
			media_tbl[config.u.xcvr].name);
		dev->if_port = config.u.xcvr;
		vp->default_media = config.u.xcvr;
		vp->autoselect = config.u.autoselect;
	}
	if (vp->media_override != 7) {
		printk(KERN_INFO "  Media override to transceiver type %d (%s).\n",
		       vp->media_override,
		       media_tbl[vp->media_override].name);
		dev->if_port = vp->media_override;
	}

	vp->capabilities = eeprom[16];
	vp->full_bus_master_tx = (vp->capabilities & 0x20) ? 1 : 0;
	/* Rx is broken at 10mbps, so we always disable it. */
	/* vp->full_bus_master_rx = 0; */
	vp->full_bus_master_rx = (vp->capabilities & 0x20) ? 1 : 0;

	/* The 3c51x-specific entries in the device structure. */
	dev->open = &corkscrew_open;
	dev->hard_start_xmit = &corkscrew_start_xmit;
	dev->tx_timeout = &corkscrew_timeout;
	dev->watchdog_timeo = (400 * HZ) / 1000;
	dev->stop = &corkscrew_close;
	dev->get_stats = &corkscrew_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->ethtool_ops = &netdev_ethtool_ops;

	return register_netdev(dev);
}


static int corkscrew_open(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct corkscrew_private *vp = netdev_priv(dev);
	union wn3_config config;
	int i;

	/* Before initializing select the active media port. */
	EL3WINDOW(3);
	if (vp->full_duplex)
		outb(0x20, ioaddr + Wn3_MAC_Ctrl);	/* Set the full-duplex bit. */
	config.i = inl(ioaddr + Wn3_Config);

	if (vp->media_override != 7) {
		if (corkscrew_debug > 1)
			printk(KERN_INFO "%s: Media override to transceiver %d (%s).\n",
				dev->name, vp->media_override,
				media_tbl[vp->media_override].name);
		dev->if_port = vp->media_override;
	} else if (vp->autoselect) {
		/* Find first available media type, starting with 100baseTx. */
		dev->if_port = 4;
		while (!(vp->available_media & media_tbl[dev->if_port].mask)) 
			dev->if_port = media_tbl[dev->if_port].next;

		if (corkscrew_debug > 1)
			printk("%s: Initial media type %s.\n",
			       dev->name, media_tbl[dev->if_port].name);

		init_timer(&vp->timer);
		vp->timer.expires = jiffies + media_tbl[dev->if_port].wait;
		vp->timer.data = (unsigned long) dev;
		vp->timer.function = &corkscrew_timer;	/* timer handler */
		add_timer(&vp->timer);
	} else
		dev->if_port = vp->default_media;

	config.u.xcvr = dev->if_port;
	outl(config.i, ioaddr + Wn3_Config);

	if (corkscrew_debug > 1) {
		printk("%s: corkscrew_open() InternalConfig %8.8x.\n",
		       dev->name, config.i);
	}

	outw(TxReset, ioaddr + EL3_CMD);
	for (i = 20; i >= 0; i--)
		if (!(inw(ioaddr + EL3_STATUS) & CmdInProgress))
			break;

	outw(RxReset, ioaddr + EL3_CMD);
	/* Wait a few ticks for the RxReset command to complete. */
	for (i = 20; i >= 0; i--)
		if (!(inw(ioaddr + EL3_STATUS) & CmdInProgress))
			break;

	outw(SetStatusEnb | 0x00, ioaddr + EL3_CMD);

	/* Use the now-standard shared IRQ implementation. */
	if (vp->capabilities == 0x11c7) {
		/* Corkscrew: Cannot share ISA resources. */
		if (dev->irq == 0
		    || dev->dma == 0
		    || request_irq(dev->irq, &corkscrew_interrupt, 0,
				   vp->product_name, dev)) return -EAGAIN;
		enable_dma(dev->dma);
		set_dma_mode(dev->dma, DMA_MODE_CASCADE);
	} else if (request_irq(dev->irq, &corkscrew_interrupt, SA_SHIRQ,
			       vp->product_name, dev)) {
		return -EAGAIN;
	}

	if (corkscrew_debug > 1) {
		EL3WINDOW(4);
		printk("%s: corkscrew_open() irq %d media status %4.4x.\n",
		       dev->name, dev->irq, inw(ioaddr + Wn4_Media));
	}

	/* Set the station address and mask in window 2 each time opened. */
	EL3WINDOW(2);
	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + i);
	for (; i < 12; i += 2)
		outw(0, ioaddr + i);

	if (dev->if_port == 3)
		/* Start the thinnet transceiver. We should really wait 50ms... */
		outw(StartCoax, ioaddr + EL3_CMD);
	EL3WINDOW(4);
	outw((inw(ioaddr + Wn4_Media) & ~(Media_10TP | Media_SQE)) |
	     media_tbl[dev->if_port].media_bits, ioaddr + Wn4_Media);

	/* Switch to the stats window, and clear all stats by reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	EL3WINDOW(6);
	for (i = 0; i < 10; i++)
		inb(ioaddr + i);
	inw(ioaddr + 10);
	inw(ioaddr + 12);
	/* New: On the Vortex we must also clear the BadSSD counter. */
	EL3WINDOW(4);
	inb(ioaddr + 12);
	/* ..and on the Boomerang we enable the extra statistics bits. */
	outw(0x0040, ioaddr + Wn4_NetDiag);

	/* Switch to register set 7 for normal use. */
	EL3WINDOW(7);

	if (vp->full_bus_master_rx) {	/* Boomerang bus master. */
		vp->cur_rx = vp->dirty_rx = 0;
		if (corkscrew_debug > 2)
			printk("%s:  Filling in the Rx ring.\n",
			       dev->name);
		for (i = 0; i < RX_RING_SIZE; i++) {
			struct sk_buff *skb;
			if (i < (RX_RING_SIZE - 1))
				vp->rx_ring[i].next =
				    isa_virt_to_bus(&vp->rx_ring[i + 1]);
			else
				vp->rx_ring[i].next = 0;
			vp->rx_ring[i].status = 0;	/* Clear complete bit. */
			vp->rx_ring[i].length = PKT_BUF_SZ | 0x80000000;
			skb = dev_alloc_skb(PKT_BUF_SZ);
			vp->rx_skbuff[i] = skb;
			if (skb == NULL)
				break;	/* Bad news!  */
			skb->dev = dev;	/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			vp->rx_ring[i].addr = isa_virt_to_bus(skb->data);
		}
		vp->rx_ring[i - 1].next = isa_virt_to_bus(&vp->rx_ring[0]);	/* Wrap the ring. */
		outl(isa_virt_to_bus(&vp->rx_ring[0]), ioaddr + UpListPtr);
	}
	if (vp->full_bus_master_tx) {	/* Boomerang bus master Tx. */
		vp->cur_tx = vp->dirty_tx = 0;
		outb(PKT_BUF_SZ >> 8, ioaddr + TxFreeThreshold);	/* Room for a packet. */
		/* Clear the Tx ring. */
		for (i = 0; i < TX_RING_SIZE; i++)
			vp->tx_skbuff[i] = NULL;
		outl(0, ioaddr + DownListPtr);
	}
	/* Set receiver mode: presumably accept b-case and phys addr only. */
	set_rx_mode(dev);
	outw(StatsEnable, ioaddr + EL3_CMD);	/* Turn on statistics. */

	netif_start_queue(dev);

	outw(RxEnable, ioaddr + EL3_CMD);	/* Enable the receiver. */
	outw(TxEnable, ioaddr + EL3_CMD);	/* Enable transmitter. */
	/* Allow status bits to be seen. */
	outw(SetStatusEnb | AdapterFailure | IntReq | StatsFull |
	     (vp->full_bus_master_tx ? DownComplete : TxAvailable) |
	     (vp->full_bus_master_rx ? UpComplete : RxComplete) |
	     (vp->bus_master ? DMADone : 0), ioaddr + EL3_CMD);
	/* Ack all pending events, and set active indicator mask. */
	outw(AckIntr | IntLatch | TxAvailable | RxEarly | IntReq,
	     ioaddr + EL3_CMD);
	outw(SetIntrEnb | IntLatch | TxAvailable | RxComplete | StatsFull
	     | (vp->bus_master ? DMADone : 0) | UpComplete | DownComplete,
	     ioaddr + EL3_CMD);

	return 0;
}

static void corkscrew_timer(unsigned long data)
{
#ifdef AUTOMEDIA
	struct net_device *dev = (struct net_device *) data;
	struct corkscrew_private *vp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	unsigned long flags;
	int ok = 0;

	if (corkscrew_debug > 1)
		printk("%s: Media selection timer tick happened, %s.\n",
		       dev->name, media_tbl[dev->if_port].name);

	spin_lock_irqsave(&vp->lock, flags);
	
	{
		int old_window = inw(ioaddr + EL3_CMD) >> 13;
		int media_status;
		EL3WINDOW(4);
		media_status = inw(ioaddr + Wn4_Media);
		switch (dev->if_port) {
		case 0:
		case 4:
		case 5:	/* 10baseT, 100baseTX, 100baseFX  */
			if (media_status & Media_LnkBeat) {
				ok = 1;
				if (corkscrew_debug > 1)
					printk("%s: Media %s has link beat, %x.\n",
						dev->name,
						media_tbl[dev->if_port].name,
						media_status);
			} else if (corkscrew_debug > 1)
				printk("%s: Media %s is has no link beat, %x.\n",
					dev->name,
					media_tbl[dev->if_port].name,
					media_status);

			break;
		default:	/* Other media types handled by Tx timeouts. */
			if (corkscrew_debug > 1)
				printk("%s: Media %s is has no indication, %x.\n",
					dev->name,
					media_tbl[dev->if_port].name,
					media_status);
			ok = 1;
		}
		if (!ok) {
			union wn3_config config;

			do {
				dev->if_port =
				    media_tbl[dev->if_port].next;
			}
			while (!(vp->available_media & media_tbl[dev->if_port].mask));
			
			if (dev->if_port == 8) {	/* Go back to default. */
				dev->if_port = vp->default_media;
				if (corkscrew_debug > 1)
					printk("%s: Media selection failing, using default %s port.\n",
						dev->name,
						media_tbl[dev->if_port].name);
			} else {
				if (corkscrew_debug > 1)
					printk("%s: Media selection failed, now trying %s port.\n",
						dev->name,
						media_tbl[dev->if_port].name);
				vp->timer.expires = jiffies + media_tbl[dev->if_port].wait;
				add_timer(&vp->timer);
			}
			outw((media_status & ~(Media_10TP | Media_SQE)) |
			     media_tbl[dev->if_port].media_bits,
			     ioaddr + Wn4_Media);

			EL3WINDOW(3);
			config.i = inl(ioaddr + Wn3_Config);
			config.u.xcvr = dev->if_port;
			outl(config.i, ioaddr + Wn3_Config);

			outw(dev->if_port == 3 ? StartCoax : StopCoax,
			     ioaddr + EL3_CMD);
		}
		EL3WINDOW(old_window);
	}
	
	spin_unlock_irqrestore(&vp->lock, flags);
	if (corkscrew_debug > 1)
		printk("%s: Media selection timer finished, %s.\n",
		       dev->name, media_tbl[dev->if_port].name);

#endif				/* AUTOMEDIA */
	return;
}

static void corkscrew_timeout(struct net_device *dev)
{
	int i;
	struct corkscrew_private *vp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	printk(KERN_WARNING
	       "%s: transmit timed out, tx_status %2.2x status %4.4x.\n",
	       dev->name, inb(ioaddr + TxStatus),
	       inw(ioaddr + EL3_STATUS));
	/* Slight code bloat to be user friendly. */
	if ((inb(ioaddr + TxStatus) & 0x88) == 0x88)
		printk(KERN_WARNING
		       "%s: Transmitter encountered 16 collisions -- network"
		       " network cable problem?\n", dev->name);
#ifndef final_version
	printk("  Flags; bus-master %d, full %d; dirty %d current %d.\n",
	       vp->full_bus_master_tx, vp->tx_full, vp->dirty_tx,
	       vp->cur_tx);
	printk("  Down list %8.8x vs. %p.\n", inl(ioaddr + DownListPtr),
	       &vp->tx_ring[0]);
	for (i = 0; i < TX_RING_SIZE; i++) {
		printk("  %d: %p  length %8.8x status %8.8x\n", i,
		       &vp->tx_ring[i],
		       vp->tx_ring[i].length, vp->tx_ring[i].status);
	}
#endif
	/* Issue TX_RESET and TX_START commands. */
	outw(TxReset, ioaddr + EL3_CMD);
	for (i = 20; i >= 0; i--)
		if (!(inw(ioaddr + EL3_STATUS) & CmdInProgress))
			break;
	outw(TxEnable, ioaddr + EL3_CMD);
	dev->trans_start = jiffies;
	vp->stats.tx_errors++;
	vp->stats.tx_dropped++;
	netif_wake_queue(dev);
}

static int corkscrew_start_xmit(struct sk_buff *skb,
				struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	/* Block a timer-based transmit from overlapping. */

	netif_stop_queue(dev);

	if (vp->full_bus_master_tx) {	/* BOOMERANG bus-master */
		/* Calculate the next Tx descriptor entry. */
		int entry = vp->cur_tx % TX_RING_SIZE;
		struct boom_tx_desc *prev_entry;
		unsigned long flags, i;

		if (vp->tx_full)	/* No room to transmit with */
			return 1;
		if (vp->cur_tx != 0)
			prev_entry = &vp->tx_ring[(vp->cur_tx - 1) % TX_RING_SIZE];
		else
			prev_entry = NULL;
		if (corkscrew_debug > 3)
			printk("%s: Trying to send a packet, Tx index %d.\n",
				dev->name, vp->cur_tx);
		/* vp->tx_full = 1; */
		vp->tx_skbuff[entry] = skb;
		vp->tx_ring[entry].next = 0;
		vp->tx_ring[entry].addr = isa_virt_to_bus(skb->data);
		vp->tx_ring[entry].length = skb->len | 0x80000000;
		vp->tx_ring[entry].status = skb->len | 0x80000000;

		spin_lock_irqsave(&vp->lock, flags);
		outw(DownStall, ioaddr + EL3_CMD);
		/* Wait for the stall to complete. */
		for (i = 20; i >= 0; i--)
			if ((inw(ioaddr + EL3_STATUS) & CmdInProgress) == 0) 
				break;
		if (prev_entry)
			prev_entry->next = isa_virt_to_bus(&vp->tx_ring[entry]);
		if (inl(ioaddr + DownListPtr) == 0) {
			outl(isa_virt_to_bus(&vp->tx_ring[entry]),
			     ioaddr + DownListPtr);
			queued_packet++;
		}
		outw(DownUnstall, ioaddr + EL3_CMD);
		spin_unlock_irqrestore(&vp->lock, flags);

		vp->cur_tx++;
		if (vp->cur_tx - vp->dirty_tx > TX_RING_SIZE - 1)
			vp->tx_full = 1;
		else {		/* Clear previous interrupt enable. */
			if (prev_entry)
				prev_entry->status &= ~0x80000000;
			netif_wake_queue(dev);
		}
		dev->trans_start = jiffies;
		return 0;
	}
	/* Put out the doubleword header... */
	outl(skb->len, ioaddr + TX_FIFO);
	vp->stats.tx_bytes += skb->len;
#ifdef VORTEX_BUS_MASTER
	if (vp->bus_master) {
		/* Set the bus-master controller to transfer the packet. */
		outl((int) (skb->data), ioaddr + Wn7_MasterAddr);
		outw((skb->len + 3) & ~3, ioaddr + Wn7_MasterLen);
		vp->tx_skb = skb;
		outw(StartDMADown, ioaddr + EL3_CMD);
		/* queue will be woken at the DMADone interrupt. */
	} else {
		/* ... and the packet rounded to a doubleword. */
		outsl(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);
		dev_kfree_skb(skb);
		if (inw(ioaddr + TxFree) > 1536) {
			netif_wake_queue(dev);
		} else
			/* Interrupt us when the FIFO has room for max-sized packet. */
			outw(SetTxThreshold + (1536 >> 2),
			     ioaddr + EL3_CMD);
	}
#else
	/* ... and the packet rounded to a doubleword. */
	outsl(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);
	dev_kfree_skb(skb);
	if (inw(ioaddr + TxFree) > 1536) {
		netif_wake_queue(dev);
	} else
		/* Interrupt us when the FIFO has room for max-sized packet. */
		outw(SetTxThreshold + (1536 >> 2), ioaddr + EL3_CMD);
#endif				/* bus master */

	dev->trans_start = jiffies;

	/* Clear the Tx status stack. */
	{
		short tx_status;
		int i = 4;

		while (--i > 0 && (tx_status = inb(ioaddr + TxStatus)) > 0) {
			if (tx_status & 0x3C) {	/* A Tx-disabling error occurred.  */
				if (corkscrew_debug > 2)
					printk("%s: Tx error, status %2.2x.\n",
						dev->name, tx_status);
				if (tx_status & 0x04)
					vp->stats.tx_fifo_errors++;
				if (tx_status & 0x38)
					vp->stats.tx_aborted_errors++;
				if (tx_status & 0x30) {
					int j;
					outw(TxReset, ioaddr + EL3_CMD);
					for (j = 20; j >= 0; j--)
						if (!(inw(ioaddr + EL3_STATUS) & CmdInProgress)) 
							break;
				}
				outw(TxEnable, ioaddr + EL3_CMD);
			}
			outb(0x00, ioaddr + TxStatus);	/* Pop the status stack. */
		}
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */

static irqreturn_t corkscrew_interrupt(int irq, void *dev_id,
				    struct pt_regs *regs)
{
	/* Use the now-standard shared IRQ implementation. */
	struct net_device *dev = dev_id;
	struct corkscrew_private *lp = netdev_priv(dev);
	int ioaddr, status;
	int latency;
	int i = max_interrupt_work;

	ioaddr = dev->base_addr;
	latency = inb(ioaddr + Timer);

	spin_lock(&lp->lock);
	
	status = inw(ioaddr + EL3_STATUS);

	if (corkscrew_debug > 4)
		printk("%s: interrupt, status %4.4x, timer %d.\n",
			dev->name, status, latency);
	if ((status & 0xE000) != 0xE000) {
		static int donedidthis;
		/* Some interrupt controllers store a bogus interrupt from boot-time.
		   Ignore a single early interrupt, but don't hang the machine for
		   other interrupt problems. */
		if (donedidthis++ > 100) {
			printk(KERN_ERR "%s: Bogus interrupt, bailing. Status %4.4x, start=%d.\n",
				   dev->name, status, netif_running(dev));
			free_irq(dev->irq, dev);
			dev->irq = -1;
		}
	}

	do {
		if (corkscrew_debug > 5)
			printk("%s: In interrupt loop, status %4.4x.\n",
			       dev->name, status);
		if (status & RxComplete)
			corkscrew_rx(dev);

		if (status & TxAvailable) {
			if (corkscrew_debug > 5)
				printk("	TX room bit was handled.\n");
			/* There's room in the FIFO for a full-sized packet. */
			outw(AckIntr | TxAvailable, ioaddr + EL3_CMD);
			netif_wake_queue(dev);
		}
		if (status & DownComplete) {
			unsigned int dirty_tx = lp->dirty_tx;

			while (lp->cur_tx - dirty_tx > 0) {
				int entry = dirty_tx % TX_RING_SIZE;
				if (inl(ioaddr + DownListPtr) == isa_virt_to_bus(&lp->tx_ring[entry]))
					break;	/* It still hasn't been processed. */
				if (lp->tx_skbuff[entry]) {
					dev_kfree_skb_irq(lp->tx_skbuff[entry]);
					lp->tx_skbuff[entry] = NULL;
				}
				dirty_tx++;
			}
			lp->dirty_tx = dirty_tx;
			outw(AckIntr | DownComplete, ioaddr + EL3_CMD);
			if (lp->tx_full && (lp->cur_tx - dirty_tx <= TX_RING_SIZE - 1)) {
				lp->tx_full = 0;
				netif_wake_queue(dev);
			}
		}
#ifdef VORTEX_BUS_MASTER
		if (status & DMADone) {
			outw(0x1000, ioaddr + Wn7_MasterStatus);	/* Ack the event. */
			dev_kfree_skb_irq(lp->tx_skb);	/* Release the transferred buffer */
			netif_wake_queue(dev);
		}
#endif
		if (status & UpComplete) {
			boomerang_rx(dev);
			outw(AckIntr | UpComplete, ioaddr + EL3_CMD);
		}
		if (status & (AdapterFailure | RxEarly | StatsFull)) {
			/* Handle all uncommon interrupts at once. */
			if (status & RxEarly) {	/* Rx early is unused. */
				corkscrew_rx(dev);
				outw(AckIntr | RxEarly, ioaddr + EL3_CMD);
			}
			if (status & StatsFull) {	/* Empty statistics. */
				static int DoneDidThat;
				if (corkscrew_debug > 4)
					printk("%s: Updating stats.\n", dev->name);
				update_stats(ioaddr, dev);
				/* DEBUG HACK: Disable statistics as an interrupt source. */
				/* This occurs when we have the wrong media type! */
				if (DoneDidThat == 0 && inw(ioaddr + EL3_STATUS) & StatsFull) {
					int win, reg;
					printk("%s: Updating stats failed, disabling stats as an"
					     " interrupt source.\n", dev->name);
					for (win = 0; win < 8; win++) {
						EL3WINDOW(win);
						printk("\n Vortex window %d:", win);
						for (reg = 0; reg < 16; reg++)
							printk(" %2.2x", inb(ioaddr + reg));
					}
					EL3WINDOW(7);
					outw(SetIntrEnb | TxAvailable |
					     RxComplete | AdapterFailure |
					     UpComplete | DownComplete |
					     TxComplete, ioaddr + EL3_CMD);
					DoneDidThat++;
				}
			}
			if (status & AdapterFailure) {
				/* Adapter failure requires Rx reset and reinit. */
				outw(RxReset, ioaddr + EL3_CMD);
				/* Set the Rx filter to the current state. */
				set_rx_mode(dev);
				outw(RxEnable, ioaddr + EL3_CMD);	/* Re-enable the receiver. */
				outw(AckIntr | AdapterFailure,
				     ioaddr + EL3_CMD);
			}
		}

		if (--i < 0) {
			printk(KERN_ERR "%s: Too much work in interrupt, status %4.4x.  "
			     "Disabling functions (%4.4x).\n", dev->name,
			     status, SetStatusEnb | ((~status) & 0x7FE));
			/* Disable all pending interrupts. */
			outw(SetStatusEnb | ((~status) & 0x7FE), ioaddr + EL3_CMD);
			outw(AckIntr | 0x7FF, ioaddr + EL3_CMD);
			break;
		}
		/* Acknowledge the IRQ. */
		outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD);

	} while ((status = inw(ioaddr + EL3_STATUS)) & (IntLatch | RxComplete));
	
	spin_unlock(&lp->lock);

	if (corkscrew_debug > 4)
		printk("%s: exiting interrupt, status %4.4x.\n", dev->name, status);
	return IRQ_HANDLED;
}

static int corkscrew_rx(struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int i;
	short rx_status;

	if (corkscrew_debug > 5)
		printk("   In rx_packet(), status %4.4x, rx_status %4.4x.\n",
		     inw(ioaddr + EL3_STATUS), inw(ioaddr + RxStatus));
	while ((rx_status = inw(ioaddr + RxStatus)) > 0) {
		if (rx_status & 0x4000) {	/* Error, update stats. */
			unsigned char rx_error = inb(ioaddr + RxErrors);
			if (corkscrew_debug > 2)
				printk(" Rx error: status %2.2x.\n",
				       rx_error);
			vp->stats.rx_errors++;
			if (rx_error & 0x01)
				vp->stats.rx_over_errors++;
			if (rx_error & 0x02)
				vp->stats.rx_length_errors++;
			if (rx_error & 0x04)
				vp->stats.rx_frame_errors++;
			if (rx_error & 0x08)
				vp->stats.rx_crc_errors++;
			if (rx_error & 0x10)
				vp->stats.rx_length_errors++;
		} else {
			/* The packet length: up to 4.5K!. */
			short pkt_len = rx_status & 0x1fff;
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len + 5 + 2);
			if (corkscrew_debug > 4)
				printk("Receiving packet size %d status %4.4x.\n",
				     pkt_len, rx_status);
			if (skb != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
				insl(ioaddr + RX_FIFO,
				     skb_put(skb, pkt_len),
				     (pkt_len + 3) >> 2);
				outw(RxDiscard, ioaddr + EL3_CMD);	/* Pop top Rx packet. */
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				dev->last_rx = jiffies;
				vp->stats.rx_packets++;
				vp->stats.rx_bytes += pkt_len;
				/* Wait a limited time to go to next packet. */
				for (i = 200; i >= 0; i--)
					if (! (inw(ioaddr + EL3_STATUS) & CmdInProgress)) 
						break;
				continue;
			} else if (corkscrew_debug)
				printk("%s: Couldn't allocate a sk_buff of size %d.\n", dev->name, pkt_len);
		}
		outw(RxDiscard, ioaddr + EL3_CMD);
		vp->stats.rx_dropped++;
		/* Wait a limited time to skip this packet. */
		for (i = 200; i >= 0; i--)
			if (!(inw(ioaddr + EL3_STATUS) & CmdInProgress))
				break;
	}
	return 0;
}

static int boomerang_rx(struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	int entry = vp->cur_rx % RX_RING_SIZE;
	int ioaddr = dev->base_addr;
	int rx_status;

	if (corkscrew_debug > 5)
		printk("   In boomerang_rx(), status %4.4x, rx_status %4.4x.\n",
			inw(ioaddr + EL3_STATUS), inw(ioaddr + RxStatus));
	while ((rx_status = vp->rx_ring[entry].status) & RxDComplete) {
		if (rx_status & RxDError) {	/* Error, update stats. */
			unsigned char rx_error = rx_status >> 16;
			if (corkscrew_debug > 2)
				printk(" Rx error: status %2.2x.\n",
				       rx_error);
			vp->stats.rx_errors++;
			if (rx_error & 0x01)
				vp->stats.rx_over_errors++;
			if (rx_error & 0x02)
				vp->stats.rx_length_errors++;
			if (rx_error & 0x04)
				vp->stats.rx_frame_errors++;
			if (rx_error & 0x08)
				vp->stats.rx_crc_errors++;
			if (rx_error & 0x10)
				vp->stats.rx_length_errors++;
		} else {
			/* The packet length: up to 4.5K!. */
			short pkt_len = rx_status & 0x1fff;
			struct sk_buff *skb;

			vp->stats.rx_bytes += pkt_len;
			if (corkscrew_debug > 4)
				printk("Receiving packet size %d status %4.4x.\n",
				     pkt_len, rx_status);

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len < rx_copybreak
			    && (skb = dev_alloc_skb(pkt_len + 4)) != 0) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
				memcpy(skb_put(skb, pkt_len),
				       isa_bus_to_virt(vp->rx_ring[entry].
						   addr), pkt_len);
				rx_copy++;
			} else {
				void *temp;
				/* Pass up the skbuff already on the Rx ring. */
				skb = vp->rx_skbuff[entry];
				vp->rx_skbuff[entry] = NULL;
				temp = skb_put(skb, pkt_len);
				/* Remove this checking code for final release. */
				if (isa_bus_to_virt(vp->rx_ring[entry].addr) != temp)
					    printk("%s: Warning -- the skbuff addresses do not match"
					     " in boomerang_rx: %p vs. %p / %p.\n",
					     dev->name,
					     isa_bus_to_virt(vp->
							 rx_ring[entry].
							 addr), skb->head,
					     temp);
				rx_nocopy++;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			vp->stats.rx_packets++;
		}
		entry = (++vp->cur_rx) % RX_RING_SIZE;
	}
	/* Refill the Rx ring buffers. */
	for (; vp->cur_rx - vp->dirty_rx > 0; vp->dirty_rx++) {
		struct sk_buff *skb;
		entry = vp->dirty_rx % RX_RING_SIZE;
		if (vp->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(PKT_BUF_SZ);
			if (skb == NULL)
				break;	/* Bad news!  */
			skb->dev = dev;	/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			vp->rx_ring[entry].addr = isa_virt_to_bus(skb->data);
			vp->rx_skbuff[entry] = skb;
		}
		vp->rx_ring[entry].status = 0;	/* Clear complete bit. */
	}
	return 0;
}

static int corkscrew_close(struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int i;

	netif_stop_queue(dev);

	if (corkscrew_debug > 1) {
		printk("%s: corkscrew_close() status %4.4x, Tx status %2.2x.\n",
		     dev->name, inw(ioaddr + EL3_STATUS),
		     inb(ioaddr + TxStatus));
		printk("%s: corkscrew close stats: rx_nocopy %d rx_copy %d"
		       " tx_queued %d.\n", dev->name, rx_nocopy, rx_copy,
		       queued_packet);
	}

	del_timer(&vp->timer);

	/* Turn off statistics ASAP.  We update lp->stats below. */
	outw(StatsDisable, ioaddr + EL3_CMD);

	/* Disable the receiver and transmitter. */
	outw(RxDisable, ioaddr + EL3_CMD);
	outw(TxDisable, ioaddr + EL3_CMD);

	if (dev->if_port == XCVR_10base2)
		/* Turn off thinnet power.  Green! */
		outw(StopCoax, ioaddr + EL3_CMD);

	free_irq(dev->irq, dev);

	outw(SetIntrEnb | 0x0000, ioaddr + EL3_CMD);

	update_stats(ioaddr, dev);
	if (vp->full_bus_master_rx) {	/* Free Boomerang bus master Rx buffers. */
		outl(0, ioaddr + UpListPtr);
		for (i = 0; i < RX_RING_SIZE; i++)
			if (vp->rx_skbuff[i]) {
				dev_kfree_skb(vp->rx_skbuff[i]);
				vp->rx_skbuff[i] = NULL;
			}
	}
	if (vp->full_bus_master_tx) {	/* Free Boomerang bus master Tx buffers. */
		outl(0, ioaddr + DownListPtr);
		for (i = 0; i < TX_RING_SIZE; i++)
			if (vp->tx_skbuff[i]) {
				dev_kfree_skb(vp->tx_skbuff[i]);
				vp->tx_skbuff[i] = NULL;
			}
	}

	return 0;
}

static struct net_device_stats *corkscrew_get_stats(struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);
	unsigned long flags;

	if (netif_running(dev)) {
		spin_lock_irqsave(&vp->lock, flags);
		update_stats(dev->base_addr, dev);
		spin_unlock_irqrestore(&vp->lock, flags);
	}
	return &vp->stats;
}

/*  Update statistics.
	Unlike with the EL3 we need not worry about interrupts changing
	the window setting from underneath us, but we must still guard
	against a race condition with a StatsUpdate interrupt updating the
	table.  This is done by checking that the ASM (!) code generated uses
	atomic updates with '+='.
	*/
static void update_stats(int ioaddr, struct net_device *dev)
{
	struct corkscrew_private *vp = netdev_priv(dev);

	/* Unlike the 3c5x9 we need not turn off stats updates while reading. */
	/* Switch to the stats window, and read everything. */
	EL3WINDOW(6);
	vp->stats.tx_carrier_errors += inb(ioaddr + 0);
	vp->stats.tx_heartbeat_errors += inb(ioaddr + 1);
	/* Multiple collisions. */ inb(ioaddr + 2);
	vp->stats.collisions += inb(ioaddr + 3);
	vp->stats.tx_window_errors += inb(ioaddr + 4);
	vp->stats.rx_fifo_errors += inb(ioaddr + 5);
	vp->stats.tx_packets += inb(ioaddr + 6);
	vp->stats.tx_packets += (inb(ioaddr + 9) & 0x30) << 4;
						/* Rx packets   */ inb(ioaddr + 7);
						/* Must read to clear */
	/* Tx deferrals */ inb(ioaddr + 8);
	/* Don't bother with register 9, an extension of registers 6&7.
	   If we do use the 6&7 values the atomic update assumption above
	   is invalid. */
	inw(ioaddr + 10);	/* Total Rx and Tx octets. */
	inw(ioaddr + 12);
	/* New: On the Vortex we must also clear the BadSSD counter. */
	EL3WINDOW(4);
	inb(ioaddr + 12);

	/* We change back to window 7 (not 1) with the Vortex. */
	EL3WINDOW(7);
	return;
}

/* This new version of set_rx_mode() supports v1.4 kernels.
   The Vortex chip has no documented multicast filter, so the only
   multicast setting is to receive all multicast frames.  At least
   the chip has a very clean way to set the mode, unlike many others. */
static void set_rx_mode(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	short new_mode;

	if (dev->flags & IFF_PROMISC) {
		if (corkscrew_debug > 3)
			printk("%s: Setting promiscuous mode.\n",
			       dev->name);
		new_mode = SetRxFilter | RxStation | RxMulticast | RxBroadcast | RxProm;
	} else if ((dev->mc_list) || (dev->flags & IFF_ALLMULTI)) {
		new_mode = SetRxFilter | RxStation | RxMulticast | RxBroadcast;
	} else
		new_mode = SetRxFilter | RxStation | RxBroadcast;

	outw(new_mode, ioaddr + EL3_CMD);
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	sprintf(info->bus_info, "ISA 0x%lx", dev->base_addr);
}

static u32 netdev_get_msglevel(struct net_device *dev)
{
	return corkscrew_debug;
}

static void netdev_set_msglevel(struct net_device *dev, u32 level)
{
	corkscrew_debug = level;
}

static struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
};


#ifdef MODULE
void cleanup_module(void)
{
	while (!list_empty(&root_corkscrew_dev)) {
		struct net_device *dev;
		struct corkscrew_private *vp;

		vp = list_entry(root_corkscrew_dev.next,
				struct corkscrew_private, list);
		dev = vp->our_dev;
		unregister_netdev(dev);
		cleanup_card(dev);
		free_netdev(dev);
	}
}
#endif				/* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c 3c515.c"
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */

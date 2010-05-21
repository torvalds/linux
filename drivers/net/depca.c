/*  depca.c: A DIGITAL DEPCA & EtherWORKS ethernet driver for linux.

    Written 1994, 1995 by David C. Davies.


                      Copyright 1994 David C. Davies
		                   and
			 United States Government
	 (as represented by the Director, National Security Agency).

               Copyright 1995  Digital Equipment Corporation.


    This software may be used and distributed according to the terms of
    the GNU General Public License, incorporated herein by reference.

    This driver is written for the Digital Equipment Corporation series
    of DEPCA and EtherWORKS ethernet cards:

        DEPCA       (the original)
    	DE100
    	DE101
	DE200 Turbo
	DE201 Turbo
	DE202 Turbo (TP BNC)
	DE210
	DE422       (EISA)

    The  driver has been tested on DE100, DE200 and DE202 cards  in  a
    relatively busy network. The DE422 has been tested a little.

    This  driver will NOT work   for the DE203,  DE204  and DE205 series  of
    cards,  since they have  a  new custom ASIC in   place of the AMD  LANCE
    chip.  See the 'ewrk3.c'   driver in the  Linux  source tree for running
    those cards.

    I have benchmarked the driver with a  DE100 at 595kB/s to (542kB/s from)
    a DECstation 5000/200.

    The author may be reached at davies@maniac.ultranet.com

    =========================================================================

    The  driver was originally based  on   the 'lance.c' driver from  Donald
    Becker   which  is included with  the  standard  driver distribution for
    linux.  V0.4  is  a complete  re-write  with only  the kernel  interface
    remaining from the original code.

    1) Lance.c code in /linux/drivers/net/
    2) "Ethernet/IEEE 802.3 Family. 1992 World Network Data Book/Handbook",
       AMD, 1992 [(800) 222-9323].
    3) "Am79C90 CMOS Local Area Network Controller for Ethernet (C-LANCE)",
       AMD, Pub. #17881, May 1993.
    4) "Am79C960 PCnet-ISA(tm), Single-Chip Ethernet Controller for ISA",
       AMD, Pub. #16907, May 1992
    5) "DEC EtherWORKS LC Ethernet Controller Owners Manual",
       Digital Equipment corporation, 1990, Pub. #EK-DE100-OM.003
    6) "DEC EtherWORKS Turbo Ethernet Controller Owners Manual",
       Digital Equipment corporation, 1990, Pub. #EK-DE200-OM.003
    7) "DEPCA Hardware Reference Manual", Pub. #EK-DEPCA-PR
       Digital Equipment Corporation, 1989
    8) "DEC EtherWORKS Turbo_(TP BNC) Ethernet Controller Owners Manual",
       Digital Equipment corporation, 1991, Pub. #EK-DE202-OM.001


    Peter Bauer's depca.c (V0.5) was referred to when debugging V0.1 of this
    driver.

    The original DEPCA  card requires that the  ethernet ROM address counter
    be enabled to count and has an 8 bit NICSR.  The ROM counter enabling is
    only  done when a  0x08 is read as the  first address octet (to minimise
    the chances  of writing over some  other hardware's  I/O register).  The
    NICSR accesses   have been changed  to  byte accesses  for all the cards
    supported by this driver, since there is only one  useful bit in the MSB
    (remote boot timeout) and it  is not used.  Also, there  is a maximum of
    only 48kB network  RAM for this  card.  My thanks  to Torbjorn Lindh for
    help debugging all this (and holding my feet to  the fire until I got it
    right).

    The DE200  series  boards have  on-board 64kB  RAM for  use  as a shared
    memory network  buffer. Only the DE100  cards make use  of a  2kB buffer
    mode which has not  been implemented in  this driver (only the 32kB  and
    64kB modes are supported [16kB/48kB for the original DEPCA]).

    At the most only 2 DEPCA cards can  be supported on  the ISA bus because
    there is only provision  for two I/O base addresses  on each card (0x300
    and 0x200). The I/O address is detected by searching for a byte sequence
    in the Ethernet station address PROM at the expected I/O address for the
    Ethernet  PROM.   The shared memory  base   address  is 'autoprobed'  by
    looking  for the self  test PROM  and detecting the  card name.   When a
    second  DEPCA is  detected,  information  is   placed in the   base_addr
    variable of the  next device structure (which  is created if necessary),
    thus  enabling ethif_probe  initialization  for the device.  More than 2
    EISA cards can  be  supported, but  care will  be  needed assigning  the
    shared memory to ensure that each slot has the  correct IRQ, I/O address
    and shared memory address assigned.

    ************************************************************************

    NOTE: If you are using two  ISA DEPCAs, it is  important that you assign
    the base memory addresses correctly.   The  driver autoprobes I/O  0x300
    then 0x200.  The  base memory address for  the first device must be less
    than that of the second so that the auto probe will correctly assign the
    I/O and memory addresses on the same card.  I can't think of a way to do
    this unambiguously at the moment, since there is nothing on the cards to
    tie I/O and memory information together.

    I am unable  to  test  2 cards   together for now,    so this  code   is
    unchecked. All reports, good or bad, are welcome.

    ************************************************************************

    The board IRQ   setting must be  at an  unused IRQ which  is auto-probed
    using Donald Becker's autoprobe routines. DEPCA and DE100 board IRQs are
    {2,3,4,5,7}, whereas the  DE200 is at {5,9,10,11,15}.  Note that IRQ2 is
    really IRQ9 in machines with 16 IRQ lines.

    No 16MB memory  limitation should exist with this  driver as DMA is  not
    used and the common memory area is in low memory on the network card (my
    current system has 20MB and I've not had problems yet).

    The ability to load this driver as a loadable module has been added. To
    utilise this ability, you have to do <8 things:

    0) have a copy of the loadable modules code installed on your system.
    1) copy depca.c from the  /linux/drivers/net directory to your favourite
    temporary directory.
    2) if you wish, edit the  source code near  line 1530 to reflect the I/O
    address and IRQ you're using (see also 5).
    3) compile  depca.c, but include -DMODULE in  the command line to ensure
    that the correct bits are compiled (see end of source code).
    4) if you are wanting to add a new  card, goto 5. Otherwise, recompile a
    kernel with the depca configuration turned off and reboot.
    5) insmod depca.o [irq=7] [io=0x200] [mem=0xd0000] [adapter_name=DE100]
       [Alan Cox: Changed the code to allow command line irq/io assignments]
       [Dave Davies: Changed the code to allow command line mem/name
                                                                assignments]
    6) run the net startup bits for your eth?? interface manually
    (usually /etc/rc.inet[12] at boot time).
    7) enjoy!

    Note that autoprobing is not allowed in loadable modules - the system is
    already up and running and you're messing with interrupts.

    To unload a module, turn off the associated interface
    'ifconfig eth?? down' then 'rmmod depca'.

    To assign a base memory address for the shared memory  when running as a
    loadable module, see 5 above.  To include the adapter  name (if you have
    no PROM  but know the card name)  also see 5  above. Note that this last
    option  will not work  with kernel  built-in  depca's.

    The shared memory assignment for a loadable module  makes sense to avoid
    the 'memory autoprobe' picking the wrong shared memory  (for the case of
    2 depca's in a PC).

    ************************************************************************
    Support for MCA EtherWORKS cards added 11-3-98.
    Verified to work with up to 2 DE212 cards in a system (although not
      fully stress-tested).

    Currently known bugs/limitations:

    Note:  with the MCA stuff as a module, it trusts the MCA configuration,
           not the command line for IRQ and memory address.  You can
           specify them if you want, but it will throw your values out.
           You still have to pass the IO address it was configured as
           though.

    ************************************************************************
    TO DO:
    ------


    Revision History
    ----------------

    Version   Date        Description

      0.1     25-jan-94   Initial writing.
      0.2     27-jan-94   Added LANCE TX hardware buffer chaining.
      0.3      1-feb-94   Added multiple DEPCA support.
      0.31     4-feb-94   Added DE202 recognition.
      0.32    19-feb-94   Tidy up. Improve multi-DEPCA support.
      0.33    25-feb-94   Fix DEPCA ethernet ROM counter enable.
                          Add jabber packet fix from murf@perftech.com
			  and becker@super.org
      0.34     7-mar-94   Fix DEPCA max network memory RAM & NICSR access.
      0.35     8-mar-94   Added DE201 recognition. Tidied up.
      0.351   30-apr-94   Added EISA support. Added DE422 recognition.
      0.36    16-may-94   DE422 fix released.
      0.37    22-jul-94   Added MODULE support
      0.38    15-aug-94   Added DBR ROM switch in depca_close().
                          Multi DEPCA bug fix.
      0.38axp 15-sep-94   Special version for Alpha AXP Linux V1.0.
      0.381   12-dec-94   Added DE101 recognition, fix multicast bug.
      0.382    9-feb-95   Fix recognition bug reported by <bkm@star.rl.ac.uk>.
      0.383   22-feb-95   Fix for conflict with VESA SCSI reported by
                          <stromain@alf.dec.com>
      0.384   17-mar-95   Fix a ring full bug reported by <bkm@star.rl.ac.uk>
      0.385    3-apr-95   Fix a recognition bug reported by
                                                <ryan.niemi@lastfrontier.com>
      0.386   21-apr-95   Fix the last fix...sorry, must be galloping senility
      0.40    25-May-95   Rewrite for portability & updated.
                          ALPHA support from <jestabro@amt.tay1.dec.com>
      0.41    26-Jun-95   Added verify_area() calls in depca_ioctl() from
                          suggestion by <heiko@colossus.escape.de>
      0.42    27-Dec-95   Add 'mem' shared memory assignment for loadable
                          modules.
                          Add 'adapter_name' for loadable modules when no PROM.
			  Both above from a suggestion by
			  <pchen@woodruffs121.residence.gatech.edu>.
			  Add new multicasting code.
      0.421   22-Apr-96	  Fix alloc_device() bug <jari@markkus2.fimr.fi>
      0.422   29-Apr-96	  Fix depca_hw_init() bug <jari@markkus2.fimr.fi>
      0.423    7-Jun-96   Fix module load bug <kmg@barco.be>
      0.43    16-Aug-96   Update alloc_device() to conform to de4x5.c
      0.44     1-Sep-97   Fix *_probe() to test check_region() first - bug
                           reported by <mmogilvi@elbert.uccs.edu>
      0.45     3-Nov-98   Added support for MCA EtherWORKS (DE210/DE212) cards
                           by <tymm@computer.org>
      0.451    5-Nov-98   Fixed mca stuff cuz I'm a dummy. <tymm@computer.org>
      0.5     14-Nov-98   Re-spin for 2.1.x kernels.
      0.51    27-Jun-99   Correct received packet length for CRC from
                           report by <worm@dkik.dk>
      0.52    16-Oct-00   Fixes for 2.3 io memory accesses
                          Fix show-stopper (ints left masked) in depca_interrupt
			   by <peterd@pnd-pc.demon.co.uk>
      0.53    12-Jan-01	  Release resources on failure, bss tidbits
      			   by acme@conectiva.com.br
      0.54    08-Nov-01	  use library crc32 functions
      			   by Matt_Domsch@dell.com
      0.55    01-Mar-03   Use EISA/sysfs framework <maz@wild-wind.fr.eu.org>

    =========================================================================
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/ctype.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>

#ifdef CONFIG_MCA
#include <linux/mca.h>
#endif

#ifdef CONFIG_EISA
#include <linux/eisa.h>
#endif

#include "depca.h"

static char version[] __initdata = "depca.c:v0.53 2001/1/12 davies@maniac.ultranet.com\n";

#ifdef DEPCA_DEBUG
static int depca_debug = DEPCA_DEBUG;
#else
static int depca_debug = 1;
#endif

#define DEPCA_NDA 0xffe0	/* No Device Address */

#define TX_TIMEOUT (1*HZ)

/*
** Ethernet PROM defines
*/
#define PROBE_LENGTH    32
#define ETH_PROM_SIG    0xAA5500FFUL

/*
** Set the number of Tx and Rx buffers. Ensure that the memory requested
** here is <= to the amount of shared memory set up by the board switches.
** The number of descriptors MUST BE A POWER OF 2.
**
** total_memory = NUM_RX_DESC*(8+RX_BUFF_SZ) + NUM_TX_DESC*(8+TX_BUFF_SZ)
*/
#define NUM_RX_DESC     8	/* Number of RX descriptors */
#define NUM_TX_DESC     8	/* Number of TX descriptors */
#define RX_BUFF_SZ	1536	/* Buffer size for each Rx buffer */
#define TX_BUFF_SZ	1536	/* Buffer size for each Tx buffer */

/*
** EISA bus defines
*/
#define DEPCA_EISA_IO_PORTS 0x0c00	/* I/O port base address, slot 0 */

/*
** ISA Bus defines
*/
#define DEPCA_RAM_BASE_ADDRESSES {0xc0000,0xd0000,0xe0000,0x00000}
#define DEPCA_TOTAL_SIZE 0x10

static struct {
	u_long iobase;
	struct platform_device *device;
} depca_io_ports[] = {
	{ 0x300, NULL },
	{ 0x200, NULL },
	{ 0    , NULL },
};

/*
** Name <-> Adapter mapping
*/
#define DEPCA_SIGNATURE {"DEPCA",\
			 "DE100","DE101",\
                         "DE200","DE201","DE202",\
			 "DE210","DE212",\
                         "DE422",\
                         ""}

static char* __initdata depca_signature[] = DEPCA_SIGNATURE;

enum depca_type {
	DEPCA, de100, de101, de200, de201, de202, de210, de212, de422, unknown
};

static char depca_string[] = "depca";

static int depca_device_remove (struct device *device);

#ifdef CONFIG_EISA
static struct eisa_device_id depca_eisa_ids[] = {
	{ "DEC4220", de422 },
	{ "" }
};
MODULE_DEVICE_TABLE(eisa, depca_eisa_ids);

static int depca_eisa_probe  (struct device *device);

static struct eisa_driver depca_eisa_driver = {
	.id_table = depca_eisa_ids,
	.driver   = {
		.name    = depca_string,
		.probe   = depca_eisa_probe,
		.remove  = __devexit_p (depca_device_remove)
	}
};
#endif

#ifdef CONFIG_MCA
/*
** Adapter ID for the MCA EtherWORKS DE210/212 adapter
*/
#define DE210_ID 0x628d
#define DE212_ID 0x6def

static short depca_mca_adapter_ids[] = {
	DE210_ID,
	DE212_ID,
	0x0000
};

static char *depca_mca_adapter_name[] = {
	"DEC EtherWORKS MC Adapter (DE210)",
	"DEC EtherWORKS MC Adapter (DE212)",
	NULL
};

static enum depca_type depca_mca_adapter_type[] = {
	de210,
	de212,
	0
};

static int depca_mca_probe (struct device *);

static struct mca_driver depca_mca_driver = {
	.id_table = depca_mca_adapter_ids,
	.driver   = {
		.name   = depca_string,
		.bus    = &mca_bus_type,
		.probe  = depca_mca_probe,
		.remove = __devexit_p(depca_device_remove),
	},
};
#endif

static int depca_isa_probe (struct platform_device *);

static int __devexit depca_isa_remove(struct platform_device *pdev)
{
	return depca_device_remove(&pdev->dev);
}

static struct platform_driver depca_isa_driver = {
	.probe  = depca_isa_probe,
	.remove = __devexit_p(depca_isa_remove),
	.driver	= {
		.name   = depca_string,
	},
};

/*
** Miscellaneous info...
*/
#define DEPCA_STRLEN 16

/*
** Memory Alignment. Each descriptor is 4 longwords long. To force a
** particular alignment on the TX descriptor, adjust DESC_SKIP_LEN and
** DESC_ALIGN. DEPCA_ALIGN aligns the start address of the private memory area
** and hence the RX descriptor ring's first entry.
*/
#define DEPCA_ALIGN4      ((u_long)4 - 1)	/* 1 longword align */
#define DEPCA_ALIGN8      ((u_long)8 - 1)	/* 2 longword (quadword) align */
#define DEPCA_ALIGN         DEPCA_ALIGN8	/* Keep the LANCE happy... */

/*
** The DEPCA Rx and Tx ring descriptors.
*/
struct depca_rx_desc {
	volatile s32 base;
	s16 buf_length;		/* This length is negative 2's complement! */
	s16 msg_length;		/* This length is "normal". */
};

struct depca_tx_desc {
	volatile s32 base;
	s16 length;		/* This length is negative 2's complement! */
	s16 misc;		/* Errors and TDR info */
};

#define LA_MASK 0x0000ffff	/* LANCE address mask for mapping network RAM
				   to LANCE memory address space */

/*
** The Lance initialization block, described in databook, in common memory.
*/
struct depca_init {
	u16 mode;		/* Mode register */
	u8 phys_addr[ETH_ALEN];	/* Physical ethernet address */
	u8 mcast_table[8];	/* Multicast Hash Table. */
	u32 rx_ring;		/* Rx ring base pointer & ring length */
	u32 tx_ring;		/* Tx ring base pointer & ring length */
};

#define DEPCA_PKT_STAT_SZ 16
#define DEPCA_PKT_BIN_SZ  128	/* Should be >=100 unless you
				   increase DEPCA_PKT_STAT_SZ */
struct depca_private {
	char adapter_name[DEPCA_STRLEN];	/* /proc/ioports string                  */
	enum depca_type adapter;		/* Adapter type */
	enum {
                DEPCA_BUS_MCA = 1,
                DEPCA_BUS_ISA,
                DEPCA_BUS_EISA,
        } depca_bus;	        /* type of bus */
	struct depca_init init_block;	/* Shadow Initialization block            */
/* CPU address space fields */
	struct depca_rx_desc __iomem *rx_ring;	/* Pointer to start of RX descriptor ring */
	struct depca_tx_desc __iomem *tx_ring;	/* Pointer to start of TX descriptor ring */
	void __iomem *rx_buff[NUM_RX_DESC];	/* CPU virt address of sh'd memory buffs  */
	void __iomem *tx_buff[NUM_TX_DESC];	/* CPU virt address of sh'd memory buffs  */
	void __iomem *sh_mem;	/* CPU mapped virt address of device RAM  */
	u_long mem_start;	/* Bus address of device RAM (before remap) */
	u_long mem_len;		/* device memory size */
/* Device address space fields */
	u_long device_ram_start;	/* Start of RAM in device addr space      */
/* Offsets used in both address spaces */
	u_long rx_ring_offset;	/* Offset from start of RAM to rx_ring    */
	u_long tx_ring_offset;	/* Offset from start of RAM to tx_ring    */
	u_long buffs_offset;	/* LANCE Rx and Tx buffers start address. */
/* Kernel-only (not device) fields */
	int rx_new, tx_new;	/* The next free ring entry               */
	int rx_old, tx_old;	/* The ring entries to be free()ed.       */
	spinlock_t lock;
	struct {		/* Private stats counters                 */
		u32 bins[DEPCA_PKT_STAT_SZ];
		u32 unicast;
		u32 multicast;
		u32 broadcast;
		u32 excessive_collisions;
		u32 tx_underruns;
		u32 excessive_underruns;
	} pktStats;
	int txRingMask;		/* TX ring mask                           */
	int rxRingMask;		/* RX ring mask                           */
	s32 rx_rlen;		/* log2(rxRingMask+1) for the descriptors */
	s32 tx_rlen;		/* log2(txRingMask+1) for the descriptors */
};

/*
** The transmit ring full condition is described by the tx_old and tx_new
** pointers by:
**    tx_old            = tx_new    Empty ring
**    tx_old            = tx_new+1  Full ring
**    tx_old+txRingMask = tx_new    Full ring  (wrapped condition)
*/
#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			 lp->tx_old+lp->txRingMask-lp->tx_new:\
                         lp->tx_old               -lp->tx_new-1)

/*
** Public Functions
*/
static int depca_open(struct net_device *dev);
static netdev_tx_t depca_start_xmit(struct sk_buff *skb,
				    struct net_device *dev);
static irqreturn_t depca_interrupt(int irq, void *dev_id);
static int depca_close(struct net_device *dev);
static int depca_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void depca_tx_timeout(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);

/*
** Private functions
*/
static void depca_init_ring(struct net_device *dev);
static int depca_rx(struct net_device *dev);
static int depca_tx(struct net_device *dev);

static void LoadCSRs(struct net_device *dev);
static int InitRestartDepca(struct net_device *dev);
static int DepcaSignature(char *name, u_long paddr);
static int DevicePresent(u_long ioaddr);
static int get_hw_addr(struct net_device *dev);
static void SetMulticastFilter(struct net_device *dev);
static int load_packet(struct net_device *dev, struct sk_buff *skb);
static void depca_dbg_open(struct net_device *dev);

static u_char de1xx_irq[] __initdata = { 2, 3, 4, 5, 7, 9, 0 };
static u_char de2xx_irq[] __initdata = { 5, 9, 10, 11, 15, 0 };
static u_char de422_irq[] __initdata = { 5, 9, 10, 11, 0 };
static u_char *depca_irq;

static int irq;
static int io;
static char *adapter_name;
static int mem;			/* For loadable module assignment
				   use insmod mem=0x????? .... */
module_param (irq, int, 0);
module_param (io, int, 0);
module_param (adapter_name, charp, 0);
module_param (mem, int, 0);
MODULE_PARM_DESC(irq, "DEPCA IRQ number");
MODULE_PARM_DESC(io, "DEPCA I/O base address");
MODULE_PARM_DESC(adapter_name, "DEPCA adapter name");
MODULE_PARM_DESC(mem, "DEPCA shared memory address");
MODULE_LICENSE("GPL");

/*
** Miscellaneous defines...
*/
#define STOP_DEPCA \
    outw(CSR0, DEPCA_ADDR);\
    outw(STOP, DEPCA_DATA)

static const struct net_device_ops depca_netdev_ops = {
	.ndo_open 		= depca_open,
	.ndo_start_xmit 	= depca_start_xmit,
	.ndo_stop 		= depca_close,
	.ndo_set_multicast_list = set_multicast_list,
	.ndo_do_ioctl 		= depca_ioctl,
	.ndo_tx_timeout 	= depca_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __init depca_hw_init (struct net_device *dev, struct device *device)
{
	struct depca_private *lp;
	int i, j, offset, netRAM, mem_len, status = 0;
	s16 nicsr;
	u_long ioaddr;
	u_long mem_start;

	/*
	 * We are now supposed to enter this function with the
	 * following fields filled with proper values :
	 *
	 * dev->base_addr
	 * lp->mem_start
	 * lp->depca_bus
	 * lp->adapter
	 *
	 * dev->irq can be set if known from device configuration (on
	 * MCA or EISA) or module option. Otherwise, it will be auto
	 * detected.
	 */

	ioaddr = dev->base_addr;

	STOP_DEPCA;

	nicsr = inb(DEPCA_NICSR);
	nicsr = ((nicsr & ~SHE & ~RBE & ~IEN) | IM);
	outb(nicsr, DEPCA_NICSR);

	if (inw(DEPCA_DATA) != STOP) {
		return -ENXIO;
	}

	lp = netdev_priv(dev);
	mem_start = lp->mem_start;

	if (!mem_start || lp->adapter < DEPCA || lp->adapter >=unknown)
		return -ENXIO;

	printk("%s: %s at 0x%04lx",
	       dev_name(device), depca_signature[lp->adapter], ioaddr);

	switch (lp->depca_bus) {
#ifdef CONFIG_MCA
	case DEPCA_BUS_MCA:
		printk(" (MCA slot %d)", to_mca_device(device)->slot + 1);
		break;
#endif

#ifdef CONFIG_EISA
	case DEPCA_BUS_EISA:
		printk(" (EISA slot %d)", to_eisa_device(device)->slot);
		break;
#endif

	case DEPCA_BUS_ISA:
		break;

	default:
		printk("Unknown DEPCA bus %d\n", lp->depca_bus);
		return -ENXIO;
	}

	printk(", h/w address ");
	status = get_hw_addr(dev);
	printk("%pM", dev->dev_addr);
	if (status != 0) {
		printk("      which has an Ethernet PROM CRC error.\n");
		return -ENXIO;
	}

	/* Set up the maximum amount of network RAM(kB) */
	netRAM = ((lp->adapter != DEPCA) ? 64 : 48);
	if ((nicsr & _128KB) && (lp->adapter == de422))
		netRAM = 128;

	/* Shared Memory Base Address */
	if (nicsr & BUF) {
		nicsr &= ~BS;	/* DEPCA RAM in top 32k */
		netRAM -= 32;

		/* Only EISA/ISA needs start address to be re-computed */
		if (lp->depca_bus != DEPCA_BUS_MCA)
			mem_start += 0x8000;
	}

	if ((mem_len = (NUM_RX_DESC * (sizeof(struct depca_rx_desc) + RX_BUFF_SZ) + NUM_TX_DESC * (sizeof(struct depca_tx_desc) + TX_BUFF_SZ) + sizeof(struct depca_init)))
	    > (netRAM << 10)) {
		printk(",\n       requests %dkB RAM: only %dkB is available!\n", (mem_len >> 10), netRAM);
		return -ENXIO;
	}

	printk(",\n      has %dkB RAM at 0x%.5lx", netRAM, mem_start);

	/* Enable the shadow RAM. */
	if (lp->adapter != DEPCA) {
		nicsr |= SHE;
		outb(nicsr, DEPCA_NICSR);
	}

	spin_lock_init(&lp->lock);
	sprintf(lp->adapter_name, "%s (%s)",
		depca_signature[lp->adapter], dev_name(device));
	status = -EBUSY;

	/* Initialisation Block */
	if (!request_mem_region (mem_start, mem_len, lp->adapter_name)) {
		printk(KERN_ERR "depca: cannot request ISA memory, aborting\n");
		goto out_priv;
	}

	status = -EIO;
	lp->sh_mem = ioremap(mem_start, mem_len);
	if (lp->sh_mem == NULL) {
		printk(KERN_ERR "depca: cannot remap ISA memory, aborting\n");
		goto out1;
	}

	lp->mem_start = mem_start;
	lp->mem_len   = mem_len;
	lp->device_ram_start = mem_start & LA_MASK;

	offset = 0;
	offset += sizeof(struct depca_init);

	/* Tx & Rx descriptors (aligned to a quadword boundary) */
	offset = (offset + DEPCA_ALIGN) & ~DEPCA_ALIGN;
	lp->rx_ring = (struct depca_rx_desc __iomem *) (lp->sh_mem + offset);
	lp->rx_ring_offset = offset;

	offset += (sizeof(struct depca_rx_desc) * NUM_RX_DESC);
	lp->tx_ring = (struct depca_tx_desc __iomem *) (lp->sh_mem + offset);
	lp->tx_ring_offset = offset;

	offset += (sizeof(struct depca_tx_desc) * NUM_TX_DESC);

	lp->buffs_offset = offset;

	/* Finish initialising the ring information. */
	lp->rxRingMask = NUM_RX_DESC - 1;
	lp->txRingMask = NUM_TX_DESC - 1;

	/* Calculate Tx/Rx RLEN size for the descriptors. */
	for (i = 0, j = lp->rxRingMask; j > 0; i++) {
		j >>= 1;
	}
	lp->rx_rlen = (s32) (i << 29);
	for (i = 0, j = lp->txRingMask; j > 0; i++) {
		j >>= 1;
	}
	lp->tx_rlen = (s32) (i << 29);

	/* Load the initialisation block */
	depca_init_ring(dev);

	/* Initialise the control and status registers */
	LoadCSRs(dev);

	/* Enable DEPCA board interrupts for autoprobing */
	nicsr = ((nicsr & ~IM) | IEN);
	outb(nicsr, DEPCA_NICSR);

	/* To auto-IRQ we enable the initialization-done and DMA err,
	   interrupts. For now we will always get a DMA error. */
	if (dev->irq < 2) {
		unsigned char irqnum;
		unsigned long irq_mask, delay;

		irq_mask = probe_irq_on();

		/* Assign the correct irq list */
		switch (lp->adapter) {
		case DEPCA:
		case de100:
		case de101:
			depca_irq = de1xx_irq;
			break;
		case de200:
		case de201:
		case de202:
		case de210:
		case de212:
			depca_irq = de2xx_irq;
			break;
		case de422:
			depca_irq = de422_irq;
			break;

		default:
			break;	/* Not reached */
		}

		/* Trigger an initialization just for the interrupt. */
		outw(INEA | INIT, DEPCA_DATA);

		delay = jiffies + HZ/50;
		while (time_before(jiffies, delay))
			yield();

		irqnum = probe_irq_off(irq_mask);

		status = -ENXIO;
		if (!irqnum) {
			printk(" and failed to detect IRQ line.\n");
			goto out2;
		} else {
			for (dev->irq = 0, i = 0; (depca_irq[i]) && (!dev->irq); i++)
				if (irqnum == depca_irq[i]) {
					dev->irq = irqnum;
					printk(" and uses IRQ%d.\n", dev->irq);
				}

			if (!dev->irq) {
				printk(" but incorrect IRQ line detected.\n");
				goto out2;
			}
		}
	} else {
		printk(" and assigned IRQ%d.\n", dev->irq);
	}

	if (depca_debug > 1) {
		printk(version);
	}

	/* The DEPCA-specific entries in the device structure. */
	dev->netdev_ops = &depca_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	dev->mem_start = 0;

	dev_set_drvdata(device, dev);
	SET_NETDEV_DEV (dev, device);

	status = register_netdev(dev);
	if (status == 0)
		return 0;
out2:
	iounmap(lp->sh_mem);
out1:
	release_mem_region (mem_start, mem_len);
out_priv:
	return status;
}


static int depca_open(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_long ioaddr = dev->base_addr;
	s16 nicsr;
	int status = 0;

	STOP_DEPCA;
	nicsr = inb(DEPCA_NICSR);

	/* Make sure the shadow RAM is enabled */
	if (lp->adapter != DEPCA) {
		nicsr |= SHE;
		outb(nicsr, DEPCA_NICSR);
	}

	/* Re-initialize the DEPCA... */
	depca_init_ring(dev);
	LoadCSRs(dev);

	depca_dbg_open(dev);

	if (request_irq(dev->irq, depca_interrupt, 0, lp->adapter_name, dev)) {
		printk("depca_open(): Requested IRQ%d is busy\n", dev->irq);
		status = -EAGAIN;
	} else {

		/* Enable DEPCA board interrupts and turn off LED */
		nicsr = ((nicsr & ~IM & ~LED) | IEN);
		outb(nicsr, DEPCA_NICSR);
		outw(CSR0, DEPCA_ADDR);

		netif_start_queue(dev);

		status = InitRestartDepca(dev);

		if (depca_debug > 1) {
			printk("CSR0: 0x%4.4x\n", inw(DEPCA_DATA));
			printk("nicsr: 0x%02x\n", inb(DEPCA_NICSR));
		}
	}
	return status;
}

/* Initialize the lance Rx and Tx descriptor rings. */
static void depca_init_ring(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_int i;
	u_long offset;

	/* Lock out other processes whilst setting up the hardware */
	netif_stop_queue(dev);

	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	/* Initialize the base address and length of each buffer in the ring */
	for (i = 0; i <= lp->rxRingMask; i++) {
		offset = lp->buffs_offset + i * RX_BUFF_SZ;
		writel((lp->device_ram_start + offset) | R_OWN, &lp->rx_ring[i].base);
		writew(-RX_BUFF_SZ, &lp->rx_ring[i].buf_length);
		lp->rx_buff[i] = lp->sh_mem + offset;
	}

	for (i = 0; i <= lp->txRingMask; i++) {
		offset = lp->buffs_offset + (i + lp->rxRingMask + 1) * TX_BUFF_SZ;
		writel((lp->device_ram_start + offset) & 0x00ffffff, &lp->tx_ring[i].base);
		lp->tx_buff[i] = lp->sh_mem + offset;
	}

	/* Set up the initialization block */
	lp->init_block.rx_ring = (lp->device_ram_start + lp->rx_ring_offset) | lp->rx_rlen;
	lp->init_block.tx_ring = (lp->device_ram_start + lp->tx_ring_offset) | lp->tx_rlen;

	SetMulticastFilter(dev);

	for (i = 0; i < ETH_ALEN; i++) {
		lp->init_block.phys_addr[i] = dev->dev_addr[i];
	}

	lp->init_block.mode = 0x0000;	/* Enable the Tx and Rx */
}


static void depca_tx_timeout(struct net_device *dev)
{
	u_long ioaddr = dev->base_addr;

	printk("%s: transmit timed out, status %04x, resetting.\n", dev->name, inw(DEPCA_DATA));

	STOP_DEPCA;
	depca_init_ring(dev);
	LoadCSRs(dev);
	dev->trans_start = jiffies; /* prevent tx timeout */
	netif_wake_queue(dev);
	InitRestartDepca(dev);
}


/*
** Writes a socket buffer to TX descriptor ring and starts transmission
*/
static netdev_tx_t depca_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_long ioaddr = dev->base_addr;
	int status = 0;

	/* Transmitter timeout, serious problems. */
	if (skb->len < 1)
		goto out;

	if (skb_padto(skb, ETH_ZLEN))
		goto out;

	netif_stop_queue(dev);

	if (TX_BUFFS_AVAIL) {	/* Fill in a Tx ring entry */
		status = load_packet(dev, skb);

		if (!status) {
			/* Trigger an immediate send demand. */
			outw(CSR0, DEPCA_ADDR);
			outw(INEA | TDMD, DEPCA_DATA);

			dev_kfree_skb(skb);
		}
		if (TX_BUFFS_AVAIL)
			netif_start_queue(dev);
	} else
		status = NETDEV_TX_LOCKED;

      out:
	return status;
}

/*
** The DEPCA interrupt handler.
*/
static irqreturn_t depca_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct depca_private *lp;
	s16 csr0, nicsr;
	u_long ioaddr;

	if (dev == NULL) {
		printk("depca_interrupt(): irq %d for unknown device.\n", irq);
		return IRQ_NONE;
	}

	lp = netdev_priv(dev);
	ioaddr = dev->base_addr;

	spin_lock(&lp->lock);

	/* mask the DEPCA board interrupts and turn on the LED */
	nicsr = inb(DEPCA_NICSR);
	nicsr |= (IM | LED);
	outb(nicsr, DEPCA_NICSR);

	outw(CSR0, DEPCA_ADDR);
	csr0 = inw(DEPCA_DATA);

	/* Acknowledge all of the current interrupt sources ASAP. */
	outw(csr0 & INTE, DEPCA_DATA);

	if (csr0 & RINT)	/* Rx interrupt (packet arrived) */
		depca_rx(dev);

	if (csr0 & TINT)	/* Tx interrupt (packet sent) */
		depca_tx(dev);

	/* Any resources available? */
	if ((TX_BUFFS_AVAIL >= 0) && netif_queue_stopped(dev)) {
		netif_wake_queue(dev);
	}

	/* Unmask the DEPCA board interrupts and turn off the LED */
	nicsr = (nicsr & ~IM & ~LED);
	outb(nicsr, DEPCA_NICSR);

	spin_unlock(&lp->lock);
	return IRQ_HANDLED;
}

/* Called with lp->lock held */
static int depca_rx(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	int i, entry;
	s32 status;

	for (entry = lp->rx_new; !(readl(&lp->rx_ring[entry].base) & R_OWN); entry = lp->rx_new) {
		status = readl(&lp->rx_ring[entry].base) >> 16;
		if (status & R_STP) {	/* Remember start of frame */
			lp->rx_old = entry;
		}
		if (status & R_ENP) {	/* Valid frame status */
			if (status & R_ERR) {	/* There was an error. */
				dev->stats.rx_errors++;	/* Update the error stats. */
				if (status & R_FRAM)
					dev->stats.rx_frame_errors++;
				if (status & R_OFLO)
					dev->stats.rx_over_errors++;
				if (status & R_CRC)
					dev->stats.rx_crc_errors++;
				if (status & R_BUFF)
					dev->stats.rx_fifo_errors++;
			} else {
				short len, pkt_len = readw(&lp->rx_ring[entry].msg_length) - 4;
				struct sk_buff *skb;

				skb = dev_alloc_skb(pkt_len + 2);
				if (skb != NULL) {
					unsigned char *buf;
					skb_reserve(skb, 2);	/* 16 byte align the IP header */
					buf = skb_put(skb, pkt_len);
					if (entry < lp->rx_old) {	/* Wrapped buffer */
						len = (lp->rxRingMask - lp->rx_old + 1) * RX_BUFF_SZ;
						memcpy_fromio(buf, lp->rx_buff[lp->rx_old], len);
						memcpy_fromio(buf + len, lp->rx_buff[0], pkt_len - len);
					} else {	/* Linear buffer */
						memcpy_fromio(buf, lp->rx_buff[lp->rx_old], pkt_len);
					}

					/*
					   ** Notify the upper protocol layers that there is another
					   ** packet to handle
					 */
					skb->protocol = eth_type_trans(skb, dev);
					netif_rx(skb);

					/*
					   ** Update stats
					 */
					dev->stats.rx_packets++;
					dev->stats.rx_bytes += pkt_len;
					for (i = 1; i < DEPCA_PKT_STAT_SZ - 1; i++) {
						if (pkt_len < (i * DEPCA_PKT_BIN_SZ)) {
							lp->pktStats.bins[i]++;
							i = DEPCA_PKT_STAT_SZ;
						}
					}
					if (buf[0] & 0x01) {	/* Multicast/Broadcast */
						if ((*(s16 *) & buf[0] == -1) && (*(s16 *) & buf[2] == -1) && (*(s16 *) & buf[4] == -1)) {
							lp->pktStats.broadcast++;
						} else {
							lp->pktStats.multicast++;
						}
					} else if ((*(s16 *) & buf[0] == *(s16 *) & dev->dev_addr[0]) && (*(s16 *) & buf[2] == *(s16 *) & dev->dev_addr[2]) && (*(s16 *) & buf[4] == *(s16 *) & dev->dev_addr[4])) {
						lp->pktStats.unicast++;
					}

					lp->pktStats.bins[0]++;	/* Duplicates stats.rx_packets */
					if (lp->pktStats.bins[0] == 0) {	/* Reset counters */
						memset((char *) &lp->pktStats, 0, sizeof(lp->pktStats));
					}
				} else {
					printk("%s: Memory squeeze, deferring packet.\n", dev->name);
					dev->stats.rx_dropped++;	/* Really, deferred. */
					break;
				}
			}
			/* Change buffer ownership for this last frame, back to the adapter */
			for (; lp->rx_old != entry; lp->rx_old = (++lp->rx_old) & lp->rxRingMask) {
				writel(readl(&lp->rx_ring[lp->rx_old].base) | R_OWN, &lp->rx_ring[lp->rx_old].base);
			}
			writel(readl(&lp->rx_ring[entry].base) | R_OWN, &lp->rx_ring[entry].base);
		}

		/*
		   ** Update entry information
		 */
		lp->rx_new = (++lp->rx_new) & lp->rxRingMask;
	}

	return 0;
}

/*
** Buffer sent - check for buffer errors.
** Called with lp->lock held
*/
static int depca_tx(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	int entry;
	s32 status;
	u_long ioaddr = dev->base_addr;

	for (entry = lp->tx_old; entry != lp->tx_new; entry = lp->tx_old) {
		status = readl(&lp->tx_ring[entry].base) >> 16;

		if (status < 0) {	/* Packet not yet sent! */
			break;
		} else if (status & T_ERR) {	/* An error occurred. */
			status = readl(&lp->tx_ring[entry].misc);
			dev->stats.tx_errors++;
			if (status & TMD3_RTRY)
				dev->stats.tx_aborted_errors++;
			if (status & TMD3_LCAR)
				dev->stats.tx_carrier_errors++;
			if (status & TMD3_LCOL)
				dev->stats.tx_window_errors++;
			if (status & TMD3_UFLO)
				dev->stats.tx_fifo_errors++;
			if (status & (TMD3_BUFF | TMD3_UFLO)) {
				/* Trigger an immediate send demand. */
				outw(CSR0, DEPCA_ADDR);
				outw(INEA | TDMD, DEPCA_DATA);
			}
		} else if (status & (T_MORE | T_ONE)) {
			dev->stats.collisions++;
		} else {
			dev->stats.tx_packets++;
		}

		/* Update all the pointers */
		lp->tx_old = (++lp->tx_old) & lp->txRingMask;
	}

	return 0;
}

static int depca_close(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	s16 nicsr;
	u_long ioaddr = dev->base_addr;

	netif_stop_queue(dev);

	outw(CSR0, DEPCA_ADDR);

	if (depca_debug > 1) {
		printk("%s: Shutting down ethercard, status was %2.2x.\n", dev->name, inw(DEPCA_DATA));
	}

	/*
	   ** We stop the DEPCA here -- it occasionally polls
	   ** memory if we don't.
	 */
	outw(STOP, DEPCA_DATA);

	/*
	   ** Give back the ROM in case the user wants to go to DOS
	 */
	if (lp->adapter != DEPCA) {
		nicsr = inb(DEPCA_NICSR);
		nicsr &= ~SHE;
		outb(nicsr, DEPCA_NICSR);
	}

	/*
	   ** Free the associated irq
	 */
	free_irq(dev->irq, dev);
	return 0;
}

static void LoadCSRs(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_long ioaddr = dev->base_addr;

	outw(CSR1, DEPCA_ADDR);	/* initialisation block address LSW */
	outw((u16) lp->device_ram_start, DEPCA_DATA);
	outw(CSR2, DEPCA_ADDR);	/* initialisation block address MSW */
	outw((u16) (lp->device_ram_start >> 16), DEPCA_DATA);
	outw(CSR3, DEPCA_ADDR);	/* ALE control */
	outw(ACON, DEPCA_DATA);

	outw(CSR0, DEPCA_ADDR);	/* Point back to CSR0 */
}

static int InitRestartDepca(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_long ioaddr = dev->base_addr;
	int i, status = 0;

	/* Copy the shadow init_block to shared memory */
	memcpy_toio(lp->sh_mem, &lp->init_block, sizeof(struct depca_init));

	outw(CSR0, DEPCA_ADDR);	/* point back to CSR0 */
	outw(INIT, DEPCA_DATA);	/* initialize DEPCA */

	/* wait for lance to complete initialisation */
	for (i = 0; (i < 100) && !(inw(DEPCA_DATA) & IDON); i++);

	if (i != 100) {
		/* clear IDON by writing a "1", enable interrupts and start lance */
		outw(IDON | INEA | STRT, DEPCA_DATA);
		if (depca_debug > 2) {
			printk("%s: DEPCA open after %d ticks, init block 0x%08lx csr0 %4.4x.\n", dev->name, i, lp->mem_start, inw(DEPCA_DATA));
		}
	} else {
		printk("%s: DEPCA unopen after %d ticks, init block 0x%08lx csr0 %4.4x.\n", dev->name, i, lp->mem_start, inw(DEPCA_DATA));
		status = -1;
	}

	return status;
}

/*
** Set or clear the multicast filter for this adaptor.
*/
static void set_multicast_list(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_long ioaddr = dev->base_addr;

	netif_stop_queue(dev);
	while (lp->tx_old != lp->tx_new);	/* Wait for the ring to empty */

	STOP_DEPCA;	/* Temporarily stop the depca.  */
	depca_init_ring(dev);	/* Initialize the descriptor rings */

	if (dev->flags & IFF_PROMISC) {	/* Set promiscuous mode */
		lp->init_block.mode |= PROM;
	} else {
		SetMulticastFilter(dev);
		lp->init_block.mode &= ~PROM;	/* Unset promiscuous mode */
	}

	LoadCSRs(dev);	/* Reload CSR3 */
	InitRestartDepca(dev);	/* Resume normal operation. */
	netif_start_queue(dev);	/* Unlock the TX ring */
}

/*
** Calculate the hash code and update the logical address filter
** from a list of ethernet multicast addresses.
** Big endian crc one liner is mine, all mine, ha ha ha ha!
** LANCE calculates its hash codes big endian.
*/
static void SetMulticastFilter(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	char *addrs;
	int i, j, bit, byte;
	u16 hashcode;
	u32 crc;

	if (dev->flags & IFF_ALLMULTI) {	/* Set all multicast bits */
		for (i = 0; i < (HASH_TABLE_LEN >> 3); i++) {
			lp->init_block.mcast_table[i] = (char) 0xff;
		}
	} else {
		for (i = 0; i < (HASH_TABLE_LEN >> 3); i++) {	/* Clear the multicast table */
			lp->init_block.mcast_table[i] = 0;
		}
		/* Add multicast addresses */
		netdev_for_each_mc_addr(ha, dev) {
			addrs = ha->addr;
			if ((*addrs & 0x01) == 1) {	/* multicast address? */
				crc = ether_crc(ETH_ALEN, addrs);
				hashcode = (crc & 1);	/* hashcode is 6 LSb of CRC ... */
				for (j = 0; j < 5; j++) {	/* ... in reverse order. */
					hashcode = (hashcode << 1) | ((crc >>= 1) & 1);
				}


				byte = hashcode >> 3;	/* bit[3-5] -> byte in filter */
				bit = 1 << (hashcode & 0x07);	/* bit[0-2] -> bit in byte */
				lp->init_block.mcast_table[byte] |= bit;
			}
		}
	}
}

static int __init depca_common_init (u_long ioaddr, struct net_device **devp)
{
	int status = 0;

	if (!request_region (ioaddr, DEPCA_TOTAL_SIZE, depca_string)) {
		status = -EBUSY;
		goto out;
	}

	if (DevicePresent(ioaddr)) {
		status = -ENODEV;
		goto out_release;
	}

	if (!(*devp = alloc_etherdev (sizeof (struct depca_private)))) {
		status = -ENOMEM;
		goto out_release;
	}

	return 0;

 out_release:
	release_region (ioaddr, DEPCA_TOTAL_SIZE);
 out:
	return status;
}

#ifdef CONFIG_MCA
/*
** Microchannel bus I/O device probe
*/
static int __init depca_mca_probe(struct device *device)
{
	unsigned char pos[2];
	unsigned char where;
	unsigned long iobase, mem_start;
	int irq, err;
	struct mca_device *mdev = to_mca_device (device);
	struct net_device *dev;
	struct depca_private *lp;

	/*
	** Search for the adapter.  If an address has been given, search
	** specifically for the card at that address.  Otherwise find the
	** first card in the system.
	*/

	pos[0] = mca_device_read_stored_pos(mdev, 2);
	pos[1] = mca_device_read_stored_pos(mdev, 3);

	/*
	** IO of card is handled by bits 1 and 2 of pos0.
	**
	**    bit2 bit1    IO
	**       0    0    0x2c00
	**       0    1    0x2c10
	**       1    0    0x2c20
	**       1    1    0x2c30
	*/
	where = (pos[0] & 6) >> 1;
	iobase = 0x2c00 + (0x10 * where);

	/*
	** Found the adapter we were looking for. Now start setting it up.
	**
	** First work on decoding the IRQ.  It's stored in the lower 4 bits
	** of pos1.  Bits are as follows (from the ADF file):
	**
	**      Bits
	**   3   2   1   0    IRQ
	**   --------------------
	**   0   0   1   0     5
	**   0   0   0   1     9
	**   0   1   0   0    10
	**   1   0   0   0    11
	*/
	where = pos[1] & 0x0f;
	switch (where) {
	case 1:
		irq = 9;
		break;
	case 2:
		irq = 5;
		break;
	case 4:
		irq = 10;
		break;
	case 8:
		irq = 11;
		break;
	default:
		printk("%s: mca_probe IRQ error.  You should never get here (%d).\n", mdev->name, where);
		return -EINVAL;
	}

	/*
	** Shared memory address of adapter is stored in bits 3-5 of pos0.
	** They are mapped as follows:
	**
	**    Bit
	**   5  4  3       Memory Addresses
	**   0  0  0       C0000-CFFFF (64K)
	**   1  0  0       C8000-CFFFF (32K)
	**   0  0  1       D0000-DFFFF (64K)
	**   1  0  1       D8000-DFFFF (32K)
	**   0  1  0       E0000-EFFFF (64K)
	**   1  1  0       E8000-EFFFF (32K)
	*/
	where = (pos[0] & 0x18) >> 3;
	mem_start = 0xc0000 + (where * 0x10000);
	if (pos[0] & 0x20) {
		mem_start += 0x8000;
	}

	/* claim the slot */
	strncpy(mdev->name, depca_mca_adapter_name[mdev->index],
		sizeof(mdev->name));
	mca_device_set_claim(mdev, 1);

        /*
	** Get everything allocated and initialized...  (almost just
	** like the ISA and EISA probes)
	*/
	irq = mca_device_transform_irq(mdev, irq);
	iobase = mca_device_transform_ioport(mdev, iobase);

	if ((err = depca_common_init (iobase, &dev)))
		goto out_unclaim;

	dev->irq = irq;
	dev->base_addr = iobase;
	lp = netdev_priv(dev);
	lp->depca_bus = DEPCA_BUS_MCA;
	lp->adapter = depca_mca_adapter_type[mdev->index];
	lp->mem_start = mem_start;

	if ((err = depca_hw_init(dev, device)))
		goto out_free;

	return 0;

 out_free:
	free_netdev (dev);
	release_region (iobase, DEPCA_TOTAL_SIZE);
 out_unclaim:
	mca_device_set_claim(mdev, 0);

	return err;
}
#endif

/*
** ISA bus I/O device probe
*/

static void __init depca_platform_probe (void)
{
	int i;
	struct platform_device *pldev;

	for (i = 0; depca_io_ports[i].iobase; i++) {
		depca_io_ports[i].device = NULL;

		/* if an address has been specified on the command
		 * line, use it (if valid) */
		if (io && io != depca_io_ports[i].iobase)
			continue;

		pldev = platform_device_alloc(depca_string, i);
		if (!pldev)
			continue;

		pldev->dev.platform_data = (void *) depca_io_ports[i].iobase;
		depca_io_ports[i].device = pldev;

		if (platform_device_add(pldev)) {
			depca_io_ports[i].device = NULL;
			pldev->dev.platform_data = NULL;
			platform_device_put(pldev);
			continue;
		}

		if (!pldev->dev.driver) {
		/* The driver was not bound to this device, there was
		 * no hardware at this address. Unregister it, as the
		 * release fuction will take care of freeing the
		 * allocated structure */

			depca_io_ports[i].device = NULL;
			pldev->dev.platform_data = NULL;
			platform_device_unregister (pldev);
		}
	}
}

static enum depca_type __init depca_shmem_probe (ulong *mem_start)
{
	u_long mem_base[] = DEPCA_RAM_BASE_ADDRESSES;
	enum depca_type adapter = unknown;
	int i;

	for (i = 0; mem_base[i]; i++) {
		*mem_start = mem ? mem : mem_base[i];
		adapter = DepcaSignature (adapter_name, *mem_start);
		if (adapter != unknown)
			break;
	}

	return adapter;
}

static int __init depca_isa_probe (struct platform_device *device)
{
	struct net_device *dev;
	struct depca_private *lp;
	u_long ioaddr, mem_start = 0;
	enum depca_type adapter = unknown;
	int status = 0;

	ioaddr = (u_long) device->dev.platform_data;

	if ((status = depca_common_init (ioaddr, &dev)))
		goto out;

	adapter = depca_shmem_probe (&mem_start);

	if (adapter == unknown) {
		status = -ENODEV;
		goto out_free;
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;		/* Use whatever value the user gave
				 * us, and 0 if he didn't. */
	lp = netdev_priv(dev);
	lp->depca_bus = DEPCA_BUS_ISA;
	lp->adapter = adapter;
	lp->mem_start = mem_start;

	if ((status = depca_hw_init(dev, &device->dev)))
		goto out_free;

	return 0;

 out_free:
	free_netdev (dev);
	release_region (ioaddr, DEPCA_TOTAL_SIZE);
 out:
	return status;
}

/*
** EISA callbacks from sysfs.
*/

#ifdef CONFIG_EISA
static int __init depca_eisa_probe (struct device *device)
{
	enum depca_type adapter = unknown;
	struct eisa_device *edev;
	struct net_device *dev;
	struct depca_private *lp;
	u_long ioaddr, mem_start;
	int status = 0;

	edev = to_eisa_device (device);
	ioaddr = edev->base_addr + DEPCA_EISA_IO_PORTS;

	if ((status = depca_common_init (ioaddr, &dev)))
		goto out;

	/* It would have been nice to get card configuration from the
	 * card. Unfortunately, this register is write-only (shares
	 * it's address with the ethernet prom)... As we don't parse
	 * the EISA configuration structures (yet... :-), just rely on
	 * the ISA probing to sort it out... */

	adapter = depca_shmem_probe (&mem_start);
	if (adapter == unknown) {
		status = -ENODEV;
		goto out_free;
	}

	dev->base_addr = ioaddr;
	dev->irq = irq;
	lp = netdev_priv(dev);
	lp->depca_bus = DEPCA_BUS_EISA;
	lp->adapter = edev->id.driver_data;
	lp->mem_start = mem_start;

	if ((status = depca_hw_init(dev, device)))
		goto out_free;

	return 0;

 out_free:
	free_netdev (dev);
	release_region (ioaddr, DEPCA_TOTAL_SIZE);
 out:
	return status;
}
#endif

static int __devexit depca_device_remove (struct device *device)
{
	struct net_device *dev;
	struct depca_private *lp;
	int bus;

	dev  = dev_get_drvdata(device);
	lp   = netdev_priv(dev);

	unregister_netdev (dev);
	iounmap (lp->sh_mem);
	release_mem_region (lp->mem_start, lp->mem_len);
	release_region (dev->base_addr, DEPCA_TOTAL_SIZE);
	bus = lp->depca_bus;
	free_netdev (dev);

	return 0;
}

/*
** Look for a particular board name in the on-board Remote Diagnostics
** and Boot (readb) ROM. This will also give us a clue to the network RAM
** base address.
*/
static int __init DepcaSignature(char *name, u_long base_addr)
{
	u_int i, j, k;
	void __iomem *ptr;
	char tmpstr[16];
	u_long prom_addr = base_addr + 0xc000;
	u_long mem_addr = base_addr + 0x8000; /* 32KB */

	/* Can't reserve the prom region, it is already marked as
	 * used, at least on x86. Instead, reserve a memory region a
	 * board would certainly use. If it works, go ahead. If not,
	 * run like hell... */

	if (!request_mem_region (mem_addr, 16, depca_string))
		return unknown;

	/* Copy the first 16 bytes of ROM */

	ptr = ioremap(prom_addr, 16);
	if (ptr == NULL) {
		printk(KERN_ERR "depca: I/O remap failed at %lx\n", prom_addr);
		return unknown;
	}
	for (i = 0; i < 16; i++) {
		tmpstr[i] = readb(ptr + i);
	}
	iounmap(ptr);

	release_mem_region (mem_addr, 16);

	/* Check if PROM contains a valid string */
	for (i = 0; *depca_signature[i] != '\0'; i++) {
		for (j = 0, k = 0; j < 16 && k < strlen(depca_signature[i]); j++) {
			if (depca_signature[i][k] == tmpstr[j]) {	/* track signature */
				k++;
			} else {	/* lost signature; begin search again */
				k = 0;
			}
		}
		if (k == strlen(depca_signature[i]))
			break;
	}

	/* Check if name string is valid, provided there's no PROM */
	if (name && *name && (i == unknown)) {
		for (i = 0; *depca_signature[i] != '\0'; i++) {
			if (strcmp(name, depca_signature[i]) == 0)
				break;
		}
	}

	return i;
}

/*
** Look for a special sequence in the Ethernet station address PROM that
** is common across all DEPCA products. Note that the original DEPCA needs
** its ROM address counter to be initialized and enabled. Only enable
** if the first address octet is a 0x08 - this minimises the chances of
** messing around with some other hardware, but it assumes that this DEPCA
** card initialized itself correctly.
**
** Search the Ethernet address ROM for the signature. Since the ROM address
** counter can start at an arbitrary point, the search must include the entire
** probe sequence length plus the (length_of_the_signature - 1).
** Stop the search IMMEDIATELY after the signature is found so that the
** PROM address counter is correctly positioned at the start of the
** ethernet address for later read out.
*/
static int __init DevicePresent(u_long ioaddr)
{
	union {
		struct {
			u32 a;
			u32 b;
		} llsig;
		char Sig[sizeof(u32) << 1];
	}
	dev;
	short sigLength = 0;
	s8 data;
	s16 nicsr;
	int i, j, status = 0;

	data = inb(DEPCA_PROM);	/* clear counter on DEPCA */
	data = inb(DEPCA_PROM);	/* read data */

	if (data == 0x08) {	/* Enable counter on DEPCA */
		nicsr = inb(DEPCA_NICSR);
		nicsr |= AAC;
		outb(nicsr, DEPCA_NICSR);
	}

	dev.llsig.a = ETH_PROM_SIG;
	dev.llsig.b = ETH_PROM_SIG;
	sigLength = sizeof(u32) << 1;

	for (i = 0, j = 0; j < sigLength && i < PROBE_LENGTH + sigLength - 1; i++) {
		data = inb(DEPCA_PROM);
		if (dev.Sig[j] == data) {	/* track signature */
			j++;
		} else {	/* lost signature; begin search again */
			if (data == dev.Sig[0]) {	/* rare case.... */
				j = 1;
			} else {
				j = 0;
			}
		}
	}

	if (j != sigLength) {
		status = -ENODEV;	/* search failed */
	}

	return status;
}

/*
** The DE100 and DE101 PROM accesses were made non-standard for some bizarre
** reason: access the upper half of the PROM with x=0; access the lower half
** with x=1.
*/
static int __init get_hw_addr(struct net_device *dev)
{
	u_long ioaddr = dev->base_addr;
	struct depca_private *lp = netdev_priv(dev);
	int i, k, tmp, status = 0;
	u_short j, x, chksum;

	x = (((lp->adapter == de100) || (lp->adapter == de101)) ? 1 : 0);

	for (i = 0, k = 0, j = 0; j < 3; j++) {
		k <<= 1;
		if (k > 0xffff)
			k -= 0xffff;

		k += (u_char) (tmp = inb(DEPCA_PROM + x));
		dev->dev_addr[i++] = (u_char) tmp;
		k += (u_short) ((tmp = inb(DEPCA_PROM + x)) << 8);
		dev->dev_addr[i++] = (u_char) tmp;

		if (k > 0xffff)
			k -= 0xffff;
	}
	if (k == 0xffff)
		k = 0;

	chksum = (u_char) inb(DEPCA_PROM + x);
	chksum |= (u_short) (inb(DEPCA_PROM + x) << 8);
	if (k != chksum)
		status = -1;

	return status;
}

/*
** Load a packet into the shared memory
*/
static int load_packet(struct net_device *dev, struct sk_buff *skb)
{
	struct depca_private *lp = netdev_priv(dev);
	int i, entry, end, len, status = NETDEV_TX_OK;

	entry = lp->tx_new;	/* Ring around buffer number. */
	end = (entry + (skb->len - 1) / TX_BUFF_SZ) & lp->txRingMask;
	if (!(readl(&lp->tx_ring[end].base) & T_OWN)) {	/* Enough room? */
		/*
		   ** Caution: the write order is important here... don't set up the
		   ** ownership rights until all the other information is in place.
		 */
		if (end < entry) {	/* wrapped buffer */
			len = (lp->txRingMask - entry + 1) * TX_BUFF_SZ;
			memcpy_toio(lp->tx_buff[entry], skb->data, len);
			memcpy_toio(lp->tx_buff[0], skb->data + len, skb->len - len);
		} else {	/* linear buffer */
			memcpy_toio(lp->tx_buff[entry], skb->data, skb->len);
		}

		/* set up the buffer descriptors */
		len = (skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;
		for (i = entry; i != end; i = (i+1) & lp->txRingMask) {
			/* clean out flags */
			writel(readl(&lp->tx_ring[i].base) & ~T_FLAGS, &lp->tx_ring[i].base);
			writew(0x0000, &lp->tx_ring[i].misc);	/* clears other error flags */
			writew(-TX_BUFF_SZ, &lp->tx_ring[i].length);	/* packet length in buffer */
			len -= TX_BUFF_SZ;
		}
		/* clean out flags */
		writel(readl(&lp->tx_ring[end].base) & ~T_FLAGS, &lp->tx_ring[end].base);
		writew(0x0000, &lp->tx_ring[end].misc);	/* clears other error flags */
		writew(-len, &lp->tx_ring[end].length);	/* packet length in last buff */

		/* start of packet */
		writel(readl(&lp->tx_ring[entry].base) | T_STP, &lp->tx_ring[entry].base);
		/* end of packet */
		writel(readl(&lp->tx_ring[end].base) | T_ENP, &lp->tx_ring[end].base);

		for (i = end; i != entry; --i) {
			/* ownership of packet */
			writel(readl(&lp->tx_ring[i].base) | T_OWN, &lp->tx_ring[i].base);
			if (i == 0)
				i = lp->txRingMask + 1;
		}
		writel(readl(&lp->tx_ring[entry].base) | T_OWN, &lp->tx_ring[entry].base);

		lp->tx_new = (++end) & lp->txRingMask;	/* update current pointers */
	} else {
		status = NETDEV_TX_LOCKED;
	}

	return status;
}

static void depca_dbg_open(struct net_device *dev)
{
	struct depca_private *lp = netdev_priv(dev);
	u_long ioaddr = dev->base_addr;
	struct depca_init *p = &lp->init_block;
	int i;

	if (depca_debug > 1) {
		/* Do not copy the shadow init block into shared memory */
		/* Debugging should not affect normal operation! */
		/* The shadow init block will get copied across during InitRestartDepca */
		printk("%s: depca open with irq %d\n", dev->name, dev->irq);
		printk("Descriptor head addresses (CPU):\n");
		printk("        0x%lx  0x%lx\n", (u_long) lp->rx_ring, (u_long) lp->tx_ring);
		printk("Descriptor addresses (CPU):\nRX: ");
		for (i = 0; i < lp->rxRingMask; i++) {
			if (i < 3) {
				printk("%p ", &lp->rx_ring[i].base);
			}
		}
		printk("...%p\n", &lp->rx_ring[i].base);
		printk("TX: ");
		for (i = 0; i < lp->txRingMask; i++) {
			if (i < 3) {
				printk("%p ", &lp->tx_ring[i].base);
			}
		}
		printk("...%p\n", &lp->tx_ring[i].base);
		printk("\nDescriptor buffers (Device):\nRX: ");
		for (i = 0; i < lp->rxRingMask; i++) {
			if (i < 3) {
				printk("0x%8.8x  ", readl(&lp->rx_ring[i].base));
			}
		}
		printk("...0x%8.8x\n", readl(&lp->rx_ring[i].base));
		printk("TX: ");
		for (i = 0; i < lp->txRingMask; i++) {
			if (i < 3) {
				printk("0x%8.8x  ", readl(&lp->tx_ring[i].base));
			}
		}
		printk("...0x%8.8x\n", readl(&lp->tx_ring[i].base));
		printk("Initialisation block at 0x%8.8lx(Phys)\n", lp->mem_start);
		printk("        mode: 0x%4.4x\n", p->mode);
		printk("        physical address: %pM\n", p->phys_addr);
		printk("        multicast hash table: ");
		for (i = 0; i < (HASH_TABLE_LEN >> 3) - 1; i++) {
			printk("%2.2x:", p->mcast_table[i]);
		}
		printk("%2.2x\n", p->mcast_table[i]);
		printk("        rx_ring at: 0x%8.8x\n", p->rx_ring);
		printk("        tx_ring at: 0x%8.8x\n", p->tx_ring);
		printk("buffers (Phys): 0x%8.8lx\n", lp->mem_start + lp->buffs_offset);
		printk("Ring size:\nRX: %d  Log2(rxRingMask): 0x%8.8x\n", (int) lp->rxRingMask + 1, lp->rx_rlen);
		printk("TX: %d  Log2(txRingMask): 0x%8.8x\n", (int) lp->txRingMask + 1, lp->tx_rlen);
		outw(CSR2, DEPCA_ADDR);
		printk("CSR2&1: 0x%4.4x", inw(DEPCA_DATA));
		outw(CSR1, DEPCA_ADDR);
		printk("%4.4x\n", inw(DEPCA_DATA));
		outw(CSR3, DEPCA_ADDR);
		printk("CSR3: 0x%4.4x\n", inw(DEPCA_DATA));
	}
}

/*
** Perform IOCTL call functions here. Some are privileged operations and the
** effective uid is checked in those cases.
** All multicast IOCTLs will not work here and are for testing purposes only.
*/
static int depca_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct depca_private *lp = netdev_priv(dev);
	struct depca_ioctl *ioc = (struct depca_ioctl *) &rq->ifr_ifru;
	int i, status = 0;
	u_long ioaddr = dev->base_addr;
	union {
		u8 addr[(HASH_TABLE_LEN * ETH_ALEN)];
		u16 sval[(HASH_TABLE_LEN * ETH_ALEN) >> 1];
		u32 lval[(HASH_TABLE_LEN * ETH_ALEN) >> 2];
	} tmp;
	unsigned long flags;
	void *buf;

	switch (ioc->cmd) {
	case DEPCA_GET_HWADDR:	/* Get the hardware address */
		for (i = 0; i < ETH_ALEN; i++) {
			tmp.addr[i] = dev->dev_addr[i];
		}
		ioc->len = ETH_ALEN;
		if (copy_to_user(ioc->data, tmp.addr, ioc->len))
			return -EFAULT;
		break;

	case DEPCA_SET_HWADDR:	/* Set the hardware address */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(tmp.addr, ioc->data, ETH_ALEN))
			return -EFAULT;
		for (i = 0; i < ETH_ALEN; i++) {
			dev->dev_addr[i] = tmp.addr[i];
		}
		netif_stop_queue(dev);
		while (lp->tx_old != lp->tx_new)
			cpu_relax();	/* Wait for the ring to empty */

		STOP_DEPCA;	/* Temporarily stop the depca.  */
		depca_init_ring(dev);	/* Initialize the descriptor rings */
		LoadCSRs(dev);	/* Reload CSR3 */
		InitRestartDepca(dev);	/* Resume normal operation. */
		netif_start_queue(dev);	/* Unlock the TX ring */
		break;

	case DEPCA_SET_PROM:	/* Set Promiscuous Mode */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		netif_stop_queue(dev);
		while (lp->tx_old != lp->tx_new)
			cpu_relax();	/* Wait for the ring to empty */

		STOP_DEPCA;	/* Temporarily stop the depca.  */
		depca_init_ring(dev);	/* Initialize the descriptor rings */
		lp->init_block.mode |= PROM;	/* Set promiscuous mode */

		LoadCSRs(dev);	/* Reload CSR3 */
		InitRestartDepca(dev);	/* Resume normal operation. */
		netif_start_queue(dev);	/* Unlock the TX ring */
		break;

	case DEPCA_CLR_PROM:	/* Clear Promiscuous Mode */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		netif_stop_queue(dev);
		while (lp->tx_old != lp->tx_new)
			cpu_relax();	/* Wait for the ring to empty */

		STOP_DEPCA;	/* Temporarily stop the depca.  */
		depca_init_ring(dev);	/* Initialize the descriptor rings */
		lp->init_block.mode &= ~PROM;	/* Clear promiscuous mode */

		LoadCSRs(dev);	/* Reload CSR3 */
		InitRestartDepca(dev);	/* Resume normal operation. */
		netif_start_queue(dev);	/* Unlock the TX ring */
		break;

	case DEPCA_SAY_BOO:	/* Say "Boo!" to the kernel log file */
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		printk("%s: Boo!\n", dev->name);
		break;

	case DEPCA_GET_MCA:	/* Get the multicast address table */
		ioc->len = (HASH_TABLE_LEN >> 3);
		if (copy_to_user(ioc->data, lp->init_block.mcast_table, ioc->len))
			return -EFAULT;
		break;

	case DEPCA_SET_MCA:	/* Set a multicast address */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (ioc->len >= HASH_TABLE_LEN)
			return -EINVAL;
		if (copy_from_user(tmp.addr, ioc->data, ETH_ALEN * ioc->len))
			return -EFAULT;
		set_multicast_list(dev);
		break;

	case DEPCA_CLR_MCA:	/* Clear all multicast addresses */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		set_multicast_list(dev);
		break;

	case DEPCA_MCA_EN:	/* Enable pass all multicast addressing */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		set_multicast_list(dev);
		break;

	case DEPCA_GET_STATS:	/* Get the driver statistics */
		ioc->len = sizeof(lp->pktStats);
		buf = kmalloc(ioc->len, GFP_KERNEL);
		if(!buf)
			return -ENOMEM;
		spin_lock_irqsave(&lp->lock, flags);
		memcpy(buf, &lp->pktStats, ioc->len);
		spin_unlock_irqrestore(&lp->lock, flags);
		if (copy_to_user(ioc->data, buf, ioc->len))
			status = -EFAULT;
		kfree(buf);
		break;

	case DEPCA_CLR_STATS:	/* Zero out the driver statistics */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		spin_lock_irqsave(&lp->lock, flags);
		memset(&lp->pktStats, 0, sizeof(lp->pktStats));
		spin_unlock_irqrestore(&lp->lock, flags);
		break;

	case DEPCA_GET_REG:	/* Get the DEPCA Registers */
		i = 0;
		tmp.sval[i++] = inw(DEPCA_NICSR);
		outw(CSR0, DEPCA_ADDR);	/* status register */
		tmp.sval[i++] = inw(DEPCA_DATA);
		memcpy(&tmp.sval[i], &lp->init_block, sizeof(struct depca_init));
		ioc->len = i + sizeof(struct depca_init);
		if (copy_to_user(ioc->data, tmp.addr, ioc->len))
			return -EFAULT;
		break;

	default:
		return -EOPNOTSUPP;
	}

	return status;
}

static int __init depca_module_init (void)
{
        int err = 0;

#ifdef CONFIG_MCA
        err = mca_register_driver (&depca_mca_driver);
#endif
#ifdef CONFIG_EISA
        err |= eisa_driver_register (&depca_eisa_driver);
#endif
	err |= platform_driver_register (&depca_isa_driver);
	depca_platform_probe ();

        return err;
}

static void __exit depca_module_exit (void)
{
	int i;
#ifdef CONFIG_MCA
        mca_unregister_driver (&depca_mca_driver);
#endif
#ifdef CONFIG_EISA
        eisa_driver_unregister (&depca_eisa_driver);
#endif
	platform_driver_unregister (&depca_isa_driver);

	for (i = 0; depca_io_ports[i].iobase; i++) {
		if (depca_io_ports[i].device) {
			depca_io_ports[i].device->dev.platform_data = NULL;
			platform_device_unregister (depca_io_ports[i].device);
			depca_io_ports[i].device = NULL;
		}
	}
}

module_init (depca_module_init);
module_exit (depca_module_exit);

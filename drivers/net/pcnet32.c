/* pcnet32.c: An AMD PCnet32 ethernet driver for linux. */
/*
 *	Copyright 1996-1999 Thomas Bogendoerfer
 *
 *	Derived from the lance driver written 1993,1994,1995 by Donald Becker.
 *
 *	Copyright 1993 United States Government as represented by the
 *	Director, National Security Agency.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 *	This driver is for PCnet32 and PCnetPCI based ethercards
 */
/**************************************************************************
 *  23 Oct, 2000.
 *  Fixed a few bugs, related to running the controller in 32bit mode.
 *
 *  Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *************************************************************************/

#define DRV_NAME	"pcnet32"
#define DRV_VERSION	"1.31c"
#define DRV_RELDATE	"01.Nov.2005"
#define PFX		DRV_NAME ": "

static const char *version =
DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE " tsbogend@alpha.franken.de\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

/*
 * PCI device identifiers for "new style" Linux PCI Device Drivers
 */
static struct pci_device_id pcnet32_pci_tbl[] = {
    { PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LANCE_HOME, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
    { PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LANCE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
    /*
     * Adapters that were sold with IBM's RS/6000 or pSeries hardware have
     * the incorrect vendor id.
     */
    { PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_AMD_LANCE, PCI_ANY_ID, PCI_ANY_ID,
	    PCI_CLASS_NETWORK_ETHERNET << 8, 0xffff00, 0 },
    { 0, }
};

MODULE_DEVICE_TABLE (pci, pcnet32_pci_tbl);

static int cards_found;

/*
 * VLB I/O addresses
 */
static unsigned int pcnet32_portlist[] __initdata =
	{ 0x300, 0x320, 0x340, 0x360, 0 };



static int pcnet32_debug = 0;
static int tx_start = 1; /* Mapping -- 0:20, 1:64, 2:128, 3:~220 (depends on chip vers) */
static int pcnet32vlb;	 /* check for VLB cards ? */

static struct net_device *pcnet32_dev;

static int max_interrupt_work = 2;
static int rx_copybreak = 200;

#define PCNET32_PORT_AUI      0x00
#define PCNET32_PORT_10BT     0x01
#define PCNET32_PORT_GPSI     0x02
#define PCNET32_PORT_MII      0x03

#define PCNET32_PORT_PORTSEL  0x03
#define PCNET32_PORT_ASEL     0x04
#define PCNET32_PORT_100      0x40
#define PCNET32_PORT_FD	      0x80

#define PCNET32_DMA_MASK 0xffffffff

#define PCNET32_WATCHDOG_TIMEOUT (jiffies + (2 * HZ))
#define PCNET32_BLINK_TIMEOUT	(jiffies + (HZ/4))

/*
 * table to translate option values from tulip
 * to internal options
 */
static unsigned char options_mapping[] = {
    PCNET32_PORT_ASEL,			   /*  0 Auto-select	  */
    PCNET32_PORT_AUI,			   /*  1 BNC/AUI	  */
    PCNET32_PORT_AUI,			   /*  2 AUI/BNC	  */
    PCNET32_PORT_ASEL,			   /*  3 not supported	  */
    PCNET32_PORT_10BT | PCNET32_PORT_FD,   /*  4 10baseT-FD	  */
    PCNET32_PORT_ASEL,			   /*  5 not supported	  */
    PCNET32_PORT_ASEL,			   /*  6 not supported	  */
    PCNET32_PORT_ASEL,			   /*  7 not supported	  */
    PCNET32_PORT_ASEL,			   /*  8 not supported	  */
    PCNET32_PORT_MII,			   /*  9 MII 10baseT	  */
    PCNET32_PORT_MII | PCNET32_PORT_FD,	   /* 10 MII 10baseT-FD	  */
    PCNET32_PORT_MII,			   /* 11 MII (autosel)	  */
    PCNET32_PORT_10BT,			   /* 12 10BaseT	  */
    PCNET32_PORT_MII | PCNET32_PORT_100,   /* 13 MII 100BaseTx	  */
    PCNET32_PORT_MII | PCNET32_PORT_100 | PCNET32_PORT_FD, /* 14 MII 100BaseTx-FD */
    PCNET32_PORT_ASEL			   /* 15 not supported	  */
};

static const char pcnet32_gstrings_test[][ETH_GSTRING_LEN] = {
    "Loopback test  (offline)"
};
#define PCNET32_TEST_LEN (sizeof(pcnet32_gstrings_test) / ETH_GSTRING_LEN)

#define PCNET32_NUM_REGS 168

#define MAX_UNITS 8	/* More are supported, limit only on options */
static int options[MAX_UNITS];
static int full_duplex[MAX_UNITS];
static int homepna[MAX_UNITS];

/*
 *				Theory of Operation
 *
 * This driver uses the same software structure as the normal lance
 * driver. So look for a verbose description in lance.c. The differences
 * to the normal lance driver is the use of the 32bit mode of PCnet32
 * and PCnetPCI chips. Because these chips are 32bit chips, there is no
 * 16MB limitation and we don't need bounce buffers.
 */

/*
 * History:
 * v0.01:  Initial version
 *	   only tested on Alpha Noname Board
 * v0.02:  changed IRQ handling for new interrupt scheme (dev_id)
 *	   tested on a ASUS SP3G
 * v0.10:  fixed an odd problem with the 79C974 in a Compaq Deskpro XL
 *	   looks like the 974 doesn't like stopping and restarting in a
 *	   short period of time; now we do a reinit of the lance; the
 *	   bug was triggered by doing ifconfig eth0 <ip> broadcast <addr>
 *	   and hangs the machine (thanks to Klaus Liedl for debugging)
 * v0.12:  by suggestion from Donald Becker: Renamed driver to pcnet32,
 *	   made it standalone (no need for lance.c)
 * v0.13:  added additional PCI detecting for special PCI devices (Compaq)
 * v0.14:  stripped down additional PCI probe (thanks to David C Niemi
 *	   and sveneric@xs4all.nl for testing this on their Compaq boxes)
 * v0.15:  added 79C965 (VLB) probe
 *	   added interrupt sharing for PCI chips
 * v0.16:  fixed set_multicast_list on Alpha machines
 * v0.17:  removed hack from dev.c; now pcnet32 uses ethif_probe in Space.c
 * v0.19:  changed setting of autoselect bit
 * v0.20:  removed additional Compaq PCI probe; there is now a working one
 *	   in arch/i386/bios32.c
 * v0.21:  added endian conversion for ppc, from work by cort@cs.nmt.edu
 * v0.22:  added printing of status to ring dump
 * v0.23:  changed enet_statistics to net_devive_stats
 * v0.90:  added multicast filter
 *	   added module support
 *	   changed irq probe to new style
 *	   added PCnetFast chip id
 *	   added fix for receive stalls with Intel saturn chipsets
 *	   added in-place rx skbs like in the tulip driver
 *	   minor cleanups
 * v0.91:  added PCnetFast+ chip id
 *	   back port to 2.0.x
 * v1.00:  added some stuff from Donald Becker's 2.0.34 version
 *	   added support for byte counters in net_dev_stats
 * v1.01:  do ring dumps, only when debugging the driver
 *	   increased the transmit timeout
 * v1.02:  fixed memory leak in pcnet32_init_ring()
 * v1.10:  workaround for stopped transmitter
 *	   added port selection for modules
 *	   detect special T1/E1 WAN card and setup port selection
 * v1.11:  fixed wrong checking of Tx errors
 * v1.20:  added check of return value kmalloc (cpeterso@cs.washington.edu)
 *	   added save original kmalloc addr for freeing (mcr@solidum.com)
 *	   added support for PCnetHome chip (joe@MIT.EDU)
 *	   rewritten PCI card detection
 *	   added dwio mode to get driver working on some PPC machines
 * v1.21:  added mii selection and mii ioctl
 * v1.22:  changed pci scanning code to make PPC people happy
 *	   fixed switching to 32bit mode in pcnet32_open() (thanks
 *	   to Michael Richard <mcr@solidum.com> for noticing this one)
 *	   added sub vendor/device id matching (thanks again to
 *	   Michael Richard <mcr@solidum.com>)
 *	   added chip id for 79c973/975 (thanks to Zach Brown <zab@zabbo.net>)
 * v1.23   fixed small bug, when manual selecting MII speed/duplex
 * v1.24   Applied Thomas' patch to use TxStartPoint and thus decrease TxFIFO
 *	   underflows.	Added tx_start_pt module parameter. Increased
 *	   TX_RING_SIZE from 16 to 32.	Added #ifdef'd code to use DXSUFLO
 *	   for FAST[+] chipsets. <kaf@fc.hp.com>
 * v1.24ac Added SMP spinlocking - Alan Cox <alan@redhat.com>
 * v1.25kf Added No Interrupt on successful Tx for some Tx's <kaf@fc.hp.com>
 * v1.26   Converted to pci_alloc_consistent, Jamey Hicks / George France
 *                                           <jamey@crl.dec.com>
 * -	   Fixed a few bugs, related to running the controller in 32bit mode.
 *	   23 Oct, 2000.  Carsten Langgaard, carstenl@mips.com
 *	   Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 * v1.26p  Fix oops on rmmod+insmod; plug i/o resource leak - Paul Gortmaker
 * v1.27   improved CSR/PROM address detection, lots of cleanups,
 * 	   new pcnet32vlb module option, HP-PARISC support,
 * 	   added module parameter descriptions,
 * 	   initial ethtool support - Helge Deller <deller@gmx.de>
 * v1.27a  Sun Feb 10 2002 Go Taniguchi <go@turbolinux.co.jp>
 *	   use alloc_etherdev and register_netdev
 *	   fix pci probe not increment cards_found
 *	   FD auto negotiate error workaround for xSeries250
 *	   clean up and using new mii module
 * v1.27b  Sep 30 2002 Kent Yoder <yoder1@us.ibm.com>
 * 	   Added timer for cable connection state changes.
 * v1.28   20 Feb 2004 Don Fry <brazilnut@us.ibm.com>
 *	   Jon Mason <jonmason@us.ibm.com>, Chinmay Albal <albal@in.ibm.com>
 *	   Now uses ethtool_ops, netif_msg_* and generic_mii_ioctl.
 *	   Fixes bogus 'Bus master arbitration failure', pci_[un]map_single
 *	   length errors, and transmit hangs.  Cleans up after errors in open.
 *	   Jim Lewis <jklewis@us.ibm.com> added ethernet loopback test.
 *	   Thomas Munck Steenholdt <tmus@tmus.dk> non-mii ioctl corrections.
 * v1.29   6 Apr 2004 Jim Lewis <jklewis@us.ibm.com> added physical
 *	   identification code (blink led's) and register dump.
 *	   Don Fry added timer for 971/972 so skbufs don't remain on tx ring
 *	   forever.
 * v1.30   18 May 2004 Don Fry removed timer and Last Transmit Interrupt
 *	   (ltint) as they added complexity and didn't give good throughput.
 * v1.30a  22 May 2004 Don Fry limit frames received during interrupt.
 * v1.30b  24 May 2004 Don Fry fix bogus tx carrier errors with 79c973,
 *	   assisted by Bruce Penrod <bmpenrod@endruntechnologies.com>.
 * v1.30c  25 May 2004 Don Fry added netif_wake_queue after pcnet32_restart.
 * v1.30d  01 Jun 2004 Don Fry discard oversize rx packets.
 * v1.30e  11 Jun 2004 Don Fry recover after fifo error and rx hang.
 * v1.30f  16 Jun 2004 Don Fry cleanup IRQ to allow 0 and 1 for PCI,
 * 	   expanding on suggestions from Ralf Baechle <ralf@linux-mips.org>,
 * 	   and Brian Murphy <brian@murphy.dk>.
 * v1.30g  22 Jun 2004 Patrick Simmons <psimmons@flash.net> added option
 *	   homepna for selecting HomePNA mode for PCNet/Home 79C978.
 * v1.30h  24 Jun 2004 Don Fry correctly select auto, speed, duplex in bcr32.
 * v1.30i  28 Jun 2004 Don Fry change to use module_param.
 * v1.30j  29 Apr 2005 Don Fry fix skb/map leak with loopback test.
 * v1.31   02 Sep 2005 Hubert WS Lin <wslin@tw.ibm.c0m> added set_ringparam().
 * v1.31a  12 Sep 2005 Hubert WS Lin <wslin@tw.ibm.c0m> set min ring size to 4
 *	   to allow loopback test to work unchanged.
 * v1.31b  06 Oct 2005 Don Fry changed alloc_ring to show name of device
 *	   if allocation fails
 * v1.31c  01 Nov 2005 Don Fry Allied Telesyn 2700/2701 FX are 100Mbit only.
 *	   Force 100Mbit FD if Auto (ASEL) is selected.
 *	   See Bugzilla 2669 and 4551.
 */


/*
 * Set the number of Tx and Rx buffers, using Log_2(# buffers).
 * Reasonable default values are 4 Tx buffers, and 16 Rx buffers.
 * That translates to 2 (4 == 2^^2) and 4 (16 == 2^^4).
 */
#ifndef PCNET32_LOG_TX_BUFFERS
#define PCNET32_LOG_TX_BUFFERS		4
#define PCNET32_LOG_RX_BUFFERS		5
#define PCNET32_LOG_MAX_TX_BUFFERS	9	/* 2^9 == 512 */
#define PCNET32_LOG_MAX_RX_BUFFERS	9
#endif

#define TX_RING_SIZE		(1 << (PCNET32_LOG_TX_BUFFERS))
#define TX_MAX_RING_SIZE	(1 << (PCNET32_LOG_MAX_TX_BUFFERS))

#define RX_RING_SIZE		(1 << (PCNET32_LOG_RX_BUFFERS))
#define RX_MAX_RING_SIZE	(1 << (PCNET32_LOG_MAX_RX_BUFFERS))

#define PKT_BUF_SZ		1544

/* Offsets from base I/O address. */
#define PCNET32_WIO_RDP		0x10
#define PCNET32_WIO_RAP		0x12
#define PCNET32_WIO_RESET	0x14
#define PCNET32_WIO_BDP		0x16

#define PCNET32_DWIO_RDP	0x10
#define PCNET32_DWIO_RAP	0x14
#define PCNET32_DWIO_RESET	0x18
#define PCNET32_DWIO_BDP	0x1C

#define PCNET32_TOTAL_SIZE	0x20

/* The PCNET32 Rx and Tx ring descriptors. */
struct pcnet32_rx_head {
    u32 base;
    s16 buf_length;
    s16 status;
    u32 msg_length;
    u32 reserved;
};

struct pcnet32_tx_head {
    u32 base;
    s16 length;
    s16 status;
    u32 misc;
    u32 reserved;
};

/* The PCNET32 32-Bit initialization block, described in databook. */
struct pcnet32_init_block {
    u16 mode;
    u16 tlen_rlen;
    u8	phys_addr[6];
    u16 reserved;
    u32 filter[2];
    /* Receive and transmit ring base, along with extra bits. */
    u32 rx_ring;
    u32 tx_ring;
};

/* PCnet32 access functions */
struct pcnet32_access {
    u16 (*read_csr)(unsigned long, int);
    void (*write_csr)(unsigned long, int, u16);
    u16 (*read_bcr)(unsigned long, int);
    void (*write_bcr)(unsigned long, int, u16);
    u16 (*read_rap)(unsigned long);
    void (*write_rap)(unsigned long, u16);
    void (*reset)(unsigned long);
};

/*
 * The first field of pcnet32_private is read by the ethernet device
 * so the structure should be allocated using pci_alloc_consistent().
 */
struct pcnet32_private {
    struct pcnet32_init_block init_block;
    /* The Tx and Rx ring entries must be aligned on 16-byte boundaries in 32bit mode. */
    struct pcnet32_rx_head    *rx_ring;
    struct pcnet32_tx_head    *tx_ring;
    dma_addr_t		dma_addr;	/* DMA address of beginning of this
					   object, returned by
					   pci_alloc_consistent */
    struct pci_dev	*pci_dev;	/* Pointer to the associated pci device
					   structure */
    const char		*name;
    /* The saved address of a sent-in-place packet/buffer, for skfree(). */
    struct sk_buff	**tx_skbuff;
    struct sk_buff	**rx_skbuff;
    dma_addr_t		*tx_dma_addr;
    dma_addr_t		*rx_dma_addr;
    struct pcnet32_access	a;
    spinlock_t		lock;		/* Guard lock */
    unsigned int	cur_rx, cur_tx;	/* The next free ring entry */
    unsigned int	rx_ring_size;	/* current rx ring size */
    unsigned int	tx_ring_size;	/* current tx ring size */
    unsigned int	rx_mod_mask;	/* rx ring modular mask */
    unsigned int	tx_mod_mask;	/* tx ring modular mask */
    unsigned short	rx_len_bits;
    unsigned short	tx_len_bits;
    dma_addr_t		rx_ring_dma_addr;
    dma_addr_t		tx_ring_dma_addr;
    unsigned int	dirty_rx, dirty_tx; /* The ring entries to be free()ed. */
    struct net_device_stats stats;
    char		tx_full;
    int			options;
    unsigned int	shared_irq:1,	/* shared irq possible */
			dxsuflo:1,	/* disable transmit stop on uflo */
			mii:1;		/* mii port available */
    struct net_device	*next;
    struct mii_if_info	mii_if;
    struct timer_list	watchdog_timer;
    struct timer_list	blink_timer;
    u32			msg_enable;	/* debug message level */
};

static void pcnet32_probe_vlbus(void);
static int  pcnet32_probe_pci(struct pci_dev *, const struct pci_device_id *);
static int  pcnet32_probe1(unsigned long, int, struct pci_dev *);
static int  pcnet32_open(struct net_device *);
static int  pcnet32_init_ring(struct net_device *);
static int  pcnet32_start_xmit(struct sk_buff *, struct net_device *);
static int  pcnet32_rx(struct net_device *);
static void pcnet32_tx_timeout (struct net_device *dev);
static irqreturn_t pcnet32_interrupt(int, void *, struct pt_regs *);
static int  pcnet32_close(struct net_device *);
static struct net_device_stats *pcnet32_get_stats(struct net_device *);
static void pcnet32_load_multicast(struct net_device *dev);
static void pcnet32_set_multicast_list(struct net_device *);
static int  pcnet32_ioctl(struct net_device *, struct ifreq *, int);
static void pcnet32_watchdog(struct net_device *);
static int mdio_read(struct net_device *dev, int phy_id, int reg_num);
static void mdio_write(struct net_device *dev, int phy_id, int reg_num, int val);
static void pcnet32_restart(struct net_device *dev, unsigned int csr0_bits);
static void pcnet32_ethtool_test(struct net_device *dev,
	struct ethtool_test *eth_test, u64 *data);
static int pcnet32_loopback_test(struct net_device *dev, uint64_t *data1);
static int pcnet32_phys_id(struct net_device *dev, u32 data);
static void pcnet32_led_blink_callback(struct net_device *dev);
static int pcnet32_get_regs_len(struct net_device *dev);
static void pcnet32_get_regs(struct net_device *dev, struct ethtool_regs *regs,
	void *ptr);
static void pcnet32_purge_tx_ring(struct net_device *dev);
static int pcnet32_alloc_ring(struct net_device *dev, char *name);
static void pcnet32_free_ring(struct net_device *dev);


enum pci_flags_bit {
    PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
    PCI_ADDR0=0x10<<0, PCI_ADDR1=0x10<<1, PCI_ADDR2=0x10<<2, PCI_ADDR3=0x10<<3,
};


static u16 pcnet32_wio_read_csr (unsigned long addr, int index)
{
    outw (index, addr+PCNET32_WIO_RAP);
    return inw (addr+PCNET32_WIO_RDP);
}

static void pcnet32_wio_write_csr (unsigned long addr, int index, u16 val)
{
    outw (index, addr+PCNET32_WIO_RAP);
    outw (val, addr+PCNET32_WIO_RDP);
}

static u16 pcnet32_wio_read_bcr (unsigned long addr, int index)
{
    outw (index, addr+PCNET32_WIO_RAP);
    return inw (addr+PCNET32_WIO_BDP);
}

static void pcnet32_wio_write_bcr (unsigned long addr, int index, u16 val)
{
    outw (index, addr+PCNET32_WIO_RAP);
    outw (val, addr+PCNET32_WIO_BDP);
}

static u16 pcnet32_wio_read_rap (unsigned long addr)
{
    return inw (addr+PCNET32_WIO_RAP);
}

static void pcnet32_wio_write_rap (unsigned long addr, u16 val)
{
    outw (val, addr+PCNET32_WIO_RAP);
}

static void pcnet32_wio_reset (unsigned long addr)
{
    inw (addr+PCNET32_WIO_RESET);
}

static int pcnet32_wio_check (unsigned long addr)
{
    outw (88, addr+PCNET32_WIO_RAP);
    return (inw (addr+PCNET32_WIO_RAP) == 88);
}

static struct pcnet32_access pcnet32_wio = {
    .read_csr	= pcnet32_wio_read_csr,
    .write_csr	= pcnet32_wio_write_csr,
    .read_bcr	= pcnet32_wio_read_bcr,
    .write_bcr	= pcnet32_wio_write_bcr,
    .read_rap	= pcnet32_wio_read_rap,
    .write_rap	= pcnet32_wio_write_rap,
    .reset	= pcnet32_wio_reset
};

static u16 pcnet32_dwio_read_csr (unsigned long addr, int index)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    return (inl (addr+PCNET32_DWIO_RDP) & 0xffff);
}

static void pcnet32_dwio_write_csr (unsigned long addr, int index, u16 val)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    outl (val, addr+PCNET32_DWIO_RDP);
}

static u16 pcnet32_dwio_read_bcr (unsigned long addr, int index)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    return (inl (addr+PCNET32_DWIO_BDP) & 0xffff);
}

static void pcnet32_dwio_write_bcr (unsigned long addr, int index, u16 val)
{
    outl (index, addr+PCNET32_DWIO_RAP);
    outl (val, addr+PCNET32_DWIO_BDP);
}

static u16 pcnet32_dwio_read_rap (unsigned long addr)
{
    return (inl (addr+PCNET32_DWIO_RAP) & 0xffff);
}

static void pcnet32_dwio_write_rap (unsigned long addr, u16 val)
{
    outl (val, addr+PCNET32_DWIO_RAP);
}

static void pcnet32_dwio_reset (unsigned long addr)
{
    inl (addr+PCNET32_DWIO_RESET);
}

static int pcnet32_dwio_check (unsigned long addr)
{
    outl (88, addr+PCNET32_DWIO_RAP);
    return ((inl (addr+PCNET32_DWIO_RAP) & 0xffff) == 88);
}

static struct pcnet32_access pcnet32_dwio = {
    .read_csr	= pcnet32_dwio_read_csr,
    .write_csr	= pcnet32_dwio_write_csr,
    .read_bcr	= pcnet32_dwio_read_bcr,
    .write_bcr	= pcnet32_dwio_write_bcr,
    .read_rap	= pcnet32_dwio_read_rap,
    .write_rap	= pcnet32_dwio_write_rap,
    .reset	= pcnet32_dwio_reset
};

#ifdef CONFIG_NET_POLL_CONTROLLER
static void pcnet32_poll_controller(struct net_device *dev)
{
    disable_irq(dev->irq);
    pcnet32_interrupt(0, dev, NULL);
    enable_irq(dev->irq);
}
#endif


static int pcnet32_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long flags;
    int r = -EOPNOTSUPP;

    if (lp->mii) {
	spin_lock_irqsave(&lp->lock, flags);
	mii_ethtool_gset(&lp->mii_if, cmd);
	spin_unlock_irqrestore(&lp->lock, flags);
	r = 0;
    }
    return r;
}

static int pcnet32_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long flags;
    int r = -EOPNOTSUPP;

    if (lp->mii) {
	spin_lock_irqsave(&lp->lock, flags);
	r = mii_ethtool_sset(&lp->mii_if, cmd);
	spin_unlock_irqrestore(&lp->lock, flags);
    }
    return r;
}

static void pcnet32_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
    struct pcnet32_private *lp = dev->priv;

    strcpy (info->driver, DRV_NAME);
    strcpy (info->version, DRV_VERSION);
    if (lp->pci_dev)
	strcpy (info->bus_info, pci_name(lp->pci_dev));
    else
	sprintf(info->bus_info, "VLB 0x%lx", dev->base_addr);
}

static u32 pcnet32_get_link(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long flags;
    int r;

    spin_lock_irqsave(&lp->lock, flags);
    if (lp->mii) {
	r = mii_link_ok(&lp->mii_if);
    } else {
	ulong ioaddr = dev->base_addr;	/* card base I/O address */
	r = (lp->a.read_bcr(ioaddr, 4) != 0xc0);
    }
    spin_unlock_irqrestore(&lp->lock, flags);

    return r;
}

static u32 pcnet32_get_msglevel(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    return lp->msg_enable;
}

static void pcnet32_set_msglevel(struct net_device *dev, u32 value)
{
    struct pcnet32_private *lp = dev->priv;
    lp->msg_enable = value;
}

static int pcnet32_nway_reset(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long flags;
    int r = -EOPNOTSUPP;

    if (lp->mii) {
	spin_lock_irqsave(&lp->lock, flags);
	r = mii_nway_restart(&lp->mii_if);
	spin_unlock_irqrestore(&lp->lock, flags);
    }
    return r;
}

static void pcnet32_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
    struct pcnet32_private *lp = dev->priv;

    ering->tx_max_pending = TX_MAX_RING_SIZE - 1;
    ering->tx_pending = lp->tx_ring_size - 1;
    ering->rx_max_pending = RX_MAX_RING_SIZE - 1;
    ering->rx_pending = lp->rx_ring_size - 1;
}

static int pcnet32_set_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long flags;
    int i;

    if (ering->rx_mini_pending || ering->rx_jumbo_pending)
	return -EINVAL;

    if (netif_running(dev))
	pcnet32_close(dev);

    spin_lock_irqsave(&lp->lock, flags);
    pcnet32_free_ring(dev);
    lp->tx_ring_size = min(ering->tx_pending, (unsigned int) TX_MAX_RING_SIZE);
    lp->rx_ring_size = min(ering->rx_pending, (unsigned int) RX_MAX_RING_SIZE);

    /* set the minimum ring size to 4, to allow the loopback test to work
     * unchanged.
     */
    for (i = 2; i <= PCNET32_LOG_MAX_TX_BUFFERS; i++) {
	if (lp->tx_ring_size <= (1 << i))
	    break;
    }
    lp->tx_ring_size = (1 << i);
    lp->tx_mod_mask = lp->tx_ring_size - 1;
    lp->tx_len_bits = (i << 12);

    for (i = 2; i <= PCNET32_LOG_MAX_RX_BUFFERS; i++) {
	if (lp->rx_ring_size <= (1 << i))
	    break;
    }
    lp->rx_ring_size = (1 << i);
    lp->rx_mod_mask = lp->rx_ring_size - 1;
    lp->rx_len_bits = (i << 4);

    if (pcnet32_alloc_ring(dev, dev->name)) {
	pcnet32_free_ring(dev);
	spin_unlock_irqrestore(&lp->lock, flags);
	return -ENOMEM;
    }

    spin_unlock_irqrestore(&lp->lock, flags);

    if (pcnet32_debug & NETIF_MSG_DRV)
	printk(KERN_INFO PFX "%s: Ring Param Settings: RX: %d, TX: %d\n",
	       dev->name, lp->rx_ring_size, lp->tx_ring_size);

    if (netif_running(dev))
	pcnet32_open(dev);

    return 0;
}

static void pcnet32_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
    memcpy(data, pcnet32_gstrings_test, sizeof(pcnet32_gstrings_test));
}

static int pcnet32_self_test_count(struct net_device *dev)
{
    return PCNET32_TEST_LEN;
}

static void pcnet32_ethtool_test(struct net_device *dev,
	struct ethtool_test *test, u64 *data)
{
    struct pcnet32_private *lp = dev->priv;
    int rc;

    if (test->flags == ETH_TEST_FL_OFFLINE) {
	rc = pcnet32_loopback_test(dev, data);
	if (rc) {
	    if (netif_msg_hw(lp))
		printk(KERN_DEBUG "%s: Loopback test failed.\n", dev->name);
	    test->flags |= ETH_TEST_FL_FAILED;
	} else if (netif_msg_hw(lp))
	    printk(KERN_DEBUG "%s: Loopback test passed.\n", dev->name);
    } else if (netif_msg_hw(lp))
	printk(KERN_DEBUG "%s: No tests to run (specify 'Offline' on ethtool).",	    dev->name);
} /* end pcnet32_ethtool_test */

static int pcnet32_loopback_test(struct net_device *dev, uint64_t *data1)
{
    struct pcnet32_private *lp = dev->priv;
    struct pcnet32_access *a = &lp->a;	/* access to registers */
    ulong ioaddr = dev->base_addr;	/* card base I/O address */
    struct sk_buff *skb;		/* sk buff */
    int x, i;				/* counters */
    int numbuffs = 4;			/* number of TX/RX buffers and descs */
    u16 status = 0x8300;		/* TX ring status */
    u16 teststatus;			/* test of ring status */
    int rc;				/* return code */
    int size;				/* size of packets */
    unsigned char *packet;		/* source packet data */
    static int data_len = 60;		/* length of source packets */
    unsigned long flags;
    unsigned long ticks;

    *data1 = 1;			/* status of test, default to fail */
    rc = 1;			/* default to fail */

    if (netif_running(dev))
	pcnet32_close(dev);

    spin_lock_irqsave(&lp->lock, flags);

    /* Reset the PCNET32 */
    lp->a.reset (ioaddr);

    /* switch pcnet32 to 32bit mode */
    lp->a.write_bcr (ioaddr, 20, 2);

    lp->init_block.mode = le16_to_cpu((lp->options & PCNET32_PORT_PORTSEL) << 7);
    lp->init_block.filter[0] = 0;
    lp->init_block.filter[1] = 0;

    /* purge & init rings but don't actually restart */
    pcnet32_restart(dev, 0x0000);

    lp->a.write_csr(ioaddr, 0, 0x0004);	/* Set STOP bit */

    /* Initialize Transmit buffers. */
    size = data_len + 15;
    for (x=0; x<numbuffs; x++) {
	if (!(skb = dev_alloc_skb(size))) {
	    if (netif_msg_hw(lp))
		printk(KERN_DEBUG "%s: Cannot allocate skb at line: %d!\n",
		    dev->name, __LINE__);
	    goto clean_up;
	} else {
	    packet = skb->data;
	    skb_put(skb, size);		/* create space for data */
	    lp->tx_skbuff[x] = skb;
	    lp->tx_ring[x].length = le16_to_cpu(-skb->len);
	    lp->tx_ring[x].misc = 0;

            /* put DA and SA into the skb */
	    for (i=0; i<6; i++)
		*packet++ = dev->dev_addr[i];
	    for (i=0; i<6; i++)
		*packet++ = dev->dev_addr[i];
	    /* type */
	    *packet++ = 0x08;
	    *packet++ = 0x06;
	    /* packet number */
	    *packet++ = x;
	    /* fill packet with data */
	    for (i=0; i<data_len; i++)
		*packet++ = i;

	    lp->tx_dma_addr[x] = pci_map_single(lp->pci_dev, skb->data,
		    skb->len, PCI_DMA_TODEVICE);
	    lp->tx_ring[x].base = (u32)le32_to_cpu(lp->tx_dma_addr[x]);
	    wmb(); /* Make sure owner changes after all others are visible */
	    lp->tx_ring[x].status = le16_to_cpu(status);
	}
    }

    x = a->read_bcr(ioaddr, 32);	/* set internal loopback in BSR32 */
    x = x | 0x0002;
    a->write_bcr(ioaddr, 32, x);

    lp->a.write_csr (ioaddr, 15, 0x0044);	/* set int loopback in CSR15 */

    teststatus = le16_to_cpu(0x8000);
    lp->a.write_csr(ioaddr, 0, 0x0002);		/* Set STRT bit */

    /* Check status of descriptors */
    for (x=0; x<numbuffs; x++) {
	ticks = 0;
	rmb();
	while ((lp->rx_ring[x].status & teststatus) && (ticks < 200)) {
	    spin_unlock_irqrestore(&lp->lock, flags);
	    mdelay(1);
	    spin_lock_irqsave(&lp->lock, flags);
	    rmb();
	    ticks++;
	}
	if (ticks == 200) {
	    if (netif_msg_hw(lp))
		printk("%s: Desc %d failed to reset!\n",dev->name,x);
	    break;
	}
    }

    lp->a.write_csr(ioaddr, 0, 0x0004);		/* Set STOP bit */
    wmb();
    if (netif_msg_hw(lp) && netif_msg_pktdata(lp)) {
	printk(KERN_DEBUG "%s: RX loopback packets:\n", dev->name);

	for (x=0; x<numbuffs; x++) {
	    printk(KERN_DEBUG "%s: Packet %d:\n", dev->name, x);
	    skb = lp->rx_skbuff[x];
	    for (i=0; i<size; i++) {
		printk("%02x ", *(skb->data+i));
	    }
	    printk("\n");
	}
    }

    x = 0;
    rc = 0;
    while (x<numbuffs && !rc) {
	skb = lp->rx_skbuff[x];
	packet = lp->tx_skbuff[x]->data;
	for (i=0; i<size; i++) {
	    if (*(skb->data+i) != packet[i]) {
		if (netif_msg_hw(lp))
		    printk(KERN_DEBUG "%s: Error in compare! %2x - %02x %02x\n",
			    dev->name, i, *(skb->data+i), packet[i]);
		rc = 1;
		break;
	    }
	}
	x++;
    }
    if (!rc) {
	*data1 = 0;
    }

clean_up:
    pcnet32_purge_tx_ring(dev);
    x = a->read_csr(ioaddr, 15) & 0xFFFF;
    a->write_csr(ioaddr, 15, (x & ~0x0044));	/* reset bits 6 and 2 */

    x = a->read_bcr(ioaddr, 32);		/* reset internal loopback */
    x = x & ~0x0002;
    a->write_bcr(ioaddr, 32, x);

    spin_unlock_irqrestore(&lp->lock, flags);

    if (netif_running(dev)) {
	pcnet32_open(dev);
    } else {
	lp->a.write_bcr (ioaddr, 20, 4);	/* return to 16bit mode */
    }

    return(rc);
} /* end pcnet32_loopback_test  */

static void pcnet32_led_blink_callback(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    struct pcnet32_access *a = &lp->a;
    ulong ioaddr = dev->base_addr;
    unsigned long flags;
    int i;

    spin_lock_irqsave(&lp->lock, flags);
    for (i=4; i<8; i++) {
	a->write_bcr(ioaddr, i, a->read_bcr(ioaddr, i) ^ 0x4000);
    }
    spin_unlock_irqrestore(&lp->lock, flags);

    mod_timer(&lp->blink_timer, PCNET32_BLINK_TIMEOUT);
}

static int pcnet32_phys_id(struct net_device *dev, u32 data)
{
    struct pcnet32_private *lp = dev->priv;
    struct pcnet32_access *a = &lp->a;
    ulong ioaddr = dev->base_addr;
    unsigned long flags;
    int i, regs[4];

    if (!lp->blink_timer.function) {
	init_timer(&lp->blink_timer);
	lp->blink_timer.function = (void *) pcnet32_led_blink_callback;
	lp->blink_timer.data = (unsigned long) dev;
    }

    /* Save the current value of the bcrs */
    spin_lock_irqsave(&lp->lock, flags);
    for (i=4; i<8; i++) {
	regs[i-4] = a->read_bcr(ioaddr, i);
    }
    spin_unlock_irqrestore(&lp->lock, flags);

    mod_timer(&lp->blink_timer, jiffies);
    set_current_state(TASK_INTERRUPTIBLE);

    if ((!data) || (data > (u32)(MAX_SCHEDULE_TIMEOUT / HZ)))
    data = (u32)(MAX_SCHEDULE_TIMEOUT / HZ);

    msleep_interruptible(data * 1000);
    del_timer_sync(&lp->blink_timer);

    /* Restore the original value of the bcrs */
    spin_lock_irqsave(&lp->lock, flags);
    for (i=4; i<8; i++) {
	a->write_bcr(ioaddr, i, regs[i-4]);
    }
    spin_unlock_irqrestore(&lp->lock, flags);

    return 0;
}

static int pcnet32_get_regs_len(struct net_device *dev)
{
    return(PCNET32_NUM_REGS * sizeof(u16));
}

static void pcnet32_get_regs(struct net_device *dev, struct ethtool_regs *regs,
	void *ptr)
{
    int i, csr0;
    u16 *buff = ptr;
    struct pcnet32_private *lp = dev->priv;
    struct pcnet32_access *a = &lp->a;
    ulong ioaddr = dev->base_addr;
    int ticks;
    unsigned long flags;

    spin_lock_irqsave(&lp->lock, flags);

    csr0 = a->read_csr(ioaddr, 0);
    if (!(csr0 & 0x0004)) {	/* If not stopped */
	/* set SUSPEND (SPND) - CSR5 bit 0 */
	a->write_csr(ioaddr, 5, 0x0001);

	/* poll waiting for bit to be set */
	ticks = 0;
	while (!(a->read_csr(ioaddr, 5) & 0x0001)) {
	    spin_unlock_irqrestore(&lp->lock, flags);
	    mdelay(1);
	    spin_lock_irqsave(&lp->lock, flags);
	    ticks++;
	    if (ticks > 200) {
		if (netif_msg_hw(lp))
		    printk(KERN_DEBUG "%s: Error getting into suspend!\n",
			    dev->name);
		break;
	    }
	}
    }

    /* read address PROM */
    for (i=0; i<16; i += 2)
	*buff++ = inw(ioaddr + i);

    /* read control and status registers */
    for (i=0; i<90; i++) {
	*buff++ = a->read_csr(ioaddr, i);
    }

    *buff++ = a->read_csr(ioaddr, 112);
    *buff++ = a->read_csr(ioaddr, 114);

    /* read bus configuration registers */
    for (i=0; i<30; i++) {
	*buff++ = a->read_bcr(ioaddr, i);
    }
    *buff++ = 0;	/* skip bcr30 so as not to hang 79C976 */
    for (i=31; i<36; i++) {
	*buff++ = a->read_bcr(ioaddr, i);
    }

    /* read mii phy registers */
    if (lp->mii) {
	for (i=0; i<32; i++) {
	    lp->a.write_bcr(ioaddr, 33, ((lp->mii_if.phy_id) << 5) | i);
	    *buff++ = lp->a.read_bcr(ioaddr, 34);
	}
    }

    if (!(csr0 & 0x0004)) {	/* If not stopped */
	/* clear SUSPEND (SPND) - CSR5 bit 0 */
	a->write_csr(ioaddr, 5, 0x0000);
    }

    i = buff - (u16 *)ptr;
    for (; i < PCNET32_NUM_REGS; i++)
	*buff++ = 0;

    spin_unlock_irqrestore(&lp->lock, flags);
}

static struct ethtool_ops pcnet32_ethtool_ops = {
    .get_settings	= pcnet32_get_settings,
    .set_settings	= pcnet32_set_settings,
    .get_drvinfo	= pcnet32_get_drvinfo,
    .get_msglevel	= pcnet32_get_msglevel,
    .set_msglevel	= pcnet32_set_msglevel,
    .nway_reset		= pcnet32_nway_reset,
    .get_link		= pcnet32_get_link,
    .get_ringparam	= pcnet32_get_ringparam,
    .set_ringparam	= pcnet32_set_ringparam,
    .get_tx_csum	= ethtool_op_get_tx_csum,
    .get_sg		= ethtool_op_get_sg,
    .get_tso		= ethtool_op_get_tso,
    .get_strings	= pcnet32_get_strings,
    .self_test_count	= pcnet32_self_test_count,
    .self_test		= pcnet32_ethtool_test,
    .phys_id		= pcnet32_phys_id,
    .get_regs_len	= pcnet32_get_regs_len,
    .get_regs		= pcnet32_get_regs,
    .get_perm_addr	= ethtool_op_get_perm_addr,
};

/* only probes for non-PCI devices, the rest are handled by
 * pci_register_driver via pcnet32_probe_pci */

static void __devinit
pcnet32_probe_vlbus(void)
{
    unsigned int *port, ioaddr;

    /* search for PCnet32 VLB cards at known addresses */
    for (port = pcnet32_portlist; (ioaddr = *port); port++) {
	if (request_region(ioaddr, PCNET32_TOTAL_SIZE, "pcnet32_probe_vlbus")) {
	    /* check if there is really a pcnet chip on that ioaddr */
	    if ((inb(ioaddr + 14) == 0x57) && (inb(ioaddr + 15) == 0x57)) {
		pcnet32_probe1(ioaddr, 0, NULL);
	    } else {
		release_region(ioaddr, PCNET32_TOTAL_SIZE);
	    }
	}
    }
}


static int __devinit
pcnet32_probe_pci(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    unsigned long ioaddr;
    int err;

    err = pci_enable_device(pdev);
    if (err < 0) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_ERR PFX "failed to enable device -- err=%d\n", err);
	return err;
    }
    pci_set_master(pdev);

    ioaddr = pci_resource_start (pdev, 0);
    if (!ioaddr) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk (KERN_ERR PFX "card has no PCI IO resources, aborting\n");
	return -ENODEV;
    }

    if (!pci_dma_supported(pdev, PCNET32_DMA_MASK)) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_ERR PFX "architecture does not support 32bit PCI busmaster DMA\n");
	return -ENODEV;
    }
    if (request_region(ioaddr, PCNET32_TOTAL_SIZE, "pcnet32_probe_pci") == NULL) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_ERR PFX "io address range already allocated\n");
	return -EBUSY;
    }

    err =  pcnet32_probe1(ioaddr, 1, pdev);
    if (err < 0) {
	pci_disable_device(pdev);
    }
    return err;
}


/* pcnet32_probe1
 *  Called from both pcnet32_probe_vlbus and pcnet_probe_pci.
 *  pdev will be NULL when called from pcnet32_probe_vlbus.
 */
static int __devinit
pcnet32_probe1(unsigned long ioaddr, int shared, struct pci_dev *pdev)
{
    struct pcnet32_private *lp;
    dma_addr_t lp_dma_addr;
    int i, media;
    int fdx, mii, fset, dxsuflo;
    int chip_version;
    char *chipname;
    struct net_device *dev;
    struct pcnet32_access *a = NULL;
    u8 promaddr[6];
    int ret = -ENODEV;

    /* reset the chip */
    pcnet32_wio_reset(ioaddr);

    /* NOTE: 16-bit check is first, otherwise some older PCnet chips fail */
    if (pcnet32_wio_read_csr(ioaddr, 0) == 4 && pcnet32_wio_check(ioaddr)) {
	a = &pcnet32_wio;
    } else {
	pcnet32_dwio_reset(ioaddr);
	if (pcnet32_dwio_read_csr(ioaddr, 0) == 4 && pcnet32_dwio_check(ioaddr)) {
	    a = &pcnet32_dwio;
	} else
	    goto err_release_region;
    }

    chip_version = a->read_csr(ioaddr, 88) | (a->read_csr(ioaddr,89) << 16);
    if ((pcnet32_debug & NETIF_MSG_PROBE) && (pcnet32_debug & NETIF_MSG_HW))
	printk(KERN_INFO "  PCnet chip version is %#x.\n", chip_version);
    if ((chip_version & 0xfff) != 0x003) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_INFO PFX "Unsupported chip version.\n");
	goto err_release_region;
    }

    /* initialize variables */
    fdx = mii = fset = dxsuflo = 0;
    chip_version = (chip_version >> 12) & 0xffff;

    switch (chip_version) {
    case 0x2420:
	chipname = "PCnet/PCI 79C970"; /* PCI */
	break;
    case 0x2430:
	if (shared)
	    chipname = "PCnet/PCI 79C970"; /* 970 gives the wrong chip id back */
	else
	    chipname = "PCnet/32 79C965"; /* 486/VL bus */
	break;
    case 0x2621:
	chipname = "PCnet/PCI II 79C970A"; /* PCI */
	fdx = 1;
	break;
    case 0x2623:
	chipname = "PCnet/FAST 79C971"; /* PCI */
	fdx = 1; mii = 1; fset = 1;
	break;
    case 0x2624:
	chipname = "PCnet/FAST+ 79C972"; /* PCI */
	fdx = 1; mii = 1; fset = 1;
	break;
    case 0x2625:
	chipname = "PCnet/FAST III 79C973"; /* PCI */
	fdx = 1; mii = 1;
	break;
    case 0x2626:
	chipname = "PCnet/Home 79C978"; /* PCI */
	fdx = 1;
	/*
	 * This is based on specs published at www.amd.com.  This section
	 * assumes that a card with a 79C978 wants to go into standard
	 * ethernet mode.  The 79C978 can also go into 1Mb HomePNA mode,
	 * and the module option homepna=1 can select this instead.
	 */
	media = a->read_bcr(ioaddr, 49);
	media &= ~3;		/* default to 10Mb ethernet */
	if (cards_found < MAX_UNITS && homepna[cards_found])
	    media |= 1; 	/* switch to home wiring mode */
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_DEBUG PFX "media set to %sMbit mode.\n", 
		    (media & 1) ? "1" : "10");
	a->write_bcr(ioaddr, 49, media);
	break;
    case 0x2627:
	chipname = "PCnet/FAST III 79C975"; /* PCI */
	fdx = 1; mii = 1;
	break;
    case 0x2628:
	chipname = "PCnet/PRO 79C976";
	fdx = 1; mii = 1;
	break;
    default:
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_INFO PFX "PCnet version %#x, no PCnet32 chip.\n",
		    chip_version);
	goto err_release_region;
    }

    /*
     *	On selected chips turn on the BCR18:NOUFLO bit. This stops transmit
     *	starting until the packet is loaded. Strike one for reliability, lose
     *	one for latency - although on PCI this isnt a big loss. Older chips
     *	have FIFO's smaller than a packet, so you can't do this.
     *	Turn on BCR18:BurstRdEn and BCR18:BurstWrEn.
     */

    if (fset) {
	a->write_bcr(ioaddr, 18, (a->read_bcr(ioaddr, 18) | 0x0860));
	a->write_csr(ioaddr, 80, (a->read_csr(ioaddr, 80) & 0x0C00) | 0x0c00);
	dxsuflo = 1;
    }

    dev = alloc_etherdev(0);
    if (!dev) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_ERR PFX "Memory allocation failed.\n");
	ret = -ENOMEM;
	goto err_release_region;
    }
    SET_NETDEV_DEV(dev, &pdev->dev);

    if (pcnet32_debug & NETIF_MSG_PROBE)
	printk(KERN_INFO PFX "%s at %#3lx,", chipname, ioaddr);

    /* In most chips, after a chip reset, the ethernet address is read from the
     * station address PROM at the base address and programmed into the
     * "Physical Address Registers" CSR12-14.
     * As a precautionary measure, we read the PROM values and complain if
     * they disagree with the CSRs.  Either way, we use the CSR values, and
     * double check that they are valid.
     */
    for (i = 0; i < 3; i++) {
	unsigned int val;
	val = a->read_csr(ioaddr, i+12) & 0x0ffff;
	/* There may be endianness issues here. */
	dev->dev_addr[2*i] = val & 0x0ff;
	dev->dev_addr[2*i+1] = (val >> 8) & 0x0ff;
    }

    /* read PROM address and compare with CSR address */
    for (i = 0; i < 6; i++)
	promaddr[i] = inb(ioaddr + i);

    if (memcmp(promaddr, dev->dev_addr, 6)
	|| !is_valid_ether_addr(dev->dev_addr)) {
	if (is_valid_ether_addr(promaddr)) {
	    if (pcnet32_debug & NETIF_MSG_PROBE) {
		printk(" warning: CSR address invalid,\n");
		printk(KERN_INFO "    using instead PROM address of");
	    }
	    memcpy(dev->dev_addr, promaddr, 6);
	}
    }
    memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

    /* if the ethernet address is not valid, force to 00:00:00:00:00:00 */
    if (!is_valid_ether_addr(dev->perm_addr))
	memset(dev->dev_addr, 0, sizeof(dev->dev_addr));

    if (pcnet32_debug & NETIF_MSG_PROBE) {
	for (i = 0; i < 6; i++)
	    printk(" %2.2x", dev->dev_addr[i]);

	/* Version 0x2623 and 0x2624 */
	if (((chip_version + 1) & 0xfffe) == 0x2624) {
	    i = a->read_csr(ioaddr, 80) & 0x0C00;  /* Check tx_start_pt */
	    printk("\n" KERN_INFO "    tx_start_pt(0x%04x):",i);
	    switch(i>>10) {
		case 0: printk("  20 bytes,"); break;
		case 1: printk("  64 bytes,"); break;
		case 2: printk(" 128 bytes,"); break;
		case 3: printk("~220 bytes,"); break;
	    }
	    i = a->read_bcr(ioaddr, 18);  /* Check Burst/Bus control */
	    printk(" BCR18(%x):",i&0xffff);
	    if (i & (1<<5)) printk("BurstWrEn ");
	    if (i & (1<<6)) printk("BurstRdEn ");
	    if (i & (1<<7)) printk("DWordIO ");
	    if (i & (1<<11)) printk("NoUFlow ");
	    i = a->read_bcr(ioaddr, 25);
	    printk("\n" KERN_INFO "    SRAMSIZE=0x%04x,",i<<8);
	    i = a->read_bcr(ioaddr, 26);
	    printk(" SRAM_BND=0x%04x,",i<<8);
	    i = a->read_bcr(ioaddr, 27);
	    if (i & (1<<14)) printk("LowLatRx");
	}
    }

    dev->base_addr = ioaddr;
    /* pci_alloc_consistent returns page-aligned memory, so we do not have to check the alignment */
    if ((lp = pci_alloc_consistent(pdev, sizeof(*lp), &lp_dma_addr)) == NULL) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_ERR PFX "Consistent memory allocation failed.\n");
	ret = -ENOMEM;
	goto err_free_netdev;
    }

    memset(lp, 0, sizeof(*lp));
    lp->dma_addr = lp_dma_addr;
    lp->pci_dev = pdev;

    spin_lock_init(&lp->lock);

    SET_MODULE_OWNER(dev);
    SET_NETDEV_DEV(dev, &pdev->dev);
    dev->priv = lp;
    lp->name = chipname;
    lp->shared_irq = shared;
    lp->tx_ring_size = TX_RING_SIZE;		/* default tx ring size */
    lp->rx_ring_size = RX_RING_SIZE;		/* default rx ring size */
    lp->tx_mod_mask = lp->tx_ring_size - 1;
    lp->rx_mod_mask = lp->rx_ring_size - 1;
    lp->tx_len_bits = (PCNET32_LOG_TX_BUFFERS << 12);
    lp->rx_len_bits = (PCNET32_LOG_RX_BUFFERS << 4);
    lp->mii_if.full_duplex = fdx;
    lp->mii_if.phy_id_mask = 0x1f;
    lp->mii_if.reg_num_mask = 0x1f;
    lp->dxsuflo = dxsuflo;
    lp->mii = mii;
    lp->msg_enable = pcnet32_debug;
    if ((cards_found >= MAX_UNITS) || (options[cards_found] > sizeof(options_mapping)))
	lp->options = PCNET32_PORT_ASEL;
    else
	lp->options = options_mapping[options[cards_found]];
    lp->mii_if.dev = dev;
    lp->mii_if.mdio_read = mdio_read;
    lp->mii_if.mdio_write = mdio_write;

    if (fdx && !(lp->options & PCNET32_PORT_ASEL) &&
		((cards_found>=MAX_UNITS) || full_duplex[cards_found]))
	lp->options |= PCNET32_PORT_FD;

    if (!a) {
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(KERN_ERR PFX "No access methods\n");
	ret = -ENODEV;
	goto err_free_consistent;
    }
    lp->a = *a;

    /* prior to register_netdev, dev->name is not yet correct */
    if (pcnet32_alloc_ring(dev, pci_name(lp->pci_dev))) {
	ret = -ENOMEM;
	goto err_free_ring;
    }
    /* detect special T1/E1 WAN card by checking for MAC address */
    if (dev->dev_addr[0] == 0x00 && dev->dev_addr[1] == 0xe0
	    && dev->dev_addr[2] == 0x75)
	lp->options = PCNET32_PORT_FD | PCNET32_PORT_GPSI;

    lp->init_block.mode = le16_to_cpu(0x0003);	/* Disable Rx and Tx. */
    lp->init_block.tlen_rlen = le16_to_cpu(lp->tx_len_bits | lp->rx_len_bits);
    for (i = 0; i < 6; i++)
	lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.filter[0] = 0x00000000;
    lp->init_block.filter[1] = 0x00000000;
    lp->init_block.rx_ring = (u32)le32_to_cpu(lp->rx_ring_dma_addr);
    lp->init_block.tx_ring = (u32)le32_to_cpu(lp->tx_ring_dma_addr);

    /* switch pcnet32 to 32bit mode */
    a->write_bcr(ioaddr, 20, 2);

    a->write_csr(ioaddr, 1, (lp->dma_addr + offsetof(struct pcnet32_private,
		    init_block)) & 0xffff);
    a->write_csr(ioaddr, 2, (lp->dma_addr + offsetof(struct pcnet32_private,
		    init_block)) >> 16);

    if (pdev) {		/* use the IRQ provided by PCI */
	dev->irq = pdev->irq;
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(" assigned IRQ %d.\n", dev->irq);
    } else {
	unsigned long irq_mask = probe_irq_on();

	/*
	 * To auto-IRQ we enable the initialization-done and DMA error
	 * interrupts. For ISA boards we get a DMA error, but VLB and PCI
	 * boards will work.
	 */
	/* Trigger an initialization just for the interrupt. */
	a->write_csr (ioaddr, 0, 0x41);
	mdelay (1);

	dev->irq = probe_irq_off (irq_mask);
	if (!dev->irq) {
	    if (pcnet32_debug & NETIF_MSG_PROBE)
		printk(", failed to detect IRQ line.\n");
	    ret = -ENODEV;
	    goto err_free_ring;
	}
	if (pcnet32_debug & NETIF_MSG_PROBE)
	    printk(", probed IRQ %d.\n", dev->irq);
    }

    /* Set the mii phy_id so that we can query the link state */
    if (lp->mii)
	lp->mii_if.phy_id = ((lp->a.read_bcr (ioaddr, 33)) >> 5) & 0x1f;

    init_timer (&lp->watchdog_timer);
    lp->watchdog_timer.data = (unsigned long) dev;
    lp->watchdog_timer.function = (void *) &pcnet32_watchdog;

    /* The PCNET32-specific entries in the device structure. */
    dev->open = &pcnet32_open;
    dev->hard_start_xmit = &pcnet32_start_xmit;
    dev->stop = &pcnet32_close;
    dev->get_stats = &pcnet32_get_stats;
    dev->set_multicast_list = &pcnet32_set_multicast_list;
    dev->do_ioctl = &pcnet32_ioctl;
    dev->ethtool_ops = &pcnet32_ethtool_ops;
    dev->tx_timeout = pcnet32_tx_timeout;
    dev->watchdog_timeo = (5*HZ);

#ifdef CONFIG_NET_POLL_CONTROLLER
    dev->poll_controller = pcnet32_poll_controller;
#endif

    /* Fill in the generic fields of the device structure. */
    if (register_netdev(dev))
	goto err_free_ring;

    if (pdev) {
	pci_set_drvdata(pdev, dev);
    } else {
	lp->next = pcnet32_dev;
	pcnet32_dev = dev;
    }

    if (pcnet32_debug & NETIF_MSG_PROBE)
	printk(KERN_INFO "%s: registered as %s\n", dev->name, lp->name);
    cards_found++;

    /* enable LED writes */
    a->write_bcr(ioaddr, 2, a->read_bcr(ioaddr, 2) | 0x1000);

    return 0;

err_free_ring:
    pcnet32_free_ring(dev);
err_free_consistent:
    pci_free_consistent(lp->pci_dev, sizeof(*lp), lp, lp->dma_addr);
err_free_netdev:
    free_netdev(dev);
err_release_region:
    release_region(ioaddr, PCNET32_TOTAL_SIZE);
    return ret;
}


/* if any allocation fails, caller must also call pcnet32_free_ring */
static int pcnet32_alloc_ring(struct net_device *dev, char *name)
{
    struct pcnet32_private *lp = dev->priv;

    lp->tx_ring = pci_alloc_consistent(lp->pci_dev,
	    sizeof(struct pcnet32_tx_head) * lp->tx_ring_size,
	    &lp->tx_ring_dma_addr);
    if (lp->tx_ring == NULL) {
	if (pcnet32_debug & NETIF_MSG_DRV)
	    printk("\n" KERN_ERR PFX "%s: Consistent memory allocation failed.\n",
		    name);
	return -ENOMEM;
    }

    lp->rx_ring = pci_alloc_consistent(lp->pci_dev,
	    sizeof(struct pcnet32_rx_head) * lp->rx_ring_size,
	    &lp->rx_ring_dma_addr);
    if (lp->rx_ring == NULL) {
	if (pcnet32_debug & NETIF_MSG_DRV)
	    printk("\n" KERN_ERR PFX "%s: Consistent memory allocation failed.\n",
		    name);
	return -ENOMEM;
    }

    lp->tx_dma_addr = kmalloc(sizeof(dma_addr_t) * lp->tx_ring_size,
	    GFP_ATOMIC);
    if (!lp->tx_dma_addr) {
	if (pcnet32_debug & NETIF_MSG_DRV)
	    printk("\n" KERN_ERR PFX "%s: Memory allocation failed.\n", name);
	return -ENOMEM;
    }
    memset(lp->tx_dma_addr, 0, sizeof(dma_addr_t) * lp->tx_ring_size);

    lp->rx_dma_addr = kmalloc(sizeof(dma_addr_t) * lp->rx_ring_size,
	    GFP_ATOMIC);
    if (!lp->rx_dma_addr) {
	if (pcnet32_debug & NETIF_MSG_DRV)
	    printk("\n" KERN_ERR PFX "%s: Memory allocation failed.\n", name);
	return -ENOMEM;
    }
    memset(lp->rx_dma_addr, 0, sizeof(dma_addr_t) * lp->rx_ring_size);

    lp->tx_skbuff = kmalloc(sizeof(struct sk_buff *) * lp->tx_ring_size,
	    GFP_ATOMIC);
    if (!lp->tx_skbuff) {
	if (pcnet32_debug & NETIF_MSG_DRV)
	    printk("\n" KERN_ERR PFX "%s: Memory allocation failed.\n", name);
	return -ENOMEM;
    }
    memset(lp->tx_skbuff, 0, sizeof(struct sk_buff *) * lp->tx_ring_size);

    lp->rx_skbuff = kmalloc(sizeof(struct sk_buff *) * lp->rx_ring_size,
	    GFP_ATOMIC);
    if (!lp->rx_skbuff) {
	if (pcnet32_debug & NETIF_MSG_DRV)
	    printk("\n" KERN_ERR PFX "%s: Memory allocation failed.\n", name);
	return -ENOMEM;
    }
    memset(lp->rx_skbuff, 0, sizeof(struct sk_buff *) * lp->rx_ring_size);

    return 0;
}


static void pcnet32_free_ring(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;

    kfree(lp->tx_skbuff);
    lp->tx_skbuff = NULL;

    kfree(lp->rx_skbuff);
    lp->rx_skbuff = NULL;

    kfree(lp->tx_dma_addr);
    lp->tx_dma_addr = NULL;

    kfree(lp->rx_dma_addr);
    lp->rx_dma_addr = NULL;

    if (lp->tx_ring) {
	pci_free_consistent(lp->pci_dev, sizeof(struct pcnet32_tx_head) * lp->tx_ring_size,
		lp->tx_ring, lp->tx_ring_dma_addr);
	lp->tx_ring = NULL;
    }

    if (lp->rx_ring) {
	pci_free_consistent(lp->pci_dev, sizeof(struct pcnet32_rx_head) * lp->rx_ring_size,
		lp->rx_ring, lp->rx_ring_dma_addr);
	lp->rx_ring = NULL;
    }
}


static int
pcnet32_open(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr;
    u16 val;
    int i;
    int rc;
    unsigned long flags;

    if (request_irq(dev->irq, &pcnet32_interrupt,
		    lp->shared_irq ? SA_SHIRQ : 0, dev->name, (void *)dev)) {
	return -EAGAIN;
    }

    spin_lock_irqsave(&lp->lock, flags);
    /* Check for a valid station address */
    if (!is_valid_ether_addr(dev->dev_addr)) {
	rc = -EINVAL;
	goto err_free_irq;
    }

    /* Reset the PCNET32 */
    lp->a.reset (ioaddr);

    /* switch pcnet32 to 32bit mode */
    lp->a.write_bcr (ioaddr, 20, 2);

    if (netif_msg_ifup(lp))
	printk(KERN_DEBUG "%s: pcnet32_open() irq %d tx/rx rings %#x/%#x init %#x.\n",
	       dev->name, dev->irq,
	       (u32) (lp->tx_ring_dma_addr),
	       (u32) (lp->rx_ring_dma_addr),
	       (u32) (lp->dma_addr + offsetof(struct pcnet32_private, init_block)));

    /* set/reset autoselect bit */
    val = lp->a.read_bcr (ioaddr, 2) & ~2;
    if (lp->options & PCNET32_PORT_ASEL)
	val |= 2;
    lp->a.write_bcr (ioaddr, 2, val);

    /* handle full duplex setting */
    if (lp->mii_if.full_duplex) {
	val = lp->a.read_bcr (ioaddr, 9) & ~3;
	if (lp->options & PCNET32_PORT_FD) {
	    val |= 1;
	    if (lp->options == (PCNET32_PORT_FD | PCNET32_PORT_AUI))
		val |= 2;
	} else if (lp->options & PCNET32_PORT_ASEL) {
	/* workaround of xSeries250, turn on for 79C975 only */
	    i = ((lp->a.read_csr(ioaddr, 88) |
			(lp->a.read_csr(ioaddr,89) << 16)) >> 12) & 0xffff;
	    if (i == 0x2627)
		val |= 3;
	}
	lp->a.write_bcr (ioaddr, 9, val);
    }

    /* set/reset GPSI bit in test register */
    val = lp->a.read_csr (ioaddr, 124) & ~0x10;
    if ((lp->options & PCNET32_PORT_PORTSEL) == PCNET32_PORT_GPSI)
	val |= 0x10;
    lp->a.write_csr (ioaddr, 124, val);

    /* Allied Telesyn AT 2700/2701 FX are 100Mbit only and do not negotiate */
    if (lp->pci_dev->subsystem_vendor == PCI_VENDOR_ID_AT &&
	    (lp->pci_dev->subsystem_device == PCI_SUBDEVICE_ID_AT_2700FX ||
	     lp->pci_dev->subsystem_device == PCI_SUBDEVICE_ID_AT_2701FX)) {
	if (lp->options & PCNET32_PORT_ASEL) {
	    lp->options = PCNET32_PORT_FD | PCNET32_PORT_100;
	    if (netif_msg_link(lp))
		printk(KERN_DEBUG "%s: Setting 100Mb-Full Duplex.\n",
			dev->name);
	}
    }
    {
	/*
	 * 24 Jun 2004 according AMD, in order to change the PHY,
	 * DANAS (or DISPM for 79C976) must be set; then select the speed,
	 * duplex, and/or enable auto negotiation, and clear DANAS
	 */
	if (lp->mii && !(lp->options & PCNET32_PORT_ASEL)) {
	    lp->a.write_bcr(ioaddr, 32,
				lp->a.read_bcr(ioaddr, 32) | 0x0080);
	    /* disable Auto Negotiation, set 10Mpbs, HD */
	    val = lp->a.read_bcr(ioaddr, 32) & ~0xb8;
	    if (lp->options & PCNET32_PORT_FD)
		val |= 0x10;
	    if (lp->options & PCNET32_PORT_100)
		val |= 0x08;
	    lp->a.write_bcr (ioaddr, 32, val);
	} else {
	    if (lp->options & PCNET32_PORT_ASEL) {
		lp->a.write_bcr(ioaddr, 32,
			lp->a.read_bcr(ioaddr, 32) | 0x0080);
		/* enable auto negotiate, setup, disable fd */
		val = lp->a.read_bcr(ioaddr, 32) & ~0x98;
		val |= 0x20;
		lp->a.write_bcr(ioaddr, 32, val);
	    }
	}
    }

#ifdef DO_DXSUFLO
    if (lp->dxsuflo) { /* Disable transmit stop on underflow */
	val = lp->a.read_csr (ioaddr, 3);
	val |= 0x40;
	lp->a.write_csr (ioaddr, 3, val);
    }
#endif

    lp->init_block.mode = le16_to_cpu((lp->options & PCNET32_PORT_PORTSEL) << 7);
    pcnet32_load_multicast(dev);

    if (pcnet32_init_ring(dev)) {
	rc = -ENOMEM;
	goto err_free_ring;
    }

    /* Re-initialize the PCNET32, and start it when done. */
    lp->a.write_csr (ioaddr, 1, (lp->dma_addr +
		offsetof(struct pcnet32_private, init_block)) & 0xffff);
    lp->a.write_csr (ioaddr, 2, (lp->dma_addr +
		offsetof(struct pcnet32_private, init_block)) >> 16);

    lp->a.write_csr (ioaddr, 4, 0x0915);
    lp->a.write_csr (ioaddr, 0, 0x0001);

    netif_start_queue(dev);

    /* If we have mii, print the link status and start the watchdog */
    if (lp->mii) {
	mii_check_media (&lp->mii_if, netif_msg_link(lp), 1);
	mod_timer (&(lp->watchdog_timer), PCNET32_WATCHDOG_TIMEOUT);
    }

    i = 0;
    while (i++ < 100)
	if (lp->a.read_csr (ioaddr, 0) & 0x0100)
	    break;
    /*
     * We used to clear the InitDone bit, 0x0100, here but Mark Stockton
     * reports that doing so triggers a bug in the '974.
     */
    lp->a.write_csr (ioaddr, 0, 0x0042);

    if (netif_msg_ifup(lp))
	printk(KERN_DEBUG "%s: pcnet32 open after %d ticks, init block %#x csr0 %4.4x.\n",
		dev->name, i, (u32) (lp->dma_addr +
		    offsetof(struct pcnet32_private, init_block)),
		lp->a.read_csr(ioaddr, 0));

    spin_unlock_irqrestore(&lp->lock, flags);

    return 0;	/* Always succeed */

err_free_ring:
    /* free any allocated skbuffs */
    for (i = 0; i < lp->rx_ring_size; i++) {
	lp->rx_ring[i].status = 0;
	if (lp->rx_skbuff[i]) {
	    pci_unmap_single(lp->pci_dev, lp->rx_dma_addr[i], PKT_BUF_SZ-2,
		    PCI_DMA_FROMDEVICE);
	    dev_kfree_skb(lp->rx_skbuff[i]);
	}
	lp->rx_skbuff[i] = NULL;
	lp->rx_dma_addr[i] = 0;
    }

    pcnet32_free_ring(dev);

    /*
     * Switch back to 16bit mode to avoid problems with dumb
     * DOS packet driver after a warm reboot
     */
    lp->a.write_bcr (ioaddr, 20, 4);

err_free_irq:
    spin_unlock_irqrestore(&lp->lock, flags);
    free_irq(dev->irq, dev);
    return rc;
}

/*
 * The LANCE has been halted for one reason or another (busmaster memory
 * arbitration error, Tx FIFO underflow, driver stopped it to reconfigure,
 * etc.).  Modern LANCE variants always reload their ring-buffer
 * configuration when restarted, so we must reinitialize our ring
 * context before restarting.  As part of this reinitialization,
 * find all packets still on the Tx ring and pretend that they had been
 * sent (in effect, drop the packets on the floor) - the higher-level
 * protocols will time out and retransmit.  It'd be better to shuffle
 * these skbs to a temp list and then actually re-Tx them after
 * restarting the chip, but I'm too lazy to do so right now.  dplatt@3do.com
 */

static void
pcnet32_purge_tx_ring(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    int i;

    for (i = 0; i < lp->tx_ring_size; i++) {
	lp->tx_ring[i].status = 0;	/* CPU owns buffer */
	wmb();	/* Make sure adapter sees owner change */
	if (lp->tx_skbuff[i]) {
	    pci_unmap_single(lp->pci_dev, lp->tx_dma_addr[i],
		    lp->tx_skbuff[i]->len, PCI_DMA_TODEVICE);
	    dev_kfree_skb_any(lp->tx_skbuff[i]);
	}
	lp->tx_skbuff[i] = NULL;
	lp->tx_dma_addr[i] = 0;
    }
}


/* Initialize the PCNET32 Rx and Tx rings. */
static int
pcnet32_init_ring(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    int i;

    lp->tx_full = 0;
    lp->cur_rx = lp->cur_tx = 0;
    lp->dirty_rx = lp->dirty_tx = 0;

    for (i = 0; i < lp->rx_ring_size; i++) {
	struct sk_buff *rx_skbuff = lp->rx_skbuff[i];
	if (rx_skbuff == NULL) {
	    if (!(rx_skbuff = lp->rx_skbuff[i] = dev_alloc_skb (PKT_BUF_SZ))) {
		/* there is not much, we can do at this point */
		if (pcnet32_debug & NETIF_MSG_DRV)
		    printk(KERN_ERR "%s: pcnet32_init_ring dev_alloc_skb failed.\n",
			    dev->name);
		return -1;
	    }
	    skb_reserve (rx_skbuff, 2);
	}

	rmb();
	if (lp->rx_dma_addr[i] == 0)
	    lp->rx_dma_addr[i] = pci_map_single(lp->pci_dev, rx_skbuff->data,
		    PKT_BUF_SZ-2, PCI_DMA_FROMDEVICE);
	lp->rx_ring[i].base = (u32)le32_to_cpu(lp->rx_dma_addr[i]);
	lp->rx_ring[i].buf_length = le16_to_cpu(2-PKT_BUF_SZ);
	wmb();	/* Make sure owner changes after all others are visible */
	lp->rx_ring[i].status = le16_to_cpu(0x8000);
    }
    /* The Tx buffer address is filled in as needed, but we do need to clear
     * the upper ownership bit. */
    for (i = 0; i < lp->tx_ring_size; i++) {
	lp->tx_ring[i].status = 0;	/* CPU owns buffer */
	wmb();	/* Make sure adapter sees owner change */
	lp->tx_ring[i].base = 0;
	lp->tx_dma_addr[i] = 0;
    }

    lp->init_block.tlen_rlen = le16_to_cpu(lp->tx_len_bits | lp->rx_len_bits);
    for (i = 0; i < 6; i++)
	lp->init_block.phys_addr[i] = dev->dev_addr[i];
    lp->init_block.rx_ring = (u32)le32_to_cpu(lp->rx_ring_dma_addr);
    lp->init_block.tx_ring = (u32)le32_to_cpu(lp->tx_ring_dma_addr);
    wmb();	/* Make sure all changes are visible */
    return 0;
}

/* the pcnet32 has been issued a stop or reset.  Wait for the stop bit
 * then flush the pending transmit operations, re-initialize the ring,
 * and tell the chip to initialize.
 */
static void
pcnet32_restart(struct net_device *dev, unsigned int csr0_bits)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr;
    int i;

    /* wait for stop */
    for (i=0; i<100; i++)
	if (lp->a.read_csr(ioaddr, 0) & 0x0004)
	   break;

    if (i >= 100 && netif_msg_drv(lp))
	printk(KERN_ERR "%s: pcnet32_restart timed out waiting for stop.\n",
		dev->name);

    pcnet32_purge_tx_ring(dev);
    if (pcnet32_init_ring(dev))
	return;

    /* ReInit Ring */
    lp->a.write_csr (ioaddr, 0, 1);
    i = 0;
    while (i++ < 1000)
	if (lp->a.read_csr (ioaddr, 0) & 0x0100)
	    break;

    lp->a.write_csr (ioaddr, 0, csr0_bits);
}


static void
pcnet32_tx_timeout (struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr, flags;

    spin_lock_irqsave(&lp->lock, flags);
    /* Transmitter timeout, serious problems. */
    if (pcnet32_debug & NETIF_MSG_DRV)
	printk(KERN_ERR "%s: transmit timed out, status %4.4x, resetting.\n",
		dev->name, lp->a.read_csr(ioaddr, 0));
    lp->a.write_csr (ioaddr, 0, 0x0004);
    lp->stats.tx_errors++;
    if (netif_msg_tx_err(lp)) {
	int i;
	printk(KERN_DEBUG " Ring data dump: dirty_tx %d cur_tx %d%s cur_rx %d.",
	   lp->dirty_tx, lp->cur_tx, lp->tx_full ? " (full)" : "",
	   lp->cur_rx);
	for (i = 0 ; i < lp->rx_ring_size; i++)
	printk("%s %08x %04x %08x %04x", i & 1 ? "" : "\n ",
	       le32_to_cpu(lp->rx_ring[i].base),
	       (-le16_to_cpu(lp->rx_ring[i].buf_length)) & 0xffff,
	       le32_to_cpu(lp->rx_ring[i].msg_length),
	       le16_to_cpu(lp->rx_ring[i].status));
	for (i = 0 ; i < lp->tx_ring_size; i++)
	printk("%s %08x %04x %08x %04x", i & 1 ? "" : "\n ",
	       le32_to_cpu(lp->tx_ring[i].base),
	       (-le16_to_cpu(lp->tx_ring[i].length)) & 0xffff,
	       le32_to_cpu(lp->tx_ring[i].misc),
	       le16_to_cpu(lp->tx_ring[i].status));
	printk("\n");
    }
    pcnet32_restart(dev, 0x0042);

    dev->trans_start = jiffies;
    netif_wake_queue(dev);

    spin_unlock_irqrestore(&lp->lock, flags);
}


static int
pcnet32_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr;
    u16 status;
    int entry;
    unsigned long flags;

    spin_lock_irqsave(&lp->lock, flags);

    if (netif_msg_tx_queued(lp)) {
	printk(KERN_DEBUG "%s: pcnet32_start_xmit() called, csr0 %4.4x.\n",
	       dev->name, lp->a.read_csr(ioaddr, 0));
    }

    /* Default status -- will not enable Successful-TxDone
     * interrupt when that option is available to us.
     */
    status = 0x8300;

    /* Fill in a Tx ring entry */

    /* Mask to ring buffer boundary. */
    entry = lp->cur_tx & lp->tx_mod_mask;

    /* Caution: the write order is important here, set the status
     * with the "ownership" bits last. */

    lp->tx_ring[entry].length = le16_to_cpu(-skb->len);

    lp->tx_ring[entry].misc = 0x00000000;

    lp->tx_skbuff[entry] = skb;
    lp->tx_dma_addr[entry] = pci_map_single(lp->pci_dev, skb->data, skb->len,
	    PCI_DMA_TODEVICE);
    lp->tx_ring[entry].base = (u32)le32_to_cpu(lp->tx_dma_addr[entry]);
    wmb(); /* Make sure owner changes after all others are visible */
    lp->tx_ring[entry].status = le16_to_cpu(status);

    lp->cur_tx++;
    lp->stats.tx_bytes += skb->len;

    /* Trigger an immediate send poll. */
    lp->a.write_csr (ioaddr, 0, 0x0048);

    dev->trans_start = jiffies;

    if (lp->tx_ring[(entry+1) & lp->tx_mod_mask].base != 0) {
	lp->tx_full = 1;
	netif_stop_queue(dev);
    }
    spin_unlock_irqrestore(&lp->lock, flags);
    return 0;
}

/* The PCNET32 interrupt handler. */
static irqreturn_t
pcnet32_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
    struct net_device *dev = dev_id;
    struct pcnet32_private *lp;
    unsigned long ioaddr;
    u16 csr0,rap;
    int boguscnt =  max_interrupt_work;
    int must_restart;

    if (!dev) {
	if (pcnet32_debug & NETIF_MSG_INTR)
	    printk (KERN_DEBUG "%s(): irq %d for unknown device\n",
		__FUNCTION__, irq);
	return IRQ_NONE;
    }

    ioaddr = dev->base_addr;
    lp = dev->priv;

    spin_lock(&lp->lock);

    rap = lp->a.read_rap(ioaddr);
    while ((csr0 = lp->a.read_csr (ioaddr, 0)) & 0x8f00 && --boguscnt >= 0) {
	if (csr0 == 0xffff) {
	    break;			/* PCMCIA remove happened */
	}
	/* Acknowledge all of the current interrupt sources ASAP. */
	lp->a.write_csr (ioaddr, 0, csr0 & ~0x004f);

	must_restart = 0;

	if (netif_msg_intr(lp))
	    printk(KERN_DEBUG "%s: interrupt  csr0=%#2.2x new csr=%#2.2x.\n",
		   dev->name, csr0, lp->a.read_csr (ioaddr, 0));

	if (csr0 & 0x0400)		/* Rx interrupt */
	    pcnet32_rx(dev);

	if (csr0 & 0x0200) {		/* Tx-done interrupt */
	    unsigned int dirty_tx = lp->dirty_tx;
	    int delta;

	    while (dirty_tx != lp->cur_tx) {
		int entry = dirty_tx & lp->tx_mod_mask;
		int status = (short)le16_to_cpu(lp->tx_ring[entry].status);

		if (status < 0)
		    break;		/* It still hasn't been Txed */

		lp->tx_ring[entry].base = 0;

		if (status & 0x4000) {
		    /* There was an major error, log it. */
		    int err_status = le32_to_cpu(lp->tx_ring[entry].misc);
		    lp->stats.tx_errors++;
		    if (netif_msg_tx_err(lp))
			printk(KERN_ERR "%s: Tx error status=%04x err_status=%08x\n",
				dev->name, status, err_status);
		    if (err_status & 0x04000000) lp->stats.tx_aborted_errors++;
		    if (err_status & 0x08000000) lp->stats.tx_carrier_errors++;
		    if (err_status & 0x10000000) lp->stats.tx_window_errors++;
#ifndef DO_DXSUFLO
		    if (err_status & 0x40000000) {
			lp->stats.tx_fifo_errors++;
			/* Ackk!  On FIFO errors the Tx unit is turned off! */
			/* Remove this verbosity later! */
			if (netif_msg_tx_err(lp))
			    printk(KERN_ERR "%s: Tx FIFO error! CSR0=%4.4x\n",
				    dev->name, csr0);
			must_restart = 1;
		    }
#else
		    if (err_status & 0x40000000) {
			lp->stats.tx_fifo_errors++;
			if (! lp->dxsuflo) {  /* If controller doesn't recover ... */
			    /* Ackk!  On FIFO errors the Tx unit is turned off! */
			    /* Remove this verbosity later! */
			    if (netif_msg_tx_err(lp))
				printk(KERN_ERR "%s: Tx FIFO error! CSR0=%4.4x\n",
					dev->name, csr0);
			    must_restart = 1;
			}
		    }
#endif
		} else {
		    if (status & 0x1800)
			lp->stats.collisions++;
		    lp->stats.tx_packets++;
		}

		/* We must free the original skb */
		if (lp->tx_skbuff[entry]) {
		    pci_unmap_single(lp->pci_dev, lp->tx_dma_addr[entry],
			lp->tx_skbuff[entry]->len, PCI_DMA_TODEVICE);
		    dev_kfree_skb_irq(lp->tx_skbuff[entry]);
		    lp->tx_skbuff[entry] = NULL;
		    lp->tx_dma_addr[entry] = 0;
		}
		dirty_tx++;
	    }

	    delta = (lp->cur_tx - dirty_tx) & (lp->tx_mod_mask + lp->tx_ring_size);
	    if (delta > lp->tx_ring_size) {
		if (netif_msg_drv(lp))
		    printk(KERN_ERR "%s: out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
			    dev->name, dirty_tx, lp->cur_tx, lp->tx_full);
		dirty_tx += lp->tx_ring_size;
		delta -= lp->tx_ring_size;
	    }

	    if (lp->tx_full &&
		netif_queue_stopped(dev) &&
		delta < lp->tx_ring_size - 2) {
		/* The ring is no longer full, clear tbusy. */
		lp->tx_full = 0;
		netif_wake_queue (dev);
	    }
	    lp->dirty_tx = dirty_tx;
	}

	/* Log misc errors. */
	if (csr0 & 0x4000) lp->stats.tx_errors++; /* Tx babble. */
	if (csr0 & 0x1000) {
	    /*
	     * this happens when our receive ring is full. This shouldn't
	     * be a problem as we will see normal rx interrupts for the frames
	     * in the receive ring. But there are some PCI chipsets (I can
	     * reproduce this on SP3G with Intel saturn chipset) which have
	     * sometimes problems and will fill up the receive ring with
	     * error descriptors. In this situation we don't get a rx
	     * interrupt, but a missed frame interrupt sooner or later.
	     * So we try to clean up our receive ring here.
	     */
	    pcnet32_rx(dev);
	    lp->stats.rx_errors++; /* Missed a Rx frame. */
	}
	if (csr0 & 0x0800) {
	    if (netif_msg_drv(lp))
		printk(KERN_ERR "%s: Bus master arbitration failure, status %4.4x.\n",
			dev->name, csr0);
	    /* unlike for the lance, there is no restart needed */
	}

	if (must_restart) {
	    /* reset the chip to clear the error condition, then restart */
	    lp->a.reset(ioaddr);
	    lp->a.write_csr(ioaddr, 4, 0x0915);
	    pcnet32_restart(dev, 0x0002);
	    netif_wake_queue(dev);
	}
    }

    /* Set interrupt enable. */
    lp->a.write_csr (ioaddr, 0, 0x0040);
    lp->a.write_rap (ioaddr,rap);

    if (netif_msg_intr(lp))
	printk(KERN_DEBUG "%s: exiting interrupt, csr0=%#4.4x.\n",
		dev->name, lp->a.read_csr (ioaddr, 0));

    spin_unlock(&lp->lock);

    return IRQ_HANDLED;
}

static int
pcnet32_rx(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    int entry = lp->cur_rx & lp->rx_mod_mask;
    int boguscnt = lp->rx_ring_size / 2;

    /* If we own the next entry, it's a new packet. Send it up. */
    while ((short)le16_to_cpu(lp->rx_ring[entry].status) >= 0) {
	int status = (short)le16_to_cpu(lp->rx_ring[entry].status) >> 8;

	if (status != 0x03) {			/* There was an error. */
	    /*
	     * There is a tricky error noted by John Murphy,
	     * <murf@perftech.com> to Russ Nelson: Even with full-sized
	     * buffers it's possible for a jabber packet to use two
	     * buffers, with only the last correctly noting the error.
	     */
	    if (status & 0x01)	/* Only count a general error at the */
		lp->stats.rx_errors++; /* end of a packet.*/
	    if (status & 0x20) lp->stats.rx_frame_errors++;
	    if (status & 0x10) lp->stats.rx_over_errors++;
	    if (status & 0x08) lp->stats.rx_crc_errors++;
	    if (status & 0x04) lp->stats.rx_fifo_errors++;
	    lp->rx_ring[entry].status &= le16_to_cpu(0x03ff);
	} else {
	    /* Malloc up new buffer, compatible with net-2e. */
	    short pkt_len = (le32_to_cpu(lp->rx_ring[entry].msg_length) & 0xfff)-4;
	    struct sk_buff *skb;

	    /* Discard oversize frames. */
	    if (unlikely(pkt_len > PKT_BUF_SZ - 2)) {
		if (netif_msg_drv(lp))
		    printk(KERN_ERR "%s: Impossible packet size %d!\n",
			    dev->name, pkt_len);
		lp->stats.rx_errors++;
	    } else if (pkt_len < 60) {
		if (netif_msg_rx_err(lp))
		    printk(KERN_ERR "%s: Runt packet!\n", dev->name);
		lp->stats.rx_errors++;
	    } else {
		int rx_in_place = 0;

		if (pkt_len > rx_copybreak) {
		    struct sk_buff *newskb;

		    if ((newskb = dev_alloc_skb(PKT_BUF_SZ))) {
			skb_reserve (newskb, 2);
			skb = lp->rx_skbuff[entry];
			pci_unmap_single(lp->pci_dev, lp->rx_dma_addr[entry],
				PKT_BUF_SZ-2, PCI_DMA_FROMDEVICE);
			skb_put (skb, pkt_len);
			lp->rx_skbuff[entry] = newskb;
			newskb->dev = dev;
			lp->rx_dma_addr[entry] =
			    pci_map_single(lp->pci_dev, newskb->data,
				    PKT_BUF_SZ-2, PCI_DMA_FROMDEVICE);
			lp->rx_ring[entry].base = le32_to_cpu(lp->rx_dma_addr[entry]);
			rx_in_place = 1;
		    } else
			skb = NULL;
		} else {
		    skb = dev_alloc_skb(pkt_len+2);
		}

		if (skb == NULL) {
		    int i;
		    if (netif_msg_drv(lp))
			printk(KERN_ERR "%s: Memory squeeze, deferring packet.\n",
				dev->name);
		    for (i = 0; i < lp->rx_ring_size; i++)
			if ((short)le16_to_cpu(lp->rx_ring[(entry+i)
				    & lp->rx_mod_mask].status) < 0)
			    break;

		    if (i > lp->rx_ring_size -2) {
			lp->stats.rx_dropped++;
			lp->rx_ring[entry].status |= le16_to_cpu(0x8000);
			wmb();	/* Make sure adapter sees owner change */
			lp->cur_rx++;
		    }
		    break;
		}
		skb->dev = dev;
		if (!rx_in_place) {
		    skb_reserve(skb,2); /* 16 byte align */
		    skb_put(skb,pkt_len);	/* Make room */
		    pci_dma_sync_single_for_cpu(lp->pci_dev,
						lp->rx_dma_addr[entry],
						PKT_BUF_SZ-2,
						PCI_DMA_FROMDEVICE);
		    eth_copy_and_sum(skb,
			    (unsigned char *)(lp->rx_skbuff[entry]->data),
			    pkt_len,0);
		    pci_dma_sync_single_for_device(lp->pci_dev,
						   lp->rx_dma_addr[entry],
						   PKT_BUF_SZ-2,
						   PCI_DMA_FROMDEVICE);
		}
		lp->stats.rx_bytes += skb->len;
		skb->protocol=eth_type_trans(skb,dev);
		netif_rx(skb);
		dev->last_rx = jiffies;
		lp->stats.rx_packets++;
	    }
	}
	/*
	 * The docs say that the buffer length isn't touched, but Andrew Boyd
	 * of QNX reports that some revs of the 79C965 clear it.
	 */
	lp->rx_ring[entry].buf_length = le16_to_cpu(2-PKT_BUF_SZ);
	wmb(); /* Make sure owner changes after all others are visible */
	lp->rx_ring[entry].status |= le16_to_cpu(0x8000);
	entry = (++lp->cur_rx) & lp->rx_mod_mask;
	if (--boguscnt <= 0) break;	/* don't stay in loop forever */
    }

    return 0;
}

static int
pcnet32_close(struct net_device *dev)
{
    unsigned long ioaddr = dev->base_addr;
    struct pcnet32_private *lp = dev->priv;
    int i;
    unsigned long flags;

    del_timer_sync(&lp->watchdog_timer);

    netif_stop_queue(dev);

    spin_lock_irqsave(&lp->lock, flags);

    lp->stats.rx_missed_errors = lp->a.read_csr (ioaddr, 112);

    if (netif_msg_ifdown(lp))
	printk(KERN_DEBUG "%s: Shutting down ethercard, status was %2.2x.\n",
	       dev->name, lp->a.read_csr (ioaddr, 0));

    /* We stop the PCNET32 here -- it occasionally polls memory if we don't. */
    lp->a.write_csr (ioaddr, 0, 0x0004);

    /*
     * Switch back to 16bit mode to avoid problems with dumb
     * DOS packet driver after a warm reboot
     */
    lp->a.write_bcr (ioaddr, 20, 4);

    spin_unlock_irqrestore(&lp->lock, flags);

    free_irq(dev->irq, dev);

    spin_lock_irqsave(&lp->lock, flags);

    /* free all allocated skbuffs */
    for (i = 0; i < lp->rx_ring_size; i++) {
	lp->rx_ring[i].status = 0;
	wmb();		/* Make sure adapter sees owner change */
	if (lp->rx_skbuff[i]) {
	    pci_unmap_single(lp->pci_dev, lp->rx_dma_addr[i], PKT_BUF_SZ-2,
		    PCI_DMA_FROMDEVICE);
	    dev_kfree_skb(lp->rx_skbuff[i]);
	}
	lp->rx_skbuff[i] = NULL;
	lp->rx_dma_addr[i] = 0;
    }

    for (i = 0; i < lp->tx_ring_size; i++) {
	lp->tx_ring[i].status = 0;	/* CPU owns buffer */
	wmb();		/* Make sure adapter sees owner change */
	if (lp->tx_skbuff[i]) {
	    pci_unmap_single(lp->pci_dev, lp->tx_dma_addr[i],
		    lp->tx_skbuff[i]->len, PCI_DMA_TODEVICE);
	    dev_kfree_skb(lp->tx_skbuff[i]);
	}
	lp->tx_skbuff[i] = NULL;
	lp->tx_dma_addr[i] = 0;
    }

    spin_unlock_irqrestore(&lp->lock, flags);

    return 0;
}

static struct net_device_stats *
pcnet32_get_stats(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr;
    u16 saved_addr;
    unsigned long flags;

    spin_lock_irqsave(&lp->lock, flags);
    saved_addr = lp->a.read_rap(ioaddr);
    lp->stats.rx_missed_errors = lp->a.read_csr (ioaddr, 112);
    lp->a.write_rap(ioaddr, saved_addr);
    spin_unlock_irqrestore(&lp->lock, flags);

    return &lp->stats;
}

/* taken from the sunlance driver, which it took from the depca driver */
static void pcnet32_load_multicast (struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    volatile struct pcnet32_init_block *ib = &lp->init_block;
    volatile u16 *mcast_table = (u16 *)&ib->filter;
    struct dev_mc_list *dmi=dev->mc_list;
    char *addrs;
    int i;
    u32 crc;

    /* set all multicast bits */
    if (dev->flags & IFF_ALLMULTI) {
	ib->filter[0] = 0xffffffff;
	ib->filter[1] = 0xffffffff;
	return;
    }
    /* clear the multicast filter */
    ib->filter[0] = 0;
    ib->filter[1] = 0;

    /* Add addresses */
    for (i = 0; i < dev->mc_count; i++) {
	addrs = dmi->dmi_addr;
	dmi   = dmi->next;

	/* multicast address? */
	if (!(*addrs & 1))
	    continue;

	crc = ether_crc_le(6, addrs);
	crc = crc >> 26;
	mcast_table [crc >> 4] = le16_to_cpu(
		le16_to_cpu(mcast_table [crc >> 4]) | (1 << (crc & 0xf)));
    }
    return;
}


/*
 * Set or clear the multicast filter for this adaptor.
 */
static void pcnet32_set_multicast_list(struct net_device *dev)
{
    unsigned long ioaddr = dev->base_addr, flags;
    struct pcnet32_private *lp = dev->priv;

    spin_lock_irqsave(&lp->lock, flags);
    if (dev->flags&IFF_PROMISC) {
	/* Log any net taps. */
	if (netif_msg_hw(lp))
	    printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
	lp->init_block.mode = le16_to_cpu(0x8000 | (lp->options & PCNET32_PORT_PORTSEL) << 7);
    } else {
	lp->init_block.mode = le16_to_cpu((lp->options & PCNET32_PORT_PORTSEL) << 7);
	pcnet32_load_multicast (dev);
    }

    lp->a.write_csr (ioaddr, 0, 0x0004); /* Temporarily stop the lance. */
    pcnet32_restart(dev, 0x0042); /*  Resume normal operation */
    netif_wake_queue(dev);

    spin_unlock_irqrestore(&lp->lock, flags);
}

/* This routine assumes that the lp->lock is held */
static int mdio_read(struct net_device *dev, int phy_id, int reg_num)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr;
    u16 val_out;

    if (!lp->mii)
	return 0;

    lp->a.write_bcr(ioaddr, 33, ((phy_id & 0x1f) << 5) | (reg_num & 0x1f));
    val_out = lp->a.read_bcr(ioaddr, 34);

    return val_out;
}

/* This routine assumes that the lp->lock is held */
static void mdio_write(struct net_device *dev, int phy_id, int reg_num, int val)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long ioaddr = dev->base_addr;

    if (!lp->mii)
	return;

    lp->a.write_bcr(ioaddr, 33, ((phy_id & 0x1f) << 5) | (reg_num & 0x1f));
    lp->a.write_bcr(ioaddr, 34, val);
}

static int pcnet32_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    struct pcnet32_private *lp = dev->priv;
    int rc;
    unsigned long flags;

    /* SIOC[GS]MIIxxx ioctls */
    if (lp->mii) {
	spin_lock_irqsave(&lp->lock, flags);
	rc = generic_mii_ioctl(&lp->mii_if, if_mii(rq), cmd, NULL);
	spin_unlock_irqrestore(&lp->lock, flags);
    } else {
	rc = -EOPNOTSUPP;
    }

    return rc;
}

static void pcnet32_watchdog(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    unsigned long flags;

    /* Print the link status if it has changed */
    if (lp->mii) {
	spin_lock_irqsave(&lp->lock, flags);
	mii_check_media (&lp->mii_if, netif_msg_link(lp), 0);
	spin_unlock_irqrestore(&lp->lock, flags);
    }

    mod_timer (&(lp->watchdog_timer), PCNET32_WATCHDOG_TIMEOUT);
}

static void __devexit pcnet32_remove_one(struct pci_dev *pdev)
{
    struct net_device *dev = pci_get_drvdata(pdev);

    if (dev) {
	struct pcnet32_private *lp = dev->priv;

	unregister_netdev(dev);
	pcnet32_free_ring(dev);
	release_region(dev->base_addr, PCNET32_TOTAL_SIZE);
	pci_free_consistent(lp->pci_dev, sizeof(*lp), lp, lp->dma_addr);
	free_netdev(dev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
    }
}

static struct pci_driver pcnet32_driver = {
    .name	= DRV_NAME,
    .probe	= pcnet32_probe_pci,
    .remove	= __devexit_p(pcnet32_remove_one),
    .id_table	= pcnet32_pci_tbl,
};

/* An additional parameter that may be passed in... */
static int debug = -1;
static int tx_start_pt = -1;
static int pcnet32_have_pci;

module_param(debug, int, 0);
MODULE_PARM_DESC(debug, DRV_NAME " debug level");
module_param(max_interrupt_work, int, 0);
MODULE_PARM_DESC(max_interrupt_work, DRV_NAME " maximum events handled per interrupt");
module_param(rx_copybreak, int, 0);
MODULE_PARM_DESC(rx_copybreak, DRV_NAME " copy breakpoint for copy-only-tiny-frames");
module_param(tx_start_pt, int, 0);
MODULE_PARM_DESC(tx_start_pt, DRV_NAME " transmit start point (0-3)");
module_param(pcnet32vlb, int, 0);
MODULE_PARM_DESC(pcnet32vlb, DRV_NAME " Vesa local bus (VLB) support (0/1)");
module_param_array(options, int, NULL, 0);
MODULE_PARM_DESC(options, DRV_NAME " initial option setting(s) (0-15)");
module_param_array(full_duplex, int, NULL, 0);
MODULE_PARM_DESC(full_duplex, DRV_NAME " full duplex setting(s) (1)");
/* Module Parameter for HomePNA cards added by Patrick Simmons, 2004 */
module_param_array(homepna, int, NULL, 0);
MODULE_PARM_DESC(homepna, DRV_NAME " mode for 79C978 cards (1 for HomePNA, 0 for Ethernet, default Ethernet");

MODULE_AUTHOR("Thomas Bogendoerfer");
MODULE_DESCRIPTION("Driver for PCnet32 and PCnetPCI based ethercards");
MODULE_LICENSE("GPL");

#define PCNET32_MSG_DEFAULT (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

static int __init pcnet32_init_module(void)
{
    printk(KERN_INFO "%s", version);

    pcnet32_debug = netif_msg_init(debug, PCNET32_MSG_DEFAULT);

    if ((tx_start_pt >= 0) && (tx_start_pt <= 3))
	tx_start = tx_start_pt;

    /* find the PCI devices */
    if (!pci_module_init(&pcnet32_driver))
	pcnet32_have_pci = 1;

    /* should we find any remaining VLbus devices ? */
    if (pcnet32vlb)
	pcnet32_probe_vlbus();

    if (cards_found && (pcnet32_debug & NETIF_MSG_PROBE))
	printk(KERN_INFO PFX "%d cards_found.\n", cards_found);

    return (pcnet32_have_pci + cards_found) ? 0 : -ENODEV;
}

static void __exit pcnet32_cleanup_module(void)
{
    struct net_device *next_dev;

    while (pcnet32_dev) {
	struct pcnet32_private *lp = pcnet32_dev->priv;
	next_dev = lp->next;
	unregister_netdev(pcnet32_dev);
	pcnet32_free_ring(pcnet32_dev);
	release_region(pcnet32_dev->base_addr, PCNET32_TOTAL_SIZE);
	pci_free_consistent(lp->pci_dev, sizeof(*lp), lp, lp->dma_addr);
	free_netdev(pcnet32_dev);
	pcnet32_dev = next_dev;
    }

    if (pcnet32_have_pci)
	pci_unregister_driver(&pcnet32_driver);
}

module_init(pcnet32_init_module);
module_exit(pcnet32_cleanup_module);

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */

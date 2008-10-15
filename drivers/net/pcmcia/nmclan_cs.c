/* ----------------------------------------------------------------------------
Linux PCMCIA ethernet adapter driver for the New Media Ethernet LAN.
  nmclan_cs.c,v 0.16 1995/07/01 06:42:17 rpao Exp rpao

  The Ethernet LAN uses the Advanced Micro Devices (AMD) Am79C940 Media
  Access Controller for Ethernet (MACE).  It is essentially the Am2150
  PCMCIA Ethernet card contained in the Am2150 Demo Kit.

Written by Roger C. Pao <rpao@paonet.org>
  Copyright 1995 Roger C. Pao
  Linux 2.5 cleanups Copyright Red Hat 2003

  This software may be used and distributed according to the terms of
  the GNU General Public License.

Ported to Linux 1.3.* network driver environment by
  Matti Aarnio <mea@utu.fi>

References

  Am2150 Technical Reference Manual, Revision 1.0, August 17, 1993
  Am79C940 (MACE) Data Sheet, 1994
  Am79C90 (C-LANCE) Data Sheet, 1994
  Linux PCMCIA Programmer's Guide v1.17
  /usr/src/linux/net/inet/dev.c, Linux kernel 1.2.8

  Eric Mears, New Media Corporation
  Tom Pollard, New Media Corporation
  Dean Siasoyco, New Media Corporation
  Ken Lesniak, Silicon Graphics, Inc. <lesniak@boston.sgi.com>
  Donald Becker <becker@scyld.com>
  David Hinds <dahinds@users.sourceforge.net>

  The Linux client driver is based on the 3c589_cs.c client driver by
  David Hinds.

  The Linux network driver outline is based on the 3c589_cs.c driver,
  the 8390.c driver, and the example skeleton.c kernel code, which are
  by Donald Becker.

  The Am2150 network driver hardware interface code is based on the
  OS/9000 driver for the New Media Ethernet LAN by Eric Mears.

  Special thanks for testing and help in debugging this driver goes
  to Ken Lesniak.

-------------------------------------------------------------------------------
Driver Notes and Issues
-------------------------------------------------------------------------------

1. Developed on a Dell 320SLi
   PCMCIA Card Services 2.6.2
   Linux dell 1.2.10 #1 Thu Jun 29 20:23:41 PDT 1995 i386

2. rc.pcmcia may require loading pcmcia_core with io_speed=300:
   'insmod pcmcia_core.o io_speed=300'.
   This will avoid problems with fast systems which causes rx_framecnt
   to return random values.

3. If hot extraction does not work for you, use 'ifconfig eth0 down'
   before extraction.

4. There is a bad slow-down problem in this driver.

5. Future: Multicast processing.  In the meantime, do _not_ compile your
   kernel with multicast ip enabled.

-------------------------------------------------------------------------------
History
-------------------------------------------------------------------------------
Log: nmclan_cs.c,v
 * 2.5.75-ac1 2003/07/11 Alan Cox <alan@redhat.com>
 * Fixed hang on card eject as we probe it
 * Cleaned up to use new style locking.
 *
 * Revision 0.16  1995/07/01  06:42:17  rpao
 * Bug fix: nmclan_reset() called CardServices incorrectly.
 *
 * Revision 0.15  1995/05/24  08:09:47  rpao
 * Re-implement MULTI_TX dev->tbusy handling.
 *
 * Revision 0.14  1995/05/23  03:19:30  rpao
 * Added, in nmclan_config(), "tuple.Attributes = 0;".
 * Modified MACE ID check to ignore chip revision level.
 * Avoid tx_free_frames race condition between _start_xmit and _interrupt.
 *
 * Revision 0.13  1995/05/18  05:56:34  rpao
 * Statistics changes.
 * Bug fix: nmclan_reset did not enable TX and RX: call restore_multicast_list.
 * Bug fix: mace_interrupt checks ~MACE_IMR_DEFAULT.  Fixes driver lockup.
 *
 * Revision 0.12  1995/05/14  00:12:23  rpao
 * Statistics overhaul.
 *

95/05/13 rpao	V0.10a
		Bug fix: MACE statistics counters used wrong I/O ports.
		Bug fix: mace_interrupt() needed to allow statistics to be
		processed without RX or TX interrupts pending.
95/05/11 rpao	V0.10
		Multiple transmit request processing.
		Modified statistics to use MACE counters where possible.
95/05/10 rpao	V0.09 Bug fix: Must use IO_DATA_PATH_WIDTH_AUTO.
		*Released
95/05/10 rpao	V0.08
		Bug fix: Make all non-exported functions private by using
		static keyword.
		Bug fix: Test IntrCnt _before_ reading MACE_IR.
95/05/10 rpao	V0.07 Statistics.
95/05/09 rpao	V0.06 Fix rx_framecnt problem by addition of PCIC wait states.

---------------------------------------------------------------------------- */

#define DRV_NAME	"nmclan_cs"
#define DRV_VERSION	"0.16"


/* ----------------------------------------------------------------------------
Conditional Compilation Options
---------------------------------------------------------------------------- */

#define MULTI_TX			0
#define RESET_ON_TIMEOUT		1
#define TX_INTERRUPTABLE		1
#define RESET_XILINX			0

/* ----------------------------------------------------------------------------
Include Files
---------------------------------------------------------------------------- */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/bitops.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

/* ----------------------------------------------------------------------------
Defines
---------------------------------------------------------------------------- */

#define ETHER_ADDR_LEN			ETH_ALEN
					/* 6 bytes in an Ethernet Address */
#define MACE_LADRF_LEN			8
					/* 8 bytes in Logical Address Filter */

/* Loop Control Defines */
#define MACE_MAX_IR_ITERATIONS		10
#define MACE_MAX_RX_ITERATIONS		12
	/*
	TBD: Dean brought this up, and I assumed the hardware would
	handle it:

	If MACE_MAX_RX_ITERATIONS is > 1, rx_framecnt may still be
	non-zero when the isr exits.  We may not get another interrupt
	to process the remaining packets for some time.
	*/

/*
The Am2150 has a Xilinx XC3042 field programmable gate array (FPGA)
which manages the interface between the MACE and the PCMCIA bus.  It
also includes buffer management for the 32K x 8 SRAM to control up to
four transmit and 12 receive frames at a time.
*/
#define AM2150_MAX_TX_FRAMES		4
#define AM2150_MAX_RX_FRAMES		12

/* Am2150 Ethernet Card I/O Mapping */
#define AM2150_RCV			0x00
#define AM2150_XMT			0x04
#define AM2150_XMT_SKIP			0x09
#define AM2150_RCV_NEXT			0x0A
#define AM2150_RCV_FRAME_COUNT		0x0B
#define AM2150_MACE_BANK		0x0C
#define AM2150_MACE_BASE		0x10

/* MACE Registers */
#define MACE_RCVFIFO			0
#define MACE_XMTFIFO			1
#define MACE_XMTFC			2
#define MACE_XMTFS			3
#define MACE_XMTRC			4
#define MACE_RCVFC			5
#define MACE_RCVFS			6
#define MACE_FIFOFC			7
#define MACE_IR				8
#define MACE_IMR			9
#define MACE_PR				10
#define MACE_BIUCC			11
#define MACE_FIFOCC			12
#define MACE_MACCC			13
#define MACE_PLSCC			14
#define MACE_PHYCC			15
#define MACE_CHIPIDL			16
#define MACE_CHIPIDH			17
#define MACE_IAC			18
/* Reserved */
#define MACE_LADRF			20
#define MACE_PADR			21
/* Reserved */
/* Reserved */
#define MACE_MPC			24
/* Reserved */
#define MACE_RNTPC			26
#define MACE_RCVCC			27
/* Reserved */
#define MACE_UTR			29
#define MACE_RTR1			30
#define MACE_RTR2			31

/* MACE Bit Masks */
#define MACE_XMTRC_EXDEF		0x80
#define MACE_XMTRC_XMTRC		0x0F

#define MACE_XMTFS_XMTSV		0x80
#define MACE_XMTFS_UFLO			0x40
#define MACE_XMTFS_LCOL			0x20
#define MACE_XMTFS_MORE			0x10
#define MACE_XMTFS_ONE			0x08
#define MACE_XMTFS_DEFER		0x04
#define MACE_XMTFS_LCAR			0x02
#define MACE_XMTFS_RTRY			0x01

#define MACE_RCVFS_RCVSTS		0xF000
#define MACE_RCVFS_OFLO			0x8000
#define MACE_RCVFS_CLSN			0x4000
#define MACE_RCVFS_FRAM			0x2000
#define MACE_RCVFS_FCS			0x1000

#define MACE_FIFOFC_RCVFC		0xF0
#define MACE_FIFOFC_XMTFC		0x0F

#define MACE_IR_JAB			0x80
#define MACE_IR_BABL			0x40
#define MACE_IR_CERR			0x20
#define MACE_IR_RCVCCO			0x10
#define MACE_IR_RNTPCO			0x08
#define MACE_IR_MPCO			0x04
#define MACE_IR_RCVINT			0x02
#define MACE_IR_XMTINT			0x01

#define MACE_MACCC_PROM			0x80
#define MACE_MACCC_DXMT2PD		0x40
#define MACE_MACCC_EMBA			0x20
#define MACE_MACCC_RESERVED		0x10
#define MACE_MACCC_DRCVPA		0x08
#define MACE_MACCC_DRCVBC		0x04
#define MACE_MACCC_ENXMT		0x02
#define MACE_MACCC_ENRCV		0x01

#define MACE_PHYCC_LNKFL		0x80
#define MACE_PHYCC_DLNKTST		0x40
#define MACE_PHYCC_REVPOL		0x20
#define MACE_PHYCC_DAPC			0x10
#define MACE_PHYCC_LRT			0x08
#define MACE_PHYCC_ASEL			0x04
#define MACE_PHYCC_RWAKE		0x02
#define MACE_PHYCC_AWAKE		0x01

#define MACE_IAC_ADDRCHG		0x80
#define MACE_IAC_PHYADDR		0x04
#define MACE_IAC_LOGADDR		0x02

#define MACE_UTR_RTRE			0x80
#define MACE_UTR_RTRD			0x40
#define MACE_UTR_RPA			0x20
#define MACE_UTR_FCOLL			0x10
#define MACE_UTR_RCVFCSE		0x08
#define MACE_UTR_LOOP_INCL_MENDEC	0x06
#define MACE_UTR_LOOP_NO_MENDEC		0x04
#define MACE_UTR_LOOP_EXTERNAL		0x02
#define MACE_UTR_LOOP_NONE		0x00
#define MACE_UTR_RESERVED		0x01

/* Switch MACE register bank (only 0 and 1 are valid) */
#define MACEBANK(win_num) outb((win_num), ioaddr + AM2150_MACE_BANK)

#define MACE_IMR_DEFAULT \
  (0xFF - \
    ( \
      MACE_IR_CERR | \
      MACE_IR_RCVCCO | \
      MACE_IR_RNTPCO | \
      MACE_IR_MPCO | \
      MACE_IR_RCVINT | \
      MACE_IR_XMTINT \
    ) \
  )
#undef MACE_IMR_DEFAULT
#define MACE_IMR_DEFAULT 0x00 /* New statistics handling: grab everything */

#define TX_TIMEOUT		((400*HZ)/1000)

/* ----------------------------------------------------------------------------
Type Definitions
---------------------------------------------------------------------------- */

typedef struct _mace_statistics {
    /* MACE_XMTFS */
    int xmtsv;
    int uflo;
    int lcol;
    int more;
    int one;
    int defer;
    int lcar;
    int rtry;

    /* MACE_XMTRC */
    int exdef;
    int xmtrc;

    /* RFS1--Receive Status (RCVSTS) */
    int oflo;
    int clsn;
    int fram;
    int fcs;

    /* RFS2--Runt Packet Count (RNTPC) */
    int rfs_rntpc;

    /* RFS3--Receive Collision Count (RCVCC) */
    int rfs_rcvcc;

    /* MACE_IR */
    int jab;
    int babl;
    int cerr;
    int rcvcco;
    int rntpco;
    int mpco;

    /* MACE_MPC */
    int mpc;

    /* MACE_RNTPC */
    int rntpc;

    /* MACE_RCVCC */
    int rcvcc;
} mace_statistics;

typedef struct _mace_private {
	struct pcmcia_device	*p_dev;
    dev_node_t node;
    struct net_device_stats linux_stats; /* Linux statistics counters */
    mace_statistics mace_stats; /* MACE chip statistics counters */

    /* restore_multicast_list() state variables */
    int multicast_ladrf[MACE_LADRF_LEN]; /* Logical address filter */
    int multicast_num_addrs;

    char tx_free_frames; /* Number of free transmit frame buffers */
    char tx_irq_disabled; /* MACE TX interrupt disabled */
    
    spinlock_t bank_lock; /* Must be held if you step off bank 0 */
} mace_private;

/* ----------------------------------------------------------------------------
Private Global Variables
---------------------------------------------------------------------------- */

#ifdef PCMCIA_DEBUG
static char rcsid[] =
"nmclan_cs.c,v 0.16 1995/07/01 06:42:17 rpao Exp rpao";
static char *version =
DRV_NAME " " DRV_VERSION " (Roger C. Pao)";
#endif

static const char *if_names[]={
    "Auto", "10baseT", "BNC",
};

/* ----------------------------------------------------------------------------
Parameters
	These are the parameters that can be set during loading with
	'insmod'.
---------------------------------------------------------------------------- */

MODULE_DESCRIPTION("New Media PCMCIA ethernet driver");
MODULE_LICENSE("GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

/* 0=auto, 1=10baseT, 2 = 10base2, default=auto */
INT_MODULE_PARM(if_port, 0);

#ifdef PCMCIA_DEBUG
INT_MODULE_PARM(pc_debug, PCMCIA_DEBUG);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
#else
#define DEBUG(n, args...)
#endif

/* ----------------------------------------------------------------------------
Function Prototypes
---------------------------------------------------------------------------- */

static int nmclan_config(struct pcmcia_device *link);
static void nmclan_release(struct pcmcia_device *link);

static void nmclan_reset(struct net_device *dev);
static int mace_config(struct net_device *dev, struct ifmap *map);
static int mace_open(struct net_device *dev);
static int mace_close(struct net_device *dev);
static int mace_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void mace_tx_timeout(struct net_device *dev);
static irqreturn_t mace_interrupt(int irq, void *dev_id);
static struct net_device_stats *mace_get_stats(struct net_device *dev);
static int mace_rx(struct net_device *dev, unsigned char RxCnt);
static void restore_multicast_list(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static const struct ethtool_ops netdev_ethtool_ops;


static void nmclan_detach(struct pcmcia_device *p_dev);

/* ----------------------------------------------------------------------------
nmclan_attach
	Creates an "instance" of the driver, allocating local data
	structures for one device.  The device is registered with Card
	Services.
---------------------------------------------------------------------------- */

static int nmclan_probe(struct pcmcia_device *link)
{
    mace_private *lp;
    struct net_device *dev;

    DEBUG(0, "nmclan_attach()\n");
    DEBUG(1, "%s\n", rcsid);

    /* Create new ethernet device */
    dev = alloc_etherdev(sizeof(mace_private));
    if (!dev)
	    return -ENOMEM;
    lp = netdev_priv(dev);
    lp->p_dev = link;
    link->priv = dev;
    
    spin_lock_init(&lp->bank_lock);
    link->io.NumPorts1 = 32;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 5;
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->irq.Handler = &mace_interrupt;
    link->irq.Instance = dev;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;
    link->conf.ConfigIndex = 1;
    link->conf.Present = PRESENT_OPTION;

    lp->tx_free_frames=AM2150_MAX_TX_FRAMES;

    dev->hard_start_xmit = &mace_start_xmit;
    dev->set_config = &mace_config;
    dev->get_stats = &mace_get_stats;
    dev->set_multicast_list = &set_multicast_list;
    SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);
    dev->open = &mace_open;
    dev->stop = &mace_close;
#ifdef HAVE_TX_TIMEOUT
    dev->tx_timeout = mace_tx_timeout;
    dev->watchdog_timeo = TX_TIMEOUT;
#endif

    return nmclan_config(link);
} /* nmclan_attach */

/* ----------------------------------------------------------------------------
nmclan_detach
	This deletes a driver "instance".  The device is de-registered
	with Card Services.  If it has been released, all local data
	structures are freed.  Otherwise, the structures will be freed
	when the device is released.
---------------------------------------------------------------------------- */

static void nmclan_detach(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;

    DEBUG(0, "nmclan_detach(0x%p)\n", link);

    if (link->dev_node)
	unregister_netdev(dev);

    nmclan_release(link);

    free_netdev(dev);
} /* nmclan_detach */

/* ----------------------------------------------------------------------------
mace_read
	Reads a MACE register.  This is bank independent; however, the
	caller must ensure that this call is not interruptable.  We are
	assuming that during normal operation, the MACE is always in
	bank 0.
---------------------------------------------------------------------------- */
static int mace_read(mace_private *lp, unsigned int ioaddr, int reg)
{
  int data = 0xFF;
  unsigned long flags;

  switch (reg >> 4) {
    case 0: /* register 0-15 */
      data = inb(ioaddr + AM2150_MACE_BASE + reg);
      break;
    case 1: /* register 16-31 */
      spin_lock_irqsave(&lp->bank_lock, flags);
      MACEBANK(1);
      data = inb(ioaddr + AM2150_MACE_BASE + (reg & 0x0F));
      MACEBANK(0);
      spin_unlock_irqrestore(&lp->bank_lock, flags);
      break;
  }
  return (data & 0xFF);
} /* mace_read */

/* ----------------------------------------------------------------------------
mace_write
	Writes to a MACE register.  This is bank independent; however,
	the caller must ensure that this call is not interruptable.  We
	are assuming that during normal operation, the MACE is always in
	bank 0.
---------------------------------------------------------------------------- */
static void mace_write(mace_private *lp, unsigned int ioaddr, int reg,
		       int data)
{
  unsigned long flags;

  switch (reg >> 4) {
    case 0: /* register 0-15 */
      outb(data & 0xFF, ioaddr + AM2150_MACE_BASE + reg);
      break;
    case 1: /* register 16-31 */
      spin_lock_irqsave(&lp->bank_lock, flags);
      MACEBANK(1);
      outb(data & 0xFF, ioaddr + AM2150_MACE_BASE + (reg & 0x0F));
      MACEBANK(0);
      spin_unlock_irqrestore(&lp->bank_lock, flags);
      break;
  }
} /* mace_write */

/* ----------------------------------------------------------------------------
mace_init
	Resets the MACE chip.
---------------------------------------------------------------------------- */
static int mace_init(mace_private *lp, unsigned int ioaddr, char *enet_addr)
{
  int i;
  int ct = 0;

  /* MACE Software reset */
  mace_write(lp, ioaddr, MACE_BIUCC, 1);
  while (mace_read(lp, ioaddr, MACE_BIUCC) & 0x01) {
    /* Wait for reset bit to be cleared automatically after <= 200ns */;
    if(++ct > 500)
    {
    	printk(KERN_ERR "mace: reset failed, card removed ?\n");
    	return -1;
    }
    udelay(1);
  }
  mace_write(lp, ioaddr, MACE_BIUCC, 0);

  /* The Am2150 requires that the MACE FIFOs operate in burst mode. */
  mace_write(lp, ioaddr, MACE_FIFOCC, 0x0F);

  mace_write(lp,ioaddr, MACE_RCVFC, 0); /* Disable Auto Strip Receive */
  mace_write(lp, ioaddr, MACE_IMR, 0xFF); /* Disable all interrupts until _open */

  /*
   * Bit 2-1 PORTSEL[1-0] Port Select.
   * 00 AUI/10Base-2
   * 01 10Base-T
   * 10 DAI Port (reserved in Am2150)
   * 11 GPSI
   * For this card, only the first two are valid.
   * So, PLSCC should be set to
   * 0x00 for 10Base-2
   * 0x02 for 10Base-T
   * Or just set ASEL in PHYCC below!
   */
  switch (if_port) {
    case 1:
      mace_write(lp, ioaddr, MACE_PLSCC, 0x02);
      break;
    case 2:
      mace_write(lp, ioaddr, MACE_PLSCC, 0x00);
      break;
    default:
      mace_write(lp, ioaddr, MACE_PHYCC, /* ASEL */ 4);
      /* ASEL Auto Select.  When set, the PORTSEL[1-0] bits are overridden,
	 and the MACE device will automatically select the operating media
	 interface port. */
      break;
  }

  mace_write(lp, ioaddr, MACE_IAC, MACE_IAC_ADDRCHG | MACE_IAC_PHYADDR);
  /* Poll ADDRCHG bit */
  ct = 0;
  while (mace_read(lp, ioaddr, MACE_IAC) & MACE_IAC_ADDRCHG)
  {
  	if(++ ct > 500)
  	{
  		printk(KERN_ERR "mace: ADDRCHG timeout, card removed ?\n");
  		return -1;
  	}
  }
  /* Set PADR register */
  for (i = 0; i < ETHER_ADDR_LEN; i++)
    mace_write(lp, ioaddr, MACE_PADR, enet_addr[i]);

  /* MAC Configuration Control Register should be written last */
  /* Let set_multicast_list set this. */
  /* mace_write(lp, ioaddr, MACE_MACCC, MACE_MACCC_ENXMT | MACE_MACCC_ENRCV); */
  mace_write(lp, ioaddr, MACE_MACCC, 0x00);
  return 0;
} /* mace_init */

/* ----------------------------------------------------------------------------
nmclan_config
	This routine is scheduled to run after a CARD_INSERTION event
	is received, to configure the PCMCIA socket, and to make the
	ethernet device available to the system.
---------------------------------------------------------------------------- */

#define CS_CHECK(fn, ret) \
  do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static int nmclan_config(struct pcmcia_device *link)
{
  struct net_device *dev = link->priv;
  mace_private *lp = netdev_priv(dev);
  tuple_t tuple;
  u_char buf[64];
  int i, last_ret, last_fn;
  unsigned int ioaddr;
  DECLARE_MAC_BUF(mac);

  DEBUG(0, "nmclan_config(0x%p)\n", link);

  CS_CHECK(RequestIO, pcmcia_request_io(link, &link->io));
  CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));
  CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));
  dev->irq = link->irq.AssignedIRQ;
  dev->base_addr = link->io.BasePort1;

  ioaddr = dev->base_addr;

  /* Read the ethernet address from the CIS. */
  tuple.DesiredTuple = 0x80 /* CISTPL_CFTABLE_ENTRY_MISC */;
  tuple.TupleData = buf;
  tuple.TupleDataMax = 64;
  tuple.TupleOffset = 0;
  tuple.Attributes = 0;
  CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
  CS_CHECK(GetTupleData, pcmcia_get_tuple_data(link, &tuple));
  memcpy(dev->dev_addr, tuple.TupleData, ETHER_ADDR_LEN);

  /* Verify configuration by reading the MACE ID. */
  {
    char sig[2];

    sig[0] = mace_read(lp, ioaddr, MACE_CHIPIDL);
    sig[1] = mace_read(lp, ioaddr, MACE_CHIPIDH);
    if ((sig[0] == 0x40) && ((sig[1] & 0x0F) == 0x09)) {
      DEBUG(0, "nmclan_cs configured: mace id=%x %x\n",
	    sig[0], sig[1]);
    } else {
      printk(KERN_NOTICE "nmclan_cs: mace id not found: %x %x should"
	     " be 0x40 0x?9\n", sig[0], sig[1]);
      return -ENODEV;
    }
  }

  if(mace_init(lp, ioaddr, dev->dev_addr) == -1)
  	goto failed;

  /* The if_port symbol can be set when the module is loaded */
  if (if_port <= 2)
    dev->if_port = if_port;
  else
    printk(KERN_NOTICE "nmclan_cs: invalid if_port requested\n");

  link->dev_node = &lp->node;
  SET_NETDEV_DEV(dev, &handle_to_dev(link));

  i = register_netdev(dev);
  if (i != 0) {
    printk(KERN_NOTICE "nmclan_cs: register_netdev() failed\n");
    link->dev_node = NULL;
    goto failed;
  }

  strcpy(lp->node.dev_name, dev->name);

  printk(KERN_INFO "%s: nmclan: port %#3lx, irq %d, %s port,"
	 " hw_addr %s\n",
	 dev->name, dev->base_addr, dev->irq, if_names[dev->if_port],
	 print_mac(mac, dev->dev_addr));
  return 0;

cs_failed:
	cs_error(link, last_fn, last_ret);
failed:
	nmclan_release(link);
	return -ENODEV;
} /* nmclan_config */

/* ----------------------------------------------------------------------------
nmclan_release
	After a card is removed, nmclan_release() will unregister the
	net device, and release the PCMCIA configuration.  If the device
	is still open, this will be postponed until it is closed.
---------------------------------------------------------------------------- */
static void nmclan_release(struct pcmcia_device *link)
{
	DEBUG(0, "nmclan_release(0x%p)\n", link);
	pcmcia_disable_device(link);
}

static int nmclan_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int nmclan_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		nmclan_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}


/* ----------------------------------------------------------------------------
nmclan_reset
	Reset and restore all of the Xilinx and MACE registers.
---------------------------------------------------------------------------- */
static void nmclan_reset(struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);

#if RESET_XILINX
  struct pcmcia_device *link = &lp->link;
  conf_reg_t reg;
  u_long OrigCorValue; 

  /* Save original COR value */
  reg.Function = 0;
  reg.Action = CS_READ;
  reg.Offset = CISREG_COR;
  reg.Value = 0;
  pcmcia_access_configuration_register(link, &reg);
  OrigCorValue = reg.Value;

  /* Reset Xilinx */
  reg.Action = CS_WRITE;
  reg.Offset = CISREG_COR;
  DEBUG(1, "nmclan_reset: OrigCorValue=0x%lX, resetting...\n",
	OrigCorValue);
  reg.Value = COR_SOFT_RESET;
  pcmcia_access_configuration_register(link, &reg);
  /* Need to wait for 20 ms for PCMCIA to finish reset. */

  /* Restore original COR configuration index */
  reg.Value = COR_LEVEL_REQ | (OrigCorValue & COR_CONFIG_MASK);
  pcmcia_access_configuration_register(link, &reg);
  /* Xilinx is now completely reset along with the MACE chip. */
  lp->tx_free_frames=AM2150_MAX_TX_FRAMES;

#endif /* #if RESET_XILINX */

  /* Xilinx is now completely reset along with the MACE chip. */
  lp->tx_free_frames=AM2150_MAX_TX_FRAMES;

  /* Reinitialize the MACE chip for operation. */
  mace_init(lp, dev->base_addr, dev->dev_addr);
  mace_write(lp, dev->base_addr, MACE_IMR, MACE_IMR_DEFAULT);

  /* Restore the multicast list and enable TX and RX. */
  restore_multicast_list(dev);
} /* nmclan_reset */

/* ----------------------------------------------------------------------------
mace_config
	[Someone tell me what this is supposed to do?  Is if_port a defined
	standard?  If so, there should be defines to indicate 1=10Base-T,
	2=10Base-2, etc. including limited automatic detection.]
---------------------------------------------------------------------------- */
static int mace_config(struct net_device *dev, struct ifmap *map)
{
  if ((map->port != (u_char)(-1)) && (map->port != dev->if_port)) {
    if (map->port <= 2) {
      dev->if_port = map->port;
      printk(KERN_INFO "%s: switched to %s port\n", dev->name,
	     if_names[dev->if_port]);
    } else
      return -EINVAL;
  }
  return 0;
} /* mace_config */

/* ----------------------------------------------------------------------------
mace_open
	Open device driver.
---------------------------------------------------------------------------- */
static int mace_open(struct net_device *dev)
{
  unsigned int ioaddr = dev->base_addr;
  mace_private *lp = netdev_priv(dev);
  struct pcmcia_device *link = lp->p_dev;

  if (!pcmcia_dev_present(link))
    return -ENODEV;

  link->open++;

  MACEBANK(0);

  netif_start_queue(dev);
  nmclan_reset(dev);

  return 0; /* Always succeed */
} /* mace_open */

/* ----------------------------------------------------------------------------
mace_close
	Closes device driver.
---------------------------------------------------------------------------- */
static int mace_close(struct net_device *dev)
{
  unsigned int ioaddr = dev->base_addr;
  mace_private *lp = netdev_priv(dev);
  struct pcmcia_device *link = lp->p_dev;

  DEBUG(2, "%s: shutting down ethercard.\n", dev->name);

  /* Mask off all interrupts from the MACE chip. */
  outb(0xFF, ioaddr + AM2150_MACE_BASE + MACE_IMR);

  link->open--;
  netif_stop_queue(dev);

  return 0;
} /* mace_close */

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	sprintf(info->bus_info, "PCMCIA 0x%lx", dev->base_addr);
}

#ifdef PCMCIA_DEBUG
static u32 netdev_get_msglevel(struct net_device *dev)
{
	return pc_debug;
}

static void netdev_set_msglevel(struct net_device *dev, u32 level)
{
	pc_debug = level;
}
#endif /* PCMCIA_DEBUG */

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
#ifdef PCMCIA_DEBUG
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
#endif /* PCMCIA_DEBUG */
};

/* ----------------------------------------------------------------------------
mace_start_xmit
	This routine begins the packet transmit function.  When completed,
	it will generate a transmit interrupt.

	According to /usr/src/linux/net/inet/dev.c, if _start_xmit
	returns 0, the "packet is now solely the responsibility of the
	driver."  If _start_xmit returns non-zero, the "transmission
	failed, put skb back into a list."
---------------------------------------------------------------------------- */

static void mace_tx_timeout(struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);
  struct pcmcia_device *link = lp->p_dev;

  printk(KERN_NOTICE "%s: transmit timed out -- ", dev->name);
#if RESET_ON_TIMEOUT
  printk("resetting card\n");
  pcmcia_reset_card(link->socket);
#else /* #if RESET_ON_TIMEOUT */
  printk("NOT resetting card\n");
#endif /* #if RESET_ON_TIMEOUT */
  dev->trans_start = jiffies;
  netif_wake_queue(dev);
}

static int mace_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);
  unsigned int ioaddr = dev->base_addr;

  netif_stop_queue(dev);

  DEBUG(3, "%s: mace_start_xmit(length = %ld) called.\n",
	dev->name, (long)skb->len);

#if (!TX_INTERRUPTABLE)
  /* Disable MACE TX interrupts. */
  outb(MACE_IMR_DEFAULT | MACE_IR_XMTINT,
    ioaddr + AM2150_MACE_BASE + MACE_IMR);
  lp->tx_irq_disabled=1;
#endif /* #if (!TX_INTERRUPTABLE) */

  {
    /* This block must not be interrupted by another transmit request!
       mace_tx_timeout will take care of timer-based retransmissions from
       the upper layers.  The interrupt handler is guaranteed never to
       service a transmit interrupt while we are in here.
    */

    lp->linux_stats.tx_bytes += skb->len;
    lp->tx_free_frames--;

    /* WARNING: Write the _exact_ number of bytes written in the header! */
    /* Put out the word header [must be an outw()] . . . */
    outw(skb->len, ioaddr + AM2150_XMT);
    /* . . . and the packet [may be any combination of outw() and outb()] */
    outsw(ioaddr + AM2150_XMT, skb->data, skb->len >> 1);
    if (skb->len & 1) {
      /* Odd byte transfer */
      outb(skb->data[skb->len-1], ioaddr + AM2150_XMT);
    }

    dev->trans_start = jiffies;

#if MULTI_TX
    if (lp->tx_free_frames > 0)
      netif_start_queue(dev);
#endif /* #if MULTI_TX */
  }

#if (!TX_INTERRUPTABLE)
  /* Re-enable MACE TX interrupts. */
  lp->tx_irq_disabled=0;
  outb(MACE_IMR_DEFAULT, ioaddr + AM2150_MACE_BASE + MACE_IMR);
#endif /* #if (!TX_INTERRUPTABLE) */

  dev_kfree_skb(skb);

  return 0;
} /* mace_start_xmit */

/* ----------------------------------------------------------------------------
mace_interrupt
	The interrupt handler.
---------------------------------------------------------------------------- */
static irqreturn_t mace_interrupt(int irq, void *dev_id)
{
  struct net_device *dev = (struct net_device *) dev_id;
  mace_private *lp = netdev_priv(dev);
  unsigned int ioaddr;
  int status;
  int IntrCnt = MACE_MAX_IR_ITERATIONS;

  if (dev == NULL) {
    DEBUG(2, "mace_interrupt(): irq 0x%X for unknown device.\n",
	  irq);
    return IRQ_NONE;
  }

  ioaddr = dev->base_addr;

  if (lp->tx_irq_disabled) {
    printk(
      (lp->tx_irq_disabled?
       KERN_NOTICE "%s: Interrupt with tx_irq_disabled "
       "[isr=%02X, imr=%02X]\n": 
       KERN_NOTICE "%s: Re-entering the interrupt handler "
       "[isr=%02X, imr=%02X]\n"),
      dev->name,
      inb(ioaddr + AM2150_MACE_BASE + MACE_IR),
      inb(ioaddr + AM2150_MACE_BASE + MACE_IMR)
    );
    /* WARNING: MACE_IR has been read! */
    return IRQ_NONE;
  }

  if (!netif_device_present(dev)) {
    DEBUG(2, "%s: interrupt from dead card\n", dev->name);
    return IRQ_NONE;
  }

  do {
    /* WARNING: MACE_IR is a READ/CLEAR port! */
    status = inb(ioaddr + AM2150_MACE_BASE + MACE_IR);

    DEBUG(3, "mace_interrupt: irq 0x%X status 0x%X.\n", irq, status);

    if (status & MACE_IR_RCVINT) {
      mace_rx(dev, MACE_MAX_RX_ITERATIONS);
    }

    if (status & MACE_IR_XMTINT) {
      unsigned char fifofc;
      unsigned char xmtrc;
      unsigned char xmtfs;

      fifofc = inb(ioaddr + AM2150_MACE_BASE + MACE_FIFOFC);
      if ((fifofc & MACE_FIFOFC_XMTFC)==0) {
	lp->linux_stats.tx_errors++;
	outb(0xFF, ioaddr + AM2150_XMT_SKIP);
      }

      /* Transmit Retry Count (XMTRC, reg 4) */
      xmtrc = inb(ioaddr + AM2150_MACE_BASE + MACE_XMTRC);
      if (xmtrc & MACE_XMTRC_EXDEF) lp->mace_stats.exdef++;
      lp->mace_stats.xmtrc += (xmtrc & MACE_XMTRC_XMTRC);

      if (
        (xmtfs = inb(ioaddr + AM2150_MACE_BASE + MACE_XMTFS)) &
        MACE_XMTFS_XMTSV /* Transmit Status Valid */
      ) {
	lp->mace_stats.xmtsv++;

	if (xmtfs & ~MACE_XMTFS_XMTSV) {
	  if (xmtfs & MACE_XMTFS_UFLO) {
	    /* Underflow.  Indicates that the Transmit FIFO emptied before
	       the end of frame was reached. */
	    lp->mace_stats.uflo++;
	  }
	  if (xmtfs & MACE_XMTFS_LCOL) {
	    /* Late Collision */
	    lp->mace_stats.lcol++;
	  }
	  if (xmtfs & MACE_XMTFS_MORE) {
	    /* MORE than one retry was needed */
	    lp->mace_stats.more++;
	  }
	  if (xmtfs & MACE_XMTFS_ONE) {
	    /* Exactly ONE retry occurred */
	    lp->mace_stats.one++;
	  }
	  if (xmtfs & MACE_XMTFS_DEFER) {
	    /* Transmission was defered */
	    lp->mace_stats.defer++;
	  }
	  if (xmtfs & MACE_XMTFS_LCAR) {
	    /* Loss of carrier */
	    lp->mace_stats.lcar++;
	  }
	  if (xmtfs & MACE_XMTFS_RTRY) {
	    /* Retry error: transmit aborted after 16 attempts */
	    lp->mace_stats.rtry++;
	  }
        } /* if (xmtfs & ~MACE_XMTFS_XMTSV) */

      } /* if (xmtfs & MACE_XMTFS_XMTSV) */

      lp->linux_stats.tx_packets++;
      lp->tx_free_frames++;
      netif_wake_queue(dev);
    } /* if (status & MACE_IR_XMTINT) */

    if (status & ~MACE_IMR_DEFAULT & ~MACE_IR_RCVINT & ~MACE_IR_XMTINT) {
      if (status & MACE_IR_JAB) {
        /* Jabber Error.  Excessive transmit duration (20-150ms). */
        lp->mace_stats.jab++;
      }
      if (status & MACE_IR_BABL) {
        /* Babble Error.  >1518 bytes transmitted. */
        lp->mace_stats.babl++;
      }
      if (status & MACE_IR_CERR) {
	/* Collision Error.  CERR indicates the absence of the
	   Signal Quality Error Test message after a packet
	   transmission. */
        lp->mace_stats.cerr++;
      }
      if (status & MACE_IR_RCVCCO) {
        /* Receive Collision Count Overflow; */
        lp->mace_stats.rcvcco++;
      }
      if (status & MACE_IR_RNTPCO) {
        /* Runt Packet Count Overflow */
        lp->mace_stats.rntpco++;
      }
      if (status & MACE_IR_MPCO) {
        /* Missed Packet Count Overflow */
        lp->mace_stats.mpco++;
      }
    } /* if (status & ~MACE_IMR_DEFAULT & ~MACE_IR_RCVINT & ~MACE_IR_XMTINT) */

  } while ((status & ~MACE_IMR_DEFAULT) && (--IntrCnt));

  return IRQ_HANDLED;
} /* mace_interrupt */

/* ----------------------------------------------------------------------------
mace_rx
	Receives packets.
---------------------------------------------------------------------------- */
static int mace_rx(struct net_device *dev, unsigned char RxCnt)
{
  mace_private *lp = netdev_priv(dev);
  unsigned int ioaddr = dev->base_addr;
  unsigned char rx_framecnt;
  unsigned short rx_status;

  while (
    ((rx_framecnt = inb(ioaddr + AM2150_RCV_FRAME_COUNT)) > 0) &&
    (rx_framecnt <= 12) && /* rx_framecnt==0xFF if card is extracted. */
    (RxCnt--)
  ) {
    rx_status = inw(ioaddr + AM2150_RCV);

    DEBUG(3, "%s: in mace_rx(), framecnt 0x%X, rx_status"
	  " 0x%X.\n", dev->name, rx_framecnt, rx_status);

    if (rx_status & MACE_RCVFS_RCVSTS) { /* Error, update stats. */
      lp->linux_stats.rx_errors++;
      if (rx_status & MACE_RCVFS_OFLO) {
        lp->mace_stats.oflo++;
      }
      if (rx_status & MACE_RCVFS_CLSN) {
        lp->mace_stats.clsn++;
      }
      if (rx_status & MACE_RCVFS_FRAM) {
	lp->mace_stats.fram++;
      }
      if (rx_status & MACE_RCVFS_FCS) {
        lp->mace_stats.fcs++;
      }
    } else {
      short pkt_len = (rx_status & ~MACE_RCVFS_RCVSTS) - 4;
        /* Auto Strip is off, always subtract 4 */
      struct sk_buff *skb;

      lp->mace_stats.rfs_rntpc += inb(ioaddr + AM2150_RCV);
        /* runt packet count */
      lp->mace_stats.rfs_rcvcc += inb(ioaddr + AM2150_RCV);
        /* rcv collision count */

      DEBUG(3, "    receiving packet size 0x%X rx_status"
	    " 0x%X.\n", pkt_len, rx_status);

      skb = dev_alloc_skb(pkt_len+2);

      if (skb != NULL) {
	skb_reserve(skb, 2);
	insw(ioaddr + AM2150_RCV, skb_put(skb, pkt_len), pkt_len>>1);
	if (pkt_len & 1)
	    *(skb_tail_pointer(skb) - 1) = inb(ioaddr + AM2150_RCV);
	skb->protocol = eth_type_trans(skb, dev);
	
	netif_rx(skb); /* Send the packet to the upper (protocol) layers. */

	dev->last_rx = jiffies;
	lp->linux_stats.rx_packets++;
	lp->linux_stats.rx_bytes += pkt_len;
	outb(0xFF, ioaddr + AM2150_RCV_NEXT); /* skip to next frame */
	continue;
      } else {
	DEBUG(1, "%s: couldn't allocate a sk_buff of size"
	      " %d.\n", dev->name, pkt_len);
	lp->linux_stats.rx_dropped++;
      }
    }
    outb(0xFF, ioaddr + AM2150_RCV_NEXT); /* skip to next frame */
  } /* while */

  return 0;
} /* mace_rx */

/* ----------------------------------------------------------------------------
pr_linux_stats
---------------------------------------------------------------------------- */
static void pr_linux_stats(struct net_device_stats *pstats)
{
  DEBUG(2, "pr_linux_stats\n");
  DEBUG(2, " rx_packets=%-7ld        tx_packets=%ld\n",
	(long)pstats->rx_packets, (long)pstats->tx_packets);
  DEBUG(2, " rx_errors=%-7ld         tx_errors=%ld\n",
	(long)pstats->rx_errors, (long)pstats->tx_errors);
  DEBUG(2, " rx_dropped=%-7ld        tx_dropped=%ld\n",
	(long)pstats->rx_dropped, (long)pstats->tx_dropped);
  DEBUG(2, " multicast=%-7ld         collisions=%ld\n",
	(long)pstats->multicast, (long)pstats->collisions);

  DEBUG(2, " rx_length_errors=%-7ld  rx_over_errors=%ld\n",
	(long)pstats->rx_length_errors, (long)pstats->rx_over_errors);
  DEBUG(2, " rx_crc_errors=%-7ld     rx_frame_errors=%ld\n",
	(long)pstats->rx_crc_errors, (long)pstats->rx_frame_errors);
  DEBUG(2, " rx_fifo_errors=%-7ld    rx_missed_errors=%ld\n",
	(long)pstats->rx_fifo_errors, (long)pstats->rx_missed_errors);

  DEBUG(2, " tx_aborted_errors=%-7ld tx_carrier_errors=%ld\n",
	(long)pstats->tx_aborted_errors, (long)pstats->tx_carrier_errors);
  DEBUG(2, " tx_fifo_errors=%-7ld    tx_heartbeat_errors=%ld\n",
	(long)pstats->tx_fifo_errors, (long)pstats->tx_heartbeat_errors);
  DEBUG(2, " tx_window_errors=%ld\n",
	(long)pstats->tx_window_errors);
} /* pr_linux_stats */

/* ----------------------------------------------------------------------------
pr_mace_stats
---------------------------------------------------------------------------- */
static void pr_mace_stats(mace_statistics *pstats)
{
  DEBUG(2, "pr_mace_stats\n");

  DEBUG(2, " xmtsv=%-7d             uflo=%d\n",
	pstats->xmtsv, pstats->uflo);
  DEBUG(2, " lcol=%-7d              more=%d\n",
	pstats->lcol, pstats->more);
  DEBUG(2, " one=%-7d               defer=%d\n",
	pstats->one, pstats->defer);
  DEBUG(2, " lcar=%-7d              rtry=%d\n",
	pstats->lcar, pstats->rtry);

  /* MACE_XMTRC */
  DEBUG(2, " exdef=%-7d             xmtrc=%d\n",
	pstats->exdef, pstats->xmtrc);

  /* RFS1--Receive Status (RCVSTS) */
  DEBUG(2, " oflo=%-7d              clsn=%d\n",
	pstats->oflo, pstats->clsn);
  DEBUG(2, " fram=%-7d              fcs=%d\n",
	pstats->fram, pstats->fcs);

  /* RFS2--Runt Packet Count (RNTPC) */
  /* RFS3--Receive Collision Count (RCVCC) */
  DEBUG(2, " rfs_rntpc=%-7d         rfs_rcvcc=%d\n",
	pstats->rfs_rntpc, pstats->rfs_rcvcc);

  /* MACE_IR */
  DEBUG(2, " jab=%-7d               babl=%d\n",
	pstats->jab, pstats->babl);
  DEBUG(2, " cerr=%-7d              rcvcco=%d\n",
	pstats->cerr, pstats->rcvcco);
  DEBUG(2, " rntpco=%-7d            mpco=%d\n",
	pstats->rntpco, pstats->mpco);

  /* MACE_MPC */
  DEBUG(2, " mpc=%d\n", pstats->mpc);

  /* MACE_RNTPC */
  DEBUG(2, " rntpc=%d\n", pstats->rntpc);

  /* MACE_RCVCC */
  DEBUG(2, " rcvcc=%d\n", pstats->rcvcc);

} /* pr_mace_stats */

/* ----------------------------------------------------------------------------
update_stats
	Update statistics.  We change to register window 1, so this
	should be run single-threaded if the device is active. This is
	expected to be a rare operation, and it's simpler for the rest
	of the driver to assume that window 0 is always valid rather
	than use a special window-state variable.

	oflo & uflo should _never_ occur since it would mean the Xilinx
	was not able to transfer data between the MACE FIFO and the
	card's SRAM fast enough.  If this happens, something is
	seriously wrong with the hardware.
---------------------------------------------------------------------------- */
static void update_stats(unsigned int ioaddr, struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);

  lp->mace_stats.rcvcc += mace_read(lp, ioaddr, MACE_RCVCC);
  lp->mace_stats.rntpc += mace_read(lp, ioaddr, MACE_RNTPC);
  lp->mace_stats.mpc += mace_read(lp, ioaddr, MACE_MPC);
  /* At this point, mace_stats is fully updated for this call.
     We may now update the linux_stats. */

  /* The MACE has no equivalent for linux_stats field which are commented
     out. */

  /* lp->linux_stats.multicast; */
  lp->linux_stats.collisions = 
    lp->mace_stats.rcvcco * 256 + lp->mace_stats.rcvcc;
    /* Collision: The MACE may retry sending a packet 15 times
       before giving up.  The retry count is in XMTRC.
       Does each retry constitute a collision?
       If so, why doesn't the RCVCC record these collisions? */

  /* detailed rx_errors: */
  lp->linux_stats.rx_length_errors = 
    lp->mace_stats.rntpco * 256 + lp->mace_stats.rntpc;
  /* lp->linux_stats.rx_over_errors */
  lp->linux_stats.rx_crc_errors = lp->mace_stats.fcs;
  lp->linux_stats.rx_frame_errors = lp->mace_stats.fram;
  lp->linux_stats.rx_fifo_errors = lp->mace_stats.oflo;
  lp->linux_stats.rx_missed_errors = 
    lp->mace_stats.mpco * 256 + lp->mace_stats.mpc;

  /* detailed tx_errors */
  lp->linux_stats.tx_aborted_errors = lp->mace_stats.rtry;
  lp->linux_stats.tx_carrier_errors = lp->mace_stats.lcar;
    /* LCAR usually results from bad cabling. */
  lp->linux_stats.tx_fifo_errors = lp->mace_stats.uflo;
  lp->linux_stats.tx_heartbeat_errors = lp->mace_stats.cerr;
  /* lp->linux_stats.tx_window_errors; */

  return;
} /* update_stats */

/* ----------------------------------------------------------------------------
mace_get_stats
	Gathers ethernet statistics from the MACE chip.
---------------------------------------------------------------------------- */
static struct net_device_stats *mace_get_stats(struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);

  update_stats(dev->base_addr, dev);

  DEBUG(1, "%s: updating the statistics.\n", dev->name);
  pr_linux_stats(&lp->linux_stats);
  pr_mace_stats(&lp->mace_stats);

  return &lp->linux_stats;
} /* net_device_stats */

/* ----------------------------------------------------------------------------
updateCRC
	Modified from Am79C90 data sheet.
---------------------------------------------------------------------------- */

#ifdef BROKEN_MULTICAST

static void updateCRC(int *CRC, int bit)
{
  int poly[]={
    1,1,1,0, 1,1,0,1,
    1,0,1,1, 1,0,0,0,
    1,0,0,0, 0,0,1,1,
    0,0,1,0, 0,0,0,0
  }; /* CRC polynomial.  poly[n] = coefficient of the x**n term of the
	CRC generator polynomial. */

  int j;

  /* shift CRC and control bit (CRC[32]) */
  for (j = 32; j > 0; j--)
    CRC[j] = CRC[j-1];
  CRC[0] = 0;

  /* If bit XOR(control bit) = 1, set CRC = CRC XOR polynomial. */
  if (bit ^ CRC[32])
    for (j = 0; j < 32; j++)
      CRC[j] ^= poly[j];
} /* updateCRC */

/* ----------------------------------------------------------------------------
BuildLAF
	Build logical address filter.
	Modified from Am79C90 data sheet.

Input
	ladrf: logical address filter (contents initialized to 0)
	adr: ethernet address
---------------------------------------------------------------------------- */
static void BuildLAF(int *ladrf, int *adr)
{
  int CRC[33]={1}; /* CRC register, 1 word/bit + extra control bit */

  int i, byte; /* temporary array indices */
  int hashcode; /* the output object */

  CRC[32]=0;

  for (byte = 0; byte < 6; byte++)
    for (i = 0; i < 8; i++)
      updateCRC(CRC, (adr[byte] >> i) & 1);

  hashcode = 0;
  for (i = 0; i < 6; i++)
    hashcode = (hashcode << 1) + CRC[i];

  byte = hashcode >> 3;
  ladrf[byte] |= (1 << (hashcode & 7));

#ifdef PCMCIA_DEBUG
  if (pc_debug > 2) {
    printk(KERN_DEBUG "    adr =");
    for (i = 0; i < 6; i++)
      printk(" %02X", adr[i]);
    printk("\n" KERN_DEBUG "    hashcode = %d(decimal), ladrf[0:63]"
	   " =", hashcode);
    for (i = 0; i < 8; i++)
      printk(" %02X", ladrf[i]);
    printk("\n");
  }
#endif
} /* BuildLAF */

/* ----------------------------------------------------------------------------
restore_multicast_list
	Restores the multicast filter for MACE chip to the last
	set_multicast_list() call.

Input
	multicast_num_addrs
	multicast_ladrf[]
---------------------------------------------------------------------------- */
static void restore_multicast_list(struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);
  int num_addrs = lp->multicast_num_addrs;
  int *ladrf = lp->multicast_ladrf;
  unsigned int ioaddr = dev->base_addr;
  int i;

  DEBUG(2, "%s: restoring Rx mode to %d addresses.\n",
	dev->name, num_addrs);

  if (num_addrs > 0) {

    DEBUG(1, "Attempt to restore multicast list detected.\n");

    mace_write(lp, ioaddr, MACE_IAC, MACE_IAC_ADDRCHG | MACE_IAC_LOGADDR);
    /* Poll ADDRCHG bit */
    while (mace_read(lp, ioaddr, MACE_IAC) & MACE_IAC_ADDRCHG)
      ;
    /* Set LADRF register */
    for (i = 0; i < MACE_LADRF_LEN; i++)
      mace_write(lp, ioaddr, MACE_LADRF, ladrf[i]);

    mace_write(lp, ioaddr, MACE_UTR, MACE_UTR_RCVFCSE | MACE_UTR_LOOP_EXTERNAL);
    mace_write(lp, ioaddr, MACE_MACCC, MACE_MACCC_ENXMT | MACE_MACCC_ENRCV);

  } else if (num_addrs < 0) {

    /* Promiscuous mode: receive all packets */
    mace_write(lp, ioaddr, MACE_UTR, MACE_UTR_LOOP_EXTERNAL);
    mace_write(lp, ioaddr, MACE_MACCC,
      MACE_MACCC_PROM | MACE_MACCC_ENXMT | MACE_MACCC_ENRCV
    );

  } else {

    /* Normal mode */
    mace_write(lp, ioaddr, MACE_UTR, MACE_UTR_LOOP_EXTERNAL);
    mace_write(lp, ioaddr, MACE_MACCC, MACE_MACCC_ENXMT | MACE_MACCC_ENRCV);

  }
} /* restore_multicast_list */

/* ----------------------------------------------------------------------------
set_multicast_list
	Set or clear the multicast filter for this adaptor.

Input
	num_addrs == -1	Promiscuous mode, receive all packets
	num_addrs == 0	Normal mode, clear multicast list
	num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
Output
	multicast_num_addrs
	multicast_ladrf[]
---------------------------------------------------------------------------- */

static void set_multicast_list(struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);
  int adr[ETHER_ADDR_LEN] = {0}; /* Ethernet address */
  int i;
  struct dev_mc_list *dmi = dev->mc_list;

#ifdef PCMCIA_DEBUG
  if (pc_debug > 1) {
    static int old;
    if (dev->mc_count != old) {
      old = dev->mc_count;
      DEBUG(0, "%s: setting Rx mode to %d addresses.\n",
	    dev->name, old);
    }
  }
#endif

  /* Set multicast_num_addrs. */
  lp->multicast_num_addrs = dev->mc_count;

  /* Set multicast_ladrf. */
  if (num_addrs > 0) {
    /* Calculate multicast logical address filter */
    memset(lp->multicast_ladrf, 0, MACE_LADRF_LEN);
    for (i = 0; i < dev->mc_count; i++) {
      memcpy(adr, dmi->dmi_addr, ETHER_ADDR_LEN);
      dmi = dmi->next;
      BuildLAF(lp->multicast_ladrf, adr);
    }
  }

  restore_multicast_list(dev);

} /* set_multicast_list */

#endif /* BROKEN_MULTICAST */

static void restore_multicast_list(struct net_device *dev)
{
  unsigned int ioaddr = dev->base_addr;
  mace_private *lp = netdev_priv(dev);

  DEBUG(2, "%s: restoring Rx mode to %d addresses.\n", dev->name,
	lp->multicast_num_addrs);

  if (dev->flags & IFF_PROMISC) {
    /* Promiscuous mode: receive all packets */
    mace_write(lp,ioaddr, MACE_UTR, MACE_UTR_LOOP_EXTERNAL);
    mace_write(lp, ioaddr, MACE_MACCC,
      MACE_MACCC_PROM | MACE_MACCC_ENXMT | MACE_MACCC_ENRCV
    );
  } else {
    /* Normal mode */
    mace_write(lp, ioaddr, MACE_UTR, MACE_UTR_LOOP_EXTERNAL);
    mace_write(lp, ioaddr, MACE_MACCC, MACE_MACCC_ENXMT | MACE_MACCC_ENRCV);
  }
} /* restore_multicast_list */

static void set_multicast_list(struct net_device *dev)
{
  mace_private *lp = netdev_priv(dev);

#ifdef PCMCIA_DEBUG
  if (pc_debug > 1) {
    static int old;
    if (dev->mc_count != old) {
      old = dev->mc_count;
      DEBUG(0, "%s: setting Rx mode to %d addresses.\n",
	    dev->name, old);
    }
  }
#endif

  lp->multicast_num_addrs = dev->mc_count;
  restore_multicast_list(dev);

} /* set_multicast_list */

static struct pcmcia_device_id nmclan_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("New Media Corporation", "Ethernet", 0x085a850b, 0x00b2e941),
	PCMCIA_DEVICE_PROD_ID12("Portable Add-ons", "Ethernet+", 0xebf1d60, 0xad673aaf),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, nmclan_ids);

static struct pcmcia_driver nmclan_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "nmclan_cs",
	},
	.probe		= nmclan_probe,
	.remove		= nmclan_detach,
	.id_table       = nmclan_ids,
	.suspend	= nmclan_suspend,
	.resume		= nmclan_resume,
};

static int __init init_nmclan_cs(void)
{
	return pcmcia_register_driver(&nmclan_cs_driver);
}

static void __exit exit_nmclan_cs(void)
{
	pcmcia_unregister_driver(&nmclan_cs_driver);
}

module_init(init_nmclan_cs);
module_exit(exit_nmclan_cs);

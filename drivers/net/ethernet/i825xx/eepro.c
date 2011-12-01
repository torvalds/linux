/* eepro.c: Intel EtherExpress Pro/10 device driver for Linux. */
/*
	Written 1994, 1995,1996 by Bao C. Ha.

	Copyright (C) 1994, 1995,1996 by Bao C. Ha.

	This software may be used and distributed
	according to the terms of the GNU General Public License,
	incorporated herein by reference.

	The author may be reached at bao.ha@srs.gov
	or 418 Hastings Place, Martinez, GA 30907.

	Things remaining to do:
	Better record keeping of errors.
	Eliminate transmit interrupt to reduce overhead.
	Implement "concurrent processing". I won't be doing it!

	Bugs:

	If you have a problem of not detecting the 82595 during a
	reboot (warm reset), disable the FLASH memory should fix it.
	This is a compatibility hardware problem.

	Versions:
	0.13b	basic ethtool support (aris, 09/13/2004)
	0.13a   in memory shortage, drop packets also in board
		(Michael Westermann <mw@microdata-pos.de>, 07/30/2002)
	0.13    irq sharing, rewrote probe function, fixed a nasty bug in
		hardware_send_packet and a major cleanup (aris, 11/08/2001)
	0.12d	fixing a problem with single card detected as eight eth devices
		fixing a problem with sudden drop in card performance
		(chris (asdn@go2.pl), 10/29/2001)
	0.12c	fixing some problems with old cards (aris, 01/08/2001)
	0.12b	misc fixes (aris, 06/26/2000)
	0.12a   port of version 0.12a of 2.2.x kernels to 2.3.x
		(aris (aris@conectiva.com.br), 05/19/2000)
	0.11e   some tweaks about multiple cards support (PdP, jul/aug 1999)
	0.11d	added __initdata, __init stuff; call spin_lock_init
	        in eepro_probe1. Replaced "eepro" by dev->name. Augmented
		the code protected by spin_lock in interrupt routine
		(PdP, 12/12/1998)
	0.11c   minor cleanup (PdP, RMC, 09/12/1998)
	0.11b   Pascal Dupuis (dupuis@lei.ucl.ac.be): works as a module
	        under 2.1.xx. Debug messages are flagged as KERN_DEBUG to
		avoid console flooding. Added locking at critical parts. Now
		the dawn thing is SMP safe.
	0.11a   Attempt to get 2.1.xx support up (RMC)
	0.11	Brian Candler added support for multiple cards. Tested as
		a module, no idea if it works when compiled into kernel.

	0.10e	Rick Bressler notified me that ifconfig up;ifconfig down fails
		because the irq is lost somewhere. Fixed that by moving
		request_irq and free_irq to eepro_open and eepro_close respectively.
	0.10d	Ugh! Now Wakeup works. Was seriously broken in my first attempt.
		I'll need to find a way to specify an ioport other than
		the default one in the PnP case. PnP definitively sucks.
		And, yes, this is not the only reason.
	0.10c	PnP Wakeup Test for 595FX. uncomment #define PnPWakeup;
		to use.
	0.10b	Should work now with (some) Pro/10+. At least for
		me (and my two cards) it does. _No_ guarantee for
		function with non-Pro/10+ cards! (don't have any)
		(RMC, 9/11/96)

	0.10	Added support for the Etherexpress Pro/10+.  The
		IRQ map was changed significantly from the old
		pro/10.  The new interrupt map was provided by
		Rainer M. Canavan (Canavan@Zeus.cs.bonn.edu).
		(BCH, 9/3/96)

	0.09	Fixed a race condition in the transmit algorithm,
		which causes crashes under heavy load with fast
		pentium computers.  The performance should also
		improve a bit.  The size of RX buffer, and hence
		TX buffer, can also be changed via lilo or insmod.
		(BCH, 7/31/96)

	0.08	Implement 32-bit I/O for the 82595TX and 82595FX
		based lan cards.  Disable full-duplex mode if TPE
		is not used.  (BCH, 4/8/96)

	0.07a	Fix a stat report which counts every packet as a
		heart-beat failure. (BCH, 6/3/95)

	0.07	Modified to support all other 82595-based lan cards.
		The IRQ vector of the EtherExpress Pro will be set
		according to the value saved in the EEPROM.  For other
		cards, I will do autoirq_request() to grab the next
		available interrupt vector. (BCH, 3/17/95)

	0.06a,b	Interim released.  Minor changes in the comments and
		print out format. (BCH, 3/9/95 and 3/14/95)

	0.06	First stable release that I am comfortable with. (BCH,
		3/2/95)

	0.05	Complete testing of multicast. (BCH, 2/23/95)

	0.04	Adding multicast support. (BCH, 2/14/95)

	0.03	First widely alpha release for public testing.
		(BCH, 2/14/95)

*/

static const char version[] =
	"eepro.c: v0.13b 09/13/2004 aris@cathedrallabs.org\n";

#include <linux/module.h>

/*
  Sources:

	This driver wouldn't have been written without the availability
	of the Crynwr's Lan595 driver source code.  It helps me to
	familiarize with the 82595 chipset while waiting for the Intel
	documentation.  I also learned how to detect the 82595 using
	the packet driver's technique.

	This driver is written by cutting and pasting the skeleton.c driver
	provided by Donald Becker.  I also borrowed the EEPROM routine from
	Donald Becker's 82586 driver.

	Datasheet for the Intel 82595 (including the TX and FX version). It
	provides just enough info that the casual reader might think that it
	documents the i82595.

	The User Manual for the 82595.  It provides a lot of the missing
	information.

*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/ethtool.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>

#define DRV_NAME "eepro"
#define DRV_VERSION "0.13c"

#define compat_dev_kfree_skb( skb, mode ) dev_kfree_skb( (skb) )
/* I had reports of looong delays with SLOW_DOWN defined as udelay(2) */
#define SLOW_DOWN inb(0x80)
/* udelay(2) */
#define compat_init_data     __initdata
enum iftype { AUI=0, BNC=1, TPE=2 };

/* First, a few definitions that the brave might change. */
/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int eepro_portlist[] compat_init_data =
   { 0x300, 0x210, 0x240, 0x280, 0x2C0, 0x200, 0x320, 0x340, 0x360, 0};
/* note: 0x300 is default, the 595FX supports ALL IO Ports
  from 0x000 to 0x3F0, some of which are reserved in PCs */

/* To try the (not-really PnP Wakeup: */
/*
#define PnPWakeup
*/

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 0
#endif
static unsigned int net_debug = NET_DEBUG;

/* The number of low I/O ports used by the ethercard. */
#define EEPRO_IO_EXTENT	16

/* Different 82595 chips */
#define	LAN595		0
#define	LAN595TX	1
#define	LAN595FX	2
#define	LAN595FX_10ISA	3

/* Information that need to be kept for each board. */
struct eepro_local {
	unsigned rx_start;
	unsigned tx_start; /* start of the transmit chain */
	int tx_last;  /* pointer to last packet in the transmit chain */
	unsigned tx_end;   /* end of the transmit chain (plus 1) */
	int eepro;	/* 1 for the EtherExpress Pro/10,
			   2 for the EtherExpress Pro/10+,
			   3 for the EtherExpress 10 (blue cards),
			   0 for other 82595-based lan cards. */
	int version;	/* a flag to indicate if this is a TX or FX
				   version of the 82595 chip. */
	int stepping;

	spinlock_t lock; /* Serializing lock  */

	unsigned rcv_ram;	/* pre-calculated space for rx */
	unsigned xmt_ram;	/* pre-calculated space for tx */
	unsigned char xmt_bar;
	unsigned char xmt_lower_limit_reg;
	unsigned char xmt_upper_limit_reg;
	short xmt_lower_limit;
	short xmt_upper_limit;
	short rcv_lower_limit;
	short rcv_upper_limit;
	unsigned char eeprom_reg;
	unsigned short word[8];
};

/* The station (ethernet) address prefix, used for IDing the board. */
#define SA_ADDR0 0x00	/* Etherexpress Pro/10 */
#define SA_ADDR1 0xaa
#define SA_ADDR2 0x00

#define GetBit(x,y) ((x & (1<<y))>>y)

/* EEPROM Word 0: */
#define ee_PnP       0  /* Plug 'n Play enable bit */
#define ee_Word1     1  /* Word 1? */
#define ee_BusWidth  2  /* 8/16 bit */
#define ee_FlashAddr 3  /* Flash Address */
#define ee_FlashMask 0x7   /* Mask */
#define ee_AutoIO    6  /* */
#define ee_reserved0 7  /* =0! */
#define ee_Flash     8  /* Flash there? */
#define ee_AutoNeg   9  /* Auto Negotiation enabled? */
#define ee_IO0       10 /* IO Address LSB */
#define ee_IO0Mask   0x /*...*/
#define ee_IO1       15 /* IO MSB */

/* EEPROM Word 1: */
#define ee_IntSel    0   /* Interrupt */
#define ee_IntMask   0x7
#define ee_LI        3   /* Link Integrity 0= enabled */
#define ee_PC        4   /* Polarity Correction 0= enabled */
#define ee_TPE_AUI   5   /* PortSelection 1=TPE */
#define ee_Jabber    6   /* Jabber prevention 0= enabled */
#define ee_AutoPort  7   /* Auto Port Selection 1= Disabled */
#define ee_SMOUT     8   /* SMout Pin Control 0= Input */
#define ee_PROM      9   /* Flash EPROM / PROM 0=Flash */
#define ee_reserved1 10  /* .. 12 =0! */
#define ee_AltReady  13  /* Alternate Ready, 0=normal */
#define ee_reserved2 14  /* =0! */
#define ee_Duplex    15

/* Word2,3,4: */
#define ee_IA5       0 /*bit start for individual Addr Byte 5 */
#define ee_IA4       8 /*bit start for individual Addr Byte 5 */
#define ee_IA3       0 /*bit start for individual Addr Byte 5 */
#define ee_IA2       8 /*bit start for individual Addr Byte 5 */
#define ee_IA1       0 /*bit start for individual Addr Byte 5 */
#define ee_IA0       8 /*bit start for individual Addr Byte 5 */

/* Word 5: */
#define ee_BNC_TPE   0 /* 0=TPE */
#define ee_BootType  1 /* 00=None, 01=IPX, 10=ODI, 11=NDIS */
#define ee_BootTypeMask 0x3
#define ee_NumConn   3  /* Number of Connections 0= One or Two */
#define ee_FlashSock 4  /* Presence of Flash Socket 0= Present */
#define ee_PortTPE   5
#define ee_PortBNC   6
#define ee_PortAUI   7
#define ee_PowerMgt  10 /* 0= disabled */
#define ee_CP        13 /* Concurrent Processing */
#define ee_CPMask    0x7

/* Word 6: */
#define ee_Stepping  0 /* Stepping info */
#define ee_StepMask  0x0F
#define ee_BoardID   4 /* Manucaturer Board ID, reserved */
#define ee_BoardMask 0x0FFF

/* Word 7: */
#define ee_INT_TO_IRQ 0 /* int to IRQ Mapping  = 0x1EB8 for Pro/10+ */
#define ee_FX_INT2IRQ 0x1EB8 /* the _only_ mapping allowed for FX chips */

/*..*/
#define ee_SIZE 0x40 /* total EEprom Size */
#define ee_Checksum 0xBABA /* initial and final value for adding checksum */


/* Card identification via EEprom:   */
#define ee_addr_vendor 0x10  /* Word offset for EISA Vendor ID */
#define ee_addr_id 0x11      /* Word offset for Card ID */
#define ee_addr_SN 0x12      /* Serial Number */
#define ee_addr_CRC_8 0x14   /* CRC over last thee Bytes */


#define ee_vendor_intel0 0x25  /* Vendor ID Intel */
#define ee_vendor_intel1 0xD4
#define ee_id_eepro10p0 0x10   /* ID for eepro/10+ */
#define ee_id_eepro10p1 0x31

#define TX_TIMEOUT ((4*HZ)/10)

/* Index to functions, as function prototypes. */

static int	eepro_probe1(struct net_device *dev, int autoprobe);
static int	eepro_open(struct net_device *dev);
static netdev_tx_t eepro_send_packet(struct sk_buff *skb,
				     struct net_device *dev);
static irqreturn_t eepro_interrupt(int irq, void *dev_id);
static void 	eepro_rx(struct net_device *dev);
static void 	eepro_transmit_interrupt(struct net_device *dev);
static int	eepro_close(struct net_device *dev);
static void     set_multicast_list(struct net_device *dev);
static void     eepro_tx_timeout (struct net_device *dev);

static int read_eeprom(int ioaddr, int location, struct net_device *dev);
static int	hardware_send_packet(struct net_device *dev, void *buf, short length);
static int	eepro_grab_irq(struct net_device *dev);

/*
			Details of the i82595.

You will need either the datasheet or the user manual to understand what
is going on here.  The 82595 is very different from the 82586, 82593.

The receive algorithm in eepro_rx() is just an implementation of the
RCV ring structure that the Intel 82595 imposes at the hardware level.
The receive buffer is set at 24K, and the transmit buffer is 8K.  I
am assuming that the total buffer memory is 32K, which is true for the
Intel EtherExpress Pro/10.  If it is less than that on a generic card,
the driver will be broken.

The transmit algorithm in the hardware_send_packet() is similar to the
one in the eepro_rx().  The transmit buffer is a ring linked list.
I just queue the next available packet to the end of the list.  In my
system, the 82595 is so fast that the list seems to always contain a
single packet.  In other systems with faster computers and more congested
network traffics, the ring linked list should improve performance by
allowing up to 8K worth of packets to be queued.

The sizes of the receive and transmit buffers can now be changed via lilo
or insmod.  Lilo uses the appended line "ether=io,irq,debug,rx-buffer,eth0"
where rx-buffer is in KB unit.  Modules uses the parameter mem which is
also in KB unit, for example "insmod io=io-address irq=0 mem=rx-buffer."
The receive buffer has to be more than 3K or less than 29K.  Otherwise,
it is reset to the default of 24K, and, hence, 8K for the trasnmit
buffer (transmit-buffer = 32K - receive-buffer).

*/
#define RAM_SIZE        0x8000

#define RCV_HEADER      8
#define RCV_DEFAULT_RAM 0x6000

#define XMT_HEADER      8
#define XMT_DEFAULT_RAM	(RAM_SIZE - RCV_DEFAULT_RAM)

#define XMT_START_PRO	RCV_DEFAULT_RAM
#define XMT_START_10	0x0000
#define RCV_START_PRO	0x0000
#define RCV_START_10	XMT_DEFAULT_RAM

#define	RCV_DONE	0x0008
#define	RX_OK		0x2000
#define	RX_ERROR	0x0d81

#define	TX_DONE_BIT	0x0080
#define	TX_OK		0x2000
#define	CHAIN_BIT	0x8000
#define	XMT_STATUS	0x02
#define	XMT_CHAIN	0x04
#define	XMT_COUNT	0x06

#define	BANK0_SELECT	0x00
#define	BANK1_SELECT	0x40
#define	BANK2_SELECT	0x80

/* Bank 0 registers */
#define	COMMAND_REG	0x00	/* Register 0 */
#define	MC_SETUP	0x03
#define	XMT_CMD		0x04
#define	DIAGNOSE_CMD	0x07
#define	RCV_ENABLE_CMD	0x08
#define	RCV_DISABLE_CMD	0x0a
#define	STOP_RCV_CMD	0x0b
#define	RESET_CMD	0x0e
#define	POWER_DOWN_CMD	0x18
#define	RESUME_XMT_CMD	0x1c
#define	SEL_RESET_CMD	0x1e
#define	STATUS_REG	0x01	/* Register 1 */
#define	RX_INT		0x02
#define	TX_INT		0x04
#define	EXEC_STATUS	0x30
#define	ID_REG		0x02	/* Register 2	*/
#define	R_ROBIN_BITS	0xc0	/* round robin counter */
#define	ID_REG_MASK	0x2c
#define	ID_REG_SIG	0x24
#define	AUTO_ENABLE	0x10
#define	INT_MASK_REG	0x03	/* Register 3	*/
#define	RX_STOP_MASK	0x01
#define	RX_MASK		0x02
#define	TX_MASK		0x04
#define	EXEC_MASK	0x08
#define	ALL_MASK	0x0f
#define	IO_32_BIT	0x10
#define	RCV_BAR		0x04	/* The following are word (16-bit) registers */
#define	RCV_STOP	0x06

#define	XMT_BAR_PRO	0x0a
#define	XMT_BAR_10	0x0b

#define	HOST_ADDRESS_REG	0x0c
#define	IO_PORT		0x0e
#define	IO_PORT_32_BIT	0x0c

/* Bank 1 registers */
#define	REG1	0x01
#define	WORD_WIDTH	0x02
#define	INT_ENABLE	0x80
#define INT_NO_REG	0x02
#define	RCV_LOWER_LIMIT_REG	0x08
#define	RCV_UPPER_LIMIT_REG	0x09

#define	XMT_LOWER_LIMIT_REG_PRO 0x0a
#define	XMT_UPPER_LIMIT_REG_PRO 0x0b
#define	XMT_LOWER_LIMIT_REG_10  0x0b
#define	XMT_UPPER_LIMIT_REG_10  0x0a

/* Bank 2 registers */
#define	XMT_Chain_Int	0x20	/* Interrupt at the end of the transmit chain */
#define	XMT_Chain_ErrStop	0x40 /* Interrupt at the end of the chain even if there are errors */
#define	RCV_Discard_BadFrame	0x80 /* Throw bad frames away, and continue to receive others */
#define	REG2		0x02
#define	PRMSC_Mode	0x01
#define	Multi_IA	0x20
#define	REG3		0x03
#define	TPE_BIT		0x04
#define	BNC_BIT		0x20
#define	REG13		0x0d
#define	FDX		0x00
#define	A_N_ENABLE	0x02

#define	I_ADD_REG0	0x04
#define	I_ADD_REG1	0x05
#define	I_ADD_REG2	0x06
#define	I_ADD_REG3	0x07
#define	I_ADD_REG4	0x08
#define	I_ADD_REG5	0x09

#define	EEPROM_REG_PRO 0x0a
#define	EEPROM_REG_10  0x0b

#define EESK 0x01
#define EECS 0x02
#define EEDI 0x04
#define EEDO 0x08

/* do a full reset */
#define eepro_reset(ioaddr) outb(RESET_CMD, ioaddr)

/* do a nice reset */
#define eepro_sel_reset(ioaddr) 	{ \
					outb(SEL_RESET_CMD, ioaddr); \
					SLOW_DOWN; \
					SLOW_DOWN; \
					}

/* disable all interrupts */
#define eepro_dis_int(ioaddr) outb(ALL_MASK, ioaddr + INT_MASK_REG)

/* clear all interrupts */
#define eepro_clear_int(ioaddr) outb(ALL_MASK, ioaddr + STATUS_REG)

/* enable tx/rx */
#define eepro_en_int(ioaddr) outb(ALL_MASK & ~(RX_MASK | TX_MASK), \
							ioaddr + INT_MASK_REG)

/* enable exec event interrupt */
#define eepro_en_intexec(ioaddr) outb(ALL_MASK & ~(EXEC_MASK), ioaddr + INT_MASK_REG)

/* enable rx */
#define eepro_en_rx(ioaddr) outb(RCV_ENABLE_CMD, ioaddr)

/* disable rx */
#define eepro_dis_rx(ioaddr) outb(RCV_DISABLE_CMD, ioaddr)

/* switch bank */
#define eepro_sw2bank0(ioaddr) outb(BANK0_SELECT, ioaddr)
#define eepro_sw2bank1(ioaddr) outb(BANK1_SELECT, ioaddr)
#define eepro_sw2bank2(ioaddr) outb(BANK2_SELECT, ioaddr)

/* enable interrupt line */
#define eepro_en_intline(ioaddr) outb(inb(ioaddr + REG1) | INT_ENABLE,\
				ioaddr + REG1)

/* disable interrupt line */
#define eepro_dis_intline(ioaddr) outb(inb(ioaddr + REG1) & 0x7f, \
				ioaddr + REG1);

/* set diagnose flag */
#define eepro_diag(ioaddr) outb(DIAGNOSE_CMD, ioaddr)

/* ack for rx int */
#define eepro_ack_rx(ioaddr) outb (RX_INT, ioaddr + STATUS_REG)

/* ack for tx int */
#define eepro_ack_tx(ioaddr) outb (TX_INT, ioaddr + STATUS_REG)

/* a complete sel reset */
#define eepro_complete_selreset(ioaddr) { \
						dev->stats.tx_errors++;\
						eepro_sel_reset(ioaddr);\
						lp->tx_end = \
							lp->xmt_lower_limit;\
						lp->tx_start = lp->tx_end;\
						lp->tx_last = 0;\
						dev->trans_start = jiffies;\
						netif_wake_queue(dev);\
						eepro_en_rx(ioaddr);\
					}

/* Check for a network adaptor of this type, and return '0' if one exists.
   If dev->base_addr == 0, probe all likely locations.
   If dev->base_addr == 1, always return failure.
   If dev->base_addr == 2, allocate space for the device and return success
   (detachable devices only).
   */
static int __init do_eepro_probe(struct net_device *dev)
{
	int i;
	int base_addr = dev->base_addr;
	int irq = dev->irq;

#ifdef PnPWakeup
	/* XXXX for multiple cards should this only be run once? */

	/* Wakeup: */
	#define WakeupPort 0x279
	#define WakeupSeq    {0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,\
	                      0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,\
	                      0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,\
	                      0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x43}

	{
		unsigned short int WS[32]=WakeupSeq;

		if (request_region(WakeupPort, 2, "eepro wakeup")) {
			if (net_debug>5)
				printk(KERN_DEBUG "Waking UP\n");

			outb_p(0,WakeupPort);
			outb_p(0,WakeupPort);
			for (i=0; i<32; i++) {
				outb_p(WS[i],WakeupPort);
				if (net_debug>5) printk(KERN_DEBUG ": %#x ",WS[i]);
			}

			release_region(WakeupPort, 2);
		} else
			printk(KERN_WARNING "PnP wakeup region busy!\n");
	}
#endif

	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return eepro_probe1(dev, 0);

	else if (base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;

	for (i = 0; eepro_portlist[i]; i++) {
		dev->base_addr = eepro_portlist[i];
		dev->irq = irq;
		if (eepro_probe1(dev, 1) == 0)
			return 0;
	}

	return -ENODEV;
}

#ifndef MODULE
struct net_device * __init eepro_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct eepro_local));
	int err;

	if (!dev)
		return ERR_PTR(-ENODEV);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_eepro_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static void __init printEEPROMInfo(struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	unsigned short Word;
	int i,j;

	j = ee_Checksum;
	for (i = 0; i < 8; i++)
		j += lp->word[i];
	for ( ; i < ee_SIZE; i++)
		j += read_eeprom(ioaddr, i, dev);

	printk(KERN_DEBUG "Checksum: %#x\n",j&0xffff);

	Word = lp->word[0];
	printk(KERN_DEBUG "Word0:\n");
	printk(KERN_DEBUG " Plug 'n Pray: %d\n",GetBit(Word,ee_PnP));
	printk(KERN_DEBUG " Buswidth: %d\n",(GetBit(Word,ee_BusWidth)+1)*8 );
	printk(KERN_DEBUG " AutoNegotiation: %d\n",GetBit(Word,ee_AutoNeg));
	printk(KERN_DEBUG " IO Address: %#x\n", (Word>>ee_IO0)<<4);

	if (net_debug>4)  {
		Word = lp->word[1];
		printk(KERN_DEBUG "Word1:\n");
		printk(KERN_DEBUG " INT: %d\n", Word & ee_IntMask);
		printk(KERN_DEBUG " LI: %d\n", GetBit(Word,ee_LI));
		printk(KERN_DEBUG " PC: %d\n", GetBit(Word,ee_PC));
		printk(KERN_DEBUG " TPE/AUI: %d\n", GetBit(Word,ee_TPE_AUI));
		printk(KERN_DEBUG " Jabber: %d\n", GetBit(Word,ee_Jabber));
		printk(KERN_DEBUG " AutoPort: %d\n", !GetBit(Word,ee_AutoPort));
		printk(KERN_DEBUG " Duplex: %d\n", GetBit(Word,ee_Duplex));
	}

	Word = lp->word[5];
	printk(KERN_DEBUG "Word5:\n");
	printk(KERN_DEBUG " BNC: %d\n",GetBit(Word,ee_BNC_TPE));
	printk(KERN_DEBUG " NumConnectors: %d\n",GetBit(Word,ee_NumConn));
	printk(KERN_DEBUG " Has ");
	if (GetBit(Word,ee_PortTPE)) printk(KERN_DEBUG "TPE ");
	if (GetBit(Word,ee_PortBNC)) printk(KERN_DEBUG "BNC ");
	if (GetBit(Word,ee_PortAUI)) printk(KERN_DEBUG "AUI ");
	printk(KERN_DEBUG "port(s)\n");

	Word = lp->word[6];
	printk(KERN_DEBUG "Word6:\n");
	printk(KERN_DEBUG " Stepping: %d\n",Word & ee_StepMask);
	printk(KERN_DEBUG " BoardID: %d\n",Word>>ee_BoardID);

	Word = lp->word[7];
	printk(KERN_DEBUG "Word7:\n");
	printk(KERN_DEBUG " INT to IRQ:\n");

	for (i=0, j=0; i<15; i++)
		if (GetBit(Word,i)) printk(KERN_DEBUG " INT%d -> IRQ %d;",j++,i);

	printk(KERN_DEBUG "\n");
}

/* function to recalculate the limits of buffer based on rcv_ram */
static void eepro_recalc (struct net_device *dev)
{
	struct eepro_local *	lp;

	lp = netdev_priv(dev);
	lp->xmt_ram = RAM_SIZE - lp->rcv_ram;

	if (lp->eepro == LAN595FX_10ISA) {
		lp->xmt_lower_limit = XMT_START_10;
		lp->xmt_upper_limit = (lp->xmt_ram - 2);
		lp->rcv_lower_limit = lp->xmt_ram;
		lp->rcv_upper_limit = (RAM_SIZE - 2);
	}
	else {
		lp->rcv_lower_limit = RCV_START_PRO;
		lp->rcv_upper_limit = (lp->rcv_ram - 2);
		lp->xmt_lower_limit = lp->rcv_ram;
		lp->xmt_upper_limit = (RAM_SIZE - 2);
	}
}

/* prints boot-time info */
static void __init eepro_print_info (struct net_device *dev)
{
	struct eepro_local *	lp = netdev_priv(dev);
	int			i;
	const char *		ifmap[] = {"AUI", "10Base2", "10BaseT"};

	i = inb(dev->base_addr + ID_REG);
	printk(KERN_DEBUG " id: %#x ",i);
	printk(" io: %#x ", (unsigned)dev->base_addr);

	switch (lp->eepro) {
		case LAN595FX_10ISA:
			printk("%s: Intel EtherExpress 10 ISA\n at %#x,",
					dev->name, (unsigned)dev->base_addr);
			break;
		case LAN595FX:
			printk("%s: Intel EtherExpress Pro/10+ ISA\n at %#x,",
					dev->name, (unsigned)dev->base_addr);
			break;
		case LAN595TX:
			printk("%s: Intel EtherExpress Pro/10 ISA at %#x,",
					dev->name, (unsigned)dev->base_addr);
			break;
		case LAN595:
			printk("%s: Intel 82595-based lan card at %#x,",
					dev->name, (unsigned)dev->base_addr);
			break;
	}

	printk(" %pM", dev->dev_addr);

	if (net_debug > 3)
		printk(KERN_DEBUG ", %dK RCV buffer",
				(int)(lp->rcv_ram)/1024);

	if (dev->irq > 2)
		printk(", IRQ %d, %s.\n", dev->irq, ifmap[dev->if_port]);
	else
		printk(", %s.\n", ifmap[dev->if_port]);

	if (net_debug > 3) {
		i = lp->word[5];
		if (i & 0x2000) /* bit 13 of EEPROM word 5 */
			printk(KERN_DEBUG "%s: Concurrent Processing is "
				"enabled but not used!\n", dev->name);
	}

	/* Check the station address for the manufacturer's code */
	if (net_debug>3)
		printEEPROMInfo(dev);
}

static const struct ethtool_ops eepro_ethtool_ops;

static const struct net_device_ops eepro_netdev_ops = {
 	.ndo_open               = eepro_open,
 	.ndo_stop               = eepro_close,
 	.ndo_start_xmit    	= eepro_send_packet,
	.ndo_set_rx_mode	= set_multicast_list,
 	.ndo_tx_timeout		= eepro_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/* This is the real probe routine.  Linux has a history of friendly device
   probes on the ISA bus.  A good device probe avoids doing writes, and
   verifies that the correct device exists and functions.  */

static int __init eepro_probe1(struct net_device *dev, int autoprobe)
{
	unsigned short station_addr[3], id, counter;
	int i;
	struct eepro_local *lp;
	int ioaddr = dev->base_addr;
	int err;

	/* Grab the region so we can find another board if autoIRQ fails. */
	if (!request_region(ioaddr, EEPRO_IO_EXTENT, DRV_NAME)) {
		if (!autoprobe)
			printk(KERN_WARNING "EEPRO: io-port 0x%04x in use\n",
				ioaddr);
		return -EBUSY;
	}

	/* Now, we are going to check for the signature of the
	   ID_REG (register 2 of bank 0) */

	id = inb(ioaddr + ID_REG);

	if ((id & ID_REG_MASK) != ID_REG_SIG)
		goto exit;

	/* We seem to have the 82595 signature, let's
	   play with its counter (last 2 bits of
	   register 2 of bank 0) to be sure. */

	counter = id & R_ROBIN_BITS;

	if ((inb(ioaddr + ID_REG) & R_ROBIN_BITS) != (counter + 0x40))
		goto exit;

	lp = netdev_priv(dev);
	memset(lp, 0, sizeof(struct eepro_local));
	lp->xmt_bar = XMT_BAR_PRO;
	lp->xmt_lower_limit_reg = XMT_LOWER_LIMIT_REG_PRO;
	lp->xmt_upper_limit_reg = XMT_UPPER_LIMIT_REG_PRO;
	lp->eeprom_reg = EEPROM_REG_PRO;
	spin_lock_init(&lp->lock);

	/* Now, get the ethernet hardware address from
	   the EEPROM */
	station_addr[0] = read_eeprom(ioaddr, 2, dev);

	/* FIXME - find another way to know that we've found
	 * an Etherexpress 10
	 */
	if (station_addr[0] == 0x0000 || station_addr[0] == 0xffff) {
		lp->eepro = LAN595FX_10ISA;
		lp->eeprom_reg = EEPROM_REG_10;
		lp->xmt_lower_limit_reg = XMT_LOWER_LIMIT_REG_10;
		lp->xmt_upper_limit_reg = XMT_UPPER_LIMIT_REG_10;
		lp->xmt_bar = XMT_BAR_10;
		station_addr[0] = read_eeprom(ioaddr, 2, dev);
	}

	/* get all words at once. will be used here and for ethtool */
	for (i = 0; i < 8; i++) {
		lp->word[i] = read_eeprom(ioaddr, i, dev);
	}
	station_addr[1] = lp->word[3];
	station_addr[2] = lp->word[4];

	if (!lp->eepro) {
		if (lp->word[7] == ee_FX_INT2IRQ)
			lp->eepro = 2;
		else if (station_addr[2] == SA_ADDR1)
			lp->eepro = 1;
	}

	/* Fill in the 'dev' fields. */
	for (i=0; i < 6; i++)
		dev->dev_addr[i] = ((unsigned char *) station_addr)[5-i];

	/* RX buffer must be more than 3K and less than 29K */
	if (dev->mem_end < 3072 || dev->mem_end > 29696)
		lp->rcv_ram = RCV_DEFAULT_RAM;

	/* calculate {xmt,rcv}_{lower,upper}_limit */
	eepro_recalc(dev);

	if (GetBit(lp->word[5], ee_BNC_TPE))
		dev->if_port = BNC;
	else
		dev->if_port = TPE;

 	if (dev->irq < 2 && lp->eepro != 0) {
 		/* Mask off INT number */
 		int count = lp->word[1] & 7;
 		unsigned irqMask = lp->word[7];

 		while (count--)
 			irqMask &= irqMask - 1;

 		count = ffs(irqMask);

 		if (count)
 			dev->irq = count - 1;

 		if (dev->irq < 2) {
 			printk(KERN_ERR " Duh! illegal interrupt vector stored in EEPROM.\n");
 			goto exit;
 		} else if (dev->irq == 2) {
 			dev->irq = 9;
 		}
 	}

	dev->netdev_ops		= &eepro_netdev_ops;
 	dev->watchdog_timeo	= TX_TIMEOUT;
	dev->ethtool_ops	= &eepro_ethtool_ops;

	/* print boot time info */
	eepro_print_info(dev);

	/* reset 82595 */
	eepro_reset(ioaddr);

	err = register_netdev(dev);
	if (err)
		goto err;
	return 0;
exit:
	err = -ENODEV;
err:
 	release_region(dev->base_addr, EEPRO_IO_EXTENT);
 	return err;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
   */

static const char irqrmap[] = {-1,-1,0,1,-1,2,-1,-1,-1,0,3,4,-1,-1,-1,-1};
static const char irqrmap2[] = {-1,-1,4,0,1,2,-1,3,-1,4,5,6,7,-1,-1,-1};
static int	eepro_grab_irq(struct net_device *dev)
{
	static const int irqlist[] = { 3, 4, 5, 7, 9, 10, 11, 12, 0 };
	const int *irqp = irqlist;
	int temp_reg, ioaddr = dev->base_addr;

	eepro_sw2bank1(ioaddr); /* be CAREFUL, BANK 1 now */

	/* Enable the interrupt line. */
	eepro_en_intline(ioaddr);

	/* be CAREFUL, BANK 0 now */
	eepro_sw2bank0(ioaddr);

	/* clear all interrupts */
	eepro_clear_int(ioaddr);

	/* Let EXEC event to interrupt */
	eepro_en_intexec(ioaddr);

	do {
		eepro_sw2bank1(ioaddr); /* be CAREFUL, BANK 1 now */

		temp_reg = inb(ioaddr + INT_NO_REG);
		outb((temp_reg & 0xf8) | irqrmap[*irqp], ioaddr + INT_NO_REG);

		eepro_sw2bank0(ioaddr); /* Switch back to Bank 0 */

		if (request_irq (*irqp, NULL, IRQF_SHARED, "bogus", dev) != EBUSY) {
			unsigned long irq_mask;
			/* Twinkle the interrupt, and check if it's seen */
			irq_mask = probe_irq_on();

			eepro_diag(ioaddr); /* RESET the 82595 */
			mdelay(20);

			if (*irqp == probe_irq_off(irq_mask))  /* It's a good IRQ line */
				break;

			/* clear all interrupts */
			eepro_clear_int(ioaddr);
		}
	} while (*++irqp);

	eepro_sw2bank1(ioaddr); /* Switch back to Bank 1 */

	/* Disable the physical interrupt line. */
	eepro_dis_intline(ioaddr);

	eepro_sw2bank0(ioaddr); /* Switch back to Bank 0 */

	/* Mask all the interrupts. */
	eepro_dis_int(ioaddr);

	/* clear all interrupts */
	eepro_clear_int(ioaddr);

	return dev->irq;
}

static int eepro_open(struct net_device *dev)
{
	unsigned short temp_reg, old8, old9;
	int irqMask;
	int i, ioaddr = dev->base_addr;
	struct eepro_local *lp = netdev_priv(dev);

	if (net_debug > 3)
		printk(KERN_DEBUG "%s: entering eepro_open routine.\n", dev->name);

	irqMask = lp->word[7];

	if (lp->eepro == LAN595FX_10ISA) {
		if (net_debug > 3) printk(KERN_DEBUG "p->eepro = 3;\n");
	}
	else if (irqMask == ee_FX_INT2IRQ) /* INT to IRQ Mask */
		{
			lp->eepro = 2; /* Yes, an Intel EtherExpress Pro/10+ */
			if (net_debug > 3) printk(KERN_DEBUG "p->eepro = 2;\n");
		}

	else if ((dev->dev_addr[0] == SA_ADDR0 &&
			dev->dev_addr[1] == SA_ADDR1 &&
			dev->dev_addr[2] == SA_ADDR2))
		{
			lp->eepro = 1;
			if (net_debug > 3) printk(KERN_DEBUG "p->eepro = 1;\n");
		}  /* Yes, an Intel EtherExpress Pro/10 */

	else lp->eepro = 0; /* No, it is a generic 82585 lan card */

	/* Get the interrupt vector for the 82595 */
	if (dev->irq < 2 && eepro_grab_irq(dev) == 0) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n", dev->name, dev->irq);
		return -EAGAIN;
	}

	if (request_irq(dev->irq , eepro_interrupt, 0, dev->name, dev)) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n", dev->name, dev->irq);
		return -EAGAIN;
	}

	/* Initialize the 82595. */

	eepro_sw2bank2(ioaddr); /* be CAREFUL, BANK 2 now */
	temp_reg = inb(ioaddr + lp->eeprom_reg);

	lp->stepping = temp_reg >> 5;	/* Get the stepping number of the 595 */

	if (net_debug > 3)
		printk(KERN_DEBUG "The stepping of the 82595 is %d\n", lp->stepping);

	if (temp_reg & 0x10) /* Check the TurnOff Enable bit */
		outb(temp_reg & 0xef, ioaddr + lp->eeprom_reg);
	for (i=0; i < 6; i++)
		outb(dev->dev_addr[i] , ioaddr + I_ADD_REG0 + i);

	temp_reg = inb(ioaddr + REG1);    /* Setup Transmit Chaining */
	outb(temp_reg | XMT_Chain_Int | XMT_Chain_ErrStop /* and discard bad RCV frames */
		| RCV_Discard_BadFrame, ioaddr + REG1);

	temp_reg = inb(ioaddr + REG2); /* Match broadcast */
	outb(temp_reg | 0x14, ioaddr + REG2);

	temp_reg = inb(ioaddr + REG3);
	outb(temp_reg & 0x3f, ioaddr + REG3); /* clear test mode */

	/* Set the receiving mode */
	eepro_sw2bank1(ioaddr); /* be CAREFUL, BANK 1 now */

	/* Set the interrupt vector */
	temp_reg = inb(ioaddr + INT_NO_REG);
	if (lp->eepro == LAN595FX || lp->eepro == LAN595FX_10ISA)
		outb((temp_reg & 0xf8) | irqrmap2[dev->irq], ioaddr + INT_NO_REG);
	else outb((temp_reg & 0xf8) | irqrmap[dev->irq], ioaddr + INT_NO_REG);


	temp_reg = inb(ioaddr + INT_NO_REG);
	if (lp->eepro == LAN595FX || lp->eepro == LAN595FX_10ISA)
		outb((temp_reg & 0xf0) | irqrmap2[dev->irq] | 0x08,ioaddr+INT_NO_REG);
	else outb((temp_reg & 0xf8) | irqrmap[dev->irq], ioaddr + INT_NO_REG);

	if (net_debug > 3)
		printk(KERN_DEBUG "eepro_open: content of INT Reg is %x\n", temp_reg);


	/* Initialize the RCV and XMT upper and lower limits */
	outb(lp->rcv_lower_limit >> 8, ioaddr + RCV_LOWER_LIMIT_REG);
	outb(lp->rcv_upper_limit >> 8, ioaddr + RCV_UPPER_LIMIT_REG);
	outb(lp->xmt_lower_limit >> 8, ioaddr + lp->xmt_lower_limit_reg);
	outb(lp->xmt_upper_limit >> 8, ioaddr + lp->xmt_upper_limit_reg);

	/* Enable the interrupt line. */
	eepro_en_intline(ioaddr);

	/* Switch back to Bank 0 */
	eepro_sw2bank0(ioaddr);

	/* Let RX and TX events to interrupt */
	eepro_en_int(ioaddr);

	/* clear all interrupts */
	eepro_clear_int(ioaddr);

	/* Initialize RCV */
	outw(lp->rcv_lower_limit, ioaddr + RCV_BAR);
	lp->rx_start = lp->rcv_lower_limit;
	outw(lp->rcv_upper_limit | 0xfe, ioaddr + RCV_STOP);

	/* Initialize XMT */
	outw(lp->xmt_lower_limit, ioaddr + lp->xmt_bar);
	lp->tx_start = lp->tx_end = lp->xmt_lower_limit;
	lp->tx_last = 0;

	/* Check for the i82595TX and i82595FX */
	old8 = inb(ioaddr + 8);
	outb(~old8, ioaddr + 8);

	if ((temp_reg = inb(ioaddr + 8)) == old8) {
		if (net_debug > 3)
			printk(KERN_DEBUG "i82595 detected!\n");
		lp->version = LAN595;
	}
	else {
		lp->version = LAN595TX;
		outb(old8, ioaddr + 8);
		old9 = inb(ioaddr + 9);

		if (irqMask==ee_FX_INT2IRQ) {
			if (net_debug > 3) {
				printk(KERN_DEBUG "IrqMask: %#x\n",irqMask);
				printk(KERN_DEBUG "i82595FX detected!\n");
			}
			lp->version = LAN595FX;
			outb(old9, ioaddr + 9);
			if (dev->if_port != TPE) {	/* Hopefully, this will fix the
							problem of using Pentiums and
							pro/10 w/ BNC. */
				eepro_sw2bank2(ioaddr); /* be CAREFUL, BANK 2 now */
				temp_reg = inb(ioaddr + REG13);
				/* disable the full duplex mode since it is not
				applicable with the 10Base2 cable. */
				outb(temp_reg & ~(FDX | A_N_ENABLE), REG13);
				eepro_sw2bank0(ioaddr); /* be CAREFUL, BANK 0 now */
			}
		}
		else if (net_debug > 3) {
			printk(KERN_DEBUG "temp_reg: %#x  ~old9: %#x\n",temp_reg,((~old9)&0xff));
			printk(KERN_DEBUG "i82595TX detected!\n");
		}
	}

	eepro_sel_reset(ioaddr);

	netif_start_queue(dev);

	if (net_debug > 3)
		printk(KERN_DEBUG "%s: exiting eepro_open routine.\n", dev->name);

	/* enabling rx */
	eepro_en_rx(ioaddr);

	return 0;
}

static void eepro_tx_timeout (struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	/* if (net_debug > 1) */
	printk (KERN_ERR "%s: transmit timed out, %s?\n", dev->name,
		"network cable problem");
	/* This is not a duplicate. One message for the console,
	   one for the log file  */
	printk (KERN_DEBUG "%s: transmit timed out, %s?\n", dev->name,
		"network cable problem");
	eepro_complete_selreset(ioaddr);
}


static netdev_tx_t eepro_send_packet(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	unsigned long flags;
	int ioaddr = dev->base_addr;
	short length = skb->len;

	if (net_debug > 5)
		printk(KERN_DEBUG  "%s: entering eepro_send_packet routine.\n", dev->name);

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;
		length = ETH_ZLEN;
	}
	netif_stop_queue (dev);

	eepro_dis_int(ioaddr);
	spin_lock_irqsave(&lp->lock, flags);

	{
		unsigned char *buf = skb->data;

		if (hardware_send_packet(dev, buf, length))
			/* we won't wake queue here because we're out of space */
			dev->stats.tx_dropped++;
		else {
			dev->stats.tx_bytes+=skb->len;
			netif_wake_queue(dev);
		}

	}

	dev_kfree_skb (skb);

	/* You might need to clean up and record Tx statistics here. */
	/* dev->stats.tx_aborted_errors++; */

	if (net_debug > 5)
		printk(KERN_DEBUG "%s: exiting eepro_send_packet routine.\n", dev->name);

	eepro_en_int(ioaddr);
	spin_unlock_irqrestore(&lp->lock, flags);

	return NETDEV_TX_OK;
}


/*	The typical workload of the driver:
	Handle the network interface interrupts. */

static irqreturn_t
eepro_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct eepro_local *lp;
	int ioaddr, status, boguscount = 20;
	int handled = 0;

	lp = netdev_priv(dev);

        spin_lock(&lp->lock);

	if (net_debug > 5)
		printk(KERN_DEBUG "%s: entering eepro_interrupt routine.\n", dev->name);

	ioaddr = dev->base_addr;

	while (((status = inb(ioaddr + STATUS_REG)) & (RX_INT|TX_INT)) && (boguscount--))
	{
		handled = 1;
		if (status & RX_INT) {
			if (net_debug > 4)
				printk(KERN_DEBUG "%s: packet received interrupt.\n", dev->name);

			eepro_dis_int(ioaddr);

			/* Get the received packets */
			eepro_ack_rx(ioaddr);
			eepro_rx(dev);

			eepro_en_int(ioaddr);
		}
		if (status & TX_INT) {
			if (net_debug > 4)
 				printk(KERN_DEBUG "%s: packet transmit interrupt.\n", dev->name);


			eepro_dis_int(ioaddr);

			/* Process the status of transmitted packets */
			eepro_ack_tx(ioaddr);
			eepro_transmit_interrupt(dev);

			eepro_en_int(ioaddr);
		}
	}

	if (net_debug > 5)
		printk(KERN_DEBUG "%s: exiting eepro_interrupt routine.\n", dev->name);

	spin_unlock(&lp->lock);
	return IRQ_RETVAL(handled);
}

static int eepro_close(struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	short temp_reg;

	netif_stop_queue(dev);

	eepro_sw2bank1(ioaddr); /* Switch back to Bank 1 */

	/* Disable the physical interrupt line. */
	temp_reg = inb(ioaddr + REG1);
	outb(temp_reg & 0x7f, ioaddr + REG1);

	eepro_sw2bank0(ioaddr); /* Switch back to Bank 0 */

	/* Flush the Tx and disable Rx. */
	outb(STOP_RCV_CMD, ioaddr);
	lp->tx_start = lp->tx_end = lp->xmt_lower_limit;
	lp->tx_last = 0;

	/* Mask all the interrupts. */
	eepro_dis_int(ioaddr);

	/* clear all interrupts */
	eepro_clear_int(ioaddr);

	/* Reset the 82595 */
	eepro_reset(ioaddr);

	/* release the interrupt */
	free_irq(dev->irq, dev);

	/* Update the statistics here. What statistics? */

	return 0;
}

/* Set or clear the multicast filter for this adaptor.
 */
static void
set_multicast_list(struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;
	unsigned short mode;
	struct netdev_hw_addr *ha;
	int mc_count = netdev_mc_count(dev);

	if (dev->flags&(IFF_ALLMULTI|IFF_PROMISC) || mc_count > 63)
	{
		eepro_sw2bank2(ioaddr); /* be CAREFUL, BANK 2 now */
		mode = inb(ioaddr + REG2);
		outb(mode | PRMSC_Mode, ioaddr + REG2);
		mode = inb(ioaddr + REG3);
		outb(mode, ioaddr + REG3); /* writing reg. 3 to complete the update */
		eepro_sw2bank0(ioaddr); /* Return to BANK 0 now */
	}

	else if (mc_count == 0)
	{
		eepro_sw2bank2(ioaddr); /* be CAREFUL, BANK 2 now */
		mode = inb(ioaddr + REG2);
		outb(mode & 0xd6, ioaddr + REG2); /* Turn off Multi-IA and PRMSC_Mode bits */
		mode = inb(ioaddr + REG3);
		outb(mode, ioaddr + REG3); /* writing reg. 3 to complete the update */
		eepro_sw2bank0(ioaddr); /* Return to BANK 0 now */
	}

	else
	{
		unsigned short status, *eaddrs;
		int i, boguscount = 0;

		/* Disable RX and TX interrupts.  Necessary to avoid
		   corruption of the HOST_ADDRESS_REG by interrupt
		   service routines. */
		eepro_dis_int(ioaddr);

		eepro_sw2bank2(ioaddr); /* be CAREFUL, BANK 2 now */
		mode = inb(ioaddr + REG2);
		outb(mode | Multi_IA, ioaddr + REG2);
		mode = inb(ioaddr + REG3);
		outb(mode, ioaddr + REG3); /* writing reg. 3 to complete the update */
		eepro_sw2bank0(ioaddr); /* Return to BANK 0 now */
		outw(lp->tx_end, ioaddr + HOST_ADDRESS_REG);
		outw(MC_SETUP, ioaddr + IO_PORT);
		outw(0, ioaddr + IO_PORT);
		outw(0, ioaddr + IO_PORT);
		outw(6 * (mc_count + 1), ioaddr + IO_PORT);

		netdev_for_each_mc_addr(ha, dev) {
			eaddrs = (unsigned short *) ha->addr;
			outw(*eaddrs++, ioaddr + IO_PORT);
			outw(*eaddrs++, ioaddr + IO_PORT);
			outw(*eaddrs++, ioaddr + IO_PORT);
		}

		eaddrs = (unsigned short *) dev->dev_addr;
		outw(eaddrs[0], ioaddr + IO_PORT);
		outw(eaddrs[1], ioaddr + IO_PORT);
		outw(eaddrs[2], ioaddr + IO_PORT);
		outw(lp->tx_end, ioaddr + lp->xmt_bar);
		outb(MC_SETUP, ioaddr);

		/* Update the transmit queue */
		i = lp->tx_end + XMT_HEADER + 6 * (mc_count + 1);

		if (lp->tx_start != lp->tx_end)
		{
			/* update the next address and the chain bit in the
			   last packet */
			outw(lp->tx_last + XMT_CHAIN, ioaddr + HOST_ADDRESS_REG);
			outw(i, ioaddr + IO_PORT);
			outw(lp->tx_last + XMT_COUNT, ioaddr + HOST_ADDRESS_REG);
			status = inw(ioaddr + IO_PORT);
			outw(status | CHAIN_BIT, ioaddr + IO_PORT);
			lp->tx_end = i ;
		}
		else {
			lp->tx_start = lp->tx_end = i ;
		}

		/* Acknowledge that the MC setup is done */
		do { /* We should be doing this in the eepro_interrupt()! */
			SLOW_DOWN;
			SLOW_DOWN;
			if (inb(ioaddr + STATUS_REG) & 0x08)
			{
				i = inb(ioaddr);
				outb(0x08, ioaddr + STATUS_REG);

				if (i & 0x20) { /* command ABORTed */
					printk(KERN_NOTICE "%s: multicast setup failed.\n",
						dev->name);
					break;
				} else if ((i & 0x0f) == 0x03)	{ /* MC-Done */
					printk(KERN_DEBUG "%s: set Rx mode to %d address%s.\n",
						dev->name, mc_count,
						mc_count > 1 ? "es":"");
					break;
				}
			}
		} while (++boguscount < 100);

		/* Re-enable RX and TX interrupts */
		eepro_en_int(ioaddr);
	}
	if (lp->eepro == LAN595FX_10ISA) {
		eepro_complete_selreset(ioaddr);
	}
	else
		eepro_en_rx(ioaddr);
}

/* The horrible routine to read a word from the serial EEPROM. */
/* IMPORTANT - the 82595 will be set to Bank 0 after the eeprom is read */

/* The delay between EEPROM clock transitions. */
#define eeprom_delay() { udelay(40); }
#define EE_READ_CMD (6 << 6)

static int
read_eeprom(int ioaddr, int location, struct net_device *dev)
{
	int i;
	unsigned short retval = 0;
	struct eepro_local *lp = netdev_priv(dev);
	short ee_addr = ioaddr + lp->eeprom_reg;
	int read_cmd = location | EE_READ_CMD;
	short ctrl_val = EECS ;

	/* XXXX - black magic */
		eepro_sw2bank1(ioaddr);
		outb(0x00, ioaddr + STATUS_REG);
	/* XXXX - black magic */

	eepro_sw2bank2(ioaddr);
	outb(ctrl_val, ee_addr);

	/* Shift the read command bits out. */
	for (i = 8; i >= 0; i--) {
		short outval = (read_cmd & (1 << i)) ? ctrl_val | EEDI
			: ctrl_val;
		outb(outval, ee_addr);
		outb(outval | EESK, ee_addr);	/* EEPROM clock tick. */
		eeprom_delay();
		outb(outval, ee_addr);	/* Finish EEPROM a clock tick. */
		eeprom_delay();
	}
	outb(ctrl_val, ee_addr);

	for (i = 16; i > 0; i--) {
		outb(ctrl_val | EESK, ee_addr);	 eeprom_delay();
		retval = (retval << 1) | ((inb(ee_addr) & EEDO) ? 1 : 0);
		outb(ctrl_val, ee_addr);  eeprom_delay();
	}

	/* Terminate the EEPROM access. */
	ctrl_val &= ~EECS;
	outb(ctrl_val | EESK, ee_addr);
	eeprom_delay();
	outb(ctrl_val, ee_addr);
	eeprom_delay();
	eepro_sw2bank0(ioaddr);
	return retval;
}

static int
hardware_send_packet(struct net_device *dev, void *buf, short length)
{
	struct eepro_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;
	unsigned status, tx_available, last, end;

	if (net_debug > 5)
		printk(KERN_DEBUG "%s: entering hardware_send_packet routine.\n", dev->name);

	/* determine how much of the transmit buffer space is available */
	if (lp->tx_end > lp->tx_start)
		tx_available = lp->xmt_ram - (lp->tx_end - lp->tx_start);
	else if (lp->tx_end < lp->tx_start)
		tx_available = lp->tx_start - lp->tx_end;
	else tx_available = lp->xmt_ram;

	if (((((length + 3) >> 1) << 1) + 2*XMT_HEADER) >= tx_available) {
		/* No space available ??? */
		return 1;
		}

		last = lp->tx_end;
		end = last + (((length + 3) >> 1) << 1) + XMT_HEADER;

	if (end >= lp->xmt_upper_limit + 2) { /* the transmit buffer is wrapped around */
		if ((lp->xmt_upper_limit + 2 - last) <= XMT_HEADER) {
				/* Arrrr!!!, must keep the xmt header together,
				several days were lost to chase this one down. */
			last = lp->xmt_lower_limit;
				end = last + (((length + 3) >> 1) << 1) + XMT_HEADER;
			}
		else end = lp->xmt_lower_limit + (end -
						lp->xmt_upper_limit + 2);
		}

		outw(last, ioaddr + HOST_ADDRESS_REG);
		outw(XMT_CMD, ioaddr + IO_PORT);
		outw(0, ioaddr + IO_PORT);
		outw(end, ioaddr + IO_PORT);
		outw(length, ioaddr + IO_PORT);

		if (lp->version == LAN595)
			outsw(ioaddr + IO_PORT, buf, (length + 3) >> 1);
		else {	/* LAN595TX or LAN595FX, capable of 32-bit I/O processing */
			unsigned short temp = inb(ioaddr + INT_MASK_REG);
			outb(temp | IO_32_BIT, ioaddr + INT_MASK_REG);
			outsl(ioaddr + IO_PORT_32_BIT, buf, (length + 3) >> 2);
			outb(temp & ~(IO_32_BIT), ioaddr + INT_MASK_REG);
		}

		/* A dummy read to flush the DRAM write pipeline */
		status = inw(ioaddr + IO_PORT);

		if (lp->tx_start == lp->tx_end) {
		outw(last, ioaddr + lp->xmt_bar);
			outb(XMT_CMD, ioaddr);
			lp->tx_start = last;   /* I don't like to change tx_start here */
		}
		else {
			/* update the next address and the chain bit in the
			last packet */

			if (lp->tx_end != last) {
				outw(lp->tx_last + XMT_CHAIN, ioaddr + HOST_ADDRESS_REG);
				outw(last, ioaddr + IO_PORT);
			}

			outw(lp->tx_last + XMT_COUNT, ioaddr + HOST_ADDRESS_REG);
			status = inw(ioaddr + IO_PORT);
			outw(status | CHAIN_BIT, ioaddr + IO_PORT);

			/* Continue the transmit command */
			outb(RESUME_XMT_CMD, ioaddr);
		}

		lp->tx_last = last;
		lp->tx_end = end;

		if (net_debug > 5)
			printk(KERN_DEBUG "%s: exiting hardware_send_packet routine.\n", dev->name);

	return 0;
}

static void
eepro_rx(struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;
	short boguscount = 20;
	short rcv_car = lp->rx_start;
	unsigned rcv_event, rcv_status, rcv_next_frame, rcv_size;

	if (net_debug > 5)
		printk(KERN_DEBUG "%s: entering eepro_rx routine.\n", dev->name);

	/* Set the read pointer to the start of the RCV */
	outw(rcv_car, ioaddr + HOST_ADDRESS_REG);

	rcv_event = inw(ioaddr + IO_PORT);

	while (rcv_event == RCV_DONE) {

		rcv_status = inw(ioaddr + IO_PORT);
		rcv_next_frame = inw(ioaddr + IO_PORT);
		rcv_size = inw(ioaddr + IO_PORT);

		if ((rcv_status & (RX_OK | RX_ERROR)) == RX_OK) {

			/* Malloc up new buffer. */
			struct sk_buff *skb;

			dev->stats.rx_bytes+=rcv_size;
			rcv_size &= 0x3fff;
			skb = dev_alloc_skb(rcv_size+5);
			if (skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
				dev->stats.rx_dropped++;
				rcv_car = lp->rx_start + RCV_HEADER + rcv_size;
				lp->rx_start = rcv_next_frame;
				outw(rcv_next_frame, ioaddr + HOST_ADDRESS_REG);

				break;
			}
			skb_reserve(skb,2);

			if (lp->version == LAN595)
				insw(ioaddr+IO_PORT, skb_put(skb,rcv_size), (rcv_size + 3) >> 1);
			else { /* LAN595TX or LAN595FX, capable of 32-bit I/O processing */
				unsigned short temp = inb(ioaddr + INT_MASK_REG);
				outb(temp | IO_32_BIT, ioaddr + INT_MASK_REG);
				insl(ioaddr+IO_PORT_32_BIT, skb_put(skb,rcv_size),
					(rcv_size + 3) >> 2);
				outb(temp & ~(IO_32_BIT), ioaddr + INT_MASK_REG);
			}

			skb->protocol = eth_type_trans(skb,dev);
			netif_rx(skb);
			dev->stats.rx_packets++;
		}

		else { /* Not sure will ever reach here,
			I set the 595 to discard bad received frames */
			dev->stats.rx_errors++;

			if (rcv_status & 0x0100)
				dev->stats.rx_over_errors++;

			else if (rcv_status & 0x0400)
				dev->stats.rx_frame_errors++;

			else if (rcv_status & 0x0800)
				dev->stats.rx_crc_errors++;

			printk(KERN_DEBUG "%s: event = %#x, status = %#x, next = %#x, size = %#x\n",
				dev->name, rcv_event, rcv_status, rcv_next_frame, rcv_size);
		}

		if (rcv_status & 0x1000)
			dev->stats.rx_length_errors++;

		rcv_car = lp->rx_start + RCV_HEADER + rcv_size;
		lp->rx_start = rcv_next_frame;

		if (--boguscount == 0)
			break;

		outw(rcv_next_frame, ioaddr + HOST_ADDRESS_REG);
		rcv_event = inw(ioaddr + IO_PORT);

	}
	if (rcv_car == 0)
		rcv_car = lp->rcv_upper_limit | 0xff;

	outw(rcv_car - 1, ioaddr + RCV_STOP);

	if (net_debug > 5)
		printk(KERN_DEBUG "%s: exiting eepro_rx routine.\n", dev->name);
}

static void
eepro_transmit_interrupt(struct net_device *dev)
{
	struct eepro_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;
	short boguscount = 25;
	short xmt_status;

	while ((lp->tx_start != lp->tx_end) && boguscount--) {

		outw(lp->tx_start, ioaddr + HOST_ADDRESS_REG);
		xmt_status = inw(ioaddr+IO_PORT);

		if (!(xmt_status & TX_DONE_BIT))
				break;

		xmt_status = inw(ioaddr+IO_PORT);
		lp->tx_start = inw(ioaddr+IO_PORT);

		netif_wake_queue (dev);

		if (xmt_status & TX_OK)
			dev->stats.tx_packets++;
		else {
			dev->stats.tx_errors++;
			if (xmt_status & 0x0400) {
				dev->stats.tx_carrier_errors++;
				printk(KERN_DEBUG "%s: carrier error\n",
					dev->name);
				printk(KERN_DEBUG "%s: XMT status = %#x\n",
					dev->name, xmt_status);
			}
			else {
				printk(KERN_DEBUG "%s: XMT status = %#x\n",
					dev->name, xmt_status);
				printk(KERN_DEBUG "%s: XMT status = %#x\n",
					dev->name, xmt_status);
			}
		}
		if (xmt_status & 0x000f) {
			dev->stats.collisions += (xmt_status & 0x000f);
		}

		if ((xmt_status & 0x0040) == 0x0) {
			dev->stats.tx_heartbeat_errors++;
		}
	}
}

static int eepro_ethtool_get_settings(struct net_device *dev,
					struct ethtool_cmd *cmd)
{
	struct eepro_local	*lp = netdev_priv(dev);

	cmd->supported = 	SUPPORTED_10baseT_Half |
				SUPPORTED_10baseT_Full |
				SUPPORTED_Autoneg;
	cmd->advertising =	ADVERTISED_10baseT_Half |
				ADVERTISED_10baseT_Full |
				ADVERTISED_Autoneg;

	if (GetBit(lp->word[5], ee_PortTPE)) {
		cmd->supported |= SUPPORTED_TP;
		cmd->advertising |= ADVERTISED_TP;
	}
	if (GetBit(lp->word[5], ee_PortBNC)) {
		cmd->supported |= SUPPORTED_BNC;
		cmd->advertising |= ADVERTISED_BNC;
	}
	if (GetBit(lp->word[5], ee_PortAUI)) {
		cmd->supported |= SUPPORTED_AUI;
		cmd->advertising |= ADVERTISED_AUI;
	}

	ethtool_cmd_speed_set(cmd, SPEED_10);

	if (dev->if_port == TPE && lp->word[1] & ee_Duplex) {
		cmd->duplex = DUPLEX_FULL;
	}
	else {
		cmd->duplex = DUPLEX_HALF;
	}

	cmd->port = dev->if_port;
	cmd->phy_address = dev->base_addr;
	cmd->transceiver = XCVR_INTERNAL;

	if (lp->word[0] & ee_AutoNeg) {
		cmd->autoneg = 1;
	}

	return 0;
}

static void eepro_ethtool_get_drvinfo(struct net_device *dev,
					struct ethtool_drvinfo *drvinfo)
{
	strcpy(drvinfo->driver, DRV_NAME);
	strcpy(drvinfo->version, DRV_VERSION);
	sprintf(drvinfo->bus_info, "ISA 0x%lx", dev->base_addr);
}

static const struct ethtool_ops eepro_ethtool_ops = {
	.get_settings	= eepro_ethtool_get_settings,
	.get_drvinfo 	= eepro_ethtool_get_drvinfo,
};

#ifdef MODULE

#define MAX_EEPRO 8
static struct net_device *dev_eepro[MAX_EEPRO];

static int io[MAX_EEPRO] = {
  [0 ... MAX_EEPRO-1] = -1
};
static int irq[MAX_EEPRO];
static int mem[MAX_EEPRO] = {	/* Size of the rx buffer in KB */
  [0 ... MAX_EEPRO-1] = RCV_DEFAULT_RAM/1024
};
static int autodetect;

static int n_eepro;
/* For linux 2.1.xx */

MODULE_AUTHOR("Pascal Dupuis and others");
MODULE_DESCRIPTION("Intel i82595 ISA EtherExpressPro10/10+ driver");
MODULE_LICENSE("GPL");

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(mem, int, NULL, 0);
module_param(autodetect, int, 0);
MODULE_PARM_DESC(io, "EtherExpress Pro/10 I/O base address(es)");
MODULE_PARM_DESC(irq, "EtherExpress Pro/10 IRQ number(s)");
MODULE_PARM_DESC(mem, "EtherExpress Pro/10 Rx buffer size(es) in kB (3-29)");
MODULE_PARM_DESC(autodetect, "EtherExpress Pro/10 force board(s) detection (0-1)");

int __init init_module(void)
{
	struct net_device *dev;
	int i;
	if (io[0] == -1 && autodetect == 0) {
		printk(KERN_WARNING "eepro_init_module: Probe is very dangerous in ISA boards!\n");
		printk(KERN_WARNING "eepro_init_module: Please add \"autodetect=1\" to force probe\n");
		return -ENODEV;
	}
	else if (autodetect) {
		/* if autodetect is set then we must force detection */
		for (i = 0; i < MAX_EEPRO; i++) {
			io[i] = 0;
		}

		printk(KERN_INFO "eepro_init_module: Auto-detecting boards (May God protect us...)\n");
	}

	for (i = 0; i < MAX_EEPRO && io[i] != -1; i++) {
		dev = alloc_etherdev(sizeof(struct eepro_local));
		if (!dev)
			break;

		dev->mem_end = mem[i];
		dev->base_addr = io[i];
		dev->irq = irq[i];

		if (do_eepro_probe(dev) == 0) {
			dev_eepro[n_eepro++] = dev;
			continue;
		}
		free_netdev(dev);
		break;
	}

	if (n_eepro)
		printk(KERN_INFO "%s", version);

	return n_eepro ? 0 : -ENODEV;
}

void __exit
cleanup_module(void)
{
	int i;

	for (i=0; i<n_eepro; i++) {
		struct net_device *dev = dev_eepro[i];
		unregister_netdev(dev);
		release_region(dev->base_addr, EEPRO_IO_EXTENT);
		free_netdev(dev);
	}
}
#endif /* MODULE */

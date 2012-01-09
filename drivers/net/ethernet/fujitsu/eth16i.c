/* eth16i.c An ICL EtherTeam 16i and 32 EISA ethernet driver for Linux

   Written 1994-1999 by Mika Kuoppala

   Copyright (C) 1994-1999 by Mika Kuoppala
   Based on skeleton.c and heavily on at1700.c by Donald Becker

   This software may be used and distributed according to the terms
   of the GNU General Public License, incorporated herein by reference.

   The author may be reached as miku@iki.fi

   This driver supports following cards :
	- ICL EtherTeam 16i
	- ICL EtherTeam 32 EISA
	  (Uses true 32 bit transfers rather than 16i compatibility mode)

   Example Module usage:
        insmod eth16i.o io=0x2a0 mediatype=bnc

	mediatype can be one of the following: bnc,tp,dix,auto,eprom

	'auto' will try to autoprobe mediatype.
	'eprom' will use whatever type defined in eprom.

   I have benchmarked driver with PII/300Mhz as a ftp client
   and 486/33Mhz as a ftp server. Top speed was 1128.37 kilobytes/sec.

   Sources:
     - skeleton.c  a sample network driver core for linux,
       written by Donald Becker <becker@scyld.com>
     - at1700.c a driver for Allied Telesis AT1700, written
       by Donald Becker.
     - e16iSRV.asm a Netware 3.X Server Driver for ICL EtherTeam16i
       written by Markku Viima
     - The Fujitsu MB86965 databook.

   Author thanks following persons due to their valueble assistance:
        Markku Viima (ICL)
	Ari Valve (ICL)
	Donald Becker
	Kurt Huwig <kurt@huwig.de>

   Revision history:

   Version	Date		Description

   0.01         15.12-94        Initial version (card detection)
   0.02         23.01-95        Interrupt is now hooked correctly
   0.03         01.02-95        Rewrote initialization part
   0.04         07.02-95        Base skeleton done...
                                Made a few changes to signature checking
                                to make it a bit reliable.
                                - fixed bug in tx_buf mapping
                                - fixed bug in initialization (DLC_EN
                                  wasn't enabled when initialization
                                  was done.)
   0.05         08.02-95        If there were more than one packet to send,
                                transmit was jammed due to invalid
                                register write...now fixed
   0.06         19.02-95        Rewrote interrupt handling
   0.07         13.04-95        Wrote EEPROM read routines
                                Card configuration now set according to
                                data read from EEPROM
   0.08         23.06-95        Wrote part that tries to probe used interface
                                port if AUTO is selected

   0.09         01.09-95        Added module support

   0.10         04.09-95        Fixed receive packet allocation to work
                                with kernels > 1.3.x

   0.20		20.09-95	Added support for EtherTeam32 EISA

   0.21         17.10-95        Removed the unnecessary extern
				init_etherdev() declaration. Some
				other cleanups.

   0.22		22.02-96	Receive buffer was not flushed
				correctly when faulty packet was
				received. Now fixed.

   0.23		26.02-96	Made resetting the adapter
			 	more reliable.

   0.24		27.02-96	Rewrote faulty packet handling in eth16i_rx

   0.25		22.05-96	kfree() was missing from cleanup_module.

   0.26		11.06-96	Sometimes card was not found by
				check_signature(). Now made more reliable.

   0.27		23.06-96	Oops. 16 consecutive collisions halted
				adapter. Now will try to retransmit
				MAX_COL_16 times before finally giving up.

   0.28	        28.10-97	Added dev_id parameter (NULL) for free_irq

   0.29         29.10-97        Multiple card support for module users

   0.30         30.10-97        Fixed irq allocation bug.
                                (request_irq moved from probe to open)

   0.30a        21.08-98        Card detection made more relaxed. Driver
                                had problems with some TCP/IP-PROM boots
				to find the card. Suggested by
				Kurt Huwig <kurt@huwig.de>

   0.31         28.08-98        Media interface port can now be selected
                                with module parameters or kernel
				boot parameters.

   0.32         31.08-98        IRQ was never freed if open/close
                                pair wasn't called. Now fixed.

   0.33         10.09-98        When eth16i_open() was called after
                                eth16i_close() chip never recovered.
				Now more shallow reset is made on
				close.

   0.34         29.06-99	Fixed one bad #ifdef.
				Changed ioaddr -> io for consistency

   0.35         01.07-99        transmit,-receive bytes were never
                                updated in stats.

   Bugs:
	In some cases the media interface autoprobing code doesn't find
	the correct interface type. In this case you can
	manually choose the interface type in DOS with E16IC.EXE which is
	configuration software for EtherTeam16i and EtherTeam32 cards.
	This is also true for IRQ setting. You cannot use module
	parameter to configure IRQ of the card (yet).

   To do:
	- Real multicast support
	- Rewrite the media interface autoprobing code. Its _horrible_ !
	- Possibly merge all the MB86965 specific code to external
	  module for use by eth16.c and Donald's at1700.c
	- IRQ configuration with module parameter. I will do
	  this when i will get enough info about setting
	  irq without configuration utility.
*/

static char *version =
    "eth16i.c: v0.35 01-Jul-1999 Mika Kuoppala (miku@iki.fi)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/dma.h>



/* Few macros */
#define BITSET(ioaddr, bnum)   ((outb(((inb(ioaddr)) | (bnum)), ioaddr)))
#define BITCLR(ioaddr, bnum)   ((outb(((inb(ioaddr)) & (~(bnum))), ioaddr)))

/* This is the I/O address space for Etherteam 16i adapter. */
#define ETH16I_IO_EXTENT       32

/* Ticks before deciding that transmit has timed out */
#define TX_TIMEOUT             (400*HZ/1000)

/* Maximum loop count when receiving packets */
#define MAX_RX_LOOP            20

/* Some interrupt masks */
#define ETH16I_INTR_ON	       0xef8a       /* Higher is receive mask */
#define ETH16I_INTR_OFF	       0x0000

/* Buffers header status byte meanings */
#define PKT_GOOD               BIT(5)
#define PKT_GOOD_RMT           BIT(4)
#define PKT_SHORT              BIT(3)
#define PKT_ALIGN_ERR          BIT(2)
#define PKT_CRC_ERR            BIT(1)
#define PKT_RX_BUF_OVERFLOW    BIT(0)

/* Transmit status register (DLCR0) */
#define TX_STATUS_REG          0
#define TX_DONE                BIT(7)
#define NET_BUSY               BIT(6)
#define TX_PKT_RCD             BIT(5)
#define CR_LOST                BIT(4)
#define TX_JABBER_ERR	       BIT(3)
#define COLLISION              BIT(2)
#define COLLISIONS_16          BIT(1)

/* Receive status register (DLCR1) */
#define RX_STATUS_REG          1
#define RX_PKT                 BIT(7)  /* Packet received */
#define BUS_RD_ERR             BIT(6)
#define SHORT_PKT_ERR          BIT(3)
#define ALIGN_ERR              BIT(2)
#define CRC_ERR                BIT(1)
#define RX_BUF_OVERFLOW        BIT(0)

/* Transmit Interrupt Enable Register (DLCR2) */
#define TX_INTR_REG            2
#define TX_INTR_DONE           BIT(7)
#define TX_INTR_COL            BIT(2)
#define TX_INTR_16_COL         BIT(1)

/* Receive Interrupt Enable Register (DLCR3) */
#define RX_INTR_REG            3
#define RX_INTR_RECEIVE        BIT(7)
#define RX_INTR_SHORT_PKT      BIT(3)
#define RX_INTR_CRC_ERR        BIT(1)
#define RX_INTR_BUF_OVERFLOW   BIT(0)

/* Transmit Mode Register (DLCR4) */
#define TRANSMIT_MODE_REG      4
#define LOOPBACK_CONTROL       BIT(1)
#define CONTROL_OUTPUT         BIT(2)

/* Receive Mode Register (DLCR5) */
#define RECEIVE_MODE_REG       5
#define RX_BUFFER_EMPTY        BIT(6)
#define ACCEPT_BAD_PACKETS     BIT(5)
#define RECEIVE_SHORT_ADDR     BIT(4)
#define ACCEPT_SHORT_PACKETS   BIT(3)
#define REMOTE_RESET           BIT(2)

#define ADDRESS_FILTER_MODE    BIT(1) | BIT(0)
#define REJECT_ALL             0
#define ACCEPT_ALL             3
#define MODE_1                 1            /* NODE ID, BC, MC, 2-24th bit */
#define MODE_2                 2            /* NODE ID, BC, MC, Hash Table */

/* Configuration Register 0 (DLCR6) */
#define CONFIG_REG_0           6
#define DLC_EN                 BIT(7)
#define SRAM_CYCLE_TIME_100NS  BIT(6)
#define SYSTEM_BUS_WIDTH_8     BIT(5)       /* 1 = 8bit, 0 = 16bit */
#define BUFFER_WIDTH_8         BIT(4)       /* 1 = 8bit, 0 = 16bit */
#define TBS1                   BIT(3)
#define TBS0                   BIT(2)
#define SRAM_BS1               BIT(1)       /* 00=8kb,  01=16kb  */
#define SRAM_BS0               BIT(0)       /* 10=32kb, 11=64kb  */

#ifndef ETH16I_TX_BUF_SIZE                   /* 0 = 2kb, 1 = 4kb  */
#define ETH16I_TX_BUF_SIZE     3             /* 2 = 8kb, 3 = 16kb */
#endif
#define TX_BUF_1x2048          0
#define TX_BUF_2x2048          1
#define TX_BUF_2x4098          2
#define TX_BUF_2x8192          3

/* Configuration Register 1 (DLCR7) */
#define CONFIG_REG_1           7
#define POWERUP                BIT(5)

/* Transmit start register */
#define TRANSMIT_START_REG     10
#define TRANSMIT_START_RB      2
#define TX_START               BIT(7)       /* Rest of register bit indicate*/
                                            /* number of packets in tx buffer*/
/* Node ID registers (DLCR8-13) */
#define NODE_ID_0              8
#define NODE_ID_RB             0

/* Hash Table registers (HT8-15) */
#define HASH_TABLE_0           8
#define HASH_TABLE_RB          1

/* Buffer memory ports */
#define BUFFER_MEM_PORT_LB     8
#define DATAPORT               BUFFER_MEM_PORT_LB
#define BUFFER_MEM_PORT_HB     9

/* 16 Collision control register (BMPR11) */
#define COL_16_REG             11
#define HALT_ON_16             0x00
#define RETRANS_AND_HALT_ON_16 0x02

/* Maximum number of attempts to send after 16 concecutive collisions */
#define MAX_COL_16	       10

/* DMA Burst and Transceiver Mode Register (BMPR13) */
#define TRANSCEIVER_MODE_REG   13
#define TRANSCEIVER_MODE_RB    2
#define IO_BASE_UNLOCK	       BIT(7)
#define LOWER_SQUELCH_TRESH    BIT(6)
#define LINK_TEST_DISABLE      BIT(5)
#define AUI_SELECT             BIT(4)
#define DIS_AUTO_PORT_SEL      BIT(3)

/* Filter Self Receive Register (BMPR14)  */
#define FILTER_SELF_RX_REG     14
#define SKIP_RX_PACKET         BIT(2)
#define FILTER_SELF_RECEIVE    BIT(0)

/* EEPROM Control Register (BMPR 16) */
#define EEPROM_CTRL_REG        16

/* EEPROM Data Register (BMPR 17) */
#define EEPROM_DATA_REG        17

/* NMC93CSx6 EEPROM Control Bits */
#define CS_0                   0x00
#define CS_1                   0x20
#define SK_0                   0x00
#define SK_1                   0x40
#define DI_0                   0x00
#define DI_1                   0x80

/* NMC93CSx6 EEPROM Instructions */
#define EEPROM_READ            0x80

/* NMC93CSx6 EEPROM Addresses */
#define E_NODEID_0             0x02
#define E_NODEID_1             0x03
#define E_NODEID_2             0x04
#define E_PORT_SELECT          0x14
  #define E_PORT_BNC           0x00
  #define E_PORT_DIX           0x01
  #define E_PORT_TP            0x02
  #define E_PORT_AUTO          0x03
  #define E_PORT_FROM_EPROM    0x04
#define E_PRODUCT_CFG          0x30


/* Macro to slow down io between EEPROM clock transitions */
#define eeprom_slow_io() do { int _i = 40; while(--_i > 0) { inb(0x80); }}while(0)

/* Jumperless Configuration Register (BMPR19) */
#define JUMPERLESS_CONFIG      19

/* ID ROM registers, writing to them also resets some parts of chip */
#define ID_ROM_0               24
#define ID_ROM_7               31
#define RESET                  ID_ROM_0

/* This is the I/O address list to be probed when seeking the card */
static unsigned int eth16i_portlist[] __initdata = {
	0x260, 0x280, 0x2A0, 0x240, 0x340, 0x320, 0x380, 0x300, 0
};

static unsigned int eth32i_portlist[] __initdata = {
	0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x8000,
	0x9000, 0xA000, 0xB000, 0xC000, 0xD000, 0xE000, 0xF000, 0
};

/* This is the Interrupt lookup table for Eth16i card */
static unsigned int eth16i_irqmap[] __initdata = { 9, 10, 5, 15, 0 };
#define NUM_OF_ISA_IRQS    4

/* This is the Interrupt lookup table for Eth32i card */
static unsigned int eth32i_irqmap[] __initdata = { 3, 5, 7, 9, 10, 11, 12, 15, 0 };
#define EISA_IRQ_REG	0xc89
#define NUM_OF_EISA_IRQS   8

static unsigned int eth16i_tx_buf_map[] = { 2048, 2048, 4096, 8192 };

/* Use 0 for production, 1 for verification, >2 for debug */
#ifndef ETH16I_DEBUG
#define ETH16I_DEBUG 0
#endif
static unsigned int eth16i_debug = ETH16I_DEBUG;

/* Information for each board */

struct eth16i_local {
	unsigned char     tx_started;
	unsigned char     tx_buf_busy;
	unsigned short    tx_queue;  /* Number of packets in transmit buffer */
	unsigned short    tx_queue_len;
	unsigned int      tx_buf_size;
	unsigned long     open_time;
	unsigned long     tx_buffered_packets;
	unsigned long     tx_buffered_bytes;
	unsigned long     col_16;
	spinlock_t	  lock;
};

/* Function prototypes */

static int     eth16i_probe1(struct net_device *dev, int ioaddr);
static int     eth16i_check_signature(int ioaddr);
static int     eth16i_probe_port(int ioaddr);
static void    eth16i_set_port(int ioaddr, int porttype);
static int     eth16i_send_probe_packet(int ioaddr, unsigned char *b, int l);
static int     eth16i_receive_probe_packet(int ioaddr);
static int     eth16i_get_irq(int ioaddr);
static int     eth16i_read_eeprom(int ioaddr, int offset);
static int     eth16i_read_eeprom_word(int ioaddr);
static void    eth16i_eeprom_cmd(int ioaddr, unsigned char command);
static int     eth16i_open(struct net_device *dev);
static int     eth16i_close(struct net_device *dev);
static netdev_tx_t eth16i_tx(struct sk_buff *skb, struct net_device *dev);
static void    eth16i_rx(struct net_device *dev);
static void    eth16i_timeout(struct net_device *dev);
static irqreturn_t eth16i_interrupt(int irq, void *dev_id);
static void    eth16i_reset(struct net_device *dev);
static void    eth16i_timeout(struct net_device *dev);
static void    eth16i_skip_packet(struct net_device *dev);
static void    eth16i_multicast(struct net_device *dev);
static void    eth16i_select_regbank(unsigned char regbank, int ioaddr);
static void    eth16i_initialize(struct net_device *dev, int boot);

#if 0
static int     eth16i_set_irq(struct net_device *dev);
#endif

#ifdef MODULE
static ushort  eth16i_parse_mediatype(const char* s);
#endif

static char cardname[] __initdata = "ICL EtherTeam 16i/32";

static int __init do_eth16i_probe(struct net_device *dev)
{
	int i;
	int ioaddr;
	int base_addr = dev->base_addr;

	if(eth16i_debug > 4)
		printk(KERN_DEBUG "Probing started for %s\n", cardname);

	if(base_addr > 0x1ff)           /* Check only single location */
		return eth16i_probe1(dev, base_addr);
	else if(base_addr != 0)         /* Don't probe at all */
		return -ENXIO;

	/* Seek card from the ISA io address space */
	for(i = 0; (ioaddr = eth16i_portlist[i]) ; i++)
		if(eth16i_probe1(dev, ioaddr) == 0)
			return 0;

	/* Seek card from the EISA io address space */
	for(i = 0; (ioaddr = eth32i_portlist[i]) ; i++)
		if(eth16i_probe1(dev, ioaddr) == 0)
			return 0;

	return -ENODEV;
}

#ifndef MODULE
struct net_device * __init eth16i_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct eth16i_local));
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_eth16i_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static const struct net_device_ops eth16i_netdev_ops = {
	.ndo_open               = eth16i_open,
	.ndo_stop               = eth16i_close,
	.ndo_start_xmit    	= eth16i_tx,
	.ndo_set_rx_mode	= eth16i_multicast,
	.ndo_tx_timeout 	= eth16i_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __init eth16i_probe1(struct net_device *dev, int ioaddr)
{
	struct eth16i_local *lp = netdev_priv(dev);
	static unsigned version_printed;
	int retval;

	/* Let's grab the region */
	if (!request_region(ioaddr, ETH16I_IO_EXTENT, cardname))
		return -EBUSY;

	/*
	  The MB86985 chip has on register which holds information in which
	  io address the chip lies. First read this register and compare
	  it to our current io address and if match then this could
	  be our chip.
	  */

	if(ioaddr < 0x1000) {
		if(eth16i_portlist[(inb(ioaddr + JUMPERLESS_CONFIG) & 0x07)]
		   != ioaddr) {
			retval = -ENODEV;
			goto out;
		}
	}

	/* Now we will go a bit deeper and try to find the chip's signature */

	if(eth16i_check_signature(ioaddr) != 0) {
		retval = -ENODEV;
		goto out;
	}

	/*
	   Now it seems that we have found a ethernet chip in this particular
	   ioaddr. The MB86985 chip has this feature, that when you read a
	   certain register it will increase it's io base address to next
	   configurable slot. Now when we have found the chip, first thing is
	   to make sure that the chip's ioaddr will hold still here.
	   */

	eth16i_select_regbank(TRANSCEIVER_MODE_RB, ioaddr);
	outb(0x00, ioaddr + TRANSCEIVER_MODE_REG);

	outb(0x00, ioaddr + RESET);             /* Reset some parts of chip */
	BITSET(ioaddr + CONFIG_REG_0, BIT(7));  /* Disable the data link */

	if( (eth16i_debug & version_printed++) == 0)
		printk(KERN_INFO "%s", version);

	dev->base_addr = ioaddr;
	dev->irq = eth16i_get_irq(ioaddr);

	/* Try to obtain interrupt vector */

	if ((retval = request_irq(dev->irq, (void *)&eth16i_interrupt, 0, cardname, dev))) {
		printk(KERN_WARNING "%s at %#3x, but is unusable due to conflicting IRQ %d.\n",
		       cardname, ioaddr, dev->irq);
		goto out;
	}

	printk(KERN_INFO "%s: %s at %#3x, IRQ %d, ",
	       dev->name, cardname, ioaddr, dev->irq);


	/* Now we will have to lock the chip's io address */
	eth16i_select_regbank(TRANSCEIVER_MODE_RB, ioaddr);
	outb(0x38, ioaddr + TRANSCEIVER_MODE_REG);

	eth16i_initialize(dev, 1); /* Initialize rest of the chip's registers */

	/* Now let's same some energy by shutting down the chip ;) */
	BITCLR(ioaddr + CONFIG_REG_1, POWERUP);

	/* Initialize the device structure */
	dev->netdev_ops         = &eth16i_netdev_ops;
	dev->watchdog_timeo	= TX_TIMEOUT;
	spin_lock_init(&lp->lock);

	retval = register_netdev(dev);
	if (retval)
		goto out1;
	return 0;
out1:
	free_irq(dev->irq, dev);
out:
	release_region(ioaddr, ETH16I_IO_EXTENT);
	return retval;
}


static void eth16i_initialize(struct net_device *dev, int boot)
{
	int ioaddr = dev->base_addr;
	int i, node_w = 0;
	unsigned char node_byte = 0;

	/* Setup station address */
	eth16i_select_regbank(NODE_ID_RB, ioaddr);
	for(i = 0 ; i < 3 ; i++) {
		unsigned short node_val = eth16i_read_eeprom(ioaddr, E_NODEID_0 + i);
		((unsigned short *)dev->dev_addr)[i] = ntohs(node_val);
	}

	for(i = 0; i < 6; i++) {
		outb( ((unsigned char *)dev->dev_addr)[i], ioaddr + NODE_ID_0 + i);
		if(boot) {
			printk("%02x", inb(ioaddr + NODE_ID_0 + i));
			if(i != 5)
				printk(":");
		}
	}

	/* Now we will set multicast addresses to accept none */
	eth16i_select_regbank(HASH_TABLE_RB, ioaddr);
	for(i = 0; i < 8; i++)
		outb(0x00, ioaddr + HASH_TABLE_0 + i);

	/*
	  Now let's disable the transmitter and receiver, set the buffer ram
	  cycle time, bus width and buffer data path width. Also we shall
	  set transmit buffer size and total buffer size.
	  */

	eth16i_select_regbank(2, ioaddr);

	node_byte = 0;
	node_w = eth16i_read_eeprom(ioaddr, E_PRODUCT_CFG);

	if( (node_w & 0xFF00) == 0x0800)
		node_byte |= BUFFER_WIDTH_8;

	node_byte |= SRAM_BS1;

	if( (node_w & 0x00FF) == 64)
		node_byte |= SRAM_BS0;

	node_byte |= DLC_EN | SRAM_CYCLE_TIME_100NS | (ETH16I_TX_BUF_SIZE << 2);

	outb(node_byte, ioaddr + CONFIG_REG_0);

	/* We shall halt the transmitting, if 16 collisions are detected */
	outb(HALT_ON_16, ioaddr + COL_16_REG);

#ifdef MODULE
	/* if_port already set by init_module() */
#else
	dev->if_port = (dev->mem_start < E_PORT_FROM_EPROM) ?
		dev->mem_start : E_PORT_FROM_EPROM;
#endif

	/* Set interface port type */
	if(boot) {
		static const char * const porttype[] = {
			"BNC", "DIX", "TP", "AUTO", "FROM_EPROM"
		};

		switch(dev->if_port)
		{

		case E_PORT_FROM_EPROM:
			dev->if_port = eth16i_read_eeprom(ioaddr, E_PORT_SELECT);
			break;

		case E_PORT_AUTO:
			dev->if_port = eth16i_probe_port(ioaddr);
			break;

		case E_PORT_BNC:
		case E_PORT_TP:
		case E_PORT_DIX:
			break;
		}

		printk(" %s interface.\n", porttype[dev->if_port]);

		eth16i_set_port(ioaddr, dev->if_port);
	}

	/* Set Receive Mode to normal operation */
	outb(MODE_2, ioaddr + RECEIVE_MODE_REG);
}

static int eth16i_probe_port(int ioaddr)
{
	int i;
	int retcode;
	unsigned char dummy_packet[64];

	/* Powerup the chip */
	outb(0xc0 | POWERUP, ioaddr + CONFIG_REG_1);

	BITSET(ioaddr + CONFIG_REG_0, DLC_EN);

	eth16i_select_regbank(NODE_ID_RB, ioaddr);

	for(i = 0; i < 6; i++) {
		dummy_packet[i] = inb(ioaddr + NODE_ID_0 + i);
		dummy_packet[i+6] = inb(ioaddr + NODE_ID_0 + i);
	}

	dummy_packet[12] = 0x00;
	dummy_packet[13] = 0x04;
	memset(dummy_packet + 14, 0, sizeof(dummy_packet) - 14);

	eth16i_select_regbank(2, ioaddr);

	for(i = 0; i < 3; i++) {
		BITSET(ioaddr + CONFIG_REG_0, DLC_EN);
		BITCLR(ioaddr + CONFIG_REG_0, DLC_EN);
		eth16i_set_port(ioaddr, i);

		if(eth16i_debug > 1)
			printk(KERN_DEBUG "Set port number %d\n", i);

		retcode = eth16i_send_probe_packet(ioaddr, dummy_packet, 64);
		if(retcode == 0) {
			retcode = eth16i_receive_probe_packet(ioaddr);
			if(retcode != -1) {
				if(eth16i_debug > 1)
					printk(KERN_DEBUG "Eth16i interface port found at %d\n", i);
				return i;
			}
		}
		else {
			if(eth16i_debug > 1)
				printk(KERN_DEBUG "TRANSMIT_DONE timeout when probing interface port\n");
		}
	}

	if( eth16i_debug > 1)
		printk(KERN_DEBUG "Using default port\n");

	return E_PORT_BNC;
}

static void eth16i_set_port(int ioaddr, int porttype)
{
	unsigned short temp = 0;

	eth16i_select_regbank(TRANSCEIVER_MODE_RB, ioaddr);
	outb(LOOPBACK_CONTROL, ioaddr + TRANSMIT_MODE_REG);

	temp |= DIS_AUTO_PORT_SEL;

	switch(porttype) {

	case E_PORT_BNC :
		temp |= AUI_SELECT;
		break;

	case E_PORT_TP :
		break;

	case E_PORT_DIX :
		temp |= AUI_SELECT;
		BITSET(ioaddr + TRANSMIT_MODE_REG, CONTROL_OUTPUT);
		break;
	}

	outb(temp, ioaddr + TRANSCEIVER_MODE_REG);

	if(eth16i_debug > 1) {
		printk(KERN_DEBUG "TRANSMIT_MODE_REG = %x\n", inb(ioaddr + TRANSMIT_MODE_REG));
		printk(KERN_DEBUG "TRANSCEIVER_MODE_REG = %x\n",
		       inb(ioaddr+TRANSCEIVER_MODE_REG));
	}
}

static int eth16i_send_probe_packet(int ioaddr, unsigned char *b, int l)
{
	unsigned long starttime;

	outb(0xff, ioaddr + TX_STATUS_REG);

	outw(l, ioaddr + DATAPORT);
	outsw(ioaddr + DATAPORT, (unsigned short *)b, (l + 1) >> 1);

	starttime = jiffies;
	outb(TX_START | 1, ioaddr + TRANSMIT_START_REG);

	while( (inb(ioaddr + TX_STATUS_REG) & 0x80) == 0) {
		if( time_after(jiffies, starttime + TX_TIMEOUT)) {
			return -1;
		}
	}

	return 0;
}

static int eth16i_receive_probe_packet(int ioaddr)
{
	unsigned long starttime;

	starttime = jiffies;

	while((inb(ioaddr + TX_STATUS_REG) & 0x20) == 0) {
		if( time_after(jiffies, starttime + TX_TIMEOUT)) {

			if(eth16i_debug > 1)
				printk(KERN_DEBUG "Timeout occurred waiting transmit packet received\n");
			starttime = jiffies;
			while((inb(ioaddr + RX_STATUS_REG) & 0x80) == 0) {
				if( time_after(jiffies, starttime + TX_TIMEOUT)) {
					if(eth16i_debug > 1)
						printk(KERN_DEBUG "Timeout occurred waiting receive packet\n");
					return -1;
				}
			}

			if(eth16i_debug > 1)
				printk(KERN_DEBUG "RECEIVE_PACKET\n");
			return 0; /* Found receive packet */
		}
	}

	if(eth16i_debug > 1) {
		printk(KERN_DEBUG "TRANSMIT_PACKET_RECEIVED %x\n", inb(ioaddr + TX_STATUS_REG));
		printk(KERN_DEBUG "RX_STATUS_REG = %x\n", inb(ioaddr + RX_STATUS_REG));
	}

	return 0; /* Return success */
}

#if 0
static int eth16i_set_irq(struct net_device* dev)
{
	const int ioaddr = dev->base_addr;
	const int irq = dev->irq;
	int i = 0;

	if(ioaddr < 0x1000) {
		while(eth16i_irqmap[i] && eth16i_irqmap[i] != irq)
			i++;

		if(i < NUM_OF_ISA_IRQS) {
			u8 cbyte = inb(ioaddr + JUMPERLESS_CONFIG);
			cbyte = (cbyte & 0x3F) | (i << 6);
			outb(cbyte, ioaddr + JUMPERLESS_CONFIG);
			return 0;
		}
	}
	else {
		printk(KERN_NOTICE "%s: EISA Interrupt cannot be set. Use EISA Configuration utility.\n", dev->name);
	}

	return -1;

}
#endif

static int __init eth16i_get_irq(int ioaddr)
{
	unsigned char cbyte;

	if( ioaddr < 0x1000) {
		cbyte = inb(ioaddr + JUMPERLESS_CONFIG);
		return eth16i_irqmap[((cbyte & 0xC0) >> 6)];
	} else {  /* Oh..the card is EISA so method getting IRQ different */
		unsigned short index = 0;
		cbyte = inb(ioaddr + EISA_IRQ_REG);
		while( (cbyte & 0x01) == 0) {
			cbyte = cbyte >> 1;
			index++;
		}
		return eth32i_irqmap[index];
	}
}

static int __init eth16i_check_signature(int ioaddr)
{
	int i;
	unsigned char creg[4] = { 0 };

	for(i = 0; i < 4 ; i++) {

		creg[i] = inb(ioaddr + TRANSMIT_MODE_REG + i);

		if(eth16i_debug > 1)
			printk("eth16i: read signature byte %x at %x\n",
			       creg[i],
			       ioaddr + TRANSMIT_MODE_REG + i);
	}

	creg[0] &= 0x0F;      /* Mask collision cnr */
	creg[2] &= 0x7F;      /* Mask DCLEN bit */

#if 0
	/*
	   This was removed because the card was sometimes left to state
	   from which it couldn't be find anymore. If there is need
	   to more strict check still this have to be fixed.
	   */
	if( ! ((creg[0] == 0x06) && (creg[1] == 0x41)) ) {
		if(creg[1] != 0x42)
			return -1;
	}
#endif

	if( !((creg[2] == 0x36) && (creg[3] == 0xE0)) ) {
		creg[2] &= 0x40;
		creg[3] &= 0x03;

		if( !((creg[2] == 0x40) && (creg[3] == 0x00)) )
			return -1;
	}

	if(eth16i_read_eeprom(ioaddr, E_NODEID_0) != 0)
		return -1;

	if((eth16i_read_eeprom(ioaddr, E_NODEID_1) & 0xFF00) != 0x4B00)
		return -1;

	return 0;
}

static int eth16i_read_eeprom(int ioaddr, int offset)
{
	int data = 0;

	eth16i_eeprom_cmd(ioaddr, EEPROM_READ | offset);
	outb(CS_1, ioaddr + EEPROM_CTRL_REG);
	data = eth16i_read_eeprom_word(ioaddr);
	outb(CS_0 | SK_0, ioaddr + EEPROM_CTRL_REG);

	return data;
}

static int eth16i_read_eeprom_word(int ioaddr)
{
	int i;
	int data = 0;

	for(i = 16; i > 0; i--) {
		outb(CS_1 | SK_0, ioaddr + EEPROM_CTRL_REG);
		eeprom_slow_io();
		outb(CS_1 | SK_1, ioaddr + EEPROM_CTRL_REG);
		eeprom_slow_io();
		data = (data << 1) |
			((inb(ioaddr + EEPROM_DATA_REG) & DI_1) ? 1 : 0);

		eeprom_slow_io();
	}

	return data;
}

static void eth16i_eeprom_cmd(int ioaddr, unsigned char command)
{
	int i;

	outb(CS_0 | SK_0, ioaddr + EEPROM_CTRL_REG);
	outb(DI_0, ioaddr + EEPROM_DATA_REG);
	outb(CS_1 | SK_0, ioaddr + EEPROM_CTRL_REG);
	outb(DI_1, ioaddr + EEPROM_DATA_REG);
	outb(CS_1 | SK_1, ioaddr + EEPROM_CTRL_REG);

	for(i = 7; i >= 0; i--) {
		short cmd = ( (command & (1 << i)) ? DI_1 : DI_0 );
		outb(cmd, ioaddr + EEPROM_DATA_REG);
		outb(CS_1 | SK_0, ioaddr + EEPROM_CTRL_REG);
		eeprom_slow_io();
		outb(CS_1 | SK_1, ioaddr + EEPROM_CTRL_REG);
		eeprom_slow_io();
	}
}

static int eth16i_open(struct net_device *dev)
{
	struct eth16i_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	/* Powerup the chip */
	outb(0xc0 | POWERUP, ioaddr + CONFIG_REG_1);

	/* Initialize the chip */
	eth16i_initialize(dev, 0);

	/* Set the transmit buffer size */
	lp->tx_buf_size = eth16i_tx_buf_map[ETH16I_TX_BUF_SIZE & 0x03];

	if(eth16i_debug > 0)
		printk(KERN_DEBUG "%s: transmit buffer size %d\n",
		       dev->name, lp->tx_buf_size);

	/* Now enable Transmitter and Receiver sections */
	BITCLR(ioaddr + CONFIG_REG_0, DLC_EN);

	/* Now switch to register bank 2, for run time operation */
	eth16i_select_regbank(2, ioaddr);

	lp->open_time = jiffies;
	lp->tx_started = 0;
	lp->tx_queue = 0;
	lp->tx_queue_len = 0;

	/* Turn on interrupts*/
	outw(ETH16I_INTR_ON, ioaddr + TX_INTR_REG);

	netif_start_queue(dev);
	return 0;
}

static int eth16i_close(struct net_device *dev)
{
	struct eth16i_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	eth16i_reset(dev);

	/* Turn off interrupts*/
	outw(ETH16I_INTR_OFF, ioaddr + TX_INTR_REG);

	netif_stop_queue(dev);

	lp->open_time = 0;

	/* Disable transmit and receive */
	BITSET(ioaddr + CONFIG_REG_0, DLC_EN);

	/* Reset the chip */
	/* outb(0xff, ioaddr + RESET); */
	/* outw(0xffff, ioaddr + TX_STATUS_REG);    */

	outb(0x00, ioaddr + CONFIG_REG_1);

	return 0;
}

static void eth16i_timeout(struct net_device *dev)
{
	struct eth16i_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	/*
	   If we get here, some higher level has decided that
	   we are broken. There should really be a "kick me"
	   function call instead.
	   */

	outw(ETH16I_INTR_OFF, ioaddr + TX_INTR_REG);
	printk(KERN_WARNING "%s: transmit timed out with status %04x, %s ?\n",
	       dev->name,
	inw(ioaddr + TX_STATUS_REG),  (inb(ioaddr + TX_STATUS_REG) & TX_DONE) ?
		       "IRQ conflict" : "network cable problem");

	dev->trans_start = jiffies; /* prevent tx timeout */

	/* Let's dump all registers */
	if(eth16i_debug > 0) {
		printk(KERN_DEBUG "%s: timeout: %02x %02x %02x %02x %02x %02x %02x %02x.\n",
		       dev->name, inb(ioaddr + 0),
		       inb(ioaddr + 1), inb(ioaddr + 2),
		       inb(ioaddr + 3), inb(ioaddr + 4),
		       inb(ioaddr + 5),
		       inb(ioaddr + 6), inb(ioaddr + 7));

		printk(KERN_DEBUG "%s: transmit start reg: %02x. collision reg %02x\n",
		       dev->name, inb(ioaddr + TRANSMIT_START_REG),
		       inb(ioaddr + COL_16_REG));
			printk(KERN_DEBUG "lp->tx_queue = %d\n", lp->tx_queue);
		printk(KERN_DEBUG "lp->tx_queue_len = %d\n", lp->tx_queue_len);
		printk(KERN_DEBUG "lp->tx_started = %d\n", lp->tx_started);
	}
	dev->stats.tx_errors++;
	eth16i_reset(dev);
	dev->trans_start = jiffies; /* prevent tx timeout */
	outw(ETH16I_INTR_ON, ioaddr + TX_INTR_REG);
	netif_wake_queue(dev);
}

static netdev_tx_t eth16i_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct eth16i_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int status = 0;
	ushort length = skb->len;
	unsigned char *buf;
	unsigned long flags;

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;
		length = ETH_ZLEN;
	}
	buf = skb->data;

	netif_stop_queue(dev);

	/* Turn off TX interrupts */
	outw(ETH16I_INTR_OFF, ioaddr + TX_INTR_REG);

	/* We would be better doing the disable_irq tricks the 3c509 does,
	   that would make this suck a lot less */

	spin_lock_irqsave(&lp->lock, flags);

	if( (length + 2) > (lp->tx_buf_size - lp->tx_queue_len)) {
		if(eth16i_debug > 0)
			printk(KERN_WARNING "%s: Transmit buffer full.\n", dev->name);
	}
	else {
		outw(length, ioaddr + DATAPORT);

		if( ioaddr < 0x1000 )
			outsw(ioaddr + DATAPORT, buf, (length + 1) >> 1);
		else {
			unsigned char frag = length % 4;
			outsl(ioaddr + DATAPORT, buf, length >> 2);
			if( frag != 0 ) {
				outsw(ioaddr + DATAPORT, (buf + (length & 0xFFFC)), 1);
				if( frag == 3 )
					outsw(ioaddr + DATAPORT,
					      (buf + (length & 0xFFFC) + 2), 1);
			}
		}
		lp->tx_buffered_packets++;
		lp->tx_buffered_bytes = length;
		lp->tx_queue++;
		lp->tx_queue_len += length + 2;
	}
	lp->tx_buf_busy = 0;

	if(lp->tx_started == 0) {
		/* If the transmitter is idle..always trigger a transmit */
		outb(TX_START | lp->tx_queue, ioaddr + TRANSMIT_START_REG);
		lp->tx_queue = 0;
		lp->tx_queue_len = 0;
		lp->tx_started = 1;
		netif_wake_queue(dev);
	}
	else if(lp->tx_queue_len < lp->tx_buf_size - (ETH_FRAME_LEN + 2)) {
		/* There is still more room for one more packet in tx buffer */
		netif_wake_queue(dev);
	}

	spin_unlock_irqrestore(&lp->lock, flags);

	outw(ETH16I_INTR_ON, ioaddr + TX_INTR_REG);
	/* Turn TX interrupts back on */
	/* outb(TX_INTR_DONE | TX_INTR_16_COL, ioaddr + TX_INTR_REG); */
	status = 0;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void eth16i_rx(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	int boguscount = MAX_RX_LOOP;

	/* Loop until all packets have been read */
	while( (inb(ioaddr + RECEIVE_MODE_REG) & RX_BUFFER_EMPTY) == 0) {

		/* Read status byte from receive buffer */
		ushort status = inw(ioaddr + DATAPORT);

		/* Get the size of the packet from receive buffer */
		ushort pkt_len = inw(ioaddr + DATAPORT);

		if(eth16i_debug > 4)
			printk(KERN_DEBUG "%s: Receiving packet mode %02x status %04x.\n",
			       dev->name,
			       inb(ioaddr + RECEIVE_MODE_REG), status);

		if( !(status & PKT_GOOD) ) {
			dev->stats.rx_errors++;

			if( (pkt_len < ETH_ZLEN) || (pkt_len > ETH_FRAME_LEN) ) {
				dev->stats.rx_length_errors++;
				eth16i_reset(dev);
				return;
			}
			else {
				eth16i_skip_packet(dev);
				dev->stats.rx_dropped++;
			}
		}
		else {   /* Ok so now we should have a good packet */
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len + 3);
			if( skb == NULL ) {
				printk(KERN_WARNING "%s: Could'n allocate memory for packet (len %d)\n",
				       dev->name, pkt_len);
				eth16i_skip_packet(dev);
				dev->stats.rx_dropped++;
				break;
			}

			skb_reserve(skb,2);

			/*
			   Now let's get the packet out of buffer.
			   size is (pkt_len + 1) >> 1, cause we are now reading words
			   and it have to be even aligned.
			   */

			if(ioaddr < 0x1000)
				insw(ioaddr + DATAPORT, skb_put(skb, pkt_len),
				     (pkt_len + 1) >> 1);
			else {
				unsigned char *buf = skb_put(skb, pkt_len);
				unsigned char frag = pkt_len % 4;

				insl(ioaddr + DATAPORT, buf, pkt_len >> 2);

				if(frag != 0) {
					unsigned short rest[2];
					rest[0] = inw( ioaddr + DATAPORT );
					if(frag == 3)
						rest[1] = inw( ioaddr + DATAPORT );

					memcpy(buf + (pkt_len & 0xfffc), (char *)rest, frag);
				}
			}

			skb->protocol=eth_type_trans(skb, dev);

			if( eth16i_debug > 5 ) {
				int i;
				printk(KERN_DEBUG "%s: Received packet of length %d.\n",
				       dev->name, pkt_len);
				for(i = 0; i < 14; i++)
					printk(KERN_DEBUG " %02x", skb->data[i]);
				printk(KERN_DEBUG ".\n");
			}
			netif_rx(skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;

		} /* else */

		if(--boguscount <= 0)
			break;

	} /* while */
}

static irqreturn_t eth16i_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct eth16i_local *lp;
	int ioaddr = 0, status;
	int handled = 0;

	ioaddr = dev->base_addr;
	lp = netdev_priv(dev);

	/* Turn off all interrupts from adapter */
	outw(ETH16I_INTR_OFF, ioaddr + TX_INTR_REG);

	/* eth16i_tx won't be called */
	spin_lock(&lp->lock);

	status = inw(ioaddr + TX_STATUS_REG);      /* Get the status */
	outw(status, ioaddr + TX_STATUS_REG);      /* Clear status bits */

	if (status)
		handled = 1;

	if(eth16i_debug > 3)
		printk(KERN_DEBUG "%s: Interrupt with status %04x.\n", dev->name, status);

	if( status & 0x7f00 ) {

		dev->stats.rx_errors++;

		if(status & (BUS_RD_ERR << 8) )
			printk(KERN_WARNING "%s: Bus read error.\n",dev->name);
		if(status & (SHORT_PKT_ERR << 8) )   dev->stats.rx_length_errors++;
		if(status & (ALIGN_ERR << 8) )       dev->stats.rx_frame_errors++;
		if(status & (CRC_ERR << 8) )	    dev->stats.rx_crc_errors++;
		if(status & (RX_BUF_OVERFLOW << 8) ) dev->stats.rx_over_errors++;
	}
	if( status & 0x001a) {

		dev->stats.tx_errors++;

		if(status & CR_LOST) dev->stats.tx_carrier_errors++;
		if(status & TX_JABBER_ERR) dev->stats.tx_window_errors++;

#if 0
		if(status & COLLISION) {
			dev->stats.collisions +=
				((inb(ioaddr+TRANSMIT_MODE_REG) & 0xF0) >> 4);
		}
#endif
		if(status & COLLISIONS_16) {
			if(lp->col_16 < MAX_COL_16) {
				lp->col_16++;
				dev->stats.collisions++;
				/* Resume transmitting, skip failed packet */
				outb(0x02, ioaddr + COL_16_REG);
			}
			else {
				printk(KERN_WARNING "%s: bailing out due to many consecutive 16-in-a-row collisions. Network cable problem?\n", dev->name);
			}
		}
	}

	if( status & 0x00ff ) {          /* Let's check the transmit status reg */

		if(status & TX_DONE) {         /* The transmit has been done */
			dev->stats.tx_packets = lp->tx_buffered_packets;
			dev->stats.tx_bytes += lp->tx_buffered_bytes;
			lp->col_16 = 0;

			if(lp->tx_queue) {           /* Is there still packets ? */
				/* There was packet(s) so start transmitting and write also
				   how many packets there is to be sended */
				outb(TX_START | lp->tx_queue, ioaddr + TRANSMIT_START_REG);
				lp->tx_queue = 0;
				lp->tx_queue_len = 0;
				lp->tx_started = 1;
			}
			else {
				lp->tx_started = 0;
			}
			netif_wake_queue(dev);
		}
	}

	if( ( status & 0x8000 ) ||
	    ( (inb(ioaddr + RECEIVE_MODE_REG) & RX_BUFFER_EMPTY) == 0) ) {
		eth16i_rx(dev);  /* We have packet in receive buffer */
	}

	/* Turn interrupts back on */
	outw(ETH16I_INTR_ON, ioaddr + TX_INTR_REG);

	if(lp->tx_queue_len < lp->tx_buf_size - (ETH_FRAME_LEN + 2)) {
		/* There is still more room for one more packet in tx buffer */
		netif_wake_queue(dev);
	}

	spin_unlock(&lp->lock);

	return IRQ_RETVAL(handled);
}

static void eth16i_skip_packet(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	inw(ioaddr + DATAPORT);
	inw(ioaddr + DATAPORT);
	inw(ioaddr + DATAPORT);

	outb(SKIP_RX_PACKET, ioaddr + FILTER_SELF_RX_REG);
	while( inb( ioaddr + FILTER_SELF_RX_REG ) != 0);
}

static void eth16i_reset(struct net_device *dev)
{
	struct eth16i_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	if(eth16i_debug > 1)
		printk(KERN_DEBUG "%s: Resetting device.\n", dev->name);

	BITSET(ioaddr + CONFIG_REG_0, DLC_EN);
	outw(0xffff, ioaddr + TX_STATUS_REG);
	eth16i_select_regbank(2, ioaddr);

	lp->tx_started = 0;
	lp->tx_buf_busy = 0;
	lp->tx_queue = 0;
	lp->tx_queue_len = 0;
	BITCLR(ioaddr + CONFIG_REG_0, DLC_EN);
}

static void eth16i_multicast(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	if (!netdev_mc_empty(dev) || dev->flags&(IFF_ALLMULTI|IFF_PROMISC))
	{
		outb(3, ioaddr + RECEIVE_MODE_REG);
	} else {
		outb(2, ioaddr + RECEIVE_MODE_REG);
	}
}

static void eth16i_select_regbank(unsigned char banknbr, int ioaddr)
{
	unsigned char data;

	data = inb(ioaddr + CONFIG_REG_1);
	outb( ((data & 0xF3) | ( (banknbr & 0x03) << 2)), ioaddr + CONFIG_REG_1);
}

#ifdef MODULE

static ushort eth16i_parse_mediatype(const char* s)
{
	if(!s)
		return E_PORT_FROM_EPROM;

        if (!strncmp(s, "bnc", 3))
		return E_PORT_BNC;
        else if (!strncmp(s, "tp", 2))
                return E_PORT_TP;
        else if (!strncmp(s, "dix", 3))
                return E_PORT_DIX;
        else if (!strncmp(s, "auto", 4))
		return E_PORT_AUTO;
	else
		return E_PORT_FROM_EPROM;
}

#define MAX_ETH16I_CARDS 4  /* Max number of Eth16i cards per module */

static struct net_device *dev_eth16i[MAX_ETH16I_CARDS];
static int io[MAX_ETH16I_CARDS];
#if 0
static int irq[MAX_ETH16I_CARDS];
#endif
static char* mediatype[MAX_ETH16I_CARDS];
static int debug = -1;

MODULE_AUTHOR("Mika Kuoppala <miku@iki.fi>");
MODULE_DESCRIPTION("ICL EtherTeam 16i/32 driver");
MODULE_LICENSE("GPL");


module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io, "eth16i I/O base address(es)");

#if 0
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq, "eth16i interrupt request number");
#endif

module_param_array(mediatype, charp, NULL, 0);
MODULE_PARM_DESC(mediatype, "eth16i media type of interface(s) (bnc,tp,dix,auto,eprom)");

module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "eth16i debug level (0-6)");

int __init init_module(void)
{
	int this_dev, found = 0;
	struct net_device *dev;

	for (this_dev = 0; this_dev < MAX_ETH16I_CARDS; this_dev++) {
		dev = alloc_etherdev(sizeof(struct eth16i_local));
		if (!dev)
			break;

		dev->base_addr = io[this_dev];

	        if(debug != -1)
			eth16i_debug = debug;

		if(eth16i_debug > 1)
			printk(KERN_NOTICE "eth16i(%d): interface type %s\n", this_dev, mediatype[this_dev] ? mediatype[this_dev] : "none" );

		dev->if_port = eth16i_parse_mediatype(mediatype[this_dev]);

		if(io[this_dev] == 0) {
			if (this_dev != 0) { /* Only autoprobe 1st one */
				free_netdev(dev);
				break;
			}

			printk(KERN_NOTICE "eth16i.c: Presently autoprobing (not recommended) for a single card.\n");
		}

		if (do_eth16i_probe(dev) == 0) {
			dev_eth16i[found++] = dev;
			continue;
		}
		printk(KERN_WARNING "eth16i.c No Eth16i card found (i/o = 0x%x).\n",
		       io[this_dev]);
		free_netdev(dev);
		break;
	}
	if (found)
		return 0;
	return -ENXIO;
}

void __exit cleanup_module(void)
{
	int this_dev;

	for(this_dev = 0; this_dev < MAX_ETH16I_CARDS; this_dev++) {
		struct net_device *dev = dev_eth16i[this_dev];

		if (netdev_priv(dev)) {
			unregister_netdev(dev);
			free_irq(dev->irq, dev);
			release_region(dev->base_addr, ETH16I_IO_EXTENT);
			free_netdev(dev);
		}
	}
}
#endif /* MODULE */

/*
 *
 * Alchemy Au1x00 ethernet driver
 *
 * Copyright 2001-2003, 2006 MontaVista Software Inc.
 * Copyright 2002 TimeSys Corp.
 * Added ethtool/mii-tool support,
 * Copyright 2004 Matt Porter <mporter@kernel.crashing.org>
 * Update: 2004 Bjoern Riemer, riemer@fokus.fraunhofer.de 
 * or riemer@riemer-nt.de: fixed the link beat detection with 
 * ioctls (SIOCGMIIPHY)
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <asm/mipsregs.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/processor.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/cpu.h>
#include "au1000_eth.h"

#ifdef AU1000_ETH_DEBUG
static int au1000_debug = 5;
#else
static int au1000_debug = 3;
#endif

#define DRV_NAME	"au1000_eth"
#define DRV_VERSION	"1.5"
#define DRV_AUTHOR	"Pete Popov <ppopov@embeddedalley.com>"
#define DRV_DESC	"Au1xxx on-chip Ethernet driver"

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");

// prototypes
static void hard_stop(struct net_device *);
static void enable_rx_tx(struct net_device *dev);
static struct net_device * au1000_probe(int port_num);
static int au1000_init(struct net_device *);
static int au1000_open(struct net_device *);
static int au1000_close(struct net_device *);
static int au1000_tx(struct sk_buff *, struct net_device *);
static int au1000_rx(struct net_device *);
static irqreturn_t au1000_interrupt(int, void *, struct pt_regs *);
static void au1000_tx_timeout(struct net_device *);
static int au1000_set_config(struct net_device *dev, struct ifmap *map);
static void set_rx_mode(struct net_device *);
static struct net_device_stats *au1000_get_stats(struct net_device *);
static void au1000_timer(unsigned long);
static int au1000_ioctl(struct net_device *, struct ifreq *, int);
static int mdio_read(struct net_device *, int, int);
static void mdio_write(struct net_device *, int, int, u16);
static void dump_mii(struct net_device *dev, int phy_id);

// externs
extern  void ack_rise_edge_irq(unsigned int);
extern int get_ethernet_addr(char *ethernet_addr);
extern void str2eaddr(unsigned char *ea, unsigned char *str);
extern char * __init prom_getcmdline(void);

/*
 * Theory of operation
 *
 * The Au1000 MACs use a simple rx and tx descriptor ring scheme. 
 * There are four receive and four transmit descriptors.  These 
 * descriptors are not in memory; rather, they are just a set of 
 * hardware registers.
 *
 * Since the Au1000 has a coherent data cache, the receive and
 * transmit buffers are allocated from the KSEG0 segment. The 
 * hardware registers, however, are still mapped at KSEG1 to
 * make sure there's no out-of-order writes, and that all writes
 * complete immediately.
 */

/* These addresses are only used if yamon doesn't tell us what
 * the mac address is, and the mac address is not passed on the
 * command line.
 */
static unsigned char au1000_mac_addr[6] __devinitdata = { 
	0x00, 0x50, 0xc2, 0x0c, 0x30, 0x00
};

#define nibswap(x) ((((x) >> 4) & 0x0f) | (((x) << 4) & 0xf0))
#define RUN_AT(x) (jiffies + (x))

// For reading/writing 32-bit words from/to DMA memory
#define cpu_to_dma32 cpu_to_be32
#define dma32_to_cpu be32_to_cpu

struct au1000_private *au_macs[NUM_ETH_INTERFACES];

/* FIXME 
 * All of the PHY code really should be detached from the MAC 
 * code.
 */

/* Default advertise */
#define GENMII_DEFAULT_ADVERTISE \
	ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full | \
	ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full | \
	ADVERTISED_Autoneg

#define GENMII_DEFAULT_FEATURES \
	SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full | \
	SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full | \
	SUPPORTED_Autoneg

int bcm_5201_init(struct net_device *dev, int phy_addr)
{
	s16 data;
	
	/* Stop auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, data & ~MII_CNTL_AUTO);

	/* Set advertisement to 10/100 and Half/Full duplex
	 * (full capabilities) */
	data = mdio_read(dev, phy_addr, MII_ANADV);
	data |= MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_T_FDX | MII_NWAY_T;
	mdio_write(dev, phy_addr, MII_ANADV, data);
	
	/* Restart auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	data |= MII_CNTL_RST_AUTO | MII_CNTL_AUTO;
	mdio_write(dev, phy_addr, MII_CONTROL, data);

	if (au1000_debug > 4) 
		dump_mii(dev, phy_addr);
	return 0;
}

int bcm_5201_reset(struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int 
bcm_5201_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	if (!dev) {
		printk(KERN_ERR "bcm_5201_status error: NULL dev\n");
		return -1;
	}
	aup = (struct au1000_private *) dev->priv;

	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);
	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, MII_AUX_CNTRL);
		if (mii_data & MII_AUX_100) {
			if (mii_data & MII_AUX_FDX) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else  {
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}

	}
	else {
		*link = 0;
		*speed = 0;
		dev->if_port = IF_PORT_UNKNOWN;
	}
	return 0;
}

int lsi_80227_init(struct net_device *dev, int phy_addr)
{
	if (au1000_debug > 4)
		printk("lsi_80227_init\n");

	/* restart auto-negotiation */
	mdio_write(dev, phy_addr, MII_CONTROL,
		   MII_CNTL_F100 | MII_CNTL_AUTO | MII_CNTL_RST_AUTO); // | MII_CNTL_FDX);
	mdelay(1);

	/* set up LEDs to correct display */
#ifdef CONFIG_MIPS_MTX1
	mdio_write(dev, phy_addr, 17, 0xff80);
#else
	mdio_write(dev, phy_addr, 17, 0xffc0);
#endif

	if (au1000_debug > 4)
		dump_mii(dev, phy_addr);
	return 0;
}

int lsi_80227_reset(struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
	if (au1000_debug > 4) {
		printk("lsi_80227_reset\n");
		dump_mii(dev, phy_addr);
	}

	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int
lsi_80227_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	if (!dev) {
		printk(KERN_ERR "lsi_80227_status error: NULL dev\n");
		return -1;
	}
	aup = (struct au1000_private *) dev->priv;

	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);
	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, MII_LSI_PHY_STAT);
		if (mii_data & MII_LSI_PHY_STAT_SPD) {
			if (mii_data & MII_LSI_PHY_STAT_FDX) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else  {
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}

	}
	else {
		*link = 0;
		*speed = 0;
		dev->if_port = IF_PORT_UNKNOWN;
	}
	return 0;
}

int am79c901_init(struct net_device *dev, int phy_addr)
{
	printk("am79c901_init\n");
	return 0;
}

int am79c901_reset(struct net_device *dev, int phy_addr)
{
	printk("am79c901_reset\n");
	return 0;
}

int 
am79c901_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	return 0;
}

int am79c874_init(struct net_device *dev, int phy_addr)
{
	s16 data;

	/* 79c874 has quit resembled bit assignments to BCM5201 */
	if (au1000_debug > 4)
		printk("am79c847_init\n");

	/* Stop auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, data & ~MII_CNTL_AUTO);

	/* Set advertisement to 10/100 and Half/Full duplex
	 * (full capabilities) */
	data = mdio_read(dev, phy_addr, MII_ANADV);
	data |= MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_T_FDX | MII_NWAY_T;
	mdio_write(dev, phy_addr, MII_ANADV, data);
	
	/* Restart auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	data |= MII_CNTL_RST_AUTO | MII_CNTL_AUTO;

	mdio_write(dev, phy_addr, MII_CONTROL, data);

	if (au1000_debug > 4) dump_mii(dev, phy_addr);
	return 0;
}

int am79c874_reset(struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
	if (au1000_debug > 4)
		printk("am79c874_reset\n");

	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int 
am79c874_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	// printk("am79c874_status\n");
	if (!dev) {
		printk(KERN_ERR "am79c874_status error: NULL dev\n");
		return -1;
	}

	aup = (struct au1000_private *) dev->priv;
	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);

	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, MII_AMD_PHY_STAT);
		if (mii_data & MII_AMD_PHY_STAT_SPD) {
			if (mii_data & MII_AMD_PHY_STAT_FDX) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else {
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}

	}
	else {
		*link = 0;
		*speed = 0;
		dev->if_port = IF_PORT_UNKNOWN;
	}
	return 0;
}

int lxt971a_init(struct net_device *dev, int phy_addr)
{
	if (au1000_debug > 4)
		printk("lxt971a_init\n");

	/* restart auto-negotiation */
	mdio_write(dev, phy_addr, MII_CONTROL,
		   MII_CNTL_F100 | MII_CNTL_AUTO | MII_CNTL_RST_AUTO | MII_CNTL_FDX);

	/* set up LEDs to correct display */
	mdio_write(dev, phy_addr, 20, 0x0422);

	if (au1000_debug > 4)
		dump_mii(dev, phy_addr);
	return 0;
}

int lxt971a_reset(struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
	if (au1000_debug > 4) {
		printk("lxt971a_reset\n");
		dump_mii(dev, phy_addr);
	}

	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int
lxt971a_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	if (!dev) {
		printk(KERN_ERR "lxt971a_status error: NULL dev\n");
		return -1;
	}
	aup = (struct au1000_private *) dev->priv;

	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);
	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, MII_INTEL_PHY_STAT);
		if (mii_data & MII_INTEL_PHY_STAT_SPD) {
			if (mii_data & MII_INTEL_PHY_STAT_FDX) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else  {
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}

	}
	else {
		*link = 0;
		*speed = 0;
		dev->if_port = IF_PORT_UNKNOWN;
	}
	return 0;
}

int ks8995m_init(struct net_device *dev, int phy_addr)
{
	s16 data;
	
//	printk("ks8995m_init\n");
	/* Stop auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, data & ~MII_CNTL_AUTO);

	/* Set advertisement to 10/100 and Half/Full duplex
	 * (full capabilities) */
	data = mdio_read(dev, phy_addr, MII_ANADV);
	data |= MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_T_FDX | MII_NWAY_T;
	mdio_write(dev, phy_addr, MII_ANADV, data);
	
	/* Restart auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	data |= MII_CNTL_RST_AUTO | MII_CNTL_AUTO;
	mdio_write(dev, phy_addr, MII_CONTROL, data);

	if (au1000_debug > 4) dump_mii(dev, phy_addr);

	return 0;
}

int ks8995m_reset(struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
//	printk("ks8995m_reset\n");
	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int ks8995m_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	if (!dev) {
		printk(KERN_ERR "ks8995m_status error: NULL dev\n");
		return -1;
	}
	aup = (struct au1000_private *) dev->priv;

	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);
	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, MII_AUX_CNTRL);
		if (mii_data & MII_AUX_100) {
			if (mii_data & MII_AUX_FDX) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else  {											
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}

	}
	else {
		*link = 0;
		*speed = 0;
		dev->if_port = IF_PORT_UNKNOWN;
	}
	return 0;
}

int
smsc_83C185_init (struct net_device *dev, int phy_addr)
{
	s16 data;

	if (au1000_debug > 4)
		printk("smsc_83C185_init\n");

	/* Stop auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, data & ~MII_CNTL_AUTO);

	/* Set advertisement to 10/100 and Half/Full duplex
	 * (full capabilities) */
	data = mdio_read(dev, phy_addr, MII_ANADV);
	data |= MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_T_FDX | MII_NWAY_T;
	mdio_write(dev, phy_addr, MII_ANADV, data);
	
	/* Restart auto-negotiation */
	data = mdio_read(dev, phy_addr, MII_CONTROL);
	data |= MII_CNTL_RST_AUTO | MII_CNTL_AUTO;

	mdio_write(dev, phy_addr, MII_CONTROL, data);

	if (au1000_debug > 4) dump_mii(dev, phy_addr);
	return 0;
}

int
smsc_83C185_reset (struct net_device *dev, int phy_addr)
{
	s16 mii_control, timeout;
	
	if (au1000_debug > 4)
		printk("smsc_83C185_reset\n");

	mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
	mdio_write(dev, phy_addr, MII_CONTROL, mii_control | MII_CNTL_RESET);
	mdelay(1);
	for (timeout = 100; timeout > 0; --timeout) {
		mii_control = mdio_read(dev, phy_addr, MII_CONTROL);
		if ((mii_control & MII_CNTL_RESET) == 0)
			break;
		mdelay(1);
	}
	if (mii_control & MII_CNTL_RESET) {
		printk(KERN_ERR "%s PHY reset timeout !\n", dev->name);
		return -1;
	}
	return 0;
}

int 
smsc_83C185_status (struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	u16 mii_data;
	struct au1000_private *aup;

	if (!dev) {
		printk(KERN_ERR "smsc_83C185_status error: NULL dev\n");
		return -1;
	}

	aup = (struct au1000_private *) dev->priv;
	mii_data = mdio_read(dev, aup->phy_addr, MII_STATUS);

	if (mii_data & MII_STAT_LINK) {
		*link = 1;
		mii_data = mdio_read(dev, aup->phy_addr, 0x1f);
		if (mii_data & (1<<3)) {
			if (mii_data & (1<<4)) {
				*speed = IF_PORT_100BASEFX;
				dev->if_port = IF_PORT_100BASEFX;
			}
			else {
				*speed = IF_PORT_100BASETX;
				dev->if_port = IF_PORT_100BASETX;
			}
		}
		else {
			*speed = IF_PORT_10BASET;
			dev->if_port = IF_PORT_10BASET;
		}
	}
	else {
		*link = 0;
		*speed = 0;
		dev->if_port = IF_PORT_UNKNOWN;
	}
	return 0;
}


#ifdef CONFIG_MIPS_BOSPORUS
int stub_init(struct net_device *dev, int phy_addr)
{
	//printk("PHY stub_init\n");
	return 0;
}

int stub_reset(struct net_device *dev, int phy_addr)
{
	//printk("PHY stub_reset\n");
	return 0;
}

int 
stub_status(struct net_device *dev, int phy_addr, u16 *link, u16 *speed)
{
	//printk("PHY stub_status\n");
	*link = 1;
	/* hmmm, revisit */
	*speed = IF_PORT_100BASEFX;
	dev->if_port = IF_PORT_100BASEFX;
	return 0;
}
#endif

struct phy_ops bcm_5201_ops = {
	bcm_5201_init,
	bcm_5201_reset,
	bcm_5201_status,
};

struct phy_ops am79c874_ops = {
	am79c874_init,
	am79c874_reset,
	am79c874_status,
};

struct phy_ops am79c901_ops = {
	am79c901_init,
	am79c901_reset,
	am79c901_status,
};

struct phy_ops lsi_80227_ops = { 
	lsi_80227_init,
	lsi_80227_reset,
	lsi_80227_status,
};

struct phy_ops lxt971a_ops = { 
	lxt971a_init,
	lxt971a_reset,
	lxt971a_status,
};

struct phy_ops ks8995m_ops = {
	ks8995m_init,
	ks8995m_reset,
	ks8995m_status,
};

struct phy_ops smsc_83C185_ops = {
	smsc_83C185_init,
	smsc_83C185_reset,
	smsc_83C185_status,
};

#ifdef CONFIG_MIPS_BOSPORUS
struct phy_ops stub_ops = {
	stub_init,
	stub_reset,
	stub_status,
};
#endif

static struct mii_chip_info {
	const char * name;
	u16 phy_id0;
	u16 phy_id1;
	struct phy_ops *phy_ops;	
	int dual_phy;
} mii_chip_table[] = {
	{"Broadcom BCM5201 10/100 BaseT PHY",0x0040,0x6212, &bcm_5201_ops,0},
	{"Broadcom BCM5221 10/100 BaseT PHY",0x0040,0x61e4, &bcm_5201_ops,0},
	{"Broadcom BCM5222 10/100 BaseT PHY",0x0040,0x6322, &bcm_5201_ops,1},
	{"NS DP83847 PHY", 0x2000, 0x5c30, &bcm_5201_ops ,0},
	{"AMD 79C901 HomePNA PHY",0x0000,0x35c8, &am79c901_ops,0},
	{"AMD 79C874 10/100 BaseT PHY",0x0022,0x561b, &am79c874_ops,0},
	{"LSI 80227 10/100 BaseT PHY",0x0016,0xf840, &lsi_80227_ops,0},
	{"Intel LXT971A Dual Speed PHY",0x0013,0x78e2, &lxt971a_ops,0},
	{"Kendin KS8995M 10/100 BaseT PHY",0x0022,0x1450, &ks8995m_ops,0},
	{"SMSC LAN83C185 10/100 BaseT PHY",0x0007,0xc0a3, &smsc_83C185_ops,0},
#ifdef CONFIG_MIPS_BOSPORUS
	{"Stub", 0x1234, 0x5678, &stub_ops },
#endif
	{0,},
};

static int mdio_read(struct net_device *dev, int phy_id, int reg)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	volatile u32 *mii_control_reg;
	volatile u32 *mii_data_reg;
	u32 timedout = 20;
	u32 mii_control;

	#ifdef CONFIG_BCM5222_DUAL_PHY
	/* First time we probe, it's for the mac0 phy.
	 * Since we haven't determined yet that we have a dual phy,
	 * aup->mii->mii_control_reg won't be setup and we'll
	 * default to the else statement.
	 * By the time we probe for the mac1 phy, the mii_control_reg
	 * will be setup to be the address of the mac0 phy control since
	 * both phys are controlled through mac0.
	 */
	if (aup->mii && aup->mii->mii_control_reg) {
		mii_control_reg = aup->mii->mii_control_reg;
		mii_data_reg = aup->mii->mii_data_reg;
	}
	else if (au_macs[0]->mii && au_macs[0]->mii->mii_control_reg) {
		/* assume both phys are controlled through mac0 */
		mii_control_reg = au_macs[0]->mii->mii_control_reg;
		mii_data_reg = au_macs[0]->mii->mii_data_reg;
	}
	else 
	#endif
	{
		/* default control and data reg addresses */
		mii_control_reg = &aup->mac->mii_control;
		mii_data_reg = &aup->mac->mii_data;
	}

	while (*mii_control_reg & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: read_MII busy timeout!!\n", 
					dev->name);
			return -1;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) | 
		MAC_SET_MII_SELECT_PHY(phy_id) | MAC_MII_READ;

	*mii_control_reg = mii_control;

	timedout = 20;
	while (*mii_control_reg & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: mdio_read busy timeout!!\n", 
					dev->name);
			return -1;
		}
	}
	return (int)*mii_data_reg;
}

static void mdio_write(struct net_device *dev, int phy_id, int reg, u16 value)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	volatile u32 *mii_control_reg;
	volatile u32 *mii_data_reg;
	u32 timedout = 20;
	u32 mii_control;

	#ifdef CONFIG_BCM5222_DUAL_PHY
	if (aup->mii && aup->mii->mii_control_reg) {
		mii_control_reg = aup->mii->mii_control_reg;
		mii_data_reg = aup->mii->mii_data_reg;
	}
	else if (au_macs[0]->mii && au_macs[0]->mii->mii_control_reg) {
		/* assume both phys are controlled through mac0 */
		mii_control_reg = au_macs[0]->mii->mii_control_reg;
		mii_data_reg = au_macs[0]->mii->mii_data_reg;
	}
	else 
	#endif
	{
		/* default control and data reg addresses */
		mii_control_reg = &aup->mac->mii_control;
		mii_data_reg = &aup->mac->mii_data;
	}

	while (*mii_control_reg & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: mdio_write busy timeout!!\n", 
					dev->name);
			return;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) | 
		MAC_SET_MII_SELECT_PHY(phy_id) | MAC_MII_WRITE;

	*mii_data_reg = value;
	*mii_control_reg = mii_control;
}


static void dump_mii(struct net_device *dev, int phy_id)
{
	int i, val;

	for (i = 0; i < 7; i++) {
		if ((val = mdio_read(dev, phy_id, i)) >= 0)
			printk("%s: MII Reg %d=%x\n", dev->name, i, val);
	}
	for (i = 16; i < 25; i++) {
		if ((val = mdio_read(dev, phy_id, i)) >= 0)
			printk("%s: MII Reg %d=%x\n", dev->name, i, val);
	}
}

static int mii_probe (struct net_device * dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	int phy_addr;
#ifdef CONFIG_MIPS_BOSPORUS
	int phy_found=0;
#endif

	/* search for total of 32 possible mii phy addresses */
	for (phy_addr = 0; phy_addr < 32; phy_addr++) {
		u16 mii_status;
		u16 phy_id0, phy_id1;
		int i;

		#ifdef CONFIG_BCM5222_DUAL_PHY
		/* Mask the already found phy, try next one */
		if (au_macs[0]->mii && au_macs[0]->mii->mii_control_reg) {
			if (au_macs[0]->phy_addr == phy_addr)
				continue;
		}
		#endif

		mii_status = mdio_read(dev, phy_addr, MII_STATUS);
		if (mii_status == 0xffff || mii_status == 0x0000)
			/* the mii is not accessable, try next one */
			continue;

		phy_id0 = mdio_read(dev, phy_addr, MII_PHY_ID0);
		phy_id1 = mdio_read(dev, phy_addr, MII_PHY_ID1);

		/* search our mii table for the current mii */ 
		for (i = 0; mii_chip_table[i].phy_id1; i++) {
			if (phy_id0 == mii_chip_table[i].phy_id0 &&
			    phy_id1 == mii_chip_table[i].phy_id1) {
				struct mii_phy * mii_phy = aup->mii;

				printk(KERN_INFO "%s: %s at phy address %d\n",
				       dev->name, mii_chip_table[i].name, 
				       phy_addr);
#ifdef CONFIG_MIPS_BOSPORUS
				phy_found = 1;
#endif
				mii_phy->chip_info = mii_chip_table+i;
				aup->phy_addr = phy_addr;
				aup->want_autoneg = 1;
				aup->phy_ops = mii_chip_table[i].phy_ops;
				aup->phy_ops->phy_init(dev,phy_addr);

				// Check for dual-phy and then store required 
				// values and set indicators. We need to do 
				// this now since mdio_{read,write} need the 
				// control and data register addresses.
				#ifdef CONFIG_BCM5222_DUAL_PHY
				if ( mii_chip_table[i].dual_phy) {

					/* assume both phys are controlled 
					 * through MAC0. Board specific? */
					
					/* sanity check */
					if (!au_macs[0] || !au_macs[0]->mii)
						return -1;
					aup->mii->mii_control_reg = (u32 *)
						&au_macs[0]->mac->mii_control;
					aup->mii->mii_data_reg = (u32 *)
						&au_macs[0]->mac->mii_data;
				}
				#endif
				goto found;
			}
		}
	}
found:

#ifdef CONFIG_MIPS_BOSPORUS
	/* This is a workaround for the Micrel/Kendin 5 port switch
	   The second MAC doesn't see a PHY connected... so we need to
	   trick it into thinking we have one.
		
	   If this kernel is run on another Au1500 development board
	   the stub will be found as well as the actual PHY. However,
	   the last found PHY will be used... usually at Addr 31 (Db1500).	
	*/
	if ( (!phy_found) )
	{
		u16 phy_id0, phy_id1;
		int i;

		phy_id0 = 0x1234;
		phy_id1 = 0x5678;

		/* search our mii table for the current mii */ 
		for (i = 0; mii_chip_table[i].phy_id1; i++) {
			if (phy_id0 == mii_chip_table[i].phy_id0 &&
			    phy_id1 == mii_chip_table[i].phy_id1) {
				struct mii_phy * mii_phy;

				printk(KERN_INFO "%s: %s at phy address %d\n",
				       dev->name, mii_chip_table[i].name, 
				       phy_addr);
				mii_phy = kmalloc(sizeof(struct mii_phy), 
						GFP_KERNEL);
				if (mii_phy) {
					mii_phy->chip_info = mii_chip_table+i;
					aup->phy_addr = phy_addr;
					mii_phy->next = aup->mii;
					aup->phy_ops = 
						mii_chip_table[i].phy_ops;
					aup->mii = mii_phy;
					aup->phy_ops->phy_init(dev,phy_addr);
				} else {
					printk(KERN_ERR "%s: out of memory\n", 
							dev->name);
					return -1;
				}
				mii_phy->chip_info = mii_chip_table+i;
				aup->phy_addr = phy_addr;
				aup->phy_ops = mii_chip_table[i].phy_ops;
				aup->phy_ops->phy_init(dev,phy_addr);
				break;
			}
		}
	}
	if (aup->mac_id == 0) {
		/* the Bosporus phy responds to addresses 0-5 but 
		 * 5 is the correct one.
		 */
		aup->phy_addr = 5;
	}
#endif

	if (aup->mii->chip_info == NULL) {
		printk(KERN_ERR "%s: Au1x No known MII transceivers found!\n",
				dev->name);
		return -1;
	}

	printk(KERN_INFO "%s: Using %s as default\n", 
			dev->name, aup->mii->chip_info->name);

	return 0;
}


/*
 * Buffer allocation/deallocation routines. The buffer descriptor returned
 * has the virtual and dma address of a buffer suitable for 
 * both, receive and transmit operations.
 */
static db_dest_t *GetFreeDB(struct au1000_private *aup)
{
	db_dest_t *pDB;
	pDB = aup->pDBfree;

	if (pDB) {
		aup->pDBfree = pDB->pnext;
	}
	return pDB;
}

void ReleaseDB(struct au1000_private *aup, db_dest_t *pDB)
{
	db_dest_t *pDBfree = aup->pDBfree;
	if (pDBfree)
		pDBfree->pnext = pDB;
	aup->pDBfree = pDB;
}

static void enable_rx_tx(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: enable_rx_tx\n", dev->name);

	aup->mac->control |= (MAC_RX_ENABLE | MAC_TX_ENABLE);
	au_sync_delay(10);
}

static void hard_stop(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: hard stop\n", dev->name);

	aup->mac->control &= ~(MAC_RX_ENABLE | MAC_TX_ENABLE);
	au_sync_delay(10);
}


static void reset_mac(struct net_device *dev)
{
	int i;
	u32 flags;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4) 
		printk(KERN_INFO "%s: reset mac, aup %x\n", 
				dev->name, (unsigned)aup);

	spin_lock_irqsave(&aup->lock, flags);
	if (aup->timer.function == &au1000_timer) {/* check if timer initted */
		del_timer(&aup->timer);
	}

	hard_stop(dev);
	#ifdef CONFIG_BCM5222_DUAL_PHY
	if (aup->mac_id != 0) {
	#endif
		/* If BCM5222, we can't leave MAC0 in reset because then 
		 * we can't access the dual phy for ETH1 */
		*aup->enable = MAC_EN_CLOCK_ENABLE;
		au_sync_delay(2);
		*aup->enable = 0;
		au_sync_delay(2);
	#ifdef CONFIG_BCM5222_DUAL_PHY
	}
	#endif
	aup->tx_full = 0;
	for (i = 0; i < NUM_RX_DMA; i++) {
		/* reset control bits */
		aup->rx_dma_ring[i]->buff_stat &= ~0xf;
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		/* reset control bits */
		aup->tx_dma_ring[i]->buff_stat &= ~0xf;
	}
	spin_unlock_irqrestore(&aup->lock, flags);
}


/* 
 * Setup the receive and transmit "rings".  These pointers are the addresses
 * of the rx and tx MAC DMA registers so they are fixed by the hardware --
 * these are not descriptors sitting in memory.
 */
static void 
setup_hw_rings(struct au1000_private *aup, u32 rx_base, u32 tx_base)
{
	int i;

	for (i = 0; i < NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i] = 
			(volatile rx_dma_t *) (rx_base + sizeof(rx_dma_t)*i);
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		aup->tx_dma_ring[i] = 
			(volatile tx_dma_t *) (tx_base + sizeof(tx_dma_t)*i);
	}
}

static struct {
	u32 base_addr;
	u32 macen_addr;
	int irq;
	struct net_device *dev;
} iflist[2] = {
#ifdef CONFIG_SOC_AU1000
	{AU1000_ETH0_BASE, AU1000_MAC0_ENABLE, AU1000_MAC0_DMA_INT},
	{AU1000_ETH1_BASE, AU1000_MAC1_ENABLE, AU1000_MAC1_DMA_INT}
#endif
#ifdef CONFIG_SOC_AU1100
	{AU1100_ETH0_BASE, AU1100_MAC0_ENABLE, AU1100_MAC0_DMA_INT}
#endif
#ifdef CONFIG_SOC_AU1500
	{AU1500_ETH0_BASE, AU1500_MAC0_ENABLE, AU1500_MAC0_DMA_INT},
	{AU1500_ETH1_BASE, AU1500_MAC1_ENABLE, AU1500_MAC1_DMA_INT}
#endif
#ifdef CONFIG_SOC_AU1550
	{AU1550_ETH0_BASE, AU1550_MAC0_ENABLE, AU1550_MAC0_DMA_INT},
	{AU1550_ETH1_BASE, AU1550_MAC1_ENABLE, AU1550_MAC1_DMA_INT}
#endif
};

static int num_ifs;

/*
 * Setup the base address and interupt of the Au1xxx ethernet macs
 * based on cpu type and whether the interface is enabled in sys_pinfunc
 * register. The last interface is enabled if SYS_PF_NI2 (bit 4) is 0.
 */
static int __init au1000_init_module(void)
{
	int ni = (int)((au_readl(SYS_PINFUNC) & (u32)(SYS_PF_NI2)) >> 4);
	struct net_device *dev;
	int i, found_one = 0;

	num_ifs = NUM_ETH_INTERFACES - ni;

	for(i = 0; i < num_ifs; i++) {
		dev = au1000_probe(i);
		iflist[i].dev = dev;
		if (dev)
			found_one++;
	}
	if (!found_one)
		return -ENODEV;
	return 0;
}

static int au1000_setup_aneg(struct net_device *dev, u32 advertise)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;
	u16 ctl, adv;

	/* Setup standard advertise */
	adv = mdio_read(dev, aup->phy_addr, MII_ADVERTISE);
	adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
	if (advertise & ADVERTISED_10baseT_Half)
		adv |= ADVERTISE_10HALF;
	if (advertise & ADVERTISED_10baseT_Full)
		adv |= ADVERTISE_10FULL;
	if (advertise & ADVERTISED_100baseT_Half)
		adv |= ADVERTISE_100HALF;
	if (advertise & ADVERTISED_100baseT_Full)
		adv |= ADVERTISE_100FULL;
	mdio_write(dev, aup->phy_addr, MII_ADVERTISE, adv);

	/* Start/Restart aneg */
	ctl = mdio_read(dev, aup->phy_addr, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	mdio_write(dev, aup->phy_addr, MII_BMCR, ctl);

	return 0;
}

static int au1000_setup_forced(struct net_device *dev, int speed, int fd)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;
	u16 ctl;

	ctl = mdio_read(dev, aup->phy_addr, MII_BMCR);
	ctl &= ~(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_ANENABLE);

	/* First reset the PHY */
	mdio_write(dev, aup->phy_addr, MII_BMCR, ctl | BMCR_RESET);

	/* Select speed & duplex */
	switch (speed) {
		case SPEED_10:
			break;
		case SPEED_100:
			ctl |= BMCR_SPEED100;
			break;
		case SPEED_1000:
		default:
			return -EINVAL;
	}
	if (fd == DUPLEX_FULL)
		ctl |= BMCR_FULLDPLX;
	mdio_write(dev, aup->phy_addr, MII_BMCR, ctl);

	return 0;
}


static void
au1000_start_link(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;
	u32 advertise;
	int autoneg;
	int forced_speed;
	int forced_duplex;

	/* Default advertise */
	advertise = GENMII_DEFAULT_ADVERTISE;
	autoneg = aup->want_autoneg;
	forced_speed = SPEED_100;
	forced_duplex = DUPLEX_FULL;

	/* Setup link parameters */
	if (cmd) {
		if (cmd->autoneg == AUTONEG_ENABLE) {
			advertise = cmd->advertising;
			autoneg = 1;
		} else {
			autoneg = 0;

			forced_speed = cmd->speed;
			forced_duplex = cmd->duplex;
		}
	}

	/* Configure PHY & start aneg */
	aup->want_autoneg = autoneg;
	if (autoneg)
		au1000_setup_aneg(dev, advertise);
	else
		au1000_setup_forced(dev, forced_speed, forced_duplex);
	mod_timer(&aup->timer, jiffies + HZ);
}

static int au1000_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;
	u16 link, speed;

	cmd->supported = GENMII_DEFAULT_FEATURES;
	cmd->advertising = GENMII_DEFAULT_ADVERTISE;
	cmd->port = PORT_MII;
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->phy_address = aup->phy_addr;
	spin_lock_irq(&aup->lock);
	cmd->autoneg = aup->want_autoneg;
	aup->phy_ops->phy_status(dev, aup->phy_addr, &link, &speed);
	if ((speed == IF_PORT_100BASETX) || (speed == IF_PORT_100BASEFX))
		cmd->speed = SPEED_100;
	else if (speed == IF_PORT_10BASET)
		cmd->speed = SPEED_10;
	if (link && (dev->if_port == IF_PORT_100BASEFX))
		cmd->duplex = DUPLEX_FULL;
	else
		cmd->duplex = DUPLEX_HALF;
	spin_unlock_irq(&aup->lock);
	return 0;
}

static int au1000_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	 struct au1000_private *aup = (struct au1000_private *)dev->priv;
	  unsigned long features = GENMII_DEFAULT_FEATURES;

	 if (!capable(CAP_NET_ADMIN))
		 return -EPERM;

	 if (cmd->autoneg != AUTONEG_ENABLE && cmd->autoneg != AUTONEG_DISABLE)
		 return -EINVAL;
	 if (cmd->autoneg == AUTONEG_ENABLE && cmd->advertising == 0)
		 return -EINVAL;
	 if (cmd->duplex != DUPLEX_HALF && cmd->duplex != DUPLEX_FULL)
		 return -EINVAL;
	 if (cmd->autoneg == AUTONEG_DISABLE)
		 switch (cmd->speed) {
		 case SPEED_10:
			 if (cmd->duplex == DUPLEX_HALF &&
				 (features & SUPPORTED_10baseT_Half) == 0)
				 return -EINVAL;
			 if (cmd->duplex == DUPLEX_FULL &&
				 (features & SUPPORTED_10baseT_Full) == 0)
				 return -EINVAL;
			 break;
		 case SPEED_100:
			 if (cmd->duplex == DUPLEX_HALF &&
				 (features & SUPPORTED_100baseT_Half) == 0)
				 return -EINVAL;
			 if (cmd->duplex == DUPLEX_FULL &&
				 (features & SUPPORTED_100baseT_Full) == 0)
				 return -EINVAL;
			 break;
		 default:
			 return -EINVAL;
		 }
	 else if ((features & SUPPORTED_Autoneg) == 0)
		 return -EINVAL;

	 spin_lock_irq(&aup->lock);
	 au1000_start_link(dev, cmd);
	 spin_unlock_irq(&aup->lock);
	 return 0;
}

static int au1000_nway_reset(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;

	if (!aup->want_autoneg)
		return -EINVAL;
	spin_lock_irq(&aup->lock);
	au1000_start_link(dev, NULL);
	spin_unlock_irq(&aup->lock);
	return 0;
}

static void
au1000_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	info->fw_version[0] = '\0';
	sprintf(info->bus_info, "%s %d", DRV_NAME, aup->mac_id);
	info->regdump_len = 0;
}

static u32 au1000_get_link(struct net_device *dev)
{
	return netif_carrier_ok(dev);
}

static struct ethtool_ops au1000_ethtool_ops = {
	.get_settings = au1000_get_settings,
	.set_settings = au1000_set_settings,
	.get_drvinfo = au1000_get_drvinfo,
	.nway_reset = au1000_nway_reset,
	.get_link = au1000_get_link
};

static struct net_device * au1000_probe(int port_num)
{
	static unsigned version_printed = 0;
	struct au1000_private *aup = NULL;
	struct net_device *dev = NULL;
	db_dest_t *pDB, *pDBfree;
	char *pmac, *argptr;
	char ethaddr[6];
	int irq, i, err;
	u32 base, macen;

	if (port_num >= NUM_ETH_INTERFACES)
 		return NULL;

	base  = CPHYSADDR(iflist[port_num].base_addr );
	macen = CPHYSADDR(iflist[port_num].macen_addr);
	irq = iflist[port_num].irq;

	if (!request_mem_region( base, MAC_IOSIZE, "Au1x00 ENET") ||
	    !request_mem_region(macen, 4, "Au1x00 ENET"))
		return NULL;

	if (version_printed++ == 0)
		printk("%s version %s %s\n", DRV_NAME, DRV_VERSION, DRV_AUTHOR);

	dev = alloc_etherdev(sizeof(struct au1000_private));
	if (!dev) {
		printk(KERN_ERR "%s: alloc_etherdev failed\n", DRV_NAME);
		return NULL;
	}

	if ((err = register_netdev(dev)) != 0) {
		printk(KERN_ERR "%s: Cannot register net device, error %d\n",
				DRV_NAME, err);
		free_netdev(dev);
		return NULL;
	}

	printk("%s: Au1xx0 Ethernet found at 0x%x, irq %d\n",
		dev->name, base, irq);

	aup = dev->priv;

	/* Allocate the data buffers */
	/* Snooping works fine with eth on all au1xxx */
	aup->vaddr = (u32)dma_alloc_noncoherent(NULL, MAX_BUF_SIZE *
						(NUM_TX_BUFFS + NUM_RX_BUFFS),
						&aup->dma_addr,	0);
	if (!aup->vaddr) {
		free_netdev(dev);
		release_mem_region( base, MAC_IOSIZE);
		release_mem_region(macen, 4);
		return NULL;
	}

	/* aup->mac is the base address of the MAC's registers */
	aup->mac = (volatile mac_reg_t *)iflist[port_num].base_addr;

	/* Setup some variables for quick register address access */
	aup->enable = (volatile u32 *)iflist[port_num].macen_addr;
	aup->mac_id = port_num;
	au_macs[port_num] = aup;

	if (port_num == 0) {
		/* Check the environment variables first */
		if (get_ethernet_addr(ethaddr) == 0)
			memcpy(au1000_mac_addr, ethaddr, sizeof(au1000_mac_addr));
		else {
			/* Check command line */
			argptr = prom_getcmdline();
			if ((pmac = strstr(argptr, "ethaddr=")) == NULL)
				printk(KERN_INFO "%s: No MAC address found\n",
						 dev->name);
				/* Use the hard coded MAC addresses */
			else {
				str2eaddr(ethaddr, pmac + strlen("ethaddr="));
				memcpy(au1000_mac_addr, ethaddr, 
				       sizeof(au1000_mac_addr));
			}
		}

		setup_hw_rings(aup, MAC0_RX_DMA_ADDR, MAC0_TX_DMA_ADDR);
	} else if (port_num == 1)
		setup_hw_rings(aup, MAC1_RX_DMA_ADDR, MAC1_TX_DMA_ADDR);

	/*
	 * Assign to the Ethernet ports two consecutive MAC addresses
	 * to match those that are printed on their stickers
	 */
	memcpy(dev->dev_addr, au1000_mac_addr, sizeof(au1000_mac_addr));
	dev->dev_addr[5] += port_num;

	/* Bring the device out of reset, otherwise probing the MII will hang */
	*aup->enable = MAC_EN_CLOCK_ENABLE;
	au_sync_delay(2);
	*aup->enable = MAC_EN_RESET0 | MAC_EN_RESET1 | MAC_EN_RESET2 |
		       MAC_EN_CLOCK_ENABLE;
	au_sync_delay(2);

	aup->mii = kmalloc(sizeof(struct mii_phy), GFP_KERNEL);
	if (!aup->mii) {
		printk(KERN_ERR "%s: out of memory\n", dev->name);
		goto err_out;
	}
	aup->mii->next = NULL;
	aup->mii->chip_info = NULL;
	aup->mii->status = 0;
	aup->mii->mii_control_reg = 0;
	aup->mii->mii_data_reg = 0;

	if (mii_probe(dev) != 0) {
		goto err_out;
	}

	pDBfree = NULL;
	/* setup the data buffer descriptors and attach a buffer to each one */
	pDB = aup->db;
	for (i = 0; i < (NUM_TX_BUFFS+NUM_RX_BUFFS); i++) {
		pDB->pnext = pDBfree;
		pDBfree = pDB;
		pDB->vaddr = (u32 *)((unsigned)aup->vaddr + MAX_BUF_SIZE*i);
		pDB->dma_addr = (dma_addr_t)virt_to_bus(pDB->vaddr);
		pDB++;
	}
	aup->pDBfree = pDBfree;

	for (i = 0; i < NUM_RX_DMA; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) {
			goto err_out;
		}
		aup->rx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->rx_db_inuse[i] = pDB;
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) {
			goto err_out;
		}
		aup->tx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->tx_dma_ring[i]->len = 0;
		aup->tx_db_inuse[i] = pDB;
	}

	spin_lock_init(&aup->lock);
	dev->base_addr = base;
	dev->irq = irq;
	dev->open = au1000_open;
	dev->hard_start_xmit = au1000_tx;
	dev->stop = au1000_close;
	dev->get_stats = au1000_get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &au1000_ioctl;
	SET_ETHTOOL_OPS(dev, &au1000_ethtool_ops);
	dev->set_config = &au1000_set_config;
	dev->tx_timeout = au1000_tx_timeout;
	dev->watchdog_timeo = ETH_TX_TIMEOUT;

	/* 
	 * The boot code uses the ethernet controller, so reset it to start 
	 * fresh.  au1000_init() expects that the device is in reset state.
	 */
	reset_mac(dev);

	return dev;

err_out:
	/* here we should have a valid dev plus aup-> register addresses
	 * so we can reset the mac properly.*/
	reset_mac(dev);
	kfree(aup->mii);
	for (i = 0; i < NUM_RX_DMA; i++) {
		if (aup->rx_db_inuse[i])
			ReleaseDB(aup, aup->rx_db_inuse[i]);
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		if (aup->tx_db_inuse[i])
			ReleaseDB(aup, aup->tx_db_inuse[i]);
	}
	dma_free_noncoherent(NULL, MAX_BUF_SIZE * (NUM_TX_BUFFS + NUM_RX_BUFFS),
			     (void *)aup->vaddr, aup->dma_addr);
	unregister_netdev(dev);
	free_netdev(dev);
	release_mem_region( base, MAC_IOSIZE);
	release_mem_region(macen, 4);
	return NULL;
}

/* 
 * Initialize the interface.
 *
 * When the device powers up, the clocks are disabled and the
 * mac is in reset state.  When the interface is closed, we
 * do the same -- reset the device and disable the clocks to
 * conserve power. Thus, whenever au1000_init() is called,
 * the device should already be in reset state.
 */
static int au1000_init(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u32 flags;
	int i;
	u32 control;
	u16 link, speed;

	if (au1000_debug > 4) 
		printk("%s: au1000_init\n", dev->name);

	spin_lock_irqsave(&aup->lock, flags);

	/* bring the device out of reset */
	*aup->enable = MAC_EN_CLOCK_ENABLE;
        au_sync_delay(2);
	*aup->enable = MAC_EN_RESET0 | MAC_EN_RESET1 | 
		MAC_EN_RESET2 | MAC_EN_CLOCK_ENABLE;
	au_sync_delay(20);

	aup->mac->control = 0;
	aup->tx_head = (aup->tx_dma_ring[0]->buff_stat & 0xC) >> 2;
	aup->tx_tail = aup->tx_head;
	aup->rx_head = (aup->rx_dma_ring[0]->buff_stat & 0xC) >> 2;

	aup->mac->mac_addr_high = dev->dev_addr[5]<<8 | dev->dev_addr[4];
	aup->mac->mac_addr_low = dev->dev_addr[3]<<24 | dev->dev_addr[2]<<16 |
		dev->dev_addr[1]<<8 | dev->dev_addr[0];

	for (i = 0; i < NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i]->buff_stat |= RX_DMA_ENABLE;
	}
	au_sync();

	aup->phy_ops->phy_status(dev, aup->phy_addr, &link, &speed);
	control = MAC_DISABLE_RX_OWN | MAC_RX_ENABLE | MAC_TX_ENABLE;
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	control |= MAC_BIG_ENDIAN;
#endif
	if (link && (dev->if_port == IF_PORT_100BASEFX)) {
		control |= MAC_FULL_DUPLEX;
	}

	aup->mac->control = control;
	aup->mac->vlan1_tag = 0x8100; /* activate vlan support */
	au_sync();

	spin_unlock_irqrestore(&aup->lock, flags);
	return 0;
}

static void au1000_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	unsigned char if_port;
	u16 link, speed;

	if (!dev) {
		/* fatal error, don't restart the timer */
		printk(KERN_ERR "au1000_timer error: NULL dev\n");
		return;
	}

	if_port = dev->if_port;
	if (aup->phy_ops->phy_status(dev, aup->phy_addr, &link, &speed) == 0) {
		if (link) {
			if (!netif_carrier_ok(dev)) {
				netif_carrier_on(dev);
				printk(KERN_INFO "%s: link up\n", dev->name);
			}
		}
		else {
			if (netif_carrier_ok(dev)) {
				netif_carrier_off(dev);
				dev->if_port = 0;
				printk(KERN_INFO "%s: link down\n", dev->name);
			}
		}
	}

	if (link && (dev->if_port != if_port) && 
			(dev->if_port != IF_PORT_UNKNOWN)) {
		hard_stop(dev);
		if (dev->if_port == IF_PORT_100BASEFX) {
			printk(KERN_INFO "%s: going to full duplex\n", 
					dev->name);
			aup->mac->control |= MAC_FULL_DUPLEX;
			au_sync_delay(1);
		}
		else {
			aup->mac->control &= ~MAC_FULL_DUPLEX;
			au_sync_delay(1);
		}
		enable_rx_tx(dev);
	}

	aup->timer.expires = RUN_AT((1*HZ)); 
	aup->timer.data = (unsigned long)dev;
	aup->timer.function = &au1000_timer; /* timer handler */
	add_timer(&aup->timer);

}

static int au1000_open(struct net_device *dev)
{
	int retval;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk("%s: open: dev=%p\n", dev->name, dev);

	if ((retval = au1000_init(dev))) {
		printk(KERN_ERR "%s: error in au1000_init\n", dev->name);
		free_irq(dev->irq, dev);
		return retval;
	}
	netif_start_queue(dev);

	if ((retval = request_irq(dev->irq, &au1000_interrupt, 0, 
					dev->name, dev))) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n", 
				dev->name, dev->irq);
		return retval;
	}

	init_timer(&aup->timer); /* used in ioctl() */
	aup->timer.expires = RUN_AT((3*HZ)); 
	aup->timer.data = (unsigned long)dev;
	aup->timer.function = &au1000_timer; /* timer handler */
	add_timer(&aup->timer);

	if (au1000_debug > 4)
		printk("%s: open: Initialization done.\n", dev->name);

	return 0;
}

static int au1000_close(struct net_device *dev)
{
	u32 flags;
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk("%s: close: dev=%p\n", dev->name, dev);

	reset_mac(dev);

	spin_lock_irqsave(&aup->lock, flags);
	
	/* stop the device */
	netif_stop_queue(dev);

	/* disable the interrupt */
	free_irq(dev->irq, dev);
	spin_unlock_irqrestore(&aup->lock, flags);

	return 0;
}

static void __exit au1000_cleanup_module(void)
{
	int i, j;
	struct net_device *dev;
	struct au1000_private *aup;

	for (i = 0; i < num_ifs; i++) {
		dev = iflist[i].dev;
		if (dev) {
			aup = (struct au1000_private *) dev->priv;
			unregister_netdev(dev);
			kfree(aup->mii);
			for (j = 0; j < NUM_RX_DMA; j++)
				if (aup->rx_db_inuse[j])
					ReleaseDB(aup, aup->rx_db_inuse[j]);
			for (j = 0; j < NUM_TX_DMA; j++)
				if (aup->tx_db_inuse[j])
					ReleaseDB(aup, aup->tx_db_inuse[j]);
 			dma_free_noncoherent(NULL, MAX_BUF_SIZE *
 					     (NUM_TX_BUFFS + NUM_RX_BUFFS),
 					     (void *)aup->vaddr, aup->dma_addr);
 			release_mem_region(dev->base_addr, MAC_IOSIZE);
 			release_mem_region(CPHYSADDR(iflist[i].macen_addr), 4);
			free_netdev(dev);
		}
	}
}

static void update_tx_stats(struct net_device *dev, u32 status)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct net_device_stats *ps = &aup->stats;

	if (status & TX_FRAME_ABORTED) {
		if (dev->if_port == IF_PORT_100BASEFX) {
			if (status & (TX_JAB_TIMEOUT | TX_UNDERRUN)) {
				/* any other tx errors are only valid
				 * in half duplex mode */
				ps->tx_errors++;
				ps->tx_aborted_errors++;
			}
		}
		else {
			ps->tx_errors++;
			ps->tx_aborted_errors++;
			if (status & (TX_NO_CARRIER | TX_LOSS_CARRIER))
				ps->tx_carrier_errors++;
		}
	}
}


/*
 * Called from the interrupt service routine to acknowledge
 * the TX DONE bits.  This is a must if the irq is setup as
 * edge triggered.
 */
static void au1000_tx_ack(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	volatile tx_dma_t *ptxd;

	ptxd = aup->tx_dma_ring[aup->tx_tail];

	while (ptxd->buff_stat & TX_T_DONE) {
		update_tx_stats(dev, ptxd->status);
		ptxd->buff_stat &= ~TX_T_DONE;
		ptxd->len = 0;
		au_sync();

		aup->tx_tail = (aup->tx_tail + 1) & (NUM_TX_DMA - 1);
		ptxd = aup->tx_dma_ring[aup->tx_tail];

		if (aup->tx_full) {
			aup->tx_full = 0;
			netif_wake_queue(dev);
		}
	}
}


/*
 * Au1000 transmit routine.
 */
static int au1000_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct net_device_stats *ps = &aup->stats;
	volatile tx_dma_t *ptxd;
	u32 buff_stat;
	db_dest_t *pDB;
	int i;

	if (au1000_debug > 5)
		printk("%s: tx: aup %x len=%d, data=%p, head %d\n", 
				dev->name, (unsigned)aup, skb->len, 
				skb->data, aup->tx_head);

	ptxd = aup->tx_dma_ring[aup->tx_head];
	buff_stat = ptxd->buff_stat;
	if (buff_stat & TX_DMA_ENABLE) {
		/* We've wrapped around and the transmitter is still busy */
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return 1;
	}
	else if (buff_stat & TX_T_DONE) {
		update_tx_stats(dev, ptxd->status);
		ptxd->len = 0;
	}

	if (aup->tx_full) {
		aup->tx_full = 0;
		netif_wake_queue(dev);
	}

	pDB = aup->tx_db_inuse[aup->tx_head];
	memcpy((void *)pDB->vaddr, skb->data, skb->len);
	if (skb->len < ETH_ZLEN) {
		for (i=skb->len; i<ETH_ZLEN; i++) { 
			((char *)pDB->vaddr)[i] = 0;
		}
		ptxd->len = ETH_ZLEN;
	}
	else
		ptxd->len = skb->len;

	ps->tx_packets++;
	ps->tx_bytes += ptxd->len;

	ptxd->buff_stat = pDB->dma_addr | TX_DMA_ENABLE;
	au_sync();
	dev_kfree_skb(skb);
	aup->tx_head = (aup->tx_head + 1) & (NUM_TX_DMA - 1);
	dev->trans_start = jiffies;
	return 0;
}

static inline void update_rx_stats(struct net_device *dev, u32 status)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct net_device_stats *ps = &aup->stats;

	ps->rx_packets++;
	if (status & RX_MCAST_FRAME)
		ps->multicast++;

	if (status & RX_ERROR) {
		ps->rx_errors++;
		if (status & RX_MISSED_FRAME)
			ps->rx_missed_errors++;
		if (status & (RX_OVERLEN | RX_OVERLEN | RX_LEN_ERROR))
			ps->rx_length_errors++;
		if (status & RX_CRC_ERROR)
			ps->rx_crc_errors++;
		if (status & RX_COLL)
			ps->collisions++;
	}
	else 
		ps->rx_bytes += status & RX_FRAME_LEN_MASK;

}

/*
 * Au1000 receive routine.
 */
static int au1000_rx(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	struct sk_buff *skb;
	volatile rx_dma_t *prxd;
	u32 buff_stat, status;
	db_dest_t *pDB;
	u32	frmlen;

	if (au1000_debug > 5)
		printk("%s: au1000_rx head %d\n", dev->name, aup->rx_head);

	prxd = aup->rx_dma_ring[aup->rx_head];
	buff_stat = prxd->buff_stat;
	while (buff_stat & RX_T_DONE)  {
		status = prxd->status;
		pDB = aup->rx_db_inuse[aup->rx_head];
		update_rx_stats(dev, status);
		if (!(status & RX_ERROR))  {

			/* good frame */
			frmlen = (status & RX_FRAME_LEN_MASK);
			frmlen -= 4; /* Remove FCS */
			skb = dev_alloc_skb(frmlen + 2);
			if (skb == NULL) {
				printk(KERN_ERR
				       "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				aup->stats.rx_dropped++;
				continue;
			}
			skb->dev = dev;
			skb_reserve(skb, 2);	/* 16 byte IP header align */
			eth_copy_and_sum(skb,
				(unsigned char *)pDB->vaddr, frmlen, 0);
			skb_put(skb, frmlen);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);	/* pass the packet to upper layers */
		}
		else {
			if (au1000_debug > 4) {
				if (status & RX_MISSED_FRAME) 
					printk("rx miss\n");
				if (status & RX_WDOG_TIMER) 
					printk("rx wdog\n");
				if (status & RX_RUNT) 
					printk("rx runt\n");
				if (status & RX_OVERLEN) 
					printk("rx overlen\n");
				if (status & RX_COLL)
					printk("rx coll\n");
				if (status & RX_MII_ERROR)
					printk("rx mii error\n");
				if (status & RX_CRC_ERROR)
					printk("rx crc error\n");
				if (status & RX_LEN_ERROR)
					printk("rx len error\n");
				if (status & RX_U_CNTRL_FRAME)
					printk("rx u control frame\n");
				if (status & RX_MISSED_FRAME)
					printk("rx miss\n");
			}
		}
		prxd->buff_stat = (u32)(pDB->dma_addr | RX_DMA_ENABLE);
		aup->rx_head = (aup->rx_head + 1) & (NUM_RX_DMA - 1);
		au_sync();

		/* next descriptor */
		prxd = aup->rx_dma_ring[aup->rx_head];
		buff_stat = prxd->buff_stat;
		dev->last_rx = jiffies;
	}
	return 0;
}


/*
 * Au1000 interrupt service routine.
 */
static irqreturn_t au1000_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;

	if (dev == NULL) {
		printk(KERN_ERR "%s: isr: null dev ptr\n", dev->name);
		return IRQ_RETVAL(1);
	}

	/* Handle RX interrupts first to minimize chance of overrun */

	au1000_rx(dev);
	au1000_tx_ack(dev);
	return IRQ_RETVAL(1);
}


/*
 * The Tx ring has been full longer than the watchdog timeout
 * value. The transmitter must be hung?
 */
static void au1000_tx_timeout(struct net_device *dev)
{
	printk(KERN_ERR "%s: au1000_tx_timeout: dev=%p\n", dev->name, dev);
	reset_mac(dev);
	au1000_init(dev);
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static void set_rx_mode(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4) 
		printk("%s: set_rx_mode: flags=%x\n", dev->name, dev->flags);

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		aup->mac->control |= MAC_PROMISCUOUS;
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
	} else if ((dev->flags & IFF_ALLMULTI)  ||
			   dev->mc_count > MULTICAST_FILTER_LIMIT) {
		aup->mac->control |= MAC_PASS_ALL_MULTI;
		aup->mac->control &= ~MAC_PROMISCUOUS;
		printk(KERN_INFO "%s: Pass all multicast\n", dev->name);
	} else {
		int i;
		struct dev_mc_list *mclist;
		u32 mc_filter[2];	/* Multicast hash filter */

		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr)>>26, 
					(long *)mc_filter);
		}
		aup->mac->multi_hash_high = mc_filter[1];
		aup->mac->multi_hash_low = mc_filter[0];
		aup->mac->control &= ~MAC_PROMISCUOUS;
		aup->mac->control |= MAC_HASH_MODE;
	}
}


static int au1000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct au1000_private *aup = (struct au1000_private *)dev->priv;
	u16 *data = (u16 *)&rq->ifr_ifru;

	switch(cmd) { 
		case SIOCDEVPRIVATE:	/* Get the address of the PHY in use. */
		case SIOCGMIIPHY:
		        if (!netif_running(dev)) return -EINVAL;
			data[0] = aup->phy_addr;
		case SIOCDEVPRIVATE+1:	/* Read the specified MII register. */
		case SIOCGMIIREG:
			data[3] =  mdio_read(dev, data[0], data[1]); 
			return 0;
		case SIOCDEVPRIVATE+2:	/* Write the specified MII register */
		case SIOCSMIIREG: 
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			mdio_write(dev, data[0], data[1],data[2]);
			return 0;
		default:
			return -EOPNOTSUPP;
	}

}


static int au1000_set_config(struct net_device *dev, struct ifmap *map)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;
	u16 control;

	if (au1000_debug > 4)  {
		printk("%s: set_config called: dev->if_port %d map->port %x\n", 
				dev->name, dev->if_port, map->port);
	}

	switch(map->port){
		case IF_PORT_UNKNOWN: /* use auto here */   
			printk(KERN_INFO "%s: config phy for aneg\n", 
					dev->name);
			dev->if_port = map->port;
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);
	
			/* read current control */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			control &= ~(MII_CNTL_FDX | MII_CNTL_F100);

			/* enable auto negotiation and reset the negotiation */
			mdio_write(dev, aup->phy_addr, MII_CONTROL, 
					control | MII_CNTL_AUTO | 
					MII_CNTL_RST_AUTO);

			break;
    
		case IF_PORT_10BASET: /* 10BaseT */         
			printk(KERN_INFO "%s: config phy for 10BaseT\n", 
					dev->name);
			dev->if_port = map->port;
	
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);

			/* set Speed to 10Mbps, Half Duplex */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			control &= ~(MII_CNTL_F100 | MII_CNTL_AUTO | 
					MII_CNTL_FDX);
	
			/* disable auto negotiation and force 10M/HD mode*/
			mdio_write(dev, aup->phy_addr, MII_CONTROL, control);
			break;
    
		case IF_PORT_100BASET: /* 100BaseT */
		case IF_PORT_100BASETX: /* 100BaseTx */ 
			printk(KERN_INFO "%s: config phy for 100BaseTX\n", 
					dev->name);
			dev->if_port = map->port;
	
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);
	
			/* set Speed to 100Mbps, Half Duplex */
			/* disable auto negotiation and enable 100MBit Mode */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			control &= ~(MII_CNTL_AUTO | MII_CNTL_FDX);
			control |= MII_CNTL_F100;
			mdio_write(dev, aup->phy_addr, MII_CONTROL, control);
			break;
    
		case IF_PORT_100BASEFX: /* 100BaseFx */
			printk(KERN_INFO "%s: config phy for 100BaseFX\n", 
					dev->name);
			dev->if_port = map->port;
	
			/* Link Down: the timer will bring it up */
			netif_carrier_off(dev);
	
			/* set Speed to 100Mbps, Full Duplex */
			/* disable auto negotiation and enable 100MBit Mode */
			control = mdio_read(dev, aup->phy_addr, MII_CONTROL);
			control &= ~MII_CNTL_AUTO;
			control |=  MII_CNTL_F100 | MII_CNTL_FDX;
			mdio_write(dev, aup->phy_addr, MII_CONTROL, control);
			break;
		case IF_PORT_10BASE2: /* 10Base2 */
		case IF_PORT_AUI: /* AUI */
		/* These Modes are not supported (are they?)*/
			printk(KERN_ERR "%s: 10Base2/AUI not supported", 
					dev->name);
			return -EOPNOTSUPP;
			break;
    
		default:
			printk(KERN_ERR "%s: Invalid media selected", 
					dev->name);
			return -EINVAL;
	}
	return 0;
}

static struct net_device_stats *au1000_get_stats(struct net_device *dev)
{
	struct au1000_private *aup = (struct au1000_private *) dev->priv;

	if (au1000_debug > 4)
		printk("%s: au1000_get_stats: dev=%p\n", dev->name, dev);

	if (netif_device_present(dev)) {
		return &aup->stats;
	}
	return 0;
}

module_init(au1000_init_module);
module_exit(au1000_cleanup_module);

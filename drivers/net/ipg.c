/*
 * ipg.c: Device Driver for the IP1000 Gigabit Ethernet Adapter
 *
 * Copyright (C) 2003, 2007  IC Plus Corp
 *
 * Original Author:
 *
 *   Craig Rich
 *   Sundance Technology, Inc.
 *   www.sundanceti.com
 *   craig_rich@sundanceti.com
 *
 * Current Maintainer:
 *
 *   Sorbica Shieh.
 *   http://www.icplus.com.tw
 *   sorbica@icplus.com.tw
 *
 *   Jesse Huang
 *   http://www.icplus.com.tw
 *   jesse@icplus.com.tw
 */
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/mutex.h>

#include <asm/div64.h>

#define IPG_RX_RING_BYTES	(sizeof(struct ipg_rx) * IPG_RFDLIST_LENGTH)
#define IPG_TX_RING_BYTES	(sizeof(struct ipg_tx) * IPG_TFDLIST_LENGTH)
#define IPG_RESET_MASK \
	(IPG_AC_GLOBAL_RESET | IPG_AC_RX_RESET | IPG_AC_TX_RESET | \
	 IPG_AC_DMA | IPG_AC_FIFO | IPG_AC_NETWORK | IPG_AC_HOST | \
	 IPG_AC_AUTO_INIT)

#define ipg_w32(val32, reg)	iowrite32((val32), ioaddr + (reg))
#define ipg_w16(val16, reg)	iowrite16((val16), ioaddr + (reg))
#define ipg_w8(val8, reg)	iowrite8((val8), ioaddr + (reg))

#define ipg_r32(reg)		ioread32(ioaddr + (reg))
#define ipg_r16(reg)		ioread16(ioaddr + (reg))
#define ipg_r8(reg)		ioread8(ioaddr + (reg))

enum {
	netdev_io_size = 128
};

#include "ipg.h"
#define DRV_NAME	"ipg"

MODULE_AUTHOR("IC Plus Corp. 2003");
MODULE_DESCRIPTION("IC Plus IP1000 Gigabit Ethernet Adapter Linux Driver");
MODULE_LICENSE("GPL");

/*
 * Defaults
 */
#define IPG_MAX_RXFRAME_SIZE	0x0600
#define IPG_RXFRAG_SIZE		0x0600
#define IPG_RXSUPPORT_SIZE	0x0600
#define IPG_IS_JUMBO		false

/*
 * Variable record -- index by leading revision/length
 * Revision/Length(=N*4), Address1, Data1, Address2, Data2,...,AddressN,DataN
 */
static unsigned short DefaultPhyParam[] = {
	/* 11/12/03 IP1000A v1-3 rev=0x40 */
	/*--------------------------------------------------------------------------
	(0x4000|(15*4)), 31, 0x0001, 27, 0x01e0, 31, 0x0002, 22, 0x85bd, 24, 0xfff2,
				 27, 0x0c10, 28, 0x0c10, 29, 0x2c10, 31, 0x0003, 23, 0x92f6,
				 31, 0x0000, 23, 0x003d, 30, 0x00de, 20, 0x20e7,  9, 0x0700,
	  --------------------------------------------------------------------------*/
	/* 12/17/03 IP1000A v1-4 rev=0x40 */
	(0x4000 | (07 * 4)), 31, 0x0001, 27, 0x01e0, 31, 0x0002, 27, 0xeb8e, 31,
	    0x0000,
	30, 0x005e, 9, 0x0700,
	/* 01/09/04 IP1000A v1-5 rev=0x41 */
	(0x4100 | (07 * 4)), 31, 0x0001, 27, 0x01e0, 31, 0x0002, 27, 0xeb8e, 31,
	    0x0000,
	30, 0x005e, 9, 0x0700,
	0x0000
};

static const char *ipg_brand_name[] = {
	"IC PLUS IP1000 1000/100/10 based NIC",
	"Sundance Technology ST2021 based NIC",
	"Tamarack Microelectronics TC9020/9021 based NIC",
	"Tamarack Microelectronics TC9020/9021 based NIC",
	"D-Link NIC",
	"D-Link NIC IP1000A"
};

static struct pci_device_id ipg_pci_tbl[] __devinitdata = {
	{ PCI_VDEVICE(SUNDANCE,	0x1023), 0 },
	{ PCI_VDEVICE(SUNDANCE,	0x2021), 1 },
	{ PCI_VDEVICE(SUNDANCE,	0x1021), 2 },
	{ PCI_VDEVICE(DLINK,	0x9021), 3 },
	{ PCI_VDEVICE(DLINK,	0x4000), 4 },
	{ PCI_VDEVICE(DLINK,	0x4020), 5 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, ipg_pci_tbl);

static inline void __iomem *ipg_ioaddr(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	return sp->ioaddr;
}

#ifdef IPG_DEBUG
static void ipg_dump_rfdlist(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;
	u32 offset;

	IPG_DEBUG_MSG("_dump_rfdlist\n");

	printk(KERN_INFO "rx_current = %2.2x\n", sp->rx_current);
	printk(KERN_INFO "rx_dirty   = %2.2x\n", sp->rx_dirty);
	printk(KERN_INFO "RFDList start address = %16.16lx\n",
	       (unsigned long) sp->rxd_map);
	printk(KERN_INFO "RFDListPtr register   = %8.8x%8.8x\n",
	       ipg_r32(IPG_RFDLISTPTR1), ipg_r32(IPG_RFDLISTPTR0));

	for (i = 0; i < IPG_RFDLIST_LENGTH; i++) {
		offset = (u32) &sp->rxd[i].next_desc - (u32) sp->rxd;
		printk(KERN_INFO "%2.2x %4.4x RFDNextPtr = %16.16lx\n", i,
		       offset, (unsigned long) sp->rxd[i].next_desc);
		offset = (u32) &sp->rxd[i].rfs - (u32) sp->rxd;
		printk(KERN_INFO "%2.2x %4.4x RFS        = %16.16lx\n", i,
		       offset, (unsigned long) sp->rxd[i].rfs);
		offset = (u32) &sp->rxd[i].frag_info - (u32) sp->rxd;
		printk(KERN_INFO "%2.2x %4.4x frag_info   = %16.16lx\n", i,
		       offset, (unsigned long) sp->rxd[i].frag_info);
	}
}

static void ipg_dump_tfdlist(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;
	u32 offset;

	IPG_DEBUG_MSG("_dump_tfdlist\n");

	printk(KERN_INFO "tx_current         = %2.2x\n", sp->tx_current);
	printk(KERN_INFO "tx_dirty = %2.2x\n", sp->tx_dirty);
	printk(KERN_INFO "TFDList start address = %16.16lx\n",
	       (unsigned long) sp->txd_map);
	printk(KERN_INFO "TFDListPtr register   = %8.8x%8.8x\n",
	       ipg_r32(IPG_TFDLISTPTR1), ipg_r32(IPG_TFDLISTPTR0));

	for (i = 0; i < IPG_TFDLIST_LENGTH; i++) {
		offset = (u32) &sp->txd[i].next_desc - (u32) sp->txd;
		printk(KERN_INFO "%2.2x %4.4x TFDNextPtr = %16.16lx\n", i,
		       offset, (unsigned long) sp->txd[i].next_desc);

		offset = (u32) &sp->txd[i].tfc - (u32) sp->txd;
		printk(KERN_INFO "%2.2x %4.4x TFC        = %16.16lx\n", i,
		       offset, (unsigned long) sp->txd[i].tfc);
		offset = (u32) &sp->txd[i].frag_info - (u32) sp->txd;
		printk(KERN_INFO "%2.2x %4.4x frag_info   = %16.16lx\n", i,
		       offset, (unsigned long) sp->txd[i].frag_info);
	}
}
#endif

static void ipg_write_phy_ctl(void __iomem *ioaddr, u8 data)
{
	ipg_w8(IPG_PC_RSVD_MASK & data, PHY_CTRL);
	ndelay(IPG_PC_PHYCTRLWAIT_NS);
}

static void ipg_drive_phy_ctl_low_high(void __iomem *ioaddr, u8 data)
{
	ipg_write_phy_ctl(ioaddr, IPG_PC_MGMTCLK_LO | data);
	ipg_write_phy_ctl(ioaddr, IPG_PC_MGMTCLK_HI | data);
}

static void send_three_state(void __iomem *ioaddr, u8 phyctrlpolarity)
{
	phyctrlpolarity |= (IPG_PC_MGMTDATA & 0) | IPG_PC_MGMTDIR;

	ipg_drive_phy_ctl_low_high(ioaddr, phyctrlpolarity);
}

static void send_end(void __iomem *ioaddr, u8 phyctrlpolarity)
{
	ipg_w8((IPG_PC_MGMTCLK_LO | (IPG_PC_MGMTDATA & 0) | IPG_PC_MGMTDIR |
		phyctrlpolarity) & IPG_PC_RSVD_MASK, PHY_CTRL);
}

static u16 read_phy_bit(void __iomem *ioaddr, u8 phyctrlpolarity)
{
	u16 bit_data;

	ipg_write_phy_ctl(ioaddr, IPG_PC_MGMTCLK_LO | phyctrlpolarity);

	bit_data = ((ipg_r8(PHY_CTRL) & IPG_PC_MGMTDATA) >> 1) & 1;

	ipg_write_phy_ctl(ioaddr, IPG_PC_MGMTCLK_HI | phyctrlpolarity);

	return bit_data;
}

/*
 * Read a register from the Physical Layer device located
 * on the IPG NIC, using the IPG PHYCTRL register.
 */
static int mdio_read(struct net_device *dev, int phy_id, int phy_reg)
{
	void __iomem *ioaddr = ipg_ioaddr(dev);
	/*
	 * The GMII mangement frame structure for a read is as follows:
	 *
	 * |Preamble|st|op|phyad|regad|ta|      data      |idle|
	 * |< 32 1s>|01|10|AAAAA|RRRRR|z0|DDDDDDDDDDDDDDDD|z   |
	 *
	 * <32 1s> = 32 consecutive logic 1 values
	 * A = bit of Physical Layer device address (MSB first)
	 * R = bit of register address (MSB first)
	 * z = High impedance state
	 * D = bit of read data (MSB first)
	 *
	 * Transmission order is 'Preamble' field first, bits transmitted
	 * left to right (first to last).
	 */
	struct {
		u32 field;
		unsigned int len;
	} p[] = {
		{ GMII_PREAMBLE,	32 },	/* Preamble */
		{ GMII_ST,		2  },	/* ST */
		{ GMII_READ,		2  },	/* OP */
		{ phy_id,		5  },	/* PHYAD */
		{ phy_reg,		5  },	/* REGAD */
		{ 0x0000,		2  },	/* TA */
		{ 0x0000,		16 },	/* DATA */
		{ 0x0000,		1  }	/* IDLE */
	};
	unsigned int i, j;
	u8 polarity, data;

	polarity  = ipg_r8(PHY_CTRL);
	polarity &= (IPG_PC_DUPLEX_POLARITY | IPG_PC_LINK_POLARITY);

	/* Create the Preamble, ST, OP, PHYAD, and REGAD field. */
	for (j = 0; j < 5; j++) {
		for (i = 0; i < p[j].len; i++) {
			/* For each variable length field, the MSB must be
			 * transmitted first. Rotate through the field bits,
			 * starting with the MSB, and move each bit into the
			 * the 1st (2^1) bit position (this is the bit position
			 * corresponding to the MgmtData bit of the PhyCtrl
			 * register for the IPG).
			 *
			 * Example: ST = 01;
			 *
			 *          First write a '0' to bit 1 of the PhyCtrl
			 *          register, then write a '1' to bit 1 of the
			 *          PhyCtrl register.
			 *
			 * To do this, right shift the MSB of ST by the value:
			 * [field length - 1 - #ST bits already written]
			 * then left shift this result by 1.
			 */
			data  = (p[j].field >> (p[j].len - 1 - i)) << 1;
			data &= IPG_PC_MGMTDATA;
			data |= polarity | IPG_PC_MGMTDIR;

			ipg_drive_phy_ctl_low_high(ioaddr, data);
		}
	}

	send_three_state(ioaddr, polarity);

	read_phy_bit(ioaddr, polarity);

	/*
	 * For a read cycle, the bits for the next two fields (TA and
	 * DATA) are driven by the PHY (the IPG reads these bits).
	 */
	for (i = 0; i < p[6].len; i++) {
		p[6].field |=
		    (read_phy_bit(ioaddr, polarity) << (p[6].len - 1 - i));
	}

	send_three_state(ioaddr, polarity);
	send_three_state(ioaddr, polarity);
	send_three_state(ioaddr, polarity);
	send_end(ioaddr, polarity);

	/* Return the value of the DATA field. */
	return p[6].field;
}

/*
 * Write to a register from the Physical Layer device located
 * on the IPG NIC, using the IPG PHYCTRL register.
 */
static void mdio_write(struct net_device *dev, int phy_id, int phy_reg, int val)
{
	void __iomem *ioaddr = ipg_ioaddr(dev);
	/*
	 * The GMII mangement frame structure for a read is as follows:
	 *
	 * |Preamble|st|op|phyad|regad|ta|      data      |idle|
	 * |< 32 1s>|01|10|AAAAA|RRRRR|z0|DDDDDDDDDDDDDDDD|z   |
	 *
	 * <32 1s> = 32 consecutive logic 1 values
	 * A = bit of Physical Layer device address (MSB first)
	 * R = bit of register address (MSB first)
	 * z = High impedance state
	 * D = bit of write data (MSB first)
	 *
	 * Transmission order is 'Preamble' field first, bits transmitted
	 * left to right (first to last).
	 */
	struct {
		u32 field;
		unsigned int len;
	} p[] = {
		{ GMII_PREAMBLE,	32 },	/* Preamble */
		{ GMII_ST,		2  },	/* ST */
		{ GMII_WRITE,		2  },	/* OP */
		{ phy_id,		5  },	/* PHYAD */
		{ phy_reg,		5  },	/* REGAD */
		{ 0x0002,		2  },	/* TA */
		{ val & 0xffff,		16 },	/* DATA */
		{ 0x0000,		1  }	/* IDLE */
	};
	unsigned int i, j;
	u8 polarity, data;

	polarity  = ipg_r8(PHY_CTRL);
	polarity &= (IPG_PC_DUPLEX_POLARITY | IPG_PC_LINK_POLARITY);

	/* Create the Preamble, ST, OP, PHYAD, and REGAD field. */
	for (j = 0; j < 7; j++) {
		for (i = 0; i < p[j].len; i++) {
			/* For each variable length field, the MSB must be
			 * transmitted first. Rotate through the field bits,
			 * starting with the MSB, and move each bit into the
			 * the 1st (2^1) bit position (this is the bit position
			 * corresponding to the MgmtData bit of the PhyCtrl
			 * register for the IPG).
			 *
			 * Example: ST = 01;
			 *
			 *          First write a '0' to bit 1 of the PhyCtrl
			 *          register, then write a '1' to bit 1 of the
			 *          PhyCtrl register.
			 *
			 * To do this, right shift the MSB of ST by the value:
			 * [field length - 1 - #ST bits already written]
			 * then left shift this result by 1.
			 */
			data  = (p[j].field >> (p[j].len - 1 - i)) << 1;
			data &= IPG_PC_MGMTDATA;
			data |= polarity | IPG_PC_MGMTDIR;

			ipg_drive_phy_ctl_low_high(ioaddr, data);
		}
	}

	/* The last cycle is a tri-state, so read from the PHY. */
	for (j = 7; j < 8; j++) {
		for (i = 0; i < p[j].len; i++) {
			ipg_write_phy_ctl(ioaddr, IPG_PC_MGMTCLK_LO | polarity);

			p[j].field |= ((ipg_r8(PHY_CTRL) &
				IPG_PC_MGMTDATA) >> 1) << (p[j].len - 1 - i);

			ipg_write_phy_ctl(ioaddr, IPG_PC_MGMTCLK_HI | polarity);
		}
	}
}

static void ipg_set_led_mode(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	u32 mode;

	mode = ipg_r32(ASIC_CTRL);
	mode &= ~(IPG_AC_LED_MODE_BIT_1 | IPG_AC_LED_MODE | IPG_AC_LED_SPEED);

	if ((sp->led_mode & 0x03) > 1)
		mode |= IPG_AC_LED_MODE_BIT_1;	/* Write Asic Control Bit 29 */

	if ((sp->led_mode & 0x01) == 1)
		mode |= IPG_AC_LED_MODE;	/* Write Asic Control Bit 14 */

	if ((sp->led_mode & 0x08) == 8)
		mode |= IPG_AC_LED_SPEED;	/* Write Asic Control Bit 27 */

	ipg_w32(mode, ASIC_CTRL);
}

static void ipg_set_phy_set(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	int physet;

	physet = ipg_r8(PHY_SET);
	physet &= ~(IPG_PS_MEM_LENB9B | IPG_PS_MEM_LEN9 | IPG_PS_NON_COMPDET);
	physet |= ((sp->led_mode & 0x70) >> 4);
	ipg_w8(physet, PHY_SET);
}

static int ipg_reset(struct net_device *dev, u32 resetflags)
{
	/* Assert functional resets via the IPG AsicCtrl
	 * register as specified by the 'resetflags' input
	 * parameter.
	 */
	void __iomem *ioaddr = ipg_ioaddr(dev);
	unsigned int timeout_count = 0;

	IPG_DEBUG_MSG("_reset\n");

	ipg_w32(ipg_r32(ASIC_CTRL) | resetflags, ASIC_CTRL);

	/* Delay added to account for problem with 10Mbps reset. */
	mdelay(IPG_AC_RESETWAIT);

	while (IPG_AC_RESET_BUSY & ipg_r32(ASIC_CTRL)) {
		mdelay(IPG_AC_RESETWAIT);
		if (++timeout_count > IPG_AC_RESET_TIMEOUT)
			return -ETIME;
	}
	/* Set LED Mode in Asic Control */
	ipg_set_led_mode(dev);

	/* Set PHYSet Register Value */
	ipg_set_phy_set(dev);
	return 0;
}

/* Find the GMII PHY address. */
static int ipg_find_phyaddr(struct net_device *dev)
{
	unsigned int phyaddr, i;

	for (i = 0; i < 32; i++) {
		u32 status;

		/* Search for the correct PHY address among 32 possible. */
		phyaddr = (IPG_NIC_PHY_ADDRESS + i) % 32;

		/* 10/22/03 Grace change verify from GMII_PHY_STATUS to
		   GMII_PHY_ID1
		 */

		status = mdio_read(dev, phyaddr, MII_BMSR);

		if ((status != 0xFFFF) && (status != 0))
			return phyaddr;
	}

	return 0x1f;
}

/*
 * Configure IPG based on result of IEEE 802.3 PHY
 * auto-negotiation.
 */
static int ipg_config_autoneg(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int txflowcontrol;
	unsigned int rxflowcontrol;
	unsigned int fullduplex;
	u32 mac_ctrl_val;
	u32 asicctrl;
	u8 phyctrl;

	IPG_DEBUG_MSG("_config_autoneg\n");

	asicctrl = ipg_r32(ASIC_CTRL);
	phyctrl = ipg_r8(PHY_CTRL);
	mac_ctrl_val = ipg_r32(MAC_CTRL);

	/* Set flags for use in resolving auto-negotation, assuming
	 * non-1000Mbps, half duplex, no flow control.
	 */
	fullduplex = 0;
	txflowcontrol = 0;
	rxflowcontrol = 0;

	/* To accomodate a problem in 10Mbps operation,
	 * set a global flag if PHY running in 10Mbps mode.
	 */
	sp->tenmbpsmode = 0;

	printk(KERN_INFO "%s: Link speed = ", dev->name);

	/* Determine actual speed of operation. */
	switch (phyctrl & IPG_PC_LINK_SPEED) {
	case IPG_PC_LINK_SPEED_10MBPS:
		printk("10Mbps.\n");
		printk(KERN_INFO "%s: 10Mbps operational mode enabled.\n",
		       dev->name);
		sp->tenmbpsmode = 1;
		break;
	case IPG_PC_LINK_SPEED_100MBPS:
		printk("100Mbps.\n");
		break;
	case IPG_PC_LINK_SPEED_1000MBPS:
		printk("1000Mbps.\n");
		break;
	default:
		printk("undefined!\n");
		return 0;
	}

	if (phyctrl & IPG_PC_DUPLEX_STATUS) {
		fullduplex = 1;
		txflowcontrol = 1;
		rxflowcontrol = 1;
	}

	/* Configure full duplex, and flow control. */
	if (fullduplex == 1) {
		/* Configure IPG for full duplex operation. */
		printk(KERN_INFO "%s: setting full duplex, ", dev->name);

		mac_ctrl_val |= IPG_MC_DUPLEX_SELECT_FD;

		if (txflowcontrol == 1) {
			printk("TX flow control");
			mac_ctrl_val |= IPG_MC_TX_FLOW_CONTROL_ENABLE;
		} else {
			printk("no TX flow control");
			mac_ctrl_val &= ~IPG_MC_TX_FLOW_CONTROL_ENABLE;
		}

		if (rxflowcontrol == 1) {
			printk(", RX flow control.");
			mac_ctrl_val |= IPG_MC_RX_FLOW_CONTROL_ENABLE;
		} else {
			printk(", no RX flow control.");
			mac_ctrl_val &= ~IPG_MC_RX_FLOW_CONTROL_ENABLE;
		}

		printk("\n");
	} else {
		/* Configure IPG for half duplex operation. */
		printk(KERN_INFO "%s: setting half duplex, "
		       "no TX flow control, no RX flow control.\n", dev->name);

		mac_ctrl_val &= ~IPG_MC_DUPLEX_SELECT_FD &
			~IPG_MC_TX_FLOW_CONTROL_ENABLE &
			~IPG_MC_RX_FLOW_CONTROL_ENABLE;
	}
	ipg_w32(mac_ctrl_val, MAC_CTRL);
	return 0;
}

/* Determine and configure multicast operation and set
 * receive mode for IPG.
 */
static void ipg_nic_set_multicast_list(struct net_device *dev)
{
	void __iomem *ioaddr = ipg_ioaddr(dev);
	struct dev_mc_list *mc_list_ptr;
	unsigned int hashindex;
	u32 hashtable[2];
	u8 receivemode;

	IPG_DEBUG_MSG("_nic_set_multicast_list\n");

	receivemode = IPG_RM_RECEIVEUNICAST | IPG_RM_RECEIVEBROADCAST;

	if (dev->flags & IFF_PROMISC) {
		/* NIC to be configured in promiscuous mode. */
		receivemode = IPG_RM_RECEIVEALLFRAMES;
	} else if ((dev->flags & IFF_ALLMULTI) ||
		   ((dev->flags & IFF_MULTICAST) &&
		    (dev->mc_count > IPG_MULTICAST_HASHTABLE_SIZE))) {
		/* NIC to be configured to receive all multicast
		 * frames. */
		receivemode |= IPG_RM_RECEIVEMULTICAST;
	} else if ((dev->flags & IFF_MULTICAST) && (dev->mc_count > 0)) {
		/* NIC to be configured to receive selected
		 * multicast addresses. */
		receivemode |= IPG_RM_RECEIVEMULTICASTHASH;
	}

	/* Calculate the bits to set for the 64 bit, IPG HASHTABLE.
	 * The IPG applies a cyclic-redundancy-check (the same CRC
	 * used to calculate the frame data FCS) to the destination
	 * address all incoming multicast frames whose destination
	 * address has the multicast bit set. The least significant
	 * 6 bits of the CRC result are used as an addressing index
	 * into the hash table. If the value of the bit addressed by
	 * this index is a 1, the frame is passed to the host system.
	 */

	/* Clear hashtable. */
	hashtable[0] = 0x00000000;
	hashtable[1] = 0x00000000;

	/* Cycle through all multicast addresses to filter. */
	for (mc_list_ptr = dev->mc_list;
	     mc_list_ptr != NULL; mc_list_ptr = mc_list_ptr->next) {
		/* Calculate CRC result for each multicast address. */
		hashindex = crc32_le(0xffffffff, mc_list_ptr->dmi_addr,
				     ETH_ALEN);

		/* Use only the least significant 6 bits. */
		hashindex = hashindex & 0x3F;

		/* Within "hashtable", set bit number "hashindex"
		 * to a logic 1.
		 */
		set_bit(hashindex, (void *)hashtable);
	}

	/* Write the value of the hashtable, to the 4, 16 bit
	 * HASHTABLE IPG registers.
	 */
	ipg_w32(hashtable[0], HASHTABLE_0);
	ipg_w32(hashtable[1], HASHTABLE_1);

	ipg_w8(IPG_RM_RSVD_MASK & receivemode, RECEIVE_MODE);

	IPG_DEBUG_MSG("ReceiveMode = %x\n", ipg_r8(RECEIVE_MODE));
}

static int ipg_io_config(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = ipg_ioaddr(dev);
	u32 origmacctrl;
	u32 restoremacctrl;

	IPG_DEBUG_MSG("_io_config\n");

	origmacctrl = ipg_r32(MAC_CTRL);

	restoremacctrl = origmacctrl | IPG_MC_STATISTICS_ENABLE;

	/* Based on compilation option, determine if FCS is to be
	 * stripped on receive frames by IPG.
	 */
	if (!IPG_STRIP_FCS_ON_RX)
		restoremacctrl |= IPG_MC_RCV_FCS;

	/* Determine if transmitter and/or receiver are
	 * enabled so we may restore MACCTRL correctly.
	 */
	if (origmacctrl & IPG_MC_TX_ENABLED)
		restoremacctrl |= IPG_MC_TX_ENABLE;

	if (origmacctrl & IPG_MC_RX_ENABLED)
		restoremacctrl |= IPG_MC_RX_ENABLE;

	/* Transmitter and receiver must be disabled before setting
	 * IFSSelect.
	 */
	ipg_w32((origmacctrl & (IPG_MC_RX_DISABLE | IPG_MC_TX_DISABLE)) &
		IPG_MC_RSVD_MASK, MAC_CTRL);

	/* Now that transmitter and receiver are disabled, write
	 * to IFSSelect.
	 */
	ipg_w32((origmacctrl & IPG_MC_IFS_96BIT) & IPG_MC_RSVD_MASK, MAC_CTRL);

	/* Set RECEIVEMODE register. */
	ipg_nic_set_multicast_list(dev);

	ipg_w16(sp->max_rxframe_size, MAX_FRAME_SIZE);

	ipg_w8(IPG_RXDMAPOLLPERIOD_VALUE,   RX_DMA_POLL_PERIOD);
	ipg_w8(IPG_RXDMAURGENTTHRESH_VALUE, RX_DMA_URGENT_THRESH);
	ipg_w8(IPG_RXDMABURSTTHRESH_VALUE,  RX_DMA_BURST_THRESH);
	ipg_w8(IPG_TXDMAPOLLPERIOD_VALUE,   TX_DMA_POLL_PERIOD);
	ipg_w8(IPG_TXDMAURGENTTHRESH_VALUE, TX_DMA_URGENT_THRESH);
	ipg_w8(IPG_TXDMABURSTTHRESH_VALUE,  TX_DMA_BURST_THRESH);
	ipg_w16((IPG_IE_HOST_ERROR | IPG_IE_TX_DMA_COMPLETE |
		 IPG_IE_TX_COMPLETE | IPG_IE_INT_REQUESTED |
		 IPG_IE_UPDATE_STATS | IPG_IE_LINK_EVENT |
		 IPG_IE_RX_DMA_COMPLETE | IPG_IE_RX_DMA_PRIORITY), INT_ENABLE);
	ipg_w16(IPG_FLOWONTHRESH_VALUE,  FLOW_ON_THRESH);
	ipg_w16(IPG_FLOWOFFTHRESH_VALUE, FLOW_OFF_THRESH);

	/* IPG multi-frag frame bug workaround.
	 * Per silicon revision B3 eratta.
	 */
	ipg_w16(ipg_r16(DEBUG_CTRL) | 0x0200, DEBUG_CTRL);

	/* IPG TX poll now bug workaround.
	 * Per silicon revision B3 eratta.
	 */
	ipg_w16(ipg_r16(DEBUG_CTRL) | 0x0010, DEBUG_CTRL);

	/* IPG RX poll now bug workaround.
	 * Per silicon revision B3 eratta.
	 */
	ipg_w16(ipg_r16(DEBUG_CTRL) | 0x0020, DEBUG_CTRL);

	/* Now restore MACCTRL to original setting. */
	ipg_w32(IPG_MC_RSVD_MASK & restoremacctrl, MAC_CTRL);

	/* Disable unused RMON statistics. */
	ipg_w32(IPG_RZ_ALL, RMON_STATISTICS_MASK);

	/* Disable unused MIB statistics. */
	ipg_w32(IPG_SM_MACCONTROLFRAMESXMTD | IPG_SM_MACCONTROLFRAMESRCVD |
		IPG_SM_BCSTOCTETXMTOK_BCSTFRAMESXMTDOK | IPG_SM_TXJUMBOFRAMES |
		IPG_SM_MCSTOCTETXMTOK_MCSTFRAMESXMTDOK | IPG_SM_RXJUMBOFRAMES |
		IPG_SM_BCSTOCTETRCVDOK_BCSTFRAMESRCVDOK |
		IPG_SM_UDPCHECKSUMERRORS | IPG_SM_TCPCHECKSUMERRORS |
		IPG_SM_IPCHECKSUMERRORS, STATISTICS_MASK);

	return 0;
}

/*
 * Create a receive buffer within system memory and update
 * NIC private structure appropriately.
 */
static int ipg_get_rxbuff(struct net_device *dev, int entry)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	struct ipg_rx *rxfd = sp->rxd + entry;
	struct sk_buff *skb;
	u64 rxfragsize;

	IPG_DEBUG_MSG("_get_rxbuff\n");

	skb = netdev_alloc_skb(dev, sp->rxsupport_size + NET_IP_ALIGN);
	if (!skb) {
		sp->rx_buff[entry] = NULL;
		return -ENOMEM;
	}

	/* Adjust the data start location within the buffer to
	 * align IP address field to a 16 byte boundary.
	 */
	skb_reserve(skb, NET_IP_ALIGN);

	/* Associate the receive buffer with the IPG NIC. */
	skb->dev = dev;

	/* Save the address of the sk_buff structure. */
	sp->rx_buff[entry] = skb;

	rxfd->frag_info = cpu_to_le64(pci_map_single(sp->pdev, skb->data,
		sp->rx_buf_sz, PCI_DMA_FROMDEVICE));

	/* Set the RFD fragment length. */
	rxfragsize = sp->rxfrag_size;
	rxfd->frag_info |= cpu_to_le64((rxfragsize << 48) & IPG_RFI_FRAGLEN);

	return 0;
}

static int init_rfdlist(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;

	IPG_DEBUG_MSG("_init_rfdlist\n");

	for (i = 0; i < IPG_RFDLIST_LENGTH; i++) {
		struct ipg_rx *rxfd = sp->rxd + i;

		if (sp->rx_buff[i]) {
			pci_unmap_single(sp->pdev,
				le64_to_cpu(rxfd->frag_info) & ~IPG_RFI_FRAGLEN,
				sp->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb_irq(sp->rx_buff[i]);
			sp->rx_buff[i] = NULL;
		}

		/* Clear out the RFS field. */
		rxfd->rfs = 0x0000000000000000;

		if (ipg_get_rxbuff(dev, i) < 0) {
			/*
			 * A receive buffer was not ready, break the
			 * RFD list here.
			 */
			IPG_DEBUG_MSG("Cannot allocate Rx buffer.\n");

			/* Just in case we cannot allocate a single RFD.
			 * Should not occur.
			 */
			if (i == 0) {
				printk(KERN_ERR "%s: No memory available"
					" for RFD list.\n", dev->name);
				return -ENOMEM;
			}
		}

		rxfd->next_desc = cpu_to_le64(sp->rxd_map +
			sizeof(struct ipg_rx)*(i + 1));
	}
	sp->rxd[i - 1].next_desc = cpu_to_le64(sp->rxd_map);

	sp->rx_current = 0;
	sp->rx_dirty = 0;

	/* Write the location of the RFDList to the IPG. */
	ipg_w32((u32) sp->rxd_map, RFD_LIST_PTR_0);
	ipg_w32(0x00000000, RFD_LIST_PTR_1);

	return 0;
}

static void init_tfdlist(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;

	IPG_DEBUG_MSG("_init_tfdlist\n");

	for (i = 0; i < IPG_TFDLIST_LENGTH; i++) {
		struct ipg_tx *txfd = sp->txd + i;

		txfd->tfc = cpu_to_le64(IPG_TFC_TFDDONE);

		if (sp->tx_buff[i]) {
			dev_kfree_skb_irq(sp->tx_buff[i]);
			sp->tx_buff[i] = NULL;
		}

		txfd->next_desc = cpu_to_le64(sp->txd_map +
			sizeof(struct ipg_tx)*(i + 1));
	}
	sp->txd[i - 1].next_desc = cpu_to_le64(sp->txd_map);

	sp->tx_current = 0;
	sp->tx_dirty = 0;

	/* Write the location of the TFDList to the IPG. */
	IPG_DDEBUG_MSG("Starting TFDListPtr = %8.8x\n",
		       (u32) sp->txd_map);
	ipg_w32((u32) sp->txd_map, TFD_LIST_PTR_0);
	ipg_w32(0x00000000, TFD_LIST_PTR_1);

	sp->reset_current_tfd = 1;
}

/*
 * Free all transmit buffers which have already been transfered
 * via DMA to the IPG.
 */
static void ipg_nic_txfree(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	unsigned int released, pending, dirty;

	IPG_DEBUG_MSG("_nic_txfree\n");

	pending = sp->tx_current - sp->tx_dirty;
	dirty = sp->tx_dirty % IPG_TFDLIST_LENGTH;

	for (released = 0; released < pending; released++) {
		struct sk_buff *skb = sp->tx_buff[dirty];
		struct ipg_tx *txfd = sp->txd + dirty;

		IPG_DEBUG_MSG("TFC = %16.16lx\n", (unsigned long) txfd->tfc);

		/* Look at each TFD's TFC field beginning
		 * at the last freed TFD up to the current TFD.
		 * If the TFDDone bit is set, free the associated
		 * buffer.
		 */
		if (!(txfd->tfc & cpu_to_le64(IPG_TFC_TFDDONE)))
                        break;

		/* Free the transmit buffer. */
		if (skb) {
			pci_unmap_single(sp->pdev,
				le64_to_cpu(txfd->frag_info) & ~IPG_TFI_FRAGLEN,
				skb->len, PCI_DMA_TODEVICE);

			dev_kfree_skb_irq(skb);

			sp->tx_buff[dirty] = NULL;
		}
		dirty = (dirty + 1) % IPG_TFDLIST_LENGTH;
	}

	sp->tx_dirty += released;

	if (netif_queue_stopped(dev) &&
	    (sp->tx_current != (sp->tx_dirty + IPG_TFDLIST_LENGTH))) {
		netif_wake_queue(dev);
	}
}

static void ipg_tx_timeout(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;

	ipg_reset(dev, IPG_AC_TX_RESET | IPG_AC_DMA | IPG_AC_NETWORK |
		  IPG_AC_FIFO);

	spin_lock_irq(&sp->lock);

	/* Re-configure after DMA reset. */
	if (ipg_io_config(dev) < 0) {
		printk(KERN_INFO "%s: Error during re-configuration.\n",
		       dev->name);
	}

	init_tfdlist(dev);

	spin_unlock_irq(&sp->lock);

	ipg_w32((ipg_r32(MAC_CTRL) | IPG_MC_TX_ENABLE) & IPG_MC_RSVD_MASK,
		MAC_CTRL);
}

/*
 * For TxComplete interrupts, free all transmit
 * buffers which have already been transfered via DMA
 * to the IPG.
 */
static void ipg_nic_txcleanup(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;

	IPG_DEBUG_MSG("_nic_txcleanup\n");

	for (i = 0; i < IPG_TFDLIST_LENGTH; i++) {
		/* Reading the TXSTATUS register clears the
		 * TX_COMPLETE interrupt.
		 */
		u32 txstatusdword = ipg_r32(TX_STATUS);

		IPG_DEBUG_MSG("TxStatus = %8.8x\n", txstatusdword);

		/* Check for Transmit errors. Error bits only valid if
		 * TX_COMPLETE bit in the TXSTATUS register is a 1.
		 */
		if (!(txstatusdword & IPG_TS_TX_COMPLETE))
			break;

		/* If in 10Mbps mode, indicate transmit is ready. */
		if (sp->tenmbpsmode) {
			netif_wake_queue(dev);
		}

		/* Transmit error, increment stat counters. */
		if (txstatusdword & IPG_TS_TX_ERROR) {
			IPG_DEBUG_MSG("Transmit error.\n");
			sp->stats.tx_errors++;
		}

		/* Late collision, re-enable transmitter. */
		if (txstatusdword & IPG_TS_LATE_COLLISION) {
			IPG_DEBUG_MSG("Late collision on transmit.\n");
			ipg_w32((ipg_r32(MAC_CTRL) | IPG_MC_TX_ENABLE) &
				IPG_MC_RSVD_MASK, MAC_CTRL);
		}

		/* Maximum collisions, re-enable transmitter. */
		if (txstatusdword & IPG_TS_TX_MAX_COLL) {
			IPG_DEBUG_MSG("Maximum collisions on transmit.\n");
			ipg_w32((ipg_r32(MAC_CTRL) | IPG_MC_TX_ENABLE) &
				IPG_MC_RSVD_MASK, MAC_CTRL);
		}

		/* Transmit underrun, reset and re-enable
		 * transmitter.
		 */
		if (txstatusdword & IPG_TS_TX_UNDERRUN) {
			IPG_DEBUG_MSG("Transmitter underrun.\n");
			sp->stats.tx_fifo_errors++;
			ipg_reset(dev, IPG_AC_TX_RESET | IPG_AC_DMA |
				  IPG_AC_NETWORK | IPG_AC_FIFO);

			/* Re-configure after DMA reset. */
			if (ipg_io_config(dev) < 0) {
				printk(KERN_INFO
				       "%s: Error during re-configuration.\n",
				       dev->name);
			}
			init_tfdlist(dev);

			ipg_w32((ipg_r32(MAC_CTRL) | IPG_MC_TX_ENABLE) &
				IPG_MC_RSVD_MASK, MAC_CTRL);
		}
	}

	ipg_nic_txfree(dev);
}

/* Provides statistical information about the IPG NIC. */
static struct net_device_stats *ipg_nic_get_stats(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	u16 temp1;
	u16 temp2;

	IPG_DEBUG_MSG("_nic_get_stats\n");

	/* Check to see if the NIC has been initialized via nic_open,
	 * before trying to read statistic registers.
	 */
	if (!test_bit(__LINK_STATE_START, &dev->state))
		return &sp->stats;

	sp->stats.rx_packets += ipg_r32(IPG_FRAMESRCVDOK);
	sp->stats.tx_packets += ipg_r32(IPG_FRAMESXMTDOK);
	sp->stats.rx_bytes += ipg_r32(IPG_OCTETRCVOK);
	sp->stats.tx_bytes += ipg_r32(IPG_OCTETXMTOK);
	temp1 = ipg_r16(IPG_FRAMESLOSTRXERRORS);
	sp->stats.rx_errors += temp1;
	sp->stats.rx_missed_errors += temp1;
	temp1 = ipg_r32(IPG_SINGLECOLFRAMES) + ipg_r32(IPG_MULTICOLFRAMES) +
		ipg_r32(IPG_LATECOLLISIONS);
	temp2 = ipg_r16(IPG_CARRIERSENSEERRORS);
	sp->stats.collisions += temp1;
	sp->stats.tx_dropped += ipg_r16(IPG_FRAMESABORTXSCOLLS);
	sp->stats.tx_errors += ipg_r16(IPG_FRAMESWEXDEFERRAL) +
		ipg_r32(IPG_FRAMESWDEFERREDXMT) + temp1 + temp2;
	sp->stats.multicast += ipg_r32(IPG_MCSTOCTETRCVDOK);

	/* detailed tx_errors */
	sp->stats.tx_carrier_errors += temp2;

	/* detailed rx_errors */
	sp->stats.rx_length_errors += ipg_r16(IPG_INRANGELENGTHERRORS) +
		ipg_r16(IPG_FRAMETOOLONGERRRORS);
	sp->stats.rx_crc_errors += ipg_r16(IPG_FRAMECHECKSEQERRORS);

	/* Unutilized IPG statistic registers. */
	ipg_r32(IPG_MCSTFRAMESRCVDOK);

	return &sp->stats;
}

/* Restore used receive buffers. */
static int ipg_nic_rxrestore(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	const unsigned int curr = sp->rx_current;
	unsigned int dirty = sp->rx_dirty;

	IPG_DEBUG_MSG("_nic_rxrestore\n");

	for (dirty = sp->rx_dirty; curr - dirty > 0; dirty++) {
		unsigned int entry = dirty % IPG_RFDLIST_LENGTH;

		/* rx_copybreak may poke hole here and there. */
		if (sp->rx_buff[entry])
			continue;

		/* Generate a new receive buffer to replace the
		 * current buffer (which will be released by the
		 * Linux system).
		 */
		if (ipg_get_rxbuff(dev, entry) < 0) {
			IPG_DEBUG_MSG("Cannot allocate new Rx buffer.\n");

			break;
		}

		/* Reset the RFS field. */
		sp->rxd[entry].rfs = 0x0000000000000000;
	}
	sp->rx_dirty = dirty;

	return 0;
}

/* use jumboindex and jumbosize to control jumbo frame status
 * initial status is jumboindex=-1 and jumbosize=0
 * 1. jumboindex = -1 and jumbosize=0 : previous jumbo frame has been done.
 * 2. jumboindex != -1 and jumbosize != 0 : jumbo frame is not over size and receiving
 * 3. jumboindex = -1 and jumbosize != 0 : jumbo frame is over size, already dump
 *               previous receiving and need to continue dumping the current one
 */
enum {
	NORMAL_PACKET,
	ERROR_PACKET
};

enum {
	FRAME_NO_START_NO_END	= 0,
	FRAME_WITH_START		= 1,
	FRAME_WITH_END		= 10,
	FRAME_WITH_START_WITH_END = 11
};

static void ipg_nic_rx_free_skb(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	unsigned int entry = sp->rx_current % IPG_RFDLIST_LENGTH;

	if (sp->rx_buff[entry]) {
		struct ipg_rx *rxfd = sp->rxd + entry;

		pci_unmap_single(sp->pdev,
			le64_to_cpu(rxfd->frag_info) & ~IPG_RFI_FRAGLEN,
			sp->rx_buf_sz, PCI_DMA_FROMDEVICE);
		dev_kfree_skb_irq(sp->rx_buff[entry]);
		sp->rx_buff[entry] = NULL;
	}
}

static int ipg_nic_rx_check_frame_type(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	struct ipg_rx *rxfd = sp->rxd + (sp->rx_current % IPG_RFDLIST_LENGTH);
	int type = FRAME_NO_START_NO_END;

	if (le64_to_cpu(rxfd->rfs) & IPG_RFS_FRAMESTART)
		type += FRAME_WITH_START;
	if (le64_to_cpu(rxfd->rfs) & IPG_RFS_FRAMEEND)
		type += FRAME_WITH_END;
	return type;
}

static int ipg_nic_rx_check_error(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	unsigned int entry = sp->rx_current % IPG_RFDLIST_LENGTH;
	struct ipg_rx *rxfd = sp->rxd + entry;

	if (IPG_DROP_ON_RX_ETH_ERRORS && (le64_to_cpu(rxfd->rfs) &
	     (IPG_RFS_RXFIFOOVERRUN | IPG_RFS_RXRUNTFRAME |
	      IPG_RFS_RXALIGNMENTERROR | IPG_RFS_RXFCSERROR |
	      IPG_RFS_RXOVERSIZEDFRAME | IPG_RFS_RXLENGTHERROR))) {
		IPG_DEBUG_MSG("Rx error, RFS = %16.16lx\n",
			      (unsigned long) rxfd->rfs);

		/* Increment general receive error statistic. */
		sp->stats.rx_errors++;

		/* Increment detailed receive error statistics. */
		if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXFIFOOVERRUN) {
			IPG_DEBUG_MSG("RX FIFO overrun occured.\n");

			sp->stats.rx_fifo_errors++;
		}

		if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXRUNTFRAME) {
			IPG_DEBUG_MSG("RX runt occured.\n");
			sp->stats.rx_length_errors++;
		}

		/* Do nothing for IPG_RFS_RXOVERSIZEDFRAME,
		 * error count handled by a IPG statistic register.
		 */

		if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXALIGNMENTERROR) {
			IPG_DEBUG_MSG("RX alignment error occured.\n");
			sp->stats.rx_frame_errors++;
		}

		/* Do nothing for IPG_RFS_RXFCSERROR, error count
		 * handled by a IPG statistic register.
		 */

		/* Free the memory associated with the RX
		 * buffer since it is erroneous and we will
		 * not pass it to higher layer processes.
		 */
		if (sp->rx_buff[entry]) {
			pci_unmap_single(sp->pdev,
				le64_to_cpu(rxfd->frag_info) & ~IPG_RFI_FRAGLEN,
				sp->rx_buf_sz, PCI_DMA_FROMDEVICE);

			dev_kfree_skb_irq(sp->rx_buff[entry]);
			sp->rx_buff[entry] = NULL;
		}
		return ERROR_PACKET;
	}
	return NORMAL_PACKET;
}

static void ipg_nic_rx_with_start_and_end(struct net_device *dev,
					  struct ipg_nic_private *sp,
					  struct ipg_rx *rxfd, unsigned entry)
{
	struct ipg_jumbo *jumbo = &sp->jumbo;
	struct sk_buff *skb;
	int framelen;

	if (jumbo->found_start) {
		dev_kfree_skb_irq(jumbo->skb);
		jumbo->found_start = 0;
		jumbo->current_size = 0;
		jumbo->skb = NULL;
	}

	/* 1: found error, 0 no error */
	if (ipg_nic_rx_check_error(dev) != NORMAL_PACKET)
		return;

	skb = sp->rx_buff[entry];
	if (!skb)
		return;

	/* accept this frame and send to upper layer */
	framelen = le64_to_cpu(rxfd->rfs) & IPG_RFS_RXFRAMELEN;
	if (framelen > sp->rxfrag_size)
		framelen = sp->rxfrag_size;

	skb_put(skb, framelen);
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;
	netif_rx(skb);
	sp->rx_buff[entry] = NULL;
}

static void ipg_nic_rx_with_start(struct net_device *dev,
				  struct ipg_nic_private *sp,
				  struct ipg_rx *rxfd, unsigned entry)
{
	struct ipg_jumbo *jumbo = &sp->jumbo;
	struct pci_dev *pdev = sp->pdev;
	struct sk_buff *skb;

	/* 1: found error, 0 no error */
	if (ipg_nic_rx_check_error(dev) != NORMAL_PACKET)
		return;

	/* accept this frame and send to upper layer */
	skb = sp->rx_buff[entry];
	if (!skb)
		return;

	if (jumbo->found_start)
		dev_kfree_skb_irq(jumbo->skb);

	pci_unmap_single(pdev, le64_to_cpu(rxfd->frag_info) & ~IPG_RFI_FRAGLEN,
			 sp->rx_buf_sz, PCI_DMA_FROMDEVICE);

	skb_put(skb, sp->rxfrag_size);

	jumbo->found_start = 1;
	jumbo->current_size = sp->rxfrag_size;
	jumbo->skb = skb;

	sp->rx_buff[entry] = NULL;
}

static void ipg_nic_rx_with_end(struct net_device *dev,
				struct ipg_nic_private *sp,
				struct ipg_rx *rxfd, unsigned entry)
{
	struct ipg_jumbo *jumbo = &sp->jumbo;

	/* 1: found error, 0 no error */
	if (ipg_nic_rx_check_error(dev) == NORMAL_PACKET) {
		struct sk_buff *skb = sp->rx_buff[entry];

		if (!skb)
			return;

		if (jumbo->found_start) {
			int framelen, endframelen;

			framelen = le64_to_cpu(rxfd->rfs) & IPG_RFS_RXFRAMELEN;

			endframelen = framelen - jumbo->current_size;
			if (framelen > sp->rxsupport_size)
				dev_kfree_skb_irq(jumbo->skb);
			else {
				memcpy(skb_put(jumbo->skb, endframelen),
				       skb->data, endframelen);

				jumbo->skb->protocol =
				    eth_type_trans(jumbo->skb, dev);

				jumbo->skb->ip_summed = CHECKSUM_NONE;
				netif_rx(jumbo->skb);
			}
		}

		jumbo->found_start = 0;
		jumbo->current_size = 0;
		jumbo->skb = NULL;

		ipg_nic_rx_free_skb(dev);
	} else {
		dev_kfree_skb_irq(jumbo->skb);
		jumbo->found_start = 0;
		jumbo->current_size = 0;
		jumbo->skb = NULL;
	}
}

static void ipg_nic_rx_no_start_no_end(struct net_device *dev,
				       struct ipg_nic_private *sp,
				       struct ipg_rx *rxfd, unsigned entry)
{
	struct ipg_jumbo *jumbo = &sp->jumbo;

	/* 1: found error, 0 no error */
	if (ipg_nic_rx_check_error(dev) == NORMAL_PACKET) {
		struct sk_buff *skb = sp->rx_buff[entry];

		if (skb) {
			if (jumbo->found_start) {
				jumbo->current_size += sp->rxfrag_size;
				if (jumbo->current_size <= sp->rxsupport_size) {
					memcpy(skb_put(jumbo->skb,
						       sp->rxfrag_size),
					       skb->data, sp->rxfrag_size);
				}
			}
			ipg_nic_rx_free_skb(dev);
		}
	} else {
		dev_kfree_skb_irq(jumbo->skb);
		jumbo->found_start = 0;
		jumbo->current_size = 0;
		jumbo->skb = NULL;
	}
}

static int ipg_nic_rx_jumbo(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	unsigned int curr = sp->rx_current;
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;

	IPG_DEBUG_MSG("_nic_rx\n");

	for (i = 0; i < IPG_MAXRFDPROCESS_COUNT; i++, curr++) {
		unsigned int entry = curr % IPG_RFDLIST_LENGTH;
		struct ipg_rx *rxfd = sp->rxd + entry;

		if (!(rxfd->rfs & cpu_to_le64(IPG_RFS_RFDDONE)))
			break;

		switch (ipg_nic_rx_check_frame_type(dev)) {
		case FRAME_WITH_START_WITH_END:
			ipg_nic_rx_with_start_and_end(dev, sp, rxfd, entry);
			break;
		case FRAME_WITH_START:
			ipg_nic_rx_with_start(dev, sp, rxfd, entry);
			break;
		case FRAME_WITH_END:
			ipg_nic_rx_with_end(dev, sp, rxfd, entry);
			break;
		case FRAME_NO_START_NO_END:
			ipg_nic_rx_no_start_no_end(dev, sp, rxfd, entry);
			break;
		}
	}

	sp->rx_current = curr;

	if (i == IPG_MAXRFDPROCESS_COUNT) {
		/* There are more RFDs to process, however the
		 * allocated amount of RFD processing time has
		 * expired. Assert Interrupt Requested to make
		 * sure we come back to process the remaining RFDs.
		 */
		ipg_w32(ipg_r32(ASIC_CTRL) | IPG_AC_INT_REQUEST, ASIC_CTRL);
	}

	ipg_nic_rxrestore(dev);

	return 0;
}

static int ipg_nic_rx(struct net_device *dev)
{
	/* Transfer received Ethernet frames to higher network layers. */
	struct ipg_nic_private *sp = netdev_priv(dev);
	unsigned int curr = sp->rx_current;
	void __iomem *ioaddr = sp->ioaddr;
	struct ipg_rx *rxfd;
	unsigned int i;

	IPG_DEBUG_MSG("_nic_rx\n");

#define __RFS_MASK \
	cpu_to_le64(IPG_RFS_RFDDONE | IPG_RFS_FRAMESTART | IPG_RFS_FRAMEEND)

	for (i = 0; i < IPG_MAXRFDPROCESS_COUNT; i++, curr++) {
		unsigned int entry = curr % IPG_RFDLIST_LENGTH;
		struct sk_buff *skb = sp->rx_buff[entry];
		unsigned int framelen;

		rxfd = sp->rxd + entry;

		if (((rxfd->rfs & __RFS_MASK) != __RFS_MASK) || !skb)
			break;

		/* Get received frame length. */
		framelen = le64_to_cpu(rxfd->rfs) & IPG_RFS_RXFRAMELEN;

		/* Check for jumbo frame arrival with too small
		 * RXFRAG_SIZE.
		 */
		if (framelen > sp->rxfrag_size) {
			IPG_DEBUG_MSG
			    ("RFS FrameLen > allocated fragment size.\n");

			framelen = sp->rxfrag_size;
		}

		if ((IPG_DROP_ON_RX_ETH_ERRORS && (le64_to_cpu(rxfd->rfs) &
		       (IPG_RFS_RXFIFOOVERRUN | IPG_RFS_RXRUNTFRAME |
			IPG_RFS_RXALIGNMENTERROR | IPG_RFS_RXFCSERROR |
			IPG_RFS_RXOVERSIZEDFRAME | IPG_RFS_RXLENGTHERROR)))) {

			IPG_DEBUG_MSG("Rx error, RFS = %16.16lx\n",
				      (unsigned long int) rxfd->rfs);

			/* Increment general receive error statistic. */
			sp->stats.rx_errors++;

			/* Increment detailed receive error statistics. */
			if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXFIFOOVERRUN) {
				IPG_DEBUG_MSG("RX FIFO overrun occured.\n");
				sp->stats.rx_fifo_errors++;
			}

			if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXRUNTFRAME) {
				IPG_DEBUG_MSG("RX runt occured.\n");
				sp->stats.rx_length_errors++;
			}

			if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXOVERSIZEDFRAME) ;
			/* Do nothing, error count handled by a IPG
			 * statistic register.
			 */

			if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXALIGNMENTERROR) {
				IPG_DEBUG_MSG("RX alignment error occured.\n");
				sp->stats.rx_frame_errors++;
			}

			if (le64_to_cpu(rxfd->rfs) & IPG_RFS_RXFCSERROR) ;
			/* Do nothing, error count handled by a IPG
			 * statistic register.
			 */

			/* Free the memory associated with the RX
			 * buffer since it is erroneous and we will
			 * not pass it to higher layer processes.
			 */
			if (skb) {
				__le64 info = rxfd->frag_info;

				pci_unmap_single(sp->pdev,
					le64_to_cpu(info) & ~IPG_RFI_FRAGLEN,
					sp->rx_buf_sz, PCI_DMA_FROMDEVICE);

				dev_kfree_skb_irq(skb);
			}
		} else {

			/* Adjust the new buffer length to accomodate the size
			 * of the received frame.
			 */
			skb_put(skb, framelen);

			/* Set the buffer's protocol field to Ethernet. */
			skb->protocol = eth_type_trans(skb, dev);

			/* The IPG encountered an error with (or
			 * there were no) IP/TCP/UDP checksums.
			 * This may or may not indicate an invalid
			 * IP/TCP/UDP frame was received. Let the
			 * upper layer decide.
			 */
			skb->ip_summed = CHECKSUM_NONE;

			/* Hand off frame for higher layer processing.
			 * The function netif_rx() releases the sk_buff
			 * when processing completes.
			 */
			netif_rx(skb);
		}

		/* Assure RX buffer is not reused by IPG. */
		sp->rx_buff[entry] = NULL;
	}

	/*
	 * If there are more RFDs to proces and the allocated amount of RFD
	 * processing time has expired, assert Interrupt Requested to make
	 * sure we come back to process the remaining RFDs.
	 */
	if (i == IPG_MAXRFDPROCESS_COUNT)
		ipg_w32(ipg_r32(ASIC_CTRL) | IPG_AC_INT_REQUEST, ASIC_CTRL);

#ifdef IPG_DEBUG
	/* Check if the RFD list contained no receive frame data. */
	if (!i)
		sp->EmptyRFDListCount++;
#endif
	while ((le64_to_cpu(rxfd->rfs) & IPG_RFS_RFDDONE) &&
	       !((le64_to_cpu(rxfd->rfs) & IPG_RFS_FRAMESTART) &&
		 (le64_to_cpu(rxfd->rfs) & IPG_RFS_FRAMEEND))) {
		unsigned int entry = curr++ % IPG_RFDLIST_LENGTH;

		rxfd = sp->rxd + entry;

		IPG_DEBUG_MSG("Frame requires multiple RFDs.\n");

		/* An unexpected event, additional code needed to handle
		 * properly. So for the time being, just disregard the
		 * frame.
		 */

		/* Free the memory associated with the RX
		 * buffer since it is erroneous and we will
		 * not pass it to higher layer processes.
		 */
		if (sp->rx_buff[entry]) {
			pci_unmap_single(sp->pdev,
				le64_to_cpu(rxfd->frag_info) & ~IPG_RFI_FRAGLEN,
				sp->rx_buf_sz, PCI_DMA_FROMDEVICE);
			dev_kfree_skb_irq(sp->rx_buff[entry]);
		}

		/* Assure RX buffer is not reused by IPG. */
		sp->rx_buff[entry] = NULL;
	}

	sp->rx_current = curr;

	/* Check to see if there are a minimum number of used
	 * RFDs before restoring any (should improve performance.)
	 */
	if ((curr - sp->rx_dirty) >= IPG_MINUSEDRFDSTOFREE)
		ipg_nic_rxrestore(dev);

	return 0;
}

static void ipg_reset_after_host_error(struct work_struct *work)
{
	struct ipg_nic_private *sp =
		container_of(work, struct ipg_nic_private, task.work);
	struct net_device *dev = sp->dev;

	IPG_DDEBUG_MSG("DMACtrl = %8.8x\n", ioread32(sp->ioaddr + IPG_DMACTRL));

	/*
	 * Acknowledge HostError interrupt by resetting
	 * IPG DMA and HOST.
	 */
	ipg_reset(dev, IPG_AC_GLOBAL_RESET | IPG_AC_HOST | IPG_AC_DMA);

	init_rfdlist(dev);
	init_tfdlist(dev);

	if (ipg_io_config(dev) < 0) {
		printk(KERN_INFO "%s: Cannot recover from PCI error.\n",
		       dev->name);
		schedule_delayed_work(&sp->task, HZ);
	}
}

static irqreturn_t ipg_interrupt_handler(int irq, void *dev_inst)
{
	struct net_device *dev = dev_inst;
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int handled = 0;
	u16 status;

	IPG_DEBUG_MSG("_interrupt_handler\n");

	if (sp->is_jumbo)
		ipg_nic_rxrestore(dev);

	spin_lock(&sp->lock);

	/* Get interrupt source information, and acknowledge
	 * some (i.e. TxDMAComplete, RxDMAComplete, RxEarly,
	 * IntRequested, MacControlFrame, LinkEvent) interrupts
	 * if issued. Also, all IPG interrupts are disabled by
	 * reading IntStatusAck.
	 */
	status = ipg_r16(INT_STATUS_ACK);

	IPG_DEBUG_MSG("IntStatusAck = %4.4x\n", status);

	/* Shared IRQ of remove event. */
	if (!(status & IPG_IS_RSVD_MASK))
		goto out_enable;

	handled = 1;

	if (unlikely(!netif_running(dev)))
		goto out_unlock;

	/* If RFDListEnd interrupt, restore all used RFDs. */
	if (status & IPG_IS_RFD_LIST_END) {
		IPG_DEBUG_MSG("RFDListEnd Interrupt.\n");

		/* The RFD list end indicates an RFD was encountered
		 * with a 0 NextPtr, or with an RFDDone bit set to 1
		 * (indicating the RFD is not read for use by the
		 * IPG.) Try to restore all RFDs.
		 */
		ipg_nic_rxrestore(dev);

#ifdef IPG_DEBUG
		/* Increment the RFDlistendCount counter. */
		sp->RFDlistendCount++;
#endif
	}

	/* If RFDListEnd, RxDMAPriority, RxDMAComplete, or
	 * IntRequested interrupt, process received frames. */
	if ((status & IPG_IS_RX_DMA_PRIORITY) ||
	    (status & IPG_IS_RFD_LIST_END) ||
	    (status & IPG_IS_RX_DMA_COMPLETE) ||
	    (status & IPG_IS_INT_REQUESTED)) {
#ifdef IPG_DEBUG
		/* Increment the RFD list checked counter if interrupted
		 * only to check the RFD list. */
		if (status & (~(IPG_IS_RX_DMA_PRIORITY | IPG_IS_RFD_LIST_END |
				IPG_IS_RX_DMA_COMPLETE | IPG_IS_INT_REQUESTED) &
			       (IPG_IS_HOST_ERROR | IPG_IS_TX_DMA_COMPLETE |
				IPG_IS_LINK_EVENT | IPG_IS_TX_COMPLETE |
				IPG_IS_UPDATE_STATS)))
			sp->RFDListCheckedCount++;
#endif

		if (sp->is_jumbo)
			ipg_nic_rx_jumbo(dev);
		else
			ipg_nic_rx(dev);
	}

	/* If TxDMAComplete interrupt, free used TFDs. */
	if (status & IPG_IS_TX_DMA_COMPLETE)
		ipg_nic_txfree(dev);

	/* TxComplete interrupts indicate one of numerous actions.
	 * Determine what action to take based on TXSTATUS register.
	 */
	if (status & IPG_IS_TX_COMPLETE)
		ipg_nic_txcleanup(dev);

	/* If UpdateStats interrupt, update Linux Ethernet statistics */
	if (status & IPG_IS_UPDATE_STATS)
		ipg_nic_get_stats(dev);

	/* If HostError interrupt, reset IPG. */
	if (status & IPG_IS_HOST_ERROR) {
		IPG_DDEBUG_MSG("HostError Interrupt\n");

		schedule_delayed_work(&sp->task, 0);
	}

	/* If LinkEvent interrupt, resolve autonegotiation. */
	if (status & IPG_IS_LINK_EVENT) {
		if (ipg_config_autoneg(dev) < 0)
			printk(KERN_INFO "%s: Auto-negotiation error.\n",
			       dev->name);
	}

	/* If MACCtrlFrame interrupt, do nothing. */
	if (status & IPG_IS_MAC_CTRL_FRAME)
		IPG_DEBUG_MSG("MACCtrlFrame interrupt.\n");

	/* If RxComplete interrupt, do nothing. */
	if (status & IPG_IS_RX_COMPLETE)
		IPG_DEBUG_MSG("RxComplete interrupt.\n");

	/* If RxEarly interrupt, do nothing. */
	if (status & IPG_IS_RX_EARLY)
		IPG_DEBUG_MSG("RxEarly interrupt.\n");

out_enable:
	/* Re-enable IPG interrupts. */
	ipg_w16(IPG_IE_TX_DMA_COMPLETE | IPG_IE_RX_DMA_COMPLETE |
		IPG_IE_HOST_ERROR | IPG_IE_INT_REQUESTED | IPG_IE_TX_COMPLETE |
		IPG_IE_LINK_EVENT | IPG_IE_UPDATE_STATS, INT_ENABLE);
out_unlock:
	spin_unlock(&sp->lock);

	return IRQ_RETVAL(handled);
}

static void ipg_rx_clear(struct ipg_nic_private *sp)
{
	unsigned int i;

	for (i = 0; i < IPG_RFDLIST_LENGTH; i++) {
		if (sp->rx_buff[i]) {
			struct ipg_rx *rxfd = sp->rxd + i;

			dev_kfree_skb_irq(sp->rx_buff[i]);
			sp->rx_buff[i] = NULL;
			pci_unmap_single(sp->pdev,
				le64_to_cpu(rxfd->frag_info) & ~IPG_RFI_FRAGLEN,
				sp->rx_buf_sz, PCI_DMA_FROMDEVICE);
		}
	}
}

static void ipg_tx_clear(struct ipg_nic_private *sp)
{
	unsigned int i;

	for (i = 0; i < IPG_TFDLIST_LENGTH; i++) {
		if (sp->tx_buff[i]) {
			struct ipg_tx *txfd = sp->txd + i;

			pci_unmap_single(sp->pdev,
				le64_to_cpu(txfd->frag_info) & ~IPG_TFI_FRAGLEN,
				sp->tx_buff[i]->len, PCI_DMA_TODEVICE);

			dev_kfree_skb_irq(sp->tx_buff[i]);

			sp->tx_buff[i] = NULL;
		}
	}
}

static int ipg_nic_open(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	struct pci_dev *pdev = sp->pdev;
	int rc;

	IPG_DEBUG_MSG("_nic_open\n");

	sp->rx_buf_sz = sp->rxsupport_size;

	/* Check for interrupt line conflicts, and request interrupt
	 * line for IPG.
	 *
	 * IMPORTANT: Disable IPG interrupts prior to registering
	 *            IRQ.
	 */
	ipg_w16(0x0000, INT_ENABLE);

	/* Register the interrupt line to be used by the IPG within
	 * the Linux system.
	 */
	rc = request_irq(pdev->irq, &ipg_interrupt_handler, IRQF_SHARED,
			 dev->name, dev);
	if (rc < 0) {
		printk(KERN_INFO "%s: Error when requesting interrupt.\n",
		       dev->name);
		goto out;
	}

	dev->irq = pdev->irq;

	rc = -ENOMEM;

	sp->rxd = dma_alloc_coherent(&pdev->dev, IPG_RX_RING_BYTES,
				     &sp->rxd_map, GFP_KERNEL);
	if (!sp->rxd)
		goto err_free_irq_0;

	sp->txd = dma_alloc_coherent(&pdev->dev, IPG_TX_RING_BYTES,
				     &sp->txd_map, GFP_KERNEL);
	if (!sp->txd)
		goto err_free_rx_1;

	rc = init_rfdlist(dev);
	if (rc < 0) {
		printk(KERN_INFO "%s: Error during configuration.\n",
		       dev->name);
		goto err_free_tx_2;
	}

	init_tfdlist(dev);

	rc = ipg_io_config(dev);
	if (rc < 0) {
		printk(KERN_INFO "%s: Error during configuration.\n",
		       dev->name);
		goto err_release_tfdlist_3;
	}

	/* Resolve autonegotiation. */
	if (ipg_config_autoneg(dev) < 0)
		printk(KERN_INFO "%s: Auto-negotiation error.\n", dev->name);

	/* initialize JUMBO Frame control variable */
	sp->jumbo.found_start = 0;
	sp->jumbo.current_size = 0;
	sp->jumbo.skb = NULL;

	/* Enable transmit and receive operation of the IPG. */
	ipg_w32((ipg_r32(MAC_CTRL) | IPG_MC_RX_ENABLE | IPG_MC_TX_ENABLE) &
		 IPG_MC_RSVD_MASK, MAC_CTRL);

	netif_start_queue(dev);
out:
	return rc;

err_release_tfdlist_3:
	ipg_tx_clear(sp);
	ipg_rx_clear(sp);
err_free_tx_2:
	dma_free_coherent(&pdev->dev, IPG_TX_RING_BYTES, sp->txd, sp->txd_map);
err_free_rx_1:
	dma_free_coherent(&pdev->dev, IPG_RX_RING_BYTES, sp->rxd, sp->rxd_map);
err_free_irq_0:
	free_irq(pdev->irq, dev);
	goto out;
}

static int ipg_nic_stop(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	struct pci_dev *pdev = sp->pdev;

	IPG_DEBUG_MSG("_nic_stop\n");

	netif_stop_queue(dev);

	IPG_DDEBUG_MSG("RFDlistendCount = %i\n", sp->RFDlistendCount);
	IPG_DDEBUG_MSG("RFDListCheckedCount = %i\n", sp->rxdCheckedCount);
	IPG_DDEBUG_MSG("EmptyRFDListCount = %i\n", sp->EmptyRFDListCount);
	IPG_DUMPTFDLIST(dev);

	do {
		(void) ipg_r16(INT_STATUS_ACK);

		ipg_reset(dev, IPG_AC_GLOBAL_RESET | IPG_AC_HOST | IPG_AC_DMA);

		synchronize_irq(pdev->irq);
	} while (ipg_r16(INT_ENABLE) & IPG_IE_RSVD_MASK);

	ipg_rx_clear(sp);

	ipg_tx_clear(sp);

	pci_free_consistent(pdev, IPG_RX_RING_BYTES, sp->rxd, sp->rxd_map);
	pci_free_consistent(pdev, IPG_TX_RING_BYTES, sp->txd, sp->txd_map);

	free_irq(pdev->irq, dev);

	return 0;
}

static int ipg_nic_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int entry = sp->tx_current % IPG_TFDLIST_LENGTH;
	unsigned long flags;
	struct ipg_tx *txfd;

	IPG_DDEBUG_MSG("_nic_hard_start_xmit\n");

	/* If in 10Mbps mode, stop the transmit queue so
	 * no more transmit frames are accepted.
	 */
	if (sp->tenmbpsmode)
		netif_stop_queue(dev);

	if (sp->reset_current_tfd) {
		sp->reset_current_tfd = 0;
		entry = 0;
	}

	txfd = sp->txd + entry;

	sp->tx_buff[entry] = skb;

	/* Clear all TFC fields, except TFDDONE. */
	txfd->tfc = cpu_to_le64(IPG_TFC_TFDDONE);

	/* Specify the TFC field within the TFD. */
	txfd->tfc |= cpu_to_le64(IPG_TFC_WORDALIGNDISABLED |
		(IPG_TFC_FRAMEID & sp->tx_current) |
		(IPG_TFC_FRAGCOUNT & (1 << 24)));
	/*
	 * 16--17 (WordAlign) <- 3 (disable),
	 * 0--15 (FrameId) <- sp->tx_current,
	 * 24--27 (FragCount) <- 1
	 */

	/* Request TxComplete interrupts at an interval defined
	 * by the constant IPG_FRAMESBETWEENTXCOMPLETES.
	 * Request TxComplete interrupt for every frame
	 * if in 10Mbps mode to accomodate problem with 10Mbps
	 * processing.
	 */
	if (sp->tenmbpsmode)
		txfd->tfc |= cpu_to_le64(IPG_TFC_TXINDICATE);
	txfd->tfc |= cpu_to_le64(IPG_TFC_TXDMAINDICATE);
	/* Based on compilation option, determine if FCS is to be
	 * appended to transmit frame by IPG.
	 */
	if (!(IPG_APPEND_FCS_ON_TX))
		txfd->tfc |= cpu_to_le64(IPG_TFC_FCSAPPENDDISABLE);

	/* Based on compilation option, determine if IP, TCP and/or
	 * UDP checksums are to be added to transmit frame by IPG.
	 */
	if (IPG_ADD_IPCHECKSUM_ON_TX)
		txfd->tfc |= cpu_to_le64(IPG_TFC_IPCHECKSUMENABLE);

	if (IPG_ADD_TCPCHECKSUM_ON_TX)
		txfd->tfc |= cpu_to_le64(IPG_TFC_TCPCHECKSUMENABLE);

	if (IPG_ADD_UDPCHECKSUM_ON_TX)
		txfd->tfc |= cpu_to_le64(IPG_TFC_UDPCHECKSUMENABLE);

	/* Based on compilation option, determine if VLAN tag info is to be
	 * inserted into transmit frame by IPG.
	 */
	if (IPG_INSERT_MANUAL_VLAN_TAG) {
		txfd->tfc |= cpu_to_le64(IPG_TFC_VLANTAGINSERT |
			((u64) IPG_MANUAL_VLAN_VID << 32) |
			((u64) IPG_MANUAL_VLAN_CFI << 44) |
			((u64) IPG_MANUAL_VLAN_USERPRIORITY << 45));
	}

	/* The fragment start location within system memory is defined
	 * by the sk_buff structure's data field. The physical address
	 * of this location within the system's virtual memory space
	 * is determined using the IPG_HOST2BUS_MAP function.
	 */
	txfd->frag_info = cpu_to_le64(pci_map_single(sp->pdev, skb->data,
		skb->len, PCI_DMA_TODEVICE));

	/* The length of the fragment within system memory is defined by
	 * the sk_buff structure's len field.
	 */
	txfd->frag_info |= cpu_to_le64(IPG_TFI_FRAGLEN &
		((u64) (skb->len & 0xffff) << 48));

	/* Clear the TFDDone bit last to indicate the TFD is ready
	 * for transfer to the IPG.
	 */
	txfd->tfc &= cpu_to_le64(~IPG_TFC_TFDDONE);

	spin_lock_irqsave(&sp->lock, flags);

	sp->tx_current++;

	mmiowb();

	ipg_w32(IPG_DC_TX_DMA_POLL_NOW, DMA_CTRL);

	if (sp->tx_current == (sp->tx_dirty + IPG_TFDLIST_LENGTH))
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&sp->lock, flags);

	return NETDEV_TX_OK;
}

static void ipg_set_phy_default_param(unsigned char rev,
				      struct net_device *dev, int phy_address)
{
	unsigned short length;
	unsigned char revision;
	unsigned short *phy_param;
	unsigned short address, value;

	phy_param = &DefaultPhyParam[0];
	length = *phy_param & 0x00FF;
	revision = (unsigned char)((*phy_param) >> 8);
	phy_param++;
	while (length != 0) {
		if (rev == revision) {
			while (length > 1) {
				address = *phy_param;
				value = *(phy_param + 1);
				phy_param += 2;
				mdio_write(dev, phy_address, address, value);
				length -= 4;
			}
			break;
		} else {
			phy_param += length / 2;
			length = *phy_param & 0x00FF;
			revision = (unsigned char)((*phy_param) >> 8);
			phy_param++;
		}
	}
}

static int read_eeprom(struct net_device *dev, int eep_addr)
{
	void __iomem *ioaddr = ipg_ioaddr(dev);
	unsigned int i;
	int ret = 0;
	u16 value;

	value = IPG_EC_EEPROM_READOPCODE | (eep_addr & 0xff);
	ipg_w16(value, EEPROM_CTRL);

	for (i = 0; i < 1000; i++) {
		u16 data;

		mdelay(10);
		data = ipg_r16(EEPROM_CTRL);
		if (!(data & IPG_EC_EEPROM_BUSY)) {
			ret = ipg_r16(EEPROM_DATA);
			break;
		}
	}
	return ret;
}

static void ipg_init_mii(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	struct mii_if_info *mii_if = &sp->mii_if;
	int phyaddr;

	mii_if->dev          = dev;
	mii_if->mdio_read    = mdio_read;
	mii_if->mdio_write   = mdio_write;
	mii_if->phy_id_mask  = 0x1f;
	mii_if->reg_num_mask = 0x1f;

	mii_if->phy_id = phyaddr = ipg_find_phyaddr(dev);

	if (phyaddr != 0x1f) {
		u16 mii_phyctrl, mii_1000cr;
		u8 revisionid = 0;

		mii_1000cr  = mdio_read(dev, phyaddr, MII_CTRL1000);
		mii_1000cr |= ADVERTISE_1000FULL | ADVERTISE_1000HALF |
			GMII_PHY_1000BASETCONTROL_PreferMaster;
		mdio_write(dev, phyaddr, MII_CTRL1000, mii_1000cr);

		mii_phyctrl = mdio_read(dev, phyaddr, MII_BMCR);

		/* Set default phyparam */
		pci_read_config_byte(sp->pdev, PCI_REVISION_ID, &revisionid);
		ipg_set_phy_default_param(revisionid, dev, phyaddr);

		/* Reset PHY */
		mii_phyctrl |= BMCR_RESET | BMCR_ANRESTART;
		mdio_write(dev, phyaddr, MII_BMCR, mii_phyctrl);

	}
}

static int ipg_hw_init(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	void __iomem *ioaddr = sp->ioaddr;
	unsigned int i;
	int rc;

	/* Read/Write and Reset EEPROM Value */
	/* Read LED Mode Configuration from EEPROM */
	sp->led_mode = read_eeprom(dev, 6);

	/* Reset all functions within the IPG. Do not assert
	 * RST_OUT as not compatible with some PHYs.
	 */
	rc = ipg_reset(dev, IPG_RESET_MASK);
	if (rc < 0)
		goto out;

	ipg_init_mii(dev);

	/* Read MAC Address from EEPROM */
	for (i = 0; i < 3; i++)
		sp->station_addr[i] = read_eeprom(dev, 16 + i);

	for (i = 0; i < 3; i++)
		ipg_w16(sp->station_addr[i], STATION_ADDRESS_0 + 2*i);

	/* Set station address in ethernet_device structure. */
	dev->dev_addr[0] =  ipg_r16(STATION_ADDRESS_0) & 0x00ff;
	dev->dev_addr[1] = (ipg_r16(STATION_ADDRESS_0) & 0xff00) >> 8;
	dev->dev_addr[2] =  ipg_r16(STATION_ADDRESS_1) & 0x00ff;
	dev->dev_addr[3] = (ipg_r16(STATION_ADDRESS_1) & 0xff00) >> 8;
	dev->dev_addr[4] =  ipg_r16(STATION_ADDRESS_2) & 0x00ff;
	dev->dev_addr[5] = (ipg_r16(STATION_ADDRESS_2) & 0xff00) >> 8;
out:
	return rc;
}

static int ipg_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	int rc;

	mutex_lock(&sp->mii_mutex);
	rc = generic_mii_ioctl(&sp->mii_if, if_mii(ifr), cmd, NULL);
	mutex_unlock(&sp->mii_mutex);

	return rc;
}

static int ipg_nic_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	int err;

	/* Function to accomodate changes to Maximum Transfer Unit
	 * (or MTU) of IPG NIC. Cannot use default function since
	 * the default will not allow for MTU > 1500 bytes.
	 */

	IPG_DEBUG_MSG("_nic_change_mtu\n");

	/*
	 * Check that the new MTU value is between 68 (14 byte header, 46 byte
	 * payload, 4 byte FCS) and 10 KB, which is the largest supported MTU.
	 */
	if (new_mtu < 68 || new_mtu > 10240)
		return -EINVAL;

	err = ipg_nic_stop(dev);
	if (err)
		return err;

	dev->mtu = new_mtu;

	sp->max_rxframe_size = new_mtu;

	sp->rxfrag_size = new_mtu;
	if (sp->rxfrag_size > 4088)
		sp->rxfrag_size = 4088;

	sp->rxsupport_size = sp->max_rxframe_size;

	if (new_mtu > 0x0600)
		sp->is_jumbo = true;
	else
		sp->is_jumbo = false;

	return ipg_nic_open(dev);
}

static int ipg_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	int rc;

	mutex_lock(&sp->mii_mutex);
	rc = mii_ethtool_gset(&sp->mii_if, cmd);
	mutex_unlock(&sp->mii_mutex);

	return rc;
}

static int ipg_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	int rc;

	mutex_lock(&sp->mii_mutex);
	rc = mii_ethtool_sset(&sp->mii_if, cmd);
	mutex_unlock(&sp->mii_mutex);

	return rc;
}

static int ipg_nway_reset(struct net_device *dev)
{
	struct ipg_nic_private *sp = netdev_priv(dev);
	int rc;

	mutex_lock(&sp->mii_mutex);
	rc = mii_nway_restart(&sp->mii_if);
	mutex_unlock(&sp->mii_mutex);

	return rc;
}

static struct ethtool_ops ipg_ethtool_ops = {
	.get_settings = ipg_get_settings,
	.set_settings = ipg_set_settings,
	.nway_reset   = ipg_nway_reset,
};

static void __devexit ipg_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct ipg_nic_private *sp = netdev_priv(dev);

	IPG_DEBUG_MSG("_remove\n");

	/* Un-register Ethernet device. */
	unregister_netdev(dev);

	pci_iounmap(pdev, sp->ioaddr);

	pci_release_regions(pdev);

	free_netdev(dev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static const struct net_device_ops ipg_netdev_ops = {
	.ndo_open		= ipg_nic_open,
	.ndo_stop		= ipg_nic_stop,
	.ndo_start_xmit		= ipg_nic_hard_start_xmit,
	.ndo_get_stats		= ipg_nic_get_stats,
	.ndo_set_multicast_list = ipg_nic_set_multicast_list,
	.ndo_do_ioctl		= ipg_ioctl,
	.ndo_tx_timeout 	= ipg_tx_timeout,
	.ndo_change_mtu 	= ipg_nic_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __devinit ipg_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	unsigned int i = id->driver_data;
	struct ipg_nic_private *sp;
	struct net_device *dev;
	void __iomem *ioaddr;
	int rc;

	rc = pci_enable_device(pdev);
	if (rc < 0)
		goto out;

	printk(KERN_INFO "%s: %s\n", pci_name(pdev), ipg_brand_name[i]);

	pci_set_master(pdev);

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(40));
	if (rc < 0) {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc < 0) {
			printk(KERN_ERR "%s: DMA config failed.\n",
			       pci_name(pdev));
			goto err_disable_0;
		}
	}

	/*
	 * Initialize net device.
	 */
	dev = alloc_etherdev(sizeof(struct ipg_nic_private));
	if (!dev) {
		printk(KERN_ERR "%s: alloc_etherdev failed\n", pci_name(pdev));
		rc = -ENOMEM;
		goto err_disable_0;
	}

	sp = netdev_priv(dev);
	spin_lock_init(&sp->lock);
	mutex_init(&sp->mii_mutex);

	sp->is_jumbo = IPG_IS_JUMBO;
	sp->rxfrag_size = IPG_RXFRAG_SIZE;
	sp->rxsupport_size = IPG_RXSUPPORT_SIZE;
	sp->max_rxframe_size = IPG_MAX_RXFRAME_SIZE;

	/* Declare IPG NIC functions for Ethernet device methods.
	 */
	dev->netdev_ops = &ipg_netdev_ops;
	SET_NETDEV_DEV(dev, &pdev->dev);
	SET_ETHTOOL_OPS(dev, &ipg_ethtool_ops);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_free_dev_1;

	ioaddr = pci_iomap(pdev, 1, pci_resource_len(pdev, 1));
	if (!ioaddr) {
		printk(KERN_ERR "%s cannot map MMIO\n", pci_name(pdev));
		rc = -EIO;
		goto err_release_regions_2;
	}

	/* Save the pointer to the PCI device information. */
	sp->ioaddr = ioaddr;
	sp->pdev = pdev;
	sp->dev = dev;

	INIT_DELAYED_WORK(&sp->task, ipg_reset_after_host_error);

	pci_set_drvdata(pdev, dev);

	rc = ipg_hw_init(dev);
	if (rc < 0)
		goto err_unmap_3;

	rc = register_netdev(dev);
	if (rc < 0)
		goto err_unmap_3;

	printk(KERN_INFO "Ethernet device registered as: %s\n", dev->name);
out:
	return rc;

err_unmap_3:
	pci_iounmap(pdev, ioaddr);
err_release_regions_2:
	pci_release_regions(pdev);
err_free_dev_1:
	free_netdev(dev);
err_disable_0:
	pci_disable_device(pdev);
	goto out;
}

static struct pci_driver ipg_pci_driver = {
	.name		= IPG_DRIVER_NAME,
	.id_table	= ipg_pci_tbl,
	.probe		= ipg_probe,
	.remove		= __devexit_p(ipg_remove),
};

static int __init ipg_init_module(void)
{
	return pci_register_driver(&ipg_pci_driver);
}

static void __exit ipg_exit_module(void)
{
	pci_unregister_driver(&ipg_pci_driver);
}

module_init(ipg_init_module);
module_exit(ipg_exit_module);

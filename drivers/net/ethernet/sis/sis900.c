/* sis900.c: A SiS 900/7016 PCI Fast Ethernet driver for Linux.
   Copyright 1999 Silicon Integrated System Corporation
   Revision:	1.08.10 Apr. 2 2006

   Modified from the driver which is originally written by Donald Becker.

   This software may be used and distributed according to the terms
   of the GNU General Public License (GPL), incorporated herein by reference.
   Drivers based on this skeleton fall under the GPL and must retain
   the authorship (implicit copyright) notice.

   References:
   SiS 7016 Fast Ethernet PCI Bus 10/100 Mbps LAN Controller with OnNow Support,
   preliminary Rev. 1.0 Jan. 14, 1998
   SiS 900 Fast Ethernet PCI Bus 10/100 Mbps LAN Single Chip with OnNow Support,
   preliminary Rev. 1.0 Nov. 10, 1998
   SiS 7014 Single Chip 100BASE-TX/10BASE-T Physical Layer Solution,
   preliminary Rev. 1.0 Jan. 18, 1998

   Rev 1.08.10 Apr.  2 2006 Daniele Venzano add vlan (jumbo packets) support
   Rev 1.08.09 Sep. 19 2005 Daniele Venzano add Wake on LAN support
   Rev 1.08.08 Jan. 22 2005 Daniele Venzano use netif_msg for debugging messages
   Rev 1.08.07 Nov.  2 2003 Daniele Venzano <venza@brownhat.org> add suspend/resume support
   Rev 1.08.06 Sep. 24 2002 Mufasa Yang bug fix for Tx timeout & add SiS963 support
   Rev 1.08.05 Jun.  6 2002 Mufasa Yang bug fix for read_eeprom & Tx descriptor over-boundary
   Rev 1.08.04 Apr. 25 2002 Mufasa Yang <mufasa@sis.com.tw> added SiS962 support
   Rev 1.08.03 Feb.  1 2002 Matt Domsch <Matt_Domsch@dell.com> update to use library crc32 function
   Rev 1.08.02 Nov. 30 2001 Hui-Fen Hsu workaround for EDB & bug fix for dhcp problem
   Rev 1.08.01 Aug. 25 2001 Hui-Fen Hsu update for 630ET & workaround for ICS1893 PHY
   Rev 1.08.00 Jun. 11 2001 Hui-Fen Hsu workaround for RTL8201 PHY and some bug fix
   Rev 1.07.11 Apr.  2 2001 Hui-Fen Hsu updates PCI drivers to use the new pci_set_dma_mask for kernel 2.4.3
   Rev 1.07.10 Mar.  1 2001 Hui-Fen Hsu <hfhsu@sis.com.tw> some bug fix & 635M/B support
   Rev 1.07.09 Feb.  9 2001 Dave Jones <davej@suse.de> PCI enable cleanup
   Rev 1.07.08 Jan.  8 2001 Lei-Chun Chang added RTL8201 PHY support
   Rev 1.07.07 Nov. 29 2000 Lei-Chun Chang added kernel-doc extractable documentation and 630 workaround fix
   Rev 1.07.06 Nov.  7 2000 Jeff Garzik <jgarzik@pobox.com> some bug fix and cleaning
   Rev 1.07.05 Nov.  6 2000 metapirat<metapirat@gmx.de> contribute media type select by ifconfig
   Rev 1.07.04 Sep.  6 2000 Lei-Chun Chang added ICS1893 PHY support
   Rev 1.07.03 Aug. 24 2000 Lei-Chun Chang (lcchang@sis.com.tw) modified 630E equalizer workaround rule
   Rev 1.07.01 Aug. 08 2000 Ollie Lho minor update for SiS 630E and SiS 630E A1
   Rev 1.07    Mar. 07 2000 Ollie Lho bug fix in Rx buffer ring
   Rev 1.06.04 Feb. 11 2000 Jeff Garzik <jgarzik@pobox.com> softnet and init for kernel 2.4
   Rev 1.06.03 Dec. 23 1999 Ollie Lho Third release
   Rev 1.06.02 Nov. 23 1999 Ollie Lho bug in mac probing fixed
   Rev 1.06.01 Nov. 16 1999 Ollie Lho CRC calculation provide by Joseph Zbiciak (im14u2c@primenet.com)
   Rev 1.06 Nov. 4 1999 Ollie Lho (ollie@sis.com.tw) Second release
   Rev 1.05.05 Oct. 29 1999 Ollie Lho (ollie@sis.com.tw) Single buffer Tx/Rx
   Chin-Shan Li (lcs@sis.com.tw) Added AMD Am79c901 HomePNA PHY support
   Rev 1.05 Aug. 7 1999 Jim Huang (cmhuang@sis.com.tw) Initial release
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>

#include <asm/processor.h>      /* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>	/* User space memory access functions */

#include "sis900.h"

#define SIS900_MODULE_NAME "sis900"
#define SIS900_DRV_VERSION "v1.08.10 Apr. 2 2006"

static const char version[] =
	KERN_INFO "sis900.c: " SIS900_DRV_VERSION "\n";

static int max_interrupt_work = 40;
static int multicast_filter_limit = 128;

static int sis900_debug = -1; /* Use SIS900_DEF_MSG as value */

#define SIS900_DEF_MSG \
	(NETIF_MSG_DRV		| \
	 NETIF_MSG_LINK		| \
	 NETIF_MSG_RX_ERR	| \
	 NETIF_MSG_TX_ERR)

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (4*HZ)

enum {
	SIS_900 = 0,
	SIS_7016
};
static const char * card_names[] = {
	"SiS 900 PCI Fast Ethernet",
	"SiS 7016 PCI Fast Ethernet"
};

static const struct pci_device_id sis900_pci_tbl[] = {
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_900,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, SIS_900},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7016,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, SIS_7016},
	{0,}
};
MODULE_DEVICE_TABLE (pci, sis900_pci_tbl);

static void sis900_read_mode(struct net_device *net_dev, int *speed, int *duplex);

static const struct mii_chip_info {
	const char * name;
	u16 phy_id0;
	u16 phy_id1;
	u8  phy_types;
#define	HOME 	0x0001
#define LAN	0x0002
#define MIX	0x0003
#define UNKNOWN	0x0
} mii_chip_table[] = {
	{ "SiS 900 Internal MII PHY", 		0x001d, 0x8000, LAN },
	{ "SiS 7014 Physical Layer Solution", 	0x0016, 0xf830, LAN },
	{ "SiS 900 on Foxconn 661 7MI",         0x0143, 0xBC70, LAN },
	{ "Altimata AC101LF PHY",               0x0022, 0x5520, LAN },
	{ "ADM 7001 LAN PHY",			0x002e, 0xcc60, LAN },
	{ "AMD 79C901 10BASE-T PHY",  		0x0000, 0x6B70, LAN },
	{ "AMD 79C901 HomePNA PHY",		0x0000, 0x6B90, HOME},
	{ "ICS LAN PHY",			0x0015, 0xF440, LAN },
	{ "ICS LAN PHY",			0x0143, 0xBC70, LAN },
	{ "NS 83851 PHY",			0x2000, 0x5C20, MIX },
	{ "NS 83847 PHY",                       0x2000, 0x5C30, MIX },
	{ "Realtek RTL8201 PHY",		0x0000, 0x8200, LAN },
	{ "VIA 6103 PHY",			0x0101, 0x8f20, LAN },
	{NULL,},
};

struct mii_phy {
	struct mii_phy * next;
	int phy_addr;
	u16 phy_id0;
	u16 phy_id1;
	u16 status;
	u8  phy_types;
};

typedef struct _BufferDesc {
	u32 link;
	u32 cmdsts;
	u32 bufptr;
} BufferDesc;

struct sis900_private {
	struct pci_dev * pci_dev;

	spinlock_t lock;

	struct mii_phy * mii;
	struct mii_phy * first_mii; /* record the first mii structure */
	unsigned int cur_phy;
	struct mii_if_info mii_info;

	void __iomem	*ioaddr;

	struct timer_list timer; /* Link status detection timer. */
	u8 autong_complete; /* 1: auto-negotiate complete  */

	u32 msg_enable;

	unsigned int cur_rx, dirty_rx; /* producer/consumer pointers for Tx/Rx ring */
	unsigned int cur_tx, dirty_tx;

	/* The saved address of a sent/receive-in-place packet buffer */
	struct sk_buff *tx_skbuff[NUM_TX_DESC];
	struct sk_buff *rx_skbuff[NUM_RX_DESC];
	BufferDesc *tx_ring;
	BufferDesc *rx_ring;

	dma_addr_t tx_ring_dma;
	dma_addr_t rx_ring_dma;

	unsigned int tx_full; /* The Tx queue is full. */
	u8 host_bridge_rev;
	u8 chipset_rev;
	/* EEPROM data */
	int eeprom_size;
};

MODULE_AUTHOR("Jim Huang <cmhuang@sis.com.tw>, Ollie Lho <ollie@sis.com.tw>");
MODULE_DESCRIPTION("SiS 900 PCI Fast Ethernet driver");
MODULE_LICENSE("GPL");

module_param(multicast_filter_limit, int, 0444);
module_param(max_interrupt_work, int, 0444);
module_param(sis900_debug, int, 0444);
MODULE_PARM_DESC(multicast_filter_limit, "SiS 900/7016 maximum number of filtered multicast addresses");
MODULE_PARM_DESC(max_interrupt_work, "SiS 900/7016 maximum events handled per interrupt");
MODULE_PARM_DESC(sis900_debug, "SiS 900/7016 bitmapped debugging message level");

#define sw32(reg, val)	iowrite32(val, ioaddr + (reg))
#define sw8(reg, val)	iowrite8(val, ioaddr + (reg))
#define sr32(reg)	ioread32(ioaddr + (reg))
#define sr16(reg)	ioread16(ioaddr + (reg))

#ifdef CONFIG_NET_POLL_CONTROLLER
static void sis900_poll(struct net_device *dev);
#endif
static int sis900_open(struct net_device *net_dev);
static int sis900_mii_probe (struct net_device * net_dev);
static void sis900_init_rxfilter (struct net_device * net_dev);
static u16 read_eeprom(void __iomem *ioaddr, int location);
static int mdio_read(struct net_device *net_dev, int phy_id, int location);
static void mdio_write(struct net_device *net_dev, int phy_id, int location, int val);
static void sis900_timer(struct timer_list *t);
static void sis900_check_mode (struct net_device *net_dev, struct mii_phy *mii_phy);
static void sis900_tx_timeout(struct net_device *net_dev, unsigned int txqueue);
static void sis900_init_tx_ring(struct net_device *net_dev);
static void sis900_init_rx_ring(struct net_device *net_dev);
static netdev_tx_t sis900_start_xmit(struct sk_buff *skb,
				     struct net_device *net_dev);
static int sis900_rx(struct net_device *net_dev);
static void sis900_finish_xmit (struct net_device *net_dev);
static irqreturn_t sis900_interrupt(int irq, void *dev_instance);
static int sis900_close(struct net_device *net_dev);
static int mii_ioctl(struct net_device *net_dev, struct ifreq *rq, int cmd);
static u16 sis900_mcast_bitnr(u8 *addr, u8 revision);
static void set_rx_mode(struct net_device *net_dev);
static void sis900_reset(struct net_device *net_dev);
static void sis630_set_eq(struct net_device *net_dev, u8 revision);
static int sis900_set_config(struct net_device *dev, struct ifmap *map);
static u16 sis900_default_phy(struct net_device * net_dev);
static void sis900_set_capability( struct net_device *net_dev ,struct mii_phy *phy);
static u16 sis900_reset_phy(struct net_device *net_dev, int phy_addr);
static void sis900_auto_negotiate(struct net_device *net_dev, int phy_addr);
static void sis900_set_mode(struct sis900_private *, int speed, int duplex);
static const struct ethtool_ops sis900_ethtool_ops;

/**
 *	sis900_get_mac_addr - Get MAC address for stand alone SiS900 model
 *	@pci_dev: the sis900 pci device
 *	@net_dev: the net device to get address for
 *
 *	Older SiS900 and friends, use EEPROM to store MAC address.
 *	MAC address is read from read_eeprom() into @net_dev->dev_addr.
 */

static int sis900_get_mac_addr(struct pci_dev *pci_dev,
			       struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	u16 signature;
	int i;

	/* check to see if we have sane EEPROM */
	signature = (u16) read_eeprom(ioaddr, EEPROMSignature);
	if (signature == 0xffff || signature == 0x0000) {
		printk (KERN_WARNING "%s: Error EEPROM read %x\n",
			pci_name(pci_dev), signature);
		return 0;
	}

	/* get MAC address from EEPROM */
	for (i = 0; i < 3; i++)
	        ((u16 *)(net_dev->dev_addr))[i] = read_eeprom(ioaddr, i+EEPROMMACAddr);

	return 1;
}

/**
 *	sis630e_get_mac_addr - Get MAC address for SiS630E model
 *	@pci_dev: the sis900 pci device
 *	@net_dev: the net device to get address for
 *
 *	SiS630E model, use APC CMOS RAM to store MAC address.
 *	APC CMOS RAM is accessed through ISA bridge.
 *	MAC address is read into @net_dev->dev_addr.
 */

static int sis630e_get_mac_addr(struct pci_dev *pci_dev,
				struct net_device *net_dev)
{
	struct pci_dev *isa_bridge = NULL;
	u8 reg;
	int i;

	isa_bridge = pci_get_device(PCI_VENDOR_ID_SI, 0x0008, isa_bridge);
	if (!isa_bridge)
		isa_bridge = pci_get_device(PCI_VENDOR_ID_SI, 0x0018, isa_bridge);
	if (!isa_bridge) {
		printk(KERN_WARNING "%s: Can not find ISA bridge\n",
		       pci_name(pci_dev));
		return 0;
	}
	pci_read_config_byte(isa_bridge, 0x48, &reg);
	pci_write_config_byte(isa_bridge, 0x48, reg | 0x40);

	for (i = 0; i < 6; i++) {
		outb(0x09 + i, 0x70);
		((u8 *)(net_dev->dev_addr))[i] = inb(0x71);
	}

	pci_write_config_byte(isa_bridge, 0x48, reg & ~0x40);
	pci_dev_put(isa_bridge);

	return 1;
}


/**
 *	sis635_get_mac_addr - Get MAC address for SIS635 model
 *	@pci_dev: the sis900 pci device
 *	@net_dev: the net device to get address for
 *
 *	SiS635 model, set MAC Reload Bit to load Mac address from APC
 *	to rfdr. rfdr is accessed through rfcr. MAC address is read into
 *	@net_dev->dev_addr.
 */

static int sis635_get_mac_addr(struct pci_dev *pci_dev,
			       struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	u32 rfcrSave;
	u32 i;

	rfcrSave = sr32(rfcr);

	sw32(cr, rfcrSave | RELOAD);
	sw32(cr, 0);

	/* disable packet filtering before setting filter */
	sw32(rfcr, rfcrSave & ~RFEN);

	/* load MAC addr to filter data register */
	for (i = 0 ; i < 3 ; i++) {
		sw32(rfcr, (i << RFADDR_shift));
		*( ((u16 *)net_dev->dev_addr) + i) = sr16(rfdr);
	}

	/* enable packet filtering */
	sw32(rfcr, rfcrSave | RFEN);

	return 1;
}

/**
 *	sis96x_get_mac_addr - Get MAC address for SiS962 or SiS963 model
 *	@pci_dev: the sis900 pci device
 *	@net_dev: the net device to get address for
 *
 *	SiS962 or SiS963 model, use EEPROM to store MAC address. And EEPROM
 *	is shared by
 *	LAN and 1394. When accessing EEPROM, send EEREQ signal to hardware first
 *	and wait for EEGNT. If EEGNT is ON, EEPROM is permitted to be accessed
 *	by LAN, otherwise it is not. After MAC address is read from EEPROM, send
 *	EEDONE signal to refuse EEPROM access by LAN.
 *	The EEPROM map of SiS962 or SiS963 is different to SiS900.
 *	The signature field in SiS962 or SiS963 spec is meaningless.
 *	MAC address is read into @net_dev->dev_addr.
 */

static int sis96x_get_mac_addr(struct pci_dev *pci_dev,
			       struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	int wait, rc = 0;

	sw32(mear, EEREQ);
	for (wait = 0; wait < 2000; wait++) {
		if (sr32(mear) & EEGNT) {
			u16 *mac = (u16 *)net_dev->dev_addr;
			int i;

			/* get MAC address from EEPROM */
			for (i = 0; i < 3; i++)
			        mac[i] = read_eeprom(ioaddr, i + EEPROMMACAddr);

			rc = 1;
			break;
		}
		udelay(1);
	}
	sw32(mear, EEDONE);
	return rc;
}

static const struct net_device_ops sis900_netdev_ops = {
	.ndo_open		 = sis900_open,
	.ndo_stop		= sis900_close,
	.ndo_start_xmit		= sis900_start_xmit,
	.ndo_set_config		= sis900_set_config,
	.ndo_set_rx_mode	= set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_do_ioctl		= mii_ioctl,
	.ndo_tx_timeout		= sis900_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
        .ndo_poll_controller	= sis900_poll,
#endif
};

/**
 *	sis900_probe - Probe for sis900 device
 *	@pci_dev: the sis900 pci device
 *	@pci_id: the pci device ID
 *
 *	Check and probe sis900 net device for @pci_dev.
 *	Get mac address according to the chip revision,
 *	and assign SiS900-specific entries in the device structure.
 *	ie: sis900_open(), sis900_start_xmit(), sis900_close(), etc.
 */

static int sis900_probe(struct pci_dev *pci_dev,
			const struct pci_device_id *pci_id)
{
	struct sis900_private *sis_priv;
	struct net_device *net_dev;
	struct pci_dev *dev;
	dma_addr_t ring_dma;
	void *ring_space;
	void __iomem *ioaddr;
	int i, ret;
	const char *card_name = card_names[pci_id->driver_data];
	const char *dev_name = pci_name(pci_dev);

/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif

	/* setup various bits in PCI command register */
	ret = pci_enable_device(pci_dev);
	if(ret) return ret;

	i = dma_set_mask(&pci_dev->dev, DMA_BIT_MASK(32));
	if(i){
		printk(KERN_ERR "sis900.c: architecture does not support "
			"32bit PCI busmaster DMA\n");
		return i;
	}

	pci_set_master(pci_dev);

	net_dev = alloc_etherdev(sizeof(struct sis900_private));
	if (!net_dev)
		return -ENOMEM;
	SET_NETDEV_DEV(net_dev, &pci_dev->dev);

	/* We do a request_region() to register /proc/ioports info. */
	ret = pci_request_regions(pci_dev, "sis900");
	if (ret)
		goto err_out;

	/* IO region. */
	ioaddr = pci_iomap(pci_dev, 0, 0);
	if (!ioaddr) {
		ret = -ENOMEM;
		goto err_out_cleardev;
	}

	sis_priv = netdev_priv(net_dev);
	sis_priv->ioaddr = ioaddr;
	sis_priv->pci_dev = pci_dev;
	spin_lock_init(&sis_priv->lock);

	sis_priv->eeprom_size = 24;

	pci_set_drvdata(pci_dev, net_dev);

	ring_space = dma_alloc_coherent(&pci_dev->dev, TX_TOTAL_SIZE,
					&ring_dma, GFP_KERNEL);
	if (!ring_space) {
		ret = -ENOMEM;
		goto err_out_unmap;
	}
	sis_priv->tx_ring = ring_space;
	sis_priv->tx_ring_dma = ring_dma;

	ring_space = dma_alloc_coherent(&pci_dev->dev, RX_TOTAL_SIZE,
					&ring_dma, GFP_KERNEL);
	if (!ring_space) {
		ret = -ENOMEM;
		goto err_unmap_tx;
	}
	sis_priv->rx_ring = ring_space;
	sis_priv->rx_ring_dma = ring_dma;

	/* The SiS900-specific entries in the device structure. */
	net_dev->netdev_ops = &sis900_netdev_ops;
	net_dev->watchdog_timeo = TX_TIMEOUT;
	net_dev->ethtool_ops = &sis900_ethtool_ops;

	if (sis900_debug > 0)
		sis_priv->msg_enable = sis900_debug;
	else
		sis_priv->msg_enable = SIS900_DEF_MSG;

	sis_priv->mii_info.dev = net_dev;
	sis_priv->mii_info.mdio_read = mdio_read;
	sis_priv->mii_info.mdio_write = mdio_write;
	sis_priv->mii_info.phy_id_mask = 0x1f;
	sis_priv->mii_info.reg_num_mask = 0x1f;

	/* Get Mac address according to the chip revision */
	sis_priv->chipset_rev = pci_dev->revision;
	if(netif_msg_probe(sis_priv))
		printk(KERN_DEBUG "%s: detected revision %2.2x, "
				"trying to get MAC address...\n",
				dev_name, sis_priv->chipset_rev);

	ret = 0;
	if (sis_priv->chipset_rev == SIS630E_900_REV)
		ret = sis630e_get_mac_addr(pci_dev, net_dev);
	else if ((sis_priv->chipset_rev > 0x81) && (sis_priv->chipset_rev <= 0x90) )
		ret = sis635_get_mac_addr(pci_dev, net_dev);
	else if (sis_priv->chipset_rev == SIS96x_900_REV)
		ret = sis96x_get_mac_addr(pci_dev, net_dev);
	else
		ret = sis900_get_mac_addr(pci_dev, net_dev);

	if (!ret || !is_valid_ether_addr(net_dev->dev_addr)) {
		eth_hw_addr_random(net_dev);
		printk(KERN_WARNING "%s: Unreadable or invalid MAC address,"
				"using random generated one\n", dev_name);
	}

	/* 630ET : set the mii access mode as software-mode */
	if (sis_priv->chipset_rev == SIS630ET_900_REV)
		sw32(cr, ACCESSMODE | sr32(cr));

	/* probe for mii transceiver */
	if (sis900_mii_probe(net_dev) == 0) {
		printk(KERN_WARNING "%s: Error probing MII device.\n",
		       dev_name);
		ret = -ENODEV;
		goto err_unmap_rx;
	}

	/* save our host bridge revision */
	dev = pci_get_device(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_630, NULL);
	if (dev) {
		sis_priv->host_bridge_rev = dev->revision;
		pci_dev_put(dev);
	}

	ret = register_netdev(net_dev);
	if (ret)
		goto err_unmap_rx;

	/* print some information about our NIC */
	printk(KERN_INFO "%s: %s at 0x%p, IRQ %d, %pM\n",
	       net_dev->name, card_name, ioaddr, pci_dev->irq,
	       net_dev->dev_addr);

	/* Detect Wake on Lan support */
	ret = (sr32(CFGPMC) & PMESP) >> 27;
	if (netif_msg_probe(sis_priv) && (ret & PME_D3C) == 0)
		printk(KERN_INFO "%s: Wake on LAN only available from suspend to RAM.", net_dev->name);

	return 0;

err_unmap_rx:
	dma_free_coherent(&pci_dev->dev, RX_TOTAL_SIZE, sis_priv->rx_ring,
			  sis_priv->rx_ring_dma);
err_unmap_tx:
	dma_free_coherent(&pci_dev->dev, TX_TOTAL_SIZE, sis_priv->tx_ring,
			  sis_priv->tx_ring_dma);
err_out_unmap:
	pci_iounmap(pci_dev, ioaddr);
err_out_cleardev:
	pci_release_regions(pci_dev);
 err_out:
	free_netdev(net_dev);
	return ret;
}

/**
 *	sis900_mii_probe - Probe MII PHY for sis900
 *	@net_dev: the net device to probe for
 *
 *	Search for total of 32 possible mii phy addresses.
 *	Identify and set current phy if found one,
 *	return error if it failed to found.
 */

static int sis900_mii_probe(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	const char *dev_name = pci_name(sis_priv->pci_dev);
	u16 poll_bit = MII_STAT_LINK, status = 0;
	unsigned long timeout = jiffies + 5 * HZ;
	int phy_addr;

	sis_priv->mii = NULL;

	/* search for total of 32 possible mii phy addresses */
	for (phy_addr = 0; phy_addr < 32; phy_addr++) {
		struct mii_phy * mii_phy = NULL;
		u16 mii_status;
		int i;

		mii_phy = NULL;
		for(i = 0; i < 2; i++)
			mii_status = mdio_read(net_dev, phy_addr, MII_STATUS);

		if (mii_status == 0xffff || mii_status == 0x0000) {
			if (netif_msg_probe(sis_priv))
				printk(KERN_DEBUG "%s: MII at address %d"
						" not accessible\n",
						dev_name, phy_addr);
			continue;
		}

		if ((mii_phy = kmalloc(sizeof(struct mii_phy), GFP_KERNEL)) == NULL) {
			mii_phy = sis_priv->first_mii;
			while (mii_phy) {
				struct mii_phy *phy;
				phy = mii_phy;
				mii_phy = mii_phy->next;
				kfree(phy);
			}
			return 0;
		}

		mii_phy->phy_id0 = mdio_read(net_dev, phy_addr, MII_PHY_ID0);
		mii_phy->phy_id1 = mdio_read(net_dev, phy_addr, MII_PHY_ID1);
		mii_phy->phy_addr = phy_addr;
		mii_phy->status = mii_status;
		mii_phy->next = sis_priv->mii;
		sis_priv->mii = mii_phy;
		sis_priv->first_mii = mii_phy;

		for (i = 0; mii_chip_table[i].phy_id1; i++)
			if ((mii_phy->phy_id0 == mii_chip_table[i].phy_id0 ) &&
			    ((mii_phy->phy_id1 & 0xFFF0) == mii_chip_table[i].phy_id1)){
				mii_phy->phy_types = mii_chip_table[i].phy_types;
				if (mii_chip_table[i].phy_types == MIX)
					mii_phy->phy_types =
					    (mii_status & (MII_STAT_CAN_TX_FDX | MII_STAT_CAN_TX)) ? LAN : HOME;
				printk(KERN_INFO "%s: %s transceiver found "
							"at address %d.\n",
							dev_name,
							mii_chip_table[i].name,
							phy_addr);
				break;
			}

		if( !mii_chip_table[i].phy_id1 ) {
			printk(KERN_INFO "%s: Unknown PHY transceiver found at address %d.\n",
			       dev_name, phy_addr);
			mii_phy->phy_types = UNKNOWN;
		}
	}

	if (sis_priv->mii == NULL) {
		printk(KERN_INFO "%s: No MII transceivers found!\n", dev_name);
		return 0;
	}

	/* select default PHY for mac */
	sis_priv->mii = NULL;
	sis900_default_phy( net_dev );

	/* Reset phy if default phy is internal sis900 */
        if ((sis_priv->mii->phy_id0 == 0x001D) &&
	    ((sis_priv->mii->phy_id1&0xFFF0) == 0x8000))
        	status = sis900_reset_phy(net_dev, sis_priv->cur_phy);

        /* workaround for ICS1893 PHY */
        if ((sis_priv->mii->phy_id0 == 0x0015) &&
            ((sis_priv->mii->phy_id1&0xFFF0) == 0xF440))
            	mdio_write(net_dev, sis_priv->cur_phy, 0x0018, 0xD200);

	if(status & MII_STAT_LINK){
		while (poll_bit) {
			yield();

			poll_bit ^= (mdio_read(net_dev, sis_priv->cur_phy, MII_STATUS) & poll_bit);
			if (time_after_eq(jiffies, timeout)) {
				printk(KERN_WARNING "%s: reset phy and link down now\n",
				       dev_name);
				return -ETIME;
			}
		}
	}

	if (sis_priv->chipset_rev == SIS630E_900_REV) {
		/* SiS 630E has some bugs on default value of PHY registers */
		mdio_write(net_dev, sis_priv->cur_phy, MII_ANADV, 0x05e1);
		mdio_write(net_dev, sis_priv->cur_phy, MII_CONFIG1, 0x22);
		mdio_write(net_dev, sis_priv->cur_phy, MII_CONFIG2, 0xff00);
		mdio_write(net_dev, sis_priv->cur_phy, MII_MASK, 0xffc0);
		//mdio_write(net_dev, sis_priv->cur_phy, MII_CONTROL, 0x1000);
	}

	if (sis_priv->mii->status & MII_STAT_LINK)
		netif_carrier_on(net_dev);
	else
		netif_carrier_off(net_dev);

	return 1;
}

/**
 *	sis900_default_phy - Select default PHY for sis900 mac.
 *	@net_dev: the net device to probe for
 *
 *	Select first detected PHY with link as default.
 *	If no one is link on, select PHY whose types is HOME as default.
 *	If HOME doesn't exist, select LAN.
 */

static u16 sis900_default_phy(struct net_device * net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
 	struct mii_phy *phy = NULL, *phy_home = NULL,
		*default_phy = NULL, *phy_lan = NULL;
	u16 status;

        for (phy=sis_priv->first_mii; phy; phy=phy->next) {
		status = mdio_read(net_dev, phy->phy_addr, MII_STATUS);
		status = mdio_read(net_dev, phy->phy_addr, MII_STATUS);

		/* Link ON & Not select default PHY & not ghost PHY */
		if ((status & MII_STAT_LINK) && !default_phy &&
		    (phy->phy_types != UNKNOWN)) {
			default_phy = phy;
		} else {
			status = mdio_read(net_dev, phy->phy_addr, MII_CONTROL);
			mdio_write(net_dev, phy->phy_addr, MII_CONTROL,
				status | MII_CNTL_AUTO | MII_CNTL_ISOLATE);
			if (phy->phy_types == HOME)
				phy_home = phy;
			else if(phy->phy_types == LAN)
				phy_lan = phy;
		}
	}

	if (!default_phy && phy_home)
		default_phy = phy_home;
	else if (!default_phy && phy_lan)
		default_phy = phy_lan;
	else if (!default_phy)
		default_phy = sis_priv->first_mii;

	if (sis_priv->mii != default_phy) {
		sis_priv->mii = default_phy;
		sis_priv->cur_phy = default_phy->phy_addr;
		printk(KERN_INFO "%s: Using transceiver found at address %d as default\n",
		       pci_name(sis_priv->pci_dev), sis_priv->cur_phy);
	}

	sis_priv->mii_info.phy_id = sis_priv->cur_phy;

	status = mdio_read(net_dev, sis_priv->cur_phy, MII_CONTROL);
	status &= (~MII_CNTL_ISOLATE);

	mdio_write(net_dev, sis_priv->cur_phy, MII_CONTROL, status);
	status = mdio_read(net_dev, sis_priv->cur_phy, MII_STATUS);
	status = mdio_read(net_dev, sis_priv->cur_phy, MII_STATUS);

	return status;
}


/**
 * 	sis900_set_capability - set the media capability of network adapter.
 *	@net_dev : the net device to probe for
 *	@phy : default PHY
 *
 *	Set the media capability of network adapter according to
 *	mii status register. It's necessary before auto-negotiate.
 */

static void sis900_set_capability(struct net_device *net_dev, struct mii_phy *phy)
{
	u16 cap;
	u16 status;

	status = mdio_read(net_dev, phy->phy_addr, MII_STATUS);
	status = mdio_read(net_dev, phy->phy_addr, MII_STATUS);

	cap = MII_NWAY_CSMA_CD |
		((phy->status & MII_STAT_CAN_TX_FDX)? MII_NWAY_TX_FDX:0) |
		((phy->status & MII_STAT_CAN_TX)    ? MII_NWAY_TX:0) |
		((phy->status & MII_STAT_CAN_T_FDX) ? MII_NWAY_T_FDX:0)|
		((phy->status & MII_STAT_CAN_T)     ? MII_NWAY_T:0);

	mdio_write(net_dev, phy->phy_addr, MII_ANADV, cap);
}


/* Delay between EEPROM clock transitions. */
#define eeprom_delay()	sr32(mear)

/**
 *	read_eeprom - Read Serial EEPROM
 *	@ioaddr: base i/o address
 *	@location: the EEPROM location to read
 *
 *	Read Serial EEPROM through EEPROM Access Register.
 *	Note that location is in word (16 bits) unit
 */

static u16 read_eeprom(void __iomem *ioaddr, int location)
{
	u32 read_cmd = location | EEread;
	int i;
	u16 retval = 0;

	sw32(mear, 0);
	eeprom_delay();
	sw32(mear, EECS);
	eeprom_delay();

	/* Shift the read command (9) bits out. */
	for (i = 8; i >= 0; i--) {
		u32 dataval = (read_cmd & (1 << i)) ? EEDI | EECS : EECS;

		sw32(mear, dataval);
		eeprom_delay();
		sw32(mear, dataval | EECLK);
		eeprom_delay();
	}
	sw32(mear, EECS);
	eeprom_delay();

	/* read the 16-bits data in */
	for (i = 16; i > 0; i--) {
		sw32(mear, EECS);
		eeprom_delay();
		sw32(mear, EECS | EECLK);
		eeprom_delay();
		retval = (retval << 1) | ((sr32(mear) & EEDO) ? 1 : 0);
		eeprom_delay();
	}

	/* Terminate the EEPROM access. */
	sw32(mear, 0);
	eeprom_delay();

	return retval;
}

/* Read and write the MII management registers using software-generated
   serial MDIO protocol. Note that the command bits and data bits are
   send out separately */
#define mdio_delay()	sr32(mear)

static void mdio_idle(struct sis900_private *sp)
{
	void __iomem *ioaddr = sp->ioaddr;

	sw32(mear, MDIO | MDDIR);
	mdio_delay();
	sw32(mear, MDIO | MDDIR | MDC);
}

/* Synchronize the MII management interface by shifting 32 one bits out. */
static void mdio_reset(struct sis900_private *sp)
{
	void __iomem *ioaddr = sp->ioaddr;
	int i;

	for (i = 31; i >= 0; i--) {
		sw32(mear, MDDIR | MDIO);
		mdio_delay();
		sw32(mear, MDDIR | MDIO | MDC);
		mdio_delay();
	}
}

/**
 *	mdio_read - read MII PHY register
 *	@net_dev: the net device to read
 *	@phy_id: the phy address to read
 *	@location: the phy register id to read
 *
 *	Read MII registers through MDIO and MDC
 *	using MDIO management frame structure and protocol(defined by ISO/IEC).
 *	Please see SiS7014 or ICS spec
 */

static int mdio_read(struct net_device *net_dev, int phy_id, int location)
{
	int mii_cmd = MIIread|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
	struct sis900_private *sp = netdev_priv(net_dev);
	void __iomem *ioaddr = sp->ioaddr;
	u16 retval = 0;
	int i;

	mdio_reset(sp);
	mdio_idle(sp);

	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;

		sw32(mear, dataval);
		mdio_delay();
		sw32(mear, dataval | MDC);
		mdio_delay();
	}

	/* Read the 16 data bits. */
	for (i = 16; i > 0; i--) {
		sw32(mear, 0);
		mdio_delay();
		retval = (retval << 1) | ((sr32(mear) & MDIO) ? 1 : 0);
		sw32(mear, MDC);
		mdio_delay();
	}
	sw32(mear, 0x00);

	return retval;
}

/**
 *	mdio_write - write MII PHY register
 *	@net_dev: the net device to write
 *	@phy_id: the phy address to write
 *	@location: the phy register id to write
 *	@value: the register value to write with
 *
 *	Write MII registers with @value through MDIO and MDC
 *	using MDIO management frame structure and protocol(defined by ISO/IEC)
 *	please see SiS7014 or ICS spec
 */

static void mdio_write(struct net_device *net_dev, int phy_id, int location,
			int value)
{
	int mii_cmd = MIIwrite|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
	struct sis900_private *sp = netdev_priv(net_dev);
	void __iomem *ioaddr = sp->ioaddr;
	int i;

	mdio_reset(sp);
	mdio_idle(sp);

	/* Shift the command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;

		sw8(mear, dataval);
		mdio_delay();
		sw8(mear, dataval | MDC);
		mdio_delay();
	}
	mdio_delay();

	/* Shift the value bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (value & (1 << i)) ? MDDIR | MDIO : MDDIR;

		sw32(mear, dataval);
		mdio_delay();
		sw32(mear, dataval | MDC);
		mdio_delay();
	}
	mdio_delay();

	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		sw8(mear, 0);
		mdio_delay();
		sw8(mear, MDC);
		mdio_delay();
	}
	sw32(mear, 0x00);
}


/**
 *	sis900_reset_phy - reset sis900 mii phy.
 *	@net_dev: the net device to write
 *	@phy_addr: default phy address
 *
 *	Some specific phy can't work properly without reset.
 *	This function will be called during initialization and
 *	link status change from ON to DOWN.
 */

static u16 sis900_reset_phy(struct net_device *net_dev, int phy_addr)
{
	int i;
	u16 status;

	for (i = 0; i < 2; i++)
		status = mdio_read(net_dev, phy_addr, MII_STATUS);

	mdio_write( net_dev, phy_addr, MII_CONTROL, MII_CNTL_RESET );

	return status;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
*/
static void sis900_poll(struct net_device *dev)
{
	struct sis900_private *sp = netdev_priv(dev);
	const int irq = sp->pci_dev->irq;

	disable_irq(irq);
	sis900_interrupt(irq, dev);
	enable_irq(irq);
}
#endif

/**
 *	sis900_open - open sis900 device
 *	@net_dev: the net device to open
 *
 *	Do some initialization and start net interface.
 *	enable interrupts and set sis900 timer.
 */

static int
sis900_open(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	int ret;

	/* Soft reset the chip. */
	sis900_reset(net_dev);

	/* Equalizer workaround Rule */
	sis630_set_eq(net_dev, sis_priv->chipset_rev);

	ret = request_irq(sis_priv->pci_dev->irq, sis900_interrupt, IRQF_SHARED,
			  net_dev->name, net_dev);
	if (ret)
		return ret;

	sis900_init_rxfilter(net_dev);

	sis900_init_tx_ring(net_dev);
	sis900_init_rx_ring(net_dev);

	set_rx_mode(net_dev);

	netif_start_queue(net_dev);

	/* Workaround for EDB */
	sis900_set_mode(sis_priv, HW_SPEED_10_MBPS, FDX_CAPABLE_HALF_SELECTED);

	/* Enable all known interrupts by setting the interrupt mask. */
	sw32(imr, RxSOVR | RxORN | RxERR | RxOK | TxURN | TxERR | TxDESC);
	sw32(cr, RxENA | sr32(cr));
	sw32(ier, IE);

	sis900_check_mode(net_dev, sis_priv->mii);

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	timer_setup(&sis_priv->timer, sis900_timer, 0);
	sis_priv->timer.expires = jiffies + HZ;
	add_timer(&sis_priv->timer);

	return 0;
}

/**
 *	sis900_init_rxfilter - Initialize the Rx filter
 *	@net_dev: the net device to initialize for
 *
 *	Set receive filter address to our MAC address
 *	and enable packet filtering.
 */

static void
sis900_init_rxfilter (struct net_device * net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	u32 rfcrSave;
	u32 i;

	rfcrSave = sr32(rfcr);

	/* disable packet filtering before setting filter */
	sw32(rfcr, rfcrSave & ~RFEN);

	/* load MAC addr to filter data register */
	for (i = 0 ; i < 3 ; i++) {
		u32 w = (u32) *((u16 *)(net_dev->dev_addr)+i);

		sw32(rfcr, i << RFADDR_shift);
		sw32(rfdr, w);

		if (netif_msg_hw(sis_priv)) {
			printk(KERN_DEBUG "%s: Receive Filter Address[%d]=%x\n",
			       net_dev->name, i, sr32(rfdr));
		}
	}

	/* enable packet filtering */
	sw32(rfcr, rfcrSave | RFEN);
}

/**
 *	sis900_init_tx_ring - Initialize the Tx descriptor ring
 *	@net_dev: the net device to initialize for
 *
 *	Initialize the Tx descriptor ring,
 */

static void
sis900_init_tx_ring(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	int i;

	sis_priv->tx_full = 0;
	sis_priv->dirty_tx = sis_priv->cur_tx = 0;

	for (i = 0; i < NUM_TX_DESC; i++) {
		sis_priv->tx_skbuff[i] = NULL;

		sis_priv->tx_ring[i].link = sis_priv->tx_ring_dma +
			((i+1)%NUM_TX_DESC)*sizeof(BufferDesc);
		sis_priv->tx_ring[i].cmdsts = 0;
		sis_priv->tx_ring[i].bufptr = 0;
	}

	/* load Transmit Descriptor Register */
	sw32(txdp, sis_priv->tx_ring_dma);
	if (netif_msg_hw(sis_priv))
		printk(KERN_DEBUG "%s: TX descriptor register loaded with: %8.8x\n",
		       net_dev->name, sr32(txdp));
}

/**
 *	sis900_init_rx_ring - Initialize the Rx descriptor ring
 *	@net_dev: the net device to initialize for
 *
 *	Initialize the Rx descriptor ring,
 *	and pre-allocate receive buffers (socket buffer)
 */

static void
sis900_init_rx_ring(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	int i;

	sis_priv->cur_rx = 0;
	sis_priv->dirty_rx = 0;

	/* init RX descriptor */
	for (i = 0; i < NUM_RX_DESC; i++) {
		sis_priv->rx_skbuff[i] = NULL;

		sis_priv->rx_ring[i].link = sis_priv->rx_ring_dma +
			((i+1)%NUM_RX_DESC)*sizeof(BufferDesc);
		sis_priv->rx_ring[i].cmdsts = 0;
		sis_priv->rx_ring[i].bufptr = 0;
	}

	/* allocate sock buffers */
	for (i = 0; i < NUM_RX_DESC; i++) {
		struct sk_buff *skb;

		if ((skb = netdev_alloc_skb(net_dev, RX_BUF_SIZE)) == NULL) {
			/* not enough memory for skbuff, this makes a "hole"
			   on the buffer ring, it is not clear how the
			   hardware will react to this kind of degenerated
			   buffer */
			break;
		}
		sis_priv->rx_skbuff[i] = skb;
		sis_priv->rx_ring[i].cmdsts = RX_BUF_SIZE;
		sis_priv->rx_ring[i].bufptr = dma_map_single(&sis_priv->pci_dev->dev,
							     skb->data,
							     RX_BUF_SIZE,
							     DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(&sis_priv->pci_dev->dev,
					       sis_priv->rx_ring[i].bufptr))) {
			dev_kfree_skb(skb);
			sis_priv->rx_skbuff[i] = NULL;
			break;
		}
	}
	sis_priv->dirty_rx = (unsigned int) (i - NUM_RX_DESC);

	/* load Receive Descriptor Register */
	sw32(rxdp, sis_priv->rx_ring_dma);
	if (netif_msg_hw(sis_priv))
		printk(KERN_DEBUG "%s: RX descriptor register loaded with: %8.8x\n",
		       net_dev->name, sr32(rxdp));
}

/**
 *	sis630_set_eq - set phy equalizer value for 630 LAN
 *	@net_dev: the net device to set equalizer value
 *	@revision: 630 LAN revision number
 *
 *	630E equalizer workaround rule(Cyrus Huang 08/15)
 *	PHY register 14h(Test)
 *	Bit 14: 0 -- Automatically detect (default)
 *		1 -- Manually set Equalizer filter
 *	Bit 13: 0 -- (Default)
 *		1 -- Speed up convergence of equalizer setting
 *	Bit 9 : 0 -- (Default)
 *		1 -- Disable Baseline Wander
 *	Bit 3~7   -- Equalizer filter setting
 *	Link ON: Set Bit 9, 13 to 1, Bit 14 to 0
 *	Then calculate equalizer value
 *	Then set equalizer value, and set Bit 14 to 1, Bit 9 to 0
 *	Link Off:Set Bit 13 to 1, Bit 14 to 0
 *	Calculate Equalizer value:
 *	When Link is ON and Bit 14 is 0, SIS900PHY will auto-detect proper equalizer value.
 *	When the equalizer is stable, this value is not a fixed value. It will be within
 *	a small range(eg. 7~9). Then we get a minimum and a maximum value(eg. min=7, max=9)
 *	0 <= max <= 4  --> set equalizer to max
 *	5 <= max <= 14 --> set equalizer to max+1 or set equalizer to max+2 if max == min
 *	max >= 15      --> set equalizer to max+5 or set equalizer to max+6 if max == min
 */

static void sis630_set_eq(struct net_device *net_dev, u8 revision)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	u16 reg14h, eq_value=0, max_value=0, min_value=0;
	int i, maxcount=10;

	if ( !(revision == SIS630E_900_REV || revision == SIS630EA1_900_REV ||
	       revision == SIS630A_900_REV || revision ==  SIS630ET_900_REV) )
		return;

	if (netif_carrier_ok(net_dev)) {
		reg14h = mdio_read(net_dev, sis_priv->cur_phy, MII_RESV);
		mdio_write(net_dev, sis_priv->cur_phy, MII_RESV,
					(0x2200 | reg14h) & 0xBFFF);
		for (i=0; i < maxcount; i++) {
			eq_value = (0x00F8 & mdio_read(net_dev,
					sis_priv->cur_phy, MII_RESV)) >> 3;
			if (i == 0)
				max_value=min_value=eq_value;
			max_value = (eq_value > max_value) ?
						eq_value : max_value;
			min_value = (eq_value < min_value) ?
						eq_value : min_value;
		}
		/* 630E rule to determine the equalizer value */
		if (revision == SIS630E_900_REV || revision == SIS630EA1_900_REV ||
		    revision == SIS630ET_900_REV) {
			if (max_value < 5)
				eq_value = max_value;
			else if (max_value >= 5 && max_value < 15)
				eq_value = (max_value == min_value) ?
						max_value+2 : max_value+1;
			else if (max_value >= 15)
				eq_value=(max_value == min_value) ?
						max_value+6 : max_value+5;
		}
		/* 630B0&B1 rule to determine the equalizer value */
		if (revision == SIS630A_900_REV &&
		    (sis_priv->host_bridge_rev == SIS630B0 ||
		     sis_priv->host_bridge_rev == SIS630B1)) {
			if (max_value == 0)
				eq_value = 3;
			else
				eq_value = (max_value + min_value + 1)/2;
		}
		/* write equalizer value and setting */
		reg14h = mdio_read(net_dev, sis_priv->cur_phy, MII_RESV);
		reg14h = (reg14h & 0xFF07) | ((eq_value << 3) & 0x00F8);
		reg14h = (reg14h | 0x6000) & 0xFDFF;
		mdio_write(net_dev, sis_priv->cur_phy, MII_RESV, reg14h);
	} else {
		reg14h = mdio_read(net_dev, sis_priv->cur_phy, MII_RESV);
		if (revision == SIS630A_900_REV &&
		    (sis_priv->host_bridge_rev == SIS630B0 ||
		     sis_priv->host_bridge_rev == SIS630B1))
			mdio_write(net_dev, sis_priv->cur_phy, MII_RESV,
						(reg14h | 0x2200) & 0xBFFF);
		else
			mdio_write(net_dev, sis_priv->cur_phy, MII_RESV,
						(reg14h | 0x2000) & 0xBFFF);
	}
}

/**
 *	sis900_timer - sis900 timer routine
 *	@data: pointer to sis900 net device
 *
 *	On each timer ticks we check two things,
 *	link status (ON/OFF) and link mode (10/100/Full/Half)
 */

static void sis900_timer(struct timer_list *t)
{
	struct sis900_private *sis_priv = from_timer(sis_priv, t, timer);
	struct net_device *net_dev = sis_priv->mii_info.dev;
	struct mii_phy *mii_phy = sis_priv->mii;
	static const int next_tick = 5*HZ;
	int speed = 0, duplex = 0;
	u16 status;

	status = mdio_read(net_dev, sis_priv->cur_phy, MII_STATUS);
	status = mdio_read(net_dev, sis_priv->cur_phy, MII_STATUS);

	/* Link OFF -> ON */
	if (!netif_carrier_ok(net_dev)) {
	LookForLink:
		/* Search for new PHY */
		status = sis900_default_phy(net_dev);
		mii_phy = sis_priv->mii;

		if (status & MII_STAT_LINK) {
			WARN_ON(!(status & MII_STAT_AUTO_DONE));

			sis900_read_mode(net_dev, &speed, &duplex);
			if (duplex) {
				sis900_set_mode(sis_priv, speed, duplex);
				sis630_set_eq(net_dev, sis_priv->chipset_rev);
				netif_carrier_on(net_dev);
			}
		}
	} else {
	/* Link ON -> OFF */
                if (!(status & MII_STAT_LINK)){
                	netif_carrier_off(net_dev);
			if(netif_msg_link(sis_priv))
                		printk(KERN_INFO "%s: Media Link Off\n", net_dev->name);

                	/* Change mode issue */
                	if ((mii_phy->phy_id0 == 0x001D) &&
			    ((mii_phy->phy_id1 & 0xFFF0) == 0x8000))
               			sis900_reset_phy(net_dev,  sis_priv->cur_phy);

			sis630_set_eq(net_dev, sis_priv->chipset_rev);

                	goto LookForLink;
                }
	}

	sis_priv->timer.expires = jiffies + next_tick;
	add_timer(&sis_priv->timer);
}

/**
 *	sis900_check_mode - check the media mode for sis900
 *	@net_dev: the net device to be checked
 *	@mii_phy: the mii phy
 *
 *	Older driver gets the media mode from mii status output
 *	register. Now we set our media capability and auto-negotiate
 *	to get the upper bound of speed and duplex between two ends.
 *	If the types of mii phy is HOME, it doesn't need to auto-negotiate
 *	and autong_complete should be set to 1.
 */

static void sis900_check_mode(struct net_device *net_dev, struct mii_phy *mii_phy)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	int speed, duplex;

	if (mii_phy->phy_types == LAN) {
		sw32(cfg, ~EXD & sr32(cfg));
		sis900_set_capability(net_dev , mii_phy);
		sis900_auto_negotiate(net_dev, sis_priv->cur_phy);
	} else {
		sw32(cfg, EXD | sr32(cfg));
		speed = HW_SPEED_HOME;
		duplex = FDX_CAPABLE_HALF_SELECTED;
		sis900_set_mode(sis_priv, speed, duplex);
		sis_priv->autong_complete = 1;
	}
}

/**
 *	sis900_set_mode - Set the media mode of mac register.
 *	@sp:     the device private data
 *	@speed : the transmit speed to be determined
 *	@duplex: the duplex mode to be determined
 *
 *	Set the media mode of mac register txcfg/rxcfg according to
 *	speed and duplex of phy. Bit EDB_MASTER_EN indicates the EDB
 *	bus is used instead of PCI bus. When this bit is set 1, the
 *	Max DMA Burst Size for TX/RX DMA should be no larger than 16
 *	double words.
 */

static void sis900_set_mode(struct sis900_private *sp, int speed, int duplex)
{
	void __iomem *ioaddr = sp->ioaddr;
	u32 tx_flags = 0, rx_flags = 0;

	if (sr32( cfg) & EDB_MASTER_EN) {
		tx_flags = TxATP | (DMA_BURST_64 << TxMXDMA_shift) |
					(TX_FILL_THRESH << TxFILLT_shift);
		rx_flags = DMA_BURST_64 << RxMXDMA_shift;
	} else {
		tx_flags = TxATP | (DMA_BURST_512 << TxMXDMA_shift) |
					(TX_FILL_THRESH << TxFILLT_shift);
		rx_flags = DMA_BURST_512 << RxMXDMA_shift;
	}

	if (speed == HW_SPEED_HOME || speed == HW_SPEED_10_MBPS) {
		rx_flags |= (RxDRNT_10 << RxDRNT_shift);
		tx_flags |= (TxDRNT_10 << TxDRNT_shift);
	} else {
		rx_flags |= (RxDRNT_100 << RxDRNT_shift);
		tx_flags |= (TxDRNT_100 << TxDRNT_shift);
	}

	if (duplex == FDX_CAPABLE_FULL_SELECTED) {
		tx_flags |= (TxCSI | TxHBI);
		rx_flags |= RxATX;
	}

#if IS_ENABLED(CONFIG_VLAN_8021Q)
	/* Can accept Jumbo packet */
	rx_flags |= RxAJAB;
#endif

	sw32(txcfg, tx_flags);
	sw32(rxcfg, rx_flags);
}

/**
 *	sis900_auto_negotiate - Set the Auto-Negotiation Enable/Reset bit.
 *	@net_dev: the net device to read mode for
 *	@phy_addr: mii phy address
 *
 *	If the adapter is link-on, set the auto-negotiate enable/reset bit.
 *	autong_complete should be set to 0 when starting auto-negotiation.
 *	autong_complete should be set to 1 if we didn't start auto-negotiation.
 *	sis900_timer will wait for link on again if autong_complete = 0.
 */

static void sis900_auto_negotiate(struct net_device *net_dev, int phy_addr)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	int i = 0;
	u32 status;

	for (i = 0; i < 2; i++)
		status = mdio_read(net_dev, phy_addr, MII_STATUS);

	if (!(status & MII_STAT_LINK)){
		if(netif_msg_link(sis_priv))
			printk(KERN_INFO "%s: Media Link Off\n", net_dev->name);
		sis_priv->autong_complete = 1;
		netif_carrier_off(net_dev);
		return;
	}

	/* (Re)start AutoNegotiate */
	mdio_write(net_dev, phy_addr, MII_CONTROL,
		   MII_CNTL_AUTO | MII_CNTL_RST_AUTO);
	sis_priv->autong_complete = 0;
}


/**
 *	sis900_read_mode - read media mode for sis900 internal phy
 *	@net_dev: the net device to read mode for
 *	@speed  : the transmit speed to be determined
 *	@duplex : the duplex mode to be determined
 *
 *	The capability of remote end will be put in mii register autorec
 *	after auto-negotiation. Use AND operation to get the upper bound
 *	of speed and duplex between two ends.
 */

static void sis900_read_mode(struct net_device *net_dev, int *speed, int *duplex)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	struct mii_phy *phy = sis_priv->mii;
	int phy_addr = sis_priv->cur_phy;
	u32 status;
	u16 autoadv, autorec;
	int i;

	for (i = 0; i < 2; i++)
		status = mdio_read(net_dev, phy_addr, MII_STATUS);

	if (!(status & MII_STAT_LINK))
		return;

	/* AutoNegotiate completed */
	autoadv = mdio_read(net_dev, phy_addr, MII_ANADV);
	autorec = mdio_read(net_dev, phy_addr, MII_ANLPAR);
	status = autoadv & autorec;

	*speed = HW_SPEED_10_MBPS;
	*duplex = FDX_CAPABLE_HALF_SELECTED;

	if (status & (MII_NWAY_TX | MII_NWAY_TX_FDX))
		*speed = HW_SPEED_100_MBPS;
	if (status & ( MII_NWAY_TX_FDX | MII_NWAY_T_FDX))
		*duplex = FDX_CAPABLE_FULL_SELECTED;

	sis_priv->autong_complete = 1;

	/* Workaround for Realtek RTL8201 PHY issue */
	if ((phy->phy_id0 == 0x0000) && ((phy->phy_id1 & 0xFFF0) == 0x8200)) {
		if (mdio_read(net_dev, phy_addr, MII_CONTROL) & MII_CNTL_FDX)
			*duplex = FDX_CAPABLE_FULL_SELECTED;
		if (mdio_read(net_dev, phy_addr, 0x0019) & 0x01)
			*speed = HW_SPEED_100_MBPS;
	}

	if(netif_msg_link(sis_priv))
		printk(KERN_INFO "%s: Media Link On %s %s-duplex\n",
	       				net_dev->name,
	       				*speed == HW_SPEED_100_MBPS ?
	       					"100mbps" : "10mbps",
	       				*duplex == FDX_CAPABLE_FULL_SELECTED ?
	       					"full" : "half");
}

/**
 *	sis900_tx_timeout - sis900 transmit timeout routine
 *	@net_dev: the net device to transmit
 *
 *	print transmit timeout status
 *	disable interrupts and do some tasks
 */

static void sis900_tx_timeout(struct net_device *net_dev, unsigned int txqueue)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	unsigned long flags;
	int i;

	if (netif_msg_tx_err(sis_priv)) {
		printk(KERN_INFO "%s: Transmit timeout, status %8.8x %8.8x\n",
			net_dev->name, sr32(cr), sr32(isr));
	}

	/* Disable interrupts by clearing the interrupt mask. */
	sw32(imr, 0x0000);

	/* use spinlock to prevent interrupt handler accessing buffer ring */
	spin_lock_irqsave(&sis_priv->lock, flags);

	/* discard unsent packets */
	sis_priv->dirty_tx = sis_priv->cur_tx = 0;
	for (i = 0; i < NUM_TX_DESC; i++) {
		struct sk_buff *skb = sis_priv->tx_skbuff[i];

		if (skb) {
			dma_unmap_single(&sis_priv->pci_dev->dev,
					 sis_priv->tx_ring[i].bufptr,
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_irq(skb);
			sis_priv->tx_skbuff[i] = NULL;
			sis_priv->tx_ring[i].cmdsts = 0;
			sis_priv->tx_ring[i].bufptr = 0;
			net_dev->stats.tx_dropped++;
		}
	}
	sis_priv->tx_full = 0;
	netif_wake_queue(net_dev);

	spin_unlock_irqrestore(&sis_priv->lock, flags);

	netif_trans_update(net_dev); /* prevent tx timeout */

	/* load Transmit Descriptor Register */
	sw32(txdp, sis_priv->tx_ring_dma);

	/* Enable all known interrupts by setting the interrupt mask. */
	sw32(imr, RxSOVR | RxORN | RxERR | RxOK | TxURN | TxERR | TxDESC);
}

/**
 *	sis900_start_xmit - sis900 start transmit routine
 *	@skb: socket buffer pointer to put the data being transmitted
 *	@net_dev: the net device to transmit with
 *
 *	Set the transmit buffer descriptor,
 *	and write TxENA to enable transmit state machine.
 *	tell upper layer if the buffer is full
 */

static netdev_tx_t
sis900_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	unsigned int  entry;
	unsigned long flags;
	unsigned int  index_cur_tx, index_dirty_tx;
	unsigned int  count_dirty_tx;

	spin_lock_irqsave(&sis_priv->lock, flags);

	/* Calculate the next Tx descriptor entry. */
	entry = sis_priv->cur_tx % NUM_TX_DESC;
	sis_priv->tx_skbuff[entry] = skb;

	/* set the transmit buffer descriptor and enable Transmit State Machine */
	sis_priv->tx_ring[entry].bufptr = dma_map_single(&sis_priv->pci_dev->dev,
							 skb->data, skb->len,
							 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&sis_priv->pci_dev->dev,
				       sis_priv->tx_ring[entry].bufptr))) {
			dev_kfree_skb_any(skb);
			sis_priv->tx_skbuff[entry] = NULL;
			net_dev->stats.tx_dropped++;
			spin_unlock_irqrestore(&sis_priv->lock, flags);
			return NETDEV_TX_OK;
	}
	sis_priv->tx_ring[entry].cmdsts = (OWN | INTR | skb->len);
	sw32(cr, TxENA | sr32(cr));

	sis_priv->cur_tx ++;
	index_cur_tx = sis_priv->cur_tx;
	index_dirty_tx = sis_priv->dirty_tx;

	for (count_dirty_tx = 0; index_cur_tx != index_dirty_tx; index_dirty_tx++)
		count_dirty_tx ++;

	if (index_cur_tx == index_dirty_tx) {
		/* dirty_tx is met in the cycle of cur_tx, buffer full */
		sis_priv->tx_full = 1;
		netif_stop_queue(net_dev);
	} else if (count_dirty_tx < NUM_TX_DESC) {
		/* Typical path, tell upper layer that more transmission is possible */
		netif_start_queue(net_dev);
	} else {
		/* buffer full, tell upper layer no more transmission */
		sis_priv->tx_full = 1;
		netif_stop_queue(net_dev);
	}

	spin_unlock_irqrestore(&sis_priv->lock, flags);

	if (netif_msg_tx_queued(sis_priv))
		printk(KERN_DEBUG "%s: Queued Tx packet at %p size %d "
		       "to slot %d.\n",
		       net_dev->name, skb->data, (int)skb->len, entry);

	return NETDEV_TX_OK;
}

/**
 *	sis900_interrupt - sis900 interrupt handler
 *	@irq: the irq number
 *	@dev_instance: the client data object
 *
 *	The interrupt handler does all of the Rx thread work,
 *	and cleans up after the Tx thread
 */

static irqreturn_t sis900_interrupt(int irq, void *dev_instance)
{
	struct net_device *net_dev = dev_instance;
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	int boguscnt = max_interrupt_work;
	void __iomem *ioaddr = sis_priv->ioaddr;
	u32 status;
	unsigned int handled = 0;

	spin_lock (&sis_priv->lock);

	do {
		status = sr32(isr);

		if ((status & (HIBERR|TxURN|TxERR|TxDESC|RxORN|RxERR|RxOK)) == 0)
			/* nothing interesting happened */
			break;
		handled = 1;

		/* why dow't we break after Tx/Rx case ?? keyword: full-duplex */
		if (status & (RxORN | RxERR | RxOK))
			/* Rx interrupt */
			sis900_rx(net_dev);

		if (status & (TxURN | TxERR | TxDESC))
			/* Tx interrupt */
			sis900_finish_xmit(net_dev);

		/* something strange happened !!! */
		if (status & HIBERR) {
			if(netif_msg_intr(sis_priv))
				printk(KERN_INFO "%s: Abnormal interrupt, "
					"status %#8.8x.\n", net_dev->name, status);
			break;
		}
		if (--boguscnt < 0) {
			if(netif_msg_intr(sis_priv))
				printk(KERN_INFO "%s: Too much work at interrupt, "
					"interrupt status = %#8.8x.\n",
					net_dev->name, status);
			break;
		}
	} while (1);

	if(netif_msg_intr(sis_priv))
		printk(KERN_DEBUG "%s: exiting interrupt, "
		       "interrupt status = %#8.8x\n",
		       net_dev->name, sr32(isr));

	spin_unlock (&sis_priv->lock);
	return IRQ_RETVAL(handled);
}

/**
 *	sis900_rx - sis900 receive routine
 *	@net_dev: the net device which receives data
 *
 *	Process receive interrupt events,
 *	put buffer to higher layer and refill buffer pool
 *	Note: This function is called by interrupt handler,
 *	don't do "too much" work here
 */

static int sis900_rx(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	unsigned int entry = sis_priv->cur_rx % NUM_RX_DESC;
	u32 rx_status = sis_priv->rx_ring[entry].cmdsts;
	int rx_work_limit;

	if (netif_msg_rx_status(sis_priv))
		printk(KERN_DEBUG "sis900_rx, cur_rx:%4.4d, dirty_rx:%4.4d "
		       "status:0x%8.8x\n",
		       sis_priv->cur_rx, sis_priv->dirty_rx, rx_status);
	rx_work_limit = sis_priv->dirty_rx + NUM_RX_DESC - sis_priv->cur_rx;

	while (rx_status & OWN) {
		unsigned int rx_size;
		unsigned int data_size;

		if (--rx_work_limit < 0)
			break;

		data_size = rx_status & DSIZE;
		rx_size = data_size - CRC_SIZE;

#if IS_ENABLED(CONFIG_VLAN_8021Q)
		/* ``TOOLONG'' flag means jumbo packet received. */
		if ((rx_status & TOOLONG) && data_size <= MAX_FRAME_SIZE)
			rx_status &= (~ ((unsigned int)TOOLONG));
#endif

		if (rx_status & (ABORT|OVERRUN|TOOLONG|RUNT|RXISERR|CRCERR|FAERR)) {
			/* corrupted packet received */
			if (netif_msg_rx_err(sis_priv))
				printk(KERN_DEBUG "%s: Corrupted packet "
				       "received, buffer status = 0x%8.8x/%d.\n",
				       net_dev->name, rx_status, data_size);
			net_dev->stats.rx_errors++;
			if (rx_status & OVERRUN)
				net_dev->stats.rx_over_errors++;
			if (rx_status & (TOOLONG|RUNT))
				net_dev->stats.rx_length_errors++;
			if (rx_status & (RXISERR | FAERR))
				net_dev->stats.rx_frame_errors++;
			if (rx_status & CRCERR)
				net_dev->stats.rx_crc_errors++;
			/* reset buffer descriptor state */
			sis_priv->rx_ring[entry].cmdsts = RX_BUF_SIZE;
		} else {
			struct sk_buff * skb;
			struct sk_buff * rx_skb;

			dma_unmap_single(&sis_priv->pci_dev->dev,
					 sis_priv->rx_ring[entry].bufptr,
					 RX_BUF_SIZE, DMA_FROM_DEVICE);

			/* refill the Rx buffer, what if there is not enough
			 * memory for new socket buffer ?? */
			if ((skb = netdev_alloc_skb(net_dev, RX_BUF_SIZE)) == NULL) {
				/*
				 * Not enough memory to refill the buffer
				 * so we need to recycle the old one so
				 * as to avoid creating a memory hole
				 * in the rx ring
				 */
				skb = sis_priv->rx_skbuff[entry];
				net_dev->stats.rx_dropped++;
				goto refill_rx_ring;
			}

			/* This situation should never happen, but due to
			   some unknown bugs, it is possible that
			   we are working on NULL sk_buff :-( */
			if (sis_priv->rx_skbuff[entry] == NULL) {
				if (netif_msg_rx_err(sis_priv))
					printk(KERN_WARNING "%s: NULL pointer "
					      "encountered in Rx ring\n"
					      "cur_rx:%4.4d, dirty_rx:%4.4d\n",
					      net_dev->name, sis_priv->cur_rx,
					      sis_priv->dirty_rx);
				dev_kfree_skb(skb);
				break;
			}

			/* give the socket buffer to upper layers */
			rx_skb = sis_priv->rx_skbuff[entry];
			skb_put(rx_skb, rx_size);
			rx_skb->protocol = eth_type_trans(rx_skb, net_dev);
			netif_rx(rx_skb);

			/* some network statistics */
			if ((rx_status & BCAST) == MCAST)
				net_dev->stats.multicast++;
			net_dev->stats.rx_bytes += rx_size;
			net_dev->stats.rx_packets++;
			sis_priv->dirty_rx++;
refill_rx_ring:
			sis_priv->rx_skbuff[entry] = skb;
			sis_priv->rx_ring[entry].cmdsts = RX_BUF_SIZE;
			sis_priv->rx_ring[entry].bufptr =
				dma_map_single(&sis_priv->pci_dev->dev,
					       skb->data, RX_BUF_SIZE,
					       DMA_FROM_DEVICE);
			if (unlikely(dma_mapping_error(&sis_priv->pci_dev->dev,
						       sis_priv->rx_ring[entry].bufptr))) {
				dev_kfree_skb_irq(skb);
				sis_priv->rx_skbuff[entry] = NULL;
				break;
			}
		}
		sis_priv->cur_rx++;
		entry = sis_priv->cur_rx % NUM_RX_DESC;
		rx_status = sis_priv->rx_ring[entry].cmdsts;
	} // while

	/* refill the Rx buffer, what if the rate of refilling is slower
	 * than consuming ?? */
	for (; sis_priv->cur_rx != sis_priv->dirty_rx; sis_priv->dirty_rx++) {
		struct sk_buff *skb;

		entry = sis_priv->dirty_rx % NUM_RX_DESC;

		if (sis_priv->rx_skbuff[entry] == NULL) {
			skb = netdev_alloc_skb(net_dev, RX_BUF_SIZE);
			if (skb == NULL) {
				/* not enough memory for skbuff, this makes a
				 * "hole" on the buffer ring, it is not clear
				 * how the hardware will react to this kind
				 * of degenerated buffer */
				net_dev->stats.rx_dropped++;
				break;
			}
			sis_priv->rx_skbuff[entry] = skb;
			sis_priv->rx_ring[entry].cmdsts = RX_BUF_SIZE;
			sis_priv->rx_ring[entry].bufptr =
				dma_map_single(&sis_priv->pci_dev->dev,
					       skb->data, RX_BUF_SIZE,
					       DMA_FROM_DEVICE);
			if (unlikely(dma_mapping_error(&sis_priv->pci_dev->dev,
						       sis_priv->rx_ring[entry].bufptr))) {
				dev_kfree_skb_irq(skb);
				sis_priv->rx_skbuff[entry] = NULL;
				break;
			}
		}
	}
	/* re-enable the potentially idle receive state matchine */
	sw32(cr , RxENA | sr32(cr));

	return 0;
}

/**
 *	sis900_finish_xmit - finish up transmission of packets
 *	@net_dev: the net device to be transmitted on
 *
 *	Check for error condition and free socket buffer etc
 *	schedule for more transmission as needed
 *	Note: This function is called by interrupt handler,
 *	don't do "too much" work here
 */

static void sis900_finish_xmit (struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);

	for (; sis_priv->dirty_tx != sis_priv->cur_tx; sis_priv->dirty_tx++) {
		struct sk_buff *skb;
		unsigned int entry;
		u32 tx_status;

		entry = sis_priv->dirty_tx % NUM_TX_DESC;
		tx_status = sis_priv->tx_ring[entry].cmdsts;

		if (tx_status & OWN) {
			/* The packet is not transmitted yet (owned by hardware) !
			 * Note: this is an almost impossible condition
			 * on TxDESC interrupt ('descriptor interrupt') */
			break;
		}

		if (tx_status & (ABORT | UNDERRUN | OWCOLL)) {
			/* packet unsuccessfully transmitted */
			if (netif_msg_tx_err(sis_priv))
				printk(KERN_DEBUG "%s: Transmit "
				       "error, Tx status %8.8x.\n",
				       net_dev->name, tx_status);
			net_dev->stats.tx_errors++;
			if (tx_status & UNDERRUN)
				net_dev->stats.tx_fifo_errors++;
			if (tx_status & ABORT)
				net_dev->stats.tx_aborted_errors++;
			if (tx_status & NOCARRIER)
				net_dev->stats.tx_carrier_errors++;
			if (tx_status & OWCOLL)
				net_dev->stats.tx_window_errors++;
		} else {
			/* packet successfully transmitted */
			net_dev->stats.collisions += (tx_status & COLCNT) >> 16;
			net_dev->stats.tx_bytes += tx_status & DSIZE;
			net_dev->stats.tx_packets++;
		}
		/* Free the original skb. */
		skb = sis_priv->tx_skbuff[entry];
		dma_unmap_single(&sis_priv->pci_dev->dev,
				 sis_priv->tx_ring[entry].bufptr, skb->len,
				 DMA_TO_DEVICE);
		dev_consume_skb_irq(skb);
		sis_priv->tx_skbuff[entry] = NULL;
		sis_priv->tx_ring[entry].bufptr = 0;
		sis_priv->tx_ring[entry].cmdsts = 0;
	}

	if (sis_priv->tx_full && netif_queue_stopped(net_dev) &&
	    sis_priv->cur_tx - sis_priv->dirty_tx < NUM_TX_DESC - 4) {
		/* The ring is no longer full, clear tx_full and schedule
		 * more transmission by netif_wake_queue(net_dev) */
		sis_priv->tx_full = 0;
		netif_wake_queue (net_dev);
	}
}

/**
 *	sis900_close - close sis900 device
 *	@net_dev: the net device to be closed
 *
 *	Disable interrupts, stop the Tx and Rx Status Machine
 *	free Tx and RX socket buffer
 */

static int sis900_close(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	struct pci_dev *pdev = sis_priv->pci_dev;
	void __iomem *ioaddr = sis_priv->ioaddr;
	struct sk_buff *skb;
	int i;

	netif_stop_queue(net_dev);

	/* Disable interrupts by clearing the interrupt mask. */
	sw32(imr, 0x0000);
	sw32(ier, 0x0000);

	/* Stop the chip's Tx and Rx Status Machine */
	sw32(cr, RxDIS | TxDIS | sr32(cr));

	del_timer(&sis_priv->timer);

	free_irq(pdev->irq, net_dev);

	/* Free Tx and RX skbuff */
	for (i = 0; i < NUM_RX_DESC; i++) {
		skb = sis_priv->rx_skbuff[i];
		if (skb) {
			dma_unmap_single(&pdev->dev,
					 sis_priv->rx_ring[i].bufptr,
					 RX_BUF_SIZE, DMA_FROM_DEVICE);
			dev_kfree_skb(skb);
			sis_priv->rx_skbuff[i] = NULL;
		}
	}
	for (i = 0; i < NUM_TX_DESC; i++) {
		skb = sis_priv->tx_skbuff[i];
		if (skb) {
			dma_unmap_single(&pdev->dev,
					 sis_priv->tx_ring[i].bufptr,
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb(skb);
			sis_priv->tx_skbuff[i] = NULL;
		}
	}

	/* Green! Put the chip in low-power mode. */

	return 0;
}

/**
 *	sis900_get_drvinfo - Return information about driver
 *	@net_dev: the net device to probe
 *	@info: container for info returned
 *
 *	Process ethtool command such as "ehtool -i" to show information
 */

static void sis900_get_drvinfo(struct net_device *net_dev,
			       struct ethtool_drvinfo *info)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);

	strlcpy(info->driver, SIS900_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, SIS900_DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(sis_priv->pci_dev),
		sizeof(info->bus_info));
}

static u32 sis900_get_msglevel(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	return sis_priv->msg_enable;
}

static void sis900_set_msglevel(struct net_device *net_dev, u32 value)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	sis_priv->msg_enable = value;
}

static u32 sis900_get_link(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	return mii_link_ok(&sis_priv->mii_info);
}

static int sis900_get_link_ksettings(struct net_device *net_dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	spin_lock_irq(&sis_priv->lock);
	mii_ethtool_get_link_ksettings(&sis_priv->mii_info, cmd);
	spin_unlock_irq(&sis_priv->lock);
	return 0;
}

static int sis900_set_link_ksettings(struct net_device *net_dev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	int rt;
	spin_lock_irq(&sis_priv->lock);
	rt = mii_ethtool_set_link_ksettings(&sis_priv->mii_info, cmd);
	spin_unlock_irq(&sis_priv->lock);
	return rt;
}

static int sis900_nway_reset(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	return mii_nway_restart(&sis_priv->mii_info);
}

/**
 *	sis900_set_wol - Set up Wake on Lan registers
 *	@net_dev: the net device to probe
 *	@wol: container for info passed to the driver
 *
 *	Process ethtool command "wol" to setup wake on lan features.
 *	SiS900 supports sending WoL events if a correct packet is received,
 *	but there is no simple way to filter them to only a subset (broadcast,
 *	multicast, unicast or arp).
 */

static int sis900_set_wol(struct net_device *net_dev, struct ethtool_wolinfo *wol)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	u32 cfgpmcsr = 0, pmctrl_bits = 0;

	if (wol->wolopts == 0) {
		pci_read_config_dword(sis_priv->pci_dev, CFGPMCSR, &cfgpmcsr);
		cfgpmcsr &= ~PME_EN;
		pci_write_config_dword(sis_priv->pci_dev, CFGPMCSR, cfgpmcsr);
		sw32(pmctrl, pmctrl_bits);
		if (netif_msg_wol(sis_priv))
			printk(KERN_DEBUG "%s: Wake on LAN disabled\n", net_dev->name);
		return 0;
	}

	if (wol->wolopts & (WAKE_MAGICSECURE | WAKE_UCAST | WAKE_MCAST
				| WAKE_BCAST | WAKE_ARP))
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC)
		pmctrl_bits |= MAGICPKT;
	if (wol->wolopts & WAKE_PHY)
		pmctrl_bits |= LINKON;

	sw32(pmctrl, pmctrl_bits);

	pci_read_config_dword(sis_priv->pci_dev, CFGPMCSR, &cfgpmcsr);
	cfgpmcsr |= PME_EN;
	pci_write_config_dword(sis_priv->pci_dev, CFGPMCSR, cfgpmcsr);
	if (netif_msg_wol(sis_priv))
		printk(KERN_DEBUG "%s: Wake on LAN enabled\n", net_dev->name);

	return 0;
}

static void sis900_get_wol(struct net_device *net_dev, struct ethtool_wolinfo *wol)
{
	struct sis900_private *sp = netdev_priv(net_dev);
	void __iomem *ioaddr = sp->ioaddr;
	u32 pmctrl_bits;

	pmctrl_bits = sr32(pmctrl);
	if (pmctrl_bits & MAGICPKT)
		wol->wolopts |= WAKE_MAGIC;
	if (pmctrl_bits & LINKON)
		wol->wolopts |= WAKE_PHY;

	wol->supported = (WAKE_PHY | WAKE_MAGIC);
}

static int sis900_get_eeprom_len(struct net_device *dev)
{
	struct sis900_private *sis_priv = netdev_priv(dev);

	return sis_priv->eeprom_size;
}

static int sis900_read_eeprom(struct net_device *net_dev, u8 *buf)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	int wait, ret = -EAGAIN;
	u16 signature;
	u16 *ebuf = (u16 *)buf;
	int i;

	if (sis_priv->chipset_rev == SIS96x_900_REV) {
		sw32(mear, EEREQ);
		for (wait = 0; wait < 2000; wait++) {
			if (sr32(mear) & EEGNT) {
				/* read 16 bits, and index by 16 bits */
				for (i = 0; i < sis_priv->eeprom_size / 2; i++)
					ebuf[i] = (u16)read_eeprom(ioaddr, i);
				ret = 0;
				break;
			}
			udelay(1);
		}
		sw32(mear, EEDONE);
	} else {
		signature = (u16)read_eeprom(ioaddr, EEPROMSignature);
		if (signature != 0xffff && signature != 0x0000) {
			/* read 16 bits, and index by 16 bits */
			for (i = 0; i < sis_priv->eeprom_size / 2; i++)
				ebuf[i] = (u16)read_eeprom(ioaddr, i);
			ret = 0;
		}
	}
	return ret;
}

#define SIS900_EEPROM_MAGIC	0xBABE
static int sis900_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom, u8 *data)
{
	struct sis900_private *sis_priv = netdev_priv(dev);
	u8 *eebuf;
	int res;

	eebuf = kmalloc(sis_priv->eeprom_size, GFP_KERNEL);
	if (!eebuf)
		return -ENOMEM;

	eeprom->magic = SIS900_EEPROM_MAGIC;
	spin_lock_irq(&sis_priv->lock);
	res = sis900_read_eeprom(dev, eebuf);
	spin_unlock_irq(&sis_priv->lock);
	if (!res)
		memcpy(data, eebuf + eeprom->offset, eeprom->len);
	kfree(eebuf);
	return res;
}

static const struct ethtool_ops sis900_ethtool_ops = {
	.get_drvinfo 	= sis900_get_drvinfo,
	.get_msglevel	= sis900_get_msglevel,
	.set_msglevel	= sis900_set_msglevel,
	.get_link	= sis900_get_link,
	.nway_reset	= sis900_nway_reset,
	.get_wol	= sis900_get_wol,
	.set_wol	= sis900_set_wol,
	.get_link_ksettings = sis900_get_link_ksettings,
	.set_link_ksettings = sis900_set_link_ksettings,
	.get_eeprom_len = sis900_get_eeprom_len,
	.get_eeprom = sis900_get_eeprom,
};

/**
 *	mii_ioctl - process MII i/o control command
 *	@net_dev: the net device to command for
 *	@rq: parameter for command
 *	@cmd: the i/o command
 *
 *	Process MII command like read/write MII register
 */

static int mii_ioctl(struct net_device *net_dev, struct ifreq *rq, int cmd)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	struct mii_ioctl_data *data = if_mii(rq);

	switch(cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		data->phy_id = sis_priv->mii->phy_addr;
		fallthrough;

	case SIOCGMIIREG:		/* Read MII PHY register. */
		data->val_out = mdio_read(net_dev, data->phy_id & 0x1f, data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
		mdio_write(net_dev, data->phy_id & 0x1f, data->reg_num & 0x1f, data->val_in);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

/**
 *	sis900_set_config - Set media type by net_device.set_config
 *	@dev: the net device for media type change
 *	@map: ifmap passed by ifconfig
 *
 *	Set media type to 10baseT, 100baseT or 0(for auto) by ifconfig
 *	we support only port changes. All other runtime configuration
 *	changes will be ignored
 */

static int sis900_set_config(struct net_device *dev, struct ifmap *map)
{
	struct sis900_private *sis_priv = netdev_priv(dev);
	struct mii_phy *mii_phy = sis_priv->mii;

	u16 status;

	if ((map->port != (u_char)(-1)) && (map->port != dev->if_port)) {
		/* we switch on the ifmap->port field. I couldn't find anything
		 * like a definition or standard for the values of that field.
		 * I think the meaning of those values is device specific. But
		 * since I would like to change the media type via the ifconfig
		 * command I use the definition from linux/netdevice.h
		 * (which seems to be different from the ifport(pcmcia) definition) */
		switch(map->port){
		case IF_PORT_UNKNOWN: /* use auto here */
			dev->if_port = map->port;
			/* we are going to change the media type, so the Link
			 * will be temporary down and we need to reflect that
			 * here. When the Link comes up again, it will be
			 * sensed by the sis_timer procedure, which also does
			 * all the rest for us */
			netif_carrier_off(dev);

			/* read current state */
			status = mdio_read(dev, mii_phy->phy_addr, MII_CONTROL);

			/* enable auto negotiation and reset the negotioation
			 * (I don't really know what the auto negatiotiation
			 * reset really means, but it sounds for me right to
			 * do one here) */
			mdio_write(dev, mii_phy->phy_addr,
				   MII_CONTROL, status | MII_CNTL_AUTO | MII_CNTL_RST_AUTO);

			break;

		case IF_PORT_10BASET: /* 10BaseT */
			dev->if_port = map->port;

			/* we are going to change the media type, so the Link
			 * will be temporary down and we need to reflect that
			 * here. When the Link comes up again, it will be
			 * sensed by the sis_timer procedure, which also does
			 * all the rest for us */
			netif_carrier_off(dev);

			/* set Speed to 10Mbps */
			/* read current state */
			status = mdio_read(dev, mii_phy->phy_addr, MII_CONTROL);

			/* disable auto negotiation and force 10MBit mode*/
			mdio_write(dev, mii_phy->phy_addr,
				   MII_CONTROL, status & ~(MII_CNTL_SPEED |
					MII_CNTL_AUTO));
			break;

		case IF_PORT_100BASET: /* 100BaseT */
		case IF_PORT_100BASETX: /* 100BaseTx */
			dev->if_port = map->port;

			/* we are going to change the media type, so the Link
			 * will be temporary down and we need to reflect that
			 * here. When the Link comes up again, it will be
			 * sensed by the sis_timer procedure, which also does
			 * all the rest for us */
			netif_carrier_off(dev);

			/* set Speed to 100Mbps */
			/* disable auto negotiation and enable 100MBit Mode */
			status = mdio_read(dev, mii_phy->phy_addr, MII_CONTROL);
			mdio_write(dev, mii_phy->phy_addr,
				   MII_CONTROL, (status & ~MII_CNTL_SPEED) |
				   MII_CNTL_SPEED);

			break;

		case IF_PORT_10BASE2: /* 10Base2 */
		case IF_PORT_AUI: /* AUI */
		case IF_PORT_100BASEFX: /* 100BaseFx */
                	/* These Modes are not supported (are they?)*/
			return -EOPNOTSUPP;

		default:
			return -EINVAL;
		}
	}
	return 0;
}

/**
 *	sis900_mcast_bitnr - compute hashtable index
 *	@addr: multicast address
 *	@revision: revision id of chip
 *
 *	SiS 900 uses the most sigificant 7 bits to index a 128 bits multicast
 *	hash table, which makes this function a little bit different from other drivers
 *	SiS 900 B0 & 635 M/B uses the most significat 8 bits to index 256 bits
 *   	multicast hash table.
 */

static inline u16 sis900_mcast_bitnr(u8 *addr, u8 revision)
{

	u32 crc = ether_crc(6, addr);

	/* leave 8 or 7 most siginifant bits */
	if ((revision >= SIS635A_900_REV) || (revision == SIS900B_900_REV))
		return (int)(crc >> 24);
	else
		return (int)(crc >> 25);
}

/**
 *	set_rx_mode - Set SiS900 receive mode
 *	@net_dev: the net device to be set
 *
 *	Set SiS900 receive mode for promiscuous, multicast, or broadcast mode.
 *	And set the appropriate multicast filter.
 *	Multicast hash table changes from 128 to 256 bits for 635M/B & 900B0.
 */

static void set_rx_mode(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	u16 mc_filter[16] = {0};	/* 256/128 bits multicast hash table */
	int i, table_entries;
	u32 rx_mode;

	/* 635 Hash Table entries = 256(2^16) */
	if((sis_priv->chipset_rev >= SIS635A_900_REV) ||
			(sis_priv->chipset_rev == SIS900B_900_REV))
		table_entries = 16;
	else
		table_entries = 8;

	if (net_dev->flags & IFF_PROMISC) {
		/* Accept any kinds of packets */
		rx_mode = RFPromiscuous;
		for (i = 0; i < table_entries; i++)
			mc_filter[i] = 0xffff;
	} else if ((netdev_mc_count(net_dev) > multicast_filter_limit) ||
		   (net_dev->flags & IFF_ALLMULTI)) {
		/* too many multicast addresses or accept all multicast packet */
		rx_mode = RFAAB | RFAAM;
		for (i = 0; i < table_entries; i++)
			mc_filter[i] = 0xffff;
	} else {
		/* Accept Broadcast packet, destination address matchs our
		 * MAC address, use Receive Filter to reject unwanted MCAST
		 * packets */
		struct netdev_hw_addr *ha;
		rx_mode = RFAAB;

		netdev_for_each_mc_addr(ha, net_dev) {
			unsigned int bit_nr;

			bit_nr = sis900_mcast_bitnr(ha->addr,
						    sis_priv->chipset_rev);
			mc_filter[bit_nr >> 4] |= (1 << (bit_nr & 0xf));
		}
	}

	/* update Multicast Hash Table in Receive Filter */
	for (i = 0; i < table_entries; i++) {
                /* why plus 0x04 ??, That makes the correct value for hash table. */
		sw32(rfcr, (u32)(0x00000004 + i) << RFADDR_shift);
		sw32(rfdr, mc_filter[i]);
	}

	sw32(rfcr, RFEN | rx_mode);

	/* sis900 is capable of looping back packets at MAC level for
	 * debugging purpose */
	if (net_dev->flags & IFF_LOOPBACK) {
		u32 cr_saved;
		/* We must disable Tx/Rx before setting loopback mode */
		cr_saved = sr32(cr);
		sw32(cr, cr_saved | TxDIS | RxDIS);
		/* enable loopback */
		sw32(txcfg, sr32(txcfg) | TxMLB);
		sw32(rxcfg, sr32(rxcfg) | RxATX);
		/* restore cr */
		sw32(cr, cr_saved);
	}
}

/**
 *	sis900_reset - Reset sis900 MAC
 *	@net_dev: the net device to reset
 *
 *	reset sis900 MAC and wait until finished
 *	reset through command register
 *	change backoff algorithm for 900B0 & 635 M/B
 */

static void sis900_reset(struct net_device *net_dev)
{
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;
	u32 status = TxRCMP | RxRCMP;
	int i;

	sw32(ier, 0);
	sw32(imr, 0);
	sw32(rfcr, 0);

	sw32(cr, RxRESET | TxRESET | RESET | sr32(cr));

	/* Check that the chip has finished the reset. */
	for (i = 0; status && (i < 1000); i++)
		status ^= sr32(isr) & status;

	if (sis_priv->chipset_rev >= SIS635A_900_REV ||
	    sis_priv->chipset_rev == SIS900B_900_REV)
		sw32(cfg, PESEL | RND_CNT);
	else
		sw32(cfg, PESEL);
}

/**
 *	sis900_remove - Remove sis900 device
 *	@pci_dev: the pci device to be removed
 *
 *	remove and release SiS900 net device
 */

static void sis900_remove(struct pci_dev *pci_dev)
{
	struct net_device *net_dev = pci_get_drvdata(pci_dev);
	struct sis900_private *sis_priv = netdev_priv(net_dev);

	unregister_netdev(net_dev);

	while (sis_priv->first_mii) {
		struct mii_phy *phy = sis_priv->first_mii;

		sis_priv->first_mii = phy->next;
		kfree(phy);
	}

	dma_free_coherent(&pci_dev->dev, RX_TOTAL_SIZE, sis_priv->rx_ring,
			  sis_priv->rx_ring_dma);
	dma_free_coherent(&pci_dev->dev, TX_TOTAL_SIZE, sis_priv->tx_ring,
			  sis_priv->tx_ring_dma);
	pci_iounmap(pci_dev, sis_priv->ioaddr);
	free_netdev(net_dev);
	pci_release_regions(pci_dev);
}

static int __maybe_unused sis900_suspend(struct device *dev)
{
	struct net_device *net_dev = dev_get_drvdata(dev);
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;

	if(!netif_running(net_dev))
		return 0;

	netif_stop_queue(net_dev);
	netif_device_detach(net_dev);

	/* Stop the chip's Tx and Rx Status Machine */
	sw32(cr, RxDIS | TxDIS | sr32(cr));

	return 0;
}

static int __maybe_unused sis900_resume(struct device *dev)
{
	struct net_device *net_dev = dev_get_drvdata(dev);
	struct sis900_private *sis_priv = netdev_priv(net_dev);
	void __iomem *ioaddr = sis_priv->ioaddr;

	if(!netif_running(net_dev))
		return 0;

	sis900_init_rxfilter(net_dev);

	sis900_init_tx_ring(net_dev);
	sis900_init_rx_ring(net_dev);

	set_rx_mode(net_dev);

	netif_device_attach(net_dev);
	netif_start_queue(net_dev);

	/* Workaround for EDB */
	sis900_set_mode(sis_priv, HW_SPEED_10_MBPS, FDX_CAPABLE_HALF_SELECTED);

	/* Enable all known interrupts by setting the interrupt mask. */
	sw32(imr, RxSOVR | RxORN | RxERR | RxOK | TxURN | TxERR | TxDESC);
	sw32(cr, RxENA | sr32(cr));
	sw32(ier, IE);

	sis900_check_mode(net_dev, sis_priv->mii);

	return 0;
}

static SIMPLE_DEV_PM_OPS(sis900_pm_ops, sis900_suspend, sis900_resume);

static struct pci_driver sis900_pci_driver = {
	.name		= SIS900_MODULE_NAME,
	.id_table	= sis900_pci_tbl,
	.probe		= sis900_probe,
	.remove		= sis900_remove,
	.driver.pm	= &sis900_pm_ops,
};

static int __init sis900_init_module(void)
{
/* when a module, this is printed whether or not devices are found in probe */
#ifdef MODULE
	printk(version);
#endif

	return pci_register_driver(&sis900_pci_driver);
}

static void __exit sis900_cleanup_module(void)
{
	pci_unregister_driver(&sis900_pci_driver);
}

module_init(sis900_init_module);
module_exit(sis900_cleanup_module);


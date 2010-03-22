/*======================================================================

    A PCMCIA ethernet driver for Asix AX88190-based cards

    The Asix AX88190 is a NS8390-derived chipset with a few nasty
    idiosyncracies that make it very inconvenient to support with a
    standard 8390 driver.  This driver is based on pcnet_cs, with the
    tweaked 8390 code grafted on the end.  Much of what I did was to
    clean up and update a similar driver supplied by Asix, which was
    adapted by William Lee, william@asix.com.tw.

    Copyright (C) 2001 David A. Hinds -- dahinds@users.sourceforge.net

    axnet_cs.c 1.28 2002/06/29 06:27:37

    The network driver code is based on Donald Becker's NE2000 code:

    Written 1992,1993 by Donald Becker.
    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU General Public License,
    incorporated herein by reference.
    Donald Becker may be reached at becker@scyld.com

======================================================================*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include "../8390.h"

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#define AXNET_CMD	0x00
#define AXNET_DATAPORT	0x10	/* NatSemi-defined port window offset. */
#define AXNET_RESET	0x1f	/* Issue a read to reset, a write to clear. */
#define AXNET_MII_EEP	0x14	/* Offset of MII access port */
#define AXNET_TEST	0x15	/* Offset of TEST Register port */
#define AXNET_GPIO	0x17	/* Offset of General Purpose Register Port */

#define AXNET_START_PG	0x40	/* First page of TX buffer */
#define AXNET_STOP_PG	0x80	/* Last page +1 of RX ring */

#define AXNET_RDC_TIMEOUT 0x02	/* Max wait in jiffies for Tx RDC */

#define IS_AX88190	0x0001
#define IS_AX88790	0x0002

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("Asix AX88190 PCMCIA ethernet driver");
MODULE_LICENSE("GPL");


/*====================================================================*/

static int axnet_config(struct pcmcia_device *link);
static void axnet_release(struct pcmcia_device *link);
static int axnet_open(struct net_device *dev);
static int axnet_close(struct net_device *dev);
static int axnet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static netdev_tx_t axnet_start_xmit(struct sk_buff *skb,
					  struct net_device *dev);
static struct net_device_stats *get_stats(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static void axnet_tx_timeout(struct net_device *dev);
static const struct ethtool_ops netdev_ethtool_ops;
static irqreturn_t ei_irq_wrapper(int irq, void *dev_id);
static void ei_watchdog(u_long arg);
static void axnet_reset_8390(struct net_device *dev);

static int mdio_read(unsigned int addr, int phy_id, int loc);
static void mdio_write(unsigned int addr, int phy_id, int loc, int value);

static void get_8390_hdr(struct net_device *,
			 struct e8390_pkt_hdr *, int);
static void block_input(struct net_device *dev, int count,
			struct sk_buff *skb, int ring_offset);
static void block_output(struct net_device *dev, int count,
			 const u_char *buf, const int start_page);

static void axnet_detach(struct pcmcia_device *p_dev);

static void AX88190_init(struct net_device *dev, int startp);
static int ax_open(struct net_device *dev);
static int ax_close(struct net_device *dev);
static irqreturn_t ax_interrupt(int irq, void *dev_id);

/*====================================================================*/

typedef struct axnet_dev_t {
	struct pcmcia_device	*p_dev;
    dev_node_t		node;
    caddr_t		base;
    struct timer_list	watchdog;
    int			stale, fast_poll;
    u_short		link_status;
    u_char		duplex_flag;
    int			phy_id;
    int			flags;
} axnet_dev_t;

static inline axnet_dev_t *PRIV(struct net_device *dev)
{
	void *p = (char *)netdev_priv(dev) + sizeof(struct ei_device);
	return p;
}

static const struct net_device_ops axnet_netdev_ops = {
	.ndo_open 		= axnet_open,
	.ndo_stop		= axnet_close,
	.ndo_do_ioctl		= axnet_ioctl,
	.ndo_start_xmit		= axnet_start_xmit,
	.ndo_tx_timeout		= axnet_tx_timeout,
	.ndo_get_stats		= get_stats,
	.ndo_set_multicast_list = set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/*======================================================================

    axnet_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

======================================================================*/

static int axnet_probe(struct pcmcia_device *link)
{
    axnet_dev_t *info;
    struct net_device *dev;
    struct ei_device *ei_local;

    dev_dbg(&link->dev, "axnet_attach()\n");

    dev = alloc_etherdev(sizeof(struct ei_device) + sizeof(axnet_dev_t));
    if (!dev)
	return -ENOMEM;

    ei_local = netdev_priv(dev);
    spin_lock_init(&ei_local->page_lock);

    info = PRIV(dev);
    info->p_dev = link;
    link->priv = dev;
    link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;

    dev->netdev_ops = &axnet_netdev_ops;

    SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);
    dev->watchdog_timeo = TX_TIMEOUT;

    return axnet_config(link);
} /* axnet_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void axnet_detach(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;

    dev_dbg(&link->dev, "axnet_detach(0x%p)\n", link);

    if (link->dev_node)
	unregister_netdev(dev);

    axnet_release(link);

    free_netdev(dev);
} /* axnet_detach */

/*======================================================================

    This probes for a card's hardware address by reading the PROM.

======================================================================*/

static int get_prom(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    unsigned int ioaddr = dev->base_addr;
    int i, j;

    /* This is based on drivers/net/ne.c */
    struct {
	u_char value, offset;
    } program_seq[] = {
	{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, /* Select page 0*/
	{0x01,	EN0_DCFG},	/* Set word-wide access. */
	{0x00,	EN0_RCNTLO},	/* Clear the count regs. */
	{0x00,	EN0_RCNTHI},
	{0x00,	EN0_IMR},	/* Mask completion irq. */
	{0xFF,	EN0_ISR},
	{E8390_RXOFF|0x40, EN0_RXCR},	/* 0x60  Set to monitor */
	{E8390_TXOFF, EN0_TXCR},	/* 0x02  and loopback mode. */
	{0x10,	EN0_RCNTLO},
	{0x00,	EN0_RCNTHI},
	{0x00,	EN0_RSARLO},	/* DMA starting at 0x0400. */
	{0x04,	EN0_RSARHI},
	{E8390_RREAD+E8390_START, E8390_CMD},
    };

    /* Not much of a test, but the alternatives are messy */
    if (link->conf.ConfigBase != 0x03c0)
	return 0;

    axnet_reset_8390(dev);
    mdelay(10);

    for (i = 0; i < ARRAY_SIZE(program_seq); i++)
	outb_p(program_seq[i].value, ioaddr + program_seq[i].offset);

    for (i = 0; i < 6; i += 2) {
	j = inw(ioaddr + AXNET_DATAPORT);
	dev->dev_addr[i] = j & 0xff;
	dev->dev_addr[i+1] = j >> 8;
    }
    return 1;
} /* get_prom */

/*======================================================================

    axnet_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.

======================================================================*/

static int try_io_port(struct pcmcia_device *link)
{
    int j, ret;
    if (link->io.NumPorts1 == 32) {
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	if (link->io.NumPorts2 > 0) {
	    /* for master/slave multifunction cards */
	    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
	    link->irq.Attributes =
		IRQ_TYPE_DYNAMIC_SHARING;
	}
    } else {
	/* This should be two 16-port windows */
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.Attributes2 = IO_DATA_PATH_WIDTH_16;
    }
    if (link->io.BasePort1 == 0) {
	link->io.IOAddrLines = 16;
	for (j = 0; j < 0x400; j += 0x20) {
	    link->io.BasePort1 = j ^ 0x300;
	    link->io.BasePort2 = (j ^ 0x300) + 0x10;
	    ret = pcmcia_request_io(link, &link->io);
	    if (ret == 0)
		    return ret;
	}
	return ret;
    } else {
	return pcmcia_request_io(link, &link->io);
    }
}

static int axnet_configcheck(struct pcmcia_device *p_dev,
			     cistpl_cftable_entry_t *cfg,
			     cistpl_cftable_entry_t *dflt,
			     unsigned int vcc,
			     void *priv_data)
{
	int i;
	cistpl_io_t *io = &cfg->io;

	if (cfg->index == 0 || cfg->io.nwin == 0)
		return -ENODEV;

	p_dev->conf.ConfigIndex = 0x05;
	/* For multifunction cards, by convention, we configure the
	   network function with window 0, and serial with window 1 */
	if (io->nwin > 1) {
		i = (io->win[1].len > io->win[0].len);
		p_dev->io.BasePort2 = io->win[1-i].base;
		p_dev->io.NumPorts2 = io->win[1-i].len;
	} else {
		i = p_dev->io.NumPorts2 = 0;
	}
	p_dev->io.BasePort1 = io->win[i].base;
	p_dev->io.NumPorts1 = io->win[i].len;
	p_dev->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
	if (p_dev->io.NumPorts1 + p_dev->io.NumPorts2 >= 32)
		return try_io_port(p_dev);

	return -ENODEV;
}

static int axnet_config(struct pcmcia_device *link)
{
    struct net_device *dev = link->priv;
    axnet_dev_t *info = PRIV(dev);
    int i, j, j2, ret;

    dev_dbg(&link->dev, "axnet_config(0x%p)\n", link);

    /* don't trust the CIS on this; Linksys got it wrong */
    link->conf.Present = 0x63;
    ret = pcmcia_loop_config(link, axnet_configcheck, NULL);
    if (ret != 0)
	goto failed;

    ret = pcmcia_request_irq(link, &link->irq);
    if (ret)
	    goto failed;
    
    if (link->io.NumPorts2 == 8) {
	link->conf.Attributes |= CONF_ENABLE_SPKR;
	link->conf.Status = CCSR_AUDIO_ENA;
    }
    
    ret = pcmcia_request_configuration(link, &link->conf);
    if (ret)
	    goto failed;

    dev->irq = link->irq.AssignedIRQ;
    dev->base_addr = link->io.BasePort1;

    if (!get_prom(link)) {
	printk(KERN_NOTICE "axnet_cs: this is not an AX88190 card!\n");
	printk(KERN_NOTICE "axnet_cs: use pcnet_cs instead.\n");
	goto failed;
    }

    ei_status.name = "AX88190";
    ei_status.word16 = 1;
    ei_status.tx_start_page = AXNET_START_PG;
    ei_status.rx_start_page = AXNET_START_PG + TX_PAGES;
    ei_status.stop_page = AXNET_STOP_PG;
    ei_status.reset_8390 = &axnet_reset_8390;
    ei_status.get_8390_hdr = &get_8390_hdr;
    ei_status.block_input = &block_input;
    ei_status.block_output = &block_output;

    if (inb(dev->base_addr + AXNET_TEST) != 0)
	info->flags |= IS_AX88790;
    else
	info->flags |= IS_AX88190;

    if (info->flags & IS_AX88790)
	outb(0x10, dev->base_addr + AXNET_GPIO);  /* select Internal PHY */

    for (i = 0; i < 32; i++) {
	j = mdio_read(dev->base_addr + AXNET_MII_EEP, i, 1);
	j2 = mdio_read(dev->base_addr + AXNET_MII_EEP, i, 2);
	if (j == j2) continue;
	if ((j != 0) && (j != 0xffff)) break;
    }

    /* Maybe PHY is in power down mode. (PPD_SET = 1) 
       Bit 2 of CCSR is active low. */ 
    if (i == 32) {
	conf_reg_t reg = { 0, CS_WRITE, CISREG_CCSR, 0x04 };
 	pcmcia_access_configuration_register(link, &reg);
	for (i = 0; i < 32; i++) {
	    j = mdio_read(dev->base_addr + AXNET_MII_EEP, i, 1);
	    j2 = mdio_read(dev->base_addr + AXNET_MII_EEP, i, 2);
	    if (j == j2) continue;
	    if ((j != 0) && (j != 0xffff)) break;
	}
    }

    info->phy_id = (i < 32) ? i : -1;
    link->dev_node = &info->node;
    SET_NETDEV_DEV(dev, &link->dev);

    if (register_netdev(dev) != 0) {
	printk(KERN_NOTICE "axnet_cs: register_netdev() failed\n");
	link->dev_node = NULL;
	goto failed;
    }

    strcpy(info->node.dev_name, dev->name);

    printk(KERN_INFO "%s: Asix AX88%d90: io %#3lx, irq %d, "
	   "hw_addr %pM\n",
	   dev->name, ((info->flags & IS_AX88790) ? 7 : 1),
	   dev->base_addr, dev->irq,
	   dev->dev_addr);
    if (info->phy_id != -1) {
	dev_dbg(&link->dev, "  MII transceiver at index %d, status %x.\n", info->phy_id, j);
    } else {
	printk(KERN_NOTICE "  No MII transceivers found!\n");
    }
    return 0;

failed:
    axnet_release(link);
    return -ENODEV;
} /* axnet_config */

/*======================================================================

    After a card is removed, axnet_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void axnet_release(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
}

static int axnet_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int axnet_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		axnet_reset_8390(dev);
		AX88190_init(dev, 1);
		netif_device_attach(dev);
	}

	return 0;
}


/*======================================================================

    MII interface support

======================================================================*/

#define MDIO_SHIFT_CLK		0x01
#define MDIO_DATA_WRITE0	0x00
#define MDIO_DATA_WRITE1	0x08
#define MDIO_DATA_READ		0x04
#define MDIO_MASK		0x0f
#define MDIO_ENB_IN		0x02

static void mdio_sync(unsigned int addr)
{
    int bits;
    for (bits = 0; bits < 32; bits++) {
	outb_p(MDIO_DATA_WRITE1, addr);
	outb_p(MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, addr);
    }
}

static int mdio_read(unsigned int addr, int phy_id, int loc)
{
    u_int cmd = (0xf6<<10)|(phy_id<<5)|loc;
    int i, retval = 0;

    mdio_sync(addr);
    for (i = 14; i >= 0; i--) {
	int dat = (cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
	outb_p(dat, addr);
	outb_p(dat | MDIO_SHIFT_CLK, addr);
    }
    for (i = 19; i > 0; i--) {
	outb_p(MDIO_ENB_IN, addr);
	retval = (retval << 1) | ((inb_p(addr) & MDIO_DATA_READ) != 0);
	outb_p(MDIO_ENB_IN | MDIO_SHIFT_CLK, addr);
    }
    return (retval>>1) & 0xffff;
}

static void mdio_write(unsigned int addr, int phy_id, int loc, int value)
{
    u_int cmd = (0x05<<28)|(phy_id<<23)|(loc<<18)|(1<<17)|value;
    int i;

    mdio_sync(addr);
    for (i = 31; i >= 0; i--) {
	int dat = (cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
	outb_p(dat, addr);
	outb_p(dat | MDIO_SHIFT_CLK, addr);
    }
    for (i = 1; i >= 0; i--) {
	outb_p(MDIO_ENB_IN, addr);
	outb_p(MDIO_ENB_IN | MDIO_SHIFT_CLK, addr);
    }
}

/*====================================================================*/

static int axnet_open(struct net_device *dev)
{
    int ret;
    axnet_dev_t *info = PRIV(dev);
    struct pcmcia_device *link = info->p_dev;
    unsigned int nic_base = dev->base_addr;
    
    dev_dbg(&link->dev, "axnet_open('%s')\n", dev->name);

    if (!pcmcia_dev_present(link))
	return -ENODEV;

    outb_p(0xFF, nic_base + EN0_ISR); /* Clear bogus intr. */
    ret = request_irq(dev->irq, ei_irq_wrapper, IRQF_SHARED, "axnet_cs", dev);
    if (ret)
	    return ret;

    link->open++;

    info->link_status = 0x00;
    init_timer(&info->watchdog);
    info->watchdog.function = &ei_watchdog;
    info->watchdog.data = (u_long)dev;
    info->watchdog.expires = jiffies + HZ;
    add_timer(&info->watchdog);

    return ax_open(dev);
} /* axnet_open */

/*====================================================================*/

static int axnet_close(struct net_device *dev)
{
    axnet_dev_t *info = PRIV(dev);
    struct pcmcia_device *link = info->p_dev;

    dev_dbg(&link->dev, "axnet_close('%s')\n", dev->name);

    ax_close(dev);
    free_irq(dev->irq, dev);
    
    link->open--;
    netif_stop_queue(dev);
    del_timer_sync(&info->watchdog);

    return 0;
} /* axnet_close */

/*======================================================================

    Hard reset the card.  This used to pause for the same period that
    a 8390 reset command required, but that shouldn't be necessary.

======================================================================*/

static void axnet_reset_8390(struct net_device *dev)
{
    unsigned int nic_base = dev->base_addr;
    int i;

    ei_status.txing = ei_status.dmaing = 0;

    outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, nic_base + E8390_CMD);

    outb(inb(nic_base + AXNET_RESET), nic_base + AXNET_RESET);

    for (i = 0; i < 100; i++) {
	if ((inb_p(nic_base+EN0_ISR) & ENISR_RESET) != 0)
	    break;
	udelay(100);
    }
    outb_p(ENISR_RESET, nic_base + EN0_ISR); /* Ack intr. */
    
    if (i == 100)
	printk(KERN_ERR "%s: axnet_reset_8390() did not complete.\n",
	       dev->name);
    
} /* axnet_reset_8390 */

/*====================================================================*/

static irqreturn_t ei_irq_wrapper(int irq, void *dev_id)
{
    struct net_device *dev = dev_id;
    PRIV(dev)->stale = 0;
    return ax_interrupt(irq, dev_id);
}

static void ei_watchdog(u_long arg)
{
    struct net_device *dev = (struct net_device *)(arg);
    axnet_dev_t *info = PRIV(dev);
    unsigned int nic_base = dev->base_addr;
    unsigned int mii_addr = nic_base + AXNET_MII_EEP;
    u_short link;

    if (!netif_device_present(dev)) goto reschedule;

    /* Check for pending interrupt with expired latency timer: with
       this, we can limp along even if the interrupt is blocked */
    if (info->stale++ && (inb_p(nic_base + EN0_ISR) & ENISR_ALL)) {
	if (!info->fast_poll)
	    printk(KERN_INFO "%s: interrupt(s) dropped!\n", dev->name);
	ei_irq_wrapper(dev->irq, dev);
	info->fast_poll = HZ;
    }
    if (info->fast_poll) {
	info->fast_poll--;
	info->watchdog.expires = jiffies + 1;
	add_timer(&info->watchdog);
	return;
    }

    if (info->phy_id < 0)
	goto reschedule;
    link = mdio_read(mii_addr, info->phy_id, 1);
    if (!link || (link == 0xffff)) {
	printk(KERN_INFO "%s: MII is missing!\n", dev->name);
	info->phy_id = -1;
	goto reschedule;
    }

    link &= 0x0004;
    if (link != info->link_status) {
	u_short p = mdio_read(mii_addr, info->phy_id, 5);
	printk(KERN_INFO "%s: %s link beat\n", dev->name,
	       (link) ? "found" : "lost");
	if (link) {
	    info->duplex_flag = (p & 0x0140) ? 0x80 : 0x00;
	    if (p)
		printk(KERN_INFO "%s: autonegotiation complete: "
		       "%sbaseT-%cD selected\n", dev->name,
		       ((p & 0x0180) ? "100" : "10"),
		       ((p & 0x0140) ? 'F' : 'H'));
	    else
		printk(KERN_INFO "%s: link partner did not autonegotiate\n",
		       dev->name);
	    AX88190_init(dev, 1);
	}
	info->link_status = link;
    }

reschedule:
    info->watchdog.expires = jiffies + HZ;
    add_timer(&info->watchdog);
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "axnet_cs");
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

/*====================================================================*/

static int axnet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    axnet_dev_t *info = PRIV(dev);
    struct mii_ioctl_data *data = if_mii(rq);
    unsigned int mii_addr = dev->base_addr + AXNET_MII_EEP;
    switch (cmd) {
    case SIOCGMIIPHY:
	data->phy_id = info->phy_id;
    case SIOCGMIIREG:		/* Read MII PHY register. */
	data->val_out = mdio_read(mii_addr, data->phy_id, data->reg_num & 0x1f);
	return 0;
    case SIOCSMIIREG:		/* Write MII PHY register. */
	mdio_write(mii_addr, data->phy_id, data->reg_num & 0x1f, data->val_in);
	return 0;
    }
    return -EOPNOTSUPP;
}

/*====================================================================*/

static void get_8390_hdr(struct net_device *dev,
			 struct e8390_pkt_hdr *hdr,
			 int ring_page)
{
    unsigned int nic_base = dev->base_addr;

    outb_p(0, nic_base + EN0_RSARLO);		/* On page boundary */
    outb_p(ring_page, nic_base + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, nic_base + AXNET_CMD);

    insw(nic_base + AXNET_DATAPORT, hdr,
	    sizeof(struct e8390_pkt_hdr)>>1);
    /* Fix for big endian systems */
    hdr->count = le16_to_cpu(hdr->count);

}

/*====================================================================*/

static void block_input(struct net_device *dev, int count,
			struct sk_buff *skb, int ring_offset)
{
    unsigned int nic_base = dev->base_addr;
    int xfer_count = count;
    char *buf = skb->data;

    if ((ei_debug > 4) && (count != 4))
	    pr_debug("%s: [bi=%d]\n", dev->name, count+4);
    outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
    outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
    outb_p(E8390_RREAD+E8390_START, nic_base + AXNET_CMD);

    insw(nic_base + AXNET_DATAPORT,buf,count>>1);
    if (count & 0x01)
	buf[count-1] = inb(nic_base + AXNET_DATAPORT), xfer_count++;

}

/*====================================================================*/

static void block_output(struct net_device *dev, int count,
			 const u_char *buf, const int start_page)
{
    unsigned int nic_base = dev->base_addr;

    pr_debug("%s: [bo=%d]\n", dev->name, count);

    /* Round the count up for word writes.  Do we need to do this?
       What effect will an odd byte count have on the 8390?
       I should check someday. */
    if (count & 0x01)
	count++;

    outb_p(0x00, nic_base + EN0_RSARLO);
    outb_p(start_page, nic_base + EN0_RSARHI);
    outb_p(E8390_RWRITE+E8390_START, nic_base + AXNET_CMD);
    outsw(nic_base + AXNET_DATAPORT, buf, count>>1);
}

static struct pcmcia_device_id axnet_ids[] = {
	PCMCIA_PFC_DEVICE_MANF_CARD(0, 0x016c, 0x0081),
	PCMCIA_DEVICE_MANF_CARD(0x018a, 0x0301),
	PCMCIA_DEVICE_MANF_CARD(0x026f, 0x0301),
	PCMCIA_DEVICE_MANF_CARD(0x026f, 0x0303),
	PCMCIA_DEVICE_MANF_CARD(0x026f, 0x0309),
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1106),
	PCMCIA_DEVICE_MANF_CARD(0x8a01, 0xc1ab),
	PCMCIA_DEVICE_MANF_CARD(0x021b, 0x0202), 
	PCMCIA_DEVICE_MANF_CARD(0xffff, 0x1090),
	PCMCIA_DEVICE_PROD_ID12("AmbiCom,Inc.", "Fast Ethernet PC Card(AMB8110)", 0x49b020a7, 0x119cc9fc),
	PCMCIA_DEVICE_PROD_ID124("Fast Ethernet", "16-bit PC Card", "AX88190", 0xb4be14e3, 0x9a12eb6a, 0xab9be5ef),
	PCMCIA_DEVICE_PROD_ID12("ASIX", "AX88190", 0x0959823b, 0xab9be5ef),
	PCMCIA_DEVICE_PROD_ID12("Billionton", "LNA-100B", 0x552ab682, 0xbc3b87e1),
	PCMCIA_DEVICE_PROD_ID12("CHEETAH ETHERCARD", "EN2228", 0x00fa7bc8, 0x00e990cc),
	PCMCIA_DEVICE_PROD_ID12("CNet", "CNF301", 0xbc477dde, 0x78c5f40b),
	PCMCIA_DEVICE_PROD_ID12("corega K.K.", "corega FEther PCC-TXD", 0x5261440f, 0x436768c5),
	PCMCIA_DEVICE_PROD_ID12("corega K.K.", "corega FEtherII PCC-TXD", 0x5261440f, 0x730df72e),
	PCMCIA_DEVICE_PROD_ID12("corega K.K.", "corega FEther PCC-TXM", 0x5261440f, 0x3abbd061),
	PCMCIA_DEVICE_PROD_ID12("Dynalink", "L100C16", 0x55632fd5, 0x66bc2a90),
	PCMCIA_DEVICE_PROD_ID12("IO DATA", "ETXPCM", 0x547e66dc, 0x233adac2),
	PCMCIA_DEVICE_PROD_ID12("Linksys", "EtherFast 10/100 PC Card (PCMPC100 V3)", 0x0733cc81, 0x232019a8),
	PCMCIA_DEVICE_PROD_ID12("MELCO", "LPC3-TX", 0x481e0094, 0xf91af609),
	PCMCIA_DEVICE_PROD_ID12("NETGEAR", "FA411", 0x9aa79dc3, 0x40fad875),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA", "100BASE", 0x281f1c5d, 0x7c2add04),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA", "FastEtherCard", 0x281f1c5d, 0x7ef26116),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA", "FEP501", 0x281f1c5d, 0x2e272058),
	PCMCIA_DEVICE_PROD_ID14("Network Everywhere", "AX88190", 0x820a67b6,  0xab9be5ef),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, axnet_ids);

static struct pcmcia_driver axnet_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "axnet_cs",
	},
	.probe		= axnet_probe,
	.remove		= axnet_detach,
	.id_table       = axnet_ids,
	.suspend	= axnet_suspend,
	.resume		= axnet_resume,
};

static int __init init_axnet_cs(void)
{
	return pcmcia_register_driver(&axnet_cs_driver);
}

static void __exit exit_axnet_cs(void)
{
	pcmcia_unregister_driver(&axnet_cs_driver);
}

module_init(init_axnet_cs);
module_exit(exit_axnet_cs);

/*====================================================================*/

/* 8390.c: A general NS8390 ethernet driver core for linux. */
/*
	Written 1992-94 by Donald Becker.
  
	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

  This is the chip-specific code for many 8390-based ethernet adaptors.
  This is not a complete driver, it must be combined with board-specific
  code such as ne.c, wd.c, 3c503.c, etc.

  Seeing how at least eight drivers use this code, (not counting the
  PCMCIA ones either) it is easy to break some card by what seems like
  a simple innocent change. Please contact me or Donald if you think
  you have found something that needs changing. -- PG

  Changelog:

  Paul Gortmaker	: remove set_bit lock, other cleanups.
  Paul Gortmaker	: add ei_get_8390_hdr() so we can pass skb's to 
			  ei_block_input() for eth_io_copy_and_sum().
  Paul Gortmaker	: exchange static int ei_pingpong for a #define,
			  also add better Tx error handling.
  Paul Gortmaker	: rewrite Rx overrun handling as per NS specs.
  Alexey Kuznetsov	: use the 8390's six bit hash multicast filter.
  Paul Gortmaker	: tweak ANK's above multicast changes a bit.
  Paul Gortmaker	: update packet statistics for v2.1.x
  Alan Cox		: support arbitary stupid port mappings on the
  			  68K Macintosh. Support >16bit I/O spaces
  Paul Gortmaker	: add kmod support for auto-loading of the 8390
			  module by all drivers that require it.
  Alan Cox		: Spinlocking work, added 'BUG_83C690'
  Paul Gortmaker	: Separate out Tx timeout code from Tx path.

  Sources:
  The National Semiconductor LAN Databook, and the 3Com 3c503 databook.

  */

static const char version_8390[] = KERN_INFO \
    "8390.c:v1.10cvs 9/23/94 Donald Becker (becker@scyld.com)\n";

#include <linux/bitops.h>
#include <asm/irq.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/interrupt.h>

#define BUG_83C690

/* These are the operational function interfaces to board-specific
   routines.
	void reset_8390(struct net_device *dev)
		Resets the board associated with DEV, including a hardware reset of
		the 8390.  This is only called when there is a transmit timeout, and
		it is always followed by 8390_init().
	void block_output(struct net_device *dev, int count, const unsigned char *buf,
					  int start_page)
		Write the COUNT bytes of BUF to the packet buffer at START_PAGE.  The
		"page" value uses the 8390's 256-byte pages.
	void get_8390_hdr(struct net_device *dev, struct e8390_hdr *hdr, int ring_page)
		Read the 4 byte, page aligned 8390 header. *If* there is a
		subsequent read, it will be of the rest of the packet.
	void block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
		Read COUNT bytes from the packet buffer into the skb data area. Start 
		reading from RING_OFFSET, the address as the 8390 sees it.  This will always
		follow the read of the 8390 header. 
*/
#define ei_reset_8390 (ei_local->reset_8390)
#define ei_block_output (ei_local->block_output)
#define ei_block_input (ei_local->block_input)
#define ei_get_8390_hdr (ei_local->get_8390_hdr)

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef ei_debug
int ei_debug = 1;
#endif

/* Index to functions. */
static void ei_tx_intr(struct net_device *dev);
static void ei_tx_err(struct net_device *dev);
static void ei_receive(struct net_device *dev);
static void ei_rx_overrun(struct net_device *dev);

/* Routines generic to NS8390-based boards. */
static void NS8390_trigger_send(struct net_device *dev, unsigned int length,
								int start_page);
static void do_set_multicast_list(struct net_device *dev);

/*
 *	SMP and the 8390 setup.
 *
 *	The 8390 isnt exactly designed to be multithreaded on RX/TX. There is
 *	a page register that controls bank and packet buffer access. We guard
 *	this with ei_local->page_lock. Nobody should assume or set the page other
 *	than zero when the lock is not held. Lock holders must restore page 0
 *	before unlocking. Even pure readers must take the lock to protect in 
 *	page 0.
 *
 *	To make life difficult the chip can also be very slow. We therefore can't
 *	just use spinlocks. For the longer lockups we disable the irq the device
 *	sits on and hold the lock. We must hold the lock because there is a dual
 *	processor case other than interrupts (get stats/set multicast list in
 *	parallel with each other and transmit).
 *
 *	Note: in theory we can just disable the irq on the card _but_ there is
 *	a latency on SMP irq delivery. So we can easily go "disable irq" "sync irqs"
 *	enter lock, take the queued irq. So we waddle instead of flying.
 *
 *	Finally by special arrangement for the purpose of being generally 
 *	annoying the transmit function is called bh atomic. That places
 *	restrictions on the user context callers as disable_irq won't save
 *	them.
 */
 
/**
 * ax_open - Open/initialize the board.
 * @dev: network device to initialize
 *
 * This routine goes all-out, setting everything
 * up anew at each open, even though many of these registers should only
 * need to be set once at boot.
 */
static int ax_open(struct net_device *dev)
{
	unsigned long flags;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);

	/*
	 *	Grab the page lock so we own the register set, then call
	 *	the init function.
	 */
      
      	spin_lock_irqsave(&ei_local->page_lock, flags);
	AX88190_init(dev, 1);
	/* Set the flag before we drop the lock, That way the IRQ arrives
	   after its set and we get no silly warnings */
	netif_start_queue(dev);
      	spin_unlock_irqrestore(&ei_local->page_lock, flags);
	ei_local->irqlock = 0;
	return 0;
}

#define dev_lock(dev) (((struct ei_device *)netdev_priv(dev))->page_lock)

/**
 * ax_close - shut down network device
 * @dev: network device to close
 *
 * Opposite of ax_open(). Only used when "ifconfig <devname> down" is done.
 */
static int ax_close(struct net_device *dev)
{
	unsigned long flags;

	/*
	 *      Hold the page lock during close
	 */

	spin_lock_irqsave(&dev_lock(dev), flags);
	AX88190_init(dev, 0);
	spin_unlock_irqrestore(&dev_lock(dev), flags);
	netif_stop_queue(dev);
	return 0;
}

/**
 * axnet_tx_timeout - handle transmit time out condition
 * @dev: network device which has apparently fallen asleep
 *
 * Called by kernel when device never acknowledges a transmit has
 * completed (or failed) - i.e. never posted a Tx related interrupt.
 */

static void axnet_tx_timeout(struct net_device *dev)
{
	long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int txsr, isr, tickssofar = jiffies - dev->trans_start;
	unsigned long flags;

	dev->stats.tx_errors++;

	spin_lock_irqsave(&ei_local->page_lock, flags);
	txsr = inb(e8390_base+EN0_TSR);
	isr = inb(e8390_base+EN0_ISR);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);

	printk(KERN_DEBUG "%s: Tx timed out, %s TSR=%#2x, ISR=%#2x, t=%d.\n",
		dev->name, (txsr & ENTSR_ABT) ? "excess collisions." :
		(isr) ? "lost interrupt?" : "cable problem?", txsr, isr, tickssofar);

	if (!isr && !dev->stats.tx_packets) 
	{
		/* The 8390 probably hasn't gotten on the cable yet. */
		ei_local->interface_num ^= 1;   /* Try a different xcvr.  */
	}

	/* Ugly but a reset can be slow, yet must be protected */
		
	spin_lock_irqsave(&ei_local->page_lock, flags);
		
	/* Try to restart the card.  Perhaps the user has fixed something. */
	ei_reset_8390(dev);
	AX88190_init(dev, 1);
		
	spin_unlock_irqrestore(&ei_local->page_lock, flags);
	netif_wake_queue(dev);
}
    
/**
 * axnet_start_xmit - begin packet transmission
 * @skb: packet to be sent
 * @dev: network device to which packet is sent
 *
 * Sends a packet to an 8390 network device.
 */
 
static netdev_tx_t axnet_start_xmit(struct sk_buff *skb,
					  struct net_device *dev)
{
	long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int length, send_length, output_page;
	unsigned long flags;
	u8 packet[ETH_ZLEN];
	
	netif_stop_queue(dev);

	length = skb->len;

	/* Mask interrupts from the ethercard. 
	   SMP: We have to grab the lock here otherwise the IRQ handler
	   on another CPU can flip window and race the IRQ mask set. We end
	   up trashing the mcast filter not disabling irqs if we don't lock */
	   
	spin_lock_irqsave(&ei_local->page_lock, flags);
	outb_p(0x00, e8390_base + EN0_IMR);
	
	/*
	 *	Slow phase with lock held.
	 */
	 
	ei_local->irqlock = 1;

	send_length = max(length, ETH_ZLEN);

	/*
	 * We have two Tx slots available for use. Find the first free
	 * slot, and then perform some sanity checks. With two Tx bufs,
	 * you get very close to transmitting back-to-back packets. With
	 * only one Tx buf, the transmitter sits idle while you reload the
	 * card, leaving a substantial gap between each transmitted packet.
	 */

	if (ei_local->tx1 == 0) 
	{
		output_page = ei_local->tx_start_page;
		ei_local->tx1 = send_length;
		if (ei_debug  &&  ei_local->tx2 > 0)
			printk(KERN_DEBUG "%s: idle transmitter tx2=%d, lasttx=%d, txing=%d.\n",
				dev->name, ei_local->tx2, ei_local->lasttx, ei_local->txing);
	}
	else if (ei_local->tx2 == 0) 
	{
		output_page = ei_local->tx_start_page + TX_PAGES/2;
		ei_local->tx2 = send_length;
		if (ei_debug  &&  ei_local->tx1 > 0)
			printk(KERN_DEBUG "%s: idle transmitter, tx1=%d, lasttx=%d, txing=%d.\n",
				dev->name, ei_local->tx1, ei_local->lasttx, ei_local->txing);
	}
	else
	{	/* We should never get here. */
		if (ei_debug)
			printk(KERN_DEBUG "%s: No Tx buffers free! tx1=%d tx2=%d last=%d\n",
				dev->name, ei_local->tx1, ei_local->tx2, ei_local->lasttx);
		ei_local->irqlock = 0;
		netif_stop_queue(dev);
		outb_p(ENISR_ALL, e8390_base + EN0_IMR);
		spin_unlock_irqrestore(&ei_local->page_lock, flags);
		dev->stats.tx_errors++;
		return NETDEV_TX_BUSY;
	}

	/*
	 * Okay, now upload the packet and trigger a send if the transmitter
	 * isn't already sending. If it is busy, the interrupt handler will
	 * trigger the send later, upon receiving a Tx done interrupt.
	 */

	if (length == skb->len)
		ei_block_output(dev, length, skb->data, output_page);
	else {
		memset(packet, 0, ETH_ZLEN);
		skb_copy_from_linear_data(skb, packet, skb->len);
		ei_block_output(dev, length, packet, output_page);
	}
	
	if (! ei_local->txing) 
	{
		ei_local->txing = 1;
		NS8390_trigger_send(dev, send_length, output_page);
		dev->trans_start = jiffies;
		if (output_page == ei_local->tx_start_page) 
		{
			ei_local->tx1 = -1;
			ei_local->lasttx = -1;
		}
		else 
		{
			ei_local->tx2 = -1;
			ei_local->lasttx = -2;
		}
	}
	else ei_local->txqueue++;

	if (ei_local->tx1  &&  ei_local->tx2)
		netif_stop_queue(dev);
	else
		netif_start_queue(dev);

	/* Turn 8390 interrupts back on. */
	ei_local->irqlock = 0;
	outb_p(ENISR_ALL, e8390_base + EN0_IMR);
	
	spin_unlock_irqrestore(&ei_local->page_lock, flags);

	dev_kfree_skb (skb);
	dev->stats.tx_bytes += send_length;
    
	return NETDEV_TX_OK;
}

/**
 * ax_interrupt - handle the interrupts from an 8390
 * @irq: interrupt number
 * @dev_id: a pointer to the net_device
 *
 * Handle the ether interface interrupts. We pull packets from
 * the 8390 via the card specific functions and fire them at the networking
 * stack. We also handle transmit completions and wake the transmit path if
 * necessary. We also update the counters and do other housekeeping as
 * needed.
 */

static irqreturn_t ax_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	long e8390_base;
	int interrupts, nr_serviced = 0, i;
	struct ei_device *ei_local;
    	int handled = 0;

	e8390_base = dev->base_addr;
	ei_local = netdev_priv(dev);

	/*
	 *	Protect the irq test too.
	 */
	 
	spin_lock(&ei_local->page_lock);

	if (ei_local->irqlock) 
	{
#if 1 /* This might just be an interrupt for a PCI device sharing this line */
		/* The "irqlock" check is only for testing. */
		printk(ei_local->irqlock
			   ? "%s: Interrupted while interrupts are masked! isr=%#2x imr=%#2x.\n"
			   : "%s: Reentering the interrupt handler! isr=%#2x imr=%#2x.\n",
			   dev->name, inb_p(e8390_base + EN0_ISR),
			   inb_p(e8390_base + EN0_IMR));
#endif
		spin_unlock(&ei_local->page_lock);
		return IRQ_NONE;
	}
    
	if (ei_debug > 3)
		printk(KERN_DEBUG "%s: interrupt(isr=%#2.2x).\n", dev->name,
			   inb_p(e8390_base + EN0_ISR));

	outb_p(0x00, e8390_base + EN0_ISR);
	ei_local->irqlock = 1;
   
	/* !!Assumption!! -- we stay in page 0.	 Don't break this. */
	while ((interrupts = inb_p(e8390_base + EN0_ISR)) != 0 &&
	       ++nr_serviced < MAX_SERVICE)
	{
		if (!netif_running(dev) || (interrupts == 0xff)) {
			if (ei_debug > 1)
				printk(KERN_WARNING "%s: interrupt from stopped card\n", dev->name);
			outb_p(interrupts, e8390_base + EN0_ISR);
			interrupts = 0;
			break;
		}
		handled = 1;

		/* AX88190 bug fix. */
		outb_p(interrupts, e8390_base + EN0_ISR);
		for (i = 0; i < 10; i++) {
			if (!(inb(e8390_base + EN0_ISR) & interrupts))
				break;
			outb_p(0, e8390_base + EN0_ISR);
			outb_p(interrupts, e8390_base + EN0_ISR);
		}
		if (interrupts & ENISR_OVER) 
			ei_rx_overrun(dev);
		else if (interrupts & (ENISR_RX+ENISR_RX_ERR)) 
		{
			/* Got a good (?) packet. */
			ei_receive(dev);
		}
		/* Push the next to-transmit packet through. */
		if (interrupts & ENISR_TX)
			ei_tx_intr(dev);
		else if (interrupts & ENISR_TX_ERR)
			ei_tx_err(dev);

		if (interrupts & ENISR_COUNTERS) 
		{
			dev->stats.rx_frame_errors += inb_p(e8390_base + EN0_COUNTER0);
			dev->stats.rx_crc_errors   += inb_p(e8390_base + EN0_COUNTER1);
			dev->stats.rx_missed_errors+= inb_p(e8390_base + EN0_COUNTER2);
		}
	}
    
	if (interrupts && ei_debug > 3) 
	{
		handled = 1;
		if (nr_serviced >= MAX_SERVICE) 
		{
			/* 0xFF is valid for a card removal */
			if(interrupts!=0xFF)
				printk(KERN_WARNING "%s: Too much work at interrupt, status %#2.2x\n",
				   dev->name, interrupts);
			outb_p(ENISR_ALL, e8390_base + EN0_ISR); /* Ack. most intrs. */
		} else {
			printk(KERN_WARNING "%s: unknown interrupt %#2x\n", dev->name, interrupts);
			outb_p(0xff, e8390_base + EN0_ISR); /* Ack. all intrs. */
		}
	}

	/* Turn 8390 interrupts back on. */
	ei_local->irqlock = 0;
	outb_p(ENISR_ALL, e8390_base + EN0_IMR);

	spin_unlock(&ei_local->page_lock);
	return IRQ_RETVAL(handled);
}

/**
 * ei_tx_err - handle transmitter error
 * @dev: network device which threw the exception
 *
 * A transmitter error has happened. Most likely excess collisions (which
 * is a fairly normal condition). If the error is one where the Tx will
 * have been aborted, we try and send another one right away, instead of
 * letting the failed packet sit and collect dust in the Tx buffer. This
 * is a much better solution as it avoids kernel based Tx timeouts, and
 * an unnecessary card reset.
 *
 * Called with lock held.
 */

static void ei_tx_err(struct net_device *dev)
{
	long e8390_base = dev->base_addr;
	unsigned char txsr = inb_p(e8390_base+EN0_TSR);
	unsigned char tx_was_aborted = txsr & (ENTSR_ABT+ENTSR_FU);

#ifdef VERBOSE_ERROR_DUMP
	printk(KERN_DEBUG "%s: transmitter error (%#2x): ", dev->name, txsr);
	if (txsr & ENTSR_ABT)
		printk("excess-collisions ");
	if (txsr & ENTSR_ND)
		printk("non-deferral ");
	if (txsr & ENTSR_CRS)
		printk("lost-carrier ");
	if (txsr & ENTSR_FU)
		printk("FIFO-underrun ");
	if (txsr & ENTSR_CDH)
		printk("lost-heartbeat ");
	printk("\n");
#endif

	if (tx_was_aborted)
		ei_tx_intr(dev);
	else 
	{
		dev->stats.tx_errors++;
		if (txsr & ENTSR_CRS) dev->stats.tx_carrier_errors++;
		if (txsr & ENTSR_CDH) dev->stats.tx_heartbeat_errors++;
		if (txsr & ENTSR_OWC) dev->stats.tx_window_errors++;
	}
}

/**
 * ei_tx_intr - transmit interrupt handler
 * @dev: network device for which tx intr is handled
 *
 * We have finished a transmit: check for errors and then trigger the next
 * packet to be sent. Called with lock held.
 */

static void ei_tx_intr(struct net_device *dev)
{
	long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int status = inb(e8390_base + EN0_TSR);
    
	/*
	 * There are two Tx buffers, see which one finished, and trigger
	 * the send of another one if it exists.
	 */
	ei_local->txqueue--;

	if (ei_local->tx1 < 0) 
	{
		if (ei_local->lasttx != 1 && ei_local->lasttx != -1)
			printk(KERN_ERR "%s: bogus last_tx_buffer %d, tx1=%d.\n",
				ei_local->name, ei_local->lasttx, ei_local->tx1);
		ei_local->tx1 = 0;
		if (ei_local->tx2 > 0) 
		{
			ei_local->txing = 1;
			NS8390_trigger_send(dev, ei_local->tx2, ei_local->tx_start_page + 6);
			dev->trans_start = jiffies;
			ei_local->tx2 = -1,
			ei_local->lasttx = 2;
		}
		else ei_local->lasttx = 20, ei_local->txing = 0;	
	}
	else if (ei_local->tx2 < 0) 
	{
		if (ei_local->lasttx != 2  &&  ei_local->lasttx != -2)
			printk("%s: bogus last_tx_buffer %d, tx2=%d.\n",
				ei_local->name, ei_local->lasttx, ei_local->tx2);
		ei_local->tx2 = 0;
		if (ei_local->tx1 > 0) 
		{
			ei_local->txing = 1;
			NS8390_trigger_send(dev, ei_local->tx1, ei_local->tx_start_page);
			dev->trans_start = jiffies;
			ei_local->tx1 = -1;
			ei_local->lasttx = 1;
		}
		else
			ei_local->lasttx = 10, ei_local->txing = 0;
	}
//	else printk(KERN_WARNING "%s: unexpected TX-done interrupt, lasttx=%d.\n",
//			dev->name, ei_local->lasttx);

	/* Minimize Tx latency: update the statistics after we restart TXing. */
	if (status & ENTSR_COL)
		dev->stats.collisions++;
	if (status & ENTSR_PTX)
		dev->stats.tx_packets++;
	else 
	{
		dev->stats.tx_errors++;
		if (status & ENTSR_ABT) 
		{
			dev->stats.tx_aborted_errors++;
			dev->stats.collisions += 16;
		}
		if (status & ENTSR_CRS) 
			dev->stats.tx_carrier_errors++;
		if (status & ENTSR_FU) 
			dev->stats.tx_fifo_errors++;
		if (status & ENTSR_CDH)
			dev->stats.tx_heartbeat_errors++;
		if (status & ENTSR_OWC)
			dev->stats.tx_window_errors++;
	}
	netif_wake_queue(dev);
}

/**
 * ei_receive - receive some packets
 * @dev: network device with which receive will be run
 *
 * We have a good packet(s), get it/them out of the buffers. 
 * Called with lock held.
 */

static void ei_receive(struct net_device *dev)
{
	long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned char rxing_page, this_frame, next_frame;
	unsigned short current_offset;
	int rx_pkt_count = 0;
	struct e8390_pkt_hdr rx_frame;
    
	while (++rx_pkt_count < 10) 
	{
		int pkt_len, pkt_stat;
		
		/* Get the rx page (incoming packet pointer). */
		rxing_page = inb_p(e8390_base + EN1_CURPAG -1);
		
		/* Remove one frame from the ring.  Boundary is always a page behind. */
		this_frame = inb_p(e8390_base + EN0_BOUNDARY) + 1;
		if (this_frame >= ei_local->stop_page)
			this_frame = ei_local->rx_start_page;
		
		/* Someday we'll omit the previous, iff we never get this message.
		   (There is at least one clone claimed to have a problem.)  
		   
		   Keep quiet if it looks like a card removal. One problem here
		   is that some clones crash in roughly the same way.
		 */
		if (ei_debug > 0  &&  this_frame != ei_local->current_page && (this_frame!=0x0 || rxing_page!=0xFF))
			printk(KERN_ERR "%s: mismatched read page pointers %2x vs %2x.\n",
				   dev->name, this_frame, ei_local->current_page);
		
		if (this_frame == rxing_page)	/* Read all the frames? */
			break;				/* Done for now */
		
		current_offset = this_frame << 8;
		ei_get_8390_hdr(dev, &rx_frame, this_frame);
		
		pkt_len = rx_frame.count - sizeof(struct e8390_pkt_hdr);
		pkt_stat = rx_frame.status;
		
		next_frame = this_frame + 1 + ((pkt_len+4)>>8);
		
		if (pkt_len < 60  ||  pkt_len > 1518) 
		{
			if (ei_debug)
				printk(KERN_DEBUG "%s: bogus packet size: %d, status=%#2x nxpg=%#2x.\n",
					   dev->name, rx_frame.count, rx_frame.status,
					   rx_frame.next);
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
		}
		 else if ((pkt_stat & 0x0F) == ENRSR_RXOK) 
		{
			struct sk_buff *skb;
			
			skb = dev_alloc_skb(pkt_len+2);
			if (skb == NULL) 
			{
				if (ei_debug > 1)
					printk(KERN_DEBUG "%s: Couldn't allocate a sk_buff of size %d.\n",
						   dev->name, pkt_len);
				dev->stats.rx_dropped++;
				break;
			}
			else
			{
				skb_reserve(skb,2);	/* IP headers on 16 byte boundaries */
				skb_put(skb, pkt_len);	/* Make room */
				ei_block_input(dev, pkt_len, skb, current_offset + sizeof(rx_frame));
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += pkt_len;
				if (pkt_stat & ENRSR_PHY)
					dev->stats.multicast++;
			}
		} 
		else 
		{
			if (ei_debug)
				printk(KERN_DEBUG "%s: bogus packet: status=%#2x nxpg=%#2x size=%d\n",
					   dev->name, rx_frame.status, rx_frame.next,
					   rx_frame.count);
			dev->stats.rx_errors++;
			/* NB: The NIC counts CRC, frame and missed errors. */
			if (pkt_stat & ENRSR_FO)
				dev->stats.rx_fifo_errors++;
		}
		next_frame = rx_frame.next;
		
		/* This _should_ never happen: it's here for avoiding bad clones. */
		if (next_frame >= ei_local->stop_page) {
			printk("%s: next frame inconsistency, %#2x\n", dev->name,
				   next_frame);
			next_frame = ei_local->rx_start_page;
		}
		ei_local->current_page = next_frame;
		outb_p(next_frame-1, e8390_base+EN0_BOUNDARY);
	}

	return;
}

/**
 * ei_rx_overrun - handle receiver overrun
 * @dev: network device which threw exception
 *
 * We have a receiver overrun: we have to kick the 8390 to get it started
 * again. Problem is that you have to kick it exactly as NS prescribes in
 * the updated datasheets, or "the NIC may act in an unpredictable manner."
 * This includes causing "the NIC to defer indefinitely when it is stopped
 * on a busy network."  Ugh.
 * Called with lock held. Don't call this with the interrupts off or your
 * computer will hate you - it takes 10ms or so. 
 */

static void ei_rx_overrun(struct net_device *dev)
{
	axnet_dev_t *info = PRIV(dev);
	long e8390_base = dev->base_addr;
	unsigned char was_txing, must_resend = 0;
    
	/*
	 * Record whether a Tx was in progress and then issue the
	 * stop command.
	 */
	was_txing = inb_p(e8390_base+E8390_CMD) & E8390_TRANS;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);
    
	if (ei_debug > 1)
		printk(KERN_DEBUG "%s: Receiver overrun.\n", dev->name);
	dev->stats.rx_over_errors++;
    
	/* 
	 * Wait a full Tx time (1.2ms) + some guard time, NS says 1.6ms total.
	 * Early datasheets said to poll the reset bit, but now they say that
	 * it "is not a reliable indicator and subsequently should be ignored."
	 * We wait at least 10ms.
	 */

	mdelay(10);

	/*
	 * Reset RBCR[01] back to zero as per magic incantation.
	 */
	outb_p(0x00, e8390_base+EN0_RCNTLO);
	outb_p(0x00, e8390_base+EN0_RCNTHI);

	/*
	 * See if any Tx was interrupted or not. According to NS, this
	 * step is vital, and skipping it will cause no end of havoc.
	 */

	if (was_txing)
	{ 
		unsigned char tx_completed = inb_p(e8390_base+EN0_ISR) & (ENISR_TX+ENISR_TX_ERR);
		if (!tx_completed)
			must_resend = 1;
	}

	/*
	 * Have to enter loopback mode and then restart the NIC before
	 * you are allowed to slurp packets up off the ring.
	 */
	outb_p(E8390_TXOFF, e8390_base + EN0_TXCR);
	outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START, e8390_base + E8390_CMD);

	/*
	 * Clear the Rx ring of all the debris, and ack the interrupt.
	 */
	ei_receive(dev);

	/*
	 * Leave loopback mode, and resend any packet that got stopped.
	 */
	outb_p(E8390_TXCONFIG | info->duplex_flag, e8390_base + EN0_TXCR); 
	if (must_resend)
    		outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START + E8390_TRANS, e8390_base + E8390_CMD);
}

/*
 *	Collect the stats. This is called unlocked and from several contexts.
 */
 
static struct net_device_stats *get_stats(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned long flags;
    
	/* If the card is stopped, just return the present stats. */
	if (!netif_running(dev))
		return &dev->stats;

	spin_lock_irqsave(&ei_local->page_lock,flags);
	/* Read the counter registers, assuming we are in page 0. */
	dev->stats.rx_frame_errors += inb_p(ioaddr + EN0_COUNTER0);
	dev->stats.rx_crc_errors   += inb_p(ioaddr + EN0_COUNTER1);
	dev->stats.rx_missed_errors+= inb_p(ioaddr + EN0_COUNTER2);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);
    
	return &dev->stats;
}

/*
 * Form the 64 bit 8390 multicast table from the linked list of addresses
 * associated with this dev structure.
 */
 
static inline void make_mc_bits(u8 *bits, struct net_device *dev)
{
	struct dev_mc_list *dmi;
	u32 crc;

	netdev_for_each_mc_addr(dmi, dev) {
		crc = ether_crc(ETH_ALEN, dmi->dmi_addr);
		/* 
		 * The 8390 uses the 6 most significant bits of the
		 * CRC to index the multicast table.
		 */
		bits[crc>>29] |= (1<<((crc>>26)&7));
	}
}

/**
 * do_set_multicast_list - set/clear multicast filter
 * @dev: net device for which multicast filter is adjusted
 *
 *	Set or clear the multicast filter for this adaptor.
 *	Must be called with lock held. 
 */
 
static void do_set_multicast_list(struct net_device *dev)
{
	long e8390_base = dev->base_addr;
	int i;
	struct ei_device *ei_local = (struct ei_device*)netdev_priv(dev);

	if (!(dev->flags&(IFF_PROMISC|IFF_ALLMULTI))) {
		memset(ei_local->mcfilter, 0, 8);
		if (!netdev_mc_empty(dev))
			make_mc_bits(ei_local->mcfilter, dev);
	} else {
		/* set to accept-all */
		memset(ei_local->mcfilter, 0xFF, 8);
	}

	outb_p(E8390_NODMA + E8390_PAGE1, e8390_base + E8390_CMD);
	for(i = 0; i < 8; i++) 
	{
		outb_p(ei_local->mcfilter[i], e8390_base + EN1_MULT_SHIFT(i));
	}
	outb_p(E8390_NODMA + E8390_PAGE0, e8390_base + E8390_CMD);

  	if(dev->flags&IFF_PROMISC)
  		outb_p(E8390_RXCONFIG | 0x58, e8390_base + EN0_RXCR);
	else if (dev->flags & IFF_ALLMULTI || !netdev_mc_empty(dev))
  		outb_p(E8390_RXCONFIG | 0x48, e8390_base + EN0_RXCR);
  	else
  		outb_p(E8390_RXCONFIG | 0x40, e8390_base + EN0_RXCR);

	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base+E8390_CMD);
}

/*
 *	Called without lock held. This is invoked from user context and may
 *	be parallel to just about everything else. Its also fairly quick and
 *	not called too often. Must protect against both bh and irq users
 */

static void set_multicast_list(struct net_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev_lock(dev), flags);
	do_set_multicast_list(dev);
	spin_unlock_irqrestore(&dev_lock(dev), flags);
}	

/* This page of functions should be 8390 generic */
/* Follow National Semi's recommendations for initializing the "NIC". */

/**
 * AX88190_init - initialize 8390 hardware
 * @dev: network device to initialize
 * @startp: boolean.  non-zero value to initiate chip processing
 *
 *	Must be called with lock held.
 */

static void AX88190_init(struct net_device *dev, int startp)
{
	axnet_dev_t *info = PRIV(dev);
	long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int i;
	int endcfg = ei_local->word16 ? (0x48 | ENDCFG_WTS) : 0x48;
    
	if(sizeof(struct e8390_pkt_hdr)!=4)
    		panic("8390.c: header struct mispacked\n");    
	/* Follow National Semi's recommendations for initing the DP83902. */
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD); /* 0x21 */
	outb_p(endcfg, e8390_base + EN0_DCFG);	/* 0x48 or 0x49 */
	/* Clear the remote byte count registers. */
	outb_p(0x00,  e8390_base + EN0_RCNTLO);
	outb_p(0x00,  e8390_base + EN0_RCNTHI);
	/* Set to monitor and loopback mode -- this is vital!. */
	outb_p(E8390_RXOFF|0x40, e8390_base + EN0_RXCR); /* 0x60 */
	outb_p(E8390_TXOFF, e8390_base + EN0_TXCR); /* 0x02 */
	/* Set the transmit page and receive ring. */
	outb_p(ei_local->tx_start_page, e8390_base + EN0_TPSR);
	ei_local->tx1 = ei_local->tx2 = 0;
	outb_p(ei_local->rx_start_page, e8390_base + EN0_STARTPG);
	outb_p(ei_local->stop_page-1, e8390_base + EN0_BOUNDARY);	/* 3c503 says 0x3f,NS0x26*/
	ei_local->current_page = ei_local->rx_start_page;		/* assert boundary+1 */
	outb_p(ei_local->stop_page, e8390_base + EN0_STOPPG);
	/* Clear the pending interrupts and mask. */
	outb_p(0xFF, e8390_base + EN0_ISR);
	outb_p(0x00,  e8390_base + EN0_IMR);
    
	/* Copy the station address into the DS8390 registers. */

	outb_p(E8390_NODMA + E8390_PAGE1 + E8390_STOP, e8390_base+E8390_CMD); /* 0x61 */
	for(i = 0; i < 6; i++) 
	{
		outb_p(dev->dev_addr[i], e8390_base + EN1_PHYS_SHIFT(i));
		if(inb_p(e8390_base + EN1_PHYS_SHIFT(i))!=dev->dev_addr[i])
			printk(KERN_ERR "Hw. address read/write mismap %d\n",i);
	}

	outb_p(ei_local->rx_start_page, e8390_base + EN1_CURPAG);
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);

	netif_start_queue(dev);
	ei_local->tx1 = ei_local->tx2 = 0;
	ei_local->txing = 0;

	if (info->flags & IS_AX88790)	/* select Internal PHY */
		outb(0x10, e8390_base + AXNET_GPIO);

	if (startp) 
	{
		outb_p(0xff,  e8390_base + EN0_ISR);
		outb_p(ENISR_ALL,  e8390_base + EN0_IMR);
		outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base+E8390_CMD);
		outb_p(E8390_TXCONFIG | info->duplex_flag,
		       e8390_base + EN0_TXCR); /* xmit on. */
		/* 3c503 TechMan says rxconfig only after the NIC is started. */
		outb_p(E8390_RXCONFIG | 0x40, e8390_base + EN0_RXCR); /* rx on, */
		do_set_multicast_list(dev);	/* (re)load the mcast table */
	}
}

/* Trigger a transmit start, assuming the length is valid. 
   Always called with the page lock held */
   
static void NS8390_trigger_send(struct net_device *dev, unsigned int length,
								int start_page)
{
	long e8390_base = dev->base_addr;
 	struct ei_device *ei_local __attribute((unused)) = (struct ei_device *) netdev_priv(dev);
    
	if (inb_p(e8390_base) & E8390_TRANS) 
	{
		printk(KERN_WARNING "%s: trigger_send() called with the transmitter busy.\n",
			dev->name);
		return;
	}
	outb_p(length & 0xff, e8390_base + EN0_TCNTLO);
	outb_p(length >> 8, e8390_base + EN0_TCNTHI);
	outb_p(start_page, e8390_base + EN0_TPSR);
	outb_p(E8390_NODMA+E8390_TRANS+E8390_START, e8390_base+E8390_CMD);
}

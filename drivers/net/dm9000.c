/*
 *      Davicom DM9000 Fast Ethernet driver for Linux.
 * 	Copyright (C) 1997  Sten Wang
 *
 * 	This program is free software; you can redistribute it and/or
 * 	modify it under the terms of the GNU General Public License
 * 	as published by the Free Software Foundation; either version 2
 * 	of the License, or (at your option) any later version.
 *
 * 	This program is distributed in the hope that it will be useful,
 * 	but WITHOUT ANY WARRANTY; without even the implied warranty of
 * 	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * 	GNU General Public License for more details.
 *
 * (C) Copyright 1997-1998 DAVICOM Semiconductor,Inc. All Rights Reserved.
 *
 * Additional updates, Copyright:
 *	Ben Dooks <ben@simtec.co.uk>
 *	Sascha Hauer <s.hauer@pengutronix.de>
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/dm9000.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/irq.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "dm9000.h"

/* Board/System/Debug information/definition ---------------- */

#define DM9000_PHY		0x40	/* PHY address 0x01 */

#define CARDNAME "dm9000"
#define PFX CARDNAME ": "
#define DRV_VERSION	"1.30"

#ifdef CONFIG_BLACKFIN
#define readsb	insb
#define readsw	insw
#define readsl	insl
#define writesb	outsb
#define writesw	outsw
#define writesl	outsl
#define DEFAULT_TRIGGER IRQF_TRIGGER_HIGH
#else
#define DEFAULT_TRIGGER (0)
#endif

/*
 * Transmit timeout, default 5 seconds.
 */
static int watchdog = 5000;
module_param(watchdog, int, 0400);
MODULE_PARM_DESC(watchdog, "transmit timeout in milliseconds");

/* DM9000 register address locking.
 *
 * The DM9000 uses an address register to control where data written
 * to the data register goes. This means that the address register
 * must be preserved over interrupts or similar calls.
 *
 * During interrupt and other critical calls, a spinlock is used to
 * protect the system, but the calls themselves save the address
 * in the address register in case they are interrupting another
 * access to the device.
 *
 * For general accesses a lock is provided so that calls which are
 * allowed to sleep are serialised so that the address register does
 * not need to be saved. This lock also serves to serialise access
 * to the EEPROM and PHY access registers which are shared between
 * these two devices.
 */

/* Structure/enum declaration ------------------------------- */
typedef struct board_info {

	void __iomem *io_addr;	/* Register I/O base address */
	void __iomem *io_data;	/* Data I/O address */
	u16 irq;		/* IRQ */

	u16 tx_pkt_cnt;
	u16 queue_pkt_len;
	u16 queue_start_addr;
	u16 dbug_cnt;
	u8 io_mode;		/* 0:word, 2:byte */
	u8 phy_addr;
	unsigned int flags;
	unsigned int in_suspend :1;

	int debug_level;

	void (*inblk)(void __iomem *port, void *data, int length);
	void (*outblk)(void __iomem *port, void *data, int length);
	void (*dumpblk)(void __iomem *port, int length);

	struct device	*dev;	     /* parent device */

	struct resource	*addr_res;   /* resources found */
	struct resource *data_res;
	struct resource	*addr_req;   /* resources requested */
	struct resource *data_req;
	struct resource *irq_res;

	struct mutex	 addr_lock;	/* phy and eeprom access lock */

	spinlock_t lock;

	struct mii_if_info mii;
	u32 msg_enable;
} board_info_t;

/* debug code */

#define dm9000_dbg(db, lev, msg...) do {		\
	if ((lev) < CONFIG_DM9000_DEBUGLEVEL &&		\
	    (lev) < db->debug_level) {			\
		dev_dbg(db->dev, msg);			\
	}						\
} while (0)

static inline board_info_t *to_dm9000_board(struct net_device *dev)
{
	return dev->priv;
}

/* function declaration ------------------------------------- */
static int dm9000_probe(struct platform_device *);
static int dm9000_open(struct net_device *);
static int dm9000_start_xmit(struct sk_buff *, struct net_device *);
static int dm9000_stop(struct net_device *);
static int dm9000_ioctl(struct net_device *dev, struct ifreq *req, int cmd);

static void dm9000_init_dm9000(struct net_device *);

static irqreturn_t dm9000_interrupt(int, void *);

static int dm9000_phy_read(struct net_device *dev, int phyaddr_unsused, int reg);
static void dm9000_phy_write(struct net_device *dev, int phyaddr_unused, int reg,
			   int value);

static void dm9000_read_eeprom(board_info_t *, int addr, u8 *to);
static void dm9000_write_eeprom(board_info_t *, int addr, u8 *dp);
static void dm9000_rx(struct net_device *);
static void dm9000_hash_table(struct net_device *);

/* DM9000 network board routine ---------------------------- */

static void
dm9000_reset(board_info_t * db)
{
	dev_dbg(db->dev, "resetting device\n");

	/* RESET device */
	writeb(DM9000_NCR, db->io_addr);
	udelay(200);
	writeb(NCR_RST, db->io_data);
	udelay(200);
}

/*
 *   Read a byte from I/O port
 */
static u8
ior(board_info_t * db, int reg)
{
	writeb(reg, db->io_addr);
	return readb(db->io_data);
}

/*
 *   Write a byte to I/O port
 */

static void
iow(board_info_t * db, int reg, int value)
{
	writeb(reg, db->io_addr);
	writeb(value, db->io_data);
}

/* routines for sending block to chip */

static void dm9000_outblk_8bit(void __iomem *reg, void *data, int count)
{
	writesb(reg, data, count);
}

static void dm9000_outblk_16bit(void __iomem *reg, void *data, int count)
{
	writesw(reg, data, (count+1) >> 1);
}

static void dm9000_outblk_32bit(void __iomem *reg, void *data, int count)
{
	writesl(reg, data, (count+3) >> 2);
}

/* input block from chip to memory */

static void dm9000_inblk_8bit(void __iomem *reg, void *data, int count)
{
	readsb(reg, data, count);
}


static void dm9000_inblk_16bit(void __iomem *reg, void *data, int count)
{
	readsw(reg, data, (count+1) >> 1);
}

static void dm9000_inblk_32bit(void __iomem *reg, void *data, int count)
{
	readsl(reg, data, (count+3) >> 2);
}

/* dump block from chip to null */

static void dm9000_dumpblk_8bit(void __iomem *reg, int count)
{
	int i;
	int tmp;

	for (i = 0; i < count; i++)
		tmp = readb(reg);
}

static void dm9000_dumpblk_16bit(void __iomem *reg, int count)
{
	int i;
	int tmp;

	count = (count + 1) >> 1;

	for (i = 0; i < count; i++)
		tmp = readw(reg);
}

static void dm9000_dumpblk_32bit(void __iomem *reg, int count)
{
	int i;
	int tmp;

	count = (count + 3) >> 2;

	for (i = 0; i < count; i++)
		tmp = readl(reg);
}

/* dm9000_set_io
 *
 * select the specified set of io routines to use with the
 * device
 */

static void dm9000_set_io(struct board_info *db, int byte_width)
{
	/* use the size of the data resource to work out what IO
	 * routines we want to use
	 */

	switch (byte_width) {
	case 1:
		db->dumpblk = dm9000_dumpblk_8bit;
		db->outblk  = dm9000_outblk_8bit;
		db->inblk   = dm9000_inblk_8bit;
		break;


	case 3:
		dev_dbg(db->dev, ": 3 byte IO, falling back to 16bit\n");
	case 2:
		db->dumpblk = dm9000_dumpblk_16bit;
		db->outblk  = dm9000_outblk_16bit;
		db->inblk   = dm9000_inblk_16bit;
		break;

	case 4:
	default:
		db->dumpblk = dm9000_dumpblk_32bit;
		db->outblk  = dm9000_outblk_32bit;
		db->inblk   = dm9000_inblk_32bit;
		break;
	}
}


/* Our watchdog timed out. Called by the networking layer */
static void dm9000_timeout(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;
	u8 reg_save;
	unsigned long flags;

	/* Save previous register address */
	reg_save = readb(db->io_addr);
	spin_lock_irqsave(&db->lock,flags);

	netif_stop_queue(dev);
	dm9000_reset(db);
	dm9000_init_dm9000(dev);
	/* We can accept TX packets again */
	dev->trans_start = jiffies;
	netif_wake_queue(dev);

	/* Restore previous register address */
	writeb(reg_save, db->io_addr);
	spin_unlock_irqrestore(&db->lock,flags);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 *Used by netconsole
 */
static void dm9000_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	dm9000_interrupt(dev->irq,dev);
	enable_irq(dev->irq);
}
#endif

static int dm9000_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	board_info_t *dm = to_dm9000_board(dev);

	if (!netif_running(dev))
		return -EINVAL;

	return generic_mii_ioctl(&dm->mii, if_mii(req), cmd, NULL);
}

/* ethtool ops */

static void dm9000_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	board_info_t *dm = to_dm9000_board(dev);

	strcpy(info->driver, CARDNAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, to_platform_device(dm->dev)->name);
}

static u32 dm9000_get_msglevel(struct net_device *dev)
{
	board_info_t *dm = to_dm9000_board(dev);

	return dm->msg_enable;
}

static void dm9000_set_msglevel(struct net_device *dev, u32 value)
{
	board_info_t *dm = to_dm9000_board(dev);

	dm->msg_enable = value;
}

static int dm9000_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	board_info_t *dm = to_dm9000_board(dev);

	mii_ethtool_gset(&dm->mii, cmd);
	return 0;
}

static int dm9000_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	board_info_t *dm = to_dm9000_board(dev);

	return mii_ethtool_sset(&dm->mii, cmd);
}

static int dm9000_nway_reset(struct net_device *dev)
{
	board_info_t *dm = to_dm9000_board(dev);
	return mii_nway_restart(&dm->mii);
}

static u32 dm9000_get_link(struct net_device *dev)
{
	board_info_t *dm = to_dm9000_board(dev);
	return mii_link_ok(&dm->mii);
}

#define DM_EEPROM_MAGIC		(0x444D394B)

static int dm9000_get_eeprom_len(struct net_device *dev)
{
	return 128;
}

static int dm9000_get_eeprom(struct net_device *dev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	board_info_t *dm = to_dm9000_board(dev);
	int offset = ee->offset;
	int len = ee->len;
	int i;

	/* EEPROM access is aligned to two bytes */

	if ((len & 1) != 0 || (offset & 1) != 0)
		return -EINVAL;

	if (dm->flags & DM9000_PLATF_NO_EEPROM)
		return -ENOENT;

	ee->magic = DM_EEPROM_MAGIC;

	for (i = 0; i < len; i += 2)
		dm9000_read_eeprom(dm, (offset + i) / 2, data + i);

	return 0;
}

static int dm9000_set_eeprom(struct net_device *dev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	board_info_t *dm = to_dm9000_board(dev);
	int offset = ee->offset;
	int len = ee->len;
	int i;

	/* EEPROM access is aligned to two bytes */

	if ((len & 1) != 0 || (offset & 1) != 0)
		return -EINVAL;

	if (dm->flags & DM9000_PLATF_NO_EEPROM)
		return -ENOENT;

	if (ee->magic != DM_EEPROM_MAGIC)
		return -EINVAL;

	for (i = 0; i < len; i += 2)
		dm9000_write_eeprom(dm, (offset + i) / 2, data + i);

	return 0;
}

static const struct ethtool_ops dm9000_ethtool_ops = {
	.get_drvinfo		= dm9000_get_drvinfo,
	.get_settings		= dm9000_get_settings,
	.set_settings		= dm9000_set_settings,
	.get_msglevel		= dm9000_get_msglevel,
	.set_msglevel		= dm9000_set_msglevel,
	.nway_reset		= dm9000_nway_reset,
	.get_link		= dm9000_get_link,
 	.get_eeprom_len		= dm9000_get_eeprom_len,
 	.get_eeprom		= dm9000_get_eeprom,
 	.set_eeprom		= dm9000_set_eeprom,
};


/* dm9000_release_board
 *
 * release a board, and any mapped resources
 */

static void
dm9000_release_board(struct platform_device *pdev, struct board_info *db)
{
	if (db->data_res == NULL) {
		if (db->addr_res != NULL)
			release_mem_region((unsigned long)db->io_addr, 4);
		return;
	}

	/* unmap our resources */

	iounmap(db->io_addr);
	iounmap(db->io_data);

	/* release the resources */

	if (db->data_req != NULL) {
		release_resource(db->data_req);
		kfree(db->data_req);
	}

	if (db->addr_req != NULL) {
		release_resource(db->addr_req);
		kfree(db->addr_req);
	}
}

#define res_size(_r) (((_r)->end - (_r)->start) + 1)

/*
 * Search DM9000 board, allocate space and register it
 */
static int
dm9000_probe(struct platform_device *pdev)
{
	struct dm9000_plat_data *pdata = pdev->dev.platform_data;
	struct board_info *db;	/* Point a board information structure */
	struct net_device *ndev;
	const unsigned char *mac_src;
	unsigned long base;
	int ret = 0;
	int iosize;
	int i;
	u32 id_val;

	/* Init network device */
	ndev = alloc_etherdev(sizeof (struct board_info));
	if (!ndev) {
		dev_err(&pdev->dev, "could not allocate device.\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	dev_dbg(&pdev->dev, "dm9000_probe()");

	/* setup board info structure */
	db = (struct board_info *) ndev->priv;
	memset(db, 0, sizeof (*db));

	db->dev = &pdev->dev;

	spin_lock_init(&db->lock);
	mutex_init(&db->addr_lock);

	if (pdev->num_resources < 2) {
		ret = -ENODEV;
		goto out;
	} else if (pdev->num_resources == 2) {
		base = pdev->resource[0].start;

		if (!request_mem_region(base, 4, ndev->name)) {
			ret = -EBUSY;
			goto out;
		}

		ndev->base_addr = base;
		ndev->irq = pdev->resource[1].start;
		db->io_addr = (void __iomem *)base;
		db->io_data = (void __iomem *)(base + 4);

		/* ensure at least we have a default set of IO routines */
		dm9000_set_io(db, 2);

	} else {
		db->addr_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		db->data_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		db->irq_res  = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

		if (db->addr_res == NULL || db->data_res == NULL ||
		    db->irq_res == NULL) {
			dev_err(db->dev, "insufficient resources\n");
			ret = -ENOENT;
			goto out;
		}

		i = res_size(db->addr_res);
		db->addr_req = request_mem_region(db->addr_res->start, i,
						  pdev->name);

		if (db->addr_req == NULL) {
			dev_err(db->dev, "cannot claim address reg area\n");
			ret = -EIO;
			goto out;
		}

		db->io_addr = ioremap(db->addr_res->start, i);

		if (db->io_addr == NULL) {
			dev_err(db->dev, "failed to ioremap address reg\n");
			ret = -EINVAL;
			goto out;
		}

		iosize = res_size(db->data_res);
		db->data_req = request_mem_region(db->data_res->start, iosize,
						  pdev->name);

		if (db->data_req == NULL) {
			dev_err(db->dev, "cannot claim data reg area\n");
			ret = -EIO;
			goto out;
		}

		db->io_data = ioremap(db->data_res->start, iosize);

		if (db->io_data == NULL) {
			dev_err(db->dev,"failed to ioremap data reg\n");
			ret = -EINVAL;
			goto out;
		}

		/* fill in parameters for net-dev structure */

		ndev->base_addr = (unsigned long)db->io_addr;
		ndev->irq	= db->irq_res->start;

		/* ensure at least we have a default set of IO routines */
		dm9000_set_io(db, iosize);
	}

	/* check to see if anything is being over-ridden */
	if (pdata != NULL) {
		/* check to see if the driver wants to over-ride the
		 * default IO width */

		if (pdata->flags & DM9000_PLATF_8BITONLY)
			dm9000_set_io(db, 1);

		if (pdata->flags & DM9000_PLATF_16BITONLY)
			dm9000_set_io(db, 2);

		if (pdata->flags & DM9000_PLATF_32BITONLY)
			dm9000_set_io(db, 4);

		/* check to see if there are any IO routine
		 * over-rides */

		if (pdata->inblk != NULL)
			db->inblk = pdata->inblk;

		if (pdata->outblk != NULL)
			db->outblk = pdata->outblk;

		if (pdata->dumpblk != NULL)
			db->dumpblk = pdata->dumpblk;

		db->flags = pdata->flags;
	}

	dm9000_reset(db);

	/* try two times, DM9000 sometimes gets the first read wrong */
	for (i = 0; i < 8; i++) {
		id_val  = ior(db, DM9000_VIDL);
		id_val |= (u32)ior(db, DM9000_VIDH) << 8;
		id_val |= (u32)ior(db, DM9000_PIDL) << 16;
		id_val |= (u32)ior(db, DM9000_PIDH) << 24;

		if (id_val == DM9000_ID)
			break;
		dev_err(db->dev, "read wrong id 0x%08x\n", id_val);
	}

	if (id_val != DM9000_ID) {
		dev_err(db->dev, "wrong id: 0x%08x\n", id_val);
		ret = -ENODEV;
		goto out;
	}

	/* from this point we assume that we have found a DM9000 */

	/* driver system function */
	ether_setup(ndev);

	ndev->open		 = &dm9000_open;
	ndev->hard_start_xmit    = &dm9000_start_xmit;
	ndev->tx_timeout         = &dm9000_timeout;
	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
	ndev->stop		 = &dm9000_stop;
	ndev->set_multicast_list = &dm9000_hash_table;
	ndev->ethtool_ops	 = &dm9000_ethtool_ops;
	ndev->do_ioctl		 = &dm9000_ioctl;

#ifdef CONFIG_NET_POLL_CONTROLLER
	ndev->poll_controller	 = &dm9000_poll_controller;
#endif

	db->msg_enable       = NETIF_MSG_LINK;
	db->mii.phy_id_mask  = 0x1f;
	db->mii.reg_num_mask = 0x1f;
	db->mii.force_media  = 0;
	db->mii.full_duplex  = 0;
	db->mii.dev	     = ndev;
	db->mii.mdio_read    = dm9000_phy_read;
	db->mii.mdio_write   = dm9000_phy_write;

	mac_src = "eeprom";

	/* try reading the node address from the attached EEPROM */
	for (i = 0; i < 6; i += 2)
		dm9000_read_eeprom(db, i / 2, ndev->dev_addr+i);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		/* try reading from mac */
		
		mac_src = "chip";
		for (i = 0; i < 6; i++)
			ndev->dev_addr[i] = ior(db, i+DM9000_PAR);
	}

	if (!is_valid_ether_addr(ndev->dev_addr))
		dev_warn(db->dev, "%s: Invalid ethernet MAC address. Please "
			 "set using ifconfig\n", ndev->name);

	platform_set_drvdata(pdev, ndev);
	ret = register_netdev(ndev);

	if (ret == 0) {
		DECLARE_MAC_BUF(mac);
		printk("%s: dm9000 at %p,%p IRQ %d MAC: %s (%s)\n",
		       ndev->name,  db->io_addr, db->io_data, ndev->irq,
		       print_mac(mac, ndev->dev_addr), mac_src);
	}
	return 0;

out:
	dev_err(db->dev, "not found (%d).\n", ret);

	dm9000_release_board(pdev, db);
	free_netdev(ndev);

	return ret;
}

/*
 *  Open the interface.
 *  The interface is opened whenever "ifconfig" actives it.
 */
static int
dm9000_open(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;
	unsigned long irqflags = db->irq_res->flags & IRQF_TRIGGER_MASK;

	if (netif_msg_ifup(db))
		dev_dbg(db->dev, "enabling %s\n", dev->name);

	/* If there is no IRQ type specified, default to something that
	 * may work, and tell the user that this is a problem */

	if (irqflags == IRQF_TRIGGER_NONE) {
		dev_warn(db->dev, "WARNING: no IRQ resource flags set.\n");
		irqflags = DEFAULT_TRIGGER;
	}
	
	irqflags |= IRQF_SHARED;

	if (request_irq(dev->irq, &dm9000_interrupt, irqflags, dev->name, dev))
		return -EAGAIN;

	/* Initialize DM9000 board */
	dm9000_reset(db);
	dm9000_init_dm9000(dev);

	/* Init driver variable */
	db->dbug_cnt = 0;

	mii_check_media(&db->mii, netif_msg_link(db), 1);
	netif_start_queue(dev);

	return 0;
}

/*
 * Initilize dm9000 board
 */
static void
dm9000_init_dm9000(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;

	dm9000_dbg(db, 1, "entering %s\n", __func__);

	/* I/O mode */
	db->io_mode = ior(db, DM9000_ISR) >> 6;	/* ISR bit7:6 keeps I/O mode */

	/* GPIO0 on pre-activate PHY */
	iow(db, DM9000_GPR, 0);	/* REG_1F bit0 activate phyxcer */
	iow(db, DM9000_GPCR, GPCR_GEP_CNTL);	/* Let GPIO0 output */
	iow(db, DM9000_GPR, 0);	/* Enable PHY */

	if (db->flags & DM9000_PLATF_EXT_PHY)
		iow(db, DM9000_NCR, NCR_EXT_PHY);

	/* Program operating register */
	iow(db, DM9000_TCR, 0);	        /* TX Polling clear */
	iow(db, DM9000_BPTR, 0x3f);	/* Less 3Kb, 200us */
	iow(db, DM9000_FCR, 0xff);	/* Flow Control */
	iow(db, DM9000_SMCR, 0);        /* Special Mode */
	/* clear TX status */
	iow(db, DM9000_NSR, NSR_WAKEST | NSR_TX2END | NSR_TX1END);
	iow(db, DM9000_ISR, ISR_CLR_STATUS); /* Clear interrupt status */

	/* Set address filter table */
	dm9000_hash_table(dev);

	/* Enable TX/RX interrupt mask */
	iow(db, DM9000_IMR, IMR_PAR | IMR_PTM | IMR_PRM);

	/* Init Driver variable */
	db->tx_pkt_cnt = 0;
	db->queue_pkt_len = 0;
	dev->trans_start = 0;
}

/*
 *  Hardware start transmission.
 *  Send a packet to media from the upper layer.
 */
static int
dm9000_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	board_info_t *db = (board_info_t *) dev->priv;

	dm9000_dbg(db, 3, "%s:\n", __func__);

	if (db->tx_pkt_cnt > 1)
		return 1;

	spin_lock_irqsave(&db->lock, flags);

	/* Move data to DM9000 TX RAM */
	writeb(DM9000_MWCMD, db->io_addr);

	(db->outblk)(db->io_data, skb->data, skb->len);
	dev->stats.tx_bytes += skb->len;

	db->tx_pkt_cnt++;
	/* TX control: First packet immediately send, second packet queue */
	if (db->tx_pkt_cnt == 1) {
		/* Set TX length to DM9000 */
		iow(db, DM9000_TXPLL, skb->len);
		iow(db, DM9000_TXPLH, skb->len >> 8);

		/* Issue TX polling command */
		iow(db, DM9000_TCR, TCR_TXREQ);	/* Cleared after TX complete */

		dev->trans_start = jiffies;	/* save the time stamp */
	} else {
		/* Second packet */
		db->queue_pkt_len = skb->len;
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&db->lock, flags);

	/* free this SKB */
	dev_kfree_skb(skb);

	return 0;
}

static void
dm9000_shutdown(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;

	/* RESET device */
	dm9000_phy_write(dev, 0, MII_BMCR, BMCR_RESET);	/* PHY RESET */
	iow(db, DM9000_GPR, 0x01);	/* Power-Down PHY */
	iow(db, DM9000_IMR, IMR_PAR);	/* Disable all interrupt */
	iow(db, DM9000_RCR, 0x00);	/* Disable RX */
}

/*
 * Stop the interface.
 * The interface is stopped when it is brought.
 */
static int
dm9000_stop(struct net_device *ndev)
{
	board_info_t *db = (board_info_t *) ndev->priv;

	if (netif_msg_ifdown(db))
		dev_dbg(db->dev, "shutting down %s\n", ndev->name);

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	/* free interrupt */
	free_irq(ndev->irq, ndev);

	dm9000_shutdown(ndev);

	return 0;
}

/*
 * DM9000 interrupt handler
 * receive the packet to upper layer, free the transmitted packet
 */

static void
dm9000_tx_done(struct net_device *dev, board_info_t * db)
{
	int tx_status = ior(db, DM9000_NSR);	/* Got TX status */

	if (tx_status & (NSR_TX2END | NSR_TX1END)) {
		/* One packet sent complete */
		db->tx_pkt_cnt--;
		dev->stats.tx_packets++;

		if (netif_msg_tx_done(db))
			dev_dbg(db->dev, "tx done, NSR %02x\n", tx_status);

		/* Queue packet check & send */
		if (db->tx_pkt_cnt > 0) {
			iow(db, DM9000_TXPLL, db->queue_pkt_len);
			iow(db, DM9000_TXPLH, db->queue_pkt_len >> 8);
			iow(db, DM9000_TCR, TCR_TXREQ);
			dev->trans_start = jiffies;
		}
		netif_wake_queue(dev);
	}
}

static irqreturn_t
dm9000_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	board_info_t *db = (board_info_t *) dev->priv;
	int int_status;
	u8 reg_save;

	dm9000_dbg(db, 3, "entering %s\n", __func__);

	/* A real interrupt coming */

	spin_lock(&db->lock);

	/* Save previous register address */
	reg_save = readb(db->io_addr);

	/* Disable all interrupts */
	iow(db, DM9000_IMR, IMR_PAR);

	/* Got DM9000 interrupt status */
	int_status = ior(db, DM9000_ISR);	/* Got ISR */
	iow(db, DM9000_ISR, int_status);	/* Clear ISR status */

	if (netif_msg_intr(db))
		dev_dbg(db->dev, "interrupt status %02x\n", int_status);

	/* Received the coming packet */
	if (int_status & ISR_PRS)
		dm9000_rx(dev);

	/* Trnasmit Interrupt check */
	if (int_status & ISR_PTS)
		dm9000_tx_done(dev, db);

	/* Re-enable interrupt mask */
	iow(db, DM9000_IMR, IMR_PAR | IMR_PTM | IMR_PRM);

	/* Restore previous register address */
	writeb(reg_save, db->io_addr);

	spin_unlock(&db->lock);

	return IRQ_HANDLED;
}

struct dm9000_rxhdr {
	u8	RxPktReady;
	u8	RxStatus;
	u16	RxLen;
} __attribute__((__packed__));

/*
 *  Received a packet and pass to upper layer
 */
static void
dm9000_rx(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;
	struct dm9000_rxhdr rxhdr;
	struct sk_buff *skb;
	u8 rxbyte, *rdptr;
	bool GoodPacket;
	int RxLen;

	/* Check packet ready or not */
	do {
		ior(db, DM9000_MRCMDX);	/* Dummy read */

		/* Get most updated data */
		rxbyte = readb(db->io_data);

		/* Status check: this byte must be 0 or 1 */
		if (rxbyte > DM9000_PKT_RDY) {
			dev_warn(db->dev, "status check fail: %d\n", rxbyte);
			iow(db, DM9000_RCR, 0x00);	/* Stop Device */
			iow(db, DM9000_ISR, IMR_PAR);	/* Stop INT request */
			return;
		}

		if (rxbyte != DM9000_PKT_RDY)
			return;

		/* A packet ready now  & Get status/length */
		GoodPacket = true;
		writeb(DM9000_MRCMD, db->io_addr);

		(db->inblk)(db->io_data, &rxhdr, sizeof(rxhdr));

		RxLen = le16_to_cpu(rxhdr.RxLen);

		if (netif_msg_rx_status(db))
			dev_dbg(db->dev, "RX: status %02x, length %04x\n",
				rxhdr.RxStatus, RxLen);

		/* Packet Status check */
		if (RxLen < 0x40) {
			GoodPacket = false;
			if (netif_msg_rx_err(db))
				dev_dbg(db->dev, "RX: Bad Packet (runt)\n");
		}

		if (RxLen > DM9000_PKT_MAX) {
			dev_dbg(db->dev, "RST: RX Len:%x\n", RxLen);
		}

		if (rxhdr.RxStatus & 0xbf) {
			GoodPacket = false;
			if (rxhdr.RxStatus & 0x01) {
				if (netif_msg_rx_err(db))
					dev_dbg(db->dev, "fifo error\n");
				dev->stats.rx_fifo_errors++;
			}
			if (rxhdr.RxStatus & 0x02) {
				if (netif_msg_rx_err(db))
					dev_dbg(db->dev, "crc error\n");
				dev->stats.rx_crc_errors++;
			}
			if (rxhdr.RxStatus & 0x80) {
				if (netif_msg_rx_err(db))
					dev_dbg(db->dev, "length error\n");
				dev->stats.rx_length_errors++;
			}
		}

		/* Move data from DM9000 */
		if (GoodPacket
		    && ((skb = dev_alloc_skb(RxLen + 4)) != NULL)) {
			skb_reserve(skb, 2);
			rdptr = (u8 *) skb_put(skb, RxLen - 4);

			/* Read received packet from RX SRAM */

			(db->inblk)(db->io_data, rdptr, RxLen);
			dev->stats.rx_bytes += RxLen;

			/* Pass to upper layer */
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->stats.rx_packets++;

		} else {
			/* need to dump the packet's data */

			(db->dumpblk)(db->io_data, RxLen);
		}
	} while (rxbyte == DM9000_PKT_RDY);
}

static unsigned int
dm9000_read_locked(board_info_t *db, int reg)
{
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&db->lock, flags);
	ret = ior(db, reg);
	spin_unlock_irqrestore(&db->lock, flags);

	return ret;
}

static int dm9000_wait_eeprom(board_info_t *db)
{
	unsigned int status;
	int timeout = 8;	/* wait max 8msec */

	/* The DM9000 data sheets say we should be able to
	 * poll the ERRE bit in EPCR to wait for the EEPROM
	 * operation. From testing several chips, this bit
	 * does not seem to work. 
	 *
	 * We attempt to use the bit, but fall back to the
	 * timeout (which is why we do not return an error
	 * on expiry) to say that the EEPROM operation has
	 * completed.
	 */

	while (1) {
		status = dm9000_read_locked(db, DM9000_EPCR);

		if ((status & EPCR_ERRE) == 0)
			break;

		if (timeout-- < 0) {
			dev_dbg(db->dev, "timeout waiting EEPROM\n");
			break;
		}
	}

	return 0;
}

/*
 *  Read a word data from EEPROM
 */
static void
dm9000_read_eeprom(board_info_t *db, int offset, u8 *to)
{
	unsigned long flags;

	if (db->flags & DM9000_PLATF_NO_EEPROM) {
		to[0] = 0xff;
		to[1] = 0xff;
		return;
	}

	mutex_lock(&db->addr_lock);

	spin_lock_irqsave(&db->lock, flags);

	iow(db, DM9000_EPAR, offset);
	iow(db, DM9000_EPCR, EPCR_ERPRR);

	spin_unlock_irqrestore(&db->lock, flags);

	dm9000_wait_eeprom(db);

	/* delay for at-least 150uS */
	msleep(1);

	spin_lock_irqsave(&db->lock, flags);

	iow(db, DM9000_EPCR, 0x0);

	to[0] = ior(db, DM9000_EPDRL);
	to[1] = ior(db, DM9000_EPDRH);

	spin_unlock_irqrestore(&db->lock, flags);

	mutex_unlock(&db->addr_lock);
}

/*
 * Write a word data to SROM
 */
static void
dm9000_write_eeprom(board_info_t *db, int offset, u8 *data)
{
	unsigned long flags;

	if (db->flags & DM9000_PLATF_NO_EEPROM)
		return;

	mutex_lock(&db->addr_lock);

	spin_lock_irqsave(&db->lock, flags);
	iow(db, DM9000_EPAR, offset);
	iow(db, DM9000_EPDRH, data[1]);
	iow(db, DM9000_EPDRL, data[0]);
	iow(db, DM9000_EPCR, EPCR_WEP | EPCR_ERPRW);
	spin_unlock_irqrestore(&db->lock, flags);

	dm9000_wait_eeprom(db);

	mdelay(1);	/* wait at least 150uS to clear */

	spin_lock_irqsave(&db->lock, flags);
	iow(db, DM9000_EPCR, 0);
	spin_unlock_irqrestore(&db->lock, flags);

	mutex_unlock(&db->addr_lock);
}

/*
 *  Set DM9000 multicast address
 */
static void
dm9000_hash_table(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;
	struct dev_mc_list *mcptr = dev->mc_list;
	int mc_cnt = dev->mc_count;
	int i, oft;
	u32 hash_val;
	u16 hash_table[4];
	u8 rcr = RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN;
	unsigned long flags;

	dm9000_dbg(db, 1, "entering %s\n", __func__);

	spin_lock_irqsave(&db->lock, flags);

	for (i = 0, oft = DM9000_PAR; i < 6; i++, oft++)
		iow(db, oft, dev->dev_addr[i]);

	/* Clear Hash Table */
	for (i = 0; i < 4; i++)
		hash_table[i] = 0x0;

	/* broadcast address */
	hash_table[3] = 0x8000;

	if (dev->flags & IFF_PROMISC)
		rcr |= RCR_PRMSC;

	if (dev->flags & IFF_ALLMULTI)
		rcr |= RCR_ALL;

	/* the multicast address in Hash Table : 64 bits */
	for (i = 0; i < mc_cnt; i++, mcptr = mcptr->next) {
		hash_val = ether_crc_le(6, mcptr->dmi_addr) & 0x3f;
		hash_table[hash_val / 16] |= (u16) 1 << (hash_val % 16);
	}

	/* Write the hash table to MAC MD table */
	for (i = 0, oft = DM9000_MAR; i < 4; i++) {
		iow(db, oft++, hash_table[i]);
		iow(db, oft++, hash_table[i] >> 8);
	}

	iow(db, DM9000_RCR, rcr);
	spin_unlock_irqrestore(&db->lock, flags);
}


/*
 * Sleep, either by using msleep() or if we are suspending, then
 * use mdelay() to sleep.
 */
static void dm9000_msleep(board_info_t *db, unsigned int ms)
{
	if (db->in_suspend)
		mdelay(ms);
	else
		msleep(ms);
}

/*
 *   Read a word from phyxcer
 */
static int
dm9000_phy_read(struct net_device *dev, int phy_reg_unused, int reg)
{
	board_info_t *db = (board_info_t *) dev->priv;
	unsigned long flags;
	unsigned int reg_save;
	int ret;

	mutex_lock(&db->addr_lock);

	spin_lock_irqsave(&db->lock,flags);

	/* Save previous register address */
	reg_save = readb(db->io_addr);

	/* Fill the phyxcer register into REG_0C */
	iow(db, DM9000_EPAR, DM9000_PHY | reg);

	iow(db, DM9000_EPCR, 0xc);	/* Issue phyxcer read command */

	writeb(reg_save, db->io_addr);
	spin_unlock_irqrestore(&db->lock,flags);

	dm9000_msleep(db, 1);		/* Wait read complete */

	spin_lock_irqsave(&db->lock,flags);
	reg_save = readb(db->io_addr);

	iow(db, DM9000_EPCR, 0x0);	/* Clear phyxcer read command */

	/* The read data keeps on REG_0D & REG_0E */
	ret = (ior(db, DM9000_EPDRH) << 8) | ior(db, DM9000_EPDRL);

	/* restore the previous address */
	writeb(reg_save, db->io_addr);
	spin_unlock_irqrestore(&db->lock,flags);

	mutex_unlock(&db->addr_lock);
	return ret;
}

/*
 *   Write a word to phyxcer
 */
static void
dm9000_phy_write(struct net_device *dev, int phyaddr_unused, int reg, int value)
{
	board_info_t *db = (board_info_t *) dev->priv;
	unsigned long flags;
	unsigned long reg_save;

	mutex_lock(&db->addr_lock);

	spin_lock_irqsave(&db->lock,flags);

	/* Save previous register address */
	reg_save = readb(db->io_addr);

	/* Fill the phyxcer register into REG_0C */
	iow(db, DM9000_EPAR, DM9000_PHY | reg);

	/* Fill the written data into REG_0D & REG_0E */
	iow(db, DM9000_EPDRL, value);
	iow(db, DM9000_EPDRH, value >> 8);

	iow(db, DM9000_EPCR, 0xa);	/* Issue phyxcer write command */

	writeb(reg_save, db->io_addr);
	spin_unlock_irqrestore(&db->lock, flags);

	dm9000_msleep(db, 1);		/* Wait write complete */

	spin_lock_irqsave(&db->lock,flags);
	reg_save = readb(db->io_addr);

	iow(db, DM9000_EPCR, 0x0);	/* Clear phyxcer write command */

	/* restore the previous address */
	writeb(reg_save, db->io_addr);

	spin_unlock_irqrestore(&db->lock, flags);
	mutex_unlock(&db->addr_lock);
}

static int
dm9000_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(dev);
	board_info_t *db;

	if (ndev) {
		db = (board_info_t *) ndev->priv;
		db->in_suspend = 1;

		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			dm9000_shutdown(ndev);
		}
	}
	return 0;
}

static int
dm9000_drv_resume(struct platform_device *dev)
{
	struct net_device *ndev = platform_get_drvdata(dev);
	board_info_t *db = (board_info_t *) ndev->priv;

	if (ndev) {

		if (netif_running(ndev)) {
			dm9000_reset(db);
			dm9000_init_dm9000(ndev);

			netif_device_attach(ndev);
		}

		db->in_suspend = 0;
	}
	return 0;
}

static int
dm9000_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	unregister_netdev(ndev);
	dm9000_release_board(pdev, (board_info_t *) ndev->priv);
	free_netdev(ndev);		/* free device structure */

	dev_dbg(&pdev->dev, "released and freed device\n");
	return 0;
}

static struct platform_driver dm9000_driver = {
	.driver	= {
		.name    = "dm9000",
		.owner	 = THIS_MODULE,
	},
	.probe   = dm9000_probe,
	.remove  = dm9000_drv_remove,
	.suspend = dm9000_drv_suspend,
	.resume  = dm9000_drv_resume,
};

static int __init
dm9000_init(void)
{
	printk(KERN_INFO "%s Ethernet Driver, V%s\n", CARDNAME, DRV_VERSION);

	return platform_driver_register(&dm9000_driver);	/* search board and register */
}

static void __exit
dm9000_cleanup(void)
{
	platform_driver_unregister(&dm9000_driver);
}

module_init(dm9000_init);
module_exit(dm9000_cleanup);

MODULE_AUTHOR("Sascha Hauer, Ben Dooks");
MODULE_DESCRIPTION("Davicom DM9000 network driver");
MODULE_LICENSE("GPL");

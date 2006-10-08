/*
 *   dm9000.c: Version 1.2 03/18/2003
 *
 *         A Davicom DM9000 ISA NIC fast Ethernet driver for Linux.
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
 *   (C)Copyright 1997-1998 DAVICOM Semiconductor,Inc. All Rights Reserved.
 *
 * V0.11	06/20/2001	REG_0A bit3=1, default enable BP with DA match
 * 	06/22/2001 	Support DM9801 progrmming
 * 	 	 	E3: R25 = ((R24 + NF) & 0x00ff) | 0xf000
 * 		 	E4: R25 = ((R24 + NF) & 0x00ff) | 0xc200
 * 		     		R17 = (R17 & 0xfff0) | NF + 3
 * 		 	E5: R25 = ((R24 + NF - 3) & 0x00ff) | 0xc200
 * 		     		R17 = (R17 & 0xfff0) | NF
 *
 * v1.00               	modify by simon 2001.9.5
 *                         change for kernel 2.4.x
 *
 * v1.1   11/09/2001      	fix force mode bug
 *
 * v1.2   03/18/2003       Weilun Huang <weilun_huang@davicom.com.tw>:
 * 			Fixed phy reset.
 * 			Added tx/rx 32 bit mode.
 * 			Cleaned up for kernel merge.
 *
 *        03/03/2004    Sascha Hauer <s.hauer@pengutronix.de>
 *                      Port to 2.6 kernel
 *
 *	  24-Sep-2004   Ben Dooks <ben@simtec.co.uk>
 *			Cleanup of code to remove ifdefs
 *			Allowed platform device data to influence access width
 *			Reformatting areas of code
 *
 *        17-Mar-2005   Sascha Hauer <s.hauer@pengutronix.de>
 *                      * removed 2.4 style module parameters
 *                      * removed removed unused stat counter and fixed
 *                        net_device_stats
 *                      * introduced tx_timeout function
 *                      * reworked locking
 *
 *	  01-Jul-2005   Ben Dooks <ben@simtec.co.uk>
 *			* fixed spinlock call without pointer
 *			* ensure spinlock is initialised
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
#include <linux/dm9000.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "dm9000.h"

/* Board/System/Debug information/definition ---------------- */

#define DM9000_PHY		0x40	/* PHY address 0x01 */

#define TRUE			1
#define FALSE			0

#define CARDNAME "dm9000"
#define PFX CARDNAME ": "

#define DM9000_TIMER_WUT  jiffies+(HZ*2)	/* timer wakeup time : 2 second */

#define DM9000_DEBUG 0

#if DM9000_DEBUG > 2
#define PRINTK3(args...)  printk(CARDNAME ": " args)
#else
#define PRINTK3(args...)  do { } while(0)
#endif

#if DM9000_DEBUG > 1
#define PRINTK2(args...)  printk(CARDNAME ": " args)
#else
#define PRINTK2(args...)  do { } while(0)
#endif

#if DM9000_DEBUG > 0
#define PRINTK1(args...)  printk(CARDNAME ": " args)
#define PRINTK(args...)   printk(CARDNAME ": " args)
#else
#define PRINTK1(args...)  do { } while(0)
#define PRINTK(args...)   printk(KERN_DEBUG args)
#endif

/*
 * Transmit timeout, default 5 seconds.
 */
static int watchdog = 5000;
module_param(watchdog, int, 0400);
MODULE_PARM_DESC(watchdog, "transmit timeout in milliseconds");

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

	void (*inblk)(void __iomem *port, void *data, int length);
	void (*outblk)(void __iomem *port, void *data, int length);
	void (*dumpblk)(void __iomem *port, int length);

	struct resource	*addr_res;   /* resources found */
	struct resource *data_res;
	struct resource	*addr_req;   /* resources requested */
	struct resource *data_req;
	struct resource *irq_res;

	struct timer_list timer;
	struct net_device_stats stats;
	unsigned char srom[128];
	spinlock_t lock;

	struct mii_if_info mii;
	u32 msg_enable;
} board_info_t;

/* function declaration ------------------------------------- */
static int dm9000_probe(struct platform_device *);
static int dm9000_open(struct net_device *);
static int dm9000_start_xmit(struct sk_buff *, struct net_device *);
static int dm9000_stop(struct net_device *);


static void dm9000_timer(unsigned long);
static void dm9000_init_dm9000(struct net_device *);

static struct net_device_stats *dm9000_get_stats(struct net_device *);

static irqreturn_t dm9000_interrupt(int, void *);

static int dm9000_phy_read(struct net_device *dev, int phyaddr_unsused, int reg);
static void dm9000_phy_write(struct net_device *dev, int phyaddr_unused, int reg,
			   int value);
static u16 read_srom_word(board_info_t *, int);
static void dm9000_rx(struct net_device *);
static void dm9000_hash_table(struct net_device *);

//#define DM9000_PROGRAM_EEPROM
#ifdef DM9000_PROGRAM_EEPROM
static void program_eeprom(board_info_t * db);
#endif
/* DM9000 network board routine ---------------------------- */

static void
dm9000_reset(board_info_t * db)
{
	PRINTK1("dm9000x: resetting\n");
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

	case 2:
		db->dumpblk = dm9000_dumpblk_16bit;
		db->outblk  = dm9000_outblk_16bit;
		db->inblk   = dm9000_inblk_16bit;
		break;

	case 3:
		printk(KERN_ERR PFX ": 3 byte IO, falling back to 16bit\n");
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
	unsigned long base;
	int ret = 0;
	int iosize;
	int i;
	u32 id_val;

	/* Init network device */
	ndev = alloc_etherdev(sizeof (struct board_info));
	if (!ndev) {
		printk("%s: could not allocate device.\n", CARDNAME);
		return -ENOMEM;
	}

	SET_MODULE_OWNER(ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	PRINTK2("dm9000_probe()");

	/* setup board info structure */
	db = (struct board_info *) ndev->priv;
	memset(db, 0, sizeof (*db));

	spin_lock_init(&db->lock);

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

	} else {
		db->addr_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		db->data_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		db->irq_res  = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

		if (db->addr_res == NULL || db->data_res == NULL ||
		    db->irq_res == NULL) {
			printk(KERN_ERR PFX "insufficient resources\n");
			ret = -ENOENT;
			goto out;
		}

		i = res_size(db->addr_res);
		db->addr_req = request_mem_region(db->addr_res->start, i,
						  pdev->name);

		if (db->addr_req == NULL) {
			printk(KERN_ERR PFX "cannot claim address reg area\n");
			ret = -EIO;
			goto out;
		}

		db->io_addr = ioremap(db->addr_res->start, i);

		if (db->io_addr == NULL) {
			printk(KERN_ERR "failed to ioremap address reg\n");
			ret = -EINVAL;
			goto out;
		}

		iosize = res_size(db->data_res);
		db->data_req = request_mem_region(db->data_res->start, iosize,
						  pdev->name);

		if (db->data_req == NULL) {
			printk(KERN_ERR PFX "cannot claim data reg area\n");
			ret = -EIO;
			goto out;
		}

		db->io_data = ioremap(db->data_res->start, iosize);

		if (db->io_data == NULL) {
			printk(KERN_ERR "failed to ioremap data reg\n");
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
	}

	dm9000_reset(db);

	/* try two times, DM9000 sometimes gets the first read wrong */
	for (i = 0; i < 2; i++) {
		id_val  = ior(db, DM9000_VIDL);
		id_val |= (u32)ior(db, DM9000_VIDH) << 8;
		id_val |= (u32)ior(db, DM9000_PIDL) << 16;
		id_val |= (u32)ior(db, DM9000_PIDH) << 24;

		if (id_val == DM9000_ID)
			break;
		printk("%s: read wrong id 0x%08x\n", CARDNAME, id_val);
	}

	if (id_val != DM9000_ID) {
		printk("%s: wrong id: 0x%08x\n", CARDNAME, id_val);
		goto release;
	}

	/* from this point we assume that we have found a DM9000 */

	/* driver system function */
	ether_setup(ndev);

	ndev->open		 = &dm9000_open;
	ndev->hard_start_xmit    = &dm9000_start_xmit;
	ndev->tx_timeout         = &dm9000_timeout;
	ndev->watchdog_timeo = msecs_to_jiffies(watchdog);
	ndev->stop		 = &dm9000_stop;
	ndev->get_stats		 = &dm9000_get_stats;
	ndev->set_multicast_list = &dm9000_hash_table;
#ifdef CONFIG_NET_POLL_CONTROLLER
	ndev->poll_controller	 = &dm9000_poll_controller;
#endif

#ifdef DM9000_PROGRAM_EEPROM
	program_eeprom(db);
#endif
	db->msg_enable       = NETIF_MSG_LINK;
	db->mii.phy_id_mask  = 0x1f;
	db->mii.reg_num_mask = 0x1f;
	db->mii.force_media  = 0;
	db->mii.full_duplex  = 0;
	db->mii.dev	     = ndev;
	db->mii.mdio_read    = dm9000_phy_read;
	db->mii.mdio_write   = dm9000_phy_write;

	/* Read SROM content */
	for (i = 0; i < 64; i++)
		((u16 *) db->srom)[i] = read_srom_word(db, i);

	/* Set Node Address */
	for (i = 0; i < 6; i++)
		ndev->dev_addr[i] = db->srom[i];

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		/* try reading from mac */

		for (i = 0; i < 6; i++)
			ndev->dev_addr[i] = ior(db, i+DM9000_PAR);
	}

	if (!is_valid_ether_addr(ndev->dev_addr))
		printk("%s: Invalid ethernet MAC address.  Please "
		       "set using ifconfig\n", ndev->name);

	platform_set_drvdata(pdev, ndev);
	ret = register_netdev(ndev);

	if (ret == 0) {
		printk("%s: dm9000 at %p,%p IRQ %d MAC: ",
		       ndev->name,  db->io_addr, db->io_data, ndev->irq);
		for (i = 0; i < 5; i++)
			printk("%02x:", ndev->dev_addr[i]);
		printk("%02x\n", ndev->dev_addr[5]);
	}
	return 0;

 release:
 out:
	printk("%s: not found (%d).\n", CARDNAME, ret);

	dm9000_release_board(pdev, db);
	kfree(ndev);

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

	PRINTK2("entering dm9000_open\n");

	if (request_irq(dev->irq, &dm9000_interrupt, IRQF_SHARED, dev->name, dev))
		return -EAGAIN;

	/* Initialize DM9000 board */
	dm9000_reset(db);
	dm9000_init_dm9000(dev);

	/* Init driver variable */
	db->dbug_cnt = 0;

	/* set and active a timer process */
	init_timer(&db->timer);
	db->timer.expires  = DM9000_TIMER_WUT;
	db->timer.data     = (unsigned long) dev;
	db->timer.function = &dm9000_timer;
	add_timer(&db->timer);

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

	PRINTK1("entering %s\n",__FUNCTION__);

	/* I/O mode */
	db->io_mode = ior(db, DM9000_ISR) >> 6;	/* ISR bit7:6 keeps I/O mode */

	/* GPIO0 on pre-activate PHY */
	iow(db, DM9000_GPR, 0);	/* REG_1F bit0 activate phyxcer */
	iow(db, DM9000_GPCR, GPCR_GEP_CNTL);	/* Let GPIO0 output */
	iow(db, DM9000_GPR, 0);	/* Enable PHY */

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

	/* Activate DM9000 */
	iow(db, DM9000_RCR, RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN);
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
	board_info_t *db = (board_info_t *) dev->priv;

	PRINTK3("dm9000_start_xmit\n");

	if (db->tx_pkt_cnt > 1)
		return 1;

	netif_stop_queue(dev);

	/* Disable all interrupts */
	iow(db, DM9000_IMR, IMR_PAR);

	/* Move data to DM9000 TX RAM */
	writeb(DM9000_MWCMD, db->io_addr);

	(db->outblk)(db->io_data, skb->data, skb->len);
	db->stats.tx_bytes += skb->len;

	/* TX control: First packet immediately send, second packet queue */
	if (db->tx_pkt_cnt == 0) {

		/* First Packet */
		db->tx_pkt_cnt++;

		/* Set TX length to DM9000 */
		iow(db, DM9000_TXPLL, skb->len & 0xff);
		iow(db, DM9000_TXPLH, (skb->len >> 8) & 0xff);

		/* Issue TX polling command */
		iow(db, DM9000_TCR, TCR_TXREQ);	/* Cleared after TX complete */

		dev->trans_start = jiffies;	/* save the time stamp */

	} else {
		/* Second packet */
		db->tx_pkt_cnt++;
		db->queue_pkt_len = skb->len;
	}

	/* free this SKB */
	dev_kfree_skb(skb);

	/* Re-enable resource check */
	if (db->tx_pkt_cnt == 1)
		netif_wake_queue(dev);

	/* Re-enable interrupt */
	iow(db, DM9000_IMR, IMR_PAR | IMR_PTM | IMR_PRM);

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

	PRINTK1("entering %s\n",__FUNCTION__);

	/* deleted timer */
	del_timer(&db->timer);

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
		db->stats.tx_packets++;

		/* Queue packet check & send */
		if (db->tx_pkt_cnt > 0) {
			iow(db, DM9000_TXPLL, db->queue_pkt_len & 0xff);
			iow(db, DM9000_TXPLH, (db->queue_pkt_len >> 8) & 0xff);
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
	board_info_t *db;
	int int_status;
	u8 reg_save;

	PRINTK3("entering %s\n",__FUNCTION__);

	if (!dev) {
		PRINTK1("dm9000_interrupt() without DEVICE arg\n");
		return IRQ_HANDLED;
	}

	/* A real interrupt coming */
	db = (board_info_t *) dev->priv;
	spin_lock(&db->lock);

	/* Save previous register address */
	reg_save = readb(db->io_addr);

	/* Disable all interrupts */
	iow(db, DM9000_IMR, IMR_PAR);

	/* Got DM9000 interrupt status */
	int_status = ior(db, DM9000_ISR);	/* Got ISR */
	iow(db, DM9000_ISR, int_status);	/* Clear ISR status */

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

/*
 *  Get statistics from driver.
 */
static struct net_device_stats *
dm9000_get_stats(struct net_device *dev)
{
	board_info_t *db = (board_info_t *) dev->priv;
	return &db->stats;
}


/*
 *  A periodic timer routine
 *  Dynamic media sense, allocated Rx buffer...
 */
static void
dm9000_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	board_info_t *db = (board_info_t *) dev->priv;

	PRINTK3("dm9000_timer()\n");

	mii_check_media(&db->mii, netif_msg_link(db), 0);

	/* Set timer again */
	db->timer.expires = DM9000_TIMER_WUT;
	add_timer(&db->timer);
}

struct dm9000_rxhdr {
	u16	RxStatus;
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
	int GoodPacket;
	int RxLen;

	/* Check packet ready or not */
	do {
		ior(db, DM9000_MRCMDX);	/* Dummy read */

		/* Get most updated data */
		rxbyte = readb(db->io_data);

		/* Status check: this byte must be 0 or 1 */
		if (rxbyte > DM9000_PKT_RDY) {
			printk("status check failed: %d\n", rxbyte);
			iow(db, DM9000_RCR, 0x00);	/* Stop Device */
			iow(db, DM9000_ISR, IMR_PAR);	/* Stop INT request */
			return;
		}

		if (rxbyte != DM9000_PKT_RDY)
			return;

		/* A packet ready now  & Get status/length */
		GoodPacket = TRUE;
		writeb(DM9000_MRCMD, db->io_addr);

		(db->inblk)(db->io_data, &rxhdr, sizeof(rxhdr));

		RxLen = rxhdr.RxLen;

		/* Packet Status check */
		if (RxLen < 0x40) {
			GoodPacket = FALSE;
			PRINTK1("Bad Packet received (runt)\n");
		}

		if (RxLen > DM9000_PKT_MAX) {
			PRINTK1("RST: RX Len:%x\n", RxLen);
		}

		if (rxhdr.RxStatus & 0xbf00) {
			GoodPacket = FALSE;
			if (rxhdr.RxStatus & 0x100) {
				PRINTK1("fifo error\n");
				db->stats.rx_fifo_errors++;
			}
			if (rxhdr.RxStatus & 0x200) {
				PRINTK1("crc error\n");
				db->stats.rx_crc_errors++;
			}
			if (rxhdr.RxStatus & 0x8000) {
				PRINTK1("length error\n");
				db->stats.rx_length_errors++;
			}
		}

		/* Move data from DM9000 */
		if (GoodPacket
		    && ((skb = dev_alloc_skb(RxLen + 4)) != NULL)) {
			skb->dev = dev;
			skb_reserve(skb, 2);
			rdptr = (u8 *) skb_put(skb, RxLen - 4);

			/* Read received packet from RX SRAM */

			(db->inblk)(db->io_data, rdptr, RxLen);
			db->stats.rx_bytes += RxLen;

			/* Pass to upper layer */
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			db->stats.rx_packets++;

		} else {
			/* need to dump the packet's data */

			(db->dumpblk)(db->io_data, RxLen);
		}
	} while (rxbyte == DM9000_PKT_RDY);
}

/*
 *  Read a word data from SROM
 */
static u16
read_srom_word(board_info_t * db, int offset)
{
	iow(db, DM9000_EPAR, offset);
	iow(db, DM9000_EPCR, EPCR_ERPRR);
	mdelay(8);		/* according to the datasheet 200us should be enough,
				   but it doesn't work */
	iow(db, DM9000_EPCR, 0x0);
	return (ior(db, DM9000_EPDRL) + (ior(db, DM9000_EPDRH) << 8));
}

#ifdef DM9000_PROGRAM_EEPROM
/*
 * Write a word data to SROM
 */
static void
write_srom_word(board_info_t * db, int offset, u16 val)
{
	iow(db, DM9000_EPAR, offset);
	iow(db, DM9000_EPDRH, ((val >> 8) & 0xff));
	iow(db, DM9000_EPDRL, (val & 0xff));
	iow(db, DM9000_EPCR, EPCR_WEP | EPCR_ERPRW);
	mdelay(8);		/* same shit */
	iow(db, DM9000_EPCR, 0);
}

/*
 * Only for development:
 * Here we write static data to the eeprom in case
 * we don't have valid content on a new board
 */
static void
program_eeprom(board_info_t * db)
{
	u16 eeprom[] = { 0x0c00, 0x007f, 0x1300,	/* MAC Address */
		0x0000,		/* Autoload: accept nothing */
		0x0a46, 0x9000,	/* Vendor / Product ID */
		0x0000,		/* pin control */
		0x0000,
	};			/* Wake-up mode control */
	int i;
	for (i = 0; i < 8; i++)
		write_srom_word(db, i, eeprom[i]);
}
#endif


/*
 *  Calculate the CRC valude of the Rx packet
 *  flag = 1 : return the reverse CRC (for the received packet CRC)
 *         0 : return the normal CRC (for Hash Table index)
 */

static unsigned long
cal_CRC(unsigned char *Data, unsigned int Len, u8 flag)
{

       u32 crc = ether_crc_le(Len, Data);

       if (flag)
               return ~crc;

       return crc;
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
	u32 hash_val;
	u16 i, oft, hash_table[4];
	unsigned long flags;

	PRINTK2("dm9000_hash_table()\n");

	spin_lock_irqsave(&db->lock,flags);

	for (i = 0, oft = 0x10; i < 6; i++, oft++)
		iow(db, oft, dev->dev_addr[i]);

	/* Clear Hash Table */
	for (i = 0; i < 4; i++)
		hash_table[i] = 0x0;

	/* broadcast address */
	hash_table[3] = 0x8000;

	/* the multicast address in Hash Table : 64 bits */
	for (i = 0; i < mc_cnt; i++, mcptr = mcptr->next) {
		hash_val = cal_CRC((char *) mcptr->dmi_addr, 6, 0) & 0x3f;
		hash_table[hash_val / 16] |= (u16) 1 << (hash_val % 16);
	}

	/* Write the hash table to MAC MD table */
	for (i = 0, oft = 0x16; i < 4; i++) {
		iow(db, oft++, hash_table[i] & 0xff);
		iow(db, oft++, (hash_table[i] >> 8) & 0xff);
	}

	spin_unlock_irqrestore(&db->lock,flags);
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

	spin_lock_irqsave(&db->lock,flags);

	/* Save previous register address */
	reg_save = readb(db->io_addr);

	/* Fill the phyxcer register into REG_0C */
	iow(db, DM9000_EPAR, DM9000_PHY | reg);

	iow(db, DM9000_EPCR, 0xc);	/* Issue phyxcer read command */
	udelay(100);		/* Wait read complete */
	iow(db, DM9000_EPCR, 0x0);	/* Clear phyxcer read command */

	/* The read data keeps on REG_0D & REG_0E */
	ret = (ior(db, DM9000_EPDRH) << 8) | ior(db, DM9000_EPDRL);

	/* restore the previous address */
	writeb(reg_save, db->io_addr);

	spin_unlock_irqrestore(&db->lock,flags);

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

	spin_lock_irqsave(&db->lock,flags);

	/* Save previous register address */
	reg_save = readb(db->io_addr);

	/* Fill the phyxcer register into REG_0C */
	iow(db, DM9000_EPAR, DM9000_PHY | reg);

	/* Fill the written data into REG_0D & REG_0E */
	iow(db, DM9000_EPDRL, (value & 0xff));
	iow(db, DM9000_EPDRH, ((value >> 8) & 0xff));

	iow(db, DM9000_EPCR, 0xa);	/* Issue phyxcer write command */
	udelay(500);		/* Wait write complete */
	iow(db, DM9000_EPCR, 0x0);	/* Clear phyxcer write command */

	/* restore the previous address */
	writeb(reg_save, db->io_addr);

	spin_unlock_irqrestore(&db->lock,flags);
}

static int
dm9000_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(dev);

	if (ndev) {
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
	kfree(ndev);		/* free device structure */

	PRINTK1("clean_module() exit\n");

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
	printk(KERN_INFO "%s Ethernet Driver\n", CARDNAME);

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

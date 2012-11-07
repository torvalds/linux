/*
 *      Davicom WEMAC Fast Ethernet driver for Linux.
 *      Copyright (C) 1997  Sten Wang
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version 2
 *      of the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
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
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/irq.h>

#include <mach/dma.h>
#include <mach/sys_config.h>
#include <mach/clock.h>

#include "sun4i_wemac.h"

/* Board/System/Debug information/definition ---------------- */

#define WEMAC_PHY	0x100 /* PHY address 0x01 */
#define CARDNAME	"wemac"
#define DRV_VERSION	"1.01"
#define DMA_CPU_TRRESHOLD 2000
#define TOLOWER(x) ((x) | 0x20)
/*
 * Transmit timeout, default 5 seconds.
 */
static int watchdog = 5000;
static unsigned char mac_addr[6] = {0x00};
static char *mac_addr_param = ":";
module_param(watchdog, int, 0400);
MODULE_PARM_DESC(watchdog, "transmit timeout in milliseconds");


/* WEMAC register address locking.
 *
 * The WEMAC uses an address register to control where data written
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

/* The driver supports the original WEMACE, and now the two newer
 * devices, WEMACA and WEMACB.
 */


/* Structure/enum declaration ------------------------------- */
typedef struct wemac_board_info {

	void __iomem	*emac_vbase;	/* mac I/O base address */
	void __iomem	*sram_vbase;	/* sram control I/O base address */
	void __iomem	*gpio_vbase;	/* gpio I/O base address */
	void __iomem	*ccmu_vbase;	/* ccmu I/O base address */
	u16		 irq;		/* IRQ */

	u16		tx_fifo_stat;
	u16		dbug_cnt;
	u8		io_mode;		/* 0:word, 2:byte */
	u8		phy_addr;
	u8		imr_all;

	unsigned int	flags;
	unsigned int	in_suspend:1;
	int		debug_level;

	void (*inblk)(void __iomem *port, void *data, int length);
	void (*outblk)(void __iomem *port, void *data, int length);
	void (*dumpblk)(void __iomem *port, int length);

	struct device	*dev;	     /* parent device */

	struct resource	*emac_base_res;   /* resources found */
	struct resource	*sram_base_res;   /* resources found */
	struct resource	*gpio_base_res;   /* resources found */
	struct resource	*ccmu_base_res;   /* resources found */

	struct resource	*emac_base_req;   /* resources found */
	struct resource	*sram_base_req;   /* resources found */
	struct resource	*gpio_base_req;   /* resources found */
	struct resource	*ccmu_base_req;   /* resources found */

	struct resource *irq_res;

	struct mutex	 addr_lock;	/* phy and eeprom access lock */

	struct delayed_work phy_poll;
	struct net_device  *ndev;

	spinlock_t	lock;

	struct mii_if_info mii;
	u32		msg_enable;
	user_gpio_set_t *mos_gpio;
	u32 mos_pin_handler;
} wemac_board_info_t;

/* debug code */
#define CONFIG_WEMAC_DEBUGLEVEL 00
#define wemac_dbg(db, lev, msg...) do {		\
	if ((lev) < CONFIG_WEMAC_DEBUGLEVEL &&		\
			(lev) < db->debug_level) {			\
		dev_dbg(db->dev, msg);			\
	}						\
} while (0)

static inline wemac_board_info_t *to_wemac_board(struct net_device *dev)
{
	return netdev_priv(dev);
}

static int  wemac_phy_read(struct net_device *dev, int phyaddr_unused, int reg);
static void wemac_phy_write(struct net_device *dev, int phyaddr_unused, int reg, int value);
static void wemac_rx(struct net_device *dev);
static void read_random_macaddr(unsigned char *mac, struct net_device *ndev);

struct sw_dma_client emacrx_dma_client = {
	.name = "EMACRX_DMA",
};

struct sw_dma_client emactx_dma_client = {
	.name = "EMACTX_DMA",
};

static int ch_rx, ch_tx;
static int emacrx_dma_completed_flag = 1;
static int emactx_dma_completed_flag = 1;
static int emacrx_completed_flag = 1;


void emacrx_dma_buffdone(struct sw_dma_chan *ch, void *buf, int size, enum sw_dma_buffresult result)
{
	struct net_device *dev = ch->dev_id;
	wemac_rx(dev);
}

int emacrx_dma_opfn(struct sw_dma_chan *ch, enum sw_chan_op op_code)
{
	if (op_code == SW_DMAOP_START)
		emacrx_dma_completed_flag = 0;
	return 0;
}

void emactx_dma_buffdone(struct sw_dma_chan *ch, void *buf,
			int size, enum sw_dma_buffresult result)
{
	emactx_dma_completed_flag = 1;
}

int  emactx_dma_opfn(struct sw_dma_chan *ch, enum sw_chan_op op_code)
{
	if (op_code == SW_DMAOP_START)
		emactx_dma_completed_flag = 0;
	return 0;
}

#if 0
__hdle emac_RequestDMA(__u32 dmatype)
{
	__hdle ch;

	ch = sw_dma_request(dmatype, &nand_dma_client, NULL);
	if (ch < 0)
		return ch;

	sw_dma_set_opfn(ch, nanddma_opfn);
	sw_dma_set_buffdone_fn(ch, nanddma_buffdone);

	return ch;
}
#endif

void eLIBs_CleanFlushDCacheRegion(void *adr, __u32 bytes)
{
	__cpuc_flush_dcache_area(adr, bytes + (1 << 5) * 2 - 2);
}

__s32 emacrx_DMAEqueueBuf(int hDma,  void *buff_addr, __u32 len)
{
	static int seq_rx;
	eLIBs_CleanFlushDCacheRegion((void *)buff_addr, len);

	emacrx_dma_completed_flag = 0;
	return sw_dma_enqueue(hDma, (void *)(seq_rx++), (dma_addr_t)buff_addr, len);
}

__s32 emactx_DMAEqueueBuf(int hDma,  void *buff_addr, __u32 len)
{
	static int seq_tx;
	eLIBs_CleanFlushDCacheRegion(buff_addr, len);

	emactx_dma_completed_flag = 0;
	return sw_dma_enqueue(hDma, (void *)(seq_tx++), (dma_addr_t)buff_addr, len);
}

int wemac_dma_config_start(__u8 rw, void *buff_addr, __u32 len)
{
	int ret;
	if (rw == 0) {
		struct dma_hw_conf emac_hwconf = {
			.xfer_type = DMAXFER_D_SWORD_S_SWORD,
			.hf_irq = SW_DMA_IRQ_FULL,
			.cmbk = 0x03030303,
			.dir = SW_DMA_RDEV,
			.from = 0x01C0B04C,
			.address_type = DMAADDRT_D_LN_S_IO,
			.drqsrc_type = DRQ_TYPE_EMAC
		};

		ret = sw_dma_setflags(ch_rx, SW_DMAF_AUTOSTART);
		if (ret != 0)
			return ret;
		ret = sw_dma_config(ch_rx, &emac_hwconf);
		if (ret != 0)
			return ret;
		ret = emacrx_DMAEqueueBuf(ch_rx, buff_addr, len);
		if (ret != 0)
			return ret;
		ret = sw_dma_ctrl(ch_rx, SW_DMAOP_START);
	} else {
		struct dma_hw_conf emac_hwconf = {
			.xfer_type = DMAXFER_D_SWORD_S_SWORD,
			.hf_irq = SW_DMA_IRQ_FULL,
			.cmbk = 0x03030303,
			.dir = SW_DMA_WDEV,
			.to = 0x01C0B024,
			.address_type = DMAADDRT_D_IO_S_LN,
			.drqdst_type = DRQ_TYPE_EMAC
		};

		ret = sw_dma_setflags(ch_tx, SW_DMAF_AUTOSTART);
		if (ret != 0)
			return ret;
		ret = sw_dma_config(ch_tx, &emac_hwconf);
		if (ret != 0)
			return ret;
		ret = emactx_DMAEqueueBuf(ch_tx, buff_addr, len);
		if (ret != 0)
			return ret;
		ret = sw_dma_ctrl(ch_tx, SW_DMAOP_START);
	}
	return 0;
}

__s32 emacrx_WaitDmaFinish(void)
{
	unsigned long flags;

	while (1) {
		local_irq_save(flags);
		if (emacrx_dma_completed_flag) {
			local_irq_restore(flags);
			break;
		}
		local_irq_restore(flags);
	}

	return 0;
}

__s32 emactx_WaitDmaFinish(void)
{
	while (1) {
		poll_dma_pending(ch_tx);
		if (emactx_dma_completed_flag)
			break;
	}

	return 0;
}

/* WEMAC network board routine ---------------------------- */

static void
wemac_reset(wemac_board_info_t *db)
{
	dev_dbg(db->dev, "resetting device\n");

	/* RESET device */
	writel(0, db->emac_vbase + EMAC_CTL_REG);
	udelay(200);
	writel(1, db->emac_vbase + EMAC_CTL_REG);
	udelay(200);
}

#if 0
static void wemac_outblk_dma(void __iomem *reg, void *data, int count)
{
	wemac_dma_config_start(1, data, count);
	emactx_WaitDmaFinish();
}
#else
static int wemac_inblk_dma(void __iomem *reg, void *data, int count)
{
	return wemac_dma_config_start(0, data, count);
}
#endif

static void wemac_outblk_32bit(void __iomem *reg, void *data, int count)
{
	writesl(reg, data, (count+3) >> 2);
}

static void wemac_inblk_32bit(void __iomem *reg, void *data, int count)
{
	readsl(reg, data, (count+3) >> 2);
}

static void wemac_dumpblk_32bit(void __iomem *reg, int count)
{
	int i;
	int tmp;

	count = (count + 3) >> 2;

	for (i = 0; i < count; i++)
		tmp = readl(reg);
}

static void wemac_schedule_poll(wemac_board_info_t *db)
{
	schedule_delayed_work(&db->phy_poll, HZ * 2);
}

static int wemac_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	wemac_board_info_t *dm = to_wemac_board(dev);

	if (!netif_running(dev))
		return -EINVAL;

	return generic_mii_ioctl(&dm->mii, if_mii(req), cmd, NULL);
}


/* ethtool ops */
static void wemac_get_drvinfo(struct net_device *dev,
		struct ethtool_drvinfo *info)
{
	wemac_board_info_t *dm = to_wemac_board(dev);

	strcpy(info->driver, CARDNAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, to_platform_device(dm->dev)->name);
}

static u32 wemac_get_msglevel(struct net_device *dev)
{
	wemac_board_info_t *dm = to_wemac_board(dev);

	return dm->msg_enable;
}

static void wemac_set_msglevel(struct net_device *dev, u32 value)
{
	wemac_board_info_t *dm = to_wemac_board(dev);

	dm->msg_enable = value;
}

static int wemac_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	wemac_board_info_t *dm = to_wemac_board(dev);

	mii_ethtool_gset(&dm->mii, cmd);
	return 0;
}

static int wemac_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	wemac_board_info_t *dm = to_wemac_board(dev);

	return mii_ethtool_sset(&dm->mii, cmd);
}

static int wemac_nway_reset(struct net_device *dev)
{
	wemac_board_info_t *dm = to_wemac_board(dev);
	return mii_nway_restart(&dm->mii);
}

static u32 wemac_get_link(struct net_device *dev)
{
	wemac_board_info_t *dm = to_wemac_board(dev);
	u32 ret;

	if (dm->flags & WEMAC_PLATF_EXT_PHY)
		ret = mii_link_ok(&dm->mii);
	else
		ret = wemac_phy_read(dev, 0, 1)  & 0x04 ? 1 : 0;

	return ret;
}

static int wemac_get_eeprom_len(struct net_device *dev)
{
	return 128;
}

static int wemac_get_eeprom(struct net_device *dev,
		struct ethtool_eeprom *ee, u8 *data)
{
#if 0
	wemac_board_info_t *dm = to_wemac_board(dev);
	int offset = ee->offset;
	int len = ee->len;
	int i;

	/* EEPROM access is aligned to two bytes */

	if ((len & 1) != 0 || (offset & 1) != 0)
		return -EINVAL;

	if (dm->flags & WEMAC_PLATF_NO_EEPROM)
		return -ENOENT;

	ee->magic = EMAC_EEPROM_MAGIC;

	for (i = 0; i < len; i += 2)
		wemac_read_eeprom(dm, (offset + i) / 2, data + i);
#endif

	return 0;
}

static int wemac_set_eeprom(struct net_device *dev,
		struct ethtool_eeprom *ee, u8 *data)
{
#if 0
	wemac_board_info_t *dm = to_wemac_board(dev);
	int offset = ee->offset;
	int len = ee->len;
	int i;

	/* EEPROM access is aligned to two bytes */

	if ((len & 1) != 0 || (offset & 1) != 0)
		return -EINVAL;

	if (dm->flags & WEMAC_PLATF_NO_EEPROM)
		return -ENOENT;

	if (ee->magic != EMAC_EEPROM_MAGIC)
		return -EINVAL;

	for (i = 0; i < len; i += 2)
		wemac_write_eeprom(dm, (offset + i) / 2, data + i);
#endif

	return 0;
}

static const struct ethtool_ops wemac_ethtool_ops = {
	.get_drvinfo		= wemac_get_drvinfo,
	.get_settings		= wemac_get_settings,
	.set_settings		= wemac_set_settings,
	.get_msglevel		= wemac_get_msglevel,
	.set_msglevel		= wemac_set_msglevel,
	.nway_reset		= wemac_nway_reset,
	.get_link		= wemac_get_link,
	.get_eeprom_len		= wemac_get_eeprom_len,
	.get_eeprom		= wemac_get_eeprom,
	.set_eeprom		= wemac_set_eeprom,
};

/*
 * ****************************************************************************
 *	void phy_link_check()
 *  Description:
 *
 *
 *	Return Value:	1: Link valid		0: Link not valid
 * ****************************************************************************
 */
unsigned int phy_link_check(struct net_device *dev)
{
	unsigned int reg_val;

	reg_val = wemac_phy_read(dev, 0, 1);

	if (reg_val & 0x4) {
		printk(KERN_INFO "EMAC PHY Linked...\n");
		return 1;
	} else {
		printk(KERN_INFO "EMAC PHY Link waiting......\n");
		return 0;
	}
}

void emac_sys_setup(wemac_board_info_t *db)
{
	struct clk *tmpClk;
	unsigned int reg_val;

	/*  map SRAM to EMAC  */
	reg_val = readl(db->sram_vbase + SRAMC_CFG_REG);
	reg_val |= 0x5<<2;
	writel(reg_val, db->sram_vbase + SRAMC_CFG_REG);

#ifdef EMAC_MAP1

	/*  PA0~PA7  */
	reg_val = readl(db->gpio_vbase + PA_CFG0_REG);
	reg_val &= 0xAAAAAAAA;
	reg_val |= 0x22222222;
	writel(reg_val, db->gpio_vbase + PA_CFG0_REG);

	/*  PA8~PA15  */
	reg_val = readl(db->gpio_vbase + PA_CFG1_REG);
	reg_val &= 0xAAAAAAAA;
	reg_val |= 0x22222222;
	writel(reg_val, db->gpio_vbase + PA_CFG1_REG);

	/*  PA16~PA17  */
	reg_val = readl(db->gpio_vbase + PA_CFG2_REG);
	reg_val &= 0xffffffAA;
	reg_val |= 0x00000022;
	writel(reg_val, db->gpio_vbase + PA_CFG2_REG);
#endif

	/*  set up clock gating  */
	tmpClk = clk_get(NULL, "ahb_emac");
	clk_enable(tmpClk);
	printk(KERN_INFO "[EMAC] ahb clk enable\n");
	printk(KERN_INFO "[EMAC] ahb gate clk: 0x%x\n", *((__u32 *)0xf1c20060));

#if 0
	reg_val = readl(db->ccmu_vbase + CCM_AHB_GATING_REG);
	printk(KERN_INFO "db->ccmu_vbase: %x, ccmu ahb gate: 0x%x\n", db->ccmu_vbase + CCM_AHB_GATING_REG, reg_val);
	reg_val |= 0x1<<17; /*EMAC*/
	writel(reg_val, db->ccmu_vbase + CCM_AHB_GATING_REG);
	reg_val = readl(db->ccmu_vbase + CCM_AHB_GATING_REG);
	printk(KERN_INFO "db->ccmu_vbase: %x, ccmu ahb gate: 0x%x\n", db->ccmu_vbase + CCM_AHB_GATING_REG, reg_val);
#endif
}

unsigned int emac_setup(struct net_device *ndev)
{
	unsigned int reg_val;
	unsigned int phy_val;
	unsigned int duplex_flag;
	wemac_board_info_t *db = netdev_priv(ndev);

	wemac_dbg(db, 3, "EMAC seting ==>\n"
		"PHY_AUTO_NEGOTIOATION  %x  0: Normal        1: Auto\n"
		"PHY_SPEED              %x  0: 10M           1: 100M\n"
		"EMAC_MAC_FULL          %x  0: Half duplex   1: Full duplex\n"
		"EMAC_TX_TM             %x  0: CPU           1: DMA\n"
		"EMAC_TX_AB_M           %x  0: Disable       1: Aborted frame enable\n"
		"EMAC_RX_TM             %x  0: CPU           1: DMA\n"
		"EMAC_RX_DRQ_MODE       %x  0: DRQ asserted  1: DRQ automatically\n",
		PHY_AUTO_NEGOTIOATION,
		PHY_SPEED,
		EMAC_MAC_FULL,
		EMAC_TX_TM,
		EMAC_TX_AB_M,
		EMAC_RX_TM,
		EMAC_RX_DRQ_MODE);

	/* set up TX */
	reg_val = readl(db->emac_vbase + EMAC_TX_MODE_REG);

	if (EMAC_TX_AB_M)
		reg_val |= 0x1;
	else
		reg_val &= (~0x1);

	if (EMAC_TX_TM)
		reg_val |= (0x1<<1);
	else
		reg_val &= (~(0x1<<1));

	writel(reg_val, db->emac_vbase + EMAC_TX_MODE_REG);

	/* set up RX */
	reg_val = readl(db->emac_vbase + EMAC_RX_CTL_REG);

	if (EMAC_RX_DRQ_MODE)
		reg_val |= (0x1<<1);
	else
		reg_val &= (~(0x1<<1));

	if (EMAC_RX_TM)
		reg_val |= (0x1<<2);
	else
		reg_val &= (~(0x1<<2));

	if (EMAC_RX_PA)
		reg_val |= (0x1<<4);
	else
		reg_val &= (~(0x1<<4));

	if (EMAC_RX_PCF)
		reg_val |= (0x1<<5);
	else
		reg_val &= (~(0x1<<5));

	if (EMAC_RX_PCRCE)
		reg_val |= (0x1<<6);
	else
		reg_val &= (~(0x1<<6));

	if (EMAC_RX_PLE)
		reg_val |= (0x1<<7);
	else
		reg_val &= (~(0x1<<7));

	if (EMAC_RX_POR)
		reg_val |= (0x1<<8);
	else
		reg_val &= (~(0x1<<8));

	if (EMAC_RX_UCAD)
		reg_val |= (0x1<<16);
	else
		reg_val &= (~(0x1<<16));

	if (EMAC_RX_DAF)
		reg_val |= (0x1<<17);
	else
		reg_val &= (~(0x1<<17));

	if (EMAC_RX_MCO)
		reg_val |= (0x1<<20);
	else
		reg_val &= (~(0x1<<20));

	if (EMAC_RX_MHF)
		reg_val |= (0x1<<21);
	else
		reg_val &= (~(0x1<<21));

	if (EMAC_RX_BCO)
		reg_val |= (0x1<<22);
	else
		reg_val &= (~(0x1<<22));

	if (EMAC_RX_SAF)
		reg_val |= (0x1<<24);
	else
		reg_val &= (~(0x1<<24));

	if (EMAC_RX_SAIF)
		reg_val |= (0x1<<25);
	else
		reg_val &= (~(0x1<<25));

	writel(reg_val, db->emac_vbase + EMAC_RX_CTL_REG);

	/* set MAC */
	/* set MAC CTL0 */
	reg_val = readl(db->emac_vbase + EMAC_MAC_CTL0_REG);

	if (EMAC_MAC_TFC)
		reg_val |= (0x1<<3);
	else
		reg_val &= (~(0x1<<3));

	if (EMAC_MAC_RFC)
		reg_val |= (0x1<<2);
	else
		reg_val &= (~(0x1<<2));

	writel(reg_val, db->emac_vbase + EMAC_MAC_CTL0_REG);

	/* set MAC CTL1 */
	reg_val = readl(db->emac_vbase + EMAC_MAC_CTL1_REG);

	/* phy setup */
	if (!PHY_AUTO_NEGOTIOATION) {
		phy_val = wemac_phy_read(ndev, 0, 0);
		dev_dbg(db->dev, "PHY reg 0 value: %x\n", phy_val);

		phy_val = (PHY_SPEED<<13)|(EMAC_MAC_FULL<<8) ;
		dev_dbg(db->dev, "PHY SETUP, write reg 0 with value: %x\n", phy_val);
		wemac_phy_write(ndev, 0, 0, phy_val);

		/* soft reset phy */
		phy_val = wemac_phy_read(ndev, 0, 0);
		phy_val |= 0x1<<15;
		wemac_phy_write(ndev, 0, 0, phy_val);

		phy_val = wemac_phy_read(ndev, 0, 0);
		dev_dbg(db->dev, "PHY reg 0 value: %x\n", phy_val);

		mdelay(10);
		phy_val = (PHY_SPEED<<13)|(EMAC_MAC_FULL<<8) ;
		dev_dbg(db->dev, "PHY SETUP, write reg 0 with value: %x\n", phy_val);
		wemac_phy_write(ndev, 0, 0, phy_val);
	}
	mdelay(10);
	phy_val = wemac_phy_read(ndev, 0, 0);
	dev_dbg(db->dev, "PHY SETUP, reg 0 value: %x\n", phy_val);
	duplex_flag = !!(phy_val & (1<<8));

	if (PHY_AUTO_NEGOTIOATION) {
		if (duplex_flag)
			reg_val |= (0x1<<0);
		else
			reg_val &= (~(0x1<<0));
	} else {
		if (EMAC_MAC_FULL)
			reg_val |= (0x1<<0);
		else
			reg_val &= (~(0x1<<0));
	}

	if (EMAC_MAC_FLC)
		reg_val |= (0x1<<1);
	else
		reg_val &= (~(0x1<<1));

	if (EMAC_MAC_HF)
		reg_val |= (0x1<<2);
	else
		reg_val &= (~(0x1<<2));

	if (EMAC_MAC_DCRC)
		reg_val |= (0x1<<3);
	else
		reg_val &= (~(0x1<<3));

	if (EMAC_MAC_CRC)
		reg_val |= (0x1<<4);
	else
		reg_val &= (~(0x1<<4));

	if (EMAC_MAC_PC)
		reg_val |= (0x1<<5);
	else
		reg_val &= (~(0x1<<5));

	if (EMAC_MAC_VC)
		reg_val |= (0x1<<6);
	else
		reg_val &= (~(0x1<<6));

	if (EMAC_MAC_ADP)
		reg_val |= (0x1<<7);
	else
		reg_val &= (~(0x1<<7));

	if (EMAC_MAC_PRE)
		reg_val |= (0x1<<8);
	else
		reg_val &= (~(0x1<<8));

	if (EMAC_MAC_LPE)
		reg_val |= (0x1<<9);
	else
		reg_val &= (~(0x1<<9));

	if (EMAC_MAC_NB)
		reg_val |= (0x1<<12);
	else
		reg_val &= (~(0x1<<12));

	if (EMAC_MAC_BNB)
		reg_val |= (0x1<<13);
	else
		reg_val &= (~(0x1<<13));

	if (EMAC_MAC_ED)
		reg_val |= (0x1<<14);
	else
		reg_val &= (~(0x1<<14));

	writel(reg_val, db->emac_vbase + EMAC_MAC_CTL1_REG);

	/* set up IPGT */
	reg_val = EMAC_MAC_IPGT;
	writel(reg_val, db->emac_vbase + EMAC_MAC_IPGT_REG);

	/* set up IPGR */
	reg_val = EMAC_MAC_NBTB_IPG2;
	reg_val |= (EMAC_MAC_NBTB_IPG1<<8);
	writel(reg_val, db->emac_vbase + EMAC_MAC_IPGR_REG);

	/* set up Collison window */
	reg_val = EMAC_MAC_RM;
	reg_val |= (EMAC_MAC_CW<<8);
	writel(reg_val, db->emac_vbase + EMAC_MAC_CLRT_REG);

	/* set up Max Frame Length */
	reg_val = EMAC_MAC_MFL;
	writel(reg_val, db->emac_vbase + EMAC_MAC_MAXF_REG);

	return 1;
}

unsigned int wemac_powerup(struct net_device *ndev)
{
	wemac_board_info_t *db = netdev_priv(ndev);
	char emac_mac[13] = {'\0'};
	int i;
	unsigned int reg_val;

	/* initial EMAC */
	/* flush RX FIFO */
	reg_val = readl(db->emac_vbase + EMAC_RX_CTL_REG); /* RX FIFO */
	reg_val |= 0x8;
	writel(reg_val, db->emac_vbase + EMAC_RX_CTL_REG);
	udelay(1);

	/* initial MAC */
	reg_val = readl(db->emac_vbase + EMAC_MAC_CTL0_REG); /* soft reset MAC */
	reg_val &= (~(0x1<<15));
	writel(reg_val, db->emac_vbase + EMAC_MAC_CTL0_REG);

	reg_val = readl(db->emac_vbase + EMAC_MAC_MCFG_REG); /* set MII clock */
	reg_val &= (~(0xf<<2));
	reg_val |= (0xD<<2);
	writel(reg_val, db->emac_vbase + EMAC_MAC_MCFG_REG);

	/* clear RX counter */
	writel(0x0, db->emac_vbase + EMAC_RX_FBC_REG);

	/* disable all interrupt and clear interrupt status */
	writel(0, db->emac_vbase + EMAC_INT_CTL_REG);
	reg_val = readl(db->emac_vbase + EMAC_INT_STA_REG);
	writel(reg_val, db->emac_vbase + EMAC_INT_STA_REG);

	udelay(1);

	/* set up EMAC */
	emac_setup(ndev);

	/* set mac_address to chip */
	if (strlen(mac_addr_param) == 17) {
		int i;
		char *p = mac_addr_param;
		printk(KERN_INFO "MAC address: %s\n", mac_addr_param);

		for (i = 0; i < 6; i++, p++)
			mac_addr[i] = simple_strtoul(p, &p, 16);
	} else if (SCRIPT_PARSER_OK != script_parser_fetch("dynamic", "MAC", (int *)emac_mac, 3)) {
		printk(KERN_WARNING "emac MAC isn't valid!\n");
	} else {
		emac_mac[12] = '\0';
		for (i = 0; i < 6; i++) {
			char emac_tmp[3] = ":::";
			memcpy(emac_tmp, (char *)(emac_mac+i*2), 2);
			emac_tmp[2] = ':';
			mac_addr[i] = simple_strtoul(emac_tmp, NULL, 16);
		}
	}
	writel(mac_addr[0]<<16 | mac_addr[1]<<8 | mac_addr[2],
			db->emac_vbase + EMAC_MAC_A1_REG);
	writel(mac_addr[3]<<16 | mac_addr[4]<<8 | mac_addr[5],
			db->emac_vbase + EMAC_MAC_A0_REG);

	mdelay(1);

	return 1;
}

#if 0
static void wemac_show_carrier(wemac_board_info_t *db,
				unsigned carrier, unsigned nsr)
{
	struct net_device *ndev = db->ndev;
	unsigned ncr = wemac_read_locked(db, WEMAC_NCR);

	if (carrier)
		dev_info(db->dev, "%s: link up, %dMbps, %s-duplex, no LPA\n",
			 ndev->name, (nsr & NSR_SPEED) ? 10 : 100,
			 (ncr & NCR_FDX) ? "full" : "half");
	else
		dev_info(db->dev, "%s: link down\n", ndev->name);
}
#endif

static void
wemac_poll_work(struct work_struct *w)
{
	struct delayed_work *dw = container_of(w, struct delayed_work, work);
	wemac_board_info_t *db = container_of(dw, wemac_board_info_t, phy_poll);
	struct net_device *ndev = db->ndev;

#if 0
	if (db->flags & WEMAC_PLATF_SIMPLE_PHY &&
	    !(db->flags & WEMAC_PLATF_EXT_PHY)) {
		unsigned nsr = wemac_read_locked(db, WEMAC_NSR);
		unsigned old_carrier = netif_carrier_ok(ndev) ? 1 : 0;
		unsigned new_carrier;

		new_carrier = (nsr & NSR_LINKST) ? 1 : 0;

		if (old_carrier != new_carrier) {
			if (netif_msg_link(db))
				wemac_show_carrier(db, new_carrier, nsr);

			if (!new_carrier)
				netif_carrier_off(ndev);
			else
				netif_carrier_on(ndev);
		}
	} else
#endif

	mii_check_media(&db->mii, netif_msg_link(db), 0);

	if (netif_running(ndev))
		wemac_schedule_poll(db);
}

/* wemac_release_board
 *
 * release a board, and any mapped resources
 */
static void
wemac_release_board(struct platform_device *pdev, struct wemac_board_info *db)
{
	/* unmap our resources */

	iounmap(db->emac_vbase);
	iounmap(db->sram_vbase);
	iounmap(db->gpio_vbase);
	iounmap(db->ccmu_vbase);

	/* release the resources */
	release_resource(db->emac_base_req);
	kfree(db->emac_base_req);
	release_resource(db->sram_base_req);
	kfree(db->sram_base_req);
	release_resource(db->gpio_base_req);
	kfree(db->gpio_base_req);
	release_resource(db->ccmu_base_req);
	kfree(db->ccmu_base_req);
}

/*
 *  Set WEMAC multicast address
 */
static void
wemac_hash_table(struct net_device *dev)
{
#if 0
	wemac_board_info_t *db = netdev_priv(dev);
	struct dev_mc_list *mcptr = dev->mc_list;
	int mc_cnt = dev->mc_count;
	int i, oft;
	u32 hash_val;
	u16 hash_table[4];
	u8 rcr = RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN;
	unsigned long flags;

	wemac_dbg(db, 1, "entering %s\n", __func__);

	spin_lock_irqsave(&db->lock, flags);

	for (i = 0, oft = WEMAC_PAR; i < 6; i++, oft++)
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
	for (i = 0, oft = WEMAC_MAR; i < 4; i++) {
		iow(db, oft++, hash_table[i]);
		iow(db, oft++, hash_table[i] >> 8);
	}

	iow(db, WEMAC_RCR, rcr);
	spin_unlock_irqrestore(&db->lock, flags);
#endif
}

static void read_random_macaddr(unsigned char *mac, struct net_device *ndev)
{
	unsigned char *buf = mac;
	wemac_board_info_t *db = netdev_priv(ndev);

	get_random_bytes(buf, 6);

	buf[0] &= 0xfe;		/*  the 48bit must set 0  */
	buf[0] |= 0x02;		/*  the 47bit must set 1  */

	/*  we write the random number into chip  */
	writel(buf[0]<<16 | buf[1]<<8 | buf[2],
			db->emac_vbase + EMAC_MAC_A1_REG);
	writel(buf[3]<<16 | buf[4]<<8 | buf[5],
			db->emac_vbase + EMAC_MAC_A0_REG);

}

static int wemac_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	wemac_board_info_t *db = netdev_priv(dev);

	if (netif_running(dev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data)) {
		dev_err(&dev->dev, "not setting invalid mac address %pM\n",
			addr->sa_data);
		return -EADDRNOTAVAIL;
	}

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);

	writel(dev->dev_addr[0]<<16 | dev->dev_addr[1]<<8 | dev->dev_addr[2],
			db->emac_vbase + EMAC_MAC_A1_REG);
	writel(dev->dev_addr[3]<<16 | dev->dev_addr[4]<<8 | dev->dev_addr[5],
			db->emac_vbase + EMAC_MAC_A0_REG);

	return 0;
}

/*
 * Initilize wemac board
 */
static void
wemac_init_wemac(struct net_device *dev)
{
	wemac_board_info_t *db = netdev_priv(dev);
	unsigned int phy_reg;
	unsigned int reg_val;

	if (db->mos_pin_handler) {
		db->mos_gpio->data = 1;
		gpio_set_one_pin_status(db->mos_pin_handler, db->mos_gpio, "emac_power", 1);
	}

	/* PHY POWER UP */
	phy_reg = wemac_phy_read(dev, 0, 0);
	wemac_phy_write(dev, 0, 0, phy_reg & (~(1<<11)));
	mdelay(1);
	/*phy_link_check();*/

	phy_reg = wemac_phy_read(dev, 0, 0);

	/* set EMAC SPEED, depend on PHY  */
	reg_val = readl(db->emac_vbase + EMAC_MAC_SUPP_REG);
	reg_val &= (~(0x1<<8));
	/*reg_val |= ((phy_reg & (1<<13)) << 8);*/
	reg_val |= (((phy_reg & (1<<13))>>13) << 8);
	writel(reg_val, db->emac_vbase + EMAC_MAC_SUPP_REG);

	/* set duplex depend on phy*/
	reg_val = readl(db->emac_vbase + EMAC_MAC_CTL1_REG);
	reg_val &= (~(0x1<<0));
	/*reg_val |= ((phy_reg & (1<<8)) << 0);*/
	reg_val |= (((phy_reg & (1<<8))>>8) << 0);
	writel(reg_val, db->emac_vbase + EMAC_MAC_CTL1_REG);

	/* Set address filter table */
	wemac_hash_table(dev);

	/* enable RX/TX */
	reg_val = readl(db->emac_vbase + EMAC_CTL_REG);
	reg_val |= 0x7;
	writel(reg_val, db->emac_vbase + EMAC_CTL_REG);

	/* enable RX/TX0/RX Hlevel interrup */
	reg_val = readl(db->emac_vbase + EMAC_INT_CTL_REG);
	/*reg_val |= (0x1<<0) | (0x01<<8)| (0x1<<17);*/
	reg_val |= (0xf<<0) | (0x01<<8);
	writel(reg_val, db->emac_vbase + EMAC_INT_CTL_REG);

	/* Init Driver variable */
	db->tx_fifo_stat = 0;
	dev->trans_start = 0;
}

/* Our watchdog timed out. Called by the networking layer */
static void wemac_timeout(struct net_device *dev)
{
	wemac_board_info_t *db = netdev_priv(dev);
	unsigned long flags;

	if (netif_msg_timer(db))
		dev_err(db->dev, "tx time out.\n");

	/* Save previous register address */
	spin_lock_irqsave(&db->lock, flags);

	netif_stop_queue(dev);
	wemac_reset(db);
	wemac_init_wemac(dev);
	/* We can accept TX packets again */
	dev->trans_start = jiffies;
	netif_wake_queue(dev);

	/* Restore previous register address */
	spin_unlock_irqrestore(&db->lock, flags);
}

#define PINGPANG_BUF 1
/*
 *  Hardware start transmission.
 *  Send a packet to media from the upper layer.
 */
static int
wemac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long channal;
	unsigned long flags;
	wemac_board_info_t *db = netdev_priv(dev);

#if PINGPANG_BUF
	if ((channal = (db->tx_fifo_stat & 3)) == 3)
		return 1;

	channal = (channal == 1 ? 1 : 0);
#else
	if (db->tx_fifo_stat > 0)
		return 1;

	channal = 0;
#endif

	spin_lock_irqsave(&db->lock, flags);

	writel(channal, db->emac_vbase + EMAC_TX_INS_REG);

	(db->outblk)(db->emac_vbase + EMAC_TX_IO_DATA_REG, skb->data, skb->len);
	dev->stats.tx_bytes += skb->len;

	db->tx_fifo_stat |= 1 << channal;
	/* TX control: First packet immediately send, second packet queue */
	if (channal == 0) {
		/* set TX len */
		writel(skb->len, db->emac_vbase + EMAC_TX_PL0_REG);
		/* start translate from fifo to phy */
		writel(readl(db->emac_vbase + EMAC_TX_CTL0_REG) | 1, db->emac_vbase + EMAC_TX_CTL0_REG);

		dev->trans_start = jiffies;	/* save the time stamp */
	} else if (channal == 1) {
		/* set TX len */
		writel(skb->len, db->emac_vbase + EMAC_TX_PL1_REG);
		/* start translate from fifo to phy */
		writel(readl(db->emac_vbase + EMAC_TX_CTL1_REG) | 1, db->emac_vbase + EMAC_TX_CTL1_REG);

		dev->trans_start = jiffies;	/* save the time stamp */
	}

	if ((db->tx_fifo_stat & 3) == 3) {
		/* Second packet */
		netif_stop_queue(dev);
	}


	spin_unlock_irqrestore(&db->lock, flags);

	/* free this SKB */
	dev_kfree_skb(skb);

	return 0;
}

/*
 * WEMAC interrupt handler
 * receive the packet to upper layer, free the transmitted packet
 */

static void wemac_tx_done(struct net_device *dev, wemac_board_info_t *db, unsigned int tx_status)
{
	/* One packet sent complete */
	db->tx_fifo_stat &= ~(tx_status & 3);
	if (3 == (tx_status & 3))
		dev->stats.tx_packets += 2;
	else
		dev->stats.tx_packets++;

	if (netif_msg_tx_done(db))
		dev_dbg(db->dev, "tx done, NSR %02x\n", tx_status);

#if 0
	/* Queue packet check & send */
	if (db->tx_fifo_stat > 0) {
		/* set TX len */
		writel(db->queue_pkt_len, db->emac_vbase + EMAC_TX_PL0_REG);
		/* start translate from fifo to mac */
		writel(readl(db->emac_vbase + EMAC_TX_CTL0_REG) | 1, db->emac_vbase + EMAC_TX_CTL0_REG);
		dev->trans_start = jiffies;
	}
#endif
	netif_wake_queue(dev);
}

struct wemac_rxhdr {
	__s16	RxLen;
	__u16	RxStatus;
} __attribute__((__packed__));

char dbg_dump_buf[0x4000];
#define DBG_LAST_MAX 6
/*
 *  Received a packet and pass to upper layer
 */
static void
wemac_rx(struct net_device *dev)
{
	wemac_board_info_t *db = netdev_priv(dev);
	struct wemac_rxhdr rxhdr;
	struct sk_buff *skb;
	static struct sk_buff *skb_last; /*todo: change static variate to member of wemac_board_info_t. bingge */
	u8 *rdptr;
	bool GoodPacket;
	int RxLen;
	static int RxLen_last;
	unsigned int RxStatus;
	unsigned int reg_val, Rxcount, ret;

	/* Check packet ready or not */
	while (1) {
		/* race warning: the first packet might arrive with
		   the interrupts disabled, but the second will fix
		   it */
		Rxcount = readl(db->emac_vbase + EMAC_RX_FBC_REG);

		if (netif_msg_rx_status(db))
			dev_dbg(db->dev, "RXCount: %x\n", Rxcount);

		if ((skb_last != NULL) && (RxLen_last > 0)) {
			dev->stats.rx_bytes += RxLen_last;

			/* Pass to upper layer */
			skb_last->protocol = eth_type_trans(skb_last, dev);
			netif_rx(skb_last);
			dev->stats.rx_packets++;
			skb_last = NULL;
			RxLen_last = 0;

			reg_val = readl(db->emac_vbase + EMAC_RX_CTL_REG);
			reg_val &= (~(0x1<<2));
			writel(reg_val, db->emac_vbase + EMAC_RX_CTL_REG);
		}

		if (!Rxcount) {
			emacrx_completed_flag = 1;
			reg_val = readl(db->emac_vbase + EMAC_INT_CTL_REG);
			reg_val |= (0xf<<0) | (0x01<<8);
			writel(reg_val, db->emac_vbase + EMAC_INT_CTL_REG);

			/* had one stuck? */
			Rxcount = readl(db->emac_vbase + EMAC_RX_FBC_REG);
			if (!Rxcount)
				return;
		}

		reg_val = readl(db->emac_vbase + EMAC_RX_IO_DATA_REG);
		if (netif_msg_rx_status(db))
			dev_dbg(db->dev, "receive header: %x\n", reg_val);
		if (reg_val != 0x0143414d) {
			/* disable RX */
			reg_val = readl(db->emac_vbase + EMAC_CTL_REG);
			writel(reg_val & (~(1<<2)), db->emac_vbase + EMAC_CTL_REG);

			/* Flush RX FIFO */
			reg_val = readl(db->emac_vbase + EMAC_RX_CTL_REG);
			writel(reg_val | (1<<3), db->emac_vbase + EMAC_RX_CTL_REG);

			while (readl(db->emac_vbase + EMAC_RX_CTL_REG)&(0x1<<3));

			/* enable RX */
			reg_val = readl(db->emac_vbase + EMAC_CTL_REG);
			writel(reg_val | (1<<2), db->emac_vbase + EMAC_CTL_REG);
			reg_val = readl(db->emac_vbase + EMAC_INT_CTL_REG);
			reg_val |= (0xf<<0) | (0x01<<8);
			writel(reg_val, db->emac_vbase + EMAC_INT_CTL_REG);

			emacrx_completed_flag = 1;

			return;
		}

		/* A packet ready now  & Get status/length */
		GoodPacket = true;

		(db->inblk)(db->emac_vbase + EMAC_RX_IO_DATA_REG, &rxhdr, sizeof(rxhdr));

		if (netif_msg_rx_status(db))
			dev_dbg(db->dev, "rxhdr: %x\n", *((int *)(&rxhdr)));

		RxLen = rxhdr.RxLen;
		RxStatus = rxhdr.RxStatus;

		if (netif_msg_rx_status(db))
			dev_dbg(db->dev, "RX: status %02x, length %04x\n",
					RxStatus, RxLen);

		/* Packet Status check */
		if (RxLen < 0x40) {
			GoodPacket = false;
			if (netif_msg_rx_err(db))
				dev_dbg(db->dev, "RX: Bad Packet (runt)\n");
		}

		/* RxStatus is identical to RSR register. */
		if (0 & RxStatus & (EMAC_CRCERR | EMAC_LENERR)) {
			GoodPacket = false;
			if (RxStatus & EMAC_CRCERR) {
				if (netif_msg_rx_err(db))
					dev_dbg(db->dev, "crc error\n");
				dev->stats.rx_crc_errors++;
			}
			if (RxStatus & EMAC_LENERR) {
				if (netif_msg_rx_err(db))
					dev_dbg(db->dev, "length error\n");
				dev->stats.rx_length_errors++;
			}
		}

		/* Move data from WEMAC */
		if (GoodPacket && ((skb = dev_alloc_skb(RxLen + 4)) != NULL)) {
			skb_reserve(skb, 2);
			rdptr = (u8 *) skb_put(skb, RxLen - 4);

			/* Read received packet from RX SRAM */
			if (netif_msg_rx_status(db))
				dev_dbg(db->dev, "RxLen %x\n", RxLen);

			if (RxLen > DMA_CPU_TRRESHOLD) {
				reg_val = readl(db->emac_vbase + EMAC_RX_CTL_REG);
				reg_val |= (0x1<<2);
				writel(reg_val, db->emac_vbase + EMAC_RX_CTL_REG);
				ret = wemac_inblk_dma(db->emac_vbase + EMAC_RX_IO_DATA_REG, rdptr, RxLen);
				if (ret != 0) {
					printk(KERN_ERR "[emac] wemac_inblk_dma failed,ret=%d, using cpu to read fifo!\n", ret);
					reg_val = readl(db->emac_vbase + EMAC_RX_CTL_REG);
					reg_val &= (~(0x1<<2));
					writel(reg_val, db->emac_vbase + EMAC_RX_CTL_REG);

					(db->inblk)(db->emac_vbase + EMAC_RX_IO_DATA_REG, rdptr, RxLen);

					dev->stats.rx_bytes += RxLen;

					/* Pass to upper layer */
					skb->protocol = eth_type_trans(skb, dev);
					netif_rx(skb);
					dev->stats.rx_packets++;
				} else {
					RxLen_last = RxLen;
					skb_last = skb;
					break;
				}
			} else {
				(db->inblk)(db->emac_vbase + EMAC_RX_IO_DATA_REG, rdptr, RxLen);

				dev->stats.rx_bytes += RxLen;

				/* Pass to upper layer */
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				dev->stats.rx_packets++;
			}
		} else {
			/* need to dump the packet's data */
			(db->dumpblk)(db->emac_vbase + EMAC_RX_IO_DATA_REG, RxLen);
		}
	}
}

static irqreturn_t wemac_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	wemac_board_info_t *db = netdev_priv(dev);
	int int_status;
	unsigned long flags;
	unsigned int reg_val;

#if 0
	int tmp1, tmp2;

	tmp1 = readl(0xf1c00034);
	tmp2 = readl(db->emac_vbase + EMAC_RX_FBC_REG);
	printk(KERN_INFO "%x\n", tmp2);
#endif

	/* A real interrupt coming */

	/* holders of db->lock must always block IRQs */
	spin_lock_irqsave(&db->lock, flags);

	/* Disable all interrupts */
	writel(0, db->emac_vbase + EMAC_INT_CTL_REG);					/* Disable all interrupt */

	/* Got WEMAC interrupt status */
	int_status = readl(db->emac_vbase + EMAC_INT_STA_REG);	/* Got ISR */
	writel(int_status, db->emac_vbase + EMAC_INT_STA_REG);	/* Clear ISR status */

	if (netif_msg_intr(db))
		dev_dbg(db->dev, "emac interrupt %02x\n", int_status);

#if 0
	/* RX Flow Control High Level */
	if (int_status & 0x20000)
		printk(KERN_INFO "f\n");
#endif

	/* Received the coming packet */
	if ((int_status & 0x100) && (emacrx_completed_flag == 1)) {
		/* carrier lost */
		emacrx_completed_flag = 0;
		wemac_rx(dev);
	}

	/* Transmit Interrupt check */
	if (int_status & (0x01 | 0x02))
		wemac_tx_done(dev, db, int_status);

	if (int_status & (0x04 | 0x08))
		printk(KERN_INFO " ab : %x\n", int_status);

#if 0
	if (int_status & (1<<18)) /* carrier lost */
		printk(KERN_INFO "eth net carrier lost\n");
#endif

	/* Re-enable interrupt mask */
	if (emacrx_completed_flag == 1) {
		reg_val = readl(db->emac_vbase + EMAC_INT_CTL_REG);
		reg_val |= (0xf<<0) | (0x01<<8);
		writel(reg_val, db->emac_vbase + EMAC_INT_CTL_REG);
	}
	spin_unlock_irqrestore(&db->lock, flags);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 *Used by netconsole
 */
static void wemac_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	wemac_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/*
 *  Open the interface.
 *  The interface is opened whenever "ifconfig" actives it.
 */
static int wemac_open(struct net_device *dev)
{
	wemac_board_info_t *db = netdev_priv(dev);
	unsigned long irqflags = db->irq_res->flags & IRQF_TRIGGER_MASK;

	if (netif_msg_ifup(db))
		dev_dbg(db->dev, "enabling %s\n", dev->name);

	/* If there is no IRQ type specified, default to something that
	 * may work, and tell the user that this is a problem */

	if (irqflags == IRQF_TRIGGER_NONE)
		dev_warn(db->dev, "WARNING: no IRQ resource flags set.\n");

	irqflags |= IRQF_SHARED;

	if (request_irq(dev->irq, &wemac_interrupt, irqflags, dev->name, dev))
		return -EAGAIN;

	/* Initialize WEMAC board */
	wemac_reset(db);
	wemac_init_wemac(dev);


	/* Init driver variable */
	db->dbug_cnt = 0;

	mii_check_media(&db->mii, netif_msg_link(db), 1);
	netif_start_queue(dev);

	wemac_schedule_poll(db);

	return 0;
}

/*
 * Sleep, either by using msleep() or if we are suspending, then
 * use mdelay() to sleep.
 */
static void wemac_msleep(wemac_board_info_t *db, unsigned int ms)
{
	if (db->in_suspend)
		mdelay(ms);
	else
		msleep(ms);
}

/*
 *   Read a word from phyxcer
 */
static int wemac_phy_read(struct net_device *dev, int phyaddr_unused, int reg)
{
	wemac_board_info_t *db = netdev_priv(dev);
	unsigned long flags;
	int ret;

	mutex_lock(&db->addr_lock);

	spin_lock_irqsave(&db->lock, flags);
	/* issue the phy address and reg */
	writel(WEMAC_PHY | reg, db->emac_vbase + EMAC_MAC_MADR_REG);
	/* pull up the phy io line */
	writel(0x1, db->emac_vbase + EMAC_MAC_MCMD_REG);
	spin_unlock_irqrestore(&db->lock, flags);

	wemac_msleep(db, 1); /* Wait read complete */

	/* push down the phy io line and read data */
	spin_lock_irqsave(&db->lock, flags);
	/* push down the phy io line */
	writel(0x0, db->emac_vbase + EMAC_MAC_MCMD_REG);
	/* and write data */
	ret = readl(db->emac_vbase + EMAC_MAC_MRDD_REG);
	spin_unlock_irqrestore(&db->lock, flags);

	mutex_unlock(&db->addr_lock);

	return ret;
}

/*
 *   Write a word to phyxcer
 */
static void wemac_phy_write(struct net_device *dev,
		int phyaddr_unused, int reg, int value)
{
	wemac_board_info_t *db = netdev_priv(dev);
	unsigned long flags;

	mutex_lock(&db->addr_lock);

	spin_lock_irqsave(&db->lock, flags);
	/* issue the phy address and reg */
	writel(WEMAC_PHY | reg, db->emac_vbase + EMAC_MAC_MADR_REG);
	/* pull up the phy io line */
	writel(0x1, db->emac_vbase + EMAC_MAC_MCMD_REG);
	spin_unlock_irqrestore(&db->lock, flags);

	wemac_msleep(db, 1);		/* Wait write complete */

	spin_lock_irqsave(&db->lock, flags);
	/* push down the phy io line */
	writel(0x0, db->emac_vbase + EMAC_MAC_MCMD_REG);
	/* and write data */
	writel(value, db->emac_vbase + EMAC_MAC_MWTD_REG);
	spin_unlock_irqrestore(&db->lock, flags);

	mutex_unlock(&db->addr_lock);
}

static void wemac_shutdown(struct net_device *dev)
{
	unsigned int reg_val;
	wemac_board_info_t *db = netdev_priv(dev);

	/* RESET device */
	reg_val = wemac_phy_read(dev, 0, 0);
	wemac_phy_write(dev, 0, 0, reg_val | (1<<15));	/* PHY RESET */
	udelay(10);
	reg_val = wemac_phy_read(dev, 0, 0);
	if (reg_val & (1<<15))
		wemac_dbg(db, 5, "phy_reset not complete. value of reg0: %x\n",
			reg_val);
	wemac_phy_write(dev, 0, 0, reg_val | (1<<11));	/* PHY POWER DOWN */

	if (db->mos_pin_handler) {
		db->mos_gpio->data = 0;
		gpio_set_one_pin_status(db->mos_pin_handler, db->mos_gpio, "emac_power", 1);
	}

	writel(0, db->emac_vbase + EMAC_INT_CTL_REG);					/* Disable all interrupt */
	writel(readl(db->emac_vbase + EMAC_INT_STA_REG), db->emac_vbase + EMAC_INT_STA_REG);          /* clear interupt status */
	writel(readl(db->emac_vbase + EMAC_CTL_REG) & (~(0x7)), db->emac_vbase + EMAC_CTL_REG);	/* Disable RX */
}

/*
 * Stop the interface.
 * The interface is stopped when it is brought.
 */
static int wemac_stop(struct net_device *ndev)
{
	wemac_board_info_t *db = netdev_priv(ndev);

	if (netif_msg_ifdown(db))
		dev_dbg(db->dev, "shutting down %s\n", ndev->name);

	cancel_delayed_work_sync(&db->phy_poll);

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	/* free interrupt */
	free_irq(ndev->irq, ndev);

	wemac_shutdown(ndev);

	return 0;
}

#define res_size(_r) (((_r)->end - (_r)->start) + 1)
static const struct net_device_ops wemac_netdev_ops = {
	.ndo_open		= wemac_open,
	.ndo_stop		= wemac_stop,
	.ndo_start_xmit		= wemac_start_xmit,
	.ndo_tx_timeout		= wemac_timeout,
	.ndo_set_multicast_list	= wemac_hash_table,
	.ndo_do_ioctl		= wemac_ioctl,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= wemac_set_mac_address,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= wemac_poll_controller,
#endif
};

/*
 * Search WEMAC board, allocate space and register it
 */
static int __devinit wemac_probe(struct platform_device *pdev)
{
	struct wemac_plat_data *pdata = pdev->dev.platform_data;
	struct wemac_board_info *db;	/* Point a board information structure */
	struct net_device *ndev;
	int ret = 0;
	int iosize;
	unsigned int reg_val;

	/* Init network device */
	ndev = alloc_etherdev(sizeof(struct wemac_board_info));
	if (!ndev) {
		dev_err(&pdev->dev, "could not allocate device.\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* setup board info structure */
	db = netdev_priv(ndev);
	memset(db, 0, sizeof(*db));

	ch_rx = sw_dma_request(DMACH_DEMACR, &emacrx_dma_client, ndev);
	if (ch_rx < 0) {
		printk(KERN_ERR "error when request dma for emac rx\n");
		return ch_rx;
	}

	sw_dma_set_opfn(ch_rx, emacrx_dma_opfn);
	sw_dma_set_buffdone_fn(ch_rx, emacrx_dma_buffdone);

	ch_tx = sw_dma_request(DMACH_DEMACT, &emactx_dma_client, ndev);
	if (ch_tx < 0) {
		printk(KERN_ERR "error when request dma for emac tx\n");
		sw_dma_free(DMACH_DEMACR, &emacrx_dma_client);
		return ch_tx;
	}

	sw_dma_set_opfn(ch_tx, emactx_dma_opfn);
	sw_dma_set_buffdone_fn(ch_tx, emactx_dma_buffdone);

	db->debug_level = 0;
	db->dev = &pdev->dev;
	db->ndev = ndev;

	spin_lock_init(&db->lock);
	mutex_init(&db->addr_lock);

	INIT_DELAYED_WORK(&db->phy_poll, wemac_poll_work);

	db->emac_base_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	db->sram_base_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	db->gpio_base_res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	db->ccmu_base_res = platform_get_resource(pdev, IORESOURCE_MEM, 3);

	db->irq_res  = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (db->emac_base_res == NULL ||
			db->sram_base_res == NULL ||
			db->gpio_base_res == NULL ||
			db->ccmu_base_res == NULL ||
			db->irq_res == NULL) {
		dev_err(db->dev, "insufficient resources\n");
		ret = -ENOENT;
		goto out;
	}

	/* emac address remap */
	iosize = res_size(db->emac_base_res);
	db->emac_base_req = request_mem_region(db->emac_base_res->start, iosize,
			pdev->name);
	if (db->emac_base_req == NULL) {
		dev_err(db->dev, "cannot claim emac address reg area\n");
		ret = -EIO;
		goto out;
	}
	db->emac_vbase = ioremap(db->emac_base_res->start, iosize);
	if (db->emac_vbase == NULL) {
		dev_err(db->dev, "failed to ioremap emac address reg\n");
		ret = -EINVAL;
		goto out;
	}

	/* sram address remap */
	iosize = res_size(db->sram_base_res);
	db->sram_base_req = request_mem_region(db->sram_base_res->start, iosize,
			pdev->name);
	if (db->sram_base_req == NULL) {
		dev_err(db->dev, "cannot claim sram address reg area\n");
		ret = -EIO;
		goto out;
	}
	db->sram_vbase = ioremap(db->sram_base_res->start, iosize);
	if (db->sram_vbase == NULL) {
		dev_err(db->dev, "failed to ioremap sram address reg\n");
		ret = -EINVAL;
		goto out;
	}

	/* gpio address remap */
	iosize = res_size(db->gpio_base_res);
	db->gpio_base_req = request_mem_region(db->gpio_base_res->start, iosize,
			pdev->name);
	if (db->gpio_base_req == NULL) {
		dev_err(db->dev, "cannot claim gpio address reg area\n");
		ret = -EIO;
		goto out;
	}
	db->gpio_vbase = ioremap(db->gpio_base_res->start, iosize);
	if (db->gpio_vbase == NULL) {
		dev_err(db->dev, "failed to ioremap gpio address reg\n");
		ret = -EINVAL;
		goto out;
	}

	db->mos_gpio = kmalloc(sizeof(user_gpio_set_t), GFP_KERNEL);
	db->mos_pin_handler = 0;
	if (NULL == db->mos_gpio) {
		printk(KERN_ERR "can't request memory for mos_gpio\n");
	} else {
		if (SCRIPT_PARSER_OK != script_parser_fetch("emac_para", "emac_power",
					(int *)(db->mos_gpio), sizeof(user_gpio_set_t)/sizeof(int))) {
			printk(KERN_ERR "can't get information emac_power gpio\n");
		} else {
			db->mos_pin_handler = gpio_request(db->mos_gpio, 1);
			if (0 == db->mos_pin_handler)
				printk(KERN_ERR "can't request gpio_port %d, port_num %d\n",
						db->mos_gpio->port, db->mos_gpio->port_num);
		}
	}

	/* ccmu address remap */
	iosize = res_size(db->ccmu_base_res);
	db->ccmu_base_req = request_mem_region(db->ccmu_base_res->start, iosize,
			pdev->name);
	if (db->ccmu_base_req == NULL) {
		dev_err(db->dev, "cannot claim ccmu address reg area\n");
		ret = -EIO;
		goto out;
	}
	db->ccmu_vbase = ioremap(db->ccmu_base_res->start, iosize);
	if (db->ccmu_vbase == NULL) {
		dev_err(db->dev, "failed to ioremap ccmu address reg\n");
		ret = -EINVAL;
		goto out;
	}

	wemac_dbg(db, 3, "emac_vbase: %p, sram_vbase: %p, gpio_vbase: %p, ccmu_vbase: %p\n",
			db->emac_vbase, db->sram_vbase, db->gpio_vbase, db->ccmu_vbase);

	/* fill in parameters for net-dev structure */
	ndev->base_addr = (unsigned long)db->emac_vbase;
	ndev->irq	= db->irq_res->start;

	/* ensure at least we have a default set of IO routines */
	db->dumpblk = wemac_dumpblk_32bit;
	db->outblk  = wemac_outblk_32bit;
	db->inblk   = wemac_inblk_32bit;

	/* check to see if anything is being over-ridden */
	if (pdata != NULL) {
		/* check to see if the driver wants to over-ride the
		 * default IO width */

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

	emac_sys_setup(db);
	wemac_powerup(ndev);

	wemac_reset(db);

	/* from this point we assume that we have found a WEMAC */

	/* driver system function */
	ether_setup(ndev);

	ndev->netdev_ops	= &wemac_netdev_ops;
	ndev->watchdog_timeo	= msecs_to_jiffies(watchdog);
	ndev->ethtool_ops	= &wemac_ethtool_ops;

#ifdef CONFIG_NET_POLL_CONTROLLER
	ndev->poll_controller	 = &wemac_poll_controller;
#endif

	db->msg_enable       = 0xffffffff & (~NETIF_MSG_TX_DONE) & (~NETIF_MSG_INTR) & (~NETIF_MSG_RX_STATUS);
	db->mii.phy_id_mask  = 0x1f;
	db->mii.reg_num_mask = 0x1f;
	/* change force_media value to 0 to force check link status */
	db->mii.force_media  = 0;
	/* change full_duplex value to 0 to set initial duplex as half */
	db->mii.full_duplex  = 0;
	db->mii.dev	     = ndev;
	db->mii.mdio_read    = wemac_phy_read;
	db->mii.mdio_write   = wemac_phy_write;

	if (!is_valid_ether_addr(ndev->dev_addr)) {

		reg_val = readl(db->emac_vbase + EMAC_MAC_A1_REG);
		*(ndev->dev_addr+0) = (reg_val>>16) & 0xff;
		*(ndev->dev_addr+1) = (reg_val>>8) & 0xff;
		*(ndev->dev_addr+2) = (reg_val>>0) & 0xff;
		reg_val = readl(db->emac_vbase + EMAC_MAC_A0_REG);
		*(ndev->dev_addr+3) = (reg_val>>16) & 0xff;
		*(ndev->dev_addr+4) = (reg_val>>8) & 0xff;
		*(ndev->dev_addr+5) = (reg_val>>0) & 0xff;
	}

	if (!is_valid_ether_addr(ndev->dev_addr))
		read_random_macaddr(mac_addr, ndev);

	memcpy(ndev->dev_addr, mac_addr, 6);
	if (!is_valid_ether_addr(ndev->dev_addr))
		printk(KERN_ERR "Invalid MAC address. Please set using ifconfig\n");

	platform_set_drvdata(pdev, ndev);
	ret = register_netdev(ndev);

	if (ret == 0)
		wemac_dbg(db, 3, "%s: at %p, IRQ %d MAC: %p\n",
				ndev->name,
				db->emac_vbase, ndev->irq,
				ndev->dev_addr);

	/* only for debug */
	iounmap(db->sram_vbase);
	iounmap(db->gpio_vbase);
	iounmap(db->ccmu_vbase);
	release_resource(db->sram_base_req);
	kfree(db->sram_base_req);
	release_resource(db->gpio_base_req);
	kfree(db->gpio_base_req);
	release_resource(db->ccmu_base_req);
	kfree(db->ccmu_base_req);

	return 0;

out:
	dev_err(db->dev, "not found (%d).\n", ret);

	wemac_release_board(pdev, db);
	free_netdev(ndev);

	return ret;
}

static int wemac_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(dev);
	wemac_board_info_t *db;

	if (ndev) {
		db = netdev_priv(ndev);
		db->in_suspend = 1;

		/* if (netif_running(ndev)) todo: shutdown the device before open it. bingge */
		if (mii_link_ok(&db->mii))
			netif_carrier_off(ndev);
		netif_device_detach(ndev);
		wemac_shutdown(ndev);
		/* endif */
	}
	return 0;
}

static int wemac_drv_resume(struct platform_device *dev)
{
	struct net_device *ndev = platform_get_drvdata(dev);
	wemac_board_info_t *db = netdev_priv(ndev);

	if (ndev) {
		/* if (netif_running(ndev)) */
		wemac_reset(db);
		wemac_init_wemac(ndev);
		netif_device_attach(ndev);
		if (mii_link_ok(&db->mii))
			netif_carrier_on(ndev);
		/* endif */
		db->in_suspend = 0;
	}
	return 0;
}

static int __devexit wemac_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	unregister_netdev(ndev);
	wemac_release_board(pdev, (wemac_board_info_t *) netdev_priv(ndev));
	free_netdev(ndev);		/* free device structure */

	dev_dbg(&pdev->dev, "released and freed device\n");
	return 0;
}

static struct resource wemac_resources[] = {
	[0] = {
		.start	= EMAC_BASE,
		.end	= EMAC_BASE+1024,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SRAMC_BASE,
		.end	= SRAMC_BASE+1024,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= PIO_BASE,
		.end	= PIO_BASE+1024,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.start	= CCM_BASE,
		.end	= CCM_BASE+1024,
		.flags	= IORESOURCE_MEM,
	},
	[4] = {
		.start	= SW_INT_IRQNO_EMAC,
		.end	= SW_INT_IRQNO_EMAC,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct wemac_plat_data wemac_platdata = {
};

static struct platform_device wemac_device = {
	.name		= "wemac",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(wemac_resources),
	.resource	= wemac_resources,
	.dev		= {
		.platform_data = &wemac_platdata,
	}

};

static struct platform_driver wemac_driver = {
	.driver	= {
		.name    = "wemac",
		.owner	 = THIS_MODULE,
	},
	.probe   = wemac_probe,
	.remove  = __devexit_p(wemac_drv_remove),
	.suspend = wemac_drv_suspend,
	.resume  = wemac_drv_resume,
};

static int __init wemac_init(void)
{
	int emac_used = 0;

	if (SCRIPT_PARSER_OK != script_parser_fetch("emac_para", "emac_used", &emac_used, 1))
		printk(KERN_WARNING "emac_init fetch emac using configuration failed\n");

	if (!emac_used) {
		printk(KERN_INFO "emac driver is disabled\n");
		return 0;
	}

	printk(KERN_INFO "%s Ethernet Driver, V%s in file:%s\n", CARDNAME, DRV_VERSION, __FILE__);

	platform_device_register(&wemac_device);
	return platform_driver_register(&wemac_driver);
}

static void __exit wemac_cleanup(void)
{
	int emac_used = 0;

	if (SCRIPT_PARSER_OK != script_parser_fetch("emac_para", "emac_used", &emac_used, 1))
		printk(KERN_WARNING "emac_init fetch emac using configuration failed\n");

	if (!emac_used) {
		printk(KERN_INFO "emac driver is disabled\n");
		return;
	}

	platform_driver_unregister(&wemac_driver);
}

module_init(wemac_init);
module_exit(wemac_cleanup);

MODULE_AUTHOR("chenlm");
MODULE_DESCRIPTION("Davicom wemac network driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wemac");

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX /* empty */
module_param_named(mac_addr, mac_addr_param, charp, 0);
MODULE_PARM_DESC(mac_addr, "MAC address");

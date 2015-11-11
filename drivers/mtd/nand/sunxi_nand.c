/*
 * Copyright (C) 2013 Boris BREZILLON <b.brezillon.dev@gmail.com>
 *
 * Derived from:
 *	https://github.com/yuq/sunxi-nfc-mtd
 *	Copyright (C) 2013 Qiang Yu <yuq825@gmail.com>
 *
 *	https://github.com/hno/Allwinner-Info
 *	Copyright (C) 2013 Henrik Nordström <Henrik Nordström>
 *
 *	Copyright (C) 2013 Dmitriy B. <rzk333@gmail.com>
 *	Copyright (C) 2013 Sergey Lapin <slapin@ossfans.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_mtd.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define NFC_REG_CTL		0x0000
#define NFC_REG_ST		0x0004
#define NFC_REG_INT		0x0008
#define NFC_REG_TIMING_CTL	0x000C
#define NFC_REG_TIMING_CFG	0x0010
#define NFC_REG_ADDR_LOW	0x0014
#define NFC_REG_ADDR_HIGH	0x0018
#define NFC_REG_SECTOR_NUM	0x001C
#define NFC_REG_CNT		0x0020
#define NFC_REG_CMD		0x0024
#define NFC_REG_RCMD_SET	0x0028
#define NFC_REG_WCMD_SET	0x002C
#define NFC_REG_IO_DATA		0x0030
#define NFC_REG_ECC_CTL		0x0034
#define NFC_REG_ECC_ST		0x0038
#define NFC_REG_DEBUG		0x003C
#define NFC_REG_ECC_ERR_CNT(x)	((0x0040 + (x)) & ~0x3)
#define NFC_REG_USER_DATA(x)	(0x0050 + ((x) * 4))
#define NFC_REG_SPARE_AREA	0x00A0
#define NFC_RAM0_BASE		0x0400
#define NFC_RAM1_BASE		0x0800

/* define bit use in NFC_CTL */
#define NFC_EN			BIT(0)
#define NFC_RESET		BIT(1)
#define NFC_BUS_WIDTH_MSK	BIT(2)
#define NFC_BUS_WIDTH_8		(0 << 2)
#define NFC_BUS_WIDTH_16	(1 << 2)
#define NFC_RB_SEL_MSK		BIT(3)
#define NFC_RB_SEL(x)		((x) << 3)
#define NFC_CE_SEL_MSK		GENMASK(26, 24)
#define NFC_CE_SEL(x)		((x) << 24)
#define NFC_CE_CTL		BIT(6)
#define NFC_PAGE_SHIFT_MSK	GENMASK(11, 8)
#define NFC_PAGE_SHIFT(x)	(((x) < 10 ? 0 : (x) - 10) << 8)
#define NFC_SAM			BIT(12)
#define NFC_RAM_METHOD		BIT(14)
#define NFC_DEBUG_CTL		BIT(31)

/* define bit use in NFC_ST */
#define NFC_RB_B2R		BIT(0)
#define NFC_CMD_INT_FLAG	BIT(1)
#define NFC_DMA_INT_FLAG	BIT(2)
#define NFC_CMD_FIFO_STATUS	BIT(3)
#define NFC_STA			BIT(4)
#define NFC_NATCH_INT_FLAG	BIT(5)
#define NFC_RB_STATE(x)		BIT(x + 8)

/* define bit use in NFC_INT */
#define NFC_B2R_INT_ENABLE	BIT(0)
#define NFC_CMD_INT_ENABLE	BIT(1)
#define NFC_DMA_INT_ENABLE	BIT(2)
#define NFC_INT_MASK		(NFC_B2R_INT_ENABLE | \
				 NFC_CMD_INT_ENABLE | \
				 NFC_DMA_INT_ENABLE)

/* define bit use in NFC_TIMING_CTL */
#define NFC_TIMING_CTL_EDO	BIT(8)

/* define NFC_TIMING_CFG register layout */
#define NFC_TIMING_CFG(tWB, tADL, tWHR, tRHW, tCAD)		\
	(((tWB) & 0x3) | (((tADL) & 0x3) << 2) |		\
	(((tWHR) & 0x3) << 4) | (((tRHW) & 0x3) << 6) |		\
	(((tCAD) & 0x7) << 8))

/* define bit use in NFC_CMD */
#define NFC_CMD_LOW_BYTE_MSK	GENMASK(7, 0)
#define NFC_CMD_HIGH_BYTE_MSK	GENMASK(15, 8)
#define NFC_CMD(x)		(x)
#define NFC_ADR_NUM_MSK		GENMASK(18, 16)
#define NFC_ADR_NUM(x)		(((x) - 1) << 16)
#define NFC_SEND_ADR		BIT(19)
#define NFC_ACCESS_DIR		BIT(20)
#define NFC_DATA_TRANS		BIT(21)
#define NFC_SEND_CMD1		BIT(22)
#define NFC_WAIT_FLAG		BIT(23)
#define NFC_SEND_CMD2		BIT(24)
#define NFC_SEQ			BIT(25)
#define NFC_DATA_SWAP_METHOD	BIT(26)
#define NFC_ROW_AUTO_INC	BIT(27)
#define NFC_SEND_CMD3		BIT(28)
#define NFC_SEND_CMD4		BIT(29)
#define NFC_CMD_TYPE_MSK	GENMASK(31, 30)
#define NFC_NORMAL_OP		(0 << 30)
#define NFC_ECC_OP		(1 << 30)
#define NFC_PAGE_OP		(2 << 30)

/* define bit use in NFC_RCMD_SET */
#define NFC_READ_CMD_MSK	GENMASK(7, 0)
#define NFC_RND_READ_CMD0_MSK	GENMASK(15, 8)
#define NFC_RND_READ_CMD1_MSK	GENMASK(23, 16)

/* define bit use in NFC_WCMD_SET */
#define NFC_PROGRAM_CMD_MSK	GENMASK(7, 0)
#define NFC_RND_WRITE_CMD_MSK	GENMASK(15, 8)
#define NFC_READ_CMD0_MSK	GENMASK(23, 16)
#define NFC_READ_CMD1_MSK	GENMASK(31, 24)

/* define bit use in NFC_ECC_CTL */
#define NFC_ECC_EN		BIT(0)
#define NFC_ECC_PIPELINE	BIT(3)
#define NFC_ECC_EXCEPTION	BIT(4)
#define NFC_ECC_BLOCK_SIZE_MSK	BIT(5)
#define NFC_RANDOM_EN		BIT(9)
#define NFC_RANDOM_DIRECTION	BIT(10)
#define NFC_ECC_MODE_MSK	GENMASK(15, 12)
#define NFC_ECC_MODE(x)		((x) << 12)
#define NFC_RANDOM_SEED_MSK	GENMASK(30, 16)
#define NFC_RANDOM_SEED(x)	((x) << 16)

/* define bit use in NFC_ECC_ST */
#define NFC_ECC_ERR(x)		BIT(x)
#define NFC_ECC_PAT_FOUND(x)	BIT(x + 16)
#define NFC_ECC_ERR_CNT(b, x)	(((x) >> ((b) * 8)) & 0xff)

#define NFC_DEFAULT_TIMEOUT_MS	1000

#define NFC_SRAM_SIZE		1024

#define NFC_MAX_CS		7

/*
 * Ready/Busy detection type: describes the Ready/Busy detection modes
 *
 * @RB_NONE:	no external detection available, rely on STATUS command
 *		and software timeouts
 * @RB_NATIVE:	use sunxi NAND controller Ready/Busy support. The Ready/Busy
 *		pin of the NAND flash chip must be connected to one of the
 *		native NAND R/B pins (those which can be muxed to the NAND
 *		Controller)
 * @RB_GPIO:	use a simple GPIO to handle Ready/Busy status. The Ready/Busy
 *		pin of the NAND flash chip must be connected to a GPIO capable
 *		pin.
 */
enum sunxi_nand_rb_type {
	RB_NONE,
	RB_NATIVE,
	RB_GPIO,
};

/*
 * Ready/Busy structure: stores information related to Ready/Busy detection
 *
 * @type:	the Ready/Busy detection mode
 * @info:	information related to the R/B detection mode. Either a gpio
 *		id or a native R/B id (those supported by the NAND controller).
 */
struct sunxi_nand_rb {
	enum sunxi_nand_rb_type type;
	union {
		int gpio;
		int nativeid;
	} info;
};

/*
 * Chip Select structure: stores information related to NAND Chip Select
 *
 * @cs:		the NAND CS id used to communicate with a NAND Chip
 * @rb:		the Ready/Busy description
 */
struct sunxi_nand_chip_sel {
	u8 cs;
	struct sunxi_nand_rb rb;
};

/*
 * sunxi HW ECC infos: stores information related to HW ECC support
 *
 * @mode:	the sunxi ECC mode field deduced from ECC requirements
 * @layout:	the OOB layout depending on the ECC requirements and the
 *		selected ECC mode
 */
struct sunxi_nand_hw_ecc {
	int mode;
	struct nand_ecclayout layout;
};

/*
 * NAND chip structure: stores NAND chip device related information
 *
 * @node:		used to store NAND chips into a list
 * @nand:		base NAND chip structure
 * @mtd:		base MTD structure
 * @clk_rate:		clk_rate required for this NAND chip
 * @timing_cfg		TIMING_CFG register value for this NAND chip
 * @selected:		current active CS
 * @nsels:		number of CS lines required by the NAND chip
 * @sels:		array of CS lines descriptions
 */
struct sunxi_nand_chip {
	struct list_head node;
	struct nand_chip nand;
	struct mtd_info mtd;
	unsigned long clk_rate;
	u32 timing_cfg;
	u32 timing_ctl;
	int selected;
	int nsels;
	struct sunxi_nand_chip_sel sels[0];
};

static inline struct sunxi_nand_chip *to_sunxi_nand(struct nand_chip *nand)
{
	return container_of(nand, struct sunxi_nand_chip, nand);
}

/*
 * NAND Controller structure: stores sunxi NAND controller information
 *
 * @controller:		base controller structure
 * @dev:		parent device (used to print error messages)
 * @regs:		NAND controller registers
 * @ahb_clk:		NAND Controller AHB clock
 * @mod_clk:		NAND Controller mod clock
 * @assigned_cs:	bitmask describing already assigned CS lines
 * @clk_rate:		NAND controller current clock rate
 * @chips:		a list containing all the NAND chips attached to
 *			this NAND controller
 * @complete:		a completion object used to wait for NAND
 *			controller events
 */
struct sunxi_nfc {
	struct nand_hw_control controller;
	struct device *dev;
	void __iomem *regs;
	struct clk *ahb_clk;
	struct clk *mod_clk;
	unsigned long assigned_cs;
	unsigned long clk_rate;
	struct list_head chips;
	struct completion complete;
};

static inline struct sunxi_nfc *to_sunxi_nfc(struct nand_hw_control *ctrl)
{
	return container_of(ctrl, struct sunxi_nfc, controller);
}

static irqreturn_t sunxi_nfc_interrupt(int irq, void *dev_id)
{
	struct sunxi_nfc *nfc = dev_id;
	u32 st = readl(nfc->regs + NFC_REG_ST);
	u32 ien = readl(nfc->regs + NFC_REG_INT);

	if (!(ien & st))
		return IRQ_NONE;

	if ((ien & st) == ien)
		complete(&nfc->complete);

	writel(st & NFC_INT_MASK, nfc->regs + NFC_REG_ST);
	writel(~st & ien & NFC_INT_MASK, nfc->regs + NFC_REG_INT);

	return IRQ_HANDLED;
}

static int sunxi_nfc_wait_int(struct sunxi_nfc *nfc, u32 flags,
			      unsigned int timeout_ms)
{
	init_completion(&nfc->complete);

	writel(flags, nfc->regs + NFC_REG_INT);

	if (!timeout_ms)
		timeout_ms = NFC_DEFAULT_TIMEOUT_MS;

	if (!wait_for_completion_timeout(&nfc->complete,
					 msecs_to_jiffies(timeout_ms))) {
		dev_err(nfc->dev, "wait interrupt timedout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int sunxi_nfc_wait_cmd_fifo_empty(struct sunxi_nfc *nfc)
{
	unsigned long timeout = jiffies +
				msecs_to_jiffies(NFC_DEFAULT_TIMEOUT_MS);

	do {
		if (!(readl(nfc->regs + NFC_REG_ST) & NFC_CMD_FIFO_STATUS))
			return 0;
	} while (time_before(jiffies, timeout));

	dev_err(nfc->dev, "wait for empty cmd FIFO timedout\n");
	return -ETIMEDOUT;
}

static int sunxi_nfc_rst(struct sunxi_nfc *nfc)
{
	unsigned long timeout = jiffies +
				msecs_to_jiffies(NFC_DEFAULT_TIMEOUT_MS);

	writel(0, nfc->regs + NFC_REG_ECC_CTL);
	writel(NFC_RESET, nfc->regs + NFC_REG_CTL);

	do {
		if (!(readl(nfc->regs + NFC_REG_CTL) & NFC_RESET))
			return 0;
	} while (time_before(jiffies, timeout));

	dev_err(nfc->dev, "wait for NAND controller reset timedout\n");
	return -ETIMEDOUT;
}

static int sunxi_nfc_dev_ready(struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nand_chip *sunxi_nand = to_sunxi_nand(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(sunxi_nand->nand.controller);
	struct sunxi_nand_rb *rb;
	unsigned long timeo = (sunxi_nand->nand.state == FL_ERASING ? 400 : 20);
	int ret;

	if (sunxi_nand->selected < 0)
		return 0;

	rb = &sunxi_nand->sels[sunxi_nand->selected].rb;

	switch (rb->type) {
	case RB_NATIVE:
		ret = !!(readl(nfc->regs + NFC_REG_ST) &
			 NFC_RB_STATE(rb->info.nativeid));
		if (ret)
			break;

		sunxi_nfc_wait_int(nfc, NFC_RB_B2R, timeo);
		ret = !!(readl(nfc->regs + NFC_REG_ST) &
			 NFC_RB_STATE(rb->info.nativeid));
		break;
	case RB_GPIO:
		ret = gpio_get_value(rb->info.gpio);
		break;
	case RB_NONE:
	default:
		ret = 0;
		dev_err(nfc->dev, "cannot check R/B NAND status!\n");
		break;
	}

	return ret;
}

static void sunxi_nfc_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nand_chip *sunxi_nand = to_sunxi_nand(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(sunxi_nand->nand.controller);
	struct sunxi_nand_chip_sel *sel;
	u32 ctl;

	if (chip > 0 && chip >= sunxi_nand->nsels)
		return;

	if (chip == sunxi_nand->selected)
		return;

	ctl = readl(nfc->regs + NFC_REG_CTL) &
	      ~(NFC_PAGE_SHIFT_MSK | NFC_CE_SEL_MSK | NFC_RB_SEL_MSK | NFC_EN);

	if (chip >= 0) {
		sel = &sunxi_nand->sels[chip];

		ctl |= NFC_CE_SEL(sel->cs) | NFC_EN |
		       NFC_PAGE_SHIFT(nand->page_shift - 10);
		if (sel->rb.type == RB_NONE) {
			nand->dev_ready = NULL;
		} else {
			nand->dev_ready = sunxi_nfc_dev_ready;
			if (sel->rb.type == RB_NATIVE)
				ctl |= NFC_RB_SEL(sel->rb.info.nativeid);
		}

		writel(mtd->writesize, nfc->regs + NFC_REG_SPARE_AREA);

		if (nfc->clk_rate != sunxi_nand->clk_rate) {
			clk_set_rate(nfc->mod_clk, sunxi_nand->clk_rate);
			nfc->clk_rate = sunxi_nand->clk_rate;
		}
	}

	writel(sunxi_nand->timing_ctl, nfc->regs + NFC_REG_TIMING_CTL);
	writel(sunxi_nand->timing_cfg, nfc->regs + NFC_REG_TIMING_CFG);
	writel(ctl, nfc->regs + NFC_REG_CTL);

	sunxi_nand->selected = chip;
}

static void sunxi_nfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nand_chip *sunxi_nand = to_sunxi_nand(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(sunxi_nand->nand.controller);
	int ret;
	int cnt;
	int offs = 0;
	u32 tmp;

	while (len > offs) {
		cnt = min(len - offs, NFC_SRAM_SIZE);

		ret = sunxi_nfc_wait_cmd_fifo_empty(nfc);
		if (ret)
			break;

		writel(cnt, nfc->regs + NFC_REG_CNT);
		tmp = NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD;
		writel(tmp, nfc->regs + NFC_REG_CMD);

		ret = sunxi_nfc_wait_int(nfc, NFC_CMD_INT_FLAG, 0);
		if (ret)
			break;

		if (buf)
			memcpy_fromio(buf + offs, nfc->regs + NFC_RAM0_BASE,
				      cnt);
		offs += cnt;
	}
}

static void sunxi_nfc_write_buf(struct mtd_info *mtd, const uint8_t *buf,
				int len)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nand_chip *sunxi_nand = to_sunxi_nand(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(sunxi_nand->nand.controller);
	int ret;
	int cnt;
	int offs = 0;
	u32 tmp;

	while (len > offs) {
		cnt = min(len - offs, NFC_SRAM_SIZE);

		ret = sunxi_nfc_wait_cmd_fifo_empty(nfc);
		if (ret)
			break;

		writel(cnt, nfc->regs + NFC_REG_CNT);
		memcpy_toio(nfc->regs + NFC_RAM0_BASE, buf + offs, cnt);
		tmp = NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD |
		      NFC_ACCESS_DIR;
		writel(tmp, nfc->regs + NFC_REG_CMD);

		ret = sunxi_nfc_wait_int(nfc, NFC_CMD_INT_FLAG, 0);
		if (ret)
			break;

		offs += cnt;
	}
}

static uint8_t sunxi_nfc_read_byte(struct mtd_info *mtd)
{
	uint8_t ret;

	sunxi_nfc_read_buf(mtd, &ret, 1);

	return ret;
}

static void sunxi_nfc_cmd_ctrl(struct mtd_info *mtd, int dat,
			       unsigned int ctrl)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nand_chip *sunxi_nand = to_sunxi_nand(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(sunxi_nand->nand.controller);
	int ret;
	u32 tmp;

	ret = sunxi_nfc_wait_cmd_fifo_empty(nfc);
	if (ret)
		return;

	if (ctrl & NAND_CTRL_CHANGE) {
		tmp = readl(nfc->regs + NFC_REG_CTL);
		if (ctrl & NAND_NCE)
			tmp |= NFC_CE_CTL;
		else
			tmp &= ~NFC_CE_CTL;
		writel(tmp, nfc->regs + NFC_REG_CTL);
	}

	if (dat == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE) {
		writel(NFC_SEND_CMD1 | dat, nfc->regs + NFC_REG_CMD);
	} else {
		writel(dat, nfc->regs + NFC_REG_ADDR_LOW);
		writel(NFC_SEND_ADR, nfc->regs + NFC_REG_CMD);
	}

	sunxi_nfc_wait_int(nfc, NFC_CMD_INT_FLAG, 0);
}

static void sunxi_nfc_hw_ecc_enable(struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	struct sunxi_nand_hw_ecc *data = nand->ecc.priv;
	u32 ecc_ctl;

	ecc_ctl = readl(nfc->regs + NFC_REG_ECC_CTL);
	ecc_ctl &= ~(NFC_ECC_MODE_MSK | NFC_ECC_PIPELINE |
		     NFC_ECC_BLOCK_SIZE_MSK);
	ecc_ctl |= NFC_ECC_EN | NFC_ECC_MODE(data->mode) | NFC_ECC_EXCEPTION;

	writel(ecc_ctl, nfc->regs + NFC_REG_ECC_CTL);
}

static void sunxi_nfc_hw_ecc_disable(struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);

	writel(readl(nfc->regs + NFC_REG_ECC_CTL) & ~NFC_ECC_EN,
	       nfc->regs + NFC_REG_ECC_CTL);
}

static inline void sunxi_nfc_user_data_to_buf(u32 user_data, u8 *buf)
{
	buf[0] = user_data;
	buf[1] = user_data >> 8;
	buf[2] = user_data >> 16;
	buf[3] = user_data >> 24;
}

static int sunxi_nfc_hw_ecc_read_chunk(struct mtd_info *mtd,
				       u8 *data, int data_off,
				       u8 *oob, int oob_off,
				       int *cur_off,
				       unsigned int *max_bitflips)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	u32 status;
	int ret;

	if (*cur_off != data_off)
		nand->cmdfunc(mtd, NAND_CMD_RNDOUT, data_off, -1);

	sunxi_nfc_read_buf(mtd, NULL, ecc->size);

	if (data_off + ecc->size != oob_off)
		nand->cmdfunc(mtd, NAND_CMD_RNDOUT, oob_off, -1);

	ret = sunxi_nfc_wait_cmd_fifo_empty(nfc);
	if (ret)
		return ret;

	writel(NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD | NFC_ECC_OP,
	       nfc->regs + NFC_REG_CMD);

	ret = sunxi_nfc_wait_int(nfc, NFC_CMD_INT_FLAG, 0);
	if (ret)
		return ret;

	status = readl(nfc->regs + NFC_REG_ECC_ST);
	ret = NFC_ECC_ERR_CNT(0, readl(nfc->regs + NFC_REG_ECC_ERR_CNT(0)));

	memcpy_fromio(data, nfc->regs + NFC_RAM0_BASE, ecc->size);

	nand->cmdfunc(mtd, NAND_CMD_RNDOUT, oob_off, -1);
	sunxi_nfc_read_buf(mtd, oob, ecc->bytes + 4);

	if (status & NFC_ECC_ERR(0)) {
		ret = nand_check_erased_ecc_chunk(data,	ecc->size,
						  oob, ecc->bytes + 4,
						  NULL, 0, ecc->strength);
	} else {
		/*
		 * The engine protects 4 bytes of OOB data per chunk.
		 * Retrieve the corrected OOB bytes.
		 */
		sunxi_nfc_user_data_to_buf(readl(nfc->regs + NFC_REG_USER_DATA(0)),
					   oob);
	}

	if (ret < 0) {
		mtd->ecc_stats.failed++;
	} else {
		mtd->ecc_stats.corrected += ret;
		*max_bitflips = max_t(unsigned int, *max_bitflips, ret);
	}

	*cur_off = oob_off + ecc->bytes + 4;

	return 0;
}

static void sunxi_nfc_hw_ecc_read_extra_oob(struct mtd_info *mtd,
					    u8 *oob, int *cur_off)
{
	struct nand_chip *nand = mtd->priv;
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	int offset = ((ecc->bytes + 4) * ecc->steps);
	int len = mtd->oobsize - offset;

	if (len <= 0)
		return;

	if (*cur_off != offset)
		nand->cmdfunc(mtd, NAND_CMD_RNDOUT,
			      offset + mtd->writesize, -1);

	sunxi_nfc_read_buf(mtd, oob + offset, len);

	*cur_off = mtd->oobsize + mtd->writesize;
}

static inline u32 sunxi_nfc_buf_to_user_data(const u8 *buf)
{
	return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

static int sunxi_nfc_hw_ecc_write_chunk(struct mtd_info *mtd,
					const u8 *data, int data_off,
					const u8 *oob, int oob_off,
					int *cur_off)
{
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	int ret;

	if (data_off != *cur_off)
		nand->cmdfunc(mtd, NAND_CMD_RNDIN, data_off, -1);

	sunxi_nfc_write_buf(mtd, data, ecc->size);

	/* Fill OOB data in */
	writel(sunxi_nfc_buf_to_user_data(oob),
	       nfc->regs + NFC_REG_USER_DATA(0));

	if (data_off + ecc->size != oob_off)
		nand->cmdfunc(mtd, NAND_CMD_RNDIN, oob_off, -1);

	ret = sunxi_nfc_wait_cmd_fifo_empty(nfc);
	if (ret)
		return ret;

	writel(NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD |
	       NFC_ACCESS_DIR | NFC_ECC_OP,
	       nfc->regs + NFC_REG_CMD);

	ret = sunxi_nfc_wait_int(nfc, NFC_CMD_INT_FLAG, 0);
	if (ret)
		return ret;

	*cur_off = oob_off + ecc->bytes + 4;

	return 0;
}

static void sunxi_nfc_hw_ecc_write_extra_oob(struct mtd_info *mtd,
					     u8 *oob, int *cur_off)
{
	struct nand_chip *nand = mtd->priv;
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	int offset = ((ecc->bytes + 4) * ecc->steps);
	int len = mtd->oobsize - offset;

	if (len <= 0)
		return;

	if (*cur_off != offset)
		nand->cmdfunc(mtd, NAND_CMD_RNDIN,
			      offset + mtd->writesize, -1);

	sunxi_nfc_write_buf(mtd, oob + offset, len);

	*cur_off = mtd->oobsize + mtd->writesize;
}

static int sunxi_nfc_hw_ecc_read_page(struct mtd_info *mtd,
				      struct nand_chip *chip, uint8_t *buf,
				      int oob_required, int page)
{
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	unsigned int max_bitflips = 0;
	int ret, i, cur_off = 0;

	sunxi_nfc_hw_ecc_enable(mtd);

	for (i = 0; i < ecc->steps; i++) {
		int data_off = i * ecc->size;
		int oob_off = i * (ecc->bytes + 4);
		u8 *data = buf + data_off;
		u8 *oob = chip->oob_poi + oob_off;

		ret = sunxi_nfc_hw_ecc_read_chunk(mtd, data, data_off, oob,
						  oob_off + mtd->writesize,
						  &cur_off, &max_bitflips);
		if (ret)
			return ret;
	}

	if (oob_required)
		sunxi_nfc_hw_ecc_read_extra_oob(mtd, chip->oob_poi, &cur_off);

	sunxi_nfc_hw_ecc_disable(mtd);

	return max_bitflips;
}

static int sunxi_nfc_hw_ecc_write_page(struct mtd_info *mtd,
				       struct nand_chip *chip,
				       const uint8_t *buf, int oob_required,
				       int page)
{
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int ret, i, cur_off = 0;

	sunxi_nfc_hw_ecc_enable(mtd);

	for (i = 0; i < ecc->steps; i++) {
		int data_off = i * ecc->size;
		int oob_off = i * (ecc->bytes + 4);
		const u8 *data = buf + data_off;
		const u8 *oob = chip->oob_poi + oob_off;

		ret = sunxi_nfc_hw_ecc_write_chunk(mtd, data, data_off, oob,
						   oob_off + mtd->writesize,
						   &cur_off);
		if (ret)
			return ret;
	}

	if (oob_required)
		sunxi_nfc_hw_ecc_write_extra_oob(mtd, chip->oob_poi, &cur_off);

	sunxi_nfc_hw_ecc_disable(mtd);

	return 0;
}

static int sunxi_nfc_hw_syndrome_ecc_read_page(struct mtd_info *mtd,
					       struct nand_chip *chip,
					       uint8_t *buf, int oob_required,
					       int page)
{
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	unsigned int max_bitflips = 0;
	int ret, i, cur_off = 0;

	sunxi_nfc_hw_ecc_enable(mtd);

	for (i = 0; i < ecc->steps; i++) {
		int data_off = i * (ecc->size + ecc->bytes + 4);
		int oob_off = data_off + ecc->size;
		u8 *data = buf + (i * ecc->size);
		u8 *oob = chip->oob_poi + (i * (ecc->bytes + 4));

		ret = sunxi_nfc_hw_ecc_read_chunk(mtd, data, data_off, oob,
						  oob_off, &cur_off,
						  &max_bitflips);
		if (ret)
			return ret;
	}

	if (oob_required)
		sunxi_nfc_hw_ecc_read_extra_oob(mtd, chip->oob_poi, &cur_off);

	sunxi_nfc_hw_ecc_disable(mtd);

	return max_bitflips;
}

static int sunxi_nfc_hw_syndrome_ecc_write_page(struct mtd_info *mtd,
						struct nand_chip *chip,
						const uint8_t *buf,
						int oob_required, int page)
{
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int ret, i, cur_off = 0;

	sunxi_nfc_hw_ecc_enable(mtd);

	for (i = 0; i < ecc->steps; i++) {
		int data_off = i * (ecc->size + ecc->bytes + 4);
		int oob_off = data_off + ecc->size;
		const u8 *data = buf + (i * ecc->size);
		const u8 *oob = chip->oob_poi + (i * (ecc->bytes + 4));

		ret = sunxi_nfc_hw_ecc_write_chunk(mtd, data, data_off,
						   oob, oob_off, &cur_off);
		if (ret)
			return ret;
	}

	if (oob_required)
		sunxi_nfc_hw_ecc_write_extra_oob(mtd, chip->oob_poi, &cur_off);

	sunxi_nfc_hw_ecc_disable(mtd);

	return 0;
}

static const s32 tWB_lut[] = {6, 12, 16, 20};
static const s32 tRHW_lut[] = {4, 8, 12, 20};

static int _sunxi_nand_lookup_timing(const s32 *lut, int lut_size, u32 duration,
		u32 clk_period)
{
	u32 clk_cycles = DIV_ROUND_UP(duration, clk_period);
	int i;

	for (i = 0; i < lut_size; i++) {
		if (clk_cycles <= lut[i])
			return i;
	}

	/* Doesn't fit */
	return -EINVAL;
}

#define sunxi_nand_lookup_timing(l, p, c) \
			_sunxi_nand_lookup_timing(l, ARRAY_SIZE(l), p, c)

static int sunxi_nand_chip_set_timings(struct sunxi_nand_chip *chip,
				       const struct nand_sdr_timings *timings)
{
	struct sunxi_nfc *nfc = to_sunxi_nfc(chip->nand.controller);
	u32 min_clk_period = 0;
	s32 tWB, tADL, tWHR, tRHW, tCAD;

	/* T1 <=> tCLS */
	if (timings->tCLS_min > min_clk_period)
		min_clk_period = timings->tCLS_min;

	/* T2 <=> tCLH */
	if (timings->tCLH_min > min_clk_period)
		min_clk_period = timings->tCLH_min;

	/* T3 <=> tCS */
	if (timings->tCS_min > min_clk_period)
		min_clk_period = timings->tCS_min;

	/* T4 <=> tCH */
	if (timings->tCH_min > min_clk_period)
		min_clk_period = timings->tCH_min;

	/* T5 <=> tWP */
	if (timings->tWP_min > min_clk_period)
		min_clk_period = timings->tWP_min;

	/* T6 <=> tWH */
	if (timings->tWH_min > min_clk_period)
		min_clk_period = timings->tWH_min;

	/* T7 <=> tALS */
	if (timings->tALS_min > min_clk_period)
		min_clk_period = timings->tALS_min;

	/* T8 <=> tDS */
	if (timings->tDS_min > min_clk_period)
		min_clk_period = timings->tDS_min;

	/* T9 <=> tDH */
	if (timings->tDH_min > min_clk_period)
		min_clk_period = timings->tDH_min;

	/* T10 <=> tRR */
	if (timings->tRR_min > (min_clk_period * 3))
		min_clk_period = DIV_ROUND_UP(timings->tRR_min, 3);

	/* T11 <=> tALH */
	if (timings->tALH_min > min_clk_period)
		min_clk_period = timings->tALH_min;

	/* T12 <=> tRP */
	if (timings->tRP_min > min_clk_period)
		min_clk_period = timings->tRP_min;

	/* T13 <=> tREH */
	if (timings->tREH_min > min_clk_period)
		min_clk_period = timings->tREH_min;

	/* T14 <=> tRC */
	if (timings->tRC_min > (min_clk_period * 2))
		min_clk_period = DIV_ROUND_UP(timings->tRC_min, 2);

	/* T15 <=> tWC */
	if (timings->tWC_min > (min_clk_period * 2))
		min_clk_period = DIV_ROUND_UP(timings->tWC_min, 2);

	/* T16 - T19 + tCAD */
	tWB  = sunxi_nand_lookup_timing(tWB_lut, timings->tWB_max,
					min_clk_period);
	if (tWB < 0) {
		dev_err(nfc->dev, "unsupported tWB\n");
		return tWB;
	}

	tADL = DIV_ROUND_UP(timings->tADL_min, min_clk_period) >> 3;
	if (tADL > 3) {
		dev_err(nfc->dev, "unsupported tADL\n");
		return -EINVAL;
	}

	tWHR = DIV_ROUND_UP(timings->tWHR_min, min_clk_period) >> 3;
	if (tWHR > 3) {
		dev_err(nfc->dev, "unsupported tWHR\n");
		return -EINVAL;
	}

	tRHW = sunxi_nand_lookup_timing(tRHW_lut, timings->tRHW_min,
					min_clk_period);
	if (tRHW < 0) {
		dev_err(nfc->dev, "unsupported tRHW\n");
		return tRHW;
	}

	/*
	 * TODO: according to ONFI specs this value only applies for DDR NAND,
	 * but Allwinner seems to set this to 0x7. Mimic them for now.
	 */
	tCAD = 0x7;

	/* TODO: A83 has some more bits for CDQSS, CS, CLHZ, CCS, WC */
	chip->timing_cfg = NFC_TIMING_CFG(tWB, tADL, tWHR, tRHW, tCAD);

	/*
	 * ONFI specification 3.1, paragraph 4.15.2 dictates that EDO data
	 * output cycle timings shall be used if the host drives tRC less than
	 * 30 ns.
	 */
	chip->timing_ctl = (timings->tRC_min < 30000) ? NFC_TIMING_CTL_EDO : 0;

	/* Convert min_clk_period from picoseconds to nanoseconds */
	min_clk_period = DIV_ROUND_UP(min_clk_period, 1000);

	/*
	 * Convert min_clk_period into a clk frequency, then get the
	 * appropriate rate for the NAND controller IP given this formula
	 * (specified in the datasheet):
	 * nand clk_rate = 2 * min_clk_rate
	 */
	chip->clk_rate = (2 * NSEC_PER_SEC) / min_clk_period;

	return 0;
}

static int sunxi_nand_chip_init_timings(struct sunxi_nand_chip *chip,
					struct device_node *np)
{
	const struct nand_sdr_timings *timings;
	int ret;
	int mode;

	mode = onfi_get_async_timing_mode(&chip->nand);
	if (mode == ONFI_TIMING_MODE_UNKNOWN) {
		mode = chip->nand.onfi_timing_mode_default;
	} else {
		uint8_t feature[ONFI_SUBFEATURE_PARAM_LEN] = {};
		int i;

		mode = fls(mode) - 1;
		if (mode < 0)
			mode = 0;

		feature[0] = mode;
		for (i = 0; i < chip->nsels; i++) {
			chip->nand.select_chip(&chip->mtd, i);
			ret = chip->nand.onfi_set_features(&chip->mtd,
						&chip->nand,
						ONFI_FEATURE_ADDR_TIMING_MODE,
						feature);
			chip->nand.select_chip(&chip->mtd, -1);
			if (ret)
				return ret;
		}
	}

	timings = onfi_async_timing_mode_to_sdr_timings(mode);
	if (IS_ERR(timings))
		return PTR_ERR(timings);

	return sunxi_nand_chip_set_timings(chip, timings);
}

static int sunxi_nand_hw_common_ecc_ctrl_init(struct mtd_info *mtd,
					      struct nand_ecc_ctrl *ecc,
					      struct device_node *np)
{
	static const u8 strengths[] = { 16, 24, 28, 32, 40, 48, 56, 60, 64 };
	struct nand_chip *nand = mtd->priv;
	struct sunxi_nand_chip *sunxi_nand = to_sunxi_nand(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(sunxi_nand->nand.controller);
	struct sunxi_nand_hw_ecc *data;
	struct nand_ecclayout *layout;
	int nsectors;
	int ret;
	int i;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Add ECC info retrieval from DT */
	for (i = 0; i < ARRAY_SIZE(strengths); i++) {
		if (ecc->strength <= strengths[i])
			break;
	}

	if (i >= ARRAY_SIZE(strengths)) {
		dev_err(nfc->dev, "unsupported strength\n");
		ret = -ENOTSUPP;
		goto err;
	}

	data->mode = i;

	/* HW ECC always request ECC bytes for 1024 bytes blocks */
	ecc->bytes = DIV_ROUND_UP(ecc->strength * fls(8 * 1024), 8);

	/* HW ECC always work with even numbers of ECC bytes */
	ecc->bytes = ALIGN(ecc->bytes, 2);

	layout = &data->layout;
	nsectors = mtd->writesize / ecc->size;

	if (mtd->oobsize < ((ecc->bytes + 4) * nsectors)) {
		ret = -EINVAL;
		goto err;
	}

	layout->eccbytes = (ecc->bytes * nsectors);

	ecc->layout = layout;
	ecc->priv = data;

	return 0;

err:
	kfree(data);

	return ret;
}

static void sunxi_nand_hw_common_ecc_ctrl_cleanup(struct nand_ecc_ctrl *ecc)
{
	kfree(ecc->priv);
}

static int sunxi_nand_hw_ecc_ctrl_init(struct mtd_info *mtd,
				       struct nand_ecc_ctrl *ecc,
				       struct device_node *np)
{
	struct nand_ecclayout *layout;
	int nsectors;
	int i, j;
	int ret;

	ret = sunxi_nand_hw_common_ecc_ctrl_init(mtd, ecc, np);
	if (ret)
		return ret;

	ecc->read_page = sunxi_nfc_hw_ecc_read_page;
	ecc->write_page = sunxi_nfc_hw_ecc_write_page;
	layout = ecc->layout;
	nsectors = mtd->writesize / ecc->size;

	for (i = 0; i < nsectors; i++) {
		if (i) {
			layout->oobfree[i].offset =
				layout->oobfree[i - 1].offset +
				layout->oobfree[i - 1].length +
				ecc->bytes;
			layout->oobfree[i].length = 4;
		} else {
			/*
			 * The first 2 bytes are used for BB markers, hence we
			 * only have 2 bytes available in the first user data
			 * section.
			 */
			layout->oobfree[i].length = 2;
			layout->oobfree[i].offset = 2;
		}

		for (j = 0; j < ecc->bytes; j++)
			layout->eccpos[(ecc->bytes * i) + j] =
					layout->oobfree[i].offset +
					layout->oobfree[i].length + j;
	}

	if (mtd->oobsize > (ecc->bytes + 4) * nsectors) {
		layout->oobfree[nsectors].offset =
				layout->oobfree[nsectors - 1].offset +
				layout->oobfree[nsectors - 1].length +
				ecc->bytes;
		layout->oobfree[nsectors].length = mtd->oobsize -
				((ecc->bytes + 4) * nsectors);
	}

	return 0;
}

static int sunxi_nand_hw_syndrome_ecc_ctrl_init(struct mtd_info *mtd,
						struct nand_ecc_ctrl *ecc,
						struct device_node *np)
{
	struct nand_ecclayout *layout;
	int nsectors;
	int i;
	int ret;

	ret = sunxi_nand_hw_common_ecc_ctrl_init(mtd, ecc, np);
	if (ret)
		return ret;

	ecc->prepad = 4;
	ecc->read_page = sunxi_nfc_hw_syndrome_ecc_read_page;
	ecc->write_page = sunxi_nfc_hw_syndrome_ecc_write_page;

	layout = ecc->layout;
	nsectors = mtd->writesize / ecc->size;

	for (i = 0; i < (ecc->bytes * nsectors); i++)
		layout->eccpos[i] = i;

	layout->oobfree[0].length = mtd->oobsize - i;
	layout->oobfree[0].offset = i;

	return 0;
}

static void sunxi_nand_ecc_cleanup(struct nand_ecc_ctrl *ecc)
{
	switch (ecc->mode) {
	case NAND_ECC_HW:
	case NAND_ECC_HW_SYNDROME:
		sunxi_nand_hw_common_ecc_ctrl_cleanup(ecc);
		break;
	case NAND_ECC_NONE:
		kfree(ecc->layout);
	default:
		break;
	}
}

static int sunxi_nand_ecc_init(struct mtd_info *mtd, struct nand_ecc_ctrl *ecc,
			       struct device_node *np)
{
	struct nand_chip *nand = mtd->priv;
	int ret;

	if (!ecc->size) {
		ecc->size = nand->ecc_step_ds;
		ecc->strength = nand->ecc_strength_ds;
	}

	if (!ecc->size || !ecc->strength)
		return -EINVAL;

	switch (ecc->mode) {
	case NAND_ECC_SOFT_BCH:
		break;
	case NAND_ECC_HW:
		ret = sunxi_nand_hw_ecc_ctrl_init(mtd, ecc, np);
		if (ret)
			return ret;
		break;
	case NAND_ECC_HW_SYNDROME:
		ret = sunxi_nand_hw_syndrome_ecc_ctrl_init(mtd, ecc, np);
		if (ret)
			return ret;
		break;
	case NAND_ECC_NONE:
		ecc->layout = kzalloc(sizeof(*ecc->layout), GFP_KERNEL);
		if (!ecc->layout)
			return -ENOMEM;
		ecc->layout->oobfree[0].length = mtd->oobsize;
	case NAND_ECC_SOFT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sunxi_nand_chip_init(struct device *dev, struct sunxi_nfc *nfc,
				struct device_node *np)
{
	const struct nand_sdr_timings *timings;
	struct sunxi_nand_chip *chip;
	struct mtd_part_parser_data ppdata;
	struct mtd_info *mtd;
	struct nand_chip *nand;
	int nsels;
	int ret;
	int i;
	u32 tmp;

	if (!of_get_property(np, "reg", &nsels))
		return -EINVAL;

	nsels /= sizeof(u32);
	if (!nsels) {
		dev_err(dev, "invalid reg property size\n");
		return -EINVAL;
	}

	chip = devm_kzalloc(dev,
			    sizeof(*chip) +
			    (nsels * sizeof(struct sunxi_nand_chip_sel)),
			    GFP_KERNEL);
	if (!chip) {
		dev_err(dev, "could not allocate chip\n");
		return -ENOMEM;
	}

	chip->nsels = nsels;
	chip->selected = -1;

	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &tmp);
		if (ret) {
			dev_err(dev, "could not retrieve reg property: %d\n",
				ret);
			return ret;
		}

		if (tmp > NFC_MAX_CS) {
			dev_err(dev,
				"invalid reg value: %u (max CS = 7)\n",
				tmp);
			return -EINVAL;
		}

		if (test_and_set_bit(tmp, &nfc->assigned_cs)) {
			dev_err(dev, "CS %d already assigned\n", tmp);
			return -EINVAL;
		}

		chip->sels[i].cs = tmp;

		if (!of_property_read_u32_index(np, "allwinner,rb", i, &tmp) &&
		    tmp < 2) {
			chip->sels[i].rb.type = RB_NATIVE;
			chip->sels[i].rb.info.nativeid = tmp;
		} else {
			ret = of_get_named_gpio(np, "rb-gpios", i);
			if (ret >= 0) {
				tmp = ret;
				chip->sels[i].rb.type = RB_GPIO;
				chip->sels[i].rb.info.gpio = tmp;
				ret = devm_gpio_request(dev, tmp, "nand-rb");
				if (ret)
					return ret;

				ret = gpio_direction_input(tmp);
				if (ret)
					return ret;
			} else {
				chip->sels[i].rb.type = RB_NONE;
			}
		}
	}

	timings = onfi_async_timing_mode_to_sdr_timings(0);
	if (IS_ERR(timings)) {
		ret = PTR_ERR(timings);
		dev_err(dev,
			"could not retrieve timings for ONFI mode 0: %d\n",
			ret);
		return ret;
	}

	ret = sunxi_nand_chip_set_timings(chip, timings);
	if (ret) {
		dev_err(dev, "could not configure chip timings: %d\n", ret);
		return ret;
	}

	nand = &chip->nand;
	/* Default tR value specified in the ONFI spec (chapter 4.15.1) */
	nand->chip_delay = 200;
	nand->controller = &nfc->controller;
	/*
	 * Set the ECC mode to the default value in case nothing is specified
	 * in the DT.
	 */
	nand->ecc.mode = NAND_ECC_HW;
	nand->flash_node = np;
	nand->select_chip = sunxi_nfc_select_chip;
	nand->cmd_ctrl = sunxi_nfc_cmd_ctrl;
	nand->read_buf = sunxi_nfc_read_buf;
	nand->write_buf = sunxi_nfc_write_buf;
	nand->read_byte = sunxi_nfc_read_byte;

	mtd = &chip->mtd;
	mtd->dev.parent = dev;
	mtd->priv = nand;

	ret = nand_scan_ident(mtd, nsels, NULL);
	if (ret)
		return ret;

	if (nand->bbt_options & NAND_BBT_USE_FLASH)
		nand->bbt_options |= NAND_BBT_NO_OOB;

	ret = sunxi_nand_chip_init_timings(chip, np);
	if (ret) {
		dev_err(dev, "could not configure chip timings: %d\n", ret);
		return ret;
	}

	ret = sunxi_nand_ecc_init(mtd, &nand->ecc, np);
	if (ret) {
		dev_err(dev, "ECC init failed: %d\n", ret);
		return ret;
	}

	ret = nand_scan_tail(mtd);
	if (ret) {
		dev_err(dev, "nand_scan_tail failed: %d\n", ret);
		return ret;
	}

	ppdata.of_node = np;
	ret = mtd_device_parse_register(mtd, NULL, &ppdata, NULL, 0);
	if (ret) {
		dev_err(dev, "failed to register mtd device: %d\n", ret);
		nand_release(mtd);
		return ret;
	}

	list_add_tail(&chip->node, &nfc->chips);

	return 0;
}

static int sunxi_nand_chips_init(struct device *dev, struct sunxi_nfc *nfc)
{
	struct device_node *np = dev->of_node;
	struct device_node *nand_np;
	int nchips = of_get_child_count(np);
	int ret;

	if (nchips > 8) {
		dev_err(dev, "too many NAND chips: %d (max = 8)\n", nchips);
		return -EINVAL;
	}

	for_each_child_of_node(np, nand_np) {
		ret = sunxi_nand_chip_init(dev, nfc, nand_np);
		if (ret)
			return ret;
	}

	return 0;
}

static void sunxi_nand_chips_cleanup(struct sunxi_nfc *nfc)
{
	struct sunxi_nand_chip *chip;

	while (!list_empty(&nfc->chips)) {
		chip = list_first_entry(&nfc->chips, struct sunxi_nand_chip,
					node);
		nand_release(&chip->mtd);
		sunxi_nand_ecc_cleanup(&chip->nand.ecc);
		list_del(&chip->node);
	}
}

static int sunxi_nfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	struct sunxi_nfc *nfc;
	int irq;
	int ret;

	nfc = devm_kzalloc(dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->dev = dev;
	spin_lock_init(&nfc->controller.lock);
	init_waitqueue_head(&nfc->controller.wq);
	INIT_LIST_HEAD(&nfc->chips);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nfc->regs = devm_ioremap_resource(dev, r);
	if (IS_ERR(nfc->regs))
		return PTR_ERR(nfc->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to retrieve irq\n");
		return irq;
	}

	nfc->ahb_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(nfc->ahb_clk)) {
		dev_err(dev, "failed to retrieve ahb clk\n");
		return PTR_ERR(nfc->ahb_clk);
	}

	ret = clk_prepare_enable(nfc->ahb_clk);
	if (ret)
		return ret;

	nfc->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(nfc->mod_clk)) {
		dev_err(dev, "failed to retrieve mod clk\n");
		ret = PTR_ERR(nfc->mod_clk);
		goto out_ahb_clk_unprepare;
	}

	ret = clk_prepare_enable(nfc->mod_clk);
	if (ret)
		goto out_ahb_clk_unprepare;

	ret = sunxi_nfc_rst(nfc);
	if (ret)
		goto out_mod_clk_unprepare;

	writel(0, nfc->regs + NFC_REG_INT);
	ret = devm_request_irq(dev, irq, sunxi_nfc_interrupt,
			       0, "sunxi-nand", nfc);
	if (ret)
		goto out_mod_clk_unprepare;

	platform_set_drvdata(pdev, nfc);

	ret = sunxi_nand_chips_init(dev, nfc);
	if (ret) {
		dev_err(dev, "failed to init nand chips\n");
		goto out_mod_clk_unprepare;
	}

	return 0;

out_mod_clk_unprepare:
	clk_disable_unprepare(nfc->mod_clk);
out_ahb_clk_unprepare:
	clk_disable_unprepare(nfc->ahb_clk);

	return ret;
}

static int sunxi_nfc_remove(struct platform_device *pdev)
{
	struct sunxi_nfc *nfc = platform_get_drvdata(pdev);

	sunxi_nand_chips_cleanup(nfc);

	return 0;
}

static const struct of_device_id sunxi_nfc_ids[] = {
	{ .compatible = "allwinner,sun4i-a10-nand" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sunxi_nfc_ids);

static struct platform_driver sunxi_nfc_driver = {
	.driver = {
		.name = "sunxi_nand",
		.of_match_table = sunxi_nfc_ids,
	},
	.probe = sunxi_nfc_probe,
	.remove = sunxi_nfc_remove,
};
module_platform_driver(sunxi_nfc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Boris BREZILLON");
MODULE_DESCRIPTION("Allwinner NAND Flash Controller driver");
MODULE_ALIAS("platform:sunxi_nand");

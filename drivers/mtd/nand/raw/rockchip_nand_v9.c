// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2019 RockChip, Inc.
 * Author: yifeng.zhao@rock-chips.com
 */

#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>

#define NANDC_V9_NUM_CHIPS	4
#define NANDC_V9_DEF_TIMEOUT	20000
#define NANDC_V9_READ		0
#define NANDC_V9_WRITE		1

#define NFC_SYS_DATA_SIZE	(4) /* 4 bytes sys data in oob pre 1024 data.*/

#define NANDC_REG_V9_FMCTL	0x00
#define NANDC_REG_V9_FMWAIT	0x04
#define NANDC_REG_V9_FLCTL	0x10
#define NANDC_REG_V9_BCHCTL	0x20
#define NANDC_REG_V9_DMA_CFG	0x30
#define NANDC_REG_V9_DMA_BUF0	0x34
#define NANDC_REG_V9_DMA_BUF1	0x38
#define NANDC_REG_V9_DMA_ST	0x40
#define NANDC_REG_V9_VER	0x80
#define NANDC_REG_V9_INTEN	0x120
#define NANDC_REG_V9_INTCLR	0x124
#define NANDC_REG_V9_INTST	0x128
#define NANDC_REG_V9_BCHST	0x150
#define NANDC_REG_V9_SPARE0	0x200
#define NANDC_REG_V9_SPARE1	0x204
#define NANDC_REG_V9_RANDMZ	0x208
#define NANDC_REG_V9_BANK0	0x800
#define NANDC_REG_V9_SRAM0	0x1000
#define NANDC_REG_V9_SRAM_SIZE	0x400

#define NANDC_REG_V9_DATA	0x00
#define NANDC_REG_V9_ADDR	0x04
#define NANDC_REG_V9_CMD	0x08

/* FMCTL */
#define NANDC_V9_FM_WP		BIT(8)
#define NANDC_V9_FM_CE_SEL_M	0xFF
#define NANDC_V9_FM_CE_SEL(x)	(1 << (x))
#define NANDC_V9_RDY		BIT(9)

/* FLCTL */
#define NANDC_V9_FL_RST		BIT(0)
#define NANDC_V9_FL_DIR_S	0x1
#define NANDC_V9_FL_XFER_START	BIT(2)
#define NANDC_V9_FL_XFER_EN	BIT(3)
#define NANDC_V9_FL_ST_BUF_S	0x4
#define NANDC_V9_FL_XFER_COUNT	BIT(5)
#define NANDC_V9_FL_ACORRECT	BIT(10)
#define NANDC_V9_FL_XFER_READY	BIT(20)

/* BCHCTL */
#define NAND_V9_BCH_MODE_S	25
#define NAND_V9_BCH_MODE_M	0x7

/* BCHST */
#define NANDC_V9_BCH0_ST_ERR	BIT(2)
#define NANDC_V9_BCH1_ST_ERR	BIT(18)
#define NANDC_V9_ECC_ERR_CNT0(x) (((x) & (0x7F << 3)) >> 3)
#define NANDC_V9_ECC_ERR_CNT1(x) (((x) & (0x7F << 19)) >> 19)

#define NANDC_V9_INT_DMA	BIT(0)

/*
 * NAND chip structure: stores NAND chip device related information
 *
 * @node:		used to store NAND chips into a list
 * @nand:		base NAND chip structure
 * @clk_rate:		clk_rate required for this NAND chip
 * @timing_cfg		TIMING_CFG register value for this NAND chip
 * @selected:		current active CS
 * @nsels:		number of CS lines required by the NAND chip
 * @sels:		array of CS lines descriptions
 */
struct rk_nand_chip {
	struct list_head node;
	struct nand_chip nand;
	unsigned long clk_rate;
	u32 timing_cfg;
	u16 metadata_size;
	int selected;
	int nsels;
	int sels[NANDC_V9_NUM_CHIPS];
};

static inline struct rk_nand_chip *to_rk_nand_chip(struct nand_chip *nand)
{
	return container_of(nand, struct rk_nand_chip, nand);
}

/*
 * NAND Controller structure: stores rk nand controller information
 *
 * @controller:		base controller structure
 * @dev:		parent device (used to print error messages)
 * @regs:		NAND controller registers
 * @hclk:		NAND Controller ahb clock
 * @clk:		NAND Controller interface clock
 * @gclk:		NAND Controller clock gate
 * @assigned_cs:	bitmask describing already assigned CS lines
 * @clk_rate:		NAND controller current clock rate
 * @complete:	a completion object used to wait for NAND
 *			controller events
 * @chips:		a list containing all the NAND chips attached to
 *			this NAND controller
 * @ecc_mode:	NAND Controller current ecc mode
 * @oob_buf:		temp buffer for oob read and write
 * @page_buf:		temp buffer for page read and write
 */
struct rk_nfc {
	struct nand_controller controller;
	struct device *dev;
	void __iomem *regs;
	struct clk *hclk;
	struct clk *clk;
	struct clk *gclk;
	unsigned long assigned_cs;
	unsigned long clk_rate;
	struct completion complete;
	struct list_head chips;
	int selected_cs;
	int ecc_mode;
	int max_ecc_strength;
	u32 *oob_buf;
	u32 *page_buf;
};

static inline struct rk_nfc *to_rk_nfc(struct nand_controller *ctrl)
{
	return container_of(ctrl, struct rk_nfc, controller);
}

static void rk_nfc_init(struct rk_nfc *nfc)
{
	writel(0, nfc->regs + NANDC_REG_V9_RANDMZ);
	writel(0, nfc->regs + NANDC_REG_V9_DMA_CFG);
	writel(NANDC_V9_FM_WP, nfc->regs + NANDC_REG_V9_FMCTL);
	writel(NANDC_V9_FL_RST, nfc->regs + NANDC_REG_V9_FLCTL);
	writel(0x1081, nfc->regs + NANDC_REG_V9_FMWAIT);
}

static irqreturn_t rk_nfc_interrupt(int irq, void *dev_id)
{
	struct rk_nfc *nfc = dev_id;
	u32 st = readl(nfc->regs + NANDC_REG_V9_INTST);
	u32 ien = readl(nfc->regs + NANDC_REG_V9_INTEN);

	if (!(ien & st))
		return IRQ_NONE;

	if ((ien & st) == ien)
		complete(&nfc->complete);

	writel(st, nfc->regs + NANDC_REG_V9_INTCLR);
	writel(~st & ien, nfc->regs + NANDC_REG_V9_INTEN);

	return IRQ_HANDLED;
}

static void rk_nfc_select_chip(struct mtd_info *mtd, int chipnr)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct rk_nand_chip *chip = to_rk_nand_chip(nand);
	struct rk_nfc *nfc = nand_get_controller_data(nand);
	u32 reg;
	int banknr;
	void __iomem *bank_base;

	if (chipnr > 0 && chipnr >= chip->nsels)
		return;

	if (chipnr == chip->selected)
		return;

	reg = readl(nfc->regs + NANDC_REG_V9_FMCTL);
	reg &= ~NANDC_V9_FM_CE_SEL_M;

	if (chipnr >= 0) {
		banknr = chip->sels[chipnr];
		bank_base = nfc->regs + NANDC_REG_V9_BANK0 + banknr * 0x100;

		nand->IO_ADDR_R = bank_base;
		nand->IO_ADDR_W = bank_base;

		reg |= NANDC_V9_FM_CE_SEL(banknr);
	} else {
		banknr = -1;
	}
	writel(reg, nfc->regs + NANDC_REG_V9_FMCTL);

	chip->selected = chipnr;
	nfc->selected_cs = banknr;
}

static void rk_nfc_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct rk_nfc *nfc = nand_get_controller_data(nand);
	u32 reg;
	void __iomem *bank_base = nfc->regs + NANDC_REG_V9_BANK0
				+ nfc->selected_cs * 0x100;

	if (ctrl & NAND_CTRL_CHANGE) {
		WARN_ON((ctrl & NAND_ALE) && (ctrl & NAND_CLE));
		if (ctrl & NAND_ALE)
			bank_base += NANDC_REG_V9_ADDR;
		else if (ctrl & NAND_CLE)
			bank_base += NANDC_REG_V9_CMD;
		nand->IO_ADDR_W = bank_base;

		reg = readl(nfc->regs + NANDC_REG_V9_FMCTL);
		reg &= ~NANDC_V9_FM_CE_SEL_M;
		if (ctrl & NAND_NCE)
			reg |= NANDC_V9_FM_CE_SEL(nfc->selected_cs);
		writel(reg, nfc->regs + NANDC_REG_V9_FMCTL);
	}

	if (dat != NAND_CMD_NONE)
		writeb(dat & 0xFF, nand->IO_ADDR_W);
}

static void rk_nfc_xfer_start(struct rk_nfc *nfc, u8 dir, u8 n_KB,
			      dma_addr_t dma_data, dma_addr_t dma_oob)
{
	u32 reg;

	reg = (1 << 0) | ((!dir) << 1) | (1 << 2) | (2 << 3) | (7 << 6) |
	      (16 << 9);
	writel(reg, nfc->regs + NANDC_REG_V9_DMA_CFG);
	writel((u32)dma_data, nfc->regs + NANDC_REG_V9_DMA_BUF0);
	writel((u32)dma_oob, nfc->regs + NANDC_REG_V9_DMA_BUF1);

	reg = (dir << 1) | (1 << 3) | (1 << 5) | (1 << 10) |
	      (n_KB << 22) | (1 << 29);
	writel(reg, nfc->regs + NANDC_REG_V9_FLCTL);
	reg |= (1 << 2);
	writel(reg, nfc->regs + NANDC_REG_V9_FLCTL);
}

static int rk_nand_wait_for_xfer_done(struct rk_nfc *nfc)
{
	u32 reg;
	int ret;
	void __iomem *ptr = nfc->regs + NANDC_REG_V9_FLCTL;

	ret = readl_poll_timeout_atomic(ptr, reg,
					reg & NANDC_V9_FL_XFER_READY,
					1, 10000);
	if (ret)
		pr_err("timeout reg=%x\n", reg);
	return ret;
}

static unsigned long rk_nand_dma_map_single(void *ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__dma_map_area((void *)ptr, size, dir);
	return ((unsigned long)virt_to_phys((void *)ptr));
#else
	return dma_map_single(NULL, (void *)ptr, size, dir);
#endif
}

static void rk_nand_dma_unmap_single(unsigned long ptr, int size, int dir)
{
#ifdef CONFIG_ARM64
	__dma_unmap_area(phys_to_virt(ptr), size, dir);
#else
	dma_unmap_single(NULL, (dma_addr_t)ptr, size, dir);
#endif
}

static int rk_nfc_hw_syndrome_ecc_read_page(struct mtd_info *mtd,
					    struct nand_chip *nand,
					    u8 *buf,
					    int oob_required, int page)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	int max_bitflips = 0;
	dma_addr_t dma_data, dma_oob;
	int ret, i;
	int bch_st;
	int dma_oob_size = ecc->steps * 128;

	nand_read_page_op(nand, page, 0, NULL, 0);

	dma_data = rk_nand_dma_map_single(nfc->page_buf, mtd->writesize,
					  DMA_FROM_DEVICE);
	dma_oob = rk_nand_dma_map_single(nfc->oob_buf, dma_oob_size,
					 DMA_FROM_DEVICE);

	init_completion(&nfc->complete);
	writel(NANDC_V9_INT_DMA, nfc->regs + NANDC_REG_V9_INTEN);
	rk_nfc_xfer_start(nfc, 0, ecc->steps, dma_data, dma_oob);
	wait_for_completion_timeout(&nfc->complete, msecs_to_jiffies(5));
	rk_nand_wait_for_xfer_done(nfc);
	rk_nand_dma_unmap_single(dma_data, mtd->writesize, DMA_FROM_DEVICE);
	rk_nand_dma_unmap_single(dma_oob, dma_oob_size, DMA_FROM_DEVICE);

	if (oob_required) {
		u8 *oob;
		u32 tmp;

		for (i = 0; i < ecc->steps; i++) {
			oob = nand->oob_poi + i * (ecc->bytes + 4);
			tmp = nfc->oob_buf[i];
			*oob++ = (u8)tmp;
			*oob++ = (u8)(tmp >> 8);
			*oob++ = (u8)(tmp >> 16);
			*oob++ = (u8)(tmp >> 24);
		}
	}

	for (i = 0; i < ecc->steps / 2; i++) {
		bch_st = readl(nfc->regs + NANDC_REG_V9_BCHST + i * 4);
		if (bch_st & NANDC_V9_BCH0_ST_ERR ||
		    bch_st & NANDC_V9_BCH1_ST_ERR) {
			mtd->ecc_stats.failed++;
			max_bitflips = -1;
		} else {
			ret = NANDC_V9_ECC_ERR_CNT0(bch_st);
			mtd->ecc_stats.corrected += ret;
			max_bitflips = max_t(unsigned int, max_bitflips, ret);

			ret = NANDC_V9_ECC_ERR_CNT1(bch_st);
			mtd->ecc_stats.corrected += ret;
			max_bitflips = max_t(unsigned int, max_bitflips, ret);
		}
	}
	memcpy(buf, nfc->page_buf, mtd->writesize);

	if (max_bitflips == -1) {
		dev_err(nfc->dev, "read_page %x %x %x %x %x %p %x\n",
			page, max_bitflips, bch_st, ((u32 *)buf)[0],
			((u32 *)buf)[1], buf, (u32)dma_data);
	}
	return max_bitflips;
}

static int rk_nfc_hw_syndrome_ecc_write_page(struct mtd_info *mtd,
					     struct nand_chip *nand,
					     const u8 *buf,
					     int oob_required, int page)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	dma_addr_t dma_data, dma_oob;
	int i;
	int dma_oob_size = ecc->steps * 64;

	nand_prog_page_begin_op(nand, page, 0, NULL, 0);

	for (i = 0; i < ecc->steps; i++) {
		u32 tmp;

		if (oob_required) {
			u8 *oob;

			oob = nand->oob_poi + i * (ecc->bytes + 4);
			tmp = oob[0] | (oob[1] << 8) | (oob[1] << 16) |
				(oob[1] << 24);
		} else {
			tmp = 0xFFFFFFFF;
		}
		nfc->oob_buf[i] = tmp;
	}

	memcpy(nfc->page_buf, buf, mtd->writesize);
	dma_data = rk_nand_dma_map_single((void *)nfc->page_buf,
					  mtd->writesize, DMA_TO_DEVICE);
	dma_oob = rk_nand_dma_map_single(nfc->oob_buf, dma_oob_size,
					 DMA_TO_DEVICE);
	init_completion(&nfc->complete);
	writel(NANDC_V9_INT_DMA, nfc->regs + NANDC_REG_V9_INTEN);
	rk_nfc_xfer_start(nfc, 1, ecc->steps, dma_data, dma_oob);
	wait_for_completion_timeout(&nfc->complete, msecs_to_jiffies(10));
	rk_nand_wait_for_xfer_done(nfc);
	rk_nand_dma_unmap_single(dma_data, mtd->writesize, DMA_TO_DEVICE);
	rk_nand_dma_unmap_single(dma_oob, dma_oob_size, DMA_TO_DEVICE);

	return nand_prog_page_end_op(nand);
}

static int rk_nfc_hw_ecc_read_oob(struct mtd_info *mtd,
				  struct nand_chip *nand,
				  int page)
{
	nand->cmdfunc(mtd, NAND_CMD_READ0, 0, page);

	nand->pagebuf = -1;

	return nand->ecc.read_page(mtd, nand, nand->data_buf, 1, page);
}

static int rk_nfc_hw_ecc_write_oob(struct mtd_info *mtd,
				   struct nand_chip *nand,
				   int page)
{
	int ret, status;

	nand->cmdfunc(mtd, NAND_CMD_SEQIN, 0, page);

	nand->pagebuf = -1;

	memset(nand->data_buf, 0xff, mtd->writesize);
	ret = nand->ecc.write_page(mtd, nand, nand->data_buf, 1, page);
	if (ret)
		return ret;

	nand->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);

	status = nand->waitfunc(mtd, nand);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

static void rk_nfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	int offs = 0;
	void __iomem *bank_base = nfc->regs + NANDC_REG_V9_BANK0
				+ nfc->selected_cs * 0x100;

	for (offs = 0; offs < len; offs++)
		buf[offs] = readl(bank_base);
}

static void rk_nfc_write_buf(struct mtd_info *mtd, const uint8_t *buf,
			     int len)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	int offs = 0;
	void __iomem *bank_base = nfc->regs + NANDC_REG_V9_BANK0
				+ nfc->selected_cs * 0x100;

	for (offs = 0; offs < len; offs++)
		writeb(buf[offs], bank_base);
}

static u8 rk_nfc_read_byte(struct mtd_info *mtd)
{
	u8 ret;

	rk_nfc_read_buf(mtd, &ret, 1);

	return ret;
}

static int rk_nfc_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rk_nand_chip *rknand = to_rk_nand_chip(chip);

	if (section)
		return -ERANGE;

	/*
	 * The beginning of the oob area stores the reserved data for the NFC,
	 * the size of the reserved data is NFC_SYS_DATA_SIZE bytes.
	 */
	oob_region->length = rknand->metadata_size - 2;
	oob_region->offset = NFC_SYS_DATA_SIZE + 2;

	return 0;
}

static int rk_nfc_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rk_nand_chip *rknand = to_rk_nand_chip(chip);

	if (section)
		return -ERANGE;

	oob_region->offset = rknand->metadata_size;
	oob_region->length = mtd->oobsize - oob_region->offset;

	return 0;
}

static const struct mtd_ooblayout_ops rk_nfc_ooblayout_ops = {
	.free = rk_nfc_ooblayout_free,
	.ecc = rk_nfc_ooblayout_ecc,
};

static int rk_nfc_hw_ecc_setup(struct mtd_info *mtd,
			       struct nand_ecc_ctrl *ecc,
			       uint32_t strength)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));

	u32 reg;

	ecc->strength = strength;
	ecc->bytes = DIV_ROUND_UP(ecc->strength * 14, 8);
	/* HW ECC always work with even numbers of ECC bytes */
	ecc->bytes = ALIGN(ecc->bytes, 2);

	switch (ecc->strength) {
	case 70:
		reg = 0x00000001;
		break;
	case 60:
		reg = 0x06000001;
		break;
	case 40:
		reg = 0x04000001;
		break;
	case 16:
		reg = 0x02000001;
		break;
	default:
		return -EINVAL;
	}
	writel(reg, nfc->regs + NANDC_REG_V9_BCHCTL);

	return 0;
}

static int rk_nfc_hw_ecc_ctrl_init(struct mtd_info *mtd,
				   struct nand_ecc_ctrl *ecc)
{
	static const u8 strengths[] = {70, 60, 40, 16};
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct rk_nfc *nfc = nand_get_controller_data(nand);
	struct rk_nand_chip *rknand = to_rk_nand_chip(nand);
	int max_strength;
	int i;
	int ver;

	ecc->size = 1024;
	ecc->prepad = 4;
	ecc->steps = mtd->writesize / ecc->size;
	rknand->metadata_size = NFC_SYS_DATA_SIZE * ecc->steps;
	max_strength = ((mtd->oobsize / ecc->steps) - NFC_SYS_DATA_SIZE) * 8 / 14;
	nfc->max_ecc_strength = 70;
	ver = readl(nfc->regs + NANDC_REG_V9_VER);
	if (ver != 0x56393030)
		dev_err(nfc->dev, "unsupported nfc version %x\n", ver);

	if (max_strength > nfc->max_ecc_strength)
		max_strength = nfc->max_ecc_strength;

	nfc->page_buf = kmalloc(mtd->writesize, GFP_KERNEL | GFP_DMA);
	if (!nfc->page_buf)
		return -ENOMEM;
	nfc->oob_buf = kmalloc(ecc->steps * 128, GFP_KERNEL | GFP_DMA);
	if (!nfc->oob_buf) {
		kfree(nfc->page_buf);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(strengths); i++)
		if (max_strength >= strengths[i])
			break;

	if (i >= ARRAY_SIZE(strengths)) {
		dev_err(nfc->dev, "unsupported strength\n");
		return -ENOTSUPP;
	}

	nfc->ecc_mode = strengths[i];
	rk_nfc_hw_ecc_setup(mtd, ecc, nfc->ecc_mode);

	return 0;
}

static int rk_nfc_block_bad(struct mtd_info *mtd, loff_t ofs)
{
	int page, res = 0;
	struct nand_chip *nand = mtd_to_nand(mtd);
	u16 bad = 0xff;
	u8 *data_buf = nand->data_buf;
	int chipnr = (int)(ofs >> nand->chip_shift);

	page = (int)(ofs >> nand->page_shift) & nand->pagemask;
	nand->select_chip(mtd, chipnr);
	nand->cmdfunc(mtd, NAND_CMD_READ0, 0x00, page);
	if (rk_nfc_hw_syndrome_ecc_read_page(mtd, nand, data_buf, 0,
					     page) == -1) {
		/* first page of the block*/
		nand->cmdfunc(mtd, NAND_CMD_READOOB, nand->badblockpos, page);
		bad = nand->read_byte(mtd);
		if (bad != 0xFF) {
			res = 1;
			goto out;
		}
		/* second page of the block*/
		nand->cmdfunc(mtd, NAND_CMD_READOOB, nand->badblockpos,
			      page + 1);
		bad = nand->read_byte(mtd);
		if (bad != 0xFF) {
			res = 1;
			goto out;
		}
		/* last page of the block */
		page += ((mtd->erasesize - mtd->writesize) >> nand->chip_shift);
		page--;
		nand->cmdfunc(mtd, NAND_CMD_READOOB, nand->badblockpos, page);
		bad = nand->read_byte(mtd);
		if (bad != 0xFF) {
			res = 1;
			goto out;
		}
	}
out:
	nand->select_chip(mtd, -1);

	return res;
}

static int rk_nfc_dev_ready(struct mtd_info *mtd)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	u32 reg;

	reg = readl(nfc->regs + NANDC_REG_V9_FMCTL);

	return (reg & NANDC_V9_RDY);
}

static int rk_nfc_setup_data_interface(struct mtd_info *mtd, int csline,
				       const struct nand_data_interface *conf)
{
	struct rk_nfc *nfc = nand_get_controller_data(mtd_to_nand(mtd));
	const struct nand_sdr_timings *timings;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return -ENOTSUPP;

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	writel(0x1081, nfc->regs + NANDC_REG_V9_FMWAIT);

	return 0;
}

static int rk_nand_attach_chip(struct nand_chip *nand)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct nand_ecc_ctrl *ecc = &nand->ecc;
	int ret;

	if (!ecc->size) {
		ecc->size = nand->ecc_step_ds;
		ecc->strength = nand->ecc_strength_ds;
	}

	if (!ecc->size || !ecc->strength)
		return -EINVAL;

	switch (ecc->mode) {
	case NAND_ECC_HW:
		ret = rk_nfc_hw_ecc_ctrl_init(mtd, ecc);
		if (ret)
			return ret;
		break;
	case NAND_ECC_NONE:
	case NAND_ECC_SOFT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct nand_controller_ops rk_nand_controller_ops = {
	.attach_chip = rk_nand_attach_chip,
};

static int rk_nand_chip_init(struct device *dev, struct rk_nfc *nfc,
			     struct device_node *np)
{
	struct rk_nand_chip *chip;
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

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->nsels = nsels;
	chip->selected = -1;

	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &tmp);
		if (ret) {
			dev_err(dev, "could not retrieve reg property: %d\n",
				ret);
			return ret;
		}

		if (tmp > NANDC_V9_NUM_CHIPS) {
			dev_err(dev,
				"invalid reg value: %u (max CS = 3)\n",
				tmp);
			return -EINVAL;
		}

		if (test_and_set_bit(tmp, &nfc->assigned_cs)) {
			dev_err(dev, "CS %d already assigned\n", tmp);
			return -EINVAL;
		}

		chip->sels[i] = tmp;
	}

	nand = &chip->nand;
	/* Default tR value specified in the ONFI spec (chapter 4.15.1) */
	nand->chip_delay = 200;
	nand->controller = &nfc->controller;
	nand->controller->ops = &rk_nand_controller_ops;

	nand_set_flash_node(nand, np);
	nand_set_controller_data(nand, nfc);

	nand->select_chip = rk_nfc_select_chip;
	nand->cmd_ctrl = rk_nfc_cmd_ctrl;
	nand->read_buf = rk_nfc_read_buf;
	nand->write_buf = rk_nfc_write_buf;
	nand->read_byte = rk_nfc_read_byte;
	nand->setup_data_interface = rk_nfc_setup_data_interface;
	nand->block_bad = rk_nfc_block_bad;
	nand->dev_ready = rk_nfc_dev_ready;
	nand->bbt_options = NAND_BBT_USE_FLASH | NAND_BBT_NO_OOB;
	nand->options = NAND_NO_SUBPAGE_WRITE | NAND_USE_BOUNCE_BUFFER;

	/* set default mode in case dt entry is missing */
	nand->ecc.mode = NAND_ECC_HW;

	nand->ecc.write_page = rk_nfc_hw_syndrome_ecc_write_page;
	nand->ecc.write_oob = rk_nfc_hw_ecc_write_oob;
	nand->ecc.read_page = rk_nfc_hw_syndrome_ecc_read_page;
	nand->ecc.read_oob = rk_nfc_hw_ecc_read_oob;

	mtd = nand_to_mtd(nand);
	mtd_set_ooblayout(mtd, &rk_nfc_ooblayout_ops);
	mtd->dev.parent = dev;
	mtd->name = "rk-nand";

	ret = nand_scan(nand, nsels);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "failed to register mtd device: %d\n", ret);
		nand_release(nand);
		return ret;
	}

	list_add_tail(&chip->node, &nfc->chips);

	return 0;
}

static int rk_nfc_chips_init(struct device *dev, struct rk_nfc *nfc)
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
		ret = rk_nand_chip_init(dev, nfc, nand_np);
		if (ret) {
			of_node_put(nand_np);
			return ret;
		}
	}

	return 0;
}

static void rk_nand_chips_cleanup(struct rk_nfc *nfc)
{
	struct rk_nand_chip *chip;

	while (!list_empty(&nfc->chips)) {
		chip = list_first_entry(&nfc->chips, struct rk_nand_chip,
					node);
		nand_release(&chip->nand);
		list_del(&chip->node);
	}
}

static int rk_nfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	struct rk_nfc *nfc;
	int irq;
	int ret;
	int clock_frequency;

	nfc = devm_kzalloc(dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->dev = dev;
	nand_controller_init(&nfc->controller);
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

	nfc->hclk = devm_clk_get(dev, "hclk_nandc");
	if (IS_ERR(nfc->hclk)) {
		dev_err(dev, "failed to retrieve hclk_nfc %p\n", nfc->hclk);
		return PTR_ERR(nfc->hclk);
	}

	ret = clk_prepare_enable(nfc->hclk);
	if (ret)
		return ret;

	nfc->clk = devm_clk_get(dev, "clk_nandc");
	if (IS_ERR(nfc->clk)) {
		dev_err(dev, "failed to retrieve nfc clk\n");
		ret = PTR_ERR(nfc->clk);
		goto out_ahb_clk_unprepare;
	}

	if (of_property_read_u32(dev->of_node, "clock-frequency",
				 &clock_frequency))
		clock_frequency = 150 * 1000 * 1000;
	clk_set_rate(nfc->clk, clock_frequency);

	ret = clk_prepare_enable(nfc->clk);
	if (ret)
		goto out_ahb_clk_unprepare;
	nfc->clk_rate = clk_get_rate(nfc->clk);

	nfc->gclk = devm_clk_get(&pdev->dev, "g_clk_nandc");
	if (!(IS_ERR(nfc->gclk)))
		clk_prepare_enable(nfc->gclk);

	rk_nfc_init(nfc);

	writel(0, nfc->regs + NANDC_REG_V9_INTEN);
	ret = devm_request_irq(dev, irq, rk_nfc_interrupt,
			       0, "rk-nand", nfc);
	if (ret)
		goto out_nfc_clk_unprepare;

	platform_set_drvdata(pdev, nfc);

	ret = rk_nfc_chips_init(dev, nfc);
	if (ret) {
		dev_err(dev, "failed to init nand chips\n");
		goto out_nfc_clk_unprepare;
	}
	return 0;

out_nfc_clk_unprepare:
	clk_disable_unprepare(nfc->clk);
out_ahb_clk_unprepare:
	clk_disable_unprepare(nfc->hclk);

	return ret;
}

static int rk_nfc_remove(struct platform_device *pdev)
{
	struct rk_nfc *nfc = platform_get_drvdata(pdev);

	rk_nand_chips_cleanup(nfc);
	kfree(nfc->page_buf);
	kfree(nfc->oob_buf);
	clk_disable_unprepare(nfc->clk);
	clk_disable_unprepare(nfc->hclk);
	if (!(IS_ERR(nfc->gclk)))
		clk_disable_unprepare(nfc->gclk);

	return 0;
}

static const struct of_device_id rk_nfc_ids[] = {
	{.compatible = "rockchip,rk-nandc-v9"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rk_nfc_ids);

static struct platform_driver rk_nfc_driver = {
	.driver = {
		.name = "rk-nand",
		.of_match_table = rk_nfc_ids,
	},
	.probe = rk_nfc_probe,
	.remove = rk_nfc_remove,
};
module_platform_driver(rk_nfc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yifeng Zhao <zyf@rock-chips.com>");
MODULE_DESCRIPTION("MTD NAND driver for Rockchip SoC");

/*
 * drivers/mtd/devices/tegra_nand.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
 *         Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Derived from: drivers/mtd/nand/nand_base.c
 *               drivers/mtd/nand/pxa3xx.c
 *
 * TODO:
 *      - Add support for 16bit bus width
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/slab.h>

#include <mach/nand.h>

#include "tegra_nand.h"

#define DRIVER_NAME	"tegra_nand"
#define DRIVER_DESC	"Nvidia Tegra NAND Flash Controller driver"

#define MAX_DMA_SZ			SZ_64K
#define ECC_BUF_SZ			SZ_1K

/* FIXME: is this right?!
 * NvRM code says it should be 128 bytes, but that seems awfully small
 */

/*#define TEGRA_NAND_DEBUG
#define TEGRA_NAND_DEBUG_PEDANTIC*/

#ifdef TEGRA_NAND_DEBUG
#define TEGRA_DBG(fmt, args...) \
	do { pr_info(fmt, ##args); } while (0)
#else
#define TEGRA_DBG(fmt, args...)
#endif

/* TODO: will vary with devices, move into appropriate device spcific header */
#define SCAN_TIMING_VAL		0x3f0bd214
#define SCAN_TIMING2_VAL	0xb

/* TODO: pull in the register defs (fields, masks, etc) from Nvidia files
 * so we don't have to redefine them */

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "cmdlinepart", NULL,  };
#endif

struct tegra_nand_chip {
	spinlock_t		lock;
	uint32_t		chipsize;
	int			num_chips;
	int			curr_chip;

	/* addr >> chip_shift == chip number */
	uint32_t		chip_shift;
	/* (addr >> page_shift) & page_mask == page number within chip */
	uint32_t		page_shift;
	uint32_t		page_mask;
	/* column within page */
	uint32_t		column_mask;
	/* addr >> block_shift == block number (across the whole mtd dev, not
	 * just a single chip. */
	uint32_t		block_shift;

	void			*priv;
};

struct tegra_nand_info {
	struct tegra_nand_chip		chip;
	struct mtd_info			mtd;
	struct tegra_nand_platform	*plat;
	struct device			*dev;
	struct mtd_partition		*parts;

	/* synchronizes access to accessing the actual NAND controller */
	struct mutex			lock;


	void				*oob_dma_buf;
	dma_addr_t			oob_dma_addr;
	/* ecc error vector info (offset into page and data mask to apply */
	void				*ecc_buf;
	dma_addr_t			ecc_addr;
	/* ecc error status (page number, err_cnt) */
	uint32_t			*ecc_errs;
	uint32_t			num_ecc_errs;
	uint32_t			max_ecc_errs;
	spinlock_t			ecc_lock;

	uint32_t			command_reg;
	uint32_t			config_reg;
	uint32_t			dmactrl_reg;

	struct completion		cmd_complete;
	struct completion		dma_complete;

	/* bad block bitmap: 1 == good, 0 == bad/unknown */
	unsigned long			*bb_bitmap;

	struct clk			*clk;
};
#define MTD_TO_INFO(mtd)	container_of((mtd), struct tegra_nand_info, mtd)

/* 64 byte oob block info for large page (== 2KB) device
 *
 * OOB flash layout for Tegra with Reed-Solomon 4 symbol correct ECC:
 *      Skipped bytes(4)
 *      Main area Ecc(36)
 *      Tag data(20)
 *      Tag data Ecc(4)
 *
 * Yaffs2 will use 16 tag bytes.
 */

static struct nand_ecclayout tegra_nand_oob_64 = {
	.eccbytes = 36,
	.eccpos = {
		4,  5,  6,  7,  8,  9,  10, 11, 12,
		13, 14, 15, 16, 17, 18, 19, 20, 21,
		22, 23, 24, 25, 26, 27, 28, 29, 30,
		31, 32, 33, 34, 35, 36, 37, 38, 39,
	},
	.oobavail = 20,
	.oobfree = {
		{ .offset = 40,
		  .length = 20,
		},
	},
};

static struct nand_flash_dev *
find_nand_flash_device(int dev_id)
{
	struct nand_flash_dev *dev = &nand_flash_ids[0];

	while (dev->name && dev->id != dev_id)
		dev++;
	return dev->name ? dev : NULL;
}

static struct nand_manufacturers *
find_nand_flash_vendor(int vendor_id)
{
	struct nand_manufacturers *vendor = &nand_manuf_ids[0];

	while (vendor->id && vendor->id != vendor_id)
		vendor++;
	return vendor->id ? vendor : NULL;
}

#define REG_NAME(name)			{ name, #name }
static struct {
	uint32_t addr;
	char *name;
} reg_names[] = {
	REG_NAME(COMMAND_REG),
	REG_NAME(STATUS_REG),
	REG_NAME(ISR_REG),
	REG_NAME(IER_REG),
	REG_NAME(CONFIG_REG),
	REG_NAME(TIMING_REG),
	REG_NAME(RESP_REG),
	REG_NAME(TIMING2_REG),
	REG_NAME(CMD_REG1),
	REG_NAME(CMD_REG2),
	REG_NAME(ADDR_REG1),
	REG_NAME(ADDR_REG2),
	REG_NAME(DMA_MST_CTRL_REG),
	REG_NAME(DMA_CFG_A_REG),
	REG_NAME(DMA_CFG_B_REG),
	REG_NAME(FIFO_CTRL_REG),
	REG_NAME(DATA_BLOCK_PTR_REG),
	REG_NAME(TAG_PTR_REG),
	REG_NAME(ECC_PTR_REG),
	REG_NAME(DEC_STATUS_REG),
	REG_NAME(HWSTATUS_CMD_REG),
	REG_NAME(HWSTATUS_MASK_REG),
	{ 0, NULL },
};
#undef REG_NAME


static int
dump_nand_regs(void)
{
	int i = 0;

	TEGRA_DBG("%s: dumping registers\n", __func__);
	while (reg_names[i].name != NULL) {
		TEGRA_DBG("%s = 0x%08x\n", reg_names[i].name, readl(reg_names[i].addr));
		i++;
	}
	TEGRA_DBG("%s: end of reg dump\n", __func__);
	return 1;
}


static inline void
enable_ints(struct tegra_nand_info *info, uint32_t mask)
{
	(void)info;
	writel(readl(IER_REG) | mask, IER_REG);
}


static inline void
disable_ints(struct tegra_nand_info *info, uint32_t mask)
{
	(void)info;
	writel(readl(IER_REG) & ~mask, IER_REG);
}


static inline void
split_addr(struct tegra_nand_info *info, loff_t offset, int *chipnr, uint32_t *page,
	   uint32_t *column)
{
	*chipnr = (int)(offset >> info->chip.chip_shift);
	*page = (offset >> info->chip.page_shift) & info->chip.page_mask;
	*column = offset & info->chip.column_mask;
}


static irqreturn_t
tegra_nand_irq(int irq, void *dev_id)
{
	struct tegra_nand_info *info = dev_id;
	uint32_t isr;
	uint32_t ier;
	uint32_t dma_ctrl;
	uint32_t tmp;

	isr = readl(ISR_REG);
	ier = readl(IER_REG);
	dma_ctrl = readl(DMA_MST_CTRL_REG);
#ifdef DEBUG_DUMP_IRQ
	pr_info("IRQ: ISR=0x%08x IER=0x%08x DMA_IS=%d DMA_IE=%d\n",
		isr, ier, !!(dma_ctrl & (1 << 20)), !!(dma_ctrl & (1 << 28)));
#endif
	if (isr & ISR_CMD_DONE) {
		if (likely(!(readl(COMMAND_REG) & COMMAND_GO)))
			complete(&info->cmd_complete);
		else
			pr_err("tegra_nand_irq: Spurious cmd done irq!\n");
	}

	if (isr & ISR_ECC_ERR) {
		/* always want to read the decode status so xfers don't stall. */
		tmp = readl(DEC_STATUS_REG);

		/* was ECC check actually enabled */
		if ((ier & IER_ECC_ERR)) {
			unsigned long flags;
			spin_lock_irqsave(&info->ecc_lock, flags);
			info->ecc_errs[info->num_ecc_errs++] = tmp;
			spin_unlock_irqrestore(&info->ecc_lock, flags);
		}
	}

	if ((dma_ctrl & DMA_CTRL_IS_DMA_DONE) &&
	    (dma_ctrl & DMA_CTRL_IE_DMA_DONE)) {
		complete(&info->dma_complete);
		writel(dma_ctrl, DMA_MST_CTRL_REG);
	}

	if ((isr & ISR_UND) && (ier & IER_UND))
		pr_err("%s: fifo underrun.\n", __func__);

	if ((isr & ISR_OVR) && (ier & IER_OVR))
		pr_err("%s: fifo overrun.\n", __func__);

	/* clear ALL interrupts?! */
	writel(isr & 0xfffc, ISR_REG);

	return IRQ_HANDLED;
}

static inline int
tegra_nand_is_cmd_done(struct tegra_nand_info *info)
{
	return (readl(COMMAND_REG) & COMMAND_GO) ? 0 : 1;
}

static int
tegra_nand_wait_cmd_done(struct tegra_nand_info *info)
{
	uint32_t timeout = (2 * HZ); /* TODO: make this realistic */
	int ret;

	ret = wait_for_completion_timeout(&info->cmd_complete, timeout);

#ifdef TEGRA_NAND_DEBUG_PEDANTIC
	BUG_ON(!ret && dump_nand_regs());
#endif

	return ret ? 0 : ret;
}

static inline void
select_chip(struct tegra_nand_info *info, int chipnr)
{
	BUG_ON(chipnr != -1 && chipnr >= info->plat->max_chips);
	info->chip.curr_chip = chipnr;
}

static void
cfg_hwstatus_mon(struct tegra_nand_info *info)
{
	uint32_t val;

	val = (HWSTATUS_RDSTATUS_MASK(1) |
	       HWSTATUS_RDSTATUS_EXP_VAL(0) |
	       HWSTATUS_RBSY_MASK(NAND_STATUS_READY) |
	       HWSTATUS_RBSY_EXP_VAL(NAND_STATUS_READY));
	writel(NAND_CMD_STATUS, HWSTATUS_CMD_REG);
	writel(val, HWSTATUS_MASK_REG);
}

/* Tells the NAND controller to initiate the command. */
static int
tegra_nand_go(struct tegra_nand_info *info)
{
	BUG_ON(!tegra_nand_is_cmd_done(info));

	INIT_COMPLETION(info->cmd_complete);
	writel(info->command_reg | COMMAND_GO, COMMAND_REG);

	if (unlikely(tegra_nand_wait_cmd_done(info))) {
		/* TODO: abort command if needed? */
		pr_err("%s: Timeout while waiting for command\n", __func__);
		return -ETIMEDOUT;
	}

	/* TODO: maybe wait for dma here? */
	return 0;
}

static void
tegra_nand_prep_readid(struct tegra_nand_info *info)
{
	info->command_reg = (COMMAND_CLE | COMMAND_ALE | COMMAND_PIO | COMMAND_RX  |
			     COMMAND_ALE_BYTE_SIZE(0) | COMMAND_TRANS_SIZE(3) |
			     (COMMAND_CE(info->chip.curr_chip)));
	writel(NAND_CMD_READID, CMD_REG1);
	writel(0, CMD_REG2);
	writel(0, ADDR_REG1);
	writel(0, ADDR_REG2);
	writel(0, CONFIG_REG);
}

static int
tegra_nand_cmd_readid(struct tegra_nand_info *info, uint32_t *chip_id)
{
	int err;

#ifdef TEGRA_NAND_DEBUG_PEDANTIC
	BUG_ON(info->chip.curr_chip == -1);
#endif

	tegra_nand_prep_readid(info);
	err = tegra_nand_go(info);
	if (err != 0)
		return err;

	*chip_id = readl(RESP_REG);
	return 0;
}


/* assumes right locks are held */
static int
nand_cmd_get_status(struct tegra_nand_info *info, uint32_t *status)
{
	int err;

	info->command_reg = (COMMAND_CLE | COMMAND_PIO | COMMAND_RX  |
			     COMMAND_RBSY_CHK | (COMMAND_CE(info->chip.curr_chip)));
	writel(NAND_CMD_STATUS, CMD_REG1);
	writel(0, CMD_REG2);
	writel(0, ADDR_REG1);
	writel(0, ADDR_REG2);
	writel(CONFIG_COM_BSY, CONFIG_REG);

	err = tegra_nand_go(info);
	if (err != 0)
		return err;

	*status = readl(RESP_REG) & 0xff;
	return 0;
}


/* must be called with lock held */
static int
check_block_isbad(struct mtd_info *mtd, loff_t offs)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	uint32_t block = offs >> info->chip.block_shift;
	int chipnr;
	uint32_t page;
	uint32_t column;
	int ret = 0;
	int i;

	if (info->bb_bitmap[BIT_WORD(block)] & BIT_MASK(block))
		return 0;

	offs &= ~(mtd->erasesize - 1);

	/* Only set COM_BSY. */
	/* TODO: should come from board file */
	writel(CONFIG_COM_BSY, CONFIG_REG);

	split_addr(info, offs, &chipnr, &page, &column);
	select_chip(info, chipnr);

	column = mtd->writesize & 0xffff; /* force to be the offset of OOB */

	/* check fist two pages of the block */
	for (i = 0; i < 2; ++i) {
		info->command_reg =
			COMMAND_CE(info->chip.curr_chip) | COMMAND_CLE | COMMAND_ALE |
			COMMAND_ALE_BYTE_SIZE(4) | COMMAND_RX | COMMAND_PIO |
			COMMAND_TRANS_SIZE(1) | COMMAND_A_VALID | COMMAND_RBSY_CHK |
			COMMAND_SEC_CMD;
		writel(NAND_CMD_READ0, CMD_REG1);
		writel(NAND_CMD_READSTART, CMD_REG2);

		writel(column | ((page & 0xffff) << 16), ADDR_REG1);
		writel((page >> 16) & 0xff, ADDR_REG2);

		/* ... poison me ... */
		writel(0xaa55aa55, RESP_REG);
		ret = tegra_nand_go(info);
		if (ret != 0) {
			pr_info("baaaaaad\n");
			goto out;
		}

		if ((readl(RESP_REG) & 0xffff) != 0xffff) {
			ret = 1;
			goto out;
		}

		/* Note: The assumption here is that we cannot cross chip
		 * boundary since the we are only looking at the first 2 pages in
		 * a block, i.e. erasesize > writesize ALWAYS */
		page++;
	}

out:
	/* update the bitmap if the block is good */
	if (ret == 0)
		set_bit(block, info->bb_bitmap);
	return ret;
}


static int
tegra_nand_block_isbad(struct mtd_info *mtd, loff_t offs)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	int ret;

	if (offs >= mtd->size)
		return -EINVAL;

	mutex_lock(&info->lock);
	ret = check_block_isbad(mtd, offs);
	mutex_unlock(&info->lock);

#if 0
	if (ret > 0)
		pr_info("block @ 0x%llx is bad.\n", offs);
	else if (ret < 0)
		pr_err("error checking block @ 0x%llx for badness.\n", offs);
#endif

	return ret;
}


static int
tegra_nand_block_markbad(struct mtd_info *mtd, loff_t offs)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	uint32_t block = offs >> info->chip.block_shift;
	int chipnr;
	uint32_t page;
	uint32_t column;
	int ret = 0;
	int i;

	if (offs >= mtd->size)
		return -EINVAL;

	pr_info("tegra_nand: setting block %d bad\n", block);

	mutex_lock(&info->lock);
	offs &= ~(mtd->erasesize - 1);

	/* mark the block bad in our bitmap */
	clear_bit(block, info->bb_bitmap);
	mtd->ecc_stats.badblocks++;

	/* Only set COM_BSY. */
	/* TODO: should come from board file */
	writel(CONFIG_COM_BSY, CONFIG_REG);

	split_addr(info, offs, &chipnr, &page, &column);
	select_chip(info, chipnr);

	column = mtd->writesize & 0xffff; /* force to be the offset of OOB */

	/* write to fist two pages in the block */
	for (i = 0; i < 2; ++i) {
		info->command_reg =
			COMMAND_CE(info->chip.curr_chip) | COMMAND_CLE | COMMAND_ALE |
			COMMAND_ALE_BYTE_SIZE(4) | COMMAND_TX | COMMAND_PIO |
			COMMAND_TRANS_SIZE(1) | COMMAND_A_VALID | COMMAND_RBSY_CHK |
			COMMAND_AFT_DAT | COMMAND_SEC_CMD;
		writel(NAND_CMD_SEQIN, CMD_REG1);
		writel(NAND_CMD_PAGEPROG, CMD_REG2);

		writel(column | ((page & 0xffff) << 16), ADDR_REG1);
		writel((page >> 16) & 0xff, ADDR_REG2);

		writel(0x0, RESP_REG);
		ret = tegra_nand_go(info);
		if (ret != 0)
			goto out;

		/* TODO: check if the program op worked? */
		page++;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}


static int
tegra_nand_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	uint32_t num_blocks;
	uint32_t offs;
	int chipnr;
	uint32_t page;
	uint32_t column;
	uint32_t status = 0;

	TEGRA_DBG("tegra_nand_erase: addr=0x%08llx len=%lld\n", instr->addr,
	       instr->len);

	if ((instr->addr + instr->len) > mtd->size) {
		pr_err("tegra_nand_erase: Can't erase past end of device\n");
		instr->state = MTD_ERASE_FAILED;
		return -EINVAL;
	}

	if (instr->addr & (mtd->erasesize - 1)) {
		pr_err("tegra_nand_erase: addr=0x%08llx not block-aligned\n",
		       instr->addr);
		instr->state = MTD_ERASE_FAILED;
		return -EINVAL;
	}

	if (instr->len & (mtd->erasesize - 1)) {
		pr_err("tegra_nand_erase: len=%lld not block-aligned\n",
		       instr->len);
		instr->state = MTD_ERASE_FAILED;
		return -EINVAL;
	}

	instr->fail_addr = 0xffffffff;

	mutex_lock(&info->lock);

	instr->state = MTD_ERASING;

	offs = instr->addr;
	num_blocks = instr->len >> info->chip.block_shift;

	select_chip(info, -1);

	while (num_blocks--) {
		split_addr(info, offs, &chipnr, &page, &column);
		if (chipnr != info->chip.curr_chip)
			select_chip(info, chipnr);
		TEGRA_DBG("tegra_nand_erase: addr=0x%08x, page=0x%08x\n", offs, page);

		if (check_block_isbad(mtd, offs)) {
			pr_info("%s: skipping bad block @ 0x%08x\n", __func__, offs);
			goto next_block;
		}

		info->command_reg =
			COMMAND_CE(info->chip.curr_chip) | COMMAND_CLE | COMMAND_ALE |
			COMMAND_ALE_BYTE_SIZE(2) | COMMAND_RBSY_CHK | COMMAND_SEC_CMD;
		writel(NAND_CMD_ERASE1, CMD_REG1);
		writel(NAND_CMD_ERASE2, CMD_REG2);

		writel(page & 0xffffff, ADDR_REG1);
		writel(0, ADDR_REG2);
		writel(CONFIG_COM_BSY, CONFIG_REG);

		if (tegra_nand_go(info) != 0) {
			instr->fail_addr = offs;
			goto out_err;
		}

		/* TODO: do we want a timeout here? */
		if ((nand_cmd_get_status(info, &status) != 0) ||
		    (status & NAND_STATUS_FAIL) ||
		    ((status & NAND_STATUS_READY) != NAND_STATUS_READY)) {
			instr->fail_addr = offs;
			pr_info("%s: erase failed @ 0x%08x (stat=0x%08x)\n",
				__func__, offs, status);
			goto out_err;
		}
next_block:
		offs += mtd->erasesize;
	}

	instr->state = MTD_ERASE_DONE;
	mutex_unlock(&info->lock);
	mtd_erase_callback(instr);
	return 0;

out_err:
	instr->state = MTD_ERASE_FAILED;
	mutex_unlock(&info->lock);
	return -EIO;
}


static inline void
dump_mtd_oob_ops(struct mtd_oob_ops *ops)
{
	pr_info("%s: oob_ops: mode=%s len=0x%x ooblen=0x%x "
		"ooboffs=0x%x dat=0x%p oob=0x%p\n", __func__,
		(ops->mode == MTD_OOB_AUTO ? "MTD_OOB_AUTO" :
		 (ops->mode == MTD_OOB_PLACE ? "MTD_OOB_PLACE" : "MTD_OOB_RAW")),
		ops->len, ops->ooblen, ops->ooboffs, ops->datbuf, ops->oobbuf);
}

static int
tegra_nand_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int ret;

	pr_debug("%s: read: from=0x%llx len=0x%x\n", __func__, from, len);
	ops.mode = MTD_OOB_AUTO;
	ops.len = len;
	ops.datbuf = buf;
	ops.oobbuf = NULL;
	ret = mtd->read_oob(mtd, from, &ops);
	*retlen = ops.retlen;
	return ret;
}

static void
correct_ecc_errors_on_blank_page(struct tegra_nand_info *info, u8 *datbuf, u8 *oobbuf, unsigned int a_len, unsigned int b_len) {
	int i;
	int all_ff = 1;
	unsigned long flags;

	spin_lock_irqsave(&info->ecc_lock, flags);
	if (info->num_ecc_errs) {
		if (datbuf) {
			for (i = 0; i < a_len; i++)
				if (datbuf[i] != 0xFF)
					all_ff = 0;
		}
		if (oobbuf) {
			for (i = 0; i < b_len; i++)
				if (oobbuf[i] != 0xFF)
					all_ff = 0;
		}
		if (all_ff)
			info->num_ecc_errs = 0;
	}
	spin_unlock_irqrestore(&info->ecc_lock, flags);
}

static void
update_ecc_counts(struct tegra_nand_info *info, int check_oob)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&info->ecc_lock, flags);
	for (i = 0; i < info->num_ecc_errs; ++i) {
		/* correctable */
		info->mtd.ecc_stats.corrected +=
			DEC_STATUS_ERR_CNT(info->ecc_errs[i]);

		/* uncorrectable */
		if (info->ecc_errs[i] & DEC_STATUS_ECC_FAIL_A)
			info->mtd.ecc_stats.failed++;
		if (check_oob && (info->ecc_errs[i] & DEC_STATUS_ECC_FAIL_B))
			info->mtd.ecc_stats.failed++;
	}
	info->num_ecc_errs = 0;
	spin_unlock_irqrestore(&info->ecc_lock, flags);
}

static inline void
clear_regs(struct tegra_nand_info *info)
{
	info->command_reg = 0;
	info->config_reg = 0;
	info->dmactrl_reg = 0;
}

static void
prep_transfer_dma(struct tegra_nand_info *info, int rx, int do_ecc, uint32_t page,
		  uint32_t column, dma_addr_t data_dma,
		  uint32_t data_len, dma_addr_t oob_dma, uint32_t oob_len)
{
	uint32_t tag_sz = oob_len;

#if 0
	pr_info("%s: rx=%d ecc=%d  page=%d col=%d data_dma=0x%x "
		"data_len=0x%08x oob_dma=0x%x ooblen=%d\n", __func__,
		rx, do_ecc, page, column, data_dma, data_len, oob_dma,
		oob_len);
#endif

	info->command_reg =
		COMMAND_CE(info->chip.curr_chip) | COMMAND_CLE | COMMAND_ALE |
		COMMAND_ALE_BYTE_SIZE(4) | COMMAND_SEC_CMD | COMMAND_RBSY_CHK |
		COMMAND_TRANS_SIZE(8);

	info->config_reg = (CONFIG_PAGE_SIZE_SEL(3) | CONFIG_PIPELINE_EN |
			    CONFIG_COM_BSY);

	info->dmactrl_reg = (DMA_CTRL_DMA_GO |
			     DMA_CTRL_DMA_PERF_EN | DMA_CTRL_IE_DMA_DONE |
			     DMA_CTRL_IS_DMA_DONE | DMA_CTRL_BURST_SIZE(4));

	if (rx) {
		if (do_ecc)
			info->config_reg |= CONFIG_HW_ERR_CORRECTION;
		info->command_reg |= COMMAND_RX;
		info->dmactrl_reg |= DMA_CTRL_REUSE_BUFFER;
		writel(NAND_CMD_READ0, CMD_REG1);
		writel(NAND_CMD_READSTART, CMD_REG2);
	} else {
		info->command_reg |= (COMMAND_TX | COMMAND_AFT_DAT);
		info->dmactrl_reg |= DMA_CTRL_DIR; /* DMA_RD == TX */
		writel(NAND_CMD_SEQIN, CMD_REG1);
		writel(NAND_CMD_PAGEPROG, CMD_REG2);
	}

	if (data_len) {
		if (do_ecc)
			info->config_reg |=
				CONFIG_HW_ECC | CONFIG_ECC_SEL | CONFIG_TVALUE(0) |
				CONFIG_SKIP_SPARE | CONFIG_SKIP_SPARE_SEL(0);
		info->command_reg |= COMMAND_A_VALID;
		info->dmactrl_reg |= DMA_CTRL_DMA_EN_A;
		writel(DMA_CFG_BLOCK_SIZE(data_len - 1), DMA_CFG_A_REG);
		writel(data_dma, DATA_BLOCK_PTR_REG);
	} else {
		column = info->mtd.writesize;
		if (do_ecc)
			column += info->mtd.ecclayout->oobfree[0].offset;
		writel(0, DMA_CFG_A_REG);
		writel(0, DATA_BLOCK_PTR_REG);
	}

	if (oob_len) {
		oob_len = info->mtd.oobavail;
		tag_sz = info->mtd.oobavail;
		if (do_ecc) {
			tag_sz += 4; /* size of tag ecc */
			if (rx)
				oob_len += 4; /* size of tag ecc */
			info->config_reg |= CONFIG_ECC_EN_TAG;
		}
		if (data_len && rx)
			oob_len += 4; /* num of skipped bytes */

		info->command_reg |= COMMAND_B_VALID;
		info->config_reg |= CONFIG_TAG_BYTE_SIZE(tag_sz - 1);
		info->dmactrl_reg |= DMA_CTRL_DMA_EN_B;
		writel(DMA_CFG_BLOCK_SIZE(oob_len - 1), DMA_CFG_B_REG);
		writel(oob_dma, TAG_PTR_REG);
	} else {
		writel(0, DMA_CFG_B_REG);
		writel(0, TAG_PTR_REG);
	}

	writel((column & 0xffff) | ((page & 0xffff) << 16), ADDR_REG1);
	writel((page >> 16) & 0xff, ADDR_REG2);
}

static dma_addr_t
tegra_nand_dma_map(struct device *dev, void *addr, size_t size,
		 enum dma_data_direction dir)
{
	struct page *page;
	unsigned long offset = (unsigned long)addr & ~PAGE_MASK;
	if (virt_addr_valid(addr))
		page = virt_to_page(addr);
	else {
		if (WARN_ON(size + offset > PAGE_SIZE))
			return ~0;
		page = vmalloc_to_page(addr);
	}
	return dma_map_page(dev, page, offset, size, dir);
}

/* if mode == RAW, then we read data only, with no ECC
 * if mode == PLACE, we read ONLY the OOB data from a raw offset into the spare
 * area (ooboffs).
 * if mode == AUTO, we read main data and the OOB data from the oobfree areas as
 * specified by nand_ecclayout.
 */
static int
do_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	struct mtd_ecc_stats old_ecc_stats;
	int chipnr;
	uint32_t page;
	uint32_t column;
	uint8_t *datbuf = ops->datbuf;
	uint8_t *oobbuf = ops->oobbuf;
	uint32_t len = datbuf ? ops->len : 0;
	uint32_t ooblen = oobbuf ? ops->ooblen : 0;
	uint32_t oobsz;
	uint32_t page_count;
	int err;
	int do_ecc = 1;
	dma_addr_t datbuf_dma_addr = 0;

#if 0
	dump_mtd_oob_ops(mtd, ops);
#endif

	ops->retlen = 0;
	ops->oobretlen = 0;

	/* TODO: Worry about reads from non-page boundaries later */
	if (unlikely(from & info->chip.column_mask)) {
		pr_err("%s: Unaligned read (from 0x%llx) not supported\n",
		       __func__, from);
		return -EINVAL;
	}

	if (likely(ops->mode == MTD_OOB_AUTO)) {
		oobsz = mtd->oobavail;
	} else {
		oobsz = mtd->oobsize;
		do_ecc = 0;
	}

	if (unlikely(ops->oobbuf && ops->ooblen > oobsz)) {
		pr_err("%s: can't read OOB from multiple pages (%d > %d)\n", __func__,
		       ops->ooblen, oobsz);
		return -EINVAL;
	} else if (ops->oobbuf) {
		page_count = 1;
	} else {
		page_count = max((uint32_t)(ops->len / mtd->writesize), (uint32_t)1);
	}

	mutex_lock(&info->lock);

	memcpy(&old_ecc_stats, &mtd->ecc_stats, sizeof(old_ecc_stats));

	if (do_ecc) {
		enable_ints(info, IER_ECC_ERR);
		writel(info->ecc_addr, ECC_PTR_REG);
	} else
		disable_ints(info, IER_ECC_ERR);

	split_addr(info, from, &chipnr, &page, &column);
	select_chip(info, chipnr);

	/* reset it to point back to beginning of page */
	from -= column;

	while (page_count--) {
		int a_len = min(mtd->writesize - column, len);
		int b_len = min(oobsz, ooblen);

#if 0
		pr_info("%s: chip:=%d page=%d col=%d\n", __func__, chipnr,
			page, column);
#endif

		clear_regs(info);
		if (datbuf)
			datbuf_dma_addr = tegra_nand_dma_map(info->dev, datbuf, a_len, DMA_FROM_DEVICE);

		prep_transfer_dma(info, 1, do_ecc, page, column, datbuf_dma_addr,
				  a_len, info->oob_dma_addr,
				  b_len);
		writel(info->config_reg, CONFIG_REG);
		writel(info->dmactrl_reg, DMA_MST_CTRL_REG);

		INIT_COMPLETION(info->dma_complete);
		err = tegra_nand_go(info);
		if (err != 0)
			goto out_err;

		if (!wait_for_completion_timeout(&info->dma_complete, 2*HZ)) {
			pr_err("%s: dma completion timeout\n", __func__);
			dump_nand_regs();
			err = -ETIMEDOUT;
			goto out_err;
		}

		/*pr_info("tegra_read_oob: DMA complete\n");*/

		/* if we are here, transfer is done */
		if (datbuf)
			dma_unmap_page(info->dev, datbuf_dma_addr, a_len, DMA_FROM_DEVICE);

		if (oobbuf) {
			uint32_t ofs = datbuf && oobbuf ? 4 : 0; /* skipped bytes */
			memcpy(oobbuf, info->oob_dma_buf + ofs, b_len);
		}

		correct_ecc_errors_on_blank_page(info, datbuf, oobbuf, a_len, b_len);

		if (datbuf) {
			len -= a_len;
			datbuf += a_len;
			ops->retlen += a_len;
		}

		if (oobbuf) {
			ooblen -= b_len;
			oobbuf += b_len;
			ops->oobretlen += b_len;
		}

		update_ecc_counts(info, oobbuf != NULL);

		if (!page_count)
			break;

		from += mtd->writesize;
		column = 0;

		split_addr(info, from, &chipnr, &page, &column);
		if (chipnr != info->chip.curr_chip)
			select_chip(info, chipnr);
	}

	disable_ints(info, IER_ECC_ERR);

	if (mtd->ecc_stats.failed != old_ecc_stats.failed)
		err = -EBADMSG;
	else if (mtd->ecc_stats.corrected != old_ecc_stats.corrected)
		err = -EUCLEAN;
	else
		err = 0;

	mutex_unlock(&info->lock);
	return err;

out_err:
	ops->retlen = 0;
	ops->oobretlen = 0;

	disable_ints(info, IER_ECC_ERR);
	mutex_unlock(&info->lock);
	return err;
}

/* just does some parameter checking and calls do_read_oob */
static int
tegra_nand_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	if (ops->datbuf && unlikely((from + ops->len) > mtd->size)) {
		pr_err("%s: Can't read past end of device.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(ops->oobbuf && !ops->ooblen)) {
		pr_err("%s: Reading 0 bytes from OOB is meaningless\n", __func__);
		return -EINVAL;
	}

	if (unlikely(ops->mode != MTD_OOB_AUTO)) {
		if (ops->oobbuf && ops->datbuf) {
			pr_err("%s: can't read OOB + Data in non-AUTO mode.\n",
			       __func__);
			return -EINVAL;
		}
		if ((ops->mode == MTD_OOB_RAW) && !ops->datbuf) {
			pr_err("%s: Raw mode only supports reading data area.\n",
			       __func__);
			return -EINVAL;
		}
	}

	return do_read_oob(mtd, from, ops);
}

static int
tegra_nand_write(struct mtd_info *mtd, loff_t to, size_t len,
		 size_t *retlen, const uint8_t *buf)
{
	struct mtd_oob_ops ops;
	int ret;

	pr_debug("%s: write: to=0x%llx len=0x%x\n", __func__, to, len);
	ops.mode = MTD_OOB_AUTO;
	ops.len = len;
	ops.datbuf = (uint8_t *)buf;
	ops.oobbuf = NULL;
	ret = mtd->write_oob(mtd, to, &ops);
	*retlen = ops.retlen;
	return ret;
}

static int
do_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	int chipnr;
	uint32_t page;
	uint32_t column;
	uint8_t *datbuf = ops->datbuf;
	uint8_t *oobbuf = ops->oobbuf;
	uint32_t len = datbuf ? ops->len : 0;
	uint32_t ooblen = oobbuf ? ops->ooblen : 0;
	uint32_t oobsz;
	uint32_t page_count;
	int err;
	int do_ecc = 1;
	dma_addr_t datbuf_dma_addr = 0;

#if 0
	dump_mtd_oob_ops(mtd, ops);
#endif

	ops->retlen = 0;
	ops->oobretlen = 0;

	if (!ops->len)
		return 0;


	if (likely(ops->mode == MTD_OOB_AUTO)) {
		oobsz = mtd->oobavail;
	} else {
		oobsz = mtd->oobsize;
		do_ecc = 0;
	}

	if (unlikely(ops->oobbuf && ops->ooblen > oobsz)) {
			pr_err("%s: can't write OOB to multiple pages (%d > %d)\n",
			       __func__, ops->ooblen, oobsz);
			return -EINVAL;
	} else if (ops->oobbuf) {
		page_count = 1;
	} else
		page_count = max((uint32_t)(ops->len / mtd->writesize), (uint32_t)1);

	mutex_lock(&info->lock);

	split_addr(info, to, &chipnr, &page, &column);
	select_chip(info, chipnr);

	while (page_count--) {
		int a_len = min(mtd->writesize, len);
		int b_len = min(oobsz, ooblen);

		if (datbuf)
			datbuf_dma_addr = tegra_nand_dma_map(info->dev, datbuf, a_len, DMA_TO_DEVICE);
		if (oobbuf)
			memcpy(info->oob_dma_buf, oobbuf, b_len);

		clear_regs(info);
		prep_transfer_dma(info, 0, do_ecc, page, column, datbuf_dma_addr,
				  a_len, info->oob_dma_addr, b_len);

		writel(info->config_reg, CONFIG_REG);
		writel(info->dmactrl_reg, DMA_MST_CTRL_REG);

		INIT_COMPLETION(info->dma_complete);
		err = tegra_nand_go(info);
		if (err != 0)
			goto out_err;

		if (!wait_for_completion_timeout(&info->dma_complete, 2*HZ)) {
			pr_err("%s: dma completion timeout\n", __func__);
			dump_nand_regs();
			goto out_err;
		}

		if (datbuf) {
			dma_unmap_page(info->dev, datbuf_dma_addr, a_len, DMA_TO_DEVICE);
			len -= a_len;
			datbuf += a_len;
			ops->retlen += a_len;
		}
		if (oobbuf) {
			ooblen -= b_len;
			oobbuf += b_len;
			ops->oobretlen += b_len;
		}

		if (!page_count)
			break;

		to += mtd->writesize;
		column = 0;

		split_addr(info, to, &chipnr, &page, &column);
		if (chipnr != info->chip.curr_chip)
			select_chip(info, chipnr);
	}

	mutex_unlock(&info->lock);
	return err;

out_err:
	ops->retlen = 0;
	ops->oobretlen = 0;

	mutex_unlock(&info->lock);
	return err;
}

static int
tegra_nand_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);

	if (unlikely(to & info->chip.column_mask)) {
		pr_err("%s: Unaligned write (to 0x%llx) not supported\n",
		       __func__, to);
		return -EINVAL;
	}

	if (unlikely(ops->oobbuf && !ops->ooblen)) {
		pr_err("%s: Writing 0 bytes to OOB is meaningless\n", __func__);
		return -EINVAL;
	}

	return do_write_oob(mtd, to, ops);
}

static int
tegra_nand_suspend(struct mtd_info *mtd)
{
	return 0;
}

static void
tegra_nand_resume(struct mtd_info *mtd)
{
}

static int
scan_bad_blocks(struct tegra_nand_info *info)
{
	struct mtd_info *mtd = &info->mtd;
	int num_blocks = mtd->size >> info->chip.block_shift;
	uint32_t block;
	int is_bad = 0;

	for (block = 0; block < num_blocks; ++block) {
		/* make sure the bit is cleared, meaning it's bad/unknown before
		 * we check. */
		clear_bit(block, info->bb_bitmap);
		is_bad = mtd->block_isbad(mtd, block << info->chip.block_shift);

		if (is_bad == 0)
			set_bit(block, info->bb_bitmap);
		else if (is_bad > 0)
			pr_info("block 0x%08x is bad.\n", block);
		else {
			pr_err("Fatal error (%d) while scanning for "
			       "bad blocks\n", is_bad);
			return is_bad;
		}
	}
	return 0;
}

static void
set_chip_timing(struct tegra_nand_info *info)
{
	struct tegra_nand_chip_parms *chip_parms = &info->plat->chip_parms[0];
	uint32_t tmp;

	/* TODO: Actually search the chip_parms list for the correct device. */
	/* TODO: Get the appropriate frequency from the clock subsystem */
#define NAND_CLK_FREQ	108000
#define CNT(t)		(((((t) * NAND_CLK_FREQ) + 1000000 - 1) / 1000000) - 1)
	tmp = (TIMING_TRP_RESP(CNT(chip_parms->timing.trp_resp)) |
	       TIMING_TWB(CNT(chip_parms->timing.twb)) |
	       TIMING_TCR_TAR_TRR(CNT(chip_parms->timing.tcr_tar_trr)) |
	       TIMING_TWHR(CNT(chip_parms->timing.twhr)) |
	       TIMING_TCS(CNT(chip_parms->timing.tcs)) |
	       TIMING_TWH(CNT(chip_parms->timing.twh)) |
	       TIMING_TWP(CNT(chip_parms->timing.twp)) |
	       TIMING_TRH(CNT(chip_parms->timing.trh)) |
	       TIMING_TRP(CNT(chip_parms->timing.trp)));
	writel(tmp, TIMING_REG);
	writel(TIMING2_TADL(CNT(chip_parms->timing.tadl)), TIMING2_REG);
#undef CNT
#undef NAND_CLK_FREQ
}

/* Scans for nand flash devices, identifies them, and fills in the
 * device info. */
static int
tegra_nand_scan(struct mtd_info *mtd, int maxchips)
{
	struct tegra_nand_info *info = MTD_TO_INFO(mtd);
	struct nand_flash_dev *dev_info;
	struct nand_manufacturers *vendor_info;
	uint32_t tmp;
	uint32_t dev_id;
	uint32_t vendor_id;
	uint32_t dev_parms;
	uint32_t mlc_parms;
	int cnt;
	int err = 0;

	writel(SCAN_TIMING_VAL, TIMING_REG);
	writel(SCAN_TIMING2_VAL, TIMING2_REG);
	writel(0, CONFIG_REG);

	select_chip(info, 0);
	err = tegra_nand_cmd_readid(info, &tmp);
	if (err != 0)
		goto out_error;

	vendor_id = tmp & 0xff;
	dev_id = (tmp >> 8) & 0xff;
	mlc_parms = (tmp >> 16) & 0xff;
	dev_parms = (tmp >> 24) & 0xff;

	dev_info = find_nand_flash_device(dev_id);
	if (dev_info == NULL) {
		pr_err("%s: unknown flash device id (0x%02x) found.\n", __func__,
			dev_id);
		err = -ENODEV;
		goto out_error;
	}

	vendor_info = find_nand_flash_vendor(vendor_id);
	if (vendor_info == NULL) {
		pr_err("%s: unknown flash vendor id (0x%02x) found.\n", __func__,
			vendor_id);
		err = -ENODEV;
		goto out_error;
	}

	/* loop through and see if we can find more devices */
	for (cnt = 1; cnt < info->plat->max_chips; ++cnt) {
		select_chip(info, cnt);
		/* TODO: figure out what to do about errors here */
		err = tegra_nand_cmd_readid(info, &tmp);
		if (err != 0)
			goto out_error;
		if ((dev_id != ((tmp >> 8) & 0xff)) ||
		    (vendor_id != (tmp & 0xff)))
			break;
	}

	pr_info("%s: %d NAND chip(s) found (vend=0x%02x, dev=0x%02x) (%s %s)\n",
	       DRIVER_NAME, cnt, vendor_id, dev_id, vendor_info->name,
	       dev_info->name);
	info->chip.num_chips = cnt;
	info->chip.chipsize = dev_info->chipsize << 20;
	mtd->size = info->chip.num_chips * info->chip.chipsize;

	/* format of 4th id byte returned by READ ID
	 *   bit     7 = rsvd
	 *   bit     6 = bus width. 1 == 16bit, 0 == 8bit
	 *   bits  5:4 = data block size. 64kb * (2^val)
	 *   bit     3 = rsvd
	 *   bit     2 = spare area size / 512 bytes. 0 == 8bytes, 1 == 16bytes
	 *   bits  1:0 = page size. 1kb * (2^val)
	 */

	/* TODO: we should reconcile the information read from chip and
	 *       the data given to us in tegra_nand_platform->chip_parms??
	 *       platform data will give us timing information. */

	/* page_size */
	tmp = dev_parms & 0x3;
	mtd->writesize = 1024 << tmp;
	info->chip.column_mask = mtd->writesize - 1;

	/* Note: See oob layout description of why we only support 2k pages. */
	if (mtd->writesize > 2048) {
		pr_err("%s: Large page devices with pagesize > 2kb are NOT "
		       "supported\n", __func__);
		goto out_error;
	} else if (mtd->writesize < 2048) {
		pr_err("%s: Small page devices are NOT supported\n", __func__);
		goto out_error;
	}

	/* spare area, must be at least 64 bytes */
	tmp = (dev_parms >> 2) & 0x1;
	tmp = (8 << tmp) * (mtd->writesize / 512);
	if (tmp < 64) {
		pr_err("%s: Spare area (%d bytes) too small\n", __func__, tmp);
		goto out_error;
	}
	mtd->oobsize = tmp;
	mtd->oobavail = tegra_nand_oob_64.oobavail;

	/* data block size (erase size) (w/o spare) */
	tmp = (dev_parms >> 4) & 0x3;
	mtd->erasesize = (64 * 1024) << tmp;
	info->chip.block_shift = ffs(mtd->erasesize) - 1;

	/* used to select the appropriate chip/page in case multiple devices
	 * are connected */
	info->chip.chip_shift = ffs(info->chip.chipsize) - 1;
	info->chip.page_shift = ffs(mtd->writesize) - 1;
	info->chip.page_mask =
		(info->chip.chipsize >> info->chip.page_shift) - 1;

	/* now fill in the rest of the mtd fields */
	mtd->ecclayout = &tegra_nand_oob_64;
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;

	mtd->erase = tegra_nand_erase;
	mtd->lock = NULL;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = tegra_nand_read;
	mtd->write = tegra_nand_write;
	mtd->read_oob = tegra_nand_read_oob;
	mtd->write_oob = tegra_nand_write_oob;

	mtd->resume = tegra_nand_resume;
	mtd->suspend = tegra_nand_suspend;
	mtd->block_isbad = tegra_nand_block_isbad;
	mtd->block_markbad = tegra_nand_block_markbad;

	/* TODO: should take vendor_id/device_id */
	set_chip_timing(info);

	return 0;

out_error:
	pr_err("%s: NAND device scan aborted due to error(s).\n", __func__);
	return err;
}

static int __devinit
tegra_nand_probe(struct platform_device *pdev)
{
	struct tegra_nand_platform *plat = pdev->dev.platform_data;
	struct tegra_nand_info *info = NULL;
	struct tegra_nand_chip *chip = NULL;
	struct mtd_info *mtd = NULL;
	int err = 0;
	uint64_t num_erase_blocks;

	pr_debug("%s: probing (%p)\n", __func__, pdev);

	if (!plat) {
		pr_err("%s: no platform device info\n", __func__);
		return -EINVAL;
	} else if (!plat->chip_parms) {
		pr_err("%s: no platform nand parms\n", __func__);
		return -EINVAL;
	}

	info = kzalloc(sizeof(struct tegra_nand_info), GFP_KERNEL);
	if (!info) {
		pr_err("%s: no memory for flash info\n", __func__);
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	info->plat = plat;

	platform_set_drvdata(pdev, info);

	init_completion(&info->cmd_complete);
	init_completion(&info->dma_complete);

	mutex_init(&info->lock);
	spin_lock_init(&info->ecc_lock);

	chip = &info->chip;
	chip->priv = &info->mtd;
	chip->curr_chip = -1;

	mtd = &info->mtd;
	mtd->name = dev_name(&pdev->dev);
	mtd->priv = &info->chip;
	mtd->owner = THIS_MODULE;

	/* HACK: allocate a dma buffer to hold 1 page oob data */
	info->oob_dma_buf = dma_alloc_coherent(NULL, 64,
					   &info->oob_dma_addr, GFP_KERNEL);
	if (!info->oob_dma_buf) {
		err = -ENOMEM;
		goto out_free_info;
	}

	/* this will store the ecc error vector info */
	info->ecc_buf = dma_alloc_coherent(NULL, ECC_BUF_SZ, &info->ecc_addr,
					   GFP_KERNEL);
	if (!info->ecc_buf) {
		err = -ENOMEM;
		goto out_free_dma_buf;
	}

	/* grab the irq */
	if (!(pdev->resource[0].flags & IORESOURCE_IRQ)) {
		pr_err("NAND IRQ resource not defined\n");
		err = -EINVAL;
		goto out_free_ecc_buf;
	}

	err = request_irq(pdev->resource[0].start, tegra_nand_irq,
			  IRQF_SHARED, DRIVER_NAME, info);
	if (err) {
		pr_err("Unable to request IRQ %d (%d)\n",
				pdev->resource[0].start, err);
		goto out_free_ecc_buf;
	}

	/* TODO: configure pinmux here?? */
	info->clk = clk_get(&pdev->dev, NULL);
	clk_set_rate(info->clk, 108000000);

	cfg_hwstatus_mon(info);

	/* clear all pending interrupts */
	writel(readl(ISR_REG), ISR_REG);

	/* clear dma interrupt */
	writel(DMA_CTRL_IS_DMA_DONE, DMA_MST_CTRL_REG);

	/* enable interrupts */
	disable_ints(info, 0xffffffff);
	enable_ints(info, IER_ERR_TRIG_VAL(4) | IER_UND | IER_OVR | IER_CMD_DONE |
		    IER_ECC_ERR | IER_GIE);

	if (tegra_nand_scan(mtd, plat->max_chips)) {
		err = -ENXIO;
		goto out_dis_irq;
	}
	pr_info("%s: NVIDIA Tegra NAND controller @ base=0x%08x irq=%d.\n",
		DRIVER_NAME, TEGRA_NAND_PHYS, pdev->resource[0].start);

	/* allocate memory to hold the ecc error info */
	info->max_ecc_errs = MAX_DMA_SZ / mtd->writesize;
	info->ecc_errs = kmalloc(info->max_ecc_errs * sizeof(uint32_t),
				 GFP_KERNEL);
	if (!info->ecc_errs) {
		err = -ENOMEM;
		goto out_dis_irq;
	}

	/* alloc the bad block bitmap */
	num_erase_blocks = mtd->size;
	do_div(num_erase_blocks, mtd->erasesize);
	info->bb_bitmap = kzalloc(BITS_TO_LONGS(num_erase_blocks) *
				  sizeof(unsigned long), GFP_KERNEL);
	if (!info->bb_bitmap) {
		err = -ENOMEM;
		goto out_free_ecc;
	}

	err = scan_bad_blocks(info);
	if (err != 0)
		goto out_free_bbbmap;

#if 0
	dump_nand_regs();
#endif

#ifdef CONFIG_MTD_PARTITIONS
	err = parse_mtd_partitions(mtd, part_probes, &info->parts, 0);
	if (err > 0) {
		err = add_mtd_partitions(mtd, info->parts, err);
	} else if (err <= 0 && plat->parts) {
		err = add_mtd_partitions(mtd, plat->parts, plat->nr_parts);
	} else
#endif
		err = add_mtd_device(mtd);
		if (err != 0)
			goto out_free_bbbmap;

	dev_set_drvdata(&pdev->dev, info);

	pr_debug("%s: probe done.\n", __func__);
	return 0;

out_free_bbbmap:
	kfree(info->bb_bitmap);

out_free_ecc:
	kfree(info->ecc_errs);

out_dis_irq:
	disable_ints(info, 0xffffffff);
	free_irq(pdev->resource[0].start, info);

out_free_ecc_buf:
	dma_free_coherent(NULL, ECC_BUF_SZ, info->ecc_buf, info->ecc_addr);

out_free_dma_buf:
	dma_free_coherent(NULL, 64, info->oob_dma_buf,
			  info->oob_dma_addr);

out_free_info:
	platform_set_drvdata(pdev, NULL);
	kfree(info);

	return err;
}

static int __devexit
tegra_nand_remove(struct platform_device *pdev)
{
	struct tegra_nand_info *info = dev_get_drvdata(&pdev->dev);

	dev_set_drvdata(&pdev->dev, NULL);

	if (info) {
		free_irq(pdev->resource[0].start, info);
		kfree(info->bb_bitmap);
		kfree(info->ecc_errs);
		dma_free_coherent(NULL, ECC_BUF_SZ, info->ecc_buf, info->ecc_addr);
		dma_free_coherent(NULL, info->mtd.writesize + info->mtd.oobsize,
				  info->oob_dma_buf, info->oob_dma_addr);
		kfree(info);
	}

	return 0;
}

static struct platform_driver tegra_nand_driver = {
	.probe   = tegra_nand_probe,
	.remove  = __devexit_p(tegra_nand_remove),
	.suspend = NULL,
	.resume  = NULL,
	.driver  = {
		.name  = "tegra_nand",
		.owner = THIS_MODULE,
	},
};

static int __init
tegra_nand_init(void)
{
	return platform_driver_register(&tegra_nand_driver);
}

static void __exit
tegra_nand_exit(void)
{
	platform_driver_unregister(&tegra_nand_driver);
}

module_init(tegra_nand_init);
module_exit(tegra_nand_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);

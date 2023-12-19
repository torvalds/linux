// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Rockchip NAND Flash controller driver.
 * Copyright (C) 2020 Rockchip Inc.
 * Author: Yifeng Zhao <yifeng.zhao@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * NFC Page Data Layout:
 *	1024 bytes data + 4Bytes sys data + 28Bytes~124Bytes ECC data +
 *	1024 bytes data + 4Bytes sys data + 28Bytes~124Bytes ECC data +
 *	......
 * NAND Page Data Layout:
 *	1024 * n data + m Bytes oob
 * Original Bad Block Mask Location:
 *	First byte of oob(spare).
 * nand_chip->oob_poi data layout:
 *	4Bytes sys data + .... + 4Bytes sys data + ECC data.
 */

/* NAND controller register definition */
#define NFC_READ			(0)
#define NFC_WRITE			(1)

#define NFC_FMCTL			(0x00)
#define   FMCTL_CE_SEL_M		0xFF
#define   FMCTL_CE_SEL(x)		(1 << (x))
#define   FMCTL_WP			BIT(8)
#define   FMCTL_RDY			BIT(9)

#define NFC_FMWAIT			(0x04)
#define   FLCTL_RST			BIT(0)
#define   FLCTL_WR			(1)	/* 0: read, 1: write */
#define   FLCTL_XFER_ST			BIT(2)
#define   FLCTL_XFER_EN			BIT(3)
#define   FLCTL_ACORRECT		BIT(10) /* Auto correct error bits. */
#define   FLCTL_XFER_READY		BIT(20)
#define   FLCTL_XFER_SECTOR		(22)
#define   FLCTL_TOG_FIX			BIT(29)

#define   BCHCTL_BANK_M			(7 << 5)
#define   BCHCTL_BANK			(5)

#define   DMA_ST			BIT(0)
#define   DMA_WR			(1)	/* 0: write, 1: read */
#define   DMA_EN			BIT(2)
#define   DMA_AHB_SIZE			(3)	/* 0: 1, 1: 2, 2: 4 */
#define   DMA_BURST_SIZE		(6)	/* 0: 1, 3: 4, 5: 8, 7: 16 */
#define   DMA_INC_NUM			(9)	/* 1 - 16 */

#define ECC_ERR_CNT(x, e) ((((x) >> (e).low) & (e).low_mask) |\
	  (((x) >> (e).high) & (e).high_mask) << (e).low_bn)
#define   INT_DMA			BIT(0)
#define NFC_BANK			(0x800)
#define NFC_BANK_STEP			(0x100)
#define   BANK_DATA			(0x00)
#define   BANK_ADDR			(0x04)
#define   BANK_CMD			(0x08)
#define NFC_SRAM0			(0x1000)
#define NFC_SRAM1			(0x1400)
#define NFC_SRAM_SIZE			(0x400)
#define NFC_TIMEOUT			(500000)
#define NFC_MAX_OOB_PER_STEP		128
#define NFC_MIN_OOB_PER_STEP		64
#define MAX_DATA_SIZE			0xFFFC
#define MAX_ADDRESS_CYC			6
#define NFC_ECC_MAX_MODES		4
#define NFC_MAX_NSELS			(8) /* Some Socs only have 1 or 2 CSs. */
#define NFC_SYS_DATA_SIZE		(4) /* 4 bytes sys data in oob pre 1024 data.*/
#define RK_DEFAULT_CLOCK_RATE		(150 * 1000 * 1000) /* 150 Mhz */
#define ACCTIMING(csrw, rwpw, rwcs)	((csrw) << 12 | (rwpw) << 5 | (rwcs))

enum nfc_type {
	NFC_V6,
	NFC_V8,
	NFC_V9,
};

/**
 * struct rk_ecc_cnt_status: represent a ecc status data.
 * @err_flag_bit: error flag bit index at register.
 * @low: ECC count low bit index at register.
 * @low_mask: mask bit.
 * @low_bn: ECC count low bit number.
 * @high: ECC count high bit index at register.
 * @high_mask: mask bit
 */
struct ecc_cnt_status {
	u8 err_flag_bit;
	u8 low;
	u8 low_mask;
	u8 low_bn;
	u8 high;
	u8 high_mask;
};

/**
 * @type: NFC version
 * @ecc_strengths: ECC strengths
 * @ecc_cfgs: ECC config values
 * @flctl_off: FLCTL register offset
 * @bchctl_off: BCHCTL register offset
 * @dma_data_buf_off: DMA_DATA_BUF register offset
 * @dma_oob_buf_off: DMA_OOB_BUF register offset
 * @dma_cfg_off: DMA_CFG register offset
 * @dma_st_off: DMA_ST register offset
 * @bch_st_off: BCG_ST register offset
 * @randmz_off: RANDMZ register offset
 * @int_en_off: interrupt enable register offset
 * @int_clr_off: interrupt clean register offset
 * @int_st_off: interrupt status register offset
 * @oob0_off: oob0 register offset
 * @oob1_off: oob1 register offset
 * @ecc0: represent ECC0 status data
 * @ecc1: represent ECC1 status data
 */
struct nfc_cfg {
	enum nfc_type type;
	u8 ecc_strengths[NFC_ECC_MAX_MODES];
	u32 ecc_cfgs[NFC_ECC_MAX_MODES];
	u32 flctl_off;
	u32 bchctl_off;
	u32 dma_cfg_off;
	u32 dma_data_buf_off;
	u32 dma_oob_buf_off;
	u32 dma_st_off;
	u32 bch_st_off;
	u32 randmz_off;
	u32 int_en_off;
	u32 int_clr_off;
	u32 int_st_off;
	u32 oob0_off;
	u32 oob1_off;
	struct ecc_cnt_status ecc0;
	struct ecc_cnt_status ecc1;
};

struct rk_nfc_nand_chip {
	struct list_head node;
	struct nand_chip chip;

	u16 boot_blks;
	u16 metadata_size;
	u32 boot_ecc;
	u32 timing;

	u8 nsels;
	u8 sels[] __counted_by(nsels);
};

struct rk_nfc {
	struct nand_controller controller;
	const struct nfc_cfg *cfg;
	struct device *dev;

	struct clk *nfc_clk;
	struct clk *ahb_clk;
	void __iomem *regs;

	u32 selected_bank;
	u32 band_offset;
	u32 cur_ecc;
	u32 cur_timing;

	struct completion done;
	struct list_head chips;

	u8 *page_buf;
	u32 *oob_buf;
	u32 page_buf_size;
	u32 oob_buf_size;

	unsigned long assigned_cs;
};

static inline struct rk_nfc_nand_chip *rk_nfc_to_rknand(struct nand_chip *chip)
{
	return container_of(chip, struct rk_nfc_nand_chip, chip);
}

static inline u8 *rk_nfc_buf_to_data_ptr(struct nand_chip *chip, const u8 *p, int i)
{
	return (u8 *)p + i * chip->ecc.size;
}

static inline u8 *rk_nfc_buf_to_oob_ptr(struct nand_chip *chip, int i)
{
	u8 *poi;

	poi = chip->oob_poi + i * NFC_SYS_DATA_SIZE;

	return poi;
}

static inline u8 *rk_nfc_buf_to_oob_ecc_ptr(struct nand_chip *chip, int i)
{
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	u8 *poi;

	poi = chip->oob_poi + rknand->metadata_size + chip->ecc.bytes * i;

	return poi;
}

static inline int rk_nfc_data_len(struct nand_chip *chip)
{
	return chip->ecc.size + chip->ecc.bytes + NFC_SYS_DATA_SIZE;
}

static inline u8 *rk_nfc_data_ptr(struct nand_chip *chip, int i)
{
	struct rk_nfc *nfc = nand_get_controller_data(chip);

	return nfc->page_buf + i * rk_nfc_data_len(chip);
}

static inline u8 *rk_nfc_oob_ptr(struct nand_chip *chip, int i)
{
	struct rk_nfc *nfc = nand_get_controller_data(chip);

	return nfc->page_buf + i * rk_nfc_data_len(chip) + chip->ecc.size;
}

static int rk_nfc_hw_ecc_setup(struct nand_chip *chip, u32 strength)
{
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	u32 reg, i;

	for (i = 0; i < NFC_ECC_MAX_MODES; i++) {
		if (strength == nfc->cfg->ecc_strengths[i]) {
			reg = nfc->cfg->ecc_cfgs[i];
			break;
		}
	}

	if (i >= NFC_ECC_MAX_MODES)
		return -EINVAL;

	writel(reg, nfc->regs + nfc->cfg->bchctl_off);

	/* Save chip ECC setting */
	nfc->cur_ecc = strength;

	return 0;
}

static void rk_nfc_select_chip(struct nand_chip *chip, int cs)
{
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u32 val;

	if (cs < 0) {
		nfc->selected_bank = -1;
		/* Deselect the currently selected target. */
		val = readl_relaxed(nfc->regs + NFC_FMCTL);
		val &= ~FMCTL_CE_SEL_M;
		writel(val, nfc->regs + NFC_FMCTL);
		return;
	}

	nfc->selected_bank = rknand->sels[cs];
	nfc->band_offset = NFC_BANK + nfc->selected_bank * NFC_BANK_STEP;

	val = readl_relaxed(nfc->regs + NFC_FMCTL);
	val &= ~FMCTL_CE_SEL_M;
	val |= FMCTL_CE_SEL(nfc->selected_bank);

	writel(val, nfc->regs + NFC_FMCTL);

	/*
	 * Compare current chip timing with selected chip timing and
	 * change if needed.
	 */
	if (nfc->cur_timing != rknand->timing) {
		writel(rknand->timing, nfc->regs + NFC_FMWAIT);
		nfc->cur_timing = rknand->timing;
	}

	/*
	 * Compare current chip ECC setting with selected chip ECC setting and
	 * change if needed.
	 */
	if (nfc->cur_ecc != ecc->strength)
		rk_nfc_hw_ecc_setup(chip, ecc->strength);
}

static inline int rk_nfc_wait_ioready(struct rk_nfc *nfc)
{
	int rc;
	u32 val;

	rc = readl_relaxed_poll_timeout(nfc->regs + NFC_FMCTL, val,
					val & FMCTL_RDY, 10, NFC_TIMEOUT);

	return rc;
}

static void rk_nfc_read_buf(struct rk_nfc *nfc, u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = readb_relaxed(nfc->regs + nfc->band_offset +
				       BANK_DATA);
}

static void rk_nfc_write_buf(struct rk_nfc *nfc, const u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		writeb(buf[i], nfc->regs + nfc->band_offset + BANK_DATA);
}

static int rk_nfc_cmd(struct nand_chip *chip,
		      const struct nand_subop *subop)
{
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	unsigned int i, j, remaining, start;
	int reg_offset = nfc->band_offset;
	u8 *inbuf = NULL;
	const u8 *outbuf;
	u32 cnt = 0;
	int ret = 0;

	for (i = 0; i < subop->ninstrs; i++) {
		const struct nand_op_instr *instr = &subop->instrs[i];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			writeb(instr->ctx.cmd.opcode,
			       nfc->regs + reg_offset + BANK_CMD);
			break;

		case NAND_OP_ADDR_INSTR:
			remaining = nand_subop_get_num_addr_cyc(subop, i);
			start = nand_subop_get_addr_start_off(subop, i);

			for (j = 0; j < 8 && j + start < remaining; j++)
				writeb(instr->ctx.addr.addrs[j + start],
				       nfc->regs + reg_offset + BANK_ADDR);
			break;

		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_DATA_OUT_INSTR:
			start = nand_subop_get_data_start_off(subop, i);
			cnt = nand_subop_get_data_len(subop, i);

			if (instr->type == NAND_OP_DATA_OUT_INSTR) {
				outbuf = instr->ctx.data.buf.out + start;
				rk_nfc_write_buf(nfc, outbuf, cnt);
			} else {
				inbuf = instr->ctx.data.buf.in + start;
				rk_nfc_read_buf(nfc, inbuf, cnt);
			}
			break;

		case NAND_OP_WAITRDY_INSTR:
			if (rk_nfc_wait_ioready(nfc) < 0) {
				ret = -ETIMEDOUT;
				dev_err(nfc->dev, "IO not ready\n");
			}
			break;
		}
	}

	return ret;
}

static const struct nand_op_parser rk_nfc_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(
		rk_nfc_cmd,
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_ADDR_ELEM(true, MAX_ADDRESS_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(true, MAX_DATA_SIZE)),
	NAND_OP_PARSER_PATTERN(
		rk_nfc_cmd,
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_ADDR_ELEM(true, MAX_ADDRESS_CYC),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(true, MAX_DATA_SIZE),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
);

static int rk_nfc_exec_op(struct nand_chip *chip,
			  const struct nand_operation *op,
			  bool check_only)
{
	if (!check_only)
		rk_nfc_select_chip(chip, op->cs);

	return nand_op_parser_exec_op(chip, &rk_nfc_op_parser, op,
				      check_only);
}

static int rk_nfc_setup_interface(struct nand_chip *chip, int target,
				  const struct nand_interface_config *conf)
{
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	const struct nand_sdr_timings *timings;
	u32 rate, tc2rw, trwpw, trw2c;
	u32 temp;

	if (target < 0)
		return 0;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return -EOPNOTSUPP;

	if (IS_ERR(nfc->nfc_clk))
		rate = clk_get_rate(nfc->ahb_clk);
	else
		rate = clk_get_rate(nfc->nfc_clk);

	/* Turn clock rate into kHz. */
	rate /= 1000;

	tc2rw = 1;
	trw2c = 1;

	trwpw = max(timings->tWC_min, timings->tRC_min) / 1000;
	trwpw = DIV_ROUND_UP(trwpw * rate, 1000000);

	temp = timings->tREA_max / 1000;
	temp = DIV_ROUND_UP(temp * rate, 1000000);

	if (trwpw < temp)
		trwpw = temp;

	/*
	 * ACCON: access timing control register
	 * -------------------------------------
	 * 31:18: reserved
	 * 17:12: csrw, clock cycles from the falling edge of CSn to the
	 *   falling edge of RDn or WRn
	 * 11:11: reserved
	 * 10:05: rwpw, the width of RDn or WRn in processor clock cycles
	 * 04:00: rwcs, clock cycles from the rising edge of RDn or WRn to the
	 *   rising edge of CSn
	 */

	/* Save chip timing */
	rknand->timing = ACCTIMING(tc2rw, trwpw, trw2c);

	return 0;
}

static void rk_nfc_xfer_start(struct rk_nfc *nfc, u8 rw, u8 n_KB,
			      dma_addr_t dma_data, dma_addr_t dma_oob)
{
	u32 dma_reg, fl_reg, bch_reg;

	dma_reg = DMA_ST | ((!rw) << DMA_WR) | DMA_EN | (2 << DMA_AHB_SIZE) |
	      (7 << DMA_BURST_SIZE) | (16 << DMA_INC_NUM);

	fl_reg = (rw << FLCTL_WR) | FLCTL_XFER_EN | FLCTL_ACORRECT |
		 (n_KB << FLCTL_XFER_SECTOR) | FLCTL_TOG_FIX;

	if (nfc->cfg->type == NFC_V6 || nfc->cfg->type == NFC_V8) {
		bch_reg = readl_relaxed(nfc->regs + nfc->cfg->bchctl_off);
		bch_reg = (bch_reg & (~BCHCTL_BANK_M)) |
			  (nfc->selected_bank << BCHCTL_BANK);
		writel(bch_reg, nfc->regs + nfc->cfg->bchctl_off);
	}

	writel(dma_reg, nfc->regs + nfc->cfg->dma_cfg_off);
	writel((u32)dma_data, nfc->regs + nfc->cfg->dma_data_buf_off);
	writel((u32)dma_oob, nfc->regs + nfc->cfg->dma_oob_buf_off);
	writel(fl_reg, nfc->regs + nfc->cfg->flctl_off);
	fl_reg |= FLCTL_XFER_ST;
	writel(fl_reg, nfc->regs + nfc->cfg->flctl_off);
}

static int rk_nfc_wait_for_xfer_done(struct rk_nfc *nfc)
{
	void __iomem *ptr;
	u32 reg;

	ptr = nfc->regs + nfc->cfg->flctl_off;

	return readl_relaxed_poll_timeout(ptr, reg,
					 reg & FLCTL_XFER_READY,
					 10, NFC_TIMEOUT);
}

static int rk_nfc_write_page_raw(struct nand_chip *chip, const u8 *buf,
				 int oob_on, int page)
{
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int i, pages_per_blk;

	pages_per_blk = mtd->erasesize / mtd->writesize;
	if ((chip->options & NAND_IS_BOOT_MEDIUM) &&
	    (page < (pages_per_blk * rknand->boot_blks)) &&
	    rknand->boot_ecc != ecc->strength) {
		/*
		 * There's currently no method to notify the MTD framework that
		 * a different ECC strength is in use for the boot blocks.
		 */
		return -EIO;
	}

	if (!buf)
		memset(nfc->page_buf, 0xff, mtd->writesize + mtd->oobsize);

	for (i = 0; i < ecc->steps; i++) {
		/* Copy data to the NFC buffer. */
		if (buf)
			memcpy(rk_nfc_data_ptr(chip, i),
			       rk_nfc_buf_to_data_ptr(chip, buf, i),
			       ecc->size);
		/*
		 * The first four bytes of OOB are reserved for the
		 * boot ROM. In some debugging cases, such as with a
		 * read, erase and write back test these 4 bytes stored
		 * in OOB also need to be written back.
		 *
		 * The function nand_block_bad detects bad blocks like:
		 *
		 * bad = chip->oob_poi[chip->badblockpos];
		 *
		 * chip->badblockpos == 0 for a large page NAND Flash,
		 * so chip->oob_poi[0] is the bad block mask (BBM).
		 *
		 * The OOB data layout on the NFC is:
		 *
		 *    PA0  PA1  PA2  PA3  | BBM OOB1 OOB2 OOB3 | ...
		 *
		 * or
		 *
		 *    0xFF 0xFF 0xFF 0xFF | BBM OOB1 OOB2 OOB3 | ...
		 *
		 * The code here just swaps the first 4 bytes with the last
		 * 4 bytes without losing any data.
		 *
		 * The chip->oob_poi data layout:
		 *
		 *    BBM  OOB1 OOB2 OOB3 |......|  PA0  PA1  PA2  PA3
		 *
		 * The rk_nfc_ooblayout_free() function already has reserved
		 * these 4 bytes together with 2 bytes for BBM
		 * by reducing it's length:
		 *
		 * oob_region->length = rknand->metadata_size - NFC_SYS_DATA_SIZE - 2;
		 */
		if (!i)
			memcpy(rk_nfc_oob_ptr(chip, i),
			       rk_nfc_buf_to_oob_ptr(chip, ecc->steps - 1),
			       NFC_SYS_DATA_SIZE);
		else
			memcpy(rk_nfc_oob_ptr(chip, i),
			       rk_nfc_buf_to_oob_ptr(chip, i - 1),
			       NFC_SYS_DATA_SIZE);
		/* Copy ECC data to the NFC buffer. */
		memcpy(rk_nfc_oob_ptr(chip, i) + NFC_SYS_DATA_SIZE,
		       rk_nfc_buf_to_oob_ecc_ptr(chip, i),
		       ecc->bytes);
	}

	nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	rk_nfc_write_buf(nfc, buf, mtd->writesize + mtd->oobsize);
	return nand_prog_page_end_op(chip);
}

static int rk_nfc_write_page_hwecc(struct nand_chip *chip, const u8 *buf,
				   int oob_on, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int oob_step = (ecc->bytes > 60) ? NFC_MAX_OOB_PER_STEP :
			NFC_MIN_OOB_PER_STEP;
	int pages_per_blk = mtd->erasesize / mtd->writesize;
	int ret = 0, i, boot_rom_mode = 0;
	dma_addr_t dma_data, dma_oob;
	u32 tmp;
	u8 *oob;

	nand_prog_page_begin_op(chip, page, 0, NULL, 0);

	if (buf)
		memcpy(nfc->page_buf, buf, mtd->writesize);
	else
		memset(nfc->page_buf, 0xFF, mtd->writesize);

	/*
	 * The first blocks (4, 8 or 16 depending on the device) are used
	 * by the boot ROM and the first 32 bits of OOB need to link to
	 * the next page address in the same block. We can't directly copy
	 * OOB data from the MTD framework, because this page address
	 * conflicts for example with the bad block marker (BBM),
	 * so we shift all OOB data including the BBM with 4 byte positions.
	 * As a consequence the OOB size available to the MTD framework is
	 * also reduced with 4 bytes.
	 *
	 *    PA0  PA1  PA2  PA3 | BBM OOB1 OOB2 OOB3 | ...
	 *
	 * If a NAND is not a boot medium or the page is not a boot block,
	 * the first 4 bytes are left untouched by writing 0xFF to them.
	 *
	 *   0xFF 0xFF 0xFF 0xFF | BBM OOB1 OOB2 OOB3 | ...
	 *
	 * The code here just swaps the first 4 bytes with the last
	 * 4 bytes without losing any data.
	 *
	 * The chip->oob_poi data layout:
	 *
	 *    BBM  OOB1 OOB2 OOB3 |......|  PA0  PA1  PA2  PA3
	 *
	 * Configure the ECC algorithm supported by the boot ROM.
	 */
	if ((page < (pages_per_blk * rknand->boot_blks)) &&
	    (chip->options & NAND_IS_BOOT_MEDIUM)) {
		boot_rom_mode = 1;
		if (rknand->boot_ecc != ecc->strength)
			rk_nfc_hw_ecc_setup(chip, rknand->boot_ecc);
	}

	for (i = 0; i < ecc->steps; i++) {
		if (!i)
			oob = chip->oob_poi + (ecc->steps - 1) * NFC_SYS_DATA_SIZE;
		else
			oob = chip->oob_poi + (i - 1) * NFC_SYS_DATA_SIZE;

		tmp = oob[0] | oob[1] << 8 | oob[2] << 16 | oob[3] << 24;

		if (nfc->cfg->type == NFC_V9)
			nfc->oob_buf[i] = tmp;
		else
			nfc->oob_buf[i * (oob_step / 4)] = tmp;
	}

	dma_data = dma_map_single(nfc->dev, (void *)nfc->page_buf,
				  mtd->writesize, DMA_TO_DEVICE);
	dma_oob = dma_map_single(nfc->dev, nfc->oob_buf,
				 ecc->steps * oob_step,
				 DMA_TO_DEVICE);

	reinit_completion(&nfc->done);
	writel(INT_DMA, nfc->regs + nfc->cfg->int_en_off);

	rk_nfc_xfer_start(nfc, NFC_WRITE, ecc->steps, dma_data,
			  dma_oob);
	ret = wait_for_completion_timeout(&nfc->done,
					  msecs_to_jiffies(100));
	if (!ret)
		dev_warn(nfc->dev, "write: wait dma done timeout.\n");
	/*
	 * Whether the DMA transfer is completed or not. The driver
	 * needs to check the NFC`s status register to see if the data
	 * transfer was completed.
	 */
	ret = rk_nfc_wait_for_xfer_done(nfc);

	dma_unmap_single(nfc->dev, dma_data, mtd->writesize,
			 DMA_TO_DEVICE);
	dma_unmap_single(nfc->dev, dma_oob, ecc->steps * oob_step,
			 DMA_TO_DEVICE);

	if (boot_rom_mode && rknand->boot_ecc != ecc->strength)
		rk_nfc_hw_ecc_setup(chip, ecc->strength);

	if (ret) {
		dev_err(nfc->dev, "write: wait transfer done timeout.\n");
		return -ETIMEDOUT;
	}

	return nand_prog_page_end_op(chip);
}

static int rk_nfc_write_oob(struct nand_chip *chip, int page)
{
	return rk_nfc_write_page_hwecc(chip, NULL, 1, page);
}

static int rk_nfc_read_page_raw(struct nand_chip *chip, u8 *buf, int oob_on,
				int page)
{
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int i, pages_per_blk;

	pages_per_blk = mtd->erasesize / mtd->writesize;
	if ((chip->options & NAND_IS_BOOT_MEDIUM) &&
	    (page < (pages_per_blk * rknand->boot_blks)) &&
	    rknand->boot_ecc != ecc->strength) {
		/*
		 * There's currently no method to notify the MTD framework that
		 * a different ECC strength is in use for the boot blocks.
		 */
		return -EIO;
	}

	nand_read_page_op(chip, page, 0, NULL, 0);
	rk_nfc_read_buf(nfc, nfc->page_buf, mtd->writesize + mtd->oobsize);
	for (i = 0; i < ecc->steps; i++) {
		/*
		 * The first four bytes of OOB are reserved for the
		 * boot ROM. In some debugging cases, such as with a read,
		 * erase and write back test, these 4 bytes also must be
		 * saved somewhere, otherwise this information will be
		 * lost during a write back.
		 */
		if (!i)
			memcpy(rk_nfc_buf_to_oob_ptr(chip, ecc->steps - 1),
			       rk_nfc_oob_ptr(chip, i),
			       NFC_SYS_DATA_SIZE);
		else
			memcpy(rk_nfc_buf_to_oob_ptr(chip, i - 1),
			       rk_nfc_oob_ptr(chip, i),
			       NFC_SYS_DATA_SIZE);

		/* Copy ECC data from the NFC buffer. */
		memcpy(rk_nfc_buf_to_oob_ecc_ptr(chip, i),
		       rk_nfc_oob_ptr(chip, i) + NFC_SYS_DATA_SIZE,
		       ecc->bytes);

		/* Copy data from the NFC buffer. */
		if (buf)
			memcpy(rk_nfc_buf_to_data_ptr(chip, buf, i),
			       rk_nfc_data_ptr(chip, i),
			       ecc->size);
	}

	return 0;
}

static int rk_nfc_read_page_hwecc(struct nand_chip *chip, u8 *buf, int oob_on,
				  int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int oob_step = (ecc->bytes > 60) ? NFC_MAX_OOB_PER_STEP :
			NFC_MIN_OOB_PER_STEP;
	int pages_per_blk = mtd->erasesize / mtd->writesize;
	dma_addr_t dma_data, dma_oob;
	int ret = 0, i, cnt, boot_rom_mode = 0;
	int max_bitflips = 0, bch_st, ecc_fail = 0;
	u8 *oob;
	u32 tmp;

	nand_read_page_op(chip, page, 0, NULL, 0);

	dma_data = dma_map_single(nfc->dev, nfc->page_buf,
				  mtd->writesize,
				  DMA_FROM_DEVICE);
	dma_oob = dma_map_single(nfc->dev, nfc->oob_buf,
				 ecc->steps * oob_step,
				 DMA_FROM_DEVICE);

	/*
	 * The first blocks (4, 8 or 16 depending on the device)
	 * are used by the boot ROM.
	 * Configure the ECC algorithm supported by the boot ROM.
	 */
	if ((page < (pages_per_blk * rknand->boot_blks)) &&
	    (chip->options & NAND_IS_BOOT_MEDIUM)) {
		boot_rom_mode = 1;
		if (rknand->boot_ecc != ecc->strength)
			rk_nfc_hw_ecc_setup(chip, rknand->boot_ecc);
	}

	reinit_completion(&nfc->done);
	writel(INT_DMA, nfc->regs + nfc->cfg->int_en_off);
	rk_nfc_xfer_start(nfc, NFC_READ, ecc->steps, dma_data,
			  dma_oob);
	ret = wait_for_completion_timeout(&nfc->done,
					  msecs_to_jiffies(100));
	if (!ret)
		dev_warn(nfc->dev, "read: wait dma done timeout.\n");
	/*
	 * Whether the DMA transfer is completed or not. The driver
	 * needs to check the NFC`s status register to see if the data
	 * transfer was completed.
	 */
	ret = rk_nfc_wait_for_xfer_done(nfc);

	dma_unmap_single(nfc->dev, dma_data, mtd->writesize,
			 DMA_FROM_DEVICE);
	dma_unmap_single(nfc->dev, dma_oob, ecc->steps * oob_step,
			 DMA_FROM_DEVICE);

	if (ret) {
		ret = -ETIMEDOUT;
		dev_err(nfc->dev, "read: wait transfer done timeout.\n");
		goto timeout_err;
	}

	for (i = 0; i < ecc->steps; i++) {
		if (!i)
			oob = chip->oob_poi + (ecc->steps - 1) * NFC_SYS_DATA_SIZE;
		else
			oob = chip->oob_poi + (i - 1) * NFC_SYS_DATA_SIZE;

		if (nfc->cfg->type == NFC_V9)
			tmp = nfc->oob_buf[i];
		else
			tmp = nfc->oob_buf[i * (oob_step / 4)];

		*oob++ = (u8)tmp;
		*oob++ = (u8)(tmp >> 8);
		*oob++ = (u8)(tmp >> 16);
		*oob++ = (u8)(tmp >> 24);
	}

	for (i = 0; i < (ecc->steps / 2); i++) {
		bch_st = readl_relaxed(nfc->regs +
				       nfc->cfg->bch_st_off + i * 4);
		if (bch_st & BIT(nfc->cfg->ecc0.err_flag_bit) ||
		    bch_st & BIT(nfc->cfg->ecc1.err_flag_bit)) {
			mtd->ecc_stats.failed++;
			ecc_fail = 1;
		} else {
			cnt = ECC_ERR_CNT(bch_st, nfc->cfg->ecc0);
			mtd->ecc_stats.corrected += cnt;
			max_bitflips = max_t(u32, max_bitflips, cnt);

			cnt = ECC_ERR_CNT(bch_st, nfc->cfg->ecc1);
			mtd->ecc_stats.corrected += cnt;
			max_bitflips = max_t(u32, max_bitflips, cnt);
		}
	}

	if (buf)
		memcpy(buf, nfc->page_buf, mtd->writesize);

timeout_err:
	if (boot_rom_mode && rknand->boot_ecc != ecc->strength)
		rk_nfc_hw_ecc_setup(chip, ecc->strength);

	if (ret)
		return ret;

	if (ecc_fail) {
		dev_err(nfc->dev, "read page: %x ecc error!\n", page);
		return 0;
	}

	return max_bitflips;
}

static int rk_nfc_read_oob(struct nand_chip *chip, int page)
{
	return rk_nfc_read_page_hwecc(chip, NULL, 1, page);
}

static inline void rk_nfc_hw_init(struct rk_nfc *nfc)
{
	/* Disable flash wp. */
	writel(FMCTL_WP, nfc->regs + NFC_FMCTL);
	/* Config default timing 40ns at 150 Mhz NFC clock. */
	writel(0x1081, nfc->regs + NFC_FMWAIT);
	nfc->cur_timing = 0x1081;
	/* Disable randomizer and DMA. */
	writel(0, nfc->regs + nfc->cfg->randmz_off);
	writel(0, nfc->regs + nfc->cfg->dma_cfg_off);
	writel(FLCTL_RST, nfc->regs + nfc->cfg->flctl_off);
}

static irqreturn_t rk_nfc_irq(int irq, void *id)
{
	struct rk_nfc *nfc = id;
	u32 sta, ien;

	sta = readl_relaxed(nfc->regs + nfc->cfg->int_st_off);
	ien = readl_relaxed(nfc->regs + nfc->cfg->int_en_off);

	if (!(sta & ien))
		return IRQ_NONE;

	writel(sta, nfc->regs + nfc->cfg->int_clr_off);
	writel(~sta & ien, nfc->regs + nfc->cfg->int_en_off);

	complete(&nfc->done);

	return IRQ_HANDLED;
}

static int rk_nfc_enable_clks(struct device *dev, struct rk_nfc *nfc)
{
	int ret;

	if (!IS_ERR(nfc->nfc_clk)) {
		ret = clk_prepare_enable(nfc->nfc_clk);
		if (ret) {
			dev_err(dev, "failed to enable NFC clk\n");
			return ret;
		}
	}

	ret = clk_prepare_enable(nfc->ahb_clk);
	if (ret) {
		dev_err(dev, "failed to enable ahb clk\n");
		clk_disable_unprepare(nfc->nfc_clk);
		return ret;
	}

	return 0;
}

static void rk_nfc_disable_clks(struct rk_nfc *nfc)
{
	clk_disable_unprepare(nfc->nfc_clk);
	clk_disable_unprepare(nfc->ahb_clk);
}

static int rk_nfc_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);

	if (section)
		return -ERANGE;

	oob_region->length = rknand->metadata_size - NFC_SYS_DATA_SIZE - 2;
	oob_region->offset = 2;

	return 0;
}

static int rk_nfc_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oob_region)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);

	if (section)
		return -ERANGE;

	oob_region->length = mtd->oobsize - rknand->metadata_size;
	oob_region->offset = rknand->metadata_size;

	return 0;
}

static const struct mtd_ooblayout_ops rk_nfc_ooblayout_ops = {
	.free = rk_nfc_ooblayout_free,
	.ecc = rk_nfc_ooblayout_ecc,
};

static int rk_nfc_ecc_init(struct device *dev, struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	const u8 *strengths = nfc->cfg->ecc_strengths;
	u8 max_strength, nfc_max_strength;
	int i;

	nfc_max_strength = nfc->cfg->ecc_strengths[0];
	/* If optional dt settings not present. */
	if (!ecc->size || !ecc->strength ||
	    ecc->strength > nfc_max_strength) {
		chip->ecc.size = 1024;
		ecc->steps = mtd->writesize / ecc->size;

		/*
		 * HW ECC always requests the number of ECC bytes per 1024 byte
		 * blocks. The first 4 OOB bytes are reserved for sys data.
		 */
		max_strength = ((mtd->oobsize / ecc->steps) - 4) * 8 /
				 fls(8 * 1024);
		if (max_strength > nfc_max_strength)
			max_strength = nfc_max_strength;

		for (i = 0; i < 4; i++) {
			if (max_strength >= strengths[i])
				break;
		}

		if (i >= 4) {
			dev_err(nfc->dev, "unsupported ECC strength\n");
			return -EOPNOTSUPP;
		}

		ecc->strength = strengths[i];
	}
	ecc->steps = mtd->writesize / ecc->size;
	ecc->bytes = DIV_ROUND_UP(ecc->strength * fls(8 * chip->ecc.size), 8);

	return 0;
}

static int rk_nfc_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct device *dev = mtd->dev.parent;
	struct rk_nfc *nfc = nand_get_controller_data(chip);
	struct rk_nfc_nand_chip *rknand = rk_nfc_to_rknand(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int new_page_len, new_oob_len;
	void *buf;
	int ret;

	if (chip->options & NAND_BUSWIDTH_16) {
		dev_err(dev, "16 bits bus width not supported");
		return -EINVAL;
	}

	if (ecc->engine_type != NAND_ECC_ENGINE_TYPE_ON_HOST)
		return 0;

	ret = rk_nfc_ecc_init(dev, mtd);
	if (ret)
		return ret;

	rknand->metadata_size = NFC_SYS_DATA_SIZE * ecc->steps;

	if (rknand->metadata_size < NFC_SYS_DATA_SIZE + 2) {
		dev_err(dev,
			"driver needs at least %d bytes of meta data\n",
			NFC_SYS_DATA_SIZE + 2);
		return -EIO;
	}

	/* Check buffer first, avoid duplicate alloc buffer. */
	new_page_len = mtd->writesize + mtd->oobsize;
	if (nfc->page_buf && new_page_len > nfc->page_buf_size) {
		buf = krealloc(nfc->page_buf, new_page_len,
			       GFP_KERNEL | GFP_DMA);
		if (!buf)
			return -ENOMEM;
		nfc->page_buf = buf;
		nfc->page_buf_size = new_page_len;
	}

	new_oob_len = ecc->steps * NFC_MAX_OOB_PER_STEP;
	if (nfc->oob_buf && new_oob_len > nfc->oob_buf_size) {
		buf = krealloc(nfc->oob_buf, new_oob_len,
			       GFP_KERNEL | GFP_DMA);
		if (!buf) {
			kfree(nfc->page_buf);
			nfc->page_buf = NULL;
			return -ENOMEM;
		}
		nfc->oob_buf = buf;
		nfc->oob_buf_size = new_oob_len;
	}

	if (!nfc->page_buf) {
		nfc->page_buf = kzalloc(new_page_len, GFP_KERNEL | GFP_DMA);
		if (!nfc->page_buf)
			return -ENOMEM;
		nfc->page_buf_size = new_page_len;
	}

	if (!nfc->oob_buf) {
		nfc->oob_buf = kzalloc(new_oob_len, GFP_KERNEL | GFP_DMA);
		if (!nfc->oob_buf) {
			kfree(nfc->page_buf);
			nfc->page_buf = NULL;
			return -ENOMEM;
		}
		nfc->oob_buf_size = new_oob_len;
	}

	chip->ecc.write_page_raw = rk_nfc_write_page_raw;
	chip->ecc.write_page = rk_nfc_write_page_hwecc;
	chip->ecc.write_oob = rk_nfc_write_oob;

	chip->ecc.read_page_raw = rk_nfc_read_page_raw;
	chip->ecc.read_page = rk_nfc_read_page_hwecc;
	chip->ecc.read_oob = rk_nfc_read_oob;

	return 0;
}

static const struct nand_controller_ops rk_nfc_controller_ops = {
	.attach_chip = rk_nfc_attach_chip,
	.exec_op = rk_nfc_exec_op,
	.setup_interface = rk_nfc_setup_interface,
};

static int rk_nfc_nand_chip_init(struct device *dev, struct rk_nfc *nfc,
				 struct device_node *np)
{
	struct rk_nfc_nand_chip *rknand;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	int nsels;
	u32 tmp;
	int ret;
	int i;

	if (!of_get_property(np, "reg", &nsels))
		return -ENODEV;
	nsels /= sizeof(u32);
	if (!nsels || nsels > NFC_MAX_NSELS) {
		dev_err(dev, "invalid reg property size %d\n", nsels);
		return -EINVAL;
	}

	rknand = devm_kzalloc(dev, struct_size(rknand, sels, nsels),
			      GFP_KERNEL);
	if (!rknand)
		return -ENOMEM;

	rknand->nsels = nsels;
	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &tmp);
		if (ret) {
			dev_err(dev, "reg property failure : %d\n", ret);
			return ret;
		}

		if (tmp >= NFC_MAX_NSELS) {
			dev_err(dev, "invalid CS: %u\n", tmp);
			return -EINVAL;
		}

		if (test_and_set_bit(tmp, &nfc->assigned_cs)) {
			dev_err(dev, "CS %u already assigned\n", tmp);
			return -EINVAL;
		}

		rknand->sels[i] = tmp;
	}

	chip = &rknand->chip;
	chip->controller = &nfc->controller;

	nand_set_flash_node(chip, np);

	nand_set_controller_data(chip, nfc);

	chip->options |= NAND_USES_DMA | NAND_NO_SUBPAGE_WRITE;
	chip->bbt_options = NAND_BBT_USE_FLASH | NAND_BBT_NO_OOB;

	/* Set default mode in case dt entry is missing. */
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	mtd = nand_to_mtd(chip);
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;

	if (!mtd->name) {
		dev_err(nfc->dev, "NAND label property is mandatory\n");
		return -EINVAL;
	}

	mtd_set_ooblayout(mtd, &rk_nfc_ooblayout_ops);
	rk_nfc_hw_init(nfc);
	ret = nand_scan(chip, nsels);
	if (ret)
		return ret;

	if (chip->options & NAND_IS_BOOT_MEDIUM) {
		ret = of_property_read_u32(np, "rockchip,boot-blks", &tmp);
		rknand->boot_blks = ret ? 0 : tmp;

		ret = of_property_read_u32(np, "rockchip,boot-ecc-strength",
					   &tmp);
		rknand->boot_ecc = ret ? chip->ecc.strength : tmp;
	}

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "MTD parse partition error\n");
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&rknand->node, &nfc->chips);

	return 0;
}

static void rk_nfc_chips_cleanup(struct rk_nfc *nfc)
{
	struct rk_nfc_nand_chip *rknand, *tmp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(rknand, tmp, &nfc->chips, node) {
		chip = &rknand->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&rknand->node);
	}
}

static int rk_nfc_nand_chips_init(struct device *dev, struct rk_nfc *nfc)
{
	struct device_node *np = dev->of_node, *nand_np;
	int nchips = of_get_child_count(np);
	int ret;

	if (!nchips || nchips > NFC_MAX_NSELS) {
		dev_err(nfc->dev, "incorrect number of NAND chips (%d)\n",
			nchips);
		return -EINVAL;
	}

	for_each_child_of_node(np, nand_np) {
		ret = rk_nfc_nand_chip_init(dev, nfc, nand_np);
		if (ret) {
			of_node_put(nand_np);
			rk_nfc_chips_cleanup(nfc);
			return ret;
		}
	}

	return 0;
}

static struct nfc_cfg nfc_v6_cfg = {
		.type			= NFC_V6,
		.ecc_strengths		= {60, 40, 24, 16},
		.ecc_cfgs		= {
			0x00040011, 0x00040001, 0x00000011, 0x00000001,
		},
		.flctl_off		= 0x08,
		.bchctl_off		= 0x0C,
		.dma_cfg_off		= 0x10,
		.dma_data_buf_off	= 0x14,
		.dma_oob_buf_off	= 0x18,
		.dma_st_off		= 0x1C,
		.bch_st_off		= 0x20,
		.randmz_off		= 0x150,
		.int_en_off		= 0x16C,
		.int_clr_off		= 0x170,
		.int_st_off		= 0x174,
		.oob0_off		= 0x200,
		.oob1_off		= 0x230,
		.ecc0			= {
			.err_flag_bit	= 2,
			.low		= 3,
			.low_mask	= 0x1F,
			.low_bn		= 5,
			.high		= 27,
			.high_mask	= 0x1,
		},
		.ecc1			= {
			.err_flag_bit	= 15,
			.low		= 16,
			.low_mask	= 0x1F,
			.low_bn		= 5,
			.high		= 29,
			.high_mask	= 0x1,
		},
};

static struct nfc_cfg nfc_v8_cfg = {
		.type			= NFC_V8,
		.ecc_strengths		= {16, 16, 16, 16},
		.ecc_cfgs		= {
			0x00000001, 0x00000001, 0x00000001, 0x00000001,
		},
		.flctl_off		= 0x08,
		.bchctl_off		= 0x0C,
		.dma_cfg_off		= 0x10,
		.dma_data_buf_off	= 0x14,
		.dma_oob_buf_off	= 0x18,
		.dma_st_off		= 0x1C,
		.bch_st_off		= 0x20,
		.randmz_off		= 0x150,
		.int_en_off		= 0x16C,
		.int_clr_off		= 0x170,
		.int_st_off		= 0x174,
		.oob0_off		= 0x200,
		.oob1_off		= 0x230,
		.ecc0			= {
			.err_flag_bit	= 2,
			.low		= 3,
			.low_mask	= 0x1F,
			.low_bn		= 5,
			.high		= 27,
			.high_mask	= 0x1,
		},
		.ecc1			= {
			.err_flag_bit	= 15,
			.low		= 16,
			.low_mask	= 0x1F,
			.low_bn		= 5,
			.high		= 29,
			.high_mask	= 0x1,
		},
};

static struct nfc_cfg nfc_v9_cfg = {
		.type			= NFC_V9,
		.ecc_strengths		= {70, 60, 40, 16},
		.ecc_cfgs		= {
			0x00000001, 0x06000001, 0x04000001, 0x02000001,
		},
		.flctl_off		= 0x10,
		.bchctl_off		= 0x20,
		.dma_cfg_off		= 0x30,
		.dma_data_buf_off	= 0x34,
		.dma_oob_buf_off	= 0x38,
		.dma_st_off		= 0x3C,
		.bch_st_off		= 0x150,
		.randmz_off		= 0x208,
		.int_en_off		= 0x120,
		.int_clr_off		= 0x124,
		.int_st_off		= 0x128,
		.oob0_off		= 0x200,
		.oob1_off		= 0x204,
		.ecc0			= {
			.err_flag_bit	= 2,
			.low		= 3,
			.low_mask	= 0x7F,
			.low_bn		= 7,
			.high		= 0,
			.high_mask	= 0x0,
		},
		.ecc1			= {
			.err_flag_bit	= 18,
			.low		= 19,
			.low_mask	= 0x7F,
			.low_bn		= 7,
			.high		= 0,
			.high_mask	= 0x0,
		},
};

static const struct of_device_id rk_nfc_id_table[] = {
	{
		.compatible = "rockchip,px30-nfc",
		.data = &nfc_v9_cfg
	},
	{
		.compatible = "rockchip,rk2928-nfc",
		.data = &nfc_v6_cfg
	},
	{
		.compatible = "rockchip,rv1108-nfc",
		.data = &nfc_v8_cfg
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rk_nfc_id_table);

static int rk_nfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_nfc *nfc;
	int ret, irq;

	nfc = devm_kzalloc(dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nand_controller_init(&nfc->controller);
	INIT_LIST_HEAD(&nfc->chips);
	nfc->controller.ops = &rk_nfc_controller_ops;

	nfc->cfg = of_device_get_match_data(dev);
	nfc->dev = dev;

	init_completion(&nfc->done);

	nfc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nfc->regs)) {
		ret = PTR_ERR(nfc->regs);
		goto release_nfc;
	}

	nfc->nfc_clk = devm_clk_get(dev, "nfc");
	if (IS_ERR(nfc->nfc_clk)) {
		dev_dbg(dev, "no NFC clk\n");
		/* Some earlier models, such as rk3066, have no NFC clk. */
	}

	nfc->ahb_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(nfc->ahb_clk)) {
		dev_err(dev, "no ahb clk\n");
		ret = PTR_ERR(nfc->ahb_clk);
		goto release_nfc;
	}

	ret = rk_nfc_enable_clks(dev, nfc);
	if (ret)
		goto release_nfc;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -EINVAL;
		goto clk_disable;
	}

	writel(0, nfc->regs + nfc->cfg->int_en_off);
	ret = devm_request_irq(dev, irq, rk_nfc_irq, 0x0, "rk-nand", nfc);
	if (ret) {
		dev_err(dev, "failed to request NFC irq\n");
		goto clk_disable;
	}

	platform_set_drvdata(pdev, nfc);

	ret = rk_nfc_nand_chips_init(dev, nfc);
	if (ret) {
		dev_err(dev, "failed to init NAND chips\n");
		goto clk_disable;
	}
	return 0;

clk_disable:
	rk_nfc_disable_clks(nfc);
release_nfc:
	return ret;
}

static void rk_nfc_remove(struct platform_device *pdev)
{
	struct rk_nfc *nfc = platform_get_drvdata(pdev);

	kfree(nfc->page_buf);
	kfree(nfc->oob_buf);
	rk_nfc_chips_cleanup(nfc);
	rk_nfc_disable_clks(nfc);
}

static int __maybe_unused rk_nfc_suspend(struct device *dev)
{
	struct rk_nfc *nfc = dev_get_drvdata(dev);

	rk_nfc_disable_clks(nfc);

	return 0;
}

static int __maybe_unused rk_nfc_resume(struct device *dev)
{
	struct rk_nfc *nfc = dev_get_drvdata(dev);
	struct rk_nfc_nand_chip *rknand;
	struct nand_chip *chip;
	int ret;
	u32 i;

	ret = rk_nfc_enable_clks(dev, nfc);
	if (ret)
		return ret;

	/* Reset NAND chip if VCC was powered off. */
	list_for_each_entry(rknand, &nfc->chips, node) {
		chip = &rknand->chip;
		for (i = 0; i < rknand->nsels; i++)
			nand_reset(chip, i);
	}

	return 0;
}

static const struct dev_pm_ops rk_nfc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rk_nfc_suspend, rk_nfc_resume)
};

static struct platform_driver rk_nfc_driver = {
	.probe = rk_nfc_probe,
	.remove_new = rk_nfc_remove,
	.driver = {
		.name = "rockchip-nfc",
		.of_match_table = rk_nfc_id_table,
		.pm = &rk_nfc_pm_ops,
	},
};

module_platform_driver(rk_nfc_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Yifeng Zhao <yifeng.zhao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Nand Flash Controller Driver");
MODULE_ALIAS("platform:rockchip-nand-controller");

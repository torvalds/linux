// SPDX-License-Identifier: GPL-2.0
/*
 * Evatronix/Renesas R-Car Gen3, RZ/N1D, RZ/N1S, RZ/N1L NAND controller driver
 *
 * Copyright (C) 2021 Schneider Electric
 * Author: Miquel RAYNAL <miquel.raynal@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#define COMMAND_REG 0x00
#define   COMMAND_SEQ(x) FIELD_PREP(GENMASK(5, 0), (x))
#define     COMMAND_SEQ_10 COMMAND_SEQ(0x2A)
#define     COMMAND_SEQ_12 COMMAND_SEQ(0x0C)
#define     COMMAND_SEQ_18 COMMAND_SEQ(0x32)
#define     COMMAND_SEQ_19 COMMAND_SEQ(0x13)
#define     COMMAND_SEQ_GEN_IN COMMAND_SEQ_18
#define     COMMAND_SEQ_GEN_OUT COMMAND_SEQ_19
#define     COMMAND_SEQ_READ_PAGE COMMAND_SEQ_10
#define     COMMAND_SEQ_WRITE_PAGE COMMAND_SEQ_12
#define   COMMAND_INPUT_SEL_AHBS 0
#define   COMMAND_INPUT_SEL_DMA BIT(6)
#define   COMMAND_FIFO_SEL 0
#define   COMMAND_DATA_SEL BIT(7)
#define   COMMAND_0(x) FIELD_PREP(GENMASK(15, 8), (x))
#define   COMMAND_1(x) FIELD_PREP(GENMASK(23, 16), (x))
#define   COMMAND_2(x) FIELD_PREP(GENMASK(31, 24), (x))

#define CONTROL_REG 0x04
#define   CONTROL_CHECK_RB_LINE 0
#define   CONTROL_ECC_BLOCK_SIZE(x) FIELD_PREP(GENMASK(2, 1), (x))
#define     CONTROL_ECC_BLOCK_SIZE_256 CONTROL_ECC_BLOCK_SIZE(0)
#define     CONTROL_ECC_BLOCK_SIZE_512 CONTROL_ECC_BLOCK_SIZE(1)
#define     CONTROL_ECC_BLOCK_SIZE_1024 CONTROL_ECC_BLOCK_SIZE(2)
#define   CONTROL_INT_EN BIT(4)
#define   CONTROL_ECC_EN BIT(5)
#define   CONTROL_BLOCK_SIZE(x) FIELD_PREP(GENMASK(7, 6), (x))
#define     CONTROL_BLOCK_SIZE_32P CONTROL_BLOCK_SIZE(0)
#define     CONTROL_BLOCK_SIZE_64P CONTROL_BLOCK_SIZE(1)
#define     CONTROL_BLOCK_SIZE_128P CONTROL_BLOCK_SIZE(2)
#define     CONTROL_BLOCK_SIZE_256P CONTROL_BLOCK_SIZE(3)

#define STATUS_REG 0x8
#define   MEM_RDY(cs, reg) (FIELD_GET(GENMASK(3, 0), (reg)) & BIT(cs))
#define   CTRL_RDY(reg) (FIELD_GET(BIT(8), (reg)) == 0)

#define ECC_CTRL_REG 0x18
#define   ECC_CTRL_CAP(x) FIELD_PREP(GENMASK(2, 0), (x))
#define     ECC_CTRL_CAP_2B ECC_CTRL_CAP(0)
#define     ECC_CTRL_CAP_4B ECC_CTRL_CAP(1)
#define     ECC_CTRL_CAP_8B ECC_CTRL_CAP(2)
#define     ECC_CTRL_CAP_16B ECC_CTRL_CAP(3)
#define     ECC_CTRL_CAP_24B ECC_CTRL_CAP(4)
#define     ECC_CTRL_CAP_32B ECC_CTRL_CAP(5)
#define   ECC_CTRL_ERR_THRESHOLD(x) FIELD_PREP(GENMASK(13, 8), (x))

#define INT_MASK_REG 0x10
#define INT_STATUS_REG 0x14
#define   INT_CMD_END BIT(1)
#define   INT_DMA_END BIT(3)
#define   INT_MEM_RDY(cs) FIELD_PREP(GENMASK(11, 8), BIT(cs))
#define   INT_DMA_ENDED BIT(3)
#define   MEM_IS_RDY(cs, reg) (FIELD_GET(GENMASK(11, 8), (reg)) & BIT(cs))
#define   DMA_HAS_ENDED(reg) FIELD_GET(BIT(3), (reg))

#define ECC_OFFSET_REG 0x1C
#define   ECC_OFFSET(x) FIELD_PREP(GENMASK(15, 0), (x))

#define ECC_STAT_REG 0x20
#define   ECC_STAT_CORRECTABLE(cs, reg) (FIELD_GET(GENMASK(3, 0), (reg)) & BIT(cs))
#define   ECC_STAT_UNCORRECTABLE(cs, reg) (FIELD_GET(GENMASK(11, 8), (reg)) & BIT(cs))

#define ADDR0_COL_REG 0x24
#define   ADDR0_COL(x) FIELD_PREP(GENMASK(15, 0), (x))

#define ADDR0_ROW_REG 0x28
#define   ADDR0_ROW(x) FIELD_PREP(GENMASK(23, 0), (x))

#define ADDR1_COL_REG 0x2C
#define   ADDR1_COL(x) FIELD_PREP(GENMASK(15, 0), (x))

#define ADDR1_ROW_REG 0x30
#define   ADDR1_ROW(x) FIELD_PREP(GENMASK(23, 0), (x))

#define FIFO_DATA_REG 0x38

#define DATA_REG 0x3C

#define DATA_REG_SIZE_REG 0x40

#define DMA_ADDR_LOW_REG 0x64

#define DMA_ADDR_HIGH_REG 0x68

#define DMA_CNT_REG 0x6C

#define DMA_CTRL_REG 0x70
#define   DMA_CTRL_INCREMENT_BURST_4 0
#define   DMA_CTRL_REGISTER_MANAGED_MODE 0
#define   DMA_CTRL_START BIT(7)

#define MEM_CTRL_REG 0x80
#define   MEM_CTRL_CS(cs) FIELD_PREP(GENMASK(1, 0), (cs))
#define   MEM_CTRL_DIS_WP(cs) FIELD_PREP(GENMASK(11, 8), BIT((cs)))

#define DATA_SIZE_REG 0x84
#define   DATA_SIZE(x) FIELD_PREP(GENMASK(14, 0), (x))

#define TIMINGS_ASYN_REG 0x88
#define   TIMINGS_ASYN_TRWP(x) FIELD_PREP(GENMASK(3, 0), max((x), 1U) - 1)
#define   TIMINGS_ASYN_TRWH(x) FIELD_PREP(GENMASK(7, 4), max((x), 1U) - 1)

#define TIM_SEQ0_REG 0x90
#define   TIM_SEQ0_TCCS(x) FIELD_PREP(GENMASK(5, 0), max((x), 1U) - 1)
#define   TIM_SEQ0_TADL(x) FIELD_PREP(GENMASK(13, 8), max((x), 1U) - 1)
#define   TIM_SEQ0_TRHW(x) FIELD_PREP(GENMASK(21, 16), max((x), 1U) - 1)
#define   TIM_SEQ0_TWHR(x) FIELD_PREP(GENMASK(29, 24), max((x), 1U) - 1)

#define TIM_SEQ1_REG 0x94
#define   TIM_SEQ1_TWB(x) FIELD_PREP(GENMASK(5, 0), max((x), 1U) - 1)
#define   TIM_SEQ1_TRR(x) FIELD_PREP(GENMASK(13, 8), max((x), 1U) - 1)
#define   TIM_SEQ1_TWW(x) FIELD_PREP(GENMASK(21, 16), max((x), 1U) - 1)

#define TIM_GEN_SEQ0_REG 0x98
#define   TIM_GEN_SEQ0_D0(x) FIELD_PREP(GENMASK(5, 0), max((x), 1U) - 1)
#define   TIM_GEN_SEQ0_D1(x) FIELD_PREP(GENMASK(13, 8), max((x), 1U) - 1)
#define   TIM_GEN_SEQ0_D2(x) FIELD_PREP(GENMASK(21, 16), max((x), 1U) - 1)
#define   TIM_GEN_SEQ0_D3(x) FIELD_PREP(GENMASK(29, 24), max((x), 1U) - 1)

#define TIM_GEN_SEQ1_REG 0x9c
#define   TIM_GEN_SEQ1_D4(x) FIELD_PREP(GENMASK(5, 0), max((x), 1U) - 1)
#define   TIM_GEN_SEQ1_D5(x) FIELD_PREP(GENMASK(13, 8), max((x), 1U) - 1)
#define   TIM_GEN_SEQ1_D6(x) FIELD_PREP(GENMASK(21, 16), max((x), 1U) - 1)
#define   TIM_GEN_SEQ1_D7(x) FIELD_PREP(GENMASK(29, 24), max((x), 1U) - 1)

#define TIM_GEN_SEQ2_REG 0xA0
#define   TIM_GEN_SEQ2_D8(x) FIELD_PREP(GENMASK(5, 0), max((x), 1U) - 1)
#define   TIM_GEN_SEQ2_D9(x) FIELD_PREP(GENMASK(13, 8), max((x), 1U) - 1)
#define   TIM_GEN_SEQ2_D10(x) FIELD_PREP(GENMASK(21, 16), max((x), 1U) - 1)
#define   TIM_GEN_SEQ2_D11(x) FIELD_PREP(GENMASK(29, 24), max((x), 1U) - 1)

#define FIFO_INIT_REG 0xB4
#define   FIFO_INIT BIT(0)

#define FIFO_STATE_REG 0xB4
#define   FIFO_STATE_R_EMPTY(reg) FIELD_GET(BIT(0), (reg))
#define   FIFO_STATE_W_FULL(reg) FIELD_GET(BIT(1), (reg))
#define   FIFO_STATE_C_EMPTY(reg) FIELD_GET(BIT(2), (reg))
#define   FIFO_STATE_R_FULL(reg) FIELD_GET(BIT(6), (reg))
#define   FIFO_STATE_W_EMPTY(reg) FIELD_GET(BIT(7), (reg))

#define GEN_SEQ_CTRL_REG 0xB8
#define   GEN_SEQ_CMD0_EN BIT(0)
#define   GEN_SEQ_CMD1_EN BIT(1)
#define   GEN_SEQ_CMD2_EN BIT(2)
#define   GEN_SEQ_CMD3_EN BIT(3)
#define   GEN_SEQ_COL_A0(x) FIELD_PREP(GENMASK(5, 4), min((x), 2U))
#define   GEN_SEQ_COL_A1(x) FIELD_PREP(GENMASK(7, 6), min((x), 2U))
#define   GEN_SEQ_ROW_A0(x) FIELD_PREP(GENMASK(9, 8), min((x), 3U))
#define   GEN_SEQ_ROW_A1(x) FIELD_PREP(GENMASK(11, 10), min((x), 3U))
#define   GEN_SEQ_DATA_EN BIT(12)
#define   GEN_SEQ_DELAY_EN(x) FIELD_PREP(GENMASK(14, 13), (x))
#define     GEN_SEQ_DELAY0_EN GEN_SEQ_DELAY_EN(1)
#define     GEN_SEQ_DELAY1_EN GEN_SEQ_DELAY_EN(2)
#define   GEN_SEQ_IMD_SEQ BIT(15)
#define   GEN_SEQ_COMMAND_3(x) FIELD_PREP(GENMASK(26, 16), (x))

#define DMA_TLVL_REG 0x114
#define   DMA_TLVL(x) FIELD_PREP(GENMASK(7, 0), (x))
#define   DMA_TLVL_MAX DMA_TLVL(0xFF)

#define TIM_GEN_SEQ3_REG 0x134
#define   TIM_GEN_SEQ3_D12(x) FIELD_PREP(GENMASK(5, 0), max((x), 1U) - 1)

#define ECC_CNT_REG 0x14C
#define   ECC_CNT(cs, reg) FIELD_GET(GENMASK(5, 0), (reg) >> ((cs) * 8))

#define RNANDC_CS_NUM 4

#define TO_CYCLES64(ps, period_ns) ((unsigned int)DIV_ROUND_UP_ULL(div_u64(ps, 1000), \
								   period_ns))

struct rnand_chip_sel {
	unsigned int cs;
};

struct rnand_chip {
	struct nand_chip chip;
	struct list_head node;
	int selected_die;
	u32 ctrl;
	unsigned int nsels;
	u32 control;
	u32 ecc_ctrl;
	u32 timings_asyn;
	u32 tim_seq0;
	u32 tim_seq1;
	u32 tim_gen_seq0;
	u32 tim_gen_seq1;
	u32 tim_gen_seq2;
	u32 tim_gen_seq3;
	struct rnand_chip_sel sels[] __counted_by(nsels);
};

struct rnandc {
	struct nand_controller controller;
	struct device *dev;
	void __iomem *regs;
	unsigned long ext_clk_rate;
	unsigned long assigned_cs;
	struct list_head chips;
	struct nand_chip *selected_chip;
	struct completion complete;
	bool use_polling;
	u8 *buf;
	unsigned int buf_sz;
};

struct rnandc_op {
	u32 command;
	u32 addr0_col;
	u32 addr0_row;
	u32 addr1_col;
	u32 addr1_row;
	u32 data_size;
	u32 ecc_offset;
	u32 gen_seq_ctrl;
	u8 *buf;
	bool read;
	unsigned int len;
};

static inline struct rnandc *to_rnandc(struct nand_controller *ctrl)
{
	return container_of(ctrl, struct rnandc, controller);
}

static inline struct rnand_chip *to_rnand(struct nand_chip *chip)
{
	return container_of(chip, struct rnand_chip, chip);
}

static inline unsigned int to_rnandc_cs(struct rnand_chip *nand)
{
	return nand->sels[nand->selected_die].cs;
}

static void rnandc_dis_correction(struct rnandc *rnandc)
{
	u32 control;

	control = readl_relaxed(rnandc->regs + CONTROL_REG);
	control &= ~CONTROL_ECC_EN;
	writel_relaxed(control, rnandc->regs + CONTROL_REG);
}

static void rnandc_en_correction(struct rnandc *rnandc)
{
	u32 control;

	control = readl_relaxed(rnandc->regs + CONTROL_REG);
	control |= CONTROL_ECC_EN;
	writel_relaxed(control, rnandc->regs + CONTROL_REG);
}

static void rnandc_clear_status(struct rnandc *rnandc)
{
	writel_relaxed(0, rnandc->regs + INT_STATUS_REG);
	writel_relaxed(0, rnandc->regs + ECC_STAT_REG);
	writel_relaxed(0, rnandc->regs + ECC_CNT_REG);
}

static void rnandc_dis_interrupts(struct rnandc *rnandc)
{
	writel_relaxed(0, rnandc->regs + INT_MASK_REG);
}

static void rnandc_en_interrupts(struct rnandc *rnandc, u32 val)
{
	if (!rnandc->use_polling)
		writel_relaxed(val, rnandc->regs + INT_MASK_REG);
}

static void rnandc_clear_fifo(struct rnandc *rnandc)
{
	writel_relaxed(FIFO_INIT, rnandc->regs + FIFO_INIT_REG);
}

static void rnandc_select_target(struct nand_chip *chip, int die_nr)
{
	struct rnand_chip *rnand = to_rnand(chip);
	struct rnandc *rnandc = to_rnandc(chip->controller);
	unsigned int cs = rnand->sels[die_nr].cs;

	if (chip == rnandc->selected_chip && die_nr == rnand->selected_die)
		return;

	rnandc_clear_status(rnandc);
	writel_relaxed(MEM_CTRL_CS(cs) | MEM_CTRL_DIS_WP(cs), rnandc->regs + MEM_CTRL_REG);
	writel_relaxed(rnand->control, rnandc->regs + CONTROL_REG);
	writel_relaxed(rnand->ecc_ctrl, rnandc->regs + ECC_CTRL_REG);
	writel_relaxed(rnand->timings_asyn, rnandc->regs + TIMINGS_ASYN_REG);
	writel_relaxed(rnand->tim_seq0, rnandc->regs + TIM_SEQ0_REG);
	writel_relaxed(rnand->tim_seq1, rnandc->regs + TIM_SEQ1_REG);
	writel_relaxed(rnand->tim_gen_seq0, rnandc->regs + TIM_GEN_SEQ0_REG);
	writel_relaxed(rnand->tim_gen_seq1, rnandc->regs + TIM_GEN_SEQ1_REG);
	writel_relaxed(rnand->tim_gen_seq2, rnandc->regs + TIM_GEN_SEQ2_REG);
	writel_relaxed(rnand->tim_gen_seq3, rnandc->regs + TIM_GEN_SEQ3_REG);

	rnandc->selected_chip = chip;
	rnand->selected_die = die_nr;
}

static void rnandc_trigger_op(struct rnandc *rnandc, struct rnandc_op *rop)
{
	writel_relaxed(rop->addr0_col, rnandc->regs + ADDR0_COL_REG);
	writel_relaxed(rop->addr0_row, rnandc->regs + ADDR0_ROW_REG);
	writel_relaxed(rop->addr1_col, rnandc->regs + ADDR1_COL_REG);
	writel_relaxed(rop->addr1_row, rnandc->regs + ADDR1_ROW_REG);
	writel_relaxed(rop->ecc_offset, rnandc->regs + ECC_OFFSET_REG);
	writel_relaxed(rop->gen_seq_ctrl, rnandc->regs + GEN_SEQ_CTRL_REG);
	writel_relaxed(DATA_SIZE(rop->len), rnandc->regs + DATA_SIZE_REG);
	writel_relaxed(rop->command, rnandc->regs + COMMAND_REG);
}

static void rnandc_trigger_dma(struct rnandc *rnandc)
{
	writel_relaxed(DMA_CTRL_INCREMENT_BURST_4 |
		       DMA_CTRL_REGISTER_MANAGED_MODE |
		       DMA_CTRL_START, rnandc->regs + DMA_CTRL_REG);
}

static irqreturn_t rnandc_irq_handler(int irq, void *private)
{
	struct rnandc *rnandc = private;

	rnandc_dis_interrupts(rnandc);
	complete(&rnandc->complete);

	return IRQ_HANDLED;
}

static int rnandc_wait_end_of_op(struct rnandc *rnandc,
				 struct nand_chip *chip)
{
	struct rnand_chip *rnand = to_rnand(chip);
	unsigned int cs = to_rnandc_cs(rnand);
	u32 status;
	int ret;

	ret = readl_poll_timeout(rnandc->regs + STATUS_REG, status,
				 MEM_RDY(cs, status) && CTRL_RDY(status),
				 1, 100000);
	if (ret)
		dev_err(rnandc->dev, "Operation timed out, status: 0x%08x\n",
			status);

	return ret;
}

static int rnandc_wait_end_of_io(struct rnandc *rnandc,
				 struct nand_chip *chip)
{
	int timeout_ms = 1000;
	int ret;

	if (rnandc->use_polling) {
		struct rnand_chip *rnand = to_rnand(chip);
		unsigned int cs = to_rnandc_cs(rnand);
		u32 status;

		ret = readl_poll_timeout(rnandc->regs + INT_STATUS_REG, status,
					 MEM_IS_RDY(cs, status) &
					 DMA_HAS_ENDED(status),
					 0, timeout_ms * 1000);
	} else {
		ret = wait_for_completion_timeout(&rnandc->complete,
						  msecs_to_jiffies(timeout_ms));
		if (!ret)
			ret = -ETIMEDOUT;
		else
			ret = 0;
	}

	return ret;
}

static int rnandc_read_page_hw_ecc(struct nand_chip *chip, u8 *buf,
				   int oob_required, int page)
{
	struct rnandc *rnandc = to_rnandc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rnand_chip *rnand = to_rnand(chip);
	unsigned int cs = to_rnandc_cs(rnand);
	struct rnandc_op rop = {
		.command = COMMAND_INPUT_SEL_DMA | COMMAND_0(NAND_CMD_READ0) |
			   COMMAND_2(NAND_CMD_READSTART) | COMMAND_FIFO_SEL |
			   COMMAND_SEQ_READ_PAGE,
		.addr0_row = page,
		.len = mtd->writesize,
		.ecc_offset = ECC_OFFSET(mtd->writesize + 2),
	};
	unsigned int max_bitflips = 0;
	dma_addr_t dma_addr;
	u32 ecc_stat;
	int bf, ret, i;

	/* Prepare controller */
	rnandc_select_target(chip, chip->cur_cs);
	rnandc_clear_status(rnandc);
	reinit_completion(&rnandc->complete);
	rnandc_en_interrupts(rnandc, INT_DMA_ENDED);
	rnandc_en_correction(rnandc);

	/* Configure DMA */
	dma_addr = dma_map_single(rnandc->dev, rnandc->buf, mtd->writesize,
				  DMA_FROM_DEVICE);
	writel(dma_addr, rnandc->regs + DMA_ADDR_LOW_REG);
	writel(mtd->writesize, rnandc->regs + DMA_CNT_REG);
	writel(DMA_TLVL_MAX, rnandc->regs + DMA_TLVL_REG);

	rnandc_trigger_op(rnandc, &rop);
	rnandc_trigger_dma(rnandc);

	ret = rnandc_wait_end_of_io(rnandc, chip);
	dma_unmap_single(rnandc->dev, dma_addr, mtd->writesize, DMA_FROM_DEVICE);
	rnandc_dis_correction(rnandc);
	if (ret) {
		dev_err(rnandc->dev, "Read page operation never ending\n");
		return ret;
	}

	ecc_stat = readl_relaxed(rnandc->regs + ECC_STAT_REG);

	if (oob_required || ECC_STAT_UNCORRECTABLE(cs, ecc_stat)) {
		ret = nand_change_read_column_op(chip, mtd->writesize,
						 chip->oob_poi, mtd->oobsize,
						 false);
		if (ret)
			return ret;
	}

	if (ECC_STAT_UNCORRECTABLE(cs, ecc_stat)) {
		for (i = 0; i < chip->ecc.steps; i++) {
			unsigned int off = i * chip->ecc.size;
			unsigned int eccoff = i * chip->ecc.bytes;

			bf = nand_check_erased_ecc_chunk(rnandc->buf + off,
							 chip->ecc.size,
							 chip->oob_poi + 2 + eccoff,
							 chip->ecc.bytes,
							 NULL, 0,
							 chip->ecc.strength);
			if (bf < 0) {
				mtd->ecc_stats.failed++;
			} else {
				mtd->ecc_stats.corrected += bf;
				max_bitflips = max_t(unsigned int, max_bitflips, bf);
			}
		}
	} else if (ECC_STAT_CORRECTABLE(cs, ecc_stat)) {
		bf = ECC_CNT(cs, readl_relaxed(rnandc->regs + ECC_CNT_REG));
		/*
		 * The number of bitflips is an approximation given the fact
		 * that this controller does not provide per-chunk details but
		 * only gives statistics on the entire page.
		 */
		mtd->ecc_stats.corrected += bf;
	}

	memcpy(buf, rnandc->buf, mtd->writesize);

	return 0;
}

static int rnandc_read_subpage_hw_ecc(struct nand_chip *chip, u32 req_offset,
				      u32 req_len, u8 *bufpoi, int page)
{
	struct rnandc *rnandc = to_rnandc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rnand_chip *rnand = to_rnand(chip);
	unsigned int cs = to_rnandc_cs(rnand);
	unsigned int page_off = round_down(req_offset, chip->ecc.size);
	unsigned int real_len = round_up(req_offset + req_len - page_off,
					 chip->ecc.size);
	unsigned int start_chunk = page_off / chip->ecc.size;
	unsigned int nchunks = real_len / chip->ecc.size;
	unsigned int ecc_off = 2 + (start_chunk * chip->ecc.bytes);
	struct rnandc_op rop = {
		.command = COMMAND_INPUT_SEL_AHBS | COMMAND_0(NAND_CMD_READ0) |
			   COMMAND_2(NAND_CMD_READSTART) | COMMAND_FIFO_SEL |
			   COMMAND_SEQ_READ_PAGE,
		.addr0_row = page,
		.addr0_col = page_off,
		.len = real_len,
		.ecc_offset = ECC_OFFSET(mtd->writesize + ecc_off),
	};
	unsigned int max_bitflips = 0, i;
	u32 ecc_stat;
	int bf, ret;

	/* Prepare controller */
	rnandc_select_target(chip, chip->cur_cs);
	rnandc_clear_status(rnandc);
	rnandc_en_correction(rnandc);
	rnandc_trigger_op(rnandc, &rop);

	while (!FIFO_STATE_C_EMPTY(readl(rnandc->regs + FIFO_STATE_REG)))
		cpu_relax();

	while (FIFO_STATE_R_EMPTY(readl(rnandc->regs + FIFO_STATE_REG)))
		cpu_relax();

	ioread32_rep(rnandc->regs + FIFO_DATA_REG, bufpoi + page_off,
		     real_len / 4);

	if (!FIFO_STATE_R_EMPTY(readl(rnandc->regs + FIFO_STATE_REG))) {
		dev_err(rnandc->dev, "Clearing residual data in the read FIFO\n");
		rnandc_clear_fifo(rnandc);
	}

	ret = rnandc_wait_end_of_op(rnandc, chip);
	rnandc_dis_correction(rnandc);
	if (ret) {
		dev_err(rnandc->dev, "Read subpage operation never ending\n");
		return ret;
	}

	ecc_stat = readl_relaxed(rnandc->regs + ECC_STAT_REG);

	if (ECC_STAT_UNCORRECTABLE(cs, ecc_stat)) {
		ret = nand_change_read_column_op(chip, mtd->writesize,
						 chip->oob_poi, mtd->oobsize,
						 false);
		if (ret)
			return ret;

		for (i = start_chunk; i < nchunks; i++) {
			unsigned int dataoff = i * chip->ecc.size;
			unsigned int eccoff = 2 + (i * chip->ecc.bytes);

			bf = nand_check_erased_ecc_chunk(bufpoi + dataoff,
							 chip->ecc.size,
							 chip->oob_poi + eccoff,
							 chip->ecc.bytes,
							 NULL, 0,
							 chip->ecc.strength);
			if (bf < 0) {
				mtd->ecc_stats.failed++;
			} else {
				mtd->ecc_stats.corrected += bf;
				max_bitflips = max_t(unsigned int, max_bitflips, bf);
			}
		}
	} else if (ECC_STAT_CORRECTABLE(cs, ecc_stat)) {
		bf = ECC_CNT(cs, readl_relaxed(rnandc->regs + ECC_CNT_REG));
		/*
		 * The number of bitflips is an approximation given the fact
		 * that this controller does not provide per-chunk details but
		 * only gives statistics on the entire page.
		 */
		mtd->ecc_stats.corrected += bf;
	}

	return 0;
}

static int rnandc_write_page_hw_ecc(struct nand_chip *chip, const u8 *buf,
				    int oob_required, int page)
{
	struct rnandc *rnandc = to_rnandc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rnand_chip *rnand = to_rnand(chip);
	unsigned int cs = to_rnandc_cs(rnand);
	struct rnandc_op rop = {
		.command = COMMAND_INPUT_SEL_DMA | COMMAND_0(NAND_CMD_SEQIN) |
			   COMMAND_1(NAND_CMD_PAGEPROG) | COMMAND_FIFO_SEL |
			   COMMAND_SEQ_WRITE_PAGE,
		.addr0_row = page,
		.len = mtd->writesize,
		.ecc_offset = ECC_OFFSET(mtd->writesize + 2),
	};
	dma_addr_t dma_addr;
	int ret;

	memcpy(rnandc->buf, buf, mtd->writesize);

	/* Prepare controller */
	rnandc_select_target(chip, chip->cur_cs);
	rnandc_clear_status(rnandc);
	reinit_completion(&rnandc->complete);
	rnandc_en_interrupts(rnandc, INT_MEM_RDY(cs));
	rnandc_en_correction(rnandc);

	/* Configure DMA */
	dma_addr = dma_map_single(rnandc->dev, (void *)rnandc->buf, mtd->writesize,
				  DMA_TO_DEVICE);
	writel(dma_addr, rnandc->regs + DMA_ADDR_LOW_REG);
	writel(mtd->writesize, rnandc->regs + DMA_CNT_REG);
	writel(DMA_TLVL_MAX, rnandc->regs + DMA_TLVL_REG);

	rnandc_trigger_op(rnandc, &rop);
	rnandc_trigger_dma(rnandc);

	ret = rnandc_wait_end_of_io(rnandc, chip);
	dma_unmap_single(rnandc->dev, dma_addr, mtd->writesize, DMA_TO_DEVICE);
	rnandc_dis_correction(rnandc);
	if (ret) {
		dev_err(rnandc->dev, "Write page operation never ending\n");
		return ret;
	}

	if (!oob_required)
		return 0;

	return nand_change_write_column_op(chip, mtd->writesize, chip->oob_poi,
					   mtd->oobsize, false);
}

static int rnandc_write_subpage_hw_ecc(struct nand_chip *chip, u32 req_offset,
				       u32 req_len, const u8 *bufpoi,
				       int oob_required, int page)
{
	struct rnandc *rnandc = to_rnandc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned int page_off = round_down(req_offset, chip->ecc.size);
	unsigned int real_len = round_up(req_offset + req_len - page_off,
					 chip->ecc.size);
	unsigned int start_chunk = page_off / chip->ecc.size;
	unsigned int ecc_off = 2 + (start_chunk * chip->ecc.bytes);
	struct rnandc_op rop = {
		.command = COMMAND_INPUT_SEL_AHBS | COMMAND_0(NAND_CMD_SEQIN) |
			   COMMAND_1(NAND_CMD_PAGEPROG) | COMMAND_FIFO_SEL |
			   COMMAND_SEQ_WRITE_PAGE,
		.addr0_row = page,
		.addr0_col = page_off,
		.len = real_len,
		.ecc_offset = ECC_OFFSET(mtd->writesize + ecc_off),
	};
	int ret;

	/* Prepare controller */
	rnandc_select_target(chip, chip->cur_cs);
	rnandc_clear_status(rnandc);
	rnandc_en_correction(rnandc);
	rnandc_trigger_op(rnandc, &rop);

	while (FIFO_STATE_W_FULL(readl(rnandc->regs + FIFO_STATE_REG)))
		cpu_relax();

	iowrite32_rep(rnandc->regs + FIFO_DATA_REG, bufpoi + page_off,
		      real_len / 4);

	while (!FIFO_STATE_W_EMPTY(readl(rnandc->regs + FIFO_STATE_REG)))
		cpu_relax();

	ret = rnandc_wait_end_of_op(rnandc, chip);
	rnandc_dis_correction(rnandc);
	if (ret) {
		dev_err(rnandc->dev, "Write subpage operation never ending\n");
		return ret;
	}

	return 0;
}

/*
 * This controller is simple enough and thus does not need to use the parser
 * provided by the core, instead, handle every situation here.
 */
static int rnandc_exec_op(struct nand_chip *chip,
			  const struct nand_operation *op, bool check_only)
{
	struct rnandc *rnandc = to_rnandc(chip->controller);
	const struct nand_op_instr *instr = NULL;
	struct rnandc_op rop = {
		.command = COMMAND_INPUT_SEL_AHBS,
		.gen_seq_ctrl = GEN_SEQ_IMD_SEQ,
	};
	unsigned int cmd_phase = 0, addr_phase = 0, data_phase = 0,
		delay_phase = 0, delays = 0;
	unsigned int op_id, col_addrs, row_addrs, naddrs, remainder, words, i;
	const u8 *addrs;
	u32 last_bytes;
	int ret;

	if (!check_only)
		rnandc_select_target(chip, op->cs);

	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		nand_op_trace("  ", instr);

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			switch (cmd_phase++) {
			case 0:
				rop.command |= COMMAND_0(instr->ctx.cmd.opcode);
				rop.gen_seq_ctrl |= GEN_SEQ_CMD0_EN;
				break;
			case 1:
				rop.gen_seq_ctrl |= GEN_SEQ_COMMAND_3(instr->ctx.cmd.opcode);
				rop.gen_seq_ctrl |= GEN_SEQ_CMD3_EN;
				if (addr_phase == 0)
					addr_phase = 1;
				break;
			case 2:
				rop.command |= COMMAND_2(instr->ctx.cmd.opcode);
				rop.gen_seq_ctrl |= GEN_SEQ_CMD2_EN;
				if (addr_phase <= 1)
					addr_phase = 2;
				break;
			case 3:
				rop.command |= COMMAND_1(instr->ctx.cmd.opcode);
				rop.gen_seq_ctrl |= GEN_SEQ_CMD1_EN;
				if (addr_phase <= 1)
					addr_phase = 2;
				if (delay_phase == 0)
					delay_phase = 1;
				if (data_phase == 0)
					data_phase = 1;
				break;
			default:
				return -EOPNOTSUPP;
			}
			break;

		case NAND_OP_ADDR_INSTR:
			addrs = instr->ctx.addr.addrs;
			naddrs = instr->ctx.addr.naddrs;
			if (naddrs > 5)
				return -EOPNOTSUPP;

			col_addrs = min(2U, naddrs);
			row_addrs = naddrs > 2 ? naddrs - col_addrs : 0;

			switch (addr_phase++) {
			case 0:
				for (i = 0; i < col_addrs; i++)
					rop.addr0_col |= addrs[i] << (i * 8);
				rop.gen_seq_ctrl |= GEN_SEQ_COL_A0(col_addrs);

				for (i = 0; i < row_addrs; i++)
					rop.addr0_row |= addrs[2 + i] << (i * 8);
				rop.gen_seq_ctrl |= GEN_SEQ_ROW_A0(row_addrs);

				if (cmd_phase == 0)
					cmd_phase = 1;
				break;
			case 1:
				for (i = 0; i < col_addrs; i++)
					rop.addr1_col |= addrs[i] << (i * 8);
				rop.gen_seq_ctrl |= GEN_SEQ_COL_A1(col_addrs);

				for (i = 0; i < row_addrs; i++)
					rop.addr1_row |= addrs[2 + i] << (i * 8);
				rop.gen_seq_ctrl |= GEN_SEQ_ROW_A1(row_addrs);

				if (cmd_phase <= 1)
					cmd_phase = 2;
				break;
			default:
				return -EOPNOTSUPP;
			}
			break;

		case NAND_OP_DATA_IN_INSTR:
			rop.read = true;
			fallthrough;
		case NAND_OP_DATA_OUT_INSTR:
			rop.gen_seq_ctrl |= GEN_SEQ_DATA_EN;
			rop.buf = instr->ctx.data.buf.in;
			rop.len = instr->ctx.data.len;
			rop.command |= COMMAND_FIFO_SEL;

			switch (data_phase++) {
			case 0:
				if (cmd_phase <= 2)
					cmd_phase = 3;
				if (addr_phase <= 1)
					addr_phase = 2;
				if (delay_phase == 0)
					delay_phase = 1;
				break;
			default:
				return -EOPNOTSUPP;
			}
			break;

		case NAND_OP_WAITRDY_INSTR:
			switch (delay_phase++) {
			case 0:
				rop.gen_seq_ctrl |= GEN_SEQ_DELAY0_EN;

				if (cmd_phase <= 2)
					cmd_phase = 3;
				break;
			case 1:
				rop.gen_seq_ctrl |= GEN_SEQ_DELAY1_EN;

				if (cmd_phase <= 3)
					cmd_phase = 4;
				if (data_phase == 0)
					data_phase = 1;
				break;
			default:
				return -EOPNOTSUPP;
			}
			break;
		}
	}

	/*
	 * Sequence 19 is generic and dedicated to write operations.
	 * Sequence 18 is also generic and works for all other operations.
	 */
	if (rop.buf && !rop.read)
		rop.command |= COMMAND_SEQ_GEN_OUT;
	else
		rop.command |= COMMAND_SEQ_GEN_IN;

	if (delays > 1) {
		dev_err(rnandc->dev, "Cannot handle more than one wait delay\n");
		return -EOPNOTSUPP;
	}

	if (check_only)
		return 0;

	rnandc_trigger_op(rnandc, &rop);

	words = rop.len / sizeof(u32);
	remainder = rop.len % sizeof(u32);
	if (rop.buf && rop.read) {
		while (!FIFO_STATE_C_EMPTY(readl(rnandc->regs + FIFO_STATE_REG)))
			cpu_relax();

		while (FIFO_STATE_R_EMPTY(readl(rnandc->regs + FIFO_STATE_REG)))
			cpu_relax();

		ioread32_rep(rnandc->regs + FIFO_DATA_REG, rop.buf, words);
		if (remainder) {
			last_bytes = readl_relaxed(rnandc->regs + FIFO_DATA_REG);
			memcpy(rop.buf + (words * sizeof(u32)), &last_bytes,
			       remainder);
		}

		if (!FIFO_STATE_R_EMPTY(readl(rnandc->regs + FIFO_STATE_REG))) {
			dev_warn(rnandc->dev,
				 "Clearing residual data in the read FIFO\n");
			rnandc_clear_fifo(rnandc);
		}
	} else if (rop.len && !rop.read) {
		while (FIFO_STATE_W_FULL(readl(rnandc->regs + FIFO_STATE_REG)))
			cpu_relax();

		iowrite32_rep(rnandc->regs + FIFO_DATA_REG, rop.buf,
			      DIV_ROUND_UP(rop.len, 4));

		if (remainder) {
			last_bytes = 0;
			memcpy(&last_bytes, rop.buf + (words * sizeof(u32)), remainder);
			writel_relaxed(last_bytes, rnandc->regs + FIFO_DATA_REG);
		}

		while (!FIFO_STATE_W_EMPTY(readl(rnandc->regs + FIFO_STATE_REG)))
			cpu_relax();
	}

	ret = rnandc_wait_end_of_op(rnandc, chip);
	if (ret)
		return ret;

	return 0;
}

static int rnandc_setup_interface(struct nand_chip *chip, int chipnr,
				  const struct nand_interface_config *conf)
{
	struct rnand_chip *rnand = to_rnand(chip);
	struct rnandc *rnandc = to_rnandc(chip->controller);
	unsigned int period_ns = 1000000000 / rnandc->ext_clk_rate;
	const struct nand_sdr_timings *sdr;
	unsigned int cyc, cle, ale, bef_dly, ca_to_data;

	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	if (sdr->tRP_min != sdr->tWP_min || sdr->tREH_min != sdr->tWH_min) {
		dev_err(rnandc->dev, "Read and write hold times must be identical\n");
		return -EINVAL;
	}

	if (chipnr < 0)
		return 0;

	rnand->timings_asyn =
		TIMINGS_ASYN_TRWP(TO_CYCLES64(sdr->tRP_min, period_ns)) |
		TIMINGS_ASYN_TRWH(TO_CYCLES64(sdr->tREH_min, period_ns));
	rnand->tim_seq0 =
		TIM_SEQ0_TCCS(TO_CYCLES64(sdr->tCCS_min, period_ns)) |
		TIM_SEQ0_TADL(TO_CYCLES64(sdr->tADL_min, period_ns)) |
		TIM_SEQ0_TRHW(TO_CYCLES64(sdr->tRHW_min, period_ns)) |
		TIM_SEQ0_TWHR(TO_CYCLES64(sdr->tWHR_min, period_ns));
	rnand->tim_seq1 =
		TIM_SEQ1_TWB(TO_CYCLES64(sdr->tWB_max, period_ns)) |
		TIM_SEQ1_TRR(TO_CYCLES64(sdr->tRR_min, period_ns)) |
		TIM_SEQ1_TWW(TO_CYCLES64(sdr->tWW_min, period_ns));

	cyc = sdr->tDS_min + sdr->tDH_min;
	cle = sdr->tCLH_min + sdr->tCLS_min;
	ale = sdr->tALH_min + sdr->tALS_min;
	bef_dly = sdr->tWB_max - sdr->tDH_min;
	ca_to_data = sdr->tWHR_min + sdr->tREA_max - sdr->tDH_min;

	/*
	 * D0 = CMD -> ADDR = tCLH + tCLS - 1 cycle
	 * D1 = CMD -> CMD = tCLH + tCLS - 1 cycle
	 * D2 = CMD -> DLY = tWB - tDH
	 * D3 = CMD -> DATA = tWHR + tREA - tDH
	 */
	rnand->tim_gen_seq0 =
		TIM_GEN_SEQ0_D0(TO_CYCLES64(cle - cyc, period_ns)) |
		TIM_GEN_SEQ0_D1(TO_CYCLES64(cle - cyc, period_ns)) |
		TIM_GEN_SEQ0_D2(TO_CYCLES64(bef_dly, period_ns)) |
		TIM_GEN_SEQ0_D3(TO_CYCLES64(ca_to_data, period_ns));

	/*
	 * D4 = ADDR -> CMD = tALH + tALS - 1 cyle
	 * D5 = ADDR -> ADDR = tALH + tALS - 1 cyle
	 * D6 = ADDR -> DLY = tWB - tDH
	 * D7 = ADDR -> DATA = tWHR + tREA - tDH
	 */
	rnand->tim_gen_seq1 =
		TIM_GEN_SEQ1_D4(TO_CYCLES64(ale - cyc, period_ns)) |
		TIM_GEN_SEQ1_D5(TO_CYCLES64(ale - cyc, period_ns)) |
		TIM_GEN_SEQ1_D6(TO_CYCLES64(bef_dly, period_ns)) |
		TIM_GEN_SEQ1_D7(TO_CYCLES64(ca_to_data, period_ns));

	/*
	 * D8 = DLY -> DATA = tRR + tREA
	 * D9 = DLY -> CMD = tRR
	 * D10 = DATA -> CMD = tCLH + tCLS - 1 cycle
	 * D11 = DATA -> DLY = tWB - tDH
	 */
	rnand->tim_gen_seq2 =
		TIM_GEN_SEQ2_D8(TO_CYCLES64(sdr->tRR_min + sdr->tREA_max, period_ns)) |
		TIM_GEN_SEQ2_D9(TO_CYCLES64(sdr->tRR_min, period_ns)) |
		TIM_GEN_SEQ2_D10(TO_CYCLES64(cle - cyc, period_ns)) |
		TIM_GEN_SEQ2_D11(TO_CYCLES64(bef_dly, period_ns));

	/* D12 = DATA -> END = tCLH - tDH */
	rnand->tim_gen_seq3 =
		TIM_GEN_SEQ3_D12(TO_CYCLES64(sdr->tCLH_min - sdr->tDH_min, period_ns));

	return 0;
}

static int rnandc_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	unsigned int eccbytes = round_up(chip->ecc.bytes, 4) * chip->ecc.steps;

	if (section)
		return -ERANGE;

	oobregion->offset = 2;
	oobregion->length = eccbytes;

	return 0;
}

static int rnandc_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	unsigned int eccbytes = round_up(chip->ecc.bytes, 4) * chip->ecc.steps;

	if (section)
		return -ERANGE;

	oobregion->offset = 2 + eccbytes;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops rnandc_ooblayout_ops = {
	.ecc = rnandc_ooblayout_ecc,
	.free = rnandc_ooblayout_free,
};

static int rnandc_hw_ecc_controller_init(struct nand_chip *chip)
{
	struct rnand_chip *rnand = to_rnand(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct rnandc *rnandc = to_rnandc(chip->controller);

	if (mtd->writesize > SZ_16K) {
		dev_err(rnandc->dev, "Unsupported page size\n");
		return -EINVAL;
	}

	switch (chip->ecc.size) {
	case SZ_256:
		rnand->control |= CONTROL_ECC_BLOCK_SIZE_256;
		break;
	case SZ_512:
		rnand->control |= CONTROL_ECC_BLOCK_SIZE_512;
		break;
	case SZ_1K:
		rnand->control |= CONTROL_ECC_BLOCK_SIZE_1024;
		break;
	default:
		dev_err(rnandc->dev, "Unsupported ECC chunk size\n");
		return -EINVAL;
	}

	switch (chip->ecc.strength) {
	case 2:
		chip->ecc.bytes = 4;
		rnand->ecc_ctrl |= ECC_CTRL_CAP_2B;
		break;
	case 4:
		chip->ecc.bytes = 7;
		rnand->ecc_ctrl |= ECC_CTRL_CAP_4B;
		break;
	case 8:
		chip->ecc.bytes = 14;
		rnand->ecc_ctrl |= ECC_CTRL_CAP_8B;
		break;
	case 16:
		chip->ecc.bytes = 28;
		rnand->ecc_ctrl |= ECC_CTRL_CAP_16B;
		break;
	case 24:
		chip->ecc.bytes = 42;
		rnand->ecc_ctrl |= ECC_CTRL_CAP_24B;
		break;
	case 32:
		chip->ecc.bytes = 56;
		rnand->ecc_ctrl |= ECC_CTRL_CAP_32B;
		break;
	default:
		dev_err(rnandc->dev, "Unsupported ECC strength\n");
		return -EINVAL;
	}

	rnand->ecc_ctrl |= ECC_CTRL_ERR_THRESHOLD(chip->ecc.strength);

	mtd_set_ooblayout(mtd, &rnandc_ooblayout_ops);
	chip->ecc.steps = mtd->writesize / chip->ecc.size;
	chip->ecc.read_page = rnandc_read_page_hw_ecc;
	chip->ecc.read_subpage = rnandc_read_subpage_hw_ecc;
	chip->ecc.write_page = rnandc_write_page_hw_ecc;
	chip->ecc.write_subpage = rnandc_write_subpage_hw_ecc;

	return 0;
}

static int rnandc_ecc_init(struct nand_chip *chip)
{
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	const struct nand_ecc_props *requirements =
		nanddev_get_ecc_requirements(&chip->base);
	struct rnandc *rnandc = to_rnandc(chip->controller);
	int ret;

	if (ecc->engine_type != NAND_ECC_ENGINE_TYPE_NONE &&
	    (!ecc->size || !ecc->strength)) {
		if (requirements->step_size && requirements->strength) {
			ecc->size = requirements->step_size;
			ecc->strength = requirements->strength;
		} else {
			dev_err(rnandc->dev, "No minimum ECC strength\n");
			return -EINVAL;
		}
	}

	switch (ecc->engine_type) {
	case NAND_ECC_ENGINE_TYPE_ON_HOST:
		ret = rnandc_hw_ecc_controller_init(chip);
		if (ret)
			return ret;
		break;
	case NAND_ECC_ENGINE_TYPE_NONE:
	case NAND_ECC_ENGINE_TYPE_SOFT:
	case NAND_ECC_ENGINE_TYPE_ON_DIE:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rnandc_attach_chip(struct nand_chip *chip)
{
	struct rnand_chip *rnand = to_rnand(chip);
	struct rnandc *rnandc = to_rnandc(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_memory_organization *memorg = nanddev_get_memorg(&chip->base);
	int ret;

	/* Do not store BBT bits in the OOB section as it is not protected */
	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	if (mtd->writesize <= 512) {
		dev_err(rnandc->dev, "Small page devices not supported\n");
		return -EINVAL;
	}

	rnand->control |= CONTROL_CHECK_RB_LINE | CONTROL_INT_EN;

	switch (memorg->pages_per_eraseblock) {
	case 32:
		rnand->control |= CONTROL_BLOCK_SIZE_32P;
		break;
	case 64:
		rnand->control |= CONTROL_BLOCK_SIZE_64P;
		break;
	case 128:
		rnand->control |= CONTROL_BLOCK_SIZE_128P;
		break;
	case 256:
		rnand->control |= CONTROL_BLOCK_SIZE_256P;
		break;
	default:
		dev_err(rnandc->dev, "Unsupported memory organization\n");
		return -EINVAL;
	}

	chip->options |= NAND_SUBPAGE_READ;

	ret = rnandc_ecc_init(chip);
	if (ret) {
		dev_err(rnandc->dev, "ECC initialization failed (%d)\n", ret);
		return ret;
	}

	/* Force an update of the configuration registers */
	rnand->selected_die = -1;

	return 0;
}

static const struct nand_controller_ops rnandc_ops = {
	.attach_chip = rnandc_attach_chip,
	.exec_op = rnandc_exec_op,
	.setup_interface = rnandc_setup_interface,
};

static int rnandc_alloc_dma_buf(struct rnandc *rnandc,
				struct mtd_info *new_mtd)
{
	unsigned int max_len = new_mtd->writesize + new_mtd->oobsize;
	struct rnand_chip *entry, *temp;
	struct nand_chip *chip;
	struct mtd_info *mtd;

	list_for_each_entry_safe(entry, temp, &rnandc->chips, node) {
		chip = &entry->chip;
		mtd = nand_to_mtd(chip);
		max_len = max(max_len, mtd->writesize + mtd->oobsize);
	}

	if (rnandc->buf && rnandc->buf_sz < max_len) {
		devm_kfree(rnandc->dev, rnandc->buf);
		rnandc->buf = NULL;
	}

	if (!rnandc->buf) {
		rnandc->buf_sz = max_len;
		rnandc->buf = devm_kmalloc(rnandc->dev, max_len,
					   GFP_KERNEL | GFP_DMA);
		if (!rnandc->buf)
			return -ENOMEM;
	}

	return 0;
}

static int rnandc_chip_init(struct rnandc *rnandc, struct device_node *np)
{
	struct rnand_chip *rnand;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int nsels, ret, i;
	u32 cs;

	nsels = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (nsels <= 0) {
		ret = (nsels < 0) ? nsels : -EINVAL;
		dev_err(rnandc->dev, "Invalid reg property (%d)\n", ret);
		return ret;
	}

	/* Alloc the driver's NAND chip structure */
	rnand = devm_kzalloc(rnandc->dev, struct_size(rnand, sels, nsels),
			     GFP_KERNEL);
	if (!rnand)
		return -ENOMEM;

	rnand->nsels = nsels;
	rnand->selected_die = -1;

	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(np, "reg", i, &cs);
		if (ret) {
			dev_err(rnandc->dev, "Incomplete reg property (%d)\n", ret);
			return ret;
		}

		if (cs >= RNANDC_CS_NUM) {
			dev_err(rnandc->dev, "Invalid reg property (%d)\n", cs);
			return -EINVAL;
		}

		if (test_and_set_bit(cs, &rnandc->assigned_cs)) {
			dev_err(rnandc->dev, "CS %d already assigned\n", cs);
			return -EINVAL;
		}

		/*
		 * No need to check for RB or WP properties, there is a 1:1
		 * mandatory mapping with the CS.
		 */
		rnand->sels[i].cs = cs;
	}

	chip = &rnand->chip;
	chip->controller = &rnandc->controller;
	nand_set_flash_node(chip, np);

	mtd = nand_to_mtd(chip);
	mtd->dev.parent = rnandc->dev;
	if (!mtd->name) {
		dev_err(rnandc->dev, "Missing MTD label\n");
		return -EINVAL;
	}

	ret = nand_scan(chip, rnand->nsels);
	if (ret) {
		dev_err(rnandc->dev, "Failed to scan the NAND chip (%d)\n", ret);
		return ret;
	}

	ret = rnandc_alloc_dma_buf(rnandc, mtd);
	if (ret)
		goto cleanup_nand;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(rnandc->dev, "Failed to register MTD device (%d)\n", ret);
		goto cleanup_nand;
	}

	list_add_tail(&rnand->node, &rnandc->chips);

	return 0;

cleanup_nand:
	nand_cleanup(chip);

	return ret;
}

static void rnandc_chips_cleanup(struct rnandc *rnandc)
{
	struct rnand_chip *entry, *temp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(entry, temp, &rnandc->chips, node) {
		chip = &entry->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&entry->node);
	}
}

static int rnandc_chips_init(struct rnandc *rnandc)
{
	int ret;

	for_each_child_of_node_scoped(rnandc->dev->of_node, np) {
		ret = rnandc_chip_init(rnandc, np);
		if (ret) {
			rnandc_chips_cleanup(rnandc);
			return ret;
		}
	}

	return 0;
}

static int rnandc_probe(struct platform_device *pdev)
{
	struct rnandc *rnandc;
	struct clk *eclk;
	int irq, ret;

	rnandc = devm_kzalloc(&pdev->dev, sizeof(*rnandc), GFP_KERNEL);
	if (!rnandc)
		return -ENOMEM;

	rnandc->dev = &pdev->dev;
	nand_controller_init(&rnandc->controller);
	rnandc->controller.ops = &rnandc_ops;
	INIT_LIST_HEAD(&rnandc->chips);
	init_completion(&rnandc->complete);

	rnandc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rnandc->regs))
		return PTR_ERR(rnandc->regs);

	devm_pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	/* The external NAND bus clock rate is needed for computing timings */
	eclk = clk_get(&pdev->dev, "eclk");
	if (IS_ERR(eclk)) {
		ret = PTR_ERR(eclk);
		goto dis_runtime_pm;
	}

	rnandc->ext_clk_rate = clk_get_rate(eclk);
	clk_put(eclk);

	rnandc_dis_interrupts(rnandc);
	irq = platform_get_irq_optional(pdev, 0);
	if (irq == -EPROBE_DEFER) {
		ret = irq;
		goto dis_runtime_pm;
	} else if (irq < 0) {
		dev_info(&pdev->dev, "No IRQ found, fallback to polling\n");
		rnandc->use_polling = true;
	} else {
		ret = devm_request_irq(&pdev->dev, irq, rnandc_irq_handler, 0,
				       "renesas-nand-controller", rnandc);
		if (ret < 0)
			goto dis_runtime_pm;
	}

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto dis_runtime_pm;

	rnandc_clear_fifo(rnandc);

	platform_set_drvdata(pdev, rnandc);

	ret = rnandc_chips_init(rnandc);
	if (ret)
		goto dis_runtime_pm;

	return 0;

dis_runtime_pm:
	pm_runtime_put(&pdev->dev);

	return ret;
}

static void rnandc_remove(struct platform_device *pdev)
{
	struct rnandc *rnandc = platform_get_drvdata(pdev);

	rnandc_chips_cleanup(rnandc);

	pm_runtime_put(&pdev->dev);
}

static const struct of_device_id rnandc_id_table[] = {
	{ .compatible = "renesas,rcar-gen3-nandc" },
	{ .compatible = "renesas,rzn1-nandc" },
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, rnandc_id_table);

static struct platform_driver rnandc_driver = {
	.driver = {
		.name = "renesas-nandc",
		.of_match_table = rnandc_id_table,
	},
	.probe = rnandc_probe,
	.remove_new = rnandc_remove,
};
module_platform_driver(rnandc_driver);

MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("Renesas R-Car Gen3 & RZ/N1 NAND controller driver");
MODULE_LICENSE("GPL v2");

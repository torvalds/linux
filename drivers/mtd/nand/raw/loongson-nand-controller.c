// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NAND Controller Driver for Loongson family chips
 *
 * Copyright (C) 2015-2025 Keguang Zhang <keguang.zhang@gmail.com>
 * Copyright (C) 2025 Binbin Zhou <zhoubinbin@loongson.cn>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sizes.h>

/* Loongson NAND Controller Registers */
#define LOONGSON_NAND_CMD		0x0
#define LOONGSON_NAND_ADDR1		0x4
#define LOONGSON_NAND_ADDR2		0x8
#define LOONGSON_NAND_TIMING		0xc
#define LOONGSON_NAND_IDL		0x10
#define LOONGSON_NAND_IDH_STATUS	0x14
#define LOONGSON_NAND_PARAM		0x18
#define LOONGSON_NAND_OP_NUM		0x1c
#define LOONGSON_NAND_CS_RDY_MAP	0x20

/* Bitfields of nand command register */
#define LOONGSON_NAND_CMD_OP_DONE	BIT(10)
#define LOONGSON_NAND_CMD_OP_SPARE	BIT(9)
#define LOONGSON_NAND_CMD_OP_MAIN	BIT(8)
#define LOONGSON_NAND_CMD_STATUS	BIT(7)
#define LOONGSON_NAND_CMD_RESET		BIT(6)
#define LOONGSON_NAND_CMD_READID	BIT(5)
#define LOONGSON_NAND_CMD_BLOCKS_ERASE	BIT(4)
#define LOONGSON_NAND_CMD_ERASE		BIT(3)
#define LOONGSON_NAND_CMD_WRITE		BIT(2)
#define LOONGSON_NAND_CMD_READ		BIT(1)
#define LOONGSON_NAND_CMD_VALID		BIT(0)

/* Bitfields of nand cs/rdy map register */
#define LOONGSON_NAND_MAP_CS1_SEL	GENMASK(11, 8)
#define LOONGSON_NAND_MAP_RDY1_SEL	GENMASK(15, 12)
#define LOONGSON_NAND_MAP_CS2_SEL	GENMASK(19, 16)
#define LOONGSON_NAND_MAP_RDY2_SEL	GENMASK(23, 20)
#define LOONGSON_NAND_MAP_CS3_SEL	GENMASK(27, 24)
#define LOONGSON_NAND_MAP_RDY3_SEL	GENMASK(31, 28)

#define LOONGSON_NAND_CS_SEL0		BIT(0)
#define LOONGSON_NAND_CS_SEL1		BIT(1)
#define LOONGSON_NAND_CS_SEL2		BIT(2)
#define LOONGSON_NAND_CS_SEL3		BIT(3)
#define LOONGSON_NAND_CS_RDY0		BIT(0)
#define LOONGSON_NAND_CS_RDY1		BIT(1)
#define LOONGSON_NAND_CS_RDY2		BIT(2)
#define LOONGSON_NAND_CS_RDY3		BIT(3)

/* Bitfields of nand timing register */
#define LOONGSON_NAND_WAIT_CYCLE_MASK	GENMASK(7, 0)
#define LOONGSON_NAND_HOLD_CYCLE_MASK	GENMASK(15, 8)

/* Bitfields of nand parameter register */
#define LOONGSON_NAND_CELL_SIZE_MASK	GENMASK(11, 8)

#define LOONGSON_NAND_COL_ADDR_CYC	2U
#define LOONGSON_NAND_MAX_ADDR_CYC	5U

#define LOONGSON_NAND_READ_ID_SLEEP_US		1000
#define LOONGSON_NAND_READ_ID_TIMEOUT_US	5000

#define BITS_PER_WORD			(4 * BITS_PER_BYTE)

/* Loongson-2K1000 NAND DMA routing register */
#define LS2K1000_NAND_DMA_MASK         GENMASK(2, 0)
#define LS2K1000_DMA0_CONF             0x0
#define LS2K1000_DMA1_CONF             0x1
#define LS2K1000_DMA2_CONF             0x2
#define LS2K1000_DMA3_CONF             0x3
#define LS2K1000_DMA4_CONF             0x4

struct loongson_nand_host;

struct loongson_nand_op {
	char addrs[LOONGSON_NAND_MAX_ADDR_CYC];
	unsigned int naddrs;
	unsigned int addrs_offset;
	unsigned int aligned_offset;
	unsigned int cmd_reg;
	unsigned int row_start;
	unsigned int rdy_timeout_ms;
	unsigned int orig_len;
	bool is_readid;
	bool is_erase;
	bool is_write;
	bool is_read;
	bool is_change_column;
	size_t len;
	char *buf;
};

struct loongson_nand_data {
	unsigned int max_id_cycle;
	unsigned int id_cycle_field;
	unsigned int status_field;
	unsigned int op_scope_field;
	unsigned int hold_cycle;
	unsigned int wait_cycle;
	unsigned int nand_cs;
	unsigned int dma_bits;
	int (*dma_config)(struct device *dev);
	void (*set_addr)(struct loongson_nand_host *host, struct loongson_nand_op *op);
};

struct loongson_nand_host {
	struct device *dev;
	struct nand_chip chip;
	struct nand_controller controller;
	const struct loongson_nand_data *data;
	unsigned int addr_cs_field;
	void __iomem *reg_base;
	struct regmap *regmap;
	/* DMA Engine stuff */
	dma_addr_t dma_base;
	struct dma_chan *dma_chan;
	dma_cookie_t dma_cookie;
	struct completion dma_complete;
};

static const struct regmap_config loongson_nand_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int loongson_nand_op_cmd_mapping(struct nand_chip *chip, struct loongson_nand_op *op,
					u8 opcode)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);

	op->row_start = chip->page_shift + 1;

	/* The controller abstracts the following NAND operations. */
	switch (opcode) {
	case NAND_CMD_STATUS:
		op->cmd_reg = LOONGSON_NAND_CMD_STATUS;
		break;
	case NAND_CMD_RESET:
		op->cmd_reg = LOONGSON_NAND_CMD_RESET;
		break;
	case NAND_CMD_READID:
		op->is_readid = true;
		op->cmd_reg = LOONGSON_NAND_CMD_READID;
		break;
	case NAND_CMD_ERASE1:
		op->is_erase = true;
		op->addrs_offset = LOONGSON_NAND_COL_ADDR_CYC;
		break;
	case NAND_CMD_ERASE2:
		if (!op->is_erase)
			return -EOPNOTSUPP;
		/* During erasing, row_start differs from the default value. */
		op->row_start = chip->page_shift;
		op->cmd_reg = LOONGSON_NAND_CMD_ERASE;
		break;
	case NAND_CMD_SEQIN:
		op->is_write = true;
		break;
	case NAND_CMD_PAGEPROG:
		if (!op->is_write)
			return -EOPNOTSUPP;
		op->cmd_reg = LOONGSON_NAND_CMD_WRITE;
		break;
	case NAND_CMD_READ0:
		op->is_read = true;
		break;
	case NAND_CMD_READSTART:
		if (!op->is_read)
			return -EOPNOTSUPP;
		op->cmd_reg = LOONGSON_NAND_CMD_READ;
		break;
	case NAND_CMD_RNDOUT:
		op->is_change_column = true;
		break;
	case NAND_CMD_RNDOUTSTART:
		if (!op->is_change_column)
			return -EOPNOTSUPP;
		op->cmd_reg = LOONGSON_NAND_CMD_READ;
		break;
	default:
		dev_dbg(host->dev, "unsupported opcode: %u\n", opcode);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int loongson_nand_parse_instructions(struct nand_chip *chip, const struct nand_subop *subop,
					    struct loongson_nand_op *op)
{
	unsigned int op_id;
	int ret;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		const struct nand_op_instr *instr = &subop->instrs[op_id];
		unsigned int offset, naddrs;
		const u8 *addrs;

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			ret = loongson_nand_op_cmd_mapping(chip, op, instr->ctx.cmd.opcode);
			if (ret < 0)
				return ret;

			break;
		case NAND_OP_ADDR_INSTR:
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			if (naddrs > LOONGSON_NAND_MAX_ADDR_CYC)
				return -EOPNOTSUPP;
			op->naddrs = naddrs;
			offset = nand_subop_get_addr_start_off(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];
			memcpy(op->addrs + op->addrs_offset, addrs, naddrs);
			break;
		case NAND_OP_DATA_IN_INSTR:
		case NAND_OP_DATA_OUT_INSTR:
			offset = nand_subop_get_data_start_off(subop, op_id);
			op->orig_len = nand_subop_get_data_len(subop, op_id);
			if (instr->type == NAND_OP_DATA_IN_INSTR)
				op->buf = instr->ctx.data.buf.in + offset;
			else if (instr->type == NAND_OP_DATA_OUT_INSTR)
				op->buf = (void *)instr->ctx.data.buf.out + offset;

			break;
		case NAND_OP_WAITRDY_INSTR:
			op->rdy_timeout_ms = instr->ctx.waitrdy.timeout_ms;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void loongson_nand_set_addr_cs(struct loongson_nand_host *host)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);

	if (!host->data->nand_cs)
		return;

	/*
	 * The Manufacturer/Chip ID read operation precedes attach_chip, at which point
	 * information such as NAND chip selection and capacity is unknown. As a
	 * workaround, we use 128MB cellsize (2KB pagesize) as a fallback.
	 */
	if (!mtd->writesize)
		host->addr_cs_field = GENMASK(17, 16);

	regmap_update_bits(host->regmap, LOONGSON_NAND_ADDR2, host->addr_cs_field,
			   host->data->nand_cs << __ffs(host->addr_cs_field));
}

static void ls1b_nand_set_addr(struct loongson_nand_host *host, struct loongson_nand_op *op)
{
	struct nand_chip *chip = &host->chip;
	int i;

	for (i = 0; i < LOONGSON_NAND_MAX_ADDR_CYC; i++) {
		int shift, mask, val;

		if (i < LOONGSON_NAND_COL_ADDR_CYC) {
			shift = i * BITS_PER_BYTE;
			mask = (u32)0xff << shift;
			mask &= GENMASK(chip->page_shift, 0);
			val = (u32)op->addrs[i] << shift;
			regmap_update_bits(host->regmap, LOONGSON_NAND_ADDR1, mask, val);
		} else if (!op->is_change_column) {
			shift = op->row_start + (i - LOONGSON_NAND_COL_ADDR_CYC) * BITS_PER_BYTE;
			mask = (u32)0xff << shift;
			val = (u32)op->addrs[i] << shift;
			regmap_update_bits(host->regmap, LOONGSON_NAND_ADDR1, mask, val);

			if (i == 4) {
				mask = (u32)0xff >> (BITS_PER_WORD - shift);
				val = (u32)op->addrs[i] >> (BITS_PER_WORD - shift);
				regmap_update_bits(host->regmap, LOONGSON_NAND_ADDR2, mask, val);
			}
		}
	}
}

static void ls1c_nand_set_addr(struct loongson_nand_host *host, struct loongson_nand_op *op)
{
	int i;

	for (i = 0; i < LOONGSON_NAND_MAX_ADDR_CYC; i++) {
		int shift, mask, val;

		if (i < LOONGSON_NAND_COL_ADDR_CYC) {
			shift = i * BITS_PER_BYTE;
			mask = (u32)0xff << shift;
			val = (u32)op->addrs[i] << shift;
			regmap_update_bits(host->regmap, LOONGSON_NAND_ADDR1, mask, val);
		} else if (!op->is_change_column) {
			shift = (i - LOONGSON_NAND_COL_ADDR_CYC) * BITS_PER_BYTE;
			mask = (u32)0xff << shift;
			val = (u32)op->addrs[i] << shift;
			regmap_update_bits(host->regmap, LOONGSON_NAND_ADDR2, mask, val);
		}
	}

	loongson_nand_set_addr_cs(host);
}

static void loongson_nand_trigger_op(struct loongson_nand_host *host, struct loongson_nand_op *op)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int col0 = op->addrs[0];
	short col;

	if (!IS_ALIGNED(col0, chip->buf_align)) {
		col0 = ALIGN_DOWN(op->addrs[0], chip->buf_align);
		op->aligned_offset = op->addrs[0] - col0;
		op->addrs[0] = col0;
	}

	if (host->data->set_addr)
		host->data->set_addr(host, op);

	/* set operation length */
	if (op->is_write || op->is_read || op->is_change_column)
		op->len = ALIGN(op->orig_len + op->aligned_offset, chip->buf_align);
	else if (op->is_erase)
		op->len = 1;
	else
		op->len = op->orig_len;

	writel(op->len, host->reg_base + LOONGSON_NAND_OP_NUM);

	/* set operation area and scope */
	col = op->addrs[1] << BITS_PER_BYTE | op->addrs[0];
	if (op->orig_len && !op->is_readid) {
		unsigned int op_scope = 0;

		if (col < mtd->writesize) {
			op->cmd_reg |= LOONGSON_NAND_CMD_OP_MAIN;
			op_scope = mtd->writesize;
		}

		op->cmd_reg |= LOONGSON_NAND_CMD_OP_SPARE;
		op_scope += mtd->oobsize;

		op_scope <<= __ffs(host->data->op_scope_field);
		regmap_update_bits(host->regmap, LOONGSON_NAND_PARAM,
				   host->data->op_scope_field, op_scope);
	}

	/* set command */
	writel(op->cmd_reg, host->reg_base + LOONGSON_NAND_CMD);

	/* trigger operation */
	regmap_write_bits(host->regmap, LOONGSON_NAND_CMD, LOONGSON_NAND_CMD_VALID,
			  LOONGSON_NAND_CMD_VALID);
}

static int loongson_nand_wait_for_op_done(struct loongson_nand_host *host,
					  struct loongson_nand_op *op)
{
	unsigned int val;
	int ret = 0;

	if (op->rdy_timeout_ms) {
		ret = regmap_read_poll_timeout(host->regmap, LOONGSON_NAND_CMD,
					       val, val & LOONGSON_NAND_CMD_OP_DONE,
					       0, op->rdy_timeout_ms * MSEC_PER_SEC);
		if (ret)
			dev_err(host->dev, "operation failed\n");
	}

	return ret;
}

static void loongson_nand_dma_callback(void *data)
{
	struct loongson_nand_host *host = (struct loongson_nand_host *)data;
	struct dma_chan *chan = host->dma_chan;
	struct device *dev = chan->device->dev;
	enum dma_status status;

	status = dmaengine_tx_status(chan, host->dma_cookie, NULL);
	if (likely(status == DMA_COMPLETE)) {
		dev_dbg(dev, "DMA complete with cookie=%d\n", host->dma_cookie);
		complete(&host->dma_complete);
	} else {
		dev_err(dev, "DMA error with cookie=%d\n", host->dma_cookie);
	}
}

static int loongson_nand_dma_transfer(struct loongson_nand_host *host, struct loongson_nand_op *op)
{
	struct nand_chip *chip = &host->chip;
	struct dma_chan *chan = host->dma_chan;
	struct device *dev = chan->device->dev;
	struct dma_async_tx_descriptor *desc;
	enum dma_data_direction data_dir = op->is_write ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	enum dma_transfer_direction xfer_dir = op->is_write ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	void *buf = op->buf;
	char *dma_buf = NULL;
	dma_addr_t dma_addr;
	int ret;

	if (IS_ALIGNED((uintptr_t)buf, chip->buf_align) &&
	    IS_ALIGNED(op->orig_len, chip->buf_align)) {
		dma_addr = dma_map_single(dev, buf, op->orig_len, data_dir);
		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "failed to map DMA buffer\n");
			return -ENXIO;
		}
	} else if (!op->is_write) {
		dma_buf = dma_alloc_coherent(dev, op->len, &dma_addr, GFP_KERNEL);
		if (!dma_buf)
			return -ENOMEM;
	} else {
		dev_err(dev, "subpage writing not supported\n");
		return -EOPNOTSUPP;
	}

	desc = dmaengine_prep_slave_single(chan, dma_addr, op->len, xfer_dir, DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "failed to prepare DMA descriptor\n");
		ret = -ENOMEM;
		goto err;
	}
	desc->callback = loongson_nand_dma_callback;
	desc->callback_param = host;

	host->dma_cookie = dmaengine_submit(desc);
	ret = dma_submit_error(host->dma_cookie);
	if (ret) {
		dev_err(dev, "failed to submit DMA descriptor\n");
		goto err;
	}

	dev_dbg(dev, "issue DMA with cookie=%d\n", host->dma_cookie);
	dma_async_issue_pending(chan);

	if (!wait_for_completion_timeout(&host->dma_complete, msecs_to_jiffies(1000))) {
		dmaengine_terminate_sync(chan);
		reinit_completion(&host->dma_complete);
		ret = -ETIMEDOUT;
		goto err;
	}

	if (dma_buf)
		memcpy(buf, dma_buf + op->aligned_offset, op->orig_len);
err:
	if (dma_buf)
		dma_free_coherent(dev, op->len, dma_buf, dma_addr);
	else
		dma_unmap_single(dev, dma_addr, op->orig_len, data_dir);

	return ret;
}

static int loongson_nand_data_type_exec(struct nand_chip *chip, const struct nand_subop *subop)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);
	struct loongson_nand_op op = {};
	int ret;

	ret = loongson_nand_parse_instructions(chip, subop, &op);
	if (ret)
		return ret;

	loongson_nand_trigger_op(host, &op);

	ret = loongson_nand_dma_transfer(host, &op);
	if (ret)
		return ret;

	return loongson_nand_wait_for_op_done(host, &op);
}

static int loongson_nand_misc_type_exec(struct nand_chip *chip, const struct nand_subop *subop,
					struct loongson_nand_op *op)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);
	int ret;

	ret = loongson_nand_parse_instructions(chip, subop, op);
	if (ret)
		return ret;

	loongson_nand_trigger_op(host, op);

	return loongson_nand_wait_for_op_done(host, op);
}

static int loongson_nand_zerolen_type_exec(struct nand_chip *chip, const struct nand_subop *subop)
{
	struct loongson_nand_op op = {};

	return loongson_nand_misc_type_exec(chip, subop, &op);
}

static int loongson_nand_read_id_type_exec(struct nand_chip *chip, const struct nand_subop *subop)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);
	struct loongson_nand_op op = {};
	int i, ret;
	union {
		char ids[6];
		struct {
			int idl;
			u16 idh;
		};
	} nand_id;

	ret = loongson_nand_misc_type_exec(chip, subop, &op);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(host->regmap, LOONGSON_NAND_IDL, nand_id.idl, nand_id.idl,
				       LOONGSON_NAND_READ_ID_SLEEP_US,
				       LOONGSON_NAND_READ_ID_TIMEOUT_US);
	if (ret)
		return ret;

	nand_id.idh = readw(host->reg_base + LOONGSON_NAND_IDH_STATUS);

	for (i = 0; i < min(host->data->max_id_cycle, op.orig_len); i++)
		op.buf[i] = nand_id.ids[host->data->max_id_cycle - 1 - i];

	return ret;
}

static int loongson_nand_read_status_type_exec(struct nand_chip *chip,
					       const struct nand_subop *subop)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);
	struct loongson_nand_op op = {};
	int val, ret;

	ret = loongson_nand_misc_type_exec(chip, subop, &op);
	if (ret)
		return ret;

	val = readl(host->reg_base + LOONGSON_NAND_IDH_STATUS);
	val &= ~host->data->status_field;
	op.buf[0] = val << ffs(host->data->status_field);

	return ret;
}

static const struct nand_op_parser loongson_nand_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(
		loongson_nand_read_id_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, LOONGSON_NAND_MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 8)),
	NAND_OP_PARSER_PATTERN(
		loongson_nand_read_status_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 1)),
	NAND_OP_PARSER_PATTERN(
		loongson_nand_zerolen_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		loongson_nand_zerolen_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, LOONGSON_NAND_MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		loongson_nand_data_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, LOONGSON_NAND_MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 0)),
	NAND_OP_PARSER_PATTERN(
		loongson_nand_data_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, LOONGSON_NAND_MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, 0),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
	);

static int loongson_nand_is_valid_cmd(u8 opcode)
{
	if (opcode == NAND_CMD_STATUS || opcode == NAND_CMD_RESET || opcode == NAND_CMD_READID)
		return 0;

	return -EOPNOTSUPP;
}

static int loongson_nand_is_valid_cmd_seq(u8 opcode1, u8 opcode2)
{
	if (opcode1 == NAND_CMD_RNDOUT && opcode2 == NAND_CMD_RNDOUTSTART)
		return 0;

	if (opcode1 == NAND_CMD_READ0 && opcode2 == NAND_CMD_READSTART)
		return 0;

	if (opcode1 == NAND_CMD_ERASE1 && opcode2 == NAND_CMD_ERASE2)
		return 0;

	if (opcode1 == NAND_CMD_SEQIN && opcode2 == NAND_CMD_PAGEPROG)
		return 0;

	return -EOPNOTSUPP;
}

static int loongson_nand_check_op(struct nand_chip *chip, const struct nand_operation *op)
{
	const struct nand_op_instr *instr1 = NULL, *instr2 = NULL;
	int op_id;

	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		const struct nand_op_instr *instr = &op->instrs[op_id];

		if (instr->type == NAND_OP_CMD_INSTR) {
			if (!instr1)
				instr1 = instr;
			else if (!instr2)
				instr2 = instr;
			else
				break;
		}
	}

	if (!instr1)
		return -EOPNOTSUPP;

	if (!instr2)
		return loongson_nand_is_valid_cmd(instr1->ctx.cmd.opcode);

	return loongson_nand_is_valid_cmd_seq(instr1->ctx.cmd.opcode, instr2->ctx.cmd.opcode);
}

static int loongson_nand_exec_op(struct nand_chip *chip, const struct nand_operation *op,
				 bool check_only)
{
	if (check_only)
		return loongson_nand_check_op(chip, op);

	return nand_op_parser_exec_op(chip, &loongson_nand_op_parser, op, check_only);
}

static int loongson_nand_get_chip_capacity(struct nand_chip *chip)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);
	u64 chipsize = nanddev_target_size(&chip->base);
	struct mtd_info *mtd = nand_to_mtd(chip);

	switch (mtd->writesize) {
	case SZ_512:
		switch (chipsize) {
		case SZ_8M:
			host->addr_cs_field = GENMASK(15, 14);
			return 0x9;
		case SZ_16M:
			host->addr_cs_field = GENMASK(16, 15);
			return 0xa;
		case SZ_32M:
			host->addr_cs_field = GENMASK(17, 16);
			return 0xb;
		case SZ_64M:
			host->addr_cs_field = GENMASK(18, 17);
			return 0xc;
		case SZ_128M:
			host->addr_cs_field = GENMASK(19, 18);
			return 0xd;
		}
		break;
	case SZ_2K:
		switch (chipsize) {
		case SZ_128M:
			host->addr_cs_field = GENMASK(17, 16);
			return 0x0;
		case SZ_256M:
			host->addr_cs_field = GENMASK(18, 17);
			return 0x1;
		case SZ_512M:
			host->addr_cs_field = GENMASK(19, 18);
			return 0x2;
		case SZ_1G:
			host->addr_cs_field = GENMASK(20, 19);
			return 0x3;
		}
		break;
	case SZ_4K:
		if (chipsize == SZ_2G) {
			host->addr_cs_field = GENMASK(20, 19);
			return 0x4;
		}
		break;
	case SZ_8K:
		switch (chipsize) {
		case SZ_4G:
			host->addr_cs_field = GENMASK(20, 19);
			return 0x5;
		case SZ_8G:
			host->addr_cs_field = GENMASK(21, 20);
			return 0x6;
		case SZ_16G:
			host->addr_cs_field = GENMASK(22, 21);
			return 0x7;
		}
		break;
	}

	dev_err(host->dev, "Unsupported chip size: %llu MB with page size %u B\n",
		chipsize, mtd->writesize);
	return -EINVAL;
}

static int loongson_nand_attach_chip(struct nand_chip *chip)
{
	struct loongson_nand_host *host = nand_get_controller_data(chip);
	int cell_size = loongson_nand_get_chip_capacity(chip);

	if (cell_size < 0)
		return cell_size;

	switch (chip->ecc.engine_type) {
	case NAND_ECC_ENGINE_TYPE_NONE:
		break;
	case NAND_ECC_ENGINE_TYPE_SOFT:
		break;
	default:
		return -EINVAL;
	}

	/* set cell size */
	regmap_update_bits(host->regmap, LOONGSON_NAND_PARAM, LOONGSON_NAND_CELL_SIZE_MASK,
			   FIELD_PREP(LOONGSON_NAND_CELL_SIZE_MASK, cell_size));

	regmap_update_bits(host->regmap, LOONGSON_NAND_TIMING, LOONGSON_NAND_HOLD_CYCLE_MASK,
			   FIELD_PREP(LOONGSON_NAND_HOLD_CYCLE_MASK, host->data->hold_cycle));

	regmap_update_bits(host->regmap, LOONGSON_NAND_TIMING, LOONGSON_NAND_WAIT_CYCLE_MASK,
			   FIELD_PREP(LOONGSON_NAND_WAIT_CYCLE_MASK, host->data->wait_cycle));

	chip->ecc.read_page_raw = nand_monolithic_read_page_raw;
	chip->ecc.write_page_raw = nand_monolithic_write_page_raw;

	return 0;
}

static const struct nand_controller_ops loongson_nand_controller_ops = {
	.exec_op = loongson_nand_exec_op,
	.attach_chip = loongson_nand_attach_chip,
};

static void loongson_nand_controller_cleanup(struct loongson_nand_host *host)
{
	if (host->dma_chan)
		dma_release_channel(host->dma_chan);
}

static int ls2k1000_nand_apbdma_config(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *regs;
	int val;

	regs = devm_platform_ioremap_resource_byname(pdev, "dma-config");
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	val = readl(regs);
	val |= FIELD_PREP(LS2K1000_NAND_DMA_MASK, LS2K1000_DMA0_CONF);
	writel(val, regs);

	return 0;
}

static int loongson_nand_controller_init(struct loongson_nand_host *host)
{
	struct device *dev = host->dev;
	struct dma_chan *chan;
	struct dma_slave_config cfg = {};
	int ret, val;

	host->regmap = devm_regmap_init_mmio(dev, host->reg_base, &loongson_nand_regmap_config);
	if (IS_ERR(host->regmap))
		return dev_err_probe(dev, PTR_ERR(host->regmap), "failed to init regmap\n");

	if (host->data->id_cycle_field)
		regmap_update_bits(host->regmap, LOONGSON_NAND_PARAM, host->data->id_cycle_field,
				   host->data->max_id_cycle << __ffs(host->data->id_cycle_field));

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(host->data->dma_bits));
	if (ret)
		return dev_err_probe(dev, ret, "failed to set DMA mask\n");

	val = FIELD_PREP(LOONGSON_NAND_MAP_CS1_SEL, LOONGSON_NAND_CS_SEL1) |
	      FIELD_PREP(LOONGSON_NAND_MAP_RDY1_SEL, LOONGSON_NAND_CS_RDY1) |
	      FIELD_PREP(LOONGSON_NAND_MAP_CS2_SEL, LOONGSON_NAND_CS_SEL2) |
	      FIELD_PREP(LOONGSON_NAND_MAP_RDY2_SEL, LOONGSON_NAND_CS_RDY2) |
	      FIELD_PREP(LOONGSON_NAND_MAP_CS3_SEL, LOONGSON_NAND_CS_SEL3) |
	      FIELD_PREP(LOONGSON_NAND_MAP_RDY3_SEL, LOONGSON_NAND_CS_RDY3);

	regmap_write(host->regmap, LOONGSON_NAND_CS_RDY_MAP, val);

	if (host->data->dma_config) {
		ret = host->data->dma_config(dev);
		if (ret)
			return dev_err_probe(dev, ret, "failed to config DMA routing\n");
	}

	chan = dma_request_chan(dev, "rxtx");
	if (IS_ERR(chan))
		return dev_err_probe(dev, PTR_ERR(chan), "failed to request DMA channel\n");
	host->dma_chan = chan;

	cfg.src_addr = host->dma_base;
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr = host->dma_base;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ret = dmaengine_slave_config(host->dma_chan, &cfg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to config DMA channel\n");

	init_completion(&host->dma_complete);

	return 0;
}

static int loongson_nand_chip_init(struct loongson_nand_host *host)
{
	struct device *dev = host->dev;
	int nchips = of_get_child_count(dev->of_node);
	struct device_node *chip_np;
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	if (nchips != 1)
		return dev_err_probe(dev, -EINVAL, "Currently one NAND chip supported\n");

	chip_np = of_get_next_child(dev->of_node, NULL);
	if (!chip_np)
		return dev_err_probe(dev, -ENODEV, "failed to get child node for NAND chip\n");

	nand_set_flash_node(chip, chip_np);
	of_node_put(chip_np);
	if (!mtd->name)
		return dev_err_probe(dev, -EINVAL, "Missing MTD label\n");

	nand_set_controller_data(chip, host);
	chip->controller = &host->controller;
	chip->options = NAND_NO_SUBPAGE_WRITE | NAND_USES_DMA | NAND_BROKEN_XD;
	chip->buf_align = 16;
	mtd->dev.parent = dev;
	mtd->owner = THIS_MODULE;

	ret = nand_scan(chip, 1);
	if (ret)
		return dev_err_probe(dev, ret, "failed to scan NAND chip\n");

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_cleanup(chip);
		return dev_err_probe(dev, ret, "failed to register MTD device\n");
	}

	return 0;
}

static int loongson_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct loongson_nand_data *data;
	struct loongson_nand_host *host;
	struct resource *res;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->reg_base))
		return PTR_ERR(host->reg_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand-dma");
	if (!res)
		return dev_err_probe(dev, -EINVAL, "Missing 'nand-dma' in reg-names property\n");

	host->dma_base = dma_map_resource(dev, res->start, resource_size(res),
					  DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(dev, host->dma_base))
		return -ENXIO;

	host->dev = dev;
	host->data = data;
	host->controller.ops = &loongson_nand_controller_ops;

	nand_controller_init(&host->controller);

	ret = loongson_nand_controller_init(host);
	if (ret)
		goto err;

	ret = loongson_nand_chip_init(host);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, host);

	return 0;
err:
	loongson_nand_controller_cleanup(host);

	return ret;
}

static void loongson_nand_remove(struct platform_device *pdev)
{
	struct loongson_nand_host *host = platform_get_drvdata(pdev);
	struct nand_chip *chip = &host->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
	loongson_nand_controller_cleanup(host);
}

static const struct loongson_nand_data ls1b_nand_data = {
	.max_id_cycle = 5,
	.status_field = GENMASK(15, 8),
	.hold_cycle = 0x2,
	.wait_cycle = 0xc,
	.dma_bits = 32,
	.set_addr = ls1b_nand_set_addr,
};

static const struct loongson_nand_data ls1c_nand_data = {
	.max_id_cycle = 6,
	.id_cycle_field = GENMASK(14, 12),
	.status_field = GENMASK(23, 16),
	.op_scope_field = GENMASK(29, 16),
	.hold_cycle = 0x2,
	.wait_cycle = 0xc,
	.dma_bits = 32,
	.set_addr = ls1c_nand_set_addr,
};

static const struct loongson_nand_data ls2k0500_nand_data = {
	.max_id_cycle = 6,
	.id_cycle_field = GENMASK(14, 12),
	.status_field = GENMASK(23, 16),
	.op_scope_field = GENMASK(29, 16),
	.hold_cycle = 0x4,
	.wait_cycle = 0x12,
	.dma_bits = 64,
	.set_addr = ls1c_nand_set_addr,
};

static const struct loongson_nand_data ls2k1000_nand_data = {
	.max_id_cycle = 6,
	.id_cycle_field = GENMASK(14, 12),
	.status_field = GENMASK(23, 16),
	.op_scope_field = GENMASK(29, 16),
	.hold_cycle = 0x4,
	.wait_cycle = 0x12,
	.nand_cs = 0x2,
	.dma_bits = 64,
	.dma_config = ls2k1000_nand_apbdma_config,
	.set_addr = ls1c_nand_set_addr,
};

static const struct of_device_id loongson_nand_match[] = {
	{
		.compatible = "loongson,ls1b-nand-controller",
		.data = &ls1b_nand_data,
	},
	{
		.compatible = "loongson,ls1c-nand-controller",
		.data = &ls1c_nand_data,
	},
	{
		.compatible = "loongson,ls2k0500-nand-controller",
		.data = &ls2k0500_nand_data,
	},
	{
		.compatible = "loongson,ls2k1000-nand-controller",
		.data = &ls2k1000_nand_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, loongson_nand_match);

static struct platform_driver loongson_nand_driver = {
	.probe = loongson_nand_probe,
	.remove = loongson_nand_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = loongson_nand_match,
	},
};

module_platform_driver(loongson_nand_driver);

MODULE_AUTHOR("Keguang Zhang <keguang.zhang@gmail.com>");
MODULE_AUTHOR("Binbin Zhou <zhoubinbin@loongson.cn>");
MODULE_DESCRIPTION("Loongson NAND Controller Driver");
MODULE_LICENSE("GPL");

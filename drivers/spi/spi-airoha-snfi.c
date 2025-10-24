// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 * Author: Ray Liu <ray.liu@airoha.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/types.h>
#include <linux/unaligned.h>

/* SPI */
#define REG_SPI_CTRL_BASE			0x1FA10000

#define REG_SPI_CTRL_READ_MODE			0x0000
#define REG_SPI_CTRL_READ_IDLE_EN		0x0004
#define REG_SPI_CTRL_SIDLY			0x0008
#define REG_SPI_CTRL_CSHEXT			0x000c
#define REG_SPI_CTRL_CSLEXT			0x0010

#define REG_SPI_CTRL_MTX_MODE_TOG		0x0014
#define SPI_CTRL_MTX_MODE_TOG			GENMASK(3, 0)

#define REG_SPI_CTRL_RDCTL_FSM			0x0018
#define SPI_CTRL_RDCTL_FSM			GENMASK(3, 0)

#define REG_SPI_CTRL_MACMUX_SEL			0x001c

#define REG_SPI_CTRL_MANUAL_EN			0x0020
#define SPI_CTRL_MANUAL_EN			BIT(0)

#define REG_SPI_CTRL_OPFIFO_EMPTY		0x0024
#define SPI_CTRL_OPFIFO_EMPTY			BIT(0)

#define REG_SPI_CTRL_OPFIFO_WDATA		0x0028
#define SPI_CTRL_OPFIFO_LEN			GENMASK(8, 0)
#define SPI_CTRL_OPFIFO_OP			GENMASK(13, 9)

#define REG_SPI_CTRL_OPFIFO_FULL		0x002c
#define SPI_CTRL_OPFIFO_FULL			BIT(0)

#define REG_SPI_CTRL_OPFIFO_WR			0x0030
#define SPI_CTRL_OPFIFO_WR			BIT(0)

#define REG_SPI_CTRL_DFIFO_FULL			0x0034
#define SPI_CTRL_DFIFO_FULL			BIT(0)

#define REG_SPI_CTRL_DFIFO_WDATA		0x0038
#define SPI_CTRL_DFIFO_WDATA			GENMASK(7, 0)

#define REG_SPI_CTRL_DFIFO_EMPTY		0x003c
#define SPI_CTRL_DFIFO_EMPTY			BIT(0)

#define REG_SPI_CTRL_DFIFO_RD			0x0040
#define SPI_CTRL_DFIFO_RD			BIT(0)

#define REG_SPI_CTRL_DFIFO_RDATA		0x0044
#define SPI_CTRL_DFIFO_RDATA			GENMASK(7, 0)

#define REG_SPI_CTRL_DUMMY			0x0080
#define SPI_CTRL_CTRL_DUMMY			GENMASK(3, 0)

#define REG_SPI_CTRL_PROBE_SEL			0x0088
#define REG_SPI_CTRL_INTERRUPT			0x0090
#define REG_SPI_CTRL_INTERRUPT_EN		0x0094
#define REG_SPI_CTRL_SI_CK_SEL			0x009c
#define REG_SPI_CTRL_SW_CFGNANDADDR_VAL		0x010c
#define REG_SPI_CTRL_SW_CFGNANDADDR_EN		0x0110
#define REG_SPI_CTRL_SFC_STRAP			0x0114

#define REG_SPI_CTRL_NFI2SPI_EN			0x0130
#define SPI_CTRL_NFI2SPI_EN			BIT(0)

/* NFI2SPI */
#define REG_SPI_NFI_CNFG			0x0000
#define SPI_NFI_DMA_MODE			BIT(0)
#define SPI_NFI_READ_MODE			BIT(1)
#define SPI_NFI_DMA_BURST_EN			BIT(2)
#define SPI_NFI_HW_ECC_EN			BIT(8)
#define SPI_NFI_AUTO_FDM_EN			BIT(9)
#define SPI_NFI_OPMODE				GENMASK(14, 12)

#define REG_SPI_NFI_PAGEFMT			0x0004
#define SPI_NFI_PAGE_SIZE			GENMASK(1, 0)
#define SPI_NFI_SPARE_SIZE			GENMASK(5, 4)

#define REG_SPI_NFI_CON				0x0008
#define SPI_NFI_FIFO_FLUSH			BIT(0)
#define SPI_NFI_RST				BIT(1)
#define SPI_NFI_RD_TRIG				BIT(8)
#define SPI_NFI_WR_TRIG				BIT(9)
#define SPI_NFI_SEC_NUM				GENMASK(15, 12)

#define REG_SPI_NFI_INTR_EN			0x0010
#define SPI_NFI_RD_DONE_EN			BIT(0)
#define SPI_NFI_WR_DONE_EN			BIT(1)
#define SPI_NFI_RST_DONE_EN			BIT(2)
#define SPI_NFI_ERASE_DONE_EN			BIT(3)
#define SPI_NFI_BUSY_RETURN_EN			BIT(4)
#define SPI_NFI_ACCESS_LOCK_EN			BIT(5)
#define SPI_NFI_AHB_DONE_EN			BIT(6)
#define SPI_NFI_ALL_IRQ_EN					\
	(SPI_NFI_RD_DONE_EN | SPI_NFI_WR_DONE_EN |		\
	 SPI_NFI_RST_DONE_EN | SPI_NFI_ERASE_DONE_EN |		\
	 SPI_NFI_BUSY_RETURN_EN | SPI_NFI_ACCESS_LOCK_EN |	\
	 SPI_NFI_AHB_DONE_EN)

#define REG_SPI_NFI_INTR			0x0014
#define SPI_NFI_AHB_DONE			BIT(6)

#define REG_SPI_NFI_CMD				0x0020

#define REG_SPI_NFI_ADDR_NOB			0x0030
#define SPI_NFI_ROW_ADDR_NOB			GENMASK(6, 4)

#define REG_SPI_NFI_STA				0x0060
#define REG_SPI_NFI_FIFOSTA			0x0064
#define REG_SPI_NFI_STRADDR			0x0080
#define REG_SPI_NFI_FDM0L			0x00a0
#define REG_SPI_NFI_FDM0M			0x00a4
#define REG_SPI_NFI_FDM7L			0x00d8
#define REG_SPI_NFI_FDM7M			0x00dc
#define REG_SPI_NFI_FIFODATA0			0x0190
#define REG_SPI_NFI_FIFODATA1			0x0194
#define REG_SPI_NFI_FIFODATA2			0x0198
#define REG_SPI_NFI_FIFODATA3			0x019c
#define REG_SPI_NFI_MASTERSTA			0x0224

#define REG_SPI_NFI_SECCUS_SIZE			0x022c
#define SPI_NFI_CUS_SEC_SIZE			GENMASK(12, 0)
#define SPI_NFI_CUS_SEC_SIZE_EN			BIT(16)

#define REG_SPI_NFI_RD_CTL2			0x0510
#define REG_SPI_NFI_RD_CTL3			0x0514

#define REG_SPI_NFI_PG_CTL1			0x0524
#define SPI_NFI_PG_LOAD_CMD			GENMASK(15, 8)

#define REG_SPI_NFI_PG_CTL2			0x0528
#define REG_SPI_NFI_NOR_PROG_ADDR		0x052c
#define REG_SPI_NFI_NOR_RD_ADDR			0x0534

#define REG_SPI_NFI_SNF_MISC_CTL		0x0538
#define SPI_NFI_DATA_READ_WR_MODE		GENMASK(18, 16)

#define REG_SPI_NFI_SNF_MISC_CTL2		0x053c
#define SPI_NFI_READ_DATA_BYTE_NUM		GENMASK(12, 0)
#define SPI_NFI_PROG_LOAD_BYTE_NUM		GENMASK(28, 16)

#define REG_SPI_NFI_SNF_STA_CTL1		0x0550
#define SPI_NFI_READ_FROM_CACHE_DONE		BIT(25)
#define SPI_NFI_LOAD_TO_CACHE_DONE		BIT(26)

#define REG_SPI_NFI_SNF_STA_CTL2		0x0554

#define REG_SPI_NFI_SNF_NFI_CNFG		0x055c
#define SPI_NFI_SPI_MODE			BIT(0)

/* SPI NAND Protocol OP */
#define SPI_NAND_OP_GET_FEATURE			0x0f
#define SPI_NAND_OP_SET_FEATURE			0x1f
#define SPI_NAND_OP_PAGE_READ			0x13
#define SPI_NAND_OP_READ_FROM_CACHE_SINGLE	0x03
#define SPI_NAND_OP_READ_FROM_CACHE_SINGLE_FAST	0x0b
#define SPI_NAND_OP_READ_FROM_CACHE_DUAL	0x3b
#define SPI_NAND_OP_READ_FROM_CACHE_QUAD	0x6b
#define SPI_NAND_OP_WRITE_ENABLE		0x06
#define SPI_NAND_OP_WRITE_DISABLE		0x04
#define SPI_NAND_OP_PROGRAM_LOAD_SINGLE		0x02
#define SPI_NAND_OP_PROGRAM_LOAD_QUAD		0x32
#define SPI_NAND_OP_PROGRAM_LOAD_RAMDOM_SINGLE	0x84
#define SPI_NAND_OP_PROGRAM_LOAD_RAMDON_QUAD	0x34
#define SPI_NAND_OP_PROGRAM_EXECUTE		0x10
#define SPI_NAND_OP_READ_ID			0x9f
#define SPI_NAND_OP_BLOCK_ERASE			0xd8
#define SPI_NAND_OP_RESET			0xff
#define SPI_NAND_OP_DIE_SELECT			0xc2

/* SNAND FIFO commands */
#define SNAND_FIFO_TX_BUSWIDTH_SINGLE		0x08
#define SNAND_FIFO_TX_BUSWIDTH_DUAL		0x09
#define SNAND_FIFO_TX_BUSWIDTH_QUAD		0x0a
#define SNAND_FIFO_RX_BUSWIDTH_SINGLE		0x0c
#define SNAND_FIFO_RX_BUSWIDTH_DUAL		0x0e
#define SNAND_FIFO_RX_BUSWIDTH_QUAD		0x0f

#define SPI_NAND_CACHE_SIZE			(SZ_4K + SZ_256)
#define SPI_MAX_TRANSFER_SIZE			511

enum airoha_snand_mode {
	SPI_MODE_AUTO,
	SPI_MODE_MANUAL,
	SPI_MODE_DMA,
};

enum airoha_snand_cs {
	SPI_CHIP_SEL_HIGH,
	SPI_CHIP_SEL_LOW,
};

struct airoha_snand_ctrl {
	struct device *dev;
	struct regmap *regmap_ctrl;
	struct regmap *regmap_nfi;
	struct clk *spi_clk;

	struct {
		size_t page_size;
		size_t sec_size;
		u8 sec_num;
		u8 spare_size;
	} nfi_cfg;
};

static int airoha_snand_set_fifo_op(struct airoha_snand_ctrl *as_ctrl,
				    u8 op_cmd, int op_len)
{
	int err;
	u32 val;

	err = regmap_write(as_ctrl->regmap_ctrl, REG_SPI_CTRL_OPFIFO_WDATA,
			   FIELD_PREP(SPI_CTRL_OPFIFO_LEN, op_len) |
			   FIELD_PREP(SPI_CTRL_OPFIFO_OP, op_cmd));
	if (err)
		return err;

	err = regmap_read_poll_timeout(as_ctrl->regmap_ctrl,
				       REG_SPI_CTRL_OPFIFO_FULL,
				       val, !(val & SPI_CTRL_OPFIFO_FULL),
				       0, 250 * USEC_PER_MSEC);
	if (err)
		return err;

	err = regmap_write(as_ctrl->regmap_ctrl, REG_SPI_CTRL_OPFIFO_WR,
			   SPI_CTRL_OPFIFO_WR);
	if (err)
		return err;

	return regmap_read_poll_timeout(as_ctrl->regmap_ctrl,
					REG_SPI_CTRL_OPFIFO_EMPTY,
					val, (val & SPI_CTRL_OPFIFO_EMPTY),
					0, 250 * USEC_PER_MSEC);
}

static int airoha_snand_set_cs(struct airoha_snand_ctrl *as_ctrl, u8 cs)
{
	return airoha_snand_set_fifo_op(as_ctrl, cs, sizeof(cs));
}

static int airoha_snand_write_data_to_fifo(struct airoha_snand_ctrl *as_ctrl,
					   const u8 *data, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int err;
		u32 val;

		/* 1. Wait until dfifo is not full */
		err = regmap_read_poll_timeout(as_ctrl->regmap_ctrl,
					       REG_SPI_CTRL_DFIFO_FULL, val,
					       !(val & SPI_CTRL_DFIFO_FULL),
					       0, 250 * USEC_PER_MSEC);
		if (err)
			return err;

		/* 2. Write data to register DFIFO_WDATA */
		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_DFIFO_WDATA,
				   FIELD_PREP(SPI_CTRL_DFIFO_WDATA, data[i]));
		if (err)
			return err;

		/* 3. Wait until dfifo is not full */
		err = regmap_read_poll_timeout(as_ctrl->regmap_ctrl,
					       REG_SPI_CTRL_DFIFO_FULL, val,
					       !(val & SPI_CTRL_DFIFO_FULL),
					       0, 250 * USEC_PER_MSEC);
		if (err)
			return err;
	}

	return 0;
}

static int airoha_snand_read_data_from_fifo(struct airoha_snand_ctrl *as_ctrl,
					    u8 *ptr, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int err;
		u32 val;

		/* 1. wait until dfifo is not empty */
		err = regmap_read_poll_timeout(as_ctrl->regmap_ctrl,
					       REG_SPI_CTRL_DFIFO_EMPTY, val,
					       !(val & SPI_CTRL_DFIFO_EMPTY),
					       0, 250 * USEC_PER_MSEC);
		if (err)
			return err;

		/* 2. read from dfifo to register DFIFO_RDATA */
		err = regmap_read(as_ctrl->regmap_ctrl,
				  REG_SPI_CTRL_DFIFO_RDATA, &val);
		if (err)
			return err;

		ptr[i] = FIELD_GET(SPI_CTRL_DFIFO_RDATA, val);
		/* 3. enable register DFIFO_RD to read next byte */
		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_DFIFO_RD, SPI_CTRL_DFIFO_RD);
		if (err)
			return err;
	}

	return 0;
}

static int airoha_snand_set_mode(struct airoha_snand_ctrl *as_ctrl,
				 enum airoha_snand_mode mode)
{
	int err;

	switch (mode) {
	case SPI_MODE_MANUAL: {
		u32 val;

		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_NFI2SPI_EN, 0);
		if (err)
			return err;

		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_READ_IDLE_EN, 0);
		if (err)
			return err;

		err = regmap_read_poll_timeout(as_ctrl->regmap_ctrl,
					       REG_SPI_CTRL_RDCTL_FSM, val,
					       !(val & SPI_CTRL_RDCTL_FSM),
					       0, 250 * USEC_PER_MSEC);
		if (err)
			return err;

		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_MTX_MODE_TOG, 9);
		if (err)
			return err;

		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_MANUAL_EN, SPI_CTRL_MANUAL_EN);
		if (err)
			return err;
		break;
	}
	case SPI_MODE_DMA:
		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_NFI2SPI_EN,
				   SPI_CTRL_MANUAL_EN);
		if (err < 0)
			return err;

		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_MTX_MODE_TOG, 0x0);
		if (err < 0)
			return err;

		err = regmap_write(as_ctrl->regmap_ctrl,
				   REG_SPI_CTRL_MANUAL_EN, 0x0);
		if (err < 0)
			return err;
		break;
	case SPI_MODE_AUTO:
	default:
		break;
	}

	return regmap_write(as_ctrl->regmap_ctrl, REG_SPI_CTRL_DUMMY, 0);
}

static int airoha_snand_write_data(struct airoha_snand_ctrl *as_ctrl,
				   const u8 *data, int len, int buswidth)
{
	int i, data_len;
	u8 cmd;

	switch (buswidth) {
	case 0:
	case 1:
		cmd = SNAND_FIFO_TX_BUSWIDTH_SINGLE;
		break;
	case 2:
		cmd = SNAND_FIFO_TX_BUSWIDTH_DUAL;
		break;
	case 4:
		cmd = SNAND_FIFO_TX_BUSWIDTH_QUAD;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < len; i += data_len) {
		int err;

		data_len = min(len - i, SPI_MAX_TRANSFER_SIZE);
		err = airoha_snand_set_fifo_op(as_ctrl, cmd, data_len);
		if (err)
			return err;

		err = airoha_snand_write_data_to_fifo(as_ctrl, &data[i],
						      data_len);
		if (err < 0)
			return err;
	}

	return 0;
}

static int airoha_snand_read_data(struct airoha_snand_ctrl *as_ctrl,
				  u8 *data, int len, int buswidth)
{
	int i, data_len;
	u8 cmd;

	switch (buswidth) {
	case 0:
	case 1:
		cmd = SNAND_FIFO_RX_BUSWIDTH_SINGLE;
		break;
	case 2:
		cmd = SNAND_FIFO_RX_BUSWIDTH_DUAL;
		break;
	case 4:
		cmd = SNAND_FIFO_RX_BUSWIDTH_QUAD;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < len; i += data_len) {
		int err;

		data_len = min(len - i, SPI_MAX_TRANSFER_SIZE);
		err = airoha_snand_set_fifo_op(as_ctrl, cmd, data_len);
		if (err)
			return err;

		err = airoha_snand_read_data_from_fifo(as_ctrl, &data[i],
						       data_len);
		if (err < 0)
			return err;
	}

	return 0;
}

static int airoha_snand_nfi_init(struct airoha_snand_ctrl *as_ctrl)
{
	int err;

	/* switch to SNFI mode */
	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_SNF_NFI_CNFG,
			   SPI_NFI_SPI_MODE);
	if (err)
		return err;

	/* Enable DMA */
	return regmap_update_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_INTR_EN,
				  SPI_NFI_ALL_IRQ_EN, SPI_NFI_AHB_DONE_EN);
}

static int airoha_snand_nfi_config(struct airoha_snand_ctrl *as_ctrl)
{
	int err;
	u32 val;

	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_CON,
			   SPI_NFI_FIFO_FLUSH | SPI_NFI_RST);
	if (err)
		return err;

	/* auto FDM */
	err = regmap_clear_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
				SPI_NFI_AUTO_FDM_EN);
	if (err)
		return err;

	/* HW ECC */
	err = regmap_clear_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
				SPI_NFI_HW_ECC_EN);
	if (err)
		return err;

	/* DMA Burst */
	err = regmap_set_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
			      SPI_NFI_DMA_BURST_EN);
	if (err)
		return err;

	/* page format */
	switch (as_ctrl->nfi_cfg.spare_size) {
	case 26:
		val = FIELD_PREP(SPI_NFI_SPARE_SIZE, 0x1);
		break;
	case 27:
		val = FIELD_PREP(SPI_NFI_SPARE_SIZE, 0x2);
		break;
	case 28:
		val = FIELD_PREP(SPI_NFI_SPARE_SIZE, 0x3);
		break;
	default:
		val = FIELD_PREP(SPI_NFI_SPARE_SIZE, 0x0);
		break;
	}

	err = regmap_update_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_PAGEFMT,
				 SPI_NFI_SPARE_SIZE, val);
	if (err)
		return err;

	switch (as_ctrl->nfi_cfg.page_size) {
	case 2048:
		val = FIELD_PREP(SPI_NFI_PAGE_SIZE, 0x1);
		break;
	case 4096:
		val = FIELD_PREP(SPI_NFI_PAGE_SIZE, 0x2);
		break;
	default:
		val = FIELD_PREP(SPI_NFI_PAGE_SIZE, 0x0);
		break;
	}

	err = regmap_update_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_PAGEFMT,
				 SPI_NFI_PAGE_SIZE, val);
	if (err)
		return err;

	/* sec num */
	val = FIELD_PREP(SPI_NFI_SEC_NUM, as_ctrl->nfi_cfg.sec_num);
	err = regmap_update_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CON,
				 SPI_NFI_SEC_NUM, val);
	if (err)
		return err;

	/* enable cust sec size */
	err = regmap_set_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_SECCUS_SIZE,
			      SPI_NFI_CUS_SEC_SIZE_EN);
	if (err)
		return err;

	/* set cust sec size */
	val = FIELD_PREP(SPI_NFI_CUS_SEC_SIZE, as_ctrl->nfi_cfg.sec_size);
	return regmap_update_bits(as_ctrl->regmap_nfi,
				  REG_SPI_NFI_SECCUS_SIZE,
				  SPI_NFI_CUS_SEC_SIZE, val);
}

static bool airoha_snand_is_page_ops(const struct spi_mem_op *op)
{
	if (op->addr.nbytes != 2)
		return false;

	if (op->addr.buswidth != 1 && op->addr.buswidth != 2 &&
	    op->addr.buswidth != 4)
		return false;

	switch (op->data.dir) {
	case SPI_MEM_DATA_IN:
		if (op->dummy.nbytes * BITS_PER_BYTE / op->dummy.buswidth > 0xf)
			return false;

		/* quad in / quad out */
		if (op->addr.buswidth == 4)
			return op->data.buswidth == 4;

		if (op->addr.buswidth == 2)
			return op->data.buswidth == 2;

		/* standard spi */
		return op->data.buswidth == 4 || op->data.buswidth == 2 ||
		       op->data.buswidth == 1;
	case SPI_MEM_DATA_OUT:
		return !op->dummy.nbytes && op->addr.buswidth == 1 &&
		       (op->data.buswidth == 4 || op->data.buswidth == 1);
	default:
		return false;
	}
}

static int airoha_snand_adjust_op_size(struct spi_mem *mem,
				       struct spi_mem_op *op)
{
	size_t max_len;

	if (airoha_snand_is_page_ops(op)) {
		struct airoha_snand_ctrl *as_ctrl;

		as_ctrl = spi_controller_get_devdata(mem->spi->controller);
		max_len = as_ctrl->nfi_cfg.sec_size;
		max_len += as_ctrl->nfi_cfg.spare_size;
		max_len *= as_ctrl->nfi_cfg.sec_num;

		if (op->data.nbytes > max_len)
			op->data.nbytes = max_len;
	} else {
		max_len = 1 + op->addr.nbytes + op->dummy.nbytes;
		if (max_len >= 160)
			return -EOPNOTSUPP;

		if (op->data.nbytes > 160 - max_len)
			op->data.nbytes = 160 - max_len;
	}

	return 0;
}

static bool airoha_snand_supports_op(struct spi_mem *mem,
				     const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (op->cmd.buswidth != 1)
		return false;

	if (airoha_snand_is_page_ops(op))
		return true;

	return (!op->addr.nbytes || op->addr.buswidth == 1) &&
	       (!op->dummy.nbytes || op->dummy.buswidth == 1) &&
	       (!op->data.nbytes || op->data.buswidth == 1);
}

static int airoha_snand_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	u8 *txrx_buf = spi_get_ctldata(desc->mem->spi);

	if (!txrx_buf)
		return -EINVAL;

	if (desc->info.offset + desc->info.length > U32_MAX)
		return -EINVAL;

	/* continuous reading is not supported */
	if (desc->info.length > SPI_NAND_CACHE_SIZE)
		return -E2BIG;

	if (!airoha_snand_supports_op(desc->mem, &desc->info.op_tmpl))
		return -EOPNOTSUPP;

	return 0;
}

static ssize_t airoha_snand_dirmap_read(struct spi_mem_dirmap_desc *desc,
					u64 offs, size_t len, void *buf)
{
	struct spi_mem_op *op = &desc->info.op_tmpl;
	struct spi_device *spi = desc->mem->spi;
	struct airoha_snand_ctrl *as_ctrl;
	u8 *txrx_buf = spi_get_ctldata(spi);
	dma_addr_t dma_addr;
	u32 val, rd_mode;
	int err;

	switch (op->cmd.opcode) {
	case SPI_NAND_OP_READ_FROM_CACHE_DUAL:
		rd_mode = 1;
		break;
	case SPI_NAND_OP_READ_FROM_CACHE_QUAD:
		rd_mode = 2;
		break;
	default:
		rd_mode = 0;
		break;
	}

	as_ctrl = spi_controller_get_devdata(spi->controller);
	err = airoha_snand_set_mode(as_ctrl, SPI_MODE_DMA);
	if (err < 0)
		return err;

	err = airoha_snand_nfi_config(as_ctrl);
	if (err)
		goto error_dma_mode_off;

	dma_addr = dma_map_single(as_ctrl->dev, txrx_buf, SPI_NAND_CACHE_SIZE,
				  DMA_FROM_DEVICE);
	err = dma_mapping_error(as_ctrl->dev, dma_addr);
	if (err)
		goto error_dma_mode_off;

	/* set dma addr */
	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_STRADDR,
			   dma_addr);
	if (err)
		goto error_dma_unmap;

	/* set cust sec size */
	val = as_ctrl->nfi_cfg.sec_size * as_ctrl->nfi_cfg.sec_num;
	val = FIELD_PREP(SPI_NFI_READ_DATA_BYTE_NUM, val);
	err = regmap_update_bits(as_ctrl->regmap_nfi,
				 REG_SPI_NFI_SNF_MISC_CTL2,
				 SPI_NFI_READ_DATA_BYTE_NUM, val);
	if (err)
		goto error_dma_unmap;

	/* set read command */
	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_RD_CTL2,
			   op->cmd.opcode);
	if (err)
		goto error_dma_unmap;

	/* set read mode */
	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_SNF_MISC_CTL,
			   FIELD_PREP(SPI_NFI_DATA_READ_WR_MODE, rd_mode));
	if (err)
		goto error_dma_unmap;

	/* set read addr: zero page offset + descriptor read offset */
	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_RD_CTL3,
			   desc->info.offset);
	if (err)
		goto error_dma_unmap;

	/* set nfi read */
	err = regmap_update_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
				 SPI_NFI_OPMODE,
				 FIELD_PREP(SPI_NFI_OPMODE, 6));
	if (err)
		goto error_dma_unmap;

	err = regmap_set_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
			      SPI_NFI_READ_MODE | SPI_NFI_DMA_MODE);
	if (err)
		goto error_dma_unmap;

	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_CMD, 0x0);
	if (err)
		goto error_dma_unmap;

	/* trigger dma start read */
	err = regmap_clear_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CON,
				SPI_NFI_RD_TRIG);
	if (err)
		goto error_dma_unmap;

	err = regmap_set_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CON,
			      SPI_NFI_RD_TRIG);
	if (err)
		goto error_dma_unmap;

	err = regmap_read_poll_timeout(as_ctrl->regmap_nfi,
				       REG_SPI_NFI_SNF_STA_CTL1, val,
				       (val & SPI_NFI_READ_FROM_CACHE_DONE),
				       0, 1 * USEC_PER_SEC);
	if (err)
		goto error_dma_unmap;

	/*
	 * SPI_NFI_READ_FROM_CACHE_DONE bit must be written at the end
	 * of dirmap_read operation even if it is already set.
	 */
	err = regmap_write_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_SNF_STA_CTL1,
				SPI_NFI_READ_FROM_CACHE_DONE,
				SPI_NFI_READ_FROM_CACHE_DONE);
	if (err)
		goto error_dma_unmap;

	err = regmap_read_poll_timeout(as_ctrl->regmap_nfi, REG_SPI_NFI_INTR,
				       val, (val & SPI_NFI_AHB_DONE), 0,
				       1 * USEC_PER_SEC);
	if (err)
		goto error_dma_unmap;

	/* DMA read need delay for data ready from controller to DRAM */
	udelay(1);

	dma_unmap_single(as_ctrl->dev, dma_addr, SPI_NAND_CACHE_SIZE,
			 DMA_FROM_DEVICE);
	err = airoha_snand_set_mode(as_ctrl, SPI_MODE_MANUAL);
	if (err < 0)
		return err;

	memcpy(buf, txrx_buf + offs, len);

	return len;

error_dma_unmap:
	dma_unmap_single(as_ctrl->dev, dma_addr, SPI_NAND_CACHE_SIZE,
			 DMA_FROM_DEVICE);
error_dma_mode_off:
	airoha_snand_set_mode(as_ctrl, SPI_MODE_MANUAL);
	return err;
}

static ssize_t airoha_snand_dirmap_write(struct spi_mem_dirmap_desc *desc,
					 u64 offs, size_t len, const void *buf)
{
	struct spi_mem_op *op = &desc->info.op_tmpl;
	struct spi_device *spi = desc->mem->spi;
	u8 *txrx_buf = spi_get_ctldata(spi);
	struct airoha_snand_ctrl *as_ctrl;
	dma_addr_t dma_addr;
	u32 wr_mode, val;
	int err;

	as_ctrl = spi_controller_get_devdata(spi->controller);
	err = airoha_snand_set_mode(as_ctrl, SPI_MODE_MANUAL);
	if (err < 0)
		return err;

	memcpy(txrx_buf + offs, buf, len);
	dma_addr = dma_map_single(as_ctrl->dev, txrx_buf, SPI_NAND_CACHE_SIZE,
				  DMA_TO_DEVICE);
	err = dma_mapping_error(as_ctrl->dev, dma_addr);
	if (err)
		return err;

	err = airoha_snand_set_mode(as_ctrl, SPI_MODE_DMA);
	if (err < 0)
		goto error_dma_unmap;

	err = airoha_snand_nfi_config(as_ctrl);
	if (err)
		goto error_dma_unmap;

	if (op->cmd.opcode == SPI_NAND_OP_PROGRAM_LOAD_QUAD ||
	    op->cmd.opcode == SPI_NAND_OP_PROGRAM_LOAD_RAMDON_QUAD)
		wr_mode = BIT(1);
	else
		wr_mode = 0;

	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_STRADDR,
			   dma_addr);
	if (err)
		goto error_dma_unmap;

	val = FIELD_PREP(SPI_NFI_PROG_LOAD_BYTE_NUM,
			 as_ctrl->nfi_cfg.sec_size * as_ctrl->nfi_cfg.sec_num);
	err = regmap_update_bits(as_ctrl->regmap_nfi,
				 REG_SPI_NFI_SNF_MISC_CTL2,
				 SPI_NFI_PROG_LOAD_BYTE_NUM, val);
	if (err)
		goto error_dma_unmap;

	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_PG_CTL1,
			   FIELD_PREP(SPI_NFI_PG_LOAD_CMD,
				      op->cmd.opcode));
	if (err)
		goto error_dma_unmap;

	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_SNF_MISC_CTL,
			   FIELD_PREP(SPI_NFI_DATA_READ_WR_MODE, wr_mode));
	if (err)
		goto error_dma_unmap;

	/* set write addr: zero page offset + descriptor write offset */
	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_PG_CTL2,
			   desc->info.offset);
	if (err)
		goto error_dma_unmap;

	err = regmap_clear_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
				SPI_NFI_READ_MODE);
	if (err)
		goto error_dma_unmap;

	err = regmap_update_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
				 SPI_NFI_OPMODE,
				 FIELD_PREP(SPI_NFI_OPMODE, 3));
	if (err)
		goto error_dma_unmap;

	err = regmap_set_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CNFG,
			      SPI_NFI_DMA_MODE);
	if (err)
		goto error_dma_unmap;

	err = regmap_write(as_ctrl->regmap_nfi, REG_SPI_NFI_CMD, 0x80);
	if (err)
		goto error_dma_unmap;

	err = regmap_clear_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CON,
				SPI_NFI_WR_TRIG);
	if (err)
		goto error_dma_unmap;

	err = regmap_set_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_CON,
			      SPI_NFI_WR_TRIG);
	if (err)
		goto error_dma_unmap;

	err = regmap_read_poll_timeout(as_ctrl->regmap_nfi, REG_SPI_NFI_INTR,
				       val, (val & SPI_NFI_AHB_DONE), 0,
				       1 * USEC_PER_SEC);
	if (err)
		goto error_dma_unmap;

	err = regmap_read_poll_timeout(as_ctrl->regmap_nfi,
				       REG_SPI_NFI_SNF_STA_CTL1, val,
				       (val & SPI_NFI_LOAD_TO_CACHE_DONE),
				       0, 1 * USEC_PER_SEC);
	if (err)
		goto error_dma_unmap;

	/*
	 * SPI_NFI_LOAD_TO_CACHE_DONE bit must be written at the end
	 * of dirmap_write operation even if it is already set.
	 */
	err = regmap_write_bits(as_ctrl->regmap_nfi, REG_SPI_NFI_SNF_STA_CTL1,
				SPI_NFI_LOAD_TO_CACHE_DONE,
				SPI_NFI_LOAD_TO_CACHE_DONE);
	if (err)
		goto error_dma_unmap;

	dma_unmap_single(as_ctrl->dev, dma_addr, SPI_NAND_CACHE_SIZE,
			 DMA_TO_DEVICE);
	err = airoha_snand_set_mode(as_ctrl, SPI_MODE_MANUAL);
	if (err < 0)
		return err;

	return len;

error_dma_unmap:
	dma_unmap_single(as_ctrl->dev, dma_addr, SPI_NAND_CACHE_SIZE,
			 DMA_TO_DEVICE);
	airoha_snand_set_mode(as_ctrl, SPI_MODE_MANUAL);
	return err;
}

static int airoha_snand_exec_op(struct spi_mem *mem,
				const struct spi_mem_op *op)
{
	struct airoha_snand_ctrl *as_ctrl;
	int op_len, addr_len, dummy_len;
	u8 buf[20], *data;
	int i, err;

	as_ctrl = spi_controller_get_devdata(mem->spi->controller);

	op_len = op->cmd.nbytes;
	addr_len = op->addr.nbytes;
	dummy_len = op->dummy.nbytes;

	if (op_len + dummy_len + addr_len > sizeof(buf))
		return -EIO;

	data = buf;
	for (i = 0; i < op_len; i++)
		*data++ = op->cmd.opcode >> (8 * (op_len - i - 1));
	for (i = 0; i < addr_len; i++)
		*data++ = op->addr.val >> (8 * (addr_len - i - 1));
	for (i = 0; i < dummy_len; i++)
		*data++ = 0xff;

	/* switch to manual mode */
	err = airoha_snand_set_mode(as_ctrl, SPI_MODE_MANUAL);
	if (err < 0)
		return err;

	err = airoha_snand_set_cs(as_ctrl, SPI_CHIP_SEL_LOW);
	if (err < 0)
		return err;

	/* opcode */
	data = buf;
	err = airoha_snand_write_data(as_ctrl, data, op_len,
				      op->cmd.buswidth);
	if (err)
		return err;

	/* addr part */
	data += op_len;
	if (addr_len) {
		err = airoha_snand_write_data(as_ctrl, data, addr_len,
					      op->addr.buswidth);
		if (err)
			return err;
	}

	/* dummy */
	data += addr_len;
	if (dummy_len) {
		err = airoha_snand_write_data(as_ctrl, data, dummy_len,
					      op->dummy.buswidth);
		if (err)
			return err;
	}

	/* data */
	if (op->data.nbytes) {
		if (op->data.dir == SPI_MEM_DATA_IN)
			err = airoha_snand_read_data(as_ctrl, op->data.buf.in,
						     op->data.nbytes,
						     op->data.buswidth);
		else
			err = airoha_snand_write_data(as_ctrl, op->data.buf.out,
						      op->data.nbytes,
						      op->data.buswidth);
		if (err)
			return err;
	}

	return airoha_snand_set_cs(as_ctrl, SPI_CHIP_SEL_HIGH);
}

static const struct spi_controller_mem_ops airoha_snand_mem_ops = {
	.adjust_op_size = airoha_snand_adjust_op_size,
	.supports_op = airoha_snand_supports_op,
	.exec_op = airoha_snand_exec_op,
	.dirmap_create = airoha_snand_dirmap_create,
	.dirmap_read = airoha_snand_dirmap_read,
	.dirmap_write = airoha_snand_dirmap_write,
};

static int airoha_snand_setup(struct spi_device *spi)
{
	struct airoha_snand_ctrl *as_ctrl;
	u8 *txrx_buf;

	/* prepare device buffer */
	as_ctrl = spi_controller_get_devdata(spi->controller);
	txrx_buf = devm_kzalloc(as_ctrl->dev, SPI_NAND_CACHE_SIZE,
				GFP_KERNEL);
	if (!txrx_buf)
		return -ENOMEM;

	spi_set_ctldata(spi, txrx_buf);

	return 0;
}

static int airoha_snand_nfi_setup(struct airoha_snand_ctrl *as_ctrl)
{
	u32 val, sec_size, sec_num;
	int err;

	err = regmap_read(as_ctrl->regmap_nfi, REG_SPI_NFI_CON, &val);
	if (err)
		return err;

	sec_num = FIELD_GET(SPI_NFI_SEC_NUM, val);

	err = regmap_read(as_ctrl->regmap_nfi, REG_SPI_NFI_SECCUS_SIZE, &val);
	if (err)
		return err;

	sec_size = FIELD_GET(SPI_NFI_CUS_SEC_SIZE, val);

	/* init default value */
	as_ctrl->nfi_cfg.sec_size = sec_size;
	as_ctrl->nfi_cfg.sec_num = sec_num;
	as_ctrl->nfi_cfg.page_size = round_down(sec_size * sec_num, 1024);
	as_ctrl->nfi_cfg.spare_size = 16;

	err = airoha_snand_nfi_init(as_ctrl);
	if (err)
		return err;

	return airoha_snand_nfi_config(as_ctrl);
}

static const struct regmap_config spi_ctrl_regmap_config = {
	.name		= "ctrl",
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= REG_SPI_CTRL_NFI2SPI_EN,
};

static const struct regmap_config spi_nfi_regmap_config = {
	.name		= "nfi",
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= REG_SPI_NFI_SNF_NFI_CNFG,
};

static const struct of_device_id airoha_snand_ids[] = {
	{ .compatible	= "airoha,en7581-snand" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_snand_ids);

static int airoha_snand_probe(struct platform_device *pdev)
{
	struct airoha_snand_ctrl *as_ctrl;
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	void __iomem *base;
	int err;

	ctrl = devm_spi_alloc_host(dev, sizeof(*as_ctrl));
	if (!ctrl)
		return -ENOMEM;

	as_ctrl = spi_controller_get_devdata(ctrl);
	as_ctrl->dev = dev;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	as_ctrl->regmap_ctrl = devm_regmap_init_mmio(dev, base,
						     &spi_ctrl_regmap_config);
	if (IS_ERR(as_ctrl->regmap_ctrl))
		return dev_err_probe(dev, PTR_ERR(as_ctrl->regmap_ctrl),
				     "failed to init spi ctrl regmap\n");

	base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base))
		return PTR_ERR(base);

	as_ctrl->regmap_nfi = devm_regmap_init_mmio(dev, base,
						    &spi_nfi_regmap_config);
	if (IS_ERR(as_ctrl->regmap_nfi))
		return dev_err_probe(dev, PTR_ERR(as_ctrl->regmap_nfi),
				     "failed to init spi nfi regmap\n");

	as_ctrl->spi_clk = devm_clk_get_enabled(dev, "spi");
	if (IS_ERR(as_ctrl->spi_clk))
		return dev_err_probe(dev, PTR_ERR(as_ctrl->spi_clk),
				     "unable to get spi clk\n");

	err = dma_set_mask(as_ctrl->dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	ctrl->num_chipselect = 2;
	ctrl->mem_ops = &airoha_snand_mem_ops;
	ctrl->bits_per_word_mask = SPI_BPW_MASK(8);
	ctrl->mode_bits = SPI_RX_DUAL;
	ctrl->setup = airoha_snand_setup;
	device_set_node(&ctrl->dev, dev_fwnode(dev));

	err = airoha_snand_nfi_setup(as_ctrl);
	if (err)
		return err;

	return devm_spi_register_controller(dev, ctrl);
}

static struct platform_driver airoha_snand_driver = {
	.driver = {
		.name = "airoha-spi",
		.of_match_table = airoha_snand_ids,
	},
	.probe = airoha_snand_probe,
};
module_platform_driver(airoha_snand_driver);

MODULE_DESCRIPTION("Airoha SPI-NAND Flash Controller Driver");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Ray Liu <ray.liu@airoha.com>");
MODULE_LICENSE("GPL");

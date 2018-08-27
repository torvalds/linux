/*
 * Hisilicon NAND Flash controller driver
 *
 * Copyright © 2012-2014 HiSilicon Technologies Co., Ltd.
 *              http://www.hisilicon.com
 *
 * Author: Zhou Wang <wangzhou.bry@gmail.com>
 * The initial developer of the original code is Zhiyong Cai
 * <caizhiyong@huawei.com>
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
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/sizes.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/rawnand.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>

#define HINFC504_MAX_CHIP                               (4)
#define HINFC504_W_LATCH                                (5)
#define HINFC504_R_LATCH                                (7)
#define HINFC504_RW_LATCH                               (3)

#define HINFC504_NFC_TIMEOUT				(2 * HZ)
#define HINFC504_NFC_PM_TIMEOUT				(1 * HZ)
#define HINFC504_NFC_DMA_TIMEOUT			(5 * HZ)
#define HINFC504_CHIP_DELAY				(25)

#define HINFC504_REG_BASE_ADDRESS_LEN			(0x100)
#define HINFC504_BUFFER_BASE_ADDRESS_LEN		(2048 + 128)

#define HINFC504_ADDR_CYCLE_MASK			0x4

#define HINFC504_CON					0x00
#define HINFC504_CON_OP_MODE_NORMAL			BIT(0)
#define HINFC504_CON_PAGEISZE_SHIFT			(1)
#define HINFC504_CON_PAGESIZE_MASK			(0x07)
#define HINFC504_CON_BUS_WIDTH				BIT(4)
#define HINFC504_CON_READY_BUSY_SEL			BIT(8)
#define HINFC504_CON_ECCTYPE_SHIFT			(9)
#define HINFC504_CON_ECCTYPE_MASK			(0x07)

#define HINFC504_PWIDTH					0x04
#define SET_HINFC504_PWIDTH(_w_lcnt, _r_lcnt, _rw_hcnt) \
	((_w_lcnt) | (((_r_lcnt) & 0x0F) << 4) | (((_rw_hcnt) & 0x0F) << 8))

#define HINFC504_CMD					0x0C
#define HINFC504_ADDRL					0x10
#define HINFC504_ADDRH					0x14
#define HINFC504_DATA_NUM				0x18

#define HINFC504_OP					0x1C
#define HINFC504_OP_READ_DATA_EN			BIT(1)
#define HINFC504_OP_WAIT_READY_EN			BIT(2)
#define HINFC504_OP_CMD2_EN				BIT(3)
#define HINFC504_OP_WRITE_DATA_EN			BIT(4)
#define HINFC504_OP_ADDR_EN				BIT(5)
#define HINFC504_OP_CMD1_EN				BIT(6)
#define HINFC504_OP_NF_CS_SHIFT                         (7)
#define HINFC504_OP_NF_CS_MASK				(3)
#define HINFC504_OP_ADDR_CYCLE_SHIFT			(9)
#define HINFC504_OP_ADDR_CYCLE_MASK			(7)

#define HINFC504_STATUS                                 0x20
#define HINFC504_READY					BIT(0)

#define HINFC504_INTEN					0x24
#define HINFC504_INTEN_DMA				BIT(9)
#define HINFC504_INTEN_UE				BIT(6)
#define HINFC504_INTEN_CE				BIT(5)

#define HINFC504_INTS					0x28
#define HINFC504_INTS_DMA				BIT(9)
#define HINFC504_INTS_UE				BIT(6)
#define HINFC504_INTS_CE				BIT(5)

#define HINFC504_INTCLR                                 0x2C
#define HINFC504_INTCLR_DMA				BIT(9)
#define HINFC504_INTCLR_UE				BIT(6)
#define HINFC504_INTCLR_CE				BIT(5)

#define HINFC504_ECC_STATUS                             0x5C
#define HINFC504_ECC_16_BIT_SHIFT                       12

#define HINFC504_DMA_CTRL				0x60
#define HINFC504_DMA_CTRL_DMA_START			BIT(0)
#define HINFC504_DMA_CTRL_WE				BIT(1)
#define HINFC504_DMA_CTRL_DATA_AREA_EN			BIT(2)
#define HINFC504_DMA_CTRL_OOB_AREA_EN			BIT(3)
#define HINFC504_DMA_CTRL_BURST4_EN			BIT(4)
#define HINFC504_DMA_CTRL_BURST8_EN			BIT(5)
#define HINFC504_DMA_CTRL_BURST16_EN			BIT(6)
#define HINFC504_DMA_CTRL_ADDR_NUM_SHIFT		(7)
#define HINFC504_DMA_CTRL_ADDR_NUM_MASK                 (1)
#define HINFC504_DMA_CTRL_CS_SHIFT			(8)
#define HINFC504_DMA_CTRL_CS_MASK			(0x03)

#define HINFC504_DMA_ADDR_DATA				0x64
#define HINFC504_DMA_ADDR_OOB				0x68

#define HINFC504_DMA_LEN				0x6C
#define HINFC504_DMA_LEN_OOB_SHIFT			(16)
#define HINFC504_DMA_LEN_OOB_MASK			(0xFFF)

#define HINFC504_DMA_PARA				0x70
#define HINFC504_DMA_PARA_DATA_RW_EN			BIT(0)
#define HINFC504_DMA_PARA_OOB_RW_EN			BIT(1)
#define HINFC504_DMA_PARA_DATA_EDC_EN			BIT(2)
#define HINFC504_DMA_PARA_OOB_EDC_EN			BIT(3)
#define HINFC504_DMA_PARA_DATA_ECC_EN			BIT(4)
#define HINFC504_DMA_PARA_OOB_ECC_EN			BIT(5)

#define HINFC_VERSION                                   0x74
#define HINFC504_LOG_READ_ADDR				0x7C
#define HINFC504_LOG_READ_LEN				0x80

#define HINFC504_NANDINFO_LEN				0x10

struct hinfc_host {
	struct nand_chip	chip;
	struct device		*dev;
	void __iomem		*iobase;
	void __iomem		*mmio;
	struct completion       cmd_complete;
	unsigned int		offset;
	unsigned int		command;
	int			chipselect;
	unsigned int		addr_cycle;
	u32                     addr_value[2];
	u32                     cache_addr_value[2];
	char			*buffer;
	dma_addr_t		dma_buffer;
	dma_addr_t		dma_oob;
	int			version;
	unsigned int            irq_status; /* interrupt status */
};

static inline unsigned int hinfc_read(struct hinfc_host *host, unsigned int reg)
{
	return readl(host->iobase + reg);
}

static inline void hinfc_write(struct hinfc_host *host, unsigned int value,
			       unsigned int reg)
{
	writel(value, host->iobase + reg);
}

static void wait_controller_finished(struct hinfc_host *host)
{
	unsigned long timeout = jiffies + HINFC504_NFC_TIMEOUT;
	int val;

	while (time_before(jiffies, timeout)) {
		val = hinfc_read(host, HINFC504_STATUS);
		if (host->command == NAND_CMD_ERASE2) {
			/* nfc is ready */
			while (!(val & HINFC504_READY))	{
				usleep_range(500, 1000);
				val = hinfc_read(host, HINFC504_STATUS);
			}
			return;
		}

		if (val & HINFC504_READY)
			return;
	}

	/* wait cmd timeout */
	dev_err(host->dev, "Wait NAND controller exec cmd timeout.\n");
}

static void hisi_nfc_dma_transfer(struct hinfc_host *host, int todev)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info	*mtd = nand_to_mtd(chip);
	unsigned long val;
	int ret;

	hinfc_write(host, host->dma_buffer, HINFC504_DMA_ADDR_DATA);
	hinfc_write(host, host->dma_oob, HINFC504_DMA_ADDR_OOB);

	if (chip->ecc.mode == NAND_ECC_NONE) {
		hinfc_write(host, ((mtd->oobsize & HINFC504_DMA_LEN_OOB_MASK)
			<< HINFC504_DMA_LEN_OOB_SHIFT), HINFC504_DMA_LEN);

		hinfc_write(host, HINFC504_DMA_PARA_DATA_RW_EN
			| HINFC504_DMA_PARA_OOB_RW_EN, HINFC504_DMA_PARA);
	} else {
		if (host->command == NAND_CMD_READOOB)
			hinfc_write(host, HINFC504_DMA_PARA_OOB_RW_EN
			| HINFC504_DMA_PARA_OOB_EDC_EN
			| HINFC504_DMA_PARA_OOB_ECC_EN, HINFC504_DMA_PARA);
		else
			hinfc_write(host, HINFC504_DMA_PARA_DATA_RW_EN
			| HINFC504_DMA_PARA_OOB_RW_EN
			| HINFC504_DMA_PARA_DATA_EDC_EN
			| HINFC504_DMA_PARA_OOB_EDC_EN
			| HINFC504_DMA_PARA_DATA_ECC_EN
			| HINFC504_DMA_PARA_OOB_ECC_EN, HINFC504_DMA_PARA);

	}

	val = (HINFC504_DMA_CTRL_DMA_START | HINFC504_DMA_CTRL_BURST4_EN
		| HINFC504_DMA_CTRL_BURST8_EN | HINFC504_DMA_CTRL_BURST16_EN
		| HINFC504_DMA_CTRL_DATA_AREA_EN | HINFC504_DMA_CTRL_OOB_AREA_EN
		| ((host->addr_cycle == 4 ? 1 : 0)
			<< HINFC504_DMA_CTRL_ADDR_NUM_SHIFT)
		| ((host->chipselect & HINFC504_DMA_CTRL_CS_MASK)
			<< HINFC504_DMA_CTRL_CS_SHIFT));

	if (todev)
		val |= HINFC504_DMA_CTRL_WE;

	init_completion(&host->cmd_complete);

	hinfc_write(host, val, HINFC504_DMA_CTRL);
	ret = wait_for_completion_timeout(&host->cmd_complete,
			HINFC504_NFC_DMA_TIMEOUT);

	if (!ret) {
		dev_err(host->dev, "DMA operation(irq) timeout!\n");
		/* sanity check */
		val = hinfc_read(host, HINFC504_DMA_CTRL);
		if (!(val & HINFC504_DMA_CTRL_DMA_START))
			dev_err(host->dev, "DMA is already done but without irq ACK!\n");
		else
			dev_err(host->dev, "DMA is really timeout!\n");
	}
}

static int hisi_nfc_send_cmd_pageprog(struct hinfc_host *host)
{
	host->addr_value[0] &= 0xffff0000;

	hinfc_write(host, host->addr_value[0], HINFC504_ADDRL);
	hinfc_write(host, host->addr_value[1], HINFC504_ADDRH);
	hinfc_write(host, NAND_CMD_PAGEPROG << 8 | NAND_CMD_SEQIN,
		    HINFC504_CMD);

	hisi_nfc_dma_transfer(host, 1);

	return 0;
}

static int hisi_nfc_send_cmd_readstart(struct hinfc_host *host)
{
	struct mtd_info	*mtd = nand_to_mtd(&host->chip);

	if ((host->addr_value[0] == host->cache_addr_value[0]) &&
	    (host->addr_value[1] == host->cache_addr_value[1]))
		return 0;

	host->addr_value[0] &= 0xffff0000;

	hinfc_write(host, host->addr_value[0], HINFC504_ADDRL);
	hinfc_write(host, host->addr_value[1], HINFC504_ADDRH);
	hinfc_write(host, NAND_CMD_READSTART << 8 | NAND_CMD_READ0,
		    HINFC504_CMD);

	hinfc_write(host, 0, HINFC504_LOG_READ_ADDR);
	hinfc_write(host, mtd->writesize + mtd->oobsize,
		    HINFC504_LOG_READ_LEN);

	hisi_nfc_dma_transfer(host, 0);

	host->cache_addr_value[0] = host->addr_value[0];
	host->cache_addr_value[1] = host->addr_value[1];

	return 0;
}

static int hisi_nfc_send_cmd_erase(struct hinfc_host *host)
{
	hinfc_write(host, host->addr_value[0], HINFC504_ADDRL);
	hinfc_write(host, (NAND_CMD_ERASE2 << 8) | NAND_CMD_ERASE1,
		    HINFC504_CMD);

	hinfc_write(host, HINFC504_OP_WAIT_READY_EN
		| HINFC504_OP_CMD2_EN
		| HINFC504_OP_CMD1_EN
		| HINFC504_OP_ADDR_EN
		| ((host->chipselect & HINFC504_OP_NF_CS_MASK)
			<< HINFC504_OP_NF_CS_SHIFT)
		| ((host->addr_cycle & HINFC504_OP_ADDR_CYCLE_MASK)
			<< HINFC504_OP_ADDR_CYCLE_SHIFT),
		HINFC504_OP);

	wait_controller_finished(host);

	return 0;
}

static int hisi_nfc_send_cmd_readid(struct hinfc_host *host)
{
	hinfc_write(host, HINFC504_NANDINFO_LEN, HINFC504_DATA_NUM);
	hinfc_write(host, NAND_CMD_READID, HINFC504_CMD);
	hinfc_write(host, 0, HINFC504_ADDRL);

	hinfc_write(host, HINFC504_OP_CMD1_EN | HINFC504_OP_ADDR_EN
		| HINFC504_OP_READ_DATA_EN
		| ((host->chipselect & HINFC504_OP_NF_CS_MASK)
			<< HINFC504_OP_NF_CS_SHIFT)
		| 1 << HINFC504_OP_ADDR_CYCLE_SHIFT, HINFC504_OP);

	wait_controller_finished(host);

	return 0;
}

static int hisi_nfc_send_cmd_status(struct hinfc_host *host)
{
	hinfc_write(host, HINFC504_NANDINFO_LEN, HINFC504_DATA_NUM);
	hinfc_write(host, NAND_CMD_STATUS, HINFC504_CMD);
	hinfc_write(host, HINFC504_OP_CMD1_EN
		| HINFC504_OP_READ_DATA_EN
		| ((host->chipselect & HINFC504_OP_NF_CS_MASK)
			<< HINFC504_OP_NF_CS_SHIFT),
		HINFC504_OP);

	wait_controller_finished(host);

	return 0;
}

static int hisi_nfc_send_cmd_reset(struct hinfc_host *host, int chipselect)
{
	hinfc_write(host, NAND_CMD_RESET, HINFC504_CMD);

	hinfc_write(host, HINFC504_OP_CMD1_EN
		| ((chipselect & HINFC504_OP_NF_CS_MASK)
			<< HINFC504_OP_NF_CS_SHIFT)
		| HINFC504_OP_WAIT_READY_EN,
		HINFC504_OP);

	wait_controller_finished(host);

	return 0;
}

static void hisi_nfc_select_chip(struct mtd_info *mtd, int chipselect)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);

	if (chipselect < 0)
		return;

	host->chipselect = chipselect;
}

static uint8_t hisi_nfc_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);

	if (host->command == NAND_CMD_STATUS)
		return *(uint8_t *)(host->mmio);

	host->offset++;

	if (host->command == NAND_CMD_READID)
		return *(uint8_t *)(host->mmio + host->offset - 1);

	return *(uint8_t *)(host->buffer + host->offset - 1);
}

static u16 hisi_nfc_read_word(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);

	host->offset += 2;
	return *(u16 *)(host->buffer + host->offset - 2);
}

static void
hisi_nfc_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);

	memcpy(host->buffer + host->offset, buf, len);
	host->offset += len;
}

static void hisi_nfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);

	memcpy(buf, host->buffer + host->offset, len);
	host->offset += len;
}

static void set_addr(struct mtd_info *mtd, int column, int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);
	unsigned int command = host->command;

	host->addr_cycle    = 0;
	host->addr_value[0] = 0;
	host->addr_value[1] = 0;

	/* Serially input address */
	if (column != -1) {
		/* Adjust columns for 16 bit buswidth */
		if (chip->options & NAND_BUSWIDTH_16 &&
				!nand_opcode_8bits(command))
			column >>= 1;

		host->addr_value[0] = column & 0xffff;
		host->addr_cycle    = 2;
	}
	if (page_addr != -1) {
		host->addr_value[0] |= (page_addr & 0xffff)
			<< (host->addr_cycle * 8);
		host->addr_cycle    += 2;
		if (chip->options & NAND_ROW_ADDR_3) {
			host->addr_cycle += 1;
			if (host->command == NAND_CMD_ERASE1)
				host->addr_value[0] |= ((page_addr >> 16) & 0xff) << 16;
			else
				host->addr_value[1] |= ((page_addr >> 16) & 0xff);
		}
	}
}

static void hisi_nfc_cmdfunc(struct mtd_info *mtd, unsigned command, int column,
		int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct hinfc_host *host = nand_get_controller_data(chip);
	int is_cache_invalid = 1;
	unsigned int flag = 0;

	host->command =  command;

	switch (command) {
	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		if (command == NAND_CMD_READ0)
			host->offset = column;
		else
			host->offset = column + mtd->writesize;

		is_cache_invalid = 0;
		set_addr(mtd, column, page_addr);
		hisi_nfc_send_cmd_readstart(host);
		break;

	case NAND_CMD_SEQIN:
		host->offset = column;
		set_addr(mtd, column, page_addr);
		break;

	case NAND_CMD_ERASE1:
		set_addr(mtd, column, page_addr);
		break;

	case NAND_CMD_PAGEPROG:
		hisi_nfc_send_cmd_pageprog(host);
		break;

	case NAND_CMD_ERASE2:
		hisi_nfc_send_cmd_erase(host);
		break;

	case NAND_CMD_READID:
		host->offset = column;
		memset(host->mmio, 0, 0x10);
		hisi_nfc_send_cmd_readid(host);
		break;

	case NAND_CMD_STATUS:
		flag = hinfc_read(host, HINFC504_CON);
		if (chip->ecc.mode == NAND_ECC_HW)
			hinfc_write(host,
				    flag & ~(HINFC504_CON_ECCTYPE_MASK <<
				    HINFC504_CON_ECCTYPE_SHIFT), HINFC504_CON);

		host->offset = 0;
		memset(host->mmio, 0, 0x10);
		hisi_nfc_send_cmd_status(host);
		hinfc_write(host, flag, HINFC504_CON);
		break;

	case NAND_CMD_RESET:
		hisi_nfc_send_cmd_reset(host, host->chipselect);
		break;

	default:
		dev_err(host->dev, "Error: unsupported cmd(cmd=%x, col=%x, page=%x)\n",
			command, column, page_addr);
	}

	if (is_cache_invalid) {
		host->cache_addr_value[0] = ~0;
		host->cache_addr_value[1] = ~0;
	}
}

static irqreturn_t hinfc_irq_handle(int irq, void *devid)
{
	struct hinfc_host *host = devid;
	unsigned int flag;

	flag = hinfc_read(host, HINFC504_INTS);
	/* store interrupts state */
	host->irq_status |= flag;

	if (flag & HINFC504_INTS_DMA) {
		hinfc_write(host, HINFC504_INTCLR_DMA, HINFC504_INTCLR);
		complete(&host->cmd_complete);
	} else if (flag & HINFC504_INTS_CE) {
		hinfc_write(host, HINFC504_INTCLR_CE, HINFC504_INTCLR);
	} else if (flag & HINFC504_INTS_UE) {
		hinfc_write(host, HINFC504_INTCLR_UE, HINFC504_INTCLR);
	}

	return IRQ_HANDLED;
}

static int hisi_nand_read_page_hwecc(struct mtd_info *mtd,
	struct nand_chip *chip, uint8_t *buf, int oob_required, int page)
{
	struct hinfc_host *host = nand_get_controller_data(chip);
	int max_bitflips = 0, stat = 0, stat_max = 0, status_ecc;
	int stat_1, stat_2;

	nand_read_page_op(chip, page, 0, buf, mtd->writesize);
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	/* errors which can not be corrected by ECC */
	if (host->irq_status & HINFC504_INTS_UE) {
		mtd->ecc_stats.failed++;
	} else if (host->irq_status & HINFC504_INTS_CE) {
		/* TODO: need add other ECC modes! */
		switch (chip->ecc.strength) {
		case 16:
			status_ecc = hinfc_read(host, HINFC504_ECC_STATUS) >>
					HINFC504_ECC_16_BIT_SHIFT & 0x0fff;
			stat_2 = status_ecc & 0x3f;
			stat_1 = status_ecc >> 6 & 0x3f;
			stat = stat_1 + stat_2;
			stat_max = max_t(int, stat_1, stat_2);
		}
		mtd->ecc_stats.corrected += stat;
		max_bitflips = max_t(int, max_bitflips, stat_max);
	}
	host->irq_status = 0;

	return max_bitflips;
}

static int hisi_nand_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
				int page)
{
	struct hinfc_host *host = nand_get_controller_data(chip);

	nand_read_oob_op(chip, page, 0, chip->oob_poi, mtd->oobsize);

	if (host->irq_status & HINFC504_INTS_UE) {
		host->irq_status = 0;
		return -EBADMSG;
	}

	host->irq_status = 0;
	return 0;
}

static int hisi_nand_write_page_hwecc(struct mtd_info *mtd,
		struct nand_chip *chip, const uint8_t *buf, int oob_required,
		int page)
{
	nand_prog_page_begin_op(chip, page, 0, buf, mtd->writesize);
	if (oob_required)
		chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);

	return nand_prog_page_end_op(chip);
}

static void hisi_nfc_host_init(struct hinfc_host *host)
{
	struct nand_chip *chip = &host->chip;
	unsigned int flag = 0;

	host->version = hinfc_read(host, HINFC_VERSION);
	host->addr_cycle		= 0;
	host->addr_value[0]		= 0;
	host->addr_value[1]		= 0;
	host->cache_addr_value[0]	= ~0;
	host->cache_addr_value[1]	= ~0;
	host->chipselect		= 0;

	/* default page size: 2K, ecc_none. need modify */
	flag = HINFC504_CON_OP_MODE_NORMAL | HINFC504_CON_READY_BUSY_SEL
		| ((0x001 & HINFC504_CON_PAGESIZE_MASK)
			<< HINFC504_CON_PAGEISZE_SHIFT)
		| ((0x0 & HINFC504_CON_ECCTYPE_MASK)
			<< HINFC504_CON_ECCTYPE_SHIFT)
		| ((chip->options & NAND_BUSWIDTH_16) ?
			HINFC504_CON_BUS_WIDTH : 0);
	hinfc_write(host, flag, HINFC504_CON);

	memset(host->mmio, 0xff, HINFC504_BUFFER_BASE_ADDRESS_LEN);

	hinfc_write(host, SET_HINFC504_PWIDTH(HINFC504_W_LATCH,
		    HINFC504_R_LATCH, HINFC504_RW_LATCH), HINFC504_PWIDTH);

	/* enable DMA irq */
	hinfc_write(host, HINFC504_INTEN_DMA, HINFC504_INTEN);
}

static int hisi_ooblayout_ecc(struct mtd_info *mtd, int section,
			      struct mtd_oob_region *oobregion)
{
	/* FIXME: add ECC bytes position */
	return -ENOTSUPP;
}

static int hisi_ooblayout_free(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 2;
	oobregion->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops hisi_ooblayout_ops = {
	.ecc = hisi_ooblayout_ecc,
	.free = hisi_ooblayout_free,
};

static int hisi_nfc_ecc_probe(struct hinfc_host *host)
{
	unsigned int flag;
	int size, strength, ecc_bits;
	struct device *dev = host->dev;
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);

	size = chip->ecc.size;
	strength = chip->ecc.strength;
	if (size != 1024) {
		dev_err(dev, "error ecc size: %d\n", size);
		return -EINVAL;
	}

	if ((size == 1024) && ((strength != 8) && (strength != 16) &&
				(strength != 24) && (strength != 40))) {
		dev_err(dev, "ecc size and strength do not match\n");
		return -EINVAL;
	}

	chip->ecc.size = size;
	chip->ecc.strength = strength;

	chip->ecc.read_page = hisi_nand_read_page_hwecc;
	chip->ecc.read_oob = hisi_nand_read_oob;
	chip->ecc.write_page = hisi_nand_write_page_hwecc;

	switch (chip->ecc.strength) {
	case 16:
		ecc_bits = 6;
		if (mtd->writesize == 2048)
			mtd_set_ooblayout(mtd, &hisi_ooblayout_ops);

		/* TODO: add more page size support */
		break;

	/* TODO: add more ecc strength support */
	default:
		dev_err(dev, "not support strength: %d\n", chip->ecc.strength);
		return -EINVAL;
	}

	flag = hinfc_read(host, HINFC504_CON);
	/* add ecc type configure */
	flag |= ((ecc_bits & HINFC504_CON_ECCTYPE_MASK)
						<< HINFC504_CON_ECCTYPE_SHIFT);
	hinfc_write(host, flag, HINFC504_CON);

	/* enable ecc irq */
	flag = hinfc_read(host, HINFC504_INTEN) & 0xfff;
	hinfc_write(host, flag | HINFC504_INTEN_UE | HINFC504_INTEN_CE,
		    HINFC504_INTEN);

	return 0;
}

static int hisi_nfc_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct hinfc_host *host = nand_get_controller_data(chip);
	int flag;

	host->buffer = dmam_alloc_coherent(host->dev,
					   mtd->writesize + mtd->oobsize,
					   &host->dma_buffer, GFP_KERNEL);
	if (!host->buffer)
		return -ENOMEM;

	host->dma_oob = host->dma_buffer + mtd->writesize;
	memset(host->buffer, 0xff, mtd->writesize + mtd->oobsize);

	flag = hinfc_read(host, HINFC504_CON);
	flag &= ~(HINFC504_CON_PAGESIZE_MASK << HINFC504_CON_PAGEISZE_SHIFT);
	switch (mtd->writesize) {
	case 2048:
		flag |= (0x001 << HINFC504_CON_PAGEISZE_SHIFT);
		break;
	/*
	 * TODO: add more pagesize support,
	 * default pagesize has been set in hisi_nfc_host_init
	 */
	default:
		dev_err(host->dev, "NON-2KB page size nand flash\n");
		return -EINVAL;
	}
	hinfc_write(host, flag, HINFC504_CON);

	if (chip->ecc.mode == NAND_ECC_HW)
		hisi_nfc_ecc_probe(host);

	return 0;
}

static const struct nand_controller_ops hisi_nfc_controller_ops = {
	.attach_chip = hisi_nfc_attach_chip,
};

static int hisi_nfc_probe(struct platform_device *pdev)
{
	int ret = 0, irq, max_chips = HINFC504_MAX_CHIP;
	struct device *dev = &pdev->dev;
	struct hinfc_host *host;
	struct nand_chip  *chip;
	struct mtd_info   *mtd;
	struct resource	  *res;
	struct device_node *np = dev->of_node;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;
	host->dev = dev;

	platform_set_drvdata(pdev, host);
	chip = &host->chip;
	mtd  = nand_to_mtd(chip);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no IRQ resource defined\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->iobase))
		return PTR_ERR(host->iobase);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	host->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->mmio)) {
		dev_err(dev, "devm_ioremap_resource[1] fail\n");
		return PTR_ERR(host->mmio);
	}

	mtd->name		= "hisi_nand";
	mtd->dev.parent         = &pdev->dev;

	nand_set_controller_data(chip, host);
	nand_set_flash_node(chip, np);
	chip->cmdfunc		= hisi_nfc_cmdfunc;
	chip->select_chip	= hisi_nfc_select_chip;
	chip->read_byte		= hisi_nfc_read_byte;
	chip->read_word		= hisi_nfc_read_word;
	chip->write_buf		= hisi_nfc_write_buf;
	chip->read_buf		= hisi_nfc_read_buf;
	chip->chip_delay	= HINFC504_CHIP_DELAY;
	chip->set_features	= nand_get_set_features_notsupp;
	chip->get_features	= nand_get_set_features_notsupp;

	hisi_nfc_host_init(host);

	ret = devm_request_irq(dev, irq, hinfc_irq_handle, 0x0, "nandc", host);
	if (ret) {
		dev_err(dev, "failed to request IRQ\n");
		return ret;
	}

	chip->dummy_controller.ops = &hisi_nfc_controller_ops;
	ret = nand_scan(mtd, max_chips);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "Err MTD partition=%d\n", ret);
		nand_cleanup(chip);
		return ret;
	}

	return 0;
}

static int hisi_nfc_remove(struct platform_device *pdev)
{
	struct hinfc_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = nand_to_mtd(&host->chip);

	nand_release(mtd);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hisi_nfc_suspend(struct device *dev)
{
	struct hinfc_host *host = dev_get_drvdata(dev);
	unsigned long timeout = jiffies + HINFC504_NFC_PM_TIMEOUT;

	while (time_before(jiffies, timeout)) {
		if (((hinfc_read(host, HINFC504_STATUS) & 0x1) == 0x0) &&
		    (hinfc_read(host, HINFC504_DMA_CTRL) &
		     HINFC504_DMA_CTRL_DMA_START)) {
			cond_resched();
			return 0;
		}
	}

	dev_err(host->dev, "nand controller suspend timeout.\n");

	return -EAGAIN;
}

static int hisi_nfc_resume(struct device *dev)
{
	int cs;
	struct hinfc_host *host = dev_get_drvdata(dev);
	struct nand_chip *chip = &host->chip;

	for (cs = 0; cs < chip->numchips; cs++)
		hisi_nfc_send_cmd_reset(host, cs);
	hinfc_write(host, SET_HINFC504_PWIDTH(HINFC504_W_LATCH,
		    HINFC504_R_LATCH, HINFC504_RW_LATCH), HINFC504_PWIDTH);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(hisi_nfc_pm_ops, hisi_nfc_suspend, hisi_nfc_resume);

static const struct of_device_id nfc_id_table[] = {
	{ .compatible = "hisilicon,504-nfc" },
	{}
};
MODULE_DEVICE_TABLE(of, nfc_id_table);

static struct platform_driver hisi_nfc_driver = {
	.driver = {
		.name  = "hisi_nand",
		.of_match_table = nfc_id_table,
		.pm = &hisi_nfc_pm_ops,
	},
	.probe		= hisi_nfc_probe,
	.remove		= hisi_nfc_remove,
};

module_platform_driver(hisi_nfc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhou Wang");
MODULE_AUTHOR("Zhiyong Cai");
MODULE_DESCRIPTION("Hisilicon Nand Flash Controller Driver");

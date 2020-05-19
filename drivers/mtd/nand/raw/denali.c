// SPDX-License-Identifier: GPL-2.0
/*
 * NAND Flash Controller Device Driver
 * Copyright © 2009-2010, Intel Corporation and its suppliers.
 *
 * Copyright (c) 2017-2019 Socionext Inc.
 *   Reworked by Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "denali.h"

#define DENALI_NAND_NAME    "denali-nand"

/* for Indexed Addressing */
#define DENALI_INDEXED_CTRL	0x00
#define DENALI_INDEXED_DATA	0x10

#define DENALI_MAP00		(0 << 26)	/* direct access to buffer */
#define DENALI_MAP01		(1 << 26)	/* read/write pages in PIO */
#define DENALI_MAP10		(2 << 26)	/* high-level control plane */
#define DENALI_MAP11		(3 << 26)	/* direct controller access */

/* MAP11 access cycle type */
#define DENALI_MAP11_CMD	((DENALI_MAP11) | 0)	/* command cycle */
#define DENALI_MAP11_ADDR	((DENALI_MAP11) | 1)	/* address cycle */
#define DENALI_MAP11_DATA	((DENALI_MAP11) | 2)	/* data cycle */

#define DENALI_BANK(denali)	((denali)->active_bank << 24)

#define DENALI_INVALID_BANK	-1

static struct denali_chip *to_denali_chip(struct nand_chip *chip)
{
	return container_of(chip, struct denali_chip, chip);
}

static struct denali_controller *to_denali_controller(struct nand_chip *chip)
{
	return container_of(chip->controller, struct denali_controller,
			    controller);
}

/*
 * Direct Addressing - the slave address forms the control information (command
 * type, bank, block, and page address).  The slave data is the actual data to
 * be transferred.  This mode requires 28 bits of address region allocated.
 */
static u32 denali_direct_read(struct denali_controller *denali, u32 addr)
{
	return ioread32(denali->host + addr);
}

static void denali_direct_write(struct denali_controller *denali, u32 addr,
				u32 data)
{
	iowrite32(data, denali->host + addr);
}

/*
 * Indexed Addressing - address translation module intervenes in passing the
 * control information.  This mode reduces the required address range.  The
 * control information and transferred data are latched by the registers in
 * the translation module.
 */
static u32 denali_indexed_read(struct denali_controller *denali, u32 addr)
{
	iowrite32(addr, denali->host + DENALI_INDEXED_CTRL);
	return ioread32(denali->host + DENALI_INDEXED_DATA);
}

static void denali_indexed_write(struct denali_controller *denali, u32 addr,
				 u32 data)
{
	iowrite32(addr, denali->host + DENALI_INDEXED_CTRL);
	iowrite32(data, denali->host + DENALI_INDEXED_DATA);
}

static void denali_enable_irq(struct denali_controller *denali)
{
	int i;

	for (i = 0; i < denali->nbanks; i++)
		iowrite32(U32_MAX, denali->reg + INTR_EN(i));
	iowrite32(GLOBAL_INT_EN_FLAG, denali->reg + GLOBAL_INT_ENABLE);
}

static void denali_disable_irq(struct denali_controller *denali)
{
	int i;

	for (i = 0; i < denali->nbanks; i++)
		iowrite32(0, denali->reg + INTR_EN(i));
	iowrite32(0, denali->reg + GLOBAL_INT_ENABLE);
}

static void denali_clear_irq(struct denali_controller *denali,
			     int bank, u32 irq_status)
{
	/* write one to clear bits */
	iowrite32(irq_status, denali->reg + INTR_STATUS(bank));
}

static void denali_clear_irq_all(struct denali_controller *denali)
{
	int i;

	for (i = 0; i < denali->nbanks; i++)
		denali_clear_irq(denali, i, U32_MAX);
}

static irqreturn_t denali_isr(int irq, void *dev_id)
{
	struct denali_controller *denali = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u32 irq_status;
	int i;

	spin_lock(&denali->irq_lock);

	for (i = 0; i < denali->nbanks; i++) {
		irq_status = ioread32(denali->reg + INTR_STATUS(i));
		if (irq_status)
			ret = IRQ_HANDLED;

		denali_clear_irq(denali, i, irq_status);

		if (i != denali->active_bank)
			continue;

		denali->irq_status |= irq_status;

		if (denali->irq_status & denali->irq_mask)
			complete(&denali->complete);
	}

	spin_unlock(&denali->irq_lock);

	return ret;
}

static void denali_reset_irq(struct denali_controller *denali)
{
	unsigned long flags;

	spin_lock_irqsave(&denali->irq_lock, flags);
	denali->irq_status = 0;
	denali->irq_mask = 0;
	spin_unlock_irqrestore(&denali->irq_lock, flags);
}

static u32 denali_wait_for_irq(struct denali_controller *denali, u32 irq_mask)
{
	unsigned long time_left, flags;
	u32 irq_status;

	spin_lock_irqsave(&denali->irq_lock, flags);

	irq_status = denali->irq_status;

	if (irq_mask & irq_status) {
		/* return immediately if the IRQ has already happened. */
		spin_unlock_irqrestore(&denali->irq_lock, flags);
		return irq_status;
	}

	denali->irq_mask = irq_mask;
	reinit_completion(&denali->complete);
	spin_unlock_irqrestore(&denali->irq_lock, flags);

	time_left = wait_for_completion_timeout(&denali->complete,
						msecs_to_jiffies(1000));
	if (!time_left) {
		dev_err(denali->dev, "timeout while waiting for irq 0x%x\n",
			irq_mask);
		return 0;
	}

	return denali->irq_status;
}

static void denali_select_target(struct nand_chip *chip, int cs)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct denali_chip_sel *sel = &to_denali_chip(chip)->sels[cs];
	struct mtd_info *mtd = nand_to_mtd(chip);

	denali->active_bank = sel->bank;

	iowrite32(1 << (chip->phys_erase_shift - chip->page_shift),
		  denali->reg + PAGES_PER_BLOCK);
	iowrite32(chip->options & NAND_BUSWIDTH_16 ? 1 : 0,
		  denali->reg + DEVICE_WIDTH);
	iowrite32(mtd->writesize, denali->reg + DEVICE_MAIN_AREA_SIZE);
	iowrite32(mtd->oobsize, denali->reg + DEVICE_SPARE_AREA_SIZE);
	iowrite32(chip->options & NAND_ROW_ADDR_3 ?
		  0 : TWO_ROW_ADDR_CYCLES__FLAG,
		  denali->reg + TWO_ROW_ADDR_CYCLES);
	iowrite32(FIELD_PREP(ECC_CORRECTION__ERASE_THRESHOLD, 1) |
		  FIELD_PREP(ECC_CORRECTION__VALUE, chip->ecc.strength),
		  denali->reg + ECC_CORRECTION);
	iowrite32(chip->ecc.size, denali->reg + CFG_DATA_BLOCK_SIZE);
	iowrite32(chip->ecc.size, denali->reg + CFG_LAST_DATA_BLOCK_SIZE);
	iowrite32(chip->ecc.steps, denali->reg + CFG_NUM_DATA_BLOCKS);

	if (chip->options & NAND_KEEP_TIMINGS)
		return;

	/* update timing registers unless NAND_KEEP_TIMINGS is set */
	iowrite32(sel->hwhr2_and_we_2_re, denali->reg + TWHR2_AND_WE_2_RE);
	iowrite32(sel->tcwaw_and_addr_2_data,
		  denali->reg + TCWAW_AND_ADDR_2_DATA);
	iowrite32(sel->re_2_we, denali->reg + RE_2_WE);
	iowrite32(sel->acc_clks, denali->reg + ACC_CLKS);
	iowrite32(sel->rdwr_en_lo_cnt, denali->reg + RDWR_EN_LO_CNT);
	iowrite32(sel->rdwr_en_hi_cnt, denali->reg + RDWR_EN_HI_CNT);
	iowrite32(sel->cs_setup_cnt, denali->reg + CS_SETUP_CNT);
	iowrite32(sel->re_2_re, denali->reg + RE_2_RE);
}

static int denali_change_column(struct nand_chip *chip, unsigned int offset,
				void *buf, unsigned int len, bool write)
{
	if (write)
		return nand_change_write_column_op(chip, offset, buf, len,
						   false);
	else
		return nand_change_read_column_op(chip, offset, buf, len,
						  false);
}

static int denali_payload_xfer(struct nand_chip *chip, void *buf, bool write)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int writesize = mtd->writesize;
	int oob_skip = denali->oob_skip_bytes;
	int ret, i, pos, len;

	for (i = 0; i < ecc->steps; i++) {
		pos = i * (ecc->size + ecc->bytes);
		len = ecc->size;

		if (pos >= writesize) {
			pos += oob_skip;
		} else if (pos + len > writesize) {
			/* This chunk overwraps the BBM area. Must be split */
			ret = denali_change_column(chip, pos, buf,
						   writesize - pos, write);
			if (ret)
				return ret;

			buf += writesize - pos;
			len -= writesize - pos;
			pos = writesize + oob_skip;
		}

		ret = denali_change_column(chip, pos, buf, len, write);
		if (ret)
			return ret;

		buf += len;
	}

	return 0;
}

static int denali_oob_xfer(struct nand_chip *chip, void *buf, bool write)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int writesize = mtd->writesize;
	int oobsize = mtd->oobsize;
	int oob_skip = denali->oob_skip_bytes;
	int ret, i, pos, len;

	/* BBM at the beginning of the OOB area */
	ret = denali_change_column(chip, writesize, buf, oob_skip, write);
	if (ret)
		return ret;

	buf += oob_skip;

	for (i = 0; i < ecc->steps; i++) {
		pos = ecc->size + i * (ecc->size + ecc->bytes);

		if (i == ecc->steps - 1)
			/* The last chunk includes OOB free */
			len = writesize + oobsize - pos - oob_skip;
		else
			len = ecc->bytes;

		if (pos >= writesize) {
			pos += oob_skip;
		} else if (pos + len > writesize) {
			/* This chunk overwraps the BBM area. Must be split */
			ret = denali_change_column(chip, pos, buf,
						   writesize - pos, write);
			if (ret)
				return ret;

			buf += writesize - pos;
			len -= writesize - pos;
			pos = writesize + oob_skip;
		}

		ret = denali_change_column(chip, pos, buf, len, write);
		if (ret)
			return ret;

		buf += len;
	}

	return 0;
}

static int denali_read_raw(struct nand_chip *chip, void *buf, void *oob_buf,
			   int page)
{
	int ret;

	if (!buf && !oob_buf)
		return -EINVAL;

	ret = nand_read_page_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	if (buf) {
		ret = denali_payload_xfer(chip, buf, false);
		if (ret)
			return ret;
	}

	if (oob_buf) {
		ret = denali_oob_xfer(chip, oob_buf, false);
		if (ret)
			return ret;
	}

	return 0;
}

static int denali_write_raw(struct nand_chip *chip, const void *buf,
			    const void *oob_buf, int page)
{
	int ret;

	if (!buf && !oob_buf)
		return -EINVAL;

	ret = nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	if (ret)
		return ret;

	if (buf) {
		ret = denali_payload_xfer(chip, (void *)buf, true);
		if (ret)
			return ret;
	}

	if (oob_buf) {
		ret = denali_oob_xfer(chip, (void *)oob_buf, true);
		if (ret)
			return ret;
	}

	return nand_prog_page_end_op(chip);
}

static int denali_read_page_raw(struct nand_chip *chip, u8 *buf,
				int oob_required, int page)
{
	return denali_read_raw(chip, buf, oob_required ? chip->oob_poi : NULL,
			       page);
}

static int denali_write_page_raw(struct nand_chip *chip, const u8 *buf,
				 int oob_required, int page)
{
	return denali_write_raw(chip, buf, oob_required ? chip->oob_poi : NULL,
				page);
}

static int denali_read_oob(struct nand_chip *chip, int page)
{
	return denali_read_raw(chip, NULL, chip->oob_poi, page);
}

static int denali_write_oob(struct nand_chip *chip, int page)
{
	return denali_write_raw(chip, NULL, chip->oob_poi, page);
}

static int denali_check_erased_page(struct nand_chip *chip, u8 *buf,
				    unsigned long uncor_ecc_flags,
				    unsigned int max_bitflips)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_ecc_stats *ecc_stats = &nand_to_mtd(chip)->ecc_stats;
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *ecc_code = chip->oob_poi + denali->oob_skip_bytes;
	int i, stat;

	for (i = 0; i < ecc->steps; i++) {
		if (!(uncor_ecc_flags & BIT(i)))
			continue;

		stat = nand_check_erased_ecc_chunk(buf, ecc->size, ecc_code,
						   ecc->bytes, NULL, 0,
						   ecc->strength);
		if (stat < 0) {
			ecc_stats->failed++;
		} else {
			ecc_stats->corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}

		buf += ecc->size;
		ecc_code += ecc->bytes;
	}

	return max_bitflips;
}

static int denali_hw_ecc_fixup(struct nand_chip *chip,
			       unsigned long *uncor_ecc_flags)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_ecc_stats *ecc_stats = &nand_to_mtd(chip)->ecc_stats;
	int bank = denali->active_bank;
	u32 ecc_cor;
	unsigned int max_bitflips;

	ecc_cor = ioread32(denali->reg + ECC_COR_INFO(bank));
	ecc_cor >>= ECC_COR_INFO__SHIFT(bank);

	if (ecc_cor & ECC_COR_INFO__UNCOR_ERR) {
		/*
		 * This flag is set when uncorrectable error occurs at least in
		 * one ECC sector.  We can not know "how many sectors", or
		 * "which sector(s)".  We need erase-page check for all sectors.
		 */
		*uncor_ecc_flags = GENMASK(chip->ecc.steps - 1, 0);
		return 0;
	}

	max_bitflips = FIELD_GET(ECC_COR_INFO__MAX_ERRORS, ecc_cor);

	/*
	 * The register holds the maximum of per-sector corrected bitflips.
	 * This is suitable for the return value of the ->read_page() callback.
	 * Unfortunately, we can not know the total number of corrected bits in
	 * the page.  Increase the stats by max_bitflips. (compromised solution)
	 */
	ecc_stats->corrected += max_bitflips;

	return max_bitflips;
}

static int denali_sw_ecc_fixup(struct nand_chip *chip,
			       unsigned long *uncor_ecc_flags, u8 *buf)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_ecc_stats *ecc_stats = &nand_to_mtd(chip)->ecc_stats;
	unsigned int ecc_size = chip->ecc.size;
	unsigned int bitflips = 0;
	unsigned int max_bitflips = 0;
	u32 err_addr, err_cor_info;
	unsigned int err_byte, err_sector, err_device;
	u8 err_cor_value;
	unsigned int prev_sector = 0;
	u32 irq_status;

	denali_reset_irq(denali);

	do {
		err_addr = ioread32(denali->reg + ECC_ERROR_ADDRESS);
		err_sector = FIELD_GET(ECC_ERROR_ADDRESS__SECTOR, err_addr);
		err_byte = FIELD_GET(ECC_ERROR_ADDRESS__OFFSET, err_addr);

		err_cor_info = ioread32(denali->reg + ERR_CORRECTION_INFO);
		err_cor_value = FIELD_GET(ERR_CORRECTION_INFO__BYTE,
					  err_cor_info);
		err_device = FIELD_GET(ERR_CORRECTION_INFO__DEVICE,
				       err_cor_info);

		/* reset the bitflip counter when crossing ECC sector */
		if (err_sector != prev_sector)
			bitflips = 0;

		if (err_cor_info & ERR_CORRECTION_INFO__UNCOR) {
			/*
			 * Check later if this is a real ECC error, or
			 * an erased sector.
			 */
			*uncor_ecc_flags |= BIT(err_sector);
		} else if (err_byte < ecc_size) {
			/*
			 * If err_byte is larger than ecc_size, means error
			 * happened in OOB, so we ignore it. It's no need for
			 * us to correct it err_device is represented the NAND
			 * error bits are happened in if there are more than
			 * one NAND connected.
			 */
			int offset;
			unsigned int flips_in_byte;

			offset = (err_sector * ecc_size + err_byte) *
					denali->devs_per_cs + err_device;

			/* correct the ECC error */
			flips_in_byte = hweight8(buf[offset] ^ err_cor_value);
			buf[offset] ^= err_cor_value;
			ecc_stats->corrected += flips_in_byte;
			bitflips += flips_in_byte;

			max_bitflips = max(max_bitflips, bitflips);
		}

		prev_sector = err_sector;
	} while (!(err_cor_info & ERR_CORRECTION_INFO__LAST_ERR));

	/*
	 * Once handle all ECC errors, controller will trigger an
	 * ECC_TRANSACTION_DONE interrupt.
	 */
	irq_status = denali_wait_for_irq(denali, INTR__ECC_TRANSACTION_DONE);
	if (!(irq_status & INTR__ECC_TRANSACTION_DONE))
		return -EIO;

	return max_bitflips;
}

static void denali_setup_dma64(struct denali_controller *denali,
			       dma_addr_t dma_addr, int page, bool write)
{
	u32 mode;
	const int page_count = 1;

	mode = DENALI_MAP10 | DENALI_BANK(denali) | page;

	/* DMA is a three step process */

	/*
	 * 1. setup transfer type, interrupt when complete,
	 *    burst len = 64 bytes, the number of pages
	 */
	denali->host_write(denali, mode,
			   0x01002000 | (64 << 16) |
			   (write ? BIT(8) : 0) | page_count);

	/* 2. set memory low address */
	denali->host_write(denali, mode, lower_32_bits(dma_addr));

	/* 3. set memory high address */
	denali->host_write(denali, mode, upper_32_bits(dma_addr));
}

static void denali_setup_dma32(struct denali_controller *denali,
			       dma_addr_t dma_addr, int page, bool write)
{
	u32 mode;
	const int page_count = 1;

	mode = DENALI_MAP10 | DENALI_BANK(denali);

	/* DMA is a four step process */

	/* 1. setup transfer type and # of pages */
	denali->host_write(denali, mode | page,
			   0x2000 | (write ? BIT(8) : 0) | page_count);

	/* 2. set memory high address bits 23:8 */
	denali->host_write(denali, mode | ((dma_addr >> 16) << 8), 0x2200);

	/* 3. set memory low address bits 23:8 */
	denali->host_write(denali, mode | ((dma_addr & 0xffff) << 8), 0x2300);

	/* 4. interrupt when complete, burst len = 64 bytes */
	denali->host_write(denali, mode | 0x14000, 0x2400);
}

static int denali_pio_read(struct denali_controller *denali, u32 *buf,
			   size_t size, int page)
{
	u32 addr = DENALI_MAP01 | DENALI_BANK(denali) | page;
	u32 irq_status, ecc_err_mask;
	int i;

	if (denali->caps & DENALI_CAP_HW_ECC_FIXUP)
		ecc_err_mask = INTR__ECC_UNCOR_ERR;
	else
		ecc_err_mask = INTR__ECC_ERR;

	denali_reset_irq(denali);

	for (i = 0; i < size / 4; i++)
		buf[i] = denali->host_read(denali, addr);

	irq_status = denali_wait_for_irq(denali, INTR__PAGE_XFER_INC);
	if (!(irq_status & INTR__PAGE_XFER_INC))
		return -EIO;

	if (irq_status & INTR__ERASED_PAGE)
		memset(buf, 0xff, size);

	return irq_status & ecc_err_mask ? -EBADMSG : 0;
}

static int denali_pio_write(struct denali_controller *denali, const u32 *buf,
			    size_t size, int page)
{
	u32 addr = DENALI_MAP01 | DENALI_BANK(denali) | page;
	u32 irq_status;
	int i;

	denali_reset_irq(denali);

	for (i = 0; i < size / 4; i++)
		denali->host_write(denali, addr, buf[i]);

	irq_status = denali_wait_for_irq(denali,
					 INTR__PROGRAM_COMP |
					 INTR__PROGRAM_FAIL);
	if (!(irq_status & INTR__PROGRAM_COMP))
		return -EIO;

	return 0;
}

static int denali_pio_xfer(struct denali_controller *denali, void *buf,
			   size_t size, int page, bool write)
{
	if (write)
		return denali_pio_write(denali, buf, size, page);
	else
		return denali_pio_read(denali, buf, size, page);
}

static int denali_dma_xfer(struct denali_controller *denali, void *buf,
			   size_t size, int page, bool write)
{
	dma_addr_t dma_addr;
	u32 irq_mask, irq_status, ecc_err_mask;
	enum dma_data_direction dir = write ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	int ret = 0;

	dma_addr = dma_map_single(denali->dev, buf, size, dir);
	if (dma_mapping_error(denali->dev, dma_addr)) {
		dev_dbg(denali->dev, "Failed to DMA-map buffer. Trying PIO.\n");
		return denali_pio_xfer(denali, buf, size, page, write);
	}

	if (write) {
		/*
		 * INTR__PROGRAM_COMP is never asserted for the DMA transfer.
		 * We can use INTR__DMA_CMD_COMP instead.  This flag is asserted
		 * when the page program is completed.
		 */
		irq_mask = INTR__DMA_CMD_COMP | INTR__PROGRAM_FAIL;
		ecc_err_mask = 0;
	} else if (denali->caps & DENALI_CAP_HW_ECC_FIXUP) {
		irq_mask = INTR__DMA_CMD_COMP;
		ecc_err_mask = INTR__ECC_UNCOR_ERR;
	} else {
		irq_mask = INTR__DMA_CMD_COMP;
		ecc_err_mask = INTR__ECC_ERR;
	}

	iowrite32(DMA_ENABLE__FLAG, denali->reg + DMA_ENABLE);
	/*
	 * The ->setup_dma() hook kicks DMA by using the data/command
	 * interface, which belongs to a different AXI port from the
	 * register interface.  Read back the register to avoid a race.
	 */
	ioread32(denali->reg + DMA_ENABLE);

	denali_reset_irq(denali);
	denali->setup_dma(denali, dma_addr, page, write);

	irq_status = denali_wait_for_irq(denali, irq_mask);
	if (!(irq_status & INTR__DMA_CMD_COMP))
		ret = -EIO;
	else if (irq_status & ecc_err_mask)
		ret = -EBADMSG;

	iowrite32(0, denali->reg + DMA_ENABLE);

	dma_unmap_single(denali->dev, dma_addr, size, dir);

	if (irq_status & INTR__ERASED_PAGE)
		memset(buf, 0xff, size);

	return ret;
}

static int denali_page_xfer(struct nand_chip *chip, void *buf, size_t size,
			    int page, bool write)
{
	struct denali_controller *denali = to_denali_controller(chip);

	denali_select_target(chip, chip->cur_cs);

	if (denali->dma_avail)
		return denali_dma_xfer(denali, buf, size, page, write);
	else
		return denali_pio_xfer(denali, buf, size, page, write);
}

static int denali_read_page(struct nand_chip *chip, u8 *buf,
			    int oob_required, int page)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned long uncor_ecc_flags = 0;
	int stat = 0;
	int ret;

	ret = denali_page_xfer(chip, buf, mtd->writesize, page, false);
	if (ret && ret != -EBADMSG)
		return ret;

	if (denali->caps & DENALI_CAP_HW_ECC_FIXUP)
		stat = denali_hw_ecc_fixup(chip, &uncor_ecc_flags);
	else if (ret == -EBADMSG)
		stat = denali_sw_ecc_fixup(chip, &uncor_ecc_flags, buf);

	if (stat < 0)
		return stat;

	if (uncor_ecc_flags) {
		ret = denali_read_oob(chip, page);
		if (ret)
			return ret;

		stat = denali_check_erased_page(chip, buf,
						uncor_ecc_flags, stat);
	}

	return stat;
}

static int denali_write_page(struct nand_chip *chip, const u8 *buf,
			     int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return denali_page_xfer(chip, (void *)buf, mtd->writesize, page, true);
}

static int denali_setup_data_interface(struct nand_chip *chip, int chipnr,
				       const struct nand_data_interface *conf)
{
	static const unsigned int data_setup_on_host = 10000;
	struct denali_controller *denali = to_denali_controller(chip);
	struct denali_chip_sel *sel;
	const struct nand_sdr_timings *timings;
	unsigned long t_x, mult_x;
	int acc_clks, re_2_we, re_2_re, we_2_re, addr_2_data;
	int rdwr_en_lo, rdwr_en_hi, rdwr_en_lo_hi, cs_setup;
	int addr_2_data_mask;
	u32 tmp;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return PTR_ERR(timings);

	/* clk_x period in picoseconds */
	t_x = DIV_ROUND_DOWN_ULL(1000000000000ULL, denali->clk_x_rate);
	if (!t_x)
		return -EINVAL;

	/*
	 * The bus interface clock, clk_x, is phase aligned with the core clock.
	 * The clk_x is an integral multiple N of the core clk.  The value N is
	 * configured at IP delivery time, and its available value is 4, 5, 6.
	 */
	mult_x = DIV_ROUND_CLOSEST_ULL(denali->clk_x_rate, denali->clk_rate);
	if (mult_x < 4 || mult_x > 6)
		return -EINVAL;

	if (chipnr == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	sel = &to_denali_chip(chip)->sels[chipnr];

	/* tRWH -> RE_2_WE */
	re_2_we = DIV_ROUND_UP(timings->tRHW_min, t_x);
	re_2_we = min_t(int, re_2_we, RE_2_WE__VALUE);

	tmp = ioread32(denali->reg + RE_2_WE);
	tmp &= ~RE_2_WE__VALUE;
	tmp |= FIELD_PREP(RE_2_WE__VALUE, re_2_we);
	sel->re_2_we = tmp;

	/* tRHZ -> RE_2_RE */
	re_2_re = DIV_ROUND_UP(timings->tRHZ_max, t_x);
	re_2_re = min_t(int, re_2_re, RE_2_RE__VALUE);

	tmp = ioread32(denali->reg + RE_2_RE);
	tmp &= ~RE_2_RE__VALUE;
	tmp |= FIELD_PREP(RE_2_RE__VALUE, re_2_re);
	sel->re_2_re = tmp;

	/*
	 * tCCS, tWHR -> WE_2_RE
	 *
	 * With WE_2_RE properly set, the Denali controller automatically takes
	 * care of the delay; the driver need not set NAND_WAIT_TCCS.
	 */
	we_2_re = DIV_ROUND_UP(max(timings->tCCS_min, timings->tWHR_min), t_x);
	we_2_re = min_t(int, we_2_re, TWHR2_AND_WE_2_RE__WE_2_RE);

	tmp = ioread32(denali->reg + TWHR2_AND_WE_2_RE);
	tmp &= ~TWHR2_AND_WE_2_RE__WE_2_RE;
	tmp |= FIELD_PREP(TWHR2_AND_WE_2_RE__WE_2_RE, we_2_re);
	sel->hwhr2_and_we_2_re = tmp;

	/* tADL -> ADDR_2_DATA */

	/* for older versions, ADDR_2_DATA is only 6 bit wide */
	addr_2_data_mask = TCWAW_AND_ADDR_2_DATA__ADDR_2_DATA;
	if (denali->revision < 0x0501)
		addr_2_data_mask >>= 1;

	addr_2_data = DIV_ROUND_UP(timings->tADL_min, t_x);
	addr_2_data = min_t(int, addr_2_data, addr_2_data_mask);

	tmp = ioread32(denali->reg + TCWAW_AND_ADDR_2_DATA);
	tmp &= ~TCWAW_AND_ADDR_2_DATA__ADDR_2_DATA;
	tmp |= FIELD_PREP(TCWAW_AND_ADDR_2_DATA__ADDR_2_DATA, addr_2_data);
	sel->tcwaw_and_addr_2_data = tmp;

	/* tREH, tWH -> RDWR_EN_HI_CNT */
	rdwr_en_hi = DIV_ROUND_UP(max(timings->tREH_min, timings->tWH_min),
				  t_x);
	rdwr_en_hi = min_t(int, rdwr_en_hi, RDWR_EN_HI_CNT__VALUE);

	tmp = ioread32(denali->reg + RDWR_EN_HI_CNT);
	tmp &= ~RDWR_EN_HI_CNT__VALUE;
	tmp |= FIELD_PREP(RDWR_EN_HI_CNT__VALUE, rdwr_en_hi);
	sel->rdwr_en_hi_cnt = tmp;

	/*
	 * tREA -> ACC_CLKS
	 * tRP, tWP, tRHOH, tRC, tWC -> RDWR_EN_LO_CNT
	 */

	/*
	 * Determine the minimum of acc_clks to meet the setup timing when
	 * capturing the incoming data.
	 *
	 * The delay on the chip side is well-defined as tREA, but we need to
	 * take additional delay into account. This includes a certain degree
	 * of unknowledge, such as signal propagation delays on the PCB and
	 * in the SoC, load capacity of the I/O pins, etc.
	 */
	acc_clks = DIV_ROUND_UP(timings->tREA_max + data_setup_on_host, t_x);

	/* Determine the minimum of rdwr_en_lo_cnt from RE#/WE# pulse width */
	rdwr_en_lo = DIV_ROUND_UP(max(timings->tRP_min, timings->tWP_min), t_x);

	/* Extend rdwr_en_lo to meet the data hold timing */
	rdwr_en_lo = max_t(int, rdwr_en_lo,
			   acc_clks - timings->tRHOH_min / t_x);

	/* Extend rdwr_en_lo to meet the requirement for RE#/WE# cycle time */
	rdwr_en_lo_hi = DIV_ROUND_UP(max(timings->tRC_min, timings->tWC_min),
				     t_x);
	rdwr_en_lo = max(rdwr_en_lo, rdwr_en_lo_hi - rdwr_en_hi);
	rdwr_en_lo = min_t(int, rdwr_en_lo, RDWR_EN_LO_CNT__VALUE);

	/* Center the data latch timing for extra safety */
	acc_clks = (acc_clks + rdwr_en_lo +
		    DIV_ROUND_UP(timings->tRHOH_min, t_x)) / 2;
	acc_clks = min_t(int, acc_clks, ACC_CLKS__VALUE);

	tmp = ioread32(denali->reg + ACC_CLKS);
	tmp &= ~ACC_CLKS__VALUE;
	tmp |= FIELD_PREP(ACC_CLKS__VALUE, acc_clks);
	sel->acc_clks = tmp;

	tmp = ioread32(denali->reg + RDWR_EN_LO_CNT);
	tmp &= ~RDWR_EN_LO_CNT__VALUE;
	tmp |= FIELD_PREP(RDWR_EN_LO_CNT__VALUE, rdwr_en_lo);
	sel->rdwr_en_lo_cnt = tmp;

	/* tCS, tCEA -> CS_SETUP_CNT */
	cs_setup = max3((int)DIV_ROUND_UP(timings->tCS_min, t_x) - rdwr_en_lo,
			(int)DIV_ROUND_UP(timings->tCEA_max, t_x) - acc_clks,
			0);
	cs_setup = min_t(int, cs_setup, CS_SETUP_CNT__VALUE);

	tmp = ioread32(denali->reg + CS_SETUP_CNT);
	tmp &= ~CS_SETUP_CNT__VALUE;
	tmp |= FIELD_PREP(CS_SETUP_CNT__VALUE, cs_setup);
	sel->cs_setup_cnt = tmp;

	return 0;
}

int denali_calc_ecc_bytes(int step_size, int strength)
{
	/* BCH code.  Denali requires ecc.bytes to be multiple of 2 */
	return DIV_ROUND_UP(strength * fls(step_size * 8), 16) * 2;
}
EXPORT_SYMBOL(denali_calc_ecc_bytes);

static int denali_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct denali_controller *denali = to_denali_controller(chip);

	if (section > 0)
		return -ERANGE;

	oobregion->offset = denali->oob_skip_bytes;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int denali_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct denali_controller *denali = to_denali_controller(chip);

	if (section > 0)
		return -ERANGE;

	oobregion->offset = chip->ecc.total + denali->oob_skip_bytes;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops denali_ooblayout_ops = {
	.ecc = denali_ooblayout_ecc,
	.free = denali_ooblayout_free,
};

static int denali_multidev_fixup(struct nand_chip *chip)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_memory_organization *memorg;

	memorg = nanddev_get_memorg(&chip->base);

	/*
	 * Support for multi device:
	 * When the IP configuration is x16 capable and two x8 chips are
	 * connected in parallel, DEVICES_CONNECTED should be set to 2.
	 * In this case, the core framework knows nothing about this fact,
	 * so we should tell it the _logical_ pagesize and anything necessary.
	 */
	denali->devs_per_cs = ioread32(denali->reg + DEVICES_CONNECTED);

	/*
	 * On some SoCs, DEVICES_CONNECTED is not auto-detected.
	 * For those, DEVICES_CONNECTED is left to 0.  Set 1 if it is the case.
	 */
	if (denali->devs_per_cs == 0) {
		denali->devs_per_cs = 1;
		iowrite32(1, denali->reg + DEVICES_CONNECTED);
	}

	if (denali->devs_per_cs == 1)
		return 0;

	if (denali->devs_per_cs != 2) {
		dev_err(denali->dev, "unsupported number of devices %d\n",
			denali->devs_per_cs);
		return -EINVAL;
	}

	/* 2 chips in parallel */
	memorg->pagesize <<= 1;
	memorg->oobsize <<= 1;
	mtd->size <<= 1;
	mtd->erasesize <<= 1;
	mtd->writesize <<= 1;
	mtd->oobsize <<= 1;
	chip->page_shift += 1;
	chip->phys_erase_shift += 1;
	chip->bbt_erase_shift += 1;
	chip->chip_shift += 1;
	chip->pagemask <<= 1;
	chip->ecc.size <<= 1;
	chip->ecc.bytes <<= 1;
	chip->ecc.strength <<= 1;
	denali->oob_skip_bytes <<= 1;

	return 0;
}

static int denali_attach_chip(struct nand_chip *chip)
{
	struct denali_controller *denali = to_denali_controller(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	ret = nand_ecc_choose_conf(chip, denali->ecc_caps,
				   mtd->oobsize - denali->oob_skip_bytes);
	if (ret) {
		dev_err(denali->dev, "Failed to setup ECC settings.\n");
		return ret;
	}

	dev_dbg(denali->dev,
		"chosen ECC settings: step=%d, strength=%d, bytes=%d\n",
		chip->ecc.size, chip->ecc.strength, chip->ecc.bytes);

	ret = denali_multidev_fixup(chip);
	if (ret)
		return ret;

	return 0;
}

static void denali_exec_in8(struct denali_controller *denali, u32 type,
			    u8 *buf, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = denali->host_read(denali, type | DENALI_BANK(denali));
}

static void denali_exec_in16(struct denali_controller *denali, u32 type,
			     u8 *buf, unsigned int len)
{
	u32 data;
	int i;

	for (i = 0; i < len; i += 2) {
		data = denali->host_read(denali, type | DENALI_BANK(denali));
		/* bit 31:24 and 15:8 are used for DDR */
		buf[i] = data;
		buf[i + 1] = data >> 16;
	}
}

static void denali_exec_in(struct denali_controller *denali, u32 type,
			   u8 *buf, unsigned int len, bool width16)
{
	if (width16)
		denali_exec_in16(denali, type, buf, len);
	else
		denali_exec_in8(denali, type, buf, len);
}

static void denali_exec_out8(struct denali_controller *denali, u32 type,
			     const u8 *buf, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++)
		denali->host_write(denali, type | DENALI_BANK(denali), buf[i]);
}

static void denali_exec_out16(struct denali_controller *denali, u32 type,
			      const u8 *buf, unsigned int len)
{
	int i;

	for (i = 0; i < len; i += 2)
		denali->host_write(denali, type | DENALI_BANK(denali),
				   buf[i + 1] << 16 | buf[i]);
}

static void denali_exec_out(struct denali_controller *denali, u32 type,
			    const u8 *buf, unsigned int len, bool width16)
{
	if (width16)
		denali_exec_out16(denali, type, buf, len);
	else
		denali_exec_out8(denali, type, buf, len);
}

static int denali_exec_waitrdy(struct denali_controller *denali)
{
	u32 irq_stat;

	/* R/B# pin transitioned from low to high? */
	irq_stat = denali_wait_for_irq(denali, INTR__INT_ACT);

	/* Just in case nand_operation has multiple NAND_OP_WAITRDY_INSTR. */
	denali_reset_irq(denali);

	return irq_stat & INTR__INT_ACT ? 0 : -EIO;
}

static int denali_exec_instr(struct nand_chip *chip,
			     const struct nand_op_instr *instr)
{
	struct denali_controller *denali = to_denali_controller(chip);

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		denali_exec_out8(denali, DENALI_MAP11_CMD,
				 &instr->ctx.cmd.opcode, 1);
		return 0;
	case NAND_OP_ADDR_INSTR:
		denali_exec_out8(denali, DENALI_MAP11_ADDR,
				 instr->ctx.addr.addrs,
				 instr->ctx.addr.naddrs);
		return 0;
	case NAND_OP_DATA_IN_INSTR:
		denali_exec_in(denali, DENALI_MAP11_DATA,
			       instr->ctx.data.buf.in,
			       instr->ctx.data.len,
			       !instr->ctx.data.force_8bit &&
			       chip->options & NAND_BUSWIDTH_16);
		return 0;
	case NAND_OP_DATA_OUT_INSTR:
		denali_exec_out(denali, DENALI_MAP11_DATA,
				instr->ctx.data.buf.out,
				instr->ctx.data.len,
				!instr->ctx.data.force_8bit &&
				chip->options & NAND_BUSWIDTH_16);
		return 0;
	case NAND_OP_WAITRDY_INSTR:
		return denali_exec_waitrdy(denali);
	default:
		WARN_ONCE(1, "unsupported NAND instruction type: %d\n",
			  instr->type);

		return -EINVAL;
	}
}

static int denali_exec_op(struct nand_chip *chip,
			  const struct nand_operation *op, bool check_only)
{
	int i, ret;

	if (check_only)
		return 0;

	denali_select_target(chip, op->cs);

	/*
	 * Some commands contain NAND_OP_WAITRDY_INSTR.
	 * irq must be cleared here to catch the R/B# interrupt there.
	 */
	denali_reset_irq(to_denali_controller(chip));

	for (i = 0; i < op->ninstrs; i++) {
		ret = denali_exec_instr(chip, &op->instrs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct nand_controller_ops denali_controller_ops = {
	.attach_chip = denali_attach_chip,
	.exec_op = denali_exec_op,
	.setup_data_interface = denali_setup_data_interface,
};

int denali_chip_init(struct denali_controller *denali,
		     struct denali_chip *dchip)
{
	struct nand_chip *chip = &dchip->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct denali_chip *dchip2;
	int i, j, ret;

	chip->controller = &denali->controller;

	/* sanity checks for bank numbers */
	for (i = 0; i < dchip->nsels; i++) {
		unsigned int bank = dchip->sels[i].bank;

		if (bank >= denali->nbanks) {
			dev_err(denali->dev, "unsupported bank %d\n", bank);
			return -EINVAL;
		}

		for (j = 0; j < i; j++) {
			if (bank == dchip->sels[j].bank) {
				dev_err(denali->dev,
					"bank %d is assigned twice in the same chip\n",
					bank);
				return -EINVAL;
			}
		}

		list_for_each_entry(dchip2, &denali->chips, node) {
			for (j = 0; j < dchip2->nsels; j++) {
				if (bank == dchip2->sels[j].bank) {
					dev_err(denali->dev,
						"bank %d is already used\n",
						bank);
					return -EINVAL;
				}
			}
		}
	}

	mtd->dev.parent = denali->dev;

	/*
	 * Fallback to the default name if DT did not give "label" property.
	 * Use "label" property if multiple chips are connected.
	 */
	if (!mtd->name && list_empty(&denali->chips))
		mtd->name = "denali-nand";

	if (denali->dma_avail) {
		chip->options |= NAND_USES_DMA;
		chip->buf_align = 16;
	}

	/* clk rate info is needed for setup_data_interface */
	if (!denali->clk_rate || !denali->clk_x_rate)
		chip->options |= NAND_KEEP_TIMINGS;

	chip->bbt_options |= NAND_BBT_USE_FLASH;
	chip->bbt_options |= NAND_BBT_NO_OOB;
	chip->options |= NAND_NO_SUBPAGE_WRITE;
	chip->ecc.mode = NAND_ECC_HW_SYNDROME;
	chip->ecc.read_page = denali_read_page;
	chip->ecc.write_page = denali_write_page;
	chip->ecc.read_page_raw = denali_read_page_raw;
	chip->ecc.write_page_raw = denali_write_page_raw;
	chip->ecc.read_oob = denali_read_oob;
	chip->ecc.write_oob = denali_write_oob;

	mtd_set_ooblayout(mtd, &denali_ooblayout_ops);

	ret = nand_scan(chip, dchip->nsels);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(denali->dev, "Failed to register MTD: %d\n", ret);
		goto cleanup_nand;
	}

	list_add_tail(&dchip->node, &denali->chips);

	return 0;

cleanup_nand:
	nand_cleanup(chip);

	return ret;
}
EXPORT_SYMBOL_GPL(denali_chip_init);

int denali_init(struct denali_controller *denali)
{
	u32 features = ioread32(denali->reg + FEATURES);
	int ret;

	nand_controller_init(&denali->controller);
	denali->controller.ops = &denali_controller_ops;
	init_completion(&denali->complete);
	spin_lock_init(&denali->irq_lock);
	INIT_LIST_HEAD(&denali->chips);
	denali->active_bank = DENALI_INVALID_BANK;

	/*
	 * The REVISION register may not be reliable. Platforms are allowed to
	 * override it.
	 */
	if (!denali->revision)
		denali->revision = swab16(ioread32(denali->reg + REVISION));

	denali->nbanks = 1 << FIELD_GET(FEATURES__N_BANKS, features);

	/* the encoding changed from rev 5.0 to 5.1 */
	if (denali->revision < 0x0501)
		denali->nbanks <<= 1;

	if (features & FEATURES__DMA)
		denali->dma_avail = true;

	if (denali->dma_avail) {
		int dma_bit = denali->caps & DENALI_CAP_DMA_64BIT ? 64 : 32;

		ret = dma_set_mask(denali->dev, DMA_BIT_MASK(dma_bit));
		if (ret) {
			dev_info(denali->dev,
				 "Failed to set DMA mask. Disabling DMA.\n");
			denali->dma_avail = false;
		}
	}

	if (denali->dma_avail) {
		if (denali->caps & DENALI_CAP_DMA_64BIT)
			denali->setup_dma = denali_setup_dma64;
		else
			denali->setup_dma = denali_setup_dma32;
	}

	if (features & FEATURES__INDEX_ADDR) {
		denali->host_read = denali_indexed_read;
		denali->host_write = denali_indexed_write;
	} else {
		denali->host_read = denali_direct_read;
		denali->host_write = denali_direct_write;
	}

	/*
	 * Set how many bytes should be skipped before writing data in OOB.
	 * If a platform requests a non-zero value, set it to the register.
	 * Otherwise, read the value out, expecting it has already been set up
	 * by firmware.
	 */
	if (denali->oob_skip_bytes)
		iowrite32(denali->oob_skip_bytes,
			  denali->reg + SPARE_AREA_SKIP_BYTES);
	else
		denali->oob_skip_bytes = ioread32(denali->reg +
						  SPARE_AREA_SKIP_BYTES);

	iowrite32(0, denali->reg + TRANSFER_SPARE_REG);
	iowrite32(GENMASK(denali->nbanks - 1, 0), denali->reg + RB_PIN_ENABLED);
	iowrite32(CHIP_EN_DONT_CARE__FLAG, denali->reg + CHIP_ENABLE_DONT_CARE);
	iowrite32(ECC_ENABLE__FLAG, denali->reg + ECC_ENABLE);
	iowrite32(0xffff, denali->reg + SPARE_AREA_MARKER);
	iowrite32(WRITE_PROTECT__FLAG, denali->reg + WRITE_PROTECT);

	denali_clear_irq_all(denali);

	ret = devm_request_irq(denali->dev, denali->irq, denali_isr,
			       IRQF_SHARED, DENALI_NAND_NAME, denali);
	if (ret) {
		dev_err(denali->dev, "Unable to request IRQ\n");
		return ret;
	}

	denali_enable_irq(denali);

	return 0;
}
EXPORT_SYMBOL(denali_init);

void denali_remove(struct denali_controller *denali)
{
	struct denali_chip *dchip, *tmp;

	list_for_each_entry_safe(dchip, tmp, &denali->chips, node) {
		nand_release(&dchip->chip);
		list_del(&dchip->node);
	}

	denali_disable_irq(denali);
}
EXPORT_SYMBOL(denali_remove);

MODULE_DESCRIPTION("Driver core for Denali NAND controller");
MODULE_AUTHOR("Intel Corporation and its suppliers");
MODULE_LICENSE("GPL v2");

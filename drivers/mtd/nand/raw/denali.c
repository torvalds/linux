/*
 * NAND Flash Controller Device Driver
 * Copyright © 2009-2010, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
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

MODULE_LICENSE("GPL");

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

/* MAP10 commands */
#define DENALI_ERASE		0x01

#define DENALI_BANK(denali)	((denali)->active_bank << 24)

#define DENALI_INVALID_BANK	-1
#define DENALI_NR_BANKS		4

static inline struct denali_nand_info *mtd_to_denali(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct denali_nand_info, nand);
}

/*
 * Direct Addressing - the slave address forms the control information (command
 * type, bank, block, and page address).  The slave data is the actual data to
 * be transferred.  This mode requires 28 bits of address region allocated.
 */
static u32 denali_direct_read(struct denali_nand_info *denali, u32 addr)
{
	return ioread32(denali->host + addr);
}

static void denali_direct_write(struct denali_nand_info *denali, u32 addr,
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
static u32 denali_indexed_read(struct denali_nand_info *denali, u32 addr)
{
	iowrite32(addr, denali->host + DENALI_INDEXED_CTRL);
	return ioread32(denali->host + DENALI_INDEXED_DATA);
}

static void denali_indexed_write(struct denali_nand_info *denali, u32 addr,
				 u32 data)
{
	iowrite32(addr, denali->host + DENALI_INDEXED_CTRL);
	iowrite32(data, denali->host + DENALI_INDEXED_DATA);
}

/*
 * Use the configuration feature register to determine the maximum number of
 * banks that the hardware supports.
 */
static void denali_detect_max_banks(struct denali_nand_info *denali)
{
	uint32_t features = ioread32(denali->reg + FEATURES);

	denali->max_banks = 1 << FIELD_GET(FEATURES__N_BANKS, features);

	/* the encoding changed from rev 5.0 to 5.1 */
	if (denali->revision < 0x0501)
		denali->max_banks <<= 1;
}

static void denali_enable_irq(struct denali_nand_info *denali)
{
	int i;

	for (i = 0; i < DENALI_NR_BANKS; i++)
		iowrite32(U32_MAX, denali->reg + INTR_EN(i));
	iowrite32(GLOBAL_INT_EN_FLAG, denali->reg + GLOBAL_INT_ENABLE);
}

static void denali_disable_irq(struct denali_nand_info *denali)
{
	int i;

	for (i = 0; i < DENALI_NR_BANKS; i++)
		iowrite32(0, denali->reg + INTR_EN(i));
	iowrite32(0, denali->reg + GLOBAL_INT_ENABLE);
}

static void denali_clear_irq(struct denali_nand_info *denali,
			     int bank, uint32_t irq_status)
{
	/* write one to clear bits */
	iowrite32(irq_status, denali->reg + INTR_STATUS(bank));
}

static void denali_clear_irq_all(struct denali_nand_info *denali)
{
	int i;

	for (i = 0; i < DENALI_NR_BANKS; i++)
		denali_clear_irq(denali, i, U32_MAX);
}

static irqreturn_t denali_isr(int irq, void *dev_id)
{
	struct denali_nand_info *denali = dev_id;
	irqreturn_t ret = IRQ_NONE;
	uint32_t irq_status;
	int i;

	spin_lock(&denali->irq_lock);

	for (i = 0; i < DENALI_NR_BANKS; i++) {
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

static void denali_reset_irq(struct denali_nand_info *denali)
{
	unsigned long flags;

	spin_lock_irqsave(&denali->irq_lock, flags);
	denali->irq_status = 0;
	denali->irq_mask = 0;
	spin_unlock_irqrestore(&denali->irq_lock, flags);
}

static uint32_t denali_wait_for_irq(struct denali_nand_info *denali,
				    uint32_t irq_mask)
{
	unsigned long time_left, flags;
	uint32_t irq_status;

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

static uint32_t denali_check_irq(struct denali_nand_info *denali)
{
	unsigned long flags;
	uint32_t irq_status;

	spin_lock_irqsave(&denali->irq_lock, flags);
	irq_status = denali->irq_status;
	spin_unlock_irqrestore(&denali->irq_lock, flags);

	return irq_status;
}

static void denali_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	u32 addr = DENALI_MAP11_DATA | DENALI_BANK(denali);
	int i;

	for (i = 0; i < len; i++)
		buf[i] = denali->host_read(denali, addr);
}

static void denali_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	u32 addr = DENALI_MAP11_DATA | DENALI_BANK(denali);
	int i;

	for (i = 0; i < len; i++)
		denali->host_write(denali, addr, buf[i]);
}

static void denali_read_buf16(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	u32 addr = DENALI_MAP11_DATA | DENALI_BANK(denali);
	uint16_t *buf16 = (uint16_t *)buf;
	int i;

	for (i = 0; i < len / 2; i++)
		buf16[i] = denali->host_read(denali, addr);
}

static void denali_write_buf16(struct mtd_info *mtd, const uint8_t *buf,
			       int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	u32 addr = DENALI_MAP11_DATA | DENALI_BANK(denali);
	const uint16_t *buf16 = (const uint16_t *)buf;
	int i;

	for (i = 0; i < len / 2; i++)
		denali->host_write(denali, addr, buf16[i]);
}

static uint8_t denali_read_byte(struct mtd_info *mtd)
{
	uint8_t byte;

	denali_read_buf(mtd, &byte, 1);

	return byte;
}

static void denali_write_byte(struct mtd_info *mtd, uint8_t byte)
{
	denali_write_buf(mtd, &byte, 1);
}

static uint16_t denali_read_word(struct mtd_info *mtd)
{
	uint16_t word;

	denali_read_buf16(mtd, (uint8_t *)&word, 2);

	return word;
}

static void denali_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t type;

	if (ctrl & NAND_CLE)
		type = DENALI_MAP11_CMD;
	else if (ctrl & NAND_ALE)
		type = DENALI_MAP11_ADDR;
	else
		return;

	/*
	 * Some commands are followed by chip->dev_ready or chip->waitfunc.
	 * irq_status must be cleared here to catch the R/B# interrupt later.
	 */
	if (ctrl & NAND_CTRL_CHANGE)
		denali_reset_irq(denali);

	denali->host_write(denali, DENALI_BANK(denali) | type, dat);
}

static int denali_dev_ready(struct mtd_info *mtd)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	return !!(denali_check_irq(denali) & INTR__INT_ACT);
}

static int denali_check_erased_page(struct mtd_info *mtd,
				    struct nand_chip *chip, uint8_t *buf,
				    unsigned long uncor_ecc_flags,
				    unsigned int max_bitflips)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint8_t *ecc_code = chip->oob_poi + denali->oob_skip_bytes;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	int i, stat;

	for (i = 0; i < ecc_steps; i++) {
		if (!(uncor_ecc_flags & BIT(i)))
			continue;

		stat = nand_check_erased_ecc_chunk(buf, ecc_size,
						  ecc_code, ecc_bytes,
						  NULL, 0,
						  chip->ecc.strength);
		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}

		buf += ecc_size;
		ecc_code += ecc_bytes;
	}

	return max_bitflips;
}

static int denali_hw_ecc_fixup(struct mtd_info *mtd,
			       struct denali_nand_info *denali,
			       unsigned long *uncor_ecc_flags)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	int bank = denali->active_bank;
	uint32_t ecc_cor;
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
	mtd->ecc_stats.corrected += max_bitflips;

	return max_bitflips;
}

static int denali_sw_ecc_fixup(struct mtd_info *mtd,
			       struct denali_nand_info *denali,
			       unsigned long *uncor_ecc_flags, uint8_t *buf)
{
	unsigned int ecc_size = denali->nand.ecc.size;
	unsigned int bitflips = 0;
	unsigned int max_bitflips = 0;
	uint32_t err_addr, err_cor_info;
	unsigned int err_byte, err_sector, err_device;
	uint8_t err_cor_value;
	unsigned int prev_sector = 0;
	uint32_t irq_status;

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
			mtd->ecc_stats.corrected += flips_in_byte;
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

static void denali_setup_dma64(struct denali_nand_info *denali,
			       dma_addr_t dma_addr, int page, int write)
{
	uint32_t mode;
	const int page_count = 1;

	mode = DENALI_MAP10 | DENALI_BANK(denali) | page;

	/* DMA is a three step process */

	/*
	 * 1. setup transfer type, interrupt when complete,
	 *    burst len = 64 bytes, the number of pages
	 */
	denali->host_write(denali, mode,
			   0x01002000 | (64 << 16) | (write << 8) | page_count);

	/* 2. set memory low address */
	denali->host_write(denali, mode, lower_32_bits(dma_addr));

	/* 3. set memory high address */
	denali->host_write(denali, mode, upper_32_bits(dma_addr));
}

static void denali_setup_dma32(struct denali_nand_info *denali,
			       dma_addr_t dma_addr, int page, int write)
{
	uint32_t mode;
	const int page_count = 1;

	mode = DENALI_MAP10 | DENALI_BANK(denali);

	/* DMA is a four step process */

	/* 1. setup transfer type and # of pages */
	denali->host_write(denali, mode | page,
			   0x2000 | (write << 8) | page_count);

	/* 2. set memory high address bits 23:8 */
	denali->host_write(denali, mode | ((dma_addr >> 16) << 8), 0x2200);

	/* 3. set memory low address bits 23:8 */
	denali->host_write(denali, mode | ((dma_addr & 0xffff) << 8), 0x2300);

	/* 4. interrupt when complete, burst len = 64 bytes */
	denali->host_write(denali, mode | 0x14000, 0x2400);
}

static int denali_pio_read(struct denali_nand_info *denali, void *buf,
			   size_t size, int page, int raw)
{
	u32 addr = DENALI_MAP01 | DENALI_BANK(denali) | page;
	uint32_t *buf32 = (uint32_t *)buf;
	uint32_t irq_status, ecc_err_mask;
	int i;

	if (denali->caps & DENALI_CAP_HW_ECC_FIXUP)
		ecc_err_mask = INTR__ECC_UNCOR_ERR;
	else
		ecc_err_mask = INTR__ECC_ERR;

	denali_reset_irq(denali);

	for (i = 0; i < size / 4; i++)
		*buf32++ = denali->host_read(denali, addr);

	irq_status = denali_wait_for_irq(denali, INTR__PAGE_XFER_INC);
	if (!(irq_status & INTR__PAGE_XFER_INC))
		return -EIO;

	if (irq_status & INTR__ERASED_PAGE)
		memset(buf, 0xff, size);

	return irq_status & ecc_err_mask ? -EBADMSG : 0;
}

static int denali_pio_write(struct denali_nand_info *denali,
			    const void *buf, size_t size, int page, int raw)
{
	u32 addr = DENALI_MAP01 | DENALI_BANK(denali) | page;
	const uint32_t *buf32 = (uint32_t *)buf;
	uint32_t irq_status;
	int i;

	denali_reset_irq(denali);

	for (i = 0; i < size / 4; i++)
		denali->host_write(denali, addr, *buf32++);

	irq_status = denali_wait_for_irq(denali,
				INTR__PROGRAM_COMP | INTR__PROGRAM_FAIL);
	if (!(irq_status & INTR__PROGRAM_COMP))
		return -EIO;

	return 0;
}

static int denali_pio_xfer(struct denali_nand_info *denali, void *buf,
			   size_t size, int page, int raw, int write)
{
	if (write)
		return denali_pio_write(denali, buf, size, page, raw);
	else
		return denali_pio_read(denali, buf, size, page, raw);
}

static int denali_dma_xfer(struct denali_nand_info *denali, void *buf,
			   size_t size, int page, int raw, int write)
{
	dma_addr_t dma_addr;
	uint32_t irq_mask, irq_status, ecc_err_mask;
	enum dma_data_direction dir = write ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	int ret = 0;

	dma_addr = dma_map_single(denali->dev, buf, size, dir);
	if (dma_mapping_error(denali->dev, dma_addr)) {
		dev_dbg(denali->dev, "Failed to DMA-map buffer. Trying PIO.\n");
		return denali_pio_xfer(denali, buf, size, page, raw, write);
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

static int denali_data_xfer(struct denali_nand_info *denali, void *buf,
			    size_t size, int page, int raw, int write)
{
	iowrite32(raw ? 0 : ECC_ENABLE__FLAG, denali->reg + ECC_ENABLE);
	iowrite32(raw ? TRANSFER_SPARE_REG__FLAG : 0,
		  denali->reg + TRANSFER_SPARE_REG);

	if (denali->dma_avail)
		return denali_dma_xfer(denali, buf, size, page, raw, write);
	else
		return denali_pio_xfer(denali, buf, size, page, raw, write);
}

static void denali_oob_xfer(struct mtd_info *mtd, struct nand_chip *chip,
			    int page, int write)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int writesize = mtd->writesize;
	int oobsize = mtd->oobsize;
	uint8_t *bufpoi = chip->oob_poi;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	int oob_skip = denali->oob_skip_bytes;
	size_t size = writesize + oobsize;
	int i, pos, len;

	/* BBM at the beginning of the OOB area */
	if (write)
		nand_prog_page_begin_op(chip, page, writesize, bufpoi,
					oob_skip);
	else
		nand_read_page_op(chip, page, writesize, bufpoi, oob_skip);
	bufpoi += oob_skip;

	/* OOB ECC */
	for (i = 0; i < ecc_steps; i++) {
		pos = ecc_size + i * (ecc_size + ecc_bytes);
		len = ecc_bytes;

		if (pos >= writesize)
			pos += oob_skip;
		else if (pos + len > writesize)
			len = writesize - pos;

		if (write)
			nand_change_write_column_op(chip, pos, bufpoi, len,
						    false);
		else
			nand_change_read_column_op(chip, pos, bufpoi, len,
						   false);
		bufpoi += len;
		if (len < ecc_bytes) {
			len = ecc_bytes - len;
			if (write)
				nand_change_write_column_op(chip, writesize +
							    oob_skip, bufpoi,
							    len, false);
			else
				nand_change_read_column_op(chip, writesize +
							   oob_skip, bufpoi,
							   len, false);
			bufpoi += len;
		}
	}

	/* OOB free */
	len = oobsize - (bufpoi - chip->oob_poi);
	if (write)
		nand_change_write_column_op(chip, size - len, bufpoi, len,
					    false);
	else
		nand_change_read_column_op(chip, size - len, bufpoi, len,
					   false);
}

static int denali_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int writesize = mtd->writesize;
	int oobsize = mtd->oobsize;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	void *tmp_buf = denali->buf;
	int oob_skip = denali->oob_skip_bytes;
	size_t size = writesize + oobsize;
	int ret, i, pos, len;

	ret = denali_data_xfer(denali, tmp_buf, size, page, 1, 0);
	if (ret)
		return ret;

	/* Arrange the buffer for syndrome payload/ecc layout */
	if (buf) {
		for (i = 0; i < ecc_steps; i++) {
			pos = i * (ecc_size + ecc_bytes);
			len = ecc_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(buf, tmp_buf + pos, len);
			buf += len;
			if (len < ecc_size) {
				len = ecc_size - len;
				memcpy(buf, tmp_buf + writesize + oob_skip,
				       len);
				buf += len;
			}
		}
	}

	if (oob_required) {
		uint8_t *oob = chip->oob_poi;

		/* BBM at the beginning of the OOB area */
		memcpy(oob, tmp_buf + writesize, oob_skip);
		oob += oob_skip;

		/* OOB ECC */
		for (i = 0; i < ecc_steps; i++) {
			pos = ecc_size + i * (ecc_size + ecc_bytes);
			len = ecc_bytes;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(oob, tmp_buf + pos, len);
			oob += len;
			if (len < ecc_bytes) {
				len = ecc_bytes - len;
				memcpy(oob, tmp_buf + writesize + oob_skip,
				       len);
				oob += len;
			}
		}

		/* OOB free */
		len = oobsize - (oob - chip->oob_poi);
		memcpy(oob, tmp_buf + size - len, len);
	}

	return 0;
}

static int denali_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
			   int page)
{
	denali_oob_xfer(mtd, chip, page, 0);

	return 0;
}

static int denali_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
			    int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	denali_reset_irq(denali);

	denali_oob_xfer(mtd, chip, page, 1);

	return nand_prog_page_end_op(chip);
}

static int denali_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			    uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	unsigned long uncor_ecc_flags = 0;
	int stat = 0;
	int ret;

	ret = denali_data_xfer(denali, buf, mtd->writesize, page, 0, 0);
	if (ret && ret != -EBADMSG)
		return ret;

	if (denali->caps & DENALI_CAP_HW_ECC_FIXUP)
		stat = denali_hw_ecc_fixup(mtd, denali, &uncor_ecc_flags);
	else if (ret == -EBADMSG)
		stat = denali_sw_ecc_fixup(mtd, denali, &uncor_ecc_flags, buf);

	if (stat < 0)
		return stat;

	if (uncor_ecc_flags) {
		ret = denali_read_oob(mtd, chip, page);
		if (ret)
			return ret;

		stat = denali_check_erased_page(mtd, chip, buf,
						uncor_ecc_flags, stat);
	}

	return stat;
}

static int denali_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				 const uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int writesize = mtd->writesize;
	int oobsize = mtd->oobsize;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	void *tmp_buf = denali->buf;
	int oob_skip = denali->oob_skip_bytes;
	size_t size = writesize + oobsize;
	int i, pos, len;

	/*
	 * Fill the buffer with 0xff first except the full page transfer.
	 * This simplifies the logic.
	 */
	if (!buf || !oob_required)
		memset(tmp_buf, 0xff, size);

	/* Arrange the buffer for syndrome payload/ecc layout */
	if (buf) {
		for (i = 0; i < ecc_steps; i++) {
			pos = i * (ecc_size + ecc_bytes);
			len = ecc_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(tmp_buf + pos, buf, len);
			buf += len;
			if (len < ecc_size) {
				len = ecc_size - len;
				memcpy(tmp_buf + writesize + oob_skip, buf,
				       len);
				buf += len;
			}
		}
	}

	if (oob_required) {
		const uint8_t *oob = chip->oob_poi;

		/* BBM at the beginning of the OOB area */
		memcpy(tmp_buf + writesize, oob, oob_skip);
		oob += oob_skip;

		/* OOB ECC */
		for (i = 0; i < ecc_steps; i++) {
			pos = ecc_size + i * (ecc_size + ecc_bytes);
			len = ecc_bytes;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(tmp_buf + pos, oob, len);
			oob += len;
			if (len < ecc_bytes) {
				len = ecc_bytes - len;
				memcpy(tmp_buf + writesize + oob_skip, oob,
				       len);
				oob += len;
			}
		}

		/* OOB free */
		len = oobsize - (oob - chip->oob_poi);
		memcpy(tmp_buf + size - len, oob, len);
	}

	return denali_data_xfer(denali, tmp_buf, size, page, 1, 1);
}

static int denali_write_page(struct mtd_info *mtd, struct nand_chip *chip,
			     const uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	return denali_data_xfer(denali, (void *)buf, mtd->writesize,
				page, 0, 1);
}

static void denali_select_chip(struct mtd_info *mtd, int chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	denali->active_bank = chip;
}

static int denali_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_status;

	/* R/B# pin transitioned from low to high? */
	irq_status = denali_wait_for_irq(denali, INTR__INT_ACT);

	return irq_status & INTR__INT_ACT ? 0 : NAND_STATUS_FAIL;
}

static int denali_erase(struct mtd_info *mtd, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_status;

	denali_reset_irq(denali);

	denali->host_write(denali, DENALI_MAP10 | DENALI_BANK(denali) | page,
			   DENALI_ERASE);

	/* wait for erase to complete or failure to occur */
	irq_status = denali_wait_for_irq(denali,
					 INTR__ERASE_COMP | INTR__ERASE_FAIL);

	return irq_status & INTR__ERASE_COMP ? 0 : -EIO;
}

static int denali_setup_data_interface(struct mtd_info *mtd, int chipnr,
				       const struct nand_data_interface *conf)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	const struct nand_sdr_timings *timings;
	unsigned long t_x, mult_x;
	int acc_clks, re_2_we, re_2_re, we_2_re, addr_2_data;
	int rdwr_en_lo, rdwr_en_hi, rdwr_en_lo_hi, cs_setup;
	int addr_2_data_mask;
	uint32_t tmp;

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

	/* tREA -> ACC_CLKS */
	acc_clks = DIV_ROUND_UP(timings->tREA_max, t_x);
	acc_clks = min_t(int, acc_clks, ACC_CLKS__VALUE);

	tmp = ioread32(denali->reg + ACC_CLKS);
	tmp &= ~ACC_CLKS__VALUE;
	tmp |= FIELD_PREP(ACC_CLKS__VALUE, acc_clks);
	iowrite32(tmp, denali->reg + ACC_CLKS);

	/* tRWH -> RE_2_WE */
	re_2_we = DIV_ROUND_UP(timings->tRHW_min, t_x);
	re_2_we = min_t(int, re_2_we, RE_2_WE__VALUE);

	tmp = ioread32(denali->reg + RE_2_WE);
	tmp &= ~RE_2_WE__VALUE;
	tmp |= FIELD_PREP(RE_2_WE__VALUE, re_2_we);
	iowrite32(tmp, denali->reg + RE_2_WE);

	/* tRHZ -> RE_2_RE */
	re_2_re = DIV_ROUND_UP(timings->tRHZ_max, t_x);
	re_2_re = min_t(int, re_2_re, RE_2_RE__VALUE);

	tmp = ioread32(denali->reg + RE_2_RE);
	tmp &= ~RE_2_RE__VALUE;
	tmp |= FIELD_PREP(RE_2_RE__VALUE, re_2_re);
	iowrite32(tmp, denali->reg + RE_2_RE);

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
	iowrite32(tmp, denali->reg + TWHR2_AND_WE_2_RE);

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
	iowrite32(tmp, denali->reg + TCWAW_AND_ADDR_2_DATA);

	/* tREH, tWH -> RDWR_EN_HI_CNT */
	rdwr_en_hi = DIV_ROUND_UP(max(timings->tREH_min, timings->tWH_min),
				  t_x);
	rdwr_en_hi = min_t(int, rdwr_en_hi, RDWR_EN_HI_CNT__VALUE);

	tmp = ioread32(denali->reg + RDWR_EN_HI_CNT);
	tmp &= ~RDWR_EN_HI_CNT__VALUE;
	tmp |= FIELD_PREP(RDWR_EN_HI_CNT__VALUE, rdwr_en_hi);
	iowrite32(tmp, denali->reg + RDWR_EN_HI_CNT);

	/* tRP, tWP -> RDWR_EN_LO_CNT */
	rdwr_en_lo = DIV_ROUND_UP(max(timings->tRP_min, timings->tWP_min), t_x);
	rdwr_en_lo_hi = DIV_ROUND_UP(max(timings->tRC_min, timings->tWC_min),
				     t_x);
	rdwr_en_lo_hi = max_t(int, rdwr_en_lo_hi, mult_x);
	rdwr_en_lo = max(rdwr_en_lo, rdwr_en_lo_hi - rdwr_en_hi);
	rdwr_en_lo = min_t(int, rdwr_en_lo, RDWR_EN_LO_CNT__VALUE);

	tmp = ioread32(denali->reg + RDWR_EN_LO_CNT);
	tmp &= ~RDWR_EN_LO_CNT__VALUE;
	tmp |= FIELD_PREP(RDWR_EN_LO_CNT__VALUE, rdwr_en_lo);
	iowrite32(tmp, denali->reg + RDWR_EN_LO_CNT);

	/* tCS, tCEA -> CS_SETUP_CNT */
	cs_setup = max3((int)DIV_ROUND_UP(timings->tCS_min, t_x) - rdwr_en_lo,
			(int)DIV_ROUND_UP(timings->tCEA_max, t_x) - acc_clks,
			0);
	cs_setup = min_t(int, cs_setup, CS_SETUP_CNT__VALUE);

	tmp = ioread32(denali->reg + CS_SETUP_CNT);
	tmp &= ~CS_SETUP_CNT__VALUE;
	tmp |= FIELD_PREP(CS_SETUP_CNT__VALUE, cs_setup);
	iowrite32(tmp, denali->reg + CS_SETUP_CNT);

	return 0;
}

static void denali_reset_banks(struct denali_nand_info *denali)
{
	u32 irq_status;
	int i;

	for (i = 0; i < denali->max_banks; i++) {
		denali->active_bank = i;

		denali_reset_irq(denali);

		iowrite32(DEVICE_RESET__BANK(i),
			  denali->reg + DEVICE_RESET);

		irq_status = denali_wait_for_irq(denali,
			INTR__RST_COMP | INTR__INT_ACT | INTR__TIME_OUT);
		if (!(irq_status & INTR__INT_ACT))
			break;
	}

	dev_dbg(denali->dev, "%d chips connected\n", i);
	denali->max_banks = i;
}

static void denali_hw_init(struct denali_nand_info *denali)
{
	/*
	 * The REVISION register may not be reliable.  Platforms are allowed to
	 * override it.
	 */
	if (!denali->revision)
		denali->revision = swab16(ioread32(denali->reg + REVISION));

	/*
	 * tell driver how many bit controller will skip before
	 * writing ECC code in OOB, this register may be already
	 * set by firmware. So we read this value out.
	 * if this value is 0, just let it be.
	 */
	denali->oob_skip_bytes = ioread32(denali->reg + SPARE_AREA_SKIP_BYTES);
	denali_detect_max_banks(denali);
	iowrite32(0x0F, denali->reg + RB_PIN_ENABLED);
	iowrite32(CHIP_EN_DONT_CARE__FLAG, denali->reg + CHIP_ENABLE_DONT_CARE);

	iowrite32(0xffff, denali->reg + SPARE_AREA_MARKER);
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
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = denali->oob_skip_bytes;
	oobregion->length = chip->ecc.total;

	return 0;
}

static int denali_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = chip->ecc.total + denali->oob_skip_bytes;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops denali_ooblayout_ops = {
	.ecc = denali_ooblayout_ecc,
	.free = denali_ooblayout_free,
};

static int denali_multidev_fixup(struct denali_nand_info *denali)
{
	struct nand_chip *chip = &denali->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);

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
	mtd->size <<= 1;
	mtd->erasesize <<= 1;
	mtd->writesize <<= 1;
	mtd->oobsize <<= 1;
	chip->chipsize <<= 1;
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
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int ret;

	if (ioread32(denali->reg + FEATURES) & FEATURES__DMA)
		denali->dma_avail = 1;

	if (denali->dma_avail) {
		int dma_bit = denali->caps & DENALI_CAP_DMA_64BIT ? 64 : 32;

		ret = dma_set_mask(denali->dev, DMA_BIT_MASK(dma_bit));
		if (ret) {
			dev_info(denali->dev,
				 "Failed to set DMA mask. Disabling DMA.\n");
			denali->dma_avail = 0;
		}
	}

	if (denali->dma_avail) {
		chip->options |= NAND_USE_BOUNCE_BUFFER;
		chip->buf_align = 16;
		if (denali->caps & DENALI_CAP_DMA_64BIT)
			denali->setup_dma = denali_setup_dma64;
		else
			denali->setup_dma = denali_setup_dma32;
	}

	chip->bbt_options |= NAND_BBT_USE_FLASH;
	chip->bbt_options |= NAND_BBT_NO_OOB;
	chip->ecc.mode = NAND_ECC_HW_SYNDROME;
	chip->options |= NAND_NO_SUBPAGE_WRITE;

	ret = nand_ecc_choose_conf(chip, denali->ecc_caps,
				   mtd->oobsize - denali->oob_skip_bytes);
	if (ret) {
		dev_err(denali->dev, "Failed to setup ECC settings.\n");
		return ret;
	}

	dev_dbg(denali->dev,
		"chosen ECC settings: step=%d, strength=%d, bytes=%d\n",
		chip->ecc.size, chip->ecc.strength, chip->ecc.bytes);

	iowrite32(FIELD_PREP(ECC_CORRECTION__ERASE_THRESHOLD, 1) |
		  FIELD_PREP(ECC_CORRECTION__VALUE, chip->ecc.strength),
		  denali->reg + ECC_CORRECTION);
	iowrite32(mtd->erasesize / mtd->writesize,
		  denali->reg + PAGES_PER_BLOCK);
	iowrite32(chip->options & NAND_BUSWIDTH_16 ? 1 : 0,
		  denali->reg + DEVICE_WIDTH);
	iowrite32(chip->options & NAND_ROW_ADDR_3 ? 0 : TWO_ROW_ADDR_CYCLES__FLAG,
		  denali->reg + TWO_ROW_ADDR_CYCLES);
	iowrite32(mtd->writesize, denali->reg + DEVICE_MAIN_AREA_SIZE);
	iowrite32(mtd->oobsize, denali->reg + DEVICE_SPARE_AREA_SIZE);

	iowrite32(chip->ecc.size, denali->reg + CFG_DATA_BLOCK_SIZE);
	iowrite32(chip->ecc.size, denali->reg + CFG_LAST_DATA_BLOCK_SIZE);
	/* chip->ecc.steps is set by nand_scan_tail(); not available here */
	iowrite32(mtd->writesize / chip->ecc.size,
		  denali->reg + CFG_NUM_DATA_BLOCKS);

	mtd_set_ooblayout(mtd, &denali_ooblayout_ops);

	if (chip->options & NAND_BUSWIDTH_16) {
		chip->read_buf = denali_read_buf16;
		chip->write_buf = denali_write_buf16;
	} else {
		chip->read_buf = denali_read_buf;
		chip->write_buf = denali_write_buf;
	}
	chip->ecc.read_page = denali_read_page;
	chip->ecc.read_page_raw = denali_read_page_raw;
	chip->ecc.write_page = denali_write_page;
	chip->ecc.write_page_raw = denali_write_page_raw;
	chip->ecc.read_oob = denali_read_oob;
	chip->ecc.write_oob = denali_write_oob;
	chip->erase = denali_erase;

	ret = denali_multidev_fixup(denali);
	if (ret)
		return ret;

	/*
	 * This buffer is DMA-mapped by denali_{read,write}_page_raw.  Do not
	 * use devm_kmalloc() because the memory allocated by devm_ does not
	 * guarantee DMA-safe alignment.
	 */
	denali->buf = kmalloc(mtd->writesize + mtd->oobsize, GFP_KERNEL);
	if (!denali->buf)
		return -ENOMEM;

	return 0;
}

static void denali_detach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	kfree(denali->buf);
}

static const struct nand_controller_ops denali_controller_ops = {
	.attach_chip = denali_attach_chip,
	.detach_chip = denali_detach_chip,
};

int denali_init(struct denali_nand_info *denali)
{
	struct nand_chip *chip = &denali->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 features = ioread32(denali->reg + FEATURES);
	int ret;

	mtd->dev.parent = denali->dev;
	denali_hw_init(denali);

	init_completion(&denali->complete);
	spin_lock_init(&denali->irq_lock);

	denali_clear_irq_all(denali);

	ret = devm_request_irq(denali->dev, denali->irq, denali_isr,
			       IRQF_SHARED, DENALI_NAND_NAME, denali);
	if (ret) {
		dev_err(denali->dev, "Unable to request IRQ\n");
		return ret;
	}

	denali_enable_irq(denali);
	denali_reset_banks(denali);
	if (!denali->max_banks) {
		/* Error out earlier if no chip is found for some reasons. */
		ret = -ENODEV;
		goto disable_irq;
	}

	denali->active_bank = DENALI_INVALID_BANK;

	nand_set_flash_node(chip, denali->dev->of_node);
	/* Fallback to the default name if DT did not give "label" property */
	if (!mtd->name)
		mtd->name = "denali-nand";

	chip->select_chip = denali_select_chip;
	chip->read_byte = denali_read_byte;
	chip->write_byte = denali_write_byte;
	chip->read_word = denali_read_word;
	chip->cmd_ctrl = denali_cmd_ctrl;
	chip->dev_ready = denali_dev_ready;
	chip->waitfunc = denali_waitfunc;

	if (features & FEATURES__INDEX_ADDR) {
		denali->host_read = denali_indexed_read;
		denali->host_write = denali_indexed_write;
	} else {
		denali->host_read = denali_direct_read;
		denali->host_write = denali_direct_write;
	}

	/* clk rate info is needed for setup_data_interface */
	if (denali->clk_rate && denali->clk_x_rate)
		chip->setup_data_interface = denali_setup_data_interface;

	chip->dummy_controller.ops = &denali_controller_ops;
	ret = nand_scan(mtd, denali->max_banks);
	if (ret)
		goto disable_irq;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(denali->dev, "Failed to register MTD: %d\n", ret);
		goto cleanup_nand;
	}

	return 0;

cleanup_nand:
	nand_cleanup(chip);
disable_irq:
	denali_disable_irq(denali);

	return ret;
}
EXPORT_SYMBOL(denali_init);

void denali_remove(struct denali_nand_info *denali)
{
	struct mtd_info *mtd = nand_to_mtd(&denali->nand);

	nand_release(mtd);
	denali_disable_irq(denali);
}
EXPORT_SYMBOL(denali_remove);

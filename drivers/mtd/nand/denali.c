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
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/module.h>

#include "denali.h"

MODULE_LICENSE("GPL");

#define DENALI_NAND_NAME    "denali-nand"

/*
 * indicates whether or not the internal value for the flash bank is
 * valid or not
 */
#define CHIP_SELECT_INVALID	-1

#define DENALI_NR_BANKS		4

/*
 * The bus interface clock, clk_x, is phase aligned with the core clock.  The
 * clk_x is an integral multiple N of the core clk.  The value N is configured
 * at IP delivery time, and its available value is 4, 5, or 6.  We need to align
 * to the largest value to make it work with any possible configuration.
 */
#define DENALI_CLK_X_MULT	6

/*
 * this macro allows us to convert from an MTD structure to our own
 * device context (denali) structure.
 */
static inline struct denali_nand_info *mtd_to_denali(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct denali_nand_info, nand);
}

/*
 * These constants are defined by the driver to enable common driver
 * configuration options.
 */
#define SPARE_ACCESS		0x41
#define MAIN_ACCESS		0x42
#define MAIN_SPARE_ACCESS	0x43

#define DENALI_READ	0
#define DENALI_WRITE	0x100

/*
 * this is a helper macro that allows us to
 * format the bank into the proper bits for the controller
 */
#define BANK(x) ((x) << 24)

/*
 * Certain operations for the denali NAND controller use an indexed mode to
 * read/write data. The operation is performed by writing the address value
 * of the command to the device memory followed by the data. This function
 * abstracts this common operation.
 */
static void index_addr(struct denali_nand_info *denali,
				uint32_t address, uint32_t data)
{
	iowrite32(address, denali->flash_mem);
	iowrite32(data, denali->flash_mem + 0x10);
}

/*
 * Use the configuration feature register to determine the maximum number of
 * banks that the hardware supports.
 */
static void detect_max_banks(struct denali_nand_info *denali)
{
	uint32_t features = ioread32(denali->flash_reg + FEATURES);

	denali->max_banks = 1 << (features & FEATURES__N_BANKS);

	/* the encoding changed from rev 5.0 to 5.1 */
	if (denali->revision < 0x0501)
		denali->max_banks <<= 1;
}

static void denali_enable_irq(struct denali_nand_info *denali)
{
	int i;

	for (i = 0; i < DENALI_NR_BANKS; i++)
		iowrite32(U32_MAX, denali->flash_reg + INTR_EN(i));
	iowrite32(GLOBAL_INT_EN_FLAG, denali->flash_reg + GLOBAL_INT_ENABLE);
}

static void denali_disable_irq(struct denali_nand_info *denali)
{
	int i;

	for (i = 0; i < DENALI_NR_BANKS; i++)
		iowrite32(0, denali->flash_reg + INTR_EN(i));
	iowrite32(0, denali->flash_reg + GLOBAL_INT_ENABLE);
}

static void denali_clear_irq(struct denali_nand_info *denali,
			     int bank, uint32_t irq_status)
{
	/* write one to clear bits */
	iowrite32(irq_status, denali->flash_reg + INTR_STATUS(bank));
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
		irq_status = ioread32(denali->flash_reg + INTR_STATUS(i));
		if (irq_status)
			ret = IRQ_HANDLED;

		denali_clear_irq(denali, i, irq_status);

		if (i != denali->flash_bank)
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
			denali->irq_mask);
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

/*
 * This helper function setups the registers for ECC and whether or not
 * the spare area will be transferred.
 */
static void setup_ecc_for_xfer(struct denali_nand_info *denali, bool ecc_en,
				bool transfer_spare)
{
	int ecc_en_flag, transfer_spare_flag;

	/* set ECC, transfer spare bits if needed */
	ecc_en_flag = ecc_en ? ECC_ENABLE__FLAG : 0;
	transfer_spare_flag = transfer_spare ? TRANSFER_SPARE_REG__FLAG : 0;

	/* Enable spare area/ECC per user's request. */
	iowrite32(ecc_en_flag, denali->flash_reg + ECC_ENABLE);
	iowrite32(transfer_spare_flag, denali->flash_reg + TRANSFER_SPARE_REG);
}

static void denali_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int i;

	iowrite32(MODE_11 | BANK(denali->flash_bank) | 2, denali->flash_mem);

	for (i = 0; i < len; i++)
		buf[i] = ioread32(denali->flash_mem + 0x10);
}

static void denali_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	int i;

	iowrite32(MODE_11 | BANK(denali->flash_bank) | 2, denali->flash_mem);

	for (i = 0; i < len; i++)
		iowrite32(buf[i], denali->flash_mem + 0x10);
}

static void denali_read_buf16(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint16_t *buf16 = (uint16_t *)buf;
	int i;

	iowrite32(MODE_11 | BANK(denali->flash_bank) | 2, denali->flash_mem);

	for (i = 0; i < len / 2; i++)
		buf16[i] = ioread32(denali->flash_mem + 0x10);
}

static void denali_write_buf16(struct mtd_info *mtd, const uint8_t *buf,
			       int len)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	const uint16_t *buf16 = (const uint16_t *)buf;
	int i;

	iowrite32(MODE_11 | BANK(denali->flash_bank) | 2, denali->flash_mem);

	for (i = 0; i < len / 2; i++)
		iowrite32(buf16[i], denali->flash_mem + 0x10);
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
		type = 0;
	else if (ctrl & NAND_ALE)
		type = 1;
	else
		return;

	/*
	 * Some commands are followed by chip->dev_ready or chip->waitfunc.
	 * irq_status must be cleared here to catch the R/B# interrupt later.
	 */
	if (ctrl & NAND_CTRL_CHANGE)
		denali_reset_irq(denali);

	index_addr(denali, MODE_11 | BANK(denali->flash_bank) | type, dat);
}

static int denali_dev_ready(struct mtd_info *mtd)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	return !!(denali_check_irq(denali) & INTR__INT_ACT);
}

/*
 * sends a pipeline command operation to the controller. See the Denali NAND
 * controller's user guide for more information (section 4.2.3.6).
 */
static int denali_send_pipeline_cmd(struct denali_nand_info *denali, int page,
				    bool ecc_en, bool transfer_spare,
				    int access_type, int op)
{
	int status = PASS;
	uint32_t addr, cmd;

	setup_ecc_for_xfer(denali, ecc_en, transfer_spare);

	denali_reset_irq(denali);

	addr = BANK(denali->flash_bank) | page;

	if (op == DENALI_WRITE && access_type != SPARE_ACCESS) {
		cmd = MODE_01 | addr;
		iowrite32(cmd, denali->flash_mem);
	} else if (op == DENALI_WRITE && access_type == SPARE_ACCESS) {
		/* read spare area */
		cmd = MODE_10 | addr;
		index_addr(denali, cmd, access_type);

		cmd = MODE_01 | addr;
		iowrite32(cmd, denali->flash_mem);
	} else if (op == DENALI_READ) {
		/* setup page read request for access type */
		cmd = MODE_10 | addr;
		index_addr(denali, cmd, access_type);

		cmd = MODE_01 | addr;
		iowrite32(cmd, denali->flash_mem);
	}
	return status;
}

/* helper function that simply writes a buffer to the flash */
static int write_data_to_flash_mem(struct denali_nand_info *denali,
				   const uint8_t *buf, int len)
{
	uint32_t *buf32;
	int i;

	/*
	 * verify that the len is a multiple of 4.
	 * see comment in read_data_from_flash_mem()
	 */
	BUG_ON((len % 4) != 0);

	/* write the data to the flash memory */
	buf32 = (uint32_t *)buf;
	for (i = 0; i < len / 4; i++)
		iowrite32(*buf32++, denali->flash_mem + 0x10);
	return i * 4; /* intent is to return the number of bytes read */
}

/* helper function that simply reads a buffer from the flash */
static int read_data_from_flash_mem(struct denali_nand_info *denali,
				    uint8_t *buf, int len)
{
	uint32_t *buf32;
	int i;

	/*
	 * we assume that len will be a multiple of 4, if not it would be nice
	 * to know about it ASAP rather than have random failures...
	 * This assumption is based on the fact that this function is designed
	 * to be used to read flash pages, which are typically multiples of 4.
	 */
	BUG_ON((len % 4) != 0);

	/* transfer the data from the flash */
	buf32 = (uint32_t *)buf;
	for (i = 0; i < len / 4; i++)
		*buf32++ = ioread32(denali->flash_mem + 0x10);
	return i * 4; /* intent is to return the number of bytes read */
}

/* writes OOB data to the device */
static int write_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_status;
	uint32_t irq_mask = INTR__PROGRAM_COMP | INTR__PROGRAM_FAIL;
	int status = 0;

	if (denali_send_pipeline_cmd(denali, page, false, false, SPARE_ACCESS,
							DENALI_WRITE) == PASS) {
		write_data_to_flash_mem(denali, buf, mtd->oobsize);

		/* wait for operation to complete */
		irq_status = denali_wait_for_irq(denali, irq_mask);

		if (!(irq_status & INTR__PROGRAM_COMP)) {
			dev_err(denali->dev, "OOB write failed\n");
			status = -EIO;
		}
	} else {
		dev_err(denali->dev, "unable to send pipeline command\n");
		status = -EIO;
	}
	return status;
}

/* reads OOB data from the device */
static void read_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	uint32_t irq_mask = INTR__LOAD_COMP;
	uint32_t irq_status, addr, cmd;

	if (denali_send_pipeline_cmd(denali, page, false, true, SPARE_ACCESS,
							DENALI_READ) == PASS) {
		read_data_from_flash_mem(denali, buf, mtd->oobsize);

		/*
		 * wait for command to be accepted
		 * can always use status0 bit as the
		 * mask is identical for each bank.
		 */
		irq_status = denali_wait_for_irq(denali, irq_mask);

		if (!(irq_status & INTR__LOAD_COMP))
			dev_err(denali->dev, "page on OOB timeout %d\n", page);

		/*
		 * We set the device back to MAIN_ACCESS here as I observed
		 * instability with the controller if you do a block erase
		 * and the last transaction was a SPARE_ACCESS. Block erase
		 * is reliable (according to the MTD test infrastructure)
		 * if you are in MAIN_ACCESS.
		 */
		addr = BANK(denali->flash_bank) | page;
		cmd = MODE_10 | addr;
		index_addr(denali, cmd, MAIN_ACCESS);
	}
}

static int denali_check_erased_page(struct mtd_info *mtd,
				    struct nand_chip *chip, uint8_t *buf,
				    unsigned long uncor_ecc_flags,
				    unsigned int max_bitflips)
{
	uint8_t *ecc_code = chip->buffers->ecccode;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	int i, ret, stat;

	ret = mtd_ooblayout_get_eccbytes(mtd, ecc_code, chip->oob_poi, 0,
					 chip->ecc.total);
	if (ret)
		return ret;

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
	int bank = denali->flash_bank;
	uint32_t ecc_cor;
	unsigned int max_bitflips;

	ecc_cor = ioread32(denali->flash_reg + ECC_COR_INFO(bank));
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

	max_bitflips = ecc_cor & ECC_COR_INFO__MAX_ERRORS;

	/*
	 * The register holds the maximum of per-sector corrected bitflips.
	 * This is suitable for the return value of the ->read_page() callback.
	 * Unfortunately, we can not know the total number of corrected bits in
	 * the page.  Increase the stats by max_bitflips. (compromised solution)
	 */
	mtd->ecc_stats.corrected += max_bitflips;

	return max_bitflips;
}

#define ECC_SECTOR(x)	(((x) & ECC_ERROR_ADDRESS__SECTOR_NR) >> 12)
#define ECC_BYTE(x)	(((x) & ECC_ERROR_ADDRESS__OFFSET))
#define ECC_CORRECTION_VALUE(x) ((x) & ERR_CORRECTION_INFO__BYTEMASK)
#define ECC_ERROR_UNCORRECTABLE(x) ((x) & ERR_CORRECTION_INFO__ERROR_TYPE)
#define ECC_ERR_DEVICE(x)	(((x) & ERR_CORRECTION_INFO__DEVICE_NR) >> 8)
#define ECC_LAST_ERR(x)		((x) & ERR_CORRECTION_INFO__LAST_ERR_INFO)

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
		err_addr = ioread32(denali->flash_reg + ECC_ERROR_ADDRESS);
		err_sector = ECC_SECTOR(err_addr);
		err_byte = ECC_BYTE(err_addr);

		err_cor_info = ioread32(denali->flash_reg + ERR_CORRECTION_INFO);
		err_cor_value = ECC_CORRECTION_VALUE(err_cor_info);
		err_device = ECC_ERR_DEVICE(err_cor_info);

		/* reset the bitflip counter when crossing ECC sector */
		if (err_sector != prev_sector)
			bitflips = 0;

		if (ECC_ERROR_UNCORRECTABLE(err_cor_info)) {
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
						denali->devnum + err_device;

			/* correct the ECC error */
			flips_in_byte = hweight8(buf[offset] ^ err_cor_value);
			buf[offset] ^= err_cor_value;
			mtd->ecc_stats.corrected += flips_in_byte;
			bitflips += flips_in_byte;

			max_bitflips = max(max_bitflips, bitflips);
		}

		prev_sector = err_sector;
	} while (!ECC_LAST_ERR(err_cor_info));

	/*
	 * Once handle all ecc errors, controller will trigger a
	 * ECC_TRANSACTION_DONE interrupt, so here just wait for
	 * a while for this interrupt
	 */
	irq_status = denali_wait_for_irq(denali, INTR__ECC_TRANSACTION_DONE);
	if (!(irq_status & INTR__ECC_TRANSACTION_DONE))
		return -EIO;

	return max_bitflips;
}

/* programs the controller to either enable/disable DMA transfers */
static void denali_enable_dma(struct denali_nand_info *denali, bool en)
{
	iowrite32(en ? DMA_ENABLE__FLAG : 0, denali->flash_reg + DMA_ENABLE);
	ioread32(denali->flash_reg + DMA_ENABLE);
}

static void denali_setup_dma64(struct denali_nand_info *denali,
			       dma_addr_t dma_addr, int page, int op)
{
	uint32_t mode;
	const int page_count = 1;

	mode = MODE_10 | BANK(denali->flash_bank) | page;

	/* DMA is a three step process */

	/*
	 * 1. setup transfer type, interrupt when complete,
	 *    burst len = 64 bytes, the number of pages
	 */
	index_addr(denali, mode, 0x01002000 | (64 << 16) | op | page_count);

	/* 2. set memory low address */
	index_addr(denali, mode, dma_addr);

	/* 3. set memory high address */
	index_addr(denali, mode, (uint64_t)dma_addr >> 32);
}

static void denali_setup_dma32(struct denali_nand_info *denali,
			       dma_addr_t dma_addr, int page, int op)
{
	uint32_t mode;
	const int page_count = 1;

	mode = MODE_10 | BANK(denali->flash_bank);

	/* DMA is a four step process */

	/* 1. setup transfer type and # of pages */
	index_addr(denali, mode | page, 0x2000 | op | page_count);

	/* 2. set memory high address bits 23:8 */
	index_addr(denali, mode | ((dma_addr >> 16) << 8), 0x2200);

	/* 3. set memory low address bits 23:8 */
	index_addr(denali, mode | ((dma_addr & 0xffff) << 8), 0x2300);

	/* 4. interrupt when complete, burst len = 64 bytes */
	index_addr(denali, mode | 0x14000, 0x2400);
}

static void denali_setup_dma(struct denali_nand_info *denali,
			     dma_addr_t dma_addr, int page, int op)
{
	if (denali->caps & DENALI_CAP_DMA_64BIT)
		denali_setup_dma64(denali, dma_addr, page, op);
	else
		denali_setup_dma32(denali, dma_addr, page, op);
}

/*
 * writes a page. user specifies type, and this function handles the
 * configuration details.
 */
static int write_page(struct mtd_info *mtd, struct nand_chip *chip,
			const uint8_t *buf, int page, bool raw_xfer)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	dma_addr_t addr = denali->dma_addr;
	size_t size = mtd->writesize + mtd->oobsize;
	uint32_t irq_status;
	uint32_t irq_mask = INTR__DMA_CMD_COMP | INTR__PROGRAM_FAIL;
	int ret = 0;

	/*
	 * if it is a raw xfer, we want to disable ecc and send the spare area.
	 * !raw_xfer - enable ecc
	 * raw_xfer - transfer spare
	 */
	setup_ecc_for_xfer(denali, !raw_xfer, raw_xfer);

	/* copy buffer into DMA buffer */
	memcpy(denali->buf, buf, mtd->writesize);

	if (raw_xfer) {
		/* transfer the data to the spare area */
		memcpy(denali->buf + mtd->writesize,
			chip->oob_poi,
			mtd->oobsize);
	}

	dma_sync_single_for_device(denali->dev, addr, size, DMA_TO_DEVICE);

	denali_reset_irq(denali);
	denali_enable_dma(denali, true);

	denali_setup_dma(denali, addr, page, DENALI_WRITE);

	/* wait for operation to complete */
	irq_status = denali_wait_for_irq(denali, irq_mask);
	if (!(irq_status & INTR__DMA_CMD_COMP)) {
		dev_err(denali->dev, "timeout on write_page (type = %d)\n",
			raw_xfer);
		ret = -EIO;
	}

	denali_enable_dma(denali, false);
	dma_sync_single_for_cpu(denali->dev, addr, size, DMA_TO_DEVICE);

	return ret;
}

/* NAND core entry points */

/*
 * this is the callback that the NAND core calls to write a page. Since
 * writing a page with ECC or without is similar, all the work is done
 * by write_page above.
 */
static int denali_write_page(struct mtd_info *mtd, struct nand_chip *chip,
				const uint8_t *buf, int oob_required, int page)
{
	/*
	 * for regular page writes, we let HW handle all the ECC
	 * data written to the device.
	 */
	return write_page(mtd, chip, buf, page, false);
}

/*
 * This is the callback that the NAND core calls to write a page without ECC.
 * raw access is similar to ECC page writes, so all the work is done in the
 * write_page() function above.
 */
static int denali_write_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				 const uint8_t *buf, int oob_required,
				 int page)
{
	/*
	 * for raw page writes, we want to disable ECC and simply write
	 * whatever data is in the buffer.
	 */
	return write_page(mtd, chip, buf, page, true);
}

static int denali_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
			    int page)
{
	return write_oob_data(mtd, chip->oob_poi, page);
}

static int denali_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
			   int page)
{
	read_oob_data(mtd, chip->oob_poi, page);

	return 0;
}

static int denali_read_page(struct mtd_info *mtd, struct nand_chip *chip,
			    uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	dma_addr_t addr = denali->dma_addr;
	size_t size = mtd->writesize + mtd->oobsize;
	uint32_t irq_status;
	uint32_t irq_mask = denali->caps & DENALI_CAP_HW_ECC_FIXUP ?
				INTR__DMA_CMD_COMP | INTR__ECC_UNCOR_ERR :
				INTR__ECC_TRANSACTION_DONE | INTR__ECC_ERR;
	unsigned long uncor_ecc_flags = 0;
	int stat = 0;

	setup_ecc_for_xfer(denali, true, false);

	denali_enable_dma(denali, true);
	dma_sync_single_for_device(denali->dev, addr, size, DMA_FROM_DEVICE);

	denali_reset_irq(denali);
	denali_setup_dma(denali, addr, page, DENALI_READ);

	/* wait for operation to complete */
	irq_status = denali_wait_for_irq(denali, irq_mask);

	dma_sync_single_for_cpu(denali->dev, addr, size, DMA_FROM_DEVICE);

	memcpy(buf, denali->buf, mtd->writesize);

	if (denali->caps & DENALI_CAP_HW_ECC_FIXUP)
		stat = denali_hw_ecc_fixup(mtd, denali, &uncor_ecc_flags);
	else if (irq_status & INTR__ECC_ERR)
		stat = denali_sw_ecc_fixup(mtd, denali, &uncor_ecc_flags, buf);
	denali_enable_dma(denali, false);

	if (stat < 0)
		return stat;

	if (uncor_ecc_flags) {
		read_oob_data(mtd, chip->oob_poi, page);

		stat = denali_check_erased_page(mtd, chip, buf,
						uncor_ecc_flags, stat);
	}

	return stat;
}

static int denali_read_page_raw(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int oob_required, int page)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	dma_addr_t addr = denali->dma_addr;
	size_t size = mtd->writesize + mtd->oobsize;
	uint32_t irq_mask = INTR__DMA_CMD_COMP;
	uint32_t irq_status;

	setup_ecc_for_xfer(denali, false, true);
	denali_enable_dma(denali, true);

	dma_sync_single_for_device(denali->dev, addr, size, DMA_FROM_DEVICE);

	denali_reset_irq(denali);
	denali_setup_dma(denali, addr, page, DENALI_READ);

	/* wait for operation to complete */
	irq_status = denali_wait_for_irq(denali, irq_mask);
	if (irq_status & INTR__DMA_CMD_COMP)
		return -ETIMEDOUT;

	dma_sync_single_for_cpu(denali->dev, addr, size, DMA_FROM_DEVICE);

	denali_enable_dma(denali, false);

	memcpy(buf, denali->buf, mtd->writesize);
	memcpy(chip->oob_poi, denali->buf + mtd->writesize, mtd->oobsize);

	return 0;
}

static void denali_select_chip(struct mtd_info *mtd, int chip)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);

	denali->flash_bank = chip;
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
	uint32_t cmd, irq_status;

	denali_reset_irq(denali);

	/* setup page read request for access type */
	cmd = MODE_10 | BANK(denali->flash_bank) | page;
	index_addr(denali, cmd, 0x1);

	/* wait for erase to complete or failure to occur */
	irq_status = denali_wait_for_irq(denali,
					 INTR__ERASE_COMP | INTR__ERASE_FAIL);

	return irq_status & INTR__ERASE_COMP ? 0 : NAND_STATUS_FAIL;
}

#define DIV_ROUND_DOWN_ULL(ll, d) \
	({ unsigned long long _tmp = (ll); do_div(_tmp, d); _tmp; })

static int denali_setup_data_interface(struct mtd_info *mtd, int chipnr,
				       const struct nand_data_interface *conf)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	const struct nand_sdr_timings *timings;
	unsigned long t_clk;
	int acc_clks, re_2_we, re_2_re, we_2_re, addr_2_data;
	int rdwr_en_lo, rdwr_en_hi, rdwr_en_lo_hi, cs_setup;
	int addr_2_data_mask;
	uint32_t tmp;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return PTR_ERR(timings);

	/* clk_x period in picoseconds */
	t_clk = DIV_ROUND_DOWN_ULL(1000000000000ULL, denali->clk_x_rate);
	if (!t_clk)
		return -EINVAL;

	if (chipnr == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	/* tREA -> ACC_CLKS */
	acc_clks = DIV_ROUND_UP(timings->tREA_max, t_clk);
	acc_clks = min_t(int, acc_clks, ACC_CLKS__VALUE);

	tmp = ioread32(denali->flash_reg + ACC_CLKS);
	tmp &= ~ACC_CLKS__VALUE;
	tmp |= acc_clks;
	iowrite32(tmp, denali->flash_reg + ACC_CLKS);

	/* tRWH -> RE_2_WE */
	re_2_we = DIV_ROUND_UP(timings->tRHW_min, t_clk);
	re_2_we = min_t(int, re_2_we, RE_2_WE__VALUE);

	tmp = ioread32(denali->flash_reg + RE_2_WE);
	tmp &= ~RE_2_WE__VALUE;
	tmp |= re_2_we;
	iowrite32(tmp, denali->flash_reg + RE_2_WE);

	/* tRHZ -> RE_2_RE */
	re_2_re = DIV_ROUND_UP(timings->tRHZ_max, t_clk);
	re_2_re = min_t(int, re_2_re, RE_2_RE__VALUE);

	tmp = ioread32(denali->flash_reg + RE_2_RE);
	tmp &= ~RE_2_RE__VALUE;
	tmp |= re_2_re;
	iowrite32(tmp, denali->flash_reg + RE_2_RE);

	/* tWHR -> WE_2_RE */
	we_2_re = DIV_ROUND_UP(timings->tWHR_min, t_clk);
	we_2_re = min_t(int, we_2_re, TWHR2_AND_WE_2_RE__WE_2_RE);

	tmp = ioread32(denali->flash_reg + TWHR2_AND_WE_2_RE);
	tmp &= ~TWHR2_AND_WE_2_RE__WE_2_RE;
	tmp |= we_2_re;
	iowrite32(tmp, denali->flash_reg + TWHR2_AND_WE_2_RE);

	/* tADL -> ADDR_2_DATA */

	/* for older versions, ADDR_2_DATA is only 6 bit wide */
	addr_2_data_mask = TCWAW_AND_ADDR_2_DATA__ADDR_2_DATA;
	if (denali->revision < 0x0501)
		addr_2_data_mask >>= 1;

	addr_2_data = DIV_ROUND_UP(timings->tADL_min, t_clk);
	addr_2_data = min_t(int, addr_2_data, addr_2_data_mask);

	tmp = ioread32(denali->flash_reg + TCWAW_AND_ADDR_2_DATA);
	tmp &= ~addr_2_data_mask;
	tmp |= addr_2_data;
	iowrite32(tmp, denali->flash_reg + TCWAW_AND_ADDR_2_DATA);

	/* tREH, tWH -> RDWR_EN_HI_CNT */
	rdwr_en_hi = DIV_ROUND_UP(max(timings->tREH_min, timings->tWH_min),
				  t_clk);
	rdwr_en_hi = min_t(int, rdwr_en_hi, RDWR_EN_HI_CNT__VALUE);

	tmp = ioread32(denali->flash_reg + RDWR_EN_HI_CNT);
	tmp &= ~RDWR_EN_HI_CNT__VALUE;
	tmp |= rdwr_en_hi;
	iowrite32(tmp, denali->flash_reg + RDWR_EN_HI_CNT);

	/* tRP, tWP -> RDWR_EN_LO_CNT */
	rdwr_en_lo = DIV_ROUND_UP(max(timings->tRP_min, timings->tWP_min),
				  t_clk);
	rdwr_en_lo_hi = DIV_ROUND_UP(max(timings->tRC_min, timings->tWC_min),
				     t_clk);
	rdwr_en_lo_hi = max(rdwr_en_lo_hi, DENALI_CLK_X_MULT);
	rdwr_en_lo = max(rdwr_en_lo, rdwr_en_lo_hi - rdwr_en_hi);
	rdwr_en_lo = min_t(int, rdwr_en_lo, RDWR_EN_LO_CNT__VALUE);

	tmp = ioread32(denali->flash_reg + RDWR_EN_LO_CNT);
	tmp &= ~RDWR_EN_LO_CNT__VALUE;
	tmp |= rdwr_en_lo;
	iowrite32(tmp, denali->flash_reg + RDWR_EN_LO_CNT);

	/* tCS, tCEA -> CS_SETUP_CNT */
	cs_setup = max3((int)DIV_ROUND_UP(timings->tCS_min, t_clk) - rdwr_en_lo,
			(int)DIV_ROUND_UP(timings->tCEA_max, t_clk) - acc_clks,
			0);
	cs_setup = min_t(int, cs_setup, CS_SETUP_CNT__VALUE);

	tmp = ioread32(denali->flash_reg + CS_SETUP_CNT);
	tmp &= ~CS_SETUP_CNT__VALUE;
	tmp |= cs_setup;
	iowrite32(tmp, denali->flash_reg + CS_SETUP_CNT);

	return 0;
}

static void denali_reset_banks(struct denali_nand_info *denali)
{
	u32 irq_status;
	int i;

	for (i = 0; i < denali->max_banks; i++) {
		denali->flash_bank = i;

		denali_reset_irq(denali);

		iowrite32(DEVICE_RESET__BANK(i),
			  denali->flash_reg + DEVICE_RESET);

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
		denali->revision =
				swab16(ioread32(denali->flash_reg + REVISION));

	/*
	 * tell driver how many bit controller will skip before
	 * writing ECC code in OOB, this register may be already
	 * set by firmware. So we read this value out.
	 * if this value is 0, just let it be.
	 */
	denali->bbtskipbytes = ioread32(denali->flash_reg +
						SPARE_AREA_SKIP_BYTES);
	detect_max_banks(denali);
	iowrite32(0x0F, denali->flash_reg + RB_PIN_ENABLED);
	iowrite32(CHIP_EN_DONT_CARE__FLAG,
			denali->flash_reg + CHIP_ENABLE_DONT_CARE);

	iowrite32(0xffff, denali->flash_reg + SPARE_AREA_MARKER);

	/* Should set value for these registers when init */
	iowrite32(0, denali->flash_reg + TWO_ROW_ADDR_CYCLES);
	iowrite32(1, denali->flash_reg + ECC_ENABLE);
}

int denali_calc_ecc_bytes(int step_size, int strength)
{
	/* BCH code.  Denali requires ecc.bytes to be multiple of 2 */
	return DIV_ROUND_UP(strength * fls(step_size * 8), 16) * 2;
}
EXPORT_SYMBOL(denali_calc_ecc_bytes);

static int denali_ecc_setup(struct mtd_info *mtd, struct nand_chip *chip,
			    struct denali_nand_info *denali)
{
	int oobavail = mtd->oobsize - denali->bbtskipbytes;
	int ret;

	/*
	 * If .size and .strength are already set (usually by DT),
	 * check if they are supported by this controller.
	 */
	if (chip->ecc.size && chip->ecc.strength)
		return nand_check_ecc_caps(chip, denali->ecc_caps, oobavail);

	/*
	 * We want .size and .strength closest to the chip's requirement
	 * unless NAND_ECC_MAXIMIZE is requested.
	 */
	if (!(chip->ecc.options & NAND_ECC_MAXIMIZE)) {
		ret = nand_match_ecc_req(chip, denali->ecc_caps, oobavail);
		if (!ret)
			return 0;
	}

	/* Max ECC strength is the last thing we can do */
	return nand_maximize_ecc(chip, denali->ecc_caps, oobavail);
}

static int denali_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct denali_nand_info *denali = mtd_to_denali(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = denali->bbtskipbytes;
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

	oobregion->offset = chip->ecc.total + denali->bbtskipbytes;
	oobregion->length = mtd->oobsize - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops denali_ooblayout_ops = {
	.ecc = denali_ooblayout_ecc,
	.free = denali_ooblayout_free,
};

static uint8_t bbt_pattern[] = {'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = {'1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	8,
	.len = 4,
	.veroffs = 12,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs =	8,
	.len = 4,
	.veroffs = 12,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

/* initialize driver data structures */
static void denali_drv_init(struct denali_nand_info *denali)
{
	/*
	 * the completion object will be used to notify
	 * the callee that the interrupt is done
	 */
	init_completion(&denali->complete);

	/*
	 * the spinlock will be used to synchronize the ISR with any
	 * element that might be access shared data (interrupt status)
	 */
	spin_lock_init(&denali->irq_lock);
}

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
	denali->devnum = ioread32(denali->flash_reg + DEVICES_CONNECTED);

	/*
	 * On some SoCs, DEVICES_CONNECTED is not auto-detected.
	 * For those, DEVICES_CONNECTED is left to 0.  Set 1 if it is the case.
	 */
	if (denali->devnum == 0) {
		denali->devnum = 1;
		iowrite32(1, denali->flash_reg + DEVICES_CONNECTED);
	}

	if (denali->devnum == 1)
		return 0;

	if (denali->devnum != 2) {
		dev_err(denali->dev, "unsupported number of devices %d\n",
			denali->devnum);
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
	denali->bbtskipbytes <<= 1;

	return 0;
}

int denali_init(struct denali_nand_info *denali)
{
	struct nand_chip *chip = &denali->nand;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	mtd->dev.parent = denali->dev;
	denali_hw_init(denali);
	denali_drv_init(denali);

	denali_clear_irq_all(denali);

	/* Request IRQ after all the hardware initialization is finished */
	ret = devm_request_irq(denali->dev, denali->irq, denali_isr,
			       IRQF_SHARED, DENALI_NAND_NAME, denali);
	if (ret) {
		dev_err(denali->dev, "Unable to request IRQ\n");
		return ret;
	}

	denali_enable_irq(denali);
	denali_reset_banks(denali);

	denali->flash_bank = CHIP_SELECT_INVALID;

	nand_set_flash_node(chip, denali->dev->of_node);
	/* Fallback to the default name if DT did not give "label" property */
	if (!mtd->name)
		mtd->name = "denali-nand";

	/* register the driver with the NAND core subsystem */
	chip->select_chip = denali_select_chip;
	chip->read_byte = denali_read_byte;
	chip->write_byte = denali_write_byte;
	chip->read_word = denali_read_word;
	chip->cmd_ctrl = denali_cmd_ctrl;
	chip->dev_ready = denali_dev_ready;
	chip->waitfunc = denali_waitfunc;

	/* clk rate info is needed for setup_data_interface */
	if (denali->clk_x_rate)
		chip->setup_data_interface = denali_setup_data_interface;

	/*
	 * scan for NAND devices attached to the controller
	 * this is the first stage in a two step process to register
	 * with the nand subsystem
	 */
	ret = nand_scan_ident(mtd, denali->max_banks, NULL);
	if (ret)
		goto disable_irq;

	denali->buf = devm_kzalloc(denali->dev, mtd->writesize + mtd->oobsize,
				   GFP_KERNEL);
	if (!denali->buf) {
		ret = -ENOMEM;
		goto disable_irq;
	}

	ret = dma_set_mask(denali->dev,
			   DMA_BIT_MASK(denali->caps & DENALI_CAP_DMA_64BIT ?
					64 : 32));
	if (ret) {
		dev_err(denali->dev, "No usable DMA configuration\n");
		goto disable_irq;
	}

	denali->dma_addr = dma_map_single(denali->dev, denali->buf,
			     mtd->writesize + mtd->oobsize,
			     DMA_BIDIRECTIONAL);
	if (dma_mapping_error(denali->dev, denali->dma_addr)) {
		dev_err(denali->dev, "Failed to map DMA buffer\n");
		ret = -EIO;
		goto disable_irq;
	}

	/*
	 * second stage of the NAND scan
	 * this stage requires information regarding ECC and
	 * bad block management.
	 */

	/* Bad block management */
	chip->bbt_td = &bbt_main_descr;
	chip->bbt_md = &bbt_mirror_descr;

	/* skip the scan for now until we have OOB read and write support */
	chip->bbt_options |= NAND_BBT_USE_FLASH;
	chip->options |= NAND_SKIP_BBTSCAN;
	chip->ecc.mode = NAND_ECC_HW_SYNDROME;

	/* no subpage writes on denali */
	chip->options |= NAND_NO_SUBPAGE_WRITE;

	ret = denali_ecc_setup(mtd, chip, denali);
	if (ret) {
		dev_err(denali->dev, "Failed to setup ECC settings.\n");
		goto disable_irq;
	}

	dev_dbg(denali->dev,
		"chosen ECC settings: step=%d, strength=%d, bytes=%d\n",
		chip->ecc.size, chip->ecc.strength, chip->ecc.bytes);

	iowrite32(chip->ecc.strength, denali->flash_reg + ECC_CORRECTION);
	iowrite32(mtd->erasesize / mtd->writesize,
		  denali->flash_reg + PAGES_PER_BLOCK);
	iowrite32(chip->options & NAND_BUSWIDTH_16 ? 1 : 0,
		  denali->flash_reg + DEVICE_WIDTH);
	iowrite32(mtd->writesize, denali->flash_reg + DEVICE_MAIN_AREA_SIZE);
	iowrite32(mtd->oobsize, denali->flash_reg + DEVICE_SPARE_AREA_SIZE);

	iowrite32(chip->ecc.size, denali->flash_reg + CFG_DATA_BLOCK_SIZE);
	iowrite32(chip->ecc.size, denali->flash_reg + CFG_LAST_DATA_BLOCK_SIZE);
	/* chip->ecc.steps is set by nand_scan_tail(); not available here */
	iowrite32(mtd->writesize / chip->ecc.size,
		  denali->flash_reg + CFG_NUM_DATA_BLOCKS);

	mtd_set_ooblayout(mtd, &denali_ooblayout_ops);

	if (chip->options & NAND_BUSWIDTH_16) {
		chip->read_buf = denali_read_buf16;
		chip->write_buf = denali_write_buf16;
	} else {
		chip->read_buf = denali_read_buf;
		chip->write_buf = denali_write_buf;
	}
	chip->ecc.options |= NAND_ECC_CUSTOM_PAGE_ACCESS;
	chip->ecc.read_page = denali_read_page;
	chip->ecc.read_page_raw = denali_read_page_raw;
	chip->ecc.write_page = denali_write_page;
	chip->ecc.write_page_raw = denali_write_page_raw;
	chip->ecc.read_oob = denali_read_oob;
	chip->ecc.write_oob = denali_write_oob;
	chip->erase = denali_erase;

	ret = denali_multidev_fixup(denali);
	if (ret)
		goto disable_irq;

	ret = nand_scan_tail(mtd);
	if (ret)
		goto disable_irq;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(denali->dev, "Failed to register MTD: %d\n", ret);
		goto disable_irq;
	}
	return 0;

disable_irq:
	denali_disable_irq(denali);

	return ret;
}
EXPORT_SYMBOL(denali_init);

/* driver exit point */
void denali_remove(struct denali_nand_info *denali)
{
	struct mtd_info *mtd = nand_to_mtd(&denali->nand);
	/*
	 * Pre-compute DMA buffer size to avoid any problems in case
	 * nand_release() ever changes in a way that mtd->writesize and
	 * mtd->oobsize are not reliable after this call.
	 */
	int bufsize = mtd->writesize + mtd->oobsize;

	nand_release(mtd);
	denali_disable_irq(denali);
	dma_unmap_single(denali->dev, denali->dma_addr, bufsize,
			 DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(denali_remove);

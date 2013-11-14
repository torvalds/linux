/*
 * MTD SPI driver for ST M25Pxx (and similar) serial flash chips
 *
 * Author: Mike Lavender, mike@steroidmicros.com
 *
 * Copyright (c) 2005, Intec Automation Inc.
 *
 * Some parts are based on lart.c by Abraham Van Der Merwe
 *
 * Cleaned up and generalized based on mtd_dataflash.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mod_devicetable.h>

#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/of_platform.h>

#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

/* Flash opcodes. */
#define	OPCODE_WREN		0x06	/* Write enable */
#define	OPCODE_RDSR		0x05	/* Read status register */
#define	OPCODE_WRSR		0x01	/* Write status register 1 byte */
#define	OPCODE_RFSR		0x70  /* read flag status register */
#define	OPCODE_NORM_READ	0x03	/* Read data bytes (low frequency) */
#define	OPCODE_FAST_READ	0x0b	/* Read data bytes (high frequency) */
#define OPCODE_QUAD_READ	0x6b	/* Quad read command */
#define	OPCODE_PP		0x02	/* Page program (up to 256 bytes) */
#define OPCODE_QPP		0x32	/* Quad page program */
#define	OPCODE_BE_4K		0x20	/* Erase 4KiB block */
#define	OPCODE_BE_32K		0x52	/* Erase 32KiB block */
#define	OPCODE_CHIP_ERASE	0xc7	/* Erase whole flash chip */
#define	OPCODE_SE		0xd8	/* Sector erase (usually 64KiB) */
#define	OPCODE_RDID		0x9f	/* Read JEDEC ID */
#define OPCODE_RDFSR		0x70	/* Read Flag Status Register */
#define OPCODE_WREAR		0xc5	/* Write Extended Address Register */

/* Used for SST flashes only. */
#define	OPCODE_BP		0x02	/* Byte program */
#define	OPCODE_WRDI		0x04	/* Write disable */
#define	OPCODE_AAI_WP		0xad	/* Auto address increment word program */

/* Used for Macronix flashes only. */
#define	OPCODE_EN4B		0xb7	/* Enter 4-byte mode */
#define	OPCODE_EX4B		0xe9	/* Exit 4-byte mode */

/* Used for Spansion flashes only. */
#define	OPCODE_BRWR		0x17	/* Bank register write */
#define	OPCODE_BRRD		0x16	/* Bank register read */

/* Used for Numonyx flashes only */
#define NUMONYX_ID             0x20
#define OPCODE_RVCR            0x85    /* Read volatile configuration register */
#define OPCODE_WVCR            0x81    /* Write volatile configuration register */
#define VCR_XIP_SHIFT                  0x03
#define VCR_XIP_MASK                   0x08
#define VCR_DUMMY_CLK_CYCLES_SHIFT     0x04
#define VCR_DUMMY_CLK_CYCLES_MASK      0xf0

/* Status Register bits. */
#define	SR_WIP			1	/* Write in progress */
#define	SR_WEL			2	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define	SR_BP0			4	/* Block protect 0 */
#define	SR_BP1			8	/* Block protect 1 */
#define	SR_BP2			0x10	/* Block protect 2 */
#define	SR_SRWD			0x80	/* SR write protect */

/* Flag Status Register bits */
#define	FSR_READY         (1 << 7)
#define	FSR_ERASE_SUSPEND (1 << 6)
#define	FSR_ERASE_ERROR   (1 << 5)
#define	FSR_PGM_ERROR     (1 << 4)
#define	FSR_VPP_INVALID   (1 << 3)
#define	FSR_PGM_SUSPEND   (1 << 2)
#define	FSR_PROTECT_ERROR (1 << 1)
#define	FSR_4BYTE_ADDR    (1 << 0)

/* Define max times to check status register before we give up. */
#define	MAX_READY_WAIT_JIFFIES	(480 * HZ) /* N25Q specs 480s max chip erase */
#define	MAX_CMD_SIZE		5

#define JEDEC_MFR(_jedec_id)	((_jedec_id) >> 16)

/****************************************************************************/

struct m25p {
	struct spi_device	*spi;
	struct mutex		lock;
	struct mtd_info		mtd;
	u16			page_size;
	u16			addr_width;
	u8			erase_opcode;
	u8			*command;
	bool			fast_read;
	u16			curbank;
	u32			jedec_id;
	bool			check_fsr;
	bool			shift;
	bool			isparallel;
	bool			isstacked;
	u8			read_opcode;
	u8			prog_opcode;
	u8			dummycount;
	struct flash_info *info;
	int (*wait_till_ready)(struct m25p *flash);
};

static inline struct m25p *mtd_to_m25p(struct mtd_info *mtd)
{
	return container_of(mtd, struct m25p, mtd);
}

/****************************************************************************/

/*
 * Internal helper functions
 */

/*
 * Read register, returning its value in the location
 * Returns negative if error occurred.
 */
static inline int read_spi_reg(struct m25p *flash, u8 code, const char *name)
{
	ssize_t retval;
	u8 val;

	retval = spi_write_then_read(flash->spi, &code, 1, &val, 1);

	if (retval < 0) {
		dev_err(&flash->spi->dev, "error %d reading %s\n",
				(int) retval, name);
		return retval;
	}

	return val;
}

/*
 * Read flag status register, returning its value in the location
 * Return flag status register value.
 * Returns negative if error occurred.
 */
static int read_fsr(struct m25p *flash)
{
	return read_spi_reg(flash, OPCODE_RDFSR, "FSR");
}

/*
 * Read the status register, returning its value in the location
 * Return the status register value.
 * Returns negative if error occurred.
 */
static int read_sr(struct m25p *flash)
{
	ssize_t retval;
	u8 code = OPCODE_RFSR;
	u8 val;

	retval = spi_write_then_read(flash->spi, &code, 1, &val, 1);

	if (retval < 0) {
		dev_err(&flash->spi->dev, "error %d reading SR\n",
				(int) retval);
		return retval;
	}

	return val;
}

/*
 * Write status register 1 byte
 * Returns negative if error occurred.
 */
static int write_sr(struct m25p *flash, u8 val)
{
	flash->command[0] = OPCODE_WRSR;
	flash->command[1] = val;

	return spi_write(flash->spi, flash->command, 2);
}

/*
 * Write volatile config register
 * Returns negative if error occurred.
 */
static int write_vcr(struct m25p *flash, u8 val)
{
	flash->command[0] = OPCODE_WVCR;
	flash->command[1] = val;

	return spi_write(flash->spi, flash->command, 2);
}

/*
 * Read volatile config register
 * Returns negative if error occurred.
 */
static int read_vcr(struct m25p *flash)
{
	ssize_t retval;
	u8 code = OPCODE_RVCR;
	u8 val;

	retval = spi_write_then_read(flash->spi, &code, 1, &val, 1);

	if (retval < 0) {
		dev_err(&flash->spi->dev, "error %d reading VCR\n",
				(int) retval);
		return retval;
	}

	return val;
}

/*
 * Set write enable latch with Write Enable command.
 * Returns negative if error occurred.
 */
static inline int write_enable(struct m25p *flash)
{
	u8	code = OPCODE_WREN;

	return spi_write_then_read(flash->spi, &code, 1, NULL, 0);
}

/*
 * Send write disble instruction to the chip.
 */
static inline int write_disable(struct m25p *flash)
{
	u8	code = OPCODE_WRDI;

	return spi_write_then_read(flash->spi, &code, 1, NULL, 0);
}

/*
 * Enable/disable 4-byte addressing mode.
 */
static inline int set_4byte(struct m25p *flash, u32 jedec_id, int enable)
{
	int ret;
	u8 val;

	switch (JEDEC_MFR(jedec_id)) {
	case CFI_MFR_MACRONIX:
	case 0xEF /* winbond */:
	case CFI_MFR_ST:
		flash->command[0] = enable ? OPCODE_EN4B : OPCODE_EX4B;
		return spi_write(flash->spi, flash->command, 1);
	default:
		/* Spansion style */
		flash->command[0] = OPCODE_BRWR;
		flash->command[1] = enable << 7;
		ret = spi_write(flash->spi, flash->command, 2);

		/* verify the 4 byte mode is enabled */
		flash->command[0] = OPCODE_BRRD;
		spi_write_then_read(flash->spi, flash->command, 1, &val, 1);
		if (val != enable << 7) {
			dev_warn(&flash->spi->dev,
				 "fallback to 3-byte address mode\n");
			dev_warn(&flash->spi->dev,
				 "maximum accessible size is 16MB\n");
			flash->addr_width = 3;
		}
		return ret;
	}
}

/*
 * Service routine to read status register until ready, or timeout occurs.
 * Returns non-zero if error.
 */
static int _wait_till_ready(struct m25p *flash)
{
	unsigned long deadline;
	int sr, fsr;

	deadline = jiffies + MAX_READY_WAIT_JIFFIES;

	do {
		if ((sr = read_sr(flash)) < 0)
			break;
		else if (!(sr & SR_WIP)) {
			if (flash->check_fsr) {
				fsr = read_fsr(flash);
				if (!(fsr & FSR_READY))
					return 1;
			}
			return 0;
		}

		cond_resched();

	} while (!time_after_eq(jiffies, deadline));

	return 1;
}

/*
 * Update Extended Address/bank selection Register.
 * Call with flash->lock locked.
 */
static int write_ear(struct m25p *flash, u32 addr)
{
	u8 ear;
	int ret;

	/* Wait until finished previous write command. */
	if (_wait_till_ready(flash))
		return 1;

	if (flash->mtd.size <= (0x1000000) << flash->shift)
		return 0;

	addr = addr % (u32) flash->mtd.size;
	ear = addr >> 24;

	if ((!flash->isstacked) && (ear == flash->curbank))
		return 0;

	if (flash->isstacked && (flash->mtd.size <= 0x2000000))
		return 0;

	if (JEDEC_MFR(flash->jedec_id) == 0x01)
		flash->command[0] = OPCODE_BRWR;
	if (JEDEC_MFR(flash->jedec_id) == 0x20) {
		write_enable(flash);
		flash->command[0] = OPCODE_WREAR;
	}
	flash->command[1] = ear;

	ret = spi_write(flash->spi, flash->command, 2);
	if (ret)
		return ret;

	flash->curbank = ear;

	return 0;
}

/*
 * Service routine to read flag status register until ready, or timeout occurs.
 * Returns non-zero if error.
 */
static int _wait_till_fsr_ready(struct m25p *flash)
{
	unsigned long deadline;
	int fsr;
	int sr;

	deadline = jiffies + MAX_READY_WAIT_JIFFIES;

	do {
		sr = read_sr(flash);
		if (sr < 0)
			break;
		/* only check fsr if sr not busy */
		if (!(sr & SR_WIP)) {
			fsr = read_fsr(flash);
			if (fsr < 0)
				break;
			if (fsr & FSR_READY)
				return 0;
		}

		cond_resched();

	} while (!time_after_eq(jiffies, deadline));

	return 1;
}



/*
 * Erase the whole flash memory
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_chip(struct m25p *flash)
{
	pr_debug("%s: %s %lldKiB\n", dev_name(&flash->spi->dev), __func__,
			(long long)(flash->mtd.size >> 10));

	/* Wait until finished previous write command. */
	if (flash->wait_till_ready(flash))
		return 1;

	if (flash->isstacked)
		flash->spi->master->flags &= ~SPI_MASTER_U_PAGE;

	/* Send write enable, then erase commands. */
	write_enable(flash);

	/* Set up command buffer. */
	flash->command[0] = OPCODE_CHIP_ERASE;

	spi_write(flash->spi, flash->command, 1);

	flash->wait_till_ready(flash);

	return 0;
}

static void m25p_addr2cmd(struct m25p *flash, unsigned int addr, u8 *cmd)
{
	int i;

	/* opcode is in cmd[0] */
	for (i = 1; i <= flash->addr_width; i++)
		cmd[i] = addr >> (flash->addr_width * 8 - i * 8);
}

static int m25p_cmdsz(struct m25p *flash)
{
	return 1 + flash->addr_width;
}

/*
 * Erase one sector of flash memory at offset ``offset'' which is any
 * address within the sector which should be erased.
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_sector(struct m25p *flash, u32 offset)
{
	pr_debug("%s: %s %dKiB at 0x%08x\n", dev_name(&flash->spi->dev),
			__func__, flash->mtd.erasesize / 1024, offset);

	/* Wait until finished previous write command. */
	if (flash->wait_till_ready(flash))
		return 1;

	/* update Extended Address Register */
	if (write_ear(flash, offset))
		return 1;

	/* Send write enable, then erase commands. */
	write_enable(flash);

	/* Set up command buffer. */
	flash->command[0] = flash->erase_opcode;
	m25p_addr2cmd(flash, offset, flash->command);

	spi_write(flash->spi, flash->command, m25p_cmdsz(flash));

	flash->wait_till_ready(flash);

	return 0;
}

/****************************************************************************/

/*
 * MTD implementation
 */

/*
 * Erase an address range on the flash chip.  The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int m25p80_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 addr, len, offset;
	uint32_t rem;

	pr_debug("%s: %s at 0x%llx, len %lld\n", dev_name(&flash->spi->dev),
			__func__, (long long)instr->addr,
			(long long)instr->len);

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	addr = instr->addr;
	len = instr->len;

	mutex_lock(&flash->lock);

	/* whole-chip erase? */
	if (len == flash->mtd.size) {
		if (erase_chip(flash)) {
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&flash->lock);
			return -EIO;
		}

	/* REVISIT in some cases we could speed up erasing large regions
	 * by using OPCODE_SE instead of OPCODE_BE_4K.  We may have set up
	 * to use "small sector erase", but that's not always optimal.
	 */

	/* "sector"-at-a-time erase */
	} else {
		while (len) {
			offset = addr;
			if (flash->isparallel == 1)
				offset /= 2;
			if (flash->isstacked == 1) {
				if (offset >= (flash->mtd.size / 2)) {
					offset = offset - (flash->mtd.size / 2);
					flash->spi->master->flags |=
							SPI_MASTER_U_PAGE;
				} else
					flash->spi->master->flags &=
							~SPI_MASTER_U_PAGE;
			}
			if (erase_sector(flash, offset)) {
				instr->state = MTD_ERASE_FAILED;
				mutex_unlock(&flash->lock);
				return -EIO;
			}

			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&flash->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

/*
 * Read an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int m25p80_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct spi_transfer t[2];
	struct spi_message m;

	pr_debug("%s: %s from 0x%08x, len %zd\n", dev_name(&flash->spi->dev),
			__func__, (u32)from, len);

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	/* NOTE:
	 * OPCODE_FAST_READ (if available) is faster.
	 * Should add 1 byte DUMMY_BYTE.
	 */
	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(flash) + flash->dummycount;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	/* Wait till previous write/erase is done. */
	if (flash->wait_till_ready(flash)) {
		/* REVISIT status return?? */
		return 1;
	}

	/* FIXME switch to OPCODE_FAST_READ.  It's required for higher
	 * clocks; and at this writing, every chip this driver handles
	 * supports that opcode.
	 */

	/* Set up the write data buffer. */
	flash->command[0] = flash->read_opcode;
	m25p_addr2cmd(flash, from, flash->command);

	spi_sync(flash->spi, &m);

	*retlen = m.actual_length - m25p_cmdsz(flash) -
			flash->dummycount;

	return 0;
}

static int m25p80_read_ext(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 addr = from;
	u32 offset = from;
	u32 read_len = 0;
	u32 actual_len = 0;
	u32 read_count = 0;
	u32 rem_bank_len = 0;
	u8 bank = 0;

#define OFFSET_16_MB 0x1000000

	mutex_lock(&flash->lock);

	while (len) {
		bank = addr / (OFFSET_16_MB << flash->shift);
		rem_bank_len = ((OFFSET_16_MB << flash->shift) * (bank + 1)) -
				addr;
		offset = addr;
		if (flash->isparallel == 1)
			offset /= 2;
		if (flash->isstacked == 1) {
			if (offset >= (flash->mtd.size / 2)) {
				offset = offset - (flash->mtd.size / 2);
				flash->spi->master->flags |= SPI_MASTER_U_PAGE;
			} else {
				flash->spi->master->flags &= ~SPI_MASTER_U_PAGE;
			}
		}
		write_ear(flash, offset);
		if (len < rem_bank_len)
			read_len = len;
		else
			read_len = rem_bank_len;

		m25p80_read(mtd, offset, read_len, &actual_len, buf);

		addr += actual_len;
		len -= actual_len;
		buf += actual_len;
		read_count += actual_len;
	}

	*retlen = read_count;

	mutex_unlock(&flash->lock);
	return 0;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int m25p80_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 page_offset, page_size;
	struct spi_transfer t[2];
	struct spi_message m;

	pr_debug("%s: %s to 0x%08x, len %zd\n", dev_name(&flash->spi->dev),
			__func__, (u32)to, len);

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(flash);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	spi_message_add_tail(&t[1], &m);

	/* Wait until finished previous write command. */
	if (flash->wait_till_ready(flash)) {
		mutex_unlock(&flash->lock);
		return 1;
	}

	write_enable(flash);

	/* Set up the opcode in the write buffer. */
	flash->command[0] = flash->prog_opcode;
	m25p_addr2cmd(flash, (to >> flash->shift), flash->command);

	page_offset = to & (flash->page_size - 1);

	/* do all the bytes fit onto one page? */
	if (page_offset + len <= flash->page_size) {
		t[1].len = len;

		spi_sync(flash->spi, &m);

		*retlen = m.actual_length - m25p_cmdsz(flash);
	} else {
		u32 i;

		/* the size of data remaining on the first page */
		page_size = flash->page_size - page_offset;

		t[1].len = page_size;
		spi_sync(flash->spi, &m);

		*retlen = m.actual_length - m25p_cmdsz(flash);

		/* write everything in flash->page_size chunks */
		for (i = page_size; i < len; i += page_size) {
			page_size = len - i;
			if (page_size > flash->page_size)
				page_size = flash->page_size;

			/* write the next page to flash */
			m25p_addr2cmd(flash, ((to + i) >> flash->shift),
					flash->command);

			t[1].tx_buf = buf + i;
			t[1].len = page_size;

			flash->wait_till_ready(flash);

			write_enable(flash);

			spi_sync(flash->spi, &m);

			*retlen += m.actual_length - m25p_cmdsz(flash);
		}
	}

	flash->wait_till_ready(flash);

	mutex_unlock(&flash->lock);

	return 0;
}

static int m25p80_write_ext(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	u32 addr = to;
	u32 offset = to;
	u32 write_len = 0;
	u32 actual_len = 0;
	u32 write_count = 0;
	u32 rem_bank_len = 0;
	u8 bank = 0;

#define OFFSET_16_MB 0x1000000

	mutex_lock(&flash->lock);
	while (len) {
		bank = addr / (OFFSET_16_MB << flash->shift);
		rem_bank_len = ((OFFSET_16_MB << flash->shift) * (bank + 1)) -
				addr;
		offset = addr;

		if (flash->isstacked == 1) {
			if (offset >= (flash->mtd.size / 2)) {
				offset = offset - (flash->mtd.size / 2);
				flash->spi->master->flags |= SPI_MASTER_U_PAGE;
			} else {
				flash->spi->master->flags &= ~SPI_MASTER_U_PAGE;
			}
		}
		write_ear(flash, (offset >> flash->shift));
		if (len < rem_bank_len)
			write_len = len;
		else
			write_len = rem_bank_len;

		m25p80_write(mtd, offset, write_len, &actual_len, buf);

		addr += actual_len;
		len -= actual_len;
		buf += actual_len;
		write_count += actual_len;
	}

	*retlen = write_count;

	mutex_unlock(&flash->lock);
	return 0;
}

static int sst_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct spi_transfer t[2];
	struct spi_message m;
	size_t actual;
	int cmd_sz, ret;

	pr_debug("%s: %s to 0x%08x, len %zd\n", dev_name(&flash->spi->dev),
			__func__, (u32)to, len);

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = m25p_cmdsz(flash);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&flash->lock);

	/* Wait until finished previous write command. */
	ret = flash->wait_till_ready(flash);
	if (ret)
		goto time_out;

	write_enable(flash);

	actual = to % 2;
	/* Start write from odd address. */
	if (actual) {
		flash->command[0] = OPCODE_BP;
		m25p_addr2cmd(flash, to, flash->command);

		/* write one byte. */
		t[1].len = 1;
		spi_sync(flash->spi, &m);
		ret = flash->wait_till_ready(flash);
		if (ret)
			goto time_out;
		*retlen += m.actual_length - m25p_cmdsz(flash);
	}
	to += actual;

	flash->command[0] = OPCODE_AAI_WP;
	m25p_addr2cmd(flash, to, flash->command);

	/* Write out most of the data here. */
	cmd_sz = m25p_cmdsz(flash);
	for (; actual < len - 1; actual += 2) {
		t[0].len = cmd_sz;
		/* write two bytes. */
		t[1].len = 2;
		t[1].tx_buf = buf + actual;

		spi_sync(flash->spi, &m);
		ret = flash->wait_till_ready(flash);
		if (ret)
			goto time_out;
		*retlen += m.actual_length - cmd_sz;
		cmd_sz = 1;
		to += 2;
	}
	write_disable(flash);
	ret = flash->wait_till_ready(flash);
	if (ret)
		goto time_out;

	/* Write out trailing byte if it exists. */
	if (actual != len) {
		write_enable(flash);
		flash->command[0] = OPCODE_BP;
		m25p_addr2cmd(flash, to, flash->command);
		t[0].len = m25p_cmdsz(flash);
		t[1].len = 1;
		t[1].tx_buf = buf + actual;

		spi_sync(flash->spi, &m);
		ret = flash->wait_till_ready(flash);
		if (ret)
			goto time_out;
		*retlen += m.actual_length - m25p_cmdsz(flash);
		write_disable(flash);
	}

time_out:
	mutex_unlock(&flash->lock);
	return ret;
}

static int m25p80_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	uint32_t offset = ofs;
	uint8_t status_old, status_new;
	int res = 0;

	mutex_lock(&flash->lock);
	/* Wait until finished previous command */
	if (flash->wait_till_ready(flash)) {
		res = 1;
		goto err;
	}

	status_old = read_sr(flash);

	if (offset < flash->mtd.size-(flash->mtd.size/2))
		status_new = status_old | SR_BP2 | SR_BP1 | SR_BP0;
	else if (offset < flash->mtd.size-(flash->mtd.size/4))
		status_new = (status_old & ~SR_BP0) | SR_BP2 | SR_BP1;
	else if (offset < flash->mtd.size-(flash->mtd.size/8))
		status_new = (status_old & ~SR_BP1) | SR_BP2 | SR_BP0;
	else if (offset < flash->mtd.size-(flash->mtd.size/16))
		status_new = (status_old & ~(SR_BP0|SR_BP1)) | SR_BP2;
	else if (offset < flash->mtd.size-(flash->mtd.size/32))
		status_new = (status_old & ~SR_BP2) | SR_BP1 | SR_BP0;
	else if (offset < flash->mtd.size-(flash->mtd.size/64))
		status_new = (status_old & ~(SR_BP2|SR_BP0)) | SR_BP1;
	else
		status_new = (status_old & ~(SR_BP2|SR_BP1)) | SR_BP0;

	/* Only modify protection if it will not unlock other areas */
	if ((status_new&(SR_BP2|SR_BP1|SR_BP0)) >
					(status_old&(SR_BP2|SR_BP1|SR_BP0))) {
		write_enable(flash);
		if (write_sr(flash, status_new) < 0) {
			res = 1;
			goto err;
		}
	}

err:	mutex_unlock(&flash->lock);
	return res;
}

static int m25p80_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	uint32_t offset = ofs;
	uint8_t status_old, status_new;
	int res = 0;

	mutex_lock(&flash->lock);
	/* Wait until finished previous command */
	if (flash->wait_till_ready(flash)) {
		res = 1;
		goto err;
	}

	status_old = read_sr(flash);

	if (offset+len > flash->mtd.size-(flash->mtd.size/64))
		status_new = status_old & ~(SR_BP2|SR_BP1|SR_BP0);
	else if (offset+len > flash->mtd.size-(flash->mtd.size/32))
		status_new = (status_old & ~(SR_BP2|SR_BP1)) | SR_BP0;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/16))
		status_new = (status_old & ~(SR_BP2|SR_BP0)) | SR_BP1;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/8))
		status_new = (status_old & ~SR_BP2) | SR_BP1 | SR_BP0;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/4))
		status_new = (status_old & ~(SR_BP0|SR_BP1)) | SR_BP2;
	else if (offset+len > flash->mtd.size-(flash->mtd.size/2))
		status_new = (status_old & ~SR_BP1) | SR_BP2 | SR_BP0;
	else
		status_new = (status_old & ~SR_BP0) | SR_BP2 | SR_BP1;

	/* Only modify protection if it will not lock other areas */
	if ((status_new&(SR_BP2|SR_BP1|SR_BP0)) <
					(status_old&(SR_BP2|SR_BP1|SR_BP0))) {
		write_enable(flash);
		if (write_sr(flash, status_new) < 0) {
			res = 1;
			goto err;
		}
	}

err:	mutex_unlock(&flash->lock);
	return res;
}

/****************************************************************************/

/*
 * SPI device driver setup and teardown
 */

struct flash_info {
	/* JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32		jedec_id;
	u16             ext_id;

	/* The size listed here is what works with OPCODE_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	u16		page_size;
	u16		addr_width;

	u16		flags;
#define	SECT_4K		0x01		/* OPCODE_BE_4K works uniformly */
#define	M25P_NO_ERASE	0x02		/* No erase command needed */
#define	SST_WRITE	0x04		/* use SST byte programming */
#define	SECT_32K	0x10		/* OPCODE_BE_32K */
#define	M25P_NO_FR	0x08		/* Can't do fastread */
#define	SECT_4K_PMC	0x10		/* OPCODE_BE_4K_PMC works uniformly */
#define	USE_FSR		0x20		/* use flag status register */
#define	SHUTDOWN_3BYTE	0x40		/* set 3-byte mode on shutdown */
};

#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.jedec_id = (_jedec_id),				\
		.ext_id = (_ext_id),					\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),					\
	})

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_width)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_width = (_addr_width),				\
		.flags = M25P_NO_ERASE,					\
	})

/* NOTE: double check command sets and memory organization when you add
 * more flash chips.  This current list focusses on newer chips, which
 * have been converging on command sets which including JEDEC ID.
 */
static const struct spi_device_id m25p_ids[] = {
	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",  INFO(0x1f6601, 0, 32 * 1024,   4, SECT_4K) },
	{ "at25fs040",  INFO(0x1f6604, 0, 64 * 1024,   8, SECT_4K) },

	{ "at25df041a", INFO(0x1f4401, 0, 64 * 1024,   8, SECT_4K) },
	{ "at25df321a", INFO(0x1f4701, 0, 64 * 1024,  64, SECT_4K) },
	{ "at25df641",  INFO(0x1f4800, 0, 64 * 1024, 128, SECT_4K) },

	{ "at26f004",   INFO(0x1f0400, 0, 64 * 1024,  8, SECT_4K) },
	{ "at26df081a", INFO(0x1f4501, 0, 64 * 1024, 16, SECT_4K) },
	{ "at26df161a", INFO(0x1f4601, 0, 64 * 1024, 32, SECT_4K) },
	{ "at26df321",  INFO(0x1f4700, 0, 64 * 1024, 64, SECT_4K) },

	{ "at45db081d", INFO(0x1f2500, 0, 64 * 1024, 16, SECT_4K) },

	/* EON -- en25xxx */
	{ "en25f32", INFO(0x1c3116, 0, 64 * 1024,  64, SECT_4K) },
	{ "en25p32", INFO(0x1c2016, 0, 64 * 1024,  64, 0) },
	{ "en25q32b", INFO(0x1c3016, 0, 64 * 1024,  64, 0) },
	{ "en25p64", INFO(0x1c2017, 0, 64 * 1024, 128, 0) },
	{ "en25q64", INFO(0x1c3017, 0, 64 * 1024, 128, SECT_4K) },
	{ "en25qh256", INFO(0x1c7019, 0, 64 * 1024, 512, 0) },

	/* Everspin */
	{ "mr25h256", CAT25_INFO(  32 * 1024, 1, 256, 2) },

	/* GigaDevice */
	{ "gd25q32", INFO(0xc84016, 0, 64 * 1024,  64, SECT_4K) },
	{ "gd25q64", INFO(0xc84017, 0, 64 * 1024, 128, SECT_4K) },

	/* Intel/Numonyx -- xxxs33b */
	{ "160s33b",  INFO(0x898911, 0, 64 * 1024,  32, 0) },
	{ "320s33b",  INFO(0x898912, 0, 64 * 1024,  64, 0) },
	{ "640s33b",  INFO(0x898913, 0, 64 * 1024, 128, 0) },
	{ "n25q064",  INFO(0x20ba17, 0, 64 * 1024, 128, 0) },
	
	/* Micron/Numonyx */
	{ "n25q128",  INFO(0x20ba18, 0, 64 * 1024, 256, 0) },

	/* Macronix */
	{ "mx25l2005a",  INFO(0xc22012, 0, 64 * 1024,   4, SECT_4K) },
	{ "mx25l4005a",  INFO(0xc22013, 0, 64 * 1024,   8, SECT_4K) },
	{ "mx25l8005",   INFO(0xc22014, 0, 64 * 1024,  16, 0) },
	{ "mx25l1606e",  INFO(0xc22015, 0, 64 * 1024,  32, SECT_4K) },
	{ "mx25l3205d",  INFO(0xc22016, 0, 64 * 1024,  64, 0) },
	{ "mx25l6405d",  INFO(0xc22017, 0, 64 * 1024, 128, 0) },
	{ "mx25l12805d", INFO(0xc22018, 0, 64 * 1024, 256, 0) },
	{ "mx25l12855e", INFO(0xc22618, 0, 64 * 1024, 256, 0) },
	{ "mx25l25635e", INFO(0xc22019, 0, 64 * 1024, 512, 0) },
	{ "mx25l25655e", INFO(0xc22619, 0, 64 * 1024, 512, 0) },
	{ "mx66l51235l", INFO(0xc2201a, 0, 64 * 1024, 1024, 0) },

	/* Micron */
	{ "n25q064",  INFO(0x20ba17, 0, 64 * 1024, 128, 0) },
	{ "n25q128a11",  INFO(0x20bb18, 0, 64 * 1024, 256, 0) },
	{ "n25q128a13",  INFO(0x20ba18, 0, 64 * 1024, 256, 0) },
	{ "n25q256a", INFO(0x20ba19, 0, 64 * 1024, 512, SECT_4K) },
	{ "n25q512a", INFO(0x20ba20, 0, 64 * 1024, 1024,
		USE_FSR|SHUTDOWN_3BYTE) },
	{ "n25q00",   INFO(0x20ba21, 0, 64 * 1024, 2048,
		USE_FSR|SHUTDOWN_3BYTE) },

	/* Spansion -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl032p",  INFO(0x010215, 0x4d00,  64 * 1024,  64, 0) },
	{ "s25sl064p",  INFO(0x010216, 0x4d00,  64 * 1024, 128, 0) },
	{ "s25fl256s0", INFO(0x010219, 0x4d00, 256 * 1024, 128, 0) },
	{ "s25fl256s1", INFO(0x010219, 0x4d01,  64 * 1024, 512, 0) },
	{ "s25fl512s",  INFO(0x010220, 0x4d00, 256 * 1024, 256, 0) },
	{ "s70fl01gs",  INFO(0x010221, 0x4d00, 256 * 1024, 256, 0) },
	{ "s25sl12800", INFO(0x012018, 0x0300, 256 * 1024,  64, 0) },
	{ "s25sl12801", INFO(0x012018, 0x0301,  64 * 1024, 256, 0) },
	{ "s25fl129p0", INFO(0x012018, 0x4d00, 256 * 1024,  64, 0) },
	{ "s25fl129p1", INFO(0x012018, 0x4d01,  64 * 1024, 256, 0) },
	{ "s25sl004a",  INFO(0x010212,      0,  64 * 1024,   8, 0) },
	{ "s25sl008a",  INFO(0x010213,      0,  64 * 1024,  16, 0) },
	{ "s25sl016a",  INFO(0x010214,      0,  64 * 1024,  32, 0) },
	{ "s25sl032a",  INFO(0x010215,      0,  64 * 1024,  64, 0) },
	{ "s25sl064a",  INFO(0x010216,      0,  64 * 1024, 128, 0) },
	{ "s25fl016k",  INFO(0xef4015,      0,  64 * 1024,  32, SECT_4K) },
	/* s25fl064k supports 4KiB, 32KiB and 64KiB sectors erase size. */
	/* To support JFFS2, the minimum erase size is 8KiB(>4KiB). */
	/* And thus, the sector size of s25fl064k is set to 32KiB for */
	/* JFFS2 support. */
	{ "s25fl064k",  INFO(0xef4017,      0,  64 * 1024, 128, SECT_32K) },

	/* SST -- large erase sizes are "overlays", "sectors" are 4K */
	{ "sst25vf040b", INFO(0xbf258d, 0, 64 * 1024,  8, SECT_4K | SST_WRITE) },
	{ "sst25vf080b", INFO(0xbf258e, 0, 64 * 1024, 16, SECT_4K | SST_WRITE) },
	{ "sst25vf016b", INFO(0xbf2541, 0, 64 * 1024, 32, SECT_4K | SST_WRITE) },
	{ "sst25vf032b", INFO(0xbf254a, 0, 64 * 1024, 64, SECT_4K | SST_WRITE) },
	{ "sst25vf064c", INFO(0xbf254b, 0, 64 * 1024, 128, SECT_4K) },
	{ "sst25wf512", INFO(0xbf2501, 0, 64 * 1024,  1, SECT_4K | SST_WRITE) },
	{ "sst25wf010", INFO(0xbf2502, 0, 64 * 1024,  2, SECT_4K | SST_WRITE) },
	{ "sst25wf020", INFO(0xbf2503, 0, 64 * 1024,  4, SECT_4K | SST_WRITE) },
	{ "sst25wf040", INFO(0xbf2504, 0, 64 * 1024,  8, SECT_4K | SST_WRITE) },
	{ "sst25wf080", INFO(0xbf2505, 0, 64 * 1024, 16, SECT_4K | SST_WRITE) },

	/* ST Microelectronics -- newer production may have feature updates */
	{ "m25p05",  INFO(0x202010,  0,  32 * 1024,   2, 0) },
	{ "m25p10",  INFO(0x202011,  0,  32 * 1024,   4, 0) },
	{ "m25p20",  INFO(0x202012,  0,  64 * 1024,   4, 0) },
	{ "m25p40",  INFO(0x202013,  0,  64 * 1024,   8, 0) },
	{ "m25p80",  INFO(0x202014,  0,  64 * 1024,  16, 0) },
	{ "m25p16",  INFO(0x202015,  0,  64 * 1024,  32, 0) },
	{ "m25p32",  INFO(0x202016,  0,  64 * 1024,  64, 0) },
	{ "m25p64",  INFO(0x202017,  0,  64 * 1024, 128, 0) },
	{ "m25p128", INFO(0x202018,  0, 256 * 1024,  64, 0) },
	{ "n25q032", INFO(0x20ba16,  0,  64 * 1024,  64, 0) },

	{ "m25p05-nonjedec",  INFO(0, 0,  32 * 1024,   2, 0) },
	{ "m25p10-nonjedec",  INFO(0, 0,  32 * 1024,   4, 0) },
	{ "m25p20-nonjedec",  INFO(0, 0,  64 * 1024,   4, 0) },
	{ "m25p40-nonjedec",  INFO(0, 0,  64 * 1024,   8, 0) },
	{ "m25p80-nonjedec",  INFO(0, 0,  64 * 1024,  16, 0) },
	{ "m25p16-nonjedec",  INFO(0, 0,  64 * 1024,  32, 0) },
	{ "m25p32-nonjedec",  INFO(0, 0,  64 * 1024,  64, 0) },
	{ "m25p64-nonjedec",  INFO(0, 0,  64 * 1024, 128, 0) },
	{ "m25p128-nonjedec", INFO(0, 0, 256 * 1024,  64, 0) },

	{ "m45pe10", INFO(0x204011,  0, 64 * 1024,    2, 0) },
	{ "m45pe80", INFO(0x204014,  0, 64 * 1024,   16, 0) },
	{ "m45pe16", INFO(0x204015,  0, 64 * 1024,   32, 0) },

	{ "m25pe20", INFO(0x208012,  0, 64 * 1024,  4,       0) },
	{ "m25pe80", INFO(0x208014,  0, 64 * 1024, 16,       0) },
	{ "m25pe16", INFO(0x208015,  0, 64 * 1024, 32, SECT_4K) },

	{ "m25px32",    INFO(0x207116,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s0", INFO(0x207316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s1", INFO(0x206316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px64",    INFO(0x207117,  0, 64 * 1024, 128, 0) },

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x10", INFO(0xef3011, 0, 64 * 1024,  2,  SECT_4K) },
	{ "w25x20", INFO(0xef3012, 0, 64 * 1024,  4,  SECT_4K) },
	{ "w25x40", INFO(0xef3013, 0, 64 * 1024,  8,  SECT_4K) },
	{ "w25x80", INFO(0xef3014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25x16", INFO(0xef3015, 0, 64 * 1024,  32, SECT_4K) },
	{ "w25x32", INFO(0xef3016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25q32", INFO(0xef4016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25q32dw", INFO(0xef6016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25x64", INFO(0xef3017, 0, 64 * 1024, 128, SECT_4K) },
	/* Winbond -- w25q "blocks" are 64K, "sectors" are 32KiB */
	/* w25q64 supports 4KiB, 32KiB and 64KiB sectors erase size. */
	/* To support JFFS2, the minimum erase size is 8KiB(>4KiB). */
	/* And thus, the sector size of w25q64 is set to 32KiB for */
	/* JFFS2 support. */
	{ "w25q64", INFO(0xef4017, 0, 64 * 1024, 128, SECT_32K) },
	{ "w25q80", INFO(0xef5014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25q80bl", INFO(0xef4014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25q128", INFO(0xef4018, 0, 64 * 1024, 256, SECT_4K) },
	{ "w25q256", INFO(0xef4019, 0, 64 * 1024, 512, SECT_4K) },

	/* Catalyst / On Semiconductor -- non-JEDEC */
	{ "cat25c11", CAT25_INFO(  16, 8, 16, 1) },
	{ "cat25c03", CAT25_INFO(  32, 8, 16, 2) },
	{ "cat25c09", CAT25_INFO( 128, 8, 32, 2) },
	{ "cat25c17", CAT25_INFO( 256, 8, 32, 2) },
	{ "cat25128", CAT25_INFO(2048, 8, 64, 2) },
	{ },
};
MODULE_DEVICE_TABLE(spi, m25p_ids);

static const struct spi_device_id *jedec_probe(struct spi_device *spi)
{
	int			tmp;
	u8			code = OPCODE_RDID;
	u8			id[5];
	u32			jedec;
	u16                     ext_jedec;
	struct flash_info	*info;

	/* JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 */
	tmp = spi_write_then_read(spi, &code, 1, id, 5);
	if (tmp < 0) {
		pr_debug("%s: error %d reading JEDEC ID\n",
				dev_name(&spi->dev), tmp);
		return ERR_PTR(tmp);
	}
	jedec = id[0];
	jedec = jedec << 8;
	jedec |= id[1];
	jedec = jedec << 8;
	jedec |= id[2];

	ext_jedec = id[3] << 8 | id[4];

	for (tmp = 0; tmp < ARRAY_SIZE(m25p_ids) - 1; tmp++) {
		info = (void *)m25p_ids[tmp].driver_data;
		if (info->jedec_id == jedec) {
			if (info->ext_id != 0 && info->ext_id != ext_jedec)
				continue;
			return &m25p_ids[tmp];
		}
	}
	dev_err(&spi->dev, "unrecognized JEDEC id %06x\n", jedec);
	return ERR_PTR(-ENODEV);
}


/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
static int m25p_probe(struct spi_device *spi)
{
	const struct spi_device_id	*id = spi_get_device_id(spi);
	struct flash_platform_data	*data;
	struct m25p			*flash;
	struct flash_info		*info;
	unsigned			i;
	struct mtd_part_parser_data	ppdata;
	struct device_node __maybe_unused *np = spi->dev.of_node;

#ifdef CONFIG_MTD_OF_PARTS
	if (!of_device_is_available(np))
		return -ENODEV;
#endif

	/* Platform data helps sort out which chip type we have, as
	 * well as how this board partitions it.  If we don't have
	 * a chip ID, try the JEDEC id commands; they'll work for most
	 * newer chips, even if we don't recognize the particular chip.
	 */
	data = spi->dev.platform_data;
	if (data && data->type) {
		const struct spi_device_id *plat_id;

		for (i = 0; i < ARRAY_SIZE(m25p_ids) - 1; i++) {
			plat_id = &m25p_ids[i];
			if (strcmp(data->type, plat_id->name))
				continue;
			break;
		}

		if (i < ARRAY_SIZE(m25p_ids) - 1)
			id = plat_id;
		else
			dev_warn(&spi->dev, "unrecognized id %s\n", data->type);
	}

	info = (void *)id->driver_data;

	if (info->jedec_id) {
		const struct spi_device_id *jid;

		jid = jedec_probe(spi);
		if (IS_ERR(jid)) {
			return PTR_ERR(jid);
		} else if (jid != id) {
			/*
			 * JEDEC knows better, so overwrite platform ID. We
			 * can't trust partitions any longer, but we'll let
			 * mtd apply them anyway, since some partitions may be
			 * marked read-only, and we don't want to lose that
			 * information, even if it's not 100% accurate.
			 */
			dev_warn(&spi->dev, "found %s, expected %s\n",
				 jid->name, id->name);
			id = jid;
			info = (void *)jid->driver_data;
		}
	}

	flash = kzalloc(sizeof *flash, GFP_KERNEL);
	if (!flash)
		return -ENOMEM;
	flash->command = kmalloc(MAX_CMD_SIZE + (flash->fast_read ? 1 : 0),
					GFP_KERNEL);
	if (!flash->command) {
		kfree(flash);
		return -ENOMEM;
	}

	flash->info = info;
	flash->spi = spi;
	mutex_init(&flash->lock);
	dev_set_drvdata(&spi->dev, flash);

	/*
	 * Atmel, SST and Intel/Numonyx serial flash tend to power
	 * up with the software protection bits set
	 */

	if (JEDEC_MFR(info->jedec_id) == CFI_MFR_ATMEL ||
	    JEDEC_MFR(info->jedec_id) == CFI_MFR_INTEL ||
	    JEDEC_MFR(info->jedec_id) == CFI_MFR_SST) {
		write_enable(flash);
		write_sr(flash, 0);
	}

	if (data && data->name)
		flash->mtd.name = data->name;
	else
		flash->mtd.name = dev_name(&spi->dev);

	flash->mtd.type = MTD_NORFLASH;
	flash->mtd.writesize = 1;
	flash->mtd.flags = MTD_CAP_NORFLASH;
	flash->mtd.size = info->sector_size * info->n_sectors;

	{
#ifdef CONFIG_OF
		const char *comp_str;
		u32 is_dual;
		np = of_get_next_parent(spi->dev.of_node);
		of_property_read_string(np, "compatible", &comp_str);
		if (!strcmp(comp_str, "xlnx,ps7-qspi-1.00.a")) {
			if (of_property_read_u32(np, "is-dual", &is_dual) < 0) {
				/* Default to single if prop not defined */
				flash->shift = 0;
				flash->isstacked = 0;
				flash->isparallel = 0;
			} else {
				if (is_dual == 1) {
					/* dual parallel */
					flash->shift = 1;
					info->sector_size <<= flash->shift;
					info->page_size <<= flash->shift;
					flash->mtd.size <<= flash->shift;
					flash->isparallel = 1;
					flash->isstacked = 0;
				} else {
#ifdef CONFIG_SPI_XILINX_PS_QSPI_DUAL_STACKED
					/* dual stacked */
					flash->shift = 0;
					flash->mtd.size <<= 1;
					flash->isstacked = 1;
					flash->isparallel = 0;
#else
					/* single */
					flash->shift = 0;
					flash->isstacked = 0;
					flash->isparallel = 0;
#endif
				}
			}
		}
#else
		/* Default to single */
		flash->shift = 0;
		flash->isstacked = 0;
		flash->isparallel = 0;
#endif
	}

	flash->mtd._erase = m25p80_erase;
	flash->mtd._read = m25p80_read_ext;

	/* flash protection support for STmicro chips */
	if (JEDEC_MFR(info->jedec_id) == CFI_MFR_ST) {
		flash->mtd._lock = m25p80_lock;
		flash->mtd._unlock = m25p80_unlock;
	}

	/* sst flash chips use AAI word program */
	if (info->flags & SST_WRITE)
		flash->mtd._write = sst_write;
	else
		flash->mtd._write = m25p80_write_ext;

	/* prefer "small sector" erase if possible */
	if (info->flags & SECT_4K) {
		flash->erase_opcode = OPCODE_BE_4K;
		flash->mtd.erasesize = 4096 << flash->shift;
	} else if (info->flags & SECT_32K) {
		flash->erase_opcode = OPCODE_BE_32K;
		flash->mtd.erasesize = 32768 << flash->shift;
	} else {
		flash->erase_opcode = OPCODE_SE;
		flash->mtd.erasesize = info->sector_size;
	}

	flash->read_opcode = OPCODE_NORM_READ;
	flash->prog_opcode = OPCODE_PP;
	flash->dummycount = 0;

	if (info->flags & M25P_NO_ERASE)
		flash->mtd.flags |= MTD_NO_ERASE;

	if (info->flags & USE_FSR)
		flash->wait_till_ready = &_wait_till_fsr_ready;
	else
		flash->wait_till_ready = &_wait_till_ready;


	ppdata.of_node = spi->dev.of_node;
	flash->mtd.dev.parent = &spi->dev;
	flash->page_size = info->page_size;
	flash->mtd.writebufsize = flash->page_size;

	flash->fast_read = false;
#ifdef CONFIG_OF
	if (np && of_property_read_bool(np, "m25p,fast-read"))
		flash->fast_read = true;
#endif

#ifdef CONFIG_M25PXX_USE_FAST_READ
	flash->fast_read = true;
#endif
	if (flash->fast_read) {
		flash->read_opcode = OPCODE_FAST_READ;
		flash->dummycount = 1;
	}

	if (spi->master->flags & SPI_MASTER_QUAD_MODE) {
		flash->read_opcode = OPCODE_QUAD_READ;
		flash->prog_opcode = OPCODE_QPP;
		flash->dummycount = 1;
	}

	if (info->addr_width)
		flash->addr_width = info->addr_width;
	else {
		/* enable 4-byte addressing if the device exceeds 16MiB */
		if (flash->mtd.size > 0x1000000) {
			flash->addr_width = 4;
			write_enable(flash);
			set_4byte(flash, info->jedec_id, 1);
		} else
			flash->addr_width = 3;
	}

	/* set up the VCR for 8 dummy cycles (instead of the default 15) */
#define N25Q00_JEDEC_ID (0x20ba21)
	if (info->jedec_id == N25Q00_JEDEC_ID) {
		int vcr = read_vcr(flash);
		if (vcr >= 0) {
			vcr &= ~(VCR_DUMMY_CLK_CYCLES_MASK | VCR_XIP_MASK);
			vcr |= (8 << VCR_DUMMY_CLK_CYCLES_SHIFT);
			write_enable(flash);
			write_vcr(flash, vcr);
		}
	}

	spi->addr_width = flash->addr_width;

	dev_info(&spi->dev, "%s (%lld Kbytes)\n", id->name,
			(long long)flash->mtd.size >> 10);

	pr_debug("mtd .name = %s, .size = 0x%llx (%lldMiB) "
			".erasesize = 0x%.8x (%uKiB) .numeraseregions = %d\n",
		flash->mtd.name,
		(long long)flash->mtd.size, (long long)(flash->mtd.size >> 20),
		flash->mtd.erasesize, flash->mtd.erasesize / 1024,
		flash->mtd.numeraseregions);

	if (flash->mtd.numeraseregions)
		for (i = 0; i < flash->mtd.numeraseregions; i++)
			pr_debug("mtd.eraseregions[%d] = { .offset = 0x%llx, "
				".erasesize = 0x%.8x (%uKiB), "
				".numblocks = %d }\n",
				i, (long long)flash->mtd.eraseregions[i].offset,
				flash->mtd.eraseregions[i].erasesize,
				flash->mtd.eraseregions[i].erasesize / 1024,
				flash->mtd.eraseregions[i].numblocks);


	/* partitions should match sector boundaries; and it may be good to
	 * use readonly partitions for writeprotected sectors (BP2..BP0).
	 */
	return mtd_device_parse_register(&flash->mtd, NULL, &ppdata,
			data ? data->parts : NULL,
			data ? data->nr_parts : 0);
}


static int m25p_remove(struct spi_device *spi)
{
	struct m25p	*flash = dev_get_drvdata(&spi->dev);
	int		status;

	/* Clean up MTD stuff. */
	status = mtd_device_unregister(&flash->mtd);
	if (status == 0) {
		kfree(flash->command);
		kfree(flash);
	}
	return 0;
}

static void m25p_shutdown(struct spi_device *spi)
{
	struct m25p	*flash = dev_get_drvdata(&spi->dev);
	if (flash->info->flags & SHUTDOWN_3BYTE)
		set_4byte(flash, flash->info->jedec_id, 0);
}

static struct spi_driver m25p80_driver = {
	.driver = {
		.name	= "m25p80",
		.owner	= THIS_MODULE,
	},
	.id_table	= m25p_ids,
	.probe	= m25p_probe,
	.remove	= m25p_remove,
	.shutdown = m25p_shutdown,

	/* REVISIT: many of these chips have deep power-down modes, which
	 * should clearly be entered on suspend() to minimize power use.
	 * And also when they're otherwise idle...
	 */
};

module_spi_driver(m25p80_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("MTD SPI driver for ST M25Pxx flash chips");

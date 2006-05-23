/*
 *  linux/drivers/mtd/onenand/onenand_base.c
 *
 *  Copyright (C) 2005 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

/**
 * onenand_oob_64 - oob info for large (2KB) page
 */
static struct nand_oobinfo onenand_oob_64 = {
	.useecc		= MTD_NANDECC_AUTOPLACE,
	.eccbytes	= 20,
	.eccpos		= {
		8, 9, 10, 11, 12,
		24, 25, 26, 27, 28,
		40, 41, 42, 43, 44,
		56, 57, 58, 59, 60,
		},
	.oobfree	= {
		{2, 3}, {14, 2}, {18, 3}, {30, 2},
		{34, 3}, {46, 2}, {50, 3}, {62, 2}
	}
};

/**
 * onenand_oob_32 - oob info for middle (1KB) page
 */
static struct nand_oobinfo onenand_oob_32 = {
	.useecc		= MTD_NANDECC_AUTOPLACE,
	.eccbytes	= 10,
	.eccpos		= {
		8, 9, 10, 11, 12,
		24, 25, 26, 27, 28,
		},
	.oobfree	= { {2, 3}, {14, 2}, {18, 3}, {30, 2} }
};

static const unsigned char ffchars[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* 16 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* 32 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* 48 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* 64 */
};

/**
 * onenand_readw - [OneNAND Interface] Read OneNAND register
 * @param addr		address to read
 *
 * Read OneNAND register
 */
static unsigned short onenand_readw(void __iomem *addr)
{
	return readw(addr);
}

/**
 * onenand_writew - [OneNAND Interface] Write OneNAND register with value
 * @param value		value to write
 * @param addr		address to write
 *
 * Write OneNAND register with value
 */
static void onenand_writew(unsigned short value, void __iomem *addr)
{
	writew(value, addr);
}

/**
 * onenand_block_address - [DEFAULT] Get block address
 * @param this		onenand chip data structure
 * @param block		the block
 * @return		translated block address if DDP, otherwise same
 *
 * Setup Start Address 1 Register (F100h)
 */
static int onenand_block_address(struct onenand_chip *this, int block)
{
	if (this->device_id & ONENAND_DEVICE_IS_DDP) {
		/* Device Flash Core select, NAND Flash Block Address */
		int dfs = 0;

		if (block & this->density_mask)
			dfs = 1;

		return (dfs << ONENAND_DDP_SHIFT) |
			(block & (this->density_mask - 1));
	}

	return block;
}

/**
 * onenand_bufferram_address - [DEFAULT] Get bufferram address
 * @param this		onenand chip data structure
 * @param block		the block
 * @return		set DBS value if DDP, otherwise 0
 *
 * Setup Start Address 2 Register (F101h) for DDP
 */
static int onenand_bufferram_address(struct onenand_chip *this, int block)
{
	if (this->device_id & ONENAND_DEVICE_IS_DDP) {
		/* Device BufferRAM Select */
		int dbs = 0;

		if (block & this->density_mask)
			dbs = 1;

		return (dbs << ONENAND_DDP_SHIFT);
	}

	return 0;
}

/**
 * onenand_page_address - [DEFAULT] Get page address
 * @param page		the page address
 * @param sector	the sector address
 * @return		combined page and sector address
 *
 * Setup Start Address 8 Register (F107h)
 */
static int onenand_page_address(int page, int sector)
{
	/* Flash Page Address, Flash Sector Address */
	int fpa, fsa;

	fpa = page & ONENAND_FPA_MASK;
	fsa = sector & ONENAND_FSA_MASK;

	return ((fpa << ONENAND_FPA_SHIFT) | fsa);
}

/**
 * onenand_buffer_address - [DEFAULT] Get buffer address
 * @param dataram1	DataRAM index
 * @param sectors	the sector address
 * @param count		the number of sectors
 * @return		the start buffer value
 *
 * Setup Start Buffer Register (F200h)
 */
static int onenand_buffer_address(int dataram1, int sectors, int count)
{
	int bsa, bsc;

	/* BufferRAM Sector Address */
	bsa = sectors & ONENAND_BSA_MASK;

	if (dataram1)
		bsa |= ONENAND_BSA_DATARAM1;	/* DataRAM1 */
	else
		bsa |= ONENAND_BSA_DATARAM0;	/* DataRAM0 */

	/* BufferRAM Sector Count */
	bsc = count & ONENAND_BSC_MASK;

	return ((bsa << ONENAND_BSA_SHIFT) | bsc);
}

/**
 * onenand_command - [DEFAULT] Send command to OneNAND device
 * @param mtd		MTD device structure
 * @param cmd		the command to be sent
 * @param addr		offset to read from or write to
 * @param len		number of bytes to read or write
 *
 * Send command to OneNAND device. This function is used for middle/large page
 * devices (1KB/2KB Bytes per page)
 */
static int onenand_command(struct mtd_info *mtd, int cmd, loff_t addr, size_t len)
{
	struct onenand_chip *this = mtd->priv;
	int value, readcmd = 0, block_cmd = 0;
	int block, page;
	/* Now we use page size operation */
	int sectors = 4, count = 4;

	/* Address translation */
	switch (cmd) {
	case ONENAND_CMD_UNLOCK:
	case ONENAND_CMD_LOCK:
	case ONENAND_CMD_LOCK_TIGHT:
		block = -1;
		page = -1;
		break;

	case ONENAND_CMD_ERASE:
	case ONENAND_CMD_BUFFERRAM:
	case ONENAND_CMD_OTP_ACCESS:
		block_cmd = 1;
		block = (int) (addr >> this->erase_shift);
		page = -1;
		break;

	default:
		block = (int) (addr >> this->erase_shift);
		page = (int) (addr >> this->page_shift);
		page &= this->page_mask;
		break;
	}

	/* NOTE: The setting order of the registers is very important! */
	if (cmd == ONENAND_CMD_BUFFERRAM) {
		/* Select DataRAM for DDP */
		value = onenand_bufferram_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);

		/* Switch to the next data buffer */
		ONENAND_SET_NEXT_BUFFERRAM(this);

		return 0;
	}

	if (block != -1) {
		/* Write 'DFS, FBA' of Flash */
		value = onenand_block_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS1);

		if (block_cmd) {
			/* Select DataRAM for DDP */
			value = onenand_bufferram_address(this, block);
			this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);
		}
	}

	if (page != -1) {
		int dataram;

		switch (cmd) {
		case ONENAND_CMD_READ:
		case ONENAND_CMD_READOOB:
			dataram = ONENAND_SET_NEXT_BUFFERRAM(this);
			readcmd = 1;
			break;

		default:
			dataram = ONENAND_CURRENT_BUFFERRAM(this);
			break;
		}

		/* Write 'FPA, FSA' of Flash */
		value = onenand_page_address(page, sectors);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS8);

		/* Write 'BSA, BSC' of DataRAM */
		value = onenand_buffer_address(dataram, sectors, count);
		this->write_word(value, this->base + ONENAND_REG_START_BUFFER);

		if (readcmd) {
			/* Select DataRAM for DDP */
			value = onenand_bufferram_address(this, block);
			this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);
		}
	}

	/* Interrupt clear */
	this->write_word(ONENAND_INT_CLEAR, this->base + ONENAND_REG_INTERRUPT);

	/* Write command */
	this->write_word(cmd, this->base + ONENAND_REG_COMMAND);

	return 0;
}

/**
 * onenand_wait - [DEFAULT] wait until the command is done
 * @param mtd		MTD device structure
 * @param state		state to select the max. timeout value
 *
 * Wait for command done. This applies to all OneNAND command
 * Read can take up to 30us, erase up to 2ms and program up to 350us
 * according to general OneNAND specs
 */
static int onenand_wait(struct mtd_info *mtd, int state)
{
	struct onenand_chip * this = mtd->priv;
	unsigned long timeout;
	unsigned int flags = ONENAND_INT_MASTER;
	unsigned int interrupt = 0;
	unsigned int ctrl, ecc;

	/* The 20 msec is enough */
	timeout = jiffies + msecs_to_jiffies(20);
	while (time_before(jiffies, timeout)) {
		interrupt = this->read_word(this->base + ONENAND_REG_INTERRUPT);

		if (interrupt & flags)
			break;

		if (state != FL_READING)
			cond_resched();
		touch_softlockup_watchdog();
	}
	/* To get correct interrupt status in timeout case */
	interrupt = this->read_word(this->base + ONENAND_REG_INTERRUPT);

	ctrl = this->read_word(this->base + ONENAND_REG_CTRL_STATUS);

	if (ctrl & ONENAND_CTRL_ERROR) {
		/* It maybe occur at initial bad block */
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_wait: controller error = 0x%04x\n", ctrl);
		/* Clear other interrupt bits for preventing ECC error */
		interrupt &= ONENAND_INT_MASTER;
	}

	if (ctrl & ONENAND_CTRL_LOCK) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_wait: it's locked error = 0x%04x\n", ctrl);
		return -EACCES;
	}

	if (interrupt & ONENAND_INT_READ) {
		ecc = this->read_word(this->base + ONENAND_REG_ECC_STATUS);
		if (ecc & ONENAND_ECC_2BIT_ALL) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_wait: ECC error = 0x%04x\n", ecc);
			return -EBADMSG;
		}
	}

	return 0;
}

/**
 * onenand_bufferram_offset - [DEFAULT] BufferRAM offset
 * @param mtd		MTD data structure
 * @param area		BufferRAM area
 * @return		offset given area
 *
 * Return BufferRAM offset given area
 */
static inline int onenand_bufferram_offset(struct mtd_info *mtd, int area)
{
	struct onenand_chip *this = mtd->priv;

	if (ONENAND_CURRENT_BUFFERRAM(this)) {
		if (area == ONENAND_DATARAM)
			return mtd->writesize;
		if (area == ONENAND_SPARERAM)
			return mtd->oobsize;
	}

	return 0;
}

/**
 * onenand_read_bufferram - [OneNAND Interface] Read the bufferram area
 * @param mtd		MTD data structure
 * @param area		BufferRAM area
 * @param buffer	the databuffer to put/get data
 * @param offset	offset to read from or write to
 * @param count		number of bytes to read/write
 *
 * Read the BufferRAM area
 */
static int onenand_read_bufferram(struct mtd_info *mtd, int area,
		unsigned char *buffer, int offset, size_t count)
{
	struct onenand_chip *this = mtd->priv;
	void __iomem *bufferram;

	bufferram = this->base + area;

	bufferram += onenand_bufferram_offset(mtd, area);

	if (ONENAND_CHECK_BYTE_ACCESS(count)) {
		unsigned short word;

		/* Align with word(16-bit) size */
		count--;

		/* Read word and save byte */
		word = this->read_word(bufferram + offset + count);
		buffer[count] = (word & 0xff);
	}

	memcpy(buffer, bufferram + offset, count);

	return 0;
}

/**
 * onenand_sync_read_bufferram - [OneNAND Interface] Read the bufferram area with Sync. Burst mode
 * @param mtd		MTD data structure
 * @param area		BufferRAM area
 * @param buffer	the databuffer to put/get data
 * @param offset	offset to read from or write to
 * @param count		number of bytes to read/write
 *
 * Read the BufferRAM area with Sync. Burst Mode
 */
static int onenand_sync_read_bufferram(struct mtd_info *mtd, int area,
		unsigned char *buffer, int offset, size_t count)
{
	struct onenand_chip *this = mtd->priv;
	void __iomem *bufferram;

	bufferram = this->base + area;

	bufferram += onenand_bufferram_offset(mtd, area);

	this->mmcontrol(mtd, ONENAND_SYS_CFG1_SYNC_READ);

	if (ONENAND_CHECK_BYTE_ACCESS(count)) {
		unsigned short word;

		/* Align with word(16-bit) size */
		count--;

		/* Read word and save byte */
		word = this->read_word(bufferram + offset + count);
		buffer[count] = (word & 0xff);
	}

	memcpy(buffer, bufferram + offset, count);

	this->mmcontrol(mtd, 0);

	return 0;
}

/**
 * onenand_write_bufferram - [OneNAND Interface] Write the bufferram area
 * @param mtd		MTD data structure
 * @param area		BufferRAM area
 * @param buffer	the databuffer to put/get data
 * @param offset	offset to read from or write to
 * @param count		number of bytes to read/write
 *
 * Write the BufferRAM area
 */
static int onenand_write_bufferram(struct mtd_info *mtd, int area,
		const unsigned char *buffer, int offset, size_t count)
{
	struct onenand_chip *this = mtd->priv;
	void __iomem *bufferram;

	bufferram = this->base + area;

	bufferram += onenand_bufferram_offset(mtd, area);

	if (ONENAND_CHECK_BYTE_ACCESS(count)) {
		unsigned short word;
		int byte_offset;

		/* Align with word(16-bit) size */
		count--;

		/* Calculate byte access offset */
		byte_offset = offset + count;

		/* Read word and save byte */
		word = this->read_word(bufferram + byte_offset);
		word = (word & ~0xff) | buffer[count];
		this->write_word(word, bufferram + byte_offset);
	}

	memcpy(bufferram + offset, buffer, count);

	return 0;
}

/**
 * onenand_check_bufferram - [GENERIC] Check BufferRAM information
 * @param mtd		MTD data structure
 * @param addr		address to check
 * @return		1 if there are valid data, otherwise 0
 *
 * Check bufferram if there is data we required
 */
static int onenand_check_bufferram(struct mtd_info *mtd, loff_t addr)
{
	struct onenand_chip *this = mtd->priv;
	int block, page;
	int i;

	block = (int) (addr >> this->erase_shift);
	page = (int) (addr >> this->page_shift);
	page &= this->page_mask;

	i = ONENAND_CURRENT_BUFFERRAM(this);

	/* Is there valid data? */
	if (this->bufferram[i].block == block &&
	    this->bufferram[i].page == page &&
	    this->bufferram[i].valid)
		return 1;

	return 0;
}

/**
 * onenand_update_bufferram - [GENERIC] Update BufferRAM information
 * @param mtd		MTD data structure
 * @param addr		address to update
 * @param valid		valid flag
 *
 * Update BufferRAM information
 */
static int onenand_update_bufferram(struct mtd_info *mtd, loff_t addr,
		int valid)
{
	struct onenand_chip *this = mtd->priv;
	int block, page;
	int i;

	block = (int) (addr >> this->erase_shift);
	page = (int) (addr >> this->page_shift);
	page &= this->page_mask;

	/* Invalidate BufferRAM */
	for (i = 0; i < MAX_BUFFERRAM; i++) {
		if (this->bufferram[i].block == block &&
		    this->bufferram[i].page == page)
			this->bufferram[i].valid = 0;
	}

	/* Update BufferRAM */
	i = ONENAND_CURRENT_BUFFERRAM(this);
	this->bufferram[i].block = block;
	this->bufferram[i].page = page;
	this->bufferram[i].valid = valid;

	return 0;
}

/**
 * onenand_get_device - [GENERIC] Get chip for selected access
 * @param mtd		MTD device structure
 * @param new_state	the state which is requested
 *
 * Get the device and lock it for exclusive access
 */
static int onenand_get_device(struct mtd_info *mtd, int new_state)
{
	struct onenand_chip *this = mtd->priv;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 * Grab the lock and see if the device is available
	 */
	while (1) {
		spin_lock(&this->chip_lock);
		if (this->state == FL_READY) {
			this->state = new_state;
			spin_unlock(&this->chip_lock);
			break;
		}
		if (new_state == FL_PM_SUSPENDED) {
			spin_unlock(&this->chip_lock);
			return (this->state == FL_PM_SUSPENDED) ? 0 : -EAGAIN;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&this->wq, &wait);
		spin_unlock(&this->chip_lock);
		schedule();
		remove_wait_queue(&this->wq, &wait);
	}

	return 0;
}

/**
 * onenand_release_device - [GENERIC] release chip
 * @param mtd		MTD device structure
 *
 * Deselect, release chip lock and wake up anyone waiting on the device
 */
static void onenand_release_device(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;

	/* Release the chip */
	spin_lock(&this->chip_lock);
	this->state = FL_READY;
	wake_up(&this->wq);
	spin_unlock(&this->chip_lock);
}

/**
 * onenand_read - [MTD Interface] Read data from flash
 * @param mtd		MTD device structure
 * @param from		offset to read from
 * @param len		number of bytes to read
 * @param retlen	pointer to variable to store the number of read bytes
 * @param buf		the databuffer to put data
 *
 * Read with ecc
*/
static int onenand_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	int read = 0, column;
	int thislen;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_read: from = 0x%08x, len = %i\n", (unsigned int) from, (int) len);

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_read: Attempt read beyond end of device\n");
		*retlen = 0;
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_READING);

	/* TODO handling oob */

	while (read < len) {
		thislen = min_t(int, mtd->writesize, len - read);

		column = from & (mtd->writesize - 1);
		if (column + thislen > mtd->writesize)
			thislen = mtd->writesize - column;

		if (!onenand_check_bufferram(mtd, from)) {
			this->command(mtd, ONENAND_CMD_READ, from, mtd->writesize);

			ret = this->wait(mtd, FL_READING);
			/* First copy data and check return value for ECC handling */
			onenand_update_bufferram(mtd, from, 1);
		}

		this->read_bufferram(mtd, ONENAND_DATARAM, buf, column, thislen);

		read += thislen;

		if (read == len)
			break;

		if (ret) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_read: read failed = %d\n", ret);
			goto out;
		}

		from += thislen;
		buf += thislen;
	}

out:
	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	/*
	 * Return success, if no ECC failures, else -EBADMSG
	 * fs driver will take care of that, because
	 * retlen == desired len and result == -EBADMSG
	 */
	*retlen = read;
	return ret;
}

/**
 * onenand_read_oob - [MTD Interface] OneNAND read out-of-band
 * @param mtd		MTD device structure
 * @param from		offset to read from
 * @param len		number of bytes to read
 * @param retlen	pointer to variable to store the number of read bytes
 * @param buf		the databuffer to put data
 *
 * OneNAND read out-of-band data from the spare area
 */
static int onenand_read_oob(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	int read = 0, thislen, column;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_read_oob: from = 0x%08x, len = %i\n", (unsigned int) from, (int) len);

	/* Initialize return length value */
	*retlen = 0;

	/* Do not allow reads past end of device */
	if (unlikely((from + len) > mtd->size)) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_read_oob: Attempt read beyond end of device\n");
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_READING);

	column = from & (mtd->oobsize - 1);

	while (read < len) {
		thislen = mtd->oobsize - column;
		thislen = min_t(int, thislen, len);

		this->command(mtd, ONENAND_CMD_READOOB, from, mtd->oobsize);

		onenand_update_bufferram(mtd, from, 0);

		ret = this->wait(mtd, FL_READING);
		/* First copy data and check return value for ECC handling */

		this->read_bufferram(mtd, ONENAND_SPARERAM, buf, column, thislen);

		read += thislen;

		if (read == len)
			break;

		if (ret) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_read_oob: read failed = %d\n", ret);
			goto out;
		}

		buf += thislen;

		/* Read more? */
		if (read < len) {
			/* Page size */
			from += mtd->writesize;
			column = 0;
		}
	}

out:
	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	*retlen = read;
	return ret;
}

#ifdef CONFIG_MTD_ONENAND_VERIFY_WRITE
/**
 * onenand_verify_oob - [GENERIC] verify the oob contents after a write
 * @param mtd		MTD device structure
 * @param buf		the databuffer to verify
 * @param to		offset to read from
 * @param len		number of bytes to read and compare
 *
 */
static int onenand_verify_oob(struct mtd_info *mtd, const u_char *buf, loff_t to, int len)
{
	struct onenand_chip *this = mtd->priv;
	char *readp = this->page_buf;
	int column = to & (mtd->oobsize - 1);
	int status, i;

	this->command(mtd, ONENAND_CMD_READOOB, to, mtd->oobsize);
	onenand_update_bufferram(mtd, to, 0);
	status = this->wait(mtd, FL_READING);
	if (status)
		return status;

	this->read_bufferram(mtd, ONENAND_SPARERAM, readp, column, len);

	for(i = 0; i < len; i++)
		if (buf[i] != 0xFF && buf[i] != readp[i])
			return -EBADMSG;

	return 0;
}

/**
 * onenand_verify_page - [GENERIC] verify the chip contents after a write
 * @param mtd		MTD device structure
 * @param buf		the databuffer to verify
 *
 * Check DataRAM area directly
 */
static int onenand_verify_page(struct mtd_info *mtd, u_char *buf, loff_t addr)
{
	struct onenand_chip *this = mtd->priv;
	void __iomem *dataram0, *dataram1;
	int ret = 0;

	this->command(mtd, ONENAND_CMD_READ, addr, mtd->writesize);

	ret = this->wait(mtd, FL_READING);
	if (ret)
		return ret;

	onenand_update_bufferram(mtd, addr, 1);

	/* Check, if the two dataram areas are same */
	dataram0 = this->base + ONENAND_DATARAM;
	dataram1 = dataram0 + mtd->writesize;

	if (memcmp(dataram0, dataram1, mtd->writesize))
		return -EBADMSG;

	return 0;
}
#else
#define onenand_verify_page(...)	(0)
#define onenand_verify_oob(...)		(0)
#endif

#define NOTALIGNED(x)	((x & (mtd->writesize - 1)) != 0)

/**
 * onenand_write - [MTD Interface] write buffer to FLASH
 * @param mtd		MTD device structure
 * @param to		offset to write to
 * @param len		number of bytes to write
 * @param retlen	pointer to variable to store the number of written bytes
 * @param buf		the data to write
 *
 * Write with ECC
 */
static int onenand_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	int written = 0;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_write: to = 0x%08x, len = %i\n", (unsigned int) to, (int) len);

	/* Initialize retlen, in case of early exit */
	*retlen = 0;

	/* Do not allow writes past end of device */
	if (unlikely((to + len) > mtd->size)) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_write: Attempt write to past end of device\n");
		return -EINVAL;
	}

	/* Reject writes, which are not page aligned */
        if (unlikely(NOTALIGNED(to)) || unlikely(NOTALIGNED(len))) {
                DEBUG(MTD_DEBUG_LEVEL0, "onenand_write: Attempt to write not page aligned data\n");
                return -EINVAL;
        }

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_WRITING);

	/* Loop until all data write */
	while (written < len) {
		int thislen = min_t(int, mtd->writesize, len - written);

		this->command(mtd, ONENAND_CMD_BUFFERRAM, to, mtd->writesize);

		this->write_bufferram(mtd, ONENAND_DATARAM, buf, 0, thislen);
		this->write_bufferram(mtd, ONENAND_SPARERAM, ffchars, 0, mtd->oobsize);

		this->command(mtd, ONENAND_CMD_PROG, to, mtd->writesize);

		onenand_update_bufferram(mtd, to, 1);

		ret = this->wait(mtd, FL_WRITING);
		if (ret) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_write: write filaed %d\n", ret);
			goto out;
		}

		written += thislen;

		/* Only check verify write turn on */
		ret = onenand_verify_page(mtd, (u_char *) buf, to);
		if (ret) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_write: verify failed %d\n", ret);
			goto out;
		}

		if (written == len)
			break;

		to += thislen;
		buf += thislen;
	}

out:
	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	*retlen = written;

	return ret;
}

/**
 * onenand_write_oob - [MTD Interface] OneNAND write out-of-band
 * @param mtd		MTD device structure
 * @param to		offset to write to
 * @param len		number of bytes to write
 * @param retlen	pointer to variable to store the number of written bytes
 * @param buf		the data to write
 *
 * OneNAND write out-of-band
 */
static int onenand_write_oob(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	int column, ret = 0;
	int written = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_write_oob: to = 0x%08x, len = %i\n", (unsigned int) to, (int) len);

	/* Initialize retlen, in case of early exit */
	*retlen = 0;

	/* Do not allow writes past end of device */
	if (unlikely((to + len) > mtd->size)) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_write_oob: Attempt write to past end of device\n");
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_WRITING);

	/* Loop until all data write */
	while (written < len) {
		int thislen = min_t(int, mtd->oobsize, len - written);

		column = to & (mtd->oobsize - 1);

		this->command(mtd, ONENAND_CMD_BUFFERRAM, to, mtd->oobsize);

		/* We send data to spare ram with oobsize
		 * to prevent byte access */
		memset(this->page_buf, 0xff, mtd->oobsize);
		memcpy(this->page_buf + column, buf, thislen);
		this->write_bufferram(mtd, ONENAND_SPARERAM, this->page_buf, 0, mtd->oobsize);

		this->command(mtd, ONENAND_CMD_PROGOOB, to, mtd->oobsize);

		onenand_update_bufferram(mtd, to, 0);

		ret = this->wait(mtd, FL_WRITING);
		if (ret) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_write_oob: write filaed %d\n", ret);
			goto out;
		}

		ret = onenand_verify_oob(mtd, buf, to, thislen);
		if (ret) {
			DEBUG(MTD_DEBUG_LEVEL0, "onenand_write_oob: verify failed %d\n", ret);
			goto out;
		}

		written += thislen;

		if (written == len)
			break;

		to += thislen;
		buf += thislen;
	}

out:
	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	*retlen = written;

	return ret;
}

/**
 * onenand_block_checkbad - [GENERIC] Check if a block is marked bad
 * @param mtd		MTD device structure
 * @param ofs		offset from device start
 * @param getchip	0, if the chip is already selected
 * @param allowbbt	1, if its allowed to access the bbt area
 *
 * Check, if the block is bad. Either by reading the bad block table or
 * calling of the scan function.
 */
static int onenand_block_checkbad(struct mtd_info *mtd, loff_t ofs, int getchip, int allowbbt)
{
	struct onenand_chip *this = mtd->priv;
	struct bbm_info *bbm = this->bbm;

	/* Return info from the table */
	return bbm->isbad_bbt(mtd, ofs, allowbbt);
}

/**
 * onenand_erase - [MTD Interface] erase block(s)
 * @param mtd		MTD device structure
 * @param instr		erase instruction
 *
 * Erase one ore more blocks
 */
static int onenand_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct onenand_chip *this = mtd->priv;
	unsigned int block_size;
	loff_t addr;
	int len;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_erase: start = 0x%08x, len = %i\n", (unsigned int) instr->addr, (unsigned int) instr->len);

	block_size = (1 << this->erase_shift);

	/* Start address must align on block boundary */
	if (unlikely(instr->addr & (block_size - 1))) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_erase: Unaligned address\n");
		return -EINVAL;
	}

	/* Length must align on block boundary */
	if (unlikely(instr->len & (block_size - 1))) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_erase: Length not block aligned\n");
		return -EINVAL;
	}

	/* Do not allow erase past end of device */
	if (unlikely((instr->len + instr->addr) > mtd->size)) {
		DEBUG(MTD_DEBUG_LEVEL0, "onenand_erase: Erase past end of device\n");
		return -EINVAL;
	}

	instr->fail_addr = 0xffffffff;

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_ERASING);

	/* Loop throught the pages */
	len = instr->len;
	addr = instr->addr;

	instr->state = MTD_ERASING;

	while (len) {

		/* Check if we have a bad block, we do not erase bad blocks */
		if (onenand_block_checkbad(mtd, addr, 0, 0)) {
			printk (KERN_WARNING "onenand_erase: attempt to erase a bad block at addr 0x%08x\n", (unsigned int) addr);
			instr->state = MTD_ERASE_FAILED;
			goto erase_exit;
		}

		this->command(mtd, ONENAND_CMD_ERASE, addr, block_size);

		ret = this->wait(mtd, FL_ERASING);
		/* Check, if it is write protected */
		if (ret) {
			if (ret == -EPERM)
				DEBUG(MTD_DEBUG_LEVEL0, "onenand_erase: Device is write protected!!!\n");
			else
				DEBUG(MTD_DEBUG_LEVEL0, "onenand_erase: Failed erase, block %d\n", (unsigned) (addr >> this->erase_shift));
			instr->state = MTD_ERASE_FAILED;
			instr->fail_addr = addr;
			goto erase_exit;
		}

		len -= block_size;
		addr += block_size;
	}

	instr->state = MTD_ERASE_DONE;

erase_exit:

	ret = instr->state == MTD_ERASE_DONE ? 0 : -EIO;
	/* Do call back function */
	if (!ret)
		mtd_erase_callback(instr);

	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	return ret;
}

/**
 * onenand_sync - [MTD Interface] sync
 * @param mtd		MTD device structure
 *
 * Sync is actually a wait for chip ready function
 */
static void onenand_sync(struct mtd_info *mtd)
{
	DEBUG(MTD_DEBUG_LEVEL3, "onenand_sync: called\n");

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_SYNCING);

	/* Release it and go back */
	onenand_release_device(mtd);
}


/**
 * onenand_block_isbad - [MTD Interface] Check whether the block at the given offset is bad
 * @param mtd		MTD device structure
 * @param ofs		offset relative to mtd start
 *
 * Check whether the block is bad
 */
static int onenand_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	/* Check for invalid offset */
	if (ofs > mtd->size)
		return -EINVAL;

	return onenand_block_checkbad(mtd, ofs, 1, 0);
}

/**
 * onenand_default_block_markbad - [DEFAULT] mark a block bad
 * @param mtd		MTD device structure
 * @param ofs		offset from device start
 *
 * This is the default implementation, which can be overridden by
 * a hardware specific driver.
 */
static int onenand_default_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct onenand_chip *this = mtd->priv;
	struct bbm_info *bbm = this->bbm;
	u_char buf[2] = {0, 0};
	size_t retlen;
	int block;

	/* Get block number */
	block = ((int) ofs) >> bbm->bbt_erase_shift;
        if (bbm->bbt)
                bbm->bbt[block >> 2] |= 0x01 << ((block & 0x03) << 1);

        /* We write two bytes, so we dont have to mess with 16 bit access */
        ofs += mtd->oobsize + (bbm->badblockpos & ~0x01);
        return mtd->write_oob(mtd, ofs , 2, &retlen, buf);
}

/**
 * onenand_block_markbad - [MTD Interface] Mark the block at the given offset as bad
 * @param mtd		MTD device structure
 * @param ofs		offset relative to mtd start
 *
 * Mark the block as bad
 */
static int onenand_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct onenand_chip *this = mtd->priv;
	int ret;

	ret = onenand_block_isbad(mtd, ofs);
	if (ret) {
		/* If it was bad already, return success and do nothing */
		if (ret > 0)
			return 0;
		return ret;
	}

	return this->block_markbad(mtd, ofs);
}

/**
 * onenand_unlock - [MTD Interface] Unlock block(s)
 * @param mtd		MTD device structure
 * @param ofs		offset relative to mtd start
 * @param len		number of bytes to unlock
 *
 * Unlock one or more blocks
 */
static int onenand_unlock(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	struct onenand_chip *this = mtd->priv;
	int start, end, block, value, status;

	start = ofs >> this->erase_shift;
	end = len >> this->erase_shift;

	/* Continuous lock scheme */
	if (this->options & ONENAND_CONT_LOCK) {
		/* Set start block address */
		this->write_word(start, this->base + ONENAND_REG_START_BLOCK_ADDRESS);
		/* Set end block address */
		this->write_word(end - 1, this->base + ONENAND_REG_END_BLOCK_ADDRESS);
		/* Write unlock command */
		this->command(mtd, ONENAND_CMD_UNLOCK, 0, 0);

		/* There's no return value */
		this->wait(mtd, FL_UNLOCKING);

		/* Sanity check */
		while (this->read_word(this->base + ONENAND_REG_CTRL_STATUS)
		    & ONENAND_CTRL_ONGO)
			continue;

		/* Check lock status */
		status = this->read_word(this->base + ONENAND_REG_WP_STATUS);
		if (!(status & ONENAND_WP_US))
			printk(KERN_ERR "wp status = 0x%x\n", status);

		return 0;
	}

	/* Block lock scheme */
	for (block = start; block < end; block++) {
		/* Set block address */
		value = onenand_block_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS1);
		/* Select DataRAM for DDP */
		value = onenand_bufferram_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);
		/* Set start block address */
		this->write_word(block, this->base + ONENAND_REG_START_BLOCK_ADDRESS);
		/* Write unlock command */
		this->command(mtd, ONENAND_CMD_UNLOCK, 0, 0);

		/* There's no return value */
		this->wait(mtd, FL_UNLOCKING);

		/* Sanity check */
		while (this->read_word(this->base + ONENAND_REG_CTRL_STATUS)
		    & ONENAND_CTRL_ONGO)
			continue;

		/* Check lock status */
		status = this->read_word(this->base + ONENAND_REG_WP_STATUS);
		if (!(status & ONENAND_WP_US))
			printk(KERN_ERR "block = %d, wp status = 0x%x\n", block, status);
	}

	return 0;
}

#ifdef CONFIG_MTD_ONENAND_OTP

/* Interal OTP operation */
typedef int (*otp_op_t)(struct mtd_info *mtd, loff_t form, size_t len,
		size_t *retlen, u_char *buf);

/**
 * do_otp_read - [DEFAULT] Read OTP block area
 * @param mtd		MTD device structure
 * @param from		The offset to read
 * @param len		number of bytes to read
 * @param retlen	pointer to variable to store the number of readbytes
 * @param buf		the databuffer to put/get data
 *
 * Read OTP block area.
 */
static int do_otp_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	int ret;

	/* Enter OTP access mode */
	this->command(mtd, ONENAND_CMD_OTP_ACCESS, 0, 0);
	this->wait(mtd, FL_OTPING);

	ret = mtd->read(mtd, from, len, retlen, buf);

	/* Exit OTP access mode */
	this->command(mtd, ONENAND_CMD_RESET, 0, 0);
	this->wait(mtd, FL_RESETING);

	return ret;
}

/**
 * do_otp_write - [DEFAULT] Write OTP block area
 * @param mtd		MTD device structure
 * @param from		The offset to write
 * @param len		number of bytes to write
 * @param retlen	pointer to variable to store the number of write bytes
 * @param buf		the databuffer to put/get data
 *
 * Write OTP block area.
 */
static int do_otp_write(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	unsigned char *pbuf = buf;
	int ret;

	/* Force buffer page aligned */
	if (len < mtd->writesize) {
		memcpy(this->page_buf, buf, len);
		memset(this->page_buf + len, 0xff, mtd->writesize - len);
		pbuf = this->page_buf;
		len = mtd->writesize;
	}

	/* Enter OTP access mode */
	this->command(mtd, ONENAND_CMD_OTP_ACCESS, 0, 0);
	this->wait(mtd, FL_OTPING);

	ret = mtd->write(mtd, from, len, retlen, pbuf);

	/* Exit OTP access mode */
	this->command(mtd, ONENAND_CMD_RESET, 0, 0);
	this->wait(mtd, FL_RESETING);

	return ret;
}

/**
 * do_otp_lock - [DEFAULT] Lock OTP block area
 * @param mtd		MTD device structure
 * @param from		The offset to lock
 * @param len		number of bytes to lock
 * @param retlen	pointer to variable to store the number of lock bytes
 * @param buf		the databuffer to put/get data
 *
 * Lock OTP block area.
 */
static int do_otp_lock(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	int ret;

	/* Enter OTP access mode */
	this->command(mtd, ONENAND_CMD_OTP_ACCESS, 0, 0);
	this->wait(mtd, FL_OTPING);

	ret = mtd->write_oob(mtd, from, len, retlen, buf);

	/* Exit OTP access mode */
	this->command(mtd, ONENAND_CMD_RESET, 0, 0);
	this->wait(mtd, FL_RESETING);

	return ret;
}

/**
 * onenand_otp_walk - [DEFAULT] Handle OTP operation
 * @param mtd		MTD device structure
 * @param from		The offset to read/write
 * @param len		number of bytes to read/write
 * @param retlen	pointer to variable to store the number of read bytes
 * @param buf		the databuffer to put/get data
 * @param action	do given action
 * @param mode		specify user and factory
 *
 * Handle OTP operation.
 */
static int onenand_otp_walk(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf,
			otp_op_t action, int mode)
{
	struct onenand_chip *this = mtd->priv;
	int otp_pages;
	int density;
	int ret = 0;

	*retlen = 0;

	density = this->device_id >> ONENAND_DEVICE_DENSITY_SHIFT;
	if (density < ONENAND_DEVICE_DENSITY_512Mb)
		otp_pages = 20;
	else
		otp_pages = 10;

	if (mode == MTD_OTP_FACTORY) {
		from += mtd->writesize * otp_pages;
		otp_pages = 64 - otp_pages;
	}

	/* Check User/Factory boundary */
	if (((mtd->writesize * otp_pages) - (from + len)) < 0)
		return 0;

	while (len > 0 && otp_pages > 0) {
		if (!action) {	/* OTP Info functions */
			struct otp_info *otpinfo;

			len -= sizeof(struct otp_info);
			if (len <= 0)
				return -ENOSPC;

			otpinfo = (struct otp_info *) buf;
			otpinfo->start = from;
			otpinfo->length = mtd->writesize;
			otpinfo->locked = 0;

			from += mtd->writesize;
			buf += sizeof(struct otp_info);
			*retlen += sizeof(struct otp_info);
		} else {
			size_t tmp_retlen;
			int size = len;

			ret = action(mtd, from, len, &tmp_retlen, buf);

			buf += size;
			len -= size;
			*retlen += size;

			if (ret < 0)
				return ret;
		}
		otp_pages--;
	}

	return 0;
}

/**
 * onenand_get_fact_prot_info - [MTD Interface] Read factory OTP info
 * @param mtd		MTD device structure
 * @param buf		the databuffer to put/get data
 * @param len		number of bytes to read
 *
 * Read factory OTP info.
 */
static int onenand_get_fact_prot_info(struct mtd_info *mtd,
			struct otp_info *buf, size_t len)
{
	size_t retlen;
	int ret;

	ret = onenand_otp_walk(mtd, 0, len, &retlen, (u_char *) buf, NULL, MTD_OTP_FACTORY);

	return ret ? : retlen;
}

/**
 * onenand_read_fact_prot_reg - [MTD Interface] Read factory OTP area
 * @param mtd		MTD device structure
 * @param from		The offset to read
 * @param len		number of bytes to read
 * @param retlen	pointer to variable to store the number of read bytes
 * @param buf		the databuffer to put/get data
 *
 * Read factory OTP area.
 */
static int onenand_read_fact_prot_reg(struct mtd_info *mtd, loff_t from,
			size_t len, size_t *retlen, u_char *buf)
{
	return onenand_otp_walk(mtd, from, len, retlen, buf, do_otp_read, MTD_OTP_FACTORY);
}

/**
 * onenand_get_user_prot_info - [MTD Interface] Read user OTP info
 * @param mtd		MTD device structure
 * @param buf		the databuffer to put/get data
 * @param len		number of bytes to read
 *
 * Read user OTP info.
 */
static int onenand_get_user_prot_info(struct mtd_info *mtd,
			struct otp_info *buf, size_t len)
{
	size_t retlen;
	int ret;

	ret = onenand_otp_walk(mtd, 0, len, &retlen, (u_char *) buf, NULL, MTD_OTP_USER);

	return ret ? : retlen;
}

/**
 * onenand_read_user_prot_reg - [MTD Interface] Read user OTP area
 * @param mtd		MTD device structure
 * @param from		The offset to read
 * @param len		number of bytes to read
 * @param retlen	pointer to variable to store the number of read bytes
 * @param buf		the databuffer to put/get data
 *
 * Read user OTP area.
 */
static int onenand_read_user_prot_reg(struct mtd_info *mtd, loff_t from,
			size_t len, size_t *retlen, u_char *buf)
{
	return onenand_otp_walk(mtd, from, len, retlen, buf, do_otp_read, MTD_OTP_USER);
}

/**
 * onenand_write_user_prot_reg - [MTD Interface] Write user OTP area
 * @param mtd		MTD device structure
 * @param from		The offset to write
 * @param len		number of bytes to write
 * @param retlen	pointer to variable to store the number of write bytes
 * @param buf		the databuffer to put/get data
 *
 * Write user OTP area.
 */
static int onenand_write_user_prot_reg(struct mtd_info *mtd, loff_t from,
			size_t len, size_t *retlen, u_char *buf)
{
	return onenand_otp_walk(mtd, from, len, retlen, buf, do_otp_write, MTD_OTP_USER);
}

/**
 * onenand_lock_user_prot_reg - [MTD Interface] Lock user OTP area
 * @param mtd		MTD device structure
 * @param from		The offset to lock
 * @param len		number of bytes to unlock
 *
 * Write lock mark on spare area in page 0 in OTP block
 */
static int onenand_lock_user_prot_reg(struct mtd_info *mtd, loff_t from,
			size_t len)
{
	unsigned char oob_buf[64];
	size_t retlen;
	int ret;

	memset(oob_buf, 0xff, mtd->oobsize);
	/*
	 * Note: OTP lock operation
	 *       OTP block : 0xXXFC
	 *       1st block : 0xXXF3 (If chip support)
	 *       Both      : 0xXXF0 (If chip support)
	 */
	oob_buf[ONENAND_OTP_LOCK_OFFSET] = 0xFC;

	/*
	 * Write lock mark to 8th word of sector0 of page0 of the spare0.
	 * We write 16 bytes spare area instead of 2 bytes.
	 */
	from = 0;
	len = 16;

	ret = onenand_otp_walk(mtd, from, len, &retlen, oob_buf, do_otp_lock, MTD_OTP_USER);

	return ret ? : retlen;
}
#endif	/* CONFIG_MTD_ONENAND_OTP */

/**
 * onenand_print_device_info - Print device ID
 * @param device        device ID
 *
 * Print device ID
 */
static void onenand_print_device_info(int device)
{
        int vcc, demuxed, ddp, density;

        vcc = device & ONENAND_DEVICE_VCC_MASK;
        demuxed = device & ONENAND_DEVICE_IS_DEMUX;
        ddp = device & ONENAND_DEVICE_IS_DDP;
        density = device >> ONENAND_DEVICE_DENSITY_SHIFT;
        printk(KERN_INFO "%sOneNAND%s %dMB %sV 16-bit (0x%02x)\n",
                demuxed ? "" : "Muxed ",
                ddp ? "(DDP)" : "",
                (16 << density),
                vcc ? "2.65/3.3" : "1.8",
                device);
}

static const struct onenand_manufacturers onenand_manuf_ids[] = {
        {ONENAND_MFR_SAMSUNG, "Samsung"},
};

/**
 * onenand_check_maf - Check manufacturer ID
 * @param manuf         manufacturer ID
 *
 * Check manufacturer ID
 */
static int onenand_check_maf(int manuf)
{
	int size = ARRAY_SIZE(onenand_manuf_ids);
	char *name;
        int i;

	for (i = 0; i < size; i++)
                if (manuf == onenand_manuf_ids[i].id)
                        break;

	if (i < size)
		name = onenand_manuf_ids[i].name;
	else
		name = "Unknown";

	printk(KERN_DEBUG "OneNAND Manufacturer: %s (0x%0x)\n", name, manuf);

	return (i == size);
}

/**
 * onenand_probe - [OneNAND Interface] Probe the OneNAND device
 * @param mtd		MTD device structure
 *
 * OneNAND detection method:
 *   Compare the the values from command with ones from register
 */
static int onenand_probe(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;
	int bram_maf_id, bram_dev_id, maf_id, dev_id;
	int version_id;
	int density;

	/* Send the command for reading device ID from BootRAM */
	this->write_word(ONENAND_CMD_READID, this->base + ONENAND_BOOTRAM);

	/* Read manufacturer and device IDs from BootRAM */
	bram_maf_id = this->read_word(this->base + ONENAND_BOOTRAM + 0x0);
	bram_dev_id = this->read_word(this->base + ONENAND_BOOTRAM + 0x2);

	/* Check manufacturer ID */
	if (onenand_check_maf(bram_maf_id))
		return -ENXIO;

	/* Reset OneNAND to read default register values */
	this->write_word(ONENAND_CMD_RESET, this->base + ONENAND_BOOTRAM);

	/* Read manufacturer and device IDs from Register */
	maf_id = this->read_word(this->base + ONENAND_REG_MANUFACTURER_ID);
	dev_id = this->read_word(this->base + ONENAND_REG_DEVICE_ID);

	/* Check OneNAND device */
	if (maf_id != bram_maf_id || dev_id != bram_dev_id)
		return -ENXIO;

	/* Flash device information */
	onenand_print_device_info(dev_id);
	this->device_id = dev_id;

	density = dev_id >> ONENAND_DEVICE_DENSITY_SHIFT;
	this->chipsize = (16 << density) << 20;
	/* Set density mask. it is used for DDP */
	this->density_mask = (1 << (density + 6));

	/* OneNAND page size & block size */
	/* The data buffer size is equal to page size */
	mtd->writesize = this->read_word(this->base + ONENAND_REG_DATA_BUFFER_SIZE);
	mtd->oobsize = mtd->writesize >> 5;
	/* Pagers per block is always 64 in OneNAND */
	mtd->erasesize = mtd->writesize << 6;

	this->erase_shift = ffs(mtd->erasesize) - 1;
	this->page_shift = ffs(mtd->writesize) - 1;
	this->ppb_shift = (this->erase_shift - this->page_shift);
	this->page_mask = (mtd->erasesize / mtd->writesize) - 1;

	/* REVIST: Multichip handling */

	mtd->size = this->chipsize;

	/* Version ID */
	version_id = this->read_word(this->base + ONENAND_REG_VERSION_ID);
	printk(KERN_DEBUG "OneNAND version = 0x%04x\n", version_id);

	/* Lock scheme */
	if (density <= ONENAND_DEVICE_DENSITY_512Mb &&
	    !(version_id >> ONENAND_VERSION_PROCESS_SHIFT)) {
		printk(KERN_INFO "Lock scheme is Continues Lock\n");
		this->options |= ONENAND_CONT_LOCK;
	}

	return 0;
}

/**
 * onenand_suspend - [MTD Interface] Suspend the OneNAND flash
 * @param mtd		MTD device structure
 */
static int onenand_suspend(struct mtd_info *mtd)
{
	return onenand_get_device(mtd, FL_PM_SUSPENDED);
}

/**
 * onenand_resume - [MTD Interface] Resume the OneNAND flash
 * @param mtd		MTD device structure
 */
static void onenand_resume(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;

	if (this->state == FL_PM_SUSPENDED)
		onenand_release_device(mtd);
	else
		printk(KERN_ERR "resume() called for the chip which is not"
				"in suspended state\n");
}

/**
 * onenand_scan - [OneNAND Interface] Scan for the OneNAND device
 * @param mtd		MTD device structure
 * @param maxchips	Number of chips to scan for
 *
 * This fills out all the not initialized function pointers
 * with the defaults.
 * The flash ID is read and the mtd/chip structures are
 * filled with the appropriate values.
 */
int onenand_scan(struct mtd_info *mtd, int maxchips)
{
	struct onenand_chip *this = mtd->priv;

	if (!this->read_word)
		this->read_word = onenand_readw;
	if (!this->write_word)
		this->write_word = onenand_writew;

	if (!this->command)
		this->command = onenand_command;
	if (!this->wait)
		this->wait = onenand_wait;

	if (!this->read_bufferram)
		this->read_bufferram = onenand_read_bufferram;
	if (!this->write_bufferram)
		this->write_bufferram = onenand_write_bufferram;

	if (!this->block_markbad)
		this->block_markbad = onenand_default_block_markbad;
	if (!this->scan_bbt)
		this->scan_bbt = onenand_default_bbt;

	if (onenand_probe(mtd))
		return -ENXIO;

	/* Set Sync. Burst Read after probing */
	if (this->mmcontrol) {
		printk(KERN_INFO "OneNAND Sync. Burst Read support\n");
		this->read_bufferram = onenand_sync_read_bufferram;
	}

	/* Allocate buffers, if necessary */
	if (!this->page_buf) {
		size_t len;
		len = mtd->writesize + mtd->oobsize;
		this->page_buf = kmalloc(len, GFP_KERNEL);
		if (!this->page_buf) {
			printk(KERN_ERR "onenand_scan(): Can't allocate page_buf\n");
			return -ENOMEM;
		}
		this->options |= ONENAND_PAGEBUF_ALLOC;
	}

	this->state = FL_READY;
	init_waitqueue_head(&this->wq);
	spin_lock_init(&this->chip_lock);

	switch (mtd->oobsize) {
	case 64:
		this->autooob = &onenand_oob_64;
		break;

	case 32:
		this->autooob = &onenand_oob_32;
		break;

	default:
		printk(KERN_WARNING "No OOB scheme defined for oobsize %d\n",
			mtd->oobsize);
		/* To prevent kernel oops */
		this->autooob = &onenand_oob_32;
		break;
	}

	memcpy(&mtd->oobinfo, this->autooob, sizeof(mtd->oobinfo));

	/* Fill in remaining MTD driver data */
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
	mtd->ecctype = MTD_ECC_SW;
	mtd->erase = onenand_erase;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = onenand_read;
	mtd->write = onenand_write;
	mtd->read_oob = onenand_read_oob;
	mtd->write_oob = onenand_write_oob;
#ifdef CONFIG_MTD_ONENAND_OTP
	mtd->get_fact_prot_info = onenand_get_fact_prot_info;
	mtd->read_fact_prot_reg = onenand_read_fact_prot_reg;
	mtd->get_user_prot_info = onenand_get_user_prot_info;
	mtd->read_user_prot_reg = onenand_read_user_prot_reg;
	mtd->write_user_prot_reg = onenand_write_user_prot_reg;
	mtd->lock_user_prot_reg = onenand_lock_user_prot_reg;
#endif
	mtd->sync = onenand_sync;
	mtd->lock = NULL;
	mtd->unlock = onenand_unlock;
	mtd->suspend = onenand_suspend;
	mtd->resume = onenand_resume;
	mtd->block_isbad = onenand_block_isbad;
	mtd->block_markbad = onenand_block_markbad;
	mtd->owner = THIS_MODULE;

	/* Unlock whole block */
	mtd->unlock(mtd, 0x0, this->chipsize);

	return this->scan_bbt(mtd);
}

/**
 * onenand_release - [OneNAND Interface] Free resources held by the OneNAND device
 * @param mtd		MTD device structure
 */
void onenand_release(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;

#ifdef CONFIG_MTD_PARTITIONS
	/* Deregister partitions */
	del_mtd_partitions (mtd);
#endif
	/* Deregister the device */
	del_mtd_device (mtd);

	/* Free bad block table memory, if allocated */
	if (this->bbm)
		kfree(this->bbm);
	/* Buffer allocated by onenand_scan */
	if (this->options & ONENAND_PAGEBUF_ALLOC)
		kfree(this->page_buf);
}

EXPORT_SYMBOL_GPL(onenand_scan);
EXPORT_SYMBOL_GPL(onenand_release);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("Generic OneNAND flash driver code");

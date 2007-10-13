/*
 *  linux/drivers/mtd/onenand/onenand_base.c
 *
 *  Copyright (C) 2005-2007 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 *  Credits:
 *	Adrian Hunter <ext-adrian.hunter@nokia.com>:
 *	auto-placement support, read-while load support, various fixes
 *	Copyright (C) Nokia Corporation, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

/**
 * onenand_oob_64 - oob info for large (2KB) page
 */
static struct nand_ecclayout onenand_oob_64 = {
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
static struct nand_ecclayout onenand_oob_32 = {
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
	/* Device Flash Core select, NAND Flash Block Address */
	if (block & this->density_mask)
		return ONENAND_DDP_CHIP1 | (block ^ this->density_mask);

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
	/* Device BufferRAM Select */
	if (block & this->density_mask)
		return ONENAND_DDP_CHIP1;

	return ONENAND_DDP_CHIP0;
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

	/* Address translation */
	switch (cmd) {
	case ONENAND_CMD_UNLOCK:
	case ONENAND_CMD_LOCK:
	case ONENAND_CMD_LOCK_TIGHT:
	case ONENAND_CMD_UNLOCK_ALL:
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

		if (ONENAND_IS_2PLANE(this)) {
			/* Make the even block number */
			block &= ~1;
			/* Is it the odd plane? */
			if (addr & this->writesize)
				block++;
			page >>= 1;
		}
		page &= this->page_mask;
		break;
	}

	/* NOTE: The setting order of the registers is very important! */
	if (cmd == ONENAND_CMD_BUFFERRAM) {
		/* Select DataRAM for DDP */
		value = onenand_bufferram_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);

		if (ONENAND_IS_2PLANE(this))
			/* It is always BufferRAM0 */
			ONENAND_SET_BUFFERRAM0(this);
		else
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
		/* Now we use page size operation */
		int sectors = 4, count = 4;
		int dataram;

		switch (cmd) {
		case ONENAND_CMD_READ:
		case ONENAND_CMD_READOOB:
			dataram = ONENAND_SET_NEXT_BUFFERRAM(this);
			readcmd = 1;
			break;

		default:
			if (ONENAND_IS_2PLANE(this) && cmd == ONENAND_CMD_PROG)
				cmd = ONENAND_CMD_2X_PROG;
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
	unsigned int ctrl;

	/* The 20 msec is enough */
	timeout = jiffies + msecs_to_jiffies(20);
	while (time_before(jiffies, timeout)) {
		interrupt = this->read_word(this->base + ONENAND_REG_INTERRUPT);

		if (interrupt & flags)
			break;

		if (state != FL_READING)
			cond_resched();
	}
	/* To get correct interrupt status in timeout case */
	interrupt = this->read_word(this->base + ONENAND_REG_INTERRUPT);

	ctrl = this->read_word(this->base + ONENAND_REG_CTRL_STATUS);

	if (ctrl & ONENAND_CTRL_ERROR) {
		printk(KERN_ERR "onenand_wait: controller error = 0x%04x\n", ctrl);
		if (ctrl & ONENAND_CTRL_LOCK)
			printk(KERN_ERR "onenand_wait: it's locked error.\n");
		return ctrl;
	}

	if (interrupt & ONENAND_INT_READ) {
		int ecc = this->read_word(this->base + ONENAND_REG_ECC_STATUS);
		if (ecc) {
			if (ecc & ONENAND_ECC_2BIT_ALL) {
				printk(KERN_ERR "onenand_wait: ECC error = 0x%04x\n", ecc);
				mtd->ecc_stats.failed++;
				return ecc;
			} else if (ecc & ONENAND_ECC_1BIT_ALL) {
				printk(KERN_INFO "onenand_wait: correctable ECC error = 0x%04x\n", ecc);
				mtd->ecc_stats.corrected++;
			}
		}
	} else if (state == FL_READING) {
		printk(KERN_ERR "onenand_wait: read timeout! ctrl=0x%04x intr=0x%04x\n", ctrl, interrupt);
		return -EIO;
	}

	return 0;
}

/*
 * onenand_interrupt - [DEFAULT] onenand interrupt handler
 * @param irq		onenand interrupt number
 * @param dev_id	interrupt data
 *
 * complete the work
 */
static irqreturn_t onenand_interrupt(int irq, void *data)
{
	struct onenand_chip *this = (struct onenand_chip *) data;

	/* To handle shared interrupt */
	if (!this->complete.done)
		complete(&this->complete);

	return IRQ_HANDLED;
}

/*
 * onenand_interrupt_wait - [DEFAULT] wait until the command is done
 * @param mtd		MTD device structure
 * @param state		state to select the max. timeout value
 *
 * Wait for command done.
 */
static int onenand_interrupt_wait(struct mtd_info *mtd, int state)
{
	struct onenand_chip *this = mtd->priv;

	wait_for_completion(&this->complete);

	return onenand_wait(mtd, state);
}

/*
 * onenand_try_interrupt_wait - [DEFAULT] try interrupt wait
 * @param mtd		MTD device structure
 * @param state		state to select the max. timeout value
 *
 * Try interrupt based wait (It is used one-time)
 */
static int onenand_try_interrupt_wait(struct mtd_info *mtd, int state)
{
	struct onenand_chip *this = mtd->priv;
	unsigned long remain, timeout;

	/* We use interrupt wait first */
	this->wait = onenand_interrupt_wait;

	timeout = msecs_to_jiffies(100);
	remain = wait_for_completion_timeout(&this->complete, timeout);
	if (!remain) {
		printk(KERN_INFO "OneNAND: There's no interrupt. "
				"We use the normal wait\n");

		/* Release the irq */
		free_irq(this->irq, this);

		this->wait = onenand_wait;
	}

	return onenand_wait(mtd, state);
}

/*
 * onenand_setup_wait - [OneNAND Interface] setup onenand wait method
 * @param mtd		MTD device structure
 *
 * There's two method to wait onenand work
 * 1. polling - read interrupt status register
 * 2. interrupt - use the kernel interrupt method
 */
static void onenand_setup_wait(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;
	int syscfg;

	init_completion(&this->complete);

	if (this->irq <= 0) {
		this->wait = onenand_wait;
		return;
	}

	if (request_irq(this->irq, &onenand_interrupt,
				IRQF_SHARED, "onenand", this)) {
		/* If we can't get irq, use the normal wait */
		this->wait = onenand_wait;
		return;
	}

	/* Enable interrupt */
	syscfg = this->read_word(this->base + ONENAND_REG_SYS_CFG1);
	syscfg |= ONENAND_SYS_CFG1_IOBE;
	this->write_word(syscfg, this->base + ONENAND_REG_SYS_CFG1);

	this->wait = onenand_try_interrupt_wait;
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
		/* Note: the 'this->writesize' is a real page size */
		if (area == ONENAND_DATARAM)
			return this->writesize;
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
 * onenand_get_2x_blockpage - [GENERIC] Get blockpage at 2x program mode
 * @param mtd		MTD data structure
 * @param addr		address to check
 * @return		blockpage address
 *
 * Get blockpage address at 2x program mode
 */
static int onenand_get_2x_blockpage(struct mtd_info *mtd, loff_t addr)
{
	struct onenand_chip *this = mtd->priv;
	int blockpage, block, page;

	/* Calculate the even block number */
	block = (int) (addr >> this->erase_shift) & ~1;
	/* Is it the odd plane? */
	if (addr & this->writesize)
		block++;
	page = (int) (addr >> (this->page_shift + 1)) & this->page_mask;
	blockpage = (block << 7) | page;

	return blockpage;
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
	int blockpage, found = 0;
	unsigned int i;

	if (ONENAND_IS_2PLANE(this))
		blockpage = onenand_get_2x_blockpage(mtd, addr);
	else
		blockpage = (int) (addr >> this->page_shift);

	/* Is there valid data? */
	i = ONENAND_CURRENT_BUFFERRAM(this);
	if (this->bufferram[i].blockpage == blockpage)
		found = 1;
	else {
		/* Check another BufferRAM */
		i = ONENAND_NEXT_BUFFERRAM(this);
		if (this->bufferram[i].blockpage == blockpage) {
			ONENAND_SET_NEXT_BUFFERRAM(this);
			found = 1;
		}
	}

	if (found && ONENAND_IS_DDP(this)) {
		/* Select DataRAM for DDP */
		int block = (int) (addr >> this->erase_shift);
		int value = onenand_bufferram_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);
	}

	return found;
}

/**
 * onenand_update_bufferram - [GENERIC] Update BufferRAM information
 * @param mtd		MTD data structure
 * @param addr		address to update
 * @param valid		valid flag
 *
 * Update BufferRAM information
 */
static void onenand_update_bufferram(struct mtd_info *mtd, loff_t addr,
		int valid)
{
	struct onenand_chip *this = mtd->priv;
	int blockpage;
	unsigned int i;

	if (ONENAND_IS_2PLANE(this))
		blockpage = onenand_get_2x_blockpage(mtd, addr);
	else
		blockpage = (int) (addr >> this->page_shift);

	/* Invalidate another BufferRAM */
	i = ONENAND_NEXT_BUFFERRAM(this);
	if (this->bufferram[i].blockpage == blockpage)
		this->bufferram[i].blockpage = -1;

	/* Update BufferRAM */
	i = ONENAND_CURRENT_BUFFERRAM(this);
	if (valid)
		this->bufferram[i].blockpage = blockpage;
	else
		this->bufferram[i].blockpage = -1;
}

/**
 * onenand_invalidate_bufferram - [GENERIC] Invalidate BufferRAM information
 * @param mtd		MTD data structure
 * @param addr		start address to invalidate
 * @param len		length to invalidate
 *
 * Invalidate BufferRAM information
 */
static void onenand_invalidate_bufferram(struct mtd_info *mtd, loff_t addr,
		unsigned int len)
{
	struct onenand_chip *this = mtd->priv;
	int i;
	loff_t end_addr = addr + len;

	/* Invalidate BufferRAM */
	for (i = 0; i < MAX_BUFFERRAM; i++) {
		loff_t buf_addr = this->bufferram[i].blockpage << this->page_shift;
		if (buf_addr >= addr && buf_addr < end_addr)
			this->bufferram[i].blockpage = -1;
	}
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
 * onenand_transfer_auto_oob - [Internal] oob auto-placement transfer
 * @param mtd		MTD device structure
 * @param buf		destination address
 * @param column	oob offset to read from
 * @param thislen	oob length to read
 */
static int onenand_transfer_auto_oob(struct mtd_info *mtd, uint8_t *buf, int column,
				int thislen)
{
	struct onenand_chip *this = mtd->priv;
	struct nand_oobfree *free;
	int readcol = column;
	int readend = column + thislen;
	int lastgap = 0;
	unsigned int i;
	uint8_t *oob_buf = this->oob_buf;

	free = this->ecclayout->oobfree;
	for (i = 0; i < MTD_MAX_OOBFREE_ENTRIES && free->length; i++, free++) {
		if (readcol >= lastgap)
			readcol += free->offset - lastgap;
		if (readend >= lastgap)
			readend += free->offset - lastgap;
		lastgap = free->offset + free->length;
	}
	this->read_bufferram(mtd, ONENAND_SPARERAM, oob_buf, 0, mtd->oobsize);
	free = this->ecclayout->oobfree;
	for (i = 0; i < MTD_MAX_OOBFREE_ENTRIES && free->length; i++, free++) {
		int free_end = free->offset + free->length;
		if (free->offset < readend && free_end > readcol) {
			int st = max_t(int,free->offset,readcol);
			int ed = min_t(int,free_end,readend);
			int n = ed - st;
			memcpy(buf, oob_buf + st, n);
			buf += n;
		} else if (column == 0)
			break;
	}
	return 0;
}

/**
 * onenand_read_ops_nolock - [OneNAND Interface] OneNAND read main and/or out-of-band
 * @param mtd		MTD device structure
 * @param from		offset to read from
 * @param ops:		oob operation description structure
 *
 * OneNAND read main and/or out-of-band data
 */
static int onenand_read_ops_nolock(struct mtd_info *mtd, loff_t from,
				struct mtd_oob_ops *ops)
{
	struct onenand_chip *this = mtd->priv;
	struct mtd_ecc_stats stats;
	size_t len = ops->len;
	size_t ooblen = ops->ooblen;
	u_char *buf = ops->datbuf;
	u_char *oobbuf = ops->oobbuf;
	int read = 0, column, thislen;
	int oobread = 0, oobcolumn, thisooblen, oobsize;
	int ret = 0, boundary = 0;
	int writesize = this->writesize;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_read_ops_nolock: from = 0x%08x, len = %i\n", (unsigned int) from, (int) len);

	if (ops->mode == MTD_OOB_AUTO)
		oobsize = this->ecclayout->oobavail;
	else
		oobsize = mtd->oobsize;

	oobcolumn = from & (mtd->oobsize - 1);

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size) {
		printk(KERN_ERR "onenand_read_ops_nolock: Attempt read beyond end of device\n");
		ops->retlen = 0;
		ops->oobretlen = 0;
		return -EINVAL;
	}

	stats = mtd->ecc_stats;

 	/* Read-while-load method */

 	/* Do first load to bufferRAM */
 	if (read < len) {
 		if (!onenand_check_bufferram(mtd, from)) {
			this->command(mtd, ONENAND_CMD_READ, from, writesize);
 			ret = this->wait(mtd, FL_READING);
 			onenand_update_bufferram(mtd, from, !ret);
 		}
 	}

	thislen = min_t(int, writesize, len - read);
	column = from & (writesize - 1);
	if (column + thislen > writesize)
		thislen = writesize - column;

 	while (!ret) {
 		/* If there is more to load then start next load */
 		from += thislen;
 		if (read + thislen < len) {
			this->command(mtd, ONENAND_CMD_READ, from, writesize);
 			/*
 			 * Chip boundary handling in DDP
 			 * Now we issued chip 1 read and pointed chip 1
 			 * bufferam so we have to point chip 0 bufferam.
 			 */
 			if (ONENAND_IS_DDP(this) &&
 			    unlikely(from == (this->chipsize >> 1))) {
 				this->write_word(ONENAND_DDP_CHIP0, this->base + ONENAND_REG_START_ADDRESS2);
 				boundary = 1;
 			} else
 				boundary = 0;
 			ONENAND_SET_PREV_BUFFERRAM(this);
 		}
 		/* While load is going, read from last bufferRAM */
 		this->read_bufferram(mtd, ONENAND_DATARAM, buf, column, thislen);

		/* Read oob area if needed */
		if (oobbuf) {
			thisooblen = oobsize - oobcolumn;
			thisooblen = min_t(int, thisooblen, ooblen - oobread);

			if (ops->mode == MTD_OOB_AUTO)
				onenand_transfer_auto_oob(mtd, oobbuf, oobcolumn, thisooblen);
			else
				this->read_bufferram(mtd, ONENAND_SPARERAM, oobbuf, oobcolumn, thisooblen);
			oobread += thisooblen;
			oobbuf += thisooblen;
			oobcolumn = 0;
		}

 		/* See if we are done */
 		read += thislen;
 		if (read == len)
 			break;
 		/* Set up for next read from bufferRAM */
 		if (unlikely(boundary))
 			this->write_word(ONENAND_DDP_CHIP1, this->base + ONENAND_REG_START_ADDRESS2);
 		ONENAND_SET_NEXT_BUFFERRAM(this);
 		buf += thislen;
		thislen = min_t(int, writesize, len - read);
 		column = 0;
 		cond_resched();
 		/* Now wait for load */
 		ret = this->wait(mtd, FL_READING);
 		onenand_update_bufferram(mtd, from, !ret);
 	}

	/*
	 * Return success, if no ECC failures, else -EBADMSG
	 * fs driver will take care of that, because
	 * retlen == desired len and result == -EBADMSG
	 */
	ops->retlen = read;
	ops->oobretlen = oobread;

	if (mtd->ecc_stats.failed - stats.failed)
		return -EBADMSG;

	if (ret)
		return ret;

	return mtd->ecc_stats.corrected - stats.corrected ? -EUCLEAN : 0;
}

/**
 * onenand_read_oob_nolock - [MTD Interface] OneNAND read out-of-band
 * @param mtd		MTD device structure
 * @param from		offset to read from
 * @param ops:		oob operation description structure
 *
 * OneNAND read out-of-band data from the spare area
 */
static int onenand_read_oob_nolock(struct mtd_info *mtd, loff_t from,
			struct mtd_oob_ops *ops)
{
	struct onenand_chip *this = mtd->priv;
	int read = 0, thislen, column, oobsize;
	size_t len = ops->ooblen;
	mtd_oob_mode_t mode = ops->mode;
	u_char *buf = ops->oobbuf;
	int ret = 0;

	from += ops->ooboffs;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_read_oob_nolock: from = 0x%08x, len = %i\n", (unsigned int) from, (int) len);

	/* Initialize return length value */
	ops->oobretlen = 0;

	if (mode == MTD_OOB_AUTO)
		oobsize = this->ecclayout->oobavail;
	else
		oobsize = mtd->oobsize;

	column = from & (mtd->oobsize - 1);

	if (unlikely(column >= oobsize)) {
		printk(KERN_ERR "onenand_read_oob_nolock: Attempted to start read outside oob\n");
		return -EINVAL;
	}

	/* Do not allow reads past end of device */
	if (unlikely(from >= mtd->size ||
		     column + len > ((mtd->size >> this->page_shift) -
				     (from >> this->page_shift)) * oobsize)) {
		printk(KERN_ERR "onenand_read_oob_nolock: Attempted to read beyond end of device\n");
		return -EINVAL;
	}

	while (read < len) {
		cond_resched();

		thislen = oobsize - column;
		thislen = min_t(int, thislen, len);

		this->command(mtd, ONENAND_CMD_READOOB, from, mtd->oobsize);

		onenand_update_bufferram(mtd, from, 0);

		ret = this->wait(mtd, FL_READING);
		/* First copy data and check return value for ECC handling */

		if (mode == MTD_OOB_AUTO)
			onenand_transfer_auto_oob(mtd, buf, column, thislen);
		else
			this->read_bufferram(mtd, ONENAND_SPARERAM, buf, column, thislen);

		if (ret) {
			printk(KERN_ERR "onenand_read_oob_nolock: read failed = 0x%x\n", ret);
			break;
		}

		read += thislen;

		if (read == len)
			break;

		buf += thislen;

		/* Read more? */
		if (read < len) {
			/* Page size */
			from += mtd->writesize;
			column = 0;
		}
	}

	ops->oobretlen = read;
	return ret;
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
	struct mtd_oob_ops ops = {
		.len	= len,
		.ooblen	= 0,
		.datbuf	= buf,
		.oobbuf	= NULL,
	};
	int ret;

	onenand_get_device(mtd, FL_READING);
	ret = onenand_read_ops_nolock(mtd, from, &ops);
	onenand_release_device(mtd);

	*retlen = ops.retlen;
	return ret;
}

/**
 * onenand_read_oob - [MTD Interface] Read main and/or out-of-band
 * @param mtd:		MTD device structure
 * @param from:		offset to read from
 * @param ops:		oob operation description structure

 * Read main and/or out-of-band
 */
static int onenand_read_oob(struct mtd_info *mtd, loff_t from,
			    struct mtd_oob_ops *ops)
{
	int ret;

	switch (ops->mode) {
	case MTD_OOB_PLACE:
	case MTD_OOB_AUTO:
		break;
	case MTD_OOB_RAW:
		/* Not implemented yet */
	default:
		return -EINVAL;
	}

	onenand_get_device(mtd, FL_READING);
	if (ops->datbuf)
		ret = onenand_read_ops_nolock(mtd, from, ops);
	else
		ret = onenand_read_oob_nolock(mtd, from, ops);
	onenand_release_device(mtd);

	return ret;
}

/**
 * onenand_bbt_wait - [DEFAULT] wait until the command is done
 * @param mtd		MTD device structure
 * @param state		state to select the max. timeout value
 *
 * Wait for command done.
 */
static int onenand_bbt_wait(struct mtd_info *mtd, int state)
{
	struct onenand_chip *this = mtd->priv;
	unsigned long timeout;
	unsigned int interrupt;
	unsigned int ctrl;

	/* The 20 msec is enough */
	timeout = jiffies + msecs_to_jiffies(20);
	while (time_before(jiffies, timeout)) {
		interrupt = this->read_word(this->base + ONENAND_REG_INTERRUPT);
		if (interrupt & ONENAND_INT_MASTER)
			break;
	}
	/* To get correct interrupt status in timeout case */
	interrupt = this->read_word(this->base + ONENAND_REG_INTERRUPT);
	ctrl = this->read_word(this->base + ONENAND_REG_CTRL_STATUS);

	if (ctrl & ONENAND_CTRL_ERROR) {
		printk(KERN_DEBUG "onenand_bbt_wait: controller error = 0x%04x\n", ctrl);
		/* Initial bad block case */
		if (ctrl & ONENAND_CTRL_LOAD)
			return ONENAND_BBT_READ_ERROR;
		return ONENAND_BBT_READ_FATAL_ERROR;
	}

	if (interrupt & ONENAND_INT_READ) {
		int ecc = this->read_word(this->base + ONENAND_REG_ECC_STATUS);
		if (ecc & ONENAND_ECC_2BIT_ALL)
			return ONENAND_BBT_READ_ERROR;
	} else {
		printk(KERN_ERR "onenand_bbt_wait: read timeout!"
			"ctrl=0x%04x intr=0x%04x\n", ctrl, interrupt);
		return ONENAND_BBT_READ_FATAL_ERROR;
	}

	return 0;
}

/**
 * onenand_bbt_read_oob - [MTD Interface] OneNAND read out-of-band for bbt scan
 * @param mtd		MTD device structure
 * @param from		offset to read from
 * @param ops		oob operation description structure
 *
 * OneNAND read out-of-band data from the spare area for bbt scan
 */
int onenand_bbt_read_oob(struct mtd_info *mtd, loff_t from, 
			    struct mtd_oob_ops *ops)
{
	struct onenand_chip *this = mtd->priv;
	int read = 0, thislen, column;
	int ret = 0;
	size_t len = ops->ooblen;
	u_char *buf = ops->oobbuf;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_bbt_read_oob: from = 0x%08x, len = %zi\n", (unsigned int) from, len);

	/* Initialize return value */
	ops->oobretlen = 0;

	/* Do not allow reads past end of device */
	if (unlikely((from + len) > mtd->size)) {
		printk(KERN_ERR "onenand_bbt_read_oob: Attempt read beyond end of device\n");
		return ONENAND_BBT_READ_FATAL_ERROR;
	}

	/* Grab the lock and see if the device is available */
	onenand_get_device(mtd, FL_READING);

	column = from & (mtd->oobsize - 1);

	while (read < len) {
		cond_resched();

		thislen = mtd->oobsize - column;
		thislen = min_t(int, thislen, len);

		this->command(mtd, ONENAND_CMD_READOOB, from, mtd->oobsize);

		onenand_update_bufferram(mtd, from, 0);

		ret = onenand_bbt_wait(mtd, FL_READING);
		if (ret)
			break;

		this->read_bufferram(mtd, ONENAND_SPARERAM, buf, column, thislen);
		read += thislen;
		if (read == len)
			break;

		buf += thislen;

		/* Read more? */
		if (read < len) {
			/* Update Page size */
			from += this->writesize;
			column = 0;
		}
	}

	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	ops->oobretlen = read;
	return ret;
}

#ifdef CONFIG_MTD_ONENAND_VERIFY_WRITE
/**
 * onenand_verify_oob - [GENERIC] verify the oob contents after a write
 * @param mtd		MTD device structure
 * @param buf		the databuffer to verify
 * @param to		offset to read from
 */
static int onenand_verify_oob(struct mtd_info *mtd, const u_char *buf, loff_t to)
{
	struct onenand_chip *this = mtd->priv;
	char oobbuf[64];
	int status, i;

	this->command(mtd, ONENAND_CMD_READOOB, to, mtd->oobsize);
	onenand_update_bufferram(mtd, to, 0);
	status = this->wait(mtd, FL_READING);
	if (status)
		return status;

	this->read_bufferram(mtd, ONENAND_SPARERAM, oobbuf, 0, mtd->oobsize);
	for (i = 0; i < mtd->oobsize; i++)
		if (buf[i] != 0xFF && buf[i] != oobbuf[i])
			return -EBADMSG;

	return 0;
}

/**
 * onenand_verify - [GENERIC] verify the chip contents after a write
 * @param mtd          MTD device structure
 * @param buf          the databuffer to verify
 * @param addr         offset to read from
 * @param len          number of bytes to read and compare
 */
static int onenand_verify(struct mtd_info *mtd, const u_char *buf, loff_t addr, size_t len)
{
	struct onenand_chip *this = mtd->priv;
	void __iomem *dataram;
	int ret = 0;
	int thislen, column;

	while (len != 0) {
		thislen = min_t(int, this->writesize, len);
		column = addr & (this->writesize - 1);
		if (column + thislen > this->writesize)
			thislen = this->writesize - column;

		this->command(mtd, ONENAND_CMD_READ, addr, this->writesize);

		onenand_update_bufferram(mtd, addr, 0);

		ret = this->wait(mtd, FL_READING);
		if (ret)
			return ret;

		onenand_update_bufferram(mtd, addr, 1);

		dataram = this->base + ONENAND_DATARAM;
		dataram += onenand_bufferram_offset(mtd, ONENAND_DATARAM);

		if (memcmp(buf, dataram + column, thislen))
			return -EBADMSG;

		len -= thislen;
		buf += thislen;
		addr += thislen;
	}

	return 0;
}
#else
#define onenand_verify(...)		(0)
#define onenand_verify_oob(...)		(0)
#endif

#define NOTALIGNED(x)	((x & (this->subpagesize - 1)) != 0)

/**
 * onenand_fill_auto_oob - [Internal] oob auto-placement transfer
 * @param mtd		MTD device structure
 * @param oob_buf	oob buffer
 * @param buf		source address
 * @param column	oob offset to write to
 * @param thislen	oob length to write
 */
static int onenand_fill_auto_oob(struct mtd_info *mtd, u_char *oob_buf,
				  const u_char *buf, int column, int thislen)
{
	struct onenand_chip *this = mtd->priv;
	struct nand_oobfree *free;
	int writecol = column;
	int writeend = column + thislen;
	int lastgap = 0;
	unsigned int i;

	free = this->ecclayout->oobfree;
	for (i = 0; i < MTD_MAX_OOBFREE_ENTRIES && free->length; i++, free++) {
		if (writecol >= lastgap)
			writecol += free->offset - lastgap;
		if (writeend >= lastgap)
			writeend += free->offset - lastgap;
		lastgap = free->offset + free->length;
	}
	free = this->ecclayout->oobfree;
	for (i = 0; i < MTD_MAX_OOBFREE_ENTRIES && free->length; i++, free++) {
		int free_end = free->offset + free->length;
		if (free->offset < writeend && free_end > writecol) {
			int st = max_t(int,free->offset,writecol);
			int ed = min_t(int,free_end,writeend);
			int n = ed - st;
			memcpy(oob_buf + st, buf, n);
			buf += n;
		} else if (column == 0)
			break;
	}
	return 0;
}

/**
 * onenand_write_ops_nolock - [OneNAND Interface] write main and/or out-of-band
 * @param mtd		MTD device structure
 * @param to		offset to write to
 * @param ops		oob operation description structure
 *
 * Write main and/or oob with ECC
 */
static int onenand_write_ops_nolock(struct mtd_info *mtd, loff_t to,
				struct mtd_oob_ops *ops)
{
	struct onenand_chip *this = mtd->priv;
	int written = 0, column, thislen, subpage;
	int oobwritten = 0, oobcolumn, thisooblen, oobsize;
	size_t len = ops->len;
	size_t ooblen = ops->ooblen;
	const u_char *buf = ops->datbuf;
	const u_char *oob = ops->oobbuf;
	u_char *oobbuf;
	int ret = 0;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_write_ops_nolock: to = 0x%08x, len = %i\n", (unsigned int) to, (int) len);

	/* Initialize retlen, in case of early exit */
	ops->retlen = 0;
	ops->oobretlen = 0;

	/* Do not allow writes past end of device */
	if (unlikely((to + len) > mtd->size)) {
		printk(KERN_ERR "onenand_write_ops_nolock: Attempt write to past end of device\n");
		return -EINVAL;
	}

	/* Reject writes, which are not page aligned */
        if (unlikely(NOTALIGNED(to)) || unlikely(NOTALIGNED(len))) {
                printk(KERN_ERR "onenand_write_ops_nolock: Attempt to write not page aligned data\n");
                return -EINVAL;
        }

	if (ops->mode == MTD_OOB_AUTO)
		oobsize = this->ecclayout->oobavail;
	else
		oobsize = mtd->oobsize;

	oobcolumn = to & (mtd->oobsize - 1);

	column = to & (mtd->writesize - 1);

	/* Loop until all data write */
	while (written < len) {
		u_char *wbuf = (u_char *) buf;

		thislen = min_t(int, mtd->writesize - column, len - written);
		thisooblen = min_t(int, oobsize - oobcolumn, ooblen - oobwritten);

		cond_resched();

		this->command(mtd, ONENAND_CMD_BUFFERRAM, to, thislen);

		/* Partial page write */
		subpage = thislen < mtd->writesize;
		if (subpage) {
			memset(this->page_buf, 0xff, mtd->writesize);
			memcpy(this->page_buf + column, buf, thislen);
			wbuf = this->page_buf;
		}

		this->write_bufferram(mtd, ONENAND_DATARAM, wbuf, 0, mtd->writesize);

		if (oob) {
			oobbuf = this->oob_buf;

			/* We send data to spare ram with oobsize
			 * to prevent byte access */
			memset(oobbuf, 0xff, mtd->oobsize);
			if (ops->mode == MTD_OOB_AUTO)
				onenand_fill_auto_oob(mtd, oobbuf, oob, oobcolumn, thisooblen);
			else
				memcpy(oobbuf + oobcolumn, oob, thisooblen);

			oobwritten += thisooblen;
			oob += thisooblen;
			oobcolumn = 0;
		} else
			oobbuf = (u_char *) ffchars;

		this->write_bufferram(mtd, ONENAND_SPARERAM, oobbuf, 0, mtd->oobsize);

		this->command(mtd, ONENAND_CMD_PROG, to, mtd->writesize);

		ret = this->wait(mtd, FL_WRITING);

		/* In partial page write we don't update bufferram */
		onenand_update_bufferram(mtd, to, !ret && !subpage);
		if (ONENAND_IS_2PLANE(this)) {
			ONENAND_SET_BUFFERRAM1(this);
			onenand_update_bufferram(mtd, to + this->writesize, !ret && !subpage);
		}

		if (ret) {
			printk(KERN_ERR "onenand_write_ops_nolock: write filaed %d\n", ret);
			break;
		}

		/* Only check verify write turn on */
		ret = onenand_verify(mtd, (u_char *) wbuf, to, thislen);
		if (ret) {
			printk(KERN_ERR "onenand_write_ops_nolock: verify failed %d\n", ret);
			break;
		}

		written += thislen;

		if (written == len)
			break;

		column = 0;
		to += thislen;
		buf += thislen;
	}

	/* Deselect and wake up anyone waiting on the device */
	onenand_release_device(mtd);

	ops->retlen = written;

	return ret;
}


/**
 * onenand_write_oob_nolock - [Internal] OneNAND write out-of-band
 * @param mtd		MTD device structure
 * @param to		offset to write to
 * @param len		number of bytes to write
 * @param retlen	pointer to variable to store the number of written bytes
 * @param buf		the data to write
 * @param mode		operation mode
 *
 * OneNAND write out-of-band
 */
static int onenand_write_oob_nolock(struct mtd_info *mtd, loff_t to,
				    struct mtd_oob_ops *ops)
{
	struct onenand_chip *this = mtd->priv;
	int column, ret = 0, oobsize;
	int written = 0;
	u_char *oobbuf;
	size_t len = ops->ooblen;
	const u_char *buf = ops->oobbuf;
	mtd_oob_mode_t mode = ops->mode;

	to += ops->ooboffs;

	DEBUG(MTD_DEBUG_LEVEL3, "onenand_write_oob_nolock: to = 0x%08x, len = %i\n", (unsigned int) to, (int) len);

	/* Initialize retlen, in case of early exit */
	ops->oobretlen = 0;

	if (mode == MTD_OOB_AUTO)
		oobsize = this->ecclayout->oobavail;
	else
		oobsize = mtd->oobsize;

	column = to & (mtd->oobsize - 1);

	if (unlikely(column >= oobsize)) {
		printk(KERN_ERR "onenand_write_oob_nolock: Attempted to start write outside oob\n");
		return -EINVAL;
	}

	/* For compatibility with NAND: Do not allow write past end of page */
	if (unlikely(column + len > oobsize)) {
		printk(KERN_ERR "onenand_write_oob_nolock: "
		      "Attempt to write past end of page\n");
		return -EINVAL;
	}

	/* Do not allow reads past end of device */
	if (unlikely(to >= mtd->size ||
		     column + len > ((mtd->size >> this->page_shift) -
				     (to >> this->page_shift)) * oobsize)) {
		printk(KERN_ERR "onenand_write_oob_nolock: Attempted to write past end of device\n");
		return -EINVAL;
	}

	oobbuf = this->oob_buf;

	/* Loop until all data write */
	while (written < len) {
		int thislen = min_t(int, oobsize, len - written);

		cond_resched();

		this->command(mtd, ONENAND_CMD_BUFFERRAM, to, mtd->oobsize);

		/* We send data to spare ram with oobsize
		 * to prevent byte access */
		memset(oobbuf, 0xff, mtd->oobsize);
		if (mode == MTD_OOB_AUTO)
			onenand_fill_auto_oob(mtd, oobbuf, buf, column, thislen);
		else
			memcpy(oobbuf + column, buf, thislen);
		this->write_bufferram(mtd, ONENAND_SPARERAM, oobbuf, 0, mtd->oobsize);

		this->command(mtd, ONENAND_CMD_PROGOOB, to, mtd->oobsize);

		onenand_update_bufferram(mtd, to, 0);
		if (ONENAND_IS_2PLANE(this)) {
			ONENAND_SET_BUFFERRAM1(this);
			onenand_update_bufferram(mtd, to + this->writesize, 0);
		}

		ret = this->wait(mtd, FL_WRITING);
		if (ret) {
			printk(KERN_ERR "onenand_write_oob_nolock: write failed %d\n", ret);
			break;
		}

		ret = onenand_verify_oob(mtd, oobbuf, to);
		if (ret) {
			printk(KERN_ERR "onenand_write_oob_nolock: verify failed %d\n", ret);
			break;
		}

		written += thislen;
		if (written == len)
			break;

		to += mtd->writesize;
		buf += thislen;
		column = 0;
	}

	ops->oobretlen = written;

	return ret;
}

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
	struct mtd_oob_ops ops = {
		.len	= len,
		.ooblen	= 0,
		.datbuf	= (u_char *) buf,
		.oobbuf	= NULL,
	};
	int ret;

	onenand_get_device(mtd, FL_WRITING);
	ret = onenand_write_ops_nolock(mtd, to, &ops);
	onenand_release_device(mtd);

	*retlen = ops.retlen;
	return ret;
}

/**
 * onenand_write_oob - [MTD Interface] NAND write data and/or out-of-band
 * @param mtd:		MTD device structure
 * @param to:		offset to write
 * @param ops:		oob operation description structure
 */
static int onenand_write_oob(struct mtd_info *mtd, loff_t to,
			     struct mtd_oob_ops *ops)
{
	int ret;

	switch (ops->mode) {
	case MTD_OOB_PLACE:
	case MTD_OOB_AUTO:
		break;
	case MTD_OOB_RAW:
		/* Not implemented yet */
	default:
		return -EINVAL;
	}

	onenand_get_device(mtd, FL_WRITING);
	if (ops->datbuf)
		ret = onenand_write_ops_nolock(mtd, to, ops);
	else
		ret = onenand_write_oob_nolock(mtd, to, ops);
	onenand_release_device(mtd);

	return ret;
}

/**
 * onenand_block_isbad_nolock - [GENERIC] Check if a block is marked bad
 * @param mtd		MTD device structure
 * @param ofs		offset from device start
 * @param allowbbt	1, if its allowed to access the bbt area
 *
 * Check, if the block is bad. Either by reading the bad block table or
 * calling of the scan function.
 */
static int onenand_block_isbad_nolock(struct mtd_info *mtd, loff_t ofs, int allowbbt)
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
		printk(KERN_ERR "onenand_erase: Unaligned address\n");
		return -EINVAL;
	}

	/* Length must align on block boundary */
	if (unlikely(instr->len & (block_size - 1))) {
		printk(KERN_ERR "onenand_erase: Length not block aligned\n");
		return -EINVAL;
	}

	/* Do not allow erase past end of device */
	if (unlikely((instr->len + instr->addr) > mtd->size)) {
		printk(KERN_ERR "onenand_erase: Erase past end of device\n");
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
		cond_resched();

		/* Check if we have a bad block, we do not erase bad blocks */
		if (onenand_block_isbad_nolock(mtd, addr, 0)) {
			printk (KERN_WARNING "onenand_erase: attempt to erase a bad block at addr 0x%08x\n", (unsigned int) addr);
			instr->state = MTD_ERASE_FAILED;
			goto erase_exit;
		}

		this->command(mtd, ONENAND_CMD_ERASE, addr, block_size);

		onenand_invalidate_bufferram(mtd, addr, block_size);

		ret = this->wait(mtd, FL_ERASING);
		/* Check, if it is write protected */
		if (ret) {
			printk(KERN_ERR "onenand_erase: Failed erase, block %d\n", (unsigned) (addr >> this->erase_shift));
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
	int ret;

	/* Check for invalid offset */
	if (ofs > mtd->size)
		return -EINVAL;

	onenand_get_device(mtd, FL_READING);
	ret = onenand_block_isbad_nolock(mtd, ofs, 0);
	onenand_release_device(mtd);
	return ret;
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
	struct mtd_oob_ops ops = {
		.mode = MTD_OOB_PLACE,
		.ooblen = 2,
		.oobbuf = buf,
		.ooboffs = 0,
	};
	int block;

	/* Get block number */
	block = ((int) ofs) >> bbm->bbt_erase_shift;
        if (bbm->bbt)
                bbm->bbt[block >> 2] |= 0x01 << ((block & 0x03) << 1);

        /* We write two bytes, so we dont have to mess with 16 bit access */
        ofs += mtd->oobsize + (bbm->badblockpos & ~0x01);
        return onenand_write_oob_nolock(mtd, ofs, &ops);
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

	onenand_get_device(mtd, FL_WRITING);
	ret = this->block_markbad(mtd, ofs);
	onenand_release_device(mtd);
	return ret;
}

/**
 * onenand_do_lock_cmd - [OneNAND Interface] Lock or unlock block(s)
 * @param mtd		MTD device structure
 * @param ofs		offset relative to mtd start
 * @param len		number of bytes to lock or unlock
 * @param cmd		lock or unlock command
 *
 * Lock or unlock one or more blocks
 */
static int onenand_do_lock_cmd(struct mtd_info *mtd, loff_t ofs, size_t len, int cmd)
{
	struct onenand_chip *this = mtd->priv;
	int start, end, block, value, status;
	int wp_status_mask;

	start = ofs >> this->erase_shift;
	end = len >> this->erase_shift;

	if (cmd == ONENAND_CMD_LOCK)
		wp_status_mask = ONENAND_WP_LS;
	else
		wp_status_mask = ONENAND_WP_US;

	/* Continuous lock scheme */
	if (this->options & ONENAND_HAS_CONT_LOCK) {
		/* Set start block address */
		this->write_word(start, this->base + ONENAND_REG_START_BLOCK_ADDRESS);
		/* Set end block address */
		this->write_word(start + end - 1, this->base + ONENAND_REG_END_BLOCK_ADDRESS);
		/* Write lock command */
		this->command(mtd, cmd, 0, 0);

		/* There's no return value */
		this->wait(mtd, FL_LOCKING);

		/* Sanity check */
		while (this->read_word(this->base + ONENAND_REG_CTRL_STATUS)
		    & ONENAND_CTRL_ONGO)
			continue;

		/* Check lock status */
		status = this->read_word(this->base + ONENAND_REG_WP_STATUS);
		if (!(status & wp_status_mask))
			printk(KERN_ERR "wp status = 0x%x\n", status);

		return 0;
	}

	/* Block lock scheme */
	for (block = start; block < start + end; block++) {
		/* Set block address */
		value = onenand_block_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS1);
		/* Select DataRAM for DDP */
		value = onenand_bufferram_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);
		/* Set start block address */
		this->write_word(block, this->base + ONENAND_REG_START_BLOCK_ADDRESS);
		/* Write lock command */
		this->command(mtd, cmd, 0, 0);

		/* There's no return value */
		this->wait(mtd, FL_LOCKING);

		/* Sanity check */
		while (this->read_word(this->base + ONENAND_REG_CTRL_STATUS)
		    & ONENAND_CTRL_ONGO)
			continue;

		/* Check lock status */
		status = this->read_word(this->base + ONENAND_REG_WP_STATUS);
		if (!(status & wp_status_mask))
			printk(KERN_ERR "block = %d, wp status = 0x%x\n", block, status);
	}

	return 0;
}

/**
 * onenand_lock - [MTD Interface] Lock block(s)
 * @param mtd		MTD device structure
 * @param ofs		offset relative to mtd start
 * @param len		number of bytes to unlock
 *
 * Lock one or more blocks
 */
static int onenand_lock(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	return onenand_do_lock_cmd(mtd, ofs, len, ONENAND_CMD_LOCK);
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
	return onenand_do_lock_cmd(mtd, ofs, len, ONENAND_CMD_UNLOCK);
}

/**
 * onenand_check_lock_status - [OneNAND Interface] Check lock status
 * @param this		onenand chip data structure
 *
 * Check lock status
 */
static void onenand_check_lock_status(struct onenand_chip *this)
{
	unsigned int value, block, status;
	unsigned int end;

	end = this->chipsize >> this->erase_shift;
	for (block = 0; block < end; block++) {
		/* Set block address */
		value = onenand_block_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS1);
		/* Select DataRAM for DDP */
		value = onenand_bufferram_address(this, block);
		this->write_word(value, this->base + ONENAND_REG_START_ADDRESS2);
		/* Set start block address */
		this->write_word(block, this->base + ONENAND_REG_START_BLOCK_ADDRESS);

		/* Check lock status */
		status = this->read_word(this->base + ONENAND_REG_WP_STATUS);
		if (!(status & ONENAND_WP_US))
			printk(KERN_ERR "block = %d, wp status = 0x%x\n", block, status);
	}
}

/**
 * onenand_unlock_all - [OneNAND Interface] unlock all blocks
 * @param mtd		MTD device structure
 *
 * Unlock all blocks
 */
static int onenand_unlock_all(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;

	if (this->options & ONENAND_HAS_UNLOCK_ALL) {
		/* Set start block address */
		this->write_word(0, this->base + ONENAND_REG_START_BLOCK_ADDRESS);
		/* Write unlock command */
		this->command(mtd, ONENAND_CMD_UNLOCK_ALL, 0, 0);

		/* There's no return value */
		this->wait(mtd, FL_LOCKING);

		/* Sanity check */
		while (this->read_word(this->base + ONENAND_REG_CTRL_STATUS)
		    & ONENAND_CTRL_ONGO)
			continue;

		/* Workaround for all block unlock in DDP */
		if (ONENAND_IS_DDP(this)) {
			/* 1st block on another chip */
			loff_t ofs = this->chipsize >> 1;
			size_t len = mtd->erasesize;

			onenand_unlock(mtd, ofs, len);
		}

		onenand_check_lock_status(this);

		return 0;
	}

	onenand_unlock(mtd, 0x0, this->chipsize);

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
	struct mtd_oob_ops ops = {
		.len	= len,
		.ooblen	= 0,
		.datbuf	= buf,
		.oobbuf	= NULL,
	};
	int ret;

	/* Enter OTP access mode */
	this->command(mtd, ONENAND_CMD_OTP_ACCESS, 0, 0);
	this->wait(mtd, FL_OTPING);

	ret = onenand_read_ops_nolock(mtd, from, &ops);

	/* Exit OTP access mode */
	this->command(mtd, ONENAND_CMD_RESET, 0, 0);
	this->wait(mtd, FL_RESETING);

	return ret;
}

/**
 * do_otp_write - [DEFAULT] Write OTP block area
 * @param mtd		MTD device structure
 * @param to		The offset to write
 * @param len		number of bytes to write
 * @param retlen	pointer to variable to store the number of write bytes
 * @param buf		the databuffer to put/get data
 *
 * Write OTP block area.
 */
static int do_otp_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, u_char *buf)
{
	struct onenand_chip *this = mtd->priv;
	unsigned char *pbuf = buf;
	int ret;
	struct mtd_oob_ops ops;

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

	ops.len = len;
	ops.ooblen = 0;
	ops.datbuf = pbuf;
	ops.oobbuf = NULL;
	ret = onenand_write_ops_nolock(mtd, to, &ops);
	*retlen = ops.retlen;

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
	struct mtd_oob_ops ops = {
		.mode = MTD_OOB_PLACE,
		.ooblen = len,
		.oobbuf = buf,
		.ooboffs = 0,
	};
	int ret;

	/* Enter OTP access mode */
	this->command(mtd, ONENAND_CMD_OTP_ACCESS, 0, 0);
	this->wait(mtd, FL_OTPING);

	ret = onenand_write_oob_nolock(mtd, from, &ops);

	*retlen = ops.oobretlen;

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

	onenand_get_device(mtd, FL_OTPING);
	while (len > 0 && otp_pages > 0) {
		if (!action) {	/* OTP Info functions */
			struct otp_info *otpinfo;

			len -= sizeof(struct otp_info);
			if (len <= 0) {
				ret = -ENOSPC;
				break;
			}

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

			if (ret)
				break;
		}
		otp_pages--;
	}
	onenand_release_device(mtd);

	return ret;
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
 * onenand_check_features - Check and set OneNAND features
 * @param mtd		MTD data structure
 *
 * Check and set OneNAND features
 * - lock scheme
 * - two plane
 */
static void onenand_check_features(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;
	unsigned int density, process;

	/* Lock scheme depends on density and process */
	density = this->device_id >> ONENAND_DEVICE_DENSITY_SHIFT;
	process = this->version_id >> ONENAND_VERSION_PROCESS_SHIFT;

	/* Lock scheme */
	switch (density) {
	case ONENAND_DEVICE_DENSITY_4Gb:
		this->options |= ONENAND_HAS_2PLANE;

	case ONENAND_DEVICE_DENSITY_2Gb:
		/* 2Gb DDP don't have 2 plane */
		if (!ONENAND_IS_DDP(this))
			this->options |= ONENAND_HAS_2PLANE;
		this->options |= ONENAND_HAS_UNLOCK_ALL;

	case ONENAND_DEVICE_DENSITY_1Gb:
		/* A-Die has all block unlock */
		if (process)
			this->options |= ONENAND_HAS_UNLOCK_ALL;
		break;

	default:
		/* Some OneNAND has continuous lock scheme */
		if (!process)
			this->options |= ONENAND_HAS_CONT_LOCK;
		break;
	}

	if (this->options & ONENAND_HAS_CONT_LOCK)
		printk(KERN_DEBUG "Lock scheme is Continuous Lock\n");
	if (this->options & ONENAND_HAS_UNLOCK_ALL)
		printk(KERN_DEBUG "Chip support all block unlock\n");
	if (this->options & ONENAND_HAS_2PLANE)
		printk(KERN_DEBUG "Chip has 2 plane\n");
}

/**
 * onenand_print_device_info - Print device & version ID
 * @param device        device ID
 * @param version	version ID
 *
 * Print device & version ID
 */
static void onenand_print_device_info(int device, int version)
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
	printk(KERN_INFO "OneNAND version = 0x%04x\n", version);
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
 *   Compare the values from command with ones from register
 */
static int onenand_probe(struct mtd_info *mtd)
{
	struct onenand_chip *this = mtd->priv;
	int bram_maf_id, bram_dev_id, maf_id, dev_id, ver_id;
	int density;
	int syscfg;

	/* Save system configuration 1 */
	syscfg = this->read_word(this->base + ONENAND_REG_SYS_CFG1);
	/* Clear Sync. Burst Read mode to read BootRAM */
	this->write_word((syscfg & ~ONENAND_SYS_CFG1_SYNC_READ), this->base + ONENAND_REG_SYS_CFG1);

	/* Send the command for reading device ID from BootRAM */
	this->write_word(ONENAND_CMD_READID, this->base + ONENAND_BOOTRAM);

	/* Read manufacturer and device IDs from BootRAM */
	bram_maf_id = this->read_word(this->base + ONENAND_BOOTRAM + 0x0);
	bram_dev_id = this->read_word(this->base + ONENAND_BOOTRAM + 0x2);

	/* Reset OneNAND to read default register values */
	this->write_word(ONENAND_CMD_RESET, this->base + ONENAND_BOOTRAM);
	/* Wait reset */
	this->wait(mtd, FL_RESETING);

	/* Restore system configuration 1 */
	this->write_word(syscfg, this->base + ONENAND_REG_SYS_CFG1);

	/* Check manufacturer ID */
	if (onenand_check_maf(bram_maf_id))
		return -ENXIO;

	/* Read manufacturer and device IDs from Register */
	maf_id = this->read_word(this->base + ONENAND_REG_MANUFACTURER_ID);
	dev_id = this->read_word(this->base + ONENAND_REG_DEVICE_ID);
	ver_id = this->read_word(this->base + ONENAND_REG_VERSION_ID);

	/* Check OneNAND device */
	if (maf_id != bram_maf_id || dev_id != bram_dev_id)
		return -ENXIO;

	/* Flash device information */
	onenand_print_device_info(dev_id, ver_id);
	this->device_id = dev_id;
	this->version_id = ver_id;

	density = dev_id >> ONENAND_DEVICE_DENSITY_SHIFT;
	this->chipsize = (16 << density) << 20;
	/* Set density mask. it is used for DDP */
	if (ONENAND_IS_DDP(this))
		this->density_mask = (1 << (density + 6));
	else
		this->density_mask = 0;

	/* OneNAND page size & block size */
	/* The data buffer size is equal to page size */
	mtd->writesize = this->read_word(this->base + ONENAND_REG_DATA_BUFFER_SIZE);
	mtd->oobsize = mtd->writesize >> 5;
	/* Pages per a block are always 64 in OneNAND */
	mtd->erasesize = mtd->writesize << 6;

	this->erase_shift = ffs(mtd->erasesize) - 1;
	this->page_shift = ffs(mtd->writesize) - 1;
	this->page_mask = (1 << (this->erase_shift - this->page_shift)) - 1;
	/* It's real page size */
	this->writesize = mtd->writesize;

	/* REVIST: Multichip handling */

	mtd->size = this->chipsize;

	/* Check OneNAND features */
	onenand_check_features(mtd);

	/*
	 * We emulate the 4KiB page and 256KiB erase block size
	 * But oobsize is still 64 bytes.
	 * It is only valid if you turn on 2X program support,
	 * Otherwise it will be ignored by compiler.
	 */
	if (ONENAND_IS_2PLANE(this)) {
		mtd->writesize <<= 1;
		mtd->erasesize <<= 1;
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
	int i;
	struct onenand_chip *this = mtd->priv;

	if (!this->read_word)
		this->read_word = onenand_readw;
	if (!this->write_word)
		this->write_word = onenand_writew;

	if (!this->command)
		this->command = onenand_command;
	if (!this->wait)
		onenand_setup_wait(mtd);

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
		this->page_buf = kzalloc(mtd->writesize, GFP_KERNEL);
		if (!this->page_buf) {
			printk(KERN_ERR "onenand_scan(): Can't allocate page_buf\n");
			return -ENOMEM;
		}
		this->options |= ONENAND_PAGEBUF_ALLOC;
	}
	if (!this->oob_buf) {
		this->oob_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
		if (!this->oob_buf) {
			printk(KERN_ERR "onenand_scan(): Can't allocate oob_buf\n");
			if (this->options & ONENAND_PAGEBUF_ALLOC) {
				this->options &= ~ONENAND_PAGEBUF_ALLOC;
				kfree(this->page_buf);
			}
			return -ENOMEM;
		}
		this->options |= ONENAND_OOBBUF_ALLOC;
	}

	this->state = FL_READY;
	init_waitqueue_head(&this->wq);
	spin_lock_init(&this->chip_lock);

	/*
	 * Allow subpage writes up to oobsize.
	 */
	switch (mtd->oobsize) {
	case 64:
		this->ecclayout = &onenand_oob_64;
		mtd->subpage_sft = 2;
		break;

	case 32:
		this->ecclayout = &onenand_oob_32;
		mtd->subpage_sft = 1;
		break;

	default:
		printk(KERN_WARNING "No OOB scheme defined for oobsize %d\n",
			mtd->oobsize);
		mtd->subpage_sft = 0;
		/* To prevent kernel oops */
		this->ecclayout = &onenand_oob_32;
		break;
	}

	this->subpagesize = mtd->writesize >> mtd->subpage_sft;

	/*
	 * The number of bytes available for a client to place data into
	 * the out of band area
	 */
	this->ecclayout->oobavail = 0;
	for (i = 0; i < MTD_MAX_OOBFREE_ENTRIES &&
	    this->ecclayout->oobfree[i].length; i++)
		this->ecclayout->oobavail +=
			this->ecclayout->oobfree[i].length;
	mtd->oobavail = this->ecclayout->oobavail;

	mtd->ecclayout = this->ecclayout;

	/* Fill in remaining MTD driver data */
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
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
	mtd->lock = onenand_lock;
	mtd->unlock = onenand_unlock;
	mtd->suspend = onenand_suspend;
	mtd->resume = onenand_resume;
	mtd->block_isbad = onenand_block_isbad;
	mtd->block_markbad = onenand_block_markbad;
	mtd->owner = THIS_MODULE;

	/* Unlock whole block */
	onenand_unlock_all(mtd);

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
	if (this->bbm) {
		struct bbm_info *bbm = this->bbm;
		kfree(bbm->bbt);
		kfree(this->bbm);
	}
	/* Buffers allocated by onenand_scan */
	if (this->options & ONENAND_PAGEBUF_ALLOC)
		kfree(this->page_buf);
	if (this->options & ONENAND_OOBBUF_ALLOC)
		kfree(this->oob_buf);
}

EXPORT_SYMBOL_GPL(onenand_scan);
EXPORT_SYMBOL_GPL(onenand_release);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("Generic OneNAND flash driver code");

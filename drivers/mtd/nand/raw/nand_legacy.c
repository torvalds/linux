// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 *		  2002-2006 Thomas Gleixner (tglx@linutronix.de)
 *
 *  Credits:
 *	David Woodhouse for adding multichip support
 *
 *	Aleph One Ltd. and Toby Churchill Ltd. for supporting the
 *	rework for 2K page size chips
 *
 * This file contains all legacy helpers/code that should be removed
 * at some point.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/nmi.h>

#include "internals.h"

/**
 * nand_read_byte - [DEFAULT] read one byte from the chip
 * @chip: NAND chip object
 *
 * Default read function for 8bit buswidth
 */
static uint8_t nand_read_byte(struct nand_chip *chip)
{
	return readb(chip->legacy.IO_ADDR_R);
}

/**
 * nand_read_byte16 - [DEFAULT] read one byte endianness aware from the chip
 * @chip: NAND chip object
 *
 * Default read function for 16bit buswidth with endianness conversion.
 *
 */
static uint8_t nand_read_byte16(struct nand_chip *chip)
{
	return (uint8_t) cpu_to_le16(readw(chip->legacy.IO_ADDR_R));
}

/**
 * nand_select_chip - [DEFAULT] control CE line
 * @chip: NAND chip object
 * @chipnr: chipnumber to select, -1 for deselect
 *
 * Default select function for 1 chip devices.
 */
static void nand_select_chip(struct nand_chip *chip, int chipnr)
{
	switch (chipnr) {
	case -1:
		chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
				      0 | NAND_CTRL_CHANGE);
		break;
	case 0:
		break;

	default:
		BUG();
	}
}

/**
 * nand_write_byte - [DEFAULT] write single byte to chip
 * @chip: NAND chip object
 * @byte: value to write
 *
 * Default function to write a byte to I/O[7:0]
 */
static void nand_write_byte(struct nand_chip *chip, uint8_t byte)
{
	chip->legacy.write_buf(chip, &byte, 1);
}

/**
 * nand_write_byte16 - [DEFAULT] write single byte to a chip with width 16
 * @chip: NAND chip object
 * @byte: value to write
 *
 * Default function to write a byte to I/O[7:0] on a 16-bit wide chip.
 */
static void nand_write_byte16(struct nand_chip *chip, uint8_t byte)
{
	uint16_t word = byte;

	/*
	 * It's not entirely clear what should happen to I/O[15:8] when writing
	 * a byte. The ONFi spec (Revision 3.1; 2012-09-19, Section 2.16) reads:
	 *
	 *    When the host supports a 16-bit bus width, only data is
	 *    transferred at the 16-bit width. All address and command line
	 *    transfers shall use only the lower 8-bits of the data bus. During
	 *    command transfers, the host may place any value on the upper
	 *    8-bits of the data bus. During address transfers, the host shall
	 *    set the upper 8-bits of the data bus to 00h.
	 *
	 * One user of the write_byte callback is nand_set_features. The
	 * four parameters are specified to be written to I/O[7:0], but this is
	 * neither an address nor a command transfer. Let's assume a 0 on the
	 * upper I/O lines is OK.
	 */
	chip->legacy.write_buf(chip, (uint8_t *)&word, 2);
}

/**
 * nand_write_buf - [DEFAULT] write buffer to chip
 * @chip: NAND chip object
 * @buf: data buffer
 * @len: number of bytes to write
 *
 * Default write function for 8bit buswidth.
 */
static void nand_write_buf(struct nand_chip *chip, const uint8_t *buf, int len)
{
	iowrite8_rep(chip->legacy.IO_ADDR_W, buf, len);
}

/**
 * nand_read_buf - [DEFAULT] read chip data into buffer
 * @chip: NAND chip object
 * @buf: buffer to store date
 * @len: number of bytes to read
 *
 * Default read function for 8bit buswidth.
 */
static void nand_read_buf(struct nand_chip *chip, uint8_t *buf, int len)
{
	ioread8_rep(chip->legacy.IO_ADDR_R, buf, len);
}

/**
 * nand_write_buf16 - [DEFAULT] write buffer to chip
 * @chip: NAND chip object
 * @buf: data buffer
 * @len: number of bytes to write
 *
 * Default write function for 16bit buswidth.
 */
static void nand_write_buf16(struct nand_chip *chip, const uint8_t *buf,
			     int len)
{
	u16 *p = (u16 *) buf;

	iowrite16_rep(chip->legacy.IO_ADDR_W, p, len >> 1);
}

/**
 * nand_read_buf16 - [DEFAULT] read chip data into buffer
 * @chip: NAND chip object
 * @buf: buffer to store date
 * @len: number of bytes to read
 *
 * Default read function for 16bit buswidth.
 */
static void nand_read_buf16(struct nand_chip *chip, uint8_t *buf, int len)
{
	u16 *p = (u16 *) buf;

	ioread16_rep(chip->legacy.IO_ADDR_R, p, len >> 1);
}

/**
 * panic_nand_wait_ready - [GENERIC] Wait for the ready pin after commands.
 * @mtd: MTD device structure
 * @timeo: Timeout
 *
 * Helper function for nand_wait_ready used when needing to wait in interrupt
 * context.
 */
static void panic_nand_wait_ready(struct mtd_info *mtd, unsigned long timeo)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	int i;

	/* Wait for the device to get ready */
	for (i = 0; i < timeo; i++) {
		if (chip->legacy.dev_ready(chip))
			break;
		touch_softlockup_watchdog();
		mdelay(1);
	}
}

/**
 * nand_wait_ready - [GENERIC] Wait for the ready pin after commands.
 * @chip: NAND chip object
 *
 * Wait for the ready pin after a command, and warn if a timeout occurs.
 */
void nand_wait_ready(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	unsigned long timeo = 400;

	if (in_interrupt() || oops_in_progress)
		return panic_nand_wait_ready(mtd, timeo);

	/* Wait until command is processed or timeout occurs */
	timeo = jiffies + msecs_to_jiffies(timeo);
	do {
		if (chip->legacy.dev_ready(chip))
			return;
		cond_resched();
	} while (time_before(jiffies, timeo));

	if (!chip->legacy.dev_ready(chip))
		pr_warn_ratelimited("timeout while waiting for chip to become ready\n");
}
EXPORT_SYMBOL_GPL(nand_wait_ready);

/**
 * nand_wait_status_ready - [GENERIC] Wait for the ready status after commands.
 * @mtd: MTD device structure
 * @timeo: Timeout in ms
 *
 * Wait for status ready (i.e. command done) or timeout.
 */
static void nand_wait_status_ready(struct mtd_info *mtd, unsigned long timeo)
{
	register struct nand_chip *chip = mtd_to_nand(mtd);
	int ret;

	timeo = jiffies + msecs_to_jiffies(timeo);
	do {
		u8 status;

		ret = nand_read_data_op(chip, &status, sizeof(status), true);
		if (ret)
			return;

		if (status & NAND_STATUS_READY)
			break;
		touch_softlockup_watchdog();
	} while (time_before(jiffies, timeo));
};

/**
 * nand_command - [DEFAULT] Send command to NAND device
 * @chip: NAND chip object
 * @command: the command to be sent
 * @column: the column address for this command, -1 if none
 * @page_addr: the page address for this command, -1 if none
 *
 * Send command to NAND device. This function is used for small page devices
 * (512 Bytes per page).
 */
static void nand_command(struct nand_chip *chip, unsigned int command,
			 int column, int page_addr)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ctrl = NAND_CTRL_CLE | NAND_CTRL_CHANGE;

	/* Write out the command to the device */
	if (command == NAND_CMD_SEQIN) {
		int readcmd;

		if (column >= mtd->writesize) {
			/* OOB area */
			column -= mtd->writesize;
			readcmd = NAND_CMD_READOOB;
		} else if (column < 256) {
			/* First 256 bytes --> READ0 */
			readcmd = NAND_CMD_READ0;
		} else {
			column -= 256;
			readcmd = NAND_CMD_READ1;
		}
		chip->legacy.cmd_ctrl(chip, readcmd, ctrl);
		ctrl &= ~NAND_CTRL_CHANGE;
	}
	if (command != NAND_CMD_NONE)
		chip->legacy.cmd_ctrl(chip, command, ctrl);

	/* Address cycle, when necessary */
	ctrl = NAND_CTRL_ALE | NAND_CTRL_CHANGE;
	/* Serially input address */
	if (column != -1) {
		/* Adjust columns for 16 bit buswidth */
		if (chip->options & NAND_BUSWIDTH_16 &&
				!nand_opcode_8bits(command))
			column >>= 1;
		chip->legacy.cmd_ctrl(chip, column, ctrl);
		ctrl &= ~NAND_CTRL_CHANGE;
	}
	if (page_addr != -1) {
		chip->legacy.cmd_ctrl(chip, page_addr, ctrl);
		ctrl &= ~NAND_CTRL_CHANGE;
		chip->legacy.cmd_ctrl(chip, page_addr >> 8, ctrl);
		if (chip->options & NAND_ROW_ADDR_3)
			chip->legacy.cmd_ctrl(chip, page_addr >> 16, ctrl);
	}
	chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
			      NAND_NCE | NAND_CTRL_CHANGE);

	/*
	 * Program and erase have their own busy handlers status and sequential
	 * in needs no delay
	 */
	switch (command) {

	case NAND_CMD_NONE:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_STATUS:
	case NAND_CMD_READID:
	case NAND_CMD_SET_FEATURES:
		return;

	case NAND_CMD_RESET:
		if (chip->legacy.dev_ready)
			break;
		udelay(chip->legacy.chip_delay);
		chip->legacy.cmd_ctrl(chip, NAND_CMD_STATUS,
				      NAND_CTRL_CLE | NAND_CTRL_CHANGE);
		chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
				      NAND_NCE | NAND_CTRL_CHANGE);
		/* EZ-NAND can take upto 250ms as per ONFi v4.0 */
		nand_wait_status_ready(mtd, 250);
		return;

		/* This applies to read commands */
	case NAND_CMD_READ0:
		/*
		 * READ0 is sometimes used to exit GET STATUS mode. When this
		 * is the case no address cycles are requested, and we can use
		 * this information to detect that we should not wait for the
		 * device to be ready.
		 */
		if (column == -1 && page_addr == -1)
			return;

	default:
		/*
		 * If we don't have access to the busy pin, we apply the given
		 * command delay
		 */
		if (!chip->legacy.dev_ready) {
			udelay(chip->legacy.chip_delay);
			return;
		}
	}
	/*
	 * Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine.
	 */
	ndelay(100);

	nand_wait_ready(chip);
}

static void nand_ccs_delay(struct nand_chip *chip)
{
	/*
	 * The controller already takes care of waiting for tCCS when the RNDIN
	 * or RNDOUT command is sent, return directly.
	 */
	if (!(chip->options & NAND_WAIT_TCCS))
		return;

	/*
	 * Wait tCCS_min if it is correctly defined, otherwise wait 500ns
	 * (which should be safe for all NANDs).
	 */
	if (chip->setup_data_interface)
		ndelay(chip->data_interface.timings.sdr.tCCS_min / 1000);
	else
		ndelay(500);
}

/**
 * nand_command_lp - [DEFAULT] Send command to NAND large page device
 * @chip: NAND chip object
 * @command: the command to be sent
 * @column: the column address for this command, -1 if none
 * @page_addr: the page address for this command, -1 if none
 *
 * Send command to NAND device. This is the version for the new large page
 * devices. We don't have the separate regions as we have in the small page
 * devices. We must emulate NAND_CMD_READOOB to keep the code compatible.
 */
static void nand_command_lp(struct nand_chip *chip, unsigned int command,
			    int column, int page_addr)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/* Emulate NAND_CMD_READOOB */
	if (command == NAND_CMD_READOOB) {
		column += mtd->writesize;
		command = NAND_CMD_READ0;
	}

	/* Command latch cycle */
	if (command != NAND_CMD_NONE)
		chip->legacy.cmd_ctrl(chip, command,
				      NAND_NCE | NAND_CLE | NAND_CTRL_CHANGE);

	if (column != -1 || page_addr != -1) {
		int ctrl = NAND_CTRL_CHANGE | NAND_NCE | NAND_ALE;

		/* Serially input address */
		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (chip->options & NAND_BUSWIDTH_16 &&
					!nand_opcode_8bits(command))
				column >>= 1;
			chip->legacy.cmd_ctrl(chip, column, ctrl);
			ctrl &= ~NAND_CTRL_CHANGE;

			/* Only output a single addr cycle for 8bits opcodes. */
			if (!nand_opcode_8bits(command))
				chip->legacy.cmd_ctrl(chip, column >> 8, ctrl);
		}
		if (page_addr != -1) {
			chip->legacy.cmd_ctrl(chip, page_addr, ctrl);
			chip->legacy.cmd_ctrl(chip, page_addr >> 8,
					     NAND_NCE | NAND_ALE);
			if (chip->options & NAND_ROW_ADDR_3)
				chip->legacy.cmd_ctrl(chip, page_addr >> 16,
						      NAND_NCE | NAND_ALE);
		}
	}
	chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
			      NAND_NCE | NAND_CTRL_CHANGE);

	/*
	 * Program and erase have their own busy handlers status, sequential
	 * in and status need no delay.
	 */
	switch (command) {

	case NAND_CMD_NONE:
	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_STATUS:
	case NAND_CMD_READID:
	case NAND_CMD_SET_FEATURES:
		return;

	case NAND_CMD_RNDIN:
		nand_ccs_delay(chip);
		return;

	case NAND_CMD_RESET:
		if (chip->legacy.dev_ready)
			break;
		udelay(chip->legacy.chip_delay);
		chip->legacy.cmd_ctrl(chip, NAND_CMD_STATUS,
				      NAND_NCE | NAND_CLE | NAND_CTRL_CHANGE);
		chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
				      NAND_NCE | NAND_CTRL_CHANGE);
		/* EZ-NAND can take upto 250ms as per ONFi v4.0 */
		nand_wait_status_ready(mtd, 250);
		return;

	case NAND_CMD_RNDOUT:
		/* No ready / busy check necessary */
		chip->legacy.cmd_ctrl(chip, NAND_CMD_RNDOUTSTART,
				      NAND_NCE | NAND_CLE | NAND_CTRL_CHANGE);
		chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
				      NAND_NCE | NAND_CTRL_CHANGE);

		nand_ccs_delay(chip);
		return;

	case NAND_CMD_READ0:
		/*
		 * READ0 is sometimes used to exit GET STATUS mode. When this
		 * is the case no address cycles are requested, and we can use
		 * this information to detect that READSTART should not be
		 * issued.
		 */
		if (column == -1 && page_addr == -1)
			return;

		chip->legacy.cmd_ctrl(chip, NAND_CMD_READSTART,
				      NAND_NCE | NAND_CLE | NAND_CTRL_CHANGE);
		chip->legacy.cmd_ctrl(chip, NAND_CMD_NONE,
				      NAND_NCE | NAND_CTRL_CHANGE);

		/* This applies to read commands */
	default:
		/*
		 * If we don't have access to the busy pin, we apply the given
		 * command delay.
		 */
		if (!chip->legacy.dev_ready) {
			udelay(chip->legacy.chip_delay);
			return;
		}
	}

	/*
	 * Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine.
	 */
	ndelay(100);

	nand_wait_ready(chip);
}

/**
 * nand_get_set_features_notsupp - set/get features stub returning -ENOTSUPP
 * @chip: nand chip info structure
 * @addr: feature address.
 * @subfeature_param: the subfeature parameters, a four bytes array.
 *
 * Should be used by NAND controller drivers that do not support the SET/GET
 * FEATURES operations.
 */
int nand_get_set_features_notsupp(struct nand_chip *chip, int addr,
				  u8 *subfeature_param)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL(nand_get_set_features_notsupp);

/**
 * nand_wait - [DEFAULT] wait until the command is done
 * @mtd: MTD device structure
 * @chip: NAND chip structure
 *
 * Wait for command done. This applies to erase and program only.
 */
static int nand_wait(struct nand_chip *chip)
{

	unsigned long timeo = 400;
	u8 status;
	int ret;

	/*
	 * Apply this short delay always to ensure that we do wait tWB in any
	 * case on any machine.
	 */
	ndelay(100);

	ret = nand_status_op(chip, NULL);
	if (ret)
		return ret;

	if (in_interrupt() || oops_in_progress)
		panic_nand_wait(chip, timeo);
	else {
		timeo = jiffies + msecs_to_jiffies(timeo);
		do {
			if (chip->legacy.dev_ready) {
				if (chip->legacy.dev_ready(chip))
					break;
			} else {
				ret = nand_read_data_op(chip, &status,
							sizeof(status), true);
				if (ret)
					return ret;

				if (status & NAND_STATUS_READY)
					break;
			}
			cond_resched();
		} while (time_before(jiffies, timeo));
	}

	ret = nand_read_data_op(chip, &status, sizeof(status), true);
	if (ret)
		return ret;

	/* This can happen if in case of timeout or buggy dev_ready */
	WARN_ON(!(status & NAND_STATUS_READY));
	return status;
}

void nand_legacy_set_defaults(struct nand_chip *chip)
{
	unsigned int busw = chip->options & NAND_BUSWIDTH_16;

	if (chip->exec_op)
		return;

	/* check for proper chip_delay setup, set 20us if not */
	if (!chip->legacy.chip_delay)
		chip->legacy.chip_delay = 20;

	/* check, if a user supplied command function given */
	if (!chip->legacy.cmdfunc && !chip->exec_op)
		chip->legacy.cmdfunc = nand_command;

	/* check, if a user supplied wait function given */
	if (chip->legacy.waitfunc == NULL)
		chip->legacy.waitfunc = nand_wait;

	if (!chip->select_chip)
		chip->select_chip = nand_select_chip;

	/* If called twice, pointers that depend on busw may need to be reset */
	if (!chip->legacy.read_byte || chip->legacy.read_byte == nand_read_byte)
		chip->legacy.read_byte = busw ? nand_read_byte16 : nand_read_byte;
	if (!chip->legacy.write_buf || chip->legacy.write_buf == nand_write_buf)
		chip->legacy.write_buf = busw ? nand_write_buf16 : nand_write_buf;
	if (!chip->legacy.write_byte || chip->legacy.write_byte == nand_write_byte)
		chip->legacy.write_byte = busw ? nand_write_byte16 : nand_write_byte;
	if (!chip->legacy.read_buf || chip->legacy.read_buf == nand_read_buf)
		chip->legacy.read_buf = busw ? nand_read_buf16 : nand_read_buf;
}

void nand_legacy_adjust_cmdfunc(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/* Do not replace user supplied command function! */
	if (mtd->writesize > 512 && chip->legacy.cmdfunc == nand_command)
		chip->legacy.cmdfunc = nand_command_lp;
}

int nand_legacy_check_hooks(struct nand_chip *chip)
{
	/*
	 * ->legacy.cmdfunc() is legacy and will only be used if ->exec_op() is
	 * not populated.
	 */
	if (chip->exec_op)
		return 0;

	/*
	 * Default functions assigned for ->legacy.cmdfunc() and
	 * ->select_chip() both expect ->legacy.cmd_ctrl() to be populated.
	 */
	if ((!chip->legacy.cmdfunc || !chip->select_chip) &&
	    !chip->legacy.cmd_ctrl) {
		pr_err("->legacy.cmd_ctrl() should be provided\n");
		return -EINVAL;
	}

	return 0;
}

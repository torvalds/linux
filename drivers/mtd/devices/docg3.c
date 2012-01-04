/*
 * Handles the M-Systems DiskOnChip G3 chip
 *
 * Copyright (C) 2011 Robert Jarzmik
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define CREATE_TRACE_POINTS
#include "docg3.h"

/*
 * This driver handles the DiskOnChip G3 flash memory.
 *
 * As no specification is available from M-Systems/Sandisk, this drivers lacks
 * several functions available on the chip, as :
 *  - block erase
 *  - page write
 *  - IPL write
 *  - ECC fixing (lack of BCH algorith understanding)
 *  - powerdown / powerup
 *
 * The bus data width (8bits versus 16bits) is not handled (if_cfg flag), and
 * the driver assumes a 16bits data bus.
 *
 * DocG3 relies on 2 ECC algorithms, which are handled in hardware :
 *  - a 1 byte Hamming code stored in the OOB for each page
 *  - a 7 bytes BCH code stored in the OOB for each page
 * The BCH part is only used for check purpose, no correction is available as
 * some information is missing. What is known is that :
 *  - BCH is in GF(2^14)
 *  - BCH is over data of 520 bytes (512 page + 7 page_info bytes
 *                                   + 1 hamming byte)
 *  - BCH can correct up to 4 bits (t = 4)
 *  - BCH syndroms are calculated in hardware, and checked in hardware as well
 *
 */

static inline u8 doc_readb(struct docg3 *docg3, u16 reg)
{
	u8 val = readb(docg3->base + reg);

	trace_docg3_io(0, 8, reg, (int)val);
	return val;
}

static inline u16 doc_readw(struct docg3 *docg3, u16 reg)
{
	u16 val = readw(docg3->base + reg);

	trace_docg3_io(0, 16, reg, (int)val);
	return val;
}

static inline void doc_writeb(struct docg3 *docg3, u8 val, u16 reg)
{
	writeb(val, docg3->base + reg);
	trace_docg3_io(1, 16, reg, val);
}

static inline void doc_writew(struct docg3 *docg3, u16 val, u16 reg)
{
	writew(val, docg3->base + reg);
	trace_docg3_io(1, 16, reg, val);
}

static inline void doc_flash_command(struct docg3 *docg3, u8 cmd)
{
	doc_writeb(docg3, cmd, DOC_FLASHCOMMAND);
}

static inline void doc_flash_sequence(struct docg3 *docg3, u8 seq)
{
	doc_writeb(docg3, seq, DOC_FLASHSEQUENCE);
}

static inline void doc_flash_address(struct docg3 *docg3, u8 addr)
{
	doc_writeb(docg3, addr, DOC_FLASHADDRESS);
}

static char const *part_probes[] = { "cmdlinepart", "saftlpart", NULL };

static int doc_register_readb(struct docg3 *docg3, int reg)
{
	u8 val;

	doc_writew(docg3, reg, DOC_READADDRESS);
	val = doc_readb(docg3, reg);
	doc_vdbg("Read register %04x : %02x\n", reg, val);
	return val;
}

static int doc_register_readw(struct docg3 *docg3, int reg)
{
	u16 val;

	doc_writew(docg3, reg, DOC_READADDRESS);
	val = doc_readw(docg3, reg);
	doc_vdbg("Read register %04x : %04x\n", reg, val);
	return val;
}

/**
 * doc_delay - delay docg3 operations
 * @docg3: the device
 * @nbNOPs: the number of NOPs to issue
 *
 * As no specification is available, the right timings between chip commands are
 * unknown. The only available piece of information are the observed nops on a
 * working docg3 chip.
 * Therefore, doc_delay relies on a busy loop of NOPs, instead of scheduler
 * friendlier msleep() functions or blocking mdelay().
 */
static void doc_delay(struct docg3 *docg3, int nbNOPs)
{
	int i;

	doc_dbg("NOP x %d\n", nbNOPs);
	for (i = 0; i < nbNOPs; i++)
		doc_writeb(docg3, 0, DOC_NOP);
}

static int is_prot_seq_error(struct docg3 *docg3)
{
	int ctrl;

	ctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);
	return ctrl & (DOC_CTRL_PROTECTION_ERROR | DOC_CTRL_SEQUENCE_ERROR);
}

static int doc_is_ready(struct docg3 *docg3)
{
	int ctrl;

	ctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);
	return ctrl & DOC_CTRL_FLASHREADY;
}

static int doc_wait_ready(struct docg3 *docg3)
{
	int maxWaitCycles = 100;

	do {
		doc_delay(docg3, 4);
		cpu_relax();
	} while (!doc_is_ready(docg3) && maxWaitCycles--);
	doc_delay(docg3, 2);
	if (maxWaitCycles > 0)
		return 0;
	else
		return -EIO;
}

static int doc_reset_seq(struct docg3 *docg3)
{
	int ret;

	doc_writeb(docg3, 0x10, DOC_FLASHCONTROL);
	doc_flash_sequence(docg3, DOC_SEQ_RESET);
	doc_flash_command(docg3, DOC_CMD_RESET);
	doc_delay(docg3, 2);
	ret = doc_wait_ready(docg3);

	doc_dbg("doc_reset_seq() -> isReady=%s\n", ret ? "false" : "true");
	return ret;
}

/**
 * doc_read_data_area - Read data from data area
 * @docg3: the device
 * @buf: the buffer to fill in
 * @len: the lenght to read
 * @first: first time read, DOC_READADDRESS should be set
 *
 * Reads bytes from flash data. Handles the single byte / even bytes reads.
 */
static void doc_read_data_area(struct docg3 *docg3, void *buf, int len,
			       int first)
{
	int i, cdr, len4;
	u16 data16, *dst16;
	u8 data8, *dst8;

	doc_dbg("doc_read_data_area(buf=%p, len=%d)\n", buf, len);
	cdr = len & 0x3;
	len4 = len - cdr;

	if (first)
		doc_writew(docg3, DOC_IOSPACE_DATA, DOC_READADDRESS);
	dst16 = buf;
	for (i = 0; i < len4; i += 2) {
		data16 = doc_readw(docg3, DOC_IOSPACE_DATA);
		*dst16 = data16;
		dst16++;
	}

	if (cdr) {
		doc_writew(docg3, DOC_IOSPACE_DATA | DOC_READADDR_ONE_BYTE,
			   DOC_READADDRESS);
		doc_delay(docg3, 1);
		dst8 = (u8 *)dst16;
		for (i = 0; i < cdr; i++) {
			data8 = doc_readb(docg3, DOC_IOSPACE_DATA);
			*dst8 = data8;
			dst8++;
		}
	}
}

/**
 * doc_set_data_mode - Sets the flash to reliable data mode
 * @docg3: the device
 *
 * The reliable data mode is a bit slower than the fast mode, but less errors
 * occur.  Entering the reliable mode cannot be done without entering the fast
 * mode first.
 */
static void doc_set_reliable_mode(struct docg3 *docg3)
{
	doc_dbg("doc_set_reliable_mode()\n");
	doc_flash_sequence(docg3, DOC_SEQ_SET_MODE);
	doc_flash_command(docg3, DOC_CMD_FAST_MODE);
	doc_flash_command(docg3, DOC_CMD_RELIABLE_MODE);
	doc_delay(docg3, 2);
}

/**
 * doc_set_asic_mode - Set the ASIC mode
 * @docg3: the device
 * @mode: the mode
 *
 * The ASIC can work in 3 modes :
 *  - RESET: all registers are zeroed
 *  - NORMAL: receives and handles commands
 *  - POWERDOWN: minimal poweruse, flash parts shut off
 */
static void doc_set_asic_mode(struct docg3 *docg3, u8 mode)
{
	int i;

	for (i = 0; i < 12; i++)
		doc_readb(docg3, DOC_IOSPACE_IPL);

	mode |= DOC_ASICMODE_MDWREN;
	doc_dbg("doc_set_asic_mode(%02x)\n", mode);
	doc_writeb(docg3, mode, DOC_ASICMODE);
	doc_writeb(docg3, ~mode, DOC_ASICMODECONFIRM);
	doc_delay(docg3, 1);
}

/**
 * doc_set_device_id - Sets the devices id for cascaded G3 chips
 * @docg3: the device
 * @id: the chip to select (amongst 0, 1, 2, 3)
 *
 * There can be 4 cascaded G3 chips. This function selects the one which will
 * should be the active one.
 */
static void doc_set_device_id(struct docg3 *docg3, int id)
{
	u8 ctrl;

	doc_dbg("doc_set_device_id(%d)\n", id);
	doc_writeb(docg3, id, DOC_DEVICESELECT);
	ctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);

	ctrl &= ~DOC_CTRL_VIOLATION;
	ctrl |= DOC_CTRL_CE;
	doc_writeb(docg3, ctrl, DOC_FLASHCONTROL);
}

/**
 * doc_set_extra_page_mode - Change flash page layout
 * @docg3: the device
 *
 * Normally, the flash page is split into the data (512 bytes) and the out of
 * band data (16 bytes). For each, 4 more bytes can be accessed, where the wear
 * leveling counters are stored.  To access this last area of 4 bytes, a special
 * mode must be input to the flash ASIC.
 *
 * Returns 0 if no error occured, -EIO else.
 */
static int doc_set_extra_page_mode(struct docg3 *docg3)
{
	int fctrl;

	doc_dbg("doc_set_extra_page_mode()\n");
	doc_flash_sequence(docg3, DOC_SEQ_PAGE_SIZE_532);
	doc_flash_command(docg3, DOC_CMD_PAGE_SIZE_532);
	doc_delay(docg3, 2);

	fctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);
	if (fctrl & (DOC_CTRL_PROTECTION_ERROR | DOC_CTRL_SEQUENCE_ERROR))
		return -EIO;
	else
		return 0;
}

/**
 * doc_seek - Set both flash planes to the specified block, page for reading
 * @docg3: the device
 * @block0: the first plane block index
 * @block1: the second plane block index
 * @page: the page index within the block
 * @wear: if true, read will occur on the 4 extra bytes of the wear area
 * @ofs: offset in page to read
 *
 * Programs the flash even and odd planes to the specific block and page.
 * Alternatively, programs the flash to the wear area of the specified page.
 */
static int doc_read_seek(struct docg3 *docg3, int block0, int block1, int page,
			 int wear, int ofs)
{
	int sector, ret = 0;

	doc_dbg("doc_seek(blocks=(%d,%d), page=%d, ofs=%d, wear=%d)\n",
		block0, block1, page, ofs, wear);

	if (!wear && (ofs < 2 * DOC_LAYOUT_PAGE_SIZE)) {
		doc_flash_sequence(docg3, DOC_SEQ_SET_PLANE1);
		doc_flash_command(docg3, DOC_CMD_READ_PLANE1);
		doc_delay(docg3, 2);
	} else {
		doc_flash_sequence(docg3, DOC_SEQ_SET_PLANE2);
		doc_flash_command(docg3, DOC_CMD_READ_PLANE2);
		doc_delay(docg3, 2);
	}

	doc_set_reliable_mode(docg3);
	if (wear)
		ret = doc_set_extra_page_mode(docg3);
	if (ret)
		goto out;

	sector = (block0 << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_flash_sequence(docg3, DOC_SEQ_READ);
	doc_flash_command(docg3, DOC_CMD_PROG_BLOCK_ADDR);
	doc_delay(docg3, 1);
	doc_flash_address(docg3, sector & 0xff);
	doc_flash_address(docg3, (sector >> 8) & 0xff);
	doc_flash_address(docg3, (sector >> 16) & 0xff);
	doc_delay(docg3, 1);

	sector = (block1 << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_flash_command(docg3, DOC_CMD_PROG_BLOCK_ADDR);
	doc_delay(docg3, 1);
	doc_flash_address(docg3, sector & 0xff);
	doc_flash_address(docg3, (sector >> 8) & 0xff);
	doc_flash_address(docg3, (sector >> 16) & 0xff);
	doc_delay(docg3, 2);

out:
	return ret;
}

/**
 * doc_read_page_ecc_init - Initialize hardware ECC engine
 * @docg3: the device
 * @len: the number of bytes covered by the ECC (BCH covered)
 *
 * The function does initialize the hardware ECC engine to compute the Hamming
 * ECC (on 1 byte) and the BCH Syndroms (on 7 bytes).
 *
 * Return 0 if succeeded, -EIO on error
 */
static int doc_read_page_ecc_init(struct docg3 *docg3, int len)
{
	doc_writew(docg3, DOC_ECCCONF0_READ_MODE
		   | DOC_ECCCONF0_BCH_ENABLE | DOC_ECCCONF0_HAMMING_ENABLE
		   | (len & DOC_ECCCONF0_DATA_BYTES_MASK),
		   DOC_ECCCONF0);
	doc_delay(docg3, 4);
	doc_register_readb(docg3, DOC_FLASHCONTROL);
	return doc_wait_ready(docg3);
}

/**
 * doc_read_page_prepare - Prepares reading data from a flash page
 * @docg3: the device
 * @block0: the first plane block index on flash memory
 * @block1: the second plane block index on flash memory
 * @page: the page index in the block
 * @offset: the offset in the page (must be a multiple of 4)
 *
 * Prepares the page to be read in the flash memory :
 *   - tell ASIC to map the flash pages
 *   - tell ASIC to be in read mode
 *
 * After a call to this method, a call to doc_read_page_finish is mandatory,
 * to end the read cycle of the flash.
 *
 * Read data from a flash page. The length to be read must be between 0 and
 * (page_size + oob_size + wear_size), ie. 532, and a multiple of 4 (because
 * the extra bytes reading is not implemented).
 *
 * As pages are grouped by 2 (in 2 planes), reading from a page must be done
 * in two steps:
 *  - one read of 512 bytes at offset 0
 *  - one read of 512 bytes at offset 512 + 16
 *
 * Returns 0 if successful, -EIO if a read error occured.
 */
static int doc_read_page_prepare(struct docg3 *docg3, int block0, int block1,
				 int page, int offset)
{
	int wear_area = 0, ret = 0;

	doc_dbg("doc_read_page_prepare(blocks=(%d,%d), page=%d, ofsInPage=%d)\n",
		block0, block1, page, offset);
	if (offset >= DOC_LAYOUT_WEAR_OFFSET)
		wear_area = 1;
	if (!wear_area && offset > (DOC_LAYOUT_PAGE_OOB_SIZE * 2))
		return -EINVAL;

	doc_set_device_id(docg3, docg3->device_id);
	ret = doc_reset_seq(docg3);
	if (ret)
		goto err;

	/* Program the flash address block and page */
	ret = doc_read_seek(docg3, block0, block1, page, wear_area, offset);
	if (ret)
		goto err;

	doc_flash_command(docg3, DOC_CMD_READ_ALL_PLANES);
	doc_delay(docg3, 2);
	doc_wait_ready(docg3);

	doc_flash_command(docg3, DOC_CMD_SET_ADDR_READ);
	doc_delay(docg3, 1);
	if (offset >= DOC_LAYOUT_PAGE_SIZE * 2)
		offset -= 2 * DOC_LAYOUT_PAGE_SIZE;
	doc_flash_address(docg3, offset >> 2);
	doc_delay(docg3, 1);
	doc_wait_ready(docg3);

	doc_flash_command(docg3, DOC_CMD_READ_FLASH);

	return 0;
err:
	doc_writeb(docg3, 0, DOC_DATAEND);
	doc_delay(docg3, 2);
	return -EIO;
}

/**
 * doc_read_page_getbytes - Reads bytes from a prepared page
 * @docg3: the device
 * @len: the number of bytes to be read (must be a multiple of 4)
 * @buf: the buffer to be filled in
 * @first: 1 if first time read, DOC_READADDRESS should be set
 *
 */
static int doc_read_page_getbytes(struct docg3 *docg3, int len, u_char *buf,
				  int first)
{
	doc_read_data_area(docg3, buf, len, first);
	doc_delay(docg3, 2);
	return len;
}

/**
 * doc_get_hw_bch_syndroms - Get hardware calculated BCH syndroms
 * @docg3: the device
 * @syns:  the array of 7 integers where the syndroms will be stored
 */
static void doc_get_hw_bch_syndroms(struct docg3 *docg3, int *syns)
{
	int i;

	for (i = 0; i < DOC_ECC_BCH_SIZE; i++)
		syns[i] = doc_register_readb(docg3, DOC_BCH_SYNDROM(i));
}

/**
 * doc_read_page_finish - Ends reading of a flash page
 * @docg3: the device
 *
 * As a side effect, resets the chip selector to 0. This ensures that after each
 * read operation, the floor 0 is selected. Therefore, if the systems halts, the
 * reboot will boot on floor 0, where the IPL is.
 */
static void doc_read_page_finish(struct docg3 *docg3)
{
	doc_writeb(docg3, 0, DOC_DATAEND);
	doc_delay(docg3, 2);
	doc_set_device_id(docg3, 0);
}

/**
 * calc_block_sector - Calculate blocks, pages and ofs.

 * @from: offset in flash
 * @block0: first plane block index calculated
 * @block1: second plane block index calculated
 * @page: page calculated
 * @ofs: offset in page
 */
static void calc_block_sector(loff_t from, int *block0, int *block1, int *page,
			      int *ofs)
{
	uint sector;

	sector = from / DOC_LAYOUT_PAGE_SIZE;
	*block0 = sector / (DOC_LAYOUT_PAGES_PER_BLOCK * DOC_LAYOUT_NBPLANES)
		* DOC_LAYOUT_NBPLANES;
	*block1 = *block0 + 1;
	*page = sector % (DOC_LAYOUT_PAGES_PER_BLOCK * DOC_LAYOUT_NBPLANES);
	*page /= DOC_LAYOUT_NBPLANES;
	if (sector % 2)
		*ofs = DOC_LAYOUT_PAGE_OOB_SIZE;
	else
		*ofs = 0;
}

/**
 * doc_read - Read bytes from flash
 * @mtd: the device
 * @from: the offset from first block and first page, in bytes, aligned on page
 *        size
 * @len: the number of bytes to read (must be a multiple of 4)
 * @retlen: the number of bytes actually read
 * @buf: the filled in buffer
 *
 * Reads flash memory pages. This function does not read the OOB chunk, but only
 * the page data.
 *
 * Returns 0 if read successfull, of -EIO, -EINVAL if an error occured
 */
static int doc_read(struct mtd_info *mtd, loff_t from, size_t len,
	     size_t *retlen, u_char *buf)
{
	struct docg3 *docg3 = mtd->priv;
	int block0, block1, page, readlen, ret, ofs = 0;
	int syn[DOC_ECC_BCH_SIZE], eccconf1;
	u8 oob[DOC_LAYOUT_OOB_SIZE];

	ret = -EINVAL;
	doc_dbg("doc_read(from=%lld, len=%zu, buf=%p)\n", from, len, buf);
	if (from % DOC_LAYOUT_PAGE_SIZE)
		goto err;
	if (len % 4)
		goto err;
	calc_block_sector(from, &block0, &block1, &page, &ofs);
	if (block1 > docg3->max_block)
		goto err;

	*retlen = 0;
	ret = 0;
	readlen = min_t(size_t, len, (size_t)DOC_LAYOUT_PAGE_SIZE);
	while (!ret && len > 0) {
		readlen = min_t(size_t, len, (size_t)DOC_LAYOUT_PAGE_SIZE);
		ret = doc_read_page_prepare(docg3, block0, block1, page, ofs);
		if (ret < 0)
			goto err;
		ret = doc_read_page_ecc_init(docg3, DOC_ECC_BCH_COVERED_BYTES);
		if (ret < 0)
			goto err_in_read;
		ret = doc_read_page_getbytes(docg3, readlen, buf, 1);
		if (ret < readlen)
			goto err_in_read;
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_OOB_SIZE,
					     oob, 0);
		if (ret < DOC_LAYOUT_OOB_SIZE)
			goto err_in_read;

		*retlen += readlen;
		buf += readlen;
		len -= readlen;

		ofs ^= DOC_LAYOUT_PAGE_OOB_SIZE;
		if (ofs == 0)
			page += 2;
		if (page > DOC_ADDR_PAGE_MASK) {
			page = 0;
			block0 += 2;
			block1 += 2;
		}

		/*
		 * There should be a BCH bitstream fixing algorithm here ...
		 * By now, a page read failure is triggered by BCH error
		 */
		doc_get_hw_bch_syndroms(docg3, syn);
		eccconf1 = doc_register_readb(docg3, DOC_ECCCONF1);

		doc_dbg("OOB - INFO: %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			 oob[0], oob[1], oob[2], oob[3], oob[4],
			 oob[5], oob[6]);
		doc_dbg("OOB - HAMMING: %02x\n", oob[7]);
		doc_dbg("OOB - BCH_ECC: %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			 oob[8], oob[9], oob[10], oob[11], oob[12],
			 oob[13], oob[14]);
		doc_dbg("OOB - UNUSED: %02x\n", oob[15]);
		doc_dbg("ECC checks: ECCConf1=%x\n", eccconf1);
		doc_dbg("ECC BCH syndrom: %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			syn[0], syn[1], syn[2], syn[3], syn[4], syn[5], syn[6]);

		ret = -EBADMSG;
		if (block0 >= DOC_LAYOUT_BLOCK_FIRST_DATA) {
			if (eccconf1 & DOC_ECCCONF1_BCH_SYNDROM_ERR)
				goto err_in_read;
			if (is_prot_seq_error(docg3))
				goto err_in_read;
		}
		doc_read_page_finish(docg3);
	}

	return 0;
err_in_read:
	doc_read_page_finish(docg3);
err:
	return ret;
}

/**
 * doc_read_oob - Read out of band bytes from flash
 * @mtd: the device
 * @from: the offset from first block and first page, in bytes, aligned on page
 *        size
 * @ops: the mtd oob structure
 *
 * Reads flash memory OOB area of pages.
 *
 * Returns 0 if read successfull, of -EIO, -EINVAL if an error occured
 */
static int doc_read_oob(struct mtd_info *mtd, loff_t from,
			struct mtd_oob_ops *ops)
{
	struct docg3 *docg3 = mtd->priv;
	int block0, block1, page, ofs, ret;
	u8 *buf = ops->oobbuf;
	size_t len = ops->ooblen;

	doc_dbg("doc_read_oob(from=%lld, buf=%p, len=%zu)\n", from, buf, len);
	if (len != DOC_LAYOUT_OOB_SIZE)
		return -EINVAL;

	switch (ops->mode) {
	case MTD_OPS_PLACE_OOB:
		buf += ops->ooboffs;
		break;
	default:
		break;
	}

	calc_block_sector(from, &block0, &block1, &page, &ofs);
	if (block1 > docg3->max_block)
		return -EINVAL;

	ret = doc_read_page_prepare(docg3, block0, block1, page,
				    ofs + DOC_LAYOUT_PAGE_SIZE);
	if (!ret)
		ret = doc_read_page_ecc_init(docg3, DOC_LAYOUT_OOB_SIZE);
	if (!ret)
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_OOB_SIZE,
					     buf, 1);
	doc_read_page_finish(docg3);

	if (ret > 0)
		ops->oobretlen = ret;
	else
		ops->oobretlen = 0;
	return (ret > 0) ? 0 : ret;
}

static int doc_reload_bbt(struct docg3 *docg3)
{
	int block = DOC_LAYOUT_BLOCK_BBT;
	int ret = 0, nbpages, page;
	u_char *buf = docg3->bbt;

	nbpages = DIV_ROUND_UP(docg3->max_block + 1, 8 * DOC_LAYOUT_PAGE_SIZE);
	for (page = 0; !ret && (page < nbpages); page++) {
		ret = doc_read_page_prepare(docg3, block, block + 1,
					    page + DOC_LAYOUT_PAGE_BBT, 0);
		if (!ret)
			ret = doc_read_page_ecc_init(docg3,
						     DOC_LAYOUT_PAGE_SIZE);
		if (!ret)
			doc_read_page_getbytes(docg3, DOC_LAYOUT_PAGE_SIZE,
					       buf, 1);
		buf += DOC_LAYOUT_PAGE_SIZE;
	}
	doc_read_page_finish(docg3);
	return ret;
}

/**
 * doc_block_isbad - Checks whether a block is good or not
 * @mtd: the device
 * @from: the offset to find the correct block
 *
 * Returns 1 if block is bad, 0 if block is good
 */
static int doc_block_isbad(struct mtd_info *mtd, loff_t from)
{
	struct docg3 *docg3 = mtd->priv;
	int block0, block1, page, ofs, is_good;

	calc_block_sector(from, &block0, &block1, &page, &ofs);
	doc_dbg("doc_block_isbad(from=%lld) => block=(%d,%d), page=%d, ofs=%d\n",
		from, block0, block1, page, ofs);

	if (block0 < DOC_LAYOUT_BLOCK_FIRST_DATA)
		return 0;
	if (block1 > docg3->max_block)
		return -EINVAL;

	is_good = docg3->bbt[block0 >> 3] & (1 << (block0 & 0x7));
	return !is_good;
}

/**
 * doc_get_erase_count - Get block erase count
 * @docg3: the device
 * @from: the offset in which the block is.
 *
 * Get the number of times a block was erased. The number is the maximum of
 * erase times between first and second plane (which should be equal normally).
 *
 * Returns The number of erases, or -EINVAL or -EIO on error.
 */
static int doc_get_erase_count(struct docg3 *docg3, loff_t from)
{
	u8 buf[DOC_LAYOUT_WEAR_SIZE];
	int ret, plane1_erase_count, plane2_erase_count;
	int block0, block1, page, ofs;

	doc_dbg("doc_get_erase_count(from=%lld, buf=%p)\n", from, buf);
	if (from % DOC_LAYOUT_PAGE_SIZE)
		return -EINVAL;
	calc_block_sector(from, &block0, &block1, &page, &ofs);
	if (block1 > docg3->max_block)
		return -EINVAL;

	ret = doc_reset_seq(docg3);
	if (!ret)
		ret = doc_read_page_prepare(docg3, block0, block1, page,
					    ofs + DOC_LAYOUT_WEAR_OFFSET);
	if (!ret)
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_WEAR_SIZE,
					     buf, 1);
	doc_read_page_finish(docg3);

	if (ret || (buf[0] != DOC_ERASE_MARK) || (buf[2] != DOC_ERASE_MARK))
		return -EIO;
	plane1_erase_count = (u8)(~buf[1]) | ((u8)(~buf[4]) << 8)
		| ((u8)(~buf[5]) << 16);
	plane2_erase_count = (u8)(~buf[3]) | ((u8)(~buf[6]) << 8)
		| ((u8)(~buf[7]) << 16);

	return max(plane1_erase_count, plane2_erase_count);
}

/*
 * Debug sysfs entries
 */
static int dbg_flashctrl_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;

	int pos = 0;
	u8 fctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);

	pos += seq_printf(s,
		 "FlashControl : 0x%02x (%s,CE# %s,%s,%s,flash %s)\n",
		 fctrl,
		 fctrl & DOC_CTRL_VIOLATION ? "protocol violation" : "-",
		 fctrl & DOC_CTRL_CE ? "active" : "inactive",
		 fctrl & DOC_CTRL_PROTECTION_ERROR ? "protection error" : "-",
		 fctrl & DOC_CTRL_SEQUENCE_ERROR ? "sequence error" : "-",
		 fctrl & DOC_CTRL_FLASHREADY ? "ready" : "not ready");
	return pos;
}
DEBUGFS_RO_ATTR(flashcontrol, dbg_flashctrl_show);

static int dbg_asicmode_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;

	int pos = 0;
	int pctrl = doc_register_readb(docg3, DOC_ASICMODE);
	int mode = pctrl & 0x03;

	pos += seq_printf(s,
			 "%04x : RAM_WE=%d,RSTIN_RESET=%d,BDETCT_RESET=%d,WRITE_ENABLE=%d,POWERDOWN=%d,MODE=%d%d (",
			 pctrl,
			 pctrl & DOC_ASICMODE_RAM_WE ? 1 : 0,
			 pctrl & DOC_ASICMODE_RSTIN_RESET ? 1 : 0,
			 pctrl & DOC_ASICMODE_BDETCT_RESET ? 1 : 0,
			 pctrl & DOC_ASICMODE_MDWREN ? 1 : 0,
			 pctrl & DOC_ASICMODE_POWERDOWN ? 1 : 0,
			 mode >> 1, mode & 0x1);

	switch (mode) {
	case DOC_ASICMODE_RESET:
		pos += seq_printf(s, "reset");
		break;
	case DOC_ASICMODE_NORMAL:
		pos += seq_printf(s, "normal");
		break;
	case DOC_ASICMODE_POWERDOWN:
		pos += seq_printf(s, "powerdown");
		break;
	}
	pos += seq_printf(s, ")\n");
	return pos;
}
DEBUGFS_RO_ATTR(asic_mode, dbg_asicmode_show);

static int dbg_device_id_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;
	int pos = 0;
	int id = doc_register_readb(docg3, DOC_DEVICESELECT);

	pos += seq_printf(s, "DeviceId = %d\n", id);
	return pos;
}
DEBUGFS_RO_ATTR(device_id, dbg_device_id_show);

static int dbg_protection_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;
	int pos = 0;
	int protect = doc_register_readb(docg3, DOC_PROTECTION);
	int dps0 = doc_register_readb(docg3, DOC_DPS0_STATUS);
	int dps0_low = doc_register_readb(docg3, DOC_DPS0_ADDRLOW);
	int dps0_high = doc_register_readb(docg3, DOC_DPS0_ADDRHIGH);
	int dps1 = doc_register_readb(docg3, DOC_DPS1_STATUS);
	int dps1_low = doc_register_readb(docg3, DOC_DPS1_ADDRLOW);
	int dps1_high = doc_register_readb(docg3, DOC_DPS1_ADDRHIGH);

	pos += seq_printf(s, "Protection = 0x%02x (",
			 protect);
	if (protect & DOC_PROTECT_FOUNDRY_OTP_LOCK)
		pos += seq_printf(s, "FOUNDRY_OTP_LOCK,");
	if (protect & DOC_PROTECT_CUSTOMER_OTP_LOCK)
		pos += seq_printf(s, "CUSTOMER_OTP_LOCK,");
	if (protect & DOC_PROTECT_LOCK_INPUT)
		pos += seq_printf(s, "LOCK_INPUT,");
	if (protect & DOC_PROTECT_STICKY_LOCK)
		pos += seq_printf(s, "STICKY_LOCK,");
	if (protect & DOC_PROTECT_PROTECTION_ENABLED)
		pos += seq_printf(s, "PROTECTION ON,");
	if (protect & DOC_PROTECT_IPL_DOWNLOAD_LOCK)
		pos += seq_printf(s, "IPL_DOWNLOAD_LOCK,");
	if (protect & DOC_PROTECT_PROTECTION_ERROR)
		pos += seq_printf(s, "PROTECT_ERR,");
	else
		pos += seq_printf(s, "NO_PROTECT_ERR");
	pos += seq_printf(s, ")\n");

	pos += seq_printf(s, "DPS0 = 0x%02x : "
			 "Protected area [0x%x - 0x%x] : OTP=%d, READ=%d, "
			 "WRITE=%d, HW_LOCK=%d, KEY_OK=%d\n",
			 dps0, dps0_low, dps0_high,
			 !!(dps0 & DOC_DPS_OTP_PROTECTED),
			 !!(dps0 & DOC_DPS_READ_PROTECTED),
			 !!(dps0 & DOC_DPS_WRITE_PROTECTED),
			 !!(dps0 & DOC_DPS_HW_LOCK_ENABLED),
			 !!(dps0 & DOC_DPS_KEY_OK));
	pos += seq_printf(s, "DPS1 = 0x%02x : "
			 "Protected area [0x%x - 0x%x] : OTP=%d, READ=%d, "
			 "WRITE=%d, HW_LOCK=%d, KEY_OK=%d\n",
			 dps1, dps1_low, dps1_high,
			 !!(dps1 & DOC_DPS_OTP_PROTECTED),
			 !!(dps1 & DOC_DPS_READ_PROTECTED),
			 !!(dps1 & DOC_DPS_WRITE_PROTECTED),
			 !!(dps1 & DOC_DPS_HW_LOCK_ENABLED),
			 !!(dps1 & DOC_DPS_KEY_OK));
	return pos;
}
DEBUGFS_RO_ATTR(protection, dbg_protection_show);

static int __init doc_dbg_register(struct docg3 *docg3)
{
	struct dentry *root, *entry;

	root = debugfs_create_dir("docg3", NULL);
	if (!root)
		return -ENOMEM;

	entry = debugfs_create_file("flashcontrol", S_IRUSR, root, docg3,
				  &flashcontrol_fops);
	if (entry)
		entry = debugfs_create_file("asic_mode", S_IRUSR, root,
					    docg3, &asic_mode_fops);
	if (entry)
		entry = debugfs_create_file("device_id", S_IRUSR, root,
					    docg3, &device_id_fops);
	if (entry)
		entry = debugfs_create_file("protection", S_IRUSR, root,
					    docg3, &protection_fops);
	if (entry) {
		docg3->debugfs_root = root;
		return 0;
	} else {
		debugfs_remove_recursive(root);
		return -ENOMEM;
	}
}

static void __exit doc_dbg_unregister(struct docg3 *docg3)
{
	debugfs_remove_recursive(docg3->debugfs_root);
}

/**
 * doc_set_driver_info - Fill the mtd_info structure and docg3 structure
 * @chip_id: The chip ID of the supported chip
 * @mtd: The structure to fill
 */
static void __init doc_set_driver_info(int chip_id, struct mtd_info *mtd)
{
	struct docg3 *docg3 = mtd->priv;
	int cfg;

	cfg = doc_register_readb(docg3, DOC_CONFIGURATION);
	docg3->if_cfg = (cfg & DOC_CONF_IF_CFG ? 1 : 0);

	switch (chip_id) {
	case DOC_CHIPID_G3:
		mtd->name = "DiskOnChip G3";
		docg3->max_block = 2047;
		break;
	}
	mtd->type = MTD_NANDFLASH;
	/*
	 * Once write methods are added, the correct flags will be set.
	 * mtd->flags = MTD_CAP_NANDFLASH;
	 */
	mtd->flags = MTD_CAP_ROM;
	mtd->size = (docg3->max_block + 1) * DOC_LAYOUT_BLOCK_SIZE;
	mtd->erasesize = DOC_LAYOUT_BLOCK_SIZE * DOC_LAYOUT_NBPLANES;
	mtd->writesize = DOC_LAYOUT_PAGE_SIZE;
	mtd->oobsize = DOC_LAYOUT_OOB_SIZE;
	mtd->owner = THIS_MODULE;
	mtd->erase = NULL;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = doc_read;
	mtd->write = NULL;
	mtd->read_oob = doc_read_oob;
	mtd->write_oob = NULL;
	mtd->sync = NULL;
	mtd->block_isbad = doc_block_isbad;
}

/**
 * doc_probe - Probe the IO space for a DiskOnChip G3 chip
 * @pdev: platform device
 *
 * Probes for a G3 chip at the specified IO space in the platform data
 * ressources.
 *
 * Returns 0 on success, -ENOMEM, -ENXIO on error
 */
static int __init docg3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct docg3 *docg3;
	struct mtd_info *mtd;
	struct resource *ress;
	int ret, bbt_nbpages;
	u16 chip_id, chip_id_inv;

	ret = -ENOMEM;
	docg3 = kzalloc(sizeof(struct docg3), GFP_KERNEL);
	if (!docg3)
		goto nomem1;
	mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!mtd)
		goto nomem2;
	mtd->priv = docg3;

	ret = -ENXIO;
	ress = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ress) {
		dev_err(dev, "No I/O memory resource defined\n");
		goto noress;
	}
	docg3->base = ioremap(ress->start, DOC_IOSPACE_SIZE);

	docg3->dev = &pdev->dev;
	docg3->device_id = 0;
	doc_set_device_id(docg3, docg3->device_id);
	doc_set_asic_mode(docg3, DOC_ASICMODE_RESET);
	doc_set_asic_mode(docg3, DOC_ASICMODE_NORMAL);

	chip_id = doc_register_readw(docg3, DOC_CHIPID);
	chip_id_inv = doc_register_readw(docg3, DOC_CHIPID_INV);

	ret = -ENODEV;
	if (chip_id != (u16)(~chip_id_inv)) {
		doc_info("No device found at IO addr %p\n",
			 (void *)ress->start);
		goto nochipfound;
	}

	switch (chip_id) {
	case DOC_CHIPID_G3:
		doc_info("Found a G3 DiskOnChip at addr %p\n",
			 (void *)ress->start);
		break;
	default:
		doc_err("Chip id %04x is not a DiskOnChip G3 chip\n", chip_id);
		goto nochipfound;
	}

	doc_set_driver_info(chip_id, mtd);
	platform_set_drvdata(pdev, mtd);

	ret = -ENOMEM;
	bbt_nbpages = DIV_ROUND_UP(docg3->max_block + 1,
				   8 * DOC_LAYOUT_PAGE_SIZE);
	docg3->bbt = kzalloc(bbt_nbpages * DOC_LAYOUT_PAGE_SIZE, GFP_KERNEL);
	if (!docg3->bbt)
		goto nochipfound;
	doc_reload_bbt(docg3);

	ret = mtd_device_parse_register(mtd, part_probes,
					NULL, NULL, 0);
	if (ret)
		goto register_error;

	doc_dbg_register(docg3);
	return 0;

register_error:
	kfree(docg3->bbt);
nochipfound:
	iounmap(docg3->base);
noress:
	kfree(mtd);
nomem2:
	kfree(docg3);
nomem1:
	return ret;
}

/**
 * docg3_release - Release the driver
 * @pdev: the platform device
 *
 * Returns 0
 */
static int __exit docg3_release(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct docg3 *docg3 = mtd->priv;

	doc_dbg_unregister(docg3);
	mtd_device_unregister(mtd);
	iounmap(docg3->base);
	kfree(docg3->bbt);
	kfree(docg3);
	kfree(mtd);
	return 0;
}

static struct platform_driver g3_driver = {
	.driver		= {
		.name	= "docg3",
		.owner	= THIS_MODULE,
	},
	.remove		= __exit_p(docg3_release),
};

static int __init docg3_init(void)
{
	return platform_driver_probe(&g3_driver, docg3_probe);
}
module_init(docg3_init);


static void __exit docg3_exit(void)
{
	platform_driver_unregister(&g3_driver);
}
module_exit(docg3_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
MODULE_DESCRIPTION("MTD driver for DiskOnChip G3");

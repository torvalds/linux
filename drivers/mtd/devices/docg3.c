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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/bitmap.h>
#include <linux/bitrev.h>
#include <linux/bch.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define CREATE_TRACE_POINTS
#include "docg3.h"

/*
 * This driver handles the DiskOnChip G3 flash memory.
 *
 * As no specification is available from M-Systems/Sandisk, this drivers lacks
 * several functions available on the chip, as :
 *  - IPL write
 *
 * The bus data width (8bits versus 16bits) is not handled (if_cfg flag), and
 * the driver assumes a 16bits data bus.
 *
 * DocG3 relies on 2 ECC algorithms, which are handled in hardware :
 *  - a 1 byte Hamming code stored in the OOB for each page
 *  - a 7 bytes BCH code stored in the OOB for each page
 * The BCH ECC is :
 *  - BCH is in GF(2^14)
 *  - BCH is over data of 520 bytes (512 page + 7 page_info bytes
 *                                   + 1 hamming byte)
 *  - BCH can correct up to 4 bits (t = 4)
 *  - BCH syndroms are calculated in hardware, and checked in hardware as well
 *
 */

static unsigned int reliable_mode;
module_param(reliable_mode, uint, 0);
MODULE_PARM_DESC(reliable_mode, "Set the docg3 mode (0=normal MLC, 1=fast, "
		 "2=reliable) : MLC normal operations are in normal mode");

static int docg3_ooblayout_ecc(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	/* byte 7 is Hamming ECC, byte 8-14 are BCH ECC */
	oobregion->offset = 7;
	oobregion->length = 8;

	return 0;
}

static int docg3_ooblayout_free(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	if (section > 1)
		return -ERANGE;

	/* free bytes: byte 0 until byte 6, byte 15 */
	if (!section) {
		oobregion->offset = 0;
		oobregion->length = 7;
	} else {
		oobregion->offset = 15;
		oobregion->length = 1;
	}

	return 0;
}

static const struct mtd_ooblayout_ops nand_ooblayout_docg3_ops = {
	.ecc = docg3_ooblayout_ecc,
	.free = docg3_ooblayout_free,
};

static inline u8 doc_readb(struct docg3 *docg3, u16 reg)
{
	u8 val = readb(docg3->cascade->base + reg);

	trace_docg3_io(0, 8, reg, (int)val);
	return val;
}

static inline u16 doc_readw(struct docg3 *docg3, u16 reg)
{
	u16 val = readw(docg3->cascade->base + reg);

	trace_docg3_io(0, 16, reg, (int)val);
	return val;
}

static inline void doc_writeb(struct docg3 *docg3, u8 val, u16 reg)
{
	writeb(val, docg3->cascade->base + reg);
	trace_docg3_io(1, 8, reg, val);
}

static inline void doc_writew(struct docg3 *docg3, u16 val, u16 reg)
{
	writew(val, docg3->cascade->base + reg);
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

static char const * const part_probes[] = { "cmdlinepart", "saftlpart", NULL };

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

	doc_vdbg("NOP x %d\n", nbNOPs);
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
 * @buf: the buffer to fill in (might be NULL is dummy reads)
 * @len: the length to read
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
	cdr = len & 0x1;
	len4 = len - cdr;

	if (first)
		doc_writew(docg3, DOC_IOSPACE_DATA, DOC_READADDRESS);
	dst16 = buf;
	for (i = 0; i < len4; i += 2) {
		data16 = doc_readw(docg3, DOC_IOSPACE_DATA);
		if (dst16) {
			*dst16 = data16;
			dst16++;
		}
	}

	if (cdr) {
		doc_writew(docg3, DOC_IOSPACE_DATA | DOC_READADDR_ONE_BYTE,
			   DOC_READADDRESS);
		doc_delay(docg3, 1);
		dst8 = (u8 *)dst16;
		for (i = 0; i < cdr; i++) {
			data8 = doc_readb(docg3, DOC_IOSPACE_DATA);
			if (dst8) {
				*dst8 = data8;
				dst8++;
			}
		}
	}
}

/**
 * doc_write_data_area - Write data into data area
 * @docg3: the device
 * @buf: the buffer to get input bytes from
 * @len: the length to write
 *
 * Writes bytes into flash data. Handles the single byte / even bytes writes.
 */
static void doc_write_data_area(struct docg3 *docg3, const void *buf, int len)
{
	int i, cdr, len4;
	u16 *src16;
	u8 *src8;

	doc_dbg("doc_write_data_area(buf=%p, len=%d)\n", buf, len);
	cdr = len & 0x3;
	len4 = len - cdr;

	doc_writew(docg3, DOC_IOSPACE_DATA, DOC_READADDRESS);
	src16 = (u16 *)buf;
	for (i = 0; i < len4; i += 2) {
		doc_writew(docg3, *src16, DOC_IOSPACE_DATA);
		src16++;
	}

	src8 = (u8 *)src16;
	for (i = 0; i < cdr; i++) {
		doc_writew(docg3, DOC_IOSPACE_DATA | DOC_READADDR_ONE_BYTE,
			   DOC_READADDRESS);
		doc_writeb(docg3, *src8, DOC_IOSPACE_DATA);
		src8++;
	}
}

/**
 * doc_set_data_mode - Sets the flash to normal or reliable data mode
 * @docg3: the device
 *
 * The reliable data mode is a bit slower than the fast mode, but less errors
 * occur.  Entering the reliable mode cannot be done without entering the fast
 * mode first.
 *
 * In reliable mode, pages 2*n and 2*n+1 are clones. Writing to page 0 of blocks
 * (4,5) make the hardware write also to page 1 of blocks blocks(4,5). Reading
 * from page 0 of blocks (4,5) or from page 1 of blocks (4,5) gives the same
 * result, which is a logical and between bytes from page 0 and page 1 (which is
 * consistent with the fact that writing to a page is _clearing_ bits of that
 * page).
 */
static void doc_set_reliable_mode(struct docg3 *docg3)
{
	static char *strmode[] = { "normal", "fast", "reliable", "invalid" };

	doc_dbg("doc_set_reliable_mode(%s)\n", strmode[docg3->reliable]);
	switch (docg3->reliable) {
	case 0:
		break;
	case 1:
		doc_flash_sequence(docg3, DOC_SEQ_SET_FASTMODE);
		doc_flash_command(docg3, DOC_CMD_FAST_MODE);
		break;
	case 2:
		doc_flash_sequence(docg3, DOC_SEQ_SET_RELIABLEMODE);
		doc_flash_command(docg3, DOC_CMD_FAST_MODE);
		doc_flash_command(docg3, DOC_CMD_RELIABLE_MODE);
		break;
	default:
		doc_err("doc_set_reliable_mode(): invalid mode\n");
		break;
	}
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
 * Returns 0 if no error occurred, -EIO else.
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
 * doc_setup_addr_sector - Setup blocks/page/ofs address for one plane
 * @docg3: the device
 * @sector: the sector
 */
static void doc_setup_addr_sector(struct docg3 *docg3, int sector)
{
	doc_delay(docg3, 1);
	doc_flash_address(docg3, sector & 0xff);
	doc_flash_address(docg3, (sector >> 8) & 0xff);
	doc_flash_address(docg3, (sector >> 16) & 0xff);
	doc_delay(docg3, 1);
}

/**
 * doc_setup_writeaddr_sector - Setup blocks/page/ofs address for one plane
 * @docg3: the device
 * @sector: the sector
 * @ofs: the offset in the page, between 0 and (512 + 16 + 512)
 */
static void doc_setup_writeaddr_sector(struct docg3 *docg3, int sector, int ofs)
{
	ofs = ofs >> 2;
	doc_delay(docg3, 1);
	doc_flash_address(docg3, ofs & 0xff);
	doc_flash_address(docg3, sector & 0xff);
	doc_flash_address(docg3, (sector >> 8) & 0xff);
	doc_flash_address(docg3, (sector >> 16) & 0xff);
	doc_delay(docg3, 1);
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

	doc_flash_sequence(docg3, DOC_SEQ_READ);
	sector = (block0 << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_flash_command(docg3, DOC_CMD_PROG_BLOCK_ADDR);
	doc_setup_addr_sector(docg3, sector);

	sector = (block1 << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_flash_command(docg3, DOC_CMD_PROG_BLOCK_ADDR);
	doc_setup_addr_sector(docg3, sector);
	doc_delay(docg3, 1);

out:
	return ret;
}

/**
 * doc_write_seek - Set both flash planes to the specified block, page for writing
 * @docg3: the device
 * @block0: the first plane block index
 * @block1: the second plane block index
 * @page: the page index within the block
 * @ofs: offset in page to write
 *
 * Programs the flash even and odd planes to the specific block and page.
 * Alternatively, programs the flash to the wear area of the specified page.
 */
static int doc_write_seek(struct docg3 *docg3, int block0, int block1, int page,
			 int ofs)
{
	int ret = 0, sector;

	doc_dbg("doc_write_seek(blocks=(%d,%d), page=%d, ofs=%d)\n",
		block0, block1, page, ofs);

	doc_set_reliable_mode(docg3);

	if (ofs < 2 * DOC_LAYOUT_PAGE_SIZE) {
		doc_flash_sequence(docg3, DOC_SEQ_SET_PLANE1);
		doc_flash_command(docg3, DOC_CMD_READ_PLANE1);
		doc_delay(docg3, 2);
	} else {
		doc_flash_sequence(docg3, DOC_SEQ_SET_PLANE2);
		doc_flash_command(docg3, DOC_CMD_READ_PLANE2);
		doc_delay(docg3, 2);
	}

	doc_flash_sequence(docg3, DOC_SEQ_PAGE_SETUP);
	doc_flash_command(docg3, DOC_CMD_PROG_CYCLE1);

	sector = (block0 << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_setup_writeaddr_sector(docg3, sector, ofs);

	doc_flash_command(docg3, DOC_CMD_PROG_CYCLE3);
	doc_delay(docg3, 2);
	ret = doc_wait_ready(docg3);
	if (ret)
		goto out;

	doc_flash_command(docg3, DOC_CMD_PROG_CYCLE1);
	sector = (block1 << DOC_ADDR_BLOCK_SHIFT) + (page & DOC_ADDR_PAGE_MASK);
	doc_setup_writeaddr_sector(docg3, sector, ofs);
	doc_delay(docg3, 1);

out:
	return ret;
}


/**
 * doc_read_page_ecc_init - Initialize hardware ECC engine
 * @docg3: the device
 * @len: the number of bytes covered by the ECC (BCH covered)
 *
 * The function does initialize the hardware ECC engine to compute the Hamming
 * ECC (on 1 byte) and the BCH hardware ECC (on 7 bytes).
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
 * doc_write_page_ecc_init - Initialize hardware BCH ECC engine
 * @docg3: the device
 * @len: the number of bytes covered by the ECC (BCH covered)
 *
 * The function does initialize the hardware ECC engine to compute the Hamming
 * ECC (on 1 byte) and the BCH hardware ECC (on 7 bytes).
 *
 * Return 0 if succeeded, -EIO on error
 */
static int doc_write_page_ecc_init(struct docg3 *docg3, int len)
{
	doc_writew(docg3, DOC_ECCCONF0_WRITE_MODE
		   | DOC_ECCCONF0_BCH_ENABLE | DOC_ECCCONF0_HAMMING_ENABLE
		   | (len & DOC_ECCCONF0_DATA_BYTES_MASK),
		   DOC_ECCCONF0);
	doc_delay(docg3, 4);
	doc_register_readb(docg3, DOC_FLASHCONTROL);
	return doc_wait_ready(docg3);
}

/**
 * doc_ecc_disable - Disable Hamming and BCH ECC hardware calculator
 * @docg3: the device
 *
 * Disables the hardware ECC generator and checker, for unchecked reads (as when
 * reading OOB only or write status byte).
 */
static void doc_ecc_disable(struct docg3 *docg3)
{
	doc_writew(docg3, DOC_ECCCONF0_READ_MODE, DOC_ECCCONF0);
	doc_delay(docg3, 4);
}

/**
 * doc_hamming_ecc_init - Initialize hardware Hamming ECC engine
 * @docg3: the device
 * @nb_bytes: the number of bytes covered by the ECC (Hamming covered)
 *
 * This function programs the ECC hardware to compute the hamming code on the
 * last provided N bytes to the hardware generator.
 */
static void doc_hamming_ecc_init(struct docg3 *docg3, int nb_bytes)
{
	u8 ecc_conf1;

	ecc_conf1 = doc_register_readb(docg3, DOC_ECCCONF1);
	ecc_conf1 &= ~DOC_ECCCONF1_HAMMING_BITS_MASK;
	ecc_conf1 |= (nb_bytes & DOC_ECCCONF1_HAMMING_BITS_MASK);
	doc_writeb(docg3, ecc_conf1, DOC_ECCCONF1);
}

/**
 * doc_ecc_bch_fix_data - Fix if need be read data from flash
 * @docg3: the device
 * @buf: the buffer of read data (512 + 7 + 1 bytes)
 * @hwecc: the hardware calculated ECC.
 *         It's in fact recv_ecc ^ calc_ecc, where recv_ecc was read from OOB
 *         area data, and calc_ecc the ECC calculated by the hardware generator.
 *
 * Checks if the received data matches the ECC, and if an error is detected,
 * tries to fix the bit flips (at most 4) in the buffer buf.  As the docg3
 * understands the (data, ecc, syndroms) in an inverted order in comparison to
 * the BCH library, the function reverses the order of bits (ie. bit7 and bit0,
 * bit6 and bit 1, ...) for all ECC data.
 *
 * The hardware ecc unit produces oob_ecc ^ calc_ecc.  The kernel's bch
 * algorithm is used to decode this.  However the hw operates on page
 * data in a bit order that is the reverse of that of the bch alg,
 * requiring that the bits be reversed on the result.  Thanks to Ivan
 * Djelic for his analysis.
 *
 * Returns number of fixed bits (0, 1, 2, 3, 4) or -EBADMSG if too many bit
 * errors were detected and cannot be fixed.
 */
static int doc_ecc_bch_fix_data(struct docg3 *docg3, void *buf, u8 *hwecc)
{
	u8 ecc[DOC_ECC_BCH_SIZE];
	int errorpos[DOC_ECC_BCH_T], i, numerrs;

	for (i = 0; i < DOC_ECC_BCH_SIZE; i++)
		ecc[i] = bitrev8(hwecc[i]);
	numerrs = decode_bch(docg3->cascade->bch, NULL,
			     DOC_ECC_BCH_COVERED_BYTES,
			     NULL, ecc, NULL, errorpos);
	BUG_ON(numerrs == -EINVAL);
	if (numerrs < 0)
		goto out;

	for (i = 0; i < numerrs; i++)
		errorpos[i] = (errorpos[i] & ~7) | (7 - (errorpos[i] & 7));
	for (i = 0; i < numerrs; i++)
		if (errorpos[i] < DOC_ECC_BCH_COVERED_BYTES*8)
			/* error is located in data, correct it */
			change_bit(errorpos[i], buf);
out:
	doc_dbg("doc_ecc_bch_fix_data: flipped %d bits\n", numerrs);
	return numerrs;
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
 * Returns 0 if successful, -EIO if a read error occurred.
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
 * @buf: the buffer to be filled in (or NULL is forget bytes)
 * @first: 1 if first time read, DOC_READADDRESS should be set
 * @last_odd: 1 if last read ended up on an odd byte
 *
 * Reads bytes from a prepared page. There is a trickery here : if the last read
 * ended up on an odd offset in the 1024 bytes double page, ie. between the 2
 * planes, the first byte must be read apart. If a word (16bit) read was used,
 * the read would return the byte of plane 2 as low *and* high endian, which
 * will mess the read.
 *
 */
static int doc_read_page_getbytes(struct docg3 *docg3, int len, u_char *buf,
				  int first, int last_odd)
{
	if (last_odd && len > 0) {
		doc_read_data_area(docg3, buf, 1, first);
		doc_read_data_area(docg3, buf ? buf + 1 : buf, len - 1, 0);
	} else {
		doc_read_data_area(docg3, buf, len, first);
	}
	doc_delay(docg3, 2);
	return len;
}

/**
 * doc_write_page_putbytes - Writes bytes into a prepared page
 * @docg3: the device
 * @len: the number of bytes to be written
 * @buf: the buffer of input bytes
 *
 */
static void doc_write_page_putbytes(struct docg3 *docg3, int len,
				    const u_char *buf)
{
	doc_write_data_area(docg3, buf, len);
	doc_delay(docg3, 2);
}

/**
 * doc_get_bch_hw_ecc - Get hardware calculated BCH ECC
 * @docg3: the device
 * @hwecc:  the array of 7 integers where the hardware ecc will be stored
 */
static void doc_get_bch_hw_ecc(struct docg3 *docg3, u8 *hwecc)
{
	int i;

	for (i = 0; i < DOC_ECC_BCH_SIZE; i++)
		hwecc[i] = doc_register_readb(docg3, DOC_BCH_HW_ECC(i));
}

/**
 * doc_page_finish - Ends reading/writing of a flash page
 * @docg3: the device
 */
static void doc_page_finish(struct docg3 *docg3)
{
	doc_writeb(docg3, 0, DOC_DATAEND);
	doc_delay(docg3, 2);
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
	doc_page_finish(docg3);
	doc_set_device_id(docg3, 0);
}

/**
 * calc_block_sector - Calculate blocks, pages and ofs.

 * @from: offset in flash
 * @block0: first plane block index calculated
 * @block1: second plane block index calculated
 * @page: page calculated
 * @ofs: offset in page
 * @reliable: 0 if docg3 in normal mode, 1 if docg3 in fast mode, 2 if docg3 in
 * reliable mode.
 *
 * The calculation is based on the reliable/normal mode. In normal mode, the 64
 * pages of a block are available. In reliable mode, as pages 2*n and 2*n+1 are
 * clones, only 32 pages per block are available.
 */
static void calc_block_sector(loff_t from, int *block0, int *block1, int *page,
			      int *ofs, int reliable)
{
	uint sector, pages_biblock;

	pages_biblock = DOC_LAYOUT_PAGES_PER_BLOCK * DOC_LAYOUT_NBPLANES;
	if (reliable == 1 || reliable == 2)
		pages_biblock /= 2;

	sector = from / DOC_LAYOUT_PAGE_SIZE;
	*block0 = sector / pages_biblock * DOC_LAYOUT_NBPLANES;
	*block1 = *block0 + 1;
	*page = sector % pages_biblock;
	*page /= DOC_LAYOUT_NBPLANES;
	if (reliable == 1 || reliable == 2)
		*page *= 2;
	if (sector % 2)
		*ofs = DOC_LAYOUT_PAGE_OOB_SIZE;
	else
		*ofs = 0;
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
 * Returns 0 if read successful, of -EIO, -EINVAL if an error occurred
 */
static int doc_read_oob(struct mtd_info *mtd, loff_t from,
			struct mtd_oob_ops *ops)
{
	struct docg3 *docg3 = mtd->priv;
	int block0, block1, page, ret, skip, ofs = 0;
	u8 *oobbuf = ops->oobbuf;
	u8 *buf = ops->datbuf;
	size_t len, ooblen, nbdata, nboob;
	u8 hwecc[DOC_ECC_BCH_SIZE], eccconf1;
	int max_bitflips = 0;

	if (buf)
		len = ops->len;
	else
		len = 0;
	if (oobbuf)
		ooblen = ops->ooblen;
	else
		ooblen = 0;

	if (oobbuf && ops->mode == MTD_OPS_PLACE_OOB)
		oobbuf += ops->ooboffs;

	doc_dbg("doc_read_oob(from=%lld, mode=%d, data=(%p:%zu), oob=(%p:%zu))\n",
		from, ops->mode, buf, len, oobbuf, ooblen);
	if (ooblen % DOC_LAYOUT_OOB_SIZE)
		return -EINVAL;

	ops->oobretlen = 0;
	ops->retlen = 0;
	ret = 0;
	skip = from % DOC_LAYOUT_PAGE_SIZE;
	mutex_lock(&docg3->cascade->lock);
	while (ret >= 0 && (len > 0 || ooblen > 0)) {
		calc_block_sector(from - skip, &block0, &block1, &page, &ofs,
			docg3->reliable);
		nbdata = min_t(size_t, len, DOC_LAYOUT_PAGE_SIZE - skip);
		nboob = min_t(size_t, ooblen, (size_t)DOC_LAYOUT_OOB_SIZE);
		ret = doc_read_page_prepare(docg3, block0, block1, page, ofs);
		if (ret < 0)
			goto out;
		ret = doc_read_page_ecc_init(docg3, DOC_ECC_BCH_TOTAL_BYTES);
		if (ret < 0)
			goto err_in_read;
		ret = doc_read_page_getbytes(docg3, skip, NULL, 1, 0);
		if (ret < skip)
			goto err_in_read;
		ret = doc_read_page_getbytes(docg3, nbdata, buf, 0, skip % 2);
		if (ret < nbdata)
			goto err_in_read;
		doc_read_page_getbytes(docg3,
				       DOC_LAYOUT_PAGE_SIZE - nbdata - skip,
				       NULL, 0, (skip + nbdata) % 2);
		ret = doc_read_page_getbytes(docg3, nboob, oobbuf, 0, 0);
		if (ret < nboob)
			goto err_in_read;
		doc_read_page_getbytes(docg3, DOC_LAYOUT_OOB_SIZE - nboob,
				       NULL, 0, nboob % 2);

		doc_get_bch_hw_ecc(docg3, hwecc);
		eccconf1 = doc_register_readb(docg3, DOC_ECCCONF1);

		if (nboob >= DOC_LAYOUT_OOB_SIZE) {
			doc_dbg("OOB - INFO: %*phC\n", 7, oobbuf);
			doc_dbg("OOB - HAMMING: %02x\n", oobbuf[7]);
			doc_dbg("OOB - BCH_ECC: %*phC\n", 7, oobbuf + 8);
			doc_dbg("OOB - UNUSED: %02x\n", oobbuf[15]);
		}
		doc_dbg("ECC checks: ECCConf1=%x\n", eccconf1);
		doc_dbg("ECC HW_ECC: %*phC\n", 7, hwecc);

		ret = -EIO;
		if (is_prot_seq_error(docg3))
			goto err_in_read;
		ret = 0;
		if ((block0 >= DOC_LAYOUT_BLOCK_FIRST_DATA) &&
		    (eccconf1 & DOC_ECCCONF1_BCH_SYNDROM_ERR) &&
		    (eccconf1 & DOC_ECCCONF1_PAGE_IS_WRITTEN) &&
		    (ops->mode != MTD_OPS_RAW) &&
		    (nbdata == DOC_LAYOUT_PAGE_SIZE)) {
			ret = doc_ecc_bch_fix_data(docg3, buf, hwecc);
			if (ret < 0) {
				mtd->ecc_stats.failed++;
				ret = -EBADMSG;
			}
			if (ret > 0) {
				mtd->ecc_stats.corrected += ret;
				max_bitflips = max(max_bitflips, ret);
				ret = max_bitflips;
			}
		}

		doc_read_page_finish(docg3);
		ops->retlen += nbdata;
		ops->oobretlen += nboob;
		buf += nbdata;
		oobbuf += nboob;
		len -= nbdata;
		ooblen -= nboob;
		from += DOC_LAYOUT_PAGE_SIZE;
		skip = 0;
	}

out:
	mutex_unlock(&docg3->cascade->lock);
	return ret;
err_in_read:
	doc_read_page_finish(docg3);
	goto out;
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
					       buf, 1, 0);
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

	calc_block_sector(from, &block0, &block1, &page, &ofs,
		docg3->reliable);
	doc_dbg("doc_block_isbad(from=%lld) => block=(%d,%d), page=%d, ofs=%d\n",
		from, block0, block1, page, ofs);

	if (block0 < DOC_LAYOUT_BLOCK_FIRST_DATA)
		return 0;
	if (block1 > docg3->max_block)
		return -EINVAL;

	is_good = docg3->bbt[block0 >> 3] & (1 << (block0 & 0x7));
	return !is_good;
}

#if 0
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
	calc_block_sector(from, &block0, &block1, &page, &ofs, docg3->reliable);
	if (block1 > docg3->max_block)
		return -EINVAL;

	ret = doc_reset_seq(docg3);
	if (!ret)
		ret = doc_read_page_prepare(docg3, block0, block1, page,
					    ofs + DOC_LAYOUT_WEAR_OFFSET, 0);
	if (!ret)
		ret = doc_read_page_getbytes(docg3, DOC_LAYOUT_WEAR_SIZE,
					     buf, 1, 0);
	doc_read_page_finish(docg3);

	if (ret || (buf[0] != DOC_ERASE_MARK) || (buf[2] != DOC_ERASE_MARK))
		return -EIO;
	plane1_erase_count = (u8)(~buf[1]) | ((u8)(~buf[4]) << 8)
		| ((u8)(~buf[5]) << 16);
	plane2_erase_count = (u8)(~buf[3]) | ((u8)(~buf[6]) << 8)
		| ((u8)(~buf[7]) << 16);

	return max(plane1_erase_count, plane2_erase_count);
}
#endif

/**
 * doc_get_op_status - get erase/write operation status
 * @docg3: the device
 *
 * Queries the status from the chip, and returns it
 *
 * Returns the status (bits DOC_PLANES_STATUS_*)
 */
static int doc_get_op_status(struct docg3 *docg3)
{
	u8 status;

	doc_flash_sequence(docg3, DOC_SEQ_PLANES_STATUS);
	doc_flash_command(docg3, DOC_CMD_PLANES_STATUS);
	doc_delay(docg3, 5);

	doc_ecc_disable(docg3);
	doc_read_data_area(docg3, &status, 1, 1);
	return status;
}

/**
 * doc_write_erase_wait_status - wait for write or erase completion
 * @docg3: the device
 *
 * Wait for the chip to be ready again after erase or write operation, and check
 * erase/write status.
 *
 * Returns 0 if erase successful, -EIO if erase/write issue, -ETIMEOUT if
 * timeout
 */
static int doc_write_erase_wait_status(struct docg3 *docg3)
{
	int i, status, ret = 0;

	for (i = 0; !doc_is_ready(docg3) && i < 5; i++)
		msleep(20);
	if (!doc_is_ready(docg3)) {
		doc_dbg("Timeout reached and the chip is still not ready\n");
		ret = -EAGAIN;
		goto out;
	}

	status = doc_get_op_status(docg3);
	if (status & DOC_PLANES_STATUS_FAIL) {
		doc_dbg("Erase/Write failed on (a) plane(s), status = %x\n",
			status);
		ret = -EIO;
	}

out:
	doc_page_finish(docg3);
	return ret;
}

/**
 * doc_erase_block - Erase a couple of blocks
 * @docg3: the device
 * @block0: the first block to erase (leftmost plane)
 * @block1: the second block to erase (rightmost plane)
 *
 * Erase both blocks, and return operation status
 *
 * Returns 0 if erase successful, -EIO if erase issue, -ETIMEOUT if chip not
 * ready for too long
 */
static int doc_erase_block(struct docg3 *docg3, int block0, int block1)
{
	int ret, sector;

	doc_dbg("doc_erase_block(blocks=(%d,%d))\n", block0, block1);
	ret = doc_reset_seq(docg3);
	if (ret)
		return -EIO;

	doc_set_reliable_mode(docg3);
	doc_flash_sequence(docg3, DOC_SEQ_ERASE);

	sector = block0 << DOC_ADDR_BLOCK_SHIFT;
	doc_flash_command(docg3, DOC_CMD_PROG_BLOCK_ADDR);
	doc_setup_addr_sector(docg3, sector);
	sector = block1 << DOC_ADDR_BLOCK_SHIFT;
	doc_flash_command(docg3, DOC_CMD_PROG_BLOCK_ADDR);
	doc_setup_addr_sector(docg3, sector);
	doc_delay(docg3, 1);

	doc_flash_command(docg3, DOC_CMD_ERASECYCLE2);
	doc_delay(docg3, 2);

	if (is_prot_seq_error(docg3)) {
		doc_err("Erase blocks %d,%d error\n", block0, block1);
		return -EIO;
	}

	return doc_write_erase_wait_status(docg3);
}

/**
 * doc_erase - Erase a portion of the chip
 * @mtd: the device
 * @info: the erase info
 *
 * Erase a bunch of contiguous blocks, by pairs, as a "mtd" page of 1024 is
 * split into 2 pages of 512 bytes on 2 contiguous blocks.
 *
 * Returns 0 if erase successful, -EINVAL if addressing error, -EIO if erase
 * issue
 */
static int doc_erase(struct mtd_info *mtd, struct erase_info *info)
{
	struct docg3 *docg3 = mtd->priv;
	uint64_t len;
	int block0, block1, page, ret = 0, ofs = 0;

	doc_dbg("doc_erase(from=%lld, len=%lld\n", info->addr, info->len);

	calc_block_sector(info->addr + info->len, &block0, &block1, &page,
			  &ofs, docg3->reliable);
	if (info->addr + info->len > mtd->size || page || ofs)
		return -EINVAL;

	calc_block_sector(info->addr, &block0, &block1, &page, &ofs,
			  docg3->reliable);
	mutex_lock(&docg3->cascade->lock);
	doc_set_device_id(docg3, docg3->device_id);
	doc_set_reliable_mode(docg3);
	for (len = info->len; !ret && len > 0; len -= mtd->erasesize) {
		ret = doc_erase_block(docg3, block0, block1);
		block0 += 2;
		block1 += 2;
	}
	mutex_unlock(&docg3->cascade->lock);

	return ret;
}

/**
 * doc_write_page - Write a single page to the chip
 * @docg3: the device
 * @to: the offset from first block and first page, in bytes, aligned on page
 *      size
 * @buf: buffer to get bytes from
 * @oob: buffer to get out of band bytes from (can be NULL if no OOB should be
 *       written)
 * @autoecc: if 0, all 16 bytes from OOB are taken, regardless of HW Hamming or
 *           BCH computations. If 1, only bytes 0-7 and byte 15 are taken,
 *           remaining ones are filled with hardware Hamming and BCH
 *           computations. Its value is not meaningfull is oob == NULL.
 *
 * Write one full page (ie. 1 page split on two planes), of 512 bytes, with the
 * OOB data. The OOB ECC is automatically computed by the hardware Hamming and
 * BCH generator if autoecc is not null.
 *
 * Returns 0 if write successful, -EIO if write error, -EAGAIN if timeout
 */
static int doc_write_page(struct docg3 *docg3, loff_t to, const u_char *buf,
			  const u_char *oob, int autoecc)
{
	int block0, block1, page, ret, ofs = 0;
	u8 hwecc[DOC_ECC_BCH_SIZE], hamming;

	doc_dbg("doc_write_page(to=%lld)\n", to);
	calc_block_sector(to, &block0, &block1, &page, &ofs, docg3->reliable);

	doc_set_device_id(docg3, docg3->device_id);
	ret = doc_reset_seq(docg3);
	if (ret)
		goto err;

	/* Program the flash address block and page */
	ret = doc_write_seek(docg3, block0, block1, page, ofs);
	if (ret)
		goto err;

	doc_write_page_ecc_init(docg3, DOC_ECC_BCH_TOTAL_BYTES);
	doc_delay(docg3, 2);
	doc_write_page_putbytes(docg3, DOC_LAYOUT_PAGE_SIZE, buf);

	if (oob && autoecc) {
		doc_write_page_putbytes(docg3, DOC_LAYOUT_OOB_PAGEINFO_SZ, oob);
		doc_delay(docg3, 2);
		oob += DOC_LAYOUT_OOB_UNUSED_OFS;

		hamming = doc_register_readb(docg3, DOC_HAMMINGPARITY);
		doc_delay(docg3, 2);
		doc_write_page_putbytes(docg3, DOC_LAYOUT_OOB_HAMMING_SZ,
					&hamming);
		doc_delay(docg3, 2);

		doc_get_bch_hw_ecc(docg3, hwecc);
		doc_write_page_putbytes(docg3, DOC_LAYOUT_OOB_BCH_SZ, hwecc);
		doc_delay(docg3, 2);

		doc_write_page_putbytes(docg3, DOC_LAYOUT_OOB_UNUSED_SZ, oob);
	}
	if (oob && !autoecc)
		doc_write_page_putbytes(docg3, DOC_LAYOUT_OOB_SIZE, oob);

	doc_delay(docg3, 2);
	doc_page_finish(docg3);
	doc_delay(docg3, 2);
	doc_flash_command(docg3, DOC_CMD_PROG_CYCLE2);
	doc_delay(docg3, 2);

	/*
	 * The wait status will perform another doc_page_finish() call, but that
	 * seems to please the docg3, so leave it.
	 */
	ret = doc_write_erase_wait_status(docg3);
	return ret;
err:
	doc_read_page_finish(docg3);
	return ret;
}

/**
 * doc_guess_autoecc - Guess autoecc mode from mbd_oob_ops
 * @ops: the oob operations
 *
 * Returns 0 or 1 if success, -EINVAL if invalid oob mode
 */
static int doc_guess_autoecc(struct mtd_oob_ops *ops)
{
	int autoecc;

	switch (ops->mode) {
	case MTD_OPS_PLACE_OOB:
	case MTD_OPS_AUTO_OOB:
		autoecc = 1;
		break;
	case MTD_OPS_RAW:
		autoecc = 0;
		break;
	default:
		autoecc = -EINVAL;
	}
	return autoecc;
}

/**
 * doc_fill_autooob - Fill a 16 bytes OOB from 8 non-ECC bytes
 * @dst: the target 16 bytes OOB buffer
 * @oobsrc: the source 8 bytes non-ECC OOB buffer
 *
 */
static void doc_fill_autooob(u8 *dst, u8 *oobsrc)
{
	memcpy(dst, oobsrc, DOC_LAYOUT_OOB_PAGEINFO_SZ);
	dst[DOC_LAYOUT_OOB_UNUSED_OFS] = oobsrc[DOC_LAYOUT_OOB_PAGEINFO_SZ];
}

/**
 * doc_backup_oob - Backup OOB into docg3 structure
 * @docg3: the device
 * @to: the page offset in the chip
 * @ops: the OOB size and buffer
 *
 * As the docg3 should write a page with its OOB in one pass, and some userland
 * applications do write_oob() to setup the OOB and then write(), store the OOB
 * into a temporary storage. This is very dangerous, as 2 concurrent
 * applications could store an OOB, and then write their pages (which will
 * result into one having its OOB corrupted).
 *
 * The only reliable way would be for userland to call doc_write_oob() with both
 * the page data _and_ the OOB area.
 *
 * Returns 0 if success, -EINVAL if ops content invalid
 */
static int doc_backup_oob(struct docg3 *docg3, loff_t to,
			  struct mtd_oob_ops *ops)
{
	int ooblen = ops->ooblen, autoecc;

	if (ooblen != DOC_LAYOUT_OOB_SIZE)
		return -EINVAL;
	autoecc = doc_guess_autoecc(ops);
	if (autoecc < 0)
		return autoecc;

	docg3->oob_write_ofs = to;
	docg3->oob_autoecc = autoecc;
	if (ops->mode == MTD_OPS_AUTO_OOB) {
		doc_fill_autooob(docg3->oob_write_buf, ops->oobbuf);
		ops->oobretlen = 8;
	} else {
		memcpy(docg3->oob_write_buf, ops->oobbuf, DOC_LAYOUT_OOB_SIZE);
		ops->oobretlen = DOC_LAYOUT_OOB_SIZE;
	}
	return 0;
}

/**
 * doc_write_oob - Write out of band bytes to flash
 * @mtd: the device
 * @ofs: the offset from first block and first page, in bytes, aligned on page
 *       size
 * @ops: the mtd oob structure
 *
 * Either write OOB data into a temporary buffer, for the subsequent write
 * page. The provided OOB should be 16 bytes long. If a data buffer is provided
 * as well, issue the page write.
 * Or provide data without OOB, and then a all zeroed OOB will be used (ECC will
 * still be filled in if asked for).
 *
 * Returns 0 is successful, EINVAL if length is not 14 bytes
 */
static int doc_write_oob(struct mtd_info *mtd, loff_t ofs,
			 struct mtd_oob_ops *ops)
{
	struct docg3 *docg3 = mtd->priv;
	int ret, autoecc, oobdelta;
	u8 *oobbuf = ops->oobbuf;
	u8 *buf = ops->datbuf;
	size_t len, ooblen;
	u8 oob[DOC_LAYOUT_OOB_SIZE];

	if (buf)
		len = ops->len;
	else
		len = 0;
	if (oobbuf)
		ooblen = ops->ooblen;
	else
		ooblen = 0;

	if (oobbuf && ops->mode == MTD_OPS_PLACE_OOB)
		oobbuf += ops->ooboffs;

	doc_dbg("doc_write_oob(from=%lld, mode=%d, data=(%p:%zu), oob=(%p:%zu))\n",
		ofs, ops->mode, buf, len, oobbuf, ooblen);
	switch (ops->mode) {
	case MTD_OPS_PLACE_OOB:
	case MTD_OPS_RAW:
		oobdelta = mtd->oobsize;
		break;
	case MTD_OPS_AUTO_OOB:
		oobdelta = mtd->oobavail;
		break;
	default:
		return -EINVAL;
	}
	if ((len % DOC_LAYOUT_PAGE_SIZE) || (ooblen % oobdelta) ||
	    (ofs % DOC_LAYOUT_PAGE_SIZE))
		return -EINVAL;
	if (len && ooblen &&
	    (len / DOC_LAYOUT_PAGE_SIZE) != (ooblen / oobdelta))
		return -EINVAL;

	ops->oobretlen = 0;
	ops->retlen = 0;
	ret = 0;
	if (len == 0 && ooblen == 0)
		return -EINVAL;
	if (len == 0 && ooblen > 0)
		return doc_backup_oob(docg3, ofs, ops);

	autoecc = doc_guess_autoecc(ops);
	if (autoecc < 0)
		return autoecc;

	mutex_lock(&docg3->cascade->lock);
	while (!ret && len > 0) {
		memset(oob, 0, sizeof(oob));
		if (ofs == docg3->oob_write_ofs)
			memcpy(oob, docg3->oob_write_buf, DOC_LAYOUT_OOB_SIZE);
		else if (ooblen > 0 && ops->mode == MTD_OPS_AUTO_OOB)
			doc_fill_autooob(oob, oobbuf);
		else if (ooblen > 0)
			memcpy(oob, oobbuf, DOC_LAYOUT_OOB_SIZE);
		ret = doc_write_page(docg3, ofs, buf, oob, autoecc);

		ofs += DOC_LAYOUT_PAGE_SIZE;
		len -= DOC_LAYOUT_PAGE_SIZE;
		buf += DOC_LAYOUT_PAGE_SIZE;
		if (ooblen) {
			oobbuf += oobdelta;
			ooblen -= oobdelta;
			ops->oobretlen += oobdelta;
		}
		ops->retlen += DOC_LAYOUT_PAGE_SIZE;
	}

	doc_set_device_id(docg3, 0);
	mutex_unlock(&docg3->cascade->lock);
	return ret;
}

static struct docg3 *sysfs_dev2docg3(struct device *dev,
				     struct device_attribute *attr)
{
	int floor;
	struct mtd_info **docg3_floors = dev_get_drvdata(dev);

	floor = attr->attr.name[1] - '0';
	if (floor < 0 || floor >= DOC_MAX_NBFLOORS)
		return NULL;
	else
		return docg3_floors[floor]->priv;
}

static ssize_t dps0_is_key_locked(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct docg3 *docg3 = sysfs_dev2docg3(dev, attr);
	int dps0;

	mutex_lock(&docg3->cascade->lock);
	doc_set_device_id(docg3, docg3->device_id);
	dps0 = doc_register_readb(docg3, DOC_DPS0_STATUS);
	doc_set_device_id(docg3, 0);
	mutex_unlock(&docg3->cascade->lock);

	return sprintf(buf, "%d\n", !(dps0 & DOC_DPS_KEY_OK));
}

static ssize_t dps1_is_key_locked(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct docg3 *docg3 = sysfs_dev2docg3(dev, attr);
	int dps1;

	mutex_lock(&docg3->cascade->lock);
	doc_set_device_id(docg3, docg3->device_id);
	dps1 = doc_register_readb(docg3, DOC_DPS1_STATUS);
	doc_set_device_id(docg3, 0);
	mutex_unlock(&docg3->cascade->lock);

	return sprintf(buf, "%d\n", !(dps1 & DOC_DPS_KEY_OK));
}

static ssize_t dps0_insert_key(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct docg3 *docg3 = sysfs_dev2docg3(dev, attr);
	int i;

	if (count != DOC_LAYOUT_DPS_KEY_LENGTH)
		return -EINVAL;

	mutex_lock(&docg3->cascade->lock);
	doc_set_device_id(docg3, docg3->device_id);
	for (i = 0; i < DOC_LAYOUT_DPS_KEY_LENGTH; i++)
		doc_writeb(docg3, buf[i], DOC_DPS0_KEY);
	doc_set_device_id(docg3, 0);
	mutex_unlock(&docg3->cascade->lock);
	return count;
}

static ssize_t dps1_insert_key(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct docg3 *docg3 = sysfs_dev2docg3(dev, attr);
	int i;

	if (count != DOC_LAYOUT_DPS_KEY_LENGTH)
		return -EINVAL;

	mutex_lock(&docg3->cascade->lock);
	doc_set_device_id(docg3, docg3->device_id);
	for (i = 0; i < DOC_LAYOUT_DPS_KEY_LENGTH; i++)
		doc_writeb(docg3, buf[i], DOC_DPS1_KEY);
	doc_set_device_id(docg3, 0);
	mutex_unlock(&docg3->cascade->lock);
	return count;
}

#define FLOOR_SYSFS(id) { \
	__ATTR(f##id##_dps0_is_keylocked, S_IRUGO, dps0_is_key_locked, NULL), \
	__ATTR(f##id##_dps1_is_keylocked, S_IRUGO, dps1_is_key_locked, NULL), \
	__ATTR(f##id##_dps0_protection_key, S_IWUSR|S_IWGRP, NULL, dps0_insert_key), \
	__ATTR(f##id##_dps1_protection_key, S_IWUSR|S_IWGRP, NULL, dps1_insert_key), \
}

static struct device_attribute doc_sys_attrs[DOC_MAX_NBFLOORS][4] = {
	FLOOR_SYSFS(0), FLOOR_SYSFS(1), FLOOR_SYSFS(2), FLOOR_SYSFS(3)
};

static int doc_register_sysfs(struct platform_device *pdev,
			      struct docg3_cascade *cascade)
{
	struct device *dev = &pdev->dev;
	int floor;
	int ret;
	int i;

	for (floor = 0;
	     floor < DOC_MAX_NBFLOORS && cascade->floors[floor];
	     floor++) {
		for (i = 0; i < 4; i++) {
			ret = device_create_file(dev, &doc_sys_attrs[floor][i]);
			if (ret)
				goto remove_files;
		}
	}

	return 0;

remove_files:
	do {
		while (--i >= 0)
			device_remove_file(dev, &doc_sys_attrs[floor][i]);
		i = 4;
	} while (--floor >= 0);

	return ret;
}

static void doc_unregister_sysfs(struct platform_device *pdev,
				 struct docg3_cascade *cascade)
{
	struct device *dev = &pdev->dev;
	int floor, i;

	for (floor = 0; floor < DOC_MAX_NBFLOORS && cascade->floors[floor];
	     floor++)
		for (i = 0; i < 4; i++)
			device_remove_file(dev, &doc_sys_attrs[floor][i]);
}

/*
 * Debug sysfs entries
 */
static int dbg_flashctrl_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;

	u8 fctrl;

	mutex_lock(&docg3->cascade->lock);
	fctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);
	mutex_unlock(&docg3->cascade->lock);

	seq_printf(s, "FlashControl : 0x%02x (%s,CE# %s,%s,%s,flash %s)\n",
		   fctrl,
		   fctrl & DOC_CTRL_VIOLATION ? "protocol violation" : "-",
		   fctrl & DOC_CTRL_CE ? "active" : "inactive",
		   fctrl & DOC_CTRL_PROTECTION_ERROR ? "protection error" : "-",
		   fctrl & DOC_CTRL_SEQUENCE_ERROR ? "sequence error" : "-",
		   fctrl & DOC_CTRL_FLASHREADY ? "ready" : "not ready");

	return 0;
}
DEBUGFS_RO_ATTR(flashcontrol, dbg_flashctrl_show);

static int dbg_asicmode_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;

	int pctrl, mode;

	mutex_lock(&docg3->cascade->lock);
	pctrl = doc_register_readb(docg3, DOC_ASICMODE);
	mode = pctrl & 0x03;
	mutex_unlock(&docg3->cascade->lock);

	seq_printf(s,
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
		seq_puts(s, "reset");
		break;
	case DOC_ASICMODE_NORMAL:
		seq_puts(s, "normal");
		break;
	case DOC_ASICMODE_POWERDOWN:
		seq_puts(s, "powerdown");
		break;
	}
	seq_puts(s, ")\n");
	return 0;
}
DEBUGFS_RO_ATTR(asic_mode, dbg_asicmode_show);

static int dbg_device_id_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;
	int id;

	mutex_lock(&docg3->cascade->lock);
	id = doc_register_readb(docg3, DOC_DEVICESELECT);
	mutex_unlock(&docg3->cascade->lock);

	seq_printf(s, "DeviceId = %d\n", id);
	return 0;
}
DEBUGFS_RO_ATTR(device_id, dbg_device_id_show);

static int dbg_protection_show(struct seq_file *s, void *p)
{
	struct docg3 *docg3 = (struct docg3 *)s->private;
	int protect, dps0, dps0_low, dps0_high, dps1, dps1_low, dps1_high;

	mutex_lock(&docg3->cascade->lock);
	protect = doc_register_readb(docg3, DOC_PROTECTION);
	dps0 = doc_register_readb(docg3, DOC_DPS0_STATUS);
	dps0_low = doc_register_readw(docg3, DOC_DPS0_ADDRLOW);
	dps0_high = doc_register_readw(docg3, DOC_DPS0_ADDRHIGH);
	dps1 = doc_register_readb(docg3, DOC_DPS1_STATUS);
	dps1_low = doc_register_readw(docg3, DOC_DPS1_ADDRLOW);
	dps1_high = doc_register_readw(docg3, DOC_DPS1_ADDRHIGH);
	mutex_unlock(&docg3->cascade->lock);

	seq_printf(s, "Protection = 0x%02x (", protect);
	if (protect & DOC_PROTECT_FOUNDRY_OTP_LOCK)
		seq_puts(s, "FOUNDRY_OTP_LOCK,");
	if (protect & DOC_PROTECT_CUSTOMER_OTP_LOCK)
		seq_puts(s, "CUSTOMER_OTP_LOCK,");
	if (protect & DOC_PROTECT_LOCK_INPUT)
		seq_puts(s, "LOCK_INPUT,");
	if (protect & DOC_PROTECT_STICKY_LOCK)
		seq_puts(s, "STICKY_LOCK,");
	if (protect & DOC_PROTECT_PROTECTION_ENABLED)
		seq_puts(s, "PROTECTION ON,");
	if (protect & DOC_PROTECT_IPL_DOWNLOAD_LOCK)
		seq_puts(s, "IPL_DOWNLOAD_LOCK,");
	if (protect & DOC_PROTECT_PROTECTION_ERROR)
		seq_puts(s, "PROTECT_ERR,");
	else
		seq_puts(s, "NO_PROTECT_ERR");
	seq_puts(s, ")\n");

	seq_printf(s, "DPS0 = 0x%02x : Protected area [0x%x - 0x%x] : OTP=%d, READ=%d, WRITE=%d, HW_LOCK=%d, KEY_OK=%d\n",
		   dps0, dps0_low, dps0_high,
		   !!(dps0 & DOC_DPS_OTP_PROTECTED),
		   !!(dps0 & DOC_DPS_READ_PROTECTED),
		   !!(dps0 & DOC_DPS_WRITE_PROTECTED),
		   !!(dps0 & DOC_DPS_HW_LOCK_ENABLED),
		   !!(dps0 & DOC_DPS_KEY_OK));
	seq_printf(s, "DPS1 = 0x%02x : Protected area [0x%x - 0x%x] : OTP=%d, READ=%d, WRITE=%d, HW_LOCK=%d, KEY_OK=%d\n",
		   dps1, dps1_low, dps1_high,
		   !!(dps1 & DOC_DPS_OTP_PROTECTED),
		   !!(dps1 & DOC_DPS_READ_PROTECTED),
		   !!(dps1 & DOC_DPS_WRITE_PROTECTED),
		   !!(dps1 & DOC_DPS_HW_LOCK_ENABLED),
		   !!(dps1 & DOC_DPS_KEY_OK));
	return 0;
}
DEBUGFS_RO_ATTR(protection, dbg_protection_show);

static void __init doc_dbg_register(struct mtd_info *floor)
{
	struct dentry *root = floor->dbg.dfs_dir;
	struct docg3 *docg3 = floor->priv;

	if (IS_ERR_OR_NULL(root)) {
		if (IS_ENABLED(CONFIG_DEBUG_FS) &&
		    !IS_ENABLED(CONFIG_MTD_PARTITIONED_MASTER))
			dev_warn(floor->dev.parent,
				 "CONFIG_MTD_PARTITIONED_MASTER must be enabled to expose debugfs stuff\n");
		return;
	}

	debugfs_create_file("docg3_flashcontrol", S_IRUSR, root, docg3,
			    &flashcontrol_fops);
	debugfs_create_file("docg3_asic_mode", S_IRUSR, root, docg3,
			    &asic_mode_fops);
	debugfs_create_file("docg3_device_id", S_IRUSR, root, docg3,
			    &device_id_fops);
	debugfs_create_file("docg3_protection", S_IRUSR, root, docg3,
			    &protection_fops);
}

/**
 * doc_set_driver_info - Fill the mtd_info structure and docg3 structure
 * @chip_id: The chip ID of the supported chip
 * @mtd: The structure to fill
 */
static int __init doc_set_driver_info(int chip_id, struct mtd_info *mtd)
{
	struct docg3 *docg3 = mtd->priv;
	int cfg;

	cfg = doc_register_readb(docg3, DOC_CONFIGURATION);
	docg3->if_cfg = (cfg & DOC_CONF_IF_CFG ? 1 : 0);
	docg3->reliable = reliable_mode;

	switch (chip_id) {
	case DOC_CHIPID_G3:
		mtd->name = kasprintf(GFP_KERNEL, "docg3.%d",
				      docg3->device_id);
		if (!mtd->name)
			return -ENOMEM;
		docg3->max_block = 2047;
		break;
	}
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
	mtd->size = (docg3->max_block + 1) * DOC_LAYOUT_BLOCK_SIZE;
	if (docg3->reliable == 2)
		mtd->size /= 2;
	mtd->erasesize = DOC_LAYOUT_BLOCK_SIZE * DOC_LAYOUT_NBPLANES;
	if (docg3->reliable == 2)
		mtd->erasesize /= 2;
	mtd->writebufsize = mtd->writesize = DOC_LAYOUT_PAGE_SIZE;
	mtd->oobsize = DOC_LAYOUT_OOB_SIZE;
	mtd->_erase = doc_erase;
	mtd->_read_oob = doc_read_oob;
	mtd->_write_oob = doc_write_oob;
	mtd->_block_isbad = doc_block_isbad;
	mtd_set_ooblayout(mtd, &nand_ooblayout_docg3_ops);
	mtd->oobavail = 8;
	mtd->ecc_strength = DOC_ECC_BCH_T;

	return 0;
}

/**
 * doc_probe_device - Check if a device is available
 * @base: the io space where the device is probed
 * @floor: the floor of the probed device
 * @dev: the device
 * @cascade: the cascade of chips this devices will belong to
 *
 * Checks whether a device at the specified IO range, and floor is available.
 *
 * Returns a mtd_info struct if there is a device, ENODEV if none found, ENOMEM
 * if a memory allocation failed. If floor 0 is checked, a reset of the ASIC is
 * launched.
 */
static struct mtd_info * __init
doc_probe_device(struct docg3_cascade *cascade, int floor, struct device *dev)
{
	int ret, bbt_nbpages;
	u16 chip_id, chip_id_inv;
	struct docg3 *docg3;
	struct mtd_info *mtd;

	ret = -ENOMEM;
	docg3 = kzalloc(sizeof(struct docg3), GFP_KERNEL);
	if (!docg3)
		goto nomem1;
	mtd = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!mtd)
		goto nomem2;
	mtd->priv = docg3;
	mtd->dev.parent = dev;
	bbt_nbpages = DIV_ROUND_UP(docg3->max_block + 1,
				   8 * DOC_LAYOUT_PAGE_SIZE);
	docg3->bbt = kcalloc(DOC_LAYOUT_PAGE_SIZE, bbt_nbpages, GFP_KERNEL);
	if (!docg3->bbt)
		goto nomem3;

	docg3->dev = dev;
	docg3->device_id = floor;
	docg3->cascade = cascade;
	doc_set_device_id(docg3, docg3->device_id);
	if (!floor)
		doc_set_asic_mode(docg3, DOC_ASICMODE_RESET);
	doc_set_asic_mode(docg3, DOC_ASICMODE_NORMAL);

	chip_id = doc_register_readw(docg3, DOC_CHIPID);
	chip_id_inv = doc_register_readw(docg3, DOC_CHIPID_INV);

	ret = 0;
	if (chip_id != (u16)(~chip_id_inv)) {
		goto nomem4;
	}

	switch (chip_id) {
	case DOC_CHIPID_G3:
		doc_info("Found a G3 DiskOnChip at addr %p, floor %d\n",
			 docg3->cascade->base, floor);
		break;
	default:
		doc_err("Chip id %04x is not a DiskOnChip G3 chip\n", chip_id);
		goto nomem4;
	}

	ret = doc_set_driver_info(chip_id, mtd);
	if (ret)
		goto nomem4;

	doc_hamming_ecc_init(docg3, DOC_LAYOUT_OOB_PAGEINFO_SZ);
	doc_reload_bbt(docg3);
	return mtd;

nomem4:
	kfree(docg3->bbt);
nomem3:
	kfree(mtd);
nomem2:
	kfree(docg3);
nomem1:
	return ERR_PTR(ret);
}

/**
 * doc_release_device - Release a docg3 floor
 * @mtd: the device
 */
static void doc_release_device(struct mtd_info *mtd)
{
	struct docg3 *docg3 = mtd->priv;

	mtd_device_unregister(mtd);
	kfree(docg3->bbt);
	kfree(docg3);
	kfree(mtd->name);
	kfree(mtd);
}

/**
 * docg3_resume - Awakens docg3 floor
 * @pdev: platfrom device
 *
 * Returns 0 (always successful)
 */
static int docg3_resume(struct platform_device *pdev)
{
	int i;
	struct docg3_cascade *cascade;
	struct mtd_info **docg3_floors, *mtd;
	struct docg3 *docg3;

	cascade = platform_get_drvdata(pdev);
	docg3_floors = cascade->floors;
	mtd = docg3_floors[0];
	docg3 = mtd->priv;

	doc_dbg("docg3_resume()\n");
	for (i = 0; i < 12; i++)
		doc_readb(docg3, DOC_IOSPACE_IPL);
	return 0;
}

/**
 * docg3_suspend - Put in low power mode the docg3 floor
 * @pdev: platform device
 * @state: power state
 *
 * Shuts off most of docg3 circuitery to lower power consumption.
 *
 * Returns 0 if suspend succeeded, -EIO if chip refused suspend
 */
static int docg3_suspend(struct platform_device *pdev, pm_message_t state)
{
	int floor, i;
	struct docg3_cascade *cascade;
	struct mtd_info **docg3_floors, *mtd;
	struct docg3 *docg3;
	u8 ctrl, pwr_down;

	cascade = platform_get_drvdata(pdev);
	docg3_floors = cascade->floors;
	for (floor = 0; floor < DOC_MAX_NBFLOORS; floor++) {
		mtd = docg3_floors[floor];
		if (!mtd)
			continue;
		docg3 = mtd->priv;

		doc_writeb(docg3, floor, DOC_DEVICESELECT);
		ctrl = doc_register_readb(docg3, DOC_FLASHCONTROL);
		ctrl &= ~DOC_CTRL_VIOLATION & ~DOC_CTRL_CE;
		doc_writeb(docg3, ctrl, DOC_FLASHCONTROL);

		for (i = 0; i < 10; i++) {
			usleep_range(3000, 4000);
			pwr_down = doc_register_readb(docg3, DOC_POWERMODE);
			if (pwr_down & DOC_POWERDOWN_READY)
				break;
		}
		if (pwr_down & DOC_POWERDOWN_READY) {
			doc_dbg("docg3_suspend(): floor %d powerdown ok\n",
				floor);
		} else {
			doc_err("docg3_suspend(): floor %d powerdown failed\n",
				floor);
			return -EIO;
		}
	}

	mtd = docg3_floors[0];
	docg3 = mtd->priv;
	doc_set_asic_mode(docg3, DOC_ASICMODE_POWERDOWN);
	return 0;
}

/**
 * doc_probe - Probe the IO space for a DiskOnChip G3 chip
 * @pdev: platform device
 *
 * Probes for a G3 chip at the specified IO space in the platform data
 * ressources. The floor 0 must be available.
 *
 * Returns 0 on success, -ENOMEM, -ENXIO on error
 */
static int __init docg3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtd_info *mtd;
	struct resource *ress;
	void __iomem *base;
	int ret, floor;
	struct docg3_cascade *cascade;

	ret = -ENXIO;
	ress = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ress) {
		dev_err(dev, "No I/O memory resource defined\n");
		return ret;
	}
	base = devm_ioremap(dev, ress->start, DOC_IOSPACE_SIZE);

	ret = -ENOMEM;
	cascade = devm_kcalloc(dev, DOC_MAX_NBFLOORS, sizeof(*cascade),
			       GFP_KERNEL);
	if (!cascade)
		return ret;
	cascade->base = base;
	mutex_init(&cascade->lock);
	cascade->bch = init_bch(DOC_ECC_BCH_M, DOC_ECC_BCH_T,
			     DOC_ECC_BCH_PRIMPOLY);
	if (!cascade->bch)
		return ret;

	for (floor = 0; floor < DOC_MAX_NBFLOORS; floor++) {
		mtd = doc_probe_device(cascade, floor, dev);
		if (IS_ERR(mtd)) {
			ret = PTR_ERR(mtd);
			goto err_probe;
		}
		if (!mtd) {
			if (floor == 0)
				goto notfound;
			else
				continue;
		}
		cascade->floors[floor] = mtd;
		ret = mtd_device_parse_register(mtd, part_probes, NULL, NULL,
						0);
		if (ret)
			goto err_probe;

		doc_dbg_register(cascade->floors[floor]);
	}

	ret = doc_register_sysfs(pdev, cascade);
	if (ret)
		goto err_probe;

	platform_set_drvdata(pdev, cascade);
	return 0;

notfound:
	ret = -ENODEV;
	dev_info(dev, "No supported DiskOnChip found\n");
err_probe:
	free_bch(cascade->bch);
	for (floor = 0; floor < DOC_MAX_NBFLOORS; floor++)
		if (cascade->floors[floor])
			doc_release_device(cascade->floors[floor]);
	return ret;
}

/**
 * docg3_release - Release the driver
 * @pdev: the platform device
 *
 * Returns 0
 */
static int docg3_release(struct platform_device *pdev)
{
	struct docg3_cascade *cascade = platform_get_drvdata(pdev);
	struct docg3 *docg3 = cascade->floors[0]->priv;
	int floor;

	doc_unregister_sysfs(pdev, cascade);
	for (floor = 0; floor < DOC_MAX_NBFLOORS; floor++)
		if (cascade->floors[floor])
			doc_release_device(cascade->floors[floor]);

	free_bch(docg3->cascade->bch);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id docg3_dt_ids[] = {
	{ .compatible = "m-systems,diskonchip-g3" },
	{}
};
MODULE_DEVICE_TABLE(of, docg3_dt_ids);
#endif

static struct platform_driver g3_driver = {
	.driver		= {
		.name	= "docg3",
		.of_match_table = of_match_ptr(docg3_dt_ids),
	},
	.suspend	= docg3_suspend,
	.resume		= docg3_resume,
	.remove		= docg3_release,
};

module_platform_driver_probe(g3_driver, docg3_probe);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
MODULE_DESCRIPTION("MTD driver for DiskOnChip G3");

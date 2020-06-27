/*
 *  Copyright © 2012 Mike Dunn <mikedunn@newsguy.com>
 *
 * mtd nand driver for M-Systems DiskOnChip G4
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Tested on the Palm Treo 680.  The G4 is also present on Toshiba Portege, Asus
 * P526, some HTC smartphones (Wizard, Prophet, ...), O2 XDA Zinc, maybe others.
 * Should work on these as well.  Let me know!
 *
 * TODO:
 *
 *  Mechanism for management of password-protected areas
 *
 *  Hamming ecc when reading oob only
 *
 *  According to the M-Sys documentation, this device is also available in a
 *  "dual-die" configuration having a 256MB capacity, but no mechanism for
 *  detecting this variant is documented.  Currently this driver assumes 128MB
 *  capacity.
 *
 *  Support for multiple cascaded devices ("floors").  Not sure which gadgets
 *  contain multiple G4s in a cascaded configuration, if any.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/bch.h>
#include <linux/bitrev.h>
#include <linux/jiffies.h>

/*
 * In "reliable mode" consecutive 2k pages are used in parallel (in some
 * fashion) to store the same data.  The data can be read back from the
 * even-numbered pages in the normal manner; odd-numbered pages will appear to
 * contain junk.  Systems that boot from the docg4 typically write the secondary
 * program loader (SPL) code in this mode.  The SPL is loaded by the initial
 * program loader (IPL, stored in the docg4's 2k NOR-like region that is mapped
 * to the reset vector address).  This module parameter enables you to use this
 * driver to write the SPL.  When in this mode, no more than 2k of data can be
 * written at a time, because the addresses do not increment in the normal
 * manner, and the starting offset must be within an even-numbered 2k region;
 * i.e., invalid starting offsets are 0x800, 0xa00, 0xc00, 0xe00, 0x1800,
 * 0x1a00, ...  Reliable mode is a special case and should not be used unless
 * you know what you're doing.
 */
static bool reliable_mode;
module_param(reliable_mode, bool, 0);
MODULE_PARM_DESC(reliable_mode, "pages are programmed in reliable mode");

/*
 * You'll want to ignore badblocks if you're reading a partition that contains
 * data written by the TrueFFS library (i.e., by PalmOS, Windows, etc), since
 * it does not use mtd nand's method for marking bad blocks (using oob area).
 * This will also skip the check of the "page written" flag.
 */
static bool ignore_badblocks;
module_param(ignore_badblocks, bool, 0);
MODULE_PARM_DESC(ignore_badblocks, "no badblock checking performed");

struct docg4_priv {
	struct mtd_info	*mtd;
	struct device *dev;
	void __iomem *virtadr;
	int status;
	struct {
		unsigned int command;
		int column;
		int page;
	} last_command;
	uint8_t oob_buf[16];
	uint8_t ecc_buf[7];
	int oob_page;
	struct bch_control *bch;
};

/*
 * Defines prefixed with DOCG4 are unique to the diskonchip G4.  All others are
 * shared with other diskonchip devices (P3, G3 at least).
 *
 * Functions with names prefixed with docg4_ are mtd / nand interface functions
 * (though they may also be called internally).  All others are internal.
 */

#define DOC_IOSPACE_DATA		0x0800

/* register offsets */
#define DOC_CHIPID			0x1000
#define DOC_DEVICESELECT		0x100a
#define DOC_ASICMODE			0x100c
#define DOC_DATAEND			0x101e
#define DOC_NOP				0x103e

#define DOC_FLASHSEQUENCE		0x1032
#define DOC_FLASHCOMMAND		0x1034
#define DOC_FLASHADDRESS		0x1036
#define DOC_FLASHCONTROL		0x1038
#define DOC_ECCCONF0			0x1040
#define DOC_ECCCONF1			0x1042
#define DOC_HAMMINGPARITY		0x1046
#define DOC_BCH_SYNDROM(idx)		(0x1048 + idx)

#define DOC_ASICMODECONFIRM		0x1072
#define DOC_CHIPID_INV			0x1074
#define DOC_POWERMODE			0x107c

#define DOCG4_MYSTERY_REG		0x1050

/* apparently used only to write oob bytes 6 and 7 */
#define DOCG4_OOB_6_7			0x1052

/* DOC_FLASHSEQUENCE register commands */
#define DOC_SEQ_RESET			0x00
#define DOCG4_SEQ_PAGE_READ		0x03
#define DOCG4_SEQ_FLUSH			0x29
#define DOCG4_SEQ_PAGEWRITE		0x16
#define DOCG4_SEQ_PAGEPROG		0x1e
#define DOCG4_SEQ_BLOCKERASE		0x24
#define DOCG4_SEQ_SETMODE		0x45

/* DOC_FLASHCOMMAND register commands */
#define DOCG4_CMD_PAGE_READ             0x00
#define DOC_CMD_ERASECYCLE2		0xd0
#define DOCG4_CMD_FLUSH                 0x70
#define DOCG4_CMD_READ2                 0x30
#define DOC_CMD_PROG_BLOCK_ADDR		0x60
#define DOCG4_CMD_PAGEWRITE		0x80
#define DOC_CMD_PROG_CYCLE2		0x10
#define DOCG4_CMD_FAST_MODE		0xa3 /* functionality guessed */
#define DOC_CMD_RELIABLE_MODE		0x22
#define DOC_CMD_RESET			0xff

/* DOC_POWERMODE register bits */
#define DOC_POWERDOWN_READY		0x80

/* DOC_FLASHCONTROL register bits */
#define DOC_CTRL_CE			0x10
#define DOC_CTRL_UNKNOWN		0x40
#define DOC_CTRL_FLASHREADY		0x01

/* DOC_ECCCONF0 register bits */
#define DOC_ECCCONF0_READ_MODE		0x8000
#define DOC_ECCCONF0_UNKNOWN		0x2000
#define DOC_ECCCONF0_ECC_ENABLE	        0x1000
#define DOC_ECCCONF0_DATA_BYTES_MASK	0x07ff

/* DOC_ECCCONF1 register bits */
#define DOC_ECCCONF1_BCH_SYNDROM_ERR	0x80
#define DOC_ECCCONF1_ECC_ENABLE         0x07
#define DOC_ECCCONF1_PAGE_IS_WRITTEN	0x20

/* DOC_ASICMODE register bits */
#define DOC_ASICMODE_RESET		0x00
#define DOC_ASICMODE_NORMAL		0x01
#define DOC_ASICMODE_POWERDOWN		0x02
#define DOC_ASICMODE_MDWREN		0x04
#define DOC_ASICMODE_BDETCT_RESET	0x08
#define DOC_ASICMODE_RSTIN_RESET	0x10
#define DOC_ASICMODE_RAM_WE		0x20

/* good status values read after read/write/erase operations */
#define DOCG4_PROGSTATUS_GOOD          0x51
#define DOCG4_PROGSTATUS_GOOD_2        0xe0

/*
 * On read operations (page and oob-only), the first byte read from I/O reg is a
 * status.  On error, it reads 0x73; otherwise, it reads either 0x71 (first read
 * after reset only) or 0x51, so bit 1 is presumed to be an error indicator.
 */
#define DOCG4_READ_ERROR           0x02 /* bit 1 indicates read error */

/* anatomy of the device */
#define DOCG4_CHIP_SIZE        0x8000000
#define DOCG4_PAGE_SIZE        0x200
#define DOCG4_PAGES_PER_BLOCK  0x200
#define DOCG4_BLOCK_SIZE       (DOCG4_PAGES_PER_BLOCK * DOCG4_PAGE_SIZE)
#define DOCG4_NUMBLOCKS        (DOCG4_CHIP_SIZE / DOCG4_BLOCK_SIZE)
#define DOCG4_OOB_SIZE         0x10
#define DOCG4_CHIP_SHIFT       27    /* log_2(DOCG4_CHIP_SIZE) */
#define DOCG4_PAGE_SHIFT       9     /* log_2(DOCG4_PAGE_SIZE) */
#define DOCG4_ERASE_SHIFT      18    /* log_2(DOCG4_BLOCK_SIZE) */

/* all but the last byte is included in ecc calculation */
#define DOCG4_BCH_SIZE         (DOCG4_PAGE_SIZE + DOCG4_OOB_SIZE - 1)

#define DOCG4_USERDATA_LEN     520 /* 512 byte page plus 8 oob avail to user */

/* expected values from the ID registers */
#define DOCG4_IDREG1_VALUE     0x0400
#define DOCG4_IDREG2_VALUE     0xfbff

/* primitive polynomial used to build the Galois field used by hw ecc gen */
#define DOCG4_PRIMITIVE_POLY   0x4443

#define DOCG4_M                14  /* Galois field is of order 2^14 */
#define DOCG4_T                4   /* BCH alg corrects up to 4 bit errors */

#define DOCG4_FACTORY_BBT_PAGE 16 /* page where read-only factory bbt lives */
#define DOCG4_REDUNDANT_BBT_PAGE 24 /* page where redundant factory bbt lives */

/*
 * Bytes 0, 1 are used as badblock marker.
 * Bytes 2 - 6 are available to the user.
 * Byte 7 is hamming ecc for first 7 oob bytes only.
 * Bytes 8 - 14 are hw-generated ecc covering entire page + oob bytes 0 - 14.
 * Byte 15 (the last) is used by the driver as a "page written" flag.
 */
static int docg4_ooblayout_ecc(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 7;
	oobregion->length = 9;

	return 0;
}

static int docg4_ooblayout_free(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 2;
	oobregion->length = 5;

	return 0;
}

static const struct mtd_ooblayout_ops docg4_ooblayout_ops = {
	.ecc = docg4_ooblayout_ecc,
	.free = docg4_ooblayout_free,
};

/*
 * The device has a nop register which M-Sys claims is for the purpose of
 * inserting precise delays.  But beware; at least some operations fail if the
 * nop writes are replaced with a generic delay!
 */
static inline void write_nop(void __iomem *docptr)
{
	writew(0, docptr + DOC_NOP);
}

static void docg4_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;
	struct nand_chip *nand = mtd_to_nand(mtd);
	uint16_t *p = (uint16_t *) buf;
	len >>= 1;

	for (i = 0; i < len; i++)
		p[i] = readw(nand->IO_ADDR_R);
}

static void docg4_write_buf16(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	int i;
	struct nand_chip *nand = mtd_to_nand(mtd);
	uint16_t *p = (uint16_t *) buf;
	len >>= 1;

	for (i = 0; i < len; i++)
		writew(p[i], nand->IO_ADDR_W);
}

static int poll_status(struct docg4_priv *doc)
{
	/*
	 * Busy-wait for the FLASHREADY bit to be set in the FLASHCONTROL
	 * register.  Operations known to take a long time (e.g., block erase)
	 * should sleep for a while before calling this.
	 */

	uint16_t flash_status;
	unsigned long timeo;
	void __iomem *docptr = doc->virtadr;

	dev_dbg(doc->dev, "%s...\n", __func__);

	/* hardware quirk requires reading twice initially */
	flash_status = readw(docptr + DOC_FLASHCONTROL);

	timeo = jiffies + msecs_to_jiffies(200); /* generous timeout */
	do {
		cpu_relax();
		flash_status = readb(docptr + DOC_FLASHCONTROL);
	} while (!(flash_status & DOC_CTRL_FLASHREADY) &&
		 time_before(jiffies, timeo));

	if (unlikely(!(flash_status & DOC_CTRL_FLASHREADY))) {
		dev_err(doc->dev, "%s: timed out!\n", __func__);
		return NAND_STATUS_FAIL;
	}

	return 0;
}


static int docg4_wait(struct mtd_info *mtd, struct nand_chip *nand)
{

	struct docg4_priv *doc = nand_get_controller_data(nand);
	int status = NAND_STATUS_WP;       /* inverse logic?? */
	dev_dbg(doc->dev, "%s...\n", __func__);

	/* report any previously unreported error */
	if (doc->status) {
		status |= doc->status;
		doc->status = 0;
		return status;
	}

	status |= poll_status(doc);
	return status;
}

static void docg4_select_chip(struct mtd_info *mtd, int chip)
{
	/*
	 * Select among multiple cascaded chips ("floors").  Multiple floors are
	 * not yet supported, so the only valid non-negative value is 0.
	 */
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;

	dev_dbg(doc->dev, "%s: chip %d\n", __func__, chip);

	if (chip < 0)
		return;		/* deselected */

	if (chip > 0)
		dev_warn(doc->dev, "multiple floors currently unsupported\n");

	writew(0, docptr + DOC_DEVICESELECT);
}

static void reset(struct mtd_info *mtd)
{
	/* full device reset */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;

	writew(DOC_ASICMODE_RESET | DOC_ASICMODE_MDWREN,
	       docptr + DOC_ASICMODE);
	writew(~(DOC_ASICMODE_RESET | DOC_ASICMODE_MDWREN),
	       docptr + DOC_ASICMODECONFIRM);
	write_nop(docptr);

	writew(DOC_ASICMODE_NORMAL | DOC_ASICMODE_MDWREN,
	       docptr + DOC_ASICMODE);
	writew(~(DOC_ASICMODE_NORMAL | DOC_ASICMODE_MDWREN),
	       docptr + DOC_ASICMODECONFIRM);

	writew(DOC_ECCCONF1_ECC_ENABLE, docptr + DOC_ECCCONF1);

	poll_status(doc);
}

static void read_hw_ecc(void __iomem *docptr, uint8_t *ecc_buf)
{
	/* read the 7 hw-generated ecc bytes */

	int i;
	for (i = 0; i < 7; i++) { /* hw quirk; read twice */
		ecc_buf[i] = readb(docptr + DOC_BCH_SYNDROM(i));
		ecc_buf[i] = readb(docptr + DOC_BCH_SYNDROM(i));
	}
}

static int correct_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	/*
	 * Called after a page read when hardware reports bitflips.
	 * Up to four bitflips can be corrected.
	 */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	int i, numerrs, errpos[4];
	const uint8_t blank_read_hwecc[8] = {
		0xcf, 0x72, 0xfc, 0x1b, 0xa9, 0xc7, 0xb9, 0 };

	read_hw_ecc(docptr, doc->ecc_buf); /* read 7 hw-generated ecc bytes */

	/* check if read error is due to a blank page */
	if (!memcmp(doc->ecc_buf, blank_read_hwecc, 7))
		return 0;	/* yes */

	/* skip additional check of "written flag" if ignore_badblocks */
	if (ignore_badblocks == false) {

		/*
		 * If the hw ecc bytes are not those of a blank page, there's
		 * still a chance that the page is blank, but was read with
		 * errors.  Check the "written flag" in last oob byte, which
		 * is set to zero when a page is written.  If more than half
		 * the bits are set, assume a blank page.  Unfortunately, the
		 * bit flips(s) are not reported in stats.
		 */

		if (nand->oob_poi[15]) {
			int bit, numsetbits = 0;
			unsigned long written_flag = nand->oob_poi[15];
			for_each_set_bit(bit, &written_flag, 8)
				numsetbits++;
			if (numsetbits > 4) { /* assume blank */
				dev_warn(doc->dev,
					 "error(s) in blank page "
					 "at offset %08x\n",
					 page * DOCG4_PAGE_SIZE);
				return 0;
			}
		}
	}

	/*
	 * The hardware ecc unit produces oob_ecc ^ calc_ecc.  The kernel's bch
	 * algorithm is used to decode this.  However the hw operates on page
	 * data in a bit order that is the reverse of that of the bch alg,
	 * requiring that the bits be reversed on the result.  Thanks to Ivan
	 * Djelic for his analysis!
	 */
	for (i = 0; i < 7; i++)
		doc->ecc_buf[i] = bitrev8(doc->ecc_buf[i]);

	numerrs = decode_bch(doc->bch, NULL, DOCG4_USERDATA_LEN, NULL,
			     doc->ecc_buf, NULL, errpos);

	if (numerrs == -EBADMSG) {
		dev_warn(doc->dev, "uncorrectable errors at offset %08x\n",
			 page * DOCG4_PAGE_SIZE);
		return -EBADMSG;
	}

	BUG_ON(numerrs < 0);	/* -EINVAL, or anything other than -EBADMSG */

	/* undo last step in BCH alg (modulo mirroring not needed) */
	for (i = 0; i < numerrs; i++)
		errpos[i] = (errpos[i] & ~7)|(7-(errpos[i] & 7));

	/* fix the errors */
	for (i = 0; i < numerrs; i++) {

		/* ignore if error within oob ecc bytes */
		if (errpos[i] > DOCG4_USERDATA_LEN * 8)
			continue;

		/* if error within oob area preceeding ecc bytes... */
		if (errpos[i] > DOCG4_PAGE_SIZE * 8)
			change_bit(errpos[i] - DOCG4_PAGE_SIZE * 8,
				   (unsigned long *)nand->oob_poi);

		else    /* error in page data */
			change_bit(errpos[i], (unsigned long *)buf);
	}

	dev_notice(doc->dev, "%d error(s) corrected at offset %08x\n",
		   numerrs, page * DOCG4_PAGE_SIZE);

	return numerrs;
}

static uint8_t docg4_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);

	dev_dbg(doc->dev, "%s\n", __func__);

	if (doc->last_command.command == NAND_CMD_STATUS) {
		int status;

		/*
		 * Previous nand command was status request, so nand
		 * infrastructure code expects to read the status here.  If an
		 * error occurred in a previous operation, report it.
		 */
		doc->last_command.command = 0;

		if (doc->status) {
			status = doc->status;
			doc->status = 0;
		}

		/* why is NAND_STATUS_WP inverse logic?? */
		else
			status = NAND_STATUS_WP | NAND_STATUS_READY;

		return status;
	}

	dev_warn(doc->dev, "unexpected call to read_byte()\n");

	return 0;
}

static void write_addr(struct docg4_priv *doc, uint32_t docg4_addr)
{
	/* write the four address bytes packed in docg4_addr to the device */

	void __iomem *docptr = doc->virtadr;
	writeb(docg4_addr & 0xff, docptr + DOC_FLASHADDRESS);
	docg4_addr >>= 8;
	writeb(docg4_addr & 0xff, docptr + DOC_FLASHADDRESS);
	docg4_addr >>= 8;
	writeb(docg4_addr & 0xff, docptr + DOC_FLASHADDRESS);
	docg4_addr >>= 8;
	writeb(docg4_addr & 0xff, docptr + DOC_FLASHADDRESS);
}

static int read_progstatus(struct docg4_priv *doc)
{
	/*
	 * This apparently checks the status of programming.  Done after an
	 * erasure, and after page data is written.  On error, the status is
	 * saved, to be later retrieved by the nand infrastructure code.
	 */
	void __iomem *docptr = doc->virtadr;

	/* status is read from the I/O reg */
	uint16_t status1 = readw(docptr + DOC_IOSPACE_DATA);
	uint16_t status2 = readw(docptr + DOC_IOSPACE_DATA);
	uint16_t status3 = readw(docptr + DOCG4_MYSTERY_REG);

	dev_dbg(doc->dev, "docg4: %s: %02x %02x %02x\n",
	      __func__, status1, status2, status3);

	if (status1 != DOCG4_PROGSTATUS_GOOD
	    || status2 != DOCG4_PROGSTATUS_GOOD_2
	    || status3 != DOCG4_PROGSTATUS_GOOD_2) {
		doc->status = NAND_STATUS_FAIL;
		dev_warn(doc->dev, "read_progstatus failed: "
			 "%02x, %02x, %02x\n", status1, status2, status3);
		return -EIO;
	}
	return 0;
}

static int pageprog(struct mtd_info *mtd)
{
	/*
	 * Final step in writing a page.  Writes the contents of its
	 * internal buffer out to the flash array, or some such.
	 */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	int retval = 0;

	dev_dbg(doc->dev, "docg4: %s\n", __func__);

	writew(DOCG4_SEQ_PAGEPROG, docptr + DOC_FLASHSEQUENCE);
	writew(DOC_CMD_PROG_CYCLE2, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);
	write_nop(docptr);

	/* Just busy-wait; usleep_range() slows things down noticeably. */
	poll_status(doc);

	writew(DOCG4_SEQ_FLUSH, docptr + DOC_FLASHSEQUENCE);
	writew(DOCG4_CMD_FLUSH, docptr + DOC_FLASHCOMMAND);
	writew(DOC_ECCCONF0_READ_MODE | 4, docptr + DOC_ECCCONF0);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);

	retval = read_progstatus(doc);
	writew(0, docptr + DOC_DATAEND);
	write_nop(docptr);
	poll_status(doc);
	write_nop(docptr);

	return retval;
}

static void sequence_reset(struct mtd_info *mtd)
{
	/* common starting sequence for all operations */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;

	writew(DOC_CTRL_UNKNOWN | DOC_CTRL_CE, docptr + DOC_FLASHCONTROL);
	writew(DOC_SEQ_RESET, docptr + DOC_FLASHSEQUENCE);
	writew(DOC_CMD_RESET, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);
	write_nop(docptr);
	poll_status(doc);
	write_nop(docptr);
}

static void read_page_prologue(struct mtd_info *mtd, uint32_t docg4_addr)
{
	/* first step in reading a page */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;

	dev_dbg(doc->dev,
	      "docg4: %s: g4 page %08x\n", __func__, docg4_addr);

	sequence_reset(mtd);

	writew(DOCG4_SEQ_PAGE_READ, docptr + DOC_FLASHSEQUENCE);
	writew(DOCG4_CMD_PAGE_READ, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);

	write_addr(doc, docg4_addr);

	write_nop(docptr);
	writew(DOCG4_CMD_READ2, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);
	write_nop(docptr);

	poll_status(doc);
}

static void write_page_prologue(struct mtd_info *mtd, uint32_t docg4_addr)
{
	/* first step in writing a page */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;

	dev_dbg(doc->dev,
	      "docg4: %s: g4 addr: %x\n", __func__, docg4_addr);
	sequence_reset(mtd);

	if (unlikely(reliable_mode)) {
		writew(DOCG4_SEQ_SETMODE, docptr + DOC_FLASHSEQUENCE);
		writew(DOCG4_CMD_FAST_MODE, docptr + DOC_FLASHCOMMAND);
		writew(DOC_CMD_RELIABLE_MODE, docptr + DOC_FLASHCOMMAND);
		write_nop(docptr);
	}

	writew(DOCG4_SEQ_PAGEWRITE, docptr + DOC_FLASHSEQUENCE);
	writew(DOCG4_CMD_PAGEWRITE, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);
	write_addr(doc, docg4_addr);
	write_nop(docptr);
	write_nop(docptr);
	poll_status(doc);
}

static uint32_t mtd_to_docg4_address(int page, int column)
{
	/*
	 * Convert mtd address to format used by the device, 32 bit packed.
	 *
	 * Some notes on G4 addressing... The M-Sys documentation on this device
	 * claims that pages are 2K in length, and indeed, the format of the
	 * address used by the device reflects that.  But within each page are
	 * four 512 byte "sub-pages", each with its own oob data that is
	 * read/written immediately after the 512 bytes of page data.  This oob
	 * data contains the ecc bytes for the preceeding 512 bytes.
	 *
	 * Rather than tell the mtd nand infrastructure that page size is 2k,
	 * with four sub-pages each, we engage in a little subterfuge and tell
	 * the infrastructure code that pages are 512 bytes in size.  This is
	 * done because during the course of reverse-engineering the device, I
	 * never observed an instance where an entire 2K "page" was read or
	 * written as a unit.  Each "sub-page" is always addressed individually,
	 * its data read/written, and ecc handled before the next "sub-page" is
	 * addressed.
	 *
	 * This requires us to convert addresses passed by the mtd nand
	 * infrastructure code to those used by the device.
	 *
	 * The address that is written to the device consists of four bytes: the
	 * first two are the 2k page number, and the second is the index into
	 * the page.  The index is in terms of 16-bit half-words and includes
	 * the preceeding oob data, so e.g., the index into the second
	 * "sub-page" is 0x108, and the full device address of the start of mtd
	 * page 0x201 is 0x00800108.
	 */
	int g4_page = page / 4;	                      /* device's 2K page */
	int g4_index = (page % 4) * 0x108 + column/2; /* offset into page */
	return (g4_page << 16) | g4_index;	      /* pack */
}

static void docg4_command(struct mtd_info *mtd, unsigned command, int column,
			  int page_addr)
{
	/* handle standard nand commands */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	uint32_t g4_addr = mtd_to_docg4_address(page_addr, column);

	dev_dbg(doc->dev, "%s %x, page_addr=%x, column=%x\n",
	      __func__, command, page_addr, column);

	/*
	 * Save the command and its arguments.  This enables emulation of
	 * standard flash devices, and also some optimizations.
	 */
	doc->last_command.command = command;
	doc->last_command.column = column;
	doc->last_command.page = page_addr;

	switch (command) {

	case NAND_CMD_RESET:
		reset(mtd);
		break;

	case NAND_CMD_READ0:
		read_page_prologue(mtd, g4_addr);
		break;

	case NAND_CMD_STATUS:
		/* next call to read_byte() will expect a status */
		break;

	case NAND_CMD_SEQIN:
		if (unlikely(reliable_mode)) {
			uint16_t g4_page = g4_addr >> 16;

			/* writes to odd-numbered 2k pages are invalid */
			if (g4_page & 0x01)
				dev_warn(doc->dev,
					 "invalid reliable mode address\n");
		}

		write_page_prologue(mtd, g4_addr);

		/* hack for deferred write of oob bytes */
		if (doc->oob_page == page_addr)
			memcpy(nand->oob_poi, doc->oob_buf, 16);
		break;

	case NAND_CMD_PAGEPROG:
		pageprog(mtd);
		break;

	/* we don't expect these, based on review of nand_base.c */
	case NAND_CMD_READOOB:
	case NAND_CMD_READID:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
		dev_warn(doc->dev, "docg4_command: "
			 "unexpected nand command 0x%x\n", command);
		break;

	}
}

static int read_page(struct mtd_info *mtd, struct nand_chip *nand,
		     uint8_t *buf, int page, bool use_ecc)
{
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	uint16_t status, edc_err, *buf16;
	int bits_corrected = 0;

	dev_dbg(doc->dev, "%s: page %08x\n", __func__, page);

	nand_read_page_op(nand, page, 0, NULL, 0);

	writew(DOC_ECCCONF0_READ_MODE |
	       DOC_ECCCONF0_ECC_ENABLE |
	       DOC_ECCCONF0_UNKNOWN |
	       DOCG4_BCH_SIZE,
	       docptr + DOC_ECCCONF0);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);

	/* the 1st byte from the I/O reg is a status; the rest is page data */
	status = readw(docptr + DOC_IOSPACE_DATA);
	if (status & DOCG4_READ_ERROR) {
		dev_err(doc->dev,
			"docg4_read_page: bad status: 0x%02x\n", status);
		writew(0, docptr + DOC_DATAEND);
		return -EIO;
	}

	dev_dbg(doc->dev, "%s: status = 0x%x\n", __func__, status);

	docg4_read_buf(mtd, buf, DOCG4_PAGE_SIZE); /* read the page data */

	/* this device always reads oob after page data */
	/* first 14 oob bytes read from I/O reg */
	docg4_read_buf(mtd, nand->oob_poi, 14);

	/* last 2 read from another reg */
	buf16 = (uint16_t *)(nand->oob_poi + 14);
	*buf16 = readw(docptr + DOCG4_MYSTERY_REG);

	write_nop(docptr);

	if (likely(use_ecc == true)) {

		/* read the register that tells us if bitflip(s) detected  */
		edc_err = readw(docptr + DOC_ECCCONF1);
		edc_err = readw(docptr + DOC_ECCCONF1);
		dev_dbg(doc->dev, "%s: edc_err = 0x%02x\n", __func__, edc_err);

		/* If bitflips are reported, attempt to correct with ecc */
		if (edc_err & DOC_ECCCONF1_BCH_SYNDROM_ERR) {
			bits_corrected = correct_data(mtd, buf, page);
			if (bits_corrected == -EBADMSG)
				mtd->ecc_stats.failed++;
			else
				mtd->ecc_stats.corrected += bits_corrected;
		}
	}

	writew(0, docptr + DOC_DATAEND);
	if (bits_corrected == -EBADMSG)	  /* uncorrectable errors */
		return 0;
	return bits_corrected;
}


static int docg4_read_page_raw(struct mtd_info *mtd, struct nand_chip *nand,
			       uint8_t *buf, int oob_required, int page)
{
	return read_page(mtd, nand, buf, page, false);
}

static int docg4_read_page(struct mtd_info *mtd, struct nand_chip *nand,
			   uint8_t *buf, int oob_required, int page)
{
	return read_page(mtd, nand, buf, page, true);
}

static int docg4_read_oob(struct mtd_info *mtd, struct nand_chip *nand,
			  int page)
{
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	uint16_t status;

	dev_dbg(doc->dev, "%s: page %x\n", __func__, page);

	nand_read_page_op(nand, page, nand->ecc.size, NULL, 0);

	writew(DOC_ECCCONF0_READ_MODE | DOCG4_OOB_SIZE, docptr + DOC_ECCCONF0);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);

	/* the 1st byte from the I/O reg is a status; the rest is oob data */
	status = readw(docptr + DOC_IOSPACE_DATA);
	if (status & DOCG4_READ_ERROR) {
		dev_warn(doc->dev,
			 "docg4_read_oob failed: status = 0x%02x\n", status);
		return -EIO;
	}

	dev_dbg(doc->dev, "%s: status = 0x%x\n", __func__, status);

	docg4_read_buf(mtd, nand->oob_poi, 16);

	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	writew(0, docptr + DOC_DATAEND);
	write_nop(docptr);

	return 0;
}

static int docg4_erase_block(struct mtd_info *mtd, int page)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	uint16_t g4_page;
	int status;

	dev_dbg(doc->dev, "%s: page %04x\n", __func__, page);

	sequence_reset(mtd);

	writew(DOCG4_SEQ_BLOCKERASE, docptr + DOC_FLASHSEQUENCE);
	writew(DOC_CMD_PROG_BLOCK_ADDR, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);

	/* only 2 bytes of address are written to specify erase block */
	g4_page = (uint16_t)(page / 4);  /* to g4's 2k page addressing */
	writeb(g4_page & 0xff, docptr + DOC_FLASHADDRESS);
	g4_page >>= 8;
	writeb(g4_page & 0xff, docptr + DOC_FLASHADDRESS);
	write_nop(docptr);

	/* start the erasure */
	writew(DOC_CMD_ERASECYCLE2, docptr + DOC_FLASHCOMMAND);
	write_nop(docptr);
	write_nop(docptr);

	usleep_range(500, 1000); /* erasure is long; take a snooze */
	poll_status(doc);
	writew(DOCG4_SEQ_FLUSH, docptr + DOC_FLASHSEQUENCE);
	writew(DOCG4_CMD_FLUSH, docptr + DOC_FLASHCOMMAND);
	writew(DOC_ECCCONF0_READ_MODE | 4, docptr + DOC_ECCCONF0);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);
	write_nop(docptr);

	read_progstatus(doc);

	writew(0, docptr + DOC_DATAEND);
	write_nop(docptr);
	poll_status(doc);
	write_nop(docptr);

	status = nand->waitfunc(mtd, nand);
	if (status < 0)
		return status;

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

static int write_page(struct mtd_info *mtd, struct nand_chip *nand,
		      const uint8_t *buf, int page, bool use_ecc)
{
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	uint8_t ecc_buf[8];

	dev_dbg(doc->dev, "%s...\n", __func__);

	nand_prog_page_begin_op(nand, page, 0, NULL, 0);

	writew(DOC_ECCCONF0_ECC_ENABLE |
	       DOC_ECCCONF0_UNKNOWN |
	       DOCG4_BCH_SIZE,
	       docptr + DOC_ECCCONF0);
	write_nop(docptr);

	/* write the page data */
	docg4_write_buf16(mtd, buf, DOCG4_PAGE_SIZE);

	/* oob bytes 0 through 5 are written to I/O reg */
	docg4_write_buf16(mtd, nand->oob_poi, 6);

	/* oob byte 6 written to a separate reg */
	writew(nand->oob_poi[6], docptr + DOCG4_OOB_6_7);

	write_nop(docptr);
	write_nop(docptr);

	/* write hw-generated ecc bytes to oob */
	if (likely(use_ecc == true)) {
		/* oob byte 7 is hamming code */
		uint8_t hamming = readb(docptr + DOC_HAMMINGPARITY);
		hamming = readb(docptr + DOC_HAMMINGPARITY); /* 2nd read */
		writew(hamming, docptr + DOCG4_OOB_6_7);
		write_nop(docptr);

		/* read the 7 bch bytes from ecc regs */
		read_hw_ecc(docptr, ecc_buf);
		ecc_buf[7] = 0;         /* clear the "page written" flag */
	}

	/* write user-supplied bytes to oob */
	else {
		writew(nand->oob_poi[7], docptr + DOCG4_OOB_6_7);
		write_nop(docptr);
		memcpy(ecc_buf, &nand->oob_poi[8], 8);
	}

	docg4_write_buf16(mtd, ecc_buf, 8);
	write_nop(docptr);
	write_nop(docptr);
	writew(0, docptr + DOC_DATAEND);
	write_nop(docptr);

	return nand_prog_page_end_op(nand);
}

static int docg4_write_page_raw(struct mtd_info *mtd, struct nand_chip *nand,
				const uint8_t *buf, int oob_required, int page)
{
	return write_page(mtd, nand, buf, page, false);
}

static int docg4_write_page(struct mtd_info *mtd, struct nand_chip *nand,
			     const uint8_t *buf, int oob_required, int page)
{
	return write_page(mtd, nand, buf, page, true);
}

static int docg4_write_oob(struct mtd_info *mtd, struct nand_chip *nand,
			   int page)
{
	/*
	 * Writing oob-only is not really supported, because MLC nand must write
	 * oob bytes at the same time as page data.  Nonetheless, we save the
	 * oob buffer contents here, and then write it along with the page data
	 * if the same page is subsequently written.  This allows user space
	 * utilities that write the oob data prior to the page data to work
	 * (e.g., nandwrite).  The disdvantage is that, if the intention was to
	 * write oob only, the operation is quietly ignored.  Also, oob can get
	 * corrupted if two concurrent processes are running nandwrite.
	 */

	/* note that bytes 7..14 are hw generated hamming/ecc and overwritten */
	struct docg4_priv *doc = nand_get_controller_data(nand);
	doc->oob_page = page;
	memcpy(doc->oob_buf, nand->oob_poi, 16);
	return 0;
}

static int __init read_factory_bbt(struct mtd_info *mtd)
{
	/*
	 * The device contains a read-only factory bad block table.  Read it and
	 * update the memory-based bbt accordingly.
	 */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	uint32_t g4_addr = mtd_to_docg4_address(DOCG4_FACTORY_BBT_PAGE, 0);
	uint8_t *buf;
	int i, block;
	__u32 eccfailed_stats = mtd->ecc_stats.failed;

	buf = kzalloc(DOCG4_PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	read_page_prologue(mtd, g4_addr);
	docg4_read_page(mtd, nand, buf, 0, DOCG4_FACTORY_BBT_PAGE);

	/*
	 * If no memory-based bbt was created, exit.  This will happen if module
	 * parameter ignore_badblocks is set.  Then why even call this function?
	 * For an unknown reason, block erase always fails if it's the first
	 * operation after device power-up.  The above read ensures it never is.
	 * Ugly, I know.
	 */
	if (nand->bbt == NULL)  /* no memory-based bbt */
		goto exit;

	if (mtd->ecc_stats.failed > eccfailed_stats) {
		/*
		 * Whoops, an ecc failure ocurred reading the factory bbt.
		 * It is stored redundantly, so we get another chance.
		 */
		eccfailed_stats = mtd->ecc_stats.failed;
		docg4_read_page(mtd, nand, buf, 0, DOCG4_REDUNDANT_BBT_PAGE);
		if (mtd->ecc_stats.failed > eccfailed_stats) {
			dev_warn(doc->dev,
				 "The factory bbt could not be read!\n");
			goto exit;
		}
	}

	/*
	 * Parse factory bbt and update memory-based bbt.  Factory bbt format is
	 * simple: one bit per block, block numbers increase left to right (msb
	 * to lsb).  Bit clear means bad block.
	 */
	for (i = block = 0; block < DOCG4_NUMBLOCKS; block += 8, i++) {
		int bitnum;
		unsigned long bits = ~buf[i];
		for_each_set_bit(bitnum, &bits, 8) {
			int badblock = block + 7 - bitnum;
			nand->bbt[badblock / 4] |=
				0x03 << ((badblock % 4) * 2);
			mtd->ecc_stats.badblocks++;
			dev_notice(doc->dev, "factory-marked bad block: %d\n",
				   badblock);
		}
	}
 exit:
	kfree(buf);
	return 0;
}

static int docg4_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	/*
	 * Mark a block as bad.  Bad blocks are marked in the oob area of the
	 * first page of the block.  The default scan_bbt() in the nand
	 * infrastructure code works fine for building the memory-based bbt
	 * during initialization, as does the nand infrastructure function that
	 * checks if a block is bad by reading the bbt.  This function replaces
	 * the nand default because writes to oob-only are not supported.
	 */

	int ret, i;
	uint8_t *buf;
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	struct nand_bbt_descr *bbtd = nand->badblock_pattern;
	int page = (int)(ofs >> nand->page_shift);
	uint32_t g4_addr = mtd_to_docg4_address(page, 0);

	dev_dbg(doc->dev, "%s: %08llx\n", __func__, ofs);

	if (unlikely(ofs & (DOCG4_BLOCK_SIZE - 1)))
		dev_warn(doc->dev, "%s: ofs %llx not start of block!\n",
			 __func__, ofs);

	/* allocate blank buffer for page data */
	buf = kzalloc(DOCG4_PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* write bit-wise negation of pattern to oob buffer */
	memset(nand->oob_poi, 0xff, mtd->oobsize);
	for (i = 0; i < bbtd->len; i++)
		nand->oob_poi[bbtd->offs + i] = ~bbtd->pattern[i];

	/* write first page of block */
	write_page_prologue(mtd, g4_addr);
	docg4_write_page(mtd, nand, buf, 1, page);
	ret = pageprog(mtd);

	kfree(buf);

	return ret;
}

static int docg4_block_neverbad(struct mtd_info *mtd, loff_t ofs)
{
	/* only called when module_param ignore_badblocks is set */
	return 0;
}

static int docg4_suspend(struct platform_device *pdev, pm_message_t state)
{
	/*
	 * Put the device into "deep power-down" mode.  Note that CE# must be
	 * deasserted for this to take effect.  The xscale, e.g., can be
	 * configured to float this signal when the processor enters power-down,
	 * and a suitable pull-up ensures its deassertion.
	 */

	int i;
	uint8_t pwr_down;
	struct docg4_priv *doc = platform_get_drvdata(pdev);
	void __iomem *docptr = doc->virtadr;

	dev_dbg(doc->dev, "%s...\n", __func__);

	/* poll the register that tells us we're ready to go to sleep */
	for (i = 0; i < 10; i++) {
		pwr_down = readb(docptr + DOC_POWERMODE);
		if (pwr_down & DOC_POWERDOWN_READY)
			break;
		usleep_range(1000, 4000);
	}

	if (pwr_down & DOC_POWERDOWN_READY) {
		dev_err(doc->dev, "suspend failed; "
			"timeout polling DOC_POWERDOWN_READY\n");
		return -EIO;
	}

	writew(DOC_ASICMODE_POWERDOWN | DOC_ASICMODE_MDWREN,
	       docptr + DOC_ASICMODE);
	writew(~(DOC_ASICMODE_POWERDOWN | DOC_ASICMODE_MDWREN),
	       docptr + DOC_ASICMODECONFIRM);

	write_nop(docptr);

	return 0;
}

static int docg4_resume(struct platform_device *pdev)
{

	/*
	 * Exit power-down.  Twelve consecutive reads of the address below
	 * accomplishes this, assuming CE# has been asserted.
	 */

	struct docg4_priv *doc = platform_get_drvdata(pdev);
	void __iomem *docptr = doc->virtadr;
	int i;

	dev_dbg(doc->dev, "%s...\n", __func__);

	for (i = 0; i < 12; i++)
		readb(docptr + 0x1fff);

	return 0;
}

static void init_mtd_structs(struct mtd_info *mtd)
{
	/* initialize mtd and nand data structures */

	/*
	 * Note that some of the following initializations are not usually
	 * required within a nand driver because they are performed by the nand
	 * infrastructure code as part of nand_scan().  In this case they need
	 * to be initialized here because we skip call to nand_scan_ident() (the
	 * first half of nand_scan()).  The call to nand_scan_ident() could be
	 * skipped because for this device the chip id is not read in the manner
	 * of a standard nand device.
	 */

	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);

	mtd->size = DOCG4_CHIP_SIZE;
	mtd->name = "Msys_Diskonchip_G4";
	mtd->writesize = DOCG4_PAGE_SIZE;
	mtd->erasesize = DOCG4_BLOCK_SIZE;
	mtd->oobsize = DOCG4_OOB_SIZE;
	mtd_set_ooblayout(mtd, &docg4_ooblayout_ops);
	nand->chipsize = DOCG4_CHIP_SIZE;
	nand->chip_shift = DOCG4_CHIP_SHIFT;
	nand->bbt_erase_shift = nand->phys_erase_shift = DOCG4_ERASE_SHIFT;
	nand->chip_delay = 20;
	nand->page_shift = DOCG4_PAGE_SHIFT;
	nand->pagemask = 0x3ffff;
	nand->badblockpos = NAND_LARGE_BADBLOCK_POS;
	nand->badblockbits = 8;
	nand->ecc.mode = NAND_ECC_HW_SYNDROME;
	nand->ecc.size = DOCG4_PAGE_SIZE;
	nand->ecc.prepad = 8;
	nand->ecc.bytes	= 8;
	nand->ecc.strength = DOCG4_T;
	nand->options = NAND_BUSWIDTH_16 | NAND_NO_SUBPAGE_WRITE;
	nand->IO_ADDR_R = nand->IO_ADDR_W = doc->virtadr + DOC_IOSPACE_DATA;
	nand->controller = &nand->dummy_controller;
	nand_controller_init(nand->controller);

	/* methods */
	nand->cmdfunc = docg4_command;
	nand->waitfunc = docg4_wait;
	nand->select_chip = docg4_select_chip;
	nand->read_byte = docg4_read_byte;
	nand->block_markbad = docg4_block_markbad;
	nand->read_buf = docg4_read_buf;
	nand->write_buf = docg4_write_buf16;
	nand->erase = docg4_erase_block;
	nand->set_features = nand_get_set_features_notsupp;
	nand->get_features = nand_get_set_features_notsupp;
	nand->ecc.read_page = docg4_read_page;
	nand->ecc.write_page = docg4_write_page;
	nand->ecc.read_page_raw = docg4_read_page_raw;
	nand->ecc.write_page_raw = docg4_write_page_raw;
	nand->ecc.read_oob = docg4_read_oob;
	nand->ecc.write_oob = docg4_write_oob;

	/*
	 * The way the nand infrastructure code is written, a memory-based bbt
	 * is not created if NAND_SKIP_BBTSCAN is set.  With no memory bbt,
	 * nand->block_bad() is used.  So when ignoring bad blocks, we skip the
	 * scan and define a dummy block_bad() which always returns 0.
	 */
	if (ignore_badblocks) {
		nand->options |= NAND_SKIP_BBTSCAN;
		nand->block_bad	= docg4_block_neverbad;
	}

}

static int read_id_reg(struct mtd_info *mtd)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct docg4_priv *doc = nand_get_controller_data(nand);
	void __iomem *docptr = doc->virtadr;
	uint16_t id1, id2;

	/* check for presence of g4 chip by reading id registers */
	id1 = readw(docptr + DOC_CHIPID);
	id1 = readw(docptr + DOCG4_MYSTERY_REG);
	id2 = readw(docptr + DOC_CHIPID_INV);
	id2 = readw(docptr + DOCG4_MYSTERY_REG);

	if (id1 == DOCG4_IDREG1_VALUE && id2 == DOCG4_IDREG2_VALUE) {
		dev_info(doc->dev,
			 "NAND device: 128MiB Diskonchip G4 detected\n");
		return 0;
	}

	return -ENODEV;
}

static char const *part_probes[] = { "cmdlinepart", "saftlpart", NULL };

static int docg4_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct docg4_priv *doc = (struct docg4_priv *)(chip + 1);
	int ret;

	init_mtd_structs(mtd);

	/* Initialize kernel BCH algorithm */
	doc->bch = init_bch(DOCG4_M, DOCG4_T, DOCG4_PRIMITIVE_POLY);
	if (!doc->bch)
		return -EINVAL;

	reset(mtd);

	ret = read_id_reg(mtd);
	if (ret)
		free_bch(doc->bch);

	return ret;
}

static void docg4_detach_chip(struct nand_chip *chip)
{
	struct docg4_priv *doc = (struct docg4_priv *)(chip + 1);

	free_bch(doc->bch);
}

static const struct nand_controller_ops docg4_controller_ops = {
	.attach_chip = docg4_attach_chip,
	.detach_chip = docg4_detach_chip,
};

static int __init probe_docg4(struct platform_device *pdev)
{
	struct mtd_info *mtd;
	struct nand_chip *nand;
	void __iomem *virtadr;
	struct docg4_priv *doc;
	int len, retval;
	struct resource *r;
	struct device *dev = &pdev->dev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(dev, "no io memory resource defined!\n");
		return -ENODEV;
	}

	virtadr = ioremap(r->start, resource_size(r));
	if (!virtadr) {
		dev_err(dev, "Diskonchip ioremap failed: %pR\n", r);
		return -EIO;
	}

	len = sizeof(struct nand_chip) + sizeof(struct docg4_priv);
	nand = kzalloc(len, GFP_KERNEL);
	if (nand == NULL) {
		retval = -ENOMEM;
		goto unmap;
	}

	mtd = nand_to_mtd(nand);
	doc = (struct docg4_priv *) (nand + 1);
	nand_set_controller_data(nand, doc);
	mtd->dev.parent = &pdev->dev;
	doc->virtadr = virtadr;
	doc->dev = dev;
	platform_set_drvdata(pdev, doc);

	/*
	 * Running nand_scan() with maxchips == 0 will skip nand_scan_ident(),
	 * which is a specific operation with this driver and done in the
	 * ->attach_chip callback.
	 */
	nand->dummy_controller.ops = &docg4_controller_ops;
	retval = nand_scan(nand, 0);
	if (retval)
		goto free_nand;

	retval = read_factory_bbt(mtd);
	if (retval)
		goto cleanup_nand;

	retval = mtd_device_parse_register(mtd, part_probes, NULL, NULL, 0);
	if (retval)
		goto cleanup_nand;

	doc->mtd = mtd;

	return 0;

cleanup_nand:
	nand_cleanup(nand);
free_nand:
	kfree(nand);
unmap:
	iounmap(virtadr);

	return retval;
}

static int __exit cleanup_docg4(struct platform_device *pdev)
{
	struct docg4_priv *doc = platform_get_drvdata(pdev);
	nand_release(mtd_to_nand(doc->mtd));
	kfree(mtd_to_nand(doc->mtd));
	iounmap(doc->virtadr);
	return 0;
}

static struct platform_driver docg4_driver = {
	.driver		= {
		.name	= "docg4",
	},
	.suspend	= docg4_suspend,
	.resume		= docg4_resume,
	.remove		= __exit_p(cleanup_docg4),
};

module_platform_driver_probe(docg4_driver, probe_docg4);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Dunn");
MODULE_DESCRIPTION("M-Systems DiskOnChip G4 device driver");

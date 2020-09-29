// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file provides ECC correction for more than 1 bit per block of data,
 * using binary BCH codes. It relies on the generic BCH library lib/bch.c.
 *
 * Copyright © 2011 Ivan Djelic <ivan.djelic@parrot.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand-ecc-sw-bch.h>
#include <linux/bch.h>

/**
 * struct nand_bch_control - private NAND BCH control structure
 * @bch:       BCH control structure
 * @errloc:    error location array
 * @eccmask:   XOR ecc mask, allows erased pages to be decoded as valid
 */
struct nand_bch_control {
	struct bch_control   *bch;
	unsigned int         *errloc;
	unsigned char        *eccmask;
};

/**
 * nand_ecc_sw_bch_calculate - Calculate the ECC corresponding to a data block
 * @nand: NAND device
 * @buf: Input buffer with raw data
 * @code: Output buffer with ECC
 */
int nand_ecc_sw_bch_calculate(struct nand_device *nand,
			      const unsigned char *buf, unsigned char *code)
{
	struct nand_chip *chip = mtd_to_nand(nanddev_to_mtd(nand));
	struct nand_bch_control *nbc = chip->ecc.priv;
	unsigned int i;

	memset(code, 0, chip->ecc.bytes);
	bch_encode(nbc->bch, buf, chip->ecc.size, code);

	/* apply mask so that an erased page is a valid codeword */
	for (i = 0; i < chip->ecc.bytes; i++)
		code[i] ^= nbc->eccmask[i];

	return 0;
}
EXPORT_SYMBOL(nand_ecc_sw_bch_calculate);

/**
 * nand_ecc_sw_bch_correct - Detect, correct and report bit error(s)
 * @nand: NAND device
 * @buf: Raw data read from the chip
 * @read_ecc: ECC bytes from the chip
 * @calc_ecc: ECC calculated from the raw data
 *
 * Detect and correct bit errors for a data block.
 */
int nand_ecc_sw_bch_correct(struct nand_device *nand, unsigned char *buf,
			    unsigned char *read_ecc, unsigned char *calc_ecc)
{
	struct nand_chip *chip = mtd_to_nand(nanddev_to_mtd(nand));
	struct nand_bch_control *nbc = chip->ecc.priv;
	unsigned int *errloc = nbc->errloc;
	int i, count;

	count = bch_decode(nbc->bch, NULL, chip->ecc.size, read_ecc, calc_ecc,
			   NULL, errloc);
	if (count > 0) {
		for (i = 0; i < count; i++) {
			if (errloc[i] < (chip->ecc.size * 8))
				/* The error is in the data area: correct it */
				buf[errloc[i] >> 3] ^= (1 << (errloc[i] & 7));

			/* Otherwise the error is in the ECC area: nothing to do */
			pr_debug("%s: corrected bitflip %u\n", __func__,
				 errloc[i]);
		}
	} else if (count < 0) {
		pr_err("ECC unrecoverable error\n");
		count = -EBADMSG;
	}

	return count;
}
EXPORT_SYMBOL(nand_ecc_sw_bch_correct);

/**
 * nand_ecc_sw_bch_init - Initialize software BCH ECC engine
 * @nand: NAND device
 *
 * Returns: a pointer to a new NAND BCH control structure, or NULL upon failure
 *
 * Initialize NAND BCH error correction. Parameters @eccsize and @eccbytes
 * are used to compute the following BCH parameters:
 *     m, the Galois field order
 *     t, the error correction capability
 * @eccbytes should be equal to the number of bytes required to store m * t
 * bits, where m is such that 2^m - 1 > step_size * 8.
 *
 * Example: to configure 4 bit correction per 512 bytes, you should pass
 * @eccsize = 512 (thus, m = 13 is the smallest integer such that 2^m - 1 > 512 * 8)
 * @eccbytes = 7 (7 bytes are required to store m * t = 13 * 4 = 52 bits)
 */
int nand_ecc_sw_bch_init(struct nand_device *nand)
{
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	struct nand_chip *chip = mtd_to_nand(mtd);
	unsigned int m, t, eccsteps, i;
	struct nand_bch_control *nbc = NULL;
	unsigned char *erased_page;
	unsigned int eccsize = chip->ecc.size;
	unsigned int eccbytes = chip->ecc.bytes;
	unsigned int eccstrength = chip->ecc.strength;

	if (!eccbytes && eccstrength) {
		eccbytes = DIV_ROUND_UP(eccstrength * fls(8 * eccsize), 8);
		chip->ecc.bytes = eccbytes;
	}

	if (!eccsize || !eccbytes) {
		pr_warn("ecc parameters not supplied\n");
		return -EINVAL;
	}

	m = fls(1+8*eccsize);
	t = (eccbytes*8)/m;

	nbc = kzalloc(sizeof(*nbc), GFP_KERNEL);
	if (!nbc)
		return -ENOMEM;

	chip->ecc.priv = nbc;

	nbc->bch = bch_init(m, t, 0, false);
	if (!nbc->bch)
		goto fail;

	/* verify that eccbytes has the expected value */
	if (nbc->bch->ecc_bytes != eccbytes) {
		pr_warn("invalid eccbytes %u, should be %u\n",
			eccbytes, nbc->bch->ecc_bytes);
		goto fail;
	}

	eccsteps = mtd->writesize/eccsize;

	/* Check that we have an oob layout description. */
	if (!mtd->ooblayout) {
		pr_warn("missing oob scheme");
		goto fail;
	}

	/* sanity checks */
	if (8*(eccsize+eccbytes) >= (1 << m)) {
		pr_warn("eccsize %u is too large\n", eccsize);
		goto fail;
	}

	/*
	 * ecc->steps and ecc->total might be used by mtd->ooblayout->ecc(),
	 * which is called by mtd_ooblayout_count_eccbytes().
	 * Make sure they are properly initialized before calling
	 * mtd_ooblayout_count_eccbytes().
	 * FIXME: we should probably rework the sequencing in nand_scan_tail()
	 * to avoid setting those fields twice.
	 */
	chip->ecc.steps = eccsteps;
	chip->ecc.total = eccsteps * eccbytes;
	nand->base.ecc.ctx.total = chip->ecc.total;
	if (mtd_ooblayout_count_eccbytes(mtd) != (eccsteps*eccbytes)) {
		pr_warn("invalid ecc layout\n");
		goto fail;
	}

	nbc->eccmask = kzalloc(eccbytes, GFP_KERNEL);
	nbc->errloc = kmalloc_array(t, sizeof(*nbc->errloc), GFP_KERNEL);
	if (!nbc->eccmask || !nbc->errloc)
		goto fail;

	/*
	 * compute and store the inverted ecc of an erased ecc block
	 */
	erased_page = kmalloc(eccsize, GFP_KERNEL);
	if (!erased_page)
		goto fail;

	memset(erased_page, 0xff, eccsize);
	bch_encode(nbc->bch, erased_page, eccsize, nbc->eccmask);
	kfree(erased_page);

	for (i = 0; i < eccbytes; i++)
		nbc->eccmask[i] ^= 0xff;

	if (!eccstrength)
		chip->ecc.strength = (eccbytes * 8) / fls(8 * eccsize);

	return 0;

fail:
	nand_ecc_sw_bch_cleanup(nand);

	return -EINVAL;
}
EXPORT_SYMBOL(nand_ecc_sw_bch_init);

/**
 * nand_ecc_sw_bch_cleanup - Cleanup software BCH ECC resources
 * @nand: NAND device
 */
void nand_ecc_sw_bch_cleanup(struct nand_device *nand)
{
	struct nand_chip *chip = mtd_to_nand(nanddev_to_mtd(nand));
	struct nand_bch_control *nbc = chip->ecc.priv;

	if (nbc) {
		bch_free(nbc->bch);
		kfree(nbc->errloc);
		kfree(nbc->eccmask);
		kfree(nbc);
	}
}
EXPORT_SYMBOL(nand_ecc_sw_bch_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Djelic <ivan.djelic@parrot.com>");
MODULE_DESCRIPTION("NAND software BCH ECC support");

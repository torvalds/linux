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
#include <linux/mtd/nand.h>
#include <linux/mtd/nand-ecc-sw-bch.h>

/**
 * nand_ecc_sw_bch_calculate - Calculate the ECC corresponding to a data block
 * @nand: NAND device
 * @buf: Input buffer with raw data
 * @code: Output buffer with ECC
 */
int nand_ecc_sw_bch_calculate(struct nand_device *nand,
			      const unsigned char *buf, unsigned char *code)
{
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;
	unsigned int i;

	memset(code, 0, engine_conf->code_size);
	bch_encode(engine_conf->bch, buf, nand->ecc.ctx.conf.step_size, code);

	/* apply mask so that an erased page is a valid codeword */
	for (i = 0; i < engine_conf->code_size; i++)
		code[i] ^= engine_conf->eccmask[i];

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
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;
	unsigned int step_size = nand->ecc.ctx.conf.step_size;
	unsigned int *errloc = engine_conf->errloc;
	int i, count;

	count = bch_decode(engine_conf->bch, NULL, step_size, read_ecc,
			   calc_ecc, NULL, errloc);
	if (count > 0) {
		for (i = 0; i < count; i++) {
			if (errloc[i] < (step_size * 8))
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
 * Initialize NAND BCH error correction. @nand.ecc parameters 'step_size' and
 * 'bytes' are used to compute the following BCH parameters:
 *     m, the Galois field order
 *     t, the error correction capability
 * 'bytes' should be equal to the number of bytes required to store m * t
 * bits, where m is such that 2^m - 1 > step_size * 8.
 *
 * Example: to configure 4 bit correction per 512 bytes, you should pass
 * step_size = 512 (thus, m = 13 is the smallest integer such that 2^m - 1 > 512 * 8)
 * bytes = 7 (7 bytes are required to store m * t = 13 * 4 = 52 bits)
 */
int nand_ecc_sw_bch_init(struct nand_device *nand)
{
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	unsigned int m, t, eccsteps, i;
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;
	unsigned char *erased_page;
	unsigned int eccsize = nand->ecc.ctx.conf.step_size;
	unsigned int eccbytes = engine_conf->code_size;
	unsigned int eccstrength = nand->ecc.ctx.conf.strength;

	if (!eccbytes && eccstrength) {
		eccbytes = DIV_ROUND_UP(eccstrength * fls(8 * eccsize), 8);
		engine_conf->code_size = eccbytes;
	}

	if (!eccsize || !eccbytes) {
		pr_warn("ecc parameters not supplied\n");
		return -EINVAL;
	}

	m = fls(1+8*eccsize);
	t = (eccbytes*8)/m;

	engine_conf->bch = bch_init(m, t, 0, false);
	if (!engine_conf->bch)
		return -EINVAL;

	/* verify that eccbytes has the expected value */
	if (engine_conf->bch->ecc_bytes != eccbytes) {
		pr_warn("invalid eccbytes %u, should be %u\n",
			eccbytes, engine_conf->bch->ecc_bytes);
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

	if (mtd_ooblayout_count_eccbytes(mtd) != (eccsteps*eccbytes)) {
		pr_warn("invalid ecc layout\n");
		goto fail;
	}

	engine_conf->eccmask = kzalloc(eccbytes, GFP_KERNEL);
	engine_conf->errloc = kmalloc_array(t, sizeof(*engine_conf->errloc),
					    GFP_KERNEL);
	if (!engine_conf->eccmask || !engine_conf->errloc)
		goto fail;

	/*
	 * compute and store the inverted ecc of an erased ecc block
	 */
	erased_page = kmalloc(eccsize, GFP_KERNEL);
	if (!erased_page)
		goto fail;

	memset(erased_page, 0xff, eccsize);
	bch_encode(engine_conf->bch, erased_page, eccsize,
		   engine_conf->eccmask);
	kfree(erased_page);

	for (i = 0; i < eccbytes; i++)
		engine_conf->eccmask[i] ^= 0xff;

	if (!eccstrength)
		nand->ecc.ctx.conf.strength = (eccbytes * 8) / fls(8 * eccsize);

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
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;

	if (engine_conf) {
		bch_free(engine_conf->bch);
		kfree(engine_conf->errloc);
		kfree(engine_conf->eccmask);
	}
}
EXPORT_SYMBOL(nand_ecc_sw_bch_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Djelic <ivan.djelic@parrot.com>");
MODULE_DESCRIPTION("NAND software BCH ECC support");

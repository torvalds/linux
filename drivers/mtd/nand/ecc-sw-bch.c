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
 * nand_ecc_sw_bch_cleanup - Cleanup software BCH ECC resources
 * @nand: NAND device
 */
static void nand_ecc_sw_bch_cleanup(struct nand_device *nand)
{
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;

	bch_free(engine_conf->bch);
	kfree(engine_conf->errloc);
	kfree(engine_conf->eccmask);
}

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
static int nand_ecc_sw_bch_init(struct nand_device *nand)
{
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;
	unsigned int eccsize = nand->ecc.ctx.conf.step_size;
	unsigned int eccbytes = engine_conf->code_size;
	unsigned int m, t, i;
	unsigned char *erased_page;
	int ret;

	m = fls(1 + (8 * eccsize));
	t = (eccbytes * 8) / m;

	engine_conf->bch = bch_init(m, t, 0, false);
	if (!engine_conf->bch)
		return -EINVAL;

	engine_conf->eccmask = kzalloc(eccbytes, GFP_KERNEL);
	engine_conf->errloc = kmalloc_array(t, sizeof(*engine_conf->errloc),
					    GFP_KERNEL);
	if (!engine_conf->eccmask || !engine_conf->errloc) {
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Compute and store the inverted ECC of an erased step */
	erased_page = kmalloc(eccsize, GFP_KERNEL);
	if (!erased_page) {
		ret = -ENOMEM;
		goto cleanup;
	}

	memset(erased_page, 0xff, eccsize);
	bch_encode(engine_conf->bch, erased_page, eccsize,
		   engine_conf->eccmask);
	kfree(erased_page);

	for (i = 0; i < eccbytes; i++)
		engine_conf->eccmask[i] ^= 0xff;

	/* Verify that the number of code bytes has the expected value */
	if (engine_conf->bch->ecc_bytes != eccbytes) {
		pr_err("Invalid number of ECC bytes: %u, expected: %u\n",
		       eccbytes, engine_conf->bch->ecc_bytes);
		ret = -EINVAL;
		goto cleanup;
	}

	/* Sanity checks */
	if (8 * (eccsize + eccbytes) >= (1 << m)) {
		pr_err("ECC step size is too large (%u)\n", eccsize);
		ret = -EINVAL;
		goto cleanup;
	}

	return 0;

cleanup:
	nand_ecc_sw_bch_cleanup(nand);

	return ret;
}

int nand_ecc_sw_bch_init_ctx(struct nand_device *nand)
{
	struct nand_ecc_props *conf = &nand->ecc.ctx.conf;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	struct nand_ecc_sw_bch_conf *engine_conf;
	unsigned int code_size = 0, nsteps;
	int ret;

	/* Only large page NAND chips may use BCH */
	if (mtd->oobsize < 64) {
		pr_err("BCH cannot be used with small page NAND chips\n");
		return -EINVAL;
	}

	if (!mtd->ooblayout)
		mtd_set_ooblayout(mtd, nand_get_large_page_ooblayout());

	conf->engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
	conf->algo = NAND_ECC_ALGO_BCH;
	conf->step_size = nand->ecc.user_conf.step_size;
	conf->strength = nand->ecc.user_conf.strength;

	/*
	 * Board driver should supply ECC size and ECC strength
	 * values to select how many bits are correctable.
	 * Otherwise, default to 512 bytes for large page devices and 256 for
	 * small page devices.
	 */
	if (!conf->step_size) {
		if (mtd->oobsize >= 64)
			conf->step_size = 512;
		else
			conf->step_size = 256;

		conf->strength = 4;
	}

	nsteps = mtd->writesize / conf->step_size;

	/* Maximize */
	if (nand->ecc.user_conf.flags & NAND_ECC_MAXIMIZE_STRENGTH) {
		conf->step_size = 1024;
		nsteps = mtd->writesize / conf->step_size;
		/* Reserve 2 bytes for the BBM */
		code_size = (mtd->oobsize - 2) / nsteps;
		conf->strength = code_size * 8 / fls(8 * conf->step_size);
	}

	if (!code_size)
		code_size = DIV_ROUND_UP(conf->strength *
					 fls(8 * conf->step_size), 8);

	if (!conf->strength)
		conf->strength = (code_size * 8) / fls(8 * conf->step_size);

	if (!code_size && !conf->strength) {
		pr_err("Missing ECC parameters\n");
		return -EINVAL;
	}

	engine_conf = kzalloc(sizeof(*engine_conf), GFP_KERNEL);
	if (!engine_conf)
		return -ENOMEM;

	ret = nand_ecc_init_req_tweaking(&engine_conf->req_ctx, nand);
	if (ret)
		goto free_engine_conf;

	engine_conf->code_size = code_size;
	engine_conf->calc_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
	engine_conf->code_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
	if (!engine_conf->calc_buf || !engine_conf->code_buf) {
		ret = -ENOMEM;
		goto free_bufs;
	}

	nand->ecc.ctx.priv = engine_conf;
	nand->ecc.ctx.nsteps = nsteps;
	nand->ecc.ctx.total = nsteps * code_size;

	ret = nand_ecc_sw_bch_init(nand);
	if (ret)
		goto free_bufs;

	/* Verify the layout validity */
	if (mtd_ooblayout_count_eccbytes(mtd) !=
	    nand->ecc.ctx.nsteps * engine_conf->code_size) {
		pr_err("Invalid ECC layout\n");
		ret = -EINVAL;
		goto cleanup_bch_ctx;
	}

	return 0;

cleanup_bch_ctx:
	nand_ecc_sw_bch_cleanup(nand);
free_bufs:
	nand_ecc_cleanup_req_tweaking(&engine_conf->req_ctx);
	kfree(engine_conf->calc_buf);
	kfree(engine_conf->code_buf);
free_engine_conf:
	kfree(engine_conf);

	return ret;
}
EXPORT_SYMBOL(nand_ecc_sw_bch_init_ctx);

void nand_ecc_sw_bch_cleanup_ctx(struct nand_device *nand)
{
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;

	if (engine_conf) {
		nand_ecc_sw_bch_cleanup(nand);
		nand_ecc_cleanup_req_tweaking(&engine_conf->req_ctx);
		kfree(engine_conf->calc_buf);
		kfree(engine_conf->code_buf);
		kfree(engine_conf);
	}
}
EXPORT_SYMBOL(nand_ecc_sw_bch_cleanup_ctx);

static int nand_ecc_sw_bch_prepare_io_req(struct nand_device *nand,
					  struct nand_page_io_req *req)
{
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int eccsize = nand->ecc.ctx.conf.step_size;
	int eccbytes = engine_conf->code_size;
	int eccsteps = nand->ecc.ctx.nsteps;
	int total = nand->ecc.ctx.total;
	u8 *ecccalc = engine_conf->calc_buf;
	const u8 *data;
	int i;

	/* Nothing to do for a raw operation */
	if (req->mode == MTD_OPS_RAW)
		return 0;

	/* This engine does not provide BBM/free OOB bytes protection */
	if (!req->datalen)
		return 0;

	nand_ecc_tweak_req(&engine_conf->req_ctx, req);

	/* No more preparation for page read */
	if (req->type == NAND_PAGE_READ)
		return 0;

	/* Preparation for page write: derive the ECC bytes and place them */
	for (i = 0, data = req->databuf.out;
	     eccsteps;
	     eccsteps--, i += eccbytes, data += eccsize)
		nand_ecc_sw_bch_calculate(nand, data, &ecccalc[i]);

	return mtd_ooblayout_set_eccbytes(mtd, ecccalc, (void *)req->oobbuf.out,
					  0, total);
}

static int nand_ecc_sw_bch_finish_io_req(struct nand_device *nand,
					 struct nand_page_io_req *req)
{
	struct nand_ecc_sw_bch_conf *engine_conf = nand->ecc.ctx.priv;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int eccsize = nand->ecc.ctx.conf.step_size;
	int total = nand->ecc.ctx.total;
	int eccbytes = engine_conf->code_size;
	int eccsteps = nand->ecc.ctx.nsteps;
	u8 *ecccalc = engine_conf->calc_buf;
	u8 *ecccode = engine_conf->code_buf;
	unsigned int max_bitflips = 0;
	u8 *data = req->databuf.in;
	int i, ret;

	/* Nothing to do for a raw operation */
	if (req->mode == MTD_OPS_RAW)
		return 0;

	/* This engine does not provide BBM/free OOB bytes protection */
	if (!req->datalen)
		return 0;

	/* No more preparation for page write */
	if (req->type == NAND_PAGE_WRITE) {
		nand_ecc_restore_req(&engine_conf->req_ctx, req);
		return 0;
	}

	/* Finish a page read: retrieve the (raw) ECC bytes*/
	ret = mtd_ooblayout_get_eccbytes(mtd, ecccode, req->oobbuf.in, 0,
					 total);
	if (ret)
		return ret;

	/* Calculate the ECC bytes */
	for (i = 0; eccsteps; eccsteps--, i += eccbytes, data += eccsize)
		nand_ecc_sw_bch_calculate(nand, data, &ecccalc[i]);

	/* Finish a page read: compare and correct */
	for (eccsteps = nand->ecc.ctx.nsteps, i = 0, data = req->databuf.in;
	     eccsteps;
	     eccsteps--, i += eccbytes, data += eccsize) {
		int stat =  nand_ecc_sw_bch_correct(nand, data,
						    &ecccode[i],
						    &ecccalc[i]);
		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}

	nand_ecc_restore_req(&engine_conf->req_ctx, req);

	return max_bitflips;
}

static const struct nand_ecc_engine_ops nand_ecc_sw_bch_engine_ops = {
	.init_ctx = nand_ecc_sw_bch_init_ctx,
	.cleanup_ctx = nand_ecc_sw_bch_cleanup_ctx,
	.prepare_io_req = nand_ecc_sw_bch_prepare_io_req,
	.finish_io_req = nand_ecc_sw_bch_finish_io_req,
};

static struct nand_ecc_engine nand_ecc_sw_bch_engine = {
	.ops = &nand_ecc_sw_bch_engine_ops,
};

struct nand_ecc_engine *nand_ecc_sw_bch_get_engine(void)
{
	return &nand_ecc_sw_bch_engine;
}
EXPORT_SYMBOL(nand_ecc_sw_bch_get_engine);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Djelic <ivan.djelic@parrot.com>");
MODULE_DESCRIPTION("NAND software BCH ECC support");

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
 * This file contains all ONFI helpers.
 */

#include <linux/slab.h>

#include "internals.h"

u16 onfi_crc16(u16 crc, u8 const *p, size_t len)
{
	int i;
	while (len--) {
		crc ^= *p++ << 8;
		for (i = 0; i < 8; i++)
			crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
	}

	return crc;
}

/* Parse the Extended Parameter Page. */
static int nand_flash_detect_ext_param_page(struct nand_chip *chip,
					    struct nand_onfi_params *p)
{
	struct onfi_ext_param_page *ep;
	struct onfi_ext_section *s;
	struct onfi_ext_ecc_info *ecc;
	uint8_t *cursor;
	int ret;
	int len;
	int i;

	len = le16_to_cpu(p->ext_param_page_length) * 16;
	ep = kmalloc(len, GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	/* Send our own NAND_CMD_PARAM. */
	ret = nand_read_param_page_op(chip, 0, NULL, 0);
	if (ret)
		goto ext_out;

	/* Use the Change Read Column command to skip the ONFI param pages. */
	ret = nand_change_read_column_op(chip,
					 sizeof(*p) * p->num_of_param_pages,
					 ep, len, true);
	if (ret)
		goto ext_out;

	ret = -EINVAL;
	if ((onfi_crc16(ONFI_CRC_BASE, ((uint8_t *)ep) + 2, len - 2)
		!= le16_to_cpu(ep->crc))) {
		pr_debug("fail in the CRC.\n");
		goto ext_out;
	}

	/*
	 * Check the signature.
	 * Do not strictly follow the ONFI spec, maybe changed in future.
	 */
	if (strncmp(ep->sig, "EPPS", 4)) {
		pr_debug("The signature is invalid.\n");
		goto ext_out;
	}

	/* find the ECC section. */
	cursor = (uint8_t *)(ep + 1);
	for (i = 0; i < ONFI_EXT_SECTION_MAX; i++) {
		s = ep->sections + i;
		if (s->type == ONFI_SECTION_TYPE_2)
			break;
		cursor += s->length * 16;
	}
	if (i == ONFI_EXT_SECTION_MAX) {
		pr_debug("We can not find the ECC section.\n");
		goto ext_out;
	}

	/* get the info we want. */
	ecc = (struct onfi_ext_ecc_info *)cursor;

	if (!ecc->codeword_size) {
		pr_debug("Invalid codeword size\n");
		goto ext_out;
	}

	chip->base.eccreq.strength = ecc->ecc_bits;
	chip->base.eccreq.step_size = 1 << ecc->codeword_size;
	ret = 0;

ext_out:
	kfree(ep);
	return ret;
}

/*
 * Recover data with bit-wise majority
 */
static void nand_bit_wise_majority(const void **srcbufs,
				   unsigned int nsrcbufs,
				   void *dstbuf,
				   unsigned int bufsize)
{
	int i, j, k;

	for (i = 0; i < bufsize; i++) {
		u8 val = 0;

		for (j = 0; j < 8; j++) {
			unsigned int cnt = 0;

			for (k = 0; k < nsrcbufs; k++) {
				const u8 *srcbuf = srcbufs[k];

				if (srcbuf[i] & BIT(j))
					cnt++;
			}

			if (cnt > nsrcbufs / 2)
				val |= BIT(j);
		}

		((u8 *)dstbuf)[i] = val;
	}
}

/*
 * Check if the NAND chip is ONFI compliant, returns 1 if it is, 0 otherwise.
 */
int nand_onfi_detect(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_memory_organization *memorg;
	struct nand_onfi_params *p;
	struct onfi_params *onfi;
	int onfi_version = 0;
	char id[4];
	int i, ret, val;
	u16 crc;

	memorg = nanddev_get_memorg(&chip->base);

	/* Try ONFI for unknown chip or LP */
	ret = nand_readid_op(chip, 0x20, id, sizeof(id));
	if (ret || strncmp(id, "ONFI", 4))
		return 0;

	/* ONFI chip: allocate a buffer to hold its parameter page */
	p = kzalloc((sizeof(*p) * 3), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	ret = nand_read_param_page_op(chip, 0, NULL, 0);
	if (ret) {
		ret = 0;
		goto free_onfi_param_page;
	}

	for (i = 0; i < 3; i++) {
		ret = nand_read_data_op(chip, &p[i], sizeof(*p), true);
		if (ret) {
			ret = 0;
			goto free_onfi_param_page;
		}

		crc = onfi_crc16(ONFI_CRC_BASE, (u8 *)&p[i], 254);
		if (crc == le16_to_cpu(p[i].crc)) {
			if (i)
				memcpy(p, &p[i], sizeof(*p));
			break;
		}
	}

	if (i == 3) {
		const void *srcbufs[3] = {p, p + 1, p + 2};

		pr_warn("Could not find a valid ONFI parameter page, trying bit-wise majority to recover it\n");
		nand_bit_wise_majority(srcbufs, ARRAY_SIZE(srcbufs), p,
				       sizeof(*p));

		crc = onfi_crc16(ONFI_CRC_BASE, (u8 *)p, 254);
		if (crc != le16_to_cpu(p->crc)) {
			pr_err("ONFI parameter recovery failed, aborting\n");
			goto free_onfi_param_page;
		}
	}

	if (chip->manufacturer.desc && chip->manufacturer.desc->ops &&
	    chip->manufacturer.desc->ops->fixup_onfi_param_page)
		chip->manufacturer.desc->ops->fixup_onfi_param_page(chip, p);

	/* Check version */
	val = le16_to_cpu(p->revision);
	if (val & ONFI_VERSION_2_3)
		onfi_version = 23;
	else if (val & ONFI_VERSION_2_2)
		onfi_version = 22;
	else if (val & ONFI_VERSION_2_1)
		onfi_version = 21;
	else if (val & ONFI_VERSION_2_0)
		onfi_version = 20;
	else if (val & ONFI_VERSION_1_0)
		onfi_version = 10;

	if (!onfi_version) {
		pr_info("unsupported ONFI version: %d\n", val);
		goto free_onfi_param_page;
	}

	sanitize_string(p->manufacturer, sizeof(p->manufacturer));
	sanitize_string(p->model, sizeof(p->model));
	chip->parameters.model = kstrdup(p->model, GFP_KERNEL);
	if (!chip->parameters.model) {
		ret = -ENOMEM;
		goto free_onfi_param_page;
	}

	memorg->pagesize = le32_to_cpu(p->byte_per_page);
	mtd->writesize = memorg->pagesize;

	/*
	 * pages_per_block and blocks_per_lun may not be a power-of-2 size
	 * (don't ask me who thought of this...). MTD assumes that these
	 * dimensions will be power-of-2, so just truncate the remaining area.
	 */
	memorg->pages_per_eraseblock =
			1 << (fls(le32_to_cpu(p->pages_per_block)) - 1);
	mtd->erasesize = memorg->pages_per_eraseblock * memorg->pagesize;

	memorg->oobsize = le16_to_cpu(p->spare_bytes_per_page);
	mtd->oobsize = memorg->oobsize;

	memorg->luns_per_target = p->lun_count;
	memorg->planes_per_lun = 1 << p->interleaved_bits;

	/* See erasesize comment */
	memorg->eraseblocks_per_lun =
		1 << (fls(le32_to_cpu(p->blocks_per_lun)) - 1);
	memorg->max_bad_eraseblocks_per_lun = le32_to_cpu(p->blocks_per_lun);
	memorg->bits_per_cell = p->bits_per_cell;

	if (le16_to_cpu(p->features) & ONFI_FEATURE_16_BIT_BUS)
		chip->options |= NAND_BUSWIDTH_16;

	if (p->ecc_bits != 0xff) {
		chip->base.eccreq.strength = p->ecc_bits;
		chip->base.eccreq.step_size = 512;
	} else if (onfi_version >= 21 &&
		(le16_to_cpu(p->features) & ONFI_FEATURE_EXT_PARAM_PAGE)) {

		/*
		 * The nand_flash_detect_ext_param_page() uses the
		 * Change Read Column command which maybe not supported
		 * by the chip->legacy.cmdfunc. So try to update the
		 * chip->legacy.cmdfunc now. We do not replace user supplied
		 * command function.
		 */
		nand_legacy_adjust_cmdfunc(chip);

		/* The Extended Parameter Page is supported since ONFI 2.1. */
		if (nand_flash_detect_ext_param_page(chip, p))
			pr_warn("Failed to detect ONFI extended param page\n");
	} else {
		pr_warn("Could not retrieve ONFI ECC requirements\n");
	}

	/* Save some parameters from the parameter page for future use */
	if (le16_to_cpu(p->opt_cmd) & ONFI_OPT_CMD_SET_GET_FEATURES) {
		chip->parameters.supports_set_get_features = true;
		bitmap_set(chip->parameters.get_feature_list,
			   ONFI_FEATURE_ADDR_TIMING_MODE, 1);
		bitmap_set(chip->parameters.set_feature_list,
			   ONFI_FEATURE_ADDR_TIMING_MODE, 1);
	}

	onfi = kzalloc(sizeof(*onfi), GFP_KERNEL);
	if (!onfi) {
		ret = -ENOMEM;
		goto free_model;
	}

	onfi->version = onfi_version;
	onfi->tPROG = le16_to_cpu(p->t_prog);
	onfi->tBERS = le16_to_cpu(p->t_bers);
	onfi->tR = le16_to_cpu(p->t_r);
	onfi->tCCS = le16_to_cpu(p->t_ccs);
	onfi->async_timing_mode = le16_to_cpu(p->async_timing_mode);
	onfi->vendor_revision = le16_to_cpu(p->vendor_revision);
	memcpy(onfi->vendor, p->vendor, sizeof(p->vendor));
	chip->parameters.onfi = onfi;

	/* Identification done, free the full ONFI parameter page and exit */
	kfree(p);

	return 1;

free_model:
	kfree(chip->parameters.model);
free_onfi_param_page:
	kfree(p);

	return ret;
}

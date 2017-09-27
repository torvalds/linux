/*
 * OMAP Crypto driver common support routines.
 *
 * Copyright (c) 2017 Texas Instruments Incorporated
 *   Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <crypto/scatterwalk.h>

#include "omap-crypto.h"

static int omap_crypto_copy_sg_lists(int total, int bs,
				     struct scatterlist **sg,
				     struct scatterlist *new_sg, u16 flags)
{
	int n = sg_nents(*sg);
	struct scatterlist *tmp;

	if (!(flags & OMAP_CRYPTO_FORCE_SINGLE_ENTRY)) {
		new_sg = kmalloc_array(n, sizeof(*sg), GFP_KERNEL);
		if (!new_sg)
			return -ENOMEM;

		sg_init_table(new_sg, n);
	}

	tmp = new_sg;

	while (*sg && total) {
		int len = (*sg)->length;

		if (total < len)
			len = total;

		if (len > 0) {
			total -= len;
			sg_set_page(tmp, sg_page(*sg), len, (*sg)->offset);
			if (total <= 0)
				sg_mark_end(tmp);
			tmp = sg_next(tmp);
		}

		*sg = sg_next(*sg);
	}

	*sg = new_sg;

	return 0;
}

static int omap_crypto_copy_sgs(int total, int bs, struct scatterlist **sg,
				struct scatterlist *new_sg, u16 flags)
{
	void *buf;
	int pages;
	int new_len;

	new_len = ALIGN(total, bs);
	pages = get_order(new_len);

	buf = (void *)__get_free_pages(GFP_ATOMIC, pages);
	if (!buf) {
		pr_err("%s: Couldn't allocate pages for unaligned cases.\n",
		       __func__);
		return -ENOMEM;
	}

	if (flags & OMAP_CRYPTO_COPY_DATA) {
		scatterwalk_map_and_copy(buf, *sg, 0, total, 0);
		if (flags & OMAP_CRYPTO_ZERO_BUF)
			memset(buf + total, 0, new_len - total);
	}

	if (!(flags & OMAP_CRYPTO_FORCE_SINGLE_ENTRY))
		sg_init_table(new_sg, 1);

	sg_set_buf(new_sg, buf, new_len);

	*sg = new_sg;

	return 0;
}

static int omap_crypto_check_sg(struct scatterlist *sg, int total, int bs,
				u16 flags)
{
	int len = 0;
	int num_sg = 0;

	if (!IS_ALIGNED(total, bs))
		return OMAP_CRYPTO_NOT_ALIGNED;

	while (sg) {
		num_sg++;

		if (!IS_ALIGNED(sg->offset, 4))
			return OMAP_CRYPTO_NOT_ALIGNED;
		if (!IS_ALIGNED(sg->length, bs))
			return OMAP_CRYPTO_NOT_ALIGNED;

		len += sg->length;
		sg = sg_next(sg);

		if (len >= total)
			break;
	}

	if ((flags & OMAP_CRYPTO_FORCE_SINGLE_ENTRY) && num_sg > 1)
		return OMAP_CRYPTO_NOT_ALIGNED;

	if (len != total)
		return OMAP_CRYPTO_BAD_DATA_LENGTH;

	return 0;
}

int omap_crypto_align_sg(struct scatterlist **sg, int total, int bs,
			 struct scatterlist *new_sg, u16 flags,
			 u8 flags_shift, unsigned long *dd_flags)
{
	int ret;

	*dd_flags &= ~(OMAP_CRYPTO_COPY_MASK << flags_shift);

	if (flags & OMAP_CRYPTO_FORCE_COPY)
		ret = OMAP_CRYPTO_NOT_ALIGNED;
	else
		ret = omap_crypto_check_sg(*sg, total, bs, flags);

	if (ret == OMAP_CRYPTO_NOT_ALIGNED) {
		ret = omap_crypto_copy_sgs(total, bs, sg, new_sg, flags);
		if (ret)
			return ret;
		*dd_flags |= OMAP_CRYPTO_DATA_COPIED << flags_shift;
	} else if (ret == OMAP_CRYPTO_BAD_DATA_LENGTH) {
		ret = omap_crypto_copy_sg_lists(total, bs, sg, new_sg, flags);
		if (ret)
			return ret;
		if (!(flags & OMAP_CRYPTO_FORCE_SINGLE_ENTRY))
			*dd_flags |= OMAP_CRYPTO_SG_COPIED << flags_shift;
	} else if (flags & OMAP_CRYPTO_FORCE_SINGLE_ENTRY) {
		sg_set_buf(new_sg, sg_virt(*sg), (*sg)->length);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(omap_crypto_align_sg);

void omap_crypto_cleanup(struct scatterlist *sg, struct scatterlist *orig,
			 int offset, int len, u8 flags_shift,
			 unsigned long flags)
{
	void *buf;
	int pages;

	flags >>= flags_shift;
	flags &= OMAP_CRYPTO_COPY_MASK;

	if (!flags)
		return;

	buf = sg_virt(sg);
	pages = get_order(len);

	if (orig && (flags & OMAP_CRYPTO_COPY_MASK))
		scatterwalk_map_and_copy(buf, orig, offset, len, 1);

	if (flags & OMAP_CRYPTO_DATA_COPIED)
		free_pages((unsigned long)buf, pages);
	else if (flags & OMAP_CRYPTO_SG_COPIED)
		kfree(sg);
}
EXPORT_SYMBOL_GPL(omap_crypto_cleanup);

MODULE_DESCRIPTION("OMAP crypto support library.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tero Kristo <t-kristo@ti.com>");

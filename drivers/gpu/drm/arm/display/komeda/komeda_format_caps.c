// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#include <linux/slab.h>
#include "komeda_format_caps.h"
#include "malidp_utils.h"

const struct komeda_format_caps *
komeda_get_format_caps(struct komeda_format_caps_table *table,
		       u32 fourcc, u64 modifier)
{
	const struct komeda_format_caps *caps;
	u64 afbc_features = modifier & ~(AFBC_FORMAT_MOD_BLOCK_SIZE_MASK);
	u32 afbc_layout = modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK;
	int id;

	for (id = 0; id < table->n_formats; id++) {
		caps = &table->format_caps[id];

		if (fourcc != caps->fourcc)
			continue;

		if ((modifier == 0ULL) && (caps->supported_afbc_layouts == 0))
			return caps;

		if (has_bits(afbc_features, caps->supported_afbc_features) &&
		    has_bit(afbc_layout, caps->supported_afbc_layouts))
			return caps;
	}

	return NULL;
}

u32 *komeda_get_layer_fourcc_list(struct komeda_format_caps_table *table,
				  u32 layer_type, u32 *n_fmts)
{
	const struct komeda_format_caps *cap;
	u32 *fmts;
	int i, j, n = 0;

	fmts = kcalloc(table->n_formats, sizeof(u32), GFP_KERNEL);
	if (!fmts)
		return NULL;

	for (i = 0; i < table->n_formats; i++) {
		cap = &table->format_caps[i];
		if (!(layer_type & cap->supported_layer_types) ||
		    (cap->fourcc == 0))
			continue;

		/* one fourcc may has two caps items in table (afbc/none-afbc),
		 * so check the existing list to avoid adding a duplicated one.
		 */
		for (j = n - 1; j >= 0; j--)
			if (fmts[j] == cap->fourcc)
				break;

		if (j < 0)
			fmts[n++] = cap->fourcc;
	}

	if (n_fmts)
		*n_fmts = n;

	return fmts;
}

void komeda_put_fourcc_list(u32 *fourcc_list)
{
	kfree(fourcc_list);
}

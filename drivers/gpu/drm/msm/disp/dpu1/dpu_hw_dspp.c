// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_dspp.h"
#include "dpu_kms.h"


static void _setup_dspp_ops(struct dpu_hw_dspp *c,
		unsigned long features)
{
	return;
}

static const struct dpu_dspp_cfg *_dspp_offset(enum dpu_dspp dspp,
		const struct dpu_mdss_cfg *m,
		void __iomem *addr,
		struct dpu_hw_blk_reg_map *b)
{
	int i;

	if (!m || !addr || !b)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < m->dspp_count; i++) {
		if (dspp == m->dspp[i].id) {
			b->base_off = addr;
			b->blk_off = m->dspp[i].base;
			b->length = m->dspp[i].len;
			b->hwversion = m->hwversion;
			b->log_mask = DPU_DBG_MASK_DSPP;
			return &m->dspp[i];
		}
	}

	return ERR_PTR(-EINVAL);
}

static struct dpu_hw_blk_ops dpu_hw_ops;

struct dpu_hw_dspp *dpu_hw_dspp_init(enum dpu_dspp idx,
			void __iomem *addr,
			const struct dpu_mdss_cfg *m)
{
	struct dpu_hw_dspp *c;
	const struct dpu_dspp_cfg *cfg;

	if (!addr || !m)
		return ERR_PTR(-EINVAL);

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	cfg = _dspp_offset(idx, m, addr, &c->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(c);
		return ERR_PTR(-EINVAL);
	}

	/* Assign ops */
	c->idx = idx;
	c->cap = cfg;
	_setup_dspp_ops(c, c->cap->features);

	dpu_hw_blk_init(&c->base, DPU_HW_BLK_DSPP, idx, &dpu_hw_ops);

	return c;
}

void dpu_hw_dspp_destroy(struct dpu_hw_dspp *dspp)
{
	if (dspp)
		dpu_hw_blk_destroy(&dspp->base);

	kfree(dspp);
}



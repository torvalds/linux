// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 */

#include <asm/io-mpt-s2mpu.h>

static void __set_l1entry_attr_with_prot(void *dev_va, unsigned int gb,
					 unsigned int vid, enum mpt_prot prot)
{
	writel_relaxed(L1ENTRY_ATTR_1G(prot),
		       dev_va + REG_NS_L1ENTRY_ATTR(vid, gb));
}

static void __set_l1entry_attr_with_fmpt(void *dev_va, unsigned int gb,
					 unsigned int vid, struct fmpt *fmpt)
{
	if (fmpt->gran_1g) {
		__set_l1entry_attr_with_prot(dev_va, gb, vid, fmpt->prot);
	} else {
		/* Order against writes to the SMPT. */
		writel(L1ENTRY_ATTR_L2(SMPT_GRAN_ATTR),
		       dev_va + REG_NS_L1ENTRY_ATTR(vid, gb));
	}
}

static void __set_l1entry_l2table_addr(void *dev_va, unsigned int gb,
				       unsigned int vid, phys_addr_t addr)
{
	/* Order against writes to the SMPT. */
	writel(L1ENTRY_L2TABLE_ADDR(addr),
	       dev_va + REG_NS_L1ENTRY_L2TABLE_ADDR(vid, gb));
}

static void init_with_prot(void *dev_va, enum mpt_prot prot)
{
	unsigned int gb, vid;

	for_each_gb_and_vid(gb, vid)
		__set_l1entry_attr_with_prot(dev_va, gb, vid, prot);
}

static void init_with_mpt(void *dev_va, struct mpt *mpt)
{
	unsigned int gb, vid;
	struct fmpt *fmpt;

	for_each_gb_and_vid(gb, vid) {
		fmpt = &mpt->fmpt[gb];
		__set_l1entry_l2table_addr(dev_va, gb, vid, __hyp_pa(fmpt->smpt));
		__set_l1entry_attr_with_fmpt(dev_va, gb, vid, fmpt);
	}
}

static void apply_range(void *dev_va, struct mpt *mpt, u32 first_gb, u32 last_gb)
{
	unsigned int gb, vid;
	struct fmpt *fmpt;

	for_each_gb_in_range(gb, first_gb, last_gb) {
		fmpt = &mpt->fmpt[gb];
		if (fmpt->flags & MPT_UPDATE_L1) {
			for_each_vid(vid)
				__set_l1entry_attr_with_fmpt(dev_va, gb, vid, fmpt);
		}
	}
}

static void prepare_range(struct mpt *mpt, phys_addr_t first_byte,
			  phys_addr_t last_byte, enum mpt_prot prot)
{
	unsigned int first_gb = first_byte / SZ_1G;
	unsigned int last_gb = last_byte / SZ_1G;
	size_t start_gb_byte, end_gb_byte;
	unsigned int gb;
	struct fmpt *fmpt;

	for_each_gb_in_range(gb, first_gb, last_gb) {
		fmpt = &mpt->fmpt[gb];
		start_gb_byte = (gb == first_gb) ? first_byte % SZ_1G : 0;
		end_gb_byte = (gb == last_gb) ? (last_byte % SZ_1G) + 1 : SZ_1G;

		__set_fmpt_range(fmpt, start_gb_byte, end_gb_byte, prot);

		if (fmpt->flags & MPT_UPDATE_L2)
			kvm_flush_dcache_to_poc(fmpt->smpt, SMPT_SIZE);
	}
}

static const struct s2mpu_mpt_ops this_ops = {
	.init_with_prot = init_with_prot,
	.init_with_mpt = init_with_mpt,
	.apply_range = apply_range,
	.prepare_range = prepare_range,
};

const struct s2mpu_mpt_ops *s2mpu_get_mpt_ops(struct s2mpu_mpt_cfg cfg)
{
	if ((cfg.version == S2MPU_VERSION_8) || (cfg.version == S2MPU_VERSION_9))
		return &this_ops;

	return NULL;
}

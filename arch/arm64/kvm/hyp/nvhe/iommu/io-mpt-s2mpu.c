// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 */

#include <asm/io-mpt-s2mpu.h>

#define GRAN_BYTE(gran)			((gran << V9_MPT_PROT_BITS) | (gran))
#define GRAN_HWORD(gran)		((GRAN_BYTE(gran) << 8) | (GRAN_BYTE(gran)))
#define GRAN_WORD(gran)			(((u32)(GRAN_HWORD(gran) << 16) | (GRAN_HWORD(gran))))
#define GRAN_DWORD(gran)		((u64)((u64)GRAN_WORD(gran) << 32) | (u64)(GRAN_WORD(gran)))

#define SMPT_NUM_TO_BYTE(x)		((x) / SMPT_GRAN / SMPT_ELEMS_PER_BYTE(config_prot_bits))
#define BYTE_TO_SMPT_INDEX(x)	((x) / SMPT_WORD_BYTE_RANGE(config_prot_bits))


/*
 * MPT table ops can be configured only for one version at runtime,
 * these variables will hold version specific data set a run time init, to avoid
 * having duplicate code or unnessery check during operations.
 */
static u32 config_prot_bits;
static u32 config_access_shift;
static const u64 *config_lut_prot;
static u32 config_gran_mask;
static u32 this_version;

/*
 * page table entries for different protection look up table
 * granularity is compile time config, so we can do this also for
 * this array without having duplicate arrays
 */
static const u64 v9_mpt_prot_doubleword[] = {
	[MPT_PROT_NONE] = 0x0000000000000000 | GRAN_DWORD(SMPT_GRAN_ATTR),
	[MPT_PROT_R]    = 0x4444444444444444 | GRAN_DWORD(SMPT_GRAN_ATTR),
	[MPT_PROT_W]	= 0x8888888888888888 | GRAN_DWORD(SMPT_GRAN_ATTR),
	[MPT_PROT_RW]   = 0xcccccccccccccccc | GRAN_DWORD(SMPT_GRAN_ATTR),
};
static const u64 mpt_prot_doubleword[] = {
	[MPT_PROT_NONE] = 0x0000000000000000,
	[MPT_PROT_R]    = 0x5555555555555555,
	[MPT_PROT_W]	= 0xaaaaaaaaaaaaaaaa,
	[MPT_PROT_RW]   = 0xffffffffffffffff,
};

static inline int pte_from_addr_smpt(u32 *smpt, u64 addr)
{
	u32 word_idx, idx, pte, val;

	word_idx = BYTE_TO_SMPT_INDEX(addr);
	val = READ_ONCE(smpt[word_idx]);
	idx  = (addr / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(config_prot_bits);

	pte  = (val >> (idx * config_prot_bits)) & ((1 << config_prot_bits)-1);
	return pte;
}

static inline int prot_from_addr_smpt(u32 *smpt, u64 addr)
{
	int pte = pte_from_addr_smpt(smpt, addr);

	return (pte >> config_access_shift);
}

/* Set protection bits of SMPT in a given range without using memset. */
static void __set_smpt_range_slow(u32 *smpt, size_t start_gb_byte,
					 size_t end_gb_byte, enum mpt_prot prot)
{
	size_t i, start_word_byte, end_word_byte, word_idx, first_elem, last_elem;
	u32 val;

	/* Iterate over u32 words. */
	start_word_byte = start_gb_byte;
	while (start_word_byte < end_gb_byte) {
		/* Determine the range of bytes covered by this word. */
		word_idx = BYTE_TO_SMPT_INDEX(start_word_byte);
		end_word_byte = min(
			ALIGN(start_word_byte + 1, SMPT_WORD_BYTE_RANGE(config_prot_bits)),
			end_gb_byte);

		/* Identify protection bit offsets within the word. */
		first_elem = (start_word_byte / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(config_prot_bits);
		last_elem =
			((end_word_byte - 1) / SMPT_GRAN) % SMPT_ELEMS_PER_WORD(config_prot_bits);

		/* Modify the corresponding word. */
		val = READ_ONCE(smpt[word_idx]);
		for (i = first_elem; i <= last_elem; i++) {
			val &= ~(MPT_PROT_MASK << (i * config_prot_bits + config_access_shift));
			val |= prot << (i * config_prot_bits + config_access_shift);
		}
		WRITE_ONCE(smpt[word_idx], val);

		start_word_byte = end_word_byte;
	}
}

/* Set protection bits of SMPT in a given range. */
static void __set_smpt_range(u32 *smpt, size_t start_gb_byte,
				    size_t end_gb_byte, enum mpt_prot prot)
{
	size_t interlude_start, interlude_end, interlude_bytes, word_idx;

	char prot_byte = (char)config_lut_prot[prot];

	if (start_gb_byte >= end_gb_byte)
		return;

	/* Check if range spans at least one full u32 word. */
	interlude_start = ALIGN(start_gb_byte, SMPT_WORD_BYTE_RANGE(config_prot_bits));
	interlude_end = ALIGN_DOWN(end_gb_byte, SMPT_WORD_BYTE_RANGE(config_prot_bits));

	/*
	 * If not, fall back to editing bits in the given range.
	 * sets bit for PTEs that are in less than 32 bits (can't be done by memset)
	 */
	if (interlude_start >= interlude_end) {
		__set_smpt_range_slow(smpt, start_gb_byte, end_gb_byte, prot);
		return;
	}

	/* Use bit-editing for prologue/epilogue, memset for interlude. */
	word_idx = BYTE_TO_SMPT_INDEX(interlude_start);
	interlude_bytes = SMPT_NUM_TO_BYTE(interlude_end - interlude_start);

	/*
	 * These are pages in the start and at then end that are
	 * not part of full 32 bit SMPT word.
	 */
	__set_smpt_range_slow(smpt, start_gb_byte, interlude_start, prot);
	memset(&smpt[word_idx], prot_byte, interlude_bytes);
	__set_smpt_range_slow(smpt, interlude_end, end_gb_byte, prot);
}

/* Returns true if all SMPT protection bits match 'prot'. */
static bool __is_smpt_uniform(u32 *smpt, enum mpt_prot prot)
{
	size_t i;
	u64 *doublewords = (u64 *)smpt;

	for (i = 0; i < SMPT_NUM_WORDS(config_prot_bits) / 2; i++) {
		if (doublewords[i] != config_lut_prot[prot])
			return false;
	}
	return true;
}

/*
 * Set protection bits of FMPT/SMPT in a given range.
 * Returns flags specifying whether L1/L2 changes need to be made visible
 * to the device.
 */
static void __set_fmpt_range(struct fmpt *fmpt, size_t start_gb_byte,
				    size_t end_gb_byte, enum mpt_prot prot)
{
	if (start_gb_byte == 0 && end_gb_byte >= SZ_1G) {
		/* Update covers the entire GB region. */
		if (fmpt->gran_1g && fmpt->prot == prot) {
			fmpt->flags = 0;
			return;
		}

		fmpt->gran_1g = true;
		fmpt->prot = prot;
		fmpt->flags = MPT_UPDATE_L1;
		return;
	}

	if (fmpt->gran_1g) {
		/* GB region currently uses 1G mapping. */
		if (fmpt->prot == prot) {
			fmpt->flags = 0;
			return;
		}

		/*
		 * Range has different mapping than the rest of the GB.
		 * Convert to PAGE_SIZE mapping.
		 */
		fmpt->gran_1g = false;
		__set_smpt_range(fmpt->smpt, 0, start_gb_byte, fmpt->prot);
		__set_smpt_range(fmpt->smpt, start_gb_byte, end_gb_byte, prot);
		__set_smpt_range(fmpt->smpt, end_gb_byte, SZ_1G, fmpt->prot);
		fmpt->flags = MPT_UPDATE_L1 | MPT_UPDATE_L2;
		return;
	}

	/* GB region currently uses PAGE_SIZE mapping. */
	__set_smpt_range(fmpt->smpt, start_gb_byte, end_gb_byte, prot);

	/* Check if the entire GB region has the same prot bits. */
	if (!__is_smpt_uniform(fmpt->smpt, prot)) {
		fmpt->flags = MPT_UPDATE_L2;
		return;
	}

	fmpt->gran_1g = true;
	fmpt->prot = prot;
	fmpt->flags = MPT_UPDATE_L1;
}

static u32 smpt_size(void)
{
	return SMPT_SIZE(config_prot_bits);
}

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
		writel(config_gran_mask | L1ENTRY_ATTR_L2TABLE_EN,
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
			kvm_flush_dcache_to_poc(fmpt->smpt, smpt_size());
	}
}

static const struct s2mpu_mpt_ops this_ops = {
	.smpt_size = smpt_size,
	.init_with_prot = init_with_prot,
	.init_with_mpt = init_with_mpt,
	.apply_range = apply_range,
	.prepare_range = prepare_range,
	.pte_from_addr_smpt = pte_from_addr_smpt,
};

const struct s2mpu_mpt_ops *s2mpu_get_mpt_ops(struct s2mpu_mpt_cfg cfg)
{

	/* If called before with different version return NULL. */
	if (WARN_ON(this_version && (this_version != cfg.version)))
		return NULL;
	/* 2MB granularity not supported in V9 */
	if ((cfg.version == S2MPU_VERSION_9) && (SMPT_GRAN_ATTR != L1ENTRY_ATTR_GRAN_2M)) {
		config_prot_bits = V9_MPT_PROT_BITS;
		config_access_shift = V9_MPT_ACCESS_SHIFT;
		config_lut_prot = v9_mpt_prot_doubleword;
		config_gran_mask = L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, V9_L1ENTRY_ATTR_GRAN_MASK);
		this_version = cfg.version;
		return &this_ops;
	} else if ((cfg.version == S2MPU_VERSION_2) || (cfg.version == S2MPU_VERSION_1)) {
		config_prot_bits = MPT_PROT_BITS;
		config_access_shift = MPT_ACCESS_SHIFT;
		config_lut_prot = mpt_prot_doubleword;
		config_gran_mask = L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, L1ENTRY_ATTR_GRAN_MASK);
		this_version = cfg.version;
		return &this_ops;
	}
	return NULL;
}

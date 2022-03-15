/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_KVM_S2MPU_H__
#define __ARM64_KVM_S2MPU_H__

#include <linux/bitfield.h>

#include <asm/kvm_mmu.h>

#define S2MPU_MMIO_SIZE				SZ_64K
#define SYSMMU_SYNC_MMIO_SIZE			SZ_64K
#define SYSMMU_SYNC_S2_OFFSET			SZ_32K
#define SYSMMU_SYNC_S2_MMIO_SIZE		(SYSMMU_SYNC_MMIO_SIZE - \
						 SYSMMU_SYNC_S2_OFFSET)

#define NR_VIDS					8
#define NR_CTX_IDS				8

#define ALL_VIDS_BITMAP				GENMASK(NR_VIDS - 1, 0)

#define REG_NS_CTRL0				0x0
#define REG_NS_CTRL1				0x4
#define REG_NS_CFG				0x10
#define REG_NS_INTERRUPT_ENABLE_PER_VID_SET	0x20
#define REG_NS_INTERRUPT_CLEAR			0x2c
#define REG_NS_VERSION				0x60
#define REG_NS_STATUS				0x68
#define REG_NS_NUM_CONTEXT			0x100
#define REG_NS_CONTEXT_CFG_VALID_VID		0x104
#define REG_NS_ALL_INVALIDATION			0x1000
#define REG_NS_RANGE_INVALIDATION		0x1020
#define REG_NS_RANGE_INVALIDATION_START_PPN	0x1024
#define REG_NS_RANGE_INVALIDATION_END_PPN	0x1028
#define REG_NS_FAULT_STATUS			0x2000
#define REG_NS_FAULT_PA_LOW(vid)		(0x2004 + ((vid) * 0x20))
#define REG_NS_FAULT_PA_HIGH(vid)		(0x2008 + ((vid) * 0x20))
#define REG_NS_FAULT_INFO(vid)			(0x2010 + ((vid) * 0x20))
#define REG_NS_L1ENTRY_L2TABLE_ADDR(vid, gb)	(0x4000 + ((vid) * 0x200) + ((gb) * 0x8))
#define REG_NS_L1ENTRY_ATTR(vid, gb)		(0x4004 + ((vid) * 0x200) + ((gb) * 0x8))

#define CTRL0_ENABLE				BIT(0)
#define CTRL0_INTERRUPT_ENABLE			BIT(1)
#define CTRL0_FAULT_RESP_TYPE_SLVERR		BIT(2) /* for v8 */
#define CTRL0_FAULT_RESP_TYPE_DECERR		BIT(2) /* for v9 */

#define CTRL1_DISABLE_CHK_S1L1PTW		BIT(0)
#define CTRL1_DISABLE_CHK_S1L2PTW		BIT(1)
#define CTRL1_ENABLE_PAGE_SIZE_AWARENESS	BIT(2)
#define CTRL1_DISABLE_CHK_USER_MATCHED_REQ	BIT(3)

#define CFG_MPTW_CACHE_OVERRIDE			BIT(0)
#define CFG_MPTW_QOS_OVERRIDE			BIT(8)
#define CFG_MPTW_SHAREABLE			BIT(16)

/* For use with hi_lo_readq_relaxed(). */
#define REG_NS_FAULT_PA_HIGH_LOW(vid)		REG_NS_FAULT_PA_LOW(vid)

/* Mask used for extracting VID from FAULT_* register offset. */
#define REG_NS_FAULT_VID_MASK			GENMASK(7, 5)

#define VERSION_MAJOR_ARCH_VER_MASK		GENMASK(31, 28)
#define VERSION_MINOR_ARCH_VER_MASK		GENMASK(27, 24)
#define VERSION_REV_ARCH_VER_MASK		GENMASK(23, 16)
#define VERSION_RTL_VER_MASK			GENMASK(7, 0)

/* Ignore RTL version in driver version check. */
#define VERSION_CHECK_MASK			(VERSION_MAJOR_ARCH_VER_MASK | \
						 VERSION_MINOR_ARCH_VER_MASK | \
						 VERSION_REV_ARCH_VER_MASK)

#define STATUS_BUSY				BIT(0)
#define STATUS_ON_INVALIDATING			BIT(1)

#define NUM_CONTEXT_MASK			GENMASK(3, 0)

#define CONTEXT_CFG_VALID_VID_CTX_VALID(ctx)	BIT((4 * (ctx)) + 3)
#define CONTEXT_CFG_VALID_VID_CTX_VID(ctx, vid)	\
		FIELD_PREP(GENMASK((4 * (ctx) + 2), 4 * (ctx)), (vid))

#define INVALIDATION_INVALIDATE			BIT(0)
#define RANGE_INVALIDATION_PPN_SHIFT		12

#define NR_FAULT_INFO_REGS			8
#define FAULT_INFO_VID_MASK			GENMASK(26, 24)
#define FAULT_INFO_TYPE_MASK			GENMASK(23, 21)
#define FAULT_INFO_TYPE_CONTEXT			0x4 /* v9 only */
#define FAULT_INFO_TYPE_AP			0x2
#define FAULT_INFO_TYPE_MPTW			0x1
#define FAULT_INFO_RW_BIT			BIT(20)
#define FAULT_INFO_LEN_MASK			GENMASK(19, 16)
#define FAULT_INFO_ID_MASK			GENMASK(15, 0)

#define L1ENTRY_L2TABLE_ADDR(pa)		((pa) >> 4)

#define L1ENTRY_ATTR_L2TABLE_EN			BIT(0)
#define L1ENTRY_ATTR_GRAN_4K			0x0
#define L1ENTRY_ATTR_GRAN_64K			0x1
#define L1ENTRY_ATTR_GRAN_2M			0x2
#define L1ENTRY_ATTR_PROT(prot)			FIELD_PREP(GENMASK(2, 1), prot)
#define L1ENTRY_ATTR_GRAN(gran)			FIELD_PREP(GENMASK(5, 4), gran)
#define L1ENTRY_ATTR_1G(prot)			L1ENTRY_ATTR_PROT(prot)
#define L1ENTRY_ATTR_L2(gran)			(L1ENTRY_ATTR_GRAN(gran) | \
						 L1ENTRY_ATTR_L2TABLE_EN)

#define NR_GIGABYTES				64
#define RO_GIGABYTES_FIRST			4
#define RO_GIGABYTES_LAST			33
#define NR_RO_GIGABYTES				(RO_GIGABYTES_LAST - RO_GIGABYTES_FIRST + 1)
#define NR_RW_GIGABYTES				(NR_GIGABYTES - NR_RO_GIGABYTES)

#ifdef CONFIG_ARM64_64K_PAGES
#define SMPT_GRAN				SZ_64K
#define SMPT_GRAN_ATTR				L1ENTRY_ATTR_GRAN_64K
#else
#define SMPT_GRAN				SZ_4K
#define SMPT_GRAN_ATTR				L1ENTRY_ATTR_GRAN_4K
#endif
static_assert(SMPT_GRAN <= PAGE_SIZE);

#define MPT_PROT_BITS				2
#define SMPT_WORD_SIZE				sizeof(u32)
#define SMPT_ELEMS_PER_BYTE			(BITS_PER_BYTE / MPT_PROT_BITS)
#define SMPT_ELEMS_PER_WORD			(SMPT_WORD_SIZE * SMPT_ELEMS_PER_BYTE)
#define SMPT_WORD_BYTE_RANGE			(SMPT_GRAN * SMPT_ELEMS_PER_WORD)
#define SMPT_NUM_ELEMS				(SZ_1G / SMPT_GRAN)
#define SMPT_SIZE				(SMPT_NUM_ELEMS / SMPT_ELEMS_PER_BYTE)
#define SMPT_NUM_WORDS				(SMPT_SIZE / SMPT_WORD_SIZE)
#define SMPT_NUM_PAGES				(SMPT_SIZE / PAGE_SIZE)
#define SMPT_ORDER				get_order(SMPT_SIZE)

/* SysMMU_SYNC registers, relative to SYSMMU_SYNC_S2_OFFSET. */
#define REG_NS_SYNC_CMD				0x0
#define REG_NS_SYNC_COMP			0x4

#define SYNC_CMD_SYNC				BIT(0)
#define SYNC_COMP_COMPLETE			BIT(0)

/*
 * Iterate over S2MPU gigabyte regions. Skip those that cannot be modified
 * (the MMIO registers are read only, with reset value MPT_PROT_NONE).
 */
#define for_each_gb_in_range(i, first, last) \
	for ((i) = (first); (i) <= (last) && (i) < NR_GIGABYTES; \
	     (i) = (((i) + 1 == RO_GIGABYTES_FIRST) ? RO_GIGABYTES_LAST : (i)) + 1)

#define for_each_gb(i)			for_each_gb_in_range(i, 0, NR_GIGABYTES - 1)
#define for_each_vid(i)			for ((i) = 0; (i) < NR_VIDS; (i)++)
#define for_each_gb_and_vid(gb, vid)	for_each_vid((vid)) for_each_gb((gb))

enum s2mpu_version {
	S2MPU_VERSION_8 = 0x11000000,
	S2MPU_VERSION_9 = 0x20000000,
};

enum mpt_prot {
	MPT_PROT_NONE	= 0,
	MPT_PROT_R	= BIT(0),
	MPT_PROT_W	= BIT(1),
	MPT_PROT_RW	= MPT_PROT_R | MPT_PROT_W,
	MPT_PROT_MASK	= MPT_PROT_RW,
};

static const u64 mpt_prot_doubleword[] = {
	[MPT_PROT_NONE] = 0x0000000000000000,
	[MPT_PROT_R]    = 0x5555555555555555,
	[MPT_PROT_W]	= 0xaaaaaaaaaaaaaaaa,
	[MPT_PROT_RW]   = 0xffffffffffffffff,
};

enum mpt_update_flags {
	MPT_UPDATE_L1 = BIT(0),
	MPT_UPDATE_L2 = BIT(1),
};

struct fmpt {
	u32 *smpt;
	bool gran_1g;
	enum mpt_prot prot;
	enum mpt_update_flags flags;
};

struct mpt {
	struct fmpt fmpt[NR_GIGABYTES];
};

/* Set protection bits of SMPT in a given range without using memset. */
static inline void __set_smpt_range_slow(u32 *smpt, size_t start_gb_byte,
					 size_t end_gb_byte, enum mpt_prot prot)
{
	size_t i, start_word_byte, end_word_byte, word_idx, first_elem, last_elem;
	u32 val;

	/* Iterate over u32 words. */
	start_word_byte = start_gb_byte;
	while (start_word_byte < end_gb_byte) {
		/* Determine the range of bytes covered by this word. */
		word_idx = start_word_byte / SMPT_WORD_BYTE_RANGE;
		end_word_byte = min(
			ALIGN(start_word_byte + 1, SMPT_WORD_BYTE_RANGE),
			end_gb_byte);

		/* Identify protection bit offsets within the word. */
		first_elem = (start_word_byte / SMPT_GRAN) % SMPT_ELEMS_PER_WORD;
		last_elem = ((end_word_byte - 1) / SMPT_GRAN) % SMPT_ELEMS_PER_WORD;

		/* Modify the corresponding word. */
		val = READ_ONCE(smpt[word_idx]);
		for (i = first_elem; i <= last_elem; i++) {
			val &= ~(MPT_PROT_MASK << (i * MPT_PROT_BITS));
			val |= prot << (i * MPT_PROT_BITS);
		}
		WRITE_ONCE(smpt[word_idx], val);

		start_word_byte = end_word_byte;
	}
}

/* Set protection bits of SMPT in a given range. */
static inline void __set_smpt_range(u32 *smpt, size_t start_gb_byte,
				    size_t end_gb_byte, enum mpt_prot prot)
{
	size_t interlude_start, interlude_end, interlude_bytes, word_idx;
	char prot_byte = (char)mpt_prot_doubleword[prot];

	if (start_gb_byte >= end_gb_byte)
		return;

	/* Check if range spans at least one full u32 word. */
	interlude_start = ALIGN(start_gb_byte, SMPT_WORD_BYTE_RANGE);
	interlude_end = ALIGN_DOWN(end_gb_byte, SMPT_WORD_BYTE_RANGE);

	/* If not, fall back to editing bits in the given range. */
	if (interlude_start >= interlude_end) {
		__set_smpt_range_slow(smpt, start_gb_byte, end_gb_byte, prot);
		return;
	}

	/* Use bit-editing for prologue/epilogue, memset for interlude. */
	word_idx = interlude_start / SMPT_WORD_BYTE_RANGE;
	interlude_bytes = (interlude_end - interlude_start) / SMPT_GRAN / SMPT_ELEMS_PER_BYTE;

	__set_smpt_range_slow(smpt, start_gb_byte, interlude_start, prot);
	memset(&smpt[word_idx], prot_byte, interlude_bytes);
	__set_smpt_range_slow(smpt, interlude_end, end_gb_byte, prot);
}

/* Returns true if all SMPT protection bits match 'prot'. */
static inline bool __is_smpt_uniform(u32 *smpt, enum mpt_prot prot)
{
	size_t i;
	u64 *doublewords = (u64 *)smpt;

	for (i = 0; i < SMPT_NUM_WORDS / 2; i++) {
		if (doublewords[i] != mpt_prot_doubleword[prot])
			return false;
	}
	return true;
}

/*
 * Set protection bits of FMPT/SMPT in a given range.
 * Returns flags specifying whether L1/L2 changes need to be made visible
 * to the device.
 */
static inline void __set_fmpt_range(struct fmpt *fmpt, size_t start_gb_byte,
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

#endif /* __ARM64_KVM_S2MPU_H__ */

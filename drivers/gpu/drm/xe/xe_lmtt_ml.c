// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/log2.h>
#include <linux/sizes.h>

#include "xe_lmtt_types.h"
#include "xe_macros.h"

/**
 * DOC: Multi-Level LMTT Structure
 *
 * LMHAW (Local Memory Host Address Width) is 48 bit (256TB)
 *
 * LMGAW (Local Memory Guest Address Width) is 48 bit (256TB)
 *
 * The following figure illustrates the structure and function of the ML LMTT::
 *
 *           LMTT L3 Directory
 *           (1 Entry per VF)                                       LMTT L1 Leaf
 *            +-----------+                                         +-----------+
 *            |           |             LMTT L2 (per VF)            |           |
 *            |           |              +-----------+              |           |
 *            |           |              |           |     index:   +===========+
 *            |           |              |           |     GDPA --> |    PTE    | => LMEM PF offset
 *            |           |              |           |     34:21    +===========+
 *            |           |    index:    |           |              |           |
 *            |           |    LMEM VF   +===========+              |           |
 *            |           |    offset -> |    PTE    |  ----------> +-----------+
 *            |           |    GAW-1:35  +===========+              /           \.
 *   index:   +===========+              |           |             /              \.
 *   VFID --> |    PDE    |  --------->  +-----------+            /                 \.
 *            +===========+             /           /            /                    \.
 *            |           |           /            /            /                       \.
 *            +-----------+  <== [LMTT Directory Ptr]          /                          \.
 *           /             \      /              /            /                             \.
 *          /                \  /               /       +-----------+-----------------+------+---+
 *         /                  /\               /        | 31:HAW-16 |        HAW-17:5 |  4:1 | 0 |
 *        /                 /    \            /         +===========+=================+======+===+
 *       /                /        \         /          |  Reserved | LMEM Page (2MB) | Rsvd | V |
 *      /                                   /           +-----------+-----------------+------+---+
 *     /                                   /
 *  +-----------+-----------------+------+---+
 *  | 63:HAW-12 |        HAW-13:4 |  3:1 | 0 |
 *  +===========+=================+======+===+
 *  |  Reserved | LMTT Ptr (64KB) | Rsvd | V |
 *  +-----------+-----------------+------+---+
 *
 */

typedef u64 lmtt_ml_pde_t;
typedef u32 lmtt_ml_pte_t;

#define LMTT_ML_HAW			48 /* 256 TiB */

#define LMTT_ML_PDE_MAX_NUM		64 /* SRIOV with PF and 63 VFs, index 0 (PF) is unused */
#define LMTT_ML_PDE_LMTT_PTR		GENMASK_ULL(LMTT_ML_HAW - 13, 4)
#define LMTT_ML_PDE_VALID		BIT(0)

#define LMTT_ML_PDE_L2_SHIFT		35
#define LMTT_ML_PDE_L2_MAX_NUM		BIT_ULL(LMTT_ML_HAW - 35)

#define LMTT_ML_PTE_MAX_NUM		BIT(35 - ilog2(SZ_2M))
#define LMTT_ML_PTE_LMEM_PAGE		GENMASK(LMTT_ML_HAW - 17, 5)
#define LMTT_ML_PTE_VALID		BIT(0)

static unsigned int lmtt_ml_root_pd_level(void)
{
	return 2; /* implementation is 0-based */
}

static unsigned int lmtt_ml_pte_num(unsigned int level)
{
	switch (level) {
	case 2:
		return LMTT_ML_PDE_MAX_NUM;
	case 1:
		BUILD_BUG_ON(LMTT_ML_HAW == 48 && LMTT_ML_PDE_L2_MAX_NUM != SZ_8K);
		return LMTT_ML_PDE_L2_MAX_NUM;
	case 0:
		BUILD_BUG_ON(LMTT_ML_PTE_MAX_NUM != SZ_16K);
		return LMTT_ML_PTE_MAX_NUM;
	default:
		return 0;
	}
}

static unsigned int lmtt_ml_pte_size(unsigned int level)
{
	switch (level) {
	case 2:
	case 1:
		return sizeof(lmtt_ml_pde_t);
	case 0:
		return sizeof(lmtt_ml_pte_t);
	default:
		return 0;
	}
}

static unsigned int lmtt_ml_pte_shift(unsigned int level)
{
	switch (level) {
	case 1:
		BUILD_BUG_ON(BIT_ULL(LMTT_ML_PDE_L2_SHIFT) != SZ_32G);
		return ilog2(SZ_32G);
	case 0:
		return ilog2(SZ_2M);
	default:
		return 0;
	}
}

static unsigned int lmtt_ml_pte_index(u64 addr, unsigned int level)
{
	addr >>= lmtt_ml_pte_shift(level);

	switch (level) {
	case 1:
		/* SZ_32G increments */
		BUILD_BUG_ON_NOT_POWER_OF_2(LMTT_ML_PDE_L2_MAX_NUM);
		return addr & (LMTT_ML_PDE_L2_MAX_NUM - 1);
	case 0:
		/* SZ_2M increments */
		BUILD_BUG_ON_NOT_POWER_OF_2(LMTT_ML_PTE_MAX_NUM);
		return addr & (LMTT_ML_PTE_MAX_NUM - 1);
	default:
		return 0;
	}
}

static u64 lmtt_ml_pte_encode(unsigned long offset, unsigned int level)
{
	switch (level) {
	case 0:
		XE_WARN_ON(!IS_ALIGNED(offset, SZ_2M));
		XE_WARN_ON(!FIELD_FIT(LMTT_ML_PTE_LMEM_PAGE, offset / SZ_2M));
		return FIELD_PREP(LMTT_ML_PTE_LMEM_PAGE, offset / SZ_2M) | LMTT_ML_PTE_VALID;
	case 1:
	case 2:
		XE_WARN_ON(!IS_ALIGNED(offset, SZ_64K));
		XE_WARN_ON(!FIELD_FIT(LMTT_ML_PDE_LMTT_PTR, offset / SZ_64K));
		return FIELD_PREP(LMTT_ML_PDE_LMTT_PTR, offset / SZ_64K) | LMTT_ML_PDE_VALID;
	default:
		XE_WARN_ON(true);
		return 0;
	}
}

const struct xe_lmtt_ops lmtt_ml_ops = {
	.lmtt_root_pd_level = lmtt_ml_root_pd_level,
	.lmtt_pte_num = lmtt_ml_pte_num,
	.lmtt_pte_size = lmtt_ml_pte_size,
	.lmtt_pte_shift = lmtt_ml_pte_shift,
	.lmtt_pte_index = lmtt_ml_pte_index,
	.lmtt_pte_encode = lmtt_ml_pte_encode,
};

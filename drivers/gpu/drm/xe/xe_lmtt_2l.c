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
 * DOC: Two-Level LMTT Structure
 *
 * LMHAW (Local Memory Host Address Width) is 37 bit (128GB)
 *
 * LMGAW (Local Memory Guest Address Width) is 37 bit (128GB)
 *
 * The following figure illustrates the structure and function of the 2L LMTT::
 *
 *            LMTT Directory
 *           (1 Entry per VF)
 *            +-----------+                     LMTT (per VF)
 *            |           |                     +-----------+
 *            |           |                     |           |
 *            |           |          index:     |           |
 *            |           |          LMEM VF    +===========+
 *            |           |          offset --> |    PTE    | ==> LMEM PF offset
 *            |           |                     +===========+
 *   index:   +===========+                     |           |
 *   VFID --> |    PDE    |  -----------------> +-----------+
 *            +===========+                    /              \.
 *            |           |                   /                 \.
 *            |           |                  /                    \.
 *            |           |                 /                       \.
 *            +-----------+ <== [LMTT Directory Ptr]                  \.
 *           /             \              /                             \.
 *          /               \         +-----------+-----------------+------+---+
 *         /                 \        | 31:HAW-16 |        HAW-17:5 |  4:1 | 0 |
 *        /                   \       +===========+=================+======+===+
 *       /                     \      |  Reserved | LMEM Page (2MB) | Rsvd | V |
 *      /                       \     +-----------+-----------------+------+---+
 *     /                         \.
 *   +-----------+-----------------+------+---+
 *   | 31:HAW-12 |        HAW-13:4 |  3:1 | 0 |
 *   +===========+=================+======+===+
 *   |  Reserved | LMTT Ptr (64KB) | Rsvd | V |
 *   +-----------+-----------------+------+---+
 *
 */

typedef u32 lmtt_2l_pde_t;
typedef u32 lmtt_2l_pte_t;

#if IS_ENABLED(CONFIG_DRM_XE_LMTT_2L_128GB)
#define LMTT_2L_HAW			37 /* 128 GiB */
#else
#define LMTT_2L_HAW			35 /* 32 GiB */
#endif

#define LMTT_2L_PDE_MAX_NUM		64 /* SRIOV with PF and 63 VFs, index 0 (PF) is unused */
#define LMTT_2L_PDE_LMTT_PTR		GENMASK(LMTT_2L_HAW - 13, 4)
#define LMTT_2L_PDE_VALID		BIT(0)

#define LMTT_2L_PTE_MAX_NUM		BIT(LMTT_2L_HAW - ilog2(SZ_2M))
#define LMTT_2L_PTE_LMEM_PAGE		GENMASK(LMTT_2L_HAW - 17, 5)
#define LMTT_2L_PTE_VALID		BIT(0)

static unsigned int lmtt_2l_root_pd_level(void)
{
	return 1; /* implementation is 0-based */
}

static unsigned int lmtt_2l_pte_num(unsigned int level)
{
	switch (level) {
	case 1:
		return LMTT_2L_PDE_MAX_NUM;
	case 0:
		BUILD_BUG_ON(LMTT_2L_HAW == 37 && LMTT_2L_PTE_MAX_NUM != SZ_64K);
		BUILD_BUG_ON(LMTT_2L_HAW == 35 && LMTT_2L_PTE_MAX_NUM != SZ_16K);
		return LMTT_2L_PTE_MAX_NUM;
	default:
		return 0;
	}
}

static unsigned int lmtt_2l_pte_size(unsigned int level)
{
	switch (level) {
	case 1:
		return sizeof(lmtt_2l_pde_t);
	case 0:
		return sizeof(lmtt_2l_pte_t);
	default:
		return 0;
	}
}

static unsigned int lmtt_2l_pte_shift(unsigned int level)
{
	switch (level) {
	case 0:
		return ilog2(SZ_2M);
	default:
		return 0;
	}
}

static unsigned int lmtt_2l_pte_index(u64 addr, unsigned int level)
{
	addr >>= lmtt_2l_pte_shift(level);

	switch (level) {
	case 0:
		/* SZ_2M increments */
		BUILD_BUG_ON_NOT_POWER_OF_2(LMTT_2L_PTE_MAX_NUM);
		return addr & (LMTT_2L_PTE_MAX_NUM - 1);
	default:
		return 0;
	}
}

static u64 lmtt_2l_pte_encode(unsigned long offset, unsigned int level)
{
	switch (level) {
	case 0:
		XE_WARN_ON(!IS_ALIGNED(offset, SZ_2M));
		XE_WARN_ON(!FIELD_FIT(LMTT_2L_PTE_LMEM_PAGE, offset / SZ_2M));
		return FIELD_PREP(LMTT_2L_PTE_LMEM_PAGE, offset / SZ_2M) | LMTT_2L_PTE_VALID;
	case 1:
		XE_WARN_ON(!IS_ALIGNED(offset, SZ_64K));
		XE_WARN_ON(!FIELD_FIT(LMTT_2L_PDE_LMTT_PTR, offset / SZ_64K));
		return FIELD_PREP(LMTT_2L_PDE_LMTT_PTR, offset / SZ_64K) | LMTT_2L_PDE_VALID;
	default:
		XE_WARN_ON(true);
		return 0;
	}
}

const struct xe_lmtt_ops lmtt_2l_ops = {
	.lmtt_root_pd_level = lmtt_2l_root_pd_level,
	.lmtt_pte_num = lmtt_2l_pte_num,
	.lmtt_pte_size = lmtt_2l_pte_size,
	.lmtt_pte_shift = lmtt_2l_pte_shift,
	.lmtt_pte_index = lmtt_2l_pte_index,
	.lmtt_pte_encode = lmtt_2l_pte_encode,
};

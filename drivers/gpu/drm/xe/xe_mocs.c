// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_mocs.h"

#include "regs/xe_gt_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_sriov.h"
#include "xe_step_types.h"

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define mocs_dbg drm_dbg
#else
__printf(2, 3)
static inline void mocs_dbg(const struct drm_device *dev,
			    const char *format, ...)
{ /* noop */ }
#endif

enum {
	HAS_GLOBAL_MOCS = BIT(0),
	HAS_LNCF_MOCS = BIT(1),
};

struct xe_mocs_entry {
	u32 control_value;
	u16 l3cc_value;
	u16 used;
};

struct xe_mocs_info {
	unsigned int size;
	unsigned int n_entries;
	const struct xe_mocs_entry *table;
	u8 uc_index;
	u8 wb_index;
	u8 unused_entries_index;
};

/* Defines for the tables (XXX_MOCS_0 - XXX_MOCS_63) */
#define _LE_CACHEABILITY(value)	((value) << 0)
#define _LE_TGT_CACHE(value)	((value) << 2)
#define LE_LRUM(value)		((value) << 4)
#define LE_AOM(value)		((value) << 6)
#define LE_RSC(value)		((value) << 7)
#define LE_SCC(value)		((value) << 8)
#define LE_PFM(value)		((value) << 11)
#define LE_SCF(value)		((value) << 14)
#define LE_COS(value)		((value) << 15)
#define LE_SSE(value)		((value) << 17)

/* Defines for the tables (LNCFMOCS0 - LNCFMOCS31) - two entries per word */
#define L3_ESC(value)		((value) << 0)
#define L3_SCC(value)		((value) << 1)
#define _L3_CACHEABILITY(value)	((value) << 4)
#define L3_GLBGO(value)		((value) << 6)
#define L3_LKUP(value)		((value) << 7)

/* Defines for the tables (GLOB_MOCS_0 - GLOB_MOCS_16) */
#define IG_PAT				REG_BIT(8)
#define L3_CACHE_POLICY_MASK		REG_GENMASK(5, 4)
#define L4_CACHE_POLICY_MASK		REG_GENMASK(3, 2)

/* Helper defines */
#define XELP_NUM_MOCS_ENTRIES	64  /* 63-64 are reserved, but configured. */
#define PVC_NUM_MOCS_ENTRIES	3
#define MTL_NUM_MOCS_ENTRIES    16
#define XE2_NUM_MOCS_ENTRIES	16

/* (e)LLC caching options */
/*
 * Note: LE_0_PAGETABLE works only up to Gen11; for newer gens it means
 * the same as LE_UC
 */
#define LE_0_PAGETABLE		_LE_CACHEABILITY(0)
#define LE_1_UC			_LE_CACHEABILITY(1)
#define LE_2_WT			_LE_CACHEABILITY(2)
#define LE_3_WB			_LE_CACHEABILITY(3)

/* Target cache */
#define LE_TC_0_PAGETABLE	_LE_TGT_CACHE(0)
#define LE_TC_1_LLC		_LE_TGT_CACHE(1)
#define LE_TC_2_LLC_ELLC	_LE_TGT_CACHE(2)
#define LE_TC_3_LLC_ELLC_ALT	_LE_TGT_CACHE(3)

/* L3 caching options */
#define L3_0_DIRECT		_L3_CACHEABILITY(0)
#define L3_1_UC			_L3_CACHEABILITY(1)
#define L3_2_RESERVED		_L3_CACHEABILITY(2)
#define L3_3_WB			_L3_CACHEABILITY(3)

/* L4 caching options */
#define L4_0_WB                 REG_FIELD_PREP(L4_CACHE_POLICY_MASK, 0)
#define L4_1_WT                 REG_FIELD_PREP(L4_CACHE_POLICY_MASK, 1)
#define L4_3_UC                 REG_FIELD_PREP(L4_CACHE_POLICY_MASK, 3)

#define XE2_L3_0_WB		REG_FIELD_PREP(L3_CACHE_POLICY_MASK, 0)
/* XD: WB Transient Display */
#define XE2_L3_1_XD		REG_FIELD_PREP(L3_CACHE_POLICY_MASK, 1)
#define XE2_L3_3_UC		REG_FIELD_PREP(L3_CACHE_POLICY_MASK, 3)

#define MOCS_ENTRY(__idx, __control_value, __l3cc_value) \
	[__idx] = { \
		.control_value = __control_value, \
		.l3cc_value = __l3cc_value, \
		.used = 1, \
	}

/*
 * MOCS tables
 *
 * These are the MOCS tables that are programmed across all the rings.
 * The control value is programmed to all the rings that support the
 * MOCS registers. While the l3cc_values are only programmed to the
 * LNCFCMOCS0 - LNCFCMOCS32 registers.
 *
 * These tables are intended to be kept reasonably consistent across
 * HW platforms, and for ICL+, be identical across OSes. To achieve
 * that, the list of entries is published as part of bspec.
 *
 * Entries not part of the following tables are undefined as far as userspace is
 * concerned and shouldn't be relied upon. The last few entries are reserved by
 * the hardware. They should be initialized according to bspec and never used.
 *
 * NOTE1: These tables are part of bspec and defined as part of the hardware
 * interface. It is expected that, for specific hardware platform, existing
 * entries will remain constant and the table will only be updated by adding new
 * entries, filling unused positions.
 *
 * NOTE2: Reserved and unspecified MOCS indices have been set to L3 WB. These
 * reserved entries should never be used. They may be changed to low performant
 * variants with better coherency in the future if more entries are needed.
 */

static const struct xe_mocs_entry gen12_mocs_desc[] = {
	/* Base - L3 + LLC */
	MOCS_ENTRY(2,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_3_WB),
	/* Base - Uncached */
	MOCS_ENTRY(3,
		   LE_1_UC | LE_TC_1_LLC,
		   L3_1_UC),
	/* Base - L3 */
	MOCS_ENTRY(4,
		   LE_1_UC | LE_TC_1_LLC,
		   L3_3_WB),
	/* Base - LLC */
	MOCS_ENTRY(5,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_1_UC),
	/* Age 0 - LLC */
	MOCS_ENTRY(6,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(1),
		   L3_1_UC),
	/* Age 0 - L3 + LLC */
	MOCS_ENTRY(7,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(1),
		   L3_3_WB),
	/* Age: Don't Chg. - LLC */
	MOCS_ENTRY(8,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(2),
		   L3_1_UC),
	/* Age: Don't Chg. - L3 + LLC */
	MOCS_ENTRY(9,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(2),
		   L3_3_WB),
	/* No AOM - LLC */
	MOCS_ENTRY(10,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1),
		   L3_1_UC),
	/* No AOM - L3 + LLC */
	MOCS_ENTRY(11,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1),
		   L3_3_WB),
	/* No AOM; Age 0 - LLC */
	MOCS_ENTRY(12,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1),
		   L3_1_UC),
	/* No AOM; Age 0 - L3 + LLC */
	MOCS_ENTRY(13,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1),
		   L3_3_WB),
	/* No AOM; Age:DC - LLC */
	MOCS_ENTRY(14,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1),
		   L3_1_UC),
	/* No AOM; Age:DC - L3 + LLC */
	MOCS_ENTRY(15,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1),
		   L3_3_WB),
	/* Self-Snoop - L3 + LLC */
	MOCS_ENTRY(18,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SSE(3),
		   L3_3_WB),
	/* Skip Caching - L3 + LLC(12.5%) */
	MOCS_ENTRY(19,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(7),
		   L3_3_WB),
	/* Skip Caching - L3 + LLC(25%) */
	MOCS_ENTRY(20,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(3),
		   L3_3_WB),
	/* Skip Caching - L3 + LLC(50%) */
	MOCS_ENTRY(21,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(1),
		   L3_3_WB),
	/* Skip Caching - L3 + LLC(75%) */
	MOCS_ENTRY(22,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(3),
		   L3_3_WB),
	/* Skip Caching - L3 + LLC(87.5%) */
	MOCS_ENTRY(23,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(7),
		   L3_3_WB),
	/* Implicitly enable L1 - HDC:L1 + L3 + LLC */
	MOCS_ENTRY(48,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_3_WB),
	/* Implicitly enable L1 - HDC:L1 + L3 */
	MOCS_ENTRY(49,
		   LE_1_UC | LE_TC_1_LLC,
		   L3_3_WB),
	/* Implicitly enable L1 - HDC:L1 + LLC */
	MOCS_ENTRY(50,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_1_UC),
	/* Implicitly enable L1 - HDC:L1 */
	MOCS_ENTRY(51,
		   LE_1_UC | LE_TC_1_LLC,
		   L3_1_UC),
	/* HW Special Case (CCS) */
	MOCS_ENTRY(60,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_1_UC),
	/* HW Special Case (Displayable) */
	MOCS_ENTRY(61,
		   LE_1_UC | LE_TC_1_LLC,
		   L3_3_WB),
	/* HW Reserved - SW program but never use */
	MOCS_ENTRY(62,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_1_UC),
	/* HW Reserved - SW program but never use */
	MOCS_ENTRY(63,
		   LE_3_WB | LE_TC_1_LLC | LE_LRUM(3),
		   L3_1_UC)
};

static const struct xe_mocs_entry dg1_mocs_desc[] = {
	/* UC */
	MOCS_ENTRY(1, 0, L3_1_UC),
	/* WB - L3 */
	MOCS_ENTRY(5, 0, L3_3_WB),
	/* WB - L3 50% */
	MOCS_ENTRY(6, 0, L3_ESC(1) | L3_SCC(1) | L3_3_WB),
	/* WB - L3 25% */
	MOCS_ENTRY(7, 0, L3_ESC(1) | L3_SCC(3) | L3_3_WB),
	/* WB - L3 12.5% */
	MOCS_ENTRY(8, 0, L3_ESC(1) | L3_SCC(7) | L3_3_WB),

	/* HDC:L1 + L3 */
	MOCS_ENTRY(48, 0, L3_3_WB),
	/* HDC:L1 */
	MOCS_ENTRY(49, 0, L3_1_UC),

	/* HW Reserved */
	MOCS_ENTRY(60, 0, L3_1_UC),
	MOCS_ENTRY(61, 0, L3_1_UC),
	MOCS_ENTRY(62, 0, L3_1_UC),
	MOCS_ENTRY(63, 0, L3_1_UC),
};

static const struct xe_mocs_entry dg2_mocs_desc[] = {
	/* UC - Coherent; GO:L3 */
	MOCS_ENTRY(0, 0, L3_1_UC | L3_LKUP(1)),
	/* UC - Coherent; GO:Memory */
	MOCS_ENTRY(1, 0, L3_1_UC | L3_GLBGO(1) | L3_LKUP(1)),
	/* UC - Non-Coherent; GO:Memory */
	MOCS_ENTRY(2, 0, L3_1_UC | L3_GLBGO(1)),

	/* WB - LC */
	MOCS_ENTRY(3, 0, L3_3_WB | L3_LKUP(1)),
};

static const struct xe_mocs_entry pvc_mocs_desc[] = {
	/* Error */
	MOCS_ENTRY(0, 0, L3_3_WB),

	/* UC */
	MOCS_ENTRY(1, 0, L3_1_UC),

	/* WB */
	MOCS_ENTRY(2, 0, L3_3_WB),
};

static const struct xe_mocs_entry mtl_mocs_desc[] = {
	/* Error - Reserved for Non-Use */
	MOCS_ENTRY(0,
		   0,
		   L3_LKUP(1) | L3_3_WB),
	/* Cached - L3 + L4 */
	MOCS_ENTRY(1,
		   IG_PAT,
		   L3_LKUP(1) | L3_3_WB),
	/* L4 - GO:L3 */
	MOCS_ENTRY(2,
		   IG_PAT,
		   L3_LKUP(1) | L3_1_UC),
	/* Uncached - GO:L3 */
	MOCS_ENTRY(3,
		   IG_PAT | L4_3_UC,
		   L3_LKUP(1) | L3_1_UC),
	/* L4 - GO:Mem */
	MOCS_ENTRY(4,
		   IG_PAT,
		   L3_LKUP(1) | L3_GLBGO(1) | L3_1_UC),
	/* Uncached - GO:Mem */
	MOCS_ENTRY(5,
		   IG_PAT | L4_3_UC,
		   L3_LKUP(1) | L3_GLBGO(1) | L3_1_UC),
	/* L4 - L3:NoLKUP; GO:L3 */
	MOCS_ENTRY(6,
		   IG_PAT,
		   L3_1_UC),
	/* Uncached - L3:NoLKUP; GO:L3 */
	MOCS_ENTRY(7,
		   IG_PAT | L4_3_UC,
		   L3_1_UC),
	/* L4 - L3:NoLKUP; GO:Mem */
	MOCS_ENTRY(8,
		   IG_PAT,
		   L3_GLBGO(1) | L3_1_UC),
	/* Uncached - L3:NoLKUP; GO:Mem */
	MOCS_ENTRY(9,
		   IG_PAT | L4_3_UC,
		   L3_GLBGO(1) | L3_1_UC),
	/* Display - L3; L4:WT */
	MOCS_ENTRY(14,
		   IG_PAT | L4_1_WT,
		   L3_LKUP(1) | L3_3_WB),
	/* CCS - Non-Displayable */
	MOCS_ENTRY(15,
		   IG_PAT,
		   L3_GLBGO(1) | L3_1_UC),
};

static const struct xe_mocs_entry xe2_mocs_table[] = {
	/* Defer to PAT */
	MOCS_ENTRY(0, XE2_L3_0_WB | L4_3_UC, 0),
	/* Cached L3, Uncached L4 */
	MOCS_ENTRY(1, IG_PAT | XE2_L3_0_WB | L4_3_UC, 0),
	/* Uncached L3, Cached L4 */
	MOCS_ENTRY(2, IG_PAT | XE2_L3_3_UC | L4_0_WB, 0),
	/* Uncached L3 + L4 */
	MOCS_ENTRY(3, IG_PAT | XE2_L3_3_UC | L4_3_UC, 0),
	/* Cached L3 + L4 */
	MOCS_ENTRY(4, IG_PAT | XE2_L3_0_WB | L4_0_WB, 0),
};

static unsigned int get_mocs_settings(struct xe_device *xe,
				      struct xe_mocs_info *info)
{
	unsigned int flags = 0;

	memset(info, 0, sizeof(struct xe_mocs_info));

	switch (xe->info.platform) {
	case XE_LUNARLAKE:
		info->size = ARRAY_SIZE(xe2_mocs_table);
		info->table = xe2_mocs_table;
		info->n_entries = XE2_NUM_MOCS_ENTRIES;
		info->uc_index = 3;
		info->wb_index = 4;
		info->unused_entries_index = 4;
		break;
	case XE_PVC:
		info->size = ARRAY_SIZE(pvc_mocs_desc);
		info->table = pvc_mocs_desc;
		info->n_entries = PVC_NUM_MOCS_ENTRIES;
		info->uc_index = 1;
		info->wb_index = 2;
		info->unused_entries_index = 2;
		break;
	case XE_METEORLAKE:
		info->size = ARRAY_SIZE(mtl_mocs_desc);
		info->table = mtl_mocs_desc;
		info->n_entries = MTL_NUM_MOCS_ENTRIES;
		info->uc_index = 9;
		info->unused_entries_index = 1;
		break;
	case XE_DG2:
		info->size = ARRAY_SIZE(dg2_mocs_desc);
		info->table = dg2_mocs_desc;
		info->uc_index = 1;
		info->n_entries = XELP_NUM_MOCS_ENTRIES;
		info->unused_entries_index = 3;
		break;
	case XE_DG1:
		info->size = ARRAY_SIZE(dg1_mocs_desc);
		info->table = dg1_mocs_desc;
		info->uc_index = 1;
		info->n_entries = XELP_NUM_MOCS_ENTRIES;
		info->unused_entries_index = 5;
		break;
	case XE_TIGERLAKE:
	case XE_ROCKETLAKE:
	case XE_ALDERLAKE_S:
	case XE_ALDERLAKE_P:
	case XE_ALDERLAKE_N:
		info->size  = ARRAY_SIZE(gen12_mocs_desc);
		info->table = gen12_mocs_desc;
		info->n_entries = XELP_NUM_MOCS_ENTRIES;
		info->uc_index = 3;
		info->unused_entries_index = 2;
		break;
	default:
		drm_err(&xe->drm, "Platform that should have a MOCS table does not.\n");
		return 0;
	}

	/*
	 * Index 0 is a reserved/unused table entry on most platforms, but
	 * even on those where it does represent a legitimate MOCS entry, it
	 * never represents the "most cached, least coherent" behavior we want
	 * to populate undefined table rows with.  So if unused_entries_index
	 * is still 0 at this point, we'll assume that it was omitted by
	 * mistake in the switch statement above.
	 */
	xe_assert(xe, info->unused_entries_index != 0);

	if (XE_WARN_ON(info->size > info->n_entries)) {
		info->table = NULL;
		return 0;
	}

	if (!IS_DGFX(xe) || GRAPHICS_VER(xe) >= 20)
		flags |= HAS_GLOBAL_MOCS;
	if (GRAPHICS_VER(xe) < 20)
		flags |= HAS_LNCF_MOCS;

	return flags;
}

/*
 * Get control_value from MOCS entry.  If the table entry is not defined, the
 * settings from unused_entries_index will be returned.
 */
static u32 get_entry_control(const struct xe_mocs_info *info,
			     unsigned int index)
{
	if (index < info->size && info->table[index].used)
		return info->table[index].control_value;
	return info->table[info->unused_entries_index].control_value;
}

static void __init_mocs_table(struct xe_gt *gt,
			      const struct xe_mocs_info *info)
{
	struct xe_device *xe = gt_to_xe(gt);

	unsigned int i;
	u32 mocs;

	mocs_dbg(&gt_to_xe(gt)->drm, "entries:%d\n", info->n_entries);
	drm_WARN_ONCE(&xe->drm, !info->unused_entries_index,
		      "Unused entries index should have been defined\n");
	for (i = 0;
	     i < info->n_entries ? (mocs = get_entry_control(info, i)), 1 : 0;
	     i++) {
		mocs_dbg(&gt_to_xe(gt)->drm, "GLOB_MOCS[%d] 0x%x 0x%x\n", i,
			 XELP_GLOBAL_MOCS(i).addr, mocs);

		if (GRAPHICS_VERx100(gt_to_xe(gt)) > 1250)
			xe_gt_mcr_multicast_write(gt, XEHP_GLOBAL_MOCS(i), mocs);
		else
			xe_mmio_write32(gt, XELP_GLOBAL_MOCS(i), mocs);
	}
}

/*
 * Get l3cc_value from MOCS entry taking into account when it's not used
 * then if unused_entries_index is not zero then its value will be returned
 * otherwise I915_MOCS_PTE's value is returned in this case.
 */
static u16 get_entry_l3cc(const struct xe_mocs_info *info,
			  unsigned int index)
{
	if (index < info->size && info->table[index].used)
		return info->table[index].l3cc_value;
	return info->table[info->unused_entries_index].l3cc_value;
}

static u32 l3cc_combine(u16 low, u16 high)
{
	return low | (u32)high << 16;
}

static void init_l3cc_table(struct xe_gt *gt,
			    const struct xe_mocs_info *info)
{
	unsigned int i;
	u32 l3cc;

	mocs_dbg(&gt_to_xe(gt)->drm, "entries:%d\n", info->n_entries);
	for (i = 0;
	     i < (info->n_entries + 1) / 2 ?
	     (l3cc = l3cc_combine(get_entry_l3cc(info, 2 * i),
				  get_entry_l3cc(info, 2 * i + 1))), 1 : 0;
	     i++) {
		mocs_dbg(&gt_to_xe(gt)->drm, "LNCFCMOCS[%d] 0x%x 0x%x\n", i, XELP_LNCFCMOCS(i).addr,
			 l3cc);

		if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1250)
			xe_gt_mcr_multicast_write(gt, XEHP_LNCFCMOCS(i), l3cc);
		else
			xe_mmio_write32(gt, XELP_LNCFCMOCS(i), l3cc);
	}
}

void xe_mocs_init_early(struct xe_gt *gt)
{
	struct xe_mocs_info table;

	get_mocs_settings(gt_to_xe(gt), &table);
	gt->mocs.uc_index = table.uc_index;
	gt->mocs.wb_index = table.wb_index;
}

void xe_mocs_init(struct xe_gt *gt)
{
	struct xe_mocs_info table;
	unsigned int flags;

	if (IS_SRIOV_VF(gt_to_xe(gt)))
		return;

	/*
	 * MOCS settings are split between "GLOB_MOCS" and/or "LNCFCMOCS"
	 * registers depending on platform.
	 *
	 * These registers should be programmed before GuC initialization
	 * since their values will affect some of the memory transactions
	 * performed by the GuC.
	 */
	flags = get_mocs_settings(gt_to_xe(gt), &table);
	mocs_dbg(&gt_to_xe(gt)->drm, "flag:0x%x\n", flags);

	if (flags & HAS_GLOBAL_MOCS)
		__init_mocs_table(gt, &table);
	if (flags & HAS_LNCF_MOCS)
		init_l3cc_table(gt, &table);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_mocs.c"
#endif
